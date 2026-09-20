import hashlib,json,subprocess,time
from pathlib import Path
root=Path('/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc')
e=Path('/private/tmp/hm-armv8m-20260920-n2j8ynnz')
out=e/'host-tests-retry';out.mkdir(exist_ok=False)
cmd=['/usr/local/bin/docker','run','--rm','--platform','linux/arm64','--mount','type=bind,src='+str(root)+',dst=/work,readonly','--mount','type=bind,src='+str(out)+',dst=/out','--workdir','/work','tizenrt/tizenrt:2.0.1-arm64-local','make','-C','os/kernel/health_monitor/tests','test','OUT_DIR=/out/bin']
start=time.monotonic()
with (out/'test.log').open('wb') as log:
    r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=300)
result={'status':'pass' if r.returncode==0 else 'fail','exit_code':r.returncode,'command':cmd,'elapsed_seconds':round(time.monotonic()-start,3),'sanitizers':'address,undefined','executables':[p.name for p in sorted((out/'bin').glob('*')) if p.is_file() and not p.suffix]}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));raise SystemExit(r.returncode)
