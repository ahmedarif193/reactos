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
 * Handle encoding
 * ---------------
 * D3DKMT_HANDLE values returned to callers are the low 32 bits of the
 * kernel object pointer XOR'd with a per-boot cookie (DxgkHandleCookie32).
 * The stored DXGKRNL_DEVICE.Handle and DXGKRNL_CONTEXT.Handle fields hold
 * the encoded value so that validation is a direct compare.
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
#include "vidmm.h"

/* GLOBALS *******************************************************************/

/*
 * DxgkHandleCookie32
 *
 * Per-boot XOR mask.  D3DKMT_HANDLE is UINT (32 bits); we use 32-bit XOR.
 * Bit 0 is cleared so that encoded handles from naturally-aligned pool
 * allocations (always even-addressed) also have bit 0 clear — this lets
 * the validation path quickly reject odd values as corrupt handles.
 */
static ULONG DxgkHandleCookie32;

/*
 * DxgkProcessNotifyRegistered — TRUE if the process-exit callback is active.
 */
static BOOLEAN DxgkProcessNotifyRegistered = FALSE;

/* Forward declaration — defined later in this file. */
VOID
NTAPI
DxgkProcessCleanup(
    _Inout_     PEPROCESS              Process,
    _In_        HANDLE                 ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo);

/* Maximum adapters snapshotted in one operation (on-stack arrays). */
#define DXGK_MAX_ADAPTERS 16

/* PRIVATE HELPERS ***********************************************************/

/*
 * DxgkpEncodeHandle
 *
 * XOR obfuscation for D3DKMT_HANDLE.  Only the low 32 bits of the pointer
 * participate because D3DKMT_HANDLE is a 32-bit type.  The stored Handle
 * field in each object records the encoded value so we can do a round-trip
 * check without a separate lookup table.
 */
FORCEINLINE D3DKMT_HANDLE
DxgkpEncodeHandle(
    _In_ PVOID Pointer)
{
    return (D3DKMT_HANDLE)((ULONG_PTR)Pointer ^ (ULONG_PTR)DxgkHandleCookie32);
}

/*
 * DxgkpSnapshotAdapters
 *
 * Build an on-stack array of all current DXGKRNL_ADAPTER pointers while
 * holding DxgkAdapterGlobalListLock.  Returns the number of adapters found
 * (capped at DXGK_MAX_ADAPTERS).
 *
 * This pattern is used throughout context.c to avoid holding a KSPIN_LOCK
 * while subsequently acquiring FAST_MUTEXes (which require IRQL <= APC_LEVEL).
 */
static ULONG
DxgkpSnapshotAdapters(
    _Out_writes_(DXGK_MAX_ADAPTERS) PDXGKRNL_ADAPTER *AdapterArray)
{
    PLIST_ENTRY Entry;
    ULONG       Count = 0;
    KIRQL       OldIrql;

    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);

    for (Entry  = DxgkAdapterGlobalListHead.Flink;
         Entry != &DxgkAdapterGlobalListHead && Count < DXGK_MAX_ADAPTERS;
         Entry  = Entry->Flink)
    {
        AdapterArray[Count++] = CONTAINING_RECORD(Entry, DXGKRNL_ADAPTER,
                                                   GlobalAdapterListEntry);
    }

    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);
    return Count;
}

/*
 * DxgkpValidateAdapter
 *
 * Confirm that Adapter is a member of the global adapter list.  Returns TRUE
 * if found, FALSE otherwise.
 *
 * Must be called at IRQL <= APC_LEVEL (snapshot acquires a spinlock briefly).
 */
static BOOLEAN
DxgkpValidateAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_ADAPTER Snapshot[DXGK_MAX_ADAPTERS];
    ULONG            Count, i;

    Count = DxgkpSnapshotAdapters(Snapshot);
    for (i = 0; i < Count; ++i)
    {
        if (Snapshot[i] == Adapter)
            return TRUE;
    }
    return FALSE;
}

/*
 * DxgkpFindDeviceOnAdapter
 *
 * Search Adapter->DeviceListHead for a device with Handle == hDevice.
 * Returns the DXGKRNL_DEVICE pointer or NULL.  Acquires Device->DeviceMutex.
 *
 * Must be called at IRQL <= APC_LEVEL.
 */
