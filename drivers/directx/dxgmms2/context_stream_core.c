/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Deterministic per-context logical admission stream core
 */

#include "dxgmms2_private.h"

static PDXGMMS2_CONTEXT_STREAM_CONTEXT Dxgmms2ContextStreamFindLocked(_In_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle)
{
    PLIST_ENTRY Entry;

    for (Entry = Manager->ContextListHead.Flink; Entry != &Manager->ContextListHead; Entry = Entry->Flink)
    {
        PDXGMMS2_CONTEXT_STREAM_CONTEXT Context = CONTAINING_RECORD(Entry, DXGMMS2_CONTEXT_STREAM_CONTEXT, ManagerEntry);

        if (Context->Signature == DXGMMS2_CONTEXT_SIGNATURE && Context->PublicHandle == ContextHandle)
            return Context;
    }
    return NULL;
}

static PDXGMMS2_CONTEXT_ENTRY Dxgmms2ContextStreamFindEntryLocked(_In_ PDXGMMS2_CONTEXT_STREAM_CONTEXT Context, _In_ ULONGLONG Sequence)
{
    ULONGLONG Offset;
    ULONG Index;

    if (Sequence <= Context->LastCompletedSequence || Sequence > Context->LastAdmittedSequence)
        return NULL;
    Offset = Sequence - (Context->LastCompletedSequence + 1);
    if (Offset >= Context->QueuedEntryCount)
        return NULL;
    Index = (Context->HeadIndex + (ULONG)Offset) % Context->QueueDepth;
    if (Context->Entries[Index].Sequence != Sequence)
        return NULL;
    return &Context->Entries[Index];
}

static ULONGLONG Dxgmms2ContextStreamNextNonzero64(_Inout_ ULONGLONG *Value)
{
    (*Value)++;
    if (*Value == 0)
        (*Value)++;
    return *Value;
}

static DXGMMS2_CONTEXT_STREAM_HANDLE Dxgmms2ContextStreamNextHandleLocked(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager)
{
    DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle;

    do
    {
        Manager->NextContextHandle++;
        if (Manager->NextContextHandle == 0)
            Manager->NextContextHandle++;
        ContextHandle = (DXGMMS2_CONTEXT_STREAM_HANDLE)Manager->NextContextHandle;
    } while (Dxgmms2ContextStreamFindLocked(Manager, ContextHandle) != NULL);
    return ContextHandle;
}

static PDXGMMS2_CONTEXT_ENTRY Dxgmms2ContextStreamAppendEntryLocked(_Inout_ PDXGMMS2_CONTEXT_STREAM_CONTEXT Context, _In_ ULONG Type, _In_ ULONG Flags, _In_ ULONGLONG ObjectId, _In_ ULONGLONG FenceValue, _In_ ULONGLONG ClientTag)
{
    PDXGMMS2_CONTEXT_ENTRY Entry;
    ULONG Index;

    ASSERT(Context->QueuedEntryCount < Context->QueueDepth);
    ASSERT(Context->LastAdmittedSequence != MAXULONGLONG);
    Index = (Context->HeadIndex + Context->QueuedEntryCount) % Context->QueueDepth;
    Entry = &Context->Entries[Index];
    RtlZeroMemory(Entry, sizeof(*Entry));
    Context->LastAdmittedSequence++;
    Entry->Sequence = Context->LastAdmittedSequence;
    Entry->Type = Type;
    Entry->Flags = Flags;
    Entry->ObjectId = ObjectId;
    Entry->FenceValue = FenceValue;
    Entry->ClientTag = ClientTag;
    Entry->TerminalStatus = STATUS_PENDING;
    Context->QueuedEntryCount++;
    return Entry;
}

static VOID Dxgmms2ContextStreamRetireTransactionMemberLocked(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _Inout_ PDXGMMS2_CONTEXT_TRANSACTION Transaction)
{
    ASSERT(Transaction->Signature == DXGMMS2_CONTEXT_TRANSACTION_SIGNATURE);
    ASSERT(Transaction->RemainingMembers != 0);
    Transaction->RemainingMembers--;
    if (Transaction->RemainingMembers != 0)
        return;
    ASSERT(!Transaction->Claimed);
    RemoveEntryList(&Transaction->ManagerEntry);
    InitializeListHead(&Transaction->ManagerEntry);
    ASSERT(Manager->TransactionCount != 0);
    Manager->TransactionCount--;
    Transaction->Signature = 0;
    ExFreePoolWithTag(Transaction, DXGMMS2_CONTEXT_TRANSACTION_TAG);
}

static VOID Dxgmms2ContextStreamAdvanceCompletedLocked(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _Inout_ PDXGMMS2_CONTEXT_STREAM_CONTEXT Context)
{
    while (Context->QueuedEntryCount != 0)
    {
        PDXGMMS2_CONTEXT_ENTRY Entry = &Context->Entries[Context->HeadIndex];
        PDXGMMS2_CONTEXT_TRANSACTION Transaction;
        PDXGMMS2_CONTEXT_RETIREMENT_V1 Retirement;
        ULONG RetirementIndex;

        if (!Entry->Terminal)
            break;
        ASSERT(!Entry->Claimed);
        ASSERT(Entry->Submitted);
        ASSERT(Entry->Sequence == Context->LastCompletedSequence + 1);
        ASSERT(Context->RetiredEntryCount < Context->QueueDepth);
        Transaction = Entry->Transaction;
        RetirementIndex = (Context->RetirementHeadIndex + Context->RetiredEntryCount) % Context->QueueDepth;
        Retirement = &Context->Retirements[RetirementIndex];
        RtlZeroMemory(Retirement, sizeof(*Retirement));
        Retirement->Sequence = Entry->Sequence;
        Retirement->TransactionId = Transaction != NULL ? Transaction->TransactionId : 0;
        Retirement->ClientTag = Entry->ClientTag;
        Retirement->TerminalStatus = Entry->TerminalStatus;
        Retirement->Type = Entry->Type;
        Retirement->Flags = Entry->Flags;
        Context->RetiredEntryCount++;
        Context->LastCompletedSequence = Entry->Sequence;
        RtlZeroMemory(Entry, sizeof(*Entry));
        Context->HeadIndex = (Context->HeadIndex + 1) % Context->QueueDepth;
        Context->QueuedEntryCount--;
        if (Transaction != NULL)
            Dxgmms2ContextStreamRetireTransactionMemberLocked(Manager, Transaction);
    }
}

