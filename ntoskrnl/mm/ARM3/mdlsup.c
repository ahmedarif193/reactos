/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/mdlsup.c
 * PURPOSE:         ARM Memory Manager Memory Descriptor List (MDL) Management
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/*
 * Top bit of a PFN_NUMBER slot: marks the LAST page MmAdvanceMdl parked at the
 * tail of the PFN array. Real frame numbers never set it, and (PFN | this) can
 * never equal LIST_HEAD (ULONG_PTR_MAX), so the teardown scans are bounded.
 */
#define MI_PARKED_PFN_END (((PFN_NUMBER)1) << (sizeof(PFN_NUMBER) * 8 - 1))

/* GLOBALS ********************************************************************/

BOOLEAN MmTrackPtes;
BOOLEAN MmTrackLockedPages;
SIZE_T MmSystemLockPagesCount;

ULONG MiCacheOverride[MiNotMapped + 1];

/* INTERNAL FUNCTIONS *********************************************************/

#ifdef _M_ARM64

NTSTATUS
MiArm64ProbeAndLockUserPages(
    _Inout_ PMDL Mdl,
    _In_ PVOID StartAddress,
    _In_ ULONG TotalPages,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation,
    _In_ PEPROCESS CurrentProcess);

#endif

static
PVOID
NTAPI
MiMapLockedPagesInUserSpace(
    _In_ PMDL Mdl,
    _In_ PVOID StartVa,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_opt_ PVOID BaseAddress)
{
    NTSTATUS Status;
    PEPROCESS Process = PsGetCurrentProcess();
    PETHREAD Thread = PsGetCurrentThread();
    TABLE_SEARCH_RESULT Result;
    MI_PFN_CACHE_ATTRIBUTE CacheAttribute;
    MI_PFN_CACHE_ATTRIBUTE EffectiveCacheAttribute;
    BOOLEAN IsIoMapping;
    KIRQL OldIrql;
    ULONG_PTR StartingVa;
    ULONG_PTR EndingVa;
    PMMADDRESS_NODE Parent;
    PMMVAD_LONG Vad;
    ULONG NumberOfPages;
    PMMPTE PointerPte;
    PMMPDE PointerPde;
    MMPTE TempPte;
    PPFN_NUMBER MdlPages;
    PMMPFN Pfn1;
    PMMPFN Pfn2;
    BOOLEAN AddressSpaceLocked = FALSE;

    PAGED_CODE();

    DPRINT("MiMapLockedPagesInUserSpace(%p, %p, 0x%x, %p)\n",
           Mdl, StartVa, CacheType, BaseAddress);

    NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(StartVa,
                                                   MmGetMdlByteCount(Mdl));
    MdlPages = MmGetMdlPfnArray(Mdl);

    ASSERT(CacheType <= MmWriteCombined);

    IsIoMapping = (Mdl->MdlFlags & MDL_IO_SPACE) != 0;
    CacheAttribute = MiPlatformCacheAttributes[IsIoMapping][CacheType];

    Status = PsChargeProcessNonPagedPoolQuota(Process, sizeof(MMVAD_LONG));
    if (!NT_SUCCESS(Status))
    {
        Vad = NULL;
        goto Error;
    }

    /* Allocate a VAD for our mapped region */
    Vad = ExAllocatePoolWithTag(NonPagedPool, sizeof(MMVAD_LONG), 'ldaV');
    if (Vad == NULL)
    {
        PsReturnProcessNonPagedPoolQuota(Process, sizeof(MMVAD_LONG));
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Error;
    }

    /* Initialize PhysicalMemory VAD */
    RtlZeroMemory(Vad, sizeof(*Vad));
    Vad->u2.VadFlags2.LongVad = 1;
    Vad->u.VadFlags.VadType = VadDevicePhysicalMemory;
    Vad->u.VadFlags.Protection = MM_READWRITE;
    Vad->u.VadFlags.PrivateMemory = 1;

    /* Did the caller specify an address? */
    if (BaseAddress == NULL)
    {
        /* We get to pick the address */
        MmLockAddressSpace(&Process->Vm);
        AddressSpaceLocked = TRUE;
        if (Process->VmDeleted)
        {
            Status = STATUS_PROCESS_IS_TERMINATING;
            goto Error;
        }

        Result = MiFindEmptyAddressRangeInTree(NumberOfPages << PAGE_SHIFT,
                                               MM_VIRTMEM_GRANULARITY,
                                               &Process->VadRoot,
                                               &Parent,
                                               &StartingVa);
        if (Result == TableFoundNode)
        {
            Status = STATUS_NO_MEMORY;
            goto Error;
        }
        EndingVa = StartingVa + NumberOfPages * PAGE_SIZE - 1;
        BaseAddress = (PVOID)StartingVa;
    }
    else
    {
        /* Caller specified a base address */
        StartingVa = (ULONG_PTR)BaseAddress;
        EndingVa = StartingVa + NumberOfPages * PAGE_SIZE - 1;

        /* Make sure it's valid */
        if (BYTE_OFFSET(StartingVa) != 0 ||
            EndingVa <= StartingVa ||
            EndingVa > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
        {
            Status = STATUS_INVALID_ADDRESS;
            goto Error;
        }

        MmLockAddressSpace(&Process->Vm);
        AddressSpaceLocked = TRUE;
        if (Process->VmDeleted)
        {
            Status = STATUS_PROCESS_IS_TERMINATING;
            goto Error;
        }

        /* Check if it's already in use */
        Result = MiCheckForConflictingNode(StartingVa >> PAGE_SHIFT,
                                           EndingVa >> PAGE_SHIFT,
                                           &Process->VadRoot,
                                           &Parent);
        if (Result == TableFoundNode)
        {
            Status = STATUS_CONFLICTING_ADDRESSES;
            goto Error;
        }
    }

    Vad->StartingVpn = StartingVa >> PAGE_SHIFT;
    Vad->EndingVpn = EndingVa >> PAGE_SHIFT;

    MiLockProcessWorkingSetUnsafe(Process, Thread);

    ASSERT(Vad->EndingVpn >= Vad->StartingVpn);
    MiInsertVad((PMMVAD)Vad, &Process->VadRoot);

    /* Check if this is uncached */
    if (CacheAttribute != MiCached)
    {
        /* Flush all caches */
        KeFlushEntireTb(TRUE, TRUE);
        KeInvalidateAllCaches();
    }

    PointerPte = MiAddressToPte(BaseAddress);
    while (NumberOfPages != 0 &&
           *MdlPages != LIST_HEAD)
    {
        PointerPde = MiPteToPde(PointerPte);
        MiMakePdeExistAndMakeValid(PointerPde, Process, MM_NOIRQL);
        ASSERT(PointerPte->u.Hard.Valid == 0);

        /* Add a PDE reference for each page */
        MiIncrementPageTableReferences(BaseAddress);

        /* Set up our basic user PTE */
        MI_MAKE_HARDWARE_PTE_USER(&TempPte,
                                  PointerPte,
                                  MM_READWRITE,
                                  *MdlPages);

        EffectiveCacheAttribute = CacheAttribute;

        /* We need to respect the PFN's caching information in some cases */
        Pfn2 = MiGetPfnEntry(*MdlPages);
        if (Pfn2 != NULL)
        {
            ASSERT(Pfn2->u3.e2.ReferenceCount != 0);

            switch (Pfn2->u3.e1.CacheAttribute)
            {
                case MiNonCached:
                    if (CacheAttribute != MiNonCached)
                    {
                        MiCacheOverride[1]++;
                        EffectiveCacheAttribute = MiNonCached;
                    }
                    break;

                case MiCached:
                    if (CacheAttribute != MiCached)
                    {
                        MiCacheOverride[0]++;
                        EffectiveCacheAttribute = MiCached;
                    }
                    break;

                case MiWriteCombined:
                    if (CacheAttribute != MiWriteCombined)
                    {
                        MiCacheOverride[2]++;
                        EffectiveCacheAttribute = MiWriteCombined;
                    }
                    break;

                default:
                    /* We don't support AWE magic (MiNotMapped) */
                    DPRINT1("FIXME: MiNotMapped is not supported\n");
                    ASSERT(FALSE);
                    break;
            }
        }

        /* Configure caching */
        switch (EffectiveCacheAttribute)
        {
            case MiNonCached:
                MI_PAGE_DISABLE_CACHE(&TempPte);
                MI_PAGE_WRITE_THROUGH(&TempPte);
                break;
            case MiCached:
                break;
            case MiWriteCombined:
                MI_PAGE_DISABLE_CACHE(&TempPte);
                MI_PAGE_WRITE_COMBINED(&TempPte);
                break;
            default:
                ASSERT(FALSE);
                break;
        }

        /* Make the page valid */
        MI_WRITE_VALID_PTE(PointerPte, TempPte);

        /* Acquire a share count */
        Pfn1 = MI_PFN_ELEMENT(PointerPde->u.Hard.PageFrameNumber);
        DPRINT("Incrementing %p from %p\n", Pfn1, _ReturnAddress());
        OldIrql = MiAcquirePfnLock();
        Pfn1->u2.ShareCount++;
        MiReleasePfnLock(OldIrql);

        /* Next page */
        MdlPages++;
        PointerPte++;
        NumberOfPages--;
        BaseAddress = (PVOID)((ULONG_PTR)BaseAddress + PAGE_SIZE);
    }

    MiUnlockProcessWorkingSetUnsafe(Process, Thread);
    ASSERT(AddressSpaceLocked);
    MmUnlockAddressSpace(&Process->Vm);

    ASSERT(StartingVa != 0);
    return (PVOID)((ULONG_PTR)StartingVa + MmGetMdlByteOffset(Mdl));

Error:
    if (AddressSpaceLocked)
    {
        MmUnlockAddressSpace(&Process->Vm);
    }
    if (Vad != NULL)
    {
        ExFreePoolWithTag(Vad, 'ldaV');
        PsReturnProcessNonPagedPoolQuota(Process, sizeof(MMVAD_LONG));
    }
    ExRaiseStatus(Status);
}

static
VOID
NTAPI
MiUnmapLockedPagesInUserSpace(
    _In_ PVOID BaseAddress,
    _In_ PMDL Mdl)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PETHREAD Thread = PsGetCurrentThread();
    PMMVAD Vad;
    PMMPTE PointerPte;
    PMMPDE PointerPde;
    KIRQL OldIrql;
    ULONG NumberOfPages;
    PPFN_NUMBER MdlPages;
    PFN_NUMBER PageTablePage;

    DPRINT("MiUnmapLockedPagesInUserSpace(%p, %p)\n", BaseAddress, Mdl);

    NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl),
                                                   MmGetMdlByteCount(Mdl));
    ASSERT(NumberOfPages != 0);
    MdlPages = MmGetMdlPfnArray(Mdl);

    /* Find the VAD */
    MmLockAddressSpace(&Process->Vm);
    Vad = MiLocateAddress(BaseAddress);
    if (!Vad ||
        Vad->u.VadFlags.VadType != VadDevicePhysicalMemory)
    {
        DPRINT1("MiUnmapLockedPagesInUserSpace invalid for %p\n", BaseAddress);
        MmUnlockAddressSpace(&Process->Vm);
        return;
    }

    MiLockProcessWorkingSetUnsafe(Process, Thread);

    /* Remove it from the process VAD tree */
    ASSERT(Process->VadRoot.NumberGenericTableElements >= 1);
    MiRemoveNode((PMMADDRESS_NODE)Vad, &Process->VadRoot);
    PsReturnProcessNonPagedPoolQuota(Process, sizeof(MMVAD_LONG));

    /* MiRemoveNode should have removed us if we were the hint */
    ASSERT(Process->VadRoot.NodeHint != Vad);

    PointerPte = MiAddressToPte(BaseAddress);
    OldIrql = MiAcquirePfnLock();
    while (NumberOfPages != 0 &&
           *MdlPages != LIST_HEAD)
    {
        ASSERT(MiAddressToPte(PointerPte)->u.Hard.Valid == 1);
        ASSERT(PointerPte->u.Hard.Valid == 1);

        /* Invalidate it */
        MI_ERASE_PTE(PointerPte);

        /* We invalidated this PTE, so dereference the PDE */
        PointerPde = MiAddressToPde(BaseAddress);
        PageTablePage = PointerPde->u.Hard.PageFrameNumber;
        MiDecrementShareCount(MiGetPfnEntry(PageTablePage), PageTablePage);

        if (MiDecrementPageTableReferences(BaseAddress) == 0)
        {
            ASSERT(MiIsPteOnPdeBoundary(PointerPte + 1) || (NumberOfPages == 1));
            MiDeletePde(PointerPde, Process);
        }

        /* Next page */
        PointerPte++;
        NumberOfPages--;
        BaseAddress = (PVOID)((ULONG_PTR)BaseAddress + PAGE_SIZE);
        MdlPages++;
    }

    KeFlushProcessTb();
    MiReleasePfnLock(OldIrql);
    MiUnlockProcessWorkingSetUnsafe(Process, Thread);
    MmUnlockAddressSpace(&Process->Vm);
    ExFreePoolWithTag(Vad, 'ldaV');
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
PMDL
NTAPI
MmCreateMdl(IN PMDL Mdl,
            IN PVOID Base,
            IN SIZE_T Length)
{
    SIZE_T Size;

    //
    // Check if we don't have an MDL built
    //
    if (!Mdl)
    {
        //
        // Calculate the size we'll need  and allocate the MDL
        //
        Size = MmSizeOfMdl(Base, Length);
        Mdl = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_MDL);
        if (!Mdl) return NULL;
    }

    //
    // Initialize it
    //
    MmInitializeMdl(Mdl, Base, Length);
    return Mdl;
}

