/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main-window chrome helpers.
 */

#include "precomp.h"

static const MENU_HINT DmMainMenuHintTable[] =
{
    { IDM_FILE_EXIT,               IDS_HINT_FILE_EXIT },
    { IDM_ACTION_REFRESH,          IDS_HINT_ACTION_REFRESH },
    { IDM_ACTION_RESCAN,           IDS_HINT_ACTION_RESCAN },
    { IDM_ACTION_INITIALIZE_DISK,  IDS_HINT_ACTION_INITIALIZE_DISK },
    { IDM_ACTION_CONVERT_GPT,      IDS_HINT_ACTION_CONVERT_GPT },
    { IDM_ACTION_CONVERT_MBR,      IDS_HINT_ACTION_CONVERT_MBR },
    { IDM_ACTION_CREATE_PARTITION, IDS_HINT_ACTION_CREATE_PARTITION },
    { IDM_ACTION_DELETE_VOLUME,    IDS_HINT_ACTION_DELETE_VOLUME },
    { IDM_ACTION_EXTEND_VOLUME,    IDS_HINT_ACTION_EXTEND_VOLUME },
    { IDM_ACTION_SHRINK_VOLUME,    IDS_HINT_ACTION_SHRINK_VOLUME },
    { IDM_ACTION_FORMAT,           IDS_HINT_ACTION_FORMAT },
    { IDM_ACTION_ASSIGN_LETTER,    IDS_HINT_ACTION_ASSIGN_LETTER },
    { IDM_ACTION_REMOVE_LETTER,    IDS_HINT_ACTION_REMOVE_LETTER },
    { IDM_ACTION_CHANGE_MOUNT_PATH, IDS_HINT_ACTION_CHANGE_MOUNT_PATH },
    { IDM_ACTION_REMOVE_MOUNT_PATH, IDS_HINT_ACTION_REMOVE_MOUNT_PATH },
    { IDM_ACTION_ONLINE,           IDS_HINT_ACTION_ONLINE },
    { IDM_ACTION_OFFLINE,          IDS_HINT_ACTION_OFFLINE },
    { IDM_ACTION_SET_READ_ONLY,    IDS_HINT_ACTION_SET_READ_ONLY },
    { IDM_ACTION_CLEAR_READ_ONLY,  IDS_HINT_ACTION_CLEAR_READ_ONLY },
    { IDM_ACTION_MARK_ACTIVE,      IDS_HINT_ACTION_MARK_ACTIVE },
    { IDM_ACTION_MARK_INACTIVE,    IDS_HINT_ACTION_MARK_INACTIVE },
    { IDM_ACTION_PROPERTIES,       IDS_HINT_ACTION_PROPERTIES },
    { IDM_VIEW_TOP,                IDS_HINT_VIEW_TOP },
    { IDM_VIEW_BOTTOM,             IDS_HINT_VIEW_BOTTOM },
    { IDM_VIEW_DISK_LIST,          IDS_HINT_VIEW_DISK_LIST },
    { IDM_HELP_ABOUT,              IDS_HINT_HELP_ABOUT }
};

static const MENU_HINT DmSystemMenuHintTable[] =
{
    { SC_CLOSE,    IDS_STATUS_READY },
    { SC_MOVE,     IDS_STATUS_READY },
    { SC_RESTORE,  IDS_STATUS_READY },
    { SC_SIZE,     IDS_STATUS_READY },
    { SC_MINIMIZE, IDS_STATUS_READY },
    { SC_MAXIMIZE, IDS_STATUS_READY }
};

enum
{
    DmToolbarImageBack = 0,
    DmToolbarImageForward,
    DmToolbarImageRescan,
    DmToolbarImageRefresh,
    DmToolbarImageCreate,
    DmToolbarImageDelete,
    DmToolbarImageProperties,
    DmToolbarImageHelp
};

typedef struct _DM_IMAGE_RESOURCE
{
    UINT ResourceId;
    COLORREF MaskColor;
} DM_IMAGE_RESOURCE, *PDM_IMAGE_RESOURCE;

