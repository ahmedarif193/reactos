/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VideoCore firmware property mailbox — polled property-tag
 *              exchanges for clock control (V3D) and firmware queries.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5vc4_mbox.h"

#define NDEBUG
#include <reactos/debug.h>

#define RPI5_MBOX_BUFFER_BYTES  PAGE_SIZE
#define RPI5_MBOX_TIMEOUT_TRIES 20000   /* x 50 us = 1 s ceiling */

static ULONG
Rpi5MboxRead(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG(
        (PULONG)((PUCHAR)DeviceExtension->MboxBase + Offset));
}

static VOID
Rpi5MboxWrite(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG(
        (PULONG)((PUCHAR)DeviceExtension->MboxBase + Offset), Value);
}

/*
 * One polled property exchange.  The buffer was staged by the caller in
 * MboxBufferVa; PASSIVE_LEVEL only (stalls up to 1 s on firmware silence).
 */
static BOOLEAN
Rpi5MboxTransact(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Message;
    ULONG Tries;

    if (DeviceExtension->MboxBase == NULL ||
        DeviceExtension->MboxBufferVa == NULL)
    {
        return FALSE;
    }

    /* The firmware takes a 32-bit address; the buffer is allocated <4 GB. */
    Message = ((ULONG)DeviceExtension->MboxBufferPhys.QuadPart & ~0xFu) |
              RPI5_MBOX_CHANNEL_PROPERTY;

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    for (Tries = 0; Tries < RPI5_MBOX_TIMEOUT_TRIES; Tries++)
    {
        if (!(Rpi5MboxRead(DeviceExtension, RPI5_MBOX1_STATUS) &
              RPI5_MBOX_STATUS_FULL))
        {
            break;
        }
        KeStallExecutionProcessor(50);
    }
    if (Tries == RPI5_MBOX_TIMEOUT_TRIES)
        return FALSE;

    Rpi5MboxWrite(DeviceExtension, RPI5_MBOX1_WRITE, Message);

    for (Tries = 0; Tries < RPI5_MBOX_TIMEOUT_TRIES; Tries++)
    {
        if (!(Rpi5MboxRead(DeviceExtension, RPI5_MBOX0_STATUS) &
              RPI5_MBOX_STATUS_EMPTY))
        {
            if (Rpi5MboxRead(DeviceExtension, RPI5_MBOX0_READ) == Message)
                break;
            /* Foreign channel traffic: keep draining. */
            continue;
        }
        KeStallExecutionProcessor(50);
    }
    if (Tries == RPI5_MBOX_TIMEOUT_TRIES)
        return FALSE;

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    return (((volatile ULONG *)DeviceExtension->MboxBufferVa)[1] ==
            RPI5_MBOX_RESPONSE_SUCCESS);
}

/* Single-tag helper: stages [size,code,tag,valuelen,0,values...,END]. */
static BOOLEAN
Rpi5MboxSingleTag(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Tag,
    _Inout_updates_(ValueDwords) PULONG Values,
    _In_ ULONG ValueDwords)
{
    volatile ULONG *Buffer = DeviceExtension->MboxBufferVa;
    ULONG i;

    if (Buffer == NULL || ValueDwords > 8)
        return FALSE;

    Buffer[0] = (6 + ValueDwords) * sizeof(ULONG);
    Buffer[1] = RPI5_MBOX_REQUEST;
    Buffer[2] = Tag;
    Buffer[3] = ValueDwords * sizeof(ULONG);
    Buffer[4] = 0;
    for (i = 0; i < ValueDwords; i++)
        Buffer[5 + i] = Values[i];
    Buffer[5 + ValueDwords] = RPI5_MBOX_TAG_END;

    if (!Rpi5MboxTransact(DeviceExtension))
        return FALSE;

    /* Bit 31 of the tag's value-length word flags a response. */
    if (!(Buffer[4] & 0x80000000u))
        return FALSE;

    for (i = 0; i < ValueDwords; i++)
        Values[i] = Buffer[5 + i];

    return TRUE;
}

