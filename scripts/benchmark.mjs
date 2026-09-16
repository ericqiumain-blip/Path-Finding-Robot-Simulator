import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync, readFileSync, readdirSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import os from 'node:os';

const root=resolve(dirname(fileURLToPath(import.meta.url)), '..');
const options={robots:'10,25,50,100,250,500,1000',schedulers:'random,nearest,cost,hungarian',ticks:'300',orders:'10000',rate:'10',seed:'42',layout:'large',output:'benchmarks/results',repeats:'1'};
for(let i=2;i<process.argv.length;i+=2) {
  const key=process.argv[i].replace(/^--/,'');
  if(!(key in options)||!process.argv[i+1]) throw new Error(`Unknown or incomplete option ${process.argv[i]}`);
  options[key]=process.argv[i+1];
}
const binary=process.env.WAREHOUSE_EXECUTABLE||resolve(root,'build/release/warehouse_sim'+(process.platform==='win32'?'.exe':''));
if(!existsSync(binary)) throw new Error('Run npm run build:engine first');
const output=resolve(root,options.output); mkdirSync(output,{recursive:true});
const rows=[];
const metadata={timestamp:new Date().toISOString(),platform:os.platform(),release:os.release(),arch:os.arch(),cpu:os.cpus()[0]?.model,logicalCpus:os.cpus().length,memoryGiB:Math.round(os.totalmem()/2**30),options,build:'Release',command:process.argv.join(' ')};
const compiler=spawnSync(existsSync(resolve(root,'.tools/w64devkit/bin/g++.exe'))?resolve(root,'.tools/w64devkit/bin/g++.exe'):'c++',['--version'],{encoding:'utf8',windowsHide:true});
metadata.compiler=compiler.status===0?compiler.stdout.split(/\r?\n/)[0]:'See CMake build configuration';
const hash=createHash('sha256');
function fingerprint(directory) {
  for(const entry of readdirSync(resolve(root,directory),{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))) {
    const path=directory+'/'+entry.name;
    if(entry.isDirectory()) fingerprint(path);
    else {hash.update(path+'\n'); hash.update(readFileSync(resolve(root,path)));}
  }
}
fingerprint('engine'); fingerprint('configs');
metadata.engineAndLayoutsSha256=hash.digest('hex');
const revision=spawnSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8',windowsHide:true});
metadata.revision=revision.status===0?revision.stdout.trim():'uncommitted workspace';
for(const robots of options.robots.split(',')) for(const scheduler of options.schedulers.split(',')) for(let repeat=0;repeat<Number(options.repeats);repeat++) {
  const args=['--robots',robots,'--scheduler',scheduler,'--layout',options.layout,'--orders',options.orders,'--initial-orders','100','--order-rate',options.rate,'--seed',options.seed,'--ticks',options.ticks];
  const result=spawnSync(binary,args,{cwd:root,encoding:'utf8',windowsHide:true,timeout:600000,maxBuffer:16*1024*1024});
  if(result.error||result.status!==0) throw new Error(`${robots}/${scheduler}: ${result.error?.message||result.stderr}`);
  const data=JSON.parse(result.stdout);
  rows.push({repeat,...data});
  writeFileSync(resolve(output,`${robots}-${scheduler}-${repeat}.json`),JSON.stringify({...data,repeat,command:[binary,...args]},null,2)+'\n');
  console.log(`${robots.padStart(4)} ${scheduler.padEnd(9)} completed=${data.metrics.ordersCompleted} throughput=${data.metrics.ordersPerHour.toFixed(1)}/h wall=${data.wallSeconds.toFixed(3)}s`);
}
writeFileSync(resolve(output,'environment.json'),JSON.stringify(metadata,null,2)+'\n');
writeFileSync(resolve(output,'results.json'),JSON.stringify(rows,null,2)+'\n');
const fields=['robots','scheduler','repeat','ordersCompleted','ordersPerHour','avgFulfillment','p95Fulfillment','utilization','totalDistance','collisionsPrevented','replans','wallSeconds','simulatedTicksPerSecond'];
const flattened=rows.map(r=>({...r.config,...r.metrics,repeat:r.repeat,wallSeconds:r.wallSeconds,simulatedTicksPerSecond:r.simulatedTicksPerSecond}));
writeFileSync(resolve(output,'results.csv'),[fields.join(','),...flattened.map(r=>fields.map(f=>r[f]).join(','))].join('\n')+'\n');
const table=['| Robots | Scheduler | Completed | Orders/h | Avg fulfillment (s) | p95 (s) | Utilization | Wall time (s) |', '| ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: |',...flattened.map(r=>`| ${r.robots} | ${r.scheduler} | ${r.ordersCompleted} | ${r.ordersPerHour.toFixed(1)} | ${r.avgFulfillment.toFixed(1)} | ${r.p95Fulfillment.toFixed(1)} | ${(100*r.utilization).toFixed(1)}% | ${r.wallSeconds.toFixed(3)} |`)];
writeFileSync(resolve(output,'SUMMARY.md'),'# Measured benchmark results\n\n'+`Generated ${metadata.timestamp}. ${metadata.cpu}; ${metadata.platform} ${metadata.arch}; Release. ${options.ticks} ticks at 1 simulated second per tick; seed ${options.seed}; layout ${options.layout}; arrivals ${options.rate}/s; initial backlog 100; cap ${options.orders}. Wall time excludes construction and JSON output.\n\n`+table.join('\n')+'\n\nFulfillment percentiles include completed orders only. These short fixed-horizon runs are a smoke/scaling experiment, not steady-state capacity estimates. Work remaining is preserved in the raw results.\n');
console.log(`Saved ${rows.length} measured runs to ${output}`);
