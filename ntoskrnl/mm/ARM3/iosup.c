/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/iosup.c
 * PURPOSE:         ARM Memory Manager I/O Mapping Functionality
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

//
// Each architecture has its own caching attributes for both I/O and Physical
// memory mappings.
//
// This describes the attributes for the x86 architecture. It eventually needs
// to go in the appropriate i386 directory.
//
MI_PFN_CACHE_ATTRIBUTE MiPlatformCacheAttributes[2][MmMaximumCacheType] =
{
    //
    // RAM
    //
    {MiNonCached,MiCached,MiWriteCombined,MiCached,MiNonCached,MiWriteCombined},

    //
    // Device Memory
    //
    {MiNonCached,MiCached,MiWriteCombined,MiCached,MiNonCached,MiWriteCombined},
};

LIST_ENTRY MiIoMappingListHead;
KSPIN_LOCK MiIoMappingLock;
static volatile LONG MiIoMappingListInitialized;
/* TODO: Replace list with a range tree for faster overlap checks */

static
VOID
MiEnsureIoSpaceMapInitialized(VOID)
{
    LONG State;

    if (MiIoMappingListInitialized == 2) return;

    State = InterlockedCompareExchange((PLONG)&MiIoMappingListInitialized, 1, 0);
    if (State == 0)
    {
        InitializeListHead(&MiIoMappingListHead);
        KeInitializeSpinLock(&MiIoMappingLock);
        InterlockedExchange((PLONG)&MiIoMappingListInitialized, 2);
    }
    else
    {
        while (MiIoMappingListInitialized != 2) YieldProcessor();
    }
}

VOID
NTAPI
MiInitializeIoSpaceMap(VOID)
{
    MiEnsureIoSpaceMapInitialized();
}

NTSTATUS
NTAPI
MiInsertIoSpaceMap(
    IN PFN_NUMBER PhysicalPage,
    IN PFN_NUMBER PageCount,
    IN MI_PFN_CACHE_ATTRIBUTE CacheAttribute,
    IN PVOID BaseAddress
)
{
    PFN_NUMBER EndPage;
    PMI_IO_MAPPING Mapping;
    PMI_IO_MAPPING NewMapping;
    PLIST_ENTRY NextEntry;
    KIRQL OldIrql;

    ASSERT(PageCount != 0);

    MiEnsureIoSpaceMapInitialized();

    EndPage = PhysicalPage + PageCount;
    if (EndPage < PhysicalPage) return STATUS_INVALID_PARAMETER;

    NewMapping = ExAllocatePoolWithTag(NonPagedPool, sizeof(*NewMapping), 'oIoM');
    if (!NewMapping) return STATUS_INSUFFICIENT_RESOURCES;

    NewMapping->PhysicalPage = PhysicalPage;
    NewMapping->PageCount = PageCount;
    NewMapping->CacheAttribute = CacheAttribute;
    NewMapping->BaseAddress = PAGE_ALIGN(BaseAddress);

    KeAcquireSpinLock(&MiIoMappingLock, &OldIrql);

    NextEntry = MiIoMappingListHead.Flink;
    while (NextEntry != &MiIoMappingListHead)
    {
        PFN_NUMBER MappingStart, MappingEnd;

        Mapping = CONTAINING_RECORD(NextEntry, MI_IO_MAPPING, ListEntry);
        MappingStart = Mapping->PhysicalPage;
        MappingEnd = MappingStart + Mapping->PageCount;

        if ((PhysicalPage < MappingEnd) && (EndPage > MappingStart))
        {
            if (Mapping->CacheAttribute != CacheAttribute)
            {
                DPRINT1("MM: I/O space cache conflict PFN %Ix (%Ix pages, cache %u) overlaps PFN %Ix (%Ix pages, cache %u)\n",
                        PhysicalPage,
                        PageCount,
                        CacheAttribute,
                        MappingStart,
                        Mapping->PageCount,
                        Mapping->CacheAttribute);
                KeReleaseSpinLock(&MiIoMappingLock, OldIrql);
                ExFreePoolWithTag(NewMapping, 'oIoM');
                return STATUS_CONFLICTING_ADDRESSES;
            }
        }

        NextEntry = NextEntry->Flink;
    }

    InsertTailList(&MiIoMappingListHead, &NewMapping->ListEntry);
    KeReleaseSpinLock(&MiIoMappingLock, OldIrql);

    return STATUS_SUCCESS;
}