/* EDID block over firmware DDC.  Values layout: in [block]; out
 * [block, status, 128 bytes].  Success requires status==0 and a
 * plugged monitor — doubles as the HPD probe. */
BOOLEAN
Rpi5MboxGetEdidBlock(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG BlockIndex,
    _Out_writes_bytes_(128) PUCHAR Buffer)
{
    volatile ULONG *Buf = DeviceExtension->MboxBufferVa;
    ULONG i;

    if (Buf == NULL)
        return FALSE;

    Buf[0] = (6 + 34) * sizeof(ULONG);
    Buf[1] = RPI5_MBOX_REQUEST;
    Buf[2] = RPI5_MBOX_TAG_GET_EDID_BLOCK;
    Buf[3] = 34 * sizeof(ULONG);
    Buf[4] = 0;
    Buf[5] = BlockIndex;
    for (i = 6; i < 5 + 34; i++)
        Buf[i] = 0;
    Buf[5 + 34] = RPI5_MBOX_TAG_END;

    if (!Rpi5MboxTransact(DeviceExtension))
        return FALSE;
    if (!(Buf[4] & 0x80000000u))
        return FALSE;
    if (Buf[6] != 0)  /* status */
        return FALSE;

    RtlCopyMemory(Buffer, (const void *)&Buf[7], 128);
    return TRUE;
}

/* Firmware framebuffer modeset: one atomic property message
 * (set physical/virtual size + depth, allocate, get pitch).  On
 * success the firmware lights the attached display scanning the
 * returned buffer — the cold-start path for headless boots. */
BOOLEAN
Rpi5MboxFbSetMode(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _Out_ PULONGLONG FbPhysical,
    _Out_ PULONG FbSize,
    _Out_ PULONG Pitch)
{
    volatile ULONG *Buf = DeviceExtension->MboxBufferVa;
    ULONG i = 0;
    ULONG AllocValIdx, PitchValIdx;

    if (Buf == NULL)
        return FALSE;

    Buf[i++] = 0;                       /* total size, patched below */
    Buf[i++] = RPI5_MBOX_REQUEST;

    Buf[i++] = RPI5_MBOX_TAG_FB_SET_PHYS_WH;
    Buf[i++] = 8; Buf[i++] = 0;
    Buf[i++] = Width; Buf[i++] = Height;

    Buf[i++] = RPI5_MBOX_TAG_FB_SET_VIRT_WH;
    Buf[i++] = 8; Buf[i++] = 0;
    Buf[i++] = Width; Buf[i++] = Height;

    Buf[i++] = RPI5_MBOX_TAG_FB_SET_DEPTH;
    Buf[i++] = 4; Buf[i++] = 0;
    Buf[i++] = 32;

    Buf[i++] = RPI5_MBOX_TAG_FB_ALLOCATE;
    Buf[i++] = 8; Buf[i++] = 0;
    AllocValIdx = i;
    Buf[i++] = 4096; Buf[i++] = 0;      /* alignment in, base/size out */

    Buf[i++] = RPI5_MBOX_TAG_FB_GET_PITCH;
    Buf[i++] = 4; Buf[i++] = 0;
    PitchValIdx = i;
    Buf[i++] = 0;

    Buf[i++] = RPI5_MBOX_TAG_END;
    Buf[0] = i * sizeof(ULONG);

    if (!Rpi5MboxTransact(DeviceExtension))
        return FALSE;

    if (Buf[AllocValIdx] == 0 || Buf[PitchValIdx] == 0)
        return FALSE;

    /* Firmware returns a VC bus address; mask to the ARM physical view. */
    *FbPhysical = Buf[AllocValIdx] & 0x3FFFFFFFu;
    *FbSize = Buf[AllocValIdx + 1];
    *Pitch = Buf[PitchValIdx];
    return TRUE;
}

