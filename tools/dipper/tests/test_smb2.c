/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include "../../../drivers/power/supply/qcom_smbx_limits.h"

int main(void)
{
	unsigned int ceilings[] = {3000000, 4800000};
	unsigned int i, ceiling;
	int request, code, previous;

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
	puts("SMB2 ICL: signed inputs, all requests 0..5 A at both ceilings, all valid ceiling boundaries passed");
	return 0;
}
