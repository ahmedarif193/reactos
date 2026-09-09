/*
 * PROJECT:     ReactOS Win32k
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Composition connections, producer commands and resource ownership
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <win32k.h>
#include "composition.h"
#include "dcomposition.h"
#include <reactos/dcompbatch.h>

#define DCOMP_TAG 'pmCD'

typedef struct _DCOMP_RESOURCE
{
    LIST_ENTRY Entry;
    LIST_ENTRY DirtyEntry;
    UINT Identifier;
    UINT Type;
    struct _DCOMP_RESOURCE *Parent;
    LIST_ENTRY Children;
    LIST_ENTRY Sibling;
    UINT FloatValues[12];
    UINT FloatProperties;
    UINT IntegerValues[19];
    UINT IntegerProperties;
    UINT InternalId;
    UINT DirtyFloats;
    UINT DirtyIntegers;
    BOOLEAN Published;
    BOOLEAN ChildrenDirty;
} DCOMP_RESOURCE;

typedef struct _DCOMP_RESOURCE_ENTRY
{
    UINT Identifier;
    DCOMP_RESOURCE *Resource;
} DCOMP_RESOURCE_ENTRY;

typedef enum _DCOMP_BATCH_TYPE
{
    DCompBatchOpen = 5,
    DCompBatchClose = 6,
    DCompBatchCommands = 7
} DCOMP_BATCH_TYPE;

/* The connection owns published batches. A live producer keeps a second
 * link to its commands, but retiring that producer must not discard them. */
typedef struct _DCOMP_BATCH
{
    DXGK_COMPOSITION_BATCH_LIST Interface;
    struct _DCOMP_BATCH *Next;
    struct _DCOMP_CONNECTION *Connection;
    struct _DCOMP_CHANNEL *Producer;
    ULONGLONG Generation;
    LIST_ENTRY ConnectionEntry;
    LIST_ENTRY ChannelEntry;
    DCOMP_BATCH_TYPE Type;
    BOOLEAN Returned;
    BOOLEAN Discarded;
    UINT ChannelId;
    UINT CommitId;
    ULONG Flags;
    UINT ByteCount;
    ULONG ProcessId;
    ULONGLONG ProcessSequence;
    PKEVENT Event;
    UCHAR Commands[ANYSIZE_ARRAY];
} DCOMP_BATCH;

C_ASSERT((FIELD_OFFSET(DCOMP_BATCH, Commands) & (sizeof(UINT) - 1)) == 0);

typedef struct _DCOMP_CONNECTION
{
    LIST_ENTRY Entry;
    ULONG SessionId;
    LONG References;
    PEPROCESS Owner;
    PKEVENT Event;
    BOOLEAN ConnectionFlag;
    LIST_ENTRY Batches;
    LIST_ENTRY ReturnedBatches;
    UINT NextChannelId;
    ULONGLONG Generation;
} DCOMP_CONNECTION;

typedef struct _DCOMP_CHANNEL
{
    LIST_ENTRY Entry;
    LONG References;
    UINT Identifier;
    BOOLEAN Listed;
    DCOMP_CONNECTION *Connection;
    PEPROCESS Process;
    PVOID Section;
    PVOID SessionView;
    PVOID UserView;
    UINT SectionSize;
    EX_PUSH_LOCK ResourceLock;
    RTL_AVL_TABLE ResourceTable;
    LIST_ENTRY Resources;
    LIST_ENTRY ChangedResources;
    LIST_ENTRY ReleasedResources;
    LIST_ENTRY Batches;
    UINT NextResourceId;
    UINT ConnectionId;
    UINT CommitSequence;
    UINT PublishedCommitId;
    UINT ReturnedCommitId;
    BOOLEAN DescriptionDirty;
    DCOMP_BATCH *OpenBatch;
    DCOMP_BATCH *CloseBatch;
    PKEVENT Event;
    ULONGLONG OpenGeneration;
} DCOMP_CHANNEL;

typedef struct _DCOMP_PROCESS
{
    LIST_ENTRY Channels;
    DCOMP_CONNECTION *Connection;
    ULONG_PTR ConnectionIdentifier;
} DCOMP_PROCESS;

/* This lock protects identifiers and references, never section mapping or
 * destruction. USER may precede it during compositor/process teardown. */
static EX_PUSH_LOCK g_DCompLock;
static LIST_ENTRY g_DCompConnections = {&g_DCompConnections, &g_DCompConnections};

static VOID DCompDestroyResources(DCOMP_CHANNEL *Channel);
static VOID DCompDereferenceConnection(DCOMP_CONNECTION *Connection);
static VOID DCompLock(VOID);
static VOID DCompUnlock(VOID);
static const DXGK_COMPOSITION_BATCH_LIST_VTABLE DCompBatchVtbl;

static DCOMP_BATCH *
DCompAllocateBatch(DCOMP_BATCH_TYPE Type, UINT ByteCount)
{
    SIZE_T Size = FIELD_OFFSET(DCOMP_BATCH, Commands) + (SIZE_T)ByteCount;
    DCOMP_BATCH *Batch;

    if (Size < ByteCount)
        return NULL;
    Batch = ExAllocatePoolWithQuotaTag(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, Size, DCOMP_TAG);
    if (Batch != NULL)
    {
        RtlZeroMemory(Batch, FIELD_OFFSET(DCOMP_BATCH, Commands));
        Batch->Interface.Vtbl = &DCompBatchVtbl;
        InitializeListHead(&Batch->ConnectionEntry);
        InitializeListHead(&Batch->ChannelEntry);
        Batch->Type = Type;
        Batch->ByteCount = ByteCount;
    }
    return Batch;
}

static VOID
DCompFreeBatch(DCOMP_BATCH *Batch)
{
    DCOMP_CONNECTION *Connection = Batch->Connection;

    if (Batch->Event != NULL)
        ObDereferenceObject(Batch->Event);
    ExFreePoolWithTag(Batch, DCOMP_TAG);
    if (Connection != NULL)
        DCompDereferenceConnection(Connection);
}

/* The caller holds g_DCompLock and owns a connection reference. A detached
 * batch pins the connection independently of the compositor and producer. */
static DXGK_COMPOSITION_BATCH_LIST *
DCompTakeConnectionBatchesLocked(DCOMP_CONNECTION *Connection)
{
    DCOMP_BATCH *First = NULL;
    DCOMP_BATCH **Tail = &First;

    while (!IsListEmpty(&Connection->Batches))
    {
        DCOMP_BATCH *Batch = CONTAINING_RECORD(RemoveHeadList(&Connection->Batches), DCOMP_BATCH, ConnectionEntry);
        InitializeListHead(&Batch->ConnectionEntry);
        ASSERT(Batch->Connection == NULL && Batch->Next == NULL);
        Batch->Connection = Connection;
        Batch->Generation = Connection->Generation;
        ++Connection->References;
        *Tail = Batch;
        Tail = &Batch->Next;
    }
    return First != NULL ? &First->Interface : NULL;
}

static DXGK_COMPOSITION_BATCH_LIST *NTAPI
DCompBatchGetNext(DXGK_COMPOSITION_BATCH_LIST *Interface)
{
    DCOMP_BATCH *Batch = CONTAINING_RECORD(Interface, DCOMP_BATCH, Interface);
    return Batch->Next != NULL ? &Batch->Next->Interface : NULL;
}

