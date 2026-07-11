/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Swap chain / present infrastructure
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * Overview
 * --------
 * Implements the per-VidPnSource present queue that sits between the
 * D3DKMTPresent user-mode entry point (dma.c) and the miniport DDI
 * present callbacks.
 *
 * Two present execution paths:
 *
 *   1. Display-only (DOD) adapters:
 *      Present = copy the shadow framebuffer to the GPU via
 *      DxgkDdiPresentDisplayOnly.  This is the same mechanism used by
 *      the existing periodic present timer in display.c, but driven
 *      on-demand by the present queue rather than (only) by a timer.
 *
 *   2. Full WDDM adapters:
 *      Present = call DxgkDdiPresent on the miniport with the
 *      appropriate blit/flip/colour-fill flags.  The miniport writes
 *      a DMA packet that the GPU scheduler (dxgmms1, future) submits
 *      to the hardware.  Until dxgmms1 is available, the DxgkDdiPresent
 *      call is made directly for immediate-mode presents.
 *
 * VSync synchronisation:
 *   Each queue tracks a VBlankCount incremented by DxgkpNotifyVSync
 *   (called from the CRTC_VSYNC interrupt path).  Flip presents with
 *   FlipInterval > 0 wait until VBlankCount reaches the target before
 *   being dequeued.  Immediate presents bypass this check.
 *
 * x86/amd64 notes
 * ---------------
 * The queue spinlock is acquired at DISPATCH_LEVEL (via KeAcquireSpinLock)
 * because the VSync DPC path must safely increment VBlankCount.  On
 * x86/amd64, DISPATCH_LEVEL spin locks use CLI/STI for IRQL management,
 * which is architecturally standard and imposes no special constraints
 * beyond the usual "no paged access while holding a spinlock" rule.
 *
 * The 64-bit VBlankCount and NextPresentId fields use InterlockedIncrement64
 * for atomic updates.  On x86-32, InterlockedIncrement64 compiles to a
 * LOCK CMPXCHG8B loop which is slightly slower than the native 64-bit
 * LOCK INC on amd64, but both are correct.
 */

#include "dxgkrnl_private.h"
#include "vidmm.h"
#include "vidsch.h"
#include "present.h"

#define DXGK_PRESENT_EXEC_LOG_LIMIT  32
#define DXGK_PRESENT_EXEC_SLOW_US    5000ULL
#define DXGK_PRESENT_IMMEDIATE_DMA_BYTES  0x1000
#define DXGK_PRESENT_TRACE_BURST     8
#define DXGK_PRESENT_TRACE_PERIOD    128

static volatile LONG g_DodPresentTraceCount = 0;
static volatile LONG g_SharedPrimaryPresentTraceCount = 0;
static volatile LONG g_PresentOpenTraceCount = 0;
static volatile LONG g_SharedPrimaryScanoutTraceCount = 0;

FORCEINLINE ULONGLONG
DxgkpPresentTraceNow100ns(VOID)
{
    return KeQueryInterruptTime();
}

FORCEINLINE ULONGLONG
DxgkpPresentTraceElapsedUs(
    _In_ ULONGLONG Start100ns)
{
    ULONGLONG End100ns = KeQueryInterruptTime();

    if (End100ns <= Start100ns)
        return 0;

    return (End100ns - Start100ns) / 10ULL;
}

FORCEINLINE BOOLEAN
DxgkpShouldTraceOrdinal(
    _In_ LONG Ordinal)
{
    return (Ordinal <= DXGK_PRESENT_TRACE_BURST ||
            ((Ordinal % DXGK_PRESENT_TRACE_PERIOD) == 0));
}

static ULONG
DxgkpSurfaceCopyPitch(
    _In_opt_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG DefaultPitch)
{
    SIZE_T CandidatePitch;

    if (DefaultPitch != 0)
        return DefaultPitch;

    if (Allocation != NULL &&
        Height != 0 &&
        Allocation->Size >= (SIZE_T)Width * sizeof(ULONG) &&
        (Allocation->Size % Height) == 0)
    {
        CandidatePitch = Allocation->Size / Height;
        if (CandidatePitch >= (SIZE_T)Width * sizeof(ULONG) &&
            CandidatePitch <= MAXULONG)
        {
            return (ULONG)CandidatePitch;
        }
    }

    return Width * sizeof(ULONG);
}

static NTSTATUS
DxgkpCopyShadowToSharedPrimary(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION PrimaryAllocation)
{
    PBYTE DestinationVa = NULL;
    PBYTE SourceVa;
    ULONG Width;
    ULONG Height;
    ULONG SourcePitch;
    ULONG DestinationPitch;
    ULONG RowBytes;
    ULONG Row;
    NTSTATUS Status;

    if (Adapter == NULL || PrimaryAllocation == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Adapter->ShadowFb == NULL || Adapter->ShadowFbPitch == 0)
        return STATUS_INVALID_PARAMETER;

    Width = Adapter->SharedPrimaryWidth;
    Height = Adapter->SharedPrimaryHeight;

    if (Adapter->SharedShadowWidth != 0 && Adapter->SharedShadowWidth < Width)
        Width = Adapter->SharedShadowWidth;
    if (Adapter->SharedShadowHeight != 0 && Adapter->SharedShadowHeight < Height)
        Height = Adapter->SharedShadowHeight;

    if (Width == 0 || Height == 0)
        return STATUS_INVALID_PARAMETER;

    SourcePitch = Adapter->ShadowFbPitch;
    RowBytes = Width * sizeof(ULONG);
    if (SourcePitch < RowBytes)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmMapAllocationCpu(PrimaryAllocation, (PVOID *)&DestinationVa);
    if (!NT_SUCCESS(Status))
        return Status;

    DestinationPitch = DxgkpSurfaceCopyPitch(PrimaryAllocation,
                                             Adapter->SharedPrimaryWidth,
                                             Adapter->SharedPrimaryHeight,
                                             0);
    if (DestinationPitch < RowBytes)
        return STATUS_INVALID_PARAMETER;

    SourceVa = (PBYTE)Adapter->ShadowFb;
    for (Row = 0; Row < Height; ++Row)
    {
        RtlCopyMemory(DestinationVa + ((SIZE_T)Row * DestinationPitch),
                      SourceVa + ((SIZE_T)Row * SourcePitch),
                      RowBytes);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpOpenPresentAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_HANDLE AllocationHandle,
    _In_ BOOLEAN ReadOnly,
    _Out_ PHANDLE DeviceSpecificHandle)
{
    PDXGKVMM_ALLOCATION Allocation;
    DXGK_OPENALLOCATIONINFO OpenInfo;
    DXGKARG_OPENALLOCATION OpenArgs;
    NTSTATUS Status;

    if (Adapter == NULL || Device == NULL || DeviceSpecificHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    *DeviceSpecificHandle = NULL;

    if (AllocationHandle == 0)
        return STATUS_SUCCESS;

    if (DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation) == NULL)
        return STATUS_NOT_SUPPORTED;

    Allocation = DxgkVidMmHandleToAllocation((HANDLE)(ULONG_PTR)AllocationHandle);
    if (Allocation == NULL)
        return STATUS_INVALID_HANDLE;

    RtlZeroMemory(&OpenInfo, sizeof(OpenInfo));
    RtlZeroMemory(&OpenArgs, sizeof(OpenArgs));

    OpenInfo.hAllocation = AllocationHandle;

    OpenArgs.NumAllocations = 1;
    OpenArgs.pOpenAllocation = &OpenInfo;
    OpenArgs.pPrivateDriverData = NULL;
    OpenArgs.PrivateDriverDataSize = 0;
    OpenArgs.hResource = (Allocation->Resource != NULL) ?
                         Allocation->Resource->MiniportHandle : NULL;
    OpenArgs.ReadOnly = ReadOnly;

    Status = DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation)(
                 Device->hMiniportDevice,
                 &OpenArgs);
    if (!NT_SUCCESS(Status))
        return Status;

    if (OpenInfo.hDeviceSpecificAllocation == NULL)
        return STATUS_INVALID_HANDLE;

    *DeviceSpecificHandle = OpenInfo.hDeviceSpecificAllocation;
    return STATUS_SUCCESS;
}

