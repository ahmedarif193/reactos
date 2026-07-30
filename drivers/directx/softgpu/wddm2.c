/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     WDDM 2.0 ABI callbacks for softgpu.sys, including per-context
 *              GPUVA roots for the software page-table walker.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Architecture notes
 * ==================
 * softgpu is a software GPU with no hardware MMU. These DDIs track the state
 * dxgkrnl supplies and let the software engine walk CPU-visible page tables.
 *
 * These callbacks only exist in DRIVER_INITIALIZATION_DATA at
 * DXGKDDI_INTERFACE_VERSION_WDDM2_0 (0x5023); the whole file is therefore only
 * meaningful for the WDDM2 build (which is the only build, see CMakeLists.txt).
 * The signatures mirror the d3dkmddi.h PFN typedefs exactly — note that
 * DxgkDdiSetRootPageTable returns VOID and DxgkDdiGetRootPageTableSize returns
 * SIZE_T, unlike the NTSTATUS-returning DDIs.
 */

/* INCLUDES ******************************************************************/

#include "softgpu.h"

/* CONSTANTS ***************************************************************** */

/*
 * Root page table sizing.  softgpu has no GPU MMU, but dxgkrnl uses the value
 * returned by DxgkDdiGetRootPageTableSize to allocate the root page table
 * backing store, so it must be a sane non-zero byte count.  The CPU_VIRTUAL
 * implementation stores the public 16-byte DXGK_PTE descriptor for each entry.
 */
#define SOFTGPU_BYTES_PER_PTE   sizeof(DXGK_PTE)

C_ASSERT(sizeof(DXGK_PTE) == 16);


/* =========================================================================
 * DxgkDdiCreateProcess  (WDDM 2.0)
 * =========================================================================
 */

/*
 * SoftGpuDdiCreateProcess
 *
 * Called by dxgkrnl (gpuva.c) once per user-mode process that opens the
 * adapter, so the miniport can set up per-process GPU MMU state.  softgpu has
 * no hardware MMU; it allocates a tracking cookie that owns the process GPUVA
 * root and is handed back at DxgkDdiDestroyProcess time.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiCreateProcess(
    _In_    CONST HANDLE          hAdapter,
    _Inout_ DXGKARG_CREATEPROCESS *pCreateProcess)
{
    PSOFTGPU_DEVICE  Device = (PSOFTGPU_DEVICE)hAdapter;
    PSOFTGPU_PROCESS Process;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pCreateProcess == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Process = (PSOFTGPU_PROCESS)ExAllocatePoolWithTag(NonPagedPool,
                                                      sizeof(SOFTGPU_PROCESS),
                                                      SOFTGPU_WDDM2_POOL_TAG);
    if (Process == NULL)
    {
        DPRINT1("SOFTGPU: CreateProcess: pool alloc failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Process, sizeof(*Process));
    Process->Magic        = SOFTGPU_PROCESS_MAGIC;
    Process->hDxgkProcess = pCreateProcess->hDxgkProcess;
    Process->Adapter      = Device;

    /* Hand the tracked cookie back as the miniport process handle. */
    pCreateProcess->hKmdProcess = (HANDLE)Process;

    DPRINT("SOFTGPU: CreateProcess hDxgkProcess=%p -> hKmdProcess=%p flags=0x%x\n",
           pCreateProcess->hDxgkProcess, Process, pCreateProcess->Flags.Value);
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiDestroyProcess  (WDDM 2.0)
 * =========================================================================
 */

/*
 * SoftGpuDdiDestroyProcess
 *
 * Frees the per-process cookie created by SoftGpuDdiCreateProcess.  The handle
 * argument is the hKmdProcess softgpu returned, not the adapter context.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiDestroyProcess(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST HANDLE hKmdProcess)
{
    PSOFTGPU_DEVICE  Device  = (PSOFTGPU_DEVICE)hAdapter;
    PSOFTGPU_PROCESS Process = (PSOFTGPU_PROCESS)hKmdProcess;
    KIRQL            OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    if (Process == NULL)
        return STATUS_SUCCESS;   /* nothing to tear down */

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        Process->Adapter != Device)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        DPRINT1("SOFTGPU: DestroyProcess: invalid process %p\n", Process);
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: DestroyProcess hKmdProcess=%p\n", Process);

    Process->Magic = 0xDEAD2607UL;
    Process->Adapter = NULL;
    SoftGpuGpuVaRootClear(&Process->Root);
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    ExFreePoolWithTag(Process, SOFTGPU_WDDM2_POOL_TAG);
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiGetRootPageTableSize  (WDDM 2.0)
 * =========================================================================
 */

/*
 * SoftGpuDdiGetRootPageTableSize
 *
 * Reports the byte size dxgkrnl must allocate to back the root page table for
 * NumberOfPte entries.  Returns SIZE_T (NOT NTSTATUS).  softgpu has no real
 * page table, so it returns the exact size of dxgkrnl's software descriptor
 * table (16 bytes per PTE, with at least one page).
 *
 * IRQL: PASSIVE_LEVEL
 */
