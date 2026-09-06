/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM GPU device and context lifecycle management
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * Overview
 * --------
 * Implements the four D3DKMT entry points that manage logical GPU devices and
 * execution contexts on behalf of user-mode Direct3D applications:
 *
 *   DxgkCreateDevice    — D3DKMTCreateDevice
 *   DxgkDestroyDevice   — D3DKMTDestroyDevice
 *   DxgkCreateContext   — D3DKMTCreateContext
 *   DxgkDestroyContext  — D3DKMTDestroyContext
 *
 * WDDM object hierarchy (per dxgkrnl_private.h):
 *   ADAPTER  — one per physical GPU
 *     DEVICE — one per D3D application device  (DXGKRNL_DEVICE)
 *       CONTEXT — one per GPU command stream   (DXGKRNL_CONTEXT)
 *
 * Handle namespace
 * ----------------
 * D3DKMT_HANDLE values are owner-scoped typed generation identifiers.  No
 * public handle contains or reconstructs a kernel pointer.
 *
 * Adapter identification (D3DKMT_CREATEDEVICE)
 * --------------------------------------------
 * D3DKMT_CREATEDEVICE carries a union of hAdapter (D3DKMT_HANDLE, user mode)
 * and pAdapter (PVOID, kernel mode).  Kernel-mode callers set pAdapter to the
 * DXGKRNL_ADAPTER pointer directly.  We validate it against the global list.
 *
 * Locking discipline
 * ------------------
 *   DxgkAdapterGlobalListLock (KSPIN_LOCK, DISPATCH_LEVEL)
 *     — protects the global adapter list.  Never held while acquiring a
 *       waitable mutex (different IRQL domains).  Snapshot adapter pointers
 *       under this lock, then release before calling any PASSIVE-level operation.
 *
 *   Adapter->AdapterMutex (KMUTEX, PASSIVE_LEVEL)
 *     — protects Adapter->DeviceListHead and serializes passive adapter lifecycle work.
 *
 *   ProcessRecord->ProcessMutex (FAST_MUTEX, APC_LEVEL)
 *     — protects ProcessRecord->DeviceListHead and each ProcessDeviceLink.
 *       Publication may acquire it while AdapterMutex is held.  Code holding
 *       ProcessMutex must never acquire AdapterMutex.
 *
 *   Device->DeviceMutex (FAST_MUTEX, APC_LEVEL)
 *     — protects Device->ContextListHead.  Must not be held while calling
 *       miniport DDIs.
 */

/* INCLUDES ******************************************************************/

#include "dxgkrnl_private.h"
#include "context.h"
#include "handles.h"
#include "vidmm.h"
#include "vidpn.h"
#include "vidsch.h"
#include <ndk/psfuncs.h>

/* GLOBALS *******************************************************************/

/* DxgkProcessNotifyRegistered — TRUE if the process-exit callback is active. */
static BOOLEAN DxgkProcessNotifyRegistered = FALSE;

/* Shared WDDM 2.0 process records, keyed by (PEPROCESS, adapter). */
static FAST_MUTEX DxgkProcessListLock;
static LIST_ENTRY DxgkProcessListHead;

/* Forward declaration — defined later in this file. */
VOID
NTAPI
DxgkProcessCleanup(
    _Inout_     PEPROCESS              Process,
    _In_        HANDLE                 ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo);

/* Maximum adapters snapshotted in one operation (on-stack arrays). */
#define DXGK_MAX_ADAPTERS 16

static NTSTATUS DxgkpDestroyDetachedDevice(_In_ PDXGKRNL_DEVICE Device);

NTSTATUS
DxgkDeviceWorkCreate(
    _In_ PDXGKRNL_DEVICE Device,
    _Outptr_ PDXGKRNL_DEVICE_WORK *OutWork)
{
    PDXGKRNL_DEVICE_WORK Work;

    if (Device == NULL || OutWork == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutWork = NULL;
    Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), TAG_DXGK_DEVICE);
    if (Work == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Work, sizeof(*Work));
    DxgkDeviceWorkCoreInitializeItem(&Work->CoreItem, &Device->WorkLedger);
    Work->Device = Device;
    Work->ReferenceCount = 1;
    Work->CompletionStatus = STATUS_PENDING;
    *OutWork = Work;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkDeviceWorkActivate(
    _Inout_ PDXGKRNL_DEVICE_WORK Work)
{
    if (Work == NULL || Work->Device == NULL)
        return STATUS_INVALID_PARAMETER;
    return DxgkDeviceWorkCoreActivate(&Work->CoreItem);
}

VOID
DxgkDeviceWorkCompleteWithStatus(
    _Inout_opt_ PDXGKRNL_DEVICE_WORK Work,
    _In_ NTSTATUS Status)
{
    if (Work == NULL || Work->Device == NULL)
        return;
    ASSERT(Status != STATUS_PENDING);
    /* Publish the result before waking ledger waiters. Keep the first result
     * when the ownership cleanup subsequently completes this item again. */
    if (InterlockedCompareExchange(&Work->CompletionStatus, Status,
                                   STATUS_PENDING) == STATUS_PENDING)
        DxgkDeviceWorkCoreComplete(&Work->CoreItem);
}

VOID
DxgkDeviceWorkComplete(
    _Inout_opt_ PDXGKRNL_DEVICE_WORK Work)
{
    DxgkDeviceWorkCompleteWithStatus(Work, STATUS_SUCCESS);
}

VOID
DxgkDeviceWorkReference(_Inout_ PDXGKRNL_DEVICE_WORK Work)
{
    InterlockedIncrement(&Work->ReferenceCount);
}

VOID
DxgkDeviceWorkDereference(_Inout_opt_ PDXGKRNL_DEVICE_WORK Work)
{
    if (Work != NULL && InterlockedDecrement(&Work->ReferenceCount) == 0)
        ExFreePoolWithTag(Work, TAG_DXGK_DEVICE);
}

NTSTATUS
DxgkDeviceWorkGetStatus(_In_ PDXGKRNL_DEVICE_WORK Work)
{
    return InterlockedCompareExchange(&Work->CompletionStatus, 0, 0);
}

VOID
DxgkDeviceWorkDestroy(
    _Inout_opt_ PDXGKRNL_DEVICE_WORK Work)
{
    if (Work == NULL)
        return;
    DxgkDeviceWorkComplete(Work);
    DxgkDeviceWorkDereference(Work);
}

VOID
DxgkDeviceWorkNotifyStateChange(
    _In_ PDXGKRNL_DEVICE Device)
{
    if (Device == NULL)
        return;
    DxgkDeviceWorkCoreNotifyStateChange(&Device->WorkLedger);
}

NTSTATUS
DxgkDeviceWorkWaitForQueued(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ ULONG TimeoutMs)
{
    DXGK_DEVICE_WORK_SNAPSHOT Snapshot;
    LARGE_INTEGER Deadline;
    NTSTATUS Status;

    PAGED_CODE();
    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;
    if (DxgkDeviceWorkCoreIsEmpty(&Device->WorkLedger))
        return STATUS_SUCCESS;
    Status = DxgkDeviceWorkCoreCaptureSnapshot(&Device->WorkLedger, &Snapshot);
    if (!NT_SUCCESS(Status))
        return Status;
    KeQuerySystemTime(&Deadline);
    Deadline.QuadPart += (LONGLONG)TimeoutMs * 10000LL;
    return DxgkDeviceWorkCoreWaitForSnapshotUntil(&Device->WorkLedger, &Snapshot, &Deadline);
}

/*
 * Wait for the DMA work already queued on every device of a process.
 *
 * Used before a process-wide mapping is torn down (D3DKMTFreeGpuVirtualAddress
 * clears page-table entries with the CPU).  The video memory manager's
 * destruction rule (D3DDDICB_DESTROYALLOCATION2FLAGS.AssumeNotInUse == FALSE)
 * assumes that commands queued before the request may still access what is
 * being taken away; the same holds for a GPU virtual address range, and on
 * GpuMmu hardware a translation miss halts the engine rather than returning
 * zeros.  Only work accepted before the call counts: the ledgers are
 * snapshotted, and a device whose work never completes is torn down by TDR,
 * which makes its ledger terminal and ends the wait.  Devices are referenced
 * under ProcessMutex and waited on with the mutex released.
 */
#define DXGK_PROCESS_WAIT_STACK_DEVICES 16

NTSTATUS
DxgkProcessWaitForQueuedWork(
    _In_ PDXGKRNL_PROCESS ProcessRecord,
    _In_ ULONG TimeoutMs)
{
    PDXGKRNL_DEVICE StackDevices[DXGK_PROCESS_WAIT_STACK_DEVICES];
    PDXGKRNL_DEVICE *Devices = StackDevices;
    ULONG Capacity = DXGK_PROCESS_WAIT_STACK_DEVICES;
    ULONG Count = 0;
    ULONG Index;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (ProcessRecord == NULL)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&ProcessRecord->ProcessMutex);
    for (Entry = ProcessRecord->DeviceListHead.Flink;
         Entry != &ProcessRecord->DeviceListHead;
         Entry = Entry->Flink)
    {
        Count++;
    }
    if (Count > Capacity)
    {
        /* NonPagedPool: the fast mutex holds us at APC_LEVEL. */
        Devices = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(*Devices), TAG_DXGK_DEVICE);
        if (Devices == NULL)
        {
            ExReleaseFastMutex(&ProcessRecord->ProcessMutex);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Capacity = Count;
    }
    Count = 0;
    for (Entry = ProcessRecord->DeviceListHead.Flink;
         Entry != &ProcessRecord->DeviceListHead && Count < Capacity;
         Entry = Entry->Flink)
    {
        PDXGK_PROCESS_DEVICE_LINK Link = CONTAINING_RECORD(Entry, DXGK_PROCESS_DEVICE_LINK, Entry);
        PDXGKRNL_DEVICE Device = (PDXGKRNL_DEVICE)Link->Device;

        /* A device that refuses the reference is being destroyed: its ledger
         * is terminal and nothing queued on it can still run. */
        if (Device == NULL || !DxgkReferenceDevice(Device))
            continue;
        Devices[Count++] = Device;
    }
    ExReleaseFastMutex(&ProcessRecord->ProcessMutex);

    for (Index = 0; Index < Count; Index++)
    {
        NTSTATUS WaitStatus = DxgkDeviceWorkWaitForQueued(Devices[Index], TimeoutMs);

        if (WaitStatus == STATUS_TIMEOUT || (!NT_SUCCESS(WaitStatus) && NT_SUCCESS(Status)))
            Status = WaitStatus;
        DxgkDereferenceDevice(Devices[Index]);
    }
    if (Devices != StackDevices)
        ExFreePoolWithTag(Devices, TAG_DXGK_DEVICE);
    return Status;
}

VOID
DxgkDeviceBeginDestroy(
    _In_ PDXGKRNL_DEVICE Device)
{
    if (Device == NULL)
        return;
    DxgkDeviceWorkCoreTransitionTerminal(&Device->WorkLedger, &Device->Destroying, 1);
    DxgkSyncObjectCancelDeviceWaits(Device, STATUS_DEVICE_REMOVED, TRUE);
}

