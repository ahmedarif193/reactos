/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter Manager I/O, context and port test driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <fltkernel.h>

#define NDEBUG
#include <debug.h>

#include "fltmgr_io.h"

#define TEST_TAG 'oItF'
#define TEST_MAGIC 0x4F49544C
#define CONTEXT_KINDS 5

#define CTX_CREATE   ((PVOID)(ULONG_PTR)0xC0DE0001)
#define CTX_WRITE    ((PVOID)(ULONG_PTR)0xC0DE0002)
#define CTX_SETINFO  ((PVOID)(ULONG_PTR)0xC0DE0003)
#define CTX_QUERY    ((PVOID)(ULONG_PTR)0xC0DE0004)
#define PORT_COOKIE  ((PVOID)(ULONG_PTR)0xC00C1E00)

typedef struct _TEST_CONTEXT
{
    ULONG Magic;
    ULONG Kind;
} TEST_CONTEXT, *PTEST_CONTEXT;

static CONST GUID TestEcpType =
    {0x3d0f2c6b, 0x91a4, 0x4c1e, {0x8b, 0x5d, 0x10, 0x2f, 0x7a, 0x44, 0x9e, 0x63}};

static KMT_MESSAGE_HANDLER TestMessageHandler;

static WCHAR ServicePath[260];
static PDRIVER_OBJECT TestDriverObject;
static PFLT_FILTER Filter;
static PFLT_PORT ServerPort;
static PFLT_PORT ClientPort;

static LONG SetupCount, AttachedCount, TeardownStartCount, TeardownCompleteCount;
static LONG PreCreate, PostCreate, Denied;
static LONG PreWrite, PostWrite, PreRead, PostRead;
static LONG PreSetInfo, PostSetInfo, PreQuery, PostQuery, PendedPre, PendedPost;
static LONG PreCleanup, PreClose;
static LONG GeneratedSeen, GeneratedDone, AsyncDone, EcpCleaned, WorkerRuns, DeferredRuns;
static FLT_CALLBACK_DATA_QUEUE Queue;
static PFLT_INSTANCE QueueInstance;
static KSPIN_LOCK QueueLock;
static LIST_ENTRY QueueList;
static KEVENT AsyncEvent;
static LONG ContextAllocated[CONTEXT_KINDS], ContextCleaned[CONTEXT_KINDS];
static LONG ConnectCount, DisconnectCount, MessageCount;

static
ULONG
ContextKind(
    _In_ FLT_CONTEXT_TYPE Type)
{
    switch (Type)
    {
        case FLT_VOLUME_CONTEXT: return 0;
        case FLT_INSTANCE_CONTEXT: return 1;
        case FLT_FILE_CONTEXT: return 2;
        case FLT_STREAM_CONTEXT: return 3;
        default: return 4;
    }
}

static
VOID
FLTAPI
ContextCleanup(
    _In_ PFLT_CONTEXT Context,
    _In_ FLT_CONTEXT_TYPE ContextType)
{
    PTEST_CONTEXT Test = Context;

    ok_eq_hex(Test->Magic, TEST_MAGIC);
    ok_eq_ulong(Test->Kind, ContextKind(ContextType));
    InterlockedIncrement(&ContextCleaned[ContextKind(ContextType)]);
}

static
NTSTATUS
AllocateTestContext(
    _In_ FLT_CONTEXT_TYPE Type,
    _In_ POOL_TYPE PoolType,
    _Out_ PTEST_CONTEXT *Context)
{
    NTSTATUS Status;

    Status = FltAllocateContext(Filter, Type, sizeof(TEST_CONTEXT), PoolType, (PFLT_CONTEXT *)Context);
    if (NT_SUCCESS(Status))
    {
        (*Context)->Magic = TEST_MAGIC;
        (*Context)->Kind = ContextKind(Type);
        InterlockedIncrement(&ContextAllocated[ContextKind(Type)]);
    }
    return Status;
}

static
BOOLEAN
IsTestName(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCWSTR Tail)
{
    UNICODE_STRING Suffix, End;

    RtlInitUnicodeString(&Suffix, Tail);
    if (FileObject == NULL || FileObject->FileName.Length < Suffix.Length)
    {
        return FALSE;
    }

    End.Length = End.MaximumLength = Suffix.Length;
    End.Buffer = (PWCH)((PUCHAR)FileObject->FileName.Buffer + FileObject->FileName.Length - Suffix.Length);
    return RtlEqualUnicodeString(&End, &Suffix, TRUE);
}

static
BOOLEAN
IsTestHandle(
    _In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    PFLT_CONTEXT Context;

    if (FltObjects->FileObject == NULL ||
        !NT_SUCCESS(FltGetStreamHandleContext(FltObjects->Instance, FltObjects->FileObject, &Context)))
    {
        return FALSE;
    }

    FltReleaseContext(Context);
    return TRUE;
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
    PTEST_CONTEXT Context, Second, Found;
    PFLT_CONTEXT Old;
    PFLT_VOLUME Volume;
    PFLT_FILTER RetFilter;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    InterlockedIncrement(&SetupCount);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_ulong((ULONG)FltObjects->Size, (ULONG)sizeof(FLT_RELATED_OBJECTS));
    ok_eq_pointer(FltObjects->Filter, Filter);
    ok(FltObjects->Volume != NULL, "Volume is NULL\n");
    ok(FltObjects->Instance != NULL, "Instance is NULL\n");
    ok_eq_pointer(FltObjects->FileObject, NULL);

    if (VolumeDeviceType != FILE_DEVICE_DISK_FILE_SYSTEM)
    {
        return STATUS_FLT_DO_NOT_ATTACH;
    }

    Status = FltGetVolumeFromInstance(FltObjects->Instance, &Volume);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Volume, FltObjects->Volume);
        FltObjectDereference(Volume);
    }

    Status = FltGetFilterFromInstance(FltObjects->Instance, &RetFilter);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(RetFilter, Filter);
        FltObjectDereference(RetFilter);
    }

    Status = AllocateTestContext(FLT_VOLUME_CONTEXT, PagedPool, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        FltReleaseContext(Context);
    }

    Status = AllocateTestContext(FLT_VOLUME_CONTEXT, NonPagedPool, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = FltSetVolumeContext(FltObjects->Volume, FLT_SET_CONTEXT_KEEP_IF_EXISTS, Context, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = FltSetVolumeContext(FltObjects->Volume, FLT_SET_CONTEXT_KEEP_IF_EXISTS, Context, NULL);
        ok_eq_hex(Status, STATUS_FLT_CONTEXT_ALREADY_LINKED);
        Status = FltGetVolumeContext(Filter, FltObjects->Volume, (PFLT_CONTEXT *)&Found);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_pointer(Found, Context);
            FltReleaseContext(Found);
        }
        FltReleaseContext(Context);
    }

    Status = FltGetInstanceContext(FltObjects->Instance, (PFLT_CONTEXT *)&Found);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    if (NT_SUCCESS(Status))
    {
        FltReleaseContext(Found);
    }

    Status = AllocateTestContext(FLT_INSTANCE_CONTEXT, NonPagedPool, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = FltSetInstanceContext(FltObjects->Instance, FLT_SET_CONTEXT_KEEP_IF_EXISTS, Context, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = AllocateTestContext(FLT_INSTANCE_CONTEXT, NonPagedPool, &Second);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Old = NULL;
            Status = FltSetInstanceContext(FltObjects->Instance, FLT_SET_CONTEXT_KEEP_IF_EXISTS, Second, &Old);
            ok_eq_hex(Status, STATUS_FLT_CONTEXT_ALREADY_DEFINED);
            ok_eq_pointer(Old, Context);
            if (Old != NULL)
            {
                FltReleaseContext(Old);
            }

            Old = NULL;
            Status = FltSetInstanceContext(FltObjects->Instance, FLT_SET_CONTEXT_REPLACE_IF_EXISTS, Second, &Old);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_pointer(Old, Context);
            if (Old != NULL)
            {
                FltReleaseContext(Old);
            }
            FltReleaseContext(Second);
        }
        FltReleaseContext(Context);
    }

    InterlockedIncrement(&AttachedCount);
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
VOID
FLTAPI
InstanceTeardownStart(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Reason)
{
    ok_eq_pointer(FltObjects->Filter, Filter);
    ok_eq_hex(Reason, FLTFL_INSTANCE_TEARDOWN_FILTER_UNLOAD);
    InterlockedIncrement(&TeardownStartCount);
}

