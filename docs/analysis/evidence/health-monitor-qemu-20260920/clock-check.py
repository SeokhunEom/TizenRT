import argparse,json,pathlib,re,subprocess,time
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--kernel',default='/work/build/output/bin/tinyara');args=p.parse_args()
out=pathlib.Path(args.output);out.mkdir()
serial=out/'serial.log'
cmd=['qemu-system-arm','-M','lm3s6965evb','-kernel',args.kernel,'-display','none','-serial','file:'+str(serial),'-monitor','stdio','-net','none']
proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
result={'status':'fail','command':cmd}
try:
 deadline=time.monotonic()+30
 while not serial.exists() or b'execute them in TASH' not in serial.read_bytes():
  if time.monotonic()>deadline or proc.poll() is not None: raise RuntimeError('Boot incomplete')
  time.sleep(0.1)
 commands=b'x /1wx 0x400fe060\nx /1wx 0x400fe070\nx /1wx 0xe000e014\nx /1wx 0xe000e010\nquit\n'
 text=proc.communicate(commands,timeout=10)[0]
 (out/'monitor.log').write_bytes(text)
 regs={addr:int(val,16) for addr,val in re.findall(r'([0-9a-f]{8}): (0x[0-9a-f]+)',text.decode('utf-8','replace'))}
 result['registers']={a:hex(v) for a,v in regs.items()}
 cfg=pathlib.Path('/work/os/.config').read_text()
 expected_us=int(re.search(r'^CONFIG_USEC_PER_TICK=(\d+)$',cfg,re.M).group(1))
 rcc,rcc2,reload,ctrl=[regs[a] for a in ('400fe060','400fe070','e000e014','e000e010')]
 scale=5*(((rcc2>>23)&0x3f)+1) if rcc2 & (1<<31) else 5*(((rcc>>23)&15)+1)
 result.update(expected_tick_us=expected_us,actual_tick_us=(reload+1)*scale/1000.0,clock_hz=1e9/scale)
 assert ctrl&7==7,'SysTick must use the system clock and have IRQ/enabled bits'
 assert result['actual_tick_us']==expected_us,'SysTick period does not match CONFIG_USEC_PER_TICK'
 assert result['clock_hz']==50000000,'Board clock is not 50MHz'
 result['status']='pass'
except Exception as exc:
 result['error']=str(exc)
finally:
 if proc.poll() is None: proc.terminate();proc.wait(timeout=5)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result,indent=2))
raise SystemExit(0 if result['status']=='pass' else 1)
