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
 *       FAST_MUTEX (different IRQL domains).  Snapshot adapter pointers
 *       under this lock, then release before calling any APC-level operation.
 *
 *   Adapter->AdapterMutex (FAST_MUTEX, APC_LEVEL)
 *     — protects Adapter->DeviceListHead.  Must not be held while calling
 *       miniport DDIs.
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

static VOID
DxgkpWaitForDeviceReferences(
    _In_ PDXGKRNL_DEVICE Device)
{
    if (InterlockedDecrement(&Device->ReferenceCount) != 0)
        KeWaitForSingleObject(&Device->ReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
}

static VOID
DxgkpWaitForContextReferences(
    _In_ PDXGKRNL_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->ReferenceCount) != 0)
        KeWaitForSingleObject(&Context->ReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
}

static NTSTATUS
DxgkpAcquireProcessRecord(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Out_ PDXGKRNL_PROCESS *OutProcessRecord)
{
    PDXGKRNL_PROCESS Candidate;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    *OutProcessRecord = NULL;

    ExAcquireFastMutex(&DxgkProcessListLock);
    for (Entry = DxgkProcessListHead.Flink; Entry != &DxgkProcessListHead; Entry = Entry->Flink)
    {
        PDXGKRNL_PROCESS Existing = CONTAINING_RECORD(Entry, DXGKRNL_PROCESS, GlobalProcessListEntry);

        if (Existing->Process == Process && Existing->Adapter == Adapter)
        {
            InterlockedIncrement(&Existing->ReferenceCount);
            ExReleaseFastMutex(&DxgkProcessListLock);
            *OutProcessRecord = Existing;
            return STATUS_SUCCESS;
        }
    }
    ExReleaseFastMutex(&DxgkProcessListLock);

    Candidate = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Candidate), TAG_DXGK_PROCESS);
    if (Candidate == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Candidate, sizeof(*Candidate));
    Candidate->Process = Process;
    Candidate->ReferenceCount = 1;
    InitializeListHead(&Candidate->DeviceListHead);
    InitializeListHead(&Candidate->AllocationListHead);
    InitializeListHead(&Candidate->GlobalProcessListEntry);
    ExInitializeFastMutex(&Candidate->ProcessMutex);
    ObReferenceObject(Process);

    Status = DxgkGpuVaCreateProcess(Adapter, Candidate);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        ExFreePoolWithTag(Candidate, TAG_DXGK_PROCESS);
        return Status;
    }
    ExAcquireFastMutex(&DxgkProcessListLock);
    for (Entry = DxgkProcessListHead.Flink; Entry != &DxgkProcessListHead; Entry = Entry->Flink)
    {
        PDXGKRNL_PROCESS Existing = CONTAINING_RECORD(Entry, DXGKRNL_PROCESS, GlobalProcessListEntry);

        if (Existing->Process == Process && Existing->Adapter == Adapter)
        {
            InterlockedIncrement(&Existing->ReferenceCount);
            ExReleaseFastMutex(&DxgkProcessListLock);
            DxgkGpuVaDestroyProcess(Adapter, Candidate);
            ObDereferenceObject(Process);
            ExFreePoolWithTag(Candidate, TAG_DXGK_PROCESS);
            *OutProcessRecord = Existing;
            return STATUS_SUCCESS;
        }
    }

    InsertTailList(&DxgkProcessListHead, &Candidate->GlobalProcessListEntry);
    ExReleaseFastMutex(&DxgkProcessListLock);
    *OutProcessRecord = Candidate;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkReferenceProcessRecordByAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Out_ PDXGKRNL_PROCESS *OutProcessRecord)
{
    PLIST_ENTRY Entry;

    if (Adapter == NULL || Process == NULL || OutProcessRecord == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutProcessRecord = NULL;
    ExAcquireFastMutex(&DxgkProcessListLock);
    for (Entry = DxgkProcessListHead.Flink; Entry != &DxgkProcessListHead; Entry = Entry->Flink)
    {
        PDXGKRNL_PROCESS ProcessRecord = CONTAINING_RECORD(Entry, DXGKRNL_PROCESS, GlobalProcessListEntry);

        if (ProcessRecord->Adapter != Adapter || ProcessRecord->Process != Process)
            continue;

        InterlockedIncrement(&ProcessRecord->ReferenceCount);
        ExReleaseFastMutex(&DxgkProcessListLock);
        *OutProcessRecord = ProcessRecord;
        return STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkProcessListLock);
    return STATUS_NOT_FOUND;
}

VOID
DxgkDereferenceProcessRecord(
    _In_opt_ PDXGKRNL_PROCESS ProcessRecord)
{
    BOOLEAN Destroy = FALSE;

    if (ProcessRecord == NULL)
        return;

    ExAcquireFastMutex(&DxgkProcessListLock);
    if (InterlockedDecrement(&ProcessRecord->ReferenceCount) == 0)
    {
        RemoveEntryList(&ProcessRecord->GlobalProcessListEntry);
        InitializeListHead(&ProcessRecord->GlobalProcessListEntry);
        Destroy = TRUE;
    }
    ExReleaseFastMutex(&DxgkProcessListLock);

    if (Destroy)
    {
        DxgkGpuVaDestroyProcess(ProcessRecord->Adapter, ProcessRecord);
        ObDereferenceObject(ProcessRecord->Process);
        ExFreePoolWithTag(ProcessRecord, TAG_DXGK_PROCESS);
    }
}

/* PRIVATE HELPERS ***********************************************************/

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

    if (MiniportContext == NULL || Adapter->MiniportContext->InitData.s.DxgkDdiDestroyContext == NULL)
        return STATUS_SUCCESS;
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_SUCCESS;
    Status = Adapter->MiniportContext->InitData.s.DxgkDdiDestroyContext(MiniportContext);
    DxgkReleaseMiniportCallback(Adapter);
    return Status;
}

