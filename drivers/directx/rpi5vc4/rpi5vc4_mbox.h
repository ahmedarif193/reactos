/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VideoCore firmware property mailbox (bcm2835-mbox) — clock
 *              control and firmware identification for the V3D bring-up.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Mailbox base from the in-repo RPi5 DTB: /soc@107c000000/mailbox@7c013880
 * (compatible "brcm,bcm2835-mbox").  The property-tag protocol is the
 * documented VideoCore firmware interface (channel 8).
 */

#ifndef _RPI5VC4_MBOX_H_
#define _RPI5VC4_MBOX_H_

#include "rpi5vc4.h"

#define RPI5_MBOX_PHYS                  0x107C013880ULL
#define RPI5_MBOX_LENGTH                0x40

/* bcm2835 mailbox registers (byte offsets). */
#define RPI5_MBOX0_READ                 0x00    /* VC -> ARM */
#define RPI5_MBOX0_STATUS               0x18
#define RPI5_MBOX1_WRITE                0x20    /* ARM -> VC */
#define RPI5_MBOX1_STATUS               0x38
#define RPI5_MBOX_STATUS_FULL           (1u << 31)
#define RPI5_MBOX_STATUS_EMPTY          (1u << 30)

#define RPI5_MBOX_CHANNEL_PROPERTY      8u

/* Property tag protocol. */
#define RPI5_MBOX_REQUEST               0x00000000u
#define RPI5_MBOX_RESPONSE_SUCCESS      0x80000000u
#define RPI5_MBOX_TAG_END               0x00000000u
#define RPI5_MBOX_TAG_GET_FW_REVISION   0x00000001u
#define RPI5_MBOX_TAG_GET_CLOCK_STATE   0x00030001u
#define RPI5_MBOX_TAG_GET_CLOCK_RATE    0x00030002u
#define RPI5_MBOX_TAG_SET_CLOCK_STATE   0x00038001u
#define RPI5_MBOX_TAG_SET_CLOCK_RATE    0x00038002u
#define RPI5_MBOX_TAG_SET_DOMAIN_STATE  0x00038030u
#define RPI5_MBOX_TAG_GET_EDID_BLOCK    0x00030020u
#define RPI5_MBOX_TAG_FB_ALLOCATE       0x00040001u
#define RPI5_MBOX_TAG_FB_SET_PHYS_WH    0x00048003u
#define RPI5_MBOX_TAG_FB_SET_VIRT_WH    0x00048004u
#define RPI5_MBOX_TAG_FB_SET_DEPTH      0x00048005u
#define RPI5_MBOX_TAG_FB_GET_PITCH      0x00040008u

#define RPI5_MBOX_CLOCK_V3D             5u
#define RPI5_MBOX_DOMAIN_V3D            10u

BOOLEAN
Rpi5MboxInitialize(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

VOID
Rpi5MboxTeardown(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

BOOLEAN
Rpi5MboxGetFirmwareRevision(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG Revision);

BOOLEAN
Rpi5MboxSetClockState(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ClockId,
    _In_ BOOLEAN Enable);

BOOLEAN
Rpi5MboxGetEdidBlock(
    _In_ struct _RPI5VC4_DEVICE_EXTENSION *DeviceExtension,
    _In_ ULONG BlockIndex,
    _Out_writes_bytes_(128) PUCHAR Buffer);

BOOLEAN
Rpi5MboxFbSetMode(
    _In_ struct _RPI5VC4_DEVICE_EXTENSION *DeviceExtension,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _Out_ PULONGLONG FbPhysical,
    _Out_ PULONG FbSize,
    _Out_ PULONG Pitch);

BOOLEAN
Rpi5MboxSetDomainState(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG DomainId,
    _In_ BOOLEAN On);

BOOLEAN
Rpi5MboxSetClockRate(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ClockId,
    _In_ ULONG RateHz);

BOOLEAN
Rpi5MboxGetClockRate(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ClockId,
    _Out_ PULONG RateHz);

#endif /* _RPI5VC4_MBOX_H_ */
