from pathlib import Path
import subprocess,json,shutil
base=Path('/private/tmp/hm-review-fixes-20260920/metadata');base.mkdir(exist_ok=True)
results=[]
for profile in ['loadable_all','loadable_apps','xip_all']:
 saved=Path('/private/tmp/hm-armv8m-fault-message-whudzy7n')/(profile+'-fault-build')
 out=base/profile;out.mkdir(exist_ok=True)
 (out/'bin').symlink_to(saved/'bin')
 shutil.copy2(saved/'effective.config',out/'effective.config')
 cmd=['/opt/homebrew/bin/python3','-O','tools/qemu-armv8m-health-monitor/fault-debug-metadata.py',str(out)]
 with (out/'run.log').open('w') as log:p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=60)
 same=p.returncode==0 and json.loads((out/'fault-debug-metadata.json').read_text())==json.loads((saved/'fault-debug-metadata.json').read_text())
 results.append({'profile':profile,'exit_code':p.returncode,'matches_saved_metadata':same,'command':cmd})
 print(profile,p.returncode,same,flush=True)
(base/'result.json').write_text(json.dumps(results,indent=2)+'\n')
raise SystemExit(not all(r['matches_saved_metadata'] for r in results))
