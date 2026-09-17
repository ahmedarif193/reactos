/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Chrome-style restricted low-integrity Core Audio parity test
 */

#include <apitest.h>

#include "sessionharness.h"
#include "sandboxaudio.h"

#include <sddl.h>
#include <strsafe.h>
#include <wchar.h>

#define SANDBOX_AUDIO_TIMEOUT 30000

static const WCHAR sync_sddl[] =
    L"D:(A;;GA;;;WD)(A;;GA;;;RC)S:(ML;;NW;;;LW)";

static BOOL SetLowIntegrity(HANDLE Token)
{
    SID_IDENTIFIER_AUTHORITY Authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    TOKEN_MANDATORY_LABEL Label;
    BOOL Result;

    if (!AllocateAndInitializeSid(&Authority, 1, SECURITY_MANDATORY_LOW_RID,
                                  0, 0, 0, 0, 0, 0, 0, &Label.Label.Sid))
        return FALSE;
    Label.Label.Attributes = SE_GROUP_INTEGRITY;
    Result = SetTokenInformation(Token, TokenIntegrityLevel, &Label, sizeof(Label));
    FreeSid(Label.Label.Sid);
    return Result;
}

static BOOL CreateKnownSid(WELL_KNOWN_SID_TYPE Type, BYTE Buffer[SECURITY_MAX_SID_SIZE], PSID *Sid)
{
    DWORD Length = SECURITY_MAX_SID_SIZE;

    *Sid = Buffer;
    return CreateWellKnownSid(Type, NULL, *Sid, &Length);
}

static HANDLE CreateAudioSandboxToken(void)
{
    BYTE UsersBuffer[SECURITY_MAX_SID_SIZE];
    BYTE WorldBuffer[SECURITY_MAX_SID_SIZE];
    BYTE InteractiveBuffer[SECURITY_MAX_SID_SIZE];
    BYTE LocalBuffer[SECURITY_MAX_SID_SIZE];
    BYTE AuthenticatedBuffer[SECURITY_MAX_SID_SIZE];
    BYTE RestrictedBuffer[SECURITY_MAX_SID_SIZE];
    SID_AND_ATTRIBUTES Restricting[8];
    PSID Users, World, Interactive, Local, Authenticated, RestrictedSid;
    PSID Logon = NULL;
    PSID_AND_ATTRIBUTES Disabled = NULL;
    PTOKEN_GROUPS Groups = NULL;
    PTOKEN_USER User = NULL;
    HANDLE Base = NULL, Token = NULL;
    DWORD GroupsLength = 0, UserLength = 0;
    DWORD DisableCount = 0, RestrictCount = 0, Index;

    if (!CreateKnownSid(WinBuiltinUsersSid, UsersBuffer, &Users) ||
        !CreateKnownSid(WinWorldSid, WorldBuffer, &World) ||
        !CreateKnownSid(WinInteractiveSid, InteractiveBuffer, &Interactive) ||
        !CreateKnownSid(WinLocalSid, LocalBuffer, &Local) ||
        !CreateKnownSid(WinAuthenticatedUserSid, AuthenticatedBuffer, &Authenticated) ||
        !CreateKnownSid(WinRestrictedCodeSid, RestrictedBuffer, &RestrictedSid))
        return NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &Base))
        goto Cleanup;

    GetTokenInformation(Base, TokenGroups, NULL, 0, &GroupsLength);
    GetTokenInformation(Base, TokenUser, NULL, 0, &UserLength);
    if (!GroupsLength || !UserLength)
        goto Cleanup;
    Groups = HeapAlloc(GetProcessHeap(), 0, GroupsLength);
    User = HeapAlloc(GetProcessHeap(), 0, UserLength);
    if (!Groups || !User ||
        !GetTokenInformation(Base, TokenGroups, Groups, GroupsLength, &GroupsLength) ||
        !GetTokenInformation(Base, TokenUser, User, UserLength, &UserLength))
        goto Cleanup;

    Disabled = HeapAlloc(GetProcessHeap(), 0,
                         Groups->GroupCount * sizeof(*Disabled));
    if (!Disabled)
        goto Cleanup;

    for (Index = 0; Index < Groups->GroupCount; ++Index)
    {
        PSID Sid = Groups->Groups[Index].Sid;

        if (Groups->Groups[Index].Attributes & SE_GROUP_LOGON_ID)
            Logon = Sid;
        if (Groups->Groups[Index].Attributes &
                (SE_GROUP_INTEGRITY | SE_GROUP_LOGON_ID) ||
            EqualSid(Sid, Users) || EqualSid(Sid, World) ||
            EqualSid(Sid, Interactive) || EqualSid(Sid, Local) ||
            EqualSid(Sid, Authenticated))
            continue;
        Disabled[DisableCount].Sid = Sid;
        Disabled[DisableCount++].Attributes = 0;
    }

    Restricting[RestrictCount].Sid = Users;
    Restricting[RestrictCount++].Attributes = 0;
    Restricting[RestrictCount].Sid = World;
    Restricting[RestrictCount++].Attributes = 0;
    Restricting[RestrictCount].Sid = Interactive;
    Restricting[RestrictCount++].Attributes = 0;
    Restricting[RestrictCount].Sid = Local;
    Restricting[RestrictCount++].Attributes = 0;
    Restricting[RestrictCount].Sid = Authenticated;
    Restricting[RestrictCount++].Attributes = 0;
    Restricting[RestrictCount].Sid = RestrictedSid;
    Restricting[RestrictCount++].Attributes = 0;
    Restricting[RestrictCount].Sid = User->User.Sid;
    Restricting[RestrictCount++].Attributes = 0;
    if (Logon)
    {
        Restricting[RestrictCount].Sid = Logon;
        Restricting[RestrictCount++].Attributes = 0;
    }

    if (!CreateRestrictedToken(Base, DISABLE_MAX_PRIVILEGE,
                               DisableCount, Disabled, 0, NULL,
                               RestrictCount, Restricting,
                               &Token) ||
        !SetLowIntegrity(Token))
    {
        if (Token)
            CloseHandle(Token);
        Token = NULL;
    }

