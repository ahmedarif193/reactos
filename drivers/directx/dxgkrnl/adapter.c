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
 * The adapter State field is read/written under AdapterMutex (KMUTEX at
 * APC_LEVEL) from PASSIVE_LEVEL paths and is updated only in the PnP dispatch
 * which is serialised by the I/O manager.  No additional barriers are needed
 * for State.
 *
 * InterruptLock is shared with the ISR at its synchronize IRQL.  A DPC or
 * PASSIVE_LEVEL lifecycle path raises to that IRQL before acquiring the lock;
 * taking it at plain DISPATCH_LEVEL would let the ISR preempt the owner and
 * deadlock on the same lock.  The spinlock acquire/release barriers provide
 * the required cross-architecture visibility.
 *
 * The one-time init guard (DxgkpInitialized) uses InterlockedCompareExchange
 * which on x86-64 compiles to LOCK CMPXCHG — a fully-ordered instruction.
 * No additional fences are required.
 */

/* dxgkrnl_private.h includes NDEBUG, <debug.h>, and "debug.h" via PCH. */
#include "dxgkrnl_private.h"
#include "vidmm.h"
#include "vidsch.h"
#include "present.h"
#include "pnp.h"
#include "context.h"
#include "dxgmms2_client.h"
#include "submit_reservation_core.h"
#include "hotplug_work_core.h"

#include <reactos/arc/arc.h>
/*
 * ntddvdeo.h is already included via dxgkrnl_private.h (before INITGUID),
 * so DEFINE_GUID only produced an extern declaration.  Instantiate here.
 */
const GUID GUID_DISPLAY_DEVICE_ARRIVAL =
    {0x1ca05180, 0xa699, 0x450a, {0x9a, 0x0c, 0xde, 0x4f, 0xbe, 0x3d, 0xdd, 0x89}};

#define DXGKP_BUGCHECK_VIDEO_DXGKRNL_FATAL_ERROR 0x113
#define DXGKP_FATAL_SURPRISE_REMOVAL_SUBTYPE 0x19
#define DXGKP_FATAL_MMS2_LIFECYCLE_SUBTYPE 0x1A
#define DXGKP_GPUMMU_END_TO_END 1
#define DXGKP_MMS2_FAILURE_ADD_ROLLBACK 1
#define DXGKP_MMS2_FAILURE_ATTACH_ROLLBACK 2
#define DXGKP_MMS2_FAILURE_FINAL_RETIREMENT 3
#define DXGKP_MMS2_FAILURE_FINAL_DESTROY 4
#define DXGKP_MINIPORT_CONTEXT_SIGNATURE 'MkgD'
#define DXGKP_DIAGNOSTIC_BUFFER_SIZE 0x80000UL

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
static KSPIN_LOCK g_PostDisplayOwnerLock;
static KMUTEX g_PostDisplayOwnershipMutex;
static KMUTEX g_MiniportRegistrationMutex;
static UCHAR g_MiniportContextClientId;
static PDXGKRNL_ADAPTER g_PostDisplayOwnerAdapter = NULL;

static NTSTATUS
DxgkpAdapterStopInternal(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN ReleasePostDisplayOwnership,
    _In_ DXGMMS2_STOP_REASON StopReason);

static NTSTATUS
DxgkpStopMiniportForTeardown(
    _In_ PDXGKRNL_ADAPTER Adapter);

/* The published owner holds one FDO reference. A transient reference returned
 * here keeps both the device extension and Owner valid after dropping the lock. */
static PDXGKRNL_ADAPTER
DxgkpReferencePostDisplayOwner(
    _Out_ PDEVICE_OBJECT *OwnerDeviceObject)
{
    PDXGKRNL_ADAPTER Owner;
    PDEVICE_OBJECT DeviceObject = NULL;
    KIRQL OldIrql;

    *OwnerDeviceObject = NULL;
    KeAcquireSpinLock(&g_PostDisplayOwnerLock, &OldIrql);
    Owner = g_PostDisplayOwnerAdapter;
    if (Owner != NULL)
    {
        DeviceObject = Owner->FunctionalDeviceObject;
        if (DeviceObject != NULL)
            ObReferenceObject(DeviceObject);
        else
            Owner = NULL;
    }
    KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
    *OwnerDeviceObject = DeviceObject;
    return Owner;
}

static VOID
DxgkpSetPostDisplayOwner(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_ADAPTER PreviousOwner;
    PDEVICE_OBJECT NewDeviceObject;
    PDEVICE_OBJECT ReleasedDeviceObject = NULL;
    KIRQL OldIrql;

    if (Adapter == NULL || Adapter->FunctionalDeviceObject == NULL)
        return;
    NewDeviceObject = Adapter->FunctionalDeviceObject;
    ObReferenceObject(NewDeviceObject);
    KeAcquireSpinLock(&g_PostDisplayOwnerLock, &OldIrql);
    PreviousOwner = g_PostDisplayOwnerAdapter;
    if (PreviousOwner == Adapter)
    {
        KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
        ObDereferenceObject(NewDeviceObject);
        return;
    }
    if (PreviousOwner != NULL)
        ReleasedDeviceObject = PreviousOwner->FunctionalDeviceObject;
    g_PostDisplayOwnerAdapter = Adapter;
    KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
    if (ReleasedDeviceObject != NULL)
        ObDereferenceObject(ReleasedDeviceObject);
}

/* Called from stop/remove so a dead adapter never stays published. */
static VOID
DxgkpClearPostDisplayOwner(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDEVICE_OBJECT ReleasedDeviceObject = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&g_PostDisplayOwnerLock, &OldIrql);
    if (g_PostDisplayOwnerAdapter == Adapter)
    {
        ReleasedDeviceObject = Adapter->FunctionalDeviceObject;
        g_PostDisplayOwnerAdapter = NULL;
    }
    KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
    if (ReleasedDeviceObject != NULL)
        ObDereferenceObject(ReleasedDeviceObject);
}

/*
 * Stop the adapter currently holding the boot display so a new claimant can
 * take over.  Uses the documented Win8 handover DDI
 * (DxgkDdiStopDeviceAndReleasePostDisplayOwnership) when the owner
 * implements it, then runs the generic adapter stop (which unregisters the
 * \Device\VideoN display device so the claimant can register its own).
 */
static NTSTATUS
DxgkpStopPostDisplayOwner(
    _In_ PDXGKRNL_ADAPTER Owner)
{
    NTSTATUS Status;

    DXGKRNL_WARN("DxgkpStopPostDisplayOwner: stopping %s adapter %p — a new miniport is acquiring the boot display\n", (Owner->MiniportContext != NULL && Owner->MiniportContext->IsBasicDisplayFallback) ? "basic-display fallback" : "display", Owner);
    Status = DxgkpAdapterStopInternal(Owner, TRUE, Dxgmms2StopReasonPnpStop);
    if (NT_SUCCESS(Status))
        DxgkpClearPostDisplayOwner(Owner);
    return Status;
}

static NTSTATUS
DxgkpRemoveMiniportDevice(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PVOID MiniportDeviceContext)
{
    NTSTATUS Status;

    if (Adapter == NULL || Adapter->MiniportContext == NULL || Adapter->MiniportContext->InitData.s.DxgkDdiRemoveDevice == NULL || MiniportDeviceContext == NULL)
        return STATUS_NOT_SUPPORTED;
    _SEH2_TRY
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiRemoveDevice(MiniportDeviceContext);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
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
    PDXGKRNL_ADAPTER Owner;
    PDXGKDDI_SYSTEM_DISPLAY_ENABLE PfnEnable;
    DXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags;
    UINT Width = 0;
    UINT Height = 0;
    D3DDDIFORMAT ColorFormat = D3DDDIFMT_UNKNOWN;
    KIRQL CurrentIrql;
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN LockAtDpcLevel;

    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);

    CurrentIrql = KeGetCurrentIrql();
    LockAtDpcLevel = (CurrentIrql >= DISPATCH_LEVEL);
    if (LockAtDpcLevel)
    {
        if (!KeTryToAcquireSpinLockAtDpcLevel(&g_PostDisplayOwnerLock))
            return;
    }
    else
    {
        KeAcquireSpinLock(&g_PostDisplayOwnerLock, &OldIrql);
    }

    Owner = g_PostDisplayOwnerAdapter;
    if (Owner == NULL || Owner->State != DxgkAdapterStateStarted || Owner->MiniportContext == NULL)
        goto ReleaseLock;

    PfnEnable = DXGK_CB(Owner, DxgkDdiSystemDisplayEnable);
    if (PfnEnable == NULL)
        goto ReleaseLock;

    RtlZeroMemory(&Flags, sizeof(Flags));
    (VOID)PfnEnable(Owner->MiniportDeviceContext, 0, &Flags, &Width, &Height, &ColorFormat);

ReleaseLock:
    if (LockAtDpcLevel)
        KeReleaseSpinLockFromDpcLevel(&g_PostDisplayOwnerLock);
    else
        KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
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
 * A per-adapter timer watches the oldest tracked submission and measures
 * elapsed time since the last observed fence progress.  A work item performs
 * the documented timeout recovery after the configured TdrDelay:
 * DxgkDdiResetFromTimeout -> DxgkDdiRestartFromTimeout -> retire.
 * ====================================================================== */

#define DXGKP_TDR_TICK_MS 100

static DECLSPEC_NORETURN VOID
DxgkpBugCheckTdrFailure(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ NTSTATUS FailureStatus)
{
    PVOID RecoveryContext;
    ULONG_PTR OwnerTag = 0;

    DXGKRNL_ERR("DxgkpBugCheckTdrFailure: unrecoverable TDR on adapter %p status 0x%08lX\n", Adapter, FailureStatus);
    RecoveryContext = InterlockedCompareExchangePointer((PVOID volatile *)&Adapter->TdrRecoveryContext, NULL, NULL);
    if (Adapter->MiniportContext != NULL && DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout) != NULL)
        OwnerTag = (ULONG_PTR)DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout);
    KeBugCheckEx(0x116, (ULONG_PTR)RecoveryContext, OwnerTag, (ULONG_PTR)FailureStatus, 0);
}

static VOID
NTAPI
DxgkpTdrDdiDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    if (Adapter != NULL && InterlockedCompareExchange(&Adapter->TdrDdiTimerArmed, 1, 1) != 0)
        DxgkpBugCheckTdrFailure(Adapter, STATUS_IO_TIMEOUT);
}

VOID
DxgkpArmTdrDdiDeadline(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Due;
    ULONGLONG Due100ns = (ULONGLONG)Adapter->TdrConfig.TdrDdiDelay * 10000000ULL;

    if (Due100ns == 0)
        Due100ns = 1;
    if (Due100ns > (ULONGLONG)MAXLONGLONG)
        Due100ns = (ULONGLONG)MAXLONGLONG;
    Due.QuadPart = -(LONGLONG)Due100ns;
    InterlockedExchange(&Adapter->TdrDdiTimerArmed, 1);
    KeSetTimer(&Adapter->TdrDdiTimer, Due, &Adapter->TdrDdiDpc);
}

VOID
DxgkpDisarmTdrDdiDeadline(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    InterlockedExchange(&Adapter->TdrDdiTimerArmed, 0);
    KeCancelTimer(&Adapter->TdrDdiTimer);
    KeRemoveQueueDpc(&Adapter->TdrDdiDpc);
    KeFlushQueuedDpcs();
}

static BOOLEAN
DxgkpTdrRecoveryLimitExhausted(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONGLONG Now100ns)
{
    ULONGLONG Window100ns = (ULONGLONG)Adapter->TdrConfig.TdrLimitTime * 10000000ULL;
    ULONG Count = 0;
    ULONG Index;
    KIRQL OldIrql;

    if (Adapter->TdrConfig.TdrDebugMode == DXGKP_TDR_DEBUG_RECOVER_UNCONDITIONAL)
        return FALSE;
    if (Adapter->TdrConfig.TdrLimitCount == 0)
        return TRUE;
    KeAcquireSpinLock(&Adapter->TdrHistoryLock, &OldIrql);
    for (Index = 0; Index < Adapter->TdrRecoveryEntryCount; ++Index)
    {
        ULONGLONG Timestamp = Adapter->TdrRecoveryTimestamps[Index];

        if (Timestamp != 0 && Now100ns >= Timestamp && Now100ns - Timestamp <= Window100ns)
            Count++;
    }
    KeReleaseSpinLock(&Adapter->TdrHistoryLock, OldIrql);
    return Count >= Adapter->TdrConfig.TdrLimitCount;
}

static VOID
DxgkpRecordTdrRecovery(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONGLONG Timestamp100ns)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->TdrHistoryLock, &OldIrql);
    Adapter->TdrRecoveryTimestamps[Adapter->TdrRecoveryWriteIndex] = Timestamp100ns;
    Adapter->TdrRecoveryWriteIndex = (Adapter->TdrRecoveryWriteIndex + 1) % DXGKP_TDR_HISTORY_CAPACITY;
    if (Adapter->TdrRecoveryEntryCount < DXGKP_TDR_HISTORY_CAPACITY)
        Adapter->TdrRecoveryEntryCount++;
    KeReleaseSpinLock(&Adapter->TdrHistoryLock, OldIrql);
}

static VOID
NTAPI
DxgkpTdrWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;
    ULONG WorkFence;
    ULONG WorkNode;
    ULONG WorkEngine;
    ULONG PreemptionFenceId = 0;
    ULONG CompletedBeforePreempt;
    ULONG CompletedAfterPreempt;
    BOOLEAN AdapterStartedAfterReset;
    BOOLEAN DdiDeadlineArmed = FALSE;
    BOOLEAN Level3Transition = FALSE;
    BOOLEAN PresentResetStarted = FALSE;
    BOOLEAN RecoveryContextPublished = FALSE;
    BOOLEAN SchedulerPrepared = FALSE;
    PVOID RecoveryContext = NULL;
    NTSTATUS Status;

    if (Adapter == NULL)
        return;

    Status = TdrCreateRecoveryContext(&RecoveryContext, Adapter);
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckTdrFailure(Adapter, Status);
    if (InterlockedCompareExchangePointer((PVOID volatile *)&Adapter->TdrRecoveryContext, RecoveryContext, NULL) != NULL)
        goto Exit;
    RecoveryContextPublished = TRUE;
    DxgkpArmTdrDdiDeadline(Adapter);
    DdiDeadlineArmed = TRUE;
    DxgkAcquireLevel3Transition(Adapter);
    Level3Transition = TRUE;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0)
        goto Exit;
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_OFF)
        goto Exit;
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_BUGCHECK)
        DxgkpBugCheckTdrFailure(Adapter, STATUS_IO_TIMEOUT);
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_RECOVER_VGA)
        DxgkpBugCheckTdrFailure(Adapter, STATUS_NOT_SUPPORTED);
    if (DxgkpTdrRecoveryLimitExhausted(Adapter, KeQueryInterruptTime()))
        DxgkpBugCheckTdrFailure(Adapter, STATUS_IO_TIMEOUT);
    if (Adapter->TdrConfig.TdrDebugMode == DXGKP_TDR_DEBUG_BREAK && !KD_DEBUGGER_NOT_PRESENT)
        DbgBreakPoint();

    WorkFence = Adapter->TdrWorkFence;
    WorkNode = Adapter->TdrWorkNode;
    WorkEngine = Adapter->TdrWorkEngine;

    DXGKRNL_ERR("DxgkpTdrWorker: GPU timeout — fence %lu stuck on adapter %p\n", WorkFence, Adapter);

    /* Attempt engine preemption first and give the miniport a short window to
     * report DMA_PREEMPTED progress. A preempted but incomplete packet is not
     * completion: until resubmission exists, it must continue into TDR reset. */
    if (WorkNode >= Adapter->NodeCount || WorkNode >= DXGK_MAX_TRACKED_NODES)
        DxgkpBugCheckTdrFailure(Adapter, STATUS_INVALID_PARAMETER);
    if (!DxgkIsSubmittedFenceIdentity(Adapter, WorkNode, WorkFence))
        goto Exit;
    CompletedBeforePreempt = Adapter->NodeLastCompletedFenceId[WorkNode];
    if ((LONG)(CompletedBeforePreempt - WorkFence) >= 0)
    {
        Adapter->TdrStuckTicks = 0;
        goto Exit;
    }
    Status = VidSchPreemptEngine(Adapter, WorkNode, WorkEngine, &PreemptionFenceId);
    if (NT_SUCCESS(Status))
    {
        if (PreemptionFenceId != 0)
            (VOID)VidSchWaitForPreemption(Adapter, WorkNode, WorkEngine, PreemptionFenceId, 100);
        CompletedAfterPreempt = Adapter->NodeLastCompletedFenceId[WorkNode];
        if ((LONG)(CompletedAfterPreempt - WorkFence) >= 0)
        {
            DXGKRNL_ERR("DxgkpTdrWorker: preemption recovered adapter %p (fence %lu -> %lu), skipping reset\n", Adapter, CompletedBeforePreempt, CompletedAfterPreempt);
            Adapter->TdrStuckTicks = 0;
            goto Exit;
        }
    }

    DXGKRNL_ERR("DxgkpTdrWorker: preemption did not recover — resetting "
                "adapter %p\n", Adapter);

    DxgkPresentBeginReset(Adapter);
    PresentResetStarted = TRUE;
    Status = VidSchPrepareAdapterReset(Adapter);
    if (NT_SUCCESS(Status))
        SchedulerPrepared = TRUE;
    else if (Status != STATUS_NOT_SUPPORTED)
    {
        DXGKRNL_ERR("DxgkpTdrWorker: scheduler reset preparation failed 0x%08lX\n", Status);
        DxgkpBugCheckTdrFailure(Adapter, Status);
    }

    DxgkBeginKmdExclusive(Adapter);
    DxgkVidMmQuiesceAdapter(Adapter);
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        if (SchedulerPrepared)
            VidSchCompleteAdapterReset(Adapter, FALSE);
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0 || Adapter->MiniportDeviceContext == NULL)
    {
        DxgkReleaseMiniportCallback(Adapter);
        if (SchedulerPrepared)
            VidSchCompleteAdapterReset(Adapter, FALSE);
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }

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
            (VOID)DXGK_CB(Adapter, DxgkDdiCollectDbgInfo)(Adapter->MiniportDeviceContext, &CollectArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    Status = STATUS_NOT_SUPPORTED;
    if (DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout) != NULL)
    {
        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout)(Adapter->MiniportDeviceContext);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

    }
    DxgkReleaseMiniportCallback(Adapter);

    if (!NT_SUCCESS(Status))
        DxgkpBugCheckTdrFailure(Adapter, Status);
    InterlockedExchange(&Adapter->TdrCompletionNotificationsEnabled, 0);
    DxgkDrainVidSchCallbacks(Adapter);
    AdapterStartedAfterReset = Adapter->State == DxgkAdapterStateStarted;
    DxgkTdrResetAdapterSynchronizationObjects(Adapter);
    Status = DxgkVidMmRecoverFromTimeout(Adapter);
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckTdrFailure(Adapter, Status);
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    if (SchedulerPrepared)
        VidSchCompleteAdapterReset(Adapter, TRUE);
    DxgkReleaseTrackedDmaBuffers(Adapter, TRUE);
    DxgkResetSubmittedFenceIdentities(Adapter);
    if (!AdapterStartedAfterReset || Adapter->State != DxgkAdapterStateStarted)
    {
        Adapter->TdrStuckTicks = 0;
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }

    Status = STATUS_NOT_SUPPORTED;
    if (DxgkAcquireMiniportCallback(Adapter))
    {
        if (Adapter->State == DxgkAdapterStateStarted && DXGK_CB_FULL(Adapter, DxgkDdiRestartFromTimeout) != NULL)
        {
            _SEH2_TRY
            {
                Status = DXGK_CB_FULL(Adapter, DxgkDdiRestartFromTimeout)(Adapter->MiniportDeviceContext);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        DxgkReleaseMiniportCallback(Adapter);
    }
    if (Adapter->State != DxgkAdapterStateStarted)
    {
        Adapter->TdrStuckTicks = 0;
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckTdrFailure(Adapter, Status);

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->State != DxgkAdapterStateStarted || Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0 || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        Adapter->TdrStuckTicks = 0;
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }
    DxgkVidMmResumeAdapter(Adapter);
    if (SchedulerPrepared)
    {
        Status = VidSchResumeScheduler(Adapter);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
            DxgkpBugCheckTdrFailure(Adapter, Status);
        }
    }
    Adapter->TdrStuckTicks = 0;
    DxgkpRecordTdrRecovery(Adapter, KeQueryInterruptTime());
    InterlockedExchange(&Adapter->TdrCompletionNotificationsEnabled, 1);
    DxgkEndKmdExclusive(Adapter, TRUE);
    DxgkPresentCompleteReset(Adapter);
    PresentResetStarted = FALSE;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

Exit:
    if (PresentResetStarted)
        DxgkPresentCompleteReset(Adapter);
    if (DdiDeadlineArmed)
        DxgkpDisarmTdrDdiDeadline(Adapter);
    if (RecoveryContext != NULL)
    {
        if (RecoveryContextPublished)
            InterlockedCompareExchangePointer((PVOID volatile *)&Adapter->TdrRecoveryContext, NULL, RecoveryContext);
        (VOID)TdrCompleteRecoveryContext(RecoveryContext);
    }
    ExReleaseRundownProtection(&Adapter->RundownRef);
    InterlockedExchange(&Adapter->TdrWorkQueued, 0);
    if (Level3Transition)
        DxgkReleaseLevel3Transition(Adapter);
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
    ULONG HeadNode = 0;
    ULONG HeadEngine = 0;
    ULONG CompletedFence = 0;
    ULONG SchedulerFence = 0;
    ULONG SchedulerNode = 0;
    ULONG SchedulerEngine = 0;
    ULONGLONG Delay100ns;
    ULONGLONG Now100ns;
    BOOLEAN Outstanding = FALSE;
    BOOLEAN SchedulerOutstanding;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0 || InterlockedCompareExchangePointer((PVOID volatile *)&Adapter->TdrRecoveryContext, NULL, NULL) != NULL)
        return;
    Now100ns = KeQueryInterruptTime();

    KeAcquireSpinLockAtDpcLevel(&Adapter->SubmitDmaLock);
    if (!IsListEmpty(&Adapter->SubmitDmaListHead))
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Head = CONTAINING_RECORD(Adapter->SubmitDmaListHead.Flink, DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
        HeadFence = Head->SubmissionFenceId;
        HeadNode = Head->NodeOrdinal;
        HeadEngine = Head->EngineOrdinal;
        Outstanding = TRUE;
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->SubmitDmaLock);

    SchedulerOutstanding = VidSchGetOldestKickedPacket(Adapter, &SchedulerFence, &SchedulerNode, &SchedulerEngine);
    if (SchedulerOutstanding && (!Outstanding || (LONG)(SchedulerFence - HeadFence) < 0))
    {
        HeadFence = SchedulerFence;
        HeadNode = SchedulerNode;
        HeadEngine = SchedulerEngine;
        Outstanding = TRUE;
    }

    if (Outstanding)
    {
        if (HeadNode >= Adapter->NodeCount || HeadNode >= DXGK_MAX_TRACKED_NODES)
            return;
        CompletedFence = Adapter->NodeLastCompletedFenceId[HeadNode];
    }
    if (!Outstanding || (LONG)(CompletedFence - HeadFence) >= 0)
    {
        /* Idle, or completed but not yet retired: not stuck. */
        Adapter->TdrStuckTicks = 0;
        Adapter->TdrLastObservedFence = HeadFence;
        Adapter->TdrLastObservedNode = HeadNode;
        Adapter->TdrLastObservedEngine = HeadEngine;
        Adapter->TdrLastObservedCompletedFence = CompletedFence;
        Adapter->TdrLastProgressTime100ns = Now100ns;
        return;
    }

    if (HeadFence != Adapter->TdrLastObservedFence || HeadNode != Adapter->TdrLastObservedNode || HeadEngine != Adapter->TdrLastObservedEngine || CompletedFence != Adapter->TdrLastObservedCompletedFence)
    {
        Adapter->TdrLastObservedFence = HeadFence;
        Adapter->TdrLastObservedNode = HeadNode;
        Adapter->TdrLastObservedEngine = HeadEngine;
        Adapter->TdrLastObservedCompletedFence = CompletedFence;
        Adapter->TdrStuckTicks = 0;
        Adapter->TdrLastProgressTime100ns = Now100ns;
        return;
    }

    Delay100ns = (ULONGLONG)Adapter->TdrConfig.TdrDelay * 10000000ULL;
    if (Now100ns < Adapter->TdrLastProgressTime100ns || Now100ns - Adapter->TdrLastProgressTime100ns < Delay100ns)
        return;
    Adapter->TdrLastProgressTime100ns = Now100ns;
    if (Adapter->TdrConfig.TdrDebugMode == DXGKP_TDR_DEBUG_IGNORE_TIMEOUT)
        return;

    {
        Adapter->TdrStuckTicks = 0;
        if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || !ExAcquireRundownProtection(&Adapter->RundownRef))
            return;
        if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0)
        {
            ExReleaseRundownProtection(&Adapter->RundownRef);
            return;
        }
        if (InterlockedCompareExchange(&Adapter->TdrWorkQueued, 1, 0) == 0)
        {
            if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0)
            {
                InterlockedExchange(&Adapter->TdrWorkQueued, 0);
                ExReleaseRundownProtection(&Adapter->RundownRef);
                return;
            }
            Adapter->TdrWorkFence = HeadFence;
            Adapter->TdrWorkNode = HeadNode;
            Adapter->TdrWorkEngine = HeadEngine;
            KeMemoryBarrier();
            ExQueueWorkItem(&Adapter->TdrWorkItem, DelayedWorkQueue);
        }
        else
        {
            ExReleaseRundownProtection(&Adapter->RundownRef);
        }
    }
}

static VOID
DxgkpStartTdrWatchdog(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Due;

    KeInitializeTimer(&Adapter->TdrTimer);
    KeInitializeDpc(&Adapter->TdrDpc, DxgkpTdrDpcRoutine, Adapter);
    KeInitializeTimer(&Adapter->TdrDdiTimer);
    KeInitializeDpc(&Adapter->TdrDdiDpc, DxgkpTdrDdiDpcRoutine, Adapter);
    ExInitializeWorkItem(&Adapter->TdrWorkItem, DxgkpTdrWorker, Adapter);
    Adapter->TdrWorkQueued = 0;
    Adapter->TdrOwnershipUncertain = 0;
    Adapter->TdrWorkFence = 0;
    Adapter->TdrWorkNode = 0;
    Adapter->TdrWorkEngine = 0;
    Adapter->TdrLastObservedFence = 0;
    Adapter->TdrLastObservedNode = 0;
    Adapter->TdrLastObservedEngine = 0;
    Adapter->TdrLastObservedCompletedFence = 0;
    Adapter->TdrStuckTicks = 0;
    Adapter->TdrLastProgressTime100ns = KeQueryInterruptTime();
    Adapter->TdrDdiTimerArmed = 0;
    Adapter->TdrRecoveryContext = NULL;
    DxgkResetSubmittedFenceIdentities(Adapter);
    InterlockedExchange(&Adapter->TdrCompletionNotificationsEnabled, 1);
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_OFF)
    {
        InterlockedExchange(&Adapter->TdrTimerActive, 0);
        return;
    }
    InterlockedExchange(&Adapter->TdrTimerActive, 1);

    Due.QuadPart = -10000LL * DXGKP_TDR_TICK_MS;
    KeSetTimerEx(&Adapter->TdrTimer, Due, DXGKP_TDR_TICK_MS, &Adapter->TdrDpc);
}

static VOID
DxgkpStopTdrWatchdog(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (InterlockedExchange(&Adapter->TdrTimerActive, 0) != 0)
    {
        KeCancelTimer(&Adapter->TdrTimer);
        KeRemoveQueueDpc(&Adapter->TdrDpc);
        KeFlushQueuedDpcs();
    }

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
 * One-time init guard.  0 = not yet initialised, 1 = initialising,
 * 2 = initialised, 3 = initialization failed.  Contending callers wait until
 * the first caller publishes either the complete global state or its failure.
 */
static LONG DxgkpInitialized = 0;
static NTSTATUS DxgkpInitializationStatus = STATUS_UNSUCCESSFUL;
static FAST_MUTEX DxgkpMapMemoryMutex;
static LIST_ENTRY DxgkpMapMemoryList;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
static FAST_MUTEX DxgkpCallbackMemoryMutex;
static LIST_ENTRY DxgkpCallbackMemoryList;
#endif
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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
typedef enum _DXGKP_CALLBACK_MEMORY_KIND
{
    DxgkpCallbackMemoryContiguous,
    DxgkpCallbackMemoryMdl
} DXGKP_CALLBACK_MEMORY_KIND;

typedef struct _DXGKP_CALLBACK_MEMORY_ENTRY
{
    LIST_ENTRY ListEntry;
    PDXGKRNL_ADAPTER Adapter;
    DXGKP_CALLBACK_MEMORY_KIND Kind;
    union
    {
        PVOID ContiguousMemory;
        PMDL Mdl;
    } Memory;
} DXGKP_CALLBACK_MEMORY_ENTRY, *PDXGKP_CALLBACK_MEMORY_ENTRY;
#endif

/*
 * Timing for the traces below.
 *
 * KeQueryInterruptTime only advances on a clock interrupt -- a 1 ms tick here --
 * so anything shorter than that measures as either zero or exactly one tick.
 * Used for short operations it does not report a duration at all: it reports
 * whether a tick happened to land inside the call.  That is how every one of
 * six "slow config read" warnings came out at exactly 1000 us with no spread,
 * which was read as a millisecond-long PCI read and is nothing of the kind.
 *
 * The performance counter runs off the ARM64 generic timer and has resolution
 * far below a microsecond, so a duration measured with it is a duration.
 */
FORCEINLINE ULONGLONG
DxgkpTraceNow100ns(VOID)
{
    LARGE_INTEGER Counter;

    Counter = KeQueryPerformanceCounter(NULL);
    return (ULONGLONG)Counter.QuadPart;
}

FORCEINLINE ULONGLONG
DxgkpTraceElapsedUs(
    _In_ ULONGLONG StartTicks)
{
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Counter;
    ULONGLONG EndTicks;

    Counter = KeQueryPerformanceCounter(&Frequency);
    EndTicks = (ULONGLONG)Counter.QuadPart;

    if (EndTicks <= StartTicks || Frequency.QuadPart <= 0)
        return 0;

    return ((EndTicks - StartTicks) * 1000000ULL) / (ULONGLONG)Frequency.QuadPart;
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
DxgkpUpdateCompletedFence(
    _Inout_ volatile ULONG *Fence,
    _In_ ULONG CompletedFence)
{
    LONG Current;

    for (;;)
    {
        Current = InterlockedCompareExchange((volatile LONG *)Fence, 0, 0);
        if (DxgkpFenceIdReached((ULONG)Current, CompletedFence) || InterlockedCompareExchange((volatile LONG *)Fence, (LONG)CompletedFence, Current) == Current)
            return;
    }
}

static VOID
DxgkpReleasePostDisplayMapping(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    SIZE_T FbSize;

    if (Adapter == NULL)
        return;

    if (Adapter->PostDisplayVirtualAddress != NULL)
    {
        FbSize = Adapter->PostDisplayMappingSize;
        if (FbSize == 0)
        {
            FbSize =
                (SIZE_T)Adapter->PostDisplayPitch *
                Adapter->PostDisplayHeight;
        }
        ASSERT(FbSize != 0);
        if (FbSize != 0)
        {
            MmUnmapIoSpace(Adapter->PostDisplayVirtualAddress, FbSize);
        }
    }

    Adapter->PostDisplayVirtualAddress = NULL;
    Adapter->PostDisplayMappingSize = 0;
    Adapter->PostDisplayPhysicalAddress.QuadPart = 0;
    Adapter->PostDisplayPitch = 0;
    Adapter->PostDisplayWidth = 0;
    Adapter->PostDisplayHeight = 0;
}

static ULONG
DxgkpShadowAllocateSubmissionFenceId(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG CurrentFenceId;
    ULONG NextFenceId;

    if (Adapter == NULL)
        return 0;

    for (;;)
    {
        CurrentFenceId = (ULONG)InterlockedCompareExchange(&Adapter->NextSubmissionFenceId, 0, 0);
        NextFenceId = CurrentFenceId + 1;
        if (NextFenceId == 0)
            NextFenceId = 1;
        if ((ULONG)InterlockedCompareExchange(&Adapter->NextSubmissionFenceId, (LONG)NextFenceId, (LONG)CurrentFenceId) == CurrentFenceId)
            return NextFenceId;
    }
}

static BOOLEAN
DxgkpShadowReserveSubmissionFenceIdentity(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LONG64 Identity;
    LONG64 CurrentIdentity;
    LONG64 PreviousIdentity;
    ULONG FirstTombstone;
    ULONG Probe;
    ULONG Slot;
    ULONG StartSlot;
    ULONG TargetSlot;
    LONG64 TargetValue;

    if (Adapter == NULL || NodeOrdinal >= Adapter->NodeCount || NodeOrdinal >= DXGK_MAX_TRACKED_NODES || SubmissionFenceId == 0)
        return FALSE;
    Identity = (LONG64)(((ULONGLONG)(NodeOrdinal + 1) << 32) | SubmissionFenceId);
    StartSlot = (ULONG)(((ULONGLONG)Identity ^ ((ULONGLONG)Identity >> 32)) * 2654435761ULL) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
    for (;;)
    {
        FirstTombstone = DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY;
        for (Probe = 0; Probe < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Probe)
        {
            Slot = (StartSlot + Probe) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
            CurrentIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0, 0);
            if (CurrentIdentity == Identity || CurrentIdentity == (LONG64)((ULONGLONG)Identity | DXGK_SUBMITTED_FENCE_PUBLISHED_BIT))
                return FALSE;
            if (CurrentIdentity == DXGK_SUBMITTED_FENCE_TOMBSTONE && FirstTombstone == DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY)
                FirstTombstone = Slot;
            if (CurrentIdentity == 0)
                break;
        }
        if (Probe == DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY && FirstTombstone == DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY)
            return FALSE;
        TargetSlot = FirstTombstone != DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY ? FirstTombstone : Slot;
        TargetValue = FirstTombstone != DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY ? DXGK_SUBMITTED_FENCE_TOMBSTONE : 0;
        PreviousIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[TargetSlot], Identity, TargetValue);
        if (PreviousIdentity == TargetValue)
            return TRUE;
    }
}

