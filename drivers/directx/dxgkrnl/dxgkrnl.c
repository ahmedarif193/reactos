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

#include <ndk/iofuncs.h>

#include "context.h"
#include "vidmm.h"
#include "vidsch.h"
#include "presenttrace.h"

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

BOOLEAN
DxgkBeginKmdTransaction(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVOID CurrentThread;
    NTSTATUS WaitStatus;

    PAGED_CODE();
    if (Adapter == NULL)
        return FALSE;
    CurrentThread = PsGetCurrentThread();
    if (Adapter->KmdTransactionOwnerThread == CurrentThread)
    {
        ASSERT(InterlockedCompareExchange(&Adapter->KmdTransactionDepth, 0, 0) > 0);
        InterlockedIncrement(&Adapter->KmdTransactionDepth);
        Adapter->KmdTraceBeginCaller = _ReturnAddress();
        return TRUE;
    }
    {
        DPT_SCOPE Trace = DptBegin(&g_DxgPresentTrace, DPT_DEVICE_LOCK);
        WaitStatus = KeWaitForSingleObject(&Adapter->KmdTransactionMutex, Executive, KernelMode, FALSE, NULL);
        DptEnd(&g_DxgPresentTrace, Trace, TRUE, 0);
    }
    if (!DxgkAcquireKmdCall(Adapter))
    {
        KeReleaseMutex(&Adapter->KmdTransactionMutex, FALSE);
        return FALSE;
    }
    if (Adapter->KmdTransactionOwnerThread != NULL || WaitStatus != STATUS_SUCCESS ||
        Adapter->KmdTransactionMutex.OwnerThread != CurrentThread)
        DbgPrint("KMD_TRANSACTION_STATE adapter=%p current=%p pid=%p owner=%p owner_pid=%p depth=%ld wait=%08lx mutex_owner=%p signal=%ld abandoned=%u caller=%p last_begin=%p last_end=%p irql=%u\n",
                 Adapter, CurrentThread, PsGetCurrentProcessId(),
                 Adapter->KmdTransactionOwnerThread, Adapter->KmdTraceOwnerPid,
                 Adapter->KmdTransactionDepth, WaitStatus,
                 Adapter->KmdTransactionMutex.OwnerThread,
                 Adapter->KmdTransactionMutex.Header.SignalState,
                 Adapter->KmdTransactionMutex.Abandoned, _ReturnAddress(),
                 Adapter->KmdTraceBeginCaller, Adapter->KmdTraceEndCaller, KeGetCurrentIrql());
    ASSERT(Adapter->KmdTransactionOwnerThread == NULL);
    ASSERT(InterlockedCompareExchange(&Adapter->KmdTransactionDepth, 0, 0) == 0);
    InterlockedExchange(&Adapter->KmdTransactionDepth, 1);
    Adapter->KmdTraceBeginCaller = _ReturnAddress();
    Adapter->KmdTraceOwnerPid = PsGetCurrentProcessId();
    KeMemoryBarrier();
    Adapter->KmdTransactionOwnerThread = CurrentThread;
    KeMemoryBarrier();
    return TRUE;
}

VOID
DxgkEndKmdTransaction(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG Depth;

    PAGED_CODE();
    ASSERT(Adapter != NULL);
    ASSERT(Adapter->KmdTransactionOwnerThread == PsGetCurrentThread());
    Adapter->KmdTraceEndCaller = _ReturnAddress();
    Depth = InterlockedDecrement(&Adapter->KmdTransactionDepth);
    ASSERT(Depth >= 0);
    if (Depth != 0)
        return;
    Adapter->KmdTransactionOwnerThread = NULL;
    KeMemoryBarrier();
    DxgkReleaseKmdCall(Adapter);
    KeReleaseMutex(&Adapter->KmdTransactionMutex, FALSE);
}

