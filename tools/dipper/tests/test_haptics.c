/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "../../../drivers/input/misc/qcom-spmi-haptics.h"

int main(void)
{
	unsigned int max_mv, strength, previous, trim, period, code, reference;

	for (max_mv = 116; max_mv <= 3596; max_mv++) {
		previous = 0;
		/* All magnitudes at the dipper ceiling; boundaries at every ceiling. */
		for (strength = 0; strength <= 65535; strength += max_mv == 3100 ? 1 : 257) {
			code = qhap_vmax_code(strength, max_mv);
			assert(code >= previous && code <= 31);
			assert(code * 116 <= max_mv);
			assert(strength ? code > 0 : code == 0);
			previous = code;
		}
		assert(qhap_vmax_code(65535, max_mv) == max_mv / 116);
	}
	assert(qhap_vmax_code(65535, 3100) == 26);
	assert(qhap_vmax_code(0, 3100) == 0);
	assert(qhap_vmax_code(65536, 3100) == 26);
	for (period = 5; period <= 20475; period++) {
		for (trim = 0; trim <= 255; trim++) {
			reference = (period / 5) *
				((trim & 128) ? 1000 - (trim & 15) * 7 :
				 1000 + (trim & 15) * 7) / 1000;
			assert(qhap_rate_code(period, trim) == reference);
		}
	}
	assert(qhap_rate_code(4878, 0) == 975);
	assert(qhap_rate_code(4878, 15) == 1077);
	assert(qhap_rate_code(4878, 143) == 872);
	assert(qhap_debounce_code(0) == 0);
	assert(qhap_debounce_code(8) == 1);
	assert(qhap_debounce_code(16) == 2);
	assert(qhap_debounce_code(32) == 3);
	puts("Haptics encoders: voltage ceiling/monotonicity, all period/trim bytes, debounce passed");
	return 0;
}
