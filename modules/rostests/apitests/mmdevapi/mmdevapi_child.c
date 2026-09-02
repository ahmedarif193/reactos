/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Foreign-process driver for the Core Audio session registry tests
 */

#include "sessionharness.h"

#include <stdlib.h>
#include <string.h>

#define CHILD_TIMEOUT_MILLISECONDS  60000
#define CHILD_MAX_SESSIONS          SESSION_REGISTRY_TEST_CAPACITY

static HANDLE ReadyEvent;
static HANDLE StopEvent;

static BOOL OpenSyncEvents(DWORD ParentProcessId)
{
    WCHAR Ready[MAX_PATH];
    WCHAR Stop[MAX_PATH];

    TestEventNames(ParentProcessId, Ready, Stop, MAX_PATH);
    ReadyEvent = CreateEventW(NULL, TRUE, FALSE, Ready);
    StopEvent = CreateEventW(NULL, TRUE, FALSE, Stop);
    return ReadyEvent != NULL && StopEvent != NULL;
}

static int ModeHold(IMMDevice *Endpoint, UINT Count, BOOL Wait)
{
    struct reactos_audio_session_snapshot Snapshot;
    struct reactos_audio_session_id Id;
    WCHAR Display[TEST_DISPLAY_NAME_CCH];
    WCHAR Icon[TEST_ICON_PATH_CCH];
    GUID Guid;
    UINT i;

    for (i = 0; i < Count; ++i)
    {
        if (FAILED(CoCreateGuid(&Guid)))
            return 2;
        if (FAILED(reactos_audio_session_register(Endpoint, &Guid, TEST_TUPLE_CHANNELS,
                                                  &Id, &Snapshot)))
            return 3;

        TestTupleString(i, TEST_DISPLAY_BASE, Display, ARRAYSIZE(Display));
        TestTupleString(i, TEST_ICON_BASE, Icon, ARRAYSIZE(Icon));
        if (FAILED(reactos_audio_session_set_strings(&Id, Display, Icon)))
            return 4;
        if (FAILED(reactos_audio_session_set_master(&Id, TestTupleVolume(i))))
            return 5;
        if (FAILED(reactos_audio_session_set_state(&Id, AudioSessionStateActive)))
            return 6;
    }

    SetEvent(ReadyEvent);
    if (Wait)
        WaitForSingleObject(StopEvent, CHILD_TIMEOUT_MILLISECONDS);
    return 0;
}

static int ModeHammer(IMMDevice *Endpoint)
{
    struct reactos_audio_session_snapshot Snapshot;
    struct reactos_audio_session_id Id;
    WCHAR Display[TEST_DISPLAY_NAME_CCH];
    WCHAR Icon[TEST_ICON_PATH_CCH];
    float Levels[TEST_TUPLE_CHANNELS];
    ULONGLONG Deadline;
    GUID Guid;
    UINT Index = 0;
    UINT i;

    if (FAILED(CoCreateGuid(&Guid)))
        return 2;
    if (FAILED(reactos_audio_session_register(Endpoint, &Guid, TEST_TUPLE_CHANNELS,
                                              &Id, &Snapshot)))
        return 3;

    TestTupleString(0, TEST_DISPLAY_BASE, Display, ARRAYSIZE(Display));
    TestTupleString(0, TEST_ICON_BASE, Icon, ARRAYSIZE(Icon));
    for (i = 0; i < ARRAYSIZE(Levels); ++i)
        Levels[i] = TestTupleChannel(0, i);

    if (FAILED(reactos_audio_session_set_strings(&Id, Display, Icon)) ||
        FAILED(reactos_audio_session_set_channels(&Id, ARRAYSIZE(Levels), Levels)) ||
        FAILED(reactos_audio_session_set_master(&Id, TestTupleVolume(0))) ||
        FAILED(reactos_audio_session_set_mute(&Id, FALSE)) ||
        FAILED(reactos_audio_session_set_state(&Id, AudioSessionStateInactive)))
        return 4;

    SetEvent(ReadyEvent);
    Deadline = GetTickCount64() + CHILD_TIMEOUT_MILLISECONDS;

    while (WaitForSingleObject(StopEvent, 0) != WAIT_OBJECT_0 &&
           GetTickCount64() < Deadline)
    {
        Index = (Index + 1) % TEST_TUPLE_COUNT;
        TestTupleString(Index, TEST_DISPLAY_BASE, Display, ARRAYSIZE(Display));
        TestTupleString(Index, TEST_ICON_BASE, Icon, ARRAYSIZE(Icon));
        for (i = 0; i < ARRAYSIZE(Levels); ++i)
            Levels[i] = TestTupleChannel(Index, i);

        reactos_audio_session_set_strings(&Id, Display, Icon);
        reactos_audio_session_set_channels(&Id, ARRAYSIZE(Levels), Levels);
        reactos_audio_session_set_master(&Id, TestTupleVolume(Index));
        reactos_audio_session_set_mute(&Id, (Index & 1) != 0);
        reactos_audio_session_set_state(&Id, (AudioSessionState)(Index % 3));
    }

    return 0;
}

static int ModeExhaust(IMMDevice *Endpoint)
{
    struct reactos_audio_session_snapshot Snapshot;
    struct reactos_audio_session_id Id;
    HRESULT hr = S_OK;
    GUID Guid;
    UINT Count = 0;

    while (Count <= CHILD_MAX_SESSIONS)
    {
        if (FAILED(CoCreateGuid(&Guid)))
            return 2;

        hr = reactos_audio_session_register(Endpoint, &Guid, 2, &Id, &Snapshot);
        if (FAILED(hr))
            break;
        ++Count;
    }

    SetEvent(ReadyEvent);

    if (hr != HRESULT_FROM_WIN32(ERROR_TOO_MANY_SESS))
        return (int)(0x00010000u | (Count & 0xffff));

    return (int)(Count & 0xffff);
}

int
main(int argc, char **argv)
{
    IMMDevice *Endpoint;
    WCHAR EndpointId[MAX_PATH];
    DWORD ParentProcessId;
    UINT Count;
    int Result;

    if (argc < 4)
        return 1;

    if (!MultiByteToWideChar(CP_ACP, 0, argv[2], -1, EndpointId, ARRAYSIZE(EndpointId)))
        return 1;

    ParentProcessId = (DWORD)strtoul(argv[3], NULL, 10);
    Count = argc > 4 ? (UINT)strtoul(argv[4], NULL, 10) : 1;
    if (Count == 0 || Count > CHILD_MAX_SESSIONS)
        Count = 1;

    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
        return 1;

    if (!OpenSyncEvents(ParentProcessId))
    {
        CoUninitialize();
        return 1;
    }

    Endpoint = TestEndpointCreate(EndpointId);
    if (!Endpoint)
    {
        CoUninitialize();
        return 1;
    }

    if (!strcmp(argv[1], "hold"))
        Result = ModeHold(Endpoint, Count, TRUE);
    else if (!strcmp(argv[1], "leak"))
        Result = ModeHold(Endpoint, Count, FALSE);
    else if (!strcmp(argv[1], "hammer"))
        Result = ModeHammer(Endpoint);
    else if (!strcmp(argv[1], "exhaust"))
        Result = ModeExhaust(Endpoint);
    else
        Result = 1;

    TestEndpointDestroy(Endpoint);
    CloseHandle(ReadyEvent);
    CloseHandle(StopEvent);
    CoUninitialize();
    return Result;
}