static VOID Dxgmms2ContextStreamNormalizeLocked(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _Inout_ PDXGMMS2_CONTEXT_STREAM_CONTEXT Context)
{
    for (;;)
    {
        PDXGMMS2_CONTEXT_ENTRY Entry;
        ULONGLONG NextSequence;

        if (Context->LastSubmittedSequence == Context->LastAdmittedSequence)
            break;
        NextSequence = Context->LastSubmittedSequence + 1;
        Entry = Dxgmms2ContextStreamFindEntryLocked(Context, NextSequence);
        ASSERT(Entry != NULL);
        if (Entry == NULL || Entry->Claimed)
            break;
        if (Entry->CancelRequested)
        {
            Entry->Submitted = TRUE;
            Entry->Terminal = TRUE;
            Entry->TerminalStatus = STATUS_CANCELLED;
            Context->LastSubmittedSequence = Entry->Sequence;
            continue;
        }
        if (Entry->Type == Dxgmms2ContextEntryWait && Entry->WaitResolved)
        {
            Entry->Submitted = TRUE;
            Entry->Terminal = TRUE;
            ASSERT(Entry->TerminalStatus != STATUS_PENDING);
            Context->LastSubmittedSequence = Entry->Sequence;
            continue;
        }
        break;
    }
    Dxgmms2ContextStreamAdvanceCompletedLocked(Manager, Context);
}

static VOID Dxgmms2ContextStreamCancelTransactionLocked(_Inout_ PDXGMMS2_CONTEXT_TRANSACTION Transaction)
{
    ULONG MemberIndex;

    if (Transaction->Claimed)
        return;
    Transaction->CancelRequested = TRUE;
    for (MemberIndex = 0; MemberIndex < Transaction->MemberCount; ++MemberIndex)
    {
        PDXGMMS2_CONTEXT_TRANSACTION_MEMBER Member = &Transaction->Members[MemberIndex];
        PDXGMMS2_CONTEXT_ENTRY Entry = Dxgmms2ContextStreamFindEntryLocked(Member->Context, Member->Sequence);

        if (Entry != NULL)
            Entry->CancelRequested = TRUE;
    }
}

static VOID Dxgmms2ContextStreamCancelContextLocked(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _Inout_ PDXGMMS2_CONTEXT_STREAM_CONTEXT Context, _In_ ULONG Flags, _In_ ULONG Reason)
{
    ULONG Offset;

    Context->State = Dxgmms2ContextStreamPublicClosing;
    Context->CancelReason = Reason;
    for (Offset = 0; Offset < Context->QueuedEntryCount; ++Offset)
    {
        ULONG Index = (Context->HeadIndex + Offset) % Context->QueueDepth;
        PDXGMMS2_CONTEXT_ENTRY Entry = &Context->Entries[Index];

        if (!Entry->Claimed)
        {
            Entry->CancelRequested = TRUE;
            if (Entry->Transaction != NULL)
                Dxgmms2ContextStreamCancelTransactionLocked(Entry->Transaction);
        }
        if ((Flags & DXGMMS2_CONTEXT_CANCEL_FORCE_SUBMITTED) != 0 && Entry->Submitted && !Entry->Claimed)
        {
            Entry->Terminal = TRUE;
            Entry->TerminalStatus = STATUS_CANCELLED;
        }
    }
    Dxgmms2ContextStreamNormalizeLocked(Manager, Context);
}

static VOID Dxgmms2ContextStreamNormalizeAllLocked(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager)
{
    PLIST_ENTRY ListEntry;

    for (ListEntry = Manager->ContextListHead.Flink; ListEntry != &Manager->ContextListHead; ListEntry = ListEntry->Flink)
    {
        PDXGMMS2_CONTEXT_STREAM_CONTEXT Context = CONTAINING_RECORD(ListEntry, DXGMMS2_CONTEXT_STREAM_CONTEXT, ManagerEntry);

        Dxgmms2ContextStreamNormalizeLocked(Manager, Context);
    }
}

static VOID Dxgmms2ContextStreamCancelAllLocked(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ ULONG Flags, _In_ ULONG Reason)
{
    PLIST_ENTRY ListEntry;

    for (ListEntry = Manager->ContextListHead.Flink; ListEntry != &Manager->ContextListHead; ListEntry = ListEntry->Flink)
    {
        PDXGMMS2_CONTEXT_STREAM_CONTEXT Context = CONTAINING_RECORD(ListEntry, DXGMMS2_CONTEXT_STREAM_CONTEXT, ManagerEntry);

        Dxgmms2ContextStreamCancelContextLocked(Manager, Context, Flags, Reason);
    }
    Dxgmms2ContextStreamNormalizeAllLocked(Manager);
}

