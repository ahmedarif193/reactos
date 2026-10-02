/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fiber local storage
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Based on Wine dlls/ntdll/thread.c, Copyright 1996, 2003 Alexandre Julliard
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

#define RTLP_FLS_MAXIMUM_INDEX 0xFF0
#define RTLP_FLS_NO_CALLBACK ((PFLS_CALLBACK_FUNCTION)~(ULONG_PTR)0)

typedef VOID (NTAPI *PRTLP_FLS_CALLBACK_DISPATCHER)(PFLS_CALLBACK_FUNCTION Callback, PVOID Data);

static PRTLP_FLS_CALLBACK_DISPATCHER volatile RtlpFlsCallbackDispatcher;

static GLOBAL_FLS_DATA RtlpFlsData =
{
    { NULL },
    { &RtlpFlsData.FlsListHead, &RtlpFlsData.FlsListHead }
};

static RTL_CRITICAL_SECTION RtlpFlsLock;
static RTL_CRITICAL_SECTION_DEBUG RtlpFlsLockDebug =
{
    0, 0, &RtlpFlsLock,
    { &RtlpFlsLockDebug.ProcessLocksList, &RtlpFlsLockDebug.ProcessLocksList },
    0, 0, { 0 }
};
static RTL_CRITICAL_SECTION RtlpFlsLock = { &RtlpFlsLockDebug, -1, 0, 0, 0, 0 };

VOID
NTAPI
RtlpSetFlsCallbackDispatcher(
    _In_opt_ PRTLP_FLS_CALLBACK_DISPATCHER Dispatcher)
{
    InterlockedExchangePointer((PVOID volatile *)&RtlpFlsCallbackDispatcher, (PVOID)Dispatcher);
}

VOID
NTAPI
RtlpCallFlsCallback(
    _In_ PFLS_CALLBACK_FUNCTION Callback,
    _In_opt_ PVOID Data)
{
    PRTLP_FLS_CALLBACK_DISPATCHER Dispatcher;

    Dispatcher = (PRTLP_FLS_CALLBACK_DISPATCHER)InterlockedCompareExchangePointer((PVOID volatile *)&RtlpFlsCallbackDispatcher, NULL, NULL);
    if (Dispatcher)
        Dispatcher(Callback, Data);
    else
        Callback(Data);
}

static
ULONG
RtlpFlsChunkSize(
    _In_ ULONG ChunkIndex)
{
    return 0x10 << ChunkIndex;
}

static
ULONG
RtlpFlsIndexFromChunkIndex(
    _In_ ULONG ChunkIndex,
    _In_ ULONG Index)
{
    return 0x10 * ((1 << ChunkIndex) - 1) + Index;
}

static
ULONG
RtlpFlsChunkIndexFromIndex(
    _In_ ULONG Index,
    _Out_ PULONG IndexInChunk)
{
    ULONG ChunkIndex = 0;

    while (Index >= RtlpFlsChunkSize(ChunkIndex))
        Index -= RtlpFlsChunkSize(ChunkIndex++);

    *IndexInChunk = Index;
    return ChunkIndex;
}

PTEB_FLS_DATA
NTAPI
RtlpAllocateFlsData(VOID)
{
    PTEB_FLS_DATA FlsData;

    FlsData = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*FlsData));
    if (!FlsData)
        return NULL;

    RtlEnterCriticalSection(&RtlpFlsLock);
    InsertTailList(&RtlpFlsData.FlsListHead, &FlsData->FlsListEntry);
    RtlLeaveCriticalSection(&RtlpFlsLock);

    return FlsData;
}