static
VOID
FLTAPI
InstanceTeardownComplete(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Reason)
{
    ok_eq_pointer(FltObjects->Filter, Filter);
    ok_eq_hex(Reason, FLTFL_INSTANCE_TEARDOWN_FILTER_UNLOAD);
    InterlockedIncrement(&TeardownCompleteCount);
}

static
NTSTATUS
FLTAPI
QueueInsert(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _In_ PFLT_CALLBACK_DATA Cbd,
    _In_opt_ PVOID InsertContext)
{
    UNREFERENCED_PARAMETER(InsertContext);

    ok_eq_pointer(Cbdq, &Queue);
    InsertTailList(&QueueList, &Cbd->QueueLinks);
    return STATUS_SUCCESS;
}

static
VOID
FLTAPI
QueueRemove(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _In_ PFLT_CALLBACK_DATA Cbd)
{
    UNREFERENCED_PARAMETER(Cbdq);

    RemoveEntryList(&Cbd->QueueLinks);
}

static
PFLT_CALLBACK_DATA
FLTAPI
QueuePeekNext(
    _In_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _In_opt_ PFLT_CALLBACK_DATA Cbd,
    _In_opt_ PVOID PeekContext)
{
    PLIST_ENTRY Next;

    UNREFERENCED_PARAMETER(Cbdq);
    UNREFERENCED_PARAMETER(PeekContext);

    Next = Cbd ? Cbd->QueueLinks.Flink : QueueList.Flink;
    return (Next == &QueueList) ? NULL : CONTAINING_RECORD(Next, FLT_CALLBACK_DATA, QueueLinks);
}

static
VOID
FLTAPI
QueueAcquire(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _Out_opt_ PKIRQL Irql)
{
    UNREFERENCED_PARAMETER(Cbdq);

    KeAcquireSpinLock(&QueueLock, Irql);
}

static
VOID
FLTAPI
QueueRelease(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _In_opt_ KIRQL Irql)
{
    UNREFERENCED_PARAMETER(Cbdq);

    KeReleaseSpinLock(&QueueLock, Irql);
}

static
VOID
FLTAPI
QueueCompleteCanceled(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _Inout_ PFLT_CALLBACK_DATA Cbd)
{
    UNREFERENCED_PARAMETER(Cbdq);

    Cbd->IoStatus.Status = STATUS_CANCELLED;
    Cbd->IoStatus.Information = 0;
    FltCompletePendedPreOperation(Cbd, FLT_PREOP_COMPLETE, NULL);
}

static
VOID
FLTAPI
QueueWorker(
    _In_ PFLT_GENERIC_WORKITEM WorkItem,
    _In_ PVOID FltObject,
    _In_opt_ PVOID Context)
{
    PFLT_CALLBACK_DATA Data;

    UNREFERENCED_PARAMETER(Context);

    ok_irql(PASSIVE_LEVEL);
    ok_eq_pointer(FltObject, Filter);
    InterlockedIncrement(&WorkerRuns);

    while ((Data = FltCbdqRemoveNextIo(&Queue, NULL)) != NULL)
    {
        FltCompletePendedPreOperation(Data, FLT_PREOP_SUCCESS_WITH_CALLBACK, CTX_SETINFO);
    }

    FltFreeGenericWorkItem(WorkItem);
}

static
BOOLEAN
QueuePendedPre(
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ PFLT_INSTANCE Instance)
{
    PFLT_GENERIC_WORKITEM WorkItem;
    PFLT_CALLBACK_DATA Removed;
    NTSTATUS Status;

    if (QueueInstance == NULL)
    {
        Status = FltCbdqInitialize(Instance,
                                   &Queue,
                                   QueueInsert,
                                   QueueRemove,
                                   QueuePeekNext,
                                   QueueAcquire,
                                   QueueRelease,
                                   QueueCompleteCanceled);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
        {
            return FALSE;
        }
        QueueInstance = Instance;
        Removed = FltCbdqRemoveNextIo(&Queue, NULL);
        ok_eq_pointer(Removed, NULL);
    }
    if (QueueInstance != Instance)
    {
        return FALSE;
    }

    WorkItem = FltAllocateGenericWorkItem();
    ok(WorkItem != NULL, "No work item\n");
    if (WorkItem == NULL)
    {
        return FALSE;
    }

    Status = FltCbdqInsertIo(&Queue, Data, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        FltFreeGenericWorkItem(WorkItem);
        return FALSE;
    }

    Status = FltQueueGenericWorkItem(WorkItem, Filter, QueueWorker, DelayedWorkQueue, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        FltFreeGenericWorkItem(WorkItem);
        Data = FltCbdqRemoveNextIo(&Queue, NULL);
        return Data == NULL;
    }

    return TRUE;
}

