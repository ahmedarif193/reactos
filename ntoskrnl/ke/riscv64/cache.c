/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
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

    /* The first-entry port admits one kernel hart. Do not silently satisfy
     * a system-wide request with a local fence after enabling another hart. */
    if (KeNumberProcessors > 1)
        KeBugCheckEx(MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED, KeNumberProcessors, 0, 0, 0);

    /* The baseline instruction has no range form. */
    __asm__ __volatile__("fence.i" ::: "memory");
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
    if (KeNumberProcessors > 1)
        KeBugCheckEx(MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED, KeNumberProcessors, 0, 0, 0);
    if (Mdl->ByteCount == 0)
        return;
    if (!MiRiscvIsCachedMdl(Mdl))
        KiRiscvUnimplemented("KeFlushIoBuffers/unsupported-MDL");

    /* PIO and CPU copies into ordinary RAM need ordering, not DMA cache
     * maintenance. A read may have supplied executable bytes through a
     * different virtual alias; synchronize this hart's instruction stream. */
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    if (ReadOperation)
        KeSweepICache(NULL, 0);
}