static const DM_IMAGE_RESOURCE DmMainToolbarImages[] =
{
    { IDB_TB_BACK,       RGB(255, 0, 128) },
    { IDB_TB_FORWARD,    RGB(255, 0, 128) },
    { IDB_TB_RESCAN,     RGB(255, 0, 128) },
    { IDB_TB_REFRESH,    RGB(255, 0, 128) },
    { IDB_TB_CREATE,     RGB(255, 0, 128) },
    { IDB_TB_DELETE,     RGB(255, 0, 128) },
    { IDB_TB_PROPERTIES, RGB(255, 0, 128) },
    { IDB_TB_HELP,       RGB(255, 0, 128) }
};

static const DM_IMAGE_RESOURCE DmMainVolumeListImages[] =
{
    { IDB_LV_VOLUME,    RGB(255, 0, 128) },
    { IDB_LV_PARTITION, RGB(255, 0, 128) }
};

static const TBBUTTON DmMainToolbarButtons[] =
{
    { DmToolbarImageBack,       IDM_NAV_BACK,                TBSTATE_ENABLED, BTNS_BUTTON, { 0, 0 }, 0, 0 },
    { DmToolbarImageForward,    IDM_NAV_FORWARD,             TBSTATE_ENABLED, BTNS_BUTTON, { 0, 0 }, 0, 0 },
    { 0,                        0,                           TBSTATE_ENABLED, BTNS_SEP,    { 0, 0 }, 0, 0 },
    { DmToolbarImageRescan,     IDM_ACTION_RESCAN,           TBSTATE_ENABLED, BTNS_BUTTON, { 0, 0 }, 0, 0 },
    { DmToolbarImageRefresh,    IDM_ACTION_REFRESH,          TBSTATE_ENABLED, BTNS_BUTTON, { 0, 0 }, 0, 0 },
    { 0,                        0,                           TBSTATE_ENABLED, BTNS_SEP,    { 0, 0 }, 0, 0 },
    { DmToolbarImageCreate,     IDM_ACTION_CREATE_PARTITION, TBSTATE_ENABLED, BTNS_BUTTON, { 0, 0 }, 0, 0 },
    { DmToolbarImageDelete,     IDM_ACTION_DELETE_VOLUME,    TBSTATE_ENABLED, BTNS_BUTTON, { 0, 0 }, 0, 0 },
    { DmToolbarImageProperties, IDM_ACTION_PROPERTIES,       TBSTATE_ENABLED, BTNS_BUTTON, { 0, 0 }, 0, 0 },
    { 0,                        0,                           TBSTATE_ENABLED, BTNS_SEP,    { 0, 0 }, 0, 0 },
    { DmToolbarImageHelp,       IDM_HELP_ABOUT,              TBSTATE_ENABLED, BTNS_BUTTON, { 0, 0 }, 0, 0 }
};

static const UINT DmMainToolbarButtonTextIds[] =
{
    IDS_TOOLBAR_BACK,
    IDS_TOOLBAR_FORWARD,
    0,
    IDS_TOOLBAR_RESCAN,
    IDS_TOOLBAR_REFRESH,
    0,
    IDS_TOOLBAR_CREATE,
    IDS_TOOLBAR_DELETE,
    IDS_TOOLBAR_PROPERTIES,
    0,
    IDS_TOOLBAR_HELP
};

static
HIMAGELIST
DmMainWndCreateMaskedBitmapImageList(
    _In_reads_(ImageCount) const DM_IMAGE_RESOURCE *Images,
    _In_ UINT ImageCount)
{
    HIMAGELIST hImageList;
    UINT Index;

    if (Images == NULL || ImageCount == 0)
        return NULL;

    hImageList = ImageList_Create(16,
                                  16,
                                  ILC_MASK | ILC_COLOR32,
                                  ImageCount,
                                  0);
    if (hImageList == NULL)
        return NULL;

    for (Index = 0; Index < ImageCount; ++Index)
    {
        HBITMAP hBitmap;
        int addResult;

        hBitmap = (HBITMAP)LoadImageW(hInstance,
                                      MAKEINTRESOURCEW(Images[Index].ResourceId),
                                      IMAGE_BITMAP,
                                      16,
                                      16,
                                      LR_CREATEDIBSECTION);
        if (hBitmap == NULL)
        {
            ImageList_Destroy(hImageList);
            return NULL;
        }

        addResult = ImageList_AddMasked(hImageList,
                                        hBitmap,
                                        Images[Index].MaskColor);
        if (addResult == -1)
        {
            DeleteObject(hBitmap);
            ImageList_Destroy(hImageList);
            return NULL;
        }

        DeleteObject(hBitmap);
    }

    return hImageList;
}

