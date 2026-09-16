import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, resolve, delimiter } from 'node:path';
import { fileURLToPath } from 'node:url';

const root=resolve(dirname(fileURLToPath(import.meta.url)), '..');
const preset=process.argv[2] || 'release';
if(!['release','debug','sanitize'].includes(preset)) throw new Error('Build must be release, debug, or sanitize');
const portable=resolve(root,'.tools/w64devkit/bin');
const env={...process.env};
if(existsSync(portable)) env.PATH=portable+delimiter+env.PATH;
function run(command,args) {
  const result=spawnSync(command,args,{cwd:root,env,stdio:'inherit',windowsHide:true});
  if(result.error) { console.error(`Cannot launch ${command}: install a C++20 compiler, CMake, and Ninja.`,result.error.message); process.exit(1); }
  if(result.status!==0) process.exit(result.status || 1);
}
run('cmake',['--preset',preset]);
run('cmake',['--build','--preset',preset,'--parallel','4']);
if(process.argv.includes('test')) run('ctest',['--preset',preset]);
