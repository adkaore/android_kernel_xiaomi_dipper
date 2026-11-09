"""Source contracts; these do not emulate IRQs, PMIC errors or the input core."""
from pathlib import Path
import os
import unittest
try:
    import libfdt
except ImportError:
    libfdt = None

ROOT = Path(__file__).resolve().parents[3]
DRIVER = (ROOT / 'drivers/input/misc/qcom-spmi-haptics.c').read_text()
TRIM = (ROOT / 'drivers/nvmem/qcom-pmi8998-trim.c').read_text()
DTB_DIR = os.environ.get('DIPPER_DTB_DIR')


class HapticsSourceTests(unittest.TestCase):
    def test_memless_callback_only_updates_request_and_queues_work(self):
        body = DRIVER.split('static int qhap_play(')[1].split('static int qhap_quiesce(')[0]
        for forbidden in ['regmap_', 'qhap_update(', 'qhap_auto_res(', 'mutex_lock(', 'msleep(']:
            self.assertNotIn(forbidden, body)
        self.assertLess(body.index('spin_lock_irqsave'), body.index('schedule_work'))
        self.assertLess(body.index('schedule_work'), body.index('spin_unlock_irqrestore'))

    def test_gate_closes_before_both_works_are_drained(self):
        body = DRIVER.split('static int qhap_quiesce(')[1].split('static int qhap_open(')[0]
        self.assertLess(body.index('qhap_gate(h, false)'), body.index('cancel_work_sync'))
        self.assertLess(body.index('cancel_work_sync'), body.index('cancel_delayed_work_sync'))
        self.assertLess(body.index('cancel_delayed_work_sync'), body.index('guard(mutex)'))

    def test_stop_attempts_all_paths_without_early_error_return(self):
        body = DRIVER.split('static int qhap_stop(')[1].split('static void qhap_gate(')[0]
        self.assertEqual(body.count('return '), 1)
        self.assertLess(body.index('HAP_PLAY, 0'), body.index('HAP_EN_CTL, HAP_ENABLE, 0'))
        self.assertIn('qhap_auto_res(h, false)', body)

    def test_initialized_resources_and_managed_input_lifetime(self):
        body = DRIVER.split('static int qhap_probe(')[1].split('static void qhap_shutdown(')[0]
        for token in ['mutex_init', 'spin_lock_init', 'INIT_WORK', 'INIT_DELAYED_WORK']:
            self.assertLess(body.index(token), body.index('devm_add_action_or_reset'))
        self.assertLess(body.index('devm_add_action_or_reset'), body.index('devm_request_threaded_irq'))
        self.assertNotIn('input_unregister_device', DRIVER)
        self.assertNotIn('mutex_destroy', DRIVER)

    def test_trim_is_read_only_and_restricts_pmic_and_register(self):
        self.assertIn('.read_only = true', TRIM)
        self.assertNotIn('regmap_write', TRIM)
        self.assertNotIn('.reg_write', TRIM)
        self.assertIn('trim->address != 0x9f3', TRIM)
        self.assertIn('offset || bytes != 1', TRIM)
        self.assertIn('PMI8998_SUBTYPE', TRIM)
        self.assertIn('nvmem_cell_read_u8(dev, "rc-trim", &trim)', DRIVER)


@unittest.skipUnless(libfdt and DTB_DIR, 'set DIPPER_DTB_DIR and use schema Python')
class HapticsDTTests(unittest.TestCase):
    def compatible(self, tree, name, node=0):
        if tree.hasprop(node, 'compatible') and name in tree.getprop(node, 'compatible').as_stringlist():
            return node
        child = tree.first_subnode(node, quiet=(libfdt.NOTFOUND,))
        while child >= 0:
            found = self.compatible(tree, name, child)
            if found is not None:
                return found
            child = tree.next_subnode(child, quiet=(libfdt.NOTFOUND,))
        return None

    def test_wiring_matches_old_source_and_candidate_stays_disabled(self):
        # The full per-board suite separately asserts shared-hardware equivalence.
        tree = libfdt.Fdt((Path(DTB_DIR) / 'sdm845-xiaomi-dipper-mp-v2.1.dtb').read_bytes())
        hap = self.compatible(tree, 'qcom,pmi8998-haptics')
        self.assertIsNotNone(hap)
        self.assertEqual(tree.getprop(hap, 'status').as_str(), 'disabled')
        expected = {'reg': [0xc000], 'interrupts': [3, 0xc0, 0, 3],
                    'qcom,vmax-mv': [3100], 'qcom,wave-play-rate-us': [4878],
                    'qcom,ilim-ma': [800], 'qcom,sc-dbc-cycles': [8],
                    'qcom,lra-auto-res-mode': [2], 'qcom,lra-high-z': [1],
                    'qcom,lra-res-cal-period': [4], 'qcom,brake-pattern': [0, 0, 0, 0]}
        for name, cells in expected.items():
            self.assertEqual(tree.getprop(hap, name).as_uint32_list(), cells, name)
        provider = self.compatible(tree, 'qcom,pmi8998-rc-trim')
        self.assertIsNotNone(provider)
        self.assertEqual(tree.getprop(provider, 'reg').as_uint32_list(), [0x9f3])
        self.assertEqual(tree.getprop(tree.parent_offset(provider), 'reg').as_uint32_list()[0], 2)
        self.assertEqual(tree.getprop(tree.parent_offset(hap), 'reg').as_uint32_list()[0], 3)
        cell = tree.node_offset_by_phandle(tree.getprop(hap, 'nvmem-cells').as_uint32())
        self.assertEqual(tree.getprop(cell, 'reg').as_uint32_list(), [0, 1])
        self.assertEqual(tree.parent_offset(tree.parent_offset(cell)), provider)


if __name__ == '__main__':
    unittest.main()
