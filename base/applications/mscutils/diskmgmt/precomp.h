#ifndef __DISKMGMT_PRECOMP_H
#define __DISKMGMT_PRECOMP_H

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <wingdi.h>
#include <wincon.h>
#include <winerror.h>
#include <windowsx.h>

#include <commctrl.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <strsafe.h>
#include <wchar.h>
#include <wctype.h>

#include "resource.h"
#include "snapshot.h"
#include "snapshotmatch.h"
#include "volumelist.h"
#include "diskview.h"
#include "diskviewwnd.h"
#include "labels.h"
#include "properties.h"
#include "actions.h"
#include "mainwndaction.h"
#include "mainwndchrome.h"
#include "mainwndlayout.h"
#include "mainwndproc.h"
#include "layout.h"
#include "sort.h"
#include "splitter.h"
#include "menustate.h"
#include "contextmenu.h"

#ifdef _MSC_VER
#pragma warning(disable : 4100)
#endif

#define IDC_TOOLBAR      1000
#define IDC_STATUSBAR    1001
#define IDC_TREEVIEW     1002
#define IDC_VOLUMEVIEW   1003
#define IDC_DISKVIEW     1004

#define DISKMGMT_MIN_LEFT_PANE    160
#define DISKMGMT_LEFT_PANE_WIDTH  180
#define DISKMGMT_TOP_PANE_PERCENT 33
#define DISKMGMT_STATUS_PARTS     2

typedef struct _MENU_HINT
{
    WORD CmdId;
    UINT HintId;
} MENU_HINT, *PMENU_HINT;

typedef struct _MAIN_WND_INFO
{
    HWND hMainWnd;
    HWND hToolbar;
    HIMAGELIST hToolbarImageList;
    HIMAGELIST hVolumeImageList;
    HWND hStatusBar;
    HWND hTreeView;
    HWND hVolumeView;
    HWND hDiskView;
    HMENU hMenu;
    int nCmdShow;
    BOOL bInMenuLoop;
    BOOL bStatusBarVisible;
    DM_VOLUME_LIST_CONTEXT VolumeListContext;
    DM_MENU_STATE MenuState;
    DM_LAYOUT_METRICS LayoutMetrics;
    DM_SORT_CONTEXT SortContext;
    DM_SPLITTER_STATE SplitterState;
    INT DiskViewScrollPos;
    const DM_DISK *CommandDiskOverride;
    const DM_REGION *CommandRegionOverride;
    const DM_VOLUME *CommandVolumeOverride;
    BOOL HasCommandContextOverride;
    DM_VIEW_TARGET FocusTarget;
    DISKMGMT_SNAPSHOT Snapshot;
} MAIN_WND_INFO, *PMAIN_WND_INFO;

extern HINSTANCE hInstance;
extern HANDLE ProcessHeap;

BOOL
InitMainWindowImpl(VOID);

VOID
UninitMainWindowImpl(VOID);

HWND
CreateMainWindow(
    _In_z_ LPCTSTR lpCaption,
    _In_ int nCmdShow);

BOOL
StatusBarLoadString(
    _In_ HWND hStatusBar,
    _In_ INT PartId,
    _In_ HINSTANCE hInst,
    _In_ UINT uID);

VOID
ResourceMessageBox(
    _In_opt_ HWND hWnd,
    _In_ UINT uType,
    _In_ UINT uTextId);

#endif /* __DISKMGMT_PRECOMP_H */
