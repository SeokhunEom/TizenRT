#!/usr/bin/env python3
"""Extract fault-message breakpoints and structure offsets from a saved ARM ELF."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('build',type=Path);a=p.parse_args()
expressions={
'message_size':'sizeof(binmgr_request_t)',
'message_cmd':'&((binmgr_request_t *)0)->cmd',
'message_binidx':'&((binmgr_request_t *)0)->requester_pid',
'pid_offset':'&((struct tcb_s *)0)->pid',
'pid_size':'sizeof(((struct tcb_s *)0)->pid)',
'group_offset':'&((struct tcb_s *)0)->group',
'binidx_offset':'&((struct task_group_s *)0)->tg_binidx',
'health_timeout_offset':'&((struct tcb_s *)0)->health_monitor.timeout',
'health_deadline_offset':'&((struct tcb_s *)0)->health_monitor.deadline',
'ready_list':'&g_readytorun',
'system_timer':'&g_system_timer',
'usage_entry':'&up_usagefault',
'hard_entry':'&up_hardfault',
'mpu_entry':'&up_memfault',
'recovery_entry':'&binary_manager_recovery',
'userfault_entry':'&binary_manager_recover_userfault',
'sender_tcb':'&g_faultmsg_sender'}
command=['/usr/local/bin/docker','run','--rm','--platform','linux/arm64','-v',str(a.build.resolve()/'bin')+':/evidence:ro','tizenrt/tizenrt:2.0.1-arm64-local','/opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gdb','-q','-batch','/evidence/tinyara']
for key,value in expressions.items():command+=['-ex','printf "HMDBG '+key+'=%u\\n", (unsigned)('+value+')']
for function in ['binary_manager_faultmsg_sender','binary_manager','up_usagefault','up_memfault','up_hardfault']:
    command+=['-ex','disassemble '+function]
text=subprocess.check_output(command,stderr=subprocess.STDOUT).decode();(a.build/'fault-debug-metadata.txt').write_text(text)
meta={key:int(value) for key,value in re.findall(r'HMDBG (\w+)=(\d+)',text)}
assert set(meta)==set(expressions)
def instructions(function):
    section=text.split('Dump of assembler code for function '+function+':')[1].split('End of assembler dump.')[0]
    return [(int(addr,16),op) for addr,op in re.findall(r'^\s*(0x[0-9a-f]+) <\+\d+>:\s*(.+)$',section,re.M)]
def call(function,target):
    seq=instructions(function);hits=[i for i,(_,op) in enumerate(seq) if re.search(r'\bbl\s+.*<'+target+r'>',op)]
    assert len(hits)==1,(function,target,hits)
    i=hits[0];return seq,i
seq,i=call('binary_manager_faultmsg_sender','mq_send');meta['send_call']=seq[i][0];meta['send_return']=seq[i+1][0]
seq,i=call('binary_manager','mq_receive');meta['receive_return']=seq[i+1][0]
# Recover the receiver's stack buffer from the compiler's actual argument setup.
setup='\n'.join(op for _,op in seq[max(0,i-5):i]);offset=re.findall(r'add(?:\.w)?\s+r1, sp, #(\d+)',setup);assert len(offset)==1,setup
meta['receive_buffer_sp_offset']=int(offset[0])
seq,i=call('binary_manager','binary_manager_recovery');meta['dispatch_call']=seq[i][0]
config=(a.build/'effective.config').read_text();assert 'CONFIG_ARM_CMNVECTOR=y' in config
meta['context_pc_offset']=4*(11+(16 if '\nCONFIG_ARCH_FPU=y\n' in config else 0)+(2 if '\nCONFIG_REG_STACK_OVERFLOW_PROTECTION=y\n' in config else 1)+6)
for key in ['usage_entry','mpu_entry','hard_entry','recovery_entry','userfault_entry']:meta[key]&=~1
meta['elf_sha256']=hashlib.sha256((a.build/'bin/tinyara').read_bytes()).hexdigest()
meta['config_sha256']=hashlib.sha256((a.build/'effective.config').read_bytes()).hexdigest()
(a.build/'fault-debug-metadata.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta))
