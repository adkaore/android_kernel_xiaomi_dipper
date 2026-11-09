/* SPDX-License-Identifier: GPL-2.0-only */
/* Pure register encoders shared with the host regression tests. */
/* RC trim encoding derived from the GPLv2 Qualcomm/Xiaomi downstream driver. */
#ifndef _QCOM_SPMI_HAPTICS_H
#define _QCOM_SPMI_HAPTICS_H

/* Never round an amplitude above the board's voltage ceiling. */
static inline unsigned int qhap_vmax_code(unsigned int magnitude,
					  unsigned int max_mv)
{
	unsigned int steps = max_mv / 116;
	unsigned int value;

	if (!magnitude || !steps)
		return 0;
	if (steps > 31)
		steps = 31;
	if (magnitude > 65535)
		magnitude = 65535;
	value = (magnitude * steps + 32767) / 65535;
	return value ? value : 1;
}

/* Downstream RC19P2 trim: sign in bit 7, 0.7 percent per low-nibble step. */
static inline unsigned int qhap_rate_code(unsigned int period_us,
					  unsigned int trim)
{
	unsigned int error = (trim & 15) * 7;
	unsigned int scale = (trim & 128) ? 1000 - error : 1000 + error;

	return (period_us / 5) * scale / 1000;
}

/* Only the documented debounce values are accepted by the binding/parser. */
static inline unsigned int qhap_debounce_code(unsigned int cycles)
{
	return cycles == 32 ? 3 : cycles == 16 ? 2 : cycles == 8 ? 1 : 0;
}

#endif
