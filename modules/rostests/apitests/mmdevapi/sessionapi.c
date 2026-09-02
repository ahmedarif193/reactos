/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Public Core Audio session surface over the shared registry
 */

#include <apitest.h>

#include "sessionharness.h"


#include <strsafe.h>

#define API_TORTURE_MILLISECONDS    2500

static IMMDeviceEnumerator *DeviceEnumerator;
static IMMDevice *Endpoint;
static IAudioSessionManager2 *Manager;
static IMMDevice *ProxyEndpoint;
static IAudioSessionControl2 *Control;
static ISimpleAudioVolume *SimpleVolume;
static IChannelAudioVolume *ChannelVolume;
static struct reactos_audio_session_id SharedId;
static WCHAR Marker[64];

static BOOL OpenDefaultEndpoint(void)
{
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&DeviceEnumerator);
    if (FAILED(hr))
    {
        skip("MMDeviceEnumerator is unavailable: 0x%08lx\n", hr);
        return FALSE;
    }

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(DeviceEnumerator, eRender,
                                                     eMultimedia, &Endpoint);
    if (FAILED(hr))
    {
        skip("No default render endpoint: 0x%08lx\n", hr);
        return FALSE;
    }

    hr = IMMDevice_Activate(Endpoint, &IID_IAudioSessionManager2, CLSCTX_INPROC_SERVER,
                            NULL, (void **)&Manager);
    if (FAILED(hr))
    {
        skip("IAudioSessionManager2 is unavailable: 0x%08lx\n", hr);
        return FALSE;
    }

    return TRUE;
}

static BOOL PublishSharedSession(void)
{
    struct reactos_audio_session_snapshot Snapshot;
    WCHAR Icon[TEST_ICON_PATH_CCH];
    WCHAR *EndpointId = NULL;
    float Levels[TEST_TUPLE_CHANNELS];
    HRESULT hr;
    GUID Guid;
    UINT i;

    hr = IMMDevice_GetId(Endpoint, &EndpointId);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return FALSE;

    ProxyEndpoint = TestEndpointCreate(EndpointId);
    CoTaskMemFree(EndpointId);
    ok(ProxyEndpoint != NULL, "Unable to mirror the endpoint identifier\n");
    if (!ProxyEndpoint)
        return FALSE;

    StringCchPrintfW(Marker, ARRAYSIZE(Marker), L"mmdevapi_apitest.%lu",
                     GetCurrentProcessId());
    TestTupleString(0, TEST_ICON_BASE, Icon, ARRAYSIZE(Icon));
    for (i = 0; i < ARRAYSIZE(Levels); ++i)
        Levels[i] = TestTupleChannel(0, i);

    CoCreateGuid(&Guid);
    hr = reactos_audio_session_register(ProxyEndpoint, &Guid, TEST_TUPLE_CHANNELS,
                                        &SharedId, &Snapshot);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return FALSE;

    ok_hr(reactos_audio_session_set_strings(&SharedId, Marker, Icon), S_OK);
    ok_hr(reactos_audio_session_set_channels(&SharedId, ARRAYSIZE(Levels), Levels), S_OK);
    ok_hr(reactos_audio_session_set_master(&SharedId, TestTupleVolume(0)), S_OK);
    ok_hr(reactos_audio_session_set_mute(&SharedId, FALSE), S_OK);
    ok_hr(reactos_audio_session_set_state(&SharedId, AudioSessionStateActive), S_OK);
    return TRUE;
}

