/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Session lifecycle management
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 *
 * A session wraps a VM, its primary PTY, and attach/detach tracking.
 * Multiple sessions can coexist (up to ROSV_MAX_SESSIONS).
 */

#include <rosv/rosv.h>
#include <rosv/vm.h>
#include <rosv/pty.h>
#include <rosv/device.h>

/* ---- Constants ---------------------------------------------------------- */

#define ROSV_MAX_SESSIONS   4

/* ---- Session structure -------------------------------------------------- */

typedef struct _ROSV_SESSION {
    ULONG SessionId;
    ROSV_SESSION_STATE State;
    PROSV_VM Vm;
    ULONG PrimaryPtyIndex;
    LARGE_INTEGER CreateTime;
    ULONG AttachCount;
    KEVENT StopEvent;
    KSPIN_LOCK Lock;
} ROSV_SESSION, *PROSV_SESSION;

/* ---- Global session table ----------------------------------------------- */

static ROSV_SESSION g_Sessions[ROSV_MAX_SESSIONS];
static KSPIN_LOCK g_SessionTableLock;
static ULONG g_NextSessionId = 1;
static LONG g_SessionTableInitialized = 0;

/* ---- Internal helpers --------------------------------------------------- */

static VOID
RosvSessionTableInit(VOID)
{
    if (InterlockedCompareExchange(&g_SessionTableInitialized, 1, 0) == 0)
    {
        KeInitializeSpinLock(&g_SessionTableLock);
        RtlZeroMemory(g_Sessions, sizeof(g_Sessions));
        ROSV_TRACE("Session table initialized, max=%d", ROSV_MAX_SESSIONS);
    }
}

static PROSV_SESSION
RosvSessionFindById(
    _In_ ULONG SessionId)
{
    ULONG i;

    for (i = 0; i < ROSV_MAX_SESSIONS; i++)
    {
        if (g_Sessions[i].SessionId == SessionId &&
            g_Sessions[i].State != RosvSessionDestroyed)
        {
            return &g_Sessions[i];
        }
    }

    return NULL;
}

static PROSV_SESSION
RosvSessionAllocSlot(VOID)
{
    ULONG i;

    for (i = 0; i < ROSV_MAX_SESSIONS; i++)
    {
        if (g_Sessions[i].SessionId == 0)
        {
            return &g_Sessions[i];
        }
    }

    return NULL;
}

/* ---- Session Create ----------------------------------------------------- */