static
VOID
FLTAPI
DeferredWorker(
    _In_ PFLT_DEFERRED_IO_WORKITEM WorkItem,
    _In_ PFLT_CALLBACK_DATA Data,
    _In_opt_ PVOID Context)
{
    ok_irql(PASSIVE_LEVEL);
    ok_eq_pointer(Context, CTX_QUERY);
    InterlockedIncrement(&DeferredRuns);
    FltCompletePendedPostOperation(Data);
    FltFreeDeferredIoWorkItem(WorkItem);
}

static
BOOLEAN
QueuePendedPost(
    _In_ PFLT_CALLBACK_DATA Data)
{
    PFLT_DEFERRED_IO_WORKITEM WorkItem;
    NTSTATUS Status;

    WorkItem = FltAllocateDeferredIoWorkItem();
    ok(WorkItem != NULL, "No work item\n");
    if (WorkItem == NULL)
    {
        return FALSE;
    }

    Status = FltQueueDeferredIoWorkItem(WorkItem, Data, DeferredWorker, DelayedWorkQueue, CTX_QUERY);
    if (!NT_SUCCESS(Status))
    {
        ok_eq_hex(Status, STATUS_FLT_NOT_SAFE_TO_POST_OPERATION);
        FltFreeDeferredIoWorkItem(WorkItem);
        return FALSE;
    }

    return TRUE;
}

static
VOID
NTAPI
EcpCleanup(
    _Inout_ PVOID EcpContext,
    _In_ LPCGUID EcpType)
{
    ok(RtlEqualMemory(EcpType, &TestEcpType, sizeof(GUID)), "Unexpected ECP type\n");
    ok_eq_ulong(*(PULONG)EcpContext, 0x45435021UL);
    InterlockedIncrement(&EcpCleaned);
}

static
VOID
TestEcps(VOID)
{
    PECP_LIST EcpList = NULL;
    PVOID Context = NULL, Second = NULL, Found;
    ULONG Size;
    GUID Type;
    NTSTATUS Status;

    Status = FltAllocateExtraCreateParameterList(Filter, 0, &EcpList);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Found = (PVOID)1;
    Status = FltGetNextExtraCreateParameter(Filter, EcpList, NULL, NULL, &Found, &Size);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    ok_eq_pointer(Found, NULL);

    Status = FltAllocateExtraCreateParameter(Filter, &TestEcpType, sizeof(ULONG), 0, EcpCleanup, TEST_TAG, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = FltAllocateExtraCreateParameter(Filter, &TestEcpType, sizeof(ULONG), 0, EcpCleanup, TEST_TAG, &Second);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Context != NULL && Second != NULL)
    {
        *(PULONG)Context = 0x45435021;
        *(PULONG)Second = 0x45435021;

        ok(!FltIsEcpAcknowledged(Filter, Context), "New ECP is acknowledged\n");
        ok(!FltIsEcpFromUserMode(Filter, Context), "New ECP is from user mode\n");
        FltAcknowledgeEcp(Filter, Context);
        ok(FltIsEcpAcknowledged(Filter, Context), "ECP is not acknowledged\n");
        FltPrepareToReuseEcp(Filter, Context);
        ok(!FltIsEcpAcknowledged(Filter, Context), "ECP is still acknowledged\n");

        Status = FltInsertExtraCreateParameter(Filter, EcpList, Context);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = FltInsertExtraCreateParameter(Filter, EcpList, Second);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

        Found = NULL;
        Size = 0;
        Status = FltFindExtraCreateParameter(Filter, EcpList, &TestEcpType, &Found, &Size);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Found, Context);
        ok_eq_ulong(Size, (ULONG)sizeof(ULONG));

        Found = NULL;
        RtlZeroMemory(&Type, sizeof(Type));
        Status = FltGetNextExtraCreateParameter(Filter, EcpList, NULL, &Type, &Found, &Size);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Found, Context);
        ok(RtlEqualMemory(&Type, &TestEcpType, sizeof(GUID)), "Unexpected ECP type\n");
        Status = FltGetNextExtraCreateParameter(Filter, EcpList, Context, NULL, &Found, &Size);
        ok_eq_hex(Status, STATUS_NOT_FOUND);

        ok_eq_long(EcpCleaned, 0L);
        FltFreeExtraCreateParameter(Filter, Second);
        ok_eq_long(EcpCleaned, 1L);

        Found = NULL;
        Status = FltRemoveExtraCreateParameter(Filter, EcpList, &TestEcpType, &Found, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Found, Context);
        Status = FltFindExtraCreateParameter(Filter, EcpList, &TestEcpType, NULL, NULL);
        ok_eq_hex(Status, STATUS_NOT_FOUND);

        Status = FltInsertExtraCreateParameter(Filter, EcpList, Context);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    FltFreeExtraCreateParameterList(Filter, EcpList);
    ok_eq_long(EcpCleaned, 2L);
}

