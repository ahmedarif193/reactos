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

typedef struct _DWM_SETTINGS_UI_STATE
{
    DWORD Effects;
    DWORD ColorScheme;
    BOOL Themed;
    COLORREF Fill;
    HBRUSH Brush;
} DWM_SETTINGS_UI_STATE;

static void
DwmSettingsApplyScheme(HWND Dialog, DWM_SETTINGS_UI_STATE *State)
{
    INT Opacity;

    if (State->Brush != NULL)
        DeleteObject(State->Brush);
    State->Brush = NULL;
    State->Themed = DwmSettingsLoadSchemeMaterial(State->ColorScheme, &State->Fill, &Opacity);
    if (State->Themed && DwmSettingsSetContentBackdrop(Dialog, State->Fill, Opacity))
        State->Brush = CreateSolidBrush(State->Fill);
    else if (!State->Themed)
        DwmSettingsClearContentBackdrop(Dialog);
    RedrawWindow(Dialog, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

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
DwmSettingsCheckControls(HWND Dialog, const DWM_SETTINGS_UI_STATE *State)
{
    UINT Index;
    BOOL Glass = State->Themed && (State->Effects & DWM_EFFECT_ACRYLIC);

    for (Index = 0; Index < 5; ++Index)
        CheckDlgButton(Dialog, IDC_ANIMATIONS + Index, (State->Effects & (1u << Index)) ? BST_CHECKED : BST_UNCHECKED);

    EnableWindow(GetDlgItem(Dialog, IDC_ACRYLIC), State->Themed);
    EnableWindow(GetDlgItem(Dialog, IDC_BLUR), Glass);
    EnableWindow(GetDlgItem(Dialog, IDC_CONTENT_BLUR), Glass);
    EnableWindow(GetDlgItem(Dialog, IDC_SCHEME_LIGHT), State->Themed);
    EnableWindow(GetDlgItem(Dialog, IDC_SCHEME_DARK), State->Themed);
    ShowWindow(GetDlgItem(Dialog, IDC_THEME_NOTE), State->Themed ? SW_HIDE : SW_SHOW);
    CheckRadioButton(Dialog, IDC_SCHEME_LIGHT, IDC_SCHEME_DARK,
                     IDC_SCHEME_LIGHT + State->ColorScheme);
}

static void
DwmSettingsNotify(HWND Dialog, const DWM_SETTINGS_UI_STATE *State, BOOL Reload)
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
    else if (Reload && (Reply & DWM_EFFECT_ALL) != State->Effects)
        String = IDS_MISMATCH;
    else if (!(Reply & DWM_SETTINGS_GPU))
        String = Reload ? IDS_INACTIVE : IDS_GPU_UNAVAILABLE;
    else
        String = Reload ? IDS_APPLIED : IDS_ACTIVE;
    DwmSettingsStatus(Dialog, String, 0);
}

static INT_PTR CALLBACK
DwmSettingsDialogProc(HWND Dialog, UINT Message, WPARAM WParam, LPARAM LParam)
{
    DWM_SETTINGS_UI_STATE *State;
    DWORD Effect, Scheme;
    LONG Error;
    UINT Control;
    BOOL Enabled;

    State = (DWM_SETTINGS_UI_STATE *)GetWindowLongPtrW(Dialog, DWLP_USER);
    switch (Message)
    {
        case WM_INITDIALOG:
            State = (DWM_SETTINGS_UI_STATE *)LParam;
            SetWindowLongPtrW(Dialog, DWLP_USER, (LONG_PTR)State);
            SendMessageW(Dialog, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(Instance, MAKEINTRESOURCEW(IDI_SETTINGS)));
            SendMessageW(Dialog, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(Instance, MAKEINTRESOURCEW(IDI_SETTINGS)));
            SetWindowTheme(GetDlgItem(Dialog, IDC_EFFECTS_GROUP), L"", L"");
            SetWindowTheme(GetDlgItem(Dialog, IDC_SCHEME_GROUP), L"", L"");
            Error = DwmSettingsRead(&State->Effects);
            State->ColorScheme = DwmSettingsReadColorScheme();
            DwmSettingsApplyScheme(Dialog, State);
            DwmSettingsCheckControls(Dialog, State);
            if (Error == ERROR_SUCCESS)
                DwmSettingsNotify(Dialog, State, FALSE);
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
            if (HIWORD(WParam) != BN_CLICKED)
                break;

            if (Control >= IDC_ANIMATIONS && Control <= IDC_CONTENT_BLUR)
            {
                Effect = 1u << (Control - IDC_ANIMATIONS);
                Enabled = IsDlgButtonChecked(Dialog, Control) == BST_CHECKED;
                Error = DwmSettingsWrite(Effect, Enabled);
                if (Error == ERROR_SUCCESS)
                    State->Effects = Enabled ? State->Effects | Effect : State->Effects & ~Effect;
            }
            else if (Control == IDC_SCHEME_LIGHT || Control == IDC_SCHEME_DARK)
            {
                Scheme = Control - IDC_SCHEME_LIGHT;
                Error = DwmSettingsWriteColorScheme(Scheme);
                if (Error == ERROR_SUCCESS)
                {
                    State->ColorScheme = Scheme;
                    DwmSettingsApplyScheme(Dialog, State);
                    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                                        (LPARAM)REACTOS_DWM_SETTINGS_KEY,
                                        SMTO_NORMAL, 2000, NULL);
                }
            }
            else
            {
                break;
            }

            if (Error != ERROR_SUCCESS)
            {
                DwmSettingsCheckControls(Dialog, State);
                DwmSettingsStatus(Dialog, IDS_WRITE_FAILED, Error);
                return TRUE;
            }
            DwmSettingsCheckControls(Dialog, State);
            DwmSettingsNotify(Dialog, State, TRUE);
            return TRUE;

        case WM_THEMECHANGED:
            if (State != NULL)
            {
                DwmSettingsApplyScheme(Dialog, State);
                DwmSettingsCheckControls(Dialog, State);
            }
            break;

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            if (State == NULL || State->Brush == NULL)
                break;
            SetTextColor((HDC)WParam, State->ColorScheme == DWM_COLOR_SCHEME_DARK ?
                         RGB(255, 255, 255) : RGB(0, 0, 0));
            SetBkColor((HDC)WParam, State->Fill);
            return (INT_PTR)State->Brush;

        case WM_CLOSE:
            EndDialog(Dialog, 0);
            return TRUE;
    }
    return FALSE;
}

int WINAPI
wWinMain(HINSTANCE CurrentInstance, HINSTANCE PreviousInstance, LPWSTR CommandLine, int ShowCommand)
{
    DWM_SETTINGS_UI_STATE State = {0};
    INT_PTR Result;

    UNREFERENCED_PARAMETER(PreviousInstance);
    UNREFERENCED_PARAMETER(CommandLine);
    UNREFERENCED_PARAMETER(ShowCommand);
    Instance = CurrentInstance;
    Result = DialogBoxParamW(Instance, MAKEINTRESOURCEW(IDD_SETTINGS), NULL,
                             DwmSettingsDialogProc, (LPARAM)&State);
    if (State.Brush != NULL)
        DeleteObject(State.Brush);
    return (int)Result;
}
