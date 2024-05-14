/*
 * Copyright 2024 Richard hughes <Richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

guint16
fu_synaptics_vmm9_crc16(const guint8 *buf, gsize bufsz, guint16 init);
