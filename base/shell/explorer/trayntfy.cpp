/*
 * ReactOS Explorer
 *
 * Copyright 2006 - 2007 Thomas Weidenmueller <w3seek@reactos.org>
 * Copyright 2018 Ged Murphy <gedmurphy@reactos.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "precomp.h"
#include <commoncontrols.h>

static const WCHAR szTrayNotifyWndClass[] = L"TrayNotifyWnd";

#define TRAY_NOTIFY_WND_SPACING_X   ShellScaleForDpi(1)
#define TRAY_NOTIFY_WND_SPACING_Y   ShellScaleForDpi(1)
#define TRAY_CHEVRON_WIDTH          ShellScaleForDpi(32)
#define TRAY_QS_PAD                 ShellScaleForDpi(4)
#define TRAY_QS_SLOT                ShellScaleForDpi(24)

static const WCHAR szTrayChevronClass[] = L"TrayChevronButton";
static const WCHAR szTrayQuickSettingsClass[] = L"TrayQuickSettingsButton";

static VOID
TrayPaintBuffered(HWND hWnd, HDC hdc, VOID (*pfnPaint)(HWND, HDC, const RECT *, PVOID), PVOID pContext)
{
    RECT rc;
    HDC hdcMem;
    HBITMAP hbm;
    HGDIOBJ hbmOld;

    ::GetClientRect(hWnd, &rc);
    hdcMem = CreateCompatibleDC(hdc);
    hbm = hdcMem ? CreateCompatibleBitmap(hdc, rc.right, rc.bottom) : NULL;
    if (!hbm)
    {
        if (hdcMem)
            DeleteDC(hdcMem);
        DrawThemeParentBackground(hWnd, hdc, &rc);
        pfnPaint(hWnd, hdc, &rc, pContext);
        return;
    }
    hbmOld = SelectObject(hdcMem, hbm);
    DrawThemeParentBackground(hWnd, hdcMem, &rc);
    pfnPaint(hWnd, hdcMem, &rc, pContext);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
}

class CTrayChevronButton :
    public CWindowImpl<CTrayChevronButton, CWindow, CControlWinTraits>
{
    HWND m_hwndPager;
    BOOL m_bHot;
    BOOL m_bPressed;
    BOOL m_bTracking;
    CTooltips m_Tooltip;
    WCHAR m_szTip[64];

    static VOID Paint(HWND hWnd, HDC hdc, const RECT *prc, PVOID pContext)
    {
        CTrayChevronButton *pThis = (CTrayChevronButton *)pContext;
        BOOL bOpen = TrayOverflow_IsOpen();
        INT iState = 0;
        RECT rcPill;

        if (pThis->m_bPressed)
            iState = TRAY_PILL_PRESSED;
        else if (bOpen)
            iState = TRAY_PILL_CHECKED;
        else if (pThis->m_bHot)
            iState = TRAY_PILL_HOT;
        ShellGetTrayPillRect(prc, &rcPill);
        ShellDrawTrayPill(hdc, &rcPill, iState);
        ShellDrawTrayGlyph(hdc, &rcPill, bOpen ? IDI_FLU_TRAYCHEVDOWN : IDI_FLU_TRAYCHEVUP);
    }

public:
    CTrayChevronButton() : m_hwndPager(NULL), m_bHot(FALSE), m_bPressed(FALSE), m_bTracking(FALSE)
    {
        m_szTip[0] = UNICODE_NULL;
    }

    DECLARE_WND_CLASS_EX(szTrayChevronClass, CS_HREDRAW | CS_VREDRAW, COLOR_3DFACE)

    HWND DoCreate(HWND hwndParent, HWND hwndPager)
    {
        m_hwndPager = hwndPager;
        Create(hwndParent, NULL, NULL, WS_CHILD | WS_CLIPSIBLINGS);
        if (!m_hWnd)
            return NULL;

        if (!LoadStringW(hExplorerInstance, IDS_TRAYCHEVRON_TOOLTIP, m_szTip, _countof(m_szTip)))
            StringCchCopyW(m_szTip, _countof(m_szTip), L"Show hidden icons");
        m_Tooltip.Create(m_hWnd, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP);
        TTTOOLINFOW ti = { 0 };
        ti.cbSize = TTTOOLINFOW_V1_SIZE;
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = m_hWnd;
        ti.uId = reinterpret_cast<UINT_PTR>(m_hWnd);
        ti.lpszText = m_szTip;
        m_Tooltip.AddTool(&ti);
        return m_hWnd;
    }

    LRESULT OnEraseBackground(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        return TRUE;
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = (uMsg == WM_PRINTCLIENT) ? (HDC)wParam : BeginPaint(&ps);

        if (hdc)
            TrayPaintBuffered(m_hWnd, hdc, Paint, this);
        if (uMsg != WM_PRINTCLIENT)
            EndPaint(&ps);
        return 0;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (!m_bTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
            TrackMouseEvent(&tme);
            m_bTracking = TRUE;
        }
        if (!m_bHot)
        {
            m_bHot = TRUE;
            Invalidate(FALSE);
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        m_bTracking = FALSE;
        m_bHot = FALSE;
        Invalidate(FALSE);
        return 0;
    }

    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        m_bPressed = TRUE;
        SetCapture();
        Invalidate(FALSE);
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc;

        if (!m_bPressed)
            return 0;
        m_bPressed = FALSE;
        ReleaseCapture();
        GetClientRect(&rc);
        if (PtInRect(&rc, pt))
        {
            m_Tooltip.Pop();
            GetWindowRect(&rc);
            TrayOverflow_Toggle(m_hWnd, &rc, m_hwndPager);
        }
        Invalidate(FALSE);
        return 0;
    }

    LRESULT OnCaptureChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (m_bPressed && (HWND)lParam != m_hWnd)
        {
            m_bPressed = FALSE;
            Invalidate(FALSE);
        }
        return 0;
    }

    BEGIN_MSG_MAP(CTrayChevronButton)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
    END_MSG_MAP()
};

class CTrayQuickSettingsButton :
    public CWindowImpl<CTrayQuickSettingsButton, CWindow, CControlWinTraits>
{
    HWND m_hwndPager;
    TRAYICONLIST m_List;
    BOOL m_bHot;
    BOOL m_bPressed;
    BOOL m_bTracking;
    CTooltips m_Tooltip;

    static VOID Paint(HWND hWnd, HDC hdc, const RECT *prc, PVOID pContext)
    {
        CTrayQuickSettingsButton *pThis = (CTrayQuickSettingsButton *)pContext;
        INT iState = 0, cx = 0, cy = 0;
        RECT rcPill;

        if (pThis->m_bPressed)
            iState = TRAY_PILL_PRESSED;
        else if (TrayQuickSettings_IsOpen())
            iState = TRAY_PILL_CHECKED;
        else if (pThis->m_bHot)
            iState = TRAY_PILL_HOT;
        ShellGetTrayPillRect(prc, &rcPill);
        ShellDrawTrayPill(hdc, &rcPill, iState);

        if (!pThis->m_List.himl || !ImageList_GetIconSize(pThis->m_List.himl, &cx, &cy))
            return;
        for (UINT i = 0; i < pThis->m_List.cItems; i++)
        {
            RECT rcSlot = pThis->SlotRect(i, &rcPill);
            if (pThis->m_List.Items[i].iImage < 0)
                continue;
            ImageList_Draw(pThis->m_List.himl, pThis->m_List.Items[i].iImage, hdc,
                           rcSlot.left + (rcSlot.right - rcSlot.left - cx) / 2,
                           rcSlot.top + (rcSlot.bottom - rcSlot.top - cy) / 2,
                           ILD_TRANSPARENT);
        }
    }

    RECT SlotRect(UINT i, const RECT *prcPill) const
    {
        RECT rc = *prcPill;
        rc.left = TRAY_QS_PAD + (INT)i * TRAY_QS_SLOT;
        rc.right = rc.left + TRAY_QS_SLOT;
        return rc;
    }

    INT SlotFromPoint(POINT pt) const
    {
        INT i = (pt.x - TRAY_QS_PAD) / TRAY_QS_SLOT;

        if (!m_List.cItems)
            return -1;
        if (pt.x < TRAY_QS_PAD || i < 0)
            return 0;
        return min(i, (INT)m_List.cItems - 1);
    }

    VOID SendToSlot(INT i, UINT uMsg)
    {
        TRAYICONEVENT Event;

        if (i < 0 || i >= (INT)m_List.cItems)
            return;
        Event.hWnd = m_List.Items[i].hWnd;
        Event.uID = m_List.Items[i].uID;
        Event.uMsg = uMsg;
        ::SendMessageW(m_hwndPager, TNWM_TRAYICONEVENT, 0, (LPARAM)&Event);
    }

public:
    CTrayQuickSettingsButton() : m_hwndPager(NULL), m_bHot(FALSE), m_bPressed(FALSE), m_bTracking(FALSE)
    {
        ZeroMemory(&m_List, sizeof(m_List));
    }

    DECLARE_WND_CLASS_EX(szTrayQuickSettingsClass, CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, COLOR_3DFACE)

    HWND DoCreate(HWND hwndParent, HWND hwndPager)
    {
        m_hwndPager = hwndPager;
        Create(hwndParent, NULL, NULL, WS_CHILD | WS_CLIPSIBLINGS);
        if (m_hWnd)
            m_Tooltip.Create(m_hWnd, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP);
        return m_hWnd;
    }

    UINT GetCount() const
    {
        return m_List.cItems;
    }

    INT GetWidth() const
    {
        return m_List.cItems ? 2 * TRAY_QS_PAD + (INT)m_List.cItems * TRAY_QS_SLOT : 0;
    }

    VOID Refresh()
    {
        RECT rcClient, rcPill;

        ZeroMemory(&m_List, sizeof(m_List));
        if (m_hwndPager)
            ::SendMessageW(m_hwndPager, TNWM_GETTRAYICONS, TRAYICONS_QUICKSETTINGS, (LPARAM)&m_List);
        if (!m_hWnd)
            return;

        while (m_Tooltip.m_hWnd && m_Tooltip.GetToolCount() > 0)
        {
            TTTOOLINFOW ti = { TTTOOLINFOW_V1_SIZE };
            if (!m_Tooltip.EnumTools(&ti))
                break;
            m_Tooltip.DelTool(ti.hwnd, (UINT)ti.uId);
        }
        GetClientRect(&rcClient);
        ShellGetTrayPillRect(&rcClient, &rcPill);
        for (UINT i = 0; m_Tooltip.m_hWnd && i < m_List.cItems; i++)
        {
            TTTOOLINFOW ti = { TTTOOLINFOW_V1_SIZE };
            ti.uFlags = TTF_SUBCLASS;
            ti.hwnd = m_hWnd;
            ti.uId = i + 1;
            ti.rect = SlotRect(i, &rcPill);
            if (i == 0)
                ti.rect.left = 0;
            if (i + 1 == m_List.cItems)
                ti.rect.right = rcClient.right;
            ti.lpszText = m_List.Items[i].szTip;
            m_Tooltip.AddTool(&ti);
        }
        Invalidate(FALSE);
    }

    BOOL GetAnchor(POINT *ppt)
    {
        RECT rc;

        if (!m_hWnd || !IsWindowVisible() || !GetWindowRect(&rc))
            return FALSE;
        ppt->x = (rc.left + rc.right) / 2;
        ppt->y = (rc.top + rc.bottom) / 2;
        return TRUE;
    }

    LRESULT OnEraseBackground(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        return TRUE;
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = (uMsg == WM_PRINTCLIENT) ? (HDC)wParam : BeginPaint(&ps);

        if (hdc)
            TrayPaintBuffered(m_hWnd, hdc, Paint, this);
        if (uMsg != WM_PRINTCLIENT)
            EndPaint(&ps);
        return 0;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (!m_bTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
            TrackMouseEvent(&tme);
            m_bTracking = TRUE;
        }
        if (!m_bHot)
        {
            m_bHot = TRUE;
            Invalidate(FALSE);
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        m_bTracking = FALSE;
        m_bHot = FALSE;
        Invalidate(FALSE);
        return 0;
    }

    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        m_bPressed = TRUE;
        SetCapture();
        Invalidate(FALSE);
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc;

        if (!m_bPressed)
            return 0;
        m_bPressed = FALSE;
        ReleaseCapture();
        GetClientRect(&rc);
        if (PtInRect(&rc, pt))
        {
            m_Tooltip.Pop();
            GetWindowRect(&rc);
            TrayQuickSettings_Toggle(m_hWnd, &rc);
        }
        Invalidate(FALSE);
        return 0;
    }

    LRESULT OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        INT i = SlotFromPoint(pt);

        SendToSlot(i, WM_RBUTTONDOWN);
        SendToSlot(i, WM_RBUTTONUP);
        SendToSlot(i, WM_CONTEXTMENU);
        return 0;
    }

    LRESULT OnCaptureChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (m_bPressed && (HWND)lParam != m_hWnd)
        {
            m_bPressed = FALSE;
            Invalidate(FALSE);
        }
        return 0;
    }

    BEGIN_MSG_MAP(CTrayQuickSettingsButton)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
        MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
    END_MSG_MAP()
};

/*
 * TrayNotifyWnd
 */