NTSTATUS
DxgkYieldKmdTransactionForContextRoom(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ PDXGKRNL_CONTEXT Context,
    _In_ ULONGLONG Deadline,
    _Inout_ PBOOLEAN TransactionHeld)
{
    NTSTATUS Status;
    DPT_SCOPE Trace;

    PAGED_CODE();
    if (Adapter == NULL || Context == NULL || TransactionHeld == NULL ||
        !*TransactionHeld ||
        Context->Device == NULL || Context->Device->Adapter != Adapter ||
        Adapter->KmdTransactionOwnerThread != PsGetCurrentThread() ||
        InterlockedCompareExchange(&Adapter->KmdTransactionDepth, 0, 0) != 1)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    DxgkEndKmdTransaction(Adapter);
    *TransactionHeld = FALSE;
    Status = DxgkContextOrderWaitForRoom(Context, Deadline);
    if (!NT_SUCCESS(Status))
        return Status;
    Trace = DxgPresentTraceProducerBegin(Context, DxgTraceTransactionReacquire);
    Status = DxgkBeginKmdTransaction(Adapter) ? STATUS_SUCCESS : STATUS_DELETE_PENDING;
    DxgPresentTraceProducerEnd(Trace, Status);
    if (!NT_SUCCESS(Status))
        return Status;
    *TransactionHeld = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkAcquireInterruptCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->InterruptCallbacksBlocked, 0, 0) != 0)
        return FALSE;
    InterlockedIncrement(&Adapter->InterruptActiveCalls);
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Adapter->InterruptCallbacksBlocked, 0, 0) != 0)
    {
        DxgkReleaseInterruptCallback(Adapter);
        return FALSE;
    }
    return TRUE;
}

VOID
DxgkReleaseInterruptCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG Remaining;

    ASSERT(Adapter != NULL);
    Remaining = InterlockedDecrement(&Adapter->InterruptActiveCalls);
    ASSERT(Remaining >= 0);
}

VOID
DxgkBlockInterruptCallbacks(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Delay;

    PAGED_CODE();
    ASSERT(Adapter != NULL);
    InterlockedExchange(&Adapter->InterruptCallbacksBlocked, 1);
    KeMemoryBarrier();
    Delay.QuadPart = -(LONGLONG)(10 * 1000);
    while (InterlockedCompareExchange(&Adapter->InterruptActiveCalls, 0, 0) != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

VOID
DxgkUnblockInterruptCallbacks(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    ASSERT(Adapter != NULL);
    InterlockedExchange(&Adapter->InterruptCallbacksBlocked, 0);
}

/*
 * GDxgControlDeviceObject
 *
 * The \\Device\\DxgKrnl device object used to service D3DKMT IOCTLs.
 */
PDEVICE_OBJECT GDxgControlDeviceObject = NULL;
volatile LONG GDxgControlDeviceState = 0;
static NTSTATUS GDxgControlDeviceStatus = STATUS_DEVICE_NOT_READY;

NTSTATUS
DxgkpEnsureControlDevice(VOID)
{
    UNICODE_STRING DriverName;
    LARGE_INTEGER Delay;
    NTSTATUS Status;
    LONG State;
    ULONG HandoffWaits = 0;

    State = InterlockedCompareExchange(&GDxgControlDeviceState, 0, 0);
    if (State == 2)
        return STATUS_SUCCESS;
    if (State == 3)
        return GDxgControlDeviceStatus;
    if (State == 0)
    {
        RtlInitUnicodeString(&DriverName, L"\\Driver\\DxgKrnl");
        Status = IoCreateDriver(&DriverName, DriverEntry);
        State = InterlockedCompareExchange(&GDxgControlDeviceState, 0, 0);
        if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_COLLISION && Status != STATUS_DEVICE_BUSY && State == 0)
            return Status;
    }
    Delay.QuadPart = -10 * 1000;
    while ((State = InterlockedCompareExchange(&GDxgControlDeviceState, 0, 0)) == 0 && HandoffWaits++ < 1000)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    while ((State = InterlockedCompareExchange(&GDxgControlDeviceState, 0, 0)) == 1)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    return State == 2 ? STATUS_SUCCESS : GDxgControlDeviceStatus;
}

/* ========================================================================
 * DxgCoreInterface — static callback table consumed by dxgmms2.sys
 *
 * The provider imports this data symbol to access adapter-independent
 * dxgkrnl callbacks without holding an adapter handle.
 *
 * The table is populated with stubs initially; the per-adapter version
 * in DxgkpFillInterface (adapter.c) sets adapter-aware callbacks at
 * StartDevice time.  dxgmms2 uses this global table for adapter-
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

VOID
DxgkpInitializeCoreInterface(VOID)
{
    /* Forward declarations from adapter.c (all non-paged) */
    extern VOID APIENTRY DxgkCbNotifyInterrupt(
        HANDLE,
        IN_CONST_PDXGKARGCB_NOTIFY_INTERRUPT_DATA);
    extern VOID APIENTRY DxgkCbNotifyDpc(HANDLE);
    extern NTSTATUS APIENTRY DxgkCbGetDeviceInformation(HANDLE, PDXGK_DEVICE_INFO);
    extern NTSTATUS APIENTRY DxgkCbIndicateChildStatus(HANDLE, PDXGK_CHILD_STATUS);
    extern NTSTATUS APIENTRY DxgkCbMapMemory(HANDLE, PHYSICAL_ADDRESS, ULONG, BOOLEAN, BOOLEAN, MEMORY_CACHING_TYPE, PVOID*);
    extern NTSTATUS APIENTRY DxgkCbUnmapMemory(HANDLE, PVOID);
    extern NTSTATUS APIENTRY DxgkCbReadDeviceSpace(HANDLE, ULONG, PVOID, ULONG, ULONG, PULONG);
    extern NTSTATUS APIENTRY DxgkCbWriteDeviceSpace(HANDLE, ULONG, PVOID, ULONG, ULONG, PULONG);
    extern NTSTATUS APIENTRY DxgkCbAcquirePostDisplayOwnership(
        HANDLE,
        PDXGK_DISPLAY_INFORMATION);

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

/* TDR registry policy used by the adapter-owned watchdog boundary. */

/*
 * g_TdrConfig — TDR configuration read from registry.
 * GraphicsDrivers\TdrDelay, TdrDdiDelay, TdrLevel, etc.
 */
TDR_CONFIG g_TdrConfig = {
    2,      /* TdrDelay */
    5,      /* TdrDdiDelay */
    3,      /* TdrLevel = recover */
    5,      /* TdrLimitCount */
    60,     /* TdrLimitTime */
    2,      /* TdrDebugMode = recover without debugger prompt */
    0       /* TdrTestMode = none */
};

static NTSTATUS
DxgkpReadTdrDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _Inout_ PULONG Value)
{
    struct
    {
        KEY_VALUE_PARTIAL_INFORMATION Header;
        ULONG ExtraData;
    } ValueBuffer;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = &ValueBuffer.Header;
    UNICODE_STRING ValueNameString;
    ULONG ResultLength = 0;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueNameString, ValueName);
    RtlZeroMemory(&ValueBuffer, sizeof(ValueBuffer));
    Status = ZwQueryValueKey(KeyHandle, &ValueNameString, KeyValuePartialInformation, ValueInfo, sizeof(ValueBuffer), &ResultLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ValueInfo->Type != REG_DWORD || ValueInfo->DataLength != sizeof(ULONG))
    {
        DXGKRNL_WARN("DxgkpLoadTdrConfig: ignoring non-DWORD %S\n", ValueName);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    RtlCopyMemory(Value, ValueInfo->Data, sizeof(*Value));
    return STATUS_SUCCESS;
}

