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
    _In_opt_ PCWSTR Title,
    _In_ UINT TitleId,
    _In_ UINT StringId,
    _In_ UINT DetailId,
    _In_ DWORD Error,
    _In_ UINT Type)
{
    WCHAR Text[512], Detail[256], Caption[64];
    DWORD Length;

    if (!LoadStringW(GetModuleHandleW(NULL), StringId, Text, _countof(Text)))
        Text[0] = UNICODE_NULL;

    Detail[0] = UNICODE_NULL;
    if (DetailId != 0)
    {
        LoadStringW(GetModuleHandleW(NULL), DetailId, Detail, _countof(Detail));
    }
    else if (Error != ERROR_SUCCESS)
    {
        Length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL,
                                Error,
                                0,
                                Detail,
                                _countof(Detail),
                                NULL);
        if (Length == 0)
            swprintf(Detail, _countof(Detail), L"0x%08lx", Error);
    }

    if (Detail[0] && wcslen(Text) + wcslen(Detail) + 3 < _countof(Text))
    {
        wcscat(Text, L"\n\n");
        wcscat(Text, Detail);
    }

    if (TitleId != 0 && LoadStringW(GetModuleHandleW(NULL), TitleId, Caption, _countof(Caption)))
        Title = Caption;

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
        ShowMessage(L"InfDefaultInstall", 0, IDS_USAGE, 0, ERROR_SUCCESS, MB_ICONINFORMATION);
        return ERROR_INVALID_PARAMETER;
    }

    Length = GetFullPathNameW(Arguments[1], _countof(InfPath), InfPath, NULL);
    LocalFree(Arguments);
    if (Length == 0 || Length >= _countof(InfPath))
    {
        ShowMessage(NULL, IDS_ERROR_TITLE, IDS_FAILED, 0, ERROR_FILE_NOT_FOUND, MB_ICONERROR);
        return ERROR_FILE_NOT_FOUND;
    }

    hInf = SetupOpenInfFileW(InfPath, NULL, INF_STYLE_WIN4, NULL);
    if (hInf == INVALID_HANDLE_VALUE)
    {
        DWORD Error = GetLastError();

        ShowMessage(NULL, IDS_ERROR_TITLE, IDS_FAILED, 0, Error, MB_ICONERROR);
        return (int)Error;
    }

    if (SetupDiGetActualSectionToInstallW(hInf, L"DefaultInstall", Section, _countof(Section), NULL, NULL) &&
        SetupGetLineCountW(hInf, Section) != -1)
    {
        SetupCloseInfFile(hInf);

        swprintf(Command, _countof(Command), L"DefaultInstall 132 %s", InfPath);
        InstallHinfSectionW(NULL, NULL, Command, SW_SHOWNORMAL);
        ShowMessage(InfPath, 0, IDS_SUCCESS, 0, ERROR_SUCCESS, MB_ICONINFORMATION);
        return ERROR_SUCCESS;
    }

    if (!SetupFindFirstLineW(hInf, L"Manufacturer", NULL, &Context))
    {
        SetupCloseInfFile(hInf);
        ShowMessage(NULL, IDS_INSTALL_ERROR, IDS_UNSUPPORTED, IDS_NO_SECTION, ERROR_SUCCESS, MB_ICONERROR);
        return ERROR_BAD_FORMAT;
    }

    SetupCloseInfFile(hInf);

    if (!DiInstallDriverW(NULL, InfPath, 0, &NeedReboot))
    {
        DWORD Error = GetLastError();

        ShowMessage(NULL, IDS_ERROR_TITLE, IDS_FAILED, 0, Error, MB_ICONERROR);
        return (int)Error;
    }

    ShowMessage(InfPath, 0, IDS_SUCCESS, 0, ERROR_SUCCESS, MB_ICONINFORMATION);

    if (NeedReboot)
        SetupPromptReboot(NULL, NULL, FALSE);

    return NeedReboot ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS;
}
