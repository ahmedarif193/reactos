/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/hypermap.c
 * PURPOSE:         ARM Memory Manager Hyperspace Mapping Functionality
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

PMMPTE MmFirstReservedMappingPte, MmLastReservedMappingPte;
PMMPTE MiFirstReservedZeroingPte;
MMPTE HyperTemplatePte;

/* PRIVATE FUNCTIONS **********************************************************/

PVOID
NTAPI
MiMapPageInHyperSpace(IN PEPROCESS Process,
                      IN PFN_NUMBER Page,
                      IN PKIRQL OldIrql)
{
    MMPTE TempPte;
    PMMPTE PointerPte;
    PFN_NUMBER Offset;

    //
    // Never accept page 0 or non-physical pages
    //
    ASSERT(Page != 0);
    ASSERT(MiGetPfnEntry(Page) != NULL);

    //
    // Build the PTE
    //
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = Page;

    //
    // Pick the first hyperspace PTE
    //
    PointerPte = MmFirstReservedMappingPte;

    //
    // Acquire the hyperlock
    //
    ASSERT(Process == PsGetCurrentProcess());
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    //
    // Vista+: lock-free hyperspace. Raise IRQL to prevent preemption,
    // then use interlocked operations on the FIFO counter.
    //
    KeRaiseIrql(DISPATCH_LEVEL, OldIrql);

    //
    // Atomically read and decrement the FIFO counter.
    // PageFrameNumber is a bit-field so we operate on the full PTE value.
    // At DISPATCH_LEVEL on UP, no contention is possible, so simple
    // read-modify-write is safe. On SMP, DISPATCH prevents preemption
    // on this CPU, and per-process hyperspace means no cross-CPU contention.
    //
    Offset = PFN_FROM_PTE(PointerPte);
    if (!Offset)
    {
        Offset = MI_HYPERSPACE_PTES;
        KeFlushProcessTb();
    }
    PointerPte->u.Hard.PageFrameNumber = Offset - 1;
#else
    KeAcquireSpinLock(&Process->HyperSpaceLock, OldIrql);

    //
    // Now get the first free PTE
    //
    Offset = PFN_FROM_PTE(PointerPte);
    if (!Offset)
    {
        //
        // Reset the PTEs
        //
        Offset = MI_HYPERSPACE_PTES;
        KeFlushProcessTb();
    }

    //
    // Prepare the next PTE
    //
    PointerPte->u.Hard.PageFrameNumber = Offset - 1;
#endif

    //
    // Write the current PTE
    //
    PointerPte += Offset;
    MI_WRITE_VALID_PTE(PointerPte, TempPte);

    //
    // Return the address
    //
    return MiPteToAddress(PointerPte);
}

VOID
NTAPI
MiUnmapPageInHyperSpace(IN PEPROCESS Process,
                        IN PVOID Address,
                        IN KIRQL OldIrql)
{
    ASSERT(Process == PsGetCurrentProcess());

    //
    // Blow away the mapping
    //
    MiAddressToPte(Address)->u.Long = 0;

    //
    // Release the hyperlock
    //
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    KeLowerIrql(OldIrql);
#else
    KeReleaseSpinLock(&Process->HyperSpaceLock, OldIrql);
#endif
}

PVOID
NTAPI
MiMapPagesInZeroSpace(IN PMMPFN Pfn1,
                      IN PFN_NUMBER NumberOfPages)
{
    MMPTE TempPte;
    PMMPTE PointerPte;
    PFN_NUMBER Offset, PageFrameIndex;
#if defined(_M_ARM64)
    ULONG DcacheLineSize;
    ULONG64 Ctr;
#endif

    //
    // Sanity checks
    //
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(NumberOfPages != 0);
    ASSERT(NumberOfPages <= MI_ZERO_PTES);

    //
    // Pick the first zeroing PTE
    //
    PointerPte = MiFirstReservedZeroingPte;

    //
    // Now get the first free PTE
    //
    Offset = PFN_FROM_PTE(PointerPte);
    if (NumberOfPages > Offset)
    {
        //
        // Reset the PTEs
        //
        Offset = MI_ZERO_PTES;
        PointerPte->u.Hard.PageFrameNumber = Offset;
        KeFlushProcessTb();
    }

    //
    // Prepare the next PTE
    //
    PointerPte->u.Hard.PageFrameNumber = Offset - NumberOfPages;

    /* Choose the correct PTE to use, and which template */
    PointerPte += (Offset + 1);
    TempPte = ValidKernelPte;

#if defined(_M_ARM64)
    /* Use a Normal-NC alias for normal RAM; Device memory is not valid here. */
    MI_SET_PTE_ATTR_INDEX(&TempPte, MI_ARM64_MAIR_NORMAL_NC_IDX);
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);
    __asm__ __volatile__("dsb sy" ::: "memory");
#else
    /* Disable cache. Write through */
    MI_PAGE_DISABLE_CACHE(&TempPte);
    MI_PAGE_WRITE_THROUGH(&TempPte);
#endif

    /* Make sure the list isn't empty and loop it */
    ASSERT(Pfn1 != (PVOID)LIST_HEAD);
    while (Pfn1 != (PVOID)LIST_HEAD)
    {
        /* Get the page index for this PFN */
        PageFrameIndex = MiGetPfnEntryIndex(Pfn1);

#if defined(_M_ARM64)
        {
            ULONG_PTR Va = (ULONG_PTR)MI_ARM64_PFN_TO_VA(PageFrameIndex);
            ULONG_PTR CacheOffset;

            for (CacheOffset = 0; CacheOffset < PAGE_SIZE; CacheOffset += DcacheLineSize)
            {
                __asm__ __volatile__("dc civac, %0" :: "r"(Va + CacheOffset) : "memory");
            }
        }
#endif

        //
        // Write the PFN
        //
        TempPte.u.Hard.PageFrameNumber = PageFrameIndex;

        //
        // Set the correct PTE to write to, and set its new value
        //
        PointerPte--;
        MI_WRITE_VALID_PTE(PointerPte, TempPte);

        /* Move to the next PFN */
        Pfn1 = (PMMPFN)Pfn1->u1.Flink;
    }

#if defined(_M_ARM64)
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
#endif

    //
    // Return the address
    //
    return MiPteToAddress(PointerPte);
}

VOID
NTAPI
MiUnmapPagesInZeroSpace(IN PVOID VirtualAddress,
                        IN PFN_NUMBER NumberOfPages)
{
    PMMPTE PointerPte;

    //
    // Sanity checks
    //
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT (NumberOfPages != 0);
    ASSERT(NumberOfPages <= MI_ZERO_PTES);

    //
    // Get the first PTE for the mapped zero VA
    //
    PointerPte = MiAddressToPte(VirtualAddress);

    //
    // Blow away the mapped zero PTEs
    //
    RtlZeroMemory(PointerPte, NumberOfPages * sizeof(MMPTE));
}

