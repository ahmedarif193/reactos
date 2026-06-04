/*
 * PROJECT:     ReactOS RPi5 Display-Only WDDM Miniport (rpi5dod)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidPN (Video Present Network) mode-set DDIs.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * The firmware-GOP path exposes exactly one display mode: the geometry the
 * UEFI firmware already programmed the HVS to scan out.  These DDIs build
 * that single mode into the source / target / monitor mode sets so dxgkrnl
 * can pin and commit it.  A full multi-mode implementation (HVS modesetting
 * over real EDID) is a hardware follow-up; see the file header in rpi5dod.h.
 */

#include "rpi5dod.h"

#define R5DOD_TRACE(fmt, ...) DbgPrint("RPI5DOD: " fmt, ##__VA_ARGS__)

/* The single source pixel format DODs expose. */
static CONST D3DDDIFORMAT g_Rpi5DodSourceFormat = D3DDDIFMT_A8R8G8B8;

/* ----------------------------------------------------------------------
 * Helpers to build the single GOP mode into the mode sets.
 * -------------------------------------------------------------------- */

static NTSTATUS
Rpi5DodAddSourceMode(
    _In_ PRPI5DOD_MODE Mode,
    _In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *Iface,
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hModeSet)
{
    D3DKMDT_VIDPN_SOURCE_MODE *SrcMode = NULL;
    NTSTATUS Status;

    Status = Iface->pfnCreateNewModeInfo(hModeSet, &SrcMode);
    if (!NT_SUCCESS(Status))
        return Status;

    SrcMode->Type = D3DKMDT_RMT_GRAPHICS;
    SrcMode->Format.Graphics.PrimSurfSize.cx   = Mode->DispInfo.Width;
    SrcMode->Format.Graphics.PrimSurfSize.cy   = Mode->DispInfo.Height;
    SrcMode->Format.Graphics.VisibleRegionSize = SrcMode->Format.Graphics.PrimSurfSize;
    SrcMode->Format.Graphics.Stride            = Mode->DispInfo.Pitch;
    SrcMode->Format.Graphics.PixelFormat       = g_Rpi5DodSourceFormat;
    SrcMode->Format.Graphics.ColorBasis        = D3DKMDT_CB_SCRGB;
    SrcMode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

    Status = Iface->pfnAddMode(hModeSet, SrcMode);
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            Status = STATUS_SUCCESS;
        Iface->pfnReleaseModeInfo(hModeSet, SrcMode);
    }

    return Status;
}

static NTSTATUS
Rpi5DodAddTargetMode(
    _In_ PRPI5DOD_MODE Mode,
    _In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE *Iface,
    _In_ D3DKMDT_HVIDPNTARGETMODESET hModeSet)
{
    D3DKMDT_VIDPN_TARGET_MODE *TgtMode = NULL;
    NTSTATUS Status;

    Status = Iface->pfnCreateNewModeInfo(hModeSet, &TgtMode);
    if (!NT_SUCCESS(Status))
        return Status;

    TgtMode->VideoSignalInfo.VideoStandard      = D3DKMDT_VSS_OTHER;
    TgtMode->VideoSignalInfo.TotalSize.cx        = Mode->DispInfo.Width;
    TgtMode->VideoSignalInfo.TotalSize.cy        = Mode->DispInfo.Height;
    TgtMode->VideoSignalInfo.ActiveSize          = TgtMode->VideoSignalInfo.TotalSize;
    TgtMode->VideoSignalInfo.VSyncFreq.Numerator   = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    TgtMode->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    TgtMode->VideoSignalInfo.HSyncFreq.Numerator   = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    TgtMode->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    TgtMode->VideoSignalInfo.PixelRate             = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    TgtMode->VideoSignalInfo.ScanLineOrdering      = D3DDDI_VSSLO_PROGRESSIVE;
    TgtMode->Preference                            = D3DKMDT_MP_PREFERRED;

    Status = Iface->pfnAddMode(hModeSet, TgtMode);
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            Status = STATUS_SUCCESS;
        Iface->pfnReleaseModeInfo(hModeSet, TgtMode);
    }

    return Status;
}

/* ----------------------------------------------------------------------
 * DDIs
 * -------------------------------------------------------------------- */

