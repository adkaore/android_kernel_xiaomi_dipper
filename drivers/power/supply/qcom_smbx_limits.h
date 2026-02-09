/* SPDX-License-Identifier: GPL-2.0-only */
/* Native adaptation of awkoo's downstream ICL safeguard (139347ae71d2). */
#ifndef _QCOM_SMBX_LIMITS_H
#define _QCOM_SMBX_LIMITS_H

#define SMB2_ICL_REGISTER_MAX_UA	4800000
#define SMB2_ICL_STEP_UA		25000

/* A ceiling is not permission to draw that current; never round a request up. */
static inline int smb2_input_current_code(int requested_ua, unsigned int ceiling_ua)
{
	if (requested_ua < 0 || ceiling_ua < SMB2_ICL_STEP_UA ||
	    ceiling_ua > SMB2_ICL_REGISTER_MAX_UA || ceiling_ua % SMB2_ICL_STEP_UA)
		return -EINVAL;
	if ((unsigned int)requested_ua > ceiling_ua)
		requested_ua = ceiling_ua;
	return requested_ua / SMB2_ICL_STEP_UA;
}

#endif
