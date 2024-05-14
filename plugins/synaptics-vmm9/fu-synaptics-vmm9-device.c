/*
 * Copyright 2024 Richard hughes <Richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-synaptics-vmm9-common.h"
#include "fu-synaptics-vmm9-device.h"
#include "fu-synaptics-vmm9-firmware.h"
#include "fu-synaptics-vmm9-struct.h"

/* Flags */
#define FU_SYNAPTICS_VMM9_DEVICE_FLAG_MANUAL_RESTART_REQUIRED (1 << 0)

struct _FuSynapticsVmm9Device {
	FuHidDevice parent_instance;
	guint16 board_id;
};

G_DEFINE_TYPE(FuSynapticsVmm9Device, fu_synaptics_vmm9_device, FU_TYPE_HID_DEVICE)

#define FU_SYNAPTICS_VMM9_DEVICE_REPORT_SIZE 62
#define FU_SYNAPTICS_VMM9_DEVICE_TIMEOUT     5000 /* ms */

#define FU_SYNAPTICS_VMM9_CTRL_BUSY_MASK 0x80
#define FU_SYNAPTICS_VMM9_BUSY_POLL	 100 /* ms */

#define FU_SYNAPTICS_VMM9_MEM_OFFSET_CUSTOMER_ID 0x9000024E
#define FU_SYNAPTICS_VMM9_MEM_OFFSET_BOARD_ID	 0x9000024F

static void
fu_synaptics_vmm9_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "BoardId", self->board_id);
}

static gboolean
fu_synaptics_vmm9_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	/* generic request */
	if (fu_device_has_private_flag(device,
				       FU_SYNAPTICS_VMM9_DEVICE_FLAG_MANUAL_RESTART_REQUIRED)) {
		g_autoptr(FwupdRequest) request = fwupd_request_new();
		fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
		fwupd_request_set_id(request, FWUPD_REQUEST_ID_REPLUG_POWER);
		fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
		if (!fu_device_emit_request(device, request, progress, error))
			return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

typedef enum {
	FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE = 0,
	FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER = 1 << 0,
	FU_SYNAPTICS_VMM9_COMMAND_FLAG_NO_REPLY = 1 << 1,
} FuSynapticsVmm9DeviceCommandFlags;

typedef struct {
	guint8 *buf;
	gsize bufsz;
} FuSynapticsVmm9DeviceCommandHelper;

static gboolean
fu_synaptics_vmm9_device_command_cb(FuDevice *self, gpointer user_data, GError **error)
{
	FuSynapticsVmm9DeviceCommandHelper *helper =
	    (FuSynapticsVmm9DeviceCommandHelper *)user_data;
	guint8 buf[FU_SYNAPTICS_VMM9_DEVICE_REPORT_SIZE] = {0};
	g_autoptr(FuStructHidPayload) st_payload = NULL;
	g_autoptr(FuStructHidGetCommand) st = NULL;

	/* get, and parse */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_STRUCT_HID_GET_COMMAND_DEFAULT_ID,
				      buf,
				      sizeof(buf),
				      FU_SYNAPTICS_VMM9_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send packet: ");
		return FALSE;
	}
	st = fu_struct_hid_get_command_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;

	/* sanity check */
	st_payload = fu_struct_hid_get_command_get_payload(st);
	if (fu_struct_hid_payload_get_sts(st_payload) != FU_SYNAPTICS_VMM9_RC_STS_SUCCESS) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "sts is %s [0x%x]",
		    fu_synaptics_vmm9_rc_sts_to_string(fu_struct_hid_payload_get_sts(st_payload)),
		    fu_struct_hid_payload_get_sts(st_payload));
		return FALSE;
	}
#if 0
	/* offset is auto-incremented by the MCU */
	if (0 && offset != fu_struct_hid_payload_get_offset(st_payload)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "payload offset invalid: got 0x%x, expected 0x%x",
			    fu_struct_hid_payload_get_offset(st_payload),
			    offset);
		return FALSE;
	}
#endif
#if 0
	if (helper->bufsz != fu_struct_hid_payload_get_length(st_payload)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "payload length invalid: got 0x%x, expected 0x%x",
			    fu_struct_hid_payload_get_length(st_payload),
			    (guint) helper->bufsz);
		return FALSE;
	}
