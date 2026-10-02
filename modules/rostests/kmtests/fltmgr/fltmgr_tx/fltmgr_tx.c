/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter Manager transaction enlistment test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <fltkernel.h>
#include <ndk/exfuncs.h>

#define NDEBUG
#include <debug.h>

#include "fltmgr_tx.h"

#define TEST_TAG 'xTtF'
#define MAX_RECORDS 8
#define MAX_FINISHED 16
#define BASIC_NOTIFICATIONS (TRANSACTION_NOTIFY_PREPREPARE | TRANSACTION_NOTIFY_PREPARE | \
                             TRANSACTION_NOTIFY_COMMIT | TRANSACTION_NOTIFY_ROLLBACK)

typedef enum _TX_MODE
{
    ModeComplete,
    ModePendPrepare,
    ModePendCommit
} TX_MODE;

typedef struct _TX_RECORD
{
    ULONG Count;
    ULONG Notifications[MAX_RECORDS];
    PFLT_CONTEXT Context;
    PFLT_INSTANCE Instance;
    PKTRANSACTION Transaction;
    PFLT_FILTER Filter;
    PFILE_OBJECT FileObject;
    PETHREAD Thread;
    PETHREAD Threads[MAX_RECORDS];
    KIRQL Irql;
} TX_RECORD, *PTX_RECORD;

static KMT_MESSAGE_HANDLER TestMessageHandler;

static WCHAR ServicePath[260];
static PDRIVER_OBJECT TestDriverObject;
static PFLT_FILTER Filter;
static PFLT_INSTANCE TestInstance;
static LONG ContextAllocated, ContextCleaned;
static TX_RECORD Record;
static TX_MODE Mode;
static KEVENT FinalizeEvent;
static HANDLE FinishedHandles[MAX_FINISHED];
static PKTRANSACTION FinishedTransactions[MAX_FINISHED];
static ULONG FinishedCount;

static
VOID
FLTAPI
ContextCleanup(
    _In_ PFLT_CONTEXT Context,
    _In_ FLT_CONTEXT_TYPE ContextType)
{
    UNREFERENCED_PARAMETER(Context);

    ok_eq_hex(ContextType, FLT_TRANSACTION_CONTEXT);
    InterlockedIncrement(&ContextCleaned);
}

static
NTSTATUS
FLTAPI
InstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    if (VolumeDeviceType != FILE_DEVICE_DISK_FILE_SYSTEM || TestInstance != NULL)
    {
        return STATUS_FLT_DO_NOT_ATTACH;
    }

    TestInstance = FltObjects->Instance;
    return STATUS_SUCCESS;
}

static
NTSTATUS
FLTAPI
InstanceQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    return STATUS_SUCCESS;
}

static
NTSTATUS
FLTAPI
TransactionNotification(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_CONTEXT TransactionContext,
    _In_ ULONG NotificationMask)
{
    ULONG Index = Record.Count;

    Record.Context = TransactionContext;
    Record.Instance = FltObjects->Instance;
    Record.Transaction = FltObjects->Transaction;
    Record.Filter = FltObjects->Filter;
    Record.FileObject = FltObjects->FileObject;
    Record.Thread = PsGetCurrentThread();
    Record.Irql = KeGetCurrentIrql();
    if (Index < MAX_RECORDS)
    {
        Record.Notifications[Index] = NotificationMask;
        Record.Threads[Index] = PsGetCurrentThread();
    }
    Record.Count = Index + 1;

    if (NotificationMask == TRANSACTION_NOTIFY_COMMIT_FINALIZE)
    {
        KeSetEvent(&FinalizeEvent, IO_NO_INCREMENT, FALSE);
    }
    if ((Mode == ModePendPrepare && NotificationMask == TRANSACTION_NOTIFY_PREPARE) ||
        (Mode == ModePendCommit && NotificationMask == TRANSACTION_NOTIFY_COMMIT))
    {
        return STATUS_PENDING;
    }
    return STATUS_SUCCESS;
}

static CONST FLT_OPERATION_REGISTRATION Operations[] =
{
    { IRP_MJ_OPERATION_END }
};

static CONST FLT_CONTEXT_REGISTRATION ContextRegistration[] =
{
    { FLT_TRANSACTION_CONTEXT, 0, ContextCleanup, sizeof(ULONG), TEST_TAG },
    { FLT_CONTEXT_END }
};