VOID
DxgkDeviceSetExecutionState(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_DEVICEEXECUTION_STATE ExecutionState)
{
    if (Device == NULL)
        return;
    if (ExecutionState != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        DPRINT1("DxgkDeviceSetExecutionState: device %p handle 0x%X state %d -> %d (caller %p)\n",
                Device, Device->Handle,
                InterlockedCompareExchange(&Device->ExecutionState, 0, 0),
                ExecutionState, _ReturnAddress());
    }
    DxgkDeviceWorkCoreTransitionTerminal(&Device->WorkLedger, &Device->ExecutionState, ExecutionState);
    if (ExecutionState == D3DKMT_DEVICEEXECUTION_STOPPED)
        DxgkSyncObjectCancelDeviceWaits(Device, STATUS_DEVICE_REMOVED, TRUE);
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
BOOLEAN
DxgkDeviceSetPageFaultExecutionState(
    _In_ PDXGKRNL_DEVICE Device)
{
    BOOLEAN Transitioned;

    if (Device == NULL)
        return FALSE;
    Transitioned = DxgkDeviceWorkCoreTryTransitionTerminal(
                       &Device->WorkLedger,
                       &Device->ExecutionState,
                       D3DKMT_DEVICEEXECUTION_ACTIVE,
                       D3DKMT_DEVICEEXECUTION_ERROR_DMAPAGEFAULT);
    if (Transitioned)
    {
        DPRINT1("DxgkDeviceSetPageFaultExecutionState: device %p handle 0x%X -> ERROR_DMAPAGEFAULT (caller %p)\n",
                Device, Device->Handle, _ReturnAddress());
        DxgkSyncObjectCancelDeviceWaits(
            Device,
            STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE,
            TRUE);
    }
    return Transitioned;
}

VOID
DxgkDeviceSetPageFaultState(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ CONST D3DKMT_DEVICEPAGEFAULT_STATE *PageFaultState)
{
    KIRQL OldIrql;

    if (Device == NULL || PageFaultState == NULL)
        return;

    KeAcquireSpinLock(&Device->PageFaultLock, &OldIrql);
    Device->PageFaultState = *PageFaultState;
    KeMemoryBarrier();
    InterlockedExchange(&Device->PageFaultValid, 1);
    KeReleaseSpinLock(&Device->PageFaultLock, OldIrql);
}

BOOLEAN
DxgkDeviceGetPageFaultState(
    _In_ PDXGKRNL_DEVICE Device,
    _Out_ D3DKMT_DEVICEPAGEFAULT_STATE *PageFaultState)
{
    KIRQL OldIrql;
    BOOLEAN Valid;

    if (Device == NULL || PageFaultState == NULL)
        return FALSE;

    KeAcquireSpinLock(&Device->PageFaultLock, &OldIrql);
    Valid = InterlockedCompareExchange(&Device->PageFaultValid, 0, 0) != 0;
    if (Valid)
        *PageFaultState = Device->PageFaultState;
    else
        RtlZeroMemory(PageFaultState, sizeof(*PageFaultState));
    KeReleaseSpinLock(&Device->PageFaultLock, OldIrql);
    return Valid;
}
#endif

VOID
DxgkMarkAdapterDevicesStoppedLocked(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PLIST_ENTRY Link;

    /* Caller owns AdapterMutex so the terminal transition and the complete
     * device-list snapshot are one admission boundary. */
    if (Adapter == NULL)
        return;
    for (Link = Adapter->DeviceListHead.Flink; Link != &Adapter->DeviceListHead; Link = Link->Flink)
    {
        PDXGKRNL_DEVICE Device = CONTAINING_RECORD(Link, DXGKRNL_DEVICE, DeviceListEntry);

        DxgkDeviceSetExecutionState(Device, D3DKMT_DEVICEEXECUTION_STOPPED);
    }
}

VOID
DxgkMarkAdapterDevicesStopped(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return;
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    DxgkMarkAdapterDevicesStoppedLocked(Adapter);
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
}

NTSTATUS
DxgkDeviceWaitForIdle(
    _In_ PDXGKRNL_DEVICE Device)
{
    PAGED_CODE();
    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;
    return DxgkDeviceWorkCoreWaitForIdle(&Device->WorkLedger, NULL);
}

static VOID
DxgkpRetainDetachedContext(
    _In_ PDXGKRNL_CONTEXT Context)
{
    PDXGKRNL_DEVICE Device = Context->Device;

    DxgkDeviceBeginDestroy(Device);
    ExAcquireFastMutex(&Device->DeviceMutex);
    if (IsListEmpty(&Context->ContextListEntry))
        InsertTailList(&Device->ContextListHead, &Context->ContextListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);
}

static VOID
DxgkpRetainDetachedDevice(
    _In_ PDXGKRNL_DEVICE Device)
{
    PDXGKRNL_ADAPTER Adapter = Device->Adapter;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (IsListEmpty(&Device->DeviceListEntry))
        InsertTailList(&Adapter->DeviceListHead, &Device->DeviceListEntry);
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
}

static VOID
DxgkpDereferenceDevice(
    _In_ PDXGKRNL_DEVICE Device)
{
    if (InterlockedDecrement(&Device->ReferenceCount) == 0)
        KeSetEvent(&Device->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
    DxgkDereferenceAdapter(Device->Adapter);
}

BOOLEAN
DxgkReferenceDevice(
    _In_ PDXGKRNL_DEVICE Device)
{
    if (Device == NULL || !DxgkReferenceAdapter(Device->Adapter))
        return FALSE;
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        DxgkDereferenceAdapter(Device->Adapter);
        return FALSE;
    }
    InterlockedIncrement(&Device->ReferenceCount);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        DxgkpDereferenceDevice(Device);
        return FALSE;
    }
    return TRUE;
}

static BOOLEAN
DxgkpTryReferenceProcessDevice(
    _In_ PVOID Object,
    _In_opt_ PVOID Context)
{
    PDXGKRNL_DEVICE Device = (PDXGKRNL_DEVICE)Object;
    PDXGKRNL_PROCESS ProcessRecord = (PDXGKRNL_PROCESS)Context;

    if (Device == NULL || Device->ProcessRecord != ProcessRecord)
        return FALSE;
    return DxgkReferenceDevice(Device);
}

/*
 * Acquire one live device owned by ProcessRecord for process-wide paging work.
 *
 * ProcessMutex keeps every list node valid while DxgkReferenceDevice performs
 * its before/after Destroying checks.  Once that reference succeeds,
 * DxgkpDestroyDetachedDevice cannot call DxgkDdiDestroyDevice (and therefore
 * cannot invalidate hMiniportDevice) until the caller releases the reference.
 */
NTSTATUS
DxgkReferenceProcessPagingDevice(
    _In_ PDXGKRNL_PROCESS ProcessRecord,
    _Out_ PDXGKRNL_DEVICE *OutDevice,
    _Out_ PHANDLE OutMiniportDevice)
{
    PVOID Object = NULL;
    BOOLEAN Referenced;

    if (ProcessRecord == NULL ||
        OutDevice == NULL ||
        OutMiniportDevice == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *OutDevice = NULL;
    *OutMiniportDevice = NULL;

    ExAcquireFastMutex(&ProcessRecord->ProcessMutex);
    Referenced = DxgkProcessDeviceTryReference(
                     &ProcessRecord->DeviceListHead,
                     DxgkpTryReferenceProcessDevice,
                     ProcessRecord,
                     &Object,
                     OutMiniportDevice);
    ExReleaseFastMutex(&ProcessRecord->ProcessMutex);

    if (!Referenced)
        return STATUS_DEVICE_NOT_READY;

    *OutDevice = (PDXGKRNL_DEVICE)Object;
    return STATUS_SUCCESS;
}

static VOID
DxgkpUnlinkProcessDevice(
    _Inout_ PDXGKRNL_DEVICE Device)
{
    PDXGKRNL_PROCESS ProcessRecord;

    if (Device == NULL)
        return;
    ProcessRecord = Device->ProcessRecord;
    if (ProcessRecord == NULL)
        return;

    ExAcquireFastMutex(&ProcessRecord->ProcessMutex);
    (VOID)DxgkProcessDeviceLinkDetach(&Device->ProcessDeviceLink);
    ExReleaseFastMutex(&ProcessRecord->ProcessMutex);
}

BOOLEAN
DxgkReferenceContext(
    _In_ PDXGKRNL_CONTEXT Context)
{
    if (Context == NULL || InterlockedCompareExchange(&Context->Destroying, 0, 0) != 0)
        return FALSE;
    InterlockedIncrement(&Context->ReferenceCount);
    if (InterlockedCompareExchange(&Context->Destroying, 0, 0) != 0)
    {
        DxgkDereferenceContext(Context);
        return FALSE;
    }
    return TRUE;
}

VOID
DxgkDereferenceDevice(
    _In_ PDXGKRNL_DEVICE Device)
{
    DxgkpDereferenceDevice(Device);
}

VOID
DxgkDereferenceContext(
    _In_ PDXGKRNL_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->ReferenceCount) == 0)
        KeSetEvent(&Context->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
}

static BOOLEAN
DxgkpWaitForDeviceReferences(
    _In_ PDXGKRNL_DEVICE Device)
{
    LARGE_INTEGER Timeout;

    if (InterlockedCompareExchange(&Device->TeardownReferencesDrained, 1, 0) == 0 && InterlockedDecrement(&Device->ReferenceCount) == 0)
        return TRUE;
    if (InterlockedCompareExchange(&Device->ReferenceCount, 0, 0) == 0)
        return TRUE;
    Timeout.QuadPart = -10 * 1000;
    while (InterlockedCompareExchange(&Device->ReferenceCount, 0, 0) != 0)
    {
        if (InterlockedCompareExchange(&Device->MiniportDestroyPending, 0, 0) != 0)
            return FALSE;
        KeWaitForSingleObject(&Device->ReferencesDrainedEvent, Executive, KernelMode, FALSE, &Timeout);
    }
    return TRUE;
}

static VOID
DxgkpWaitForContextReferences(
    _In_ PDXGKRNL_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->ReferenceCount) != 0)
        KeWaitForSingleObject(&Context->ReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
}

static PDXGKRNL_PROCESS
DxgkpFindProcessRecordLocked(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process)
{
    PLIST_ENTRY Entry;

    for (Entry = DxgkProcessListHead.Flink;
         Entry != &DxgkProcessListHead;
         Entry = Entry->Flink)
    {
        PDXGKRNL_PROCESS ProcessRecord =
            CONTAINING_RECORD(Entry,
                              DXGKRNL_PROCESS,
                              GlobalProcessListEntry);

        if (ProcessRecord->Process == Process &&
            ProcessRecord->Adapter == Adapter)
        {
            return ProcessRecord;
        }
    }

    return NULL;
}

/*
 * Validate the opaque hDxgkProcess token passed to the miniport without ever
 * dereferencing an untrusted pointer.  The CreateProcess owner retains the
 * record's original lifetime reference for the whole synchronous callback,
 * so a matching Creating record cannot disappear after the list lock drops.
 */