static VOID
DxgkpClosePresentAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_opt_ HANDLE DeviceSpecificHandle)
{
    DXGKARG_CLOSEALLOCATION CloseArgs;
    HANDLE OpenHandle;

    if (Adapter == NULL ||
        Device == NULL ||
        DeviceSpecificHandle == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) == NULL)
    {
        return;
    }

    OpenHandle = DeviceSpecificHandle;

    RtlZeroMemory(&CloseArgs, sizeof(CloseArgs));
    CloseArgs.NumAllocations = 1;
    CloseArgs.pOpenHandleList = &OpenHandle;

    DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation)(
        Device->hMiniportDevice,
        &CloseArgs);
}

/*
 * Program the base-plane flip through the multi-plane overlay DDI when the
 * miniport implements it (documented Windows behavior: MPO-capable drivers
 * receive flips as one-plane MPO configurations).  Returns STATUS_NOT_-
 * SUPPORTED to let the caller fall back to the legacy SetVidPnSourceAddress.
 */
static NTSTATUS
DxgkpProgramScanoutViaMpo(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ LARGE_INTEGER PrimaryAddress)
{
    DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY MpoArgs;
    DXGK_MULTIPLANE_OVERLAY_PLANE Plane;
    PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY PfnSetMpo;
    NTSTATUS Status;

    PfnSetMpo = DXGK_CB_FULL(Adapter,
                             DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay);
    if (PfnSetMpo == NULL ||
        Adapter->CommittedWidth == 0 ||
        Adapter->CommittedHeight == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&Plane, sizeof(Plane));
    Plane.LayerIndex = 0;
    Plane.Enabled = TRUE;
    Plane.AllocationSegment = Allocation->SegmentId;
    Plane.AllocationAddress.QuadPart = PrimaryAddress.QuadPart;
    Plane.hAllocation = Allocation->MiniportHandle;
    Plane.PlaneAttributes.SrcRect.right = (LONG)Adapter->CommittedWidth;
    Plane.PlaneAttributes.SrcRect.bottom = (LONG)Adapter->CommittedHeight;
    Plane.PlaneAttributes.DstRect = Plane.PlaneAttributes.SrcRect;
    Plane.PlaneAttributes.ClipRect = Plane.PlaneAttributes.SrcRect;

    RtlZeroMemory(&MpoArgs, sizeof(MpoArgs));
    MpoArgs.VidPnSourceId = VidPnSourceId;
    MpoArgs.PlaneCount = 1;
    MpoArgs.pPlanes = &Plane;
    MpoArgs.Flags.FlipImmediate = 1;

    _SEH2_TRY
    {
        Status = PfnSetMpo(Adapter->MiniportDeviceContext, &MpoArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static NTSTATUS
DxgkpProgramSharedPrimaryScanout(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DKMT_HANDLE AllocationHandle,
    _In_ ULONG64 PresentId)
{
    DXGKARG_SETVIDPNSOURCEADDRESS SetSourceAddress;
    LARGE_INTEGER PrimaryAddress;
    LONG TraceSeq;
    NTSTATUS Status;

    if (Adapter == NULL ||
        Allocation == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver ||
        DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpProgramSharedPrimaryScanout: aperture map failed "
                     "0x%08lX for hAllocation=0x%X\n",
                     Status,
                     AllocationHandle);
        return Status;
    }

    PrimaryAddress = DxgkVidMmGetAllocationPrimaryAddress(Allocation);

    /* MPO-capable miniports get the flip as a one-plane configuration. */
    Status = DxgkpProgramScanoutViaMpo(Adapter, Allocation, VidPnSourceId,
                                       PrimaryAddress);
    if (NT_SUCCESS(Status))
    {
        if (DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility) != NULL)
        {
            DXGKARG_SETVIDPNSOURCEVISIBILITY MpoVisibility;

            RtlZeroMemory(&MpoVisibility, sizeof(MpoVisibility));
            MpoVisibility.VidPnSourceId = VidPnSourceId;
            MpoVisibility.Visible = TRUE;

            DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility)(
                Adapter->MiniportDeviceContext,
                &MpoVisibility);
        }
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&SetSourceAddress, sizeof(SetSourceAddress));
    SetSourceAddress.VidPnSourceId = VidPnSourceId;
    SetSourceAddress.hAllocation = Allocation->MiniportHandle;
    SetSourceAddress.PrimaryAddress = PrimaryAddress;
    SetSourceAddress.SegmentId = Allocation->SegmentId;
    SetSourceAddress.Flags.FlipImmediate = 1;

    TraceSeq = InterlockedIncrement(&g_SharedPrimaryScanoutTraceCount);
    if (DxgkpShouldTraceOrdinal(TraceSeq))
    {
        DXGKRNL_TRACE("DxgkpProgramSharedPrimaryScanout: seq=%ld PresentId=%llu "
                      "VidPnSrc=%u alloc=0x%X miniport=%p seg=%u addr=0x%I64x\n",
                      TraceSeq,
                      PresentId,
                      VidPnSourceId,
                      AllocationHandle,
                      Allocation->MiniportHandle,
                      Allocation->SegmentId,
                      PrimaryAddress.QuadPart);
    }

    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress)(
                     Adapter->MiniportDeviceContext,
                     &SetSourceAddress);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        DXGKRNL_ERR("DxgkpProgramSharedPrimaryScanout: "
                    "DxgkDdiSetVidPnSourceAddress FAULTED 0x%08lX\n",
                    Status);
    }
    _SEH2_END;

    if (NT_SUCCESS(Status) &&
        DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility) != NULL)
    {
        DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;

        RtlZeroMemory(&Visibility, sizeof(Visibility));
        Visibility.VidPnSourceId = VidPnSourceId;
        Visibility.Visible = TRUE;

        DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility)(
            Adapter->MiniportDeviceContext,
            &Visibility);
    }

    return Status;
}

