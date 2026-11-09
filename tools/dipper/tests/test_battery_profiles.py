"""Source-audit decoding tests, not battery selection or charge control."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('battery_profiles', ROOT / 'tools/dipper/audit-battery-profiles.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def cells(*values):
    return b''.join(value.to_bytes(4, 'big', signed=value < 0) for value in values)


class BatteryAuditTests(unittest.TestCase):
    def test_truncated_and_empty_cells_rejected(self):
        for data in [b'', b'\x01', bytes(5)]:
            with self.assertRaises(ValueError):
                audit.cells(data)

    def test_signed_values_remain_signed(self):
        self.assertEqual(audit.cells(cells(-300), signed=True), [-300])

    def test_dynamic_fv_is_cycle_count_not_temperature(self):
        rows = audit.ranges(cells(0, 100, 4400000), 4400000, unit='cycles')
        self.assertEqual(rows[0]['threshold_unit'], 'cycles')

    def test_ranges_preserve_values(self):
        self.assertEqual(audit.ranges(cells(0, 50, 330000, 51, 100, 990000), 3300000)[0]['value'], 330000)

    def test_malformed_ranges_are_not_silently_clamped(self):
        for values in [(0, 50), (100, 50, 1), (0, 700, 1), (0, 50, 4000000),
                       (0, 50, 1, 50, 100, 1)]:
            with self.assertRaises(ValueError):
                audit.ranges(cells(*values), 3300000)

    def test_id_integer_boundaries_and_no_default(self):
        profiles = [dict(battery_type='atl', id_kohm=[68], tolerance_percent=15),
                    dict(battery_type='coslight', id_kohm=[100], tolerance_percent=15)]
        for resistance in range(0, 301):
            wanted = 'atl' if 58 <= resistance <= 78 else 'coslight' if 85 <= resistance <= 115 else None
            self.assertEqual(audit.reference_selection(profiles, resistance), wanted)

    def test_ambiguous_and_negative_id_rejected(self):
        profiles = [dict(battery_type=name, id_kohm=[100], tolerance_percent=15) for name in ['a', 'b']]
        for resistance in [-1, 100]:
            with self.assertRaises(ValueError):
                audit.reference_selection(profiles, resistance)

    def test_thermal_outlier_preserved_not_corrected(self):
        values = [1125000, 10255000, 925000]
        findings = audit.thermal_findings(values)
        self.assertEqual(len(findings), 2)
        self.assertEqual(values[1], 10255000)

    def test_strings_require_one_terminator(self):
        self.assertEqual(audit.string(b'atl\0'), 'atl')
        for data in [b'atl', b'a\0b\0']:
            with self.assertRaises(ValueError):
                audit.string(data)

    def test_recorded_coverage_and_profile_identity(self):
        report = json.loads((ROOT / 'Documentation/dipper/battery-policy-comparison.json').read_text())
        for source in ['xiaomi', 'legacy']:
            self.assertEqual(len(report['definitions'][source]), 15)
            self.assertEqual(len({row['source'] for row in report['definitions'][source]}), 15)
        expected = {'85eac085f24042ca0cfa201ecea3b8dff2d645d74b8b60b434e20892d879b8f0',
                    '6b2025cc0ddeb131a13dc1b22c884042ffa47c0c1aa6640ba0b6275b73ca0b43'}
        for group in report['policy_groups'].values():
            self.assertEqual({p['payload_sha256'] for p in group['profiles']}, expected)
            self.assertEqual({p['payload_bytes'] for p in group['profiles']}, {224})
            self.assertTrue(any(f['value'] == 10255000 for f in group['findings']))