static CONST FLT_REGISTRATION Registration =
{
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    FLTFL_REGISTRATION_DO_NOT_SUPPORT_SERVICE_STOP,
    ContextRegistration,
    Operations,
    NULL,
    InstanceSetup,
    InstanceQueryTeardown,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    TransactionNotification,
    NULL,
    NULL
};

static
NTSTATUS
WriteInstanceRegistry(VOID)
{
    static WCHAR InstanceName[] = L"FltMgrTx Instance";
    static WCHAR Altitude[] = L"370124";
    WCHAR Path[320];
    ULONG Flags = 0;
    NTSTATUS Status;

    RtlStringCbPrintfW(Path, sizeof(Path), L"%ls\\Instances", ServicePath);
    Status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE,
                                   Path,
                                   L"DefaultInstance",
                                   REG_SZ,
                                   InstanceName,
                                   sizeof(InstanceName));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlStringCbPrintfW(Path, sizeof(Path), L"%ls\\Instances\\%ls", ServicePath, InstanceName);
    Status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Path, L"Altitude", REG_SZ, Altitude, sizeof(Altitude));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    return RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Path, L"Flags", REG_DWORD, &Flags, sizeof(Flags));
}

static
NTSTATUS
CreateTransaction(
    _Out_ PHANDLE Handle,
    _Out_ PKTRANSACTION *Transaction)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateTransaction(Handle,
                                 TRANSACTION_ALL_ACCESS,
                                 &ObjectAttributes,
                                 NULL,
                                 NULL,
                                 0,
                                 0,
                                 0,
                                 NULL,
                                 NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ObReferenceObjectByHandle(*Handle,
                                       0,
                                       *TmTransactionObjectType,
                                       KernelMode,
                                       (PVOID *)Transaction,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(*Handle);
    }
    return Status;
}

static
NTSTATUS
AllocateContext(
    _Out_ PFLT_CONTEXT *Context)
{
    NTSTATUS Status;

    Status = FltAllocateContext(Filter, FLT_TRANSACTION_CONTEXT, sizeof(ULONG), PagedPool, Context);
    if (NT_SUCCESS(Status))
    {
        InterlockedIncrement(&ContextAllocated);
    }
    return Status;
}

