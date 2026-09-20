/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/mm/mminit.c
 * PURPOSE:         Memory Manager Initialization
 * PROGRAMMERS:
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_VMM
#include <vmm/vmm.h>

/* GLOBALS *******************************************************************/

BOOLEAN Mm64BitPhysicalAddress = FALSE;
ULONG MmReadClusterSize;
//
// 0 | 1 is on/off paging, 2 is undocumented
//
UCHAR MmDisablePagingExecutive = 1; // Forced to off
PMMPTE MmSharedUserDataPte;
PMMSUPPORT MmKernelAddressSpace;

extern KEVENT MmWaitPageEvent;
extern FAST_MUTEX MiGlobalPageOperation;
extern LIST_ENTRY MiSegmentList;

/* PRIVATE FUNCTIONS *********************************************************/

//
// Helper function to create initial memory areas.
// The created area is always read/write.
//
CODE_SEG("INIT")
VOID
NTAPI
MiCreateVmmStaticMemoryArea(PVOID BaseAddress, SIZE_T Size, BOOLEAN Executable)
{
    const ULONG Protection = Executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    PVOID pBaseAddress = BaseAddress;
    PMEMORY_AREA MArea;
    NTSTATUS Status;

    Status = MmCreateMemoryArea(MmGetKernelAddressSpace(),
                                MEMORY_AREA_OWNED_BY_VMM | MEMORY_AREA_STATIC,
                                &pBaseAddress,
                                Size,
                                Protection,
                                &MArea,
                                0,
                                PAGE_SIZE);
    ASSERT(Status == STATUS_SUCCESS);
    // TODO: Perhaps it would be  prudent to bugcheck here, not only assert?
}

#if defined(_M_AMD64) || defined(_M_ARM64)
CODE_SEG("INIT")
static
VOID
MiSize64BitPagedPool(VOID)
{
    /*
     * Not (meaningfully) configured through the registry: size it the way the
     * x86 path does — twice the nonpaged pool maximum, which itself scales
     * with physical memory. Leaving the 32MB floor here starved every GDI
     * bitmap on large desktops (each DDB's pixel buffer is one contiguous
     * paged pool allocation, ~8MB for a 1080p-sized window surface).
     * The VA is reserved only; pages are faulted in on demand by the paged
     * pool expansion path.
     */
    if (MmSizeOfPagedPoolInBytes <= MI_MIN_INIT_PAGED_POOLSIZE)
    {
        MmSizeOfPagedPoolInBytes = 2 * MmMaximumNonPagedPoolInBytes;
    }

    if (MmSizeOfPagedPoolInBytes < MI_MIN_INIT_PAGED_POOLSIZE)
    {
        MmSizeOfPagedPoolInBytes = MI_MIN_INIT_PAGED_POOLSIZE;
    }

    if (MmSizeOfPagedPoolInBytes > MiSystemVaRegions[AssignedRegionPagedPool].NumberOfBytes)
    {
        MmSizeOfPagedPoolInBytes = MiSystemVaRegions[AssignedRegionPagedPool].NumberOfBytes;
    }

    MmSizeOfPagedPoolInBytes = ALIGN_UP_BY(MmSizeOfPagedPoolInBytes, PDE_MAPPED_VA);
    MmSizeOfPagedPoolInPages = MmSizeOfPagedPoolInBytes >> PAGE_SHIFT;
    MmPagedPoolEnd = Add2Ptr(MmPagedPoolStart, MmSizeOfPagedPoolInBytes - 1);
}

CODE_SEG("INIT")
static
VOID
MiReserveAssignedSystemVaRegions(VOID)
{
    PMI_SYSTEM_VA_ASSIGNMENT Region;
    ULONG i;

    for (i = 0; i < ARRAYSIZE(MiSystemVaRegions); i++)
    {
        Region = &MiSystemVaRegions[i];

        if (Region->BaseAddress == NULL)
        {
            continue;
        }

#ifdef _M_AMD64
        if (i == AssignedRegionSystemPtes)
        {
            continue;
        }
#endif

        if ((i == AssignedRegionSystemCache) || (i == AssignedRegionNonPagedPool) || (i == AssignedRegionPagedPool))
        {
            continue;
        }

        MiCreateVmmStaticMemoryArea(Region->BaseAddress, Region->NumberOfBytes, FALSE);
    }
}
#endif

