from pathlib import Path
import gzip,hashlib,json,os,re,shutil,subprocess
h=Path('/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor');q=Path('/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc')
e=Path('/private/tmp/hm-armv8m-fault-message-whudzy7n');dest=h/'docs/analysis/evidence/health-monitor-armv8m-fault-message-20260920';dest.mkdir(exist_ok=True)
before=json.loads((e/'before.json').read_text());summary=json.loads((e/'matrix-summary.json').read_text())
assert summary['status']=='pass' and summary['total']==20
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def gz(src,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    with target.open('wb') as output:
        with gzip.GzipFile(filename='',fileobj=output,mode='wb',mtime=0) as compressed:compressed.write(src.read_bytes())
def copy(src,target):
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,target)
def git(root,*args):return subprocess.check_output(['git',*args],cwd=root)
# Keep all actual target observations, including the initial oracle adjustment.
for directory in sorted(e.iterdir()):
    if not directory.is_dir() or not (directory/'result.json').exists():continue
    for src in directory.iterdir():
        if src.is_file() and (src.name.endswith('.json') or src.name=='effective.config'):
            copy(src,dest/directory.name/src.name)
        elif src.is_file() and src.name in ['serial.log','gdb-rsp.log','build.log','fault-debug-metadata.txt']:
            gz(src,dest/directory.name/(src.name+'.gz'))
    if (directory/'bin/System.map').is_file():gz(directory/'bin/System.map',dest/directory.name/'System.map.gz')
for row in summary['cases']:
    record=json.loads(Path(row['result']).read_text())
    assert record['status']=='pass' and record['stopped_before_recovery_call']
    fault,sent,received,stop=record['events']
    assert [v['stage'] for v in record['events']]==['cpu_fault_handler','fault_sender_before_mq_send','binary_manager_after_mq_receive','stopped_before_binary_manager_recovery_call']
    assert sent['message']['raw']==received['message']['raw']
    assert received['message']['cmd']==10 and received['message']['binidx']==(1 if row['app']=='app1' else 2)
    assert sent['length']==received['length']==24 and sent['priority']==100
    assert sent['task']['binidx']==received['task']['binidx']==0 and sent['task']['pid']!=received['task']['pid']
    assert fault['task']['health_timeout']==(60000 if row['registered'] else 0)
    assert not record['recovery_executed'] and not record['unload_tested']
    trace=(Path(row['result']).parent/'gdb-rsp.log').read_text()
    requests=re.findall(r'^> (.*)$',trace,re.M)
    assert all(re.fullmatch(r'\?|[csg]|[Zz]1,[0-9a-f]+,2|m[0-9a-f]+,[0-9a-f]+',req) for req in requests),requests
    tail=trace[trace.rfind('< T05'):]
    assert not re.search(r'^> [csD]$',tail,re.M),'Resumed after final breakpoint'
    row['result']=row['name']+'/result.json'
    row['serial_log']=row['name']+'/serial.log.gz'
    row['debugger_trace']=row['name']+'/gdb-rsp.log.gz'
