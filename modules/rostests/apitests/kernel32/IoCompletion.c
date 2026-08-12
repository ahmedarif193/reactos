/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Test I/O completion port dispatcher-object semantics
 */

#include "precomp.h"

typedef struct _DIRECT_WAIT_CONTEXT
{
    HANDLE Port;
    HANDLE Ready;
    DWORD Result;
    DWORD Timeout;
} DIRECT_WAIT_CONTEXT;

typedef struct _DEQUEUE_WAIT_CONTEXT
{
    HANDLE Port;
    HANDLE Ready;
    BOOL Success;
    DWORD Bytes;
    ULONG_PTR Key;
    LPOVERLAPPED Overlapped;
} DEQUEUE_WAIT_CONTEXT;

typedef struct _OWN_QUEUE_WAIT_CONTEXT
{
    HANDLE Event;
    HANDLE Port;
    HANDLE Ready;
    BOOL PrimeSuccess;
    BOOL RemoveSuccess;
    DWORD Result;
    DWORD PrimeBytes;
    DWORD RemoveBytes;
    ULONG_PTR PrimeKey;
    ULONG_PTR RemoveKey;
    LPOVERLAPPED PrimeOverlapped;
    LPOVERLAPPED RemoveOverlapped;
} OWN_QUEUE_WAIT_CONTEXT;

static DWORD WINAPI
DirectWaitThread(_Inout_ PVOID Parameter)
{
    DIRECT_WAIT_CONTEXT *Context = Parameter;

    SetEvent(Context->Ready);
    Context->Result = WaitForSingleObject(Context->Port, Context->Timeout);
    return 0;
}

static DWORD WINAPI
DequeueWaitThread(_Inout_ PVOID Parameter)
{
    DEQUEUE_WAIT_CONTEXT *Context = Parameter;

    SetEvent(Context->Ready);
    Context->Success = GetQueuedCompletionStatus(Context->Port, &Context->Bytes, &Context->Key, &Context->Overlapped, 5000);
    return 0;
}

static DWORD WINAPI
OwnQueueMultipleWaitThread(_Inout_ PVOID Parameter)
{
    OWN_QUEUE_WAIT_CONTEXT *Context = Parameter;
    HANDLE Handles[2];

    Context->PrimeSuccess = GetQueuedCompletionStatus(Context->Port, &Context->PrimeBytes, &Context->PrimeKey, &Context->PrimeOverlapped, 5000);
    SetEvent(Context->Ready);
    Handles[0] = Context->Event;
    Handles[1] = Context->Port;
    Context->Result = WaitForMultipleObjects(2, Handles, FALSE, 5000);
    Context->RemoveSuccess = GetQueuedCompletionStatus(Context->Port, &Context->RemoveBytes, &Context->RemoveKey, &Context->RemoveOverlapped, 0);
    return 0;
}