NTSTATUS
DxgkValidateCreatingProcessHandle(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE ProcessHandle,
    _Out_ PDXGKRNL_PROCESS *OutProcessRecord)
{
    PDXGKRNL_PROCESS Requested;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_INVALID_HANDLE;

    PAGED_CODE();

    if (Adapter == NULL || ProcessHandle == NULL || OutProcessRecord == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutProcessRecord = NULL;
    Requested = (PDXGKRNL_PROCESS)ProcessHandle;

    ExAcquireFastMutex(&DxgkProcessListLock);
    for (Entry = DxgkProcessListHead.Flink;
         Entry != &DxgkProcessListHead;
         Entry = Entry->Flink)
    {
        PDXGKRNL_PROCESS ProcessRecord =
            CONTAINING_RECORD(Entry,
                              DXGKRNL_PROCESS,
                              GlobalProcessListEntry);

        if (ProcessRecord != Requested)
            continue;

        if (ProcessRecord->Adapter == Adapter &&
            ProcessRecord->Lifetime.State == DxgkProcessLifetimeCreating &&
            ProcessRecord->Lifetime.CallbackOwner == PsGetCurrentThread())
        {
            *OutProcessRecord = ProcessRecord;
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_DEVICE_NOT_READY;
        }
        break;
    }
    ExReleaseFastMutex(&DxgkProcessListLock);

    return Status;
}

static VOID
DxgkpFreeProcessRecordStorage(
    _In_ PDXGKRNL_PROCESS ProcessRecord)
{
    ASSERT(ProcessRecord != NULL);
    ASSERT(ProcessRecord->Lifetime.ReferenceCount == 0);
    ASSERT(IsListEmpty(&ProcessRecord->GlobalProcessListEntry));
    ASSERT(IsListEmpty(&ProcessRecord->DeviceListHead));
    ASSERT(IsListEmpty(&ProcessRecord->AllocationListHead));
    ObDereferenceObject(ProcessRecord->Process);
    ExFreePoolWithTag(ProcessRecord, TAG_DXGK_PROCESS);
}

NTSTATUS
DxgkAcquireProcessRecord(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Out_ PDXGKRNL_PROCESS *OutProcessRecord)
{
    PDXGKRNL_PROCESS Candidate;
    PDXGKRNL_PROCESS Existing;
    DXGK_PROCESS_LIFETIME_ACQUIRE AcquireResult;
    DXGK_PROCESS_LIFETIME_RELEASE ReleaseResult;
    DXGK_PROCESS_LIFETIME_STATE State;
    PVOID CurrentThread;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Process == NULL || OutProcessRecord == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutProcessRecord = NULL;
    CurrentThread = PsGetCurrentThread();

    for (;;)
    {
        if (PsGetProcessExitStatus(Process) != STATUS_PENDING)
            return STATUS_PROCESS_IS_TERMINATING;

        ExAcquireFastMutex(&DxgkProcessListLock);
        Existing = DxgkpFindProcessRecordLocked(Adapter, Process);
        if (Existing != NULL)
        {
            AcquireResult =
                DxgkProcessLifetimeAcquire(&Existing->Lifetime,
                                           CurrentThread);
            ExReleaseFastMutex(&DxgkProcessListLock);

            if (AcquireResult == DxgkProcessLifetimeAcquireReady)
            {
                *OutProcessRecord = Existing;
                return STATUS_SUCCESS;
            }
            if (AcquireResult == DxgkProcessLifetimeRetry)
                continue;
            if (AcquireResult == DxgkProcessLifetimeRejectReentrant)
                return STATUS_DEVICE_BUSY;

            Status = KeWaitForSingleObject(&Existing->LifetimeEvent,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           NULL);

            ExAcquireFastMutex(&DxgkProcessListLock);
            State = Existing->Lifetime.State;
            if (NT_SUCCESS(Status) &&
                AcquireResult == DxgkProcessLifetimeWaitForCreate &&
                State == DxgkProcessLifetimeReady)
            {
                /*
                 * The wait pin acquired while Creating becomes this caller's
                 * ordinary adapter/device ownership reference.
                 */
                ExReleaseFastMutex(&DxgkProcessListLock);
                *OutProcessRecord = Existing;
                return STATUS_SUCCESS;
            }

            if (NT_SUCCESS(Status) &&
                AcquireResult == DxgkProcessLifetimeWaitForCreate &&
                State == DxgkProcessLifetimeFailed)
            {
                Status = Existing->Lifetime.FailureStatus;
            }
            else if (NT_SUCCESS(Status) &&
                     AcquireResult != DxgkProcessLifetimeWaitForDestroy)
            {
                Status = STATUS_INTERNAL_ERROR;
            }

            ReleaseResult =
                DxgkProcessLifetimeRelease(&Existing->Lifetime,
                                           CurrentThread);
            ExReleaseFastMutex(&DxgkProcessListLock);
            if (ReleaseResult == DxgkProcessLifetimeFree)
                DxgkpFreeProcessRecordStorage(Existing);

            if (AcquireResult == DxgkProcessLifetimeWaitForCreate)
                return Status;

            /* Destruction completed; retry against the now-empty key. */
            continue;
        }
        ExReleaseFastMutex(&DxgkProcessListLock);

        Candidate = ExAllocatePoolWithTag(NonPagedPool,
                                          sizeof(*Candidate),
                                          TAG_DXGK_PROCESS);
        if (Candidate == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory(Candidate, sizeof(*Candidate));
        Candidate->Process = Process;
        Candidate->Adapter = Adapter;
        DxgkProcessLifetimeInitialize(&Candidate->Lifetime,
                                      CurrentThread);
        KeInitializeEvent(&Candidate->LifetimeEvent,
                          NotificationEvent,
                          FALSE);
        InitializeListHead(&Candidate->DeviceListHead);
        InitializeListHead(&Candidate->AllocationListHead);
        InitializeListHead(&Candidate->GlobalProcessListEntry);
        ExInitializeFastMutex(&Candidate->ProcessMutex);
        ObReferenceObject(Process);

        /*
         * Publish the Creating placeholder before entering the miniport.
         * A competing opener pins and waits on this exact object instead of
         * issuing a duplicate CreateProcess callback.
         */
        ExAcquireFastMutex(&DxgkProcessListLock);
        if (PsGetProcessExitStatus(Process) != STATUS_PENDING)
        {
            ExReleaseFastMutex(&DxgkProcessListLock);
            ObDereferenceObject(Process);
            ExFreePoolWithTag(Candidate, TAG_DXGK_PROCESS);
            return STATUS_PROCESS_IS_TERMINATING;
        }
        Existing = DxgkpFindProcessRecordLocked(Adapter, Process);
        if (Existing != NULL)
        {
            ExReleaseFastMutex(&DxgkProcessListLock);
            ObDereferenceObject(Process);
            ExFreePoolWithTag(Candidate, TAG_DXGK_PROCESS);
            continue;
        }
        InsertTailList(&DxgkProcessListHead,
                       &Candidate->GlobalProcessListEntry);
        ExReleaseFastMutex(&DxgkProcessListLock);

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        Status = DxgkGpuVaCreateProcess(Adapter, Candidate);
        if (!NT_SUCCESS(Status))
        {
            /*
             * Generic GPUVA state is initialized before the optional WDDM2
             * callback. Finish that local teardown before allowing a retry.
             * hMiniportProcess is published only on callback success.
             */
            DxgkGpuVaDestroyProcess(Adapter, Candidate);
        }
#else
        Status = STATUS_SUCCESS;
#endif

        ExAcquireFastMutex(&DxgkProcessListLock);
        if (!NT_SUCCESS(Status))
        {
            RemoveEntryList(&Candidate->GlobalProcessListEntry);
            InitializeListHead(&Candidate->GlobalProcessListEntry);
        }
        DxgkProcessLifetimeCompleteCreate(&Candidate->Lifetime,
                                          CurrentThread,
                                          Status);
        KeSetEvent(&Candidate->LifetimeEvent, IO_NO_INCREMENT, FALSE);
        if (!NT_SUCCESS(Status))
            ReleaseResult =
                DxgkProcessLifetimeRelease(&Candidate->Lifetime,
                                           CurrentThread);
        else
            ReleaseResult = DxgkProcessLifetimeReleaseNone;
        ExReleaseFastMutex(&DxgkProcessListLock);

        if (!NT_SUCCESS(Status))
        {
            if (ReleaseResult == DxgkProcessLifetimeFree)
                DxgkpFreeProcessRecordStorage(Candidate);
            return Status;
        }

        *OutProcessRecord = Candidate;
        return STATUS_SUCCESS;
    }
}

NTSTATUS
DxgkReferenceProcessRecordByAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Out_ PDXGKRNL_PROCESS *OutProcessRecord)
{
    PDXGKRNL_PROCESS ProcessRecord;
    DXGK_PROCESS_LIFETIME_ACQUIRE AcquireResult;

    if (Adapter == NULL || Process == NULL || OutProcessRecord == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutProcessRecord = NULL;
    ExAcquireFastMutex(&DxgkProcessListLock);
    ProcessRecord = DxgkpFindProcessRecordLocked(Adapter, Process);
    if (ProcessRecord != NULL &&
        ProcessRecord->Lifetime.State == DxgkProcessLifetimeReady)
    {
        AcquireResult =
            DxgkProcessLifetimeAcquire(&ProcessRecord->Lifetime,
                                       PsGetCurrentThread());
        ASSERT(AcquireResult == DxgkProcessLifetimeAcquireReady);
        ExReleaseFastMutex(&DxgkProcessListLock);
        *OutProcessRecord = ProcessRecord;
        return STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkProcessListLock);

    if (ProcessRecord != NULL)
        return STATUS_DEVICE_NOT_READY;
    return STATUS_NOT_FOUND;
}

VOID
DxgkDereferenceProcessRecord(
    _In_opt_ PDXGKRNL_PROCESS ProcessRecord)
{
    DXGK_PROCESS_LIFETIME_RELEASE ReleaseResult;
    BOOLEAN FreeRecord;

    PAGED_CODE();

    if (ProcessRecord == NULL)
        return;

    ExAcquireFastMutex(&DxgkProcessListLock);
    ReleaseResult =
        DxgkProcessLifetimeRelease(&ProcessRecord->Lifetime,
                                   PsGetCurrentThread());
    if (ReleaseResult == DxgkProcessLifetimeBeginDestroy)
        KeResetEvent(&ProcessRecord->LifetimeEvent);
    ExReleaseFastMutex(&DxgkProcessListLock);

    if (ReleaseResult == DxgkProcessLifetimeBeginDestroy)
    {
        ASSERT(InterlockedCompareExchange(
                   &ProcessRecord->InFlightSubmissions, 0, 0) == 0);
        ASSERT(IsListEmpty(&ProcessRecord->DeviceListHead));

        /*
         * Keep the Destroying record discoverable while the callback runs.
         * New openers pin it and wait, so CreateProcess cannot overlap this
         * DestroyProcess for the same (process, adapter) identity.
         */
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        DxgkGpuVaDestroyProcess(ProcessRecord->Adapter, ProcessRecord);
#endif

        ExAcquireFastMutex(&DxgkProcessListLock);
        RemoveEntryList(&ProcessRecord->GlobalProcessListEntry);
        InitializeListHead(&ProcessRecord->GlobalProcessListEntry);
        FreeRecord =
            DxgkProcessLifetimeCompleteDestroy(
                &ProcessRecord->Lifetime,
                PsGetCurrentThread());
        KeSetEvent(&ProcessRecord->LifetimeEvent, IO_NO_INCREMENT, FALSE);
        ExReleaseFastMutex(&DxgkProcessListLock);

        if (FreeRecord)
            DxgkpFreeProcessRecordStorage(ProcessRecord);
    }
    else if (ReleaseResult == DxgkProcessLifetimeFree)
    {
        DxgkpFreeProcessRecordStorage(ProcessRecord);
    }
}

/* PRIVATE HELPERS ***********************************************************/

#define DXGKP_CONTEXT_STREAM_DRAIN_BATCH 16

static VOID
DxgkpInitializeContextStreamState(_Inout_ PDXGKRNL_CONTEXT Context)
{
    KeInitializeMutex(&Context->RenderLock, 0);
    KeInitializeMutex(&Context->StreamAdmissionMutex, 0);
    ExInitializeRundownProtection(&Context->StreamAdmissionRundown);
    KeInitializeSpinLock(&Context->StreamLock);
    InitializeListHead(&Context->StreamOperationList);
    InitializeListHead(&Context->StreamReadyEntry);
    Context->StreamWorkerQueued = 0;
    Context->StreamWaitOperationCount = 0;
    Context->StreamStopping = 0;
    KeInitializeEvent(&Context->StreamDrainedEvent, NotificationEvent, TRUE);
}

static NTSTATUS
DxgkpCreateContextStream(_Inout_ PDXGKRNL_CONTEXT Context)
{
    PDXGKRNL_ADAPTER Adapter;
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 Interface;
    DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1 Info;
    DXGMMS2_CONTEXT_STREAM_HANDLE Stream;
    NTSTATUS Status;

    PAGED_CODE();
    if (Context == NULL || Context->Device == NULL || Context->Mms2ContextStream != NULL)
        return STATUS_INVALID_PARAMETER;
    Adapter = Context->Device->Adapter;
    if (InterlockedCompareExchange(&Adapter->Mms2ContextStreamValid, 0, 0) == 0)
        return STATUS_DEVICE_NOT_READY;
    KeMemoryBarrier();
    Interface = Adapter->Mms2ContextStreamInterface;
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Adapter->Mms2ContextStreamValid, 0, 0) == 0 || Interface.Size != DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE || Interface.Version != DXGMMS2_CONTEXT_STREAM_VERSION_1 || Interface.Generation == 0 || Interface.AdapterHandle != Adapter->Mms2Adapter || Interface.CreateContextStream == NULL)
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1_SIZE;
    Info.Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Info.NodeOrdinal = Context->NodeOrdinal;
    Stream = NULL;
    Status = Interface.CreateContextStream(Interface.AdapterHandle, &Info, &Stream);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Stream == NULL)
        return STATUS_REVISION_MISMATCH;
    Context->Mms2ContextStream = Stream;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpCaptureContextStreamTeardownInterface(_In_ PDXGKRNL_CONTEXT Context, _Out_ DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *Interface)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (Context == NULL || Context->Device == NULL || Interface == NULL)
        return STATUS_INVALID_PARAMETER;
    Adapter = Context->Device->Adapter;
    KeMemoryBarrier();
    *Interface = Adapter->Mms2ContextStreamInterface;
    KeMemoryBarrier();
    if (Interface->Size != DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE || Interface->Version != DXGMMS2_CONTEXT_STREAM_VERSION_1 || Interface->Generation == 0 || Interface->AdapterHandle != Adapter->Mms2Adapter || Interface->CancelContextStream == NULL || Interface->DrainRetirements == NULL || Interface->DestroyContextStream == NULL)
        return STATUS_DEVICE_NOT_READY;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpDrainContextStreamRetirements(_In_ PDXGKRNL_CONTEXT Context, _In_ const DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *Interface)
{
    DXGMMS2_CONTEXT_RETIREMENT_V1 Records[DXGKP_CONTEXT_STREAM_DRAIN_BATCH];
    ULONG RecordIndex;
    ULONG RetiredCount;
    NTSTATUS Status;

    PAGED_CODE();
    do
    {
        RetiredCount = 0;
        RtlZeroMemory(Records, sizeof(Records));
        Status = Interface->DrainRetirements(Interface->AdapterHandle, Context->Mms2ContextStream, Records, RTL_NUMBER_OF(Records), &RetiredCount);
        if (!NT_SUCCESS(Status) && Status != STATUS_MORE_ENTRIES)
            return Status;
        if (RetiredCount > RTL_NUMBER_OF(Records))
            return STATUS_REVISION_MISMATCH;
        for (RecordIndex = 0; RecordIndex < RetiredCount; ++RecordIndex)
            DxgkContextOrderRetire(Context, &Records[RecordIndex]);
        if (RetiredCount == 0)
            break;
    } while (Status == STATUS_MORE_ENTRIES);
    return STATUS_SUCCESS;
}

/* Upper bound on the engine drain taken while a context is torn down. */
#define DXGKP_CONTEXT_TEARDOWN_DRAIN_MS 1000

static NTSTATUS
DxgkpBeginContextStreamTeardown(_Inout_ PDXGKRNL_CONTEXT Context)
{
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 Interface;
    DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1 CancelInfo;
    NTSTATUS CancelStatus;
    NTSTATUS Status;

    PAGED_CODE();
    InterlockedExchange(&Context->StreamStopping, 1);
    /*
     * StreamStopping alone does not close the admission window: a submission
     * that already passed the check is still building its operation.  Wait for
     * those to drain before cancelling, so no packet is admitted into a stream
     * that is being torn down.
     */
    ExWaitForRundownProtectionRelease(&Context->StreamAdmissionRundown);
    (VOID)KeWaitForSingleObject(&Context->StreamAdmissionMutex, Executive, KernelMode, FALSE, NULL);
    CancelStatus = InterlockedCompareExchange(&Context->Device->ExecutionState, 0, 0) == D3DKMT_DEVICEEXECUTION_ACTIVE ? STATUS_CANCELLED : STATUS_DEVICE_REMOVED;
    VidSchCancelContextPackets(Context->Device->Adapter, Context, CancelStatus);
    if (Context->Mms2ContextStream == NULL)
    {
        Status = STATUS_SUCCESS;
        goto Exit;
    }
    Status = DxgkpCaptureContextStreamTeardownInterface(Context, &Interface);
    if (!NT_SUCCESS(Status))
        goto Exit;
    RtlZeroMemory(&CancelInfo, sizeof(CancelInfo));
    CancelInfo.Size = DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1_SIZE;
    CancelInfo.Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    CancelInfo.Reason = CancelStatus;
    Status = Interface.CancelContextStream(Interface.AdapterHandle, Context->Mms2ContextStream, &CancelInfo);
    if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
        goto Exit;
    DxgkContextOrderWakeDevice(Context->Device);
    Status = DxgkpDrainContextStreamRetirements(Context, &Interface);

Exit:
    KeReleaseMutex(&Context->StreamAdmissionMutex, FALSE);
    /*
     * NOTE: cancellation only withdraws packets the provider never dispatched.
     * Work already handed to the engine keeps running out of this context's
     * page tables, and other contexts may still hold references to resources
     * torn down here -- that is the residual GPU page fault.  Draining this
     * context's own operations here does not close it (measured: the fault
     * returns), and idling every engine does (measured: no faults) but blocks
     * the destroying thread long enough to deadlock against work that needs it
     * to proceed, which TDRs.  The real fix is a per-allocation reference
     * fence so only the work that actually touches these resources is awaited.
     * TODO: implement that tracking; neither coarse drain belongs here.
     */
    return Status;
}

static NTSTATUS
DxgkpFinishContextStreamTeardown(_Inout_ PDXGKRNL_CONTEXT Context)
{
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 Interface;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();
    if (Context->Mms2ContextStream == NULL)
        return STATUS_SUCCESS;
    (VOID)KeWaitForSingleObject(&Context->StreamAdmissionMutex, Executive, KernelMode, FALSE, NULL);
    Status = DxgkpCaptureContextStreamTeardownInterface(Context, &Interface);
    if (!NT_SUCCESS(Status))
        goto Exit;
    Status = DxgkpDrainContextStreamRetirements(Context, &Interface);
    if (!NT_SUCCESS(Status))
        goto Exit;
    KeAcquireSpinLock(&Context->StreamLock, &OldIrql);
    if (!IsListEmpty(&Context->StreamOperationList) || InterlockedCompareExchange(&Context->StreamWorkerQueued, 0, 0) != 0)
        Status = STATUS_DEVICE_BUSY;
    KeReleaseSpinLock(&Context->StreamLock, OldIrql);
    if (!NT_SUCCESS(Status))
        goto Exit;
    Status = Interface.DestroyContextStream(Interface.AdapterHandle, Context->Mms2ContextStream);
    if (NT_SUCCESS(Status))
        Context->Mms2ContextStream = NULL;

Exit:
    KeReleaseMutex(&Context->StreamAdmissionMutex, FALSE);
    return Status;
}

