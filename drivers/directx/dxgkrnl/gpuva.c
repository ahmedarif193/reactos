/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU Virtual Address Space Management (WDDM 2.0)
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * This file implements per-process GPU virtual address space management,
 * the core feature of WDDM 2.0.  Each process gets its own GPU VA space
 * backed by a root page table programmed into the GPU MMU.
 *
 * Architecture:
 *
 *   - Each DXGKRNL_PROCESS tracks a sorted list of GPU VA ranges
 *     (DXGKRNL_GPUVA_RANGE), either reserved or mapped to allocations.
 *
 *   - DxgkDdiCreateProcess is called once per process when it first
 *     uses the GPU.  The miniport creates per-process GPU state.
 *
 *   - DxgkDdiSetRootPageTable programs the GPU MMU with the process's
 *     page table base before any context in that process can execute.
 *
 *   - GPU VA mapping (MapGpuVirtualAddress) assigns a VA from the
 *     process space and updates page tables via the miniport.
 *
 *   - CPU host aperture mapping allows CPU access to GPU-local memory
 *     through a CPU-visible aperture segment.
 *
 * Pool tags:
 *   'GVxD' — GPU VA range objects  (TAG_DXGK_GPUVA)
 */

/* INCLUDES *******************************************************************/

#include "dxgkrnl_private.h"
#include "vidmm.h"

#define NDEBUG
#include <debug.h>

/* MACROS / CONSTANTS *********************************************************/

/*
 * Default GPU VA space size per process: 256 TB (48-bit VA space).
 * Most WDDM 2.0 GPUs support at least 48-bit virtual addressing.
 */
#define GPUVA_DEFAULT_SPACE_SIZE    (256ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL)

/*
 * Minimum alignment for GPU VA allocations: 4 KB (GPU page size).
 */
#define GPUVA_PAGE_SIZE             (4096ULL)
#define GPUVA_PAGE_MASK             (GPUVA_PAGE_SIZE - 1ULL)

/*
 * GPU VA space starts at 64 KB (skip the first 16 pages as a guard region
 * to catch NULL pointer dereferences on the GPU).
 */
#define GPUVA_START_ADDRESS         (64ULL * 1024ULL)

/* HELPERS ********************************************************************/

/*
 * GpuVaAlignUp
 * Rounds Value up to the next multiple of Alignment (power-of-2).
 */
FORCEINLINE
ULONGLONG
GpuVaAlignUp(
    _In_ ULONGLONG Value,
    _In_ ULONGLONG Alignment)
{
    ASSERT(Alignment != 0);
    ASSERT((Alignment & (Alignment - 1)) == 0);
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

/*
 * GpuVaAlignDown
 * Rounds Value down to the nearest multiple of Alignment (power-of-2).
 */
FORCEINLINE
ULONGLONG
GpuVaAlignDown(
    _In_ ULONGLONG Value,
    _In_ ULONGLONG Alignment)
{
    ASSERT(Alignment != 0);
    ASSERT((Alignment & (Alignment - 1)) == 0);
    return Value & ~(Alignment - 1);
}

/*
 * GpuVaRangesOverlap
 * Returns TRUE if [StartA, StartA+SizeA) overlaps [StartB, StartB+SizeB).
 */
FORCEINLINE
BOOLEAN
GpuVaRangesOverlap(
    _In_ ULONGLONG StartA, _In_ ULONGLONG SizeA,
    _In_ ULONGLONG StartB, _In_ ULONGLONG SizeB)
{
    return (StartA < StartB + SizeB) && (StartB < StartA + SizeA);
}

/*
 * GpuVaAllocRange
 * Allocate and initialize a DXGKRNL_GPUVA_RANGE from NonPagedPool.
 */
static PDXGKRNL_GPUVA_RANGE
GpuVaAllocRange(VOID)
{
    PDXGKRNL_GPUVA_RANGE Range;

    Range = (PDXGKRNL_GPUVA_RANGE)ExAllocatePoolWithTag(
                NonPagedPool,
                sizeof(DXGKRNL_GPUVA_RANGE),
                TAG_DXGK_GPUVA);

    if (Range != NULL)
    {
        RtlZeroMemory(Range, sizeof(DXGKRNL_GPUVA_RANGE));
        InitializeListHead(&Range->RangeListEntry);
    }

    return Range;
}

/*
 * GpuVaFreeRange
 * Free a DXGKRNL_GPUVA_RANGE.
 */
static VOID
GpuVaFreeRange(
    _In_ PDXGKRNL_GPUVA_RANGE Range)
{
    ExFreePoolWithTag(Range, TAG_DXGK_GPUVA);
}

/*
 * GpuVaFindOverlapping
 *
 * Search Process->GpuVaRangeList for any range that overlaps
 * [Address, Address + Size).  Returns the first overlapping range
 * or NULL.
 *
 * Caller MUST hold Process->GpuVaLock.
 */
static PDXGKRNL_GPUVA_RANGE
GpuVaFindOverlapping(
    _In_ PDXGKRNL_PROCESS       Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG              Size)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

        /* Ranges are sorted by VA; if this range starts past our end, stop. */
        if (Range->GpuVirtualAddress >= Address + Size)
            break;

        if (GpuVaRangesOverlap(Range->GpuVirtualAddress, Range->SizeInBytes,
                                Address, Size))
        {
            return Range;
        }
    }

    return NULL;
}

