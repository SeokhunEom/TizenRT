from pathlib import Path
import subprocess,json,re,sys
base=Path(sys.argv[1]);rows={}
prefix='/opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-'
for mode in ['off','on','test']:
 out=base/('rtl-'+mode);elf=out/'source/build/output/bin/tinyara.axf'
 cmd=[prefix+'gdb','-nx','-q','-batch',str(elf),'-ex','printf "TCBSIZES %u %u %u\\n", (unsigned)sizeof(struct tcb_s), (unsigned)sizeof(struct task_tcb_s), (unsigned)sizeof(struct pthread_tcb_s)']
 text=subprocess.check_output(cmd,stderr=subprocess.STDOUT).decode();(out/'tcb-sizes.txt').write_text(text)
 m=re.search(r'TCBSIZES (\d+) (\d+) (\d+)',text)
 if not m:raise RuntimeError(mode+' TCB DWARF types unavailable')
 r=json.loads((out/'validation.json').read_text());r['tcb_bytes']=dict(zip(['tcb','task_tcb','pthread_tcb'],map(int,m.groups())))
 rows[mode]={'tcb_bytes':r['tcb_bytes'],'kernel_sections':r['elf']['tinyara.axf']['sections'],'common_sections':r['elf']['common_dbg']['sections']}
 (out/'validation.json').write_text(json.dumps(r,indent=2)+'\n')
for name in ['tcb','task_tcb','pthread_tcb']:
 if rows['on']['tcb_bytes'][name]-rows['off']['tcb_bytes'][name]!=8:raise RuntimeError('unexpected TCB growth '+name)
 if rows['test']['tcb_bytes'][name]!=rows['on']['tcb_bytes'][name]:raise RuntimeError('example changed TCB '+name)
(base/'rtl-memory.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps({m:r['tcb_bytes'] for m,r in rows.items()}))
