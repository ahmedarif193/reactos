/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM miniport registration, adapter PnP/Power lifecycle,
 *              and DxgkCb* callbacks exported to miniport drivers
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Overview
 * --------
 * This file implements the "port" half of the WDDM miniport/port driver split,
 * mirroring the role that videoprt.c plays for XDDM miniports and fdo.c plays
 * for storport miniports.
 *
 * Two-level object model
 * ----------------------
 *   DXGKRNL_MINIPORT_CONTEXT   — allocated as a DriverObjectExtension for the
 *       miniport's own DRIVER_OBJECT.  Created by DxgkInitializeEx; lives as
 *       long as the miniport is loaded.  Holds the DDI callback table copy and
 *       the canonical registry path.
 *
 *   DXGKRNL_ADAPTER            — stored in the FDO's DeviceExtension.  One
 *       instance per physical GPU.  Created by DxgkpAddDevice; destroyed by
 *       DxgkAdapterRemove.
 *
 * Dispatch hooking
 * ----------------
 * DxgkInitializeEx installs four callbacks into the miniport's DriverObject:
 *
 *   DriverExtension->AddDevice           ← DxgkpAddDevice
 *   MajorFunction[IRP_MJ_PNP]           ← DxgkpMiniportPnpDispatch
 *   MajorFunction[IRP_MJ_POWER]         ← DxgkpMiniportPowerDispatch
 *   DriverUnload                         ← DxgkpDriverUnload
 *
 * x86/amd64 memory ordering notes
 * --------------------------------
 * The adapter State field is read/written under AdapterMutex (FAST_MUTEX at
 * APC_LEVEL) from PASSIVE_LEVEL paths and is updated only in the PnP dispatch
 * which is serialised by the I/O manager.  No additional barriers are needed
 * for State.
 *
 * InterruptLock is a KSPIN_LOCK that may be acquired at DIRQL; the ISR path
 * uses it at that level.  The DPC path acquires it at DISPATCH_LEVEL via
 * KeAcquireSpinLockAtDpcLevel.  On x86-64 the TSO memory model guarantees
 * store visibility ordering across cores; LOCK XCHG (implicit in spinlock
 * acquire) provides the full fence semantics required here.
 *
 * The one-time init guard (DxgkpInitialized) uses InterlockedCompareExchange
 * which on x86-64 compiles to LOCK CMPXCHG — a fully-ordered instruction.
 * No additional fences are required.
 */

/* dxgkrnl_private.h includes NDEBUG, <debug.h>, and "debug.h" via PCH. */
#include "dxgkrnl_private.h"
#include "vidmm.h"
#include "vidsch.h"
#include "pnp.h"
#include "context.h"

#include <reactos/arc/arc.h>
/*
 * ntddvdeo.h is already included via dxgkrnl_private.h (before INITGUID),
 * so DEFINE_GUID only produced an extern declaration.  Instantiate here.
 */
const GUID GUID_DISPLAY_DEVICE_ARRIVAL =
    {0x1ca05180, 0xa699, 0x450a, {0x9a, 0x0c, 0xde, 0x4f, 0xbe, 0x3d, 0xdd, 0x89}};

/* ========================================================================
 * InbV forward declarations
 *
 * These functions are exported by ntoskrnl but not declared in public SDK
 * headers.  We forward-declare them here rather than pulling in the full
 * internal inbv.h to keep the dependency boundary clean.
 * ====================================================================== */

BOOLEAN
NTAPI
InbvHasValidGopFrameBuffer(VOID);

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

VOID
NTAPI
InbvAcquireDisplayOwnership(VOID);

/* ========================================================================
 * POST (boot) display ownership
 *
 * Windows semantics: exactly one adapter owns the firmware boot display at
 * a time.  The basic-display fallback (softgpu, the MSBDD equivalent) holds
 * it only until a real miniport acquires it through
 * DxgkCbAcquirePostDisplayOwnership, at which point dxgkrnl stops the
 * fallback adapter (the MSBDD handover).  Conversely, when a real miniport
 * already owns the boot display, an acquire from the fallback returns an
 * empty descriptor so its StartDevice declines and the fallback devnode is
 * torn down.
 * ====================================================================== */
static PDXGKRNL_ADAPTER g_PostDisplayOwnerAdapter = NULL;

/*
 * Stop the adapter currently holding the boot display so a new claimant can
 * take over.  Uses the documented Win8 handover DDI
 * (DxgkDdiStopDeviceAndReleasePostDisplayOwnership) when the owner
 * implements it, then runs the generic adapter stop (which unregisters the
 * \Device\VideoN display device so the claimant can register its own).
 */
static VOID
DxgkpStopPostDisplayOwner(
    _In_ PDXGKRNL_ADAPTER Owner)
{
    PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP PfnRelease;
    DXGK_DISPLAY_INFORMATION ReleasedInfo;
    NTSTATUS Status;

    DXGKRNL_WARN("DxgkpStopPostDisplayOwner: stopping %s adapter %p — "
                 "a new miniport is acquiring the boot display\n",
                 (Owner->MiniportContext != NULL &&
                  Owner->MiniportContext->IsBasicDisplayFallback)
                     ? "basic-display fallback" : "display",
                 Owner);

    if (Owner->State == DxgkAdapterStateStarted)
    {
        PfnRelease = DXGK_CB(Owner, DxgkDdiStopDeviceAndReleasePostDisplayOwnership);
        if (PfnRelease != NULL)
        {
            RtlZeroMemory(&ReleasedInfo, sizeof(ReleasedInfo));
            _SEH2_TRY
            {
                Status = PfnRelease(Owner->MiniportDeviceContext, 0, &ReleasedInfo);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (NT_SUCCESS(Status))
                Owner->MiniportDeviceStopped = TRUE;
            else
                DXGKRNL_WARN("DxgkpStopPostDisplayOwner: "
                             "StopDeviceAndReleasePostDisplayOwnership "
                             "failed 0x%08lX\n", Status);
        }

        DxgkAdapterStop(Owner);
    }

    if (g_PostDisplayOwnerAdapter == Owner)
        g_PostDisplayOwnerAdapter = NULL;
}

/* Called from DxgkAdapterStop so a stopped adapter never stays the owner. */
static VOID
DxgkpClearPostDisplayOwner(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (g_PostDisplayOwnerAdapter == Adapter)
        g_PostDisplayOwnerAdapter = NULL;
}

/* ========================================================================
 * Bugcheck-time display
 *
 * Windows brings the panic screen up through the display owner's
 * DxgkDdiSystemDisplayEnable.  The documented plumbing available to a
 * driver is KeRegisterBugCheckCallback: at bugcheck time the callback
 * tells the owning miniport to fall back to a kernel-writable linear
 * frame buffer (rpi5vc4 re-points the HVS at the firmware framebuffer),
 * after which the Inbv-driven bugcheck output is actually visible.
 * ====================================================================== */

static KBUGCHECK_CALLBACK_RECORD g_DxgkBugCheckRecord;

static VOID
NTAPI
DxgkpBugCheckCallback(
    _In_opt_ PVOID Buffer,
    _In_ ULONG Length)
{
    PDXGKRNL_ADAPTER Owner = g_PostDisplayOwnerAdapter;
    PDXGKDDI_SYSTEM_DISPLAY_ENABLE PfnEnable;
    DXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags;
    UINT Width = 0;
    UINT Height = 0;
    D3DDDIFORMAT ColorFormat = D3DDDIFMT_UNKNOWN;

    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);

    if (Owner == NULL ||
        Owner->State != DxgkAdapterStateStarted ||
        Owner->MiniportContext == NULL)
    {
        return;
    }

    PfnEnable = DXGK_CB(Owner, DxgkDdiSystemDisplayEnable);
    if (PfnEnable == NULL)
        return;

    RtlZeroMemory(&Flags, sizeof(Flags));
    (VOID)PfnEnable(Owner->MiniportDeviceContext,
                    0,
                    &Flags,
                    &Width,
                    &Height,
                    &ColorFormat);
}

VOID
DxgkpRegisterBugCheckCallback(VOID)
{
    KeInitializeCallbackRecord(&g_DxgkBugCheckRecord);
    KeRegisterBugCheckCallback(&g_DxgkBugCheckRecord,
                               DxgkpBugCheckCallback,
                               NULL,
                               0,
                               (PUCHAR)"dxgkrnl");
}

/* ========================================================================
 * TDR watchdog
 *
 * A 1 Hz per-adapter timer watches the oldest entry of the tracked
 * submitted-DMA list.  If the same fence stays at the head for
 * DXGKP_TDR_STUCK_TICKS consecutive ticks without the miniport reporting
 * completion, a work item performs the documented timeout recovery:
 * DxgkDdiResetFromTimeout -> DxgkDdiRestartFromTimeout -> retire.
 * ====================================================================== */

#define DXGKP_TDR_TICK_MS       1000
#define DXGKP_TDR_STUCK_TICKS   3

static VOID
NTAPI
DxgkpTdrWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;
    NTSTATUS Status;

    if (Adapter == NULL)
        return;

    DXGKRNL_ERR("DxgkpTdrWorker: GPU timeout — fence %lu stuck on "
                "adapter %p\n", Adapter->TdrLastObservedFence, Adapter);

    /*
     * Documented TDR escalation: attempt engine preemption first; a hung
     * submission that preempts cleanly recovers without an adapter reset.
     * Give the miniport a short window to report DMA_PREEMPTED progress.
     */
    if (NT_SUCCESS(VidSchPreemptEngine(Adapter, 0)))
    {
        LARGE_INTEGER Delay;

        Delay.QuadPart = -(LONGLONG)(100 * 10 * 1000);  /* 100 ms */
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);

        if (Adapter->LastCompletedSubmissionFenceId !=
            Adapter->TdrLastObservedFence)
        {
            DXGKRNL_ERR("DxgkpTdrWorker: preemption recovered adapter %p "
                        "(fence %lu -> %lu), skipping reset\n",
                        Adapter,
                        Adapter->TdrLastObservedFence,
                        Adapter->LastCompletedSubmissionFenceId);
            Adapter->TdrStuckTicks = 0;
            return;
        }
    }

    DXGKRNL_ERR("DxgkpTdrWorker: preemption did not recover — resetting "
                "adapter %p\n", Adapter);

    /*
     * Documented TDR order: collect the driver's debug report before the
     * reset (Windows feeds it into the OCA/watchdog dump).  A small
     * scratch buffer suffices for the log side effects; content is
     * currently discarded.
     */
    if (DXGK_CB(Adapter, DxgkDdiCollectDbgInfo) != NULL)
    {
        DXGKARG_COLLECTDBGINFO CollectArgs;
        UCHAR DbgBuffer[256];

        RtlZeroMemory(&CollectArgs, sizeof(CollectArgs));
        RtlZeroMemory(DbgBuffer, sizeof(DbgBuffer));
        CollectArgs.Reason = 0x117;     /* VIDEO_TDR_TIMEOUT_DETECTED */
        CollectArgs.pBuffer = DbgBuffer;
        CollectArgs.BufferSize = sizeof(DbgBuffer);

        _SEH2_TRY
        {
            (VOID)DXGK_CB(Adapter, DxgkDdiCollectDbgInfo)(
                      Adapter->MiniportDeviceContext,
                      &CollectArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    if (DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout) != NULL)
    {
        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout)(
                         Adapter->MiniportDeviceContext);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkpTdrWorker: ResetFromTimeout failed 0x%08lX\n",
                        Status);
        }

        if (DXGK_CB_FULL(Adapter, DxgkDdiRestartFromTimeout) != NULL)
        {
            _SEH2_TRY
            {
                (VOID)DXGK_CB_FULL(Adapter, DxgkDdiRestartFromTimeout)(
                          Adapter->MiniportDeviceContext);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            _SEH2_END;
        }
    }

    /* The reset completed outstanding fences; retire their buffers. */
    DxgkRetireCompletedDmaBuffers(Adapter);

    InterlockedExchange(&Adapter->TdrWorkQueued, 0);
}

static VOID
NTAPI
DxgkpTdrDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)DeferredContext;
    ULONG HeadFence = 0;
    BOOLEAN Outstanding = FALSE;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Adapter == NULL || !Adapter->TdrTimerActive)
        return;

    KeAcquireSpinLockAtDpcLevel(&Adapter->SubmitDmaLock);
    if (!IsListEmpty(&Adapter->SubmitDmaListHead))
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Head =
            CONTAINING_RECORD(Adapter->SubmitDmaListHead.Flink,
                              DXGKRNL_SUBMIT_DMA_BUFFER,
                              ListEntry);
        HeadFence = Head->SubmissionFenceId;
        Outstanding = TRUE;
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->SubmitDmaLock);

    if (!Outstanding ||
        (LONG)(Adapter->LastCompletedSubmissionFenceId - HeadFence) >= 0)
    {
        /* Idle, or completed but not yet retired: not stuck. */
        Adapter->TdrStuckTicks = 0;
        Adapter->TdrLastObservedFence = HeadFence;
        return;
    }

    if (HeadFence != Adapter->TdrLastObservedFence)
    {
        Adapter->TdrLastObservedFence = HeadFence;
        Adapter->TdrStuckTicks = 0;
        return;
    }

    if (++Adapter->TdrStuckTicks >= DXGKP_TDR_STUCK_TICKS)
    {
        Adapter->TdrStuckTicks = 0;
        if (InterlockedCompareExchange(&Adapter->TdrWorkQueued, 1, 0) == 0)
            ExQueueWorkItem(&Adapter->TdrWorkItem, DelayedWorkQueue);
    }
}

static VOID
DxgkpStartTdrWatchdog(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Due;

    KeInitializeTimer(&Adapter->TdrTimer);
    KeInitializeDpc(&Adapter->TdrDpc, DxgkpTdrDpcRoutine, Adapter);
    ExInitializeWorkItem(&Adapter->TdrWorkItem, DxgkpTdrWorker, Adapter);
    Adapter->TdrWorkQueued = 0;
    Adapter->TdrLastObservedFence = 0;
    Adapter->TdrStuckTicks = 0;
    Adapter->TdrTimerActive = TRUE;

    Due.QuadPart = -10000LL * DXGKP_TDR_TICK_MS;
    KeSetTimerEx(&Adapter->TdrTimer, Due, DXGKP_TDR_TICK_MS, &Adapter->TdrDpc);
}

static VOID
DxgkpStopTdrWatchdog(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (!Adapter->TdrTimerActive)
        return;

    Adapter->TdrTimerActive = FALSE;
    KeCancelTimer(&Adapter->TdrTimer);
    KeRemoveQueueDpc(&Adapter->TdrDpc);
    KeFlushQueuedDpcs();

    /* A TDR work item may be mid-reset against the miniport; wait it out. */
    DxgkpWaitForFlagClear(&Adapter->TdrWorkQueued);
}

/*
 * DxgkpEnsurePostDisplayResolution
 *
 * Populate Adapter->PostDisplayWidth/Height from the firmware GOP framebuffer
 * if a minimal miniport never called DxgkCbAcquirePostDisplayOwnership (softgpu
 * does not). This MUST run before DxgkDisplayRegister writes the registry
 * DefaultSettings and before the VidPn is committed: win32k sizes its GDI
 * surface from DefaultSettings while framebuf renders into a shadow FB sized to
 * the committed mode. If they disagree (e.g. DefaultSettings 1024x768 vs a
 * committed/GOP 800x600 FB) framebuf writes past the shadow FB and corrupts
 * adjacent NonPagedPool. Anchoring all three to the real GOP resolution keeps
 * them consistent.
 */
VOID
DxgkpEnsurePostDisplayResolution(
    _Inout_ PDXGKRNL_ADAPTER Adapter)
{
    LOADER_PARAMETER_FRAMEBUFFER Fb;
    ULONG  Pitch;
    SIZE_T FbSize;

    if (Adapter == NULL)
        return;

    /* Already set up (geometry known and GOP mapped)? */
    if (Adapter->PostDisplayVirtualAddress != NULL &&
        Adapter->PostDisplayWidth != 0 && Adapter->PostDisplayHeight != 0)
        return;

    /*
     * A minimal WDDM 1.0 miniport (softgpu) never calls
     * DxgkCbAcquirePostDisplayOwnership, so PostDisplay* stay zero and the GOP
     * is never mapped. Query + map the firmware framebuffer directly.
     *
     * We deliberately use InbvGetGopFrameBufferInfo (not the
     * DxgkCbAcquirePostDisplayOwnership path) because InbvHasValidGopFrameBuffer
     * can report FALSE this late in boot even though the GOP info is still
     * retrievable — the callback would then take its VBE fallback and leave
     * PostDisplay zeroed. Anchoring the real GOP geometry here keeps registry
     * DefaultSettings, the pinned VidPn mode and the shadow FB consistent, and
     * the kernel mapping lets the present path blit the shadow FB to the screen.
     */
    RtlZeroMemory(&Fb, sizeof(Fb));
    if (!InbvGetGopFrameBufferInfo(&Fb) ||
        Fb.HorizontalResolution == 0 || Fb.VerticalResolution == 0 ||
        Fb.FrameBufferBase.QuadPart == 0)
    {
        DXGKRNL_WARN("DxgkpEnsurePostDisplayResolution: no usable GOP framebuffer\n");
        return;
    }

    Pitch = Fb.PixelsPerScanLine * 4;
    if (Pitch < Fb.HorizontalResolution * 4)
        Pitch = Fb.HorizontalResolution * 4;

    Adapter->PostDisplayWidth           = Fb.HorizontalResolution;
    Adapter->PostDisplayHeight          = Fb.VerticalResolution;
    Adapter->PostDisplayPitch           = Pitch;
    Adapter->PostDisplayPhysicalAddress = Fb.FrameBufferBase;

    if (Adapter->PostDisplayVirtualAddress == NULL)
    {
        FbSize = (SIZE_T)Pitch * Fb.VerticalResolution;
        Adapter->PostDisplayVirtualAddress =
            MmMapIoSpace(Fb.FrameBufferBase, FbSize, MmWriteCombined);
        if (Adapter->PostDisplayVirtualAddress == NULL)
            Adapter->PostDisplayVirtualAddress =
                MmMapIoSpace(Fb.FrameBufferBase, FbSize, MmNonCached);
        if (Adapter->PostDisplayVirtualAddress != NULL)
            Adapter->PostDisplayMappingSize = FbSize;
    }

    DXGKRNL_TRACE("DxgkpEnsurePostDisplayResolution: GOP %lux%lu pitch=%lu "
                  "PA=0x%I64X VA=%p size=%Iu\n",
                  Adapter->PostDisplayWidth, Adapter->PostDisplayHeight,
                  Adapter->PostDisplayPitch,
                  Adapter->PostDisplayPhysicalAddress.QuadPart,
                  Adapter->PostDisplayVirtualAddress,
                  Adapter->PostDisplayMappingSize);
}

/* ========================================================================
 * Module-local state
 * ====================================================================== */

/*
 * DxgkpInitialized
 *
 * One-time init guard.  0 = not yet initialised, 1 = initialised.
 * Written only by DxgkpFirstInit via InterlockedCompareExchange so that
 * concurrent DriverEntry calls from multiple miniports are safe.
 */
static LONG DxgkpInitialized = 0;
static FAST_MUTEX DxgkpMapMemoryMutex;
static LIST_ENTRY DxgkpMapMemoryList;
static volatile LONG DxgkpTrackedRefreshTraceCount = 0;
static volatile LONG DxgkpRetireTraceCount = 0;
static volatile LONG DxgkpTrackedSampleTraceCount = 0;

#define DXGK_TRACE_SLOW_CONFIG_ACCESS_US   1000ULL
#define DXGK_TRACE_SLOW_SYNC_US            1000ULL
#define DXGK_TRACE_SLOW_DPC_US             1000ULL
#define DXGK_TRACE_ISR_LOG_LIMIT           16
#define DXGK_TRACE_DPC_LOG_LIMIT           16
#define DXGKP_FIELD_END(Type, Field) \
    (FIELD_OFFSET(Type, Field) + sizeof(((Type *)0)->Field))

typedef struct _DXGK_MAPMEM_ENTRY
{
    LIST_ENTRY ListEntry;
    PVOID      VirtualAddress;
    PVOID      BaseAddress;
    PMDL       Mdl;
} DXGK_MAPMEM_ENTRY, *PDXGK_MAPMEM_ENTRY;

FORCEINLINE ULONGLONG
DxgkpTraceNow100ns(VOID)
{
    return KeQueryInterruptTime();
}

FORCEINLINE ULONGLONG
DxgkpTraceElapsedUs(
    _In_ ULONGLONG Start100ns)
{
    ULONGLONG End100ns = KeQueryInterruptTime();

    if (End100ns <= Start100ns)
        return 0;

    return (End100ns - Start100ns) / 10ULL;
}

FORCEINLINE ULONGLONG
DxgkpTraceSinceStartUs(
    _In_opt_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || Adapter->InterruptTraceEpoch100ns == 0)
        return 0;

    return DxgkpTraceElapsedUs(Adapter->InterruptTraceEpoch100ns);
}

FORCEINLINE BOOLEAN
DxgkpFenceIdReached(
    _In_ ULONG CompletedFenceId,
    _In_ ULONG SubmissionFenceId)
{
    return ((LONG)(CompletedFenceId - SubmissionFenceId) >= 0);
}

static VOID
DxgkpReleasePostDisplayMapping(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    SIZE_T FbSize;

    if (Adapter == NULL || Adapter->PostDisplayVirtualAddress == NULL)
        return;

    FbSize = Adapter->PostDisplayMappingSize;
    if (FbSize == 0)
        FbSize = (SIZE_T)Adapter->PostDisplayPitch * Adapter->PostDisplayHeight;
    MmUnmapIoSpace(Adapter->PostDisplayVirtualAddress, FbSize);

    Adapter->PostDisplayVirtualAddress = NULL;
    Adapter->PostDisplayMappingSize = 0;
    Adapter->PostDisplayPhysicalAddress.QuadPart = 0;
    Adapter->PostDisplayPitch = 0;
    Adapter->PostDisplayWidth = 0;
    Adapter->PostDisplayHeight = 0;
}

ULONG
NTAPI
DxgkAllocateSubmissionFenceId(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG FenceId;

    if (Adapter == NULL)
        return 0;

    FenceId = InterlockedIncrement(&Adapter->NextSubmissionFenceId);
    if (FenceId <= 0)
    {
        Adapter->NextSubmissionFenceId = 1;
        FenceId = 1;
    }

    return (ULONG)FenceId;
}

