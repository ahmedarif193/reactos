/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     DriverEntry and core DDI callbacks for softgpu.sys.
 *              Implements a WDDM 2.0 ABI miniport with a physical/software
 *              engine, no GPU MMU, and a 16 MB system-RAM framebuffer.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Target device: QEMU STD VGA (PCI VEN_1234&DEV_1111) or root-enumerated
 * virtual display adapter.
 *
 * Architecture notes (amd64/x86)
 * ================================
 * - No real hardware; all DDI entry points are stubs or trivial.
 * - Framebuffer: 16 MB MmAllocateContiguousMemorySpecifyCache(MmWriteCombined)
 *   slab.  Write-combining is optimal for framebuffer writes on x86 as it
 *   coalesces stores into PCIe bursts.
 * - Pool allocations use tag SOFTGPU_POOL_TAG ('SfGu') for pool filter tools.
 * - All DPRINT calls use the "SOFTGPU: " prefix for easy log filtering.
 */

/* INCLUDES ******************************************************************/

#include "softgpu.h"

/* CONSTANTS *****************************************************************/

/*
 * WDDM 2.0 (Windows 10) interface version.
 * dispmprt.h / d3dukmdt.h define DXGKDDI_INTERFACE_VERSION_WDDM2_0 = 0x5023.
 * softgpu builds against this version (see CMakeLists.txt) so its DDI table is
 * byte-identical to the one dxgkrnl consumes.  The Version field in
 * DRIVER_INITIALIZATION_DATA is set to the same value below.  Define the macro
 * defensively in case a future SDK trim removes it.
 */
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_0
#define DXGKDDI_INTERFACE_VERSION_WDDM2_0 0x5023
#endif

/* Default display geometry */
#define SOFTGPU_DEFAULT_WIDTH   1024
#define SOFTGPU_DEFAULT_HEIGHT  768
#define SOFTGPU_DEFAULT_FORMAT  D3DDDIFMT_A8R8G8B8  /* 32-bpp ARGB */

/* SOFTGPU_FB_SIZE and SOFTGPU_SEGMENT_ID are defined in softgpu.h */

/* =========================================================================
 * SOFTGPU_DRIVERCAPS
 *
 * Inline capability record returned by
 * DxgkDdiQueryAdapterInfo(DXGKQAITYPE_DRIVERCAPS).
 * ========================================================================= */

/*
 * SOFTGPU_DRIVER_CAPS_FILLED
 *
 * Statically-initialised DXGK_DRIVERCAPS advertised to dxgkrnl.
 * All capability fields left at zero except what softgpu actually supports:
 *   - MaxPointerWidth/Height: 64x64 hardware-cursor placeholder (unused)
 *   - MaxAllocationListSlotId: 255
 *   - GpuEngineTopology: 1 (one 3D engine node)
 *   - WDDMVersion: WDDM 2.0, matching the registered DDI table
 */
static const DXGK_DRIVERCAPS SOFTGPU_DRIVER_CAPS =
{
    /* HighestAcceptableAddress: accept any physical address (64-bit clean) */
    .HighestAcceptableAddress.QuadPart   = (LONGLONG)(-1),
    .MaxPointerWidth            = 64,
    .MaxPointerHeight           = 64,
    /* PointerCaps.Value = 0: no hardware cursor */
    .MaxAllocationListSlotId    = 255,
    .ApertureSegmentCommitLimit = SOFTGPU_FB_SIZE,
    /* PresentationCaps.Value = 0 */
    .MaxOverlays                = 0,
    .GammaRampCaps              = 0,
    .SchedulingCaps.Value       = 0,
    .MemoryManagementCaps.Value = 0,
    .GpuEngineTopology.NbAsymetricProcessingNodes = 1,
    .WDDMVersion                = DXGKDDI_WDDMv2_ENUM,
};


/* =========================================================================
 * DriverEntry
 * =========================================================================
 */

