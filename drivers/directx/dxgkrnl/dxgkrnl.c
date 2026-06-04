/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Module entry point and global state
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * DriverEntry
 * -----------
 * dxgkrnl.sys is a port driver that miniport drivers call into via
 * DxgkInitialize() during their own DriverEntry.  dxgkrnl's own
 * DriverEntry only:
 *
 *   1. Creates the control device object (\Device\DxgKrnl) for IOCTL-
 *      based communication with DXGI / D3D user-mode stubs.
 *   2. Installs the IRP_MJ_DEVICE_CONTROL and IRP_MJ_CREATE/CLOSE
 *      dispatch routines on that object.
 *   3. Initialises the global adapter list lock.
 *   4. Seeds the D3DKMT handle obfuscation cookie.
 *
 * DxgkInitialize / DxgkInitializeEx are called later by each miniport
 * and hook the miniport's own DriverObject.  Those functions live in
 * adapter.c.
 */

/* The PCH already defines NDEBUG and includes <debug.h> + "debug.h". */
#include "dxgkrnl_private.h"

#include "context.h"

/* ========================================================================
 * Global state
 * ====================================================================== */

/*
 * DxgkAdapterGlobalListLock / DxgkAdapterGlobalListHead
 *
 * System-wide list of all active DXGKRNL_ADAPTER instances.
 * Declared extern in dxgkrnl_private.h; defined here (single translation
 * unit owns the definition).
 */
KSPIN_LOCK   DxgkAdapterGlobalListLock;
LIST_ENTRY   DxgkAdapterGlobalListHead;

/*
 * GDxgControlDeviceObject
 *
 * The \\Device\\DxgKrnl device object used to service D3DKMT IOCTLs.
 */
PDEVICE_OBJECT GDxgControlDeviceObject = NULL;

/*
 * GDxgmms1Interface
 *
 * Function table registered by dxgmms1.sys at load time.  NULL until then.
 */
PVOID GDxgmms1Interface = NULL;

/* ========================================================================
 * DxgCoreInterface — static callback table exported for dxgmms1.sys
 *
 * This is a 184-byte structure (8-byte header + 22 function pointers)
 * matching the Win7 dxgkrnl.sys layout.  dxgmms1.sys imports this data
 * symbol to access dxgkrnl's DxgkCb* callbacks without holding an
 * adapter handle.
 *
 * The table is populated with stubs initially; the per-adapter version
 * in DxgkpFillInterface (adapter.c) sets adapter-aware callbacks at
 * StartDevice time.  dxgmms1 uses this global table for adapter-
 * independent operations.
 *
 * Layout (all pointers are non-paged / DISPATCH_LEVEL safe):
 *   +0x000: ULONG Size = 0xB8 (184)
 *   +0x004: ULONG Version = 1
 *   +0x008: 22 function pointers (slots 0-21)
 * ====================================================================== */

/*
 * DxgCoreInterface stub callbacks — used for slots that need to return
 * STATUS_NOT_SUPPORTED when no adapter context is available.
 */
static NTSTATUS APIENTRY DxgkCoreStubNotSupported(VOID)
{
    return STATUS_NOT_SUPPORTED;
}

static VOID APIENTRY DxgkCoreStubVoid(VOID)
{
}

typedef struct _DXGCORE_INTERFACE
{
    ULONG Size;
    ULONG Version;
    PVOID Slots[22];
} DXGCORE_INTERFACE;

/*
 * Exported as a data symbol.  Initially filled with stubs.
 * DxgkpInitializeCoreInterface() is called from DriverEntry to populate
 * with the real callbacks (from adapter.c's global implementations).
 */
DXGCORE_INTERFACE DxgCoreInterface = {
    sizeof(DXGCORE_INTERFACE),  /* Size = 0xB8 = 184 */
    1,                          /* Version = 1 */
    { NULL }                    /* Slots filled at init time */
};

