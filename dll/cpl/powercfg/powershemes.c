/*
 * PROJECT:     ReactOS Power Configuration Applet
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Power Schemes tab
 * COPYRIGHT:   Copyright 2006 Alexander Wurzinger <lohnegrim@gmx.net>
 *              Copyright 2006 Johannes Anderwald <johannes.anderwald@reactos.org>
 *              Copyright 2006 Martin Rottensteiner <2005only@pianonote.at>
 *              Copyright 2007 Dmitry Chapyshev <lentind@yandex.ru>
 *              Copyright 2019 Eric Kohl <eric.kohl@reactos.org>
 */

#include "powercfg.h"

typedef struct _POWER_SCHEME
{
    LIST_ENTRY ListEntry;
    UINT uId;
    LPTSTR pszName;
    LPTSTR pszDescription;
    POWER_POLICY PowerPolicy;
} POWER_SCHEME, *PPOWER_SCHEME;


typedef struct _POWER_SCHEMES_PAGE_DATA
{
    LIST_ENTRY PowerSchemesList;
    PPOWER_SCHEME pActivePowerScheme;
    PPOWER_SCHEME pSelectedPowerScheme;
} POWER_SCHEMES_PAGE_DATA, *PPOWER_SCHEMES_PAGE_DATA;


typedef struct _SAVE_POWER_SCHEME_DATA
{
    PPOWER_SCHEMES_PAGE_DATA pPageData;
    PPOWER_SCHEME pNewScheme;
    HWND hwndPage;
    POWER_POLICY PowerPolicy;
} SAVE_POWER_SCHEME_DATA, *PSAVE_POWER_SCHEME_DATA;


UINT Sec[]=
{
    60,
    120,
    180,
    300,
    600,
    900,
    1200,
    1500,
    1800,
    2700,
    3600,
    7200,
    10800,
    14400,
    18000,
    0
};


static
PPOWER_SCHEME
AddPowerScheme(
    PPOWER_SCHEMES_PAGE_DATA pPageData,
    UINT uId,
    DWORD dwName,
    LPTSTR pszName,
    DWORD dwDescription,
    LPWSTR pszDescription,
    PPOWER_POLICY pp)
{
    PPOWER_SCHEME pScheme;
    BOOL bResult = FALSE;

    if (dwName < sizeof(TCHAR) || pszName == NULL ||
        (dwDescription != 0 && (dwDescription < sizeof(TCHAR) || pszDescription == NULL)))
        return NULL;

    pScheme = HeapAlloc(GetProcessHeap(),
                        HEAP_ZERO_MEMORY,
                        sizeof(POWER_SCHEME));
    if (pScheme == NULL)
        return NULL;

    pScheme->uId = uId;
    CopyMemory(&pScheme->PowerPolicy, pp, sizeof(POWER_POLICY));

    if (dwName != 0)
    {
        pScheme->pszName = HeapAlloc(GetProcessHeap(),
                                     HEAP_ZERO_MEMORY,
                                     dwName);
        if (pScheme->pszName == NULL)
            goto done;

        if (FAILED(StringCchCopy(pScheme->pszName, dwName / sizeof(TCHAR), pszName)))
            goto done;
    }

    if (dwDescription != 0)
    {
        pScheme->pszDescription = HeapAlloc(GetProcessHeap(),
                                            HEAP_ZERO_MEMORY,
                                            dwDescription);
        if (pScheme->pszDescription == NULL)
            goto done;

        if (FAILED(StringCchCopy(pScheme->pszDescription,
                                 dwDescription / sizeof(TCHAR), pszDescription)))
            goto done;
    }

    InsertTailList(&pPageData->PowerSchemesList, &pScheme->ListEntry);
    bResult = TRUE;

done:
    if (bResult == FALSE)
    {
        if (pScheme->pszName)
            HeapFree(GetProcessHeap(), 0, pScheme->pszName);

        if (pScheme->pszDescription)
            HeapFree(GetProcessHeap(), 0, pScheme->pszDescription);

        HeapFree(GetProcessHeap(), 0, pScheme);
        pScheme = NULL;
    }

    return pScheme;
}


