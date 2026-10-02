/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel Transaction Manager: namespaces and transaction manager objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <internal/tm.h>
#define NDEBUG
#include <debug.h>

POBJECT_TYPE TmTransactionManagerObjectType;
POBJECT_TYPE TmTransactionObjectType;
POBJECT_TYPE TmResourceManagerObjectType;
POBJECT_TYPE TmEnlistmentObjectType;

KTMOBJECT_NAMESPACE TmpTransactionNamespace;
KTMOBJECT_NAMESPACE TmpTransactionManagerNamespace;

static LARGE_INTEGER TmpGlobalVirtualClock;
static FAST_MUTEX TmpGlobalVirtualClockMutex;

static GENERIC_MAPPING TmpTransactionManagerMapping =
{
    TRANSACTIONMANAGER_GENERIC_READ,
    TRANSACTIONMANAGER_GENERIC_WRITE,
    TRANSACTIONMANAGER_GENERIC_EXECUTE,
    TRANSACTIONMANAGER_ALL_ACCESS
};

static GENERIC_MAPPING TmpTransactionMapping =
{
    TRANSACTION_GENERIC_READ,
    TRANSACTION_GENERIC_WRITE,
    TRANSACTION_GENERIC_EXECUTE,
    TRANSACTION_ALL_ACCESS
};

static GENERIC_MAPPING TmpResourceManagerMapping =
{
    RESOURCEMANAGER_GENERIC_READ,
    RESOURCEMANAGER_GENERIC_WRITE,
    RESOURCEMANAGER_GENERIC_EXECUTE,
    RESOURCEMANAGER_ALL_ACCESS
};

static GENERIC_MAPPING TmpEnlistmentMapping =
{
    ENLISTMENT_GENERIC_READ,
    ENLISTMENT_GENERIC_WRITE,
    ENLISTMENT_GENERIC_EXECUTE,
    ENLISTMENT_ALL_ACCESS
};

VOID
TmpAcquireMutex(
    _Inout_ PKMUTANT Mutex)
{
    KeWaitForSingleObject(Mutex, Executive, KernelMode, FALSE, NULL);
}

VOID
TmpReleaseMutex(
    _Inout_ PKMUTANT Mutex)
{
    KeReleaseMutex(Mutex, FALSE);
}

BOOLEAN
TmpComponentFiltered(
    _In_ KPROCESSOR_MODE PreviousMode)
{
    return PreviousMode != KernelMode &&
           (ReadULongAcquire(&PsGetCurrentProcess()->DisabledComponentFlags) & PSP_COMPONENT_FILTER_KTM);
}

LARGE_INTEGER
TmpNextVirtualClock(
    _In_opt_ PKTM Tm)
{
    LARGE_INTEGER Clock;

    if (Tm != NULL)
    {
        ExAcquireFastMutex(&Tm->CommitVirtualClockMutex);
        Tm->CommitVirtualClock.QuadPart++;
        Clock = Tm->CommitVirtualClock;
        ExReleaseFastMutex(&Tm->CommitVirtualClockMutex);
    }
    else
    {
        ExAcquireFastMutex(&TmpGlobalVirtualClockMutex);
        TmpGlobalVirtualClock.QuadPart++;
        Clock = TmpGlobalVirtualClock;
        ExReleaseFastMutex(&TmpGlobalVirtualClockMutex);
    }

    return Clock;
}

static
CONST GUID *
TmpNamespaceGuid(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID Element)
{
    PKTMOBJECT_NAMESPACE Namespace = CONTAINING_RECORD(Table, KTMOBJECT_NAMESPACE, Table);
    PUCHAR Object = (PUCHAR)Element - sizeof(RTL_BALANCED_LINKS) - Namespace->LinksOffset;

    return (CONST GUID *)(Object + Namespace->GuidOffset);
}

static
RTL_GENERIC_COMPARE_RESULTS
NTAPI
TmpNamespaceCompare(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct)
{
    CONST GUID *First = TmpNamespaceGuid(Table, FirstStruct);
    CONST GUID *Second = TmpNamespaceGuid(Table, SecondStruct);
    INT Result = memcmp(First, Second, sizeof(GUID));

    if (Result < 0)
    {
        return GenericLessThan;
    }
    if (Result > 0)
    {
        return GenericGreaterThan;
    }
    return GenericEqual;
}