static NTSTATUS
DxgkpDestroyMiniportDevice(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ HANDLE MiniportDevice)
{
    NTSTATUS Status;

    if (MiniportDevice == NULL || Adapter->MiniportContext->InitData.s.DxgkDdiDestroyDevice == NULL)
        return STATUS_SUCCESS;
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_SUCCESS;
    Status = Adapter->MiniportContext->InitData.s.DxgkDdiDestroyDevice(MiniportDevice);
    DxgkReleaseMiniportCallback(Adapter);
    return Status;
}

/* Context must be detached from Device->ContextListHead and DeviceMutex must
 * not be held. The helper waits transient references before final teardown. */
static VOID
DxgkpDestroyContextNoLock(
    _In_ _Post_invalid_ PDXGKRNL_CONTEXT Context)
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
    DxgkpWaitForContextReferences(Context);

    DXGKRNL_TRACE("DxgkpDestroyContextNoLock: Context %p hMiniport %p\n", Context, Context->hMiniportContext);

    Status = DxgkpDestroyMiniportContext(Adapter, Context->hMiniportContext);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpDestroyContextNoLock: DxgkDdiDestroyContext failed 0x%08lX\n", Status);
    }

#if DBG
    Context->Handle           = 0xDEADCCCC;
    Context->hMiniportContext = (HANDLE)(ULONG_PTR)0xDEADCCCCDEADCCCCULL;
