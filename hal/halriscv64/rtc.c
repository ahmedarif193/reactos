/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */
/* Device-tree-discovered Goldfish RTC on QEMU virt. Register contract:
 * https://github.com/qemu/qemu/blob/v10.2.0/hw/rtc/goldfish_rtc.c */
#include <ntifs.h>
#include <reactos/riscv64/fdtlib.h>
#include "halp.h"

#define RTC_UNIX_EPOCH 116444736000000000ULL
static PHYSICAL_ADDRESS HalpRtcAddress;
static PULONG HalpRtcRegisters;
static KSPIN_LOCK HalpRtcLock;

BOOLEAN
HalpRiscvInitializeRtc(const VOID *DeviceTree, SIZE_T DeviceTreeSize)
{
    RISCV_FDT Fdt;
    ULONG Root, Soc, Parent, Node, Length, Cells;
    ULONG64 Address, Size;
    const VOID *Property;

    if (!RiscvFdtOpen(DeviceTree, DeviceTreeSize, &Fdt)) return FALSE;
    Root = RiscvFdtRootNode(&Fdt);
    Property = RiscvFdtGetProperty(&Fdt, Root, "compatible", &Length);
    if (!RiscvFdtStringListContains(Property, Length, "riscv-virtio")) return TRUE;
    if (!RiscvFdtFindNode(&Fdt, "/soc", &Soc, &Parent) || Parent != Root)
        return FALSE;
    if (!RiscvFdtReadU32(&Fdt, Soc, "#address-cells", &Cells) || Cells != 2 ||
        !RiscvFdtReadU32(&Fdt, Soc, "#size-cells", &Cells) || Cells != 2)
        return FALSE;
    Property = RiscvFdtGetProperty(&Fdt, Soc, "ranges", &Length);
    if (!Property || Length) return FALSE;
    for (Node = RiscvFdtFirstChild(&Fdt, Soc); Node != RISCV_FDT_NO_NODE;
         Node = RiscvFdtNextSibling(&Fdt, Node))
    {
        Property = RiscvFdtGetProperty(&Fdt, Node, "compatible", &Length);
        if (!RiscvFdtStringListContains(Property, Length, "google,goldfish-rtc")) continue;
        Property = RiscvFdtGetProperty(&Fdt, Node, "status", &Length);
        if (Property && !RiscvFdtStringListContains(Property, Length, "okay") &&
            !RiscvFdtStringListContains(Property, Length, "ok")) continue;
        Property = RiscvFdtGetProperty(&Fdt, Node, "reg", &Length);
        if (HalpRtcAddress.QuadPart || !Property || Length != 16 ||
            !RiscvFdtReadCells(Property, Length, 0, 2, &Address) ||
            !RiscvFdtReadCells(Property, Length, 2, 2, &Size) ||
            !Address || Address > MAXLONGLONG - PAGE_SIZE ||
            (Address & (PAGE_SIZE - 1)) || Size < 8 || Size > PAGE_SIZE)
            return FALSE;
        HalpRtcAddress.QuadPart = Address;
    }
    KeInitializeSpinLock(&HalpRtcLock);
    return TRUE;
}

BOOLEAN
HalpRiscvRtcIsDeviceMemory(PHYSICAL_ADDRESS Address, SIZE_T Length)
{
    return HalpRtcAddress.QuadPart && Length && Length <= PAGE_SIZE &&
           Address.QuadPart >= HalpRtcAddress.QuadPart &&
           (ULONG64)(Address.QuadPart - HalpRtcAddress.QuadPart) <= PAGE_SIZE - Length;
}

BOOLEAN
HalpRiscvMapRtc(VOID)
{
    if (!HalpRtcAddress.QuadPart) return TRUE;
    HalpRtcRegisters = MmMapIoSpace(HalpRtcAddress, PAGE_SIZE, MmNonCached);
    return HalpRtcRegisters != NULL;
}

BOOLEAN NTAPI
HalQueryRealTimeClock(PTIME_FIELDS RtcTime)
{
    ULONG Low, High;
    LARGE_INTEGER Time;
    KIRQL OldIrql;
    if (!RtcTime || !HalpRtcRegisters) return FALSE;
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&HalpRtcLock);
    /* Reading LOW latches HIGH; preserve that order and serialize readers. */
    Low = READ_REGISTER_ULONG(HalpRtcRegisters);
    High = READ_REGISTER_ULONG(HalpRtcRegisters + 1);
    KeReleaseSpinLockFromDpcLevel(&HalpRtcLock);
    KeLowerIrql(OldIrql);
    Time.QuadPart = (((ULONG64)High << 32) | Low) / 100 + RTC_UNIX_EPOCH;
    RtlTimeToTimeFields(&Time, RtcTime);
    return TRUE;
}

BOOLEAN NTAPI
HalSetRealTimeClock(PTIME_FIELDS RtcTime)
{
    LARGE_INTEGER Time;
    ULONG64 Ticks;
    KIRQL OldIrql;
    if (!RtcTime || !HalpRtcRegisters || !RtlTimeFieldsToTime(RtcTime, &Time) ||
        Time.QuadPart < RTC_UNIX_EPOCH) return FALSE;
    Ticks = Time.QuadPart - RTC_UNIX_EPOCH;
    if (Ticks > MAXULONGLONG / 100) return FALSE;
    Ticks *= 100;
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&HalpRtcLock);
    WRITE_REGISTER_ULONG(HalpRtcRegisters + 1, (ULONG)(Ticks >> 32));
    WRITE_REGISTER_ULONG(HalpRtcRegisters, (ULONG)Ticks);
    KeReleaseSpinLockFromDpcLevel(&HalpRtcLock);
    KeLowerIrql(OldIrql);
    return TRUE;
}
