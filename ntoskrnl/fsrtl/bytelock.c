/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Byte-range file lock package for file system drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

PAGED_LOOKASIDE_LIST FsRtlFileLockLookasideList;

typedef struct _FSRTL_GRANTED_LOCK
{
    LIST_ENTRY Links;
    FILE_LOCK_INFO Info;
} FSRTL_GRANTED_LOCK, *PFSRTL_GRANTED_LOCK;

typedef struct _FSRTL_LOCK_STATE
{
    FAST_MUTEX Mutex;
    LIST_ENTRY Granted;
    ULONG GrantedCount;
    LIST_ENTRY Waiting;
} FSRTL_LOCK_STATE, *PFSRTL_LOCK_STATE;

#define FSRTL_IRP_CONTEXT(Irp)  ((Irp)->Tail.Overlay.DriverContext[0])
#define FSRTL_IRP_COMPLETE(Irp) ((Irp)->Tail.Overlay.DriverContext[1])
#define FSRTL_IRP_ENTRY(Irp)    ((Irp)->Tail.Overlay.DriverContext[2])

static
BOOLEAN
FsRtlpRangeIsValid(
    _In_ ULONGLONG Start,
    _In_ ULONGLONG Length)
{
    return Length == 0 || Start + (Length - 1) >= Start;
}

static
BOOLEAN
FsRtlpLocksOverlap(
    _In_ PFILE_LOCK_INFO A,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG Length)
{
    ULONGLONG AStart = (ULONGLONG)A->StartingByte.QuadPart;
    ULONGLONG ALength = (ULONGLONG)A->Length.QuadPart;

    if (ALength == 0 && Length == 0)
        return FALSE;
    if (ALength == 0)
        return AStart > Start && AStart - Start < Length;
    if (Length == 0)
        return Start > AStart && Start - AStart < ALength;
    return AStart <= Start + (Length - 1) && Start <= AStart + (ALength - 1);
}

static
BOOLEAN
FsRtlpIoOverlaps(
    _In_ PFILE_LOCK_INFO Lock,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG Length)
{
    ULONGLONG LockStart = (ULONGLONG)Lock->StartingByte.QuadPart;
    ULONGLONG LockLength = (ULONGLONG)Lock->Length.QuadPart;
    ULONGLONG End;

    if (LockLength == 0 || Length == 0)
        return FALSE;
    End = Start + (Length - 1);
    if (End < Start)
        End = ~0ULL;
    return LockStart <= End && Start <= LockStart + (LockLength - 1);
}

