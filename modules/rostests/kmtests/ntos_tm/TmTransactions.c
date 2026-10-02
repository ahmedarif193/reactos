/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite kernel transaction manager (transactions, resource managers, enlistments)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define MAX_RECORDS 8

typedef enum _ENLISTMENT_MODE
{
    ModeComplete,
    ModeRollbackOnPrepare,
    ModeReadOnlyOnPrePrepare,
    ModeRejectSinglePhase,
    ModePendPrepare,
    ModeFailPrepare,
    ModeReturnPending,
    ModeDeferPending,
    ModeDeferSuccess
} ENLISTMENT_MODE;

typedef struct _TEST_ENLISTMENT
{
    HANDLE Handle;
    ENLISTMENT_MODE Mode;
    ULONG Count;
    ULONG Notifications[MAX_RECORDS];
    NTSTATUS Responses[MAX_RECORDS];
    PKENLISTMENT Object;
    PVOID RmContext;
    KIRQL Irql;
    PETHREAD Thread;
    BOOLEAN CloseInCallback;
} TEST_ENLISTMENT, *PTEST_ENLISTMENT;

static HANDLE TmHandle;
static HANDLE RmHandle;
static PKRESOURCEMANAGER RmObject;
static GUID RmGuid;
static ULONG RmKey;

static
NTSTATUS
NTAPI
Notification(
    _In_ PKENLISTMENT EnlistmentObject,
    _In_ PVOID RMContext,
    _In_ PVOID TransactionContext,
    _In_ ULONG TransactionNotification,
    _Inout_ PLARGE_INTEGER TmVirtualClock,
    _In_ ULONG ArgumentLength,
    _In_ PVOID Argument)
{
    PTEST_ENLISTMENT Test = TransactionContext;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    UNREFERENCED_PARAMETER(ArgumentLength);
    UNREFERENCED_PARAMETER(Argument);

    Index = Test->Count++;
    Test->Object = EnlistmentObject;
    Test->RmContext = RMContext;
    Test->Irql = KeGetCurrentIrql();
    Test->Thread = PsGetCurrentThread();
    if (Index >= MAX_RECORDS)
    {
        return STATUS_SUCCESS;
    }
    Test->Notifications[Index] = TransactionNotification;
    if (Test->Mode == ModeDeferPending)
    {
        return STATUS_PENDING;
    }
    if (Test->Mode == ModeDeferSuccess)
    {
        return STATUS_SUCCESS;
    }

    switch (TransactionNotification)
    {
        case TRANSACTION_NOTIFY_PREPREPARE:
            if (Test->Mode == ModeReadOnlyOnPrePrepare)
            {
                Status = TmReadOnlyEnlistment(EnlistmentObject, TmVirtualClock);
            }
            else
            {
                Status = TmPrePrepareComplete(EnlistmentObject, TmVirtualClock);
            }
            break;

        case TRANSACTION_NOTIFY_PREPARE:
            if (Test->Mode == ModeRollbackOnPrepare)
            {
                Status = TmRollbackEnlistment(EnlistmentObject, TmVirtualClock);
            }
            else if (Test->Mode == ModePendPrepare)
            {
                Test->Responses[Index] = STATUS_PENDING;
                return STATUS_PENDING;
            }
            else if (Test->Mode == ModeFailPrepare)
            {
                Test->Responses[Index] = STATUS_UNSUCCESSFUL;
                return STATUS_UNSUCCESSFUL;
            }
            else
            {
                Status = TmPrepareComplete(EnlistmentObject, TmVirtualClock);
            }
            break;

        case TRANSACTION_NOTIFY_SINGLE_PHASE_COMMIT:
            if (Test->Mode == ModeRejectSinglePhase)
            {
                Status = TmSinglePhaseReject(EnlistmentObject, TmVirtualClock);
            }
            else
            {
                Status = TmCommitComplete(EnlistmentObject, TmVirtualClock);
            }
            break;

        case TRANSACTION_NOTIFY_COMMIT:
            Status = TmCommitComplete(EnlistmentObject, TmVirtualClock);
            break;

        case TRANSACTION_NOTIFY_ROLLBACK:
            Status = TmRollbackComplete(EnlistmentObject, TmVirtualClock);
            break;

        default:
            break;
    }

    Test->Responses[Index] = Status;
    if (Test->CloseInCallback && Test->Handle != NULL)
    {
        ZwClose(Test->Handle);
        Test->Handle = NULL;
    }
    return Test->Mode == ModeReturnPending ? STATUS_PENDING : STATUS_SUCCESS;
}

static
NTSTATUS
CreateTransaction(
    _Out_ PHANDLE Transaction,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    return ZwCreateTransaction(Transaction,
                               TRANSACTION_ALL_ACCESS,
                               &ObjectAttributes,
                               NULL,
                               NULL,
                               0,
                               0,
                               0,
                               Timeout,
                               NULL);
}

static
NTSTATUS
Enlist(
    _In_ HANDLE ResourceManager,
    _In_ HANDLE Transaction,
    _In_ NOTIFICATION_MASK Mask,
    _In_ ENLISTMENT_MODE Mode,
    _Out_ PTEST_ENLISTMENT Test)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    RtlZeroMemory(Test, sizeof(*Test));
    Test->Mode = Mode;
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    return ZwCreateEnlistment(&Test->Handle,
                              ENLISTMENT_ALL_ACCESS,
                              ResourceManager,
                              Transaction,
                              &ObjectAttributes,
                              0,
                              Mask,
                              Test);
}

