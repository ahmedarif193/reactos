/*
 * PROJECT:         ReactOS Run-Time Library
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * PURPOSE:         Rtl Trace Routines
 */

/* INCLUDES *******************************************************************/

#include <rtl.h>
#include "rtlp.h"
#define NDEBUG
#include <debug.h>

static RTL_UNLOAD_EVENT_TRACE RtlpUnloadEventTrace[RTL_UNLOAD_EVENT_TRACE_NUMBER];
static UINT RtlpUnloadEventTraceIndex = 0;

#ifndef _BLDR_
PVOID
NTAPI
RtlpDebugBufferCommit(_Inout_ PRTL_DEBUG_INFORMATION Buffer,
                      _In_ SIZE_T Size);
#endif

#define TAG_STACKTRACE 'tSRt'
#define RTL_STACKTRACE_MAX_DEPTH 32
#define RTL_STACKTRACE_BUCKETS   1024

static PSTACK_TRACE_DATABASE RtlpStackTraceDataBase;
static RTL_TRACE_DATABASE RtlpUserTraceDb;
static RTL_TRACE_SEGMENT RtlpUserTraceSegment;
static RTL_TRACE_BLOCK *RtlpUserTraceBuckets[RTL_STACKTRACE_BUCKETS];
static PVOID RtlpTraceArenaBase;
static SIZE_T RtlpTraceArenaSize;
static SIZE_T RtlpTraceArenaUsed;
static SIZE_T RtlpTraceArenaCommit;
static RTL_CRITICAL_SECTION RtlpTraceArenaLock;
static RTL_CRITICAL_SECTION RtlpStackTraceInitLock;
static LONG RtlpTraceLockInitState;
static PVOID RtlpTraceAlloc(SIZE_T Size);

__forceinline
PRTL_CRITICAL_SECTION
RtlpTraceDatabaseLock(
    _In_ PSTACK_TRACE_DATABASE DataBase)
{
    return (PRTL_CRITICAL_SECTION)&DataBase->Lock;
}

static
VOID
RtlpWaitForTraceLockInitialization(VOID)
{
#ifdef NTOS_MODE_USER
    NtYieldExecution();
#else
    LARGE_INTEGER Interval;
    Interval.QuadPart = -10000; /* ~1 ms */
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
#endif
}

static
NTSTATUS
RtlpInitializeTraceLocks(VOID)
{
    NTSTATUS Status;
    LONG InitState;

    if (RtlpTraceLockInitState == 1)
        return STATUS_SUCCESS;

    while (TRUE)
    {
        InitState = InterlockedCompareExchange(&RtlpTraceLockInitState, 2, 0);
        if (InitState == 1)
            return STATUS_SUCCESS;

        if (InitState == 0)
        {
            Status = RtlInitializeCriticalSection(&RtlpTraceArenaLock);
            if (!NT_SUCCESS(Status))
            {
                InterlockedExchange(&RtlpTraceLockInitState, 0);
                return Status;
            }

            Status = RtlInitializeCriticalSection(&RtlpStackTraceInitLock);
            if (!NT_SUCCESS(Status))
            {
                RtlDeleteCriticalSection(&RtlpTraceArenaLock);
                InterlockedExchange(&RtlpTraceLockInitState, 0);
                return Status;
            }

            InterlockedExchange(&RtlpTraceLockInitState, 1);
            return STATUS_SUCCESS;
        }

        RtlpWaitForTraceLockInitialization();
    }
}