/*
 * GpuVaFindFreeRegion
 *
 * Find a free GPU VA region of at least Size bytes within [MinAddress, MaxAddress).
 * Returns the base address on success, 0 on failure.
 *
 * Uses a first-fit algorithm scanning the sorted range list for gaps.
 *
 * Caller MUST hold Process->GpuVaLock.
 */
static D3DGPU_VIRTUAL_ADDRESS
GpuVaFindFreeRegion(
    _In_ PDXGKRNL_PROCESS       Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS MinAddress,
    _In_ D3DGPU_VIRTUAL_ADDRESS MaxAddress,
    _In_ ULONGLONG              Size)
{
    D3DGPU_VIRTUAL_ADDRESS Candidate;
    PLIST_ENTRY Entry;

    if (MinAddress < GPUVA_START_ADDRESS)
        MinAddress = GPUVA_START_ADDRESS;

    Candidate = GpuVaAlignUp(MinAddress, GPUVA_PAGE_SIZE);

    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

        /* If this range is entirely before our candidate, skip. */
        if (Range->GpuVirtualAddress + Range->SizeInBytes <= Candidate)
            continue;

        /* Check if the gap before this range fits. */
        if (Candidate + Size <= Range->GpuVirtualAddress &&
            Candidate + Size <= MaxAddress)
        {
            return Candidate;
        }

        /* Advance candidate past this range. */
        Candidate = GpuVaAlignUp(Range->GpuVirtualAddress + Range->SizeInBytes,
                                  GPUVA_PAGE_SIZE);

        if (Candidate >= MaxAddress)
            return 0;
    }

    /* Check trailing gap after last range. */
    if (Candidate + Size <= MaxAddress)
        return Candidate;

    return 0;
}

/*
 * GpuVaInsertRange
 *
 * Insert a range into the sorted list, maintaining ascending VA order.
 *
 * Caller MUST hold Process->GpuVaLock.
 */
static VOID
GpuVaInsertRange(
    _In_ PDXGKRNL_PROCESS    Process,
    _In_ PDXGKRNL_GPUVA_RANGE Range)
{
    PLIST_ENTRY Entry;

    /* Find insertion point (first range with VA > new range's VA). */
    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Existing =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

        if (Existing->GpuVirtualAddress > Range->GpuVirtualAddress)
        {
            /* Insert before this entry. */
            InsertTailList(Entry, &Range->RangeListEntry);
            return;
        }
    }

    /* All existing entries have VA <= new range's VA; append at end. */
    InsertTailList(&Process->GpuVaRangeList, &Range->RangeListEntry);
}


/* PROCESS LIFECYCLE **********************************************************/

/*
 * DxgkGpuVaCreateProcess
 *
 * Called when a process first uses the GPU.  Initializes the per-process
 * GPU VA space and calls DxgkDdiCreateProcess on the miniport.
 */