static
BOOLEAN
FsRtlpLockConflicts(
    _In_ PFSRTL_LOCK_STATE State,
    _In_ PFILE_LOCK_INFO Request)
{
    PLIST_ENTRY Entry;

    for (Entry = State->Granted.Flink; Entry != &State->Granted; Entry = Entry->Flink)
    {
        PFSRTL_GRANTED_LOCK Lock = CONTAINING_RECORD(Entry, FSRTL_GRANTED_LOCK, Links);

        if (!FsRtlpLocksOverlap(&Lock->Info,
                                (ULONGLONG)Request->StartingByte.QuadPart,
                                (ULONGLONG)Request->Length.QuadPart))
        {
            continue;
        }
        if (Request->ExclusiveLock)
            return TRUE;
        if (Lock->Info.ExclusiveLock &&
            (Lock->Info.FileObject != Request->FileObject ||
             Lock->Info.ProcessId != Request->ProcessId))
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
BOOLEAN
FsRtlpIoAllowed(
    _In_ PFILE_LOCK FileLock,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG Length,
    _In_ ULONG Key,
    _In_ PFILE_OBJECT FileObject,
    _In_ PVOID Process,
    _In_ BOOLEAN Write)
{
    PFSRTL_LOCK_STATE State = FileLock->LockInformation;
    PLIST_ENTRY Entry;
    BOOLEAN Allowed = TRUE;

    if (!State || Length == 0)
        return TRUE;

    ExAcquireFastMutex(&State->Mutex);
    for (Entry = State->Granted.Flink; Entry != &State->Granted; Entry = Entry->Flink)
    {
        PFSRTL_GRANTED_LOCK Lock = CONTAINING_RECORD(Entry, FSRTL_GRANTED_LOCK, Links);

        if (!FsRtlpIoOverlaps(&Lock->Info, Start, Length))
            continue;
        if (!Lock->Info.ExclusiveLock)
        {
            if (Write)
            {
                Allowed = FALSE;
                break;
            }
            continue;
        }
        if (Lock->Info.FileObject != FileObject ||
            Lock->Info.ProcessId != Process ||
            Lock->Info.Key != Key)
        {
            Allowed = FALSE;
            break;
        }
    }
    ExReleaseFastMutex(&State->Mutex);
    return Allowed;
}

static
PFSRTL_LOCK_STATE
FsRtlpGetLockState(
    _In_ PFILE_LOCK FileLock)
{
    PFSRTL_LOCK_STATE State, Existing;

    State = FileLock->LockInformation;
    if (State)
        return State;

    State = ExAllocatePoolWithTag(NonPagedPool, sizeof(*State), TAG_FLOCK);
    if (!State)
        return NULL;

    ExInitializeFastMutex(&State->Mutex);
    InitializeListHead(&State->Granted);
    State->GrantedCount = 0;
    InitializeListHead(&State->Waiting);

    Existing = InterlockedCompareExchangePointer(&FileLock->LockInformation, State, NULL);
    if (Existing)
    {
        ExFreePoolWithTag(State, TAG_FLOCK);
        return Existing;
    }
    return State;
}

static
VOID
FsRtlpInsertGrantedLock(
    _In_ PFILE_LOCK FileLock,
    _In_ PFSRTL_LOCK_STATE State,
    _In_ PFSRTL_GRANTED_LOCK Lock)
{
    PLIST_ENTRY Entry;

    for (Entry = State->Granted.Flink; Entry != &State->Granted; Entry = Entry->Flink)
    {
        PFSRTL_GRANTED_LOCK Next = CONTAINING_RECORD(Entry, FSRTL_GRANTED_LOCK, Links);

        if ((ULONGLONG)Next->Info.StartingByte.QuadPart > (ULONGLONG)Lock->Info.StartingByte.QuadPart)
            break;
    }
    InsertTailList(Entry, &Lock->Links);
    State->GrantedCount++;
    FileLock->FastIoIsQuestionable = TRUE;
}

static
VOID
FsRtlpRemoveGrantedLock(
    _In_ PFILE_LOCK FileLock,
    _In_ PFSRTL_LOCK_STATE State,
    _In_ PFSRTL_GRANTED_LOCK Lock)
{
    RemoveEntryList(&Lock->Links);
    State->GrantedCount--;
    if (State->GrantedCount == 0)
        FileLock->FastIoIsQuestionable = FALSE;
}

static
VOID
FsRtlpFillLockInfo(
    _Out_ PFILE_LOCK_INFO Info,
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ PVOID Process,
    _In_ ULONG Key,
    _In_ BOOLEAN ExclusiveLock)
{
    Info->StartingByte = *FileOffset;
    Info->Length = *Length;
    Info->ExclusiveLock = ExclusiveLock;
    Info->Key = Key;
    Info->FileObject = FileObject;
    Info->ProcessId = Process;
    Info->EndingByte.QuadPart = (LONGLONG)((ULONGLONG)FileOffset->QuadPart + (ULONGLONG)Length->QuadPart - 1);
}

static
NTSTATUS
FsRtlpCompleteLockIrp(
    _In_opt_ PCOMPLETE_LOCK_IRP_ROUTINE CompleteRoutine,
    _In_opt_ PVOID Context,
    _In_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    Irp->IoStatus.Information = 0;
    if (CompleteRoutine)
    {
        Irp->IoStatus.Status = Status;
        return CompleteRoutine(Context, Irp);
    }

    FsRtlCompleteRequest(Irp, Status);
    return Status;
}

static
VOID
NTAPI
FsRtlpCancelLockIrp(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCOMPLETE_LOCK_IRP_ROUTINE CompleteRoutine = FSRTL_IRP_COMPLETE(Irp);
    PVOID Context = FSRTL_IRP_CONTEXT(Irp);
    PFSRTL_GRANTED_LOCK Lock = FSRTL_IRP_ENTRY(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);

    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    IoReleaseCancelSpinLock(Irp->CancelIrql);

    ExFreePoolWithTag(Lock, TAG_RANGE);
    (VOID)FsRtlpCompleteLockIrp(CompleteRoutine, Context, Irp, STATUS_CANCELLED);
}

static
VOID
FsRtlpGrantWaiters(
    _In_ PFILE_LOCK FileLock,
    _In_ PFSRTL_LOCK_STATE State,
    _Inout_ PLIST_ENTRY Completed)
{
    PLIST_ENTRY Entry, Next;
    KIRQL Irql;

    IoAcquireCancelSpinLock(&Irql);
    for (Entry = State->Waiting.Flink; Entry != &State->Waiting; Entry = Next)
    {
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        PFSRTL_GRANTED_LOCK Lock = FSRTL_IRP_ENTRY(Irp);

        Next = Entry->Flink;
        if (FsRtlpLockConflicts(State, &Lock->Info))
            continue;

        (VOID)IoSetCancelRoutine(Irp, NULL);
        RemoveEntryList(Entry);
        FsRtlpInsertGrantedLock(FileLock, State, Lock);
        InsertTailList(Completed, Entry);
    }
    IoReleaseCancelSpinLock(Irql);
}

static
VOID
FsRtlpRemoveLockIfPresent(
    _In_ PFILE_LOCK FileLock,
    _In_ PFSRTL_GRANTED_LOCK Target);

static
VOID
FsRtlpCompleteGranted(
    _In_ PFILE_LOCK FileLock,
    _Inout_ PLIST_ENTRY Completed)
{
    while (!IsListEmpty(Completed))
    {
        PLIST_ENTRY Entry = RemoveHeadList(Completed);
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        PCOMPLETE_LOCK_IRP_ROUTINE CompleteRoutine = FSRTL_IRP_COMPLETE(Irp);
        PVOID Context = FSRTL_IRP_CONTEXT(Irp);
        PFSRTL_GRANTED_LOCK Lock = FSRTL_IRP_ENTRY(Irp);
        NTSTATUS Status;

        Status = FsRtlpCompleteLockIrp(CompleteRoutine, Context, Irp, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            FsRtlpRemoveLockIfPresent(FileLock, Lock);
    }
}

static
VOID
FsRtlpRemoveLockIfPresent(
    _In_ PFILE_LOCK FileLock,
    _In_ PFSRTL_GRANTED_LOCK Target)
{
    PFSRTL_LOCK_STATE State = FileLock->LockInformation;
    LIST_ENTRY Completed;
    PLIST_ENTRY Entry;
    BOOLEAN Found = FALSE;

    if (!State)
        return;

    InitializeListHead(&Completed);
    ExAcquireFastMutex(&State->Mutex);
    for (Entry = State->Granted.Flink; Entry != &State->Granted; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, FSRTL_GRANTED_LOCK, Links) == Target)
        {
            FsRtlpRemoveGrantedLock(FileLock, State, Target);
            Found = TRUE;
            break;
        }
    }
    if (Found)
        FsRtlpGrantWaiters(FileLock, State, &Completed);
    ExReleaseFastMutex(&State->Mutex);

    if (Found)
        ExFreePoolWithTag(Target, TAG_RANGE);
    FsRtlpCompleteGranted(FileLock, &Completed);
}

_Must_inspect_result_
BOOLEAN
NTAPI
FsRtlPrivateLock(
    _In_ PFILE_LOCK FileLock,
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ PEPROCESS Process,
    _In_ ULONG Key,
    _In_ BOOLEAN FailImmediately,
    _In_ BOOLEAN ExclusiveLock,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID Context,
    _In_ BOOLEAN AlreadySynchronized)
{
    PFSRTL_LOCK_STATE State;
    PFSRTL_GRANTED_LOCK Lock;
    NTSTATUS Status;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(AlreadySynchronized);

    IoStatus->Information = 0;

    if (!FsRtlpRangeIsValid((ULONGLONG)FileOffset->QuadPart, (ULONGLONG)Length->QuadPart))
    {
        IoStatus->Status = STATUS_INVALID_PARAMETER;
        if (Irp)
            IoStatus->Status = FsRtlpCompleteLockIrp(FileLock->CompleteLockIrpRoutine, Context, Irp, STATUS_INVALID_PARAMETER);
        return TRUE;
    }

    State = FsRtlpGetLockState(FileLock);
    Lock = State ? ExAllocatePoolWithTag(NonPagedPool, sizeof(*Lock), TAG_RANGE) : NULL;
    if (!Lock)
    {
        if (!Irp)
            return FALSE;
        IoStatus->Status = FsRtlpCompleteLockIrp(FileLock->CompleteLockIrpRoutine, Context, Irp, STATUS_INSUFFICIENT_RESOURCES);
        return TRUE;
    }

    FsRtlpFillLockInfo(&Lock->Info, FileObject, FileOffset, Length, Process, Key, ExclusiveLock);

    ExAcquireFastMutex(&State->Mutex);
    if (!FsRtlpLockConflicts(State, &Lock->Info))
    {
        FsRtlpInsertGrantedLock(FileLock, State, Lock);
        ExReleaseFastMutex(&State->Mutex);

        IoStatus->Status = STATUS_SUCCESS;
        if (Irp)
        {
            Status = FsRtlpCompleteLockIrp(FileLock->CompleteLockIrpRoutine, Context, Irp, STATUS_SUCCESS);
            if (!NT_SUCCESS(Status))
                FsRtlpRemoveLockIfPresent(FileLock, Lock);
            IoStatus->Status = Status;
        }
        return TRUE;
    }

    if (FailImmediately || !Irp)
    {
        ExReleaseFastMutex(&State->Mutex);
        ExFreePoolWithTag(Lock, TAG_RANGE);

        IoStatus->Status = STATUS_FILE_LOCK_CONFLICT;
        if (!Irp)
            return FailImmediately;
        IoStatus->Status = FsRtlpCompleteLockIrp(FileLock->CompleteLockIrpRoutine, Context, Irp, STATUS_FILE_LOCK_CONFLICT);
        return TRUE;
    }

    FSRTL_IRP_CONTEXT(Irp) = Context;
    FSRTL_IRP_COMPLETE(Irp) = FileLock->CompleteLockIrpRoutine;
    FSRTL_IRP_ENTRY(Irp) = Lock;

    IoAcquireCancelSpinLock(&Irql);
    if (Irp->Cancel)
    {
        IoReleaseCancelSpinLock(Irql);
        ExReleaseFastMutex(&State->Mutex);
        ExFreePoolWithTag(Lock, TAG_RANGE);
        IoStatus->Status = FsRtlpCompleteLockIrp(FileLock->CompleteLockIrpRoutine, Context, Irp, STATUS_CANCELLED);
        return TRUE;
    }
    IoMarkIrpPending(Irp);
    InsertTailList(&State->Waiting, &Irp->Tail.Overlay.ListEntry);
    (VOID)IoSetCancelRoutine(Irp, FsRtlpCancelLockIrp);
    IoReleaseCancelSpinLock(Irql);
    ExReleaseFastMutex(&State->Mutex);

    IoStatus->Status = STATUS_PENDING;
    return TRUE;
}

typedef enum _FSRTL_UNLOCK_MATCH
{
    FsRtlUnlockAllForOwner,
    FsRtlUnlockAllForKey
} FSRTL_UNLOCK_MATCH;

static
NTSTATUS
FsRtlpUnlockMatching(
    _In_ PFILE_LOCK FileLock,
    _In_ PFILE_OBJECT FileObject,
    _In_ PVOID Process,
    _In_ ULONG Key,
    _In_ FSRTL_UNLOCK_MATCH Match,
    _In_opt_ PVOID Context)
{
    PFSRTL_LOCK_STATE State = FileLock->LockInformation;
    LIST_ENTRY Released, Completed;
    PLIST_ENTRY Entry, Next;

    if (!State)
        return STATUS_RANGE_NOT_LOCKED;

    InitializeListHead(&Released);
    InitializeListHead(&Completed);

    ExAcquireFastMutex(&State->Mutex);
    for (Entry = State->Granted.Flink; Entry != &State->Granted; Entry = Next)
    {
        PFSRTL_GRANTED_LOCK Lock = CONTAINING_RECORD(Entry, FSRTL_GRANTED_LOCK, Links);

        Next = Entry->Flink;
        if (Lock->Info.FileObject != FileObject || Lock->Info.ProcessId != Process)
            continue;
        if (Match == FsRtlUnlockAllForKey && Lock->Info.Key != Key)
            continue;

        FsRtlpRemoveGrantedLock(FileLock, State, Lock);
        InsertTailList(&Released, &Lock->Links);
    }
    if (!IsListEmpty(&Released))
        FsRtlpGrantWaiters(FileLock, State, &Completed);
    ExReleaseFastMutex(&State->Mutex);

    while (!IsListEmpty(&Released))
    {
        PFSRTL_GRANTED_LOCK Lock = CONTAINING_RECORD(RemoveHeadList(&Released), FSRTL_GRANTED_LOCK, Links);

        if (FileLock->UnlockRoutine)
            FileLock->UnlockRoutine(Context, &Lock->Info);
        ExFreePoolWithTag(Lock, TAG_RANGE);
    }
    FsRtlpCompleteGranted(FileLock, &Completed);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FsRtlFastUnlockSingle(
    _In_ PFILE_LOCK FileLock,
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ PEPROCESS Process,
    _In_ ULONG Key,
    _In_opt_ PVOID Context,
    _In_ BOOLEAN AlreadySynchronized)
{
    PFSRTL_LOCK_STATE State = FileLock->LockInformation;
    PFSRTL_GRANTED_LOCK Found = NULL;
    LIST_ENTRY Completed;
    PLIST_ENTRY Entry;
    ULONG Pass;

    UNREFERENCED_PARAMETER(AlreadySynchronized);

    if (!State)
        return STATUS_RANGE_NOT_LOCKED;

    InitializeListHead(&Completed);

    ExAcquireFastMutex(&State->Mutex);
    for (Pass = 0; Pass < 2 && !Found; Pass++)
    {
        for (Entry = State->Granted.Flink; Entry != &State->Granted; Entry = Entry->Flink)
        {
            PFSRTL_GRANTED_LOCK Lock = CONTAINING_RECORD(Entry, FSRTL_GRANTED_LOCK, Links);

            if (Lock->Info.ExclusiveLock != (Pass == 0))
                continue;
            if (Lock->Info.StartingByte.QuadPart == FileOffset->QuadPart &&
                Lock->Info.Length.QuadPart == Length->QuadPart &&
                Lock->Info.FileObject == FileObject &&
                Lock->Info.ProcessId == Process &&
                Lock->Info.Key == Key)
            {
                Found = Lock;
                break;
            }
        }
    }
    if (Found)
    {
        FsRtlpRemoveGrantedLock(FileLock, State, Found);
        FsRtlpGrantWaiters(FileLock, State, &Completed);
    }
    ExReleaseFastMutex(&State->Mutex);

    if (!Found)
        return STATUS_RANGE_NOT_LOCKED;

    if (FileLock->UnlockRoutine)
        FileLock->UnlockRoutine(Context, &Found->Info);
    ExFreePoolWithTag(Found, TAG_RANGE);
    FsRtlpCompleteGranted(FileLock, &Completed);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FsRtlFastUnlockAll(
    _In_ PFILE_LOCK FileLock,
    _In_ PFILE_OBJECT FileObject,
    _In_ PEPROCESS Process,
    _In_opt_ PVOID Context)
{
    return FsRtlpUnlockMatching(FileLock, FileObject, Process, 0, FsRtlUnlockAllForOwner, Context);
}

NTSTATUS
NTAPI
FsRtlFastUnlockAllByKey(
    _In_ PFILE_LOCK FileLock,
    _In_ PFILE_OBJECT FileObject,
    _In_ PEPROCESS Process,
    _In_ ULONG Key,
    _In_opt_ PVOID Context)
{
    return FsRtlpUnlockMatching(FileLock, FileObject, Process, Key, FsRtlUnlockAllForKey, Context);
}

_Must_inspect_result_
NTSTATUS
NTAPI
FsRtlProcessFileLock(
    _In_ PFILE_LOCK FileLock,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    ASSERT(IoStack->MajorFunction == IRP_MJ_LOCK_CONTROL);

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_LOCK:
            (VOID)FsRtlPrivateLock(FileLock,
                                   IoStack->FileObject,
                                   &IoStack->Parameters.LockControl.ByteOffset,
                                   IoStack->Parameters.LockControl.Length,
                                   IoGetRequestorProcess(Irp),
                                   IoStack->Parameters.LockControl.Key,
                                   BooleanFlagOn(IoStack->Flags, SL_FAIL_IMMEDIATELY),
                                   BooleanFlagOn(IoStack->Flags, SL_EXCLUSIVE_LOCK),
                                   &IoStatus,
                                   Irp,
                                   Context,
                                   FALSE);
            return IoStatus.Status;

        case IRP_MN_UNLOCK_SINGLE:
            Status = FsRtlFastUnlockSingle(FileLock,
                                           IoStack->FileObject,
                                           &IoStack->Parameters.LockControl.ByteOffset,
                                           IoStack->Parameters.LockControl.Length,
                                           IoGetRequestorProcess(Irp),
                                           IoStack->Parameters.LockControl.Key,
                                           Context,
                                           FALSE);
            break;

        case IRP_MN_UNLOCK_ALL:
            Status = FsRtlFastUnlockAll(FileLock,
                                        IoStack->FileObject,
                                        IoGetRequestorProcess(Irp),
                                        Context);
            break;

        case IRP_MN_UNLOCK_ALL_BY_KEY:
            Status = FsRtlFastUnlockAllByKey(FileLock,
                                             IoStack->FileObject,
                                             IoGetRequestorProcess(Irp),
                                             IoStack->Parameters.LockControl.Key,
                                             Context);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    return FsRtlpCompleteLockIrp(FileLock->CompleteLockIrpRoutine, Context, Irp, Status);
}

BOOLEAN
NTAPI
FsRtlCheckLockForReadAccess(
    _In_ PFILE_LOCK FileLock,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    return FsRtlpIoAllowed(FileLock,
                           (ULONGLONG)IoStack->Parameters.Read.ByteOffset.QuadPart,
                           IoStack->Parameters.Read.Length,
                           IoStack->Parameters.Read.Key,
                           IoStack->FileObject,
                           IoGetRequestorProcess(Irp),
                           FALSE);
}

BOOLEAN
NTAPI
FsRtlCheckLockForWriteAccess(
    _In_ PFILE_LOCK FileLock,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    return FsRtlpIoAllowed(FileLock,
                           (ULONGLONG)IoStack->Parameters.Write.ByteOffset.QuadPart,
                           IoStack->Parameters.Write.Length,
                           IoStack->Parameters.Write.Key,
                           IoStack->FileObject,
                           IoGetRequestorProcess(Irp),
                           TRUE);
}

BOOLEAN
NTAPI
FsRtlFastCheckLockForRead(
    _In_ PFILE_LOCK FileLock,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ ULONG Key,
    _In_ PFILE_OBJECT FileObject,
    _In_ PVOID Process)
{
    return FsRtlpIoAllowed(FileLock,
                           (ULONGLONG)FileOffset->QuadPart,
                           (ULONGLONG)Length->QuadPart,
                           Key,
                           FileObject,
                           Process,
                           FALSE);
}

BOOLEAN
NTAPI
FsRtlFastCheckLockForWrite(
    _In_ PFILE_LOCK FileLock,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ ULONG Key,
    _In_ PFILE_OBJECT FileObject,
    _In_ PVOID Process)
{
    return FsRtlpIoAllowed(FileLock,
                           (ULONGLONG)FileOffset->QuadPart,
                           (ULONGLONG)Length->QuadPart,
                           Key,
                           FileObject,
                           Process,
                           TRUE);
}

PFILE_LOCK_INFO
NTAPI
FsRtlGetNextFileLock(
    _In_ PFILE_LOCK FileLock,
    _In_ BOOLEAN Restart)
{
    PFSRTL_LOCK_STATE State = FileLock->LockInformation;
    PFSRTL_GRANTED_LOCK Lock = NULL;
    PLIST_ENTRY Entry;

    if (!State)
        return NULL;

    ExAcquireFastMutex(&State->Mutex);
    if (Restart)
    {
        Entry = State->Granted.Flink;
    }
    else
    {
        for (Entry = State->Granted.Flink; Entry != &State->Granted; Entry = Entry->Flink)
        {
            if (CONTAINING_RECORD(Entry, FSRTL_GRANTED_LOCK, Links) == FileLock->LastReturnedLock)
            {
                Entry = Entry->Flink;
                break;
            }
        }
    }
    if (Entry != &State->Granted)
    {
        Lock = CONTAINING_RECORD(Entry, FSRTL_GRANTED_LOCK, Links);
        FileLock->LastReturnedLockInfo = Lock->Info;
    }
    FileLock->LastReturnedLock = Lock;
    ExReleaseFastMutex(&State->Mutex);

    return Lock ? &FileLock->LastReturnedLockInfo : NULL;
}

VOID
NTAPI
FsRtlInitializeFileLock(
    _Out_ PFILE_LOCK FileLock,
    _In_opt_ PCOMPLETE_LOCK_IRP_ROUTINE CompleteLockIrpRoutine,
    _In_opt_ PUNLOCK_ROUTINE UnlockRoutine)
{
    RtlZeroMemory(FileLock, sizeof(*FileLock));
    FileLock->CompleteLockIrpRoutine = CompleteLockIrpRoutine;
    FileLock->UnlockRoutine = UnlockRoutine;
}

VOID
NTAPI
FsRtlUninitializeFileLock(
    _Inout_ PFILE_LOCK FileLock)
{
    PFSRTL_LOCK_STATE State = FileLock->LockInformation;
    LIST_ENTRY Pending;
    KIRQL Irql;

    if (!State)
        return;

    InitializeListHead(&Pending);

    IoAcquireCancelSpinLock(&Irql);
    while (!IsListEmpty(&State->Waiting))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&State->Waiting);
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        (VOID)IoSetCancelRoutine(Irp, NULL);
        InsertTailList(&Pending, Entry);
    }
    IoReleaseCancelSpinLock(Irql);

    while (!IsListEmpty(&Pending))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Pending), IRP, Tail.Overlay.ListEntry);

        ExFreePoolWithTag(FSRTL_IRP_ENTRY(Irp), TAG_RANGE);
        (VOID)FsRtlpCompleteLockIrp(FSRTL_IRP_COMPLETE(Irp), FSRTL_IRP_CONTEXT(Irp), Irp, STATUS_RANGE_NOT_LOCKED);
    }

    while (!IsListEmpty(&State->Granted))
    {
        PFSRTL_GRANTED_LOCK Lock = CONTAINING_RECORD(RemoveHeadList(&State->Granted), FSRTL_GRANTED_LOCK, Links);

        ExFreePoolWithTag(Lock, TAG_RANGE);
    }

    ExFreePoolWithTag(State, TAG_FLOCK);
    FileLock->LockInformation = NULL;
    FileLock->LastReturnedLock = NULL;
    FileLock->FastIoIsQuestionable = FALSE;
}