static
ULONG
QueryOutcome(
    _In_ HANDLE Transaction)
{
    TRANSACTION_BASIC_INFORMATION Basic;
    NTSTATUS Status;

    RtlZeroMemory(&Basic, sizeof(Basic));
    Status = ZwQueryInformationTransaction(Transaction,
                                           TransactionBasicInformation,
                                           &Basic,
                                           sizeof(Basic),
                                           NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return Basic.Outcome;
}

static
VOID
CloseEnlistment(
    _Inout_ PTEST_ENLISTMENT Test)
{
    if (Test->Handle != NULL)
    {
        ZwClose(Test->Handle);
        Test->Handle = NULL;
    }
}

typedef struct _CALLBACK_STATUS_CASE
{
    ENLISTMENT_MODE Mode;
    BOOLEAN Defer;
    NTSTATUS Delivered;
} CALLBACK_STATUS_CASE;

static
VOID
TestCallbackStatus(VOID)
{
    static CONST CALLBACK_STATUS_CASE Cases[] =
    {
        { ModeComplete, FALSE, STATUS_SUCCESS },
        { ModeReturnPending, FALSE, STATUS_PENDING },
        { ModeDeferPending, TRUE, STATUS_PENDING },
        { ModeDeferSuccess, TRUE, STATUS_SUCCESS },
    };
    NOTIFICATION_MASK Mask = TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                             TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK;
    CONST CALLBACK_STATUS_CASE *Case;
    TEST_ENLISTMENT Test;
    LARGE_INTEGER Timeout;
    HANDLE Transaction;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Cases); Index++)
    {
        Case = &Cases[Index];

        Status = CreateTransaction(&Transaction, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = Enlist(RmHandle, Transaction, Mask, Case->Mode, &Test);
            ok_eq_hex(Status, STATUS_SUCCESS);

            Status = ZwCommitTransaction(Transaction, FALSE);
            ok_eq_hex(Status, Case->Delivered);
            ok_eq_ulong(Test.Count, Case->Defer ? 1UL : 3UL);
            ok_eq_pointer(Test.Thread, PsGetCurrentThread());
            if (Case->Defer)
            {
                Status = ZwPrePrepareComplete(Test.Handle, NULL);
                ok_eq_hex(Status, Case->Delivered);
                ok_eq_ulong(Test.Count, 2UL);
                Status = ZwPrepareComplete(Test.Handle, NULL);
                ok_eq_hex(Status, Case->Delivered);
                ok_eq_ulong(Test.Count, 3UL);
                Status = ZwCommitComplete(Test.Handle, NULL);
                ok_eq_hex(Status, STATUS_SUCCESS);
                ok_eq_ulong(Test.Count, 3UL);
            }
            Timeout.QuadPart = -10 * 1000 * 1000;
            Status = ZwWaitForSingleObject(Transaction, FALSE, &Timeout);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);
            CloseEnlistment(&Test);
            ZwClose(Transaction);
        }

        Status = CreateTransaction(&Transaction, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = Enlist(RmHandle, Transaction, Mask, Case->Mode, &Test);
            ok_eq_hex(Status, STATUS_SUCCESS);

            Status = ZwRollbackEnlistment(Test.Handle, NULL);
            ok_eq_hex(Status, Case->Delivered);
            ok_eq_ulong(Test.Count, 1UL);
            if (Case->Defer)
            {
                Status = ZwRollbackComplete(Test.Handle, NULL);
                ok_eq_hex(Status, STATUS_SUCCESS);
            }
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
            CloseEnlistment(&Test);
            ZwClose(Transaction);
        }

        Status = CreateTransaction(&Transaction, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = Enlist(RmHandle, Transaction, Mask, Case->Mode, &Test);
            ok_eq_hex(Status, STATUS_SUCCESS);

            Status = ZwRollbackTransaction(Transaction, FALSE);
            ok_eq_hex(Status, Case->Delivered);
            ok_eq_ulong(Test.Count, 1UL);
            if (Case->Defer)
            {
                Status = ZwRollbackComplete(Test.Handle, NULL);
                ok_eq_hex(Status, STATUS_SUCCESS);
            }
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
            CloseEnlistment(&Test);
            ZwClose(Transaction);
        }

        if (Case->Defer)
        {
            continue;
        }

        Status = CreateTransaction(&Transaction, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = Enlist(RmHandle, Transaction, Mask, Case->Mode, &Test);
            ok_eq_hex(Status, STATUS_SUCCESS);

            Status = ZwCommitTransaction(Transaction, TRUE);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Test.Count, 3UL);
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);
            CloseEnlistment(&Test);
            ZwClose(Transaction);
        }
    }
}