static NTSTATUS
DxgkpRefreshSharedPrimaryScanout(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PRESENT_ENTRY Entry,
    _Out_ PBOOLEAN Handled)
{
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status;

    if (Handled == NULL)
        return STATUS_INVALID_PARAMETER;

    *Handled = FALSE;

    if (Adapter == NULL ||
        Entry == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver ||
        DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress) == NULL ||
        Entry->Type != DxgkPresentTypeBlt ||
        Entry->hSource == 0 ||
        Entry->hSource != Entry->hDestination ||
        Adapter->SharedPrimaryAllocationHandle == NULL ||
        (HANDLE)(ULONG_PTR)Entry->hSource != Adapter->SharedPrimaryAllocationHandle)
    {
        return STATUS_NOT_SUPPORTED;
    }

    Allocation = DxgkVidMmHandleToAllocation((HANDLE)(ULONG_PTR)Entry->hSource);
    if (Allocation == NULL || Allocation->Adapter != Adapter)
        return STATUS_INVALID_HANDLE;

    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpRefreshSharedPrimaryScanout: aperture map failed "
                     "0x%08lX for hAllocation=0x%X\n",
                     Status,
                     Entry->hSource);
        return Status;
    }

    Status = DxgkpCopyShadowToSharedPrimary(Adapter, Allocation);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpRefreshSharedPrimaryScanout: shadow copy failed "
                     "0x%08lX for hAllocation=0x%X\n",
                     Status,
                     Entry->hSource);
        return Status;
    }

    Status = DxgkpProgramSharedPrimaryScanout(Adapter,
                                              Allocation,
                                              Adapter->SharedPrimaryVidPnSourceId,
                                              Entry->hSource,
                                              Entry->PresentId);

    *Handled = TRUE;
    return Status;
}

/* ========================================================================
 * DxgkPresentInit
 *
 * Allocates and initialises the present queue array for the adapter.
 * One DXGKRNL_PRESENT_QUEUE per VidPn source.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
DxgkPresentInit(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG i;
    ULONG NumSources;
    PDXGKRNL_PRESENT_QUEUE Queues;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    NumSources = Adapter->NumberOfVideoPresentSources;
    if (NumSources == 0)
        NumSources = 1; /* At least one source for the primary display. */

    DXGKRNL_TRACE("DxgkPresentInit: allocating %lu present queue(s)\n",
                  NumSources);

    Queues = (PDXGKRNL_PRESENT_QUEUE)ExAllocatePoolWithTag(
                 NonPagedPool,
                 NumSources * sizeof(DXGKRNL_PRESENT_QUEUE),
                 TAG_DXGK_PRESENT);

    if (Queues == NULL)
    {
        DXGKRNL_ERR("DxgkPresentInit: pool alloc failed (%lu x %Iu bytes)\n",
                    NumSources, sizeof(DXGKRNL_PRESENT_QUEUE));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Queues, NumSources * sizeof(DXGKRNL_PRESENT_QUEUE));

    for (i = 0; i < NumSources; i++)
    {
        Queues[i].VidPnSourceId = i;
        Queues[i].Head          = 0;
        Queues[i].Tail          = 0;
        Queues[i].Count         = 0;
        Queues[i].NextPresentId = 1; /* IDs start at 1; 0 = invalid. */
        Queues[i].VBlankCount   = 0;
        Queues[i].LastPresentVBlank = 0;
        Queues[i].Adapter       = Adapter;
        KeInitializeSpinLock(&Queues[i].QueueLock);
    }

    Adapter->PresentQueues     = Queues;
    Adapter->PresentQueueCount = NumSources;

    DXGKRNL_TRACE("DxgkPresentInit: %lu queue(s) ready at %p\n",
                  NumSources, Queues);

    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkPresentTeardown
 *
 * Frees the present queue array.  Any queued presents are silently
 * discarded (the adapter is stopping, so there is no display to
 * present to).
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
VOID
DxgkPresentTeardown(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();

    if (Adapter == NULL)
        return;

    if (Adapter->PresentQueues != NULL)
    {
        ULONG i;

        for (i = 0; i < Adapter->PresentQueueCount; i++)
        {
            PDXGKRNL_PRESENT_QUEUE Queue =
                &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[i];

            if (Queue->Count > 0)
            {
                DXGKRNL_WARN("DxgkPresentTeardown: VidPnSource %lu — "
                             "discarding %lu queued present(s)\n",
                             Queue->VidPnSourceId, Queue->Count);
            }
        }

        ExFreePoolWithTag(Adapter->PresentQueues, TAG_DXGK_PRESENT);
        Adapter->PresentQueues     = NULL;
        Adapter->PresentQueueCount = 0;

        DXGKRNL_TRACE("DxgkPresentTeardown: present queues freed\n");
    }
}

/* ========================================================================
 * DxgkpExecuteDodPresent  (private)
 *
 * Executes a present on a display-only (DOD) adapter by calling
 * DxgkDdiPresentDisplayOnly.  This pushes the shadow framebuffer
 * contents to the GPU.
 *
 * The logic mirrors DxgkpPresentShadowFb in display.c but is driven
 * by the present queue rather than the periodic timer.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
static NTSTATUS
DxgkpExecuteDodPresent(
    _In_ PDXGKRNL_ADAPTER        Adapter,
    _In_ PDXGKRNL_PRESENT_ENTRY  Entry)
{
    typedef NTSTATUS (APIENTRY *PFN_PRESENT_DISPLAY_ONLY)(
        _In_ PVOID MiniportDeviceContext,
        _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly);

    PFN_PRESENT_DISPLAY_ONLY PfnPresent;
    DXGKARG_PRESENT_DISPLAYONLY PresentArgs;
    RECT DirtyRect;
    ULONGLONG Start100ns;
    ULONGLONG ElapsedUs;
    LONG ShadowPitch;
    LONG TraceSeq;
    NTSTATUS Status;

    if (Adapter->ShadowFb == NULL || !Adapter->VidPnCommitted)
    {
        DXGKRNL_TRACE("DxgkpExecuteDodPresent: no shadow FB or VidPn not "
                      "committed\n");
        return STATUS_DEVICE_NOT_READY;
    }

    /*
     * Retrieve the DxgkDdiPresentDisplayOnly callback.
     * Same offset logic as display.c DxgkpPresentShadowFb.
     */
    if (Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        SIZE_T Offset = FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA,
                                     DxgkDdiPresentDisplayOnly);
        if (Adapter->MiniportContext->InitDataSize >= Offset + sizeof(PVOID))
        {
            PfnPresent = *(PFN_PRESENT_DISPLAY_ONLY *)
                ((PUCHAR)&Adapter->MiniportContext->InitData + Offset);
        }
        else
        {
            PfnPresent = NULL;
        }
    }
    else
    {
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
        SIZE_T Offset = FIELD_OFFSET(DRIVER_INITIALIZATION_DATA,
                                     DxgkDdiPresentDisplayOnly);

        if (Adapter->MiniportContext->InitDataSize >= Offset + sizeof(PVOID))
        {
            PfnPresent = *(PFN_PRESENT_DISPLAY_ONLY *)
                ((PUCHAR)&Adapter->MiniportContext->InitData + Offset);
        }
        else
        {
            PfnPresent = NULL;
        }
#else
        /* Win7 full WDDM miniports do not expose PresentDisplayOnly. */
        PfnPresent = NULL;
#endif
    }

    if (PfnPresent == NULL)
        return STATUS_NOT_SUPPORTED;

    /* Validate the function pointer against a known good callback. */
    {
        PVOID KnownGoodCb = (PVOID)(Adapter->MiniportContext->UseDodLayout
            ? Adapter->MiniportContext->InitData.dod.DxgkDdiStartDevice
            : Adapter->MiniportContext->InitData.s.DxgkDdiStartDevice);

        if (KnownGoodCb != NULL)
        {
            ULONG_PTR Delta = ((ULONG_PTR)PfnPresent > (ULONG_PTR)KnownGoodCb)
                ? (ULONG_PTR)PfnPresent - (ULONG_PTR)KnownGoodCb
                : (ULONG_PTR)KnownGoodCb - (ULONG_PTR)PfnPresent;

            if (Delta > 0x100000)
            {
                DXGKRNL_WARN("DxgkpExecuteDodPresent: PfnPresent=%p garbage "
                             "(StartDevice=%p delta=0x%IX)\n",
                             PfnPresent, KnownGoodCb, Delta);
                return STATUS_NOT_SUPPORTED;
            }
        }
    }

    /* Build a full-screen dirty rect. */
    DirtyRect.left   = 0;
    DirtyRect.top    = 0;
    DirtyRect.right  = (LONG)Adapter->CommittedWidth;
    DirtyRect.bottom = (LONG)Adapter->CommittedHeight;

    ASSERT(Adapter->ShadowFbPitch != 0);
    ShadowPitch = (LONG)(Adapter->ShadowFbPitch != 0 ?
                         Adapter->ShadowFbPitch :
                         (Adapter->CommittedWidth * 4));

    RtlZeroMemory(&PresentArgs, sizeof(PresentArgs));
    PresentArgs.VidPnSourceId = Entry->VidPnSourceId;
    PresentArgs.pSource       = Adapter->ShadowFb;
    PresentArgs.BytesPerPixel = 4;
    PresentArgs.Pitch         = ShadowPitch;
    PresentArgs.Flags.Value   = 0;
    PresentArgs.NumMoves      = 0;
    PresentArgs.pMoves        = NULL;
    PresentArgs.NumDirtyRects = 1;
    PresentArgs.pDirtyRect    = &DirtyRect;
    PresentArgs.pfnPresentDisplayOnlyProgress = NULL;

    Start100ns = DxgkpPresentTraceNow100ns();
    Status = PfnPresent(Adapter->MiniportDeviceContext, &PresentArgs);
    ElapsedUs = DxgkpPresentTraceElapsedUs(Start100ns);
    TraceSeq = InterlockedIncrement(&g_DodPresentTraceCount);

    if (TraceSeq <= DXGK_PRESENT_EXEC_LOG_LIMIT ||
        ElapsedUs >= DXGK_PRESENT_EXEC_SLOW_US)
    {
        DXGKRNL_TRACE("DxgkpExecuteDodPresent: seq=%ld PresentId=%llu "
                      "VidPnSrc=%u status=0x%08lX dur=%I64u us size=%ux%u\n",
                      TraceSeq,
                      Entry->PresentId,
                      Entry->VidPnSourceId,
                      Status,
                      ElapsedUs,
                      Adapter->CommittedWidth,
                      Adapter->CommittedHeight);
    }

    return Status;
}