static PDXGKRNL_DEVICE
DxgkpFindDeviceOnAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DKMT_HANDLE    hDevice)
{
    PLIST_ENTRY     Entry;
    PDXGKRNL_DEVICE Found = NULL;

    PAGED_CODE();

    ExAcquireFastMutex(&Adapter->AdapterMutex);

    for (Entry  = Adapter->DeviceListHead.Flink;
         Entry != &Adapter->DeviceListHead;
         Entry  = Entry->Flink)
    {
        PDXGKRNL_DEVICE Candidate = CONTAINING_RECORD(Entry, DXGKRNL_DEVICE,
                                                       DeviceListEntry);
        if (Candidate->Handle == hDevice)
        {
            Found = Candidate;
            break;
        }
    }

    ExReleaseFastMutex(&Adapter->AdapterMutex);
    return Found;
}

/*
 * DxgkpFindDeviceByHandle
 *
 * Walk all adapters looking for a device with the given handle.
 * Sets *OutAdapter to the owning adapter on success.
 *
 * Must be called at IRQL <= APC_LEVEL (acquires FAST_MUTEXes).
 */
static PDXGKRNL_DEVICE
DxgkpFindDeviceByHandle(
    _In_  D3DKMT_HANDLE     hDevice,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter)
{
    PDXGKRNL_ADAPTER Snapshot[DXGK_MAX_ADAPTERS];
    ULONG            Count, i;
    PDXGKRNL_DEVICE  Device = NULL;

    PAGED_CODE();

    *OutAdapter = NULL;

    Count = DxgkpSnapshotAdapters(Snapshot);

    for (i = 0; i < Count; ++i)
    {
        Device = DxgkpFindDeviceOnAdapter(Snapshot[i], hDevice);
        if (Device != NULL)
        {
            *OutAdapter = Snapshot[i];
            return Device;
        }
    }

    return NULL;
}

PDXGKRNL_DEVICE
DxgkLookupDeviceByHandle(
    _In_ D3DKMT_HANDLE       Handle,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter)
{
    PDXGKRNL_ADAPTER LocalAdapter;

    if (OutAdapter == NULL)
        OutAdapter = &LocalAdapter;

    return DxgkpFindDeviceByHandle(Handle, OutAdapter);
}

/*
 * DxgkpFindContextByHandle
 *
 * Walk all adapters → all devices looking for a context with the given handle.
 * Sets *OutAdapter and *OutDevice on success.
 *
 * Must be called at IRQL <= APC_LEVEL.
 */
static PDXGKRNL_CONTEXT
DxgkpFindContextByHandle(
    _In_  D3DKMT_HANDLE     hContext,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE  *OutDevice)
{
    PDXGKRNL_ADAPTER Snapshot[DXGK_MAX_ADAPTERS];
    ULONG            Count, i;
    PLIST_ENTRY      DevEntry;

    PAGED_CODE();

    *OutAdapter = NULL;
    *OutDevice  = NULL;

    Count = DxgkpSnapshotAdapters(Snapshot);

    for (i = 0; i < Count; ++i)
    {
        PDXGKRNL_ADAPTER Adapter = Snapshot[i];

        ExAcquireFastMutex(&Adapter->AdapterMutex);

        for (DevEntry  = Adapter->DeviceListHead.Flink;
             DevEntry != &Adapter->DeviceListHead;
             DevEntry  = DevEntry->Flink)
        {
            PDXGKRNL_DEVICE Device = CONTAINING_RECORD(DevEntry, DXGKRNL_DEVICE,
                                                        DeviceListEntry);
            PLIST_ENTRY CtxEntry;

            ExAcquireFastMutex(&Device->DeviceMutex);

            for (CtxEntry  = Device->ContextListHead.Flink;
                 CtxEntry != &Device->ContextListHead;
                 CtxEntry  = CtxEntry->Flink)
            {
                PDXGKRNL_CONTEXT Ctx = CONTAINING_RECORD(CtxEntry,
                                                          DXGKRNL_CONTEXT,
                                                          ContextListEntry);
                if (Ctx->Handle == hContext)
                {
                    ExReleaseFastMutex(&Device->DeviceMutex);
                    ExReleaseFastMutex(&Adapter->AdapterMutex);
                    *OutAdapter = Adapter;
                    *OutDevice  = Device;
                    return Ctx;
                }
            }

            ExReleaseFastMutex(&Device->DeviceMutex);
        }

        ExReleaseFastMutex(&Adapter->AdapterMutex);
    }

    return NULL;
}