static
PVOID
NTAPI
TmpNamespaceAllocate(
    _In_ PRTL_AVL_TABLE Table,
    _In_ CLONG ByteSize)
{
    UNREFERENCED_PARAMETER(ByteSize);

    return Table->TableContext;
}

static
VOID
NTAPI
TmpNamespaceFree(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(Buffer);
}

VOID
TmpInitializeNamespace(
    _Out_ PKTMOBJECT_NAMESPACE Namespace,
    _In_ USHORT LinksOffset,
    _In_ USHORT GuidOffset)
{
    RtlInitializeGenericTableAvl(&Namespace->Table,
                                 TmpNamespaceCompare,
                                 TmpNamespaceAllocate,
                                 TmpNamespaceFree,
                                 NULL);
    KeInitializeMutex(&Namespace->Mutex, 0);
    Namespace->LinksOffset = LinksOffset;
    Namespace->GuidOffset = GuidOffset;
    Namespace->Expired = FALSE;
}

NTSTATUS
TmpInsertNamespace(
    _Inout_ PKTMOBJECT_NAMESPACE Namespace,
    _In_ PVOID Object)
{
    PKTMOBJECT_NAMESPACE_LINK Link = (PKTMOBJECT_NAMESPACE_LINK)((PUCHAR)Object + Namespace->LinksOffset);
    BOOLEAN NewElement = FALSE;

    TmpAcquireMutex(&Namespace->Mutex);
    Link->Expired = FALSE;
    Namespace->Table.TableContext = Link;
    RtlInsertElementGenericTableAvl(&Namespace->Table,
                                    &Link->Expired,
                                    sizeof(KTMOBJECT_NAMESPACE_LINK) - sizeof(RTL_BALANCED_LINKS),
                                    &NewElement);
    Namespace->Table.TableContext = NULL;
    TmpReleaseMutex(&Namespace->Mutex);

    return NewElement ? STATUS_SUCCESS : STATUS_OBJECT_NAME_COLLISION;
}

VOID
TmpRemoveNamespace(
    _Inout_ PKTMOBJECT_NAMESPACE Namespace,
    _In_ PVOID Object)
{
    PKTMOBJECT_NAMESPACE_LINK Link = (PKTMOBJECT_NAMESPACE_LINK)((PUCHAR)Object + Namespace->LinksOffset);
    PVOID Found;

    TmpAcquireMutex(&Namespace->Mutex);
    Found = RtlLookupElementGenericTableAvl(&Namespace->Table, &Link->Expired);
    if (Found == &Link->Expired)
    {
        RtlDeleteElementGenericTableAvl(&Namespace->Table, &Link->Expired);
    }
    Link->Expired = TRUE;
    TmpReleaseMutex(&Namespace->Mutex);
}

PVOID
TmpLookupNamespace(
    _Inout_ PKTMOBJECT_NAMESPACE Namespace,
    _In_ CONST GUID *Guid)
{
    UCHAR Key[sizeof(KTRANSACTION)];
    PVOID Element, Object = NULL;

    RtlCopyMemory(Key + Namespace->GuidOffset, Guid, sizeof(GUID));

    TmpAcquireMutex(&Namespace->Mutex);
    Element = RtlLookupElementGenericTableAvl(&Namespace->Table,
                                              Key + Namespace->LinksOffset + sizeof(RTL_BALANCED_LINKS));
    if (Element != NULL)
    {
        Object = (PUCHAR)Element - sizeof(RTL_BALANCED_LINKS) - Namespace->LinksOffset;
        if (!ObReferenceObjectSafe(Object))
        {
            Object = NULL;
        }
    }
    TmpReleaseMutex(&Namespace->Mutex);
    return Object;
}

