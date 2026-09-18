/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM display-only miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidPN DDIs.  The scanout mode is fixed by the firmware
 *              (the HVS scans the GOP framebuffer at the raster the firmware
 *              programmed), so mode enumeration collapses every unpinned
 *              source/target mode set to that single native mode.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5vc4.h"
#include "rpi5vc4_hvs.h"
#include "rpi5vc4_crtc.h"

#define NDEBUG
#include <reactos/debug.h>

/* ========================================================================
 * Native (firmware) mode helpers
 * ====================================================================== */

static VOID
Rpi5Vc4FillSourceMode(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Inout_ D3DKMDT_VIDPN_SOURCE_MODE *SourceMode)
{
    /* Keep the Id assigned by CreateNewModeInfo. */
    SourceMode->Type = D3DKMDT_RMT_GRAPHICS;
    SourceMode->Format.Graphics.PrimSurfSize.cx = DeviceExtension->ScreenWidth;
    SourceMode->Format.Graphics.PrimSurfSize.cy = DeviceExtension->ScreenHeight;
    SourceMode->Format.Graphics.VisibleRegionSize.cx = DeviceExtension->ScreenWidth;
    SourceMode->Format.Graphics.VisibleRegionSize.cy = DeviceExtension->ScreenHeight;
    SourceMode->Format.Graphics.Stride = DeviceExtension->BytesPerScanLine;
    SourceMode->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
    SourceMode->Format.Graphics.ColorBasis = D3DKMDT_CB_SRGB;
    SourceMode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
}

static VOID
Rpi5Vc4FillVideoSignalInfo(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Inout_ D3DKMDT_VIDEO_SIGNAL_INFO *SignalInfo)
{
    SignalInfo->VideoStandard = D3DKMDT_VSS_OTHER;
    SignalInfo->TotalSize.cx = DeviceExtension->TargetHTotal;
    SignalInfo->TotalSize.cy = DeviceExtension->TargetVTotal;
    SignalInfo->ActiveSize.cx = DeviceExtension->ScanoutWidth;
    SignalInfo->ActiveSize.cy = DeviceExtension->ScanoutHeight;
    SignalInfo->VSyncFreq = DeviceExtension->TargetVSyncFreq;
    SignalInfo->HSyncFreq = DeviceExtension->TargetHSyncFreq;
    SignalInfo->PixelRate = DeviceExtension->TargetPixelRate;
    SignalInfo->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
}

/* Replace an unpinned source mode set with one holding only the native mode. */
static NTSTATUS
Rpi5Vc4UpdateSourceModeSet(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    D3DKMDT_VIDPN_SOURCE_MODE *NewMode = NULL;
    NTSTATUS Status;

    /* Leave the set alone if a mode is already pinned on it. */
    Status = VidPnInterface->pfnAcquireSourceModeSet(hVidPn, SourceId,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet, &PinnedMode);
    if (NT_SUCCESS(Status) && PinnedMode != NULL)
    {
        ModeSetInterface->pfnReleaseModeInfo(hModeSet, PinnedMode);
        VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
        return STATUS_SUCCESS;
    }
    VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);

    Status = VidPnInterface->pfnCreateNewSourceModeSet(hVidPn, SourceId,
                                                       &hModeSet,
                                                       &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ModeSetInterface->pfnCreateNewModeInfo(hModeSet, &NewMode);
    if (!NT_SUCCESS(Status))
    {
        VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
        return Status;
    }

    Rpi5Vc4FillSourceMode(DeviceExtension, NewMode);

    Status = ModeSetInterface->pfnAddMode(hModeSet, NewMode);
    if (!NT_SUCCESS(Status))
    {
        ModeSetInterface->pfnReleaseModeInfo(hModeSet, NewMode);
        VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
        return Status;
    }

    Status = VidPnInterface->pfnAssignSourceModeSet(hVidPn, SourceId, hModeSet);
    if (!NT_SUCCESS(Status))
        VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);

    return Status;
}