/*
 * @implemented
 */
SIZE_T
NTAPI
MmSizeOfMdl(IN PVOID Base,
            IN SIZE_T Length)
{
    //
    // Return the MDL size
    //
    return sizeof(MDL) +
           (ADDRESS_AND_SIZE_TO_SPAN_PAGES(Base, Length) * sizeof(PFN_NUMBER));
}

/*
 * @implemented
 */
VOID
NTAPI
MmBuildMdlForNonPagedPool(IN PMDL Mdl)
{
    PPFN_NUMBER MdlPages, EndPage;
    PFN_NUMBER Pfn, PageCount;
    PVOID Base;
    PMMPTE PointerPte;

    //
    // Sanity checks
    //
    ASSERT(Mdl->ByteCount != 0);
    ASSERT((Mdl->MdlFlags & (MDL_PAGES_LOCKED |
                             MDL_MAPPED_TO_SYSTEM_VA |
                             MDL_SOURCE_IS_NONPAGED_POOL |
                             MDL_PARTIAL)) == 0);

    //
    // We know the MDL isn't associated to a process now
    //
    Mdl->Process = NULL;

    //
    // Get page and VA information
    //
    MdlPages = (PPFN_NUMBER)(Mdl + 1);
    Base = Mdl->StartVa;

    //
    // Set the system address and now get the page count
    //
    Mdl->MappedSystemVa = (PVOID)((ULONG_PTR)Base + Mdl->ByteOffset);
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Mdl->MappedSystemVa,
                                               Mdl->ByteCount);
    ASSERT(PageCount != 0);
    EndPage = MdlPages + PageCount;

    //
    // Loop the PTEs
    //
    PointerPte = MiAddressToPte(Base);
    do
    {
        //
        // Write the PFN
        //
        Pfn = PFN_FROM_PTE(PointerPte++);
        *MdlPages++ = Pfn;
    } while (MdlPages < EndPage);

    //
    // Set the nonpaged pool flag
    //
    Mdl->MdlFlags |= MDL_SOURCE_IS_NONPAGED_POOL;

    //
    // Check if this is an I/O mapping
    //
    if (!MiGetPfnEntry(Pfn)) Mdl->MdlFlags |= MDL_IO_SPACE;
}

/*
 * @implemented
 */
PMDL
NTAPI
MmAllocatePagesForMdl(IN PHYSICAL_ADDRESS LowAddress,
                      IN PHYSICAL_ADDRESS HighAddress,
                      IN PHYSICAL_ADDRESS SkipBytes,
                      IN SIZE_T TotalBytes)
{
    //
    // Call the internal routine
    //
    return MiAllocatePagesForMdl(LowAddress,
                                 HighAddress,
                                 SkipBytes,
                                 TotalBytes,
                                 MiNotMapped,
                                 0);
}

/*
 * @implemented
 */
PMDL
NTAPI
MmAllocatePagesForMdlEx(IN PHYSICAL_ADDRESS LowAddress,
                        IN PHYSICAL_ADDRESS HighAddress,
                        IN PHYSICAL_ADDRESS SkipBytes,
                        IN SIZE_T TotalBytes,
                        IN MEMORY_CACHING_TYPE CacheType,
                        IN ULONG Flags)
{
    MI_PFN_CACHE_ATTRIBUTE CacheAttribute;

    //
    // Check for invalid cache type
    //
    if (CacheType > MmWriteCombined)
    {
        //
        // Normalize to default
        //
        CacheAttribute = MiNotMapped;
    }
    else
    {
        //
        // Convert to internal caching attribute
        //
        CacheAttribute = MiPlatformCacheAttributes[FALSE][CacheType];
    }

    //
    // Only these flags are allowed
    //
    if (Flags & ~(MM_DONT_ZERO_ALLOCATION | MM_ALLOCATE_FROM_LOCAL_NODE_ONLY))
    {
        //
        // Silently fail
        //
        return NULL;
    }

    //
    // Call the internal routine
    //
    return MiAllocatePagesForMdl(LowAddress,
                                 HighAddress,
                                 SkipBytes,
                                 TotalBytes,
                                 CacheAttribute,
                                 Flags);
}

/*
 * @implemented
 */
