/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main-window action and menu-state helpers.
 */

#include "precomp.h"

static
DM_MENU_FOCUS
DmMainWndMenuFocusFromViewTarget(
    _In_ DM_VIEW_TARGET Target)
{
    switch (Target)
    {
        case DmViewTargetTop:
            return DmMenuFocusTop;

        case DmViewTargetBottom:
            return DmMenuFocusBottom;

        case DmViewTargetDiskList:
            return DmMenuFocusDiskList;

        default:
            return DmMenuFocusNone;
    }
}

static
VOID
DmMainWndUpdateDynamicActionMenuText(
    _In_ PMAIN_WND_INFO Info)
{
    MENUITEMINFOW ItemInfo;
    DM_ACTION_CONTEXT ActionContext;
    WCHAR Verb[64];
    WCHAR MenuText[64];

    if (Info == NULL || Info->hMenu == NULL)
        return;

    DmMainWndBuildActionContext(Info, &ActionContext);

    DmActionBuildVerbText(&ActionContext, IDM_ACTION_CREATE_PARTITION, Verb, ARRAYSIZE(Verb));
    if (_wcsicmp(Verb, L"Create Logical Drive") == 0)
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"Create &Logical Drive...");
    else if (_wcsicmp(Verb, L"Create Volume") == 0)
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"Create &Volume...");
    else
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"Cre&ate Partition...");

    ZeroMemory(&ItemInfo, sizeof(ItemInfo));
    ItemInfo.cbSize = sizeof(ItemInfo);
    ItemInfo.fMask = MIIM_STRING;
    ItemInfo.dwTypeData = MenuText;
    SetMenuItemInfoW(Info->hMenu, IDM_ACTION_CREATE_PARTITION, FALSE, &ItemInfo);

    DmActionBuildVerbText(&ActionContext, IDM_ACTION_DELETE_VOLUME, Verb, ARRAYSIZE(Verb));
    if (_wcsicmp(Verb, L"Delete Partition") == 0)
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"Delete &Partition");
    else
        StringCchCopyW(MenuText, ARRAYSIZE(MenuText), L"De&lete Volume");

    ZeroMemory(&ItemInfo, sizeof(ItemInfo));
    ItemInfo.cbSize = sizeof(ItemInfo);
    ItemInfo.fMask = MIIM_STRING;
    ItemInfo.dwTypeData = MenuText;
    SetMenuItemInfoW(Info->hMenu, IDM_ACTION_DELETE_VOLUME, FALSE, &ItemInfo);
}

static
VOID
DmMainWndUpdateSummaryText(
    _In_ PMAIN_WND_INFO Info)
{
    WCHAR Buffer[64];

    StringCchPrintfW(Buffer,
                     ARRAYSIZE(Buffer),
                     L"%lu disks, %lu volumes",
                     Info->Snapshot.DiskCount,
                     Info->Snapshot.VolumeCount);

    SendMessageW(Info->hStatusBar,
                 SB_SETTEXTW,
                 0,
                 (LPARAM)Buffer);
}

VOID
DmMainWndCacheSelectedVolumeName(
    _In_ PMAIN_WND_INFO Info)
{
    DmVolumeListCacheSelection(&Info->VolumeListContext,
                               Info->hVolumeView);
}

VOID
DmMainWndRestoreSelectedVolume(
    _In_ PMAIN_WND_INFO Info)
{
    DmVolumeListRestoreSelection(&Info->VolumeListContext,
                                 Info->hVolumeView,
                                 &Info->Snapshot);
}

VOID
DmMainWndBuildActionContext(
    _In_ PMAIN_WND_INFO Info,
    _Out_ PDM_ACTION_CONTEXT Context)
{
    ZeroMemory(Context, sizeof(*Context));
    Context->hWnd = Info->hMainWnd;
    Context->hStatusBar = Info->hStatusBar;
    Context->Snapshot = &Info->Snapshot;

    if (Info->HasCommandContextOverride)
    {
        Context->Disk = (PDM_DISK)Info->CommandDiskOverride;
        Context->Region = (PDM_REGION)Info->CommandRegionOverride;
        Context->Volume = (PDM_VOLUME)Info->CommandVolumeOverride;
    }
    else
    {
        Context->Volume = DmMainWndGetSelectedVolume(Info);

        if (Context->Volume != NULL)
        {
            Context->Disk = Context->Volume->Disk;
            Context->Region = Context->Volume->Region;
        }
    }
}

