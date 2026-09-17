/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Foreign-process driver for the Core Audio session registry tests
 */

#include "sessionharness.h"
#include "sandboxaudio.h"

#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>
#include <stdlib.h>
#include <string.h>

#define CHILD_TIMEOUT_MILLISECONDS  60000
#define CHILD_MAX_SESSIONS          SESSION_REGISTRY_TEST_CAPACITY

static HANDLE ReadyEvent;
static HANDLE StopEvent;

static BOOL ProbeWdmaudInterface(void)
{
    static const GUID WdmaudCategory = {STATIC_KSCATEGORY_WDMAUD};
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *Detail = NULL;
    SP_DEVICE_INTERFACE_DATA Interface;
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices;
    DWORD Required = 0;
    BOOL Result = FALSE;

    Devices = SetupDiGetClassDevsW(&WdmaudCategory, NULL, NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Devices == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(&Interface, sizeof(Interface));
    Interface.cbSize = sizeof(Interface);
    ZeroMemory(&Device, sizeof(Device));
    Device.cbSize = sizeof(Device);
    if (!SetupDiEnumDeviceInterfaces(Devices, NULL, &WdmaudCategory, 0,
                                     &Interface))
        goto Cleanup;

    SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, NULL, 0,
                                     &Required, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        Required < sizeof(*Detail))
        goto Cleanup;

    Detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Required);
    if (!Detail)
        goto Cleanup;
    Detail->cbSize = sizeof(*Detail);
    Result = SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, Detail,
                                               Required, NULL, &Device);

Cleanup:
    if (Detail) HeapFree(GetProcessHeap(), 0, Detail);
    SetupDiDestroyDeviceInfoList(Devices);
    return Result;
}

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

static int ModeSandboxAudio(void)
{
    IMMDeviceEnumerator *Enumerator = NULL;
    IAudioRenderClient *Render = NULL;
    IAudioClient3 *Client = NULL;
    IMMDevice *Endpoint = NULL;
    WAVEFORMATEX *Format = NULL;
    AudioClientProperties Properties;
    UINT32 DefaultPeriod, FundamentalPeriod, MinimumPeriod, MaximumPeriod;
    UINT32 Frames;
    HANDLE Event = NULL;
    BYTE *Buffer;
    HRESULT hr;
    int Result = 0;

    if (!ProbeWdmaudInterface())
        return SANDBOX_AUDIO_DEVICE_INTERFACE;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&Enumerator);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_ENUMERATOR;
        goto Cleanup;
    }
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, eRender,
                                                       eConsole, &Endpoint);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_ENDPOINT;
        goto Cleanup;
    }
    hr = IMMDevice_Activate(Endpoint, &IID_IAudioClient3, CLSCTX_INPROC_SERVER,
                            NULL, (void **)&Client);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_ACTIVATE;
        goto Cleanup;
    }

    ZeroMemory(&Properties, sizeof(Properties));
    Properties.cbSize = sizeof(Properties);
    Properties.eCategory = AudioCategory_Media;
    Properties.Options = AUDCLNT_STREAMOPTIONS_NONE;
    hr = IAudioClient3_SetClientProperties(Client, &Properties);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_PROPERTIES;
        goto Cleanup;
    }
    hr = IAudioClient3_GetMixFormat(Client, &Format);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_FORMAT;
        goto Cleanup;
    }
    hr = IAudioClient3_GetSharedModeEnginePeriod(Client, Format, &DefaultPeriod,
                                                  &FundamentalPeriod,
                                                  &MinimumPeriod, &MaximumPeriod);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_PERIOD;
        goto Cleanup;
    }
    hr = IAudioClient3_InitializeSharedAudioStream(
        Client, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
        DefaultPeriod, Format, NULL);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_INITIALIZE;
        goto Cleanup;
    }

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!Event)
    {
        Result = SANDBOX_AUDIO_EVENT;
        goto Cleanup;
    }
    hr = IAudioClient3_SetEventHandle(Client, Event);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_SET_EVENT;
        goto Cleanup;
    }
    hr = IAudioClient3_GetService(Client, &IID_IAudioRenderClient,
                                  (void **)&Render);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_RENDER_SERVICE;
        goto Cleanup;
    }
    hr = IAudioClient3_GetBufferSize(Client, &Frames);
    if (FAILED(hr) || !Frames)
    {
        Result = SANDBOX_AUDIO_BUFFER_SIZE;
        goto Cleanup;
    }
    hr = IAudioRenderClient_GetBuffer(Render, Frames, &Buffer);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_GET_BUFFER;
        goto Cleanup;
    }
    hr = IAudioRenderClient_ReleaseBuffer(Render, Frames,
                                           AUDCLNT_BUFFERFLAGS_SILENT);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_RELEASE_BUFFER;
        goto Cleanup;
    }
    hr = IAudioClient3_Start(Client);
    if (FAILED(hr))
    {
        Result = SANDBOX_AUDIO_START;
        goto Cleanup;
    }

    SetEvent(ReadyEvent);
    WaitForSingleObject(StopEvent, CHILD_TIMEOUT_MILLISECONDS);
    hr = IAudioClient3_Stop(Client);
    if (FAILED(hr))
        Result = SANDBOX_AUDIO_STOP;

Cleanup:
    if (Render) IAudioRenderClient_Release(Render);
    if (Event) CloseHandle(Event);
    if (Format) CoTaskMemFree(Format);
    if (Client) IAudioClient3_Release(Client);
    if (Endpoint) IMMDevice_Release(Endpoint);
    if (Enumerator) IMMDeviceEnumerator_Release(Enumerator);
    return Result;
}

int
main(int argc, char **argv)
{
    IMMDevice *Endpoint;
    WCHAR EndpointId[MAX_PATH];
    DWORD ParentProcessId;
    UINT Count;
    int Result;

    if (argc == 3 && !strcmp(argv[1], "sandbox-audio"))
    {
        ParentProcessId = (DWORD)strtoul(argv[2], NULL, 10);
        if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
            return SANDBOX_AUDIO_COM;
        if (!OpenSyncEvents(ParentProcessId))
        {
            CoUninitialize();
            return SANDBOX_AUDIO_EVENTS;
        }
        Result = ModeSandboxAudio();
        CloseHandle(ReadyEvent);
        CloseHandle(StopEvent);
        CoUninitialize();
        return Result;
    }

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
