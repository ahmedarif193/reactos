/*
 * PROJECT:     LiberNT INF Default Install
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Install an INF from its shell context menu
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <wingdi.h>
#include <winreg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <setupapi.h>
#include <newdev.h>

#include "resource.h"

static INT
ShowMessage(
    _In_ PCWSTR Title,
    _In_ UINT StringId,
    _In_ DWORD Error,
    _In_ UINT Type)
{
    WCHAR Text[512], System[256];
    DWORD Length;

    if (!LoadStringW(GetModuleHandleW(NULL), StringId, Text, _countof(Text)))
        Text[0] = UNICODE_NULL;

    if (Error != ERROR_SUCCESS)
    {
        Length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL,
                                Error,
                                0,
                                System,
                                _countof(System),
                                NULL);
        if (Length == 0)
            swprintf(System, _countof(System), L"0x%08lx", Error);

        if (wcslen(Text) + wcslen(System) + 2 < _countof(Text))
        {
            wcscat(Text, L"\n");
            wcscat(Text, System);
        }
    }

    return MessageBoxW(NULL, Text, Title, MB_OK | Type);
}

int
WINAPI
wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ PWSTR lpCmdLine,
    _In_ int nShowCmd)
{
    WCHAR InfPath[MAX_PATH], Section[MAX_PATH], Command[MAX_PATH + 32];
    INFCONTEXT Context;
    PWSTR *Arguments;
    BOOL NeedReboot = FALSE;
    DWORD Length;
    HINF hInf;
    INT Count;

    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);

    Arguments = CommandLineToArgvW(GetCommandLineW(), &Count);
    if (!Arguments || Count < 2)
    {
        ShowMessage(L"InfDefaultInstall", IDS_USAGE, ERROR_SUCCESS, MB_ICONINFORMATION);
        return ERROR_INVALID_PARAMETER;
    }

    Length = GetFullPathNameW(Arguments[1], _countof(InfPath), InfPath, NULL);
    LocalFree(Arguments);
    if (Length == 0 || Length >= _countof(InfPath))
    {
        ShowMessage(L"InfDefaultInstall", IDS_FAILED, ERROR_FILE_NOT_FOUND, MB_ICONERROR);
        return ERROR_FILE_NOT_FOUND;
    }

    hInf = SetupOpenInfFileW(InfPath, NULL, INF_STYLE_WIN4, NULL);
    if (hInf == INVALID_HANDLE_VALUE)
    {
        DWORD Error = GetLastError();

        ShowMessage(InfPath, IDS_FAILED, Error, MB_ICONERROR);
        return (int)Error;
    }

    if (SetupDiGetActualSectionToInstallW(hInf, L"DefaultInstall", Section, _countof(Section), NULL, NULL) &&
        SetupGetLineCountW(hInf, Section) != -1)
    {
        SetupCloseInfFile(hInf);

        swprintf(Command, _countof(Command), L"DefaultInstall 132 %s", InfPath);
        InstallHinfSectionW(NULL, NULL, Command, SW_SHOWNORMAL);
        ShowMessage(InfPath, IDS_SUCCESS, ERROR_SUCCESS, MB_ICONINFORMATION);
        return ERROR_SUCCESS;
    }

    if (!SetupFindFirstLineW(hInf, L"Manufacturer", NULL, &Context))
    {
        SetupCloseInfFile(hInf);
        ShowMessage(InfPath, IDS_UNSUPPORTED, ERROR_SUCCESS, MB_ICONERROR);
        return ERROR_BAD_FORMAT;
    }

    SetupCloseInfFile(hInf);

    if (!DiInstallDriverW(NULL, InfPath, 0, &NeedReboot))
    {
        DWORD Error = GetLastError();

        ShowMessage(InfPath, IDS_FAILED, Error, MB_ICONERROR);
        return (int)Error;
    }

    ShowMessage(InfPath, IDS_SUCCESS, ERROR_SUCCESS, MB_ICONINFORMATION);

    if (NeedReboot)
        SetupPromptReboot(NULL, NULL, FALSE);

    return NeedReboot ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS;
}
