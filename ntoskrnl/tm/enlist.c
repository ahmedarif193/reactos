/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel Transaction Manager: enlistment objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <internal/tm.h>
#define NDEBUG
#include <debug.h>

VOID
TmpAdvanceVirtualClock(
    _In_opt_ PKTM Tm,
    _In_opt_ PLARGE_INTEGER Clock)
{
    if (Tm == NULL || Clock == NULL)
    {
        return;
    }

    ExAcquireFastMutex(&Tm->CommitVirtualClockMutex);
    if (Clock->QuadPart > Tm->CommitVirtualClock.QuadPart)
    {
        Tm->CommitVirtualClock = *Clock;
    }
    ExReleaseFastMutex(&Tm->CommitVirtualClockMutex);
}

static
VOID
TmpRecordResponse(
    _Inout_ PKENLISTMENT Enlistment)
{
    ULONG Index = Enlistment->NextHistory % RTL_NUMBER_OF(Enlistment->History);

    Enlistment->History[Index].Notification = 0;
    Enlistment->History[Index].NewState = Enlistment->State;
    Enlistment->NextHistory++;
}

static
BOOLEAN
TmpEnlistmentFinished(
    _In_ PKENLISTMENT Enlistment)
{
    switch (Enlistment->State)
    {
        case KEnlistmentReadOnly:
        case KEnlistmentAborted:
        case KEnlistmentCommitted:
        case KEnlistmentForgotten:
            return TRUE;

        default:
            return FALSE;
    }
}

VOID
TmpAbandonEnlistment(
    _In_ PKENLISTMENT Enlistment,
    _In_ ULONG Notification)
{
    PKTRANSACTION Transaction = Enlistment->Transaction;
    KENLISTMENT_STATE Pending;
    BOOLEAN Rollback = FALSE;

    switch (Notification)
    {
        case TRANSACTION_NOTIFY_PREPREPARE: Pending = KEnlistmentPrePreparing; break;
        case TRANSACTION_NOTIFY_PREPARE: Pending = KEnlistmentPreparing; break;
        case TRANSACTION_NOTIFY_SINGLE_PHASE_COMMIT: Pending = KEnlistmentCommitRequested; break;
        case TRANSACTION_NOTIFY_COMMIT: Pending = KEnlistmentCommittedNotify; break;
        case TRANSACTION_NOTIFY_ROLLBACK: Pending = KEnlistmentAborting; break;
        default: Pending = KEnlistmentUninitialized; break;
    }

    TmpAcquireMutex(&Transaction->Mutex);
    if (Notification == 0)
    {
        Enlistment->Flags |= KENLISTMENT_FLAG_CLOSED;
    }
    else if (Enlistment->State != Pending)
    {
        TmpReleaseMutex(&Transaction->Mutex);
        return;
    }

    switch (Enlistment->State)
    {
        case KEnlistmentCommittedNotify:
            Enlistment->State = KEnlistmentCommitted;
            Transaction->PendingResponses--;
            break;

        case KEnlistmentAborting:
            Enlistment->State = KEnlistmentAborted;
            Transaction->PendingResponses--;
            break;

        case KEnlistmentPrePreparing:
        case KEnlistmentPreparing:
        case KEnlistmentCommitRequested:
            Enlistment->State = KEnlistmentActive;
            Rollback = TRUE;
            break;

        case KEnlistmentActive:
        case KEnlistmentPrePrepared:
            Rollback = TRUE;
            break;

        case KEnlistmentPrepared:
            Rollback = (Transaction->State != KTransactionPrepared &&
                        Transaction->State != KTransactionCommitted);
            break;

        default:
            break;
    }

    if (Rollback &&
        Transaction->State != KTransactionCommitted &&
        Transaction->State != KTransactionAborted)
    {
        Transaction->Flags |= KTRANSACTION_FLAG_ROLLBACK_REQUESTED;
    }
    TmpRecordResponse(Enlistment);
    TmpReleaseMutex(&Transaction->Mutex);

    (VOID)TmpPumpTransaction(Transaction);
}