/* Replace an unpinned target mode set with one holding only the native mode. */
static NTSTATUS
Rpi5Vc4UpdateTargetModeSet(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *PinnedMode = NULL;
    D3DKMDT_VIDPN_TARGET_MODE *NewMode = NULL;
    NTSTATUS Status;

    Status = VidPnInterface->pfnAcquireTargetModeSet(hVidPn, TargetId,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet, &PinnedMode);
    if (NT_SUCCESS(Status) && PinnedMode != NULL)
    {
        ModeSetInterface->pfnReleaseModeInfo(hModeSet, PinnedMode);
        VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
        return STATUS_SUCCESS;
    }
    VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);

    Status = VidPnInterface->pfnCreateNewTargetModeSet(hVidPn, TargetId,
                                                       &hModeSet,
                                                       &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ModeSetInterface->pfnCreateNewModeInfo(hModeSet, &NewMode);
    if (!NT_SUCCESS(Status))
    {
        VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
        return Status;
    }

    Rpi5Vc4FillVideoSignalInfo(DeviceExtension, &NewMode->VideoSignalInfo);
    NewMode->Preference = D3DKMDT_MP_PREFERRED;

    Status = ModeSetInterface->pfnAddMode(hModeSet, NewMode);
    if (!NT_SUCCESS(Status))
    {
        ModeSetInterface->pfnReleaseModeInfo(hModeSet, NewMode);
        VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
        return Status;
    }

    Status = VidPnInterface->pfnAssignTargetModeSet(hVidPn, TargetId, hModeSet);
    if (!NT_SUCCESS(Status))
        VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);

    return Status;
}

