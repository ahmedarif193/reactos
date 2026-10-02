/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel Transaction Manager: transaction objects and commit processing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <internal/tm.h>
#define NDEBUG
#include <debug.h>

#define TMP_INLINE_DELIVERIES 8

typedef struct _TMP_DELIVERY
{
    PKENLISTMENT Enlistment;
    ULONG Notification;
    KENLISTMENT_STATE State;
} TMP_DELIVERY, *PTMP_DELIVERY;

typedef struct _TMP_PUMP
{
    LIST_ENTRY Link;
    PKTRANSACTION Transaction;
    PKTHREAD Thread;
} TMP_PUMP, *PTMP_PUMP;

static LIST_ENTRY TmpPumpList = { &TmpPumpList, &TmpPumpList };
static KSPIN_LOCK TmpPumpLock;

static
VOID
TmpRecordHistory(
    _Inout_ PKTRANSACTION Transaction,
    _In_ ULONG RecordType,
    _In_ ULONG Payload)
{
    ULONG Index = Transaction->TransactionHistoryCount % RTL_NUMBER_OF(Transaction->TransactionHistory);

    Transaction->TransactionHistory[Index].RecordType = RecordType;
    Transaction->TransactionHistory[Index].Payload = Payload;
    Transaction->TransactionHistoryCount++;
}

static
BOOLEAN
TmpEnlistmentParticipates(
    _In_ PKENLISTMENT Enlistment)
{
    switch (Enlistment->State)
    {
        case KEnlistmentReadOnly:
        case KEnlistmentAborted:
        case KEnlistmentCommitted:
        case KEnlistmentForgotten:
            return FALSE;

        default:
            return TRUE;
    }
}

static
ULONG
TmpCollectPhase(
    _Inout_ PKTRANSACTION Transaction,
    _In_ KENLISTMENT_STATE FromState,
    _In_ ULONG Notification,
    _In_ KENLISTMENT_STATE PendingState,
    _In_ KENLISTMENT_STATE DoneState,
    _Out_writes_(Capacity) PTMP_DELIVERY Deliveries,
    _In_ ULONG Capacity)
{
    PKENLISTMENT Enlistment;
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    for (Entry = Transaction->EnlistmentHead.Flink;
         Entry != &Transaction->EnlistmentHead;
         Entry = Entry->Flink)
    {
        Enlistment = CONTAINING_RECORD(Entry, KENLISTMENT, NextSameTx);

        if (Notification == TRANSACTION_NOTIFY_ROLLBACK)
        {
            if (!TmpEnlistmentParticipates(Enlistment) || Enlistment->State == KEnlistmentAborting)
            {
                continue;
            }
        }
        else if (Enlistment->State != FromState)
        {
            continue;
        }

        if ((Enlistment->NotificationMask & Notification) &&
            !(Enlistment->Flags & KENLISTMENT_FLAG_CLOSED) &&
            Count < Capacity &&
            ObReferenceObjectSafe(Enlistment))
        {
            Enlistment->State = PendingState;
            Deliveries[Count].Enlistment = Enlistment;
            Deliveries[Count].Notification = Notification;
            Deliveries[Count].State = PendingState;
            Count++;
        }
        else
        {
            Enlistment->State = DoneState;
        }
    }

    return Count;
}

static
PKENLISTMENT
TmpSinglePhaseCandidate(
    _In_ PKTRANSACTION Transaction)
{
    PKENLISTMENT Enlistment, Candidate = NULL;
    PLIST_ENTRY Entry;

    if (Transaction->Flags & KTRANSACTION_FLAG_NO_SINGLE_PHASE)
    {
        return NULL;
    }

    for (Entry = Transaction->EnlistmentHead.Flink;
         Entry != &Transaction->EnlistmentHead;
         Entry = Entry->Flink)
    {
        Enlistment = CONTAINING_RECORD(Entry, KENLISTMENT, NextSameTx);
        if (Enlistment->State != KEnlistmentActive)
        {
            continue;
        }
        if (Candidate != NULL ||
            !(Enlistment->NotificationMask & TRANSACTION_NOTIFY_SINGLE_PHASE_COMMIT) ||
            (Enlistment->Flags & KENLISTMENT_FLAG_CLOSED))
        {
            return NULL;
        }
        Candidate = Enlistment;
    }

    return Candidate;
}

