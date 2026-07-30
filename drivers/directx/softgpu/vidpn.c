/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VidPN DDI callbacks for softgpu.sys.
 *              Implements the Video Present Network miniport interface.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * VidPN (Video Present Network) overview
 * =======================================
 * A VidPN describes the mapping of rendering sources (primary surfaces) to
 * display targets (monitor connectors).  dxgkrnl manages the VidPN topology
 * and calls the miniport to:
 *
 *   IsSupportedVidPn         — validate a proposed VidPN topology.
 *   RecommendFunctionalVidPn — propose a full VidPN if none is given.
 *   EnumVidPnCofuncModality  — enumerate modes compatible with constraints.
 *   CommitVidPn              — activate a VidPN topology.
 *   SetVidPnSourceAddress    — program a new framebuffer address.
 *   SetVidPnSourceVisibility — toggle source display on/off.
 *
 * softgpu exposes one source, one target, and one identity path at the
 * firmware framebuffer's native size. The software renderer can extend an
 * unpinned VidPN with that mode, but it must reject pinned modes, transforms,
 * or topologies that the scanout path cannot implement.
 */

/* INCLUDES ******************************************************************/

#include "softgpu.h"
#include "softgpu_2d_core.h"
#include "vidpn_policy_core.h"

static NTSTATUS
SoftGpuScheduleScanout(
    _Inout_ PSOFTGPU_DEVICE Device);

/*
 * STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY and STATUS_MONITOR_NO_DESCRIPTOR
 * are defined in ntstatus.h (pulled in via ntddk.h / wdm.h).
 * No local defines needed.
 */

/* =========================================================================
 * DxgkDdiIsSupportedVidPn
 * =========================================================================
 */

static BOOLEAN
SoftGpuSourceModeMatchesDisplay(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ const D3DKMDT_VIDPN_SOURCE_MODE *Mode)
{
    return Mode->Type == D3DKMDT_RMT_GRAPHICS &&
           Mode->Format.Graphics.PrimSurfSize.cx == (LONG)Device->Width &&
           Mode->Format.Graphics.PrimSurfSize.cy == (LONG)Device->Height &&
           Mode->Format.Graphics.VisibleRegionSize.cx ==
               (LONG)Device->Width &&
           Mode->Format.Graphics.VisibleRegionSize.cy ==
               (LONG)Device->Height &&
           Mode->Format.Graphics.Stride ==
               Device->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL &&
           (Mode->Format.Graphics.PixelFormat == D3DDDIFMT_X8R8G8B8 ||
            Mode->Format.Graphics.PixelFormat == D3DDDIFMT_A8R8G8B8) &&
           Mode->Format.Graphics.PixelValueAccessMode == D3DKMDT_PVAM_DIRECT;
}

static BOOLEAN
SoftGpuTargetModeMatchesDisplay(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ const D3DKMDT_VIDPN_TARGET_MODE *Mode)
{
    return Mode->VideoSignalInfo.ActiveSize.cx == (LONG)Device->Width &&
           Mode->VideoSignalInfo.ActiveSize.cy == (LONG)Device->Height &&
           Mode->VideoSignalInfo.ScanLineOrdering ==
               D3DDDI_VSSLO_PROGRESSIVE;
}

static NTSTATUS
SoftGpuReleasePathInfo(
    _In_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface,
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *Path,
    _In_ NTSTATUS Status)
{
    NTSTATUS ReleaseStatus;

    ReleaseStatus =
        TopologyInterface->pfnReleasePathInfo(hTopology, Path);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        return ReleaseStatus;

    return Status;
}