CODE_SEG("INIT")
VOID
NTAPI
MiInitSystemMemoryAreas(VOID)
{
    //
    // Create all the static memory areas.
    //

    MmLockAddressSpace(MmGetKernelAddressSpace());

#if defined(_M_AMD64) || defined(_M_ARM64)
    MiSize64BitPagedPool();
    MiReserveAssignedSystemVaRegions();
    MiCreateVmmStaticMemoryArea((PVOID)KI_USER_SHARED_DATA, PAGE_SIZE, FALSE);
    MiCreateVmmStaticMemoryArea(MmNonPagedPoolStart, MmMaximumNonPagedPoolInBytes, FALSE);
    MiCreateVmmStaticMemoryArea(MmPagedPoolStart, MmSizeOfPagedPoolInBytes, FALSE);
#ifdef _M_AMD64
    MiCreateVmmStaticMemoryArea(MmSystemPteSpaceStart, (MmNumberOfSystemPtes + 1) * PAGE_SIZE, FALSE);
#endif
#ifdef _M_ARM64
    MiCreateVmmStaticMemoryArea((PVOID)KSEG0_BASE, max(((ULONG64)MmHighestPhysicalPage + 1) << PAGE_SHIFT, PXE_MAPPED_VA), FALSE);
    MiCreateVmmStaticMemoryArea((PVOID)MI_SYSTEM_SPACE_START, 128 * _1GB, FALSE);
    MiCreateVmmStaticMemoryArea((PVOID)MI_ARM64_PHYS_MAP_BASE, 0ULL - MI_ARM64_PHYS_MAP_BASE, FALSE);
#else
    MiCreateVmmStaticMemoryArea((PVOID)MM_HAL_VA_START, MM_HAL_VA_END - MM_HAL_VA_START + 1, FALSE);
#endif
#else /* _M_AMD64 || _M_ARM64 */

    // The loader mappings. The only Executable area.
    MiCreateVmmStaticMemoryArea((PVOID)KSEG0_BASE, MmBootImageSize, TRUE);

    // The PTE base
    MiCreateVmmStaticMemoryArea((PVOID)PTE_BASE, PTE_TOP - PTE_BASE + 1, FALSE);

    // Hyperspace
    MiCreateVmmStaticMemoryArea((PVOID)HYPER_SPACE, HYPER_SPACE_END - HYPER_SPACE + 1, FALSE);

    // Protect the PFN database
    MiCreateVmmStaticMemoryArea(MmPfnDatabase, (MxPfnAllocation << PAGE_SHIFT), FALSE);

    // ReactOS requires a memory area to keep the initial NP area off-bounds
    MiCreateVmmStaticMemoryArea(MmNonPagedPoolStart, MmSizeOfNonPagedPoolInBytes, FALSE);

    // System PTE space
    MiCreateVmmStaticMemoryArea(MmSystemPteSpaceStart, (MmNumberOfSystemPtes + 1) * PAGE_SIZE, FALSE);

    // Nonpaged pool expansion space
    MiCreateVmmStaticMemoryArea(MmNonPagedPoolExpansionStart, (ULONG_PTR)MmNonPagedPoolEnd - (ULONG_PTR)MmNonPagedPoolExpansionStart, FALSE);

    // System view space
    MiCreateVmmStaticMemoryArea(MiSystemViewStart, MmSystemViewSize, FALSE);

    // Session space
    MiCreateVmmStaticMemoryArea(MmSessionBase, (ULONG_PTR)MiSessionSpaceEnd - (ULONG_PTR)MmSessionBase, FALSE);

    // Paged pool
    MiCreateVmmStaticMemoryArea(MmPagedPoolStart, MmSizeOfPagedPoolInBytes, FALSE);

    // Debugger mapping
    MiCreateVmmStaticMemoryArea(MI_DEBUG_MAPPING, PAGE_SIZE, FALSE);

#if defined(_X86_)
    // Reserved HAL area (includes KUSER_SHARED_DATA and KPCR)
    MiCreateVmmStaticMemoryArea((PVOID)MM_HAL_VA_START, MM_HAL_VA_END - MM_HAL_VA_START + 1, FALSE);
#else /* _X86_ */
    // KPCR, one page per CPU. Only for 32-bit kernel.
    MiCreateVmmStaticMemoryArea(PCR, PAGE_SIZE * KeNumberProcessors, FALSE);

    // KUSER_SHARED_DATA
    MiCreateVmmStaticMemoryArea((PVOID)KI_USER_SHARED_DATA, PAGE_SIZE, FALSE);
#endif /* _X86_ */
#endif /* _M_AMD64 || _M_ARM64 */

    MmUnlockAddressSpace(MmGetKernelAddressSpace());
}

CODE_SEG("INIT")
VOID
NTAPI
MiDbgDumpAddressSpace(VOID)
{
    //
    // Print the memory layout
    //
    DPRINT1("          0x%p - 0x%p\t%s\n",
#ifdef _M_ARM64
            (PVOID)(ULONG_PTR)MI_ARM64_BOOT_IMAGE_BASE,
            (PVOID)(ULONG_PTR)(MI_ARM64_BOOT_IMAGE_BASE + MmBootImageSize),
#else
            KSEG0_BASE,
            (ULONG_PTR)KSEG0_BASE + MmBootImageSize,
#endif
            "Boot Loaded Image");
#ifdef _M_IX86
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MmSystemPteSpaceStart,
            (PVOID)((ULONG_PTR)MmSystemPteSpaceStart +
                    (MmNumberOfSystemPtes + 1) * PAGE_SIZE),
            "System PTE Space");