static VOID NTAPI
DCompBatchReturn(DXGK_COMPOSITION_BATCH_LIST *Interface, BOOLEAN Discard)
{
    DCOMP_BATCH *Batch = CONTAINING_RECORD(Interface, DCOMP_BATCH, Interface);
    DCOMP_CONNECTION *Connection = Batch->Connection;
    BOOLEAN Connected;
    BOOLEAN Recycle;

    DCompLock();
    ASSERT(Connection != NULL && !Batch->Returned && IsListEmpty(&Batch->ConnectionEntry));
    Connected = Connection->Owner != NULL && Batch->Generation == Connection->Generation;
    Recycle = Connected && Batch->Producer != NULL;
    Batch->Discarded = Discard || !Connected;
    Batch->Returned = TRUE;
    Batch->Next = NULL;
    if (Batch->Producer != NULL)
    {
        if (Connected)
        {
            /* Return progress includes cancelled batches. It is separate
             * from rendering or presentation completion. */
            Batch->Producer->ReturnedCommitId = Batch->CommitId;
            KeSetEvent(Batch->Producer->Event, IO_NO_INCREMENT, FALSE);
        }
        Batch->Producer = NULL;
    }
    RemoveEntryList(&Batch->ChannelEntry);
    InitializeListHead(&Batch->ChannelEntry);
    if (Recycle)
        InsertTailList(&Connection->ReturnedBatches, &Batch->ConnectionEntry);
    DCompUnlock();

    if (!Recycle)
        DCompFreeBatch(Batch);
}

static const DXGK_COMPOSITION_BATCH_LIST_VTABLE DCompBatchVtbl =
{
    DCompBatchGetNext,
    DCompBatchReturn
};

/* All list changes happen under g_DCompLock; pool frees happen outside it. */
static VOID
DCompRetireConnectionBatches(DCOMP_CONNECTION *Connection, PLIST_ENTRY Retired)
{
    DXGK_COMPOSITION_BATCH_LIST *Interface = DCompTakeConnectionBatchesLocked(Connection);

    while (Interface != NULL)
    {
        DCOMP_BATCH *Batch = CONTAINING_RECORD(Interface, DCOMP_BATCH, Interface);
        Interface = Interface->Vtbl->GetNext(Interface);
        Batch->Next = NULL;
        Batch->Producer = NULL;
        RemoveEntryList(&Batch->ChannelEntry);
        InitializeListHead(&Batch->ChannelEntry);
        InsertTailList(Retired, &Batch->ConnectionEntry);
    }
    while (!IsListEmpty(&Connection->ReturnedBatches))
        InsertTailList(Retired, RemoveHeadList(&Connection->ReturnedBatches));
}

static VOID
DCompFreeBatches(PLIST_ENTRY Retired)
{
    while (!IsListEmpty(Retired))
        DCompFreeBatch(CONTAINING_RECORD(RemoveHeadList(Retired), DCOMP_BATCH, ConnectionEntry));
}

static VOID
DCompReapReturnedBatches(DCOMP_CONNECTION *Connection)
{
    LIST_ENTRY Retired;

    InitializeListHead(&Retired);
    DCompLock();
    while (!IsListEmpty(&Connection->ReturnedBatches))
        InsertTailList(&Retired, RemoveHeadList(&Connection->ReturnedBatches));
    DCompUnlock();
    DCompFreeBatches(&Retired);
}

static RTL_GENERIC_COMPARE_RESULTS NTAPI
DCompCompareResource(PRTL_AVL_TABLE Table, PVOID First, PVOID Second)
{
    UINT Left = ((DCOMP_RESOURCE_ENTRY *)First)->Identifier;
    UINT Right = ((DCOMP_RESOURCE_ENTRY *)Second)->Identifier;

    UNREFERENCED_PARAMETER(Table);
    return Left < Right ? GenericLessThan : Left > Right ? GenericGreaterThan : GenericEqual;
}

static PVOID NTAPI
DCompAllocateResourceEntry(PRTL_AVL_TABLE Table, CLONG Size)
{
    UNREFERENCED_PARAMETER(Table);
    return ExAllocatePoolWithQuotaTag(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, Size, DCOMP_TAG);
}

static VOID NTAPI
DCompFreeResourceEntry(PRTL_AVL_TABLE Table, PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Table);
    ExFreePoolWithTag(Buffer, DCOMP_TAG);
}

static VOID
DCompLock(VOID)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_DCompLock);
}

static VOID
DCompUnlock(VOID)
{
    ExReleasePushLockExclusive(&g_DCompLock);
    KeLeaveCriticalRegion();
}

static DCOMP_CHANNEL *
DCompReferenceChannel(UINT Identifier)
{
    PPROCESSINFO ProcessInfo = PsGetCurrentProcessWin32Process();
    DCOMP_PROCESS *Data;
    DCOMP_CHANNEL *Channel = NULL;
    PLIST_ENTRY Entry;

    DCompLock();
    Data = ProcessInfo != NULL ? ProcessInfo->CompositionData : NULL;
    if (Data != NULL)
    {
        for (Entry = Data->Channels.Flink; Entry != &Data->Channels; Entry = Entry->Flink)
        {
            DCOMP_CHANNEL *Current = CONTAINING_RECORD(Entry, DCOMP_CHANNEL, Entry);
            if (Current->Identifier == Identifier)
            {
                Channel = Current;
                InterlockedIncrement(&Channel->References);
                break;
            }
        }
    }
    DCompUnlock();
    return Channel;
}

static DCOMP_PROCESS *
DCompGetProcessLocked(PPROCESSINFO ProcessInfo)
{
    DCOMP_PROCESS *Data;

    if (ProcessInfo == NULL || (ProcessInfo->W32PF_flags & W32PF_TERMINATED))
        return NULL;
    Data = ProcessInfo->CompositionData;
    if (Data == NULL)
    {
        Data = ExAllocatePoolWithQuotaTag(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, sizeof(*Data), DCOMP_TAG);
        if (Data != NULL)
        {
            RtlZeroMemory(Data, sizeof(*Data));
            InitializeListHead(&Data->Channels);
            ProcessInfo->CompositionData = Data;
        }
    }
    return Data;
}

static DCOMP_CONNECTION *
DCompFindConnectionLocked(ULONG SessionId)
{
    PLIST_ENTRY Entry;

    for (Entry = g_DCompConnections.Flink; Entry != &g_DCompConnections; Entry = Entry->Flink)
    {
        DCOMP_CONNECTION *Connection = CONTAINING_RECORD(Entry, DCOMP_CONNECTION, Entry);
        if (Connection->SessionId == SessionId)
            return Connection;
    }
    return NULL;
}

static VOID
DCompDereferenceConnection(DCOMP_CONNECTION *Connection)
{
    BOOLEAN Free = FALSE;

    DCompLock();
    ASSERT(Connection->References > 1);
    if (--Connection->References == 1 && Connection->Owner == NULL)
    {
        /* The disconnected session entry has no producers or temporary
         * references left. Reconnection can otherwise reuse this entry. */
        RemoveEntryList(&Connection->Entry);
        Connection->References = 0;
        Free = TRUE;
    }
    DCompUnlock();
    if (Free)
    {
        ASSERT(IsListEmpty(&Connection->Batches));
        ASSERT(IsListEmpty(&Connection->ReturnedBatches));
        ExFreePoolWithTag(Connection, DCOMP_TAG);
    }
}

static VOID
DCompDereferenceChannel(DCOMP_CHANNEL *Channel)
{
    if (InterlockedDecrement(&Channel->References) != 0)
        return;
    ASSERT(!Channel->Listed);
    DCompLock();
    while (!IsListEmpty(&Channel->Batches))
    {
        DCOMP_BATCH *Batch = CONTAINING_RECORD(RemoveHeadList(&Channel->Batches), DCOMP_BATCH, ChannelEntry);
        InitializeListHead(&Batch->ChannelEntry);
        Batch->Producer = NULL;
    }
    /* Reserve this record at creation: closing must not lose its ordered
     * notification merely because a later allocation would fail. */
    if (Channel->CloseBatch != NULL && Channel->ConnectionId != 0 && Channel->Connection->Owner != NULL &&
        Channel->OpenGeneration == Channel->Connection->Generation)
    {
        Channel->CloseBatch->ChannelId = Channel->ConnectionId;
        InsertTailList(&Channel->Connection->Batches, &Channel->CloseBatch->ConnectionEntry);
        Channel->CloseBatch = NULL;
        KeSetEvent(Channel->Connection->Event, IO_NO_INCREMENT, FALSE);
    }
    DCompUnlock();
    DCompReapReturnedBatches(Channel->Connection);
    if (Channel->OpenBatch != NULL)
        DCompFreeBatch(Channel->OpenBatch);
    if (Channel->CloseBatch != NULL)
        DCompFreeBatch(Channel->CloseBatch);
    if (Channel->Event != NULL)
        ObDereferenceObject(Channel->Event);
    DCompDestroyResources(Channel);
    if (Channel->UserView != NULL)
        MmUnmapViewOfSection(Channel->Process, Channel->UserView);
    if (Channel->SessionView != NULL)
        MmUnmapViewInSessionSpace(Channel->SessionView);
    if (Channel->Section != NULL)
        ObDereferenceObject(Channel->Section);
    ObDereferenceObject(Channel->Process);
    DCompDereferenceConnection(Channel->Connection);
    ExFreePoolWithTag(Channel, DCOMP_TAG);
}