static
NTSTATUS
RtlpInitializeStackTraceDataBase(VOID)
{
    PSTACK_TRACE_DATABASE DataBase;
    SIZE_T HeaderSize, IndexArraySize, TotalSize;
    NTSTATUS Status;

    if (RtlpStackTraceDataBase)
        return STATUS_SUCCESS;

    Status = RtlpInitializeTraceLocks();
    if (!NT_SUCCESS(Status))
        return Status;

    RtlEnterCriticalSection(&RtlpStackTraceInitLock);
    if (RtlpStackTraceDataBase)
    {
        RtlLeaveCriticalSection(&RtlpStackTraceInitLock);
        return STATUS_SUCCESS;
    }

    HeaderSize = FIELD_OFFSET(STACK_TRACE_DATABASE, Buckets) +
                 (RTL_STACKTRACE_BUCKETS * sizeof(PRTL_STACK_TRACE_ENTRY));
    IndexArraySize = sizeof(PRTL_STACK_TRACE_ENTRY) * 0x10000;
    TotalSize = HeaderSize + IndexArraySize;

    /* Allocate the header from a private arena to avoid heap recursion */
    DataBase = (PSTACK_TRACE_DATABASE)RtlpTraceAlloc(TotalSize);
    if (!DataBase)
    {
        RtlLeaveCriticalSection(&RtlpStackTraceInitLock);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(DataBase, TotalSize);
    DataBase->NumberOfBuckets = RTL_STACKTRACE_BUCKETS;
    DataBase->CommitBase = DataBase;
    DataBase->CurrentLowerCommitLimit = DataBase;
    DataBase->CurrentUpperCommitLimit = (PCHAR)DataBase + HeaderSize;
    DataBase->NextFreeLowerMemory = (PCHAR)DataBase + HeaderSize;
    DataBase->NextFreeUpperMemory = (PCHAR)DataBase + HeaderSize;
    DataBase->EntryIndexArray = (PRTL_STACK_TRACE_ENTRY)((PUCHAR)DataBase + HeaderSize);

    Status = RtlInitializeCriticalSection(RtlpTraceDatabaseLock(DataBase));
    if (!NT_SUCCESS(Status))
    {
        RtlLeaveCriticalSection(&RtlpStackTraceInitLock);
        return Status;
    }

    RtlpStackTraceDataBase = DataBase;
    RtlLeaveCriticalSection(&RtlpStackTraceInitLock);

    return STATUS_SUCCESS;
}

static
ULONG
RtlpDefaultTraceHash(ULONG Count, PVOID *Trace)
{
    ULONG i, Hash = 0;

    for (i = 0; i < Count; i++)
    {
        Hash ^= PtrToUlong(Trace[i]);
        Hash = _rotl(Hash, 3);
    }

    return Hash;
}

static
NTSTATUS
RtlpEnsureTraceArena(SIZE_T Size)
{
    SIZE_T NeededCommit;
    NTSTATUS Status;

    Size = (Size + 15) & ~((SIZE_T)15);

    if (RtlpTraceArenaBase == NULL)
    {
        SIZE_T Reserve = 0x20000; /* 128 KB reserve for trace data */
        PVOID Base = NULL;
        Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                         &Base,
                                         0,
                                         &Reserve,
                                         MEM_RESERVE,
                                         PAGE_READWRITE);
        if (!NT_SUCCESS(Status))
            return Status;
        RtlpTraceArenaBase = Base;
        RtlpTraceArenaSize = Reserve;
        RtlpTraceArenaCommit = 0;
        RtlpTraceArenaUsed = 0;
    }

    NeededCommit = RtlpTraceArenaUsed + Size;
    if (NeededCommit > RtlpTraceArenaSize)
        return STATUS_NO_MEMORY;

    if (NeededCommit > RtlpTraceArenaCommit)
    {
        SIZE_T CommitSize = (NeededCommit - RtlpTraceArenaCommit + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        PVOID CommitBase = (PVOID)((PUCHAR)RtlpTraceArenaBase + RtlpTraceArenaCommit);
        Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                         &CommitBase,
                                         0,
                                         &CommitSize,
                                         MEM_COMMIT,
                                         PAGE_READWRITE);
        if (!NT_SUCCESS(Status))
            return Status;
        RtlpTraceArenaCommit += CommitSize;
    }

    return STATUS_SUCCESS;
}

static
PVOID
RtlpTraceAlloc(SIZE_T Size)
{
    NTSTATUS Status;
    PVOID Ptr = NULL;

    Size = (Size + 15) & ~((SIZE_T)15);

    RtlEnterCriticalSection(&RtlpTraceArenaLock);

    Status = RtlpEnsureTraceArena(Size);
    if (NT_SUCCESS(Status))
    {
        Ptr = (PUCHAR)RtlpTraceArenaBase + RtlpTraceArenaUsed;
        RtlpTraceArenaUsed += Size;
        RtlZeroMemory(Ptr, Size);
    }

    RtlLeaveCriticalSection(&RtlpTraceArenaLock);
    return Ptr;
}