static
VOID
FLTAPI
AsyncReadComplete(
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _In_ PFLT_CONTEXT Context)
{
    ok_eq_pointer(Context, CTX_WRITE);
    ok_eq_hex(CallbackData->IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(CallbackData->IoStatus.Information, (ULONG_PTR)4096);
    InterlockedIncrement(&AsyncDone);
    KeSetEvent(&AsyncEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
TestNames(
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ BOOLEAN Opened)
{
    static CONST FLT_FILE_NAME_OPTIONS Formats[] = {FLT_FILE_NAME_OPENED, FLT_FILE_NAME_NORMALIZED};
    UNICODE_STRING Final = RTL_CONSTANT_STRING(L"fltmgrio_test.txt");
    UNICODE_STRING Extension = RTL_CONSTANT_STRING(L"txt");
    UNICODE_STRING Device = RTL_CONSTANT_STRING(L"\\Device\\");
    PFLT_FILE_NAME_INFORMATION Info, Unsafe;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Formats); Index++)
    {
        Info = NULL;
        Status = FltGetFileNameInformation(Data, Formats[Index] | FLT_FILE_NAME_QUERY_DEFAULT, &Info);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
        {
            continue;
        }

        ok_eq_ulong((ULONG)Info->Size, (ULONG)sizeof(FLT_FILE_NAME_INFORMATION));
        ok_eq_hex(Info->Format, Formats[Index]);
        ok_eq_hex(Info->NamesParsed,
                  Opened ? (FLTFL_FILE_NAME_PARSED_FINAL_COMPONENT | FLTFL_FILE_NAME_PARSED_EXTENSION |
                            FLTFL_FILE_NAME_PARSED_STREAM | FLTFL_FILE_NAME_PARSED_PARENT_DIR)
                         : 0);
        ok(RtlPrefixUnicodeString(&Device, &Info->Name, TRUE), "Name = %wZ\n", &Info->Name);
        ok(Info->Volume.Length != 0 && Info->Volume.Buffer == Info->Name.Buffer, "Volume = %wZ\n", &Info->Volume);

        Status = FltParseFileNameInformation(Info);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(Info->NamesParsed,
                  FLTFL_FILE_NAME_PARSED_FINAL_COMPONENT | FLTFL_FILE_NAME_PARSED_EXTENSION |
                  FLTFL_FILE_NAME_PARSED_STREAM | FLTFL_FILE_NAME_PARSED_PARENT_DIR);
        ok(RtlEqualUnicodeString(&Info->FinalComponent, &Final, TRUE), "FinalComponent = %wZ\n", &Info->FinalComponent);
        ok(RtlEqualUnicodeString(&Info->Extension, &Extension, TRUE), "Extension = %wZ\n", &Info->Extension);
        ok_eq_ulong((ULONG)Info->Stream.Length, 0UL);
        ok(Info->ParentDir.Length >= sizeof(WCHAR) &&
           Info->ParentDir.Buffer[0] == L'\\' &&
           Info->ParentDir.Buffer[Info->ParentDir.Length / sizeof(WCHAR) - 1] == L'\\',
           "ParentDir = %wZ\n", &Info->ParentDir);
        ok_eq_ulong((ULONG)Info->Name.Length,
                    (ULONG)(Info->Volume.Length + Info->Share.Length + Info->ParentDir.Length +
                            Info->FinalComponent.Length + Info->Stream.Length));

        if (Opened)
        {
            Unsafe = NULL;
            Status = FltGetFileNameInformationUnsafe(FltObjects->FileObject,
                                                     FltObjects->Instance,
                                                     Formats[Index] | FLT_FILE_NAME_QUERY_DEFAULT,
                                                     &Unsafe);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok(RtlEqualUnicodeString(&Unsafe->Name, &Info->Name, TRUE), "%wZ != %wZ\n", &Unsafe->Name, &Info->Name);
                FltReleaseFileNameInformation(Unsafe);
            }
        }

        FltReferenceFileNameInformation(Info);
        FltReleaseFileNameInformation(Info);
        FltReleaseFileNameInformation(Info);
    }

    Info = NULL;
    Status = FltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_CACHE_ONLY, &Info);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Info != NULL, "No cached name\n");
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(Info->Format, FLT_FILE_NAME_OPENED);
        FltReleaseFileNameInformation(Info);
    }
}

static
VOID
TestGeneratedIo(
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    static WCHAR Leaf[] = L"fltmgrio_gen.bin";
    PFLT_FILE_NAME_INFORMATION Info = NULL;
    FILE_STANDARD_INFORMATION Standard;
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    PFLT_CALLBACK_DATA NewData;
    PFILE_OBJECT FileObject = NULL;
    HANDLE Handle = NULL;
    LARGE_INTEGER Offset, Timeout;
    UNICODE_STRING Name;
    PUCHAR Buffer, Readback;
    BOOLEAN Directory;
    ULONG Bytes, Index, Returned;
    NTSTATUS Status;

    Status = FltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT, &Info);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }
    (VOID)FltParseFileNameInformation(Info);

    Name.Length = 0;
    Name.MaximumLength = Info->Name.Length + sizeof(Leaf);
    Name.Buffer = ExAllocatePoolWithTag(PagedPool, Name.MaximumLength, TEST_TAG);
    Buffer = FltAllocatePoolAlignedWithTag(FltObjects->Instance, NonPagedPool, 4096, TEST_TAG);
    Readback = FltAllocatePoolAlignedWithTag(FltObjects->Instance, NonPagedPool, 4096, TEST_TAG);
    ok(Name.Buffer != NULL && Buffer != NULL && Readback != NULL, "Allocation failed\n");
    if (Name.Buffer == NULL || Buffer == NULL || Readback == NULL)
    {
        goto Cleanup;
    }
    ok(((ULONG_PTR)Buffer & (PAGE_SIZE - 1)) == 0, "Buffer %p is not aligned\n", Buffer);

    RtlCopyMemory(Name.Buffer, Info->Name.Buffer, Info->Name.Length - Info->FinalComponent.Length);
    Name.Length = Info->Name.Length - Info->FinalComponent.Length;
    RtlAppendUnicodeToString(&Name, Leaf);

    for (Index = 0; Index < 4096; Index++)
    {
        Buffer[Index] = (UCHAR)(Index * 13);
    }

    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = FltCreateFileEx(Filter,
                             FltObjects->Instance,
                             &Handle,
                             &FileObject,
                             GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                             &ObjectAttributes,
                             &IoStatusBlock,
                             NULL,
                             FILE_ATTRIBUTE_NORMAL,
                             0,
                             FILE_OVERWRITE_IF,
                             FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_DELETE_ON_CLOSE,
                             NULL,
                             0,
                             0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(GeneratedSeen, 0L);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    ok(FileObject != NULL, "No file object\n");

    Offset.QuadPart = 0;
    Bytes = 0;
    Status = FltWriteFile(FltObjects->Instance, FileObject, &Offset, 4096, Buffer, 0, &Bytes, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Bytes, 4096UL);

    Bytes = 0;
    RtlZeroMemory(Readback, 4096);
    Status = FltReadFile(FltObjects->Instance,
                         FileObject,
                         &Offset,
                         4096,
                         Readback,
                         FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET,
                         &Bytes,
                         NULL,
                         NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Bytes, 4096UL);
    ok(RtlEqualMemory(Readback, Buffer, 4096), "Data read back differs\n");

    RtlZeroMemory(Readback, 4096);
    KeClearEvent(&AsyncEvent);
    Status = FltReadFile(FltObjects->Instance, FileObject, &Offset, 4096, Readback, 0, NULL, AsyncReadComplete, CTX_WRITE);
    ok_eq_hex(Status, STATUS_PENDING);
    if (Status == STATUS_PENDING)
    {
        Timeout.QuadPart = -10 * 10000000LL;
        Status = KeWaitForSingleObject(&AsyncEvent, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_long(AsyncDone, 1L);
        ok(RtlEqualMemory(Readback, Buffer, 4096), "Asynchronous data differs\n");
    }

    Returned = 0;
    RtlZeroMemory(&Standard, sizeof(Standard));
    Status = FltQueryInformationFile(FltObjects->Instance,
                                     FileObject,
                                     &Standard,
                                     sizeof(Standard),
                                     FileStandardInformation,
                                     &Returned);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Returned, (ULONG)sizeof(Standard));
    ok_eq_longlong(Standard.EndOfFile.QuadPart, 4096LL);
    ok_eq_bool(Standard.Directory, FALSE);

    Directory = TRUE;
    Status = FltIsDirectory(FileObject, FltObjects->Instance, &Directory);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_bool(Directory, FALSE);

    EndOfFile.EndOfFile.QuadPart = 100;
    Status = FltSetInformationFile(FltObjects->Instance, FileObject, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = FltAllocateCallbackData(FltObjects->Instance, FileObject, &NewData);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < 2; Index++)
        {
            RtlZeroMemory(&Standard, sizeof(Standard));
            NewData->Iopb->MajorFunction = IRP_MJ_QUERY_INFORMATION;
            NewData->Iopb->Parameters.QueryFileInformation.Length = sizeof(Standard);
            NewData->Iopb->Parameters.QueryFileInformation.FileInformationClass = FileStandardInformation;
            NewData->Iopb->Parameters.QueryFileInformation.InfoBuffer = &Standard;
            ok_eq_pointer(NewData->Iopb->TargetInstance, NULL);
            ok_eq_pointer(NewData->Iopb->TargetFileObject, NULL);
            ok(FLT_IS_IRP_OPERATION(NewData), "Not an IRP operation\n");

            FltPerformSynchronousIo(NewData);
            ok_eq_hex(NewData->IoStatus.Status, STATUS_SUCCESS);
            ok_eq_longlong(Standard.EndOfFile.QuadPart, 100LL);

            FltReuseCallbackData(NewData);
        }
        FltFreeCallbackData(NewData);
    }

    Status = FltFlushBuffers(FltObjects->Instance, FileObject);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InterlockedIncrement(&GeneratedDone);

