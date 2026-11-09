"""Checks on every compiled candidate DTB, not evidence of bootability."""
import os
import json
from pathlib import Path
import unittest
try:
    import libfdt
except ImportError:
    libfdt = None

DTB = os.environ.get('DIPPER_DTB')
DTB_DIR = os.environ.get('DIPPER_DTB_DIR')
ROOT = Path(__file__).resolve().parents[3]
LEGACY = json.loads((ROOT / 'Documentation/dipper/legacy-resolved-boards.json').read_text())
TARGETS = json.loads((ROOT / 'Documentation/dipper/board-targets.json').read_text())['targets']


@unittest.skipUnless(libfdt and (DTB or DTB_DIR), 'use dtschema Python and set DIPPER_DTB_DIR')
class BoardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = Path(cls.binary) if hasattr(cls, 'binary') else Path(DTB)
        cls.variant = next(v for v in TARGETS if v['dtb'] == binary.name)
        cls.legacy = next(v for v in LEGACY['variants']
                          if v['source'] == cls.variant['legacy_board_source'])
        cls.tree = libfdt.Fdt(binary.read_bytes())
        cls.nodes = {}
        def walk(offset, path):
            cls.nodes[path] = offset
            child = cls.tree.first_subnode(offset, quiet=(libfdt.NOTFOUND,))
            while child >= 0:
                walk(child, path.rstrip('/') + '/' + cls.tree.get_name(child))
                child = cls.tree.next_subnode(child, quiet=(libfdt.NOTFOUND,))
        walk(0, '/')

    def prop(self, path, name):
        return self.tree.getprop(self.nodes[path], name)

    def matching(self, compatible):
        return [(p, n) for p, n in self.nodes.items() if self.tree.hasprop(n, 'compatible')
                and compatible in self.tree.getprop(n, 'compatible').as_stringlist()]

    def test_board_identity(self):
        self.assertEqual(self.prop('/', 'compatible').as_stringlist(),
                         [self.variant['compatible'], 'xiaomi,dipper', 'qcom,sdm845'])
        self.assertEqual(self.prop('/', 'qcom,board-id').as_uint32_list(), self.legacy['board_id'])
        self.assertEqual(self.prop('/', 'qcom,msm-id').as_uint32_list(), self.legacy['msm_id'])

    def test_fixed_reserved_regions_do_not_overlap(self):
        regions = []
        for path, node in self.nodes.items():
            if not path.startswith('/reserved-memory/') or not self.tree.hasprop(node, 'reg'):
                continue
            data = self.tree.getprop(node, 'reg').as_uint64_list()
            self.assertEqual(len(data) % 2, 0)
            for start, size in zip(data[::2], data[1::2]):
                if size:
                    regions.append((start, start + size, path))
        regions.sort()
        for previous, current in zip(regions, regions[1:]):
            self.assertLessEqual(previous[1], current[0], (previous, current))

    def test_legacy_carveouts(self):
        expected = {'memory@8c500000': (0x8c500000, 0x1e00000),
                    'memory@8e300000': (0x8e300000, 0x100000),
                    'memory@8e400000': (0x8e400000, 0x7800000),
                    'memory@95c00000': (0x95c00000, 0x500000),
                    'xbl-mem@85d00000': (0x85d00000, 0x200000)}
        for node, region in expected.items():
            self.assertEqual(tuple(self.prop('/reserved-memory/' + node, 'reg').as_uint64_list()), region)

    def test_board_override_of_secure_reservation_retained(self):
        self.assertEqual(self.prop('/reserved-memory/tz@86200000', 'reg').as_uint64_list(),
                         [0x86200000, 0x4700000])

    def test_every_final_legacy_fixed_region_stays_reserved(self):
        regions = []
        for path, node in self.nodes.items():
            if path.startswith('/reserved-memory/') and self.tree.hasprop(node, 'reg'):
                reg = self.prop(path, 'reg').as_uint64_list()
                for start, size in zip(reg[::2], reg[1::2]):
                    regions.append((start, start + size, self.tree.hasprop(node, 'no-map')))
        for old in LEGACY['fixed_memory_layouts'][self.legacy['fixed_memory_layout']]:
            with self.subTest(legacy_region=old['path']):
                cursor = int(old['start'], 0)
                end = cursor + int(old['size'], 0)
                for start, stop, no_map in sorted(regions):
                    if old['no_map'] and not no_map:
                        continue
                    if start <= cursor < stop:
                        cursor = stop
                self.assertGreaterEqual(cursor, end,
                                        f'Unreserved legacy memory starts at {cursor:#x}')

    def test_rmtfs_is_dynamic_and_guarded(self):
        path = self.matching('qcom,rmtfs-mem')[0][0]
        self.assertFalse(self.tree.hasprop(self.nodes[path], 'reg'))
        self.assertTrue(self.tree.hasprop(self.nodes[path], 'qcom,use-guard-pages'))
        self.assertEqual(self.prop(path, 'size').as_uint64(), 0x202000)

    def test_no_device_specific_mac(self):
        for node in self.nodes.values():
            for prop in ['mac-address', 'local-mac-address']:
                self.assertFalse(self.tree.hasprop(node, prop))

    def test_no_always_on_camera_rails(self):
        for path, node in self.nodes.items():
            if 'cam-' in path or 'camera' in path:
                self.assertFalse(self.tree.hasprop(node, 'regulator-always-on'), path)

    def test_panel_reset_active_low(self):
        path = self.matching('samsung,ea8074')[0][0]
        cells = self.prop(path, 'reset-gpios').as_uint32_list()
        self.assertEqual(cells[1:], [6, 1])
        self.assertTrue(self.tree.hasprop(self.nodes[path], 'vddio-supply'))

    def test_usb_peripheral_only(self):
        path = self.matching('snps,dwc3')[0][0]
        self.assertEqual(self.prop(path, 'dr_mode').as_str(), 'peripheral')
        self.assertFalse(self.tree.hasprop(self.nodes[path], 'usb-role-switch'))

    def test_touch_candidate_stays_gated(self):
        path = self.matching('st,stmfts-ftm5')[0][0]
        self.assertEqual(self.prop(path, 'status').as_str(), 'disabled')
        self.assertEqual(self.prop(path, 'touchscreen-size-x').as_uint32(), 1080)
        self.assertEqual(self.prop(path, 'touchscreen-size-y').as_uint32(), 2248)
        self.assertEqual(self.prop(path, 'reset-gpios').as_uint32_list()[1:], [32, 1])
        for supply in ['vdd-supply', 'avdd-supply']:
            regulator = self.tree.node_offset_by_phandle(self.prop(path, supply).as_uint32())
            self.assertFalse(self.tree.hasprop(regulator, 'regulator-always-on'))

    def test_no_fabricated_pd_identity(self):
        for node in self.nodes.values():
            for prop in ['sink-vdos', 'sink-vdos-v1', 'source-pdos']:
                self.assertFalse(self.tree.hasprop(node, prop))

    def test_ramoops_matches_pinned_board_source(self):
        self.assertEqual(len(self.matching('ramoops')), 1)
        path = self.matching('ramoops')[0][0]
        self.assertEqual(self.prop(path, 'reg').as_uint64_list(), [0xb0000000, 0x400000])
        for name, value in self.legacy['ramoops'].items():
            self.assertEqual(self.prop(path, name).as_uint32(), value, name)

    def test_unported_gnss_spi_is_not_accidentally_enabled(self):
        # GB explicitly excludes it downstream. Other boards also keep it gated
        # until the BCM4775 interface/power/IRQ path has actually been ported.
        for path, node in self.matching('qcom,geni-spi'):
            if path.endswith('/spi@890000'):
                self.assertEqual(self.prop(path, 'status').as_str(), 'disabled')
                break
        else:
            self.fail('Missing spi4 controller')


def load_tests(loader, tests, pattern):
    if not DTB_DIR:
        return tests
    suite = unittest.TestSuite()
    for target in TARGETS:
        board_class = type('Board_' + target['dtb'].removesuffix('.dtb'), (BoardTests,),
                           {'binary': str(Path(DTB_DIR) / target['dtb'])})
        suite.addTests(loader.loadTestsFromTestCase(board_class))
    return suite


if __name__ == '__main__':
    unittest.main()