static BOOL FindPublishedSession(void)
{
    IAudioSessionEnumerator *SessionEnumerator = NULL;
    HRESULT hr;
    int Count = 0;
    int i;

    hr = IAudioSessionManager2_GetSessionEnumerator(Manager, &SessionEnumerator);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return FALSE;

    hr = IAudioSessionEnumerator_GetCount(SessionEnumerator, &Count);
    ok_hr(hr, S_OK);
    ok(Count > 0, "The session enumerator is empty\n");

    for (i = 0; i < Count && !Control; ++i)
    {
        IAudioSessionControl *Session = NULL;
        IAudioSessionControl2 *Session2 = NULL;
        WCHAR *Name = NULL;

        if (FAILED(IAudioSessionEnumerator_GetSession(SessionEnumerator, i, &Session)))
            continue;

        if (SUCCEEDED(IAudioSessionControl_QueryInterface(Session,
                                                          &IID_IAudioSessionControl2,
                                                          (void **)&Session2)))
        {
            if (SUCCEEDED(IAudioSessionControl2_GetDisplayName(Session2, &Name)) && Name)
            {
                if (!lstrcmpW(Name, Marker))
                {
                    Control = Session2;
                    IAudioSessionControl2_AddRef(Control);
                }
                CoTaskMemFree(Name);
            }
            IAudioSessionControl2_Release(Session2);
        }

        IAudioSessionControl_Release(Session);
    }

    IAudioSessionEnumerator_Release(SessionEnumerator);
    ok(Control != NULL, "mmdevapi did not surface the shared session\n");
    return Control != NULL;
}

static void TestProxyContract(void)
{
    struct reactos_audio_session_snapshot Snapshot;
    WCHAR Expected[TEST_ICON_PATH_CCH];
    WCHAR *Value = NULL;
    AudioSessionState State = AudioSessionStateExpired;
    DWORD ProcessId = 0;
    float Level = 0.0f;
    BOOL Mute = TRUE;
    HRESULT hr;

    hr = IAudioSessionControl2_GetProcessId(Control, &ProcessId);
    ok_hr(hr, S_OK);
    ok_eq_ulong(ProcessId, GetCurrentProcessId());

    hr = IAudioSessionControl2_GetState(Control, &State);
    ok_hr(hr, S_OK);
    ok_eq_ulong((ULONG)State, (ULONG)AudioSessionStateActive);

    TestTupleString(0, TEST_ICON_BASE, Expected, ARRAYSIZE(Expected));
    hr = IAudioSessionControl2_GetIconPath(Control, &Value);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr) && Value)
    {
        ok_eq_wstr(Value, Expected);
        CoTaskMemFree(Value);
    }

    hr = IAudioSessionControl2_SetDisplayName(Control, L"set-through-mmdevapi", NULL);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&SharedId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_wstr(Snapshot.display_name, L"set-through-mmdevapi");

    hr = IAudioSessionControl2_SetIconPath(Control, L"set-icon-through-mmdevapi", NULL);
    ok_hr(hr, S_OK);
    hr = reactos_audio_session_read(&SharedId, 0, &Snapshot);
    ok_hr(hr, S_OK);
    ok_eq_wstr(Snapshot.icon_path, L"set-icon-through-mmdevapi");

    hr = IAudioSessionControl2_QueryInterface(Control, &IID_ISimpleAudioVolume,
                                              (void **)&SimpleVolume);
    ok_hr(hr, S_OK);
    hr = IAudioSessionControl2_QueryInterface(Control, &IID_IChannelAudioVolume,
                                              (void **)&ChannelVolume);
    ok_hr(hr, S_OK);

    if (SimpleVolume)
    {
        hr = ISimpleAudioVolume_SetMasterVolume(SimpleVolume, -0.001f, NULL);
        ok_hr(hr, E_INVALIDARG);
        hr = ISimpleAudioVolume_SetMasterVolume(SimpleVolume, 1.001f, NULL);
        ok_hr(hr, E_INVALIDARG);

        hr = ISimpleAudioVolume_SetMasterVolume(SimpleVolume, 0.375f, NULL);
        ok_hr(hr, S_OK);
        hr = reactos_audio_session_read(&SharedId, 0, &Snapshot);
        ok_hr(hr, S_OK);
        ok(Snapshot.master_volume == 0.375f, "master volume %f, expected 0.375\n",
           Snapshot.master_volume);

        ok_hr(reactos_audio_session_set_master(&SharedId, 0.75f), S_OK);
        hr = ISimpleAudioVolume_GetMasterVolume(SimpleVolume, &Level);
        ok_hr(hr, S_OK);
        ok(Level == 0.75f, "master volume %f, expected 0.75\n", Level);

        ok_hr(reactos_audio_session_set_mute(&SharedId, TRUE), S_OK);
        hr = ISimpleAudioVolume_GetMute(SimpleVolume, &Mute);
        ok_hr(hr, S_OK);
        ok_eq_bool(Mute, TRUE);
        ok_hr(reactos_audio_session_set_mute(&SharedId, FALSE), S_OK);
    }

    if (ChannelVolume)
    {
        float Levels[REACTOS_AUDIO_SESSION_MAX_CHANNELS];
        UINT32 Channels = 0;
        UINT32 i;

        hr = IChannelAudioVolume_GetChannelCount(ChannelVolume, &Channels);
        ok_hr(hr, S_OK);
        ok_eq_uint(Channels, TEST_TUPLE_CHANNELS);

        if (Channels == TEST_TUPLE_CHANNELS)
        {
            hr = IChannelAudioVolume_SetChannelVolume(ChannelVolume, Channels, 0.5f, NULL);
            ok_hr(hr, E_INVALIDARG);
            hr = IChannelAudioVolume_SetChannelVolume(ChannelVolume, 0, 1.5f, NULL);
            ok_hr(hr, E_INVALIDARG);

            for (i = 0; i < ARRAYSIZE(Levels); ++i)
                Levels[i] = 0.0f;
            hr = IChannelAudioVolume_GetAllVolumes(ChannelVolume, Channels, Levels);
            ok_hr(hr, S_OK);
            for (i = 0; i < Channels; ++i)
                ok(Levels[i] == TestTupleChannel(0, i),
                   "channel %u is %f, expected %f\n", i, Levels[i],
                   TestTupleChannel(0, i));
        }
    }
}

