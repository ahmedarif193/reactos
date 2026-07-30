/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     DriverEntry and core DDI callbacks for softgpu.sys.
 *              Implements a configurable WDDM ABI miniport with a
 *              physical/software engine and a bounded system-RAM segment.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Target device: a platform provider supplies the adapter binding and
 * firmware-display contract; this file contains only the software engine.
 *
 * Architecture notes (amd64/x86)
 * ================================
 * - No real hardware; all DDI entry points are stubs or trivial.
 * - Framebuffer: bounded multi-surface
 *   MmAllocateContiguousMemorySpecifyCache(MmWriteCombined) segment.
 *   Write-combining is optimal for framebuffer writes on x86 as it coalesces
 *   stores into PCIe bursts.
 * - Pool allocations use tag SOFTGPU_POOL_TAG ('SfGu') for pool filter tools.
 * - All DPRINT calls use the "SOFTGPU: " prefix for easy log filtering.
 */

/* INCLUDES ******************************************************************/

#include "softgpu.h"
#include "softgpu_2d_core.h"
#include "vsync_policy_core.h"

/* CONSTANTS *****************************************************************/

/*
 * Exact interface selectors used by the configurable declaration ceiling.
 * The driver compiles against the newest audited headers so its local tables
 * are byte-identical to dxgkrnl's. InitData.Version follows the selected
 * target tier exactly; optional features are still negotiated independently.
 */
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_0
#define DXGKDDI_INTERFACE_VERSION_WDDM2_0 0x5023
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_1
#define DXGKDDI_INTERFACE_VERSION_WDDM2_1 0x6003
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_2
#define DXGKDDI_INTERFACE_VERSION_WDDM2_2 0x700A
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_3
#define DXGKDDI_INTERFACE_VERSION_WDDM2_3 0x8001
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_4
#define DXGKDDI_INTERFACE_VERSION_WDDM2_4 0x9006
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_5
#define DXGKDDI_INTERFACE_VERSION_WDDM2_5 0xA00B
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_6
#define DXGKDDI_INTERFACE_VERSION_WDDM2_6 0xB004
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_7
#define DXGKDDI_INTERFACE_VERSION_WDDM2_7 0xC004
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_8
#define DXGKDDI_INTERFACE_VERSION_WDDM2_8 0xD001
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM2_9
#define DXGKDDI_INTERFACE_VERSION_WDDM2_9 0xE003
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM3_0
#define DXGKDDI_INTERFACE_VERSION_WDDM3_0 0xF003
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM3_1
#define DXGKDDI_INTERFACE_VERSION_WDDM3_1 0x10004
#endif
#ifndef DXGKDDI_INTERFACE_VERSION_WDDM3_2
#define DXGKDDI_INTERFACE_VERSION_WDDM3_2 0x11007
#endif

#ifndef REACTOS_WDDM_TARGET_LEVEL
#define REACTOS_WDDM_TARGET_LEVEL 2000
#endif
#ifndef REACTOS_WDDM_TARGET_INTERFACE_VERSION
#define REACTOS_WDDM_TARGET_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_0
#endif

/*
 * softgpu is the compatibility fallback for every selectable WDDM build.
 * Below 2.0 it registers the exact selected DDI prefix. At and above each
 * completed implementation tier it advertises that exact compatible prefix.
 * Optional feature families remain independently negotiated and may be
 * disabled even when their declaration tail is present.
 */
#if (REACTOS_WDDM_TARGET_LEVEL < 2000)
#define SOFTGPU_DECLARED_INTERFACE_VERSION REACTOS_WDDM_TARGET_INTERFACE_VERSION
#elif (REACTOS_WDDM_TARGET_LEVEL < 2100)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_0
#elif (REACTOS_WDDM_TARGET_LEVEL < 2200)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_1
#elif (REACTOS_WDDM_TARGET_LEVEL < 2300)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_2
#elif (REACTOS_WDDM_TARGET_LEVEL < 2400)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_3
#elif (REACTOS_WDDM_TARGET_LEVEL < 2500)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_4
#elif (REACTOS_WDDM_TARGET_LEVEL < 2600)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_5
#elif (REACTOS_WDDM_TARGET_LEVEL < 2700)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_6
#elif (REACTOS_WDDM_TARGET_LEVEL < 2800)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_7
#elif (REACTOS_WDDM_TARGET_LEVEL < 2900)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_8
#elif (REACTOS_WDDM_TARGET_LEVEL < 3000)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_9
#elif (REACTOS_WDDM_TARGET_LEVEL < 3100)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM3_0
#elif (REACTOS_WDDM_TARGET_LEVEL < 3200)
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM3_1
#else
#define SOFTGPU_DECLARED_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM3_2
#endif

#if (REACTOS_WDDM_TARGET_LEVEL < 1200)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv1_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 1300)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv1_2_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2000)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv1_3_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2100)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2200)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_1_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2300)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_2_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2400)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_3_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2500)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_4_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2600)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_5_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2700)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_6_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2800)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_7_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 2900)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_8_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 3000)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv2_9_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 3100)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv3_0_ENUM
#elif (REACTOS_WDDM_TARGET_LEVEL < 3200)
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv3_1_ENUM
#else
#define SOFTGPU_DECLARED_WDDM_VERSION DXGKDDI_WDDMv3_2_ENUM
#endif

#define SOFTGPU_GPUMMU_END_TO_END 1
#define SOFTGPU_DIAGNOSTIC_PAYLOAD_VERSION 1

C_ASSERT((ULONG)DXGK_VSYNC_ENABLE == (ULONG)SoftGpuVsyncEnable);
C_ASSERT((ULONG)DXGK_VSYNC_DISABLE_KEEP_PHASE ==
         (ULONG)SoftGpuVsyncDisableKeepPhase);
C_ASSERT((ULONG)DXGK_VSYNC_DISABLE_NO_PHASE ==
         (ULONG)SoftGpuVsyncDisableNoPhase);

/* Framebuffer geometry limits and SOFTGPU_SEGMENT_ID are in softgpu.h. */

typedef struct _SOFTGPU_DIAGNOSTIC_PAYLOAD
{
    ULONG Size;
    ULONG Version;
    ULONG TargetLevel;
    ULONG Type;
    ULONG AdapterContextAvailable;
} SOFTGPU_DIAGNOSTIC_PAYLOAD, *PSOFTGPU_DIAGNOSTIC_PAYLOAD;

