/*
 * Copyright 2024 Richard hughes <Richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-synaptics-vmm9-common.h"

guint16
fu_synaptics_vmm9_crc16(const guint8 *buf, gsize bufsz, guint16 init)
{
	guint16 crc = init;
	for (gsize len = 0; len < bufsz; len++) {
		crc ^= ((guint16)buf[len] << 8);
		for (guint8 i = 0; i < 8; i++) {
			if (crc & (1u << 15)) {
				crc = (crc << 1) ^ 0x8005;
			} else {
				crc <<= 1;
			}
		}
	}
	return crc;
}