Cleanup:
    if (Disabled) HeapFree(GetProcessHeap(), 0, Disabled);
    if (User) HeapFree(GetProcessHeap(), 0, User);
    if (Groups) HeapFree(GetProcessHeap(), 0, Groups);
    if (Base) CloseHandle(Base);
    return Token;
}

static BOOL CreateSyncEvents(HANDLE *ReadyEvent, HANDLE *StopEvent)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    SECURITY_ATTRIBUTES Attributes;
    WCHAR ReadyName[MAX_PATH], StopName[MAX_PATH];

    *ReadyEvent = NULL;
    *StopEvent = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sync_sddl, SDDL_REVISION_1, &Descriptor, NULL))
        return FALSE;

    Attributes.nLength = sizeof(Attributes);
    Attributes.lpSecurityDescriptor = Descriptor;
    Attributes.bInheritHandle = FALSE;
    TestEventNames(GetCurrentProcessId(), ReadyName, StopName, ARRAYSIZE(ReadyName));
    *ReadyEvent = CreateEventW(&Attributes, TRUE, FALSE, ReadyName);
    *StopEvent = CreateEventW(&Attributes, TRUE, FALSE, StopName);
    LocalFree(Descriptor);
    if (!*ReadyEvent || !*StopEvent)
    {
        if (*ReadyEvent) CloseHandle(*ReadyEvent);
        if (*StopEvent) CloseHandle(*StopEvent);
        *ReadyEvent = *StopEvent = NULL;
        return FALSE;
    }
    return TRUE;
}