static
VOID
DeletePowerScheme(
    PPOWER_SCHEME pScheme)
{
    RemoveEntryList(&pScheme->ListEntry);

    if (pScheme->pszName)
        HeapFree(GetProcessHeap(), 0, pScheme->pszName);

    if (pScheme->pszDescription)
        HeapFree(GetProcessHeap(), 0, pScheme->pszDescription);

    HeapFree(GetProcessHeap(), 0, pScheme);
}


static
BOOLEAN
CALLBACK
EnumPowerSchemeCallback(
    UINT uiIndex,
    DWORD dwName,
    LPTSTR pszName,
    DWORD dwDesc,
    LPWSTR pszDesc,
    PPOWER_POLICY pp,
    LPARAM lParam)
{
    if (ValidatePowerPolicies(0, pp))
    {
        return AddPowerScheme((PPOWER_SCHEMES_PAGE_DATA)lParam,
                              uiIndex,
                              dwName,
                              pszName,
                              dwDesc,
                              pszDesc,
                              pp) != NULL;
    }

    return TRUE;
}

static
VOID
BuildSchemesList(
    PPOWER_SCHEMES_PAGE_DATA pPageData)
{
    InitializeListHead(&pPageData->PowerSchemesList);

    EnumPwrSchemes(EnumPowerSchemeCallback, (LPARAM)pPageData);
}


static
VOID
DestroySchemesList(
    PPOWER_SCHEMES_PAGE_DATA pPageData)
{
    PLIST_ENTRY ListEntry;
    PPOWER_SCHEME pScheme;

    for (;;)
    {
        ListEntry = pPageData->PowerSchemesList.Flink;
        if (ListEntry == &pPageData->PowerSchemesList)
            break;

        pScheme = CONTAINING_RECORD(ListEntry, POWER_SCHEME, ListEntry);
        DeletePowerScheme(pScheme);
    }

    pPageData->pActivePowerScheme = NULL;
    pPageData->pSelectedPowerScheme = NULL;
}


BOOLEAN
Pos_InitData(
    HWND hwndDlg)
{
    SYSTEM_POWER_CAPABILITIES spc;

    if (!GetPwrCapabilities(&spc))
        return FALSE;

    ShowWindow(GetDlgItem(hwndDlg, IDC_STANDBY),
               IS_PWR_SUSPEND_ALLOWED(&spc) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwndDlg, IDC_STANDBYACLIST),
               IS_PWR_SUSPEND_ALLOWED(&spc) ? SW_SHOW : SW_HIDE);
    if (spc.SystemBatteriesPresent)
    {
        ShowWindow(GetDlgItem(hwndDlg, IDC_STANDBYDCLIST),
                   IS_PWR_SUSPEND_ALLOWED(&spc) ? SW_SHOW : SW_HIDE);
    }

    ShowWindow(GetDlgItem(hwndDlg, IDC_HIBERNATE),
               IS_PWR_HIBERNATE_ALLOWED(&spc) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwndDlg, IDC_HIBERNATEACLIST),
               IS_PWR_HIBERNATE_ALLOWED(&spc) ? SW_SHOW : SW_HIDE);
    if (spc.SystemBatteriesPresent)
    {
        ShowWindow(GetDlgItem(hwndDlg, IDC_HIBERNATEDCLIST),
                   IS_PWR_HIBERNATE_ALLOWED(&spc) ? SW_SHOW : SW_HIDE);
    }

    return TRUE;
}


static
VOID
Pos_SelectTimeout(
    HWND hwndCtrl,
    ULONG Timeout)
{
    LRESULT Count, Index, Value;

    if (hwndCtrl == NULL)
        return;

    Count = SendMessage(hwndCtrl, CB_GETCOUNT, 0, 0);
    for (Index = 0; Index < Count; Index++)
    {
        Value = SendMessage(hwndCtrl, CB_GETITEMDATA, Index, 0);
        if (Value != CB_ERR && (ULONG)Value == Timeout)
            break;
    }

    SendMessage(hwndCtrl, CB_SETCURSEL, Index < Count ? Index : -1, 0);
}