static VOID
DxgkpLoadTdrConfig(VOID)
{
    static CONST WCHAR GraphicsDriversPath[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\GraphicsDrivers";
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle = NULL;
    NTSTATUS Status;

    RtlInitUnicodeString(&KeyName, GraphicsDriversPath);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_TRACE("DxgkpLoadTdrConfig: using defaults, GraphicsDrivers key unavailable 0x%08lX\n", Status);
        return;
    }

    (VOID)DxgkpReadTdrDword(KeyHandle, L"TdrDelay", &g_TdrConfig.TdrDelay);
    (VOID)DxgkpReadTdrDword(KeyHandle, L"TdrDdiDelay", &g_TdrConfig.TdrDdiDelay);
    (VOID)DxgkpReadTdrDword(KeyHandle, L"TdrLevel", &g_TdrConfig.TdrLevel);
    (VOID)DxgkpReadTdrDword(KeyHandle, L"TdrLimitCount", &g_TdrConfig.TdrLimitCount);
    (VOID)DxgkpReadTdrDword(KeyHandle, L"TdrLimitTime", &g_TdrConfig.TdrLimitTime);
    (VOID)DxgkpReadTdrDword(KeyHandle, L"TdrDebugMode", &g_TdrConfig.TdrDebugMode);
    (VOID)DxgkpReadTdrDword(KeyHandle, L"TdrTestMode", &g_TdrConfig.TdrTestMode);
    ZwClose(KeyHandle);

    if (g_TdrConfig.TdrLevel > DXGKP_TDR_LEVEL_RECOVER)
        g_TdrConfig.TdrLevel = DXGKP_TDR_LEVEL_RECOVER;
    if (g_TdrConfig.TdrDebugMode > DXGKP_TDR_DEBUG_RECOVER_UNCONDITIONAL)
        g_TdrConfig.TdrDebugMode = DXGKP_TDR_DEBUG_RECOVER_NO_PROMPT;
    DXGKRNL_TRACE("DxgkpLoadTdrConfig: delay=%lu ddi=%lu level=%lu limit=%lu/%lus debug=%lu\n", g_TdrConfig.TdrDelay, g_TdrConfig.TdrDdiDelay, g_TdrConfig.TdrLevel, g_TdrConfig.TdrLimitCount, g_TdrConfig.TdrLimitTime, g_TdrConfig.TdrDebugMode);
}