static
VOID
TmpCompleteTransaction(
    _Inout_ PKTRANSACTION Transaction)
{
    BOOLEAN Cancelled = FALSE;

    if (Transaction->Flags & KTRANSACTION_FLAG_COMPLETED)
    {
        return;
    }

    Transaction->Flags |= KTRANSACTION_FLAG_COMPLETED;
    if (Transaction->Flags & KTRANSACTION_FLAG_TIMER)
    {
        Cancelled = KeCancelTimer(&Transaction->RollbackTimer);
        if (Cancelled)
        {
            Transaction->Flags &= ~KTRANSACTION_FLAG_TIMER;
        }
    }

    KeSetEvent(&Transaction->OutcomeEvent, IO_NO_INCREMENT, FALSE);

    if (Cancelled)
    {
        ObDereferenceObject(Transaction);
    }
}

static
BOOLEAN
TmpOwnsPump(
    _In_ PKTRANSACTION Transaction)
{
    PKTHREAD Thread = KeGetCurrentThread();
    BOOLEAN Found = FALSE;
    PLIST_ENTRY Link;
    PTMP_PUMP Pump;
    KIRQL OldIrql;

    KeAcquireSpinLock(&TmpPumpLock, &OldIrql);
    for (Link = TmpPumpList.Flink; Link != &TmpPumpList; Link = Link->Flink)
    {
        Pump = CONTAINING_RECORD(Link, TMP_PUMP, Link);
        if (Pump->Transaction == Transaction && Pump->Thread == Thread)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&TmpPumpLock, OldIrql);
    return Found;
}

