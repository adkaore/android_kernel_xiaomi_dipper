"""Native current-ceiling contracts, not charger/PD emulation."""
import json
import os
import re
import subprocess
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

    def test_voltage_ceiling_is_validated_without_upward_bias(self):
        self.assertIn('smb2_float_voltage_code(chip->batt_info->voltage_max_design_uv)', SOURCE)
        self.assertNotIn('/ 7500 + 1', SOURCE)

    def test_iio_measurement_is_not_replaced_by_a_charging_status_guess(self):
        reader = SOURCE.split('static int smb_get_iio_chan(')[1].split('static int')[0]
        self.assertNotIn('POWER_SUPPLY_PROP_STATUS', reader)
        self.assertNotIn('*val = 0', reader)
        self.assertIn('return iio_read_channel_processed(chan, val)', reader)

    def test_irq_does_not_use_uninitialized_status_on_read_failure(self):
        handler = SOURCE.split('static irqreturn_t smb_handle_batt_overvoltage(')[1].split('static irqreturn_t')[0]
        self.assertIn('ret = regmap_read(', handler)
        self.assertIn('else if (status &', handler)

    def test_status_bits_match_all_three_hardware_sources(self):
        header = (ROOT / 'drivers/power/supply/qcom_smbx_status.h').read_text()
        fields = dict(re.findall(r'^#define\s+(\w+)\s+BIT\((\d+)\)', header, re.M))
        self.assertEqual(len(fields), 8)
        for revision in ['dcfb1676925aec83b06ae04fd8ca989ced75067c',
                         '0b3e38ef6c40e0a346dfa0241aeceaf5540e91f6',
                         '1f01db5a42ccd3123517a9d5730024719abf095d']:
            result = subprocess.run(['git', 'show', revision + ':drivers/power/supply/qcom/smb-reg.h'],
                                    cwd=ROOT, capture_output=True, text=True)
            if result.returncode:
                self.skipTest('fetch pinned hardware references for the three-source bit audit')
            reference = dict(re.findall(r'^#define\s+(\w+)\s+BIT\((\d+)\)', result.stdout, re.M))
            for field, bit in fields.items():
                self.assertEqual(reference[field], bit, (revision, field))


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