VOID
NTAPI
MmFreePagesFromMdl(IN PMDL Mdl)
{
    PVOID Base;
    PPFN_NUMBER Pages;
    LONG NumberOfPages;
    PMMPFN Pfn1;
    KIRQL OldIrql;
    DPRINT("Freeing MDL: %p\n", Mdl);

    //
    // Sanity checks
    //
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT((Mdl->MdlFlags & MDL_IO_SPACE) == 0);
    ASSERT(((ULONG_PTR)Mdl->StartVa & (PAGE_SIZE - 1)) == 0);

    //
    // Get address and page information
    //
    Base = (PVOID)((ULONG_PTR)Mdl->StartVa + Mdl->ByteOffset);
    NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Base, Mdl->ByteCount);

    //
    // Acquire PFN lock
    //
    OldIrql = MiAcquirePfnLock();

    //
    // Loop all the MDL pages
    //
    Pages = (PPFN_NUMBER)(Mdl + 1);
    do
    {
        //
        // Reached the last page
        //
        if (*Pages == LIST_HEAD) break;

        //
        // Get the page entry
        //
        Pfn1 = MiGetPfnEntry(*Pages);
        ASSERT(Pfn1);
        ASSERT(Pfn1->u2.ShareCount == 1);
        ASSERT(MI_IS_PFN_DELETED(Pfn1) == TRUE);
        if (Pfn1->u4.PteFrame != 0x1FFEDCB)
        {
            /* Corrupted PFN entry or invalid free */
            KeBugCheckEx(MEMORY_MANAGEMENT, 0x1236, (ULONG_PTR)Mdl, (ULONG_PTR)Pages, *Pages);
        }

        //
        // Clear it
        //
        Pfn1->u3.e1.StartOfAllocation = 0;
        Pfn1->u3.e1.EndOfAllocation = 0;
        Pfn1->u3.e1.PageLocation = StandbyPageList;
        Pfn1->u2.ShareCount = 0;

        //
        // Dereference it
        //
        ASSERT(Pfn1->u3.e2.ReferenceCount != 0);
        if (Pfn1->u3.e2.ReferenceCount != 1)
        {
            /* Just take off one reference */
            InterlockedDecrement16((PSHORT)&Pfn1->u3.e2.ReferenceCount);
        }
        else
        {
            /* We'll be nuking the whole page */
            MiDecrementReferenceCount(Pfn1, *Pages);
        }

        //
        // Clear this page and move on
        //
        *Pages++ = LIST_HEAD;
    } while (--NumberOfPages != 0);

    //
    // Release the lock
    //
    MiReleasePfnLock(OldIrql);

    //
    // Remove the pages locked flag
    //
    Mdl->MdlFlags &= ~MDL_PAGES_LOCKED;
}

/*
 * @implemented
 */
PVOID
NTAPI
MmMapLockedPagesSpecifyCache(IN PMDL Mdl,
                             IN KPROCESSOR_MODE AccessMode,
                             IN MEMORY_CACHING_TYPE CacheType,
                             IN PVOID BaseAddress,
                             IN ULONG BugCheckOnFailure,
                             IN ULONG Priority) // MM_PAGE_PRIORITY
{
    PVOID Base;
    PPFN_NUMBER MdlPages, LastPage;
    PFN_COUNT PageCount;
    BOOLEAN IsIoMapping;
    MI_PFN_CACHE_ATTRIBUTE CacheAttribute;
    PMMPTE PointerPte;
    MMPTE TempPte;

    //
    // Sanity check
    //
    ASSERT(Mdl->ByteCount != 0);

    //
    // Get the base
    //
    Base = (PVOID)((ULONG_PTR)Mdl->StartVa + Mdl->ByteOffset);

    //
    // Handle kernel case first
    //
    if (AccessMode == KernelMode)
    {
        //
        // Get the list of pages and count
        //
        MdlPages = (PPFN_NUMBER)(Mdl + 1);
        PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Base, Mdl->ByteCount);
        LastPage = MdlPages + PageCount;

        //
        // Sanity checks
        //
        ASSERT((Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA |
                                 MDL_SOURCE_IS_NONPAGED_POOL |
                                 MDL_PARTIAL_HAS_BEEN_MAPPED)) == 0);
        ASSERT((Mdl->MdlFlags & (MDL_PAGES_LOCKED | MDL_PARTIAL)) != 0);

        //
        // Get the correct cache type
        //
        IsIoMapping = (Mdl->MdlFlags & MDL_IO_SPACE) != 0;
        CacheAttribute = MiPlatformCacheAttributes[IsIoMapping][CacheType];

        //
        // Reserve the PTEs
        //
        PointerPte = MiReserveSystemPtes(PageCount, SystemPteSpace);
        if (!PointerPte)
        {
            //
            // If it can fail, return NULL
            //
            if (Mdl->MdlFlags & MDL_MAPPING_CAN_FAIL) return NULL;

            //
            // Should we bugcheck?
            //
            if (!BugCheckOnFailure) return NULL;

            //
            // Yes, crash the system
            //
            KeBugCheckEx(NO_MORE_SYSTEM_PTES, 0, PageCount, 0, 0);
        }

        //
        // Get the mapped address
        //
        Base = (PVOID)((ULONG_PTR)MiPteToAddress(PointerPte) + Mdl->ByteOffset);

        //
        // Get the template
        //
        TempPte = ValidKernelPte;
        switch (CacheAttribute)
        {
            case MiNonCached:

                //
                // Disable caching
                //
                MI_PAGE_DISABLE_CACHE(&TempPte);
                MI_PAGE_WRITE_THROUGH(&TempPte);
                break;

            case MiWriteCombined:

                //
                // Enable write combining
                //
                MI_PAGE_DISABLE_CACHE(&TempPte);
                MI_PAGE_WRITE_COMBINED(&TempPte);
                break;

            default:
                //
                // Nothing to do
                //
                break;
        }

        //
        // Loop all PTEs
        //
        do
        {
            //
            // We're done here
            //
            if (*MdlPages == LIST_HEAD) break;

            //
            // Write the PTE
            //
            TempPte.u.Hard.PageFrameNumber = *MdlPages;
            MI_WRITE_VALID_PTE(PointerPte++, TempPte);
        } while (++MdlPages < LastPage);

        //
        // Mark it as mapped
        //
        ASSERT((Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA) == 0);
        Mdl->MappedSystemVa = Base;
        Mdl->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;

        //
        // Check if it was partial
        //
        if (Mdl->MdlFlags & MDL_PARTIAL)
        {
            //
            // Write the appropriate flag here too
            //
            Mdl->MdlFlags |= MDL_PARTIAL_HAS_BEEN_MAPPED;
        }

        //
        // Return the mapped address
        //
        return Base;
    }

    return MiMapLockedPagesInUserSpace(Mdl, Base, CacheType, BaseAddress);
}

/*
 * @implemented
 */
PVOID
NTAPI
MmMapLockedPages(IN PMDL Mdl,
                 IN KPROCESSOR_MODE AccessMode)
{
    //
    // Call the extended version
    //
    return MmMapLockedPagesSpecifyCache(Mdl,
                                        AccessMode,
                                        MmCached,
                                        NULL,
                                        TRUE,
                                        HighPagePriority);
}

/*
 * @implemented
 */