#endif

	/* check the busy status */
	if (fu_struct_hid_payload_get_ctrl(st_payload) & FU_SYNAPTICS_VMM9_CTRL_BUSY_MASK) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "is busy");
		return FALSE;
	}

	/* payload is optional */
	if (helper->buf != NULL) {
		gsize fifosz = 0;
		const guint8 *fifo = fu_struct_hid_payload_get_fifo(st_payload, &fifosz);
		if (!fu_memcpy_safe(helper->buf,
				    helper->bufsz,
				    0x0, /* dst */
				    fifo,
				    fifosz,
				    0x0, /*src */
				    helper->bufsz,
				    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_command(FuSynapticsVmm9Device *self,
				 FuSynapticsVmm9RcCtrl ctrl,
				 guint32 offset,
				 const guint8 *src_buf,
				 gsize src_bufsz,
				 guint8 *dst_buf,
				 gsize dst_bufsz,
				 FuSynapticsVmm9DeviceCommandFlags flags,
				 GError **error)
{
	FuSynapticsVmm9DeviceCommandHelper helper = {.buf = dst_buf, .bufsz = dst_bufsz};
	guint8 checksum;
	goffset offset_checksum;
	g_autoptr(FuStructHidPayload) st_payload = fu_struct_hid_payload_new();
	g_autoptr(FuStructHidSetCommand) st = fu_struct_hid_set_command_new();
	g_autofree gchar *str = NULL;

	/* payload */
	fu_struct_hid_payload_set_ctrl(st_payload, ctrl | FU_SYNAPTICS_VMM9_CTRL_BUSY_MASK);
	fu_struct_hid_payload_set_offset(st_payload, offset);
	fu_struct_hid_payload_set_length(st_payload, src_bufsz);
	if (src_buf != NULL) {
		if (!fu_struct_hid_payload_set_fifo(st_payload, src_buf, src_bufsz, error))
			return FALSE;
	}

	/* request */
	fu_struct_hid_set_command_set_size(st, FU_STRUCT_HID_PAYLOAD_OFFSET_FIFO + src_bufsz);
	if (!fu_struct_hid_set_command_set_payload(st, st_payload, error))
		return FALSE;
	if (flags & FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER) {
		offset_checksum = FU_STRUCT_HID_GET_COMMAND_SIZE - 1;
	} else {
		offset_checksum = FU_STRUCT_HID_SET_COMMAND_OFFSET_PAYLOAD +
				  FU_STRUCT_HID_PAYLOAD_OFFSET_FIFO + src_bufsz;
	}
	checksum = 0x100 - fu_sum8(st->data + 1, offset_checksum - 1);
	if (!fu_memwrite_uint8_safe(st->data, st->len, offset_checksum, checksum, error))
		return FALSE;
	fu_byte_array_set_size(st, FU_SYNAPTICS_VMM9_DEVICE_REPORT_SIZE, 0x0);

	/* set */
	str = fu_struct_hid_set_command_to_string(st);
	g_debug("%s", str);
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_STRUCT_HID_SET_COMMAND_DEFAULT_ID,
				      st->data,
				      st->len,
				      FU_SYNAPTICS_VMM9_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to send packet: ");
		return FALSE;
	}

	/* disregard */
	if (flags & FU_SYNAPTICS_VMM9_COMMAND_FLAG_NO_REPLY)
		return TRUE;

	/* poll for success */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_synaptics_vmm9_device_command_cb,
				  FU_SYNAPTICS_VMM9_DEVICE_TIMEOUT / FU_SYNAPTICS_VMM9_BUSY_POLL,
				  FU_SYNAPTICS_VMM9_BUSY_POLL, /* ms */
				  &helper,
				  error)) {
		g_prefix_error(error, "failed to wait for !busy: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_setup(FuDevice *device, GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	guint32 customer_id;
	guint32 board_id;
	guint8 buf[4] = {0x0};

#if 1
	//	guint32 temperature;
	g_warning("getting CHIP TEMP");
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_GET_CHIP_CORE_TEMPERATURE,
					      0x0,
					      NULL,
					      4,
					      buf,
					      4,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error))
		return FALSE;
		// g_warning("SUCCESS!!!: %s", fu_device_to_string(device));
#endif

	/* read customer ID */
	g_warning("getting customer ID");
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_READ_FROM_MEMORY,
					      FU_SYNAPTICS_VMM9_MEM_OFFSET_CUSTOMER_ID,
					      NULL,
					      sizeof(buf),
					      buf,
					      sizeof(buf),
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error))
		return FALSE;
	customer_id = fu_memread_uint32(buf, G_BIG_ENDIAN);
	fu_device_add_instance_u32(device, "CID", customer_id);
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "CID", NULL);

	/* read board ID */
	g_warning("getting board ID");
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_READ_FROM_MEMORY,
					      FU_SYNAPTICS_VMM9_MEM_OFFSET_BOARD_ID,
					      NULL,
					      sizeof(buf),
					      buf,
					      sizeof(buf),
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error))
		return FALSE;
	board_id = fu_memread_uint32(buf, G_BIG_ENDIAN);
	fu_device_add_instance_u32(device, "BID", board_id);
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "BID", NULL);
	//	g_warning("SUCCESS!!!: %s", fu_device_to_string(device));

