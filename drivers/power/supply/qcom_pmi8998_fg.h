/* SPDX-License-Identifier: GPL-2.0-only */
/* Direct-register formats from the Qualcomm/Xiaomi GPLv2 FG GEN3 driver. */
#ifndef _QCOM_PMI8998_FG_H
#define _QCOM_PMI8998_FG_H

static inline unsigned int pmi8998_fg_word(const unsigned char *data,
					   unsigned int major)
{
	return major == 1 ? data[0] * 256U + data[1] : data[1] * 256U + data[0];
}

static inline int pmi8998_fg_capacity(unsigned int raw)
{
	if (!raw)
		return 0;
	if (raw >= 255)
		return 100;
	return ((raw - 1) * 98 + 126) / 253 + 1;
}

static inline unsigned int pmi8998_fg_voltage(unsigned int raw)
{
	return (unsigned long long)raw * 122070 / 1000;
}

/* PMIC-positive means discharge; power_supply-positive means charge. */
static inline int pmi8998_fg_current(unsigned int raw)
{
	int signed_raw = raw & 0x8000 ? (int)raw - 65536 : (int)raw;

	return -(long long)signed_raw * 488281 / 1000;
}

static inline int pmi8998_fg_temperature(const unsigned char *data)
{
	unsigned int raw = (data[1] & 7) * 256U + data[0];
	int mc = (int)raw * 250 - 273150;

	/* Quarter-kelvin register to nearest deci-Celsius, including negatives. */
	return mc < 0 ? (mc - 50) / 100 : (mc + 50) / 100;
}

#endif