typedef struct _API_CONTEXT
{
    volatile LONG Stop;
    volatile LONG Writes;
    LONG Reads;
    LONG Failures;
    LONG TornDisplayName;
    LONG TornIconPath;
    LONG TornMaster;
} API_CONTEXT;

static DWORD WINAPI ApiWrite(LPVOID Parameter)
{
    API_CONTEXT *Context = Parameter;
    WCHAR Display[TEST_DISPLAY_NAME_CCH];
    WCHAR Icon[TEST_ICON_PATH_CCH];
    UINT Index = 0;

    while (!Context->Stop)
    {
        TestTupleString(Index, TEST_DISPLAY_BASE, Display, ARRAYSIZE(Display));
        TestTupleString(Index, TEST_ICON_BASE, Icon, ARRAYSIZE(Icon));
        reactos_audio_session_set_strings(&SharedId, Display, Icon);
        reactos_audio_session_set_master(&SharedId, TestTupleVolume(Index));
        InterlockedIncrement(&Context->Writes);
        Index = (Index + 1) % TEST_TUPLE_COUNT;
    }

    return 0;
}

static DWORD WINAPI ApiRead(LPVOID Parameter)
{
    API_CONTEXT *Context = Parameter;
    LONG Reads = 0, Failures = 0;
    LONG TornDisplayName = 0, TornIconPath = 0, TornMaster = 0;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    while (!Context->Stop)
    {
        WCHAR *Name = NULL;
        WCHAR *Icon = NULL;
        float Level = 0.0f;

        if (FAILED(IAudioSessionControl2_GetDisplayName(Control, &Name)) || !Name)
        {
            ++Failures;
            continue;
        }
        if (FAILED(IAudioSessionControl2_GetIconPath(Control, &Icon)) || !Icon)
        {
            CoTaskMemFree(Name);
            ++Failures;
            continue;
        }

        ++Reads;

        if (!TestTupleStringValid(Name, TEST_DISPLAY_NAME_CCH, TEST_DISPLAY_BASE, NULL))
            ++TornDisplayName;
        if (!TestTupleStringValid(Icon, TEST_ICON_PATH_CCH, TEST_ICON_BASE, NULL))
            ++TornIconPath;

        CoTaskMemFree(Name);
        CoTaskMemFree(Icon);

        if (SimpleVolume &&
            SUCCEEDED(ISimpleAudioVolume_GetMasterVolume(SimpleVolume, &Level)) &&
            !TestTupleVolumeValid(Level, NULL))
            ++TornMaster;
    }

    InterlockedExchangeAdd(&Context->Reads, Reads);
    InterlockedExchangeAdd(&Context->Failures, Failures);
    InterlockedExchangeAdd(&Context->TornDisplayName, TornDisplayName);
    InterlockedExchangeAdd(&Context->TornIconPath, TornIconPath);
    InterlockedExchangeAdd(&Context->TornMaster, TornMaster);

    if (SUCCEEDED(hr))
        CoUninitialize();
    return 0;
}

