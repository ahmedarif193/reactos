/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minifilter enlistment in kernel transactions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

#define FLTP_TRANSACTION_TAG 'xTlF'
#define FLTP_KTM_NOTIFICATIONS (TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE | \
                                TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK)

typedef struct _FLTP_TRANSACTION
{
    LIST_ENTRY Link;
    PKTRANSACTION Transaction;
    PKENLISTMENT Enlistment;
    HANDLE EnlistmentHandle;
    LIST_ENTRY Enlistments;
    ULONG Notification;
    volatile LONG Pending;
    volatile LONG ReferenceCount;
    WORK_QUEUE_ITEM FinalizeItem;
} FLTP_TRANSACTION, *PFLTP_TRANSACTION;

typedef struct _FLTP_ENLISTMENT
{
    LIST_ENTRY Link;
    PFLT_INSTANCE Instance;
    PFLT_CONTEXT Context;
    ULONG NotificationMask;
    ULONG PendingNotification;
} FLTP_ENLISTMENT, *PFLTP_ENLISTMENT;

typedef struct _FLTP_DELIVERY
{
    PFLT_INSTANCE Instance;
    PFLT_CONTEXT Context;
} FLTP_DELIVERY, *PFLTP_DELIVERY;

static
VOID
FltpNotifyTransaction(
    _In_ PFLTP_TRANSACTION Record,
    _In_ ULONG Notification);

static
PFLTP_TRANSACTION
FltpFindTransactionLocked(
    _In_ PKTRANSACTION Transaction)
{
    PFLTP_TRANSACTION Record;
    PLIST_ENTRY Link;

    for (Link = FltGlobals.TransactionList.Flink;
         Link != &FltGlobals.TransactionList;
         Link = Link->Flink)
    {
        Record = CONTAINING_RECORD(Link, FLTP_TRANSACTION, Link);
        if (Record->Transaction == Transaction)
        {
            return Record;
        }
    }
    return NULL;
}

static
PFLTP_ENLISTMENT
FltpFindEnlistmentLocked(
    _In_ PFLTP_TRANSACTION Record,
    _In_ PFLT_INSTANCE Instance)
{
    PFLTP_ENLISTMENT Enlistment;
    PLIST_ENTRY Link;

    for (Link = Record->Enlistments.Flink; Link != &Record->Enlistments; Link = Link->Flink)
    {
        Enlistment = CONTAINING_RECORD(Link, FLTP_ENLISTMENT, Link);
        if (Enlistment->Instance == Instance)
        {
            return Enlistment;
        }
    }
    return NULL;
}

static
VOID
FltpFreeEnlistment(
    _In_ PFLTP_ENLISTMENT Enlistment)
{
    FltReleaseContext(Enlistment->Context);
    FltpDereferencePointer(&Enlistment->Instance->Base);
    ExFreePoolWithTag(Enlistment, FLTP_TRANSACTION_TAG);
}

static
VOID
FltpDereferenceTransaction(
    _In_ PFLTP_TRANSACTION Record)
{
    if (InterlockedDecrement(&Record->ReferenceCount) == 0)
    {
        ObDereferenceObject(Record->Enlistment);
        ObCloseHandle(Record->EnlistmentHandle, KernelMode);
        ObDereferenceObject(Record->Transaction);
        ExFreePoolWithTag(Record, FLTP_TRANSACTION_TAG);
    }
}

static
VOID
FltpTeardownTransaction(
    _In_ PFLTP_TRANSACTION Record)
{
    PFLTP_ENLISTMENT Enlistment;
    LIST_ENTRY Enlistments;
    PLIST_ENTRY Link;

    InitializeListHead(&Enlistments);

    ExAcquireFastMutex(&FltGlobals.TransactionLock);
    if (!IsListEmpty(&Record->Link))
    {
        RemoveEntryList(&Record->Link);
        InitializeListHead(&Record->Link);
    }
    while (!IsListEmpty(&Record->Enlistments))
    {
        Link = RemoveHeadList(&Record->Enlistments);
        InsertTailList(&Enlistments, Link);
    }
    ExReleaseFastMutex(&FltGlobals.TransactionLock);

    while (!IsListEmpty(&Enlistments))
    {
        Link = RemoveHeadList(&Enlistments);
        Enlistment = CONTAINING_RECORD(Link, FLTP_ENLISTMENT, Link);
        FltpFreeEnlistment(Enlistment);
    }

    FltpDeleteTransactionContexts(Record->Transaction);
    FltpDereferenceTransaction(Record);
}