static
VOID
LoadConfig(
    HWND hwndDlg,
    PPOWER_SCHEMES_PAGE_DATA pPageData,
    PPOWER_SCHEME pScheme)
{
    INT iCurSel = 0;
    TCHAR szTemp[MAX_PATH];
    TCHAR szConfig[MAX_PATH];
    PPOWER_POLICY pp;

    iCurSel = (INT)SendDlgItemMessage(hwndDlg,
                                          IDC_ENERGYLIST,
                                          CB_GETCURSEL,
                                          0,
                                          0);
    if (iCurSel == CB_ERR)
        return;

    EnableWindow(GetDlgItem(hwndDlg, IDC_DELETE_BTN),
                (iCurSel > 0));

    if (pScheme == NULL)
    {
        pScheme = (PPOWER_SCHEME)SendDlgItemMessage(hwndDlg,
                                                    IDC_ENERGYLIST,
                                                    CB_GETITEMDATA,
                                                    (WPARAM)iCurSel,
                                                    0);
        if (pScheme == NULL || pScheme == (PPOWER_SCHEME)CB_ERR)
            return;
    }

    pPageData->pSelectedPowerScheme = pScheme;

    if (LoadString(hApplet, IDS_CONFIG1, szTemp, _countof(szTemp)))
    {
        if (SUCCEEDED(StringCchPrintf(szConfig, _countof(szConfig), szTemp, pScheme->pszName)))
            SetWindowText(GetDlgItem(hwndDlg, IDC_GRPDETAIL), szConfig);
        else
            SetWindowText(GetDlgItem(hwndDlg, IDC_GRPDETAIL), pScheme->pszName);
    }

    pp = &pScheme->PowerPolicy;

    Pos_SelectTimeout(GetDlgItem(hwndDlg, IDC_MONITORACLIST), pp->user.VideoTimeoutAc);
    Pos_SelectTimeout(GetDlgItem(hwndDlg, IDC_MONITORDCLIST), pp->user.VideoTimeoutDc);
    Pos_SelectTimeout(GetDlgItem(hwndDlg, IDC_DISKACLIST), pp->user.SpindownTimeoutAc);
    Pos_SelectTimeout(GetDlgItem(hwndDlg, IDC_DISKDCLIST), pp->user.SpindownTimeoutDc);
    Pos_SelectTimeout(GetDlgItem(hwndDlg, IDC_STANDBYACLIST), pp->user.IdleTimeoutAc);
    Pos_SelectTimeout(GetDlgItem(hwndDlg, IDC_STANDBYDCLIST), pp->user.IdleTimeoutDc);
    Pos_SelectTimeout(GetDlgItem(hwndDlg, IDC_HIBERNATEACLIST), pp->mach.DozeS4TimeoutAc);
    Pos_SelectTimeout(GetDlgItem(hwndDlg, IDC_HIBERNATEDCLIST), pp->mach.DozeS4TimeoutDc);
}


