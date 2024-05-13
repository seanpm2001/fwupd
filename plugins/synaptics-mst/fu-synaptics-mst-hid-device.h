/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_MST_HID_DEVICE (fu_synaptics_mst_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsMstHidDevice,
		     fu_synaptics_mst_hid_device,
		     FU,
		     SYNAPTICS_MST_HID_DEVICE,
		     FuHidDevice)
