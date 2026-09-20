import json,subprocess,time
from pathlib import Path
q=Path('/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc')
e=Path('/private/tmp/hm-armv8m-20260920-n2j8ynnz')
for profile in ('loadable_all','loadable_apps','xip_all','hello'):
    (q/'build/configs/qemu-armv8m'/profile/'defconfig').write_bytes((e/(profile+'-defconfig-before-resume')).read_bytes())
results=[]
for profile in ('loadable_all','loadable_apps','xip_all','hello'):
    build=subprocess.run(['/opt/homebrew/bin/python3','/private/tmp/hm-armv8m-build.py',profile,profile+'-final-build'])
    if build.returncode: raise SystemExit(build.returncode)
    cmd=['/opt/homebrew/bin/python3',str(q/'.github/scripts/qemu-armv8m-kernel-tc.py'),'--config',profile,'--timeout','600','--log',str(e/(profile+'-final-regression.log')),'--result',str(e/(profile+'-final-regression.json'))]
    with (e/(profile+'-final-runner.log')).open('wb') as log:
        result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=1500)
    results.append({'profile':profile,'exit_code':result.returncode})
    (e/'production-matrix.json').write_text(json.dumps(results,indent=2)+'\n')
    print('FINAL REGRESSION',profile,result.returncode,flush=True)