summary['excluded']=['binary_manager_recovery body','unload/reload','unload delayed beyond Health Monitor deadline']
(dest/'matrix-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
# Copy runnable tool sources and exact temporary orchestration commands.
for name in ['fault-message.py','fault-debug-metadata.py','README.md']:
    copy(h/'tools/qemu-armv8m-health-monitor'/name,dest/'reproduction-tools'/name)
for name in ['hm-armv8m-fault-build.py','hm-armv8m-fault-matrix.py','hm-armv8m-fault-collect.py']:
    copy(Path('/private/tmp')/name,dest/'reproduction-tools'/name)
patch=git(q,'diff','--binary')
newfiles=git(q,'ls-files','--others','--exclude-standard','-z').decode().split('\0')
for name in newfiles:
    if not name:continue
    r=subprocess.run(['git','diff','--no-index','--binary','--','/dev/null',name],cwd=q,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert r.returncode in (0,1),(name,r.stderr)
    patch+=r.stdout
(dest/'qemu-worktree.patch').write_bytes(patch)
subprocess.run(['git','apply','--reverse','--check',str(dest/'qemu-worktree.patch')],cwd=q,check=True)
# Preserve every pre-existing file except the explicitly edited test/docs files.
allowed_common={'loadable_apps/health_monitor/health_monitor_user.inc','apps/examples/health_monitor_armv8m/health_monitor_armv8m_main.c','tools/qemu-armv8m-health-monitor/README.md'}
state={}
for root in [q,h]:
    original=before[root.name]
    assert git(root,'rev-parse','HEAD').decode().strip()==original['head']
    assert not git(root,'diff','--cached','--name-only').strip()
    subprocess.run(['git','diff','--check'],cwd=root,check=True)
    changed=[]
    for name,digest in original['hashes'].items():
        if sha(root/name)!=digest:
            changed.append(name)
            allowed=set(allowed_common)
            if root==h:allowed|={'docs/analysis/QEMU_ARMv8M_Health_Monitor_Validation.md','docs/analysis/evidence/health-monitor-armv8m-20260920/evidence-index.json'}
            assert name in allowed,(root,name,'unrelated preexisting file changed')
    names=set(git(root,'diff','--name-only').decode().splitlines())|set(git(root,'ls-files','--others','--exclude-standard').decode().splitlines())
    # Source identity only; evidence-file hashes are indexed separately.
    names={n for n in names if not n.startswith('docs/analysis/evidence/')}
    state[root.name]={'head':original['head'],'branch':git(root,'branch','--show-current').decode().strip(),'index':'empty','modified_preexisting_files':changed,'source_sha256':{name:sha(root/name) for name in sorted(names) if (root/name).is_file()}}
for profile in ['hello','loadable_all','loadable_apps','xip_all']:
    assert (q/'build/configs/qemu-armv8m'/profile/'defconfig').read_bytes()==(e/(profile+'-defconfig')).read_bytes()
assert (q/'os/.config').read_bytes()==(e/'before.config').read_bytes()
restored=e/'hello-restored-build'
assert json.loads((restored/'result.json').read_text())['status']=='pass'
elf=(restored/'bin/tinyara').read_bytes()
assert all(marker not in elf for marker in [b'HM_USER CPU_FAULT',b'HM_CONTROL STATUS',b'HM_QEMU BEGIN'])
(dest/'final-source-state.json').write_text(json.dumps(state,indent=2)+'\n')
firmware={}
for name in ['loadable_all-fault-build','loadable_apps-fault-build','xip_all-fault-build','hello-restored-build']:
    firmware[name]={'source_head':json.loads((e/name/'result.json').read_text())['source_head'],'effective_config_sha256':sha(e/name/'effective.config'),'files':{p.name:sha(p) for p in sorted((e/name/'bin').iterdir()) if p.is_file()}}
(dest/'firmware-manifest.json').write_text(json.dumps(firmware,indent=2)+'\n')
verification={'status':'pass','fault_message_cases':20,'recovery_executed':False,'unload_tested':False,'debugger_target_writes':False,'stopped_before_recovery_call_in_every_case':True,'all_preexisting_unrelated_files_preserved':True,'defconfigs_restored':4,'effective_config_restored_byte_for_byte':True,'restored_hello_build':'pass','fault_fixtures_absent_from_restored_elf':True,'qemu_patch_reverse_check':'pass','both_indexes_empty':True,'commits_created_this_followup':0,'pushes':0,'raw_evidence':str(e),'prior_qemu_port_commits':['82ed01c10739ef5291c036d96d9a29c73fdb6b5c','5c0120f685ff47ab17f7ec26c0554965e312bc27']}
(dest/'verification.json').write_text(json.dumps(verification,indent=2)+'\n')
print(json.dumps(verification,indent=2))
