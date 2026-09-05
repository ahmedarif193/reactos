/*
 * PROJECT:     ReactOS Control Panel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     In-process Control Panel hub pages: System, Power Options,
 *              Network and Sharing Center, Personalization
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "control11.h"
#include <powersetting.h>
#include <netlistmgr.h>
#include <iphlpapi.h>
#include <lm.h>
#include <netcon.h>

static const GUID GUID_VIDEO_SUBGROUP = { 0x7516B95F, 0xF776, 0x4464, { 0x8C, 0x53, 0x06, 0x16, 0x7F, 0x40, 0xCC, 0x99 } };
static const GUID GUID_VIDEO_POWERDOWN_TIMEOUT = { 0x3C0BC021, 0xC8A8, 0x4E07, { 0xA9, 0x73, 0x6B, 0x14, 0xCB, 0xCB, 0x2B, 0x7E } };
static const GUID GUID_SLEEP_SUBGROUP = { 0x238C9FA8, 0x0AAD, 0x41ED, { 0x83, 0xF4, 0x97, 0xBE, 0x24, 0x2C, 0x8F, 0x20 } };
static const GUID GUID_STANDBY_TIMEOUT = { 0x29F6C1DB, 0x86DA, 0x48C5, { 0x9F, 0xDB, 0xF2, 0xB6, 0x7B, 0x1F, 0x44, 0xDA } };

#define IDC_PLAN_DISPLAY_AC 3001
#define IDC_PLAN_DISPLAY_DC 3002
#define IDC_PLAN_SLEEP_AC   3003
#define IDC_PLAN_SLEEP_DC   3004
#define IDC_PLAN_SAVE       3005
#define IDC_PLAN_CANCEL     3006

#define CUSTOM_SETPLAN      1
#define CUSTOM_NETCATEGORY  2
#define CUSTOM_APPLYTHEME   3
#define CUSTOM_EDITPLAN     4
#define CUSTOM_MOREPLANS    5

typedef struct _PWRPLAN
{
    GUID guid;
    WCHAR szName[128];
    WCHAR szDescription[256];
} PWRPLAN;

static PWRPLAN s_Plans[16];
static int s_cPlans;
static GUID s_ActivePlan;
static GUID s_EditPlan;
static HWND s_hwndCombo[4];
static HWND s_hwndSave, s_hwndCancel;
static RECT s_rcCombo[4], s_rcSave, s_rcCancel;
static BOOL s_bPlanControlsVisible;
static BOOL s_bHasBattery;
static BOOL s_bShowMorePlans;

static const struct { int minutes; LPCWSTR psz; } s_Timeouts[] =
{
    { 1, L"1 minute" }, { 2, L"2 minutes" }, { 3, L"3 minutes" }, { 5, L"5 minutes" },
    { 10, L"10 minutes" }, { 15, L"15 minutes" }, { 20, L"20 minutes" }, { 25, L"25 minutes" },
    { 30, L"30 minutes" }, { 45, L"45 minutes" }, { 60, L"1 hour" }, { 120, L"2 hours" },
    { 180, L"3 hours" }, { 240, L"4 hours" }, { 300, L"5 hours" }, { 0, L"Never" },
};

LPCWSTR Hub_Canonical(int hub)
{
    switch (hub)
    {
    case HUB_SYSTEM: return L"Microsoft.System";
    case HUB_POWER: return L"Microsoft.PowerOptions";
    case HUB_NETWORK: return L"Microsoft.NetworkAndSharingCenter";
    case HUB_PERSONALIZATION: return L"Microsoft.Personalization";
    }
    return L"";
}

LPCWSTR Hub_Title(int hub, LPCWSTR pszPage)
{
    switch (hub)
    {
    case HUB_SYSTEM: return L"System";
    case HUB_POWER:
        if (pszPage && !lstrcmpiW(pszPage, L"pagePlanSettings")) return L"Edit Plan Settings";
        return L"Power Options";
    case HUB_NETWORK: return L"Network and Sharing Center";
    case HUB_PERSONALIZATION: return L"Personalization";
    }
    return L"Control Panel";
}

static int SectionTitle(HDC hdc, int x, int y, int cx, LPCWSTR psz)
{
    HGDIOBJ oldF = SelectObject(hdc, g_f.hfTitle);
    SetTextColor(hdc, g_f.pal.Header);
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, x, y, psz, lstrlenW(psz));
    SelectObject(hdc, oldF);
    int h = TextH(hdc, g_f.hfTitle);
    HPEN pen = CreatePen(PS_SOLID, 1, g_f.pal.Rule);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, x, y + h + S(4), NULL);
    LineTo(hdc, x + cx, y + h + S(4));
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    return h + S(14);
}

static int PageHeader(HDC hdc, int x, int y, LPCWSTR psz)
{
    HGDIOBJ oldF = SelectObject(hdc, g_f.hfHeader);
    SetTextColor(hdc, g_f.pal.Header);
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, x, y, psz, lstrlenW(psz));
    SelectObject(hdc, oldF);
    return TextH(hdc, g_f.hfHeader) + S(16);
}

static int LabelValue(HDC hdc, int x, int y, int cxLabel, int cx, LPCWSTR pszLabel, LPCWSTR pszValue)
{
    HGDIOBJ oldF = SelectObject(hdc, g_f.hfText);
    SetTextColor(hdc, g_f.pal.DimText);
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, x, y, pszLabel, lstrlenW(pszLabel));
    SelectObject(hdc, oldF);
    int h = DrawWrapped(hdc, x + cxLabel, y, cx - cxLabel, pszValue, g_f.hfText, g_f.pal.Text);
    return max(h, TextH(hdc, g_f.hfText)) + S(4);
}

static void RegString(HKEY hRoot, LPCWSTR pszKey, LPCWSTR pszValue, LPWSTR pszOut, int cch)
{
    DWORD cb = cch * sizeof(WCHAR);
    pszOut[0] = 0;
    if (SHGetValueW(hRoot, pszKey, pszValue, NULL, pszOut, &cb) != ERROR_SUCCESS)
        pszOut[0] = 0;
}

static void LaunchCplPage(LPCWSTR psz)
{
    Catalog_LaunchCpl(g_f.hwnd, psz);
}

/* ------------------------------------------------------------------ */
/*  System                                                             */
/* ------------------------------------------------------------------ */

