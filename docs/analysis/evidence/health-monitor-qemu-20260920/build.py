import argparse,json,pathlib,subprocess,time,hashlib,shutil
p=argparse.ArgumentParser();p.add_argument('name');p.add_argument('--clean',action='store_true');args=p.parse_args()
out=pathlib.Path(__file__).parent/args.name;out.mkdir()
root=pathlib.Path('/Volumes/T7/Dev/TizenRT/codex/qemu-build-test')
script='set -euo pipefail\ncd /work/os\n'
if args.clean: script+='make distclean\ncd tools\n./configure.sh qemu/build_test\ncd ..\n'
script+='make -j4\ncmp .config ../build/configs/qemu/build_test/defconfig\narm-none-eabi-size ../build/output/bin/tinyara\n'
(out/'build.sh').write_text(script)
cmd=['docker','run','--rm','--pull=never','--platform','linux/arm64','--network','none','--name','hm-qemu-'+args.name,'-v',str(root)+':/work','-v',str(out)+':/evidence','sha256:8f2d15b7d82cf8c58a9092ec0dcc1ed1bbda9721a6cf19cc832c4eb9a48f8496','bash','/evidence/build.sh']
start=time.monotonic();result={'command':cmd,'clean':args.clean}
try:
 with (out/'build.log').open('wb') as f: r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=1800)
 result.update(exit_code=r.returncode,status='pass' if r.returncode==0 else 'fail')
 if r.returncode==0:
  for n in ('tinyara','tinyara.bin','tinyara.map','System.map'): shutil.copy2(root/'build/output/bin'/n,out/n)
  shutil.copy2(root/'os/.config',out/'effective.config')
  result['elf_sha256']=hashlib.sha256((out/'tinyara').read_bytes()).hexdigest()
except Exception as exc:
 result.update(status='fail',error=str(exc))
 subprocess.run(['docker','stop','--time','1','hm-qemu-'+args.name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=20)
result['elapsed_seconds']=round(time.monotonic()-start,3)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['status']=='pass' else 1)