/*
 * DriverEntry
 *
 * Standard WDM DriverEntry.  Fills in the DRIVER_INITIALIZATION_DATA table
 * and calls DxgkInitialize which hooks the driver into the WDDM framework.
 *
 * All callbacks that softgpu does not implement are left NULL; dxgkrnl will
 * either default them or return STATUS_NOT_SUPPORTED to the caller.
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    DRIVER_INITIALIZATION_DATA InitData;
    NTSTATUS                   Status;

    DPRINT("SOFTGPU: DriverEntry DriverObject=%p RegistryPath=%wZ\n",
           DriverObject, RegistryPath);

    RtlZeroMemory(&InitData, sizeof(InitData));

    /*
     * The WDDM 2.0 version selects the ABI table consumed by dxgkrnl. The
     * physical/software engine does not advertise GPU-MMU or IOMMU support.
     */
    InitData.Version = DXGKDDI_INTERFACE_VERSION_WDDM2_0;

    /* --- Adapter lifecycle ----------------------------------------------- */
    InitData.DxgkDdiAddDevice                       = SoftGpuDdiAddDevice;
    InitData.DxgkDdiStartDevice                     = SoftGpuDdiStartDevice;
    InitData.DxgkDdiStopDevice                      = SoftGpuDdiStopDevice;
    InitData.DxgkDdiRemoveDevice                    = SoftGpuDdiRemoveDevice;
    InitData.DxgkDdiResetDevice                     = SoftGpuDdiResetDevice;
    InitData.DxgkDdiSetPowerState                   = SoftGpuDdiSetPowerState;

    /* --- Adapter information --------------------------------------------- */
    InitData.DxgkDdiQueryAdapterInfo                = SoftGpuDdiQueryAdapterInfo;
    InitData.DxgkDdiQueryChildRelations             = SoftGpuDdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus                = SoftGpuDdiQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor           = SoftGpuDdiQueryDeviceDescriptor;
    InitData.DxgkDdiGetNodeMetadata                 = SoftGpuDdiGetNodeMetadata;

    /* --- Interrupt / DPC ------------------------------------------------- */
    InitData.DxgkDdiInterruptRoutine                = SoftGpuDdiInterruptRoutine;
    InitData.DxgkDdiDpcRoutine                      = SoftGpuDdiDpcRoutine;

    /* --- Memory management ----------------------------------------------- */
    InitData.DxgkDdiCreateAllocation                = SoftGpuDdiCreateAllocation;
    InitData.DxgkDdiDestroyAllocation               = SoftGpuDdiDestroyAllocation;
    InitData.DxgkDdiOpenAllocation                  = SoftGpuDdiOpenAllocation;
    InitData.DxgkDdiCloseAllocation                 = SoftGpuDdiCloseAllocation;
    InitData.DxgkDdiBuildPagingBuffer               = SoftGpuDdiBuildPagingBuffer;
    InitData.DxgkDdiPatch                           = SoftGpuDdiPatch;

    /* --- Command scheduling --------------------------------------------- */
    InitData.DxgkDdiSubmitCommand                   = SoftGpuDdiSubmitCommand;
    InitData.DxgkDdiQueryCurrentFence               = SoftGpuDdiQueryCurrentFence;
    InitData.DxgkDdiResetFromTimeout                = SoftGpuDdiResetFromTimeout;
    InitData.DxgkDdiRestartFromTimeout              = SoftGpuDdiRestartFromTimeout;

    /* --- VidPN management ----------------------------------------------- */
    InitData.DxgkDdiIsSupportedVidPn                = SoftGpuDdiIsSupportedVidPn;
    InitData.DxgkDdiRecommendFunctionalVidPn        = SoftGpuDdiRecommendFunctionalVidPn;
    InitData.DxgkDdiEnumVidPnCofuncModality         = SoftGpuDdiEnumVidPnCofuncModality;
    InitData.DxgkDdiSetVidPnSourceAddress           = SoftGpuDdiSetVidPnSourceAddress;
    InitData.DxgkDdiSetVidPnSourceVisibility        = SoftGpuDdiSetVidPnSourceVisibility;
    InitData.DxgkDdiCommitVidPn                     = SoftGpuDdiCommitVidPn;
    InitData.DxgkDdiUpdateActiveVidPnPresentPath    = SoftGpuDdiUpdateActiveVidPnPresentPath;
    InitData.DxgkDdiRecommendMonitorModes           = SoftGpuDdiRecommendMonitorModes;
    InitData.DxgkDdiRecommendVidPnTopology          = SoftGpuDdiRecommendVidPnTopology;

    /* --- Cursor / scanline ----------------------------------------------- */
    InitData.DxgkDdiSetPointerPosition              = SoftGpuDdiSetPointerPosition;
    InitData.DxgkDdiSetPointerShape                 = SoftGpuDdiSetPointerShape;
    InitData.DxgkDdiSetPalette                      = SoftGpuDdiSetPalette;
    InitData.DxgkDdiGetScanLine                     = SoftGpuDdiGetScanLine;

    /* --- Per-device / context -------------------------------------------- */
    InitData.DxgkDdiCreateDevice                    = SoftGpuDdiCreateDevice;
    InitData.DxgkDdiDestroyDevice                   = SoftGpuDdiDestroyDevice;
    InitData.DxgkDdiCreateContext                   = SoftGpuDdiCreateContext;
    InitData.DxgkDdiDestroyContext                  = SoftGpuDdiDestroyContext;

    /* --- Interrupt control ----------------------------------------------- */
    InitData.DxgkDdiControlInterrupt                = SoftGpuDdiControlInterrupt;

    /* --- WDDM 2.0 additions ---------------------------------------------- *
     * Only the fields that actually exist in DRIVER_INITIALIZATION_DATA at
     * DXGKDDI_INTERFACE_VERSION_WDDM2_0 (0x5023) are wired (see dispmprt.h
     * lines 1564-1580).  The GPU virtual-addressing DDIs (CreateProcess,
     * DestroyProcess, GetRootPageTableSize, SetRootPageTable, Map/Unmap CPU
     * host aperture) are the ones dxgkrnl's gpuva.c invokes; SubmitCommand-
     * Virtual and RenderGdi are wired for Win11 DDI completeness.  The MPO,
     * power-runtime and protected-region DDIs are left NULL (not needed by a
     * software/null GPU and not called by dxgkrnl).  Hardware-queue /
     * MapGpuVirtualAddresses / monitored-fence DDIs do NOT exist at 0x5023 in
     * this SDK, so they are intentionally not referenced. */
    InitData.DxgkDdiCreateProcess                   = SoftGpuDdiCreateProcess;
    InitData.DxgkDdiDestroyProcess                  = SoftGpuDdiDestroyProcess;
    InitData.DxgkDdiGetRootPageTableSize            = SoftGpuDdiGetRootPageTableSize;
    InitData.DxgkDdiSetRootPageTable                = SoftGpuDdiSetRootPageTable;
    InitData.DxgkDdiMapCpuHostAperture             = SoftGpuDdiMapCpuHostAperture;
    InitData.DxgkDdiUnmapCpuHostAperture           = SoftGpuDdiUnmapCpuHostAperture;
    InitData.DxgkDdiSubmitCommandVirtual           = SoftGpuDdiSubmitCommandVirtual;
    InitData.DxgkDdiRenderGdi                      = SoftGpuDdiRenderGdi;

    Status = DxgkInitialize(DriverObject, RegistryPath, &InitData);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SOFTGPU: DxgkInitialize failed 0x%08lx\n", Status);
        return Status;
    }

    DPRINT("SOFTGPU: DriverEntry success\n");
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiAddDevice
 * =========================================================================
 */