static VOID
DxgkpInitializeCoreInterface(VOID)
{
    /* Forward declarations from adapter.c (all non-paged) */
    extern NTSTATUS APIENTRY DxgkCbNotifyInterrupt(HANDLE, CONST DXGKARGCB_NOTIFY_INTERRUPT_DATA*);
    extern VOID APIENTRY DxgkCbNotifyDpc(HANDLE);
    extern NTSTATUS APIENTRY DxgkCbGetDeviceInformation(HANDLE, PDXGK_DEVICE_INFO);
    extern NTSTATUS APIENTRY DxgkCbIndicateChildStatus(HANDLE, PDXGK_CHILD_STATUS);
    extern NTSTATUS APIENTRY DxgkCbMapMemory(HANDLE, PHYSICAL_ADDRESS, ULONG, BOOLEAN, BOOLEAN, MEMORY_CACHING_TYPE, PVOID*);
    extern NTSTATUS APIENTRY DxgkCbUnmapMemory(HANDLE, PVOID);
    extern NTSTATUS APIENTRY DxgkCbReadDeviceSpace(HANDLE, ULONG, PVOID, ULONG, ULONG, PULONG);
    extern NTSTATUS APIENTRY DxgkCbWriteDeviceSpace(HANDLE, ULONG, PVOID, ULONG, ULONG, PULONG);
    extern NTSTATUS APIENTRY DxgkCbAcquirePostDisplayOwnership(HANDLE, PVOID);

    /*
     * Slot order matches the Win7 report's DxgCoreInterface layout:
     *   0: EvalAcpiMethod           12: GetCaptureAddress
     *   1: GetDeviceInformation     13: IsDevicePresent
     *   2: IndicateChildStatus      14: ReadDeviceSpace
     *   3: LogEtwEvent              15: WriteDeviceSpace
     *   4: ExcludeAdapterAccess     16: MapMemory
     *   5: GetHandleData            17: UnmapMemory
     *   6: GetHandleParent          18: AcquirePostDisplayOwnership
     *   7: EnumHandleChildren       19: PowerRuntimeControlRequest
     *   8: NotifyInterrupt          20: SetPowerComponentLatency
     *   9: NotifyDpc                21: SetPowerComponentResidency
     *  10: QueryVidPnInterface
     *  11: QueryMonitorInterface
     */
    DxgCoreInterface.Slots[0]  = (PVOID)DxgkCoreStubNotSupported;  /* EvalAcpiMethod */
    DxgCoreInterface.Slots[1]  = (PVOID)DxgkCbGetDeviceInformation;
    DxgCoreInterface.Slots[2]  = (PVOID)DxgkCbIndicateChildStatus;
    DxgCoreInterface.Slots[3]  = (PVOID)DxgkCoreStubNotSupported;  /* LogEtwEvent */
    DxgCoreInterface.Slots[4]  = (PVOID)DxgkCoreStubNotSupported;  /* ExcludeAdapterAccess */
    DxgCoreInterface.Slots[5]  = (PVOID)DxgkCoreStubNotSupported;  /* GetHandleData */
    DxgCoreInterface.Slots[6]  = (PVOID)DxgkCoreStubNotSupported;  /* GetHandleParent */
    DxgCoreInterface.Slots[7]  = (PVOID)DxgkCoreStubNotSupported;  /* EnumHandleChildren */
    DxgCoreInterface.Slots[8]  = (PVOID)DxgkCbNotifyInterrupt;
    DxgCoreInterface.Slots[9]  = (PVOID)DxgkCbNotifyDpc;
    DxgCoreInterface.Slots[10] = (PVOID)DxgkCoreStubNotSupported;  /* QueryVidPnInterface */
    DxgCoreInterface.Slots[11] = (PVOID)DxgkCoreStubNotSupported;  /* QueryMonitorInterface */
    DxgCoreInterface.Slots[12] = (PVOID)DxgkCoreStubNotSupported;  /* GetCaptureAddress */
    DxgCoreInterface.Slots[13] = (PVOID)DxgkCoreStubNotSupported;  /* IsDevicePresent */
    DxgCoreInterface.Slots[14] = (PVOID)DxgkCbReadDeviceSpace;
    DxgCoreInterface.Slots[15] = (PVOID)DxgkCbWriteDeviceSpace;
    DxgCoreInterface.Slots[16] = (PVOID)DxgkCbMapMemory;
    DxgCoreInterface.Slots[17] = (PVOID)DxgkCbUnmapMemory;
    DxgCoreInterface.Slots[18] = (PVOID)DxgkCbAcquirePostDisplayOwnership;
    DxgCoreInterface.Slots[19] = (PVOID)DxgkCoreStubNotSupported;  /* PowerRuntimeControlRequest */
    DxgCoreInterface.Slots[20] = (PVOID)DxgkCoreStubVoid;          /* SetPowerComponentLatency */
    DxgCoreInterface.Slots[21] = (PVOID)DxgkCoreStubVoid;          /* SetPowerComponentResidency */
}

