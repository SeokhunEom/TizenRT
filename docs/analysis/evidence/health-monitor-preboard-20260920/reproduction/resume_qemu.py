from pathlib import Path
import json,shutil,re
base=Path(__file__).resolve().parent
archive=base/'diagnosis';archive.mkdir(exist_ok=True)
shutil.copy2(base/'qemu-matrix.json',archive/'matrix-before-policy-correction.json')
shutil.copy2(base/'qemu_validation.py',archive/'qemu_validation-before-policy-correction.py')
failed=base/'qemu-loadable_all-production-reset-1000';cfg=(base/'qemu-loadable_all-production-build/effective.config').read_text();text=(failed/'serial.log').read_text(errors='replace')
logs=re.findall(r'HEALTH MONITOR TIMEOUT pid=(\d+) now=(\d+) deadline=(\d+)',text)
record={'cause':'test plan requested software reset on a CONFIG_BOARD_ASSERT_SYSTEM_HALT firmware','product_change_required':False,'health_expiry_and_panic_observed':len(logs)==1,'timeout_log':logs,'config_system_halt':'CONFIG_BOARD_ASSERT_SYSTEM_HALT=y' in cfg,'config_autoreset':'CONFIG_BOARD_ASSERT_AUTORESET=y' in cfg,'original_failure':str(failed/'result.json'),'resolution':'Use expiry on HALT profiles, retaining reset tests on the AUTORESET hello profile.'}
(archive/'assert-policy.json').write_text(json.dumps(record,indent=2)+'\n')
p=base/'qemu_validation.py';s=p.read_text().replace('all_results=[]', "all_results=[r for r in json.loads((base/'qemu-matrix.json').read_text()) if r['status']=='pass']")
s=s.replace("('-reset-' in name", "('-reset-' in name")
s=s.replace("'fatal-' in name or '-reset-' in name or '-user-expire' in name", "'fatal-' in name or '-reset-' in name or '-expiry-' in name or '-user-expire' in name")
s=s.replace("for label,profile,config in variants:\n out=",'''def expected_names(label):
 if label=='hello-off':return [label+'-kernel-tc']
 if label.endswith('-production'):
  case='reset' if label.startswith('hello-') else 'expiry'
  return [label+'-normal',label+'-'+case+'-1000',label+'-'+case+'-2000']
 if label=='hello-probes':return [label+'-all']+[label+'-fatal-'+c for c in ['close','equality','overdue','late','wrap','stale','multi','equal','far','unstable']]
 if label=='hello-capacity4':return [label+'-capacity']
 return []

for label,profile,config in variants:
 completed={r['name'] for r in all_results if r['status']=='pass'}
 expected=expected_names(label)
 if expected and set(expected).issubset(completed):continue
 out=''')
s=s.replace("out=base/('qemu-'+label+'-build');out.mkdir()", "out=base/('qemu-'+label+'-build');reuse=(out/'result.json').exists();out.mkdir(exist_ok=True)")
a=s.index(" print('BUILD',label,flush=True);start=time.monotonic()")
b=s.index(" runner=health/",a)
block=s[a:b]
# Keep fresh builds unchanged; reuse only the saved build currently installed in the isolated source.
replacement=''' if reuse:
  build=json.loads((out/'result.json').read_text())
  if build['status']!='pass' or hashlib.sha256((root/'build/output/bin/tinyara').read_bytes()).hexdigest()!=build['firmware_sha256']:
   raise RuntimeError('Cannot resume a different or failed live firmware: '+label)
  if (root/'os/.config').read_bytes()!=(out/'effective.config').read_bytes():raise RuntimeError('Resume config mismatch')
 else:
'''+''.join(' '+line+'\n' for line in block.splitlines())
s=s[:a]+replacement+s[b:]
s=s.replace("  output=base/('qemu-'+name)", "  if any(r['name']==name and r['status']=='pass' for r in all_results):return\n  output=base/('qemu-'+name)")
s=s.replace("run('normal');run('reset',ms=1000);run('reset',ms=2000)", "run('normal');policy='reset' if 'CONFIG_BOARD_ASSERT_AUTORESET=y' in config.read_text() else 'expiry';run(policy,ms=1000);run(policy,ms=2000)")
p.write_text(s)