static int PaintSystem(HDC hdc, int x, int y, int cx)
{
    y += PageHeader(hdc, x, y, L"View basic information about your computer");

    WCHAR szProduct[128], szVersion[64], szBuild[64], szText[256];
    RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName", szProduct, _countof(szProduct));
    RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentVersion", szVersion, _countof(szVersion));
    RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber", szBuild, _countof(szBuild));
    if (!szProduct[0]) StringCchCopyW(szProduct, _countof(szProduct), L"ReactOS");

    y += SectionTitle(hdc, x, y, cx, L"Windows edition");
    int lx = x + S(16);
    int lcx = cx - S(16);
    y += DrawWrapped(hdc, lx, y, lcx, szProduct, g_f.hfBold, g_f.pal.Text) + S(2);
    OSVERSIONINFOEXW osv = { sizeof(osv) };
    GetVersionExW((LPOSVERSIONINFOW)&osv);
    StringCchPrintfW(szText, _countof(szText), L"Version %u.%u (Build %u)%s%s", osv.dwMajorVersion, osv.dwMinorVersion, osv.dwBuildNumber,
                     osv.szCSDVersion[0] ? L" " : L"", osv.szCSDVersion);
    y += DrawWrapped(hdc, lx, y, lcx, szText, g_f.hfText, g_f.pal.Text) + S(2);
    y += DrawWrapped(hdc, lx, y, lcx, L"ReactOS is free software released under the GNU GPL.", g_f.hfText, g_f.pal.DimText) + S(14);

    y += SectionTitle(hdc, x, y, cx, L"System");
    int cxLabel = S(150);
    WCHAR szCpu[256] = L"", szMhz[32] = L"";
    RegString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", szCpu, _countof(szCpu));
    DWORD mhz = 0, cb = sizeof(mhz);
    if (SHGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", NULL, &mhz, &cb) == ERROR_SUCCESS && mhz)
        StringCchPrintfW(szMhz, _countof(szMhz), L"   %u.%02u GHz", mhz / 1000, (mhz % 1000) / 10);
    if (!szCpu[0]) RegString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"Identifier", szCpu, _countof(szCpu));
    StrTrimW(szCpu, L" ");
    StringCchCatW(szCpu, _countof(szCpu), szMhz);
    HGDIOBJ oldRating = SelectObject(hdc, g_f.hfText);
    SetTextColor(hdc, g_f.pal.DimText);
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, lx, y, L"Rating:", 7);
    SelectObject(hdc, oldRating);
    y += DrawWrapped(hdc, lx + cxLabel, y, lcx - cxLabel, L"System rating is not available", g_f.hfText, g_f.pal.Disabled) + S(4);
    y += LabelValue(hdc, lx, y, cxLabel, lcx, L"Processor:", szCpu);

    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    ULONGLONG mb = ms.ullTotalPhys / (1024 * 1024);
    if (mb >= 1024)
        StringCchPrintfW(szText, _countof(szText), L"%u.%02u GB", (UINT)(mb / 1024), (UINT)((mb % 1024) * 100 / 1024));
    else
        StringCchPrintfW(szText, _countof(szText), L"%u MB", (UINT)mb);
    y += LabelValue(hdc, lx, y, cxLabel, lcx, L"Installed memory (RAM):", szText);

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    LPCWSTR pszType = L"32-bit Operating System";
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) pszType = L"64-bit Operating System, x64-based processor";
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) pszType = L"64-bit Operating System, ARM-based processor";
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM) pszType = L"32-bit Operating System, ARM-based processor";
    y += LabelValue(hdc, lx, y, cxLabel, lcx, L"System type:", pszType);
    StringCchPrintfW(szText, _countof(szText), L"%u", si.dwNumberOfProcessors);
    y += LabelValue(hdc, lx, y, cxLabel, lcx, L"Logical processors:", szText);
    y += LabelValue(hdc, lx, y, cxLabel, lcx, L"Pen and Touch:", L"No Pen or Touch Input is available for this Display");
    y += S(10);

    y += SectionTitle(hdc, x, y, cx, L"Computer name, domain, and workgroup settings");
    WCHAR szName[256] = L"", szFull[256] = L"", szDesc[256] = L"", szGroup[256] = L"";
    DWORD cch = _countof(szName);
    GetComputerNameExW(ComputerNamePhysicalDnsHostname, szName, &cch);
    cch = _countof(szFull);
    GetComputerNameExW(ComputerNameDnsFullyQualified, szFull, &cch);
    StrTrimW(szFull, L".");
    RegString(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\LanmanServer\\Parameters", L"srvcomment", szDesc, _countof(szDesc));
    LPBYTE pBuf = NULL;
    if (NetWkstaGetInfo(NULL, 100, &pBuf) == NERR_Success && pBuf)
    {
        WKSTA_INFO_100 *pInfo = (WKSTA_INFO_100 *)pBuf;
        if (pInfo->wki100_langroup) StringCchCopyW(szGroup, _countof(szGroup), pInfo->wki100_langroup);
        NetApiBufferFree(pBuf);
    }
    if (!szGroup[0]) RegString(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Workstation\\Parameters", L"Workgroup", szGroup, _countof(szGroup));
    if (!szGroup[0]) StringCchCopyW(szGroup, _countof(szGroup), L"WORKGROUP");
    int ty = y;
    y += LabelValue(hdc, lx, y, cxLabel, lcx - S(120), L"Computer name:", szName);
    y += LabelValue(hdc, lx, y, cxLabel, lcx - S(120), L"Full computer name:", szFull[0] ? szFull : szName);
    y += LabelValue(hdc, lx, y, cxLabel, lcx - S(120), L"Computer description:", szDesc);
    y += LabelValue(hdc, lx, y, cxLabel, lcx - S(120), L"Workgroup:", szGroup);
    DrawLinkText(hdc, x + cx - S(100), ty, S(100), L"Change settings", g_f.hfText, LA_COMMAND, 0, NULL,
                 L"%SystemRoot%\\system32\\rundll32.exe shell32.dll,Control_RunDLL sysdm.cpl,,1", L"Change computer name and hardware settings", TRUE);
    y += S(10);
    return y;
}

static void NavSystem(HDC hdc, int x, int *py, int cx)
{
    int y = *py;
    y += DrawLinkText(hdc, x, y, cx, L"Device Manager", g_f.hfText, LA_COMMAND, 0, NULL, L"%SystemRoot%\\system32\\devmgmt.exe", L"View and update your hardware's settings and driver software.", TRUE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Remote settings", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"System protection", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Advanced system settings", g_f.hfText, LA_COMMAND, 0, NULL, L"%SystemRoot%\\system32\\rundll32.exe shell32.dll,Control_RunDLL sysdm.cpl,,2", L"Performance, user profiles, environment variables, startup and recovery.", TRUE) + S(6);
    *py = y;
}

/* ------------------------------------------------------------------ */
/*  Power Options                                                      */
/* ------------------------------------------------------------------ */

static void LoadPlans(void)
{
    s_cPlans = 0;
    GUID *pActive = NULL;
    ZeroMemory(&s_ActivePlan, sizeof(s_ActivePlan));
    if (PowerGetActiveScheme(NULL, &pActive) == ERROR_SUCCESS && pActive)
    {
        s_ActivePlan = *pActive;
        LocalFree(pActive);
    }
    for (ULONG i = 0; s_cPlans < (int)_countof(s_Plans); i++)
    {
        GUID g;
        DWORD cb = sizeof(g);
        if (PowerEnumerate(NULL, NULL, NULL, ACCESS_SCHEME, i, (UCHAR *)&g, &cb) != ERROR_SUCCESS)
            break;
        PWRPLAN *p = &s_Plans[s_cPlans];
        ZeroMemory(p, sizeof(*p));
        p->guid = g;
        cb = sizeof(p->szName);
        if (PowerReadFriendlyName(NULL, &g, NULL, NULL, (UCHAR *)p->szName, &cb) != ERROR_SUCCESS)
            StringFromGUID2(g, p->szName, _countof(p->szName));
        cb = sizeof(p->szDescription);
        PowerReadDescription(NULL, &g, NULL, NULL, (UCHAR *)p->szDescription, &cb);
        s_cPlans++;
    }
    SYSTEM_POWER_STATUS sps;
    s_bHasBattery = GetSystemPowerStatus(&sps) && !(sps.BatteryFlag & 128);
}

static int AdvancedTabIndex(void)
{
    return s_bHasBattery ? 3 : 1;
}

static int PaintPowerPlans(HDC hdc, int x, int y, int cx)
{
    y += PageHeader(hdc, x, y, L"Select a power plan");
    y += DrawWrapped(hdc, x, y, cx, L"Power plans can help you maximize your computer's performance or conserve energy. Make a plan active by selecting it, or choose a plan and customize it by changing its power settings.", g_f.hfText, g_f.pal.Text) + S(14);
    y += SectionTitle(hdc, x, y, cx, s_bHasBattery ? L"Plans shown on the battery meter" : L"Preferred plans");
    for (int i = 0; i < s_cPlans; i++)
    {
        PWRPLAN *p = &s_Plans[i];
        BOOL active = IsEqualGUID(p->guid, s_ActivePlan);
        if (i == 2)
        {
            RECT rExp = { x, y, x + S(200), y + TextH(hdc, g_f.hfText) + S(4) };
            HICON hc = LoadFluent(s_bShowMorePlans ? IDI_NAV_CHEVRON : IDI_NAV_CRUMB, S(12));
            DrawIconAt(hdc, x + S(4), y + S(3), hc, S(12), TRUE);
            DrawLinkText(hdc, x + S(22), y, S(200), s_bShowMorePlans ? L"Hide additional plans" : L"Show additional plans", g_f.hfText, LA_CUSTOM, CUSTOM_MOREPLANS, NULL, L"toggle", NULL, TRUE);
            AddLink(&rExp, LA_CUSTOM, CUSTOM_MOREPLANS, NULL, L"toggle", NULL, TRUE);
            y += TextH(hdc, g_f.hfText) + S(12);
            if (!s_bShowMorePlans)
                break;
        }
        int rx = x + S(8), ry = y + S(2), rs = S(13);
        HPEN pen = CreatePen(PS_SOLID, 1, g_f.pal.DimText);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HBRUSH br = CreateSolidBrush(g_f.pal.Edit);
        HGDIOBJ oldBr = SelectObject(hdc, br);
        Ellipse(hdc, rx, ry, rx + rs, ry + rs);
        SelectObject(hdc, oldBr);
        DeleteObject(br);
        if (active)
        {
            HBRUSH dot = CreateSolidBrush(g_f.pal.Accent);
            SelectObject(hdc, dot);
            SelectObject(hdc, GetStockObject(NULL_PEN));
            Ellipse(hdc, rx + S(3), ry + S(3), rx + rs - S(2), ry + rs - S(2));
            SelectObject(hdc, oldBr);
            DeleteObject(dot);
        }
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        WCHAR szGuid[64];
        StringFromGUID2(p->guid, szGuid, _countof(szGuid));
        RECT rRadio = { rx - S(4), ry - S(4), rx + rs + S(4), ry + rs + S(4) };
        AddLink(&rRadio, LA_CUSTOM, CUSTOM_SETPLAN, NULL, szGuid, p->szDescription, !active);
        int tx = rx + rs + S(10);
        WCHAR szLabel[160];
        StringCchCopyW(szLabel, _countof(szLabel), p->szName);
        if (IsEqualGUID(p->guid, GUID_TYPICAL_POWER_SAVINGS))
            StringCchCatW(szLabel, _countof(szLabel), L" (recommended)");
        HGDIOBJ oldF = SelectObject(hdc, active ? g_f.hfBold : g_f.hfText);
        SetTextColor(hdc, g_f.pal.Text);
        SetBkMode(hdc, TRANSPARENT);
        TextOutW(hdc, tx, y, szLabel, lstrlenW(szLabel));
        SIZE sz;
        GetTextExtentPoint32W(hdc, szLabel, lstrlenW(szLabel), &sz);
        SelectObject(hdc, oldF);
        RECT rName = { tx, y, tx + sz.cx, y + sz.cy };
        AddLink(&rName, LA_CUSTOM, CUSTOM_SETPLAN, NULL, szGuid, p->szDescription, !active);
        DrawLinkText(hdc, x + cx - S(150), y, S(150), L"Change plan settings", g_f.hfText, LA_CUSTOM, CUSTOM_EDITPLAN, NULL, szGuid, NULL, TRUE);
        y += sz.cy + S(2);
        if (p->szDescription[0])
            y += DrawWrapped(hdc, tx, y, cx - (tx - x) - S(160), p->szDescription, g_f.hfSmall, g_f.pal.DimText);
        y += S(12);
    }
    if (!s_cPlans)
        y += DrawWrapped(hdc, x, y, cx, L"No power plans are available.", g_f.hfText, g_f.pal.DimText);
    return y + S(10);
}

static void FillTimeoutCombo(HWND hCombo, DWORD seconds)
{
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    int sel = (int)_countof(s_Timeouts) - 1;
    for (int i = 0; i < (int)_countof(s_Timeouts); i++)
    {
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)s_Timeouts[i].psz);
        if (s_Timeouts[i].minutes && seconds == (DWORD)s_Timeouts[i].minutes * 60) sel = i;
    }
    if (seconds == 0) sel = (int)_countof(s_Timeouts) - 1;
    SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
}

