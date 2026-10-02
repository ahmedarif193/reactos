/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel Transaction Manager: resource manager objects and notifications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <internal/tm.h>
#define NDEBUG
#include <debug.h>

static
VOID
TmpRecordEnlistmentHistory(
    _Inout_ PKENLISTMENT Enlistment,
    _In_ ULONG Notification)
{
    ULONG Index = Enlistment->NextHistory % RTL_NUMBER_OF(Enlistment->History);

    Enlistment->History[Index].Notification = Notification;
    Enlistment->History[Index].NewState = Enlistment->State;
    Enlistment->NextHistory++;
}

static
BOOLEAN
TmpQueueNotification(
    _Inout_ PKRESOURCEMANAGER ResourceManager,
    _In_opt_ PVOID Key,
    _In_ ULONG Notification,
    _In_ LARGE_INTEGER Clock)
{
    PKTMNOTIFICATION_PACKET Packet;

    Packet = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Packet), TAG_TM);
    if (Packet == NULL)
    {
        return FALSE;
    }

    Packet->Key = Key;
    Packet->Notification = Notification;
    Packet->VirtualClock = Clock;
    KeInsertQueue(&ResourceManager->NotificationQueue, &Packet->ListEntry);
    KeSetEvent(&ResourceManager->NotificationAvailable, IO_NO_INCREMENT, FALSE);
    return TRUE;
}