static NTSTATUS
SoftGpuCheckPinnedSourceMode(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _Out_ PBOOLEAN Supported)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface = NULL;
    const D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    *Supported = FALSE;
    Status = VidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                                                     0,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSetInterface == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet,
                                                        &PinnedMode);
    if (NT_SUCCESS(Status))
    {
        *Supported = PinnedMode == NULL ||
                     SoftGpuSourceModeMatchesDisplay(Device, PinnedMode);
    }

    if (PinnedMode != NULL)
    {
        ReleaseStatus = ModeSetInterface->pfnReleaseModeInfo(hModeSet,
                                                             PinnedMode);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

static NTSTATUS
SoftGpuCheckPinnedTargetMode(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _Out_ PBOOLEAN Supported)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSetInterface = NULL;
    const D3DKMDT_VIDPN_TARGET_MODE *PinnedMode = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    *Supported = FALSE;
    Status = VidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                                                     0,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSetInterface == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet,
                                                        &PinnedMode);
    if (NT_SUCCESS(Status))
    {
        *Supported = PinnedMode == NULL ||
                     SoftGpuTargetModeMatchesDisplay(Device, PinnedMode);
    }

    if (PinnedMode != NULL)
    {
        ReleaseStatus = ModeSetInterface->pfnReleaseModeInfo(hModeSet,
                                                             PinnedMode);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

static NTSTATUS
SoftGpuValidateCommittedSourceMode(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    Status = VidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                                                     0,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSetInterface == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet,
                                                        &PinnedMode);
    if (!NT_SUCCESS(Status))
        goto ReleaseModeInfo;
    if (PinnedMode == NULL)
    {
        Status = STATUS_GRAPHICS_MODE_NOT_PINNED;
        goto ReleaseModeInfo;
    }
    if (!SoftGpuSourceModeMatchesDisplay(Device, PinnedMode))
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;

ReleaseModeInfo:
    if (PinnedMode != NULL)
    {
        ReleaseStatus = ModeSetInterface->pfnReleaseModeInfo(hModeSet,
                                                             PinnedMode);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

static NTSTATUS
SoftGpuValidateCommittedTargetMode(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *PinnedMode = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    Status = VidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                                                     0,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSetInterface == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet,
                                                        &PinnedMode);
    if (!NT_SUCCESS(Status))
        goto ReleaseModeInfo;
    if (PinnedMode == NULL)
    {
        Status = STATUS_GRAPHICS_MODE_NOT_PINNED;
        goto ReleaseModeInfo;
    }
    if (!SoftGpuTargetModeMatchesDisplay(Device, PinnedMode))
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET_MODE;

ReleaseModeInfo:
    if (PinnedMode != NULL)
    {
        ReleaseStatus = ModeSetInterface->pfnReleaseModeInfo(hModeSet,
                                                             PinnedMode);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

/*
 * Validate the desired VidPN without changing it. An empty VidPN is always
 * supported. A non-empty VidPN is extensible only when its sole 0 -> 0 path,
 * any pinned modes, and any pinned transforms match the native scanout.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiIsSupportedVidPn(
    _In_    PVOID                     MiniportDeviceContext,
    _Inout_ PDXGKARG_ISSUPPORTEDVIDPN IsSupportedVidPn)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    const DXGK_VIDPN_INTERFACE *VidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface = NULL;
    const D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    SIZE_T NumPaths;
    BOOLEAN SourceModeSupported;
    BOOLEAN TargetModeSupported;
    BOOLEAN PathSupported;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        IsSupportedVidPn == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: IsSupportedVidPn hDesiredVidPn=%p\n",
           (PVOID)IsSupportedVidPn->hDesiredVidPn);

    IsSupportedVidPn->IsVidPnSupported = FALSE;
    if (IsSupportedVidPn->hDesiredVidPn == 0)
    {
        IsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    if (Device->DxgkInterface.DxgkCbQueryVidPnInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;

    Status = Device->DxgkInterface.DxgkCbQueryVidPnInterface(
                 IsSupportedVidPn->hDesiredVidPn,
                 DXGK_VIDPN_INTERFACE_VERSION_V1,
                 &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (VidPnInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;

    Status = VidPnInterface->pfnGetTopology(
                 IsSupportedVidPn->hDesiredVidPn,
                 &hTopology,
                 &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TopologyInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;

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
        (Path->ContentTransformation.Rotation == D3DKMDT_VPPR_IDENTITY ||
         Path->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED ||
         Path->ContentTransformation.Rotation == D3DKMDT_VPPR_NOTSPECIFIED);

    ReleaseStatus = TopologyInterface->pfnReleasePathInfo(hTopology, Path);
    if (!NT_SUCCESS(ReleaseStatus))
        return ReleaseStatus;
    if (!PathSupported)
        return STATUS_SUCCESS;

    Status = SoftGpuCheckPinnedSourceMode(Device,
                                          IsSupportedVidPn->hDesiredVidPn,
                                          VidPnInterface,
                                          &SourceModeSupported);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!SourceModeSupported)
        return STATUS_SUCCESS;

    Status = SoftGpuCheckPinnedTargetMode(Device,
                                          IsSupportedVidPn->hDesiredVidPn,
                                          VidPnInterface,
                                          &TargetModeSupported);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!TargetModeSupported)
        return STATUS_SUCCESS;

    IsSupportedVidPn->IsVidPnSupported = TRUE;
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiRecommendFunctionalVidPn
 * =========================================================================
 */

/*
 * SoftGpuDdiRecommendFunctionalVidPn
 *
 * Propose a functional VidPN when the OS has no prior state.
 * Since the ReactOS VidPN object model is not yet complete, we simply
 * return success without populating the VidPN object — dxgkrnl will
 * continue with whatever state it has.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiRecommendFunctionalVidPn(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *RecommendFunctionalVidPn)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        RecommendFunctionalVidPn == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: RecommendFunctionalVidPn "
           "hRecommendedFunctionalVidPn=%p\n",
           (PVOID)RecommendFunctionalVidPn->hRecommendedFunctionalVidPn);

    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}


/* =========================================================================
 * DxgkDdiEnumVidPnCofuncModality
 * =========================================================================
 */

/* Ensure the source mode set for SourceId holds the current mode (no-op when
 * a mode is already pinned there). */
static NTSTATUS
SoftGpuUpdateSourceModeSet(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    D3DKMDT_VIDPN_SOURCE_MODE *NewMode = NULL;
    NTSTATUS Status;

    Status = VidPnInterface->pfnAcquireSourceModeSet(hVidPn, SourceId,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet, &PinnedMode);
    if (!NT_SUCCESS(Status))
    {
        VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
        return Status;
    }
    if (PinnedMode != NULL)
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

    NewMode->Type = D3DKMDT_RMT_GRAPHICS;
    NewMode->Format.Graphics.PrimSurfSize.cx = Device->Width;
    NewMode->Format.Graphics.PrimSurfSize.cy = Device->Height;
    NewMode->Format.Graphics.VisibleRegionSize.cx = Device->Width;
    NewMode->Format.Graphics.VisibleRegionSize.cy = Device->Height;
    NewMode->Format.Graphics.Stride = Device->Width * 4;
    NewMode->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
    NewMode->Format.Graphics.ColorBasis = D3DKMDT_CB_SRGB;
    NewMode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

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

/* Ensure the target mode set for TargetId holds the current mode. */
static NTSTATUS
SoftGpuUpdateTargetModeSet(
    _In_ PSOFTGPU_DEVICE Device,
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
    if (!NT_SUCCESS(Status))
    {
        VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
        return Status;
    }
    if (PinnedMode != NULL)
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

    NewMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    NewMode->VideoSignalInfo.TotalSize.cx = Device->Width;
    NewMode->VideoSignalInfo.TotalSize.cy = Device->Height;
    NewMode->VideoSignalInfo.ActiveSize.cx = Device->Width;
    NewMode->VideoSignalInfo.ActiveSize.cy = Device->Height;
    NewMode->VideoSignalInfo.VSyncFreq.Numerator = 60;
    NewMode->VideoSignalInfo.VSyncFreq.Denominator = 1;
    NewMode->VideoSignalInfo.HSyncFreq.Numerator = 60 * Device->Height;
    NewMode->VideoSignalInfo.HSyncFreq.Denominator = 1;
    NewMode->VideoSignalInfo.PixelRate =
        (SIZE_T)Device->Width * Device->Height * 60;
    NewMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    NewMode->WireFormatAndPreference.Value = 0;
    NewMode->WireFormatAndPreference.Preference = D3DKMDT_MP_PREFERRED;
    NewMode->WireFormatAndPreference.Rgb =
        D3DKMDT_BITS_PER_COMPONENT_08;
#else
    NewMode->Preference = D3DKMDT_MP_PREFERRED;
#endif

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

/*
 * SoftGpuDdiEnumVidPnCofuncModality
 *
 * Populate unpinned source/target mode sets on every path of the
 * constraining VidPN with the current display mode (Basic Display model:
 * a single mode, the boot framebuffer's).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiEnumVidPnCofuncModality(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *EnumCofuncModality)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    CONST DXGK_VIDPN_INTERFACE *VidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *NextPath = NULL;
    D3DKMDT_VIDPN_PRESENT_PATH UpdatedPath;
    SOFTGPU_VIDPN_TRANSFORM_POLICY TransformPolicy;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        EnumCofuncModality == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Device->DxgkInterface.DxgkCbQueryVidPnInterface == NULL ||
        Device->Width == 0 || Device->Height == 0)
    {
        return STATUS_SUCCESS;
    }

    Status = Device->DxgkInterface.DxgkCbQueryVidPnInterface(
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
    if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;
    if (Path == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;

    while (Path != NULL)
    {
        if (!(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE &&
              EnumCofuncModality->EnumPivot.VidPnSourceId == Path->VidPnSourceId))
        {
            Status = SoftGpuUpdateSourceModeSet(Device,
                                                EnumCofuncModality->hConstrainingVidPn,
                                                VidPnInterface,
                                                Path->VidPnSourceId);
            if (!NT_SUCCESS(Status))
            {
                return SoftGpuReleasePathInfo(TopologyInterface,
                                              hTopology,
                                              Path,
                                              Status);
            }
        }

        if (!(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET &&
              EnumCofuncModality->EnumPivot.VidPnTargetId == Path->VidPnTargetId))
        {
            Status = SoftGpuUpdateTargetModeSet(Device,
                                                EnumCofuncModality->hConstrainingVidPn,
                                                VidPnInterface,
                                                Path->VidPnTargetId);
            if (!NT_SUCCESS(Status))
            {
                return SoftGpuReleasePathInfo(TopologyInterface,
                                              hTopology,
                                              Path,
                                              Status);
            }
        }

        UpdatedPath = *Path;
        SoftGpuVidPnEvaluateTransformPolicy(
            EnumCofuncModality->EnumPivotType,
            EnumCofuncModality->EnumPivot.VidPnSourceId,
            EnumCofuncModality->EnumPivot.VidPnTargetId,
            Path->VidPnSourceId,
            Path->VidPnTargetId,
            Path->ContentTransformation.Scaling,
            Path->ContentTransformation.Rotation,
            &TransformPolicy);

        if (TransformPolicy.UpdateScalingSupport)
        {
            RtlZeroMemory(
                &UpdatedPath.ContentTransformation.ScalingSupport,
                sizeof(UpdatedPath.ContentTransformation.ScalingSupport));
            UpdatedPath.ContentTransformation.ScalingSupport.Identity = 1;
        }

        if (TransformPolicy.UpdateRotationSupport)
        {
            RtlZeroMemory(
                &UpdatedPath.ContentTransformation.RotationSupport,
                sizeof(UpdatedPath.ContentTransformation.RotationSupport));
            UpdatedPath.ContentTransformation.RotationSupport.Identity = 1;
#if (REACTOS_WDDM_TARGET_LEVEL >= 1300)
            UpdatedPath.ContentTransformation.RotationSupport.Offset0 = 1;
#endif
        }

        if (TransformPolicy.UpdateScalingSupport ||
            TransformPolicy.UpdateRotationSupport)
        {
            Status = TopologyInterface->pfnUpdatePathSupportInfo(
                         hTopology,
                         &UpdatedPath);
            if (!NT_SUCCESS(Status))
            {
                return SoftGpuReleasePathInfo(TopologyInterface,
                                              hTopology,
                                              Path,
                                              Status);
            }
        }

        NextPath = NULL;
        Status = TopologyInterface->pfnAcquireNextPathInfo(hTopology, Path, &NextPath);
        if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
        {
            return SoftGpuReleasePathInfo(TopologyInterface,
                                          hTopology,
                                          Path,
                                          STATUS_SUCCESS);
        }
        if (!NT_SUCCESS(Status))
        {
            return SoftGpuReleasePathInfo(TopologyInterface,
                                          hTopology,
                                          Path,
                                          Status);
        }
        if (NextPath == NULL)
        {
            return SoftGpuReleasePathInfo(
                       TopologyInterface,
                       hTopology,
                       Path,
                       STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY);
        }

        ReleaseStatus =
            TopologyInterface->pfnReleasePathInfo(hTopology, Path);
        if (!NT_SUCCESS(ReleaseStatus))
        {
            TopologyInterface->pfnReleasePathInfo(hTopology, NextPath);
            return ReleaseStatus;
        }
        Path = NextPath;
    }

    return STATUS_SUCCESS;
}


static NTSTATUS
SoftGpuValidateFunctionalVidPn(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hFunctionalVidPn,
    _Out_opt_ PSIZE_T ValidatedPathCount)
{
    CONST DXGK_VIDPN_INTERFACE *VidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    SIZE_T NumPaths;
    NTSTATUS Status;

    if (ValidatedPathCount != NULL)
        *ValidatedPathCount = 0;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    if (hFunctionalVidPn == 0 ||
        Device->DxgkInterface.DxgkCbQueryVidPnInterface == NULL)
    {
        return STATUS_GRAPHICS_INVALID_VIDPN;
    }

    Status = Device->DxgkInterface.DxgkCbQueryVidPnInterface(
                 hFunctionalVidPn,
                 DXGK_VIDPN_INTERFACE_VERSION_V1,
                 &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (VidPnInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;

    Status = VidPnInterface->pfnGetTopology(hFunctionalVidPn,
                                            &hTopology,
                                            &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TopologyInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;

    Status = TopologyInterface->pfnGetNumPaths(hTopology, &NumPaths);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ValidatedPathCount != NULL)
        *ValidatedPathCount = NumPaths;
    if (NumPaths == 0)
        return STATUS_SUCCESS;
    if (NumPaths != 1)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;

    Status = TopologyInterface->pfnAcquireFirstPathInfo(hTopology, &Path);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Path == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;

    if (Path->VidPnSourceId != 0)
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    else if (Path->VidPnTargetId != 0)
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    else if (!SoftGpuVidPnActiveTransformSupported(
                 Path->ContentTransformation.Scaling,
                 Path->ContentTransformation.Rotation))
        Status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    else if (Path->GammaRamp.Type != D3DDDI_GAMMARAMP_DEFAULT)
        Status = STATUS_GRAPHICS_GAMMA_RAMP_NOT_SUPPORTED;
    else
        Status = STATUS_SUCCESS;

    Status = SoftGpuReleasePathInfo(TopologyInterface,
                                    hTopology,
                                    Path,
                                    Status);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SoftGpuValidateCommittedSourceMode(
                 Device,
                 hFunctionalVidPn,
                 VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SoftGpuValidateCommittedTargetMode(
                 Device,
                 hFunctionalVidPn,
                 VidPnInterface);
    return Status;
}


/* =========================================================================
 * DxgkDdiCommitVidPn
 * =========================================================================
 */

/*
 * Activate the new VidPN for pre-WDDM 2.3 callers. StartDevice has already
 * fixed the adapter geometry from the validated POST/loader framebuffer.
 * Commit validates the pinned source and target modes against that geometry;
 * it never substitutes a fallback mode.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiCommitVidPn(
    _In_ PVOID                      MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN *CommitVidPn)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    NTSTATUS Status;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        CommitVidPn == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: CommitVidPn hFunctionalVidPn=%p\n",
           (PVOID)CommitVidPn->hFunctionalVidPn);

    if (CommitVidPn->AffectedVidPnSourceId != 0 &&
        CommitVidPn->AffectedVidPnSourceId != D3DDDI_ID_ALL)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    if (CommitVidPn->MonitorConnectivityChecks != D3DKMDT_MCC_IGNORE &&
        CommitVidPn->MonitorConnectivityChecks != D3DKMDT_MCC_ENFORCE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = SoftGpuValidateFunctionalVidPn(
                 Device,
                 CommitVidPn->hFunctionalVidPn,
                 NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SOFTGPU: CommitVidPn rejected functional VidPN 0x%08lx\n",
                Status);
        return Status;
    }

    DPRINT("SOFTGPU: CommitVidPn: committed %lux%lu format=%d\n",
           Device->Width, Device->Height, (int)Device->Format);

    return STATUS_SUCCESS;
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2300)
/*
 * WDDM 2.3 timing transaction. The platform binding exposes a single fixed
 * firmware scanout, so this callback accepts only that pinned mode, SDR
 * G22/P709 over 8-bit RGB, and target zero. No hardware queue, MPO, protected
 * session, or programmable-link capability is implied.
 */
NTSTATUS
APIENTRY
SoftGpuDdiSetTimingsFromVidPn(
    _In_ PVOID MiniportDeviceContext,
    IN_OUT_PDXGKARG_SETTIMINGSFROMVIDPN SetTimings)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    DXGK_SET_TIMING_PATH_INFO *PathInfo = NULL;
    SIZE_T VidPnPathCount;
    BOOLEAN Active;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SetTimings == NULL ||
        SetTimings->pResultsFlags == NULL ||
        SetTimings->SetFlags.Value != 0 ||
        SetTimings->PathCount > 1 ||
        (SetTimings->PathCount != 0 &&
         SetTimings->pSetTimingPathInfo == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    SetTimings->pResultsFlags->Value = 0;
    Status = SoftGpuValidateFunctionalVidPn(
                 Device,
                 SetTimings->hFunctionalVidPn,
                 &VidPnPathCount);
    if (!NT_SUCCESS(Status))
        return Status;

    if (SetTimings->PathCount != 0)
    {
        PathInfo = &SetTimings->pSetTimingPathInfo[0];
        if (PathInfo->VidPnTargetId != 0 ||
            (PathInfo->InputFlags & ~0x1FUL) != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    if (VidPnPathCount == 0)
    {
        if (PathInfo != NULL &&
            (PathInfo->Input.VidPnPathUpdates !=
                 DXGK_PATH_UPDATE_REMOVED ||
             PathInfo->Input.Active != 0))
        {
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        }
        Active = FALSE;
    }
    else
    {
        if (VidPnPathCount != 1 ||
            PathInfo == NULL ||
            PathInfo->Input.VidPnPathUpdates ==
                DXGK_PATH_UPDATE_REMOVED)
        {
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        }

        if (PathInfo->OutputWireColorSpace !=
                D3DDDI_OUTPUT_WIRE_COLOR_SPACE_G22_P709 ||
            PathInfo->SelectedWireFormat.Preference != 0 ||
            PathInfo->SelectedWireFormat.Rgb !=
                D3DKMDT_BITS_PER_COMPONENT_08 ||
            PathInfo->SelectedWireFormat.YCbCr444 != 0 ||
            PathInfo->SelectedWireFormat.YCbCr422 != 0 ||
            PathInfo->SelectedWireFormat.YCbCr420 != 0 ||
            PathInfo->SelectedWireFormat.Intensity != 0)
        {
            return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        }
        Active = PathInfo->Input.Active ? TRUE : FALSE;
    }

    if (PathInfo != NULL)
    {
        PathInfo->OutputFlags = 0;
        RtlZeroMemory(&PathInfo->TargetState,
                      sizeof(PathInfo->TargetState));
        PathInfo->TargetState.ConnectionChangeId = 1;
        PathInfo->TargetState.TargetId = 0;
        PathInfo->TargetState.ConnectionStatus =
            MonitorStatusConnected;
        PathInfo->TargetState.MonitorConnect.LinkTargetType =
            D3DKMDT_VOT_OTHER;
        PathInfo->DiagnosticInfo = 0;
        PathInfo->GlitchCause = DXGK_GLITCH_CAUSE_NONE;
        PathInfo->GlitchEffect = DXGK_GLITCH_EFFECT_SEAMLESS;
        PathInfo->GlitchDuration = DXGK_GLITCH_DURATION_NONE;
    }

    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    Device->TimingActive = Active;
    if (++Device->ScanoutGeneration == 0)
        ++Device->ScanoutGeneration;
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

    Status = SoftGpuScheduleScanout(Device);
    if (!NT_SUCCESS(Status))
        return Status;

    DPRINT("SOFTGPU: SetTimingsFromVidPn path=%Iu active=%u\n",
           VidPnPathCount,
           Active);
    return STATUS_SUCCESS;
}
#endif


/* =========================================================================
 * DxgkDdiSetVidPnSourceAddress
 * =========================================================================
 */

typedef struct _SOFTGPU_SCANOUT_SNAPSHOT
{
    PVOID Source;
    SIZE_T SourceSize;
    PVOID Destination;
    SIZE_T DestinationSize;
    ULONG SourcePitch;
    ULONG DestinationPitch;
    ULONG Width;
    ULONG Height;
    ULONG Generation;
    BOOLEAN Valid;
    BOOLEAN Visible;
} SOFTGPU_SCANOUT_SNAPSHOT, *PSOFTGPU_SCANOUT_SNAPSHOT;

static NTSTATUS
SoftGpuCopyCurrentPrimaryToScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    SOFTGPU_SCANOUT_SNAPSHOT Snapshot;
    KIRQL OldIrql;
    RECT FrameRect;
    NTSTATUS Status;

    PAGED_CODE();

    Status = KeWaitForSingleObject(&Device->ScanoutMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Snapshot, sizeof(Snapshot));
    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    if (InterlockedCompareExchange(&Device->Stopped, 0, 0) == 0 &&
        Device->FrameBuffer != NULL &&
        Device->CurrentPrimaryOffset <= Device->FrameBufferSize)
    {
        Snapshot.Source =
            (PUCHAR)Device->FrameBuffer +
            Device->CurrentPrimaryOffset;
        Snapshot.SourceSize =
            Device->FrameBufferSize -
            (SIZE_T)Device->CurrentPrimaryOffset;
        Snapshot.Destination = Device->Scanout;
        Snapshot.DestinationSize = Device->ScanoutSize;
        Snapshot.SourcePitch = Device->CurrentPrimaryPitch;
        Snapshot.DestinationPitch = Device->ScanoutPitch;
        Snapshot.Width = Device->CurrentPrimaryWidth;
        Snapshot.Height = Device->CurrentPrimaryHeight;
        Snapshot.Generation = Device->ScanoutGeneration;
        Snapshot.Valid = Device->CurrentPrimaryValid;
        Snapshot.Visible =
            Device->ScanoutVisible && Device->TimingActive;
    }
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

    if (Snapshot.Destination == NULL)
    {
        /* A genuinely headless generic adapter has no firmware scanout. */
        Status = STATUS_SUCCESS;
        goto Complete;
    }
    if (!Snapshot.Valid ||
        Snapshot.Source == NULL ||
        Snapshot.Width == 0 ||
        Snapshot.Height == 0 ||
        Snapshot.Width >
            MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Complete;
    }

    FrameRect.left = 0;
    FrameRect.top = 0;
    FrameRect.right = (LONG)Snapshot.Width;
    FrameRect.bottom = (LONG)Snapshot.Height;
    if (Snapshot.Visible)
    {
        Status = SoftGpu2dCopyRect(
                     Snapshot.Source,
                     Snapshot.SourceSize,
                     Snapshot.SourcePitch,
                     &FrameRect,
                     Snapshot.Destination,
                     Snapshot.DestinationSize,
                     Snapshot.DestinationPitch,
                     &FrameRect);
    }
    else
    {
        Status = SoftGpu2dFillRect(
                     Snapshot.Destination,
                     Snapshot.DestinationSize,
                     Snapshot.DestinationPitch,
                     &FrameRect,
                     0);
    }
    if (!NT_SUCCESS(Status))
        goto Complete;

    KeMemoryBarrier();

Complete:
    if (NT_SUCCESS(Status))
    {
        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        if (Device->ScanoutGeneration == Snapshot.Generation)
        {
            Device->ScanoutPresentedGeneration =
                Snapshot.Generation;
        }
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    }
    KeReleaseMutex(&Device->ScanoutMutex, FALSE);
    return Status;
}

static VOID
NTAPI
SoftGpuScanoutWorker(
    _In_ PVOID Context)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)Context;

    for (;;)
    {
        NTSTATUS Status =
            SoftGpuCopyCurrentPrimaryToScanout(Device);
        KIRQL OldIrql;
        BOOLEAN Repeat;

        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        Repeat =
            NT_SUCCESS(Status) &&
            InterlockedCompareExchange(&Device->Stopped, 0, 0) == 0 &&
            Device->ScanoutPresentedGeneration !=
                Device->ScanoutGeneration;
        if (!Repeat)
            InterlockedExchange(&Device->ScanoutWorkQueued, 0);
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

        if (!Repeat)
            break;
    }

    ExReleaseRundownProtection(&Device->ScanoutRundown);
}

VOID
SoftGpuScanoutInitializeDevice(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    KeInitializeSpinLock(&Device->ScanoutLock);
    KeInitializeMutex(&Device->ScanoutMutex, 0);
    ExInitializeRundownProtection(&Device->ScanoutRundown);
    ExInitializeWorkItem(&Device->ScanoutWorkItem,
                         SoftGpuScanoutWorker,
                         Device);
    Device->ScanoutRundownCompleted = FALSE;
    Device->ScanoutWorkQueued = 0;
}

NTSTATUS
SoftGpuScanoutStart(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_PLATFORM_CONFIG *Config)
{
    PVOID Mapping;
    SIZE_T MappingSize;
    ULONGLONG RequiredSize;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Device == NULL || Config == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Device->ScanoutRundownCompleted)
    {
        ExReInitializeRundownProtection(&Device->ScanoutRundown);
        Device->ScanoutRundownCompleted = FALSE;
    }

    if (Config->ScanoutPhysicalAddress.QuadPart == 0)
        return STATUS_SUCCESS;

    if (Device->Scanout != NULL ||
        Config->Width == 0 ||
        Config->Height == 0 ||
        Config->Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
        Config->Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        Config->Width >
            MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        Config->ScanoutPitch <
            Config->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        Config->ScanoutPitch > SOFTGPU_MAX_DISPLAY_PITCH ||
        Config->ScanoutSize == 0 ||
        Config->ScanoutSize > SOFTGPU_MAX_SURFACE_SIZE ||
        Config->ScanoutSize > MAXULONG_PTR)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RequiredSize =
        (ULONGLONG)Config->ScanoutPitch * Config->Height;
    if (RequiredSize == 0 || RequiredSize > Config->ScanoutSize)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    MappingSize = (SIZE_T)Config->ScanoutSize;
    Mapping = MmMapIoSpace(Config->ScanoutPhysicalAddress,
                           MappingSize,
                           MmWriteCombined);
    if (Mapping == NULL)
    {
        Mapping = MmMapIoSpace(Config->ScanoutPhysicalAddress,
                               MappingSize,
                               MmNonCached);
    }
    if (Mapping == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    Device->Scanout = Mapping;
    Device->ScanoutPhys = Config->ScanoutPhysicalAddress;
    Device->ScanoutSize = MappingSize;
    Device->ScanoutPitch = Config->ScanoutPitch;
    Device->CurrentPrimaryOffset = 0;
    Device->CurrentPrimaryPitch = 0;
    Device->CurrentPrimaryWidth = 0;
    Device->CurrentPrimaryHeight = 0;
    Device->CurrentPrimaryValid = FALSE;
    Device->ScanoutVisible = TRUE;
    Device->TimingActive = TRUE;
    Device->ScanoutGeneration = 1;
    Device->ScanoutPresentedGeneration = 0;
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    return STATUS_SUCCESS;
}

VOID
SoftGpuScanoutStop(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PVOID Mapping;
    SIZE_T MappingSize;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Device == NULL)
        return;

    if (!Device->ScanoutRundownCompleted)
    {
        ExWaitForRundownProtectionRelease(&Device->ScanoutRundown);
        Device->ScanoutRundownCompleted = TRUE;
    }

    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    Mapping = Device->Scanout;
    MappingSize = Device->ScanoutSize;
    Device->Scanout = NULL;
    Device->ScanoutPhys.QuadPart = 0;
    Device->ScanoutSize = 0;
    Device->ScanoutPitch = 0;
    Device->CurrentPrimaryValid = FALSE;
    Device->ScanoutVisible = FALSE;
    Device->TimingActive = FALSE;
    Device->ScanoutGeneration++;
    Device->ScanoutPresentedGeneration =
        Device->ScanoutGeneration;
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

    if (Mapping != NULL && MappingSize != 0)
        MmUnmapIoSpace(Mapping, MappingSize);
}

static NTSTATUS
SoftGpuScheduleScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    KIRQL CurrentIrql;
    KIRQL OldIrql;
    BOOLEAN QueueWork;
    NTSTATUS Status;

    if (!ExAcquireRundownProtection(&Device->ScanoutRundown))
        return STATUS_DELETE_PENDING;

    CurrentIrql = KeGetCurrentIrql();
    if (CurrentIrql == PASSIVE_LEVEL)
    {
        Status = SoftGpuCopyCurrentPrimaryToScanout(Device);
        ExReleaseRundownProtection(&Device->ScanoutRundown);
        return Status;
    }
    if (CurrentIrql > DISPATCH_LEVEL)
    {
        ExReleaseRundownProtection(&Device->ScanoutRundown);
        return STATUS_INVALID_DEVICE_STATE;
    }

    QueueWork = FALSE;
    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    if (InterlockedCompareExchange(&Device->Stopped, 0, 0) != 0)
    {
        Status = STATUS_DELETE_PENDING;
    }
    else if (InterlockedCompareExchange(
                 &Device->ScanoutWorkQueued, 1, 0) == 0)
    {
        QueueWork = TRUE;
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

    if (QueueWork)
    {
        ExQueueWorkItem(&Device->ScanoutWorkItem,
                        DelayedWorkQueue);
        /* The queued worker owns this rundown reference. */
        return STATUS_SUCCESS;
    }

    ExReleaseRundownProtection(&Device->ScanoutRundown);
    return Status;
}

/*
 * Validate a primary placement inside the software allocation slab, remember
 * its exact linear geometry, and copy it to the fixed firmware framebuffer.
 * Platform providers remain responsible only for validating and describing
 * the firmware scanout; the shared engine contains no bus or vendor policy.
 */
NTSTATUS
APIENTRY
SoftGpuDdiSetVidPnSourceAddress(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *SetVidPnSourceAddress)
{
    PSOFTGPU_DEVICE  Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    ULONGLONG        PrimaryAddress;
    ULONGLONG        FbBase;
    ULONGLONG        FbEnd;
    ULONGLONG        SurfaceSize;
    ULONGLONG        SurfaceSpan;
    PSOFTGPU_ALLOC   Allocation;
    ULONG            Width;
    ULONG            Height;
    ULONG            Pitch;
    KIRQL            OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SetVidPnSourceAddress == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: SetVidPnSourceAddress Source=%u PA=0x%I64x\n",
           SetVidPnSourceAddress->VidPnSourceId,
           SetVidPnSourceAddress->PrimaryAddress.QuadPart);

    if (SetVidPnSourceAddress->VidPnSourceId != 0)
    {
        DPRINT1("SOFTGPU: SetVidPnSourceAddress: invalid source %u\n",
                SetVidPnSourceAddress->VidPnSourceId);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    if (SetVidPnSourceAddress->PrimarySegment != 0 &&
        SetVidPnSourceAddress->PrimarySegment != SOFTGPU_SEGMENT_ID)
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }

    FbBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    if (Device->FrameBufferSize > (SIZE_T)(((ULONGLONG)-1) - FbBase))
        return STATUS_INTEGER_OVERFLOW;

    FbEnd = FbBase + (ULONGLONG)Device->FrameBufferSize;
    PrimaryAddress = (ULONGLONG)SetVidPnSourceAddress->PrimaryAddress.QuadPart;

    if (PrimaryAddress != 0)
    {
        Allocation =
            (PSOFTGPU_ALLOC)SetVidPnSourceAddress->hAllocation;
        if (Allocation == NULL ||
            Allocation->Magic != SOFTGPU_ALLOC_MAGIC)
        {
            return STATUS_INVALID_HANDLE;
        }

        Width = Allocation->Width != 0 ?
                    Allocation->Width :
                    Device->Width;
        Height = Allocation->Height != 0 ?
                     Allocation->Height :
                     Device->Height;
        Pitch = Allocation->Pitch != 0 ?
                    Allocation->Pitch :
                    Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;

        if (Width != Device->Width ||
            Height != Device->Height ||
            Width == 0 ||
            Height == 0 ||
            Width >
                MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

        SurfaceSize =
            (ULONGLONG)Width *
            SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
        if (Pitch < SurfaceSize ||
            Pitch > SOFTGPU_MAX_DISPLAY_PITCH ||
            Height > (~(ULONGLONG)0) / Pitch)
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }
        SurfaceSpan =
            (ULONGLONG)Pitch * (Height - 1) + SurfaceSize;

        if (PrimaryAddress < FbBase ||
            PrimaryAddress >= FbEnd ||
            SurfaceSpan > FbEnd - PrimaryAddress ||
            SurfaceSpan > Allocation->Size)
        {
            DPRINT1("SOFTGPU: SetVidPnSourceAddress: PA 0x%I64x outside "
                    "framebuffer [0x%I64x, 0x%I64x) for %I64u-byte surface\n",
                    PrimaryAddress,
                    FbBase,
                    FbEnd,
                    SurfaceSpan);
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        Device->CurrentPrimaryOffset =
            PrimaryAddress - FbBase;
        Device->CurrentPrimaryPitch = Pitch;
        Device->CurrentPrimaryWidth = Width;
        Device->CurrentPrimaryHeight = Height;
        Device->CurrentPrimaryValid = TRUE;
        if (++Device->ScanoutGeneration == 0)
            ++Device->ScanoutGeneration;
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    }
    else
    {
        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        Device->CurrentPrimaryValid = FALSE;
        if (++Device->ScanoutGeneration == 0)
            ++Device->ScanoutGeneration;
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    }

    return PrimaryAddress != 0 ?
               SoftGpuScheduleScanout(Device) :
               STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiSetVidPnSourceVisibility
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiSetVidPnSourceVisibility(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SetVidPnSourceVisibility == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: SetVidPnSourceVisibility Source=%u Visible=%d\n",
           SetVidPnSourceVisibility->VidPnSourceId,
           (int)SetVidPnSourceVisibility->Visible);

    if (SetVidPnSourceVisibility->VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    {
        KIRQL OldIrql;

        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        Device->ScanoutVisible =
            SetVidPnSourceVisibility->Visible ? TRUE : FALSE;
        if (++Device->ScanoutGeneration == 0)
            ++Device->ScanoutGeneration;
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    }

    return SoftGpuScheduleScanout(Device);
}


/* =========================================================================
 * DxgkDdiUpdateActiveVidPnPresentPath
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiUpdateActiveVidPnPresentPath(
    _In_ PVOID                                      MiniportDeviceContext,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *UpdateActiveVidPnPresentPath)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        UpdateActiveVidPnPresentPath == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (UpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    if (UpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnTargetId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    if (!SoftGpuVidPnActiveTransformSupported(
            UpdateActiveVidPnPresentPath->
                VidPnPresentPathInfo.ContentTransformation.Scaling,
            UpdateActiveVidPnPresentPath->
                VidPnPresentPathInfo.ContentTransformation.Rotation))
    {
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiRecommendMonitorModes
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiRecommendMonitorModes(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *RecommendMonitorModes)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        RecommendMonitorModes == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: RecommendMonitorModes TargetId=%u\n",
           RecommendMonitorModes->VideoPresentTargetId);

    if (RecommendMonitorModes->VideoPresentTargetId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    return STATUS_GRAPHICS_NO_PREFERRED_MODE;
}


/* =========================================================================
 * DxgkDdiRecommendVidPnTopology
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiRecommendVidPnTopology(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *RecommendVidPnTopology)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        RecommendVidPnTopology == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: RecommendVidPnTopology\n");

    /*
     * Returning STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY tells
     * dxgkrnl that the miniport has no topology recommendation; the OS
     * will use its own heuristics.
     */
    return STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY;
}


/* =========================================================================
 * DxgkDdiQueryDeviceDescriptor
 * =========================================================================
 */

/*
 * SoftGpuDdiQueryDeviceDescriptor
 *
 * Returns STATUS_MONITOR_NO_DESCRIPTOR to indicate that no EDID or other
 * device descriptor is available for the virtual monitor.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiQueryDeviceDescriptor(
    _In_    PVOID                   MiniportDeviceContext,
    _In_    ULONG                   ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        DeviceDescriptor == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ChildUid != 1)
        return STATUS_INVALID_PARAMETER;

    DPRINT("SOFTGPU: QueryDeviceDescriptor ChildUid=%lu\n", ChildUid);
    return STATUS_MONITOR_NO_DESCRIPTOR;
}

/* EOF */
