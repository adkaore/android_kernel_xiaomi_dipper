/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#define BIT(n) (1U << (n))
#include "../../../drivers/power/supply/qcom_smbx_limits.h"
#include "../../../drivers/power/supply/qcom_smbx_status.h"

int main(void)
{
	unsigned int ceilings[] = {3000000, 4800000};
	unsigned int i, ceiling;
	int request, code, previous;
	unsigned int status;
	enum smb2_health expected;

	assert(smb2_float_voltage_code(INT_MIN) == -EINVAL);
	assert(smb2_float_voltage_code(INT_MAX) == -EINVAL);
	assert(smb2_float_voltage_code(-1) == -EINVAL);
	assert(smb2_float_voltage_code(3487499) == -EINVAL);
	assert(smb2_float_voltage_code(4920001) == -EINVAL);
	for (request = 3487500; request <= 4920000; request++) {
		code = smb2_float_voltage_code(request);
		assert(code >= 0 && code <= 191);
		assert(3487500 + code * 7500 <= request);
		assert(request - (3487500 + code * 7500) < 7500);
	}
	assert(smb2_float_voltage_code(4400000) == 121);
	for (status = 0; status <= 255; status++) {
		if (status & 0x20)
			expected = SMB2_HEALTH_OVERVOLTAGE;
		else if ((status & 3) == 3)
			expected = SMB2_HEALTH_FAILURE;
		else if (status & 1)
			expected = SMB2_HEALTH_COLD;
		else if (status & 2)
			expected = SMB2_HEALTH_OVERHEAT;
		else if (status & 0x40)
			expected = SMB2_HEALTH_TIMER_EXPIRED;
		else if ((status & 0x10) || ((status & 12) == 12))
			expected = SMB2_HEALTH_FAILURE;
		else if (status & 4)
			expected = SMB2_HEALTH_COOL;
		else if (status & 8)
			expected = SMB2_HEALTH_WARM;
		else
			expected = SMB2_HEALTH_GOOD;
		assert(smb2_health_from_status(status) == expected);
		assert(smb2_charge_blocked(status) == !!(status & 0x73));
		if (status & 0x7f)
			assert(expected != SMB2_HEALTH_GOOD);
	}

	assert(smb2_input_current_code(-1, 3000000) == -EINVAL);
	assert(smb2_input_current_code(INT_MIN, 3000000) == -EINVAL);
	assert(smb2_input_current_code(500000, 0) == -EINVAL);
	assert(smb2_input_current_code(500000, 3000001) == -EINVAL);
	assert(smb2_input_current_code(500000, UINT_MAX) == -EINVAL);
	assert(smb2_input_current_code(500000, 4825000) == -EINVAL);
	for (i = 0; i < sizeof(ceilings) / sizeof(ceilings[0]); i++) {
		ceiling = ceilings[i];
		previous = 0;
		for (request = 0; request <= 5000000; request++) {
			code = smb2_input_current_code(request, ceiling);
			assert(code == (request > (int)ceiling ? (int)ceiling : request) / 25000);
			assert(code >= previous && code <= 192);
			assert(code * 25000 <= request);
			assert((unsigned int)code * 25000 <= ceiling);
			previous = code;
		}
	}
	for (ceiling = 25000; ceiling <= 4800000; ceiling += 25000) {
		assert(smb2_input_current_code(INT_MAX, ceiling) == (int)ceiling / 25000);
		assert(smb2_input_current_code(ceiling - 1, ceiling) == (int)ceiling / 25000 - 1);
		assert(smb2_input_current_code(ceiling + 1, ceiling) == (int)ceiling / 25000);
	}
	assert(smb2_input_current_code(5000000, 3000000) == 120);
	puts("SMB2: ICL bounds, all valid FV ceilings, invalid signed limits "
	     "and all 256 status bytes passed");
	return 0;
}