NTSTATUS
DxgkGpuVaCreateProcess(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PDXGKRNL_PROCESS  Process)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    DPRINT("DxgkGpuVaCreateProcess: Adapter=%p Process=%p\n",
           Adapter, Process);

    ASSERT(Adapter != NULL);
    ASSERT(Process != NULL);

    /* Initialize GPU VA space. */
    Process->Adapter = Adapter;
    InitializeListHead(&Process->GpuVaRangeList);
    ExInitializeFastMutex(&Process->GpuVaLock);
    Process->GpuVaTotalReserved = 0;
    Process->GpuVaTotalMapped   = 0;
    Process->ResidencyBudget    = 0;  /* 0 = no limit */
    Process->ResidentBytes      = 0;

    KeInitializeEvent(&Process->ResidencyBudgetEvent,
                      NotificationEvent, FALSE);

    Process->hMiniportProcess   = NULL;
    Process->hRootPageTable     = NULL;
    RtlZeroMemory(&Process->RootPageTableAddress,
                  sizeof(Process->RootPageTableAddress));
    Process->RootPageTableEntries = 0;

    /* Call the miniport's DxgkDdiCreateProcess if available. */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Adapter->MiniportContext->InitData.s.DxgkDdiCreateProcess != NULL)
    {
        DXGKARG_CREATEPROCESS CreateArgs;

        RtlZeroMemory(&CreateArgs, sizeof(CreateArgs));
        CreateArgs.hDxgkProcess = (HANDLE)Process;
        CreateArgs.Flags.Value  = 0;
        CreateArgs.NumPasid     = 0;
        CreateArgs.pPasid       = NULL;

        Status = Adapter->MiniportContext->InitData.s.DxgkDdiCreateProcess(
                     Adapter->MiniportDeviceContext,
                     &CreateArgs);

        if (NT_SUCCESS(Status))
        {
            Process->hMiniportProcess = CreateArgs.hKmdProcess;
            DPRINT("DxgkGpuVaCreateProcess: miniport process=%p\n",
                   Process->hMiniportProcess);
        }
        else
        {
            DPRINT1("DxgkGpuVaCreateProcess: DxgkDdiCreateProcess failed "
                    "0x%08lx\n", Status);
            return Status;
        }
    }
#endif

    DPRINT("DxgkGpuVaCreateProcess: success\n");
    return Status;
}


/*
 * DxgkGpuVaDestroyProcess
 *
 * Tears down the per-process GPU VA space.  Frees all VA ranges and
 * calls DxgkDdiDestroyProcess on the miniport.
 */
VOID
DxgkGpuVaDestroyProcess(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PDXGKRNL_PROCESS  Process)
{
    PAGED_CODE();

    DPRINT("DxgkGpuVaDestroyProcess: Adapter=%p Process=%p\n",
           Adapter, Process);

    if (Process == NULL)
        return;

    /* Free all GPU VA ranges. */
    ExAcquireFastMutex(&Process->GpuVaLock);
    {
        PLIST_ENTRY Entry = Process->GpuVaRangeList.Flink;
        while (Entry != &Process->GpuVaRangeList)
        {
            PLIST_ENTRY Next = Entry->Flink;
            PDXGKRNL_GPUVA_RANGE Range =
                CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

            RemoveEntryList(Entry);
            GpuVaFreeRange(Range);
            Entry = Next;
        }
    }
    ExReleaseFastMutex(&Process->GpuVaLock);

    /* Destroy root page table allocation if any. */
    if (Process->hRootPageTable != NULL && Adapter != NULL)
    {
        DxgkVidMmDestroyAllocation(Adapter, Process->hRootPageTable);
        Process->hRootPageTable = NULL;
    }

    /* Call the miniport's DxgkDdiDestroyProcess. */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Process->hMiniportProcess != NULL &&
        Adapter != NULL &&
        Adapter->MiniportContext->InitData.s.DxgkDdiDestroyProcess != NULL)
    {
        NTSTATUS Status;

        Status = Adapter->MiniportContext->InitData.s.DxgkDdiDestroyProcess(
                     Adapter->MiniportDeviceContext,
                     Process->hMiniportProcess);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkGpuVaDestroyProcess: DxgkDdiDestroyProcess failed "
                    "0x%08lx\n", Status);
        }

        Process->hMiniportProcess = NULL;
    }
