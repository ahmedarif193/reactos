/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Concurrent interface statistics queries
 */

#include <apitest.h>
#include <iphlpapi.h>

#define QUERY_THREADS 8
#define QUERY_ROUNDS 100

typedef struct _QUERY_CONTEXT
{
    HANDLE Start;
    PMIB_IFTABLE Table;
    LONG Failures;
} QUERY_CONTEXT;

static DWORD WINAPI
QueryInterfaces(PVOID Parameter)
{
    QUERY_CONTEXT *Context = Parameter;
    MIB_IFROW Row;
    DWORD Round, Index;

    WaitForSingleObject(Context->Start, INFINITE);
    for (Round = 0; Round < QUERY_ROUNDS; ++Round)
    {
        for (Index = 0; Index < Context->Table->dwNumEntries; ++Index)
        {
            ZeroMemory(&Row, sizeof(Row));
            Row.dwIndex = Context->Table->table[Index].dwIndex;
            if (GetIfEntry(&Row) != NO_ERROR)
                InterlockedIncrement(&Context->Failures);
        }
    }
    return 0;
}

START_TEST(GetIfEntry)
{
    QUERY_CONTEXT *Context;
    HANDLE Threads[QUERY_THREADS];
    DWORD Size = 0, Error, Count = 0, Index, Wait;

    Error = GetIfTable(NULL, &Size, FALSE);
    if (Error != ERROR_INSUFFICIENT_BUFFER)
    {
        skip("Cannot enumerate interfaces: %lu\n", Error);
        return;
    }

    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (!Context)
        return;
    Context->Table = HeapAlloc(GetProcessHeap(), 0, Size);
    Context->Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Context->Table || !Context->Start)
        goto Cleanup;

    Error = GetIfTable(Context->Table, &Size, FALSE);
    ok(Error == NO_ERROR, "GetIfTable returned %lu\n", Error);
    if (Error != NO_ERROR || !Context->Table->dwNumEntries)
        goto Cleanup;

    /* Force queued legacy OIDs as well as synchronous requests. Every caller
     * needs its own completion, even when several query the same adapter. */
    for (Count = 0; Count < QUERY_THREADS; ++Count)
    {
        Threads[Count] = CreateThread(NULL, 0, QueryInterfaces, Context, 0, NULL);
        if (!Threads[Count])
            break;
    }
    ok(Count == QUERY_THREADS, "Created only %lu query threads\n", Count);
    SetEvent(Context->Start);
    if (Count)
    {
        Wait = WaitForMultipleObjects(Count, Threads, TRUE, 60000);
        ok(Wait == WAIT_OBJECT_0, "Concurrent GetIfEntry calls hung: %lu\n", Wait);
        for (Index = 0; Index < Count; ++Index)
            CloseHandle(Threads[Index]);
        /* A stuck driver may still reference the worker's data. Leave it alive
         * until process exit rather than terminating a thread in an IOCTL. */
        if (Wait != WAIT_OBJECT_0)
            return;
    }
    ok(Context->Failures == 0, "%ld interface queries failed\n", Context->Failures);
    trace("Completed %lu rounds on %lu threads across %lu interfaces\n",
          (DWORD)QUERY_ROUNDS, Count, Context->Table->dwNumEntries);

Cleanup:
    if (Context->Start)
        CloseHandle(Context->Start);
    HeapFree(GetProcessHeap(), 0, Context->Table);
    HeapFree(GetProcessHeap(), 0, Context);
}
