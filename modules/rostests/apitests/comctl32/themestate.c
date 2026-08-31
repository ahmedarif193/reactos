/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test that comctl32 requests the extended theme part/state
 *                  pairs (hover, selection, sort, disabled) from uxtheme
 * PROGRAMMERS:     Ahmed ARIF
 */

#include "wine/test.h"
#include <stdio.h>
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>

typedef HRESULT (WINAPI *DTB_FN)(HTHEME, HDC, int, int, LPCRECT, LPCRECT);
typedef HRESULT (WINAPI *TDI_FN)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);

#define LOG_MAX 2048

static struct
{
    HTHEME theme;
    int part;
    int state;
} g_log[LOG_MAX];
static int g_logCount;

static DTB_FN g_realDTB;
static HMODULE g_hComctl;
static PVOID *g_slots[2];
static PVOID g_slotOld[2];
static int g_slotCount;
static HWND g_hMainWnd;

static HRESULT WINAPI
LogDrawThemeBackground(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCRECT prc, LPCRECT prcClip)
{
    if (g_logCount < LOG_MAX)
    {
        g_log[g_logCount].theme = hTheme;
        g_log[g_logCount].part = iPartId;
        g_log[g_logCount].state = iStateId;
        g_logCount++;
    }
    return g_realDTB(hTheme, hdc, iPartId, iStateId, prc, prcClip);
}

static void LogReset(void)
{
    g_logCount = 0;
}

static BOOL LogHas(int part, int state)
{
    int i;
    for (i = 0; i < g_logCount; i++)
    {
        if (g_log[i].part == part && g_log[i].state == state)
            return TRUE;
    }
    return FALSE;
}

static BOOL LogHasPart(int part)
{
    int i;
    for (i = 0; i < g_logCount; i++)
    {
        if (g_log[i].part == part)
            return TRUE;
    }
    return FALSE;
}

static BOOL LogHasPartRangeState(int partFirst, int partLast, int state)
{
    int i;
    for (i = 0; i < g_logCount; i++)
    {
        if (g_log[i].part >= partFirst && g_log[i].part <= partLast && g_log[i].state == state)
            return TRUE;
    }
    return FALSE;
}

static void DumpLog(const char *ctx)
{
    int i;
    trace("%s: %d DrawThemeBackground calls\n", ctx, g_logCount);
    for (i = 0; i < g_logCount && i < 64; i++)
        trace("  theme=%p part=%d state=%d\n", g_log[i].theme, g_log[i].part, g_log[i].state);
}

static BOOL NameContainsUxtheme(const char *name)
{
    char buf[64];
    int i;
    for (i = 0; name[i] && i < 63; i++)
        buf[i] = (name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i];
    buf[i] = 0;
    return strstr(buf, "uxtheme") != NULL;
}

static PVOID *FindUxthemeSlot(HMODULE mod, const char *func)
{
    PBYTE base = (PBYTE)mod;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + ((PIMAGE_DOS_HEADER)base)->e_lfanew);
    DWORD rva;

    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (rva)
    {
        PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)(base + rva);
        for (; imp->Name; imp++)
        {
            PIMAGE_THUNK_DATA nameTh, addrTh;
            if (!NameContainsUxtheme((const char *)(base + imp->Name)))
                continue;
            nameTh = (PIMAGE_THUNK_DATA)(base + imp->OriginalFirstThunk);
            addrTh = (PIMAGE_THUNK_DATA)(base + imp->FirstThunk);
            for (; nameTh->u1.AddressOfData; nameTh++, addrTh++)
            {
                PIMAGE_IMPORT_BY_NAME ibn;
                if (IMAGE_SNAP_BY_ORDINAL(nameTh->u1.Ordinal))
                    continue;
                ibn = (PIMAGE_IMPORT_BY_NAME)(base + nameTh->u1.AddressOfData);
                if (!strcmp((const char *)ibn->Name, func))
                    return (PVOID *)&addrTh->u1.Function;
            }
        }
    }

    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    if (rva)
    {
        PIMAGE_DELAYLOAD_DESCRIPTOR dld = (PIMAGE_DELAYLOAD_DESCRIPTOR)(base + rva);
        for (; dld->DllNameRVA; dld++)
        {
            const char *dll;
            PIMAGE_THUNK_DATA nameTh, addrTh;
            BOOL rvaBased = dld->Attributes.RvaBased;
            if (rvaBased)
            {
                dll = (const char *)(base + dld->DllNameRVA);
                nameTh = (PIMAGE_THUNK_DATA)(base + dld->ImportNameTableRVA);
                addrTh = (PIMAGE_THUNK_DATA)(base + dld->ImportAddressTableRVA);
            }
            else
            {
                dll = (const char *)(ULONG_PTR)dld->DllNameRVA;
                nameTh = (PIMAGE_THUNK_DATA)(ULONG_PTR)dld->ImportNameTableRVA;
                addrTh = (PIMAGE_THUNK_DATA)(ULONG_PTR)dld->ImportAddressTableRVA;
            }
            if (!NameContainsUxtheme(dll))
                continue;
            for (; nameTh->u1.AddressOfData; nameTh++, addrTh++)
            {
                PIMAGE_IMPORT_BY_NAME ibn;
                if (IMAGE_SNAP_BY_ORDINAL(nameTh->u1.Ordinal))
                    continue;
                ibn = rvaBased ? (PIMAGE_IMPORT_BY_NAME)(base + nameTh->u1.AddressOfData)
                               : (PIMAGE_IMPORT_BY_NAME)(ULONG_PTR)nameTh->u1.AddressOfData;
                if (!strcmp((const char *)ibn->Name, func))
                    return (PVOID *)&addrTh->u1.Function;
            }
        }
    }
    return NULL;
}

