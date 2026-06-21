/*
 * PROJECT:     ReactOS HAL — ARM64 BCM2711 VideoCore property mailbox
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Broadcom BCM2711 (Raspberry Pi 4 / CM4) VideoCore firmware
 *              property-channel mailbox.  Provides the generic property
 *              transaction and the SET_POWER_STATE wrapper used to assert
 *              the firmware-owned USB power domain so the shared USB2 PHY
 *              leaves IDDQ before usbxhci starts the built-in xHCI.
 *
 *              Reimplemented from the public RPi mailbox specification and
 *              the pftf RpiFirmwareDxe register semantics; no GPL source
 *              copied.
 *
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <hal.h>
#include "halext.h"
#include "bcm2711_pci.h"
#include "bcm2711_vc.h"

#define NDEBUG
#include <debug.h>

typedef struct _BCM2711_VC_MBOX
{
    PVOID    VirtBase;
    PVOID    BufferVa;
    ULONGLONG BufferPhys;
    BOOLEAN  Mapped;
    BOOLEAN  Initialized;
} BCM2711_VC_MBOX;

static BCM2711_VC_MBOX Bcm2711Vc;
static KSPIN_LOCK Bcm2711VcLock;

#define BCM2711_VC_BUFFER_BYTES         256
#define BCM2711_VC_POLL_STALL_US        10
#define BCM2711_VC_POLL_MAX             100000

FORCEINLINE
ULONG
Bcm2711VcRead32(
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Bcm2711Vc.VirtBase + Offset));
}

FORCEINLINE
VOID
Bcm2711VcWrite32(
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Bcm2711Vc.VirtBase + Offset), Value);
}

static
BOOLEAN
Bcm2711VcWaitStatusClear(
    _In_ ULONG Mask)
{
    ULONG Tries;

    for (Tries = 0; Tries < BCM2711_VC_POLL_MAX; Tries++)
    {
        if ((Bcm2711VcRead32(BCM2711_VC_MBOX_STATUS) & Mask) == 0)
            return TRUE;

        KeStallExecutionProcessor(BCM2711_VC_POLL_STALL_US);
    }

    return FALSE;
}

static
BOOLEAN
Bcm2711VcDrain(VOID)
{
    ULONG Tries;

    for (Tries = 0; Tries < BCM2711_VC_POLL_MAX; Tries++)
    {
        if (Bcm2711VcRead32(BCM2711_VC_MBOX_STATUS) & BCM2711_VC_MBOX_STATUS_EMPTY)
            return TRUE;

        (VOID)Bcm2711VcRead32(BCM2711_VC_MBOX_READ);
        KeStallExecutionProcessor(BCM2711_VC_POLL_STALL_US);
    }

    return FALSE;
}

static
NTSTATUS
Bcm2711VcEnsureInit(VOID)
{
    PHYSICAL_ADDRESS Phys;
    PHYSICAL_ADDRESS Low;
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Boundary;
    PVOID VirtAddr;
    PVOID BufferVa;
    PHYSICAL_ADDRESS BufferPhys;

    if (Bcm2711Vc.Initialized)
        return Bcm2711Vc.Mapped ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

    KeInitializeSpinLock(&Bcm2711VcLock);

    Phys.QuadPart = BCM2711_VC_MBOX_BASE;
    VirtAddr = HalpMapPhysicalMemory64(Phys,
                   (PFN_COUNT)((BCM2711_VC_MBOX_LENGTH + PAGE_SIZE - 1) >> PAGE_SHIFT));
    if (!VirtAddr)
    {
        DPRINT1("[BCM2711][VC] Failed to map mailbox at %I64x\n", Phys.QuadPart);
        Bcm2711Vc.Initialized = TRUE;
        return STATUS_UNSUCCESSFUL;
    }

    Low.QuadPart = 0;
    High.QuadPart = 0x3FFFFFFF;
    Boundary.QuadPart = 0;

    BufferVa = MmAllocateContiguousMemorySpecifyCache(BCM2711_VC_BUFFER_BYTES,
                                                      Low,
                                                      High,
                                                      Boundary,
                                                      MmNonCached);
    if (!BufferVa)
    {
        DPRINT1("[BCM2711][VC] Failed to allocate uncached mailbox buffer\n");
        Bcm2711Vc.VirtBase = VirtAddr;
        Bcm2711Vc.Initialized = TRUE;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    BufferPhys = MmGetPhysicalAddress(BufferVa);
    if (BufferPhys.QuadPart == 0 || BufferPhys.QuadPart > 0x3FFFFFFF ||
        (BufferPhys.QuadPart & 0xF) != 0)
    {
        DPRINT1("[BCM2711][VC] Bad mailbox buffer phys %I64x\n", BufferPhys.QuadPart);
        MmFreeContiguousMemorySpecifyCache(BufferVa, BCM2711_VC_BUFFER_BYTES, MmNonCached);
        Bcm2711Vc.VirtBase = VirtAddr;
        Bcm2711Vc.Initialized = TRUE;
        return STATUS_UNSUCCESSFUL;
    }

    Bcm2711Vc.VirtBase = VirtAddr;
    Bcm2711Vc.BufferVa = BufferVa;
    Bcm2711Vc.BufferPhys = (ULONGLONG)BufferPhys.QuadPart;
    Bcm2711Vc.Mapped = TRUE;
    Bcm2711Vc.Initialized = TRUE;

    DPRINT1("[BCM2711][VC] Mailbox ready (mmio %I64x, buf phys %I64x)\n",
            Phys.QuadPart, Bcm2711Vc.BufferPhys);

    return STATUS_SUCCESS;
}

BOOLEAN
HalpBcm2711VcMailboxProperty(
    _Inout_updates_bytes_(SizeBytes) PULONG Buffer,
    _In_ ULONG SizeBytes)
{
    KIRQL OldIrql;
    ULONG BusAddr;
    ULONG Value;
    BOOLEAN Ok = FALSE;

    if (!Bcm2711Detected)
        return FALSE;

    if (!Buffer || SizeBytes == 0 || SizeBytes > BCM2711_VC_BUFFER_BYTES ||
        (SizeBytes & 0xF) != 0)
    {
        return FALSE;
    }

    if (!NT_SUCCESS(Bcm2711VcEnsureInit()))
        return FALSE;

    KeAcquireSpinLock(&Bcm2711VcLock, &OldIrql);

    RtlCopyMemory(Bcm2711Vc.BufferVa, Buffer, SizeBytes);

    if (!Bcm2711VcDrain())
    {
        DPRINT1("[BCM2711][VC] Drain timeout\n");
        goto Done;
    }

    if (!Bcm2711VcWaitStatusClear(BCM2711_VC_MBOX_STATUS_FULL))
    {
        DPRINT1("[BCM2711][VC] Outbox full timeout\n");
        goto Done;
    }

    BusAddr = (ULONG)(((Bcm2711Vc.BufferPhys | BCM2711_VC_BUS_ALIAS) & ~0xFULL) |
                      BCM2711_VC_MBOX_CHANNEL_PROP);
    Bcm2711VcWrite32(BCM2711_VC_MBOX_WRITE, BusAddr);

    for (;;)
    {
        if (!Bcm2711VcWaitStatusClear(BCM2711_VC_MBOX_STATUS_EMPTY))
        {
            DPRINT1("[BCM2711][VC] Inbox empty timeout\n");
            goto Done;
        }

        Value = Bcm2711VcRead32(BCM2711_VC_MBOX_READ);
        if ((Value & BCM2711_VC_MBOX_CHANNEL_MASK) == BCM2711_VC_MBOX_CHANNEL_PROP)
            break;
    }

    RtlCopyMemory(Buffer, Bcm2711Vc.BufferVa, SizeBytes);

    Ok = (Buffer[1] == BCM2711_VC_RESPONSE_SUCCESS);
    if (!Ok)
        DPRINT1("[BCM2711][VC] Property response 0x%08lx\n", Buffer[1]);

Done:
    KeReleaseSpinLock(&Bcm2711VcLock, OldIrql);
    return Ok;
}

BOOLEAN
HalpBcm2711VcSetPowerState(
    _In_ ULONG DeviceId,
    _In_ BOOLEAN On)
{
    ULONG Message[8];
    ULONG State;

    State = On ? (BCM2711_VC_POWER_STATE_ON | BCM2711_VC_POWER_STATE_WAIT) : 0;

    Message[0] = sizeof(Message);
    Message[1] = BCM2711_VC_REQUEST_CODE;
    Message[2] = BCM2711_VC_TAG_SET_POWER_STATE;
    Message[3] = 8;
    Message[4] = 8;
    Message[5] = DeviceId;
    Message[6] = State;
    Message[7] = BCM2711_VC_TAG_END;

    if (!HalpBcm2711VcMailboxProperty(Message, sizeof(Message)))
        return FALSE;

    if (On && (Message[6] & BCM2711_VC_POWER_STATE_ON) == 0)
    {
        DPRINT1("[BCM2711][VC] Power-on of device %lu not confirmed (0x%08lx)\n",
                DeviceId, Message[6]);
        return FALSE;
    }

    DPRINT1("[BCM2711][VC] Device %lu power %s (state 0x%08lx)\n",
            DeviceId, On ? "ON" : "OFF", Message[6]);

    return TRUE;
}

VOID
HalpBcm2711VcInit(VOID)
{
    if (!Bcm2711Detected)
        return;

    if (!NT_SUCCESS(Bcm2711VcEnsureInit()))
    {
        DPRINT1("[BCM2711][VC] Mailbox init failed; USB power domain not asserted\n");
        return;
    }

    if (!HalpBcm2711VcSetPowerState(RPI_MBOX_POWER_STATE_USB_HCD, TRUE))
        DPRINT1("[BCM2711][VC] Failed to assert USB power domain\n");
}
