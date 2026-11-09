"""Reference provenance, report classification and source-only board checks."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
DOCS = ROOT / 'Documentation/dipper'
spec = importlib.util.spec_from_file_location('audit_hardware', ROOT / 'tools/dipper/audit-hardware-sources.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class HardwareSourceTests(unittest.TestCase):
    def test_exact_official_source_and_independent_controls(self):
        pins = {s['name']: s for s in json.loads((DOCS / 'sources.json').read_text())['sources']}
        self.assertEqual(pins['xiaomi']['url'], 'https://github.com/MiCode/Xiaomi_Kernel_OpenSource.git')
        self.assertEqual(pins['xiaomi']['branch'], 'dipper-q-oss')
        self.assertEqual(pins['xiaomi']['commit'], 'dcfb1676925aec83b06ae04fd8ca989ced75067c')
        self.assertEqual(pins['xiaomi_declared_caf']['commit'], '1f01db5a42ccd3123517a9d5730024719abf095d')
        self.assertNotEqual(pins['linux_4_9_186']['commit'], pins['linux_4_9_337']['commit'])

    def test_byte_inventory_does_not_invent_device_patch_classification(self):
        self.assertEqual(audit.classify(None, None), 'absent-both')
        self.assertEqual(audit.classify(None, 'x'), 'right-only')
        self.assertEqual(audit.classify('x', None), 'left-only')
        self.assertEqual(audit.classify('x', 'x'), 'identical-bytes')
        self.assertEqual(audit.classify('x', 'y'), 'different')

    def test_report_keeps_bsp_provenance_uncertainty(self):
        report = json.loads((DOCS / 'hardware-comparison.json').read_text())
        self.assertEqual(report['target'], 'crDroid 13.1 / Android 17')
        self.assertFalse(report['hardware_accessed'])
        self.assertIn('not established', report['qualcomm_provenance']['legacy'])
        self.assertEqual(report['sources']['xiaomi']['linux_version'], '4.9.186')
        self.assertEqual(report['sources']['legacy']['linux_version'], '4.9.337')
        self.assertEqual(len(report['comparisons']), 6)

    def test_registers_and_pdphy_are_not_assumed_newer_hardware(self):
        report = json.loads((DOCS / 'hardware-comparison.json').read_text())
        path = 'drivers/usb/pd/qpnp-pdphy.c'
        files = report['files'][path]
        self.assertEqual(files['xiaomi']['sha256'], files['legacy']['sha256'])
        self.assertEqual(files['xiaomi']['sha256'], files['xiaomi_declared_caf']['sha256'])

    def test_factory_absence_does_not_delete_later_ramoops(self):
        report = json.loads((DOCS / 'hardware-board-comparison.json').read_text())
        self.assertTrue(report['same_root_sources_and_selectors'])
        self.assertEqual(report['xiaomi_roots'], 15)
        self.assertEqual(report['crdroid_roots'], 15)
        self.assertEqual(report['only_xiaomi_memory_regions'], [])
        self.assertEqual(report['only_crdroid_memory_regions'], [{
            'path': '/reserved-memory/ramoops@b0000000', 'start': '0xb0000000',
            'size': '0x400000', 'no_map': False, 'compatible': ['ramoops']}])


if __name__ == '__main__':
    unittest.main()