/*
 * SoftGpuDdiAddDevice
 *
 * Called by dxgkrnl when a matching PCI device is found.  Allocates the
 * per-adapter SOFTGPU_DEVICE context and the 16 MB framebuffer slab.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiAddDevice(
    _In_  PDEVICE_OBJECT  PhysicalDeviceObject,
    _Out_ PVOID          *MiniportDeviceContext)
{
    PSOFTGPU_DEVICE        Device;
    PHYSICAL_ADDRESS       LowAddress;
    PHYSICAL_ADDRESS       HighAddress;
    PHYSICAL_ADDRESS       SkipBytes;

    DPRINT("SOFTGPU: AddDevice PDO=%p\n", PhysicalDeviceObject);

    if (MiniportDeviceContext == NULL)
        return STATUS_INVALID_PARAMETER;

    *MiniportDeviceContext = NULL;

    if (PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Allocate the per-adapter device context. */
    Device = (PSOFTGPU_DEVICE)ExAllocatePoolWithTag(NonPagedPool,
                                                     sizeof(SOFTGPU_DEVICE),
                                                     SOFTGPU_POOL_TAG);
    if (Device == NULL)
    {
        DPRINT1("SOFTGPU: AddDevice: failed to allocate SOFTGPU_DEVICE\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Device, sizeof(SOFTGPU_DEVICE));
    Device->Magic           = SOFTGPU_DEVICE_MAGIC;
    Device->FrameBufferSize = SOFTGPU_FB_SIZE;
    Device->Width           = SOFTGPU_DEFAULT_WIDTH;
    Device->Height          = SOFTGPU_DEFAULT_HEIGHT;
    Device->Format          = SOFTGPU_DEFAULT_FORMAT;
    Device->Stopped         = 1;

    KeInitializeSpinLock(&Device->FenceLock);

    /*
     * Allocate the 16 MB write-combined contiguous framebuffer.
     *
     * We allow any physical address (LowAddress = 0, HighAddress = max).
     * MmWriteCombined caching is correct for framebuffer usage: writes are
     * coalesced before being flushed to memory, matching GPU framebuffer
     * access semantics.  On AMD64 this maps via PAT entries with WC type.
     *
     * SkipBytes = 0: no alignment beyond what the allocator provides.
     */
    LowAddress.QuadPart  = 0;
    HighAddress.QuadPart = (LONGLONG)-1;
    SkipBytes.QuadPart   = 0;

    Device->FrameBuffer = MmAllocateContiguousMemorySpecifyCache(
                              Device->FrameBufferSize,
                              LowAddress,
                              HighAddress,
                              SkipBytes,
                              MmWriteCombined);

    if (Device->FrameBuffer == NULL)
    {
        DPRINT1("SOFTGPU: AddDevice: failed to allocate %Iu-byte framebuffer\n",
                Device->FrameBufferSize);
        ExFreePoolWithTag(Device, SOFTGPU_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Record the physical address for segment reporting. */
    Device->FrameBufferPhys = MmGetPhysicalAddress(Device->FrameBuffer);

    DPRINT("SOFTGPU: AddDevice: framebuffer virt=%p phys=0x%I64x size=%Iu\n",
           Device->FrameBuffer,
           Device->FrameBufferPhys.QuadPart,
           Device->FrameBufferSize);

    /* Clear the framebuffer to black. */
    RtlZeroMemory(Device->FrameBuffer, Device->FrameBufferSize);

    *MiniportDeviceContext = Device;
    DPRINT("SOFTGPU: AddDevice success Device=%p\n", Device);
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiStartDevice
 * =========================================================================
 */

/*
 * SoftGpuDdiStartDevice
 *
 * Called after AddDevice once PnP resources have been assigned.  Saves the
 * dxgkrnl callback vtable (DXGK_INTERFACE) and reports topology.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiStartDevice(
    _In_  PVOID             MiniportDeviceContext,
    _In_  PDXGK_START_INFO  DxgkStartInfo,
    _In_  PDXGK_INTERFACE   DxgkInterface,
    _Out_ PULONG            NumberOfVideoPresentSources,
    _Out_ PULONG            NumberOfChildren)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    KIRQL OldIrql;

    DPRINT("SOFTGPU: StartDevice Device=%p\n", Device);

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL ||
        NumberOfChildren == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbNotifyInterrupt == NULL ||
        DxgkInterface->DxgkCbNotifyDpc == NULL)
    {
        DPRINT1("SOFTGPU: StartDevice: incomplete DxgkInterface\n");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Basic-Display fallback gate.  softgpu presents to the firmware boot
     * framebuffer, so it only starts when it can acquire POST display
     * ownership through the documented WDDM 1.2+ callback.  dxgkrnl returns
     * an empty descriptor when there is no usable boot framebuffer OR when a
     * real GPU miniport (viogpudo, rpi5vc4) already owns the boot display —
     * in both cases decline so softgpu never becomes a competing second
     * display adapter.  Conversely, when softgpu started first, a real
     * miniport's later acquire makes dxgkrnl stop this adapter (the MSBDD
     * handover), so the fallback also yields after the fact.
     */
    {
        DXGK_DISPLAY_INFORMATION PostDisplayInfo;

        RtlZeroMemory(&PostDisplayInfo, sizeof(PostDisplayInfo));
        if (DxgkInterface->DxgkCbAcquirePostDisplayOwnership == NULL ||
            !NT_SUCCESS(DxgkInterface->DxgkCbAcquirePostDisplayOwnership(
                            DxgkInterface->DeviceHandle, &PostDisplayInfo)) ||
            PostDisplayInfo.Width == 0 ||
            PostDisplayInfo.Height == 0 ||
            PostDisplayInfo.PhysicAddress.QuadPart == 0)
        {
            DPRINT1("SOFTGPU: no acquirable boot framebuffer -- declining "
                    "(none exists, or a real GPU miniport owns the display)\n");
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        Device->Width  = PostDisplayInfo.Width;
        Device->Height = PostDisplayInfo.Height;
    }

    /* Save the dxgkrnl callback vtable for later use by the DPC. */
    RtlCopyMemory(&Device->DxgkInterface, DxgkInterface, sizeof(DXGK_INTERFACE));

    /* One display output source, one child device (the monitor). */
    Device->NumSources   = 1;
    Device->NumChildren  = 1;

    *NumberOfVideoPresentSources = Device->NumSources;
    *NumberOfChildren            = Device->NumChildren;

    /*
     * Initialise the DPC object used to simulate asynchronous GPU completion.
     * The DPC is queued from SoftGpuDdiSubmitCommand at DISPATCH_LEVEL and
     * fires SoftGpuDpcRoutine which notifies dxgkrnl of fence completion.
     */
    KeInitializeDpc(&Device->DpcObject, SoftGpuDpcRoutine, Device);
    Device->DpcInitialized = TRUE;
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    Device->Stopped = 0;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    DPRINT("SOFTGPU: StartDevice success: %lu source(s), %lu child(ren)\n",
           Device->NumSources, Device->NumChildren);
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiStopDevice
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiStopDevice(
    _In_ PVOID MiniportDeviceContext)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    KIRQL OldIrql;

    DPRINT("SOFTGPU: StopDevice Device=%p\n", Device);

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    /* Serialize the stopped gate with every DPC insertion. Once the gate is
     * closed, remove queued work and wait for any running callback to return. */
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    Device->Stopped = 1;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
    if (Device->DpcInitialized)
    {
        KeRemoveQueueDpc(&Device->DpcObject);
        KeFlushQueuedDpcs();
        Device->DpcInitialized = FALSE;
    }
    RtlZeroMemory(&Device->DxgkInterface, sizeof(Device->DxgkInterface));

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiRemoveDevice
 * =========================================================================
 */

/*
 * SoftGpuDdiRemoveDevice
 *
 * Free the framebuffer and the SOFTGPU_DEVICE context.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    NTSTATUS Status;

    DPRINT("SOFTGPU: RemoveDevice Device=%p\n", Device);

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    Status = SoftGpuDdiStopDevice(Device);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Device->FrameBuffer != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Device->FrameBuffer,
                                           Device->FrameBufferSize,
                                           MmWriteCombined);
        Device->FrameBuffer = NULL;
    }

    /* Poison the magic field so dangling-pointer dereferences are detectable. */
    Device->Magic = 0xDEADDEADUL;
    ExFreePoolWithTag(Device, SOFTGPU_POOL_TAG);

    DPRINT("SOFTGPU: RemoveDevice done\n");
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiQueryAdapterInfo
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiGetNodeMetadata(
    _In_ PVOID MiniportDeviceContext,
    _In_ UINT NodeOrdinalAndAdapterIndex,
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC || GetNodeMetadata == NULL)
        return STATUS_INVALID_PARAMETER;
    if (NodeOrdinalAndAdapterIndex != 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(GetNodeMetadata, sizeof(*GetNodeMetadata));
    GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    RtlCopyMemory(GetNodeMetadata->FriendlyName, L"ReactOS software GPU", sizeof(L"ReactOS software GPU"));
    GetNodeMetadata->GpuMmuSupported = FALSE;
    GetNodeMetadata->IoMmuSupported = FALSE;
    return STATUS_SUCCESS;
}

/*
 * SoftGpuDdiQueryAdapterInfo
 *
 * Handles the three query types that dxgkrnl issues during adapter
 * initialisation:
 *
 *   DXGKQAITYPE_UMDRIVERPRIVATE  — zero-fill the output buffer.
 *   DXGKQAITYPE_DRIVERCAPS       — fill DXGK_DRIVERCAPS.
 *   DXGKQAITYPE_QUERYSEGMENT     — two-phase protocol:
 *       Phase 1: pSegmentDescriptor == NULL → set NbSegment = 1.
 *       Phase 2: pSegmentDescriptor != NULL → fill one descriptor.
 *
 * IRQL: PASSIVE_LEVEL
 */
/* The QUERYSEGMENT flavours share field names; fill them uniformly.
 * Count pass (pSegmentDescriptor == NULL): */
#define SOFTGPU_FILL_SEGMENT_COUNTS(pOut)                                   \
    do {                                                                    \
        (pOut)->NbSegment                   = 1;                            \
        (pOut)->PagingBufferSegmentId       = SOFTGPU_SEGMENT_ID;           \
        (pOut)->PagingBufferSize            = 64 * 1024;                    \
        (pOut)->PagingBufferPrivateDataSize = 0;                            \
    } while (0)

/* Fill pass: one CPU-visible aperture segment over the framebuffer slab. */
#define SOFTGPU_FILL_SEGMENT_DESC(pDesc, Dev)                               \
    do {                                                                    \
        RtlZeroMemory((pDesc), sizeof(*(pDesc)));                           \
        (pDesc)->Flags.Aperture                  = 1;                       \
        (pDesc)->Flags.CpuVisible                = 1;                       \
        (pDesc)->Flags.PopulatedFromSystemMemory = 1;                       \
        (pDesc)->BaseAddress.QuadPart          = (Dev)->FrameBufferPhys.QuadPart; \
        (pDesc)->CpuTranslatedAddress.QuadPart = (Dev)->FrameBufferPhys.QuadPart; \
        (pDesc)->Size                          = (Dev)->FrameBufferSize;    \
        (pDesc)->CommitLimit                   = (Dev)->FrameBufferSize;    \
    } while (0)

NTSTATUS
APIENTRY
SoftGpuDdiQueryAdapterInfo(
    _In_ PVOID                             MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO   *pQueryAdapterInfo)
{
    PSOFTGPU_DEVICE     Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PDXGK_QUERYSEGMENTOUT pSegOut;
    PDXGK_SEGMENTDESCRIPTOR pDesc;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pQueryAdapterInfo == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: QueryAdapterInfo Type=%d\n",
           (int)pQueryAdapterInfo->Type);

    switch (pQueryAdapterInfo->Type)
    {
    case DXGKQAITYPE_UMDRIVERPRIVATE:
        /*
         * User-mode driver private data.  softgpu has no UMD, so we return
         * a zero buffer.  The output size is determined by the UMD; since we
         * have none, just zero whatever dxgkrnl provided.
         */
        if (pQueryAdapterInfo->pOutputData != NULL &&
            pQueryAdapterInfo->OutputDataSize > 0)
        {
            RtlZeroMemory(pQueryAdapterInfo->pOutputData,
                          pQueryAdapterInfo->OutputDataSize);
        }
        return STATUS_SUCCESS;

    case DXGKQAITYPE_DRIVERCAPS:
        /*
         * Copy the statically-initialised capability record.
         * dxgkrnl will have allocated a buffer of sizeof(DXGK_DRIVERCAPS).
         */
        if (pQueryAdapterInfo->pOutputData == NULL)
            return STATUS_INVALID_PARAMETER;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
        {
            DPRINT1("SOFTGPU: QueryAdapterInfo DRIVERCAPS: buffer too small "
                    "(%u < %Iu)\n",
                    pQueryAdapterInfo->OutputDataSize,
                    sizeof(DXGK_DRIVERCAPS));
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlCopyMemory(pQueryAdapterInfo->pOutputData,
                      &SOFTGPU_DRIVER_CAPS,
                      sizeof(DXGK_DRIVERCAPS));
        return STATUS_SUCCESS;

    case DXGKQAITYPE_QUERYSEGMENT:
    {
        /*
         * QUERYSEGMENT two-phase protocol.
         *
         * softgpu exposes a single CPU-visible aperture segment backed by
         * the 16 MB write-combined contiguous buffer.
         *
         * Segment flags:
         *   Aperture              = 1: segment is a GPU-accessible mapping
         *                             of system memory (no real VRAM).
         *   CpuVisible            = 1: CPU can access directly.
         *   PopulatedFromSystemMemory = 1: backed by system RAM.
         *
         * BaseAddress and CpuTranslatedAddress both point to the physical
         * base of our framebuffer slab.
         */
        if (pQueryAdapterInfo->pOutputData == NULL)
            return STATUS_INVALID_PARAMETER;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT))
        {
            DPRINT1("SOFTGPU: QueryAdapterInfo QUERYSEGMENT: output buffer "
                    "too small (%u < %Iu)\n",
                    pQueryAdapterInfo->OutputDataSize,
                    sizeof(DXGK_QUERYSEGMENTOUT));
            return STATUS_BUFFER_TOO_SMALL;
        }

        pSegOut = (PDXGK_QUERYSEGMENTOUT)pQueryAdapterInfo->pOutputData;

        if (pSegOut->pSegmentDescriptor == NULL)
        {
            /* Phase 1: report segment count. */
            DPRINT("SOFTGPU: QueryAdapterInfo QUERYSEGMENT phase 1: "
                   "reporting 1 segment\n");
            SOFTGPU_FILL_SEGMENT_COUNTS(pSegOut);
            return STATUS_SUCCESS;
        }

        /* Phase 2: fill the descriptor array.  NbSegment must be >= 1. */
        if (pSegOut->NbSegment < 1)
        {
            DPRINT1("SOFTGPU: QueryAdapterInfo QUERYSEGMENT phase 2: "
                    "NbSegment=%u\n", pSegOut->NbSegment);
            return STATUS_INVALID_PARAMETER;
        }

        DPRINT("SOFTGPU: QueryAdapterInfo QUERYSEGMENT phase 2: "
               "filling %u segment(s)\n", pSegOut->NbSegment);

        pDesc = &pSegOut->pSegmentDescriptor[0];
        SOFTGPU_FILL_SEGMENT_DESC(pDesc, Device);

        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT4:
    {
        /* WDDM 2.0 flavour: stride-addressed descriptor array. */
        PDXGK_QUERYSEGMENTOUT4 pSegOut4;
        PDXGK_SEGMENTDESCRIPTOR4 pDesc4;

        if (pQueryAdapterInfo->pOutputData == NULL)
            return STATUS_INVALID_PARAMETER;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4))
            return STATUS_BUFFER_TOO_SMALL;

        pSegOut4 = (PDXGK_QUERYSEGMENTOUT4)pQueryAdapterInfo->pOutputData;

        if (pSegOut4->pSegmentDescriptor == NULL)
        {
            SOFTGPU_FILL_SEGMENT_COUNTS(pSegOut4);
            return STATUS_SUCCESS;
        }

        if (pSegOut4->NbSegment < 1 ||
            pSegOut4->SegmentDescriptorStride < sizeof(DXGK_SEGMENTDESCRIPTOR4))
        {
            return STATUS_INVALID_PARAMETER;
        }

        pDesc4 = (PDXGK_SEGMENTDESCRIPTOR4)pSegOut4->pSegmentDescriptor;
        SOFTGPU_FILL_SEGMENT_DESC(pDesc4, Device);

        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT3:
    {
        /* WDDM 2.x flavour of QUERYSEGMENT (distinct descriptor layout). */
        PDXGK_QUERYSEGMENTOUT3 pSegOut3;
        PDXGK_SEGMENTDESCRIPTOR3 pDesc3;

        if (pQueryAdapterInfo->pOutputData == NULL)
            return STATUS_INVALID_PARAMETER;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT3))
            return STATUS_BUFFER_TOO_SMALL;

        pSegOut3 = (PDXGK_QUERYSEGMENTOUT3)pQueryAdapterInfo->pOutputData;

        if (pSegOut3->pSegmentDescriptor == NULL)
        {
            SOFTGPU_FILL_SEGMENT_COUNTS(pSegOut3);
            return STATUS_SUCCESS;
        }

        if (pSegOut3->NbSegment < 1)
            return STATUS_INVALID_PARAMETER;

        pDesc3 = &pSegOut3->pSegmentDescriptor[0];
        SOFTGPU_FILL_SEGMENT_DESC(pDesc3, Device);

        return STATUS_SUCCESS;
    }

    default:
        DPRINT("SOFTGPU: QueryAdapterInfo: unhandled type %d\n",
               (int)pQueryAdapterInfo->Type);
        return STATUS_NOT_SUPPORTED;
    }
}


/* =========================================================================
 * DxgkDdiCreateDevice / DxgkDdiDestroyDevice
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiCreateDevice(
    _In_    PVOID                 MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEDEVICE CreateDevice)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        CreateDevice == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: CreateDevice hDevice=%p\n", CreateDevice->hDevice);
    /* No per-device state; return the dxgkrnl handle unchanged. */
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
SoftGpuDdiDestroyDevice(
    _In_ PVOID MiniportDeviceContext)
{
    DPRINT("SOFTGPU: DestroyDevice context=%p\n", MiniportDeviceContext);
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiCreateAllocation / DxgkDdiDestroyAllocation
 * =========================================================================
 */

/*
 * SoftGpuDdiCreateAllocation
 *
 * Allocates a per-allocation SOFTGPU_ALLOC context and fills in the
 * DXGK_ALLOCATIONINFO fields that dxgkrnl needs for placement decisions.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiCreateAllocation(
    _In_    PVOID                      MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEALLOCATION  CreateAllocation)
{
    ULONG               i;
    PSOFTGPU_ALLOC      Alloc;
    DXGK_ALLOCATIONINFO *pInfo;

    if (MiniportDeviceContext == NULL ||
        ((PSOFTGPU_DEVICE)MiniportDeviceContext)->Magic != SOFTGPU_DEVICE_MAGIC ||
        CreateAllocation == NULL ||
        (CreateAllocation->NumAllocations != 0 &&
         CreateAllocation->pAllocationInfo == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: CreateAllocation NumAllocations=%u\n",
           CreateAllocation->NumAllocations);

    for (i = 0; i < CreateAllocation->NumAllocations; i++)
    {
        pInfo = &CreateAllocation->pAllocationInfo[i];

        if (pInfo->Size > SOFTGPU_FB_SIZE)
        {
            while (i > 0)
            {
                --i;
                ExFreePoolWithTag(
                    (PVOID)CreateAllocation->pAllocationInfo[i].hAllocation,
                    SOFTGPU_POOL_TAG);
                CreateAllocation->pAllocationInfo[i].hAllocation = NULL;
            }
            return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
        }

        Alloc = (PSOFTGPU_ALLOC)ExAllocatePoolWithTag(NonPagedPool,
                                                        sizeof(SOFTGPU_ALLOC),
                                                        SOFTGPU_POOL_TAG);
        if (Alloc == NULL)
        {
            DPRINT1("SOFTGPU: CreateAllocation: alloc[%lu] pool alloc failed\n",
                    i);
            /* Free allocations created so far. */
            while (i > 0)
            {
                --i;
                ExFreePoolWithTag(
                    (PVOID)CreateAllocation->pAllocationInfo[i].hAllocation,
                    SOFTGPU_POOL_TAG);
                CreateAllocation->pAllocationInfo[i].hAllocation = NULL;
            }
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(Alloc, sizeof(SOFTGPU_ALLOC));
        Alloc->Magic = SOFTGPU_ALLOC_MAGIC;

        /*
         * Infer surface geometry from private driver data if provided by the
         * user-mode driver.  For now treat every allocation as an opaque blob
         * with the size given by the pInfo fields.  dxgkrnl will have filled
         * in pPrivateDriverData / PrivateDriverDataSize before the call.
         */
        Alloc->Size   = (pInfo->Size != 0) ? pInfo->Size : PAGE_SIZE;
        if (Alloc->Size > SOFTGPU_FB_SIZE)
        {
            ExFreePoolWithTag(Alloc, SOFTGPU_POOL_TAG);
            return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
        }

        Alloc->Size   = (Alloc->Size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        Alloc->Format = D3DDDIFMT_A8R8G8B8;    /* default */

        /* Fill in DXGK_ALLOCATIONINFO fields for dxgkrnl placement. */
        pInfo->Size                    = Alloc->Size;
        pInfo->Alignment               = PAGE_SIZE;
        pInfo->SupportedReadSegmentSet = (1 << (SOFTGPU_SEGMENT_ID - 1));
        pInfo->SupportedWriteSegmentSet= (1 << (SOFTGPU_SEGMENT_ID - 1));
        pInfo->EvictionSegmentSet      = 0;     /* no eviction for aperture */
        pInfo->Flags.CpuVisible        = 1;
        pInfo->Flags.PermanentSysMem   = 1;     /* stays in system memory   */
        pInfo->Flags.AccessedPhysically= 1;
        pInfo->hAllocation             = (HANDLE)Alloc;

        DPRINT("SOFTGPU: CreateAllocation alloc[%lu]: size=%Iu handle=%p\n",
               i, Alloc->Size, Alloc);
    }

    return STATUS_SUCCESS;
}

/*
 * SoftGpuDdiDestroyAllocation
 *
 * Frees each SOFTGPU_ALLOC context, verifying the magic number first.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiDestroyAllocation(
    _In_ PVOID                             MiniportDeviceContext,
    _In_ CONST DXGKARG_DESTROYALLOCATION  *DestroyAllocation)
{
    ULONG          i;
    PSOFTGPU_ALLOC Alloc;

    if (MiniportDeviceContext == NULL ||
        ((PSOFTGPU_DEVICE)MiniportDeviceContext)->Magic != SOFTGPU_DEVICE_MAGIC ||
        DestroyAllocation == NULL ||
        (DestroyAllocation->NumAllocations != 0 &&
         DestroyAllocation->phAllocation == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: DestroyAllocation NumAllocations=%u\n",
           DestroyAllocation->NumAllocations);

    for (i = 0; i < DestroyAllocation->NumAllocations; i++)
    {
        Alloc = (PSOFTGPU_ALLOC)DestroyAllocation->phAllocation[i];
        if (Alloc == NULL)
            continue;

        if (Alloc->Magic != SOFTGPU_ALLOC_MAGIC)
        {
            DPRINT1("SOFTGPU: DestroyAllocation alloc[%lu]=%p: bad magic "
                    "0x%08lx\n", i, Alloc, Alloc->Magic);
            return STATUS_INVALID_PARAMETER;
        }
    }

    for (i = 0; i < DestroyAllocation->NumAllocations; i++)
    {
        Alloc = (PSOFTGPU_ALLOC)DestroyAllocation->phAllocation[i];
        if (Alloc == NULL)
            continue;

        Alloc->Magic = 0xDEADA110UL;   /* poison */
        ExFreePoolWithTag(Alloc, SOFTGPU_POOL_TAG);
    }

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiOpenAllocation / DxgkDdiCloseAllocation
 * =========================================================================
 */

