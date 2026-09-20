import json,subprocess,sys,time
from pathlib import Path
h=Path('/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor')
q=Path('/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc')
e=Path('/private/tmp/hm-armv8m-fault-message-whudzy7n')
rows=[]
for profile in ['loadable_all','loadable_apps','xip_all']:
    build=e/(profile+'-fault-build')
    end=time.monotonic()+120
    while not (build/'result.json').exists():
        if time.monotonic()>end:raise TimeoutError('Build not ready: '+profile)
        time.sleep(1)
    assert json.loads((build/'result.json').read_text())['status']=='pass'
    subprocess.run([sys.executable,str(h/'tools/qemu-armv8m-health-monitor/fault-debug-metadata.py'),str(build)],check=True)
    for app in (['app1'] if profile=='xip_all' else ['app1','app2']):
        for kind in ['udf','mpu']:
            for registered in [True,False]:
                name='matrix-'+profile+'-'+app+'-'+kind+('-registered' if registered else '-unregistered')
                out=e/name
                cmd=[sys.executable,str(h/'tools/qemu-armv8m-health-monitor/fault-message.py'),'--root',str(q),'--build',str(build),'--output',str(out),'--profile',profile,'--app',app,'--kind',kind]
                if not registered:cmd+=['--unregistered']
                run=subprocess.run(cmd,timeout=140)
                result=json.loads((out/'result.json').read_text())
                rows.append({'name':name,'status':result['status'],'profile':profile,'app':app,'kind':kind,'registered':registered,'result':str(out/'result.json')})
                (e/'matrix-summary.json').write_text(json.dumps({'status':'running','cases':rows},indent=2)+'\n')
                if run.returncode:raise RuntimeError('Fault-message matrix failed: '+name)
assert len(rows)==20
(e/'matrix-summary.json').write_text(json.dumps({'status':'pass','total':20,'cases':rows},indent=2)+'\n')
print('FAULT_MESSAGE_MATRIX PASS 20/20',flush=True)