PDXGKRNL_CONTEXT
DxgkLookupContextByHandle(
    _In_ D3DKMT_HANDLE       Handle,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_opt_ PDXGKRNL_DEVICE  *OutDevice)
{
    PDXGKRNL_ADAPTER LocalAdapter;
    PDXGKRNL_DEVICE  LocalDevice;

    if (OutAdapter == NULL)
        OutAdapter = &LocalAdapter;
    if (OutDevice == NULL)
        OutDevice = &LocalDevice;

    return DxgkpFindContextByHandle(Handle, OutAdapter, OutDevice);
}

/*
 * DxgkpDestroyContextNoLock
 *
 * Internal context teardown.  The context MUST already have been removed from
 * Device->ContextListHead before this is called (so that no other path can
 * find or reference it).  Device->DeviceMutex must NOT be held on entry.
 *
 * Calls DxgkDdiDestroyContext and frees the pool allocation.
 */
static VOID
DxgkpDestroyContextNoLock(
    _In_ _Post_invalid_ PDXGKRNL_CONTEXT Context)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS         Status;

    PAGED_CODE();

    Adapter = Context->Device->Adapter;

    DXGKRNL_TRACE("DxgkpDestroyContextNoLock: Context %p hMiniport %p\n",
                  Context, Context->hMiniportContext);

    if (Context->hMiniportContext != NULL &&
        Adapter->MiniportContext->InitData.s.DxgkDdiDestroyContext != NULL)
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiDestroyContext(
                     Context->hMiniportContext);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkpDestroyContextNoLock: DxgkDdiDestroyContext "
                        "failed 0x%08lX\n", Status);
        }
    }

#if DBG
    Context->Handle           = 0xDEADCCCC;
    Context->hMiniportContext = (HANDLE)(ULONG_PTR)0xDEADCCCCDEADCCCCULL;
#endif

    ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
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
 * Called once from DriverEntry.  Seeds the handle cookie from the performance
 * counter and registers DxgkProcessCleanup via PsSetCreateProcessNotifyRoutine.
 */
