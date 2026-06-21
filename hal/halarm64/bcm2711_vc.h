/*
 * PROJECT:     ReactOS HAL — ARM64 BCM2711 VideoCore property mailbox
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Header for the Broadcom BCM2711 (Raspberry Pi 4 / CM4)
 *              VideoCore firmware property-channel mailbox, used to drive
 *              firmware-owned power domains (e.g. the USB host PHY).
 *
 *              Reimplemented from the public RPi mailbox specification and
 *              the pftf RpiFirmwareDxe register semantics; no GPL source
 *              copied.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define BCM2711_VC_SOC_BASE             0x00000000FE000000ULL
#define BCM2711_VC_MBOX_OFFSET          0x0000B880ULL
#define BCM2711_VC_MBOX_BASE            (BCM2711_VC_SOC_BASE + BCM2711_VC_MBOX_OFFSET)
#define BCM2711_VC_MBOX_LENGTH          0x00000024ULL

#define BCM2711_VC_MBOX_READ            0x00
#define BCM2711_VC_MBOX_POLL            0x10
#define BCM2711_VC_MBOX_SENDER          0x14
#define BCM2711_VC_MBOX_STATUS          0x18
#define BCM2711_VC_MBOX_CONFIG          0x1C
#define BCM2711_VC_MBOX_WRITE           0x20

#define BCM2711_VC_MBOX_STATUS_FULL     0x80000000
#define BCM2711_VC_MBOX_STATUS_EMPTY    0x40000000

#define BCM2711_VC_MBOX_CHANNEL_PROP    8
#define BCM2711_VC_MBOX_CHANNEL_MASK    0xF

#define BCM2711_VC_BUS_ALIAS            0xC0000000ULL

#define BCM2711_VC_REQUEST_CODE         0x00000000
#define BCM2711_VC_RESPONSE_SUCCESS     0x80000000
#define BCM2711_VC_TAG_END              0x00000000

#define BCM2711_VC_TAG_SET_POWER_STATE  0x00028001

#define RPI_MBOX_POWER_STATE_USB_HCD    3

#define BCM2711_VC_POWER_STATE_ON       0x00000001
#define BCM2711_VC_POWER_STATE_WAIT     0x00000002

BOOLEAN
HalpBcm2711VcMailboxProperty(
    _Inout_updates_bytes_(SizeBytes) PULONG Buffer,
    _In_ ULONG SizeBytes);

BOOLEAN
HalpBcm2711VcSetPowerState(
    _In_ ULONG DeviceId,
    _In_ BOOLEAN On);

VOID
HalpBcm2711VcInit(VOID);