#endif

    Process->Adapter = NULL;

    DPRINT("DxgkGpuVaDestroyProcess: done\n");
}


/* GPU VA RANGE MANAGEMENT ****************************************************/

/*
 * DxgkGpuVaReserve
 *
 * Reserve a region of GPU VA space without mapping it to any allocation.
 * If BaseAddress is 0, a free region is found automatically.
 * On success, *OutAddress receives the actual VA.
 */
NTSTATUS
DxgkGpuVaReserve(
    _In_  PDXGKRNL_PROCESS         Process,
    _In_  D3DGPU_VIRTUAL_ADDRESS   BaseAddress,
    _In_  ULONGLONG                SizeInBytes,
    _Out_ D3DGPU_VIRTUAL_ADDRESS  *OutAddress)
{
    PDXGKRNL_GPUVA_RANGE Range;
    D3DGPU_VIRTUAL_ADDRESS ActualAddress;

    PAGED_CODE();

    DPRINT("DxgkGpuVaReserve: Process=%p Base=0x%I64x Size=0x%I64x\n",
           Process, BaseAddress, SizeInBytes);

    if (Process == NULL || SizeInBytes == 0 || OutAddress == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutAddress = 0;

    /* Align size up to page boundary. */
    SizeInBytes = GpuVaAlignUp(SizeInBytes, GPUVA_PAGE_SIZE);

    Range = GpuVaAllocRange();
    if (Range == NULL)
        return STATUS_NO_MEMORY;

    ExAcquireFastMutex(&Process->GpuVaLock);

    if (BaseAddress != 0)
    {
        /* Caller requested a specific address. Align it. */
        ActualAddress = GpuVaAlignDown(BaseAddress, GPUVA_PAGE_SIZE);

        if (ActualAddress < GPUVA_START_ADDRESS)
            ActualAddress = GPUVA_START_ADDRESS;

        /* Check for conflicts. */
        if (GpuVaFindOverlapping(Process, ActualAddress, SizeInBytes) != NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            DPRINT1("DxgkGpuVaReserve: conflict at 0x%I64x\n", ActualAddress);
            return STATUS_CONFLICTING_ADDRESSES;
        }
    }
    else
    {
        /* Find a free region. */
        ActualAddress = GpuVaFindFreeRegion(Process,
                                             GPUVA_START_ADDRESS,
                                             GPUVA_DEFAULT_SPACE_SIZE,
                                             SizeInBytes);
        if (ActualAddress == 0)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            DPRINT1("DxgkGpuVaReserve: no free VA region for %I64u bytes\n",
                    SizeInBytes);
            return STATUS_NO_MEMORY;
        }
    }

    /* Populate the range. */
    Range->GpuVirtualAddress = ActualAddress;
    Range->SizeInBytes       = SizeInBytes;
    Range->State             = GpuVaStateReserved;
    Range->hAllocation       = NULL;
    Range->AllocationOffset  = 0;

    GpuVaInsertRange(Process, Range);
    Process->GpuVaTotalReserved += SizeInBytes;

    ExReleaseFastMutex(&Process->GpuVaLock);

    *OutAddress = ActualAddress;

    DPRINT("DxgkGpuVaReserve: reserved at 0x%I64x size=0x%I64x\n",
           ActualAddress, SizeInBytes);

    return STATUS_SUCCESS;
}


/*
 * DxgkGpuVaMap
 *
 * Map an allocation into the process's GPU VA space.
 * If BaseAddress is 0, a free region within [MinAddress, MaxAddress) is used.
 * On success, *OutAddress receives the GPU VA where the allocation was mapped.
 */