/*
 * SoftGpuDdiOpenAllocation
 *
 * Creates a per-device SOFTGPU_OPENALLOC binding for each opened allocation.
 * A software adapter has no per-device GPU state, so the binding only records
 * the dxgkrnl allocation handle for later validation in CloseAllocation.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiOpenAllocation(
    _In_ PVOID                          hDevice,
    _In_ CONST DXGKARG_OPENALLOCATION  *OpenAllocation)
{
    ULONG              i;
    PSOFTGPU_OPENALLOC Open;

    UNREFERENCED_PARAMETER(hDevice);

    if (OpenAllocation == NULL ||
        OpenAllocation->NumAllocations == 0 ||
        OpenAllocation->pOpenAllocation == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: OpenAllocation NumAllocations=%u\n",
           OpenAllocation->NumAllocations);

    for (i = 0; i < OpenAllocation->NumAllocations; i++)
    {
        DXGK_OPENALLOCATIONINFO *pInfo = &OpenAllocation->pOpenAllocation[i];

        Open = (PSOFTGPU_OPENALLOC)ExAllocatePoolWithTag(NonPagedPool,
                                                         sizeof(SOFTGPU_OPENALLOC),
                                                         SOFTGPU_POOL_TAG);
        if (Open == NULL)
        {
            while (i > 0)
            {
                --i;
                Open = (PSOFTGPU_OPENALLOC)
                    OpenAllocation->pOpenAllocation[i].hDeviceSpecificAllocation;
                OpenAllocation->pOpenAllocation[i].hDeviceSpecificAllocation = NULL;
                if (Open != NULL)
                {
                    Open->Magic = 0;
                    ExFreePoolWithTag(Open, SOFTGPU_POOL_TAG);
                }
            }
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(Open, sizeof(*Open));
        Open->Magic = SOFTGPU_OPENALLOC_MAGIC;
        Open->hAllocation = pInfo->hAllocation;
        pInfo->hDeviceSpecificAllocation = (HANDLE)Open;
    }

    return STATUS_SUCCESS;
}

/*
 * SoftGpuDdiCloseAllocation
 *
 * Frees each SOFTGPU_OPENALLOC binding, verifying the magic number first.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiCloseAllocation(
    _In_ PVOID                          hDevice,
    _In_ CONST DXGKARG_CLOSEALLOCATION *CloseAllocation)
{
    ULONG              i;
    PSOFTGPU_OPENALLOC Open;

    UNREFERENCED_PARAMETER(hDevice);

    if (CloseAllocation == NULL ||
        CloseAllocation->NumAllocations == 0 ||
        CloseAllocation->pOpenHandleList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: CloseAllocation NumAllocations=%u\n",
           CloseAllocation->NumAllocations);

    for (i = 0; i < CloseAllocation->NumAllocations; i++)
    {
        Open = (PSOFTGPU_OPENALLOC)CloseAllocation->pOpenHandleList[i];
        if (Open == NULL || Open->Magic != SOFTGPU_OPENALLOC_MAGIC)
        {
            DPRINT1("SOFTGPU: CloseAllocation open[%lu]=%p: bad binding\n",
                    i, Open);
            return STATUS_INVALID_PARAMETER;
        }
    }

    for (i = 0; i < CloseAllocation->NumAllocations; i++)
    {
        Open = (PSOFTGPU_OPENALLOC)CloseAllocation->pOpenHandleList[i];
        Open->Magic = 0xDEAD0A11UL;    /* poison */
        ExFreePoolWithTag(Open, SOFTGPU_POOL_TAG);
    }

    return STATUS_SUCCESS;
}