/* ========================================================================
 * VidPN DDIs
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiIsSupportedVidPn(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_ISSUPPORTEDVIDPN IsSupportedVidPn)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    CONST DXGK_VIDPN_INTERFACE *VidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    SIZE_T NumPaths = 0;
    BOOLEAN PathSupported;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    if (DeviceExtension == NULL || IsSupportedVidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    IsSupportedVidPn->IsVidPnSupported = FALSE;

    if (IsSupportedVidPn->hDesiredVidPn == 0)
    {
        /* A null VidPN (display off) is always supported. */
        IsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    if (DeviceExtension->DxgkInterface.DxgkCbQueryVidPnInterface == NULL)
    {
        IsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    Status = DeviceExtension->DxgkInterface.DxgkCbQueryVidPnInterface(
                 IsSupportedVidPn->hDesiredVidPn,
                 DXGK_VIDPN_INTERFACE_VERSION_V1,
                 &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = VidPnInterface->pfnGetTopology(IsSupportedVidPn->hDesiredVidPn,
                                            &hTopology,
                                            &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = TopologyInterface->pfnGetNumPaths(hTopology, &NumPaths);
    if (!NT_SUCCESS(Status))
        return Status;

    if (NumPaths == 0)
    {
        IsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }
    if (NumPaths != 1)
        return STATUS_SUCCESS;

    Status = TopologyInterface->pfnAcquireFirstPathInfo(hTopology, &Path);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Path == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;

    PathSupported =
        Path->VidPnSourceId == 0 &&
        Path->VidPnTargetId == 0 &&
        (Path->ContentTransformation.Scaling == D3DKMDT_VPPS_IDENTITY ||
         Path->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED ||
         Path->ContentTransformation.Scaling == D3DKMDT_VPPS_NOTSPECIFIED) &&
        (Path->ContentTransformation.Rotation == DeviceExtension->PathRotation ||
         Path->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED ||
         Path->ContentTransformation.Rotation == D3DKMDT_VPPR_NOTSPECIFIED);

    ReleaseStatus = TopologyInterface->pfnReleasePathInfo(hTopology, Path);
    if (!NT_SUCCESS(ReleaseStatus))
        return ReleaseStatus;

    IsSupportedVidPn->IsVidPnSupported = PathSupported;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiRecommendFunctionalVidPn(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *RecommendFunctionalVidPn)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(RecommendFunctionalVidPn);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiEnumVidPnCofuncModality(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *EnumCofuncModality)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    CONST DXGK_VIDPN_INTERFACE *VidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *NextPath = NULL;
    NTSTATUS Status;

    if (DeviceExtension == NULL || EnumCofuncModality == NULL)
        return STATUS_INVALID_PARAMETER;

    if (DeviceExtension->DxgkInterface.DxgkCbQueryVidPnInterface == NULL)
        return STATUS_SUCCESS;

    Status = DeviceExtension->DxgkInterface.DxgkCbQueryVidPnInterface(
                 EnumCofuncModality->hConstrainingVidPn,
                 DXGK_VIDPN_INTERFACE_VERSION_V1,
                 &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = VidPnInterface->pfnGetTopology(EnumCofuncModality->hConstrainingVidPn,
                                            &hTopology,
                                            &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = TopologyInterface->pfnAcquireFirstPathInfo(hTopology, &Path);
    if (!NT_SUCCESS(Status) || Path == NULL)
    {
        /* Empty topology: nothing to constrain. */
        return STATUS_SUCCESS;
    }

    while (Path != NULL)
    {
        D3DKMDT_VIDPN_PRESENT_PATH UpdatedPath;
        BOOLEAN UpdateScaling;
        BOOLEAN UpdateRotation;
        BOOLEAN PivotMatchesPath;

        /* Source mode set (skip when the source is the enumeration pivot). */
        if (!(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE &&
              EnumCofuncModality->EnumPivot.VidPnSourceId == Path->VidPnSourceId))
        {
            Status = Rpi5Vc4UpdateSourceModeSet(DeviceExtension,
                                                EnumCofuncModality->hConstrainingVidPn,
                                                VidPnInterface,
                                                Path->VidPnSourceId);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("RPI5VC4: source mode set update failed 0x%08lx\n", Status);
                TopologyInterface->pfnReleasePathInfo(hTopology, Path);
                return Status;
            }
        }

        /* Target mode set (skip when the target is the enumeration pivot). */
        if (!(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET &&
              EnumCofuncModality->EnumPivot.VidPnTargetId == Path->VidPnTargetId))
        {
            Status = Rpi5Vc4UpdateTargetModeSet(DeviceExtension,
                                                EnumCofuncModality->hConstrainingVidPn,
                                                VidPnInterface,
                                                Path->VidPnTargetId);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("RPI5VC4: target mode set update failed 0x%08lx\n", Status);
                TopologyInterface->pfnReleasePathInfo(hTopology, Path);
                return Status;
            }
        }

        PivotMatchesPath =
            EnumCofuncModality->EnumPivot.VidPnSourceId == Path->VidPnSourceId &&
            EnumCofuncModality->EnumPivot.VidPnTargetId == Path->VidPnTargetId;
        UpdateScaling =
            Path->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY &&
            Path->ContentTransformation.Scaling != D3DKMDT_VPPS_CENTERED &&
            Path->ContentTransformation.Scaling != D3DKMDT_VPPS_STRETCHED &&
            Path->ContentTransformation.Scaling !=
                D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX &&
            Path->ContentTransformation.Scaling != D3DKMDT_VPPS_CUSTOM &&
            !(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_SCALING &&
              PivotMatchesPath);
        UpdateRotation =
            Path->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY &&
            Path->ContentTransformation.Rotation != D3DKMDT_VPPR_ROTATE90 &&
            Path->ContentTransformation.Rotation != D3DKMDT_VPPR_ROTATE180 &&
            Path->ContentTransformation.Rotation != D3DKMDT_VPPR_ROTATE270 &&
            !(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_ROTATION &&
              PivotMatchesPath);

        if (UpdateScaling || UpdateRotation)
        {
            UpdatedPath = *Path;
            if (UpdateScaling)
            {
                RtlZeroMemory(
                    &UpdatedPath.ContentTransformation.ScalingSupport,
                    sizeof(UpdatedPath.ContentTransformation.ScalingSupport));
                UpdatedPath.ContentTransformation.ScalingSupport.Identity = 1;
            }
            if (UpdateRotation)
            {
                RtlZeroMemory(
                    &UpdatedPath.ContentTransformation.RotationSupport,
                    sizeof(UpdatedPath.ContentTransformation.RotationSupport));
                if (DeviceExtension->PathRotation == D3DKMDT_VPPR_ROTATE90)
                {
                    UpdatedPath.ContentTransformation.RotationSupport.Rotate90 = 1;
                }
                else
                {
                    UpdatedPath.ContentTransformation.RotationSupport.Identity = 1;
                }
            }

            Status = TopologyInterface->pfnUpdatePathSupportInfo(
                         hTopology,
                         &UpdatedPath);
            if (!NT_SUCCESS(Status))
            {
                TopologyInterface->pfnReleasePathInfo(hTopology, Path);
                return Status;
            }
        }

        NextPath = NULL;
        Status = TopologyInterface->pfnAcquireNextPathInfo(hTopology, Path, &NextPath);
        TopologyInterface->pfnReleasePathInfo(hTopology, Path);
        if (!NT_SUCCESS(Status))
            break;
        Path = NextPath;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiSetVidPnSourceAddress(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *SetVidPnSourceAddress)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    PRPI5VC4_ALLOCATION Allocation;
    PHYSICAL_ADDRESS Target;
    ULONGLONG SlabBase;
    ULONGLONG SlabEnd;
    ULONGLONG Offset;

    if (DeviceExtension == NULL || SetVidPnSourceAddress == NULL)
        return STATUS_INVALID_PARAMETER;

    if (SetVidPnSourceAddress->VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    if (SetVidPnSourceAddress->PrimarySegment != 0 && SetVidPnSourceAddress->PrimarySegment != RPI5VC4_SEGMENT_ID)
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;

    Target = SetVidPnSourceAddress->PrimaryAddress;

    /* RP1 DSI consumes a fixed portrait firmware framebuffer rather than a
     * page-flippable HVS plane.  A full-WDDM flip reaches this DDI only after
     * its GPU fence retires, so publish the completed linear primary through
     * the fixed scanout transport instead of reporting a no-op success. */
    if (Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension))
    {
        if (Target.QuadPart == 0)
        {
            DeviceExtension->FrameBufferPhysical =
                DeviceExtension->FirmwareFrameBufferPhysical;
            return STATUS_SUCCESS;
        }

        if (SetVidPnSourceAddress->PrimarySegment !=
                RPI5VC4_LOCAL_SEGMENT_ID ||
            SetVidPnSourceAddress->hAllocation == NULL ||
            DeviceExtension->VramVa == NULL)
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

        Allocation = (PRPI5VC4_ALLOCATION)
            SetVidPnSourceAddress->hAllocation;
        if (Allocation->Magic != RPI5VC4_ALLOCATION_MAGIC ||
            !Allocation->Primary ||
            Allocation->ResourceLayout != RPI5VC4_RESOURCE_LAYOUT_LINEAR ||
            Allocation->ResourceFormat !=
                RPI5VC4_RESOURCE_DXGI_FORMAT_B8G8R8A8_UNORM ||
            Allocation->Width != DeviceExtension->ScreenWidth ||
            Allocation->Height != DeviceExtension->ScreenHeight ||
            Allocation->Pitch <
                DeviceExtension->ScreenWidth * sizeof(ULONG))
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

        if (DeviceExtension->VramPhysical.QuadPart < 0 ||
            DeviceExtension->VramSize == 0)
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

        SlabBase = (ULONGLONG)DeviceExtension->VramPhysical.QuadPart;
        if (SlabBase > MAXULONGLONG - DeviceExtension->VramSize)
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        SlabEnd = SlabBase + DeviceExtension->VramSize;
        if ((ULONGLONG)Target.QuadPart < SlabBase ||
            (ULONGLONG)Target.QuadPart >= SlabEnd)
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

        Offset = (ULONGLONG)Target.QuadPart - SlabBase;
        if (Allocation->Size > DeviceExtension->VramSize - Offset)
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;

        DeviceExtension->FrameBufferPhysical =
            DeviceExtension->FirmwareFrameBufferPhysical;
        return Rpi5Vc4PresentFixedFirmwarePrimary(
                   DeviceExtension,
                   (const UCHAR *)DeviceExtension->VramVa + Offset,
                   Allocation->Size,
                   Allocation->Pitch);
    }

    /* A null address parks the scanout back on the firmware framebuffer. */
    if (Target.QuadPart == 0)
        Target = DeviceExtension->FirmwareFrameBufferPhysical;

    /*
     * The HVS scans physical memory directly; only surfaces in the VRAM
     * slab or the firmware framebuffer are reachable scanout targets.
     * The whole visible raster must fit below the target's end.
     */
    if (DeviceExtension->VramPhysical.QuadPart < 0 ||
        DeviceExtension->VramSize == 0)
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }

    SlabBase = (ULONGLONG)DeviceExtension->VramPhysical.QuadPart;
    if (SlabBase > MAXULONGLONG - DeviceExtension->VramSize)
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    SlabEnd = SlabBase + DeviceExtension->VramSize;

    if (Target.QuadPart != DeviceExtension->FirmwareFrameBufferPhysical.QuadPart)
    {
        ULONGLONG RasterBytes = (ULONGLONG)DeviceExtension->BytesPerScanLine *
                                DeviceExtension->ScreenHeight;

        if (DeviceExtension->VramVa == NULL ||
            (ULONGLONG)Target.QuadPart < SlabBase ||
            (ULONGLONG)Target.QuadPart >= SlabEnd ||
            RasterBytes > SlabEnd - (ULONGLONG)Target.QuadPart)
        {
            DPRINT1("RPI5VC4: SetVidPnSourceAddress: 0x%I64x outside the "
                    "VRAM slab\n", Target.QuadPart);
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }
    }

    if (!Rpi5HvsFlipScanout(DeviceExtension, Target))
    {
        /* Latched-off flip path: fail fast, no reinstall churn. */
        if (DeviceExtension->HvsFlipBroken)
            return STATUS_UNSUCCESSFUL;

        /*
         * The live display list no longer matches (e.g. the firmware plane
         * was rebuilt); reinstall our list at the current address, then
         * retry the flip once.
         */
        Rpi5HvsInstallScanout(DeviceExtension);
        if (!Rpi5HvsFlipScanout(DeviceExtension, Target))
        {
            DPRINT1("RPI5VC4: HVS flip to 0x%I64x failed\n", Target.QuadPart);
            return STATUS_UNSUCCESSFUL;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiSetVidPnSourceVisibility(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL || SetVidPnSourceVisibility == NULL)
        return STATUS_INVALID_PARAMETER;

    if (SetVidPnSourceVisibility->VidPnSourceId != 0 &&
        SetVidPnSourceVisibility->VidPnSourceId != D3DDDI_ID_ALL)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    if (Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension))
    {
        ExAcquireFastMutex(&DeviceExtension->HvsMutex);
        Rpi5Vc4PointerRestore(DeviceExtension->SoftwarePointer);
    }

    DeviceExtension->SourceVisible = SetVidPnSourceVisibility->Visible;

    if (SetVidPnSourceVisibility->Visible)
    {
        if (!Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension))
            Rpi5HvsInstallScanout(DeviceExtension);
    }
    else
    {
        /* No plane on/off control is implemented; blank to black instead. */
        PVOID ScanoutVa = Rpi5Vc4CurrentScanoutVa(DeviceExtension);

        if (ScanoutVa != NULL)
        {
            RtlZeroMemory(ScanoutVa,
                          Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension) ?
                              (SIZE_T)DeviceExtension->ScanoutPitch *
                                  DeviceExtension->ScanoutHeight :
                              (SIZE_T)DeviceExtension->BytesPerScanLine *
                                  DeviceExtension->ScreenHeight);
#if defined(_M_ARM64)
            __dsb(_ARM64_BARRIER_SY);
#endif
        }
    }

    if (Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension))
    {
        if (DeviceExtension->SoftwarePointer != NULL &&
            DeviceExtension->SourceVisible && DeviceExtension->CursorVisible)
        {
            Rpi5Vc4PointerDraw(DeviceExtension->SoftwarePointer);
#if defined(_M_ARM64)
            __dsb(_ARM64_BARRIER_SY);
#endif
        }
        ExReleaseFastMutex(&DeviceExtension->HvsMutex);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiCommitVidPn(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN *CommitVidPn)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    CONST DXGK_VIDPN_INTERFACE *VidPnInterface = NULL;
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    NTSTATUS Status;

    if (DeviceExtension == NULL || CommitVidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (CommitVidPn->AffectedVidPnSourceId != 0 &&
        CommitVidPn->AffectedVidPnSourceId != D3DDDI_ID_ALL)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    /*
     * Sanity-check the pinned source mode against the fixed firmware raster.
     * Only the native mode is ever advertised, so a mismatch means the OS
     * pinned one of its fallback modes; the scanout cannot follow it.
     */
    if (DeviceExtension->DxgkInterface.DxgkCbQueryVidPnInterface != NULL &&
        CommitVidPn->hFunctionalVidPn != 0)
    {
        Status = DeviceExtension->DxgkInterface.DxgkCbQueryVidPnInterface(
                     CommitVidPn->hFunctionalVidPn,
                     DXGK_VIDPN_INTERFACE_VERSION_V1,
                     &VidPnInterface);
        if (NT_SUCCESS(Status))
        {
            Status = VidPnInterface->pfnAcquireSourceModeSet(
                         CommitVidPn->hFunctionalVidPn, 0,
                         &hModeSet, &ModeSetInterface);
            if (NT_SUCCESS(Status))
            {
                if (NT_SUCCESS(ModeSetInterface->pfnAcquirePinnedModeInfo(
                                   hModeSet, &PinnedMode)) &&
                    PinnedMode != NULL)
                {
                    if ((ULONG)PinnedMode->Format.Graphics.PrimSurfSize.cx !=
                            DeviceExtension->ScreenWidth ||
                        (ULONG)PinnedMode->Format.Graphics.PrimSurfSize.cy !=
                            DeviceExtension->ScreenHeight)
                    {
                        DPRINT1("RPI5VC4: CommitVidPn: pinned %ldx%ld != native "
                                "%lux%lu (fixed firmware mode keeps scanning)\n",
                                PinnedMode->Format.Graphics.PrimSurfSize.cx,
                                PinnedMode->Format.Graphics.PrimSurfSize.cy,
                                DeviceExtension->ScreenWidth,
                                DeviceExtension->ScreenHeight);
                    }
                    ModeSetInterface->pfnReleaseModeInfo(hModeSet, PinnedMode);
                }
                VidPnInterface->pfnReleaseSourceModeSet(
                    CommitVidPn->hFunctionalVidPn, hModeSet);
            }
        }
    }

    /* Re-assert the firmware raster timing and our HVS display list. */
    if (!Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension) &&
        DeviceExtension->PixelValveValid)
        Rpi5CrtcProgramCurrentTiming(DeviceExtension);
    if (!Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension))
        Rpi5HvsInstallScanout(DeviceExtension);

    DeviceExtension->VidPnCommitted = TRUE;
    return STATUS_SUCCESS;
}