static
USHORT
RtlpLogCapturedStackTrace(
    _In_reads_(Depth) PVOID *Frames,
    _In_ USHORT Depth,
    _In_ ULONG Hash)
{
    PSTACK_TRACE_DATABASE DataBase = RtlpStackTraceDataBase;
    PRTL_STACK_TRACE_ENTRY Entry;
    ULONG Bucket;

    if (!Depth || !DataBase)
        return 0;

    Bucket = Hash % DataBase->NumberOfBuckets;

    /* Look for an existing entry */
    for (Entry = DataBase->Buckets[Bucket]; Entry; Entry = Entry->HashChain)
    {
        if ((Entry->Depth == Depth) &&
            RtlCompareMemory(Entry->BackTrace, Frames, Depth * sizeof(PVOID)) ==
            Depth * sizeof(PVOID))
        {
            Entry->TraceCount++;
            return Entry->Index;
        }
    }

    if (DataBase->NumberOfEntriesAdded >= 0xFFFF)
    {
        DataBase->NumberOfAllocationFailures++;
        return 0;
    }

    Entry = RtlpTraceAlloc(sizeof(*Entry));
    if (!Entry)
    {
        DataBase->NumberOfAllocationFailures++;
        return 0;
    }

    Entry->Depth = Depth;
    Entry->Index = (USHORT)(DataBase->NumberOfEntriesAdded + 1);
    Entry->TraceCount = 1;
    RtlCopyMemory(Entry->BackTrace, Frames, Depth * sizeof(PVOID));

    Entry->HashChain = DataBase->Buckets[Bucket];
    DataBase->Buckets[Bucket] = Entry;
    DataBase->EntryIndexArray[Entry->Index - 1] = Entry;
    DataBase->NumberOfEntriesAdded++;

    return Entry->Index;
}

/*
 * @implemented
 */
USHORT
NTAPI
RtlLogStackBackTrace(VOID)
{
    PVOID Frames[RTL_STACKTRACE_MAX_DEPTH];
    ULONG Hash = 0;
    USHORT Depth, Index = 0;
    PSTACK_TRACE_DATABASE DataBase;

    if (!NT_SUCCESS(RtlpInitializeStackTraceDataBase()))
        return 0;

    DataBase = RtlpStackTraceDataBase;

    /* Skip ourselves */
    Depth = RtlCaptureStackBackTrace(1, RTL_STACKTRACE_MAX_DEPTH, Frames, &Hash);
    if (!Depth)
        return 0;

    RtlEnterCriticalSection(RtlpTraceDatabaseLock(DataBase));
    Index = RtlpLogCapturedStackTrace(Frames, Depth, Hash);
    RtlLeaveCriticalSection(RtlpTraceDatabaseLock(DataBase));

    return Index;
}