#endif

    ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
    DxgkDereferenceDevice(Device);
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

    if (Adapter->MiniportContext->InitData.s.DxgkDdiQueryCurrentFence == NULL)
    {
        DXGKRNL_WARN("DxgkpQueryFence: no DxgkDdiQueryCurrentFence DDI\n");
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&FenceArg, sizeof(FenceArg));

    Status = Adapter->MiniportContext->InitData.s.DxgkDdiQueryCurrentFence(
                 Adapter->MiniportDeviceContext,
                 &FenceArg);

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
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PsSetCreateProcessNotifyRoutineEx(DxgkProcessCleanup, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkContextInit: PsSetCreateProcessNotifyRoutineEx "
                    "failed 0x%08lX\n", Status);
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
NTSTATUS
NTAPI
DxgkCreateDevice(
    _Inout_ D3DKMT_CREATEDEVICE *pCreateDevice)
{
    PDXGKRNL_ADAPTER     Adapter;
    PDXGKRNL_DEVICE      Device;
    DXGKARG_CREATEDEVICE CreateDeviceArg;
    NTSTATUS             Status;

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

    /* --- Allocate the device -------------------------------------------- */

    Device = (PDXGKRNL_DEVICE)ExAllocatePoolWithTag(NonPagedPool, sizeof(DXGKRNL_DEVICE), TAG_DXGK_DEVICE);
    if (Device == NULL)
    {
        DXGKRNL_ERR("DxgkCreateDevice: pool alloc failed (%Iu bytes)\n", sizeof(DXGKRNL_DEVICE));
        DxgkDereferenceAdapter(Adapter);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Device, sizeof(DXGKRNL_DEVICE));

    /* --- Initialise fields ----------------------------------------------- */

    Device->Adapter = Adapter;
    Device->OwnerProcess = PsGetCurrentProcess();
    Device->Flags   = pCreateDevice->Flags;
    Device->ReferenceCount = 1;

    InitializeListHead(&Device->ContextListHead);
    InitializeListHead(&Device->SyncObjListHead);
    InitializeListHead(&Device->DeviceListEntry);
    ExInitializeFastMutex(&Device->DeviceMutex);
    KeInitializeEvent(&Device->ReferencesDrainedEvent, NotificationEvent, FALSE);

    Status = DxgkpAcquireProcessRecord(Adapter, Device->OwnerProcess, &Device->ProcessRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
        DxgkDereferenceAdapter(Adapter);
        return Status;
    }

    /* --- Call DxgkDdiCreateDevice ---------------------------------------- */

    RtlZeroMemory(&CreateDeviceArg, sizeof(CreateDeviceArg));
    CreateDeviceArg.hDevice             = (HANDLE)Device; /* raw pointer as token */
    /* UMD D3DKMT flags are not bit-compatible with the KMD device flags. */
    CreateDeviceArg.Flags.Value         = 0;
    CreateDeviceArg.hKmdProcess         = Device->ProcessRecord->hMiniportProcess;

    if (Adapter->MiniportContext->InitData.s.DxgkDdiCreateDevice != NULL)
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiCreateDevice(Adapter->MiniportDeviceContext, &CreateDeviceArg);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkCreateDevice: DxgkDdiCreateDevice failed 0x%08lX\n", Status);
            DxgkDereferenceProcessRecord(Device->ProcessRecord);
            ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
            DxgkDereferenceAdapter(Adapter);
            return Status;
        }

        /*
         * The miniport wrote its opaque per-device context back into hDevice.
         * Store it in hMiniportDevice; it will be passed to DxgkDdiDestroyDevice
         * and to DxgkDdiCreateContext (as MiniportDeviceContext per-device).
         */
        Device->hMiniportDevice = CreateDeviceArg.hDevice;

        DXGKRNL_TRACE("DxgkCreateDevice: miniport device handle %p\n", Device->hMiniportDevice);
    }
    else
    {
        DXGKRNL_WARN("DxgkCreateDevice: miniport has no DxgkDdiCreateDevice\n");
        DxgkDereferenceProcessRecord(Device->ProcessRecord);
        ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
        DxgkDereferenceAdapter(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    Status = DxgkCreateDeviceHandle(Device, Device->OwnerProcess, &Device->Handle);
    if (!NT_SUCCESS(Status))
    {
        (VOID)DxgkpDestroyMiniportDevice(Adapter, Device->hMiniportDevice);
        DxgkDereferenceProcessRecord(Device->ProcessRecord);
        ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
        DxgkDereferenceAdapter(Adapter);
        return Status;
    }

    DXGKRNL_TRACE("DxgkCreateDevice: Device %p handle 0x%08X\n", Device, Device->Handle);

    ExAcquireFastMutex(&Adapter->AdapterMutex);
    if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&Adapter->AdapterMutex);
        DxgkRemoveDeviceHandleObject(Device);
        (VOID)DxgkpDestroyMiniportDevice(Adapter, Device->hMiniportDevice);
        DxgkDereferenceProcessRecord(Device->ProcessRecord);
        ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
        DxgkDereferenceAdapter(Adapter);
        return STATUS_DELETE_PENDING;
    }
    InsertTailList(&Adapter->DeviceListHead, &Device->DeviceListEntry);
    ExReleaseFastMutex(&Adapter->AdapterMutex);

    pCreateDevice->hDevice = Device->Handle;

    DXGKRNL_TRACE("DxgkCreateDevice: success hDevice=0x%08X\n", pCreateDevice->hDevice);
    return STATUS_SUCCESS;
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
    ExAcquireFastMutex(&Device->Adapter->AdapterMutex);
    if (!IsListEmpty(&Device->DeviceListEntry))
    {
        RemoveEntryList(&Device->DeviceListEntry);
        InitializeListHead(&Device->DeviceListEntry);
    }
    ExReleaseFastMutex(&Device->Adapter->AdapterMutex);
    *OutAdapter = Device->Adapter;
    *OutDevice = Device;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpDestroyDetachedDevice(
    _In_ _Post_invalid_ PDXGKRNL_DEVICE Device)
{
    PDXGKRNL_ADAPTER Adapter = Device->Adapter;
    NTSTATUS Status;
    NTSTATUS FinalStatus = STATUS_SUCCESS;

    ASSERT(InterlockedCompareExchange(&Device->TeardownClaimed, 1, 1) == 1);
    InterlockedExchange(&Device->Destroying, 1);
    DxgkRemoveDeviceHandleObject(Device);
    DxgkVidPnCleanupDeviceOwners(Device);
    DxgkD3dkmtDeviceCleanup(Device);
    DxgkCleanupDeviceSynchronizationObjects(Device);

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
        if (!OwnsTeardown)
        {
            /* The direct owner retains this Device until its final release. */
            continue;
        }
        DxgkRemoveContextHandleObject(Context);
        DxgkpDestroyContextNoLock(Context);
    }

    DxgkpWaitForDeviceReferences(Device);
    DxgkVidMmCleanupDeviceAllocations(Device);
    Status = DxgkpDestroyMiniportDevice(Adapter, Device->hMiniportDevice);
    if (!NT_SUCCESS(Status))
        FinalStatus = Status;