NTSTATUS
NTAPI
DxgkTrackSubmittedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const DXGKRNL_TRACK_DMA_ARGS *Args)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Entry;
    KIRQL OldIrql;
    UINT Index;

    if (Adapter == NULL || Args == NULL ||
        Args->Buffer == NULL || Args->SubmissionFenceId == 0)
        return STATUS_INVALID_PARAMETER;

    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Entry),
                                  TAG_DXGK_SUBMITDMA);
    if (Entry == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Entry->SubmissionFenceId = Args->SubmissionFenceId;
    Entry->NodeOrdinal = Args->NodeOrdinal % DXGK_MAX_TRACKED_NODES;
    Entry->hSignalSyncObject = Args->hSignalSyncObject;
    Entry->SignalFenceValue = Args->SignalFenceValue;
    Entry->RefreshPresentId = Args->PresentId;
    if (Args->Device != NULL)
        InterlockedIncrement(&Args->Device->InFlightSubmissions);
    Entry->Buffer = Args->Buffer;
    Entry->Tag = Args->Tag;
    Entry->Device = Args->Device;
    Entry->SourceAllocationHandle = Args->SourceAllocationHandle;
    Entry->RefreshAllocationHandle = Args->RefreshAllocationHandle;
    Entry->RefreshVidPnSourceId = Args->RefreshVidPnSourceId;
    if (Args->RefreshDstRect != NULL)
        Entry->RefreshDstRect = *Args->RefreshDstRect;
    else
        RtlZeroMemory(&Entry->RefreshDstRect, sizeof(Entry->RefreshDstRect));
    Entry->RefreshSharedPrimaryOnRetire = (Args->RefreshAllocationHandle != NULL);
    Entry->OpenHandleCount = Args->OpenHandleCount;
    Entry->OpenHandleList = NULL;

    if (Args->OpenHandleCount != 0)
    {
        Entry->OpenHandleList = ExAllocatePoolWithTag(NonPagedPool,
                                                      Args->OpenHandleCount * sizeof(HANDLE),
                                                      TAG_DXGK_SUBMITDMA);
        if (Entry->OpenHandleList == NULL)
        {
            ExFreePoolWithTag(Entry, TAG_DXGK_SUBMITDMA);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        for (Index = 0; Index < Args->OpenHandleCount; ++Index)
            Entry->OpenHandleList[Index] = Args->OpenHandles[Index];
    }

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    InsertTailList(&Adapter->SubmitDmaListHead, &Entry->ListEntry);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
    return STATUS_SUCCESS;
}

static ULONG
DxgkpTrackedSamplePitch(
    _In_ PDXGKVMM_ALLOCATION Allocation,
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

static VOID
DxgkpTraceTrackedSurfaceSample(
    _In_z_ PCSTR SurfaceTag,
    _In_ ULONG64 PresentId,
    _In_ ULONG FenceId,
    _In_ const RECT *Rect,
    _In_reads_bytes_(PitchBytes * Height) const VOID *Base,
    _In_ ULONG PitchBytes,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    const ULONG *Pixels = (const ULONG *)Base;
    ULONG PitchPixels;
    RECT SampleRect;
    LONG xs[5];
    LONG ys[5];
    ULONG Samples[5];
    ULONG NonZeroCount = 0;
    UINT i;

    if (Rect == NULL ||
        Base == NULL ||
        PitchBytes < sizeof(ULONG) ||
        Width == 0 ||
        Height == 0)
    {
        return;
    }

    SampleRect = *Rect;
    if (SampleRect.left < 0)
        SampleRect.left = 0;
    if (SampleRect.top < 0)
        SampleRect.top = 0;
    if (SampleRect.right > (LONG)Width)
        SampleRect.right = (LONG)Width;
    if (SampleRect.bottom > (LONG)Height)
        SampleRect.bottom = (LONG)Height;
    if (SampleRect.left >= SampleRect.right ||
        SampleRect.top >= SampleRect.bottom)
    {
        return;
    }

    PitchPixels = PitchBytes / sizeof(ULONG);
    xs[0] = SampleRect.left;
    ys[0] = SampleRect.top;
    xs[1] = SampleRect.right - 1;
    ys[1] = SampleRect.top;
    xs[2] = SampleRect.left + ((SampleRect.right - SampleRect.left) / 2);
    ys[2] = SampleRect.top + ((SampleRect.bottom - SampleRect.top) / 2);
    xs[3] = SampleRect.left;
    ys[3] = SampleRect.bottom - 1;
    xs[4] = SampleRect.right - 1;
    ys[4] = SampleRect.bottom - 1;

    for (i = 0; i < RTL_NUMBER_OF(Samples); ++i)
    {
        Samples[i] = Pixels[(ys[i] * PitchPixels) + xs[i]];
        if (Samples[i] != 0)
            ++NonZeroCount;
    }

    DXGKRNL_TRACE("DxgkpTrackedSample[%s]: PresentId=%llu fence=%u "
                  "nz=%lu pitch=%lu rect=(%ld,%ld)-(%ld,%ld) "
                  "tl=%08lx tr=%08lx c=%08lx bl=%08lx br=%08lx\n",
                  SurfaceTag,
                  PresentId,
                  FenceId,
                  NonZeroCount,
                  PitchBytes,
                  SampleRect.left,
                  SampleRect.top,
                  SampleRect.right,
                  SampleRect.bottom,
                  (unsigned long)Samples[0],
                  (unsigned long)Samples[1],
                  (unsigned long)Samples[2],
                  (unsigned long)Samples[3],
                  (unsigned long)Samples[4]);
}

static VOID
DxgkpTraceTrackedRefreshSamples(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry,
    _In_ PDXGKVMM_ALLOCATION DestinationAllocation)
{
    PDXGKVMM_ALLOCATION SourceAllocation;
    PVOID SourceVa = NULL;
    PVOID DestinationVa = NULL;
    ULONG SourcePitch = 0;
    ULONG DestinationPitch = 0;
    ULONG Width;
    ULONG Height;

    if (Adapter == NULL || Entry == NULL || DestinationAllocation == NULL)
        return;

    if (InterlockedIncrement(&DxgkpTrackedSampleTraceCount) > 64)
        return;

    Width = Adapter->SharedPrimaryWidth;
    Height = Adapter->SharedPrimaryHeight;
    if (Width == 0 || Height == 0)
        return;

    DestinationPitch = DxgkpTrackedSamplePitch(DestinationAllocation,
                                               Width,
                                               Height,
                                               0);
    if (NT_SUCCESS(DxgkVidMmMapAllocationCpu(DestinationAllocation, &DestinationVa)))
    {
        DxgkpTraceTrackedSurfaceSample("dst",
                                       Entry->RefreshPresentId,
                                       Entry->SubmissionFenceId,
                                       &Entry->RefreshDstRect,
                                       DestinationVa,
                                       DestinationPitch,
                                       Width,
                                       Height);
    }

    if (Entry->SourceAllocationHandle == NULL)
        return;

    SourceAllocation = DxgkVidMmHandleToAllocation(Entry->SourceAllocationHandle);
    if (SourceAllocation == NULL || SourceAllocation->Adapter != Adapter)
        return;

    if (Entry->SourceAllocationHandle == Adapter->SharedShadowAllocationHandle)
    {
        Width = Adapter->SharedShadowWidth;
        Height = Adapter->SharedShadowHeight;
        SourcePitch = Adapter->SharedShadowPitch;
    }
    else
    {
        Width = Adapter->SharedPrimaryWidth;
        Height = Adapter->SharedPrimaryHeight;
    }

    if (Width == 0 || Height == 0)
        return;

    SourcePitch = DxgkpTrackedSamplePitch(SourceAllocation,
                                          Width,
                                          Height,
                                          SourcePitch);
    if (NT_SUCCESS(DxgkVidMmMapAllocationCpu(SourceAllocation, &SourceVa)))
    {
        DxgkpTraceTrackedSurfaceSample("src",
                                       Entry->RefreshPresentId,
                                       Entry->SubmissionFenceId,
                                       &Entry->RefreshDstRect,
                                       SourceVa,
                                       SourcePitch,
                                       Width,
                                       Height);
    }
}

static VOID
DxgkpRefreshTrackedSharedPrimaryScanout(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    PDXGKVMM_ALLOCATION Allocation;
    DXGKARG_SETVIDPNSOURCEADDRESS SetSourceAddress;
    DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
    LARGE_INTEGER PrimaryAddress;
    NTSTATUS Status;

    if (Adapter == NULL ||
        Entry == NULL ||
        !Entry->RefreshSharedPrimaryOnRetire ||
        Entry->RefreshAllocationHandle == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver ||
        DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress) == NULL)
    {
        return;
    }

    Allocation = DxgkVidMmHandleToAllocation(Entry->RefreshAllocationHandle);
    if (Allocation == NULL || Allocation->Adapter != Adapter)
    {
        DXGKRNL_WARN("DxgkpRefreshTrackedSharedPrimaryScanout: invalid allocation %p\n",
                     Entry->RefreshAllocationHandle);
        return;
    }

    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpRefreshTrackedSharedPrimaryScanout: aperture map failed "
                     "0x%08lX for %p\n",
                     Status,
                     Entry->RefreshAllocationHandle);
        return;
    }

    DxgkpTraceTrackedRefreshSamples(Adapter, Entry, Allocation);

    PrimaryAddress = DxgkVidMmGetAllocationPrimaryAddress(Allocation);

    RtlZeroMemory(&SetSourceAddress, sizeof(SetSourceAddress));
    SetSourceAddress.VidPnSourceId = Entry->RefreshVidPnSourceId;
    SetSourceAddress.hAllocation = Allocation->MiniportHandle;
    SetSourceAddress.PrimaryAddress = PrimaryAddress;
    SetSourceAddress.SegmentId = Allocation->SegmentId;
    SetSourceAddress.Flags.FlipImmediate = 1;

    Status = DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress)(
        Adapter->MiniportDeviceContext,
        &SetSourceAddress);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpRefreshTrackedSharedPrimaryScanout: SetVidPnSourceAddress "
                     "failed 0x%08lX fence=%u alloc=%p seg=%u addr=0x%I64x\n",
                     Status,
                     Entry->SubmissionFenceId,
                     Entry->RefreshAllocationHandle,
                     Allocation->SegmentId,
                     PrimaryAddress.QuadPart);
        return;
    }

    if (DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility) != NULL)
    {
        RtlZeroMemory(&Visibility, sizeof(Visibility));
        Visibility.VidPnSourceId = Entry->RefreshVidPnSourceId;
        Visibility.Visible = TRUE;
        DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility)(
            Adapter->MiniportDeviceContext,
            &Visibility);
    }

    if (InterlockedIncrement(&DxgkpTrackedRefreshTraceCount) <= 128)
    {
        DXGKRNL_TRACE("DxgkpRefreshTrackedSharedPrimaryScanout: fence=%u "
                      "present=%llu alloc=%p seg=%u addr=0x%I64x src=%u status=0x%08lX\n",
                      Entry->SubmissionFenceId,
                      Entry->RefreshPresentId,
                      Entry->RefreshAllocationHandle,
                      Allocation->SegmentId,
                      PrimaryAddress.QuadPart,
                      Entry->RefreshVidPnSourceId,
                      Status);
    }

}

static VOID
DxgkpFreeTrackedDmaBufferEntry(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    if (Entry->RefreshSharedPrimaryOnRetire)
        DxgkpRefreshTrackedSharedPrimaryScanout(Adapter, Entry);

    if (Entry->Device != NULL)
        InterlockedDecrement(&Entry->Device->InFlightSubmissions);

    /* GPU completion drives the monitored fence: update the CPU-visible
     * value page (monotonic) and wake CPU waiters. */
    if (Entry->hSignalSyncObject != 0)
    {
        (VOID)DxgkSyncObjectGpuRetireSignal(Entry->hSignalSyncObject,
                                            Entry->SignalFenceValue);
    }

    if (Entry->Device != NULL &&
        Entry->OpenHandleCount != 0 &&
        Entry->OpenHandleList != NULL &&
        DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) != NULL)
    {
        DXGKARG_CLOSEALLOCATION CloseArgs;

        RtlZeroMemory(&CloseArgs, sizeof(CloseArgs));
        CloseArgs.NumAllocations = Entry->OpenHandleCount;
        CloseArgs.pOpenHandleList = Entry->OpenHandleList;

        DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation)(
            Entry->Device->hMiniportDevice,
            &CloseArgs);
    }

    if (Entry->OpenHandleList != NULL)
        ExFreePoolWithTag(Entry->OpenHandleList, TAG_DXGK_SUBMITDMA);

    ExFreePoolWithTag(Entry->Buffer, Entry->Tag);
    ExFreePoolWithTag(Entry, TAG_DXGK_SUBMITDMA);
}

static VOID
NTAPI
DxgkpRetireSubmittedDmaBuffersWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;
    LIST_ENTRY FreeList;
    KIRQL OldIrql;

    ULONG Batch;

    if (Adapter == NULL)
        return;

    /*
     * Bounded batch: under sustained present load an unbounded drain
     * loop monopolises a Delayed worker thread indefinitely (observed
     * on RPi5 silicon as 1 Hz "Work Queue Deadlock detected" with 16
     * dynamic threads spawned).  Free up to one batch, then re-queue
     * ourselves and RETURN the thread to the pool.
     */
    InitializeListHead(&FreeList);

    {
    BOOLEAN MorePending = FALSE;

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    for (Batch = 0;
         Batch < 64 && !IsListEmpty(&Adapter->SubmitDmaRetireListHead);
         Batch++)
    {
        PLIST_ENTRY Link = RemoveHeadList(&Adapter->SubmitDmaRetireListHead);
        InsertTailList(&FreeList, Link);
    }

    if (IsListEmpty(&Adapter->SubmitDmaRetireListHead))
    {
        InterlockedExchange(&Adapter->SubmitDmaRetireWorkQueued, 0);
    }
    else
    {
        /* More work pending: WE keep the flag set, so producers won't
         * queue — the re-queue below is ours alone. Re-queueing based on
         * the flag after a reset would race a producer that re-armed it
         * and already queued this same WORK_QUEUE_ITEM. */
        MorePending = TRUE;
    }
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    while (!IsListEmpty(&FreeList))
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Entry;

        Entry = CONTAINING_RECORD(RemoveHeadList(&FreeList),
                                  DXGKRNL_SUBMIT_DMA_BUFFER,
                                  ListEntry);
        DxgkpFreeTrackedDmaBufferEntry(Adapter, Entry);
    }

    if (MorePending)
    {
        ExQueueWorkItem(&Adapter->SubmitDmaRetireWorkItem, DelayedWorkQueue);
    }
    }
}

VOID
NTAPI
DxgkRetireCompletedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY RetireList;
    KIRQL OldIrql;
    ULONG CompletedFenceId;
    BOOLEAN QueueWorker = FALSE;

    if (Adapter == NULL)
        return;

    CompletedFenceId = Adapter->LastCompletedSubmissionFenceId;
    if (CompletedFenceId == 0)
        return;

    InitializeListHead(&RetireList);

    /*
     * Walk the whole list: independent GPU nodes complete out of global
     * fence order, so an unreached entry no longer implies everything
     * behind it is unreached.  Each entry retires against ITS node's
     * completed fence (falling back to the global fence for node 0
     * completions reported before per-node tracking existed).
     */
    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    {
        PLIST_ENTRY Link = Adapter->SubmitDmaListHead.Flink;

        while (Link != &Adapter->SubmitDmaListHead)
        {
            PDXGKRNL_SUBMIT_DMA_BUFFER Entry =
                CONTAINING_RECORD(Link, DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
            PLIST_ENTRY Next = Link->Flink;
            ULONG NodeFence =
                Adapter->NodeLastCompletedFenceId[Entry->NodeOrdinal];

            if (NodeFence == 0 && Entry->NodeOrdinal == 0)
                NodeFence = CompletedFenceId;

            if (NodeFence != 0 &&
                DxgkpFenceIdReached(NodeFence, Entry->SubmissionFenceId))
            {
                RemoveEntryList(Link);
                InsertTailList(&RetireList, Link);
            }

            Link = Next;
        }
    }

    while (!IsListEmpty(&RetireList))
    {
        PLIST_ENTRY Link = RemoveHeadList(&RetireList);
        InsertTailList(&Adapter->SubmitDmaRetireListHead, Link);
        QueueWorker = TRUE;
    }

    if (!IsListEmpty(&Adapter->SubmitDmaRetireListHead) &&
        InterlockedIncrement(&DxgkpRetireTraceCount) <= 128)
    {
        DXGKRNL_TRACE("DxgkRetireCompletedDmaBuffers: completedFence=%u "
                      "queued retire work state=%d\n",
                      CompletedFenceId,
                      Adapter->State);
    }

    if (QueueWorker &&
        InterlockedCompareExchange(&Adapter->SubmitDmaRetireWorkQueued, 1, 0) == 0)
    {
        QueueWorker = TRUE;
    }
    else
    {
        QueueWorker = FALSE;
    }

    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    if (QueueWorker)
        ExQueueWorkItem(&Adapter->SubmitDmaRetireWorkItem, DelayedWorkQueue);
}

VOID
NTAPI
DxgkReleaseTrackedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY FreeList;
    KIRQL OldIrql;

    if (Adapter == NULL)
        return;

    InitializeListHead(&FreeList);

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    while (!IsListEmpty(&Adapter->SubmitDmaListHead))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Adapter->SubmitDmaListHead);
        InsertTailList(&FreeList, Link);
    }
    while (!IsListEmpty(&Adapter->SubmitDmaRetireListHead))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Adapter->SubmitDmaRetireListHead);
        InsertTailList(&FreeList, Link);
    }
    InterlockedExchange(&Adapter->SubmitDmaRetireWorkQueued, 0);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    while (!IsListEmpty(&FreeList))
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Entry;

        Entry = CONTAINING_RECORD(RemoveHeadList(&FreeList),
                                  DXGKRNL_SUBMIT_DMA_BUFFER,
                                  ListEntry);
        DxgkpFreeTrackedDmaBufferEntry(Adapter, Entry);
    }
}

/* ========================================================================
 * Private helpers
 * ====================================================================== */

/*
 * DxgkpFirstInit
 *
 * Performs one-time global initialisation.  Called from DxgkInitializeEx
 * (and optionally from DriverEntry if dxgkrnl loads as a service).
 *
 * CRITICAL: When dxgkrnl is loaded as an import dependency of a miniport
 * driver (e.g. kmdod matched by CDD), DriverEntry is NOT called by the
 * I/O manager.  The PE loader loads the image and resolves exports but
 * skips DriverEntry.  All global initialization MUST happen here, which
 * is guaranteed to run before any dxgkrnl function is used.
 *
 * Safe to call from multiple simultaneous threads; only the first caller
 * performs the actual work.
 *
 * IRQL: PASSIVE_LEVEL
 */
static VOID
DxgkpFirstInit(VOID)
{
    if (InterlockedCompareExchange(&DxgkpInitialized, 1, 0) != 0)
        return; /* another caller got here first */

    /* Initialize global adapter list and lock. */
    KeInitializeSpinLock(&DxgkAdapterGlobalListLock);
    InitializeListHead(&DxgkAdapterGlobalListHead);
    ExInitializeFastMutex(&DxgkpMapMemoryMutex);
    InitializeListHead(&DxgkpMapMemoryList);

    /* Initialize debug helpers. */
    DxgkDebugInit();

    /* Seed D3DKMT handle cookie. */
    DxgkContextInit();

    /* Bring the panic screen up through the display owner (see above). */
    DxgkpRegisterBugCheckCallback();

    DXGKRNL_TRACE("DxgkpFirstInit: one-time init complete\n");
}

/*
 * DxgkpForwardIrp
 *
 * Skip the current IRP stack location and forward the IRP to the next
 * lower driver in the stack synchronously.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
static NTSTATUS
DxgkpForwardIrp(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PIRP             Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Adapter->LowerDeviceObject, Irp);
}

/*
 * DxgkpAdapterDpcRoutine
 *
 * KDPC callback.  Invoked at DISPATCH_LEVEL by the I/O manager after the
 * ISR requests a DPC.  Calls the miniport's DxgkDdiDpcRoutine.
 *
 * IRQL: DISPATCH_LEVEL
 */
static VOID
NTAPI
DxgkpAdapterDpcRoutine(
    _In_     PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)DeferredContext;
    LONG             Sequence;
    BOOLEAN          Logged;
    ULONGLONG        Start100ns;
    ULONGLONG        ElapsedUs = 0;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Adapter == NULL)
        return;

    Sequence = InterlockedIncrement(&Adapter->DpcCount);
    Logged = (Sequence <= DXGK_TRACE_DPC_LOG_LIMIT);

    if (Logged)
    {
        DXGKRNL_TRACE("DxgkpAdapterDpcRoutine: seq=%ld state=%d irq=%ld queue=%ld t+%I64u us\n",
                      Sequence,
                      Adapter->State,
                      Adapter->InterruptCount,
                      Adapter->QueueDpcCount,
                      DxgkpTraceSinceStartUs(Adapter));
    }

    if (Adapter->MiniportContext->InitData.s.DxgkDdiDpcRoutine != NULL)
    {
        Start100ns = DxgkpTraceNow100ns();
        Adapter->MiniportContext->InitData.s.DxgkDdiDpcRoutine(
            Adapter->MiniportDeviceContext);
        ElapsedUs = DxgkpTraceElapsedUs(Start100ns);

        if (Logged || ElapsedUs >= DXGK_TRACE_SLOW_DPC_US)
        {
            DXGKRNL_TRACE("DxgkpAdapterDpcRoutine: seq=%ld done dur=%I64u us irq=%ld queue=%ld t+%I64u us\n",
                          Sequence,
                          ElapsedUs,
                          Adapter->InterruptCount,
                          Adapter->QueueDpcCount,
                          DxgkpTraceSinceStartUs(Adapter));
        }
    }
    else if (Logged)
    {
        DXGKRNL_TRACE("DxgkpAdapterDpcRoutine: seq=%ld no miniport DPC routine\n",
                      Sequence);
    }

    /* Turn a vblank pulse into a vsync-paced present flush. */
    if (InterlockedExchange(&Adapter->VsyncPending, 0) != 0)
        DxgkDisplayVsyncFlush(Adapter);

    DxgkRetireCompletedDmaBuffers(Adapter);
}

/* Forward declarations for callbacks defined later in this file */
NTSTATUS APIENTRY DxgkCbQueryServices(HANDLE, ULONG, PVOID);
NTSTATUS APIENTRY DxgkCbMapMemory(HANDLE, PHYSICAL_ADDRESS, ULONG, BOOLEAN, BOOLEAN, MEMORY_CACHING_TYPE, PVOID*);
NTSTATUS APIENTRY DxgkCbUnmapMemory(HANDLE, PVOID);
BOOLEAN  APIENTRY DxgkCbQueueDpc(HANDLE);
NTSTATUS APIENTRY DxgkCbReadDeviceSpace(HANDLE, ULONG, PVOID, ULONG, ULONG, PULONG);
NTSTATUS APIENTRY DxgkCbWriteDeviceSpace(HANDLE, ULONG, PVOID, ULONG, ULONG, PULONG);

/* ========================================================================
 * Stub callbacks for DXGK_INTERFACE slots that viogpudo accesses
 *
 * These return error codes rather than being NULL, which prevents
 * NULL pointer dereference crashes when the miniport calls them.
 * ====================================================================== */

/*
 * DxgkCbIsDevicePresent — offset 0x60
 * Deprecated in WDDM 1.0; returns TRUE to indicate the device is still present.
 */
