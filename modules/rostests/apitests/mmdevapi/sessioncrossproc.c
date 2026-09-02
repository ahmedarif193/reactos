/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Cross-process torture tests for the Core Audio session registry
 */

#include <apitest.h>

#include "sessionharness.h"

#include <stdlib.h>
#include <strsafe.h>

#define CHILD_READY_MILLISECONDS    20000
#define CHILD_EXIT_MILLISECONDS     30000
#define CROSS_TORTURE_MILLISECONDS  2500
#define CROSS_TORTURE_READERS       3

static HANDLE ReadyEvent;
static HANDLE StopEvent;

typedef struct _CHILD
{
    HANDLE Process;
    DWORD ProcessId;
    IMMDevice *Endpoint;
    WCHAR EndpointId[MAX_PATH];
} CHILD;

static BOOL CreateSyncEvents(void)
{
    WCHAR Ready[MAX_PATH];
    WCHAR Stop[MAX_PATH];

    TestEventNames(GetCurrentProcessId(), Ready, Stop, MAX_PATH);
    ReadyEvent = CreateEventW(NULL, TRUE, FALSE, Ready);
    StopEvent = CreateEventW(NULL, TRUE, FALSE, Stop);
    return ReadyEvent != NULL && StopEvent != NULL;
}

static BOOL GetChildPath(WCHAR Path[MAX_PATH])
{
    WCHAR *Separator;
    DWORD Length = GetModuleFileNameW(NULL, Path, MAX_PATH);

    if (Length == 0 || Length >= MAX_PATH)
        return FALSE;

    Separator = wcsrchr(Path, L'\\');
    if (!Separator)
        return FALSE;

    return SUCCEEDED(StringCchCopyW(Separator + 1,
                                    MAX_PATH - (Separator + 1 - Path),
                                    L"mmdevapi_child.exe"));
}

static BOOL SpawnChild(CHILD *Child, const WCHAR *Mode, const WCHAR *Tag, UINT Count)
{
    PROCESS_INFORMATION ProcessInformation;
    STARTUPINFOW StartupInfo;
    WCHAR ChildPath[MAX_PATH];
    WCHAR CommandLine[MAX_PATH * 3];

    ZeroMemory(Child, sizeof(*Child));
    ResetEvent(ReadyEvent);
    ResetEvent(StopEvent);

    if (!GetChildPath(ChildPath))
        return FALSE;

    TestEndpointId(Child->EndpointId, ARRAYSIZE(Child->EndpointId), Tag,
                   GetCurrentProcessId());
    Child->Endpoint = TestEndpointCreate(Child->EndpointId);
    if (!Child->Endpoint)
        return FALSE;

    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                     L"\"%s\" %s %s %lu %u", ChildPath, Mode, Child->EndpointId,
                     GetCurrentProcessId(), Count);

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    ZeroMemory(&ProcessInformation, sizeof(ProcessInformation));

    if (!CreateProcessW(ChildPath, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL,
                        &StartupInfo, &ProcessInformation))
    {
        ok(0, "CreateProcessW(%ls) failed: %lu\n", Mode, GetLastError());
        TestEndpointDestroy(Child->Endpoint);
        Child->Endpoint = NULL;
        return FALSE;
    }

    CloseHandle(ProcessInformation.hThread);
    Child->Process = ProcessInformation.hProcess;
    Child->ProcessId = ProcessInformation.dwProcessId;
    return TRUE;
}

static void CloseChild(CHILD *Child)
{
    if (Child->Endpoint)
        TestEndpointDestroy(Child->Endpoint);
    if (Child->Process)
        CloseHandle(Child->Process);
    ZeroMemory(Child, sizeof(*Child));
}

static BOOL WaitChildReady(CHILD *Child)
{
    HANDLE Handles[2];
    DWORD Wait;

    Handles[0] = ReadyEvent;
    Handles[1] = Child->Process;
    Wait = WaitForMultipleObjects(2, Handles, FALSE, CHILD_READY_MILLISECONDS);
    ok(Wait == WAIT_OBJECT_0, "The child never signalled readiness: %lu\n", Wait);
    return Wait == WAIT_OBJECT_0;
}

