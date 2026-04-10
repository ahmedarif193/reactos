/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Disk-view child window management.
 */

#include "precomp.h"
#include "diskviewwnd.h"

static
LRESULT
DmOnDiskViewPaint(
    _In_ HWND hwnd)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC hdc;
    PMAIN_WND_INFO Info;
    HFONT hFont;

    hdc = BeginPaint(hwnd, &ps);
    if (hdc == NULL)
        return 0;

    GetClientRect(hwnd, &rc);
    Info = (PMAIN_WND_INFO)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);

    if (Info == NULL)
    {
        DmDiskViewPaintEmptyState(hdc, &rc, NULL);
        EndPaint(hwnd, &ps);
        return 0;
    }

    hFont = (HFONT)SendMessageW(GetParent(hwnd), WM_GETFONT, 0, 0);
    DmDiskViewPaintSnapshot(hdc,
                            &rc,
                            &Info->Snapshot,
                            DmMainWndGetSelectedVolume(Info),
                            hFont,
                            Info->DiskViewScrollPos);
    EndPaint(hwnd, &ps);
    return 0;
}

static
INT
DmGetDiskViewPageHeight(
    _In_ PMAIN_WND_INFO Info)
{
    RECT rc;

    if (Info == NULL || Info->hDiskView == NULL)
        return 0;

    GetClientRect(Info->hDiskView, &rc);
    return max(rc.bottom - rc.top, 0);
}

static
INT
DmGetDiskViewMaxScrollPos(
    _In_ PMAIN_WND_INFO Info)
{
    INT TotalHeight;
    INT PageHeight;

    if (Info == NULL)
        return 0;

    TotalHeight = DmDiskViewGetPreferredHeight(&Info->Snapshot);
    PageHeight = DmGetDiskViewPageHeight(Info);
    return max(TotalHeight - PageHeight, 0);
}

static
VOID
DmSetDiskViewScrollPos(
    _In_ PMAIN_WND_INFO Info,
    _In_ INT ScrollPos,
    _In_ BOOL Redraw)
{
    SCROLLINFO si;
    INT MaxPos;

    if (Info == NULL || Info->hDiskView == NULL)
        return;

    MaxPos = DmGetDiskViewMaxScrollPos(Info);
    ScrollPos = max(min(ScrollPos, MaxPos), 0);
    Info->DiskViewScrollPos = ScrollPos;

    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = ScrollPos;
    SetScrollInfo(Info->hDiskView, SB_VERT, &si, Redraw);

    if (Redraw)
        InvalidateRect(Info->hDiskView, NULL, TRUE);
}

VOID
DmMainWndUpdateDiskViewScrollInfo(
    _In_ PMAIN_WND_INFO Info)
{
    SCROLLINFO si;
    INT TotalHeight;
    INT PageHeight;
    INT MaxPos;

    if (Info == NULL || Info->hDiskView == NULL)
        return;

    TotalHeight = DmDiskViewGetPreferredHeight(&Info->Snapshot);
    PageHeight = max(DmGetDiskViewPageHeight(Info), 1);
    MaxPos = max(TotalHeight - PageHeight, 0);
    if (Info->DiskViewScrollPos > MaxPos)
        Info->DiskViewScrollPos = MaxPos;

    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = max(TotalHeight - 1, 0);
    si.nPage = (UINT)PageHeight;
    si.nPos = Info->DiskViewScrollPos;
    SetScrollInfo(Info->hDiskView, SB_VERT, &si, TRUE);
}