static BOOLEAN
APIENTRY
DxgkCbIsDevicePresent(
    _In_ HANDLE DeviceHandle,
    _In_ PVOID  Unused1,
    _In_ PVOID  Unused2)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Unused1);
    UNREFERENCED_PARAMETER(Unused2);
    DXGKRNL_TRACE("DxgkCbIsDevicePresent: called (stub returning TRUE)\n");
    return TRUE;
}

static PVOID
APIENTRY
DxgkCbGetHandleData(
    _In_ PDXGKARGCB_GETHANDLEDATA HandleData)
{
    if (HandleData == NULL)
        return NULL;

    return DxgkVidMmGetHandleData(HandleData->Type, HandleData->hObject);
}

/*
 * DxgkCbGetHandleParent — offset 0x70
 */
static NTSTATUS
APIENTRY
DxgkCbGetHandleParent(
    _In_  HANDLE DeviceHandle,
    _Out_ PVOID  Result)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Result);
    DXGKRNL_TRACE("DxgkCbGetHandleParent: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbEnumHandleChildren — offset 0x78
 */
static NTSTATUS
APIENTRY
DxgkCbEnumHandleChildren(
    _In_  HANDLE DeviceHandle,
    _Inout_ PVOID EnumData)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(EnumData);
    DXGKRNL_TRACE("DxgkCbEnumHandleChildren: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * VidPN and Monitor interface callbacks (0x90 and 0x98) are now provided by
 * the real implementations in vidpn.c: DxgkCbQueryVidPnInterface and
 * DxgkCbQueryMonitorInterface.  The old stubs have been removed.
 */

/*
 * DxgkCbGetCaptureAddressStub — offset 0xa0
 */
static NTSTATUS
APIENTRY
DxgkCbGetCaptureAddressStub(
    _In_  HANDLE DeviceHandle,
    _Inout_ PVOID CaptureData)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(CaptureData);
    DXGKRNL_TRACE("DxgkCbGetCaptureAddress: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbLogEtwEventStub — offset 0xa8
 * ETW event logging for GPU diagnostics.
 */
static NTSTATUS
APIENTRY
DxgkCbLogEtwEventStub(
    _In_ PVOID EtwEvent)
{
    UNREFERENCED_PARAMETER(EtwEvent);
    /* Silently succeed — ETW is optional */
    return STATUS_SUCCESS;
}

/*
 * DxgkCbExcludeAdapterAccessStub — offset 0xb0
 * Used for VGA arbitration during display mode changes.
 */
static NTSTATUS
APIENTRY
DxgkCbExcludeAdapterAccessStub(
    _In_ HANDLE DeviceHandle,
    _In_ ULONG  Attributes,
    _In_ PVOID  DmaBufferInfo)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(DmaBufferInfo);
    DXGKRNL_TRACE("DxgkCbExcludeAdapterAccess: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbCreateContextAllocationStub — offset 0xb8 (Win8)
 */
static NTSTATUS
APIENTRY
DxgkCbCreateContextAllocationStub(
    _In_  HANDLE DeviceHandle,
    _Inout_ PVOID ContextAllocation)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(ContextAllocation);
    DXGKRNL_TRACE("DxgkCbCreateContextAllocation: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbDestroyContextAllocationStub — offset 0xc0 (Win8)
 */
static NTSTATUS
APIENTRY
DxgkCbDestroyContextAllocationStub(
    _In_ HANDLE DeviceHandle,
    _In_ HANDLE ContextAllocationHandle)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(ContextAllocationHandle);
    DXGKRNL_TRACE("DxgkCbDestroyContextAllocation: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbSetPowerComponentActiveStub — offset 0xc8 (Win8)
 */
static NTSTATUS
APIENTRY
DxgkCbSetPowerComponentActiveStub(
    _In_ HANDLE DeviceHandle,
    _In_ UINT   Component)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Component);
    DXGKRNL_TRACE("DxgkCbSetPowerComponentActive: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbSetPowerComponentIdleStub — offset 0xd0 (Win8)
 */
static NTSTATUS
APIENTRY
DxgkCbSetPowerComponentIdleStub(
    _In_ HANDLE DeviceHandle,
    _In_ UINT   Component)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Component);
    DXGKRNL_TRACE("DxgkCbSetPowerComponentIdle: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbPowerRuntimeControlRequestStub — offset 0xe0 (Win8)
 */
static NTSTATUS
APIENTRY
DxgkCbPowerRuntimeControlRequestStub(
    _In_ HANDLE DeviceHandle,
    _In_ PVOID  PowerControlCode,
    _In_ PVOID  InBuffer,
    _In_ SIZE_T InBufferSize,
    _Out_ PVOID OutBuffer,
    _In_ SIZE_T OutBufferSize,
    _Out_ PSIZE_T BytesReturned)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(PowerControlCode);
    UNREFERENCED_PARAMETER(InBuffer);
    UNREFERENCED_PARAMETER(InBufferSize);
    UNREFERENCED_PARAMETER(OutBuffer);
    UNREFERENCED_PARAMETER(OutBufferSize);
    if (BytesReturned) *BytesReturned = 0;
    DXGKRNL_TRACE("DxgkCbPowerRuntimeControlRequest: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbEvalAcpiMethodStub — offset 0x10
 * ACPI method evaluation for GPU power management.
 */
static NTSTATUS
APIENTRY
DxgkCbEvalAcpiMethodStub(
    _In_  HANDLE DeviceHandle,
    _In_  ULONG  DeviceUid,
    _In_  PVOID  AcpiInputBuffer,
    _In_  ULONG  AcpiInputSize,
    _Out_ PVOID  AcpiOutputBuffer,
    _In_  ULONG  AcpiOutputSize)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(AcpiInputBuffer);
    UNREFERENCED_PARAMETER(AcpiInputSize);
    UNREFERENCED_PARAMETER(AcpiOutputBuffer);
    UNREFERENCED_PARAMETER(AcpiOutputSize);
    DXGKRNL_TRACE("DxgkCbEvalAcpiMethod: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkpFillInterface
 *
 * Populate a DXGK_INTERFACE (DXGKRNL_INTERFACE) structure with the callbacks
 * dxgkrnl provides to the miniport at DxgkDdiStartDevice time.
 *
 * Field order must match the official Windows WDK DXGKRNL_INTERFACE layout.
 * See dispmprt.h for the verified field-by-field offset table.
 *
 * IRQL: PASSIVE_LEVEL
 */
static VOID
DxgkpFillInterface(
    _In_  PDXGKRNL_ADAPTER Adapter,
    _Out_ PDXGK_INTERFACE  Interface)
{
    RtlZeroMemory(Interface, sizeof(*Interface));

    Interface->Size         = sizeof(*Interface);
    Interface->Version      = Adapter->MiniportContext->InitData.s.Version;
    Interface->DeviceHandle = (HANDLE)Adapter;

    DXGKRNL_TRACE("DxgkpFillInterface: DeviceHandle=%p Size=%u Version=%u\n",
                  Interface->DeviceHandle, Interface->Size, Interface->Version);

    /* WDDM 1.0 (Vista) baseline callbacks — correct WDK field order */
    Interface->DxgkCbEvalAcpiMethod                = (PVOID)DxgkCbEvalAcpiMethodStub; /* 0x10 */
    Interface->DxgkCbGetDeviceInformation          = DxgkCbGetDeviceInformation;  /* 0x18 */
    Interface->DxgkCbIndicateChildStatus           = DxgkCbIndicateChildStatus;   /* 0x20 */
    Interface->DxgkCbMapMemory                     = DxgkCbMapMemory;             /* 0x28 */
    Interface->DxgkCbQueueDpc                      = DxgkCbQueueDpc;             /* 0x30 */
    Interface->DxgkCbQueryServices                 = DxgkCbQueryServices;         /* 0x38 */
    Interface->DxgkCbReadDeviceSpace               = DxgkCbReadDeviceSpace;       /* 0x40 */
    Interface->DxgkCbSynchronizeExecution          = DxgkCbSynchronizeExecution;  /* 0x48 */
    Interface->DxgkCbUnmapMemory                   = DxgkCbUnmapMemory;              /* 0x50 */
    Interface->DxgkCbWriteDeviceSpace              = DxgkCbWriteDeviceSpace;      /* 0x58 */
    Interface->DxgkCbIsDevicePresent               = (PVOID)DxgkCbIsDevicePresent;   /* 0x60 */
    Interface->DxgkCbGetHandleData                 = (PVOID)DxgkCbGetHandleData;     /* 0x68 */
    Interface->DxgkCbGetHandleParent               = (PVOID)DxgkCbGetHandleParent;   /* 0x70 */
    Interface->DxgkCbEnumHandleChildren            = (PVOID)DxgkCbEnumHandleChildren; /* 0x78 */
    Interface->DxgkCbNotifyInterrupt               = DxgkCbNotifyInterrupt;       /* 0x80 */
    Interface->DxgkCbNotifyDpc                     = DxgkCbNotifyDpc;             /* 0x88 */
    Interface->DxgkCbQueryVidPnInterface           = (PDXGKCB_QUERYVIDPNINTERFACE)DxgkCbQueryVidPnInterface; /* 0x90 */
    Interface->DxgkCbQueryMonitorInterface         = (PDXGKCB_QUERYMONITORINTERFACE)DxgkCbQueryMonitorInterface; /* 0x98 */
    Interface->DxgkCbGetCaptureAddress             = (PVOID)DxgkCbGetCaptureAddressStub; /* 0xa0 */

    if (Interface->Version >= DXGKDDI_INTERFACE_VERSION_WIN7)
    {
        Interface->DxgkCbLogEtwEvent          = (PVOID)DxgkCbLogEtwEventStub; /* 0xa8 */
        Interface->DxgkCbExcludeAdapterAccess = (PVOID)DxgkCbExcludeAdapterAccessStub; /* 0xb0 */
    }

    if (Interface->Version >= DXGKDDI_INTERFACE_VERSION_WIN8)
    {
        Interface->DxgkCbCreateContextAllocation =
            (PVOID)DxgkCbCreateContextAllocationStub; /* 0xb8 */
        Interface->DxgkCbDestroyContextAllocation =
            (PVOID)DxgkCbDestroyContextAllocationStub; /* 0xc0 */
        Interface->DxgkCbSetPowerComponentActive =
            (PVOID)DxgkCbSetPowerComponentActiveStub; /* 0xc8 */
        Interface->DxgkCbSetPowerComponentIdle =
            (PVOID)DxgkCbSetPowerComponentIdleStub; /* 0xd0 */
        Interface->DxgkCbAcquirePostDisplayOwnership =
            DxgkCbAcquirePostDisplayOwnership; /* 0xd8 */
        Interface->DxgkCbPowerRuntimeControlRequest =
            (PVOID)DxgkCbPowerRuntimeControlRequestStub; /* 0xe0 */
    }

#ifdef DXGKDDI_INTERFACE_VERSION_WDDM2_4
    if (Interface->Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    {
        Interface->DxgkCbAllocateContiguousMemory =
            DxgkCbAllocateContiguousMemory; /* 0x170 */
        Interface->DxgkCbFreeContiguousMemory =
            DxgkCbFreeContiguousMemory; /* 0x178 */
    }
#endif

#ifdef DXGKDDI_INTERFACE_VERSION_WDDM2_9
    if (Interface->Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    {
        Interface->DxgkCbMapPhysicalMemory =
            DxgkCbMapPhysicalMemory; /* 0x200 */
        Interface->DxgkCbUnmapPhysicalMemory =
            DxgkCbUnmapPhysicalMemory; /* 0x208 */
    }
#endif
}

/*
 * DxgkpHandleToAdapter
 *
 * Validate and dereference a DeviceHandle as a DXGKRNL_ADAPTER pointer.
 * All DxgkCb* callbacks call this helper to convert the opaque handle
 * supplied by the miniport back to the adapter object.
 *
 * Returns the adapter pointer or NULL if the handle is invalid.
 *
 * IRQL: any (the global list walk uses a spinlock)
 */
static PDXGKRNL_ADAPTER
DxgkpHandleToAdapter(
    _In_ HANDLE DeviceHandle)
{
    PLIST_ENTRY      Entry;
    KIRQL            OldIrql;
    PDXGKRNL_ADAPTER Adapter = NULL;

    if (DeviceHandle == NULL)
        return NULL;

    /*
     * DeviceHandle is the raw DXGKRNL_ADAPTER pointer cast to HANDLE.
     * Validate it against the global list to guard against stale handles.
     */
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);

    for (Entry  = DxgkAdapterGlobalListHead.Flink;
         Entry != &DxgkAdapterGlobalListHead;
         Entry  = Entry->Flink)
    {
        PDXGKRNL_ADAPTER Candidate =
            CONTAINING_RECORD(Entry, DXGKRNL_ADAPTER, GlobalAdapterListEntry);
        if (Candidate == (PDXGKRNL_ADAPTER)DeviceHandle)
        {
            Adapter = Candidate;
            break;
        }
    }

    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);
    return Adapter;
}

/* ========================================================================
 * DxgkCb* callbacks — exported to the miniport via DXGK_INTERFACE
 * ====================================================================== */

/*
 * DxgkCbNotifyInterrupt
 *
 * Called from the miniport's ISR (at DIRQL) to notify dxgkrnl of a GPU
 * interrupt.  Queues a DPC to perform deferred processing.
 *
 * IRQL: DIRQL (called from ISR context)
 */
NTSTATUS
APIENTRY
DxgkCbNotifyInterrupt(
    _In_ HANDLE                                    DeviceHandle,
    _In_ CONST DXGKARGCB_NOTIFY_INTERRUPT_DATA    *NotifyInterruptData)
{
    PDXGKRNL_ADAPTER Adapter;

    /*
     * DeviceHandle is set to Adapter at DxgkpFillInterface time; it is
     * valid as long as the adapter is started.  We do not walk the global
     * list here (cannot acquire a spinlock while holding another at DIRQL)
     * — cast directly.
     */
    Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;

    /* Log interrupt type for diagnostics */
    if (NotifyInterruptData)
    {
        static LONG NotifyCount = 0;
        LONG c = InterlockedIncrement(&NotifyCount);
        if (NotifyInterruptData->InterruptType == DXGK_INTERRUPT_DMA_COMPLETED)
        {
            InterlockedExchange((volatile LONG *)&Adapter->LastCompletedSubmissionFenceId,
                                (LONG)NotifyInterruptData->DmaCompleted.SubmissionFenceId);
            InterlockedExchange((volatile LONG *)&Adapter->NodeLastCompletedFenceId[
                                    NotifyInterruptData->DmaCompleted.NodeOrdinal %
                                    DXGK_MAX_TRACKED_NODES],
                                (LONG)NotifyInterruptData->DmaCompleted.SubmissionFenceId);
        }
        else if (NotifyInterruptData->InterruptType == DXGK_INTERRUPT_DMA_PREEMPTED)
        {
            InterlockedExchange((volatile LONG *)&Adapter->LastCompletedSubmissionFenceId,
                                (LONG)NotifyInterruptData->DmaPreempted.LastCompletedFenceId);
        }
        else if (NotifyInterruptData->InterruptType == DXGK_INTERRUPT_CRTC_VSYNC)
        {
            /* Vblank pulse: the adapter DPC flushes pending dirty rects. */
            InterlockedExchange(&Adapter->VsyncPending, 1);
        }
        if (c <= 10)
        {
            /* Can't use DPRINT1 at DIRQL safely — just count */
        }

        /* Forward DMA completion/preemption to the VidSch engine state machine. */
        VidSchNotifyInterrupt(Adapter, NotifyInterruptData);
    }
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    /* Queue the DPC; the DPC routine will call DxgkDdiDpcRoutine. */
    {
        static LONG NotifyCount = 0;
        InterlockedIncrement(&NotifyCount);
    }
    KeInsertQueueDpc(&Adapter->DpcObject, NULL, NULL);

    return STATUS_SUCCESS;
}

/*
 * DxgkCbNotifyDpc
 *
 * Called from the miniport's DPC routine to signal dxgkrnl that deferred
 * GPU work is complete.  Sets the synchronisation event used by
 * DxgkCbSynchronizeExecution and vidmm fence waits.
 *
 * IRQL: DISPATCH_LEVEL
 */
VOID
APIENTRY
DxgkCbNotifyDpc(
    _In_ HANDLE DeviceHandle)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;
    if (Adapter == NULL)
        return;

    KeSetEvent(&Adapter->SyncEvent, IO_NO_INCREMENT, FALSE);
}

/*
 * DxgkCbGetDeviceInformation
 *
 * Fills in DXGK_DEVICE_INFO for the miniport.  Called during
 * DxgkDdiStartDevice before the miniport probes hardware.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbGetDeviceInformation(
    _In_  HANDLE            DeviceHandle,
    _Out_ PDXGK_DEVICE_INFO DeviceInformation)
{
    PDXGKRNL_ADAPTER Adapter;
    ULONG            BusNumber = 0;
    ULONGLONG        TotalStart100ns;
    ULONGLONG        PropertyStart100ns;
    ULONGLONG        PropertyUs;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    DXGKRNL_TRACE("DxgkCbGetDeviceInformation: handle=%p DeviceInfo=%p\n",
                  DeviceHandle, DeviceInformation);

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbGetDeviceInformation: invalid handle %p\n",
                    DeviceHandle);
        return STATUS_INVALID_HANDLE;
    }

    DXGKRNL_TRACE("DxgkCbGetDeviceInformation: Adapter=%p PDO=%p\n",
                  Adapter, Adapter->PhysicalDeviceObject);

    RtlZeroMemory(DeviceInformation, sizeof(*DeviceInformation));

    DeviceInformation->PhysicalDeviceObject = Adapter->PhysicalDeviceObject;
    DeviceInformation->MiniportRegistryPath = &Adapter->MiniportContext->RegistryPath;
    DeviceInformation->TranslatedResourceList = Adapter->TranslatedResources;

    /* Query the PCI bus number for the GPU's PDO. */
    {
        ULONG ResultLength = 0;

        PropertyStart100ns = DxgkpTraceNow100ns();
        IoGetDeviceProperty(Adapter->PhysicalDeviceObject,
                            DevicePropertyBusNumber,
                            sizeof(BusNumber),
                            &BusNumber,
                            &ResultLength);
        PropertyUs = DxgkpTraceElapsedUs(PropertyStart100ns);
    }
    DeviceInformation->SystemIoBusNumber  = BusNumber;
    DeviceInformation->AdapterInterfaceType = PCIBus;
    DeviceInformation->DmaAddressWidth = 64; /* VirtIO GPU supports 64-bit DMA */
    DeviceInformation->BusInterruptVector = Adapter->InterruptVector;
    DeviceInformation->BusInterruptLevel = (ULONG)Adapter->InterruptLevel;
    DeviceInformation->InterruptMode = Adapter->InterruptMode;

    /*
     * Report system memory size in MB.  Use SharedUserData which is always
     * accessible from kernel mode without a syscall.
     */
    DeviceInformation->SystemMemorySize = (ULONG)(SharedUserData->NumberOfPhysicalPages >> (20 - PAGE_SHIFT));

    DXGKRNL_TRACE("DxgkCbGetDeviceInformation: PDO %p Bus %lu SysMem=%luMB TransRes=%p\n",
                  Adapter->PhysicalDeviceObject, BusNumber,
                  DeviceInformation->SystemMemorySize,
                  DeviceInformation->TranslatedResourceList);
    DXGKRNL_TRACE("DxgkCbGetDeviceInformation: property=%I64u us total=%I64u us\n",
                  PropertyUs,
                  DxgkpTraceElapsedUs(TotalStart100ns));

    return STATUS_SUCCESS;
}

/*
 * DxgkCbAllocateContiguousMemory
 *
 * Allocates a physically contiguous block of memory on behalf of the
 * miniport.  Wraps MmAllocateContiguousMemorySpecifyCache.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbAllocateContiguousMemory(
    _In_    HANDLE  DeviceHandle,
    _Inout_ PVOID   AllocContiguousMemoryArg)
{
    PDXGKARGCB_ALLOCATECONTIGUOUSMEMORY AllocContiguousMemory =
        (PDXGKARGCB_ALLOCATECONTIGUOUSMEMORY)AllocContiguousMemoryArg;
    PHYSICAL_ADDRESS HighAddr;
    PVOID            Va;
    ULONGLONG        TotalStart100ns;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    UNREFERENCED_PARAMETER(DeviceHandle);

    if (AllocContiguousMemory == NULL || AllocContiguousMemory->NumberOfBytes == 0)
        return STATUS_INVALID_PARAMETER;

    /*
     * Use the highest-acceptable address from the caller; if not set
     * default to the full 64-bit range (any physical address is OK).
     */
    HighAddr = AllocContiguousMemory->HighestAcceptableAddress;
    if (HighAddr.QuadPart == 0)
        HighAddr.QuadPart = (LONGLONG)-1; /* ~0ULL */

    Va = MmAllocateContiguousMemorySpecifyCache(
             AllocContiguousMemory->NumberOfBytes,
             AllocContiguousMemory->LowestAcceptableAddress,
             HighAddr,
             AllocContiguousMemory->BoundaryAddressMultiple,
             MmWriteCombined);

    if (Va == NULL)
    {
        DXGKRNL_ERR("DxgkCbAllocateContiguousMemory: failed "
                    "NumberOfBytes=%Iu\n",
                    AllocContiguousMemory->NumberOfBytes);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    AllocContiguousMemory->pContiguousMemoryAddress = Va;

    DXGKRNL_TRACE("DxgkCbAllocateContiguousMemory: VA=%p "
                  "NumberOfBytes=%Iu total=%I64u us\n",
                  Va,
                  AllocContiguousMemory->NumberOfBytes,
                  DxgkpTraceElapsedUs(TotalStart100ns));

    return STATUS_SUCCESS;
}

/*
 * DxgkCbFreeContiguousMemory
 *
 * Releases memory allocated by DxgkCbAllocateContiguousMemory.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbFreeContiguousMemory(
    _In_ HANDLE  DeviceHandle,
    _In_ PVOID   FreeContiguousMemoryArg)
{
    PDXGKARGCB_FREECONTIGUOUSMEMORY FreeContiguousMemory =
        (PDXGKARGCB_FREECONTIGUOUSMEMORY)FreeContiguousMemoryArg;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceHandle);

    if (FreeContiguousMemory == NULL ||
        FreeContiguousMemory->pContiguousMemoryAddress == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DXGKRNL_TRACE("DxgkCbFreeContiguousMemory: VA=%p\n",
                  FreeContiguousMemory->pContiguousMemoryAddress);

    MmFreeContiguousMemory(FreeContiguousMemory->pContiguousMemoryAddress);

    return STATUS_SUCCESS;
}

/*
 * DxgkCbMapMemory
 *
 * WDDM 1.0 callback — maps a range of translated physical addresses
 * (BAR regions) into kernel virtual address space or user-mode address
 * space.  This is the primary memory-mapping callback used by WDDM 1.x
 * miniport drivers to access GPU MMIO registers and frame buffers.
 *
 * CRITICAL (ReactOS amd64): MmMapIoSpace system PTEs share the same VA
 * range as kernel thread stacks.  When called from the PnP thread during
 * DxgkDdiStartDevice, the allocated VA can land WITHIN the calling
 * thread's stack.  The miniport's deep init code then overwrites the
 * BAR mapping with stack frames, causing a page fault.
 *
 * Fix: call MmMapIoSpace from a system worker thread whose stack is
 * in a different VA region, so the returned VA is away from the
 * StartDevice thread's stack.
 *
 * Unlike DxgkCbMapPhysicalMemory (WDDM 2.9), this callback takes
 * individual parameters rather than a structure pointer.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbMapMemory(
    _In_  HANDLE              DeviceHandle,
    _In_  PHYSICAL_ADDRESS    TranslatedAddress,
    _In_  ULONG               Length,
    _In_  BOOLEAN             InIoSpace,
    _In_  BOOLEAN             MapToUserMode,
    _In_  MEMORY_CACHING_TYPE CacheType,
    _Out_ PVOID              *VirtualAddress)
{
    PVOID Va;
    ULONGLONG TotalStart100ns;
    ULONGLONG MapStart100ns;
    ULONGLONG MapUs = 0;
    PCSTR     MapMethod = "mmmapiospace";

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    UNREFERENCED_PARAMETER(DeviceHandle);

    if (VirtualAddress == NULL || Length == 0)
        return STATUS_INVALID_PARAMETER;

    *VirtualAddress = NULL;

    DXGKRNL_TRACE("DxgkCbMapMemory: enter PA=0x%I64X Len=0x%lX IoSpace=%d UserMode=%d Cache=%d\n",
                  TranslatedAddress.QuadPart, Length, InIoSpace, MapToUserMode, CacheType);

    if (InIoSpace)
    {
        /*
         * I/O port mapping: not typical for modern GPU devices but must
         * be handled.  On amd64 I/O ports are accessed directly, but
         * MmMapIoSpace still works for the mapping.
         */
        MapStart100ns = DxgkpTraceNow100ns();
        Va = MmMapIoSpace(TranslatedAddress, Length, MmNonCached);
        MapUs = DxgkpTraceElapsedUs(MapStart100ns);
        MapMethod = "iospace";
    }
    else if (MapToUserMode)
    {
        /*
         * User-mode mapping: not supported in our initial implementation.
         * Real Windows dxgkrnl would create an MDL, probe-and-lock, then
         * MmMapLockedPagesSpecifyCache with UserMode.  Return failure for
         * now; miniports that need user-mode mapping will fail gracefully.
         */
        DXGKRNL_ERR("DxgkCbMapMemory: user-mode mapping not supported\n");
        return STATUS_NOT_SUPPORTED;
    }
    else
    {
#if defined(_M_ARM64)
        /*
         * ARM64: a device BAR (MMIO) MUST be mapped as Device memory so the
         * miniport's register accesses keep strict MMIO ordering and are not
         * speculated or write-combined.  The MDL + MmMapLockedPagesSpecifyCache
         * route used on amd64 (below) yields a Normal-NonCacheable mapping,
         * which breaks virtio register handshakes — the genuine viogpudo
         * StartDevice negotiates the virtio-gpu over this BAR and times out
         * (INSUFFICIENT_RESOURCES) when it is not Device memory.  MmMapIoSpace
         * gives a proper Device mapping here; the amd64 system-PTE collision
         * the MDL route works around does not apply on ARM64.
         */
        MapStart100ns = DxgkpTraceNow100ns();
        Va = MmMapIoSpace(TranslatedAddress, Length, MmNonCached);
        MapUs = DxgkpTraceElapsedUs(MapStart100ns);
        MapMethod = "iospace-device";
#else
        /*
         * On amd64/UEFI, MmMapIoSpace can return a system-PTE VA in the
         * same FFFFF880... range that the current kernel stack expands into.
         * For viogpudo StartDevice this can place the BAR mapping directly
         * in the path of deeper stack growth and trigger recursive faults.
         *
         * Build an MDL over the device PFNs and map it through
         * MmMapLockedPagesSpecifyCache instead. That uses a different
         * allocator and keeps the returned BAR VA out of the colliding
         * system-PTE window.
         */
        PMDL                Mdl;
        PVOID               BaseVa;
        ULONG               Offset;
        ULONG               PageCount;
        PFN_NUMBER          FirstPfn;
        PDXGK_MAPMEM_ENTRY  Entry;

        Va = NULL;
        Offset = BYTE_OFFSET(TranslatedAddress.LowPart);
        PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Offset, Length);
        FirstPfn = (PFN_NUMBER)(TranslatedAddress.QuadPart >> PAGE_SHIFT);

        MapStart100ns = DxgkpTraceNow100ns();
        Mdl = IoAllocateMdl(NULL,
                            PageCount << PAGE_SHIFT,
                            FALSE,
                            FALSE,
                            NULL);
        if (Mdl != NULL)
        {
            PPFN_NUMBER Pages;
            ULONG       i;

            Pages = MmGetMdlPfnArray(Mdl);
            for (i = 0; i < PageCount; ++i)
                Pages[i] = FirstPfn + i;

            Mdl->MdlFlags |= MDL_PAGES_LOCKED;

            BaseVa = MmMapLockedPagesSpecifyCache(Mdl,
                                                  KernelMode,
                                                  CacheType,
                                                  NULL,
                                                  FALSE,
                                                  NormalPagePriority);
            if (BaseVa != NULL)
            {
                Entry = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(*Entry),
                                              TAG_DXGK_RESOURCES);
                if (Entry != NULL)
                {
                    Va = (PVOID)((ULONG_PTR)BaseVa + Offset);
                    Entry->VirtualAddress = Va;
                    Entry->BaseAddress = BaseVa;
                    Entry->Mdl = Mdl;

                    ExAcquireFastMutex(&DxgkpMapMemoryMutex);
                    InsertTailList(&DxgkpMapMemoryList, &Entry->ListEntry);
                    ExReleaseFastMutex(&DxgkpMapMemoryMutex);
                    MapMethod = "mdl";
                }
                else
                {
                    MmUnmapLockedPages(BaseVa, Mdl);
                    IoFreeMdl(Mdl);
                }
            }
            else
            {
                IoFreeMdl(Mdl);
            }
        }

        if (Va == NULL)
        {
            Va = MmMapIoSpace(TranslatedAddress, Length, CacheType);
            MapMethod = "mmmapiospace-fallback";
        }

        MapUs = DxgkpTraceElapsedUs(MapStart100ns);
#endif /* _M_ARM64 */
    }

    if (Va == NULL)
    {
        DXGKRNL_ERR("DxgkCbMapMemory: MmMapIoSpace failed PA=0x%I64X Len=0x%lX\n",
                     TranslatedAddress.QuadPart, Length);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *VirtualAddress = Va;

    DXGKRNL_TRACE("DxgkCbMapMemory: PA=0x%I64X -> VA=%p Len=0x%lX IoSpace=%d UserMode=%d Cache=%d via=%s map=%I64u us total=%I64u us\n",
                  TranslatedAddress.QuadPart, Va, Length, InIoSpace, MapToUserMode, CacheType,
                  MapMethod, MapUs, DxgkpTraceElapsedUs(TotalStart100ns));

    return STATUS_SUCCESS;
}

/*
 * DxgkCbUnmapMemory (WDDM 1.0 version)
 *
 * Unmaps an address range previously mapped by DxgkCbMapMemory.
 * Takes a HANDLE + PVOID (simpler than the WDDM 2.9 struct-based variant).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbUnmapMemory(
    _In_ HANDLE DeviceHandle,
    _In_ PVOID  VirtualAddress)
{
    PLIST_ENTRY Entry;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceHandle);

    if (VirtualAddress == NULL)
        return STATUS_INVALID_PARAMETER;

    DXGKRNL_TRACE("DxgkCbUnmapMemory: VA=%p\n", VirtualAddress);

    ExAcquireFastMutex(&DxgkpMapMemoryMutex);
    for (Entry = DxgkpMapMemoryList.Flink;
         Entry != &DxgkpMapMemoryList;
         Entry = Entry->Flink)
    {
        PDXGK_MAPMEM_ENTRY MapEntry =
            CONTAINING_RECORD(Entry, DXGK_MAPMEM_ENTRY, ListEntry);

        if (MapEntry->VirtualAddress == VirtualAddress)
        {
            RemoveEntryList(&MapEntry->ListEntry);
            ExReleaseFastMutex(&DxgkpMapMemoryMutex);

            MmUnmapLockedPages(MapEntry->BaseAddress, MapEntry->Mdl);
            IoFreeMdl(MapEntry->Mdl);
            ExFreePoolWithTag(MapEntry, TAG_DXGK_RESOURCES);
            return STATUS_SUCCESS;
        }
    }
    ExReleaseFastMutex(&DxgkpMapMemoryMutex);

    /*
     * MmUnmapIoSpace requires the byte count, but the WDDM 1.0
     * DxgkCbUnmapMemory callback does not receive it.  Windows dxgkrnl
     * maintains an internal tracking table for this purpose.
     *
     * As a practical workaround we pass 0 to MmUnmapIoSpace.  On ReactOS
     * the implementation will look up the mapping size from the PTE range.
     * This matches the behavior of videoprt's VideoPortUnmapMemory.
     */
    MmUnmapIoSpace(VirtualAddress, 0);

    return STATUS_SUCCESS;
}

/*
 * DxgkCbQueueDpc
 *
 * Called from the miniport's ISR (at any IRQL) to queue a DPC that will
 * invoke DxgkDdiDpcRoutine at DISPATCH_LEVEL.  Returns TRUE if the DPC
 * was queued, FALSE if one was already pending.
 *
 * IRQL: Any level (called from ISR)
 */
BOOLEAN
APIENTRY
DxgkCbQueueDpc(
    _In_ HANDLE DeviceHandle)
{
    PDXGKRNL_ADAPTER Adapter;
    BOOLEAN          Queued;
    LONG             Sequence;

    Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;
    if (Adapter == NULL)
        return FALSE;

    Sequence = InterlockedIncrement(&Adapter->QueueDpcCount);
    Queued = KeInsertQueueDpc(&Adapter->DpcObject, NULL, NULL);

    if (Sequence <= DXGK_TRACE_DPC_LOG_LIMIT)
    {
        DXGKRNL_TRACE("DxgkCbQueueDpc: seq=%ld queued=%d state=%d irq=%ld dpc=%ld t+%I64u us\n",
                      Sequence,
                      Queued,
                      Adapter->State,
                      Adapter->InterruptCount,
                      Adapter->DpcCount,
                      DxgkpTraceSinceStartUs(Adapter));
    }

    return Queued;
}

/*
 * DxgkCbReadDeviceSpace
 *
 * Reads from the PCI configuration space of the display adapter.
 * Wraps the HalGetBusDataByOffset / IoGetDeviceProperty mechanism.
 *
 * DataType values:
 *   DXGK_WHICHSPACE_CONFIG (1) — adapter's own PCI config space
 *   DXGK_WHICHSPACE_BRIDGE (0) — parent bridge config space
 *   DXGK_WHICHSPACE_ROM    (3) — expansion ROM
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbReadDeviceSpace(
    _In_  HANDLE  DeviceHandle,
    _In_  ULONG   DataType,
    _In_  PVOID   Buffer,
    _In_  ULONG   Offset,
    _In_  ULONG   Length,
    _Out_ PULONG  BytesRead)
{
    PDXGKRNL_ADAPTER Adapter;
    ULONG            BytesTransferred;
    ULONGLONG        TotalStart100ns;
    ULONGLONG        ElapsedUs;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    if (Buffer == NULL || BytesRead == NULL || Length == 0)
        return STATUS_INVALID_PARAMETER;

    *BytesRead = 0;

    /*
     * Direct-cast the DeviceHandle instead of walking the global adapter
     * list with DxgkpHandleToAdapter().  The global list walk acquires
     * DxgkAdapterGlobalListLock at DISPATCH_LEVEL.  IoGetDeviceProperty
     * (which we previously used below) sends PnP IRPs that re-enter this
     * driver, and on ReactOS the IRP dispatch path can trigger a spinlock
     * re-entrancy check (SPIN_LOCK_ALREADY_OWNED bugcheck).
     *
     * Since the DeviceHandle is set by DxgkpFillInterface to the Adapter
     * pointer and remains valid for the entire adapter lifetime, a direct
     * cast is safe and avoids the spinlock entirely.
     */
    Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbReadDeviceSpace: NULL handle\n");
        return STATUS_INVALID_HANDLE;
    }

    /* No per-read trace — viogpudo does hundreds of reads during StartDevice
     * and the serial output overhead causes stack overflow. */

    /*
     * Read from the adapter's PCI configuration space using
     * HalGetBusDataByOffset.
     *
     * Windows display miniports in the field have been observed issuing
     * their initial PCI header probe with DataType 0. Accept both the
     * CONFIG and BRIDGE codes here and route them to the adapter's own
     * PCI config space so StartDevice does not fail before display init.
     *
     * Use the cached PCI bus/slot from DxgkAdapterStart to avoid calling
     * IoGetDeviceProperty, which sends PnP IRPs and can cause spinlock
     * re-entrancy on ReactOS.
     */
    if (DataType == DXGK_WHICHSPACE_CONFIG ||
        DataType == DXGK_WHICHSPACE_BRIDGE)
    {
        if (!Adapter->PciBusSlotCached)
        {
            DXGKRNL_ERR("DxgkCbReadDeviceSpace: PCI bus/slot not yet cached\n");
            return STATUS_DEVICE_NOT_READY;
        }

        BytesTransferred = HalGetBusDataByOffset(
            PCIConfiguration,
            Adapter->PciBusNumber,
            Adapter->PciSlotNumber.u.AsULONG,
            Buffer,
            Offset,
            Length);

        if (BytesTransferred == 0)
        {
            DXGKRNL_ERR("DxgkCbReadDeviceSpace: HalGetBusDataByOffset failed\n");
            return STATUS_UNSUCCESSFUL;
        }

        *BytesRead = BytesTransferred;
        ElapsedUs = DxgkpTraceElapsedUs(TotalStart100ns);
        if (ElapsedUs >= DXGK_TRACE_SLOW_CONFIG_ACCESS_US)
        {
            DXGKRNL_WARN("DxgkCbReadDeviceSpace: slow config read Off=0x%lX Len=0x%lX Bytes=%lu took %I64u us\n",
                         Offset, Length, BytesTransferred, ElapsedUs);
        }
        return STATUS_SUCCESS;
    }

    /* Other space types not yet supported. */
    DXGKRNL_WARN("DxgkCbReadDeviceSpace: unsupported DataType %lu\n", DataType);
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbWriteDeviceSpace
 *
 * Writes to the PCI configuration space of the display adapter.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbWriteDeviceSpace(
    _In_  HANDLE  DeviceHandle,
    _In_  ULONG   DataType,
    _In_  PVOID   Buffer,
    _In_  ULONG   Offset,
    _In_  ULONG   Length,
    _Out_ PULONG  BytesWritten)
{
    PDXGKRNL_ADAPTER Adapter;
    ULONG            BytesTransferred;
    ULONGLONG        TotalStart100ns;
    ULONGLONG        ElapsedUs;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    if (Buffer == NULL || BytesWritten == NULL || Length == 0)
        return STATUS_INVALID_PARAMETER;

    *BytesWritten = 0;

    /* Direct-cast for the same reasons as DxgkCbReadDeviceSpace. */
    Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbWriteDeviceSpace: NULL handle\n");
        return STATUS_INVALID_HANDLE;
    }

    /* No per-write trace to avoid stack overflow from serial output. */

    /*
     * Mirror the read path compatibility handling: accept both CONFIG and
     * BRIDGE codes for adapter PCI config writes.
     */
    if (DataType == DXGK_WHICHSPACE_CONFIG ||
        DataType == DXGK_WHICHSPACE_BRIDGE)
    {
        if (!Adapter->PciBusSlotCached)
        {
            DXGKRNL_ERR("DxgkCbWriteDeviceSpace: PCI bus/slot not yet cached\n");
            return STATUS_DEVICE_NOT_READY;
        }

        BytesTransferred = HalSetBusDataByOffset(
            PCIConfiguration,
            Adapter->PciBusNumber,
            Adapter->PciSlotNumber.u.AsULONG,
            Buffer,
            Offset,
            Length);

        if (BytesTransferred == 0)
        {
            DXGKRNL_ERR("DxgkCbWriteDeviceSpace: HalSetBusDataByOffset failed\n");
            return STATUS_UNSUCCESSFUL;
        }

        *BytesWritten = BytesTransferred;
        ElapsedUs = DxgkpTraceElapsedUs(TotalStart100ns);
        if (ElapsedUs >= DXGK_TRACE_SLOW_CONFIG_ACCESS_US)
        {
            DXGKRNL_WARN("DxgkCbWriteDeviceSpace: slow config write Off=0x%lX Len=0x%lX Bytes=%lu took %I64u us\n",
                         Offset, Length, BytesTransferred, ElapsedUs);
        }
        return STATUS_SUCCESS;
    }

    DXGKRNL_WARN("DxgkCbWriteDeviceSpace: unsupported DataType %lu\n", DataType);
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbMapPhysicalMemory
 *
 * Maps a physical address range into kernel virtual address space.
 * Uses MmNonCached because GPU MMIO registers must not be cached.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbMapPhysicalMemory(
    _In_    HANDLE  DeviceHandle,
    _Inout_ PVOID   MapPhysicalMemoryArg)
{
    PDXGKARGCB_MAPPHYSICALMEMORY MapPhysicalMemory =
        (PDXGKARGCB_MAPPHYSICALMEMORY)MapPhysicalMemoryArg;
    PVOID Va;
    ULONGLONG TotalStart100ns;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    UNREFERENCED_PARAMETER(DeviceHandle);

    if (MapPhysicalMemory == NULL || MapPhysicalMemory->NumberOfBytes == 0)
        return STATUS_INVALID_PARAMETER;

    Va = MmMapIoSpace(MapPhysicalMemory->PhysicalAddress,
                      MapPhysicalMemory->NumberOfBytes,
                      MmNonCached);
    if (Va == NULL)
    {
        DXGKRNL_ERR("DxgkCbMapPhysicalMemory: MmMapIoSpace failed "
                    "PA=0x%I64X Len=%Iu\n",
                    MapPhysicalMemory->PhysicalAddress.QuadPart,
                    MapPhysicalMemory->NumberOfBytes);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    MapPhysicalMemory->pVirtualAddress = Va;

    DXGKRNL_TRACE("DxgkCbMapPhysicalMemory: PA=0x%I64X -> VA=%p Len=%Iu total=%I64u us\n",
                  MapPhysicalMemory->PhysicalAddress.QuadPart, Va,
                  MapPhysicalMemory->NumberOfBytes,
                  DxgkpTraceElapsedUs(TotalStart100ns));

    return STATUS_SUCCESS;
}

/*
 * DxgkCbUnmapPhysicalMemory
 *
 * Unmaps a range mapped by DxgkCbMapPhysicalMemory.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbUnmapPhysicalMemory(
    _In_ HANDLE  DeviceHandle,
    _In_ PVOID   UnmapPhysicalMemoryArg)
{
    PDXGKARGCB_UNMAP_PHYSICAL_MEMORY UnmapPhysicalMemory =
        (PDXGKARGCB_UNMAP_PHYSICAL_MEMORY)UnmapPhysicalMemoryArg;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceHandle);

    if (UnmapPhysicalMemory == NULL ||
        UnmapPhysicalMemory->pVirtualAddress == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DXGKRNL_TRACE("DxgkCbUnmapPhysicalMemory: VA=%p Len=%Iu\n",
                  UnmapPhysicalMemory->pVirtualAddress,
                  UnmapPhysicalMemory->NumberOfBytes);

    MmUnmapIoSpace(UnmapPhysicalMemory->pVirtualAddress,
                   UnmapPhysicalMemory->NumberOfBytes);

    return STATUS_SUCCESS;
}

/*
 * DxgkCbIndicateChildStatus
 *
 * Called by the miniport when a child device's connection status changes
 * (e.g. a monitor is hot-plugged or removed).  Invalidates the bus
 * relations for the FDO, causing the PnP manager to re-enumerate children.
 * Also triggers a VidPN rebuild to update the display topology.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbIndicateChildStatus(
    _In_ HANDLE              DeviceHandle,
    _In_ PDXGK_CHILD_STATUS  ChildStatus)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbIndicateChildStatus: invalid handle %p\n",
                    DeviceHandle);
        return STATUS_INVALID_HANDLE;
    }

    DXGKRNL_TRACE("DxgkCbIndicateChildStatus: adapter %p type %d uid %lu "
                  "connected=%d\n",
                  Adapter,
                  ChildStatus ? ChildStatus->Type : -1,
                  ChildStatus ? ChildStatus->ChildUid : 0,
                  (ChildStatus && ChildStatus->Type == StatusConnection)
                      ? ChildStatus->HotPlug.Connected : -1);

    /* Trigger VidPN topology rebuild for connector state changes. */
    if (ChildStatus != NULL && ChildStatus->Type == StatusConnection)
    {
        DxgkVidPnRebuildForHotPlug(Adapter);
    }

    IoInvalidateDeviceRelations(Adapter->PhysicalDeviceObject, BusRelations);

    return STATUS_SUCCESS;
}

/*
 * DxgkCbQueryServices
 *
 * Returns an interface for the requested service type. Used by miniports
 * to obtain DMA adapter, AGP, or debug report interfaces.
 */
NTSTATUS
APIENTRY
DxgkCbQueryServices(
    _In_  HANDLE  DeviceHandle,
    _In_  ULONG   ServicesType,   /* DXGK_SERVICES enum */
    _Inout_ PVOID Interface)
{
    DXGKRNL_TRACE("DxgkCbQueryServices: handle=%p type=%lu iface=%p\n",
                  DeviceHandle, ServicesType, Interface);

    /*
     * DXGK_SERVICES enum values:
     *   0 = DxgkServicesAgp          (AGP interface)
     *   1 = DxgkServicesBusInterface (PCI bus interface — internal)
     *   2 = DxgkServicesDebugReport  (debug report interface)
     *   3 = DxgkServicesTimedOperation
     *   4 = DxgkServicesSPB
     *   5 = DxgkServicesFirmwareTable
     *
     * For DOD drivers, none of these are required.  Full WDDM drivers
     * may use AGP or debug report.  Return NOT_SUPPORTED for now;
     * miniports that need these services will fall back gracefully.
     */
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Interface);

    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbSynchronizeExecution
 *
 * Runs SynchronizeRoutine in the context of the adapter's interrupt ISR
 * (i.e. at the interrupt's IRQL with the interrupt spinlock held), then
 * returns the routine's boolean result in *ReturnValue.
 *
 * If the adapter has no interrupt object registered, calls the routine
 * directly at the current IRQL (safe because the miniport must not call
 * us with a non-trivial routine in that case).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbSynchronizeExecution(
    _In_  HANDLE                  DeviceHandle,
    _In_  PKSYNCHRONIZE_ROUTINE   SynchronizeRoutine,
    _In_  PVOID                   Context,
    _In_  ULONG                   MessageNumber,
    _Out_ PBOOLEAN                ReturnValue)
{
    PDXGKRNL_ADAPTER Adapter;
    PKINTERRUPT InterruptObject = NULL;
    ULONGLONG        TotalStart100ns;
    ULONGLONG        ElapsedUs;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbSynchronizeExecution: invalid handle %p\n",
                    DeviceHandle);
        if (ReturnValue) *ReturnValue = FALSE;
        return STATUS_INVALID_HANDLE;
    }

    if (ReturnValue == NULL)
    {
        DXGKRNL_ERR("DxgkCbSynchronizeExecution: NULL ReturnValue pointer\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (SynchronizeRoutine == NULL)
    {
        *ReturnValue = TRUE;
        return STATUS_SUCCESS;
    }

    if (Adapter->InterruptMessageTable != NULL &&
        MessageNumber < Adapter->InterruptMessageTable->MessageCount)
    {
        InterruptObject =
            Adapter->InterruptMessageTable->MessageInfo[MessageNumber].InterruptObject;
    }

    if (InterruptObject == NULL)
        InterruptObject = Adapter->InterruptObject;

    if (InterruptObject != NULL)
    {
        *ReturnValue = KeSynchronizeExecution(InterruptObject,
                                              SynchronizeRoutine,
                                              Context);
    }
    else
    {
        /* No interrupt; call directly at current IRQL. */
        *ReturnValue = SynchronizeRoutine(Context);
    }

    ElapsedUs = DxgkpTraceElapsedUs(TotalStart100ns);
    if (ElapsedUs >= DXGK_TRACE_SLOW_SYNC_US)
    {
        DXGKRNL_WARN("DxgkCbSynchronizeExecution: slow sync Message=%lu Interrupt=%p took %I64u us\n",
                     MessageNumber, InterruptObject, ElapsedUs);
    }

    return STATUS_SUCCESS;
}

/*
 * DxgkCbAcquirePostDisplayOwnership
 *
 * Called by the miniport during DxgkDdiStartDevice to query the
 * firmware-provided POST framebuffer (EFI GOP) and claim display
 * ownership from the InbV boot-video layer.
 *
 * On return DisplayInformation is filled with the GOP framebuffer
 * geometry.  If no UEFI GOP framebuffer is present, we fall back to
 * detecting a VBE DISPI (bochs-display / QEMU stdvga) device and
 * programming a linear framebuffer mode.  If neither is available
 * all fields are zeroed and STATUS_SUCCESS is still returned (the
 * miniport must then cold-start its display pipeline).
 *
 * IRQL: PASSIVE_LEVEL
 */

/* ---- VBE DISPI register definitions (bochs-display / QEMU stdvga) ---- */
#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF

#define VBE_DISPI_INDEX_ID      0
#define VBE_DISPI_INDEX_XRES    1
#define VBE_DISPI_INDEX_YRES    2
#define VBE_DISPI_INDEX_BPP     3
#define VBE_DISPI_INDEX_ENABLE  4

#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40

#define VBE_DISPI_ID_MIN        0xB0C0
#define VBE_DISPI_ID_MAX        0xB0C5

#define VBE_FALLBACK_WIDTH      1024
#define VBE_FALLBACK_HEIGHT     768
#define VBE_FALLBACK_BPP        32

static USHORT
VbeDispiRead(USHORT Index)
{
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)VBE_DISPI_IOPORT_INDEX, Index);
    return READ_PORT_USHORT((PUSHORT)(ULONG_PTR)VBE_DISPI_IOPORT_DATA);
}

