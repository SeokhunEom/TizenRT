from pathlib import Path
import subprocess,json,hashlib,re,sys,struct
base=Path(sys.argv[1]);source=base/'qemu-source';prefix='/opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-'
manifest=json.loads((base/'qemu-health-source-manifest.json').read_text())
for name,expected in manifest.items():
 if hashlib.sha256((source/name).read_bytes()).hexdigest()!=expected:raise RuntimeError('latest source drift: '+name)
results=[]
for out in sorted(base.glob('qemu-*-build')):
 state=json.loads((out/'result.json').read_text())
 if state['status']!='pass':raise RuntimeError('nonpassing build '+out.name)
 binary=out/'bin/tinyara';raw=binary.read_bytes();config=(out/'effective.config').read_text()
 if raw[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',raw,18)[0]!=40:raise RuntimeError('ELF encoding '+out.name)
 symbols=subprocess.check_output([prefix+'nm','-S',str(binary)],text=True)
 (out/'symbols.txt').write_text(symbols)
 enabled='\nCONFIG_HEALTH_MONITOR=y\n' in '\n'+config
 if bool(re.search(r'\bhealth_monitor_timer$',symbols,re.M))!=enabled:raise RuntimeError('core symbol gating '+out.name)
 if (b'HEALTH MONITOR TIMEOUT pid=' in raw)!=enabled:raise RuntimeError('product diagnostic gating '+out.name)
 result={'variant':out.name,'elf_sha256':hashlib.sha256(raw).hexdigest(),'config_sha256':hashlib.sha256(config.encode()).hexdigest(),'health_monitor_enabled':enabled,'product_log_present':enabled,'status':'pass'}
 if enabled:
  m=re.search(r'^[0-9a-f]+\s+([0-9a-f]+)\s+[bBdD]\s+g_health_heap$',symbols,re.M)
  if not m:raise RuntimeError('heap symbol missing '+out.name)
  expected=4 if 'capacity4' in out.name else int(re.search(r'^CONFIG_MAX_TASKS=(\d+)$',config,re.M).group(1))
  result['registry_capacity']=expected;result['registry_bytes']=int(m.group(1),16)
  if result['registry_bytes']!=expected*8:raise RuntimeError('registry capacity mismatch '+out.name)
 results.append(result)
if len(results)!=10:raise RuntimeError('incomplete QEMU build matrix')
runs=json.loads((base/'qemu-matrix.json').read_text())
if len(runs)!=63 or any(r['status']!='pass' for r in runs):raise RuntimeError('incomplete runtime matrix')
(base/'qemu-artifact-validation.json').write_text(json.dumps({'status':'pass','latest_health_files':len(manifest),'builds':results,'runtime_and_build_records':len(runs)},indent=2)+'\n');print('QEMU source/ELF/symbol/capacity matrix PASS')
