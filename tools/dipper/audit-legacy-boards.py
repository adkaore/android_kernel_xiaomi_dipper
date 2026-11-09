#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Resolve the pinned downstream DTS inheritance without accessing a phone.

Requires libfdt (the dtschema venv), clang and a built dtc. Output is deterministic
apart from compiler logs, which stay in the external output directory. This is
an audit of source definitions, not a bootloader/physical-board compatibility test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile

import libfdt

ROOT = Path(__file__).resolve().parents[2]
LEGACY = '0b3e38ef6c40e0a346dfa0241aeceaf5540e91f6'
DTS = 'arch/arm64/boot/dts/qcom'
IGNORED_ROOT = {'model', 'qcom,msm-id', 'qcom,board-id'}


def properties(tree, node):
    result = {}
    offset = tree.first_property_offset(node, quiet=(libfdt.NOTFOUND,))
    while offset >= 0:
        prop = tree.get_property_by_offset(offset)
        result[prop.name] = bytes(prop)
        offset = tree.next_property_offset(offset, quiet=(libfdt.NOTFOUND,))
    return result


def nodes(tree, node=0, path='/'):
    result = {path: properties(tree, node)}
    child = tree.first_subnode(node, quiet=(libfdt.NOTFOUND,))
    while child >= 0:
        result.update(nodes(tree, child, path.rstrip('/') + '/' + tree.get_name(child)))
        child = tree.next_subnode(child, quiet=(libfdt.NOTFOUND,))
    return result