#if 0
{
	guint8 buf[13] = {0};
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_READ_FROM_TX_DPCD,
					      0x0, //offset
					      NULL,
					      0,
					      buf,
					      sizeof(buf),
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error))
		return FALSE;
	g_warning("SUCCESS!!!: %s", fu_device_to_string(device));
}
#endif

	//	g_warning("SUCCESS!: %s", fu_device_to_string(device));

	/* TODO: get the version and other properties */
	fu_device_set_version(device, "1.2.3");

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_open(FuDevice *device, GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	guint8 payload[] = {'P', 'R', 'I', 'U', 'S'};

	/* HidDevice->open */
	if (!FU_DEVICE_CLASS(fu_synaptics_vmm9_device_parent_class)->open(device, error))
		return FALSE;

	/* unconditionally disable, then enable RC with the magic token */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_DISABLE_RC,
					      0x0,
					      NULL,
					      0,
					      NULL,
					      0,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NO_REPLY,
					      error)) {
		g_prefix_error(error, "failed to DISABLE_RC before ENABLE_RC: ");
		return FALSE;
	}
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_ENABLE_RC,
					      0x0,
					      payload,
					      sizeof(payload),
					      NULL,
					      0,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_FULL_BUFFER,
					      error)) {
		g_prefix_error(error, "failed to ENABLE_RC: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_close(FuDevice *device, GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);

	/* no magic token required */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_DISABLE_RC,
					      0,
					      NULL,
					      0x0,
					      NULL,
					      0x0,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
					      error)) {
		g_prefix_error(error, "failed to DISABLE_RC: ");
		return FALSE;
	}

	/* HidDevice->close */
	if (!FU_DEVICE_CLASS(fu_synaptics_vmm9_device_parent_class)->close(device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_synaptics_vmm9_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_synaptics_vmm9_firmware_new();

	/* verify this firmware is for this hardware */
	if (self->board_id !=
	    fu_synaptics_vmm9_firmware_get_board_id(FU_SYNAPTICS_VMM9_FIRMWARE(firmware))) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "start address mismatch, got 0x%04x, expected 0x%04x",
		    fu_synaptics_vmm9_firmware_get_board_id(FU_SYNAPTICS_VMM9_FIRMWARE(firmware)),
		    self->board_id);
		return NULL;
	}

	/* parse */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaptics_vmm9_device_write_blocks(FuSynapticsVmm9Device *self,
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

		if (!fu_synaptics_vmm9_device_command(self,
						      FU_SYNAPTICS_VMM9_RC_CTRL_FLASH_ERASE,
						      fu_chunk_get_address(chk),
						      fu_chunk_get_data(chk),
						      fu_chunk_get_data_sz(chk),
						      NULL,
						      0,
						      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
						      error)) {
			g_prefix_error(error,
				       "failed at page %u, @0x%x",
				       fu_chunk_get_idx(chk),
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_erase(FuSynapticsVmm9Device *self, FuProgress *progress, GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(NULL,
				    fu_device_get_firmware_size_max(FU_DEVICE(self)),
				    0,
				    0x0 /* pagesz*/,
				    64 * 1024);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 buf[2] = {i, 0x3};
		if (!fu_synaptics_vmm9_device_command(self,
						      FU_SYNAPTICS_VMM9_RC_CTRL_FLASH_ERASE,
						      0x0,
						      buf,
						      2,
						      NULL,
						      0,
						      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
						      error)) {
			g_prefix_error(error,
				       "failed at page %u, @0x%x",
				       fu_chunk_get_idx(chk),
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_crc16_cb(const guint8 *buf,
				  gsize bufsz,
				  gpointer user_data,
				  GError **error)
{
	guint16 *value = (guint16 *)user_data;
	*value = fu_synaptics_vmm9_crc16(buf, bufsz, *value);
	return TRUE;
}

static gboolean
fu_synaptics_vmm9_device_verify_crc16(FuSynapticsVmm9Device *self,
				      guint16 crc16_image,
				      gsize streamsz,
				      GError **error)
{
	guint8 buf[4] = {0};
	guint16 crc16_device;

	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_CAL_EEPROM_CHECK_CRC16,
					      0x0,
					      NULL,
					      streamsz,
					      buf,
					      sizeof(buf),
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
					      error)) {
		g_prefix_error(error, "failed to calculate device CRC16: ");
		return FALSE;
	}
	crc16_device = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	if (crc16_image != crc16_device) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "device CRC 0x%x mismatched image CRC 0x%x",
			    crc16_device,
			    crc16_image);
		return FALSE;
	}

	/* success! */
	return TRUE;
}

static FuFirmware *
fu_synaptics_vmm9_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	gsize bufsz = fu_device_get_firmware_size_max(FU_DEVICE(self));
	g_autofree guint8 *buf = g_malloc0(bufsz);
	g_autoptr(GPtrArray) chunks = NULL;
	// g_autoptr(FuFirmware) firmware = fu_synaptics_vmm9_firmware_new();
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(GBytes) fw = NULL;

	chunks = fu_chunk_array_mutable_new(buf, bufsz, 0, 0x0, FU_STRUCT_HID_PAYLOAD_SIZE_FIFO);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_synaptics_vmm9_device_command(self,
						      FU_SYNAPTICS_VMM9_RC_CTRL_READ_FROM_EEPROM,
						      fu_chunk_get_address(chk),
						      NULL,
						      fu_chunk_get_data_sz(chk),
						      fu_chunk_get_data_out(chk),
						      fu_chunk_get_data_sz(chk),
						      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
						      error)) {
			g_prefix_error(error,
				       "failed at chunk %u, @0x%x",
				       fu_chunk_get_idx(chk),
				       (guint)fu_chunk_get_address(chk));
			return NULL;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* parse */
	fw = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
	if (!fu_firmware_parse(firmware, fw, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaptics_vmm9_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuSynapticsVmm9Device *self = FU_SYNAPTICS_VMM9_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	guint16 crc16_image = 0x0;
	gsize streamsz = 0;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 35, NULL);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	/* calculate the checksum of the stream */
	if (!fu_input_stream_chunkify(stream,
				      fu_synaptics_vmm9_device_crc16_cb,
				      &crc16_image,
				      error)) {
		g_prefix_error(error, "failed to compute image CRC16: ");
		return FALSE;
	}

	/* erase the storage bank */
	if (!fu_synaptics_vmm9_device_erase(self, fu_progress_get_child(progress), error)) {
		g_prefix_error(error, "failed to erase: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* ensure the SPI flash is ready to access the write command */
	fu_device_sleep_full(device, 3000, fu_progress_get_child(progress));
	fu_progress_step_done(progress);

	/* write each block */
	chunks =
	    fu_chunk_array_new_from_stream(stream, 0x0, FU_STRUCT_HID_PAYLOAD_SIZE_FIFO, error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_synaptics_vmm9_device_write_blocks(self,
						   chunks,
						   fu_progress_get_child(progress),
						   error)) {
		g_prefix_error(error, "failed to write: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* verify each block */
	if (!fu_synaptics_vmm9_device_verify_crc16(self, crc16_image, streamsz, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* activate the firmware */
	if (!fu_synaptics_vmm9_device_command(self,
					      FU_SYNAPTICS_VMM9_RC_CTRL_ACTIVATE_FIRMWARE,
					      0x0,
					      NULL,
					      0,
					      NULL,
					      0,
					      FU_SYNAPTICS_VMM9_COMMAND_FLAG_NONE,
					      error)) {
		g_prefix_error(error, "failed to activate: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_synaptics_vmm9_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_synaptics_vmm9_device_init(FuSynapticsVmm9Device *self)
{
	// fu_device_set_firmware_size(FU_DEVICE(self), 64 * 1024 * 5);
	fu_device_set_firmware_size(FU_DEVICE(self), 0x80000);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.mst");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_SYNAPTICS_VMM9_DEVICE_FLAG_MANUAL_RESTART_REQUIRED,
					"manual-restart-required");
}

static void
fu_synaptics_vmm9_device_class_init(FuSynapticsVmm9DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_synaptics_vmm9_device_to_string;
	device_class->setup = fu_synaptics_vmm9_device_setup;
	device_class->open = fu_synaptics_vmm9_device_open;
	device_class->close = fu_synaptics_vmm9_device_close;
	device_class->attach = fu_synaptics_vmm9_device_attach;
	device_class->prepare_firmware = fu_synaptics_vmm9_device_prepare_firmware;
	device_class->write_firmware = fu_synaptics_vmm9_device_write_firmware;
	device_class->read_firmware = fu_synaptics_vmm9_device_read_firmware;
	device_class->set_progress = fu_synaptics_vmm9_device_set_progress;
}