VOID
NTAPI
MmUnmapLockedPages(IN PVOID BaseAddress,
                   IN PMDL Mdl)
{
    PVOID Base;
    PFN_COUNT PageCount, ExtraPageCount;
    PPFN_NUMBER MdlPages;
    PMMPTE PointerPte;

    //
    // Sanity check
    //
    ASSERT(Mdl->ByteCount != 0);

    //
    // Check if this is a kernel request
    //
    if (BaseAddress > MM_HIGHEST_USER_ADDRESS)
    {
        //
        // Get base and count information
        //
        Base = (PVOID)((ULONG_PTR)Mdl->StartVa + Mdl->ByteOffset);
        PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Base, Mdl->ByteCount);

        //
        // Sanity checks
        //
        ASSERT((Mdl->MdlFlags & MDL_PARENT_MAPPED_SYSTEM_VA) == 0);
        ASSERT(PageCount != 0);
        ASSERT(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA);

        //
        // Get the PTE
        //
        PointerPte = MiAddressToPte(BaseAddress);

        //
        // This should be a resident system PTE - either a valid mapping or a
        // transition PTE parked by MmProtectMdlSystemAddress(PAGE_NOACCESS),
        // which the release below handles either way.
        //
        ASSERT(PointerPte >= MmSystemPtesStart[SystemPteSpace]);
        ASSERT(PointerPte <= MmSystemPtesEnd[SystemPteSpace]);
        ASSERT((PointerPte->u.Hard.Valid == 1) ||
               (PointerPte->u.Trans.Transition == 1));

        //
        // Check if the caller wants us to free advanced pages
        //
        if (Mdl->MdlFlags & MDL_FREE_EXTRA_PTES)
        {
            //
            // Get the MDL page array
            //
            MdlPages = MmGetMdlPfnArray(Mdl);

            //
            // MmAdvanceMdl parked the consumed pages right after the current
            // span and tagged the last one with MI_PARKED_PFN_END. Count them
            // by scanning to that sentinel.
            //
            for (ExtraPageCount = 0;
                 !(MdlPages[PageCount + ExtraPageCount] & MI_PARKED_PFN_END);
                 ExtraPageCount++)
            {
                NOTHING;
            }
            ExtraPageCount++;

            //
            // Free the leading PTEs too and walk the base/PTE pointer back to
            // the original (pre-advance) mapping start
            //
            PointerPte -= ExtraPageCount;
            ASSERT(PointerPte >= MmSystemPtesStart[SystemPteSpace]);
            ASSERT(PointerPte <= MmSystemPtesEnd[SystemPteSpace]);
            BaseAddress = (PVOID)((ULONG_PTR)BaseAddress -
                                  (ExtraPageCount << PAGE_SHIFT));
            PageCount += ExtraPageCount;

            //
            // Restore the MDL to its pre-advance page span: clear the sentinel
            // and walk StartVa/ByteCount back so any later MmUnlockPages sees a
            // whole MDL (order-independent; survivors stay at the front).
            //
            MdlPages[PageCount - 1] &= ~MI_PARKED_PFN_END;
            Mdl->StartVa = (PVOID)((ULONG_PTR)Mdl->StartVa -
                                   (ExtraPageCount << PAGE_SHIFT));
            Mdl->ByteCount += (ExtraPageCount << PAGE_SHIFT);
        }

        //
        // Remove flags
        //
        Mdl->MdlFlags &= ~(MDL_MAPPED_TO_SYSTEM_VA |
                           MDL_PARTIAL_HAS_BEEN_MAPPED |
                           MDL_FREE_EXTRA_PTES);

        //
        // Release the system PTEs
        //
        MiReleaseSystemPtes(PointerPte, PageCount, SystemPteSpace);
    }
    else
    {
        MiUnmapLockedPagesInUserSpace(BaseAddress, Mdl);
    }
}

/*
 * @implemented
 */
VOID
NTAPI
MmProbeAndLockPages(IN PMDL Mdl,
                    IN KPROCESSOR_MODE AccessMode,
                    IN LOCK_OPERATION Operation)
{
    PPFN_NUMBER MdlPages;
    PVOID Base, Address, LastAddress, StartAddress;
    ULONG LockPages, TotalPages;
    NTSTATUS Status = STATUS_SUCCESS;
    PEPROCESS CurrentProcess;
    NTSTATUS ProbeStatus;
    PMMPTE PointerPte, LastPte;
    PMMPDE PointerPde;
#if (_MI_PAGING_LEVELS >= 3)
    PMMPDE PointerPpe;
#endif
#if (_MI_PAGING_LEVELS == 4)
    PMMPDE PointerPxe;
#endif
    PFN_NUMBER PageFrameIndex;
    BOOLEAN UsePfnLock;
    KIRQL OldIrql;
    PMMPFN Pfn1;
    DPRINT("Probing MDL: %p\n", Mdl);

    //
    // Sanity checks
    //
    ASSERT(Mdl->ByteCount != 0);
    ASSERT(((ULONG)Mdl->ByteOffset & ~(PAGE_SIZE - 1)) == 0);
    ASSERT(((ULONG_PTR)Mdl->StartVa & (PAGE_SIZE - 1)) == 0);
    ASSERT((Mdl->MdlFlags & (MDL_PAGES_LOCKED |
                             MDL_MAPPED_TO_SYSTEM_VA |
                             MDL_SOURCE_IS_NONPAGED_POOL |
                             MDL_PARTIAL |
                             MDL_IO_SPACE)) == 0);

    //
    // Get page and base information
    //
    MdlPages = (PPFN_NUMBER)(Mdl + 1);
    Base = Mdl->StartVa;

    //
    // Get the addresses and how many pages we span (and need to lock)
    //
    Address = (PVOID)((ULONG_PTR)Base + Mdl->ByteOffset);
    LastAddress = (PVOID)((ULONG_PTR)Address + Mdl->ByteCount);
    LockPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Address, Mdl->ByteCount);
    ASSERT(LockPages != 0);

    /* Block invalid access */
    if ((AccessMode != KernelMode) &&
        ((LastAddress > (PVOID)MM_USER_PROBE_ADDRESS) || (Address >= LastAddress)))
    {
        /* Caller should be in SEH, raise the error */
        *MdlPages = LIST_HEAD;
        ExRaiseStatus(STATUS_ACCESS_VIOLATION);
    }

    //
    // Get the process
    //
    if (Address <= MM_HIGHEST_USER_ADDRESS)
    {
        //
        // Get the process
        //
        CurrentProcess = PsGetCurrentProcess();
    }
    else
    {
        //
        // No process
        //
        CurrentProcess = NULL;
    }

    //
    // Save the number of pages we'll have to lock, and the start address
    //
    TotalPages = LockPages;
    StartAddress = Address;

    /* Large pages not supported */
    ASSERT(!MI_IS_PHYSICAL_ADDRESS(Address));

#ifdef _M_ARM64
    if (CurrentProcess != NULL)
    {
        Status = MiArm64ProbeAndLockUserPages(Mdl,
                                              StartAddress,
                                              TotalPages,
                                              AccessMode,
                                              Operation,
                                              CurrentProcess);
        if (!NT_SUCCESS(Status))
        {
            ExRaiseStatus(Status);
        }

        return;
    }
#endif

    //
    // Now probe them
    //
    ProbeStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        //
        // Enter probe loop
        //
        do
        {
            //
            // Assume failure
            //
            *MdlPages = LIST_HEAD;

            //
            // Read
            //
            *(volatile CHAR*)Address;

            //
            // Check if this is write access (only probe for user-mode)
            //
            if ((Operation != IoReadAccess) &&
                (Address <= MM_HIGHEST_USER_ADDRESS))
            {
                //
                // Probe for write too
                //
                ProbeForWriteChar(Address);
            }

            //
            // Next address...
            //
            Address = PAGE_ALIGN((ULONG_PTR)Address + PAGE_SIZE);

            //
            // Next page...
            //
            LockPages--;
            MdlPages++;
        } while (Address < LastAddress);

        //
        // Reset back to the original page
        //
        ASSERT(LockPages == 0);
        MdlPages = (PPFN_NUMBER)(Mdl + 1);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        //
        // Oops :(
        //
        ProbeStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    //
    // So how did that go?
    //
    if (ProbeStatus != STATUS_SUCCESS)
    {
        //
        // Fail
        //
        DPRINT1("MDL PROBE FAILED!\n");
        Mdl->Process = NULL;
        ExRaiseStatus(ProbeStatus);
    }

    //
    // Get the PTE and PDE
    //
    PointerPte = MiAddressToPte(StartAddress);
    PointerPde = MiAddressToPde(StartAddress);
#if (_MI_PAGING_LEVELS >= 3)
    PointerPpe = MiAddressToPpe(StartAddress);
#endif
#if (_MI_PAGING_LEVELS == 4)
    PointerPxe = MiAddressToPxe(StartAddress);
