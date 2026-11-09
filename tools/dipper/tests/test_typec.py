"""Offline source/DT contracts, not TCPM emulation or electrical validation."""
from pathlib import Path
import json
import os
import re
import unittest
try:
    import libfdt
except ImportError:
    libfdt = None

ROOT = Path(__file__).resolve().parents[3]
QCOM = ROOT / 'drivers/usb/typec/tcpm/qcom'
PORT = (QCOM / 'qcom_pmic_typec_port_pmi8998.c').read_text()
PDPHY = (QCOM / 'qcom_pmic_typec_pdphy.c').read_text()
GLUE = (QCOM / 'qcom_pmic_typec.c').read_text()
VBUS = (ROOT / 'drivers/regulator/qcom_usb_vbus-regulator.c').read_text()
CHARGER = (ROOT / 'drivers/power/supply/qcom_smbx.c').read_text()
DTB_DIR = os.environ.get('DIPPER_DTB_DIR')


class TypeCSourceTests(unittest.TestCase):
    def test_real_pmic_and_register_bases_required(self):
        for source in [PORT, VBUS]:
            self.assertIn('pmic->subtype != PMI8998_SUBTYPE', source)
            self.assertIn('to_spmi_device(dev->parent)->usid & 1', source)
        self.assertIn('base != 0x1300', PORT)
        self.assertIn('base != 0x1100', VBUS)

    def test_prepare_before_synchronous_tcpm_init(self):
        self.assertLess(GLUE.index('ret = tcpm->port_prepare(tcpm)'), GLUE.index('= tcpm_register_port('))
        self.assertLess(GLUE.index('ret = tcpm->pdphy_prepare(tcpm)'), GLUE.index('= tcpm_register_port('))
        cleanup = GLUE.split('cleanup:')[1].split('static void')[0]
        self.assertLess(cleanup.index('tcpm->pdphy_stop(tcpm)'), cleanup.index('tcpm_unregister_port('))
        self.assertLess(cleanup.index('tcpm->port_stop(tcpm)'), cleanup.index('tcpm_unregister_port('))

    def test_open_is_disable_not_drp_search(self):
        case = PORT.split('case TYPEC_CC_OPEN:')[1].split('case TYPEC_CC_RD:')[0]
        self.assertIn('power_role = TYPEC_DISABLE_CMD_BIT', case)
        self.assertNotIn('power_role = 0', case)
        self.assertIn('ret < required_ua', PORT)

    def test_irq_and_io_lifetimes(self):
        self.assertIn('IRQF_ONESHOT | IRQF_NO_AUTOEN', PORT)
        self.assertIn('port->tcpm_port && !port->stopping', PORT)
        stop = PORT.split('static void pmi8998_typec_port_stop(')[1]
        self.assertLess(stop.index('port->stopping = true'), stop.index('disable_irq(port->irq)'))
        self.assertLess(stop.index('disable_irq(port->irq)'), stop.index('port->tcpm_port = NULL'))
        stop = PDPHY.split('static void qcom_pmic_typec_pdphy_stop(')[1]
        self.assertLess(stop.index('disable_irq('), stop.index('cancel_work_sync('))
        self.assertLess(stop.index('cancel_work_sync('), stop.index('pdphy->tcpm_port = NULL'))
        self.assertNotIn('spin_lock', PDPHY)

    def test_bounded_cc_snapshot_and_no_vbus_error_to_zero(self):
        self.assertIn('tries < 3', PORT)
        self.assertIn('check == status[3]', PORT)
        getter = PORT.split('static int pmi8998_typec_port_get_vbus(')[1].split('static int')[0]
        self.assertNotIn('!ret &&', getter)
        self.assertIn('VBUS status unavailable', getter)

    def test_pd_input_validation_and_signal_type(self):
        receiver = PDPHY.split('static void qcom_pmic_typec_pdphy_pd_receive(')[1].split('static irqreturn_t')[0]
        self.assertIn('struct pd_message msg = {}', receiver)
        self.assertIn('qcom_pdphy_frame_valid(size, le16_to_cpu(msg.header))', receiver)
        self.assertIn('(rx_status & RX_FRAME_TYPE) != TCPC_TX_SOP', receiver)
        self.assertIn('goto ack', receiver)
        self.assertIn('if (!ret)\n\t\ttcpm_pd_receive', receiver)
        signal = PDPHY.split('qcom_pmic_typec_pdphy_pd_transmit_signal(')[1].split('qcom_pmic_typec_pdphy_pd_transmit_payload')[0]
        self.assertIn('if (type == TCPC_TX_CABLE_RESET)', signal)
        self.assertNotIn('type == TCPC_TX_CABLE_RESET ||', signal)

    def test_pmi8998_has_its_own_current_table(self):
        table = VBUS.split('pmi8998_curr_table[] = {')[1].split('}')[0]
        self.assertEqual([int(v) for v in re.findall(r'\d+', table)],
                         list(range(250000, 1500001, 250000)))
        self.assertIn('devm_kmemdup(dev, template', VBUS)
        self.assertIn('regmap_read_poll_timeout(', VBUS)
        self.assertIn('cleanup = pmi8998_vbus_disable(rdev)', VBUS)

    def test_charger_does_not_overwrite_a_managed_port(self):
        init = CHARGER.split('static int smb_init_hw(')[1].split('static int smb_init_irq')[0]
        self.assertIn('for_each_available_child_of_node_scoped', init)
        self.assertIn('of_device_is_compatible(child, "qcom,pmi8998-typec")', init)
        self.assertIn('if (typec_managed &&', init)
        self.assertIn('smb_init_seq[i].addr == TYPE_C_INTRPT_ENB_SOFTWARE_CTRL', init)
        self.assertIn('smb_init_seq[i].addr == TYPE_C_CFG', init)

    def test_host_enum_matches_kernel(self):
        kernel = (ROOT / 'include/linux/usb/tcpm.h').read_text().split('enum typec_cc_status {')[1].split('}')[0]
        host = (ROOT / 'tools/dipper/tests/test_typec.c').read_text().split('enum typec_cc_status {')[1].split('}')[0]
        self.assertEqual(re.findall(r'TYPEC_CC_\w+', kernel), re.findall(r'TYPEC_CC_\w+', host))


