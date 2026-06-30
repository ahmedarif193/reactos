/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU command buffer / DMA submission
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * DxgkRender — command buffer submission (stub for DOD, future dxgmms1).
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

/* ========================================================================
 * Helper: find the adapter that owns a given D3DKMT device handle.
 *
 * Walks the global adapter list + per-adapter device lists.  Returns the
 * PDXGKRNL_ADAPTER on success, NULL if the handle is unknown.
 *
 * This is the same pattern used by context.c (DxgkpFindDeviceByHandle)
 * but we keep a local copy to avoid exposing context.c internals.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */

/* Maximum adapters snapshotted (matches context.c). */
#define DMA_MAX_ADAPTERS 16

static PDXGKRNL_ADAPTER
DxgkpFindAdapterForDevice(
    _In_ D3DKMT_HANDLE hDevice)
{
    PDXGKRNL_ADAPTER Snapshot[DMA_MAX_ADAPTERS];
    ULONG Count, i;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    /* Snapshot the global adapter list under the spinlock. */
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);
    Count = 0;
    for (Entry  = DxgkAdapterGlobalListHead.Flink;
         Entry != &DxgkAdapterGlobalListHead && Count < DMA_MAX_ADAPTERS;
         Entry  = Entry->Flink)
    {
        Snapshot[Count++] = CONTAINING_RECORD(Entry, DXGKRNL_ADAPTER,
                                               GlobalAdapterListEntry);
    }
    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);

    /* Search each adapter's device list for the handle. */
    for (i = 0; i < Count; ++i)
    {
        PDXGKRNL_ADAPTER Adapter = Snapshot[i];
        PLIST_ENTRY DevEntry;

        ExAcquireFastMutex(&Adapter->AdapterMutex);

        for (DevEntry  = Adapter->DeviceListHead.Flink;
             DevEntry != &Adapter->DeviceListHead;
             DevEntry  = DevEntry->Flink)
        {
            PDXGKRNL_DEVICE Device = CONTAINING_RECORD(DevEntry,
                                                        DXGKRNL_DEVICE,
                                                        DeviceListEntry);
            if (Device->Handle == hDevice)
            {
                ExReleaseFastMutex(&Adapter->AdapterMutex);
                return Adapter;
            }
        }

        ExReleaseFastMutex(&Adapter->AdapterMutex);
    }

    return NULL;
}

/*
 * DxgkpGetFirstAdapter
 *
 * Returns the first adapter in the global list.  Used as a fallback when
 * no device handle is available (e.g., for present calls that only carry
 * a window handle).
 *
 * IRQL: PASSIVE_LEVEL
 */
static PDXGKRNL_ADAPTER
DxgkpGetFirstAdapter(VOID)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);

    if (!IsListEmpty(&DxgkAdapterGlobalListHead))
    {
        Adapter = CONTAINING_RECORD(DxgkAdapterGlobalListHead.Flink,
                                     DXGKRNL_ADAPTER,
                                     GlobalAdapterListEntry);
    }

    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);
    return Adapter;
}

/* ========================================================================
 * DxgkRender
 *
 * D3DKMTRender -- submits a command buffer to the GPU scheduler.
 *
 * For display-only adapters there is no GPU command stream, so we
 * validate parameters and return success with a zero-length output
 * command buffer.  Full WDDM adapters need dxgmms1 scheduler integration.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
NTAPI
DxgkRender(
    _Inout_ D3DKMT_RENDER *pRender)
{
    PAGED_CODE();

    if (pRender == NULL)
        return STATUS_INVALID_PARAMETER;

    {
        PDXGKRNL_ADAPTER Adapter = NULL;
        PDXGKRNL_CONTEXT Context;

        if (pRender->hContext == 0)
            return STATUS_INVALID_HANDLE;

        Context = DxgkLookupContextByHandle(pRender->hContext,
                                            &Adapter,
                                            NULL);
        if (Context == NULL || Adapter == NULL)
            return STATUS_INVALID_HANDLE;

        if ((Adapter->MiniportContext == NULL ||
             !Adapter->MiniportContext->IsDisplayOnlyDriver) &&
            pRender->CommandLength != 0)
        {
            return STATUS_NOT_SUPPORTED;
        }
    }

    if (pRender->Flags.RenderKm || pRender->Flags.RenderKmReadback)
    {
        return STATUS_NOT_SUPPORTED;
    }

    DXGKRNL_TRACE("DxgkRender: hDevice=0x%X hContext=0x%X CmdBufSize=%u\n",
                  pRender->hDevice,
                  pRender->hContext,
                  pRender->CommandLength);

    /*
     * For display-only DOD adapters: acknowledge the render call without
     * submitting anything.  Return a zero-length new command buffer to
     * indicate no further work is queued.
     *
     * TODO (Priority 3): Route through dxgmms1 scheduler for real GPU work.
     */
    pRender->pNewCommandBuffer = NULL;
    pRender->NewCommandBufferSize = 0;
    pRender->pNewAllocationList = NULL;
    pRender->NewAllocationListSize = 0;
    pRender->pNewPatchLocationList = NULL;
    pRender->NewPatchLocationListSize = 0;

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
    PDXGKRNL_ADAPTER         Adapter;
    DXGKRNL_PRESENT_ENTRY    Entry;
    ULONG64                  PresentId;
    NTSTATUS                 Status;

    PAGED_CODE();

    if (pPresent == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pPresent->hDevice != 0 &&
        DxgkLookupDeviceByHandle(pPresent->hDevice, NULL) == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    if (pPresent->Flags.Value == 0 && pPresent->hSource == 0)
        return STATUS_INVALID_PARAMETER;

    /* --- Resolve the adapter from the device handle --------------------- */

    Adapter = DxgkpFindAdapterForDevice(pPresent->hDevice);
    if (Adapter == NULL)
    {
        /*
         * The device handle may be zero for some runtime paths (e.g.,
         * GDI presents that only carry a window handle).  Fall back to
         * the first adapter in the global list.
         */
        Adapter = DxgkpGetFirstAdapter();
        if (Adapter == NULL)
        {
            DXGKRNL_WARN("DxgkPresent: no adapter found for hDevice=0x%X\n",
                         pPresent->hDevice);
            return STATUS_DEVICE_NOT_READY;
        }
    }

    /*
     * If the present queue is not initialised (adapter not fully started),
     * fall through to the legacy behaviour: acknowledge the present and
     * let the periodic timer in display.c handle pixel output.
     */
    if (Adapter->PresentQueues == NULL)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    /* --- Build the present entry ---------------------------------------- */

    RtlZeroMemory(&Entry, sizeof(Entry));

    Entry.hDevice        = pPresent->hDevice;
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
        Entry.SrcRect.right  = (LONG)Adapter->CommittedWidth;
        Entry.SrcRect.bottom = (LONG)Adapter->CommittedHeight;
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
        Entry.DstRect.right  = (LONG)Adapter->CommittedWidth;
        Entry.DstRect.bottom = (LONG)Adapter->CommittedHeight;
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
            return STATUS_INVALID_PARAMETER;
        }

        if (Entry.hSource != 0 &&
            Entry.hDestination == 0 &&
            Adapter->SharedPrimaryAllocationHandle != NULL)
        {
            Entry.hDestination =
                (D3DKMT_HANDLE)(ULONG_PTR)Adapter->SharedPrimaryAllocationHandle;
        }
    }

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
