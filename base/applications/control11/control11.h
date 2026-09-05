/*
 * PROJECT:     ReactOS Control Panel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows 7 style Control Panel host
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#define WIN32_NO_STATUS
#define COBJMACROS
#define NTOS_MODE_USER

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <wingdi.h>
#include <winreg.h>
#include <winnls.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shlguid.h>
#include <commctrl.h>
#include <windowsx.h>
#include <strsafe.h>
#include <objbase.h>
#include <propkey.h>
#include <powrprof.h>
#include <uxtheme.h>

#include "resource.h"

#define CP_MAX_ITEMS        96
#define CP_MAX_TASKS        256
#define CP_MAX_LINKS        512
#define CP_MAX_CATS         4
#define CP_MAX_HISTORY      64

enum CPHUB
{
    HUB_NONE = 0,
    HUB_SYSTEM,
    HUB_POWER,
    HUB_NETWORK,
    HUB_PERSONALIZATION,
};

typedef struct _CPITEM
{
    WCHAR szCanonical[64];
    WCHAR szName[128];
    WCHAR szInfoTip[512];
    GUID guid;
    BOOL bHasGuid;
    int categories[CP_MAX_CATS];
    int cCategories;
    PITEMID_CHILD pidl;
    HICON hIcon48;
    HICON hIcon32;
    HICON hIcon16;
    BOOL bEnabled;
    int hub;
} CPITEM;

typedef struct _CPTASK
{
    GUID id;
    GUID app;
    WCHAR szName[160];
    WCHAR szKeywords[512];
    WCHAR szCanonical[64];
    WCHAR szPage[64];
    WCHAR szCommand[520];
    int categories[CP_MAX_CATS];
    int cCategories;
    BOOL bEnabled;
} CPTASK;

typedef struct _CPCATEGORY
{
    int id;
    UINT nIcon;
    LPCWSTR pszName;
    LPCWSTR pszDescription;
    GUID homeTasks[8];
    int cHomeTasks;
} CPCATEGORY;

extern CPITEM g_Items[CP_MAX_ITEMS];
extern int g_cItems;
extern CPTASK g_Tasks[CP_MAX_TASKS];
extern int g_cTasks;
extern CPCATEGORY g_Categories[9];
extern IShellFolder2 *g_pControlsFolder;

void Catalog_Load(void);
CPITEM *Catalog_FindByCanonical(LPCWSTR pszCanonical);
CPITEM *Catalog_FindByGuid(REFGUID guid);
CPTASK *Catalog_FindTask(REFGUID id);
BOOL Catalog_ItemInCategory(const CPITEM *pItem, int cat);
BOOL Catalog_TaskInCategory(const CPTASK *pTask, int cat);
void Catalog_OpenItem(HWND hwnd, CPITEM *pItem, LPCWSTR pszPage);
void Catalog_RunTask(HWND hwnd, CPTASK *pTask);
BOOL Catalog_RunCommand(HWND hwnd, LPCWSTR pszCommand);
void Catalog_LaunchCpl(HWND hwnd, LPCWSTR pszCplAndArgs);
BOOL Catalog_MatchText(LPCWSTR pszHaystack, LPCWSTR pszQuery);
int Catalog_CategoryIndex(int id);

enum CPNAVKIND
{
    NV_HOME = 0,
    NV_CATEGORY,
    NV_ALL,
    NV_SEARCH,
    NV_HUB,
};

typedef struct _CPVIEW
{
    int kind;
    int category;
    int hub;
    WCHAR szPage[64];
    WCHAR szQuery[128];
    int scroll;
} CPNAVVIEW;

enum LINKACTION
{
    LA_NONE = 0,
    LA_HOME,
    LA_CATEGORY,
    LA_ALL,
    LA_ITEM,
    LA_TASK,
    LA_HUBPAGE,
    LA_COMMAND,
    LA_VIEWBY,
    LA_BACK,
    LA_FORWARD,
    LA_CUSTOM,
};

typedef struct _CPLINK
{
    RECT rc;
    int action;
    int param;
    void *ptr;
    WCHAR szArg[MAX_PATH];
    WCHAR szTip[256];
    BOOL bEnabled;
} CPLINK;

typedef struct _CPPALETTE
{
    BOOL dark;
    COLORREF Window;
    COLORREF NavPane;
    COLORREF NavBorder;
    COLORREF Text;
    COLORREF DimText;
    COLORREF Link;
    COLORREF LinkHot;
    COLORREF Disabled;
    COLORREF Header;
    COLORREF Bar;
    COLORREF BarBorder;
    COLORREF Edit;
    COLORREF Rule;
    COLORREF Accent;
} CPPALETTE;

typedef struct _CPFRAME
{
    HWND hwnd;
    HWND hwndSearch;
    HWND hwndTip;
    int dpi;
    CPPALETTE pal;
    HFONT hfText;
    HFONT hfLink;
    HFONT hfBold;
    HFONT hfHeader;
    HFONT hfTitle;
    HFONT hfSmall;
    CPNAVVIEW view;
    CPNAVVIEW history[CP_MAX_HISTORY];
    int cHistory;
    int iHistory;
    CPLINK links[CP_MAX_LINKS];
    int cLinks;
    int hotLink;
    int contentHeight;
    int contentTop;
    BOOL bLargeIcons;
    BOOL bViewMenuOpen;
    RECT rcContent;
    RECT rcNav;
    RECT rcBar;
} CPFRAME;

extern CPFRAME g_f;

int S(int px);
COLORREF Blend(COLORREF a, COLORREF b, int pctB);
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
HICON LoadFluent(UINT nId, int cx);
void DrawFluent(HDC hdc, const RECT *prc, UINT nId);
void DrawIconAt(HDC hdc, int x, int y, HICON hIcon, int cx, BOOL bEnabled);
CPLINK *AddLink(const RECT *prc, int action, int param, void *ptr, LPCWSTR pszArg, LPCWSTR pszTip, BOOL bEnabled);
int DrawLinkText(HDC hdc, int x, int y, int cxMax, LPCWSTR psz, HFONT hf, int action, int param, void *ptr, LPCWSTR pszArg, LPCWSTR pszTip, BOOL bEnabled);
int DrawWrapped(HDC hdc, int x, int y, int cx, LPCWSTR psz, HFONT hf, COLORREF cr);
int TextH(HDC hdc, HFONT hf);
int TextW(HDC hdc, HFONT hf, LPCWSTR psz);
void Frame_Navigate(const CPNAVVIEW *pView);
void Frame_Invalidate(void);
void Frame_OpenHub(int hub, LPCWSTR pszPage);
BOOL Frame_HandleCustom(int param, LPCWSTR pszArg);

int Hub_Paint(HDC hdc, const RECT *prc, int hub, LPCWSTR pszPage);
void Hub_PaintNavTasks(HDC hdc, int x, int *py, int cx, int hub, LPCWSTR pszPage);
BOOL Hub_Custom(HWND hwnd, int param, LPCWSTR pszArg);
void Hub_ViewChanged(int hub, LPCWSTR pszPage);
void Hub_Size(void);
LPCWSTR Hub_Title(int hub, LPCWSTR pszPage);
LPCWSTR Hub_Canonical(int hub);