static DWORD WaitChildExit(CHILD *Child)
{
    DWORD ExitCode = 0xffffffff;
    DWORD Wait = WaitForSingleObject(Child->Process, CHILD_EXIT_MILLISECONDS);

    ok_eq_ulong(Wait, (ULONG)WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateProcess(Child->Process, 0xdead);
        WaitForSingleObject(Child->Process, CHILD_EXIT_MILLISECONDS);
        return ExitCode;
    }

    GetExitCodeProcess(Child->Process, &ExitCode);
    return ExitCode;
}

static int CountForeignSessions(CHILD *Child, struct reactos_audio_session_id **Ids)
{
    struct reactos_audio_session_id *Found = NULL;
    int Count = 0;
    HRESULT hr;

    hr = reactos_audio_session_enumerate(Child->Endpoint, &Found, &Count);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return -1;

    if (Ids)
        *Ids = Found;
    else
        free(Found);

    return Count;
}

static void TestForeignVisibility(void)
{
    struct reactos_audio_session_id *Ids = NULL;
    struct reactos_audio_session_snapshot Snapshot;
    CHILD Child;
    ULONG Seen = 0;
    int Count;
    int i;

    if (!SpawnChild(&Child, L"hold", L"visible", 4))
        return;

    if (WaitChildReady(&Child))
    {
        Count = CountForeignSessions(&Child, &Ids);
        ok_eq_int(Count, 4);

        for (i = 0; i < Count && Ids; ++i)
        {
            UINT Index = 0;
            UINT IconIndex = 0;
            HRESULT hr = reactos_audio_session_read(&Ids[i], 0, &Snapshot);

            ok_hr(hr, S_OK);
            if (FAILED(hr))
                continue;

            ok_eq_ulong(Snapshot.process_id, Child.ProcessId);
            ok_eq_ulong(Snapshot.state, (ULONG)AudioSessionStateActive);
            ok_eq_uint(Snapshot.channel_count, TEST_TUPLE_CHANNELS);
            ok(TestTupleStringValid(Snapshot.display_name, TEST_DISPLAY_NAME_CCH,
                                    TEST_DISPLAY_BASE, &Index),
               "record %d has a torn display name\n", i);
            ok(TestTupleStringValid(Snapshot.icon_path, TEST_ICON_PATH_CCH,
                                    TEST_ICON_BASE, &IconIndex),
               "record %d has a torn icon path\n", i);
            ok_eq_uint(IconIndex, Index);
            ok(Snapshot.master_volume == TestTupleVolume(Index),
               "record %d volume %f does not match index %u\n", i,
               Snapshot.master_volume, Index);
            ok(wcsstr(Snapshot.process_path, L"mmdevapi_child.exe") != NULL,
               "record %d process path is %ls\n", i, Snapshot.process_path);
            Seen |= 1u << Index;
        }

        ok_eq_hex(Seen, 0xfu);
        free(Ids);
    }

    SetEvent(StopEvent);
    WaitChildExit(&Child);

    ok_eq_int(CountForeignSessions(&Child, NULL), 0);
    CloseChild(&Child);
}

static void TestStaleReclaim(void)
{
    CHILD Child;
    DWORD ExitCode;

    if (SpawnChild(&Child, L"hold", L"terminate", 3))
    {
        if (WaitChildReady(&Child))
            ok_eq_int(CountForeignSessions(&Child, NULL), 3);

        ok(TerminateProcess(Child.Process, 0xdead), "TerminateProcess failed: %lu\n",
           GetLastError());
        ok_eq_ulong(WaitForSingleObject(Child.Process, CHILD_EXIT_MILLISECONDS),
                    (ULONG)WAIT_OBJECT_0);
        ok_eq_int(CountForeignSessions(&Child, NULL), 0);
        CloseChild(&Child);
    }

    if (SpawnChild(&Child, L"leak", L"exit", 2))
    {
        ExitCode = WaitChildExit(&Child);
        ok_eq_ulong(ExitCode, 0);
        ok_eq_int(CountForeignSessions(&Child, NULL), 0);
        CloseChild(&Child);
    }
}