#endif
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MmPfnDatabase,
            (ULONG_PTR)MmPfnDatabase + (MxPfnAllocation << PAGE_SHIFT),
            "PFN Database");
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MmNonPagedPoolStart,
            (ULONG_PTR)MmNonPagedPoolStart + MmSizeOfNonPagedPoolInBytes,
            "ARM3 Non Paged Pool");
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MiSystemViewStart,
            (ULONG_PTR)MiSystemViewStart + MmSystemViewSize,
            "System View Space");
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MmSessionBase,
            MiSessionSpaceEnd,
            "Session Space");
    DPRINT1("          0x%p - 0x%p\t%s\n",
            PTE_BASE, PTE_TOP,
            "Page Tables");
    DPRINT1("          0x%p - 0x%p\t%s\n",
            PDE_BASE, PDE_TOP,
            "Page Directories");
    DPRINT1("          0x%p - 0x%p\t%s\n",
            HYPER_SPACE, HYPER_SPACE_END,
            "Hyperspace");
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MmSystemCacheStart, MmSystemCacheEnd,
            "System Cache");
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MmPagedPoolStart,
            (ULONG_PTR)MmPagedPoolStart + MmSizeOfPagedPoolInBytes,
            "ARM3 Paged Pool");
#ifndef _M_IX86
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MmSystemPteSpaceStart,
#ifdef _M_ARM64
            (PVOID)((ULONG_PTR)MmSystemPteSpaceStart +
                    ((ULONG_PTR)MmNumberOfSystemPtes << PAGE_SHIFT)),
#else
            (PVOID)((ULONG_PTR)MmSystemPteSpaceStart +
                    ((MmNumberOfSystemPtes + 1) * PAGE_SIZE)),
#endif
            "System PTE Space");
#endif
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MmNonPagedPoolExpansionStart, MmNonPagedPoolEnd,
            "Non Paged Pool Expansion PTE Space");
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
MmInitBsmThread(VOID)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE ThreadHandle;

    /* Create the thread */
    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    Status = PsCreateSystemThread(&ThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  &ObjectAttributes,
                                  NULL,
                                  NULL,
                                  KeBalanceSetManager,
                                  NULL);

    /* Close the handle and return status */
    ZwClose(ThreadHandle);
    return Status;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
MmInitSystem(IN ULONG Phase,
             IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    extern MMPTE ValidKernelPte;
    PMMPTE PointerPte;
    MMPTE TempPte = ValidKernelPte;
    PFN_NUMBER PageFrameNumber;
    PLIST_ENTRY ListEntry;
    PLDR_DATA_TABLE_ENTRY DataTableEntry;

    /* Initialize the kernel address space */
    ASSERT(Phase == 1);

    KeInitializeEvent(&MmWaitPageEvent, SynchronizationEvent, FALSE);

    MmKernelAddressSpace = &PsIdleProcess->Vm;

    /* Intialize system memory areas */
    MiInitSystemMemoryAreas();

    /* Dump the address space */
    MiDbgDumpAddressSpace();

    MmInitGlobalKernelPageDirectory();
    MmInitializeMemoryConsumer(MC_USER, MmTrimUserMemory);
    MmInitializeRmapList();
    MmInitSectionImplementation();
    MmInitPagingFile();

    //
    // Create a PTE to double-map the shared data section. We allocate it
    // from paged pool so that we can't fault when trying to touch the PTE
    // itself (to map it), since paged pool addresses will already be mapped
    // by the fault handler.
    //
    MmSharedUserDataPte = ExAllocatePoolWithTag(PagedPool,
                          sizeof(MMPTE),
                          TAG_MM);
    if (!MmSharedUserDataPte) return FALSE;

    //
    // Now get the PTE for shared data, and read the PFN that holds it
    //
    PointerPte = MiAddressToPte((PVOID)KI_USER_SHARED_DATA);
    ASSERT(PointerPte->u.Hard.Valid == 1);
    PageFrameNumber = PFN_FROM_PTE(PointerPte);

    /* Build the PTE and write it */
    MI_MAKE_HARDWARE_PTE_KERNEL(&TempPte,
                                PointerPte,
                                MM_READONLY,
                                PageFrameNumber);
    *MmSharedUserDataPte = TempPte;

    /* Initialize session working set support */
    MiInitializeSessionWsSupport();

    /* Setup session IDs */
    MiInitializeSessionIds();

    /* Setup the memory threshold events */
    if (!MiInitializeMemoryEvents()) return FALSE;

    /*
     * Unmap low memory
     */
    MiInitBalancerThread();

    /* Initialize the balance set manager */
    MmInitBsmThread();

    /* Loop the boot loaded images (under lock) */
    ExAcquireResourceExclusiveLite(&PsLoadedModuleResource, TRUE);
    for (ListEntry = PsLoadedModuleList.Flink;
         ListEntry != &PsLoadedModuleList;
         ListEntry = ListEntry->Flink)
    {
        /* Get the data table entry */
        DataTableEntry = CONTAINING_RECORD(ListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        /* Set up the image protection */
        MiWriteProtectSystemImage(DataTableEntry->DllBase);
    }
    ExReleaseResourceLite(&PsLoadedModuleResource);

    return TRUE;
}