PFILE_LOCK
NTAPI
FsRtlAllocateFileLock(
    _In_opt_ PCOMPLETE_LOCK_IRP_ROUTINE CompleteLockIrpRoutine,
    _In_opt_ PUNLOCK_ROUTINE UnlockRoutine)
{
    PFILE_LOCK FileLock;

    FileLock = ExAllocateFromPagedLookasideList(&FsRtlFileLockLookasideList);
    if (FileLock)
        FsRtlInitializeFileLock(FileLock, CompleteLockIrpRoutine, UnlockRoutine);
    return FileLock;
}

VOID
NTAPI
FsRtlFreeFileLock(
    _In_ PFILE_LOCK FileLock)
{
    FsRtlUninitializeFileLock(FileLock);
    ExFreeToPagedLookasideList(&FsRtlFileLockLookasideList, FileLock);
}

BOOLEAN
NTAPI
FsRtlCheckLockForOplockRequest(
    _In_ PFILE_LOCK FileLock,
    _In_ PLARGE_INTEGER AllocationSize)
{
    UNREFERENCED_PARAMETER(AllocationSize);

    return !FsRtlAreThereCurrentFileLocks(FileLock);
}

BOOLEAN
NTAPI
FsRtlAreThereWaitingFileLocks(
    _In_ PFILE_LOCK FileLock)
{
    PFSRTL_LOCK_STATE State = FileLock->LockInformation;
    BOOLEAN Waiting;
    KIRQL Irql;

    if (!State)
        return FALSE;

    IoAcquireCancelSpinLock(&Irql);
    Waiting = !IsListEmpty(&State->Waiting);
    IoReleaseCancelSpinLock(Irql);
    return Waiting;
}
