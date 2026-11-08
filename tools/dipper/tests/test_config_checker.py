import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('config_checker', Path(__file__).resolve().parents[1] / 'check-config.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ConfigTests(unittest.TestCase):
    def test_dropped_driver_detected(self):
        self.assertEqual(checker.mismatches({'CONFIG_X': 'm'}, {}), [('CONFIG_X', 'm', 'n')])

    def test_wrong_linkage_detected(self):
        self.assertTrue(checker.mismatches({'CONFIG_UFS': 'y'}, {'CONFIG_UFS': 'm'}))

    def test_disabled_option_passes(self):
        self.assertFalse(checker.mismatches({'CONFIG_OC': 'n'}, {}))

    def test_strings_and_disabled_parse(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'config'
            p.write_text('CONFIG_BINDER="binder,hwbinder,vndbinder"\n# CONFIG_BAD is not set\n')
            self.assertEqual(checker.read_config(p), {'CONFIG_BINDER': '"binder,hwbinder,vndbinder"', 'CONFIG_BAD': 'n'})