NTSTATUS
RosvSessionCreate(
    _In_ PROSV_SESSION_CREATE_REQUEST Request,
    _Out_ PROSV_SESSION_CREATE_RESULT Result)
{
    PROSV_SESSION Session;
    ROSV_VM_CONFIG VmConfig;
    NTSTATUS Status;
    KIRQL OldIrql;
    ULONG PtyIndex = 0;

    if (Request == NULL || Result == NULL)
    {
        ROSV_ERR("RosvSessionCreate: invalid parameters (Request=%p Result=%p)",
                 Request, Result);
        return STATUS_INVALID_PARAMETER;
    }

    RosvSessionTableInit();

    ROSV_TRACE("RosvSessionCreate: RamSizeMB=%u rows=%u cols=%u flags=0x%X",
               Request->RamSizeMB, Request->InitialRows,
               Request->InitialCols, Request->Flags);

    Result->SessionId = 0;
    Result->PtyIndex = 0;
    Result->Status = STATUS_UNSUCCESSFUL;

    /* Find a free slot */
    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);
    Session = RosvSessionAllocSlot();
    if (Session == NULL)
    {
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        ROSV_ERR("RosvSessionCreate: No free session slots (max=%d)", ROSV_MAX_SESSIONS);
        Result->Status = STATUS_QUOTA_EXCEEDED;
        return STATUS_QUOTA_EXCEEDED;
    }

    /* Reserve the slot with a session ID */
    Session->SessionId = g_NextSessionId++;
    Session->State = RosvSessionCreated;
    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    ROSV_TRACE("RosvSessionCreate: Allocated session %u in slot", Session->SessionId);

    /* Initialize session fields */
    KeInitializeSpinLock(&Session->Lock);
    KeInitializeEvent(&Session->StopEvent, NotificationEvent, FALSE);
    KeQuerySystemTime(&Session->CreateTime);
    Session->AttachCount = 0;

    /* Create the VM */
    RtlZeroMemory(&VmConfig, sizeof(VmConfig));
    VmConfig.RamSizeMB = Request->RamSizeMB;
    VmConfig.NetBackendType = RosvNetBackendNone;

    Status = RosvVmCreate(&VmConfig, &Session->Vm);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvSessionCreate: RosvVmCreate failed, Status=0x%08X", Status);
        Session->SessionId = 0;
        Session->State = RosvSessionDestroyed;
        Result->Status = Status;
        return Status;
    }

    ROSV_TRACE("RosvSessionCreate: VM created, VmId=%u", Session->Vm->VmId);

    /* Create primary PTY on the VM's PTY manager. */
    {
        ULONG SessionReaderIndex;
        Status = RosvPtyCreate(
            &Session->Vm->PtyManager,
            Request->InitialRows ? Request->InitialRows : 24,
            Request->InitialCols ? Request->InitialCols : 80,
            &PtyIndex,
            &SessionReaderIndex);
    }

    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvSessionCreate: RosvPtyCreate failed, Status=0x%08X", Status);
        RosvVmDestroy(Session->Vm);
        Session->Vm = NULL;
        Session->SessionId = 0;
        Session->State = RosvSessionDestroyed;
        Result->Status = Status;
        return Status;
    }

    Session->PrimaryPtyIndex = PtyIndex;

    ROSV_TRACE("RosvSessionCreate: Session %u created, VM=%u, PTY=%u",
               Session->SessionId, Session->Vm->VmId, PtyIndex);

    Result->SessionId = Session->SessionId;
    Result->PtyIndex = PtyIndex;
    Result->Status = STATUS_SUCCESS;

    return STATUS_SUCCESS;
}

/* ---- Session Start ------------------------------------------------------ */

NTSTATUS
RosvSessionStart(
    _In_ ULONG SessionId)
{
    PROSV_SESSION Session;
    NTSTATUS Status;
    KIRQL OldIrql;

    RosvSessionTableInit();

    ROSV_TRACE("RosvSessionStart: SessionId=%u", SessionId);

    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);
    Session = RosvSessionFindById(SessionId);
    if (Session == NULL)
    {
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        ROSV_ERR("RosvSessionStart: Session %u not found", SessionId);
        return STATUS_NOT_FOUND;
    }

    if (Session->State != RosvSessionCreated)
    {
        ROSV_ERR("RosvSessionStart: Session %u in wrong state %d (expected Created)",
                 SessionId, Session->State);
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Session->State = RosvSessionStarting;
    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    ROSV_TRACE("RosvSessionStart: Session %u transitioning to Starting", SessionId);

    if (Session->Vm == NULL)
    {
        ROSV_ERR("RosvSessionStart: Session %u has no VM instance", SessionId);
        Session->State = RosvSessionCreated;
        return STATUS_INVALID_DEVICE_STATE;
    }

    /* Set memory */
    Status = RosvVmSetMemory(Session->Vm);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvSessionStart: RosvVmSetMemory failed, Status=0x%08X", Status);
        Session->State = RosvSessionCreated;
        return Status;
    }

    ROSV_TRACE("RosvSessionStart: Session %u memory set, VM state=%d",
               SessionId, Session->Vm->State);

    /*
     * Kernel/initrd loading and VM start are done via separate IOCTLs
     * (LOAD_KERNEL, LOAD_INITRD, SET_CMDLINE, START_VM) on the session's VM.
     * Mark the session as running once the VM is started.
     *
     * For now, we leave the session in Starting state. The caller must
     * load kernel/initrd and issue START_VM. We will transition to Running
     * when the VM reaches the Running state.
     */
    Session->State = RosvSessionRunning;
    KeClearEvent(&Session->StopEvent);

    ROSV_TRACE("RosvSessionStart: Session %u is now Running", SessionId);

    return STATUS_SUCCESS;
}