static VOID
DxgkpShadowPublishSubmittedFence(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LONG64 Identity;
    LONG64 PublishedIdentity;
    LONG64 CurrentIdentity;
    ULONG Probe;
    ULONG Slot;
    ULONG StartSlot;

    if (Adapter == NULL || NodeOrdinal >= Adapter->NodeCount || NodeOrdinal >= DXGK_MAX_TRACKED_NODES || SubmissionFenceId == 0)
        KeBugCheckEx(0x119, 0x1, (ULONG_PTR)SubmissionFenceId, (ULONG_PTR)NodeOrdinal, (ULONG_PTR)Adapter);
    Identity = (LONG64)(((ULONGLONG)(NodeOrdinal + 1) << 32) | SubmissionFenceId);
    PublishedIdentity = (LONG64)((ULONGLONG)Identity | DXGK_SUBMITTED_FENCE_PUBLISHED_BIT);
    StartSlot = (ULONG)(((ULONGLONG)Identity ^ ((ULONGLONG)Identity >> 32)) * 2654435761ULL) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
    for (Probe = 0; Probe < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Probe)
    {
        Slot = (StartSlot + Probe) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
        CurrentIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0, 0);
        if (CurrentIdentity == PublishedIdentity)
            break;
        if (CurrentIdentity == 0)
        {
            Probe = DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY;
            break;
        }
        if (CurrentIdentity == Identity && InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], PublishedIdentity, Identity) == Identity)
            break;
    }
    if (Probe == DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY)
        KeBugCheckEx(0x119, 0x1, (ULONG_PTR)SubmissionFenceId, (ULONG_PTR)NodeOrdinal, (ULONG_PTR)Adapter);
    DxgkpUpdateCompletedFence(&Adapter->NodeLastSubmittedFenceId[NodeOrdinal], SubmissionFenceId);
}

static BOOLEAN
DxgkpShadowIsSubmittedFenceIdentity(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LONG64 Identity;
    LONG64 PublishedIdentity;
    LONG64 CurrentIdentity;
    ULONG Probe;
    ULONG Slot;
    ULONG StartSlot;

    if (Adapter == NULL || NodeOrdinal >= Adapter->NodeCount || NodeOrdinal >= DXGK_MAX_TRACKED_NODES || SubmissionFenceId == 0)
        return FALSE;
    Identity = (LONG64)(((ULONGLONG)(NodeOrdinal + 1) << 32) | SubmissionFenceId);
    PublishedIdentity = (LONG64)((ULONGLONG)Identity | DXGK_SUBMITTED_FENCE_PUBLISHED_BIT);
    StartSlot = (ULONG)(((ULONGLONG)Identity ^ ((ULONGLONG)Identity >> 32)) * 2654435761ULL) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
    for (Probe = 0; Probe < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Probe)
    {
        Slot = (StartSlot + Probe) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
        CurrentIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0, 0);
        if (CurrentIdentity == PublishedIdentity)
            return TRUE;
        if (CurrentIdentity == 0)
            return FALSE;
    }
    return FALSE;
}

static BOOLEAN
DxgkpShadowReleaseSubmittedFenceIdentity(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LONG64 Identity;
    LONG64 PublishedIdentity;
    LONG64 CurrentIdentity;
    ULONG Probe;
    ULONG Slot;
    ULONG StartSlot;

    if (Adapter == NULL || NodeOrdinal >= DXGK_MAX_TRACKED_NODES || SubmissionFenceId == 0)
        return FALSE;
    Identity = (LONG64)(((ULONGLONG)(NodeOrdinal + 1) << 32) | SubmissionFenceId);
    PublishedIdentity = (LONG64)((ULONGLONG)Identity | DXGK_SUBMITTED_FENCE_PUBLISHED_BIT);
    StartSlot = (ULONG)(((ULONGLONG)Identity ^ ((ULONGLONG)Identity >> 32)) * 2654435761ULL) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
    for (Probe = 0; Probe < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Probe)
    {
        Slot = (StartSlot + Probe) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
        CurrentIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0, 0);
        if (CurrentIdentity == 0)
            return FALSE;
        if (CurrentIdentity != Identity && CurrentIdentity != PublishedIdentity)
            continue;
        if (InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], DXGK_SUBMITTED_FENCE_TOMBSTONE, CurrentIdentity) == CurrentIdentity)
            return TRUE;
    }
    return FALSE;
}

static VOID
DxgkpShadowResetSubmittedFenceIdentities(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG Slot;

    if (Adapter == NULL)
        return;
    for (Slot = 0; Slot < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Slot)
        InterlockedExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0);
}

static DECLSPEC_NORETURN VOID DxgkpBugCheckMms2Timeline(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    KeBugCheckEx(0x119, 0x1, (ULONG_PTR)FenceId, (ULONG_PTR)NodeOrdinal, (ULONG_PTR)Adapter);
}

static BOOLEAN DxgkpAcquireMms2TimelineCall(_In_ PDXGKRNL_ADAPTER Adapter, _Out_ DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 *Timeline)
{
    LONG ActiveCalls;

    if (Adapter == NULL || Timeline == NULL)
        return FALSE;
    ActiveCalls = InterlockedIncrement(&Adapter->Mms2TimelineActiveCalls);
    ASSERT(ActiveCalls > 0);
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Adapter->Mms2TimelineCallsOpen, 0, 0) == 0 || InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0)
    {
        ActiveCalls = InterlockedDecrement(&Adapter->Mms2TimelineActiveCalls);
        ASSERT(ActiveCalls >= 0);
        return FALSE;
    }
    *Timeline = Adapter->Mms2Timeline;
    return TRUE;
}

static VOID DxgkpReleaseMms2TimelineCall(_In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG ActiveCalls = InterlockedDecrement(&Adapter->Mms2TimelineActiveCalls);

    ASSERT(ActiveCalls >= 0);
}

static BOOLEAN DxgkpCloseMms2TimelineCalls(_In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Delay;
    BOOLEAN WasOpen;

    PAGED_CODE();
    WasOpen = InterlockedExchange(&Adapter->Mms2TimelineCallsOpen, 0) != 0;
    KeMemoryBarrier();
    Delay.QuadPart = -10000;
    while (InterlockedCompareExchange(&Adapter->Mms2TimelineActiveCalls, 0, 0) != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    KeMemoryBarrier();
    ASSERT(!WasOpen || InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0);
    return WasOpen;
}

static VOID DxgkpPublishMms2TimelineCalls(_In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->Mms2TimelineValid, 1);
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->Mms2TimelineCallsOpen, 1);
}

static VOID DxgkpReopenMms2TimelineCalls(_In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    ASSERT(InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0);
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->Mms2TimelineCallsOpen, 1);
}

ULONG NTAPI DxgkAllocateSubmissionFenceId(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    ULONG ProviderFence;

    if (Adapter == NULL)
        return 0;
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
        return InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0 ? DxgkpShadowAllocateSubmissionFenceId(Adapter) : 0;
    ProviderFence = Timeline.AllocateFence(Timeline.TimelineHandle, Timeline.Generation);
    if (ProviderFence == 0)
    {
        DxgkpReleaseMms2TimelineCall(Adapter);
        return 0;
    }
    DxgkpReleaseMms2TimelineCall(Adapter);
    return ProviderFence;
}

BOOLEAN NTAPI DxgkReserveSubmissionFenceIdentity(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG SubmissionFenceId)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    BOOLEAN ProviderReserved;

    if (Adapter == NULL)
        return FALSE;
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
        return InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0 ? DxgkpShadowReserveSubmissionFenceIdentity(Adapter, NodeOrdinal, SubmissionFenceId) : FALSE;
    ProviderReserved = Timeline.ReserveFence(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, SubmissionFenceId);
    DxgkpReleaseMms2TimelineCall(Adapter);
    return ProviderReserved;
}

VOID NTAPI DxgkPublishSubmittedFence(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG SubmissionFenceId)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;

    if (Adapter == NULL)
        return;
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
    {
        if (InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0)
            DxgkpShadowPublishSubmittedFence(Adapter, NodeOrdinal, SubmissionFenceId);
        else
            DxgkpBugCheckMms2Timeline(Adapter, NodeOrdinal, SubmissionFenceId);
        return;
    }
    if (!Timeline.PublishFence(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, SubmissionFenceId))
        DxgkpBugCheckMms2Timeline(Adapter, NodeOrdinal, SubmissionFenceId);
    DxgkpUpdateCompletedFence(&Adapter->NodeLastSubmittedFenceId[NodeOrdinal], SubmissionFenceId);
    DxgkpReleaseMms2TimelineCall(Adapter);
}

BOOLEAN NTAPI DxgkIsSubmittedFenceIdentity(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG SubmissionFenceId)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    BOOLEAN ProviderPublished;

    if (Adapter == NULL)
        return FALSE;
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
        return InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0 ? DxgkpShadowIsSubmittedFenceIdentity(Adapter, NodeOrdinal, SubmissionFenceId) : FALSE;
    ProviderPublished = Timeline.IsFencePublished(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, SubmissionFenceId);
    DxgkpReleaseMms2TimelineCall(Adapter);
    return ProviderPublished;
}

VOID NTAPI DxgkReleaseSubmittedFenceIdentity(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG SubmissionFenceId)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;

    if (Adapter == NULL)
        return;
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
    {
        if (InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0)
            (VOID)DxgkpShadowReleaseSubmittedFenceIdentity(Adapter, NodeOrdinal, SubmissionFenceId);
        else
            DxgkpBugCheckMms2Timeline(Adapter, NodeOrdinal, SubmissionFenceId);
        return;
    }
    if (!Timeline.ReleaseFence(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, SubmissionFenceId))
        DxgkpBugCheckMms2Timeline(Adapter, NodeOrdinal, SubmissionFenceId);
    DxgkpReleaseMms2TimelineCall(Adapter);
}

VOID NTAPI DxgkResetSubmittedFenceIdentities(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    BOOLEAN TimelineWasPublished;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL)
        return;
    if (InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0)
    {
        DxgkpShadowResetSubmittedFenceIdentities(Adapter);
        return;
    }
    TimelineWasPublished = DxgkpCloseMms2TimelineCalls(Adapter);
    if (!TimelineWasPublished)
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    Timeline = Adapter->Mms2Timeline;
    Status = Timeline.ResetFenceIdentities(Timeline.TimelineHandle, Timeline.Generation);
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    DxgkpShadowResetSubmittedFenceIdentities(Adapter);
    DxgkpReopenMms2TimelineCalls(Adapter);
}

NTSTATUS NTAPI DxgkNotifySubmissionFenceCompletion(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId, _In_ BOOLEAN Preempted, _Out_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    NTSTATUS Status;

    if (Adapter == NULL || Snapshot == NULL || !DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
        return STATUS_INVALID_DEVICE_STATE;
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    Snapshot->Size = DXGMMS2_FENCE_SNAPSHOT_V1_SIZE;
    Snapshot->Version = DXGMMS2_SCHEDULER_TIMELINE_VERSION_1;
    Status = Timeline.NotifyFenceCompletion(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, FenceId, Preempted ? DXGMMS2_TIMELINE_NOTIFY_PREEMPTED : 0, Snapshot);
    if (!NT_SUCCESS(Status))
    {
        DxgkpReleaseMms2TimelineCall(Adapter);
        return Status;
    }
    if (Snapshot->Size != DXGMMS2_FENCE_SNAPSHOT_V1_SIZE || Snapshot->Version != DXGMMS2_SCHEDULER_TIMELINE_VERSION_1 || Snapshot->Generation != Timeline.Generation || Snapshot->NodeOrdinal != NodeOrdinal)
    {
        DxgkpReleaseMms2TimelineCall(Adapter);
        return STATUS_DATA_ERROR;
    }
    DxgkpUpdateCompletedFence(&Adapter->NodeLastSubmittedFenceId[NodeOrdinal], Snapshot->LastSubmittedFence);
    DxgkpUpdateCompletedFence(&Adapter->NodeLastCompletedFenceId[NodeOrdinal], Snapshot->LastCompletedFence);
    DxgkpUpdateCompletedFence(&Adapter->LastCompletedSubmissionFenceId, Snapshot->GlobalLastCompletedFence);
    DxgkpReleaseMms2TimelineCall(Adapter);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkAllocateDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Capacity,
    _Out_ PDXGKRNL_DMA_BUFFER *OutDmaBuffer)
{
    PDXGKRNL_DMA_BUFFER DmaBuffer;
    PHYSICAL_ADDRESS LowestAddress;
    PHYSICAL_ADDRESS HighestAddress;
    PHYSICAL_ADDRESS BoundaryAddress;

    if (Adapter == NULL || Capacity == 0 || OutDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutDmaBuffer = NULL;
    DmaBuffer = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DmaBuffer), TAG_DXGK_SUBMITDMA);
    if (DmaBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(DmaBuffer, sizeof(*DmaBuffer));
    LowestAddress.QuadPart = 0;
    HighestAddress = Adapter->HighestAcceptableAddress;
    if (HighestAddress.QuadPart == 0)
        HighestAddress.QuadPart = (LONGLONG)-1;
    BoundaryAddress.QuadPart = 0;
    DmaBuffer->VirtualAddress = MmAllocateContiguousMemorySpecifyCache(Capacity, LowestAddress, HighestAddress, BoundaryAddress, MmCached);
    if (DmaBuffer->VirtualAddress == NULL)
    {
        ExFreePoolWithTag(DmaBuffer, TAG_DXGK_SUBMITDMA);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DmaBuffer->Capacity = Capacity;
    DmaBuffer->SubmissionStartOffset = 0;
    DmaBuffer->SubmissionEndOffset = 0;
    DmaBuffer->SegmentId = 0;
    DmaBuffer->SegmentAddress = MmGetPhysicalAddress(DmaBuffer->VirtualAddress);
    DmaBuffer->BackingKind = DxgkDmaBackingContiguousMemory;
    *OutDmaBuffer = DmaBuffer;
    return STATUS_SUCCESS;
}

VOID
NTAPI
DxgkFreeDmaBuffer(
    _In_opt_ PDXGKRNL_DMA_BUFFER DmaBuffer)
{
    if (DmaBuffer == NULL)
        return;

    if (DmaBuffer->VirtualAddress != NULL && DmaBuffer->BackingKind == DxgkDmaBackingContiguousMemory)
        MmFreeContiguousMemory(DmaBuffer->VirtualAddress);
    DmaBuffer->VirtualAddress = NULL;
    DmaBuffer->BackingKind = DxgkDmaBackingInvalid;
    ExFreePoolWithTag(DmaBuffer, TAG_DXGK_SUBMITDMA);
}

static VOID DxgkpAssertSubmitDmaReservationInvariantLocked(_In_ PDXGKRNL_ADAPTER Adapter)
{
    ASSERT(Adapter->SubmitDmaActiveReservations >= 0);
    ASSERT((KeReadStateEvent(&Adapter->SubmitDmaReservationsDrainedEvent) != 0) == DxgkSubmitReservationCoreIsDrainedLocked(&Adapter->SubmitDmaActiveReservations));
}

static BOOLEAN DxgkpAcquireSubmitDmaReservation(_In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN Acquired;
    BOOLEAN ClearDrainedEvent;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    Acquired = DxgkSubmitReservationCoreTryAcquireLocked(&Adapter->SubmitDmaActiveReservations, InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0, &ClearDrainedEvent);
    if (Acquired && ClearDrainedEvent)
        KeClearEvent(&Adapter->SubmitDmaReservationsDrainedEvent);
    DxgkpAssertSubmitDmaReservationInvariantLocked(Adapter);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
    return Acquired;
}

static VOID DxgkpReleaseSubmitDmaReservationLocked(_In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN Released;
    BOOLEAN SetDrainedEvent;

    Released = DxgkSubmitReservationCoreReleaseLocked(&Adapter->SubmitDmaActiveReservations, &SetDrainedEvent);
    ASSERT(Released);
    if (SetDrainedEvent)
        KeSetEvent(&Adapter->SubmitDmaReservationsDrainedEvent, IO_NO_INCREMENT, FALSE);
    DxgkpAssertSubmitDmaReservationInvariantLocked(Adapter);
}

static VOID DxgkpReleaseSubmitDmaReservation(_In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    DxgkpReleaseSubmitDmaReservationLocked(Adapter);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
}

VOID NTAPI DxgkWaitForSubmitDmaReservations(_In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN Drained;
    KIRQL OldIrql;

    PAGED_CODE();
    if (Adapter == NULL)
        return;
    for (;;)
    {
        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        ASSERT(InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0);
        Drained = DxgkSubmitReservationCoreIsDrainedLocked(&Adapter->SubmitDmaActiveReservations);
        DxgkpAssertSubmitDmaReservationInvariantLocked(Adapter);
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        if (Drained)
            return;
        KeWaitForSingleObject(&Adapter->SubmitDmaReservationsDrainedEvent, Executive, KernelMode, FALSE, NULL);
    }
}

static VOID NTAPI DxgkpTrackedWorkAdjustInFlight(_In_opt_ PVOID Context, _In_ LONG Delta)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Entry = Context;
    BOOLEAN Changed;

    if (Entry == NULL || Entry->Device == NULL || Entry->Device->ProcessRecord == NULL)
        return;
    if (Delta > 0)
    {
        Changed = DxgkSubmissionAccountingCommitLocked(&Entry->SubmissionAccounting, &Entry->Device->InFlightSubmissions, &Entry->Device->ProcessRecord->InFlightSubmissions);
        ASSERT(Changed);
    }
    else if (Delta < 0)
    {
        Changed = DxgkSubmissionAccountingRelease(&Entry->SubmissionAccounting, &Entry->Device->InFlightSubmissions, &Entry->Device->ProcessRecord->InFlightSubmissions);
        ASSERT(Changed);
    }
}

static NTSTATUS DxgkpPrechargeTrackedSubmission(_In_ PDXGKRNL_ADAPTER Adapter, _Inout_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    BOOLEAN Charged;
    KIRQL OldIrql;

    if (Adapter == NULL || Entry == NULL || Entry->Device == NULL || Entry->Device->ProcessRecord == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    Charged = DxgkSubmissionAccountingTryPrechargeLocked(&Entry->SubmissionAccounting, &Entry->Device->InFlightSubmissions, &Entry->Device->ProcessRecord->InFlightSubmissions, DXGK_DEVICE_MAX_INFLIGHT, DXGK_PROCESS_MAX_INFLIGHT);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
    return Charged ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
}

static VOID NTAPI DxgkpTrackedWorkPublishSignal(_In_opt_ PVOID Context)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Entry = Context;

    if (Entry != NULL &&
        Entry->SignalSyncObjectReference != NULL
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
        && !Entry->SignalWrittenByGpu
#endif
        )
    {
        DxgkSyncObjectPublishTrackedSignal(Entry->SignalSyncObjectReference, Entry->SignalFenceValue);
    }
}

static VOID NTAPI DxgkpTrackedWorkCompleteDeviceWork(_In_opt_ PVOID Context)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Entry = Context;

    if (Entry != NULL)
        DxgkDeviceWorkComplete(Entry->DeviceWork);
}

static const DXGK_TRACKED_WORK_CALLBACKS DxgkpTrackedWorkCallbacks = { DxgkpTrackedWorkAdjustInFlight, DxgkpTrackedWorkPublishSignal, DxgkpTrackedWorkCompleteDeviceWork };

static VOID DxgkpFreeTrackedDmaBufferEntry(_In_opt_ PDXGKRNL_ADAPTER Adapter, _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry, _In_ BOOLEAN Completed, _In_ BOOLEAN FreeDmaBuffer, _In_ BOOLEAN MiniportCallbacksValid);

static NTSTATUS
DxgkpReferenceTrackedAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation)
{
    if (Adapter == NULL || Allocation == NULL || OutAllocation == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutAllocation = NULL;
    if (Allocation->Adapter != Adapter || !DxgkVidMmDuplicateAllocationReference(Allocation))
        return STATUS_INVALID_HANDLE;
    *OutAllocation = Allocation;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpReferenceTrackedLogicalAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation)
{
    if (Adapter == NULL || Allocation == NULL || OutAllocation == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAllocation = NULL;
    if (Allocation->Adapter != Adapter || !DxgkVidMmDuplicateLogicalReference(Allocation))
        return STATUS_INVALID_HANDLE;
    *OutAllocation = Allocation;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkPrepareTrackedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const DXGKRNL_TRACK_DMA_ARGS *Args,
    _Out_ PDXGKRNL_SUBMIT_DMA_BUFFER *OutEntry)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Entry;
    NTSTATUS Status;
    UINT Index;

    if (OutEntry == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutEntry = NULL;
    if (Adapter == NULL || Args == NULL || Args->DmaBuffer == NULL || Args->SubmissionFenceId == 0 || Args->NodeOrdinal >= Adapter->NodeCount || Args->NodeOrdinal >= DXGK_MAX_TRACKED_NODES || Args->DmaBuffer->VirtualAddress == NULL || Args->DmaBuffer->SubmissionStartOffset > Args->DmaBuffer->SubmissionEndOffset || Args->DmaBuffer->SubmissionEndOffset > Args->DmaBuffer->Capacity || (Args->PresentBindingReferenceCount != 0 && (Args->PresentBindingReferences == NULL || Args->Device == NULL)) || (Args->OpenBindingReferenceCount != 0 && Args->OpenBindingReferences == NULL) || (Args->AllocationReferenceCount != 0 && Args->AllocationReferences == NULL) || (Args->LifetimeAllocationReferenceCount != 0 && Args->LifetimeAllocationReferences == NULL) || (SIZE_T)Args->PresentBindingReferenceCount > MAXULONG_PTR / sizeof(PDXGKVMM_ALLOCATION) || (SIZE_T)Args->OpenBindingReferenceCount > MAXULONG_PTR / sizeof(PDXGKVMM_ALLOCATION) || (SIZE_T)Args->AllocationReferenceCount > MAXULONG_PTR / sizeof(PDXGKVMM_ALLOCATION) || (SIZE_T)Args->LifetimeAllocationReferenceCount > MAXULONG_PTR / sizeof(PDXGKVMM_ALLOCATION))
        return STATUS_INVALID_PARAMETER;
    if (!DxgkpAcquireSubmitDmaReservation(Adapter))
        return STATUS_DELETE_PENDING;

    Entry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Entry), TAG_DXGK_SUBMITDMA);
    if (Entry == NULL)
    {
        DxgkpReleaseSubmitDmaReservation(Adapter);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Entry, sizeof(*Entry));
    InitializeListHead(&Entry->ListEntry);
    DxgkTrackedWorkCoreInitialize(&Entry->TrackedWork, &DxgkpTrackedWorkCallbacks, Entry, FALSE);
    DxgkSubmissionAccountingInitialize(&Entry->SubmissionAccounting);

    Entry->Adapter = Adapter;
    Entry->ReservationActive = 1;
    if (Args->HoldSharedSurfaceRundown)
    {
        if (!ExAcquireRundownProtection(&Adapter->SharedSurfaceRundown))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_DELETE_PENDING;
        }
        Entry->SharedSurfaceRundownHeld = TRUE;
    }
    Entry->SubmissionFenceId = Args->SubmissionFenceId;
    Entry->NodeOrdinal = Args->NodeOrdinal;
    Entry->EngineOrdinal = Args->EngineOrdinal;
    Entry->SignalFenceValue = Args->SignalFenceValue;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    Entry->SignalWrittenByGpu = Args->SignalWrittenByGpu;
    if (Entry->SignalWrittenByGpu &&
        Args->SignalSyncObjectReference == NULL)
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_INVALID_PARAMETER;
    }
#endif
    Entry->RefreshPresentId = Args->PresentId;
    Entry->DmaBuffer = Args->DmaBuffer;
    if (Args->Device != NULL && !DxgkReferenceDevice(Args->Device))
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_DELETE_PENDING;
    }
    Entry->Device = Args->Device;
    if (Entry->Device != NULL && Entry->Device->ProcessRecord == NULL)
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Args->EnforceSubmissionQuota)
    {
        Status = DxgkpPrechargeTrackedSubmission(Adapter, Entry);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->DeviceWork != NULL)
    {
        if (Entry->Device == NULL || Args->DeviceWork->Device != Entry->Device)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INVALID_PARAMETER;
        }
        Entry->DeviceWork = Args->DeviceWork;
    }
    else if (Entry->Device != NULL)
    {
        Status = DxgkDeviceWorkCreate(Entry->Device, &Entry->DeviceWork);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
        Status = DxgkTrackedWorkCoreClaimDeviceWork(&Entry->TrackedWork) ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    if (Args->SignalSyncObjectReference != NULL)
    {
        Status =
            DxgkSyncObjectDuplicateTrackedSignal(
                Args->SignalSyncObjectReference,
                Entry->Device,
                &Entry->SignalSyncObjectReference);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    else
#endif
    if (Args->hSignalSyncObject != 0)
    {
        Status = DxgkSyncObjectReferenceTrackedSignal(Args->hSignalSyncObject, Entry->Device, &Entry->SignalSyncObjectReference);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->Context != NULL && !DxgkReferenceContext(Args->Context))
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_DELETE_PENDING;
    }
    Entry->Context = Args->Context;
    if (Entry->Context != NULL && (Entry->Context->Device == NULL || Entry->Context->Device->Adapter != Adapter || (Entry->Device != NULL && Entry->Context->Device != Entry->Device)))
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_INVALID_PARAMETER;
    }
    Entry->SourceAllocationHandle = Args->SourceAllocationHandle;
    Entry->RefreshAllocationHandle = Args->RefreshAllocationHandle;
    if (Args->SourceAllocation != NULL)
    {
        Status = DxgkpReferenceTrackedAllocation(Adapter, Args->SourceAllocation, &Entry->SourceAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    else if (Args->SourceAllocationHandle != NULL)
    {
        Status = DxgkVidMmReferenceAllocation(Args->SourceAllocationHandle, Adapter, NULL, &Entry->SourceAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->RefreshAllocation != NULL)
    {
        Status = DxgkpReferenceTrackedAllocation(Adapter, Args->RefreshAllocation, &Entry->RefreshAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    else if (Args->RefreshAllocationHandle != NULL)
    {
        Status = DxgkVidMmReferenceAllocation(Args->RefreshAllocationHandle, Adapter, NULL, &Entry->RefreshAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->SourceOpenBindingReference != NULL)
    {
        Status = DxgkpReferenceTrackedLogicalAllocation(Adapter, Args->SourceOpenBindingReference, &Entry->SourceOpenBindingReference);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->DestinationOpenBindingReference != NULL)
    {
        Status = DxgkpReferenceTrackedLogicalAllocation(Adapter, Args->DestinationOpenBindingReference, &Entry->DestinationOpenBindingReference);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    Entry->RefreshVidPnSourceId = Args->RefreshVidPnSourceId;
    if (Args->RefreshDstRect != NULL)
        Entry->RefreshDstRect = *Args->RefreshDstRect;
    else
        RtlZeroMemory(&Entry->RefreshDstRect, sizeof(Entry->RefreshDstRect));
    Entry->RefreshSharedPrimaryOnRetire = Args->RefreshIsSharedPrimary && Args->RefreshAllocationHandle != NULL;
    Entry->ProgramSourceScanoutOnRetire =
        Args->ProgramSourceScanoutOnRetire &&
        Args->SourceAllocationHandle != NULL;
    Entry->SharedSurfaceGeneration = Args->SharedSurfaceGeneration;
    Entry->SourceIsSharedPrimary = Args->SourceIsSharedPrimary;
    Entry->SourceIsSharedShadow = Args->SourceIsSharedShadow;
    Entry->SourceWidth = Args->SourceWidth;
    Entry->SourceHeight = Args->SourceHeight;
    Entry->SourcePitch = Args->SourcePitch;
    Entry->RefreshWidth = Args->RefreshWidth;
    Entry->RefreshHeight = Args->RefreshHeight;
    Entry->PresentBindingReferenceCount = 0;
    Entry->PresentBindingReferenceList = NULL;
    Entry->OpenBindingReferenceCount = 0;
    Entry->OpenBindingReferenceList = NULL;
    Entry->AllocationReferenceCount = 0;
    Entry->AllocationReferenceList = NULL;
    Entry->LifetimeAllocationReferenceCount = 0;
    Entry->LifetimeAllocationReferenceList = NULL;

    if (Args->PresentBindingReferenceCount != 0)
    {
        Entry->PresentBindingReferenceList = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Args->PresentBindingReferenceCount * sizeof(*Entry->PresentBindingReferenceList), TAG_DXGK_SUBMITDMA);
        if (Entry->PresentBindingReferenceList == NULL)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Entry->PresentBindingReferenceList, (SIZE_T)Args->PresentBindingReferenceCount * sizeof(*Entry->PresentBindingReferenceList));
        for (Index = 0; Index < Args->PresentBindingReferenceCount; ++Index)
        {
            Status = DxgkpReferenceTrackedLogicalAllocation(Adapter, Args->PresentBindingReferences[Index], &Entry->PresentBindingReferenceList[Index]);
            if (!NT_SUCCESS(Status))
            {
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            Entry->PresentBindingReferenceCount++;
        }
    }

    if (Args->OpenBindingReferenceCount != 0)
    {
        Entry->OpenBindingReferenceList = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Args->OpenBindingReferenceCount * sizeof(*Entry->OpenBindingReferenceList), TAG_DXGK_SUBMITDMA);
        if (Entry->OpenBindingReferenceList == NULL)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Entry->OpenBindingReferenceList, (SIZE_T)Args->OpenBindingReferenceCount * sizeof(*Entry->OpenBindingReferenceList));
        for (Index = 0; Index < Args->OpenBindingReferenceCount; ++Index)
        {
            Status = DxgkpReferenceTrackedLogicalAllocation(Adapter, Args->OpenBindingReferences[Index], &Entry->OpenBindingReferenceList[Index]);
            if (!NT_SUCCESS(Status))
            {
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            Entry->OpenBindingReferenceCount++;
        }
    }

    if (Args->AllocationReferenceCount != 0)
    {
        Entry->AllocationReferenceList = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Args->AllocationReferenceCount * sizeof(*Entry->AllocationReferenceList), TAG_DXGK_SUBMITDMA);
        if (Entry->AllocationReferenceList == NULL)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Entry->AllocationReferenceList, (SIZE_T)Args->AllocationReferenceCount * sizeof(*Entry->AllocationReferenceList));
        for (Index = 0; Index < Args->AllocationReferenceCount; ++Index)
        {
            Status = DxgkpReferenceTrackedAllocation(Adapter, Args->AllocationReferences[Index], &Entry->AllocationReferenceList[Index]);
            if (!NT_SUCCESS(Status))
            {
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            Status = DxgkVidMmAcquireSubmissionResidencyPin(Entry->AllocationReferenceList[Index], Adapter, NULL);
            if (!NT_SUCCESS(Status))
            {
                DxgkVidMmDereferenceAllocation(Entry->AllocationReferenceList[Index]);
                Entry->AllocationReferenceList[Index] = NULL;
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            Entry->AllocationReferenceCount++;
        }
    }

    if (Args->LifetimeAllocationReferenceCount != 0)
    {
        Entry->LifetimeAllocationReferenceList =
            ExAllocatePoolWithTag(
                NonPagedPool,
                (SIZE_T)Args->LifetimeAllocationReferenceCount *
                    sizeof(*Entry->LifetimeAllocationReferenceList),
                TAG_DXGK_SUBMITDMA);
        if (Entry->LifetimeAllocationReferenceList == NULL)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(
            Entry->LifetimeAllocationReferenceList,
            (SIZE_T)Args->LifetimeAllocationReferenceCount *
                sizeof(*Entry->LifetimeAllocationReferenceList));
        for (Index = 0;
             Index < Args->LifetimeAllocationReferenceCount;
             ++Index)
        {
            Status = DxgkpReferenceTrackedAllocation(
                         Adapter,
                         Args->LifetimeAllocationReferences[Index],
                         &Entry->LifetimeAllocationReferenceList[Index]);
            if (!NT_SUCCESS(Status))
            {
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            Entry->LifetimeAllocationReferenceCount++;
        }
    }

    *OutEntry = Entry;
    return STATUS_SUCCESS;
}

VOID
NTAPI
DxgkCommitTrackedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    BOOLEAN CompletionAlreadyReached;
    BOOLEAN RetiredNow;
    KIRQL OldIrql;
    ULONG NodeFence;

    if (Adapter == NULL || Entry == NULL || Entry->Adapter != Adapter)
        return;
    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    NodeFence = Adapter->NodeLastCompletedFenceId[Entry->NodeOrdinal];
    CompletionAlreadyReached = NodeFence != 0 && DxgkpFenceIdReached(NodeFence, Entry->SubmissionFenceId);
    if (!DxgkTrackedWorkCoreCommit(&Entry->TrackedWork, CompletionAlreadyReached, &RetiredNow))
    {
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        return;
    }
    if (RetiredNow)
    {
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        Entry->CleanupAsCompleted = TRUE;
#endif
        InsertTailList(&Adapter->SubmitDmaRetireListHead, &Entry->ListEntry);
        KeClearEvent(&Adapter->SubmitDmaRetireDrainedEvent);
    }
    else
    {
        InsertTailList(&Adapter->SubmitDmaListHead, &Entry->ListEntry);
    }
    if (InterlockedExchange(&Entry->ReservationActive, 0) != 0)
        DxgkpReleaseSubmitDmaReservationLocked(Adapter);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
    DxgkRetireCompletedDmaBuffers(Adapter);
}

VOID
NTAPI
DxgkAdoptTrackedDmaBuffer(
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    if (Entry != NULL)
        (VOID)DxgkTrackedWorkCoreClaimExternalCleanup(&Entry->TrackedWork);
}

NTSTATUS
NTAPI
DxgkActivateTrackedDmaBuffer(
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    NTSTATUS Status;

    if (Entry == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Entry->DeviceWork == NULL)
        return STATUS_SUCCESS;
    Status = DxgkDeviceWorkActivate(Entry->DeviceWork);
    if (NT_SUCCESS(Status) && !DxgkTrackedWorkCoreClaimDeviceWork(&Entry->TrackedWork))
        return STATUS_INVALID_DEVICE_STATE;
    return Status;
}

VOID
NTAPI
DxgkCancelTrackedDmaBuffer(
    _In_opt_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    PDXGKRNL_ADAPTER Adapter;
    BOOLEAN Cancelled;
    BOOLEAN FreeDmaBuffer;
    DXGK_TRACKED_WORK_STATE PreviousState;
    KIRQL OldIrql;

    if (Entry == NULL)
        return;

    Adapter = Entry->Adapter;
    if (Adapter != NULL)
    {
        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        PreviousState = DxgkTrackedWorkCoreGetState(&Entry->TrackedWork);
        Cancelled = DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);
        if (Cancelled && PreviousState == DxgkTrackedWorkCommitted)
        {
            RemoveEntryList(&Entry->ListEntry);
            InitializeListHead(&Entry->ListEntry);
        }
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
    }
    else
    {
        PreviousState = DxgkTrackedWorkCoreGetState(&Entry->TrackedWork);
        Cancelled = DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);
    }
    if (!Cancelled)
        return;
    FreeDmaBuffer = PreviousState == DxgkTrackedWorkCommitted;
    if (Adapter != NULL && InterlockedExchange(&Entry->ReservationActive, 0) != 0)
        DxgkpReleaseSubmitDmaReservation(Adapter);
    DxgkpFreeTrackedDmaBufferEntry(Adapter, Entry, FALSE, FreeDmaBuffer, TRUE);
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
/*
 * Terminalize one miniport-accepted DMA buffer without pretending that its
 * fence completed.  The tracked-work cancellation is DPC-safe and owns the
 * exactly-once in-flight/device-work transition.  Allocation references,
 * DMA storage, and fence identity are left for the existing PASSIVE worker.
 */
BOOLEAN
NTAPI
DxgkFailTrackedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    DXGK_TRACKED_WORK_STATE PreviousState;
    BOOLEAN Cancelled;
    BOOLEAN QueueWorker = FALSE;
    LONG ActiveWorkers;
    KIRQL OldIrql;

    if (Adapter == NULL || Entry == NULL || Entry->Adapter != Adapter)
        return FALSE;

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    PreviousState = DxgkTrackedWorkCoreGetState(&Entry->TrackedWork);
    Cancelled = DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);
    if (!Cancelled)
    {
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        return FALSE;
    }

    if (PreviousState == DxgkTrackedWorkCommitted)
    {
        RemoveEntryList(&Entry->ListEntry);
        InitializeListHead(&Entry->ListEntry);
    }
    ASSERT(PreviousState == DxgkTrackedWorkPrepared ||
           PreviousState == DxgkTrackedWorkCommitted);
    ASSERT(DxgkTrackedWorkCoreOwnsExternalCleanup(&Entry->TrackedWork));

    Entry->CleanupAsCompleted = FALSE;
    InsertTailList(&Adapter->SubmitDmaRetireListHead, &Entry->ListEntry);
    if (InterlockedExchange(&Entry->ReservationActive, 0) != 0)
        DxgkpReleaseSubmitDmaReservationLocked(Adapter);
    KeClearEvent(&Adapter->SubmitDmaRetireDrainedEvent);
    if (InterlockedCompareExchange(
            &Adapter->SubmitDmaRetireWorkQueued, 1, 0) == 0)
    {
        ActiveWorkers =
            InterlockedIncrement(&Adapter->SubmitDmaRetireActiveWorkers);
        ASSERT(ActiveWorkers == 1);
        QueueWorker = TRUE;
    }
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    if (QueueWorker)
        ExQueueWorkItem(
            &Adapter->SubmitDmaRetireWorkItem,
            DelayedWorkQueue);
    return TRUE;
}
#endif

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

    Width = Entry->RefreshWidth;
    Height = Entry->RefreshHeight;
    if (Width == 0 || Height == 0 || Width > (MAXULONG / sizeof(ULONG)))
        return;

    DestinationPitch = DxgkpTrackedSamplePitch(DestinationAllocation, Width, Height, 0);
    if (DestinationPitch < Width * sizeof(ULONG) || DestinationAllocation->Size / Height < DestinationPitch)
        return;
    if (NT_SUCCESS(DxgkVidMmMapAllocationCpu(DestinationAllocation, &DestinationVa)))
    {
        DxgkpTraceTrackedSurfaceSample("dst", Entry->RefreshPresentId, Entry->SubmissionFenceId, &Entry->RefreshDstRect, DestinationVa, DestinationPitch, Width, Height);
    }

    if (Entry->SourceAllocationHandle == NULL)
        return;

    SourceAllocation = Entry->SourceAllocation;
    if (SourceAllocation == NULL || SourceAllocation->Adapter != Adapter)
        return;

    Width = Entry->SourceWidth;
    Height = Entry->SourceHeight;
    SourcePitch = Entry->SourcePitch;

    if (Width == 0 || Height == 0 || Width > (MAXULONG / sizeof(ULONG)))
        return;

    SourcePitch = DxgkpTrackedSamplePitch(SourceAllocation, Width, Height, SourcePitch);
    if (SourcePitch < Width * sizeof(ULONG) || SourceAllocation->Size / Height < SourcePitch)
        return;
    if (NT_SUCCESS(DxgkVidMmMapAllocationCpu(SourceAllocation, &SourceVa)))
    {
        DxgkpTraceTrackedSurfaceSample("src", Entry->RefreshPresentId, Entry->SubmissionFenceId, &Entry->RefreshDstRect, SourceVa, SourcePitch, Width, Height);
    }
}

