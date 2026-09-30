/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that WDMAUD opens wave pins for clients whose token cannot open the audio device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <winreg.h>
#include <winioctl.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <interface.h>

static const GUID WdmaudCategory = {STATIC_KSCATEGORY_WDMAUD};

static HANDLE OpenWdmaud(void)
{
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *Detail;
    SP_DEVICE_INTERFACE_DATA Interface;
    HANDLE Handle = INVALID_HANDLE_VALUE;
    HDEVINFO Devices;
    DWORD Size;

    Devices = SetupDiGetClassDevsW(&WdmaudCategory, NULL, NULL,
                                   DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (Devices == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    Interface.cbSize = sizeof(Interface);
    Size = sizeof(*Detail) + MAX_PATH * sizeof(WCHAR);
    Detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (Detail &&
        SetupDiEnumDeviceInterfaces(Devices, NULL, &WdmaudCategory, 0, &Interface))
    {
        Detail->cbSize = sizeof(*Detail);
        if (SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, Detail, Size, NULL, NULL))
        {
            WCHAR *Path = Detail->DevicePath;

            if (Path[0] == L'\\' && Path[1] == L'?')
                Path[1] = L'\\';
            Handle = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE,
                                 0, NULL, OPEN_EXISTING, 0, NULL);
        }
    }

    if (Detail)
        HeapFree(GetProcessHeap(), 0, Detail);
    SetupDiDestroyDeviceInfoList(Devices);
    return Handle;
}

static BOOL Ioctl(HANDLE Wdmaud, DWORD Code, WDMAUD_DEVICE_INFO *Info)
{
    DWORD Returned;

    return DeviceIoControl(Wdmaud, Code, Info, sizeof(*Info), Info, sizeof(*Info),
                           &Returned, NULL);
}

static BOOL OpenAndCloseRenderPin(HANDLE Wdmaud, const WAVEFORMATEXTENSIBLE *Format)
{
    WDMAUD_DEVICE_INFO Info;
    BOOL Opened;

    ZeroMemory(&Info, sizeof(Info));
    Info.DeviceType = WAVE_OUT_DEVICE_TYPE;
    Info.DeviceIndex = 0;
    Info.u.WaveFormatExtensible = *Format;
    Opened = Ioctl(Wdmaud, IOCTL_OPEN_WDMAUD, &Info);
    if (Opened)
    {
        HANDLE Pin = Info.hDevice;

        RevertToSelf();
        ZeroMemory(&Info, sizeof(Info));
        Info.DeviceType = WAVE_OUT_DEVICE_TYPE;
        Info.DeviceIndex = 0;
        Info.hDevice = Pin;
        ok(Ioctl(Wdmaud, IOCTL_CLOSE_WDMAUD, &Info),
           "IOCTL_CLOSE_WDMAUD failed with %lu\n", GetLastError());
    }
    return Opened;
}

static BOOL CreateWellKnown(WELL_KNOWN_SID_TYPE Type, BYTE Buffer[SECURITY_MAX_SID_SIZE])
{
    DWORD Length = SECURITY_MAX_SID_SIZE;

    return CreateWellKnownSid(Type, NULL, Buffer, &Length);
}