#ifndef _BLDR_
/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlQueryProcessBackTraceInformation(IN OUT PRTL_DEBUG_INFORMATION Buffer)
{
    PSTACK_TRACE_DATABASE DataBase;
    PRTL_PROCESS_BACKTRACES BackTraces;
    SIZE_T Size;
    ULONG Entries, i, Written = 0;

    if (!Buffer)
        return STATUS_INVALID_PARAMETER;

    if (!NT_SUCCESS(RtlpInitializeStackTraceDataBase()))
        return STATUS_NO_MEMORY;

    DataBase = RtlpStackTraceDataBase;

    RtlEnterCriticalSection(RtlpTraceDatabaseLock(DataBase));

    Entries = DataBase->NumberOfEntriesAdded;
    Size = FIELD_OFFSET(RTL_PROCESS_BACKTRACES, BackTraces);
    Size += max(Entries, 1) * sizeof(RTL_PROCESS_BACKTRACE_INFORMATION);

    BackTraces = RtlpDebugBufferCommit(Buffer, Size);
    if (!BackTraces)
    {
        RtlLeaveCriticalSection(RtlpTraceDatabaseLock(DataBase));
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(BackTraces, Size);
    BackTraces->CommittedMemory = (ULONG)Buffer->CommitSize;
    BackTraces->ReservedMemory = (ULONG)Buffer->ViewSize;
    BackTraces->NumberOfBackTraceLookups = DataBase->NumberOfEntriesAdded;

    for (i = 0; i < Entries; i++)
    {
        PRTL_STACK_TRACE_ENTRY Entry = DataBase->EntryIndexArray[i];
        PRTL_PROCESS_BACKTRACE_INFORMATION Info;

        if (!Entry)
            continue;

        Info = &BackTraces->BackTraces[Written++];
        Info->SymbolicBackTrace = NULL;
        Info->TraceCount = Entry->TraceCount;
        Info->Index = Entry->Index;
        Info->Depth = Entry->Depth;
        RtlCopyMemory(Info->BackTrace, Entry->BackTrace, Entry->Depth * sizeof(PVOID));
    }

    BackTraces->NumberOfBackTraces = Written;
    Buffer->BackTraces = BackTraces;

    RtlLeaveCriticalSection(RtlpTraceDatabaseLock(DataBase));
    return STATUS_SUCCESS;
}
#endif /* !_BLDR_ */

/* FUNCTIONS ******************************************************************/

PRTL_UNLOAD_EVENT_TRACE
NTAPI
RtlGetUnloadEventTrace(VOID)
{
    /* Just return a pointer to an array, according to MSDN */
    return RtlpUnloadEventTrace;
}

VOID
NTAPI
LdrpRecordUnloadEvent(_In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PIMAGE_NT_HEADERS NtHeaders;
    UINT Sequence = RtlpUnloadEventTraceIndex++;
    UINT Index = Sequence % RTL_UNLOAD_EVENT_TRACE_NUMBER;
    USHORT StringLen;

    DPRINT("LdrpRecordUnloadEvent(%wZ, %p - %p)\n", &LdrEntry->BaseDllName, LdrEntry->DllBase,
        (ULONG_PTR)LdrEntry->DllBase + LdrEntry->SizeOfImage);

    RtlpUnloadEventTrace[Index].BaseAddress = LdrEntry->DllBase;
    RtlpUnloadEventTrace[Index].SizeOfImage = LdrEntry->SizeOfImage;
    RtlpUnloadEventTrace[Index].Sequence = Sequence;

    NtHeaders = RtlImageNtHeader(LdrEntry->DllBase);

    if (NtHeaders)
    {
        RtlpUnloadEventTrace[Index].TimeDateStamp = NtHeaders->FileHeader.TimeDateStamp;
        RtlpUnloadEventTrace[Index].CheckSum = NtHeaders->OptionalHeader.CheckSum;
    }
    else
    {
        RtlpUnloadEventTrace[Index].TimeDateStamp = 0;
        RtlpUnloadEventTrace[Index].CheckSum = 0;
    }

    StringLen = min(LdrEntry->BaseDllName.Length / sizeof(WCHAR), RTL_NUMBER_OF(RtlpUnloadEventTrace[Index].ImageName));
    RtlCopyMemory(RtlpUnloadEventTrace[Index].ImageName, LdrEntry->BaseDllName.Buffer, StringLen * sizeof(WCHAR));
    if (StringLen < RTL_NUMBER_OF(RtlpUnloadEventTrace[Index].ImageName))
        RtlpUnloadEventTrace[Index].ImageName[StringLen] = 0;
}

BOOLEAN
NTAPI
RtlTraceDatabaseAdd(IN PRTL_TRACE_DATABASE Database,
                    IN ULONG Count,
                    IN PVOID *Trace,
                    OUT OPTIONAL PRTL_TRACE_BLOCK *TraceBlock)
{
    ULONG Hash, Bucket;
    PRTL_TRACE_BLOCK Block, *Prev, Current;

    if (!Database || !Trace || !Count || Count > RTL_STACKTRACE_MAX_DEPTH)
        return FALSE;

    if (Database->Magic != 'RTDB')
        return FALSE;

    /* TODO: enforce MaximumSize/quota and grow segments instead of using process heap */
    Hash = Database->HashFunction ? Database->HashFunction(Count, Trace) : RtlpDefaultTraceHash(Count, Trace);
    Bucket = Hash % Database->NoOfBuckets;

    Prev = &Database->Buckets[Bucket];
    for (Current = *Prev; Current; Prev = &Current->Next, Current = Current->Next)
    {
        if ((Current->Count == Count) &&
            (RtlCompareMemory(Current->Trace, Trace, Count * sizeof(PVOID)) == Count * sizeof(PVOID)))
        {
            Current->UserCount++;
            Current->UserSize += sizeof(PVOID) * Count;
            Database->NoOfHits++;
            if (TraceBlock) *TraceBlock = Current;
            return TRUE;
        }
    }

    Block = (PRTL_TRACE_BLOCK)RtlpAllocateMemory(sizeof(RTL_TRACE_BLOCK) + Count * sizeof(PVOID),
                                                 TAG_STACKTRACE);
    if (!Block)
        return FALSE;

    RtlZeroMemory(Block, sizeof(RTL_TRACE_BLOCK));
    Block->Magic = 'RTBL';
    Block->Count = Count;
    Block->Size = Count * sizeof(PVOID);
    Block->UserCount = 1;
    Block->UserSize = Block->Size;
    Block->Trace = (PVOID *)(Block + 1);
    RtlCopyMemory(Block->Trace, Trace, Block->Size);

    Block->Next = *Prev;
    *Prev = Block;
    Database->CurrentSize += sizeof(RTL_TRACE_BLOCK) + Block->Size;
    Database->NoOfTraces++;

    if (TraceBlock) *TraceBlock = Block;
    return TRUE;
}

PRTL_TRACE_DATABASE
NTAPI
RtlTraceDatabaseCreate(IN ULONG Buckets,
                       IN OPTIONAL SIZE_T MaximumSize,
                       IN ULONG Flags,
                       IN ULONG Tag,
                       IN OPTIONAL RTL_TRACE_HASH_FUNCTION HashFunction)
{
    PRTL_TRACE_DATABASE Db = &RtlpUserTraceDb;

    UNREFERENCED_PARAMETER(Tag);

    if (!Buckets) Buckets = RTL_STACKTRACE_BUCKETS;
    if (Buckets > RTL_STACKTRACE_BUCKETS) Buckets = RTL_STACKTRACE_BUCKETS;

    RtlZeroMemory(Db, sizeof(*Db));
    Db->Magic = 'RTDB';
    Db->Flags = Flags;
    Db->MaximumSize = MaximumSize ? MaximumSize : 0x100000;
    Db->Owner = NULL;
    Db->NoOfBuckets = Buckets;
    Db->Buckets = RtlpUserTraceBuckets;
    Db->HashFunction = HashFunction;
    Db->SegmentList = &RtlpUserTraceSegment;
    Db->CurrentSize = sizeof(RtlpUserTraceSegment);
    Db->SegmentList->Magic = 'RTSG';
    Db->SegmentList->Database = Db;
    Db->SegmentList->NextSegment = NULL;
    Db->SegmentList->SegmentStart = (PCHAR)Db;
    Db->SegmentList->SegmentEnd = (PCHAR)Db + Db->CurrentSize;
    Db->SegmentList->SegmentFree = Db->SegmentList->SegmentStart + sizeof(RTL_TRACE_DATABASE);
    RtlZeroMemory(Db->Buckets, sizeof(Db->Buckets[0]) * Db->NoOfBuckets);
    /* TODO: support multiple segments, caller-supplied Tag, and per-owner allocations */

#ifdef NTOS_MODE_USER
    RtlInitializeCriticalSection(&Db->Lock);
#else
    KeInitializeSpinLock(&Db->u.SpinLock);
#endif

    return Db;
}

BOOLEAN
NTAPI
RtlTraceDatabaseDestroy(IN PRTL_TRACE_DATABASE Database)
{
    PRTL_TRACE_BLOCK Block, Next;
    ULONG i;

    if (!Database || Database->Magic != 'RTDB')
        return FALSE;

    for (i = 0; i < Database->NoOfBuckets; i++)
    {
        Block = Database->Buckets[i];
        while (Block)
        {
            Next = Block->Next;
            RtlpFreeMemory(Block, TAG_STACKTRACE);
            Block = Next;
        }
        Database->Buckets[i] = NULL;
    }

    Database->Magic = 0;
#ifdef NTOS_MODE_USER
    RtlDeleteCriticalSection(&Database->Lock);
#endif
    return TRUE;
}

BOOLEAN
NTAPI
RtlTraceDatabaseEnumerate(IN PRTL_TRACE_DATABASE Database,
                          IN PRTL_TRACE_ENUMERATE TraceEnumerate,
                          IN OUT PRTL_TRACE_BLOCK *TraceBlock)
{
    ULONG Index;
    PRTL_TRACE_BLOCK Block;

    if (!Database || Database->Magic != 'RTDB' || !TraceEnumerate || !TraceBlock)
        return FALSE;

    if (TraceEnumerate->Database != Database)
        return FALSE;

    Index = TraceEnumerate->Index;
    Block = TraceEnumerate->Block;
    if (Block)
    {
        Block = Block->Next;
    }
    else if (Index < Database->NoOfBuckets)
    {
        Block = Database->Buckets[Index];
    }

    while (Index < Database->NoOfBuckets)
    {
        if (Block)
        {
            TraceEnumerate->Index = Index;
            TraceEnumerate->Block = Block;
            *TraceBlock = Block;
            return TRUE;
        }

        Index++;
        if (Index < Database->NoOfBuckets)
            Block = Database->Buckets[Index];
        else
            Block = NULL;
    }

    TraceEnumerate->Index = Index;
    TraceEnumerate->Block = NULL;
    return FALSE;
}


BOOLEAN
NTAPI
RtlTraceDatabaseFind(IN PRTL_TRACE_DATABASE Database,
                     IN ULONG Count,
                     IN PVOID *Trace,
                     OUT OPTIONAL PRTL_TRACE_BLOCK *TraceBlock)
{
    ULONG Hash, Bucket;
    PRTL_TRACE_BLOCK Block;

    if (!Database || Database->Magic != 'RTDB' || !Trace || !Count)
        return FALSE;

    Hash = Database->HashFunction ? Database->HashFunction(Count, Trace) : RtlpDefaultTraceHash(Count, Trace);
    Bucket = Hash % Database->NoOfBuckets;

    for (Block = Database->Buckets[Bucket]; Block; Block = Block->Next)
    {
        if ((Block->Count == Count) &&
            (RtlCompareMemory(Block->Trace, Trace, Count * sizeof(PVOID)) == Count * sizeof(PVOID)))
        {
            if (TraceBlock) *TraceBlock = Block;
            return TRUE;
        }
    }

    return FALSE;
}

BOOLEAN
NTAPI
RtlTraceDatabaseLock(IN PRTL_TRACE_DATABASE Database)
{
    if (!Database || Database->Magic != 'RTDB')
        return FALSE;
#ifdef NTOS_MODE_USER
    RtlEnterCriticalSection(&Database->Lock);
#else
    KeAcquireSpinLock(&Database->u.SpinLock, &Database->u.FastMutex.OldIrql);
#endif
    return TRUE;
}

BOOLEAN
NTAPI
RtlTraceDatabaseUnlock(IN PRTL_TRACE_DATABASE Database)
{
    if (!Database || Database->Magic != 'RTDB')
        return FALSE;
#ifdef NTOS_MODE_USER
    RtlLeaveCriticalSection(&Database->Lock);
#else
    KeReleaseSpinLock(&Database->u.SpinLock, Database->u.FastMutex.OldIrql);
#endif
    return TRUE;
}

BOOLEAN
NTAPI
RtlTraceDatabaseValidate(IN PRTL_TRACE_DATABASE Database)
{
    if (!Database || Database->Magic != 'RTDB')
        return FALSE;
    return TRUE;
}
