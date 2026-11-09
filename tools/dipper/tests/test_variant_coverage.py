"""Selector coverage and new-base overlay equivalence, not hardware parity."""
import json
import os
from pathlib import Path
import unittest

try:
    import libfdt
except ImportError:
    libfdt = None

ROOT = Path(__file__).resolve().parents[3]
DOC = ROOT / 'Documentation/dipper'
LEGACY = json.loads((DOC / 'legacy-resolved-boards.json').read_text())
MANIFEST = json.loads((DOC / 'board-targets.json').read_text())
TARGETS = MANIFEST['targets']
DTB_DIR = os.environ.get('DIPPER_DTB_DIR')


class CoverageManifestTests(unittest.TestCase):
    def test_all_listed_legacy_targets_have_one_explicit_mapping(self):
        expected = [v['source'] for v in LEGACY['variants'] if v['listed_build_target']]
        actual = [v['legacy_source'] for v in TARGETS]
        self.assertCountEqual(actual, expected)
        self.assertEqual(len(actual), len(set(actual)))
        self.assertFalse(MANIFEST['hardware_tested'])

    def test_unlisted_revision_one_is_not_advertised(self):
        old = next(v for v in LEGACY['variants'] if v['source'] == MANIFEST['unlisted_legacy_source'])
        self.assertFalse(old['listed_build_target'])
        self.assertEqual(old['msm_id'], [321, 0x10000])
        self.assertNotIn(old['source'], [v['legacy_source'] for v in TARGETS])

    def test_standalone_selector_pairs_are_unique(self):
        selectors = []
        for target in TARGETS:
            if target['kind'] != 'standalone':
                continue
            old = next(v for v in LEGACY['variants'] if v['source'] == target['legacy_source'])
            selectors.append((tuple(old['msm_id']), tuple(old['board_id'])))
        self.assertEqual(len(selectors), len(set(selectors)))

    def test_every_target_is_registered_and_has_a_source(self):
        directory = ROOT / 'arch/arm64/boot/dts/qcom'
        makefile = (directory / 'Makefile').read_text()
        for target in TARGETS:
            with self.subTest(target=target['dtb']):
                self.assertIn('+= ' + target['dtb'] + '\n', makefile)
                if target['kind'] == 'standalone':
                    source = (directory / target['dtb']).with_suffix('.dts').read_text()
                    self.assertIn('#include "sdm845-xiaomi-dipper-common.dtsi"', source)
                else:
                    self.assertTrue((directory / target['overlay']).with_suffix('.dtso').is_file())
                    self.assertTrue((directory / target['base']).with_suffix('.dts').is_file())
                    self.assertIn(target['dtb'].removesuffix('.dtb') + '-dtbs := '
                                  + target['base'] + ' ' + target['overlay'], makefile)


@unittest.skipUnless(libfdt and DTB_DIR, 'use dtschema Python and set DIPPER_DTB_DIR')
class CompiledCoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = Path(DTB_DIR)
        cls.trees = {}
        cls.snapshots = {}

    def tree(self, name):
        if name not in self.trees:
            self.trees[name] = libfdt.Fdt((self.directory / name).read_bytes())
        return self.trees[name]

    def snapshot(self, name):
        if name in self.snapshots:
            return self.snapshots[name]
        tree, result = self.tree(name), {}

        def walk(node, path):
            if path.startswith('/__'):
                return
            offset = tree.first_property_offset(node, quiet=(libfdt.NOTFOUND,))
            while offset >= 0:
                prop = tree.get_property_by_offset(offset)
                if prop.name not in ['phandle', 'linux,phandle']:
                    result[path, prop.name] = bytes(prop)
                offset = tree.next_property_offset(offset, quiet=(libfdt.NOTFOUND,))
            # Keep empty nodes in the comparison too.
            result[path, None] = b''
            child = tree.first_subnode(node, quiet=(libfdt.NOTFOUND,))
            while child >= 0:
                walk(child, path.rstrip('/') + '/' + tree.get_name(child))
                child = tree.next_subnode(child, quiet=(libfdt.NOTFOUND,))

        walk(0, '/')
        self.snapshots[name] = result
        return result

    def test_new_selection_overlays_equal_standalone_trees(self):
        for target in TARGETS:
            if target['kind'] == 'composed':
                with self.subTest(target=target['dtb']):
                    # Compare every property byte, including referenced phandle
                    # values, except generated node IDs and metadata. This does
                    # not compare the new plugin with an old MTP plugin.
                    self.assertEqual(self.snapshot(target['dtb']),
                                     self.snapshot(target['equivalent_dtb']))

    def test_new_plugins_retain_header_selectors(self):
        for target in TARGETS:
            if target['kind'] != 'composed':
                continue
            old = next(v for v in LEGACY['variants'] if v['source'] == target['legacy_source'])
            plugin = self.tree(target['overlay'])
            with self.subTest(plugin=target['overlay']):
                for name, key in [('qcom,msm-id', 'msm_id'), ('qcom,board-id', 'board_id')]:
                    self.assertEqual(plugin.getprop(0, name).as_uint32_list(), old[key])

    def test_common_base_cannot_masquerade_as_a_selected_board(self):
        for name in {v['base'] for v in TARGETS if v['kind'] == 'composed'}:
            base = self.tree(name)
            self.assertFalse(base.hasprop(0, 'qcom,msm-id'))
            self.assertFalse(base.hasprop(0, 'qcom,board-id'))
            self.assertEqual(base.getprop(0, 'compatible').as_stringlist()[0],
                             'xiaomi,dipper-common-base')
            self.assertGreaterEqual(base.path_offset('/__symbols__'), 0)

    def test_shared_hardware_cannot_drift_between_revision_wrappers(self):
        identity = {'model', 'compatible', 'qcom,msm-id', 'qcom,board-id'}
        for target in TARGETS:
            if target['kind'] != 'standalone':
                continue
            old = next(v for v in LEGACY['variants'] if v['source'] == target['legacy_source'])
            reference = next(v for v in TARGETS if v['kind'] == 'standalone'
                             and v['compatible'] == 'xiaomi,dipper-p0-v2'
                             + ('.1' if old['msm_id'][1] == 0x20001 else ''))
            current, previous = self.snapshot(target['dtb']), self.snapshot(reference['dtb'])
            changed = {key for key in current.keys() | previous.keys()
                       if current.get(key) != previous.get(key)
                       and not (key[0] == '/' and key[1] in identity)}
            # The old GB source explicitly disables this controller. It remains
            # disabled on all candidates until the GNSS driver is ported.
            if target['compatible'].endswith('-gb'):
                changed = {key for key in changed
                           if not (key[0].endswith('/spi@890000') and key[1] == 'status')}
            self.assertFalse(changed, (target['dtb'], changed))


if __name__ == '__main__':
    unittest.main()