static VOID
Pos_InitPage(HWND hwndDlg)
{
    int ifrom = 0, i = 0, imin = 0;
    HWND hwnd = NULL;
    TCHAR szName[MAX_PATH];
    LRESULT index;

    for (i = 1; i < 9; i++)
    {
        switch (i)
        {
            case 1:
                hwnd = GetDlgItem(hwndDlg, IDC_MONITORACLIST);
                imin = IDS_TIMEOUT1;
                break;

            case 2:
                hwnd = GetDlgItem(hwndDlg, IDC_STANDBYACLIST);
                imin = IDS_TIMEOUT1;
                break;

            case 3:
                hwnd = GetDlgItem(hwndDlg, IDC_DISKACLIST);
                imin = IDS_TIMEOUT3;
                break;

            case 4:
                hwnd = GetDlgItem(hwndDlg, IDC_HIBERNATEACLIST);
                imin = IDS_TIMEOUT3;
                break;

            case 5:
                hwnd = GetDlgItem(hwndDlg, IDC_MONITORDCLIST);
                imin = IDS_TIMEOUT1;
                break;

            case 6:
                hwnd = GetDlgItem(hwndDlg, IDC_STANDBYDCLIST);
                imin = IDS_TIMEOUT1;
                break;

            case 7:
                hwnd = GetDlgItem(hwndDlg, IDC_DISKDCLIST);
                imin = IDS_TIMEOUT3;
                break;

            case 8:
                hwnd = GetDlgItem(hwndDlg, IDC_HIBERNATEDCLIST);
                imin = IDS_TIMEOUT3;
                break;

            default:
                hwnd = NULL;
                return;
        }

        if (hwnd == NULL)
            continue;

        for (ifrom = imin; ifrom < (IDS_TIMEOUT15 + 1); ifrom++)
        {
            if (LoadString(hApplet, ifrom, szName, _countof(szName)))
            {
                index = SendMessage(hwnd,
                                     CB_ADDSTRING,
                                     0,
                                    (LPARAM)szName);
                if (index == CB_ERR || index == CB_ERRSPACE)
                    return;

                if (SendMessage(hwnd,
                                CB_SETITEMDATA,
                                index,
                                (LPARAM)Sec[ifrom - IDS_TIMEOUT1]) == CB_ERR)
                {
                    SendMessage(hwnd, CB_DELETESTRING, index, 0);
                    return;
                }
            }
        }

        if (LoadString(hApplet, IDS_TIMEOUT16, szName, _countof(szName)))
        {
            index = SendMessage(hwnd,
                                 CB_ADDSTRING,
                                 0,
                                 (LPARAM)szName);
            if (index == CB_ERR || index == CB_ERRSPACE)
                return;

            if (SendMessage(hwnd,
                            CB_SETITEMDATA,
                            index,
                            (LPARAM)Sec[_countof(Sec) - 1]) == CB_ERR)
            {
                SendMessage(hwnd, CB_DELETESTRING, index, 0);
                return;
            }
        }
    }
}


static VOID
Pos_SaveTimeout(
    HWND hwndCtrl,
    PULONG Timeout)
{
    LRESULT Index, Value;

    if (hwndCtrl == NULL)
        return;

    Index = SendMessage(hwndCtrl, CB_GETCURSEL, 0, 0);
    if (Index == CB_ERR)
        return;

    Value = SendMessage(hwndCtrl, CB_GETITEMDATA, Index, 0);
    if (Value != CB_ERR)
        *Timeout = (ULONG)Value;
}


static VOID
Pos_ReadData(
    HWND hwndDlg,
    PPOWER_POLICY pp)
{
    Pos_SaveTimeout(GetDlgItem(hwndDlg, IDC_MONITORACLIST), &pp->user.VideoTimeoutAc);
    Pos_SaveTimeout(GetDlgItem(hwndDlg, IDC_MONITORDCLIST), &pp->user.VideoTimeoutDc);
    Pos_SaveTimeout(GetDlgItem(hwndDlg, IDC_DISKACLIST), &pp->user.SpindownTimeoutAc);
    Pos_SaveTimeout(GetDlgItem(hwndDlg, IDC_DISKDCLIST), &pp->user.SpindownTimeoutDc);
    Pos_SaveTimeout(GetDlgItem(hwndDlg, IDC_STANDBYACLIST), &pp->user.IdleTimeoutAc);
    Pos_SaveTimeout(GetDlgItem(hwndDlg, IDC_STANDBYDCLIST), &pp->user.IdleTimeoutDc);
    Pos_SaveTimeout(GetDlgItem(hwndDlg, IDC_HIBERNATEACLIST), &pp->mach.DozeS4TimeoutAc);
    Pos_SaveTimeout(GetDlgItem(hwndDlg, IDC_HIBERNATEDCLIST), &pp->mach.DozeS4TimeoutDc);

}


static BOOL
Pos_SaveData(
    HWND hwndDlg,
    PPOWER_SCHEMES_PAGE_DATA pPageData)
{
    PPOWER_SCHEME pScheme;
    POWER_POLICY PowerPolicy;

    if (pPageData == NULL || pPageData->pSelectedPowerScheme == NULL)
        return FALSE;

    pScheme = pPageData->pSelectedPowerScheme;
    PowerPolicy = pScheme->PowerPolicy;
    Pos_ReadData(hwndDlg, &PowerPolicy);

    if (!SetActivePwrScheme(pScheme->uId, NULL, &PowerPolicy))
        return FALSE;

    pScheme->PowerPolicy = PowerPolicy;
    pPageData->pActivePowerScheme = pScheme;
    return TRUE;
}