static
VOID
NTAPI
FltpFinalizeWorker(
    _In_ PVOID Parameter)
{
    PFLTP_TRANSACTION Record = Parameter;

    KeWaitForSingleObject(Record->Transaction, Executive, KernelMode, FALSE, NULL);
    FltpNotifyTransaction(Record, TRANSACTION_NOTIFY_COMMIT_FINALIZE);
    FltpDereferenceTransaction(Record);
}

static
NTSTATUS
FltpNotificationDone(
    _In_ PFLTP_TRANSACTION Record,
    _In_ ULONG Notification)
{
    PFLTP_ENLISTMENT Enlistment;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Finalize = FALSE;
    PLIST_ENTRY Link;

    switch (Notification)
    {
        case TRANSACTION_NOTIFY_PREPREPARE:
            Status = TmPrePrepareComplete(Record->Enlistment, NULL);
            break;

        case TRANSACTION_NOTIFY_PREPARE:
            Status = TmPrepareComplete(Record->Enlistment, NULL);
            break;

        case TRANSACTION_NOTIFY_COMMIT:
            Status = TmCommitComplete(Record->Enlistment, NULL);

            ExAcquireFastMutex(&FltGlobals.TransactionLock);
            for (Link = Record->Enlistments.Flink; Link != &Record->Enlistments; Link = Link->Flink)
            {
                Enlistment = CONTAINING_RECORD(Link, FLTP_ENLISTMENT, Link);
                if (Enlistment->NotificationMask & TRANSACTION_NOTIFY_COMMIT_FINALIZE)
                {
                    Finalize = TRUE;
                    break;
                }
            }
            ExReleaseFastMutex(&FltGlobals.TransactionLock);

            if (Finalize)
            {
                InterlockedIncrement(&Record->ReferenceCount);
                ExInitializeWorkItem(&Record->FinalizeItem, FltpFinalizeWorker, Record);
                ExQueueWorkItem(&Record->FinalizeItem, DelayedWorkQueue);
            }
            else
            {
                FltpTeardownTransaction(Record);
            }
            break;

        case TRANSACTION_NOTIFY_ROLLBACK:
            Status = TmRollbackComplete(Record->Enlistment, NULL);
            FltpTeardownTransaction(Record);
            break;

        case TRANSACTION_NOTIFY_COMMIT_FINALIZE:
            FltpTeardownTransaction(Record);
            break;

        default:
            break;
    }

    return Status == STATUS_PENDING ? STATUS_PENDING : STATUS_SUCCESS;
}

static
NTSTATUS
FltpCompleteNotification(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _In_ ULONG Notification)
{
    PFLTP_ENLISTMENT Enlistment;
    PFLTP_TRANSACTION Record;
    NTSTATUS Status = STATUS_SUCCESS;

    ExAcquireFastMutex(&FltGlobals.TransactionLock);
    Record = FltpFindTransactionLocked(Transaction);
    Enlistment = (Record != NULL) ? FltpFindEnlistmentLocked(Record, Instance) : NULL;
    if (Enlistment == NULL)
    {
        Status = STATUS_NOT_FOUND;
    }
    else if (Enlistment->PendingNotification != Notification)
    {
        Status = STATUS_TRANSACTION_NOT_REQUESTED;
    }
    else
    {
        Enlistment->PendingNotification = 0;
        InterlockedIncrement(&Record->ReferenceCount);
    }
    ExReleaseFastMutex(&FltGlobals.TransactionLock);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (InterlockedDecrement(&Record->Pending) == 0)
    {
        Status = FltpNotificationDone(Record, Notification);
    }
    FltpDereferenceTransaction(Record);
    return Status;
}

