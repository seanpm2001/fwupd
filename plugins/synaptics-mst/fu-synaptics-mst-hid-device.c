/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-synaptics-mst-firmware.h"
#include "fu-synaptics-mst-hid-device.h"
#include "fu-synaptics-mst-struct.h"

/* this can be set using Flags=example in the quirk file  */
#define FU_SYNAPTICS_MST_HID_DEVICE_FLAG_EXAMPLE (1 << 0)

struct _FuSynapticsMstHidDevice {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuSynapticsMstHidDevice, fu_synaptics_mst_hid_device, FU_TYPE_HID_DEVICE)

static void
fu_synaptics_mst_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	//	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);
	//	fwupd_codec_string_append_hex(str, idt, "StartAddr", self->start_addr);
}

static gboolean
fu_synaptics_mst_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* TODO: switch the device into bootloader mode */
	g_assert(self != NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_synaptics_mst_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* TODO: switch the device into runtime mode */
	g_assert(self != NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

// static gboolean
// fu_synaptics_mst_hid_device_reload(FuDevice *device, GError **error)
//{
//	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);
//	/* TODO: reprobe the hardware, or delete this vfunc to use ->setup() as a fallback */
//	g_assert(self != NULL);
//	return TRUE;
// }

// static gboolean
// fu_synaptics_mst_hid_device_probe(FuDevice *device, GError **error)
//{
//	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);
//
/* FuHidDevice->probe */
//	if (!FU_DEVICE_CLASS(fu_synaptics_mst_hid_device_parent_class)->probe(device, error))
//		return FALSE;

/* TODO: probe the device for properties available before it is opened */
//	if (fu_device_has_private_flag(device, FU_SYNAPTICS_MST_HID_DEVICE_FLAG_EXAMPLE))
//		self->start_addr = 0x100;
//
//	/* success */
//	return TRUE;
//}

#define FU_SYNAPTICS_MST_HID_DEVICE_REPORT_SIZE 62
#define FU_SYNAPTICS_MST_HID_DEVICE_TIMEOUT	5000 /* ms */

static gboolean
fu_synaptics_mst_hid_device_command_set(FuSynapticsMstHidDevice *self,
					guint32 offset,
					const guint8 *buf,
					gsize bufsz,
					GError **error)
{
	guint8 checksum;
	g_autoptr(FuStructHidSetCommand) st_req = fu_struct_hid_set_command_new();
	g_autoptr(FuStructHidPayload) st_payload_req = fu_struct_hid_payload_new();

	/* payload */
	fu_struct_hid_payload_set_ctrl(st_payload_req, FU_SYNAPTICS_MST_UPDC_CMD_ENABLE_RC | 0x80);
	fu_struct_hid_payload_set_offset(st_payload_req, offset);
	fu_struct_hid_payload_set_length(st_payload_req, bufsz);
	if (!fu_struct_hid_payload_set_fifo(st_payload_req, buf, bufsz, error))
		return FALSE;

	/* request */
	fu_struct_hid_set_command_set_size(st_req, FU_STRUCT_HID_PAYLOAD_OFFSET_FIFO + bufsz);
	if (!fu_struct_hid_set_command_set_payload(st_req, st_payload_req, error))
		return FALSE;
	checksum = 0x100 - fu_sum8(st_req->data + 1, FU_STRUCT_HID_SET_COMMAND_SIZE - 2);
	if (!fu_memwrite_uint8_safe(st_req->data,
				    st_req->len,
				    FU_STRUCT_HID_SET_COMMAND_OFFSET_PAYLOAD +
					FU_STRUCT_HID_PAYLOAD_OFFSET_FIFO + bufsz,
				    checksum,
				    error))
		return FALSE;
	fu_byte_array_set_size(st_req, FU_SYNAPTICS_MST_HID_DEVICE_REPORT_SIZE, 0x0);

	/* send to hardware */
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_STRUCT_HID_SET_COMMAND_DEFAULT_ID,
				      st_req->data,
				      st_req->len,
				      FU_SYNAPTICS_MST_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send packet: ");
		return FALSE;
	}

	fu_dump_raw(G_LOG_DOMAIN, "REPLY", st_req->data, st_req->len);
	// FIXME: Do we need to parse the reply and check RC_STS?
#if 0
16:00:50.618 FuHidDevice          HID::SetReport [wValue=0x0201, wIndex=0]:
01 00 11 00 00 81 00 00 00 00 00 05 00 00 00 50 52 49 55 53 d6 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
16:00:50.618 FuHidDevice          HID::SetReport [wValue=0x0201, wIndex=0]:
01 00 0c 00 00 b1 00 38 a0 20 20 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 27 00 00 00 00 00 00 00 00 00 00 00 00 00 00
16:00:50.618 FuHidDevice          HID::GetReport [wValue=0x0101, wIndex=0]:
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
16:00:50.619 FuHidDevice          HID::GetReport [wValue=0x0101, wIndex=0]:
01 00 2c b4 01 31 00 3c a0 20 20 04 00 00 00 02 09 08 50 53 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 17 00 00 00 00 00 00 00 00 00 00 00 00 00 00
16:00:50.619 FuPluginSynapticsMST SUCCESS!
#endif

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_hid_device_command_set_get(FuSynapticsMstHidDevice *self,
					    guint32 offset,
					    guint8 *buf,
					    gsize bufsz,
					    GError **error)
{
	guint32 length = 4;
	guint8 buf_res[FU_SYNAPTICS_MST_HID_DEVICE_REPORT_SIZE] = {0};
	guint8 checksum;
	g_autoptr(FuStructHidGetCommand) st_res = NULL;
	g_autoptr(FuStructHidPayload) st_payload_req = fu_struct_hid_payload_new();
	g_autoptr(FuStructHidPayload) st_payload_res = NULL;
	g_autoptr(FuStructHidSetCommand) st_req = fu_struct_hid_set_command_new();

	/* payload */
	fu_struct_hid_payload_set_ctrl(st_payload_req,
				       FU_SYNAPTICS_MST_UPDC_CMD_READ_FROM_MEMORY | 0x80);
	fu_struct_hid_payload_set_offset(st_payload_req, offset);
	fu_struct_hid_payload_set_length(st_payload_req, length);
	//	if (!fu_struct_hid_payload_set_fifo(st_payload_req, buf, bufsz, error))
	//		return FALSE;

	/* request */
	fu_struct_hid_set_command_set_size(st_req, FU_STRUCT_HID_PAYLOAD_OFFSET_FIFO);
	if (!fu_struct_hid_set_command_set_payload(st_req, st_payload_req, error))
		return FALSE;
	checksum = 0x100 - fu_sum8(st_req->data + 1, FU_STRUCT_HID_SET_COMMAND_SIZE - 2);
	if (!fu_memwrite_uint8_safe(st_req->data,
				    st_req->len,
				    FU_STRUCT_HID_SET_COMMAND_OFFSET_PAYLOAD +
					FU_STRUCT_HID_PAYLOAD_OFFSET_FIFO +
					FU_STRUCT_HID_PAYLOAD_SIZE_FIFO,
				    checksum,
				    error))
		return FALSE;
	fu_byte_array_set_size(st_req, FU_SYNAPTICS_MST_HID_DEVICE_REPORT_SIZE, 0x0);

	/* set */
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_STRUCT_HID_SET_COMMAND_DEFAULT_ID,
				      st_req->data,
				      st_req->len,
				      FU_SYNAPTICS_MST_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send packet: ");
		return FALSE;
	}

	/* get, and parse */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_STRUCT_HID_GET_COMMAND_DEFAULT_ID,
				      buf_res,
				      sizeof(buf_res),
				      FU_SYNAPTICS_MST_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send packet: ");
		return FALSE;
	}
	st_res = fu_struct_hid_get_command_parse(buf_res, sizeof(buf_res), 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* sanity check */
	st_payload_res = fu_struct_hid_get_command_get_payload(st_res);
	if (fu_struct_hid_payload_get_sts(st_payload_res) != FU_SYNAPTICS_MST_UPDC_RC_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "error code %s [0x%x]",
			    fu_synaptics_mst_updc_rc_to_string(
				fu_struct_hid_payload_get_sts(st_payload_res)),
			    fu_struct_hid_payload_get_sts(st_payload_res));
		return FALSE;
	}
	if (offset != fu_struct_hid_payload_get_offset(st_payload_res)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "payload offset invalid: got 0x%x, expected 0x%x",
			    fu_struct_hid_payload_get_offset(st_payload_res),
			    offset);
		return FALSE;
	}
	if (length != fu_struct_hid_payload_get_length(st_payload_res)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "payload length invalid: got 0x%x, expected 0x%x",
			    fu_struct_hid_payload_get_length(st_payload_res),
			    length);
		return FALSE;
	}

	/* payload is optional */
	if (buf != NULL) {
		gsize fifosz = 0;
		const guint8 *fifo = fu_struct_hid_payload_get_fifo(st_payload_res, &fifosz);
		if (!fu_memcpy_safe(buf,
				    bufsz,
				    0x0, /* dst */
				    fifo,
				    fifosz,
				    0x0, /*src */
				    bufsz,
				    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_hid_device_setup(FuDevice *device, GError **error)
{
	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);
	guint8 payload[] = {'P', 'R', 'I', 'U', 'S'};

	/* HidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_synaptics_mst_hid_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_synaptics_mst_hid_device_command_set(self, 0x0, payload, sizeof(payload), error))
		return FALSE;
	if (!fu_synaptics_mst_hid_device_command_set_get(self, 0x2020A038, NULL, 0, error))
		return FALSE;
	g_warning("SUCCESS!");

	/* TODO: get the version and other properties from the hardware while open */
	fu_device_set_version(device, "1.2.3");

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_hid_device_prepare(FuDevice *device,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);
	/* TODO: anything the device has to do before the update starts */
	g_assert(self != NULL);
	return TRUE;
}

static gboolean
fu_synaptics_mst_hid_device_cleanup(FuDevice *device,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);
	/* TODO: anything the device has to do when the update has completed */
	g_assert(self != NULL);
	return TRUE;
}

static FuFirmware *
fu_synaptics_mst_hid_device_prepare_firmware(FuDevice *device,
					     GInputStream *stream,
					     FwupdInstallFlags flags,
					     GError **error)
{
	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_synaptics_mst_firmware_new();
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	// FIXME: verify customer and board ID
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaptics_mst_hid_device_write_blocks(FuSynapticsMstHidDevice *self,
					 FuChunkArray *chunks,
					 FuProgress *progress,
					 GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
#if 0
		/* TODO: send to hardware */
		guint8 buf[64] = { 0x12, 0x24, 0x0 }; /* TODO: this is the preamble */

		/* TODO: copy in payload */
		if (!fu_memcpy_safe(buf, sizeof(buf), 0x2, /* TODO: copy to dst at offset */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      0x01,
					      buf,
					      sizeof(buf),
					      5000, /* ms */
					      FU_HID_DEVICE_FLAG_NONE,
					      error)) {
			g_prefix_error(error, "failed to send packet: ");
			return FALSE;
		}
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      0x01,
					      buf,
					      sizeof(buf),
					      5000, /* ms */
					      FU_HID_DEVICE_FLAG_NONE,
					      error)) {
			g_prefix_error(error, "failed to receive packet: ");
			return FALSE;
		}
#endif

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_hid_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35, NULL);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

