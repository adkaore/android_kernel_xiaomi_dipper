"""Source guards and protocol cross-check; runtime decoder is tested in C."""
from pathlib import Path
import re
import subprocess
import unittest
ROOT = Path(__file__).resolve().parents[3]


class TouchSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / 'drivers/input/touchscreen/stmfts-ftm5.c').read_text()
        cls.vendor = subprocess.check_output(['git', 'show',
            '0b3e38ef6c40e0a346dfa0241aeceaf5540e91f6:drivers/input/touchscreen/fts_521/fts_lib/ftsSoftware.h'],
            cwd=ROOT, text=True)

    def test_protocol_ids_match_old_kernel(self):
        for name, value in [('EVT_ID_CONTROLLER_READY', 0x03), ('EVT_ID_ENTER_POINT', 0x13),
                            ('EVT_ID_MOTION_POINT', 0x23), ('EVT_ID_LEAVE_POINT', 0x33),
                            ('EVT_ID_ERROR', 0xf3), ('FTS_CMD_SCAN_MODE', 0xa0)]:
            match = re.search(r'#define\s+' + name + r'\s+(0x[0-9a-fA-F]+)', self.vendor)
            self.assertEqual(int(match[1], 0), value)

    def test_no_flash_or_manufacturing_api(self):
        # Only the three-byte runtime scan command may use the write transport.
        self.assertEqual(self.source.count('i2c_master_send('), 1)
        for forbidden in ['request_firmware(', 'flashProcedure(', 'proc_create(',
                          'DEVICE_ATTR(', 'copy_from_user(', 'sysfs_create_group(']:
            self.assertNotIn(forbidden, self.source)
        self.assertRegex(self.source, r'0xa0, 0x00, enable \? 0x01 : 0x00')

    def test_interrupt_is_not_enabled_before_start(self):
        self.assertIn('IRQF_NO_AUTOEN', self.source)
        self.assertIn('atomic_set(&ts->irq_enabled, 0)', self.source)
        self.assertIn('synchronize_irq(ts->client->irq)', self.source)

    def test_fault_stops_level_irq(self):
        self.assertIn('disable_irq_nosync(irq)', self.source)
        self.assertIn('atomic_xchg(&ts->irq_enabled, 0)', self.source)
        self.assertNotIn('schedule_work(', self.source)


if __name__ == '__main__':
    unittest.main()