static VOID
VbeDispiWrite(USHORT Index, USHORT Value)
{
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)VBE_DISPI_IOPORT_INDEX, Index);
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)VBE_DISPI_IOPORT_DATA, Value);
}

/**
 * Try to detect and program a VBE DISPI linear framebuffer when booting
 * on BIOS (no UEFI GOP).  Returns TRUE on success.
 */
static BOOLEAN
DxgkpAcquireVbeDisplayOwnership(
    _In_  PDXGKRNL_ADAPTER          Adapter,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInformation)
{
    USHORT DispiId;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc;
    PHYSICAL_ADDRESS FbPhysAddr = {{0}};
    ULONG i;

#if !defined(_M_IX86) && !defined(_M_AMD64)
    /* Legacy port I/O: on ARM64 WRITE_PORT_USHORT((PUSHORT)0x1CE, ...) is a
     * raw store to VA 0x1CE — a NULL-page fault, observed as a boot-killing
     * DABORT when no GOP framebuffer exists. There is no I/O port space to
     * probe; report no VBE device. */
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(DisplayInformation);
    return FALSE;
#endif

    /* Probe VBE DISPI ID register to detect bochs-display */
    DispiId = VbeDispiRead(VBE_DISPI_INDEX_ID);
    if (DispiId < VBE_DISPI_ID_MIN || DispiId > VBE_DISPI_ID_MAX)
    {
        DXGKRNL_TRACE("VBE DISPI ID 0x%04X not recognized\n", DispiId);
        return FALSE;
    }

    DXGKRNL_TRACE("Detected bochs-display DISPI ID 0x%04X\n", DispiId);

    /* Find framebuffer physical address from PCI BAR (first large memory resource) */
    if (!Adapter->TranslatedResources || Adapter->TranslatedResources->Count == 0)
    {
        DXGKRNL_ERR("VBE fallback: no translated resources\n");
        return FALSE;
    }

    FullDesc = &Adapter->TranslatedResources->List[0];
    for (i = 0; i < FullDesc->PartialResourceList.Count; i++)
    {
        Desc = &FullDesc->PartialResourceList.PartialDescriptors[i];
        if (Desc->Type == CmResourceTypeMemory &&
            Desc->u.Memory.Length >= (VBE_FALLBACK_WIDTH * VBE_FALLBACK_HEIGHT * (VBE_FALLBACK_BPP / 8)))
        {
            FbPhysAddr = Desc->u.Memory.Start;
            break;
        }
    }

    if (FbPhysAddr.QuadPart == 0)
    {
        DXGKRNL_ERR("VBE fallback: no suitable memory BAR for framebuffer\n");
        return FALSE;
    }

    /* Program VBE linear framebuffer mode */
    VbeDispiWrite(VBE_DISPI_INDEX_ENABLE, 0);
    VbeDispiWrite(VBE_DISPI_INDEX_XRES, VBE_FALLBACK_WIDTH);
    VbeDispiWrite(VBE_DISPI_INDEX_YRES, VBE_FALLBACK_HEIGHT);
    VbeDispiWrite(VBE_DISPI_INDEX_BPP,  VBE_FALLBACK_BPP);
    VbeDispiWrite(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    DisplayInformation->Width         = VBE_FALLBACK_WIDTH;
    DisplayInformation->Height        = VBE_FALLBACK_HEIGHT;
    DisplayInformation->Pitch         = VBE_FALLBACK_WIDTH * (VBE_FALLBACK_BPP / 8);
    DisplayInformation->ColorFormat   = D3DDDIFMT_X8R8G8B8;
    DisplayInformation->PhysicAddress = FbPhysAddr;
    DisplayInformation->TargetId      = 0;
    DisplayInformation->AcpiId        = 0;

    DXGKRNL_ERR("VBE fallback: %ux%ux%u LFB at %I64X pitch=%u\n",
                VBE_FALLBACK_WIDTH, VBE_FALLBACK_HEIGHT, VBE_FALLBACK_BPP,
                FbPhysAddr.QuadPart, DisplayInformation->Pitch);

    return TRUE;
}

NTSTATUS
APIENTRY
DxgkCbAcquirePostDisplayOwnership(
    _In_  HANDLE  DeviceHandle,
    _Out_ PVOID   DisplayInformationArg)
{
    PDXGK_DISPLAY_INFORMATION    DisplayInformation =
        (PDXGK_DISPLAY_INFORMATION)DisplayInformationArg;
    LOADER_PARAMETER_FRAMEBUFFER Fb;
    D3DDDIFORMAT                 ColorFormat;
    ULONG                        BytesPerPixel;
    ULONGLONG                    TotalStart100ns;
    ULONGLONG                    GopQueryUs = 0;
    ULONGLONG                    OwnershipUs = 0;
    ULONGLONG                    StepStart100ns;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: handle=%p out=%p\n",
                  DeviceHandle, DisplayInformationArg);

    RtlZeroMemory(DisplayInformation, sizeof(*DisplayInformation));

    /*
     * Boot display ownership policy (the MSBDD handover, see
     * g_PostDisplayOwnerAdapter above):
     *   - the basic-display fallback never takes the display away from a
     *     real miniport: it gets an empty descriptor and declines start;
     *   - a real miniport claiming the display stops the current owner
     *     (typically the fallback) before acquiring.
     */
    {
        PDXGKRNL_ADAPTER Claimant = (PDXGKRNL_ADAPTER)DeviceHandle;
        PDXGKRNL_ADAPTER Owner = g_PostDisplayOwnerAdapter;

        if (Claimant != NULL && Owner != NULL && Owner != Claimant)
        {
            if (Claimant->MiniportContext != NULL &&
                Claimant->MiniportContext->IsBasicDisplayFallback &&
                Owner->MiniportContext != NULL &&
                !Owner->MiniportContext->IsBasicDisplayFallback)
            {
                DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: boot display "
                              "already owned by adapter %p — fallback yields\n",
                              Owner);
                return STATUS_SUCCESS;
            }

            DxgkpStopPostDisplayOwner(Owner);
        }
    }

    /*
     * If no valid GOP framebuffer was saved by FreeLOADer / InbV the
     * miniport must initialise its pipeline from scratch.  Return a
     * zeroed structure with STATUS_SUCCESS.
     */
    if (!InbvHasValidGopFrameBuffer())
    {
        PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;

        DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: "
                      "no GOP framebuffer, trying VBE DISPI fallback\n");

        if (DxgkpAcquireVbeDisplayOwnership(Adapter, DisplayInformation))
        {
            DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: "
                          "VBE fallback succeeded total=%I64u us\n",
                          DxgkpTraceElapsedUs(TotalStart100ns));
            g_PostDisplayOwnerAdapter = Adapter;
            return STATUS_SUCCESS;
        }

        /* Expected on headless boots: the miniport cold-starts. */
        DXGKRNL_WARN("DxgkCbAcquirePostDisplayOwnership: "
                     "no GOP and no VBE — miniport must cold-start\n");
        return STATUS_SUCCESS;
    }

    StepStart100ns = DxgkpTraceNow100ns();
    if (!InbvGetGopFrameBufferInfo(&Fb))
    {
        DXGKRNL_ERR("DxgkCbAcquirePostDisplayOwnership: "
                    "InbvGetGopFrameBufferInfo failed\n");
        return STATUS_SUCCESS;
    }
    GopQueryUs = DxgkpTraceElapsedUs(StepStart100ns);

    /*
     * Translate EFI GOP PixelFormat to a D3DDDIFMT value.
     *
     * EFI GOP pixel format constants:
     *   0 = PixelRedGreenBlueReserved8BitPerColor  (RGBX — stored as XRGB)
     *   1 = PixelBlueGreenRedReserved8BitPerColor  (BGRX — most common)
     *   2 = PixelBitMask                           (custom bitmask)
     *   3 = PixelBltOnly                           (no linear framebuffer)
     *
     * WDDM maps both RGBX and BGRX to D3DDDIFMT_X8R8G8B8 (32bpp) for the
     * POST display because the byte order does not affect the scan-out —
     * the miniport reads the actual PixelFormat from DXGK_DISPLAY_INFORMATION
     * or from the GOP protocol directly to determine final byte order.
     *
     * For PixelBitMask with R=0xF800, G=0x07E0, B=0x001F we infer 16bpp
     * R5G6B5.  Otherwise fall back to X8R8G8B8.
     */
    switch (Fb.PixelFormat)
    {
        case 0: /* RGBX 32bpp */
        case 1: /* BGRX 32bpp */
            ColorFormat   = D3DDDIFMT_X8R8G8B8;
            BytesPerPixel = 4;
            break;

        case 2: /* BitMask */
            if (Fb.RedMask == 0xF800 &&
                Fb.GreenMask == 0x07E0 &&
                Fb.BlueMask  == 0x001F)
            {
                ColorFormat   = D3DDDIFMT_R5G6B5;
                BytesPerPixel = 2;
            }
            else
            {
                /* Non-standard bitmask: treat as 32bpp X8R8G8B8. */
                ColorFormat   = D3DDDIFMT_X8R8G8B8;
                BytesPerPixel = 4;
            }
            break;

        /*
         * The ARM64 loader stores BITS PER PIXEL here rather than the EFI
         * GOP enum (freeldr GOP detection; the former XPDM consumers
         * computed BytesPerPixel = (PixelFormat + 7) / 8).  Accept the
         * two linear formats that convention produces.
         */
        case 32:
            ColorFormat   = D3DDDIFMT_X8R8G8B8;
            BytesPerPixel = 4;
            break;

        case 16:
            ColorFormat   = D3DDDIFMT_R5G6B5;
            BytesPerPixel = 2;
            break;

        default: /* BltOnly or unknown — no usable linear framebuffer */
            DXGKRNL_WARN("DxgkCbAcquirePostDisplayOwnership: "
                         "PixelFormat %lu has no linear FB\n", Fb.PixelFormat);
            return STATUS_SUCCESS;
    }

    DisplayInformation->Width         = Fb.HorizontalResolution;
    DisplayInformation->Height        = Fb.VerticalResolution;
    DisplayInformation->Pitch         = Fb.PixelsPerScanLine * BytesPerPixel;
    DisplayInformation->ColorFormat   = ColorFormat;
    DisplayInformation->PhysicAddress.QuadPart = Fb.FrameBufferBase.QuadPart;
    DisplayInformation->TargetId      = 0; /* primary output */
    DisplayInformation->AcpiId        = 0;

    /* Store POST display info in the adapter for later use. */
    {
        PDXGKRNL_ADAPTER PostAdapter = (PDXGKRNL_ADAPTER)DeviceHandle;
        if (PostAdapter != NULL)
        {
            SIZE_T FbSize = (SIZE_T)DisplayInformation->Pitch *
                            DisplayInformation->Height;
            BOOLEAN MappingChanged;

            MappingChanged =
                (PostAdapter->PostDisplayVirtualAddress != NULL) &&
                (PostAdapter->PostDisplayPhysicalAddress.QuadPart !=
                     DisplayInformation->PhysicAddress.QuadPart ||
                 PostAdapter->PostDisplayPitch != DisplayInformation->Pitch ||
                 PostAdapter->PostDisplayHeight != DisplayInformation->Height);

            if (MappingChanged)
            {
                DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: "
                              "remapping GOP FB old PA=0x%I64X Pitch=%lu Height=%lu "
                              "new PA=0x%I64X Pitch=%lu Height=%lu\n",
                              PostAdapter->PostDisplayPhysicalAddress.QuadPart,
                              PostAdapter->PostDisplayPitch,
                              PostAdapter->PostDisplayHeight,
                              DisplayInformation->PhysicAddress.QuadPart,
                              DisplayInformation->Pitch,
                              DisplayInformation->Height);
                DxgkpReleasePostDisplayMapping(PostAdapter);
            }

            PostAdapter->PostDisplayWidth  = DisplayInformation->Width;
            PostAdapter->PostDisplayHeight = DisplayInformation->Height;
            PostAdapter->PostDisplayPhysicalAddress = DisplayInformation->PhysicAddress;
            PostAdapter->PostDisplayPitch  = DisplayInformation->Pitch;

            /* Map the GOP framebuffer into kernel VA for direct CPU access. */
            if (DisplayInformation->PhysicAddress.QuadPart != 0 &&
                PostAdapter->PostDisplayVirtualAddress == NULL)
            {
                /* Try write-combined first (standard for framebuffers),
                 * fall back to non-cached. */
                PostAdapter->PostDisplayVirtualAddress =
                    MmMapIoSpace(DisplayInformation->PhysicAddress,
                                 FbSize,
                                 MmWriteCombined);
                if (PostAdapter->PostDisplayVirtualAddress == NULL)
                {
                    PostAdapter->PostDisplayVirtualAddress =
                        MmMapIoSpace(DisplayInformation->PhysicAddress,
                                     FbSize,
                                     MmNonCached);
                }

                if (PostAdapter->PostDisplayVirtualAddress != NULL)
                {
                    PostAdapter->PostDisplayMappingSize = FbSize;
                    ASSERT(PostAdapter->PostDisplayMappingSize == FbSize);
                }

                DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: "
                              "mapped GOP FB PA=0x%I64X -> VA=%p (%Iu bytes)\n",
                              DisplayInformation->PhysicAddress.QuadPart,
                              PostAdapter->PostDisplayVirtualAddress,
                              FbSize);
            }
        }
    }

    DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: "
                  "%lux%lu Pitch=%lu Fmt=%d PA=0x%I64X\n",
                  DisplayInformation->Width,
                  DisplayInformation->Height,
                  DisplayInformation->Pitch,
                  (int)DisplayInformation->ColorFormat,
                  DisplayInformation->PhysicAddress.QuadPart);

    /*
     * Transfer display ownership from InbV to the miniport.
     * After this call InbV stops writing to the framebuffer.
     */
    StepStart100ns = DxgkpTraceNow100ns();
    InbvAcquireDisplayOwnership();
    OwnershipUs = DxgkpTraceElapsedUs(StepStart100ns);

    g_PostDisplayOwnerAdapter = (PDXGKRNL_ADAPTER)DeviceHandle;

    DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: gop=%I64u us ownership=%I64u us total=%I64u us\n",
                  GopQueryUs,
                  OwnershipUs,
                  DxgkpTraceElapsedUs(TotalStart100ns));

    return STATUS_SUCCESS;
}