Cleanup:
    if (FileObject != NULL)
    {
        ObDereferenceObject(FileObject);
    }
    if (Handle != NULL)
    {
        Status = FltClose(Handle);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    if (Readback != NULL)
    {
        FltFreePoolAlignedWithTag(FltObjects->Instance, Readback, TEST_TAG);
    }
    if (Buffer != NULL)
    {
        FltFreePoolAlignedWithTag(FltObjects->Instance, Buffer, TEST_TAG);
    }
    if (Name.Buffer != NULL)
    {
        ExFreePoolWithTag(Name.Buffer, TEST_TAG);
    }
    FltReleaseFileNameInformation(Info);
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreCreateCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Outptr_result_maybenull_ PVOID *CompletionContext)
{
    if (IsTestName(FltObjects->FileObject, L"fltmgrio_deny.txt"))
    {
        InterlockedIncrement(&Denied);
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    if (IsTestName(FltObjects->FileObject, L"fltmgrio_gen.bin"))
    {
        InterlockedIncrement(&GeneratedSeen);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    if (!IsTestName(FltObjects->FileObject, L"fltmgrio_test.txt"))
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    InterlockedIncrement(&PreCreate);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_ulong((ULONG)FltObjects->Size, (ULONG)sizeof(FLT_RELATED_OBJECTS));
    ok_eq_pointer(FltObjects->Filter, Filter);
    ok_eq_pointer(Data->Iopb->TargetInstance, FltObjects->Instance);
    ok_eq_pointer(Data->Iopb->TargetFileObject, FltObjects->FileObject);
    ok_eq_ulong((ULONG)Data->Iopb->MajorFunction, (ULONG)IRP_MJ_CREATE);
    ok(FLT_IS_IRP_OPERATION(Data), "Create is not IRP based\n");
    ok(!FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_POST_OPERATION), "Post flag set in pre\n");
    ok_eq_pointer(Data->Thread, PsGetCurrentThread());
    ok(Data->Iopb->Parameters.Create.SecurityContext != NULL, "No security context\n");
    ok(FltIsOperationSynchronous(Data), "Create is not synchronous\n");
    ok_eq_pointer(FltGetRequestorProcess(Data), PsGetCurrentProcess());

    {
        PECP_LIST EcpList = (PECP_LIST)1;
        NTSTATUS Status = FltGetEcpListFromCallbackData(Filter, Data, &EcpList);

        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(EcpList, NULL);
    }
    TestNames(Data, FltObjects, FALSE);

    *CompletionContext = CTX_CREATE;
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

static
FLT_POSTOP_CALLBACK_STATUS
FLTAPI
PostCreateCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    PTEST_CONTEXT Context, Found;
    PFLT_CONTEXT Old;
    NTSTATUS Status;

    InterlockedIncrement(&PostCreate);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_pointer(CompletionContext, CTX_CREATE);
    ok_eq_hex(Flags, 0);
    ok_eq_pointer(Data->Thread, PsGetCurrentThread());
    ok(FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_POST_OPERATION), "Post flag not set\n");

    if (!NT_SUCCESS(Data->IoStatus.Status) || Data->IoStatus.Status == STATUS_REPARSE)
    {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    TestNames(Data, FltObjects, TRUE);
    if (InterlockedCompareExchange(&GeneratedDone, 0, 0) == 0)
    {
        TestGeneratedIo(Data, FltObjects);
    }

    ok(FltSupportsStreamContexts(FltObjects->FileObject), "Stream contexts not supported\n");
    ok(FltSupportsStreamHandleContexts(FltObjects->FileObject), "Stream handle contexts not supported\n");

    Status = FltGetStreamHandleContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT *)&Found);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    if (NT_SUCCESS(Status))
    {
        FltReleaseContext(Found);
    }

    Status = AllocateTestContext(FLT_STREAMHANDLE_CONTEXT, PagedPool, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = FltSetStreamHandleContext(FltObjects->Instance,
                                           FltObjects->FileObject,
                                           FLT_SET_CONTEXT_KEEP_IF_EXISTS,
                                           Context,
                                           NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        FltReleaseContext(Context);
    }

    Status = AllocateTestContext(FLT_STREAM_CONTEXT, PagedPool, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Old = NULL;
        Status = FltSetStreamContext(FltObjects->Instance,
                                     FltObjects->FileObject,
                                     FLT_SET_CONTEXT_KEEP_IF_EXISTS,
                                     Context,
                                     &Old);
        ok(Status == STATUS_SUCCESS || Status == STATUS_FLT_CONTEXT_ALREADY_DEFINED, "Status = 0x%lx\n", Status);
        if (Old != NULL)
        {
            FltReleaseContext(Old);
        }
        FltReleaseContext(Context);
    }

    if (FltSupportsFileContextsEx(FltObjects->FileObject, FltObjects->Instance))
    {
        Status = AllocateTestContext(FLT_FILE_CONTEXT, PagedPool, &Context);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Old = NULL;
            Status = FltSetFileContext(FltObjects->Instance,
                                       FltObjects->FileObject,
                                       FLT_SET_CONTEXT_KEEP_IF_EXISTS,
                                       Context,
                                       &Old);
            ok(Status == STATUS_SUCCESS || Status == STATUS_FLT_CONTEXT_ALREADY_DEFINED, "Status = 0x%lx\n", Status);
            if (Old != NULL)
            {
                FltReleaseContext(Old);
            }
            FltReleaseContext(Context);
        }
    }
    else
    {
        Status = FltGetFileContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT *)&Found);
        ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
        if (NT_SUCCESS(Status))
        {
            FltReleaseContext(Found);
        }
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreWriteCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Outptr_result_maybenull_ PVOID *CompletionContext)
{
    if (!IsTestHandle(FltObjects))
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    InterlockedIncrement(&PreWrite);
    ok_eq_ulong(Data->Iopb->Parameters.Write.Length, 4096UL);
    *CompletionContext = CTX_WRITE;
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

static
FLT_POSTOP_CALLBACK_STATUS
FLTAPI
PostWriteCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);

    InterlockedIncrement(&PostWrite);
    ok_eq_pointer(CompletionContext, CTX_WRITE);
    ok_eq_hex(Flags, 0);
    ok_eq_hex(Data->IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(Data->IoStatus.Information, (ULONG_PTR)4096);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreReadCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Outptr_result_maybenull_ PVOID *CompletionContext)
{
    UNREFERENCED_PARAMETER(Data);

    if (!IsTestHandle(FltObjects))
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    InterlockedIncrement(&PreRead);
    *CompletionContext = PsGetCurrentThread();
    return FLT_PREOP_SYNCHRONIZE;
}