static
HIMAGELIST
DmMainWndCreateToolbarImageList(VOID)
{
    return DmMainWndCreateMaskedBitmapImageList(DmMainToolbarImages,
                                                ARRAYSIZE(DmMainToolbarImages));
}

UINT
DmMainWndGetMainMenuHintStringId(
    _In_ UINT CommandId)
{
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(DmMainMenuHintTable); ++Index)
    {
        if (DmMainMenuHintTable[Index].CmdId == CommandId)
            return DmMainMenuHintTable[Index].HintId;
    }

    return 0;
}

BOOL
DmMainWndBuildDynamicHintText(
    _In_ PMAIN_WND_INFO Info,
    _In_ UINT CommandId,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    DM_ACTION_CONTEXT ActionContext;
    WCHAR Verb[64];

    if (Info == NULL || Buffer == NULL || cchBuffer == 0)
        return FALSE;

    Buffer[0] = UNICODE_NULL;

    DmMainWndBuildActionContext(Info, &ActionContext);
    DmActionBuildVerbText(&ActionContext, CommandId, Verb, ARRAYSIZE(Verb));
    switch (CommandId)
    {
        case IDM_ACTION_DELETE_VOLUME:
            if (_wcsicmp(Verb, L"Delete Partition") == 0)
                StringCchCopyW(Buffer, cchBuffer, L"Delete the selected partition");
            else
                StringCchCopyW(Buffer, cchBuffer, L"Delete the selected volume");
            return TRUE;

        case IDM_ACTION_CREATE_PARTITION:
            if (ActionContext.Disk != NULL &&
                ActionContext.Disk->PartitionStyle == PARTITION_STYLE_RAW)
            {
                StringCchCopyW(Buffer, cchBuffer, L"Initialize the selected disk before creating partitions or volumes");
                return TRUE;
            }
            break;

        default:
            break;
    }

    if (ActionContext.Volume != NULL && ActionContext.Volume->ExtentCount > 1)
    {
        StringCchPrintfW(Buffer,
                         cchBuffer,
                         L"%s is unavailable for multi-extent volumes",
                         Verb);
        return TRUE;
    }

    if ((ActionContext.Disk != NULL && ActionContext.Disk->IsDynamic) ||
        (ActionContext.Region != NULL && ActionContext.Region->IsDynamic) ||
        (ActionContext.Volume != NULL && ActionContext.Volume->IsDynamic))
    {
        StringCchPrintfW(Buffer,
                         cchBuffer,
                         L"%s is unavailable on dynamic / LDM storage",
                         Verb);
        return TRUE;
    }

    return FALSE;
}

static
BOOL
DmMainWndShowHint(
    _In_ PMAIN_WND_INFO Info,
    _In_ WORD CmdId,
    _In_reads_(HintCount) const MENU_HINT *HintArray,
    _In_ DWORD HintCount,
    _In_ UINT DefHintId)
{
    UINT HintId;
    DWORD Index;
    WCHAR DynamicHint[128];

    HintId = DefHintId;
    if (DmMainWndBuildDynamicHintText(Info, CmdId, DynamicHint, ARRAYSIZE(DynamicHint)))
    {
        return (BOOL)SendMessageW(Info->hStatusBar,
                                  SB_SETTEXTW,
                                  1,
                                  (LPARAM)DynamicHint);
    }

    for (Index = 0; Index < HintCount; ++Index)
    {
        if (HintArray[Index].CmdId == CmdId)
        {
            HintId = HintArray[Index].HintId;
            break;
        }
    }

    return StatusBarLoadString(Info->hStatusBar,
                               1,
                               hInstance,
                               HintId);
}