static BOOLEAN
DxgkpInvokeMiniportInterrupt(
    _In_opt_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG MessageNumber,
    _In_ PCSTR IsrName)
{
    BOOLEAN   Handled;
    LONG      Sequence;
    BOOLEAN   Logged;
    ULONGLONG Start100ns;
    ULONGLONG ElapsedUs;

    if (Adapter == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->InitData.s.DxgkDdiInterruptRoutine == NULL)
    {
        return FALSE;
    }

    Sequence = InterlockedIncrement(&Adapter->InterruptCount);
    Logged = (Sequence <= DXGK_TRACE_ISR_LOG_LIMIT);
    Start100ns = DxgkpTraceNow100ns();

    Handled = Adapter->MiniportContext->InitData.s.DxgkDdiInterruptRoutine(
        Adapter->MiniportDeviceContext,
        MessageNumber);

    ElapsedUs = DxgkpTraceElapsedUs(Start100ns);

    if (Logged)
    {
        DXGKRNL_TRACE("%s: seq=%ld msg=%lu handled=%d state=%d queue=%ld dpc=%ld t+%I64u us dur=%I64u us\n",
                      IsrName,
                      Sequence,
                      MessageNumber,
                      Handled,
                      Adapter->State,
                      Adapter->QueueDpcCount,
                      Adapter->DpcCount,
                      DxgkpTraceSinceStartUs(Adapter),
                      ElapsedUs);
    }

    return Handled;
}

/*
 * DxgkpIsrTrampoline — ISR wrapper for KSERVICE_ROUTINE→DxgkDdiInterruptRoutine
 */
static BOOLEAN NTAPI
DxgkpIsrTrampoline(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID       ServiceContext)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)ServiceContext;
    UNREFERENCED_PARAMETER(Interrupt);

    return DxgkpInvokeMiniportInterrupt(Adapter,
                                        0 /* MessageNumber */,
                                        "DxgkpIsrTrampoline");
}

/*
 * DxgkpMessageIsrTrampoline — MSI/MSI-X ISR wrapper for
 * PKMESSAGE_SERVICE_ROUTINE→DxgkDdiInterruptRoutine.
 */
static BOOLEAN NTAPI
DxgkpMessageIsrTrampoline(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID       ServiceContext,
    _In_ ULONG       MessageNumber)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)ServiceContext;
    BOOLEAN Handled = FALSE;
    ULONG m;
    UNREFERENCED_PARAMETER(Interrupt);

    /*
     * On ROS ARM64 the interrupt arbiter grants a single GIC vector even for a
     * multi-message MSI-X device, and pci.sys replicates that vector across the
     * device's whole MSI-X table (see PciPdoEnableMsix path).  So every queue's
     * MSI arrives on this one vector with MessageNumber 0.  Poll every message
     * the device exposes so the miniport checks all of its VirtIO queues and
     * retires whichever completed — otherwise queue>0 completions are missed and
     * the command queue stalls.
     */
    if (Adapter != NULL && Adapter->InterruptMessageCount > 1)
    {
        for (m = 0; m < Adapter->InterruptMessageCount; m++)
            Handled |= DxgkpInvokeMiniportInterrupt(Adapter, m,
                                                    "DxgkpMessageIsrTrampoline");
        return Handled;
    }

    return DxgkpInvokeMiniportInterrupt(Adapter,
                                        MessageNumber,
                                        "DxgkpMessageIsrTrampoline");
}

/*
 * DxgkpIsMsixEnabled — read the device's live MSI-X Message Control Enable bit.
 *
 * On ROS ARM64 the interrupt resource descriptor handed to the FDO can lack
 * CM_RESOURCE_INTERRUPT_MESSAGE even after pci.sys has allocated + enabled MSI-X
 * on the device (the flag is lost in the PnP resource hand-off).  pci.sys enables
 * MSI-X before the FDO connects its interrupt, so the live Enable bit in config
 * space is the reliable signal for "connect message-based".
 */
static ULONG
DxgkpIsMsixEnabled(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG BusNum = 0, SlotNum = 0, Dummy = 0;
    PCI_SLOT_NUMBER Slot;
    UCHAR CapPtr = 0, CapId = 0;
    USHORT MsgCtrl = 0;
    ULONG Guard = 0;

    if (Adapter == NULL || Adapter->PhysicalDeviceObject == NULL)
        return 0;

    IoGetDeviceProperty(Adapter->PhysicalDeviceObject,
                        DevicePropertyBusNumber, sizeof(BusNum), &BusNum, &Dummy);
    IoGetDeviceProperty(Adapter->PhysicalDeviceObject,
                        DevicePropertyAddress, sizeof(SlotNum), &SlotNum, &Dummy);

    Slot.u.AsULONG = 0;
    Slot.u.bits.DeviceNumber = (SlotNum >> 16) & 0x1F;
    Slot.u.bits.FunctionNumber = SlotNum & 0x7;

    if (HalGetBusDataByOffset(PCIConfiguration, BusNum, Slot.u.AsULONG,
                              &CapPtr, 0x34 /* Cap ptr */, 1) == 0)
        return 0;

    while (CapPtr >= 0x40 && CapPtr != 0xFF && Guard++ < 48)
    {
        HalGetBusDataByOffset(PCIConfiguration, BusNum, Slot.u.AsULONG,
                              &CapId, CapPtr, 1);
        if (CapId == 0x11) /* PCI_CAPABILITY_ID_MSIX */
        {
            HalGetBusDataByOffset(PCIConfiguration, BusNum, Slot.u.AsULONG,
                                  &MsgCtrl, CapPtr + 2, 2);
            /* Enable = bit 15; low 11 bits = (table size - 1).  Return the MSI-X
             * table size when enabled, 0 when MSI-X is not enabled. */
            return (MsgCtrl & 0x8000) ? (((ULONG)(MsgCtrl & 0x07FF)) + 1) : 0;
        }
        HalGetBusDataByOffset(PCIConfiguration, BusNum, Slot.u.AsULONG,
                              &CapPtr, CapPtr + 1 /* next cap */, 1);
    }
    return 0;
}

