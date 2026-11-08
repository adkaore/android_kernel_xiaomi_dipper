/* SPDX-License-Identifier: GPL-2.0-only */
/* FTM5 packet layout derived from STMicroelectronics' GPLv2 fts_521 driver. */
#ifndef _STMFTS_FTM5_H
#define _STMFTS_FTM5_H

#define STMFTM5_EVENT_SIZE 8
#define STMFTM5_FIFO_DEPTH 32
#define STMFTM5_MAX_CONTACTS 10

struct stmftm5_contact {
	unsigned int slot;
	unsigned int x;
	unsigned int y;
	unsigned int pressure;
	unsigned int major;
	unsigned int minor;
	unsigned int palm;
};

enum stmftm5_event {
	STMFTM5_IGNORED,
	STMFTM5_CONTACT,
	STMFTM5_RELEASE,
};

/* Caller supplies exactly one eight-byte FIFO event. No allocation or I/O. */
static inline enum stmftm5_event
stmftm5_decode(const unsigned char event[STMFTM5_EVENT_SIZE],
	       struct stmftm5_contact *contact)
{
	unsigned int id = event[0] >> 4;
	unsigned int type = event[1] & 0xf;
	unsigned int slot = event[1] >> 4;

	if ((event[0] & 3) != 3 || slot >= STMFTM5_MAX_CONTACTS)
		return STMFTM5_IGNORED;
	if (id == 3) {
		contact->slot = slot;
		return STMFTM5_RELEASE;
	}
	if (id != 1 && id != 2)
		return STMFTM5_IGNORED;
	/* Only finger, glove and palm are described by this input device. */
	if (type != 1 && type != 2 && type != 4)
		return STMFTM5_IGNORED;

	contact->slot = slot;
	contact->x = event[2] | ((event[3] & 0xf) << 8);
	contact->y = (event[4] << 4) | (event[3] >> 4);
	contact->pressure = event[5] ? event[5] : 1;
	contact->major = ((event[0] & 0xc) << 2) | (event[6] >> 4);
	contact->minor = ((event[7] & 0xc0) >> 2) | (event[6] & 0xf);
	contact->palm = type == 4;

	return STMFTM5_CONTACT;
}
#endif
