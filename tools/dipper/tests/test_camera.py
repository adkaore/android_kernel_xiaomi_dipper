"""Source-only IMX363 regression checks; no sensor I/O or image-quality claims."""
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]
PATH = 'drivers/media/i2c/imx363.c'


def body(source, name):
    start = source.index('static int imx363_' + name + '(')
    return source[start:source.index('\n}', start)]


def register_bytes(source, name):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    match = re.search(r'\b' + name + r'\[\]\s*=\s*\{(.*?)\n\};', source, re.S)
    return [(int(a, 0), int(b, 0)) for a, b in re.findall(
        r'\{\s*CCI_REG8\((0x[0-9a-fA-F]+)\),\s*(0x[0-9a-fA-F]+)\s*\}', match[1])]


class CameraTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / PATH).read_text()
        cls.original = subprocess.check_output(['git', 'show', '9b50edadb057:' + PATH], cwd=ROOT, text=True)

    def test_full_resolution_registers_unchanged(self):
        actual = register_bytes(self.source, 'mode_4032x3024_regs')
        self.assertTrue(actual)
        self.assertEqual(actual, register_bytes(self.original, 'mode_4032x3024_regs'))

    def test_frame_length_matches_registers(self):
        regs = dict(register_bytes(self.source, 'mode_4032x3024_regs'))
        value = (regs[0x0340] << 8) | regs[0x0341]
        declared = int(re.search(r'#define IMX363_VTS_DEFAULT\s+(\d+)', self.source)[1])
        self.assertEqual(declared, value)

    def test_only_one_candidate_mode(self):
        start = self.source.index('supported_modes[]')
        end = self.source.index('\n};', start)
        self.assertEqual(re.findall(r'\.width = (\d+)', self.source[start:end]), ['4032'])
        self.assertNotIn('V4L2_CID_WIDE_DYNAMIC_RANGE', self.source)
        self.assertNotIn('.get_selection =', self.source)

    def test_repeated_stream_call_guarded(self):
        self.assertIn('imx363->streaming == !!enable', body(self.source, 'set_stream'))
        self.assertIn('__v4l2_ctrl_grab', body(self.source, 'set_stream'))

    def test_active_format_change_refused_while_streaming(self):
        self.assertIn('return -EBUSY;', body(self.source, 'set_pad_format'))

    def test_clock_before_reset_release(self):
        fn = body(self.source, 'power_on')
        self.assertLess(fn.index('clk_prepare_enable'), fn.index('regulator_bulk_enable'))
        self.assertLess(fn.index('regulator_bulk_enable'), fn.index('reset_gpio, 0'))
        self.assertIn('clk_disable_unprepare', fn)

    def test_endpoint_error_freed(self):
        fn = body(self.source, 'probe')
        start = fn.index('if (IS_ERR(imx363->reset_gpio))')
        self.assertIn('goto error_endpoint_free;', fn[start:start + 150])
        self.assertIn('ep.bus.mipi_csi2.num_data_lanes != 4', fn)

    def test_exposure_bound_to_frame_length(self):
        self.assertIn('imx363->cur_mode->vts_def - IMX363_EXPOSURE_OFFSET', self.source)

    def test_license_preserved(self):
        self.assertTrue(self.source.startswith('// SPDX-License-Identifier: GPL-2.0\n'))
        self.assertIn('Copyright (C) 2018 Intel Corporation', self.source)


if __name__ == '__main__':
    unittest.main()
