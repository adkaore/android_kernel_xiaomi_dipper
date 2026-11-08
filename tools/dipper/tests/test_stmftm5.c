// SPDX-License-Identifier: GPL-2.0-only
/* Exercise the actual pure decoder, not a separately reimplemented model. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../../drivers/input/touchscreen/stmfts-ftm5.h"

static uint32_t rng = 0x10293847;
static uint32_t next(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

int main(void)
{
	unsigned char event[8] = {0x13, 0x91, 0x34, 0x61, 0x45, 0, 0xab, 0xc0};
	struct stmftm5_contact p;
	unsigned int i, slot, type;

	assert(stmftm5_decode(event, &p) == STMFTM5_CONTACT);
	assert(p.slot == 9 && p.x == 0x134 && p.y == 0x456);
	assert(p.pressure == 1 && p.major == 10 && p.minor == 59 && !p.palm);
	for (slot = 0; slot < 16; slot++) {
		for (type = 0; type < 16; type++) {
			event[0] = 0x13;
			event[1] = (slot << 4) | type;
			assert(stmftm5_decode(event, &p) ==
			       ((slot < 10 && (type == 1 || type == 2 || type == 4)) ?
				STMFTM5_CONTACT : STMFTM5_IGNORED));
			event[0] = 0x33;
			assert(stmftm5_decode(event, &p) ==
			       (slot < 10 ? STMFTM5_RELEASE : STMFTM5_IGNORED));
		}
	}
	for (i = 0; i < 1000000; i++) {
		unsigned char copy[8];
		unsigned int j;
		enum stmftm5_event kind;

		for (j = 0; j < 8; j++)
			event[j] = next();
		memcpy(copy, event, 8);
		kind = stmftm5_decode(event, &p);
		assert(!memcmp(copy, event, 8));
		if (kind == STMFTM5_CONTACT) {
			assert(p.slot < 10 && p.x <= 4095 && p.y <= 4095);
			assert(p.pressure >= 1 && p.pressure <= 255);
			assert(p.major <= 63 && p.minor <= 63);
			assert(p.palm <= 1);
		} else if (kind == STMFTM5_RELEASE) {
			assert(p.slot < 10);
		}
	}
	puts("FTM5 decoder: targeted cases and 1000000 fuzz packets passed");
	return 0;
}