static NTSTATUS
SoftGpuAllocateFrameBuffer(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ ULONGLONG RequiredSize)
{
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS SkipBytes;
    PHYSICAL_ADDRESS NewPhysicalAddress;
    PVOID NewFrameBuffer;
    PVOID OldFrameBuffer;
    SIZE_T AllocationSize;
    SIZE_T OldFrameBufferSize;

    if (Device == NULL ||
        ((Device->FrameBuffer == NULL) !=
         (Device->FrameBufferSize == 0)) ||
        RequiredSize == 0 ||
        RequiredSize >
            (ULONGLONG)SOFTGPU_MAX_ALLOCATION_SLAB_SIZE ||
        RequiredSize >
            (ULONGLONG)MAXULONG_PTR - (PAGE_SIZE - 1))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    AllocationSize =
        (SIZE_T)((RequiredSize + PAGE_SIZE - 1) &
                 ~((ULONGLONG)PAGE_SIZE - 1));
    if (AllocationSize == 0 ||
        AllocationSize > SOFTGPU_MAX_ALLOCATION_SLAB_SIZE)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    if (Device->FrameBuffer != NULL &&
        Device->FrameBufferSize >= AllocationSize)
    {
        RtlZeroMemory(Device->FrameBuffer, Device->FrameBufferSize);
        return STATUS_SUCCESS;
    }

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = (LONGLONG)-1;
    SkipBytes.QuadPart = 0;
    NewFrameBuffer = MmAllocateContiguousMemorySpecifyCache(
                         AllocationSize,
                         LowAddress,
                         HighAddress,
                         SkipBytes,
                         MmWriteCombined);
    if (NewFrameBuffer == NULL)
    {
        DPRINT1("SOFTGPU: failed to allocate %Iu-byte framebuffer\n",
                AllocationSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NewFrameBuffer, AllocationSize);
    NewPhysicalAddress = MmGetPhysicalAddress(NewFrameBuffer);

    /*
     * Publish a complete replacement before releasing the prior stopped
     * segment.  If allocation failed above, the prior segment stayed valid
     * for RemoveDevice or a later StartDevice attempt.
     */
    OldFrameBuffer = Device->FrameBuffer;
    OldFrameBufferSize = Device->FrameBufferSize;
    Device->FrameBuffer = NewFrameBuffer;
    Device->FrameBufferPhys = NewPhysicalAddress;
    Device->FrameBufferSize = AllocationSize;

    if (OldFrameBuffer != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(OldFrameBuffer,
                                           OldFrameBufferSize,
                                           MmWriteCombined);
    }

    DPRINT("SOFTGPU: framebuffer virt=%p phys=0x%I64x size=%Iu "
           "(mode requires %I64u)\n",
           Device->FrameBuffer,
           Device->FrameBufferPhys.QuadPart,
           Device->FrameBufferSize,
           RequiredSize);
    return STATUS_SUCCESS;
}


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
 *   - WDDMVersion: the highest completed tier, matching the registered table
 */
static const DXGK_DRIVERCAPS SOFTGPU_DRIVER_CAPS =
{
    /* HighestAcceptableAddress: accept any physical address (64-bit clean) */
    .HighestAcceptableAddress.QuadPart   = (LONGLONG)(-1),
    .MaxPointerWidth            = 64,
    .MaxPointerHeight           = 64,
    /* PointerCaps.Value = 0: no hardware cursor */
    .MaxAllocationListSlotId    = 255,
    .ApertureSegmentCommitLimit = 0,
    /* PresentationCaps.Value = 0 */
    .MaxOverlays                = 0,
    .GammaRampCaps.Value        = 0,
    .SchedulingCaps.Value       = 0,
    .MemoryManagementCaps.Value = 0,
    .GpuEngineTopology.NbAsymetricProcessingNodes = 1,
    .WDDMVersion                = SOFTGPU_DECLARED_WDDM_VERSION,
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

    InitData.Version = SOFTGPU_DECLARED_INTERFACE_VERSION;

    /* --- Adapter lifecycle ----------------------------------------------- */
    InitData.DxgkDdiAddDevice                       = SoftGpuDdiAddDevice;
    InitData.DxgkDdiStartDevice                     = SoftGpuDdiStartDevice;
    InitData.DxgkDdiStopDevice                      = SoftGpuDdiStopDevice;
    InitData.DxgkDdiRemoveDevice                    = SoftGpuDdiRemoveDevice;
    InitData.DxgkDdiResetDevice                     = SoftGpuDdiResetDevice;
    InitData.DxgkDdiSetPowerState                   = SoftGpuDdiSetPowerState;
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    InitData.DxgkDdiQueryInterface                  =
        SoftGpuDdiQueryInterface;
#endif

    /* --- Adapter information --------------------------------------------- */
    InitData.DxgkDdiQueryAdapterInfo                = SoftGpuDdiQueryAdapterInfo;
    InitData.DxgkDdiQueryChildRelations             = SoftGpuDdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus                = SoftGpuDdiQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor           = SoftGpuDdiQueryDeviceDescriptor;
#if (REACTOS_WDDM_TARGET_LEVEL >= 1300)
    InitData.DxgkDdiGetNodeMetadata                 = SoftGpuDdiGetNodeMetadata;
#endif

    /* --- Interrupt / DPC ------------------------------------------------- */
    InitData.DxgkDdiInterruptRoutine                = SoftGpuDdiInterruptRoutine;
    InitData.DxgkDdiDpcRoutine                      = SoftGpuDdiDpcRoutine;

    /* --- Memory management ----------------------------------------------- */
    InitData.DxgkDdiCreateAllocation                = SoftGpuDdiCreateAllocation;
    InitData.DxgkDdiDestroyAllocation               = SoftGpuDdiDestroyAllocation;
    InitData.DxgkDdiOpenAllocation                  = SoftGpuDdiOpenAllocation;
    InitData.DxgkDdiCloseAllocation                 = SoftGpuDdiCloseAllocation;
    InitData.DxgkDdiGetStandardAllocationDriverData =
        SoftGpuDdiGetStandardAllocationDriverData;
    InitData.DxgkDdiBuildPagingBuffer               = SoftGpuDdiBuildPagingBuffer;
    InitData.DxgkDdiPatch                           = SoftGpuDdiPatch;
    InitData.DxgkDdiRender                          = SoftGpuDdiRender;
    InitData.DxgkDdiPresent                         = SoftGpuDdiPresent;

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
#if (REACTOS_WDDM_TARGET_LEVEL >= 1300)
    InitData.DxgkDdiControlInterrupt2               = SoftGpuDdiControlInterrupt2;
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 2600)
    InitData.DxgkDdiCollectDiagnosticInfo           =
        SoftGpuDdiCollectDiagnosticInfo;
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 2700)
    InitData.DxgkDdiControlInterrupt3               = SoftGpuDdiControlInterrupt3;
#endif

    /* --- WDDM 2.x additions ---------------------------------------------- *
     * The GPU virtual-addressing DDIs (CreateProcess, DestroyProcess,
     * GetRootPageTableSize, SetRootPageTable, and Map/Unmap CPU host aperture)
     * are the ones dxgkrnl's gpuva.c invokes. RenderGdi remains NULL because
     * this driver does not advertise or implement GDI hardware command-buffer
     * acceleration; returning success without translating the command would
     * lose drawing.
     *
     * At 2.2, regular monitored-fence interrupts are implemented by the
     * paging-buffer signal opcode and the type-11 completion handoff. This
     * software-scheduled adapter does not claim the optional hardware-queue,
     * periodic-frame, MPO, post-composition, or advanced display-control
     * features, so their callbacks remain NULL and their capability bits zero.
     */
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    InitData.DxgkDdiCreateProcess                   = SoftGpuDdiCreateProcess;
    InitData.DxgkDdiDestroyProcess                  = SoftGpuDdiDestroyProcess;
    InitData.DxgkDdiGetRootPageTableSize            = SoftGpuDdiGetRootPageTableSize;
    InitData.DxgkDdiSetRootPageTable                = SoftGpuDdiSetRootPageTable;
    InitData.DxgkDdiMapCpuHostAperture             = SoftGpuDdiMapCpuHostAperture;
    InitData.DxgkDdiUnmapCpuHostAperture           = SoftGpuDdiUnmapCpuHostAperture;
    InitData.DxgkDdiSubmitCommandVirtual           = SoftGpuDdiSubmitCommandVirtual;
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 2100)
    InitData.DxgkDdiValidateUpdateAllocationProperty =
        SoftGpuDdiValidateUpdateAllocationProperty;
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 2300)
    InitData.DxgkDdiSetTimingsFromVidPn =
        SoftGpuDdiSetTimingsFromVidPn;
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 3000)
    InitData.DxgkDdiEscape                         = SoftGpuDdiEscape;
    InitData.DxgkDdiCreateCpuEvent                 =
        SoftGpuDdiCreateCpuEvent;
    InitData.DxgkDdiDestroyCpuEvent                =
        SoftGpuDdiDestroyCpuEvent;
#endif

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
 * Called by dxgkrnl when a matching device is found.  Allocates the
 * per-adapter context; the linked platform provider validates the PDO and
 * StartDevice display contract before the common software engine starts.
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
    NTSTATUS               Status;

    DPRINT("SOFTGPU: AddDevice PDO=%p\n", PhysicalDeviceObject);

    if (MiniportDeviceContext == NULL)
        return STATUS_INVALID_PARAMETER;

    *MiniportDeviceContext = NULL;

    if (PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = SoftGpuPlatformValidatePdo(PhysicalDeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

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
    Device->Width           = SOFTGPU_DEFAULT_WIDTH;
    Device->Height          = SOFTGPU_DEFAULT_HEIGHT;
    Device->Format          = SOFTGPU_DEFAULT_FORMAT;
    Device->Stopped         = 1;
    Device->PhysicalDeviceObject = PhysicalDeviceObject;

    KeInitializeSpinLock(&Device->FenceLock);
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    KeInitializeEvent(&Device->FeatureInterfaceZeroEvent,
                      NotificationEvent,
                      TRUE);
#endif
    SoftGpuScanoutInitializeDevice(Device);

    *MiniportDeviceContext = Device;
    DPRINT("SOFTGPU: AddDevice success Device=%p\n", Device);
    return STATUS_SUCCESS;
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
static const GUID SoftGpuWddmFeatureInterfaceGuid =
{
    0x94bb3993, 0xc6c3, 0x4da7,
    { 0x89, 0x49, 0xa1, 0x13, 0x82, 0x32, 0xe7, 0x59 }
};

static BOOLEAN
SoftGpuAcquireFeatureInterface(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    KIRQL OldIrql;
    BOOLEAN Acquired = FALSE;

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->FeatureInterfaceQueriesOpen != 0)
    {
        if (InterlockedIncrement(
                &Device->FeatureInterfaceReferences) == 1)
        {
            KeClearEvent(&Device->FeatureInterfaceZeroEvent);
        }
        Acquired = TRUE;
    }
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    return Acquired;
}

static VOID
NTAPI
SoftGpuFeatureInterfaceReference(
    _In_ PVOID Context)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)Context;
    KIRQL OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
    {
        ASSERT(FALSE);
        return;
    }

    /*
     * InterfaceReference has no failure return. A valid caller already owns
     * one reference, so it may duplicate that reference while StopDevice is
     * waiting, but it may never resurrect a zero-reference interface.
     */
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->FeatureInterfaceReferences <= 0)
    {
        ASSERT(FALSE);
    }
    else
    {
        (VOID)InterlockedIncrement(
                  &Device->FeatureInterfaceReferences);
    }
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
}