static
FLT_POSTOP_CALLBACK_STATUS
FLTAPI
PostReadCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);

    InterlockedIncrement(&PostRead);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_pointer(CompletionContext, PsGetCurrentThread());
    ok_eq_hex(Flags, 0);
    ok_eq_hex(Data->IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(Data->IoStatus.Information, (ULONG_PTR)4096);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreSetInfoCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Outptr_result_maybenull_ PVOID *CompletionContext)
{
    if (!IsTestHandle(FltObjects))
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    InterlockedIncrement(&PreSetInfo);
    ok(FLT_IS_IRP_OPERATION(Data), "Set information is not IRP based\n");

    if (FLT_IS_IRP_OPERATION(Data) && QueuePendedPre(Data, FltObjects->Instance))
    {
        InterlockedIncrement(&PendedPre);
        return FLT_PREOP_PENDING;
    }

    *CompletionContext = CTX_SETINFO;
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

static
FLT_POSTOP_CALLBACK_STATUS
FLTAPI
PostSetInfoCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);

    InterlockedIncrement(&PostSetInfo);
    ok_eq_pointer(CompletionContext, CTX_SETINFO);
    ok_eq_hex(Flags, 0);
    ok_eq_hex(Data->IoStatus.Status, STATUS_SUCCESS);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreQueryCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Outptr_result_maybenull_ PVOID *CompletionContext)
{
    UNREFERENCED_PARAMETER(Data);

    if (!IsTestHandle(FltObjects))
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    InterlockedIncrement(&PreQuery);
    *CompletionContext = CTX_QUERY;
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

static
FLT_POSTOP_CALLBACK_STATUS
FLTAPI
PostQueryCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);

    InterlockedIncrement(&PostQuery);
    ok_eq_pointer(CompletionContext, CTX_QUERY);
    ok_eq_hex(Flags, 0);

    if (FLT_IS_IRP_OPERATION(Data) && QueuePendedPost(Data))
    {
        InterlockedIncrement(&PendedPost);
        return FLT_POSTOP_MORE_PROCESSING_REQUIRED;
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreCleanupCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Outptr_result_maybenull_ PVOID *CompletionContext)
{
    FLT_RELATED_CONTEXTS Contexts;
    PTEST_CONTEXT Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (!IsTestHandle(FltObjects))
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    InterlockedIncrement(&PreCleanup);

    Status = FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT *)&Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(Context->Magic, TEST_MAGIC);
        ok_eq_ulong(Context->Kind, ContextKind(FLT_STREAM_CONTEXT));
        FltReleaseContext(Context);
    }

    RtlFillMemory(&Contexts, sizeof(Contexts), 0xFF);
    FltGetContexts(FltObjects, FLT_ALL_CONTEXTS, &Contexts);
    ok(Contexts.VolumeContext != NULL, "No volume context\n");
    ok(Contexts.InstanceContext != NULL, "No instance context\n");
    ok(Contexts.StreamContext != NULL, "No stream context\n");
    ok(Contexts.StreamHandleContext != NULL, "No stream handle context\n");
    ok_eq_pointer(Contexts.TransactionContext, NULL);
    FltReleaseContexts(&Contexts);
    ok_eq_pointer(Contexts.VolumeContext, NULL);
    ok_eq_pointer(Contexts.StreamHandleContext, NULL);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreCloseCallback(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Outptr_result_maybenull_ PVOID *CompletionContext)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (IsTestHandle(FltObjects))
    {
        InterlockedIncrement(&PreClose);
    }
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static CONST FLT_OPERATION_REGISTRATION Operations[] =
{
    { IRP_MJ_CREATE, 0, PreCreateCallback, PostCreateCallback },
    { IRP_MJ_WRITE, FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO, PreWriteCallback, PostWriteCallback },
    { IRP_MJ_READ, FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO, PreReadCallback, PostReadCallback },
    { IRP_MJ_SET_INFORMATION, 0, PreSetInfoCallback, PostSetInfoCallback },
    { IRP_MJ_QUERY_INFORMATION, 0, PreQueryCallback, PostQueryCallback },
    { IRP_MJ_CLEANUP, 0, PreCleanupCallback, NULL },
    { IRP_MJ_CLOSE, 0, PreCloseCallback, NULL },
    { IRP_MJ_OPERATION_END }
};