NTSTATUS APIENTRY
Rpi5DodIsSupportedVidPn(
    _In_ VOID *DeviceContext,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN *IsSupportedVidPn)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceContext);

    if (IsSupportedVidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * The firmware-GOP path supports exactly one functional configuration
     * (the POST geometry).  dxgkrnl's commit path pins that mode from the
     * POST display info, so accepting all VidPNs here is correct and simple.
     */
    IsSupportedVidPn->IsVidPnSupported = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodEnumVidPnCofuncModality(
    _In_ VOID *DeviceContext,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *CONST EnumCofuncModality)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;
    PRPI5DOD_MODE   Mode;
    CONST DXGK_VIDPN_INTERFACE             *VidPnIface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY                  hTopology = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE     *TopoIface = NULL;
    SIZE_T                                  PathIdx;
    SIZE_T                                  NumPaths = 0;
    NTSTATUS                                Status;

    PAGED_CODE();

    if (Device == NULL || EnumCofuncModality == NULL)
        return STATUS_INVALID_PARAMETER;

    Mode = &Device->Mode[0];

    Status = Device->DxgkInterface.DxgkCbQueryVidPnInterface(
                 EnumCofuncModality->hConstrainingVidPn,
                 DXGK_VIDPN_INTERFACE_VERSION_V1,
                 &VidPnIface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = VidPnIface->pfnGetTopology(EnumCofuncModality->hConstrainingVidPn,
                                        &hTopology, &TopoIface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = TopoIface->pfnGetNumPaths(hTopology, &NumPaths);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Walk each present path and pin the single GOP source/target mode set. */
    for (PathIdx = 0; PathIdx < NumPaths; PathIdx++)
    {
        CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;

        Status = (PathIdx == 0)
            ? TopoIface->pfnAcquireFirstPathInfo(hTopology, &Path)
            : STATUS_NO_MORE_FILES;
        /*
         * The V1 topology interface exposes only first/next iteration; to keep
         * this compact we acquire the first path (single path on the Pi 5) and
         * build its mode sets, which is the only path for one HDMI output.
         */
        if (PathIdx > 0)
            break;
        if (!NT_SUCCESS(Status))
            break;

        /* --- Source mode set --- */
        {
            D3DKMDT_HVIDPNSOURCEMODESET hSrcSet = NULL;
            CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *SrcIface = NULL;

            Status = VidPnIface->pfnCreateNewSourceModeSet(
                         EnumCofuncModality->hConstrainingVidPn,
                         Path->VidPnSourceId, &hSrcSet, &SrcIface);
            if (NT_SUCCESS(Status))
            {
                Status = Rpi5DodAddSourceMode(Mode, SrcIface, hSrcSet);
                if (NT_SUCCESS(Status))
                {
                    Status = VidPnIface->pfnAssignSourceModeSet(
                                 EnumCofuncModality->hConstrainingVidPn,
                                 Path->VidPnSourceId, hSrcSet);
                }
                if (!NT_SUCCESS(Status))
                    VidPnIface->pfnReleaseSourceModeSet(
                        EnumCofuncModality->hConstrainingVidPn, hSrcSet);
            }
        }

        /* --- Target mode set --- */
        if (NT_SUCCESS(Status))
        {
            D3DKMDT_HVIDPNTARGETMODESET hTgtSet = NULL;
            CONST DXGK_VIDPNTARGETMODESET_INTERFACE *TgtIface = NULL;

            Status = VidPnIface->pfnCreateNewTargetModeSet(
                         EnumCofuncModality->hConstrainingVidPn,
                         Path->VidPnTargetId, &hTgtSet, &TgtIface);
            if (NT_SUCCESS(Status))
            {
                Status = Rpi5DodAddTargetMode(Mode, TgtIface, hTgtSet);
                if (NT_SUCCESS(Status))
                {
                    Status = VidPnIface->pfnAssignTargetModeSet(
                                 EnumCofuncModality->hConstrainingVidPn,
                                 Path->VidPnTargetId, hTgtSet);
                }
                if (!NT_SUCCESS(Status))
                    VidPnIface->pfnReleaseTargetModeSet(
                        EnumCofuncModality->hConstrainingVidPn, hTgtSet);
            }
        }

        TopoIface->pfnReleasePathInfo(hTopology, Path);
        break; /* single path */
    }

    return Status;
}

NTSTATUS APIENTRY
Rpi5DodSetVidPnSourceVisibility(
    _In_ VOID *DeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID Id;

    PAGED_CODE();

    if (Device == NULL || SetVidPnSourceVisibility == NULL)
        return STATUS_INVALID_PARAMETER;

    Id = SetVidPnSourceVisibility->VidPnSourceId;
    if (Id == D3DDDI_ID_ALL)
    {
        ULONG i;
        for (i = 0; i < RPI5DOD_MAX_VIEWS; i++)
            Device->Mode[i].SourceVisible = SetVidPnSourceVisibility->Visible;
    }
    else if (Id < RPI5DOD_MAX_VIEWS)
    {
        Device->Mode[Id].SourceVisible = SetVidPnSourceVisibility->Visible;
    }
    else
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodCommitVidPn(
    _In_ VOID *DeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN *CONST CommitVidPn)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;
    PRPI5DOD_MODE   Mode;

    PAGED_CODE();

    if (Device == NULL || CommitVidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (CommitVidPn->AffectedVidPnSourceId >= RPI5DOD_MAX_VIEWS &&
        CommitVidPn->AffectedVidPnSourceId != D3DDDI_ID_ALL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * The scanout framebuffer was already established in StartDevice from the
     * POST geometry; committing the (only) mode simply confirms the source is
     * the GOP framebuffer.  Nothing else to program for the firmware-GOP path.
     */
    Mode = &Device->Mode[0];
    Mode->SourceVisible = TRUE;

    R5DOD_TRACE("CommitVidPn: source=%u committed (%lux%lu)\n",
                CommitVidPn->AffectedVidPnSourceId,
                Mode->DispInfo.Width, Mode->DispInfo.Height);

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodUpdateActiveVidPnPresentPath(
    _In_ VOID *DeviceContext,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *CONST UpdateActiveVidPnPresentPath)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceContext);

    if (UpdateActiveVidPnPresentPath == NULL)
        return STATUS_INVALID_PARAMETER;

    /* No path transforms (rotation/scaling) on the firmware-GOP path. */
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Rpi5DodRecommendFunctionalVidPn(
    _In_ VOID *DeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *CONST RecommendFunctionalVidPn)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(RecommendFunctionalVidPn);

    /* dxgkrnl builds the functional VidPN from the POST geometry. */
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS APIENTRY
Rpi5DodRecommendMonitorModes(
    _In_ VOID *DeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *CONST RecommendMonitorModes)
{
    PRPI5DOD_DEVICE Device = (PRPI5DOD_DEVICE)DeviceContext;
    PRPI5DOD_MODE   Mode;
    D3DKMDT_MONITOR_SOURCE_MODE *MonMode = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (Device == NULL || RecommendMonitorModes == NULL)
        return STATUS_INVALID_PARAMETER;

    Mode = &Device->Mode[0];

    Status = RecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(
                 RecommendMonitorModes->hMonitorSourceModeSet, &MonMode);
    if (!NT_SUCCESS(Status))
        return Status;

    MonMode->VideoSignalInfo.VideoStandard       = D3DKMDT_VSS_OTHER;
    MonMode->VideoSignalInfo.TotalSize.cx          = Mode->DispInfo.Width;
    MonMode->VideoSignalInfo.TotalSize.cy          = Mode->DispInfo.Height;
    MonMode->VideoSignalInfo.ActiveSize            = MonMode->VideoSignalInfo.TotalSize;
    MonMode->VideoSignalInfo.VSyncFreq.Numerator   = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    MonMode->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    MonMode->VideoSignalInfo.HSyncFreq.Numerator   = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    MonMode->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    MonMode->VideoSignalInfo.PixelRate             = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    MonMode->VideoSignalInfo.ScanLineOrdering      = D3DDDI_VSSLO_PROGRESSIVE;
    MonMode->Origin                                = D3DKMDT_MCO_DRIVER;
    MonMode->Preference                            = D3DKMDT_MP_PREFERRED;
    MonMode->ColorBasis                            = D3DKMDT_CB_SRGB;
    MonMode->ColorCoeffDynamicRanges.FirstChannel  = 8;
    MonMode->ColorCoeffDynamicRanges.SecondChannel = 8;
    MonMode->ColorCoeffDynamicRanges.ThirdChannel  = 8;
    MonMode->ColorCoeffDynamicRanges.FourthChannel = 0;

    Status = RecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(
                 RecommendMonitorModes->hMonitorSourceModeSet, MonMode);
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            Status = STATUS_SUCCESS;
        RecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(
            RecommendMonitorModes->hMonitorSourceModeSet, MonMode);
    }

    return Status;
}
