/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V instruction-stream synchronization
 */

#include <ntoskrnl.h>

/* Platform initialization owns this declaration. Zero means that coherent
 * DMA has not been established for the active machine. */
ULONG KiDmaIoCoherency;

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    /* No baseline whole-cache maintenance operation is available. */
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
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(FlushSize);

    KIRQL OldIrql = KeGetCurrentIrql();
    KAFFINITY Targets;
    if (OldIrql < SYNCH_LEVEL) KfRaiseIrql(SYNCH_LEVEL);
    Targets = KeActiveProcessors & ~KeGetCurrentPrcb()->SetMember;
    __asm__ __volatile__("fence rw, rw\n\tfence.i" ::: "memory");
    if (Targets) HalpRiscvRemoteFence(Targets, NULL, 0, TRUE);
    KfLowerIrql(OldIrql);
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

    /* The HAL sets coherency only for a firmware-declared coherent DMA host.
     * In that case a fence orders CPU and device accesses; no cache clean or
     * invalidate is needed. Noncoherent DMA still needs a board provider. */
    if (DmaOperation)
    {
        if (!KiDmaIoCoherency)
            KiRiscvUnimplemented("KeFlushIoBuffers/noncoherent-DMA");
        __asm__ __volatile__("fence iorw, iorw" ::: "memory");
        return;
    }
    if (Mdl->ByteCount == 0)
        return;

    /* PIO and CPU copies need ordering, not cache maintenance: caches are
     * coherent with every alias of a page. A read may have supplied
     * executable bytes through a different virtual alias; synchronize this
     * hart's instruction stream. */
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    if (ReadOperation)
        KeSweepICache(NULL, 0);
}

VOID
FASTCALL
KeInvalidateRangeAllCaches(
    _In_ PVOID BaseAddress,
    _In_ ULONG Length)
{
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    if (!Length)
        return;

    /* The coherent platform does not require data-cache maintenance to make
     * RAM visible to CPUs or devices. Order those accesses and synchronize
     * instruction caches on every active hart. A noncoherent platform needs
     * cache-block operations supplied by its hardware provider. */
    if (!KiDmaIoCoherency)
        KiRiscvUnimplemented("KeInvalidateRangeAllCaches/noncoherent-platform");
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    KeSweepICache(BaseAddress, Length);
}