NTSTATUS
TmpEnumerateNamespace(
    _Inout_ PKTMOBJECT_NAMESPACE Namespace,
    _Inout_ PKTMOBJECT_CURSOR Cursor,
    _In_ ULONG CursorLength,
    _Out_ PULONG ReturnLength)
{
    ULONG Capacity = (CursorLength - FIELD_OFFSET(KTMOBJECT_CURSOR, ObjectIds)) / sizeof(GUID);
    ULONG Count = 0;
    CONST GUID *Guid;
    PVOID Element, Restart = NULL;

    TmpAcquireMutex(&Namespace->Mutex);
    for (Element = RtlEnumerateGenericTableWithoutSplayingAvl(&Namespace->Table, &Restart);
         Element != NULL;
         Element = RtlEnumerateGenericTableWithoutSplayingAvl(&Namespace->Table, &Restart))
    {
        Guid = TmpNamespaceGuid(&Namespace->Table, Element);
        if (memcmp(Guid, &Cursor->LastQuery, sizeof(GUID)) <= 0)
        {
            continue;
        }

        if (Count == Capacity)
        {
            break;
        }
        Cursor->ObjectIds[Count++] = *Guid;
    }
    TmpReleaseMutex(&Namespace->Mutex);

    Cursor->ObjectIdCount = Count;
    if (Count != 0)
    {
        Cursor->LastQuery = Cursor->ObjectIds[Count - 1];
    }
    *ReturnLength = FIELD_OFFSET(KTMOBJECT_CURSOR, ObjectIds) + Count * sizeof(GUID);

    return (Count == Capacity) ? STATUS_SUCCESS : STATUS_NO_MORE_ENTRIES;
}

