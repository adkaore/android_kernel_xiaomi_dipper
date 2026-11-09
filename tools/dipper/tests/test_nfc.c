/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "../../../drivers/nfc/nxp-nci/frame.h"

int main(void)
{
	unsigned char header[2];
	unsigned int value, length, decoded;
	unsigned long available;
	int valid;

	assert(!nxp_nci_fw_frame_size(NULL, 0, &decoded));
	assert(!nxp_nci_fw_frame_size(NULL, 1, &decoded));
	for (value = 0; value <= 65535; value++) {
		header[0] = value >> 8;
		header[1] = value & 255;
		length = value & 0x3ff;
		for (available = 0; available <= 1026; available++) {
			decoded = 0xdeadbeef;
			valid = nxp_nci_fw_frame_size(header, available, &decoded);
			assert(valid == (available >= 2 && length && length <= available - 2));
			if (valid)
				assert(decoded == length);
			else
				assert(decoded == 0xdeadbeef);
		}
	}
	for (available = 0; available < 1100; available++)
		assert(nxp_nci_fw_has_status(available) == (available >= 5));
	puts("NFC frame parser: all 65536 headers and lengths 0..1026, truncated status cases passed");
	return 0;
}