BOOLEAN
TmpPumpTransaction(
    _Inout_ PKTRANSACTION Transaction)
{
    TMP_DELIVERY Inline[TMP_INLINE_DELIVERIES];
    PTMP_DELIVERY Deliveries;
    PKENLISTMENT Candidate;
    ULONG Capacity, Count, Index;
    BOOLEAN Queued = FALSE, Nested = FALSE, Stale;
    TMP_PUMP Pump;
    KIRQL OldIrql;

    TmpAcquireMutex(&Transaction->Mutex);
    if (Transaction->Flags & KTRANSACTION_FLAG_PUMPING)
    {
        if (!TmpOwnsPump(Transaction))
        {
            TmpReleaseMutex(&Transaction->Mutex);
            return FALSE;
        }
        Nested = TRUE;
    }
    Transaction->Flags |= KTRANSACTION_FLAG_PUMPING;

    Pump.Transaction = Transaction;
    Pump.Thread = KeGetCurrentThread();
    KeAcquireSpinLock(&TmpPumpLock, &OldIrql);
    InsertHeadList(&TmpPumpList, &Pump.Link);
    KeReleaseSpinLock(&TmpPumpLock, OldIrql);

    for (;;)
    {
        Count = 0;
        Deliveries = Inline;
        Capacity = TMP_INLINE_DELIVERIES;

        if (Transaction->EnlistmentCount > TMP_INLINE_DELIVERIES)
        {
            Deliveries = ExAllocatePoolWithTag(NonPagedPool,
                                               Transaction->EnlistmentCount * sizeof(TMP_DELIVERY),
                                               TAG_TM);
            if (Deliveries == NULL)
            {
                Deliveries = Inline;
            }
            else
            {
                Capacity = Transaction->EnlistmentCount;
            }
        }

        if ((Transaction->Flags & KTRANSACTION_FLAG_ROLLBACK_REQUESTED) &&
            Transaction->State != KTransactionCommitted &&
            Transaction->State != KTransactionAborted)
        {
            Transaction->State = KTransactionAborted;
            Transaction->Outcome = KTxOutcomeAborted;
            Transaction->Flags &= ~KTRANSACTION_FLAG_SINGLE_PHASE;
            TmpRecordHistory(Transaction, KTransactionAborted, 0);
            TmpCompleteTransaction(Transaction);
            Count = TmpCollectPhase(Transaction,
                                    KEnlistmentUninitialized,
                                    TRANSACTION_NOTIFY_ROLLBACK,
                                    KEnlistmentAborting,
                                    KEnlistmentAborted,
                                    Deliveries,
                                    Capacity);
            Transaction->PendingResponses = Count;
        }
        else if (Transaction->PendingResponses != 0)
        {
            if (Deliveries != Inline)
            {
                ExFreePoolWithTag(Deliveries, TAG_TM);
            }
            break;
        }
        else
        {
            switch (Transaction->State)
            {
                case KTransactionActive:
                    if (!(Transaction->Flags & KTRANSACTION_FLAG_COMMIT_REQUESTED))
                    {
                        goto Idle;
                    }

                    Candidate = TmpSinglePhaseCandidate(Transaction);
                    if (Candidate != NULL && ObReferenceObjectSafe(Candidate))
                    {
                        Transaction->Flags |= KTRANSACTION_FLAG_SINGLE_PHASE;
                        Candidate->State = KEnlistmentCommitRequested;
                        Deliveries[0].Enlistment = Candidate;
                        Deliveries[0].Notification = TRANSACTION_NOTIFY_SINGLE_PHASE_COMMIT;
                        Deliveries[0].State = KEnlistmentCommitRequested;
                        Count = 1;
                        Transaction->State = KTransactionPreparing;
                    }
                    else
                    {
                        Transaction->State = KTransactionPrePreparing;
                        Count = TmpCollectPhase(Transaction,
                                                KEnlistmentActive,
                                                TRANSACTION_NOTIFY_PREPREPARE,
                                                KEnlistmentPrePreparing,
                                                KEnlistmentPrePrepared,
                                                Deliveries,
                                                Capacity);
                    }
                    Transaction->PendingResponses = Count;
                    TmpRecordHistory(Transaction, Transaction->State, Count);
                    break;

                case KTransactionPrePreparing:
                    Transaction->State = KTransactionPrePrepared;
                    break;

                case KTransactionPrePrepared:
                    Transaction->State = KTransactionPreparing;
                    Count = TmpCollectPhase(Transaction,
                                            KEnlistmentPrePrepared,
                                            TRANSACTION_NOTIFY_PREPARE,
                                            KEnlistmentPreparing,
                                            KEnlistmentPrepared,
                                            Deliveries,
                                            Capacity);
                    Transaction->PendingResponses = Count;
                    TmpRecordHistory(Transaction, KTransactionPreparing, Count);
                    break;

                case KTransactionPreparing:
                    if (Transaction->Flags & KTRANSACTION_FLAG_SINGLE_PHASE)
                    {
                        Transaction->Flags &= ~KTRANSACTION_FLAG_SINGLE_PHASE;
                        if (Transaction->Outcome == KTxOutcomeCommitted)
                        {
                            Transaction->State = KTransactionCommitted;
                        }
                        else
                        {
                            Transaction->Flags |= KTRANSACTION_FLAG_NO_SINGLE_PHASE;
                            Transaction->State = KTransactionActive;
                        }
                    }
                    else
                    {
                        Transaction->State = KTransactionPrepared;
                    }
                    break;

                case KTransactionPrepared:
                    Transaction->Outcome = KTxOutcomeCommitted;
                    Transaction->State = KTransactionCommitted;
                    Count = TmpCollectPhase(Transaction,
                                            KEnlistmentPrepared,
                                            TRANSACTION_NOTIFY_COMMIT,
                                            KEnlistmentCommittedNotify,
                                            KEnlistmentCommitted,
                                            Deliveries,
                                            Capacity);
                    Transaction->PendingResponses = Count;
                    TmpRecordHistory(Transaction, KTransactionCommitted, Count);
                    break;

                case KTransactionCommitted:
                    TmpCompleteTransaction(Transaction);
                    goto Idle;

                default:
                    goto Idle;
            }
        }

        TmpReleaseMutex(&Transaction->Mutex);

        for (Index = 0; Index < Count; Index++)
        {
            TmpAcquireMutex(&Transaction->Mutex);
            Stale = (Deliveries[Index].Enlistment->State != Deliveries[Index].State);
            TmpReleaseMutex(&Transaction->Mutex);

            if (!Stale && TmpDeliverNotification(Deliveries[Index].Enlistment, Deliveries[Index].Notification))
            {
                Queued = TRUE;
            }
            ObDereferenceObject(Deliveries[Index].Enlistment);
        }
        if (Deliveries != Inline)
        {
            ExFreePoolWithTag(Deliveries, TAG_TM);
        }

        TmpAcquireMutex(&Transaction->Mutex);
        continue;

Idle:
        if (Deliveries != Inline)
        {
            ExFreePoolWithTag(Deliveries, TAG_TM);
        }
        break;
    }

    KeAcquireSpinLock(&TmpPumpLock, &OldIrql);
    RemoveEntryList(&Pump.Link);
    KeReleaseSpinLock(&TmpPumpLock, OldIrql);

    if (!Nested)
    {
        Transaction->Flags &= ~KTRANSACTION_FLAG_PUMPING;
    }
    TmpReleaseMutex(&Transaction->Mutex);
    return Queued;
}

