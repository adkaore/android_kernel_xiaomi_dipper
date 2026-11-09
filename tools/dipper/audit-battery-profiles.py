#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Audit resolved pinned battery/thermal data; never select/load a phone profile."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PINS = {'xiaomi': 'dcfb1676925aec83b06ae04fd8ca989ced75067c',
        'legacy': '0b3e38ef6c40e0a346dfa0241aeceaf5540e91f6',
        'caf': '1f01db5a42ccd3123517a9d5730024719abf095d'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def cells(data, signed=False):
    if not data or len(data) % 4:
        raise ValueError('Empty/truncated 32-bit DT property')
    return [int.from_bytes(data[i:i + 4], 'big', signed=signed)
            for i in range(0, len(data), 4)]


def string(data):
    if not data.endswith(b'\0') or b'\0' in data[:-1]:
        raise ValueError('Expected a single terminated DT string')
    return data[:-1].decode()


def ranges(data, ceiling, unit='decicelsius'):
    values = cells(data)
    if len(values) % 3:
        raise ValueError('Truncated range triple')
    result = []
    previous = None
    for low, high, value in zip(values[0::3], values[1::3], values[2::3]):
        if low > high or high > 600 or value > ceiling:
            raise ValueError('Invalid threshold/value range; do not clamp source evidence')
        if previous is not None and low <= previous:
            raise ValueError('Overlapping or unordered ranges')
        result.append(dict(low=low, high=high, threshold_unit=unit, value=value))
        previous = high
    return result