BOOL
DmMainWndShowCommandHint(
    _In_ PMAIN_WND_INFO Info,
    _In_ WORD CmdId,
    _In_ UINT DefHintId)
{
    return DmMainWndShowHint(Info,
                             CmdId,
                             DmMainMenuHintTable,
                             ARRAYSIZE(DmMainMenuHintTable),
                             DefHintId);
}

BOOL
DmMainWndShowSystemHint(
    _In_ PMAIN_WND_INFO Info,
    _In_ WORD CmdId,
    _In_ UINT DefHintId)
{
    return DmMainWndShowHint(Info,
                             CmdId,
                             DmSystemMenuHintTable,
                             ARRAYSIZE(DmSystemMenuHintTable),
                             DefHintId);
}

VOID
DmMainWndShowAboutDialog(
    _In_ HWND hWnd)
{
    WCHAR Copyright[96];
    WCHAR Text[512];

    if (LoadStringW(hInstance,
                    IDS_COPYRIGHT,
                    Copyright,
                    ARRAYSIZE(Copyright)) == 0)
    {
        Copyright[0] = UNICODE_NULL;
    }

    StringCchPrintfW(Text,
                     ARRAYSIZE(Text),
                     L"ReactOS Disk Management\n\n"
                     L"Current support:\n"
                     L"- Read-only disk and volume enumeration\n"
                     L"- Windows-style top list and bottom disk map\n"
                     L"- Selection sync, refresh, rescan, and properties\n"
                     L"- Initialize, empty-disk MBR/GPT conversion, create, delete, format, drive-letter, mount-path, and disk online/read-only operations\n"
                     L"- Primary, extended, and logical MBR partition create/delete, including removing an empty extended partition via its free space\n"
                     L"- Extend/shrink for single-extent basic volumes; the file system is resized online when its driver supports FSCTL extend/shrink\n"
                     L"- MBR active/inactive changes for supported primary partitions\n\n"
                     L"Dynamic/LDM status:\n"
                     L"- Dynamic disks and LDM volumes are detected and labeled\n"
                     L"- Layout-changing operations stay disabled on dynamic/LDM storage\n\n"
                     L"Integration model:\n"
                     L"- `diskmgmt.exe` remains a standalone tool for now\n"
                     L"- MMC / Computer Management integration is deferred until the MMC host grows real `.msc` support\n\n"
                     L"Not implemented yet:\n"
                     L"- Formatted-volume resize where the installed file system driver lacks online FSCTL extend/shrink support\n"
                     L"- Non-empty conversion flows and dynamic-disk / LDM mutation support\n"
                     L"- Full multi-extent layout classification\n\n"
                     L"%s",
                     Copyright);

    MessageBoxW(hWnd,
                Text,
                L"About Disk Management",
                MB_OK | MB_ICONINFORMATION);
}

VOID
DmMainWndUpdateStatusBarMode(
    _In_ PMAIN_WND_INFO Info)
{
    UNREFERENCED_PARAMETER(Info);
}

BOOL
DmMainWndCreateStatusBar(
    _In_ PMAIN_WND_INFO Info)
{
    INT Widths[DISKMGMT_STATUS_PARTS] = {130, -1};

    Info->hStatusBar = CreateWindowExW(0,
                                       STATUSCLASSNAMEW,
                                       NULL,
                                       WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                       0, 0, 0, 0,
                                       Info->hMainWnd,
                                       (HMENU)IDC_STATUSBAR,
                                       hInstance,
                                       NULL);
    if (Info->hStatusBar == NULL)
        return FALSE;

    SendMessageW(Info->hStatusBar,
                 SB_SETPARTS,
                 DISKMGMT_STATUS_PARTS,
                 (LPARAM)Widths);

    ShowWindow(Info->hStatusBar, SW_SHOW);
    Info->bStatusBarVisible = TRUE;
    SendMessageW(Info->hStatusBar,
                 SB_SETTEXTW,
                 0,
                 (LPARAM)L"Volumes: 0");
    StatusBarLoadString(Info->hStatusBar,
                        1,
                        hInstance,
                        IDS_STATUS_READY);
    return TRUE;
}

