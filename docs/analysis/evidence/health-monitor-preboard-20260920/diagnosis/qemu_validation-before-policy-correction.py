from pathlib import Path
import subprocess,json,shutil,hashlib,time,os,re,sys,importlib.util
base=Path(__file__).resolve().parent;root=base/'qemu-source';health=Path('/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor')
old=Path('/private/tmp/hm-armv8m-20260920-n2j8ynnz');fault_old=Path('/private/tmp/hm-armv8m-fault-message-whudzy7n')
env=dict(os.environ,PATH='/opt/homebrew/bin:/usr/local/bin:'+os.environ['PATH'],TIZENRT_BUILD_JOBS='2',TIZENRT_DOCKER_IMAGE='tizenrt/tizenrt:2.0.1-arm64-local',PYTHONDONTWRITEBYTECODE='1')
variants=[('hello-off','hello',old/'hello-hm-off-build/effective.config'),('hello-production','hello',old/'hello-final-build/effective.config'),('hello-probes','hello',old/'hello-stress256-build-retry/effective.config'),('hello-capacity4','hello',old/'hello-capacity4-build/effective.config')]
variants += [(p+'-production',p,old/(p+'-final-build/effective.config')) for p in ['loadable_all','loadable_apps','xip_all']]
variants += [(p+'-fault',p,fault_old/(p+'-fault-build/effective.config')) for p in ['loadable_all','loadable_apps','xip_all']]
for label,profile,cfg in variants:
 if not cfg.is_file():raise RuntimeError(str(cfg))
# Avoid simultaneous full compiler jobs with the large RTL build.
while True:
 done=base/'rtl-matrix.json'
 results=json.loads(done.read_text()) if done.exists() else []
 if any(x['status']=='fail' for x in results):raise SystemExit('RTL failure requires diagnosis before QEMU builds')
 if len(results)==3 and all(x['status']=='pass' for x in results):break
 time.sleep(10)
all_results=[]

def record(value):
 all_results.append(value);(base/'qemu-matrix.json').write_text(json.dumps(all_results,indent=2)+'\n');print(value['name'],value['status'],flush=True)