static VOID
DCompDisconnect(DCOMP_CONNECTION *Connection, PEPROCESS Owner, PKEVENT Event)
{
    if (Event != NULL)
    {
        KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
        ObDereferenceObject(Event);
    }
    ObDereferenceObject(Owner);
    DCompDereferenceConnection(Connection);
}

static NTSTATUS
DCompDestroyConnection(PEPROCESS Process, HANDLE Identifier, BOOLEAN CheckIdentifier)
{
    PPROCESSINFO ProcessInfo = PsGetProcessWin32Process(Process);
    DCOMP_PROCESS *Data;
    DCOMP_CONNECTION *Connection = NULL;
    PKEVENT Event = NULL;
    LIST_ENTRY Retired;

    InitializeListHead(&Retired);
    DCompLock();
    Data = ProcessInfo != NULL ? ProcessInfo->CompositionData : NULL;
    if (Data != NULL && Data->Connection != NULL &&
        (!CheckIdentifier || Data->ConnectionIdentifier == (ULONG_PTR)Identifier))
    {
        Connection = Data->Connection;
        ASSERT(Connection->Owner == Process);
        Data->Connection = NULL;
        Event = Connection->Event;
        Connection->Event = NULL;
        Connection->Owner = NULL;
        DCompRetireConnectionBatches(Connection, &Retired);
    }
    DCompUnlock();
    if (Connection == NULL)
        return ProcessInfo != NULL ? STATUS_ACCESS_DENIED : STATUS_UNSUCCESSFUL;
    DCompFreeBatches(&Retired);
    DCompDisconnect(Connection, Process, Event);
    return STATUS_SUCCESS;
}

VOID
IntDCompositionDisconnectProcess(PEPROCESS Process)
{
    (void)DCompDestroyConnection(Process, NULL, FALSE);
}

