import hashlib,json,subprocess
from pathlib import Path
h=Path('/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor');q=Path('/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc');e=Path('/private/tmp/hm-armv8m-20260920-n2j8ynnz')
rows=[]
for profile in ('loadable_all','loadable_apps','xip_all','hello'):
    d=e/(profile+'-final-build');cfg=(d/'effective.config').read_text()
    assert json.loads((d/'result.json').read_text())['status']=='pass'
    assert 'CONFIG_HEALTH_MONITOR=y' in cfg
    assert 'CONFIG_EXAMPLES_HEALTH_MONITOR_QEMU=y' not in cfg and 'CONFIG_EXAMPLES_HEALTH_MONITOR_ARMV8M=y' not in cfg
    assert (q/'build/configs/qemu-armv8m'/profile/'defconfig').read_bytes()==(e/(profile+'-defconfig-before-resume')).read_bytes()
    files={}
    for p in (d/'bin').iterdir():
        if p.name in ('tinyara','app1_dbg','app2_dbg','common_dbg'):
            found=[x.decode() for x in (b'HM_QEMU',b'HM_USER',b'HM_CONTROL') if x in p.read_bytes()]
            assert not found,(profile,p.name,found)
            files[p.name]=found
    r=json.loads((e/(profile+'-final-regression.json')).read_text())
    assert r['status']=='pass' and r['fail_count']==0 and r['pass_count']==(459 if profile=='hello' else 447)
    assert r['network']['network_tc_pass_count']==161 and r['network']['network_tc_fail_count']==0
    rows.append({'profile':profile,'status':'pass','fixture_strings_found':files,'network_pass':161,'kernel_pass':r['pass_count']})
for root,head in ((h,'eddceb9665e302a32e78742359669ed4ee1c8e79'),(q,'b5eacbdc0426df9ca0784dc5198ac8ff30443631')):
    assert subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD']).decode().strip()==head
    assert not subprocess.check_output(['git','-C',str(root),'diff','--cached','--name-only'])
    subprocess.run(['git','-C',str(root),'diff','--check'],check=True)
(e/'production-fixture-exclusion.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows,indent=2))
