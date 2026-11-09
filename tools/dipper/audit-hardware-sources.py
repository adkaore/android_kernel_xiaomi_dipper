#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Inventory pinned hardware references offline, without extracting patches.

Fetch the pinned source objects first. JSON on stdout is deterministic and may
be redirected to an external path or reviewed/committed as evidence. No device,
working-tree source, Git branch, index or remote is modified.
"""
import fnmatch
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE_NAMES = ['xiaomi', 'legacy', 'xiaomi_declared_caf',
                'linux_4_9_186', 'linux_4_9_337']
PAIRS = [('linux_4_9_186', 'xiaomi_declared_caf'),
         ('xiaomi_declared_caf', 'xiaomi'), ('xiaomi', 'legacy'),
         ('linux_4_9_337', 'legacy'), ('linux_4_9_186', 'linux_4_9_337'),
         ('xiaomi_declared_caf', 'legacy')]


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def classify(left, right):
    if left is None:
        return 'absent-both' if right is None else 'right-only'
    if right is None:
        return 'left-only'
    return 'identical-bytes' if left == right else 'different'


def main():
    pins_path = ROOT / 'Documentation/dipper/sources.json'
    scope_path = ROOT / 'Documentation/dipper/hardware-comparison-scope.json'
    pins = json.loads(pins_path.read_text())
    scope = json.loads(scope_path.read_text())
    sources = {s['name']: s for s in pins['sources'] if s['name'] in SOURCE_NAMES}
    if set(sources) != set(SOURCE_NAMES):
        raise SystemExit('Missing required pinned hardware source')
    contents, source_info = {}, {}
    all_paths = set()
    for name in SOURCE_NAMES:
        source = sources[name]
        revision = source['commit']
        if not re.fullmatch('[0-9a-f]{40}', revision):
            raise SystemExit(f'{name} is not pinned to a full commit')
        git('cat-file', '-e', revision + '^{commit}')
        paths = git('ls-tree', '-r', '--name-only', revision).decode().splitlines()
        all_paths.update(paths)
        contents[name] = set(paths)
        makefile = git('show', revision + ':Makefile').decode()
        version = '.'.join(re.search(r'^' + key + r' = (\d+)$', makefile, re.M)[1]
                           for key in ['VERSION', 'PATCHLEVEL', 'SUBLEVEL'])
        source_info[name] = {'repository': source['url'], 'commit': revision,
                             'linux_version': version}
    groups = {name: sorted(p for p in all_paths if any(fnmatch.fnmatchcase(p, pattern)
                                                     for pattern in patterns))
              for name, patterns in scope['groups'].items()}
    selected = sorted({p for paths in groups.values() for p in paths})
    metadata = {}
    for path in selected:
        metadata[path] = {}
        for name in SOURCE_NAMES:
            if path not in contents[name]:
                metadata[path][name] = None
                continue
            data = git('show', sources[name]['commit'] + ':' + path)
            metadata[path][name] = {'sha256': hashlib.sha256(data).hexdigest(),
                                    'bytes': len(data)}
    comparisons = {}
    for left, right in PAIRS:
        numstat = {}
        for line in git('diff', '--numstat', '--no-renames', sources[left]['commit'],
                        sources[right]['commit'], '--', *selected).decode().splitlines():
            added, deleted, path = line.split('\t')
            numstat[path] = {'added': added, 'deleted': deleted}
        rows = []
        for path in selected:
            a, b = metadata[path][left], metadata[path][right]
            rows.append({'file': path, 'status': classify(a, b),
                         'numstat': numstat.get(path)})
        comparisons[left + '..' + right] = rows
    report = {
        'target': 'crDroid 13.1 / Android 17', 'hardware_accessed': False,
        'policy': scope['policy'], 'sources': source_info,
        'scope_sha256': hashlib.sha256(scope_path.read_bytes()).hexdigest(),
        'groups': groups, 'files': metadata, 'comparisons': comparisons,
        'qualcomm_provenance': {
            'xiaomi': 'MiCode README declares LA.UM.8.3.r1-05800-sdm845.0; '
                      'the tag and commit are pinned, but exact ancestry is not proved',
            'legacy': 'Exact Qualcomm base is not established; comparisons against '
                      'the Xiaomi-declared tag include BSP/version/later changes',
        },
        'limits': ['Byte equality is not hardware validation',
                   'A changed file is not automatically a device patch or a crDroid-authored fix',
                   'Linux version-matched controls are not ancestry proofs',
                   'No target HAL/vendor ABI is selected by this inventory'],
    }
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
