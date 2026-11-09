/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "../../../drivers/power/supply/qcom_pmi8998_fg.h"

int main(void)
{
	unsigned char bytes[2];
	unsigned int raw, previous_voltage = 0;
	int previous_soc = 0, previous_temperature = -10000, expected, soc;

	for (raw = 0; raw <= 65535; raw++) {
		bytes[0] = raw & 255;
		bytes[1] = raw >> 8;
		assert(pmi8998_fg_word(bytes, 2) == raw);
		assert(pmi8998_fg_word(bytes, 1) == (raw >> 8) + ((raw & 255) << 8));
		assert(pmi8998_fg_voltage(raw) == (unsigned long long)raw * 122070 / 1000);
		assert(pmi8998_fg_voltage(raw) >= previous_voltage);
		previous_voltage = pmi8998_fg_voltage(raw);
		expected = raw < 32768 ? (int)raw : (int)raw - 65536;
		assert(pmi8998_fg_current(raw) == -(long long)expected * 488281 / 1000);
	}
	for (raw = 0; raw <= 255; raw++) {
		soc = pmi8998_fg_capacity(raw);
		assert(soc >= previous_soc && soc >= 0 && soc <= 100);
		if (raw > 0 && raw < 255)
			assert(soc >= 1 && soc <= 99);
		previous_soc = soc;
	}
	assert(pmi8998_fg_capacity(0) == 0);
	assert(pmi8998_fg_capacity(255) == 100);
	for (raw = 0; raw < 2048; raw++) {
		bytes[0] = raw & 255;
		bytes[1] = raw >> 8;
		expected = pmi8998_fg_temperature(bytes);
		assert(expected >= previous_temperature);
		previous_temperature = expected;
		bytes[1] |= 0xf8; /* Reserved high bits do not change temperature. */
		assert(pmi8998_fg_temperature(bytes) == expected);
	}
	bytes[0] = 0xb0; bytes[1] = 4; /* 300 kelvin. */
	assert(pmi8998_fg_temperature(bytes) == 269);
	assert(pmi8998_fg_current(1) == -488);
	assert(pmi8998_fg_current(65535) == 488);
	puts("FG encoders: all raw words, SOC endpoints, revision byte order, current sign and temperatures passed");
	return 0;
}