#endif

    //
    // Sanity check
    //
    ASSERT(MdlPages == (PPFN_NUMBER)(Mdl + 1));

    //
    // Check what kind of operation this is
    //
    if (Operation != IoReadAccess)
    {
        //
        // Set the write flag
        //
        Mdl->MdlFlags |= MDL_WRITE_OPERATION;
    }
    else
    {
        //
        // Remove the write flag
        //
        Mdl->MdlFlags &= ~(MDL_WRITE_OPERATION);
    }

    //
    // Mark the MDL as locked *now*
    //
    Mdl->MdlFlags |= MDL_PAGES_LOCKED;

    //
    // Check if this came from kernel mode
    //
    if (Base > MM_HIGHEST_USER_ADDRESS)
    {
        //
        // We should not have a process
        //
        ASSERT(CurrentProcess == NULL);
        Mdl->Process = NULL;

        //
        // In kernel mode, we don't need to check for write access
        //
        Operation = IoReadAccess;

        //
        // Use the PFN lock
        //
        UsePfnLock = TRUE;
        OldIrql = MiAcquirePfnLock();
    }
    else
    {
        //
        // Sanity checks
        //
        ASSERT(TotalPages != 0);
        ASSERT(CurrentProcess == PsGetCurrentProcess());

        //
        // Track locked pages
        //
        InterlockedExchangeAddSizeT(&CurrentProcess->NumberOfLockedPages,
                                    TotalPages);

        //
        // Save the process
        //
        Mdl->Process = CurrentProcess;

        /* Lock the process working set */
        MiLockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
        UsePfnLock = FALSE;
        OldIrql = MM_NOIRQL;
    }

    //
    // Get the last PTE
    //
    LastPte = MiAddressToPte((PVOID)((ULONG_PTR)LastAddress - 1));

    //
    // Loop the pages
    //
    do
    {
        //
        // Assume failure and check for non-mapped pages
        //
        *MdlPages = LIST_HEAD;
        while (
#if (_MI_PAGING_LEVELS == 4)
               (PointerPxe->u.Hard.Valid == 0) ||
#endif
#if (_MI_PAGING_LEVELS >= 3)
               (PointerPpe->u.Hard.Valid == 0) ||
#endif
               (PointerPde->u.Hard.Valid == 0) ||
               (PointerPte->u.Hard.Valid == 0))
        {
            //
            // What kind of lock were we using?
            //
            if (UsePfnLock)
            {
                //
                // Release PFN lock
                //
                MiReleasePfnLock(OldIrql);
            }
            else
            {
                /* Release process working set */
                MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
            }

            //
            // Access the page
            //
            Address = MiPteToAddress(PointerPte);

            //HACK: Pass a placeholder TrapInformation so the fault handler knows we're unlocked
            Status = MmAccessFault(FALSE, Address, KernelMode, (PVOID)(ULONG_PTR)0xBADBADA3BADBADA3ULL);
            if (!NT_SUCCESS(Status))
            {
                //
                // Fail
                //
                DPRINT1("Access fault failed\n");
                goto Cleanup;
            }

            //
            // What lock should we use?
            //
            if (UsePfnLock)
            {
                //
                // Grab the PFN lock
                //
                OldIrql = MiAcquirePfnLock();
            }
            else
            {
                /* Lock the process working set */
                MiLockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
            }
        }

        //
        // Check if this was a write or modify
        //
        if (Operation != IoReadAccess)
        {
            //
            // Check if the PTE is not writable
            //
            if (MI_IS_PAGE_WRITEABLE(PointerPte) == FALSE)
            {
                //
                // Check if it's copy on write
                //
                if (MI_IS_PAGE_COPY_ON_WRITE(PointerPte))
                {
                    //
                    // Get the base address and allow a change for user-mode
                    //
                    Address = MiPteToAddress(PointerPte);
                    if (Address <= MM_HIGHEST_USER_ADDRESS)
                    {
                        //
                        // What kind of lock were we using?
                        //
                        if (UsePfnLock)
                        {
                            //
                            // Release PFN lock
                            //
                            MiReleasePfnLock(OldIrql);
                        }
                        else
                        {
                            /* Release process working set */
                            MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
                        }

                        //
                        // Access the page
                        //

                        //HACK: Pass a placeholder TrapInformation so the fault handler knows we're unlocked
                        Status = MmAccessFault(TRUE, Address, KernelMode, (PVOID)(ULONG_PTR)0xBADBADA3BADBADA3ULL);
                        if (!NT_SUCCESS(Status))
                        {
                            //
                            // Fail
                            //
                            DPRINT1("Access fault failed\n");
                            goto Cleanup;
                        }

                        //
                        // Re-acquire the lock
                        //
                        if (UsePfnLock)
                        {
                            //
                            // Grab the PFN lock
                            //
                            OldIrql = MiAcquirePfnLock();
                        }
                        else
                        {
                            /* Lock the process working set */
                            MiLockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
                        }

                        //
                        // Start over
                        //
                        continue;
                    }
                }

                //
                // Fail, since we won't allow this
                //
                Status = STATUS_ACCESS_VIOLATION;
                goto CleanupWithLock;
            }
        }

        //
        // Grab the PFN
        //
        PageFrameIndex = PFN_FROM_PTE(PointerPte);
        Pfn1 = MiGetPfnEntry(PageFrameIndex);
        if (Pfn1)
        {
            /* Either this is for kernel-mode, or the working set is held */
            ASSERT((CurrentProcess == NULL) || (UsePfnLock == FALSE));

            /* No Physical VADs supported yet */
            if (CurrentProcess) ASSERT(CurrentProcess->PhysicalVadRoot == NULL);

            /* This address should already exist and be fully valid */
            MiReferenceProbedPageAndBumpLockCount(Pfn1);
        }
        else
        {
            //
            // For I/O addresses, just remember this
            //
            Mdl->MdlFlags |= MDL_IO_SPACE;
        }

        //
        // Write the page and move on
        //
        *MdlPages++ = PageFrameIndex;
        PointerPte++;

        /* Check if we're on a PDE boundary */
        if (MiIsPteOnPdeBoundary(PointerPte)) PointerPde++;
#if (_MI_PAGING_LEVELS >= 3)
        if (MiIsPteOnPpeBoundary(PointerPte)) PointerPpe++;
#endif
#if (_MI_PAGING_LEVELS == 4)
        if (MiIsPteOnPxeBoundary(PointerPte)) PointerPxe++;
#endif

    } while (PointerPte <= LastPte);

    //
    // What kind of lock were we using?
    //
    if (UsePfnLock)
    {
        //
        // Release PFN lock
        //
        MiReleasePfnLock(OldIrql);
    }
    else
    {
        /* Release process working set */
        MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
    }

    //
    // Sanity check
    //
    ASSERT((Mdl->MdlFlags & MDL_DESCRIBES_AWE) == 0);
    return;

CleanupWithLock:
    //
    // This is the failure path
    //
    ASSERT(!NT_SUCCESS(Status));

    //
    // What kind of lock were we using?
    //
    if (UsePfnLock)
    {
        //
        // Release PFN lock
        //
        MiReleasePfnLock(OldIrql);
    }
    else
    {
        /* Release process working set */
        MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
    }
Cleanup:
    //
    // Pages must be locked so MmUnlock can work
    //
    ASSERT(Mdl->MdlFlags & MDL_PAGES_LOCKED);
    MmUnlockPages(Mdl);

    //
    // Raise the error
    //
    ExRaiseStatus(Status);
}

/*
 * @implemented
 */
VOID
NTAPI
MmUnlockPages(IN PMDL Mdl)
{
    PPFN_NUMBER MdlPages, LastPage;
    PEPROCESS Process;
    PVOID Base;
    ULONG Flags, PageCount;
    KIRQL OldIrql;
    PMMPFN Pfn1;
    DPRINT("Unlocking MDL: %p\n", Mdl);

    //
    // Sanity checks
    //
    ASSERT((Mdl->MdlFlags & MDL_PAGES_LOCKED) != 0);
    ASSERT((Mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL) == 0);
    ASSERT((Mdl->MdlFlags & MDL_PARTIAL) == 0);
    ASSERT(Mdl->ByteCount != 0);

    //
    // Get the process associated and capture the flags which are volatile
    //
    Process = Mdl->Process;
    Flags = Mdl->MdlFlags;

    //
    // Automagically undo any calls to MmGetSystemAddressForMdl's for this MDL
    //
    if (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
    {
        //
        // Unmap the pages from system space
        //
        MmUnmapLockedPages(Mdl->MappedSystemVa, Mdl);
    }

    //
    // Get the page count
    //
    MdlPages = (PPFN_NUMBER)(Mdl + 1);
    Base = (PVOID)((ULONG_PTR)Mdl->StartVa + Mdl->ByteOffset);
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Base, Mdl->ByteCount);
    ASSERT(PageCount != 0);

    //
    // If MmAdvanceMdl parked consumed pages and no MmUnmapLockedPages restored
    // them (an unmapped MDL), fold them back into the span so every originally
    // locked page is released. For a mapped MDL, MmUnmapLockedPages above already
    // restored the full span and cleared the flag, so this is skipped.
    //
    if (Mdl->MdlFlags & MDL_FREE_EXTRA_PTES)
    {
        while (!(MdlPages[PageCount] & MI_PARKED_PFN_END)) PageCount++;
        MdlPages[PageCount] &= ~MI_PARKED_PFN_END;
        PageCount++;
        Mdl->MdlFlags &= ~MDL_FREE_EXTRA_PTES;
    }

    //
    // We don't support AWE
    //
    if (Flags & MDL_DESCRIBES_AWE) ASSERT(FALSE);

    //
    // Check if the buffer is mapped I/O space
    //
    if (Flags & MDL_IO_SPACE)
    {
        //
        // Acquire PFN lock
        //
        OldIrql = MiAcquirePfnLock();

        //
        // Loop every page
        //
        LastPage = MdlPages + PageCount;
        do
        {
            //
            // Last page, break out
            //
            if (*MdlPages == LIST_HEAD) break;

            //
            // Check if this page is in the PFN database
            //
            Pfn1 = MiGetPfnEntry(*MdlPages);
            if (Pfn1) MiDereferencePfnAndDropLockCount(Pfn1);
        } while (++MdlPages < LastPage);

        //
        // Release the lock
        //
        MiReleasePfnLock(OldIrql);

        //
        // Check if we have a process
        //
        if (Process)
        {
            //
            // Handle the accounting of locked pages
            //
            ASSERT(Process->NumberOfLockedPages > 0);
            InterlockedExchangeAddSizeT(&Process->NumberOfLockedPages,
                                        -(LONG_PTR)PageCount);
        }

        //
        // We're done
        //
        Mdl->MdlFlags &= ~MDL_IO_SPACE;
        Mdl->MdlFlags &= ~MDL_PAGES_LOCKED;
        return;
    }

    //
    // Check if we have a process
    //
    if (Process)
    {
        //
        // Handle the accounting of locked pages
        //
        ASSERT(Process->NumberOfLockedPages > 0);
        InterlockedExchangeAddSizeT(&Process->NumberOfLockedPages,
                                    -(LONG_PTR)PageCount);
    }

    //
    // Loop every page
    //
    LastPage = MdlPages + PageCount;
    do
    {
        //
        // Last page reached
        //
        if (*MdlPages == LIST_HEAD)
        {
            //
            // Were there no pages at all?
            //
            if (MdlPages == (PPFN_NUMBER)(Mdl + 1))
            {
                //
                // We're already done
                //
                Mdl->MdlFlags &= ~MDL_PAGES_LOCKED;
                return;
            }

            //
            // Otherwise, stop here
            //
            LastPage = MdlPages;
            break;
        }

        /* Save the PFN entry instead for the secondary loop */
        *MdlPages = (PFN_NUMBER)MiGetPfnEntry(*MdlPages);
        ASSERT(*MdlPages != 0);
    } while (++MdlPages < LastPage);

    //
    // Reset pointer
    //
    MdlPages = (PPFN_NUMBER)(Mdl + 1);

    //
    // Now grab the PFN lock for the actual unlock and dereference
    //
    OldIrql = MiAcquirePfnLock();
    do
    {
        /* Get the current entry and reference count */
        Pfn1 = (PMMPFN)*MdlPages;
        MiDereferencePfnAndDropLockCount(Pfn1);
    } while (++MdlPages < LastPage);

    //
    // Release the lock
    //
    MiReleasePfnLock(OldIrql);

    //
    // We're done
    //
    Mdl->MdlFlags &= ~MDL_PAGES_LOCKED;
}