BOOLEAN
TmpDeliverNotification(
    _In_ PKENLISTMENT Enlistment,
    _In_ ULONG Notification)
{
    PKRESOURCEMANAGER ResourceManager = Enlistment->ResourceManager;
    PTM_RM_NOTIFICATION Routine = NULL;
    BOOLEAN Pending = FALSE;
    LARGE_INTEGER Clock;
    NTSTATUS Status;
    PVOID Key = NULL;

    Clock = TmpNextVirtualClock(ResourceManager->Tm);

    TmpAcquireMutex(&ResourceManager->NotificationMutex);
    TmpRecordEnlistmentHistory(Enlistment, Notification);
    if (ResourceManager->State != KResourceManagerOnline)
    {
        Status = STATUS_TRANSACTIONMANAGER_NOT_ONLINE;
    }
    else if (ResourceManager->NotificationRoutine != NULL)
    {
        Routine = ResourceManager->NotificationRoutine;
        Key = ResourceManager->Key;
        Status = STATUS_SUCCESS;
    }
    else if (TmpQueueNotification(ResourceManager, Enlistment->Key, Notification, Clock))
    {
        Status = STATUS_SUCCESS;
        Pending = TRUE;
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    TmpReleaseMutex(&ResourceManager->NotificationMutex);

    if (Routine != NULL)
    {
        Status = Routine(Enlistment, Key, Enlistment->Key, Notification, &Clock, 0, NULL);
        TmpAdvanceVirtualClock(ResourceManager->Tm, &Clock);
        Pending = (Status == STATUS_PENDING);
    }

    if (!NT_SUCCESS(Status))
    {
        TmpAbandonEnlistment(Enlistment, Notification);
    }
    return Pending;
}

NTSTATUS
NTAPI
TmEnableCallbacks(
    _In_ PKRESOURCEMANAGER ResourceManager,
    _In_ PTM_RM_NOTIFICATION CallbackRoutine,
    _In_opt_ PVOID RMKey)
{
    if (CallbackRoutine == NULL)
    {
        return STATUS_UNSUCCESSFUL;
    }

    TmpAcquireMutex(&ResourceManager->NotificationMutex);
    ResourceManager->Key = RMKey;
    ResourceManager->NotificationRoutine = CallbackRoutine;
    TmpReleaseMutex(&ResourceManager->NotificationMutex);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
TmRecoverResourceManager(
    _In_ PKRESOURCEMANAGER ResourceManager)
{
    NTSTATUS Status = STATUS_SUCCESS;
    LARGE_INTEGER Clock;

    if (ResourceManager->Tm->State != KKtmOnline)
    {
        return STATUS_TRANSACTIONMANAGER_NOT_ONLINE;
    }

    Clock = TmpNextVirtualClock(ResourceManager->Tm);
    TmpAcquireMutex(&ResourceManager->NotificationMutex);
    if (ResourceManager->State == KResourceManagerOnline &&
        ResourceManager->NotificationRoutine == NULL)
    {
        if (TmpQueueNotification(ResourceManager, NULL, TRANSACTION_NOTIFY_LAST_RECOVER, Clock))
        {
            Status = STATUS_PENDING;
        }
        else
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    TmpReleaseMutex(&ResourceManager->NotificationMutex);

    return Status;
}

NTSTATUS
NTAPI
TmPropagationComplete(
    _In_ PKRESOURCEMANAGER ResourceManager,
    _In_ ULONG RequestCookie,
    _In_ ULONG BufferLength,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(ResourceManager);
    UNREFERENCED_PARAMETER(RequestCookie);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(Buffer);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
TmPropagationFailed(
    _In_ PKRESOURCEMANAGER ResourceManager,
    _In_ ULONG RequestCookie,
    _In_ NTSTATUS Status)
{
    UNREFERENCED_PARAMETER(ResourceManager);
    UNREFERENCED_PARAMETER(RequestCookie);
    UNREFERENCED_PARAMETER(Status);

    return STATUS_NOT_SUPPORTED;
}

VOID
NTAPI
TmpCloseResourceManager(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    PKRESOURCEMANAGER ResourceManager = Object;
    PKENLISTMENT Enlistment;
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);

    if (SystemHandleCount != 1 || ResourceManager->cookie != KRESOURCEMANAGER_COOKIE)
    {
        return;
    }

    TmpAcquireMutex(&ResourceManager->NotificationMutex);
    ResourceManager->State = KResourceManagerOffline;
    ResourceManager->NotificationRoutine = NULL;
    ResourceManager->Key = NULL;
    TmpReleaseMutex(&ResourceManager->NotificationMutex);

    for (;;)
    {
        Enlistment = NULL;

        TmpAcquireMutex(&ResourceManager->Mutex);
        for (Entry = ResourceManager->EnlistmentHead.Flink;
             Entry != &ResourceManager->EnlistmentHead;
             Entry = Entry->Flink)
        {
            Enlistment = CONTAINING_RECORD(Entry, KENLISTMENT, NextSameRm);
            if (!(Enlistment->Flags & KENLISTMENT_FLAG_OFFLINE))
            {
                InterlockedOr((PLONG)&Enlistment->Flags, KENLISTMENT_FLAG_OFFLINE);
                if (ObReferenceObjectSafe(Enlistment))
                {
                    break;
                }
            }
            Enlistment = NULL;
        }
        TmpReleaseMutex(&ResourceManager->Mutex);

        if (Enlistment == NULL)
        {
            break;
        }

        TmpAbandonEnlistment(Enlistment, 0);
        ObDereferenceObject(Enlistment);
    }
}

VOID
NTAPI
TmpDeleteResourceManager(
    _In_ PVOID Object)
{
    PKRESOURCEMANAGER ResourceManager = Object;
    PLIST_ENTRY First, Entry, Next;

    if (ResourceManager->cookie != KRESOURCEMANAGER_COOKIE)
    {
        return;
    }

    TmpRemoveNamespace(&ResourceManager->Tm->ResourceManagers, ResourceManager);

    First = KeRundownQueue(&ResourceManager->NotificationQueue);
    if (First != NULL)
    {
        Entry = First;
        do
        {
            Next = Entry->Flink;
            ExFreePoolWithTag(CONTAINING_RECORD(Entry, KTMNOTIFICATION_PACKET, ListEntry), TAG_TM);
            Entry = Next;
        } while (Entry != First);
    }

    if (ResourceManager->CompletionBinding.Port != NULL)
    {
        ObDereferenceObject(ResourceManager->CompletionBinding.Port);
    }
    if (ResourceManager->Description.Buffer != NULL)
    {
        ExFreePoolWithTag(ResourceManager->Description.Buffer, TAG_TM);
    }
    ObDereferenceObject(ResourceManager->Tm);
    ResourceManager->cookie = 0;
}

NTSTATUS
NTAPI
NtCreateResourceManager(
    _Out_ PHANDLE ResourceManagerHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ HANDLE TmHandle,
    _In_ LPGUID RmGuid,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ ULONG CreateOptions,
    _In_opt_ PUNICODE_STRING Description)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKRESOURCEMANAGER ResourceManager;
    UNICODE_STRING SafeDescription;
    BOOLEAN Present;
    HANDLE Handle;
    NTSTATUS Status;
    GUID Guid;
    PKTM Tm;
    PAGED_CODE();

    if (CreateOptions & ~RESOURCE_MANAGER_MAXIMUM_OPTION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = TmpCaptureGuid(PreviousMode, RmGuid, &Guid, &Present);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (!Present)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = TmpCaptureDescription(PreviousMode,
                                   Description,
                                   MAX_RESOURCEMANAGER_DESCRIPTION_LENGTH,
                                   &SafeDescription);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ObReferenceObjectByHandle(TmHandle,
                                       TRANSACTIONMANAGER_CREATE_RM,
                                       TmTransactionManagerObjectType,
                                       PreviousMode,
                                       (PVOID *)&Tm,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        goto FreeDescription;
    }

    if (!(CreateOptions & RESOURCE_MANAGER_VOLATILE))
    {
        Status = STATUS_TM_VOLATILE;
        goto DereferenceTm;
    }
    if (Tm->State != KKtmOnline)
    {
        Status = STATUS_TRANSACTIONMANAGER_NOT_ONLINE;
        goto DereferenceTm;
    }

    Status = ObCreateObject(PreviousMode,
                            TmResourceManagerObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(KRESOURCEMANAGER),
                            0,
                            0,
                            (PVOID *)&ResourceManager);
    if (!NT_SUCCESS(Status))
    {
        goto DereferenceTm;
    }

    RtlZeroMemory(ResourceManager, sizeof(*ResourceManager));
    KeInitializeEvent(&ResourceManager->NotificationAvailable, NotificationEvent, FALSE);
    ResourceManager->State = KResourceManagerOnline;
    ResourceManager->Flags = CreateOptions;
    KeInitializeMutex(&ResourceManager->Mutex, 0);
    ResourceManager->RmId = Guid;
    KeInitializeQueue(&ResourceManager->NotificationQueue, 0);
    KeInitializeMutex(&ResourceManager->NotificationMutex, 0);
    InitializeListHead(&ResourceManager->EnlistmentHead);
    InitializeListHead(&ResourceManager->ProtocolListHead);
    InitializeListHead(&ResourceManager->PendingPropReqListHead);
    InitializeListHead(&ResourceManager->CRMListEntry);
    InitializeListHead(&ResourceManager->CompletionBinding.NotificationListHead);
    TmpInitializeNamespace(&ResourceManager->Enlistments,
                           FIELD_OFFSET(KENLISTMENT, NamespaceLink),
                           FIELD_OFFSET(KENLISTMENT, EnlistmentId));
    ResourceManager->Tm = Tm;
    ResourceManager->cookie = KRESOURCEMANAGER_COOKIE;

    Status = TmpInsertNamespace(&Tm->ResourceManagers, ResourceManager);
    if (!NT_SUCCESS(Status))
    {
        ResourceManager->cookie = 0;
        KeRundownQueue(&ResourceManager->NotificationQueue);
        ObDereferenceObject(ResourceManager);
        goto DereferenceTm;
    }

    ResourceManager->Description = SafeDescription;
    RtlInitEmptyUnicodeString(&SafeDescription, NULL, 0);

    Status = ObInsertObject(ResourceManager, NULL, DesiredAccess, 0, NULL, &Handle);
    if (NT_SUCCESS(Status))
    {
        Status = TmpWriteHandle(PreviousMode, ResourceManagerHandle, Handle);
        if (!NT_SUCCESS(Status))
        {
            ObCloseHandle(Handle, PreviousMode);
        }
    }
    return Status;

DereferenceTm:
    ObDereferenceObject(Tm);
FreeDescription:
    if (SafeDescription.Buffer != NULL)
    {
        ExFreePoolWithTag(SafeDescription.Buffer, TAG_TM);
    }
    return Status;
}

NTSTATUS
NTAPI
NtOpenResourceManager(
    _Out_ PHANDLE ResourceManagerHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ HANDLE TmHandle,
    _In_opt_ LPGUID ResourceManagerGuid,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKRESOURCEMANAGER ResourceManager;
    BOOLEAN Present;
    HANDLE Handle;
    NTSTATUS Status;
    GUID Guid;
    PKTM Tm;
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ObjectAttributes);

    Status = TmpCaptureGuid(PreviousMode, ResourceManagerGuid, &Guid, &Present);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (DesiredAccess == 0 || !Present)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = ObReferenceObjectByHandle(TmHandle,
                                       TRANSACTIONMANAGER_QUERY_INFORMATION,
                                       TmTransactionManagerObjectType,
                                       PreviousMode,
                                       (PVOID *)&Tm,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Tm->State != KKtmOnline)
    {
        ObDereferenceObject(Tm);
        return STATUS_TRANSACTIONMANAGER_NOT_ONLINE;
    }

    ResourceManager = TmpLookupNamespace(&Tm->ResourceManagers, &Guid);
    ObDereferenceObject(Tm);
    if (ResourceManager == NULL)
    {
        return STATUS_RESOURCEMANAGER_NOT_FOUND;
    }

    Status = ObOpenObjectByPointer(ResourceManager,
                                   (PreviousMode == KernelMode) ? OBJ_KERNEL_HANDLE : 0,
                                   NULL,
                                   DesiredAccess,
                                   TmResourceManagerObjectType,
                                   PreviousMode,
                                   &Handle);
    ObDereferenceObject(ResourceManager);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmpWriteHandle(PreviousMode, ResourceManagerHandle, Handle);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(Handle, PreviousMode);
    }
    return Status;
}

NTSTATUS
NTAPI
NtRecoverResourceManager(
    _In_ HANDLE ResourceManagerHandle)
{
    PKRESOURCEMANAGER ResourceManager;
    NTSTATUS Status;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ResourceManagerHandle,
                                       RESOURCEMANAGER_RECOVER,
                                       TmResourceManagerObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&ResourceManager,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmRecoverResourceManager(ResourceManager);
    ObDereferenceObject(ResourceManager);
    return Status;
}

NTSTATUS
NTAPI
NtGetNotificationResourceManager(
    _In_ HANDLE ResourceManagerHandle,
    _Out_ PTRANSACTION_NOTIFICATION TransactionNotification,
    _In_ ULONG NotificationLength,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_opt_ PULONG ReturnLength,
    _In_ ULONG Asynchronous,
    _In_opt_ ULONG_PTR AsynchronousContext)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKRESOURCEMANAGER ResourceManager;
    TRANSACTION_NOTIFICATION Notification;
    PKTMNOTIFICATION_PACKET Packet;
    LARGE_INTEGER SafeTimeout;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();

    if (Asynchronous != 0 || AsynchronousContext != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(TransactionNotification, NotificationLength, sizeof(ULONG));
            if (ReturnLength != NULL)
            {
                ProbeForWriteUlong(ReturnLength);
            }
            if (Timeout != NULL)
            {
                ProbeForRead(Timeout, sizeof(LARGE_INTEGER), sizeof(ULONG));
            }
        }
        if (Timeout != NULL)
        {
            SafeTimeout = *Timeout;
            Timeout = &SafeTimeout;
        }
        if (NotificationLength < sizeof(TRANSACTION_NOTIFICATION))
        {
            if (ReturnLength != NULL)
            {
                *ReturnLength = sizeof(TRANSACTION_NOTIFICATION);
            }
            Status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ObReferenceObjectByHandle(ResourceManagerHandle,
                                       RESOURCEMANAGER_GET_NOTIFICATION,
                                       TmResourceManagerObjectType,
                                       PreviousMode,
                                       (PVOID *)&ResourceManager,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Entry = KeRemoveQueue(&ResourceManager->NotificationQueue, PreviousMode, Timeout);
    if ((ULONG_PTR)Entry == (ULONG_PTR)STATUS_TIMEOUT ||
        (ULONG_PTR)Entry == (ULONG_PTR)STATUS_USER_APC ||
        (ULONG_PTR)Entry == (ULONG_PTR)STATUS_ABANDONED)
    {
        Status = (NTSTATUS)(ULONG_PTR)Entry;
        ObDereferenceObject(ResourceManager);
        return Status;
    }

    TmpAcquireMutex(&ResourceManager->NotificationMutex);
    if (KeReadStateQueue(&ResourceManager->NotificationQueue) == 0)
    {
        KeClearEvent(&ResourceManager->NotificationAvailable);
    }
    TmpReleaseMutex(&ResourceManager->NotificationMutex);

    Packet = CONTAINING_RECORD(Entry, KTMNOTIFICATION_PACKET, ListEntry);
    RtlZeroMemory(&Notification, sizeof(Notification));
    Notification.TransactionKey = Packet->Key;
    Notification.TransactionNotification = Packet->Notification;
    Notification.TmVirtualClock = Packet->VirtualClock;
    Notification.ArgumentLength = 0;
    ExFreePoolWithTag(Packet, TAG_TM);

    _SEH2_TRY
    {
        *TransactionNotification = Notification;
        if (ReturnLength != NULL)
        {
            *ReturnLength = sizeof(TRANSACTION_NOTIFICATION);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(ResourceManager);
    return Status;
}

NTSTATUS
NTAPI
NtQueryInformationResourceManager(
    _In_ HANDLE ResourceManagerHandle,
    _In_ RESOURCEMANAGER_INFORMATION_CLASS ResourceManagerInformationClass,
    _Out_writes_bytes_(ResourceManagerInformationLength) PVOID ResourceManagerInformation,
    _In_ ULONG ResourceManagerInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PRESOURCEMANAGER_BASIC_INFORMATION Basic;
    PKRESOURCEMANAGER ResourceManager;
    NTSTATUS Status;
    ULONG Length;
    PAGED_CODE();

    if (ResourceManagerInformationClass != ResourceManagerBasicInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    Status = ObReferenceObjectByHandle(ResourceManagerHandle,
                                       RESOURCEMANAGER_QUERY_INFORMATION,
                                       TmResourceManagerObjectType,
                                       PreviousMode,
                                       (PVOID *)&ResourceManager,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Length = FIELD_OFFSET(RESOURCEMANAGER_BASIC_INFORMATION, Description) +
             ResourceManager->Description.Length;
    Basic = ExAllocatePoolWithTag(PagedPool, Length, TAG_TM);
    if (Basic == NULL)
    {
        ObDereferenceObject(ResourceManager);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Basic->ResourceManagerId = ResourceManager->RmId;
    Basic->DescriptionLength = ResourceManager->Description.Length;
    RtlCopyMemory(Basic->Description,
                  ResourceManager->Description.Buffer,
                  ResourceManager->Description.Length);
    Status = TmpReturnInformation(PreviousMode,
                                  ResourceManagerInformation,
                                  ResourceManagerInformationLength,
                                  ReturnLength,
                                  Basic,
                                  Length,
                                  FIELD_OFFSET(RESOURCEMANAGER_BASIC_INFORMATION, Description));
    ExFreePoolWithTag(Basic, TAG_TM);
    ObDereferenceObject(ResourceManager);
    return Status;
}

NTSTATUS
NTAPI
NtSetInformationResourceManager(
    _In_ HANDLE ResourceManagerHandle,
    _In_ RESOURCEMANAGER_INFORMATION_CLASS ResourceManagerInformationClass,
    _In_reads_bytes_(ResourceManagerInformationLength) PVOID ResourceManagerInformation,
    _In_ ULONG ResourceManagerInformationLength)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ResourceManagerHandle);
    UNREFERENCED_PARAMETER(ResourceManagerInformation);
    UNREFERENCED_PARAMETER(ResourceManagerInformationLength);

    if (ResourceManagerInformationClass != ResourceManagerCompletionInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NtRegisterProtocolAddressInformation(
    _In_ HANDLE ResourceManager,
    _In_ PCRM_PROTOCOL_ID ProtocolId,
    _In_ ULONG ProtocolInformationSize,
    _In_ PVOID ProtocolInformation,
    _In_opt_ ULONG CreateOptions)
{
    UNREFERENCED_PARAMETER(ResourceManager);
    UNREFERENCED_PARAMETER(ProtocolId);
    UNREFERENCED_PARAMETER(ProtocolInformationSize);
    UNREFERENCED_PARAMETER(ProtocolInformation);
    UNREFERENCED_PARAMETER(CreateOptions);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NtPropagationComplete(
    _In_ HANDLE ResourceManagerHandle,
    _In_ ULONG RequestCookie,
    _In_ ULONG BufferLength,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(ResourceManagerHandle);
    UNREFERENCED_PARAMETER(RequestCookie);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(Buffer);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NtPropagationFailed(
    _In_ HANDLE ResourceManagerHandle,
    _In_ ULONG RequestCookie,
    _In_ NTSTATUS PropStatus)
{
    UNREFERENCED_PARAMETER(ResourceManagerHandle);
    UNREFERENCED_PARAMETER(RequestCookie);
    UNREFERENCED_PARAMETER(PropStatus);

    return STATUS_NOT_SUPPORTED;
}