/* ---- Session Stop ------------------------------------------------------- */

NTSTATUS
RosvSessionStop(
    _In_ ULONG SessionId)
{
    PROSV_SESSION Session;
    NTSTATUS Status;
    KIRQL OldIrql;

    RosvSessionTableInit();

    ROSV_TRACE("RosvSessionStop: SessionId=%u", SessionId);

    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);
    Session = RosvSessionFindById(SessionId);
    if (Session == NULL)
    {
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        ROSV_ERR("RosvSessionStop: Session %u not found", SessionId);
        return STATUS_NOT_FOUND;
    }

    if (Session->State != RosvSessionRunning &&
        Session->State != RosvSessionStarting)
    {
        ROSV_ERR("RosvSessionStop: Session %u in wrong state %d",
                 SessionId, Session->State);
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Session->State = RosvSessionStopping;
    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    ROSV_TRACE("RosvSessionStop: Session %u transitioning to Stopping", SessionId);

    /* Stop the VM if it is running */
    if (Session->Vm != NULL &&
        Session->Vm->State == RosvVmStateRunning)
    {
        Status = RosvVmStop(Session->Vm);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("RosvSessionStop: RosvVmStop failed, Status=0x%08X", Status);
            /* Continue to mark stopped even if VM stop had issues */
        }
    }
    else
    {
        ROSV_TRACE("RosvSessionStop: VM not running (state=%d), skipping VmStop",
                   Session->Vm ? Session->Vm->State : -1);
    }

    /* Flush primary PTY */
    if (Session->Vm != NULL &&
        Session->PrimaryPtyIndex < ROSV_PTY_MAX)
    {
        PROSV_PTY_STATE Pty = &Session->Vm->PtyManager.Ptys[Session->PrimaryPtyIndex];
        if (Pty->Flags & RosvPtyFlagAllocated)
        {
            RosvPtyFlush(Pty, ROSV_TCIOFLUSH);
            ROSV_TRACE("RosvSessionStop: Flushed PTY %u", Session->PrimaryPtyIndex);
        }
    }

    Session->State = RosvSessionStopped;
    KeSetEvent(&Session->StopEvent, IO_NO_INCREMENT, FALSE);

    ROSV_TRACE("RosvSessionStop: Session %u is now Stopped", SessionId);

    return STATUS_SUCCESS;
}

/* ---- Session Destroy ---------------------------------------------------- */

NTSTATUS
RosvSessionDestroy(
    _In_ ULONG SessionId)
{
    PROSV_SESSION Session;
    KIRQL OldIrql;

    RosvSessionTableInit();

    ROSV_TRACE("RosvSessionDestroy: SessionId=%u", SessionId);

    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);
    Session = RosvSessionFindById(SessionId);
    if (Session == NULL)
    {
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        ROSV_ERR("RosvSessionDestroy: Session %u not found", SessionId);
        return STATUS_NOT_FOUND;
    }

    if (Session->AttachCount > 0)
    {
        ROSV_WARN("RosvSessionDestroy: Session %u still has %u attachments",
                  SessionId, Session->AttachCount);
    }
    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    /* Stop session if still running */
    if (Session->State == RosvSessionRunning ||
        Session->State == RosvSessionStarting)
    {
        ROSV_TRACE("RosvSessionDestroy: Stopping session %u first", SessionId);
        RosvSessionStop(SessionId);
    }

    /* Destroy PTY */
    if (Session->Vm != NULL &&
        Session->PrimaryPtyIndex < ROSV_PTY_MAX)
    {
        ROSV_TRACE("RosvSessionDestroy: Destroying PTY %u", Session->PrimaryPtyIndex);
        RosvPtyDestroy(&Session->Vm->PtyManager, Session->PrimaryPtyIndex);
    }

    /* Destroy VM */
    if (Session->Vm != NULL)
    {
        ROSV_TRACE("RosvSessionDestroy: Destroying VM %u", Session->Vm->VmId);
        RosvVmDestroy(Session->Vm);
        Session->Vm = NULL;
    }

    /* Clear the slot */
    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);
    Session->State = RosvSessionDestroyed;
    Session->SessionId = 0;
    Session->AttachCount = 0;
    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    ROSV_TRACE("RosvSessionDestroy: Session %u destroyed, slot freed", SessionId);

    return STATUS_SUCCESS;
}

