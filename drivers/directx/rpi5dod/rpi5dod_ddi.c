/*
 * PROJECT:     ReactOS RPi5 Display-Only WDDM Miniport (rpi5dod)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DriverEntry and the WDDM display-only (DOD) DDI entry points.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * This miniport is registered with dxgkrnl via DxgkInitializeDisplayOnly
 * Driver, the same entry point kmdod uses.  It is intended for real
 * Raspberry Pi 5 hardware, where the display is driven by the VideoCore VII
 * HVS rather than a PCI VGA device.  See rpi5dod.h for the full design.
 */

#include "rpi5dod.h"
#include "rpi5dod_hvs.h"

#define NDEBUG
#include <debug.h>

/* Trace helper: always reaches the serial in the debug build. */
#define R5DOD_TRACE(fmt, ...) DbgPrint("RPI5DOD: " fmt, ##__VA_ARGS__)

/* ======================================================================
 * DriverEntry
 * ====================================================================== */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    KMDDOD_INITIALIZATION_DATA InitData;
    NTSTATUS Status;

    PAGED_CODE();

    R5DOD_TRACE("DriverEntry: RPi5 display-only WDDM miniport loading\n");

    RtlZeroMemory(&InitData, sizeof(InitData));

    InitData.Version = DXGKDDI_INTERFACE_VERSION;

    InitData.DxgkDdiAddDevice                       = Rpi5DodAddDevice;
    InitData.DxgkDdiStartDevice                     = Rpi5DodStartDevice;
    InitData.DxgkDdiStopDevice                      = Rpi5DodStopDevice;
    InitData.DxgkDdiResetDevice                     = Rpi5DodResetDevice;
    InitData.DxgkDdiRemoveDevice                    = Rpi5DodRemoveDevice;
    InitData.DxgkDdiDispatchIoRequest               = Rpi5DodDispatchIoRequest;
    InitData.DxgkDdiInterruptRoutine                = Rpi5DodInterruptRoutine;
    InitData.DxgkDdiDpcRoutine                      = Rpi5DodDpcRoutine;
    InitData.DxgkDdiQueryChildRelations             = Rpi5DodQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus                = Rpi5DodQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor           = Rpi5DodQueryDeviceDescriptor;
    InitData.DxgkDdiSetPowerState                   = Rpi5DodSetPowerState;
    InitData.DxgkDdiUnload                          = Rpi5DodUnload;
    InitData.DxgkDdiQueryAdapterInfo                = Rpi5DodQueryAdapterInfo;
    InitData.DxgkDdiSetPointerPosition              = (PDXGKDDI_SET_POINTER_POSITION)Rpi5DodSetPointerPosition;
    InitData.DxgkDdiSetPointerShape                 = (PDXGKDDI_SET_POINTER_SHAPE)Rpi5DodSetPointerShape;
    InitData.DxgkDdiIsSupportedVidPn                = Rpi5DodIsSupportedVidPn;
    InitData.DxgkDdiRecommendFunctionalVidPn        = (PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN)Rpi5DodRecommendFunctionalVidPn;
    InitData.DxgkDdiEnumVidPnCofuncModality         = (PDXGKDDI_ENUM_VIDPN_COFUNC_MODALITY)Rpi5DodEnumVidPnCofuncModality;
    InitData.DxgkDdiSetVidPnSourceVisibility        = (PDXGKDDI_SET_VIDPN_SOURCE_VISIBILITY)Rpi5DodSetVidPnSourceVisibility;
    InitData.DxgkDdiCommitVidPn                     = (PDXGKDDI_COMMIT_VIDPN)Rpi5DodCommitVidPn;
    InitData.DxgkDdiUpdateActiveVidPnPresentPath    = (PDXGKDDI_UPDATE_ACTIVE_VIDPN_PRESENT_PATH)Rpi5DodUpdateActiveVidPnPresentPath;
    InitData.DxgkDdiRecommendMonitorModes           = (PDXGKDDI_RECOMMEND_MONITORMODES)Rpi5DodRecommendMonitorModes;
    InitData.DxgkDdiPresentDisplayOnly              = (PVOID)Rpi5DodPresentDisplayOnly;
    InitData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership =
        Rpi5DodStopDeviceAndReleasePostDisplayOwnership;
    InitData.DxgkDdiSystemDisplayEnable             = Rpi5DodSystemDisplayEnable;
    InitData.DxgkDdiSystemDisplayWrite              = Rpi5DodSystemDisplayWrite;

    Status = DxgkInitializeDisplayOnlyDriver(DriverObject, RegistryPath, &InitData);
    if (!NT_SUCCESS(Status))
    {
        R5DOD_TRACE("DriverEntry: DxgkInitializeDisplayOnlyDriver failed 0x%08lX\n",
                    Status);
    }

    return Status;
}