def cells(data, width=4):
    if len(data) % width:
        raise ValueError('Truncated DT cell')
    return [int.from_bytes(data[i:i + width], 'big') for i in range(0, len(data), width)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--clang', required=True)
    parser.add_argument('--dtc', required=True)
    parser.add_argument('--source', choices=['legacy', 'xiaomi'], default='legacy',
                        help='Use the pinned crDroid or official Xiaomi comparison source')
    args = parser.parse_args()
    revision = LEGACY
    if args.source == 'xiaomi':
        pins = json.loads((ROOT / 'Documentation/dipper/sources.json').read_text())
        revision = next(s['commit'] for s in pins['sources'] if s['name'] == 'xiaomi')
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        parser.error('Source must be pinned to a full commit ID')
    if not args.output_dir.is_absolute():
        parser.error('--output-dir must be absolute and outside the source tree')
    out = args.output_dir.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error('Output must be outside the source tree')
    out.mkdir(parents=True, exist_ok=True)
    source = out / ('source' if args.source == 'legacy' else 'source-xiaomi')
    stamp = out / (args.source + '-source-commit')
    if stamp.exists() and stamp.read_text().strip() != revision:
        parser.error('Output contains another source revision; use a fresh output directory')
    source.mkdir(exist_ok=True)
    archive = out / (args.source + '-dts.tar')
    subprocess.run(['git', 'archive', '--format=tar', f'--output={archive}',
                    revision, DTS, 'include/dt-bindings'], cwd=ROOT, check=True)
    with tarfile.open(archive) as files:
        files.extractall(source, filter='data')
    stamp.write_text(revision + '\n')
    makefile = (source / DTS / 'Makefile').read_text()
    paths = sorted((source / DTS).glob('dipper*.dts'))
    rows, flattened, layouts = [], {}, {}
    for path in paths:
        name = path.stem
        overlay = name.endswith('-overlay')
        suffix = '.dtbo' if overlay else '.dtb'
        pre, binary = out / (name + '.pre.dts'), out / (name + suffix)
        with (out / (name + '.log')).open('w') as log:
            subprocess.run([args.clang, '-E', '-nostdinc', '-undef', '-D__DTS__',
                            '-x', 'assembler-with-cpp', '-I', str(source / 'include'),
                            '-I', str(source / DTS), str(path), '-o', str(pre)],
                           check=True, stdout=log, stderr=log)
            subprocess.run([args.dtc, '-I', 'dts', '-O', 'dtb', '-@',
                            '-o', str(binary), str(pre)], check=True, stdout=log, stderr=log)
        tree = libfdt.Fdt(binary.read_bytes())
        all_nodes = nodes(tree)
        root = all_nodes['/']
        row = {
            'source': str(path.relative_to(source)),
            'kind': 'overlay' if overlay else 'standalone',
            # Match a list entry, not an unused *.dtbo-base assignment.
            'listed_build_target': bool(re.search(
                r'^\s+' + re.escape(name + suffix) + r'\s*(?:\\\s*)?$',
                makefile, re.MULTILINE)),
            'model': root['model'].rstrip(b'\0').decode(),
            'msm_id': cells(root['qcom,msm-id']),
            'board_id': cells(root['qcom,board-id']),
        }
        if overlay:
            match = re.search(r'^' + re.escape(name + '.dtbo-base') + r'\s*:=\s*(\S+)',
                              makefile, re.MULTILINE)
            row['legacy_base'] = match.group(1) if match else None
        else:
            regions = []
            for node_path, props in sorted(all_nodes.items()):
                if not node_path.startswith('/reserved-memory/') or 'reg' not in props:
                    continue
                reg = cells(props['reg'], 8)
                if len(reg) % 2:
                    raise ValueError(f'Invalid reserved memory tuple: {node_path}')
                for start, size in zip(reg[::2], reg[1::2]):
                    region = {'path': node_path, 'start': hex(start), 'size': hex(size),
                              'no_map': 'no-map' in props}
                    if 'compatible' in props:
                        region['compatible'] = props['compatible'].rstrip(b'\0').decode().split('\0')
                    if region.get('compatible') == ['ramoops']:
                        row['ramoops'] = {key: cells(value)[0] for key, value in props.items()
                                         if key.endswith('-size')}
                    regions.append(region)
            layout = hashlib.sha256(json.dumps(regions, sort_keys=True).encode()).hexdigest()[:16]
            layouts[layout] = regions
            row['fixed_memory_layout'] = layout
            flattened[name] = {(p, k): v.hex() for p, props in all_nodes.items()
                               if not p.startswith('/__') for k, v in props.items()
                               if k not in {'phandle', 'linux,phandle'}
                               and not (p == '/' and k in IGNORED_ROOT)}
        rows.append(row)
    for row in rows:
        if row['kind'] != 'standalone':
            continue
        reference = next(v for v in rows if v['kind'] == 'standalone'
                         and v['board_id'][0] in (30, 50) and v['msm_id'] == row['msm_id'])
        name, base = Path(row['source']).stem, Path(reference['source']).stem
        current, previous = flattened[name], flattened[base]
        row['raw_comparison_base'] = reference['source']
        row['raw_property_differences'] = [
            {'path': p, 'property': k, 'reference': previous.get((p, k)),
             'variant': current.get((p, k))}
            for p, k in sorted(current.keys() | previous.keys())
            if current.get((p, k)) != previous.get((p, k))]
    report = {
        'legacy_commit': revision,
        'method': 'clang preprocessing and dtc -@; no forced compilation',
        'scope': 'Source definitions; no phone accessed and no physical compatibility inferred.',
        'comparison_note': 'Same-SoC-revision P0 reference; root identity and generated phandles '
                           'excluded. Reference-valued properties remain raw; not an electrical diff.',
        'overlay_note': 'Old plugins are compiled with unresolved external fixups, not applied '
                        'to a legacy base and not claimed compatible with the new base.',
        'fixed_memory_layouts': layouts,
        'variants': rows,
    }
    if args.source == 'xiaomi':
        report['source_commit'] = report.pop('legacy_commit')
        report['source_name'] = 'official Xiaomi dipper-q-oss'
    result = out / (args.source + '-resolved-boards.json')
    result.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(rows)} definitions, '
          f'{sum(v["listed_build_target"] and v["kind"] == "standalone" for v in rows)} '
          f'listed standalone targets, {len(layouts)} fixed memory layout(s): {result}')


if __name__ == '__main__':
    main()