NTSTATUS
TmpRequestRollback(
    _Inout_ PKTRANSACTION Transaction)
{
    NTSTATUS Status = STATUS_SUCCESS;

    TmpAcquireMutex(&Transaction->Mutex);
    if (Transaction->State == KTransactionCommitted)
    {
        Status = STATUS_TRANSACTION_ALREADY_COMMITTED;
    }
    else if (Transaction->State == KTransactionAborted)
    {
        Status = STATUS_SUCCESS;
    }
    else if (Transaction->State == KTransactionPrepared ||
             (Transaction->Flags & KTRANSACTION_FLAG_SINGLE_PHASE))
    {
        Status = STATUS_TRANSACTION_REQUEST_NOT_VALID;
    }
    else
    {
        Transaction->Flags |= KTRANSACTION_FLAG_ROLLBACK_REQUESTED;
    }
    TmpReleaseMutex(&Transaction->Mutex);

    if (NT_SUCCESS(Status) && TmpPumpTransaction(Transaction))
    {
        Status = STATUS_PENDING;
    }
    return Status;
}

static
NTSTATUS
TmpWaitOutcome(
    _In_ PKTRANSACTION Transaction,
    _In_ BOOLEAN Commit)
{
    KeWaitForSingleObject(&Transaction->OutcomeEvent, Executive, KernelMode, FALSE, NULL);

    if (Commit)
    {
        return (Transaction->Outcome == KTxOutcomeCommitted) ? STATUS_SUCCESS : STATUS_TRANSACTION_ALREADY_ABORTED;
    }
    return (Transaction->Outcome == KTxOutcomeAborted) ? STATUS_SUCCESS : STATUS_TRANSACTION_ALREADY_COMMITTED;
}

NTSTATUS
NTAPI
TmCommitTransaction(
    _In_ PKTRANSACTION Transaction,
    _In_ BOOLEAN Wait)
{
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Queued;

    TmpAcquireMutex(&Transaction->Mutex);
    if (Transaction->SuperiorEnlistment != NULL)
    {
        Status = STATUS_TRANSACTION_SUPERIOR_EXISTS;
    }
    else if (Transaction->State == KTransactionAborted ||
             (Transaction->Flags & KTRANSACTION_FLAG_ROLLBACK_REQUESTED))
    {
        Status = STATUS_TRANSACTION_ALREADY_ABORTED;
    }
    else if (Transaction->State == KTransactionCommitted)
    {
        Status = STATUS_TRANSACTION_ALREADY_COMMITTED;
    }
    else if (Transaction->Flags & KTRANSACTION_FLAG_COMMIT_REQUESTED)
    {
        Status = STATUS_TRANSACTION_REQUEST_NOT_VALID;
    }
    else
    {
        Transaction->Flags |= KTRANSACTION_FLAG_COMMIT_REQUESTED;
    }
    TmpReleaseMutex(&Transaction->Mutex);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Queued = TmpPumpTransaction(Transaction);
    if (Wait || Transaction->Outcome == KTxOutcomeAborted)
    {
        return TmpWaitOutcome(Transaction, TRUE);
    }
    return Queued ? STATUS_PENDING : STATUS_SUCCESS;
}

NTSTATUS
NTAPI
TmRollbackTransaction(
    _In_ PKTRANSACTION Transaction,
    _In_ BOOLEAN Wait)
{
    NTSTATUS Status;

    Status = TmpRequestRollback(Transaction);
    if (!NT_SUCCESS(Status) || !Wait)
    {
        return Status;
    }
    return TmpWaitOutcome(Transaction, FALSE);
}

VOID
NTAPI
TmGetTransactionId(
    _In_ PKTRANSACTION Transaction,
    _Out_ PUOW TransactionId)
{
    *TransactionId = Transaction->UOW;
}