static
NTSTATUS
TmpRespond(
    _Inout_ PKENLISTMENT Enlistment,
    _In_ KENLISTMENT_STATE Expected,
    _In_ KENLISTMENT_STATE NewState,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    PKTRANSACTION Transaction = Enlistment->Transaction;

    TmpAcquireMutex(&Transaction->Mutex);
    if (Enlistment->State != Expected)
    {
        TmpReleaseMutex(&Transaction->Mutex);
        return STATUS_TRANSACTION_NOT_REQUESTED;
    }

    if (Expected == KEnlistmentCommitRequested && NewState == KEnlistmentCommitted)
    {
        Transaction->Outcome = KTxOutcomeCommitted;
    }
    Enlistment->State = NewState;
    Transaction->PendingResponses--;
    TmpRecordResponse(Enlistment);
    TmpReleaseMutex(&Transaction->Mutex);

    TmpAdvanceVirtualClock(Transaction->Tm, TmVirtualClock);
    return TmpPumpTransaction(Transaction) ? STATUS_PENDING : STATUS_SUCCESS;
}

NTSTATUS
NTAPI
TmPrePrepareComplete(
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpRespond(Enlistment, KEnlistmentPrePreparing, KEnlistmentPrePrepared, TmVirtualClock);
}

NTSTATUS
NTAPI
TmPrepareComplete(
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpRespond(Enlistment, KEnlistmentPreparing, KEnlistmentPrepared, TmVirtualClock);
}

NTSTATUS
NTAPI
TmCommitComplete(
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    NTSTATUS Status;

    Status = TmpRespond(Enlistment, KEnlistmentCommittedNotify, KEnlistmentCommitted, TmVirtualClock);
    if (Status == STATUS_TRANSACTION_NOT_REQUESTED)
    {
        Status = TmpRespond(Enlistment, KEnlistmentCommitRequested, KEnlistmentCommitted, TmVirtualClock);
    }
    return Status;
}

NTSTATUS
NTAPI
TmRollbackComplete(
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpRespond(Enlistment, KEnlistmentAborting, KEnlistmentAborted, TmVirtualClock);
}

NTSTATUS
NTAPI
TmSinglePhaseReject(
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpRespond(Enlistment, KEnlistmentCommitRequested, KEnlistmentActive, TmVirtualClock);
}

NTSTATUS
NTAPI
TmReadOnlyEnlistment(
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    PKTRANSACTION Transaction = Enlistment->Transaction;
    NTSTATUS Status = STATUS_SUCCESS;

    TmpAcquireMutex(&Transaction->Mutex);
    switch (Enlistment->State)
    {
        case KEnlistmentPrePreparing:
        case KEnlistmentPreparing:
            Transaction->PendingResponses--;
            Enlistment->State = KEnlistmentReadOnly;
            TmpRecordResponse(Enlistment);
            break;

        case KEnlistmentActive:
        case KEnlistmentPrePrepared:
            Enlistment->State = KEnlistmentReadOnly;
            TmpRecordResponse(Enlistment);
            break;

        default:
            Status = STATUS_TRANSACTION_NOT_REQUESTED;
            break;
    }
    TmpReleaseMutex(&Transaction->Mutex);

    if (NT_SUCCESS(Status))
    {
        TmpAdvanceVirtualClock(Transaction->Tm, TmVirtualClock);
        if (TmpPumpTransaction(Transaction))
        {
            Status = STATUS_PENDING;
        }
    }
    return Status;
}