VOID
DmMainWndUpdateMenuState(
    _In_ PMAIN_WND_INFO Info)
{
    DM_ACTION_CONTEXT ActionContext;

    if (Info == NULL)
        return;

    DmMainWndBuildActionContext(Info, &ActionContext);
    DmMenuStateSetActionContext(&Info->MenuState, &ActionContext);
    DmMenuStateSetFocus(&Info->MenuState,
                        DmMainWndMenuFocusFromViewTarget(Info->FocusTarget),
                        DmLayoutViewCommandFromTarget(Info->FocusTarget));

    if (Info->hMenu != NULL)
    {
        DmMainWndUpdateDynamicActionMenuText(Info);
        DmMenuStateApplyMenu(&Info->MenuState, Info->hMenu);
        DrawMenuBar(Info->hMainWnd);
    }

    if (Info->hToolbar != NULL)
    {
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_NAV_BACK, MAKELONG(FALSE, 0));
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_NAV_FORWARD, MAKELONG(FALSE, 0));
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_ACTION_RESCAN, MAKELONG(TRUE, 0));
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_ACTION_REFRESH, MAKELONG(TRUE, 0));
        SendMessageW(Info->hToolbar,
                     TB_ENABLEBUTTON,
                     IDM_ACTION_CREATE_PARTITION,
                     MAKELONG(DmActionIsAvailable(&ActionContext, IDM_ACTION_CREATE_PARTITION), 0));
        SendMessageW(Info->hToolbar,
                     TB_ENABLEBUTTON,
                     IDM_ACTION_DELETE_VOLUME,
                     MAKELONG(DmActionIsAvailable(&ActionContext, IDM_ACTION_DELETE_VOLUME), 0));
        SendMessageW(Info->hToolbar,
                     TB_ENABLEBUTTON,
                     IDM_ACTION_PROPERTIES,
                     MAKELONG(DmActionIsAvailable(&ActionContext, IDM_ACTION_PROPERTIES), 0));
        SendMessageW(Info->hToolbar, TB_ENABLEBUTTON, IDM_HELP_ABOUT, MAKELONG(TRUE, 0));
        InvalidateRect(Info->hToolbar, NULL, TRUE);
    }
}

VOID
DmMainWndPopulateVolumeList(
    _In_ PMAIN_WND_INFO Info)
{
    DmVolumeListPopulate(Info->hVolumeView, &Info->Snapshot);
    DmSortApplyToListView(Info->hVolumeView, &Info->SortContext);
    DmMainWndUpdateSummaryText(Info);
    DmMainWndRestoreSelectedVolume(Info);
    DmMainWndUpdateMenuState(Info);
}

BOOL
DmMainWndExecuteCommand(
    _In_ PMAIN_WND_INFO Info,
    _In_ UINT CommandId)
{
    HCURSOR hWait;
    HCURSOR hOldCursor;
    DM_ACTION_CONTEXT ActionContext;
    BOOL RefreshesSnapshot;
    BOOL Success;
    UINT StatusId;

    if (Info == NULL)
        return FALSE;

    DmMainWndBuildActionContext(Info, &ActionContext);
    RefreshesSnapshot = (CommandId == IDM_ACTION_REFRESH ||
                         CommandId == IDM_ACTION_RESCAN ||
                         DmActionMutatesSnapshot(CommandId));
    StatusId = DmActionGetStatusStringId(CommandId);

    if (RefreshesSnapshot)
        DmMainWndCacheSelectedVolumeName(Info);

    if (StatusId != 0)
        StatusBarLoadString(Info->hStatusBar, 1, hInstance, StatusId);

    hWait = LoadCursorW(NULL, IDC_WAIT);
    hOldCursor = SetCursor(hWait);
    Success = DmActionExecute(&ActionContext, CommandId);
    SetCursor(hOldCursor);

    if (!Success)
    {
        StatusBarLoadString(Info->hStatusBar, 1, hInstance, IDS_STATUS_READY);
        return FALSE;
    }

    if (RefreshesSnapshot)
    {
        DmMainWndPopulateVolumeList(Info);
        InvalidateRect(Info->hDiskView, NULL, TRUE);
        DmMainWndUpdateDiskViewScrollInfo(Info);
    }

    DmMainWndUpdateMenuState(Info);
    StatusBarLoadString(Info->hStatusBar, 1, hInstance, IDS_STATUS_READY);
    return TRUE;
}