/* ========================================================================
 * Multi-plane overlay — the HVS is a hardware compositor: each MPO plane
 * maps 1:1 onto an HVS display-list plane (position, size, per-pixel
 * alpha).  Constraints of the current display-list builder: identity
 * rotation/flip, unscaled (SrcRect size == DstRect size), 32bpp planes in
 * the VRAM slab, and at most RPI5VC4_MPO_MAX_PLANES planes.
 * ====================================================================== */

#define RPI5VC4_MPO_MAX_PLANES RPI5_HVS_MPO_MAX_PLANES

static BOOLEAN
Rpi5Vc4MpoPlaneSupported(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES *Attributes)
{
    UNREFERENCED_PARAMETER(DeviceExtension);

    if (Attributes->Flags.VerticalFlip || Attributes->Flags.HorizontalFlip)
        return FALSE;

    if (Attributes->Rotation != D3DDDI_ROTATION_IDENTITY)
        return FALSE;

    /* The display-list builder emits UNITY (unscaled) planes only. */
    if ((Attributes->SrcRect.right - Attributes->SrcRect.left) !=
        (Attributes->DstRect.right - Attributes->DstRect.left) ||
        (Attributes->SrcRect.bottom - Attributes->SrcRect.top) !=
        (Attributes->DstRect.bottom - Attributes->DstRect.top))
    {
        return FALSE;
    }

    if (Attributes->VideoFrameFormat !=
        DXGK_MULTIPLANE_OVERLAY_VIDEO_FRAME_FORMAT_PROGRESSIVE)
    {
        return FALSE;
    }

    if (Attributes->StereoFormat !=
        DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT_MONO)
    {
        return FALSE;
    }

    return TRUE;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiCheckMultiPlaneOverlaySupport(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT CheckMultiPlaneOverlaySupport)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    UINT i;

    if (DeviceExtension == NULL || CheckMultiPlaneOverlaySupport == NULL)
        return STATUS_INVALID_PARAMETER;

    CheckMultiPlaneOverlaySupport->Supported =
        !Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension);
    CheckMultiPlaneOverlaySupport->ReturnInfo.Value = 0;

    if (!CheckMultiPlaneOverlaySupport->Supported)
        return STATUS_SUCCESS;

    if (CheckMultiPlaneOverlaySupport->PlaneCount > RPI5VC4_MPO_MAX_PLANES || (CheckMultiPlaneOverlaySupport->PlaneCount != 0 && CheckMultiPlaneOverlaySupport->pPlanes == NULL))
    {
        CheckMultiPlaneOverlaySupport->Supported = FALSE;
        return STATUS_SUCCESS;
    }

    for (i = 0; i < CheckMultiPlaneOverlaySupport->PlaneCount; i++)
    {
        if (CheckMultiPlaneOverlaySupport->pPlanes[i].VidPnSourceId != 0 || !Rpi5Vc4MpoPlaneSupported(DeviceExtension, &CheckMultiPlaneOverlaySupport->pPlanes[i].PlaneAttributes))
        {
            CheckMultiPlaneOverlaySupport->Supported = FALSE;
            CheckMultiPlaneOverlaySupport->ReturnInfo.FailingPlane = i;
            break;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiSetVidPnSourceAddressWithMultiPlaneOverlay(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY *SetMpo)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    CONST DXGK_MULTIPLANE_OVERLAY_PLANE *Base = NULL;
    UINT i;

    if (DeviceExtension == NULL || SetMpo == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Rpi5Vc4IsFixedFirmwareScanout(DeviceExtension))
        return STATUS_NOT_SUPPORTED;

    if (SetMpo->VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    if (SetMpo->PlaneCount > RPI5VC4_MPO_MAX_PLANES ||
        (SetMpo->PlaneCount != 0 && SetMpo->pPlanes == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Map the enabled MPO planes (bottom-up by LayerIndex) onto HVS
     * display-list planes.  A single base plane flips through the live
     * head; multi-plane configurations compose through the private
     * double-buffered display-list slots (Rpi5HvsInstallPlaneList).
     */
    {
        RPI5VC4_HVS_PLANE HvsPlanes[RPI5_HVS_MPO_MAX_PLANES];
        ULONG EnabledCount = 0;
        ULONG Layer;

        for (Layer = 0; Layer < RPI5_HVS_MPO_MAX_PLANES; Layer++)
        {
            for (i = 0; i < SetMpo->PlaneCount; i++)
            {
                CONST DXGK_MULTIPLANE_OVERLAY_PLANE *Plane = &SetMpo->pPlanes[i];
                ULONGLONG SlabBase =
                    (ULONGLONG)DeviceExtension->VramPhysical.QuadPart;
                ULONGLONG Phys;

                if (!Plane->Enabled || Plane->LayerIndex != Layer)
                    continue;

                if (EnabledCount >= RPI5_HVS_MPO_MAX_PLANES)
                    return STATUS_NOT_IMPLEMENTED;

                if (!Rpi5Vc4MpoPlaneSupported(DeviceExtension,
                                              &Plane->PlaneAttributes))
                {
                    return STATUS_NOT_IMPLEMENTED;
                }

                Phys = (ULONGLONG)Plane->AllocationAddress.QuadPart;
                if (Phys != (ULONGLONG)DeviceExtension->FirmwareFrameBufferPhysical.QuadPart &&
                    (DeviceExtension->VramVa == NULL ||
                     Phys < SlabBase ||
                     Phys >= SlabBase + DeviceExtension->VramSize))
                {
                    return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
                }

                HvsPlanes[EnabledCount].Phys = Phys;
                HvsPlanes[EnabledCount].X = Plane->PlaneAttributes.DstRect.left;
                HvsPlanes[EnabledCount].Y = Plane->PlaneAttributes.DstRect.top;
                HvsPlanes[EnabledCount].Width =
                    Plane->PlaneAttributes.DstRect.right -
                    Plane->PlaneAttributes.DstRect.left;
                HvsPlanes[EnabledCount].Height =
                    Plane->PlaneAttributes.DstRect.bottom -
                    Plane->PlaneAttributes.DstRect.top;
                HvsPlanes[EnabledCount].PitchBytes =
                    HvsPlanes[EnabledCount].Width * 4;
                /* Layer 0 is the opaque base; overlays alpha-blend. */
                HvsPlanes[EnabledCount].Opaque =
                    (Layer == 0) ||
                    !Plane->PlaneAttributes.Blend.AlphaBlend;
                EnabledCount++;

                if (Layer == 0)
                    Base = Plane;
            }
        }

        if (EnabledCount == 0)
        {
            /* All planes disabled: park on the firmware framebuffer. */
            Rpi5HvsFlipScanout(DeviceExtension,
                               DeviceExtension->FirmwareFrameBufferPhysical);
            return STATUS_SUCCESS;
        }

        if (EnabledCount == 1 && Base != NULL)
        {
            /* Base-plane-only: stay on the validated single-plane path. */
            if (!Rpi5HvsFlipScanout(DeviceExtension, Base->AllocationAddress))
            {
                if (DeviceExtension->HvsFlipBroken)
                    return STATUS_UNSUCCESSFUL;

                Rpi5HvsInstallScanout(DeviceExtension);
                if (!Rpi5HvsFlipScanout(DeviceExtension,
                                        Base->AllocationAddress))
                {
                    return STATUS_UNSUCCESSFUL;
                }
            }
            return STATUS_SUCCESS;
        }

        if (!Rpi5HvsInstallPlaneList(DeviceExtension, HvsPlanes, EnabledCount))
            return STATUS_UNSUCCESSFUL;

        /* Presents and flips track the base layer from here. */
        if (Base != NULL)
            DeviceExtension->FrameBufferPhysical = Base->AllocationAddress;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiUpdateActiveVidPnPresentPath(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *UpdateActiveVidPnPresentPath)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL || UpdateActiveVidPnPresentPath == NULL)
        return STATUS_INVALID_PARAMETER;

    if (UpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    if (UpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnTargetId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    if (UpdateActiveVidPnPresentPath->VidPnPresentPathInfo.ContentTransformation.Scaling !=
            D3DKMDT_VPPS_IDENTITY ||
        UpdateActiveVidPnPresentPath->VidPnPresentPathInfo.ContentTransformation.Rotation !=
            DeviceExtension->PathRotation)
    {
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiRecommendMonitorModes(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *RecommendMonitorModes)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    D3DKMDT_MONITOR_SOURCE_MODE *NewMode = NULL;
    NTSTATUS Status;

    if (DeviceExtension == NULL || RecommendMonitorModes == NULL)
        return STATUS_INVALID_PARAMETER;

    if (RecommendMonitorModes->VideoPresentTargetId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    if (RecommendMonitorModes->pMonitorSourceModeSetInterface == NULL)
        return STATUS_SUCCESS;

    /* Recommend the firmware raster as the (only) monitor mode. */
    Status = RecommendMonitorModes->pMonitorSourceModeSetInterface->
                 pfnCreateNewModeInfo(RecommendMonitorModes->hMonitorSourceModeSet,
                                      &NewMode);
    if (!NT_SUCCESS(Status))
        return STATUS_SUCCESS;

    Rpi5Vc4FillVideoSignalInfo(DeviceExtension, &NewMode->VideoSignalInfo);
    NewMode->ColorBasis = D3DKMDT_CB_SRGB;
    NewMode->ColorCoeffDynamicRanges.FirstChannel = 8;
    NewMode->ColorCoeffDynamicRanges.SecondChannel = 8;
    NewMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
    NewMode->ColorCoeffDynamicRanges.FourthChannel = 0;
    NewMode->Origin = D3DKMDT_MCO_DRIVER;
    NewMode->Preference = D3DKMDT_MP_PREFERRED;

    Status = RecommendMonitorModes->pMonitorSourceModeSetInterface->
                 pfnAddMode(RecommendMonitorModes->hMonitorSourceModeSet, NewMode);
    if (!NT_SUCCESS(Status))
    {
        RecommendMonitorModes->pMonitorSourceModeSetInterface->
            pfnReleaseModeInfo(RecommendMonitorModes->hMonitorSourceModeSet,
                               NewMode);
    }

    return STATUS_SUCCESS;
}