PDXGKRNL_DEVICE
DxgkLookupDeviceByHandle(
    _In_ D3DKMT_HANDLE       Handle,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;

    if (!NT_SUCCESS(DxgkReferenceDeviceByHandle(Handle, PsGetCurrentProcess(), &Adapter, &Device)))
        return NULL;
    if (OutAdapter != NULL)
        *OutAdapter = Adapter;
    return Device;
}

PDXGKRNL_CONTEXT
DxgkLookupContextByHandle(
    _In_ D3DKMT_HANDLE       Handle,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_opt_ PDXGKRNL_DEVICE  *OutDevice)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_CONTEXT Context;

    if (!NT_SUCCESS(DxgkReferenceContextByHandle(Handle, PsGetCurrentProcess(), &Adapter, &Device, &Context)))
        return NULL;
    if (OutAdapter != NULL)
        *OutAdapter = Adapter;
    if (OutDevice != NULL)
        *OutDevice = Device;
    return Context;
}

NTSTATUS
DxgkReferenceOwnedDeviceByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice)
{
    return DxgkReferenceDeviceByHandle(Handle, OwnerProcess, OutAdapter, OutDevice);
}

NTSTATUS
DxgkReferenceVirtualContextByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice,
    _Out_ PDXGKRNL_CONTEXT *OutContext)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = DxgkReferenceContextByHandle(Handle, OwnerProcess, OutAdapter, OutDevice, OutContext);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!(*OutContext)->VirtualAddressing)
    {
        DxgkDereferenceContext(*OutContext);
        *OutAdapter = NULL;
        *OutDevice = NULL;
        *OutContext = NULL;
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpDetachOwnedContextByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_CONTEXT *OutContext)
{
    NTSTATUS Status;
    PDXGKRNL_CONTEXT Context;

    Status = DxgkDetachContextHandle(Handle, OwnerProcess, &Context);
    if (!NT_SUCCESS(Status))
        return Status;
    ExAcquireFastMutex(&Context->Device->DeviceMutex);
    if (!IsListEmpty(&Context->ContextListEntry))
    {
        RemoveEntryList(&Context->ContextListEntry);
        InitializeListHead(&Context->ContextListEntry);
    }
    ExReleaseFastMutex(&Context->Device->DeviceMutex);
    *OutContext = Context;
    return STATUS_SUCCESS;
}

/* Serialize teardown DDIs with the final DxgkDdiRemoveDevice boundary. */
static NTSTATUS
DxgkpDestroyMiniportContext(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ HANDLE MiniportContext)
{
    NTSTATUS Status;

    if (MiniportContext == NULL || DXGK_CB_FULL(Adapter, DxgkDdiDestroyContext) == NULL || Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
        return STATUS_SUCCESS;
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_DEVICE_NOT_READY;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiDestroyContext)(MiniportContext);
    DxgkReleaseMiniportCallback(Adapter);
    return Status;
}

static NTSTATUS
DxgkpDestroyMiniportDevice(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ HANDLE MiniportDevice)
{
    NTSTATUS Status;

    if (MiniportDevice == NULL || DXGK_CB_FULL(Adapter, DxgkDdiDestroyDevice) == NULL || Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
        return STATUS_SUCCESS;
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_DEVICE_NOT_READY;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiDestroyDevice)(MiniportDevice);
    DxgkReleaseMiniportCallback(Adapter);
    return Status;
}

static NTSTATUS
DxgkpEnsureBaseNamedObjectsDirectory(VOID)
{
    static const UNICODE_STRING DirectoryName =
        RTL_CONSTANT_STRING(L"\\BaseNamedObjects");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE DirectoryHandle;
    NTSTATUS Status;

    /* Native WDDM miniports may publish permanent synchronization objects
     * from their system-context CreateContext path.  That path runs before
     * basesrv creates the session-zero namespace during ReactOS boot. */
    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&DirectoryName,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENIF |
                                   OBJ_PERMANENT | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateDirectoryObject(&DirectoryHandle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (NT_SUCCESS(Status))
        ZwClose(DirectoryHandle);

    return Status;
}

/*
 * Create the native VidSch-style device/context pair used exclusively by
 * VidMm paging.  hSystemContext is a real miniport context handle, not an OS
 * context token and not one of the caller's render contexts.  Keeping this
 * pair adapter-owned also prevents a client-device close from invalidating
 * the context while another process is still updating page tables.
 */
NTSTATUS
DxgkCreatePagingSystemContext(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_CONTEXT Context;
    PDXGKRNL_PROCESS ProcessRecord;
    DXGK_DEVICE_WORK_TERMINAL_STATE WorkTerminal;
    DXGKARG_CREATEDEVICE CreateDeviceArg;
    DXGKARG_CREATECONTEXT CreateContextArg;
    DXGK_NODEMETADATA NodeMetadata;
    BOOLEAN ContextSchedulingSupported;
    HANDLE RuntimeContextToken;
    ULONG PagingNode;
    NTSTATUS Status;
    NTSTATUS CleanupStatus;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Adapter->SchedulingSystemDevice != NULL ||
        Adapter->PagingSystemDevice != NULL ||
        Adapter->PagingSystemContext != NULL)
    {
        return (Adapter->SchedulingSystemDevice != NULL &&
                Adapter->PagingSystemDevice != NULL &&
                Adapter->PagingSystemContext != NULL &&
                Adapter->PagingSystemDevice->hMiniportDevice != NULL &&
                Adapter->PagingSystemContext->hMiniportContext != NULL)
                   ? STATUS_SUCCESS
                   : STATUS_INVALID_DEVICE_STATE;
    }
    /* BasicDisplay is only the firmware-framebuffer owner while a hardware
     * adapter starts.  Native scheduler system devices belong to the
     * hardware scheduling adapter; retaining a fallback GPU process across
     * that handoff also changes miniport callback lifetime and pool reuse. */
    if (Adapter->MiniportContext->IsDisplayOnlyDriver ||
        Adapter->MiniportContext->IsBasicDisplayFallback)
        return STATUS_SUCCESS;
    if (DXGK_CB_FULL(Adapter, DxgkDdiCreateDevice) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiCreateContext) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    PagingNode = 0;
    if (Adapter->PhysicalAdapterCapsValid)
        PagingNode = Adapter->PhysicalAdapterCaps.PagingNodeIndex;
    if (PagingNode >= Adapter->NodeCount)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Status = DxgkpEnsureBaseNamedObjectsDirectory();
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkCreatePagingSystemContext: failed to prepare "
                    "\\BaseNamedObjects, status 0x%08lx\n",
                    Status);
        return Status;
    }

    Status = DxgkAcquireProcessRecord(Adapter,
                                      PsInitialSystemProcess,
                                      &ProcessRecord);
    if (!NT_SUCCESS(Status))
        return Status;

    Device = ExAllocatePoolWithTag(NonPagedPool,
                                   sizeof(*Device),
                                   TAG_DXGK_DEVICE);
    Context = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Context),
                                    TAG_DXGK_CONTEXT);
    if (Device == NULL || Context == NULL)
    {
        if (Context != NULL)
            ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        if (Device != NULL)
            ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
        DxgkDereferenceProcessRecord(ProcessRecord);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Device, sizeof(*Device));
    Device->Adapter = Adapter;
    Device->OwnerProcess = PsInitialSystemProcess;
    Device->ProcessRecord = ProcessRecord;
    Device->ReferenceCount = 1;
    Device->ExecutionState = D3DKMT_DEVICEEXECUTION_ACTIVE;
    InitializeListHead(&Device->ContextListHead);
    InitializeListHead(&Device->SyncObjListHead);
    InitializeListHead(&Device->OverlayListHead);
    InitializeListHead(&Device->DeviceListEntry);
    DxgkProcessDeviceLinkInitialize(&Device->ProcessDeviceLink, Device);
    RtlZeroMemory(&WorkTerminal, sizeof(WorkTerminal));
    WorkTerminal.Destroying = &Device->Destroying;
    WorkTerminal.ExecutionState = &Device->ExecutionState;
    WorkTerminal.ActiveExecutionState = D3DKMT_DEVICEEXECUTION_ACTIVE;
    WorkTerminal.TerminalStatus = STATUS_DEVICE_REMOVED;
    DxgkDeviceWorkCoreInitializeLedger(&Device->WorkLedger, &WorkTerminal);
    DxgkSyncWaitCoreInitializeRegistry(&Device->SyncWaitRegistry);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    KeInitializeSpinLock(&Device->PageFaultLock);
#endif
    DxgkPresentLimitCoreInitialize(&Device->PresentLimit,
                                   DXGKRNL_DEFAULT_QUEUED_PRESENT_LIMIT);
    ExInitializeFastMutex(&Device->DeviceMutex);
    KeInitializeEvent(&Device->ReferencesDrainedEvent,
                      NotificationEvent,
                      FALSE);

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Device = Device;
    Context->NodeOrdinal = PagingNode;
    Context->EngineAffinity = 1;
    Context->ReferenceCount = 1;
    InitializeListHead(&Context->ContextListEntry);
    KeInitializeEvent(&Context->ReferencesDrainedEvent,
                      NotificationEvent,
                      FALSE);
    DxgkpInitializeContextStreamState(Context);

    Adapter->PagingSystemDevice = Device;
    Adapter->PagingSystemContext = Context;

    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto CreationFailed;
    }

    /* VidSchiCreateNode caches ContextSchedulingSupported and
     * VidSchCreateSystemDevices uses that bit to choose its paging-context
     * creation contract.  The context-scheduling path supplies a runtime
     * context token and advertises hardware-queue support to CreateContext;
     * the legacy path supplies a NULL token. */
    ContextSchedulingSupported = FALSE;
    if (DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata) != NULL)
    {
        RtlZeroMemory(&NodeMetadata, sizeof(NodeMetadata));
        Status = DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata)(
                     Adapter->MiniportDeviceContext,
                     PagingNode,
                     &NodeMetadata);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkCreatePagingSystemContext: "
                        "DxgkDdiGetNodeMetadata failed 0x%08lx node=%lu\n",
                        Status,
                        PagingNode);
            DxgkEndKmdTransaction(Adapter);
            goto CreationFailed;
        }
        ContextSchedulingSupported =
            NodeMetadata.Flags.ContextSchedulingSupported != 0;
    }

    /* VidSchCreateSystemDevices creates its general system device before the
     * paging device.  Both calls carry the same public SystemDevice flag;
     * the native 0x1/0x11 distinction remains private to VidSch. */
    RtlZeroMemory(&CreateDeviceArg, sizeof(CreateDeviceArg));
    CreateDeviceArg.hDevice = NULL;
    CreateDeviceArg.Flags.SystemDevice = 1;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    CreateDeviceArg.hKmdProcess = ProcessRecord->hMiniportProcess;
#endif
    Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateDevice)(
                 Adapter->MiniportDeviceContext,
                 &CreateDeviceArg);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkCreatePagingSystemContext: general system "
                    "DxgkDdiCreateDevice failed 0x%08lx output=%p process=%p\n",
                    Status,
                    CreateDeviceArg.hDevice,
                    CreateDeviceArg.hKmdProcess);
        DxgkEndKmdTransaction(Adapter);
        goto CreationFailed;
    }
    if (CreateDeviceArg.hDevice == NULL)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        DXGKRNL_ERR("DxgkCreatePagingSystemContext: general system "
                    "DxgkDdiCreateDevice returned success without a handle\n");
        DxgkEndKmdTransaction(Adapter);
        goto CreationFailed;
    }
    Adapter->SchedulingSystemDevice = CreateDeviceArg.hDevice;

    RtlZeroMemory(&CreateDeviceArg, sizeof(CreateDeviceArg));
    CreateDeviceArg.hDevice = NULL;
    CreateDeviceArg.Flags.SystemDevice = 1;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    CreateDeviceArg.hKmdProcess = ProcessRecord->hMiniportProcess;
