/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _QCOM_PMIC_TYPEC_PDPHY_FRAME_H
#define _QCOM_PMIC_TYPEC_PDPHY_FRAME_H

/* RX_SIZE is the wire length minus one; a PD header is two bytes. */
static inline int qcom_pdphy_frame_valid(unsigned int size, unsigned int header)
{
	return size >= 1 && size <= 29 && size == 1 + 4 * ((header >> 12) & 7);
}

#endif