static CONST FLT_CONTEXT_REGISTRATION ContextRegistration[] =
{
    { FLT_VOLUME_CONTEXT, 0, ContextCleanup, sizeof(TEST_CONTEXT), TEST_TAG },
    { FLT_INSTANCE_CONTEXT, 0, ContextCleanup, sizeof(TEST_CONTEXT), TEST_TAG },
    { FLT_FILE_CONTEXT, 0, ContextCleanup, sizeof(TEST_CONTEXT), TEST_TAG },
    { FLT_STREAM_CONTEXT, 0, ContextCleanup, sizeof(TEST_CONTEXT), TEST_TAG },
    { FLT_STREAMHANDLE_CONTEXT, 0, ContextCleanup, sizeof(TEST_CONTEXT), TEST_TAG },
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
    InstanceTeardownStart,
    InstanceTeardownComplete,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static
NTSTATUS
FLTAPI
PortConnect(
    _In_ PFLT_PORT Port,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID *ConnectionPortCookie)
{
    InterlockedIncrement(&ConnectCount);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_pointer(ServerPortCookie, &ServerPort);
    ok_eq_ulong(SizeOfContext, (ULONG)sizeof(FLTIO_CONNECT_CONTEXT));
    ok(ConnectionContext != NULL &&
       SizeOfContext == sizeof(FLTIO_CONNECT_CONTEXT) &&
       RtlEqualMemory(ConnectionContext, FLTIO_CONNECT_CONTEXT, sizeof(FLTIO_CONNECT_CONTEXT)),
       "Unexpected connection context\n");

    ClientPort = Port;
    *ConnectionPortCookie = PORT_COOKIE;
    return STATUS_SUCCESS;
}

static
VOID
FLTAPI
PortDisconnect(
    _In_opt_ PVOID ConnectionCookie)
{
    InterlockedIncrement(&DisconnectCount);
    ok_eq_pointer(ConnectionCookie, PORT_COOKIE);
    FltCloseClientPort(Filter, &ClientPort);
    ok_eq_pointer(ClientPort, NULL);
}

static
NTSTATUS
FLTAPI
PortMessage(
    _In_opt_ PVOID PortCookie,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ReturnOutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_ PULONG ReturnOutputBufferLength)
{
    FLTIO_MESSAGE Input, Output, Sent, Reply;
    LARGE_INTEGER Timeout;
    ULONG ReplyLength;
    NTSTATUS Status = STATUS_SUCCESS;

    InterlockedIncrement(&MessageCount);
    ok_eq_pointer(PortCookie, PORT_COOKIE);
    *ReturnOutputBufferLength = 0;

    if (InputBuffer == NULL || InputBufferLength < sizeof(Input) ||
        OutputBuffer == NULL || OutputBufferLength < sizeof(Output))
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        ProbeForRead(InputBuffer, sizeof(Input), 1);
        RtlCopyMemory(&Input, InputBuffer, sizeof(Input));
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

    Output.Command = Input.Command;
    Output.Value = 0;

    switch (Input.Command)
    {
        case FLTIO_MSG_PING:
            Output.Value = Input.Value + 1;
            break;

        case FLTIO_MSG_SEND:
            Sent.Command = FLTIO_MSG_SEND;
            Sent.Value = Input.Value + 100;
            Reply.Command = 0;
            Reply.Value = 0;
            ReplyLength = sizeof(Reply);
            Timeout.QuadPart = -10 * 10000000LL;
            Status = FltSendMessage(Filter, &ClientPort, &Sent, sizeof(Sent), &Reply, &ReplyLength, &Timeout);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(ReplyLength, (ULONG)sizeof(Reply));
            ok_eq_ulong(Reply.Command, (ULONG)FLTIO_MSG_SEND);
            ok_eq_ulong(Reply.Value, Sent.Value + 1);
            Output.Value = (ULONG)Status;
            Status = STATUS_SUCCESS;
            break;

        case FLTIO_MSG_SEND_NOREPLY:
            Sent.Command = FLTIO_MSG_SEND_NOREPLY;
            Sent.Value = Input.Value + 100;
            Timeout.QuadPart = 0;
            Status = FltSendMessage(Filter, &ClientPort, &Sent, sizeof(Sent), NULL, NULL, &Timeout);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Output.Value = (ULONG)Status;
            Status = STATUS_SUCCESS;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        ProbeForWrite(OutputBuffer, sizeof(Output), 1);
        RtlCopyMemory(OutputBuffer, &Output, sizeof(Output));
        *ReturnOutputBufferLength = sizeof(Output);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
NTSTATUS
WriteInstanceRegistry(VOID)
{
    static WCHAR InstanceName[] = L"FltMgrIo Instance";
    static WCHAR Altitude[] = L"370123";
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
VOID
TestEnumeration(VOID)
{
    PFLT_VOLUME Volumes[32];
    PFLT_INSTANCE Instances[32];
    PFLT_FILTER Filters[32];
    PFLT_FILTER Found;
    UNICODE_STRING Name;
    UCHAR Buffer[512];
    PFLT_VOLUME_PROPERTIES Properties = (PFLT_VOLUME_PROPERTIES)Buffer;
    ULONG Count, Index, Returned, Needed;
    BOOLEAN Seen = FALSE;
    NTSTATUS Status;

    Count = 0;
    Status = FltEnumerateVolumes(Filter, Volumes, RTL_NUMBER_OF(Volumes), &Count);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Count >= 1, "No volumes\n");
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Needed = 0;
        Status = FltGetVolumeName(Volumes[Index], NULL, &Needed);
        ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
        ok(Needed != 0, "No volume name length\n");

        Returned = 0;
        Status = FltGetVolumeProperties(Volumes[Index], Properties, sizeof(Buffer), &Returned);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(Returned >= sizeof(FLT_VOLUME_PROPERTIES), "Returned = %lu\n", Returned);
        Status = STATUS_SUCCESS;

        FltObjectDereference(Volumes[Index]);
    }

    Count = 0;
    Status = FltEnumerateVolumes(Filter, Volumes, 0, &Count);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok(Count >= 1, "No volumes\n");

    Count = 0;
    Status = FltEnumerateInstances(NULL, Filter, Instances, RTL_NUMBER_OF(Instances), &Count);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Count, (ULONG)AttachedCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        FltObjectDereference(Instances[Index]);
    }

    Count = 0;
    Status = FltEnumerateFilters(Filters, RTL_NUMBER_OF(Filters), &Count);
    ok_eq_hex(Status, STATUS_SUCCESS);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        if (Filters[Index] == Filter)
        {
            Seen = TRUE;
        }
        FltObjectDereference(Filters[Index]);
    }
    ok(Seen, "Filter not enumerated\n");

    RtlInitUnicodeString(&Name, L"Kmtest-FltMgrIo");
    Status = FltGetFilterFromName(&Name, &Found);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Found, Filter);
        FltObjectDereference(Found);
    }

    RtlInitUnicodeString(&Name, L"Kmtest-NoSuchFilter");
    Status = FltGetFilterFromName(&Name, &Found);
    ok_eq_hex(Status, STATUS_FLT_FILTER_NOT_FOUND);
}