NTSTATUS
DxgkGpuVaMap(
    _In_    PDXGKRNL_PROCESS       Process,
    _In_    HANDLE                 hAllocation,
    _In_    ULONGLONG              AllocationOffset,
    _In_    ULONGLONG              SizeInBytes,
    _In_    D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_    D3DGPU_VIRTUAL_ADDRESS MinAddress,
    _In_    D3DGPU_VIRTUAL_ADDRESS MaxAddress,
    _Out_   D3DGPU_VIRTUAL_ADDRESS *OutAddress)
{
    PDXGKRNL_GPUVA_RANGE Range;
    D3DGPU_VIRTUAL_ADDRESS ActualAddress;

    PAGED_CODE();

    DPRINT("DxgkGpuVaMap: Process=%p hAlloc=%p Offset=0x%I64x "
           "Size=0x%I64x Base=0x%I64x\n",
           Process, hAllocation, AllocationOffset, SizeInBytes, BaseAddress);

    if (Process == NULL || hAllocation == NULL ||
        SizeInBytes == 0 || OutAddress == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *OutAddress = 0;

    /* Align size up to page boundary. */
    SizeInBytes = GpuVaAlignUp(SizeInBytes, GPUVA_PAGE_SIZE);

    /* Default address range if not specified. */
    if (MinAddress == 0)
        MinAddress = GPUVA_START_ADDRESS;
    if (MaxAddress == 0)
        MaxAddress = GPUVA_DEFAULT_SPACE_SIZE;

    Range = GpuVaAllocRange();
    if (Range == NULL)
        return STATUS_NO_MEMORY;

    ExAcquireFastMutex(&Process->GpuVaLock);

    if (BaseAddress != 0)
    {
        ActualAddress = GpuVaAlignDown(BaseAddress, GPUVA_PAGE_SIZE);

        if (ActualAddress < MinAddress)
            ActualAddress = GpuVaAlignUp(MinAddress, GPUVA_PAGE_SIZE);

        /* Check for conflicts. */
        if (GpuVaFindOverlapping(Process, ActualAddress, SizeInBytes) != NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            DPRINT1("DxgkGpuVaMap: conflict at 0x%I64x\n", ActualAddress);
            return STATUS_CONFLICTING_ADDRESSES;
        }
    }
    else
    {
        ActualAddress = GpuVaFindFreeRegion(Process, MinAddress,
                                             MaxAddress, SizeInBytes);
        if (ActualAddress == 0)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            DPRINT1("DxgkGpuVaMap: no free region\n");
            return STATUS_NO_MEMORY;
        }
    }

    /* Populate the range. */
    Range->GpuVirtualAddress = ActualAddress;
    Range->SizeInBytes       = SizeInBytes;
    Range->State             = GpuVaStateMapped;
    Range->hAllocation       = hAllocation;
    Range->AllocationOffset  = AllocationOffset;

    GpuVaInsertRange(Process, Range);
    Process->GpuVaTotalMapped += SizeInBytes;

    ExReleaseFastMutex(&Process->GpuVaLock);

    *OutAddress = ActualAddress;

    DPRINT("DxgkGpuVaMap: mapped at 0x%I64x size=0x%I64x alloc=%p\n",
           ActualAddress, SizeInBytes, hAllocation);

    return STATUS_SUCCESS;
}


/*
 * DxgkGpuVaFree
 *
 * Free a previously reserved or mapped GPU VA region.
 */