VOID
NTAPI
MiRemoveIoSpaceMap(
    IN PFN_NUMBER PhysicalPage,
    IN PFN_NUMBER PageCount,
    IN MI_PFN_CACHE_ATTRIBUTE CacheAttribute,
    IN PVOID BaseAddress
)
{
    PMI_IO_MAPPING Mapping;
    PLIST_ENTRY NextEntry;
    KIRQL OldIrql;
    PVOID AlignedBaseAddress;

    MiEnsureIoSpaceMapInitialized();

    AlignedBaseAddress = PAGE_ALIGN(BaseAddress);

    KeAcquireSpinLock(&MiIoMappingLock, &OldIrql);

    NextEntry = MiIoMappingListHead.Flink;
    while (NextEntry != &MiIoMappingListHead)
    {
        Mapping = CONTAINING_RECORD(NextEntry, MI_IO_MAPPING, ListEntry);
        NextEntry = NextEntry->Flink;

        if ((Mapping->PhysicalPage == PhysicalPage) &&
            (Mapping->PageCount == PageCount) &&
            (Mapping->CacheAttribute == CacheAttribute) &&
            (Mapping->BaseAddress == AlignedBaseAddress))
        {
            RemoveEntryList(&Mapping->ListEntry);
            KeReleaseSpinLock(&MiIoMappingLock, OldIrql);
            ExFreePoolWithTag(Mapping, 'oIoM');
            return;
        }
    }

    KeReleaseSpinLock(&MiIoMappingLock, OldIrql);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
PVOID
NTAPI
MmMapIoSpace(IN PHYSICAL_ADDRESS PhysicalAddress,
             IN SIZE_T NumberOfBytes,
             IN MEMORY_CACHING_TYPE CacheType)
{
    NTSTATUS Status;
    PFN_NUMBER Pfn;
    PFN_NUMBER PhysicalPage;
    PFN_COUNT PageCount;
    PMMPTE PointerPte;
    PVOID BaseAddress;
    PVOID MappingBaseAddress;
    MMPTE TempPte;
    PMMPFN Pfn1 = NULL;
    MI_PFN_CACHE_ATTRIBUTE CacheAttribute;
    BOOLEAN IsIoMapping;

    //
    // Must be called with a non-zero count
    //
    ASSERT(NumberOfBytes != 0);

    //
    // Make sure the upper bits are 0 if this system
    // can't describe more than 4 GB of physical memory.
    // FIXME: This doesn't respect PAE, but we currently don't
    // define a PAE build flag since there is no such build.
    //
#if !defined(_WIN64)
    ASSERT(PhysicalAddress.HighPart == 0);
#endif

    //
    // Normalize and validate the caching attributes
    //
    CacheType &= 0xFF;
    if (CacheType >= MmMaximumCacheType) return NULL;

    //
    // Calculate page count
    //
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(PhysicalAddress.LowPart,
                                               NumberOfBytes);

    //
    // Compute the PFN and check if it's a known I/O mapping.
    // Also translate the cache attribute.
    //
    // Determine whether this is an I/O (MMIO) mapping by checking if
    // the PFN is beyond the highest physical RAM page.  We must NOT
    // rely solely on MiGetPfnEntry() for this determination because:
    //
    //   1. On amd64, PFNs for MMIO regions above 4 GB (e.g. 64-bit
    //      PCI BARs) can be larger than MAXULONG, which causes the
    //      bitmap index in MiGetPfnEntry to overflow when cast to ULONG.
    //      The aliased bit index might correspond to a valid RAM PFN,
    //      making MiGetPfnEntry return a non-NULL pointer into the PFN
    //      database -- but that entry belongs to a different physical
    //      page.  Dereferencing or trusting it corrupts state.
    //
    //   2. Even when the PFN fits in ULONG, MiGetPfnEntry returns
    //      &MmPfnDatabase[Pfn].  If the PFN falls in a physical memory
    //      hole (within MmHighestPhysicalPage but not in any memory
    //      descriptor), the corresponding PFN database pages may not be
    //      mapped (MiBuildPfnDatabase only maps PTEs for descriptor
    //      ranges).  The returned pointer is garbage.
    //
    // Checking Pfn > MmHighestPhysicalPage is the correct, safe test:
    // it is a simple integer comparison with no pointer dereference, and
    // MmHighestPhysicalPage is set from the firmware memory map during
    // MmInitSystem.  Any PFN above it is definitively MMIO.
    //
    Pfn = (PFN_NUMBER)(PhysicalAddress.QuadPart >> PAGE_SHIFT);
    PhysicalPage = Pfn;
    IsIoMapping = (Pfn > MmHighestPhysicalPage) ? TRUE : FALSE;

    if (!IsIoMapping)
    {
        //
        // PFN is within RAM range.  MiGetPfnEntry is safe to call here
        // since the PFN database is mapped for all PFNs within the
        // physical memory descriptors.  If MiGetPfnEntry returns NULL
        // (PFN falls in a hole inside the RAM range), treat it as I/O.
        //
        Pfn1 = MiGetPfnEntry(Pfn);
        if (Pfn1 == NULL)
        {
            IsIoMapping = TRUE;
        }
    }

    CacheAttribute = MiPlatformCacheAttributes[IsIoMapping][CacheType];

    //
    // Now allocate system PTEs for the mapping, and get the VA
    //
    PointerPte = MiReserveSystemPtes(PageCount, SystemPteSpace);
    if (!PointerPte) return NULL;
    BaseAddress = MiPteToAddress(PointerPte);
    MappingBaseAddress = BaseAddress;

    //
    // Enforce coherency on I/O mappings
    //
    if (IsIoMapping)
    {
        Status = MiInsertIoSpaceMap(PhysicalPage,
                                    PageCount,
                                    CacheAttribute,
                                    MappingBaseAddress);
        if (!NT_SUCCESS(Status))
        {
            MiReleaseSystemPtes(PointerPte, PageCount, SystemPteSpace);
            return NULL;
        }
    }

    //
    // Now compute the VA offset
    //
    BaseAddress = (PVOID)((ULONG_PTR)BaseAddress +
                          BYTE_OFFSET(PhysicalAddress.LowPart));

    //
    // Get the template and configure caching
    //
    TempPte = ValidKernelPte;
    switch (CacheAttribute)
    {
        case MiNonCached:

            //
            // Disable the cache
            //
            MI_PAGE_DISABLE_CACHE(&TempPte);
            MI_PAGE_WRITE_THROUGH(&TempPte);
            break;

        case MiCached:

            //
            // Leave defaults
            //
            break;

        case MiWriteCombined:

            //
            // Disable the cache and allow combined writing
            //
            MI_PAGE_DISABLE_CACHE(&TempPte);
            MI_PAGE_WRITE_COMBINED(&TempPte);
            break;

        default:

            //
            // Should never happen
            //
            ASSERT(FALSE);
            break;
    }

#if defined(_M_ARM64)
    if (IsIoMapping && CacheAttribute == MiNonCached)
    {
        /* MMIO should use Device-nGnRnE regardless of cache attributes. */
        MI_SET_PTE_ATTR_INDEX(&TempPte, 0);
    }
#endif

    //
    // Sanity check: Pfn must still match the original computation.
    // Do NOT call MiGetPfnEntry again for MMIO PFNs -- it is unsafe
    // to dereference PFN database entries outside the RAM range.
    //
    ASSERT(Pfn == (PFN_NUMBER)(PhysicalAddress.QuadPart >> PAGE_SHIFT));

    //
    // Do the mapping
    //
    SIZE_T RemainingPages = PageCount;
    PFN_NUMBER CurrentPfn = Pfn;

    do
    {
        CurrentPfn = Pfn;

        if (PointerPte->u.Hard.Valid != 0)
        {
#if DBG
            ASSERTMSG("MmMapIoSpace: remapping physical address without prior unmap",
                      FALSE);
#endif
            DbgPrintEx(DPFLTR_MM_ID,
                       DPFLTR_ERROR_LEVEL,
                       "MmMapIoSpace: reusing valid PTE %p (VA %p, raw %p, phys %I64x, bytes %Ix, remaining %Iu, cache %u, pfn %Ix)\n",
                       PointerPte,
                       MiPteToAddress(PointerPte),
                       (PVOID)(ULONG_PTR)PointerPte->u.Long,
                       PhysicalAddress.QuadPart,
                       NumberOfBytes,
                       RemainingPages,
                       CacheType,
                       CurrentPfn);
        }
        //
        // Write the PFN
        //
        TempPte.u.Hard.PageFrameNumber = Pfn++;
        MI_WRITE_VALID_PTE(PointerPte++, TempPte);
        RemainingPages--;
    } while (--PageCount);

    //
    // Always flush the TLB on all processors after writing the PTEs.
    //
    // Even though x86-64 does not negatively cache invalid PTEs, we must
    // flush because the system PTE allocator recycles VA ranges.  A stale
    // TLB entry from a PREVIOUS mapping at the same VA (on this processor
    // or another) could route accesses to the old physical page instead
    // of the MMIO target.  This is the root cause of the intermittent
    // "page fault on MmMapIoSpace VA for addresses above 4 GB" bug:
    // a stale zero/invalid TLB entry from a prior MmUnmapIoSpace at the
    // same VA wins the TLB lookup race against the freshly-written PTE,
    // causing the access to fault.
    //
    // For non-cached or write-combined mappings we additionally invalidate
    // all CPU caches (WBINVD) so that stale cached data from a prior
    // cached mapping at the same VA cannot satisfy reads to the new
    // non-cached / write-combined region.
    //
    KeFlushEntireTb(TRUE, TRUE);
    if (CacheAttribute != MiCached)
    {
        KeInvalidateAllCaches();
    }

    //
    // We're done!
    //
    return BaseAddress;
}

/*
 * @implemented
 */
VOID
NTAPI
MmUnmapIoSpace(IN PVOID BaseAddress,
               IN SIZE_T NumberOfBytes)
{
    PFN_NUMBER Pfn;
    PFN_COUNT PageCount;
    PMMPTE PointerPte;
    MI_PFN_CACHE_ATTRIBUTE CacheAttribute;

    //
    // Sanity check
    //
    ASSERT(NumberOfBytes != 0);

    //
    // Get the page count
    //
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(BaseAddress, NumberOfBytes);

    //
    // Get the PTE and PFN
    //
    PointerPte = MiAddressToPte(BaseAddress);
    Pfn = PFN_FROM_PTE(PointerPte);

    //
    // Is this an I/O mapping?
    //
    // Use the same safe detection as MmMapIoSpace: check against
    // MmHighestPhysicalPage first, then fall back to MiGetPfnEntry
    // only for PFNs within the RAM range.  This avoids the ULONG
    // truncation bug in MiGetPfnEntry's bitmap check on amd64 and
    // the unmapped PFN database access for MMIO holes.
    //
    {
        BOOLEAN IsIoUnmap;

        if (Pfn > MmHighestPhysicalPage)
        {
            IsIoUnmap = TRUE;
        }
        else
        {
            IsIoUnmap = (MiGetPfnEntry(Pfn) == NULL) ? TRUE : FALSE;
        }

        if (IsIoUnmap)
        {
            CacheAttribute = MiGetPteCacheAttribute(PointerPte);
            MiRemoveIoSpaceMap(Pfn,
                               PageCount,
                               CacheAttribute,
                               BaseAddress);
        }
    }

    //
    // Zero the PTEs before releasing them.  This must happen for ALL
    // mappings (both I/O and RAM-backed) so that MiReleaseSystemPtes
    // does not inherit PTEs with valid content.  The original code only
    // zeroed PTEs for I/O mappings, leaving RAM-backed pages with valid
    // PTEs that could cause stale TLB hits after the PTEs were recycled.
    //
    RtlZeroMemory(PointerPte, PageCount * sizeof(MMPTE));

    //
    // Flush the TLB so that stale entries pointing to the old physical
    // pages cannot be used after the PTEs are returned to the free pool.
    // Without this, a newly-allocated mapping at the same VA could see
    // accesses routed to the OLD physical page via cached TLB entries.
    //
    KeFlushEntireTb(TRUE, TRUE);

    //
    // Release the PTEs
    //
    MiReleaseSystemPtes(PointerPte, PageCount, 0);
}

/*
 * @implemented
 */
PVOID
NTAPI
MmMapVideoDisplay(IN PHYSICAL_ADDRESS PhysicalAddress,
                  IN SIZE_T NumberOfBytes,
                  IN MEMORY_CACHING_TYPE CacheType)
{
    PAGED_CODE();

    //
    // Call the real function
    //
    return MmMapIoSpace(PhysicalAddress, NumberOfBytes, CacheType);
}

/*
 * @implemented
 */
VOID
NTAPI
MmUnmapVideoDisplay(IN PVOID BaseAddress,
                    IN SIZE_T NumberOfBytes)
{
    //
    // Call the real function
    //
    MmUnmapIoSpace(BaseAddress, NumberOfBytes);
}

LOGICAL
NTAPI
MmIsIoSpaceActive(IN PHYSICAL_ADDRESS StartAddress,
                  IN SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED;
    return FALSE;
}

/* EOF */