NTSTATUS
NTAPI
TmRollbackEnlistment(
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    PKTRANSACTION Transaction = Enlistment->Transaction;
    NTSTATUS Status = STATUS_SUCCESS;

    TmpAcquireMutex(&Transaction->Mutex);
    switch (Enlistment->State)
    {
        case KEnlistmentActive:
        case KEnlistmentPrePreparing:
        case KEnlistmentPrePrepared:
        case KEnlistmentPreparing:
        case KEnlistmentCommitRequested:
            if (Transaction->State == KTransactionCommitted || Transaction->State == KTransactionPrepared)
            {
                Status = STATUS_TRANSACTION_REQUEST_NOT_VALID;
            }
            else if (Transaction->State != KTransactionAborted)
            {
                Transaction->Flags |= KTRANSACTION_FLAG_ROLLBACK_REQUESTED;
            }
            break;

        default:
            Status = STATUS_TRANSACTION_REQUEST_NOT_VALID;
            break;
    }
    TmpReleaseMutex(&Transaction->Mutex);

    if (NT_SUCCESS(Status))
    {
        TmpAdvanceVirtualClock(Transaction->Tm, TmVirtualClock);
        if (TmpPumpTransaction(Transaction))
        {
            Status = STATUS_PENDING;
        }
    }
    return Status;
}

NTSTATUS
NTAPI
TmPrePrepareEnlistment(
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock)
{
    UNREFERENCED_PARAMETER(Enlistment);
    UNREFERENCED_PARAMETER(TmVirtualClock);

    return STATUS_ENLISTMENT_NOT_SUPERIOR;
}

NTSTATUS
NTAPI
TmPrepareEnlistment(
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock)
{
    UNREFERENCED_PARAMETER(Enlistment);
    UNREFERENCED_PARAMETER(TmVirtualClock);

    return STATUS_ENLISTMENT_NOT_SUPERIOR;
}

NTSTATUS
NTAPI
TmCommitEnlistment(
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock)
{
    UNREFERENCED_PARAMETER(Enlistment);
    UNREFERENCED_PARAMETER(TmVirtualClock);

    return STATUS_ENLISTMENT_NOT_SUPERIOR;
}

NTSTATUS
NTAPI
TmRecoverEnlistment(
    _In_ PKENLISTMENT Enlistment,
    _In_ PVOID EnlistmentKey)
{
    UNREFERENCED_PARAMETER(Enlistment);
    UNREFERENCED_PARAMETER(EnlistmentKey);

    return STATUS_TRANSACTION_REQUEST_NOT_VALID;
}

NTSTATUS
NTAPI
TmRequestOutcomeEnlistment(
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock)
{
    PKTRANSACTION Transaction = Enlistment->Transaction;
    NTSTATUS Status = STATUS_SUCCESS;

    TmpAcquireMutex(&Transaction->Mutex);
    if (Enlistment->State != KEnlistmentPrepared)
    {
        Status = STATUS_TRANSACTION_REQUEST_NOT_VALID;
    }
    else if (Transaction->State == KTransactionPreparing)
    {
        Transaction->Flags |= KTRANSACTION_FLAG_ROLLBACK_REQUESTED;
    }
    TmpReleaseMutex(&Transaction->Mutex);

    if (NT_SUCCESS(Status))
    {
        TmpAdvanceVirtualClock(Transaction->Tm, TmVirtualClock);
        if (TmpPumpTransaction(Transaction))
        {
            Status = STATUS_PENDING;
        }
    }
    return Status;
}

NTSTATUS
NTAPI
TmReferenceEnlistmentKey(
    _In_ PKENLISTMENT Enlistment,
    _Out_ PVOID *Key)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Key == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    TmpAcquireMutex(&Enlistment->Mutex);
    if (Enlistment->KeyRefCount == 0)
    {
        Status = STATUS_UNSUCCESSFUL;
    }
    else if (Enlistment->KeyRefCount == MAXULONG)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        Enlistment->KeyRefCount++;
        *Key = Enlistment->Key;
    }
    TmpReleaseMutex(&Enlistment->Mutex);

    return Status;
}

NTSTATUS
NTAPI
TmDereferenceEnlistmentKey(
    _In_ PKENLISTMENT Enlistment,
    _Out_opt_ PBOOLEAN LastReference)
{
    NTSTATUS Status = STATUS_SUCCESS;

    TmpAcquireMutex(&Enlistment->Mutex);
    if (Enlistment->KeyRefCount == 0)
    {
        Status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        Enlistment->KeyRefCount--;
        if (LastReference != NULL)
        {
            *LastReference = (Enlistment->KeyRefCount == 0);
        }
    }
    TmpReleaseMutex(&Enlistment->Mutex);

    return Status;
}