/* ========================================================================
 * TDR (Timeout Detection and Recovery) exports
 *
 * These functions are consumed by dxgmms1.sys (GPU scheduler) to
 * coordinate GPU hang detection and recovery.
 *
 * TDR recovery context layout (0xB30 bytes, tag "vTDR"):
 *   +0x00: ULONG  Signature1       — "vTDR" (0x52445476)
 *   +0x04: ULONG  Reserved04
 *   +0x08: PVOID  Reserved08
 *   +0x10: ULONG  RecoveryType
 *   +0x18: ULONGLONG Reserved18     — copied to history slot field 1
 *   +0x20: PVOID  SchedulerContext
 *   +0x28: PVOID  SchedulerPtr
 *   +0x30: ULONGLONG Reserved30     — copied to history slot field 2
 *   +0x38: ULONGLONG Reserved38     — copied to history slot field 3
 *   +0x40: ULONGLONG Reserved40     — copied to history slot field 4
 *   +0x58: ULONG  Signature2       — "vTDR" (0x52445476)
 *   +0x5C: ULONG  Flags
 *   +0x60: ULONGLONG Timestamp      — SharedUserData tick at creation
 *   +0x68: ULONG  TimeoutMultiplier — default 0x10
 *   +0xAC8: ULONG TdrDelay         — copied from g_TdrConfig
 *   +0xACC: ULONG TdrDdiDelay      — copied from g_TdrConfig
 *   +0xAD0: ULONG TdrLevel         — copied from g_TdrConfig
 *   +0xAF0: ULONG Signature3       — "vTDR" (0x52445476)
 *
 * TDR history buffer layout (0xA18 bytes):
 *   +0x00: ULONGLONG Reserved
 *   +0x08: ULONGLONG Reserved
 *   +0x10: ULONG  TimeIncrement    — from KeQueryTimeIncrement
 *   +0x14: LONG   WriteIndex       — atomic circular index (AND 0x3F)
 *   +0x18: TDR_HISTORY_ENTRY[64]   — each entry is 40 bytes (5 QWORDs)
 * ====================================================================== */

/*
 * g_TdrConfig — TDR configuration read from registry.
 * GraphicsDrivers\TdrDelay, TdrDdiDelay, TdrLevel, etc.
 */
typedef struct _TDR_CONFIG
{
    ULONG TdrDelay;         /* seconds before declaring timeout (default 2) */
    ULONG TdrDdiDelay;      /* DDI-specific timeout (default 5) */
    ULONG TdrLevel;         /* 0=off, 1=bugcheck, 3=recover */
    ULONG TdrLimitCount;    /* max recoveries in TdrLimitTime */
    ULONG TdrLimitTime;     /* time window (seconds) for recovery counting */
    ULONG TdrDebugMode;     /* debugger break policy (0x00-0x03) */
    ULONG TdrTestMode;      /* testing overrides */
} TDR_CONFIG;

TDR_CONFIG g_TdrConfig = {
    2,      /* TdrDelay */
    5,      /* TdrDdiDelay */
    3,      /* TdrLevel = recover */
    5,      /* TdrLimitCount */
    60,     /* TdrLimitTime */
    0,      /* TdrDebugMode = continue */
    0       /* TdrTestMode = none */
};

/*
 * g_TdrForceTimeout — controls forced timeout for testing/debugging.
 */
ULONG g_TdrForceTimeout = 0;

/*
 * g_TdrForceDodPresentTimeout — forced DOD present timeout override.
 * Read atomically via XCHG by the timeout check path.
 */
LONG g_TdrForceDodPresentTimeout = 0;

/*
 * g_TdrForceDodVSyncTimeout — forced DOD VSync timeout override.
 * Read atomically via XCHG by the timeout check path.
 */
LONG g_TdrForceDodVSyncTimeout = 0;

/*
 * g_bVSyncEnabledForLogging — VSync logging flag for ETW tracing.
 */
BOOLEAN g_bVSyncEnabledForLogging = FALSE;

/* Pool tag for TDR recovery context allocations ("vTDR" = 0x52445476) */
#define TAG_TDR_CONTEXT 0x52445476

/* Recovery context size matching Win8.1 */
#define TDR_RECOVERY_CONTEXT_SIZE   0xB30

/* History buffer constants */
#define TDR_HISTORY_BUFFER_SIZE     0xA18
#define TDR_HISTORY_ENTRY_COUNT     64
#define TDR_HISTORY_ENTRY_QWORDS    5
#define TDR_HISTORY_ENTRY_BYTES     (TDR_HISTORY_ENTRY_QWORDS * sizeof(ULONGLONG))

/* Recovery context field offsets */
#define TDR_CTX_SIGNATURE1      0x00
#define TDR_CTX_RECOVERY_TYPE   0x10
#define TDR_CTX_SCHEDULER_PTR   0x28
#define TDR_CTX_SIGNATURE2      0x58
#define TDR_CTX_FLAGS           0x5C
#define TDR_CTX_TIMESTAMP       0x60
#define TDR_CTX_TIMEOUT_MULT    0x68
#define TDR_CTX_TDR_DELAY       0xAC8
#define TDR_CTX_TDR_DDI_DELAY   0xACC
#define TDR_CTX_TDR_LEVEL       0xAD0
#define TDR_CTX_SIGNATURE3      0xAF0