START_TEST(IoCompletion)
{
    DEQUEUE_WAIT_CONTEXT DequeueContext;
    DIRECT_WAIT_CONTEXT DirectContexts[2];
    OWN_QUEUE_WAIT_CONTEXT OwnContext;
    HANDLE ReadyHandles[2];
    HANDLE Threads[2];
    LPOVERLAPPED Overlapped;
    HANDLE Handles[2];
    HANDLE MixedPort;
    HANDLE OwnPort;
    ULONG_PTR Key;
    HANDLE Event;
    HANDLE Port;
    DWORD Bytes;
    DWORD Result;
    ULONG Index;
    BOOL Success;

    Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ok(Port != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    if (!Port)
        return;

    Result = WaitForSingleObject(Port, 0);
    ok(Result == WAIT_TIMEOUT, "An empty completion port returned %#lx instead of WAIT_TIMEOUT\n", Result);

    Success = PostQueuedCompletionStatus(Port, 7, 0x1234, (LPOVERLAPPED)(ULONG_PTR)0x5678);
    ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
    Result = WaitForSingleObject(Port, 0);
    ok(Result == WAIT_OBJECT_0, "A queued completion port returned %#lx instead of WAIT_OBJECT_0\n", Result);

    Bytes = 0;
    Key = 0;
    Overlapped = NULL;
    Success = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Overlapped, 0);
    ok(Success, "GetQueuedCompletionStatus failed with %lu\n", GetLastError());
    ok(Bytes == 7, "Expected 7 transferred bytes, got %lu\n", Bytes);
    ok(Key == 0x1234, "Expected completion key 0x1234, got %p\n", (PVOID)Key);
    ok(Overlapped == (LPOVERLAPPED)(ULONG_PTR)0x5678, "Expected OVERLAPPED 0x5678, got %p\n", Overlapped);
    Result = WaitForSingleObject(Port, 0);
    ok(Result == WAIT_TIMEOUT, "A drained completion port returned %#lx instead of WAIT_TIMEOUT\n", Result);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (Event)
    {
        Handles[0] = Event;
        Handles[1] = Port;
        Result = WaitForMultipleObjects(2, Handles, FALSE, 0);
        ok(Result == WAIT_TIMEOUT, "An empty completion port in a multiple wait returned %#lx instead of WAIT_TIMEOUT\n", Result);
        Success = PostQueuedCompletionStatus(Port, 11, 0x4321, (LPOVERLAPPED)(ULONG_PTR)0x8765);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForMultipleObjects(2, Handles, FALSE, 0);
        ok(Result == WAIT_OBJECT_0 + 1, "A queued completion port in a multiple wait returned %#lx instead of %#x\n", Result, WAIT_OBJECT_0 + 1);
        Success = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Overlapped, 0);
        ok(Success, "GetQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForMultipleObjects(2, Handles, FALSE, 0);
        ok(Result == WAIT_TIMEOUT, "A drained completion port in a multiple wait returned %#lx instead of WAIT_TIMEOUT\n", Result);
        CloseHandle(Event);
    }

    for (Index = 0; Index != 2; ++Index)
    {
        DirectContexts[Index].Port = Port;
        DirectContexts[Index].Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
        DirectContexts[Index].Result = WAIT_FAILED;
        DirectContexts[Index].Timeout = 5000;
        ReadyHandles[Index] = DirectContexts[Index].Ready;
        Threads[Index] = CreateThread(NULL, 0, DirectWaitThread, &DirectContexts[Index], 0, NULL);
        ok(ReadyHandles[Index] != NULL, "CreateEventW failed with %lu\n", GetLastError());
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }
    if (ReadyHandles[0] && ReadyHandles[1] && Threads[0] && Threads[1])
    {
        Result = WaitForMultipleObjects(2, ReadyHandles, TRUE, 5000);
        ok(Result == WAIT_OBJECT_0, "Direct waiters did not become ready: %#lx\n", Result);
        Sleep(100);
        Success = PostQueuedCompletionStatus(Port, 21, 0x2001, (LPOVERLAPPED)(ULONG_PTR)0x2002);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForMultipleObjects(2, Threads, TRUE, 5000);
        ok(Result == WAIT_OBJECT_0, "Direct waiters did not exit: %#lx\n", Result);
        ok(DirectContexts[0].Result == WAIT_OBJECT_0, "First direct waiter returned %#lx\n", DirectContexts[0].Result);
        ok(DirectContexts[1].Result == WAIT_OBJECT_0, "Second direct waiter returned %#lx\n", DirectContexts[1].Result);
        Success = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Overlapped, 0);
        ok(Success, "GetQueuedCompletionStatus failed with %lu\n", GetLastError());
        ok(Key == 0x2001, "Expected completion key 0x2001, got %p\n", (PVOID)Key);
    }
    for (Index = 0; Index != 2; ++Index)
    {
        if (Threads[Index]) CloseHandle(Threads[Index]);
        if (ReadyHandles[Index]) CloseHandle(ReadyHandles[Index]);
    }

    MixedPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    ok(MixedPort != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    DequeueContext.Port = MixedPort;
    DequeueContext.Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    DequeueContext.Success = FALSE;
    DequeueContext.Bytes = 0;
    DequeueContext.Key = 0;
    DequeueContext.Overlapped = NULL;
    Threads[0] = CreateThread(NULL, 0, DequeueWaitThread, &DequeueContext, 0, NULL);
    DirectContexts[0].Port = MixedPort;
    DirectContexts[0].Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    DirectContexts[0].Result = WAIT_FAILED;
    DirectContexts[0].Timeout = 250;
    Threads[1] = NULL;
    if (MixedPort && DequeueContext.Ready && Threads[0])
    {
        Result = WaitForSingleObject(DequeueContext.Ready, 5000);
        ok(Result == WAIT_OBJECT_0, "Dequeue waiter did not become ready: %#lx\n", Result);
        Sleep(100);
        Threads[1] = CreateThread(NULL, 0, DirectWaitThread, &DirectContexts[0], 0, NULL);
    }
    ok(DequeueContext.Ready != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(DirectContexts[0].Ready != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(Threads[0] != NULL, "CreateThread failed with %lu\n", GetLastError());
    ok(Threads[1] != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (DirectContexts[0].Ready && Threads[1])
    {
        Result = WaitForSingleObject(DirectContexts[0].Ready, 5000);
        ok(Result == WAIT_OBJECT_0, "Direct waiter did not become ready: %#lx\n", Result);
        Sleep(100);
        Success = PostQueuedCompletionStatus(MixedPort, 31, 0x3001, (LPOVERLAPPED)(ULONG_PTR)0x3002);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForMultipleObjects(2, Threads, TRUE, 5000);
        ok(Result == WAIT_OBJECT_0, "Mixed waiters did not exit: %#lx\n", Result);
        ok(DequeueContext.Success, "GetQueuedCompletionStatus failed in dequeue waiter\n");
        ok(DequeueContext.Key == 0x3001, "Expected completion key 0x3001, got %p\n", (PVOID)DequeueContext.Key);
        ok(DirectContexts[0].Result == WAIT_TIMEOUT, "Direct waiter returned %#lx instead of WAIT_TIMEOUT\n", DirectContexts[0].Result);
    }
    if (Threads[0]) CloseHandle(Threads[0]);
    if (Threads[1]) CloseHandle(Threads[1]);
    if (DequeueContext.Ready) CloseHandle(DequeueContext.Ready);
    if (DirectContexts[0].Ready) CloseHandle(DirectContexts[0].Ready);
    if (MixedPort) CloseHandle(MixedPort);

    OwnPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    OwnContext.Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    OwnContext.Event = Event;
    OwnContext.Port = OwnPort;
    OwnContext.PrimeSuccess = FALSE;
    OwnContext.RemoveSuccess = FALSE;
    OwnContext.Result = WAIT_FAILED;
    OwnContext.PrimeBytes = 0;
    OwnContext.RemoveBytes = 0;
    OwnContext.PrimeKey = 0;
    OwnContext.RemoveKey = 0;
    OwnContext.PrimeOverlapped = NULL;
    OwnContext.RemoveOverlapped = NULL;
    Threads[0] = NULL;
    ok(OwnPort != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    ok(OwnContext.Ready != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (OwnPort && Event && OwnContext.Ready)
    {
        Success = PostQueuedCompletionStatus(OwnPort, 41, 0x4001, (LPOVERLAPPED)(ULONG_PTR)0x4002);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Threads[0] = CreateThread(NULL, 0, OwnQueueMultipleWaitThread, &OwnContext, 0, NULL);
    }
    ok(Threads[0] != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Threads[0])
    {
        Result = WaitForSingleObject(OwnContext.Ready, 5000);
        ok(Result == WAIT_OBJECT_0, "Associated waiter did not become ready: %#lx\n", Result);
        ok(OwnContext.PrimeSuccess, "GetQueuedCompletionStatus failed for priming packet\n");
        ok(OwnContext.PrimeKey == 0x4001, "Expected priming key 0x4001, got %p\n", (PVOID)OwnContext.PrimeKey);
        Sleep(100);
        Success = PostQueuedCompletionStatus(OwnPort, 42, 0x4003, (LPOVERLAPPED)(ULONG_PTR)0x4004);
        ok(Success, "PostQueuedCompletionStatus failed with %lu\n", GetLastError());
        Result = WaitForSingleObject(Threads[0], 5000);
        ok(Result == WAIT_OBJECT_0, "Associated waiter did not exit: %#lx\n", Result);
        ok(OwnContext.Result == WAIT_OBJECT_0 + 1, "Associated multiple wait returned %#lx\n", OwnContext.Result);
        ok(OwnContext.RemoveSuccess, "GetQueuedCompletionStatus failed for observed packet\n");
        ok(OwnContext.RemoveKey == 0x4003, "Expected completion key 0x4003, got %p\n", (PVOID)OwnContext.RemoveKey);
    }
    if (Threads[0]) CloseHandle(Threads[0]);
    if (OwnContext.Ready) CloseHandle(OwnContext.Ready);
    if (Event) CloseHandle(Event);
    if (OwnPort) CloseHandle(OwnPort);

    for (Index = 0; Index != 256; ++Index)
    {
        Result = WaitForSingleObject(Port, 0);
        if (Result != WAIT_TIMEOUT)
            break;
        Success = PostQueuedCompletionStatus(Port, Index, Index, (LPOVERLAPPED)(ULONG_PTR)(Index + 1));
        if (!Success)
            break;
        Result = WaitForSingleObject(Port, 0);
        if (Result != WAIT_OBJECT_0)
            break;
        Success = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Overlapped, 0);
        if (!Success || Bytes != Index || Key != Index || Overlapped != (LPOVERLAPPED)(ULONG_PTR)(Index + 1))
            break;
    }
    ok(Index == 256, "Completion port state diverged at stress iteration %lu\n", Index);

    CloseHandle(Port);
}