static DWORD ComboSeconds(HWND hCombo)
{
    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)_countof(s_Timeouts)) return 0;
    return (DWORD)s_Timeouts[sel].minutes * 60;
}

static void EnsurePlanControls(void)
{
    if (s_hwndCombo[0]) return;
    HINSTANCE hInst = GetModuleHandleW(NULL);
    static const int ids[4] = { IDC_PLAN_DISPLAY_AC, IDC_PLAN_DISPLAY_DC, IDC_PLAN_SLEEP_AC, IDC_PLAN_SLEEP_DC };
    for (int i = 0; i < 4; i++)
    {
        s_hwndCombo[i] = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                         0, 0, S(150), S(300), g_f.hwnd, (HMENU)(INT_PTR)ids[i], hInst, NULL);
        SendMessageW(s_hwndCombo[i], WM_SETFONT, (WPARAM)g_f.hfText, TRUE);
    }
    s_hwndSave = CreateWindowExW(0, L"BUTTON", L"Save changes", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, S(100), S(24), g_f.hwnd, (HMENU)IDC_PLAN_SAVE, hInst, NULL);
    s_hwndCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, S(80), S(24), g_f.hwnd, (HMENU)IDC_PLAN_CANCEL, hInst, NULL);
    SendMessageW(s_hwndSave, WM_SETFONT, (WPARAM)g_f.hfText, TRUE);
    SendMessageW(s_hwndCancel, WM_SETFONT, (WPARAM)g_f.hfText, TRUE);
}

static void LoadPlanSettings(void)
{
    EnsurePlanControls();
    DWORD v;
    v = 0; PowerReadACValueIndex(NULL, &s_EditPlan, &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT, &v); FillTimeoutCombo(s_hwndCombo[0], v);
    v = 0; PowerReadDCValueIndex(NULL, &s_EditPlan, &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT, &v); FillTimeoutCombo(s_hwndCombo[1], v);
    v = 0; PowerReadACValueIndex(NULL, &s_EditPlan, &GUID_SLEEP_SUBGROUP, &GUID_STANDBY_TIMEOUT, &v); FillTimeoutCombo(s_hwndCombo[2], v);
    v = 0; PowerReadDCValueIndex(NULL, &s_EditPlan, &GUID_SLEEP_SUBGROUP, &GUID_STANDBY_TIMEOUT, &v); FillTimeoutCombo(s_hwndCombo[3], v);
}

