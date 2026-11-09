/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>

#define BIT(n) (1U << (n))
#define GENMASK(hi, lo) (((1U << ((hi) + 1)) - 1) & ~((1U << (lo)) - 1))
enum typec_cc_status {
	TYPEC_CC_OPEN, TYPEC_CC_RA, TYPEC_CC_RD, TYPEC_CC_RP_DEF,
	TYPEC_CC_RP_1_5, TYPEC_CC_RP_3_0,
};
#include "../../../drivers/usb/typec/tcpm/qcom/qcom_pmic_typec_port_pmi8998.h"
#include "../../../drivers/usb/typec/tcpm/qcom/qcom_pmic_typec_pdphy_frame.h"

int main(void)
{
	unsigned int s1, s2, s4, header, size;
	unsigned long cases = 0;
	enum typec_cc_status cc1, cc2, active, other;
	int ret, expected;

	/* Every possible hardware byte tuple, not just advertised working cases. */
	for (s4 = 0; s4 < 256; s4++) {
		for (s1 = 0; s1 < 256; s1++) {
			for (s2 = 0; s2 < 256; s2++) {
				active = other = TYPEC_CC_OPEN;
				expected = 0;
				if ((s4 & 1) && !(s4 & 16)) {
					expected = -EAGAIN;
				} else if ((s4 & 1) && (s4 & 128)) {
					switch (s2 % 16) {
					case 1: active = other = TYPEC_CC_RA; break;
					case 2: active = other = TYPEC_CC_RD; break;
					case 4: active = TYPEC_CC_RD; other = TYPEC_CC_RA; break;
					case 8: active = TYPEC_CC_RD; break;
					default: expected = -EAGAIN;
					}
				} else if (s4 & 1) {
					switch (s1) {
					case 128: active = TYPEC_CC_RP_DEF; break;
					case 64: active = TYPEC_CC_RP_1_5; break;
					case 32: active = TYPEC_CC_RP_3_0; break;
					default: expected = -EAGAIN;
					}
				}
				cc1 = cc2 = TYPEC_CC_RP_3_0;
				ret = pmi8998_decode_cc(s1, s2, s4, &cc1, &cc2);
				assert(ret == expected);
				assert(cc1 == ((s4 & 2) ? other : active));
				assert(cc2 == ((s4 & 2) ? active : other));
				cases++;
			}
		}
	}
	for (header = 0; header < 65536; header++) {
		for (size = 0; size < 256; size++) {
			assert(qcom_pdphy_frame_valid(size, header) ==
			       (size + 1 == 2 + 4 * ((header / 4096) % 8)));
			cases++;
		}
	}
	assert(!qcom_pdphy_frame_valid(0xffffffff, 0));
	printf("Type-C CC and PD frame helpers: %lu exhaustive cases passed\n", cases);
	return 0;
}