def profile(props, tolerance):
    payload = props['qcom,fg-profile-data']
    ids = cells(props['qcom,batt-id-kohm'])
    if len(payload) != 224 or any(not value for value in ids) or not 0 <= tolerance <= 100:
        raise ValueError('Unqualified GEN3 payload/identity')
    voltage = cells(props['qcom,max-voltage-uv'])[0]
    current = cells(props['qcom,fastchg-current-ma'])[0] * 1000
    return {
        'battery_type': string(props['qcom,battery-type']),
        'id_kohm': ids, 'tolerance_percent': tolerance,
        # The old caller floors measured ohms to integer kohm before selection.
        'accepted_integer_kohm': [[value - value * tolerance // 100,
                                  value + value * tolerance // 100] for value in ids],
        'payload_bytes': len(payload), 'payload_sha256': digest(payload),
        'declared_checksum': hex(cells(props['qcom,checksum'])[0]),
        'declared_checksum_verified': False,
        'max_voltage_uv': voltage, 'max_fcc_ua': current,
        'capacity_mah': cells(props['qcom,nom-batt-capacity-mah'])[0],
        'jeita_fcc': ranges(props['qcom,jeita-fcc-ranges'], current),
        'jeita_fv': ranges(props['qcom,jeita-fv-ranges'], voltage),
        'dynamic_fv': ranges(props['qcom,dynamic-fv-ranges'], voltage, unit='cycles'),
    }


def reference_selection(profiles, measured_integer_kohm):
    """Model the source tolerance/nearest rule, reject ambiguity, not a driver."""
    if measured_integer_kohm < 0:
        raise ValueError('Negative resistance')
    matches = []
    for item in profiles:
        for nominal in item['id_kohm']:
            delta = abs(nominal - measured_integer_kohm)
            if delta <= nominal * item['tolerance_percent'] // 100:
                matches.append((delta, item['battery_type']))
    if not matches:
        return None
    best = min(delta for delta, _ in matches)
    names = {name for delta, name in matches if delta == best}
    if len(names) != 1:
        raise ValueError('Ambiguous source battery-ID selection')
    return names.pop()


def thermal_findings(values):
    findings = []
    for i, value in enumerate(values):
        if value > 3300000:
            findings.append(dict(index=i, value=value, issue='exceeds_both_3A_ICL_and_3.3A_FCC'))
        if i and value > values[i - 1]:
            findings.append(dict(index=i, value=value, issue='increases_with_cooling_state'))
    return findings


def nodes(tree, node=0, path='/'):
    import libfdt
    result = {}
    props = {}
    offset = tree.first_property_offset(node, quiet=(libfdt.NOTFOUND,))
    while offset >= 0:
        prop = tree.get_property_by_offset(offset)
        props[prop.name] = bytes(prop)
        offset = tree.next_property_offset(offset, quiet=(libfdt.NOTFOUND,))
    result[path] = props
    child = tree.first_subnode(node, quiet=(libfdt.NOTFOUND,))
    while child >= 0:
        result.update(nodes(tree, child, path.rstrip('/') + '/' + tree.get_name(child)))
        child = tree.next_subnode(child, quiet=(libfdt.NOTFOUND,))
    return result


def inspect(all_nodes):
    profiles, policies, findings = [], [], []
    for path, props in sorted(all_nodes.items()):
        if 'qcom,fg-profile-data' in props:
            parent = all_nodes[path.rsplit('/', 1)[0]]
            tolerance = cells(parent.get('qcom,batt-id-range-pct', bytes(4)))[0]
            profiles.append(profile(props, tolerance))
        if 'qcom,fcc-max-ua' in props or 'qcom,fg-jeita-thresholds' in props:
            selected = {}
            for name, value in sorted(props.items()):
                if name.startswith(('qcom,thermal-mitigation', 'qcom,fg-', 'qcom,fcc-')) or name in {
                        'qcom,usb-icl-ua', 'qcom,sw-jeita-enable', 'qcom,dynamic-fv-enable',
                        'qcom,wd-bark-time-secs'}:
                    if not value:
                        selected[name] = True
                    else:
                        selected[name] = cells(value, signed=name == 'qcom,fg-sys-term-current')
                    if name.startswith('qcom,thermal-mitigation'):
                        findings.extend(dict(path=path, property=name, **issue)
                                        for issue in thermal_findings(cells(value)))
            policies.append(dict(path=path, properties=selected))
    if not profiles:
        raise ValueError('No resolved battery profiles; wrong/incomplete input tree')
    return dict(profiles=profiles, policies=policies, findings=findings)


def main():
    import libfdt
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xiaomi', 'legacy'):
        parser.add_argument('--' + name + '-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not args.output.is_absolute() or ROOT in args.output.resolve().parents:
        parser.error('Use an absolute external output path')
    report = dict(target='crDroid 13.1 / Android 17', hardware_accessed=False,
                  profile_loaded=False, source_commits=PINS, definitions={}, policy_groups={})
    for source in ('xiaomi', 'legacy'):
        directory = getattr(args, source + '_dir')
        if not directory.is_absolute() or ROOT in directory.resolve().parents:
            parser.error('Resolved-tree inputs must be absolute external directories')
        if (directory / (source + '-source-commit')).read_text().strip() != PINS[source]:
            parser.error('Resolved source stamp does not match the pinned revision')
        manifest = json.loads((directory / (source + '-resolved-boards.json')).read_text())
        actual = manifest.get('source_commit', manifest.get('legacy_commit'))
        if actual != PINS[source] or len(manifest['variants']) != 15:
            parser.error('Unexpected resolved manifest source/coverage')
        rows = []
        for variant in manifest['variants']:
            suffix = '.dtbo' if variant['kind'] == 'overlay' else '.dtb'
            name = Path(variant['source']).stem + suffix
            data = (directory / name).read_bytes()
            policy = inspect(nodes(libfdt.Fdt(data)))
            identity = digest(json.dumps(policy, sort_keys=True).encode())
            report['policy_groups'][identity] = policy
            rows.append(dict(source=variant['source'], kind=variant['kind'],
                             listed=variant['listed_build_target'], blob_sha256=digest(data),
                             policy_group=identity))
        report['definitions'][source] = rows
    paths = ['arch/arm64/boot/dts/qcom/batterydata-e1-' + name + '-3400mAh.dtsi'
             for name in ('coslight', 'atl')]
    report['profile_file_controls'] = {}
    for source, revision in PINS.items():
        report['profile_file_controls'][source] = {}
        for path in paths:
            result = subprocess.run(['git', 'show', revision + ':' + path], cwd=ROOT,
                                    capture_output=True)
            report['profile_file_controls'][source][path] = (
                digest(result.stdout) if result.returncode == 0 else None)
    report['limitations'] = [
        'Compiled source properties, not actual battery identity or loaded SRAM.',
        'Plugins retain external fixups and are not applied to a legacy base.',
        'Declared profile checksum algorithm is unverified; SHA256 is a file/payload identity only.',
        'QC2 outlier is preserved as evidence, not imported, clamped or guessed corrected.',
        'Native profile loading, SRAM/IMA protocol, learning/persistence and software JEITA are not implemented by this audit.',
    ]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'30 source definitions, {len(report["policy_groups"])} resolved battery policy group(s)')


if __name__ == '__main__':
    main()