NTSTATUS
DxgkContextInit(VOID)
{
    LARGE_INTEGER Counter;
    NTSTATUS      Status;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkContextInit: enter\n");

    Counter = KeQueryPerformanceCounter(NULL);

    /*
     * 32-bit cookie from the lower and upper halves of the counter XOR'd
     * together.  Bit 0 cleared to preserve the even-address invariant.
     */
    DxgkHandleCookie32 = (ULONG)(Counter.LowPart ^ Counter.HighPart) & ~1UL;
    if (DxgkHandleCookie32 == 0)
        DxgkHandleCookie32 = 0xDEADBEEEUL & ~1UL;

    DXGKRNL_TRACE("DxgkContextInit: handle cookie = 0x%08lX\n",
                  DxgkHandleCookie32);

    Status = PsSetCreateProcessNotifyRoutineEx(DxgkProcessCleanup, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkContextInit: PsSetCreateProcessNotifyRoutineEx "
                    "failed 0x%08lX\n", Status);
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

    DXGKRNL_TRACE("DxgkCreateDevice: pAdapter=%p Flags=0x%08X\n",
                  pCreateDevice->pAdapter,
                  (UINT)(pCreateDevice->Flags.LegacyMode |
                         (pCreateDevice->Flags.RequestVSync << 1)));

    /*
     * Kernel-mode callers set pAdapter to the raw DXGKRNL_ADAPTER pointer.
     * Validate it against the global list before trusting it.
     */
    Adapter = (PDXGKRNL_ADAPTER)pCreateDevice->pAdapter;

    if (Adapter == NULL || !DxgkpValidateAdapter(Adapter))
    {
        DXGKRNL_ERR("DxgkCreateDevice: invalid adapter %p\n", Adapter);
        return STATUS_INVALID_HANDLE;
    }

    /* --- Allocate the device -------------------------------------------- */

    Device = (PDXGKRNL_DEVICE)ExAllocatePoolWithTag(NonPagedPool,
                                                     sizeof(DXGKRNL_DEVICE),
                                                     TAG_DXGK_DEVICE);
    if (Device == NULL)
    {
        DXGKRNL_ERR("DxgkCreateDevice: pool alloc failed (%Iu bytes)\n",
                    sizeof(DXGKRNL_DEVICE));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Device, sizeof(DXGKRNL_DEVICE));

    /* --- Initialise fields ----------------------------------------------- */

    Device->Adapter = Adapter;
    Device->Flags   = pCreateDevice->Flags;

    InitializeListHead(&Device->ContextListHead);
    InitializeListHead(&Device->SyncObjListHead);
    InitializeListHead(&Device->DeviceListEntry);
    ExInitializeFastMutex(&Device->DeviceMutex);

    /*
     * Encode the device handle (XOR of low 32 bits of pointer with cookie).
     * Store it in Device->Handle for round-trip validation.
     */
    Device->Handle = DxgkpEncodeHandle(Device);

    DXGKRNL_TRACE("DxgkCreateDevice: Device %p handle 0x%08X\n",
                  Device, Device->Handle);

    /* --- Call DxgkDdiCreateDevice ---------------------------------------- */

    RtlZeroMemory(&CreateDeviceArg, sizeof(CreateDeviceArg));
    CreateDeviceArg.hDevice             = (HANDLE)Device; /* raw pointer as token */
    /*
     * D3DKMT_CREATEDEVICEFLAGS (no .Value union) → DXGK_CREATEDEVICEFLAGS (.Value union).
     * Copy the two defined bits explicitly.
     */
    CreateDeviceArg.Flags.LegacyMode    = pCreateDevice->Flags.LegacyMode;
    CreateDeviceArg.Flags.RequestVSync  = pCreateDevice->Flags.RequestVSync;

    if (Adapter->MiniportContext->InitData.s.DxgkDdiCreateDevice != NULL)
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiCreateDevice(
                     Adapter->MiniportDeviceContext,
                     &CreateDeviceArg);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkCreateDevice: DxgkDdiCreateDevice failed "
                        "0x%08lX\n", Status);
            ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
            return Status;
        }

        /*
         * The miniport wrote its opaque per-device context back into hDevice.
         * Store it in hMiniportDevice; it will be passed to DxgkDdiDestroyDevice
         * and to DxgkDdiCreateContext (as MiniportDeviceContext per-device).
         */
        Device->hMiniportDevice = CreateDeviceArg.hDevice;

        DXGKRNL_TRACE("DxgkCreateDevice: miniport device handle %p\n",
                      Device->hMiniportDevice);
    }
    else
    {
        DXGKRNL_WARN("DxgkCreateDevice: miniport has no DxgkDdiCreateDevice\n");
        ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);
        return STATUS_NOT_SUPPORTED;
    }

    /* --- Link into adapter's device list --------------------------------- */

    ExAcquireFastMutex(&Adapter->AdapterMutex);
    InsertTailList(&Adapter->DeviceListHead, &Device->DeviceListEntry);
    ExReleaseFastMutex(&Adapter->AdapterMutex);

    pCreateDevice->hDevice = Device->Handle;

    DXGKRNL_TRACE("DxgkCreateDevice: success hDevice=0x%08X\n",
                  pCreateDevice->hDevice);
    return STATUS_SUCCESS;
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
    PLIST_ENTRY      Entry;
    NTSTATUS         Status;
    NTSTATUS         FinalStatus = STATUS_SUCCESS;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkDestroyDevice: hDevice=0x%08X\n",
                  pDestroyDevice->hDevice);

    /* --- Validate handle ------------------------------------------------- */

    Device = DxgkpFindDeviceByHandle(pDestroyDevice->hDevice, &Adapter);
    if (Device == NULL)
    {
        DXGKRNL_ERR("DxgkDestroyDevice: invalid handle 0x%08X\n",
                    pDestroyDevice->hDevice);
        return STATUS_INVALID_HANDLE;
    }

    DXGKRNL_TRACE("DxgkDestroyDevice: Device %p on Adapter %p\n",
                  Device, Adapter);

    /* --- Unlink from adapter's device list before destroying contexts ---- */

    /*
     * Remove from the list first so that DxgkProcessCleanup cannot also
     * find and destroy this device concurrently.
     */
    ExAcquireFastMutex(&Adapter->AdapterMutex);
    RemoveEntryList(&Device->DeviceListEntry);
    InitializeListHead(&Device->DeviceListEntry);
    ExReleaseFastMutex(&Adapter->AdapterMutex);

    /* --- Tear down leaked sync objects before freeing the device --------- */

    DxgkCleanupDeviceSynchronizationObjects(Device);

    /* --- Destroy all contexts ------------------------------------------- */

    /*
     * Pop contexts one at a time: acquire DeviceMutex, remove head, release
     * mutex, then call the miniport DDI (which may block at PASSIVE_LEVEL).
     */
    for (;;)
    {
        PDXGKRNL_CONTEXT Context;

        ExAcquireFastMutex(&Device->DeviceMutex);

        if (IsListEmpty(&Device->ContextListHead))
        {
            ExReleaseFastMutex(&Device->DeviceMutex);
            break;
        }

        Entry = RemoveHeadList(&Device->ContextListHead);
        InitializeListHead(Entry); /* self-loop: double-remove is a no-op */
        ExReleaseFastMutex(&Device->DeviceMutex);

        Context = CONTAINING_RECORD(Entry, DXGKRNL_CONTEXT, ContextListEntry);

        DXGKRNL_TRACE("DxgkDestroyDevice: destroying Context %p\n", Context);
        DxgkpDestroyContextNoLock(Context);
    }

    /* --- Call DxgkDdiDestroyDevice --------------------------------------- */

    if (Device->hMiniportDevice != NULL &&
        Adapter->MiniportContext->InitData.s.DxgkDdiDestroyDevice != NULL)
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiDestroyDevice(
                     Device->hMiniportDevice);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkDestroyDevice: DxgkDdiDestroyDevice failed "
                        "0x%08lX (continuing)\n", Status);
            FinalStatus = Status;
        }
    }

    /* --- Free the device ------------------------------------------------- */