static void SavePlanSettings(void)
{
    PowerWriteACValueIndex(NULL, &s_EditPlan, &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT, ComboSeconds(s_hwndCombo[0]));
    PowerWriteDCValueIndex(NULL, &s_EditPlan, &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT, ComboSeconds(s_hwndCombo[1]));
    PowerWriteACValueIndex(NULL, &s_EditPlan, &GUID_SLEEP_SUBGROUP, &GUID_STANDBY_TIMEOUT, ComboSeconds(s_hwndCombo[2]));
    PowerWriteDCValueIndex(NULL, &s_EditPlan, &GUID_SLEEP_SUBGROUP, &GUID_STANDBY_TIMEOUT, ComboSeconds(s_hwndCombo[3]));
    if (IsEqualGUID(s_EditPlan, s_ActivePlan))
        PowerSetActiveScheme(NULL, &s_EditPlan);
}

static void ShowPlanControls(BOOL bShow)
{
    s_bPlanControlsVisible = bShow;
    for (int i = 0; i < 4; i++)
        if (s_hwndCombo[i]) ShowWindow(s_hwndCombo[i], (bShow && (s_bHasBattery || (i % 2) == 0)) ? SW_SHOW : SW_HIDE);
    if (s_hwndSave) ShowWindow(s_hwndSave, bShow ? SW_SHOW : SW_HIDE);
    if (s_hwndCancel) ShowWindow(s_hwndCancel, bShow ? SW_SHOW : SW_HIDE);
}

void Hub_Size(void)
{
    if (!s_bPlanControlsVisible || !s_hwndCombo[0]) return;
    for (int i = 0; i < 4; i++)
        SetWindowPos(s_hwndCombo[i], NULL, s_rcCombo[i].left, s_rcCombo[i].top, s_rcCombo[i].right - s_rcCombo[i].left, S(300), SWP_NOZORDER);
    SetWindowPos(s_hwndSave, NULL, s_rcSave.left, s_rcSave.top, s_rcSave.right - s_rcSave.left, s_rcSave.bottom - s_rcSave.top, SWP_NOZORDER);
    SetWindowPos(s_hwndCancel, NULL, s_rcCancel.left, s_rcCancel.top, s_rcCancel.right - s_rcCancel.left, s_rcCancel.bottom - s_rcCancel.top, SWP_NOZORDER);
}

static int PaintPlanSettings(HDC hdc, int x, int y, int cx)
{
    PWRPLAN *pPlan = NULL;
    for (int i = 0; i < s_cPlans; i++)
        if (IsEqualGUID(s_Plans[i].guid, s_EditPlan)) pPlan = &s_Plans[i];
    WCHAR szHdr[200];
    StringCchPrintfW(szHdr, _countof(szHdr), L"Change settings for the plan: %s", pPlan ? pPlan->szName : L"");
    y += PageHeader(hdc, x, y, szHdr);
    y += DrawWrapped(hdc, x, y, cx, L"Choose the sleep and display settings that you want your computer to use.", g_f.hfText, g_f.pal.Text) + S(18);

    int colLabel = S(210), colW = S(160);
    int cols = s_bHasBattery ? 2 : 1;
    HGDIOBJ oldF = SelectObject(hdc, g_f.hfText);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, g_f.pal.Text);
    if (s_bHasBattery)
    {
        HICON hb = LoadFluent(IDI_PWR_BATTERY, S(20));
        DrawIconAt(hdc, x + colLabel + S(20), y, hb, S(20), TRUE);
        TextOutW(hdc, x + colLabel + S(44), y + S(2), L"On battery", 10);
        HICON hp = LoadFluent(IDI_PWR_PLUG, S(20));
        DrawIconAt(hdc, x + colLabel + colW + S(20), y, hp, S(20), TRUE);
        TextOutW(hdc, x + colLabel + colW + S(44), y + S(2), L"Plugged in", 10);
    }
    else
    {
        HICON hp = LoadFluent(IDI_PWR_PLUG, S(20));
        DrawIconAt(hdc, x + colLabel + S(20), y, hp, S(20), TRUE);
        TextOutW(hdc, x + colLabel + S(44), y + S(2), L"Plugged in", 10);
    }
    y += S(30);
    LPCWSTR rows[2] = { L"Turn off the display:", L"Put the computer to sleep:" };
    for (int r = 0; r < 2; r++)
    {
        TextOutW(hdc, x, y + S(4), rows[r], lstrlenW(rows[r]));
        for (int c = 0; c < cols; c++)
        {
            int idx = r * 2 + (s_bHasBattery ? (c == 0 ? 1 : 0) : 0);
            int cxCombo = x + colLabel + c * colW + S(10);
            SetRect(&s_rcCombo[idx], cxCombo, y, cxCombo + S(140), y + S(22));
        }
        y += S(34);
    }
    SelectObject(hdc, oldF);
    if (!s_bHasBattery)
    {
        SetRect(&s_rcCombo[1], -S(200), -S(200), -S(50), -S(180));
        SetRect(&s_rcCombo[3], -S(200), -S(200), -S(50), -S(180));
    }
    y += S(6);
    int adv = AdvancedTabIndex();
    WCHAR szCmd[128];
    StringCchPrintfW(szCmd, _countof(szCmd), L"%%SystemRoot%%\\system32\\rundll32.exe shell32.dll,Control_RunDLL powercfg.cpl,,%d", adv);
    y += DrawLinkText(hdc, x, y, cx, L"Change advanced power settings", g_f.hfText, LA_COMMAND, 0, NULL, szCmd, NULL, TRUE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Restore default settings for this plan", g_f.hfText, LA_CUSTOM, CUSTOM_SETPLAN, NULL, L"restore", NULL, TRUE) + S(20);
    SetRect(&s_rcSave, x + cx - S(200), y, x + cx - S(96), y + S(26));
    SetRect(&s_rcCancel, x + cx - S(86), y, x + cx, y + S(26));
    y += S(40);
    Hub_Size();
    return y;
}

static void NavPower(HDC hdc, int x, int *py, int cx, LPCWSTR pszPage)
{
    int y = *py;
    int adv = AdvancedTabIndex();
    WCHAR szCmd[128];
    StringCchPrintfW(szCmd, _countof(szCmd), L"%%SystemRoot%%\\system32\\rundll32.exe shell32.dll,Control_RunDLL powercfg.cpl,,%d", adv);
    y += DrawLinkText(hdc, x, y, cx, L"Require a password on wakeup", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Choose what the power buttons do", g_f.hfText, LA_COMMAND, 0, NULL, szCmd, NULL, TRUE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Choose what closing the lid does", g_f.hfText, s_bHasBattery ? LA_COMMAND : LA_NONE, 0, NULL, szCmd, s_bHasBattery ? NULL : L"This computer has no lid", s_bHasBattery) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Create a power plan", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Choose when to turn off the display", g_f.hfText, LA_HUBPAGE, HUB_POWER, NULL, L"pagePlanSettings", NULL, TRUE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Change when the computer sleeps", g_f.hfText, LA_HUBPAGE, HUB_POWER, NULL, L"pagePlanSettings", NULL, TRUE) + S(6);
    *py = y;
}

