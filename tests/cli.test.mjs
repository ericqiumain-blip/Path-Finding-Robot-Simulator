import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';

const binary=process.env.WAREHOUSE_EXECUTABLE||resolve('build/release/warehouse_sim'+(process.platform==='win32'?'.exe':''));
function run(args,input) { return spawnSync(binary,args,{encoding:'utf8',input,windowsHide:true,timeout:30000}); }

test('CLI emits machine-readable measured metrics',()=>{
  const result=run(['--robots','4','--orders','10','--initial-orders','10','--order-rate','0','--ticks','2000','--until-complete','--seed','42','--layout','small']);
  assert.equal(result.status,0,result.stderr); const data=JSON.parse(result.stdout);
  assert.equal(data.completedWorkload,true); assert.equal(data.metrics.ordersCompleted,10);
  assert.ok(data.wallSeconds>0); assert.ok(data.ticks<2000); assert.equal(data.config.seed,42);
});

test('CLI rejects malformed parameters without silent fallback',()=>{
  for(const args of [['--robots','-1'],['--robots','3.5'],['--order-rate','NaN'],['--seed','4294967296'],['--scheduler','unknown'],['--ticks'],['--not-a-flag'],['--layout','missing.map']]) {
    const result=run(args); assert.equal(result.status,1,args.join(' ')); assert.match(result.stderr,/warehouse_sim:/);
  }
});

test('Interactive protocol is ordered and recovers from a bad request',()=>{
  const result=run(['--interactive','--robots','4','--layout','small'],[
    '{"action":"step","ticks":5}',
    '{"action":"restart","config":{"robots":0}}',
    '{"action":"snapshot"}',
    '{"action":"restart","config":{"robots":2,"seed":13}}',
    '{"action":"fail","robotId":0,"duration":5}',
  ].join('\n')+'\n');
  assert.equal(result.status,0,result.stderr);
  const lines=result.stdout.trim().split(/\r?\n/).map(line=>JSON.parse(line));
  assert.equal(lines.length,6); assert.equal(lines[0].tick,0); assert.equal(lines[1].tick,5);
  assert.ok(lines[2].error); assert.equal(lines[3].tick,5); assert.equal(lines[3].robots.length,4);
  assert.equal(lines[4].tick,0); assert.equal(lines[4].robots.length,2); assert.equal(lines[4].config.seed,13);
  assert.equal(lines[5].robots[0].state,'FAILED'); assert.equal(lines[5].metrics.failureEvents,1);
});
