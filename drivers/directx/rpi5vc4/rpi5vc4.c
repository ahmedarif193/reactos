/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM display-only miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DriverEntry, PnP/power lifecycle, child devices, adapter
 *              capabilities and the HVS hardware-cursor pointer DDIs.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Display path: dxgkrnl hands the firmware GOP framebuffer over in
 * StartDevice (DxgkCbAcquirePostDisplayOwnership); this driver installs its
 * own HVS display list scanning that framebuffer and afterwards receives the
 * desktop through DxgkDdiPresentDisplayOnly dirty-rect blits.  The mouse
 * cursor is a second, per-pixel-alpha HVS plane driven by the pointer DDIs.
 */

#include "rpi5vc4.h"
#include "rpi5vc4_hvs.h"
#include "rpi5vc4_crtc.h"
#include "rpi5vc4_v3d.h"
#include "rpi5vc4_mbox.h"
#include "rpi5vc4_iommu.h"

#define NDEBUG
#include <reactos/debug.h>

#define RPI5VC4_ACPI_SIGNATURE(a, b, c, d) \
    ((ULONG)(a) | ((ULONG)(b) << 8) | ((ULONG)(c) << 16) | ((ULONG)(d) << 24))

#define RPI5VC4_ACPI_FADT RPI5VC4_ACPI_SIGNATURE('F', 'A', 'C', 'P')

BOOLEAN
Rpi5Vc4IsRpi5Platform(VOID)
{
    if (HalGetCachedAcpiTable == NULL)
        return FALSE;

    /* Match the stable OEM table id only: firmware revisions vary the OEM id
     * ("RPIFDN" vs "NXPMX6") across tables. */
    return HalGetCachedAcpiTable(RPI5VC4_ACPI_FADT, NULL, "RPI5") != NULL;
}

/* Allocate the hardware cursor surface used by the HVS overlay plane. */
static VOID
Rpi5Vc4InitCursor(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    const SIZE_T Bytes = RPI5VC4_CURSOR_WIDTH * RPI5VC4_CURSOR_HEIGHT * sizeof(ULONG);

    if (DeviceExtension->CursorVa != NULL)
        return;

    /*
     * Allocate above the firmware framebuffer (which lives just below 1GB at
     * 0x3F400000 and is the one address we know the HVS scans). Low DRAM is
     * often VPU-reserved and not in the HVS's view, so force a high buffer.
     */
    Low.QuadPart = 0x40000000ULL;
    High.QuadPart = 0xFFFFFFFFFFULL;   /* 40-bit DMA reach of the HVS */
    Boundary.QuadPart = 0;

    DeviceExtension->CursorVa = MmAllocateContiguousMemorySpecifyCache(
        Bytes, Low, High, Boundary, MmWriteCombined);
    if (DeviceExtension->CursorVa == NULL)
    {
        DPRINT1("RPI5VC4: cursor buffer alloc failed\n");
        DeviceExtension->CursorVisible = FALSE;
        return;
    }
    DeviceExtension->CursorPhys = MmGetPhysicalAddress(DeviceExtension->CursorVa);
    DeviceExtension->CursorWidth = RPI5VC4_CURSOR_WIDTH;
    DeviceExtension->CursorHeight = RPI5VC4_CURSOR_HEIGHT;
    DeviceExtension->CursorVisible = FALSE;
    DeviceExtension->CursorShapeValid = FALSE;
    RtlZeroMemory(DeviceExtension->CursorVa, Bytes);
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
}

/*
 * Allocate the local VRAM slab (segment 1): a contiguous write-combined
 * buffer above the 1 GB mark (in the HVS's and V3D's 40-bit DMA view)
 * holding every WDDM allocation, including flip targets.
 */
static BOOLEAN
Rpi5Vc4AllocateVram(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    ULONG Size;

    Low.QuadPart = 0x40000000ULL;
    High.QuadPart = 0xFFFFFFFFFFULL;
    Boundary.QuadPart = 0;

    for (Size = RPI5VC4_VRAM_SIZE_PREFERRED;
         Size >= RPI5VC4_VRAM_SIZE_MIN;
         Size /= 2)
    {
        DeviceExtension->VramVa = MmAllocateContiguousMemorySpecifyCache(
            Size, Low, High, Boundary, MmWriteCombined);
        if (DeviceExtension->VramVa != NULL)
            break;
    }

    if (DeviceExtension->VramVa == NULL)
    {
        DPRINT1("RPI5VC4: VRAM slab alloc failed\n");
        return FALSE;
    }

    DeviceExtension->VramSize = Size;
    DeviceExtension->VramPhysical =
        MmGetPhysicalAddress(DeviceExtension->VramVa);

#if defined(_M_ARM64)
    /*
     * The contiguous allocator leaves a cacheable kernel-linear alias of
     * these pages.  Stale dirty lines from their previous life would write
     * back over what the HVS scans / the V3D reads, so clean+invalidate the
     * alias once now.  All later CPU access goes through WC mappings only.
     */
    {
        SIZE_T Offset;

        for (Offset = 0; Offset < Size; Offset += PAGE_SIZE)
        {
            PHYSICAL_ADDRESS PagePhys;
            PUCHAR AliasVa;
            SIZE_T Line;

            PagePhys.QuadPart = DeviceExtension->VramPhysical.QuadPart + Offset;
            AliasVa = MmGetVirtualForPhysical(PagePhys);
            if (AliasVa == NULL)
                continue;

            for (Line = 0; Line < PAGE_SIZE; Line += 64)
            {
                __asm__ __volatile__("dc civac, %0"
                                     :: "r"(AliasVa + Line) : "memory");
            }
        }
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
#endif

    RtlZeroMemory(DeviceExtension->VramVa, Size);
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif

    DPRINT("RPI5VC4: VRAM slab %lu MB at phys 0x%I64x\n",
           Size / (1024 * 1024), DeviceExtension->VramPhysical.QuadPart);
    return TRUE;
}

static VOID
Rpi5Vc4FreeVram(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->VramVa != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(DeviceExtension->VramVa,
                                           DeviceExtension->VramSize,
                                           MmWriteCombined);
        DeviceExtension->VramVa = NULL;
        DeviceExtension->VramSize = 0;
        DeviceExtension->VramPhysical.QuadPart = 0;
    }
}