static VOID
DxgkpProgramTrackedScanout(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    PDXGKVMM_ALLOCATION Allocation;
    PDXGKARG_SETVIDPNSOURCEADDRESS SetSourceAddress;
    DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
    LARGE_INTEGER PrimaryAddress;
    HANDLE AllocationHandle;
    BOOLEAN SharedPrimaryRefresh;
    NTSTATUS Status;

    if (Adapter == NULL ||
        Entry == NULL ||
        (!Entry->RefreshSharedPrimaryOnRetire &&
         !Entry->ProgramSourceScanoutOnRetire) ||
        (Entry->RefreshSharedPrimaryOnRetire &&
         Entry->ProgramSourceScanoutOnRetire) ||
        !Entry->SharedSurfaceRundownHeld ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver ||
        DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress) == NULL)
    {
        return;
    }

    SharedPrimaryRefresh =
        Entry->RefreshSharedPrimaryOnRetire;
    if (SharedPrimaryRefresh)
    {
        Allocation = Entry->RefreshAllocation;
        AllocationHandle = Entry->RefreshAllocationHandle;
    }
    else
    {
        Allocation = Entry->SourceAllocation;
        AllocationHandle = Entry->SourceAllocationHandle;
    }

    if (AllocationHandle == NULL)
        return;
    if (Allocation == NULL || Allocation->Adapter != Adapter)
    {
        DXGKRNL_WARN("DxgkpProgramTrackedScanout: invalid allocation %p\n",
                     AllocationHandle);
        return;
    }

    ASSERT(Adapter->SharedSurfaceGeneration == Entry->SharedSurfaceGeneration);

    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpProgramTrackedScanout: aperture map failed "
                     "0x%08lX for %p\n",
                     Status,
                     AllocationHandle);
        goto Cleanup;
    }

    if (SharedPrimaryRefresh)
        DxgkpTraceTrackedRefreshSamples(Adapter, Entry, Allocation);

    PrimaryAddress = DxgkVidMmGetAllocationPrimaryAddress(Allocation);

    SetSourceAddress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*SetSourceAddress), TAG_DXGK_ADAPTER);
    if (SetSourceAddress == NULL)
        goto Cleanup;

    RtlZeroMemory(SetSourceAddress, sizeof(*SetSourceAddress));
    SetSourceAddress->VidPnSourceId = Entry->RefreshVidPnSourceId;
    SetSourceAddress->hAllocation = Allocation->MiniportHandle;
    SetSourceAddress->PrimaryAddress = PrimaryAddress;
    SetSourceAddress->PrimarySegment = Allocation->SegmentId;
    SetSourceAddress->Flags.FlipImmediate = 1;

    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        ExFreePoolWithTag(SetSourceAddress, TAG_DXGK_ADAPTER);
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress)(Adapter->MiniportDeviceContext, SetSourceAddress);
    if (NT_SUCCESS(Status) && DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility) != NULL)
    {
        RtlZeroMemory(&Visibility, sizeof(Visibility));
        Visibility.VidPnSourceId = Entry->RefreshVidPnSourceId;
        Visibility.Visible = TRUE;
        DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility)(Adapter->MiniportDeviceContext, &Visibility);
    }
    DxgkReleaseMiniportCallback(Adapter);
    ExFreePoolWithTag(SetSourceAddress, TAG_DXGK_ADAPTER);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpProgramTrackedScanout: SetVidPnSourceAddress failed 0x%08lX fence=%u alloc=%p seg=%u addr=0x%I64x\n", Status, Entry->SubmissionFenceId, AllocationHandle, Allocation->SegmentId, PrimaryAddress.QuadPart);
        goto Cleanup;
    }

    if (InterlockedIncrement(&DxgkpTrackedRefreshTraceCount) <= 128)
    {
        DXGKRNL_TRACE("DxgkpProgramTrackedScanout: fence=%u "
                      "present=%llu alloc=%p seg=%u addr=0x%I64x src=%u status=0x%08lX\n",
                      Entry->SubmissionFenceId,
                      Entry->RefreshPresentId,
                      AllocationHandle,
                      Allocation->SegmentId,
                      PrimaryAddress.QuadPart,
                      Entry->RefreshVidPnSourceId,
                      Status);
    }

Cleanup:
    return;
}

static VOID
DxgkpFreeTrackedDmaBufferEntry(
    _In_opt_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry,
    _In_ BOOLEAN Completed,
    _In_ BOOLEAN FreeDmaBuffer,
    _In_ BOOLEAN MiniportCallbacksValid)
{
    BOOLEAN DeviceWorkOwned;
    BOOLEAN ExternalCleanupOwned;

    if (Completed)
        DxgkTrackedWorkCoreRetire(&Entry->TrackedWork);
    else
        DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);
    DeviceWorkOwned = DxgkTrackedWorkCoreOwnsDeviceWork(&Entry->TrackedWork);
    ExternalCleanupOwned = DxgkTrackedWorkCoreOwnsExternalCleanup(&Entry->TrackedWork);
    if (Entry->FenceIdentityOwned && Adapter != NULL)
        DxgkReleaseSubmittedFenceIdentity(Adapter, Entry->NodeOrdinal, Entry->SubmissionFenceId);
    if (Completed &&
        MiniportCallbacksValid &&
        (Entry->RefreshSharedPrimaryOnRetire ||
         Entry->ProgramSourceScanoutOnRetire))
    {
        DxgkpProgramTrackedScanout(Adapter, Entry);
    }

    /* GPU completion drives the retained monitored fence even when user mode
     * destroyed its public handle after submission. */
    DxgkSyncObjectReleaseTrackedSignal(Entry->SignalSyncObjectReference, FALSE, Entry->SignalFenceValue);
    Entry->SignalSyncObjectReference = NULL;

    if (Entry->PresentBindingReferenceList != NULL)
    {
        UINT Index;

        for (Index = 0; Index < Entry->PresentBindingReferenceCount; ++Index)
        {
            if (ExternalCleanupOwned)
            {
                NTSTATUS Status = DxgkVidMmDestroyPresentBinding(Entry->Device, Entry->PresentBindingReferenceList[Index]);

                if (!NT_SUCCESS(Status))
                    DXGKRNL_WARN("DxgkpFreeTrackedDmaBufferEntry: present binding close deferred in VidMm 0x%08lX\n", Status);
            }
            else
                DxgkVidMmDereferenceLogicalAllocation(Entry->PresentBindingReferenceList[Index]);
        }
        ExFreePoolWithTag(Entry->PresentBindingReferenceList, TAG_DXGK_SUBMITDMA);
    }
    if (Entry->OpenBindingReferenceList != NULL)
    {
        UINT Index;

        for (Index = 0; Index < Entry->OpenBindingReferenceCount; ++Index)
            DxgkVidMmDereferenceLogicalAllocation(Entry->OpenBindingReferenceList[Index]);
        ExFreePoolWithTag(Entry->OpenBindingReferenceList, TAG_DXGK_SUBMITDMA);
    }
    if (Entry->AllocationReferenceList != NULL)
    {
        UINT Index;

        for (Index = 0; Index < Entry->AllocationReferenceCount; ++Index)
        {
            DxgkVidMmReleaseSubmissionResidencyPin(Entry->AllocationReferenceList[Index]);
            DxgkVidMmDereferenceAllocation(Entry->AllocationReferenceList[Index]);
        }
        ExFreePoolWithTag(Entry->AllocationReferenceList, TAG_DXGK_SUBMITDMA);
    }
    if (Entry->LifetimeAllocationReferenceList != NULL)
    {
        UINT Index;

        for (Index = 0;
             Index < Entry->LifetimeAllocationReferenceCount;
             ++Index)
        {
            DxgkVidMmDereferenceAllocation(
                Entry->LifetimeAllocationReferenceList[Index]);
        }
        ExFreePoolWithTag(Entry->LifetimeAllocationReferenceList,
                          TAG_DXGK_SUBMITDMA);
    }

    if (FreeDmaBuffer)
        DxgkFreeDmaBuffer(Entry->DmaBuffer);
    if (Entry->SourceAllocation != NULL)
        DxgkVidMmDereferenceAllocation(Entry->SourceAllocation);
    if (Entry->RefreshAllocation != NULL)
        DxgkVidMmDereferenceAllocation(Entry->RefreshAllocation);
    if (Entry->SourceOpenBindingReference != NULL)
        DxgkVidMmDereferenceLogicalAllocation(Entry->SourceOpenBindingReference);
    if (Entry->DestinationOpenBindingReference != NULL)
        DxgkVidMmDereferenceLogicalAllocation(Entry->DestinationOpenBindingReference);
    if (Entry->SharedSurfaceRundownHeld && Adapter != NULL)
        ExReleaseRundownProtection(&Adapter->SharedSurfaceRundown);
    if (Entry->Context != NULL)
        DxgkDereferenceContext(Entry->Context);
    if (Entry->Device != NULL && Entry->Device->ProcessRecord != NULL)
        (VOID)DxgkSubmissionAccountingRelease(&Entry->SubmissionAccounting, &Entry->Device->InFlightSubmissions, &Entry->Device->ProcessRecord->InFlightSubmissions);
    if (DeviceWorkOwned)
        DxgkDeviceWorkDestroy(Entry->DeviceWork);
    Entry->DeviceWork = NULL;
    if (Entry->Device != NULL)
        DxgkDereferenceDevice(Entry->Device);
    ExFreePoolWithTag(Entry, TAG_DXGK_SUBMITDMA);
}

static VOID NTAPI
DxgkpRetireSubmittedDmaBuffersWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = Context;
    LIST_ENTRY FreeList;
    KIRQL OldIrql;
    LONG ActiveWorkers;
    ULONG Batch;

    if (Adapter == NULL)
        return;

    for (;;)
    {
        InitializeListHead(&FreeList);
        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        for (Batch = 0; Batch < 64 && !IsListEmpty(&Adapter->SubmitDmaRetireListHead); Batch++)
        {
            PLIST_ENTRY Link = RemoveHeadList(&Adapter->SubmitDmaRetireListHead);

            InsertTailList(&FreeList, Link);
        }
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

        while (!IsListEmpty(&FreeList))
        {
            PDXGKRNL_SUBMIT_DMA_BUFFER Entry = CONTAINING_RECORD(RemoveHeadList(&FreeList), DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);

            DxgkpFreeTrackedDmaBufferEntry(
                Adapter,
                Entry,
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
                Entry->CleanupAsCompleted,
#else
                TRUE,
#endif
                TRUE,
                Adapter->MiniportDeviceContext != NULL &&
                    (Adapter->State == DxgkAdapterStateStarted ||
                     Adapter->State == DxgkAdapterStateStopping));
        }

        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        if (!IsListEmpty(&Adapter->SubmitDmaRetireListHead))
        {
            KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
            continue;
        }
        InterlockedExchange(&Adapter->SubmitDmaRetireWorkQueued, 0);
        ActiveWorkers = InterlockedDecrement(&Adapter->SubmitDmaRetireActiveWorkers);
        ASSERT(ActiveWorkers == 0);
        KeSetEvent(&Adapter->SubmitDmaRetireDrainedEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        return;
    }
}

VOID
NTAPI
DxgkRetireCompletedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY RetireList;
    KIRQL OldIrql;
    LONG ActiveWorkers;
    ULONG CompletedFenceId;
    BOOLEAN PendingRetire;
    BOOLEAN QueueWorker = FALSE;

    if (Adapter == NULL)
        return;

    CompletedFenceId = Adapter->LastCompletedSubmissionFenceId;
    InitializeListHead(&RetireList);

    /*
     * Walk the whole list: independent GPU nodes complete out of global
     * fence order, so an unreached entry no longer implies everything
     * behind it is unreached. Each entry retires only against its node's
     * completed fence; the adapter-global maximum cannot prove completion on
     * any particular node.
     */
    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    {
        PLIST_ENTRY Link = Adapter->SubmitDmaListHead.Flink;

        while (Link != &Adapter->SubmitDmaListHead)
        {
            PDXGKRNL_SUBMIT_DMA_BUFFER Entry = CONTAINING_RECORD(Link, DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
            PLIST_ENTRY Next = Link->Flink;
            ULONG NodeFence = Adapter->NodeLastCompletedFenceId[Entry->NodeOrdinal];

            if (NodeFence != 0 && DxgkpFenceIdReached(NodeFence, Entry->SubmissionFenceId))
            {
                if (DxgkTrackedWorkCoreRetire(&Entry->TrackedWork))
                {
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
                    Entry->CleanupAsCompleted = TRUE;
#endif
                    RemoveEntryList(Link);
                    InsertTailList(&RetireList, Link);
                }
                else
                {
                    ASSERT(FALSE);
                }
            }

            Link = Next;
        }
    }

    while (!IsListEmpty(&RetireList))
    {
        PLIST_ENTRY Link = RemoveHeadList(&RetireList);
        InsertTailList(&Adapter->SubmitDmaRetireListHead, Link);
    }

    if (!IsListEmpty(&Adapter->SubmitDmaRetireListHead) && InterlockedIncrement(&DxgkpRetireTraceCount) <= 128)
    {
        DXGKRNL_TRACE("DxgkRetireCompletedDmaBuffers: completedFence=%u "
                      "queued retire work state=%d\n",
                      CompletedFenceId,
                      Adapter->State);
    }

    PendingRetire = !IsListEmpty(&Adapter->SubmitDmaRetireListHead);
    if (PendingRetire)
        KeClearEvent(&Adapter->SubmitDmaRetireDrainedEvent);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    if (!PendingRetire)
        return;

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    if (!IsListEmpty(&Adapter->SubmitDmaRetireListHead) && InterlockedCompareExchange(&Adapter->SubmitDmaRetireWorkQueued, 1, 0) == 0)
    {
        ActiveWorkers = InterlockedIncrement(&Adapter->SubmitDmaRetireActiveWorkers);
        ASSERT(ActiveWorkers == 1);
        QueueWorker = TRUE;
    }
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    if (QueueWorker)
        ExQueueWorkItem(&Adapter->SubmitDmaRetireWorkItem, DelayedWorkQueue);
}

VOID
NTAPI
DxgkReleaseTrackedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN MiniportCallbacksValid)
{
    LIST_ENTRY CancelList;
    LIST_ENTRY CompletedList;
    KIRQL OldIrql;

    if (Adapter == NULL)
        return;

    if (InterlockedCompareExchange(&Adapter->SubmitDmaRetireActiveWorkers, 0, 0) != 0)
        KeWaitForSingleObject(&Adapter->SubmitDmaRetireDrainedEvent, Executive, KernelMode, FALSE, NULL);

    InitializeListHead(&CancelList);
    InitializeListHead(&CompletedList);

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    while (!IsListEmpty(&Adapter->SubmitDmaListHead))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Adapter->SubmitDmaListHead);
        PDXGKRNL_SUBMIT_DMA_BUFFER Entry = CONTAINING_RECORD(Link, DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
        BOOLEAN Cancelled;

        Cancelled = DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);
        ASSERT(Cancelled);
        InsertTailList(&CancelList, Link);
    }
    while (!IsListEmpty(&Adapter->SubmitDmaRetireListHead))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Adapter->SubmitDmaRetireListHead);
        InsertTailList(&CompletedList, Link);
    }
    if (InterlockedCompareExchange(&Adapter->SubmitDmaRetireActiveWorkers, 0, 0) == 0)
        KeSetEvent(&Adapter->SubmitDmaRetireDrainedEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    while (!IsListEmpty(&CompletedList))
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Entry;

        Entry = CONTAINING_RECORD(RemoveHeadList(&CompletedList), DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
        DxgkpFreeTrackedDmaBufferEntry(
            Adapter,
            Entry,
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            Entry->CleanupAsCompleted,
#else
            TRUE,
#endif
            TRUE,
            MiniportCallbacksValid);
    }
    while (!IsListEmpty(&CancelList))
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Entry;

        Entry = CONTAINING_RECORD(RemoveHeadList(&CancelList), DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
        DxgkpFreeTrackedDmaBufferEntry(Adapter, Entry, FALSE, TRUE, MiniportCallbacksValid);
    }
}

/* ========================================================================
 * Private helpers
 * ====================================================================== */

/*
 * DxgkpEnsureGlobalInitialization
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
NTSTATUS
DxgkpEnsureGlobalInitialization(VOID)
{
    LONG PreviousState;
    NTSTATUS Status;

    PreviousState = InterlockedCompareExchange(&DxgkpInitialized, 1, 0);
    if (PreviousState != 0)
    {
        LARGE_INTEGER Delay;

        Delay.QuadPart = -10 * 1000;
        while ((PreviousState = InterlockedCompareExchange(&DxgkpInitialized, 0, 0)) == 1)
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        ASSERT(PreviousState == 2 || PreviousState == 3);
        return DxgkpInitializationStatus;
    }

    /* Initialize global adapter list and lock. */
    KeInitializeSpinLock(&DxgkAdapterGlobalListLock);
    InitializeListHead(&DxgkAdapterGlobalListHead);
    KeInitializeSpinLock(&g_PostDisplayOwnerLock);
    KeInitializeMutex(&g_PostDisplayOwnershipMutex, 0);
    KeInitializeMutex(&g_MiniportRegistrationMutex, 0);
    ExInitializeFastMutex(&DxgkpMapMemoryMutex);
    InitializeListHead(&DxgkpMapMemoryList);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    ExInitializeFastMutex(&DxgkpCallbackMemoryMutex);
    InitializeListHead(&DxgkpCallbackMemoryList);
#endif

    DxgkpInitializeCoreInterface();

    /* Initialize debug helpers. */
    DxgkDebugInit();

    Status = DxgkpMms2Initialize();
    if (!NT_SUCCESS(Status))
    {
        DxgkpInitializationStatus = Status;
        InterlockedExchange(&DxgkpInitialized, 3);
        DXGKRNL_ERR("DxgkpEnsureGlobalInitialization: dxgmms2 registration failed 0x%08lX\n", Status);
        return Status;
    }

    /* Seed D3DKMT handle cookie. */
    Status = DxgkContextInit();
    if (!NT_SUCCESS(Status))
    {
        NTSTATUS RollbackStatus;

        RollbackStatus = DxgkpMms2Uninitialize();
        if (!NT_SUCCESS(RollbackStatus))
            DXGKRNL_ERR("DxgkpEnsureGlobalInitialization: dxgmms2 rollback failed 0x%08lX\n", RollbackStatus);
        DxgkpInitializationStatus = Status;
        InterlockedExchange(&DxgkpInitialized, 3);
        DXGKRNL_ERR("DxgkpEnsureGlobalInitialization: DxgkContextInit failed 0x%08lX\n", Status);
        return Status;
    }

    /* Bring the panic screen up through the display owner (see above). */
    DxgkpRegisterBugCheckCallback();

    DxgkpInitializationStatus = STATUS_SUCCESS;
    InterlockedExchange(&DxgkpInitialized, 2);
    DXGKRNL_TRACE("DxgkpEnsureGlobalInitialization: one-time init complete\n");
    return STATUS_SUCCESS;
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

static BOOLEAN
DxgkpAcquireVidSchCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (InterlockedCompareExchange(&Adapter->VidSchStopping, 0, 0) != 0)
        return FALSE;
    InterlockedIncrement(&Adapter->VidSchActiveCalls);
    if (InterlockedCompareExchange(&Adapter->VidSchStopping, 0, 0) != 0)
    {
        InterlockedDecrement(&Adapter->VidSchActiveCalls);
        return FALSE;
    }
    return TRUE;
}

static VOID
DxgkpReleaseVidSchCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG ActiveCalls = InterlockedDecrement(&Adapter->VidSchActiveCalls);

    ASSERT(ActiveCalls >= 0);
}

static VOID
DxgkpWaitForVidSchCallbacks(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Delay;

    Delay.QuadPart = -10000;
    while (InterlockedCompareExchange(&Adapter->VidSchActiveCalls, 0, 0) != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

VOID
NTAPI
DxgkDrainVidSchCallbacks(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter != NULL)
        DxgkpWaitForVidSchCallbacks(Adapter);
}

static VOID
DxgkpDisablePeriodicInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter);

static VOID
DxgkpDisconnectAdapterInterrupt(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    DxgkBlockInterruptCallbacks(Adapter);
    DxgkpDisablePeriodicInterruptHandoff(Adapter);
    if (Adapter->InterruptMessageTable != NULL)
    {
        IO_DISCONNECT_INTERRUPT_PARAMETERS DisconnectParams;

        RtlZeroMemory(&DisconnectParams, sizeof(DisconnectParams));
        DisconnectParams.Version = CONNECT_MESSAGE_BASED;
        DisconnectParams.ConnectionContext.InterruptMessageTable = Adapter->InterruptMessageTable;
        IoDisconnectInterruptEx(&DisconnectParams);
        Adapter->InterruptMessageTable = NULL;
        Adapter->InterruptObject = NULL;
    }
    else if (Adapter->InterruptObject != NULL)
    {
        IoDisconnectInterrupt(Adapter->InterruptObject);
        Adapter->InterruptObject = NULL;
    }
}

/*
 * InterruptLock is also taken by DxgkCbNotifyInterrupt at the device's
 * synchronize IRQL.  Non-ISR users must raise to at least that IRQL before
 * acquiring it; KeAcquireSpinLock would raise only to DISPATCH_LEVEL.
 */
static KIRQL
DxgkpAcquireAdapterInterruptLock(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL CurrentIrql;
    KIRQL LockIrql;
    KIRQL OldIrql;

    CurrentIrql = KeGetCurrentIrql();
    LockIrql = Adapter->InterruptLevel;
    if (LockIrql < DISPATCH_LEVEL)
        LockIrql = DISPATCH_LEVEL;

    OldIrql = CurrentIrql;
    if (CurrentIrql < LockIrql)
        KeRaiseIrql(LockIrql, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&Adapter->InterruptLock);
    return OldIrql;
}

static VOID
DxgkpReleaseAdapterInterruptLock(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ KIRQL OldIrql)
{
    KIRQL LockIrql = KeGetCurrentIrql();

    KeReleaseSpinLockFromDpcLevel(&Adapter->InterruptLock);
    if (LockIrql != OldIrql)
        KeLowerIrql(OldIrql);
}

static BOOLEAN
DxgkpPeriodicInterruptHandoffSupported(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    return Adapter != NULL &&
           REACTOS_WDDM_TARGET_LEVEL >=
               DXGK_CAPS_CORE_LEVEL_WDDM_2_2 &&
           Adapter->MiniportContext != NULL &&
           !Adapter->MiniportContext->UseDodLayout &&
           DxgkCapsCoreInterfaceVersionAtLeast(
               Adapter->MiniportContext->InitData.s.Version,
               DXGK_CAPS_CORE_LEVEL_WDDM_2_2) &&
           DXGK_CB_FULL(
               Adapter,
               DxgkDdiCreatePeriodicFrameNotification) != NULL &&
           DXGK_CB_FULL(
               Adapter,
               DxgkDdiDestroyPeriodicFrameNotification) != NULL;
#else
    UNREFERENCED_PARAMETER(Adapter);
    return FALSE;
#endif
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
static BOOLEAN
DxgkpMonitoredFenceInterruptSupported(
    _In_opt_ PDXGKRNL_ADAPTER Adapter)
{
    return Adapter != NULL &&
           Adapter->MiniportContext != NULL &&
           !Adapter->MiniportContext->UseDodLayout &&
           DxgkMonitoredInterruptCoreSupported(
               REACTOS_WDDM_TARGET_LEVEL,
               DxgkCapsCoreInterfaceVersionToLevel(
                   Adapter->MiniportContext->InitData.s.Version),
               Adapter->NodeCount);
}

static BOOLEAN
DxgkpQueueMonitoredFenceEvaluation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG EngineOrdinal)
{
    if (!DxgkpMonitoredFenceInterruptSupported(Adapter) ||
        NodeOrdinal >= Adapter->NodeCount ||
        EngineOrdinal != 0)
    {
        return FALSE;
    }

    /*
     * Publish the engine's fence write before making its node visible to
     * the adapter DPC.  The DPC pairs this with the acquire barrier in the
     * monitored-fence registry evaluator.
     */
    KeMemoryBarrier();
    return NT_SUCCESS(
        DxgkMonitoredInterruptCoreEnqueue(
            &Adapter->MonitoredFencePendingNodes,
            Adapter->NodeCount,
            NodeOrdinal,
            EngineOrdinal));
}

static VOID
DxgkpDrainMonitoredFenceInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG PendingNodes;
    ULONG NodeOrdinal;

    PendingNodes =
        DxgkMonitoredInterruptCoreDrain(
            &Adapter->MonitoredFencePendingNodes);
    for (NodeOrdinal = 0; NodeOrdinal < 32; ++NodeOrdinal)
    {
        NTSTATUS Status;

        if ((PendingNodes & (1UL << NodeOrdinal)) == 0)
            continue;
        Status =
            DxgkSyncNotifyMonitoredFence(
                Adapter,
                NodeOrdinal,
                0);
        if (!NT_SUCCESS(Status) &&
            Status != STATUS_NOT_FOUND &&
            Status != STATUS_DELETE_PENDING)
        {
            DXGKRNL_WARN(
                "DXGKRNL: monitored-fence interrupt for node %lu "
                "was rejected (0x%08lX)\n",
                NodeOrdinal,
                Status);
        }
    }
}
#endif

static VOID
DxgkpEnablePeriodicInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    OldIrql = DxgkpAcquireAdapterInterruptLock(Adapter);
    if (DxgkpPeriodicInterruptHandoffSupported(Adapter))
    {
        DxgkPeriodicInterruptCoreEnableLocked(
            &Adapter->PeriodicInterruptCore);
    }
    else
    {
        DxgkPeriodicInterruptCoreDisableLocked(
            &Adapter->PeriodicInterruptCore);
    }
    Adapter->PeriodicInterruptOverflowReported = FALSE;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    InterlockedExchange(
        &Adapter->MonitoredFencePendingNodes,
        0);
#endif
    DxgkpReleaseAdapterInterruptLock(Adapter, OldIrql);
}

static VOID
DxgkpDisablePeriodicInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    OldIrql = DxgkpAcquireAdapterInterruptLock(Adapter);
    DxgkPeriodicInterruptCoreDisableLocked(
        &Adapter->PeriodicInterruptCore);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    InterlockedExchange(
        &Adapter->MonitoredFencePendingNodes,
        0);
#endif
    DxgkpReleaseAdapterInterruptLock(Adapter, OldIrql);
}

/*
 * Drain the fixed ISR handoff without carrying InterruptLock into the sync
 * registry.  The empty transition and DpcActive update happen under the same
 * lock as enqueue, so a later ISR either joins this drain or queues a new one.
 */
static VOID
DxgkpDrainPeriodicInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    for (;;)
    {
        DXGK_PERIODIC_INTERRUPT_CORE_ENTRY Entry;
        ULONGLONG OverflowCount = 0;
        BOOLEAN HaveEntry;
        BOOLEAN ReportOverflow = FALSE;
        KIRQL OldIrql;
        NTSTATUS Status;

        OldIrql = DxgkpAcquireAdapterInterruptLock(Adapter);
        HaveEntry = DxgkPeriodicInterruptCoreDequeueLocked(
            &Adapter->PeriodicInterruptCore,
            &Entry);
        if (!HaveEntry &&
            Adapter->PeriodicInterruptCore.State ==
                DxgkPeriodicInterruptOverflowed &&
            !Adapter->PeriodicInterruptOverflowReported)
        {
            Adapter->PeriodicInterruptOverflowReported = TRUE;
            OverflowCount =
                Adapter->PeriodicInterruptCore.OverflowCount;
            ReportOverflow = TRUE;
        }
        DxgkpReleaseAdapterInterruptLock(Adapter, OldIrql);

        if (ReportOverflow)
        {
            DXGKRNL_ERR(
                "DXGKRNL: periodic interrupt handoff disabled after "
                "%I64u protocol/overflow failure(s)\n",
                OverflowCount);
        }
        if (!HaveEntry)
            break;

        Status = DxgkSyncNotifyPeriodicFenceCount(
            Adapter,
            Entry.VidPnTargetId,
            Entry.NotificationId,
            Entry.PendingCount);
        if (!NT_SUCCESS(Status) &&
            Status != STATUS_NOT_FOUND &&
            Status != STATUS_DELETE_PENDING)
        {
            DXGKRNL_WARN(
                "DXGKRNL: periodic notification %lu target %lu "
                "count %I64u was rejected (0x%08lX)\n",
                Entry.NotificationId,
                Entry.VidPnTargetId,
                Entry.PendingCount,
                Status);
        }
    }
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
    if (!DxgkpAcquireVidSchCallback(Adapter))
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

    if (Adapter->MiniportContext->InitData.s.DxgkDdiDpcRoutine != NULL && DxgkAcquireInterruptCallback(Adapter))
    {
        Start100ns = DxgkpTraceNow100ns();
        Adapter->MiniportContext->InitData.s.DxgkDdiDpcRoutine(Adapter->MiniportDeviceContext);
        ElapsedUs = DxgkpTraceElapsedUs(Start100ns);
        DxgkReleaseInterruptCallback(Adapter);

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

    DxgkpDrainPeriodicInterruptHandoff(Adapter);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    DxgkpDrainMonitoredFenceInterruptHandoff(Adapter);
#endif

    /* A target bit maps one-to-one to the implemented source ordinal. */
    {
        ULONG VsyncMask = (ULONG)InterlockedExchange(&Adapter->VsyncPending, 0);
        ULONG SourceId;

        for (SourceId = 0; SourceId < 32; SourceId++)
        {
            if ((VsyncMask & (1UL << SourceId)) != 0)
                DxgkpNotifyVSync(Adapter, (D3DDDI_VIDEO_PRESENT_SOURCE_ID)SourceId);
        }
        if (VsyncMask != 0)
            DxgkDisplayVsyncFlush(Adapter);
    }

    DxgkRetireCompletedDmaBuffers(Adapter);
    DxgkpReleaseVidSchCallback(Adapter);
}

