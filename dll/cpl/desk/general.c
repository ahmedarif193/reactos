/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Display Control Panel
 * FILE:            dll/cpl/desk/general.c
 * PURPOSE:         Advanced General settings
 */

#include "desk.h"

#define FONT_DPI_KEY \
    _T("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontDPI")
#define HARDWARE_PROFILE_FONT_KEY \
    _T("SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current\\Software\\Fonts")
#define WINDOW_METRICS_KEY \
    _T("Control Panel\\Desktop\\WindowMetrics")

static BOOL
WriteDpiValue(HKEY hRoot, LPCTSTR pszKey, DWORD dwDpi)
{
    HKEY hKey;
    LONG lError;

    lError = RegCreateKeyEx(hRoot, pszKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (lError != ERROR_SUCCESS)
    {
        SetLastError(lError);
        return FALSE;
    }

    lError = RegSetValueEx(hKey, _T("LogPixels"), 0, REG_DWORD, (const BYTE *)&dwDpi, sizeof(dwDpi));
    RegCloseKey(hKey);

    if (lError != ERROR_SUCCESS)
    {
        SetLastError(lError);
        return FALSE;
    }

    return TRUE;
}

static BOOL
SaveDpi(HWND hWnd, PBOOL pbChanged)
{
    HWND hFontSize = GetDlgItem(hWnd, IDC_FONTSIZE_COMBO);
    LRESULT iSelection;
    DWORD dwDpi;
    HKEY hKey;
    DWORD dwCurrent = 96, dwType = REG_DWORD, cbData = sizeof(dwCurrent);

    *pbChanged = FALSE;

    iSelection = SendMessage(hFontSize, CB_GETCURSEL, 0, 0);
    if (iSelection == CB_ERR)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    dwDpi = (DWORD)SendMessage(hFontSize, CB_GETITEMDATA, (WPARAM)iSelection, 0);
    if (dwDpi < 96 || dwDpi > 480)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, HARDWARE_PROFILE_FONT_KEY, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hKey,
                            _T("LogPixels"),
                            NULL,
                            &dwType,
                            (LPBYTE)&dwCurrent,
                            &cbData) != ERROR_SUCCESS ||
            dwType != REG_DWORD ||
            cbData != sizeof(dwCurrent) ||
            dwCurrent < 96 ||
            dwCurrent > 480)
        {
            dwCurrent = 96;
        }
        RegCloseKey(hKey);
    }

    *pbChanged = (dwCurrent != dwDpi);

    /* Write mirrors first; commit the authoritative hardware-profile value last. */
    if (!WriteDpiValue(HKEY_LOCAL_MACHINE, FONT_DPI_KEY, dwDpi))
        return FALSE;

    {
        HKEY hKey;
        LONG lError;

        lError = RegCreateKeyEx(HKEY_CURRENT_USER,
                                WINDOW_METRICS_KEY,
                                0,
                                NULL,
                                REG_OPTION_NON_VOLATILE,
                                KEY_SET_VALUE,
                                NULL,
                                &hKey,
                                NULL);
        if (lError != ERROR_SUCCESS)
        {
            SetLastError(lError);
            return FALSE;
        }

        lError = RegSetValueEx(hKey, _T("AppliedDPI"), 0, REG_DWORD, (const BYTE *)&dwDpi, sizeof(dwDpi));
        RegCloseKey(hKey);
        if (lError != ERROR_SUCCESS)
        {
            SetLastError(lError);
            return FALSE;
        }
    }

    if (!WriteDpiValue(HKEY_LOCAL_MACHINE, HARDWARE_PROFILE_FONT_KEY, dwDpi))
    {
        return FALSE;
    }

    if (!*pbChanged)
        return TRUE;

    SendMessageTimeout(HWND_BROADCAST,
                       WM_SETTINGCHANGE,
                       0,
                       (LPARAM)_T("Control Panel\\Desktop"),
                       SMTO_ABORTIFHUNG,
                       2000,
                       NULL);
    return TRUE;
}