typedef struct _CROSS_CONTEXT
{
    struct reactos_audio_session_id Id;
    volatile LONG Stop;
    LONG Reads;
    LONG Retries;
    LONG Invalidated;
    LONG Unexpected;
    LONG Torn;
    LONG OddGeneration;
} CROSS_CONTEXT;

static DWORD WINAPI CrossRead(LPVOID Parameter)
{
    CROSS_CONTEXT *Context = Parameter;
    struct reactos_audio_session_snapshot Snapshot;
    LONG Reads = 0, Retries = 0, Invalidated = 0, Unexpected = 0;
    LONG Torn = 0, OddGeneration = 0;

    while (!Context->Stop)
    {
        HRESULT hr = reactos_audio_session_read(&Context->Id, 0, &Snapshot);
        UINT Index = 0, IconIndex = 0;

        if (hr == HRESULT_FROM_WIN32(ERROR_RETRY))
        {
            ++Retries;
            continue;
        }
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
        {
            ++Invalidated;
            continue;
        }
        if (hr != S_OK)
        {
            ++Unexpected;
            continue;
        }

        ++Reads;

        if ((Snapshot.generation & 1) != 0 || Snapshot.generation == 0)
            ++OddGeneration;

        if (!TestTupleStringValid(Snapshot.display_name, TEST_DISPLAY_NAME_CCH,
                                  TEST_DISPLAY_BASE, &Index) ||
            !TestTupleStringValid(Snapshot.icon_path, TEST_ICON_PATH_CCH,
                                  TEST_ICON_BASE, &IconIndex) ||
            !TestTupleVolumeValid(Snapshot.master_volume, NULL) ||
            Snapshot.channel_count != TEST_TUPLE_CHANNELS ||
            Snapshot.state > AudioSessionStateExpired)
        {
            ++Torn;
            continue;
        }

        if (!TestTupleChannelsValid(Snapshot.channel_volumes, Snapshot.channel_count, &Index))
            ++Torn;
    }

    InterlockedExchangeAdd(&Context->Reads, Reads);
    InterlockedExchangeAdd(&Context->Retries, Retries);
    InterlockedExchangeAdd(&Context->Invalidated, Invalidated);
    InterlockedExchangeAdd(&Context->Unexpected, Unexpected);
    InterlockedExchangeAdd(&Context->Torn, Torn);
    InterlockedExchangeAdd(&Context->OddGeneration, OddGeneration);
    return 0;
}

