from pathlib import Path
import gzip
import hashlib
import json
import re
import shutil
import subprocess

base = Path(__file__).resolve().parent
health = Path('/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor')
qemu = health.parent / 'qemu-armv8m-kernel-tc'
dest = health / 'docs/analysis/evidence/health-monitor-preboard-20260920'

def read(path):
    return json.loads(path.read_text())

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def require(value, message):
    if not value:
        raise RuntimeError(message)

def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args]).decode().strip()

def copy(src, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if src.suffix == '.log' or src.name.endswith('.nm.txt') or src.name == 'symbols.txt':
        target.with_name(target.name + '.gz').write_bytes(gzip.compress(src.read_bytes(), mtime=0))
    else:
        shutil.copy2(src, target)

initial = read(base / 'initial-state.json')
require(git(health, 'rev-parse', 'HEAD') == initial['health']['head'], 'health HEAD changed before documentation commit')
require(git(qemu, 'rev-parse', 'HEAD') == initial['qemu']['head'], 'original QEMU HEAD changed')
require(git(qemu, 'status', '--short') == initial['qemu']['status'].strip(), 'original QEMU status changed')
for name, expected in initial['qemu']['worktree_hashes'].items():
    require(sha(qemu / name) == expected, 'original QEMU content changed: ' + name)
for name in git(health, 'diff', '--name-only', 'HEAD').splitlines():
    require(name.startswith('docs/'), 'product/test source changed: ' + name)
for name, expected in read(base / 'qemu-health-source-manifest.json').items():
    require(sha(health / name) == sha(base / 'qemu-source' / name) == expected, 'latest source mismatch: ' + name)

matrix = read(base / 'qemu-matrix.json')
builds = [r for r in matrix if r['name'].endswith('-build')]
runs = [r for r in matrix if not r['name'].endswith('-build')]
require(len(builds) == 10 and len(runs) == 53, 'matrix counts')
require(len({r['name'] for r in matrix}) == len(matrix), 'duplicate scenarios')
require(all(r['status'] == 'pass' and r['exit_code'] == 0 for r in matrix), 'failed final record')
firmwares = {r['firmware_sha256']: r for r in builds}
for r in builds:
    require(sha(base / ('qemu-' + r['name']) / 'bin/tinyara') == r['firmware_sha256'], 'build ELF mismatch')
faults = []
kernel_pass = 0
timeouts = 0
for r in runs:
    folder = base / ('qemu-' + r['name'])
    result = read(folder / 'result.json')
    require(result['status'] == 'pass', 'failed runtime result')
    require(result['firmware_sha256'] in firmwares, 'runtime not tied to fresh ELF')
    build = base / ('qemu-' + firmwares[result['firmware_sha256']]['name'])
    if 'effective_config_sha256' in result:
        require(result['effective_config_sha256'] == sha(build / 'effective.config'), 'runtime config mismatch')
    kernel_pass += result.get('kernel_pass', 0) + result.get('tc_regression', {}).get('pass', 0)
    require(result.get('kernel_fail', 0) == result.get('tc_regression', {}).get('fail', 0) == 0, 'kernel regression failure')
    if 'recovery_executed' in result:
        require(result['recovery_executed'] is False and result['unload_tested'] is False, 'excluded recovery/unload executed')
        require(result['target_memory_or_register_writes'] is False and result['stopped_before_recovery_call'] is True, 'fault observation boundary')
        require(result['same_message_bytes_received'] is True and result['observed_message_size'] == 24, 'fault MQ mismatch')
        require('-O' in r['command'], 'fault not run with optimized Python')
        faults.append(r['name'])
    if 'product_timeout' in r:
        timeouts += 1
        pid, now, deadline = map(int, re.findall(r'HEALTH MONITOR TIMEOUT pid=(\d+) now=(\d+) deadline=(\d+)', (folder / 'serial.log').read_text())[0])
        require(r['product_timeout'] == dict(pid=pid, now=now, deadline=deadline), 'product log mismatch')
        require(((now - deadline) & 0xffffffff) < 0x80000000, 'early timeout')
require(kernel_pass == 2259 and len(faults) == 20 and timeouts == 21, 'aggregate result counts')
probes = read(base / 'qemu-hello-probes-all/result.json')
checks = sum(c['checks'] for r in probes['rounds'] for c in r['cases'])
require(checks == 10767, 'probe count')
for mode in ['off', 'on', 'test']:
    require(read(base / ('rtl-' + mode) / 'validation.json')['status'] == 'pass', 'RTL artifact failure')

for name in ['initial-state.json', 'plan.json', 'qemu-existing.patch', 'qemu-latest-overlay.patch', 'qemu-health-source-manifest.json', 'qemu-status-before-build.txt', 'qemu-matrix.json', 'qemu-artifact-validation.json', 'rtl-matrix.json', 'rtl-memory.json']:
    copy(base / name, dest / name)
for script in ['build_rtl.py', 'measure_rtl.py', 'prepare_qemu.py', 'qemu_off.py', 'qemu_validation.py', 'resume_qemu.py', 'verify_rtl.py', 'verify_qemu.py', 'package_evidence.py']:
    copy(base / script, dest / 'reproduction' / script)
for folder in [base / ('qemu-' + r['name']) for r in matrix] + [base / 'qemu-loadable_all-production-reset-1000'] + [base / ('rtl-' + m) for m in ['off', 'on', 'test']]:
    for src in folder.iterdir():
        if src.is_file() and (src.suffix in ['.json', '.log', '.txt', '.config']):
            copy(src, dest / folder.name / src.name)
    runner = folder.with_name(folder.name + '.runner.log')
    if runner.is_file():
        copy(runner, dest / folder.name / 'runner.log')
for src in (base / 'diagnosis').iterdir():
    if src.is_file():
        copy(src, dest / 'diagnosis' / src.name)

summary = {
    'status': 'pass', 'date': '2026-09-20', 'health_source_head': initial['health']['head'],
    'qemu_port_head': initial['qemu']['head'], 'latest_health_files': 74,
    'rtl_clean_builds': 3, 'qemu_clean_builds': len(builds), 'qemu_runtime_scenarios': len(runs),
    'kernel_regression_pass': kernel_pass, 'kernel_regression_fail': 0,
    'probe_condition_checks': checks, 'probe_stress_operations': 210000,
    'cpu_fault_to_mq_cases': len(faults), 'product_timeout_log_cases': timeouts,
    'resolved_test_expectation_failures': 1, 'unresolved_failures': 0,
    'binary_manager_recovery_tested': False, 'unload_delay_tested': False,
    'physical_board_tested': False, 'original_qemu_head_status_and_hashes_unchanged': True,
    'original_qemu_preserved_file_count': len(initial['qemu']['worktree_hashes']),
    'product_and_test_code_changed_during_preboard_validation': False,
    'raw_artifact_path': str(base),
    'note': 'plan.json records the initial no-commit instruction; subsequent user instruction authorized documentation/evidence commit and push.'
}
(dest / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
index = {str(p.relative_to(dest)): {'sha256': sha(p), 'bytes': p.stat().st_size} for p in sorted(dest.rglob('*')) if p.is_file() and p.name != 'evidence-index.json'}
(dest / 'evidence-index.json').write_text(json.dumps(index, indent=2) + '\n')
print(json.dumps(summary, indent=2))
print('Packaged files:', len(index), 'bytes:', sum(v['bytes'] for v in index.values()))