/*
 * @implemented
 *
 * Advance the MDL forward by NumberOfBytes (Win11 contract, verified against
 * the ARM64 ntoskrnl disassembly). The consumed leading pages are rotated to
 * the tail and the last one is tagged with MI_PARKED_PFN_END (rather than
 * dropped), so MDL teardown can still free their PTEs and release their lock
 * references; MmUnmapLockedPages / MmUnmapReservedMapping / MmUnlockPages
 * recover the parked pages by scanning to that sentinel (see those routines).
 */
NTSTATUS
NTAPI
MmAdvanceMdl(IN PMDL Mdl,
             IN ULONG NumberOfBytes)
{
    PPFN_NUMBER MdlPages;
    PFN_NUMBER Tmp;
    ULONG TotalPages, PageCount, Lo, Hi, NewByteOffset;

    //
    // Proceed only if NumberOfBytes is strictly less than the described bytes
    // (a surviving page is guaranteed); otherwise it is an invalid request.
    //
    if (NumberOfBytes >= Mdl->ByteCount)
        return STATUS_INVALID_PARAMETER_2;

    TotalPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl),
                                                Mdl->ByteCount);
    NewByteOffset = Mdl->ByteOffset + NumberOfBytes;
    PageCount = NewByteOffset >> PAGE_SHIFT;     // whole pages consumed by the advance

    if (PageCount != 0)
    {
        MdlPages = MmGetMdlPfnArray(Mdl);

        //
        // Left-rotate the current logical sub-array [0..TotalPages-1] by
        // PageCount (three-reversal, in place): survivors move to the front so
        // PFN[0] maps the new start, consumed pages move just before any pages
        // parked by an earlier advance (which stay put). Tail order is
        // irrelevant to teardown.
        //
        for (Lo = 0, Hi = PageCount - 1; Lo < Hi; Lo++, Hi--)
            { Tmp = MdlPages[Lo]; MdlPages[Lo] = MdlPages[Hi]; MdlPages[Hi] = Tmp; }
        for (Lo = PageCount, Hi = TotalPages - 1; Lo < Hi; Lo++, Hi--)
            { Tmp = MdlPages[Lo]; MdlPages[Lo] = MdlPages[Hi]; MdlPages[Hi] = Tmp; }
        for (Lo = 0, Hi = TotalPages - 1; Lo < Hi; Lo++, Hi--)
            { Tmp = MdlPages[Lo]; MdlPages[Lo] = MdlPages[Hi]; MdlPages[Hi] = Tmp; }

        //
        // The first advance tags the end sentinel; later advances leave it at
        // the original last slot.
        //
        if (!(Mdl->MdlFlags & MDL_FREE_EXTRA_PTES))
            MdlPages[TotalPages - 1] |= MI_PARKED_PFN_END;

        Mdl->StartVa = (PVOID)((PUCHAR)Mdl->StartVa +
                               ((ULONG_PTR)PageCount << PAGE_SHIFT));
        Mdl->MdlFlags |= MDL_FREE_EXTRA_PTES;
    }

    Mdl->ByteOffset = NewByteOffset & (PAGE_SIZE - 1);
    Mdl->ByteCount -= NumberOfBytes;
    if (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
        Mdl->MappedSystemVa = (PVOID)((PUCHAR)Mdl->MappedSystemVa + NumberOfBytes);

    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
PVOID
NTAPI
MmMapLockedPagesWithReservedMapping(
    _In_ PVOID MappingAddress,
    _In_ ULONG PoolTag,
    _In_ PMDL Mdl,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    PPFN_NUMBER MdlPages, LastPage;
    PFN_COUNT PageCount;
    BOOLEAN IsIoMapping;
    MI_PFN_CACHE_ATTRIBUTE CacheAttribute;
    PMMPTE PointerPte;
    MMPTE TempPte;

    ASSERT(Mdl->ByteCount != 0);

    MappingAddress = (PVOID)ALIGN_DOWN_BY(MappingAddress, 16);

    // Get the list of pages and count
    MdlPages = MmGetMdlPfnArray(Mdl);
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl),
                                               Mdl->ByteCount);
    LastPage = MdlPages + PageCount;

    // Sanity checks
    ASSERT((Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA |
                             MDL_SOURCE_IS_NONPAGED_POOL |
                             MDL_PARTIAL_HAS_BEEN_MAPPED)) == 0);
    ASSERT((Mdl->MdlFlags & (MDL_PAGES_LOCKED | MDL_PARTIAL)) != 0);

    // Get the correct cache type
    IsIoMapping = (Mdl->MdlFlags & MDL_IO_SPACE) != 0;
    CacheAttribute = MiPlatformCacheAttributes[IsIoMapping][CacheType];

    // Get the first PTE we reserved
    ASSERT(MappingAddress);
    PointerPte = MiAddressToPte(MappingAddress) - 2;
    ASSERT(!PointerPte[0].u.Hard.Valid &&
           !PointerPte[1].u.Hard.Valid);

    // Verify that the pool tag matches
    TempPte.u.Long = PoolTag;
    TempPte.u.Hard.Valid = 0;
    if (PointerPte[1].u.Long != TempPte.u.Long)
    {
        KeBugCheckEx(SYSTEM_PTE_MISUSE,
                     PTE_MAPPING_ADDRESS_NOT_OWNED, /* Trying to map an address it does not own */
                     (ULONG_PTR)MappingAddress,
                     PoolTag,
                     PointerPte[1].u.Long);
    }

    // We must have a size, and our helper PTEs must be invalid
    if (PointerPte[0].u.List.NextEntry < 3)
    {
        KeBugCheckEx(SYSTEM_PTE_MISUSE,
                     PTE_MAPPING_ADDRESS_INVALID, /* Trying to map an invalid address */
                     (ULONG_PTR)MappingAddress,
                     PoolTag,
                     (ULONG_PTR)_ReturnAddress());
    }

    // If the mapping isn't big enough, fail
    if ((PointerPte[0].u.List.NextEntry < 2) ||
        (PointerPte[0].u.List.NextEntry - 2 < PageCount))
    {
        DPRINT1("Reserved mapping too small. Need %Iu pages, have %Iu\n",
                PageCount,
                (PointerPte[0].u.List.NextEntry >= 2) ?
                    (SIZE_T)(PointerPte[0].u.List.NextEntry - 2) : 0);
        return NULL;
    }
    // Skip our two helper PTEs
    PointerPte += 2;

    // Get the template
    TempPte = ValidKernelPte;
    switch (CacheAttribute)
    {
        case MiNonCached:
            // Disable caching
            MI_PAGE_DISABLE_CACHE(&TempPte);
            MI_PAGE_WRITE_THROUGH(&TempPte);
            break;

        case MiWriteCombined:
            // Enable write combining
            MI_PAGE_DISABLE_CACHE(&TempPte);
            MI_PAGE_WRITE_COMBINED(&TempPte);
            break;

        default:
            // Nothing to do
            break;
    }

    // Loop all PTEs
    for (; (MdlPages < LastPage) && (*MdlPages != LIST_HEAD); ++MdlPages)
    {
        // Write the PTE
        TempPte.u.Hard.PageFrameNumber = *MdlPages;
        MI_WRITE_VALID_PTE(PointerPte++, TempPte);
    }

    // Mark it as mapped
    ASSERT((Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA) == 0);
    Mdl->MappedSystemVa = MappingAddress;
    Mdl->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;

    // Check if it was partial
    if (Mdl->MdlFlags & MDL_PARTIAL)
    {
        // Write the appropriate flag here too
        Mdl->MdlFlags |= MDL_PARTIAL_HAS_BEEN_MAPPED;
    }

    // Return the mapped address
    return (PVOID)((ULONG_PTR)MappingAddress + Mdl->ByteOffset);
}