#endif
    Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateDevice)(
                 Adapter->MiniportDeviceContext,
                 &CreateDeviceArg);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkCreatePagingSystemContext: paging system "
                    "DxgkDdiCreateDevice failed 0x%08lx output=%p process=%p\n",
                    Status,
                    CreateDeviceArg.hDevice,
                    CreateDeviceArg.hKmdProcess);
        DxgkEndKmdTransaction(Adapter);
        goto CreationFailed;
    }
    if (CreateDeviceArg.hDevice == NULL)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        DXGKRNL_ERR("DxgkCreatePagingSystemContext: paging system "
                    "DxgkDdiCreateDevice returned success without a handle\n");
        DxgkEndKmdTransaction(Adapter);
        goto CreationFailed;
    }
    Device->hMiniportDevice = CreateDeviceArg.hDevice;

    RtlZeroMemory(&CreateContextArg, sizeof(CreateContextArg));
    RuntimeContextToken = ContextSchedulingSupported ? (HANDLE)Context : NULL;
    CreateContextArg.hContext = RuntimeContextToken;
    CreateContextArg.NodeOrdinal = PagingNode;
    CreateContextArg.EngineAffinity = 1;
    CreateContextArg.Flags.SystemContext = 1;
    if (ContextSchedulingSupported)
        CreateContextArg.Flags.HwQueueSupported = 1;
    DXGKRNL_INFO("DxgkCreatePagingSystemContext: creating paging context "
                 "node=%lu context-scheduling=%u runtime=%p flags=0x%08x\n",
                 PagingNode,
                 ContextSchedulingSupported,
                 RuntimeContextToken,
                 CreateContextArg.Flags.Value);
    Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateContext)(
                 Device->hMiniportDevice,
                 &CreateContextArg);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkCreatePagingSystemContext: DxgkDdiCreateContext failed "
                    "0x%08lx output=%p runtime=%p node=%lu engine=0x%x "
                    "flags=0x%08x context-scheduling=%u\n",
                    Status,
                    CreateContextArg.hContext,
                    RuntimeContextToken,
                    CreateContextArg.NodeOrdinal,
                    CreateContextArg.EngineAffinity,
                    CreateContextArg.Flags.Value,
                    ContextSchedulingSupported);
        DxgkEndKmdTransaction(Adapter);
        goto CreationFailed;
    }
    if (CreateContextArg.hContext == NULL)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        DXGKRNL_ERR("DxgkCreatePagingSystemContext: DxgkDdiCreateContext "
                    "returned success without a context handle\n");
        DxgkEndKmdTransaction(Adapter);
        goto CreationFailed;
    }
    Context->hMiniportContext = CreateContextArg.hContext;
    Context->ContextInfo = CreateContextArg.ContextInfo;
    DxgkEndKmdTransaction(Adapter);

    DXGKRNL_INFO("DxgkCreatePagingSystemContext: device=%p context=%p node=%lu engine=0x%x dma-size=0x%x segment-set=0x%x private=%u\n",
                 Device->hMiniportDevice,
                 Context->hMiniportContext,
                 PagingNode,
                 Context->EngineAffinity,
                 Context->ContextInfo.DmaBufferSize,
                 Context->ContextInfo.DmaBufferSegmentSet,
                 Context->ContextInfo.DmaBufferPrivateDataSize);
    return STATUS_SUCCESS;

CreationFailed:
    CleanupStatus = DxgkDestroyPagingSystemContext(Adapter);
    if (!NT_SUCCESS(CleanupStatus))
    {
        DXGKRNL_ERR("DxgkCreatePagingSystemContext: creation failed 0x%08lx and cleanup failed 0x%08lx\n",
                    Status,
                    CleanupStatus);
        return CleanupStatus;
    }
    return Status;
}

NTSTATUS
DxgkDestroyPagingSystemContext(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_CONTEXT Context;
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = Adapter->PagingSystemContext;
    Device = Adapter->PagingSystemDevice;
    if (Context != NULL)
    {
        InterlockedExchange(&Context->Destroying, 1);
        if (InterlockedCompareExchange(&Context->TeardownReferencesDrained,
                                       1,
                                       0) == 0)
        {
            DxgkpWaitForContextReferences(Context);
        }
        Status = STATUS_SUCCESS;
        if (Context->hMiniportContext != NULL)
            Status = DxgkpDestroyMiniportContext(Adapter,
                                                 Context->hMiniportContext);
        if (!NT_SUCCESS(Status))
        {
            InterlockedExchange(&Context->MiniportDestroyPending, 1);
            return Status;
        }
        Context->hMiniportContext = NULL;
        Adapter->PagingSystemContext = NULL;
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
    }

    if (Device != NULL)
    {
        PDXGKRNL_PROCESS ProcessRecord = Device->ProcessRecord;

        InterlockedExchange(&Device->Destroying, 1);
        if (!DxgkpWaitForDeviceReferences(Device))
            return STATUS_DEVICE_BUSY;
        Status = STATUS_SUCCESS;
        if (Device->hMiniportDevice != NULL)
            Status = DxgkpDestroyMiniportDevice(Adapter,
                                                Device->hMiniportDevice);
        if (!NT_SUCCESS(Status))
        {
            InterlockedExchange(&Device->MiniportDestroyPending, 1);
            return Status;
        }
        Device->hMiniportDevice = NULL;
        if (Adapter->SchedulingSystemDevice != NULL)
        {
            Status = DxgkpDestroyMiniportDevice(
                         Adapter,
                         Adapter->SchedulingSystemDevice);
            if (!NT_SUCCESS(Status))
            {
                InterlockedExchange(&Device->MiniportDestroyPending, 1);
                return Status;
            }
            Adapter->SchedulingSystemDevice = NULL;
        }
        Adapter->PagingSystemDevice = NULL;
        ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
        DxgkDereferenceProcessRecord(ProcessRecord);
    }

    return STATUS_SUCCESS;
}

/* Context must be detached from Device->ContextListHead and DeviceMutex must
 * not be held. The helper waits transient references before final teardown. */
static NTSTATUS
DxgkpDestroyContextNoLock(
    _In_ PDXGKRNL_CONTEXT Context)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    NTSTATUS         Status;

    PAGED_CODE();

    Device = Context->Device;
    Adapter = Device->Adapter;

    ASSERT(InterlockedCompareExchange(&Context->TeardownClaimed, 1, 1) == 1);
    InterlockedExchange(&Context->Destroying, 1);
    DxgkRemoveContextHandleObject(Context);
    DxgkPresentCancelContext(Adapter, Context);
    Status = DxgkpBeginContextStreamTeardown(Context);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Context->MiniportDestroyPending, 1);
        return Status;
    }
    if (InterlockedCompareExchange(&Context->TeardownReferencesDrained, 1, 0) == 0)
        DxgkpWaitForContextReferences(Context);
    if (InterlockedCompareExchange(&Context->StreamWorkerQueued, 0, 0) != 0)
        (VOID)KeWaitForSingleObject(&Context->StreamDrainedEvent, Executive, KernelMode, FALSE, NULL);

    /*
     * NOTE: an idle worker does not prove the GPU is finished -- operations
     * leave StreamOperationList only when the engine retires them, and the
     * emptiness check lives in DxgkpFinishContextStreamTeardown, which runs
     * after the miniport context is destroyed.  Draining here instead was
     * measured to hang the engine into an unrecoverable TDR, so the ordering
     * is left as-is until per-allocation reference fences make a precise wait
     * possible.  TODO: fix the ordering together with that tracking.
     */
    DXGKRNL_TRACE("DxgkpDestroyContextNoLock: Context %p hMiniport %p\n", Context, Context->hMiniportContext);

    Status = DxgkpDestroyMiniportContext(Adapter, Context->hMiniportContext);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpDestroyContextNoLock: DxgkDdiDestroyContext failed 0x%08lX\n", Status);
        InterlockedExchange(&Context->MiniportDestroyPending, 1);
        return Status;
    }
    Context->hMiniportContext = NULL;
    Status = DxgkpFinishContextStreamTeardown(Context);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpDestroyContextNoLock: dxgmms2 stream teardown failed 0x%08lX\n", Status);
        InterlockedExchange(&Context->MiniportDestroyPending, 1);
        return Status;
    }
    InterlockedExchange(&Context->MiniportDestroyPending, 0);
    ASSERT(Context->Mms2ContextStream == NULL);
    ASSERT(IsListEmpty(&Context->StreamOperationList));
    ASSERT(InterlockedCompareExchange(&Context->StreamWorkerQueued, 0, 0) == 0);
    ASSERT(InterlockedCompareExchange(&Context->StreamWaitOperationCount, 0, 0) == 0);

    DxgkContextRenderTeardown(Context);

#if DBG
    Context->Handle           = 0xDEADCCCC;
    Context->hMiniportContext = (HANDLE)(ULONG_PTR)0xDEADCCCCDEADCCCCULL;
#endif

    ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
    DxgkDereferenceDevice(Device);
    return STATUS_SUCCESS;
}

static VOID
DxgkpRollbackCreatedContext(
    _In_ PDXGKRNL_CONTEXT Context)
{
    PDXGKRNL_DEVICE Device = Context->Device;
    NTSTATUS Status;

    InterlockedExchange(&Context->TeardownClaimed, 1);
    InterlockedExchange(&Context->Destroying, 1);
    DxgkRemoveContextHandleObject(Context);
    Status = DxgkpDestroyContextNoLock(Context);
    if (!NT_SUCCESS(Status))
    {
        DxgkpRetainDetachedContext(Context);
        InterlockedExchange(&Device->MiniportDestroyPending, 1);
        DxgkpRetainDetachedDevice(Device);
    }
}

/*
 * DxgkpQueryFence
 *
 * Query the most recently completed GPU fence from the miniport.
 * Writes to *OutFence on success.
 *
 * IRQL: PASSIVE_LEVEL (miniport DDI contract).
 */
static NTSTATUS __attribute__((unused))
DxgkpQueryFence(
    _In_  PDXGKRNL_ADAPTER Adapter,
    _Out_ PULONG           OutFence)
{
    DXGKARG_QUERYCURRENTFENCE FenceArg;
    NTSTATUS                  Status;

    PAGED_CODE();

    *OutFence = 0;

    if (DXGK_CB_FULL(Adapter, DxgkDdiQueryCurrentFence) == NULL)
    {
        DXGKRNL_WARN("DxgkpQueryFence: no DxgkDdiQueryCurrentFence DDI\n");
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&FenceArg, sizeof(FenceArg));
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DEVICE_NOT_READY;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiQueryCurrentFence)(Adapter->MiniportDeviceContext, &FenceArg);
    DxgkReleaseKmdCall(Adapter);

    if (NT_SUCCESS(Status))
    {
        *OutFence = FenceArg.CurrentFence;
        DXGKRNL_TRACE("DxgkpQueryFence: fence = %lu\n", FenceArg.CurrentFence);
    }
    else
    {
        DXGKRNL_ERR("DxgkpQueryFence: DDI returned 0x%08lX\n", Status);
    }

    return Status;
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * DxgkContextInit
 *
 * Initializes the typed handle namespace and registers DxgkProcessCleanup via
 * PsSetCreateProcessNotifyRoutineEx.
 */
NTSTATUS
DxgkContextInit(VOID)
{
    NTSTATUS      Status;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkContextInit: enter\n");

    ExInitializeFastMutex(&DxgkProcessListLock);
    InitializeListHead(&DxgkProcessListHead);

    Status = DxgkHandleManagerInitialize();
    if (NT_SUCCESS(Status))
        Status = DxgkKeyedMutexInitialize();
    if (NT_SUCCESS(Status))
        Status = DxgkTrimNotificationInitialize();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PsSetCreateProcessNotifyRoutineEx(DxgkProcessCleanup, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkContextInit: PsSetCreateProcessNotifyRoutineEx "
                    "failed 0x%08lX\n", Status);
        DxgkTrimNotificationUninitialize();
        DxgkKeyedMutexUninitialize();
        DxgkHandleManagerUninitialize();
        return Status;
    }

    DxgkProcessNotifyRegistered = TRUE;
    DXGKRNL_TRACE("DxgkContextInit: done\n");
    return STATUS_SUCCESS;
}

/*
 * DxgkContextUninit
 *
 * Called from DriverUnload.  Deregisters the process-exit notification.
 */
VOID
DxgkContextUninit(VOID)
{
    PAGED_CODE();

    DXGKRNL_TRACE("DxgkContextUninit: enter\n");

    if (DxgkProcessNotifyRegistered)
    {
        PsSetCreateProcessNotifyRoutineEx(DxgkProcessCleanup, TRUE);
        DxgkProcessNotifyRegistered = FALSE;
        DXGKRNL_TRACE("DxgkContextUninit: process notify deregistered\n");
    }
    DxgkTrimNotificationUninitialize();
    DxgkKeyedMutexUninitialize();
    DxgkHandleManagerUninitialize();
}

/*
 * DxgkCreateDevice
 *
 * D3DKMTCreateDevice kernel entry point.
 *
 * For kernel-mode callers pCreateDevice->pAdapter is the DXGKRNL_ADAPTER
 * pointer (the PVOID union member of D3DKMT_CREATEDEVICE).  We validate it
 * against the global adapter list before using it.
 *
 * On success pCreateDevice->hDevice receives the new device handle.
 */
