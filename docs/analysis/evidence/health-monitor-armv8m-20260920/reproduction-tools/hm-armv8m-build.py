import argparse,os,pathlib,subprocess,time,json,hashlib,shutil
p=argparse.ArgumentParser();p.add_argument('profile');p.add_argument('name');p.add_argument('--incremental',action='store_true');args=p.parse_args()
root=pathlib.Path('/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc');e=pathlib.Path(pathlib.Path('/private/tmp/hm-armv8m-current-path').read_text());out=e/args.name;out.mkdir()
env=dict(os.environ);env['PATH']='/opt/homebrew/bin:'+env['PATH'];env['TIZENRT_BUILD_JOBS']='4';env['TIZENRT_DOCKER_IMAGE']='tizenrt/tizenrt:2.0.1-arm64-local'
# dbuild's non-interactive arguments select the same clean/configure/build menu actions.
opts=[]
if (root/'os/.config').exists():
 if not args.incremental:opts+=['5','qemu-armv8m',args.profile]
else:opts+=['qemu-armv8m',args.profile]
opts+=['1'];cmd=['/opt/homebrew/bin/bash','./dbuild.sh']+opts
paths=set(subprocess.check_output(['git','-C',str(root),'diff','--name-only']).decode().splitlines())|set(subprocess.check_output(['git','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines())
manifest={rel:hashlib.sha256((root/rel).read_bytes()).hexdigest() for rel in sorted(paths) if (root/rel).is_file()}
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
start=time.monotonic();result={'command':cmd,'profile':args.profile,'source_head':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD']).decode().strip(),'status':'fail'}
try:
 with (out/'build.log').open('wb') as f:r=subprocess.run(cmd,cwd=root/'os',env=env,stdout=f,stderr=subprocess.STDOUT,timeout=1800)
 result['exit_code']=r.returncode
 if r.returncode==0:
  shutil.copy2(root/'os/.config',out/'effective.config')
  dest=out/'bin';dest.mkdir()
  for src in (root/'build/output/bin').iterdir():
   if src.is_file():shutil.copy2(src,dest/src.name)
  result['elf_sha256']=hashlib.sha256((dest/'tinyara').read_bytes()).hexdigest();result['status']='pass'
except Exception as exc:result['error']=str(exc)
result['elapsed_seconds']=round(time.monotonic()-start,3);(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));raise SystemExit(result['status']!='pass')
