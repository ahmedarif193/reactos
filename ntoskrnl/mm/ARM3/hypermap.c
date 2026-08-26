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

#if (NTDDI_VERSION >= NTDDI_LONGHORN)

/*
 * Keep a released slot unavailable until its stale translation has been
 * invalidated. This is an invalid hardware PTE, but unlike zero it cannot be
 * claimed by another processor.
 */
#define MI_HYPERSPACE_PTE_RELEASING ((ULONG_PTR)2)

static __inline
ULONG_PTR
MiCompareExchangeHyperSpacePte(
    _Inout_ PMMPTE PointerPte,
    _In_ ULONG_PTR Exchange,
    _In_ ULONG_PTR Comparand)
{
#if defined(_M_AMD64) || defined(_M_ARM64)
    return (ULONG_PTR)InterlockedCompareExchange64((PLONG64)PointerPte,
                                                   (LONG64)Exchange,
                                                   (LONG64)Comparand);
#else
    return (ULONG_PTR)(ULONG)InterlockedCompareExchange((PLONG)PointerPte,
                                                        (LONG)Exchange,
                                                        (LONG)Comparand);
#endif
}

static __inline
ULONG_PTR
MiExchangeHyperSpacePte(
    _Inout_ PMMPTE PointerPte,
    _In_ ULONG_PTR Value)
{
#if defined(_M_AMD64) || defined(_M_ARM64)
    return (ULONG_PTR)InterlockedExchange64((PLONG64)PointerPte,
                                            (LONG64)Value);
#else
    return (ULONG_PTR)(ULONG)InterlockedExchange((PLONG)PointerPte,
                                                 (LONG)Value);
#endif
}

static __inline
BOOLEAN
MiTryClaimHyperSpacePte(
    _Inout_ PMMPTE PointerPte,
    _In_ MMPTE TempPte)
{
    ASSERT(TempPte.u.Hard.Valid == 1);
#if defined(_M_AMD64)
    ASSERT(!MI_IS_PAGE_TABLE_ADDRESS(MiPteToAddress(PointerPte)) ||
           (TempPte.u.Hard.NoExecute == 0));
#endif
#if defined(_M_ARM64)
    TempPte = MI_ARM64_PREPARE_VALID_PTE(PointerPte, TempPte);
#endif

    if (MiCompareExchangeHyperSpacePte(PointerPte, TempPte.u.Long, 0) != 0)
        return FALSE;

#if defined(_M_ARM64)
    MI_ARM64_FLUSH_VALID_PTE(PointerPte);
#endif
    return TRUE;
}

#endif

PVOID
NTAPI
MiMapPageInHyperSpace(IN PEPROCESS Process,
                      IN PFN_NUMBER Page,
                      IN PKIRQL OldIrql)
{
    MMPTE TempPte;
    PMMPTE PointerPte;
    PFN_NUMBER Offset;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PFN_NUMBER Index, SlotCount, StartOffset;
    ULONG Processor;
#endif

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
    // Vista+: raise IRQL so the mapping remains owned by this processor until
    // MiUnmapPageInHyperSpace. The PTE itself is the interlocked slot owner;
    // no Windows-visible process field is needed.
    //
    KeRaiseIrql(DISPATCH_LEVEL, OldIrql);

    //
    // Spread first choices across the range to avoid making all processors
    // contend on one cache line. Scan the complete range so nested mappings
    // remain supported and temporary collisions cannot cause false failure.
    //
    SlotCount = (PFN_NUMBER)(MmLastReservedMappingPte - PointerPte);
    ASSERT(SlotCount == MI_HYPERSPACE_PTES);
    ASSERT(SlotCount >= MAXIMUM_PROCESSORS);

    Processor = KeGetCurrentProcessorNumber() % MAXIMUM_PROCESSORS;
    StartOffset = 1 + ((SlotCount * Processor) / MAXIMUM_PROCESSORS);

    for (Index = 0; Index < SlotCount; Index++)
    {
        Offset = StartOffset + Index;
        if (Offset > SlotCount)
            Offset -= SlotCount;

        PointerPte = MmFirstReservedMappingPte + Offset;
        if (MiTryClaimHyperSpacePte(PointerPte, TempPte))
            return MiPteToAddress(PointerPte);
    }

    KeLowerIrql(*OldIrql);
    return NULL;
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
    // Write the current PTE. Vista+ returned from the atomic claim above.
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
    PMMPTE PointerPte;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    ULONG_PTR PreviousPte;
#endif

    ASSERT(Process == PsGetCurrentProcess());

    PointerPte = MiAddressToPte(Address);
    ASSERT(PointerPte > MmFirstReservedMappingPte);
    ASSERT(PointerPte <= MmLastReservedMappingPte);
    ASSERT(Address == MiPteToAddress(PointerPte));

    //
    // Release the mapping.
    //
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PreviousPte = MiExchangeHyperSpacePte(PointerPte,
                                          MI_HYPERSPACE_PTE_RELEASING);
    ASSERT((PreviousPte & 1) != 0);

#if defined(_M_ARM64)
    MiArm64CleanEntryToPoC(PointerPte);
#endif
    KeInvalidateTlbEntry(Address);

    PreviousPte = MiExchangeHyperSpacePte(PointerPte, 0);
    ASSERT(PreviousPte == MI_HYPERSPACE_PTE_RELEASING);
#else
    PointerPte->u.Long = 0;
#endif

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