NTSTATUS
TmpCaptureGuid(
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_opt_ LPGUID Guid,
    _Out_ LPGUID Captured,
    _Out_ PBOOLEAN Present)
{
    NTSTATUS Status = STATUS_SUCCESS;

    *Present = (Guid != NULL);
    RtlZeroMemory(Captured, sizeof(GUID));
    if (Guid == NULL)
    {
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(Guid, sizeof(GUID), sizeof(ULONG));
        }
        *Captured = *Guid;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
TmpWriteHandle(
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PHANDLE HandlePointer,
    _In_ HANDLE Handle)
{
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWriteHandle(HandlePointer);
        }
        *HandlePointer = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
TmpCaptureDescription(
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_opt_ PUNICODE_STRING Source,
    _In_ ULONG MaximumCharacters,
    _Out_ PUNICODE_STRING Description)
{
    UNICODE_STRING Local;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlInitEmptyUnicodeString(Description, NULL, 0);
    if (Source == NULL)
    {
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(Source, sizeof(UNICODE_STRING), sizeof(ULONG));
        }
        Local = *Source;
        if (Local.Length > MaximumCharacters * sizeof(WCHAR))
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        else if (Local.Length != 0)
        {
            if (PreviousMode != KernelMode)
            {
                ProbeForRead(Local.Buffer, Local.Length, sizeof(WCHAR));
            }
            Description->Buffer = ExAllocatePoolWithTag(PagedPool, Local.Length, TAG_TM);
            if (Description->Buffer == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                RtlCopyMemory(Description->Buffer, Local.Buffer, Local.Length);
                Description->Length = Description->MaximumLength = Local.Length;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status) && Description->Buffer != NULL)
    {
        ExFreePoolWithTag(Description->Buffer, TAG_TM);
        RtlInitEmptyUnicodeString(Description, NULL, 0);
    }
    return Status;
}

NTSTATUS
TmpReturnInformation(
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG ReturnLength,
    _In_reads_bytes_(SourceLength) PVOID Source,
    _In_ ULONG SourceLength,
    _In_ ULONG FixedLength)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Length < FixedLength)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else if (Length < SourceLength)
    {
        Status = STATUS_BUFFER_OVERFLOW;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(Buffer, Length, sizeof(ULONG));
            if (ReturnLength != NULL)
            {
                ProbeForWriteUlong(ReturnLength);
            }
        }
        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            RtlCopyMemory(Buffer, Source, min(Length, SourceLength));
        }
        if (ReturnLength != NULL)
        {
            *ReturnLength = SourceLength;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
VOID
NTAPI
TmpDeleteTransactionManager(
    _In_ PVOID Object)
{
    PKTM Tm = Object;

    if (Tm->cookie != KTM_COOKIE)
    {
        return;
    }

    TmpRemoveNamespace(&TmpTransactionManagerNamespace, Tm);
    if (Tm->LogFileName.Buffer != NULL)
    {
        ExFreePoolWithTag(Tm->LogFileName.Buffer, TAG_TM);
    }
    ExDeleteResourceLite(&Tm->LogWriteResource);
    Tm->cookie = 0;
}

NTSTATUS
NTAPI
TmInitializeTransactionManager(
    _In_ PRKTM TransactionManager,
    _In_opt_ PCUNICODE_STRING LogFileName,
    _In_opt_ PGUID TmId,
    _In_ ULONG CreateOptions)
{
    PKTM Tm = TransactionManager;
    NTSTATUS Status;

    if ((CreateOptions & ~TRANSACTION_MANAGER_MAXIMUM_OPTION) ||
        ((CreateOptions & TRANSACTION_MANAGER_VOLATILE) != 0) != (LogFileName == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!(CreateOptions & TRANSACTION_MANAGER_VOLATILE))
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(Tm, sizeof(*Tm));
    KeInitializeMutex(&Tm->Mutex, 0);
    Tm->State = KKtmInitialized;
    Tm->Flags = CreateOptions;
    TmpInitializeNamespace(&Tm->Transactions,
                           FIELD_OFFSET(KTRANSACTION, TmNamespaceLink),
                           FIELD_OFFSET(KTRANSACTION, UOW));
    TmpInitializeNamespace(&Tm->ResourceManagers,
                           FIELD_OFFSET(KRESOURCEMANAGER, NamespaceLink),
                           FIELD_OFFSET(KRESOURCEMANAGER, RmId));
    KeInitializeMutex(&Tm->LsnOrderedMutex, 0);
    InitializeListHead(&Tm->LsnOrderedList);
    ExInitializeFastMutex(&Tm->CommitVirtualClockMutex);
    KeInitializeEvent(&Tm->LogFullNotifyEvent, NotificationEvent, FALSE);
    ExInitializeResourceLite(&Tm->LogWriteResource);
    InitializeListHead(&Tm->RestartOrderedList);

    if (TmId != NULL)
    {
        Tm->TmIdentity = *TmId;
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = ExUuidCreate(&Tm->TmIdentity);
    }

    if (NT_SUCCESS(Status))
    {
        Tm->cookie = KTM_COOKIE;
        Status = TmpInsertNamespace(&TmpTransactionManagerNamespace, Tm);
        if (!NT_SUCCESS(Status))
        {
            Tm->cookie = 0;
        }
    }
    if (!NT_SUCCESS(Status))
    {
        ExDeleteResourceLite(&Tm->LogWriteResource);
        return Status;
    }

    Tm->State = KKtmOnline;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtCreateTransactionManager(
    _Out_ PHANDLE TmHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PUNICODE_STRING LogFileName,
    _In_opt_ ULONG CreateOptions,
    _In_opt_ ULONG CommitStrength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle;
    NTSTATUS Status;
    PKTM Tm;
    PAGED_CODE();

    if (TmpComponentFiltered(PreviousMode))
    {
        return STATUS_ACCESS_DENIED;
    }

    if (CommitStrength != 0 ||
        (CreateOptions & ~TRANSACTION_MANAGER_MAXIMUM_OPTION) ||
        ((CreateOptions & TRANSACTION_MANAGER_VOLATILE) != 0) != (LogFileName == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!(CreateOptions & TRANSACTION_MANAGER_VOLATILE))
    {
        return STATUS_NOT_SUPPORTED;
    }

    Status = ObCreateObject(PreviousMode,
                            TmTransactionManagerObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(KTM),
                            0,
                            0,
                            (PVOID *)&Tm);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Tm->cookie = 0;
    Status = TmInitializeTransactionManager(Tm, NULL, NULL, CreateOptions);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Tm);
        return Status;
    }

    Status = ObInsertObject(Tm, NULL, DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmpWriteHandle(PreviousMode, TmHandle, Handle);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(Handle, PreviousMode);
    }
    return Status;
}

NTSTATUS
NTAPI
NtOpenTransactionManager(
    _Out_ PHANDLE TmHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PUNICODE_STRING LogFileName,
    _In_opt_ LPGUID TmIdentity,
    _In_opt_ ULONG OpenOptions)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    BOOLEAN Present;
    HANDLE Handle;
    NTSTATUS Status;
    GUID Guid;
    PKTM Tm;
    PAGED_CODE();

    UNREFERENCED_PARAMETER(OpenOptions);

    if (TmpComponentFiltered(PreviousMode))
    {
        return STATUS_ACCESS_DENIED;
    }

    if (LogFileName != NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    Status = TmpCaptureGuid(PreviousMode, TmIdentity, &Guid, &Present);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Present)
    {
        Tm = TmpLookupNamespace(&TmpTransactionManagerNamespace, &Guid);
        if (Tm == NULL)
        {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        Status = ObOpenObjectByPointer(Tm,
                                       (PreviousMode == KernelMode) ? OBJ_KERNEL_HANDLE : 0,
                                       NULL,
                                       DesiredAccess,
                                       TmTransactionManagerObjectType,
                                       PreviousMode,
                                       &Handle);
        ObDereferenceObject(Tm);
    }
    else if (ObjectAttributes != NULL)
    {
        Status = ObOpenObjectByName(ObjectAttributes,
                                    TmTransactionManagerObjectType,
                                    PreviousMode,
                                    NULL,
                                    DesiredAccess,
                                    NULL,
                                    &Handle);
    }
    else
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmpWriteHandle(PreviousMode, TmHandle, Handle);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(Handle, PreviousMode);
    }
    return Status;
}

NTSTATUS
NTAPI
TmRenameTransactionManager(
    _In_ PUNICODE_STRING LogFileName,
    _In_ LPGUID ExistingTransactionManagerGuid)
{
    UNREFERENCED_PARAMETER(LogFileName);
    UNREFERENCED_PARAMETER(ExistingTransactionManagerGuid);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NtRenameTransactionManager(
    _In_ PUNICODE_STRING LogFileName,
    _In_ LPGUID ExistingTransactionManagerGuid)
{
    return TmRenameTransactionManager(LogFileName, ExistingTransactionManagerGuid);
}

NTSTATUS
NTAPI
TmRecoverTransactionManager(
    _In_ PKTM Tm,
    _In_ PLARGE_INTEGER TargetVirtualClock)
{
    UNREFERENCED_PARAMETER(Tm);
    UNREFERENCED_PARAMETER(TargetVirtualClock);

    return STATUS_TM_VOLATILE;
}

static
NTSTATUS
TmpRecoverByHandle(
    _In_ HANDLE TransactionManagerHandle)
{
    LARGE_INTEGER Clock;
    NTSTATUS Status;
    PKTM Tm;

    Status = ObReferenceObjectByHandle(TransactionManagerHandle,
                                       TRANSACTIONMANAGER_RECOVER,
                                       TmTransactionManagerObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Tm,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Clock.QuadPart = 0;
    Status = TmRecoverTransactionManager(Tm, &Clock);
    ObDereferenceObject(Tm);
    return Status;
}

NTSTATUS
NTAPI
NtRecoverTransactionManager(
    _In_ HANDLE TransactionManagerHandle)
{
    PAGED_CODE();

    return TmpRecoverByHandle(TransactionManagerHandle);
}

NTSTATUS
NTAPI
NtRollforwardTransactionManager(
    _In_ HANDLE TransactionManagerHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(TmVirtualClock);

    return TmpRecoverByHandle(TransactionManagerHandle);
}

NTSTATUS
NTAPI
NtQueryInformationTransactionManager(
    _In_ HANDLE TransactionManagerHandle,
    _In_ TRANSACTIONMANAGER_INFORMATION_CLASS TransactionManagerInformationClass,
    _Out_writes_bytes_(TransactionManagerInformationLength) PVOID TransactionManagerInformation,
    _In_ ULONG TransactionManagerInformationLength,
    _Out_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    TRANSACTIONMANAGER_BASIC_INFORMATION Basic;
    TRANSACTIONMANAGER_RECOVERY_INFORMATION Recovery;
    NTSTATUS Status;
    PKTM Tm;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(TransactionManagerHandle,
                                       TRANSACTIONMANAGER_QUERY_INFORMATION,
                                       TmTransactionManagerObjectType,
                                       PreviousMode,
                                       (PVOID *)&Tm,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    switch (TransactionManagerInformationClass)
    {
        case TransactionManagerBasicInformation:
            Basic.TmIdentity = Tm->TmIdentity;
            ExAcquireFastMutex(&Tm->CommitVirtualClockMutex);
            Basic.VirtualClock = Tm->CommitVirtualClock;
            ExReleaseFastMutex(&Tm->CommitVirtualClockMutex);
            Status = TmpReturnInformation(PreviousMode,
                                          TransactionManagerInformation,
                                          TransactionManagerInformationLength,
                                          ReturnLength,
                                          &Basic,
                                          sizeof(Basic),
                                          sizeof(Basic));
            break;

        case TransactionManagerRecoveryInformation:
            Recovery.LastRecoveredLsn = Tm->LastRecoveredLsn.ullOffset;
            Status = TmpReturnInformation(PreviousMode,
                                          TransactionManagerInformation,
                                          TransactionManagerInformationLength,
                                          ReturnLength,
                                          &Recovery,
                                          sizeof(Recovery),
                                          sizeof(Recovery));
            break;

        case TransactionManagerLogInformation:
        case TransactionManagerLogPathInformation:
            Status = STATUS_NOT_SUPPORTED;
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    ObDereferenceObject(Tm);
    return Status;
}

NTSTATUS
NTAPI
NtSetInformationTransactionManager(
    _In_opt_ HANDLE TmHandle,
    _In_ TRANSACTIONMANAGER_INFORMATION_CLASS TransactionManagerInformationClass,
    _In_reads_bytes_(TransactionManagerInformationLength) PVOID TransactionManagerInformation,
    _In_ ULONG TransactionManagerInformationLength)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(TmHandle);
    UNREFERENCED_PARAMETER(TransactionManagerInformationClass);
    UNREFERENCED_PARAMETER(TransactionManagerInformation);
    UNREFERENCED_PARAMETER(TransactionManagerInformationLength);

    return STATUS_INVALID_INFO_CLASS;
}

NTSTATUS
NTAPI
NtEnumerateTransactionObject(
    _In_opt_ HANDLE RootObjectHandle,
    _In_ KTMOBJECT_TYPE QueryType,
    _Inout_updates_bytes_(ObjectCursorLength) PKTMOBJECT_CURSOR ObjectCursor,
    _In_ ULONG ObjectCursorLength,
    _Out_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKTMOBJECT_NAMESPACE Namespace = NULL;
    PKTMOBJECT_CURSOR Cursor;
    PVOID Root = NULL;
    ULONG Returned = 0;
    NTSTATUS Status, CopyStatus = STATUS_SUCCESS;
    PAGED_CODE();

    if (ObjectCursorLength < sizeof(KTMOBJECT_CURSOR))
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (QueryType)
    {
        case KTMOBJECT_TRANSACTION:
            if (RootObjectHandle == NULL)
            {
                Namespace = &TmpTransactionNamespace;
                break;
            }
            Status = ObReferenceObjectByHandle(RootObjectHandle,
                                               TRANSACTIONMANAGER_QUERY_INFORMATION,
                                               TmTransactionManagerObjectType,
                                               PreviousMode,
                                               &Root,
                                               NULL);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            Namespace = &((PKTM)Root)->Transactions;
            break;

        case KTMOBJECT_TRANSACTION_MANAGER:
            Namespace = &TmpTransactionManagerNamespace;
            break;

        case KTMOBJECT_RESOURCE_MANAGER:
            Status = ObReferenceObjectByHandle(RootObjectHandle,
                                               TRANSACTIONMANAGER_QUERY_INFORMATION,
                                               TmTransactionManagerObjectType,
                                               PreviousMode,
                                               &Root,
                                               NULL);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            Namespace = &((PKTM)Root)->ResourceManagers;
            break;

        case KTMOBJECT_ENLISTMENT:
            Status = ObReferenceObjectByHandle(RootObjectHandle,
                                               RESOURCEMANAGER_QUERY_INFORMATION,
                                               TmResourceManagerObjectType,
                                               PreviousMode,
                                               &Root,
                                               NULL);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            Namespace = &((PKRESOURCEMANAGER)Root)->Enlistments;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    Cursor = ExAllocatePoolWithTag(PagedPool, ObjectCursorLength, TAG_TM);
    if (Cursor == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(ObjectCursor, ObjectCursorLength, sizeof(ULONG));
            ProbeForWriteUlong(ReturnLength);
        }
        Cursor->LastQuery = ObjectCursor->LastQuery;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        CopyStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(CopyStatus))
    {
        ExFreePoolWithTag(Cursor, TAG_TM);
        Status = CopyStatus;
        goto Exit;
    }

    Status = TmpEnumerateNamespace(Namespace, Cursor, ObjectCursorLength, &Returned);

    _SEH2_TRY
    {
        RtlCopyMemory(ObjectCursor, Cursor, Returned);
        *ReturnLength = Returned;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ExFreePoolWithTag(Cursor, TAG_TM);

Exit:
    if (Root != NULL)
    {
        ObDereferenceObject(Root);
    }
    return Status;
}

static
NTSTATUS
TmpCreateObjectType(
    _In_ PCWSTR Name,
    _In_ PGENERIC_MAPPING Mapping,
    _In_ ACCESS_MASK ValidAccess,
    _In_ ULONG Size,
    _In_ BOOLEAN Waitable,
    _In_opt_ OB_CLOSE_METHOD CloseProcedure,
    _In_opt_ OB_DELETE_METHOD DeleteProcedure,
    _Out_ POBJECT_TYPE *ObjectType)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING TypeName;

    RtlInitUnicodeString(&TypeName, Name);
    RtlZeroMemory(&Initializer, sizeof(Initializer));
    Initializer.Length = sizeof(Initializer);
    Initializer.GenericMapping = *Mapping;
    Initializer.ValidAccessMask = ValidAccess;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = Size;
    Initializer.UseDefaultObject = !Waitable;
    Initializer.CloseProcedure = CloseProcedure;
    Initializer.DeleteProcedure = DeleteProcedure;

    return ObCreateObjectType(&TypeName, &Initializer, NULL, ObjectType);
}

BOOLEAN
NTAPI
TmpInitSystem(VOID)
{
    ExInitializeFastMutex(&TmpGlobalVirtualClockMutex);
    TmpInitializeNamespace(&TmpTransactionNamespace,
                           FIELD_OFFSET(KTRANSACTION, GlobalNamespaceLink),
                           FIELD_OFFSET(KTRANSACTION, UOW));
    TmpInitializeNamespace(&TmpTransactionManagerNamespace,
                           FIELD_OFFSET(KTM, NamespaceLink),
                           FIELD_OFFSET(KTM, TmIdentity));

    if (!NT_SUCCESS(TmpCreateObjectType(L"TmTm",
                                        &TmpTransactionManagerMapping,
                                        TRANSACTIONMANAGER_ALL_ACCESS,
                                        sizeof(KTM),
                                        FALSE,
                                        NULL,
                                        TmpDeleteTransactionManager,
                                        &TmTransactionManagerObjectType)) ||
        !NT_SUCCESS(TmpCreateObjectType(L"TmTx",
                                        &TmpTransactionMapping,
                                        TRANSACTION_ALL_ACCESS,
                                        sizeof(KTRANSACTION),
                                        TRUE,
                                        TmpCloseTransaction,
                                        TmpDeleteTransaction,
                                        &TmTransactionObjectType)) ||
        !NT_SUCCESS(TmpCreateObjectType(L"TmRm",
                                        &TmpResourceManagerMapping,
                                        RESOURCEMANAGER_ALL_ACCESS,
                                        sizeof(KRESOURCEMANAGER),
                                        TRUE,
                                        TmpCloseResourceManager,
                                        TmpDeleteResourceManager,
                                        &TmResourceManagerObjectType)) ||
        !NT_SUCCESS(TmpCreateObjectType(L"TmEn",
                                        &TmpEnlistmentMapping,
                                        ENLISTMENT_ALL_ACCESS,
                                        sizeof(KENLISTMENT),
                                        FALSE,
                                        TmpCloseEnlistment,
                                        TmpDeleteEnlistment,
                                        &TmEnlistmentObjectType)))
    {
        return FALSE;
    }

    return TRUE;
}
