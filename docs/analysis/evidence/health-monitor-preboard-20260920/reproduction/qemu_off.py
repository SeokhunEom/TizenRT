from pathlib import Path
import argparse,importlib.util,sys,subprocess,json,re,hashlib
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.profile='hello';a.case='off';a.output.mkdir()
health=Path('/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor')
spec=importlib.util.spec_from_file_location('hm_runner',health/'tools/qemu-armv8m-health-monitor/run.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
q=m.Session(a);result={'status':'fail','health_monitor_enabled':False,'firmware_sha256':hashlib.sha256((a.root/'build/output/bin/tinyara').read_bytes()).hexdigest()}
try:
 sys.path.insert(0,str(a.root/'.github/scripts'));port=m.module('off_port',a.root/'.github/scripts/qemu-armv8m-kernel-tc.py')
 q.command=port.qemu_command(a.root,'hello');q.process=subprocess.Popen(q.command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,bufsize=0);q.selector.register(q.process.stdout,m.selectors.EVENT_READ)
 q.wait(rb'TASH>>',60);q.observe(1)
 text=q.shell('help')
 if re.search(r'\bhealth_monitor\b',text) or b'/dev/health_monitor ready' in q.data:raise RuntimeError('Health monitor leaked into OFF build')
 q.send('kernel_tc');q.wait(rb'Kernel TC Start');text=q.wait(rb'Kernel TC End \[PASS\s*:\s*\d+, FAIL\s*:\s*\d+\]',900)
 matches=re.search(r'Kernel TC End \[PASS\s*:\s*(\d+), FAIL\s*:\s*(\d+)\]',text)
 if not matches or tuple(map(int,matches.groups()))!=(459,0) or re.search(r'\] FAIL\b|TC Assertion FAIL',text):raise RuntimeError('OFF kernel suite failed')
 q.wait_for_exit('kernel_tc');q.shell('ps',['PID | PRIO']);result.update(status='pass',kernel_pass=459,kernel_fail=0)
except Exception as exc:result['error']=str(exc)
finally:
 q.finish(result);(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
raise SystemExit(result['status']!='pass')