SIZE_T
APIENTRY
SoftGpuDdiGetRootPageTableSize(
    _In_    CONST HANDLE                  hAdapter,
    _Inout_ DXGKARG_GETROOTPAGETABLESIZE *pArgs)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)hAdapter;
    SIZE_T          Size;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pArgs == NULL)
    {
        /* Cannot signal failure via NTSTATUS; return a minimal page. */
        return PAGE_SIZE;
    }

    Size = (SIZE_T)pArgs->NumberOfPte * SOFTGPU_BYTES_PER_PTE;
    if (Size < PAGE_SIZE)
        Size = PAGE_SIZE;

    DPRINT("SOFTGPU: GetRootPageTableSize NumberOfPte=%u adapterIdx=%u -> %Iu bytes\n",
           pArgs->NumberOfPte, pArgs->PhysicalAdapterIndex, Size);
    return Size;
}


/* =========================================================================
 * DxgkDdiSetRootPageTable  (WDDM 2.0)
 * =========================================================================
 */

/*
 * SoftGpuDdiSetRootPageTable
 *
 * Records a context's root page table for the software walker. Returns VOID.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
APIENTRY
SoftGpuDdiSetRootPageTable(
    _In_ CONST HANDLE                      hAdapter,
    _In_ CONST DXGKARG_SETROOTPAGETABLE   *pSetPageTable)
{
    PSOFTGPU_DEVICE     Device = (PSOFTGPU_DEVICE)hAdapter;
    PSOFTGPU_CONTEXT    Context;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_PROCESS    Process;
    KIRQL               OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pSetPageTable == NULL)
    {
        return;
    }

    DPRINT("SOFTGPU: SetRootPageTable hContext=%p segOffset=0x%I64x entries=%u\n",
           pSetPageTable->hContext,
           pSetPageTable->Address.SegmentOffset,
           pSetPageTable->NumEntries);

    /* SegmentId 0 is a physical page-table address in CPU_VIRTUAL mode. */
    if (pSetPageTable->Address.SegmentId != 0)
        return;

    Context = (PSOFTGPU_CONTEXT)pSetPageTable->hContext;
    /*
     * Context state is the authoritative virtual-submit root.  The process
     * copy supplies the same root to ordinary non-MultiEngineAware submits,
     * whose ABI exposes hDevice rather than hContext.
     */
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped ||
        Context == NULL ||
        Context->Magic != SOFTGPU_CONTEXT_MAGIC)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return;
    }
    KmdDevice = Context->Device;
    Process = Context->Process;
    if (KmdDevice == NULL ||
        KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        KmdDevice->Adapter != Device ||
        KmdDevice->Process != Process ||
        Process == NULL ||
        Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        Process->Adapter != Device)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return;
    }
    SoftGpuGpuVaRootProgram(&Context->Root,
                            pSetPageTable->Address.SegmentOffset,
                            pSetPageTable->NumEntries);
    SoftGpuGpuVaRootProgram(&Process->Root,
                            pSetPageTable->Address.SegmentOffset,
                            pSetPageTable->NumEntries);
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
}


/* =========================================================================
 * DxgkDdiMapCpuHostAperture  (WDDM 2.0)
 * =========================================================================
 */

/*
 * SoftGpuDdiMapCpuHostAperture
 *
 * Maps a range of an allocation's segment pages into a CPU-visible aperture
 * segment.  softgpu's single segment is already CPU-visible system RAM, so the
 * mapping is identity: each requested page is reported back at the same page
 * index in the aperture.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiMapCpuHostAperture(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_MAPCPUHOSTAPERTURE  *pArgs)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)hAdapter;
    UINT64          i;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pArgs == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: MapCpuHostAperture hAlloc=%p seg=%u pages=%I64u\n",
           pArgs->hAllocation, pArgs->SegmentId, pArgs->NumberOfPages);

    /*
     * Report an identity aperture mapping.  The CPU host aperture page for
     * source page i is simply i (the segment is contiguous system RAM that is
     * already CPU-addressable; there is no separate aperture window to assign).
     */
    if (pArgs->pCpuHostAperturePages != NULL)
    {
        for (i = 0; i < pArgs->NumberOfPages; i++)
            pArgs->pCpuHostAperturePages[i] = (UINT32)i;
    }

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiUnmapCpuHostAperture  (WDDM 2.0)
 * =========================================================================
 */

/*
 * SoftGpuDdiUnmapCpuHostAperture
 *
 * Releases an aperture mapping established by SoftGpuDdiMapCpuHostAperture.
 * Nothing was really mapped, so this is a no-op acknowledgement.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiUnmapCpuHostAperture(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_UNMAPCPUHOSTAPERTURE  *pArgs)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)hAdapter;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pArgs == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: UnmapCpuHostAperture seg=%u pages=%I64u\n",
           pArgs->SegmentId, pArgs->NumberOfPages);
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiSubmitCommandVirtual  (WDDM 2.0)
 * =========================================================================
 */