/* History buffer field offsets */
#define TDR_HIST_TIME_INCREMENT 0x10
#define TDR_HIST_WRITE_INDEX    0x14
#define TDR_HIST_ENTRIES        0x18

/* Recovery context offsets copied into history entries */
#define TDR_CTX_HIST_FIELD0     0x60   /* Timestamp */
#define TDR_CTX_HIST_FIELD1     0x10   /* RecoveryType (as ULONGLONG) */
#define TDR_CTX_HIST_FIELD2     0x30
#define TDR_CTX_HIST_FIELD3     0x38
#define TDR_CTX_HIST_FIELD4     0x40

/* Atomic pointer for single active recovery context */
static PVOID g_TdrActiveRecoveryContext = NULL;

/*
 * TdrIsEnabled
 *
 * Returns TRUE if TDR is enabled (TdrLevel != 0).
 * Consumed by dxgmms1.sys to decide whether timeout detection is active.
 */
BOOLEAN
NTAPI
TdrIsEnabled(VOID)
{
    return (g_TdrConfig.TdrLevel != 0) ? TRUE : FALSE;
}

/*
 * TdrHistoryInit
 *
 * Initialises a TDR history buffer (0xA18 bytes).
 * Zeroes the buffer and stores KeQueryTimeIncrement at offset +0x10.
 */
NTSTATUS
NTAPI
TdrHistoryInit(
    _Out_writes_bytes_(TDR_HISTORY_BUFFER_SIZE) PVOID HistoryBuffer)
{
    ULONG TimeIncrement;

    RtlZeroMemory(HistoryBuffer, TDR_HISTORY_BUFFER_SIZE);

    TimeIncrement = KeQueryTimeIncrement();
    *(PULONG)((PUCHAR)HistoryBuffer + TDR_HIST_TIME_INCREMENT) = TimeIncrement;

    return STATUS_SUCCESS;
}

/*
 * TdrHistoryUpdate
 *
 * Appends a recovery event to the circular TDR history buffer.
 * Uses atomic increment for the write index (mod 64).
 * Copies 5 QWORD fields from the recovery context into the history slot.
 */
VOID
NTAPI
TdrHistoryUpdate(
    _Inout_ PVOID HistoryBuffer,
    _In_ PVOID RecoveryContext)
{
    LONG Index;
    LONG Slot;
    PUCHAR CtxBase;
    PULONGLONG EntryBase;

    /* Atomically claim the next slot */
    Index = InterlockedIncrement((PLONG)((PUCHAR)HistoryBuffer + TDR_HIST_WRITE_INDEX)) - 1;
    Slot = Index & (TDR_HISTORY_ENTRY_COUNT - 1);  /* mod 64 */

    CtxBase = (PUCHAR)RecoveryContext;
    EntryBase = (PULONGLONG)((PUCHAR)HistoryBuffer + TDR_HIST_ENTRIES +
                             (ULONG_PTR)Slot * TDR_HISTORY_ENTRY_BYTES);

    /* Copy 5 QWORD fields from recovery context into the history entry */
    EntryBase[0] = *(PULONGLONG)(CtxBase + TDR_CTX_HIST_FIELD0);
    EntryBase[1] = *(PULONGLONG)(CtxBase + TDR_CTX_HIST_FIELD1);
    EntryBase[2] = *(PULONGLONG)(CtxBase + TDR_CTX_HIST_FIELD2);
    EntryBase[3] = *(PULONGLONG)(CtxBase + TDR_CTX_HIST_FIELD3);
    EntryBase[4] = *(PULONGLONG)(CtxBase + TDR_CTX_HIST_FIELD4);
}

/*
 * TdrHistoryIsLimitExhausted
 *
 * Walks backwards through the circular history buffer, counting recovery
 * events within the TdrLimitTime window.  Returns TRUE if the count
 * (optionally including the current recovery) meets or exceeds TdrLimitCount.
 */
