import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';

test('Live API controls a real C++ subprocess', { timeout: 45000 }, async t => {
  const port=18080;
  const process=spawn(globalThis.process.execPath,['server/index.mjs'],{env:{...globalThis.process.env,PORT:String(port)},stdio:['ignore','pipe','pipe'],windowsHide:true});
  let errors=''; process.stderr.on('data',data=>{errors+=data;});
  t.after(async()=>{ process.kill(); await Promise.race([once(process,'exit'),new Promise(resolve=>setTimeout(resolve,3000))]); });
  await new Promise((resolve,reject)=>{
    const timeout=setTimeout(()=>reject(new Error('Server startup timeout: '+errors)),25000);
    process.stdout.on('data',data=>{if(data.toString().includes('Pathfinder:')){clearTimeout(timeout);resolve();}});
    process.once('exit',code=>{clearTimeout(timeout);reject(new Error(`Server exit ${code}: ${errors}`));});
    process.once('error',reject);
  });
  const base=`http://127.0.0.1:${port}`;
  const control=async data=>{
    const response=await fetch(base+'/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
    return {status:response.status,data:await response.json()};
  };
  await t.test('state contains engine data and valid topology',async()=>{
    const response=await fetch(base+'/api/state'); const state=await response.json();
    assert.equal(state.robots.length,32); assert.equal(state.warehouse.cells.length,state.warehouse.height);
    assert.equal(state.heatmap.length,state.warehouse.width*state.warehouse.height);
    assert.equal(new Set(state.robots.map(r=>`${r.x},${r.y}`)).size,32);
  });
  await t.test('pause freezes ticks and step advances exactly once',async()=>{
    const paused=await control({action:'pause'}); assert.equal(paused.data.paused,true);
    await new Promise(resolve=>setTimeout(resolve,200));
    const state=await(await fetch(base+'/api/state')).json(); assert.equal(state.tick,paused.data.tick);
    const stepped=await control({action:'step'}); assert.equal(stepped.data.tick,state.tick+1); assert.equal(stepped.data.paused,true);
  });
  await t.test('restart configures C++ and resets metrics and clock',async()=>{
    const restarted=await control({action:'restart',config:{robots:10,scheduler:'hungarian',seed:17,layout:'small',orderRate:0}});
    assert.equal(restarted.status,200); assert.equal(restarted.data.tick,0); assert.equal(restarted.data.robots.length,10);
    assert.equal(restarted.data.config.seed,17); assert.equal(restarted.data.metrics.ordersCompleted,0);
    assert.equal(restarted.data.config.scheduler,'hungarian');
  });
  await t.test('invalid inputs cannot mutate the running simulation',async()=>{
    for(const data of [{action:'speed',speed:7},{action:'restart',config:{robots:-1}},{action:'restart',config:{layout:'../../secret'}},{action:'restart',config:{orderRate:'high'}},{action:'restart',config:{unrecognized:3}}])
      assert.equal((await control(data)).status,400);
    const state=await(await fetch(base+'/api/state')).json(); assert.equal(state.robots.length,10); assert.equal(state.tick,0);
  });
  await t.test('metrics export both machine-readable formats',async()=>{
    const json=await(await fetch(base+'/api/metrics?format=json')).json(); assert.equal(json.config.robots,10);
    const csv=await(await fetch(base+'/api/metrics?format=csv')).text(); assert.match(csv,/ordersCompleted/); assert.equal(csv.trim().split('\n').length,2);
  });
  await t.test('SSE supplies live state',async()=>{
    const controller=new AbortController(); const response=await fetch(base+'/api/events',{signal:controller.signal});
    assert.match(response.headers.get('content-type'),/text\/event-stream/);
    const reader=response.body.getReader(); const {value}=await reader.read();
    assert.match(new TextDecoder().decode(value),/event: state\ndata:/); controller.abort();
  });
  await t.test('speed and resume advance the engine',async()=>{
    assert.equal((await control({action:'speed',speed:50})).data.speed,50);
    await control({action:'resume'}); await new Promise(resolve=>setTimeout(resolve,250));
    const state=(await control({action:'pause'})).data; assert.ok(state.tick>0); assert.equal(state.paused,true);
  });
  await t.test('cross-origin writes are rejected',async()=>{
    const response=await fetch(base+'/api/control',{method:'POST',headers:{'Content-Type':'application/json',Origin:'https://unrelated.example'},body:'{"action":"resume"}'});
    assert.equal(response.status,403);
  });
});