static
VOID
TestNestedStatus(VOID)
{
    NOTIFICATION_MASK Mask = TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                             TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK;
    TEST_ENLISTMENT Test, Other;
    HANDLE Transaction;
    NTSTATUS Status;
    ULONG Order;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModePendPrepare, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwCommitTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Test.Count, 2UL);
        ok_eq_hex(Test.Responses[0], STATUS_PENDING);
        ok_eq_hex(Test.Responses[1], STATUS_PENDING);

        Status = ZwPrepareComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Test.Count, 3UL);
        ok_eq_hex(Test.Responses[2], STATUS_SUCCESS);
        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    for (Order = 0; Order < 2; Order++)
    {
        Status = CreateTransaction(&Transaction, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
        {
            continue;
        }

        Status = Enlist(RmHandle, Transaction, Mask, Order == 0 ? ModeComplete : ModeReturnPending, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = Enlist(RmHandle, Transaction, Mask, Order == 0 ? ModeReturnPending : ModeComplete, &Other);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwCommitTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_PENDING);
        ok_eq_ulong(Test.Count, 3UL);
        ok_eq_ulong(Other.Count, 3UL);
        ok_eq_hex(Test.Responses[0], STATUS_SUCCESS);
        ok_eq_hex(Test.Responses[1], STATUS_SUCCESS);
        ok_eq_hex(Test.Responses[2], STATUS_SUCCESS);
        ok_eq_hex(Other.Responses[0], STATUS_PENDING);
        ok_eq_hex(Other.Responses[1], STATUS_PENDING);
        ok_eq_hex(Other.Responses[2], STATUS_SUCCESS);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);
        CloseEnlistment(&Test);
        CloseEnlistment(&Other);
        ZwClose(Transaction);
    }
}

static
VOID
TestManagers(VOID)
{
    TRANSACTIONMANAGER_BASIC_INFORMATION TmBasic;
    UCHAR Buffer[sizeof(RESOURCEMANAGER_BASIC_INFORMATION) + 64 * sizeof(WCHAR)];
    PRESOURCEMANAGER_BASIC_INFORMATION RmBasic = (PVOID)Buffer;
    UNICODE_STRING Description = RTL_CONSTANT_STRING(L"KmtRm");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Length;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    Handle = NULL;
    Status = ZwCreateTransactionManager(&Handle, TRANSACTIONMANAGER_ALL_ACCESS, &ObjectAttributes, NULL, 0, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Status = ZwCreateTransactionManager(&TmHandle,
                                        TRANSACTIONMANAGER_ALL_ACCESS,
                                        &ObjectAttributes,
                                        NULL,
                                        TRANSACTION_MANAGER_VOLATILE,
                                        0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        TmHandle = NULL;
        return;
    }

    Length = 0;
    RtlZeroMemory(&TmBasic, sizeof(TmBasic));
    Status = ZwQueryInformationTransactionManager(TmHandle,
                                                  TransactionManagerBasicInformation,
                                                  &TmBasic,
                                                  sizeof(TmBasic),
                                                  &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Length, (ULONG)sizeof(TmBasic));

    Status = ExUuidCreate(&RmGuid);
    ok(NT_SUCCESS(Status), "ExUuidCreate returned %lx\n", Status);

    Handle = NULL;
    Status = ZwCreateResourceManager(&Handle,
                                     RESOURCEMANAGER_ALL_ACCESS,
                                     TmHandle,
                                     &RmGuid,
                                     &ObjectAttributes,
                                     0,
                                     NULL);
    ok_eq_hex(Status, STATUS_TM_VOLATILE);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Status = ZwCreateResourceManager(&RmHandle,
                                     RESOURCEMANAGER_ALL_ACCESS,
                                     TmHandle,
                                     &RmGuid,
                                     &ObjectAttributes,
                                     RESOURCE_MANAGER_VOLATILE,
                                     &Description);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        RmHandle = NULL;
        return;
    }

    Handle = NULL;
    Status = ZwCreateResourceManager(&Handle,
                                     RESOURCEMANAGER_ALL_ACCESS,
                                     TmHandle,
                                     &RmGuid,
                                     &ObjectAttributes,
                                     RESOURCE_MANAGER_VOLATILE,
                                     NULL);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_COLLISION);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Length = 0;
    RtlZeroMemory(Buffer, sizeof(Buffer));
    Status = ZwQueryInformationResourceManager(RmHandle,
                                               ResourceManagerBasicInformation,
                                               RmBasic,
                                               sizeof(Buffer),
                                               &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(RtlCompareMemory(&RmBasic->ResourceManagerId, &RmGuid, sizeof(GUID)) == sizeof(GUID), "Wrong RM id\n");
    ok_eq_ulong(RmBasic->DescriptionLength, (ULONG)Description.Length);
    ok(RtlCompareMemory(RmBasic->Description, Description.Buffer, Description.Length) == Description.Length,
       "Wrong RM description\n");

    Handle = NULL;
    Status = ZwOpenResourceManager(&Handle, RESOURCEMANAGER_QUERY_INFORMATION, TmHandle, &RmGuid, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Status = ObReferenceObjectByHandle(RmHandle,
                                       0,
                                       *TmResourceManagerObjectType,
                                       KernelMode,
                                       (PVOID *)&RmObject,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        RmObject = NULL;
        return;
    }

    Status = TmEnableCallbacks(RmObject, NULL, &RmKey);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    Status = TmEnableCallbacks(RmObject, Notification, &RmKey);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestTwoPhaseCommit(VOID)
{
    TRANSACTION_BASIC_INFORMATION Basic;
    ENLISTMENT_BASIC_INFORMATION EnlistmentBasic;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PKTRANSACTION TransactionObject;
    TEST_ENLISTMENT Test;
    HANDLE Transaction, Handle;
    NTSTATUS Status;
    ULONG Length;
    UOW Uow;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    RtlZeroMemory(&Basic, sizeof(Basic));
    Length = 0;
    Status = ZwQueryInformationTransaction(Transaction, TransactionBasicInformation, &Basic, sizeof(Basic), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Length, (ULONG)sizeof(Basic));
    ok_eq_ulong(Basic.State, (ULONG)TransactionStateNormal);
    ok_eq_ulong(Basic.Outcome, (ULONG)TransactionOutcomeUndetermined);

    Status = ObReferenceObjectByHandle(Transaction,
                                       0,
                                       *TmTransactionObjectType,
                                       KernelMode,
                                       (PVOID *)&TransactionObject,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Uow, sizeof(Uow));
        TmGetTransactionId(TransactionObject, &Uow);
        ok(RtlCompareMemory(&Uow, &Basic.TransactionId, sizeof(GUID)) == sizeof(GUID), "Wrong UOW\n");
        ok_bool_true(TmIsTransactionActive(TransactionObject), "TmIsTransactionActive returned");
    }
    else
    {
        TransactionObject = NULL;
    }

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Handle = NULL;
    Status = ZwOpenTransaction(&Handle, TRANSACTION_QUERY_INFORMATION, &ObjectAttributes, &Basic.TransactionId, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Status = Enlist(RmHandle,
                    Transaction,
                    TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                    TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                    ModeComplete,
                    &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&EnlistmentBasic, sizeof(EnlistmentBasic));
        Length = 0;
        Status = ZwQueryInformationEnlistment(Test.Handle,
                                              EnlistmentBasicInformation,
                                              &EnlistmentBasic,
                                              sizeof(EnlistmentBasic),
                                              &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(RtlCompareMemory(&EnlistmentBasic.TransactionId, &Basic.TransactionId, sizeof(GUID)) == sizeof(GUID),
           "Wrong enlistment transaction id\n");
        ok(RtlCompareMemory(&EnlistmentBasic.ResourceManagerId, &RmGuid, sizeof(GUID)) == sizeof(GUID),
           "Wrong enlistment RM id\n");

        Handle = NULL;
        Status = ZwOpenEnlistment(&Handle,
                                  ENLISTMENT_QUERY_INFORMATION,
                                  RmHandle,
                                  &EnlistmentBasic.EnlistmentId,
                                  &ObjectAttributes);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Handle);
        }

        Status = ZwPrepareComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_TRANSACTION_NOT_REQUESTED);
        Status = ZwCommitComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_TRANSACTION_NOT_REQUESTED);
        Status = ZwPrepareEnlistment(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_ENLISTMENT_NOT_SUPERIOR);
    }

    Status = ZwCommitTransaction(Transaction, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Test.Count, 3UL);
    ok_eq_hex(Test.Notifications[0], (ULONG)TRANSACTION_NOTIFY_PREPREPARE);
    ok_eq_hex(Test.Notifications[1], (ULONG)TRANSACTION_NOTIFY_PREPARE);
    ok_eq_hex(Test.Notifications[2], (ULONG)TRANSACTION_NOTIFY_COMMIT);
    ok_eq_hex(Test.Responses[0], STATUS_SUCCESS);
    ok_eq_hex(Test.Responses[1], STATUS_SUCCESS);
    ok_eq_hex(Test.Responses[2], STATUS_SUCCESS);
    ok_eq_pointer(Test.RmContext, &RmKey);
    ok_eq_uint(Test.Irql, PASSIVE_LEVEL);
    ok_eq_pointer(Test.Thread, PsGetCurrentThread());
    ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);
    if (TransactionObject != NULL)
    {
        ok_bool_false(TmIsTransactionActive(TransactionObject), "TmIsTransactionActive returned");
    }

    Status = ZwCommitTransaction(Transaction, TRUE);
    ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_COMMITTED);
    Status = ZwRollbackTransaction(Transaction, TRUE);
    ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_COMMITTED);
    Status = ZwWaitForSingleObject(Transaction, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    CloseEnlistment(&Test);
    if (TransactionObject != NULL)
    {
        ObDereferenceObject(TransactionObject);
    }
    ZwClose(Transaction);
}

