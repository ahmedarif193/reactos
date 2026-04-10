/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main-window procedure and event helpers.
 */

#include "precomp.h"

PDM_VOLUME
DmMainWndGetSelectedVolume(
    _In_ PMAIN_WND_INFO Info)
{
    return DmVolumeListGetSelectedVolume(Info->hVolumeView);
}

VOID
DmMainWndSetFocusTarget(
    _In_ PMAIN_WND_INFO Info,
    _In_ DM_VIEW_TARGET Target)
{
    Info->FocusTarget = Target;
    DmMainWndUpdateMenuState(Info);
}

static
BOOL
FocusViewTarget(
    _In_ PMAIN_WND_INFO Info,
    _In_ DM_VIEW_TARGET Target)
{
    HWND hTarget;
    DM_VIEW_TARGET EffectiveTarget;

    EffectiveTarget = Target;
    hTarget = NULL;
    switch (Target)
    {
        case DmViewTargetTop:
            hTarget = Info->hVolumeView;
            break;

        case DmViewTargetBottom:
            hTarget = Info->hDiskView;
            break;

        case DmViewTargetDiskList:
            hTarget = Info->hVolumeView;
            EffectiveTarget = DmViewTargetTop;
            break;

        default:
            break;
    }

    if (hTarget == NULL)
        return FALSE;

    SetFocus(hTarget);
    DmMainWndSetFocusTarget(Info, EffectiveTarget);
    return TRUE;
}