/*
 * @implemented
 */
VOID
NTAPI
MmUnmapReservedMapping(
    _In_ PVOID BaseAddress,
    _In_ ULONG PoolTag,
    _In_ PMDL Mdl)
{
    PVOID Base;
    PFN_COUNT PageCount, ExtraPageCount;
    PPFN_NUMBER MdlPages;
    PMMPTE PointerPte;
    MMPTE TempPte;

    // Sanity check
    ASSERT(Mdl->ByteCount != 0);
    ASSERT(BaseAddress > MM_HIGHEST_USER_ADDRESS);

    // Get base and count information
    Base = (PVOID)((ULONG_PTR)Mdl->StartVa + Mdl->ByteOffset);
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Base, Mdl->ByteCount);

    // Sanity checks
    ASSERT((Mdl->MdlFlags & MDL_PARENT_MAPPED_SYSTEM_VA) == 0);
    ASSERT(PageCount != 0);
    ASSERT(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA);

    // Get the first PTE we reserved
    PointerPte = MiAddressToPte(BaseAddress) - 2;
    ASSERT(!PointerPte[0].u.Hard.Valid &&
           !PointerPte[1].u.Hard.Valid);

    // Verify that the pool tag matches
    TempPte.u.Long = PoolTag;
    TempPte.u.Hard.Valid = 0;
    if (PointerPte[1].u.Long != TempPte.u.Long)
    {
        KeBugCheckEx(SYSTEM_PTE_MISUSE,
                     PTE_UNMAPPING_ADDRESS_NOT_OWNED, /* Trying to unmap an address it does not own */
                     (ULONG_PTR)BaseAddress,
                     PoolTag,
                     PointerPte[1].u.Long);
    }

    // We must have a size
    if (PointerPte[0].u.List.NextEntry < 3)
    {
        KeBugCheckEx(SYSTEM_PTE_MISUSE,
                     PTE_MAPPING_ADDRESS_EMPTY, /* Mapping apparently empty */
                     (ULONG_PTR)BaseAddress,
                     PoolTag,
                     (ULONG_PTR)_ReturnAddress());
    }

    // Skip our two helper PTEs
    PointerPte += 2;

    // This should be a resident system PTE - either a valid mapping or a
    // transition PTE parked by MmProtectMdlSystemAddress(PAGE_NOACCESS).
    ASSERT(PointerPte >= MmSystemPtesStart[SystemPteSpace]);
    ASSERT(PointerPte <= MmSystemPtesEnd[SystemPteSpace]);
    ASSERT((PointerPte->u.Hard.Valid == 1) ||
           (PointerPte->u.Trans.Transition == 1));

    // TODO: check the MDL range makes sense with regard to the mapping range
    // TODO: check if any of them are already zero
    // TODO: check if any outside the MDL range are nonzero
    // TODO: find out what to do with extra PTEs

    // Check if the caller wants us to free advanced pages
    if (Mdl->MdlFlags & MDL_FREE_EXTRA_PTES)
    {
        // Get the MDL page array
        MdlPages = MmGetMdlPfnArray(Mdl);

        // MmAdvanceMdl parked the consumed pages after the current span and
        // tagged the last with MI_PARKED_PFN_END; count them by scanning to it.
        for (ExtraPageCount = 0;
             !(MdlPages[PageCount + ExtraPageCount] & MI_PARKED_PFN_END);
             ExtraPageCount++)
        {
            NOTHING;
        }
        ExtraPageCount++;

        // Unlike MmUnmapLockedPages (which anchors PointerPte on the ADVANCED
        // MappedSystemVa and must walk back to the region start), the reserved-
        // mapping base is the ORIGINAL page-0 address - the pool-tag helper PTEs
        // at BaseAddress-2 force that - so PointerPte already points at page 0
        // after the "+= 2" above. We must NOT move PointerPte; we only widen the
        // zeroed span to cover the parked pages so every reserved PTE is released.
        PageCount += ExtraPageCount;

        // Restore the MDL to its pre-advance page span (clear the sentinel,
        // walk StartVa/ByteCount back) so a later MmUnlockPages sees a whole MDL.
        MdlPages[PageCount - 1] &= ~MI_PARKED_PFN_END;
        Mdl->StartVa = (PVOID)((ULONG_PTR)Mdl->StartVa -
                               (ExtraPageCount << PAGE_SHIFT));
        Mdl->ByteCount += (ExtraPageCount << PAGE_SHIFT);
    }

    // Zero the PTEs
    RtlZeroMemory(PointerPte, PageCount * sizeof(MMPTE));

    // Flush the TLB
    KeFlushEntireTb(TRUE, TRUE);

    // Remove flags
    Mdl->MdlFlags &= ~(MDL_MAPPED_TO_SYSTEM_VA |
                       MDL_PARTIAL_HAS_BEEN_MAPPED |
                       MDL_FREE_EXTRA_PTES);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