static VOID
NTAPI
SoftGpuFeatureInterfaceDereference(
    _In_ PVOID Context)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)Context;
    KIRQL OldIrql;
    LONG References;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
    {
        ASSERT(FALSE);
        return;
    }

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    References = Device->FeatureInterfaceReferences;
    if (References <= 0)
    {
        ASSERT(FALSE);
    }
    else if (InterlockedDecrement(
                 &Device->FeatureInterfaceReferences) == 0)
    {
        /*
         * Signal while the lock is held. StopDevice takes this same lock
         * before deciding whether it must wait, so it cannot observe zero
         * references before the zero event becomes signaled.
         */
        KeSetEvent(&Device->FeatureInterfaceZeroEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    }
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
}

static BOOLEAN
SoftGpuKmdCpuEventAvailableOnCurrentConfig(
    _In_ PSOFTGPU_DEVICE Device)
{
    return InterlockedCompareExchange(
               &Device->FeatureInterfaceQueriesOpen, 0, 0) != 0 &&
           (Device->KmdSignalCpuEventEnabled ||
            InterlockedCompareExchange(
                &Device->FeatureNegotiationActive, 0, 0) != 0) &&
           Device->DxgkInterface.Size >=
               FIELD_OFFSET(DXGK_INTERFACE, DxgkCbSignalEvent) +
                   sizeof(Device->DxgkInterface.DxgkCbSignalEvent) &&
           Device->DxgkInterface.DxgkCbSignalEvent != NULL;
}

