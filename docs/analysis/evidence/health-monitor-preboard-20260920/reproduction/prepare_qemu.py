from pathlib import Path
import subprocess,json,shutil,hashlib
base=Path(__file__).resolve().parent;health=Path('/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor');origin=health.parent/'qemu-armv8m-kernel-tc';target=base/'qemu-source'
def git(root,*args):return subprocess.check_output(['git','-C',str(root),*args])
subprocess.run(['git','clone','--shared','--no-checkout',str(origin),str(target)],check=True)
head=json.loads((base/'initial-state.json').read_text())['qemu']['head'];subprocess.run(['git','-C',str(target),'checkout','--detach',head],check=True)
subprocess.run(['git','-C',str(target),'apply',str(base/'qemu-existing.patch')],check=True)
for name in git(origin,'ls-files','--others','--exclude-standard').decode().splitlines():
 src=origin/name;dst=target/name
 if src.is_file():dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
updated={}
for dirname in ['os/kernel/health_monitor','apps/examples/health_monitor','apps/examples/health_monitor_qemu','apps/examples/health_monitor_armv8m','loadable_apps/health_monitor','tools/qemu-armv8m-health-monitor','tools/qemu-build-test']:
 for name in git(health,'ls-files',dirname).decode().splitlines():
  src=health/name;dst=target/name
  if src.is_file():dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);updated[name]=hashlib.sha256(src.read_bytes()).hexdigest()
for name in ['os/pm/pm_idle.c','os/include/tinyara/pm/pm.h','os/arch/arm/src/amebasmart/amebasmart_idle.c','os/arch/arm/src/amebasmart/amebasmart_timerisr.c','os/arch/arm/src/amebasmart/arch_timer.h','os/drivers/health_monitor.c','os/include/tinyara/health_monitor.h']:
 shutil.copy2(health/name,target/name);updated[name]=hashlib.sha256((health/name).read_bytes()).hexdigest()
(base/'qemu-health-source-manifest.json').write_text(json.dumps(updated,indent=2)+'\n')
(base/'qemu-latest-overlay.patch').write_bytes(git(target,'diff','--binary','HEAD'))
(base/'qemu-status-before-build.txt').write_bytes(git(target,'status','--short'))
print(target,'latest HM files:',len(updated),flush=True)