static INT
FindPowerSchemeIndex(
    HWND hwndList,
    PPOWER_SCHEME pScheme)
{
    INT Count, Index;

    if (pScheme == NULL)
        return CB_ERR;

    Count = (INT)SendMessage(hwndList, CB_GETCOUNT, 0, 0);
    for (Index = 0; Index < Count; Index++)
    {
        if ((PPOWER_SCHEME)SendMessage(hwndList, CB_GETITEMDATA, Index, 0) == pScheme)
            return Index;
    }

    return CB_ERR;
}


static
BOOL
DelScheme(
    HWND hwnd,
    PPOWER_SCHEMES_PAGE_DATA pPageData)
{
    WCHAR szTitleBuffer[256];
    WCHAR szRawBuffer[256], szCookedBuffer[512];
    INT iCurSel;
    HWND hList;
    PPOWER_SCHEME pScheme;
    WCHAR szErrorText[512];

    hList = GetDlgItem(hwnd, IDC_ENERGYLIST);

    iCurSel = SendMessage(hList, CB_GETCURSEL, 0, 0);
    if (iCurSel == CB_ERR)
        return FALSE;

    SendMessage(hList, CB_SETCURSEL, iCurSel, 0);

    pScheme = (PPOWER_SCHEME)SendMessage(hList, CB_GETITEMDATA, (WPARAM)iCurSel, 0);
    if (pScheme == NULL || pScheme == (PPOWER_SCHEME)CB_ERR)
        return FALSE;

    if (!LoadStringW(hApplet, IDS_DEL_SCHEME_TITLE, szTitleBuffer, _countof(szTitleBuffer)) ||
        !LoadStringW(hApplet, IDS_DEL_SCHEME, szRawBuffer, _countof(szRawBuffer)) ||
        FAILED(StringCchPrintfW(szCookedBuffer, _countof(szCookedBuffer), szRawBuffer, pScheme->pszName)))
        return FALSE;

    if (MessageBoxW(hwnd, szCookedBuffer, szTitleBuffer, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
    {
        if (!DeletePwrScheme(pScheme->uId))
        {
            if (LoadStringW(hApplet, IDS_DEL_SCHEME_ERROR, szErrorText, _countof(szErrorText)))
                MessageBoxW(NULL, szErrorText, NULL, MB_OK | MB_ICONERROR);
            return FALSE;
        }

        iCurSel = FindPowerSchemeIndex(hList, pScheme);
        if (iCurSel == CB_ERR || SendMessage(hList, CB_DELETESTRING, iCurSel, 0) == CB_ERR)
            return FALSE;

        if (pPageData->pSelectedPowerScheme == pScheme)
            pPageData->pSelectedPowerScheme = NULL;
        if (pPageData->pActivePowerScheme == pScheme)
            pPageData->pActivePowerScheme = NULL;

        DeletePowerScheme(pScheme);

        iCurSel = FindPowerSchemeIndex(hList, pPageData->pActivePowerScheme);
        if (iCurSel == CB_ERR)
            iCurSel = 0;

        if (SendMessage(hList, CB_SETCURSEL, iCurSel, 0) == CB_ERR)
        {
            EnableWindow(GetDlgItem(hwnd, IDC_DELETE_BTN), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_SAVEAS_BTN), FALSE);
        }

        LoadConfig(hwnd, pPageData, NULL);
        return TRUE;
    }

    return FALSE;
}


static
BOOL
SavePowerScheme(
    HWND hwndDlg,
    PSAVE_POWER_SCHEME_DATA pSaveSchemeData)
{
    PPOWER_SCHEMES_PAGE_DATA pPageData;
    PPOWER_SCHEME pScheme;
    TCHAR szSchemeName[512];
    BOOL bRet = FALSE;

    pPageData = pSaveSchemeData->pPageData;

    if (!GetDlgItemText(hwndDlg, IDC_SCHEMENAME, szSchemeName, _countof(szSchemeName)))
        return FALSE;

    pScheme = AddPowerScheme(pPageData,
                             -1,
                             (_tcslen(szSchemeName) + 1) * sizeof(TCHAR),
                             szSchemeName,
                             sizeof(TCHAR),
                             TEXT(""),
                             &pSaveSchemeData->PowerPolicy);
    if (pScheme != NULL)
    {
        if (WritePwrScheme(&pScheme->uId,
                           pScheme->pszName,
                           pScheme->pszDescription,
                           &pScheme->PowerPolicy))
        {
            pSaveSchemeData->pNewScheme = pScheme;
            bRet = TRUE;
        }
        else
        {
            DeletePowerScheme(pScheme);
        }
    }

    return bRet;
}


INT_PTR
CALLBACK
SaveSchemeDlgProc(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    PSAVE_POWER_SCHEME_DATA pSaveSchemeData;

    pSaveSchemeData = (PSAVE_POWER_SCHEME_DATA)GetWindowLongPtr(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pSaveSchemeData = (PSAVE_POWER_SCHEME_DATA)lParam;
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)pSaveSchemeData);

            SetDlgItemText(hwndDlg,
                           IDC_SCHEMENAME,
                           pSaveSchemeData->pPageData->pSelectedPowerScheme->pszName);
            return TRUE;

        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
                case IDOK:
                    EndDialog(hwndDlg, SavePowerScheme(hwndDlg, pSaveSchemeData));
                    break;

                case IDCANCEL:
                    EndDialog(hwndDlg, FALSE);
                    break;
            }
            break;
    }

    return FALSE;
}


