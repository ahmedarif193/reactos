/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntinit.c
 * PURPOSE:     NT memory manager initialization
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>
#include <nvs/pool/include/poolenv.h>
#include <nvs/pool/include/poolva.h>
#include <nvs/pool/include/poolback.h>

#define MI_BOOT_RESIDENT_POOL_BYTES  (4 * _1MB)
#define MI_MINIMUM_PHYSICAL_PAGES    ((24 * _1MB) >> PAGE_SHIFT)

extern POOL_KM_LAYOUT MiPoolLayout;
extern BOOLEAN MiPoolReady;
extern KMUTANT MmSystemLoadLock;

VOID NTAPI InitializePool(IN POOL_TYPE PoolType, IN ULONG Threshold);

PKUSER_SHARED_DATA MmWriteableSharedUserData = (PKUSER_SHARED_DATA)KI_USER_SHARED_DATA;

static MI_PROCESS MiSystemProcess;
static ULONG64 MiBootRootFrame;
static BOOLEAN MiUserHalfCleared;

static
BOOLEAN
MiMemoryTypeIsFree(
    _In_ TYPE_OF_MEMORY MemoryType)
{
    return (BOOLEAN)(MemoryType == LoaderFree || MemoryType == LoaderLoadedProgram ||
                     MemoryType == LoaderFirmwareTemporary || MemoryType == LoaderOsloaderStack);
}

static
BOOLEAN
MiMemoryTypeIsInvisible(
    _In_ TYPE_OF_MEMORY MemoryType)
{
    return (BOOLEAN)(MemoryType == LoaderFirmwarePermanent || MemoryType == LoaderSpecialMemory ||
                     MemoryType == LoaderHALCachedMemory || MemoryType == LoaderBBTMemory ||
                     MemoryType == LoaderBad);
}

static
PMEMORY_ALLOCATION_DESCRIPTOR
MiScanMemoryDescriptors(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Largest = NULL;
    PLIST_ENTRY Entry;

    MmNumberOfPhysicalPages = 0;
    MmLowestPhysicalPage = (PFN_NUMBER)-1;
    MmHighestPhysicalPage = 0;

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR,
                                                                     ListEntry);

        if (Descriptor->PageCount == 0 || MiMemoryTypeIsInvisible(Descriptor->MemoryType))
            continue;

        MmNumberOfPhysicalPages += (PFN_COUNT)Descriptor->PageCount;

        if (Descriptor->BasePage < MmLowestPhysicalPage)
            MmLowestPhysicalPage = Descriptor->BasePage;

        if (Descriptor->BasePage + Descriptor->PageCount - 1 > MmHighestPhysicalPage)
            MmHighestPhysicalPage = Descriptor->BasePage + Descriptor->PageCount - 1;

        if (Descriptor->MemoryType == LoaderFree &&
            (Largest == NULL || Descriptor->PageCount > Largest->PageCount))
        {
            Largest = Descriptor;
        }
    }

    return Largest;
}

static
VOID
MiBuildPhysicalMemoryBlock(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PPHYSICAL_MEMORY_DESCRIPTOR Block;
    PLIST_ENTRY Entry;
    ULONG Runs = 0;

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        Runs++;
    }

    Block = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(PHYSICAL_MEMORY_DESCRIPTOR) + Runs * sizeof(PHYSICAL_MEMORY_RUN), 'lMmM');
    if (Block == NULL)
        return;

    Block->NumberOfRuns = 0;
    Block->NumberOfPages = 0;

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR,
                                                                     ListEntry);
        PPHYSICAL_MEMORY_RUN Last = (Block->NumberOfRuns != 0) ? &Block->Run[Block->NumberOfRuns - 1] : NULL;

        if (Descriptor->PageCount == 0 || MiMemoryTypeIsInvisible(Descriptor->MemoryType))
            continue;

        Block->NumberOfPages += Descriptor->PageCount;

        if (Last != NULL && Last->BasePage + Last->PageCount == Descriptor->BasePage)
        {
            Last->PageCount += Descriptor->PageCount;
            continue;
        }

        Block->Run[Block->NumberOfRuns].BasePage = Descriptor->BasePage;
        Block->Run[Block->NumberOfRuns].PageCount = Descriptor->PageCount;
        Block->NumberOfRuns++;
    }

    MmPhysicalMemoryBlock = Block;
}