BOOLEAN
Rpi5MboxInitialize(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PHYSICAL_ADDRESS Phys, Low, High, Boundary;

    Phys.QuadPart = RPI5_MBOX_PHYS;
    DeviceExtension->MboxBase = MmMapIoSpace(Phys, RPI5_MBOX_LENGTH,
                                             MmNonCached);
    if (DeviceExtension->MboxBase == NULL)
        return FALSE;

    /* Property buffer: 16-byte aligned, below 4 GB for the firmware. */
    Low.QuadPart = 0;
    High.QuadPart = 0xFFFFFFFFULL;
    Boundary.QuadPart = 0;
    DeviceExtension->MboxBufferVa = MmAllocateContiguousMemorySpecifyCache(
        RPI5_MBOX_BUFFER_BYTES, Low, High, Boundary, MmNonCached);
    if (DeviceExtension->MboxBufferVa == NULL)
    {
        MmUnmapIoSpace(DeviceExtension->MboxBase, RPI5_MBOX_LENGTH);
        DeviceExtension->MboxBase = NULL;
        return FALSE;
    }

    DeviceExtension->MboxBufferPhys =
        MmGetPhysicalAddress(DeviceExtension->MboxBufferVa);
    RtlZeroMemory(DeviceExtension->MboxBufferVa, RPI5_MBOX_BUFFER_BYTES);

    return TRUE;
}

VOID
Rpi5MboxTeardown(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->MboxBufferVa != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(DeviceExtension->MboxBufferVa,
                                           RPI5_MBOX_BUFFER_BYTES,
                                           MmNonCached);
        DeviceExtension->MboxBufferVa = NULL;
    }

    if (DeviceExtension->MboxBase != NULL)
    {
        MmUnmapIoSpace(DeviceExtension->MboxBase, RPI5_MBOX_LENGTH);
        DeviceExtension->MboxBase = NULL;
    }
}

BOOLEAN
Rpi5MboxGetFirmwareRevision(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG Revision)
{
    ULONG Values[1] = { 0 };

    *Revision = 0;
    if (!Rpi5MboxSingleTag(DeviceExtension, RPI5_MBOX_TAG_GET_FW_REVISION,
                           Values, 1))
    {
        return FALSE;
    }

    *Revision = Values[0];
    return TRUE;
}

BOOLEAN
Rpi5MboxSetClockState(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ClockId,
    _In_ BOOLEAN Enable)
{
    ULONG Values[2];

    Values[0] = ClockId;
    Values[1] = Enable ? 1u : 0u;

    if (!Rpi5MboxSingleTag(DeviceExtension, RPI5_MBOX_TAG_SET_CLOCK_STATE,
                           Values, 2))
    {
        return FALSE;
    }

    /* Response state bit0 = on, bit1 = clock doesn't exist. */
    return (Values[1] & 0x3u) == 1u;
}

BOOLEAN
Rpi5MboxSetDomainState(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG DomainId,
    _In_ BOOLEAN On)
{
    ULONG Values[2];

    Values[0] = DomainId;
    Values[1] = On ? 1u : 0u;

    if (!Rpi5MboxSingleTag(DeviceExtension, RPI5_MBOX_TAG_SET_DOMAIN_STATE,
                           Values, 2))
    {
        return FALSE;
    }

    return (Values[1] & 0x1u) == (On ? 1u : 0u);
}

BOOLEAN
Rpi5MboxSetClockRate(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ClockId,
    _In_ ULONG RateHz)
{
    ULONG Values[3];

    Values[0] = ClockId;
    Values[1] = RateHz;
    Values[2] = 1; /* skip setting turbo */

    if (!Rpi5MboxSingleTag(DeviceExtension, RPI5_MBOX_TAG_SET_CLOCK_RATE,
                           Values, 3))
    {
        return FALSE;
    }

    return Values[1] != 0;
}

BOOLEAN
Rpi5MboxGetClockRate(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ClockId,
    _Out_ PULONG RateHz)
{
    ULONG Values[2];

    Values[0] = ClockId;
    Values[1] = 0;
    *RateHz = 0;

    if (!Rpi5MboxSingleTag(DeviceExtension, RPI5_MBOX_TAG_GET_CLOCK_RATE,
                           Values, 2))
    {
        return FALSE;
    }

    *RateHz = Values[1];
    return TRUE;
}