/* Forward declarations for callbacks defined later in this file */
static PDXGKRNL_ADAPTER
DxgkpHandleToAdapter(
    _In_ HANDLE DeviceHandle);

NTSTATUS APIENTRY DxgkCbQueryServices(HANDLE, DXGK_SERVICES, PINTERFACE);
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
 * Deprecated in WDDM 1.0.  The callback reports presence through its output
 * parameter; the return value reports whether the query itself was accepted.
 */
static NTSTATUS
APIENTRY
DxgkCbIsDevicePresent(
    _In_ HANDLE DeviceHandle,
    _In_ PPCI_DEVICE_PRESENCE_PARAMETERS DevicePresenceParameters,
    _Out_ PBOOLEAN DevicePresent)
{
    PDXGKRNL_ADAPTER Adapter;

    if (DevicePresenceParameters == NULL || DevicePresent == NULL ||
        DevicePresenceParameters->Size < sizeof(*DevicePresenceParameters))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *DevicePresent = FALSE;
    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    *DevicePresent =
        Adapter->PhysicalDeviceObject != NULL &&
        Adapter->State != DxgkAdapterStateRemoved &&
        InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) == 0;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

static PVOID
APIENTRY
DxgkCbGetHandleData(
    _In_ CONST DXGKARGCB_GETHANDLEDATA *HandleData)
{
    if (HandleData == NULL || HandleData->Flags.Reserved != 0)
        return NULL;

    return DxgkVidMmGetHandleData(HandleData->Type, HandleData->hObject, HandleData->Flags.DeviceSpecific);
}

static PVOID
APIENTRY
DxgkCbAcquireHandleData(
    _In_ CONST DXGKARGCB_GETHANDLEDATA *HandleData,
    _Out_ PDXGKARG_RELEASE_HANDLE ReleaseHandle)
{
    if (ReleaseHandle == NULL)
        return NULL;
    *ReleaseHandle = NULL;
    if (HandleData == NULL || HandleData->Flags.Reserved != 0)
        return NULL;

    return DxgkVidMmAcquireHandleData(HandleData->Type, HandleData->hObject, HandleData->Flags.DeviceSpecific, ReleaseHandle);
}

static VOID
APIENTRY
DxgkCbReleaseHandleData(
    _In_ CONST DXGKARGCB_RELEASEHANDLEDATA HandleData)
{
    if (HandleData.ReleaseHandle != NULL)
        DxgkVidMmReleaseHandleData(HandleData.Type, HandleData.ReleaseHandle);
}

/*
 * DxgkCbGetHandleParent — offset 0x70
 */
static D3DKMT_HANDLE
APIENTRY
DxgkCbGetHandleParent(
    IN_D3DKMT_HANDLE hAllocation)
{
    return DxgkVidMmGetHandleParent(hAllocation);
}

/*
 * DxgkCbEnumHandleChildren — offset 0x78
 */
static D3DKMT_HANDLE
APIENTRY
DxgkCbEnumHandleChildren(
    IN_CONST_PDXGKARGCB_ENUMHANDLECHILDREN EnumHandleChildren)
{
    if (EnumHandleChildren == NULL)
        return 0;

    return DxgkVidMmEnumHandleChildren(
        EnumHandleChildren->hObject,
        EnumHandleChildren->Index);
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
    INOUT_PDXGKARGCB_GETCAPTUREADDRESS GetCaptureAddress)
{
    return DxgkVidMmGetCaptureAddress(GetCaptureAddress);
}

/*
 * DxgkCbLogEtwEventStub — offset 0xa8
 * ETW event logging for GPU diagnostics.
 */
static VOID
APIENTRY
DxgkCbLogEtwEventStub(
    _In_ CONST LPCGUID EventGuid,
    _In_ UCHAR Type,
    _In_ USHORT EventBufferSize,
    _In_reads_bytes_(EventBufferSize) PVOID EventBuffer)
{
    UNREFERENCED_PARAMETER(EventGuid);
    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(EventBufferSize);
    UNREFERENCED_PARAMETER(EventBuffer);
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
    _In_ DXGKDDI_PROTECTED_CALLBACK DxgkProtectedCallback,
    _In_ PVOID ProtectedCallbackContext)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(DxgkProtectedCallback);
    UNREFERENCED_PARAMETER(ProtectedCallbackContext);
    DXGKRNL_TRACE("DxgkCbExcludeAdapterAccess: called (stub returning NOT_SUPPORTED)\n");
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbCreateContextAllocationStub — offset 0xb8 (Win8)
 */
static NTSTATUS
APIENTRY
DxgkCbCreateContextAllocationStub(
    INOUT_PDXGKARGCB_CREATECONTEXTALLOCATION ContextAllocation)
{
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
static VOID
APIENTRY
DxgkCbSetPowerComponentActiveStub(
    _In_ HANDLE DeviceHandle,
    _In_ UINT   Component)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Component);
    DXGKRNL_TRACE("DxgkCbSetPowerComponentActive: called\n");
}

/*
 * DxgkCbSetPowerComponentIdleStub — offset 0xd0 (Win8)
 */
static VOID
APIENTRY
DxgkCbSetPowerComponentIdleStub(
    _In_ HANDLE DeviceHandle,
    _In_ UINT   Component)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Component);
    DXGKRNL_TRACE("DxgkCbSetPowerComponentIdle: called\n");
}

/*
 * DxgkCbPowerRuntimeControlRequestStub — offset 0xe0 (Win8)
 */
static NTSTATUS
APIENTRY
DxgkCbPowerRuntimeControlRequestStub(
    _In_ HANDLE DeviceHandle,
    _In_ LPCGUID PowerControlCode,
    _In_opt_ PVOID InBuffer,
    _In_ SIZE_T InBufferSize,
    _Out_opt_ PVOID OutBuffer,
    _In_ SIZE_T OutBufferSize,
    _Out_opt_ PSIZE_T BytesReturned)
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
    _In_reads_bytes_(AcpiInputSize)
          PACPI_EVAL_INPUT_BUFFER_COMPLEX AcpiInputBuffer,
    _In_  ULONG  AcpiInputSize,
    _Out_writes_bytes_(AcpiOutputSize)
          PACPI_EVAL_OUTPUT_BUFFER AcpiOutputBuffer,
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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
static BOOLEAN
DxgkpKmdCpuEventFeatureAvailable(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
#if (REACTOS_WDDM_TARGET_LEVEL >= 3000) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
    if (Adapter == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->UseDodLayout ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_3_0))
    {
        return FALSE;
    }

    return DXGK_CB_FULL(Adapter, DxgkDdiCreateCpuEvent) != NULL &&
           DXGK_CB_FULL(Adapter, DxgkDdiDestroyCpuEvent) != NULL;
#else
    UNREFERENCED_PARAMETER(Adapter);
    return FALSE;
#endif
}

static NTSTATUS
APIENTRY
DxgkCbIsFeatureEnabled(
    INOUT_PDXGKARGCB_ISFEATUREENABLED Args)
{
    PDXGKRNL_ADAPTER Adapter;

    if (Args == NULL)
        return STATUS_INVALID_PARAMETER;
    Args->Enabled = FALSE;

    Adapter = DxgkpHandleToAdapter(Args->DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (Args->FeatureId == DXGK_FEATURE_KMD_SIGNAL_CPU_EVENT)
        Args->Enabled = DxgkpKmdCpuEventFeatureAvailable(Adapter);

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
static NTSTATUS
APIENTRY
DxgkCbQueryFeatureSupport(
    INOUT_PDXGKARGCB_QUERYFEATURESUPPORT Args)
{
    PDXGKRNL_ADAPTER Adapter;

    if (Args == NULL ||
        Args->DriverSupportState > DXGK_FEATURE_SUPPORT_ALWAYS_ON)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Args->Enabled = FALSE;

    Adapter = DxgkpHandleToAdapter(Args->DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (Args->FeatureId == DXGK_FEATURE_KMD_SIGNAL_CPU_EVENT &&
        Args->DriverSupportState >= DXGK_FEATURE_SUPPORT_STABLE)
    {
        Args->Enabled = DxgkpKmdCpuEventFeatureAvailable(Adapter);
    }

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}
#endif

/*
 * Native dxgkrnl does not advertise sizeof(DXGKRNL_INTERFACE) blindly.  Older
 * selectors are normalized to the newest compatible revision and receive the
 * exact prefix ending at that revision's last callback.  Starting with WDDM
 * 2.8, native publishes its whole current callback buffer and leaves callbacks
 * it does not implement NULL.
 *
 * Keep the sizes tied to WDK field ends rather than pointer-size literals.
 * These assertions also prove the x86 sizes independently of the native
 * amd64/arm64 branch constants.
 */
#ifdef _WIN64
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbExcludeAdapterAccess) == 0xB8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompleteFStateTransition) == 0x100);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompletePStateTransition) == 0x108);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbHardwareContentProtectionTeardown) == 0x138);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbMitigatedRangeUpdate) == 0x148);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership2) == 0x168);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSetProtectedSessionStatus) == 0x170);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbReportDiagnostic) == 0x1C8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSignalEvent) == 0x1D0);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSaveMemoryForHotUpdate) == 0x1E0);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbDisconnectDoorbell) == 0x240);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x240);
#else
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbExcludeAdapterAccess) == 0x60);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompleteFStateTransition) == 0x84);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompletePStateTransition) == 0x88);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbHardwareContentProtectionTeardown) == 0xA0);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbMitigatedRangeUpdate) == 0xA8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership2) == 0xB8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSetProtectedSessionStatus) == 0xBC);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbReportDiagnostic) == 0xE8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSignalEvent) == 0xEC);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSaveMemoryForHotUpdate) == 0xF4);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbDisconnectDoorbell) == 0x124);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x124);
#endif

static VOID
DxgkpSelectInterfaceAdvertisement(
    _In_ ULONG RequestedVersion,
    _Out_ PULONG AdvertisedSize,
    _Out_ PULONG AdvertisedVersion)
{
    if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WIN7)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbExcludeAdapterAccess);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WIN7;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WIN8)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompleteFStateTransition);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WIN8;
    }
    else if (RequestedVersion <=
             DXGKDDI_INTERFACE_VERSION_WDDM1_3_PATH_INDEPENDENT_ROTATION)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompletePStateTransition);
        *AdvertisedVersion =
            DXGKDDI_INTERFACE_VERSION_WDDM1_3_PATH_INDEPENDENT_ROTATION;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    {
        *AdvertisedSize = DXGKP_FIELD_END(
            DXGKRNL_INTERFACE,
            DxgkCbHardwareContentProtectionTeardown);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_0;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbMitigatedRangeUpdate);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_1;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    {
        *AdvertisedSize = DXGKP_FIELD_END(
            DXGKRNL_INTERFACE,
            DxgkCbAcquirePostDisplayOwnership2);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_2;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    {
        *AdvertisedSize = DXGKP_FIELD_END(
            DXGKRNL_INTERFACE,
            DxgkCbSetProtectedSessionStatus);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_3;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbReportDiagnostic);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_4;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSignalEvent);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_5;
    }
    else if (RequestedVersion < DXGKDDI_INTERFACE_VERSION_WDDM2_8)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSaveMemoryForHotUpdate);
        *AdvertisedVersion = RequestedVersion;
    }
    else
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbDisconnectDoorbell);
        *AdvertisedVersion = RequestedVersion;
    }
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
    ULONG RequestedVersion;

    RequestedVersion = Adapter->MiniportContext->InitData.s.Version;
    RtlZeroMemory(Interface, sizeof(*Interface));

    DxgkpSelectInterfaceAdvertisement(
        RequestedVersion,
        &Interface->Size,
        &Interface->Version);
    Interface->DeviceHandle = (HANDLE)Adapter;

    DXGKRNL_TRACE("DxgkpFillInterface: DeviceHandle=%p Size=%u "
                  "RequestedVersion=0x%lX AdvertisedVersion=0x%lX\n",
                  Interface->DeviceHandle,
                  Interface->Size,
                  RequestedVersion,
                  Interface->Version);

    /* WDDM 1.0 (Vista) baseline callbacks — correct WDK field order */
    Interface->DxgkCbEvalAcpiMethod                = DxgkCbEvalAcpiMethodStub; /* 0x10 */
    Interface->DxgkCbGetDeviceInformation          = DxgkCbGetDeviceInformation;  /* 0x18 */
    Interface->DxgkCbIndicateChildStatus           = DxgkCbIndicateChildStatus;   /* 0x20 */
    Interface->DxgkCbMapMemory                     = DxgkCbMapMemory;             /* 0x28 */
    Interface->DxgkCbQueueDpc                      = DxgkCbQueueDpc;             /* 0x30 */
    Interface->DxgkCbQueryServices                 = DxgkCbQueryServices;         /* 0x38 */
    Interface->DxgkCbReadDeviceSpace               = DxgkCbReadDeviceSpace;       /* 0x40 */
    Interface->DxgkCbSynchronizeExecution          = DxgkCbSynchronizeExecution;  /* 0x48 */
    Interface->DxgkCbUnmapMemory                   = DxgkCbUnmapMemory;              /* 0x50 */
    Interface->DxgkCbWriteDeviceSpace              = DxgkCbWriteDeviceSpace;      /* 0x58 */
    Interface->DxgkCbIsDevicePresent               = DxgkCbIsDevicePresent;   /* 0x60 */
    Interface->DxgkCbGetHandleData                 = DxgkCbGetHandleData;          /* 0x68 */
    Interface->DxgkCbGetHandleParent               = DxgkCbGetHandleParent;   /* 0x70 */
    Interface->DxgkCbEnumHandleChildren            = DxgkCbEnumHandleChildren; /* 0x78 */
    Interface->DxgkCbNotifyInterrupt               = DxgkCbNotifyInterrupt;       /* 0x80 */
    Interface->DxgkCbNotifyDpc                     = DxgkCbNotifyDpc;             /* 0x88 */
    Interface->DxgkCbQueryVidPnInterface           = DxgkCbQueryVidPnInterface; /* 0x90 */
    Interface->DxgkCbQueryMonitorInterface         = DxgkCbQueryMonitorInterface; /* 0x98 */
    Interface->DxgkCbGetCaptureAddress             = DxgkCbGetCaptureAddressStub; /* 0xa0 */

    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_1_1))
    {
        Interface->DxgkCbLogEtwEvent          = DxgkCbLogEtwEventStub; /* 0xa8 */
        Interface->DxgkCbExcludeAdapterAccess = DxgkCbExcludeAdapterAccessStub; /* 0xb0 */
    }

    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_1_2))
    {
        Interface->DxgkCbCreateContextAllocation =
            DxgkCbCreateContextAllocationStub; /* 0xb8 */
        Interface->DxgkCbDestroyContextAllocation =
            DxgkCbDestroyContextAllocationStub; /* 0xc0 */
        Interface->DxgkCbSetPowerComponentActive =
            DxgkCbSetPowerComponentActiveStub; /* 0xc8 */
        Interface->DxgkCbSetPowerComponentIdle =
            DxgkCbSetPowerComponentIdleStub; /* 0xd0 */
        Interface->DxgkCbAcquirePostDisplayOwnership =
            DxgkCbAcquirePostDisplayOwnership; /* 0xd8 */
        Interface->DxgkCbPowerRuntimeControlRequest =
            DxgkCbPowerRuntimeControlRequestStub; /* 0xe0 */
    }

    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
    {
        Interface->DxgkCbAcquireHandleData = DxgkCbAcquireHandleData; /* 0x120 */
        Interface->DxgkCbReleaseHandleData = DxgkCbReleaseHandleData; /* 0x128 */
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_4))
    {
        Interface->DxgkCbAllocateContiguousMemory =
            DxgkCbAllocateContiguousMemory; /* 0x170 */
        Interface->DxgkCbFreeContiguousMemory =
            DxgkCbFreeContiguousMemory; /* 0x178 */
        Interface->DxgkCbAllocatePagesForMdl =
            DxgkCbAllocatePagesForMdl; /* 0x180 */
        Interface->DxgkCbFreePagesFromMdl =
            DxgkCbFreePagesFromMdl; /* 0x188 */

        /*
         * Leave the framebuffer-save, MDL-to-IOMMU, and diagnostic callbacks
         * NULL.  The memory callbacks require per-physical-adapter
         * section/commit accounting or a graphics IOMMU domain, and diagnostic
         * reporting requires an ingestion sink.  None exists in this dxgkrnl.
         */
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 3000) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_5))
    {
        Interface->DxgkCbSignalEvent = DxgkCbSignalEvent;
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2600) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_6))
    {
        Interface->DxgkCbIsFeatureEnabled = DxgkCbIsFeatureEnabled;
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2900) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_9))
    {
        Interface->DxgkCbQueryFeatureSupport = DxgkCbQueryFeatureSupport;
    }
#endif

    /*
     * Keep the WDDM 2.9 physical-memory-object callbacks NULL until dxgkrnl
     * owns the corresponding object lifecycle.  The legacy raw-physical
     * mapping helpers below have a different ABI and must not be published
     * through these typed slots.
     */

}

/*
 * DxgkpHandleToAdapter
 *
 * Validate and dereference a DeviceHandle as a DXGKRNL_ADAPTER pointer.
 * All DxgkCb* callbacks call this helper to convert the opaque handle
 * supplied by the miniport back to the adapter object.
 *
 * Returns the adapter pointer with ReverseCallbackRundownRef held, or NULL if the
 * handle is invalid or final reverse-callback teardown has begun.  The caller must release
 * ReverseCallbackRundownRef after its last adapter access.
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
            if (ExAcquireRundownProtection(&Candidate->ReverseCallbackRundownRef))
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
VOID
APIENTRY
DxgkCbNotifyInterrupt(
    _In_ HANDLE DeviceHandle,
    IN_CONST_PDXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyInterruptData)
{
    PDXGKRNL_ADAPTER Adapter;

    /*
     * DeviceHandle is set to Adapter at DxgkpFillInterface time; it is
     * valid as long as the adapter is started.  We do not walk the global
     * list here (cannot acquire a spinlock while holding another at DIRQL)
     * — cast directly.
     */
    Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;
    if (Adapter == NULL)
        return;
    if (NotifyInterruptData == NULL)
        return;
    if (!DxgkpAcquireVidSchCallback(Adapter))
        return;

    /* Log interrupt type for diagnostics */
    if (NotifyInterruptData)
    {
        static LONG NotifyCount = 0;
        LONG c = InterlockedIncrement(&NotifyCount);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
        if (NotifyInterruptData->InterruptType ==
            DXGK_INTERRUPT_MONITORED_FENCE_SIGNALED)
        {
            (VOID)DxgkpQueueMonitoredFenceEvaluation(
                Adapter,
                NotifyInterruptData->MonitoredFenceSignaled
                    .NodeOrdinal,
                NotifyInterruptData->MonitoredFenceSignaled
                    .EngineOrdinal);
        }
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
        if (NotifyInterruptData->InterruptType ==
            DXGK_INTERRUPT_PERIODIC_MONITORED_FENCE_SIGNALED)
        {
            BOOLEAN QueuePeriodicDpc;
            KIRQL OldIrql;

            if (DxgkpPeriodicInterruptHandoffSupported(Adapter))
            {
                OldIrql =
                    DxgkpAcquireAdapterInterruptLock(Adapter);
                (VOID)DxgkPeriodicInterruptCoreEnqueueLocked(
                    &Adapter->PeriodicInterruptCore,
                    NotifyInterruptData->PeriodicMonitoredFenceSignaled
                        .VidPnTargetId,
                    NotifyInterruptData->PeriodicMonitoredFenceSignaled
                        .NotificationID,
                    1,
                    &QueuePeriodicDpc);
                DxgkpReleaseAdapterInterruptLock(Adapter, OldIrql);
            }
        }
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
        if (NotifyInterruptData->InterruptType ==
                DXGK_INTERRUPT_NATIVE_FENCE_SIGNALED &&
            Adapter->MiniportContext != NULL &&
            DxgkCapsCoreInterfaceVersionAtLeast(
                Adapter->MiniportContext->InitData.s.Version,
                DXGK_CAPS_CORE_LEVEL_WDDM_3_2) &&
            NotifyInterruptData->Flags
                .EvaluateLegacyMonitoredFences)
        {
            (VOID)DxgkpQueueMonitoredFenceEvaluation(
                Adapter,
                NotifyInterruptData->NativeFenceSignaled
                    .NodeOrdinal,
                NotifyInterruptData->NativeFenceSignaled
                    .EngineOrdinal);
        }
#endif
        if (NotifyInterruptData->InterruptType == DXGK_INTERRUPT_CRTC_VSYNC)
        {
            ULONG TargetId = NotifyInterruptData->CrtcVsync.VidPnTargetId;

            if (TargetId < Adapter->PresentQueueCount && TargetId < 32)
                InterlockedOr(&Adapter->VsyncPending, (LONG)(1UL << TargetId));
        }
        if (c <= 10)
        {
            /* Can't use DPRINT1 at DIRQL safely — just count */
        }

        /* Forward DMA completion/preemption to the VidSch engine state machine. */
        VidSchNotifyInterrupt(Adapter, NotifyInterruptData);
    }
    /* Queue the DPC; the DPC routine will call DxgkDdiDpcRoutine. */
    {
        static LONG NotifyCount = 0;
        InterlockedIncrement(&NotifyCount);
    }
    KeInsertQueueDpc(&Adapter->DpcObject, NULL, NULL);

    DxgkpReleaseVidSchCallback(Adapter);
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
    if (!DxgkpAcquireVidSchCallback(Adapter))
        return;

    KeSetEvent(&Adapter->SyncEvent, IO_NO_INCREMENT, FALSE);
    DxgkpReleaseVidSchCallback(Adapter);
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
    PPHYSICAL_MEMORY_RANGE MemoryRanges;
    PPHYSICAL_MEMORY_RANGE Range;
    ULONGLONG        TotalStart100ns;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    DXGKRNL_TRACE("DxgkCbGetDeviceInformation: handle=%p DeviceInfo=%p\n",
                  DeviceHandle, DeviceInformation);

    if (DeviceInformation == NULL)
        return STATUS_INVALID_PARAMETER;

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

    DeviceInformation->MiniportDeviceContext = Adapter->MiniportDeviceContext;
    DeviceInformation->PhysicalDeviceObject = Adapter->PhysicalDeviceObject;
    DeviceInformation->DeviceRegistryPath = Adapter->MiniportContext->RegistryPath;
    DeviceInformation->TranslatedResourceList = Adapter->TranslatedResources;
    DeviceInformation->SystemMemorySize.QuadPart =
        (LONGLONG)SharedUserData->NumberOfPhysicalPages << PAGE_SHIFT;
    DeviceInformation->DockingState = DockStateUnsupported;

    /*
     * HighestPhysicalAddress is the highest byte in any installed physical
     * run, not merely (installed-page-count - 1).  Machines with firmware
     * holes make those values observably different.
     */
    MemoryRanges = MmGetPhysicalMemoryRanges();
    if (MemoryRanges != NULL)
    {
        for (Range = MemoryRanges;
             Range->NumberOfBytes.QuadPart != 0;
             ++Range)
        {
            ULONGLONG EndAddress;

            EndAddress = (ULONGLONG)Range->BaseAddress.QuadPart +
                         (ULONGLONG)Range->NumberOfBytes.QuadPart - 1;
            if (EndAddress >
                (ULONGLONG)DeviceInformation->HighestPhysicalAddress.QuadPart)
            {
                DeviceInformation->HighestPhysicalAddress.QuadPart =
                    (LONGLONG)EndAddress;
            }
        }
        ExFreePool(MemoryRanges);
    }
    else if (DeviceInformation->SystemMemorySize.QuadPart != 0)
    {
        DeviceInformation->HighestPhysicalAddress.QuadPart =
            DeviceInformation->SystemMemorySize.QuadPart - 1;
    }

    DXGKRNL_TRACE("DxgkCbGetDeviceInformation: PDO %p SysMem=%I64u "
                  "HighestPA=0x%I64X TransRes=%p\n",
                  Adapter->PhysicalDeviceObject,
                  DeviceInformation->SystemMemorySize.QuadPart,
                  DeviceInformation->HighestPhysicalAddress.QuadPart,
                  DeviceInformation->TranslatedResourceList);
    DXGKRNL_TRACE("DxgkCbGetDeviceInformation: total=%I64u us\n",
                  DxgkpTraceElapsedUs(TotalStart100ns));

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
static VOID
DxgkpFreeCallbackMemoryEntry(
    _In_ PDXGKP_CALLBACK_MEMORY_ENTRY Entry)
{
    if (Entry->Kind == DxgkpCallbackMemoryContiguous)
    {
        MmFreeContiguousMemory(Entry->Memory.ContiguousMemory);
    }
    else
    {
        ASSERT(Entry->Kind == DxgkpCallbackMemoryMdl);
        MmFreePagesFromMdl(Entry->Memory.Mdl);
        ExFreePool(Entry->Memory.Mdl);
    }

    ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
}