#if DBG
    Device->Handle = 0xDEADDEAD;
    Device->hMiniportDevice = (HANDLE)(ULONG_PTR)0xDEADDEADDEADDEADULL;
#endif

    DxgkDereferenceProcessRecord(Device->ProcessRecord);
    ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
    DxgkDereferenceAdapter(Adapter);
    return FinalStatus;
}

VOID
DxgkCleanupAdapterDevices(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_DEVICE DetachedHead = NULL;

    if (Adapter == NULL)
        return;
    DxgkD3dkmtAdapterCleanup(Adapter);
    DxgkPurgeAdapterHandles(Adapter);
    ExAcquireFastMutex(&Adapter->AdapterMutex);
    while (!IsListEmpty(&Adapter->DeviceListHead))
    {
        PLIST_ENTRY Entry = Adapter->DeviceListHead.Flink;
        PDXGKRNL_DEVICE Device = CONTAINING_RECORD(Entry, DXGKRNL_DEVICE, DeviceListEntry);
        BOOLEAN OwnsTeardown = DxgkTryClaimTeardown(&Device->TeardownClaimed);

        InterlockedExchange(&Device->Destroying, 1);
        RemoveEntryList(Entry);
        if (OwnsTeardown)
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
    ExReleaseFastMutex(&Adapter->AdapterMutex);

    while (DetachedHead != NULL)
    {
        PDXGKRNL_DEVICE Device = DetachedHead;

        DetachedHead = (PDXGKRNL_DEVICE)Device->DeviceListEntry.Flink;
        InitializeListHead(&Device->DeviceListEntry);
        DxgkpDestroyDetachedDevice(Device);
    }
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
NTSTATUS
NTAPI
DxgkCreateContext(
    _Inout_ D3DKMT_CREATECONTEXT *pCreateContext)
{
    PDXGKRNL_ADAPTER      Adapter;
    PDXGKRNL_DEVICE       Device;
    PDXGKRNL_CONTEXT      Context;
    DXGKARG_CREATECONTEXT CreateContextArg;
    NTSTATUS              Status;

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
    Context->ReferenceCount = 1;

    InitializeListHead(&Context->ContextListEntry);
    KeInitializeEvent(&Context->ReferencesDrainedEvent, NotificationEvent, FALSE);

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
    CreateContextArg.pPrivateDriverData    = pCreateContext->pPrivateDriverData;
    CreateContextArg.PrivateDriverDataSize = pCreateContext->PrivateDriverDataSize;

    if (Adapter->MiniportContext->InitData.s.DxgkDdiCreateContext != NULL)
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiCreateContext(Device->hMiniportDevice, &CreateContextArg);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkCreateContext: DxgkDdiCreateContext failed 0x%08lX\n", Status);
            ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
            DxgkpDereferenceDevice(Device);
            return Status;
        }

        Context->hMiniportContext = CreateContextArg.hContext;
        Context->ContextInfo = CreateContextArg.ContextInfo;

        DXGKRNL_TRACE("DxgkCreateContext: miniport ctx %p DmaBufferSize=%u AllocationListSize=%u PatchLocationListSize=%u\n", Context->hMiniportContext, CreateContextArg.ContextInfo.DmaBufferSize, CreateContextArg.ContextInfo.AllocationListSize, CreateContextArg.ContextInfo.PatchLocationListSize);

        /*
         * Propagate DMA-buffer geometry to the caller so the UMD can
         * set up its command-buffer ring.  pCommandBuffer / pAllocationList /
         * pPatchLocationList (actual mapped addresses) are set by the DMA
         * submission path in dxgmms1 / dma.c; for now they remain NULL.
         */
        pCreateContext->CommandBufferSize = CreateContextArg.ContextInfo.DmaBufferSize;
        pCreateContext->AllocationListSize = CreateContextArg.ContextInfo.AllocationListSize;
        pCreateContext->PatchLocationListSize = CreateContextArg.ContextInfo.PatchLocationListSize;
    }
    else
    {
        DXGKRNL_WARN("DxgkCreateContext: miniport has no DxgkDdiCreateContext\n");
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkpDereferenceDevice(Device);
        return STATUS_NOT_SUPPORTED;
    }

    Status = DxgkCreateContextHandle(Context, Device->OwnerProcess, &Context->Handle);
    if (!NT_SUCCESS(Status))
    {
        (VOID)DxgkpDestroyMiniportContext(Adapter, Context->hMiniportContext);
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkDereferenceDevice(Device);
        return Status;
    }

    DXGKRNL_TRACE("DxgkCreateContext: Context %p handle 0x%08X\n", Context, Context->Handle);

    ExAcquireFastMutex(&Device->DeviceMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&Device->DeviceMutex);
        DxgkRemoveContextHandleObject(Context);
        (VOID)DxgkpDestroyMiniportContext(Adapter, Context->hMiniportContext);
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkDereferenceDevice(Device);
        return STATUS_DELETE_PENDING;
    }
    InsertTailList(&Device->ContextListHead, &Context->ContextListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);

    pCreateContext->hContext = Context->Handle;

    DXGKRNL_TRACE("DxgkCreateContext: success hContext=0x%08X\n", pCreateContext->hContext);
    return STATUS_SUCCESS;
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

    PAGED_CODE();

    if (pCreateContext == NULL || (pCreateContext->Flags.Value & ~RXGK_CREATECONTEXTVIRTUAL_SUPPORTED_FLAGS) != 0)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceOwnedDeviceByHandle(pCreateContext->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;

    if (pCreateContext->NodeOrdinal >= Adapter->NodeCount || Adapter->MiniportContext->InitData.s.Version < DXGKDDI_INTERFACE_VERSION_WDDM2_0 || DXGK_CB_FULL(Adapter, DxgkDdiCreateContext) == NULL || DXGK_CB_FULL(Adapter, DxgkDdiDestroyContext) == NULL || DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual) == NULL)
    {
        DxgkpDereferenceDevice(Device);
        return STATUS_NOT_SUPPORTED;
    }
    if (!pCreateContext->Flags.NullRendering && !DxgkGpuVaPageTableReady(Adapter, Device->ProcessRecord))
    {
        DxgkpDereferenceDevice(Device);
        return STATUS_NOT_SUPPORTED;
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
    Context->VirtualAddressing = TRUE;
    Context->UserModeCreateFlags = pCreateContext->Flags;
    Context->ReferenceCount = 1;
    InitializeListHead(&Context->ContextListEntry);
    KeInitializeEvent(&Context->ReferencesDrainedEvent, NotificationEvent, FALSE);

    RtlZeroMemory(&CreateContextArg, sizeof(CreateContextArg));
    CreateContextArg.hContext = (HANDLE)Context;
    CreateContextArg.NodeOrdinal = pCreateContext->NodeOrdinal;
    CreateContextArg.EngineAffinity = pCreateContext->EngineAffinity;
    CreateContextArg.Flags.Value = 0;
    CreateContextArg.Flags.VirtualAddressing = 1;
    CreateContextArg.pPrivateDriverData = pCreateContext->pPrivateDriverData;
    CreateContextArg.PrivateDriverDataSize = pCreateContext->PrivateDriverDataSize;

    Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateContext)(Device->hMiniportDevice, &CreateContextArg);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkpDereferenceDevice(Device);
        return Status;
    }

    Context->hMiniportContext = CreateContextArg.hContext;
    Context->ContextInfo = CreateContextArg.ContextInfo;
    if (pCreateContext->Flags.NullRendering)
        Status = STATUS_SUCCESS;
    else
        Status = DxgkGpuVaSetRootPageTable(Adapter, Device->ProcessRecord, Context);

    if (!NT_SUCCESS(Status))
    {
        (VOID)DxgkpDestroyMiniportContext(Adapter, Context->hMiniportContext);
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkpDereferenceDevice(Device);
        return Status;
    }

    Status = DxgkCreateContextHandle(Context, Device->OwnerProcess, &Context->Handle);
    if (!NT_SUCCESS(Status))
    {
        (VOID)DxgkpDestroyMiniportContext(Adapter, Context->hMiniportContext);
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkDereferenceDevice(Device);
        return Status;
    }

    ExAcquireFastMutex(&Device->DeviceMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&Device->DeviceMutex);
        DxgkRemoveContextHandleObject(Context);
        (VOID)DxgkpDestroyMiniportContext(Adapter, Context->hMiniportContext);
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        DxgkpDereferenceDevice(Device);
        return STATUS_DELETE_PENDING;
    }
    InsertTailList(&Device->ContextListHead, &Context->ContextListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);

    pCreateContext->hContext = Context->Handle;
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

    DxgkpDestroyContextNoLock(Context);

    DXGKRNL_TRACE("DxgkDestroyContext: done hContext=0x%08X\n", pDestroyContext->hContext);
    return STATUS_SUCCESS;
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
    PLIST_ENTRY      DevEntry;

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
    DxgkVidMmProcessCleanup(Process);
    DxgkD3dkmtProcessCleanup(Process);
    DxgkPurgeProcessHandles(Process);
    Count = DxgkReferenceStartedAdapters(Snapshot, DXGK_MAX_ADAPTERS);

    for (i = 0; i < Count; ++i)
    {
        PDXGKRNL_ADAPTER Adapter = Snapshot[i];
        PDXGKRNL_DEVICE DetachedHead = NULL;

        ExAcquireFastMutex(&Adapter->AdapterMutex);
        {
            PLIST_ENTRY Entry = Adapter->DeviceListHead.Flink;

            while (Entry != &Adapter->DeviceListHead)
            {
                PLIST_ENTRY Next = Entry->Flink;
                PDXGKRNL_DEVICE Device = CONTAINING_RECORD(Entry, DXGKRNL_DEVICE, DeviceListEntry);

                if (Device->OwnerProcess == Process)
                {
                    BOOLEAN OwnsTeardown = DxgkTryClaimTeardown(&Device->TeardownClaimed);

                    InterlockedExchange(&Device->Destroying, 1);
                    RemoveEntryList(&Device->DeviceListEntry);
                    if (OwnsTeardown)
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
        ExReleaseFastMutex(&Adapter->AdapterMutex);

        while (DetachedHead != NULL)
        {
            PDXGKRNL_DEVICE Device = DetachedHead;

            DetachedHead = (PDXGKRNL_DEVICE)Device->DeviceListEntry.Flink;
            InitializeListHead(&Device->DeviceListEntry);
            DxgkpDestroyDetachedDevice(Device);
        }
        DxgkDereferenceAdapter(Adapter);
    }
}

/* EOF */
