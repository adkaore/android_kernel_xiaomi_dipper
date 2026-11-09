#!/usr/bin/env python3
"""Validate a complete ARM64 handoff run, not the harness placeholder branch."""
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys

SUITE = 'qcom-pmi8998-input-handoff'
CASES = {
    'handoff_source', 'handoff_sink', 'handoff_input_failure',
    'handoff_boost_failure', 'handoff_disable_failure', 'handoff_shutdown_gate',
    'handoff_current_bounds', 'handoff_source_twice', 'handoff_sink_without_boost',
    'handoff_resume_failure', 'handoff_boost_retry', 'handoff_current_failure',
    'handoff_current_reset', 'handoff_shutdown_current',
}


def checked_cases(result):
    """Reject missing, skipped, duplicate, crashed or unexpected tests/suites."""
    expected = dict(tests=len(CASES), passed=len(CASES), failed=0,
                    crashed=0, skipped=0, errors=0)
    if result.get('arch') != 'arm64' or result.get('misc') != expected:
        raise ValueError('Incomplete ARM64 KUnit result')
    groups = result.get('sub_groups', [])
    if len(groups) != 1 or result.get('test_cases'):
        raise ValueError('Unexpected root test structure')
    group = groups[0]
    if (group.get('name') != SUITE or group.get('misc') != expected
            or group.get('sub_groups')):
        raise ValueError('Missing or incomplete handoff suite')
    cases = group.get('test_cases', [])
    if (len(cases) != len(CASES) or {case.get('name') for case in cases} != CASES
            or any(case.get('status') != 'PASS' for case in cases)):
        raise ValueError('Unexpected or unsuccessful handoff cases')
    return sorted(CASES)


def main():
    root, out = map(Path, sys.argv[1:3])
    revision, status = sys.argv[3:5]
    result = json.loads((out / 'typec-results.json').read_text())
    names = checked_cases(result)
    raw = (out / 'test.log').read_text()
    for fault in ('WARNING:', 'BUG:', 'Kernel panic', 'possible circular locking'):
        if fault in raw:
            raise ValueError(f'Kernel diagnostic detected: {fault}')
    config = (out / '.config').read_text()
    for required in ('CONFIG_INIT_STACK_ALL_PATTERN=y', 'CONFIG_RANDSTRUCT_FULL=y',
                     'CONFIG_PROVE_LOCKING=y', 'CONFIG_DEBUG_ATOMIC_SLEEP=y'):
        if required not in config.splitlines():
            raise ValueError(f'Missing boot regression/hardening config: {required}')
    report = {
        'kind': 'pmi8998-kunit-api-handoff',
        'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'commit': revision, 'source_dirty': bool(status),
        'branch': subprocess.check_output(['git', 'branch', '--show-current'],
                                          cwd=root, text=True).strip(),
        'arch': 'arm64', 'emulator': 'QEMU virt',
        'hardware_tested': False, 'target_hal_tested': False,
        'result': 'pass', 'cases': names,
        'sha256': {name: hashlib.sha256((out / name).read_bytes()).hexdigest()
                   for name in ['.config', 'arch/arm64/boot/Image.gz',
                                'typec-results.json', 'test.log']},
        'compiler': subprocess.check_output(['clang', '--version'], text=True).splitlines()[0],
        'qemu': subprocess.check_output(['qemu-system-aarch64', '--version'],
                                        text=True).splitlines()[0],
    }
    (out / 'dipper-kunit-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(names)} real PMI8998 callback tests in ARM64 QEMU')


if __name__ == '__main__':
    main()
