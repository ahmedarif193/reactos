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

#define MODULE_INVOLVED_IN_ARM3
#include "ARM3/miarm.h"

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
extern NTSTATUS MiRosTrimCache(ULONG Target, ULONG Priority, PULONG NrFreed);

/* PRIVATE FUNCTIONS *********************************************************/

//
// Helper function to create initial memory areas.
// The created area is always read/write.
//
CODE_SEG("INIT")
VOID
NTAPI
MiCreateArm3StaticMemoryArea(PVOID BaseAddress, SIZE_T Size, BOOLEAN Executable)
{
    const ULONG Protection = Executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    PVOID pBaseAddress = BaseAddress;
    PMEMORY_AREA MArea;
    NTSTATUS Status;

    if (Size == 0)
    {
        DPRINT1("MiCreateArm3StaticMemoryArea: ignoring zero-sized range at %p\n",
                BaseAddress);
        return;
    }

    const ULONG_PTR Base = (ULONG_PTR)BaseAddress;
    const ULONG_PTR End = Base + Size - 1;

    /* Guard against inverted or wrapping spans */
    if (End < Base)
    {
        DPRINT1("MiCreateArm3StaticMemoryArea: span overflow Base=%p Size=%Ix\n",
                BaseAddress,
                Size);
        return;
    }

    Status = MmCreateMemoryArea(MmGetKernelAddressSpace(),
                                MEMORY_AREA_OWNED_BY_ARM3 | MEMORY_AREA_STATIC,
                                &pBaseAddress,
                                Size,
                                Protection,
                                &MArea,
                                0,
                                PAGE_SIZE);
    ASSERT(Status == STATUS_SUCCESS);
    // TODO: Perhaps it would be  prudent to bugcheck here, not only assert?
}

CODE_SEG("INIT")
VOID
NTAPI
MiInitSystemMemoryAreas(VOID)
{
    //
    // Create all the static memory areas.
    //

    MmLockAddressSpace(MmGetKernelAddressSpace());

#ifdef _M_AMD64
    // Reserved range FFFF800000000000 - FFFFF68000000000
    MiCreateArm3StaticMemoryArea((PVOID)MI_REAL_SYSTEM_RANGE_START, PTE_BASE - MI_REAL_SYSTEM_RANGE_START, FALSE);
#endif /* _M_AMD64 */

    // The loader mappings. The only Executable area.
    MiCreateArm3StaticMemoryArea((PVOID)KSEG0_BASE, MmBootImageSize, TRUE);

    // The PTE base
    MiCreateArm3StaticMemoryArea((PVOID)PTE_BASE, PTE_TOP - PTE_BASE + 1, FALSE);

    // Hyperspace
    MiCreateArm3StaticMemoryArea((PVOID)HYPER_SPACE, HYPER_SPACE_END - HYPER_SPACE + 1, FALSE);

    // Protect the PFN database
    MiCreateArm3StaticMemoryArea(MmPfnDatabase, (MxPfnAllocation << PAGE_SHIFT), FALSE);

    // ReactOS requires a memory area to keep the initial NP area off-bounds
    MiCreateArm3StaticMemoryArea(MmNonPagedPoolStart, MmSizeOfNonPagedPoolInBytes, FALSE);

    // System PTE space
    MiCreateArm3StaticMemoryArea(MmNonPagedSystemStart, (MmNumberOfSystemPtes + 1) * PAGE_SIZE, FALSE);

    // Nonpaged pool expansion space
    MiCreateArm3StaticMemoryArea(MmNonPagedPoolExpansionStart, (ULONG_PTR)MmNonPagedPoolEnd - (ULONG_PTR)MmNonPagedPoolExpansionStart, FALSE);

    // System view space
    MiCreateArm3StaticMemoryArea(MiSystemViewStart, MmSystemViewSize, FALSE);

    // Session space
    MiCreateArm3StaticMemoryArea(MmSessionBase, (ULONG_PTR)MiSessionSpaceEnd - (ULONG_PTR)MmSessionBase, FALSE);

    // Paged pool
    MiCreateArm3StaticMemoryArea(MmPagedPoolStart, MmSizeOfPagedPoolInBytes, FALSE);

    // Debugger mapping
    MiCreateArm3StaticMemoryArea(MI_DEBUG_MAPPING, PAGE_SIZE, FALSE);

#if defined(_X86_)
    // Reserved HAL area (includes KUSER_SHARED_DATA and KPCR)
    MiCreateArm3StaticMemoryArea((PVOID)MM_HAL_VA_START, MM_HAL_VA_END - MM_HAL_VA_START + 1, FALSE);
#else /* _X86_ */
#if !defined(_M_AMD64) && !defined(_M_ARM64)
    // KPCR, one page per CPU. Only for 32-bit kernel.
    MiCreateArm3StaticMemoryArea(PCR, PAGE_SIZE * KeNumberProcessors, FALSE);
#endif /* _M_AMD64 */

    // KUSER_SHARED_DATA
    MiCreateArm3StaticMemoryArea((PVOID)KI_USER_SHARED_DATA, PAGE_SIZE, FALSE);

#if defined(_M_ARM64) || defined(__aarch64__)
    //
    // ARM64: Pre-map the PTE for KI_USER_SHARED_DATA in the self-map structure.
    // This ensures that when Phase 1 tries to read the PTE at line 325, the
    // self-map entry is already accessible and won't trigger a page fault while
    // holding the paged pool mutex (IRQL 2).
    //
    {
        PMMPTE SharedDataPte = MiAddressToPte((PVOID)KI_USER_SHARED_DATA);
        DPRINT1("[MM] Phase 0: Pre-mapping PTE for KI_USER_SHARED_DATA (%p) at PTE address %p\n",
                (PVOID)KI_USER_SHARED_DATA, SharedDataPte);
        MiArm64MapAliasForPointer(SharedDataPte);
        DPRINT1("[MM] Phase 0: PTE pre-mapping complete\n");
    }
#endif
#endif /* _X86_ */

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
            KSEG0_BASE,
            (ULONG_PTR)KSEG0_BASE + MmBootImageSize,
            "Boot Loaded Image");
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
    DPRINT1("          0x%p - 0x%p\t%s\n",
            MmNonPagedSystemStart, MmNonPagedPoolExpansionStart,
            "System PTE Space");
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
    extern KGUARDED_MUTEX MmPagedPoolMutex;
    PMMPTE PointerPte;
    MMPTE TempPte = ValidKernelPte;
    PFN_NUMBER PageFrameNumber;
    PLIST_ENTRY ListEntry;
    PLDR_DATA_TABLE_ENTRY DataTableEntry;

    /* ARM64: Check mutex state at entry to Phase 1 */
    DPRINT1("[MM] MmInitSystem Phase %lu ENTRY: MmPagedPoolMutex @ %p, Type=%02x, Count=%ld\n",
            Phase,
            &MmPagedPoolMutex,
            (ULONG)MmPagedPoolMutex.Gate.Header.Type,
            MmPagedPoolMutex.Count);

    DPRINT1("[MM] Phase 1: After DPRINT1, before ASSERT\n");

    /* Initialize the kernel address space */
    ASSERT(Phase == 1);

    DPRINT1("[MM] Phase 1: After ASSERT, before event init\n");