static
VOID
TestCommitOnlyMask(VOID)
{
    TEST_ENLISTMENT Test;
    HANDLE Transaction;
    NTSTATUS Status;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = Enlist(RmHandle,
                    Transaction,
                    TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                    ModeComplete,
                    &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Test.CloseInCallback = TRUE;

    Status = ZwCommitTransaction(Transaction, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Test.Count, 1UL);
    ok_eq_hex(Test.Notifications[0], (ULONG)TRANSACTION_NOTIFY_COMMIT);
    ok_eq_hex(Test.Responses[0], STATUS_SUCCESS);
    ok_eq_pointer(Test.Handle, NULL);
    ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);

    CloseEnlistment(&Test);
    ZwClose(Transaction);
}

static
VOID
TestSinglePhase(VOID)
{
    TEST_ENLISTMENT Test;
    HANDLE Transaction;
    NTSTATUS Status;
    NOTIFICATION_MASK Mask = TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                             TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK |
                             TRANSACTION_NOTIFY_SINGLE_PHASE_COMMIT;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeComplete, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Test.Count, 1UL);
        ok_eq_hex(Test.Notifications[0], (ULONG)TRANSACTION_NOTIFY_SINGLE_PHASE_COMMIT);
        ok_eq_hex(Test.Responses[0], STATUS_SUCCESS);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);

        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeRejectSinglePhase, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Test.Count, 4UL);
        ok_eq_hex(Test.Notifications[0], (ULONG)TRANSACTION_NOTIFY_SINGLE_PHASE_COMMIT);
        ok_eq_hex(Test.Notifications[1], (ULONG)TRANSACTION_NOTIFY_PREPREPARE);
        ok_eq_hex(Test.Notifications[2], (ULONG)TRANSACTION_NOTIFY_PREPARE);
        ok_eq_hex(Test.Notifications[3], (ULONG)TRANSACTION_NOTIFY_COMMIT);
        ok_eq_hex(Test.Responses[0], STATUS_SUCCESS);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);

        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }
}