static
LRESULT
DmOnDiskViewVScroll(
    _In_ HWND hwnd,
    _In_ WPARAM wParam)
{
    PMAIN_WND_INFO Info;
    SCROLLINFO si;
    INT PageHeight;
    INT ScrollPos;

    Info = (PMAIN_WND_INFO)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    if (Info == NULL)
        return 0;

    PageHeight = max(DmGetDiskViewPageHeight(Info) - DM_DISKVIEW_LEGEND_HEIGHT, 16);
    ScrollPos = Info->DiskViewScrollPos;

    switch (LOWORD(wParam))
    {
        case SB_TOP:
            ScrollPos = 0;
            break;

        case SB_BOTTOM:
            ScrollPos = DmGetDiskViewMaxScrollPos(Info);
            break;

        case SB_LINEUP:
            ScrollPos -= 20;
            break;

        case SB_LINEDOWN:
            ScrollPos += 20;
            break;

        case SB_PAGEUP:
            ScrollPos -= PageHeight;
            break;

        case SB_PAGEDOWN:
            ScrollPos += PageHeight;
            break;

        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            ZeroMemory(&si, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            ScrollPos = si.nTrackPos;
            break;

        default:
            return 0;
    }

    DmSetDiskViewScrollPos(Info, ScrollPos, TRUE);
    return 0;
}

static
LRESULT
DmOnDiskViewMouseWheel(
    _In_ HWND hwnd,
    _In_ WPARAM wParam)
{
    PMAIN_WND_INFO Info;
    INT Delta;
    INT ScrollPos;

    Info = (PMAIN_WND_INFO)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    if (Info == NULL)
        return 0;

    Delta = GET_WHEEL_DELTA_WPARAM(wParam);
    ScrollPos = Info->DiskViewScrollPos - ((Delta / WHEEL_DELTA) * 60);
    DmSetDiskViewScrollPos(Info, ScrollPos, TRUE);
    return 0;
}

static
LRESULT CALLBACK
DmDiskViewWndProc(
    _In_ HWND hwnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_LBUTTONDOWN:
        {
            PMAIN_WND_INFO Info;
            POINT Point;

            Info = (PMAIN_WND_INFO)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
            if (Info != NULL)
            {
                Point.x = GET_X_LPARAM(lParam);
                Point.y = GET_Y_LPARAM(lParam);
                ClientToScreen(hwnd, &Point);
                DmMainWndUpdateVolumeSelectionFromDiskViewPoint(Info, Point.x, Point.y);
            }
            SetFocus(hwnd);
            return 0;
        }

        case WM_LBUTTONDBLCLK:
        {
            PMAIN_WND_INFO Info;
            POINT Point;

            Info = (PMAIN_WND_INFO)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
            if (Info != NULL)
            {
                PDM_VOLUME Volume;

                Point.x = GET_X_LPARAM(lParam);
                Point.y = GET_Y_LPARAM(lParam);
                ClientToScreen(hwnd, &Point);
                DmMainWndUpdateVolumeSelectionFromDiskViewPoint(Info, Point.x, Point.y);
                Volume = DmMainWndGetSelectedVolume(Info);
                if (Volume != NULL)
                    DmShowVolumeProperties(Info->hMainWnd, Volume);
                else
                {
                    /* Double-click on disk label area: show disk properties */
                    POINT ClientPoint;
                    RECT ClientRect;
                    const DM_DISK *Disk;
                    const DM_REGION *Region;

                    ClientPoint.x = GET_X_LPARAM(lParam);
                    ClientPoint.y = GET_Y_LPARAM(lParam);
                    GetClientRect(hwnd, &ClientRect);
                    if (DmDiskViewHitTest(&ClientRect,
                                          &Info->Snapshot,
                                          Info->DiskViewScrollPos,
                                          &ClientPoint,
                                          &Disk,
                                          &Region))
                    {
                        if (Disk != NULL && Region == NULL)
                            DmShowDiskProperties(Info->hMainWnd, Disk);
                        else if (Region != NULL && Disk != NULL)
                            DmShowDiskProperties(Info->hMainWnd, Disk);
                    }
                }
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
        {
            PMAIN_WND_INFO Info;
            POINT Point;

            /* Select the clicked region before context menu appears */
            Info = (PMAIN_WND_INFO)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
            if (Info != NULL)
            {
                Point.x = GET_X_LPARAM(lParam);
                Point.y = GET_Y_LPARAM(lParam);
                ClientToScreen(hwnd, &Point);
                DmMainWndUpdateVolumeSelectionFromDiskViewPoint(Info, Point.x, Point.y);
            }
            SetFocus(hwnd);
            break;  /* fall through to DefWindowProc for WM_CONTEXTMENU */
        }

        case WM_SETFOCUS:
        {
            PMAIN_WND_INFO Info;

            Info = (PMAIN_WND_INFO)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
            if (Info != NULL)
                DmMainWndSetFocusTarget(Info, DmViewTargetBottom);
            return 0;
        }

        case WM_PAINT:
            return DmOnDiskViewPaint(hwnd);

        case WM_SIZE:
        {
            PMAIN_WND_INFO Info;

            Info = (PMAIN_WND_INFO)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
            if (Info != NULL)
                DmMainWndUpdateDiskViewScrollInfo(Info);
            return 0;
        }

        case WM_VSCROLL:
            return DmOnDiskViewVScroll(hwnd, wParam);

        case WM_MOUSEWHEEL:
            return DmOnDiskViewMouseWheel(hwnd, wParam);

        default:
            break;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

BOOL
DmMainWndRegisterDiskViewClass(
    _In_ HINSTANCE Instance)
{
    WNDCLASSEXW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = DmDiskViewWndProc;
    wc.hInstance = Instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = DISKMGMT_DISKVIEW_CLASS_NAME;
    return RegisterClassExW(&wc) != 0;
}

VOID
DmMainWndUnregisterDiskViewClass(
    _In_ HINSTANCE Instance)
{
    UnregisterClassW(DISKMGMT_DISKVIEW_CLASS_NAME, Instance);
}

BOOL
DmMainWndCreateDiskView(
    _In_ PMAIN_WND_INFO Info)
{
    Info->hDiskView = CreateWindowExW(WS_EX_CLIENTEDGE,
                                      DISKMGMT_DISKVIEW_CLASS_NAME,
                                      NULL,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL,
                                      0, 0, 0, 0,
                                      Info->hMainWnd,
                                      (HMENU)IDC_DISKVIEW,
                                      hInstance,
                                      NULL);
    return (Info->hDiskView != NULL);
}