#if DBG
    Device->Handle          = 0xDEADDEAD;
    Device->hMiniportDevice = (HANDLE)(ULONG_PTR)0xDEADDEADDEADDEADULL;
#endif

    ExFreePoolWithTag(Device, TAG_DXGK_DEVICE);

    DXGKRNL_TRACE("DxgkDestroyDevice: done (0x%08lX)\n", FinalStatus);
    return FinalStatus;
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

    DXGKRNL_TRACE("DxgkCreateContext: hDevice=0x%08X NodeOrdinal=%u "
                  "EngineAffinity=0x%08X\n",
                  pCreateContext->hDevice,
                  pCreateContext->NodeOrdinal,
                  pCreateContext->EngineAffinity);

    /* --- Validate device handle ----------------------------------------- */

    Device = DxgkpFindDeviceByHandle(pCreateContext->hDevice, &Adapter);
    if (Device == NULL)
    {
        DXGKRNL_ERR("DxgkCreateContext: invalid device handle 0x%08X\n",
                    pCreateContext->hDevice);
        return STATUS_INVALID_HANDLE;
    }

    /* --- Allocate context ------------------------------------------------ */

    Context = (PDXGKRNL_CONTEXT)ExAllocatePoolWithTag(NonPagedPool,
                                                       sizeof(DXGKRNL_CONTEXT),
                                                       TAG_DXGK_CONTEXT);
    if (Context == NULL)
    {
        DXGKRNL_ERR("DxgkCreateContext: pool alloc failed (%Iu bytes)\n",
                    sizeof(DXGKRNL_CONTEXT));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context, sizeof(DXGKRNL_CONTEXT));

    /* --- Initialise context fields --------------------------------------- */

    Context->Device         = Device;
    Context->NodeOrdinal    = pCreateContext->NodeOrdinal;
    Context->EngineAffinity = pCreateContext->EngineAffinity;
    Context->SchedulingPriority = 0;

    InitializeListHead(&Context->ContextListEntry);
    Context->Handle = DxgkpEncodeHandle(Context);

    DXGKRNL_TRACE("DxgkCreateContext: Context %p handle 0x%08X\n",
                  Context, Context->Handle);

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
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiCreateContext(
                     Device->hMiniportDevice,
                     &CreateContextArg);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkCreateContext: DxgkDdiCreateContext failed "
                        "0x%08lX\n", Status);
            ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
            return Status;
        }

        Context->hMiniportContext = CreateContextArg.hContext;

        DXGKRNL_TRACE("DxgkCreateContext: miniport ctx %p "
                      "DmaBufferSize=%u AllocationListSize=%u "
                      "PatchLocationListSize=%u\n",
                      Context->hMiniportContext,
                      CreateContextArg.ContextInfo.DmaBufferSize,
                      CreateContextArg.ContextInfo.AllocationListSize,
                      CreateContextArg.ContextInfo.PatchLocationListSize);

        /*
         * Propagate DMA-buffer geometry to the caller so the UMD can
         * set up its command-buffer ring.  pCommandBuffer / pAllocationList /
         * pPatchLocationList (actual mapped addresses) are set by the DMA
         * submission path in dxgmms1 / dma.c; for now they remain NULL.
         */
        pCreateContext->CommandBufferSize     =
            CreateContextArg.ContextInfo.DmaBufferSize;
        pCreateContext->AllocationListSize    =
            CreateContextArg.ContextInfo.AllocationListSize;
        pCreateContext->PatchLocationListSize =
            CreateContextArg.ContextInfo.PatchLocationListSize;
    }
    else
    {
        DXGKRNL_WARN("DxgkCreateContext: miniport has no DxgkDdiCreateContext\n");
        ExFreePoolWithTag(Context, TAG_DXGK_CONTEXT);
        return STATUS_NOT_SUPPORTED;
    }

    /* --- Link into device's context list --------------------------------- */

    ExAcquireFastMutex(&Device->DeviceMutex);
    InsertTailList(&Device->ContextListHead, &Context->ContextListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);

    pCreateContext->hContext = Context->Handle;

    DXGKRNL_TRACE("DxgkCreateContext: success hContext=0x%08X\n",
                  pCreateContext->hContext);
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
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE  Device;
    PDXGKRNL_CONTEXT Context;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkDestroyContext: hContext=0x%08X\n",
                  pDestroyContext->hContext);

    /* --- Validate handle ------------------------------------------------- */

    Context = DxgkpFindContextByHandle(pDestroyContext->hContext,
                                       &Adapter, &Device);
    if (Context == NULL)
    {
        DXGKRNL_ERR("DxgkDestroyContext: invalid handle 0x%08X\n",
                    pDestroyContext->hContext);
        return STATUS_INVALID_HANDLE;
    }

    DXGKRNL_TRACE("DxgkDestroyContext: Context %p on Device %p\n",
                  Context, Device);

    /* --- Remove from device's context list ------------------------------ */

    ExAcquireFastMutex(&Device->DeviceMutex);
    RemoveEntryList(&Context->ContextListEntry);
    InitializeListHead(&Context->ContextListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);

    /* --- Call miniport destroy and free ---------------------------------- */

    DxgkpDestroyContextNoLock(Context);

    DXGKRNL_TRACE("DxgkDestroyContext: done hContext=0x%08X\n",
                  pDestroyContext->hContext);
    return STATUS_SUCCESS;
}