LRESULT
DmMainWndOnPaint(
    _In_ PMAIN_WND_INFO Info,
    _In_ HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc;
    RECT rcClient;
    DM_LAYOUT_PANES Panes;
    RECT LineRect;

    hdc = BeginPaint(hwnd, &ps);
    if (hdc == NULL)
        return 0;

    GetClientRect(hwnd, &rcClient);
    FillRect(hdc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));

    if (Info != NULL)
    {
        DmMainWndGetCurrentLayoutPanes(Info, &Panes);

        if (Panes.ToolbarHeight > 0)
        {
            LineRect.left = rcClient.left;
            LineRect.top = max(Panes.ToolbarRect.bottom - 1, rcClient.top);
            LineRect.right = rcClient.right;
            LineRect.bottom = min(LineRect.top + 1, rcClient.bottom);
            FillRect(hdc, &LineRect, GetSysColorBrush(COLOR_3DSHADOW));
        }

        if (Panes.HorizontalSplitterRect.bottom > Panes.HorizontalSplitterRect.top)
        {
            RECT SplitRect;

            SplitRect = Panes.HorizontalSplitterRect;
            FillRect(hdc, &SplitRect, GetSysColorBrush(COLOR_BTNFACE));

            LineRect = SplitRect;
            LineRect.bottom = min(LineRect.top + 1, SplitRect.bottom);
            FillRect(hdc, &LineRect, GetSysColorBrush(COLOR_3DSHADOW));

            LineRect = SplitRect;
            LineRect.top = max(LineRect.bottom - 1, SplitRect.top);
            FillRect(hdc, &LineRect, GetSysColorBrush(COLOR_3DHIGHLIGHT));
        }
    }

    EndPaint(hwnd, &ps);
    return 0;
}