VOID
NTAPI
TmpCloseEnlistment(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    PKENLISTMENT Enlistment = Object;

    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);

    if (SystemHandleCount != 1 || Enlistment->cookie != KENLISTMENT_COOKIE)
    {
        return;
    }

    TmpAbandonEnlistment(Enlistment, 0);
}

VOID
NTAPI
TmpDeleteEnlistment(
    _In_ PVOID Object)
{
    PKENLISTMENT Enlistment = Object;
    PKRESOURCEMANAGER ResourceManager;
    PKTRANSACTION Transaction;

    if (Enlistment->cookie != KENLISTMENT_COOKIE)
    {
        return;
    }

    ResourceManager = Enlistment->ResourceManager;
    Transaction = Enlistment->Transaction;

    TmpAcquireMutex(&Transaction->Mutex);
    RemoveEntryList(&Enlistment->NextSameTx);
    Transaction->EnlistmentCount--;
    TmpReleaseMutex(&Transaction->Mutex);

    TmpAcquireMutex(&ResourceManager->Mutex);
    RemoveEntryList(&Enlistment->NextSameRm);
    ResourceManager->EnlistmentCount--;
    TmpReleaseMutex(&ResourceManager->Mutex);

    TmpRemoveNamespace(&ResourceManager->Enlistments, Enlistment);

    if (Enlistment->RecoveryInformation != NULL)
    {
        ExFreePoolWithTag(Enlistment->RecoveryInformation, TAG_TM);
    }
    Enlistment->cookie = 0;

    ObDereferenceObject(Transaction);
    ObDereferenceObject(ResourceManager);
}

static
NTSTATUS
TmpCreateEnlistment(
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ PKRESOURCEMANAGER ResourceManager,
    _In_ PKTRANSACTION Transaction,
    _In_ ULONG CreateOptions,
    _In_ NOTIFICATION_MASK NotificationMask,
    _In_opt_ PVOID EnlistmentKey,
    _Out_ PHANDLE EnlistmentHandle)
{
    PKENLISTMENT Enlistment;
    NTSTATUS Status;

    if ((CreateOptions & ~ENLISTMENT_SUPERIOR) || (NotificationMask & ~TRANSACTION_NOTIFY_MASK))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (CreateOptions & ENLISTMENT_SUPERIOR)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (ResourceManager->State != KResourceManagerOnline || ResourceManager->Tm->State != KKtmOnline)
    {
        return STATUS_TRANSACTIONMANAGER_NOT_ONLINE;
    }

    TmpBindTransaction(Transaction, ResourceManager->Tm);

    Status = ObCreateObject(PreviousMode,
                            TmEnlistmentObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(KENLISTMENT),
                            0,
                            0,
                            (PVOID *)&Enlistment);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(Enlistment, sizeof(*Enlistment));
    KeInitializeMutex(&Enlistment->Mutex, 0);
    Enlistment->State = KEnlistmentActive;
    Enlistment->NotificationMask = NotificationMask;
    Enlistment->Key = EnlistmentKey;
    Enlistment->KeyRefCount = 1;

    Status = ExUuidCreate(&Enlistment->EnlistmentId);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Enlistment);
        return Status;
    }

    TmpAcquireMutex(&Transaction->Mutex);
    if (!TmIsTransactionActive(Transaction))
    {
        TmpReleaseMutex(&Transaction->Mutex);
        ObDereferenceObject(Enlistment);
        return STATUS_TRANSACTION_NOT_ACTIVE;
    }

    ObReferenceObject(Transaction);
    ObReferenceObject(ResourceManager);
    Enlistment->Transaction = Transaction;
    Enlistment->ResourceManager = ResourceManager;
    Enlistment->cookie = KENLISTMENT_COOKIE;
    InsertTailList(&Transaction->EnlistmentHead, &Enlistment->NextSameTx);
    Transaction->EnlistmentCount++;

    TmpAcquireMutex(&ResourceManager->Mutex);
    InsertTailList(&ResourceManager->EnlistmentHead, &Enlistment->NextSameRm);
    ResourceManager->EnlistmentCount++;
    TmpReleaseMutex(&ResourceManager->Mutex);
    TmpReleaseMutex(&Transaction->Mutex);

    (VOID)TmpInsertNamespace(&ResourceManager->Enlistments, Enlistment);

    return ObInsertObject(Enlistment, NULL, DesiredAccess, 0, NULL, EnlistmentHandle);
}