static
NTSTATUS
MiCreatePoolRegion(
    _In_ ULONG64 Bytes,
    _Out_ PVOID *Base)
{
    PMI_VAD Vad;
    ULONG64 Address;
    NTSTATUS Status;

    Status = MiSystemRegionCreate(&MiSystem, Bytes >> PAGE_SHIFT, TRUE, &Vad, &Address);
    *Base = (PVOID)(ULONG_PTR)Address;
    return Status;
}

static
ULONG64
MiClampBytes(
    _In_ ULONG64 Value,
    _In_ ULONG64 Minimum,
    _In_ ULONG64 Maximum)
{
    Value &= ~((ULONG64)(2 * _1MB) - 1);
    return (Value < Minimum) ? Minimum : ((Value > Maximum) ? Maximum : Value);
}

static
VOID
MiInitializePhase0(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Largest;
    ULONG64 PhysicalBytes;
    ULONG64 NonPagedBytes, PagedBytes, SystemPteBytes, ExecutableBytes;
    ULONG64 SharedPhysical;
    PEPROCESS Process = PsGetCurrentProcess();
    ULONG FrameCount, PfnPages, PfnFirst;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    PMI_PFN PfnArray;

    MmSystemRangeStart = (PVOID)(ULONG_PTR)MiArchDescribe()->SystemAddressStart;
    MmUserProbeAddress = (ULONG_PTR)MI_USER_PROBE_ADDRESS;
    MmHighestUserAddress = (PVOID)MI_HIGHEST_USER_ADDRESS;
    MmCriticalSectionTimeout.QuadPart = -(LONGLONG)MmCritsectTimeoutSeconds * 10000000LL;
    KeInitializeMutant(&MmSystemLoadLock, FALSE);

    DbgPrint("MM: phase 0 start\n");
    Largest = MiScanMemoryDescriptors(LoaderBlock);
    if (Largest == NULL || MmNumberOfPhysicalPages < MI_MINIMUM_PHYSICAL_PAGES)
        KeBugCheckEx(INSTALL_MORE_MEMORY, MmNumberOfPhysicalPages, MmLowestPhysicalPage, MmHighestPhysicalPage, 0);

    FrameCount = (ULONG)(MmHighestPhysicalPage + 1);
    PfnPages = (ULONG)BYTES_TO_PAGES((ULONG64)FrameCount * sizeof(MI_PFN));

    if (Largest->PageCount < PfnPages + 1024)
        KeBugCheckEx(INSTALL_MORE_MEMORY, MmNumberOfPhysicalPages, PfnPages, Largest->PageCount, 1);

    PfnFirst = (ULONG)(Largest->BasePage + Largest->PageCount - PfnPages);
    PfnArray = MiArchMapFrame(PfnFirst);

    Status = MiSystemInitialize(&MiSystem, PfnArray, FrameCount, MI_PFN_CPU_CACHES,
                                (LONG64)MmNumberOfPhysicalPages);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x494E4954, (ULONG_PTR)Status, 0, 0);

    DbgPrint("MM: pfn database frames %lx at frame %lx (%lu pages), physical pages %lu\n", FrameCount, PfnFirst,
             PfnPages, (ULONG)MmNumberOfPhysicalPages);
    MiPfnMarkInUse(&MiSystem.Pfn, PfnFirst, PfnPages);

    MiBootRootFrame = MiArchBootRootFrame();
    Status = MiAddressSpaceAdopt(&MiSystem, &MiSystem.SystemSpace, (ULONG)MiBootRootFrame, TRUE);
    if (NT_SUCCESS(Status))
        Status = MiSystemAdoptBootMappings(&MiSystem);

    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x41444F50, (ULONG_PTR)Status, (ULONG_PTR)MiBootRootFrame, 0);

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR,
                                                                     ListEntry);

        if (Descriptor->PageCount == 0 || MiMemoryTypeIsInvisible(Descriptor->MemoryType))
            continue;

        if (MiMemoryTypeIsFree(Descriptor->MemoryType))
            MiPfnDbAddRange(&MiSystem.Pfn, (ULONG)Descriptor->BasePage, (ULONG)Descriptor->PageCount);
        else
            MiPfnMarkInUse(&MiSystem.Pfn, (ULONG)Descriptor->BasePage, (ULONG)Descriptor->PageCount);
    }

    DbgPrint("MM: boot tables adopted, root %I64x, %lu VADs, %I64d tables, %I64u pages free\n", MiBootRootFrame,
             (ULONG)MiSystem.SystemSpace.VadRoot.NodeCount, MiSystem.SystemSpace.PageTablePages,
             MiPfnAvailablePages(&MiSystem.Pfn));
    Status = MiSystemPopulateTopLevel(&MiSystem);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x544F504C, (ULONG_PTR)Status, 0, 0);

    PhysicalBytes = (ULONG64)MmNumberOfPhysicalPages << PAGE_SHIFT;
    SystemPteBytes = MiClampBytes(PhysicalBytes / 2, 128 * _1MB, 2 * _1GB);
    NonPagedBytes = MiClampBytes(PhysicalBytes / 2, 64 * _1MB, 4ULL * _1GB);
    ExecutableBytes = NonPagedBytes / 2;
    PagedBytes = MiClampBytes(PhysicalBytes, 128 * _1MB, 4ULL * _1GB);

    Status = MiSystemPtesInitialize(&MiSystem, SystemPteBytes >> PAGE_SHIFT, MI_SYSPTE_CPU_CACHES);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x53505445, (ULONG_PTR)Status, 0, 0);

    MiSystem.SystemPtes->DefaultStackPages = KERNEL_STACK_SIZE >> PAGE_SHIFT;
    MmNumberOfSystemPtes = (ULONG)(SystemPteBytes >> PAGE_SHIFT);

    Status = MiCreatePoolRegion(NonPagedBytes, &MmNonPagedPoolStart);
    if (NT_SUCCESS(Status))
        Status = MiCreatePoolRegion(PagedBytes, &MmPagedPoolStart);

    if (NT_SUCCESS(Status))
    {
        Status = MiSystemCommitPinned(&MiSystem, (ULONG64)(ULONG_PTR)MmNonPagedPoolStart,
                                      (ULONG)(MI_BOOT_RESIDENT_POOL_BYTES >> PAGE_SHIFT), MI_PROT_READWRITE);
    }

    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x504F4F4C, (ULONG_PTR)Status, 0, 0);

    MmSizeOfNonPagedPoolInBytes = MI_BOOT_RESIDENT_POOL_BYTES;
    MmMaximumNonPagedPoolInBytes = (SIZE_T)NonPagedBytes;
    MmNonPagedPoolExpansionStart = (PVOID)((PUCHAR)MmNonPagedPoolStart + MI_BOOT_RESIDENT_POOL_BYTES);
    MmNonPagedPoolEnd = (PVOID)((PUCHAR)MmNonPagedPoolStart + NonPagedBytes);
    MmSizeOfPagedPoolInBytes = (SIZE_T)PagedBytes;
    MmPagedPoolEnd = (PVOID)((PUCHAR)MmPagedPoolStart + PagedBytes - 1);
    MmNonPagedSystemStart = MmNonPagedPoolStart;

    MiPoolLayout.NonPagedResidentBase = MmNonPagedPoolStart;
    MiPoolLayout.NonPagedResidentBytes = MI_BOOT_RESIDENT_POOL_BYTES;
    MiPoolLayout.NonPagedExpansionBase = MmNonPagedPoolExpansionStart;
    MiPoolLayout.NonPagedExpansionBytes = (SIZE_T)(NonPagedBytes - MI_BOOT_RESIDENT_POOL_BYTES - ExecutableBytes);
    MiPoolLayout.ExecutableBase = (PUCHAR)MmNonPagedPoolStart + NonPagedBytes - ExecutableBytes;
    MiPoolLayout.ExecutableBytes = (SIZE_T)ExecutableBytes;
    MiPoolLayout.PagedBase = MmPagedPoolStart;
    MiPoolLayout.PagedBytes = (SIZE_T)PagedBytes;

    DbgPrint("MM: system PTEs %I64x, nonpaged pool %p, paged pool %p\n", MiSystem.SystemPtes->Base,
             MmNonPagedPoolStart, MmPagedPoolStart);
    InitializePool(NonPagedPool, 0);
    InitializePool(PagedPool, 0);
    MiPoolReady = TRUE;

    if (!MiPtTranslate(&MiSystem.SystemSpace, (ULONG64)KI_USER_SHARED_DATA, &SharedPhysical, NULL))
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x4B555352, 0, 0, 0);

    Status = MiProcessManagerInitializeEx(&MiSystem, &MiProcessManager, (ULONG)(SharedPhysical >> PAGE_SHIFT),
                                          (ULONG64)KI_USER_SHARED_DATA);
    if (NT_SUCCESS(Status))
        Status = MiProcessAdopt(&MiSystem, &MiProcessManager, &MiSystemProcess, (ULONG)MiBootRootFrame);

    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x50524F43, (ULONG_PTR)Status, 0, 0);

    MmWriteableSharedUserData->LargePageMinimum = MiSystem.Arch->SupportsLargePages
        ? (ULONG)MiSystem.Arch->LargePageSize : 0;

    Process->Vm.VmWorkingSetList = (PVOID)&MiSystemProcess;
    KPROCESS_DTB0(&Process->Pcb) = (ULONG_PTR)(MiBootRootFrame << PAGE_SHIFT);
    KPROCESS_DTB1(&Process->Pcb) = (ULONG_PTR)(MiBootRootFrame << PAGE_SHIFT);

    if (PsIdleProcess != NULL && PsIdleProcess != Process)
    {
        PsIdleProcess->Vm.VmWorkingSetList = (PVOID)&MiSystemProcess;
        KPROCESS_DTB0(&PsIdleProcess->Pcb) = KPROCESS_DTB0(&Process->Pcb);
        KPROCESS_DTB1(&PsIdleProcess->Pcb) = KPROCESS_DTB1(&Process->Pcb);
    }

    MmAvailablePages = (PFN_NUMBER)MiPfnAvailablePages(&MiSystem.Pfn);
    MmResidentAvailablePages = MmAvailablePages;
    MiSystem.CommitLimit = (LONG64)MmAvailablePages + MI_ATOMIC_READ64(&MiSystem.CommittedPages);
    MiSystem.UnusedSegmentLimit = MI_NT_UNUSED_SEGMENT_LIMIT;
    MmTotalCommitLimit = (SIZE_T)MiSystem.CommitLimit;
    MmTotalCommitLimitMaximum = MmTotalCommitLimit;
    MmtotalCommitLimitMaximum = MmTotalCommitLimit;
    MmLowMemoryThreshold = MmNumberOfPhysicalPages / 32;
    MmHighMemoryThreshold = MmNumberOfPhysicalPages / 8;
    MmReadClusterSize = 7;
    MmSecondaryColors = 1;
    MmAllocationFragment = 64 * _1KB;

    DbgPrint("MM: phase 0 done, %I64u pages available, commit limit %I64d\n", (ULONG64)MmAvailablePages,
             MiSystem.CommitLimit);
    MiBuildPhysicalMemoryBlock(LoaderBlock);
    MiInitializeLoadedModuleList(LoaderBlock);
}