/* ------------------------------------------------------------------ */
/*  Network and Sharing Center                                         */
/* ------------------------------------------------------------------ */

typedef struct _NETINFO
{
    WCHAR szName[128];
    WCHAR szAdapter[256];
    GUID guid;
    NLM_CONNECTIVITY connectivity;
    NLM_NETWORK_CATEGORY category;
    BOOL bInternet;
} NETINFO;

static NETINFO s_Nets[8];
static int s_cNets;
static BOOL s_bAnyAdapter;

static void AdapterNameFromGuid(REFGUID guid, LPWSTR pszOut, int cch)
{
    pszOut[0] = 0;
    ULONG cb = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, NULL, &cb) != ERROR_BUFFER_OVERFLOW || !cb)
        return;
    IP_ADAPTER_ADDRESSES *pAddr = (IP_ADAPTER_ADDRESSES *)LocalAlloc(LMEM_FIXED, cb);
    if (!pAddr) return;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, pAddr, &cb) == ERROR_SUCCESS)
    {
        WCHAR szGuid[64];
        StringFromGUID2(guid, szGuid, _countof(szGuid));
        for (IP_ADAPTER_ADDRESSES *p = pAddr; p; p = p->Next)
        {
            s_bAnyAdapter = TRUE;
            WCHAR szAdapterName[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, p->AdapterName, -1, szAdapterName, _countof(szAdapterName));
            if (!lstrcmpiW(szAdapterName, szGuid))
            {
                StringCchCopyW(pszOut, cch, p->FriendlyName ? p->FriendlyName : L"");
                break;
            }
        }
    }
    LocalFree(pAddr);
}

static void ConnectionNameFromGuid(REFGUID guid, LPWSTR pszOut, int cch)
{
    INetConnectionManager *pMgr = NULL;
    if (FAILED(CoCreateInstance(CLSID_ConnectionManager, NULL, CLSCTX_ALL, IID_INetConnectionManager, (void **)&pMgr)) || !pMgr)
        return;
    IEnumNetConnection *pEnum = NULL;
    if (SUCCEEDED(pMgr->EnumConnections(NCME_DEFAULT, &pEnum)) && pEnum)
    {
        INetConnection *pConn = NULL;
        ULONG fetched = 0;
        WCHAR szFirst[256] = L"";
        int count = 0;
        while (pEnum->Next(1, &pConn, &fetched) == S_OK && pConn)
        {
            NETCON_PROPERTIES *pProps = NULL;
            if (SUCCEEDED(pConn->GetProperties(&pProps)) && pProps)
            {
                if (pProps->pszwName && !szFirst[0]) StringCchCopyW(szFirst, _countof(szFirst), pProps->pszwName);
                if (IsEqualGUID(pProps->guidId, guid) && pProps->pszwName)
                    StringCchCopyW(pszOut, cch, pProps->pszwName);
                if (pProps->pszwName) CoTaskMemFree(pProps->pszwName);
                if (pProps->pszwDeviceName) CoTaskMemFree(pProps->pszwDeviceName);
                CoTaskMemFree(pProps);
            }
            count++;
            pConn->Release();
        }
        if (!pszOut[0] && count == 1) StringCchCopyW(pszOut, cch, szFirst);
        pEnum->Release();
    }
    pMgr->Release();
}

static void LoadNetworks(void)
{
    s_cNets = 0;
    s_bAnyAdapter = FALSE;
    INetworkListManager *pMgr = NULL;
    if (FAILED(CoCreateInstance(CLSID_NetworkListManager, NULL, CLSCTX_ALL, IID_INetworkListManager, (void **)&pMgr)) || !pMgr)
        return;
    IEnumNetworks *pEnum = NULL;
    if (SUCCEEDED(pMgr->GetNetworks(NLM_ENUM_NETWORK_CONNECTED, &pEnum)) && pEnum)
    {
        INetwork *pNet = NULL;
        ULONG fetched = 0;
        while (s_cNets < (int)_countof(s_Nets) && pEnum->Next(1, &pNet, &fetched) == S_OK && pNet)
        {
            NETINFO *ni = &s_Nets[s_cNets];
            ZeroMemory(ni, sizeof(*ni));
            BSTR bstr = NULL;
            if (SUCCEEDED(pNet->GetName(&bstr)) && bstr)
            {
                StringCchCopyW(ni->szName, _countof(ni->szName), bstr);
                SysFreeString(bstr);
            }
            if (!ni->szName[0]) StringCchCopyW(ni->szName, _countof(ni->szName), L"Network");
            pNet->GetNetworkId(&ni->guid);
            pNet->GetConnectivity(&ni->connectivity);
            ni->category = NLM_NETWORK_CATEGORY_PUBLIC;
            pNet->GetCategory(&ni->category);
            ni->bInternet = (ni->connectivity & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET)) != 0;
            IEnumNetworkConnections *pConns = NULL;
            if (SUCCEEDED(pNet->GetNetworkConnections(&pConns)) && pConns)
            {
                INetworkConnection *pConn = NULL;
                ULONG f2 = 0;
                if (pConns->Next(1, &pConn, &f2) == S_OK && pConn)
                {
                    GUID adapter;
                    if (SUCCEEDED(pConn->GetAdapterId(&adapter)))
                    {
                        ConnectionNameFromGuid(adapter, ni->szAdapter, _countof(ni->szAdapter));
                        if (!ni->szAdapter[0])
                            AdapterNameFromGuid(adapter, ni->szAdapter, _countof(ni->szAdapter));
                    }
                    pConn->Release();
                }
                pConns->Release();
            }
            pNet->Release();
            s_cNets++;
        }
        pEnum->Release();
    }
    pMgr->Release();
    if (!s_bAnyAdapter)
    {
        ULONG cb = 0;
        GetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &cb);
        s_bAnyAdapter = cb != 0;
    }
}

static LPCWSTR CategoryName(NLM_NETWORK_CATEGORY c)
{
    switch (c)
    {
    case NLM_NETWORK_CATEGORY_PRIVATE: return L"Home network";
    case NLM_NETWORK_CATEGORY_DOMAIN_AUTHENTICATED: return L"Domain network";
    default: return L"Public network";
    }
}

