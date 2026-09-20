from pathlib import Path
import subprocess,time,json,hashlib,os
base=Path(__file__).resolve().parent;root=Path('/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor')
head=json.loads((base/'initial-state.json').read_text())['health']['head'];results=[]
for mode in ['off','on','test']:
 out=base/('rtl-'+mode);out.mkdir();source=out/'source';source.mkdir()
 cmd=['git','-C',str(root),'archive',head,'os','apps','external','framework','build','lib','tools','loadable_apps','resource','docs/health-monitor']
 archive=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 extract=subprocess.run(['tar','-x','-C',str(source)],stdin=archive.stdout);archive.stdout.close();_,err=archive.communicate()
 if extract.returncode or archive.returncode:raise RuntimeError(err.decode())
 command=['/usr/local/bin/docker','run','--rm','--pull','never','--platform','linux/arm64','--network','none','--mount',f'type=bind,src={source},dst=/work','--workdir','/work/os','tizenrt/tizenrt:2.0.1-arm64-rtl8730e-local','bash','-lc',f'python3 ../docs/health-monitor/configure-validation.py /work {mode} && make -j1 JOBS=-j2']
 result={'mode':mode,'source_head':head,'source':str(source),'status':'running','command':command,'started':time.time(),'clean_source':True}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('START',mode,flush=True)
 start=time.monotonic()
 try:
  with (out/'build.log').open('wb') as log:p=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=5400)
  result['exit_code']=p.returncode;result['status']='pass' if p.returncode==0 else 'fail'
 except Exception as e:result['status']='fail';result['error']=str(e)
 result['elapsed_seconds']=round(time.monotonic()-start,3)
 if (source/'os/.config').exists():(out/'effective.config').write_bytes((source/'os/.config').read_bytes())
 result['outputs']={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source/'build/output/bin').glob('*') if p.is_file()}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');results.append(result);(base/'rtl-matrix.json').write_text(json.dumps(results,indent=2)+'\n')
 print('DONE',mode,result['status'],result['elapsed_seconds'],flush=True)
 if result['status']!='pass':raise SystemExit(1)