BOOLEAN
NTAPI
TmIsTransactionActive(
    _In_ PKTRANSACTION Transaction)
{
    return Transaction->State == KTransactionActive &&
           !(Transaction->Flags & (KTRANSACTION_FLAG_COMMIT_REQUESTED | KTRANSACTION_FLAG_ROLLBACK_REQUESTED));
}

VOID
TmpBindTransaction(
    _Inout_ PKTRANSACTION Transaction,
    _In_ PKTM Tm)
{
    TmpAcquireMutex(&Transaction->Mutex);
    if (Transaction->Tm == NULL)
    {
        ObReferenceObject(Tm);
        Transaction->Tm = Tm;
        (VOID)TmpInsertNamespace(&Tm->Transactions, Transaction);
    }
    TmpReleaseMutex(&Transaction->Mutex);
}

static
VOID
NTAPI
TmpRollbackWorker(
    _In_ PVOID Parameter)
{
    PKTRANSACTION Transaction = Parameter;

    (VOID)TmpRequestRollback(Transaction);
    ObDereferenceObject(Transaction);
}

static
VOID
NTAPI
TmpRollbackDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PKTRANSACTION Transaction = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ExInitializeWorkItem(&Transaction->RollbackWorkItem, TmpRollbackWorker, Transaction);
    ExQueueWorkItem(&Transaction->RollbackWorkItem, DelayedWorkQueue);
}

static
VOID
TmpSetTimeout(
    _Inout_ PKTRANSACTION Transaction,
    _In_ LARGE_INTEGER Timeout)
{
    TmpAcquireMutex(&Transaction->Mutex);
    Transaction->Timeout = Timeout;
    if (Timeout.QuadPart != 0 &&
        !(Transaction->Flags & (KTRANSACTION_FLAG_TIMER | KTRANSACTION_FLAG_COMPLETED)))
    {
        Transaction->Flags |= KTRANSACTION_FLAG_TIMER;
        ObReferenceObject(Transaction);
        KeSetTimer(&Transaction->RollbackTimer, Timeout, &Transaction->RollbackDpc);
    }
    TmpReleaseMutex(&Transaction->Mutex);
}

VOID
NTAPI
TmpCloseTransaction(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    PKTRANSACTION Transaction = Object;

    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);

    if (SystemHandleCount != 1 || Transaction->cookie != KTRANSACTION_COOKIE)
    {
        return;
    }

    if (Transaction->State != KTransactionCommitted &&
        Transaction->State != KTransactionAborted &&
        !(Transaction->Flags & KTRANSACTION_FLAG_COMMIT_REQUESTED))
    {
        (VOID)TmpRequestRollback(Transaction);
    }
}

VOID
NTAPI
TmpDeleteTransaction(
    _In_ PVOID Object)
{
    PKTRANSACTION Transaction = Object;

    if (Transaction->cookie != KTRANSACTION_COOKIE)
    {
        return;
    }

    TmpRemoveNamespace(&TmpTransactionNamespace, Transaction);
    if (Transaction->Tm != NULL)
    {
        TmpRemoveNamespace(&Transaction->Tm->Transactions, Transaction);
        ObDereferenceObject(Transaction->Tm);
    }
    if (Transaction->Description.Buffer != NULL)
    {
        ExFreePoolWithTag(Transaction->Description.Buffer, TAG_TM);
    }
    Transaction->cookie = 0;
}