static
VOID
SaveScheme(
    HWND hwndDlg,
    PPOWER_SCHEMES_PAGE_DATA pPageData)
{
    SAVE_POWER_SCHEME_DATA SaveSchemeData;
    HWND hwndList;
    INT index;

    if (pPageData->pSelectedPowerScheme == NULL)
        return;

    SaveSchemeData.pPageData = pPageData;
    SaveSchemeData.pNewScheme = NULL;
    SaveSchemeData.hwndPage = hwndDlg;

    SaveSchemeData.PowerPolicy = pPageData->pSelectedPowerScheme->PowerPolicy;
    Pos_ReadData(hwndDlg, &SaveSchemeData.PowerPolicy);

    if (DialogBoxParam(hApplet,
                       MAKEINTRESOURCE(IDD_SAVEPOWERSCHEME),
                       hwndDlg,
                       SaveSchemeDlgProc,
                       (LPARAM)&SaveSchemeData) == TRUE)
    {
        if (SaveSchemeData.pNewScheme)
        {
            hwndList = GetDlgItem(hwndDlg, IDC_ENERGYLIST);

            index = (INT)SendMessage(hwndList,
                                     CB_ADDSTRING,
                                     0,
                                     (LPARAM)SaveSchemeData.pNewScheme->pszName);
            if (index == CB_ERR || index == CB_ERRSPACE)
                return;

            if (SendMessage(hwndList,
                            CB_SETITEMDATA,
                            index,
                            (LPARAM)SaveSchemeData.pNewScheme) == CB_ERR)
            {
                SendMessage(hwndList, CB_DELETESTRING, index, 0);
                return;
            }

            if (SendMessage(hwndList, CB_SETCURSEL, (WPARAM)index, 0) == CB_ERR)
                return;

            LoadConfig(hwndDlg, pPageData, SaveSchemeData.pNewScheme);
            PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
        }
    }
}


