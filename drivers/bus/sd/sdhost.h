/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2835 "SDHost" controller backend definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SDBUS_SDHOST_H_
#define _SDBUS_SDHOST_H_

#include "sdbus.h"

/*
 * BCM2835 SDHost register block (all registers are 32 bits wide).
 * This is the Broadcom-proprietary SD controller at 0x7e202000 (0x3f202000
 * on RPi3), not the Arasan SDHCI block; none of the SDHCI offsets apply.
 */
#define SDHOST_SDCMD                    0x00
#define SDHOST_SDARG                    0x04
#define SDHOST_SDTOUT                   0x08
#define SDHOST_SDCDIV                   0x0C
#define SDHOST_SDRSP0                   0x10
#define SDHOST_SDRSP1                   0x14
#define SDHOST_SDRSP2                   0x18
#define SDHOST_SDRSP3                   0x1C
#define SDHOST_SDHSTS                   0x20
#define SDHOST_SDVDD                    0x30
#define SDHOST_SDEDM                    0x34
#define SDHOST_SDHCFG                   0x38
#define SDHOST_SDHBCT                   0x3C
#define SDHOST_SDDATA                   0x40
#define SDHOST_SDHBLC                   0x50

/* SDCMD bits; NEW_FLAG starts the command and clears when the command phase is done */
#define SDHOST_CMD_NEW_FLAG             0x8000
#define SDHOST_CMD_FAIL_FLAG            0x4000
#define SDHOST_CMD_BUSYWAIT             0x0800
#define SDHOST_CMD_NO_RESPONSE          0x0400
#define SDHOST_CMD_LONG_RESPONSE        0x0200
#define SDHOST_CMD_WRITE_CMD            0x0080
#define SDHOST_CMD_READ_CMD             0x0040
#define SDHOST_CMD_INDEX_MASK           0x003F

/* SDHSTS bits (write-1-to-clear) */
#define SDHOST_HSTS_BUSY_IRPT           0x0400
#define SDHOST_HSTS_REW_TIME_OUT        0x0080
#define SDHOST_HSTS_CMD_TIME_OUT        0x0040
#define SDHOST_HSTS_CRC16_ERROR         0x0020
#define SDHOST_HSTS_CRC7_ERROR          0x0010
#define SDHOST_HSTS_FIFO_ERROR          0x0008
#define SDHOST_HSTS_CLEAR_MASK          0x07F8
#define SDHOST_HSTS_TRANSFER_ERROR_MASK \
    (SDHOST_HSTS_CRC7_ERROR | SDHOST_HSTS_CRC16_ERROR | SDHOST_HSTS_REW_TIME_OUT | SDHOST_HSTS_FIFO_ERROR)
#define SDHOST_HSTS_ERROR_MASK          (SDHOST_HSTS_CMD_TIME_OUT | SDHOST_HSTS_TRANSFER_ERROR_MASK)

/* SDHCFG bits */
#define SDHOST_CFG_BUSY_IRPT_EN         0x0400
#define SDHOST_CFG_SLOW_CARD            0x0008
#define SDHOST_CFG_WIDE_EXT_BUS         0x0004
#define SDHOST_CFG_WIDE_INT_BUS         0x0002

/* SDVDD */
#define SDHOST_VDD_POWER_ON             0x0001

/* SDEDM: FSM state, FIFO fill level, and the FIFO threshold fields */
#define SDHOST_EDM_FORCE_DATA_MODE      0x00080000
#define SDHOST_EDM_FSM_MASK             0x0000000F
#define SDHOST_EDM_FSM_IDENTMODE        0x0
#define SDHOST_EDM_FSM_DATAMODE         0x1
#define SDHOST_EDM_FSM_READDATA         0x2
#define SDHOST_EDM_FSM_READWAIT         0x4
#define SDHOST_EDM_FSM_WRITESTART1      0xA
#define SDHOST_EDM_FIFO_FILL_SHIFT      4
#define SDHOST_EDM_FIFO_FILL_MASK       0x1F
#define SDHOST_EDM_WRITE_THRESHOLD_SHIFT 9
#define SDHOST_EDM_READ_THRESHOLD_SHIFT 14
#define SDHOST_EDM_THRESHOLD_MASK       0x1F

#define SDHOST_FIFO_WORDS               16
#define SDHOST_FIFO_READ_THRESHOLD      4
#define SDHOST_FIFO_WRITE_THRESHOLD     4

/* The SDHost core clock; SDclk = 250 MHz / (SDCDIV + 2) */
#define SDHOST_BASE_CLOCK_KHZ           250000
#define SDHOST_MAX_CDIV                 0x7FF
#define SDHOST_INIT_TIMEOUT_CLOCKS      0x00F00000

NTSTATUS
SdHostInitializeController(_In_ PFDO_EXTENSION FdoExtension);

NTSTATUS
SdHostReset(_In_ PFDO_EXTENSION FdoExtension);

NTSTATUS
SdHostSetClock(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG TargetClockKhz);

VOID
SdHostSetBusWidth(_In_ PFDO_EXTENSION FdoExtension, _In_ UCHAR BusWidth);

NTSTATUS
SdHostSendCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_opt_ PULONG Response);

NTSTATUS
SdHostExecuteDataCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _In_ BOOLEAN IsRead,
    _In_ BOOLEAN SendStopCommand,
    _Inout_updates_bytes_(BlockSize * BlockCount) PVOID Buffer,
    _In_ ULONG BlockSize,
    _In_ ULONG BlockCount,
    _Out_opt_ PULONG Response);

NTSTATUS
SdHostExecuteRequest(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PSDCMD_DESCRIPTOR CmdDesc,
    _In_ ULONG Argument,
    _Inout_opt_ PMDL Mdl,
    _In_ ULONG DataLength,
    _In_ ULONG BlockSize,
    _In_ USHORT RequestFlags,
    _Out_opt_ PULONG Response);

#endif /* _SDBUS_SDHOST_H_ */