#if 0
1. Enable the remote update feature.
RC_Command_SET( RC_CMD_ENABLE, 5, 0, “PRIUS”)
2. Erase the storage bank.
For (i = 0; i<5; i++)
{
Data[ 0 ] = 0x3; Data[1] = i;
RC_Command_SET( RC_CMD_ERASE_SPI_Flash, 2, 0, Data)
}
3. Wait 3 seconds to ensure the SPI flash is ready to access the write command.
Sleep( 3000 );
4. Loop to write all the firmware data to the flash.
RC_Command_SET( RC_CMD_Firmware_update, fwSize, offset, Data)
5. Calculate the checksum of the firmware data in SPI flash to ensure the firmware update is successful.
RC_Command_SETGET( RC_CMD_CRC16CHECK, fwSize, offset, NULL, 4, Data)
6. When the image is updated, check and activate the firmware.
RC_Command_SETGET( RC_CMD_UPDE_ACTIVATE_FIRMWARE, fwSize, offset, NULL, 4, Data)
7. Disable the remote update feature.
RC_Command_SET( RC_CMD_DISABLE, 0, 0, NULL)
8. Power on the hub after updating the firmware.
Once updated, power off/on the VMM plus chip.
#endif

	/* write each block */
	chunks = fu_chunk_array_new_from_stream(stream, 0x0, 64 /* block_size */, error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_synaptics_mst_hid_device_write_blocks(self,
						      chunks,
						      fu_progress_get_child(progress),
						      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* TODO: verify each block */
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_synaptics_mst_hid_device_set_quirk_kv(FuDevice *device,
					 const gchar *key,
					 const gchar *value,
					 GError **error)
{
	//	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(device);

	/* TODO: parse value from quirk file */
	if (g_strcmp0(key, "SynapticsMstHidStartAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		//		self->start_addr = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_synaptics_mst_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_synaptics_mst_hid_device_init(FuSynapticsMstHidDevice *self)
{
	//	self->start_addr = 0x5000;
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.mst");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	//	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_AUTODETECT_EPS);
	//	fu_hid_device_add_flag(FU_HID_DEVICE(self),
	// FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_SYNAPTICS_MST_FIRMWARE);
	//	fu_device_register_private_flag(FU_DEVICE(self),
	//					FU_SYNAPTICS_MST_HID_DEVICE_FLAG_EXAMPLE,
	//					"example");
	//	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_RETRY_FAILURE);
}

// static void
// fu_synaptics_mst_hid_device_finalize(GObject *object)
//{
//	FuSynapticsMstHidDevice *self = FU_SYNAPTICS_MST_HID_DEVICE(object);
//
//	/* TODO: free any allocated instance state here */
//	g_assert(self != NULL);
//
//	G_OBJECT_CLASS(fu_synaptics_mst_hid_device_parent_class)->finalize(object);
// }

static void
fu_synaptics_mst_hid_device_class_init(FuSynapticsMstHidDeviceClass *klass)
{
	//	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	//	object_class->finalize = fu_synaptics_mst_hid_device_finalize;
	device_class->to_string = fu_synaptics_mst_hid_device_to_string;
	//	device_class->probe = fu_synaptics_mst_hid_device_probe;
	device_class->setup = fu_synaptics_mst_hid_device_setup;
	//	device_class->reload = fu_synaptics_mst_hid_device_reload;
	device_class->prepare = fu_synaptics_mst_hid_device_prepare;
	device_class->cleanup = fu_synaptics_mst_hid_device_cleanup;
	device_class->attach = fu_synaptics_mst_hid_device_attach;
	device_class->detach = fu_synaptics_mst_hid_device_detach;
	device_class->prepare_firmware = fu_synaptics_mst_hid_device_prepare_firmware;
	device_class->write_firmware = fu_synaptics_mst_hid_device_write_firmware;
	device_class->set_quirk_kv = fu_synaptics_mst_hid_device_set_quirk_kv;
	device_class->set_progress = fu_synaptics_mst_hid_device_set_progress;
}