/* ========================================================================
 * DxgkpExecuteCpuPresent  (private)
 *
 * Handles CPU-backed blit presents created by the compatibility D3DKMT path.
 * These allocations have dxgkrnl backing memory but no miniport allocation
 * handle, so calling the full miniport present DDI would fail before a real
 * scheduler submission can exist.
 * ====================================================================== */
static PVOID
DxgkpGetCpuAllocationAddress(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    if (Allocation == NULL)
        return NULL;
    if (Allocation->CpuAddress != NULL)
        return Allocation->CpuAddress;
    if (Allocation->SystemMemory != NULL)
        return Allocation->SystemMemory;
    return NULL;
}

static NTSTATUS
DxgkpExecuteCpuPresent(
    _In_  PDXGKRNL_ADAPTER        Adapter,
    _In_  PDXGKRNL_PRESENT_ENTRY  Entry,
    _Out_ PBOOLEAN                Handled)
{
    PDXGKVMM_ALLOCATION SourceAllocation;
    PDXGKVMM_ALLOCATION DestinationAllocation = NULL;
    PVOID SourceAddress;
    PVOID DestinationAddress = NULL;
    SIZE_T BytesToCopy;
    NTSTATUS Status;

    if (Handled == NULL)
        return STATUS_INVALID_PARAMETER;

    *Handled = FALSE;

    if (Adapter == NULL ||
        Entry == NULL ||
        Entry->Type != DxgkPresentTypeBlt ||
        Entry->hSource == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    SourceAllocation = DxgkVidMmHandleToAllocation((HANDLE)(ULONG_PTR)Entry->hSource);
    if (SourceAllocation == NULL || SourceAllocation->Adapter != Adapter)
        return STATUS_INVALID_HANDLE;

    SourceAddress = DxgkpGetCpuAllocationAddress(SourceAllocation);
    if (SourceAddress == NULL)
        return STATUS_NOT_SUPPORTED;

    *Handled = TRUE;

    if (Entry->hDestination != 0)
    {
        DestinationAllocation =
            DxgkVidMmHandleToAllocation((HANDLE)(ULONG_PTR)Entry->hDestination);
        if (DestinationAllocation == NULL || DestinationAllocation->Adapter != Adapter)
            return STATUS_INVALID_HANDLE;

        DestinationAddress = DxgkpGetCpuAllocationAddress(DestinationAllocation);
        if (DestinationAddress == NULL)
        {
            Status = DxgkVidMmEnsureAllocationApertureMapped(DestinationAllocation);
            if (!NT_SUCCESS(Status))
                return Status;

            DestinationAddress = DxgkpGetCpuAllocationAddress(DestinationAllocation);
        }

        if (DestinationAddress == NULL)
            return STATUS_NOT_SUPPORTED;

        BytesToCopy = (SourceAllocation->Size < DestinationAllocation->Size) ?
                      SourceAllocation->Size : DestinationAllocation->Size;
        if (BytesToCopy != 0 && SourceAddress != DestinationAddress)
            RtlCopyMemory(DestinationAddress, SourceAddress, BytesToCopy);

        if (Adapter->SharedPrimaryAllocationHandle != NULL &&
            (HANDLE)(ULONG_PTR)Entry->hDestination == Adapter->SharedPrimaryAllocationHandle)
        {
            Status = DxgkpProgramSharedPrimaryScanout(Adapter,
                                                      DestinationAllocation,
                                                      Entry->VidPnSourceId,
                                                      Entry->hDestination,
                                                      Entry->PresentId);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("DxgkpExecuteCpuPresent: shared-primary scanout "
                             "refresh failed 0x%08lX dst=0x%X\n",
                             Status,
                             Entry->hDestination);
                return Status;
            }
        }
    }

    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkpExecuteFullPresent  (private)
 *
 * Executes a present on a full WDDM adapter by calling DxgkDdiPresent
 * on the miniport.  The miniport builds a DMA packet for the GPU
 * scheduler.
 *
 * NOTE: Without dxgmms1 scheduler integration, this is a simplified
 * direct call.  The DMA buffer is stack-allocated (small) and the
 * present is executed synchronously.  This is sufficient for initial
 * bring-up but will need rework when the GPU scheduler is available.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
static NTSTATUS
DxgkpExecuteFullPresent(
    _In_ PDXGKRNL_ADAPTER        Adapter,
    _In_ PDXGKRNL_PRESENT_ENTRY  Entry)
{
    DXGKARG_PRESENT PresentArgs;
    DXGK_ALLOCATIONLIST PresentAllocationList[DXGK_PRESENT_DESTINATION_INDEX + 1];
    D3DDDI_ALLOCATIONLIST SubmitAllocationList[DXGK_PRESENT_DESTINATION_INDEX + 1];
    D3DDDI_PATCHLOCATIONLIST PatchLocationList[DXGK_PRESENT_DESTINATION_INDEX];
    RECT DstSubRect;
    HANDLE SourceDeviceSpecificHandle = NULL;
    HANDLE DestinationDeviceSpecificHandle = NULL;
    BOOLEAN CloseSourceHandle = FALSE;
    BOOLEAN CloseDestinationHandle = FALSE;
    PDXGKRNL_DEVICE Device;
    HANDLE MiniportPresentContext;
    PVOID DmaBuffer = NULL;
    PVOID DmaBufferPrivateData = NULL;
    ULONG SubmissionFenceId = 0;
    UINT DmaBytesUsed = 0;
    HANDLE TrackedOpenHandles[2];
    UINT TrackedOpenHandleCount = 0;
    BOOLEAN HandlesTracked = FALSE;
    BOOLEAN RefreshSharedPrimaryOnRetire = FALSE;
    LONG TraceSeq;
    NTSTATUS Status;
    BOOLEAN Handled;

    Status = DxgkpExecuteCpuPresent(Adapter, Entry, &Handled);
    if (Handled)
        return Status;

    /*
     * Check that the miniport provides DxgkDdiPresent.
     * DOD drivers do not have this callback.
     */
    if (Adapter->MiniportContext->InitData.s.DxgkDdiPresent == NULL)
    {
        DXGKRNL_TRACE("DxgkpExecuteFullPresent: no DxgkDdiPresent DDI\n");
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&PresentArgs, sizeof(PresentArgs));
    RtlZeroMemory(PresentAllocationList, sizeof(PresentAllocationList));
    RtlZeroMemory(SubmitAllocationList, sizeof(SubmitAllocationList));

    Device = DxgkLookupDeviceByHandle(Entry->hDevice, NULL);
    if (Device == NULL)
    {
        DXGKRNL_WARN("DxgkpExecuteFullPresent: invalid hDevice=0x%X\n",
                     Entry->hDevice);
        return STATUS_INVALID_HANDLE;
    }

    MiniportPresentContext = Device->hMiniportDevice;

    if (Entry->hSource != 0)
    {
        PDXGKVMM_ALLOCATION SourceAllocation;

        SourceAllocation = DxgkVidMmHandleToAllocation(
            (HANDLE)(ULONG_PTR)Entry->hSource);
        if (SourceAllocation != NULL &&
            SourceAllocation->MiniportHandle == NULL)
        {
            return STATUS_NOT_SUPPORTED;
        }
    }

    if (Entry->hDestination != 0)
    {
        PDXGKVMM_ALLOCATION DestinationAllocation;

        DestinationAllocation = DxgkVidMmHandleToAllocation(
            (HANDLE)(ULONG_PTR)Entry->hDestination);
        if (DestinationAllocation != NULL &&
            DestinationAllocation->MiniportHandle == NULL)
        {
            return STATUS_NOT_SUPPORTED;
        }
    }

    Status = DxgkpRefreshSharedPrimaryScanout(Adapter, Entry, &Handled);
    if (Handled)
    {
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_WARN("DxgkpExecuteFullPresent: shared-primary refresh "
                         "failed 0x%08lX\n",
                         Status);
        }

        return Status;
    }

    /*
     * Present uses an allocation list where slot 0 is always reserved.
     * Source and destination miniport handles live in slots 1 and 2.
     */
    if (Entry->hSource != 0)
    {
        Status = DxgkpOpenPresentAllocation(Adapter,
                                            Device,
                                            Entry->hSource,
                                            TRUE,
                                            &SourceDeviceSpecificHandle);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_WARN("DxgkpExecuteFullPresent: invalid hSource=0x%X\n",
                         Entry->hSource);
            return Status;
        }

        CloseSourceHandle = TRUE;

        PresentAllocationList[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation =
            SourceDeviceSpecificHandle;
        DxgkVidMmFillAllocationListEntry(
            Entry->hSource,
            &PresentAllocationList[DXGK_PRESENT_SOURCE_INDEX]);
        SubmitAllocationList[DXGK_PRESENT_SOURCE_INDEX].hAllocation = Entry->hSource;
        SubmitAllocationList[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation =
            (D3DKMT_HANDLE)(ULONG_PTR)SourceDeviceSpecificHandle;
    }

    if (Entry->hDestination != 0)
    {
        if (Entry->hDestination == Entry->hSource &&
            SourceDeviceSpecificHandle != NULL)
        {
            DestinationDeviceSpecificHandle = SourceDeviceSpecificHandle;
        }
        else
        {
            Status = DxgkpOpenPresentAllocation(Adapter,
                                                Device,
                                                Entry->hDestination,
                                                FALSE,
                                                &DestinationDeviceSpecificHandle);
            if (!NT_SUCCESS(Status))
            {
                if (CloseSourceHandle)
                    DxgkpClosePresentAllocation(Adapter, Device, SourceDeviceSpecificHandle);

                DXGKRNL_WARN("DxgkpExecuteFullPresent: invalid hDestination=0x%X\n",
                             Entry->hDestination);
                return Status;
            }

            CloseDestinationHandle = TRUE;
        }

        PresentAllocationList[DXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation =
            DestinationDeviceSpecificHandle;
        PresentAllocationList[DXGK_PRESENT_DESTINATION_INDEX].WriteOperation = 1;
        DxgkVidMmFillAllocationListEntry(
            Entry->hDestination,
            &PresentAllocationList[DXGK_PRESENT_DESTINATION_INDEX]);

        SubmitAllocationList[DXGK_PRESENT_DESTINATION_INDEX].hAllocation =
            Entry->hDestination;
        SubmitAllocationList[DXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation =
            (D3DKMT_HANDLE)(ULONG_PTR)DestinationDeviceSpecificHandle;
        SubmitAllocationList[DXGK_PRESENT_DESTINATION_INDEX].WriteOperation = 1;
    }

    TraceSeq = InterlockedIncrement(&g_PresentOpenTraceCount);
    if (DxgkpShouldTraceOrdinal(TraceSeq))
    {
        DXGKRNL_TRACE("DxgkpExecuteFullPresent: seq=%ld PresentId=%llu srcAlloc=0x%X "
                      "srcOpen=%p dstAlloc=0x%X dstOpen=%p type=%u rect=(%ld,%ld)-(%ld,%ld)\n",
                      TraceSeq,
                      Entry->PresentId,
                      Entry->hSource,
                      SourceDeviceSpecificHandle,
                      Entry->hDestination,
                      DestinationDeviceSpecificHandle,
                      Entry->Type,
                      Entry->DstRect.left,
                      Entry->DstRect.top,
                      Entry->DstRect.right,
                      Entry->DstRect.bottom);
    }

    DmaBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                      DXGK_PRESENT_IMMEDIATE_DMA_BYTES,
                                      TAG_DXGK_PRESENT);
    if (DmaBuffer == NULL)
    {
        DXGKRNL_WARN("DxgkpExecuteFullPresent: DMA buffer alloc failed\n");
        if (CloseDestinationHandle)
            DxgkpClosePresentAllocation(Adapter, Device, DestinationDeviceSpecificHandle);
        if (CloseSourceHandle)
            DxgkpClosePresentAllocation(Adapter, Device, SourceDeviceSpecificHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(PatchLocationList, sizeof(PatchLocationList));
    DstSubRect = Entry->DstRect;

    PresentArgs.pDmaBuffer            = DmaBuffer;
    PresentArgs.DmaSize               = DXGK_PRESENT_IMMEDIATE_DMA_BYTES;
    PresentArgs.pDmaBufferPrivateData = &DmaBufferPrivateData;
    PresentArgs.DmaBufferPrivateDataSize = sizeof(DmaBufferPrivateData);
    PresentArgs.pAllocationList       = PresentAllocationList;
    PresentArgs.pPatchLocationListOut = PatchLocationList;
    PresentArgs.PatchLocationListOutSize = RTL_NUMBER_OF(PatchLocationList);
    PresentArgs.MultipassOffset       = 0;
    PresentArgs.DmaBufferSegmentId    = 0;
    PresentArgs.DmaBufferPhysicalAddress.QuadPart = 0;
    PresentArgs.Reserved              = 0;
    PresentArgs.NumSrcAllocations     = (SourceDeviceSpecificHandle != NULL) ? 1 : 0;
    PresentArgs.NumDstAllocations     = (DestinationDeviceSpecificHandle != NULL) ? 1 : 0;
    PresentArgs.PrivateDriverDataSize = 0;
    PresentArgs.pPrivateDriverData    = NULL;

    /* Source and destination rectangles. */
    PresentArgs.SrcRect      = Entry->SrcRect;
    PresentArgs.DstRect      = Entry->DstRect;
    PresentArgs.SubRectCnt   = 1;
    PresentArgs.pDstSubRects = &DstSubRect;

    /* Map the present type to miniport flags. */
    PresentArgs.Flags.Value = 0;
    switch (Entry->Type)
    {
        case DxgkPresentTypeBlt:
            PresentArgs.Flags.Blt = 1;
            break;
        case DxgkPresentTypeFlip:
            PresentArgs.Flags.Flip = 1;
            break;
        case DxgkPresentTypeColorFill:
            PresentArgs.Flags.ColorFill = 1;
            break;
    }

    PresentArgs.Color           = Entry->Color;
    PresentArgs.FlipInterval    = Entry->FlipInterval;

    _SEH2_TRY
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiPresent(
                     MiniportPresentContext,
                     &PresentArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        DXGKRNL_ERR("DxgkpExecuteFullPresent: DxgkDdiPresent FAULTED "
                    "0x%08lX\n", Status);
    }
    _SEH2_END;

    if (NT_SUCCESS(Status) &&
        PresentArgs.pDmaBuffer != NULL &&
        (PUCHAR)PresentArgs.pDmaBuffer >= (PUCHAR)DmaBuffer)
    {
        DmaBytesUsed = (UINT)((PUCHAR)PresentArgs.pDmaBuffer - (PUCHAR)DmaBuffer);
    }

    if (NT_SUCCESS(Status) &&
        DmaBytesUsed > 0 &&
        DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand) != NULL)
    {
        DXGKARG_SUBMITCOMMAND SubmitArgs;
        DXGK_SUBMITCOMMANDFLAGS SubmitFlags;

        SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
        if (SubmissionFenceId == 0)
            SubmissionFenceId = 1;

        /*
         * Patch the DMA buffer before submission (documented WDDM order:
         * Present -> Patch -> SubmitCommand).  The allocation list carries
         * the current SegmentId/PhysicalAddress placement; the patch
         * location list holds whatever entries the Present DDI emitted.
         */
        if (DXGK_CB_FULL(Adapter, DxgkDdiPatch) != NULL)
        {
            DXGKARG_PATCH PatchArgs;
            UINT PatchEntries = 0;
            NTSTATUS PatchStatus;

            if (PresentArgs.pPatchLocationListOut != NULL &&
                PresentArgs.pPatchLocationListOut >= PatchLocationList &&
                (SIZE_T)(PresentArgs.pPatchLocationListOut - PatchLocationList) <=
                    RTL_NUMBER_OF(PatchLocationList))
            {
                PatchEntries = (UINT)(PresentArgs.pPatchLocationListOut -
                                      PatchLocationList);
            }

            RtlZeroMemory(&PatchArgs, sizeof(PatchArgs));
            PatchArgs.hDevice = MiniportPresentContext;
            PatchArgs.pDmaBuffer = DmaBuffer;
            PatchArgs.DmaBufferSize = DXGK_PRESENT_IMMEDIATE_DMA_BYTES;
            PatchArgs.DmaBufferSubmissionStartOffset = 0;
            PatchArgs.DmaBufferSubmissionEndOffset = DmaBytesUsed;
            PatchArgs.pDmaBufferPrivateData = &DmaBufferPrivateData;
            PatchArgs.DmaBufferPrivateDataSize = sizeof(DmaBufferPrivateData);
            PatchArgs.pAllocationList = PresentAllocationList;
            PatchArgs.AllocationListSize = RTL_NUMBER_OF(PresentAllocationList);
            PatchArgs.pPatchLocationList = PatchLocationList;
            PatchArgs.PatchLocationListSize = PatchEntries;
            PatchArgs.PatchLocationListSubmissionStart = 0;
            PatchArgs.PatchLocationListSubmissionLength = PatchEntries;
            PatchArgs.SubmissionFenceId = SubmissionFenceId;
            PatchArgs.Flags.Present = 1;

            _SEH2_TRY
            {
                PatchStatus = DXGK_CB_FULL(Adapter, DxgkDdiPatch)(
                                  Adapter->MiniportDeviceContext,
                                  &PatchArgs);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                PatchStatus = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(PatchStatus))
            {
                DXGKRNL_WARN("DxgkpExecuteFullPresent: DxgkDdiPatch failed "
                             "0x%08lX (continuing)\n", PatchStatus);
            }
        }

        RtlZeroMemory(&SubmitArgs, sizeof(SubmitArgs));
        SubmitFlags.Value = 0;
        SubmitFlags.Present = 1;
        if (Entry->Type == DxgkPresentTypeFlip)
            SubmitFlags.Flip = 1;

        /*
         * Presents are display control ops (flip/shared-primary refresh)
         * with no V3D dependency — their data dependency was satisfied by
         * the CPU blit before queueing.  Route them to the last node so
         * vsync-released packets never serialize behind 3D work on engine
         * 0 (sharing that FIFO quantized every 3D completion at ~16.7ms).
         */
        {
            ULONG VidSchFence = 0;
            ULONG PresentEngine = (Adapter->NodeCount > 1)
                                      ? Adapter->NodeCount - 1 : 0;

            Status = VidSchSubmitCommand(Adapter,
                                         PresentEngine,
                                         SubmissionFenceId,
                                         DmaBuffer,
                                         DmaBytesUsed,
                                         NULL,
                                         0,
                                         SubmitAllocationList,
                                         RTL_NUMBER_OF(SubmitAllocationList),
                                         NULL,
                                         0,
                                         MiniportPresentContext,
                                         TRUE,
                                         SubmitFlags.Value,
                                         Entry->VidPnSourceId,
                                         &VidSchFence);
        }

        if (Status == STATUS_DEVICE_NOT_READY || Status == STATUS_NOT_SUPPORTED)
        {
            SubmitArgs.pDmaBuffer = DmaBuffer;
            SubmitArgs.DmaBufferSize = DXGK_PRESENT_IMMEDIATE_DMA_BYTES;
            SubmitArgs.pDmaBufferPrivateData = &DmaBufferPrivateData;
            SubmitArgs.DmaBufferPrivateDataSize = sizeof(DmaBufferPrivateData);
            SubmitArgs.DmaBufferSubmissionStartOffset = 0;
            SubmitArgs.DmaBufferSubmissionEndOffset = DmaBytesUsed;
            SubmitArgs.pAllocationList = SubmitAllocationList;
            SubmitArgs.AllocationListSize = RTL_NUMBER_OF(SubmitAllocationList);
            SubmitArgs.SubmissionFenceId = SubmissionFenceId;
            SubmitArgs.VidPnSourceId = Entry->VidPnSourceId;
            SubmitArgs.NodeOrdinal = 0;
            SubmitArgs.EngineOrdinal = 0;
            SubmitArgs.hContext = MiniportPresentContext;
            SubmitArgs.Flags = SubmitFlags.Value;

            Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand)(
                         Adapter->MiniportDeviceContext,
                         &SubmitArgs);
        }

        if (NT_SUCCESS(Status))
        {
            NTSTATUS TrackStatus;

            if (Entry->Type == DxgkPresentTypeBlt &&
                Entry->hDestination != 0 &&
                Adapter->SharedPrimaryAllocationHandle != NULL &&
                (HANDLE)(ULONG_PTR)Entry->hDestination == Adapter->SharedPrimaryAllocationHandle)
            {
                RefreshSharedPrimaryOnRetire = TRUE;
                TraceSeq = InterlockedIncrement(&g_SharedPrimaryPresentTraceCount);
                if (DxgkpShouldTraceOrdinal(TraceSeq))
                {
                    DXGKRNL_TRACE("DxgkpExecuteFullPresent: seq=%ld PresentId=%llu "
                                  "queueing shared-primary refresh on retire "
                                  "fence=%u dst=0x%X src=0x%X rect=(%ld,%ld)-(%ld,%ld)\n",
                                  TraceSeq,
                                  Entry->PresentId,
                                  SubmissionFenceId,
                                  Entry->hDestination,
                                  Entry->hSource,
                                  Entry->DstRect.left,
                                  Entry->DstRect.top,
                                  Entry->DstRect.right,
                                  Entry->DstRect.bottom);
                }
            }

            if (SourceDeviceSpecificHandle != NULL)
                TrackedOpenHandles[TrackedOpenHandleCount++] = SourceDeviceSpecificHandle;
            if (DestinationDeviceSpecificHandle != NULL &&
                DestinationDeviceSpecificHandle != SourceDeviceSpecificHandle)
            {
                TrackedOpenHandles[TrackedOpenHandleCount++] =
                    DestinationDeviceSpecificHandle;
            }

            {
                DXGKRNL_TRACK_DMA_ARGS TrackArgs;

                RtlZeroMemory(&TrackArgs, sizeof(TrackArgs));
                TrackArgs.SubmissionFenceId = SubmissionFenceId;
                TrackArgs.PresentId = Entry->PresentId;
                TrackArgs.Buffer = DmaBuffer;
                TrackArgs.Tag = TAG_DXGK_PRESENT;
                TrackArgs.Device = Device;
                TrackArgs.SourceAllocationHandle = (HANDLE)(ULONG_PTR)Entry->hSource;
                TrackArgs.RefreshAllocationHandle = RefreshSharedPrimaryOnRetire ?
                    (HANDLE)(ULONG_PTR)Entry->hDestination : NULL;
                TrackArgs.RefreshVidPnSourceId = Entry->VidPnSourceId;
                TrackArgs.RefreshDstRect = &Entry->DstRect;
                TrackArgs.OpenHandles = TrackedOpenHandles;
                TrackArgs.OpenHandleCount = TrackedOpenHandleCount;

                TrackStatus = DxgkTrackSubmittedDmaBuffer(Adapter, &TrackArgs);
            }
            if (NT_SUCCESS(TrackStatus))
            {
                DmaBuffer = NULL;
                HandlesTracked = TRUE;
            }
            else
            {
                RefreshSharedPrimaryOnRetire = FALSE;
                DXGKRNL_WARN("DxgkpExecuteFullPresent: DMA track failed "
                             "0x%08lX for fence=%u\n",
                             TrackStatus,
                             SubmissionFenceId);
            }
        }
    }

    if (NT_SUCCESS(Status) &&
        !RefreshSharedPrimaryOnRetire &&
        Entry->Type == DxgkPresentTypeBlt &&
        Entry->hDestination != 0 &&
        Adapter->SharedPrimaryAllocationHandle != NULL &&
        (HANDLE)(ULONG_PTR)Entry->hDestination == Adapter->SharedPrimaryAllocationHandle)
    {
        DXGKRNL_PRESENT_ENTRY RefreshEntry;
        BOOLEAN RefreshHandled;
        NTSTATUS RefreshStatus;

        RefreshEntry = *Entry;
        RefreshEntry.hSource = Entry->hDestination;
        RefreshEntry.hDestination = Entry->hDestination;

        RefreshStatus = DxgkpRefreshSharedPrimaryScanout(Adapter,
                                                         &RefreshEntry,
                                                         &RefreshHandled);
        if (RefreshHandled)
        {
            TraceSeq = InterlockedIncrement(&g_SharedPrimaryPresentTraceCount);
            if (DxgkpShouldTraceOrdinal(TraceSeq))
            {
                DXGKRNL_TRACE("DxgkpExecuteFullPresent: seq=%ld PresentId=%llu "
                              "refreshed shared-primary immediately status=0x%08lX "
                              "dst=0x%X rect=(%ld,%ld)-(%ld,%ld)\n",
                              TraceSeq,
                              Entry->PresentId,
                              RefreshStatus,
                              Entry->hDestination,
                              Entry->DstRect.left,
                              Entry->DstRect.top,
                              Entry->DstRect.right,
                              Entry->DstRect.bottom);
            }
        }
        if (RefreshHandled && !NT_SUCCESS(RefreshStatus))
        {
            DXGKRNL_WARN("DxgkpExecuteFullPresent: shared-primary destination "
                         "refresh failed 0x%08lX\n",
                         RefreshStatus);
        }
    }

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpExecuteFullPresent: DxgkDdiPresent returned "
                     "0x%08lX\n", Status);
    }

    if (DmaBuffer != NULL)
        ExFreePoolWithTag(DmaBuffer, TAG_DXGK_PRESENT);

    if (CloseDestinationHandle)
    {
        if (HandlesTracked &&
            DestinationDeviceSpecificHandle != NULL &&
            (DestinationDeviceSpecificHandle == SourceDeviceSpecificHandle ||
             (TrackedOpenHandleCount == 2 &&
              DestinationDeviceSpecificHandle == TrackedOpenHandles[1])))
        {
            CloseDestinationHandle = FALSE;
        }
    }

    if (CloseSourceHandle && HandlesTracked)
        CloseSourceHandle = FALSE;

    if (CloseDestinationHandle)
        DxgkpClosePresentAllocation(Adapter, Device, DestinationDeviceSpecificHandle);

    if (CloseSourceHandle)
        DxgkpClosePresentAllocation(Adapter, Device, SourceDeviceSpecificHandle);

    return Status;
}