static
NTSTATUS
StartTransaction(
    _In_ NOTIFICATION_MASK Mask,
    _In_ TX_MODE NewMode,
    _Out_ PHANDLE Handle,
    _Out_ PKTRANSACTION *Transaction,
    _Out_ PFLT_CONTEXT *Context)
{
    NTSTATUS Status;

    RtlZeroMemory(&Record, sizeof(Record));
    Mode = NewMode;
    *Context = NULL;

    Status = CreateTransaction(Handle, Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = AllocateContext(Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = FltSetTransactionContext(TestInstance, *Transaction, FLT_SET_CONTEXT_KEEP_IF_EXISTS, *Context, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    if (NT_SUCCESS(Status))
    {
        Status = FltEnlistInTransaction(TestInstance, *Transaction, *Context, Mask);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    if (!NT_SUCCESS(Status))
    {
        if (*Context != NULL)
        {
            FltReleaseContext(*Context);
        }
        ObDereferenceObject(*Transaction);
        ZwClose(*Handle);
    }
    return Status;
}

static
VOID
RetireTransaction(
    _In_ HANDLE Handle,
    _In_ PKTRANSACTION Transaction)
{
    if (FinishedCount < MAX_FINISHED)
    {
        FinishedHandles[FinishedCount] = Handle;
        FinishedTransactions[FinishedCount] = Transaction;
        FinishedCount++;
        return;
    }

    ObDereferenceObject(Transaction);
    ZwClose(Handle);
}

static
VOID
WaitForContextCleanup(VOID)
{
    LARGE_INTEGER Delay;
    ULONG Tries;

    Delay.QuadPart = -10 * 1000 * 10;
    for (Tries = 0; Tries < 500 && ContextCleaned != ContextAllocated; Tries++)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    ok_eq_long(ContextCleaned, ContextAllocated);
}

static
VOID
CloseRetiredTransactions(VOID)
{
    while (FinishedCount != 0)
    {
        FinishedCount--;
        ObDereferenceObject(FinishedTransactions[FinishedCount]);
        ZwClose(FinishedHandles[FinishedCount]);
    }
    WaitForContextCleanup();
}

static
VOID
EndTransaction(
    _In_ HANDLE Handle,
    _In_ PKTRANSACTION Transaction,
    _In_ PFLT_CONTEXT Context)
{
    FltReleaseContext(Context);
    RetireTransaction(Handle, Transaction);
    WaitForContextCleanup();
}

static
VOID
TestCommit(VOID)
{
    PFLT_CONTEXT Context, Found;
    PKTRANSACTION Transaction;
    LONG Cleaned;
    HANDLE Handle;
    NTSTATUS Status;

    Status = CreateTransaction(&Handle, &Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Found = (PVOID)1;
    Status = FltGetTransactionContext(TestInstance, Transaction, &Found);
    ok_eq_hex(Status, STATUS_NOT_FOUND);

    Status = AllocateContext(&Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Transaction);
        ZwClose(Handle);
        return;
    }

    Status = FltSetTransactionContext(TestInstance, Transaction, FLT_SET_CONTEXT_KEEP_IF_EXISTS, Context, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Found = NULL;
    Status = FltGetTransactionContext(TestInstance, Transaction, &Found);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Found, Context);
    if (NT_SUCCESS(Status))
    {
        FltReleaseContext(Found);
    }

    RtlZeroMemory(&Record, sizeof(Record));
    Mode = ModeComplete;
    Status = FltEnlistInTransaction(TestInstance, Transaction, Context, BASIC_NOTIFICATIONS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Cleaned = ContextCleaned;
    Status = ZwCommitTransaction(Handle, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Record.Threads[0], PsGetCurrentThread());
    ok_eq_pointer(Record.Threads[1], PsGetCurrentThread());
    ok_eq_pointer(Record.Threads[2], PsGetCurrentThread());
    ok_eq_ulong(Record.Count, 3UL);
    ok_eq_hex(Record.Notifications[0], (ULONG)TRANSACTION_NOTIFY_PREPREPARE);
    ok_eq_hex(Record.Notifications[1], (ULONG)TRANSACTION_NOTIFY_PREPARE);
    ok_eq_hex(Record.Notifications[2], (ULONG)TRANSACTION_NOTIFY_COMMIT);
    ok_eq_pointer(Record.Context, Context);
    ok_eq_pointer(Record.Instance, TestInstance);
    ok_eq_pointer(Record.Transaction, Transaction);
    ok_eq_pointer(Record.Filter, Filter);
    ok_eq_pointer(Record.FileObject, NULL);
    ok_eq_pointer(Record.Thread, PsGetCurrentThread());
    ok_eq_uint(Record.Irql, PASSIVE_LEVEL);

    ok_eq_long(ContextCleaned, Cleaned);

    EndTransaction(Handle, Transaction, Context);
}

static
VOID
TestRollback(VOID)
{
    PKTRANSACTION Transaction;
    PFLT_CONTEXT Context;
    HANDLE Handle;
    NTSTATUS Status;

    Status = StartTransaction(BASIC_NOTIFICATIONS, ModeComplete, &Handle, &Transaction, &Context);
    if (NT_SUCCESS(Status))
    {
        Status = ZwRollbackTransaction(Handle, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Record.Threads[0], PsGetCurrentThread());
        ok_eq_ulong(Record.Count, 1UL);
        ok_eq_hex(Record.Notifications[0], (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        EndTransaction(Handle, Transaction, Context);
    }

    Status = StartTransaction(BASIC_NOTIFICATIONS, ModeComplete, &Handle, &Transaction, &Context);
    if (NT_SUCCESS(Status))
    {
        Status = FltRollbackEnlistment(TestInstance, Transaction, Context);
        ok_eq_hex(Status, STATUS_PENDING);
        ok_eq_pointer(Record.Threads[0], PsGetCurrentThread());
        ok_eq_ulong(Record.Count, 1UL);
        ok_eq_hex(Record.Notifications[0], (ULONG)TRANSACTION_NOTIFY_ROLLBACK);
        Status = ZwCommitTransaction(Handle, TRUE);
        ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_ABORTED);
        EndTransaction(Handle, Transaction, Context);
    }
}

static
VOID
TestPending(VOID)
{
    PKTRANSACTION Transaction;
    PFLT_CONTEXT Context;
    LARGE_INTEGER Timeout;
    HANDLE Handle;
    NTSTATUS Status;

    Status = StartTransaction(BASIC_NOTIFICATIONS, ModePendPrepare, &Handle, &Transaction, &Context);
    if (NT_SUCCESS(Status))
    {
        Status = ZwCommitTransaction(Handle, FALSE);
        ok_eq_hex(Status, STATUS_PENDING);
        ok_eq_pointer(Record.Threads[0], PsGetCurrentThread());
        ok_eq_pointer(Record.Threads[1], PsGetCurrentThread());
        ok_eq_ulong(Record.Count, 2UL);
        ok_eq_hex(Record.Notifications[1], (ULONG)TRANSACTION_NOTIFY_PREPARE);

        Status = FltPrepareComplete(TestInstance, Transaction, Context);
        ok_eq_hex(Status, STATUS_PENDING);
        ok_eq_pointer(Record.Threads[2], PsGetCurrentThread());
        ok_eq_ulong(Record.Count, 3UL);
        ok_eq_hex(Record.Notifications[2], (ULONG)TRANSACTION_NOTIFY_COMMIT);

        Timeout.QuadPart = -10 * 1000 * 1000 * 5;
        Status = ZwWaitForSingleObject(Handle, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        EndTransaction(Handle, Transaction, Context);
    }

    Status = StartTransaction(BASIC_NOTIFICATIONS, ModePendCommit, &Handle, &Transaction, &Context);
    if (NT_SUCCESS(Status))
    {
        Status = ZwCommitTransaction(Handle, FALSE);
        ok_eq_hex(Status, STATUS_PENDING);
        ok_eq_pointer(Record.Threads[2], PsGetCurrentThread());
        ok_eq_ulong(Record.Count, 3UL);

        Timeout.QuadPart = 0;
        Status = ZwWaitForSingleObject(Handle, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_TIMEOUT);
        Status = FltCommitComplete(TestInstance, Transaction, Context);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Timeout.QuadPart = -10 * 1000 * 1000 * 5;
        Status = ZwWaitForSingleObject(Handle, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        EndTransaction(Handle, Transaction, Context);
    }
}

static
VOID
TestFinalize(VOID)
{
    PKTRANSACTION Transaction;
    PFLT_CONTEXT Context;
    LARGE_INTEGER Timeout;
    HANDLE Handle;
    NTSTATUS Status;

    KeInitializeEvent(&FinalizeEvent, NotificationEvent, FALSE);
    Status = StartTransaction(FLT_MAX_TRANSACTION_NOTIFICATIONS,
                              ModeComplete,
                              &Handle,
                              &Transaction,
                              &Context);
    if (NT_SUCCESS(Status))
    {
        Status = ZwCommitTransaction(Handle, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Timeout.QuadPart = -10 * 1000 * 1000 * 5;
        Status = KeWaitForSingleObject(&FinalizeEvent, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Record.Count, 4UL);
        ok_eq_hex(Record.Notifications[2], (ULONG)TRANSACTION_NOTIFY_COMMIT);
        ok_eq_hex(Record.Notifications[3], (ULONG)TRANSACTION_NOTIFY_COMMIT_FINALIZE);
        EndTransaction(Handle, Transaction, Context);
    }
}

static
VOID
TestRegister(VOID)
{
    NTSTATUS Status;

    Status = WriteInstanceRegistry();
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = FltRegisterFilter(TestDriverObject, &Registration, &Filter);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        Filter = NULL;
        return;
    }

    Status = FltStartFiltering(Filter);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(TestInstance != NULL, "No instance attached\n");
}

static
VOID
TestRun(VOID)
{
    if (Filter == NULL || TestInstance == NULL)
    {
        return;
    }

    TestFinalize();
    TestRollback();
    TestPending();
    TestCommit();
    CloseRetiredTransactions();
}

static
VOID
TestUnregister(VOID)
{
    if (Filter == NULL)
    {
        return;
    }

    FltUnregisterFilter(Filter);
    Filter = NULL;
    TestInstance = NULL;
    ok_eq_long(ContextCleaned, ContextAllocated);
}

NTSTATUS
TestEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PCUNICODE_STRING RegistryPath,
    OUT PCWSTR *DeviceName,
    IN OUT INT *Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    TestDriverObject = DriverObject;
    if (RegistryPath->Length >= sizeof(ServicePath))
    {
        return STATUS_NAME_TOO_LONG;
    }
    RtlCopyMemory(ServicePath, RegistryPath->Buffer, RegistryPath->Length);
    ServicePath[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    *DeviceName = L"FltMgrTx";
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);

    TestUnregister();
}

static
NTSTATUS
TestMessageHandler(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    switch (ControlCode)
    {
        case IOCTL_FLTTX_REGISTER:
            TestRegister();
            break;

        case IOCTL_FLTTX_RUN:
            TestRun();
            break;

        case IOCTL_FLTTX_UNREGISTER:
            TestUnregister();
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}