class CTrayNotifyWnd :
    public CComCoClass<CTrayNotifyWnd>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public CWindowImpl < CTrayNotifyWnd, CWindow, CControlWinTraits >,
    public IOleWindow
{
    CComPtr<IUnknown> m_clock;
    CTrayShowDesktopButton m_ShowDesktopButton;
    CComPtr<IUnknown> m_pager;
    CTrayChevronButton m_Chevron;
    CTrayQuickSettingsButton m_QuickSettings;

    HWND m_hwndClock;
    HWND m_hwndShowDesktop;
    HWND m_hwndPager;

    HTHEME TrayTheme;
    SIZE trayClockMinSize;
    SIZE trayShowDesktopSize;
    SIZE trayNotifySize;
    MARGINS ContentMargin;
    BOOL IsHorizontal;
    BOOL m_bModern;
    BOOL m_bChevron;

public:
    CTrayNotifyWnd() :
        m_hwndClock(NULL),
        m_hwndPager(NULL),
        TrayTheme(NULL),
        IsHorizontal(FALSE),
        m_bModern(FALSE),
        m_bChevron(FALSE)
    {
        ZeroMemory(&trayClockMinSize, sizeof(trayClockMinSize));
        ZeroMemory(&trayShowDesktopSize, sizeof(trayShowDesktopSize));
        ZeroMemory(&trayNotifySize, sizeof(trayNotifySize));
        ZeroMemory(&ContentMargin, sizeof(ContentMargin));
    }
    ~CTrayNotifyWnd() { }

    LRESULT OnThemeChanged()
    {
        if (TrayTheme)
            CloseThemeData(TrayTheme);

        if (IsThemeActive())
            TrayTheme = OpenThemeData(m_hWnd, L"TrayNotify");
        else
            TrayTheme = NULL;

        if (TrayTheme)
        {
            SetWindowExStyle(m_hWnd, WS_EX_STATICEDGE, 0);

            GetThemeMargins(TrayTheme,
                NULL,
                TNP_BACKGROUND,
                0,
                TMT_CONTENTMARGINS,
                NULL,
                &ContentMargin);
        }
        else
        {
            SetWindowExStyle(m_hWnd, WS_EX_STATICEDGE, WS_EX_STATICEDGE);

            ContentMargin.cxLeftWidth = ShellScaleForDpi(2);
            ContentMargin.cxRightWidth = ShellScaleForDpi(2);
            ContentMargin.cyTopHeight = ShellScaleForDpi(2);
            ContentMargin.cyBottomHeight = ShellScaleForDpi(2);
        }

        return TRUE;
    }

    LRESULT OnThemeChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        return OnThemeChanged();
    }

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        HRESULT hr;

        hr = CTrayClockWnd_CreateInstance(m_hWnd, IID_PPV_ARG(IUnknown, &m_clock));
        if (FAILED_UNEXPECTEDLY(hr))
            return FALSE;

        hr = IUnknown_GetWindow(m_clock, &m_hwndClock);
        if (FAILED_UNEXPECTEDLY(hr))
            return FALSE;

        hr = CSysPagerWnd_CreateInstance(m_hWnd, IID_PPV_ARG(IUnknown, &m_pager));
        if (FAILED_UNEXPECTEDLY(hr))
            return FALSE;

        hr = IUnknown_GetWindow(m_pager, &m_hwndPager);
        if (FAILED_UNEXPECTEDLY(hr))
            return FALSE;

        /* Create the 'Show Desktop' button */
        m_ShowDesktopButton.DoCreate(m_hWnd);
        m_hwndShowDesktop = m_ShowDesktopButton.m_hWnd;

        m_Chevron.DoCreate(m_hWnd, m_hwndPager);
        m_QuickSettings.DoCreate(m_hWnd, m_hwndPager);

        return TRUE;
    }

    UINT GetOverflowCount()
    {
        TRAYICONLIST List;

        ZeroMemory(&List, sizeof(List));
        ::SendMessage(m_hwndPager, TNWM_GETTRAYICONS, TRAYICONS_OVERFLOW, (LPARAM)&List);
        return List.cItems;
    }

    BOOL GetModernMinimumSize(IN OUT PSIZE pSize)
    {
        SIZE clockSize = { 0, pSize->cy };
        SIZE traySize = { 0, pSize->cy };

        ZeroMemory(&trayClockMinSize, sizeof(trayClockMinSize));
        if (!GetHideClock() && pSize->cy > 0)
        {
            ::SendMessage(m_hwndClock, TNWM_GETMINIMUMSIZE, TNWM_MINSIZE_MODERN, (LPARAM)&clockSize);
            trayClockMinSize = clockSize;
        }

        ::SendMessage(m_hwndPager, TNWM_GETMINIMUMSIZE, TNWM_MINSIZE_MODERN, (LPARAM)&traySize);
        trayNotifySize = traySize;

        m_QuickSettings.Refresh();
        m_bChevron = (GetOverflowCount() != 0);

        m_ShowDesktopButton.m_bModern = TRUE;
        trayShowDesktopSize.cx = g_TaskbarSettings.bShowDesktopButton ? m_ShowDesktopButton.WidthOrHeight() : 0;
        trayShowDesktopSize.cy = pSize->cy;

        pSize->cx = (m_bChevron ? TRAY_CHEVRON_WIDTH : 0) + trayNotifySize.cx +
                    m_QuickSettings.GetWidth() + trayClockMinSize.cx + trayShowDesktopSize.cx;
        return TRUE;
    }

    VOID AlignModernControls(IN CONST RECT *prcClient)
    {
        CONST UINT swpFlags = SWP_NOCOPYBITS | SWP_NOZORDER | SWP_NOACTIVATE;
        INT cy = prcClient->bottom - prcClient->top;
        INT x = prcClient->right;
        INT cxQuickSettings = m_QuickSettings.GetWidth();

        if (g_TaskbarSettings.bShowDesktopButton)
        {
            x -= trayShowDesktopSize.cx;
            ::SetWindowPos(m_hwndShowDesktop, NULL, x, prcClient->top, trayShowDesktopSize.cx, cy, swpFlags);
        }

        if (!GetHideClock())
        {
            x -= trayClockMinSize.cx;
            ::SetWindowPos(m_hwndClock, NULL, x, prcClient->top, trayClockMinSize.cx, cy, swpFlags);
        }

        x -= cxQuickSettings;
        ::SetWindowPos(m_QuickSettings, NULL, x, prcClient->top, cxQuickSettings, cy,
                       swpFlags | (cxQuickSettings ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));

        x -= trayNotifySize.cx;
        ::SetWindowPos(m_hwndPager, NULL, x, prcClient->top, trayNotifySize.cx, cy, swpFlags);

        x -= TRAY_CHEVRON_WIDTH;
        ::SetWindowPos(m_Chevron, NULL, x, prcClient->top, TRAY_CHEVRON_WIDTH, cy,
                       swpFlags | (m_bChevron ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }

    BOOL GetMinimumSize(IN OUT PSIZE pSize)
    {
        SIZE clockSize = { 0, 0 };
        SIZE traySize = { 0, 0 };
        SIZE showDesktopSize = { 0, 0 };
        BOOL bHideClock = GetHideClock();

        m_bModern = ShellIsModernTray(IsHorizontal);
        if (m_bModern)
            return GetModernMinimumSize(pSize);

        m_bChevron = FALSE;
        m_ShowDesktopButton.m_bModern = FALSE;
        if (m_Chevron.m_hWnd)
            m_Chevron.ShowWindow(SW_HIDE);
        if (m_QuickSettings.m_hWnd)
            m_QuickSettings.ShowWindow(SW_HIDE);

        if (!bHideClock)
        {
            if (IsHorizontal)
            {
                clockSize.cy = pSize->cy;
                if (clockSize.cy <= 0)
                    goto NoClock;
            }
            else
            {
                clockSize.cx = pSize->cx;
                if (clockSize.cx <= 0)
                    goto NoClock;
            }

            ::SendMessage(m_hwndClock, TNWM_GETMINIMUMSIZE, (WPARAM) IsHorizontal, (LPARAM) &clockSize);

            trayClockMinSize = clockSize;
        }
        else
        NoClock:
        trayClockMinSize = clockSize;

        if (IsHorizontal)
        {
            traySize.cy = pSize->cy - 2 * TRAY_NOTIFY_WND_SPACING_Y;
        }
        else
        {
            traySize.cx = pSize->cx - 2 * TRAY_NOTIFY_WND_SPACING_X;
        }

        ::SendMessage(m_hwndPager, TNWM_GETMINIMUMSIZE, (WPARAM) IsHorizontal, (LPARAM) &traySize);

        trayNotifySize = traySize;

        INT showDesktopButtonExtent = 0;
        if (g_TaskbarSettings.bShowDesktopButton)
        {
            showDesktopButtonExtent = m_ShowDesktopButton.WidthOrHeight();
            if (IsHorizontal)
            {
                showDesktopSize.cx = showDesktopButtonExtent;
                showDesktopSize.cy = pSize->cy;
            }
            else
            {
                showDesktopSize.cx = pSize->cx;
                showDesktopSize.cy = showDesktopButtonExtent;
            }
        }
        trayShowDesktopSize = showDesktopSize;

        if (IsHorizontal)
        {
            pSize->cx = ContentMargin.cxLeftWidth + TRAY_NOTIFY_WND_SPACING_X + traySize.cx;

            if (!bHideClock)
                pSize->cx += TRAY_NOTIFY_WND_SPACING_X + trayClockMinSize.cx;

            if (g_TaskbarSettings.bShowDesktopButton)
                pSize->cx += showDesktopButtonExtent;
            else
                pSize->cx += ContentMargin.cxRightWidth;
        }
        else
        {
            pSize->cy = ContentMargin.cyTopHeight + TRAY_NOTIFY_WND_SPACING_Y + traySize.cy;

            if (!bHideClock)
                pSize->cy += TRAY_NOTIFY_WND_SPACING_Y + trayClockMinSize.cy;

            if (g_TaskbarSettings.bShowDesktopButton)
                pSize->cy += showDesktopButtonExtent;
            else
                pSize->cy += ContentMargin.cyBottomHeight;
        }

        return TRUE;
    }

    VOID Size(IN OUT SIZE *pszClient)
    {
        RECT rcClient = {0, 0, pszClient->cx, pszClient->cy};
        AlignControls(&rcClient);
        pszClient->cx = rcClient.right - rcClient.left;
        pszClient->cy = rcClient.bottom - rcClient.top;
    }

    VOID AlignControls(IN CONST PRECT prcClient OPTIONAL)
    {
        RECT rcClient;
        if (prcClient != NULL)
            rcClient = *prcClient;
        else
            GetClientRect(&rcClient);

        if (m_bModern)
        {
            AlignModernControls(&rcClient);
            return;
        }

        rcClient.left += ContentMargin.cxLeftWidth;
        rcClient.top += ContentMargin.cyTopHeight;
        rcClient.right -= ContentMargin.cxRightWidth;
        rcClient.bottom -= ContentMargin.cyBottomHeight;

        CONST UINT swpFlags = SWP_DRAWFRAME | SWP_NOCOPYBITS | SWP_NOZORDER;

        if (g_TaskbarSettings.bShowDesktopButton)
        {
            POINT ptShowDesktop =
            {
                rcClient.left,
                rcClient.top
            };
            SIZE showDesktopSize =
            {
                rcClient.right - rcClient.left,
                rcClient.bottom - rcClient.top
            };

            INT cxyShowDesktop = m_ShowDesktopButton.WidthOrHeight();
            if (IsHorizontal)
            {
                if (!TrayTheme)
                {
                    ptShowDesktop.y -= ContentMargin.cyTopHeight;
                    showDesktopSize.cy += ContentMargin.cyTopHeight + ContentMargin.cyBottomHeight;
                }

                rcClient.right -= (cxyShowDesktop - ContentMargin.cxRightWidth);

                ptShowDesktop.x = rcClient.right;
                showDesktopSize.cx = cxyShowDesktop;
            }
            else
            {
                if (!TrayTheme)
                {
                    ptShowDesktop.x -= ContentMargin.cxLeftWidth;
                    showDesktopSize.cx += ContentMargin.cxLeftWidth + ContentMargin.cxRightWidth;
                }

                rcClient.bottom -= (cxyShowDesktop - ContentMargin.cyBottomHeight);

                ptShowDesktop.y = rcClient.bottom;
                showDesktopSize.cy = cxyShowDesktop;
            }

            /* Resize and reposition the button */
            ::SetWindowPos(m_hwndShowDesktop,
                NULL,
                ptShowDesktop.x,
                ptShowDesktop.y,
                showDesktopSize.cx,
                showDesktopSize.cy,
                swpFlags);
        }

        if (!GetHideClock())
        {
            POINT ptClock = { rcClient.left, rcClient.top };
            SIZE clockSize = { rcClient.right - rcClient.left, rcClient.bottom - rcClient.top };

            if (IsHorizontal)
            {
                rcClient.right -= trayClockMinSize.cx;

                ptClock.x = rcClient.right;
                clockSize.cx = trayClockMinSize.cx;
            }
            else
            {
                rcClient.bottom -= trayClockMinSize.cy;

                ptClock.y = rcClient.bottom;
                clockSize.cy = trayClockMinSize.cy;
            }

            ::SetWindowPos(m_hwndClock,
                NULL,
                ptClock.x,
                ptClock.y,
                clockSize.cx,
                clockSize.cy,
                swpFlags);
        }

        POINT ptPager;
        if (IsHorizontal)
        {
            ptPager.x = ContentMargin.cxLeftWidth + TRAY_NOTIFY_WND_SPACING_X;
            ptPager.y = ((rcClient.bottom - rcClient.top) - trayNotifySize.cy) / 2;
            if (g_TaskbarSettings.UseCompactTrayIcons())
                ptPager.y += ContentMargin.cyTopHeight;
        }
        else
        {
            ptPager.x = ((rcClient.right - rcClient.left) - trayNotifySize.cx) / 2;
            if (g_TaskbarSettings.UseCompactTrayIcons())
                ptPager.x += ContentMargin.cxLeftWidth;
            ptPager.y = ContentMargin.cyTopHeight + TRAY_NOTIFY_WND_SPACING_Y;
        }

        ::SetWindowPos(m_hwndPager,
            NULL,
            ptPager.x,
            ptPager.y,
            trayNotifySize.cx,
            trayNotifySize.cy,
            swpFlags);

        if (prcClient != NULL)
        {
            prcClient->left = rcClient.left - ContentMargin.cxLeftWidth;
            prcClient->top = rcClient.top - ContentMargin.cyTopHeight;
            prcClient->right = rcClient.right + ContentMargin.cxRightWidth;
            prcClient->bottom = rcClient.bottom + ContentMargin.cyBottomHeight;
        }
    }

    LRESULT OnEraseBackground(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        HDC hdc = (HDC) wParam;

        if (!TrayTheme)
        {
            bHandled = FALSE;
            return 0;
        }

        RECT rect;
        GetClientRect(&rect);
        if (m_bModern)
        {
            DrawThemeParentBackground(m_hWnd, hdc, &rect);
            return TRUE;
        }

        if (IsThemeBackgroundPartiallyTransparent(TrayTheme, TNP_BACKGROUND, 0))
            DrawThemeParentBackground(m_hWnd, hdc, &rect);

        DrawThemeBackground(TrayTheme, hdc, TNP_BACKGROUND, 0, &rect, 0);

        return TRUE;
    }

    LRESULT OnGetIconAnchor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT *ppt = (POINT *)lParam;
        RECT rc;

        if (!ppt)
            return FALSE;
        if ((INT)wParam != TRAYICON_APP && m_QuickSettings.GetAnchor(ppt))
            return TRUE;
        if (m_bChevron && m_Chevron.IsWindowVisible() && m_Chevron.GetWindowRect(&rc))
        {
            ppt->x = (rc.left + rc.right) / 2;
            ppt->y = (rc.top + rc.bottom) / 2;
            return TRUE;
        }
        GetWindowRect(&rc);
        ppt->x = (rc.left + rc.right) / 2;
        ppt->y = (rc.top + rc.bottom) / 2;
        return TRUE;
    }

    LRESULT OnIconsChanged(INT uCode, LPNMHDR hdr, BOOL& bHandled)
    {
        if (!m_bModern)
            return 0;
        m_QuickSettings.Refresh();
        m_Chevron.Invalidate(FALSE);
        TrayOverflow_Refresh();
        return 0;
    }

    LRESULT OnGetMinimumSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        BOOL Horizontal = (BOOL) wParam;

        if (Horizontal != IsHorizontal)
            IsHorizontal = Horizontal;

        SetWindowTheme(m_hWnd,
                       IsHorizontal ? L"TrayNotifyHoriz" : L"TrayNotifyVert",
                       NULL);
        m_ShowDesktopButton.m_bHorizontal = Horizontal;

        return (LRESULT)GetMinimumSize((PSIZE)lParam);
    }

    LRESULT OnGetShowDesktopButton(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (wParam == NULL)
            return 0;

        CTrayShowDesktopButton** ptr = (CTrayShowDesktopButton**)wParam;
        if (!m_ShowDesktopButton)
        {
            *ptr = NULL;
            return 0;
        }

        *ptr = &m_ShowDesktopButton;
        bHandled = TRUE;
        return 0;
    }

    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        SIZE clientSize;

        clientSize.cx = LOWORD(lParam);
        clientSize.cy = HIWORD(lParam);

        Size(&clientSize);

        return TRUE;
    }

    LRESULT OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        if (m_ShowDesktopButton && m_ShowDesktopButton.PtInButton(&pt))
            return HTCLIENT;

        return HTTRANSPARENT;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt;
        ::GetCursorPos(&pt);

        if (m_ShowDesktopButton && m_ShowDesktopButton.PtInButton(&pt))
            m_ShowDesktopButton.StartHovering();

        return TRUE;
    }

    LRESULT OnCtxMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        bHandled = TRUE;

        if (reinterpret_cast<HWND>(wParam) == m_hwndClock)
            return GetParent().SendMessage(uMsg, wParam, lParam);
        else
            return 0;
    }

    LRESULT OnClockMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        return SendMessageW(m_hwndClock, uMsg, wParam, lParam);
    }

    LRESULT OnTaskbarSettingsChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        TaskbarSettings* newSettings = (TaskbarSettings*)lParam;

        /* Toggle show desktop button */
        if (newSettings->bShowDesktopButton != g_TaskbarSettings.bShowDesktopButton)
        {
            g_TaskbarSettings.bShowDesktopButton = newSettings->bShowDesktopButton;
            ::ShowWindow(m_hwndShowDesktop, g_TaskbarSettings.bShowDesktopButton ? SW_SHOW : SW_HIDE);

            /* Ask the parent to resize */
            NMHDR nmh = {m_hWnd, 0, NTNWM_REALIGN};
            SendMessage(WM_NOTIFY, 0, (LPARAM) &nmh);
        }

        g_TaskbarSettings.bHideInactiveIcons = newSettings->bHideInactiveIcons;

        return OnClockMessage(uMsg, wParam, lParam, bHandled);
    }

    LRESULT OnPagerMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        return SendMessageW(m_hwndPager, uMsg, wParam, lParam);
    }

    LRESULT OnRealign(INT uCode, LPNMHDR hdr, BOOL& bHandled)
    {
        hdr->hwndFrom = m_hWnd;
        return GetParent().SendMessage(WM_NOTIFY, 0, (LPARAM)hdr);
    }

    // *** IOleWindow methods ***

    STDMETHODIMP
    GetWindow(HWND* phwnd) override
    {
        if (!phwnd)
            return E_INVALIDARG;
        *phwnd = m_hWnd;
        return S_OK;
    }

    STDMETHODIMP
    ContextSensitiveHelp(BOOL fEnterMode) override
    {
        return E_NOTIMPL;
    }

    HRESULT Initialize(IN HWND hwndParent)
    {
        const DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        Create(hwndParent, 0, NULL, dwStyle, WS_EX_STATICEDGE);
        return m_hWnd ? S_OK : E_FAIL;
    }

    DECLARE_NOT_AGGREGATABLE(CTrayNotifyWnd)

    DECLARE_PROTECT_FINAL_CONSTRUCT()
    BEGIN_COM_MAP(CTrayNotifyWnd)
        COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
    END_COM_MAP()

    DECLARE_WND_CLASS_EX(szTrayNotifyWndClass, CS_DBLCLKS, COLOR_3DFACE)

    BEGIN_MSG_MAP(CTrayNotifyWnd)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_THEMECHANGED, OnThemeChanged)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_NCMOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnCtxMenu)
        MESSAGE_HANDLER(WM_NCLBUTTONDBLCLK, OnClockMessage)
        MESSAGE_HANDLER(WM_SETFONT, OnClockMessage)
        MESSAGE_HANDLER(WM_SETTINGCHANGE, OnPagerMessage)
        MESSAGE_HANDLER(WM_COPYDATA, OnPagerMessage)
        MESSAGE_HANDLER(TWM_SETTINGSCHANGED, OnTaskbarSettingsChanged)
        NOTIFY_CODE_HANDLER(NTNWM_REALIGN, OnRealign)
        NOTIFY_CODE_HANDLER(NTNWM_ICONSCHANGED, OnIconsChanged)
        MESSAGE_HANDLER(TNWM_GETMINIMUMSIZE, OnGetMinimumSize)
        MESSAGE_HANDLER(TNWM_GETICONANCHOR, OnGetIconAnchor)
        MESSAGE_HANDLER(TNWM_GETSHOWDESKTOPBUTTON, OnGetShowDesktopButton)
    END_MSG_MAP()
};

HRESULT CTrayNotifyWnd_CreateInstance(HWND hwndParent, REFIID riid, void **ppv)
{
    return ShellObjectCreatorInit<CTrayNotifyWnd>(hwndParent, riid, ppv);
}
