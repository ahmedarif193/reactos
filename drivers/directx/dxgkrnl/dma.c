/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU command buffer / DMA submission
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * DxgkRender — validates the legacy render entry point without faking work.
 * DxgkPresent — present surface to display via the present queue.
 *
 * The present path validates parameters, builds a DXGKRNL_PRESENT_ENTRY
 * descriptor, and hands it off to the present queue (present.c).  The
 * queue handles VSync synchronisation and dispatches to either the DOD
 * (DxgkDdiPresentDisplayOnly) or full WDDM (DxgkDdiPresent) miniport
 * callback.
 */

#include "dxgkrnl_private.h"
#include "present.h"
#include "vidmm.h"
#include "vidpn.h"

/* ========================================================================
 * DxgkRender
 *
 * D3DKMTRender -- submits a command buffer to the GPU scheduler.
 *
 * A zero-length render carries no GPU work and is acknowledged after context
 * validation.  Non-empty legacy render streams remain unsupported until the
 * complete Render/Patch/Submit contract is implemented.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
NTAPI
DxgkRender(
    _Inout_ D3DKMT_RENDER *pRender)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKRNL_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();

    if (pRender == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pRender->hContext == 0)
        return STATUS_INVALID_HANDLE;

    if (pRender->Flags.RenderKm || pRender->Flags.RenderKmReadback)
        return STATUS_NOT_SUPPORTED;

    Context = DxgkLookupContextByHandle(pRender->hContext, &Adapter, &Device);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    if (Adapter == NULL || Device == NULL)
    {
        DxgkDereferenceContext(Context);
        return STATUS_INVALID_HANDLE;
    }
    if (Adapter->MiniportContext == NULL || Adapter->MiniportContext->UseDodLayout || Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        DxgkDereferenceContext(Context);
        return STATUS_NOT_SUPPORTED;
    }

    Status = STATUS_SUCCESS;
    if (pRender->hDevice != 0 && pRender->hDevice != Device->Handle)
        Status = STATUS_INVALID_HANDLE;
    else if (pRender->CommandLength != 0)
        Status = STATUS_NOT_SUPPORTED;

    if (!NT_SUCCESS(Status))
    {
        DxgkDereferenceContext(Context);
        return Status;
    }

    DXGKRNL_TRACE("DxgkRender: hDevice=0x%X hContext=0x%X CmdBufSize=%u\n",
                  pRender->hDevice,
                  pRender->hContext,
                  pRender->CommandLength);

    /* A zero-length render has no GPU work.  Non-empty legacy render streams
     * stay unsupported until a complete Render/Patch/Submit path exists. */
    pRender->pNewCommandBuffer = NULL;
    pRender->NewCommandBufferSize = 0;
    pRender->pNewAllocationList = NULL;
    pRender->NewAllocationListSize = 0;
    pRender->pNewPatchLocationList = NULL;
    pRender->NewPatchLocationListSize = 0;

    DxgkDereferenceContext(Context);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkPresent
 *
 * D3DKMTPresent -- presents a rendered surface to the display.
 *
 * Validates the present parameters, determines the present type
 * (blt / flip / colour fill), and routes the operation through the
 * present queue in present.c.
 *
 * For display-only adapters with immediate flip interval, the present
 * is executed synchronously (the shadow FB is pushed to the GPU before
 * this function returns).  For full WDDM adapters or VSync-synchronized
 * presents, the operation is queued and processed at VSync time.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