static
VOID
FltpNotifyTransaction(
    _In_ PFLTP_TRANSACTION Record,
    _In_ ULONG Notification)
{
    FLTP_RELATED_OBJECTS FltObjects;
    PFLTP_ENLISTMENT Enlistment;
    PFLTP_DELIVERY Deliveries = NULL;
    ULONG Count = 0, Index;
    PLIST_ENTRY Link;
    NTSTATUS Status;

    InterlockedIncrement(&Record->ReferenceCount);

    ExAcquireFastMutex(&FltGlobals.TransactionLock);
    for (Link = Record->Enlistments.Flink; Link != &Record->Enlistments; Link = Link->Flink)
    {
        Count++;
    }
    if (Count != 0)
    {
        Deliveries = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(*Deliveries), FLTP_TRANSACTION_TAG);
    }

    Count = 0;
    Record->Notification = Notification;
    Record->Pending = 1;
    if (Deliveries != NULL)
    {
        for (Link = Record->Enlistments.Flink; Link != &Record->Enlistments; Link = Link->Flink)
        {
            Enlistment = CONTAINING_RECORD(Link, FLTP_ENLISTMENT, Link);
            if (!(Enlistment->NotificationMask & Notification) ||
                (Enlistment->Instance->Flags & FLTP_INSTANCE_DETACHING) ||
                !ExAcquireRundownProtection(&Enlistment->Instance->Base.RundownRef))
            {
                continue;
            }

            Enlistment->PendingNotification = Notification;
            FltReferenceContext(Enlistment->Context);
            Deliveries[Count].Instance = Enlistment->Instance;
            Deliveries[Count].Context = Enlistment->Context;
            Count++;
            InterlockedIncrement(&Record->Pending);
        }
    }
    ExReleaseFastMutex(&FltGlobals.TransactionLock);

    for (Index = 0; Index < Count; Index++)
    {
        RtlZeroMemory(&FltObjects, sizeof(FltObjects));
        FltObjects.Size = sizeof(FltObjects);
        FltObjects.Filter = Deliveries[Index].Instance->Filter;
        FltObjects.Volume = Deliveries[Index].Instance->Volume;
        FltObjects.Instance = Deliveries[Index].Instance;
        FltObjects.Transaction = Record->Transaction;

        Status = FltObjects.Filter->TransactionNotification((PCFLT_RELATED_OBJECTS)&FltObjects,
                                                            Deliveries[Index].Context,
                                                            Notification);
        if (Status != STATUS_PENDING)
        {
            (VOID)FltpCompleteNotification(Deliveries[Index].Instance, Record->Transaction, Notification);
        }

        FltReleaseContext(Deliveries[Index].Context);
        ExReleaseRundownProtection(&Deliveries[Index].Instance->Base.RundownRef);
    }

    if (Deliveries != NULL)
    {
        ExFreePoolWithTag(Deliveries, FLTP_TRANSACTION_TAG);
    }

    if (InterlockedDecrement(&Record->Pending) == 0)
    {
        (VOID)FltpNotificationDone(Record, Notification);
    }
    FltpDereferenceTransaction(Record);
}

static
NTSTATUS
NTAPI
FltpTransactionNotification(
    _In_ PKENLISTMENT EnlistmentObject,
    _In_ PVOID RMContext,
    _In_ PVOID TransactionContext,
    _In_ ULONG TransactionNotification,
    _Inout_ PLARGE_INTEGER TmVirtualClock,
    _In_ ULONG ArgumentLength,
    _In_ PVOID Argument)
{
    UNREFERENCED_PARAMETER(EnlistmentObject);
    UNREFERENCED_PARAMETER(RMContext);
    UNREFERENCED_PARAMETER(TmVirtualClock);
    UNREFERENCED_PARAMETER(ArgumentLength);
    UNREFERENCED_PARAMETER(Argument);

    FltpNotifyTransaction(TransactionContext, TransactionNotification);
    return STATUS_PENDING;
}