#ifdef NEWCC
    InitializeListHead(&MiSegmentList);
    ExInitializeFastMutex(&MiGlobalPageOperation);
    KeInitializeEvent(&MmWaitPageEvent, SynchronizationEvent, FALSE);
    // Until we're fully demand paged, we can do things the old way through
    // the balance manager
    // CcInitView will override this...
    MmInitializeMemoryConsumer(MC_CACHE, MiRosTrimCache);
#else
    KeInitializeEvent(&MmWaitPageEvent, SynchronizationEvent, FALSE);
#endif

    DPRINT1("[MM] Phase 1: After event init, before setting address space\n");

    MmKernelAddressSpace = &PsIdleProcess->Vm;

    DPRINT1("[MM] Phase 1: After setting address space, before MiInitSystemMemoryAreas\n");

    /* Intialize system memory areas */
    MiInitSystemMemoryAreas();

    DPRINT1("[MM] Phase 1: After MiInitSystemMemoryAreas, before MiDbgDumpAddressSpace\n");

    /* Dump the address space */
    MiDbgDumpAddressSpace();

    DPRINT1("[MM] Phase 1: After MiDbgDumpAddressSpace, before MmInitGlobalKernelPageDirectory\n");

    MmInitGlobalKernelPageDirectory();
    DPRINT1("[MM] Phase 1: After MmInitGlobalKernelPageDirectory\n");

    MmInitializeMemoryConsumer(MC_USER, MmTrimUserMemory);
    DPRINT1("[MM] Phase 1: After MmInitializeMemoryConsumer\n");

    MmInitializeRmapList();
    DPRINT1("[MM] Phase 1: After MmInitializeRmapList\n");

    MmInitSectionImplementation();
    DPRINT1("[MM] Phase 1: After MmInitSectionImplementation\n");

    MmInitPagingFile();
    DPRINT1("[MM] Phase 1: After MmInitPagingFile, before MiInitializePoolEvents\n");

    /*
     * ARM64: Initialize pool events BEFORE the first paged pool allocation.
     * MiInitializePoolEvents() must be called before any code that tries to
     * acquire the paged pool mutex, because it initializes the mutex's gate
     * and pool work item structures. We cannot call MiInitializeMemoryEvents()
     * here because it itself allocates from paged pool.
     */
    MiInitializePoolEvents();

    DPRINT1("[MM] Phase 1: After MiInitializePoolEvents\n");

#if defined(_M_ARM64)
    /* CHECKPOINT: Before first paged pool allocation (SharedUserDataPte) */
    MiArm64CheckSystemViewSpacePte("Before SharedUserDataPte allocation");