/*
 * DxgkpMarkInterruptResourcesMessageBased — set CM_RESOURCE_INTERRUPT_MESSAGE on
 * every interrupt descriptor in a CM resource list.
 *
 * The FDO's interrupt resource descriptor can arrive line-based on ROS ARM64 even
 * when MSI-X is in use.  The miniport (viogpudo) reads these resources back via
 * DxgkCbGetDeviceInformation and, if it sees a line-based interrupt, programs its
 * VirtIO queues for polling (NO_VECTOR) instead of enabling per-queue MSI-X — so
 * the device never raises a completion MSI.  Once we know we are message-based,
 * mark the resources accordingly so the miniport enables queue MSI-X.
 */
static VOID
DxgkpMarkInterruptResourcesMessageBased(
    _In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    ULONG li, di;

    if (ResourceList == NULL)
        return;

    for (li = 0; li < ResourceList->Count; li++)
    {
        PCM_PARTIAL_RESOURCE_LIST Partial =
            &ResourceList->List[li].PartialResourceList;
        for (di = 0; di < Partial->Count; di++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc =
                &Partial->PartialDescriptors[di];
            if (Desc->Type == CmResourceTypeInterrupt)
                Desc->Flags |= CM_RESOURCE_INTERRUPT_MESSAGE;
        }
    }
}

/*
 * DxgkpQueryDriverCaps — DXGKQAITYPE_DRIVERCAPS into a caller buffer of
 * DXGKP_DRIVERCAPS_QUERY_SIZE bytes (see dxgkrnl_private.h).
 *
 * First asks with our own sizeof; if the miniport was built against a
 * newer WDK whose DXGK_DRIVERCAPS is bigger it fails the size check
 * (STATUS_BUFFER_TOO_SMALL/INVALID_PARAMETER) — retry with the large
 * zeroed buffer so callers can read the stable head fields.
 */
NTSTATUS
DxgkpQueryDriverCaps(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_writes_bytes_(DXGKP_DRIVERCAPS_QUERY_SIZE) PDXGK_DRIVERCAPS Caps)
{
    PDXGKDDI_QUERY_ADAPTER_INFO PfnQueryAdapterInfo;
    DXGKARG_QUERYADAPTERINFO QueryArgs;
    ULONG Attempt;
    NTSTATUS Status;

    if (Caps == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Caps, DXGKP_DRIVERCAPS_QUERY_SIZE);

    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return STATUS_INVALID_PARAMETER;

    PfnQueryAdapterInfo = DXGK_CB(Adapter, DxgkDdiQueryAdapterInfo);
    if (PfnQueryAdapterInfo == NULL)
        return STATUS_NOT_SUPPORTED;

    Status = STATUS_UNSUCCESSFUL;
    for (Attempt = 0; Attempt < 2; Attempt++)
    {
        RtlZeroMemory(Caps, DXGKP_DRIVERCAPS_QUERY_SIZE);
        RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
        QueryArgs.Type = DXGKQAITYPE_DRIVERCAPS;
        QueryArgs.pOutputData = Caps;
        QueryArgs.OutputDataSize = (Attempt == 0) ? sizeof(DXGK_DRIVERCAPS)
                                                  : DXGKP_DRIVERCAPS_QUERY_SIZE;

        _SEH2_TRY
        {
            Status = PfnQueryAdapterInfo(Adapter->MiniportDeviceContext,
                                         &QueryArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (NT_SUCCESS(Status))
            break;
    }

    return Status;
}

/* ========================================================================
 * Adapter lifecycle functions
 * ====================================================================== */

/*
 * DxgkAdapterStart
 *
 * Called from DxgkpMiniportPnpDispatch in response to IRP_MN_START_DEVICE.
 * Captures the resource lists, fills the DXGK_INTERFACE, and calls
 * DxgkDdiStartDevice on the miniport.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkAdapterStart(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PCM_RESOURCE_LIST AllocatedResources,
    _In_ PCM_RESOURCE_LIST TranslatedResources)
{
    DXGK_START_INFO StartInfo;
    DXGK_INTERFACE  Interface;
    NTSTATUS        Status;
    ULONGLONG       AdapterStart100ns;
    ULONGLONG       StepStart100ns;
    ULONGLONG       InterruptConnectUs = 0;
    ULONGLONG       MiniportStartUs = 0;
    ULONGLONG       VidMmUs = 0;
    ULONGLONG       VidPnUs = 0;
    ULONGLONG       PresentUs = 0;
    ULONGLONG       DisplayUs = 0;

    PAGED_CODE();

    AdapterStart100ns = DxgkpTraceNow100ns();

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);
    DxgkRosAssert(Adapter->State == DxgkAdapterStateStopped,
                  DXGKRNL_BUGCHECK_NULL_ADAPTER);

    Adapter->InterruptCount = 0;
    Adapter->QueueDpcCount = 0;
    Adapter->DpcCount = 0;
    Adapter->InterruptTraceEpoch100ns = AdapterStart100ns;

    DXGKRNL_TRACE("DxgkAdapterStart: Adapter %p AllocRes=%p TransRes=%p\n",
                  Adapter, AllocatedResources, TranslatedResources);

    if (TranslatedResources && TranslatedResources->Count > 0)
    {
        ULONG i;
        PCM_PARTIAL_RESOURCE_LIST PartialList = &TranslatedResources->List[0].PartialResourceList;
        DXGKRNL_TRACE("DxgkAdapterStart: %lu translated resources\n", PartialList->Count);
        for (i = 0; i < PartialList->Count; i++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &PartialList->PartialDescriptors[i];
            DXGKRNL_TRACE("  [%lu] Type=%u Start=0x%llX Length=0x%lX\n",
                          i, Desc->Type,
                          (ULONGLONG)Desc->u.Memory.Start.QuadPart,
                          Desc->u.Memory.Length);
        }
    }
    else
    {
        /* Normal for root-enumerated software adapters (softgpu); miniports
         * that require hardware resources fail their own StartDevice. */
        DXGKRNL_TRACE("DxgkAdapterStart: no translated resources (PDO=%p DOD=%d)\n",
                      Adapter->PhysicalDeviceObject,
                      Adapter->MiniportContext ? Adapter->MiniportContext->IsDisplayOnlyDriver : -1);
    }

    /* Save raw PCI resource lists for DxgkCbGetDeviceInformation. */
    Adapter->AllocatedResources  = AllocatedResources;
    Adapter->TranslatedResources = TranslatedResources;

    /* Save interrupt resource info for deferred connection.
     * The interrupt is connected AFTER DxgkDdiStartDevice completes
     * (the miniport initializes VirtIO queues during StartDevice and
     * must be fully initialized before the ISR fires). */
    if (TranslatedResources && TranslatedResources->Count > 0)
    {
        ULONG ri;
        PCM_PARTIAL_RESOURCE_LIST PartialList = &TranslatedResources->List[0].PartialResourceList;
        PCM_PARTIAL_RESOURCE_LIST RawPartialList =
            (AllocatedResources && AllocatedResources->Count > 0) ?
            &AllocatedResources->List[0].PartialResourceList : NULL;
        for (ri = 0; ri < PartialList->Count; ri++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &PartialList->PartialDescriptors[ri];
            PCM_PARTIAL_RESOURCE_DESCRIPTOR RawDesc =
                (RawPartialList != NULL && ri < RawPartialList->Count) ?
                &RawPartialList->PartialDescriptors[ri] : NULL;
            if (Desc->Type == CmResourceTypeInterrupt)
            {
                /* A message-signalled (MSI/MSI-X) interrupt carries
                 * CM_RESOURCE_INTERRUPT_MESSAGE.  On ROS ARM64 the TRANSLATED
                 * descriptor can drop that flag, so also honour it from the RAW
                 * descriptor; connect message-based (CONNECT_MESSAGE_BASED) if
                 * either carries it.  (The .Translated vector/level/affinity of a
                 * message interrupt overlays u.Interrupt, so it stays valid even
                 * when the translated flag was dropped.) */
                Adapter->InterruptMessageBased =
                    ((Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) ||
                     (RawDesc != NULL &&
                      RawDesc->Type == CmResourceTypeInterrupt &&
                      (RawDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)))
                    ? TRUE : FALSE;

                if (Adapter->InterruptMessageBased)
                {
                    Adapter->InterruptVector =
                        Desc->u.MessageInterrupt.Translated.Vector;
                    Adapter->InterruptLevel =
                        (KIRQL)Desc->u.MessageInterrupt.Translated.Level;
                    Adapter->InterruptAffinity =
                        Desc->u.MessageInterrupt.Translated.Affinity;
                    Adapter->InterruptShared = FALSE;
                    Adapter->InterruptMode = Latched;
                    Adapter->InterruptMessageCount =
                        (RawDesc != NULL &&
                         RawDesc->Type == CmResourceTypeInterrupt &&
                         (RawDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) &&
                         RawDesc->u.MessageInterrupt.Raw.MessageCount) ?
                        RawDesc->u.MessageInterrupt.Raw.MessageCount : 1;

                    DXGKRNL_TRACE("DxgkAdapterStart: saved MSI interrupt — "
                                  "BaseVector=%lu Count=%lu IRQL=%u\n",
                                  Adapter->InterruptVector,
                                  Adapter->InterruptMessageCount,
                                  Adapter->InterruptLevel);
                }
                else
                {
                    Adapter->InterruptVector   = Desc->u.Interrupt.Vector;
                    Adapter->InterruptLevel    = (KIRQL)Desc->u.Interrupt.Level;
                    Adapter->InterruptAffinity = Desc->u.Interrupt.Affinity;
                    Adapter->InterruptShared   =
                        (Desc->ShareDisposition == CmResourceShareShared);
                    Adapter->InterruptMode     =
                        (Desc->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                        Latched : LevelSensitive;

                    DXGKRNL_TRACE("DxgkAdapterStart: saved interrupt — Vector=%lu IRQL=%u\n",
                                  Adapter->InterruptVector,
                                  Adapter->InterruptLevel);
                }
                break;
            }
        }
    }

    /*
     * Cache PCI bus number and slot (device/function) from the PDO properties.
     * This is done once so DxgkCbReadDeviceSpace/DxgkCbWriteDeviceSpace don't
     * need to call IoGetDeviceProperty on every PCI config access, which sends
     * PnP IRPs and can cause spinlock re-entrancy issues on ReactOS.
     */
    {
        ULONG BusNum = 0, DevAddr = 0, Dummy = 0;
        IoGetDeviceProperty(Adapter->PhysicalDeviceObject,
                            DevicePropertyBusNumber, sizeof(BusNum), &BusNum, &Dummy);
        IoGetDeviceProperty(Adapter->PhysicalDeviceObject,
                            DevicePropertyAddress, sizeof(DevAddr), &DevAddr, &Dummy);
        Adapter->PciBusNumber = BusNum;
        RtlZeroMemory(&Adapter->PciSlotNumber, sizeof(Adapter->PciSlotNumber));
        Adapter->PciSlotNumber.u.bits.DeviceNumber = (DevAddr >> 16) & 0x1F;
        Adapter->PciSlotNumber.u.bits.FunctionNumber = DevAddr & 0x7;
        Adapter->PciBusSlotCached = TRUE;
        DXGKRNL_TRACE("DxgkAdapterStart: cached PCI bus=%lu dev=%lu fn=%lu\n",
                      BusNum,
                      (ULONG)Adapter->PciSlotNumber.u.bits.DeviceNumber,
                      (ULONG)Adapter->PciSlotNumber.u.bits.FunctionNumber);
    }

    /* Fill the callback table for the miniport -- zero the full 512-byte
     * buffer so WDDM 2.0+ callbacks beyond our struct definition are NULL. */
    RtlZeroMemory(&Interface, sizeof(Interface));
    DxgkpFillInterface(Adapter, &Interface);
    Interface.Size = sizeof(Interface); /* advertise full buffer size */

    /* Build the start-info block. */
    RtlZeroMemory(&StartInfo, sizeof(StartInfo));
    StartInfo.RequiredDmaQueueEntry = 32;
    /* Assign a unique LUID for this adapter and persist it. */
    {
        static LONG NextLuid = 0x10000;
        StartInfo.AdapterLuid.LowPart = InterlockedIncrement(&NextLuid);
        StartInfo.AdapterLuid.HighPart = 0;
        Adapter->AdapterLuid = StartInfo.AdapterLuid;
    }

    /* Connect the interrupt BEFORE calling StartDevice — the miniport needs
     * it for VirtIO queue completion notifications during initialization. */
    if (Adapter->InterruptVector != 0 &&
        Adapter->MiniportContext->InitData.s.DxgkDdiInterruptRoutine != NULL)
    {
        IO_CONNECT_INTERRUPT_PARAMETERS ConnectParams;
        RtlZeroMemory(&ConnectParams, sizeof(ConnectParams));

        /* If pci.sys enabled MSI-X on the device but the resource descriptor
         * arrived line-based (the message flag is dropped in the PnP resource
         * hand-off on ROS ARM64), connect message-based anyway — the device is
         * in MSI-X mode and will never assert INTx. */
        {
            ULONG MsixSize = DxgkpIsMsixEnabled(Adapter);
            if (!Adapter->InterruptMessageBased && MsixSize > 0)
            {
                DXGKRNL_WARN("DxgkAdapterStart: MSI-X enabled in config (table=%lu) but "
                             "descriptor was line-based — connecting message-based\n", MsixSize);
                Adapter->InterruptMessageBased = TRUE;
                /* All MSI-X table entries share the single granted GIC vector, so
                 * record the table size: the ISR trampoline polls every message
                 * on this vector so the miniport checks all of its VirtIO queues. */
                Adapter->InterruptMessageCount = MsixSize;
            }
        }

        if (Adapter->InterruptMessageBased)
        {
            ConnectParams.Version = CONNECT_MESSAGE_BASED;
            ConnectParams.MessageBased.PhysicalDeviceObject =
                Adapter->PhysicalDeviceObject;
            ConnectParams.MessageBased.ConnectionContext.InterruptMessageTable =
                &Adapter->InterruptMessageTable;
            ConnectParams.MessageBased.MessageServiceRoutine =
                DxgkpMessageIsrTrampoline;
            ConnectParams.MessageBased.ServiceContext = Adapter;
            ConnectParams.MessageBased.SpinLock = NULL;
            ConnectParams.MessageBased.SynchronizeIrql =
                Adapter->InterruptLevel;
            ConnectParams.MessageBased.FloatingSave = FALSE;
            ConnectParams.MessageBased.FallBackServiceRoutine = NULL;
        }
        else
        {
            ConnectParams.Version = CONNECT_FULLY_SPECIFIED;
            ConnectParams.FullySpecified.PhysicalDeviceObject =
                Adapter->PhysicalDeviceObject;
            ConnectParams.FullySpecified.InterruptObject =
                &Adapter->InterruptObject;
            ConnectParams.FullySpecified.ServiceRoutine = DxgkpIsrTrampoline;
            ConnectParams.FullySpecified.ServiceContext = Adapter;
            ConnectParams.FullySpecified.SpinLock = NULL;
            ConnectParams.FullySpecified.SynchronizeIrql =
                Adapter->InterruptLevel;
            ConnectParams.FullySpecified.FloatingSave = FALSE;
            ConnectParams.FullySpecified.ShareVector = Adapter->InterruptShared;
            ConnectParams.FullySpecified.Vector = Adapter->InterruptVector;
            ConnectParams.FullySpecified.Irql = Adapter->InterruptLevel;
            ConnectParams.FullySpecified.InterruptMode = Adapter->InterruptMode;
            ConnectParams.FullySpecified.ProcessorEnableMask =
                Adapter->InterruptAffinity;
        }

        StepStart100ns = DxgkpTraceNow100ns();
        Status = IoConnectInterruptEx(&ConnectParams);
        InterruptConnectUs = DxgkpTraceElapsedUs(StepStart100ns);
        if (NT_SUCCESS(Status))
        {
            if (Adapter->InterruptMessageTable != NULL &&
                Adapter->InterruptMessageTable->MessageCount > 0)
            {
                Adapter->InterruptObject =
                    Adapter->InterruptMessageTable->MessageInfo[0].InterruptObject;
            }

            /* Tell the miniport (via DxgkCbGetDeviceInformation) that its
             * interrupt is message-based so it enables per-queue MSI-X instead
             * of polling; the FDO's descriptor arrived line-based. */
            if (Adapter->InterruptMessageBased)
            {
                DxgkpMarkInterruptResourcesMessageBased(Adapter->AllocatedResources);
                DxgkpMarkInterruptResourcesMessageBased(Adapter->TranslatedResources);
            }

            DXGKRNL_TRACE("DxgkAdapterStart: Interrupt connected pre-start — "
                          "Vector=%lu MessageBased=%d Count=%lu\n",
                          Adapter->InterruptVector,
                          Adapter->InterruptMessageBased,
                          Adapter->InterruptMessageTable ?
                              Adapter->InterruptMessageTable->MessageCount : 1);
        }
        else
            DXGKRNL_ERR("DxgkAdapterStart: IoConnectInterruptEx failed 0x%08lX\n", Status);
    }

    /* Call miniport start. */
    DXGKRNL_TRACE("DxgkAdapterStart: calling DxgkDdiStartDevice MiniportCtx=%p\n",
                  Adapter->MiniportDeviceContext);
    StepStart100ns = DxgkpTraceNow100ns();
    Status = Adapter->MiniportContext->InitData.s.DxgkDdiStartDevice(
                 Adapter->MiniportDeviceContext,
                 &StartInfo,
                 &Interface,
                 &Adapter->NumberOfVideoPresentSources,
                 &Adapter->NumberOfChildren);
    MiniportStartUs = DxgkpTraceElapsedUs(StepStart100ns);
    DXGKRNL_TRACE("DxgkAdapterStart: DxgkDdiStartDevice returned 0x%08lX after %I64u us\n",
                  Status,
                  MiniportStartUs);

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStart: DxgkDdiStartDevice failed 0x%08lX (IRQs fired during start=%ld, vec=%lu msgbased=%d)\n",
                    Status, Adapter->InterruptCount,
                    Adapter->InterruptVector, Adapter->InterruptMessageBased);
        /* Disconnect interrupt if we connected it */
        if (Adapter->InterruptMessageTable != NULL)
        {
            IO_DISCONNECT_INTERRUPT_PARAMETERS DisconnectParams;
            RtlZeroMemory(&DisconnectParams, sizeof(DisconnectParams));
            DisconnectParams.Version = CONNECT_MESSAGE_BASED;
            DisconnectParams.ConnectionContext.InterruptMessageTable =
                Adapter->InterruptMessageTable;
            IoDisconnectInterruptEx(&DisconnectParams);
            Adapter->InterruptMessageTable = NULL;
            Adapter->InterruptObject = NULL;
        }
        else if (Adapter->InterruptObject)
        {
            IoDisconnectInterrupt(Adapter->InterruptObject);
            Adapter->InterruptObject = NULL;
        }
        Adapter->AllocatedResources  = NULL;
        Adapter->TranslatedResources = NULL;
        DXGKRNL_TRACE("DxgkAdapterStart: fail summary connect=%I64u us miniport=%I64u us total=%I64u us irq=%ld queue=%ld dpc=%ld\n",
                      InterruptConnectUs,
                      MiniportStartUs,
                      DxgkpTraceElapsedUs(AdapterStart100ns),
                      Adapter->InterruptCount,
                      Adapter->QueueDpcCount,
                      Adapter->DpcCount);
        Adapter->InterruptTraceEpoch100ns = 0;
        return Status;
    }

    DXGKRNL_TRACE("DxgkAdapterStart: started — Sources=%lu Children=%lu\n",
                  Adapter->NumberOfVideoPresentSources,
                  Adapter->NumberOfChildren);

    /* Initialise the video memory manager for this adapter. */
    StepStart100ns = DxgkpTraceNow100ns();
    Status = DxgkVidMmInitializeAdapter(Adapter);
    VidMmUs = DxgkpTraceElapsedUs(StepStart100ns);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStart: DxgkVidMmInitializeAdapter failed "
                    "0x%08lX\n", Status);
        Adapter->MiniportContext->InitData.s.DxgkDdiStopDevice(
            Adapter->MiniportDeviceContext);
        Adapter->AllocatedResources  = NULL;
        Adapter->TranslatedResources = NULL;
        return Status;
    }

    /*
     * Discover the GPU node topology from DRIVERCAPS (full WDDM only) so
     * the scheduler sizes its engine array from what the miniport
     * reports.
     */
    Adapter->NodeCount = 0;
    if (!Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        PDXGK_DRIVERCAPS Caps;

        Caps = ExAllocatePoolWithTag(NonPagedPool,
                                     DXGKP_DRIVERCAPS_QUERY_SIZE,
                                     TAG_DXGK_ADAPTER);
        if (Caps != NULL)
        {
            if (NT_SUCCESS(DxgkpQueryDriverCaps(Adapter, Caps)))
            {
                Adapter->NodeCount =
                    Caps->GpuEngineTopology.NbAsymetricProcessingNodes;
                DXGKRNL_TRACE("DxgkAdapterStart: %lu GPU node(s) reported\n",
                              Adapter->NodeCount);
            }

            ExFreePoolWithTag(Caps, TAG_DXGK_ADAPTER);
        }
    }

    /* Initialise the video scheduler for this adapter (full WDDM only). */
    {
        NTSTATUS VidSchStatus = VidSchInitialize(Adapter);
        if (!NT_SUCCESS(VidSchStatus))
        {
            DXGKRNL_WARN("DxgkAdapterStart: VidSchInitialize failed "
                         "0x%08lX — continuing without scheduler\n",
                         VidSchStatus);
            /* Non-fatal: present timer provides a fallback for DOD/basic. */
        }
    }

    /*
     * Create the VidPN (Video Present Network) for this adapter.
     * The VidPN is needed by the miniport driver for mode enumeration
     * (IsSupportedVidPn, EnumVidPnCofuncModality, CommitVidPn).
     */
    {
        D3DKMDT_HVIDPN hVidPn = NULL;
        StepStart100ns = DxgkpTraceNow100ns();
        Status = DxgkVidPnCreateForAdapter(Adapter, &hVidPn);
        VidPnUs = DxgkpTraceElapsedUs(StepStart100ns);
        if (NT_SUCCESS(Status))
        {
            Adapter->VidPn = (PVOID)hVidPn;
            DXGKRNL_TRACE("DxgkAdapterStart: VidPN created %p\n", hVidPn);
        }
        else
        {
            DXGKRNL_ERR("DxgkAdapterStart: DxgkVidPnCreateForAdapter failed "
                        "0x%08lX — continuing without VidPN\n", Status);
            /* Non-fatal: adapter can still start; VidPN calls will fail gracefully. */
            Adapter->VidPn = NULL;
        }
    }

    /*
     * Full WDDM adapters keep VidPN commit deferred until cdd opens the
     * shared primary. Display-only BIOS/no-GOP adapters establish a real
     * mode here so win32k never sees a synthetic fallback geometry.
     */
    if (Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        NTSTATUS InitialModeStatus;

        InitialModeStatus = DxgkDisplayEstablishInitialMode(Adapter);
        if (!NT_SUCCESS(InitialModeStatus))
        {
            DXGKRNL_WARN("DxgkAdapterStart: initial DOD mode establishment "
                         "failed 0x%08lX — continuing with fallback query path\n",
                         InitialModeStatus);
        }
    }

    /* Initialise the per-VidPnSource present queues. */
    {
        NTSTATUS PresentStatus;

        StepStart100ns = DxgkpTraceNow100ns();
        PresentStatus = DxgkPresentInit(Adapter);
        PresentUs = DxgkpTraceElapsedUs(StepStart100ns);
        if (!NT_SUCCESS(PresentStatus))
        {
            DXGKRNL_ERR("DxgkAdapterStart: DxgkPresentInit failed "
                        "0x%08lX — continuing without present queues\n",
                        PresentStatus);
            /* Non-fatal: the timer-based present in display.c provides a fallback. */
        }
    }

    Adapter->State = DxgkAdapterStateStarted;

    /* Watch for stuck submissions (documented TDR recovery). */
    DxgkpStartTdrWatchdog(Adapter);

    /*
     * Ask the miniport to deliver vsync notifications; adapters that
     * report them get vblank-paced present flushes (see display.c).
     */
    if (DXGK_CB(Adapter, DxgkDdiControlInterrupt) != NULL)
    {
        NTSTATUS VsyncStatus;

        _SEH2_TRY
        {
            VsyncStatus = DXGK_CB(Adapter, DxgkDdiControlInterrupt)(
                              Adapter->MiniportDeviceContext,
                              DXGK_INTERRUPT_TYPE_CRTC_VSYNC,
                              TRUE);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            VsyncStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        DXGKRNL_TRACE("DxgkAdapterStart: CRTC_VSYNC enable -> 0x%08lX\n",
                      VsyncStatus);
    }

    /* Enable the GUID_DISPLAY_DEVICE_ARRIVAL device interface.
     * User-mode components (DXGI, OpenGL ICD loader) and kernel PnP
     * notification consumers listen for this interface to discover adapters. */
    if (Adapter->DeviceInterfaceName.Buffer != NULL)
    {
        NTSTATUS IfStatus;
        IfStatus = IoSetDeviceInterfaceState(&Adapter->DeviceInterfaceName, TRUE);
        if (NT_SUCCESS(IfStatus))
        {
            Adapter->DeviceInterfaceEnabled = TRUE;
            DXGKRNL_TRACE("DxgkAdapterStart: enabled device interface %wZ\n",
                          &Adapter->DeviceInterfaceName);
        }
        else
        {
            DXGKRNL_WARN("DxgkAdapterStart: IoSetDeviceInterfaceState(TRUE) "
                          "failed 0x%08lX\n", IfStatus);
        }
    }

    /* Create \DosDevices\DISPLAY symlink pointing to \Device\DxgKrnl.
     * This is created once (first adapter to start).  If the symlink
     * already exists we silently ignore the collision. */
    {
        static LONG DisplaySymlinkCreated = 0;
        if (InterlockedCompareExchange(&DisplaySymlinkCreated, 1, 0) == 0)
        {
            UNICODE_STRING SymlinkName, TargetName;
            NTSTATUS SymStatus;

            RtlInitUnicodeString(&SymlinkName, L"\\DosDevices\\DISPLAY");
            RtlInitUnicodeString(&TargetName,  L"\\Device\\DxgKrnl");
            SymStatus = IoCreateSymbolicLink(&SymlinkName, &TargetName);
            if (NT_SUCCESS(SymStatus))
            {
                DXGKRNL_TRACE("DxgkAdapterStart: created \\DosDevices\\DISPLAY symlink\n");
            }
            else if (SymStatus == STATUS_OBJECT_NAME_COLLISION)
            {
                DXGKRNL_TRACE("DxgkAdapterStart: \\DosDevices\\DISPLAY already exists\n");
            }
            else
            {
                DXGKRNL_WARN("DxgkAdapterStart: IoCreateSymbolicLink(DISPLAY) "
                              "failed 0x%08lX (non-fatal)\n", SymStatus);
                InterlockedExchange(&DisplaySymlinkCreated, 0);
            }
        }
    }

    /*
     * Register the display device with win32ss.
     * This creates \Device\Video0, writes the DEVICEMAP\VIDEO registry
     * entries, and writes InstalledDisplayDrivers=cdd for full WDDM
     * adapters (framebuf for display-only fallback) so that win32ss
     * EngpUpdateGraphicsDeviceList can discover the adapter and load the
     * matching display driver.
     *
     * On Windows, dxgkrnl registers with the display subsystem through a
     * different mechanism; on ReactOS we emulate the XPDM device discovery
     * path that win32ss expects.
     */
    {
        NTSTATUS DisplayStatus;

        StepStart100ns = DxgkpTraceNow100ns();
        DisplayStatus = DxgkDisplayRegister(Adapter);
        DisplayUs = DxgkpTraceElapsedUs(StepStart100ns);
        if (!NT_SUCCESS(DisplayStatus))
        {
            DXGKRNL_ERR("DxgkAdapterStart: DxgkDisplayRegister failed "
                         "0x%08lX — continuing without display\n",
                         DisplayStatus);
            /* Non-fatal: adapter is started but display won't work */
        }
    }

    DXGKRNL_TRACE("DxgkAdapterStart: summary connect=%I64u us miniport=%I64u us vidmm=%I64u us vidpn=%I64u us present=%I64u us display=%I64u us total=%I64u us irq=%ld queue=%ld dpc=%ld\n",
                  InterruptConnectUs,
                  MiniportStartUs,
                  VidMmUs,
                  VidPnUs,
                  PresentUs,
                  DisplayUs,
                  DxgkpTraceElapsedUs(AdapterStart100ns),
                  Adapter->InterruptCount,
                  Adapter->QueueDpcCount,
                  Adapter->DpcCount);

    return STATUS_SUCCESS;
}