static VOID
InitFontSizeList(HWND hWnd)
{
    HINF hInf;
    HKEY hKey;
    HWND hFontSize;
    INFCONTEXT Context;
    int i, ci = 0, iSelected = -1;
    DWORD dwSize, dwValue = 96, dwType;

    hFontSize = GetDlgItem(hWnd, IDC_FONTSIZE_COMBO);

    hInf = SetupOpenInfFile(_T("font.inf"), NULL,
                            INF_STYLE_WIN4, NULL);

    if (hInf != INVALID_HANDLE_VALUE)
    {
        if (SetupFindFirstLine(hInf, _T("Font Sizes"), NULL, &Context))
        {
            if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, HARDWARE_PROFILE_FONT_KEY,
                             0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                dwSize = sizeof(dwValue);
                dwType = REG_DWORD;

                if (RegQueryValueEx(hKey, _T("LogPixels"), NULL, &dwType,
                                    (LPBYTE)&dwValue, &dwSize) != ERROR_SUCCESS ||
                    dwType != REG_DWORD ||
                    dwSize != sizeof(dwValue) ||
                    dwValue < 96 ||
                    dwValue > 480)
                {
                    dwValue = 96;
                }

                RegCloseKey(hKey);
            }

            for (;;)
            {
                TCHAR Buffer[LINE_LEN];
                TCHAR Desc[LINE_LEN];

                if (SetupGetStringField(&Context, 0, Buffer, sizeof(Buffer) / sizeof(TCHAR), NULL) &&
                    SetupGetIntField(&Context, 1, &ci))
                {
                    _stprintf(Desc, _T("%s (%d DPI)"), Buffer, ci);
                    i = SendMessage(hFontSize, CB_ADDSTRING, 0, (LPARAM)Desc);
                    if (i != CB_ERR)
                    {
                        SendMessage(hFontSize, CB_SETITEMDATA, (WPARAM)i, (LPARAM)ci);

                        if ((int)dwValue == ci)
                        {
                            iSelected = i;
                            SetWindowText(GetDlgItem(hWnd, IDC_FONTSIZE_CUSTOM), Desc);
                        }
                    }
                }

                if (!SetupFindNextLine(&Context, &Context))
                {
                    break;
                }
            }

            SendMessage(hFontSize, CB_SETCURSEL, (WPARAM)((iSelected >= 0) ? iSelected : 0), 0);
            if (iSelected < 0)
            {
                TCHAR Desc[LINE_LEN];

                if (SendMessage(hFontSize, CB_GETLBTEXT, 0, (LPARAM)Desc) != CB_ERR)
                {
                    SetWindowText(GetDlgItem(hWnd, IDC_FONTSIZE_CUSTOM), Desc);
                }
            }
        }

        SetupCloseInfFile(hInf);
    }
}

static VOID
InitRadioButtons(HWND hWnd)
{
    HKEY hKey;

    if (RegOpenKeyEx(HKEY_CURRENT_USER,
                     _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Controls Folder\\Display"),
                     0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        TCHAR szBuf[64];
        DWORD dwSize = 64;

        if (RegQueryValueEx(hKey, _T("DynaSettingsChange"), 0, NULL,
                            (LPBYTE)szBuf, &dwSize) == ERROR_SUCCESS)
        {
            switch (_ttoi(szBuf))
            {
                case 0:
                    SendDlgItemMessage(hWnd, IDC_RESTART_RB, BM_SETCHECK, 1, 1);
                    break;
                case 1:
                    SendDlgItemMessage(hWnd, IDC_WITHOUTREBOOT_RB, BM_SETCHECK, 1, 1);
                    break;
                case 3:
                    SendDlgItemMessage(hWnd, IDC_ASKME_RB, BM_SETCHECK, 1, 1);
                    break;
            }
        }
        else
            SendDlgItemMessage(hWnd, IDC_WITHOUTREBOOT_RB, BM_SETCHECK, 1, 1);

        RegCloseKey(hKey);
    }
    else
        SendDlgItemMessage(hWnd, IDC_WITHOUTREBOOT_RB, BM_SETCHECK, 1, 1);
}

INT_PTR CALLBACK
AdvGeneralPageProc(HWND hwndDlg,
                   UINT uMsg,
                   WPARAM wParam,
                   LPARAM lParam)
{
    PDISPLAY_DEVICE_ENTRY DispDevice = NULL;
    INT_PTR Ret = 0;

    if (uMsg != WM_INITDIALOG)
        DispDevice = (PDISPLAY_DEVICE_ENTRY)GetWindowLongPtr(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            DispDevice = (PDISPLAY_DEVICE_ENTRY)(((LPPROPSHEETPAGE)lParam)->lParam);
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)DispDevice);

            InitFontSizeList(hwndDlg);
            InitRadioButtons(hwndDlg);

            Ret = TRUE;
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_FONTSIZE_COMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        TCHAR Desc[LINE_LEN];
                        LRESULT iSelection;

                        iSelection = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
                        if (iSelection != CB_ERR &&
                            SendMessage((HWND)lParam,
                                        CB_GETLBTEXT,
                                        (WPARAM)iSelection,
                                        (LPARAM)Desc) != CB_ERR)
                        {
                            SetWindowText(GetDlgItem(hwndDlg, IDC_FONTSIZE_CUSTOM), Desc);
                        }
                        PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                    }
                    break;
                case IDC_RESTART_RB:
                case IDC_WITHOUTREBOOT_RB:
                case IDC_ASKME_RB:
                    PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                break;
            }
            break;

        case WM_NOTIFY:
            if (((LPNMHDR)lParam)->code == PSN_APPLY)
            {
                BOOL bChanged;

                if (!SaveDpi(hwndDlg, &bChanged))
                {
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                    return TRUE;
                }

                PropSheet_UnChanged(GetParent(hwndDlg), hwndDlg);
                if (bChanged)
                    PropSheet_RestartWindows(GetParent(hwndDlg));
                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
    }

    return Ret;
}