#endif

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

#if defined(_M_ARM64)
    /* CHECKPOINT: After first paged pool allocation (SharedUserDataPte) */
    MiArm64CheckSystemViewSpacePte("After SharedUserDataPte allocation");
#endif

    //
    // Now get the PTE for shared data, and read the PFN that holds it
    // (ARM64: The PTE self-map entry was pre-populated in Phase 0 to avoid
    // page faults while holding the paged pool mutex at IRQL 2)
    //
    PointerPte = MiAddressToPte((PVOID)KI_USER_SHARED_DATA);
    DPRINT1("[MM] Phase 1: Got PTE for KI_USER_SHARED_DATA at %p\n", PointerPte);
    ASSERT(PointerPte->u.Hard.Valid == 1);
    DPRINT1("[MM] Phase 1: PTE is valid, getting PFN\n");
    PageFrameNumber = PFN_FROM_PTE(PointerPte);
    DPRINT1("[MM] Phase 1: Got PFN %I64x\n", (ULONG64)PageFrameNumber);

    /* Build the PTE and write it */
    DPRINT1("[MM] Phase 1: Building shared user data PTE\n");
    MI_MAKE_HARDWARE_PTE_KERNEL(&TempPte,
                                PointerPte,
                                MM_READONLY,
                                PageFrameNumber);
    *MmSharedUserDataPte = TempPte;
    DPRINT1("[MM] Phase 1: Shared user data PTE written\n");

    /* Initialize session working set support */
    DPRINT1("[MM] Phase 1: Calling MiInitializeSessionWsSupport\n");
    MiInitializeSessionWsSupport();
    DPRINT1("[MM] Phase 1: MiInitializeSessionWsSupport complete\n");

    /* Setup session IDs */
    DPRINT1("[MM] Phase 1: Calling MiInitializeSessionIds\n");
    MiInitializeSessionIds();
    DPRINT1("[MM] Phase 1: MiInitializeSessionIds complete\n");

#if defined(_M_ARM64)
    /* CHECKPOINT: Before MiInitializeMemoryEvents */
    MiArm64CheckSystemViewSpacePte("Before MiInitializeMemoryEvents");
#endif

    /* Setup the memory threshold events */
    DPRINT1("[MM] Phase 1: Calling MiInitializeMemoryEvents\n");
    if (!MiInitializeMemoryEvents()) return FALSE;
    DPRINT1("[MM] Phase 1: MiInitializeMemoryEvents complete\n");

#if defined(_M_ARM64)
    /* CHECKPOINT: After MiInitializeMemoryEvents (before MiInitBalancerThread) */
    MiArm64CheckSystemViewSpacePte("After MiInitializeMemoryEvents");
#endif

    /*
     * Unmap low memory
     */
    MiInitBalancerThread();

#if defined(_M_ARM64)
    /* CHECKPOINT: After MiInitBalancerThread */
    MiArm64CheckSystemViewSpacePte("After MiInitBalancerThread");
#endif

    /* Initialize the balance set manager */
    MmInitBsmThread();

#if defined(_M_ARM64)
    /* CHECKPOINT: After MmInitBsmThread */
    MiArm64CheckSystemViewSpacePte("After MmInitBsmThread");
#endif

    /* Loop the boot loaded images (under lock) */
    ExAcquireResourceExclusiveLite(&PsLoadedModuleResource, TRUE);
#if defined(_M_ARM64)
    DPRINT1("[MM] Phase 1: About to process boot loaded modules. PsLoadedModuleList @ %p, Flink=%p\n",
            &PsLoadedModuleList, PsLoadedModuleList.Flink);
#endif
    for (ListEntry = PsLoadedModuleList.Flink;
         ListEntry != &PsLoadedModuleList;
         ListEntry = ListEntry->Flink)
    {
        /* Get the data table entry */
        DataTableEntry = CONTAINING_RECORD(ListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

#if defined(_M_ARM64)
        DPRINT1("[MM] Processing module: Base=%p Name=%wZ ListEntry=%p Flink=%p Blink=%p\n",
                DataTableEntry->DllBase,
                &DataTableEntry->BaseDllName,
                ListEntry,
                ListEntry->Flink,
                ListEntry->Blink);
#endif

        /* Set up the image protection */
        MiWriteProtectSystemImage(DataTableEntry->DllBase);

#if defined(_M_ARM64)
        DPRINT1("[MM] Finished module: Base=%p Name=%wZ Next ListEntry will be=%p\n",
                DataTableEntry->DllBase,
                &DataTableEntry->BaseDllName,
                ListEntry->Flink);
#endif
    }
#if defined(_M_ARM64)
    DPRINT1("[MM] Phase 1: Finished processing boot loaded modules.\n");
#endif
    ExReleaseResourceLite(&PsLoadedModuleResource);

    return TRUE;
}
