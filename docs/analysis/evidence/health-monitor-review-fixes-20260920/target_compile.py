from pathlib import Path
import subprocess,json
root=Path('/work');out=Path('/out/target');out.mkdir(exist_ok=True)
inc=out/'include';(inc/'tinyara').mkdir(parents=True,exist_ok=True);(inc/'arch').mkdir(exist_ok=True)
for p in (root/'os/arch/arm/include').iterdir():
 link=inc/'arch'/p.name
 if link.is_symlink():link.unlink()
 if not link.exists():link.symlink_to(p)
for name,p in [('chip',root/'os/arch/arm/include/amebasmart'),('board',root/'os/board/rtl8730e/include')]:
 link=inc/'arch'/name
 if link.is_symlink():link.unlink()
 if not link.exists():link.symlink_to(p)
subprocess.run(['cc','/work/os/tools/mkconfig.c','/work/os/tools/cfgdefine.c','-o',str(out/'mkconfig')],check=True)
config=(root/'build/configs/rtl8730e/loadable_ext_ddr_st7785/defconfig').read_text()
(out/'.config').write_text(config)
with (inc/'tinyara/config.h').open('w') as f:subprocess.run([str(out/'mkconfig'),str(out)],stdout=f,check=True)
(inc/'tinyara/version.h').write_text('#define CONFIG_VERSION_STRING "review"\n#define CONFIG_VERSION_BUILD "review"\n')
paths=[inc,root/'os/include',root/'os/kernel',root/'os/arch/arm/src/armv7-a',root/'os/arch/arm/src/common',root/'os/arch/arm/src/amebasmart',root/'os/arch/arm/src',root/'external/include',root/'framework/include',root/'os/net/lwip/src/include']
paths += [root/'os/board/rtl8730e/src/project/realtek_amebaD2_va0_example/inc/inc_ap']
paths+=sorted({p.parent for p in (root/'os/board/rtl8730e/src/component').rglob('*.h')})
# Header overlay creates the normal configured arch/chip include relationship.
(inc/'chip').symlink_to(root/'os/arch/arm/src/amebasmart') if not (inc/'chip').exists() else None
base=['/opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc','-c','-mcpu=cortex-a32','-marm','-mfloat-abi=soft','-std=gnu11','-O2','-fno-builtin','-Wall','-Werror=implicit-function-declaration','-Werror=incompatible-pointer-types','-D__KERNEL__','-D__TINYARA__','-DCONFIG_PLATFORM_TIZENRT_OS']
for p in paths:base += ['-I',str(p)]
results=[]
original_config=(inc/'tinyara/config.h').read_text()
for mode,disabled in [('default',[]),('no-debug',['CONFIG_DEBUG','CONFIG_DEBUG_ERROR']),('hm-off',['CONFIG_HEALTH_MONITOR']),('no-tick-suppress',['CONFIG_PM_TICKSUPPRESS'])]:
 (inc/'tinyara/config.h').write_text(original_config+'\n'+''.join('#undef '+name+'\n' for name in disabled))
 for source in ['os/pm/pm_idle.c','os/kernel/health_monitor/health_monitor.c','os/arch/arm/src/amebasmart/amebasmart_timerisr.c','os/arch/arm/src/amebasmart/amebasmart_idle.c']:
  if mode=='hm-off' and 'health_monitor.c' in source:continue
  label=mode+'-'+Path(source).stem
  cmd=base+[str(root/source),'-o',str(out/(label+'.o'))]
  if source.endswith('amebasmart_idle.c'):
   # Existing threads.h thrd_join void*** diagnostic, unrelated to this patch.
   cmd += ['-Wno-error=incompatible-pointer-types']
  p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  (out/(label+'.log')).write_bytes(p.stdout)
  results.append({'mode':mode,'source':source,'exit_code':p.returncode,'command':cmd})
  print(label,p.returncode,flush=True)
(inc/'tinyara/config.h').write_text(original_config)
(out/'result.json').write_text(json.dumps(results,indent=2)+'\n')
raise SystemExit(any(r['exit_code'] for r in results))