static
NTSTATUS
FltpCreateResourceManager(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PKRESOURCEMANAGER ResourceManager;
    HANDLE TmHandle, RmHandle;
    NTSTATUS Status;
    GUID Guid;

    if (FltGlobals.ResourceManager != NULL)
    {
        return STATUS_SUCCESS;
    }

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateTransactionManager(&TmHandle,
                                        TRANSACTIONMANAGER_ALL_ACCESS,
                                        &ObjectAttributes,
                                        NULL,
                                        TRANSACTION_MANAGER_VOLATILE,
                                        0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ExUuidCreate(&Guid);
    if (NT_SUCCESS(Status))
    {
        Status = ZwCreateResourceManager(&RmHandle,
                                         RESOURCEMANAGER_ALL_ACCESS,
                                         TmHandle,
                                         &Guid,
                                         &ObjectAttributes,
                                         RESOURCE_MANAGER_VOLATILE,
                                         NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        ZwClose(TmHandle);
        return Status;
    }

    Status = ObReferenceObjectByHandle(RmHandle,
                                       0,
                                       *TmResourceManagerObjectType,
                                       KernelMode,
                                       (PVOID *)&ResourceManager,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        Status = TmEnableCallbacks(ResourceManager, FltpTransactionNotification, NULL);
        if (NT_SUCCESS(Status) &&
            InterlockedCompareExchangePointer((PVOID *)&FltGlobals.ResourceManager, ResourceManager, NULL) == NULL)
        {
            FltGlobals.TransactionManagerHandle = TmHandle;
            FltGlobals.ResourceManagerHandle = RmHandle;
            return STATUS_SUCCESS;
        }
        ObDereferenceObject(ResourceManager);
    }

    ZwClose(RmHandle);
    ZwClose(TmHandle);
    return Status;
}

NTSTATUS
FltpTrackTransaction(
    _In_ PKTRANSACTION Transaction)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PFLTP_TRANSACTION Record, Existing;
    NTSTATUS Status;

    ExAcquireFastMutex(&FltGlobals.TransactionLock);
    Existing = FltpFindTransactionLocked(Transaction);
    ExReleaseFastMutex(&FltGlobals.TransactionLock);
    if (Existing != NULL)
    {
        return STATUS_SUCCESS;
    }

    Status = FltpCreateResourceManager();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Record = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Record), FLTP_TRANSACTION_TAG);
    if (Record == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Record, sizeof(*Record));
    InitializeListHead(&Record->Enlistments);
    Record->Transaction = Transaction;
    Record->ReferenceCount = 1;
    ObReferenceObject(Transaction);

    KeAcquireGuardedMutex(&FltGlobals.TransactionCreateLock);
    ExAcquireFastMutex(&FltGlobals.TransactionLock);
    Existing = FltpFindTransactionLocked(Transaction);
    ExReleaseFastMutex(&FltGlobals.TransactionLock);

    if (Existing == NULL)
    {
        InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
        Status = TmCreateEnlistment(&Record->EnlistmentHandle,
                                    KernelMode,
                                    ENLISTMENT_ALL_ACCESS,
                                    &ObjectAttributes,
                                    FltGlobals.ResourceManager,
                                    Transaction,
                                    0,
                                    FLTP_KTM_NOTIFICATIONS,
                                    Record);
        if (NT_SUCCESS(Status))
        {
            Status = ObReferenceObjectByHandle(Record->EnlistmentHandle,
                                               0,
                                               *TmEnlistmentObjectType,
                                               KernelMode,
                                               (PVOID *)&Record->Enlistment,
                                               NULL);
            ASSERT(NT_SUCCESS(Status));

            ExAcquireFastMutex(&FltGlobals.TransactionLock);
            InsertTailList(&FltGlobals.TransactionList, &Record->Link);
            ExReleaseFastMutex(&FltGlobals.TransactionLock);
            Record = NULL;
        }
    }
    KeReleaseGuardedMutex(&FltGlobals.TransactionCreateLock);

    if (Record != NULL)
    {
        ObDereferenceObject(Transaction);
        ExFreePoolWithTag(Record, FLTP_TRANSACTION_TAG);
    }
    return Status;
}

VOID
FltpDeleteInstanceEnlistments(
    _In_ PFLT_INSTANCE Instance)
{
    PFLTP_ENLISTMENT Enlistment;
    PFLTP_TRANSACTION Record;
    PLIST_ENTRY Link;
    ULONG Pending;

    for (;;)
    {
        Enlistment = NULL;
        Record = NULL;
        Pending = 0;

        ExAcquireFastMutex(&FltGlobals.TransactionLock);
        for (Link = FltGlobals.TransactionList.Flink;
             Link != &FltGlobals.TransactionList && Enlistment == NULL;
             Link = Link->Flink)
        {
            Record = CONTAINING_RECORD(Link, FLTP_TRANSACTION, Link);
            Enlistment = FltpFindEnlistmentLocked(Record, Instance);
        }
        if (Enlistment != NULL)
        {
            RemoveEntryList(&Enlistment->Link);
            Pending = Enlistment->PendingNotification;
            InterlockedIncrement(&Record->ReferenceCount);
        }
        ExReleaseFastMutex(&FltGlobals.TransactionLock);

        if (Enlistment == NULL)
        {
            break;
        }

        if (Pending != 0 && InterlockedDecrement(&Record->Pending) == 0)
        {
            (VOID)FltpNotificationDone(Record, Pending);
        }
        FltpFreeEnlistment(Enlistment);
        FltpDereferenceTransaction(Record);
    }
}