static NTSTATUS
DxgkpCreateDevice(
    _Inout_ D3DKMT_CREATEDEVICE *pCreateDevice,
    _In_ BOOLEAN GdiDevice)
{
    PDXGKRNL_ADAPTER     Adapter;
    PDXGKRNL_DEVICE      Device;
    DXGKARG_CREATEDEVICE CreateDeviceArg;
    DXGK_DEVICEINFO      LegacyDeviceInfo;
    DXGK_DEVICE_WORK_TERMINAL_STATE WorkTerminal;
    NTSTATUS             Status;
    BOOLEAN              KmdTransactionStarted = FALSE;
    BOOLEAN              LegacyDeviceInfoValid = FALSE;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkCreateDevice: pAdapter=%p Flags=0x%08X\n", pCreateDevice->pAdapter, (UINT)(pCreateDevice->Flags.LegacyMode | (pCreateDevice->Flags.RequestVSync << 1)));

    /*
     * Kernel-mode callers set pAdapter to the raw DXGKRNL_ADAPTER pointer.
     * Validate it against the global list before trusting it.
     */
    Adapter = (PDXGKRNL_ADAPTER)pCreateDevice->pAdapter;

    if (Adapter == NULL || !DxgkReferenceAdapterObject(Adapter))
    {
        DXGKRNL_ERR("DxgkCreateDevice: invalid adapter %p\n", Adapter);
        return STATUS_INVALID_PARAMETER;
    }
    if (Adapter->MiniportContext == NULL || Adapter->MiniportContext->UseDodLayout || Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        DxgkDereferenceAdapter(Adapter);
        return STATUS_NOT_SUPPORTED;
    }
    if (!DxgkBeginDeviceLifecycleOperation(Adapter))
    {
        DxgkDereferenceAdapter(Adapter);
        return STATUS_DELETE_PENDING;
    }

    /* --- Allocate the device -------------------------------------------- */

    Device = (PDXGKRNL_DEVICE)ExAllocatePoolWithTag(NonPagedPool, sizeof(DXGKRNL_DEVICE), TAG_DXGK_DEVICE);
    if (Device == NULL)
    {
        DXGKRNL_ERR("DxgkCreateDevice: pool alloc failed (%Iu bytes)\n", sizeof(DXGKRNL_DEVICE));
        DxgkEndDeviceLifecycleOperation(Adapter);
        DxgkDereferenceAdapter(Adapter);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Device, sizeof(DXGKRNL_DEVICE));

    /* --- Initialise fields ----------------------------------------------- */

    Device->Adapter = Adapter;
    Device->OwnerProcess = PsGetCurrentProcess();
    Device->Flags   = pCreateDevice->Flags;
    Device->GdiDevice = GdiDevice;
    Device->ReferenceCount = 1;
    Device->ExecutionState = D3DKMT_DEVICEEXECUTION_ACTIVE;

    InitializeListHead(&Device->ContextListHead);
    InitializeListHead(&Device->SyncObjListHead);
    InitializeListHead(&Device->OverlayListHead);
    InitializeListHead(&Device->DeviceListEntry);
    DxgkProcessDeviceLinkInitialize(&Device->ProcessDeviceLink, Device);
    RtlZeroMemory(&WorkTerminal, sizeof(WorkTerminal));
    WorkTerminal.Destroying = &Device->Destroying;
    WorkTerminal.ExecutionState = &Device->ExecutionState;
    WorkTerminal.ActiveExecutionState = D3DKMT_DEVICEEXECUTION_ACTIVE;
    WorkTerminal.TerminalStatus = STATUS_DEVICE_REMOVED;
    DxgkDeviceWorkCoreInitializeLedger(&Device->WorkLedger, &WorkTerminal);
    DxgkSyncWaitCoreInitializeRegistry(&Device->SyncWaitRegistry);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    KeInitializeSpinLock(&Device->PageFaultLock);
#endif
    DxgkPresentLimitCoreInitialize(&Device->PresentLimit, DXGKRNL_DEFAULT_QUEUED_PRESENT_LIMIT);
    ExInitializeFastMutex(&Device->DeviceMutex);
    KeInitializeEvent(&Device->ReferencesDrainedEvent, NotificationEvent, FALSE);

    /*
     * Opening the adapter establishes the per-process/per-adapter KMD
     * process. A device takes another reference to that already-published
     * record; it must not create or recreate process state on its own.
     */
    Status = DxgkReferenceProcessRecordByAdapter(Adapter,
                                                 Device->OwnerProcess,
                                                 &Device->ProcessRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
        DxgkEndDeviceLifecycleOperation(Adapter);
        DxgkDereferenceAdapter(Adapter);
        return Status;
    }

    /* --- Call DxgkDdiCreateDevice ---------------------------------------- */

    RtlZeroMemory(&CreateDeviceArg, sizeof(CreateDeviceArg));
    RtlZeroMemory(&LegacyDeviceInfo, sizeof(LegacyDeviceInfo));
    CreateDeviceArg.hDevice             = (HANDLE)Device; /* raw pointer as token */
    /* UMD D3DKMT flags are not bit-compatible with the KMD device flags. */
    CreateDeviceArg.Flags.Value         = 0;
    CreateDeviceArg.Flags.GdiDevice     = GdiDevice;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    CreateDeviceArg.hKmdProcess         = Device->ProcessRecord->hMiniportProcess;
#endif

    if (DXGK_CB_FULL(Adapter, DxgkDdiCreateDevice) != NULL)
    {
        if (!DxgkReferenceAdapter(Adapter))
        {
            DxgkDereferenceProcessRecord(Device->ProcessRecord);
            ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
            DxgkEndDeviceLifecycleOperation(Adapter);
            DxgkDereferenceAdapter(Adapter);
            return STATUS_DELETE_PENDING;
        }
        if (!DxgkBeginKmdTransaction(Adapter))
        {
            DxgkDereferenceAdapter(Adapter);
            DxgkDereferenceProcessRecord(Device->ProcessRecord);
            ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
            DxgkEndDeviceLifecycleOperation(Adapter);
            DxgkDereferenceAdapter(Adapter);
            return STATUS_DEVICE_NOT_READY;
        }
        KmdTransactionStarted = TRUE;
        Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateDevice)(Adapter->MiniportDeviceContext, &CreateDeviceArg);
        if (NT_SUCCESS(Status) && !Adapter->SchedulingCaps.MultiEngineAware && CreateDeviceArg.pInfo != NULL)
        {
            /*
             * pInfo belongs to the miniport and is only an output pointer for
             * backward-compatible contextless DMA geometry.  Snapshot it
             * before any other callback can invalidate driver-owned storage.
             * Multi-engine miniports use ContextInfo instead and may leave
             * this union holding the input Flags rather than a pointer.
             */
            _SEH2_TRY
            {
                LegacyDeviceInfo = *CreateDeviceArg.pInfo;
                LegacyDeviceInfoValid = TRUE;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkCreateDevice: DxgkDdiCreateDevice failed 0x%08lX\n", Status);
            if (CreateDeviceArg.hDevice != NULL && CreateDeviceArg.hDevice != (HANDLE)Device)
            {
                Device->hMiniportDevice = CreateDeviceArg.hDevice;
                InterlockedExchange(&Device->TeardownClaimed, 1);
                (VOID)DxgkpDestroyDetachedDevice(Device);
                DxgkEndDeviceLifecycleOperation(Adapter);
                DxgkEndKmdTransaction(Adapter);
                DxgkDereferenceAdapter(Adapter);
                return Status;
            }
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
            DxgkDereferenceProcessRecord(Device->ProcessRecord);
            ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
            DxgkEndDeviceLifecycleOperation(Adapter);
            DxgkDereferenceAdapter(Adapter);
            return Status;
        }

        /*
         * The miniport wrote its opaque per-device context back into hDevice.
         * Store it in hMiniportDevice; it will be passed to DxgkDdiDestroyDevice
         * and to DxgkDdiCreateContext (as MiniportDeviceContext per-device).
         */
        Device->hMiniportDevice = CreateDeviceArg.hDevice;
        if (LegacyDeviceInfoValid)
        {
            Device->LegacyDeviceInfo = LegacyDeviceInfo;
            Device->LegacyDeviceInfoValid = TRUE;
        }
        DXGKRNL_TRACE("DxgkCreateDevice: miniport device handle %p\n", Device->hMiniportDevice);
    }
    else
    {
        DXGKRNL_WARN("DxgkCreateDevice: miniport has no DxgkDdiCreateDevice\n");
        DxgkDereferenceProcessRecord(Device->ProcessRecord);
        ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
        DxgkEndDeviceLifecycleOperation(Adapter);
        DxgkDereferenceAdapter(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    Status = DxgkCreateDeviceHandle(Device, Device->OwnerProcess, &Device->Handle);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Device->TeardownClaimed, 1);
        (VOID)DxgkpDestroyDetachedDevice(Device);
        DxgkEndDeviceLifecycleOperation(Adapter);
        if (KmdTransactionStarted)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
        }
        return Status;
    }

    DXGKRNL_TRACE("DxgkCreateDevice: Device %p handle 0x%08X\n", Device, Device->Handle);

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        DxgkRemoveDeviceHandleObject(Device);
        InterlockedExchange(&Device->TeardownClaimed, 1);
        (VOID)DxgkpDestroyDetachedDevice(Device);
        DxgkEndDeviceLifecycleOperation(Adapter);
        if (KmdTransactionStarted)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
        }
        return STATUS_DELETE_PENDING;
    }
    /*
     * Publish both memberships while AdapterMutex still excludes every
     * teardown path.  ProcessMutex makes the process-owned list and its
     * miniport handle one atomic selection boundary for paging flushes.
     */
    ExAcquireFastMutex(&Device->ProcessRecord->ProcessMutex);
    if (!DxgkProcessDeviceLinkAttach(&Device->ProcessRecord->DeviceListHead,
                                     &Device->ProcessDeviceLink,
                                     Device->hMiniportDevice))
    {
        ExReleaseFastMutex(&Device->ProcessRecord->ProcessMutex);
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        DxgkRemoveDeviceHandleObject(Device);
        InterlockedExchange(&Device->TeardownClaimed, 1);
        (VOID)DxgkpDestroyDetachedDevice(Device);
        DxgkEndDeviceLifecycleOperation(Adapter);
        if (KmdTransactionStarted)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
        }
        return STATUS_INVALID_DEVICE_STATE;
    }
    InsertTailList(&Adapter->DeviceListHead, &Device->DeviceListEntry);
    ExReleaseFastMutex(&Device->ProcessRecord->ProcessMutex);
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    pCreateDevice->hDevice = Device->Handle;

    DXGKRNL_TRACE("DxgkCreateDevice: success hDevice=0x%08X\n", pCreateDevice->hDevice);
    DxgkEndDeviceLifecycleOperation(Adapter);
    if (KmdTransactionStarted)
    {
        DxgkEndKmdTransaction(Adapter);
        DxgkDereferenceAdapter(Adapter);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkCreateDevice(
    _Inout_ D3DKMT_CREATEDEVICE *pCreateDevice)
{
    return DxgkpCreateDevice(pCreateDevice, FALSE);
}

NTSTATUS
DxgkCreateCddDevice(
    _Inout_ D3DKMT_CREATEDEVICE *CreateDevice)
{
    PDXGKRNL_PROCESS Process;
    NTSTATUS Status;

    /* CDD is owned by the system, not by whichever application dirtied GDI. */
    ASSERT(PsGetCurrentProcess() == PsInitialSystemProcess);
    Status = DxgkAcquireProcessRecord(CreateDevice->pAdapter, PsInitialSystemProcess, &Process);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkpCreateDevice(CreateDevice, TRUE);
    DxgkDereferenceProcessRecord(Process);
    return Status;
}

static NTSTATUS
DxgkpDetachOwnedDeviceByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice)
{
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    *OutAdapter = NULL;
    *OutDevice = NULL;
    Status = DxgkDetachDeviceHandle(Handle, OwnerProcess, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    (VOID)KeWaitForSingleObject(&Device->Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (!IsListEmpty(&Device->DeviceListEntry))
    {
        RemoveEntryList(&Device->DeviceListEntry);
        InitializeListHead(&Device->DeviceListEntry);
    }
    KeReleaseMutex(&Device->Adapter->AdapterMutex, FALSE);
    *OutAdapter = Device->Adapter;
    *OutDevice = Device;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpDestroyDetachedDevice(
    _In_ PDXGKRNL_DEVICE Device)
{
    PDXGKRNL_ADAPTER Adapter = Device->Adapter;
    NTSTATUS Status;

    ASSERT(InterlockedCompareExchange(&Device->TeardownClaimed, 1, 1) == 1);
    DxgkDeviceBeginDestroy(Device);
    DxgkpUnlinkProcessDevice(Device);
    DxgkDeviceSetExecutionState(Device, D3DKMT_DEVICEEXECUTION_STOPPED);
    DxgkPresentNotifyDeviceRemoved(Adapter);
    DxgkPresentCancelDevice(Adapter, Device);
    InterlockedExchange(&Device->MiniportDestroyPending, 0);
    if (InterlockedCompareExchange(&Device->TeardownOsCleanupComplete, 1, 0) == 0)
    {
        DxgkRemoveDeviceHandleObject(Device);
        DxgkVidPnCleanupDeviceOwners(Device);
        DxgkD3dkmtDeviceCleanup(Device);
        DxgkCleanupDeviceSynchronizationObjects(Device);
    }

    for (;;)
    {
        PDXGKRNL_CONTEXT Context;
        PLIST_ENTRY Entry;
        BOOLEAN OwnsTeardown;

        ExAcquireFastMutex(&Device->DeviceMutex);
        if (IsListEmpty(&Device->ContextListHead))
        {
            ExReleaseFastMutex(&Device->DeviceMutex);
            break;
        }
        Entry = Device->ContextListHead.Flink;
        Context = CONTAINING_RECORD(Entry, DXGKRNL_CONTEXT, ContextListEntry);
        OwnsTeardown = DxgkTryClaimTeardown(&Context->TeardownClaimed);
        InterlockedExchange(&Context->Destroying, 1);
        RemoveEntryList(Entry);
        InitializeListHead(Entry);
        ExReleaseFastMutex(&Device->DeviceMutex);
        if (!OwnsTeardown && InterlockedCompareExchange(&Context->MiniportDestroyPending, 0, 0) == 0)
        {
            InterlockedExchange(&Device->MiniportDestroyPending, 1);
            DxgkpRetainDetachedDevice(Device);
            return STATUS_DEVICE_BUSY;
        }
        DxgkRemoveContextHandleObject(Context);
        Status = DxgkpDestroyContextNoLock(Context);
        if (!NT_SUCCESS(Status))
        {
            DxgkpRetainDetachedContext(Context);
            InterlockedExchange(&Device->MiniportDestroyPending, 1);
            DxgkpRetainDetachedDevice(Device);
            return Status;
        }
    }

    Status = DxgkOverlayCleanupDevice(Device);
    /* Virtual DMA pool entries retain this device so their GPUVA address
     * space remains valid. Destroy those entries before waiting for the last
     * device reference. */
    DxgkPurgeDmaBufferCacheForDevice(Device);
    if (NT_SUCCESS(Status) && !DxgkpWaitForDeviceReferences(Device))
        Status = STATUS_DEVICE_BUSY;
    if (NT_SUCCESS(Status))
        Status = DxgkVidMmCleanupDeviceAllocations(Device);
    if (NT_SUCCESS(Status))
        Status = DxgkpDestroyMiniportDevice(Adapter, Device->hMiniportDevice);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Device->MiniportDestroyPending, 1);
        DxgkpRetainDetachedDevice(Device);
        return Status;
    }
    InterlockedExchange(&Device->MiniportDestroyPending, 0);

#if DBG
    Device->Handle = 0xDEADDEAD;
    Device->hMiniportDevice = (HANDLE)(ULONG_PTR)0xDEADDEADDEADDEADULL;
#endif

    ASSERT(DxgkDeviceWorkCoreIsEmpty(&Device->WorkLedger));
    ASSERT(DxgkSyncWaitCoreIsEmpty(&Device->SyncWaitRegistry));
    ASSERT(IsListEmpty(&Device->OverlayListHead));
    ASSERT(InterlockedCompareExchange(&Device->InFlightSubmissions, 0, 0) == 0);
    ASSERT(IsListEmpty(&Device->ProcessDeviceLink.Entry));
    DxgkDereferenceProcessRecord(Device->ProcessRecord);
    ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
    DxgkDereferenceAdapter(Adapter);
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkCleanupAdapterDevices(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_DEVICE DetachedHead = NULL;
    NTSTATUS CleanupStatus = STATUS_SUCCESS;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    DxgkD3dkmtAdapterCleanup(Adapter);
    DxgkPurgeAdapterHandles(Adapter);
    Adapter->CddDeviceHandle = 0;
    Adapter->CddContextHandle = 0;
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    while (!IsListEmpty(&Adapter->DeviceListHead))
    {
        PLIST_ENTRY Entry = Adapter->DeviceListHead.Flink;
        PDXGKRNL_DEVICE Device = CONTAINING_RECORD(Entry, DXGKRNL_DEVICE, DeviceListEntry);
        BOOLEAN OwnsTeardown = DxgkTryClaimTeardown(&Device->TeardownClaimed);

        DxgkDeviceBeginDestroy(Device);
        RemoveEntryList(Entry);
        if (OwnsTeardown || InterlockedCompareExchange(&Device->MiniportDestroyPending, 0, 0) != 0)
        {
            Device->DeviceListEntry.Flink = (PLIST_ENTRY)DetachedHead;
            DetachedHead = Device;
        }
        else
        {
            /* The direct owner retains Adapter rundown through this Device. */
            InitializeListHead(&Device->DeviceListEntry);
        }
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    while (DetachedHead != NULL)
    {
        PDXGKRNL_DEVICE Device = DetachedHead;
        NTSTATUS Status;

        DetachedHead = (PDXGKRNL_DEVICE)Device->DeviceListEntry.Flink;
        InitializeListHead(&Device->DeviceListEntry);
        Status = DxgkpDestroyDetachedDevice(Device);
        if (!NT_SUCCESS(Status) && NT_SUCCESS(CleanupStatus))
            CleanupStatus = Status;
    }
    return CleanupStatus;
}

/*
 * DxgkDestroyDevice
 *
 * D3DKMTDestroyDevice kernel entry point.
 *
 * Destroys all contexts in the device, calls DxgkDdiDestroyDevice, unlinks
 * the device from its adapter's device list, and frees the pool allocation.
 */
NTSTATUS
NTAPI
DxgkDestroyDevice(
    _In_ D3DKMT_DESTROYDEVICE *pDestroyDevice)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE  Device;
    NTSTATUS         Status;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkDestroyDevice: hDevice=0x%08X\n", pDestroyDevice->hDevice);

    /* --- Validate handle ------------------------------------------------- */

    Status = DxgkpDetachOwnedDeviceByHandle(pDestroyDevice->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkDestroyDevice: invalid handle 0x%08X\n", pDestroyDevice->hDevice);
        return STATUS_INVALID_PARAMETER;
    }

    DXGKRNL_TRACE("DxgkDestroyDevice: Device %p on Adapter %p\n", Device, Adapter);

    Status = DxgkpDestroyDetachedDevice(Device);
    DxgkEndDeviceLifecycleOperation(Adapter);
    DXGKRNL_TRACE("DxgkDestroyDevice: done (0x%08lX)\n", Status);
    return Status;
}

/*
 * DxgkCreateContext
 *
 * D3DKMTCreateContext kernel entry point.
 *
 * Allocates a DXGKRNL_CONTEXT, calls DxgkDdiCreateContext, and links the
 * new context into Device->ContextListHead.
 *
 * On success pCreateContext->hContext receives the new context handle.
 */
static NTSTATUS
DxgkpCreateContextCaptured(
    _Inout_ D3DKMT_CREATECONTEXT *pCreateContext)
{
    PDXGKRNL_ADAPTER      Adapter;
    PDXGKRNL_DEVICE       Device;
    PDXGKRNL_CONTEXT      Context;
    DXGKARG_CREATECONTEXT CreateContextArg;
    NTSTATUS              Status;
    BOOLEAN               KmdTransactionStarted = FALSE;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkCreateContext: hDevice=0x%08X NodeOrdinal=%u EngineAffinity=0x%08X\n", pCreateContext->hDevice, pCreateContext->NodeOrdinal, pCreateContext->EngineAffinity);

    /* --- Validate device handle ----------------------------------------- */

    Status = DxgkReferenceOwnedDeviceByHandle(pCreateContext->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkCreateContext: invalid device handle 0x%08X\n", pCreateContext->hDevice);
        return STATUS_INVALID_PARAMETER;
    }
    if (pCreateContext->NodeOrdinal >= Adapter->NodeCount)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_INVALID_PARAMETER;
    }

    /* --- Allocate context ------------------------------------------------ */

    Context = (PDXGKRNL_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(DXGKRNL_CONTEXT), TAG_DXGK_CONTEXT);
    if (Context == NULL)
    {
        DXGKRNL_ERR("DxgkCreateContext: pool alloc failed (%Iu bytes)\n", sizeof(DXGKRNL_CONTEXT));
        DxgkpDereferenceDevice(Device);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context, sizeof(DXGKRNL_CONTEXT));

    /* --- Initialise context fields --------------------------------------- */

    Context->Device         = Device;
    Context->NodeOrdinal    = pCreateContext->NodeOrdinal;
    Context->EngineAffinity = pCreateContext->EngineAffinity;
    Context->SchedulingPriority = 0;
#if (REACTOS_WDDM_TARGET_LEVEL >= 1300)
    Context->InProcessSchedulingPriority = 0;
#endif
    Context->ReferenceCount = 1;

    InitializeListHead(&Context->ContextListEntry);
    KeInitializeEvent(&Context->ReferencesDrainedEvent, NotificationEvent, FALSE);
    DxgkpInitializeContextStreamState(Context);

    /* --- Call DxgkDdiCreateContext --------------------------------------- */

    RtlZeroMemory(&CreateContextArg, sizeof(CreateContextArg));
    CreateContextArg.hContext              = (HANDLE)Context; /* raw ptr */
    CreateContextArg.NodeOrdinal           = pCreateContext->NodeOrdinal;
    CreateContextArg.EngineAffinity        = pCreateContext->EngineAffinity;
    /*
     * D3DDDI_CREATECONTEXTFLAGS (UMD flags: NullRendering, InitialData) maps
     * to DXGK_CREATECONTEXTFLAGS (KMD flags: SystemContext, GdiContext, etc.).
     * The two flag sets have different semantics; zero the KMD flags and let
     * the miniport use its defaults.  The UMD flags are advisory only at this
     * level of the stack for WDDM 1.0.
     */
    CreateContextArg.Flags.Value           = 0;
    CreateContextArg.Flags.GdiContext      = Device->GdiDevice;
    CreateContextArg.pPrivateDriverData    = pCreateContext->pPrivateDriverData;
    CreateContextArg.PrivateDriverDataSize = pCreateContext->PrivateDriverDataSize;

    if (DXGK_CB_FULL(Adapter, DxgkDdiCreateContext) != NULL)
    {
        if (!DxgkReferenceAdapter(Adapter))
        {
            ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
            DxgkpDereferenceDevice(Device);
            return STATUS_DELETE_PENDING;
        }
        if (!DxgkBeginKmdTransaction(Adapter))
        {
            DxgkDereferenceAdapter(Adapter);
            ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
            DxgkpDereferenceDevice(Device);
            return STATUS_DEVICE_NOT_READY;
        }
        KmdTransactionStarted = TRUE;
        if (InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
            ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
            DxgkpDereferenceDevice(Device);
            return STATUS_DEVICE_REMOVED;
        }
        Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateContext)(Device->hMiniportDevice, &CreateContextArg);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkCreateContext: DxgkDdiCreateContext failed 0x%08lX\n", Status);
            if (CreateContextArg.hContext != NULL && CreateContextArg.hContext != (HANDLE)Context)
            {
                Context->hMiniportContext = CreateContextArg.hContext;
                DxgkpRollbackCreatedContext(Context);
                DxgkEndKmdTransaction(Adapter);
                DxgkDereferenceAdapter(Adapter);
                return Status;
            }
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
            ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
            DxgkpDereferenceDevice(Device);
            return Status;
        }

        Context->hMiniportContext = CreateContextArg.hContext;
        Context->ContextInfo = CreateContextArg.ContextInfo;

        DXGKRNL_TRACE("DxgkCreateContext: miniport ctx %p DmaBufferSize=%u AllocationListSize=%u PatchLocationListSize=%u\n", Context->hMiniportContext, CreateContextArg.ContextInfo.DmaBufferSize, CreateContextArg.ContextInfo.AllocationListSize, CreateContextArg.ContextInfo.PatchLocationListSize);

        /*
         * Build the render ring dxgkrnl owns and hand its mapped addresses
         * plus the accepted geometry back, so the UMD writes commands,
         * allocation entries, and patch locations straight into the memory
         * D3DKMTRender will translate.
         */
        Status = Device->GdiDevice ? STATUS_SUCCESS : DxgkContextRenderInitialize(Context);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkCreateContext: render ring setup failed 0x%08lX\n", Status);
            DxgkpRollbackCreatedContext(Context);
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
            return Status;
        }
        pCreateContext->pCommandBuffer = Context->RenderRingUser;
        pCreateContext->CommandBufferSize = Context->RenderCommandBufferSize;
        pCreateContext->pAllocationList = (D3DDDI_ALLOCATIONLIST *)((PUCHAR)Context->RenderRingUser + Context->RenderAllocationListOffset);
        pCreateContext->AllocationListSize = Context->RenderAllocationListSize;
        pCreateContext->pPatchLocationList = (D3DDDI_PATCHLOCATIONLIST *)((PUCHAR)Context->RenderRingUser + Context->RenderPatchLocationListOffset);
        pCreateContext->PatchLocationListSize = Context->RenderPatchLocationListSize;
    }
    else
    {
        DXGKRNL_WARN("DxgkCreateContext: miniport has no DxgkDdiCreateContext\n");
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkpDereferenceDevice(Device);
        return STATUS_NOT_SUPPORTED;
    }

    Status = DxgkpCreateContextStream(Context);
    if (!NT_SUCCESS(Status))
    {
        DxgkpRollbackCreatedContext(Context);
        if (KmdTransactionStarted)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
        }
        return Status;
    }

    Status = DxgkCreateContextHandle(Context, Device->OwnerProcess, &Context->Handle);
    if (!NT_SUCCESS(Status))
    {
        DxgkpRollbackCreatedContext(Context);
        if (KmdTransactionStarted)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
        }
        return Status;
    }

    DXGKRNL_TRACE("DxgkCreateContext: Context %p handle 0x%08X\n", Context, Context->Handle);

    ExAcquireFastMutex(&Device->DeviceMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&Device->DeviceMutex);
        DxgkRemoveContextHandleObject(Context);
        DxgkpRollbackCreatedContext(Context);
        if (KmdTransactionStarted)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
        }
        return STATUS_DELETE_PENDING;
    }
    InsertTailList(&Device->ContextListHead, &Context->ContextListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);

    pCreateContext->hContext = Context->Handle;

    DXGKRNL_TRACE("DxgkCreateContext: success hContext=0x%08X\n", pCreateContext->hContext);
    if (KmdTransactionStarted)
    {
        DxgkEndKmdTransaction(Adapter);
        DxgkDereferenceAdapter(Adapter);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkpCreateContextWithAccessMode(
    _Inout_ D3DKMT_CREATECONTEXT *pCreateContext,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    D3DKMT_CREATECONTEXT CapturedContext;
    PVOID CapturedPrivateData = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (pCreateContext == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pCreateContext->PrivateDriverDataSize > DXGKP_MAX_USER_PRIVATE_DATA)
        return STATUS_INVALID_BUFFER_SIZE;

    CapturedContext = *pCreateContext;
    CapturedContext.hContext = 0;
    CapturedContext.pCommandBuffer = NULL;
    CapturedContext.CommandBufferSize = 0;
    CapturedContext.pAllocationList = NULL;
    CapturedContext.AllocationListSize = 0;
    CapturedContext.pPatchLocationList = NULL;
    CapturedContext.PatchLocationListSize = 0;
    CapturedContext.CommandBuffer = 0;
    if (CapturedContext.PrivateDriverDataSize != 0)
    {
        Status = DxgkpCaptureUserBuffer(CapturedContext.pPrivateDriverData, CapturedContext.PrivateDriverDataSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, &CapturedPrivateData);
        if (!NT_SUCCESS(Status))
            return Status;
        CapturedContext.pPrivateDriverData = CapturedPrivateData;
    }

    Status = DxgkpCreateContextCaptured(&CapturedContext);
    if (NT_SUCCESS(Status))
    {
        pCreateContext->hContext = CapturedContext.hContext;
        pCreateContext->pCommandBuffer = CapturedContext.pCommandBuffer;
        pCreateContext->CommandBufferSize = CapturedContext.CommandBufferSize;
        pCreateContext->pAllocationList = CapturedContext.pAllocationList;
        pCreateContext->AllocationListSize = CapturedContext.AllocationListSize;
        pCreateContext->pPatchLocationList = CapturedContext.pPatchLocationList;
        pCreateContext->PatchLocationListSize = CapturedContext.PatchLocationListSize;
        pCreateContext->CommandBuffer = CapturedContext.CommandBuffer;
    }

    if (CapturedPrivateData != NULL)
        ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
    return Status;
}

NTSTATUS
NTAPI
DxgkCreateContext(
    _Inout_ D3DKMT_CREATECONTEXT *pCreateContext)
{
    return DxgkpCreateContextWithAccessMode(pCreateContext, KernelMode);
}

NTSTATUS
DxgkCreateCddContext(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ D3DKMT_CREATECONTEXT *CreateContext)
{
    DXGKARG_GETNODEMETADATA Metadata;
    D3DKMT_CREATECONTEXTVIRTUAL VirtualContext;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL || CreateContext == NULL || PsGetCurrentProcess() != PsInitialSystemProcess)
        return STATUS_INVALID_PARAMETER;
    if (DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata) == NULL)
        return DxgkCreateContext(CreateContext);
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    RtlZeroMemory(&Metadata, sizeof(Metadata));
    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata)(Adapter->MiniportDeviceContext, CreateContext->NodeOrdinal, &Metadata);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Metadata.GpuMmuSupported && !Metadata.IoMmuSupported)
        return DxgkCreateContext(CreateContext);
    /* GDI must use the node's addressing contract, just like user contexts.
     * The GPUVA context creator installs the process root page table. */
    RtlZeroMemory(&VirtualContext, sizeof(VirtualContext));
    VirtualContext.hDevice = CreateContext->hDevice;
    VirtualContext.NodeOrdinal = CreateContext->NodeOrdinal;
    VirtualContext.EngineAffinity = CreateContext->EngineAffinity;
    Status = DxgkCreateContextVirtual(&VirtualContext);
    if (NT_SUCCESS(Status))
        CreateContext->hContext = VirtualContext.hContext;
    return Status;
}