/*
 * Exported build and bugcheck state consumed as data by dxgmms2. The Windows
 * 11 public symbols and consumer accesses define the widths; retail ReactOS
 * is neither an internal nor a checked Microsoft build.
 */
ULONG g_DxgMmsBugcheckExportIndex = 2;
UCHAR g_IsInternalRelease = FALSE;
UCHAR g_IsInternalReleaseOrDbg = FALSE;

/*
 * g_TdrForceTimeout — controls forced timeout for testing/debugging.
 */
volatile LONG g_TdrForceTimeout = 0;

/*
 * g_TdrForceDodPresentTimeout — forced DOD present timeout override.
 * Read atomically via XCHG by the timeout check path.
 */
volatile LONG g_TdrForceDodPresentTimeout = 0;

/*
 * g_TdrForceDodVSyncTimeout — forced DOD VSync timeout override.
 * Read atomically via XCHG by the timeout check path.
 */
volatile LONG g_TdrForceDodVSyncTimeout = 0;

/*
 * g_bVSyncEnabledForLogging — VSync logging flag for ETW tracing.
 */
BOOLEAN g_bVSyncEnabledForLogging = FALSE;

/*
 * The native dxgmms2 consumer treats a false result as a request to skip its
 * optional telemetry event. ReactOS has no equivalent provider or rate-limit
 * policy, so keep the ABI available while declining every event.
 */
BOOLEAN
CDECL
DxgKrnlTelemetryGlobal_LogTelemetryEvent(VOID)
{
    return FALSE;
}

/*
 * Windows 11 build 26100 maps the two FSE-block system services and monitor
 * color-space transform service to one terminal entry point. It neither reads
 * the caller buffer nor changes it before returning STATUS_NOT_SUPPORTED.
 */
NTSTATUS
NTAPI
DxgkD3dkmtNotSupported(
    _In_opt_ PVOID Data)
{
    UNREFERENCED_PARAMETER(Data);
    return STATUS_NOT_SUPPORTED;
}

/*
 * Layout-free TDR policy query consumed by dxgmms2. The public symbol fixes a
 * no-argument BOOLEAN contract, and the native entry reads only the enabled
 * policy state. ReactOS derives that state from its documented TdrLevel value.
 */
BOOLEAN
NTAPI
TdrIsEnabled(VOID)
{
    return g_TdrConfig.TdrLevel != DXGKP_TDR_LEVEL_OFF;
}

/* Atomically consumes the one-shot forced-flip timeout control. */
BOOLEAN
NTAPI
TdrIsTimeoutForcedFlip(VOID)
{
    return InterlockedExchange(&g_TdrForceTimeout, 0) != 0;
}

/*
 * The private TDR export ABI is intentionally excluded. Its public PDB names
 * do not define recovery-context sizes, field offsets, ownership, or calling
 * contracts, and no paired black-box contract has cleared those details.
 */
#if REACTOS_WDDM_PRIVATE_TDR_ABI

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

    /* Use the architecture-neutral interrupt-time clock in 100ns units. */
    CurrentTimestamp = KeQueryInterruptTime();

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

    /* Preserve the caller's adapter/scheduler context for recovery. */
    *(PVOID *)(Ctx + TDR_CTX_SCHEDULER_PTR) = AdapterContext;

    /* Default timeout multiplier */
    *(PULONG)(Ctx + TDR_CTX_TIMEOUT_MULT) = 0x10;

    /* Copy TDR configuration */
    *(PULONG)(Ctx + TDR_CTX_TDR_DELAY)     = g_TdrConfig.TdrDelay;
    *(PULONG)(Ctx + TDR_CTX_TDR_DDI_DELAY) = g_TdrConfig.TdrDdiDelay;
    *(PULONG)(Ctx + TDR_CTX_TDR_LEVEL)     = g_TdrConfig.TdrLevel;

    /* Store creation timestamp from the architecture-neutral clock. */
    *(PULONGLONG)(Ctx + TDR_CTX_TIMESTAMP) = KeQueryInterruptTime();

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