NTSTATUS
DxgkGpuVaFree(
    _In_ PDXGKRNL_PROCESS       Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_ ULONGLONG              SizeInBytes)
{
    PLIST_ENTRY Entry, Next;
    BOOLEAN     Found = FALSE;

    PAGED_CODE();

    DPRINT("DxgkGpuVaFree: Process=%p Base=0x%I64x Size=0x%I64x\n",
           Process, BaseAddress, SizeInBytes);

    if (Process == NULL || BaseAddress == 0)
        return STATUS_INVALID_PARAMETER;

    /* Align size if specified. */
    if (SizeInBytes != 0)
        SizeInBytes = GpuVaAlignUp(SizeInBytes, GPUVA_PAGE_SIZE);

    ExAcquireFastMutex(&Process->GpuVaLock);

    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList;
         Entry = Next)
    {
        PDXGKRNL_GPUVA_RANGE Range =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        Next = Entry->Flink;

        /*
         * Match by base address. If SizeInBytes is 0, free the range at
         * exactly BaseAddress. Otherwise, free all ranges that overlap
         * [BaseAddress, BaseAddress + SizeInBytes).
         */
        if (SizeInBytes == 0)
        {
            if (Range->GpuVirtualAddress == BaseAddress)
            {
                if (Range->State == GpuVaStateMapped)
                    Process->GpuVaTotalMapped -= Range->SizeInBytes;
                else
                    Process->GpuVaTotalReserved -= Range->SizeInBytes;

                RemoveEntryList(&Range->RangeListEntry);
                GpuVaFreeRange(Range);
                Found = TRUE;
                break;
            }
        }
        else
        {
            if (GpuVaRangesOverlap(Range->GpuVirtualAddress,
                                    Range->SizeInBytes,
                                    BaseAddress, SizeInBytes))
            {
                if (Range->State == GpuVaStateMapped)
                    Process->GpuVaTotalMapped -= Range->SizeInBytes;
                else
                    Process->GpuVaTotalReserved -= Range->SizeInBytes;

                RemoveEntryList(&Range->RangeListEntry);
                GpuVaFreeRange(Range);
                Found = TRUE;
                /* Continue to free all overlapping ranges. */
            }
        }

        /* Sorted list: if past our end address, stop. */
        if (SizeInBytes != 0 &&
            Range->GpuVirtualAddress >= BaseAddress + SizeInBytes)
        {
            break;
        }
    }

    ExReleaseFastMutex(&Process->GpuVaLock);

    if (!Found)
    {
        DPRINT1("DxgkGpuVaFree: no range found at 0x%I64x\n", BaseAddress);
        return STATUS_NOT_FOUND;
    }

    DPRINT("DxgkGpuVaFree: freed at 0x%I64x\n", BaseAddress);
    return STATUS_SUCCESS;
}


/* ROOT PAGE TABLE ************************************************************/

/*
 * DxgkGpuVaSetRootPageTable
 *
 * Programs the GPU MMU with the process's root page table for a context.
 * Calls DxgkDdiSetRootPageTable on the miniport.
 */
NTSTATUS
DxgkGpuVaSetRootPageTable(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PDXGKRNL_PROCESS  Process,
    _In_ PDXGKRNL_CONTEXT  Context)
{
    PAGED_CODE();

    DPRINT("DxgkGpuVaSetRootPageTable: Adapter=%p Process=%p Context=%p\n",
           Adapter, Process, Context);

    if (Adapter == NULL || Process == NULL || Context == NULL)
        return STATUS_INVALID_PARAMETER;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Adapter->MiniportContext->InitData.s.DxgkDdiSetRootPageTable != NULL)
    {
        DXGKARG_SETROOTPAGETABLE SetArgs;

        RtlZeroMemory(&SetArgs, sizeof(SetArgs));
        SetArgs.hContext    = Context->hMiniportContext;
        SetArgs.Address     = Process->RootPageTableAddress;
        SetArgs.NumEntries  = Process->RootPageTableEntries;

        Adapter->MiniportContext->InitData.s.DxgkDdiSetRootPageTable(
            Adapter->MiniportDeviceContext,
            &SetArgs);

        DPRINT("DxgkGpuVaSetRootPageTable: set for context %p "
               "addr=0x%I64x entries=%u\n",
               Context, SetArgs.Address.SegmentOffset, SetArgs.NumEntries);
    }
#else
    UNREFERENCED_PARAMETER(Context);
#endif

    return STATUS_SUCCESS;
}


/*
 * DxgkGpuVaGetRootPageTableSize
 *
 * Query the miniport for the root page table allocation size
 * needed to cover NumberOfPte entries.
 */
SIZE_T
DxgkGpuVaGetRootPageTableSize(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ UINT              NumberOfPte,
    _In_ UINT              PhysicalAdapterIndex)
{
    PAGED_CODE();

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Adapter != NULL &&
        Adapter->MiniportContext->InitData.s.DxgkDdiGetRootPageTableSize != NULL)
    {
        DXGKARG_GETROOTPAGETABLESIZE Args;
        SIZE_T Size;

        Args.NumberOfPte       = NumberOfPte;
        Args.PhysicalAdapterIndex = PhysicalAdapterIndex;

        Size = Adapter->MiniportContext->InitData.s.DxgkDdiGetRootPageTableSize(
                   Adapter->MiniportDeviceContext,
                   &Args);

        DPRINT("DxgkGpuVaGetRootPageTableSize: NumberOfPte=%u size=%Iu\n",
               NumberOfPte, Size);
        return Size;
    }