/* =========================================================================
 * Child device queries
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiQueryChildRelations(
    _In_  PVOID                  MiniportDeviceContext,
    _Out_ PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_  ULONG                  ChildRelationsSize)
{
    DPRINT("SOFTGPU: QueryChildRelations size=%lu\n", ChildRelationsSize);

    if (ChildRelations == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ChildRelationsSize < sizeof(DXGK_CHILD_DESCRIPTOR))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    /*
     * Report one child device: a video output (monitor connector).
     * ChildUid = 1 is an arbitrary stable identifier.
     * HpdAwareness = HpdAwarenessAlwaysConnected: the virtual monitor is
     * permanently attached and never raises hot-plug interrupts.  In the WDDM2
     * dispmprt.h layout HpdAwareness is a top-level DXGK_CHILD_CAPABILITIES
     * field (the .Type union now holds the video-output capabilities).
     */
    RtlZeroMemory(ChildRelations, sizeof(DXGK_CHILD_DESCRIPTOR));
    ChildRelations[0].ChildDeviceType                = TypeVideoOutput;
    ChildRelations[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    ChildRelations[0].ChildUid                       = 1;

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
SoftGpuDdiQueryChildStatus(
    _In_    PVOID              MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS ChildStatus,
    _In_    BOOLEAN            NonDestructiveOnly)
{
    if (ChildStatus == NULL)
        return STATUS_INVALID_PARAMETER;

    DPRINT("SOFTGPU: QueryChildStatus ChildUid=%lu Type=%d\n",
           ChildStatus->ChildUid, (int)ChildStatus->Type);

    if (ChildStatus->ChildUid != 1)
        return STATUS_INVALID_PARAMETER;

    if (ChildStatus->Type == StatusConnection)
    {
        /* The virtual monitor is always connected. */
        ChildStatus->HotPlug.Connected = TRUE;
    }

    return STATUS_SUCCESS;
}


/* =========================================================================
 * Timeout recovery stubs
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiResetFromTimeout(
    _In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
SoftGpuDdiRestartFromTimeout(
    _In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    return STATUS_SUCCESS;
}

VOID
APIENTRY
SoftGpuDdiResetDevice(
    _In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}


/* =========================================================================
 * Power management stub
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiSetPowerState(
    _In_ PVOID              MiniportDeviceContext,
    _In_ ULONG              DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION       ActionType)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ActionType);

    if (DeviceUid != 0 && DeviceUid != 1)
        return STATUS_INVALID_PARAMETER;

    if (DevicePowerState > PowerDeviceD3)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}


/* =========================================================================
 * Interrupt control
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiControlInterrupt(
    _In_ PVOID                     MiniportDeviceContext,
    _In_ CONST DXGK_INTERRUPT_TYPE InterruptType,
    _In_ BOOLEAN                   EnableInterrupt)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(EnableInterrupt);

    if (InterruptType != DXGK_INTERRUPT_TYPE_DMA_COMPLETED &&
        InterruptType != DXGK_INTERRUPT_TYPE_DMA_PREEMPTED)
    {
        return STATUS_NOT_SUPPORTED;
    }

    /* Completion is synthesized by the submit/preempt paths. */
    return STATUS_SUCCESS;
}

/* EOF */
