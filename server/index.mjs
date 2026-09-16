import http from 'node:http';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { existsSync, createReadStream } from 'node:fs';
import { readFile, stat } from 'node:fs/promises';
import { dirname, extname, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const executable = process.env.WAREHOUSE_EXECUTABLE || [
  'build/release/warehouse_sim', 'build/release/Release/warehouse_sim',
  'build/Release/warehouse_sim', 'build/warehouse_sim', 'build/debug/warehouse_sim',
].map(p => resolve(root, p + (process.platform === 'win32' ? '.exe' : ''))).find(existsSync);
if (!executable) {
  console.error('Build the C++ engine first: npm run build:engine');
  process.exit(1);
}
const port = Number(process.env.PORT || 8080);
const child = spawn(executable, ['--interactive', '--layout', 'medium', '--robots', '32', '--orders', '100000'], {
  cwd: root, stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true,
});
let state, fatal, pending, paused = false, speed = 1, busy = false, accumulator = 0;
let history = [];
let chain = Promise.resolve();
const clients = new Set();
let startupResolve, startupReject;
const ready = new Promise((res, rej) => { startupResolve = res; startupReject = rej; });
const startupTimer = setTimeout(() => startupReject(new Error('Engine startup timed out')), 30000);
function envelope() { return { ...state, paused, speed, history }; }
function publish() {
  if (!state) return;
  const data = `event: state\ndata: ${JSON.stringify(envelope())}\n\n`;
  for (const response of clients) {
    // A slow tab must never stall or grow the simulation's memory without bound.
    if (response.writableLength < 1024 * 1024) response.write(data);
  }
}
createInterface({ input: child.stdout }).on('line', line => {
  try {
    const message = JSON.parse(line);
    if (message.error) {
      if (pending) { pending.reject(new Error(message.error)); pending = undefined; }
      else startupReject(new Error(message.error));
      return;
    }
    state = message;
    if (!history.length || state.tick < history.at(-1).tick) history = [];
    if (!history.length || state.tick !== history.at(-1).tick) {
      history.push({ tick: state.tick, ordersPerHour: state.metrics.ordersPerHour, completed: state.metrics.ordersCompleted });
      if (history.length > 180) history.shift();
    }
    clearTimeout(startupTimer);
    startupResolve();
    if (pending) { pending.resolve(envelope()); pending = undefined; }
    publish();
  } catch (error) {
    fatal = `Invalid engine response: ${error.message}`;
    if (pending) { pending.reject(new Error(fatal)); pending = undefined; }
    startupReject(new Error(fatal));
  }
});
child.stderr.on('data', data => process.stderr.write(data));
function engineFailure(message) {
  fatal = message;
  startupReject(new Error(message));
  if (pending) { pending.reject(new Error(message)); pending = undefined; }
  for (const response of clients) response.end();
  clients.clear();
}
child.on('error', error => engineFailure(error.message));
child.on('exit', code => engineFailure(`Engine exited (${code})`));
function command(payload) {
  const next = chain.then(async () => {
    await ready;
    if (fatal) throw new Error(fatal);
    return new Promise((resolveCommand, reject) => {
      pending = { resolve: resolveCommand, reject };
      child.stdin.write(JSON.stringify(payload) + '\n', error => {
        if (error && pending) { pending.reject(error); pending = undefined; }
      });
    });
  });
  chain = next.catch(() => {});
  return next;
}

export function validateConfig(config) {
  if (!config || typeof config !== 'object' || Array.isArray(config)) throw new Error('config must be an object');
  const result = {};
  const ranges = { robots: [1, 1000, true], orderRate: [0, 100, false], seed: [0, 4294967295, true], failureProbability: [0, 1, false] };
  for (const [key, value] of Object.entries(config)) {
    if (ranges[key]) {
      const [min, max, integer] = ranges[key];
      if (typeof value !== 'number' || !Number.isFinite(value) || value < min || value > max || (integer && !Number.isInteger(value))) throw new Error(`Invalid ${key}`);
    } else if (key === 'scheduler') {
      if (!['random', 'nearest', 'cost', 'hungarian'].includes(value)) throw new Error('Invalid scheduler');
    } else if (key === 'layout') {
      if (!['small', 'medium', 'large', 'congested', 'procedural'].includes(value)) throw new Error('Invalid layout');
    } else if (key === 'failuresEnabled') {
      if (typeof value !== 'boolean') throw new Error('Invalid failuresEnabled');
    } else throw new Error(`Unknown config option: ${key}`);
    result[key] = value;
  }
  return result;
}
function json(response, status, payload) {
  response.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
  response.end(JSON.stringify(payload));
}
async function body(request) {
  let bytes = 0, parts = [];
  for await (const part of request) {
    bytes += part.length;
    if (bytes > 16384) throw new Error('Request too large');
    parts.push(part);
  }
  return JSON.parse(Buffer.concat(parts).toString());
}
const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon' };
const dist = resolve(root, 'frontend/dist');
const server = http.createServer(async (request, response) => {
  try {
    const url = new URL(request.url, `http://127.0.0.1:${port}`);
    if (request.method === 'POST') {
      const origin = request.headers.origin;
      if (origin && ![`http://127.0.0.1:${port}`, `http://localhost:${port}`, 'http://127.0.0.1:5173', 'http://localhost:5173'].includes(origin)) return json(response, 403, { error: 'Origin not allowed' });
    }
    if (url.pathname === '/api/health') return json(response, fatal ? 503 : 200, { ok: !fatal, engine: 'C++20', ready: !!state });
    if (fatal) return json(response, 503, { error: fatal });
    if (url.pathname === '/api/state' && request.method === 'GET') { await ready; return json(response, 200, envelope()); }
    if (url.pathname === '/api/events' && request.method === 'GET') {
      await ready;
      response.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', Connection: 'keep-alive', 'X-Accel-Buffering': 'no' });
      response.write(`event: state\ndata: ${JSON.stringify(envelope())}\n\n`);
      clients.add(response);
      request.on('close', () => clients.delete(response));
      return;
    }
    if (url.pathname === '/api/control' && request.method === 'POST') {
      const data = await body(request);
      switch (data.action) {
        case 'pause': paused = true; await chain; break;
        case 'resume': paused = false; break;
        case 'speed':
          if (![1, 5, 10, 50].includes(data.speed)) throw new Error('Speed must be 1, 5, 10, or 50');
          speed = data.speed; break;
        case 'step': paused = true; await command({ action: 'step', ticks: 1 }); break;
        case 'restart': {
          const config = validateConfig(data.config || {});
          await command({ action: 'restart', config });
          accumulator = 0; history = [{ tick: state.tick, ordersPerHour: state.metrics.ordersPerHour, completed: state.metrics.ordersCompleted }];
          break;
        }
        default: throw new Error('Unknown action');
      }
      await ready;
      publish();
      return json(response, 200, envelope());
    }
    if (url.pathname === '/api/metrics' && request.method === 'GET') {
      await ready;
      const csv = url.searchParams.get('format') === 'csv';
      response.writeHead(200, { 'Content-Type': csv ? 'text/csv' : 'application/json', 'Content-Disposition': `attachment; filename="pathfinder-metrics.${csv ? 'csv' : 'json'}"` });
      const values = { tick: state.tick, robots: state.config.robots, scheduler: state.config.scheduler, seed: state.config.seed, ...state.metrics };
      response.end(csv ? Object.keys(values).join(',') + '\n' + Object.values(values).join(',') + '\n' : JSON.stringify({ config: state.config, tick: state.tick, metrics: state.metrics }, null, 2));
      return;
    }
    if (url.pathname.startsWith('/api/')) return json(response, 404, { error: 'Unknown endpoint' });
    if (!['GET', 'HEAD'].includes(request.method)) return json(response, 405, { error: 'Method not allowed' });
    let path = resolve(dist, '.' + decodeURIComponent(url.pathname));
    if (!path.startsWith(dist + sep) && path !== dist) return json(response, 403, { error: 'Invalid path' });
    if (!existsSync(path) || (await stat(path)).isDirectory()) path = resolve(dist, 'index.html');
    if (!existsSync(path)) return json(response, 503, { error: 'Build the dashboard: npm run build:frontend; or run npm run dev:frontend' });
    response.writeHead(200, { 'Content-Type': mime[extname(path)] || 'application/octet-stream' });
    if (request.method === 'HEAD') response.end();
    else createReadStream(path).pipe(response);
  } catch (error) { if (!response.headersSent) json(response, 400, { error: error.message }); else response.end(); }
});
await ready.catch(error => { console.error(error.message); child.kill(); process.exit(1); });
server.listen(port, '127.0.0.1', () => console.log(`Pathfinder: http://127.0.0.1:${port}`));
const timer = setInterval(async () => {
  if (paused || busy || fatal) return;
  accumulator += speed / 10;
  const ticks = Math.floor(accumulator + 1e-8);
  if (!ticks) return;
  accumulator -= ticks; busy = true;
  try { await command({ action: 'step', ticks }); } catch (error) { console.error(error.message); }
  finally { busy = false; }
}, 100);
const heartbeat = setInterval(() => { for (const response of clients) response.write(': heartbeat\n\n'); }, 15000);
function shutdown() {
  clearInterval(timer); clearInterval(heartbeat); clearTimeout(startupTimer);
  for (const response of clients) response.end();
  child.kill(); server.close();
}
process.on('SIGINT', shutdown); process.on('SIGTERM', shutdown);