BOOLEAN
NTAPI
TdrHistoryIsLimitExhausted(
    _In_ PVOID HistoryBuffer,
    _In_ PVOID RecoveryContext,
    _In_ BOOLEAN IncludeCurrentRecovery)
{
    ULONGLONG LimitWindow;
    ULONGLONG CurrentTimestamp;
    LONG WriteIndex;
    LONG Slot;
    LONG i;
    ULONG Count;
    PULONGLONG EntryBase;
    ULONGLONG EntryTimestamp;

    UNREFERENCED_PARAMETER(RecoveryContext);

    /* Convert TdrLimitTime (seconds) to 100ns units */
    LimitWindow = (ULONGLONG)g_TdrConfig.TdrLimitTime * 10000000ULL;

    /* Read current timestamp from SharedUserData (same source as creation) */
    CurrentTimestamp = *((volatile PULONGLONG)((PUCHAR)(ULONG_PTR)0xFFFFF78000000320ULL));

    /* Read current write index */
    WriteIndex = *(volatile PLONG)((PUCHAR)HistoryBuffer + TDR_HIST_WRITE_INDEX);
    WriteIndex = (WriteIndex - 1) & (TDR_HISTORY_ENTRY_COUNT - 1);

    Count = IncludeCurrentRecovery ? 1 : 0;

    /* Walk backwards through the 64-entry circular buffer */
    for (i = 0; i < TDR_HISTORY_ENTRY_COUNT; i++)
    {
        Slot = (WriteIndex - i) & (TDR_HISTORY_ENTRY_COUNT - 1);
        EntryBase = (PULONGLONG)((PUCHAR)HistoryBuffer + TDR_HIST_ENTRIES +
                                 (ULONG_PTR)Slot * TDR_HISTORY_ENTRY_BYTES);

        /* Entry field 0 is the timestamp */
        EntryTimestamp = EntryBase[0];
        if (EntryTimestamp == 0)
            continue;  /* empty slot */

        /* Check if this entry is within the time window */
        if (CurrentTimestamp >= EntryTimestamp &&
            (CurrentTimestamp - EntryTimestamp) <= LimitWindow)
        {
            Count++;
        }
    }

    return (Count >= g_TdrConfig.TdrLimitCount) ? TRUE : FALSE;
}

/*
 * TdrCreateRecoveryContext
 *
 * Allocates and initialises a 0xB30-byte TDR recovery context.
 * Called when a GPU hang is first detected.
 */
NTSTATUS
NTAPI
TdrCreateRecoveryContext(
    _Out_ PVOID *RecoveryContext,
    _In_  PVOID AdapterContext)
{
    PUCHAR Ctx;

    UNREFERENCED_PARAMETER(AdapterContext);

    if (RecoveryContext == NULL)
        return STATUS_INVALID_PARAMETER;

    Ctx = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                         TDR_RECOVERY_CONTEXT_SIZE,
                                         TAG_TDR_CONTEXT);
    if (Ctx == NULL)
    {
        DXGKRNL_ERR("TdrCreateRecoveryContext: allocation failed (0x%X bytes)\n",
                     TDR_RECOVERY_CONTEXT_SIZE);
        *RecoveryContext = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Ctx, TDR_RECOVERY_CONTEXT_SIZE);

    /* Set "vTDR" signatures at three locations */
    *(PULONG)(Ctx + TDR_CTX_SIGNATURE1) = TAG_TDR_CONTEXT;
    *(PULONG)(Ctx + TDR_CTX_SIGNATURE2) = TAG_TDR_CONTEXT;
    *(PULONG)(Ctx + TDR_CTX_SIGNATURE3) = TAG_TDR_CONTEXT;

    /* Default timeout multiplier */
    *(PULONG)(Ctx + TDR_CTX_TIMEOUT_MULT) = 0x10;

    /* Copy TDR configuration */
    *(PULONG)(Ctx + TDR_CTX_TDR_DELAY)     = g_TdrConfig.TdrDelay;
    *(PULONG)(Ctx + TDR_CTX_TDR_DDI_DELAY) = g_TdrConfig.TdrDdiDelay;
    *(PULONG)(Ctx + TDR_CTX_TDR_LEVEL)     = g_TdrConfig.TdrLevel;

    /* Store creation timestamp from SharedUserData */
    *(PULONGLONG)(Ctx + TDR_CTX_TIMESTAMP) =
        *((volatile PULONGLONG)((PUCHAR)(ULONG_PTR)0xFFFFF78000000320ULL));

    *RecoveryContext = (PVOID)Ctx;
    return STATUS_SUCCESS;
}

/*
 * TdrCompleteRecoveryContext
 *
 * Called after the miniport completes its reset sequence.
 * Clears the active recovery pointer and frees the context.
 */
NTSTATUS
NTAPI
TdrCompleteRecoveryContext(
    _In_ PVOID RecoveryContext)
{
    if (RecoveryContext == NULL)
        return STATUS_SUCCESS;

    /* Atomically clear the active recovery pointer if it matches */
    InterlockedCompareExchangePointer(&g_TdrActiveRecoveryContext,
                                      NULL,
                                      RecoveryContext);

    /* Free the recovery context allocation */
    ExFreePoolWithTag(RecoveryContext, TAG_TDR_CONTEXT);

    return STATUS_SUCCESS;
}

/*
 * TdrIsRecoveryRequired
 *
 * Returns whether a pending timeout needs GPU recovery.
 * Checks TdrLevel and atomically claims the active recovery slot.
 */