static PDXGKP_CALLBACK_MEMORY_ENTRY
DxgkpDetachCallbackMemoryEntry(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE MemoryHandle,
    _In_ DXGKP_CALLBACK_MEMORY_KIND Kind)
{
    PLIST_ENTRY Link;
    PDXGKP_CALLBACK_MEMORY_ENTRY Found = NULL;

    ExAcquireFastMutex(&DxgkpCallbackMemoryMutex);
    for (Link = DxgkpCallbackMemoryList.Flink;
         Link != &DxgkpCallbackMemoryList;
         Link = Link->Flink)
    {
        PDXGKP_CALLBACK_MEMORY_ENTRY Entry =
            CONTAINING_RECORD(Link, DXGKP_CALLBACK_MEMORY_ENTRY, ListEntry);

        if ((HANDLE)Entry == MemoryHandle &&
            Entry->Adapter == Adapter &&
            Entry->Kind == Kind)
        {
            RemoveEntryList(&Entry->ListEntry);
            Found = Entry;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkpCallbackMemoryMutex);

    return Found;
}

static VOID
DxgkpReleaseCallbackMemory(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY FreeList;
    PLIST_ENTRY Link;
    ULONG LeakCount = 0;

    InitializeListHead(&FreeList);

    ExAcquireFastMutex(&DxgkpCallbackMemoryMutex);
    Link = DxgkpCallbackMemoryList.Flink;
    while (Link != &DxgkpCallbackMemoryList)
    {
        PLIST_ENTRY Next = Link->Flink;
        PDXGKP_CALLBACK_MEMORY_ENTRY Entry =
            CONTAINING_RECORD(Link, DXGKP_CALLBACK_MEMORY_ENTRY, ListEntry);

        if (Entry->Adapter == Adapter)
        {
            RemoveEntryList(Link);
            InsertTailList(&FreeList, Link);
            LeakCount++;
        }
        Link = Next;
    }
    ExReleaseFastMutex(&DxgkpCallbackMemoryMutex);

    while (!IsListEmpty(&FreeList))
    {
        Link = RemoveHeadList(&FreeList);
        DxgkpFreeCallbackMemoryEntry(
            CONTAINING_RECORD(Link,
                              DXGKP_CALLBACK_MEMORY_ENTRY,
                              ListEntry));
    }

    if (LeakCount != 0)
    {
        DXGKRNL_WARN("DxgkpReleaseCallbackMemory: reclaimed %lu leaked "
                     "WDDM callback allocations for adapter %p\n",
                     LeakCount,
                     Adapter);
    }
}

NTSTATUS
APIENTRY
DxgkCbAllocateContiguousMemory(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARGCB_ALLOCATECONTIGUOUSMEMORY pAllocateContiguousMemory)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_CALLBACK_MEMORY_ENTRY Entry;
    MEMORY_CACHING_TYPE CacheType;
    PVOID Va;
    ULONGLONG        TotalStart100ns;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    if (pAllocateContiguousMemory == NULL ||
        pAllocateContiguousMemory->NumberOfBytes == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    pAllocateContiguousMemory->hMemoryHandle = NULL;
    pAllocateContiguousMemory->pMemory = NULL;

    if ((ULONGLONG)
            pAllocateContiguousMemory->LowestAcceptableAddress.QuadPart >
        (ULONGLONG)
            pAllocateContiguousMemory->HighestAcceptableAddress.QuadPart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pAllocateContiguousMemory->BoundaryAddressMultiple.QuadPart != 0)
    {
        ULONGLONG Boundary =
            (ULONGLONG)
                pAllocateContiguousMemory->BoundaryAddressMultiple.QuadPart;

        if (Boundary < PAGE_SIZE ||
            (Boundary & (Boundary - 1)) != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    switch (pAllocateContiguousMemory->CacheType)
    {
        case DXGK_MEMORY_CACHING_TYPE_NON_CACHED:
            CacheType = MmNonCached;
            break;
        case DXGK_MEMORY_CACHING_TYPE_CACHED:
            CacheType = MmCached;
            break;
        case DXGK_MEMORY_CACHING_TYPE_WRITE_COMBINED:
            CacheType = MmWriteCombined;
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Entry),
                                  TAG_DXGK_RESOURCES);
    if (Entry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Va = MmAllocateContiguousMemorySpecifyCache(
             pAllocateContiguousMemory->NumberOfBytes,
             pAllocateContiguousMemory->LowestAcceptableAddress,
             pAllocateContiguousMemory->HighestAcceptableAddress,
             pAllocateContiguousMemory->BoundaryAddressMultiple,
             CacheType);

    if (Va == NULL)
    {
        DXGKRNL_ERR("DxgkCbAllocateContiguousMemory: failed "
                    "NumberOfBytes=%Iu\n",
                    pAllocateContiguousMemory->NumberOfBytes);
        ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Entry->Adapter = Adapter;
    Entry->Kind = DxgkpCallbackMemoryContiguous;
    Entry->Memory.ContiguousMemory = Va;

    ExAcquireFastMutex(&DxgkpCallbackMemoryMutex);
    InsertTailList(&DxgkpCallbackMemoryList, &Entry->ListEntry);
    ExReleaseFastMutex(&DxgkpCallbackMemoryMutex);

    pAllocateContiguousMemory->hMemoryHandle = (HANDLE)Entry;
    pAllocateContiguousMemory->pMemory = Va;

    DXGKRNL_TRACE("DxgkCbAllocateContiguousMemory: VA=%p "
                  "NumberOfBytes=%Iu total=%I64u us\n",
                  Va,
                  pAllocateContiguousMemory->NumberOfBytes,
                  DxgkpTraceElapsedUs(TotalStart100ns));

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
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
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARGCB_FREECONTIGUOUSMEMORY pFreeContiguousMemory)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_CALLBACK_MEMORY_ENTRY Entry;

    PAGED_CODE();

    if (pFreeContiguousMemory == NULL ||
        pFreeContiguousMemory->hMemoryHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Entry = DxgkpDetachCallbackMemoryEntry(
                Adapter,
                pFreeContiguousMemory->hMemoryHandle,
                DxgkpCallbackMemoryContiguous);
    if (Entry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INVALID_HANDLE;
    }

    DXGKRNL_TRACE("DxgkCbFreeContiguousMemory: VA=%p\n",
                  Entry->Memory.ContiguousMemory);

    DxgkpFreeCallbackMemoryEntry(Entry);

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

/*
 * DxgkCbAllocatePagesForMdl
 *
 * Provides the WDDM 2.4 MmAllocatePagesForMdlEx-equivalent allocation
 * service.  ReactOS does not yet expose a graphics IOMMU domain, so the
 * IOMMU map/unmap callbacks remain NULL and adapters continue to report the
 * IOMMU capability as unsupported.  On that truthful identity-domain path,
 * the MDL returned here describes the physical pages allocated by Mm.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbAllocatePagesForMdl(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARGCB_ALLOCATEPAGESFORMDL pAllocatePagesForMdl)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_CALLBACK_MEMORY_ENTRY Entry;
    PHYSICAL_ADDRESS HighAddress;
    MEMORY_CACHING_TYPE CacheType;
    SIZE_T RequiredBytes;
    ULONG MmFlags;
    PMDL Mdl;

    PAGED_CODE();

    if (pAllocatePagesForMdl == NULL)
        return STATUS_INVALID_PARAMETER;

    pAllocatePagesForMdl->hMemoryHandle = NULL;
    pAllocatePagesForMdl->pMdl = NULL;

    if (pAllocatePagesForMdl->TotalBytes == 0 ||
        pAllocatePagesForMdl->TotalBytes >
            ((SIZE_T)MAXULONG - (PAGE_SIZE - 1)) ||
        pAllocatePagesForMdl->LowAddress.QuadPart < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * MiAllocatePagesForMdl does not implement SkipBytes.  Publishing a
     * result while silently ignoring it would violate the callback contract.
     */
    if (pAllocatePagesForMdl->SkipBytes.QuadPart != 0)
        return STATUS_NOT_SUPPORTED;

    if (pAllocatePagesForMdl->Flags &
        ~(MM_DONT_ZERO_ALLOCATION |
          MM_ALLOCATE_FROM_LOCAL_NODE_ONLY |
          MM_ALLOCATE_FULLY_REQUIRED |
          MM_ALLOCATE_NO_WAIT |
          MM_ALLOCATE_PREFER_CONTIGUOUS |
          MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS |
          MM_ALLOCATE_FAST_LARGE_PAGES |
          MM_ALLOCATE_TRIM_IF_NECESSARY |
          MM_ALLOCATE_AND_HOT_REMOVE))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Contiguous-chunk allocation is a hard requirement that the current Mm
     * primitive cannot honor.  The same is true of allocation from the large
     * page cache and hot-removing pages from managed physical memory.
     * PREFER_CONTIGUOUS and TRIM_IF_NECESSARY are preferences, while NO_WAIT
     * is already satisfied by the nonblocking allocator.
     */
    if (pAllocatePagesForMdl->Flags &
        (MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS |
         MM_ALLOCATE_FAST_LARGE_PAGES |
         MM_ALLOCATE_AND_HOT_REMOVE))
    {
        return STATUS_NOT_SUPPORTED;
    }

    switch (pAllocatePagesForMdl->CacheType)
    {
        case DXGK_MEMORY_CACHING_TYPE_NON_CACHED:
            CacheType = MmNonCached;
            break;
        case DXGK_MEMORY_CACHING_TYPE_CACHED:
            CacheType = MmCached;
            break;
        case DXGK_MEMORY_CACHING_TYPE_WRITE_COMBINED:
            CacheType = MmWriteCombined;
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }

    HighAddress = pAllocatePagesForMdl->HighAddress;
    if ((ULONGLONG)pAllocatePagesForMdl->LowAddress.QuadPart >
        (ULONGLONG)HighAddress.QuadPart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Entry),
                                  TAG_DXGK_RESOURCES);
    if (Entry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * ReactOS Mm accepts only the two material flags below.  Enforce the
     * WDDM callback's always-fully-required rule explicitly after allocation.
     */
    MmFlags = pAllocatePagesForMdl->Flags &
              (MM_DONT_ZERO_ALLOCATION |
               MM_ALLOCATE_FROM_LOCAL_NODE_ONLY);
    Mdl = MmAllocatePagesForMdlEx(
              pAllocatePagesForMdl->LowAddress,
              HighAddress,
              pAllocatePagesForMdl->SkipBytes,
              pAllocatePagesForMdl->TotalBytes,
              CacheType,
              MmFlags);
    if (Mdl == NULL)
    {
        ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RequiredBytes =
        (pAllocatePagesForMdl->TotalBytes + PAGE_SIZE - 1) &
        ~((SIZE_T)PAGE_SIZE - 1);
    if ((SIZE_T)Mdl->ByteCount < RequiredBytes)
    {
        MmFreePagesFromMdl(Mdl);
        ExFreePool(Mdl);
        ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Entry->Adapter = Adapter;
    Entry->Kind = DxgkpCallbackMemoryMdl;
    Entry->Memory.Mdl = Mdl;

    ExAcquireFastMutex(&DxgkpCallbackMemoryMutex);
    InsertTailList(&DxgkpCallbackMemoryList, &Entry->ListEntry);
    ExReleaseFastMutex(&DxgkpCallbackMemoryMutex);

    pAllocatePagesForMdl->hMemoryHandle = (HANDLE)Entry;
    pAllocatePagesForMdl->pMdl = Mdl;

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

/*
 * DxgkCbFreePagesFromMdl
 *
 * Releases both the pages and the MDL storage returned by the matching
 * allocation callback.  MmFreePagesFromMdl intentionally does not free the
 * MDL structure itself.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbFreePagesFromMdl(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARGCB_FREEPAGESFROMMDL pFreePagesFromMdl)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_CALLBACK_MEMORY_ENTRY Entry;

    PAGED_CODE();

    if (pFreePagesFromMdl == NULL ||
        pFreePagesFromMdl->hMemoryHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Entry = DxgkpDetachCallbackMemoryEntry(
                Adapter,
                pFreePagesFromMdl->hMemoryHandle,
                DxgkpCallbackMemoryMdl);
    if (Entry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INVALID_HANDLE;
    }

    DxgkpFreeCallbackMemoryEntry(Entry);

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}
#endif

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
    if (!DxgkpAcquireVidSchCallback(Adapter))
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

    DxgkpReleaseVidSchCallback(Adapter);
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

    /* Resolve and pin the adapter while it remains on the global list. */
    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbReadDeviceSpace: invalid handle %p\n", DeviceHandle);
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
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
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
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
            return STATUS_UNSUCCESSFUL;
        }

        *BytesRead = BytesTransferred;
        ElapsedUs = DxgkpTraceElapsedUs(TotalStart100ns);
        if (ElapsedUs >= DXGK_TRACE_SLOW_CONFIG_ACCESS_US)
        {
            DXGKRNL_WARN("DxgkCbReadDeviceSpace: slow config read Off=0x%lX Len=0x%lX Bytes=%lu took %I64u us\n",
                         Offset, Length, BytesTransferred, ElapsedUs);
        }
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_SUCCESS;
    }

    /* Other space types not yet supported. */
    DXGKRNL_WARN("DxgkCbReadDeviceSpace: unsupported DataType %lu\n", DataType);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
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

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbWriteDeviceSpace: invalid handle %p\n", DeviceHandle);
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
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
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
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
            return STATUS_UNSUCCESSFUL;
        }

        *BytesWritten = BytesTransferred;
        ElapsedUs = DxgkpTraceElapsedUs(TotalStart100ns);
        if (ElapsedUs >= DXGK_TRACE_SLOW_CONFIG_ACCESS_US)
        {
            DXGKRNL_WARN("DxgkCbWriteDeviceSpace: slow config write Off=0x%lX Len=0x%lX Bytes=%lu took %I64u us\n",
                         Offset, Length, BytesTransferred, ElapsedUs);
        }
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_SUCCESS;
    }

    DXGKRNL_WARN("DxgkCbWriteDeviceSpace: unsupported DataType %lu\n", DataType);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/*
 * DxgkCbMapPhysicalMemory (legacy internal helper)
 *
 * Maps a physical address range into kernel virtual address space.
 * Uses MmNonCached because GPU MMIO registers must not be cached.
 * This two-argument helper is not the WDDM 2.9 physical-memory-object
 * callback and is deliberately not published in DXGKRNL_INTERFACE.
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
 * DxgkCbUnmapPhysicalMemory (legacy internal helper)
 *
 * Unmaps a range mapped by DxgkCbMapPhysicalMemory.
 * This two-argument helper is not the WDDM 2.9 physical-memory-object
 * callback and is deliberately not published in DXGKRNL_INTERFACE.
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
    NTSTATUS Status = STATUS_SUCCESS;

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

    /* Publish the connector state before any deferred topology snapshot.  The
     * rebuild is never run inside a reverse callback because the miniport may
     * already own the adapter KMD transaction on this thread. */
    if (ChildStatus != NULL && ChildStatus->Type == StatusConnection)
    {
        if (DxgkPnpPublishChildConnection(Adapter, ChildStatus->ChildUid, ChildStatus->HotPlug.Connected))
            Status = DxgkVidPnQueueHotPlugRebuild(Adapter);
    }

    IoInvalidateDeviceRelations(Adapter->PhysicalDeviceObject, BusRelations);

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

/*
 * DxgkCbQueryServices
 *
 * Returns an interface for the requested service type. Used by miniports
 * to obtain DMA adapter, AGP, or debug report interfaces.
 */
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
static VOID
NTAPI
DxgkpFeatureInterfaceReferenceNop(
    _In_opt_ PVOID Context)
{
    /*
     * This is a same-stack interface: dxgkrnl cannot unload while its
     * display miniport is running. Each callback validates the opaque adapter
     * handle and holds ReverseCallbackRundownRef for the duration of the call.
     */
    UNREFERENCED_PARAMETER(Context);
}

static NTSTATUS
APIENTRY
DxgkpFeatureIsEnabled(
    _In_ HANDLE DeviceHandle,
    INOUT_PDXGKARGCB_ISFEATUREENABLED2 Args)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    if (Args == NULL)
        return STATUS_INVALID_PARAMETER;

    Args->Result.Version = 0;
    Args->Result.Value = 0;
    if (Args->Flags.Value != 0)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Status = DxgkQueryFeatureState(Adapter,
                                   Args->FeatureId,
                                   &Args->Result);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

static NTSTATUS
APIENTRY
DxgkpFeatureQueryInterface(
    _In_ HANDLE DeviceHandle,
    INOUT_PDXGKARGCB_QUERYFEATUREINTERFACE Args)
{
    PDXGKRNL_ADAPTER Adapter;

    if (Args == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);

    /*
     * None of the OS-side features ReactOS currently enables has a
     * feature-specific callback table. In particular, KMD-signaled CPU
     * events use the main DXGK_INTERFACE callback.
     */
    return STATUS_NOT_SUPPORTED;
}
#endif

NTSTATUS
APIENTRY
DxgkCbQueryServices(
    _In_ HANDLE DeviceHandle,
    _In_ DXGK_SERVICES ServicesType,
    _Inout_ PINTERFACE Interface)
{
    PDXGKRNL_ADAPTER Adapter;

    DXGKRNL_TRACE("DxgkCbQueryServices: handle=%p type=%lu iface=%p\n",
                  DeviceHandle, (ULONG)ServicesType, Interface);

    if (Interface == NULL)
        return STATUS_INVALID_PARAMETER;
    if (ServicesType < DxgkServicesAgp ||
        ServicesType > DxgkServicesFeature)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    /*
     * Preserve the legacy internal bus-interface selector used by existing
     * miniports.  It is not present in the public DXGK_SERVICES enum, but
     * removing it prevents PCI display drivers from obtaining the DMA adapter
     * needed to allocate device-visible common buffers.
     */
    if ((ULONG)ServicesType == 1)
    {
        PBUS_INTERFACE_STANDARD BusInterface =
            (PBUS_INTERFACE_STANDARD)Interface;
        static const GUID DxgkpBusInterfaceStandardGuid =
        {
            0x496b8280, 0x6f25, 0x11d0,
            { 0xbe, 0xaf, 0x08, 0x00, 0x2b, 0xe2, 0x09, 0x2f }
        };
        KEVENT Event;
        IO_STATUS_BLOCK IoStatus;
        PIRP Irp;
        PIO_STACK_LOCATION Stack;
        PDEVICE_OBJECT TargetDevice;
        NTSTATUS Status;

        if (Adapter->PhysicalDeviceObject == NULL)
        {
            ExReleaseRundownProtection(
                &Adapter->ReverseCallbackRundownRef);
            return STATUS_NOT_SUPPORTED;
        }

        TargetDevice =
            IoGetAttachedDeviceReference(Adapter->PhysicalDeviceObject);
        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                           TargetDevice,
                                           NULL,
                                           0,
                                           NULL,
                                           &Event,
                                           &IoStatus);
        if (Irp == NULL)
        {
            ObDereferenceObject(TargetDevice);
            ExReleaseRundownProtection(
                &Adapter->ReverseCallbackRundownRef);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        Stack = IoGetNextIrpStackLocation(Irp);
        Stack->MajorFunction = IRP_MJ_PNP;
        Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
        Stack->Parameters.QueryInterface.InterfaceType =
            &DxgkpBusInterfaceStandardGuid;
        Stack->Parameters.QueryInterface.Size =
            sizeof(BUS_INTERFACE_STANDARD);
        Stack->Parameters.QueryInterface.Version = 1;
        Stack->Parameters.QueryInterface.Interface =
            (PINTERFACE)BusInterface;
        Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

        Status = IoCallDriver(TargetDevice, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            Status = IoStatus.Status;
        }
        ObDereferenceObject(TargetDevice);
        ExReleaseRundownProtection(
            &Adapter->ReverseCallbackRundownRef);
        return Status;
    }

#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    if (ServicesType == DxgkServicesFeature)
    {
        PDXGK_FEATURE_INTERFACE FeatureInterface =
            (PDXGK_FEATURE_INTERFACE)Interface;
        DXGK_FEATURE_INTERFACE ReturnedInterface;

        if (FeatureInterface->Size < sizeof(ReturnedInterface))
        {
            ExReleaseRundownProtection(
                &Adapter->ReverseCallbackRundownRef);
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (FeatureInterface->Version !=
            DXGK_FEATURE_INTERFACE_VERSION_1)
        {
            ExReleaseRundownProtection(
                &Adapter->ReverseCallbackRundownRef);
            return STATUS_NOT_SUPPORTED;
        }

        RtlZeroMemory(&ReturnedInterface, sizeof(ReturnedInterface));
        ReturnedInterface.Size = sizeof(ReturnedInterface);
        ReturnedInterface.Version =
            DXGK_FEATURE_INTERFACE_VERSION_1;
        ReturnedInterface.Context = Adapter;
        ReturnedInterface.InterfaceReference =
            DxgkpFeatureInterfaceReferenceNop;
        ReturnedInterface.InterfaceDereference =
            DxgkpFeatureInterfaceReferenceNop;
        ReturnedInterface.IsFeatureEnabled =
            DxgkpFeatureIsEnabled;
        ReturnedInterface.QueryFeatureInterface =
            DxgkpFeatureQueryInterface;
        *FeatureInterface = ReturnedInterface;

        ExReleaseRundownProtection(
            &Adapter->ReverseCallbackRundownRef);
        return STATUS_SUCCESS;
    }
#endif

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);

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
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INVALID_PARAMETER;
    }

    if (SynchronizeRoutine == NULL)
    {
        *ReturnValue = TRUE;
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
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

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
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
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInformation)
{
    LOADER_PARAMETER_FRAMEBUFFER Fb;
    D3DDDIFORMAT                 ColorFormat;
    ULONG                        BytesPerPixel;
    ULONGLONG                    TotalStart100ns;
    ULONGLONG                    GopQueryUs = 0;
    ULONGLONG                    OwnershipUs = 0;
    ULONGLONG                    StepStart100ns;
    NTSTATUS                     Status = STATUS_SUCCESS;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: handle=%p out=%p\n",
                  DeviceHandle, DisplayInformation);

    RtlZeroMemory(DisplayInformation, sizeof(*DisplayInformation));
    (VOID)KeWaitForSingleObject(&g_PostDisplayOwnershipMutex, Executive, KernelMode, FALSE, NULL);

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
        PDEVICE_OBJECT OwnerDeviceObject;
        PDXGKRNL_ADAPTER Owner = DxgkpReferencePostDisplayOwner(&OwnerDeviceObject);

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
                ObDereferenceObject(OwnerDeviceObject);
                goto Complete;
            }

            Status = DxgkpStopPostDisplayOwner(Owner);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_ERR("DxgkCbAcquirePostDisplayOwnership: old owner %p could not be stopped 0x%08lX\n", Owner, Status);
                ObDereferenceObject(OwnerDeviceObject);
                goto Complete;
            }
        }
        if (OwnerDeviceObject != NULL)
            ObDereferenceObject(OwnerDeviceObject);
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
            DxgkpSetPostDisplayOwner(Adapter);
            goto Complete;
        }

        /* Expected on headless boots: the miniport cold-starts. */
        DXGKRNL_WARN("DxgkCbAcquirePostDisplayOwnership: "
                     "no GOP and no VBE — miniport must cold-start\n");
        goto Complete;
    }

    StepStart100ns = DxgkpTraceNow100ns();
    if (!InbvGetGopFrameBufferInfo(&Fb))
    {
        DXGKRNL_ERR("DxgkCbAcquirePostDisplayOwnership: "
                    "InbvGetGopFrameBufferInfo failed\n");
        goto Complete;
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
            goto Complete;
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

    DxgkpSetPostDisplayOwner((PDXGKRNL_ADAPTER)DeviceHandle);

    DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: gop=%I64u us ownership=%I64u us total=%I64u us\n",
                  GopQueryUs,
                  OwnershipUs,
                  DxgkpTraceElapsedUs(TotalStart100ns));

Complete:
    KeReleaseMutex(&g_PostDisplayOwnershipMutex, FALSE);
    return Status;
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

    if (Adapter == NULL)
        return FALSE;
    if (!DxgkpAcquireVidSchCallback(Adapter))
        return FALSE;
    if (Adapter->MiniportContext == NULL || Adapter->MiniportContext->InitData.s.DxgkDdiInterruptRoutine == NULL)
    {
        DxgkpReleaseVidSchCallback(Adapter);
        return FALSE;
    }
    if (!DxgkAcquireInterruptCallback(Adapter))
    {
        DxgkpReleaseVidSchCallback(Adapter);
        return FALSE;
    }

    Sequence = InterlockedIncrement(&Adapter->InterruptCount);
    Logged = (Sequence <= DXGK_TRACE_ISR_LOG_LIMIT);
    Start100ns = DxgkpTraceNow100ns();

    Handled = Adapter->MiniportContext->InitData.s.DxgkDdiInterruptRoutine(Adapter->MiniportDeviceContext, MessageNumber);
    DxgkReleaseInterruptCallback(Adapter);

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

    DxgkpReleaseVidSchCallback(Adapter);
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
 * the interrupt descriptors that really are messages.
 *
 * The FDO's interrupt resource descriptor can arrive line-based on ROS ARM64 even
 * when MSI-X is in use.  The miniport (viogpudo) reads these resources back via
 * DxgkCbGetDeviceInformation and, if it sees a line-based interrupt, programs its
 * VirtIO queues for polling (NO_VECTOR) instead of enabling per-queue MSI-X — so
 * the device never raises a completion MSI.  Once we know we are message-based,
 * mark the resources accordingly so the miniport enables queue MSI-X.
 *
 * But *only* the ones that are messages.  This used to mark every interrupt
 * descriptor it found, which included the INTx GSI still present in the list
 * alongside the MSI-X messages.  A miniport pairing its virtqueues with messages
 * then counts one more than the device's table holds, and the extra one -- the
 * line -- is precisely the interrupt that cannot fire while the device is in
 * MSI-X mode.
 *
 * An MSI is edge-triggered by definition: the message is a posted write, and
 * there is no wire to hold asserted.  A level-sensitive descriptor is therefore
 * never a message, and that is a property of what MSI *is* rather than a guess
 * about how this platform happens to build its resource lists.
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
        PCM_PARTIAL_RESOURCE_LIST Partial = &ResourceList->List[li].PartialResourceList;
        for (di = 0; di < Partial->Count; di++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &Partial->PartialDescriptors[di];

            if (Desc->Type != CmResourceTypeInterrupt)
                continue;
            if ((Desc->Flags & CM_RESOURCE_INTERRUPT_LATCHED) == 0)
            {
                /* Level-sensitive: the INTx line, not a message.  Leave it as
                 * it is so the miniport does not count it among its MSI-X
                 * messages. */
                continue;
            }
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
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;

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
            Status = PfnQueryAdapterInfo(Adapter->MiniportDeviceContext, &QueryArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (NT_SUCCESS(Status))
            break;
    }

    DxgkReleaseKmdCall(Adapter);
    return Status;
}

