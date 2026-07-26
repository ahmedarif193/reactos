/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Trim notification registration
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * A runtime registers here to be told when VidMm wants memory back.  The
 * registration is per-process bookkeeping: the callback is a user-mode address,
 * so it is only ever meaningful in the process that registered it, and a
 * registration that outlived its process would name code that is no longer
 * mapped.  Process exit therefore drops every registration the process made.
 *
 * Delivery is not wired yet -- see DxgkTrimNotificationPending below.  What is
 * implemented is the registration contract: a real opaque handle, unregister by
 * handle, unregister-all by callback (the DLL-unload path), and cleanup.
 */

#include "dxgkrnl_private.h"

#define TAG_DXGK_TRIMNOTIFY 'NTxD'

typedef struct _DXGKRNL_TRIM_REGISTRATION
{
    LIST_ENTRY Link;
    PEPROCESS OwnerProcess;
    LUID AdapterLuid;
    D3DKMT_HANDLE hDevice;
    PFND3DKMT_TRIMNOTIFICATIONCALLBACK Callback;
    PVOID Context;
} DXGKRNL_TRIM_REGISTRATION, *PDXGKRNL_TRIM_REGISTRATION;

static LIST_ENTRY DxgkTrimRegistrationList;
static FAST_MUTEX DxgkTrimRegistrationLock;
static BOOLEAN DxgkTrimRegistrationReady;

NTSTATUS
DxgkTrimNotificationInitialize(VOID)
{
    InitializeListHead(&DxgkTrimRegistrationList);
    ExInitializeFastMutex(&DxgkTrimRegistrationLock);
    DxgkTrimRegistrationReady = TRUE;
    return STATUS_SUCCESS;
}

VOID
DxgkTrimNotificationUninitialize(VOID)
{
    PLIST_ENTRY Entry;

    if (!DxgkTrimRegistrationReady)
        return;
    DxgkTrimRegistrationReady = FALSE;
    ExAcquireFastMutex(&DxgkTrimRegistrationLock);
    while (!IsListEmpty(&DxgkTrimRegistrationList))
    {
        Entry = RemoveHeadList(&DxgkTrimRegistrationList);
        ExFreePoolWithTag(CONTAINING_RECORD(Entry, DXGKRNL_TRIM_REGISTRATION, Link), TAG_DXGK_TRIMNOTIFY);
    }
    ExReleaseFastMutex(&DxgkTrimRegistrationLock);
}