static NTSTATUS
APIENTRY
SoftGpuQueryFeatureSupport(
    _In_ HANDLE MiniportDeviceContext,
    INOUT_PDXGKARG_QUERYFEATURESUPPORT Args)
{
    PSOFTGPU_DEVICE Device =
        (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        Args == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Args->MinSupportedVersion = 0;
    Args->MaxSupportedVersion = 0;
    Args->SupportedByDriver = FALSE;
    Args->SupportedOnCurrentConfig = FALSE;

    if (Args->FeatureId != DXGK_FEATURE_KMD_SIGNAL_CPU_EVENT)
        return STATUS_NOT_SUPPORTED;

    Args->MinSupportedVersion = 1;
    Args->MaxSupportedVersion = 1;
    Args->SupportedByDriver = TRUE;
    Args->SupportedOnCurrentConfig =
        SoftGpuKmdCpuEventAvailableOnCurrentConfig(Device);
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
SoftGpuQueryFeatureInterface(
    _In_ HANDLE MiniportDeviceContext,
    INOUT_PDXGKARG_QUERYFEATUREINTERFACE Args)
{
    PSOFTGPU_DEVICE Device =
        (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        Args == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * KMD-signaled CPU events have no feature-specific interface table.
     * Support negotiation still requires this callback to be present in the
     * containing DXGKDDI_FEATURE_INTERFACE.
     */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
APIENTRY
SoftGpuDdiQueryInterface(
    _In_ PVOID MiniportDeviceContext,
    _In_ PQUERY_INTERFACE QueryInterface)
{
    PSOFTGPU_DEVICE Device =
        (PSOFTGPU_DEVICE)MiniportDeviceContext;
    DXGKDDI_FEATURE_INTERFACE FeatureInterface;

    PAGED_CODE();

    if (Device == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        QueryInterface == NULL ||
        QueryInterface->InterfaceType == NULL ||
        QueryInterface->Interface == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!RtlEqualMemory(QueryInterface->InterfaceType,
                        &SoftGpuWddmFeatureInterfaceGuid,
                        sizeof(SoftGpuWddmFeatureInterfaceGuid)) ||
        QueryInterface->DeviceUid != DISPLAY_ADAPTER_HW_ID)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (QueryInterface->Version < DXGK_FEATURE_INTERFACE_VERSION_1)
        return STATUS_NOT_SUPPORTED;
    if (QueryInterface->Size < sizeof(FeatureInterface))
        return STATUS_BUFFER_TOO_SMALL;
    if (!SoftGpuAcquireFeatureInterface(Device))
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(&FeatureInterface, sizeof(FeatureInterface));
    FeatureInterface.Size = sizeof(FeatureInterface);
    FeatureInterface.Version = DXGK_FEATURE_INTERFACE_VERSION_1;
    FeatureInterface.Context = Device;
    FeatureInterface.InterfaceReference =
        SoftGpuFeatureInterfaceReference;
    FeatureInterface.InterfaceDereference =
        SoftGpuFeatureInterfaceDereference;
    FeatureInterface.QueryFeatureSupport =
        SoftGpuQueryFeatureSupport;
    FeatureInterface.QueryFeatureInterface =
        SoftGpuQueryFeatureInterface;
    *(PDXGKDDI_FEATURE_INTERFACE)QueryInterface->Interface =
        FeatureInterface;

    return STATUS_SUCCESS;
}
#endif


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
    SOFTGPU_PLATFORM_CONFIG PlatformConfig;
    KIRQL OldIrql;
    NTSTATUS Status;
    SIZE_T RequiredFrameBufferSize;

    UNREFERENCED_PARAMETER(DxgkStartInfo);

    DPRINT("SOFTGPU: StartDevice Device=%p\n", Device);

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL ||
        NumberOfChildren == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DxgkInterface->Size <
            FIELD_OFFSET(DXGK_INTERFACE, DxgkCbNotifyDpc) +
                sizeof(DxgkInterface->DxgkCbNotifyDpc) ||
        DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbNotifyInterrupt == NULL ||
        DxgkInterface->DxgkCbNotifyDpc == NULL)
    {
        DPRINT1("SOFTGPU: StartDevice: incomplete DxgkInterface\n");
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&PlatformConfig, sizeof(PlatformConfig));
    Status = SoftGpuPlatformQueryStart(Device,
                                       DxgkInterface,
                                       &PlatformConfig);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SOFTGPU: platform display validation failed 0x%08lx\n",
                Status);
        return Status;
    }

    if (PlatformConfig.Width == 0 ||
        PlatformConfig.Height == 0 ||
        PlatformConfig.Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
        PlatformConfig.Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        PlatformConfig.Width >
            MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        (PlatformConfig.Format != D3DDDIFMT_X8R8G8B8 &&
         PlatformConfig.Format != D3DDDIFMT_A8R8G8B8))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = SoftGpu2dComputeAllocationSlabSize(
                 PlatformConfig.Width,
                 PlatformConfig.Height,
                 SOFTGPU_2D_WORKING_SURFACE_COUNT,
                 SOFTGPU_MAX_ALLOCATION_SLAB_SIZE,
                 &RequiredFrameBufferSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Device->Width = PlatformConfig.Width;
    Device->Height = PlatformConfig.Height;
    Device->Format = PlatformConfig.Format;

    Status = SoftGpuAllocateFrameBuffer(
                 Device,
                 RequiredFrameBufferSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SoftGpuScanoutStart(Device, &PlatformConfig);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * Save only the prefix the OS advertised.  Older WDDM levels pass a
     * shorter table; reading sizeof(DXGK_INTERFACE) from such a caller would
     * overrun its allocation and leak the compile-time header ceiling into
     * the runtime ABI.
     */
    RtlZeroMemory(&Device->DxgkInterface, sizeof(Device->DxgkInterface));
    RtlCopyMemory(&Device->DxgkInterface,
                  DxgkInterface,
                  min((SIZE_T)DxgkInterface->Size,
                      sizeof(Device->DxgkInterface)));

#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    /*
     * Feature negotiation can reenter DxgkDdiQueryInterface while this
     * StartDevice call is still active. Open only that provider gate here;
     * ordinary submit/DPC admission remains stopped until initialization
     * below is complete.
     */
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    Device->FeatureInterfaceQueriesOpen = 1;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 3000)
    {
        BOOLEAN FeatureStateResolved = FALSE;

        Device->KmdSignalCpuEventEnabled = FALSE;
        if (DxgkInterface->Size >=
                FIELD_OFFSET(DXGK_INTERFACE, DxgkCbSignalEvent) +
                    sizeof(DxgkInterface->DxgkCbSignalEvent) &&
            DxgkInterface->DxgkCbSignalEvent != NULL)
        {
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
            InterlockedExchange(
                &Device->FeatureNegotiationActive, 1);
            if (DxgkInterface->Size >=
                    FIELD_OFFSET(DXGK_INTERFACE,
                                 DxgkCbQueryServices) +
                        sizeof(DxgkInterface->DxgkCbQueryServices) &&
                DxgkInterface->DxgkCbQueryServices != NULL)
            {
                DXGK_FEATURE_INTERFACE FeatureInterface;
                DXGKARGCB_ISFEATUREENABLED2 FeatureArgs;
                NTSTATUS FeatureStatus;

                RtlZeroMemory(&FeatureInterface,
                              sizeof(FeatureInterface));
                FeatureInterface.Size = sizeof(FeatureInterface);
                FeatureInterface.Version =
                    DXGK_FEATURE_INTERFACE_VERSION_1;
                FeatureStatus =
                    DxgkInterface->DxgkCbQueryServices(
                        DxgkInterface->DeviceHandle,
                        DxgkServicesFeature,
                        (PINTERFACE)&FeatureInterface);
                if (NT_SUCCESS(FeatureStatus))
                {
                    if (FeatureInterface.Size ==
                            sizeof(FeatureInterface) &&
                        FeatureInterface.Version ==
                            DXGK_FEATURE_INTERFACE_VERSION_1 &&
                        FeatureInterface.InterfaceReference != NULL &&
                        FeatureInterface.InterfaceDereference != NULL &&
                        FeatureInterface.IsFeatureEnabled != NULL &&
                        FeatureInterface.QueryFeatureInterface != NULL)
                    {
                        RtlZeroMemory(&FeatureArgs,
                                      sizeof(FeatureArgs));
                        FeatureArgs.FeatureId =
                            DXGK_FEATURE_KMD_SIGNAL_CPU_EVENT;
                        FeatureStatus =
                            FeatureInterface.IsFeatureEnabled(
                                DxgkInterface->DeviceHandle,
                                &FeatureArgs);
                        if (NT_SUCCESS(FeatureStatus))
                        {
                            Device->KmdSignalCpuEventEnabled =
                                FeatureArgs.Result.Enabled != FALSE;
                            FeatureStateResolved = TRUE;
                        }
                    }

                    if (FeatureInterface.InterfaceDereference != NULL)
                    {
                        FeatureInterface.InterfaceDereference(
                            FeatureInterface.Context);
                    }
                }
            }
            InterlockedExchange(
                &Device->FeatureNegotiationActive, 0);
#endif

#if (REACTOS_WDDM_TARGET_LEVEL < 3200)
            if (!FeatureStateResolved &&
                DxgkInterface->Size >=
                    FIELD_OFFSET(DXGK_INTERFACE,
                                 DxgkCbQueryFeatureSupport) +
                        sizeof(DxgkInterface->
                                   DxgkCbQueryFeatureSupport) &&
                DxgkInterface->DxgkCbQueryFeatureSupport != NULL)
            {
                DXGKARGCB_QUERYFEATURESUPPORT FeatureArgs;
                NTSTATUS FeatureStatus;

                RtlZeroMemory(&FeatureArgs, sizeof(FeatureArgs));
                FeatureArgs.DeviceHandle =
                    DxgkInterface->DeviceHandle;
                FeatureArgs.FeatureId =
                    DXGK_FEATURE_KMD_SIGNAL_CPU_EVENT;
                FeatureArgs.DriverSupportState =
                    DXGK_FEATURE_SUPPORT_STABLE;
                FeatureStatus =
                    DxgkInterface->DxgkCbQueryFeatureSupport(
                        &FeatureArgs);
                if (NT_SUCCESS(FeatureStatus))
                {
                    Device->KmdSignalCpuEventEnabled =
                        FeatureArgs.Enabled != FALSE;
                    FeatureStateResolved = TRUE;
                }
            }
            else if (!FeatureStateResolved &&
                     DxgkInterface->Size >=
                         FIELD_OFFSET(DXGK_INTERFACE,
                                      DxgkCbIsFeatureEnabled) +
                             sizeof(DxgkInterface->
                                        DxgkCbIsFeatureEnabled) &&
                     DxgkInterface->DxgkCbIsFeatureEnabled != NULL)
            {
                DXGKARGCB_ISFEATUREENABLED FeatureArgs;
                NTSTATUS FeatureStatus;

                RtlZeroMemory(&FeatureArgs, sizeof(FeatureArgs));
                FeatureArgs.DeviceHandle =
                    DxgkInterface->DeviceHandle;
                FeatureArgs.FeatureId =
                    DXGK_FEATURE_KMD_SIGNAL_CPU_EVENT;
                FeatureStatus =
                    DxgkInterface->DxgkCbIsFeatureEnabled(
                        &FeatureArgs);
                if (NT_SUCCESS(FeatureStatus))
                {
                    Device->KmdSignalCpuEventEnabled =
                        FeatureArgs.Enabled != FALSE;
                    FeatureStateResolved = TRUE;
                }
            }
#endif
        }
    }
#endif

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
    KeInitializeTimer(&Device->VsyncTimer);
    KeInitializeDpc(&Device->VsyncDpc, SoftGpuVsyncDpcRoutine, Device);
    Device->VsyncTimerInitialized = TRUE;
    InterlockedExchange(&Device->VsyncPhaseEnabled, 0);
    InterlockedExchange(&Device->VsyncEnabled, 0);
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
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    Device->FeatureInterfaceQueriesOpen = 0;
    Device->FeatureNegotiationActive = 0;
#endif
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    if (InterlockedCompareExchange(
            &Device->FeatureInterfaceReferences, 0, 0) != 0)
    {
        (VOID)KeWaitForSingleObject(
                  &Device->FeatureInterfaceZeroEvent,
                  Executive,
                  KernelMode,
                  FALSE,
                  NULL);
    }
#endif
    InterlockedExchange(&Device->VsyncEnabled, 0);
    InterlockedExchange(&Device->VsyncPhaseEnabled, 0);
    if (Device->VsyncTimerInitialized)
    {
        KeCancelTimer(&Device->VsyncTimer);
        Device->VsyncTimerInitialized = FALSE;
    }
    if (Device->DpcInitialized)
    {
        KeRemoveQueueDpc(&Device->DpcObject);
        KeRemoveQueueDpc(&Device->VsyncDpc);
        KeFlushQueuedDpcs();
        Device->DpcInitialized = FALSE;
    }
    SoftGpuScanoutStop(Device);
    RtlZeroMemory(&Device->DxgkInterface, sizeof(Device->DxgkInterface));

    /*
     * Keep the validated segment across a PnP stop/start.  A later start
     * reuses it when large enough or atomically replaces it; RemoveDevice is
     * the single final owner that frees the recorded allocation size.
     */
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
        Device->FrameBufferSize = 0;
        Device->FrameBufferPhys.QuadPart = 0;
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
    SoftGpuPlatformFillNodeMetadata(GetNodeMetadata);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    GetNodeMetadata->GpuMmuSupported = SOFTGPU_GPUMMU_END_TO_END;
    GetNodeMetadata->IoMmuSupported = FALSE;
#endif
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
        (pOut)->PagingBufferSegmentId       = 0;                            \
        (pOut)->PagingBufferSize            = 64 * 1024;                    \
        (pOut)->PagingBufferPrivateDataSize = 0;                            \
    } while (0)

/* Fill pass: one CPU-visible segment over the validated mode-sized buffer. */
#define SOFTGPU_FILL_SEGMENT_DESC(pDesc, Dev)                               \
    do {                                                                    \
        RtlZeroMemory((pDesc), sizeof(*(pDesc)));                           \
        (pDesc)->Flags.CpuVisible                = 1;                       \
        (pDesc)->Flags.PopulatedFromSystemMemory = 1;                       \
        (pDesc)->Flags.LocalBudgetGroup          = 1;                       \
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

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    case DXGKQAITYPE_GPUMMUCAPS:
    {
        /*
         * GPU MMU declaration: dxgkrnl owns 4-level software page tables
         * over a 48-bit VA space and writes the PTEs with the CPU
         * (DXGK_PAGETABLEUPDATE_CPU_VIRTUAL).  System memory is coherent,
         * and the PTE format carries read-only/no-execute/zero bits.
         */
        DXGK_GPUMMUCAPS *GpuMmuCaps;

        if (!SOFTGPU_GPUMMU_END_TO_END)
            return STATUS_NOT_SUPPORTED;
        if (pQueryAdapterInfo->pOutputData == NULL)
            return STATUS_INVALID_PARAMETER;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_GPUMMUCAPS))
            return STATUS_BUFFER_TOO_SMALL;

        GpuMmuCaps = (DXGK_GPUMMUCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(GpuMmuCaps, sizeof(*GpuMmuCaps));
        GpuMmuCaps->ReadOnlyMemorySupported = 1;
        GpuMmuCaps->NoExecuteMemorySupported = 1;
        GpuMmuCaps->ZeroInPteSupported = 1;
        GpuMmuCaps->CacheCoherentMemorySupported = 1;
        GpuMmuCaps->PageTableUpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        GpuMmuCaps->VirtualAddressBitCount = SOFTGPU_GPUVA_BIT_COUNT;
        GpuMmuCaps->PageTableLevelCount = SOFTGPU_GPUVA_LEVELS;
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PAGETABLELEVELDESC:
    {
        /*
         * Radix geometry this device implements: SOFTGPU_GPUVA_LEVELS levels
         * of SOFTGPU_GPUVA_INDEX_BITS index bits each over 4 KB pages.  Each
         * table holds its entries as DXGK_PTE update descriptors and is page
         * aligned.
         */
        CONST DXGK_QUERYPAGETABLELEVELDESCIN *In;
        DXGK_PAGE_TABLE_LEVEL_DESC *Desc;

        if (!SOFTGPU_GPUMMU_END_TO_END)
            return STATUS_NOT_SUPPORTED;
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*In) ||
            pQueryAdapterInfo->pOutputData == NULL)
            return STATUS_INVALID_PARAMETER;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*Desc))
            return STATUS_BUFFER_TOO_SMALL;

        In = (CONST DXGK_QUERYPAGETABLELEVELDESCIN *)pQueryAdapterInfo->pInputData;
        if (In->PhysicalAdapterIndex != 0 || In->LevelIndex >= SOFTGPU_GPUVA_LEVELS)
            return STATUS_INVALID_PARAMETER;

        Desc = (DXGK_PAGE_TABLE_LEVEL_DESC *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(Desc, sizeof(*Desc));
        Desc->PageTableIndexBitCount = SOFTGPU_GPUVA_INDEX_BITS;
        Desc->PageTableSegmentId = 0;
        Desc->PagingProcessPageTableSegmentId = 0;
        Desc->PageTableSizeInBytes = (1u << SOFTGPU_GPUVA_INDEX_BITS) * (UINT)sizeof(DXGK_PTE);
        Desc->PageTableAlignmentInBytes = PAGE_SIZE;
        return STATUS_SUCCESS;
    }
