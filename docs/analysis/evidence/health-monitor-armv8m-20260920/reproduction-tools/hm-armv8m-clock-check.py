import argparse,hashlib,json,os,re,select,socket,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--reference-timer',action='store_true');p.add_argument('--icount',action='store_true');p.add_argument('--duration',type=float,default=2);p.add_argument('--verify',action='store_true');a=p.parse_args();a.output.mkdir()
addr=re.search(r'^([0-9a-f]+) B g_system_timer\b',(a.build/'bin/System.map').read_text(),re.M).group(1)
serial=a.output/'serial.log';cmd=['/opt/homebrew/bin/qemu-system-arm','-M','mps2-an505','-kernel',str(a.build/'bin/tinyara'),'-display','none','-serial','file:'+str(serial),'-monitor','stdio','-nic','none']
if a.reference_timer:
 sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close();cmd.extend(['-S','-gdb','tcp:127.0.0.1:'+str(port)])
if a.icount:cmd.extend(['-icount','shift=auto,align=off,sleep=on'])
q=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,bufsize=0);buf=bytearray()
def monitor(command):
 q.stdin.write((command+'\n').encode());q.stdin.flush();data=bytearray();end=time.monotonic()+8
 while time.monotonic()<end:
  if select.select([q.stdout],[],[],.1)[0]:
   part=os.read(q.stdout.fileno(),65536);data.extend(part)
   if data.endswith(b'(qemu) '):break
 buf.extend(data);return data.decode(errors='replace')
r={'status':'fail','qemu_command':cmd,'firmware_sha256':hashlib.sha256((a.build/'bin/tinyara').read_bytes()).hexdigest()}
try:
 if a.reference_timer:
  for retry in range(50):
   try:g=socket.create_connection(('127.0.0.1',port),timeout=5);break
   except ConnectionRefusedError:time.sleep(.05)
  def packet():
   data=b''
   while b'$' not in data:data+=g.recv(1)
   data=b''
   while b'#' not in data:data+=g.recv(1)
   g.recv(2);g.sendall(b'+');return data[:-1].decode()
  def request(text):
   b=text.encode();g.sendall(b'$'+b+b'#'+('%02x'%(sum(b)&255)).encode());return packet()
  request('?')
  r['gdb_reference_setup']=[request('M40001008,4:ffffffff'),request('M40001000,4:01000000'),request('m40001000,10')]
  g.sendall(b'$c#63');g.recv(1)
 end=time.monotonic()+30
 while not serial.exists() or b'TASH>>' not in serial.read_bytes():
  if time.monotonic()>end:raise RuntimeError('boot timeout')
  time.sleep(.05)
 if select.select([q.stdout],[],[],1)[0]:buf.extend(os.read(q.stdout.fileno(),65536))
 r['timer_registers']=monitor('x /4wx 0x40000000');tree=monitor('info qtree');(a.output/'qtree.txt').write_text(tree)
 samples=[]
 for i in range(5):
  t=time.monotonic();v=monitor('x /1wx 0x'+addr);tick=int(re.findall(r': (0x[0-9a-f]+)',v)[-1],16);record={'seconds':t,'tick':tick}
  if a.reference_timer:
   v=monitor('x /1wx 0x40001004');record['reference_count']=int(re.findall(r': (0x[0-9a-f]+)',v)[-1],16)
  samples.append(record)
  if i < 4:time.sleep(a.duration/4)
 r['samples']=samples;r['wall_seconds_per_1000_ticks']=(samples[-1]['seconds']-samples[0]['seconds'])*1000/(samples[-1]['tick']-samples[0]['tick']);r['status']='observed'
 if a.reference_timer:
  reference=((samples[0]['reference_count']-samples[-1]['reference_count'])&0xffffffff)/20000000
  # Five samples allow reconstructing a wrap even for observations over 214 seconds.
  reference=sum(((x['reference_count']-y['reference_count'])&0xffffffff)/20000000 for x,y in zip(samples,samples[1:]))
  ticks=(samples[-1]['tick']-samples[0]['tick'])&0xffffffff
  r.update(reference_seconds=reference,os_ticks=ticks,ticks_per_reference_ms=ticks/reference/1000,reference_wrap_seen=any(y['reference_count']>x['reference_count'] for x,y in zip(samples,samples[1:])))
  if a.verify:
   assert abs(ticks-reference*1000)<10, 'OS ticks diverged from TIMER1'
   assert abs(r['wall_seconds_per_1000_ticks']-1)<.05, 'OS ticks diverged from host wall time'
   if a.duration > 214.75:assert r['reference_wrap_seen'], 'Reference counter wrap not observed'
   r['status']='pass'
except Exception as exc:
 r.update(status='fail',error=str(exc));raise
finally:
 q.terminate();q.wait(timeout=5);(a.output/'monitor.log').write_bytes(buf);(a.output/'result.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2))