/*
 * SoftGpuDdiSubmitCommandVirtual
 *
 * WDDM2 GPU-virtual-addressing submission path (the DMA buffer is referenced
 * by GPU virtual address rather than a patched allocation list).  dxgkrnl
 * validates the buffer VA against the process's real GpuMmu page tables
 * before submission; execution follows the same software-engine model as
 * SoftGpuDdiSubmitCommand (rendering happened on the CPU, executing a DMA
 * buffer is completing its fence).
 *
 * IRQL: DISPATCH_LEVEL (called from dxgkrnl scheduler)
 */
NTSTATUS
APIENTRY
SoftGpuDdiSubmitCommandVirtual(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMANDVIRTUAL  *pSubmitCommand)
{
    PSOFTGPU_DEVICE     Device = (PSOFTGPU_DEVICE)hAdapter;
    PSOFTGPU_CONTEXT    Context;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_PROCESS    Process;
    SOFTGPU_GPUVA_ROOT  Root;
    HANDLE              DxgkProcessHandle;
    KIRQL               OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pSubmitCommand == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pSubmitCommand->NodeOrdinal != 0 || pSubmitCommand->EngineOrdinal != 0)
        return STATUS_INVALID_PARAMETER;

    DPRINT("SOFTGPU: SubmitCommandVirtual fence=%u node=%u gpuVa=0x%I64x size=%u null=%u\n",
           pSubmitCommand->SubmissionFenceId,
           pSubmitCommand->NodeOrdinal,
           pSubmitCommand->DmaBufferVirtualAddress,
           pSubmitCommand->DmaBufferSize,
           pSubmitCommand->Flags.NullRendering);

    /*
     * Take the root before translating or queueing.  The ring owns this value;
     * programming another context cannot retarget already-submitted work.
     */
    Context = (PSOFTGPU_CONTEXT)pSubmitCommand->hContext;
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_DELETE_PENDING;
    }
    if (Context == NULL || Context->Magic != SOFTGPU_CONTEXT_MAGIC)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_INVALID_HANDLE;
    }
    KmdDevice = Context->Device;
    Process = Context->Process;
    if (KmdDevice == NULL ||
        KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        KmdDevice->Adapter != Device ||
        KmdDevice->Process != Process ||
        Process == NULL ||
        Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        Process->Adapter != Device)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_INVALID_HANDLE;
    }
    SoftGpuGpuVaSubmissionSnapshot(
        &Context->Root,
        Process->hDxgkProcess,
        &Root,
        &DxgkProcessHandle);
    if (pSubmitCommand->Flags.NullRendering)
    {
        PSOFTGPU_SUBMIT Entry;

        if (Device->SubmitRingTail - Device->SubmitRingHead >=
            SOFTGPU_SUBMIT_RING_SIZE)
        {
            KeReleaseSpinLock(&Device->FenceLock, OldIrql);
            return STATUS_DEVICE_BUSY;
        }
        Entry = &Device->SubmitRing[Device->SubmitRingTail %
                                    SOFTGPU_SUBMIT_RING_SIZE];
        RtlZeroMemory(Entry, sizeof(*Entry));
        Entry->Fence = pSubmitCommand->SubmissionFenceId;
        Entry->NullRendering = TRUE;
        Entry->DxgkProcessHandle = DxgkProcessHandle;
        Device->SubmitRingTail++;
        Device->CurrentFence = pSubmitCommand->SubmissionFenceId;
        KeInsertQueueDpc(&Device->DpcObject, NULL, NULL);
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    if (Root.PhysicalAddress == 0 || Root.EntryCount == 0)
        return STATUS_INVALID_DEVICE_STATE;
    if (pSubmitCommand->DmaBufferSize == 0 ||
        pSubmitCommand->DmaBufferVirtualAddress >
            MAXULONGLONG - pSubmitCommand->DmaBufferSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_DELETE_PENDING;
    }
    if (Device->SubmitRingTail - Device->SubmitRingHead >=
        SOFTGPU_SUBMIT_RING_SIZE)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }
    {
        PSOFTGPU_SUBMIT Entry =
            &Device->SubmitRing[Device->SubmitRingTail %
                                SOFTGPU_SUBMIT_RING_SIZE];

        RtlZeroMemory(Entry, sizeof(*Entry));
        Entry->DmaGpuVa =
            pSubmitCommand->DmaBufferVirtualAddress;
        Entry->StartOffset = 0;
        Entry->EndOffset = pSubmitCommand->DmaBufferSize;
        Entry->Fence = pSubmitCommand->SubmissionFenceId;
        Entry->VirtualAddressing = TRUE;
        Entry->DxgkProcessHandle = DxgkProcessHandle;
        SoftGpuGpuVaRootSnapshot(&Root, &Entry->Root);
        Device->SubmitRingTail++;
    }
    Device->CurrentFence = pSubmitCommand->SubmissionFenceId;
    KeInsertQueueDpc(&Device->DpcObject, NULL, NULL);
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
    return STATUS_SUCCESS;
}

/* EOF */