static
VOID
TestRollback(VOID)
{
    TEST_ENLISTMENT Test, Other;
    HANDLE Transaction;
    NTSTATUS Status;
    NOTIFICATION_MASK Mask = TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                             TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeComplete, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwRollbackTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Test.Count, 1UL);
        ok_eq_hex(Test.Notifications[0], (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        ok_eq_hex(Test.Responses[0], STATUS_SUCCESS);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);

        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_ABORTED);
        Status = ZwRollbackComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_TRANSACTION_NOT_REQUESTED);

        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeRollbackOnPrepare, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = Enlist(RmHandle, Transaction, Mask, ModeComplete, &Other);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_ABORTED);
        ok_eq_ulong(Test.Count, 3UL);
        ok_eq_hex(Test.Notifications[0], (ULONG)TRANSACTION_NOTIFY_PREPREPARE);
        ok_eq_hex(Test.Notifications[1], (ULONG)TRANSACTION_NOTIFY_PREPARE);
        ok_eq_hex(Test.Notifications[2], (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        ok_eq_hex(Test.Responses[1], STATUS_SUCCESS);
        ok(Other.Count >= 2, "Other.Count = %lu\n", Other.Count);
        ok_eq_hex(Other.Notifications[0], (ULONG)TRANSACTION_NOTIFY_PREPREPARE);
        ok_eq_hex(Other.Notifications[Other.Count - 1], (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);

        CloseEnlistment(&Test);
        CloseEnlistment(&Other);
        ZwClose(Transaction);
    }
}

static
VOID
TestReadOnly(VOID)
{
    TEST_ENLISTMENT Test, Other;
    HANDLE Transaction;
    NTSTATUS Status;
    NOTIFICATION_MASK Mask = TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                             TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = Enlist(RmHandle, Transaction, Mask, ModeReadOnlyOnPrePrepare, &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Enlist(RmHandle, Transaction, Mask, ModeComplete, &Other);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = ZwCommitTransaction(Transaction, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Test.Count, 1UL);
    ok_eq_hex(Test.Notifications[0], (ULONG)TRANSACTION_NOTIFY_PREPREPARE);
    ok_eq_hex(Test.Responses[0], STATUS_SUCCESS);
    ok_eq_ulong(Other.Count, 3UL);
    ok_eq_hex(Other.Notifications[2], (ULONG)TRANSACTION_NOTIFY_COMMIT);
    ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);

    Status = ZwReadOnlyEnlistment(Other.Handle, NULL);
    ok_eq_hex(Status, STATUS_TRANSACTION_NOT_REQUESTED);

    CloseEnlistment(&Test);
    CloseEnlistment(&Other);
    ZwClose(Transaction);
}

static
VOID
TestHandleClose(VOID)
{
    TEST_ENLISTMENT Test;
    HANDLE Transaction;
    NTSTATUS Status;
    NOTIFICATION_MASK Mask = TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                             TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeComplete, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        ZwClose(Transaction);
        ok_eq_ulong(Test.Count, 1UL);
        ok_eq_hex(Test.Notifications[0], (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        CloseEnlistment(&Test);
    }

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeComplete, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        CloseEnlistment(&Test);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_ABORTED);
        ZwClose(Transaction);
    }
}