NTSTATUS
NTAPI
TmCreateEnlistment(
    _Out_ PHANDLE EnlistmentHandle,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ PRKRESOURCEMANAGER ResourceManager,
    _In_ PKTRANSACTION Transaction,
    _In_opt_ ULONG CreateOptions,
    _In_ NOTIFICATION_MASK NotificationMask,
    _In_opt_ PVOID EnlistmentKey)
{
    return TmpCreateEnlistment(PreviousMode,
                               DesiredAccess,
                               ObjectAttributes,
                               ResourceManager,
                               Transaction,
                               CreateOptions,
                               NotificationMask,
                               EnlistmentKey,
                               EnlistmentHandle);
}

NTSTATUS
NTAPI
NtCreateEnlistment(
    _Out_ PHANDLE EnlistmentHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ HANDLE ResourceManagerHandle,
    _In_ HANDLE TransactionHandle,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ ULONG CreateOptions,
    _In_ NOTIFICATION_MASK NotificationMask,
    _In_opt_ PVOID EnlistmentKey)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKRESOURCEMANAGER ResourceManager;
    PKTRANSACTION Transaction;
    HANDLE Handle;
    NTSTATUS Status;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ResourceManagerHandle,
                                       RESOURCEMANAGER_ENLIST,
                                       TmResourceManagerObjectType,
                                       PreviousMode,
                                       (PVOID *)&ResourceManager,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ObReferenceObjectByHandle(TransactionHandle,
                                       TRANSACTION_ENLIST,
                                       TmTransactionObjectType,
                                       PreviousMode,
                                       (PVOID *)&Transaction,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(ResourceManager);
        return Status;
    }

    Status = TmpCreateEnlistment(PreviousMode,
                                 DesiredAccess,
                                 ObjectAttributes,
                                 ResourceManager,
                                 Transaction,
                                 CreateOptions,
                                 NotificationMask,
                                 EnlistmentKey,
                                 &Handle);
    ObDereferenceObject(Transaction);
    ObDereferenceObject(ResourceManager);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmpWriteHandle(PreviousMode, EnlistmentHandle, Handle);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(Handle, PreviousMode);
    }
    return Status;
}

NTSTATUS
NTAPI
NtOpenEnlistment(
    _Out_ PHANDLE EnlistmentHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ HANDLE ResourceManagerHandle,
    _In_ LPGUID EnlistmentGuid,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKRESOURCEMANAGER ResourceManager;
    PKENLISTMENT Enlistment;
    BOOLEAN Present;
    HANDLE Handle;
    NTSTATUS Status;
    GUID Guid;
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ObjectAttributes);

    Status = TmpCaptureGuid(PreviousMode, EnlistmentGuid, &Guid, &Present);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (DesiredAccess == 0 || !Present)
    {
        return STATUS_INVALID_PARAMETER;
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

    Enlistment = TmpLookupNamespace(&ResourceManager->Enlistments, &Guid);
    ObDereferenceObject(ResourceManager);
    if (Enlistment == NULL)
    {
        return STATUS_ENLISTMENT_NOT_FOUND;
    }

    Status = ObOpenObjectByPointer(Enlistment,
                                   (PreviousMode == KernelMode) ? OBJ_KERNEL_HANDLE : 0,
                                   NULL,
                                   DesiredAccess,
                                   TmEnlistmentObjectType,
                                   PreviousMode,
                                   &Handle);
    ObDereferenceObject(Enlistment);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmpWriteHandle(PreviousMode, EnlistmentHandle, Handle);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(Handle, PreviousMode);
    }
    return Status;
}