NTSTATUS
DxgkpQueryGpuMmuCaps(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ DXGK_GPUMMUCAPS *Caps)
{
    PDXGKDDI_QUERY_ADAPTER_INFO PfnQueryAdapterInfo;
    DXGKARG_QUERYADAPTERINFO QueryArgs;
    NTSTATUS Status;

    if (Caps == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Caps, sizeof(*Caps));
    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return STATUS_INVALID_PARAMETER;

    PfnQueryAdapterInfo = DXGK_CB(Adapter, DxgkDdiQueryAdapterInfo);
    if (PfnQueryAdapterInfo == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;

    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.Type = DXGKQAITYPE_GPUMMUCAPS;
    QueryArgs.pOutputData = Caps;
    QueryArgs.OutputDataSize = sizeof(*Caps);

    _SEH2_TRY
    {
        Status = PfnQueryAdapterInfo(Adapter->MiniportDeviceContext, &QueryArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    DxgkReleaseKmdCall(Adapter);
    return Status;
}

/*
 * DxgkpQueryPageTableLevelDesc
 *
 * Reads one level of the miniport's page-table geometry.  Level 0 is the leaf.
 */
NTSTATUS
DxgkpQueryPageTableLevelDesc(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG LevelIndex,
    _Out_ DXGK_PAGE_TABLE_LEVEL_DESC *Desc)
{
    PDXGKDDI_QUERY_ADAPTER_INFO PfnQueryAdapterInfo;
    DXGKARG_QUERYADAPTERINFO QueryArgs;
    DXGK_QUERYPAGETABLELEVELDESCIN Input;
    NTSTATUS Status;

    if (Desc == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Desc, sizeof(*Desc));
    if (Adapter == NULL || Adapter->MiniportContext == NULL || LevelIndex > MAXUSHORT)
        return STATUS_INVALID_PARAMETER;

    PfnQueryAdapterInfo = DXGK_CB(Adapter, DxgkDdiQueryAdapterInfo);
    if (PfnQueryAdapterInfo == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.LevelIndex = (WORD)LevelIndex;
    Input.PhysicalAdapterIndex = 0;
    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.Type = DXGKQAITYPE_PAGETABLELEVELDESC;
    QueryArgs.pInputData = &Input;
    QueryArgs.InputDataSize = sizeof(Input);
    QueryArgs.pOutputData = Desc;
    QueryArgs.OutputDataSize = sizeof(*Desc);

    _SEH2_TRY
    {
        Status = PfnQueryAdapterInfo(Adapter->MiniportDeviceContext, &QueryArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    DxgkReleaseKmdCall(Adapter);
    return Status;
}

/*
 * DxgkpCacheGpuMmuGeometry
 *
 * Validates and caches every page-table level the miniport declares.  A level
 * whose index bit count, size, or alignment is inconsistent disables the whole
 * GPU virtual-memory model rather than leaving a partially-derived geometry
 * that later code would have to guess about.
 */
static BOOLEAN
DxgkpCacheGpuMmuGeometry(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG TotalIndexBits = 0;
    ULONG Level;

    if (Adapter->GpuMmuCaps.PageTableLevelCount == 0 ||
        Adapter->GpuMmuCaps.PageTableLevelCount > DXGK_MAX_PAGE_TABLE_LEVELS)
    {
        return FALSE;
    }

    for (Level = 0; Level < Adapter->GpuMmuCaps.PageTableLevelCount; ++Level)
    {
        DXGK_PAGE_TABLE_LEVEL_DESC *Desc = &Adapter->PageTableLevels[Level];
        ULONGLONG Entries;

        if (!NT_SUCCESS(DxgkpQueryPageTableLevelDesc(Adapter, Level, Desc)))
            return FALSE;
        if (Desc->PageTableIndexBitCount == 0 || Desc->PageTableIndexBitCount >= 32)
            return FALSE;
        if (Desc->PageTableSizeInBytes == 0)
            return FALSE;
        /* Zero alignment means the memory segment's page size. */
        if (Desc->PageTableAlignmentInBytes == 0)
            Desc->PageTableAlignmentInBytes = PAGE_SIZE;
        if ((Desc->PageTableAlignmentInBytes & (Desc->PageTableAlignmentInBytes - 1)) != 0)
            return FALSE;
        Entries = 1ULL << Desc->PageTableIndexBitCount;
        if (Entries * sizeof(DXGK_PTE) > Desc->PageTableSizeInBytes)
            return FALSE;
        TotalIndexBits += Desc->PageTableIndexBitCount;
    }

    /* Every VA bit above the page offset must be covered by exactly the
     * declared levels, otherwise a translation would be ambiguous. */
    if (TotalIndexBits + 12 != Adapter->GpuMmuCaps.VirtualAddressBitCount)
        return FALSE;

    Adapter->PageTableLevelsValid = TRUE;
    return TRUE;
}

/* ========================================================================
 * Adapter lifecycle functions
 * ====================================================================== */

static ULONG
DxgkpMms2GetAdapterFlags(_In_ PDXGKRNL_ADAPTER Adapter)
{
    return Adapter->MiniportContext->IsDisplayOnlyDriver ? DXGMMS2_ADAPTER_FLAG_DISPLAY_ONLY : 0;
}

static DECLSPEC_NORETURN VOID
DxgkpBugCheckMms2Lifecycle(_In_ PDXGKRNL_ADAPTER Adapter, _In_ NTSTATUS FailureStatus, _In_ ULONG Phase)
{
    InterlockedExchange(&Adapter->KmdCallsBlocked, 1);
    InterlockedExchange(&Adapter->InterruptCallbacksBlocked, 1);
    InterlockedExchange(&Adapter->MiniportCallbacksValid, 0);
    KeMemoryBarrier();
    DXGKRNL_ERR("DxgkpBugCheckMms2Lifecycle: adapter %p cannot contain dxgmms2 lifecycle failure, status 0x%08lX phase %lu\n", Adapter, FailureStatus, Phase);
    KeBugCheckEx(DXGKP_BUGCHECK_VIDEO_DXGKRNL_FATAL_ERROR, (ULONG_PTR)DXGKP_FATAL_MMS2_LIFECYCLE_SUBTYPE, (ULONG_PTR)FailureStatus, (ULONG_PTR)Adapter, (ULONG_PTR)Phase);
}

static VOID
DxgkpMms2PublishStarted(_In_ PDXGKRNL_ADAPTER Adapter)
{
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    ASSERT(Adapter->Mms2State == DxgkMms2AdapterCreated || Adapter->Mms2State == DxgkMms2AdapterStopped);
    Adapter->Mms2State = DxgkMms2AdapterStarted;
    Adapter->Mms2StopReason = 0;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
}

static VOID
DxgkpMms2PublishContextStreamInterface(_In_ PDXGKRNL_ADAPTER Adapter, _In_ const DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *ContextStreamInterface)
{
    ASSERT(Adapter != NULL);
    ASSERT(ContextStreamInterface != NULL);
    ASSERT(InterlockedCompareExchange(&Adapter->Mms2ContextStreamValid, 0, 0) == 0);
    Adapter->Mms2ContextStreamInterface = *ContextStreamInterface;
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->Mms2ContextStreamValid, 1);
}

static VOID
DxgkpMms2UnpublishContextStreamInterface(_In_ PDXGKRNL_ADAPTER Adapter)
{
    ASSERT(Adapter != NULL);
    InterlockedExchange(&Adapter->Mms2ContextStreamValid, 0);
    KeMemoryBarrier();
}

static VOID
DxgkpMms2ClearContextStreamInterface(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DxgkpMms2UnpublishContextStreamInterface(Adapter);
    RtlZeroMemory(&Adapter->Mms2ContextStreamInterface, sizeof(Adapter->Mms2ContextStreamInterface));
}

static NTSTATUS
DxgkpMms2BeginStop(_In_ PDXGKRNL_ADAPTER Adapter, _In_ DXGMMS2_STOP_REASON RequestedReason)
{
    DXGMMS2_ADAPTER_HANDLE Mms2Adapter;
    BOOLEAN ContextStreamWasPublished;
    NTSTATUS Status;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2State == DxgkMms2AdapterBeginPending || Adapter->Mms2State == DxgkMms2AdapterStopping || Adapter->Mms2State == DxgkMms2AdapterCreated || Adapter->Mms2State == DxgkMms2AdapterStopped)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_SUCCESS;
    }
    if (Adapter->Mms2State != DxgkMms2AdapterStarted || Adapter->Mms2Adapter == NULL)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Mms2Adapter = Adapter->Mms2Adapter;
    ContextStreamWasPublished = InterlockedCompareExchange(&Adapter->Mms2ContextStreamValid, 0, 0) != 0;
    DxgkpMms2UnpublishContextStreamInterface(Adapter);
    Adapter->Mms2State = DxgkMms2AdapterBeginPending;
    Adapter->Mms2StopReason = RequestedReason;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    Status = DxgkpMms2BeginStopAdapter(Mms2Adapter, RequestedReason);
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2Adapter == Mms2Adapter && Adapter->Mms2State == DxgkMms2AdapterBeginPending && Adapter->Mms2StopReason == RequestedReason)
    {
        Adapter->Mms2State = NT_SUCCESS(Status) ? DxgkMms2AdapterStopping : DxgkMms2AdapterStarted;
        if (!NT_SUCCESS(Status))
        {
            Adapter->Mms2StopReason = 0;
            if (ContextStreamWasPublished)
            {
                KeMemoryBarrier();
                InterlockedExchange(&Adapter->Mms2ContextStreamValid, 1);
            }
        }
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    return Status;
}

static NTSTATUS
DxgkpMms2CompleteRetiredStop(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGMMS2_ADAPTER_HANDLE Mms2Adapter;
    DXGMMS2_STOP_REASON Reason;
    BOOLEAN TimelineWasPublished;
    NTSTATUS Status;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2State == DxgkMms2AdapterCreated || Adapter->Mms2State == DxgkMms2AdapterStopped)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_SUCCESS;
    }
    if (Adapter->Mms2State != DxgkMms2AdapterStopping || Adapter->Mms2Adapter == NULL)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Mms2Adapter = Adapter->Mms2Adapter;
    Reason = Adapter->Mms2StopReason;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    TimelineWasPublished = DxgkpCloseMms2TimelineCalls(Adapter);
    if (!TimelineWasPublished && InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0)
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    Status = DxgkpMms2CompleteStopAdapter(Mms2Adapter, Reason);
    if (TimelineWasPublished)
        DxgkpReopenMms2TimelineCalls(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    DxgkpMms2ClearContextStreamInterface(Adapter);
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2Adapter == Mms2Adapter && Adapter->Mms2State == DxgkMms2AdapterStopping && Adapter->Mms2StopReason == Reason)
    {
        Adapter->Mms2State = DxgkMms2AdapterStopped;
        Adapter->Mms2StopReason = 0;
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpMms2DestroyAdministrativeAdapter(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGMMS2_ADAPTER_HANDLE Mms2Adapter;
    BOOLEAN TimelineWasPublished;
    NTSTATUS Status;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2Adapter == NULL && Adapter->Mms2State == DxgkMms2AdapterAbsent)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_SUCCESS;
    }
    if (Adapter->Mms2Adapter == NULL || (Adapter->Mms2State != DxgkMms2AdapterCreated && Adapter->Mms2State != DxgkMms2AdapterStopped))
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Mms2Adapter = Adapter->Mms2Adapter;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    DxgkpMms2ClearContextStreamInterface(Adapter);
    TimelineWasPublished = DxgkpCloseMms2TimelineCalls(Adapter);
    if (!TimelineWasPublished && InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0)
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    Status = DxgkpMms2DestroyAdapter(Mms2Adapter);
    if (!NT_SUCCESS(Status))
    {
        if (TimelineWasPublished)
            DxgkpReopenMms2TimelineCalls(Adapter);
        return Status;
    }
    InterlockedExchange(&Adapter->Mms2TimelineValid, 0);
    KeMemoryBarrier();
    RtlZeroMemory(&Adapter->Mms2Timeline, sizeof(Adapter->Mms2Timeline));
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2Adapter == Mms2Adapter)
    {
        Adapter->Mms2Adapter = NULL;
        Adapter->Mms2State = DxgkMms2AdapterAbsent;
        Adapter->Mms2StopReason = 0;
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpMms2StartAdministrativeAdapter(_In_ PDXGKRNL_ADAPTER Adapter, _Out_ PBOOLEAN ProviderStarted)
{
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 ContextStreamInterface;
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    ULONGLONG EnabledSubsystems;
    ULONG HighestCompleteWddmVersion;
    ULONG RequestedWddmVersion;
    BOOLEAN TimelineWasPublished;
    NTSTATUS Status;

    EnabledSubsystems = 0;
    HighestCompleteWddmVersion = 0;
    RequestedWddmVersion = min(Adapter->MiniportContext->InitData.s.Version, (ULONG)DXGKDDI_INTERFACE_VERSION);
    RtlZeroMemory(&ContextStreamInterface, sizeof(ContextStreamInterface));
    TimelineWasPublished = DxgkpCloseMms2TimelineCalls(Adapter);
    if (!TimelineWasPublished && InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0)
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    Status = DxgkpMms2StartAdapter(Adapter->Mms2Adapter, Adapter->MiniportContext->InitData.s.Version, RequestedWddmVersion, Adapter->NodeCount, Adapter->SegmentCount, DxgkpMms2GetAdapterFlags(Adapter), Adapter->SchedulingCaps.Value, &EnabledSubsystems, &HighestCompleteWddmVersion, ProviderStarted);
    if (NT_SUCCESS(Status))
    {
        /* dxgmms2 reports which subsystems it actually owns.  The scheduler
         * bit is required: dxgkrnl has no run queue of its own to fall back
         * on, so a provider that does not own scheduling cannot drive it. */
        if ((EnabledSubsystems & (DXGMMS2_SUBSYSTEM_SCHEDULER | DXGMMS2_SUBSYSTEM_VIDMM)) != (DXGMMS2_SUBSYSTEM_SCHEDULER | DXGMMS2_SUBSYSTEM_VIDMM))
        {
            DXGKRNL_ERR("dxgmms2 does not own the scheduler and VidMm subsystems (0x%I64X)\n", EnabledSubsystems);
            Status = STATUS_REVISION_MISMATCH;
        }
        Adapter->Mms2EnabledSubsystems = EnabledSubsystems;
        Adapter->Mms2HighestCompleteWddmVersion = HighestCompleteWddmVersion;
        if (NT_SUCCESS(Status))
            Status = DxgkpMms2QueryVidMmInterface(Adapter->Mms2Adapter, &Adapter->Mms2VidMmInterface);
        if (NT_SUCCESS(Status))
        {
            InterlockedExchange(&Adapter->Mms2VidMmValid, 1);
            /* Segment geometry is discovered before the provider starts, so
             * it is published here, once the owner exists. */
            Status = DxgkVidMmPublishSegments(Adapter);
            if (!NT_SUCCESS(Status))
                InterlockedExchange(&Adapter->Mms2VidMmValid, 0);
        }
        if (NT_SUCCESS(Status))
            Status = DxgkpMms2QuerySchedulerInterface(Adapter->Mms2Adapter, &Adapter->Mms2SchedulerInterface);
        if (NT_SUCCESS(Status))
        {
            InterlockedExchange(&Adapter->Mms2SchedulerValid, 1);
            Status = Adapter->Mms2SchedulerInterface.Start(Adapter->Mms2SchedulerInterface.SchedulerHandle, Adapter->NodeCount != 0 ? Adapter->NodeCount : 1);
            if (Status == STATUS_INVALID_DEVICE_STATE)
                Status = STATUS_SUCCESS;   /* already started by StartAdapter */
        }
        if (NT_SUCCESS(Status))
            Status = DxgkpMms2QuerySchedulerTimeline(Adapter->Mms2Adapter, &Timeline);
        if (NT_SUCCESS(Status) && Timeline.NodeCount != Adapter->NodeCount)
            Status = STATUS_REVISION_MISMATCH;
        if (NT_SUCCESS(Status))
            Status = DxgkpMms2QueryContextStreamInterface(Adapter->Mms2Adapter, &ContextStreamInterface);
        if (NT_SUCCESS(Status))
        {
            Adapter->Mms2Timeline = Timeline;
            DxgkpMms2PublishContextStreamInterface(Adapter, &ContextStreamInterface);
            DxgkpPublishMms2TimelineCalls(Adapter);
        }
    }
    if (!NT_SUCCESS(Status) && !*ProviderStarted && TimelineWasPublished)
        DxgkpReopenMms2TimelineCalls(Adapter);
    else if (!NT_SUCCESS(Status) && *ProviderStarted)
    {
        DxgkpMms2UnpublishContextStreamInterface(Adapter);
        InterlockedExchange(&Adapter->Mms2TimelineValid, 0);
        KeMemoryBarrier();
    }
    return Status;
}

static NTSTATUS
DxgkpBeginAdapterStart(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PULONG Generation)
{
    ULONG NextGeneration;
    NTSTATUS Status = STATUS_SUCCESS;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->State != DxgkAdapterStateStopped || InterlockedCompareExchange(&Adapter->AdapterStopInProgress, 0, 0) != 0 || Adapter->AdapterStopIntentCount != 0)
    {
        Status = STATUS_DEVICE_BUSY;
    }
    else
    {
        NextGeneration = Adapter->AdapterStartGeneration + 1;
        if (NextGeneration == 0)
            NextGeneration = 1;
        Adapter->AdapterStartGeneration = NextGeneration;
        Adapter->AdapterStartStatus = STATUS_PENDING;
        KeResetEvent(&Adapter->AdapterStartCompletedEvent);
        Adapter->State = DxgkAdapterStateStarting;
        *Generation = NextGeneration;
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    return Status;
}

static VOID
DxgkpCompleteAdapterStart(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Generation,
    _In_ NTSTATUS Status,
    _In_ BOOLEAN Restartable)
{
    BOOLEAN QueueHotPlug = FALSE;

    /*
     * A restartable failure has crossed a proven StopDevice boundary (or the
     * miniport never completed StartDevice), so it must not remain the boot
     * display owner.  A non-restartable rollback failed to stop the miniport;
     * retain ownership until RemoveDevice rather than letting a second
     * claimant race hardware that can still be scanning out.
     */
    if (!NT_SUCCESS(Status) && Restartable)
        DxgkpClearPostDisplayOwner(Adapter);
    if (NT_SUCCESS(Status))
        DxgkPnpBeginChildEnumerationEpoch(Adapter);

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    ASSERT(Adapter->AdapterStartGeneration == Generation);
    ASSERT(Adapter->State == DxgkAdapterStateStarting);
    if (Adapter->AdapterStartGeneration == Generation && Adapter->State == DxgkAdapterStateStarting)
    {
        Adapter->AdapterStartStatus = Status;
        Adapter->AdapterStartCompletedGeneration = Generation;
        Adapter->State = NT_SUCCESS(Status) ? DxgkAdapterStateStarted : (Restartable ? DxgkAdapterStateStopped : DxgkAdapterStateStopping);
        QueueHotPlug = NT_SUCCESS(Status);
        KeSetEvent(&Adapter->AdapterStartCompletedEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    if (QueueHotPlug)
        (VOID)DxgkVidPnQueueHotPlugRebuild(Adapter);
}

/* Returns with AdapterMutex held after every in-flight start has completed. */
static VOID
DxgkpAcquireAdapterMutexAfterStart(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    for (;;)
    {
        ULONG Generation;

        (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
        if (Adapter->State != DxgkAdapterStateStarting)
            return;
        Generation = Adapter->AdapterStartGeneration;
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        KeWaitForSingleObject(&Adapter->AdapterStartCompletedEvent, Executive, KernelMode, FALSE, NULL);
        (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
        if (Adapter->AdapterStartCompletedGeneration == Generation && Adapter->State != DxgkAdapterStateStarting)
            return;
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    }
}

static NTSTATUS
DxgkpCollectAdapterDiagnosticInfo(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_DIAGNOSTICINFO_TYPE Type,
    _In_ BOOLEAN AcquireCallbackGate)
{
    PDXGKDDI_COLLECTDIAGNOSTICINFO CollectDiagnosticInfo;
    DXGKARG_COLLECTDIAGNOSTICINFO Args;
    PVOID Buffer;
    NTSTATUS Status;
    BOOLEAN CallbackAcquired;

    PAGED_CODE();

    if (Adapter == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->PhysicalDeviceObject == NULL ||
        Adapter->MiniportContext->UseDodLayout ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_6))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Type == DXGK_DI_BLACKSCREEN &&
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_7))
    {
        return STATUS_NOT_SUPPORTED;
    }

    CollectDiagnosticInfo =
        DXGK_CB_FULL(Adapter, DxgkDdiCollectDiagnosticInfo);
    if (CollectDiagnosticInfo == NULL)
        return STATUS_NOT_SUPPORTED;

    Buffer = ExAllocatePoolWithTag(
                 PagedPool,
                 DXGKP_DIAGNOSTIC_BUFFER_SIZE,
                 TAG_DXGK_ADAPTER);
    if (Buffer != NULL)
        RtlZeroMemory(Buffer, DXGKP_DIAGNOSTIC_BUFFER_SIZE);

    RtlZeroMemory(&Args, sizeof(Args));
    Args.hAdapter = Adapter->MiniportDeviceContext;
    Args.Type = Type;
    Args.BufferSizeIn =
        Buffer != NULL ? DXGKP_DIAGNOSTIC_BUFFER_SIZE : 0;
    Args.pBuffer = Buffer;
    CallbackAcquired = FALSE;

    if (AcquireCallbackGate)
    {
        if (!DxgkAcquireMiniportCallback(Adapter))
        {
            Status = STATUS_DELETE_PENDING;
            goto Cleanup;
        }
        CallbackAcquired = TRUE;
    }

    _SEH2_TRY
    {
        Status = CollectDiagnosticInfo(
                     Adapter->PhysicalDeviceObject,
                     &Args);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (CallbackAcquired)
        DxgkReleaseMiniportCallback(Adapter);

    Args.BucketingString[DXGK_DUMP_BUCKETING_BUFFER_SIZE - 1] = '\0';
    Args.DescriptionString[DXGK_DUMP_DESCRIPTION_BUFFER_SIZE - 1] = '\0';
    if (Args.BufferSizeOut > Args.BufferSizeIn)
    {
        DXGKRNL_ERR("DxgkCollectAdapterDiagnosticInfo: miniport returned "
                    "oversized payload %u > %u\n",
                    Args.BufferSizeOut,
                    Args.BufferSizeIn);
        Args.BufferSizeOut = 0;
        Status = STATUS_DATA_ERROR;
    }

    DXGKRNL_WARN("DxgkCollectAdapterDiagnosticInfo: type=%u "
                 "status=0x%08lX bucket=%s description=%s bytes=%u\n",
                 (UINT)Type,
                 Status,
                 Args.BucketingString,
                 Args.DescriptionString,
                 Args.BufferSizeOut);

Cleanup:
    if (Buffer != NULL)
        ExFreePoolWithTag(Buffer, TAG_DXGK_ADAPTER);
    return Status;
}

NTSTATUS
DxgkCollectAdapterDiagnosticInfo(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_DIAGNOSTICINFO_TYPE Type)
{
    return DxgkpCollectAdapterDiagnosticInfo(Adapter, Type, TRUE);
}

static NTSTATUS
DxgkpSetVsyncInterruptState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_CRTC_VSYNC_STATE VsyncState)
{
    PDXGKDDI_CONTROLINTERRUPT3 ControlInterrupt3;
    PDXGKDDI_CONTROLINTERRUPT2 ControlInterrupt2;
    PDXGKDDI_CONTROL_INTERRUPT ControlInterrupt;
    DXGKARG_CONTROLINTERRUPT3 Args3;
    DXGKARG_CONTROLINTERRUPT2 Args2;
    ULONG Version;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return STATUS_INVALID_PARAMETER;

    Version = Adapter->MiniportContext->InitData.s.Version;
    ControlInterrupt3 = NULL;
    ControlInterrupt2 = NULL;
    ControlInterrupt = DXGK_CB(Adapter, DxgkDdiControlInterrupt);

    if (!Adapter->MiniportContext->UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_7))
    {
        ControlInterrupt3 =
            DXGK_CB_FULL(Adapter, DxgkDdiControlInterrupt3);
    }
    if (ControlInterrupt3 == NULL &&
        !Adapter->MiniportContext->UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_1_3))
    {
        ControlInterrupt2 =
            DXGK_CB_FULL(Adapter, DxgkDdiControlInterrupt2);
    }

    if (ControlInterrupt3 == NULL &&
        ControlInterrupt2 == NULL &&
        ControlInterrupt == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_DELETE_PENDING;

    _SEH2_TRY
    {
        if (ControlInterrupt3 != NULL)
        {
            RtlZeroMemory(&Args3, sizeof(Args3));
            Args3.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
            Args3.CrtcVsyncState = VsyncState;
            Args3.VidPnSourceId = 0;
            Status = ControlInterrupt3(
                         Adapter->MiniportDeviceContext,
                         &Args3);
        }
        else if (ControlInterrupt2 != NULL)
        {
            RtlZeroMemory(&Args2, sizeof(Args2));
            Args2.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
            Args2.CrtcVsyncState = VsyncState;
            Status = ControlInterrupt2(
                         Adapter->MiniportDeviceContext,
                         Args2);
        }
        else
        {
            Status = ControlInterrupt(
                         Adapter->MiniportDeviceContext,
                         DXGK_INTERRUPT_CRTC_VSYNC,
                         VsyncState == DXGK_VSYNC_ENABLE);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    DxgkReleaseMiniportCallback(Adapter);
    return Status;
}

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
    ULONG           StartGeneration;

    PAGED_CODE();

    AdapterStart100ns = DxgkpTraceNow100ns();

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);
    Status = DxgkpBeginAdapterStart(Adapter, &StartGeneration);
    if (!NT_SUCCESS(Status))
        return Status;
    Adapter->MiniportDeviceStopped = FALSE;
    Adapter->SurpriseRemovalHandled = FALSE;
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);

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

    /* Save interrupt resource info for connection immediately before
     * DxgkDdiStartDevice, which may require initialization completions. */
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

    /* Fill the callback table for the miniport.  DxgkpFillInterface zeros the
     * full current buffer before publishing the version-specific prefix, so
     * every unimplemented callback remains NULL. */
    DxgkpFillInterface(Adapter, &Interface);

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

    /* Connect the interrupt before StartDevice, but hold ISR/DPC admission
     * closed until the Level-3 StartDevice callback has returned. */
    DxgkAcquireLevel3Transition(Adapter);
    DxgkBeginKmdExclusive(Adapter);
    InterlockedExchange(&Adapter->VidSchStopping, 1);
    DxgkpWaitForVidSchCallbacks(Adapter);
    InterlockedExchange(&Adapter->VidSchStopping, 0);
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
        (VOID)DxgkpCollectAdapterDiagnosticInfo(
                  Adapter,
                  DXGK_DI_STARTDEVICE,
                  FALSE);
        InterlockedExchange(&Adapter->VidSchStopping, 1);
        DxgkpDisconnectAdapterInterrupt(Adapter);
        KeRemoveQueueDpc(&Adapter->DpcObject);
        KeFlushQueuedDpcs();
        DxgkpWaitForVidSchCallbacks(Adapter);
        /*
         * A miniport can acquire the firmware display and then reject the
         * descriptor or fail a later allocation before StartDevice returns.
         * Do not leave that failed adapter published as the POST owner: doing
         * so would make a fallback or a restart yield forever.
         */
        DxgkpClearPostDisplayOwner(Adapter);
        DxgkpReleasePostDisplayMapping(Adapter);
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
        DxgkEndKmdExclusive(Adapter, FALSE);
        DxgkReleaseLevel3Transition(Adapter);
        DxgkpCompleteAdapterStart(Adapter, StartGeneration, Status, TRUE);
        return Status;
    }

    DxgkpEnablePeriodicInterruptHandoff(Adapter);
    DxgkEndKmdExclusive(Adapter, TRUE);
    DxgkUnblockInterruptCallbacks(Adapter);
    DxgkReleaseLevel3Transition(Adapter);

    DXGKRNL_TRACE("DxgkAdapterStart: started — Sources=%lu Children=%lu\n",
                  Adapter->NumberOfVideoPresentSources,
                  Adapter->NumberOfChildren);

    /* Initialise the video memory manager for this adapter. */
    StepStart100ns = DxgkpTraceNow100ns();
    Status = DxgkVidMmInitializeAdapter(Adapter);
    VidMmUs = DxgkpTraceElapsedUs(StepStart100ns);
    if (!NT_SUCCESS(Status))
    {
        NTSTATUS StopStatus;

        DXGKRNL_ERR("DxgkAdapterStart: DxgkVidMmInitializeAdapter failed "
                    "0x%08lX\n", Status);
        InterlockedExchange(&Adapter->VidSchStopping, 1);
        DxgkpDisconnectAdapterInterrupt(Adapter);
        KeRemoveQueueDpc(&Adapter->DpcObject);
        KeFlushQueuedDpcs();
        DxgkpWaitForVidSchCallbacks(Adapter);
        StopStatus = DxgkpStopMiniportForTeardown(Adapter);
        if (NT_SUCCESS(StopStatus))
        {
            DxgkpClearPostDisplayOwner(Adapter);
            DxgkpReleasePostDisplayMapping(Adapter);
            Adapter->AllocatedResources = NULL;
            Adapter->TranslatedResources = NULL;
            DxgkpCompleteAdapterStart(Adapter, StartGeneration, Status, TRUE);
        }
        else
        {
            DXGKRNL_ERR("DxgkAdapterStart: rollback StopDevice failed 0x%08lX; retaining the non-restartable miniport state\n", StopStatus);
            Status = StopStatus;
            DxgkpCompleteAdapterStart(Adapter, StartGeneration, Status, FALSE);
        }
        return Status;
    }

    /* Cache surprise-removal support while hardware is still present.  The
     * topology fields apply only to full WDDM adapters. */
    Adapter->NodeCount = 0;
    Adapter->SupportSurpriseRemoval = FALSE;
    {
        PDXGK_DRIVERCAPS Caps;

        Caps = ExAllocatePoolWithTag(NonPagedPool,
                                     DXGKP_DRIVERCAPS_QUERY_SIZE,
                                     TAG_DXGK_ADAPTER);
        if (Caps != NULL)
        {
            if (NT_SUCCESS(DxgkpQueryDriverCaps(Adapter, Caps)))
            {
                if (DxgkCapsCoreInterfaceVersionAtLeast(
                        Adapter->MiniportContext->InitData.s.Version,
                        DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
                    Adapter->SupportSurpriseRemoval = Caps->SupportSurpriseRemoval;
                if (!Adapter->MiniportContext->IsDisplayOnlyDriver)
                {
                    Adapter->NodeCount = Caps->GpuEngineTopology.NbAsymetricProcessingNodes;
                    Adapter->HighestAcceptableAddress = Caps->HighestAcceptableAddress;
                    Adapter->SchedulingCaps.Value = Caps->SchedulingCaps.Value;
                    DXGKRNL_TRACE("DxgkAdapterStart: %lu GPU node(s) reported\n", Adapter->NodeCount);
                }
            }

            ExFreePoolWithTag(Caps, TAG_DXGK_ADAPTER);
        }
    }

    /* Cache the GPU MMU declaration while the miniport is callable. */
    Adapter->GpuMmuCapsValid = FALSE;
    Adapter->PageTableLevelsValid = FALSE;
    RtlZeroMemory(&Adapter->GpuMmuCaps, sizeof(Adapter->GpuMmuCaps));
    RtlZeroMemory(Adapter->PageTableLevels, sizeof(Adapter->PageTableLevels));
    if (DXGKP_GPUMMU_END_TO_END &&
        !Adapter->MiniportContext->IsDisplayOnlyDriver &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_0) &&
        NT_SUCCESS(DxgkpQueryGpuMmuCaps(Adapter, &Adapter->GpuMmuCaps)) &&
        Adapter->GpuMmuCaps.VirtualAddressBitCount != 0 &&
        Adapter->GpuMmuCaps.PageTableLevelCount != 0 &&
        DxgkpCacheGpuMmuGeometry(Adapter))
    {
        Adapter->GpuMmuCapsValid = TRUE;
        DXGKRNL_TRACE("DxgkAdapterStart: GpuMmu %u-bit, %u level(s), leaf %u entry bits\n",
                      Adapter->GpuMmuCaps.VirtualAddressBitCount,
                      Adapter->GpuMmuCaps.PageTableLevelCount,
                      Adapter->PageTableLevels[0].PageTableIndexBitCount);
    }

    {
        BOOLEAN ProviderStarted;
        NTSTATUS Mms2Status;

        ProviderStarted = FALSE;
        Mms2Status = DxgkpMms2StartAdministrativeAdapter(Adapter, &ProviderStarted);
        if (ProviderStarted)
            DxgkpMms2PublishStarted(Adapter);
        if (!NT_SUCCESS(Mms2Status))
        {
            NTSTATUS BeginStatus;
            NTSTATUS CompleteStatus;
            NTSTATUS StopStatus;

            DXGKRNL_ERR("DxgkAdapterStart: dxgmms2 start failed 0x%08lX\n", Mms2Status);
            BeginStatus = ProviderStarted ? DxgkpMms2BeginStop(Adapter, Dxgmms2StopReasonStartRollback) : STATUS_SUCCESS;
            InterlockedExchange(&Adapter->VidSchStopping, 1);
            DxgkpDisconnectAdapterInterrupt(Adapter);
            KeRemoveQueueDpc(&Adapter->DpcObject);
            KeFlushQueuedDpcs();
            DxgkpWaitForVidSchCallbacks(Adapter);
            StopStatus = DxgkpStopMiniportForTeardown(Adapter);
            if (NT_SUCCESS(StopStatus) && NT_SUCCESS(BeginStatus))
            {
                CompleteStatus = ProviderStarted ? DxgkpMms2CompleteRetiredStop(Adapter) : STATUS_SUCCESS;
                if (NT_SUCCESS(CompleteStatus))
                {
                    DxgkVidMmQuiesceAdapter(Adapter);
                    DxgkVidMmTeardownAdapter(Adapter);
                    DxgkpClearPostDisplayOwner(Adapter);
                    DxgkpReleasePostDisplayMapping(Adapter);
                    Adapter->AllocatedResources = NULL;
                    Adapter->TranslatedResources = NULL;
                    DxgkpCompleteAdapterStart(Adapter, StartGeneration, Mms2Status, TRUE);
                    return Mms2Status;
                }
                Mms2Status = CompleteStatus;
            }
            else if (!NT_SUCCESS(StopStatus))
            {
                Mms2Status = StopStatus;
            }
            else
            {
                Mms2Status = BeginStatus;
            }
            DXGKRNL_ERR("DxgkAdapterStart: dxgmms2 rollback incomplete 0x%08lX; retaining state for RemoveDevice\n", Mms2Status);
            DxgkpCompleteAdapterStart(Adapter, StartGeneration, Mms2Status, FALSE);
            return Mms2Status;
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
            (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
            Adapter->VidPn = (PVOID)hVidPn;
            KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
            DXGKRNL_TRACE("DxgkAdapterStart: VidPN created %p\n", hVidPn);
        }
        else
        {
            DXGKRNL_ERR("DxgkAdapterStart: DxgkVidPnCreateForAdapter failed "
                        "0x%08lX — continuing without VidPN\n", Status);
            /* Non-fatal: adapter can still start; VidPN calls will fail gracefully. */
            (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
            Adapter->VidPn = NULL;
            KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
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
        Adapter->PresentQueueInitializationStatus = PresentStatus;
        PresentUs = DxgkpTraceElapsedUs(StepStart100ns);
        if (!NT_SUCCESS(PresentStatus))
        {
            DXGKRNL_ERR("DxgkAdapterStart: DxgkPresentInit failed "
                        "0x%08lX — continuing without present queues\n",
                        PresentStatus);
            /* Non-fatal: the timer-based present in display.c provides a fallback. */
        }
    }

    DxgkReinitializeAdapterRundown(Adapter);
    InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
    InterlockedExchange(&Adapter->VidSchStopping, 0);
    DxgkPresentResume(Adapter);

    /* Watch for stuck submissions (documented TDR recovery). */
    DxgkpStartTdrWatchdog(Adapter);

    /*
     * Ask the miniport to deliver vsync notifications. WDDM 2.7 uses the
     * per-source ControlInterrupt3 contract when supplied; earlier full
     * miniports use ControlInterrupt2, with the legacy callback retained for
     * pre-1.3 and DOD tables.
     */
    {
        NTSTATUS VsyncStatus;

        VsyncStatus =
            DxgkpSetVsyncInterruptState(Adapter, DXGK_VSYNC_ENABLE);
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

    Status = STATUS_SUCCESS;
    DxgkpCompleteAdapterStart(Adapter, StartGeneration, Status, TRUE);
    return Status;
}

/*
 * DxgkAdapterStop
 *
 * Called from DxgkpMiniportPnpDispatch in response to IRP_MN_STOP_DEVICE.
 * Tears down the video memory manager and calls DxgkDdiStopDevice.
 *
 * IRQL: PASSIVE_LEVEL
 */
static NTSTATUS
DxgkpStopMiniportForTeardown(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    NTSTATUS Status;

    if (Adapter->MiniportDeviceStopped)
        return STATUS_SUCCESS;
    if (Adapter->MiniportContext->InitData.s.DxgkDdiStopDevice == NULL)
        return STATUS_NOT_SUPPORTED;
    DxgkBeginKmdExclusive(Adapter);
    DxgkVidMmQuiesceAdapter(Adapter);
    Status = DxgkVidMmPrepareForIdle(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DxgkEndKmdExclusive(Adapter, FALSE);
        return Status;
    }
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        DxgkEndKmdExclusive(Adapter, FALSE);
        return STATUS_DELETE_PENDING;
    }

    _SEH2_TRY
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiStopDevice(Adapter->MiniportDeviceContext);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (NT_SUCCESS(Status))
    {
        Adapter->MiniportDeviceStopped = TRUE;
        InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    }
    else
    {
        InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
    }
    DxgkReleaseMiniportCallback(Adapter);
    DxgkEndKmdExclusive(Adapter, FALSE);
    return Status;
}

static NTSTATUS
DxgkpWaitForTrackedDmaIdle(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG TimeoutMs)
{
    LARGE_INTEGER Delay;
    ULONG ElapsedMs = 0;

    Delay.QuadPart = -(LONGLONG)(10 * 10 * 1000);
    for (;;)
    {
        BOOLEAN Outstanding;
        KIRQL OldIrql;

        DxgkRetireCompletedDmaBuffers(Adapter);
        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        Outstanding = !IsListEmpty(&Adapter->SubmitDmaListHead) || !IsListEmpty(&Adapter->SubmitDmaRetireListHead) || InterlockedCompareExchange(&Adapter->SubmitDmaRetireActiveWorkers, 0, 0) != 0;
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        if (!Outstanding)
            return STATUS_SUCCESS;
        if (ElapsedMs >= TimeoutMs)
            return STATUS_TIMEOUT;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        ElapsedMs += 10;
    }
}

static NTSTATUS
DxgkpResetMiniportForTeardown(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN SchedulerPrepared = FALSE;
    NTSTATUS SchedulerStatus;
    NTSTATUS Status;

    SchedulerStatus = VidSchPrepareAdapterReset(Adapter);
    if (NT_SUCCESS(SchedulerStatus))
        SchedulerPrepared = TRUE;
    else if (SchedulerStatus != STATUS_NOT_SUPPORTED)
        return SchedulerStatus;

    if (DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout) == NULL)
    {
        if (SchedulerPrepared)
            VidSchCompleteAdapterReset(Adapter, FALSE);
        return STATUS_NOT_SUPPORTED;
    }
    DxgkBeginKmdExclusive(Adapter);
    DxgkVidMmQuiesceAdapter(Adapter);
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        if (SchedulerPrepared)
            VidSchCompleteAdapterReset(Adapter, FALSE);
        DxgkEndKmdExclusive(Adapter, FALSE);
        return STATUS_DELETE_PENDING;
    }

    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout)(Adapter->MiniportDeviceContext);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseMiniportCallback(Adapter);
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckTdrFailure(Adapter, Status);
    Status = DxgkVidMmRecoverFromTimeout(Adapter);
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckTdrFailure(Adapter, Status);
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    if (SchedulerPrepared)
        VidSchCompleteAdapterReset(Adapter, TRUE);
    DxgkEndKmdExclusive(Adapter, FALSE);
    return Status;
}

BOOLEAN
DxgkAcquireMiniportCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return FALSE;
    if (!DxgkAcquireKmdCall(Adapter))
        return FALSE;
    (VOID)KeWaitForSingleObject(&Adapter->MiniportCallbackMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
    {
        KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
        DxgkReleaseKmdCall(Adapter);
        return FALSE;
    }
    return TRUE;
}

BOOLEAN
DxgkAcquireMiniportCallbackFromReservedKmdCall(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return FALSE;
    ASSERT(InterlockedCompareExchange(&Adapter->KmdActiveCalls, 0, 0) > 0);
    (VOID)KeWaitForSingleObject(&Adapter->MiniportCallbackMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
    {
        KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
        DxgkReleaseKmdCall(Adapter);
        return FALSE;
    }
    return TRUE;
}

VOID
DxgkReleaseMiniportCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ASSERT(Adapter != NULL);
    KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
    DxgkReleaseKmdCall(Adapter);
}

BOOLEAN
DxgkAcquireKmdCall(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVOID CurrentThread;

    if (Adapter == NULL)
        return FALSE;
    CurrentThread = PsGetCurrentThread();
    if (InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) != 0 && Adapter->KmdExclusiveOwnerThread != CurrentThread && Adapter->KmdTransactionOwnerThread != CurrentThread)
        return FALSE;
    InterlockedIncrement(&Adapter->KmdActiveCalls);
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) != 0 && Adapter->KmdExclusiveOwnerThread != CurrentThread && Adapter->KmdTransactionOwnerThread != CurrentThread)
    {
        DxgkReleaseKmdCall(Adapter);
        return FALSE;
    }
    return TRUE;
}

VOID
DxgkReleaseKmdCall(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG Remaining;

    ASSERT(Adapter != NULL);
    Remaining = InterlockedDecrement(&Adapter->KmdActiveCalls);
    ASSERT(Remaining >= 0);
}

VOID
DxgkAcquireLevel3Transition(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVOID CurrentThread;

    PAGED_CODE();
    ASSERT(Adapter != NULL);
    CurrentThread = PsGetCurrentThread();
    if (Adapter->Level3TransitionOwnerThread == CurrentThread)
    {
        ASSERT(InterlockedCompareExchange(&Adapter->Level3TransitionDepth, 0, 0) > 0);
        InterlockedIncrement(&Adapter->Level3TransitionDepth);
        return;
    }
    (VOID)KeWaitForSingleObject(&Adapter->Level3TransitionMutex, Executive, KernelMode, FALSE, NULL);
    ASSERT(Adapter->Level3TransitionOwnerThread == NULL);
    ASSERT(InterlockedCompareExchange(&Adapter->Level3TransitionDepth, 0, 0) == 0);
    InterlockedExchange(&Adapter->Level3TransitionDepth, 1);
    KeMemoryBarrier();
    Adapter->Level3TransitionOwnerThread = CurrentThread;
    KeMemoryBarrier();
}

VOID
DxgkReleaseLevel3Transition(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG Depth;

    PAGED_CODE();
    ASSERT(Adapter != NULL);
    ASSERT(Adapter->Level3TransitionOwnerThread == PsGetCurrentThread());
    Depth = InterlockedDecrement(&Adapter->Level3TransitionDepth);
    ASSERT(Depth >= 0);
    if (Depth != 0)
        return;
    Adapter->Level3TransitionOwnerThread = NULL;
    KeMemoryBarrier();
    KeReleaseMutex(&Adapter->Level3TransitionMutex, FALSE);
}