BOOL
DmMainWndCreateToolbar(
    _In_ PMAIN_WND_INFO Info)
{
    TBBUTTON Buttons[ARRAYSIZE(DmMainToolbarButtons)];
    WCHAR ButtonText[ARRAYSIZE(DmMainToolbarButtons)][24];
    UINT Index;
    BOOL HasImages;

    Info->hToolbar = CreateWindowExW(0,
                                     TOOLBARCLASSNAMEW,
                                     NULL,
                                     WS_CHILD | WS_VISIBLE |
                                     CCS_TOP | CCS_NORESIZE |
                                     TBSTYLE_FLAT | TBSTYLE_TOOLTIPS,
                                     0, 0, 0, 0,
                                     Info->hMainWnd,
                                     (HMENU)IDC_TOOLBAR,
                                     hInstance,
                                     NULL);
    if (Info->hToolbar == NULL)
        return FALSE;

    SendMessageW(Info->hToolbar,
                 TB_BUTTONSTRUCTSIZE,
                 sizeof(TBBUTTON),
                 0);
    SendMessageW(Info->hToolbar,
                 TB_SETEXTENDEDSTYLE,
                 0,
                 TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_HIDECLIPPEDBUTTONS);
    SendMessageW(Info->hToolbar,
                 TB_SETBITMAPSIZE,
                 0,
                 MAKELPARAM(16, 16));
    SendMessageW(Info->hToolbar,
                 TB_SETBUTTONSIZE,
                 0,
                 MAKELPARAM(23, 22));
    SendMessageW(Info->hToolbar,
                 TB_SETPADDING,
                 0,
                 MAKELPARAM(3, 3));

    Info->hToolbarImageList = DmMainWndCreateToolbarImageList();
    HasImages = (Info->hToolbarImageList != NULL);

    if (HasImages)
    {
        ImageList_Destroy((HIMAGELIST)SendMessageW(Info->hToolbar,
                                                   TB_SETIMAGELIST,
                                                   0,
                                                   (LPARAM)Info->hToolbarImageList));
        SendMessageW(Info->hToolbar,
                     TB_SETMAXTEXTROWS,
                     0,
                     0);
    }

    CopyMemory(Buttons, DmMainToolbarButtons, sizeof(Buttons));
    ZeroMemory(ButtonText, sizeof(ButtonText));

    if (!HasImages)
    {
        LONG_PTR Style;

        Style = GetWindowLongPtrW(Info->hToolbar, GWL_STYLE);
        SetWindowLongPtrW(Info->hToolbar,
                          GWL_STYLE,
                          (Style | (LONG_PTR)TBSTYLE_LIST) & ~((LONG_PTR)TBSTYLE_FLAT));
        SendMessageW(Info->hToolbar,
                     TB_SETBUTTONWIDTH,
                     0,
                     MAKELONG(40, 56));
        SendMessageW(Info->hToolbar,
                     TB_SETBUTTONSIZE,
                     0,
                     MAKELPARAM(56, 22));
        SendMessageW(Info->hToolbar,
                     TB_SETPADDING,
                     0,
                     MAKELPARAM(6, 4));

        for (Index = 0; Index < ARRAYSIZE(Buttons); ++Index)
        {
            if (DmMainToolbarButtonTextIds[Index] == 0 ||
                (Buttons[Index].fsStyle & BTNS_SEP) != 0)
            {
                continue;
            }

            if (LoadStringW(hInstance,
                            DmMainToolbarButtonTextIds[Index],
                            ButtonText[Index],
                            ARRAYSIZE(ButtonText[Index])) == 0)
            {
                ButtonText[Index][0] = UNICODE_NULL;
            }

            Buttons[Index].iBitmap = I_IMAGENONE;
            Buttons[Index].fsStyle |= BTNS_SHOWTEXT;
            Buttons[Index].iString = (INT_PTR)ButtonText[Index];
        }

        SendMessageW(Info->hToolbar,
                     TB_SETMAXTEXTROWS,
                     1,
                     0);
    }

    SendMessageW(Info->hToolbar,
                 TB_ADDBUTTONSW,
                 ARRAYSIZE(Buttons),
                 (LPARAM)Buttons);

    /* Give the toolbar the parent's full width before TB_AUTOSIZE so it can
       compute a valid row height.  Created at 0x0 with CCS_NORESIZE, the
       control has no room to lay out buttons and returns height 0. */
    {
        RECT rcParent;
        GetClientRect(Info->hMainWnd, &rcParent);
        SetWindowPos(Info->hToolbar, NULL,
                     0, 0,
                     rcParent.right - rcParent.left, 26,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    }

    SendMessageW(Info->hToolbar,
                 TB_AUTOSIZE,
                 0,
                 0);

    return TRUE;
}

BOOL
DmMainWndCreateVolumeView(
    _In_ PMAIN_WND_INFO Info)
{
    Info->hVolumeView = CreateWindowExW(WS_EX_CLIENTEDGE,
                                        WC_LISTVIEWW,
                                        NULL,
                                        WS_CHILD | WS_VISIBLE | LVS_REPORT |
                                        LVS_SHOWSELALWAYS | WS_TABSTOP,
                                        0, 0, 0, 0,
                                        Info->hMainWnd,
                                        (HMENU)IDC_VOLUMEVIEW,
                                        hInstance,
                                        NULL);
    if (Info->hVolumeView == NULL)
        return FALSE;

    ListView_SetExtendedListViewStyle(Info->hVolumeView,
                                      LVS_EX_FULLROWSELECT |
                                      LVS_EX_HEADERDRAGDROP |
                                      LVS_EX_LABELTIP |
                                      LVS_EX_DOUBLEBUFFER);

    DmVolumeListInsertColumns(Info->hVolumeView, hInstance);

    Info->hVolumeImageList = DmMainWndCreateMaskedBitmapImageList(DmMainVolumeListImages,
                                                                  ARRAYSIZE(DmMainVolumeListImages));
    if (Info->hVolumeImageList != NULL)
    {
        ImageList_Destroy((HIMAGELIST)ListView_SetImageList(Info->hVolumeView,
                                                            Info->hVolumeImageList,
                                                            LVSIL_SMALL));
    }

    return TRUE;
}
