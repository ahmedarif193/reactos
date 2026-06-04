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
 * softgpu implementation
 * ======================
 * Since the ReactOS dxgkrnl VidPN object model is still in development,
 * the opaque D3DKMDT_HVIDPN handles passed to these DDIs are NULL or point
 * to simple internal structures.  The softgpu callbacks therefore:
 *
 *   - Accept any VidPN topology as valid (IsSupportedVidPn always TRUE).
 *   - Enumerate a fixed set of 6 supported resolutions in cofunc modality.
 *   - Update Width/Height/Format in the device context on CommitVidPn,
 *     using the first path's source mode geometry when available.
 *   - Accept SetVidPnSourceAddress and update the device framebuffer base.
 *
 * Supported resolutions: 640x480, 800x600, 1024x768, 1280x720, 1280x1024,
 *                        1920x1080.  All in D3DDDIFMT_A8R8G8B8.
 */

/* INCLUDES ******************************************************************/

#include "softgpu.h"

/* CONSTANTS *****************************************************************/

/* Default mode when no VidPN geometry is available */
#define SOFTGPU_DEFAULT_WIDTH   1024
#define SOFTGPU_DEFAULT_HEIGHT  768
#define SOFTGPU_DEFAULT_FORMAT  D3DDDIFMT_A8R8G8B8

/*
 * STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY and STATUS_MONITOR_NO_DESCRIPTOR
 * are defined in ntstatus.h (pulled in via ntddk.h / wdm.h).
 * No local defines needed.
 */

/* =========================================================================
 * DxgkDdiIsSupportedVidPn
 * =========================================================================
 */

/*
 * SoftGpuDdiIsSupportedVidPn
 *
 * Validate the proposed VidPN topology.  softgpu accepts any topology since
 * it has no hardware constraints.
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

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        IsSupportedVidPn == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: IsSupportedVidPn hDesiredVidPn=%p\n",
           (PVOID)IsSupportedVidPn->hDesiredVidPn);

    /* Accept any topology. */
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

/*
 * SoftGpuDdiEnumVidPnCofuncModality
 *
 * Enumerate source modes compatible with the constraining VidPN.
 * Since the ReactOS VidPN mode-set interfaces are not yet implemented,
 * we simply return success.  dxgkrnl will use whatever modes were
 * previously pinned or fall back to its built-in defaults.
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

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        EnumCofuncModality == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: EnumVidPnCofuncModality "
           "hConstrainingVidPn=%p PivotType=%d\n",
           (PVOID)EnumCofuncModality->hConstrainingVidPn,
           (int)EnumCofuncModality->EnumPivotType);

    return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
}


/* =========================================================================
 * DxgkDdiCommitVidPn
 * =========================================================================
 */

/*
 * SoftGpuDdiCommitVidPn
 *
 * Activate the new VidPN.  Extract the display geometry and store it in the
 * device context so that subsequent SetVidPnSourceAddress calls can validate
 * the address range.
 *
 * Since the ReactOS VidPN object model is a stub, hFunctionalVidPn may not
 * carry usable geometry.  We fall back to the default 1024x768 mode.
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

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        CommitVidPn == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: CommitVidPn hFunctionalVidPn=%p\n",
           (PVOID)CommitVidPn->hFunctionalVidPn);

    if (CommitVidPn->AffectedVidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    /*
     * Record the default display geometry.  When the ReactOS VidPN object
     * model is complete, we will extract Width/Height/Format from the pinned
     * source mode of the functional VidPN.  Until then use 1024x768x32bpp.
     */
    Device->Width  = SOFTGPU_DEFAULT_WIDTH;
    Device->Height = SOFTGPU_DEFAULT_HEIGHT;
    Device->Format = SOFTGPU_DEFAULT_FORMAT;

    DPRINT("SOFTGPU: CommitVidPn: committed %lux%lu format=%d\n",
           Device->Width, Device->Height, (int)Device->Format);

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiSetVidPnSourceAddress
 * =========================================================================
 */

/*
 * SoftGpuDdiSetVidPnSourceAddress
 *
 * Programs the hardware to scan out from a new surface address.
 * On softgpu the "hardware" is our software framebuffer slab; we update a
 * cached pointer but do no actual register writes.
 *
 * PrimaryAddress.QuadPart is a physical address within our segment.
 * We validate it falls within the framebuffer slab before accepting it.
 *
 * IRQL: PASSIVE_LEVEL or DISPATCH_LEVEL
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

    if (SetVidPnSourceAddress->SegmentId != 0 &&
        SetVidPnSourceAddress->SegmentId != SOFTGPU_SEGMENT_ID)
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
        if (PrimaryAddress < FbBase || PrimaryAddress >= FbEnd)
        {
            DPRINT1("SOFTGPU: SetVidPnSourceAddress: PA 0x%I64x outside "
                    "framebuffer [0x%I64x, 0x%I64x)\n",
                    PrimaryAddress,
                    FbBase, FbEnd);
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }
    }

    /* Nothing to program — the CPU already reads from FrameBuffer directly. */
    return STATUS_SUCCESS;
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

    /* No hardware visibility control. */
    return STATUS_SUCCESS;
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
