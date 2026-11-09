/* SPDX-License-Identifier: GPL-2.0 */
/* Register decoding shared with the host regression tests. */
#ifndef _QCOM_PMIC_TYPEC_PORT_PMI8998_H
#define _QCOM_PMIC_TYPEC_PORT_PMI8998_H

#define PMI8998_CC_ATTACHED	BIT(0)
#define PMI8998_CC_ORIENTATION	BIT(1)
#define PMI8998_VCONN_OC		BIT(2)
#define PMI8998_CC_DEBOUNCED	BIT(4)
#define PMI8998_VBUS_PRESENT	BIT(6)
#define PMI8998_DFP_MODE		BIT(7)

/* Do not turn an unknown/transient hardware combination into an attachment. */
static inline int pmi8998_decode_cc(unsigned int status1, unsigned int status2,
				    unsigned int status4,
				    enum typec_cc_status *cc1,
				    enum typec_cc_status *cc2)
{
	enum typec_cc_status active, other = TYPEC_CC_OPEN;

	*cc1 = TYPEC_CC_OPEN;
	*cc2 = TYPEC_CC_OPEN;
	if (!(status4 & PMI8998_CC_ATTACHED))
		return 0;
	if (!(status4 & PMI8998_CC_DEBOUNCED))
		return -EAGAIN;
	if (status4 & PMI8998_DFP_MODE) {
		switch (status2 & GENMASK(3, 0)) {
		case BIT(0):
			active = TYPEC_CC_RA;
			other = TYPEC_CC_RA;
			break;
		case BIT(1):
			active = TYPEC_CC_RD;
			other = TYPEC_CC_RD;
			break;
		case BIT(2):
			active = TYPEC_CC_RD;
			other = TYPEC_CC_RA;
			break;
		case BIT(3):
			active = TYPEC_CC_RD;
			break;
		default:
			return -EAGAIN;
		}
	} else {
		/* The legacy decoder accepts only these exact UFP status bytes. */
		switch (status1) {
		case BIT(7):
			active = TYPEC_CC_RP_DEF;
			break;
		case BIT(6):
			active = TYPEC_CC_RP_1_5;
			break;
		case BIT(5):
			active = TYPEC_CC_RP_3_0;
			break;
		default:
			return -EAGAIN;
		}
	}
	*cc1 = status4 & PMI8998_CC_ORIENTATION ? other : active;
	*cc2 = status4 & PMI8998_CC_ORIENTATION ? active : other;
	return 0;
}

#endif