static void TestProxyTorture(void)
{
    API_CONTEXT Context;
    HANDLE Writer;
    HANDLE Reader;
    WCHAR Display[TEST_DISPLAY_NAME_CCH];
    WCHAR Icon[TEST_ICON_PATH_CCH];

    ZeroMemory(&Context, sizeof(Context));

    TestTupleString(0, TEST_DISPLAY_BASE, Display, ARRAYSIZE(Display));
    TestTupleString(0, TEST_ICON_BASE, Icon, ARRAYSIZE(Icon));
    ok_hr(reactos_audio_session_set_strings(&SharedId, Display, Icon), S_OK);
    ok_hr(reactos_audio_session_set_master(&SharedId, TestTupleVolume(0)), S_OK);

    Writer = CreateThread(NULL, 0, ApiWrite, &Context, 0, NULL);
    Reader = CreateThread(NULL, 0, ApiRead, &Context, 0, NULL);
    ok(Writer != NULL && Reader != NULL, "CreateThread failed: %lu\n", GetLastError());

    Sleep(API_TORTURE_MILLISECONDS);
    InterlockedExchange(&Context.Stop, TRUE);

    if (Writer)
    {
        ok_eq_ulong(WaitForSingleObject(Writer, 30000), (ULONG)WAIT_OBJECT_0);
        CloseHandle(Writer);
    }
    if (Reader)
    {
        ok_eq_ulong(WaitForSingleObject(Reader, 30000), (ULONG)WAIT_OBJECT_0);
        CloseHandle(Reader);
    }

    trace("Proxy torture: %ld reads, %ld writes\n", Context.Reads, Context.Writes);

    ok(Context.Reads > 0, "The proxy reader never observed the session\n");
    ok(Context.Writes > 0, "The writer never updated the session\n");
    ok_eq_long(Context.Failures, 0);
    ok_eq_long(Context.TornDisplayName, 0);
    ok_eq_long(Context.TornIconPath, 0);
    ok_eq_long(Context.TornMaster, 0);
}

START_TEST(sessionapi)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    ok(SUCCEEDED(hr), "CoInitializeEx failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    if (OpenDefaultEndpoint() && PublishSharedSession() && FindPublishedSession())
    {
        TestProxyContract();
        TestProxyTorture();
    }

    if (ChannelVolume)
        IChannelAudioVolume_Release(ChannelVolume);
    if (SimpleVolume)
        ISimpleAudioVolume_Release(SimpleVolume);
    if (Control)
        IAudioSessionControl2_Release(Control);
    if (ProxyEndpoint)
        TestEndpointDestroy(ProxyEndpoint);
    if (Manager)
        IAudioSessionManager2_Release(Manager);
    if (Endpoint)
        IMMDevice_Release(Endpoint);
    if (DeviceEnumerator)
        IMMDeviceEnumerator_Release(DeviceEnumerator);
    CoUninitialize();
}