/* ======================================================================
 * PnP DDIs
 * ====================================================================== */

VOID APIENTRY
Rpi5DodUnload(VOID)
{
    PAGED_CODE();
}

NTSTATUS APIENTRY
Rpi5DodAddDevice(
    _In_ DEVICE_OBJECT *PhysicalDeviceObject,
    _Outptr_ PVOID *DeviceContext)
{
    PRPI5DOD_DEVICE Device;

    PAGED_CODE();

    if (PhysicalDeviceObject == NULL || DeviceContext == NULL)
        return STATUS_INVALID_PARAMETER;

    *DeviceContext = NULL;

    Device = (PRPI5DOD_DEVICE)ExAllocatePoolZero(NonPagedPool,
                                                 sizeof(RPI5DOD_DEVICE),
                                                 RPI5DOD_TAG);
    if (Device == NULL)
        return STATUS_NO_MEMORY;

    Device->PhysicalDeviceObject = PhysicalDeviceObject;
    Device->PowerState = PowerDeviceD0;

    *DeviceContext = Device;

    R5DOD_TRACE("AddDevice: ctx=%p PDO=%p\n", Device, PhysicalDeviceObject);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodRemoveDevice(
    _In_ VOID *DeviceContext)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;

    PAGED_CODE();

    if (Device != NULL)
        ExFreePoolWithTag(Device, RPI5DOD_TAG);

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodStartDevice(
    _In_ VOID *DeviceContext,
    _In_ DXGK_START_INFO *DxgkStartInfo,
    _In_ DXGKRNL_INTERFACE *DxgkInterface,
    _Out_ ULONG *NumberOfViews,
    _Out_ ULONG *NumberOfChildren)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;
    PRPI5DOD_MODE   Mode;
    NTSTATUS        Status;
    ULONG           FbLength;

    PAGED_CODE();

    if (Device == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
        NumberOfViews == NULL || NumberOfChildren == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Device->DxgkInterface = *DxgkInterface;
    Mode = &Device->Mode[0];
    RtlZeroMemory(Mode, sizeof(*Mode));

    /* Fetch device info (resources etc.) — not strictly required for the
     * firmware-GOP path but mirrors the BDD model and validates the device. */
    Status = Device->DxgkInterface.DxgkCbGetDeviceInformation(
                 Device->DxgkInterface.DeviceHandle, &Device->DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        R5DOD_TRACE("StartDevice: DxgkCbGetDeviceInformation failed 0x%08lX\n",
                    Status);
        /* Continue: the GOP framebuffer comes from AcquirePostDisplayOwnership. */
    }

    /*
     * Acquire the POST (firmware GOP) framebuffer.  On the Pi 5 the UEFI
     * firmware programs the HVS to scan out this framebuffer, so blitting
     * complete frames into it is sufficient for a tear-free display.
     *
     * dxgkrnl's DxgkCbAcquirePostDisplayOwnership fills DispInfo with the
     * GOP geometry (it reads the ReactOS BPP-in-PixelFormat convention).
     */
    Mode->DispInfo.TargetId = D3DDDI_ID_UNINITIALIZED;
    Status = Device->DxgkInterface.DxgkCbAcquirePostDisplayOwnership(
                 Device->DxgkInterface.DeviceHandle, &Mode->DispInfo);
    if (!NT_SUCCESS(Status) ||
        Mode->DispInfo.Width == 0 ||
        Mode->DispInfo.Height == 0 ||
        Mode->DispInfo.PhysicAddress.QuadPart == 0)
    {
        R5DOD_TRACE("StartDevice: no POST/GOP framebuffer (status=0x%08lX %lux%lu PA=0x%I64X)\n",
                    Status, Mode->DispInfo.Width, Mode->DispInfo.Height,
                    Mode->DispInfo.PhysicAddress.QuadPart);
        /*
         * On real hardware without a firmware GOP the HVS scanout must be
         * brought up directly (see rpi5dod_hvs.c).  That path is not wired
         * for first bring-up; fail cleanly so dxgkrnl tears the adapter down
         * instead of presenting to nothing.
         */
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    if (Mode->DispInfo.ColorFormat == D3DDDIFMT_UNKNOWN)
        Mode->DispInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;

    Mode->SrcWidth  = Mode->DispInfo.Width;
    Mode->SrcHeight = Mode->DispInfo.Height;

    /* Map the scanout framebuffer write-combined for CPU blt access. */
    FbLength = Mode->DispInfo.Pitch * Mode->DispInfo.Height;
    Mode->FrameBufferLength = FbLength;
    Mode->FrameBufferVa = MmMapIoSpace(Mode->DispInfo.PhysicAddress,
                                       FbLength, MmWriteCombined);
    if (Mode->FrameBufferVa == NULL)
    {
        Mode->FrameBufferVa = MmMapIoSpace(Mode->DispInfo.PhysicAddress,
                                           FbLength, MmNonCached);
    }
    if (Mode->FrameBufferVa == NULL)
    {
        R5DOD_TRACE("StartDevice: MmMapIoSpace failed for PA=0x%I64X len=0x%lX\n",
                    Mode->DispInfo.PhysicAddress.QuadPart, FbLength);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Mode->FrameBufferActive = TRUE;
    Mode->SourceVisible = TRUE;
    Device->ChildConnected = TRUE;
    Device->Started = TRUE;

    *NumberOfViews    = RPI5DOD_MAX_VIEWS;
    *NumberOfChildren = RPI5DOD_MAX_CHILDREN;

    /*
     * Make the live HVS scanout plane opaque.  The firmware leaves the HVS
     * plane honouring the per-pixel source alpha, so GDI/DWM output (written as
     * 0x00RRGGBB, alpha byte 0) would composite to BLACK.  This is the SAME
     * GOP framebuffer the GDI desktop is painted into, so making the plane
     * ignore the source alpha shows the real desktop instead of black.  Safe
     * no-op if the HVS head does not match the GOP framebuffer.
     */
    Rpi5DodHvsMakeScanoutOpaque(Mode->DispInfo.PhysicAddress.QuadPart,
                                Mode->DispInfo.Width,
                                Mode->DispInfo.Height,
                                Mode->DispInfo.Pitch);

    R5DOD_TRACE("StartDevice: GOP/HVS scanout %lux%lu pitch=%lu PA=0x%I64X VA=%p fmt=%d\n",
                Mode->DispInfo.Width, Mode->DispInfo.Height, Mode->DispInfo.Pitch,
                Mode->DispInfo.PhysicAddress.QuadPart, Mode->FrameBufferVa,
                (int)Mode->DispInfo.ColorFormat);

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodStopDevice(
    _In_ VOID *DeviceContext)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;
    PRPI5DOD_MODE   Mode;

    PAGED_CODE();

    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;

    Mode = &Device->Mode[0];
    if (Mode->FrameBufferVa != NULL)
    {
        MmUnmapIoSpace(Mode->FrameBufferVa, Mode->FrameBufferLength);
        Mode->FrameBufferVa = NULL;
        Mode->FrameBufferActive = FALSE;
    }
    Device->Started = FALSE;

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodDispatchIoRequest(
    _In_ VOID *DeviceContext,
    _In_ ULONG VidPnSourceId,
    _In_ VIDEO_REQUEST_PACKET *VideoRequestPacket)
{
    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(VideoRequestPacket);
    /* DODs do not service legacy VRPs. */
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY
Rpi5DodSetPowerState(
    _In_ VOID *DeviceContext,
    _In_ ULONG HardwareUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION ActionType)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;

    UNREFERENCED_PARAMETER(HardwareUid);
    UNREFERENCED_PARAMETER(ActionType);

    if (Device != NULL)
        Device->PowerState = DevicePowerState;

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodQueryChildRelations(
    _In_ VOID *DeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR *ChildRelations,
    _In_ ULONG ChildRelationsSize)
{
    ULONG Count;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceContext);

    if (ChildRelations == NULL)
        return STATUS_INVALID_PARAMETER;

    Count = ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR);
    /* Last entry is the array terminator; we expose a single HDMI target. */
    if (Count < (RPI5DOD_MAX_CHILDREN + 1))
        return STATUS_INVALID_PARAMETER;

    ChildRelations[0].ChildDeviceType                       = TypeVideoOutput;
    ChildRelations[0].ChildCapabilities.HpdAwareness        = HpdAwarenessInterruptible;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology =
        D3DKMDT_VOT_HDMI;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness =
        D3DKMDT_MOA_NONE;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
    ChildRelations[0].AcpiUid                               = 0;
    ChildRelations[0].ChildUid                              = 0;

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodQueryChildStatus(
    _In_ VOID *DeviceContext,
    _Inout_ DXGK_CHILD_STATUS *ChildStatus,
    _In_ BOOLEAN NonDestructiveOnly)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    if (Device == NULL || ChildStatus == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (ChildStatus->Type)
    {
        case StatusConnection:
            /* Firmware-GOP path: HDMI is assumed connected (the firmware
             * already lit it).  Real HDMI hotplug can refine this. */
            ChildStatus->HotPlug.Connected = Device->ChildConnected;
            return STATUS_SUCCESS;

        case StatusRotation:
            ChildStatus->Rotation.Angle = 0;
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS APIENTRY
Rpi5DodQueryDeviceDescriptor(
    _In_ VOID *DeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR *DeviceDescriptor)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(DeviceDescriptor);

    /* No EDID retrieval for the firmware-GOP path; dxgkrnl falls back to the
     * POST display geometry.  Real EDID-over-DDC can be added later. */
    return STATUS_MONITOR_NO_DESCRIPTOR;
}

BOOLEAN APIENTRY
Rpi5DodInterruptRoutine(
    _In_ VOID *DeviceContext,
    _In_ ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);
    /* No hardware interrupt source wired for the firmware-GOP path. */
    return FALSE;
}

VOID APIENTRY
Rpi5DodDpcRoutine(
    _In_ VOID *DeviceContext)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;

    if (Device != NULL)
        Device->DxgkInterface.DxgkCbNotifyDpc(Device->DxgkInterface.DeviceHandle);
}

VOID APIENTRY
Rpi5DodResetDevice(
    _In_ VOID *DeviceContext)
{
    /* Nothing to reset for the firmware-GOP path. */
    UNREFERENCED_PARAMETER(DeviceContext);
}

/* ======================================================================
 * Pointer DDIs — software cursor only (no hardware overlay on the HVS path)
 * ====================================================================== */

NTSTATUS APIENTRY
Rpi5DodSetPointerPosition(
    _In_ VOID *DeviceContext,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition)
{
    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(SetPointerPosition);
    /* No hardware pointer: report success so the OS uses a software cursor. */
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodSetPointerShape(
    _In_ VOID *DeviceContext,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape)
{
    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(SetPointerShape);
    return STATUS_NOT_SUPPORTED;
}

/* ======================================================================
 * Present (the tear-free flip): blt the complete frame to the scanout
 * ====================================================================== */

NTSTATUS APIENTRY
Rpi5DodPresentDisplayOnly(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)hAdapter;
    PRPI5DOD_MODE   Mode;

    if (Device == NULL || PresentDisplayOnly == NULL)
        return STATUS_INVALID_PARAMETER;

    if (PresentDisplayOnly->VidPnSourceId >= RPI5DOD_MAX_VIEWS)
        return STATUS_INVALID_PARAMETER;

    Mode = &Device->Mode[PresentDisplayOnly->VidPnSourceId];

    if (PresentDisplayOnly->BytesPerPixel != RPI5DOD_SOURCE_BYTES)
        return STATUS_INVALID_PARAMETER;

    /* Nothing to scan out / not visible: succeed without touching memory. */
    if (!Mode->FrameBufferActive || Mode->FrameBufferVa == NULL ||
        !Mode->SourceVisible || Device->PowerState != PowerDeviceD0)
    {
        return STATUS_SUCCESS;
    }

    if (PresentDisplayOnly->pSource == NULL)
        return STATUS_SUCCESS;

    /*
     * Blt the (already-complete) source frame into the scanout framebuffer.
     * dxgkrnl hands us its shadow framebuffer as pSource, which it has just
     * filled from DWM's composition allocation — so this puts a COMPLETE
     * composed frame on screen.  Because the HVS scans the framebuffer out
     * continuously and we copy whole rects, the result is tear-free.
     */
    Rpi5DodBltSourceToScanout(Mode,
                              PresentDisplayOnly->pSource,
                              (ULONG)PresentDisplayOnly->Pitch,
                              PresentDisplayOnly->NumDirtyRects,
                              PresentDisplayOnly->pDirtyRect);

    return STATUS_SUCCESS;
}

/* ======================================================================
 * Post-display ownership release + bugcheck (system display) DDIs
 * ====================================================================== */

NTSTATUS APIENTRY
Rpi5DodStopDeviceAndReleasePostDisplayOwnership(
    _In_ VOID *DeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION *DisplayInfo)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;
    PRPI5DOD_MODE   Mode;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(TargetId);

    if (Device == NULL || DisplayInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    Mode = &Device->Mode[0];
    *DisplayInfo = Mode->DispInfo;

    if (Mode->FrameBufferVa != NULL)
    {
        MmUnmapIoSpace(Mode->FrameBufferVa, Mode->FrameBufferLength);
        Mode->FrameBufferVa = NULL;
        Mode->FrameBufferActive = FALSE;
    }
    Device->Started = FALSE;

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodSystemDisplayEnable(
    _In_ VOID *DeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT *Width,
    _Out_ UINT *Height,
    _Out_ D3DDDIFORMAT *ColorFormat)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;
    PRPI5DOD_MODE   Mode;

    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);

    if (Device == NULL || Width == NULL || Height == NULL || ColorFormat == NULL)
        return STATUS_INVALID_PARAMETER;

    Mode = &Device->Mode[0];
    if (!Mode->FrameBufferActive || Mode->FrameBufferVa == NULL)
        return STATUS_UNSUCCESSFUL;

    *Width       = Mode->DispInfo.Width;
    *Height      = Mode->DispInfo.Height;
    *ColorFormat = Mode->DispInfo.ColorFormat;

    return STATUS_SUCCESS;
}

VOID APIENTRY
Rpi5DodSystemDisplayWrite(
    _In_ VOID *DeviceContext,
    _In_ VOID *Source,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ UINT PositionX,
    _In_ UINT PositionY)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;
    PRPI5DOD_MODE   Mode;
    PUCHAR          Dst;
    PUCHAR          Src;
    UINT            Row;
    UINT            CopyBytes;

    if (Device == NULL || Source == NULL)
        return;

    Mode = &Device->Mode[0];
    if (Mode->FrameBufferVa == NULL)
        return;

    /* Bugcheck-time blocking write of a sub-rect into the scanout FB. */
    CopyBytes = SourceWidth * RPI5DOD_SOURCE_BYTES;
    for (Row = 0; Row < SourceHeight; Row++)
    {
        Dst = (PUCHAR)Mode->FrameBufferVa +
              (SIZE_T)(PositionY + Row) * Mode->DispInfo.Pitch +
              (SIZE_T)PositionX * RPI5DOD_SOURCE_BYTES;
        Src = (PUCHAR)Source + (SIZE_T)Row * SourceStride;
        RtlCopyMemory(Dst, Src, CopyBytes);
    }
}

/* ======================================================================
 * QueryAdapterInfo — report DOD capabilities
 * ====================================================================== */

NTSTATUS APIENTRY
Rpi5DodQueryAdapterInfo(
    _In_ VOID *DeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceContext);

    if (QueryAdapterInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (QueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
        {
            DXGK_DRIVERCAPS *Caps;

            if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
                return STATUS_BUFFER_TOO_SMALL;

            Caps = (DXGK_DRIVERCAPS *)QueryAdapterInfo->pOutputData;
            RtlZeroMemory(Caps, sizeof(DXGK_DRIVERCAPS));

            Caps->HighestAcceptableAddress.QuadPart = (ULONGLONG)-1;
            Caps->SupportNonVGA                = TRUE;
            Caps->SupportSmoothRotation        = FALSE;

            return STATUS_SUCCESS;
        }

        default:
            /* DODs only need DRIVERCAPS; everything else is unsupported. */
            return STATUS_NOT_SUPPORTED;
    }
}