/*
 * D3DKMTCreateContextVirtual is a thunk-layer entry point, not a distinct
 * miniport DDI. The WDDM 2.0 contract uses DxgkDdiCreateContext with the
 * VirtualAddressing KMD flag set, then submits through
 * DxgkDdiSubmitCommandVirtual.
 */
NTSTATUS
NTAPI
DxgkCreateContextVirtual(
    _Inout_ D3DKMT_CREATECONTEXTVIRTUAL *pCreateContext)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_CONTEXT Context;
    DXGKARG_CREATECONTEXT CreateContextArg;
    NTSTATUS Status;
    BOOLEAN KmdTransactionStarted = FALSE;

    PAGED_CODE();

    if (pCreateContext == NULL || (pCreateContext->Flags.Value & ~RXGK_CREATECONTEXTVIRTUAL_SUPPORTED_FLAGS) != 0)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceOwnedDeviceByHandle(pCreateContext->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;

    if (pCreateContext->NodeOrdinal >= Adapter->NodeCount ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_0) ||
        DXGK_CB_FULL(Adapter, DxgkDdiCreateContext) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiDestroyContext) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual) == NULL)
    {
        DXGKRNL_ERR("DxgkCreateContextVirtual: capability gate node=%lu nodes=%lu version=0x%lX create=%p destroy=%p submit-virtual=%p\n",
                    pCreateContext->NodeOrdinal,
                    Adapter->NodeCount,
                    Adapter->MiniportContext->InitData.s.Version,
                    DXGK_CB_FULL(Adapter, DxgkDdiCreateContext),
                    DXGK_CB_FULL(Adapter, DxgkDdiDestroyContext),
                    DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual));
        DxgkpDereferenceDevice(Device);
        return STATUS_NOT_SUPPORTED;
    }
    if (!pCreateContext->Flags.NullRendering)
    {
        Status = DxgkGpuVaPreparePageTable(Adapter,
                                           Device->ProcessRecord,
                                           Device->hMiniportDevice);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkCreateContextVirtual: GPUVA preparation failed 0x%08lX mode=%u mmu=%u levels=%u\n",
                        Status,
                        Adapter->GpuMmuCaps.PageTableUpdateMode,
                        Adapter->GpuMmuCapsValid,
                        Adapter->PageTableLevelsValid);
            DxgkpDereferenceDevice(Device);
            return Status;
        }
    }

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), TAG_DXGK_CONTEXT);
    if (Context == NULL)
    {
        DxgkpDereferenceDevice(Device);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Device = Device;
    Context->NodeOrdinal = pCreateContext->NodeOrdinal;
    Context->EngineAffinity = pCreateContext->EngineAffinity;
    Context->SchedulingPriority = 0;
#if (REACTOS_WDDM_TARGET_LEVEL >= 1300)
    Context->InProcessSchedulingPriority = 0;
#endif
    Context->VirtualAddressing = TRUE;
    Context->UserModeCreateFlags = pCreateContext->Flags;
    Context->ReferenceCount = 1;
    InitializeListHead(&Context->ContextListEntry);
    KeInitializeEvent(&Context->ReferencesDrainedEvent, NotificationEvent, FALSE);
    DxgkpInitializeContextStreamState(Context);

    RtlZeroMemory(&CreateContextArg, sizeof(CreateContextArg));
    CreateContextArg.hContext = (HANDLE)Context;
    CreateContextArg.NodeOrdinal = pCreateContext->NodeOrdinal;
    CreateContextArg.EngineAffinity = pCreateContext->EngineAffinity;
    CreateContextArg.Flags.Value = 0;
    CreateContextArg.Flags.VirtualAddressing = 1;
    CreateContextArg.Flags.GdiContext = Device->GdiDevice;
    CreateContextArg.pPrivateDriverData = pCreateContext->pPrivateDriverData;
    CreateContextArg.PrivateDriverDataSize = pCreateContext->PrivateDriverDataSize;

    if (!DxgkReferenceAdapter(Adapter))
    {
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkpDereferenceDevice(Device);
        return STATUS_DELETE_PENDING;
    }
    if (!DxgkBeginKmdTransaction(Adapter))
    {
        DxgkDereferenceAdapter(Adapter);
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkpDereferenceDevice(Device);
        return STATUS_DEVICE_NOT_READY;
    }
    KmdTransactionStarted = TRUE;
    if (InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        DxgkEndKmdTransaction(Adapter);
        DxgkDereferenceAdapter(Adapter);
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkpDereferenceDevice(Device);
        return STATUS_DEVICE_REMOVED;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateContext)(Device->hMiniportDevice, &CreateContextArg);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkCreateContextVirtual: DxgkDdiCreateContext=%p failed 0x%08lX device=%p context=%p node=%lu engine=0x%X flags=0x%X private=%p bytes=%u arg-size=%Iu\n",
                    DXGK_CB_FULL(Adapter, DxgkDdiCreateContext),
                    Status,
                    Device->hMiniportDevice,
                    CreateContextArg.hContext,
                    CreateContextArg.NodeOrdinal,
                    CreateContextArg.EngineAffinity,
                    CreateContextArg.Flags.Value,
                    CreateContextArg.pPrivateDriverData,
                    CreateContextArg.PrivateDriverDataSize,
                    sizeof(CreateContextArg));
        if (CreateContextArg.hContext != NULL && CreateContextArg.hContext != (HANDLE)Context)
        {
            Context->hMiniportContext = CreateContextArg.hContext;
            DxgkpRollbackCreatedContext(Context);
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
            return Status;
        }
        DxgkEndKmdTransaction(Adapter);
        DxgkDereferenceAdapter(Adapter);
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkpDereferenceDevice(Device);
        return Status;
    }

    Context->hMiniportContext = CreateContextArg.hContext;
    Context->ContextInfo = CreateContextArg.ContextInfo;
    /* Associate the root with the miniport context only when the scheduler
     * dispatches its first non-null virtual packet. */

    Status = DxgkpCreateContextStream(Context);
    if (!NT_SUCCESS(Status))
    {
        DxgkpRollbackCreatedContext(Context);
        if (KmdTransactionStarted)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
        }
        return Status;
    }

    Status = DxgkCreateContextHandle(Context, Device->OwnerProcess, &Context->Handle);
    if (!NT_SUCCESS(Status))
    {
        DxgkpRollbackCreatedContext(Context);
        if (KmdTransactionStarted)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
        }
        return Status;
    }

    ExAcquireFastMutex(&Device->DeviceMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&Device->DeviceMutex);
        DxgkRemoveContextHandleObject(Context);
        DxgkpRollbackCreatedContext(Context);
        if (KmdTransactionStarted)
        {
            DxgkEndKmdTransaction(Adapter);
            DxgkDereferenceAdapter(Adapter);
        }
        return STATUS_DELETE_PENDING;
    }
    InsertTailList(&Device->ContextListHead, &Context->ContextListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);

    pCreateContext->hContext = Context->Handle;
    if (KmdTransactionStarted)
    {
        DxgkEndKmdTransaction(Adapter);
        DxgkDereferenceAdapter(Adapter);
    }
    return STATUS_SUCCESS;
}