static DECLSPEC_NORETURN VOID
DxgkpBugCheckExportedTdrFailure(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PVOID RecoveryContext,
    _In_ NTSTATUS FailureStatus)
{
    ULONG_PTR OwnerTag = 0;

    if (Adapter != NULL && Adapter->MiniportContext != NULL && DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout) != NULL)
        OwnerTag = (ULONG_PTR)DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout);
    KeBugCheckEx(0x116, (ULONG_PTR)RecoveryContext, OwnerTag, (ULONG_PTR)FailureStatus, 0);
}

/* Core TDR entry point consumed by the external scheduler contract. */
NTSTATUS
NTAPI
TdrResetFromTimeout(
    _In_ PVOID RecoveryContext)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS SchedulerStatus;
    NTSTATUS Status;
    BOOLEAN AdapterStartedAfterReset = FALSE;
    BOOLEAN ContextPublished = FALSE;
    BOOLEAN DdiDeadlineArmed = FALSE;
    BOOLEAN SchedulerPrepared = FALSE;
    BOOLEAN SchedulerCompleted = FALSE;
    BOOLEAN KmdExclusive = FALSE;
    BOOLEAN Level3Transition = FALSE;
    BOOLEAN PresentResetStarted = FALSE;

    PAGED_CODE();

    if (RecoveryContext == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (*(PULONG)((PUCHAR)RecoveryContext + TDR_CTX_SIGNATURE1) != TAG_TDR_CONTEXT || *(PULONG)((PUCHAR)RecoveryContext + TDR_CTX_SIGNATURE2) != TAG_TDR_CONTEXT || *(PULONG)((PUCHAR)RecoveryContext + TDR_CTX_SIGNATURE3) != TAG_TDR_CONTEXT)
            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
        Adapter = *(PDXGKRNL_ADAPTER *)((PUCHAR)RecoveryContext + TDR_CTX_SCHEDULER_PTR);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
    }
    _SEH2_END;

    if (!DxgkReferenceAdapterObject(Adapter))
        return STATUS_DEVICE_NOT_READY;
    if (Adapter == NULL || Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_OFF)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }
    if (DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout) == NULL || DXGK_CB_FULL(Adapter, DxgkDdiRestartFromTimeout) == NULL)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }
    if (InterlockedCompareExchangePointer((PVOID volatile *)&Adapter->TdrRecoveryContext, RecoveryContext, NULL) != NULL)
    {
        Status = STATUS_DEVICE_BUSY;
        goto Cleanup;
    }
    ContextPublished = TRUE;
    DxgkpArmTdrDdiDeadline(Adapter);
    DdiDeadlineArmed = TRUE;
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_BUGCHECK)
        DxgkpBugCheckExportedTdrFailure(Adapter, RecoveryContext, STATUS_IO_TIMEOUT);
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_RECOVER_VGA)
        DxgkpBugCheckExportedTdrFailure(Adapter, RecoveryContext, STATUS_NOT_SUPPORTED);
    DxgkAcquireLevel3Transition(Adapter);
    Level3Transition = TRUE;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }

    DxgkPresentBeginReset(Adapter);
    PresentResetStarted = TRUE;
    SchedulerStatus = VidSchPrepareAdapterReset(Adapter);
    if (NT_SUCCESS(SchedulerStatus))
        SchedulerPrepared = TRUE;
    else if (SchedulerStatus != STATUS_NOT_SUPPORTED)
        DxgkpBugCheckExportedTdrFailure(Adapter, RecoveryContext, SchedulerStatus);

    DxgkBeginKmdExclusive(Adapter);
    KmdExclusive = TRUE;
    DxgkVidMmQuiesceAdapter(Adapter);
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportDeviceContext == NULL)
    {
        DxgkReleaseMiniportCallback(Adapter);
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
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

    /*
     * A miniport that cannot reset itself from a timeout has left the engine in
     * an unknown state, and restarting into that is worse than not restarting
     * at all.  DxgkDdiResetDevice is the escalation the driver supplies for
     * exactly this: put the device back to a known state before we go on.  It
     * returns nothing, so the only report is that we tried.
     */
    if (!NT_SUCCESS(Status) && DXGK_CB_FULL(Adapter, DxgkDdiResetDevice) != NULL)
    {
        _SEH2_TRY
        {
            DXGK_CB_FULL(Adapter, DxgkDdiResetDevice)(Adapter->MiniportDeviceContext);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            DXGKRNL_WARN("TDR: DxgkDdiResetDevice raised 0x%08lX\n", _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    DxgkReleaseMiniportCallback(Adapter);

    if (!NT_SUCCESS(Status))
        DxgkpBugCheckExportedTdrFailure(Adapter, RecoveryContext, Status);
    InterlockedExchange(&Adapter->TdrCompletionNotificationsEnabled, 0);
    DxgkDrainVidSchCallbacks(Adapter);
    AdapterStartedAfterReset = Adapter->State == DxgkAdapterStateStarted;
    DxgkTdrResetAdapterSynchronizationObjects(Adapter);
    Status = DxgkVidMmRecoverFromTimeout(Adapter);
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckExportedTdrFailure(Adapter, RecoveryContext, Status);
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    if (SchedulerPrepared)
    {
        VidSchCompleteAdapterReset(Adapter, TRUE);
        SchedulerCompleted = TRUE;
    }
    DxgkReleaseTrackedDmaBuffers(Adapter, TRUE);
    DxgkResetSubmittedFenceIdentities(Adapter);
    if (!AdapterStartedAfterReset || Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    Status = STATUS_DELETE_PENDING;
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        if (Adapter->State == DxgkAdapterStateStarted && InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) == 0 && InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) == 0)
            DxgkpBugCheckExportedTdrFailure(Adapter, RecoveryContext, Status);
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportDeviceContext == NULL)
    {
        DxgkReleaseMiniportCallback(Adapter);
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiRestartFromTimeout)(Adapter->MiniportDeviceContext);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseMiniportCallback(Adapter);

    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckExportedTdrFailure(Adapter, RecoveryContext, Status);

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->State != DxgkAdapterStateStarted || Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0 || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    DxgkVidMmResumeAdapter(Adapter);
    if (SchedulerPrepared)
        Status = VidSchResumeScheduler(Adapter);
    else
        Status = STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        DxgkpBugCheckExportedTdrFailure(Adapter, RecoveryContext, Status);
    }
    InterlockedExchange(&Adapter->TdrCompletionNotificationsEnabled, 1);
    DxgkEndKmdExclusive(Adapter, TRUE);
    KmdExclusive = FALSE;
    DxgkPresentCompleteReset(Adapter);
    PresentResetStarted = FALSE;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

Cleanup:
    if (SchedulerPrepared && !SchedulerCompleted)
        VidSchCompleteAdapterReset(Adapter, FALSE);
    if (KmdExclusive)
        DxgkEndKmdExclusive(Adapter, FALSE);
    if (PresentResetStarted)
        DxgkPresentCompleteReset(Adapter);
    if (DdiDeadlineArmed)
        DxgkpDisarmTdrDdiDeadline(Adapter);
    if (ContextPublished)
        InterlockedCompareExchangePointer((PVOID volatile *)&Adapter->TdrRecoveryContext, NULL, RecoveryContext);
    if (Level3Transition)
        DxgkReleaseLevel3Transition(Adapter);
    DxgkDereferenceAdapter(Adapter);
    return Status;
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

    if (KD_DEBUGGER_NOT_PRESENT)
        return FALSE;
    return g_TdrConfig.TdrDebugMode == DXGKP_TDR_DEBUG_BREAK;
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

#endif /* REACTOS_WDDM_PRIVATE_TDR_ABI */

/*
 * DxgkVidMmAllowFailOnOfferReclaimErrors
 *
 * Queried by the Windows 11 dxgmms2 VidMm path to determine whether offer/reclaim
 * allocation failures should be treated as errors or silently ignored.
 * Returns TRUE (allow failures) when no adapter is present or when
 * the adapter policy permits it.
 */
BOOLEAN
NTAPI
DxgkVidMmAllowFailOnOfferReclaimErrors(VOID)
{
    /*
     * Windows 11 returns TRUE when no current adapter exists and otherwise
     * evaluates an adapter policy. ReactOS has no corresponding public policy
     * input yet, so keep the no-fail permissive result without importing a
     * private native layout.
     */
    return TRUE;
}

/* ========================================================================
 * Display Port / Scheduler bridge exports
 *
 * Provide the private bridge consumed by the Windows 11 dxgmms2 architecture.
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
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)AdapterContext;

    if (Adapter == NULL || Adapter->MiniportContext == NULL || Adapter->MiniportContext->UseDodLayout)
        return 0;

    return Adapter->MiniportContext->InitData.s.Version;
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
 * Returns the current scheduler callback-state bit mask.
 */
ULONG
NTAPI
DpiGetSchedulerCallbackState(
    _In_ PVOID AdapterContext)
{
    return VidSchGetSchedulerCallbackState((PDXGKRNL_ADAPTER)AdapterContext);
}

/*
 * DpiSetSchedulerCallbackState
 *
 * Exchanges the scheduler callback-state bit mask and returns its old value.
 */
ULONG
NTAPI
DpiSetSchedulerCallbackState(
    _In_ PVOID AdapterContext,
    _In_ ULONG State)
{
    return VidSchSetSchedulerCallbackState((PDXGKRNL_ADAPTER)AdapterContext, State);
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
    PDEVICE_OBJECT  ControlDeviceObject = NULL;

    UNREFERENCED_PARAMETER(RegistryPath);

    DXGKRNL_TRACE("DriverEntry: dxgkrnl.sys loading\n");

    if (InterlockedCompareExchange(&GDxgControlDeviceState, 1, 0) != 0)
    {
        if (InterlockedCompareExchange(&GDxgControlDeviceState, 0, 0) == 2 && GDxgControlDeviceObject != NULL && GDxgControlDeviceObject->DriverObject == DriverObject)
            return DxgkpEnsureGlobalInitialization();
        return STATUS_DEVICE_BUSY;
    }
    GDxgControlDeviceStatus = STATUS_PENDING;
    DxgkpLoadTdrConfig();
    DxgkRedirectionInitialize();

    /* --- Create the control device object ------------------------------- */

    RtlInitUnicodeString(&DeviceName, L"\\Device\\DxgKrnl");

    Status = IoCreateDevice(DriverObject,
                            0,              /* no device extension needed */
                            &DeviceName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &ControlDeviceObject);
    if (Status == STATUS_OBJECT_NAME_COLLISION)
    {
        if (GDxgControlDeviceObject != NULL && GDxgControlDeviceObject->DriverObject == DriverObject)
        {
            DXGKRNL_TRACE("DriverEntry: \\Device\\DxgKrnl already belongs to this driver\n");
            Status = DxgkpEnsureGlobalInitialization();
            GDxgControlDeviceStatus = Status;
            InterlockedExchange(&GDxgControlDeviceState, NT_SUCCESS(Status) ? 2 : 3);
            return Status;
        }
        DXGKRNL_ERR("DriverEntry: \\Device\\DxgKrnl is owned by another driver instance\n");
        GDxgControlDeviceStatus = Status;
        InterlockedExchange(&GDxgControlDeviceState, 3);
        return Status;
    }
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DriverEntry: IoCreateDevice failed 0x%08lX\n", Status);
        GDxgControlDeviceStatus = Status;
        InterlockedExchange(&GDxgControlDeviceState, 3);
        return Status;
    }

    Status = DxgkpEnsureGlobalInitialization();
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DriverEntry: global initialization failed 0x%08lX\n", Status);
        IoDeleteDevice(ControlDeviceObject);
        GDxgControlDeviceStatus = Status;
        InterlockedExchange(&GDxgControlDeviceState, 3);
        return Status;
    }

    Status = DxgkSharedObjectsInitialize();
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DriverEntry: shared-object initialization failed 0x%08lX\n",
                    Status);
        IoDeleteDevice(ControlDeviceObject);
        GDxgControlDeviceStatus = Status;
        InterlockedExchange(&GDxgControlDeviceState, 3);
        return Status;
    }

    /* Install every dispatch target before publishing or opening admission. */
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DxgkDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DxgkDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DxgkDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = DxgkDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = DxgkDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = DxgkDispatchPower;
    GDxgControlDeviceObject = ControlDeviceObject;

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

    ControlDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    GDxgControlDeviceStatus = STATUS_SUCCESS;
    InterlockedExchange(&GDxgControlDeviceState, 2);

    DXGKRNL_TRACE("DriverEntry: success — control device at %p\n",
                  GDxgControlDeviceObject);
    return STATUS_SUCCESS;
}