BOOLEAN
NTAPI
MmArmInitSystem(
    _In_ ULONG Phase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (Phase == 0)
        MiInitializePhase0(LoaderBlock);

    return TRUE;
}

BOOLEAN
NTAPI
MmInitSystem(
    _In_ ULONG Phase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    if (Phase != 1)
        return TRUE;

    if (!MiUserHalfCleared)
    {
        MiUserHalfCleared = TRUE;
        MiArchBootClearUserHalf(MiBootRootFrame);
    }

    if (!NT_SUCCESS(MiSectionInitialize()))
        return FALSE;

    if (!NT_SUCCESS(MiWorkerThreadsInitialize()))
        return FALSE;

    return TRUE;
}

VOID
NTAPI
MiArm64UnmapEarlyDeviceAliases(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    if (MiUserHalfCleared)
        return;

    MiUserHalfCleared = TRUE;
    MiArchBootClearUserHalf(MiBootRootFrame);
}

VOID
NTAPI
MmFreeLoaderBlock(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
}

VOID
NTAPI
MmDumpArmPfnDatabase(
    _In_ BOOLEAN StatusOnly)
{
    UNREFERENCED_PARAMETER(StatusOnly);

    DbgPrint("Mm: physical %lu available %lu standby %lu modified %lu committed %lu limit %lu\n",
             (ULONG)MmNumberOfPhysicalPages, (ULONG)MiPfnAvailablePages(&MiSystem.Pfn),
             (ULONG)MiPfnListCount(&MiSystem.Pfn, MiPageStandby), (ULONG)MiPfnListCount(&MiSystem.Pfn, MiPageModified),
             (ULONG)MI_ATOMIC_READ64(&MiSystem.CommittedPages), (ULONG)MiSystem.CommitLimit);
}
