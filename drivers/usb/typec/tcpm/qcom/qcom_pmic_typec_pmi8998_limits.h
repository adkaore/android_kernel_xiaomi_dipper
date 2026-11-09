/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _QCOM_PMIC_TYPEC_PMI8998_LIMITS_H
#define _QCOM_PMIC_TYPEC_PMI8998_LIMITS_H

/* Until charger voltage/frequency policy is ported, only fixed 5 V contracts. */
static inline int pmi8998_pdo_supported(unsigned int pdo, unsigned int max_ma)
{
	unsigned int ma = (pdo & 1023) * 10;

	/* No FRS/unchunked-extended capability is implemented by this backend. */
	return !(pdo >> 30) && !(pdo & 0x01800000) &&
	       ((pdo >> 10) & 1023) == 100 && ma && ma <= max_ma;
}

#endif