NTSTATUS
WINAPI
DECLSPEC_HOTPATCH
RtlFlsAlloc(
    _In_opt_ PFLS_CALLBACK_FUNCTION Callback,
    _Out_ PULONG Index)
{
    PFLS_INFO_CHUNK Chunk;
    ULONG ChunkIndex, IndexInChunk;
    PTEB Teb = NtCurrentTeb();

    if (!Teb->FlsData && !(Teb->FlsData = RtlpAllocateFlsData()))
        return STATUS_NO_MEMORY;

    RtlEnterCriticalSection(&RtlpFlsLock);

    for (ChunkIndex = 0; ChunkIndex < RTL_NUMBER_OF(RtlpFlsData.FlsCallbackChunks); ChunkIndex++)
    {
        Chunk = RtlpFlsData.FlsCallbackChunks[ChunkIndex];
        if (!Chunk || Chunk->Count < RtlpFlsChunkSize(ChunkIndex))
            break;
    }

    if (ChunkIndex == RTL_NUMBER_OF(RtlpFlsData.FlsCallbackChunks))
    {
        RtlLeaveCriticalSection(&RtlpFlsLock);
        return STATUS_NO_MEMORY;
    }

    Chunk = RtlpFlsData.FlsCallbackChunks[ChunkIndex];
    if (Chunk)
    {
        for (IndexInChunk = 0; IndexInChunk < RtlpFlsChunkSize(ChunkIndex); IndexInChunk++)
        {
            if (!Chunk->Callbacks[IndexInChunk].Callback)
                break;
        }

        ASSERT(IndexInChunk < RtlpFlsChunkSize(ChunkIndex));
    }
    else
    {
        Chunk = RtlAllocateHeap(RtlGetProcessHeap(),
                                HEAP_ZERO_MEMORY,
                                FIELD_OFFSET(FLS_INFO_CHUNK, Callbacks) +
                                sizeof(Chunk->Callbacks[0]) * RtlpFlsChunkSize(ChunkIndex));
        if (!Chunk)
        {
            RtlLeaveCriticalSection(&RtlpFlsLock);
            return STATUS_NO_MEMORY;
        }

        RtlpFlsData.FlsCallbackChunks[ChunkIndex] = Chunk;
        if (ChunkIndex)
        {
            IndexInChunk = 0;
        }
        else
        {
            Chunk->Count = 1;
            Chunk->Callbacks[0].Callback = RTLP_FLS_NO_CALLBACK;
            IndexInChunk = 1;
        }
    }

    Chunk->Count++;
    Chunk->Callbacks[IndexInChunk].Callback = Callback ? Callback : RTLP_FLS_NO_CALLBACK;

    *Index = RtlpFlsIndexFromChunkIndex(ChunkIndex, IndexInChunk);
    if (*Index > RtlpFlsData.FlsHighIndex)
        RtlpFlsData.FlsHighIndex = *Index;

    RtlLeaveCriticalSection(&RtlpFlsLock);
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
DECLSPEC_HOTPATCH
RtlFlsFree(
    _In_ ULONG Index)
{
    PFLS_CALLBACK_FUNCTION Callback;
    ULONG ChunkIndex, IndexInChunk;
    PFLS_INFO_CHUNK Chunk;
    PTEB_FLS_DATA FlsData;
    PLIST_ENTRY Entry;

    RtlEnterCriticalSection(&RtlpFlsLock);

    if (!Index || Index > RtlpFlsData.FlsHighIndex)
    {
        RtlLeaveCriticalSection(&RtlpFlsLock);
        return STATUS_INVALID_PARAMETER;
    }

    ChunkIndex = RtlpFlsChunkIndexFromIndex(Index, &IndexInChunk);
    Chunk = RtlpFlsData.FlsCallbackChunks[ChunkIndex];
    if (!Chunk || !(Callback = Chunk->Callbacks[IndexInChunk].Callback))
    {
        RtlLeaveCriticalSection(&RtlpFlsLock);
        return STATUS_INVALID_PARAMETER;
    }

    for (Entry = RtlpFlsData.FlsListHead.Flink; Entry != &RtlpFlsData.FlsListHead; Entry = Entry->Flink)
    {
        FlsData = CONTAINING_RECORD(Entry, TEB_FLS_DATA, FlsListEntry);

        if (FlsData->FlsDataChunks[ChunkIndex] && FlsData->FlsDataChunks[ChunkIndex][IndexInChunk + 1])
        {
            if (Callback != RTLP_FLS_NO_CALLBACK)
                RtlpCallFlsCallback(Callback, FlsData->FlsDataChunks[ChunkIndex][IndexInChunk + 1]);

            FlsData->FlsDataChunks[ChunkIndex][IndexInChunk + 1] = NULL;
        }
    }

    Chunk->Count--;
    Chunk->Callbacks[IndexInChunk].Callback = NULL;

    RtlLeaveCriticalSection(&RtlpFlsLock);
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
DECLSPEC_HOTPATCH
RtlFlsSetValue(
    _In_ ULONG Index,
    _In_opt_ PVOID Data)
{
    ULONG ChunkIndex, IndexInChunk;
    PTEB Teb = NtCurrentTeb();
    PTEB_FLS_DATA FlsData;

    if (!Index || Index >= RTLP_FLS_MAXIMUM_INDEX)
        return STATUS_INVALID_PARAMETER;

    FlsData = Teb->FlsData;
    if (!FlsData)
    {
        FlsData = RtlpAllocateFlsData();
        if (!FlsData)
            return STATUS_NO_MEMORY;

        Teb->FlsData = FlsData;
    }

    ChunkIndex = RtlpFlsChunkIndexFromIndex(Index, &IndexInChunk);
    if (!FlsData->FlsDataChunks[ChunkIndex])
    {
        FlsData->FlsDataChunks[ChunkIndex] = RtlAllocateHeap(RtlGetProcessHeap(),
                                                           HEAP_ZERO_MEMORY,
                                                           (RtlpFlsChunkSize(ChunkIndex) + 1) * sizeof(PVOID));
        if (!FlsData->FlsDataChunks[ChunkIndex])
            return STATUS_NO_MEMORY;
    }

    FlsData->FlsDataChunks[ChunkIndex][IndexInChunk + 1] = Data;
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
DECLSPEC_HOTPATCH
RtlFlsGetValue(
    _In_ ULONG Index,
    _Out_ PVOID *Data)
{
    ULONG ChunkIndex, IndexInChunk;
    PTEB_FLS_DATA FlsData;

    if (!Index || Index >= RTLP_FLS_MAXIMUM_INDEX || !(FlsData = NtCurrentTeb()->FlsData))
        return STATUS_INVALID_PARAMETER;

    ChunkIndex = RtlpFlsChunkIndexFromIndex(Index, &IndexInChunk);
    *Data = FlsData->FlsDataChunks[ChunkIndex] ? FlsData->FlsDataChunks[ChunkIndex][IndexInChunk + 1] : NULL;
    return STATUS_SUCCESS;
}

VOID
WINAPI
DECLSPEC_HOTPATCH
RtlProcessFlsData(
    _In_opt_ PVOID FlsDataPointer,
    _In_ ULONG Flags)
{
    PTEB_FLS_DATA FlsData = FlsDataPointer;
    PFLS_CALLBACK_FUNCTION Callback;
    ULONG ChunkIndex, IndexInChunk;
    PFLS_INFO_CHUNK Chunk;

    if (!FlsData)
        return;

    if (Flags & 1)
    {
        RtlEnterCriticalSection(&RtlpFlsLock);

        for (ChunkIndex = 0; ChunkIndex < RTL_NUMBER_OF(FlsData->FlsDataChunks); ChunkIndex++)
        {
            Chunk = RtlpFlsData.FlsCallbackChunks[ChunkIndex];
            if (!FlsData->FlsDataChunks[ChunkIndex] || !Chunk || !Chunk->Count)
                continue;

            for (IndexInChunk = 0; IndexInChunk < RtlpFlsChunkSize(ChunkIndex); IndexInChunk++)
            {
                Callback = Chunk->Callbacks[IndexInChunk].Callback;

                if (!FlsData->FlsDataChunks[ChunkIndex][IndexInChunk + 1])
                    continue;

                if (Callback && Callback != RTLP_FLS_NO_CALLBACK)
                    RtlpCallFlsCallback(Callback, FlsData->FlsDataChunks[ChunkIndex][IndexInChunk + 1]);

                FlsData->FlsDataChunks[ChunkIndex][IndexInChunk + 1] = NULL;
            }
        }

        FlsData->FlsListEntry.Flink->Blink = FlsData->FlsListEntry.Blink;
        FlsData->FlsListEntry.Blink->Flink = FlsData->FlsListEntry.Flink;

        RtlLeaveCriticalSection(&RtlpFlsLock);
    }

    if (Flags & 2)
    {
        for (ChunkIndex = 0; ChunkIndex < RTL_NUMBER_OF(FlsData->FlsDataChunks); ChunkIndex++)
        {
            if (FlsData->FlsDataChunks[ChunkIndex])
                RtlFreeHeap(RtlGetProcessHeap(), 0, FlsData->FlsDataChunks[ChunkIndex]);
        }

        RtlFreeHeap(RtlGetProcessHeap(), 0, FlsData);
    }
}
