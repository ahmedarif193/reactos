/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     WDDM 2.0 ABI callbacks for softgpu.sys. The process/page-table
 *              DDIs retain ABI bookkeeping only; the physical/software engine
 *              has no GPU MMU and virtual submission accepts null rendering.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Architecture notes
 * ==================
 * softgpu is a software/null GPU: it has no real MMU, no command processor and
 * no page tables.  These DDIs therefore validate their arguments, track the
 * minimum state the caller (dxgkrnl) expects (a per-process cookie, identity
 * aperture page numbers), and report success without doing any hardware work.
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
 * backing store, so it must be a sane non-zero byte count.  Model 8 bytes per
 * PTE (one 64-bit entry), matching dxgkrnl's own fallback in gpuva.c.
 */
#define SOFTGPU_BYTES_PER_PTE   sizeof(ULONGLONG)


/* =========================================================================
 * DxgkDdiCreateProcess  (WDDM 2.0)
 * =========================================================================
 */

/*
 * SoftGpuDdiCreateProcess
 *
 * Called by dxgkrnl (gpuva.c) once per user-mode process that opens the
 * adapter, so the miniport can set up per-process GPU MMU state.  softgpu has
 * no MMU; it allocates a tracking cookie so dxgkrnl receives a stable non-NULL
 * hKmdProcess that is handed back at DxgkDdiDestroyProcess time.
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

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    if (Process == NULL)
        return STATUS_SUCCESS;   /* nothing to tear down */

    if (Process->Magic != SOFTGPU_PROCESS_MAGIC)
    {
        DPRINT1("SOFTGPU: DestroyProcess: bad magic 0x%08lx on %p\n",
                Process->Magic, Process);
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: DestroyProcess hKmdProcess=%p\n", Process);

    Process->Magic = 0xDEAD2607UL;   /* poison */
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
 * page table, so it returns a plausible non-zero size (8 bytes per PTE, at
 * least one page) that dxgkrnl can allocate without choking.
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
 * Programs the GPU MMU with a context's root page table.  Returns VOID.
 * softgpu has no MMU, so this is a no-op acknowledgement.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
APIENTRY
SoftGpuDdiSetRootPageTable(
    _In_ CONST HANDLE                      hAdapter,
    _In_ CONST DXGKARG_SETROOTPAGETABLE   *pSetPageTable)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)hAdapter;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pSetPageTable == NULL)
    {
        return;
    }

    DPRINT("SOFTGPU: SetRootPageTable hContext=%p segOffset=0x%I64x entries=%u\n",
           pSetPageTable->hContext,
           pSetPageTable->Address.SegmentOffset,
           pSetPageTable->NumEntries);
    /* No hardware MMU to program. */
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
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)hAdapter;
    KIRQL           OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pSubmitCommand == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pSubmitCommand->NodeOrdinal != 0 || pSubmitCommand->EngineOrdinal != 0)
        return STATUS_INVALID_PARAMETER;

    DPRINT("SOFTGPU: SubmitCommandVirtual fence=%u node=%u gpuVa=0x%I64x size=%u\n",
           pSubmitCommand->SubmissionFenceId,
           pSubmitCommand->NodeOrdinal,
           pSubmitCommand->DmaBufferVirtualAddress,
           pSubmitCommand->DmaBufferSize);

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_DELETE_PENDING;
    }
    Device->CurrentFence = pSubmitCommand->SubmissionFenceId;
    KeInsertQueueDpc(&Device->DpcObject, NULL, NULL);
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiRenderGdi  (WDDM 2.0)
 * =========================================================================
 */

/*
 * SoftGpuDdiRenderGdi
 *
 * Hardware-accelerated GDI render entry.  The first argument is the per-context
 * handle returned by DxgkDdiCreateContext (a SOFTGPU_CONTEXT), not the adapter.
 * softgpu performs all GDI rendering through the CPU framebuffer, so it accepts
 * the call as a no-op (no DMA buffer to emit) and reports completion.
 *
 * IRQL: PASSIVE_LEVEL or DISPATCH_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiRenderGdi(
    _In_    CONST HANDLE       hContext,
    _Inout_ DXGKARG_RENDERGDI *pRenderGdi)
{
    PSOFTGPU_CONTEXT Ctx = (PSOFTGPU_CONTEXT)hContext;

    if (Ctx == NULL || Ctx->Magic != SOFTGPU_CONTEXT_MAGIC ||
        pRenderGdi == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * No real DMA stream is produced.  Report that the whole command was
     * consumed in a single pass so dxgkrnl does not loop expecting more.
     */
    pRenderGdi->MultipassOffset = 0;

    DPRINT("SOFTGPU: RenderGdi ctx=%p cmdLen=%u\n",
           Ctx, pRenderGdi->CommandLength);
    return STATUS_SUCCESS;
}

/* EOF */