static void TestCrossProcessTorture(void)
{
    struct reactos_audio_session_id *Ids = NULL;
    HANDLE Threads[CROSS_TORTURE_READERS];
    CROSS_CONTEXT Context;
    CHILD Child;
    int Count;
    UINT i;

    if (!SpawnChild(&Child, L"hammer", L"hammer", 1))
        return;

    if (!WaitChildReady(&Child))
    {
        TerminateProcess(Child.Process, 0xdead);
        WaitForSingleObject(Child.Process, CHILD_EXIT_MILLISECONDS);
        CloseChild(&Child);
        return;
    }

    Count = CountForeignSessions(&Child, &Ids);
    ok_eq_int(Count, 1);
    if (Count != 1 || !Ids)
    {
        free(Ids);
        SetEvent(StopEvent);
        WaitChildExit(&Child);
        CloseChild(&Child);
        return;
    }

    ZeroMemory(&Context, sizeof(Context));
    Context.Id = Ids[0];
    free(Ids);

    for (i = 0; i < CROSS_TORTURE_READERS; ++i)
    {
        Threads[i] = CreateThread(NULL, 0, CrossRead, &Context, 0, NULL);
        ok(Threads[i] != NULL, "CreateThread %u failed: %lu\n", i, GetLastError());
    }

    Sleep(CROSS_TORTURE_MILLISECONDS);
    InterlockedExchange(&Context.Stop, TRUE);

    for (i = 0; i < CROSS_TORTURE_READERS; ++i)
    {
        if (!Threads[i])
            continue;
        ok_eq_ulong(WaitForSingleObject(Threads[i], CHILD_EXIT_MILLISECONDS),
                    (ULONG)WAIT_OBJECT_0);
        CloseHandle(Threads[i]);
    }

    trace("Cross-process torture: %ld reads, %ld retries\n",
          Context.Reads, Context.Retries);

    ok(Context.Reads > 0, "The readers never completed a snapshot\n");
    ok_eq_long(Context.Torn, 0);
    ok_eq_long(Context.OddGeneration, 0);
    ok_eq_long(Context.Invalidated, 0);
    ok_eq_long(Context.Unexpected, 0);

    SetEvent(StopEvent);
    WaitChildExit(&Child);
    ok_eq_int(CountForeignSessions(&Child, NULL), 0);
    CloseChild(&Child);
}

static void TestCapacityExhaustion(void)
{
    struct reactos_audio_session_snapshot Snapshot;
    struct reactos_audio_session_id Id;
    TEST_SHARED_REGISTRY *Registry;
    CHILD Child;
    DWORD ExitCode;
    GUID Guid;
    ULONG Occupied = 0;
    ULONG i;

    if (!SpawnChild(&Child, L"exhaust", L"exhaust", 1))
        return;

    ExitCode = WaitChildExit(&Child);

    ok((ExitCode & 0xffff0000u) == 0,
       "The child stopped registering with an unexpected error, exit code %#lx\n",
       ExitCode);
    ok((ExitCode & 0xffffu) > 0, "The child never registered a session\n");
    ok((ExitCode & 0xffffu) <= SESSION_REGISTRY_TEST_CAPACITY,
       "The child registered %lu sessions, capacity is %u\n",
       ExitCode & 0xffffu, (unsigned)SESSION_REGISTRY_TEST_CAPACITY);
    trace("The child filled %lu of %u records before exhaustion\n",
          ExitCode & 0xffffu, (unsigned)SESSION_REGISTRY_TEST_CAPACITY);

    ok_eq_int(CountForeignSessions(&Child, NULL), 0);

    CoCreateGuid(&Guid);
    ok_hr(reactos_audio_session_register(Child.Endpoint, &Guid, 2, &Id, &Snapshot), S_OK);

    Registry = TestRegistryMap();
    ok(Registry != NULL, "The session registry section is absent: %lu\n", GetLastError());
    if (Registry)
    {
        for (i = 0; i < SESSION_REGISTRY_TEST_CAPACITY; ++i)
        {
            if (Registry->Sessions[i].Occupied)
                ++Occupied;
        }
        ok(Occupied < SESSION_REGISTRY_TEST_CAPACITY,
           "The registry is still full after the child died: %lu records\n", Occupied);
        TestRegistryUnmap(Registry);
    }

    CloseChild(&Child);
}

START_TEST(sessioncrossproc)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    ok(SUCCEEDED(hr), "CoInitializeEx failed: 0x%08lx\n", hr);

    if (!CreateSyncEvents())
    {
        ok(0, "Unable to create the child synchronisation events: %lu\n", GetLastError());
        goto done;
    }

    TestForeignVisibility();
    TestStaleReclaim();
    TestCrossProcessTorture();
    TestCapacityExhaustion();

done:
    if (ReadyEvent)
        CloseHandle(ReadyEvent);
    if (StopEvent)
        CloseHandle(StopEvent);
    if (SUCCEEDED(hr))
        CoUninitialize();
}
