"""Checks on the compiled MP v2.1 DTB, not evidence of bootability."""
import os
from pathlib import Path
import unittest
try:
    import libfdt
except ImportError:
    libfdt = None

DTB = os.environ.get('DIPPER_DTB')


@unittest.skipUnless(libfdt and DTB, 'use dtschema Python and set DIPPER_DTB')
class BoardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tree = libfdt.Fdt(Path(DTB).read_bytes())
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
                         ['xiaomi,dipper-mp-v2.1', 'xiaomi,dipper', 'qcom,sdm845'])
        self.assertEqual(self.prop('/', 'qcom,board-id').as_uint32_list(), [55, 0])
        self.assertEqual(self.prop('/', 'qcom,msm-id').as_uint32_list(), [321, 0x20001])

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

    def test_trustzone_reservation_retained(self):
        self.assertEqual(self.prop('/reserved-memory/tz@86200000', 'reg').as_uint64_list(),
                         [0x86200000, 0x2d00000])

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

    def test_no_fabricated_pd_identity_or_ramoops(self):
        for node in self.nodes.values():
            for prop in ['sink-vdos', 'sink-vdos-v1', 'source-pdos']:
                self.assertFalse(self.tree.hasprop(node, prop))
        self.assertFalse(self.matching('ramoops'))


if __name__ == '__main__':
    unittest.main()