NTSTATUS
FLTAPI
FltEnlistInTransaction(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _In_ PFLT_CONTEXT TransactionContext,
    _In_ NOTIFICATION_MASK NotificationMask)
{
    PFLTP_ENLISTMENT Enlistment;
    PFLTP_TRANSACTION Record;
    NTSTATUS Status;

    if (Instance->Filter->TransactionNotification == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (NotificationMask & ~FLT_MAX_TRANSACTION_NOTIFICATIONS)
    {
        return STATUS_INVALID_PARAMETER_4;
    }
    if (Instance->Flags & FLTP_INSTANCE_DETACHING)
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    Status = FltpTrackTransaction(Transaction);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Enlistment = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Enlistment), FLTP_TRANSACTION_TAG);
    if (Enlistment == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Enlistment, sizeof(*Enlistment));
    Enlistment->Instance = Instance;
    Enlistment->Context = TransactionContext;
    Enlistment->NotificationMask = NotificationMask;

    ExAcquireFastMutex(&FltGlobals.TransactionLock);
    Record = FltpFindTransactionLocked(Transaction);
    if (Record == NULL)
    {
        Status = STATUS_TRANSACTION_NOT_ACTIVE;
    }
    else if (FltpFindEnlistmentLocked(Record, Instance) != NULL)
    {
        Status = STATUS_FLT_ALREADY_ENLISTED;
    }
    else
    {
        FltpReferencePointer(&Instance->Base);
        FltReferenceContext(TransactionContext);
        InsertTailList(&Record->Enlistments, &Enlistment->Link);
        Enlistment = NULL;
    }
    ExReleaseFastMutex(&FltGlobals.TransactionLock);

    if (Enlistment != NULL)
    {
        ExFreePoolWithTag(Enlistment, FLTP_TRANSACTION_TAG);
    }
    return Status;
}

NTSTATUS
FLTAPI
FltRollbackEnlistment(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _In_opt_ PFLT_CONTEXT TransactionContext)
{
    PKENLISTMENT Enlistment = NULL;
    PFLTP_TRANSACTION Record;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(TransactionContext);

    ExAcquireFastMutex(&FltGlobals.TransactionLock);
    Record = FltpFindTransactionLocked(Transaction);
    if (Record != NULL && FltpFindEnlistmentLocked(Record, Instance) != NULL)
    {
        Enlistment = Record->Enlistment;
        ObReferenceObject(Enlistment);
    }
    ExReleaseFastMutex(&FltGlobals.TransactionLock);

    if (Enlistment == NULL)
    {
        return STATUS_NOT_FOUND;
    }

    Status = TmRollbackEnlistment(Enlistment, NULL);
    ObDereferenceObject(Enlistment);
    return Status;
}

NTSTATUS
FLTAPI
FltPrePrepareComplete(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _In_opt_ PFLT_CONTEXT TransactionContext)
{
    UNREFERENCED_PARAMETER(TransactionContext);

    return FltpCompleteNotification(Instance, Transaction, TRANSACTION_NOTIFY_PREPREPARE);
}

NTSTATUS
FLTAPI
FltPrepareComplete(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _In_opt_ PFLT_CONTEXT TransactionContext)
{
    UNREFERENCED_PARAMETER(TransactionContext);

    return FltpCompleteNotification(Instance, Transaction, TRANSACTION_NOTIFY_PREPARE);
}

NTSTATUS
FLTAPI
FltCommitComplete(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _In_opt_ PFLT_CONTEXT TransactionContext)
{
    UNREFERENCED_PARAMETER(TransactionContext);

    return FltpCompleteNotification(Instance, Transaction, TRANSACTION_NOTIFY_COMMIT);
}

NTSTATUS
FLTAPI
FltCommitFinalizeComplete(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _In_opt_ PFLT_CONTEXT TransactionContext)
{
    UNREFERENCED_PARAMETER(TransactionContext);

    return FltpCompleteNotification(Instance, Transaction, TRANSACTION_NOTIFY_COMMIT_FINALIZE);
}

NTSTATUS
FLTAPI
FltRollbackComplete(
    _In_ PFLT_INSTANCE Instance,
    _In_ PKTRANSACTION Transaction,
    _In_opt_ PFLT_CONTEXT TransactionContext)
{
    UNREFERENCED_PARAMETER(TransactionContext);

    return FltpCompleteNotification(Instance, Transaction, TRANSACTION_NOTIFY_ROLLBACK);
}