static void HookSlot(HMODULE mod)
{
    PVOID *slot;
    DWORD oldProt;
    int i;

    if (!mod || g_slotCount >= 2)
        return;
    slot = FindUxthemeSlot(mod, "DrawThemeBackground");
    if (!slot)
        return;
    for (i = 0; i < g_slotCount; i++)
    {
        if (g_slots[i] == slot)
            return;
    }
    if (!VirtualProtect(slot, sizeof(PVOID), PAGE_EXECUTE_READWRITE, &oldProt))
        return;
    g_slots[g_slotCount] = slot;
    g_slotOld[g_slotCount] = *slot;
    *slot = (PVOID)LogDrawThemeBackground;
    VirtualProtect(slot, sizeof(PVOID), oldProt, &oldProt);
    g_slotCount++;
}

static BOOL HookComctl(void)
{
    HWND probe;
    WNDPROC wp;
    HMODULE mod = NULL;
    HMODULE hUxtheme;

    probe = CreateWindowW(L"Button", L"probe", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    if (probe)
    {
        wp = (WNDPROC)GetClassLongPtrW(probe, GCLP_WNDPROC);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)wp, &mod);
        DestroyWindow(probe);
    }
    g_hComctl = mod ? mod : GetModuleHandleW(L"comctl32.dll");
    HookSlot(mod);
    HookSlot(GetModuleHandleW(L"comctl32.dll"));

    hUxtheme = LoadLibraryW(L"uxtheme.dll");
    g_realDTB = (DTB_FN)GetProcAddress(hUxtheme, "DrawThemeBackground");
    return g_slotCount > 0 && g_realDTB != NULL;
}

static void UnhookComctl(void)
{
    DWORD oldProt;
    int i;
    for (i = 0; i < g_slotCount; i++)
    {
        if (VirtualProtect(g_slots[i], sizeof(PVOID), PAGE_EXECUTE_READWRITE, &oldProt))
        {
            *g_slots[i] = g_slotOld[i];
            VirtualProtect(g_slots[i], sizeof(PVOID), oldProt, &oldProt);
        }
    }
    g_slotCount = 0;
}

static void PumpMessages(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void FlushPaint(HWND hwnd)
{
    PumpMessages();
    RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    PumpMessages();
}

static void MouseOver(HWND hwnd, int x, int y)
{
    POINT pt = { x, y };
    ClientToScreen(hwnd, &pt);
    SetCursorPos(pt.x, pt.y);
    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
}

static void MouseAway(HWND hwnd)
{
    SetCursorPos(0, 0);
    SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0);
    PumpMessages();
}