/* ========================================================================
 * DxgkpQueuePresent
 *
 * Enqueues a present into the per-VidPnSource FIFO.
 *
 * For display-only adapters with IMMEDIATE flip interval, the present
 * is executed synchronously (bypassing the queue) since there is no
 * hardware flip to synchronise with VSync.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
DxgkpQueuePresent(
    _In_  PDXGKRNL_ADAPTER         Adapter,
    _In_  PDXGKRNL_PRESENT_ENTRY   Entry,
    _Out_ ULONG64                  *OutPresentId)
{
    PDXGKRNL_PRESENT_QUEUE Queue;
    KIRQL OldIrql;

    PAGED_CODE();

    *OutPresentId = 0;

    if (Adapter == NULL || Entry == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Adapter->PresentQueues == NULL || Adapter->PresentQueueCount == 0)
    {
        DXGKRNL_ERR("DxgkpQueuePresent: no present queues initialised\n");
        return STATUS_DEVICE_NOT_READY;
    }

    /* Validate VidPn source index. */
    if (Entry->VidPnSourceId >= Adapter->PresentQueueCount)
    {
        DXGKRNL_ERR("DxgkpQueuePresent: VidPnSourceId %u >= queue count %lu\n",
                    Entry->VidPnSourceId, Adapter->PresentQueueCount);
        return STATUS_INVALID_PARAMETER;
    }

    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[Entry->VidPnSourceId];

    /*
     * For display-only adapters, execute the present immediately and
     * inline — there is no hardware flip, so queueing adds latency
     * without benefit.  The periodic timer in display.c still runs as a
     * fallback for any pixels not pushed via the present queue.
     */
    if (Adapter->MiniportContext->IsDisplayOnlyDriver &&
        Entry->FlipInterval == D3DDDI_FLIPINTERVAL_IMMEDIATE)
    {
        NTSTATUS Status;

        /* Assign a present ID even for immediate presents. */
        Entry->PresentId = InterlockedIncrement64(&Queue->NextPresentId);
        *OutPresentId = Entry->PresentId;

        Status = DxgkpExecuteDodPresent(Adapter, Entry);
        return Status;
    }

    /* --- Enqueue into the circular FIFO --------------------------------- */

    KeAcquireSpinLock(&Queue->QueueLock, &OldIrql);

    if (Queue->Count >= DXGKRNL_PRESENT_QUEUE_DEPTH)
    {
        KeReleaseSpinLock(&Queue->QueueLock, OldIrql);
        DXGKRNL_WARN("DxgkpQueuePresent: queue full (VidPnSrc=%u depth=%u)\n",
                     Entry->VidPnSourceId, DXGKRNL_PRESENT_QUEUE_DEPTH);
        return STATUS_DEVICE_BUSY;
    }

    /* Assign a present ID. */
    Entry->PresentId = InterlockedIncrement64(&Queue->NextPresentId);
    *OutPresentId = Entry->PresentId;

    /* Copy the entry into the FIFO slot. */
    Queue->Entries[Queue->Tail] = *Entry;
    Queue->Tail = (Queue->Tail + 1) % DXGKRNL_PRESENT_QUEUE_DEPTH;
    Queue->Count++;

    KeReleaseSpinLock(&Queue->QueueLock, OldIrql);

    /*
     * For immediate flip interval (non-DOD), execute right away rather
     * than waiting for VSync.  This allows full WDDM adapters to present
     * without tearing protection when the application requests it.
     */
    if (Entry->FlipInterval == D3DDDI_FLIPINTERVAL_IMMEDIATE)
    {
        NTSTATUS Status;

        Status = DxgkpProcessPresentQueue(Adapter, Entry->VidPnSourceId);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkpProcessPresentQueue
 *
 * Dequeues and executes the head present from the specified queue.
 * For VSync-synchronized presents, checks that enough VBlanks have
 * elapsed before executing.
 *
 * IRQL: PASSIVE_LEVEL (called from work-item or direct call)
 * ====================================================================== */
NTSTATUS
DxgkpProcessPresentQueue(
    _In_ PDXGKRNL_ADAPTER                  Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    VidPnSourceId)
{
    PDXGKRNL_PRESENT_QUEUE Queue;
    DXGKRNL_PRESENT_ENTRY  Entry;
    KIRQL OldIrql;
    NTSTATUS Status;
    PAGED_CODE();

    if (Adapter == NULL || Adapter->PresentQueues == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= Adapter->PresentQueueCount)
        return STATUS_INVALID_PARAMETER;

    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[VidPnSourceId];

    /* --- Dequeue the head entry under the spinlock ---------------------- */

    KeAcquireSpinLock(&Queue->QueueLock, &OldIrql);

    if (Queue->Count == 0)
    {
        KeReleaseSpinLock(&Queue->QueueLock, OldIrql);
        return STATUS_NO_MORE_ENTRIES;
    }

    /*
     * VSync gate: for flip presents with FlipInterval > 0, check that
     * at least FlipInterval VBlanks have elapsed since the last present.
     */
    {
        PDXGKRNL_PRESENT_ENTRY Head = &Queue->Entries[Queue->Head];

        if (Head->FlipInterval > D3DDDI_FLIPINTERVAL_IMMEDIATE)
        {
            LONG64 TargetVBlank = Queue->LastPresentVBlank +
                                  (LONG64)Head->FlipInterval;

            if (Queue->VBlankCount < TargetVBlank)
            {
                /* Not time yet — leave the entry queued. */
                KeReleaseSpinLock(&Queue->QueueLock, OldIrql);
                return STATUS_PENDING;
            }
        }
    }

    /* Copy the entry out and advance the head pointer. */
    Entry = Queue->Entries[Queue->Head];
    Queue->Head = (Queue->Head + 1) % DXGKRNL_PRESENT_QUEUE_DEPTH;
    Queue->Count--;
    Queue->LastPresentVBlank = Queue->VBlankCount;

    KeReleaseSpinLock(&Queue->QueueLock, OldIrql);

    /* --- Execute the present ------------------------------------------- */

    if (Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        Status = DxgkpExecuteDodPresent(Adapter, &Entry);
    }
    else
    {
        Status = DxgkpExecuteFullPresent(Adapter, &Entry);

        /*
         * If the full WDDM present path is not supported (e.g., the
         * miniport has no DxgkDdiPresent), fall back to the DOD path
         * if a shadow framebuffer is available.
         */
        if (Status == STATUS_NOT_SUPPORTED && Adapter->ShadowFb != NULL)
        {
            Status = DxgkpExecuteDodPresent(Adapter, &Entry);
        }
    }

    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
    {
        DXGKRNL_WARN("DxgkpProcessPresentQueue: present execution "
                     "returned 0x%08lX\n", Status);
    }

    return Status;
}

/* ========================================================================
 * DxgkpNotifyVSync
 *
 * Called from the CRTC_VSYNC interrupt notification path to bump the
 * VBlank counter on the specified VidPn source queue.
 *
 * IRQL: DISPATCH_LEVEL (ISR DPC context)
 * ====================================================================== */
VOID
DxgkpNotifyVSync(
    _In_ PDXGKRNL_ADAPTER                  Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    VidPnSourceId)
{
    PDXGKRNL_PRESENT_QUEUE Queue;

    if (Adapter == NULL || Adapter->PresentQueues == NULL)
        return;

    if (VidPnSourceId >= Adapter->PresentQueueCount)
        return;

    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[VidPnSourceId];

    /*
     * Atomic increment of the VBlank counter.  This is safe at
     * DISPATCH_LEVEL without the queue spinlock because VBlankCount is
     * only written here and only read (non-destructively) in the
     * dequeue path under the spinlock.
     */
    InterlockedIncrement64(&Queue->VBlankCount);
}

/* EOF */
