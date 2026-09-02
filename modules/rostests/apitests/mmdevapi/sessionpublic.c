/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Public Core Audio session metadata parity tests
 */

#include <apitest.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

static BOOL CheckString(IAudioSessionControl *Control, BOOL Icon, LPCWSTR Expected)
{
    WCHAR *Value = NULL;
    HRESULT hr;

    if (Icon)
        hr = IAudioSessionControl_GetIconPath(Control, &Value);
    else
        hr = IAudioSessionControl_GetDisplayName(Control, &Value);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return FALSE;

    ok(Value != NULL, "The returned string is NULL\n");
    if (Value)
    {
        ok_eq_wstr(Value, Expected);
        CoTaskMemFree(Value);
    }
    return Value != NULL;
}

static IAudioSessionControl *FindSession(IAudioSessionManager2 *Manager,
                                         LPCWSTR DisplayName)
{
    IAudioSessionEnumerator *Enumerator = NULL;
    IAudioSessionControl *Control = NULL;
    HRESULT hr;
    int Count = 0, i;

    hr = IAudioSessionManager2_GetSessionEnumerator(Manager, &Enumerator);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return NULL;

    hr = IAudioSessionEnumerator_GetCount(Enumerator, &Count);
    ok_hr(hr, S_OK);
    for (i = 0; SUCCEEDED(hr) && i < Count; ++i)
    {
        IAudioSessionControl *Candidate = NULL;
        WCHAR *Name = NULL;

        if (FAILED(IAudioSessionEnumerator_GetSession(Enumerator, i, &Candidate)))
            continue;
        if (SUCCEEDED(IAudioSessionControl_GetDisplayName(Candidate, &Name)) &&
            Name && !lstrcmpW(Name, DisplayName))
        {
            Control = Candidate;
            Candidate = NULL;
        }
        CoTaskMemFree(Name);
        if (Candidate)
            IAudioSessionControl_Release(Candidate);
        if (Control)
            break;
    }

    IAudioSessionEnumerator_Release(Enumerator);
    return Control;
}

START_TEST(sessionpublic)
{
    static const WCHAR InitialName[] = L"mmdevapi public parity session";
    static const WCHAR InitialIcon[] = L"C:\\Windows\\System32\\mmdevapi.dll,-1";
    IMMDeviceEnumerator *DeviceEnumerator = NULL;
    IMMDevice *Endpoint = NULL;
    IAudioSessionManager2 *Manager = NULL;
    IAudioClient *Client1 = NULL, *Client2 = NULL;
    IAudioSessionControl *Control1 = NULL, *Control2 = NULL, *Proxy = NULL;
    WAVEFORMATEX *Format = NULL;
    WCHAR Name[96], Icon[128];
    GUID SessionGuid;
    HRESULT hr;
    DWORD deadline;
    UINT i;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "CoInitializeEx failed: %#lx\n", hr);
    if (FAILED(hr))
        return;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&DeviceEnumerator);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        goto done;

    deadline = GetTickCount() + 30000;
    do
    {
        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(DeviceEnumerator, eRender,
                                                          eMultimedia, &Endpoint);
        if (hr != E_NOTFOUND)
            break;
        Sleep(100);
    } while ((LONG)(deadline - GetTickCount()) > 0);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        goto done;

    hr = IMMDevice_Activate(Endpoint, &IID_IAudioSessionManager2,
                            CLSCTX_INPROC_SERVER, NULL, (void **)&Manager);
    ok_hr(hr, S_OK);
    hr = IMMDevice_Activate(Endpoint, &IID_IAudioClient,
                            CLSCTX_INPROC_SERVER, NULL, (void **)&Client1);
    ok_hr(hr, S_OK);
    if (!Manager || !Client1)
        goto done;

    hr = IAudioClient_GetMixFormat(Client1, &Format);
    ok_hr(hr, S_OK);
    CoCreateGuid(&SessionGuid);
    hr = IAudioClient_Initialize(Client1, AUDCLNT_SHAREMODE_SHARED, 0,
                                 5000000, 0, Format, &SessionGuid);
    ok_hr(hr, S_OK);
    hr = IMMDevice_Activate(Endpoint, &IID_IAudioClient,
                            CLSCTX_INPROC_SERVER, NULL, (void **)&Client2);
    ok_hr(hr, S_OK);
    if (!Client2)
        goto done;
    hr = IAudioClient_Initialize(Client2, AUDCLNT_SHAREMODE_SHARED, 0,
                                 5000000, 0, Format, &SessionGuid);
    ok_hr(hr, S_OK);

    hr = IAudioClient_GetService(Client1, &IID_IAudioSessionControl,
                                 (void **)&Control1);
    ok_hr(hr, S_OK);
    hr = IAudioClient_GetService(Client2, &IID_IAudioSessionControl,
                                 (void **)&Control2);
    ok_hr(hr, S_OK);
    if (!Control1 || !Control2)
        goto done;

    ok_hr(IAudioSessionControl_GetDisplayName(Control1, NULL), E_POINTER);
    ok_hr(IAudioSessionControl_GetIconPath(Control1, NULL), E_POINTER);
    ok_hr(IAudioSessionControl_SetDisplayName(Control1, NULL, NULL),
          HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER));
    ok_hr(IAudioSessionControl_SetIconPath(Control1, NULL, NULL),
          HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER));

    ok_hr(IAudioSessionControl_SetDisplayName(Control1, InitialName, NULL), S_OK);
    ok_hr(IAudioSessionControl_SetIconPath(Control1, InitialIcon, NULL), S_OK);
    CheckString(Control2, FALSE, InitialName);
    CheckString(Control2, TRUE, InitialIcon);

    Proxy = FindSession(Manager, InitialName);
    ok(Proxy != NULL, "The session enumerator did not return the new session\n");
    if (!Proxy)
        goto done;
    CheckString(Proxy, TRUE, InitialIcon);

    for (i = 0; i < 128; ++i)
    {
        wsprintfW(Name, L"session-name-%03u-abcdefghijklmnopqrstuvwxyz", i);
        wsprintfW(Icon, L"C:\\parity\\icon-%03u.dll,-%u", i, i + 1);
        ok_hr(IAudioSessionControl_SetDisplayName((i & 1) ? Control1 : Proxy,
                                                   Name, NULL), S_OK);
        ok_hr(IAudioSessionControl_SetIconPath((i & 1) ? Proxy : Control2,
                                               Icon, NULL), S_OK);
        CheckString((i & 1) ? Proxy : Control2, FALSE, Name);
        CheckString((i & 1) ? Control1 : Proxy, TRUE, Icon);
    }

    ok_hr(IAudioSessionControl_SetDisplayName(Control2, L"", NULL), S_OK);
    ok_hr(IAudioSessionControl_SetIconPath(Control2, L"", NULL), S_OK);
    CheckString(Control1, FALSE, L"");
    CheckString(Control1, TRUE, L"");
    CheckString(Proxy, FALSE, L"");
    CheckString(Proxy, TRUE, L"");

done:
    if (Proxy) IAudioSessionControl_Release(Proxy);
    if (Control2) IAudioSessionControl_Release(Control2);
    if (Control1) IAudioSessionControl_Release(Control1);
    if (Client2) IAudioClient_Release(Client2);
    if (Format) CoTaskMemFree(Format);
    if (Client1) IAudioClient_Release(Client1);
    if (Manager) IAudioSessionManager2_Release(Manager);
    if (Endpoint) IMMDevice_Release(Endpoint);
    if (DeviceEnumerator) IMMDeviceEnumerator_Release(DeviceEnumerator);
    CoUninitialize();
}