static BOOL
CreateEnergyList(
    HWND hwndDlg,
    PPOWER_SCHEMES_PAGE_DATA pPageData)
{
    PLIST_ENTRY ListEntry;
    PPOWER_SCHEME pScheme;
    INT index;
    POWER_POLICY pp;
    SYSTEM_POWER_CAPABILITIES spc;
    HWND hwndList;
    UINT aps = 0;

    hwndList = GetDlgItem(hwndDlg, IDC_ENERGYLIST);

    if (!GetActivePwrScheme(&aps))
        return FALSE;

    if (!ReadGlobalPwrPolicy(&gGPP))
        return FALSE;

    if (!ReadPwrScheme(aps, &pp))
        return FALSE;

    if (!ValidatePowerPolicies(&gGPP, 0))
        return FALSE;

/*
    if (!SetActivePwrScheme(aps, &gGPP, &pp))
        return FALSE;
*/

    if (!GetPwrCapabilities(&spc))
        return FALSE;

    if (CanUserWritePwrScheme())
    {
        // TODO:
        // Enable write / delete powerscheme button
    }

    Pos_InitPage(hwndDlg);

    if (!GetActivePwrScheme(&aps))
        return FALSE;

    ListEntry = pPageData->PowerSchemesList.Flink;
    while (ListEntry != &pPageData->PowerSchemesList)
    {
        pScheme = CONTAINING_RECORD(ListEntry, POWER_SCHEME, ListEntry);

        index = (int)SendMessage(hwndList,
                                 CB_ADDSTRING,
                                 0,
                                 (LPARAM)pScheme->pszName);
        if (index == CB_ERR || index == CB_ERRSPACE)
            return FALSE;

        if (SendMessage(hwndList,
                        CB_SETITEMDATA,
                        index,
                        (LPARAM)pScheme) == CB_ERR)
        {
            SendMessage(hwndList, CB_DELETESTRING, index, 0);
            return FALSE;
        }

        if (aps == pScheme->uId)
        {
            if (SendMessage(hwndList, CB_SETCURSEL, index, 0) == CB_ERR)
                return FALSE;

            pPageData->pActivePowerScheme = pScheme;
            LoadConfig(hwndDlg, pPageData, pScheme);
        }

        ListEntry = ListEntry->Flink;
    }

    if (SendMessage(hwndList, CB_GETCOUNT, 0, 0) > 0)
    {
        EnableWindow(GetDlgItem(hwndDlg, IDC_SAVEAS_BTN), TRUE);
    }

    return TRUE;
}


/* Property page dialog callback */
INT_PTR CALLBACK
PowerSchemesDlgProc(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    PPOWER_SCHEMES_PAGE_DATA pPageData;

    pPageData = (PPOWER_SCHEMES_PAGE_DATA)GetWindowLongPtr(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pPageData = (PPOWER_SCHEMES_PAGE_DATA)HeapAlloc(GetProcessHeap(),
                                                            HEAP_ZERO_MEMORY,
                                                            sizeof(POWER_SCHEMES_PAGE_DATA));
            if (pPageData == NULL)
            {
                EnableWindow(hwndDlg, FALSE);
                return TRUE;
            }
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)pPageData);

            BuildSchemesList(pPageData);

            if (!Pos_InitData(hwndDlg))
            {
                // TODO:
                // Initialization failed
                // Handle error
                MessageBox(hwndDlg,_T("Pos_InitData failed\n"), NULL, MB_OK);
            }

            if (!CreateEnergyList(hwndDlg, pPageData))
            {
                // TODO:
                // Initialization failed
                // Handle error
                MessageBox(hwndDlg,_T("CreateEnergyList failed\n"), NULL, MB_OK);
            }
            return TRUE;

        case WM_DESTROY:
            if (pPageData)
            {
                DestroySchemesList(pPageData);
                HeapFree(GetProcessHeap(), 0, pPageData);
                SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)NULL);
            }
            break;

        case WM_COMMAND:
            if (pPageData == NULL)
                return FALSE;

            switch(LOWORD(wParam))
            {
                case IDC_ENERGYLIST:
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        LoadConfig(hwndDlg, pPageData, NULL);
                        PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                    }
                    break;

                case IDC_DELETE_BTN:
                    DelScheme(hwndDlg, pPageData);
                    break;

                case IDC_SAVEAS_BTN:
                    SaveScheme(hwndDlg, pPageData);
                    break;

                case IDC_MONITORACLIST:
                case IDC_MONITORDCLIST:
                case IDC_DISKACLIST:
                case IDC_DISKDCLIST:
                case IDC_STANDBYACLIST:
                case IDC_STANDBYDCLIST:
                case IDC_HIBERNATEACLIST:
                case IDC_HIBERNATEDCLIST:
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                    }
                    break;
            }
            break;

        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->code)
            {
                case PSN_APPLY:
                    Pos_SaveData(hwndDlg, pPageData);
                    return TRUE;

                case PSN_SETACTIVE:
                    Pos_InitData(hwndDlg);
                    return TRUE;
            }
            break;
    }

    return FALSE;
}
