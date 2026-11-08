#!/usr/bin/env python3
"""Source-regression checks against pinned vendor DT, not hardware emulation."""
import json
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]
PANEL = ROOT / 'drivers/gpu/drm/panel/panel-samsung-ea8074.c'
LEGACY_PATH = 'arch/arm64/boot/dts/qcom/dsi-panel-ss-notch-fhd-ea8074-cmd.dtsi'


def packets(data):
    result = []
    while data:
        if len(data) < 7:
            raise ValueError('truncated packet header')
        size = int.from_bytes(data[5:7], 'big')
        if not size or len(data) < 7 + size:
            raise ValueError('invalid packet payload')
        result.append((data[:7], data[7:7 + size]))
        data = data[7 + size:]
    return result


def seqs(code):
    result = []
    for match in re.finditer(r'mipi_dsi_dcs_write_seq_multi\(&dsi_ctx,\s*([^;]+)\);', code):
        words = [x.strip() for x in match[1].split(',')]
        if words[0].startswith('MIPI_'):
            continue
        result.append(bytes(int(x, 0) for x in words))
    return result


class PanelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        lock = json.loads((ROOT / 'Documentation/dipper/sources.json').read_text())
        ref = next(x['commit'] for x in lock['sources'] if x['name'] == 'legacy')
        cls.vendor = subprocess.check_output(['git', 'show', f'{ref}:{LEGACY_PATH}'], cwd=ROOT, text=True)
        cls.source = PANEL.read_text()

    def vendor_packets(self, direction):
        match = re.search(r'qcom,mdss-dsi-' + direction + r'-command\s*=\s*\[([^]]+)\]', self.vendor)
        return packets(bytes.fromhex(match[1]))

    def function(self, name):
        start = self.source.index('static int ss_notch_fhd_ea8074_' + name + '(')
        end = self.source.index('\n}\n', start)
        return self.source[start:end]

    def test_vendor_initialization_bytes(self):
        expected = [p for _, p in self.vendor_packets('on') if p[0] >= 0xb0]
        self.assertEqual(seqs(self.function('on')), expected)

    def test_vendor_poweroff_bytes(self):
        expected = [p for _, p in self.vendor_packets('off') if p[0] >= 0xb0]
        self.assertEqual(seqs(self.function('unprepare')), expected)

    def test_low_power_commands(self):
        for direction in ('on', 'off'):
            self.assertIn(f'qcom,mdss-dsi-{direction}-command-state = "dsi_lp_mode"', self.vendor)
        flags = re.findall(r'dsi->mode_flags\s*([|&]?=)\s*([^;]+);', self.source)
        self.assertEqual(flags, [('=', 'MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM')])

    def test_no_transfer_to_unpowered_backlight(self):
        body = self.function('bl_update_status')
        self.assertLess(body.index('guard(mutex)'), body.index('if (!ctx->initialized)'))
        self.assertLess(body.index('if (!ctx->initialized)'), body.index('mipi_dsi_dcs_set_display_brightness'))

    def test_poweroff_even_after_command_error(self):
        body = self.function('unprepare')
        self.assertIn('return ret;', body)
        self.assertNotIn('return dsi_ctx.accum_err;', body)
        self.assertLess(body.index('enter_sleep_mode'), body.index('regulator_bulk_disable'))

    def test_zero_initial_dcs_brightness(self):
        self.assertIn('set_display_brightness_large(dsi, 0)', self.function('on'))

    def test_reset_initially_asserted(self):
        self.assertIn('devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH)', self.function('probe'))

    def test_parser_rejects_truncation(self):
        for raw in (b'\x39', bytes.fromhex('39 00 00 00 00 00 03 f0')):
            with self.assertRaises(ValueError):
                packets(raw)

    def test_all_vendor_packets_consumed(self):
        self.assertEqual(len(self.vendor_packets('on')), 30)
        self.assertEqual(len(self.vendor_packets('off')), 9)


if __name__ == '__main__':
    unittest.main()