/*
 * DxgkDestroyContext
 *
 * D3DKMTDestroyContext kernel entry point.
 *
 * Removes the context from its device's ContextListHead, calls
 * DxgkDdiDestroyContext on the miniport, and frees the pool allocation.
 */
NTSTATUS
NTAPI
DxgkDestroyContext(
    _In_ D3DKMT_DESTROYCONTEXT *pDestroyContext)
{
    PDXGKRNL_CONTEXT Context;
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkDestroyContext: hContext=0x%08X\n", pDestroyContext->hContext);

    /* --- Validate handle ------------------------------------------------- */

    Status = DxgkpDetachOwnedContextByHandle(pDestroyContext->hContext, PsGetCurrentProcess(), &Context);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkDestroyContext: invalid handle 0x%08X\n", pDestroyContext->hContext);
        return STATUS_INVALID_PARAMETER;
    }

    Device = Context->Device;
    DXGKRNL_TRACE("DxgkDestroyContext: Context %p on Device %p\n", Context, Context->Device);

    /* --- Call miniport destroy and free ---------------------------------- */

    Status = DxgkpDestroyContextNoLock(Context);
    if (!NT_SUCCESS(Status))
    {
        DxgkpRetainDetachedContext(Context);
        InterlockedExchange(&Device->MiniportDestroyPending, 1);
        DxgkpRetainDetachedDevice(Device);
        return Status;
    }

    DXGKRNL_TRACE("DxgkDestroyContext: done hContext=0x%08X\n", pDestroyContext->hContext);
    return Status;
}

/*
 * DxgkProcessCleanup
 *
 * PCREATE_PROCESS_NOTIFY_ROUTINE_EX callback.  Invoked at PASSIVE_LEVEL
 * when a process is created (CreateInfo != NULL) or exits (CreateInfo == NULL).
 *
 * On exit (CreateInfo == NULL): removes every owned device from the public
 * handle namespace, waits for transient users, and tears down contexts,
 * synchronization objects, miniport devices, and the shared miniport process.
 */
VOID
NTAPI
DxgkProcessCleanup(
    _Inout_  PEPROCESS              Process,
    _In_        HANDLE                 ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    PDXGKRNL_ADAPTER Snapshot[DXGK_MAX_ADAPTERS];
    ULONG            Count, i;

    UNREFERENCED_PARAMETER(ProcessId);

    /* Ignore process-creation notifications; only act on exits. */
    if (CreateInfo != NULL)
        return;

    /*
     * Tear down any lingering user-mode GPU mappings before the process VAD
     * tree is destroyed. User-mode runtimes do not always unlock everything
     * before exit, and MmMapLockedPagesSpecifyCache(UserMode) uses dedicated
     * VADs that ARM3 expects to be gone by this point.
     */
    DxgkAdapterProcessCleanup(Process);
    DxgkVidMmProcessCleanup(Process);
    DxgkD3dkmtProcessCleanup(Process);
    DxgkKeyedMutexProcessCleanup(Process);
    DxgkTrimNotificationProcessCleanup(Process);
    DxgkProcessPriorityCleanup(Process);
    DxgkVidMmReleaseProcessReservations(Process);
    DxgkPurgeProcessHandles(Process);
    Count = DxgkReferenceStartedAdapters(Snapshot, DXGK_MAX_ADAPTERS);

    for (i = 0; i < Count; ++i)
    {
        PDXGKRNL_ADAPTER Adapter = Snapshot[i];
        PDXGKRNL_DEVICE DetachedHead = NULL;

        if (!DxgkBeginDeviceLifecycleOperation(Adapter))
        {
            DxgkDereferenceAdapter(Adapter);
            continue;
        }

        (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
        {
            PLIST_ENTRY Entry = Adapter->DeviceListHead.Flink;

            while (Entry != &Adapter->DeviceListHead)
            {
                PLIST_ENTRY Next = Entry->Flink;
                PDXGKRNL_DEVICE Device = CONTAINING_RECORD(Entry, DXGKRNL_DEVICE, DeviceListEntry);

                if (Device->OwnerProcess == Process)
                {
                    BOOLEAN OwnsTeardown = DxgkTryClaimTeardown(&Device->TeardownClaimed);

                    DxgkSyncObjectCancelDeviceWaits(Device, STATUS_PROCESS_IS_TERMINATING, TRUE);
                    DxgkDeviceBeginDestroy(Device);
                    RemoveEntryList(&Device->DeviceListEntry);
                    if (OwnsTeardown || InterlockedCompareExchange(&Device->MiniportDestroyPending, 0, 0) != 0)
                    {
                        Device->DeviceListEntry.Flink = (PLIST_ENTRY)DetachedHead;
                        DetachedHead = Device;
                    }
                    else
                    {
                        InitializeListHead(&Device->DeviceListEntry);
                    }
                }
                Entry = Next;
            }
        }
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

        while (DetachedHead != NULL)
        {
            PDXGKRNL_DEVICE Device = DetachedHead;

            DetachedHead = (PDXGKRNL_DEVICE)Device->DeviceListEntry.Flink;
            InitializeListHead(&Device->DeviceListEntry);
            DxgkpDestroyDetachedDevice(Device);
        }
        DxgkEndDeviceLifecycleOperation(Adapter);
        DxgkDereferenceAdapter(Adapter);
    }

    /*
     * The early pass closes admission through the ExitStatus-backed VidMm
     * tombstone; this idempotent pass also retires any zero-charge record
     * retained while device teardown released its final placement.
     */
    DxgkVidMmReleaseProcessReservations(Process);
}

/* EOF */