VOID
DxgkBeginKmdExclusive(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Delay;

    PAGED_CODE();
    ASSERT(Adapter != NULL);
    (VOID)KeWaitForSingleObject(&Adapter->KmdExclusiveMutex, Executive, KernelMode, FALSE, NULL);
    InterlockedExchange(&Adapter->KmdCallsBlocked, 1);
    KeMemoryBarrier();
    Delay.QuadPart = -(LONGLONG)(10 * 1000);
    while (InterlockedCompareExchange(&Adapter->KmdActiveCalls, 0, 0) != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    Adapter->KmdExclusiveOwnerThread = PsGetCurrentThread();
    KeMemoryBarrier();
}

VOID
DxgkEndKmdExclusive(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN ReopenAdmission)
{
    PAGED_CODE();
    ASSERT(Adapter != NULL);
    ASSERT(Adapter->KmdExclusiveOwnerThread == PsGetCurrentThread());
    Adapter->KmdExclusiveOwnerThread = NULL;
    KeMemoryBarrier();
    if (ReopenAdmission)
        InterlockedExchange(&Adapter->KmdCallsBlocked, 0);
    KeReleaseMutex(&Adapter->KmdExclusiveMutex, FALSE);
    if (ReopenAdmission)
        DxgkVidMmKickDeferredDestroyBatches(Adapter);
}

static NTSTATUS
DxgkpAdapterStopInternal(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN ReleasePostDisplayOwnership,
    _In_ DXGMMS2_STOP_REASON StopReason)
{
    NTSTATUS Status;
    NTSTATUS SchedulerIdleStatus;
    NTSTATUS TrackerIdleStatus;
    NTSTATUS VsyncStatus;
    PDEVICE_OBJECT FunctionalDeviceObject;
    BOOLEAN SchedulerAlreadyPrepared;
    BOOLEAN StopDeviceEstablishedBoundary = FALSE;
    BOOLEAN StopChangedState = FALSE;
    ULONG StopGeneration = 0;

    PAGED_CODE();

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);

    DXGKRNL_TRACE("DxgkAdapterStop: Adapter %p\n", Adapter);

    FunctionalDeviceObject = Adapter->FunctionalDeviceObject;
    if (FunctionalDeviceObject != NULL)
        ObReferenceObject(FunctionalDeviceObject);

    DxgkpAcquireAdapterMutexAfterStart(Adapter);
    if (InterlockedCompareExchange(&Adapter->AdapterStopInProgress, 0, 0) != 0)
    {
        StopGeneration = Adapter->AdapterStopGeneration;
        ASSERT(StopGeneration != 0);
        Adapter->AdapterStopIntentCount++;
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        for (;;)
        {
            KeWaitForSingleObject(&Adapter->AdapterStopCompletedEvent, Executive, KernelMode, FALSE, NULL);
            (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
            if (Adapter->AdapterStopCompletedGeneration == StopGeneration)
            {
                Status = Adapter->AdapterStopStatus;
                ASSERT(Adapter->AdapterStopIntentCount > 0);
                Adapter->AdapterStopIntentCount--;
                KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
                break;
            }
            KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        }
        if (FunctionalDeviceObject != NULL)
            ObDereferenceObject(FunctionalDeviceObject);
        return Status;
    }
    if (Adapter->AdapterStopIntentCount != 0)
    {
        ASSERT(Adapter->AdapterStopCompletedGeneration == Adapter->AdapterStopGeneration);
        Status = Adapter->AdapterStopStatus;
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        if (FunctionalDeviceObject != NULL)
            ObDereferenceObject(FunctionalDeviceObject);
        return Status;
    }
    if (Adapter->State == DxgkAdapterStateStarted || Adapter->State == DxgkAdapterStateStopping)
    {
        StopGeneration = Adapter->AdapterStopGeneration + 1;
        if (StopGeneration == 0)
            StopGeneration = 1;
        Adapter->AdapterStopGeneration = StopGeneration;
        Adapter->AdapterStopStatus = STATUS_PENDING;
        Adapter->AdapterStopIntentCount++;
        KeResetEvent(&Adapter->AdapterStopCompletedEvent);
        InterlockedExchange(&Adapter->AdapterStopInProgress, 1);
        if (Adapter->State == DxgkAdapterStateStarted)
        {
            Adapter->State = DxgkAdapterStateStopping;
            StopChangedState = TRUE;
        }
    }
    else
    {
        DXGKRNL_ADAPTER_STATE State = Adapter->State;

        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        DXGKRNL_WARN("DxgkAdapterStop: adapter not started (State=%d)\n", State);
        if (FunctionalDeviceObject != NULL)
            ObDereferenceObject(FunctionalDeviceObject);
        return STATUS_SUCCESS;
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    Status = DxgkpMms2BeginStop(Adapter, StopReason);
    if (!NT_SUCCESS(Status))
    {
        (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
        if (StopChangedState && Adapter->State == DxgkAdapterStateStopping)
            Adapter->State = DxgkAdapterStateStarted;
        Adapter->AdapterStopStatus = Status;
        Adapter->AdapterStopCompletedGeneration = StopGeneration;
        InterlockedExchange(&Adapter->AdapterStopInProgress, 0);
        ASSERT(Adapter->AdapterStopIntentCount > 0);
        Adapter->AdapterStopIntentCount--;
        KeSetEvent(&Adapter->AdapterStopCompletedEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        if (FunctionalDeviceObject != NULL)
            ObDereferenceObject(FunctionalDeviceObject);
        return Status;
    }

    VsyncStatus =
        DxgkpSetVsyncInterruptState(
            Adapter,
            DXGK_VSYNC_DISABLE_NO_PHASE);
    if (!NT_SUCCESS(VsyncStatus) &&
        VsyncStatus != STATUS_NOT_SUPPORTED)
    {
        DXGKRNL_WARN("DxgkAdapterStop: CRTC_VSYNC disable failed "
                     "0x%08lX\n",
                     VsyncStatus);
    }

    InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
    DxgkPresentBeginStop(Adapter);
    DxgkMarkAdapterDevicesStopped(Adapter);
    DxgkBeginAdapterRundown(Adapter);
    SchedulerAlreadyPrepared = (InterlockedCompareExchange(&Adapter->VidSchStopping, 0, 0) != 0);

    /* Stop the TDR watchdog before the miniport goes away. */
    DxgkpStopTdrWatchdog(Adapter);
    DxgkpWaitForFlagClear(&Adapter->HotPlugWorkActive);
    ASSERT(DxgkHotPlugWorkCoreCanAcquireLevel3AfterRundown(&Adapter->HotPlugWorkActive));
    DxgkAcquireLevel3Transition(Adapter);

    /* Present work and already-admitted reservations drain while completion
     * DPCs remain active, so accepted GPU work can finish normally. */
    DxgkPresentTeardown(Adapter);
    DxgkWaitForSubmitDmaReservations(Adapter);

    if (!SchedulerAlreadyPrepared)
    {
        SchedulerIdleStatus = VidSchBeginStopDrain(Adapter);
        if (NT_SUCCESS(SchedulerIdleStatus))
            SchedulerIdleStatus = VidSchWaitForIdle(Adapter, 1000);
        TrackerIdleStatus = DxgkpWaitForTrackedDmaIdle(Adapter, 1000);
        if ((!NT_SUCCESS(SchedulerIdleStatus) && SchedulerIdleStatus != STATUS_NOT_SUPPORTED) || !NT_SUCCESS(TrackerIdleStatus))
        {
            Status = DxgkpResetMiniportForTeardown(Adapter);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_ERR("DxgkAdapterStop: GPU could not be drained or reset (scheduler=0x%08lX tracker=0x%08lX reset=0x%08lX); retaining all owned storage\n", SchedulerIdleStatus, TrackerIdleStatus, Status);
                goto CompleteStop;
            }
        }
    }

    /* Completion or ResetFromTimeout is the DMA ownership boundary. Close
     * every late ISR/DPC path before releasing tracker or scheduler state. */
    VidSchPrepareForStop(Adapter);
    DxgkpDisablePeriodicInterruptHandoff(Adapter);
    KeRemoveQueueDpc(&Adapter->DpcObject);
    KeFlushQueuedDpcs();
    if (InterlockedCompareExchange(&Adapter->TdrOwnershipUncertain, 0, 0) != 0)
    {
        Status = DxgkpStopMiniportForTeardown(Adapter);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkAdapterStop: StopDevice could not establish the DMA ownership boundary 0x%08lX; retaining all owned storage\n", Status);
            goto CompleteStop;
        }
        Adapter->MiniportDeviceStopped = TRUE;
        StopDeviceEstablishedBoundary = TRUE;
        InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    }
    if (StopDeviceEstablishedBoundary)
    {
        VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);
        DxgkVidMmQuiesceAdapter(Adapter);
    }
    DxgkReleaseTrackedDmaBuffers(Adapter, !StopDeviceEstablishedBoundary);
    DxgkWaitForDeviceLifecycleOperations(Adapter);

    /* No scheduler/VidMm storage can disappear while a generic adapter,
     * device, context, present, or tracker producer still owns it. */
    Status = DxgkCleanupAdapterDevices(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: device cleanup failed 0x%08lX; retaining devices for RemoveDevice\n", Status);
        goto CompleteStop;
    }
    DxgkWaitForAdapterRundown(Adapter);
    if (!StopDeviceEstablishedBoundary)
        DxgkVidMmQuiesceAdapter(Adapter);

    if (ReleasePostDisplayOwnership && !Adapter->MiniportDeviceStopped)
    {
        PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP PfnRelease = DXGK_CB(Adapter, DxgkDdiStopDeviceAndReleasePostDisplayOwnership);

        if (PfnRelease != NULL)
        {
            DXGK_DISPLAY_INFORMATION ReleasedInfo;

            RtlZeroMemory(&ReleasedInfo, sizeof(ReleasedInfo));
            DxgkBeginKmdExclusive(Adapter);
            if (DxgkAcquireMiniportCallback(Adapter))
            {
                _SEH2_TRY
                {
                    Status = PfnRelease(Adapter->MiniportDeviceContext, 0, &ReleasedInfo);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (NT_SUCCESS(Status))
                    Adapter->MiniportDeviceStopped = TRUE;
                else
                    DXGKRNL_WARN("DxgkpStopPostDisplayOwner: StopDeviceAndReleasePostDisplayOwnership failed 0x%08lX\n", Status);
                DxgkReleaseMiniportCallback(Adapter);
            }
            DxgkEndKmdExclusive(Adapter, FALSE);
        }
    }

    Status = DxgkpStopMiniportForTeardown(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: DxgkDdiStopDevice remained failed 0x%08lX; retaining scheduler/VidMm state for RemoveDevice\n", Status);
        goto CompleteStop;
    }

    VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);
    DxgkpDisconnectAdapterInterrupt(Adapter);
    Status = DxgkCleanupAdapterDevices(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: context-stream cleanup failed 0x%08lX; retaining scheduler/VidMm state for retry\n", Status);
        goto CompleteStop;
    }
    Status = DxgkpMms2CompleteRetiredStop(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: dxgmms2 completion failed 0x%08lX; retaining scheduler/VidMm state for retry\n", Status);
        goto CompleteStop;
    }
    VidSchDestroy(Adapter);

    /* Unregister the display device from win32ss. */
    DxgkDisplayUnregister();

    /* Tear down the VidPN. */
    {
        D3DKMDT_HVIDPN hVidPn;

        (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
        hVidPn = (D3DKMDT_HVIDPN)Adapter->VidPn;
        Adapter->VidPn = NULL;
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        if (hVidPn != NULL)
            DxgkVidPnDestroy(hVidPn);
    }

    /* Tracker retirement can reference both objects, so destroy them last. */
    DxgkDestroySharedPrimary(Adapter);
    DxgkVidMmTeardownAdapter(Adapter);

    DxgkpClearPostDisplayOwner(Adapter);

    Adapter->AllocatedResources  = NULL;
    Adapter->TranslatedResources = NULL;
    Adapter->InterruptTraceEpoch100ns = 0;
    DxgkpReleasePostDisplayMapping(Adapter);

    DXGKRNL_TRACE("DxgkAdapterStop: stopped\n");

CompleteStop:
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    ASSERT(Adapter->AdapterStopGeneration == StopGeneration);
    ASSERT(Adapter->AdapterStopIntentCount > 0);
    if (NT_SUCCESS(Status) && Adapter->State == DxgkAdapterStateStopping)
        Adapter->State = DxgkAdapterStateStopped;
    Adapter->AdapterStopStatus = Status;
    Adapter->AdapterStopCompletedGeneration = StopGeneration;
    InterlockedExchange(&Adapter->AdapterStopInProgress, 0);
    Adapter->AdapterStopIntentCount--;
    KeSetEvent(&Adapter->AdapterStopCompletedEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    DxgkReleaseLevel3Transition(Adapter);
    if (FunctionalDeviceObject != NULL)
        ObDereferenceObject(FunctionalDeviceObject);
    return Status;
}

NTSTATUS
DxgkAdapterStop(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    return DxgkpAdapterStopInternal(Adapter, FALSE, Dxgmms2StopReasonPnpStop);
}

/*
 * DxgkAdapterRemove
 *
 * Called from DxgkpMiniportPnpDispatch in response to IRP_MN_REMOVE_DEVICE
 * (or IRP_MN_SURPRISE_REMOVAL + IRP_MN_REMOVE_DEVICE).  Stops the adapter
 * if still started, calls DxgkDdiRemoveDevice, disconnects the interrupt,
 * frees descriptor arrays, and unlinks from the per-miniport and global lists.
 * The PnP dispatch routine forwards the remove IRP before it performs the final
 * detach and FDO deletion.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkAdapterRemove(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;
    NTSTATUS StopStatus = STATUS_SUCCESS;
    PVOID MiniportDeviceContext;
    NTSTATUS CleanupStatus;
    BOOLEAN MiniportCleanupBeforeRemove = FALSE;
    BOOLEAN MiniportCleanupCallbacksPermitted = FALSE;

    PAGED_CODE();

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);

    DXGKRNL_TRACE("DxgkAdapterRemove: Adapter %p State=%d\n",
                  Adapter, Adapter->State);

    /* A remove cannot close admission or callbacks underneath StartDevice. */
    DxgkpAcquireAdapterMutexAfterStart(Adapter);
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    if (InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 1, 0) == 0)
        ExWaitForRundownProtectionRelease(&Adapter->RemoveRundownRef);

    /* Stop the adapter if it is still running. */
    InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
    DxgkPresentBeginStop(Adapter);
    DxgkMarkAdapterDevicesStopped(Adapter);
    DxgkBeginAdapterRundown(Adapter);
    if (InterlockedCompareExchange(&Adapter->AdapterStopInProgress, 0, 0) != 0 || Adapter->State == DxgkAdapterStateStarting || Adapter->State == DxgkAdapterStateStarted || Adapter->State == DxgkAdapterStateStopping)
    {
        StopStatus = DxgkpAdapterStopInternal(Adapter, FALSE, Dxgmms2StopReasonRemove);
        MiniportCleanupBeforeRemove = NT_SUCCESS(StopStatus);
    }
    else if (Adapter->State == DxgkAdapterStateStopped)
    {
        MiniportCleanupBeforeRemove = TRUE;
    }
    else if (Adapter->State == DxgkAdapterStateSurpriseRemoved)
    {
        DxgkpStopTdrWatchdog(Adapter);
        DxgkPresentTeardown(Adapter);
        VidSchPrepareForStop(Adapter);
        DxgkpDisablePeriodicInterruptHandoff(Adapter);
        KeRemoveQueueDpc(&Adapter->DpcObject);
        KeFlushQueuedDpcs();
        DxgkWaitForSubmitDmaReservations(Adapter);
        MiniportCleanupBeforeRemove = Adapter->SurpriseRemovalHandled || Adapter->MiniportDeviceStopped;
        MiniportCleanupCallbacksPermitted = Adapter->SurpriseRemovalHandled;
    }
    if (!NT_SUCCESS(StopStatus))
        DXGKRNL_ERR("DxgkAdapterRemove: StopDevice remained failed 0x%08lX; deferring DMA release until RemoveDevice\n", StopStatus);

    /* Close every scheduler/interrupt producer before device or VidMm
     * cleanup. Failed StopDevice paths have not necessarily done this yet. */
    VidSchPrepareForStop(Adapter);
    DxgkpDisconnectAdapterInterrupt(Adapter);
    KeRemoveQueueDpc(&Adapter->DpcObject);
    KeFlushQueuedDpcs();
    DxgkpWaitForVidSchCallbacks(Adapter);
    /* Keep public KMD admission closed through software cleanup and the final
     * RemoveDevice callback.  The exclusive owner may still issue the cleanup
     * DDIs that a successful surprise-removal notification permits. */
    DxgkBeginKmdExclusive(Adapter);
    if (MiniportCleanupBeforeRemove)
    {
        VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);
        DxgkReleaseTrackedDmaBuffers(Adapter, MiniportCleanupCallbacksPermitted);
        DxgkWaitForDeviceLifecycleOperations(Adapter);
        CleanupStatus = DxgkCleanupAdapterDevices(Adapter);
        if (NT_SUCCESS(CleanupStatus))
        {
            DxgkWaitForAdapterRundown(Adapter);
            DxgkVidMmQuiesceAdapter(Adapter);
        }
        else
        {
            DXGKRNL_ERR("DxgkAdapterRemove: pre-RemoveDevice cleanup failed 0x%08lX; deferring retained devices to the final boundary\n", CleanupStatus);
            MiniportCleanupBeforeRemove = FALSE;
        }
    }

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

    /* Stop display dispatch and its present worker while callbacks remain
     * valid, then prevent bugcheck-time display callbacks from retaining the
     * adapter past the final miniport removal boundary. */
    DxgkDisplayUnregister();
    DxgkpClearPostDisplayOwner(Adapter);

    /* Close the callback gate and detach the opaque context before invoking
     * the final DDI. All subsequent cleanup is OS bookkeeping only. */
    (VOID)KeWaitForSingleObject(&Adapter->MiniportCallbackMutex, Executive, KernelMode, FALSE, NULL);
    MiniportDeviceContext = Adapter->MiniportDeviceContext;
    InterlockedExchange(&Adapter->MiniportCallbacksValid, 0);
    Adapter->MiniportDeviceContext = NULL;
    if (MiniportDeviceContext != NULL && Adapter->MiniportContext->InitData.s.DxgkDdiRemoveDevice != NULL)
    {
        NTSTATUS Status = DxgkpRemoveMiniportDevice(Adapter, MiniportDeviceContext);
        if (!NT_SUCCESS(Status))
            DXGKRNL_ERR("DxgkAdapterRemove: DxgkDdiRemoveDevice failed 0x%08lX (continuing)\n", Status);
    }
    if (InterlockedCompareExchange(&Adapter->ReverseCallbackRundownStarted, 1, 0) == 0)
        ExWaitForRundownProtectionRelease(&Adapter->ReverseCallbackRundownRef);
    KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    DxgkpReleaseCallbackMemory(Adapter);
#endif
    DxgkEndKmdExclusive(Adapter, FALSE);

    /* RemoveDevice is the final hardware-ownership boundary when StopDevice
     * could not complete. Only now may OS tracking force-release storage. */
    Adapter->MiniportDeviceStopped = TRUE;
    Adapter->MiniportRemoveDeviceComplete = TRUE;
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);
    if (!MiniportCleanupBeforeRemove)
    {
        DxgkReleaseTrackedDmaBuffers(Adapter, FALSE);
        DxgkWaitForDeviceLifecycleOperations(Adapter);
        DxgkVidMmQuiesceAdapter(Adapter);
        CleanupStatus = DxgkCleanupAdapterDevices(Adapter);
        if (!NT_SUCCESS(CleanupStatus))
            DxgkpBugCheckMms2Lifecycle(Adapter, CleanupStatus, DXGKP_MMS2_FAILURE_FINAL_RETIREMENT);
        DxgkWaitForAdapterRundown(Adapter);
    }

    {
        DXGMMS2_STOP_REASON FinalReason;
        NTSTATUS Mms2Status;

        FinalReason = Adapter->State == DxgkAdapterStateSurpriseRemoved ? Dxgmms2StopReasonSurpriseRemove : Dxgmms2StopReasonRemove;
        Mms2Status = DxgkpMms2BeginStop(Adapter, FinalReason);
        if (NT_SUCCESS(Mms2Status))
            Mms2Status = DxgkpMms2CompleteRetiredStop(Adapter);
        if (!NT_SUCCESS(Mms2Status))
            DxgkpBugCheckMms2Lifecycle(Adapter, Mms2Status, DXGKP_MMS2_FAILURE_FINAL_RETIREMENT);
    }

    VidSchDestroy(Adapter);
    {
        D3DKMDT_HVIDPN hVidPn;

        (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
        hVidPn = (D3DKMDT_HVIDPN)Adapter->VidPn;
        Adapter->VidPn = NULL;
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        if (hVidPn != NULL)
            DxgkVidPnDestroy(hVidPn);
    }
    DxgkDestroySharedPrimary(Adapter);
    DxgkVidMmTeardownAdapter(Adapter);
    (VOID)DxgkCleanupAdapterDevices(Adapter);
    Adapter->AllocatedResources = NULL;
    Adapter->TranslatedResources = NULL;
    DxgkpReleasePostDisplayMapping(Adapter);

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

    {
        NTSTATUS Mms2Status;

        Mms2Status = DxgkpMms2DestroyAdministrativeAdapter(Adapter);
        if (!NT_SUCCESS(Mms2Status))
            DxgkpBugCheckMms2Lifecycle(Adapter, Mms2Status, DXGKP_MMS2_FAILURE_FINAL_DESTROY);
    }

    /* Unlink from per-miniport adapter list. */
    KeAcquireSpinLock(&Adapter->MiniportContext->AdapterListLock, &OldIrql);
    RemoveEntryList(&Adapter->MiniportAdapterListEntry);
    InitializeListHead(&Adapter->MiniportAdapterListEntry);
    KeReleaseSpinLock(&Adapter->MiniportContext->AdapterListLock, OldIrql);

    /* Unlink from global adapter list. */
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);
    RemoveEntryList(&Adapter->GlobalAdapterListEntry);
    InitializeListHead(&Adapter->GlobalAdapterListEntry);
    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);

    Adapter->State = DxgkAdapterStateRemoved;
    DXGKRNL_TRACE("DxgkAdapterRemove: teardown complete\n");
}

static VOID
DxgkpDeleteRemovedAdapterFdo(_In_ PDXGKRNL_ADAPTER Adapter)
{
    PDEVICE_OBJECT FunctionalDeviceObject;
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    KIRQL OldIrql;

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);
    MpCtx = Adapter->MiniportContext;
    FunctionalDeviceObject = Adapter->FunctionalDeviceObject;
    if (Adapter->LowerDeviceObject != NULL)
    {
        IoDetachDevice(Adapter->LowerDeviceObject);
        Adapter->LowerDeviceObject = NULL;
    }
    DXGKRNL_TRACE("DxgkpDeleteRemovedAdapterFdo: deleting FDO %p\n", FunctionalDeviceObject);
    IoDeleteDevice(FunctionalDeviceObject);
    KeAcquireSpinLock(&MpCtx->AdapterListLock, &OldIrql);
    DxgkRosAssert(MpCtx->AdapterCount > 0, DXGKRNL_BUGCHECK_BAD_DEVICE_EXT);
    MpCtx->AdapterCount--;
    KeReleaseSpinLock(&MpCtx->AdapterListLock, OldIrql);
}

/* A native Windows dump rooted at dxgkrnl!DpiFdoHandleSurpriseRemoval uses
 * VIDEO_DXGKRNL_FATAL_ERROR (0x113), subtype 0x19.  ReactOS has no dxgkrnl
 * graceful-reboot handoff, so this is the closest non-silent containment for
 * the documented "no more miniport DDIs and reboot" failure contract. */
static DECLSPEC_NORETURN VOID
DxgkpBugCheckSurpriseRemoval(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ NTSTATUS FailureStatus,
    _In_opt_ PDXGKDDI_NOTIFY_SURPRISE_REMOVAL NotifyCallback)
{
    InterlockedExchange(&Adapter->KmdCallsBlocked, 1);
    InterlockedExchange(&Adapter->InterruptCallbacksBlocked, 1);
    InterlockedExchange(&Adapter->MiniportCallbacksValid, 0);
    KeMemoryBarrier();
    DXGKRNL_ERR("DxgkpBugCheckSurpriseRemoval: adapter %p cannot contain surprise removal, status 0x%08lX callback %p\n", Adapter, FailureStatus, (PVOID)NotifyCallback);
    KeBugCheckEx(DXGKP_BUGCHECK_VIDEO_DXGKRNL_FATAL_ERROR, (ULONG_PTR)DXGKP_FATAL_SURPRISE_REMOVAL_SUBTYPE, (ULONG_PTR)FailureStatus, (ULONG_PTR)Adapter, (ULONG_PTR)NotifyCallback);
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
            Status = DxgkAdapterStop(Adapter);
            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            return DxgkpForwardIrp(Adapter, Irp);
        }

        case IRP_MN_REMOVE_DEVICE:
        {
            PDEVICE_OBJECT LowerDevice = Adapter->LowerDeviceObject;

            if (LowerDevice != NULL)
                ObReferenceObject(LowerDevice);

            /* Release miniport and OS-owned resources while the lower stack
             * still owns live hardware, then forward before detach/delete. */
            DxgkAdapterRemove(Adapter);

            if (LowerDevice != NULL)
            {
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoSkipCurrentIrpStackLocation(Irp);
                Status = IoCallDriver(LowerDevice, Irp);
                DxgkpDeleteRemovedAdapterFdo(Adapter);
                ObDereferenceObject(LowerDevice);
                return Status;
            }

            DxgkpDeleteRemovedAdapterFdo(Adapter);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }

        case IRP_MN_SURPRISE_REMOVAL:
        {
            PDXGKDDI_NOTIFY_SURPRISE_REMOVAL NotifyCallback;
            DXGKRNL_ADAPTER_STATE PreviousState;
            BOOLEAN NotifyRunningRemoval;
            BOOLEAN WaitForStop;
            NTSTATUS NotifyStatus;
            NTSTATUS StopStatus = STATUS_SUCCESS;

            DxgkpAcquireAdapterMutexAfterStart(Adapter);
            PreviousState = Adapter->State;
            WaitForStop = (InterlockedCompareExchange(&Adapter->AdapterStopInProgress, 0, 0) != 0);
            NotifyRunningRemoval = PreviousState == DxgkAdapterStateStarted || PreviousState == DxgkAdapterStateStopping || WaitForStop;
            Adapter->State = DxgkAdapterStateSurpriseRemoved;
            DxgkMarkAdapterDevicesStoppedLocked(Adapter);
            KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

            /* Level-0: notify immediately and deliberately bypass ordinary
             * KMD admission so this can run while another DDI is pending. */
            NotifyCallback = DXGK_CB(Adapter, DxgkDdiNotifySurpriseRemoval);
            if (NotifyRunningRemoval)
            {
                if (!Adapter->SupportSurpriseRemoval)
                    DxgkpBugCheckSurpriseRemoval(Adapter, STATUS_NOT_SUPPORTED, NotifyCallback);
                if (NotifyCallback == NULL || Adapter->MiniportDeviceContext == NULL || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
                    DxgkpBugCheckSurpriseRemoval(Adapter, STATUS_PROCEDURE_NOT_FOUND, NotifyCallback);
                _SEH2_TRY
                {
                    NotifyStatus = NotifyCallback(Adapter->MiniportDeviceContext, DxgkRemovalPnPNotify);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    NotifyStatus = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(NotifyStatus))
                    DxgkpBugCheckSurpriseRemoval(Adapter, NotifyStatus, NotifyCallback);
                Adapter->SurpriseRemovalHandled = TRUE;
            }

            if (!WaitForStop)
            {
                NTSTATUS Mms2Status;

                Mms2Status = DxgkpMms2BeginStop(Adapter, Dxgmms2StopReasonSurpriseRemove);
                if (!NT_SUCCESS(Mms2Status))
                    DXGKRNL_ERR("DxgkpMiniportPnpDispatch: dxgmms2 surprise begin-stop failed 0x%08lX; final RemoveDevice will retry\n", Mms2Status);
            }

            if (WaitForStop)
                StopStatus = DxgkAdapterStop(Adapter);
            if (WaitForStop && NT_SUCCESS(StopStatus))
                return DxgkpForwardIrp(Adapter, Irp);
            InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
            DxgkPresentBeginStop(Adapter);
            if (InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 1, 0) == 0)
                ExWaitForRundownProtectionRelease(&Adapter->RemoveRundownRef);
            DxgkBeginAdapterRundown(Adapter);
            /* A resetting TDR owns Level3 and may still need ISR/DPC progress. */
            DxgkpStopTdrWatchdog(Adapter);
            DxgkpWaitForFlagClear(&Adapter->HotPlugWorkActive);
            ASSERT(DxgkHotPlugWorkCoreCanAcquireLevel3AfterRundown(&Adapter->HotPlugWorkActive));
            /* Serialize the remaining Level3 teardown against power/query/stop. */
            DxgkAcquireLevel3Transition(Adapter);
            DxgkBeginKmdExclusive(Adapter);
            InterlockedExchange(&Adapter->VidSchStopping, 1);
            DxgkpDisconnectAdapterInterrupt(Adapter);
            DxgkPresentTeardown(Adapter);
            DxgkWaitForSubmitDmaReservations(Adapter);
            VidSchPrepareForStop(Adapter);
            KeRemoveQueueDpc(&Adapter->DpcObject);
            KeFlushQueuedDpcs();
            /* Do not reopen public/KMT admission after hardware removal.
             * RemoveDevice later owns KMD exclusivity for cleanup DDIs. */
            DxgkEndKmdExclusive(Adapter, FALSE);
            DxgkReleaseLevel3Transition(Adapter);
            return DxgkpForwardIrp(Adapter, Irp);
        }

        default:
            return DxgkpForwardIrp(Adapter, Irp);
    }
}