def execute(name,cmd,out,timeout=1200):
 start=time.monotonic();out.parent.mkdir(exist_ok=True,parents=True)
 with (out.parent/(out.name+'.runner.log')).open('wb') as log:p=subprocess.run(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
 result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else {}
 status='pass' if p.returncode==0 and result.get('status')=='pass' else 'fail'
 r={'name':name,'status':status,'exit_code':p.returncode,'elapsed_seconds':round(time.monotonic()-start,3),'command':cmd,'result':str(out/'result.json')}
 if status=='fail':r['error']=result.get('error','runner exited without a passing result')
 # Compare the product diagnostic to the independent fixture verdict.
 if status=='pass' and ('fatal-' in name or '-reset-' in name or '-user-expire' in name):
  text=(out/'serial.log').read_text(errors='replace')
  logs=re.findall(r'HEALTH MONITOR TIMEOUT pid=(\d+) now=(\d+) deadline=(\d+)',text)
  if len(logs)!=1:raise RuntimeError(name+': expected one product timeout diagnostic')
  pid,now,deadline=map(int,logs[0]);r['product_timeout']={'pid':pid,'now':now,'deadline':deadline}
  if ((now-deadline)&0xffffffff)>=0x80000000:raise RuntimeError(name+': early timeout log')
  if 'fatal-' in name and (pid,now,deadline)!=(result['expired_pid'],result['now'],result['deadline']):raise RuntimeError(name+': product/fixture verdict mismatch')
  if '-user-expire' in name:
   expected=int(re.search(r'pid=(\d+)',result['user_expiry_notice']).group(1))
   if pid!=expected:raise RuntimeError(name+': wrong expired user PID')
  if re.search(r'Try to recover fault|HM_CONTROL RECOVER dispatched',text):raise RuntimeError(name+': excluded recovery path executed')
 record(r)
 if status!='pass':raise SystemExit(1)

for label,profile,config in variants:
 out=base/('qemu-'+label+'-build');out.mkdir()
 (root/f'build/configs/qemu-armv8m/{profile}/defconfig').write_bytes(config.read_bytes())
 options=['5','qemu-armv8m',profile,'1'] if (root/'os/.config').exists() else ['qemu-armv8m',profile,'1']
 cmd=['/opt/homebrew/bin/bash','./dbuild.sh']+options
 print('BUILD',label,flush=True);start=time.monotonic()
 with (out/'build.log').open('wb') as log:p=subprocess.run(cmd,cwd=root/'os',env=env,stdout=log,stderr=subprocess.STDOUT,timeout=2400)
 build={'name':label+'-build','profile':profile,'status':'pass' if p.returncode==0 else 'fail','exit_code':p.returncode,'elapsed_seconds':round(time.monotonic()-start,3),'command':cmd,'health_head':json.loads((base/'initial-state.json').read_text())['health']['head'],'qemu_port_head':json.loads((base/'initial-state.json').read_text())['qemu']['head'],'clean_build':True}
 if p.returncode==0:
  shutil.copy2(root/'os/.config',out/'effective.config');shutil.copytree(root/'build/output/bin',out/'bin')
  build['firmware_sha256']=hashlib.sha256((out/'bin/tinyara').read_bytes()).hexdigest()
  if (out/'effective.config').read_bytes()!=config.read_bytes():
   # configure may normalize flags; retain exact effective config for inspection.
   build['config_normalized']=True
 (out/'result.json').write_text(json.dumps(build,indent=2)+'\n');record(build)
 if p.returncode:raise SystemExit(1)
 runner=health/'tools/qemu-armv8m-health-monitor/run.py'
 def run(case,rounds=3,ms=None):
  name=label+'-'+case+('-'+str(ms) if ms else '')
  output=base/('qemu-'+name)
  command=['/opt/homebrew/bin/python3',str(runner),'--root',str(root),'--profile',profile,'--case',case,'--rounds',str(rounds),'--output',str(output)]
  if ms:command+=['--ms',str(ms)]
  execute(name,command,output)
 if label=='hello-off':
  execute(label+'-kernel-tc',['/opt/homebrew/bin/python3',str(base/'qemu_off.py'),'--root',str(root),'--output',str(base/'qemu-hello-off-kernel-tc')],base/'qemu-hello-off-kernel-tc')
 elif label.endswith('-production'):
  run('normal');run('reset',ms=1000);run('reset',ms=2000)
 elif label=='hello-probes':
  run('all')
  for case in ['close','equality','overdue','late','wrap','stale','multi','equal','far','unstable']:run('fatal-'+case)
 elif label=='hello-capacity4':run('capacity',rounds=1)
 elif label.endswith('-fault'):
  run('user-api');run('user-expire',rounds=1)
  if profile!='xip_all':run('user-return-app2',rounds=1)
  metadata=['/opt/homebrew/bin/python3','-O',str(health/'tools/qemu-armv8m-health-monitor/fault-debug-metadata.py'),str(out)]
  with (out/'metadata.log').open('wb') as log:subprocess.run(metadata,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120,check=True)
  for app in (['app1'] if profile=='xip_all' else ['app1','app2']):
   for kind in ['udf','mpu']:
    for unregistered in [False,True]:
     name=label+'-'+app+'-'+kind+'-'+('unregistered' if unregistered else 'registered');output=base/('qemu-'+name)
     command=['/opt/homebrew/bin/python3','-O',str(health/'tools/qemu-armv8m-health-monitor/fault-message.py'),'--root',str(root),'--build',str(out),'--profile',profile,'--app',app,'--kind',kind,'--output',str(output)]
     if unregistered:command+=['--unregistered']
     execute(name,command,output,timeout=120)
print('QEMU COMPLETE',flush=True)
