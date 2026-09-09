/*
 * PROJECT:     ReactOS DWM Settings
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native desktop panel for persistent, live GPU effect settings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <windows.h>
#include <stdio.h>
#include "settings.h"
#include "resource.h"

static HINSTANCE Instance;

static void
DwmSettingsStatus(HWND Dialog, UINT String, DWORD Error)
{
    WCHAR Format[256], Text[320];

    LoadStringW(Instance, String, Format, ARRAYSIZE(Format));
    if (String == IDS_READ_FAILED || String == IDS_WRITE_FAILED)
    {
        _snwprintf(Text, ARRAYSIZE(Text), Format, Error);
        Text[ARRAYSIZE(Text) - 1] = 0;
        SetDlgItemTextW(Dialog, IDC_STATUS, Text);
    }
    else
        SetDlgItemTextW(Dialog, IDC_STATUS, Format);
}

static void
DwmSettingsCheckControls(HWND Dialog, DWORD Effects)
{
    UINT Index;

    for (Index = 0; Index < 5; ++Index)
        CheckDlgButton(Dialog, IDC_ANIMATIONS + Index, (Effects & (1u << Index)) ? BST_CHECKED : BST_UNCHECKED);
}

static void
DwmSettingsNotify(HWND Dialog, BOOL Reload)
{
    HWND Compositor = FindWindowExW(HWND_MESSAGE, NULL, DWM_SETTINGS_CLASS, NULL);
    DWORD_PTR Reply = 0;
    UINT String;

    /* Message-only windows do not receive HWND_BROADCAST. Registry handles
     * are already closed before this bounded call into the render thread. */
    if (Compositor == NULL ||
        !SendMessageTimeoutW(Compositor, Reload ? WM_SETTINGCHANGE : DWM_SETTINGS_QUERY, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &Reply) ||
        (Reply & 0xffff0000u) != DWM_SETTINGS_REPLY)
        String = Reload ? IDS_UNAVAILABLE : IDS_DISCONNECTED;
    else if (Reply & DWM_SETTINGS_ERROR)
        String = IDS_RELOAD_FAILED;
    else if (!(Reply & DWM_SETTINGS_GPU))
        String = Reload ? IDS_INACTIVE : IDS_GPU_UNAVAILABLE;
    else if (Reload && (Reply & DWM_EFFECT_ALL) != (DWORD)GetWindowLongPtrW(Dialog, DWLP_USER))
        String = IDS_MISMATCH;
    else
        String = Reload ? IDS_APPLIED : IDS_ACTIVE;
    DwmSettingsStatus(Dialog, String, 0);
}

static INT_PTR CALLBACK
DwmSettingsDialogProc(HWND Dialog, UINT Message, WPARAM WParam, LPARAM LParam)
{
    DWORD Effects, Effect;
    LONG Error;
    UINT Control;
    BOOL Enabled;

    UNREFERENCED_PARAMETER(LParam);
    switch (Message)
    {
        case WM_INITDIALOG:
            SendMessageW(Dialog, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(Instance, MAKEINTRESOURCEW(IDI_SETTINGS)));
            SendMessageW(Dialog, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(Instance, MAKEINTRESOURCEW(IDI_SETTINGS)));
            Error = DwmSettingsRead(&Effects);
            SetWindowLongPtrW(Dialog, DWLP_USER, Effects);
            DwmSettingsCheckControls(Dialog, Effects);
            if (Error == ERROR_SUCCESS)
                DwmSettingsNotify(Dialog, FALSE);
            else
                DwmSettingsStatus(Dialog, IDS_READ_FAILED, Error);
            return TRUE;

        case WM_COMMAND:
            Control = LOWORD(WParam);
            if (Control == IDCANCEL)
            {
                EndDialog(Dialog, 0);
                return TRUE;
            }
            if (HIWORD(WParam) != BN_CLICKED || Control < IDC_ANIMATIONS || Control > IDC_ACRYLIC)
                break;
            Effects = (DWORD)GetWindowLongPtrW(Dialog, DWLP_USER);
            Effect = 1u << (Control - IDC_ANIMATIONS);
            Enabled = IsDlgButtonChecked(Dialog, Control) == BST_CHECKED;
            Error = DwmSettingsWrite(Effect, Enabled);
            if (Error != ERROR_SUCCESS)
            {
                DwmSettingsCheckControls(Dialog, Effects);
                DwmSettingsStatus(Dialog, IDS_WRITE_FAILED, Error);
                return TRUE;
            }
            Effects = Enabled ? Effects | Effect : Effects & ~Effect;
            SetWindowLongPtrW(Dialog, DWLP_USER, Effects);
            DwmSettingsNotify(Dialog, TRUE);
            return TRUE;

        case WM_CLOSE:
            EndDialog(Dialog, 0);
            return TRUE;
    }
    return FALSE;
}

int WINAPI
wWinMain(HINSTANCE CurrentInstance, HINSTANCE PreviousInstance, LPWSTR CommandLine, int ShowCommand)
{
    UNREFERENCED_PARAMETER(PreviousInstance);
    UNREFERENCED_PARAMETER(CommandLine);
    UNREFERENCED_PARAMETER(ShowCommand);
    Instance = CurrentInstance;
    return (int)DialogBoxParamW(Instance, MAKEINTRESOURCEW(IDD_SETTINGS), NULL, DwmSettingsDialogProc, 0);
}
