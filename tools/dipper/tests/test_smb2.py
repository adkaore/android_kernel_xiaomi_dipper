"""Native current-ceiling contracts, not charger/PD emulation."""
import json
import os
from pathlib import Path
import unittest
try:
    import libfdt
except ImportError:
    libfdt = None

ROOT = Path(__file__).resolve().parents[3]
SOURCE = (ROOT / 'drivers/power/supply/qcom_smbx.c').read_text()
DTB_DIR = os.environ.get('DIPPER_DTB_DIR')


class SMB2SourceTests(unittest.TestCase):
    def test_every_current_config_write_uses_signed_bounded_encoder(self):
        setter = SOURCE.split('static int smb_set_current_limit(')[1].split('static void')[0]
        self.assertIn('smb2_input_current_code(val, chip->input_current_limit_max_ua)', setter)
        self.assertIn('if (val_raw < 0)', setter)
        self.assertEqual(SOURCE.count('chip->base + USBIN_CURRENT_LIMIT_CFG'), 1)
        self.assertIn('struct smb_chip *chip, int val)', SOURCE)

    def test_ceiling_validated_before_hardware_init_and_publication(self):
        probe = SOURCE.split('static int smb_probe(')[1]
        self.assertLess(probe.index('smb2_input_current_code(0,'), probe.index('smb_init_hw(chip)'))
        self.assertLess(probe.index('smb2_input_current_code(0,'), probe.index('devm_power_supply_register('))

    def test_fast_charge_is_not_raised_by_input_ceiling(self):
        self.assertIn('.val = 1000000 / CURRENT_SCALE_FACTOR', SOURCE)
        self.assertNotIn('INT_MAX', SOURCE)

    def test_watchdog_pet_is_relative_to_the_charger_base(self):
        handler = SOURCE.split('static irqreturn_t smb_handle_wdog_bark(')[1].split('static const')[0]
        self.assertIn('regmap_write(chip->regmap, chip->base + BARK_BITE_WDOG_PET,', handler)


@unittest.skipUnless(libfdt and DTB_DIR, 'set DIPPER_DTB_DIR and use schema Python')
class SMB2DTTests(unittest.TestCase):
    def find(self, tree, node=0):
        if tree.hasprop(node, 'compatible') and 'qcom,pmi8998-charger' in tree.getprop(node, 'compatible').as_stringlist():
            return node
        child = tree.first_subnode(node, quiet=(libfdt.NOTFOUND,))
        while child >= 0:
            found = self.find(tree, child)
            if found is not None:
                return found
            child = tree.next_subnode(child, quiet=(libfdt.NOTFOUND,))
        return None

    def test_factory_input_ceiling_in_all_selected_variants(self):
        for target in json.loads((ROOT / 'Documentation/dipper/board-targets.json').read_text())['targets']:
            with self.subTest(dtb=target['dtb']):
                tree = libfdt.Fdt((Path(DTB_DIR) / target['dtb']).read_bytes())
                charger = self.find(tree)
                self.assertIsNotNone(charger)
                self.assertEqual(tree.getprop(charger, 'input-current-limit-microamp').as_uint32(), 3000000)


if __name__ == '__main__':
    unittest.main()