static int PaintNetwork(HDC hdc, int x, int y, int cx)
{
    y += PageHeader(hdc, x, y, L"View your basic network information and set up connections");

    int nodeW = S(64), gap = (cx - 3 * nodeW) / 2;
    if (gap > S(120)) gap = S(120);
    int totalW = 3 * nodeW + 2 * gap;
    int nx = x + (cx - totalW) / 2;
    int iconSz = S(48);
    WCHAR szPc[64] = L"";
    DWORD cch = _countof(szPc);
    GetComputerNameExW(ComputerNamePhysicalDnsHostname, szPc, &cch);
    CharUpperW(szPc);
    BOOL connected = s_cNets > 0;
    BOOL internet = FALSE;
    for (int i = 0; i < s_cNets; i++) if (s_Nets[i].bInternet) internet = TRUE;
    LPCWSTR labels[3] = { szPc, s_cNets ? s_Nets[0].szName : L"Unidentified network", L"Internet" };
    UINT icons[3] = { IDI_NET_PC, IDI_NET_ROUTER, IDI_NET_GLOBE };
    HGDIOBJ oldF = SelectObject(hdc, g_f.hfText);
    SetBkMode(hdc, TRANSPARENT);
    for (int i = 0; i < 3; i++)
    {
        int cxNode = nx + i * (nodeW + gap);
        HICON h = LoadFluent(icons[i], iconSz);
        BOOL en = (i == 0) || (i == 1 && connected) || (i == 2 && internet);
        DrawIconAt(hdc, cxNode + (nodeW - iconSz) / 2, y, h, iconSz, en);
        SetTextColor(hdc, en ? g_f.pal.Text : g_f.pal.Disabled);
        RECT rt = { cxNode - S(20), y + iconSz + S(4), cxNode + nodeW + S(20), y + iconSz + S(40) };
        DrawTextW(hdc, labels[i], -1, &rt, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        if (i < 2)
        {
            BOOL linkOk = (i == 0) ? connected : internet;
            HPEN pen = CreatePen(PS_SOLID, S(2), linkOk ? g_f.pal.Accent : g_f.pal.Disabled);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            int ly = y + iconSz / 2;
            MoveToEx(hdc, cxNode + nodeW + S(4), ly, NULL);
            LineTo(hdc, cxNode + nodeW + gap - S(4), ly);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            HICON hs = LoadFluent(linkOk ? IDI_NET_OK : IDI_NET_BAD, S(16));
            DrawIconAt(hdc, cxNode + nodeW + gap / 2 - S(8), ly - S(8), hs, S(16), TRUE);
        }
    }
    SelectObject(hdc, oldF);
    y += iconSz + S(44);
    DrawLinkText(hdc, x + cx - S(160), y - S(20), S(160), L"See full map", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE);
    y += S(6);

    y += SectionTitle(hdc, x, y, cx, L"View your active networks");
    if (!s_cNets)
    {
        y += DrawWrapped(hdc, x + S(16), y, cx - S(16), s_bAnyAdapter ? L"You are currently not connected to any networks." : L"No network adapters are installed on this computer.", g_f.hfText, g_f.pal.Text) + S(4);
        y += DrawLinkText(hdc, x + S(16), y, cx - S(16), L"Connect to a network", g_f.hfText, LA_COMMAND, 0, NULL, L"%SystemRoot%\\system32\\control.exe netconnections", NULL, s_bAnyAdapter) + S(14);
    }
    for (int i = 0; i < s_cNets; i++)
    {
        NETINFO *ni = &s_Nets[i];
        int lx = x + S(16);
        HICON h = LoadFluent(IDI_NET_ROUTER, S(32));
        DrawIconAt(hdc, lx, y, h, S(32), TRUE);
        int tx = lx + S(44);
        int top = y;
        y += DrawWrapped(hdc, tx, y, S(220), ni->szName, g_f.hfBold, g_f.pal.Text) + S(2);
        WCHAR szGuid[64];
        StringFromGUID2(ni->guid, szGuid, _countof(szGuid));
        y += DrawLinkText(hdc, tx, y, S(220), CategoryName(ni->category), g_f.hfText, LA_CUSTOM, CUSTOM_NETCATEGORY, NULL, szGuid, L"Change the network location type", TRUE) + S(4);
        int rx = x + cx / 2 + S(10);
        int ry = top;
        LPCWSTR pszAccess = ni->bInternet ? L"Internet" : (ni->connectivity & (NLM_CONNECTIVITY_IPV4_LOCALNETWORK | NLM_CONNECTIVITY_IPV6_LOCALNETWORK | NLM_CONNECTIVITY_IPV4_SUBNET | NLM_CONNECTIVITY_IPV6_SUBNET)) ? L"No Internet access" : L"No network access";
        ry += LabelValue(hdc, rx, ry, S(100), x + cx - rx, L"Access type:", pszAccess);
        oldF = SelectObject(hdc, g_f.hfText);
        SetTextColor(hdc, g_f.pal.DimText);
        TextOutW(hdc, rx, ry, L"Connections:", 12);
        SelectObject(hdc, oldF);
        DrawLinkText(hdc, rx + S(100), ry, x + cx - rx - S(100), ni->szAdapter[0] ? ni->szAdapter : L"Local Area Connection", g_f.hfText, LA_COMMAND, 0, NULL, L"%SystemRoot%\\system32\\control.exe netconnections", L"View the status of this connection", TRUE);
        ry += TextH(hdc, g_f.hfText) + S(4);
        if (ry > y) y = ry;
        y += S(14);
    }

    y += SectionTitle(hdc, x, y, cx, L"Change your networking settings");
    int lx = x + S(16), lcx = cx - S(16);
    y += DrawLinkText(hdc, lx, y, lcx, L"Set up a new connection or network", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(2);
    y += DrawWrapped(hdc, lx + S(16), y, lcx - S(16), L"Set up a wireless, broadband, dial-up, ad hoc, or VPN connection; or set up a router or access point.", g_f.hfSmall, g_f.pal.DimText) + S(10);
    y += DrawLinkText(hdc, lx, y, lcx, L"Connect to a network", g_f.hfText, LA_COMMAND, 0, NULL, L"%SystemRoot%\\system32\\control.exe netconnections", NULL, s_bAnyAdapter) + S(2);
    y += DrawWrapped(hdc, lx + S(16), y, lcx - S(16), L"Connect or reconnect to a wireless, wired, dial-up, or VPN network connection.", g_f.hfSmall, g_f.pal.DimText) + S(10);
    y += DrawLinkText(hdc, lx, y, lcx, L"Choose homegroup and sharing options", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(2);
    y += DrawWrapped(hdc, lx + S(16), y, lcx - S(16), L"Access files and printers located on other network computers, or change sharing settings.", g_f.hfSmall, g_f.pal.DimText) + S(10);
    y += DrawLinkText(hdc, lx, y, lcx, L"Troubleshoot problems", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(2);
    y += DrawWrapped(hdc, lx + S(16), y, lcx - S(16), L"Diagnose and repair network problems, or get troubleshooting information.", g_f.hfSmall, g_f.pal.DimText) + S(10);
    return y;
}

static void NavNetwork(HDC hdc, int x, int *py, int cx)
{
    int y = *py;
    y += DrawLinkText(hdc, x, y, cx, L"Change adapter settings", g_f.hfText, LA_COMMAND, 0, NULL, L"%SystemRoot%\\system32\\control.exe netconnections", NULL, TRUE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Change advanced sharing settings", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(6);
    *py = y;
}

static void SetNetworkCategory(LPCWSTR pszGuid)
{
    GUID g;
    if (FAILED(CLSIDFromString(pszGuid, &g))) return;
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"Home network");
    AppendMenuW(hMenu, MF_STRING, 2, L"Work network");
    AppendMenuW(hMenu, MF_STRING, 3, L"Public network");
    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_f.hwnd, NULL);
    DestroyMenu(hMenu);
    if (!cmd) return;
    NLM_NETWORK_CATEGORY cat = cmd == 3 ? NLM_NETWORK_CATEGORY_PUBLIC : NLM_NETWORK_CATEGORY_PRIVATE;
    INetworkListManager *pMgr = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_NetworkListManager, NULL, CLSCTX_ALL, IID_INetworkListManager, (void **)&pMgr)) && pMgr)
    {
        INetwork *pNet = NULL;
        if (SUCCEEDED(pMgr->GetNetwork(g, &pNet)) && pNet)
        {
            pNet->SetCategory(cat);
            pNet->Release();
        }
        pMgr->Release();
    }
    LoadNetworks();
    Frame_Invalidate();
}

