import json,re,subprocess
from pathlib import Path
e=Path('/private/tmp/hm-armv8m-20260920-n2j8ynnz');out=e/'static-inspection';out.mkdir(exist_ok=False)
commands={
'flat-hint':['arm-none-eabi-objdump','-d','--disassemble=health_monitor_next_check','/e/hello-final-build/bin/tinyara'],
'flat-timer':['arm-none-eabi-objdump','-d','--disassemble=health_monitor_timer','/e/hello-final-build/bin/tinyara'],
'xip-hint':['arm-none-eabi-objdump','-d','--disassemble=health_monitor_next_check','/e/xip_all-final-build/bin/tinyara'],
'user-ioctl':['arm-none-eabi-objdump','-d','--disassemble=ioctl','/e/loadable_all-lifecycle-clean-build/bin/common_dbg'],
'user-app':['arm-none-eabi-objdump','-d','/e/loadable_all-lifecycle-clean-build/bin/app1_dbg'],
'compiler':['arm-none-eabi-gcc','--version']}
result={'status':'fail','scope':'static ARMv8-M UP code and protected syscall; no SMP execution or SMP CAS proof'}
for name,cmd in commands.items():
    docker=['/usr/local/bin/docker','run','--rm','--platform','linux/arm64','--mount','type=bind,src='+str(e)+',dst=/e,readonly','tizenrt/tizenrt:2.0.1-arm64-local']+cmd
    data=subprocess.check_output(docker,stderr=subprocess.STDOUT);(out/(name+'.txt')).write_bytes(data)
for name in ('flat-hint','xip-hint'):
    data=(out/(name+'.txt')).read_text().lower()
    assert '<health_monitor_next_check>:' in data
    assert re.search(r'\blda\b',data),name+' missing native acquire load'
    assert '__atomic' not in data and '__sync' not in data
ioctl=(out/'user-ioctl.txt').read_text().lower()
assert '<ioctl>:' in ioctl and re.search(r'\bsvc\s+(?:0x0+|0)\b',ioctl), 'protected ioctl lacks SVC'
app=(out/'user-app.txt').read_text().lower()
assert re.search(r'\bmrs\b[^\n]*control',app),'app lacks CONTROL observation'
result.update(status='pass',native_acquire_hint_load=True,atomic_helper_in_hint=False,protected_ioctl_svc=True,user_control_read=True)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