NTSTATUS
NTAPI
NtQueryInformationEnlistment(
    _In_ HANDLE EnlistmentHandle,
    _In_ ENLISTMENT_INFORMATION_CLASS EnlistmentInformationClass,
    _Out_writes_bytes_(EnlistmentInformationLength) PVOID EnlistmentInformation,
    _In_ ULONG EnlistmentInformationLength,
    _Out_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ENLISTMENT_BASIC_INFORMATION Basic;
    ENLISTMENT_CRM_INFORMATION Crm;
    PKENLISTMENT Enlistment;
    NTSTATUS Status;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(EnlistmentHandle,
                                       ENLISTMENT_QUERY_INFORMATION,
                                       TmEnlistmentObjectType,
                                       PreviousMode,
                                       (PVOID *)&Enlistment,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    switch (EnlistmentInformationClass)
    {
        case EnlistmentBasicInformation:
            if (EnlistmentInformationLength < sizeof(Basic))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            Basic.EnlistmentId = Enlistment->EnlistmentId;
            Basic.TransactionId = Enlistment->Transaction->UOW;
            Basic.ResourceManagerId = Enlistment->ResourceManager->RmId;
            Status = TmpReturnInformation(PreviousMode,
                                          EnlistmentInformation,
                                          EnlistmentInformationLength,
                                          ReturnLength,
                                          &Basic,
                                          sizeof(Basic),
                                          sizeof(Basic));
            break;

        case EnlistmentRecoveryInformation:
            TmpAcquireMutex(&Enlistment->Mutex);
            if (EnlistmentInformationLength < Enlistment->RecoveryInformationLength)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
            }
            else
            {
                Status = TmpReturnInformation(PreviousMode,
                                              EnlistmentInformation,
                                              EnlistmentInformationLength,
                                              ReturnLength,
                                              Enlistment->RecoveryInformation,
                                              Enlistment->RecoveryInformationLength,
                                              0);
            }
            TmpReleaseMutex(&Enlistment->Mutex);
            break;

        case EnlistmentCrmInformation:
            if (EnlistmentInformationLength < sizeof(Crm))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            Crm.CrmTransactionManagerId = Enlistment->CrmEnlistmentTmId;
            Crm.CrmResourceManagerId = Enlistment->CrmEnlistmentRmId;
            Crm.CrmEnlistmentId = Enlistment->CrmEnlistmentEnId;
            Status = TmpReturnInformation(PreviousMode,
                                          EnlistmentInformation,
                                          EnlistmentInformationLength,
                                          ReturnLength,
                                          &Crm,
                                          sizeof(Crm),
                                          sizeof(Crm));
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    ObDereferenceObject(Enlistment);
    return Status;
}

NTSTATUS
NTAPI
NtSetInformationEnlistment(
    _In_opt_ HANDLE EnlistmentHandle,
    _In_ ENLISTMENT_INFORMATION_CLASS EnlistmentInformationClass,
    _In_reads_bytes_(EnlistmentInformationLength) PVOID EnlistmentInformation,
    _In_ ULONG EnlistmentInformationLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKENLISTMENT Enlistment;
    PVOID Buffer = NULL, Old;
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();

    if (EnlistmentInformationClass != EnlistmentRecoveryInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (EnlistmentInformationLength != 0)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, EnlistmentInformationLength, TAG_TM);
        if (Buffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        _SEH2_TRY
        {
            if (PreviousMode != KernelMode)
            {
                ProbeForRead(EnlistmentInformation, EnlistmentInformationLength, sizeof(UCHAR));
            }
            RtlCopyMemory(Buffer, EnlistmentInformation, EnlistmentInformationLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Buffer, TAG_TM);
            return Status;
        }
    }

    Status = ObReferenceObjectByHandle(EnlistmentHandle,
                                       ENLISTMENT_SET_INFORMATION,
                                       TmEnlistmentObjectType,
                                       PreviousMode,
                                       (PVOID *)&Enlistment,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        if (Buffer != NULL)
        {
            ExFreePoolWithTag(Buffer, TAG_TM);
        }
        return Status;
    }

    TmpAcquireMutex(&Enlistment->Mutex);
    Old = Enlistment->RecoveryInformation;
    Enlistment->RecoveryInformation = Buffer;
    Enlistment->RecoveryInformationLength = EnlistmentInformationLength;
    TmpReleaseMutex(&Enlistment->Mutex);

    if (Old != NULL)
    {
        ExFreePoolWithTag(Old, TAG_TM);
    }
    ObDereferenceObject(Enlistment);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtRecoverEnlistment(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PVOID EnlistmentKey)
{
    PKENLISTMENT Enlistment;
    NTSTATUS Status;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(EnlistmentHandle,
                                       ENLISTMENT_RECOVER,
                                       TmEnlistmentObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Enlistment,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmRecoverEnlistment(Enlistment, EnlistmentKey);
    ObDereferenceObject(Enlistment);
    return Status;
}

typedef
NTSTATUS
(NTAPI *PTMP_ENLISTMENT_ROUTINE)(
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock);

static
NTSTATUS
TmpEnlistmentCall(
    _In_ HANDLE EnlistmentHandle,
    _In_ ACCESS_MASK Access,
    _In_opt_ PLARGE_INTEGER TmVirtualClock,
    _In_ PTMP_ENLISTMENT_ROUTINE Routine)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER Clock;
    PKENLISTMENT Enlistment;
    NTSTATUS Status = STATUS_SUCCESS;

    if (TmVirtualClock != NULL)
    {
        _SEH2_TRY
        {
            if (PreviousMode != KernelMode)
            {
                ProbeForRead(TmVirtualClock, sizeof(LARGE_INTEGER), sizeof(ULONG));
            }
            Clock = *TmVirtualClock;
            TmVirtualClock = &Clock;
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
    }

    Status = ObReferenceObjectByHandle(EnlistmentHandle,
                                       Access,
                                       TmEnlistmentObjectType,
                                       PreviousMode,
                                       (PVOID *)&Enlistment,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = Routine(Enlistment, TmVirtualClock);
    ObDereferenceObject(Enlistment);
    return Status;
}

NTSTATUS
NTAPI
NtPrePrepareComplete(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUBORDINATE_RIGHTS, TmVirtualClock, TmPrePrepareComplete);
}

NTSTATUS
NTAPI
NtPrepareComplete(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUBORDINATE_RIGHTS, TmVirtualClock, TmPrepareComplete);
}

NTSTATUS
NTAPI
NtCommitComplete(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUBORDINATE_RIGHTS, TmVirtualClock, TmCommitComplete);
}

NTSTATUS
NTAPI
NtRollbackComplete(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUBORDINATE_RIGHTS, TmVirtualClock, TmRollbackComplete);
}

NTSTATUS
NTAPI
NtReadOnlyEnlistment(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUBORDINATE_RIGHTS, TmVirtualClock, TmReadOnlyEnlistment);
}

NTSTATUS
NTAPI
NtSinglePhaseReject(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUBORDINATE_RIGHTS, TmVirtualClock, TmSinglePhaseReject);
}

NTSTATUS
NTAPI
NtRollbackEnlistment(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUBORDINATE_RIGHTS, TmVirtualClock, TmRollbackEnlistment);
}

NTSTATUS
NTAPI
NtPrePrepareEnlistment(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUPERIOR_RIGHTS, TmVirtualClock, TmPrePrepareEnlistment);
}

NTSTATUS
NTAPI
NtPrepareEnlistment(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUPERIOR_RIGHTS, TmVirtualClock, TmPrepareEnlistment);
}

NTSTATUS
NTAPI
NtCommitEnlistment(
    _In_ HANDLE EnlistmentHandle,
    _In_opt_ PLARGE_INTEGER TmVirtualClock)
{
    return TmpEnlistmentCall(EnlistmentHandle, ENLISTMENT_SUPERIOR_RIGHTS, TmVirtualClock, TmCommitEnlistment);
}