/* ------------------------------------------------------------------ */
/*  Personalization                                                    */
/* ------------------------------------------------------------------ */

typedef struct _THEMEENTRY
{
    WCHAR szName[64];
    WCHAR szPath[MAX_PATH];
} THEMEENTRY;

static THEMEENTRY s_Themes[32];
static int s_cThemes;
static WCHAR s_szCurrentTheme[MAX_PATH];

static void LoadThemes(void)
{
    s_cThemes = 0;
    s_szCurrentTheme[0] = 0;
    if (IsThemeActive())
        GetCurrentThemeName(s_szCurrentTheme, _countof(s_szCurrentTheme), NULL, 0, NULL, 0);
    WCHAR szDir[MAX_PATH], szPattern[MAX_PATH];
    ExpandEnvironmentStringsW(L"%SystemRoot%\\Resources\\Themes", szDir, _countof(szDir));
    StringCchPrintfW(szPattern, _countof(szPattern), L"%s\\*", szDir);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(szPattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        WCHAR szSub[MAX_PATH];
        StringCchPrintfW(szSub, _countof(szSub), L"%s\\%s\\*.msstyles", szDir, fd.cFileName);
        WIN32_FIND_DATAW fs;
        HANDLE hSub = FindFirstFileW(szSub, &fs);
        if (hSub != INVALID_HANDLE_VALUE)
        {
            if (s_cThemes < (int)_countof(s_Themes))
            {
                THEMEENTRY *t = &s_Themes[s_cThemes++];
                StringCchCopyW(t->szName, _countof(t->szName), fd.cFileName);
                StringCchPrintfW(t->szPath, _countof(t->szPath), L"%s\\%s\\%s", szDir, fd.cFileName, fs.cFileName);
            }
            FindClose(hSub);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

static int PaintPersonalization(HDC hdc, int x, int y, int cx)
{
    y += PageHeader(hdc, x, y, L"Change the visuals and sounds on your computer");
    y += DrawWrapped(hdc, x, y, cx, L"Click a theme to change the desktop background, window color, sounds, and screen saver all at once.", g_f.hfText, g_f.pal.Text) + S(14);

    y += SectionTitle(hdc, x, y, cx, L"Installed Themes");
    int tileW = S(150), tileH = S(110);
    int cols = max(1, cx / tileW);
    int col = 0;
    int total = s_cThemes + 1;
    for (int i = 0; i < total; i++)
    {
        BOOL classic = i == s_cThemes;
        LPCWSTR pszName = classic ? L"Windows Classic" : s_Themes[i].szName;
        LPCWSTR pszPath = classic ? L"" : s_Themes[i].szPath;
        BOOL current = classic ? !s_szCurrentTheme[0] : !lstrcmpiW(s_szCurrentTheme, pszPath);
        int tx = x + col * tileW;
        RECT r = { tx, y, tx + tileW - S(10), y + tileH - S(10) };
        int idx = g_f.cLinks;
        AddLink(&r, LA_CUSTOM, CUSTOM_APPLYTHEME, NULL, classic ? L"classic" : pszPath, L"Apply this theme", TRUE);
        HBRUSH br = CreateSolidBrush(current ? g_f.pal.Accent : (g_f.hotLink == idx ? g_f.pal.Rule : g_f.pal.NavBorder));
        FrameRect(hdc, &r, br);
        DeleteObject(br);
        if (current || g_f.hotLink == idx)
        {
            RECT r2 = { r.left + 1, r.top + 1, r.right - 1, r.bottom - 1 };
            HBRUSH b2 = CreateSolidBrush(current ? g_f.pal.Accent : g_f.pal.Rule);
            FrameRect(hdc, &r2, b2);
            DeleteObject(b2);
        }
        RECT rp = { r.left + S(10), r.top + S(10), r.right - S(10), r.bottom - S(32) };
        HBRUSH bp = CreateSolidBrush(classic ? RGB(58, 110, 165) : g_f.pal.Accent);
        FillRect(hdc, &rp, bp);
        DeleteObject(bp);
        RECT rw = { rp.left + S(14), rp.top + S(12), rp.right - S(14), rp.bottom - S(8) };
        HBRUSH bw = CreateSolidBrush(g_f.pal.Window);
        FillRect(hdc, &rw, bw);
        DeleteObject(bw);
        RECT rc = { rw.left, rw.top, rw.right, rw.top + S(8) };
        HBRUSH bc = CreateSolidBrush(classic ? RGB(10, 36, 106) : Blend(g_f.pal.Accent, g_f.pal.Window, 35));
        FillRect(hdc, &rc, bc);
        DeleteObject(bc);
        HGDIOBJ oldF = SelectObject(hdc, current ? g_f.hfBold : g_f.hfText);
        SetTextColor(hdc, g_f.pal.Text);
        SetBkMode(hdc, TRANSPARENT);
        RECT rt = { r.left + S(4), r.bottom - S(26), r.right - S(4), r.bottom - S(4) };
        DrawTextW(hdc, pszName, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, oldF);
        col++;
        if (col == cols) { col = 0; y += tileH; }
    }
    if (col) y += tileH;
    y += S(10);

    struct { LPCWSTR psz; LPCWSTR cmd; } bottom[4] =
    {
        { L"Desktop Background", L"%SystemRoot%\\system32\\rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,@Desktop" },
        { L"Window Color", L"%SystemRoot%\\system32\\rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,@Appearance" },
        { L"Sounds", L"%SystemRoot%\\system32\\rundll32.exe shell32.dll,Control_RunDLL mmsys.cpl,,1" },
        { L"Screen Saver", L"%SystemRoot%\\system32\\rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,\"@Screen Saver\"" },
    };
    int bw = cx / 4;
    for (int i = 0; i < 4; i++)
    {
        int bx = x + i * bw;
        DrawLinkText(hdc, bx, y, bw - S(8), bottom[i].psz, g_f.hfText, LA_COMMAND, 0, NULL, bottom[i].cmd, NULL, TRUE);
    }
    y += TextH(hdc, g_f.hfText) + S(20);
    return y;
}

static void NavPersonalization(HDC hdc, int x, int *py, int cx)
{
    int y = *py;
    y += DrawLinkText(hdc, x, y, cx, L"Change desktop icons", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Change mouse pointers", g_f.hfText, LA_COMMAND, 0, NULL, L"%SystemRoot%\\system32\\rundll32.exe shell32.dll,Control_RunDLL main.cpl,@0,1", NULL, TRUE) + S(6);
    y += DrawLinkText(hdc, x, y, cx, L"Change your account picture", g_f.hfText, LA_NONE, 0, NULL, NULL, L"Not available in ReactOS yet", FALSE) + S(6);
    *py = y;
}

/* ------------------------------------------------------------------ */
/*  Dispatch                                                           */
/* ------------------------------------------------------------------ */

static void SeeAlso(HDC hdc, int x, int *py, int cx, LPCWSTR const *ppsz, int n)
{
    int y = *py + S(14);
    HGDIOBJ oldF = SelectObject(hdc, g_f.hfBold);
    SetTextColor(hdc, g_f.pal.Text);
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, x, y, L"See also", 8);
    SelectObject(hdc, oldF);
    y += TextH(hdc, g_f.hfBold) + S(6);
    for (int i = 0; i < n; i++)
    {
        CPITEM *pItem = Catalog_FindByCanonical(ppsz[i]);
        if (!pItem) continue;
        y += DrawLinkText(hdc, x, y, cx, pItem->szName, g_f.hfText, LA_ITEM, 0, pItem, NULL, pItem->bEnabled ? pItem->szInfoTip : L"Not available in ReactOS yet", pItem->bEnabled) + S(6);
    }
    *py = y;
}

void Hub_PaintNavTasks(HDC hdc, int x, int *py, int cx, int hub, LPCWSTR pszPage)
{
    switch (hub)
    {
    case HUB_SYSTEM:
    {
        NavSystem(hdc, x, py, cx);
        static LPCWSTR const also[] = { L"Microsoft.ActionCenter", L"Microsoft.WindowsUpdate", L"Microsoft.PerformanceInformationAndTools" };
        SeeAlso(hdc, x, py, cx, also, _countof(also));
        break;
    }
    case HUB_POWER:
    {
        NavPower(hdc, x, py, cx, pszPage);
        static LPCWSTR const also[] = { L"Microsoft.Personalization", L"Microsoft.UserAccounts" };
        SeeAlso(hdc, x, py, cx, also, _countof(also));
        break;
    }
    case HUB_NETWORK:
    {
        NavNetwork(hdc, x, py, cx);
        static LPCWSTR const also[] = { L"Microsoft.HomeGroup", L"Microsoft.InternetOptions", L"Microsoft.WindowsFirewall" };
        SeeAlso(hdc, x, py, cx, also, _countof(also));
        break;
    }
    case HUB_PERSONALIZATION:
    {
        NavPersonalization(hdc, x, py, cx);
        static LPCWSTR const also[] = { L"Microsoft.Display", L"Microsoft.TaskbarAndStartMenu", L"Microsoft.EaseOfAccessCenter" };
        SeeAlso(hdc, x, py, cx, also, _countof(also));
        break;
    }
    }
}

int Hub_Paint(HDC hdc, const RECT *prc, int hub, LPCWSTR pszPage)
{
    int x = prc->left + S(24);
    int cx = prc->right - x - S(24);
    int y = prc->top + S(18);
    switch (hub)
    {
    case HUB_SYSTEM: y = PaintSystem(hdc, x, y, cx); break;
    case HUB_POWER:
        if (pszPage && !lstrcmpiW(pszPage, L"pagePlanSettings")) y = PaintPlanSettings(hdc, x, y, cx);
        else y = PaintPowerPlans(hdc, x, y, cx);
        break;
    case HUB_NETWORK: y = PaintNetwork(hdc, x, y, cx); break;
    case HUB_PERSONALIZATION: y = PaintPersonalization(hdc, x, y, cx); break;
    }
    return y - prc->top + S(20);
}

void Hub_ViewChanged(int hub, LPCWSTR pszPage)
{
    ShowPlanControls(FALSE);
    switch (hub)
    {
    case HUB_POWER:
        LoadPlans();
        if (pszPage && !lstrcmpiW(pszPage, L"pagePlanSettings"))
        {
            if (IsEqualGUID(s_EditPlan, GUID_NULL)) s_EditPlan = s_ActivePlan;
            BOOL known = FALSE;
            for (int i = 0; i < s_cPlans; i++) if (IsEqualGUID(s_Plans[i].guid, s_EditPlan)) known = TRUE;
            if (!known) s_EditPlan = s_ActivePlan;
            LoadPlanSettings();
            ShowPlanControls(TRUE);
        }
        else if (pszPage && !lstrcmpiW(pszPage, L"pageGlobalSettings"))
        {
            WCHAR szCmd[128];
            StringCchPrintfW(szCmd, _countof(szCmd), L"powercfg.cpl,,%d", AdvancedTabIndex());
            LaunchCplPage(szCmd);
        }
        break;
    case HUB_NETWORK:
        LoadNetworks();
        break;
    case HUB_PERSONALIZATION:
        LoadThemes();
        if (pszPage && !lstrcmpiW(pszPage, L"pageWallpaper")) LaunchCplPage(L"desk.cpl,,@Desktop");
        else if (pszPage && !lstrcmpiW(pszPage, L"pageColorization")) LaunchCplPage(L"desk.cpl,,@Appearance");
        else if (pszPage && !lstrcmpiW(pszPage, L"pageScreenSaver")) LaunchCplPage(L"desk.cpl,,\"@Screen Saver\"");
        break;
    case HUB_SYSTEM:
        if (pszPage && !lstrcmpiW(pszPage, L"pageAdvanced")) LaunchCplPage(L"sysdm.cpl,,2");
        else if (pszPage && !lstrcmpiW(pszPage, L"pageComputerName")) LaunchCplPage(L"sysdm.cpl,,1");
        break;
    }
}

BOOL Hub_Custom(HWND hwnd, int param, LPCWSTR pszArg)
{
    if (param & 0x10000)
    {
        int id = param & 0xFFFF;
        if (id == IDC_PLAN_SAVE)
        {
            SavePlanSettings();
            Frame_OpenHub(HUB_POWER, NULL);
            return TRUE;
        }
        if (id == IDC_PLAN_CANCEL)
        {
            Frame_OpenHub(HUB_POWER, NULL);
            return TRUE;
        }
        return FALSE;
    }
    switch (param)
    {
    case CUSTOM_SETPLAN:
        if (!lstrcmpW(pszArg, L"restore"))
        {
            FillTimeoutCombo(s_hwndCombo[0], 10 * 60);
            FillTimeoutCombo(s_hwndCombo[1], 5 * 60);
            FillTimeoutCombo(s_hwndCombo[2], 30 * 60);
            FillTimeoutCombo(s_hwndCombo[3], 15 * 60);
            return TRUE;
        }
        {
            GUID g;
            if (SUCCEEDED(CLSIDFromString(pszArg, &g)))
            {
                PowerSetActiveScheme(NULL, &g);
                s_EditPlan = g;
                LoadPlans();
                Frame_Invalidate();
            }
        }
        return TRUE;
    case CUSTOM_NETCATEGORY:
        SetNetworkCategory(pszArg);
        return TRUE;
    case CUSTOM_EDITPLAN:
        if (SUCCEEDED(CLSIDFromString(pszArg, &s_EditPlan)))
            Frame_OpenHub(HUB_POWER, L"pagePlanSettings");
        return TRUE;
    case CUSTOM_MOREPLANS:
        s_bShowMorePlans = !s_bShowMorePlans;
        Frame_Invalidate();
        return TRUE;
    case CUSTOM_APPLYTHEME:
        if (!lstrcmpW(pszArg, L"classic"))
            LaunchCplPage(L"desk.cpl,,/Action:ActivateMSTheme");
        else
        {
            WCHAR szCmd[MAX_PATH + 64];
            StringCchPrintfW(szCmd, _countof(szCmd), L"desk.cpl,,/Action:ActivateMSTheme /file:%s", pszArg);
            LaunchCplPage(szCmd);
        }
        return TRUE;
    }
    return FALSE;
}