/*
 * DxgkAdapterStop
 *
 * Called from DxgkpMiniportPnpDispatch in response to IRP_MN_STOP_DEVICE.
 * Tears down the video memory manager and calls DxgkDdiStopDevice.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkAdapterStop(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    NTSTATUS Status;

    PAGED_CODE();

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);

    DXGKRNL_TRACE("DxgkAdapterStop: Adapter %p\n", Adapter);

    if (Adapter->State != DxgkAdapterStateStarted)
    {
        DXGKRNL_WARN("DxgkAdapterStop: adapter not started (State=%d)\n",
                     Adapter->State);
        return STATUS_SUCCESS;
    }

    /* Stop the TDR watchdog before the miniport goes away. */
    DxgkpStopTdrWatchdog(Adapter);

    /* Tear down the present queues before the display device. */
    DxgkPresentTeardown(Adapter);

    /* Unregister the display device from win32ss. */
    DxgkDisplayUnregister();

    /* Tear down the VidPN. */
    if (Adapter->VidPn != NULL)
    {
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)Adapter->VidPn);
        Adapter->VidPn = NULL;
    }

    DxgkDestroySharedPrimary(Adapter);
    DxgkReleaseTrackedDmaBuffers(Adapter);

    /* Tear down the video scheduler before the memory manager. */
    VidSchDestroy(Adapter);

    /* Tear down the video memory manager. */
    DxgkVidMmTeardownAdapter(Adapter);

    if (Adapter->MiniportDeviceStopped)
    {
        /* Already stopped via StopDeviceAndReleasePostDisplayOwnership. */
        Adapter->MiniportDeviceStopped = FALSE;
    }
    else
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiStopDevice(
                     Adapter->MiniportDeviceContext);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkAdapterStop: DxgkDdiStopDevice failed 0x%08lX\n",
                        Status);
            /* Continue with teardown even on failure. */
        }
    }

    DxgkpClearPostDisplayOwner(Adapter);

    Adapter->AllocatedResources  = NULL;
    Adapter->TranslatedResources = NULL;
    Adapter->State = DxgkAdapterStateStopped;
    Adapter->InterruptTraceEpoch100ns = 0;
    DxgkpReleasePostDisplayMapping(Adapter);

    DXGKRNL_TRACE("DxgkAdapterStop: stopped\n");
    return STATUS_SUCCESS;
}

/*
 * DxgkAdapterRemove
 *
 * Called from DxgkpMiniportPnpDispatch in response to IRP_MN_REMOVE_DEVICE
 * (or IRP_MN_SURPRISE_REMOVAL + IRP_MN_REMOVE_DEVICE).  Stops the adapter
 * if still started, calls DxgkDdiRemoveDevice, disconnects the interrupt,
 * frees descriptor arrays, unlinks from the per-miniport and global lists,
 * detaches from the device stack, and deletes the FDO.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkAdapterRemove(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    PAGED_CODE();

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);

    DXGKRNL_TRACE("DxgkAdapterRemove: Adapter %p State=%d\n",
                  Adapter, Adapter->State);

    DxgkReleaseTrackedDmaBuffers(Adapter);

    /* Stop the adapter if it is still running. */
    if (Adapter->State == DxgkAdapterStateStarted)
        DxgkAdapterStop(Adapter);

    /* Call the miniport remove callback. */
    if (Adapter->MiniportDeviceContext != NULL &&
        Adapter->MiniportContext->InitData.s.DxgkDdiRemoveDevice != NULL)
    {
        NTSTATUS Status =
            Adapter->MiniportContext->InitData.s.DxgkDdiRemoveDevice(
                Adapter->MiniportDeviceContext);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkAdapterRemove: DxgkDdiRemoveDevice "
                        "failed 0x%08lX (continuing)\n", Status);
        }
        Adapter->MiniportDeviceContext = NULL;
    }

    DxgkpReleasePostDisplayMapping(Adapter);

    /* Disable and free the GUID_DISPLAY_DEVICE_ARRIVAL device interface. */
    if (Adapter->DeviceInterfaceEnabled)
    {
        IoSetDeviceInterfaceState(&Adapter->DeviceInterfaceName, FALSE);
        Adapter->DeviceInterfaceEnabled = FALSE;
    }
    if (Adapter->DeviceInterfaceName.Buffer != NULL)
    {
        RtlFreeUnicodeString(&Adapter->DeviceInterfaceName);
        RtlInitUnicodeString(&Adapter->DeviceInterfaceName, NULL);
    }

    /* Disconnect the interrupt if registered. */
    if (Adapter->InterruptMessageTable != NULL)
    {
        IO_DISCONNECT_INTERRUPT_PARAMETERS DisconnectParams;
        RtlZeroMemory(&DisconnectParams, sizeof(DisconnectParams));
        DisconnectParams.Version = CONNECT_MESSAGE_BASED;
        DisconnectParams.ConnectionContext.InterruptMessageTable =
            Adapter->InterruptMessageTable;
        IoDisconnectInterruptEx(&DisconnectParams);
        Adapter->InterruptMessageTable = NULL;
        Adapter->InterruptObject = NULL;
    }
    else if (Adapter->InterruptObject != NULL)
    {
        IoDisconnectInterrupt(Adapter->InterruptObject);
        Adapter->InterruptObject = NULL;
    }

    /* Delete all child PDOs. */
    while (!IsListEmpty(&Adapter->ChildListHead))
    {
        PLIST_ENTRY Entry;
        PDXGK_CHILD_PDO_EXTENSION ChildExt;

        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        if (IsListEmpty(&Adapter->ChildListHead))
        {
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            break;
        }
        Entry = RemoveHeadList(&Adapter->ChildListHead);
        if (Adapter->ChildPdoCount > 0)
            Adapter->ChildPdoCount--;
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

        ChildExt = CONTAINING_RECORD(Entry,
                                     DXGK_CHILD_PDO_EXTENSION,
                                     ListEntry);
        InitializeListHead(&ChildExt->ListEntry);
        ChildExt->ParentAdapter = NULL;
        DxgkpDeleteChildPdo(ChildExt);
    }

    /* Free descriptor arrays. */
    if (Adapter->ChildDescriptors != NULL)
    {
        ExFreePoolWithTag(Adapter->ChildDescriptors, TAG_DXGK_RESOURCES);
        Adapter->ChildDescriptors = NULL;
    }
    /*
     * Adapter->Segments (DXGKRNL_SEGMENT array) is freed by
     * DxgkVidMmTeardownAdapter which is called earlier in DxgkAdapterStop.
     * Do not double-free here.
     */

    /* Unlink from per-miniport adapter list. */
    KeAcquireSpinLock(&Adapter->MiniportContext->AdapterListLock, &OldIrql);
    RemoveEntryList(&Adapter->MiniportAdapterListEntry);
    InitializeListHead(&Adapter->MiniportAdapterListEntry);
    if (Adapter->MiniportContext->AdapterCount > 0)
        Adapter->MiniportContext->AdapterCount--;
    KeReleaseSpinLock(&Adapter->MiniportContext->AdapterListLock, OldIrql);

    /* Unlink from global adapter list. */
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);
    RemoveEntryList(&Adapter->GlobalAdapterListEntry);
    InitializeListHead(&Adapter->GlobalAdapterListEntry);
    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);

    /* Detach from the device stack and delete the FDO. */
    if (Adapter->LowerDeviceObject != NULL)
    {
        IoDetachDevice(Adapter->LowerDeviceObject);
        Adapter->LowerDeviceObject = NULL;
    }

    Adapter->State = DxgkAdapterStateRemoved;

    /* The DeviceExtension (Adapter) is freed by IoDeleteDevice below. */
    IoDeleteDevice(Adapter->FunctionalDeviceObject);

    DXGKRNL_TRACE("DxgkAdapterRemove: done\n");
}

/* ========================================================================
 * PnP and Power dispatch routines (installed into miniport DriverObject)
 * ====================================================================== */

/*
 * DxgkpStartDeviceCompletion
 *
 * IoCompletion routine for IRP_MN_START_DEVICE.  Signals the event passed
 * as Context and returns STATUS_MORE_PROCESSING_REQUIRED so that the IRP
 * is not completed twice.  The dispatch routine waits on the event and
 * then completes the IRP itself.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
static NTSTATUS
NTAPI
DxgkpStartDeviceCompletion(
    _In_     PDEVICE_OBJECT DeviceObject,
    _In_     PIRP           Irp,
    _In_opt_ PVOID          Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    if (Context != NULL)
        KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * DxgkpMiniportPnpDispatch
 *
 * IRP_MJ_PNP handler installed into the miniport DriverObject.
 * Handles the subset of PnP minor codes that affect adapter state;
 * all others are forwarded to the lower device object.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkpMiniportPnpDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PDXGKRNL_ADAPTER   Adapter;
    PIO_STACK_LOCATION Stack;
    NTSTATUS           Status;
    ULONGLONG          IrpStart100ns;
    ULONGLONG          LowerStart100ns;
    ULONGLONG          LowerUs = 0;
    ULONGLONG          AdapterStartUs = 0;

    PAGED_CODE();

    /*
     * Route IRPs for non-adapter devices first.
     *
     * The miniport DriverObject is shared by the adapter FDO, child PDOs,
     * \Device\Video0 (display device), and \Device\DxgKrnl (control device).
     * Check these before treating the IRP as an adapter FDO IRP.
     */

    /* Route \Device\Video0 PnP IRPs to the display handler. */
    if (DxgkDisplayDispatchPnp(DeviceObject, Irp))
        return STATUS_SUCCESS;

    /* Route \Device\DxgKrnl PnP IRPs — complete with success (no-op). */
    if (GDxgControlDeviceObject != NULL && DeviceObject == GDxgControlDeviceObject)
    {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    /*
     * Determine whether this IRP targets a child PDO or the GPU FDO.
     *
     * Both the FDO and child PDOs share the same DriverObject dispatch
     * table (because IoCreateDevice uses the FDO's DriverObject).  The
     * first ULONG of the DeviceExtension distinguishes them:
     *
     *   Child PDO  → DXGK_CHILD_PDO_SIGNATURE (0x43786744)
     *   GPU FDO    → PDXGKRNL_MINIPORT_CONTEXT pointer (never == signature)
     *
     * If the device is a child PDO, route to the child-specific handler.
     */
    {
        PULONG Signature = (PULONG)DeviceObject->DeviceExtension;
        if (Signature != NULL && *Signature == DXGK_CHILD_PDO_SIGNATURE)
        {
            return DxgkpChildPdoPnpDispatch(DeviceObject, Irp);
        }
    }

    Adapter = DXGKRNL_ADAPTER_FROM_DEVOBJ(DeviceObject);
    Stack   = IoGetCurrentIrpStackLocation(Irp);

    DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: Adapter %p Minor=%u\n",
                  Adapter, Stack->MinorFunction);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            IrpStart100ns = DxgkpTraceNow100ns();
            /*
             * Forward IRP_MN_START_DEVICE to the lower stack first so that
             * the bus driver can assign resources, then call DxgkAdapterStart.
             *
             * Pattern: copy stack, install a completion routine that sets an
             * event, call the lower driver, wait if pending, then complete.
             */
            {
                KEVENT Event;
                KeInitializeEvent(&Event, NotificationEvent, FALSE);

                IoCopyCurrentIrpStackLocationToNext(Irp);
                IoSetCompletionRoutine(Irp,
                                       DxgkpStartDeviceCompletion,
                                       &Event,
                                       TRUE, TRUE, TRUE);

                LowerStart100ns = DxgkpTraceNow100ns();
                Status = IoCallDriver(Adapter->LowerDeviceObject, Irp);
                if (Status == STATUS_PENDING)
                {
                    KeWaitForSingleObject(&Event, Executive, KernelMode,
                                         FALSE, NULL);
                    Status = Irp->IoStatus.Status;
                }
                LowerUs = DxgkpTraceElapsedUs(LowerStart100ns);
            }

            DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: IRP_MN_START_DEVICE lower stack status=0x%08lX time=%I64u us\n",
                          Status,
                          LowerUs);

            if (NT_SUCCESS(Status))
            {
                LowerStart100ns = DxgkpTraceNow100ns();
                Status = DxgkAdapterStart(
                             Adapter,
                             Stack->Parameters.StartDevice.AllocatedResources,
                             Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
                AdapterStartUs = DxgkpTraceElapsedUs(LowerStart100ns);
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: IRP_MN_START_DEVICE complete status=0x%08lX lower=%I64u us port=%I64u us total=%I64u us\n",
                          Status,
                          LowerUs,
                          AdapterStartUs,
                          DxgkpTraceElapsedUs(IrpStart100ns));
            return Status;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            DEVICE_RELATION_TYPE RelType =
                Stack->Parameters.QueryDeviceRelations.Type;

            DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: "
                          "QUERY_DEVICE_RELATIONS Type=%d\n", RelType);

            if (RelType == BusRelations)
            {
                /*
                 * BusRelations: enumerate child devices (monitors) by
                 * calling DxgkDdiQueryChildRelations on the miniport and
                 * creating PDOs for each reported child.
                 *
                 * Only enumerate if the adapter has been started and the
                 * miniport reported at least one child.
                 */
                if (Adapter->State == DxgkAdapterStateStarted &&
                    Adapter->MiniportContext->InitData.s.DxgkDdiQueryChildRelations != NULL)
                {
                    PDEVICE_RELATIONS Relations = NULL;

                    Status = DxgkpQueryBusRelations(Adapter, &Relations);
                    if (NT_SUCCESS(Status) && Relations != NULL)
                    {
                        DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: "
                                      "BusRelations returning %lu children\n",
                                      Relations->Count);

                        Irp->IoStatus.Information = (ULONG_PTR)Relations;
                        Irp->IoStatus.Status      = STATUS_SUCCESS;
                    }
                    else if (!NT_SUCCESS(Status))
                    {
                        DXGKRNL_ERR("DxgkpMiniportPnpDispatch: "
                                    "DxgkpQueryBusRelations failed 0x%08lX\n",
                                    Status);
                        /*
                         * On failure, forward the IRP so the lower driver
                         * can still report its own bus relations (if any).
                         */
                    }
                }

                /*
                 * Forward BusRelations to the lower driver.  The PnP
                 * manager merges our children with any the bus driver
                 * may report.
                 */
                return DxgkpForwardIrp(Adapter, Irp);
            }
            else if (RelType == TargetDeviceRelation)
            {
                /*
                 * TargetDeviceRelation: return this FDO's underlying PDO.
                 * The PnP manager uses this to find the physical device
                 * for handle-based APIs.
                 */
                PDEVICE_RELATIONS Rel;

                Rel = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
                          PagedPool,
                          sizeof(DEVICE_RELATIONS),
                          TAG_DXGK_RESOURCES);
                if (Rel == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }

                Rel->Count      = 1;
                Rel->Objects[0] = Adapter->PhysicalDeviceObject;
                ObReferenceObject(Adapter->PhysicalDeviceObject);

                Irp->IoStatus.Information = (ULONG_PTR)Rel;
                Irp->IoStatus.Status      = STATUS_SUCCESS;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_SUCCESS;
            }
            else
            {
                /* Other relation types: forward to lower driver. */
                return DxgkpForwardIrp(Adapter, Irp);
            }
        }

        case IRP_MN_STOP_DEVICE:
        {
            DxgkAdapterStop(Adapter);
            return DxgkpForwardIrp(Adapter, Irp);
        }

        case IRP_MN_REMOVE_DEVICE:
        {
            /*
             * Save the lower device object pointer before DxgkAdapterRemove
             * calls IoDeleteDevice and frees the DeviceExtension (Adapter).
             * After IoDeleteDevice the Adapter pointer is invalid.
             *
             * The Windows convention for IRP_MN_REMOVE_DEVICE is:
             *   1. Perform our teardown (DxgkAdapterRemove).
             *   2. Forward the IRP to the lower stack.
             *   3. Do NOT complete the IRP ourselves — the lower driver does.
             *
             * DxgkAdapterRemove calls IoDetachDevice internally, so the lower
             * device object is already detached; we use it only to forward
             * the IRP.  We must not dereference Adapter after the call.
             */
            PDEVICE_OBJECT LowerDevice = Adapter->LowerDeviceObject;

            DxgkAdapterRemove(Adapter);
            /* Adapter is now invalid. */

            if (LowerDevice != NULL)
            {
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoSkipCurrentIrpStackLocation(Irp);
                return IoCallDriver(LowerDevice, Irp);
            }

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }

        case IRP_MN_SURPRISE_REMOVAL:
        {
            Adapter->State = DxgkAdapterStateSurpriseRemoved;
            return DxgkpForwardIrp(Adapter, Irp);
        }

        default:
            return DxgkpForwardIrp(Adapter, Irp);
    }
}

