from pathlib import Path
import subprocess,json,hashlib,struct,zlib,re,sys
base=Path(sys.argv[1]);mode=sys.argv[2];out=base/('rtl-'+mode);source=out/'source';binary=source/'build/output/bin';result={'mode':mode,'status':'fail'}
def check(ok,message):
 if not ok:raise RuntimeError(message)
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
config={}
for line in (out/'effective.config').read_text().splitlines():
 if line.startswith('CONFIG_') and '=' in line:
  key,value=line.split('=',1);config[key]=value.strip('"')
capacities={};kernel_addresses=[]
for prefix in ['CONFIG_FLASH','CONFIG_SECOND_FLASH']:
 names=config.get(prefix+'_PART_NAME','').strip(',').split(',');sizes=config.get(prefix+'_PART_SIZE','').strip(',').split(',')
 if not names[0]:continue
 check(len(names)==len(sizes),'partition name/size length mismatch');offset=int(config.get(prefix+'_START_ADDR','0'),0)
 for name,size in zip(names,sizes):
  size=int(size)*1024;capacities.setdefault(name,[]).append(size)
  if name=='kernel':kernel_addresses.append(offset)
  offset+=size
packages=[]
for p in sorted(binary.glob('*.trpk')):
 data=p.read_bytes();role=p.name.split('_')[0];header=struct.unpack_from('<H',data,4)[0]
 crc=struct.unpack_from('<I',data)[0];check(crc==zlib.crc32(data[4:])&0xffffffff,p.name+' CRC')
 offset=9 if role.startswith('app') else 10;payload=struct.unpack_from('<I',data,offset)[0]
 total=4096+payload if role=='resource' else 4+header+payload
 check(len(data)==total,p.name+' size');check(role in capacities,p.name+' partition absent')
 check(len(data)<=min(capacities[role]),p.name+' exceeds partition')
 if role=='resource':check(data[4+header:4096]==b'\xff'*(4096-4-header),'resource padding')
 packages.append({'name':p.name,'bytes':len(data),'header_bytes':header,'payload_bytes':payload,'partition_bytes':min(capacities[role]),'crc32_ok':True,'length_ok':True,'sha256':digest(p)})
check(len(packages)==4,'expected kernel/common/app1/resource packages')
bp=(binary/'bootparam.bin').read_bytes();check(len(bp)==8192,'bootparam size');check(struct.unpack_from('<I',bp)[0]==zlib.crc32(bp[4:4096])&0xffffffff,'bootparam CRC');check(bp[4096:]==b'\xff'*4096,'secondary bootparam erased')
check(struct.unpack_from('<II',bp,4)==(1,2),'bootparam version/format');check(bp[12]==0,'active kernel slot');check(list(struct.unpack_from('<II',bp,13))==kernel_addresses,'kernel addresses');check(bp[4095]==0,'initial update reason')
elfs={};prefix='/opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-'
for name in ['tinyara.axf','common_dbg','app1_dbg']:
 p=binary/name;data=p.read_bytes();check(data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,18)[0]==40,name+' ARM ELF32 LE')
 sizes=subprocess.check_output([prefix+'size','-A',str(p)],text=True);(out/(name+'.size.txt')).write_text(sizes)
 sections={m.group(1):int(m.group(2)) for m in re.finditer(r'^(\.[\w.]+)\s+(\d+)\s+\d+',sizes,re.M)}
 symbols=subprocess.check_output([prefix+'nm','-S',str(p)],text=True);(out/(name+'.nm.txt')).write_text(symbols)
 elfs[name]={'sha256':digest(p),'sections':sections,'arm_elf32_le':True}
 if name=='tinyara.axf':
  present=bool(re.search(r'\bhealth_monitor_timer$',symbols,re.M));check(present==(mode!='off'),'monitor symbol gating')
  check((b'HEALTH MONITOR TIMEOUT pid=' in data)==(mode!='off'),'product timeout log gating')
  check(re.search(r'\bup_timer_get_elapsedtick$',symbols,re.M) is not None,'RTL PM elapsed callback linked')
  elfs[name]['health_monitor_present']=present;elfs[name]['elapsed_callback_linked']=True
 if name=='common_dbg':check(bool(re.search(r'\bhealth_monitor_main$',symbols,re.M))==(mode=='test'),'example symbol gating')
fip=subprocess.check_output(['fiptool','info',str(binary/'fip.bin')],text=True);(out/'fip-info.txt').write_text(fip)
for name in ['BL2','BL32','BL33']:check(name in fip,'FIP '+name+' missing')
unpacked=out/'fip-unpacked';unpacked.mkdir(exist_ok=True)
subprocess.run(['fiptool','unpack','--nt-fw',str(unpacked/'bl33.bin'),str(binary/'fip.bin')],check=True)
check(digest(unpacked/'bl33.bin')==digest(binary/'ca32_image2_all.bin'),'FIP BL33 content')
log=(out/'build.log').read_text(errors='replace');check('Partition verification SUCCESS' in log and 'Size verification SUCCESS' in log,'build partition checks')
for role in ['KERNEL','COMMON','APP1','RESOURCE']:check(re.search(r'^\s*'+role+r'\s+.*PASS',log,re.M) is not None,'size check '+role)
result.update(status='pass',packages=packages,elf=elfs,bootparam={'bytes':8192,'crc32_ok':True,'secondary_erased':True,'format_version':2,'kernel_addresses':kernel_addresses,'update_reason':0},fip={'sha256':digest(binary/'fip.bin'),'bl2_bl32_bl33':True,'bl33_matches_kernel_ram':True},physical_board_tested=False)
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(mode,'artifact validation PASS',flush=True)