static NTSTATUS
DxgkpForwardPowerIrpSynchronously(
    _In_ PDEVICE_OBJECT LowerDeviceObject,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    PAGED_CODE();
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, DxgkpStartDeviceCompletion, &Event, TRUE, TRUE, TRUE);
    PoStartNextPowerIrp(Irp);
    Status = PoCallDriver(LowerDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static NTSTATUS
DxgkpCallMiniportSetPowerState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DEVICE_POWER_STATE NewState,
    _In_ POWER_ACTION ShutdownType)
{
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(Adapter->KmdExclusiveOwnerThread == PsGetCurrentThread());
    if (Adapter->MiniportContext == NULL || Adapter->MiniportContext->InitData.s.DxgkDdiSetPowerState == NULL)
        return STATUS_SUCCESS;
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_DELETE_PENDING;
    Status = STATUS_DELETE_PENDING;
    if (Adapter->State == DxgkAdapterStateStarted && Adapter->MiniportDeviceContext != NULL)
    {
        _SEH2_TRY
        {
            Status = Adapter->MiniportContext->InitData.s.DxgkDdiSetPowerState(Adapter->MiniportDeviceContext, DISPLAY_ADAPTER_HW_ID, NewState, ShutdownType);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    DxgkReleaseMiniportCallback(Adapter);
    return Status;
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
    PDEVICE_OBJECT LowerDeviceObject;
    NTSTATUS Status;
    BOOLEAN Level3TransitionHeld = FALSE;

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

    if (InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || !ExAcquireRundownProtection(&Adapter->RemoveRundownRef))
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }
    if (InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->LowerDeviceObject == NULL)
    {
        ExReleaseRundownProtection(&Adapter->RemoveRundownRef);
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }
    LowerDeviceObject = Adapter->LowerDeviceObject;
    ObReferenceObject(LowerDeviceObject);

    if (Stack->MinorFunction == IRP_MN_SET_POWER &&
        Stack->Parameters.Power.Type == DevicePowerState &&
        Adapter->State == DxgkAdapterStateStarted)
    {
        DEVICE_POWER_STATE NewState;
        DEVICE_POWER_STATE CurrentState;
        POWER_ACTION ShutdownType;
        BOOLEAN ValidState;
        BOOLEAN KmdAdmissionBlocked;
        BOOLEAN InterruptAdmissionBlocked;
        BOOLEAN PoweringDown;
        BOOLEAN PoweringUp;

        DxgkAcquireLevel3Transition(Adapter);
        Level3TransitionHeld = TRUE;
        if (Adapter->State != DxgkAdapterStateStarted)
        {
            DxgkReleaseLevel3Transition(Adapter);
            Level3TransitionHeld = FALSE;
            goto ForwardPowerIrp;
        }
        NewState = Stack->Parameters.Power.State.DeviceState;
        CurrentState = Adapter->DevicePowerState;
        ShutdownType = Stack->Parameters.Power.ShutdownType;
        ValidState = NewState >= PowerDeviceD0 && NewState < PowerDeviceMaximum;
        KmdAdmissionBlocked = InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) != 0;
        InterruptAdmissionBlocked = InterlockedCompareExchange(&Adapter->InterruptCallbacksBlocked, 0, 0) != 0;
        PoweringDown = ValidState && NewState > PowerDeviceD0 && (NewState > CurrentState || (NewState == CurrentState && (!KmdAdmissionBlocked || !InterruptAdmissionBlocked)));
        PoweringUp = ValidState && (NewState < CurrentState || (NewState == PowerDeviceD0 && KmdAdmissionBlocked && InterruptAdmissionBlocked));

        DXGKRNL_TRACE("DxgkpMiniportPowerDispatch: SET_POWER D%d -> D%d down=%u up=%u\n", CurrentState - PowerDeviceD0, NewState - PowerDeviceD0, PoweringDown, PoweringUp);

        if (PoweringDown)
        {
            NTSTATUS KmdStatus;
            NTSTATUS LowerStatus;
            NTSTATUS ResumeStatus = STATUS_SUCCESS;
            NTSTATUS SchedulerStatus;
            NTSTATUS TrackerStatus;
            BOOLEAN InterruptCallbacksBlocked = InterruptAdmissionBlocked;
            BOOLEAN LowerPowerIrpStarted = FALSE;
            BOOLEAN SchedulerSuspended;

            InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
            DxgkPresentBeginStop(Adapter);
            DxgkReleaseLevel3Transition(Adapter);
            Level3TransitionHeld = FALSE;
            DxgkpStopTdrWatchdog(Adapter);
            DxgkAcquireLevel3Transition(Adapter);
            Level3TransitionHeld = TRUE;
            if (Adapter->State != DxgkAdapterStateStarted)
            {
                DxgkReleaseLevel3Transition(Adapter);
                Level3TransitionHeld = FALSE;
                goto ForwardPowerIrp;
            }
            DxgkWaitForSubmitDmaReservations(Adapter);
            if (InterlockedCompareExchange(&Adapter->PresentQueueActiveCalls, 0, 0) != 0)
                KeWaitForSingleObject(&Adapter->PresentQueueCallsDrainedEvent, Executive, KernelMode, FALSE, NULL);
            DxgkPresentCancelAllStopped(Adapter);

            SchedulerStatus = VidSchSuspendScheduler(Adapter);
            SchedulerSuspended = NT_SUCCESS(SchedulerStatus) && Adapter->VidSchContext != NULL && CurrentState == PowerDeviceD0;
            if (!NT_SUCCESS(SchedulerStatus) && SchedulerStatus != STATUS_NOT_SUPPORTED)
            {
                if (CurrentState == PowerDeviceD0)
                {
                    DxgkBeginKmdExclusive(Adapter);
                    DxgkVidMmResumeAdapter(Adapter);
                    DxgkUnblockInterruptCallbacks(Adapter);
                    InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
                    DxgkPresentResume(Adapter);
                    DxgkpStartTdrWatchdog(Adapter);
                    DxgkEndKmdExclusive(Adapter, TRUE);
                }
                else
                {
                    DxgkBeginKmdExclusive(Adapter);
                    DxgkVidMmQuiesceAdapter(Adapter);
                    DxgkBlockInterruptCallbacks(Adapter);
                    DxgkEndKmdExclusive(Adapter, FALSE);
                }
                PoStartNextPowerIrp(Irp);
                Status = SchedulerStatus;
                goto CompletePowerIrp;
            }

            TrackerStatus = DxgkpWaitForTrackedDmaIdle(Adapter, 1000);
            if (!NT_SUCCESS(TrackerStatus))
            {
                Status = TrackerStatus;
                DxgkBeginKmdExclusive(Adapter);
                if (CurrentState == PowerDeviceD0)
                {
                    DxgkVidMmResumeAdapter(Adapter);
                    if (SchedulerSuspended)
                        ResumeStatus = VidSchResumeScheduler(Adapter);
                    if (NT_SUCCESS(ResumeStatus) || ResumeStatus == STATUS_NOT_SUPPORTED)
                    {
                        DxgkUnblockInterruptCallbacks(Adapter);
                        InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
                        DxgkPresentResume(Adapter);
                        DxgkpStartTdrWatchdog(Adapter);
                        DxgkEndKmdExclusive(Adapter, TRUE);
                    }
                    else
                    {
                        Status = ResumeStatus;
                        DxgkVidMmQuiesceAdapter(Adapter);
                        DxgkBlockInterruptCallbacks(Adapter);
                        DxgkEndKmdExclusive(Adapter, FALSE);
                    }
                }
                else
                {
                    DxgkVidMmQuiesceAdapter(Adapter);
                    DxgkBlockInterruptCallbacks(Adapter);
                    DxgkEndKmdExclusive(Adapter, FALSE);
                }
                PoStartNextPowerIrp(Irp);
                goto CompletePowerIrp;
            }

            DxgkBeginKmdExclusive(Adapter);
            DxgkVidMmQuiesceAdapter(Adapter);
            /* Paging eviction can require ISR/DPC completion progress. */
            Status = DxgkVidMmPrepareForIdle(Adapter);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("DxgkpMiniportPowerDispatch: power-down VidMm idle preparation failed 0x%08lX; restoring D%d\n", Status, CurrentState - PowerDeviceD0);
                goto RollbackPowerDown;
            }
            DxgkBlockInterruptCallbacks(Adapter);
            InterruptCallbacksBlocked = TRUE;
            if (Adapter->State != DxgkAdapterStateStarted || Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0 || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
            {
                Status = STATUS_DELETE_PENDING;
                goto RetainPowerDownAdmission;
            }
            KmdStatus = DxgkpCallMiniportSetPowerState(Adapter, NewState, ShutdownType);
            if (!NT_SUCCESS(KmdStatus))
            {
                DXGKRNL_WARN("DxgkpMiniportPowerDispatch: power-down DxgkDdiSetPowerState failed 0x%08lX; restoring D%d\n", KmdStatus, CurrentState - PowerDeviceD0);
                Status = KmdStatus;
                goto RollbackPowerDown;
            }

            LowerPowerIrpStarted = TRUE;
            LowerStatus = DxgkpForwardPowerIrpSynchronously(LowerDeviceObject, Irp);
            if (!NT_SUCCESS(LowerStatus))
            {
                KmdStatus = DxgkpCallMiniportSetPowerState(Adapter, CurrentState, PowerActionNone);
                if (!NT_SUCCESS(KmdStatus))
                {
                    DXGKRNL_ERR("DxgkpMiniportPowerDispatch: lower power-down failed 0x%08lX and D%d compensation failed 0x%08lX; retaining blocked admission\n", LowerStatus, CurrentState - PowerDeviceD0, KmdStatus);
                    Status = KmdStatus;
                    goto RetainPowerDownAdmission;
                }
                Status = LowerStatus;
                goto RollbackPowerDown;
            }

            Adapter->DevicePowerState = NewState;
            Status = LowerStatus;
            DxgkEndKmdExclusive(Adapter, FALSE);
            goto CompletePowerIrp;

RollbackPowerDown:
            if (CurrentState != PowerDeviceD0 || Adapter->State != DxgkAdapterStateStarted || Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0 || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
                goto RetainPowerDownAdmission;
            DxgkVidMmResumeAdapter(Adapter);
            if (SchedulerSuspended)
                ResumeStatus = VidSchResumeScheduler(Adapter);
            if (!NT_SUCCESS(ResumeStatus) && ResumeStatus != STATUS_NOT_SUPPORTED)
            {
                DXGKRNL_ERR("DxgkpMiniportPowerDispatch: D0 rollback scheduler resume failed 0x%08lX; retaining blocked admission\n", ResumeStatus);
                DxgkVidMmQuiesceAdapter(Adapter);
                Status = ResumeStatus;
                goto RetainPowerDownAdmission;
            }
            if (InterruptCallbacksBlocked)
                DxgkUnblockInterruptCallbacks(Adapter);
            InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
            DxgkPresentResume(Adapter);
            DxgkpStartTdrWatchdog(Adapter);
            DxgkEndKmdExclusive(Adapter, TRUE);
            if (!LowerPowerIrpStarted)
                PoStartNextPowerIrp(Irp);
            goto CompletePowerIrp;

RetainPowerDownAdmission:
            if (!InterruptCallbacksBlocked)
            {
                DxgkBlockInterruptCallbacks(Adapter);
                InterruptCallbacksBlocked = TRUE;
            }
            DxgkEndKmdExclusive(Adapter, FALSE);
            if (!LowerPowerIrpStarted)
                PoStartNextPowerIrp(Irp);
            goto CompletePowerIrp;
        }

        if (PoweringUp)
        {
            NTSTATUS KmdStatus;
            NTSTATUS LowerStatus;
            NTSTATUS SchedulerStatus;

            InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
            DxgkPresentBeginStop(Adapter);
            InterlockedExchange(&Adapter->KmdCallsBlocked, 1);
            LowerStatus = DxgkpForwardPowerIrpSynchronously(LowerDeviceObject, Irp);
            Status = LowerStatus;
            if (!NT_SUCCESS(LowerStatus))
                goto CompletePowerIrp;

            DxgkBeginKmdExclusive(Adapter);
            KmdStatus = DxgkpCallMiniportSetPowerState(Adapter, NewState, ShutdownType);
            if (!NT_SUCCESS(KmdStatus))
            {
                DXGKRNL_WARN("DxgkpMiniportPowerDispatch: power-up DxgkDdiSetPowerState failed 0x%08lX; completing the lower power IRP and retaining blocked admission\n", KmdStatus);
                Status = KmdStatus;
                DxgkEndKmdExclusive(Adapter, FALSE);
                goto CompletePowerIrp;
            }
            Adapter->DevicePowerState = NewState;

            if (NewState == PowerDeviceD0)
            {
                DxgkVidMmResumeAdapter(Adapter);
                SchedulerStatus = VidSchResumeScheduler(Adapter);
                if (!NT_SUCCESS(SchedulerStatus) && SchedulerStatus != STATUS_NOT_SUPPORTED)
                {
                    DXGKRNL_ERR("DxgkpMiniportPowerDispatch: D0 scheduler resume failed 0x%08lX; retaining blocked admission\n", SchedulerStatus);
                    DxgkVidMmQuiesceAdapter(Adapter);
                    Status = SchedulerStatus;
                    DxgkEndKmdExclusive(Adapter, FALSE);
                    goto CompletePowerIrp;
                }
                DxgkUnblockInterruptCallbacks(Adapter);
                InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
                DxgkPresentResume(Adapter);
                DxgkpStartTdrWatchdog(Adapter);
                DxgkEndKmdExclusive(Adapter, TRUE);
            }
            else
            {
                DxgkEndKmdExclusive(Adapter, FALSE);
            }
            goto CompletePowerIrp;
        }

        DxgkReleaseLevel3Transition(Adapter);
        Level3TransitionHeld = FALSE;
    }

ForwardPowerIrp:
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(LowerDeviceObject, Irp);
    ObDereferenceObject(LowerDeviceObject);
    ExReleaseRundownProtection(&Adapter->RemoveRundownRef);
    return Status;

CompletePowerIrp:
    if (Level3TransitionHeld)
        DxgkReleaseLevel3Transition(Adapter);
    ObDereferenceObject(LowerDeviceObject);
    ExReleaseRundownProtection(&Adapter->RemoveRundownRef);
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static BOOLEAN
DxgkpAcquireMiniportRegistration(
    _In_ PDXGKRNL_MINIPORT_CONTEXT MpCtx)
{
    if (MpCtx == NULL || MpCtx->Signature != DXGKP_MINIPORT_CONTEXT_SIGNATURE || InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationRegistered)
        return FALSE;
    if (!ExAcquireRundownProtection(&MpCtx->RegistrationRundown))
        return FALSE;
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationRegistered)
    {
        ExReleaseRundownProtection(&MpCtx->RegistrationRundown);
        return FALSE;
    }
    return TRUE;
}

static VOID
DxgkpReleaseMiniportRegistration(
    _In_ PDXGKRNL_MINIPORT_CONTEXT MpCtx)
{
    ExReleaseRundownProtection(&MpCtx->RegistrationRundown);
}

static VOID
DxgkpFreeMiniportRegistryPath(
    _Inout_ PDXGKRNL_MINIPORT_CONTEXT MpCtx)
{
    if (MpCtx->RegistryPath.Buffer != NULL)
        ExFreePoolWithTag(MpCtx->RegistryPath.Buffer, TAG_DXGK_REGISTRY);
    RtlZeroMemory(&MpCtx->RegistryPath, sizeof(MpCtx->RegistryPath));
}

static VOID
DxgkpClearMiniportRegistrationPayload(
    _Inout_ PDXGKRNL_MINIPORT_CONTEXT MpCtx)
{
    DxgkpFreeMiniportRegistryPath(MpCtx);
    RtlZeroMemory(&MpCtx->InitData, sizeof(MpCtx->InitData));
    MpCtx->InitDataSize = 0;
    MpCtx->IsDisplayOnlyDriver = FALSE;
    MpCtx->UseDodLayout = FALSE;
    MpCtx->IsBasicDisplayFallback = FALSE;
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
static NTSTATUS
DxgkpAddDeviceRegistered(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    PDEVICE_OBJECT            Fdo;
    PDXGKRNL_ADAPTER          Adapter;
    PVOID                     MiniportDeviceContext;
    KIRQL                     OldIrql;
    NTSTATUS                  Mms2Status;
    NTSTATUS                  Status;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkpAddDevice: DriverObject %p PDO %p\n",
                  DriverObject, PhysicalDeviceObject);

    /* Retrieve the per-miniport context from the DriverObjectExtension. */
    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)
            IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);
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
    if (!DxgkCapsCoreInterfaceVersionAtLeast(
            MpCtx->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_1_0))
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
    Fdo->Flags |= DO_POWER_PAGABLE;

    Adapter = DXGKRNL_ADAPTER_FROM_DEVOBJ(Fdo);
    RtlZeroMemory(Adapter, sizeof(*Adapter));

    Adapter->MiniportContext         = MpCtx;
    Adapter->FunctionalDeviceObject  = Fdo;
    Adapter->PhysicalDeviceObject    = PhysicalDeviceObject;
    Adapter->State                   = DxgkAdapterStateUninitialized;
    Adapter->DevicePowerState        = PowerDeviceD0;
    Adapter->SystemPowerState        = PowerSystemWorking;
    Adapter->HighestAcceptableAddress.QuadPart = (LONGLONG)-1;
    Adapter->SchedulingCaps.Value = 0;
    Adapter->TdrConfig = g_TdrConfig;

    /* Initialise synchronisation primitives. */
    KeInitializeSpinLock(&Adapter->InterruptLock);
    DxgkPeriodicInterruptCoreInitialize(
        &Adapter->PeriodicInterruptCore);
    KeInitializeSpinLock(&Adapter->ChildListLock);
    KeInitializeSpinLock(&Adapter->SubmitDmaLock);
    KeInitializeSpinLock(&Adapter->TdrHistoryLock);
    KeInitializeEvent(&Adapter->SyncEvent, SynchronizationEvent, FALSE);
    KeInitializeMutex(&Adapter->AdapterMutex, 0);
    KeInitializeMutex(&Adapter->VidPnMutex, 0);
    KeInitializeEvent(&Adapter->AdapterStartCompletedEvent, NotificationEvent, TRUE);
    Adapter->AdapterStartGeneration = 0;
    Adapter->AdapterStartCompletedGeneration = 0;
    Adapter->AdapterStartStatus = STATUS_SUCCESS;
    KeInitializeEvent(&Adapter->AdapterStopCompletedEvent, NotificationEvent, TRUE);
    Adapter->AdapterStopInProgress = 0;
    Adapter->AdapterStopGeneration = 0;
    Adapter->AdapterStopCompletedGeneration = 0;
    Adapter->AdapterStopIntentCount = 0;
    Adapter->AdapterStopStatus = STATUS_SUCCESS;
    Adapter->Mms2ContextStreamValid = 0;
    Adapter->Mms2TimelineCallsOpen = 0;
    Adapter->Mms2TimelineActiveCalls = 0;
    KeInitializeMutex(&Adapter->MiniportCallbackMutex, 0);
    KeInitializeMutex(&Adapter->KmdExclusiveMutex, 0);
    KeInitializeMutex(&Adapter->KmdTransactionMutex, 0);
    KeInitializeMutex(&Adapter->Level3TransitionMutex, 0);
    Adapter->KmdCallsBlocked = 0;
    Adapter->KmdActiveCalls = 0;
    Adapter->KmdExclusiveOwnerThread = NULL;
    Adapter->KmdTransactionOwnerThread = NULL;
    Adapter->KmdTransactionDepth = 0;
    Adapter->Level3TransitionOwnerThread = NULL;
    Adapter->Level3TransitionDepth = 0;
    Adapter->InterruptCallbacksBlocked = 1;
    Adapter->InterruptActiveCalls = 0;
    KeInitializeMutex(&Adapter->SharedPrimaryMutex, 0);
    ExInitializeRundownProtection(&Adapter->SharedSurfaceRundown);
    Adapter->SharedSurfaceGeneration = 1;
    Adapter->SharedSurfaceMutationDepth = 0;
    Adapter->SharedSurfaceAvailable = 0;
    ExInitializeRundownProtection(&Adapter->RundownRef);
    Adapter->RundownStarted = 0;
    Adapter->DeviceLifecycleActiveOperations = 0;
    KeInitializeEvent(&Adapter->DeviceLifecycleOperationsDrainedEvent, NotificationEvent, TRUE);
    ExInitializeRundownProtection(&Adapter->RemoveRundownRef);
    Adapter->RemoveRundownStarted = 0;
    ExInitializeRundownProtection(&Adapter->ReverseCallbackRundownRef);
    Adapter->ReverseCallbackRundownStarted = 0;
    Adapter->MiniportRemoveDeviceComplete = FALSE;
    KeInitializeDpc(&Adapter->DpcObject, DxgkpAdapterDpcRoutine, Adapter);
    Adapter->SubmitDmaRetireWorkQueued = 0;
    Adapter->SubmitDmaRetireActiveWorkers = 0;
    ExInitializeWorkItem(&Adapter->SubmitDmaRetireWorkItem, DxgkpRetireSubmittedDmaBuffersWorker, Adapter);
    Adapter->SubmitDmaStopping = 1;
    Adapter->SubmitDmaActiveReservations = 0;
    Adapter->PresentQueueInitializationStatus = STATUS_DEVICE_NOT_READY;
    Adapter->PresentQueueStopping = 1;
    Adapter->VBlankResetActive = 0;
    Adapter->PresentQueueActiveCalls = 0;
    Adapter->VBlankResetGeneration = 0;
    Adapter->VidSchStopping = 1;
    Adapter->VidSchActiveCalls = 0;
    Adapter->VidMmBackingCount = 0;
    Adapter->VidMmDestroyWorkerCount = 0;
    Adapter->VidMmDestroyQueuesBlocked = 1;
    Adapter->HotPlugGeneration = 0;
    Adapter->HotPlugWorkActive = 0;
    Adapter->ChildEnumerationEpoch = 0;
    Adapter->ChildRelationsEnumerated = 0;
    DxgkVidPnInitializeHotPlugWorker(Adapter);
    KeInitializeEvent(&Adapter->SubmitDmaRetireDrainedEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->SubmitDmaReservationsDrainedEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->PresentQueueCallsDrainedEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->VidMmBackingsDrainedEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->VidMmDestroyWorkersDrainedEvent, NotificationEvent, TRUE);

    /* Initialise linked lists. */
    InitializeListHead(&Adapter->DeviceListHead);
    InitializeListHead(&Adapter->ChildListHead);
    InitializeListHead(&Adapter->SubmitDmaListHead);
    InitializeListHead(&Adapter->SubmitDmaRetireListHead);
    InitializeListHead(&Adapter->MiniportAdapterListEntry);
    InitializeListHead(&Adapter->GlobalAdapterListEntry);

    Status = DxgkpMms2CreateAdapter(Adapter, DxgkpMms2GetAdapterFlags(Adapter), &Adapter->Mms2Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: dxgmms2 adapter creation failed 0x%08lX\n", Status);
        IoDeleteDevice(Fdo);
        return Status;
    }
    Adapter->Mms2State = DxgkMms2AdapterCreated;

    /* Call DxgkDdiAddDevice to obtain the miniport's device context. */
    Status = MpCtx->InitData.s.DxgkDdiAddDevice(PhysicalDeviceObject,
                                               &Adapter->MiniportDeviceContext);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: DxgkDdiAddDevice failed 0x%08lX\n",
                    Status);
        (VOID)DxgkpCollectAdapterDiagnosticInfo(
                  Adapter,
                  DXGK_DI_ADDDEVICE,
                  FALSE);
        Mms2Status = DxgkpMms2DestroyAdministrativeAdapter(Adapter);
        if (!NT_SUCCESS(Mms2Status))
            DxgkpBugCheckMms2Lifecycle(Adapter, Mms2Status, DXGKP_MMS2_FAILURE_ADD_ROLLBACK);
        IoDeleteDevice(Fdo);
        return Status;
    }

    InterlockedExchange(&Adapter->MiniportCallbacksValid, 1);

    DXGKRNL_TRACE("DxgkpAddDevice: MiniportDeviceContext = %p\n",
                  Adapter->MiniportDeviceContext);

    /* Attach the FDO to the device stack above the PDO. */
    Adapter->LowerDeviceObject =
        IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (Adapter->LowerDeviceObject == NULL)
    {
        DXGKRNL_ERR("DxgkpAddDevice: IoAttachDeviceToDeviceStack failed\n");
        (VOID)KeWaitForSingleObject(&Adapter->MiniportCallbackMutex, Executive, KernelMode, FALSE, NULL);
        MiniportDeviceContext = Adapter->MiniportDeviceContext;
        InterlockedExchange(&Adapter->MiniportCallbacksValid, 0);
        Adapter->MiniportDeviceContext = NULL;
        Status = DxgkpRemoveMiniportDevice(Adapter, MiniportDeviceContext);
        if (!NT_SUCCESS(Status))
            DXGKRNL_ERR("DxgkpAddDevice: rollback DxgkDdiRemoveDevice failed 0x%08lX\n", Status);
        if (InterlockedCompareExchange(&Adapter->ReverseCallbackRundownStarted, 1, 0) == 0)
            ExWaitForRundownProtectionRelease(&Adapter->ReverseCallbackRundownRef);
        KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
        Mms2Status = DxgkpMms2DestroyAdministrativeAdapter(Adapter);
        if (!NT_SUCCESS(Mms2Status))
            DxgkpBugCheckMms2Lifecycle(Adapter, Mms2Status, DXGKP_MMS2_FAILURE_ATTACH_ROLLBACK);
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

NTSTATUS
NTAPI
DxgkpAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    NTSTATUS Status;

    PAGED_CODE();
    if (DriverObject == NULL || PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;
    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);
    if (!DxgkpAcquireMiniportRegistration(MpCtx))
        return STATUS_DELETE_PENDING;
    Status = DxgkpAddDeviceRegistered(DriverObject, PhysicalDeviceObject);
    DxgkpReleaseMiniportRegistration(MpCtx);
    return Status;
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
    PDXGKDDI_UNLOAD UnloadCallback = NULL;
    KIRQL OldIrql;
    BOOLEAN LiveAdapters;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkpDriverUnload: DriverObject %p\n", DriverObject);

    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)
            IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);

    if (MpCtx == NULL || MpCtx->Signature != DXGKP_MINIPORT_CONTEXT_SIGNATURE)
        return;

    (VOID)KeWaitForSingleObject(&g_MiniportRegistrationMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationRegistered)
    {
        KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
        return;
    }
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationUninitializing);
    KeMemoryBarrier();
    ExWaitForRundownProtectionRelease(&MpCtx->RegistrationRundown);
    KeAcquireSpinLock(&MpCtx->AdapterListLock, &OldIrql);
    LiveAdapters = MpCtx->AdapterCount != 0 || !IsListEmpty(&MpCtx->AdapterListHead);
    KeReleaseSpinLock(&MpCtx->AdapterListLock, OldIrql);
    if (LiveAdapters)
    {
        ExReInitializeRundownProtection(&MpCtx->RegistrationRundown);
        KeMemoryBarrier();
        InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationRegistered);
        KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
        DXGKRNL_ERR("DxgkpDriverUnload: refusing teardown with live adapters\n");
        return;
    }
    UnloadCallback = MpCtx->UseDodLayout ? MpCtx->InitData.dod.DxgkDdiUnload : MpCtx->InitData.s.DxgkDdiUnload;
    KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
    if (UnloadCallback != NULL)
        UnloadCallback();
    (VOID)KeWaitForSingleObject(&g_MiniportRegistrationMutex, Executive, KernelMode, FALSE, NULL);
    DxgkpClearMiniportRegistrationPayload(MpCtx);
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationEmpty);
    KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
}

NTSTATUS
APIENTRY
DxgkUnInitialize(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    KIRQL OldIrql;
    BOOLEAN LiveAdapters;
    NTSTATUS Status;

    PAGED_CODE();
    if (DriverObject == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&DxgkpInitialized, 0, 0) != 2)
        return STATUS_SUCCESS;
    (VOID)KeWaitForSingleObject(&g_MiniportRegistrationMutex, Executive, KernelMode, FALSE, NULL);
    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);
    if (MpCtx == NULL || MpCtx->Signature != DXGKP_MINIPORT_CONTEXT_SIGNATURE || InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationRegistered)
    {
        Status = STATUS_SUCCESS;
        goto UninitializeUnlock;
    }
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationUninitializing);
    KeMemoryBarrier();
    ExWaitForRundownProtectionRelease(&MpCtx->RegistrationRundown);
    KeAcquireSpinLock(&MpCtx->AdapterListLock, &OldIrql);
    LiveAdapters = MpCtx->AdapterCount != 0 || !IsListEmpty(&MpCtx->AdapterListHead);
    KeReleaseSpinLock(&MpCtx->AdapterListLock, OldIrql);
    if (LiveAdapters)
    {
        ExReInitializeRundownProtection(&MpCtx->RegistrationRundown);
        KeMemoryBarrier();
        InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationRegistered);
        DXGKRNL_WARN("DxgkUnInitialize: cleanup deferred because the miniport still owns an adapter\n");
        Status = STATUS_SUCCESS;
        goto UninitializeUnlock;
    }
    DxgkpClearMiniportRegistrationPayload(MpCtx);
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationEmpty);
    Status = STATUS_SUCCESS;

UninitializeUnlock:
    KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
    return Status;
}

/* ========================================================================
 * DxgkInitializeEx / DxgkInitialize — miniport registration entry points
 * ====================================================================== */

#ifndef REACTOS_WDDM_TARGET_LEVEL
#define REACTOS_WDDM_TARGET_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_0
#endif

/*
 * Highest full DRIVER_INITIALIZATION_DATA tail imported and ABI-checked in
 * this translation unit. Keep this independent from the compile selector:
 * dxgkrnl compiles with the newest audited declaration surface, but must not
 * accept a miniport whose declared table extends beyond the tails actually
 * present here.
 */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_3_2
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_3_1
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_3_0
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_9
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_8
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_7
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_6
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_5
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_4
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_3
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_2
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_1
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_0
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_1_3
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_1_2
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_1_1
#else
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_1_0
#endif

/* Return the append-only prefix that a full-table caller compiled for Version
 * can make readable.  Versions newer than the last locally declared tail are
 * deliberately capped at that tail. */
static ULONG
DxgkpFullInitDataPrefixSize(
    _In_ ULONG Version)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_3_2))
        return DXGKP_FIELD_END(
            DRIVER_INITIALIZATION_DATA,
            DxgkDdiResetDisplayEngine);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_3_1))
        return DXGKP_FIELD_END(
            DRIVER_INITIALIZATION_DATA,
            Reserved4);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_3_0))
        return DXGKP_FIELD_END(
            DRIVER_INITIALIZATION_DATA,
            DxgkDdiCancelFlips);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_9))
        return DXGKP_FIELD_END(
            DRIVER_INITIALIZATION_DATA,
            DxgkDdiSetInterruptTargetPresentId);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_8))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_7))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_6))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved3);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_5))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTrackedWorkloadPowerLevel);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_4))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeHwEngine);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_3))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyProtectedSession);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_2))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiGetPostCompositionCaps);
#endif
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_1))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateMonitorLinkInfo);
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVideoProtectedRegion);
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_1_3))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiFormatHistoryBuffer);
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_1_2))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval);
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_1_1))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryVidPnHWCapability);
    return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetDisplayPrivateDriverFormat);
}

/* KMDDOD has a distinct layout.  Its Win8/WDDM1.3 prefix ends at surprise
 * removal; WDDM2.0 appends one power-runtime callback and later SDKs append no
 * additional public fields through Windows 11 26100. */
static ULONG
DxgkpDodInitDataPrefixSize(
    _In_ ULONG Version)
{
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
        return DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiPowerRuntimeSetDeviceHandle);
    return DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval);
}

#ifdef _WIN64
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetDisplayPrivateDriverFormat) == 0x1F0);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryVidPnHWCapability) == 0x238);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval) == 0x298);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiFormatHistoryBuffer) == 0x2C8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVideoProtectedRegion) == 0x340);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateMonitorLinkInfo) == 0x370);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateHwContext) == 0x370);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiGetPostCompositionCaps) == 0x408);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateHwContextState) == 0x408);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateProtectedSession) == 0x410);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyProtectedSession) == 0x420);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetSchedulingLogBuffer) == 0x420);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryDiagnosticTypesSupport) == 0x468);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeHwEngine) == 0x480);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSignalMonitoredFence) == 0x480);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTargetAdjustedColorimetry2) == 0x498);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTrackedWorkloadPowerLevel) == 0x4A8);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSaveMemoryForHotUpdate) == 0x4A8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCollectDiagnosticInfo) == 0x4B8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved3) == 0x4C8);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x4C8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x4D0);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetFlipQueueLogBuffer) == 0x4D0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateFlipQueueLog) == 0x4D8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelQueuedFlips) == 0x4E0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x4E8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x4F0);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetAllocationBackingStore) == 0x4F0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateCpuEvent) == 0x4F8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyCpuEvent) == 0x500);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x508);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x510);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateNativeFence) == 0x510);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x558);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x560);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateMemoryBasis) == 0x560);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x600);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x608);
#endif
C_ASSERT(DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval) == 0x148);
C_ASSERT(DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiPowerRuntimeSetDeviceHandle) == 0x150);
#else
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetDisplayPrivateDriverFormat) == 0xF8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryVidPnHWCapability) == 0x11C);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval) == 0x14C);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiFormatHistoryBuffer) == 0x164);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVideoProtectedRegion) == 0x1A0);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateMonitorLinkInfo) == 0x1B8);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateHwContext) == 0x1B8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiGetPostCompositionCaps) == 0x204);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateHwContextState) == 0x204);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateProtectedSession) == 0x208);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyProtectedSession) == 0x210);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetSchedulingLogBuffer) == 0x210);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryDiagnosticTypesSupport) == 0x234);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeHwEngine) == 0x240);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSignalMonitoredFence) == 0x240);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTargetAdjustedColorimetry2) == 0x24C);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTrackedWorkloadPowerLevel) == 0x254);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSaveMemoryForHotUpdate) == 0x254);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCollectDiagnosticInfo) == 0x25C);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved3) == 0x264);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x264);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x268);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetFlipQueueLogBuffer) == 0x268);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateFlipQueueLog) == 0x26C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelQueuedFlips) == 0x270);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x274);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x278);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetAllocationBackingStore) == 0x278);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateCpuEvent) == 0x27C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyCpuEvent) == 0x280);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x284);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x288);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateNativeFence) == 0x288);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x2AC);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x2B0);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateMemoryBasis) == 0x2B0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x300);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x304);
#endif
C_ASSERT(DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval) == 0xA4);
C_ASSERT(DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiPowerRuntimeSetDeviceHandle) == 0xA8);
#endif
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Version) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, Version));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiAddDevice) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiAddDevice));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiStartDevice) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiStartDevice));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiStopDevice) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiStopDevice));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiRemoveDevice) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiRemoveDevice));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUnload) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiUnload));

/*
 * DxgkpInitializeMiniport
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
static NTSTATUS
DxgkpInitializeMiniport(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ ULONG                       DriverInitDataSize,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData,
    _In_ BOOLEAN                     UseDodLayout)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    ULONG                     RequiredPrefixSize;
    ULONG                     CopySize;
    ULONG                     Version;
    ULONG                     VersionLevel;
    PWCH                      RegBuf;
    NTSTATUS                  Status;
    BOOLEAN                   NewContext;

    PAGED_CODE();

    /* --- Validate parameters -------------------------------------------- */

    if (DriverObject == NULL ||
        RegistryPath == NULL ||
        DriverInitializationData == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((RegistryPath->Length & (sizeof(WCHAR) - 1)) != 0 || RegistryPath->Length > UNICODE_STRING_MAX_BYTES - sizeof(WCHAR) || RegistryPath->MaximumLength < RegistryPath->Length || (RegistryPath->Length != 0 && RegistryPath->Buffer == NULL))
        return STATUS_INVALID_PARAMETER;

    if (DriverInitDataSize < DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Version))
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: DriverInitDataSize %lu too small\n",
                    DriverInitDataSize);
        return STATUS_INVALID_PARAMETER;
    }

    Version = DriverInitializationData->Version;
    DXGKRNL_TRACE("DxgkpInitializeMiniport: DriverObject %p RegPath %wZ Size=%lu Version=0x%lX Layout=%s\n", DriverObject, RegistryPath, DriverInitDataSize, Version, UseDodLayout ? "DOD" : "full");

    VersionLevel = DxgkCapsCoreInterfaceVersionToLevel(Version);
    if (VersionLevel == 0)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: unknown DDI selector 0x%lX\n",
                    Version);
        return STATUS_REVISION_MISMATCH;
    }
    if (!DxgkCapsCoreInterfaceVersionPermitted(
            Version, REACTOS_WDDM_TARGET_LEVEL))
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: DDI selector 0x%lX level %lu exceeds configured WDDM ceiling %lu\n",
                    Version, VersionLevel, (ULONG)REACTOS_WDDM_TARGET_LEVEL);
        return STATUS_REVISION_MISMATCH;
    }

    if (UseDodLayout)
    {
        if (!DxgkCapsCoreInterfaceVersionAtLeast(
                Version, DXGK_CAPS_CORE_LEVEL_WDDM_1_2))
        {
            DXGKRNL_ERR("DxgkpInitializeMiniport: unsupported DOD version 0x%lX (minimum 0x%lX)\n", Version, (ULONG)DXGKDDI_INTERFACE_VERSION_WIN8);
            return STATUS_INVALID_PARAMETER;
        }
        RequiredPrefixSize = DxgkpDodInitDataPrefixSize(Version);
    }
    else
    {
        if (VersionLevel > DXGKP_FULL_INIT_DATA_MAX_LEVEL)
        {
            DXGKRNL_ERR("DxgkpInitializeMiniport: DDI selector 0x%lX level %lu exceeds imported full-table ceiling %lu\n",
                        Version, VersionLevel,
                        (ULONG)DXGKP_FULL_INIT_DATA_MAX_LEVEL);
            return STATUS_REVISION_MISMATCH;
        }
        if (!DxgkCapsCoreInterfaceVersionAtLeast(
                Version, DXGK_CAPS_CORE_LEVEL_WDDM_1_0))
        {
            DXGKRNL_ERR("DxgkpInitializeMiniport: unsupported full-table version 0x%lX (minimum 0x%lX)\n", Version, (ULONG)DXGKDDI_INTERFACE_VERSION_VISTA);
            return STATUS_INVALID_PARAMETER;
        }
        RequiredPrefixSize = DxgkpFullInitDataPrefixSize(Version);
    }

    if (DriverInitDataSize < RequiredPrefixSize)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: %s table size %lu is smaller than version 0x%lX prefix %lu\n", UseDodLayout ? "DOD" : "full", DriverInitDataSize, Version, RequiredPrefixSize);
        return STATUS_INVALID_PARAMETER;
    }

    /* The four PnP lifecycle callbacks are required as one transaction. */
    if (DriverInitializationData->DxgkDdiAddDevice == NULL ||
        DriverInitializationData->DxgkDdiStartDevice == NULL ||
        DriverInitializationData->DxgkDdiStopDevice == NULL ||
        DriverInitializationData->DxgkDdiRemoveDevice == NULL)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: mandatory lifecycle callback missing (AddDevice=%p StartDevice=%p StopDevice=%p RemoveDevice=%p)\n",
                    DriverInitializationData->DxgkDdiAddDevice,
                    DriverInitializationData->DxgkDdiStartDevice,
                    DriverInitializationData->DxgkDdiStopDevice,
                    DriverInitializationData->DxgkDdiRemoveDevice);
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * WDDM 2.3 replaces CommitVidPn with SetTimingsFromVidPn for full
     * display miniports. Render-only full-table drivers have no CommitVidPn
     * surface and are therefore not forced into a display contract.
     */
    if (!UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_3) &&
        DriverInitializationData->DxgkDdiCommitVidPn != NULL &&
        DriverInitializationData->DxgkDdiSetTimingsFromVidPn == NULL)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: WDDM 2.3 display miniport "
                    "is missing mandatory SetTimingsFromVidPn\n");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * CollectDiagnosticInfo is a WDDM 2.6 core requirement for full graphics
     * miniports. Its AddDevice failure form explicitly permits a NULL adapter
     * context, so registration must establish the callback before any PDO is
     * handed to the driver.
     */
    if (!UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_6) &&
        DriverInitializationData->DxgkDdiCollectDiagnosticInfo == NULL)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: WDDM 2.6 miniport "
                    "is missing mandatory CollectDiagnosticInfo\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* --- One-time global init ------------------------------------------- */

    Status = DxgkpEnsureGlobalInitialization();
    if (!NT_SUCCESS(Status))
        return Status;

    /* Import-only callers create a stable dxgkrnl-owned DriverObject rather
     * than binding the global control device to the first miniport. */
    Status = DxgkpEnsureControlDevice();
    if (!NT_SUCCESS(Status) || GDxgControlDeviceObject == NULL)
    {
        DXGKRNL_ERR("DxgkInitializeEx: dxgkrnl control device initialization failed 0x%08lX\n", Status);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_NOT_READY : Status;
    }

    /* The extension is I/O-manager-owned and cannot be deleted separately
     * from the DRIVER_OBJECT.  Serialize allocation/reuse so failed
     * registration and DxgkUnInitialize can return it to an empty state. */
    (VOID)KeWaitForSingleObject(&g_MiniportRegistrationMutex, Executive, KernelMode, FALSE, NULL);
    NewContext = FALSE;
    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);
    if (MpCtx == NULL)
    {
        Status = IoAllocateDriverObjectExtension(DriverObject, &g_MiniportContextClientId, sizeof(DXGKRNL_MINIPORT_CONTEXT), (PVOID *)&MpCtx);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkInitializeEx: IoAllocateDriverObjectExtension failed 0x%08lX\n", Status);
            goto RegistrationUnlock;
        }
        RtlZeroMemory(MpCtx, sizeof(*MpCtx));
        MpCtx->Signature = DXGKP_MINIPORT_CONTEXT_SIGNATURE;
        KeInitializeSpinLock(&MpCtx->AdapterListLock);
        InitializeListHead(&MpCtx->AdapterListHead);
        ExInitializeRundownProtection(&MpCtx->RegistrationRundown);
        NewContext = TRUE;
    }
    else
    {
        if (MpCtx->Signature != DXGKP_MINIPORT_CONTEXT_SIGNATURE || InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationEmpty || MpCtx->AdapterCount != 0 || !IsListEmpty(&MpCtx->AdapterListHead))
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
            goto RegistrationUnlock;
        }
        ExReInitializeRundownProtection(&MpCtx->RegistrationRundown);
    }
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationInitializing);

    /* --- Copy callback table -------------------------------------------- */

    /*
     * Copy only the prefix implied by the declared version.  This is the only
     * byte count the size-less wrappers can prove readable.  Explicit callers
     * may provide a larger future table, but unknown append-only tails remain
     * zero in our context and are never read from the caller.
     */
    CopySize = min(RequiredPrefixSize, (ULONG)sizeof(MpCtx->InitData));
    RtlCopyMemory(&MpCtx->InitData, DriverInitializationData, CopySize);
    MpCtx->InitDataSize = CopySize;
    MpCtx->IsDisplayOnlyDriver = UseDodLayout;
    MpCtx->UseDodLayout = UseDodLayout;

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
        DXGKRNL_ERR("DxgkInitializeEx: registry path alloc failed (%u bytes)\n", RegistryPath->Length + (ULONG)sizeof(WCHAR));
        Status = STATUS_INSUFFICIENT_RESOURCES;
        DxgkpClearMiniportRegistrationPayload(MpCtx);
        ExWaitForRundownProtectionRelease(&MpCtx->RegistrationRundown);
        InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationEmpty);
        goto RegistrationUnlock;
    }

    if (RegistryPath->Length != 0)
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
    if (!MpCtx->UseDodLayout && MpCtx->InitDataSize >= DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateDevice) &&
        MpCtx->InitData.s.DxgkDdiCreateDevice == NULL)
    {
        MpCtx->IsDisplayOnlyDriver = TRUE;
        DXGKRNL_TRACE("DxgkInitializeEx: auto-detected DOD (CreateDevice=NULL)\n");
    }

    KeMemoryBarrier();
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationRegistered);
    Status = STATUS_SUCCESS;
    DXGKRNL_TRACE("DxgkInitializeEx: success — MpCtx %p Version=0x%lX DOD=%d NewContext=%d\n", MpCtx, MpCtx->InitData.s.Version, MpCtx->IsDisplayOnlyDriver, NewContext);

RegistrationUnlock:
    KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
    return Status;
}

/* Explicit-size registration is always the full DRIVER_INITIALIZATION_DATA
 * contract.  The distinct KMDDOD layout enters only through
 * DxgkInitializeDisplayOnlyDriver and is never inferred from a byte count. */
NTSTATUS
APIENTRY
DxgkInitializeEx(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ ULONG                       DriverInitDataSize,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData)
{
    return DxgkpInitializeMiniport(DriverObject, RegistryPath, DriverInitDataSize, DriverInitializationData, FALSE);
}

/*
 * DxgkInitialize
 *
 * Size-less wrapper around DxgkInitializeEx.  Derives the readable append-only
 * prefix from the caller's declared DDI version instead of using dxgkrnl's
 * compile-time structure size.
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
    if (DriverInitializationData == NULL)
        return STATUS_INVALID_PARAMETER;
    return DxgkInitializeEx(DriverObject, RegistryPath, DxgkpFullInitDataPrefixSize(DriverInitializationData->Version), DriverInitializationData);
}

/*
 * DxgkInitializeDisplayOnlyDriver
 *
 * Entry point for WDDM Display-Only Drivers (DOD).  The KMDDOD_INITIALIZATION_DATA
 * is a subset of DRIVER_INITIALIZATION_DATA with only DOD-relevant callbacks.
 * Pass the layout explicitly to the shared registration implementation.
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
    DXGKRNL_TRACE("DxgkInitializeDisplayOnlyDriver: DriverObject %p Version=0x%lX\n",
                  DriverObject, KmDodInitData ? KmDodInitData->Version : 0);

    if (KmDodInitData == NULL)
        return STATUS_INVALID_PARAMETER;
    return DxgkpInitializeMiniport(DriverObject, RegistryPath, DxgkpDodInitDataPrefixSize(KmDodInitData->Version), (PDRIVER_INITIALIZATION_DATA)(PVOID)KmDodInitData, TRUE);
}

/* EOF */