BOOLEAN
NTAPI
TdrIsRecoveryRequired(
    _In_ PVOID RecoveryContext)
{
    PVOID OldContext;

    /* TDR disabled — never recover */
    if (g_TdrConfig.TdrLevel == 0)
        return FALSE;

    /* Try to atomically set ourselves as the active recovery context.
     * If another recovery is already in progress (non-NULL), bail out. */
    OldContext = InterlockedCompareExchangePointer(&g_TdrActiveRecoveryContext,
                                                   RecoveryContext,
                                                   NULL);
    if (OldContext != NULL)
        return FALSE;  /* concurrent recovery in progress */

    return TRUE;
}

/*
 * TdrIsTimeoutForcedFlip
 *
 * Checks whether the current timeout is a forced-flip scenario.
 * Matches Win8.1 behavior: always returns FALSE.
 */
BOOLEAN
NTAPI
TdrIsTimeoutForcedFlip(
    _In_ PVOID RecoveryContext)
{
    UNREFERENCED_PARAMETER(RecoveryContext);
    return FALSE;
}

/*
 * TdrResetFromTimeout
 *
 * Initiates the actual GPU reset sequence.  Core TDR entry point.
 * Currently a stub — full implementation requires scheduler coordination.
 */
NTSTATUS
NTAPI
TdrResetFromTimeout(
    _In_ PVOID RecoveryContext)
{
    if (RecoveryContext == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_IMPLEMENTED;
}

/*
 * TdrAllowToDebugEngineTimeout
 *
 * Returns TRUE if the debugger should be allowed to handle the timeout
 * (i.e., TDR should not reset while debugging).
 * Checks debugger presence and g_TdrConfig.TdrDebugMode.
 */
BOOLEAN
NTAPI
TdrAllowToDebugEngineTimeout(
    _In_ PVOID Adapter)
{
    UNREFERENCED_PARAMETER(Adapter);

    /* If a kernel debugger is attached and TdrDebugMode allows it,
     * let the debugger handle the timeout instead of resetting. */
    if (KD_DEBUGGER_NOT_PRESENT)
        return FALSE;

    /* TdrDebugMode 0 = continue (no debug break)
     * TdrDebugMode 1 = break on timeout
     * TdrDebugMode 2 = break on recovery
     * TdrDebugMode 3 = break on both */
    if (g_TdrConfig.TdrDebugMode == 0)
        return FALSE;

    return TRUE;
}

/*
 * TdrCollectDbgInfoStage1 / TdrCollectDbgInfoStage2
 *
 * Collect debug information about a GPU hang.  Depends on watchdog.sys
 * infrastructure which is not present in ReactOS.  Stubs for now.
 */
NTSTATUS
NTAPI
TdrCollectDbgInfoStage1(
    _In_ PVOID RecoveryContext)
{
    if (RecoveryContext == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
TdrCollectDbgInfoStage2(
    _In_ PVOID RecoveryContext)
{
    if (RecoveryContext == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_SUPPORTED;
}

/*
 * TdrUpdateDbgReport
 *
 * Updates the WatchDog debug report with TDR state.
 * Depends on watchdog.sys — stub for now.
 */
VOID
NTAPI
TdrUpdateDbgReport(
    _In_ PVOID RecoveryContext,
    _In_ ULONG Stage)
{
    UNREFERENCED_PARAMETER(RecoveryContext);
    UNREFERENCED_PARAMETER(Stage);
}

/*
 * DxgkVidMmAllowFailOnOfferReclaimErrors
 *
 * Queried by dxgmms1.sys VidMm to determine whether offer/reclaim
 * allocation failures should be treated as errors or silently ignored.
 * Returns TRUE (allow failures) when no adapter is present or when
 * the adapter policy permits it.
 */
BOOLEAN
NTAPI
DxgkVidMmAllowFailOnOfferReclaimErrors(VOID)
{
    /*
     * Win8.1 queries the current adapter and calls a VidMm policy callback.
     * In ReactOS we return TRUE (permissive) because the VidMm scheduler
     * integration is not yet complete and we want offer/reclaim failures
     * to be non-fatal.
     */
    return TRUE;
}

/* ========================================================================
 * Display Port / Scheduler bridge exports
 *
 * Provide the bridge between dxgkrnl and dxgmms1.sys's scheduler.
 * ====================================================================== */

/*
 * DpSynchronizeExecution
 *
 * Synchronises execution with the display adapter's interrupt.
 * Wrapper around KeSynchronizeExecution for the adapter's interrupt object.
 */
NTSTATUS
NTAPI
DpSynchronizeExecution(
    _In_ PVOID AdapterContext,
    _In_ PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    _In_ PVOID SynchronizeContext,
    _In_ ULONG MessageNumber,
    _Out_ PBOOLEAN ReturnValue)
{
    PDXGKRNL_ADAPTER Adapter;

    if (AdapterContext == NULL || SynchronizeRoutine == NULL || ReturnValue == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = (PDXGKRNL_ADAPTER)AdapterContext;

    return DxgkCbSynchronizeExecution((HANDLE)Adapter,
                                      SynchronizeRoutine,
                                      SynchronizeContext,
                                      MessageNumber,
                                      ReturnValue);
}

/*
 * DpiGetDriverVersion
 *
 * Returns the WDDM driver version/feature level supported by the miniport.
 */
ULONG
NTAPI
DpiGetDriverVersion(
    _In_ PVOID AdapterContext)
{
    UNREFERENCED_PARAMETER(AdapterContext);

    /* Full WDDM miniports are capped to the Win7/WDDM 1.1 contract. */
    return DXGKDDI_INTERFACE_VERSION_WIN7;
}

/*
 * DpiGetDxgAdapter
 *
 * Returns the internal DXG_ADAPTER object pointer.
 */
PVOID
NTAPI
DpiGetDxgAdapter(
    _In_ PVOID AdapterContext)
{
    return AdapterContext;
}

/*
 * DpiGetSchedulerCallbackState
 *
 * Queries whether scheduler callbacks are enabled.
 */
NTSTATUS
NTAPI
DpiGetSchedulerCallbackState(
    _In_  PVOID AdapterContext,
    _Out_ PBOOLEAN Enabled)
{
    UNREFERENCED_PARAMETER(AdapterContext);

    if (Enabled == NULL)
        return STATUS_INVALID_PARAMETER;

    *Enabled = TRUE;
    return STATUS_SUCCESS;
}

/*
 * DpiSetSchedulerCallbackState
 *
 * Sets the scheduler callback state (enable/disable).
 */
NTSTATUS
NTAPI
DpiSetSchedulerCallbackState(
    _In_ PVOID AdapterContext,
    _In_ BOOLEAN Enable)
{
    UNREFERENCED_PARAMETER(AdapterContext);
    UNREFERENCED_PARAMETER(Enable);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * SQM telemetry stubs
 *
 * Software Quality Metrics instrumentation.  No-op stubs for ReactOS.
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkSqmAddToStream(
    _In_ PVOID Stream,
    _In_ ULONG DataType,
    _In_ PVOID Data,
    _In_ ULONG DataSize)
{
    UNREFERENCED_PARAMETER(Stream);
    UNREFERENCED_PARAMETER(DataType);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(DataSize);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkSqmCommonGeneric(
    _In_ ULONG DataPointId,
    _In_ PVOID Data,
    _In_ ULONG DataSize)
{
    UNREFERENCED_PARAMETER(DataPointId);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(DataSize);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkSqmCreateDwordStreamEntry(
    _In_ PVOID Stream,
    _In_ ULONG DataPointId,
    _In_ ULONG Value)
{
    UNREFERENCED_PARAMETER(Stream);
    UNREFERENCED_PARAMETER(DataPointId);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkSqmCreateStringStreamEntry(
    _In_ PVOID Stream,
    _In_ ULONG DataPointId,
    _In_ PVOID String)
{
    UNREFERENCED_PARAMETER(Stream);
    UNREFERENCED_PARAMETER(DataPointId);
    UNREFERENCED_PARAMETER(String);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkSqmGenericDword(
    _In_ ULONG DataPointId,
    _In_ ULONG Value)
{
    UNREFERENCED_PARAMETER(DataPointId);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkSqmGenericDword64(
    _In_ ULONG DataPointId,
    _In_ ULONGLONG Value)
{
    UNREFERENCED_PARAMETER(DataPointId);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_SUCCESS;
}

/*
 * DxgkSqmGenericString — no-op telemetry stub.
 */
NTSTATUS
NTAPI
DxgkSqmGenericString(
    _In_ ULONG DataPointId,
    _In_ PVOID String)
{
    UNREFERENCED_PARAMETER(DataPointId);
    UNREFERENCED_PARAMETER(String);
    return STATUS_SUCCESS;
}

/*
 * DxgkSqmOptedIn — returns FALSE (no SQM telemetry in ReactOS).
 */
BOOLEAN
NTAPI
DxgkSqmOptedIn(VOID)
{
    return FALSE;
}

/*
 * DxgkSqmSetDword — no-op telemetry stub.
 */
NTSTATUS
NTAPI
DxgkSqmSetDword(
    _In_ ULONG DataPointId,
    _In_ ULONG Value)
{
    UNREFERENCED_PARAMETER(DataPointId);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * ETW tracing stubs
 *
 * Event Tracing for Windows instrumentation.  No-op stubs for ReactOS.
 * ====================================================================== */

/*
 * g_loggerInfo — global ETW logger state.
 * This is an opaque structure; we provide a zero-initialised placeholder.
 */
UCHAR g_loggerInfo[128] = { 0 };

VOID
NTAPI
TraceDxgkBlockThread(
    _In_ ULONG Reason)
{
    UNREFERENCED_PARAMETER(Reason);
}

VOID
NTAPI
TraceDxgkContext(
    _In_ ULONG EventId,
    _In_ PVOID ContextData)
{
    UNREFERENCED_PARAMETER(EventId);
    UNREFERENCED_PARAMETER(ContextData);
}

VOID
NTAPI
TraceDxgkDevice(
    _In_ ULONG EventId,
    _In_ PVOID DeviceData)
{
    UNREFERENCED_PARAMETER(EventId);
    UNREFERENCED_PARAMETER(DeviceData);
}

VOID
NTAPI
TraceDxgkFunctionProfiler(
    _In_ ULONG FunctionId)
{
    UNREFERENCED_PARAMETER(FunctionId);
}

VOID
NTAPI
TraceDxgkPerformanceWarning(
    _In_ ULONG WarningId,
    _In_ PVOID WarningData)
{
    UNREFERENCED_PARAMETER(WarningId);
    UNREFERENCED_PARAMETER(WarningData);
}

VOID
NTAPI
TraceDxgkPresentHistory(
    _In_ ULONG EventId,
    _In_ PVOID PresentData)
{
    UNREFERENCED_PARAMETER(EventId);
    UNREFERENCED_PARAMETER(PresentData);
}

/* ========================================================================
 * DriverEntry
 * ====================================================================== */

/*
 * DriverEntry
 *
 * Called once by the I/O manager when dxgkrnl.sys is first loaded.
 *
 * Parameters:
 *   DriverObject  — the DRIVER_OBJECT for dxgkrnl.sys itself.
 *   RegistryPath  — service registry key path (not currently used).
 *
 * Returns STATUS_SUCCESS or an NTSTATUS error code.
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS        Status;
    UNICODE_STRING  DeviceName;

    UNREFERENCED_PARAMETER(RegistryPath);

    DXGKRNL_TRACE("DriverEntry: dxgkrnl.sys loading\n");

    /* --- Initialise global adapter list --------------------------------- */

    KeInitializeSpinLock(&DxgkAdapterGlobalListLock);
    InitializeListHead(&DxgkAdapterGlobalListHead);

    /* --- Populate DxgCoreInterface with callback pointers --------------- */

    DxgkpInitializeCoreInterface();

    /* --- Initialise debug helpers --------------------------------------- */

    DxgkDebugInit();

    /* --- Seed D3DKMT handle cookie (context.c) ------------------------- */

    Status = DxgkContextInit();
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DriverEntry: DxgkContextInit failed 0x%08lX\n", Status);
        return Status;
    }

    /* --- Create the control device object ------------------------------- */

    RtlInitUnicodeString(&DeviceName, L"\\Device\\DxgKrnl");

    Status = IoCreateDevice(DriverObject,
                            0,              /* no device extension needed */
                            &DeviceName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &GDxgControlDeviceObject);
    if (Status == STATUS_OBJECT_NAME_COLLISION)
    {
        /* Already loaded as boot driver — device exists from first load.
         * This is normal when dxgkrnl is Start=0 and gets re-entered. */
        DXGKRNL_TRACE("DriverEntry: \\Device\\DxgKrnl already exists (boot driver re-entry)\n");
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DriverEntry: IoCreateDevice failed 0x%08lX\n", Status);
        DxgkContextUninit();
        return Status;
    }

    {
        UNICODE_STRING SymlinkName, TargetName;
        NTSTATUS SymStatus;

        RtlInitUnicodeString(&SymlinkName, L"\\DosDevices\\DxgKrnl");
        RtlInitUnicodeString(&TargetName, L"\\Device\\DxgKrnl");
        SymStatus = IoCreateSymbolicLink(&SymlinkName, &TargetName);
        if (!NT_SUCCESS(SymStatus) &&
            SymStatus != STATUS_OBJECT_NAME_COLLISION)
        {
            DXGKRNL_WARN("DriverEntry: IoCreateSymbolicLink(DxgKrnl) "
                         "failed 0x%08lX\n", SymStatus);
        }
    }

    /* --- Install dispatch routines on the control device ---------------- */

    DriverObject->MajorFunction[IRP_MJ_CREATE]                  = DxgkDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]                   = DxgkDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]          = DxgkDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = DxgkDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP]                     = DxgkDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER]                   = DxgkDispatchPower;

    GDxgControlDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    DXGKRNL_TRACE("DriverEntry: success — control device at %p\n",
                  GDxgControlDeviceObject);
    return STATUS_SUCCESS;
}