static BOOL GetChildPath(WCHAR Path[MAX_PATH])
{
    WCHAR *Separator;
    DWORD Length = GetModuleFileNameW(NULL, Path, MAX_PATH);

    if (!Length || Length >= MAX_PATH || !(Separator = wcsrchr(Path, L'\\')))
        return FALSE;
    return SUCCEEDED(StringCchCopyW(Separator + 1,
                                    MAX_PATH - (Separator + 1 - Path),
                                    L"mmdevapi_child.exe"));
}

static BOOL SpawnAudioChild(HANDLE Token, PROCESS_INFORMATION *Process)
{
    STARTUPINFOW Startup;
    WCHAR Application[MAX_PATH];
    WCHAR CommandLine[MAX_PATH * 2];

    if (!GetChildPath(Application))
        return FALSE;
    if (FAILED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                                L"\"%s\" sandbox-audio %lu", Application,
                                GetCurrentProcessId())))
        return FALSE;

    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    ZeroMemory(Process, sizeof(*Process));
    return CreateProcessAsUserW(Token, Application, CommandLine, NULL, NULL,
                                FALSE, CREATE_NO_WINDOW, NULL, NULL, &Startup,
                                Process);
}

static BOOL MixerSeesProcess(DWORD ProcessId)
{
    IAudioSessionEnumerator *Sessions = NULL;
    IMMDeviceEnumerator *DeviceEnumerator = NULL;
    IAudioSessionManager2 *Manager = NULL;
    IMMDevice *Endpoint = NULL;
    BOOL Found = FALSE;
    HRESULT hr;
    int Count = 0, Index;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&DeviceEnumerator);
    ok_hr(hr, S_OK);
    if (FAILED(hr)) goto Cleanup;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(DeviceEnumerator, eRender,
                                                       eConsole, &Endpoint);
    ok_hr(hr, S_OK);
    if (FAILED(hr)) goto Cleanup;
    hr = IMMDevice_Activate(Endpoint, &IID_IAudioSessionManager2,
                            CLSCTX_INPROC_SERVER, NULL, (void **)&Manager);
    ok_hr(hr, S_OK);
    if (FAILED(hr)) goto Cleanup;
    hr = IAudioSessionManager2_GetSessionEnumerator(Manager, &Sessions);
    ok_hr(hr, S_OK);
    if (FAILED(hr)) goto Cleanup;
    hr = IAudioSessionEnumerator_GetCount(Sessions, &Count);
    ok_hr(hr, S_OK);
    if (FAILED(hr)) goto Cleanup;

    for (Index = 0; Index < Count; ++Index)
    {
        IAudioSessionControl2 *Control2 = NULL;
        IAudioSessionControl *Control = NULL;
        AudioSessionState State;
        DWORD Candidate = 0;

        if (FAILED(IAudioSessionEnumerator_GetSession(Sessions, Index, &Control)))
            continue;
        if (SUCCEEDED(IAudioSessionControl_QueryInterface(
                Control, &IID_IAudioSessionControl2, (void **)&Control2)) &&
            SUCCEEDED(IAudioSessionControl2_GetProcessId(Control2, &Candidate)) &&
            Candidate == ProcessId)
        {
            hr = IAudioSessionControl2_GetState(Control2, &State);
            ok_hr(hr, S_OK);
            ok(State == AudioSessionStateActive,
               "restricted audio session state is %u\n", State);
            Found = TRUE;
        }
        if (Control2) IAudioSessionControl2_Release(Control2);
        IAudioSessionControl_Release(Control);
        if (Found) break;
    }

Cleanup:
    if (Sessions) IAudioSessionEnumerator_Release(Sessions);
    if (Manager) IAudioSessionManager2_Release(Manager);
    if (Endpoint) IMMDevice_Release(Endpoint);
    if (DeviceEnumerator) IMMDeviceEnumerator_Release(DeviceEnumerator);
    return Found;
}