static
VOID
UpdateVolumeSelectionFromScreenPoint(
    _In_ PMAIN_WND_INFO Info,
    _In_ INT x,
    _In_ INT y)
{
    POINT Point;
    LVHITTESTINFO HitInfo;
    INT ItemIndex;

    if (x == -1 && y == -1)
        return;

    Point.x = x;
    Point.y = y;
    ScreenToClient(Info->hVolumeView, &Point);

    ZeroMemory(&HitInfo, sizeof(HitInfo));
    HitInfo.pt = Point;
    ItemIndex = ListView_HitTest(Info->hVolumeView, &HitInfo);

    ListView_SetItemState(Info->hVolumeView,
                          -1,
                          0,
                          LVIS_SELECTED | LVIS_FOCUSED);

    if (ItemIndex >= 0)
    {
        ListView_SetItemState(Info->hVolumeView,
                              ItemIndex,
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(Info->hVolumeView, ItemIndex, FALSE);
    }

    DmMainWndCacheSelectedVolumeName(Info);
    DmMainWndUpdateMenuState(Info);
    InvalidateRect(Info->hDiskView, NULL, TRUE);
}

VOID
DmMainWndUpdateVolumeSelectionFromDiskViewPoint(
    _In_ PMAIN_WND_INFO Info,
    _In_ INT x,
    _In_ INT y)
{
    RECT ClientRect;
    POINT Point;
    const DM_DISK *Disk;
    const DM_REGION *Region;
    PDM_VOLUME Volume;

    if (Info == NULL || Info->hDiskView == NULL)
        return;

    if (x == -1 && y == -1)
        return;

    Point.x = x;
    Point.y = y;
    ScreenToClient(Info->hDiskView, &Point);
    GetClientRect(Info->hDiskView, &ClientRect);

    if (!DmDiskViewHitTest(&ClientRect,
                           &Info->Snapshot,
                           Info->DiskViewScrollPos,
                           &Point,
                           &Disk,
                           &Region))
    {
        return;
    }

    UNREFERENCED_PARAMETER(Disk);
    Volume = (Region != NULL) ? Region->Volume : NULL;
    if (Volume != NULL && DmVolumeListSelectVolume(Info->hVolumeView, Volume))
    {
        DmMainWndCacheSelectedVolumeName(Info);
        DmMainWndUpdateMenuState(Info);
        InvalidateRect(Info->hDiskView, NULL, TRUE);
    }
}

static
LRESULT
OnContextMenu(
    _In_ PMAIN_WND_INFO Info,
    _In_ HWND hSource,
    _In_ INT x,
    _In_ INT y)
{
    DM_ACTION_CONTEXT ActionContext;
    HMENU Menu;
    UINT CommandId;
    POINT Point;
    const DM_DISK *Disk;
    const DM_REGION *Region;
    const DM_VOLUME *Volume;

    if (Info == NULL)
        return 0;

    if (hSource == Info->hVolumeView)
        UpdateVolumeSelectionFromScreenPoint(Info, x, y);

    if (x == -1 && y == -1)
    {
        GetCursorPos(&Point);
    }
    else
    {
        Point.x = x;
        Point.y = y;
    }

    DmMainWndBuildActionContext(Info, &ActionContext);

    if (hSource == Info->hVolumeView)
    {
        Disk = ActionContext.Disk;
        Region = ActionContext.Region;
        Volume = ActionContext.Volume;
    }
    else if (hSource == Info->hDiskView && x != -1 && y != -1)
    {
        POINT ClientPoint;
        RECT ClientRect;

        ClientPoint.x = x;
        ClientPoint.y = y;
        ScreenToClient(Info->hDiskView, &ClientPoint);
        GetClientRect(Info->hDiskView, &ClientRect);

        if (!DmDiskViewHitTest(&ClientRect,
                               &Info->Snapshot,
                               Info->DiskViewScrollPos,
                               &ClientPoint,
                               &Disk,
                               &Region))
        {
            Disk = ActionContext.Disk;
            Region = ActionContext.Region;
        }

        Volume = (Region != NULL) ? Region->Volume : ActionContext.Volume;
    }
    else
    {
        Disk = NULL;
        Region = NULL;
        Volume = NULL;
    }

    Menu = DmCreateContextMenu(&Info->Snapshot, Disk, Region, Volume);
    if (Menu == NULL)
        return 0;

    DmMenuStateApplyMenu(&Info->MenuState, Menu);
    CommandId = TrackPopupMenuEx(Menu,
                                 TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                 Point.x,
                                 Point.y,
                                 Info->hMainWnd,
                                 NULL);
    DmDestroyContextMenu(Menu);

    if (CommandId != 0)
    {
        Info->CommandDiskOverride = Disk;
        Info->CommandRegionOverride = Region;
        Info->CommandVolumeOverride = Volume;
        Info->HasCommandContextOverride = TRUE;
        SendMessageW(Info->hMainWnd,
                     WM_COMMAND,
                     MAKEWPARAM(CommandId, 0),
                     0);
        Info->HasCommandContextOverride = FALSE;
        Info->CommandDiskOverride = NULL;
        Info->CommandRegionOverride = NULL;
        Info->CommandVolumeOverride = NULL;
    }

    return 0;
}

static
LRESULT
OnToolbarCustomDraw(
    _In_ PMAIN_WND_INFO Info,
    _In_ LPNMTBCUSTOMDRAW TbCd)
{
    switch (TbCd->nmcd.dwDrawStage)
    {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;

        case CDDS_ITEMPREPAINT:
            if (!(TbCd->nmcd.uItemState & CDIS_DISABLED))
                return CDRF_DODEFAULT;

            /* ReactOS comctl32 does not paint icons for disabled flat
               toolbar buttons.  Draw the icon as grayscale ourselves. */
            if (Info->hToolbarImageList != NULL)
            {
                TBBUTTONINFOW tbi;
                RECT rc;

                ZeroMemory(&tbi, sizeof(tbi));
                tbi.cbSize = sizeof(tbi);
                tbi.dwMask = TBIF_IMAGE;
                if (SendMessageW(Info->hToolbar,
                                 TB_GETBUTTONINFOW,
                                 TbCd->nmcd.dwItemSpec,
                                 (LPARAM)&tbi) != -1 &&
                    tbi.iImage >= 0)
                {
                    HDC hdcMem;
                    HBITMAP hbm, hbmOld;
                    BITMAPINFO bmi;
                    BYTE Pixels[16 * 16 * 4];
                    INT px, py;
                    COLORREF BtnFace;
                    BYTE BfR, BfG, BfB;
                    INT dx, dy;

                    rc = TbCd->nmcd.rc;
                    dx = rc.left + (rc.right - rc.left - 16) / 2;
                    dy = rc.top + (rc.bottom - rc.top - 16) / 2;
                    BtnFace = GetSysColor(COLOR_BTNFACE);
                    BfR = GetRValue(BtnFace);
                    BfG = GetGValue(BtnFace);
                    BfB = GetBValue(BtnFace);

                    hdcMem = CreateCompatibleDC(TbCd->nmcd.hdc);
                    hbm = CreateCompatibleBitmap(TbCd->nmcd.hdc, 16, 16);
                    hbmOld = (HBITMAP)SelectObject(hdcMem, hbm);

                    /* Fill with button face, then draw icon on top. */
                    {
                        RECT FillRc = { 0, 0, 16, 16 };
                        FillRect(hdcMem, &FillRc, GetSysColorBrush(COLOR_BTNFACE));
                    }
                    ImageList_Draw(Info->hToolbarImageList,
                                   tbi.iImage,
                                   hdcMem,
                                   0, 0,
                                   ILD_TRANSPARENT);

                    /* Read pixels, convert to grayscale, lighten. */
                    ZeroMemory(&bmi, sizeof(bmi));
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = 16;
                    bmi.bmiHeader.biHeight = -16;  /* top-down */
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;

                    if (GetDIBits(hdcMem, hbm, 0, 16, Pixels, &bmi, DIB_RGB_COLORS))
                    {
                        for (py = 0; py < 16; py++)
                        {
                            for (px = 0; px < 16; px++)
                            {
                                INT off = (py * 16 + px) * 4;
                                BYTE b = Pixels[off + 0];
                                BYTE g = Pixels[off + 1];
                                BYTE r = Pixels[off + 2];
                                BYTE gray;

                                /* Skip background pixels. */
                                if (r == BfR && g == BfG && b == BfB)
                                    continue;

                                /* Luminance, then blend 60% toward button face. */
                                gray = (BYTE)((r * 30 + g * 59 + b * 11) / 100);
                                Pixels[off + 0] = (BYTE)((gray * 40 + BfB * 60) / 100);
                                Pixels[off + 1] = (BYTE)((gray * 40 + BfG * 60) / 100);
                                Pixels[off + 2] = (BYTE)((gray * 40 + BfR * 60) / 100);
                            }
                        }
                        SetDIBits(hdcMem, hbm, 0, 16, Pixels, &bmi, DIB_RGB_COLORS);
                    }

                    BitBlt(TbCd->nmcd.hdc, dx, dy, 16, 16, hdcMem, 0, 0, SRCCOPY);
                    SelectObject(hdcMem, hbmOld);
                    DeleteObject(hbm);
                    DeleteDC(hdcMem);
                }
            }
            return CDRF_SKIPDEFAULT;

        default:
            return CDRF_DODEFAULT;
    }
}

LRESULT
OnNotify(
    _In_ PMAIN_WND_INFO Info,
    _In_ LPARAM lParam)
{
    LPNMHDR NmHdr;

    NmHdr = (LPNMHDR)lParam;

    /* Toolbar custom draw: paint disabled icons as grayed. */
    if (NmHdr->hwndFrom == Info->hToolbar && NmHdr->code == NM_CUSTOMDRAW)
        return OnToolbarCustomDraw(Info, (LPNMTBCUSTOMDRAW)lParam);

    if (NmHdr->code == TTN_GETDISPINFOW || NmHdr->code == TTN_NEEDTEXTW)
    {
        LPNMTTDISPINFOW InfoW;
        UINT HintId;
        WCHAR DynamicHint[80];

        if (DmMainWndBuildDynamicHintText(Info,
                                          (UINT)NmHdr->idFrom,
                                          DynamicHint,
                                          ARRAYSIZE(DynamicHint)))
        {
            InfoW = (LPNMTTDISPINFOW)lParam;
            StringCchCopyW(InfoW->szText, ARRAYSIZE(InfoW->szText), DynamicHint);
            InfoW->lpszText = InfoW->szText;
            return TRUE;
        }

        HintId = DmMainWndGetMainMenuHintStringId((UINT)NmHdr->idFrom);
        if (HintId != 0)
        {
            InfoW = (LPNMTTDISPINFOW)lParam;
            InfoW->hinst = hInstance;
            InfoW->lpszText = MAKEINTRESOURCEW(HintId);
            return TRUE;
        }
    }

    if (NmHdr->hwndFrom == Info->hVolumeView)
    {
        switch (NmHdr->code)
        {
            case NM_SETFOCUS:
                DmMainWndSetFocusTarget(Info, DmViewTargetTop);
                break;

            case LVN_ITEMCHANGED:
            {
                LPNMLISTVIEW ListView = (LPNMLISTVIEW)lParam;

                if ((ListView->uChanged & LVIF_STATE) != 0)
                {
                    DmMainWndCacheSelectedVolumeName(Info);
                    DmMainWndUpdateMenuState(Info);
                    InvalidateRect(Info->hDiskView, NULL, TRUE);
                }
                break;
            }

            case LVN_COLUMNCLICK:
            {
                LPNMLISTVIEW ListView;

                ListView = (LPNMLISTVIEW)lParam;
                DmMainWndCacheSelectedVolumeName(Info);
                DmSortHandleColumnClick(Info->hVolumeView,
                                        &Info->SortContext,
                                        ListView->iSubItem);
                DmMainWndRestoreSelectedVolume(Info);
                InvalidateRect(Info->hDiskView, NULL, TRUE);
                break;
            }

            case NM_DBLCLK:
            {
                PDM_VOLUME Volume;

                Volume = DmMainWndGetSelectedVolume(Info);
                if (Volume != NULL)
                    DmShowVolumeProperties(Info->hMainWnd, Volume);
                break;
            }

            default:
                break;
        }
    }

    return 0;
}

static
LRESULT
OnCreate(
    _In_ PMAIN_WND_INFO Info)
{
    WCHAR DbgBuf[256];

    OutputDebugStringW(L"[DISKMGMT] OnCreate: ENTER\n");

    Info->hMenu = GetMenu(Info->hMainWnd);
    DmSnapshotInitialize(&Info->Snapshot);
    DmVolumeListInitializeContext(&Info->VolumeListContext);
    DmMenuStateInitialize(&Info->MenuState);
    DmLayoutSetDefaults(&Info->LayoutMetrics);
    DmSortInitializeContext(&Info->SortContext);
    DmSplitterInitializeState(&Info->SplitterState);
    Info->LayoutMetrics.NavigationVisible = FALSE;
    Info->FocusTarget = DmViewTargetTop;

    if (!DmMainWndCreateStatusBar(Info))
    {
        OutputDebugStringW(L"[DISKMGMT] CreateStatusBar FAILED\n");
        return -1;
    }
    OutputDebugStringW(L"[DISKMGMT] StatusBar OK\n");

    if (!DmMainWndCreateToolbar(Info))
    {
        OutputDebugStringW(L"[DISKMGMT] CreateToolbar FAILED\n");
        return -1;
    }

    StringCchPrintfW(DbgBuf, ARRAYSIZE(DbgBuf),
                     L"[DISKMGMT] Toolbar OK: hwnd=%p visible=%d\n",
                     (void *)Info->hToolbar,
                     IsWindowVisible(Info->hToolbar));
    OutputDebugStringW(DbgBuf);

    if (!DmMainWndCreateVolumeView(Info))
    {
        OutputDebugStringW(L"[DISKMGMT] CreateVolumeView FAILED\n");
        return -1;
    }
    OutputDebugStringW(L"[DISKMGMT] VolumeView OK\n");

    if (!DmMainWndCreateDiskView(Info))
    {
        OutputDebugStringW(L"[DISKMGMT] CreateDiskView FAILED\n");
        return -1;
    }
    OutputDebugStringW(L"[DISKMGMT] DiskView OK\n");

    DmMainWndLayoutMainWindow(Info);
    OutputDebugStringW(L"[DISKMGMT] Layout done, starting refresh\n");

    DmMainWndExecuteCommand(Info, IDM_ACTION_REFRESH);
    DmMainWndUpdateMenuState(Info);
    OutputDebugStringW(L"[DISKMGMT] OnCreate: DONE\n");
    return 0;
}

static
LRESULT
OnCommand(
    _In_ PMAIN_WND_INFO Info,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (LOWORD(wParam))
    {
        case IDM_FILE_EXIT:
            DestroyWindow(Info->hMainWnd);
            return 0;

        case IDM_NAV_BACK:
        case IDM_NAV_FORWARD:
            return 0;

        case IDM_ACTION_REFRESH:
        case IDM_ACTION_RESCAN:
            DmMainWndExecuteCommand(Info, LOWORD(wParam));
            return 0;

        case IDM_ACTION_INITIALIZE_DISK:
        case IDM_ACTION_CONVERT_GPT:
        case IDM_ACTION_CONVERT_MBR:
        case IDM_ACTION_CREATE_PARTITION:
        case IDM_ACTION_DELETE_VOLUME:
        case IDM_ACTION_FORMAT:
        case IDM_ACTION_ASSIGN_LETTER:
        case IDM_ACTION_REMOVE_LETTER:
        case IDM_ACTION_CHANGE_MOUNT_PATH:
        case IDM_ACTION_REMOVE_MOUNT_PATH:
        case IDM_ACTION_ONLINE:
        case IDM_ACTION_OFFLINE:
        case IDM_ACTION_SET_READ_ONLY:
        case IDM_ACTION_CLEAR_READ_ONLY:
        case IDM_ACTION_MARK_ACTIVE:
        case IDM_ACTION_MARK_INACTIVE:
        case IDM_ACTION_PROPERTIES:
            DmMainWndExecuteCommand(Info, LOWORD(wParam));
            return 0;

        case IDM_VIEW_TOP:
            FocusViewTarget(Info, DmViewTargetTop);
            return 0;

        case IDM_VIEW_BOTTOM:
            FocusViewTarget(Info, DmViewTargetBottom);
            return 0;

        case IDM_VIEW_DISK_LIST:
            FocusViewTarget(Info, DmViewTargetDiskList);
            return 0;

        case IDM_HELP_ABOUT:
            DmMainWndShowAboutDialog(Info->hMainWnd);
            return 0;

        default:
            break;
    }

    return 1;
}

LRESULT CALLBACK
DmMainWndProc(
    _In_ HWND hwnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PMAIN_WND_INFO Info;
    CREATESTRUCTW *CreateInfo;

    Info = (PMAIN_WND_INFO)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_NCCREATE:
            CreateInfo = (CREATESTRUCTW *)lParam;
            Info = (PMAIN_WND_INFO)CreateInfo->lpCreateParams;
            Info->hMainWnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)Info);
            return TRUE;

        case WM_CREATE:
            return OnCreate(Info);

        case WM_SIZE:
            DmMainWndLayoutMainWindow(Info);
            return 0;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            return DmMainWndOnPaint(Info, hwnd);

        case WM_LBUTTONDOWN:
        {
            POINT Point;

            Point.x = GET_X_LPARAM(lParam);
            Point.y = GET_Y_LPARAM(lParam);
            if (DmMainWndBeginSplitterDrag(Info, &Point))
                return 0;
            break;
        }

        case WM_MOUSEMOVE:
        {
            POINT Point;

            Point.x = GET_X_LPARAM(lParam);
            Point.y = GET_Y_LPARAM(lParam);
            if (DmSplitterIsDragging(&Info->SplitterState))
            {
                DmMainWndUpdateSplitterDrag(Info, &Point);
                return 0;
            }
            break;
        }

        case WM_LBUTTONUP:
        {
            POINT Point;

            Point.x = GET_X_LPARAM(lParam);
            Point.y = GET_Y_LPARAM(lParam);
            if (DmSplitterIsDragging(&Info->SplitterState))
            {
                DmMainWndUpdateSplitterDrag(Info, &Point);
                DmSplitterEndDrag(&Info->SplitterState);
                ReleaseCapture();
                return 0;
            }
            break;
        }

        case WM_CANCELMODE:
        case WM_CAPTURECHANGED:
            if (DmSplitterIsDragging(&Info->SplitterState))
            {
                DmSplitterCancelDrag(&Info->SplitterState, &Info->LayoutMetrics);
                DmMainWndLayoutMainWindow(Info);
                return 0;
            }
            break;

        case WM_SETCURSOR:
            if ((HWND)wParam == hwnd &&
                LOWORD(lParam) == HTCLIENT &&
                DmMainWndHandleSplitterSetCursor(Info))
            {
                return TRUE;
            }
            break;

        case WM_INITMENUPOPUP:
            DmMainWndUpdateMenuState(Info);
            DmMenuStateApplyMenu(&Info->MenuState, (HMENU)wParam);
            return 0;

        case WM_COMMAND:
            return OnCommand(Info, wParam, lParam);

        case WM_CONTEXTMENU:
            return OnContextMenu(Info,
                                 (HWND)wParam,
                                 GET_X_LPARAM(lParam),
                                 GET_Y_LPARAM(lParam));

        case WM_NOTIFY:
            return OnNotify(Info, lParam);

        case WM_MENUSELECT:
        {
            UINT Item = LOWORD(wParam);
            UINT Flags = HIWORD(wParam);

            if ((Flags & MF_POPUP) || (Flags & MF_SYSMENU))
            {
                Info->bInMenuLoop = TRUE;
                DmMainWndUpdateStatusBarMode(Info);
                if (Flags & MF_SYSMENU)
                {
                    DmMainWndShowSystemHint(Info,
                                            (WORD)Item,
                                            IDS_STATUS_READY);
                }
                else
                {
                    StatusBarLoadString(Info->hStatusBar,
                                        1,
                                        hInstance,
                                        IDS_STATUS_READY);
                }
            }
            else if ((Flags & MF_MOUSESELECT) && !(Flags & MF_SEPARATOR))
            {
                DmMainWndShowCommandHint(Info,
                                         (WORD)Item,
                                         IDS_STATUS_READY);
            }
            else if (Flags == 0xFFFF && (HMENU)lParam == NULL)
            {
                Info->bInMenuLoop = FALSE;
                DmMainWndUpdateStatusBarMode(Info);
                StatusBarLoadString(Info->hStatusBar,
                                    1,
                                    hInstance,
                                    IDS_STATUS_READY);
            }

            return 0;
        }

        case WM_DESTROY:
            if (Info->hToolbarImageList != NULL)
            {
                ImageList_Destroy(Info->hToolbarImageList);
                Info->hToolbarImageList = NULL;
            }
            if (Info->hVolumeImageList != NULL)
            {
                ImageList_Destroy(Info->hVolumeImageList);
                Info->hVolumeImageList = NULL;
            }
            DmSnapshotClear(&Info->Snapshot);
            PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY:
            HeapFree(ProcessHeap, 0, Info);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