MmPrefetchPages(IN ULONG NumberOfLists,
                IN PREAD_LIST *ReadLists)
{
    ULONG ListIndex, EntryIndex;
    PREAD_LIST ReadList;
    PVOID Section, MappedBase;
    SIZE_T ViewSize;
    LARGE_INTEGER MaximumSize;
    NTSTATUS Status;
    volatile UCHAR Sink = 0;

    if ((NumberOfLists == 0) || (ReadLists == NULL))
        return STATUS_INVALID_PARAMETER;

    MaximumSize.QuadPart = 0;

    for (ListIndex = 0; ListIndex < NumberOfLists; ListIndex++)
    {
        ReadList = ReadLists[ListIndex];
        if ((ReadList == NULL) ||
            (ReadList->FileObject == NULL) ||
            (ReadList->NumberOfEntries == 0))
        {
            continue;
        }

        Section = NULL;
        Status = MmCreateSection(&Section,
                                 SECTION_MAP_READ,
                                 NULL,
                                 &MaximumSize,
                                 PAGE_READONLY,
                                 SEC_COMMIT,
                                 NULL,
                                 ReadList->FileObject);
        if (!NT_SUCCESS(Status) || (Section == NULL))
            continue;

        MappedBase = NULL;
        ViewSize = 0;
        Status = MmMapViewInSystemSpace(Section, &MappedBase, &ViewSize);
        if (NT_SUCCESS(Status) && (MappedBase != NULL))
        {
            for (EntryIndex = 0; EntryIndex < ReadList->NumberOfEntries; EntryIndex++)
            {
                SIZE_T Offset =
                    (SIZE_T)(ReadList->List[EntryIndex].Alignment & ~((ULONGLONG)PAGE_SIZE - 1));

                if (Offset < ViewSize)
                    Sink += ((volatile UCHAR *)MappedBase)[Offset];
            }

            MmUnmapViewInSystemSpace(MappedBase);
        }

        ObDereferenceObject(Section);
    }

    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
MmProtectMdlSystemAddress(IN PMDL MemoryDescriptorList,
                          IN ULONG NewProtect)
{
    PVOID SystemAddress;
    ULONG_PTR Va;
    PMMPTE PointerPte;
    MMPTE OldPte, TempPte;
    PFN_NUMBER NumberOfPages, PageFrameIndex;
    ULONG ProtectionMask;
    BOOLEAN NoAccess;

    //
    // The MDL must already own a system-space mapping (built by
    // MmMapLockedPagesSpecifyCache / MmGetSystemAddressForMdlSafe). There is
    // nothing to reprotect otherwise.
    //
    if (!(MemoryDescriptorList->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA))
    {
        DPRINT1("MDL %p has no system mapping to protect\n", MemoryDescriptorList);
        return STATUS_NOT_MAPPED_VIEW;
    }

    SystemAddress = MemoryDescriptorList->MappedSystemVa;

    //
    // Large/super-page and direct physical mappings are not described by the
    // ordinary system PTEs we rewrite below, so they cannot be reprotected.
    //
    if (MI_IS_PHYSICAL_ADDRESS(SystemAddress))
    {
        DPRINT1("Cannot protect physical mapping at %p\n", SystemAddress);
        return STATUS_NOT_SUPPORTED;
    }

    //
    // Translate the Win32 protection into the internal protection mask and reject
    // the combinations that are illegal for a system MDL mapping. This mirrors the
    // Windows validation: a valid mask, no stand-alone PAGE_NOCACHE / PAGE_GUARD,
    // no PAGE_WRITECOMBINE next to access bits, and no copy-on-write.
    //
    ProtectionMask = MiMakeProtectionMask(NewProtect);
    if (ProtectionMask == MM_INVALID_PROTECTION)
    {
        DPRINT1("Invalid protection 0x%lx\n", NewProtect);
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    /* PAGE_NOCACHE family (0x08..0x0F) and PAGE_GUARD family (0x10..0x17) */
    if (((ProtectionMask >> 3) == (MM_NOCACHE >> 3)) ||
        ((ProtectionMask >> 3) == (MM_GUARDPAGE >> 3)))
    {
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    /* PAGE_WRITECOMBINE (0x18) must not carry any access bits here */
    if (((ProtectionMask & MM_PROTECT_SPECIAL) == MM_WRITECOMBINE) &&
        ((ProtectionMask & MM_PROTECT_ACCESS) != 0))
    {
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    /* MM_WRITECOPY (5) and MM_EXECUTE_WRITECOPY (7) both have bits 0 and 2 set */
    if ((ProtectionMask & MM_WRITECOPY) == MM_WRITECOPY)
    {
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    //
    // MM_NOACCESS shares its numeric encoding (0x18) with MM_WRITECOMBINE; with no
    // access bits set it means "remove all access".
    //
    NoAccess = (ProtectionMask == MM_NOACCESS);

    //
    // Walk every system PTE that maps the locked pages. The pages stay locked by
    // the MDL, so we only rewrite the protection bits of their system PTEs and
    // never touch PFN share counts; no PFN lock is required.
    //
    Va = (ULONG_PTR)PAGE_ALIGN(SystemAddress);
    NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(SystemAddress,
                                                   MemoryDescriptorList->ByteCount);
    PointerPte = MiAddressToPte(SystemAddress);

    while (NumberOfPages != 0)
    {
        OldPte = *PointerPte;

        //
        // Recover the page frame. A live mapping is a valid hardware PTE; a page
        // previously set to PAGE_NOACCESS by this routine is parked as a
        // transition PTE that still records the frame.
        //
        if (OldPte.u.Hard.Valid)
        {
            PageFrameIndex = PFN_FROM_PTE(&OldPte);
        }
        else if ((OldPte.u.Trans.Transition == 1) && (OldPte.u.Trans.Prototype == 0))
        {
            PageFrameIndex = OldPte.u.Trans.PageFrameNumber;
        }
        else
        {
            //
            // A system MDL PTE must be valid or a transition PTE; anything else
            // means the mapping (or the MDL) has been corrupted.
            //
            KeBugCheckEx(MEMORY_MANAGEMENT,
                         0x1235,
                         (ULONG_PTR)MemoryDescriptorList,
                         (ULONG_PTR)PointerPte,
                         (ULONG_PTR)OldPte.u.Long);
        }

        if (NoAccess)
        {
            //
            // Remove access: park the frame in a transition PTE carrying the
            // MM_NOACCESS protection so a later call can restore it. The frame
            // remains locked by the MDL, so its PFN entry is left untouched.
            //
            MI_MAKE_TRANSITION_PTE(&TempPte, PageFrameIndex, MM_NOACCESS);

#if defined(_M_ARM64)
            //
            // The transition Protection field overlaps the hardware MAIR index
            // (CacheType[1:0] + OsAvailable2), so MI_MAKE_TRANSITION_PTE cannot
            // keep the memory type in place. Stash the original AttrIndx in the
            // transition spare bits so the NOACCESS->valid round-trip restores the
            // exact type (a non-cached / write-combined mapping would otherwise
            // silently come back write-back). These transition PTEs live in
            // system-PTE space (not the PFN transition list) and are read back
            // only here, so reusing Spare/OnStandbyLookaside is safe.
            //
            if (OldPte.u.Hard.Valid)
            {
                TempPte.u.Trans.Spare = OldPte.u.Hard.CacheType;
                TempPte.u.Trans.OnStandbyLookaside = OldPte.u.Hard.OsAvailable2;
            }
            else
            {
                /* Already parked - carry the previously-stashed AttrIndx forward. */
                TempPte.u.Trans.Spare = OldPte.u.Trans.Spare;
                TempPte.u.Trans.OnStandbyLookaside = OldPte.u.Trans.OnStandbyLookaside;
            }
#endif

            MI_WRITE_INVALID_PTE(PointerPte, TempPte);

            /* Only a previously-valid translation can be cached in the TLB */
            if (OldPte.u.Hard.Valid)
            {
                KeInvalidateTlbEntry((PVOID)Va);
            }
        }
        else
        {
            //
            // Build a fresh valid kernel PTE with the requested access/execute
            // rights for the same frame.
            //
            MI_MAKE_HARDWARE_PTE_KERNEL(&TempPte,
                                        PointerPte,
                                        ProtectionMask,
                                        PageFrameIndex);

            //
            // MI_MAKE_HARDWARE_PTE_KERNEL resets the memory type to write-back, so
            // transplant the cache attribute that MmMapLockedPagesSpecifyCache
            // originally installed (e.g. non-cached or write-combined framebuffer
            // mappings). Changing cacheability under a live mapping would corrupt
            // the page, so we keep the original memory type and only change access.
            // The cache fields live at different PTE bits per architecture.
            //
#if defined(_M_ARM64)
            /* ARM64 holds the MAIR AttrIndx in CacheType[1:0] + OsAvailable2[2]. */
            if (OldPte.u.Hard.Valid)
            {
                /* valid->valid: the live PTE still carries the real AttrIndx. */
                TempPte.u.Hard.CacheType = OldPte.u.Hard.CacheType;
                TempPte.u.Hard.OsAvailable2 = OldPte.u.Hard.OsAvailable2;
            }
            else
            {
                /* NOACCESS->valid: recover the AttrIndx stashed at park time (the
                   transition Protection field overlapped the live cache bits). */
                TempPte.u.Hard.CacheType = OldPte.u.Trans.Spare;
                TempPte.u.Hard.OsAvailable2 = OldPte.u.Trans.OnStandbyLookaside;
            }
#elif defined(_M_AMD64) || defined(_M_IX86)
            /* x86/x64 hold the memory type in PCD (CacheDisable) + PWT (WriteThrough).
               Note: the NOACCESS->valid round-trip reads these from a transition PTE
               (same limitation as the ARM64 path before the spare-bit stash); not
               addressed here as this is ARM64 parity work and is untested on x86. */
            TempPte.u.Hard.CacheDisable = OldPte.u.Hard.CacheDisable;
            TempPte.u.Hard.WriteThrough = OldPte.u.Hard.WriteThrough;
#endif

            if (OldPte.u.Hard.Valid)
            {
                //
                // In-place permission change of a live mapping (same frame). The
                // valid-to-valid update needs an explicit TLB invalidation.
                //
                MI_UPDATE_VALID_PTE(PointerPte, TempPte);
                KeInvalidateTlbEntry((PVOID)Va);
            }
            else
            {
                //
                // Re-validate a page that had been parked at PAGE_NOACCESS. The
                // invalid-to-valid write needs no TLB shootdown (no stale entry
                // was cached); the write helper publishes the new translation.
                //
                MI_WRITE_VALID_PTE(PointerPte, TempPte);
            }
        }

        /* Advance to the next page */
        PointerPte++;
        Va += PAGE_SIZE;
        NumberOfPages--;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief
 * Probes and locks virtual pages in memory for the specified process.
 *
 * @param[in,out] MemoryDescriptorList
 * Memory Descriptor List (MDL) containing the buffer to be probed and locked.
 *
 * @param[in] Process
 * The process for which the buffer should be probed and locked.
 *
 * @param[in] AccessMode
 * Access mode for probing the pages. Can be KernelMode or UserMode.
 *
 * @param[in] LockOperation
 * The type of the probing and locking operation. Can be IoReadAccess, IoWriteAccess or IoModifyAccess.
 *
 * @return
 * Nothing.
 *
 * @see MmProbeAndLockPages
 *
 * @remarks Must be called at IRQL <= APC_LEVEL
 */
_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
MmProbeAndLockProcessPages(
    _Inout_ PMDL MemoryDescriptorList,
    _In_ PEPROCESS Process,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    KAPC_STATE ApcState;
    BOOLEAN IsAttached = FALSE;

    if (Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        IsAttached = TRUE;
    }

    /* Protect in try/finally to ensure we detach even if MmProbeAndLockPages() throws an exception */
    _SEH2_TRY
    {
        MmProbeAndLockPages(MemoryDescriptorList, AccessMode, Operation);
    }
    _SEH2_FINALLY
    {
        if (IsAttached)
            KeUnstackDetachProcess(&ApcState);
    }
    _SEH2_END;
}

/*
 * @unimplemented
 */
VOID
NTAPI
MmProbeAndLockSelectedPages(IN OUT PMDL MemoryDescriptorList,
                            IN LARGE_INTEGER PageList[],
                            IN KPROCESSOR_MODE AccessMode,
                            IN LOCK_OPERATION Operation)
{
    UNIMPLEMENTED;
}

/*
 * @unimplemented
 */
VOID
NTAPI
MmMapMemoryDumpMdl(IN PMDL Mdl)
{
    UNIMPLEMENTED;
}

/* EOF */