static
VOID
TestPendingPrepare(VOID)
{
    TEST_ENLISTMENT Test;
    LARGE_INTEGER Timeout;
    HANDLE Transaction;
    NTSTATUS Status;
    NOTIFICATION_MASK Mask = TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                             TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = Enlist(RmHandle, Transaction, Mask, ModePendPrepare, &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = ZwCommitTransaction(Transaction, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Test.Count, 2UL);
    ok_eq_hex(Test.Notifications[1], (ULONG)TRANSACTION_NOTIFY_PREPARE);

    Timeout.QuadPart = 0;
    Status = ZwWaitForSingleObject(Transaction, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_TIMEOUT);
    Status = ZwCommitTransaction(Transaction, FALSE);
    ok_eq_hex(Status, STATUS_TRANSACTION_REQUEST_NOT_VALID);

    Status = ZwPrepareComplete(Test.Handle, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Timeout.QuadPart = -10 * 1000 * 1000 * 5;
    Status = ZwWaitForSingleObject(Transaction, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Test.Count, 3UL);
    ok_eq_hex(Test.Notifications[2], (ULONG)TRANSACTION_NOTIFY_COMMIT);
    ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);

    CloseEnlistment(&Test);
    ZwClose(Transaction);
}

static
VOID
TestEnlistmentKey(VOID)
{
    PKENLISTMENT Enlistment;
    TEST_ENLISTMENT Test;
    BOOLEAN Last;
    HANDLE Transaction;
    NTSTATUS Status;
    PVOID Key;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = Enlist(RmHandle,
                    Transaction,
                    TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                    ModeComplete,
                    &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Test.Handle,
                                           0,
                                           *TmEnlistmentObjectType,
                                           KernelMode,
                                           (PVOID *)&Enlistment,
                                           NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Key = NULL;
            Status = TmReferenceEnlistmentKey(Enlistment, &Key);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_pointer(Key, &Test);

            Last = TRUE;
            Status = TmDereferenceEnlistmentKey(Enlistment, &Last);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_bool_false(Last, "LastReference is");

            ObDereferenceObject(Enlistment);
        }
    }

    Status = ZwRollbackTransaction(Transaction, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CloseEnlistment(&Test);
    ZwClose(Transaction);
}

static
VOID
TestTimeout(VOID)
{
    TEST_ENLISTMENT Test;
    LARGE_INTEGER Timeout;
    HANDLE Transaction;
    NTSTATUS Status;

    Timeout.QuadPart = -10 * 1000 * 200;
    Status = CreateTransaction(&Transaction, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = Enlist(RmHandle,
                    Transaction,
                    TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                    ModeComplete,
                    &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Timeout.QuadPart = -10 * 1000 * 1000 * 5;
    Status = ZwWaitForSingleObject(Transaction, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Test.Count, 1UL);
    ok_eq_hex(Test.Notifications[0], (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
    ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);

    CloseEnlistment(&Test);
    ZwClose(Transaction);
}

static
VOID
TestQueue(VOID)
{
    UCHAR Buffer[sizeof(TRANSACTION_NOTIFICATION) + 64];
    PTRANSACTION_NOTIFICATION Record = (PVOID)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE QueueRm, Transaction;
    TEST_ENLISTMENT Test;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG Length;
    GUID Guid;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ExUuidCreate(&Guid);
    ok(NT_SUCCESS(Status), "ExUuidCreate returned %lx\n", Status);
    Status = ZwCreateResourceManager(&QueueRm,
                                     RESOURCEMANAGER_ALL_ACCESS,
                                     TmHandle,
                                     &Guid,
                                     &ObjectAttributes,
                                     RESOURCE_MANAGER_VOLATILE,
                                     NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Timeout.QuadPart = 0;
    Length = 0;
    Status = ZwGetNotificationResourceManager(QueueRm, Record, sizeof(Buffer), &Timeout, &Length, 0, 0);
    ok_eq_hex(Status, STATUS_TIMEOUT);

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(QueueRm,
                        Transaction,
                        TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                        TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                        ModeComplete,
                        &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwCommitTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_PENDING);
        ok_eq_ulong(Test.Count, 0UL);

        Length = 0;
        Status = ZwGetNotificationResourceManager(QueueRm, Record, sizeof(ULONG), &Timeout, &Length, 0, 0);
        ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
        ok_eq_ulong(Length, (ULONG)sizeof(TRANSACTION_NOTIFICATION));

        RtlZeroMemory(Buffer, sizeof(Buffer));
        Status = ZwGetNotificationResourceManager(QueueRm, Record, sizeof(Buffer), &Timeout, &Length, 0, 0);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Record->TransactionKey, &Test);
        ok_eq_hex(Record->TransactionNotification, (ULONG)TRANSACTION_NOTIFY_PREPREPARE);
        Status = ZwPrePrepareComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_PENDING);

        RtlZeroMemory(Buffer, sizeof(Buffer));
        Status = ZwGetNotificationResourceManager(QueueRm, Record, sizeof(Buffer), NULL, &Length, 0, 0);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(Record->TransactionNotification, (ULONG)TRANSACTION_NOTIFY_PREPARE);
        Status = ZwPrepareComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_PENDING);

        RtlZeroMemory(Buffer, sizeof(Buffer));
        Status = ZwGetNotificationResourceManager(QueueRm, Record, sizeof(Buffer), NULL, &Length, 0, 0);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(Record->TransactionNotification, (ULONG)TRANSACTION_NOTIFY_COMMIT);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);
        Timeout.QuadPart = 0;
        Status = ZwWaitForSingleObject(Transaction, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_TIMEOUT);
        Status = ZwCommitComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Timeout.QuadPart = -10 * 1000 * 1000 * 5;
        Status = ZwWaitForSingleObject(Transaction, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);

        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(QueueRm,
                        Transaction,
                        TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                        ModeComplete,
                        &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        ZwClose(QueueRm);
        QueueRm = NULL;
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_ABORTED);

        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    if (QueueRm != NULL)
    {
        ZwClose(QueueRm);
    }
}

static
VOID
TestNoWait(VOID)
{
    TEST_ENLISTMENT Test;
    HANDLE Transaction;
    NTSTATUS Status;
    NOTIFICATION_MASK Mask = TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE |
                             TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeComplete, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = ZwCommitTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Test.Count, 3UL);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeCommitted);
        Status = Enlist(RmHandle, Transaction, Mask, ModeComplete, &Test);
        ok_eq_hex(Status, STATUS_TRANSACTION_NOT_ACTIVE);
        if (NT_SUCCESS(Status))
        {
            CloseEnlistment(&Test);
        }
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeRollbackOnPrepare, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = ZwCommitTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_ABORTED);
        ok_eq_ulong(Test.Count, 3UL);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeComplete, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = ZwRollbackTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Test.Count, 1UL);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
        Status = ZwRollbackTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModeFailPrepare, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = ZwCommitTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_ABORTED);
        ok_eq_ulong(Test.Count, 3UL);
        ok_eq_hex(Test.Notifications[Test.Count - 1], (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(RmHandle, Transaction, Mask, ModePendPrepare, &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = ZwCommitTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = ZwRollbackTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
        ok_eq_ulong(Test.Count, 3UL);
        ok_eq_hex(Test.Notifications[2], (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        Status = ZwPrepareComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_TRANSACTION_NOT_REQUESTED);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }
}

static
VOID
TestMisc(VOID)
{
    UCHAR Buffer[sizeof(TRANSACTION_PROPERTIES_INFORMATION) + 64 * sizeof(WCHAR)];
    PTRANSACTION_PROPERTIES_INFORMATION Properties = (PVOID)Buffer;
    UCHAR EnlistBuffer[sizeof(TRANSACTION_ENLISTMENTS_INFORMATION) + 4 * sizeof(TRANSACTION_ENLISTMENT_PAIR)];
    PTRANSACTION_ENLISTMENTS_INFORMATION Enlistments = (PVOID)EnlistBuffer;
    UCHAR CursorBuffer[sizeof(KTMOBJECT_CURSOR) + 7 * sizeof(GUID)];
    PKTMOBJECT_CURSOR Cursor = (PVOID)CursorBuffer;
    UNICODE_STRING Description = RTL_CONSTANT_STRING(L"KmtTx");
    UCHAR Record[sizeof(TRANSACTION_NOTIFICATION) + 64];
    TRANSACTION_BASIC_INFORMATION Basic;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Transaction, OtherTm, Handle;
    TEST_ENLISTMENT Test;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG Length, Index;
    BOOLEAN Found;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    Status = TmEnableCallbacks(RmObject, Notification, &RmKey);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Timeout.QuadPart = 0;
    Length = 0;
    Status = ZwGetNotificationResourceManager(RmHandle, (PVOID)Record, sizeof(Record), &Timeout, &Length, 0, 0);
    ok_eq_hex(Status, STATUS_TIMEOUT);

    Status = ZwRecoverTransactionManager(TmHandle);
    ok_eq_hex(Status, STATUS_TM_VOLATILE);

    Handle = NULL;
    Status = ZwCreateTransaction(&Handle, 0, &ObjectAttributes, NULL, NULL, 0, 0, 0, NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Handle = NULL;
    Status = ZwOpenTransaction(&Handle, TRANSACTION_QUERY_INFORMATION, &ObjectAttributes, NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    RtlZeroMemory(CursorBuffer, sizeof(CursorBuffer));
    Length = 0;
    Status = ZwEnumerateTransactionObject(TmHandle,
                                          KTMOBJECT_RESOURCE_MANAGER,
                                          Cursor,
                                          sizeof(CursorBuffer),
                                          &Length);
    ok_eq_hex(Status, STATUS_NO_MORE_ENTRIES);
    Found = FALSE;
    for (Index = 0; Index < Cursor->ObjectIdCount && Index < 8; Index++)
    {
        if (RtlCompareMemory(&Cursor->ObjectIds[Index], &RmGuid, sizeof(GUID)) == sizeof(GUID))
        {
            Found = TRUE;
        }
    }
    ok_bool_true(Found, "Resource manager enumerated");
    Status = ZwEnumerateTransactionObject(TmHandle,
                                          KTMOBJECT_RESOURCE_MANAGER,
                                          Cursor,
                                          sizeof(CursorBuffer),
                                          &Length);
    ok_eq_hex(Status, STATUS_NO_MORE_ENTRIES);

    Status = ZwCreateTransaction(&Transaction,
                                 TRANSACTION_ALL_ACCESS,
                                 &ObjectAttributes,
                                 NULL,
                                 TmHandle,
                                 0,
                                 0,
                                 0,
                                 NULL,
                                 &Description);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Basic, sizeof(Basic));
        Length = 0;
        Status = ZwQueryInformationTransaction(Transaction,
                                               TransactionBasicInformation,
                                               &Basic,
                                               sizeof(ULONG),
                                               &Length);
        ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

        RtlZeroMemory(Buffer, sizeof(Buffer));
        Length = 0;
        Status = ZwQueryInformationTransaction(Transaction,
                                               TransactionPropertiesInformation,
                                               Properties,
                                               sizeof(Buffer),
                                               &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Properties->DescriptionLength, (ULONG)Description.Length);
        ok_eq_ulong(Properties->Outcome, (ULONG)TransactionOutcomeUndetermined);

        Status = Enlist(RmHandle,
                        Transaction,
                        TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                        ModeComplete,
                        &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        RtlZeroMemory(EnlistBuffer, sizeof(EnlistBuffer));
        Length = 0;
        Status = ZwQueryInformationTransaction(Transaction,
                                               TransactionEnlistmentInformation,
                                               Enlistments,
                                               sizeof(EnlistBuffer),
                                               &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Enlistments->NumberOfEnlistments, 1UL);
        ok(RtlCompareMemory(&Enlistments->EnlistmentPair[0].ResourceManagerId, &RmGuid, sizeof(GUID)) == sizeof(GUID),
           "Wrong RM id in enlistment pair\n");

        Status = ZwRollbackTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    Status = ZwCreateTransactionManager(&OtherTm,
                                        TRANSACTIONMANAGER_ALL_ACCESS,
                                        &ObjectAttributes,
                                        NULL,
                                        TRANSACTION_MANAGER_VOLATILE,
                                        0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwCreateTransaction(&Transaction,
                                     TRANSACTION_ALL_ACCESS,
                                     &ObjectAttributes,
                                     NULL,
                                     OtherTm,
                                     0,
                                     0,
                                     0,
                                     NULL,
                                     NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = Enlist(RmHandle,
                            Transaction,
                            TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                            ModeComplete,
                            &Test);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = ZwRollbackTransaction(Transaction, TRUE);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Test.Count, 1UL);
            CloseEnlistment(&Test);
            ZwClose(Transaction);
        }
        ZwClose(OtherTm);
    }
}

static
VOID
TestQueueRollback(VOID)
{
    UCHAR Buffer[sizeof(TRANSACTION_NOTIFICATION) + 64];
    PTRANSACTION_NOTIFICATION Record = (PVOID)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE QueueRm, Transaction;
    TEST_ENLISTMENT Test;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG Length;
    GUID Guid;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ExUuidCreate(&Guid);
    ok(NT_SUCCESS(Status), "ExUuidCreate returned %lx\n", Status);
    Status = ZwCreateResourceManager(&QueueRm,
                                     RESOURCEMANAGER_ALL_ACCESS,
                                     TmHandle,
                                     &Guid,
                                     &ObjectAttributes,
                                     RESOURCE_MANAGER_VOLATILE,
                                     NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Timeout.QuadPart = 0;
    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(QueueRm,
                        Transaction,
                        TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                        ModeComplete,
                        &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwRollbackTransaction(Transaction, FALSE);
        ok_eq_hex(Status, STATUS_PENDING);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
        Status = ZwWaitForSingleObject(Transaction, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);

        RtlZeroMemory(Buffer, sizeof(Buffer));
        Length = 0;
        Status = ZwGetNotificationResourceManager(QueueRm, Record, sizeof(Buffer), &Timeout, &Length, 0, 0);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(Record->TransactionNotification, (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        ok_eq_ulong(Record->ArgumentLength, 0UL);
        ok_eq_ulong(Length, (ULONG)sizeof(TRANSACTION_NOTIFICATION));
        Status = ZwRollbackComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = ZwWaitForSingleObject(Transaction, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);

        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = Enlist(QueueRm,
                        Transaction,
                        TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                        ModeComplete,
                        &Test);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwRollbackEnlistment(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_PENDING);
        RtlZeroMemory(Buffer, sizeof(Buffer));
        Status = ZwGetNotificationResourceManager(QueueRm, Record, sizeof(Buffer), &Timeout, &Length, 0, 0);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(Record->TransactionNotification, (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        Status = ZwRollbackComplete(Test.Handle, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);

        CloseEnlistment(&Test);
        ZwClose(Transaction);
    }

    Status = ZwRecoverResourceManager(QueueRm);
    ok_eq_hex(Status, STATUS_PENDING);
    RtlZeroMemory(Buffer, sizeof(Buffer));
    Status = ZwGetNotificationResourceManager(QueueRm, Record, sizeof(Buffer), &Timeout, &Length, 0, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Record->TransactionNotification, (ULONG)TRANSACTION_NOTIFY_LAST_RECOVER);
    ok_eq_pointer(Record->TransactionKey, NULL);

    ZwClose(QueueRm);
}

static
VOID
TestKeyReferences(VOID)
{
    PKENLISTMENT Enlistment;
    TEST_ENLISTMENT Test;
    BOOLEAN Last;
    HANDLE Transaction;
    NTSTATUS Status;
    PVOID Key;

    Status = CreateTransaction(&Transaction, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = Enlist(RmHandle,
                    Transaction,
                    TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK,
                    ModeComplete,
                    &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Test.Handle,
                                           0,
                                           *TmEnlistmentObjectType,
                                           KernelMode,
                                           (PVOID *)&Enlistment,
                                           NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Last = FALSE;
            Status = TmDereferenceEnlistmentKey(Enlistment, &Last);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_bool_true(Last, "LastReference is");

            Key = NULL;
            Status = TmReferenceEnlistmentKey(Enlistment, &Key);
            ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
            Status = TmDereferenceEnlistmentKey(Enlistment, &Last);
            ok_eq_hex(Status, STATUS_UNSUCCESSFUL);

            ObDereferenceObject(Enlistment);
        }
    }

    Status = ZwRollbackTransaction(Transaction, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CloseEnlistment(&Test);
    ZwClose(Transaction);
}

START_TEST(TmTransactions)
{
    TestManagers();
    if (TmHandle != NULL && RmHandle != NULL && RmObject != NULL)
    {
        TestTwoPhaseCommit();
        TestCommitOnlyMask();
        TestSinglePhase();
        TestRollback();
        TestReadOnly();
        TestHandleClose();
        TestPendingPrepare();
        TestEnlistmentKey();
        TestTimeout();
        TestQueue();
        TestNoWait();
        TestMisc();
        TestQueueRollback();
        TestKeyReferences();
        TestCallbackStatus();
        TestNestedStatus();
    }

    if (RmObject != NULL)
    {
        ObDereferenceObject(RmObject);
    }
    if (RmHandle != NULL)
    {
        ZwClose(RmHandle);
    }
    if (TmHandle != NULL)
    {
        ZwClose(TmHandle);
    }
}