static NTSTATUS Dxgmms2ContextStreamValidateAction(_Inout_ DXGMMS2_CONTEXT_ACTION_V1 *Action)
{
    ULONG Capacity;

    if (Action == NULL)
        return STATUS_INVALID_PARAMETER;
    Capacity = Action->Size;
    if (Capacity < FIELD_OFFSET(DXGMMS2_CONTEXT_ACTION_V1, Version) + sizeof(Action->Version))
    {
        if (Capacity >= sizeof(Action->Size))
            Action->Size = DXGMMS2_CONTEXT_ACTION_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (Action->Version != DXGMMS2_CONTEXT_STREAM_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Capacity < DXGMMS2_CONTEXT_ACTION_V1_SIZE)
    {
        Action->Size = DXGMMS2_CONTEXT_ACTION_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS Dxgmms2ContextStreamValidateSnapshot(_Inout_ DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 *Snapshot)
{
    ULONG Capacity;

    if (Snapshot == NULL)
        return STATUS_INVALID_PARAMETER;
    Capacity = Snapshot->Size;
    if (Capacity < FIELD_OFFSET(DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1, Version) + sizeof(Snapshot->Version))
    {
        if (Capacity >= sizeof(Snapshot->Size))
            Snapshot->Size = DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (Snapshot->Version != DXGMMS2_CONTEXT_STREAM_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Capacity < DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1_SIZE)
    {
        Snapshot->Size = DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    return STATUS_SUCCESS;
}

VOID Dxgmms2ContextStreamManagerInitialize(_Out_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager)
{
    RtlZeroMemory(Manager, sizeof(*Manager));
    Manager->Signature = DXGMMS2_CONTEXT_MANAGER_SIGNATURE;
    Manager->State = Dxgmms2ContextManagerCreated;
    KeInitializeSpinLock(&Manager->Lock);
    InitializeListHead(&Manager->ContextListHead);
    InitializeListHead(&Manager->TransactionListHead);
}

NTSTATUS Dxgmms2ContextStreamManagerStart(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ ULONG NodeCount)
{
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || NodeCount > DXGMMS2_TIMELINE_MAX_NODES)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    if (Manager->State != Dxgmms2ContextManagerCreated && Manager->State != Dxgmms2ContextManagerStopped)
        Status = STATUS_INVALID_DEVICE_STATE;
    else if (Manager->ContextCount != 0 || Manager->TransactionCount != 0 || Manager->ActiveClaimCount != 0)
        Status = STATUS_DEVICE_BUSY;
    else
    {
        Manager->Generation++;
        if (Manager->Generation == 0)
            Manager->Generation++;
        Manager->NodeCount = NodeCount;
        Manager->State = Dxgmms2ContextManagerActive;
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamManagerBeginStop(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ ULONG Reason)
{
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    if (Manager->State == Dxgmms2ContextManagerActive)
        Manager->State = Dxgmms2ContextManagerStopping;
    else if (Manager->State != Dxgmms2ContextManagerStopping)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamManagerCompleteStop(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ BOOLEAN HardwareRetired)
{
    KIRQL OldIrql;
    PLIST_ENTRY ListEntry;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || !HardwareRetired)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    if (Manager->State == Dxgmms2ContextManagerStopped)
        goto Exit;
    if (Manager->State != Dxgmms2ContextManagerStopping)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    Dxgmms2ContextStreamCancelAllLocked(Manager, DXGMMS2_CONTEXT_CANCEL_FORCE_SUBMITTED, STATUS_DEVICE_REMOVED);
    if (Manager->ActiveClaimCount != 0)
    {
        Status = STATUS_DEVICE_BUSY;
        goto Exit;
    }
    for (ListEntry = Manager->ContextListHead.Flink; ListEntry != &Manager->ContextListHead; ListEntry = ListEntry->Flink)
    {
        PDXGMMS2_CONTEXT_STREAM_CONTEXT Context = CONTAINING_RECORD(ListEntry, DXGMMS2_CONTEXT_STREAM_CONTEXT, ManagerEntry);

        if (Context->QueuedEntryCount != 0 || Context->ActiveClaimCount != 0)
        {
            Status = STATUS_DEVICE_BUSY;
            goto Exit;
        }
    }
    if (Manager->ContextCount != 0)
    {
        Status = STATUS_DEVICE_BUSY;
        goto Exit;
    }
    ASSERT(Manager->TransactionCount == 0);
    Manager->State = Dxgmms2ContextManagerStopped;

Exit:
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamManagerCanDestroy(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    if (Manager->State != Dxgmms2ContextManagerCreated && Manager->State != Dxgmms2ContextManagerStopped)
        Status = STATUS_INVALID_DEVICE_STATE;
    else if (Manager->ContextCount != 0 || Manager->TransactionCount != 0 || Manager->ActiveClaimCount != 0)
        Status = STATUS_DEVICE_BUSY;
    else
        Status = STATUS_SUCCESS;
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamManagerPrepareDestroy(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    Status = Dxgmms2ContextStreamManagerCanDestroy(Manager);
    if (!NT_SUCCESS(Status))
        return Status;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    if ((Manager->State != Dxgmms2ContextManagerCreated && Manager->State != Dxgmms2ContextManagerStopped) || Manager->ContextCount != 0 || Manager->TransactionCount != 0 || Manager->ActiveClaimCount != 0)
        Status = STATUS_DEVICE_BUSY;
    else
    {
        Manager->State = Dxgmms2ContextManagerDestroying;
        Status = STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

ULONG Dxgmms2ContextStreamManagerGetGeneration(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager)
{
    KIRQL OldIrql;
    ULONG Generation = 0;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE)
        return 0;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Generation = Manager->Generation;
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Generation;
}

NTSTATUS Dxgmms2ContextStreamCreate(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ const DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1 *Info, _Out_ DXGMMS2_CONTEXT_STREAM_HANDLE *ContextHandle)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    SIZE_T AllocationSize;
    ULONG QueueDepth;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (ContextHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    *ContextHandle = NULL;
    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || Info == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size < FIELD_OFFSET(DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1, Version) + sizeof(Info->Version) || Info->Size < DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Info->Version != DXGMMS2_CONTEXT_STREAM_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Info->Flags != 0 || Info->Reserved != 0)
        return STATUS_INVALID_PARAMETER;
    QueueDepth = Info->QueueDepth == 0 ? DXGMMS2_CONTEXT_STREAM_DEFAULT_QUEUE_DEPTH : Info->QueueDepth;
    if (QueueDepth < DXGMMS2_CONTEXT_STREAM_MIN_QUEUE_DEPTH || QueueDepth > DXGMMS2_CONTEXT_STREAM_MAX_QUEUE_DEPTH)
        return STATUS_INVALID_PARAMETER;
    AllocationSize = FIELD_OFFSET(DXGMMS2_CONTEXT_STREAM_CONTEXT, Entries) + ((SIZE_T)QueueDepth * sizeof(DXGMMS2_CONTEXT_ENTRY)) + ((SIZE_T)QueueDepth * sizeof(DXGMMS2_CONTEXT_RETIREMENT_V1));
    Context = ExAllocatePoolWithTag(NonPagedPool, AllocationSize, DXGMMS2_CONTEXT_TAG);
    if (Context == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Context, AllocationSize);
    Context->Signature = DXGMMS2_CONTEXT_SIGNATURE;
    Context->State = Dxgmms2ContextStreamPublicActive;
    Context->NodeOrdinal = Info->NodeOrdinal;
    Context->QueueDepth = QueueDepth;
    Context->Retirements = (PDXGMMS2_CONTEXT_RETIREMENT_V1)&Context->Entries[QueueDepth];
    InitializeListHead(&Context->ManagerEntry);

    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    if (Manager->State != Dxgmms2ContextManagerActive)
        Status = STATUS_INVALID_DEVICE_STATE;
    else if (Info->NodeOrdinal >= Manager->NodeCount)
        Status = STATUS_INVALID_PARAMETER;
    else
    {
        Context->PublicHandle = Dxgmms2ContextStreamNextHandleLocked(Manager);
        InsertTailList(&Manager->ContextListHead, &Context->ManagerEntry);
        Manager->ContextCount++;
        *ContextHandle = Context->PublicHandle;
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    if (!NT_SUCCESS(Status))
    {
        Context->Signature = 0;
        ExFreePoolWithTag(Context, DXGMMS2_CONTEXT_TAG);
    }
    return Status;
}

NTSTATUS Dxgmms2ContextStreamDestroy(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    Context = NULL;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Context == NULL)
        Status = STATUS_INVALID_HANDLE;
    else if (Context->QueuedEntryCount != 0 || Context->ActiveClaimCount != 0 || Context->RetiredEntryCount != 0)
        Status = STATUS_DEVICE_BUSY;
    else
    {
        RemoveEntryList(&Context->ManagerEntry);
        InitializeListHead(&Context->ManagerEntry);
        ASSERT(Manager->ContextCount != 0);
        Manager->ContextCount--;
        Context->PublicHandle = NULL;
        Context->Signature = 0;
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    if (NT_SUCCESS(Status))
        ExFreePoolWithTag(Context, DXGMMS2_CONTEXT_TAG);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamAdmitWork(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ const DXGMMS2_ADMIT_CONTEXT_WORK_V1 *Info, _Out_ ULONGLONG *Sequence)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    PDXGMMS2_CONTEXT_ENTRY Entry;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Sequence == NULL)
        return STATUS_INVALID_PARAMETER;
    *Sequence = 0;
    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL || Info == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size < FIELD_OFFSET(DXGMMS2_ADMIT_CONTEXT_WORK_V1, Version) + sizeof(Info->Version) || Info->Size < DXGMMS2_ADMIT_CONTEXT_WORK_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Info->Version != DXGMMS2_CONTEXT_STREAM_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Manager->State != Dxgmms2ContextManagerActive)
        Status = STATUS_INVALID_DEVICE_STATE;
    else if (Context == NULL)
        Status = STATUS_INVALID_HANDLE;
    else if (Context->State != Dxgmms2ContextStreamPublicActive)
        Status = STATUS_DELETE_PENDING;
    else if (Context->QueuedEntryCount + Context->RetiredEntryCount >= Context->QueueDepth)
        Status = STATUS_DEVICE_BUSY;
    else if (Context->LastAdmittedSequence == MAXULONGLONG)
        Status = STATUS_INTEGER_OVERFLOW;
    else
    {
        Entry = Dxgmms2ContextStreamAppendEntryLocked(Context, Dxgmms2ContextEntryWork, 0, 0, 0, Info->ClientTag);
        *Sequence = Entry->Sequence;
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamAdmitWait(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ const DXGMMS2_ADMIT_CONTEXT_WAIT_V1 *Info, _Out_ ULONGLONG *Sequence)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    PDXGMMS2_CONTEXT_ENTRY Entry;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Sequence == NULL)
        return STATUS_INVALID_PARAMETER;
    *Sequence = 0;
    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL || Info == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size < FIELD_OFFSET(DXGMMS2_ADMIT_CONTEXT_WAIT_V1, Version) + sizeof(Info->Version) || Info->Size < DXGMMS2_ADMIT_CONTEXT_WAIT_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Info->Version != DXGMMS2_CONTEXT_STREAM_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Info->Flags != 0 || Info->Reserved != 0 || Info->ObjectId == 0)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Manager->State != Dxgmms2ContextManagerActive)
        Status = STATUS_INVALID_DEVICE_STATE;
    else if (Context == NULL)
        Status = STATUS_INVALID_HANDLE;
    else if (Context->State != Dxgmms2ContextStreamPublicActive)
        Status = STATUS_DELETE_PENDING;
    else if (Context->QueuedEntryCount + Context->RetiredEntryCount >= Context->QueueDepth)
        Status = STATUS_DEVICE_BUSY;
    else if (Context->LastAdmittedSequence == MAXULONGLONG)
        Status = STATUS_INTEGER_OVERFLOW;
    else
    {
        Entry = Dxgmms2ContextStreamAppendEntryLocked(Context, Dxgmms2ContextEntryWait, 0, Info->ObjectId, Info->FenceValue, Info->ClientTag);
        *Sequence = Entry->Sequence;
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamAdmitSignal(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ const DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1 *Info, _Out_ ULONGLONG *TransactionId)
{
    DXGMMS2_CONTEXT_STREAM_HANDLE Handles[DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS];
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Contexts[DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS];
    ULONGLONG Sequences[DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS];
    PDXGMMS2_CONTEXT_TRANSACTION Transaction;
    SIZE_T AllocationSize;
    ULONG ContextIndex;
    ULONG CompareIndex;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (TransactionId == NULL)
        return STATUS_INVALID_PARAMETER;
    *TransactionId = 0;
    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || Info == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size < FIELD_OFFSET(DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1, Version) + sizeof(Info->Version) || Info->Size < DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Info->Version != DXGMMS2_CONTEXT_STREAM_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if ((Info->Flags & ~DXGMMS2_CONTEXT_SIGNAL_VALID_MASK) != 0 || Info->ContextCount == 0 || Info->ContextCount > DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS || Info->ContextHandles == NULL || Info->Sequences == NULL || Info->ObjectId == 0)
        return STATUS_INVALID_PARAMETER;
    for (ContextIndex = 0; ContextIndex < Info->ContextCount; ++ContextIndex)
        Handles[ContextIndex] = Info->ContextHandles[ContextIndex];
    AllocationSize = FIELD_OFFSET(DXGMMS2_CONTEXT_TRANSACTION, Members) + ((SIZE_T)Info->ContextCount * sizeof(DXGMMS2_CONTEXT_TRANSACTION_MEMBER));
    Transaction = ExAllocatePoolWithTag(NonPagedPool, AllocationSize, DXGMMS2_CONTEXT_TRANSACTION_TAG);
    if (Transaction == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Transaction, AllocationSize);
    Transaction->Signature = DXGMMS2_CONTEXT_TRANSACTION_SIGNATURE;
    Transaction->Flags = Info->Flags;
    Transaction->MemberCount = Info->ContextCount;
    Transaction->RemainingMembers = Info->ContextCount;
    InitializeListHead(&Transaction->ManagerEntry);

    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    if (Manager->State != Dxgmms2ContextManagerActive)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto ExitLocked;
    }
    for (ContextIndex = 0; ContextIndex < Info->ContextCount; ++ContextIndex)
    {
        if (Handles[ContextIndex] == NULL)
        {
            Status = STATUS_INVALID_HANDLE;
            goto ExitLocked;
        }
        Contexts[ContextIndex] = Dxgmms2ContextStreamFindLocked(Manager, Handles[ContextIndex]);
        if (Contexts[ContextIndex] == NULL)
        {
            Status = STATUS_INVALID_HANDLE;
            goto ExitLocked;
        }
        if (Contexts[ContextIndex]->State != Dxgmms2ContextStreamPublicActive)
        {
            Status = STATUS_DELETE_PENDING;
            goto ExitLocked;
        }
        if (Contexts[ContextIndex]->QueuedEntryCount + Contexts[ContextIndex]->RetiredEntryCount >= Contexts[ContextIndex]->QueueDepth)
        {
            Status = STATUS_DEVICE_BUSY;
            goto ExitLocked;
        }
        if (Contexts[ContextIndex]->LastAdmittedSequence == MAXULONGLONG)
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto ExitLocked;
        }
        for (CompareIndex = 0; CompareIndex < ContextIndex; ++CompareIndex)
        {
            if (Contexts[CompareIndex] == Contexts[ContextIndex])
            {
                Status = STATUS_INVALID_PARAMETER;
                goto ExitLocked;
            }
        }
    }
    Transaction->TransactionId = Dxgmms2ContextStreamNextNonzero64(&Manager->NextTransactionId);
    for (ContextIndex = 0; ContextIndex < Info->ContextCount; ++ContextIndex)
    {
        PDXGMMS2_CONTEXT_ENTRY Entry = Dxgmms2ContextStreamAppendEntryLocked(Contexts[ContextIndex], Dxgmms2ContextEntrySignal, Info->Flags, Info->ObjectId, Info->FenceValue, Info->ClientTag);

        Entry->Transaction = Transaction;
        Transaction->Members[ContextIndex].Context = Contexts[ContextIndex];
        Transaction->Members[ContextIndex].Sequence = Entry->Sequence;
        Sequences[ContextIndex] = Entry->Sequence;
    }
    InsertTailList(&Manager->TransactionListHead, &Transaction->ManagerEntry);
    Manager->TransactionCount++;
    *TransactionId = Transaction->TransactionId;

ExitLocked:
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    if (!NT_SUCCESS(Status))
    {
        Transaction->Signature = 0;
        ExFreePoolWithTag(Transaction, DXGMMS2_CONTEXT_TRANSACTION_TAG);
        return Status;
    }
    for (ContextIndex = 0; ContextIndex < Info->ContextCount; ++ContextIndex)
        Info->Sequences[ContextIndex] = Sequences[ContextIndex];
    return STATUS_SUCCESS;
}

NTSTATUS Dxgmms2ContextStreamResolveWait(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ ULONGLONG Sequence, _In_ NTSTATUS WaitStatus)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    PDXGMMS2_CONTEXT_ENTRY Entry;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL || Sequence == 0 || WaitStatus == STATUS_PENDING)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Context == NULL)
        Status = STATUS_INVALID_HANDLE;
    else
    {
        Entry = Dxgmms2ContextStreamFindEntryLocked(Context, Sequence);
        if (Entry == NULL)
            Status = Sequence <= Context->LastCompletedSequence ? STATUS_ALREADY_COMPLETE : STATUS_NOT_FOUND;
        else if (Entry->Type != Dxgmms2ContextEntryWait)
            Status = STATUS_OBJECT_TYPE_MISMATCH;
        else if (Entry->CancelRequested)
            Status = STATUS_CANCELLED;
        else
        {
            Entry->TerminalStatus = WaitStatus;
            Entry->WaitResolved = TRUE;
            Dxgmms2ContextStreamNormalizeLocked(Manager, Context);
        }
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

static BOOLEAN Dxgmms2ContextStreamSignalReadyLocked(_In_ PDXGMMS2_CONTEXT_TRANSACTION Transaction)
{
    ULONG MemberIndex;

    if (Transaction->Claimed || Transaction->CancelRequested)
        return FALSE;
    for (MemberIndex = 0; MemberIndex < Transaction->MemberCount; ++MemberIndex)
    {
        PDXGMMS2_CONTEXT_TRANSACTION_MEMBER Member = &Transaction->Members[MemberIndex];
        PDXGMMS2_CONTEXT_STREAM_CONTEXT Context = Member->Context;
        PDXGMMS2_CONTEXT_ENTRY Entry = Dxgmms2ContextStreamFindEntryLocked(Context, Member->Sequence);

        if (Entry == NULL || Entry->Claimed || Entry->CancelRequested || Context->State != Dxgmms2ContextStreamPublicActive || Context->LastSubmittedSequence + 1 != Entry->Sequence)
            return FALSE;
        if ((Transaction->Flags & DXGMMS2_CONTEXT_SIGNAL_AT_SUBMISSION) == 0 && Context->LastCompletedSequence + 1 != Entry->Sequence)
            return FALSE;
    }
    return TRUE;
}

NTSTATUS Dxgmms2ContextStreamClaimNextAction(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _Inout_ DXGMMS2_CONTEXT_ACTION_V1 *Action)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    PDXGMMS2_CONTEXT_ENTRY Entry;
    PDXGMMS2_CONTEXT_TRANSACTION Transaction;
    ULONGLONG ClaimToken;
    ULONGLONG NextSequence;
    ULONG MemberIndex;
    KIRQL OldIrql;
    NTSTATUS Status;

    Status = Dxgmms2ContextStreamValidateAction(Action);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    if (Manager->State != Dxgmms2ContextManagerActive && Manager->State != Dxgmms2ContextManagerStopping)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Context == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Exit;
    }
    if (Context->State != Dxgmms2ContextStreamPublicActive)
    {
        Status = STATUS_DELETE_PENDING;
        goto Exit;
    }
    Dxgmms2ContextStreamNormalizeLocked(Manager, Context);
    if (Context->LastSubmittedSequence == Context->LastAdmittedSequence)
    {
        Status = STATUS_NO_MORE_ENTRIES;
        goto Exit;
    }
    NextSequence = Context->LastSubmittedSequence + 1;
    Entry = Dxgmms2ContextStreamFindEntryLocked(Context, NextSequence);
    ASSERT(Entry != NULL);
    if (Entry == NULL)
    {
        Status = STATUS_INTERNAL_ERROR;
        goto Exit;
    }
    if (Entry->Claimed)
    {
        Status = STATUS_DEVICE_BUSY;
        goto Exit;
    }
    ClaimToken = Dxgmms2ContextStreamNextNonzero64(&Manager->NextClaimToken);
    if (Entry->Type == Dxgmms2ContextEntryWork || Entry->Type == Dxgmms2ContextEntryWait)
    {
        Entry->Claimed = TRUE;
        Entry->ClaimToken = ClaimToken;
        Context->ActiveClaimCount++;
        Manager->ActiveClaimCount++;
    }
    else if (Entry->Type == Dxgmms2ContextEntrySignal)
    {
        Transaction = Entry->Transaction;
        ASSERT(Transaction != NULL && Transaction->Signature == DXGMMS2_CONTEXT_TRANSACTION_SIGNATURE);
        if (Transaction == NULL || Transaction->Signature != DXGMMS2_CONTEXT_TRANSACTION_SIGNATURE || !Dxgmms2ContextStreamSignalReadyLocked(Transaction))
        {
            Status = STATUS_PENDING;
            goto Exit;
        }
        Transaction->Claimed = TRUE;
        Transaction->ClaimToken = ClaimToken;
        for (MemberIndex = 0; MemberIndex < Transaction->MemberCount; ++MemberIndex)
        {
            PDXGMMS2_CONTEXT_TRANSACTION_MEMBER Member = &Transaction->Members[MemberIndex];
            PDXGMMS2_CONTEXT_ENTRY MemberEntry = Dxgmms2ContextStreamFindEntryLocked(Member->Context, Member->Sequence);

            ASSERT(MemberEntry != NULL);
            MemberEntry->Claimed = TRUE;
            MemberEntry->ClaimToken = ClaimToken;
            Member->Context->ActiveClaimCount++;
        }
        Manager->ActiveClaimCount++;
    }
    else
    {
        Status = STATUS_OBJECT_TYPE_MISMATCH;
        goto Exit;
    }
    RtlZeroMemory(Action, DXGMMS2_CONTEXT_ACTION_V1_SIZE);
    Action->Size = DXGMMS2_CONTEXT_ACTION_V1_SIZE;
    Action->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Action->Type = Entry->Type == Dxgmms2ContextEntryWork ? Dxgmms2ContextActionWork : (Entry->Type == Dxgmms2ContextEntryWait ? Dxgmms2ContextActionWait : Dxgmms2ContextActionSignal);
    Action->Flags = Entry->Flags;
    Action->ContextHandle = Context->PublicHandle;
    Action->Sequence = Entry->Sequence;
    Action->TransactionId = Entry->Transaction != NULL ? Entry->Transaction->TransactionId : 0;
    Action->ClaimToken = ClaimToken;
    Action->ObjectId = Entry->ObjectId;
    Action->FenceValue = Entry->FenceValue;
    Action->ClientTag = Entry->ClientTag;
    Status = STATUS_SUCCESS;

Exit:
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamCommitAction(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ ULONGLONG Sequence, _In_ ULONGLONG ClaimToken, _In_ NTSTATUS ActionStatus)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT TransactionContexts[DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS];
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    PDXGMMS2_CONTEXT_ENTRY Entry;
    PDXGMMS2_CONTEXT_TRANSACTION Transaction;
    NTSTATUS EffectiveStatus;
    ULONG TransactionMemberCount;
    ULONG MemberIndex;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL || Sequence == 0 || ClaimToken == 0)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Context == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Exit;
    }
    Entry = Dxgmms2ContextStreamFindEntryLocked(Context, Sequence);
    if (Entry == NULL || !Entry->Claimed || Entry->ClaimToken != ClaimToken)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Exit;
    }
    EffectiveStatus = Entry->CancelRequested ? STATUS_CANCELLED : ActionStatus;
    if (Entry->Type == Dxgmms2ContextEntryWork)
    {
        ASSERT(Context->LastSubmittedSequence + 1 == Entry->Sequence);
        Entry->Claimed = FALSE;
        Entry->ClaimToken = 0;
        ASSERT(Context->ActiveClaimCount != 0 && Manager->ActiveClaimCount != 0);
        Context->ActiveClaimCount--;
        Manager->ActiveClaimCount--;
        Entry->Submitted = TRUE;
        Context->LastSubmittedSequence = Entry->Sequence;
        if (!NT_SUCCESS(EffectiveStatus))
        {
            Entry->Terminal = TRUE;
            Entry->TerminalStatus = EffectiveStatus;
        }
        Dxgmms2ContextStreamNormalizeLocked(Manager, Context);
        Status = EffectiveStatus;
        goto Exit;
    }
    if (Entry->Type == Dxgmms2ContextEntryWait)
    {
        Entry->Claimed = FALSE;
        Entry->ClaimToken = 0;
        ASSERT(Context->ActiveClaimCount != 0 && Manager->ActiveClaimCount != 0);
        Context->ActiveClaimCount--;
        Manager->ActiveClaimCount--;
        if (ActionStatus == STATUS_PENDING && Context->State == Dxgmms2ContextStreamPublicActive)
        {
            Status = STATUS_PENDING;
            goto Exit;
        }
        EffectiveStatus = Context->State == Dxgmms2ContextStreamPublicActive ? ActionStatus : STATUS_CANCELLED;
        Entry->Submitted = TRUE;
        Entry->Terminal = TRUE;
        Entry->TerminalStatus = EffectiveStatus;
        Context->LastSubmittedSequence = Entry->Sequence;
        Dxgmms2ContextStreamNormalizeLocked(Manager, Context);
        Status = EffectiveStatus;
        goto Exit;
    }
    if (Entry->Type != Dxgmms2ContextEntrySignal || Entry->Transaction == NULL)
    {
        Status = STATUS_OBJECT_TYPE_MISMATCH;
        goto Exit;
    }
    Transaction = Entry->Transaction;
    if (!Transaction->Claimed || Transaction->ClaimToken != ClaimToken)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Exit;
    }
    EffectiveStatus = Transaction->CancelRequested ? STATUS_CANCELLED : ActionStatus;
    TransactionMemberCount = Transaction->MemberCount;
    ASSERT(TransactionMemberCount <= DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS);
    for (MemberIndex = 0; MemberIndex < Transaction->MemberCount; ++MemberIndex)
    {
        PDXGMMS2_CONTEXT_TRANSACTION_MEMBER Member = &Transaction->Members[MemberIndex];
        PDXGMMS2_CONTEXT_ENTRY MemberEntry = Dxgmms2ContextStreamFindEntryLocked(Member->Context, Member->Sequence);

        ASSERT(MemberEntry != NULL && MemberEntry->Claimed && MemberEntry->ClaimToken == ClaimToken);
        ASSERT(Member->Context->LastSubmittedSequence + 1 == MemberEntry->Sequence);
        MemberEntry->Claimed = FALSE;
        MemberEntry->ClaimToken = 0;
        MemberEntry->Submitted = TRUE;
        MemberEntry->Terminal = TRUE;
        MemberEntry->TerminalStatus = EffectiveStatus;
        Member->Context->LastSubmittedSequence = MemberEntry->Sequence;
        TransactionContexts[MemberIndex] = Member->Context;
        ASSERT(Member->Context->ActiveClaimCount != 0);
        Member->Context->ActiveClaimCount--;
    }
    Transaction->Claimed = FALSE;
    Transaction->ClaimToken = 0;
    ASSERT(Manager->ActiveClaimCount != 0);
    Manager->ActiveClaimCount--;
    for (MemberIndex = 0; MemberIndex < TransactionMemberCount; ++MemberIndex)
        Dxgmms2ContextStreamNormalizeLocked(Manager, TransactionContexts[MemberIndex]);
    Status = EffectiveStatus;