/*
 * DxgkpMiniportPowerDispatch
 *
 * IRP_MJ_POWER handler installed into the miniport DriverObject.
 * Handles device power state changes by calling DxgkDdiSetPowerState
 * with the DISPLAY_ADAPTER_HW_ID device UID (targets the whole GPU).
 *
 * IRQL: PASSIVE_LEVEL for set-power; may be called at DISPATCH_LEVEL
 *       for query-power by some callers — handled by forwarding directly.
 */
NTSTATUS
NTAPI
DxgkpMiniportPowerDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PDXGKRNL_ADAPTER   Adapter;
    PIO_STACK_LOCATION Stack;

    /*
     * Route non-adapter devices: \Device\Video0, \Device\DxgKrnl, child PDOs.
     */
    if (GDxgControlDeviceObject != NULL && DeviceObject == GDxgControlDeviceObject)
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    {
        PULONG Signature = (PULONG)DeviceObject->DeviceExtension;
        if (Signature != NULL && *Signature == DXGK_CHILD_PDO_SIGNATURE)
        {
            PoStartNextPowerIrp(Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
    }

    /* Check for display device (\Device\Video0) — not a real power device. */
    {
        extern PDEVICE_OBJECT g_DisplayDeviceObject;
        if (g_DisplayDeviceObject != NULL && DeviceObject == g_DisplayDeviceObject)
        {
            PoStartNextPowerIrp(Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
    }

    Adapter = DXGKRNL_ADAPTER_FROM_DEVOBJ(DeviceObject);
    Stack   = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MinorFunction == IRP_MN_SET_POWER &&
        Stack->Parameters.Power.Type == DevicePowerState &&
        Adapter->State == DxgkAdapterStateStarted &&
        Adapter->MiniportContext->InitData.s.DxgkDdiSetPowerState != NULL)
    {
        DEVICE_POWER_STATE NewState =
            Stack->Parameters.Power.State.DeviceState;

        DXGKRNL_TRACE("DxgkpMiniportPowerDispatch: SET_POWER D%d\n",
                      NewState - PowerDeviceD0);

        Adapter->MiniportContext->InitData.s.DxgkDdiSetPowerState(
            Adapter->MiniportDeviceContext,
            DISPLAY_ADAPTER_HW_ID,
            NewState,
            Stack->Parameters.Power.ShutdownType);

        Adapter->DevicePowerState = NewState;
    }

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Adapter->LowerDeviceObject, Irp);
}

/*
 * DxgkpAddDevice
 *
 * DRIVER_ADD_DEVICE callback installed into the miniport's DriverObject
 * DriverExtension->AddDevice by DxgkInitializeEx.  Called by the PnP
 * manager when it matches a GPU PDO to this miniport.
 *
 * Creates the FDO, calls DxgkDdiAddDevice, attaches to the device stack,
 * and links the adapter into both per-miniport and global lists.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkpAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    PDEVICE_OBJECT            Fdo;
    PDXGKRNL_ADAPTER          Adapter;
    KIRQL                     OldIrql;
    NTSTATUS                  Status;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkpAddDevice: DriverObject %p PDO %p\n",
                  DriverObject, PhysicalDeviceObject);

    /* Retrieve the per-miniport context from the DriverObjectExtension. */
    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)
            IoGetDriverObjectExtension(DriverObject, DriverObject);
    if (MpCtx == NULL)
    {
        DXGKRNL_ERR("DxgkpAddDevice: no miniport context for %p\n",
                    DriverObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    /*
     * Refuse to add this device if the miniport's DDI version indicates
     * it is actually an XDDM driver (version < WDDM 1.0 threshold).
     * The PnP manager will then try the next compatible driver (videoprt).
     */
    if (MpCtx->InitData.s.Version < DXGKDDI_INTERFACE_VERSION_VISTA)
    {
        DXGKRNL_WARN("DxgkpAddDevice: XDDM miniport (version 0x%lX), "
                     "deferring to videoprt\n", MpCtx->InitData.s.Version);
        return STATUS_NOT_SUPPORTED;
    }

    /* Create the FDO; size the DeviceExtension to hold DXGKRNL_ADAPTER. */
    Status = IoCreateDevice(DriverObject,
                            sizeof(DXGKRNL_ADAPTER),
                            NULL,       /* no device name for FDOs */
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: IoCreateDevice failed 0x%08lX\n",
                    Status);
        return Status;
    }

    Adapter = DXGKRNL_ADAPTER_FROM_DEVOBJ(Fdo);
    RtlZeroMemory(Adapter, sizeof(*Adapter));

    Adapter->MiniportContext         = MpCtx;
    Adapter->FunctionalDeviceObject  = Fdo;
    Adapter->PhysicalDeviceObject    = PhysicalDeviceObject;
    Adapter->State                   = DxgkAdapterStateUninitialized;
    Adapter->DevicePowerState        = PowerDeviceD0;
    Adapter->SystemPowerState        = PowerSystemWorking;

    /* Initialise synchronisation primitives. */
    KeInitializeSpinLock(&Adapter->InterruptLock);
    KeInitializeSpinLock(&Adapter->ChildListLock);
    KeInitializeSpinLock(&Adapter->SubmitDmaLock);
    KeInitializeEvent(&Adapter->SyncEvent, SynchronizationEvent, FALSE);
    ExInitializeFastMutex(&Adapter->AdapterMutex);
    KeInitializeDpc(&Adapter->DpcObject, DxgkpAdapterDpcRoutine, Adapter);
    ExInitializeWorkItem(&Adapter->SubmitDmaRetireWorkItem,
                         DxgkpRetireSubmittedDmaBuffersWorker,
                         Adapter);
    Adapter->SubmitDmaRetireWorkQueued = 0;

    /* Initialise linked lists. */
    InitializeListHead(&Adapter->DeviceListHead);
    InitializeListHead(&Adapter->ChildListHead);
    InitializeListHead(&Adapter->SubmitDmaListHead);
    InitializeListHead(&Adapter->SubmitDmaRetireListHead);
    InitializeListHead(&Adapter->MiniportAdapterListEntry);
    InitializeListHead(&Adapter->GlobalAdapterListEntry);

    /* Call DxgkDdiAddDevice to obtain the miniport's device context. */
    Status = MpCtx->InitData.s.DxgkDdiAddDevice(PhysicalDeviceObject,
                                               &Adapter->MiniportDeviceContext);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: DxgkDdiAddDevice failed 0x%08lX\n",
                    Status);
        IoDeleteDevice(Fdo);
        return Status;
    }

    DXGKRNL_TRACE("DxgkpAddDevice: MiniportDeviceContext = %p\n",
                  Adapter->MiniportDeviceContext);

    /* Attach the FDO to the device stack above the PDO. */
    Adapter->LowerDeviceObject =
        IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (Adapter->LowerDeviceObject == NULL)
    {
        DXGKRNL_ERR("DxgkpAddDevice: IoAttachDeviceToDeviceStack failed\n");
        MpCtx->InitData.s.DxgkDdiRemoveDevice(Adapter->MiniportDeviceContext);
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    /* Propagate alignment requirement from the lower device. */
    Fdo->AlignmentRequirement = Adapter->LowerDeviceObject->AlignmentRequirement;

    /* Register GUID_DISPLAY_DEVICE_ARRIVAL device interface.
     * The interface is registered against the PDO (not the FDO).
     * It will be enabled in DxgkAdapterStart and disabled in DxgkAdapterRemove. */
    RtlInitUnicodeString(&Adapter->DeviceInterfaceName, NULL);
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject,
                                       &GUID_DISPLAY_DEVICE_ARRIVAL,
                                       NULL,
                                       &Adapter->DeviceInterfaceName);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpAddDevice: IoRegisterDeviceInterface failed "
                      "0x%08lX (non-fatal)\n", Status);
        /* Non-fatal: the adapter still functions without PnP notifications. */
        RtlInitUnicodeString(&Adapter->DeviceInterfaceName, NULL);
    }
    else
    {
        DXGKRNL_TRACE("DxgkpAddDevice: registered device interface %wZ\n",
                      &Adapter->DeviceInterfaceName);
    }

    /* Link into per-miniport adapter list. */
    KeAcquireSpinLock(&MpCtx->AdapterListLock, &OldIrql);
    InsertTailList(&MpCtx->AdapterListHead, &Adapter->MiniportAdapterListEntry);
    MpCtx->AdapterCount++;
    KeReleaseSpinLock(&MpCtx->AdapterListLock, OldIrql);

    /* Link into the global adapter list. */
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);
    InsertTailList(&DxgkAdapterGlobalListHead, &Adapter->GlobalAdapterListEntry);
    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);

    Adapter->State = DxgkAdapterStateStopped;

    /* Clear DO_DEVICE_INITIALIZING so the device can receive IRPs. */
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    DXGKRNL_TRACE("DxgkpAddDevice: success — FDO %p Adapter %p\n",
                  Fdo, Adapter);
    return STATUS_SUCCESS;
}

/*
 * DxgkpDriverUnload
 *
 * DRIVER_UNLOAD callback installed into the miniport DriverObject.
 * Called when the miniport is being unloaded.  Calls DxgkDdiUnload
 * if provided, then releases the registry path buffer.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
NTAPI
DxgkpDriverUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkpDriverUnload: DriverObject %p\n", DriverObject);

    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)
            IoGetDriverObjectExtension(DriverObject, DriverObject);

    if (MpCtx == NULL)
        return;

    if (MpCtx->InitData.s.DxgkDdiUnload != NULL)
        MpCtx->InitData.s.DxgkDdiUnload();

    if (MpCtx->RegistryPath.Buffer != NULL)
    {
        ExFreePoolWithTag(MpCtx->RegistryPath.Buffer, TAG_DXGK_REGISTRY);
        MpCtx->RegistryPath.Buffer = NULL;
        MpCtx->RegistryPath.Length = 0;
        MpCtx->RegistryPath.MaximumLength = 0;
    }
}

/* ========================================================================
 * DxgkInitializeEx / DxgkInitialize — miniport registration entry points
 * ====================================================================== */

/*
 * DxgkInitializeEx
 *
 * Called from the miniport's DriverEntry.  Validates the DDI callback table,
 * allocates a DXGKRNL_MINIPORT_CONTEXT as a DriverObjectExtension, copies
 * the callback table, saves a canonical copy of the registry path, and hooks
 * the miniport DriverObject.
 *
 * Parameters:
 *   DriverObject              — miniport's DriverObject (from DriverEntry).
 *   RegistryPath              — miniport's registry path (from DriverEntry).
 *   DriverInitDataSize        — byte size of *DriverInitializationData as
 *                               provided by the miniport.
 *   DriverInitializationData  — miniport DDI callback table.
 *
 * Returns:
 *   STATUS_SUCCESS on success.
 *   STATUS_INVALID_PARAMETER if validation fails.
 *   NTSTATUS error from IoAllocateDriverObjectExtension on allocation failure.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkInitializeEx(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ ULONG                       DriverInitDataSize,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    ULONG                     CopySize;
    PWCH                      RegBuf;
    NTSTATUS                  Status;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkInitializeEx: DriverObject %p RegPath %wZ "
                  "Size=%lu Version=0x%lX\n",
                  DriverObject, RegistryPath,
                  DriverInitDataSize,
                  DriverInitializationData ? DriverInitializationData->Version
                                           : 0);

    /* --- Validate parameters -------------------------------------------- */

    if (DriverObject == NULL ||
        RegistryPath == NULL ||
        DriverInitializationData == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DriverInitDataSize < DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Version))
    {
        DXGKRNL_ERR("DxgkInitializeEx: DriverInitDataSize %lu too small\n",
                    DriverInitDataSize);
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * WDDM 1.0 (Vista) is the minimum supported version.
     * Earlier values indicate an XDDM miniport calling the wrong API.
     */
    if (DriverInitializationData->Version < DXGKDDI_INTERFACE_VERSION_VISTA)
    {
        DXGKRNL_ERR("DxgkInitializeEx: unsupported version 0x%lX "
                    "(minimum 0x%lX)\n",
                    DriverInitializationData->Version,
                    (ULONG)DXGKDDI_INTERFACE_VERSION_VISTA);
        return STATUS_INVALID_PARAMETER;
    }

    if (DriverInitDataSize <
        DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiStartDevice))
    {
        DXGKRNL_ERR("DxgkInitializeEx: DriverInitDataSize %lu too small "
                    "for mandatory callbacks\n",
                    DriverInitDataSize);
        return STATUS_INVALID_PARAMETER;
    }

    if (DriverInitDataSize < DXGKRNL_DRIVER_INIT_DATA_MIN_SIZE &&
        DriverInitDataSize != sizeof(KMDDOD_INITIALIZATION_DATA))
    {
        DXGKRNL_ERR("DxgkInitializeEx: full WDDM init table too small "
                    "%lu < %u\n",
                    DriverInitDataSize,
                    (ULONG)DXGKRNL_DRIVER_INIT_DATA_MIN_SIZE);
        return STATUS_INVALID_PARAMETER;
    }

    /* Mandatory callbacks: AddDevice and StartDevice are always required. */
    if (DriverInitializationData->DxgkDdiAddDevice == NULL ||
        DriverInitializationData->DxgkDdiStartDevice == NULL)
    {
        DXGKRNL_ERR("DxgkInitializeEx: mandatory callback missing "
                    "(AddDevice=%p StartDevice=%p)\n",
                    DriverInitializationData->DxgkDdiAddDevice,
                    DriverInitializationData->DxgkDdiStartDevice);
        return STATUS_INVALID_PARAMETER;
    }

    /* --- One-time global init ------------------------------------------- */

    DxgkpFirstInit();

    /*
     * Create the \Device\DxgKrnl control device if it doesn't exist.
     * When dxgkrnl is loaded as an import dependency (DriverEntry skipped),
     * GDxgControlDeviceObject is NULL.  Create it now using the miniport's
     * DriverObject since all dispatch routines will be overwritten below.
     */
    if (GDxgControlDeviceObject == NULL)
    {
        UNICODE_STRING CtlDevName;

        RtlInitUnicodeString(&CtlDevName, L"\\Device\\DxgKrnl");
        Status = IoCreateDevice(DriverObject,
                                0,
                                &CtlDevName,
                                FILE_DEVICE_UNKNOWN,
                                FILE_DEVICE_SECURE_OPEN,
                                FALSE,
                                &GDxgControlDeviceObject);
        if (Status == STATUS_OBJECT_NAME_COLLISION)
        {
            /* Already exists — DriverEntry did run or another miniport created it. */
            DXGKRNL_TRACE("DxgkInitializeEx: \\Device\\DxgKrnl already exists\n");
            Status = STATUS_SUCCESS;
        }
        else if (NT_SUCCESS(Status))
        {
            UNICODE_STRING SymlinkName, TargetName;
            NTSTATUS SymStatus;

            GDxgControlDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
            DXGKRNL_TRACE("DxgkInitializeEx: created \\Device\\DxgKrnl at %p\n",
                          GDxgControlDeviceObject);

            RtlInitUnicodeString(&SymlinkName, L"\\DosDevices\\DxgKrnl");
            RtlInitUnicodeString(&TargetName, L"\\Device\\DxgKrnl");
            SymStatus = IoCreateSymbolicLink(&SymlinkName, &TargetName);
            if (!NT_SUCCESS(SymStatus) &&
                SymStatus != STATUS_OBJECT_NAME_COLLISION)
            {
                DXGKRNL_WARN("DxgkInitializeEx: IoCreateSymbolicLink(DxgKrnl) "
                             "failed 0x%08lX\n", SymStatus);
            }
        }
        else
        {
            DXGKRNL_ERR("DxgkInitializeEx: IoCreateDevice(DxgKrnl) failed 0x%08lX\n",
                        Status);
            return Status;
        }
    }

    /* --- Allocate the per-miniport context ------------------------------ */

    Status = IoAllocateDriverObjectExtension(DriverObject,
                                             DriverObject, /* unique ID */
                                             sizeof(DXGKRNL_MINIPORT_CONTEXT),
                                             (PVOID *)&MpCtx);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkInitializeEx: IoAllocateDriverObjectExtension "
                    "failed 0x%08lX\n", Status);
        return Status;
    }

    RtlZeroMemory(MpCtx, sizeof(*MpCtx));

    KeInitializeSpinLock(&MpCtx->AdapterListLock);
    InitializeListHead(&MpCtx->AdapterListHead);

    /* --- Copy callback table -------------------------------------------- */

    /*
     * Copy only the minimum of (what the miniport provided, what we know).
     * This forward-compatibility: a WDDM 2.x miniport's larger table is
     * accepted; we just ignore the extra fields.
     */
    CopySize = min(DriverInitDataSize, (ULONG)sizeof(MpCtx->InitData));
    RtlCopyMemory(&MpCtx->InitData, DriverInitializationData, CopySize);
    MpCtx->InitDataSize = DriverInitDataSize;

    /* --- Copy registry path -------------------------------------------- */

    /*
     * Allocate a non-paged buffer for the registry path and copy it.
     * Length is in bytes (as always for UNICODE_STRING); add 2 for NUL.
     */
    RegBuf = (PWCH)ExAllocatePoolWithTag(
                 NonPagedPool,
                 RegistryPath->Length + sizeof(WCHAR),
                 TAG_DXGK_REGISTRY);
    if (RegBuf == NULL)
    {
        DXGKRNL_ERR("DxgkInitializeEx: registry path alloc failed (%u bytes)\n",
                    RegistryPath->Length + (ULONG)sizeof(WCHAR));
        /*
         * Cannot free MpCtx — IoAllocateDriverObjectExtension owns it.
         * The miniport will fail to load.
         */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(RegBuf, RegistryPath->Buffer, RegistryPath->Length);
    RegBuf[RegistryPath->Length / sizeof(WCHAR)] = L'\0';

    MpCtx->RegistryPath.Buffer        = RegBuf;
    MpCtx->RegistryPath.Length        = RegistryPath->Length;
    MpCtx->RegistryPath.MaximumLength = RegistryPath->Length + sizeof(WCHAR);

    /*
     * Recognize the in-box basic-display fallback (softgpu) by its service
     * key name, the same way Windows dxgkrnl special-cases MSBDD.  The
     * fallback yields the boot display to any real miniport that acquires
     * POST display ownership.
     */
    {
        UNICODE_STRING ServiceName;
        ULONG CharIndex = RegistryPath->Length / sizeof(WCHAR);

        while (CharIndex > 0 && RegBuf[CharIndex - 1] != L'\\')
            CharIndex--;

        RtlInitUnicodeString(&ServiceName, &RegBuf[CharIndex]);

        if (ServiceName.Length > 0)
        {
            UNICODE_STRING FallbackName = RTL_CONSTANT_STRING(L"softgpu");

            if (RtlEqualUnicodeString(&ServiceName, &FallbackName, TRUE))
            {
                MpCtx->IsBasicDisplayFallback = TRUE;
                DXGKRNL_TRACE("DxgkInitializeEx: basic-display fallback "
                              "miniport (%wZ)\n", &ServiceName);
            }
        }
    }

    /* --- Hook the miniport DriverObject --------------------------------- */

    DriverObject->DriverExtension->AddDevice           = DxgkpAddDevice;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DxgkDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DxgkDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DxgkDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = DxgkDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP]            = DxgkpMiniportPnpDispatch;
    DriverObject->MajorFunction[IRP_MJ_POWER]          = DxgkpMiniportPowerDispatch;
    DriverObject->DriverUnload                         = DxgkpDriverUnload;

    /*
     * Auto-detect Display-Only Driver (DOD) from the init data content.
     * DOD drivers fill the full DRIVER_INITIALIZATION_DATA but leave
     * DxgkDdiCreateDevice NULL (they don't support device/allocation).
     * This detection works for both DxgkInitialize (IOCTL path, viogpudo)
     * and DxgkInitializeDisplayOnlyDriver (direct import, kmdod).
     */
    if (DriverInitDataSize >=
            DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateDevice) &&
        MpCtx->InitData.s.DxgkDdiCreateDevice == NULL)
    {
        MpCtx->IsDisplayOnlyDriver = TRUE;
        DXGKRNL_TRACE("DxgkInitializeEx: auto-detected DOD (CreateDevice=NULL)\n");
    }

    DXGKRNL_TRACE("DxgkInitializeEx: success — MpCtx %p Version=0x%lX DOD=%d\n",
                  MpCtx, MpCtx->InitData.s.Version, MpCtx->IsDisplayOnlyDriver);
    return STATUS_SUCCESS;
}

/*
 * DxgkInitialize
 *
 * Simplified wrapper around DxgkInitializeEx.  Passes sizeof of the local
 * DRIVER_INITIALIZATION_DATA definition as the size parameter.
 * Exported by dxgkrnl.sys; called from WDDM miniport DriverEntry routines
 * that do not know about DxgkInitializeEx.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkInitialize(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData)
{
    return DxgkInitializeEx(DriverObject,
                            RegistryPath,
                            sizeof(DRIVER_INITIALIZATION_DATA),
                            DriverInitializationData);
}

/*
 * DxgkInitializeDisplayOnlyDriver
 *
 * Entry point for WDDM Display-Only Drivers (DOD).  The KMDDOD_INITIALIZATION_DATA
 * is a subset of DRIVER_INITIALIZATION_DATA with only DOD-relevant callbacks.
 * We treat it as a generic init-data blob and pass it through DxgkInitializeEx.
 *
 * The KMDDOD_INITIALIZATION_DATA struct has the same leading fields as
 * DRIVER_INITIALIZATION_DATA (Version, AddDevice, StartDevice, etc.) so
 * casting is safe for the subset of callbacks DOD drivers provide.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkInitializeDisplayOnlyDriver(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ PKMDDOD_INITIALIZATION_DATA KmDodInitData)
{
    NTSTATUS Status;
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;

    DXGKRNL_TRACE("DxgkInitializeDisplayOnlyDriver: DriverObject %p Version=0x%lX\n",
                  DriverObject, KmDodInitData ? KmDodInitData->Version : 0);

    /*
     * KMDDOD_INITIALIZATION_DATA has a DIFFERENT field layout from
     * DRIVER_INITIALIZATION_DATA — it skips allocation/render/DMA fields.
     * We pass sizeof(KMDDOD_INITIALIZATION_DATA) so DxgkInitializeEx
     * copies the correct amount, then set IsDisplayOnlyDriver so the
     * callback access code uses InitData.dod instead of InitData.s.
     */
    Status = DxgkInitializeEx(DriverObject,
                              RegistryPath,
                              sizeof(KMDDOD_INITIALIZATION_DATA),
                              (PDRIVER_INITIALIZATION_DATA)(PVOID)KmDodInitData);

    if (NT_SUCCESS(Status))
    {
        /* Mark DOD with KMDDOD struct layout. */
        MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)
                IoGetDriverObjectExtension(DriverObject, DriverObject);
        if (MpCtx != NULL)
        {
            MpCtx->IsDisplayOnlyDriver = TRUE;
            MpCtx->UseDodLayout = TRUE;
            DXGKRNL_TRACE("DxgkInitializeDisplayOnlyDriver: marked as DOD "
                          "(sizeof KMDDOD=%Iu)\n", sizeof(KMDDOD_INITIALIZATION_DATA));
        }
    }

    return Status;
}

/* EOF */
