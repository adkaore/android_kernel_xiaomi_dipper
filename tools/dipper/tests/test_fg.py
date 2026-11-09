"""FG source contracts and compiled wiring; not a hardware gauge simulation."""
from pathlib import Path
import os
import unittest
try:
    import libfdt
except ImportError:
    libfdt = None
ROOT = Path(__file__).resolve().parents[3]
SOURCE = (ROOT / 'drivers/power/supply/qcom_pmi8998_fg.c').read_text()
DTB_DIR = os.environ.get('DIPPER_DTB_DIR')


class FGSourceTests(unittest.TestCase):
    def test_no_profile_dma_or_threshold_writes(self):
        for token in ['regmap_write(', 'regmap_bulk_write(', 'regmap_update_bits(',
                      'set_property', 'property_is_writeable', 'request_firmware(',
                      'schedule_delayed_work(', 'MEM_INTF_', 'DMA_CTL']:
            self.assertNotIn(token, SOURCE)

    def test_revision_and_base_usid_are_checked(self):
        for token in ['pmic->subtype != PMI8998_SUBTYPE', 'pmic->major != 1 && pmic->major != 2',
                      'to_spmi_device(dev->parent)->usid & 1', 'fg->base != 0x4000']:
            self.assertIn(token, SOURCE)

    def test_capacity_has_bounded_shadow_retry_not_minimum_fallback(self):
        body = SOURCE.split('static int pmi8998_fg_get_capacity(')[1].split('static int pmi8998_fg_get_property(')[0]
        self.assertIn('tries < 5', body)
        self.assertIn('data[0] == data[1]', body)
        self.assertIn('return -EAGAIN', body)
        self.assertNotIn('min(', body)

    def test_no_fake_missing_battery_or_charging_status(self):
        body = SOURCE.split('static int pmi8998_fg_ready(')[1].split('static int pmi8998_fg_get_capacity(')[0]
        self.assertIn('FG_ALGORITHM_EN', body)
        self.assertIn('FG_SOC_READY', body)
        self.assertIn('return -ENODATA', body)
        self.assertIn('power_supply_get_property_from_supplier(psy, property, value)', SOURCE)

    def test_irqs_cannot_outlive_managed_supply(self):
        body = SOURCE.split('static int pmi8998_fg_probe(')[1]
        self.assertLess(body.index('devm_power_supply_register'), body.index('devm_request_threaded_irq'))


@unittest.skipUnless(libfdt and DTB_DIR, 'set DIPPER_DTB_DIR and use schema Python')
class FGDTTests(unittest.TestCase):
    def find(self, tree, compatible, node=0):
        if tree.hasprop(node, 'compatible') and compatible in tree.getprop(node, 'compatible').as_stringlist():
            return node
        child = tree.first_subnode(node, quiet=(libfdt.NOTFOUND,))
        while child >= 0:
            found = self.find(tree, compatible, child)
            if found is not None:
                return found
            child = tree.next_subnode(child, quiet=(libfdt.NOTFOUND,))
        return None

    def test_direct_gauge_wiring_and_gating(self):
        tree = libfdt.Fdt((Path(DTB_DIR) / 'sdm845-xiaomi-dipper-mp-v2.1.dtb').read_bytes())
        node = self.find(tree, 'qcom,pmi8998-fg')
        self.assertIsNotNone(node)
        self.assertEqual(tree.getprop(node, 'status').as_str(), 'disabled')
        self.assertEqual(tree.getprop(node, 'reg').as_uint32_list(), [0x4000])
        self.assertEqual(tree.getprop(tree.parent_offset(node), 'reg').as_uint32_list()[0], 2)
        self.assertEqual(tree.getprop(node, 'interrupts').as_uint32_list(),
                         [2, 0x40, 1, 1, 2, 0x40, 3, 1, 2, 0x41, 3, 3, 2, 0x41, 6, 1])
        charger = tree.node_offset_by_phandle(tree.getprop(node, 'power-supplies').as_uint32())
        self.assertIn('qcom,pmi8998-charger', tree.getprop(charger, 'compatible').as_stringlist())
        battery = tree.node_offset_by_phandle(tree.getprop(node, 'monitored-battery').as_uint32())
        self.assertEqual(tree.getprop(battery, 'compatible').as_str(), 'simple-battery')


if __name__ == '__main__':
    unittest.main()