NTAPI
DxgkPresent(
    _Inout_ D3DKMT_PRESENT *pPresent)
{
    PDXGKRNL_ADAPTER         Adapter = NULL;
    PDXGKRNL_DEVICE          Device = NULL;
    PDXGKRNL_CONTEXT         Context = NULL;
    DXGKRNL_PRESENT_ENTRY    Entry;
    ULONG64                  PresentId;
    NTSTATUS                 Status;
    BOOLEAN                  DestinationIsInternal = FALSE;

    PAGED_CODE();

    if (pPresent == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pPresent->hDevice == 0)
        return STATUS_INVALID_HANDLE;

    if (pPresent->Flags.Value == 0 && pPresent->hSource == 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Entry, sizeof(Entry));

    Context = DxgkLookupContextByHandle(pPresent->hContext, &Adapter, &Device);
    if (Context != NULL)
    {
        if (!DxgkReferenceDevice(Device))
        {
            DxgkDereferenceContext(Context);
            return STATUS_DELETE_PENDING;
        }
    }
    else
    {
        Status = DxgkReferenceOwnedDeviceByHandle(pPresent->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Adapter->SchedulingCaps.MultiEngineAware)
        {
            DxgkDereferenceDevice(Device);
            return STATUS_INVALID_HANDLE;
        }
    }

    Entry.Context = Context;
    Entry.Device = Device;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0)
    {
        DxgkpReleasePresentEntry(&Entry);
        return STATUS_DELETE_PENDING;
    }

    Status = DxgkpAcquireSharedSurfaceSnapshot(Adapter, &Entry.SharedSurface);
    if (!NT_SUCCESS(Status))
    {
        DxgkpReleasePresentEntry(&Entry);
        return Status;
    }

    /* --- Build the present entry ---------------------------------------- */

    Entry.hSource        = pPresent->hSource;
    Entry.hDestination   = pPresent->hDestination;
    Entry.Color          = pPresent->Color;
    Entry.FlipInterval   = pPresent->FlipInterval;

    /*
     * Determine the VidPn source.  If RestrictVidPnSource is set, use the
     * caller-specified VidPnSourceId.  Otherwise default to source 0
     * (the primary display).
     */
    if (pPresent->Flags.RestrictVidPnSource)
        Entry.VidPnSourceId = pPresent->VidPnSourceId;
    else
        Entry.VidPnSourceId = 0;

    /* Source rectangle. */
    if (pPresent->Flags.SrcRectValid)
    {
        Entry.SrcRect = pPresent->SrcRect;
    }
    else
    {
        /* Default to the full committed display resolution. */
        Entry.SrcRect.left   = 0;
        Entry.SrcRect.top    = 0;
        Entry.SrcRect.right  = (LONG)Entry.SharedSurface.CommittedWidth;
        Entry.SrcRect.bottom = (LONG)Entry.SharedSurface.CommittedHeight;
    }

    /* Destination rectangle. */
    if (pPresent->Flags.DstRectValid)
    {
        Entry.DstRect = pPresent->DstRect;
    }
    else
    {
        Entry.DstRect.left   = 0;
        Entry.DstRect.top    = 0;
        Entry.DstRect.right  = (LONG)Entry.SharedSurface.CommittedWidth;
        Entry.DstRect.bottom = (LONG)Entry.SharedSurface.CommittedHeight;
    }

    /*
     * Determine the present type from the D3DKMT_PRESENTFLAGS.
     *
     * Priority order (matching Windows behaviour):
     *   1. Flip — hardware page flip (if supported).
     *   2. ColorFill — solid colour fill.
     *   3. Blt — default: blit from source to destination.
     */
    if (pPresent->Flags.Flip)
    {
        Entry.Type = DxgkPresentTypeFlip;
    }
    else if (pPresent->Flags.ColorFill)
    {
        Entry.Type = DxgkPresentTypeColorFill;
    }
    else
    {
        /* Default to blit present (covers Blt flag and unset flags). */
        Entry.Type = DxgkPresentTypeBlt;
    }

    /*
     * User-mode present callers may omit hDestination to mean "present to the
     * current primary". Route those blits to the shared-primary allocation
     * when dxgkrnl already exposed one.
     *
     * Phase-1 callers like dwm.exe can also still issue placeholder presents
     * before they have a real source allocation. Treat those as no-ops rather
     * than faulting the miniport with null allocation handles.
     */
    if (Entry.Type == DxgkPresentTypeBlt)
    {
        if (Entry.hSource == 0 && Entry.hDestination == 0)
        {
            DXGKRNL_WARN("DxgkPresent: rejecting source-less blt present "
                          "hDevice=0x%X hWindow=%p VidPn=%u rect=(%ld,%ld)-(%ld,%ld)\n",
                          pPresent->hDevice,
                          pPresent->hWindow,
                          Entry.VidPnSourceId,
                          Entry.DstRect.left,
                          Entry.DstRect.top,
                          Entry.DstRect.right,
                          Entry.DstRect.bottom);
            DxgkpReleasePresentEntry(&Entry);
            return STATUS_INVALID_PARAMETER;
        }

        if (Entry.hSource != 0 &&
            Entry.hDestination == 0 &&
            Entry.SharedSurface.PrimaryHandle != NULL &&
            DxgkpDeviceOwnsVidPnSource(Device->Handle, Entry.VidPnSourceId))
        {
            /*
             * Only the VidPn source owner may present straight to the primary.
             * A present with no destination from a non-owner is a windowed
             * present: it must succeed but must NOT scan its surface out to the
             * primary (which would paint over the live desktop).  Leaving
             * hDestination == 0 makes the present a no-op-to-screen below.
             * This mirrors the Windows model where DWM owns the primary and
             * composites app presents into their window.
             */
            Entry.hDestination = (D3DKMT_HANDLE)(ULONG_PTR)Entry.SharedSurface.PrimaryHandle;
            DestinationIsInternal = TRUE;
        }
    }

    if (Entry.hSource != 0)
    {
        Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)Entry.hSource, Adapter, Device, &Entry.SourceAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkpReleasePresentEntry(&Entry);
            return Status;
        }
        Status = DxgkVidMmReferenceOpenBinding((HANDLE)(ULONG_PTR)Entry.hSource, Adapter, Device, &Entry.SourceOpenBindingHandle, &Entry.SourceOpenBindingReference);
        if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND)
        {
            DxgkpReleasePresentEntry(&Entry);
            return Status;
        }
    }

    if (Entry.hDestination != 0)
    {
        Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)Entry.hDestination, Adapter, DestinationIsInternal ? NULL : Device, &Entry.DestinationAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkpReleasePresentEntry(&Entry);
            return Status;
        }
        if (!DestinationIsInternal)
        {
            Status = DxgkVidMmReferenceOpenBinding((HANDLE)(ULONG_PTR)Entry.hDestination, Adapter, Device, &Entry.DestinationOpenBindingHandle, &Entry.DestinationOpenBindingReference);
            if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND)
            {
                DxgkpReleasePresentEntry(&Entry);
                return Status;
            }
        }
    }

    Entry.SourceIsSharedPrimary = Entry.SourceAllocation != NULL && Entry.SourceAllocation == Entry.SharedSurface.PrimaryAllocation;
    Entry.SourceIsSharedShadow = Entry.SourceAllocation != NULL && Entry.SourceAllocation == Entry.SharedSurface.ShadowAllocation;
    Entry.DestinationIsSharedPrimary = Entry.DestinationAllocation != NULL && Entry.DestinationAllocation == Entry.SharedSurface.PrimaryAllocation;
    Entry.DestinationIsSharedShadow = Entry.DestinationAllocation != NULL && Entry.DestinationAllocation == Entry.SharedSurface.ShadowAllocation;
    if (!Adapter->MiniportContext->IsDisplayOnlyDriver && Entry.SharedSurface.ShadowFb == NULL && !Entry.SourceIsSharedPrimary && !Entry.SourceIsSharedShadow && !Entry.DestinationIsSharedPrimary && !Entry.DestinationIsSharedShadow)
        DxgkpReleaseSharedSurfaceSnapshot(&Entry.SharedSurface);

    /* --- Submit to the present queue ----------------------------------- */

    Status = DxgkpQueuePresent(Adapter, &Entry, &PresentId);

    if (Status == STATUS_DEVICE_BUSY)
    {
        DXGKRNL_WARN("DxgkPresent: queue full — present dropped "
                     "(PresentId=%llu)\n", PresentId);
    }
    else if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkPresent: DxgkpQueuePresent returned 0x%08lX\n",
                     Status);
    }

    return Status;
}
