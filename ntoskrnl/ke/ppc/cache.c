/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC cache maintenance
 *
 * The 604 data cache snoops DMA; the instruction cache is not coherent with
 * stores, so new code must be pushed out of the data cache (dcbst) and the
 * instruction cache invalidated (icbi) line by line.
 */

#include <ntoskrnl.h>

ULONG KiPpcDcacheLineSize = 32;
ULONG KiPpcIcacheLineSize = 32;
ULONG KiDmaIoCoherency;

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    /* The architecture has no whole-cache operation. */
    return FALSE;
}

VOID
NTAPI
KeSetDmaIoCoherency(_In_ ULONG Coherency)
{
    KiDmaIoCoherency = Coherency;
}

VOID
NTAPI
KiPpcSweepDcache(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    ULONG_PTR Line = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(KiPpcDcacheLineSize - 1);
    ULONG_PTR End = (ULONG_PTR)BaseAddress + FlushSize;

    for (; Line < End; Line += KiPpcDcacheLineSize)
        __asm__ __volatile__("dcbst 0, %0" :: "r"(Line) : "memory");
    __asm__ __volatile__("sync" ::: "memory");
}

VOID
NTAPI
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    ULONG_PTR Line, End;
    ULONG Hid0;

    if (!BaseAddress || !FlushSize)
    {
        /* Flash-invalidate the whole instruction cache (HID0[ICFI]). Callers
         * without a range have already pushed their stores out. */
        __asm__ __volatile__("sync\n\tmfspr %0, 1008" : "=r"(Hid0) :: "memory");
        __asm__ __volatile__("mtspr 1008, %0\n\tisync\n\tmtspr 1008, %1\n\tisync" :: "r"(Hid0 | 0x800), "r"(Hid0) : "memory");
        return;
    }

    KiPpcSweepDcache(BaseAddress, FlushSize);
    Line = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(KiPpcIcacheLineSize - 1);
    End = (ULONG_PTR)BaseAddress + FlushSize;
    for (; Line < End; Line += KiPpcIcacheLineSize)
        __asm__ __volatile__("icbi 0, %0" :: "r"(Line) : "memory");
    __asm__ __volatile__("sync\n\tisync" ::: "memory");
}

VOID
NTAPI
KeFlushIoBuffers(
    _In_ PMDL Mdl,
    _In_ BOOLEAN ReadOperation,
    _In_ BOOLEAN DmaOperation)
{
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(Mdl != NULL);

    /* Snooping keeps the data cache coherent with DMA; order the accesses.
     * A read may have delivered code through another alias. */
    __asm__ __volatile__("sync" ::: "memory");
    if (ReadOperation && Mdl->ByteCount && !DmaOperation)
        KeSweepICache(NULL, 0);
}

VOID
FASTCALL
KeInvalidateRangeAllCaches(
    _In_ PVOID BaseAddress,
    _In_ ULONG Length)
{
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    if (Length)
        KeSweepICache(BaseAddress, Length);
}
