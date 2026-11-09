/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _NXP_NCI_FRAME_H
#define _NXP_NCI_FRAME_H

/* Image header length is two, followed by at least one payload byte. */
static inline int nxp_nci_fw_frame_size(const unsigned char *data,
					unsigned long size, unsigned int *payload)
{
	unsigned int length;

	if (size < 2)
		return 0;
	length = (data[0] * 256U + data[1]) & 0x3ff;
	if (!length || length > size - 2)
		return 0;
	*payload = length;
	return 1;
}

/* Response: two-byte header, status byte, two-byte CRC. */
static inline int nxp_nci_fw_has_status(unsigned long size)
{
	return size >= 5;
}

#endif