static VOID
Rpi5Vc4FreeCursor(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->CursorVa != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(
            DeviceExtension->CursorVa,
            RPI5VC4_CURSOR_WIDTH * RPI5VC4_CURSOR_HEIGHT * sizeof(ULONG),
            MmWriteCombined);
        DeviceExtension->CursorVa = NULL;
        DeviceExtension->CursorPhys.QuadPart = 0;
    }
    DeviceExtension->CursorVisible = FALSE;
    DeviceExtension->CursorShapeValid = FALSE;
}

/* ========================================================================
 * DriverEntry
 * ====================================================================== */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    DRIVER_INITIALIZATION_DATA InitData;

    if (!Rpi5Vc4IsRpi5Platform())
    {
        /*
         * ARM64 images can boot on generic UEFI systems. Refuse a non-Pi
         * binding before registering the miniport with dxgkrnl.
         */
        DPRINT("RPI5VC4: not a Raspberry Pi 5 - declining\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RtlZeroMemory(&InitData, sizeof(InitData));
    /*
     * The compile-time selector exposes declarations; it does not promote the
     * miniport's implemented contract. DRIVER_INITIALIZATION_DATA is
     * append-only, so compiling with later declarations does not move any
     * WDDM 2.0 member. DxgkInitialize uses Version to copy exactly the prefix
     * the miniport promises to implement.
     */
    InitData.Version = DXGKDDI_INTERFACE_VERSION_WDDM2_0;

    /* PnP / power lifecycle */
    InitData.DxgkDdiAddDevice             = Rpi5Vc4DdiAddDevice;
    InitData.DxgkDdiStartDevice           = Rpi5Vc4DdiStartDevice;
    InitData.DxgkDdiStopDevice            = Rpi5Vc4DdiStopDevice;
    InitData.DxgkDdiRemoveDevice          = Rpi5Vc4DdiRemoveDevice;
    InitData.DxgkDdiInterruptRoutine      = Rpi5Vc4DdiInterruptRoutine;
    InitData.DxgkDdiDpcRoutine            = Rpi5Vc4DdiDpcRoutine;
    InitData.DxgkDdiQueryChildRelations   = Rpi5Vc4DdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus      = Rpi5Vc4DdiQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor = Rpi5Vc4DdiQueryDeviceDescriptor;
    InitData.DxgkDdiSetPowerState         = Rpi5Vc4DdiSetPowerState;
    InitData.DxgkDdiResetDevice           = Rpi5Vc4DdiResetDevice;
    InitData.DxgkDdiUnload                = Rpi5Vc4DdiUnload;
    InitData.DxgkDdiQueryAdapterInfo      = Rpi5Vc4DdiQueryAdapterInfo;

    /* Memory management — the VRAM slab segment */
    InitData.DxgkDdiCreateAllocation      = Rpi5Vc4DdiCreateAllocation;
    InitData.DxgkDdiDestroyAllocation     = Rpi5Vc4DdiDestroyAllocation;
    InitData.DxgkDdiOpenAllocation        = Rpi5Vc4DdiOpenAllocation;
    InitData.DxgkDdiCloseAllocation       = Rpi5Vc4DdiCloseAllocation;
    InitData.DxgkDdiGetStandardAllocationDriverData =
        Rpi5Vc4DdiGetStandardAllocationDriverData;
    InitData.DxgkDdiBuildPagingBuffer     = Rpi5Vc4DdiBuildPagingBuffer;

    /* Command submission / fences (the in-order pipeline) */
    InitData.DxgkDdiRender                = Rpi5Vc4DdiRender;
    InitData.DxgkDdiPresent               = Rpi5Vc4DdiPresent;
    InitData.DxgkDdiPatch                 = Rpi5Vc4DdiPatch;
    InitData.DxgkDdiSubmitCommand         = Rpi5Vc4DdiSubmitCommand;
    InitData.DxgkDdiQueryCurrentFence     = Rpi5Vc4DdiQueryCurrentFence;
    InitData.DxgkDdiGetNodeMetadata       = Rpi5Vc4DdiGetNodeMetadata;
    InitData.DxgkDdiResetFromTimeout      = Rpi5Vc4DdiResetFromTimeout;
    InitData.DxgkDdiRestartFromTimeout    = Rpi5Vc4DdiRestartFromTimeout;
    InitData.DxgkDdiControlInterrupt      = Rpi5Vc4DdiControlInterrupt;
    InitData.DxgkDdiEscape                = Rpi5Vc4DdiEscape;

    /* Per-device / per-context objects */
    InitData.DxgkDdiCreateDevice          = Rpi5Vc4DdiCreateDevice;
    InitData.DxgkDdiDestroyDevice         = Rpi5Vc4DdiDestroyDevice;
    InitData.DxgkDdiCreateContext         = Rpi5Vc4DdiCreateContext;
    InitData.DxgkDdiDestroyContext        = Rpi5Vc4DdiDestroyContext;

    /* Hardware cursor (HVS overlay plane) */
    InitData.DxgkDdiSetPointerPosition    = Rpi5Vc4DdiSetPointerPosition;
    InitData.DxgkDdiSetPointerShape       = Rpi5Vc4DdiSetPointerShape;

    /* VidPN management + real HVS page flips */
    InitData.DxgkDdiIsSupportedVidPn         = Rpi5Vc4DdiIsSupportedVidPn;
    InitData.DxgkDdiRecommendFunctionalVidPn = Rpi5Vc4DdiRecommendFunctionalVidPn;
    InitData.DxgkDdiEnumVidPnCofuncModality  = Rpi5Vc4DdiEnumVidPnCofuncModality;
    InitData.DxgkDdiSetVidPnSourceAddress    = Rpi5Vc4DdiSetVidPnSourceAddress;
    InitData.DxgkDdiSetVidPnSourceVisibility = Rpi5Vc4DdiSetVidPnSourceVisibility;
    InitData.DxgkDdiCommitVidPn              = Rpi5Vc4DdiCommitVidPn;
    InitData.DxgkDdiUpdateActiveVidPnPresentPath =
        Rpi5Vc4DdiUpdateActiveVidPnPresentPath;
    InitData.DxgkDdiRecommendMonitorModes    = Rpi5Vc4DdiRecommendMonitorModes;

    /* Multi-plane overlay (HVS planes; base-plane flips today) */
    InitData.DxgkDdiCheckMultiPlaneOverlaySupport =
        Rpi5Vc4DdiCheckMultiPlaneOverlaySupport;
    InitData.DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay =
        Rpi5Vc4DdiSetVidPnSourceAddressWithMultiPlaneOverlay;

    /* Boot-display handoff + bugcheck-time display */
    InitData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership =
        Rpi5Vc4DdiStopDeviceAndReleasePostDisplayOwnership;
    InitData.DxgkDdiSystemDisplayEnable   = Rpi5Vc4DdiSystemDisplayEnable;
    InitData.DxgkDdiSystemDisplayWrite    = Rpi5Vc4DdiSystemDisplayWrite;

    return DxgkInitialize(DriverObject, RegistryPath, &InitData);
}

/* ========================================================================
 * PnP lifecycle
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiAddDevice(
    _In_  PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PVOID *MiniportDeviceContext)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension;

    if (MiniportDeviceContext == NULL || PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;

    *MiniportDeviceContext = NULL;

    DeviceExtension = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(RPI5VC4_DEVICE_EXTENSION),
                                            RPI5VC4_POOL_TAG);
    if (DeviceExtension == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->PhysicalDeviceObject = PhysicalDeviceObject;

    *MiniportDeviceContext = DeviceExtension;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiStartDevice(
    _In_  PVOID MiniportDeviceContext,
    _In_  PDXGK_START_INFO DxgkStartInfo,
    _In_  PDXGK_INTERFACE DxgkInterface,
    _Out_ PULONG NumberOfVideoPresentSources,
    _Out_ PULONG NumberOfChildren)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    DXGK_DISPLAY_INFORMATION DisplayInfo;
    ULONGLONG FrameBufferSize;
    NTSTATUS Status;

    if (DeviceExtension == NULL ||
        DxgkStartInfo == NULL ||
        DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL ||
        NumberOfChildren == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbAcquirePostDisplayOwnership == NULL)
    {
        DPRINT1("RPI5VC4: StartDevice: incomplete DxgkInterface\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (!Rpi5Vc4IsRpi5Platform())
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    RtlCopyMemory(&DeviceExtension->DxgkInterface,
                  DxgkInterface,
                  min(DxgkInterface->Size, sizeof(DXGK_INTERFACE)));

    /*
     * Take over the firmware GOP framebuffer.  dxgkrnl reads the loader
     * framebuffer info, maps it and silences Inbv; we get the physical
     * address and raster geometry the HVS is scanning out.
     */
    RtlZeroMemory(&DisplayInfo, sizeof(DisplayInfo));
    Status = DxgkInterface->DxgkCbAcquirePostDisplayOwnership(
                 DxgkInterface->DeviceHandle, &DisplayInfo);
    if (!NT_SUCCESS(Status) ||
        DisplayInfo.Width == 0 ||
        DisplayInfo.Height == 0 ||
        DisplayInfo.Pitch == 0 ||
        DisplayInfo.PhysicAddress.QuadPart == 0)
    {
        /*
         * Headless boot: the firmware initialized no display (no HDMI sink
         * at UEFI time), so there is no GOP state to inherit. Own a RAM
         * scanout and report a phantom monitor so the session comes up —
         * HVS/PV/HDMI are never touched (a cold-start modeset is the
         * follow-up that lights real outputs from here).
         */
        PHYSICAL_ADDRESS Low, High, Skip;
        SIZE_T Size;

        DPRINT1("RPI5VC4: no POST framebuffer (status=0x%08lx) - headless "
                "start, phantom 1024x768\n", Status);

        DeviceExtension->ScreenWidth = 1024;
        DeviceExtension->ScreenHeight = 768;
        DeviceExtension->BytesPerScanLine = 1024 * 4;
        DeviceExtension->PixelsPerScanLine = 1024;
        DeviceExtension->BitsPerPixel = 32;
        DeviceExtension->ColorFormat = D3DDDIFMT_X8R8G8B8;

        Size = (SIZE_T)DeviceExtension->BytesPerScanLine *
               DeviceExtension->ScreenHeight;
        Low.QuadPart = 0;
        High.QuadPart = 0xFFFFFFFF;
        Skip.QuadPart = 0;
        DeviceExtension->HeadlessFbVa =
            MmAllocateContiguousMemorySpecifyCache(Size, Low, High, Skip,
                                                   MmWriteCombined);
        if (DeviceExtension->HeadlessFbVa == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(DeviceExtension->HeadlessFbVa, Size);

        DeviceExtension->FrameBufferVa = DeviceExtension->HeadlessFbVa;
        DeviceExtension->FrameBufferPhysical =
            MmGetPhysicalAddress(DeviceExtension->HeadlessFbVa);
        DeviceExtension->FirmwareFrameBufferPhysical.QuadPart = 0;
        DeviceExtension->FrameBufferSize = (ULONG)Size;
        DeviceExtension->Headless = TRUE;

        /* Hotplug probe (stage 2): firmware-DDC EDID poll on an independent
         * timer (armed in the pipeline init below, since dxgkrnl leaves the
         * display vsync off while headless); on connect the worker cold-starts
         * the display. */
        DeviceExtension->HpdWorkItem =
            IoAllocateWorkItem(DeviceExtension->PhysicalDeviceObject);
    }
    else
    {
    FrameBufferSize = (ULONGLONG)DisplayInfo.Pitch * DisplayInfo.Height;
    if (FrameBufferSize == 0 || FrameBufferSize > MAXULONG)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    DeviceExtension->FirmwareFrameBufferPhysical = DisplayInfo.PhysicAddress;
    DeviceExtension->FrameBufferPhysical = DisplayInfo.PhysicAddress;
    DeviceExtension->FrameBufferSize = (ULONG)FrameBufferSize;
    DeviceExtension->ScreenWidth = DisplayInfo.Width;
    DeviceExtension->ScreenHeight = DisplayInfo.Height;
    DeviceExtension->BytesPerScanLine = DisplayInfo.Pitch;
    DeviceExtension->PixelsPerScanLine = DisplayInfo.Pitch / 4;
    DeviceExtension->BitsPerPixel = 32;
    DeviceExtension->ColorFormat = (DisplayInfo.ColorFormat != D3DDDIFMT_UNKNOWN)
                                       ? DisplayInfo.ColorFormat
                                       : D3DDDIFMT_X8R8G8B8;

    /*
     * The firmware framebuffer is reserved by the loader as
     * LoaderFirmwarePermanent, so this write-combined mapping is its only
     * kernel view and stays coherent with the (non-coherent) HVS.  A private
     * scanout buffer is NOT used: MmAllocateContiguousMemorySpecifyCache
     * leaves a cacheable kernel-linear alias whose stale lines corrupt the
     * scanout (see the XPDM driver history).
     */
    DeviceExtension->FrameBufferVa = MmMapIoSpace(
        DeviceExtension->FrameBufferPhysical,
        DeviceExtension->FrameBufferSize,
        MmWriteCombined);
    if (DeviceExtension->FrameBufferVa == NULL)
    {
        DeviceExtension->FrameBufferVa = MmMapIoSpace(
            DeviceExtension->FrameBufferPhysical,
            DeviceExtension->FrameBufferSize,
            MmNonCached);
    }
    if (DeviceExtension->FrameBufferVa == NULL)
    {
        DPRINT1("RPI5VC4: framebuffer map failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    }

    Rpi5Vc4InitCursor(DeviceExtension);

    /* Local VRAM segment + the in-order submission pipeline. */
    if (!Rpi5Vc4AllocateVram(DeviceExtension))
    {
        MmUnmapIoSpace(DeviceExtension->FrameBufferVa,
                       DeviceExtension->FrameBufferSize);
        DeviceExtension->FrameBufferVa = NULL;
        Rpi5Vc4FreeCursor(DeviceExtension);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Rpi5Vc4DmaPipelineInit(DeviceExtension);

    /* Verify the HVS's system IOMMU won't undercut slab scanout
     * (read-only; see rpi5vc4_iommu.h for the pass-through analysis). */
    if (!DeviceExtension->Headless)
        Rpi5HvsIommuDumpState();

    /*
     * Firmware property mailbox: identify the firmware and make sure the
     * V3D core clock is running before the SMS power-up touches it.
     * All best-effort — the V3D init gates itself on the hub responding.
     */
    if (Rpi5MboxInitialize(DeviceExtension))
    {
        ULONG RateHz = 0;

        if (Rpi5MboxGetFirmwareRevision(DeviceExtension,
                                        &DeviceExtension->FirmwareRevision))
        {
            DPRINT1("RPI5VC4: VideoCore firmware revision 0x%08lx\n",
                    DeviceExtension->FirmwareRevision);
        }

        if (!Rpi5MboxSetClockState(DeviceExtension, RPI5_MBOX_CLOCK_V3D, TRUE))
            DPRINT1("RPI5VC4: V3D clock enable via mailbox failed\n");

        /* Pin the clock: firmware DVFS transitions mid-vertex-fetch are a
         * park suspect (500MHz experiment — parks vanish => clock root). */
        if (!Rpi5MboxSetClockRate(DeviceExtension, RPI5_MBOX_CLOCK_V3D,
                                  500 * 1000 * 1000))
            DPRINT1("RPI5VC4: V3D clock pin failed\n");

        if (Rpi5MboxGetClockRate(DeviceExtension, RPI5_MBOX_CLOCK_V3D, &RateHz))
            DPRINT1("RPI5VC4: V3D clock at %lu MHz\n", RateHz / 1000000);
    }
    else
    {
        DPRINT1("RPI5VC4: firmware mailbox unavailable\n");
    }

    /* Bring up the V3D 3D engine (2D-only when unavailable). */
    if (!Rpi5V3dInitialize(DeviceExtension))
    {
        DPRINT1("RPI5VC4: V3D unavailable — continuing 2D-only\n");
    }
    else
    {
        Rpi5V3dConnectInterrupt(DeviceExtension);
        Rpi5Vc4QueueWarmupV3dJob(DeviceExtension);
    }

    /* Record the live PixelValve raster timing and re-assert it. */
    if (!DeviceExtension->Headless && Rpi5CrtcReportTiming(DeviceExtension))
        Rpi5CrtcProgramCurrentTiming(DeviceExtension);

    /*
     * Take ownership of the HVS scanout: install our own display list (built
     * to ignore the per-pixel source alpha, i.e. XRGB) over the firmware's at
     * the live head. Without this the HVS treats the top byte as alpha, so
     * GDI-drawn pixels (which leave it 0) are composited to black.
     */
    Rpi5HvsInstallScanout(DeviceExtension);

    DeviceExtension->SourceVisible = TRUE;
    DeviceExtension->Started = TRUE;

    /* Headless boot: start polling for an HDMI hotplug now that the pipeline
     * is fully started, so StopDevice's drain always tears the poll down. */
    if (DeviceExtension->Headless && DeviceExtension->HpdWorkItem != NULL)
        Rpi5Vc4ArmHpdTimer(DeviceExtension);

    *NumberOfVideoPresentSources = 1;
    *NumberOfChildren = RPI5VC4_CHILD_COUNT;

    DPRINT("RPI5VC4: StartDevice: %lux%lu pitch=%lu fb=0x%I64x\n",
           DeviceExtension->ScreenWidth,
           DeviceExtension->ScreenHeight,
           DeviceExtension->BytesPerScanLine,
           DeviceExtension->FrameBufferPhysical.QuadPart);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiStopDevice(
    _In_ PVOID MiniportDeviceContext)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Close every producer before any timer, DPC, worker, MMIO mapping, or
     * backing allocation can be released. This is idempotent for RemoveDevice. */
    DeviceExtension->StopAccepting = TRUE;
    Rpi5V3dDisconnectInterrupt(DeviceExtension);
    if (DeviceExtension->DmaPipelineInitialized)
        Rpi5Vc4DmaPipelineDrain(DeviceExtension);
    Rpi5V3dTeardown(DeviceExtension);
    Rpi5MboxTeardown(DeviceExtension);

    /* Restore the firmware framebuffer as the (only) scanout plane. */
    DeviceExtension->CursorVisible = FALSE;
    if (DeviceExtension->Started && !DeviceExtension->Headless)
    {
        Rpi5HvsFlipScanout(DeviceExtension,
                           DeviceExtension->FirmwareFrameBufferPhysical);
        Rpi5HvsInstallScanout(DeviceExtension);
    }

    Rpi5Vc4FreeCursor(DeviceExtension);
    Rpi5Vc4FreeFlipRing(DeviceExtension);
    Rpi5Vc4FreeVram(DeviceExtension);

    if (DeviceExtension->FrameBufferVa != NULL)
    {
        if (DeviceExtension->Headless)
        {
            MmFreeContiguousMemorySpecifyCache(DeviceExtension->HeadlessFbVa,
                                               DeviceExtension->FrameBufferSize,
                                               MmWriteCombined);
            DeviceExtension->HeadlessFbVa = NULL;
        }
        else
        {
            MmUnmapIoSpace(DeviceExtension->FrameBufferVa,
                           DeviceExtension->FrameBufferSize);
        }
        DeviceExtension->FrameBufferVa = NULL;
    }

    /* Release the headless hotplug work item (drained above by
     * Rpi5Vc4DmaPipelineDrain; never queued if StartDevice never armed it). */
    if (DeviceExtension->HpdWorkItem != NULL)
    {
        IoFreeWorkItem(DeviceExtension->HpdWorkItem);
        DeviceExtension->HpdWorkItem = NULL;
    }

    if (DeviceExtension->HvsBase != NULL)
    {
        MmUnmapIoSpace(DeviceExtension->HvsBase, RPI5_HVS_LENGTH);
        DeviceExtension->HvsBase = NULL;
    }

    if (DeviceExtension->PixelValveBase != NULL)
    {
        MmUnmapIoSpace(DeviceExtension->PixelValveBase, RPI5_PV_LENGTH);
        DeviceExtension->PixelValveBase = NULL;
    }

    DeviceExtension->HvsCursorFastValid = FALSE;
    DeviceExtension->VidPnCommitted = FALSE;
    DeviceExtension->Started = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext)
{
    NTSTATUS Status;

    if (MiniportDeviceContext == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = Rpi5Vc4DdiStopDevice(MiniportDeviceContext);
    if (!NT_SUCCESS(Status))
        return Status;

    ExFreePoolWithTag(MiniportDeviceContext, RPI5VC4_POOL_TAG);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * Child devices — one always-connected HDMI output
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiQueryChildRelations(
    _In_  PVOID MiniportDeviceContext,
    _Out_ PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_  ULONG ChildRelationsSize)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    ULONG Child;

    if (ChildRelations == NULL ||
        ChildRelationsSize < RPI5VC4_CHILD_COUNT * sizeof(DXGK_CHILD_DESCRIPTOR))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    /*
     * The BCM2712 drives two HDMI connectors (HDMI0/HDMI1).  The boot
     * display (whichever port the firmware lit) is child 0 and reported
     * always-connected; the second port is enumerated honestly as a
     * connector but stays disconnected until real HPD probing exists
     * (parity roadmap 2.9).
     */
    RtlZeroMemory(ChildRelations,
                  RPI5VC4_CHILD_COUNT * sizeof(DXGK_CHILD_DESCRIPTOR));
    for (Child = 0; Child < RPI5VC4_CHILD_COUNT; Child++)
    {
        ChildRelations[Child].ChildDeviceType = TypeVideoOutput;
        ChildRelations[Child].ChildCapabilities.HpdAwareness =
            (Child == 0) ? HpdAwarenessAlwaysConnected
                         : HpdAwarenessPolled;
        ChildRelations[Child].ChildCapabilities.Type.VideoOutput.InterfaceTechnology =
            D3DKMDT_VOT_HDMI;
        ChildRelations[Child].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness =
            D3DKMDT_MOA_NONE;
        ChildRelations[Child].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        ChildRelations[Child].AcpiUid = Child;
        ChildRelations[Child].ChildUid = Child;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiQueryChildStatus(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS ChildStatus,
    _In_    BOOLEAN NonDestructiveOnly)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    if (ChildStatus == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ChildStatus->Type == StatusConnection)
    {
        /* Child 0 is the firmware-lit boot display; the second HDMI
         * port reports disconnected until HPD probing exists. */
        ChildStatus->HotPlug.Connected = (ChildStatus->ChildUid == 0);
    }

    return STATUS_SUCCESS;
}

/*
 * Synthesize an EDID 1.3 base block for the firmware-negotiated mode, the
 * way Windows drivers for fixed panels do.  The detailed timing descriptor
 * carries the live PixelValve raster when it was captured, else a CVT-ish
 * 60 Hz estimate around the GOP resolution.
 */
static VOID
Rpi5Vc4BuildEdid(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(128) PUCHAR Edid)
{
    ULONG HActive = DeviceExtension->ScreenWidth;
    ULONG VActive = DeviceExtension->ScreenHeight;
    ULONG HBlank = HActive / 5 + 8;      /* estimate */
    ULONG VBlank = VActive / 20 + 4;
    ULONG HSyncOff = 8, HSyncWidth = 32;
    ULONG VSyncOff = 2, VSyncWidth = 5;
    ULONG PixelClock10kHz;
    UCHAR Checksum = 0;
    ULONG i;

    if (DeviceExtension->PixelValveValid)
    {
        /* PV registers: HORZA = HBP:HSYNC, HORZB = HFP:HACTIVE,
         * VERTA = VBP:VSYNC, VERTB = VFP:VACTIVE (16-bit halves). */
        ULONG HorzA = DeviceExtension->PixelValveHorzA;
        ULONG HorzB = DeviceExtension->PixelValveHorzB;
        ULONG VertA = DeviceExtension->PixelValveVertA;
        ULONG VertB = DeviceExtension->PixelValveVertB;

        HSyncWidth = HorzA & 0xFFFF;
        HSyncOff = (HorzB >> 16) & 0xFFFF;           /* front porch */
        HBlank = HSyncWidth + HSyncOff + ((HorzA >> 16) & 0xFFFF);
        VSyncWidth = VertA & 0xFFFF;
        VSyncOff = (VertB >> 16) & 0xFFFF;
        VBlank = VSyncWidth + VSyncOff + ((VertA >> 16) & 0xFFFF);

        if ((HorzB & 0xFFFF) != 0)
            HActive = HorzB & 0xFFFF;
        if ((VertB & 0xFFFF) != 0)
            VActive = VertB & 0xFFFF;
    }

    PixelClock10kHz = (ULONG)(((ULONGLONG)(HActive + HBlank) *
                               (VActive + VBlank) * 60ULL) / 10000ULL);

    RtlZeroMemory(Edid, 128);

    /* Header + vendor "RPF" (RPi Foundation), product 0x2712. */
    Edid[0] = 0x00; Edid[1] = 0xFF; Edid[2] = 0xFF; Edid[3] = 0xFF;
    Edid[4] = 0xFF; Edid[5] = 0xFF; Edid[6] = 0xFF; Edid[7] = 0x00;
    Edid[8] = 0x4A;                     /* 'R'=18,'P'=16,'F'=6 packed */
    Edid[9] = 0x06;
    Edid[10] = 0x12; Edid[11] = 0x27;   /* product code (LE)         */
    Edid[17] = 36;                      /* year 2026                 */
    Edid[18] = 1; Edid[19] = 3;         /* EDID 1.3                  */
    Edid[20] = 0x80;                    /* digital input             */
    Edid[24] = 0x08;                    /* RGB color                 */

    /* Detailed timing descriptor #1 (bytes 54-71). */
    Edid[54] = (UCHAR)(PixelClock10kHz & 0xFF);
    Edid[55] = (UCHAR)(PixelClock10kHz >> 8);
    Edid[56] = (UCHAR)(HActive & 0xFF);
    Edid[57] = (UCHAR)(HBlank & 0xFF);
    Edid[58] = (UCHAR)(((HActive >> 8) << 4) | ((HBlank >> 8) & 0x0F));
    Edid[59] = (UCHAR)(VActive & 0xFF);
    Edid[60] = (UCHAR)(VBlank & 0xFF);
    Edid[61] = (UCHAR)(((VActive >> 8) << 4) | ((VBlank >> 8) & 0x0F));
    Edid[62] = (UCHAR)(HSyncOff & 0xFF);
    Edid[63] = (UCHAR)(HSyncWidth & 0xFF);
    Edid[64] = (UCHAR)((((VSyncOff & 0xF) << 4) | (VSyncWidth & 0xF)));
    Edid[65] = (UCHAR)((((HSyncOff >> 8) & 3) << 6) |
                       (((HSyncWidth >> 8) & 3) << 4) |
                       (((VSyncOff >> 4) & 3) << 2) |
                       ((VSyncWidth >> 4) & 3));
    Edid[71] = 0x1E;                    /* digital separate sync     */

    /* Descriptors 2-4: dummy (type 0x10). */
    Edid[75] = 0x10;
    Edid[93] = 0x10;
    Edid[111] = 0x10;

    for (i = 0; i < 127; i++)
        Checksum = (UCHAR)(Checksum + Edid[i]);
    Edid[127] = (UCHAR)(0x100 - Checksum);
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiQueryDeviceDescriptor(
    _In_    PVOID MiniportDeviceContext,
    _In_    ULONG ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    UCHAR Edid[128];
    ULONG CopyLength;

    if (DeviceExtension == NULL || DeviceDescriptor == NULL ||
        ChildUid >= RPI5VC4_CHILD_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Only the boot display has a (synthesized) EDID. */
    if (ChildUid != 0)
        return STATUS_MONITOR_NO_DESCRIPTOR;

    if (DeviceDescriptor->DescriptorOffset >= sizeof(Edid))
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;

    if (DeviceExtension->ScreenWidth == 0 || DeviceExtension->ScreenHeight == 0)
        return STATUS_MONITOR_NO_DESCRIPTOR;

    Rpi5Vc4BuildEdid(DeviceExtension, Edid);

    CopyLength = min(DeviceDescriptor->DescriptorLength,
                     sizeof(Edid) - DeviceDescriptor->DescriptorOffset);
    RtlCopyMemory(DeviceDescriptor->DescriptorBuffer,
                  Edid + DeviceDescriptor->DescriptorOffset,
                  CopyLength);
    DeviceDescriptor->DescriptorLength = CopyLength;

    return STATUS_SUCCESS;
}

/* ========================================================================
 * Power / reset / unload
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiSetPowerState(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION ActionType)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    UNREFERENCED_PARAMETER(ActionType);

    if (DeviceExtension == NULL)
        return STATUS_INVALID_PARAMETER;

    if (DeviceUid != DISPLAY_ADAPTER_HW_ID && DeviceUid != 0)
        return STATUS_SUCCESS; /* only one child output */

    if (!DeviceExtension->Started)
        return STATUS_SUCCESS;

    if (DevicePowerState == PowerDeviceD0)
    {
        /*
         * Wake: re-assert the firmware raster timing and rebuild the
         * scanout display list (VRAM content survived — the slab is
         * ordinary DRAM).
         */
        if (DeviceExtension->PixelValveValid)
            Rpi5CrtcProgramCurrentTiming(DeviceExtension);
        Rpi5HvsInstallScanout(DeviceExtension);
        DeviceExtension->SourceVisible = TRUE;
    }
    else
    {
        /*
         * Sleep: park the scanout on the firmware framebuffer and blank
         * it (no PixelValve/PHY power-down is implemented yet, so black
         * is the closest honest "off" state).
         */
        DeviceExtension->CursorVisible = FALSE;
        Rpi5HvsFlipScanout(DeviceExtension,
                           DeviceExtension->FirmwareFrameBufferPhysical);
        Rpi5HvsInstallScanout(DeviceExtension);

        if (DeviceExtension->FrameBufferVa != NULL)
        {
            RtlZeroMemory(DeviceExtension->FrameBufferVa,
                          DeviceExtension->FrameBufferSize);
#if defined(_M_ARM64)
            __dsb(_ARM64_BARRIER_SY);
#endif
        }
        DeviceExtension->SourceVisible = FALSE;
    }

    return STATUS_SUCCESS;
}

VOID
APIENTRY
Rpi5Vc4DdiResetDevice(
    _In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

VOID
APIENTRY
Rpi5Vc4DdiUnload(VOID)
{
}

/* ========================================================================
 * Hardware cursor — HVS per-pixel-alpha overlay plane
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiSetPointerShape(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    const UCHAR *Source;
    PUCHAR Destination;
    ULONG Row;
    ULONG CopyBytes;

    if (DeviceExtension == NULL || SetPointerShape == NULL)
        return STATUS_INVALID_PARAMETER;

    if (SetPointerShape->VidPnSourceId != 0)
        return STATUS_INVALID_PARAMETER;

    if (DeviceExtension->CursorVa == NULL)
        return STATUS_NOT_SUPPORTED;

    /* Only 32bpp ARGB color shapes; the cdd bridge converts mono cursors. */
    if (!SetPointerShape->Flags.Color ||
        SetPointerShape->pPixels == NULL ||
        SetPointerShape->Width == 0 ||
        SetPointerShape->Height == 0 ||
        SetPointerShape->Width > RPI5VC4_CURSOR_WIDTH ||
        SetPointerShape->Height > RPI5VC4_CURSOR_HEIGHT ||
        SetPointerShape->Pitch < SetPointerShape->Width * sizeof(ULONG))
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(DeviceExtension->CursorVa,
                  RPI5VC4_CURSOR_WIDTH * RPI5VC4_CURSOR_HEIGHT * sizeof(ULONG));

    Source = SetPointerShape->pPixels;
    Destination = DeviceExtension->CursorVa;
    CopyBytes = SetPointerShape->Width * sizeof(ULONG);

    for (Row = 0; Row < SetPointerShape->Height; ++Row)
    {
        RtlCopyMemory(Destination + (Row * RPI5VC4_CURSOR_WIDTH * sizeof(ULONG)),
                      Source + (Row * SetPointerShape->Pitch),
                      CopyBytes);
    }

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif

    /*
     * The pixels were updated in place at the physical address the live
     * overlay plane already scans, so a same-size shape swap needs no
     * display-list touch at all. Only a size change updates the plane —
     * in place when possible; a full rebuild rewrites the live element
     * (context-word stomp) mid-frame and flickers the cursor.
     */
    if (DeviceExtension->CursorVisible &&
        DeviceExtension->CursorShapeValid &&
        (DeviceExtension->CursorWidth != SetPointerShape->Width ||
         DeviceExtension->CursorHeight != SetPointerShape->Height))
    {
        DeviceExtension->CursorWidth = SetPointerShape->Width;
        DeviceExtension->CursorHeight = SetPointerShape->Height;
        DeviceExtension->CursorHotX = (LONG)SetPointerShape->XHot;
        DeviceExtension->CursorHotY = (LONG)SetPointerShape->YHot;

        if (!Rpi5HvsMoveCursor(DeviceExtension))
            Rpi5HvsInstallScanout(DeviceExtension);
        return STATUS_SUCCESS;
    }

    DeviceExtension->CursorWidth = SetPointerShape->Width;
    DeviceExtension->CursorHeight = SetPointerShape->Height;
    DeviceExtension->CursorHotX = (LONG)SetPointerShape->XHot;
    DeviceExtension->CursorHotY = (LONG)SetPointerShape->YHot;

    if (DeviceExtension->CursorVisible && !DeviceExtension->CursorShapeValid)
    {
        DeviceExtension->CursorShapeValid = TRUE;
        Rpi5HvsInstallScanout(DeviceExtension);
        return STATUS_SUCCESS;
    }

    DeviceExtension->CursorShapeValid = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiSetPointerPosition(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    BOOLEAN WasVisible;

    if (DeviceExtension == NULL || SetPointerPosition == NULL)
        return STATUS_INVALID_PARAMETER;

    if (SetPointerPosition->VidPnSourceId != 0)
        return STATUS_INVALID_PARAMETER;

    if (DeviceExtension->CursorVa == NULL)
        return STATUS_NOT_SUPPORTED;

    WasVisible = DeviceExtension->CursorVisible;

    /* X/Y locate the hot spot; the HVS plane wants the top-left corner. */
    DeviceExtension->CursorX = SetPointerPosition->X - DeviceExtension->CursorHotX;
    DeviceExtension->CursorY = SetPointerPosition->Y - DeviceExtension->CursorHotY;
    DeviceExtension->CursorVisible = SetPointerPosition->Flags.Visible &&
                                     DeviceExtension->CursorShapeValid;

    if (DeviceExtension->CursorVisible)
    {
        if (!WasVisible || !Rpi5HvsMoveCursor(DeviceExtension))
            Rpi5HvsInstallScanout(DeviceExtension);
    }
    else if (WasVisible)
    {
        /* Rebuild the display list without the cursor overlay. */
        Rpi5HvsInstallScanout(DeviceExtension);
    }

    return STATUS_SUCCESS;
}