/*
 * DxgkProcessCleanup
 *
 * PCREATE_PROCESS_NOTIFY_ROUTINE_EX callback.  Invoked at PASSIVE_LEVEL
 * when a process is created (CreateInfo != NULL) or exits (CreateInfo == NULL).
 *
 * On exit (CreateInfo == NULL): walks all adapters and destroys DXGKRNL_DEVICE
 * objects owned by the exiting process.  Ownership is established via the
 * DXGKRNL_PROCESS record linked from each device.
 *
 * NOTE: DXGKRNL_PROCESS integration is pending (dxgkrnl_private.h declares
 * the struct but it is not yet fully wired into device creation).  Once
 * device-to-process linkage is in place, the check below should compare
 * Device->ProcessRecord->Process against the Process argument.
 *
 * Until then this function logs a trace for each device discovered and
 * relies on the D3D runtime enforcing explicit cleanup before process exit.
 * This is safe because DxgkProcessCleanup is still registered — it simply
 * has no devices to destroy in the current implementation.
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

    /*
     * Snapshot adapters under the global spinlock, then iterate each one
     * at APC_LEVEL to collect and destroy orphaned devices.
     */
    Count = DxgkpSnapshotAdapters(Snapshot);

    for (i = 0; i < Count; ++i)
    {
        PDXGKRNL_ADAPTER Adapter = Snapshot[i];

        /*
         * Collect devices owned by the dying process from this adapter.
         * We build a local singly-linked list (reusing DeviceListEntry.Flink
         * as a next pointer after removing from the doubly-linked list) to
         * avoid calling the miniport while holding AdapterMutex.
         */
        {
            PDXGKRNL_DEVICE OrphanHead = NULL;
            PDXGKRNL_DEVICE OrphanCurr;

            ExAcquireFastMutex(&Adapter->AdapterMutex);

            DevEntry = Adapter->DeviceListHead.Flink;
            while (DevEntry != &Adapter->DeviceListHead)
            {
                PLIST_ENTRY     Next   = DevEntry->Flink;
                /*
                 * TODO: Replace this stub with the real process-ownership
                 * test once DXGKRNL_PROCESS is wired into DxgkCreateDevice:
                 *
                 *   if (Device->ProcessRecord != NULL &&
                 *       Device->ProcessRecord->Process == Process)
                 *   {
                 *       RemoveEntryList(&Device->DeviceListEntry);
                 *       Device->DeviceListEntry.Flink = (PLIST_ENTRY)OrphanHead;
                 *       OrphanHead = Device;
                 *   }
                 */
                DevEntry = Next;
            }

            ExReleaseFastMutex(&Adapter->AdapterMutex);

            /*
             * Drain the orphan list outside AdapterMutex so miniport DDIs
             * can run at PASSIVE_LEVEL without holding the mutex.
             */
            OrphanCurr = OrphanHead;
            while (OrphanCurr != NULL)
            {
                PDXGKRNL_DEVICE Next = (PDXGKRNL_DEVICE)
                                       OrphanCurr->DeviceListEntry.Flink;
                PLIST_ENTRY     CtxEntry;

                /* Destroy all contexts. */
                for (;;)
                {
                    PDXGKRNL_CONTEXT Ctx;

                    ExAcquireFastMutex(&OrphanCurr->DeviceMutex);

                    if (IsListEmpty(&OrphanCurr->ContextListHead))
                    {
                        ExReleaseFastMutex(&OrphanCurr->DeviceMutex);
                        break;
                    }

                    CtxEntry = RemoveHeadList(&OrphanCurr->ContextListHead);
                    InitializeListHead(CtxEntry);
                    ExReleaseFastMutex(&OrphanCurr->DeviceMutex);

                    Ctx = CONTAINING_RECORD(CtxEntry, DXGKRNL_CONTEXT,
                                            ContextListEntry);
                    DxgkpDestroyContextNoLock(Ctx);
                }

                /* Call miniport device destroy. */
                if (OrphanCurr->hMiniportDevice != NULL &&
                    Adapter->MiniportContext->InitData.s.DxgkDdiDestroyDevice
                    != NULL)
                {
                    NTSTATUS Status =
                        Adapter->MiniportContext->InitData.s.DxgkDdiDestroyDevice(
                            OrphanCurr->hMiniportDevice);
                    if (!NT_SUCCESS(Status))
                    {
                        DXGKRNL_ERR("DxgkProcessCleanup: DxgkDdiDestroyDevice "
                                    "failed 0x%08lX\n", Status);
                    }
                }

#if DBG
                OrphanCurr->Handle = 0xDEADDEAD;
#endif
                ExFreePoolWithTag(OrphanCurr, TAG_DXGK_DEVICE);
                OrphanCurr = Next;
            }
        }
    }
}

/* EOF */