/* ---- Session Attach ----------------------------------------------------- */

NTSTATUS
RosvSessionAttach(
    _In_ ULONG SessionId,
    _Out_ PROSV_SESSION_INFO Info)
{
    PROSV_SESSION Session;
    KIRQL OldIrql;

    if (Info == NULL)
    {
        ROSV_ERR("RosvSessionAttach: Info is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    RosvSessionTableInit();

    ROSV_TRACE("RosvSessionAttach: SessionId=%u", SessionId);

    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);
    Session = RosvSessionFindById(SessionId);
    if (Session == NULL)
    {
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        ROSV_ERR("RosvSessionAttach: Session %u not found", SessionId);
        return STATUS_NOT_FOUND;
    }

    if (Session->State == RosvSessionStopped ||
        Session->State == RosvSessionDestroyed)
    {
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        ROSV_ERR("RosvSessionAttach: Session %u in state %d, cannot attach",
                 SessionId, Session->State);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Session->AttachCount++;
    ROSV_TRACE("RosvSessionAttach: Session %u AttachCount now %u",
               SessionId, Session->AttachCount);

    /* Fill in session info */
    Info->SessionId = Session->SessionId;
    Info->State = Session->State;
    Info->PrimaryPtyIndex = Session->PrimaryPtyIndex;
    Info->AttachCount = Session->AttachCount;
    Info->CreateTime = Session->CreateTime;

    /* Compute uptime */
    {
        LARGE_INTEGER Now;
        KeQuerySystemTime(&Now);
        Info->Uptime.QuadPart = Now.QuadPart - Session->CreateTime.QuadPart;
    }

    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    return STATUS_SUCCESS;
}

/* ---- Session Detach ----------------------------------------------------- */

NTSTATUS
RosvSessionDetach(
    _In_ ULONG SessionId)
{
    PROSV_SESSION Session;
    KIRQL OldIrql;

    RosvSessionTableInit();

    ROSV_TRACE("RosvSessionDetach: SessionId=%u", SessionId);

    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);
    Session = RosvSessionFindById(SessionId);
    if (Session == NULL)
    {
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        ROSV_ERR("RosvSessionDetach: Session %u not found", SessionId);
        return STATUS_NOT_FOUND;
    }

    if (Session->AttachCount == 0)
    {
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        ROSV_WARN("RosvSessionDetach: Session %u already has AttachCount=0", SessionId);
        return STATUS_SUCCESS;
    }

    Session->AttachCount--;
    ROSV_TRACE("RosvSessionDetach: Session %u AttachCount now %u",
               SessionId, Session->AttachCount);

    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    return STATUS_SUCCESS;
}

/* ---- Session List ------------------------------------------------------- */

NTSTATUS
RosvSessionList(
    _Out_ PROSV_SESSION_LIST_RESULT Result,
    _In_ ULONG MaxOutputSize,
    _Out_ PULONG OutputBytes)
{
    ULONG i;
    ULONG Count = 0;
    ULONG MaxSessions;
    KIRQL OldIrql;
    LARGE_INTEGER Now;

    if (Result == NULL || OutputBytes == NULL)
    {
        ROSV_ERR("RosvSessionList: invalid parameters (Result=%p OutputBytes=%p)",
                 Result, OutputBytes);
        return STATUS_INVALID_PARAMETER;
    }

    RosvSessionTableInit();

    ROSV_TRACE("RosvSessionList: MaxOutputSize=%u", MaxOutputSize);

    /* Calculate how many session entries fit in the output buffer */
    if (MaxOutputSize < FIELD_OFFSET(ROSV_SESSION_LIST_RESULT, Sessions))
    {
        ROSV_ERR("RosvSessionList: Output buffer too small (%u)", MaxOutputSize);
        *OutputBytes = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    MaxSessions = (MaxOutputSize - FIELD_OFFSET(ROSV_SESSION_LIST_RESULT, Sessions))
                  / sizeof(ROSV_SESSION_INFO);
    if (MaxSessions > ROSV_MAX_SESSIONS)
        MaxSessions = ROSV_MAX_SESSIONS;

    KeQuerySystemTime(&Now);

    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);

    for (i = 0; i < ROSV_MAX_SESSIONS && Count < MaxSessions; i++)
    {
        if (g_Sessions[i].SessionId != 0 &&
            g_Sessions[i].State != RosvSessionDestroyed)
        {
            Result->Sessions[Count].SessionId = g_Sessions[i].SessionId;
            Result->Sessions[Count].State = g_Sessions[i].State;
            Result->Sessions[Count].PrimaryPtyIndex = g_Sessions[i].PrimaryPtyIndex;
            Result->Sessions[Count].AttachCount = g_Sessions[i].AttachCount;
            Result->Sessions[Count].CreateTime = g_Sessions[i].CreateTime;
            Result->Sessions[Count].Uptime.QuadPart =
                Now.QuadPart - g_Sessions[i].CreateTime.QuadPart;
            Count++;
        }
    }

    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    Result->Count = Count;
    *OutputBytes = FIELD_OFFSET(ROSV_SESSION_LIST_RESULT, Sessions)
                   + Count * sizeof(ROSV_SESSION_INFO);

    ROSV_TRACE("RosvSessionList: Returning %u sessions (%u bytes)", Count, *OutputBytes);

    return STATUS_SUCCESS;
}

/* ---- Session Get Info --------------------------------------------------- */

NTSTATUS
RosvSessionGetInfo(
    _In_ ULONG SessionId,
    _Out_ PROSV_SESSION_INFO Info)
{
    PROSV_SESSION Session;
    KIRQL OldIrql;
    LARGE_INTEGER Now;

    if (Info == NULL)
    {
        ROSV_ERR("RosvSessionGetInfo: Info is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    RosvSessionTableInit();

    ROSV_TRACE("RosvSessionGetInfo: SessionId=%u", SessionId);

    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);
    Session = RosvSessionFindById(SessionId);
    if (Session == NULL)
    {
        KeReleaseSpinLock(&g_SessionTableLock, OldIrql);
        ROSV_ERR("RosvSessionGetInfo: Session %u not found", SessionId);
        return STATUS_NOT_FOUND;
    }

    KeQuerySystemTime(&Now);

    Info->SessionId = Session->SessionId;
    Info->State = Session->State;
    Info->PrimaryPtyIndex = Session->PrimaryPtyIndex;
    Info->AttachCount = Session->AttachCount;
    Info->CreateTime = Session->CreateTime;
    Info->Uptime.QuadPart = Now.QuadPart - Session->CreateTime.QuadPart;

    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    ROSV_TRACE("RosvSessionGetInfo: Session %u state=%d attach=%u pty=%u",
               SessionId, Info->State, Info->AttachCount, Info->PrimaryPtyIndex);

    return STATUS_SUCCESS;
}

/* ---- Session Cleanup (for handle close) --------------------------------- */

VOID
RosvSessionCleanupAll(VOID)
{
    if (!g_SessionTableInitialized)
        return;

    /*
     * Cleanup is per-file-object, but AttachCount is currently tracked per
     * session globally. Do not mutate attach counters here until per-handle
     * attachment ownership is implemented.
     */
    ROSV_TRACE("RosvSessionCleanupAll: no-op (per-handle attachment tracking not implemented)");
}

/* ---- Session Get VM (for routing IOCTLs to session's VM) ---------------- */

PROSV_VM
RosvSessionGetVm(
    _In_ ULONG SessionId)
{
    PROSV_SESSION Session;
    PROSV_VM Vm = NULL;
    KIRQL OldIrql;

    if (!g_SessionTableInitialized)
        return NULL;

    KeAcquireSpinLock(&g_SessionTableLock, &OldIrql);
    Session = RosvSessionFindById(SessionId);
    if (Session != NULL)
    {
        Vm = Session->Vm;
    }
    KeReleaseSpinLock(&g_SessionTableLock, OldIrql);

    return Vm;
}