@unittest.skipUnless(libfdt and DTB_DIR, 'set DIPPER_DTB_DIR and use schema Python')
class TypeCDTTests(unittest.TestCase):
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

    def test_all_candidates_remain_gated_without_foreign_connector_policy(self):
        targets = json.loads((ROOT / 'Documentation/dipper/board-targets.json').read_text())['targets']
        for target in targets:
            with self.subTest(dtb=target['dtb']):
                tree = libfdt.Fdt((Path(DTB_DIR) / target['dtb']).read_bytes())
                offset = self.find(tree, 'qcom,pmi8998-typec')
                self.assertIsNotNone(offset)
                self.assertEqual(tree.getprop(offset, 'status').as_str(), 'disabled')
                self.assertEqual(tree.getprop(offset, 'reg').as_uint32_list(), [0x1300, 0x1700])
                self.assertFalse(tree.hasprop(offset, 'source-pdos'))
                self.assertLess(tree.subnode_offset(offset, 'connector', quiet=(libfdt.NOTFOUND,)), 0)
                self.assertEqual(tree.getprop(offset, 'interrupts').as_uint32_list(),
                                 [2, 0x13, 7, 3] + [v for irq in range(7) for v in [2, 0x17, irq, 1]])
                boost = tree.node_offset_by_phandle(tree.getprop(offset, 'vdd-vbus-supply').as_uint32())
                self.assertEqual(tree.getprop(boost, 'compatible').as_stringlist(), ['qcom,pmi8998-vbus-reg'])
                self.assertEqual(tree.getprop(boost, 'status').as_str(), 'disabled')
                self.assertEqual(tree.getprop(boost, 'regulator-max-microamp').as_uint32(), 500000)
                supply = tree.node_offset_by_phandle(tree.getprop(offset, 'vdd-pdphy-supply').as_uint32())
                self.assertEqual(tree.getprop(supply, 'regulator-min-microvolt').as_uint32(), 3088000)
                self.assertEqual(tree.getprop(supply, 'regulator-max-microvolt').as_uint32(), 3088000)


if __name__ == '__main__':
    unittest.main()