NTSTATUS
NTAPI
NtCreateTransaction(
    _Out_ PHANDLE TransactionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ LPGUID Uow,
    _In_opt_ HANDLE TmHandle,
    _In_opt_ ULONG CreateOptions,
    _In_opt_ ULONG IsolationLevel,
    _In_opt_ ULONG IsolationFlags,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_opt_ PUNICODE_STRING Description)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER SafeTimeout;
    UNICODE_STRING SafeDescription;
    PKTRANSACTION Transaction;
    BOOLEAN UowPresent;
    PKTM Tm = NULL;
    HANDLE Handle;
    NTSTATUS Status;
    GUID SafeUow;
    PAGED_CODE();

    if (TmpComponentFiltered(PreviousMode))
    {
        return STATUS_ACCESS_DENIED;
    }

    if (DesiredAccess == 0 || (CreateOptions & ~TRANSACTION_DO_NOT_PROMOTE))
    {
        return STATUS_INVALID_PARAMETER;
    }

    SafeTimeout.QuadPart = 0;
    Status = TmpCaptureGuid(PreviousMode, Uow, &SafeUow, &UowPresent);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Timeout != NULL)
    {
        _SEH2_TRY
        {
            if (PreviousMode != KernelMode)
            {
                ProbeForRead(Timeout, sizeof(LARGE_INTEGER), sizeof(ULONG));
            }
            SafeTimeout = *Timeout;
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

    Status = TmpCaptureDescription(PreviousMode, Description, MAX_TRANSACTION_DESCRIPTION_LENGTH, &SafeDescription);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (TmHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(TmHandle,
                                           TRANSACTIONMANAGER_QUERY_INFORMATION,
                                           TmTransactionManagerObjectType,
                                           PreviousMode,
                                           (PVOID *)&Tm,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            goto Fail;
        }
    }

    Status = ObCreateObject(PreviousMode,
                            TmTransactionObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(KTRANSACTION),
                            0,
                            0,
                            (PVOID *)&Transaction);
    if (!NT_SUCCESS(Status))
    {
        goto Fail;
    }

    RtlZeroMemory(Transaction, sizeof(*Transaction));
    KeInitializeEvent(&Transaction->OutcomeEvent, NotificationEvent, FALSE);
    KeInitializeMutex(&Transaction->Mutex, 0);
    InitializeListHead(&Transaction->EnlistmentHead);
    InitializeListHead(&Transaction->PromotedEntry);
    InitializeListHead(&Transaction->LsnOrderedEntry);
    KeInitializeMutex(&Transaction->DTCPrivateInformationMutex, 0);
    KeInitializeEvent(&Transaction->PromotionCompletedEvent, NotificationEvent, FALSE);
    KeInitializeTimer(&Transaction->RollbackTimer);
    KeInitializeDpc(&Transaction->RollbackDpc, TmpRollbackDpc, Transaction);
    Transaction->State = KTransactionActive;
    Transaction->Outcome = KTxOutcomeUndetermined;
    Transaction->IsolationLevel = IsolationLevel;
    Transaction->IsolationFlags = IsolationFlags;
    Transaction->Description = SafeDescription;
    RtlInitEmptyUnicodeString(&SafeDescription, NULL, 0);

    if (UowPresent)
    {
        Transaction->UOW = SafeUow;
    }
    else
    {
        Status = ExUuidCreate(&Transaction->UOW);
    }

    if (NT_SUCCESS(Status))
    {
        Transaction->cookie = KTRANSACTION_COOKIE;
        Status = TmpInsertNamespace(&TmpTransactionNamespace, Transaction);
        if (!NT_SUCCESS(Status))
        {
            Transaction->cookie = 0;
        }
    }
    if (!NT_SUCCESS(Status))
    {
        if (Transaction->Description.Buffer != NULL)
        {
            ExFreePoolWithTag(Transaction->Description.Buffer, TAG_TM);
            Transaction->Description.Buffer = NULL;
        }
        ObDereferenceObject(Transaction);
        goto Fail;
    }

    if (Tm != NULL)
    {
        TmpBindTransaction(Transaction, Tm);
    }
    if (SafeTimeout.QuadPart != 0)
    {
        TmpSetTimeout(Transaction, SafeTimeout);
    }

    Status = ObInsertObject(Transaction, NULL, DesiredAccess, 0, NULL, &Handle);
    if (NT_SUCCESS(Status))
    {
        Status = TmpWriteHandle(PreviousMode, TransactionHandle, Handle);
        if (!NT_SUCCESS(Status))
        {
            ObCloseHandle(Handle, PreviousMode);
        }
    }

Fail:
    if (Tm != NULL)
    {
        ObDereferenceObject(Tm);
    }
    if (SafeDescription.Buffer != NULL)
    {
        ExFreePoolWithTag(SafeDescription.Buffer, TAG_TM);
    }
    return Status;
}

NTSTATUS
NTAPI
NtOpenTransaction(
    _Out_ PHANDLE TransactionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ LPGUID Uow,
    _In_opt_ HANDLE TmHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKTRANSACTION Transaction;
    BOOLEAN Present;
    HANDLE Handle;
    NTSTATUS Status;
    GUID Guid;
    PKTM Tm;
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ObjectAttributes);

    if (TmpComponentFiltered(PreviousMode))
    {
        return STATUS_ACCESS_DENIED;
    }

    Status = TmpCaptureGuid(PreviousMode, Uow, &Guid, &Present);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (DesiredAccess == 0 || !Present)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (TmHandle != NULL)
    {
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
        Transaction = TmpLookupNamespace(&Tm->Transactions, &Guid);
        ObDereferenceObject(Tm);
    }
    else
    {
        Transaction = TmpLookupNamespace(&TmpTransactionNamespace, &Guid);
    }

    if (Transaction == NULL)
    {
        return STATUS_TRANSACTION_NOT_FOUND;
    }

    Status = ObOpenObjectByPointer(Transaction,
                                   (PreviousMode == KernelMode) ? OBJ_KERNEL_HANDLE : 0,
                                   NULL,
                                   DesiredAccess,
                                   TmTransactionObjectType,
                                   PreviousMode,
                                   &Handle);
    ObDereferenceObject(Transaction);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmpWriteHandle(PreviousMode, TransactionHandle, Handle);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(Handle, PreviousMode);
    }
    return Status;
}