static const char *ResultStage(DWORD Result)
{
    switch (Result)
    {
        case 0: return "complete";
        case SANDBOX_AUDIO_COM: return "COM initialization";
        case SANDBOX_AUDIO_EVENTS: return "broker events";
        case SANDBOX_AUDIO_DEVICE_INTERFACE: return "WDMAUD device interface";
        case SANDBOX_AUDIO_ENUMERATOR: return "MMDevice enumerator";
        case SANDBOX_AUDIO_ENDPOINT: return "default endpoint";
        case SANDBOX_AUDIO_ACTIVATE: return "IAudioClient3 activation";
        case SANDBOX_AUDIO_PROPERTIES: return "client properties";
        case SANDBOX_AUDIO_FORMAT: return "mix format";
        case SANDBOX_AUDIO_PERIOD: return "shared engine period";
        case SANDBOX_AUDIO_INITIALIZE: return "shared stream initialization";
        case SANDBOX_AUDIO_EVENT: return "render event";
        case SANDBOX_AUDIO_SET_EVENT: return "event callback registration";
        case SANDBOX_AUDIO_RENDER_SERVICE: return "render service";
        case SANDBOX_AUDIO_BUFFER_SIZE: return "buffer size";
        case SANDBOX_AUDIO_GET_BUFFER: return "render buffer";
        case SANDBOX_AUDIO_RELEASE_BUFFER: return "render buffer release";
        case SANDBOX_AUDIO_START: return "stream start";
        case SANDBOX_AUDIO_STOP: return "stream stop";
        default: return "unknown";
    }
}

START_TEST(sandboxaudio)
{
    PROCESS_INFORMATION Process;
    HANDLE WaitHandles[2];
    HANDLE ReadyEvent = NULL, StopEvent = NULL;
    HANDLE Token = NULL;
    DWORD ExitCode = MAXDWORD, Wait;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "CoInitializeEx failed: %#lx\n", hr);
    if (FAILED(hr))
        return;

    ZeroMemory(&Process, sizeof(Process));

    ok(CreateSyncEvents(&ReadyEvent, &StopEvent),
       "low-integrity synchronization events failed: %lu\n", GetLastError());
    if (!ReadyEvent || !StopEvent)
        goto Cleanup;

    Token = CreateAudioSandboxToken();
    ok(Token != NULL, "Chrome-style audio token creation failed: %lu\n",
       GetLastError());
    if (!Token)
        goto Cleanup;
    ok(IsTokenRestricted(Token), "audio child token is not restricted\n");

    ok(SpawnAudioChild(Token, &Process),
       "CreateProcessAsUserW(mmdevapi_child) failed: %lu\n", GetLastError());
    if (!Process.hProcess)
        goto Cleanup;
    CloseHandle(Process.hThread);

    WaitHandles[0] = ReadyEvent;
    WaitHandles[1] = Process.hProcess;
    Wait = WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE,
                                  SANDBOX_AUDIO_TIMEOUT);
    if (Wait == WAIT_OBJECT_0)
    {
        ok(MixerSeesProcess(Process.dwProcessId),
           "volume mixer did not expose the restricted audio process\n");
    }
    else if (Wait == WAIT_OBJECT_0 + 1)
    {
        GetExitCodeProcess(Process.hProcess, &ExitCode);
        ok(0, "restricted audio child exited at %s (%lu)\n",
           ResultStage(ExitCode), ExitCode);
    }
    else
    {
        ok(0, "restricted audio child did not initialize: wait %lu\n", Wait);
    }

    SetEvent(StopEvent);
    Wait = WaitForSingleObject(Process.hProcess, SANDBOX_AUDIO_TIMEOUT);
    ok_eq_ulong(Wait, (ULONG)WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateProcess(Process.hProcess, 0xdead);
        WaitForSingleObject(Process.hProcess, 5000);
    }
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    ok(ExitCode == 0, "restricted audio child failed at %s (%lu)\n",
       ResultStage(ExitCode), ExitCode);
    CloseHandle(Process.hProcess);

Cleanup:
    if (Token) CloseHandle(Token);
    if (StopEvent) CloseHandle(StopEvent);
    if (ReadyEvent) CloseHandle(ReadyEvent);
    CoUninitialize();
}