static void Test_ListView(void)
{
    HWND hLV;
    HTHEME theme;
    RECT rc;
    LVITEMW item = { 0 };
    int i;

    hLV = CreateWindowExW(0, WC_LISTVIEWW, NULL,
                          WS_CHILD | WS_VISIBLE | LVS_LIST | LVS_SHOWSELALWAYS,
                          5, 5, 260, 180, g_hMainWnd, NULL, NULL, NULL);
    ok(hLV != NULL, "Expected listview creation to succeed\n");
    if (!hLV)
        return;

    SetWindowTheme(hLV, L"Explorer", NULL);
    PumpMessages();

    theme = OpenThemeData(hLV, L"Explorer::ListView;ListView");
    if (!theme || !IsThemePartDefined(theme, LVP_LISTITEM, 0))
    {
        skip("No themed LVP_LISTITEM (theme=%p)\n", theme);
        if (theme) CloseThemeData(theme);
        DestroyWindow(hLV);
        return;
    }

    item.mask = LVIF_TEXT;
    for (i = 0; i < 3; i++)
    {
        WCHAR name[16];
        wsprintfW(name, L"item%d", i);
        item.iItem = i;
        item.pszText = name;
        SendMessageW(hLV, LVM_INSERTITEMW, 0, (LPARAM)&item);
    }
    FlushPaint(hLV);

    rc.left = LVIR_BOUNDS;
    SendMessageW(hLV, LVM_GETITEMRECT, 0, (LPARAM)&rc);

    MouseOver(hLV, (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    SendMessageW(hLV, LVM_SETHOTITEM, 0, 0);
    LogReset();
    FlushPaint(hLV);
    if (!LogHas(LVP_LISTITEM, LISS_HOT)) DumpLog("listview hot");
    ok(LogHas(LVP_LISTITEM, LISS_HOT), "Expected LVP_LISTITEM/LISS_HOT for hot item\n");

    SetFocus(hLV);
    item.mask = LVIF_STATE;
    item.state = LVIS_SELECTED | LVIS_FOCUSED;
    item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(hLV, LVM_SETITEMSTATE, 0, (LPARAM)&item);
    SendMessageW(hLV, LVM_SETHOTITEM, (WPARAM)-1, 0);
    MouseAway(hLV);
    LogReset();
    FlushPaint(hLV);
    if (!LogHas(LVP_LISTITEM, LISS_SELECTED)) DumpLog("listview selected");
    ok(LogHas(LVP_LISTITEM, LISS_SELECTED), "Expected LVP_LISTITEM/LISS_SELECTED for focused selection\n");

    SetFocus(g_hMainWnd);
    LogReset();
    FlushPaint(hLV);
    if (!LogHas(LVP_LISTITEM, LISS_SELECTEDNOTFOCUS)) DumpLog("listview selectednotfocus");
    ok(LogHas(LVP_LISTITEM, LISS_SELECTEDNOTFOCUS), "Expected LVP_LISTITEM/LISS_SELECTEDNOTFOCUS for unfocused selection\n");

    SetFocus(hLV);
    MouseOver(hLV, (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    SendMessageW(hLV, LVM_SETHOTITEM, 0, 0);
    LogReset();
    FlushPaint(hLV);
    if (!LogHas(LVP_LISTITEM, LISS_HOTSELECTED)) DumpLog("listview hotselected");
    ok(LogHas(LVP_LISTITEM, LISS_HOTSELECTED), "Expected LVP_LISTITEM/LISS_HOTSELECTED for hot selected item\n");

    CloseThemeData(theme);
    DestroyWindow(hLV);
}

static void Test_TreeView(void)
{
    HWND hTV;
    HTHEME theme;
    TVINSERTSTRUCTW ins = { 0 };
    HTREEITEM hRoot;
    RECT rc;
    DWORD indent;

    hTV = CreateWindowExW(0, WC_TREEVIEWW, NULL,
                          WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                          5, 5, 260, 180, g_hMainWnd, NULL, NULL, NULL);
    ok(hTV != NULL, "Expected treeview creation to succeed\n");
    if (!hTV)
        return;

    SetWindowTheme(hTV, L"Explorer", NULL);
    PumpMessages();

    theme = OpenThemeData(hTV, L"Explorer::TreeView;TreeView");
    if (!theme || !IsThemePartDefined(theme, TVP_TREEITEM, 0))
    {
        skip("No themed TVP_TREEITEM (theme=%p)\n", theme);
        if (theme) CloseThemeData(theme);
        DestroyWindow(hTV);
        return;
    }

    ins.hParent = TVI_ROOT;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT;
    ins.item.pszText = (LPWSTR)L"root";
    hRoot = (HTREEITEM)SendMessageW(hTV, TVM_INSERTITEMW, 0, (LPARAM)&ins);
    ins.hParent = hRoot;
    ins.item.pszText = (LPWSTR)L"child";
    SendMessageW(hTV, TVM_INSERTITEMW, 0, (LPARAM)&ins);
    FlushPaint(hTV);

    *(HTREEITEM *)&rc = hRoot;
    SendMessageW(hTV, TVM_GETITEMRECT, TRUE, (LPARAM)&rc);

    MouseOver(hTV, (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    LogReset();
    FlushPaint(hTV);
    if (!LogHas(TVP_TREEITEM, TREIS_HOT)) DumpLog("treeview hot");
    ok(LogHas(TVP_TREEITEM, TREIS_HOT), "Expected TVP_TREEITEM/TREIS_HOT for hot item\n");

    SetFocus(hTV);
    SendMessageW(hTV, TVM_SELECTITEM, TVGN_CARET, (LPARAM)hRoot);
    MouseAway(hTV);
    LogReset();
    FlushPaint(hTV);
    if (!LogHas(TVP_TREEITEM, TREIS_SELECTED)) DumpLog("treeview selected");
    ok(LogHas(TVP_TREEITEM, TREIS_SELECTED), "Expected TVP_TREEITEM/TREIS_SELECTED for focused selection\n");

    SetFocus(g_hMainWnd);
    LogReset();
    FlushPaint(hTV);
    if (!LogHas(TVP_TREEITEM, TREIS_SELECTEDNOTFOCUS)) DumpLog("treeview selectednotfocus");
    ok(LogHas(TVP_TREEITEM, TREIS_SELECTEDNOTFOCUS), "Expected TVP_TREEITEM/TREIS_SELECTEDNOTFOCUS for unfocused selection\n");

    if (IsThemePartDefined(theme, TVP_HOTGLYPH, 0))
    {
        indent = (DWORD)SendMessageW(hTV, TVM_GETINDENT, 0, 0);
        *(HTREEITEM *)&rc = hRoot;
        SendMessageW(hTV, TVM_GETITEMRECT, TRUE, (LPARAM)&rc);
        MouseOver(hTV, rc.left - indent / 2, (rc.top + rc.bottom) / 2);
        LogReset();
        FlushPaint(hTV);
        if (!LogHasPart(TVP_HOTGLYPH)) DumpLog("treeview hotglyph");
        ok(LogHasPart(TVP_HOTGLYPH), "Expected TVP_HOTGLYPH when hovering the expando glyph\n");
    }
    else
    {
        skip("TVP_HOTGLYPH not defined\n");
    }

    CloseThemeData(theme);
    DestroyWindow(hTV);
}

static void Test_Edit(void)
{
    HWND hEdit;
    HTHEME theme;
    BOOL haveBorder;

    hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"text",
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                            5, 5, 150, 24, g_hMainWnd, NULL, NULL, NULL);
    ok(hEdit != NULL, "Expected edit creation to succeed\n");
    if (!hEdit)
        return;
    PumpMessages();

    theme = GetWindowTheme(hEdit);
    if (!theme)
        theme = OpenThemeData(hEdit, L"Edit");
    if (!theme)
    {
        skip("Edit is not themed\n");
        DestroyWindow(hEdit);
        return;
    }
    haveBorder = IsThemePartDefined(theme, EP_EDITBORDER_NOSCROLL, 0);

    MouseOver(hEdit, 20, 10);
    LogReset();
    FlushPaint(hEdit);
    if (!LogHas(EP_EDITBORDER_NOSCROLL, EPSN_HOT) && !LogHas(EP_EDITTEXT, ETS_HOT))
        DumpLog("edit hot");
    ok(LogHas(EP_EDITBORDER_NOSCROLL, EPSN_HOT) || LogHas(EP_EDITTEXT, ETS_HOT),
       "Expected hot edit border (EPSN_HOT or ETS_HOT)\n");

    SetFocus(hEdit);
    MouseAway(hEdit);
    LogReset();
    FlushPaint(hEdit);
    if (!LogHas(EP_EDITBORDER_NOSCROLL, EPSN_FOCUSED) && !LogHas(EP_EDITTEXT, ETS_FOCUSED))
        DumpLog("edit focused");
    ok(LogHas(EP_EDITBORDER_NOSCROLL, EPSN_FOCUSED) || LogHas(EP_EDITTEXT, ETS_FOCUSED),
       "Expected focused edit border (EPSN_FOCUSED or ETS_FOCUSED)\n");

    SetFocus(g_hMainWnd);
    SendMessageW(hEdit, EM_SETREADONLY, TRUE, 0);
    MouseAway(hEdit);
    LogReset();
    FlushPaint(hEdit);
    if (haveBorder)
    {
        /* The specialized border parts have no read-only state; the frame draws EPSN_NORMAL */
        if (!LogHas(EP_EDITBORDER_NOSCROLL, EPSN_NORMAL)) DumpLog("edit readonly");
        ok(LogHas(EP_EDITBORDER_NOSCROLL, EPSN_NORMAL),
           "Expected EP_EDITBORDER_NOSCROLL/EPSN_NORMAL for read-only edit\n");
    }
    else
    {
        if (!LogHas(EP_EDITTEXT, ETS_READONLY)) DumpLog("edit readonly");
        ok(LogHas(EP_EDITTEXT, ETS_READONLY), "Expected EP_EDITTEXT/ETS_READONLY for read-only edit\n");
    }

    SendMessageW(hEdit, EM_SETREADONLY, FALSE, 0);
    EnableWindow(hEdit, FALSE);
    LogReset();
    FlushPaint(hEdit);
    if (!LogHas(EP_EDITTEXT, ETS_DISABLED) && !LogHas(EP_EDITBORDER_NOSCROLL, EPSN_DISABLED))
        DumpLog("edit disabled");
    ok(LogHas(EP_EDITTEXT, ETS_DISABLED) || LogHas(EP_EDITBORDER_NOSCROLL, EPSN_DISABLED),
       "Expected disabled edit state (ETS_DISABLED or EPSN_DISABLED)\n");

    DestroyWindow(hEdit);
}

static void Test_Combo(void)
{
    HWND hCombo;
    HTHEME theme;

    hCombo = CreateWindowExW(0, L"ComboBox", NULL,
                             WS_CHILD | WS_VISIBLE | CBS_DROPDOWN,
                             5, 40, 150, 200, g_hMainWnd, NULL, NULL, NULL);
    ok(hCombo != NULL, "Expected combobox creation to succeed\n");
    if (!hCombo)
        return;
    PumpMessages();

    theme = OpenThemeData(hCombo, L"Combobox");
    if (!theme)
    {
        skip("Combobox is not themed\n");
        DestroyWindow(hCombo);
        return;
    }

    if (IsThemePartDefined(theme, CP_BORDER, 0))
    {
        MouseOver(hCombo, 30, 10);
        LogReset();
        FlushPaint(hCombo);
        if (!LogHas(CP_BORDER, CBB_HOT)) DumpLog("combo border hot");
        ok(LogHas(CP_BORDER, CBB_HOT), "Expected CP_BORDER/CBB_HOT for hot dropdown combo\n");
    }
    else
    {
        skip("CP_BORDER not defined\n");
    }
    MouseAway(hCombo);
    DestroyWindow(hCombo);

    hCombo = CreateWindowExW(0, L"ComboBox", NULL,
                             WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                             5, 40, 150, 200, g_hMainWnd, NULL, NULL, NULL);
    ok(hCombo != NULL, "Expected droplist combobox creation to succeed\n");
    if (!hCombo)
    {
        CloseThemeData(theme);
        return;
    }
    PumpMessages();

    if (IsThemePartDefined(theme, CP_READONLY, 0))
    {
        MouseOver(hCombo, 30, 10);
        LogReset();
        FlushPaint(hCombo);
        if (!LogHas(CP_READONLY, CBB_HOT)) DumpLog("combo readonly hot");
        ok(LogHas(CP_READONLY, CBB_HOT), "Expected CP_READONLY hot state for hot droplist combo\n");
    }
    else
    {
        skip("CP_READONLY not defined\n");
    }
    MouseAway(hCombo);

    CloseThemeData(theme);
    DestroyWindow(hCombo);
}

static void Test_Tab(void)
{
    HWND hTab;
    HTHEME theme;
    TCITEMW item = { 0 };

    hTab = CreateWindowExW(0, WC_TABCONTROLW, NULL,
                           WS_CHILD | WS_VISIBLE,
                           5, 5, 260, 180, g_hMainWnd, NULL, NULL, NULL);
    ok(hTab != NULL, "Expected tab control creation to succeed\n");
    if (!hTab)
        return;
    PumpMessages();

    theme = GetWindowTheme(hTab);
    if (!theme)
        theme = OpenThemeData(hTab, L"Tab");
    if (!theme || !IsThemePartDefined(theme, TABP_TABITEM, 0))
    {
        skip("Tab is not themed\n");
        DestroyWindow(hTab);
        return;
    }

    item.mask = TCIF_TEXT;
    item.pszText = (LPWSTR)L"tab1";
    SendMessageW(hTab, TCM_INSERTITEMW, 0, (LPARAM)&item);
    FlushPaint(hTab);

    EnableWindow(hTab, FALSE);
    LogReset();
    FlushPaint(hTab);
    if (!LogHasPartRangeState(TABP_TABITEM, TABP_TOPTABITEMBOTHEDGE, TIS_DISABLED))
        DumpLog("tab disabled");
    ok(LogHasPartRangeState(TABP_TABITEM, TABP_TOPTABITEMBOTHEDGE, TIS_DISABLED),
       "Expected a tab item part with TIS_DISABLED for disabled tab control\n");

    DestroyWindow(hTab);
}

static void Test_Header(void)
{
    HWND hHeader;
    HTHEME theme;
    HDITEMW item = { 0 };
    int imageCount = 0;

    hHeader = CreateWindowExW(0, WC_HEADERW, NULL,
                              WS_CHILD | WS_VISIBLE | HDS_BUTTONS | HDS_HORZ,
                              5, 5, 260, 24, g_hMainWnd, NULL, NULL, NULL);
    ok(hHeader != NULL, "Expected header creation to succeed\n");
    if (!hHeader)
        return;
    PumpMessages();

    theme = GetWindowTheme(hHeader);
    if (!theme)
        theme = OpenThemeData(hHeader, L"Header");
    if (!theme || !IsThemePartDefined(theme, HP_HEADERITEM, 0))
    {
        skip("Header is not themed\n");
        DestroyWindow(hHeader);
        return;
    }

    item.mask = HDI_TEXT | HDI_FORMAT | HDI_WIDTH;
    item.pszText = (LPWSTR)L"col";
    item.cxy = 120;
    item.fmt = HDF_STRING | HDF_SORTUP;
    SendMessageW(hHeader, HDM_INSERTITEMW, 0, (LPARAM)&item);

    if (FAILED(GetThemeInt(theme, HP_HEADERITEM, HIS_NORMAL, TMT_IMAGECOUNT, &imageCount)) ||
        imageCount < HIS_SORTEDPRESSED)
    {
        skip("HeaderItem has no sorted states (count=%d)\n", imageCount);
        DestroyWindow(hHeader);
        return;
    }

    LogReset();
    FlushPaint(hHeader);
    if (!LogHas(HP_HEADERITEM, HIS_SORTEDNORMAL)) DumpLog("header sorted");
    ok(LogHas(HP_HEADERITEM, HIS_SORTEDNORMAL),
       "Expected HP_HEADERITEM/HIS_SORTEDNORMAL for HDF_SORTUP column\n");
    if (IsThemePartDefined(theme, HP_HEADERSORTARROW, 0))
    {
        if (!LogHas(HP_HEADERSORTARROW, HSAS_SORTEDUP)) DumpLog("header sortarrow");
        ok(LogHas(HP_HEADERSORTARROW, HSAS_SORTEDUP),
           "Expected HP_HEADERSORTARROW/HSAS_SORTEDUP for HDF_SORTUP column\n");
    }
    else
    {
        skip("HP_HEADERSORTARROW not defined\n");
    }

    DestroyWindow(hHeader);
}

static void Test_Tooltip(void)
{
    HWND hTT;
    HTHEME theme;
    TTTOOLINFOW ti = { 0 };

    hTT = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                          WS_POPUP | TTS_ALWAYSTIP | TTS_NOANIMATE | TTS_NOFADE,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                          g_hMainWnd, NULL, NULL, NULL);
    ok(hTT != NULL, "Expected tooltip creation to succeed\n");
    if (!hTT)
        return;
    PumpMessages();

    theme = GetWindowTheme(hTT);
    if (!theme)
        theme = OpenThemeData(hTT, L"Tooltip");
    if (!theme || !IsThemePartDefined(theme, TTP_STANDARD, 0))
    {
        skip("Tooltip is not themed\n");
        DestroyWindow(hTT);
        return;
    }

    ti.cbSize = TTTOOLINFOW_V1_SIZE;
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    ti.hwnd = g_hMainWnd;
    ti.uId = 1;
    ti.lpszText = (LPWSTR)L"tip text";
    SendMessageW(hTT, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    SendMessageW(hTT, TTM_TRACKPOSITION, 0, MAKELPARAM(120, 120));
    LogReset();
    SendMessageW(hTT, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    FlushPaint(hTT);
    if (!LogHas(TTP_STANDARD, TTSS_NORMAL)) DumpLog("tooltip standard");
    ok(LogHas(TTP_STANDARD, TTSS_NORMAL), "Expected TTP_STANDARD/TTSS_NORMAL for shown tooltip\n");

    SendMessageW(hTT, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
    DestroyWindow(hTT);
}

static int g_tdTimerTicks;

static HRESULT CALLBACK
TaskDialogCb(HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)
{
    if (uNotification == TDN_TIMER)
    {
        g_tdTimerTicks++;
        if (g_tdTimerTicks >= 2)
            SendMessageW(hwnd, TDM_CLICK_BUTTON, IDCANCEL, 0);
    }
    return S_OK;
}

static void Test_TaskDialog(void)
{
    TDI_FN pTDI;
    TASKDIALOGCONFIG config = { 0 };
    HTHEME theme;
    HRESULT hr;
    int btn = 0;

    pTDI = (TDI_FN)GetProcAddress(g_hComctl, "TaskDialogIndirect");
    if (!pTDI)
    {
        skip("TaskDialogIndirect not available\n");
        return;
    }

    theme = OpenThemeData(g_hMainWnd, L"TaskDialog");
    if (!theme || !IsThemePartDefined(theme, TDLG_EXPANDOBUTTON, 0))
    {
        skip("No themed TDLG_EXPANDOBUTTON (theme=%p)\n", theme);
        if (theme) CloseThemeData(theme);
        return;
    }
    CloseThemeData(theme);

    config.cbSize = sizeof(config);
    config.hwndParent = g_hMainWnd;
    config.dwFlags = TDF_CALLBACK_TIMER;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszMainInstruction = L"themestate";
    config.pszExpandedInformation = L"expanded information";
    config.pfCallback = TaskDialogCb;

    g_tdTimerTicks = 0;
    LogReset();
    hr = pTDI(&config, &btn, NULL, NULL);
    ok(hr == S_OK, "Expected TaskDialogIndirect to succeed, got 0x%lx\n", hr);
    if (hr != S_OK)
        return;
    if (!LogHasPart(TDLG_EXPANDOBUTTON)) DumpLog("taskdialog expando");
    ok(LogHasPart(TDLG_EXPANDOBUTTON), "Expected TDLG_EXPANDOBUTTON to be drawn themed\n");
}

START_TEST(themestate)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES };
    HTHEME probeTheme;

    InitCommonControlsEx(&icc);

    g_hMainWnd = CreateWindowExW(0, L"static", L"themestate",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 30, 30, 320, 260, NULL, NULL, NULL, NULL);
    ok(g_hMainWnd != NULL, "Expected main window creation to succeed\n");
    if (!g_hMainWnd)
        return;
    SetForegroundWindow(g_hMainWnd);
    FlushPaint(g_hMainWnd);

    /* The probe must come after window creation and painting: ReactOS gates
     * OpenThemeData on the per-process user api hook, which user32 loads
     * lazily on the first paint/nonclient path, not at process start */
    probeTheme = OpenThemeData(g_hMainWnd, L"Button");
    if (!probeTheme)
    {
        skip("Themes are not active\n");
        DestroyWindow(g_hMainWnd);
        return;
    }
    CloseThemeData(probeTheme);
    trace("IsAppThemed=%d IsThemeActive=%d\n", IsAppThemed(), IsThemeActive());

    if (!HookComctl())
    {
        skip("Could not hook comctl32's DrawThemeBackground import\n");
        DestroyWindow(g_hMainWnd);
        return;
    }

    Test_ListView();
    Test_TreeView();
    Test_Edit();
    Test_Combo();
    Test_Tab();
    Test_Header();
    Test_Tooltip();
    Test_TaskDialog();

    UnhookComctl();
    DestroyWindow(g_hMainWnd);
}
