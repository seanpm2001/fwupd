// Copyright 2024 Richard hughes <Richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream, ParseStream)]
struct FuStructSynapticsVmm9 {
    signature: [char; 7] == "CARRERA",
}

#[repr(u8)]
enum FuSynapticsVmm9RcCtrl {
    EnableRc = 0x01,
    DisableRc = 0x02,
    GetId = 0x03,
    GetVersion = 0x04,
    FlashMapping = 0x07,
    EnableFlashChipErase = 0x08,
    CalEepromChecksum = 0x11,
    FlashErase = 0x14,
    CalEepromCheckCrc8 = 0x16,
    CalEepromCheckCrc16 = 0x17,
    ActivateFirmware = 0x18,
    WriteToEeprom = 0x20,
    WriteToMemory = 0x21,
    WriteToTxDpcd = 0x22, // TX0
    WriteToTxDpcdTx1 = 0x23,
    WriteToTxDpcdTx2 = 0x24,
    WriteToTxDpcdTx3 = 0x25,
    ReadFromEeprom = 0x30,
    ReadFromMemory = 0x31,
    ReadFromTxDpcd = 0x32, // TX0
    ReadFromTxDpcdTx1 = 0x33,
    ReadFromTxDpcdTx2 = 0x34,
    ReadFromTxDpcdTx3 = 0x35,
    GetChipCoreTemperature = 0x64,
}

#[derive(ToString)]
#[repr(u8)]
enum FuSynapticsVmm9RcSts {
    Success,
    Invalid,
    Unsupported,
    Failed,
    Disabled,
    ConfigureSignFailed,
    FirmwareSignFailed,
    RollbackFailed,
}

#[derive(New, Getters)]
struct FuStructHidPayload {
    cap: u8,
    state: u8,
    ctrl: FuSynapticsVmm9RcCtrl,
    sts: FuSynapticsVmm9RcSts,
    offset: u32le,
    length: u32le,
    fifo: [u8; 32],
}

#[derive(New, ToString, Getters)]
struct FuStructHidSetCommand {
    id: u8 == 0x1,
    type: u8 == 0x0, // packet write
    size: u8,
    payload: FuStructHidPayload,
    _checksum: u8, // this is actually lower if @rc_fifo is less than 32 bytes
}

#[derive(New, Parse)]
struct FuStructHidGetCommand {
    id: u8 == 0x1,
    type: u8 == 0x0, // packet reply
    size: u8,
    payload: FuStructHidPayload,
    _checksum: u8, // payload is always 32 bytes
}