#else
    UNREFERENCED_PARAMETER(NumberOfPte);
    UNREFERENCED_PARAMETER(PhysicalAdapterIndex);
#endif

    /* Default: assume 4 KB per PTE entry. */
    return (SIZE_T)NumberOfPte * sizeof(ULONGLONG);
}


/* RESIDENCY MANAGEMENT *******************************************************/

/*
 * DxgkGpuVaMakeResident
 *
 * Bring a set of allocations into GPU-accessible memory.
 * This is the WDDM 2.0 residency model where applications explicitly
 * manage what is resident.
 */
NTSTATUS
DxgkGpuVaMakeResident(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_    HANDLE            *AllocationList,
    _In_    ULONG              NumAllocations,
    _Out_   ULONGLONG         *OutBytesResidency)
{
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONGLONG TotalBytes = 0;

    PAGED_CODE();

    DPRINT("DxgkGpuVaMakeResident: %lu allocations\n", NumAllocations);

    if (Adapter == NULL || Process == NULL ||
        AllocationList == NULL || NumAllocations == 0 ||
        OutBytesResidency == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *OutBytesResidency = 0;

    for (i = 0; i < NumAllocations; i++)
    {
        PDXGKVMM_ALLOCATION Alloc = DxgkVidMmHandleToAllocation(AllocationList[i]);

        if (Alloc == NULL)
        {
            DPRINT1("DxgkGpuVaMakeResident: invalid alloc handle %p\n",
                    AllocationList[i]);
            Status = STATUS_INVALID_HANDLE;
            continue;
        }

        if (!Alloc->Resident)
        {
            NTSTATUS MakeStatus = DxgkVidMmMakeResident(Alloc, Adapter);
            if (!NT_SUCCESS(MakeStatus))
            {
                DPRINT1("DxgkGpuVaMakeResident: MakeResident failed for %p: "
                        "0x%08lx\n", Alloc, MakeStatus);
                if (NT_SUCCESS(Status))
                    Status = MakeStatus;
                continue;
            }
        }

        TotalBytes += Alloc->Size;
    }

    Process->ResidentBytes += TotalBytes;
    *OutBytesResidency = Process->ResidentBytes;

    DPRINT("DxgkGpuVaMakeResident: total resident=%I64u\n",
           Process->ResidentBytes);

    return Status;
}


/*
 * DxgkGpuVaEvict
 *
 * Evict allocations from GPU-accessible memory (WDDM 2.0 residency model).
 */
NTSTATUS
DxgkGpuVaEvict(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_    HANDLE            *AllocationList,
    _In_    ULONG              NumAllocations,
    _Out_   ULONGLONG         *OutNumBytesToTrim)
{
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONGLONG TotalEvicted = 0;

    PAGED_CODE();

    DPRINT("DxgkGpuVaEvict: %lu allocations\n", NumAllocations);

    if (Adapter == NULL || Process == NULL ||
        AllocationList == NULL || NumAllocations == 0 ||
        OutNumBytesToTrim == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *OutNumBytesToTrim = 0;

    for (i = 0; i < NumAllocations; i++)
    {
        PDXGKVMM_ALLOCATION Alloc = DxgkVidMmHandleToAllocation(AllocationList[i]);

        if (Alloc == NULL)
        {
            DPRINT1("DxgkGpuVaEvict: invalid alloc handle %p\n",
                    AllocationList[i]);
            Status = STATUS_INVALID_HANDLE;
            continue;
        }

        if (Alloc->Resident)
        {
            NTSTATUS EvictStatus = DxgkVidMmEvict(Alloc);
            if (!NT_SUCCESS(EvictStatus))
            {
                DPRINT1("DxgkGpuVaEvict: Evict failed for %p: 0x%08lx\n",
                        Alloc, EvictStatus);
                if (NT_SUCCESS(Status))
                    Status = EvictStatus;
                continue;
            }
            TotalEvicted += Alloc->Size;
        }
    }

    if (Process->ResidentBytes >= TotalEvicted)
        Process->ResidentBytes -= TotalEvicted;
    else
        Process->ResidentBytes = 0;

    /* Report how much the process would need to trim to fit budget. */
    if (Process->ResidencyBudget > 0 &&
        Process->ResidentBytes > Process->ResidencyBudget)
    {
        *OutNumBytesToTrim = Process->ResidentBytes - Process->ResidencyBudget;
    }

    DPRINT("DxgkGpuVaEvict: evicted=%I64u resident=%I64u trim=%I64u\n",
           TotalEvicted, Process->ResidentBytes, *OutNumBytesToTrim);

    return Status;
}


/* CPU HOST APERTURE **********************************************************/

/*
 * DxgkGpuVaMapCpuHostAperture
 *
 * Map GPU memory into a CPU-visible aperture segment via the miniport.
 */
NTSTATUS
DxgkGpuVaMapCpuHostAperture(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ HANDLE            hAllocation,
    _In_ WORD              SegmentId,
    _In_ UINT64            NumberOfPages,
    _In_ UINT32           *pCpuHostAperturePages,
    _In_ UINT64           *pMemorySegmentPages)
{
    PAGED_CODE();

    DPRINT("DxgkGpuVaMapCpuHostAperture: Adapter=%p hAlloc=%p Seg=%u "
           "Pages=%I64u\n",
           Adapter, hAllocation, SegmentId, NumberOfPages);

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Adapter->MiniportContext->InitData.s.DxgkDdiMapCpuHostAperture != NULL)
    {
        DXGKARG_MAPCPUHOSTAPERTURE Args;

        RtlZeroMemory(&Args, sizeof(Args));
        Args.hAllocation           = hAllocation;
        Args.SegmentId             = SegmentId;
        Args.PhysicalAdapterIndex  = 0;
        Args.NumberOfPages         = NumberOfPages;
        Args.pCpuHostAperturePages = pCpuHostAperturePages;
        Args.pMemorySegmentPages   = pMemorySegmentPages;

        return Adapter->MiniportContext->InitData.s.DxgkDdiMapCpuHostAperture(
                   Adapter->MiniportDeviceContext,
                   &Args);
    }
#else
    UNREFERENCED_PARAMETER(hAllocation);
    UNREFERENCED_PARAMETER(SegmentId);
    UNREFERENCED_PARAMETER(NumberOfPages);
    UNREFERENCED_PARAMETER(pCpuHostAperturePages);
    UNREFERENCED_PARAMETER(pMemorySegmentPages);
#endif

    return STATUS_NOT_SUPPORTED;
}


/*
 * DxgkGpuVaUnmapCpuHostAperture
 *
 * Unmap GPU memory from a CPU-visible aperture segment.
 */
NTSTATUS
DxgkGpuVaUnmapCpuHostAperture(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ UINT64            NumberOfPages,
    _In_ UINT32           *pCpuHostAperturePages,
    _In_ WORD              SegmentId)
{
    PAGED_CODE();

    DPRINT("DxgkGpuVaUnmapCpuHostAperture: Adapter=%p Seg=%u Pages=%I64u\n",
           Adapter, SegmentId, NumberOfPages);

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Adapter->MiniportContext->InitData.s.DxgkDdiUnmapCpuHostAperture != NULL)
    {
        DXGKARG_UNMAPCPUHOSTAPERTURE Args;

        RtlZeroMemory(&Args, sizeof(Args));
        Args.NumberOfPages         = NumberOfPages;
        Args.pCpuHostAperturePages = pCpuHostAperturePages;
        Args.SegmentId             = SegmentId;
        Args.PhysicalAdapterIndex  = 0;

        return Adapter->MiniportContext->InitData.s.DxgkDdiUnmapCpuHostAperture(
                   Adapter->MiniportDeviceContext,
                   &Args);
    }
#else
    UNREFERENCED_PARAMETER(NumberOfPages);
    UNREFERENCED_PARAMETER(pCpuHostAperturePages);
    UNREFERENCED_PARAMETER(SegmentId);
#endif

    return STATUS_NOT_SUPPORTED;
}

/* EOF */
