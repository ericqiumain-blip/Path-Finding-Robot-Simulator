import { chromium } from '@playwright/test';
import { PNG } from 'pngjs';
import gifenc from 'gifenc';
import { mkdir, writeFile } from 'node:fs/promises';

// Capture the real dashboard with presentation-only cropping. No robot positions
// or metrics are synthesized: every frame advances the native engine one tick.
const base = process.env.DEMO_URL || 'http://127.0.0.1:8080';
const browser = await chromium.launch({ channel: process.env.CI ? undefined : 'chrome' });
const page = await browser.newPage({ viewport: { width: 780, height: 600 }, deviceScaleFactor: 1 });
async function control(data) {
  const response = await page.request.post(base + '/api/control', { data });
  if (!response.ok()) throw new Error(await response.text());
  return response.json();
}
try {
  await control({ action: 'pause' });
  await control({ action: 'restart', config: { robots: 32, layout: 'medium', scheduler: 'nearest', seed: 42, orderRate: 1.5, failuresEnabled: false } });
  await control({ action: 'speed', speed: 50 });
  await control({ action: 'resume' });
  await page.goto(base);
  while ((await (await page.request.get(base + '/api/state')).json()).tick < 120)
    await page.waitForTimeout(100);
  await control({ action: 'pause' });
  await page.getByLabel('Select robot').selectOption('0');
  await page.addStyleTag({ content: `
    .sidebar, .topbar, .page-heading, .metrics-grid, .playback-bar,
    .detail-column, .throughput-panel, .page-footer { display: none !important; }
    main { margin: 0; padding: 0; }
    .app-shell, .main-shell, .operations-grid, .floor-column { display: block; }
    .floor-panel { width: 780px; border: 0; }
    .floor-panel > .panel-heading, .map-legend, .map-caption { display: none; }
    .canvas-container { height: 530px; }
    .capture-selector { display: flex; align-items: center; gap: 12px; padding: 12px 16px; font: 14px Arial; background: white; }
    .capture-selector select { margin: 0; width: 310px; }
  ` });
  await page.evaluate(() => {
    const row = document.createElement('div');
    row.className = 'capture-selector';
    const label = document.createElement('label');
    label.htmlFor = 'robot-selector';
    label.textContent = 'Robot:';
    row.append(label, document.querySelector('#robot-selector'));
    document.querySelector('.floor-panel').prepend(row);
  });
  const gif = gifenc.GIFEncoder();
  const palette = Array.from({ length: 256 }, (_, n) => [n, n, n]);
  let previous, changingFrames = 0, firstTick;
  for (let frame = 0; frame < 80; frame++) {
    const state = await control({ action: 'step' });
    firstTick ??= state.tick;
    if (frame === 28) await page.getByLabel('Robot:', { exact: true }).selectOption('7');
    if (frame === 54) await page.getByLabel('Robot:', { exact: true }).selectOption('12');
    await page.waitForTimeout(100);
    const screenshot = await page.locator('.floor-panel').screenshot();
    const { data, width, height } = PNG.sync.read(screenshot);
    const indices = new Uint8Array(width * height);
    for (let i = 0; i < indices.length; i++)
      indices[i] = Math.round(.2126 * data[i * 4] + .7152 * data[i * 4 + 1] + .0722 * data[i * 4 + 2]);
    if (previous && indices.some((value, i) => value !== previous[i])) changingFrames++;
    gif.writeFrame(indices, width, height, { palette: frame === 0 ? palette : undefined, delay: 100, repeat: 0 });
    previous = indices;
    if (frame === 0) {
      await mkdir('.tools', { recursive: true });
      await writeFile('.tools/gif-preview.png', screenshot);
    }
  }
  gif.finish();
  if (changingFrames < 60) throw new Error('Recording did not contain sufficient robot movement');
  await mkdir('docs/demo', { recursive: true });
  const bytes = gif.bytes();
  await writeFile('docs/demo/demo.gif', bytes);
  await writeFile('docs/demo/recording.json', JSON.stringify({ frames: 80, changingFrames, delayMs: 100, firstTick, lastTick: firstTick + 79, seed: 42, robots: 32, layout: 'medium', bytes: bytes.length }, null, 2) + '\n');
  console.log(`Saved docs/demo/demo.gif: 80 frames, ${changingFrames} changing frames, ${(bytes.length / 1024 / 1024).toFixed(2)} MiB`);
} finally {
  await control({ action: 'speed', speed: 10 });
  await control({ action: 'resume' });
  await browser.close();
}