static
ULONG
TmpPublicOutcome(
    _In_ PKTRANSACTION Transaction)
{
    switch (Transaction->Outcome)
    {
        case KTxOutcomeCommitted: return TransactionOutcomeCommitted;
        case KTxOutcomeAborted: return TransactionOutcomeAborted;
        default: return TransactionOutcomeUndetermined;
    }
}

NTSTATUS
NTAPI
NtQueryInformationTransaction(
    _In_ HANDLE TransactionHandle,
    _In_ TRANSACTION_INFORMATION_CLASS TransactionInformationClass,
    _Out_writes_bytes_(TransactionInformationLength) PVOID TransactionInformation,
    _In_ ULONG TransactionInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PTRANSACTION_PROPERTIES_INFORMATION Properties;
    PTRANSACTION_ENLISTMENTS_INFORMATION Enlistments;
    TRANSACTION_BASIC_INFORMATION Basic;
    PKTRANSACTION Transaction;
    PKENLISTMENT Enlistment;
    PLIST_ENTRY Entry;
    ULONG Length, Index;
    NTSTATUS Status;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(TransactionHandle,
                                       TRANSACTION_QUERY_INFORMATION,
                                       TmTransactionObjectType,
                                       PreviousMode,
                                       (PVOID *)&Transaction,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    TmpAcquireMutex(&Transaction->Mutex);
    switch (TransactionInformationClass)
    {
        case TransactionBasicInformation:
            if (TransactionInformationLength < sizeof(Basic))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            Basic.TransactionId = Transaction->UOW;
            Basic.State = (Transaction->State == KTransactionInDoubt) ? TransactionStateIndoubt : TransactionStateNormal;
            Basic.Outcome = TmpPublicOutcome(Transaction);
            Status = TmpReturnInformation(PreviousMode,
                                          TransactionInformation,
                                          TransactionInformationLength,
                                          ReturnLength,
                                          &Basic,
                                          sizeof(Basic),
                                          sizeof(Basic));
            break;

        case TransactionPropertiesInformation:
            if (TransactionInformationLength < FIELD_OFFSET(TRANSACTION_PROPERTIES_INFORMATION, Description))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            Length = FIELD_OFFSET(TRANSACTION_PROPERTIES_INFORMATION, Description) + Transaction->Description.Length;
            Properties = ExAllocatePoolWithTag(PagedPool, Length, TAG_TM);
            if (Properties == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            Properties->IsolationLevel = Transaction->IsolationLevel;
            Properties->IsolationFlags = Transaction->IsolationFlags;
            Properties->Timeout = Transaction->Timeout;
            Properties->Outcome = TmpPublicOutcome(Transaction);
            Properties->DescriptionLength = Transaction->Description.Length;
            RtlCopyMemory(Properties->Description, Transaction->Description.Buffer, Transaction->Description.Length);
            Status = TmpReturnInformation(PreviousMode,
                                          TransactionInformation,
                                          TransactionInformationLength,
                                          ReturnLength,
                                          Properties,
                                          Length,
                                          FIELD_OFFSET(TRANSACTION_PROPERTIES_INFORMATION, Description));
            ExFreePoolWithTag(Properties, TAG_TM);
            break;

        case TransactionEnlistmentInformation:
            if (TransactionInformationLength < FIELD_OFFSET(TRANSACTION_ENLISTMENTS_INFORMATION, EnlistmentPair))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            Length = FIELD_OFFSET(TRANSACTION_ENLISTMENTS_INFORMATION, EnlistmentPair) +
                     Transaction->EnlistmentCount * sizeof(TRANSACTION_ENLISTMENT_PAIR);
            Enlistments = ExAllocatePoolWithTag(PagedPool, Length, TAG_TM);
            if (Enlistments == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            Index = 0;
            for (Entry = Transaction->EnlistmentHead.Flink;
                 Entry != &Transaction->EnlistmentHead && Index < Transaction->EnlistmentCount;
                 Entry = Entry->Flink)
            {
                Enlistment = CONTAINING_RECORD(Entry, KENLISTMENT, NextSameTx);
                Enlistments->EnlistmentPair[Index].EnlistmentId = Enlistment->EnlistmentId;
                Enlistments->EnlistmentPair[Index].ResourceManagerId = Enlistment->ResourceManager->RmId;
                Index++;
            }
            Enlistments->NumberOfEnlistments = Index;
            Status = TmpReturnInformation(PreviousMode,
                                          TransactionInformation,
                                          TransactionInformationLength,
                                          ReturnLength,
                                          Enlistments,
                                          Length,
                                          FIELD_OFFSET(TRANSACTION_ENLISTMENTS_INFORMATION, EnlistmentPair));
            ExFreePoolWithTag(Enlistments, TAG_TM);
            break;

        case TransactionSuperiorEnlistmentInformation:
            Status = STATUS_TRANSACTION_NO_SUPERIOR;
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }
    TmpReleaseMutex(&Transaction->Mutex);

    ObDereferenceObject(Transaction);
    return Status;
}

NTSTATUS
NTAPI
NtSetInformationTransaction(
    _In_ HANDLE TransactionHandle,
    _In_ TRANSACTION_INFORMATION_CLASS TransactionInformationClass,
    _In_reads_bytes_(TransactionInformationLength) PVOID TransactionInformation,
    _In_ ULONG TransactionInformationLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    TRANSACTION_PROPERTIES_INFORMATION Properties;
    PKTRANSACTION Transaction;
    NTSTATUS Status;
    PAGED_CODE();

    if (TransactionInformationClass != TransactionPropertiesInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }
    if (TransactionInformationLength < FIELD_OFFSET(TRANSACTION_PROPERTIES_INFORMATION, Description))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(TransactionInformation, TransactionInformationLength, sizeof(ULONG));
        }
        RtlCopyMemory(&Properties,
                      TransactionInformation,
                      FIELD_OFFSET(TRANSACTION_PROPERTIES_INFORMATION, Description));
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

    Status = ObReferenceObjectByHandle(TransactionHandle,
                                       TRANSACTION_SET_INFORMATION,
                                       TmTransactionObjectType,
                                       PreviousMode,
                                       (PVOID *)&Transaction,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    TmpAcquireMutex(&Transaction->Mutex);
    Transaction->IsolationLevel = Properties.IsolationLevel;
    Transaction->IsolationFlags = Properties.IsolationFlags;
    TmpReleaseMutex(&Transaction->Mutex);

    if (Properties.Timeout.QuadPart != 0)
    {
        TmpSetTimeout(Transaction, Properties.Timeout);
    }

    ObDereferenceObject(Transaction);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtCommitTransaction(
    _In_ HANDLE TransactionHandle,
    _In_ BOOLEAN Wait)
{
    PKTRANSACTION Transaction;
    NTSTATUS Status;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(TransactionHandle,
                                       TRANSACTION_COMMIT,
                                       TmTransactionObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Transaction,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmCommitTransaction(Transaction, Wait);
    ObDereferenceObject(Transaction);
    return Status;
}

NTSTATUS
NTAPI
NtRollbackTransaction(
    _In_ HANDLE TransactionHandle,
    _In_ BOOLEAN Wait)
{
    PKTRANSACTION Transaction;
    NTSTATUS Status;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(TransactionHandle,
                                       TRANSACTION_ROLLBACK,
                                       TmTransactionObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Transaction,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = TmRollbackTransaction(Transaction, Wait);
    ObDereferenceObject(Transaction);
    return Status;
}