NTSTATUS
DxgkRegisterTrimNotification(
    _Inout_ D3DKMT_REGISTERTRIMNOTIFICATION *pData)
{
    PDXGKRNL_TRIM_REGISTRATION Registration;
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->Callback == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!DxgkTrimRegistrationReady)
        return STATUS_DEVICE_NOT_READY;

    /* The device must be one this process owns: the notification names the
     * device whose memory is being reclaimed. */
    Status = DxgkReferenceDeviceByHandle(pData->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Registration = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Registration), TAG_DXGK_TRIMNOTIFY);
    if (Registration == NULL)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Registration, sizeof(*Registration));
    Registration->OwnerProcess = PsGetCurrentProcess();
    Registration->AdapterLuid = pData->AdapterLuid;
    Registration->hDevice = pData->hDevice;
    Registration->Callback = pData->Callback;
    Registration->Context = pData->Context;

    ExAcquireFastMutex(&DxgkTrimRegistrationLock);
    InsertTailList(&DxgkTrimRegistrationList, &Registration->Link);
    ExReleaseFastMutex(&DxgkTrimRegistrationLock);

    DxgkDereferenceDevice(Device);

    /* The registration record itself is the handle; it is only ever handed
     * back to us, never dereferenced by the caller. */
    pData->Handle = Registration;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkUnregisterTrimNotification(
    _In_ CONST D3DKMT_UNREGISTERTRIMNOTIFICATION *pData)
{
    PEPROCESS Process = PsGetCurrentProcess();
    LIST_ENTRY Removed;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;
    BOOLEAN Matched = FALSE;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->Handle == NULL && pData->Callback == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!DxgkTrimRegistrationReady)
        return STATUS_DEVICE_NOT_READY;

    InitializeListHead(&Removed);
    ExAcquireFastMutex(&DxgkTrimRegistrationLock);
    for (Entry = DxgkTrimRegistrationList.Flink; Entry != &DxgkTrimRegistrationList; Entry = Next)
    {
        PDXGKRNL_TRIM_REGISTRATION Registration = CONTAINING_RECORD(Entry, DXGKRNL_TRIM_REGISTRATION, Link);

        Next = Entry->Flink;
        /* Only this process's registrations are reachable: a handle is an
         * address in our list, and honouring another process's would let one
         * process silence another's notifications. */
        if (Registration->OwnerProcess != Process)
            continue;
        if (pData->Handle != NULL)
        {
            if ((PVOID)Registration != pData->Handle)
                continue;
        }
        else if (Registration->Callback != pData->Callback)
        {
            /* Handle NULL means "every registration of this callback", the
             * path a DLL takes when unloading without tracking its handles. */
            continue;
        }
        RemoveEntryList(&Registration->Link);
        InsertTailList(&Removed, &Registration->Link);
        Matched = TRUE;
    }
    ExReleaseFastMutex(&DxgkTrimRegistrationLock);

    while (!IsListEmpty(&Removed))
    {
        Entry = RemoveHeadList(&Removed);
        ExFreePoolWithTag(CONTAINING_RECORD(Entry, DXGKRNL_TRIM_REGISTRATION, Link), TAG_DXGK_TRIMNOTIFY);
    }
    return Matched ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

VOID
DxgkTrimNotificationProcessCleanup(
    _In_ PEPROCESS Process)
{
    LIST_ENTRY Removed;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;

    if (!DxgkTrimRegistrationReady)
        return;

    InitializeListHead(&Removed);
    ExAcquireFastMutex(&DxgkTrimRegistrationLock);
    for (Entry = DxgkTrimRegistrationList.Flink; Entry != &DxgkTrimRegistrationList; Entry = Next)
    {
        PDXGKRNL_TRIM_REGISTRATION Registration = CONTAINING_RECORD(Entry, DXGKRNL_TRIM_REGISTRATION, Link);

        Next = Entry->Flink;
        if (Registration->OwnerProcess != Process)
            continue;
        RemoveEntryList(&Registration->Link);
        InsertTailList(&Removed, &Registration->Link);
    }
    ExReleaseFastMutex(&DxgkTrimRegistrationLock);

    while (!IsListEmpty(&Removed))
    {
        Entry = RemoveHeadList(&Removed);
        ExFreePoolWithTag(CONTAINING_RECORD(Entry, DXGKRNL_TRIM_REGISTRATION, Link), TAG_DXGK_TRIMNOTIFY);
    }
}

/*
 * TRUE if any live registration would want to hear about a trim on this device.
 *
 * The callback is a user-mode address, so VidMm cannot simply call it: delivery
 * needs an upcall into the registering process, which this stack does not have
 * yet.  Until it does, this reports whether anyone is listening so the budget
 * path can be written against a real answer instead of a guess, and so the
 * registration bookkeeping is exercised rather than dead.
 */
BOOLEAN
DxgkTrimNotificationPending(
    _In_ D3DKMT_HANDLE hDevice)
{
    PLIST_ENTRY Entry;
    BOOLEAN Found = FALSE;

    if (!DxgkTrimRegistrationReady)
        return FALSE;
    ExAcquireFastMutex(&DxgkTrimRegistrationLock);
    for (Entry = DxgkTrimRegistrationList.Flink; Entry != &DxgkTrimRegistrationList; Entry = Entry->Flink)
    {
        PDXGKRNL_TRIM_REGISTRATION Registration = CONTAINING_RECORD(Entry, DXGKRNL_TRIM_REGISTRATION, Link);

        if (Registration->hDevice == hDevice)
        {
            Found = TRUE;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkTrimRegistrationLock);
    return Found;
}

/* EOF */
