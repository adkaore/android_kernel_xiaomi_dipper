#!/usr/bin/env python3
"""Validate ARM64 driver API runs, not the harness placeholder branch."""
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys

SUITES = {'qcom-pmi8998-input-handoff': {
    'handoff_source', 'handoff_sink', 'handoff_input_failure',
    'handoff_boost_failure', 'handoff_disable_failure', 'handoff_shutdown_gate',
    'handoff_current_bounds', 'handoff_source_twice', 'handoff_sink_without_boost',
    'handoff_resume_failure', 'handoff_boost_retry', 'handoff_current_failure',
    'handoff_current_reset', 'handoff_shutdown_current',
}, 'qcom-pmi8998-smb2-policy': {
    'smb2_suspend_ack', 'smb2_suspend_delayed_ack', 'smb2_suspend_command_read_error',
    'smb2_suspend_write_error', 'smb2_suspend_status_error', 'smb2_suspend_timeout',
    'smb2_resume_no_poll', 'smb2_unmanaged_no_io', 'smb2_invalid_online',
    'smb2_current_limits', 'smb2_lower_never_raises', 'smb2_not_ready',
    'smb2_disable_fallback', 'smb2_disable_source_error', 'smb2_health_read_error',
    'smb2_stop_gate', 'smb2_sysfs_policy',
    'smb2_watchdog_config', 'smb2_watchdog_start_stop',
    'smb2_watchdog_start_pet_error', 'smb2_watchdog_start_enable_error',
    'smb2_watchdog_pet_fault', 'smb2_watchdog_not_ready',
    'smb2_watchdog_other_pmic', 'smb2_watchdog_suspend_no_wake',
    'smb2_watchdog_config_error', 'smb2_watchdog_stop_error',
    'smb2_watchdog_service_ok', 'smb2_watchdog_cleanup_error',
}}


def counts(total):
    return dict(tests=total, passed=total, failed=0, crashed=0, skipped=0, errors=0)


def checked_cases(result):
    """Reject missing, skipped, duplicate, crashed or unexpected tests/suites."""
    expected = counts(sum(map(len, SUITES.values())))
    if result.get('arch') != 'arm64' or result.get('misc') != expected:
        raise ValueError('Incomplete ARM64 KUnit result')
    groups = result.get('sub_groups', [])
    if (len(groups) != len(SUITES) or result.get('test_cases')
            or {group.get('name') for group in groups} != SUITES.keys()):
        raise ValueError('Unexpected root test structure')
    for group in groups:
        wanted = SUITES[group['name']]
        if group.get('misc') != counts(len(wanted)) or group.get('sub_groups'):
            raise ValueError('Missing or incomplete PMI8998 suite')
        cases = group.get('test_cases', [])
        if (len(cases) != len(wanted) or {case.get('name') for case in cases} != wanted
                or any(case.get('status') != 'PASS' for case in cases)):
            raise ValueError('Unexpected or unsuccessful PMI8998 cases')
    return {suite: sorted(cases) for suite, cases in SUITES.items()}


def main():
    root, out = map(Path, sys.argv[1:3])
    revision, status = sys.argv[3:5]
    result = json.loads((out / 'pmi8998-results.json').read_text())
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
        'kind': 'pmi8998-kunit-kernel-apis',
        'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'commit': revision, 'source_dirty': bool(status),
        'branch': subprocess.check_output(['git', 'branch', '--show-current'],
                                          cwd=root, text=True).strip(),
        'arch': 'arm64', 'emulator': 'QEMU virt',
        'hardware_tested': False, 'target_hal_tested': False,
        'result': 'pass', 'suites': names,
        'sha256': {name: hashlib.sha256((out / name).read_bytes()).hexdigest()
                   for name in ['.config', 'arch/arm64/boot/Image.gz',
                                'pmi8998-results.json', 'test.log']},
        'compiler': subprocess.check_output(['clang', '--version'], text=True).splitlines()[0],
        'qemu': subprocess.check_output(['qemu-system-aarch64', '--version'],
                                        text=True).splitlines()[0],
    }
    (out / 'dipper-kunit-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {sum(map(len, names.values()))} real PMI8998 callback tests in ARM64 QEMU')


if __name__ == '__main__':
    main()