static
VOID
TestRegister(VOID)
{
    UNICODE_STRING PortName = RTL_CONSTANT_STRING(FLTIO_PORT_NAME);
    OBJECT_ATTRIBUTES ObjectAttributes;
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    LARGE_INTEGER Timeout;
    PVOID Routine;
    PFLT_PORT Port;
    NTSTATUS Status;

    Status = WriteInstanceRegistry();
    ok_eq_hex(Status, STATUS_SUCCESS);

    KeInitializeSpinLock(&QueueLock);
    InitializeListHead(&QueueList);
    KeInitializeEvent(&AsyncEvent, NotificationEvent, FALSE);

    Status = FltRegisterFilter(TestDriverObject, &Registration, &Filter);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        Filter = NULL;
        return;
    }

    Status = FltBuildDefaultSecurityDescriptor(&SecurityDescriptor, FLT_PORT_ALL_ACCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes, &PortName, OBJ_CASE_INSENSITIVE, NULL, SecurityDescriptor);
        Status = FltCreateCommunicationPort(Filter, &Port, &ObjectAttributes, &ServerPort, PortConnect, PortDisconnect, PortMessage, 1);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        if (NT_SUCCESS(Status))
        {
            FltCloseCommunicationPort(Port);
        }

        InitializeObjectAttributes(&ObjectAttributes,
                                   &PortName,
                                   OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                   NULL,
                                   SecurityDescriptor);
        Status = FltCreateCommunicationPort(Filter, &ServerPort, &ObjectAttributes, &ServerPort, PortConnect, PortDisconnect, PortMessage, 1);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = FltCreateCommunicationPort(Filter, &Port, &ObjectAttributes, &ServerPort, PortConnect, PortDisconnect, PortMessage, 1);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_COLLISION);
        if (NT_SUCCESS(Status))
        {
            FltCloseCommunicationPort(Port);
        }

        FltFreeSecurityDescriptor(SecurityDescriptor);
    }

    Routine = FltGetRoutineAddress("FltCreateFileEx2");
    ok(Routine != NULL, "FltCreateFileEx2 not found\n");
    Routine = FltGetRoutineAddress("FltNoSuchRoutine");
    ok_eq_pointer(Routine, NULL);

    KeSetEvent(&AsyncEvent, IO_NO_INCREMENT, FALSE);
    Status = FltCancellableWaitForSingleObject(&AsyncEvent, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    KeClearEvent(&AsyncEvent);
    Timeout.QuadPart = 0;
    Status = FltCancellableWaitForSingleObject(&AsyncEvent, &Timeout, NULL);
    ok_eq_hex(Status, STATUS_TIMEOUT);

    TestEcps();

    Status = FltStartFiltering(Filter);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = FltStartFiltering(Filter);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    ok(SetupCount >= 1, "InstanceSetup was not called\n");
    ok(AttachedCount >= 1, "No instance attached\n");
    TestEnumeration();
}

static
VOID
TestCheck(VOID)
{
    ok(PreCreate >= 1, "PreCreate = %ld\n", PreCreate);
    ok_eq_long(PostCreate, PreCreate);
    ok_eq_long(Denied, 1L);
    ok(PreWrite >= 1, "PreWrite = %ld\n", PreWrite);
    ok_eq_long(PostWrite, PreWrite);
    ok(PreRead >= 1, "PreRead = %ld\n", PreRead);
    ok_eq_long(PostRead, PreRead);
    ok(PreSetInfo >= 1, "PreSetInfo = %ld\n", PreSetInfo);
    ok_eq_long(PostSetInfo, PreSetInfo);
    ok_eq_long(PendedPre, PreSetInfo);
    ok(PreQuery >= 1, "PreQuery = %ld\n", PreQuery);
    ok_eq_long(PostQuery, PreQuery);
    ok(PendedPost >= 1, "PendedPost = %ld\n", PendedPost);
    ok_eq_long(DeferredRuns, PendedPost);
    ok(WorkerRuns >= 1, "WorkerRuns = %ld\n", WorkerRuns);
    ok_eq_long(GeneratedSeen, 0L);
    ok_eq_long(GeneratedDone, 1L);
    ok_eq_long(AsyncDone, 1L);
    ok(PreCleanup >= 1, "PreCleanup = %ld\n", PreCleanup);
    ok_eq_long(PreClose, PreCleanup);
    ok_eq_long(ContextCleaned[4], ContextAllocated[4]);
    ok_eq_long(ConnectCount, 1L);
    ok_eq_long(DisconnectCount, 1L);
    ok_eq_long(MessageCount, 3L);
    ok_eq_pointer(ClientPort, NULL);
}


static
VOID
TestUnregister(VOID)
{
    PFLT_CALLBACK_DATA Removed;
    ULONG Kind;

    if (Filter == NULL)
    {
        return;
    }

    if (ServerPort != NULL)
    {
        FltCloseCommunicationPort(ServerPort);
        ServerPort = NULL;
    }

    if (QueueInstance != NULL)
    {
        FltCbdqDisable(&Queue);
        Removed = FltCbdqRemoveNextIo(&Queue, NULL);
        ok_eq_pointer(Removed, NULL);
        FltCbdqEnable(&Queue);
        QueueInstance = NULL;
    }

    FltUnregisterFilter(Filter);
    Filter = NULL;

    ok_eq_long(TeardownStartCount, AttachedCount);
    ok_eq_long(TeardownCompleteCount, AttachedCount);
    for (Kind = 0; Kind < CONTEXT_KINDS; Kind++)
    {
        ok(ContextCleaned[Kind] == ContextAllocated[Kind],
           "Kind %lu: %ld cleaned, %ld allocated\n", Kind, ContextCleaned[Kind], ContextAllocated[Kind]);
    }
    ok(ContextAllocated[0] >= 1 && ContextAllocated[1] >= 2 && ContextAllocated[3] >= 1 && ContextAllocated[4] >= 1,
       "Contexts were not allocated\n");
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

    *DeviceName = L"FltMgrIo";
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
        case IOCTL_FLTIO_REGISTER:
            TestRegister();
            break;

        case IOCTL_FLTIO_CHECK:
            TestCheck();
            break;

        case IOCTL_FLTIO_UNREGISTER:
            TestUnregister();
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}
