/* SPDX-License-Identifier: GPL-2.0-only */
/* Exact result patterns from the pinned Qualcomm/Xiaomi APSD result table. */
#ifndef _QCOM_SMBX_APSD_H
#define _QCOM_SMBX_APSD_H

#define APSD_RESULT_STATUS_MASK	GENMASK(6, 0)
#define QC_3P0_BIT		BIT(6)
#define QC_2P0_BIT		BIT(5)
#define FLOAT_CHARGER_BIT	BIT(4)
#define DCP_CHARGER_BIT		BIT(3)
#define CDP_CHARGER_BIT		BIT(2)
#define OCP_CHARGER_BIT		BIT(1)
#define SDP_CHARGER_BIT		BIT(0)

#define SMB2_APSD_MAX_TRIES	8

enum smb2_apsd_type {
	SMB2_APSD_UNKNOWN,
	SMB2_APSD_SDP,
	SMB2_APSD_CDP,
	SMB2_APSD_DCP,
};

static inline enum smb2_apsd_type smb2_decode_apsd(unsigned int status)
{
	switch (status & APSD_RESULT_STATUS_MASK) {
	case SDP_CHARGER_BIT:
		return SMB2_APSD_SDP;
	case CDP_CHARGER_BIT:
		return SMB2_APSD_CDP;
	case DCP_CHARGER_BIT:
	case OCP_CHARGER_BIT:
	case DCP_CHARGER_BIT | QC_2P0_BIT:
	case DCP_CHARGER_BIT | QC_3P0_BIT:
		/* A DCP classification is not a high-voltage/QC negotiation. */
		return SMB2_APSD_DCP;
	default:
		/* Floating, zero and conflicting result combinations stay unknown. */
		return SMB2_APSD_UNKNOWN;
	}
}

#endif