static HANDLE CreateAudioServiceToken(void)
{
    BYTE Users[SECURITY_MAX_SID_SIZE], World[SECURITY_MAX_SID_SIZE];
    BYTE Restricted[SECURITY_MAX_SID_SIZE], Low[SECURITY_MAX_SID_SIZE];
    SID_AND_ATTRIBUTES Restricting[3];
    PSID_AND_ATTRIBUTES Disabled = NULL;
    TOKEN_MANDATORY_LABEL Label;
    PTOKEN_GROUPS Groups = NULL;
    HANDLE Base = NULL, Primary = NULL, Impersonation = NULL;
    DWORD Length = 0, DisableCount = 0, Index;

    if (!CreateWellKnown(WinBuiltinUsersSid, Users) ||
        !CreateWellKnown(WinWorldSid, World) ||
        !CreateWellKnown(WinRestrictedCodeSid, Restricted) ||
        !CreateWellKnown(WinLowLabelSid, Low))
        return NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &Base))
        return NULL;

    GetTokenInformation(Base, TokenGroups, NULL, 0, &Length);
    Groups = Length ? HeapAlloc(GetProcessHeap(), 0, Length) : NULL;
    if (!Groups || !GetTokenInformation(Base, TokenGroups, Groups, Length, &Length))
        goto Cleanup;

    Disabled = HeapAlloc(GetProcessHeap(), 0, Groups->GroupCount * sizeof(*Disabled));
    if (!Disabled)
        goto Cleanup;
    for (Index = 0; Index < Groups->GroupCount; ++Index)
    {
        PSID Sid = Groups->Groups[Index].Sid;

        if ((Groups->Groups[Index].Attributes & (SE_GROUP_INTEGRITY | SE_GROUP_LOGON_ID)) ||
            EqualSid(Sid, Users) || EqualSid(Sid, World))
            continue;
        Disabled[DisableCount].Sid = Sid;
        Disabled[DisableCount++].Attributes = 0;
    }

    Restricting[0].Sid = Users;
    Restricting[0].Attributes = 0;
    Restricting[1].Sid = World;
    Restricting[1].Attributes = 0;
    Restricting[2].Sid = Restricted;
    Restricting[2].Attributes = 0;

    if (!CreateRestrictedToken(Base, DISABLE_MAX_PRIVILEGE, DisableCount, Disabled,
                               0, NULL, ARRAYSIZE(Restricting), Restricting, &Primary))
        goto Cleanup;

    Label.Label.Sid = Low;
    Label.Label.Attributes = SE_GROUP_INTEGRITY;
    if (!SetTokenInformation(Primary, TokenIntegrityLevel, &Label,
                             sizeof(Label) + GetLengthSid(Low)))
        goto Cleanup;

    DuplicateTokenEx(Primary, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation,
                     TokenImpersonation, &Impersonation);

Cleanup:
    if (Primary) CloseHandle(Primary);
    if (Disabled) HeapFree(GetProcessHeap(), 0, Disabled);
    if (Groups) HeapFree(GetProcessHeap(), 0, Groups);
    CloseHandle(Base);
    return Impersonation;
}

START_TEST(wdmaudbroker)
{
    WDMAUD_DEVICE_INFO Info;
    WAVEFORMATEXTENSIBLE Format;
    HANDLE Wdmaud, Token;
    BOOL Opened;

    Wdmaud = OpenWdmaud();
    if (Wdmaud == INVALID_HANDLE_VALUE)
    {
        skip("No WDMAUD interface\n");
        return;
    }

    ZeroMemory(&Info, sizeof(Info));
    Info.DeviceType = WAVE_OUT_DEVICE_TYPE;
    if (!Ioctl(Wdmaud, IOCTL_GETNUMDEVS_TYPE, &Info) || Info.DeviceCount == 0)
    {
        skip("No wave render device\n");
        CloseHandle(Wdmaud);
        return;
    }

    ZeroMemory(&Info, sizeof(Info));
    Info.DeviceType = WAVE_OUT_DEVICE_TYPE;
    Info.DeviceIndex = 0;
    if (!Ioctl(Wdmaud, IOCTL_GETPREFERRED_WAVE_FORMAT, &Info))
    {
        skip("IOCTL_GETPREFERRED_WAVE_FORMAT failed with %lu\n", GetLastError());
        CloseHandle(Wdmaud);
        return;
    }
    Format = Info.u.WaveFormatExtensible;

    if (!OpenAndCloseRenderPin(Wdmaud, &Format))
    {
        skip("The render pin is not available, IOCTL_OPEN_WDMAUD failed with %lu\n", GetLastError());
        CloseHandle(Wdmaud);
        return;
    }

    Token = CreateAudioServiceToken();
    ok(Token != NULL, "Creating the restricted token failed with %lu\n", GetLastError());
    if (Token && SetThreadToken(NULL, Token))
    {
        Opened = OpenAndCloseRenderPin(Wdmaud, &Format);
        ok(Opened, "IOCTL_OPEN_WDMAUD for a restricted low-integrity client failed with %lu\n",
           GetLastError());
        RevertToSelf();
    }
    else if (Token)
    {
        ok(0, "SetThreadToken failed with %lu\n", GetLastError());
    }

    if (Token)
        CloseHandle(Token);
    CloseHandle(Wdmaud);
}