LONG NTAPI
NtDCompositionCreateConnection(BOOL ConnectionFlag, HANDLE EventHandle, HANDLE *Identifier)
{
    PEPROCESS Process = PsGetCurrentProcess();
    DCOMP_PROCESS *Data;
    DCOMP_CONNECTION *Connection;
    PKEVENT Event = NULL;
    ULONG_PTR Value = 0;
    NTSTATUS Status;

    if (Identifier == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Registration/redirection bootstrap establishes the compositor owner.
     * Take USER before the composition lock, as process teardown does. */
    UserEnterShared();
    if (!IntCompositionIsAttachedProcess())
    {
        UserLeave();
        return STATUS_ACCESS_DENIED;
    }
    DCompLock();
    Data = DCompGetProcessLocked(PsGetCurrentProcessWin32Process());
    Connection = DCompFindConnectionLocked(PsGetProcessSessionId(Process));
    if (Data == NULL)
        Status = STATUS_UNSUCCESSFUL;
    else if (Data->Connection != NULL || (Connection != NULL && Connection->Owner != NULL))
        Status = STATUS_ACCESS_DENIED;
    else
    {
        Status = ObReferenceObjectByHandle(EventHandle, EVENT_MODIFY_STATE, *ExEventObjectType, UserMode, (PVOID *)&Event, NULL);
        if (NT_SUCCESS(Status))
        {
            if (Connection == NULL)
            {
                Connection = ExAllocatePoolWithQuotaTag(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, sizeof(*Connection), DCOMP_TAG);
                if (Connection != NULL)
                {
                    RtlZeroMemory(Connection, sizeof(*Connection));
                    Connection->References = 1; /* session list */
                    Connection->SessionId = PsGetProcessSessionId(Process);
                    InitializeListHead(&Connection->Batches);
                    InitializeListHead(&Connection->ReturnedBatches);
                    InsertTailList(&g_DCompConnections, &Connection->Entry);
                }
            }
            if (Connection == NULL)
                Status = STATUS_NO_MEMORY;
            else
            {
                ++Connection->References; /* process identifier */
                ObReferenceObject(Process);
                Connection->Owner = Process;
                ++Connection->Generation;
                Connection->Event = Event;
                Connection->ConnectionFlag = !!ConnectionFlag;
                Data->Connection = Connection;
                Value = Data->ConnectionIdentifier += 4;
                KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
                Event = NULL;
                Status = STATUS_SUCCESS;
            }
        }
    }
    DCompUnlock();
    UserLeave();
    if (Event != NULL)
        ObDereferenceObject(Event);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        ProbeForWrite(Identifier, sizeof(*Identifier), 1);
        *Identifier = (HANDLE)Value;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        (void)DCompDestroyConnection(Process, (HANDLE)Value, TRUE);
    return Status;
}

LONG NTAPI
NtDCompositionDestroyConnection(HANDLE Identifier)
{
    return DCompDestroyConnection(PsGetCurrentProcess(), Identifier, TRUE);
}

static NTSTATUS
DCompMapChannel(DCOMP_CHANNEL *Channel, UINT Size)
{
    LARGE_INTEGER MaximumSize, Offset;
    SIZE_T ViewSize;
    NTSTATUS Status;

    if (Size == 0)
        return STATUS_SUCCESS;
    if (Size > MAXUINT - (PAGE_SIZE - 1))
        return STATUS_NO_MEMORY;
    Channel->SectionSize = (Size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    MaximumSize.QuadPart = Channel->SectionSize;
    Status = MmCreateSection(&Channel->Section, SECTION_ALL_ACCESS, NULL, &MaximumSize, PAGE_READWRITE, SEC_COMMIT, NULL, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    ViewSize = Channel->SectionSize;
    Status = MmMapViewInSessionSpace(Channel->Section, &Channel->SessionView, &ViewSize);
    if (!NT_SUCCESS(Status))
        return Status;
    Offset.QuadPart = 0;
    ViewSize = Channel->SectionSize;
    return MmMapViewOfSection(Channel->Section, Channel->Process, &Channel->UserView, 0, Channel->SectionSize, &Offset, &ViewSize, ViewUnmap, SEC_NO_CHANGE, PAGE_READWRITE);
}

static VOID
DCompRemoveChannel(DCOMP_CHANNEL *Channel)
{
    BOOLEAN Removed = FALSE;

    DCompLock();
    if (Channel->Listed)
    {
        RemoveEntryList(&Channel->Entry);
        Channel->Listed = FALSE;
        Removed = TRUE;
    }
    DCompUnlock();
    if (Removed)
        DCompDereferenceChannel(Channel);
}

static NTSTATUS
DCompPrepareChannelNotifications(DCOMP_CHANNEL *Channel)
{
    OBJECT_ATTRIBUTES Attributes;
    HANDLE EventHandle;
    NTSTATUS Status;

    Channel->OpenBatch = DCompAllocateBatch(DCompBatchOpen, 0);
    Channel->CloseBatch = DCompAllocateBatch(DCompBatchClose, 0);
    if (Channel->OpenBatch == NULL || Channel->CloseBatch == NULL)
        return STATUS_NO_MEMORY;
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateEvent(&EventHandle, EVENT_MODIFY_STATE, &Attributes, NotificationEvent, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ObReferenceObjectByHandle(EventHandle, EVENT_MODIFY_STATE, *ExEventObjectType, KernelMode, (PVOID *)&Channel->Event, NULL);
    ZwClose(EventHandle);
    if (!NT_SUCCESS(Status))
        return Status;
    Channel->OpenBatch->ProcessId = HandleToUlong(PsGetProcessId(Channel->Process));
    Channel->OpenBatch->ProcessSequence = PsGetProcessSequenceNumber(Channel->Process);
    Channel->OpenBatch->Event = Channel->Event;
    ObReferenceObject(Channel->Event);
    return STATUS_SUCCESS;
}

LONG NTAPI
NtDCompositionCreateChannel(UINT *Identifier, UINT *SectionSize, PVOID *SectionBase)
{
    PEPROCESS Process = PsGetCurrentProcess();
    DCOMP_PROCESS *Data;
    DCOMP_CONNECTION *Connection;
    DCOMP_CHANNEL *Channel;
    PLIST_ENTRY Entry;
    UINT Size, Value;
    NTSTATUS Status;

    if (Identifier == NULL || SectionSize == NULL || SectionBase == NULL)
        return STATUS_INVALID_PARAMETER;
    _SEH2_TRY
    {
        ProbeForRead(SectionSize, sizeof(*SectionSize), 1);
        Size = *SectionSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    DCompLock();
    Connection = DCompFindConnectionLocked(PsGetProcessSessionId(Process));
    if (Connection == NULL || Connection->Owner == NULL)
    {
        DCompUnlock();
        return STATUS_ACCESS_DENIED;
    }
    ++Connection->References;
    DCompUnlock();
    Channel = ExAllocatePoolWithQuotaTag(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, sizeof(*Channel), DCOMP_TAG);
    if (Channel == NULL)
    {
        DCompDereferenceConnection(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Channel, sizeof(*Channel));
    Channel->References = 1; /* construction/output capture */
    Channel->Connection = Connection;
    Channel->Process = Process;
    InitializeListHead(&Channel->Resources);
    InitializeListHead(&Channel->ChangedResources);
    InitializeListHead(&Channel->ReleasedResources);
    InitializeListHead(&Channel->Batches);
    Channel->CommitSequence = 1;
    Channel->DescriptionDirty = TRUE;
    RtlInitializeGenericTableAvl(&Channel->ResourceTable, DCompCompareResource, DCompAllocateResourceEntry, DCompFreeResourceEntry, NULL);
    ObReferenceObject(Process);
    Status = DCompMapChannel(Channel, Size);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DCompPrepareChannelNotifications(Channel);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    DCompLock();
    Data = DCompGetProcessLocked(PsGetCurrentProcessWin32Process());
    if (Connection->Owner == NULL)
        Status = STATUS_ACCESS_DENIED;
    else if (Data == NULL || Connection->NextChannelId == MAXUINT)
        Status = STATUS_NO_MEMORY;
    else
    {
        /* Keep identifiers sorted so a retired identifier can be reused,
         * without allocating an address-sized or fixed-capacity table. */
        Value = 1;
        for (Entry = Data->Channels.Flink; Entry != &Data->Channels; Entry = Entry->Flink)
        {
            DCOMP_CHANNEL *Current = CONTAINING_RECORD(Entry, DCOMP_CHANNEL, Entry);
            if (Current->Identifier != Value || ++Value == 0)
                break;
        }
        if (Value == 0)
            Status = STATUS_NO_MEMORY;
        else
        {
            Channel->Identifier = Value;
            Channel->ConnectionId = ++Connection->NextChannelId;
            Channel->OpenGeneration = Connection->Generation;
            Channel->Listed = TRUE;
            InterlockedIncrement(&Channel->References); /* identifier table */
            InsertTailList(Entry, &Channel->Entry);
            Channel->OpenBatch->ChannelId = Channel->ConnectionId;
            InsertTailList(&Connection->Batches, &Channel->OpenBatch->ConnectionEntry);
            Channel->OpenBatch = NULL;
            KeSetEvent(Connection->Event, IO_NO_INCREMENT, FALSE);
        }
    }
    DCompUnlock();
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    _SEH2_TRY
    {
        ProbeForWrite(SectionSize, sizeof(*SectionSize), 1);
        *SectionSize = Channel->SectionSize;
        ProbeForWrite(Identifier, sizeof(*Identifier), 1);
        *Identifier = Channel->Identifier;
        ProbeForWrite(SectionBase, sizeof(*SectionBase), 1);
        *SectionBase = Channel->UserView;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        DCompRemoveChannel(Channel);

Cleanup:
    DCompDereferenceChannel(Channel);
    return Status;
}

/* Resource lookup and pending changes are serialized per channel. The process
 * identifier lock is never held while copying commands or editing resources. */
static DCOMP_RESOURCE *
DCompFindResource(DCOMP_CHANNEL *Channel, UINT Identifier)
{
    DCOMP_RESOURCE_ENTRY Key = {Identifier, NULL};
    DCOMP_RESOURCE_ENTRY *Entry;

    Entry = RtlLookupElementGenericTableAvl(&Channel->ResourceTable, &Key);
    return Entry != NULL ? Entry->Resource : NULL;
}

static VOID
DCompMarkResourceChanged(DCOMP_CHANNEL *Channel, DCOMP_RESOURCE *Resource)
{
    if (IsListEmpty(&Resource->DirtyEntry))
        InsertTailList(&Channel->ChangedResources, &Resource->DirtyEntry);
}

static NTSTATUS
DCompCreateResource(DCOMP_CHANNEL *Channel, UINT Identifier, UINT Type, UINT Shared)
{
    DCOMP_RESOURCE *Resource;
    DCOMP_RESOURCE_ENTRY Entry;

    if (Type == 0 || Type > 190)
        return STATUS_INVALID_PARAMETER;
    /* Each additional resource type needs its own property and reference
     * semantics before it can be accepted by this producer database. */
    if (Type != DCOMP_RESOURCE_VISUAL || Shared != 0)
        return STATUS_NOT_SUPPORTED;
    if (Identifier == 0)
        return STATUS_INVALID_PARAMETER;
    if (DCompFindResource(Channel, Identifier) != NULL)
        return STATUS_ACCESS_DENIED;
    if (Channel->NextResourceId == MAXUINT)
        return STATUS_NO_MEMORY;
    Resource = ExAllocatePoolWithQuotaTag(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, sizeof(*Resource), DCOMP_TAG);
    if (Resource == NULL)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Resource, sizeof(*Resource));
    Resource->Identifier = Identifier;
    Resource->Type = Type;
    Resource->FloatValues[5] = 0x3f800000u; /* opacity */
    Resource->IntegerValues[1] = MAXUINT; /* interpolation */
    Resource->IntegerValues[2] = MAXUINT; /* border */
    Resource->IntegerValues[3] = MAXUINT; /* composite */
    Resource->IntegerValues[4] = MAXUINT; /* antialias */
    Resource->IntegerValues[9] = MAXUINT; /* text antialias */
    Resource->IntegerValues[10] = MAXUINT; /* text rendering */
    Resource->IntegerValues[13] = 1; /* visible */
    InitializeListHead(&Resource->Children);
    InitializeListHead(&Resource->Sibling);
    InitializeListHead(&Resource->DirtyEntry);
    Entry.Identifier = Identifier;
    Entry.Resource = Resource;
    if (RtlInsertElementGenericTableAvl(&Channel->ResourceTable, &Entry, sizeof(Entry), NULL) == NULL)
    {
        ExFreePoolWithTag(Resource, DCOMP_TAG);
        return STATUS_NO_MEMORY;
    }
    InsertTailList(&Channel->Resources, &Resource->Entry);
    Resource->InternalId = ++Channel->NextResourceId;
    DCompMarkResourceChanged(Channel, Resource);
    return STATUS_SUCCESS;
}

static VOID
DCompReleaseUnusedResource(DCOMP_CHANNEL *Channel, DCOMP_RESOURCE *Resource)
{
    LIST_ENTRY Unused;

    if (Resource->Identifier != 0 || Resource->Parent != NULL)
        return;
    InitializeListHead(&Unused);
    RemoveEntryList(&Resource->Entry);
    InsertTailList(&Unused, &Resource->Entry);
    while (!IsListEmpty(&Unused))
    {
        Resource = CONTAINING_RECORD(RemoveHeadList(&Unused), DCOMP_RESOURCE, Entry);
        if (!IsListEmpty(&Resource->DirtyEntry))
        {
            RemoveEntryList(&Resource->DirtyEntry);
            InitializeListHead(&Resource->DirtyEntry);
        }
        while (!IsListEmpty(&Resource->Children))
        {
            DCOMP_RESOURCE *Child = CONTAINING_RECORD(RemoveHeadList(&Resource->Children), DCOMP_RESOURCE, Sibling);
            Child->Parent = NULL;
            InitializeListHead(&Child->Sibling);
            if (Child->Identifier == 0)
            {
                RemoveEntryList(&Child->Entry);
                InsertTailList(&Unused, &Child->Entry);
            }
        }
        if (Resource->Published)
            InsertTailList(&Channel->ReleasedResources, &Resource->Entry);
        else
            ExFreePoolWithTag(Resource, DCOMP_TAG);
    }
}

static NTSTATUS
DCompReleaseResource(DCOMP_CHANNEL *Channel, UINT Identifier)
{
    DCOMP_RESOURCE_ENTRY Key = {Identifier, NULL};
    DCOMP_RESOURCE *Resource = DCompFindResource(Channel, Identifier);

    if (Resource == NULL)
        return STATUS_ACCESS_DENIED;
    RtlDeleteElementGenericTableAvl(&Channel->ResourceTable, &Key);
    Resource->Identifier = 0;
    /* A child remains owned by its parent after the external identifier goes
     * away. Reusing that identifier must create a different resource. */
    DCompReleaseUnusedResource(Channel, Resource);
    return STATUS_SUCCESS;
}

static VOID
DCompDestroyResources(DCOMP_CHANNEL *Channel)
{
    while (!IsListEmpty(&Channel->ReleasedResources))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Channel->ReleasedResources), DCOMP_RESOURCE, Entry), DCOMP_TAG);
    while (!IsListEmpty(&Channel->Resources))
    {
        DCOMP_RESOURCE *Resource = CONTAINING_RECORD(RemoveHeadList(&Channel->Resources), DCOMP_RESOURCE, Entry);
        DCOMP_RESOURCE_ENTRY Key = {Resource->Identifier, NULL};
        if (!IsListEmpty(&Resource->DirtyEntry))
            RemoveEntryList(&Resource->DirtyEntry);
        if (Resource->Identifier != 0)
            RtlDeleteElementGenericTableAvl(&Channel->ResourceTable, &Key);
        /* Cycles are permitted in pending visual changes. At final channel
         * retirement the ownership list also collects unreachable cycles. */
        ExFreePoolWithTag(Resource, DCOMP_TAG);
    }
}

static NTSTATUS
DCompSetFloatProperty(DCOMP_CHANNEL *Channel, UINT Identifier, UINT Property, UINT Value)
{
    static const UINT Properties[] = {1, 2, 3, 24, 25, 26, 32, 33, 34, 35, 36, 44};
    DCOMP_RESOURCE *Resource = DCompFindResource(Channel, Identifier);
    UINT Index;

    if (Resource == NULL)
        return STATUS_ACCESS_DENIED;
    for (Index = 0; Index < RTL_NUMBER_OF(Properties); ++Index)
    {
        BOOLEAN Changed;

        if (Property != Properties[Index])
            continue;
        if (Property == 26)
        {
            /* Clamp opacity to [0, 1], with NaN becoming zero, using IEEE-754
             * bits so command validation does not acquire kernel FP state. */
            if ((Value & 0x80000000u) || Value > 0x7f800000u)
                Value = 0;
            else if (Value > 0x3f800000u)
                Value = 0x3f800000u;
        }
        /* The first six visual properties dirty their command group even for
         * equal values. The others use floating-point equality: signed zeros
         * compare equal and every NaN compares unequal, without kernel FP. */
        Changed = (Value & 0x7fffffffu) > 0x7f800000u ||
                  (Resource->FloatValues[Index] & 0x7fffffffu) > 0x7f800000u ||
                  (Value != Resource->FloatValues[Index] &&
                   ((Value | Resource->FloatValues[Index]) & 0x7fffffffu) != 0);
        if (Changed)
            Resource->FloatValues[Index] = Value;
        if (Index < 6 || Changed)
        {
            Resource->DirtyFloats |= 1u << Index;
            DCompMarkResourceChanged(Channel, Resource);
        }
        Resource->FloatProperties |= 1u << Index;
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
}

static NTSTATUS
DCompSetIntegerProperty(DCOMP_CHANNEL *Channel, UINT Identifier, UINT Property, ULONGLONG Value)
{
    static const UINT Properties[] = {0, 8, 9, 10, 14, 16, 17, 18, 19, 20, 21, 27, 37, 38, 41, 42, 43, 48, 51};
    DCOMP_RESOURCE *Resource = DCompFindResource(Channel, Identifier);
    UINT Index;

    if (Resource == NULL)
        return STATUS_ACCESS_DENIED;
    switch (Property)
    {
        case 8:
            if (Value == 6)
                break;
            /* Fall through to the inherited/disabled/enabled range. */
        case 9:
        case 14:
        case 20:
        case 21:
        case 22:
            if (Value != (ULONGLONG)-1 && Value > 1)
                return STATUS_INVALID_PARAMETER;
            break;
        case 10:
            if (Value != (ULONGLONG)-1 && Value > 2)
                return STATUS_INVALID_PARAMETER;
            break;
        case 16:
            if (Value > 2)
                return STATUS_INVALID_PARAMETER;
            break;
        case 18:
        case 19:
        case 27:
        case 37:
        case 41:
        case 43:
        case 48:
        case 51:
            Value = (UINT)Value != 0;
            break;
        case 38:
            Value = Value != 0;
            break;
        case 0:
        case 17:
        case 42:
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }
    /* These two producer properties update the same visual state. */
    if (Property == 22)
        Property = 21;
    for (Index = 0; Index < RTL_NUMBER_OF(Properties); ++Index)
    {
        if (Property == Properties[Index])
        {
            BOOLEAN Changed;

            if (Property == 38)
                Changed = TRUE;
            else if (Property == 17)
                Changed = Value != (ULONGLONG)(LONG)Resource->IntegerValues[Index];
            else
                Changed = (UINT)Value != Resource->IntegerValues[Index];
            /* The inherited blend mode is normalized on its first setter. */
            if (Property == 10 && !(Resource->IntegerProperties & (1u << Index)) && Value == (ULONGLONG)-1)
                Changed = TRUE;
            if (Changed)
            {
                Resource->DirtyIntegers |= 1u << Index;
                DCompMarkResourceChanged(Channel, Resource);
            }
            Resource->IntegerValues[Index] = (UINT)Value;
            Resource->IntegerProperties |= 1u << Index;
            break;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
DCompAddVisualChild(DCOMP_CHANNEL *Channel, UINT ParentId, UINT ChildId, UINT InsertAfter, UINT RelativeId)
{
    DCOMP_RESOURCE *Parent = DCompFindResource(Channel, ParentId);
    DCOMP_RESOURCE *Child = DCompFindResource(Channel, ChildId);
    DCOMP_RESOURCE *Relative = RelativeId != 0 ? DCompFindResource(Channel, RelativeId) : NULL;
    PLIST_ENTRY Position;

    if (Parent == NULL)
        return STATUS_ACCESS_DENIED;
    if (Child == NULL || Child->Parent != NULL ||
        (RelativeId != 0 && (Relative == NULL || Relative->Parent != Parent)))
        return STATUS_INVALID_PARAMETER;
    Position = Relative != NULL ? &Relative->Sibling : &Parent->Children;
    if (InsertAfter != 0)
        Position = Position->Flink;
    InsertTailList(Position, &Child->Sibling);
    Child->Parent = Parent;
    Parent->ChildrenDirty = TRUE;
    DCompMarkResourceChanged(Channel, Parent);
    return STATUS_SUCCESS;
}

static NTSTATUS
DCompRemoveVisualChild(DCOMP_CHANNEL *Channel, UINT ParentId, UINT ChildId)
{
    DCOMP_RESOURCE *Parent = DCompFindResource(Channel, ParentId);
    DCOMP_RESOURCE *Child = DCompFindResource(Channel, ChildId);

    if (Parent == NULL)
        return STATUS_ACCESS_DENIED;
    if (ChildId == 0)
    {
        while (!IsListEmpty(&Parent->Children))
        {
            Parent->ChildrenDirty = TRUE;
            DCompMarkResourceChanged(Channel, Parent);
            Child = CONTAINING_RECORD(RemoveHeadList(&Parent->Children), DCOMP_RESOURCE, Sibling);
            InitializeListHead(&Child->Sibling);
            Child->Parent = NULL;
            DCompReleaseUnusedResource(Channel, Child);
        }
        return STATUS_SUCCESS;
    }
    if (Child == NULL || Child->Parent != Parent)
        return STATUS_INVALID_PARAMETER;
    RemoveEntryList(&Child->Sibling);
    InitializeListHead(&Child->Sibling);
    Child->Parent = NULL;
    Parent->ChildrenDirty = TRUE;
    DCompMarkResourceChanged(Channel, Parent);
    return STATUS_SUCCESS;
}

static NTSTATUS DCompProcessCommands(DCOMP_CHANNEL *Channel, const UCHAR *Buffer, UINT Size, BOOLEAN AllowExternal, UINT *CommandCount);

typedef struct _DCOMP_BATCH_WRITER
{
    UCHAR *Buffer;
    UINT Capacity;
    UINT Size;
    NTSTATUS Status;
} DCOMP_BATCH_WRITER;

static VOID
DCompWriteCommand(DCOMP_BATCH_WRITER *Writer, DCOMP_MIL_COMMAND Command, UINT Resource, const UINT *Values, UINT Count)
{
    UINT Length;

    if (!NT_SUCCESS(Writer->Status))
        return;
    if (Count > (MAXUINT / sizeof(UINT)) - 3)
    {
        Writer->Status = STATUS_NO_MEMORY;
        return;
    }
    Length = (Count + 3) * sizeof(UINT);
    if (Length > MAXUINT - Writer->Size)
    {
        Writer->Status = STATUS_NO_MEMORY;
        return;
    }
    if (Writer->Buffer != NULL)
    {
        UINT *Destination = (UINT *)(Writer->Buffer + Writer->Size);
        if (Writer->Size > Writer->Capacity || Length > Writer->Capacity - Writer->Size)
        {
            Writer->Status = STATUS_NO_MEMORY;
            return;
        }
        Destination[0] = Length;
        Destination[1] = Command;
        Destination[2] = Resource;
        if (Count != 0)
            RtlCopyMemory(Destination + 3, Values, Count * sizeof(UINT));
    }
    Writer->Size += Length;
}

static VOID
DCompWriteVisual(DCOMP_BATCH_WRITER *Writer, DCOMP_RESOURCE *Resource)
{
    const UINT *Floats = Resource->FloatValues;
    const UINT *Integers = Resource->IntegerValues;
    UINT Dirty = Resource->DirtyIntegers;
    UINT Values[10] = {0};
    PLIST_ENTRY Entry;

    if (Dirty & 1u)
        DCompWriteCommand(Writer, DCompMilResourceNotificationId, Resource->InternalId, Integers, 1);
    if (Resource->ChildrenDirty)
    {
        UINT Previous = 0;
        for (Entry = Resource->Children.Flink; Entry != &Resource->Children; Entry = Entry->Flink)
        {
            DCOMP_RESOURCE *Child = CONTAINING_RECORD(Entry, DCOMP_RESOURCE, Sibling);
            UINT Insert[3] = {Child->InternalId, Previous, TRUE};
            DCompWriteCommand(Writer, DCompMilVisualInsertChild, Resource->InternalId, Insert, RTL_NUMBER_OF(Insert));
            Previous = Child->InternalId;
        }
    }
    if (Resource->DirtyFloats & 0x7)
        DCompWriteCommand(Writer, DCompMilVisualOffset, Resource->InternalId, Floats, 3);
    if (Resource->DirtyFloats & 0x18)
        DCompWriteCommand(Writer, DCompMilVisualSize, Resource->InternalId, Floats + 3, 2);
    if (Resource->DirtyFloats & 0x1c0)
        DCompWriteCommand(Writer, DCompMilVisualRelativeOffset, Resource->InternalId, Floats + 6, 3);
    if (Resource->DirtyFloats & 0x600)
        DCompWriteCommand(Writer, DCompMilVisualRelativeSize, Resource->InternalId, Floats + 9, 2);
    if (Dirty & ((1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 9) | (1u << 10)))
    {
        static const UINT Indices[] = {1, 2, 3, 4, 9, 10};
        static const UINT Offsets[] = {1, 2, 5, 6, 7, 8};
        UINT Index;

        for (Index = 0; Index < RTL_NUMBER_OF(Indices); ++Index)
        {
            UINT Value = Integers[Indices[Index]];
            if (Indices[Index] == 3 && (Resource->IntegerProperties & (1u << 3)))
                Value = Value == MAXUINT ? 5 : Value * 2;
            if (Value != MAXUINT)
            {
                Values[0] |= 1u << (Index + 1);
                Values[Offsets[Index]] = Value;
            }
        }
        DCompWriteCommand(Writer, DCompMilVisualRenderOptions, Resource->InternalId, Values, RTL_NUMBER_OF(Values));
    }
    if (Dirty & ((1u << 11) | (1u << 12) | (1u << 16) | (1u << 18)))
    {
        UINT Options = Integers[12] | (Integers[11] << 8) | (Integers[16] << 16) | (Integers[18] << 24);
        DCompWriteCommand(Writer, DCompMilVisualOptions, Resource->InternalId, &Options, 1);
    }
    if (Dirty & (1u << 17))
        DCompWriteCommand(Writer, DCompMilVisualProtection, Resource->InternalId, Integers + 17, 1);
    if (Dirty & ((1u << 7) | (1u << 8)))
    {
        UINT Options = Integers[7] | (Integers[8] << 8);
        DCompWriteCommand(Writer, DCompMilVisualRedrawRegion, Resource->InternalId, &Options, 1);
    }
    if (Dirty & ((1u << 5) | (1u << 6)))
    {
        UINT HeatMap[6] = {0, 0, 0, 0, Integers[5], Integers[6]};
        DCompWriteCommand(Writer, DCompMilVisualHeatMap, Resource->InternalId, HeatMap, RTL_NUMBER_OF(HeatMap));
    }
    if (Resource->DirtyFloats & (1u << 5))
        DCompWriteCommand(Writer, DCompMilVisualOpacity, Resource->InternalId, Floats + 5, 1);
    if (Dirty & (1u << 13))
        DCompWriteCommand(Writer, DCompMilVisualVisible, Resource->InternalId, Integers + 13, 1);
    if (Dirty & (1u << 14))
        DCompWriteCommand(Writer, DCompMilVisualForceLowColor, Resource->InternalId, Integers + 14, 1);
    if (Dirty & (1u << 15))
        DCompWriteCommand(Writer, DCompMilVisualResampleMode, Resource->InternalId, Integers + 15, 1);
    if (Resource->DirtyFloats & (1u << 11))
    {
        UINT Overrides[4] = {Floats[11], 0, 0, 0};
        DCompWriteCommand(Writer, DCompMilVisualContextOverrides, Resource->InternalId, Overrides, RTL_NUMBER_OF(Overrides));
    }
}

static VOID
DCompWriteChannel(DCOMP_CHANNEL *Channel, DCOMP_BATCH_WRITER *Writer)
{
    PLIST_ENTRY Entry;

    if (Channel->DescriptionDirty)
    {
        UINT Name[4] = {0};
        const CHAR *ImageName = PsGetProcessImageFileName(Channel->Process);
        UINT Length = 0;

        while (Length < sizeof(Name) - 1 && ImageName[Length] != 0)
            ++Length;
        RtlCopyMemory(Name, ImageName, Length);
        DCompWriteCommand(Writer, DCompMilChannelDescription, Length, Name, (Length + sizeof(UINT)) / sizeof(UINT));
    }
    for (Entry = Channel->ChangedResources.Flink; Entry != &Channel->ChangedResources; Entry = Entry->Flink)
    {
        DCOMP_RESOURCE *Resource = CONTAINING_RECORD(Entry, DCOMP_RESOURCE, DirtyEntry);
        if (!Resource->Published)
            DCompWriteCommand(Writer, DCompMilCreateResource, Resource->InternalId, &Resource->Type, 1);
    }
    /* Remove old ownership edges before adding new ones, including moves
     * between parents whose identifiers have a different ordering. */
    for (Entry = Channel->ReleasedResources.Flink; Entry != &Channel->ReleasedResources; Entry = Entry->Flink)
    {
        DCOMP_RESOURCE *Resource = CONTAINING_RECORD(Entry, DCOMP_RESOURCE, Entry);
        DCompWriteCommand(Writer, DCompMilVisualRemoveAllChildren, Resource->InternalId, NULL, 0);
    }
    for (Entry = Channel->ChangedResources.Flink; Entry != &Channel->ChangedResources; Entry = Entry->Flink)
    {
        DCOMP_RESOURCE *Resource = CONTAINING_RECORD(Entry, DCOMP_RESOURCE, DirtyEntry);
        if (Resource->ChildrenDirty)
            DCompWriteCommand(Writer, DCompMilVisualRemoveAllChildren, Resource->InternalId, NULL, 0);
    }
    for (Entry = Channel->ChangedResources.Flink; Entry != &Channel->ChangedResources; Entry = Entry->Flink)
        DCompWriteVisual(Writer, CONTAINING_RECORD(Entry, DCOMP_RESOURCE, DirtyEntry));
    for (Entry = Channel->ReleasedResources.Flink; Entry != &Channel->ReleasedResources; Entry = Entry->Flink)
    {
        DCOMP_RESOURCE *Resource = CONTAINING_RECORD(Entry, DCOMP_RESOURCE, Entry);
        DCompWriteCommand(Writer, DCompMilReleaseResource, Resource->InternalId, NULL, 0);
    }
}

static NTSTATUS
DCompCommitChannel(DCOMP_CHANNEL *Channel, ULONG Flags)
{
    DCOMP_BATCH_WRITER Writer = {NULL, 0, 0, STATUS_SUCCESS};
    DCOMP_BATCH *Batch;
    UINT CommitId = ++Channel->CommitSequence;

    DCompReapReturnedBatches(Channel->Connection);
    /* Count and encode under the same resource lock. Allocation failure leaves
     * every pending resource and dirty bit available for a later retry. */
    DCompWriteChannel(Channel, &Writer);
    if (!NT_SUCCESS(Writer.Status))
        return Writer.Status;
    if (Writer.Size == 0 && !(Flags & 1))
        return STATUS_SUCCESS;
    Batch = DCompAllocateBatch(DCompBatchCommands, Writer.Size);
    if (Batch == NULL)
        return STATUS_NO_MEMORY;
    Batch->ChannelId = Channel->ConnectionId;
    Batch->CommitId = CommitId;
    Batch->Flags = Flags & 3;
    Batch->ByteCount = Writer.Size;
    Writer.Buffer = Batch->Commands;
    Writer.Capacity = Writer.Size;
    Writer.Size = 0;
    DCompWriteChannel(Channel, &Writer);
    if (!NT_SUCCESS(Writer.Status))
    {
        DCompFreeBatch(Batch);
        return Writer.Status;
    }
    ASSERT(Writer.Size == Batch->ByteCount);
    DCompLock();
    if (Channel->Connection->Owner == NULL || !Channel->Listed)
    {
        DCompUnlock();
        DCompFreeBatch(Batch);
        return STATUS_ACCESS_DENIED;
    }
    InsertTailList(&Channel->Connection->Batches, &Batch->ConnectionEntry);
    Batch->Producer = Channel;
    InsertTailList(&Channel->Batches, &Batch->ChannelEntry);
    KeSetEvent(Channel->Connection->Event, IO_NO_INCREMENT, FALSE);
    DCompUnlock();
    Channel->PublishedCommitId = CommitId;
    Channel->DescriptionDirty = FALSE;
    while (!IsListEmpty(&Channel->ChangedResources))
    {
        DCOMP_RESOURCE *Resource = CONTAINING_RECORD(RemoveHeadList(&Channel->ChangedResources), DCOMP_RESOURCE, DirtyEntry);
        InitializeListHead(&Resource->DirtyEntry);
        Resource->Published = TRUE;
        Resource->ChildrenDirty = FALSE;
        Resource->DirtyFloats = Resource->DirtyIntegers = 0;
    }
    while (!IsListEmpty(&Channel->ReleasedResources))
        ExFreePoolWithTag(CONTAINING_RECORD(RemoveHeadList(&Channel->ReleasedResources), DCOMP_RESOURCE, Entry), DCOMP_TAG);
    return STATUS_SUCCESS;
}

static NTSTATUS
DCompProcessExternalBuffer(DCOMP_CHANNEL *Channel, const UCHAR *Buffer, UINT *CommandCount)
{
    DCOMP_EXTERNAL_BUFFER Command;
    PVOID Captured;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlCopyMemory(&Command, Buffer, sizeof(Command));
    if (Command.ByteCount == 0)
        return STATUS_INVALID_PARAMETER;
    if ((ULONGLONG)(ULONG_PTR)Command.Address != Command.Address)
        return STATUS_ACCESS_VIOLATION;
    Captured = ExAllocatePoolWithQuotaTag(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, Command.ByteCount, DCOMP_TAG);
    if (Captured == NULL)
        return STATUS_NO_MEMORY;
    _SEH2_TRY
    {
        ProbeForRead((PVOID)(ULONG_PTR)Command.Address, Command.ByteCount, 1);
        RtlCopyMemory(Captured, (PVOID)(ULONG_PTR)Command.Address, Command.ByteCount);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (NT_SUCCESS(Status))
        Status = DCompProcessCommands(Channel, Captured, Command.ByteCount, FALSE, CommandCount);
    ExFreePoolWithTag(Captured, DCOMP_TAG);
    return Status;
}

static NTSTATUS
DCompProcessCommands(DCOMP_CHANNEL *Channel, const UCHAR *Buffer, UINT Size, BOOLEAN AllowExternal, UINT *CommandCount)
{
    while (Size >= sizeof(UINT))
    {
        const UINT *Command = (const UINT *)Buffer;
        UINT Length;
        NTSTATUS Status;

        ++*CommandCount;
        switch (Command[0])
        {
            case DCompCommandExternalBuffer: Length = sizeof(DCOMP_EXTERNAL_BUFFER); break;
            case DCompCommandCreateResource: Length = 16; break;
            case DCompCommandReleaseResource: Length = 8; break;
            case DCompCommandSetIntegerProperty: Length = 24; break;
            case DCompCommandSetFloatProperty: Length = 16; break;
            case DCompCommandAddVisualChild: Length = 20; break;
            case DCompCommandRemoveVisualChild: Length = 12; break;
            default: return Command[0] > 23 ? STATUS_INVALID_PARAMETER : STATUS_NOT_SUPPORTED;
        }
        if (Size < Length)
            return STATUS_INVALID_PARAMETER;
        switch (Command[0])
        {
            case DCompCommandExternalBuffer:
                Status = AllowExternal ? DCompProcessExternalBuffer(Channel, Buffer, CommandCount) : STATUS_INVALID_PARAMETER;
                break;
            case DCompCommandCreateResource:
                Status = DCompCreateResource(Channel, Command[1], Command[2], Command[3]);
                break;
            case DCompCommandReleaseResource:
                Status = DCompReleaseResource(Channel, Command[1]);
                break;
            case DCompCommandSetFloatProperty:
                Status = DCompSetFloatProperty(Channel, Command[1], Command[2], Command[3]);
                break;
            case DCompCommandSetIntegerProperty:
            {
                ULONGLONG Value;
                RtlCopyMemory(&Value, Buffer + 16, sizeof(Value));
                Status = DCompSetIntegerProperty(Channel, Command[1], Command[2], Value);
                break;
            }
            case DCompCommandAddVisualChild:
                Status = DCompAddVisualChild(Channel, Command[1], Command[2], Command[3], Command[4]);
                break;
            default:
                Status = DCompRemoveVisualChild(Channel, Command[1], Command[2]);
                break;
        }
        if (!NT_SUCCESS(Status))
            return Status;
        Buffer += Length;
        Size -= Length;
    }
    return Size == 0 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

LONG NTAPI
NtDCompositionProcessChannelBatchBuffer(UINT Identifier, UINT ByteCount, UINT *CommandCount, BOOLEAN *State)
{
    PPROCESSINFO ProcessInfo = PsGetCurrentProcessWin32Process();
    DCOMP_CHANNEL *Channel = DCompReferenceChannel(Identifier);
    PVOID Captured = NULL;
    UINT Count = 0;
    NTSTATUS Status;

    if (Channel == NULL)
        Status = ProcessInfo != NULL ? STATUS_ACCESS_DENIED : STATUS_UNSUCCESSFUL;
    else if (ByteCount > Channel->SectionSize)
        Status = STATUS_INVALID_PARAMETER;
    else
    {
        Status = STATUS_SUCCESS;
        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&Channel->ResourceLock);
        if (ByteCount != 0)
        {
            Captured = ExAllocatePoolWithQuotaTag(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, ByteCount, DCOMP_TAG);
            if (Captured == NULL)
                Status = STATUS_NO_MEMORY;
            else
                RtlCopyMemory(Captured, Channel->SessionView, ByteCount);
        }
        if (NT_SUCCESS(Status))
            Status = DCompProcessCommands(Channel, Captured, ByteCount, TRUE, &Count);
        ExReleasePushLockExclusive(&Channel->ResourceLock);
        KeLeaveCriticalRegion();
        if (Captured != NULL)
            ExFreePoolWithTag(Captured, DCOMP_TAG);
    }
    _SEH2_TRY
    {
        ProbeForWrite(CommandCount, sizeof(*CommandCount), 1);
        *CommandCount = Count;
        ProbeForWrite(State, sizeof(*State), 1);
        /* Private visual commands do not generate channel notifications. */
        *State = FALSE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        if (NT_SUCCESS(Status))
            Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (Channel != NULL)
        DCompDereferenceChannel(Channel);
    return Status;
}

LONG NTAPI
NtDCompositionCommitChannel(UINT Identifier, UINT *CommitId, BOOLEAN *State, ULONG Flags, HANDLE Synchronization, const VOID *Commands, const UINT *Resources, UINT ResourceCount)
{
    DCOMP_CHANNEL *Channel;
    NTSTATUS Status;
    UINT PublishedId;

    if (State == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Commands != NULL || Resources != NULL || ResourceCount != 0)
    {
        BOOLEAN Compositor;
        UserEnterShared();
        Compositor = IntCompositionIsAttachedProcess();
        UserLeave();
        /* The compositor-only protocol block/release array needs its own
         * capture and validation path, separate from producer commands. */
        return Compositor ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER;
    }
    Channel = DCompReferenceChannel(Identifier);
    if (Channel == NULL)
        return PsGetCurrentProcessWin32Process() != NULL ? STATUS_ACCESS_DENIED : STATUS_UNSUCCESSFUL;

    /* Invalid synchronization handles are ignored by the native call. A
     * composition synchronization object cannot yet be created in this stack. */
    UNREFERENCED_PARAMETER(Synchronization);
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Channel->ResourceLock);
    Status = DCompCommitChannel(Channel, Flags);
    PublishedId = Channel->PublishedCommitId;
    ExReleasePushLockExclusive(&Channel->ResourceLock);
    KeLeaveCriticalRegion();
    if (NT_SUCCESS(Status))
    {
        _SEH2_TRY
        {
            /* Publication precedes output capture, including its faults. */
            if (CommitId != NULL)
            {
                ProbeForWrite(CommitId, sizeof(*CommitId), 1);
                *CommitId = PublishedId;
            }
            ProbeForWrite(State, sizeof(*State), 1);
            *State = FALSE;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    DCompDereferenceChannel(Channel);
    return Status;
}

LONG NTAPI
NtDCompositionDestroyChannel(UINT Identifier)
{
    PPROCESSINFO ProcessInfo = PsGetCurrentProcessWin32Process();
    DCOMP_PROCESS *Data;
    DCOMP_CHANNEL *Channel = NULL;
    PLIST_ENTRY Entry;

    DCompLock();
    Data = ProcessInfo != NULL ? ProcessInfo->CompositionData : NULL;
    if (Data != NULL)
    {
        for (Entry = Data->Channels.Flink; Entry != &Data->Channels; Entry = Entry->Flink)
        {
            DCOMP_CHANNEL *Current = CONTAINING_RECORD(Entry, DCOMP_CHANNEL, Entry);
            if (Current->Identifier == Identifier)
            {
                Channel = Current;
                RemoveEntryList(Entry);
                Channel->Listed = FALSE;
                break;
            }
        }
    }
    DCompUnlock();
    if (Channel == NULL)
        return ProcessInfo != NULL ? STATUS_ACCESS_DENIED : STATUS_UNSUCCESSFUL;
    DCompDereferenceChannel(Channel);
    return STATUS_SUCCESS;
}

VOID
IntDCompositionCleanupProcess(PPROCESSINFO ProcessInfo)
{
    DCOMP_PROCESS *Data;
    DCOMP_CONNECTION *Connection;
    PKEVENT Event = NULL;
    LIST_ENTRY Channels;
    LIST_ENTRY Retired;

    InitializeListHead(&Channels);
    InitializeListHead(&Retired);
    DCompLock();
    Data = ProcessInfo->CompositionData;
    ProcessInfo->CompositionData = NULL;
    if (Data == NULL)
    {
        DCompUnlock();
        return;
    }
    Connection = Data->Connection;
    if (Connection != NULL)
    {
        ASSERT(Connection->Owner == ProcessInfo->peProcess);
        Event = Connection->Event;
        Connection->Event = NULL;
        Connection->Owner = NULL;
        DCompRetireConnectionBatches(Connection, &Retired);
    }
    while (!IsListEmpty(&Data->Channels))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Data->Channels);
        CONTAINING_RECORD(Entry, DCOMP_CHANNEL, Entry)->Listed = FALSE;
        InsertTailList(&Channels, Entry);
    }
    DCompUnlock();
    DCompFreeBatches(&Retired);
    while (!IsListEmpty(&Channels))
        DCompDereferenceChannel(CONTAINING_RECORD(RemoveHeadList(&Channels), DCOMP_CHANNEL, Entry));
    if (Connection != NULL)
        DCompDisconnect(Connection, ProcessInfo->peProcess, Event);
    ExFreePoolWithTag(Data, DCOMP_TAG);
}