Exit:
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamCompleteWork(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ ULONGLONG Sequence, _In_ NTSTATUS CompletionStatus)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    PDXGMMS2_CONTEXT_ENTRY Entry;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL || Sequence == 0 || CompletionStatus == STATUS_PENDING)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Context == NULL)
        Status = STATUS_INVALID_HANDLE;
    else
    {
        Entry = Dxgmms2ContextStreamFindEntryLocked(Context, Sequence);
        if (Entry == NULL)
            Status = Sequence <= Context->LastCompletedSequence ? STATUS_ALREADY_COMPLETE : STATUS_NOT_FOUND;
        else if (Entry->Type != Dxgmms2ContextEntryWork)
            Status = STATUS_OBJECT_TYPE_MISMATCH;
        else if (!Entry->Submitted || Entry->Claimed)
            Status = STATUS_INVALID_DEVICE_STATE;
        else if (Entry->Terminal)
            Status = STATUS_ALREADY_COMPLETE;
        else
        {
            Entry->Terminal = TRUE;
            Entry->TerminalStatus = CompletionStatus;
            Dxgmms2ContextStreamAdvanceCompletedLocked(Manager, Context);
        }
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamCancel(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ const DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1 *Info)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL || Info == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size < FIELD_OFFSET(DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1, Version) + sizeof(Info->Version) || Info->Size < DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Info->Version != DXGMMS2_CONTEXT_STREAM_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if ((Info->Flags & ~DXGMMS2_CONTEXT_CANCEL_VALID_MASK) != 0 || ((Info->Flags & DXGMMS2_CONTEXT_CANCEL_TDR) != 0 && (Info->Flags & DXGMMS2_CONTEXT_CANCEL_FORCE_SUBMITTED) == 0))
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Context == NULL)
        Status = STATUS_INVALID_HANDLE;
    else
    {
        Dxgmms2ContextStreamCancelContextLocked(Manager, Context, Info->Flags, Info->Reason);
        Dxgmms2ContextStreamNormalizeAllLocked(Manager);
        Status = Context->ActiveClaimCount == 0 ? STATUS_SUCCESS : STATUS_PENDING;
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamQuery(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _Inout_ DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 *Snapshot)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    KIRQL OldIrql;
    NTSTATUS Status;

    Status = Dxgmms2ContextStreamValidateSnapshot(Snapshot);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Context == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else
    {
        RtlZeroMemory(Snapshot, DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1_SIZE);
        Snapshot->Size = DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1_SIZE;
        Snapshot->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
        Snapshot->Generation = Manager->Generation;
        Snapshot->State = Context->State;
        Snapshot->NodeOrdinal = Context->NodeOrdinal;
        Snapshot->QueueDepth = Context->QueueDepth;
        Snapshot->QueuedEntryCount = Context->QueuedEntryCount;
        Snapshot->ActiveClaimCount = Context->ActiveClaimCount;
        Snapshot->CancelReason = Context->CancelReason;
        Snapshot->RetiredEntryCount = Context->RetiredEntryCount;
        Snapshot->LastAdmittedSequence = Context->LastAdmittedSequence;
        Snapshot->LastSubmittedSequence = Context->LastSubmittedSequence;
        Snapshot->LastCompletedSequence = Context->LastCompletedSequence;
        Status = STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}

NTSTATUS Dxgmms2ContextStreamDrainRetirements(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _Out_writes_to_(RecordCapacity, *RetiredCount) DXGMMS2_CONTEXT_RETIREMENT_V1 *Records, _In_ ULONG RecordCapacity, _Out_ ULONG *RetiredCount)
{
    PDXGMMS2_CONTEXT_STREAM_CONTEXT Context;
    ULONG AvailableCount;
    ULONG DrainCount;
    ULONG RecordIndex;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (RetiredCount == NULL)
        return STATUS_INVALID_PARAMETER;
    *RetiredCount = 0;
    if (Manager == NULL || Manager->Signature != DXGMMS2_CONTEXT_MANAGER_SIGNATURE || ContextHandle == NULL || (RecordCapacity != 0 && Records == NULL))
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Manager->Lock, &OldIrql);
    Context = Dxgmms2ContextStreamFindLocked(Manager, ContextHandle);
    if (Context == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Exit;
    }
    AvailableCount = Context->RetiredEntryCount;
    if (AvailableCount != 0 && RecordCapacity == 0)
    {
        *RetiredCount = AvailableCount;
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }
    DrainCount = min(AvailableCount, RecordCapacity);
    for (RecordIndex = 0; RecordIndex < DrainCount; ++RecordIndex)
    {
        ULONG RetirementIndex = (Context->RetirementHeadIndex + RecordIndex) % Context->QueueDepth;

        Records[RecordIndex] = Context->Retirements[RetirementIndex];
        RtlZeroMemory(&Context->Retirements[RetirementIndex], sizeof(Context->Retirements[RetirementIndex]));
    }
    Context->RetirementHeadIndex = (Context->RetirementHeadIndex + DrainCount) % Context->QueueDepth;
    Context->RetiredEntryCount -= DrainCount;
    *RetiredCount = DrainCount;
    Status = DrainCount < AvailableCount ? STATUS_MORE_ENTRIES : STATUS_SUCCESS;

Exit:
    KeReleaseSpinLock(&Manager->Lock, OldIrql);
    return Status;
}