#endif

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
         * softgpu exposes a single CPU-visible memory segment backed by
         * the validated, mode-sized write-combined contiguous buffer.
         *
         * Segment flags:
         *   Aperture              = 0: the framebuffer slab is the placement,
         *                             not a remappable aperture.
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

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
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
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
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
#endif

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
    PSOFTGPU_DEVICE     Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PSOFTGPU_PROCESS    Process;
    PSOFTGPU_KMD_DEVICE KmdDevice;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        CreateDevice == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    Process = (PSOFTGPU_PROCESS)CreateDevice->hKmdProcess;
    if (Process == NULL ||
        Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        Process->Adapter != Device)
    {
        return STATUS_INVALID_PARAMETER;
    }
#else
    Process = NULL;
#endif

    KmdDevice = (PSOFTGPU_KMD_DEVICE)ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(*KmdDevice),
        SOFTGPU_WDDM2_POOL_TAG);
    if (KmdDevice == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(KmdDevice, sizeof(*KmdDevice));
    KmdDevice->Magic = SOFTGPU_KMD_DEVICE_MAGIC;
    KmdDevice->Adapter = Device;
    KmdDevice->Process = Process;
    CreateDevice->hDevice = KmdDevice;

    DPRINT("SOFTGPU: CreateDevice process=%p -> hDevice=%p\n",
           Process, KmdDevice);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
SoftGpuDdiDestroyDevice(
    _In_ PVOID MiniportDeviceContext)
{
    PSOFTGPU_KMD_DEVICE KmdDevice =
        (PSOFTGPU_KMD_DEVICE)MiniportDeviceContext;
    PSOFTGPU_DEVICE Device;
    KIRQL OldIrql;

    if (KmdDevice == NULL)
        return STATUS_SUCCESS;
    if (KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        KmdDevice->Adapter == NULL ||
        KmdDevice->Adapter->Magic != SOFTGPU_DEVICE_MAGIC)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Device = KmdDevice->Adapter;
    DPRINT("SOFTGPU: DestroyDevice context=%p\n", KmdDevice);

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    KmdDevice->Magic = 0xDEAD260DUL;
    KmdDevice->Process = NULL;
    KmdDevice->Adapter = NULL;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    ExFreePoolWithTag(KmdDevice, SOFTGPU_WDDM2_POOL_TAG);
    return STATUS_SUCCESS;
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 3000)
NTSTATUS
APIENTRY
SoftGpuDdiCreateCpuEvent(
    _In_ HANDLE MiniportDeviceContext,
    INOUT_PDXGKARG_CREATECPUEVENT Args)
{
    PSOFTGPU_DEVICE Device =
        (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    PSOFTGPU_CPU_EVENT CpuEvent;

    if (Device == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        Args == NULL ||
        Args->Flags.Value != 0 ||
        Args->hKmdDevice == NULL ||
        Args->hDxgCpuEvent == NULL ||
        Args->hKmdCpuEvent != NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Device->KmdSignalCpuEventEnabled)
        return STATUS_NOT_SUPPORTED;

    KmdDevice = (PSOFTGPU_KMD_DEVICE)Args->hKmdDevice;
    if (KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        KmdDevice->Adapter != Device)
    {
        return STATUS_INVALID_PARAMETER;
    }

    CpuEvent = ExAllocatePoolWithTag(
                   NonPagedPool,
                   sizeof(*CpuEvent),
                   SOFTGPU_WDDM2_POOL_TAG);
    if (CpuEvent == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(CpuEvent, sizeof(*CpuEvent));
    CpuEvent->Magic = SOFTGPU_CPU_EVENT_MAGIC;
    CpuEvent->Adapter = Device;
    CpuEvent->hDxgCpuEvent = Args->hDxgCpuEvent;
    ExInitializeRundownProtection(&CpuEvent->Rundown);
    KeInitializeSpinLock(&CpuEvent->UsageLock);
    Args->hKmdCpuEvent = CpuEvent;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
SoftGpuDdiDestroyCpuEvent(
    _In_ HANDLE MiniportDeviceContext,
    _In_ HANDLE KmdCpuEvent)
{
    PSOFTGPU_DEVICE Device =
        (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PSOFTGPU_CPU_EVENT CpuEvent =
        (PSOFTGPU_CPU_EVENT)KmdCpuEvent;

    if (Device == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        CpuEvent == NULL ||
        CpuEvent->Magic != SOFTGPU_CPU_EVENT_MAGIC ||
        CpuEvent->Adapter != Device ||
        InterlockedCompareExchange(
            &CpuEvent->Destroying, 1, 0) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ExWaitForRundownProtectionRelease(&CpuEvent->Rundown);
    CpuEvent->Magic = 0xDEAD3000UL;
    CpuEvent->Adapter = NULL;
    CpuEvent->hDxgCpuEvent = NULL;
    ExFreePoolWithTag(CpuEvent, SOFTGPU_WDDM2_POOL_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
SoftGpuDdiEscape(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_ESCAPE *Escape)
{
    PSOFTGPU_DEVICE Device =
        (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PSOFTGPU_KMD_DEVICE KmdDevice;
    D3DDDI_DRIVERESCAPE_CPUEVENTUSAGE *Usage;
    PSOFTGPU_CPU_EVENT CpuEvent;
    KIRQL OldIrql;

    if (Device == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        Escape == NULL ||
        Escape->hDevice == NULL ||
        Escape->hContext != NULL ||
        Escape->pPrivateDriverData == NULL ||
        Escape->PrivateDriverDataSize !=
            sizeof(D3DDDI_DRIVERESCAPE_CPUEVENTUSAGE) ||
        !Escape->Flags.DriverKnownEscape)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KmdDevice = (PSOFTGPU_KMD_DEVICE)Escape->hDevice;
    if (KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        KmdDevice->Adapter != Device)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Usage = (D3DDDI_DRIVERESCAPE_CPUEVENTUSAGE *)
                Escape->pPrivateDriverData;
    if (Usage->EscapeType !=
            D3DDDI_DRIVERESCAPETYPE_CPUEVENTUSAGE ||
        Usage->hSyncObject == 0 ||
        Usage->hKmdCpuEvent == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    CpuEvent =
        (PSOFTGPU_CPU_EVENT)(ULONG_PTR)Usage->hKmdCpuEvent;
    if (!ExAcquireRundownProtection(&CpuEvent->Rundown))
        return STATUS_DELETE_PENDING;

    if (CpuEvent->Magic != SOFTGPU_CPU_EVENT_MAGIC ||
        CpuEvent->Adapter != Device ||
        InterlockedCompareExchange(
            &CpuEvent->Destroying, 0, 0) != 0)
    {
        ExReleaseRundownProtection(&CpuEvent->Rundown);
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&CpuEvent->UsageLock, &OldIrql);
    RtlCopyMemory(CpuEvent->Usage,
                  Usage->Usage,
                  sizeof(CpuEvent->Usage));
    KeReleaseSpinLock(&CpuEvent->UsageLock, OldIrql);
    ExReleaseRundownProtection(&CpuEvent->Rundown);
    return STATUS_SUCCESS;
}
#endif


/* =========================================================================
 * DxgkDdiCreateAllocation / DxgkDdiDestroyAllocation
 * =========================================================================
 */

/*
 * Describe the linear allocations that dxgkrnl creates for its shared
 * primary/shadow pipeline. The private record is returned on the first
 * size-query pass and copied into both the allocation and every per-device
 * open binding on the second pass, so Present never has to guess a pitch.
 */
NTSTATUS
APIENTRY
SoftGpuDdiGetStandardAllocationDriverData(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA
        GetStandardAllocationDriverData)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    SOFTGPU_ALLOCATION_PRIVATE_DATA PrivateData;
    UINT SuppliedPrivateSize;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    D3DDDIFORMAT Format;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        GetStandardAllocationDriverData == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Width = 0;
    Height = 0;
    Pitch = 0;
    Format = D3DDDIFMT_X8R8G8B8;

    switch (GetStandardAllocationDriverData->StandardAllocationType)
    {
        case DXGK_STDALLOCATION_SHAREDPRIMARYSURFACE:
        {
            D3DKMDT_SHAREDPRIMARYSURFACEDATA *Surface =
                GetStandardAllocationDriverData
                    ->pCreateSharedPrimarySurfaceData;

            if (Surface == NULL ||
                Surface->VidPnSourceId != 0 ||
                Surface->Width != Device->Width ||
                Surface->Height != Device->Height)
            {
                return STATUS_INVALID_PARAMETER;
            }
            Width = Surface->Width;
            Height = Surface->Height;
            Format = Surface->Format;
            break;
        }

        case DXGK_STDALLOCATION_SHADOWSURFACE:
        {
            D3DKMDT_SHADOWSURFACEDATA *Surface =
                GetStandardAllocationDriverData->pCreateShadowSurfaceData;

            if (Surface == NULL ||
                Surface->Width != Device->Width ||
                Surface->Height != Device->Height)
            {
                return STATUS_INVALID_PARAMETER;
            }
            Width = Surface->Width;
            Height = Surface->Height;
            Format = Surface->Format;
            if (Width > MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
                return STATUS_INTEGER_OVERFLOW;
            Surface->Pitch = Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
            Pitch = Surface->Pitch;
            break;
        }

        case DXGK_STDALLOCATION_STAGINGSURFACE:
        {
            D3DKMDT_STAGINGSURFACEDATA *Surface =
                GetStandardAllocationDriverData->pCreateStagingSurfaceData;

            if (Surface == NULL)
                return STATUS_INVALID_PARAMETER;
            Width = Surface->Width;
            Height = Surface->Height;
            if (Width > MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
                return STATUS_INTEGER_OVERFLOW;
            Surface->Pitch = Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
            Pitch = Surface->Pitch;
            break;
        }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
        case DXGK_STDALLOCATION_GDISURFACE:
        {
            D3DKMDT_GDISURFACEDATA *Surface =
                GetStandardAllocationDriverData->pCreateGdiSurfaceData;

            if (Surface == NULL)
                return STATUS_INVALID_PARAMETER;
            Width = Surface->Width;
            Height = Surface->Height;
            Format = Surface->Format;
            if (Width > MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
                return STATUS_INTEGER_OVERFLOW;
            Surface->Pitch = Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
            Pitch = Surface->Pitch;
            break;
        }
#endif

        default:
            return STATUS_NOT_SUPPORTED;
    }

    if (Width == 0 || Height == 0 ||
        Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
        Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        Width > MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        (Format != D3DDDIFMT_X8R8G8B8 &&
         Format != D3DDDIFMT_A8R8G8B8))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Pitch == 0)
        Pitch = Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;

    RtlZeroMemory(&PrivateData, sizeof(PrivateData));
    PrivateData.Width = Width;
    PrivateData.Height = Height;
    PrivateData.BitsPerPixel = SOFTGPU_DISPLAY_BITS_PER_PIXEL;
    PrivateData.Magic = SOFTGPU_ALLOCATION_PRIVATE_MAGIC;
    PrivateData.Version = SOFTGPU_ALLOCATION_PRIVATE_VERSION;
    PrivateData.Pitch = Pitch;
    PrivateData.Format = Format;
    if (!SoftGpuAllocationPrivateDataValid(&PrivateData) ||
        (ULONGLONG)Pitch * Height > Device->FrameBufferSize)
    {
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
    }

    SuppliedPrivateSize =
        GetStandardAllocationDriverData->AllocationPrivateDriverDataSize;
    GetStandardAllocationDriverData->AllocationPrivateDriverDataSize =
        sizeof(PrivateData);
    GetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0;

    if (GetStandardAllocationDriverData->pAllocationPrivateDriverData == NULL)
        return STATUS_SUCCESS;
    if (SuppliedPrivateSize < sizeof(PrivateData))
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(
        GetStandardAllocationDriverData->pAllocationPrivateDriverData,
        &PrivateData,
        sizeof(PrivateData));
    return STATUS_SUCCESS;
}

/*
 * SoftGpuDdiCreateAllocation
 *
 * Allocates a per-allocation SOFTGPU_ALLOC context and fills in the
 * DXGK_ALLOCATIONINFO fields that dxgkrnl needs for placement decisions.
 *
 * IRQL: PASSIVE_LEVEL
 */
static VOID
SoftGpuRollbackCreatedAllocations(
    _Inout_ PDXGKARG_CREATEALLOCATION CreateAllocation,
    _In_ ULONG CreatedCount)
{
    while (CreatedCount > 0)
    {
        PSOFTGPU_ALLOC Alloc;

        --CreatedCount;
        Alloc = (PSOFTGPU_ALLOC)
            CreateAllocation->pAllocationInfo[CreatedCount].hAllocation;
        CreateAllocation->pAllocationInfo[CreatedCount].hAllocation = NULL;
        if (Alloc != NULL)
        {
            ASSERT(Alloc->Magic == SOFTGPU_ALLOC_MAGIC);
            Alloc->Magic = 0xDEADA110UL;
            ExFreePoolWithTag(Alloc, SOFTGPU_POOL_TAG);
        }
    }
}

NTSTATUS
APIENTRY
SoftGpuDdiCreateAllocation(
    _In_    PVOID                      MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEALLOCATION  CreateAllocation)
{
    ULONG               i;
    PSOFTGPU_DEVICE      Device;
    PSOFTGPU_ALLOC       Alloc = NULL;
    DXGK_ALLOCATIONINFO *pInfo;
    NTSTATUS             Status = STATUS_SUCCESS;

    if (MiniportDeviceContext == NULL ||
        ((PSOFTGPU_DEVICE)MiniportDeviceContext)->Magic != SOFTGPU_DEVICE_MAGIC ||
        CreateAllocation == NULL ||
        (CreateAllocation->NumAllocations != 0 &&
         CreateAllocation->pAllocationInfo == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    if (Device->FrameBuffer == NULL || Device->FrameBufferSize == 0)
        return STATUS_DEVICE_NOT_READY;

    DPRINT("SOFTGPU: CreateAllocation NumAllocations=%u\n",
           CreateAllocation->NumAllocations);

    for (i = 0; i < CreateAllocation->NumAllocations; ++i)
        CreateAllocation->pAllocationInfo[i].hAllocation = NULL;

    for (i = 0; i < CreateAllocation->NumAllocations; i++)
    {
        pInfo = &CreateAllocation->pAllocationInfo[i];

        if (pInfo->Size > Device->FrameBufferSize)
        {
            Status = STATUS_GRAPHICS_NO_VIDEO_MEMORY;
            goto Rollback;
        }

        Alloc = (PSOFTGPU_ALLOC)ExAllocatePoolWithTag(NonPagedPool,
                                                        sizeof(SOFTGPU_ALLOC),
                                                        SOFTGPU_POOL_TAG);
        if (Alloc == NULL)
        {
            DPRINT1("SOFTGPU: CreateAllocation: alloc[%lu] pool alloc failed\n",
                    i);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Rollback;
        }

        RtlZeroMemory(Alloc, sizeof(SOFTGPU_ALLOC));
        Alloc->Magic = SOFTGPU_ALLOC_MAGIC;

        /*
         * Standard allocations and softgpu UMD resources carry the same exact
         * linear geometry record. Other private records remain opaque and can
         * never enter the validated 2D render path.
         */
        Alloc->Size   = (pInfo->Size != 0) ? pInfo->Size : PAGE_SIZE;
        if (Alloc->Size > MAXULONG_PTR - (PAGE_SIZE - 1))
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Rollback;
        }

        Alloc->Size   = (Alloc->Size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (Alloc->Size > Device->FrameBufferSize)
        {
            Status = STATUS_GRAPHICS_NO_VIDEO_MEMORY;
            goto Rollback;
        }
        Alloc->Format = D3DDDIFMT_A8R8G8B8;    /* default */

        if (pInfo->pPrivateDriverData != NULL &&
            pInfo->PrivateDriverDataSize >=
                sizeof(SOFTGPU_ALLOCATION_PRIVATE_DATA))
        {
            const SOFTGPU_ALLOCATION_PRIVATE_DATA *PrivateData =
                (const SOFTGPU_ALLOCATION_PRIVATE_DATA *)
                    pInfo->pPrivateDriverData;

            if (PrivateData->Magic ==
                    SOFTGPU_ALLOCATION_PRIVATE_MAGIC)
            {
                ULONGLONG RequiredSize;

                if (!SoftGpuAllocationPrivateDataValid(PrivateData))
                {
                    Status = STATUS_INVALID_PARAMETER;
                    goto Rollback;
                }
                RequiredSize =
                    (ULONGLONG)PrivateData->Pitch *
                    PrivateData->Height;
                if (RequiredSize > Alloc->Size)
                {
                    Status = STATUS_INVALID_BUFFER_SIZE;
                    goto Rollback;
                }

                Alloc->Width = PrivateData->Width;
                Alloc->Height = PrivateData->Height;
                Alloc->Pitch = PrivateData->Pitch;
                Alloc->Format = PrivateData->Format;
            }
        }

        /* Fill in DXGK_ALLOCATIONINFO fields for dxgkrnl placement. */
        pInfo->Size                    = Alloc->Size;
        pInfo->Alignment               = PAGE_SIZE;
        pInfo->SupportedReadSegmentSet = (1 << (SOFTGPU_SEGMENT_ID - 1));
        pInfo->SupportedWriteSegmentSet= (1 << (SOFTGPU_SEGMENT_ID - 1));
        pInfo->EvictionSegmentSet      = 0;     /* direct system transfer */
        pInfo->Flags.CpuVisible        = 1;
        pInfo->Flags.AccessedPhysically= 1;
        pInfo->hAllocation             = (HANDLE)Alloc;
        Alloc = NULL;

        DPRINT("SOFTGPU: CreateAllocation alloc[%lu]: size=%Iu handle=%p\n",
               i,
               ((PSOFTGPU_ALLOC)pInfo->hAllocation)->Size,
               pInfo->hAllocation);
    }

    return STATUS_SUCCESS;

Rollback:
    if (Alloc != NULL)
    {
        Alloc->Magic = 0xDEADA110UL;
        ExFreePoolWithTag(Alloc, SOFTGPU_POOL_TAG);
    }
    SoftGpuRollbackCreatedAllocations(CreateAllocation, i);
    return Status;
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

#if (REACTOS_WDDM_TARGET_LEVEL >= 2100)
NTSTATUS
APIENTRY
SoftGpuDdiValidateUpdateAllocationProperty(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_VALIDATEUPDATEALLOCPROPERTY
        *ValidateUpdateAllocationProperty)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)hAdapter;
    PSOFTGPU_OPENALLOC Open;
    ULONG PreferredIds[5];
    ULONG Index;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        ValidateUpdateAllocationProperty == NULL ||
        ValidateUpdateAllocationProperty->hAllocation == NULL ||
        (ValidateUpdateAllocationProperty->PropertyMaskValue &
         ~0x7UL) != 0 ||
        (ValidateUpdateAllocationProperty->Flags.Value & ~0x1UL) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Open = (PSOFTGPU_OPENALLOC)
        ValidateUpdateAllocationProperty->hAllocation;
    if (Open->Magic != SOFTGPU_OPENALLOC_MAGIC ||
        Open->Device == NULL ||
        Open->Device->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        Open->Device->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }

    if (ValidateUpdateAllocationProperty->SetAccessedPhysically &&
        !ValidateUpdateAllocationProperty->Flags.AccessedPhysically)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (ValidateUpdateAllocationProperty->SetSupportedSegmentSet &&
        ValidateUpdateAllocationProperty->SupportedSegmentSet !=
            (1UL << (SOFTGPU_SEGMENT_ID - 1)))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (ValidateUpdateAllocationProperty->SetPreferredSegment)
    {
        PreferredIds[0] =
            ValidateUpdateAllocationProperty
                ->PreferredSegment.SegmentId0;
        PreferredIds[1] =
            ValidateUpdateAllocationProperty
                ->PreferredSegment.SegmentId1;
        PreferredIds[2] =
            ValidateUpdateAllocationProperty
                ->PreferredSegment.SegmentId2;
        PreferredIds[3] =
            ValidateUpdateAllocationProperty
                ->PreferredSegment.SegmentId3;
        PreferredIds[4] =
            ValidateUpdateAllocationProperty
                ->PreferredSegment.SegmentId4;
        for (Index = 0;
             Index < RTL_NUMBER_OF(PreferredIds);
             ++Index)
        {
            if (PreferredIds[Index] != 0 &&
                PreferredIds[Index] != SOFTGPU_SEGMENT_ID)
            {
                return STATUS_NOT_SUPPORTED;
            }
        }
    }

    return STATUS_SUCCESS;
}
#endif


/* =========================================================================
 * DxgkDdiOpenAllocation / DxgkDdiCloseAllocation
 * =========================================================================
 */

/*
 * SoftGpuDdiOpenAllocation
 *
 * Creates a per-device SOFTGPU_OPENALLOC binding for each opened allocation.
 * The binding records its owning KMD device and the exact standard-allocation
 * geometry. Render and Present use that ownership record to reject a binding
 * from another device instead of treating every adapter allocation as global.
 *
 * IRQL: PASSIVE_LEVEL
 */
static VOID
SoftGpuRollbackOpenedAllocations(
    _In_ CONST DXGKARG_OPENALLOCATION *OpenAllocation,
    _In_ ULONG OpenedCount)
{
    while (OpenedCount > 0)
    {
        PSOFTGPU_OPENALLOC Open;

        --OpenedCount;
        Open = (PSOFTGPU_OPENALLOC)
            OpenAllocation->pOpenAllocation[OpenedCount]
                .hDeviceSpecificAllocation;
        OpenAllocation->pOpenAllocation[OpenedCount]
            .hDeviceSpecificAllocation = NULL;
        if (Open != NULL)
        {
            ASSERT(Open->Magic == SOFTGPU_OPENALLOC_MAGIC);
            Open->Magic = 0xDEAD0A11UL;
            Open->Device = NULL;
            ExFreePoolWithTag(Open, SOFTGPU_POOL_TAG);
        }
    }
}

NTSTATUS
APIENTRY
SoftGpuDdiOpenAllocation(
    _In_ PVOID                          hDevice,
    _In_ CONST DXGKARG_OPENALLOCATION  *OpenAllocation)
{
    ULONG                  i;
    PSOFTGPU_KMD_DEVICE    KmdDevice =
        (PSOFTGPU_KMD_DEVICE)hDevice;
    PSOFTGPU_DEVICE        Device;
    PSOFTGPU_OPENALLOC     Open = NULL;
    NTSTATUS               Status = STATUS_SUCCESS;

    if (KmdDevice == NULL ||
        KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        OpenAllocation == NULL ||
        OpenAllocation->NumAllocations == 0 ||
        OpenAllocation->pOpenAllocation == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Device = KmdDevice->Adapter;
    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_HANDLE;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (KmdDevice->Process == NULL ||
        KmdDevice->Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }
#endif

    DPRINT("SOFTGPU: OpenAllocation NumAllocations=%u\n",
           OpenAllocation->NumAllocations);

    for (i = 0; i < OpenAllocation->NumAllocations; ++i)
    {
        OpenAllocation->pOpenAllocation[i]
            .hDeviceSpecificAllocation = NULL;
    }

    for (i = 0; i < OpenAllocation->NumAllocations; i++)
    {
        DXGK_OPENALLOCATIONINFO *pInfo = &OpenAllocation->pOpenAllocation[i];
        SOFTGPU_ALLOCATION_PRIVATE_DATA PrivateData;

        if (pInfo->hAllocation == 0 ||
            (pInfo->PrivateDriverDataSize != 0 &&
             pInfo->pPrivateDriverData == NULL))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Rollback;
        }
        Open = (PSOFTGPU_OPENALLOC)ExAllocatePoolWithTag(NonPagedPool,
                                                         sizeof(SOFTGPU_OPENALLOC),
                                                         SOFTGPU_POOL_TAG);
        if (Open == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Rollback;
        }

        RtlZeroMemory(Open, sizeof(*Open));
        Open->Magic = SOFTGPU_OPENALLOC_MAGIC;
        Open->Device = KmdDevice;
        Open->hAllocation = pInfo->hAllocation;

        if (pInfo->PrivateDriverDataSize >= sizeof(PrivateData))
        {
            RtlCopyMemory(&PrivateData,
                          pInfo->pPrivateDriverData,
                          sizeof(PrivateData));
            if (PrivateData.Magic ==
                    SOFTGPU_ALLOCATION_PRIVATE_MAGIC &&
                !SoftGpuAllocationPrivateDataValid(&PrivateData))
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Rollback;
            }

            if (PrivateData.Magic ==
                    SOFTGPU_ALLOCATION_PRIVATE_MAGIC)
            {
                Open->Size =
                    (SIZE_T)PrivateData.Pitch * PrivateData.Height;
                Open->Width = PrivateData.Width;
                Open->Height = PrivateData.Height;
                Open->Pitch = PrivateData.Pitch;
                Open->Format = PrivateData.Format;
            }
        }
        pInfo->hDeviceSpecificAllocation = (HANDLE)Open;
        Open = NULL;
    }

    return STATUS_SUCCESS;

Rollback:
    if (Open != NULL)
    {
        Open->Magic = 0xDEAD0A11UL;
        Open->Device = NULL;
        ExFreePoolWithTag(Open, SOFTGPU_POOL_TAG);
    }
    SoftGpuRollbackOpenedAllocations(OpenAllocation, i);
    return Status;
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
    ULONG                  i;
    PSOFTGPU_KMD_DEVICE    KmdDevice =
        (PSOFTGPU_KMD_DEVICE)hDevice;
    PSOFTGPU_DEVICE        Device;
    PSOFTGPU_OPENALLOC     Open;

    if (KmdDevice == NULL ||
        KmdDevice->Magic != SOFTGPU_KMD_DEVICE_MAGIC ||
        CloseAllocation == NULL ||
        CloseAllocation->NumAllocations == 0 ||
        CloseAllocation->pOpenHandleList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Device = KmdDevice->Adapter;
    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_HANDLE;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (KmdDevice->Process == NULL ||
        KmdDevice->Process->Magic != SOFTGPU_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != Device)
    {
        return STATUS_INVALID_HANDLE;
    }
#endif

    DPRINT("SOFTGPU: CloseAllocation NumAllocations=%u\n",
           CloseAllocation->NumAllocations);

    for (i = 0; i < CloseAllocation->NumAllocations; i++)
    {
        Open = (PSOFTGPU_OPENALLOC)CloseAllocation->pOpenHandleList[i];
        if (Open == NULL ||
            Open->Magic != SOFTGPU_OPENALLOC_MAGIC ||
            Open->Device != KmdDevice)
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
        Open->Device = NULL;
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

static NTSTATUS
SoftGpuSetVsyncState(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ DXGK_CRTC_VSYNC_STATE VsyncState)
{
    LARGE_INTEGER Due;
    SOFTGPU_VSYNC_POLICY Policy;

    if (!Device->VsyncTimerInitialized ||
        !SoftGpuVsyncEvaluatePolicy((ULONG)VsyncState, &Policy))
    {
        return !Device->VsyncTimerInitialized
                   ? STATUS_INVALID_DEVICE_STATE
                   : STATUS_INVALID_PARAMETER;
    }

    InterlockedExchange(
        &Device->VsyncEnabled,
        Policy.NotificationEnabled ? 1 : 0);

    if (Policy.PhaseEnabled)
    {
        if (InterlockedExchange(&Device->VsyncPhaseEnabled, 1) == 0)
        {
            Due.QuadPart = -166667;
            KeSetTimerEx(&Device->VsyncTimer, Due, 16, &Device->VsyncDpc);
        }
    }
    else
    {
        InterlockedExchange(&Device->VsyncPhaseEnabled, 0);
        if (Policy.CancelTimer)
            KeCancelTimer(&Device->VsyncTimer);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
SoftGpuSetNonVsyncInterruptState(
    _In_ DXGK_INTERRUPT_TYPE InterruptType,
    _In_ DXGK_INTERRUPT_STATE InterruptState)
{
    if (InterruptState != DXGK_INTERRUPT_ENABLE &&
        InterruptState != DXGK_INTERRUPT_DISABLE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterruptType != DXGK_INTERRUPT_TYPE_DMA_COMPLETED &&
        InterruptType != DXGK_INTERRUPT_TYPE_DMA_PREEMPTED)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    /* Completion is synthesized by the submit/preempt paths. */
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
SoftGpuDdiControlInterrupt(
    _In_ PVOID                     MiniportDeviceContext,
    _In_ CONST DXGK_INTERRUPT_TYPE InterruptType,
    _In_ BOOLEAN                   EnableInterrupt)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    if (InterruptType == DXGK_INTERRUPT_CRTC_VSYNC)
        return SoftGpuSetVsyncState(
                   Device,
                   EnableInterrupt
                       ? DXGK_VSYNC_ENABLE
                       : DXGK_VSYNC_DISABLE_NO_PHASE);

    return SoftGpuSetNonVsyncInterruptState(
               InterruptType,
               EnableInterrupt
                   ? DXGK_INTERRUPT_ENABLE
                   : DXGK_INTERRUPT_DISABLE);
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 1300)
NTSTATUS
APIENTRY
SoftGpuDdiControlInterrupt2(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_CONTROLINTERRUPT2 InterruptControl)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    if (InterruptControl.InterruptType == DXGK_INTERRUPT_CRTC_VSYNC)
        return SoftGpuSetVsyncState(Device, InterruptControl.CrtcVsyncState);

    return SoftGpuSetNonVsyncInterruptState(
               InterruptControl.InterruptType,
               InterruptControl.InterruptState);
}
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2600)
NTSTATUS
APIENTRY
SoftGpuDdiCollectDiagnosticInfo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    INOUT_PDXGKARG_COLLECTDIAGNOSTICINFO CollectDiagnosticInfo)
{
    PSOFTGPU_DEVICE Device;
    SOFTGPU_DIAGNOSTIC_PAYLOAD Payload;
    const CHAR *Bucket;
    const CHAR *Description;
    SIZE_T BucketSize;
    SIZE_T DescriptionSize;

    if (PhysicalDeviceObject == NULL || CollectDiagnosticInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    Device = (PSOFTGPU_DEVICE)CollectDiagnosticInfo->hAdapter;
    if (Device != NULL &&
        (Device->Magic != SOFTGPU_DEVICE_MAGIC ||
         Device->PhysicalDeviceObject != PhysicalDeviceObject))
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (CollectDiagnosticInfo->Type)
    {
        case DXGK_DI_ADDDEVICE:
            Bucket = "softgpu_adddevice";
            Description = "softgpu_adddevice_failure_state";
            BucketSize = sizeof("softgpu_adddevice") - 1;
            DescriptionSize =
                sizeof("softgpu_adddevice_failure_state") - 1;
            break;

        case DXGK_DI_STARTDEVICE:
            Bucket = "softgpu_startdevice";
            Description = "softgpu_startdevice_failure_state";
            BucketSize = sizeof("softgpu_startdevice") - 1;
            DescriptionSize =
                sizeof("softgpu_startdevice_failure_state") - 1;
            break;

        case DXGK_DI_BLACKSCREEN:
#if (REACTOS_WDDM_TARGET_LEVEL >= 2700)
            Bucket = "softgpu_blackscreen";
            Description = "softgpu_scanout_failure_state";
            BucketSize = sizeof("softgpu_blackscreen") - 1;
            DescriptionSize =
                sizeof("softgpu_scanout_failure_state") - 1;
            break;
#else
            return STATUS_NOT_SUPPORTED;
#endif

        default:
            return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(CollectDiagnosticInfo->BucketingString,
                  sizeof(CollectDiagnosticInfo->BucketingString));
    RtlZeroMemory(CollectDiagnosticInfo->DescriptionString,
                  sizeof(CollectDiagnosticInfo->DescriptionString));
    RtlCopyMemory(CollectDiagnosticInfo->BucketingString,
                  Bucket,
                  min(BucketSize,
                      sizeof(CollectDiagnosticInfo->BucketingString) - 1));
    RtlCopyMemory(CollectDiagnosticInfo->DescriptionString,
                  Description,
                  min(DescriptionSize,
                      sizeof(CollectDiagnosticInfo->DescriptionString) - 1));

    CollectDiagnosticInfo->BufferSizeOut = 0;
    if (CollectDiagnosticInfo->BufferSizeIn != 0 &&
        CollectDiagnosticInfo->pBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (CollectDiagnosticInfo->BufferSizeIn < sizeof(Payload))
        return STATUS_SUCCESS;

    RtlZeroMemory(&Payload, sizeof(Payload));
    Payload.Size = sizeof(Payload);
    Payload.Version = SOFTGPU_DIAGNOSTIC_PAYLOAD_VERSION;
    Payload.TargetLevel = REACTOS_WDDM_TARGET_LEVEL;
    Payload.Type = CollectDiagnosticInfo->Type;
    Payload.AdapterContextAvailable = (Device != NULL);
    RtlCopyMemory(CollectDiagnosticInfo->pBuffer, &Payload, sizeof(Payload));
    CollectDiagnosticInfo->BufferSizeOut = sizeof(Payload);
    return STATUS_SUCCESS;
}
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2700)
NTSTATUS
APIENTRY
SoftGpuDdiControlInterrupt3(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_CONTROLINTERRUPT3 *InterruptControl)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        InterruptControl == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterruptControl->InterruptType == DXGK_INTERRUPT_CRTC_VSYNC)
    {
        if (InterruptControl->VidPnSourceId != 0)
            return STATUS_INVALID_PARAMETER;
        return SoftGpuSetVsyncState(Device, InterruptControl->CrtcVsyncState);
    }

    return SoftGpuSetNonVsyncInterruptState(
               InterruptControl->InterruptType,
               InterruptControl->InterruptState);
}
#endif

/* EOF */
