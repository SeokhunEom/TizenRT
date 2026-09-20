import subprocess,json,time
from pathlib import Path
root=Path('/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor')
qroot=Path('/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc')
base=Path('/private/tmp/hm-review-fixes-20260920/qemu-optimized')
base.mkdir(exist_ok=True)
results=[]
for profile in ['loadable_all','loadable_apps','xip_all']:
 for app in (['app1'] if profile=='xip_all' else ['app1','app2']):
  for kind in ['udf','mpu']:
   for unregistered in [False,True]:
    label=f'{profile}-{app}-{kind}-'+('unregistered' if unregistered else 'registered')
    output=base/label
    cmd=['/opt/homebrew/bin/python3','-O',str(root/'tools/qemu-armv8m-health-monitor/fault-message.py'),'--root',str(qroot),'--build',f'/private/tmp/hm-armv8m-fault-message-whudzy7n/{profile}-fault-build','--output',str(output),'--profile',profile,'--app',app,'--kind',kind]
    if unregistered:cmd.append('--unregistered')
    start=time.monotonic()
    with (base/(label+'.log')).open('w') as log:
     p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=90)
    result=json.loads((output/'result.json').read_text()) if (output/'result.json').exists() else {}
    results.append({'case':label,'exit_code':p.returncode,'status':result.get('status'),'elapsed_seconds':round(time.monotonic()-start,2),'command':cmd})
    (base/'matrix.json').write_text(json.dumps(results,indent=2)+'\n')
    print(label,p.returncode,result.get('status'),flush=True)
    if p.returncode:raise SystemExit(1)
