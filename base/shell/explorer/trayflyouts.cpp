#include "precomp.h"
#include <commctrl.h>
#include <windowsx.h>

enum
{
    TFY_TIMER_ANIM = 1,
    TFY_TIMER_TICK = 2,
    TFY_TIMER_ALIVE = 3
};

enum TFYANIM { TFY_NONE, TFY_OPEN, TFY_CLOSE };

static double
TfyEase(double t)
{
    double u;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    u = 1.0 - t;
    return 1.0 - u * u * u;
}

static COLORREF
TfyMix(COLORREF a, COLORREF b, int t)
{
    if (t <= 0) return a;
    if (t >= 255) return b;
    return RGB(GetRValue(a) + MulDiv(GetRValue(b) - GetRValue(a), t, 255),
               GetGValue(a) + MulDiv(GetGValue(b) - GetGValue(a), t, 255),
               GetBValue(a) + MulDiv(GetBValue(b) - GetBValue(a), t, 255));
}

static HFONT
TfyCreateFont(int nHeight, int nWeight)
{
    return CreateFontW(-ShellScaleForDpi(nHeight), 0, 0, 0, nWeight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static VOID
TfyPrintOne(HWND hwnd, HDC hdc, const RECT *prcRoot, int nDepth)
{
    RECT rcClient;
    POINT ptClient;
    DWORD_PTR dwResult;
    HWND ahChildren[64];
    UINT cChildren = 0, i;
    HWND hChild;
    int cw, ch;

    if (nDepth > 4 || !::IsWindowVisible(hwnd))
        return;

    ::GetClientRect(hwnd, &rcClient);
    cw = rcClient.right;
    ch = rcClient.bottom;
    if (cw <= 0 || ch <= 0)
        return;

    ptClient.x = ptClient.y = 0;
    ::ClientToScreen(hwnd, &ptClient);

    SaveDC(hdc);
    SetViewportOrgEx(hdc, ptClient.x - prcRoot->left, ptClient.y - prcRoot->top, NULL);
    IntersectClipRect(hdc, 0, 0, cw, ch);

    SendMessageTimeoutW(hwnd, WM_ERASEBKGND, (WPARAM)hdc, 0,
                        SMTO_ABORTIFHUNG, 200, &dwResult);
    SendMessageTimeoutW(hwnd, WM_PRINTCLIENT, (WPARAM)hdc, PRF_CLIENT,
                        SMTO_ABORTIFHUNG, 200, &dwResult);

    RestoreDC(hdc, -1);

    for (hChild = ::GetWindow(hwnd, GW_CHILD);
         hChild != NULL && cChildren < _countof(ahChildren);
         hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        ahChildren[cChildren++] = hChild;
    }

    for (i = cChildren; i > 0; i--)
        TfyPrintOne(ahChildren[i - 1], hdc, prcRoot, nDepth + 1);
}

static HBITMAP
TfyCaptureThumb(HWND hwnd, int maxW, int maxH, SIZE *pSize)
{
    RECT rcWnd;
    HDC hdcScreen, hdcSrc, hdcDst;
    HBITMAP hbmSrc, hbmDst = NULL;
    HGDIOBJ hOldSrc, hOldDst;
    int w, h, tw, th;

    pSize->cx = pSize->cy = 0;

    if (!::IsWindow(hwnd) || ::IsIconic(hwnd))
        return NULL;

    if (!::GetWindowRect(hwnd, &rcWnd))
        return NULL;

    w = rcWnd.right - rcWnd.left;
    h = rcWnd.bottom - rcWnd.top;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return NULL;

    hdcScreen = ::GetDC(NULL);
    if (!hdcScreen)
        return NULL;

    hdcSrc = CreateCompatibleDC(hdcScreen);
    hdcDst = CreateCompatibleDC(hdcScreen);
    hbmSrc = CreateCompatibleBitmap(hdcScreen, w, h);

    if (hdcSrc && hdcDst && hbmSrc)
    {
        BOOL bCaptured = FALSE;
        hOldSrc = SelectObject(hdcSrc, hbmSrc);

        if (hwnd == ::GetForegroundWindow())
        {
            bCaptured = BitBlt(hdcSrc, 0, 0, w, h, hdcScreen,
                               rcWnd.left, rcWnd.top, SRCCOPY);
        }

        if (!bCaptured)
            bCaptured = ::PrintWindow(hwnd, hdcSrc, 0);

        if (!bCaptured)
        {
            RECT rcFill = { 0, 0, w, h };
            HBRUSH hbrFill = CreateSolidBrush(RGB(48, 48, 48));
            FillRect(hdcSrc, &rcFill, hbrFill);
            DeleteObject(hbrFill);
            TfyPrintOne(hwnd, hdcSrc, &rcWnd, 0);
            bCaptured = TRUE;
        }

        if (bCaptured)
        {
            if (w * maxH > h * maxW)
            {
                tw = maxW;
                th = max(1, h * maxW / w);
            }
            else
            {
                th = maxH;
                tw = max(1, w * maxH / h);
            }

            hbmDst = CreateCompatibleBitmap(hdcScreen, tw, th);
            if (hbmDst)
            {
                hOldDst = SelectObject(hdcDst, hbmDst);
                SetStretchBltMode(hdcDst, HALFTONE);
                SetBrushOrgEx(hdcDst, 0, 0, NULL);
                StretchBlt(hdcDst, 0, 0, tw, th, hdcSrc, 0, 0, w, h, SRCCOPY);
                SelectObject(hdcDst, hOldDst);
                pSize->cx = tw;
                pSize->cy = th;
            }
        }

        SelectObject(hdcSrc, hOldSrc);
    }

    if (hbmSrc) DeleteObject(hbmSrc);
    if (hdcSrc) DeleteDC(hdcSrc);
    if (hdcDst) DeleteDC(hdcDst);
    ::ReleaseDC(NULL, hdcScreen);

    return hbmDst;
}

static HICON
TfyGetWindowIcon(HWND hwnd)
{
    HICON hIcon = NULL;
    SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_NOTIMEOUTIFNOTHUNG, 100, (PDWORD_PTR)&hIcon);
    if (!hIcon)
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_NOTIMEOUTIFNOTHUNG, 100, (PDWORD_PTR)&hIcon);
    if (!hIcon)
        hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM);
    if (!hIcon)
        hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    return hIcon;
}

typedef CWinTraits<WS_POPUP | WS_CLIPCHILDREN,
                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED> CTrayFlyoutTraits;

struct TFYCARD
{
    HWND hWnd;
    HBITMAP hbmThumb;
    SIZE thumbSize;
    HICON hIcon;
    WCHAR szTitle[128];
    RECT rc;
    RECT rcClose;
};

class CTaskPreviewWnd :
    public CWindowImpl<CTaskPreviewWnd, CWindow, CTrayFlyoutTraits>
{
public:
    DECLARE_WND_CLASS_EX(L"TrayTaskPreview", CS_DROPSHADOW, COLOR_WINDOW)

    SM2_FLYOUT_PALETTE m_Pal;
    CAtlArray<TFYCARD> m_Cards;
    HFONT m_hFont;
    INT_PTR m_nGroupId;
    int m_iHot;
    BOOL m_bCloseHot;
    BOOL m_bTracking;
    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    SIZE m_size;

    CTaskPreviewWnd() : m_hFont(NULL), m_nGroupId(0), m_iHot(-1),
                        m_bCloseHot(FALSE), m_bTracking(FALSE),
                        m_AnimPhase(TFY_NONE), m_AnimT0(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        ZeroMemory(&m_size, sizeof(m_size));
    }

    int Sc(int v) const { return ShellScaleForDpi(v); }
    int CardW() const { return Sc(200); }
    int TitleH() const { return Sc(26); }
    int ThumbW() const { return Sc(184); }
    int ThumbH() const { return Sc(104); }
    int CardH() const { return TitleH() + ThumbH() + Sc(12); }

    VOID FreeCards()
    {
        for (SIZE_T i = 0; i < m_Cards.GetCount(); i++)
        {
            if (m_Cards[i].hbmThumb)
                DeleteObject(m_Cards[i].hbmThumb);
        }
        m_Cards.SetCount(0);
    }

    VOID BuildCards(const HWND *pahWnd, UINT cWindows)
    {
        FreeCards();
        for (UINT i = 0; i < cWindows && i < 16; i++)
        {
            TFYCARD card;
            ZeroMemory(&card, sizeof(card));
            card.hWnd = pahWnd[i];
            card.hbmThumb = TfyCaptureThumb(card.hWnd, ThumbW(), ThumbH(), &card.thumbSize);
            card.hIcon = TfyGetWindowIcon(card.hWnd);
            ::GetWindowTextW(card.hWnd, card.szTitle, _countof(card.szTitle));
            m_Cards.Add(card);
        }
    }

    VOID LayoutCards()
    {
        int x = Sc(8);
        for (SIZE_T i = 0; i < m_Cards.GetCount(); i++)
        {
            TFYCARD &card = m_Cards[i];
            SetRect(&card.rc, x, Sc(8), x + CardW(), Sc(8) + CardH());
            SetRect(&card.rcClose,
                    card.rc.right - Sc(24), card.rc.top + Sc(4),
                    card.rc.right - Sc(6), card.rc.top + TitleH() - Sc(4));
            x += CardW() + Sc(8);
        }
        m_size.cx = x;
        m_size.cy = CardH() + Sc(16);
    }

    VOID Popup(HWND hwndOwner, const RECT *prcAnchor, const HWND *pahWnd, UINT cWindows, INT_PTR nGroupId)
    {
        MONITORINFO mi;
        POINT ptRef;
        int xPos, yPos;

        StartMenu2_GetFlyoutPalette(&m_Pal);
        m_nGroupId = nGroupId;
        m_iHot = -1;
        m_bCloseHot = FALSE;

        if (!m_hFont)
            m_hFont = TfyCreateFont(12, FW_NORMAL);

        BuildCards(pahWnd, cWindows);
        LayoutCards();

        ptRef.x = (prcAnchor->left + prcAnchor->right) / 2;
        ptRef.y = prcAnchor->top;
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST), &mi);

        xPos = ptRef.x - m_size.cx / 2;
        if (xPos + m_size.cx > mi.rcMonitor.right) xPos = mi.rcMonitor.right - m_size.cx;
        if (xPos < mi.rcMonitor.left) xPos = mi.rcMonitor.left;
        yPos = prcAnchor->top - m_size.cy - Sc(6);
        if (yPos < mi.rcMonitor.top) yPos = prcAnchor->bottom + Sc(6);

        m_ptFinal.x = xPos;
        m_ptFinal.y = yPos;

        m_AnimPhase = TFY_OPEN;
        m_AnimT0 = GetTickCount64();

        SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
        SetWindowPos(HWND_TOPMOST, xPos, yPos + Sc(10), m_size.cx, m_size.cy,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::SetForegroundWindow(m_hWnd);
        SetTimer(TFY_TIMER_ANIM, 16, NULL);
        SetTimer(TFY_TIMER_ALIVE, 500, NULL);
        InvalidateRect(NULL, FALSE);
    }

    VOID FadeOut();

    int HitTest(POINT pt, BOOL *pbClose)
    {
        *pbClose = FALSE;
        for (SIZE_T i = 0; i < m_Cards.GetCount(); i++)
        {
            if (PtInRect(&m_Cards[i].rcClose, pt) && (int)i == m_iHot)
            {
                *pbClose = TRUE;
                return (int)i;
            }
            if (PtInRect(&m_Cards[i].rc, pt))
                return (int)i;
        }
        return -1;
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);
        RECT rc;
        GetClientRect(&rc);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

        HBRUSH hbr = CreateSolidBrush(m_Pal.PanelBg);
        FillRect(hdcMem, &rc, hbr);
        DeleteObject(hbr);
        HBRUSH hbrEdge = CreateSolidBrush(m_Pal.Border);
        FrameRect(hdcMem, &rc, hbrEdge);
        DeleteObject(hbrEdge);

        SetBkMode(hdcMem, TRANSPARENT);
        HGDIOBJ hFontOld = SelectObject(hdcMem, m_hFont);

        for (SIZE_T i = 0; i < m_Cards.GetCount(); i++)
        {
            TFYCARD &card = m_Cards[i];
            BOOL bHot = ((int)i == m_iHot);

            if (bHot)
            {
                HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
                FillRect(hdcMem, &card.rc, hbrHot);
                DeleteObject(hbrHot);
                HBRUSH hbrHotEdge = CreateSolidBrush(m_Pal.HotBorder);
                FrameRect(hdcMem, &card.rc, hbrHotEdge);
                DeleteObject(hbrHotEdge);
            }

            RECT rcTitle = { card.rc.left + Sc(6), card.rc.top,
                             card.rcClose.left - Sc(4), card.rc.top + TitleH() };
            if (card.hIcon)
            {
                DrawIconEx(hdcMem, rcTitle.left, card.rc.top + (TitleH() - Sc(16)) / 2,
                           card.hIcon, Sc(16), Sc(16), 0, NULL, DI_NORMAL);
            }
            rcTitle.left += Sc(22);
            SetTextColor(hdcMem, m_Pal.PanelText);
            DrawTextW(hdcMem, card.szTitle, -1, &rcTitle,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

            if (bHot)
            {
                COLORREF crX = m_bCloseHot ? RGB(232, 17, 35) : m_Pal.DimText;
                HPEN hpen = CreatePen(PS_SOLID, Sc(2), crX);
                HGDIOBJ hpenOld = SelectObject(hdcMem, hpen);
                int cxm = (card.rcClose.left + card.rcClose.right) / 2;
                int cym = (card.rcClose.top + card.rcClose.bottom) / 2;
                int r = Sc(4);
                MoveToEx(hdcMem, cxm - r, cym - r, NULL);
                LineTo(hdcMem, cxm + r, cym + r);
                MoveToEx(hdcMem, cxm + r, cym - r, NULL);
                LineTo(hdcMem, cxm - r, cym + r);
                SelectObject(hdcMem, hpenOld);
                DeleteObject(hpen);
            }

            RECT rcThumbBox = { card.rc.left + Sc(8), card.rc.top + TitleH(),
                                card.rc.left + Sc(8) + ThumbW(),
                                card.rc.top + TitleH() + ThumbH() };
            HBRUSH hbrBox = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.Border, 60));
            FillRect(hdcMem, &rcThumbBox, hbrBox);
            DeleteObject(hbrBox);

            if (card.hbmThumb)
            {
                HDC hdcThumb = CreateCompatibleDC(hdcMem);
                HGDIOBJ hOldThumb = SelectObject(hdcThumb, card.hbmThumb);
                int tx = rcThumbBox.left + (ThumbW() - card.thumbSize.cx) / 2;
                int ty = rcThumbBox.top + (ThumbH() - card.thumbSize.cy) / 2;
                BitBlt(hdcMem, tx, ty, card.thumbSize.cx, card.thumbSize.cy,
                       hdcThumb, 0, 0, SRCCOPY);
                SelectObject(hdcThumb, hOldThumb);
                DeleteDC(hdcThumb);
            }
            else if (card.hIcon)
            {
                DrawIconEx(hdcMem,
                           rcThumbBox.left + (ThumbW() - Sc(32)) / 2,
                           rcThumbBox.top + (ThumbH() - Sc(32)) / 2,
                           card.hIcon, Sc(32), Sc(32), 0, NULL, DI_NORMAL);
            }

            HBRUSH hbrThumbEdge = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 110));
            FrameRect(hdcMem, &rcThumbBox, hbrThumbEdge);
            DeleteObject(hbrThumbEdge);
        }

        SelectObject(hdcMem, hFontOld);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == TFY_TIMER_ANIM)
        {
            ULONGLONG now = GetTickCount64();
            if (m_AnimPhase == TFY_OPEN)
            {
                double t = (double)(now - m_AnimT0) / 160.0;
                double e = TfyEase(t);
                SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)(255.0 * e), LWA_ALPHA);
                SetWindowPos(NULL, m_ptFinal.x, m_ptFinal.y + (int)(Sc(10) * (1.0 - e)), 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (t >= 1.0)
                {
                    m_AnimPhase = TFY_NONE;
                    KillTimer(TFY_TIMER_ANIM);
                }
            }
            else if (m_AnimPhase == TFY_CLOSE)
            {
                double t = (double)(now - m_AnimT0) / 120.0;
                if (t >= 1.0)
                {
                    KillTimer(TFY_TIMER_ANIM);
                    DestroyWindow();
                    return 0;
                }
                SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)(255.0 * (1.0 - TfyEase(t))), LWA_ALPHA);
            }
            else
            {
                KillTimer(TFY_TIMER_ANIM);
            }
        }
        else if (wParam == TFY_TIMER_ALIVE)
        {
            BOOL bChanged = FALSE;
            SIZE_T nKept = 0;
            for (SIZE_T i = 0; i < m_Cards.GetCount(); i++)
            {
                if (!::IsWindow(m_Cards[i].hWnd))
                {
                    if (m_Cards[i].hbmThumb)
                        DeleteObject(m_Cards[i].hbmThumb);
                    bChanged = TRUE;
                    continue;
                }
                if (nKept != i)
                    m_Cards[nKept] = m_Cards[i];
                nKept++;
            }
            if (bChanged)
                m_Cards.SetCount(nKept);
            if (bChanged)
            {
                if (m_Cards.GetCount() == 0)
                {
                    FadeOut();
                    return 0;
                }
                m_iHot = -1;
                LayoutCards();
                SetWindowPos(NULL, 0, 0, m_size.cx, m_size.cy,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                InvalidateRect(NULL, FALSE);
            }
        }
        return 0;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        BOOL bClose;
        int iHot = HitTest(pt, &bClose);
        if (iHot != m_iHot || bClose != m_bCloseHot)
        {
            m_iHot = iHot;
            m_bCloseHot = bClose;
            InvalidateRect(NULL, FALSE);
        }
        if (!m_bTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
            TrackMouseEvent(&tme);
            m_bTracking = TRUE;
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_bTracking = FALSE;
        if (m_iHot != -1)
        {
            m_iHot = -1;
            m_bCloseHot = FALSE;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        BOOL bClose;
        int i = HitTest(pt, &bClose);

        if (i < 0)
            return 0;

        HWND hwndTarget = m_Cards[i].hWnd;
        if (bClose)
        {
            ::PostMessageW(hwndTarget, WM_CLOSE, 0, 0);
            return 0;
        }

        if (::IsWindow(hwndTarget))
            ::SwitchToThisWindow(hwndTarget, TRUE);
        FadeOut();
        return 0;
    }

    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == WA_INACTIVE)
            FadeOut();
        return 0;
    }

    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == VK_ESCAPE)
            FadeOut();
        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        KillTimer(TFY_TIMER_ANIM);
        KillTimer(TFY_TIMER_ALIVE);
        FreeCards();
        if (m_hFont)
        {
            DeleteObject(m_hFont);
            m_hFont = NULL;
        }
        return 0;
    }

    virtual void OnFinalMessage(HWND hWnd) override;

    BEGIN_MSG_MAP(CTaskPreviewWnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()
};

static CTaskPreviewWnd *g_pTaskPreview = NULL;
static ULONGLONG g_TpDismissTick = 0;
static INT_PTR g_TpDismissGroup = 0;

void CTaskPreviewWnd::OnFinalMessage(HWND hWnd)
{
    if (g_pTaskPreview == this)
        g_pTaskPreview = NULL;
    delete this;
}

VOID CTaskPreviewWnd::FadeOut()
{
    if (m_AnimPhase == TFY_CLOSE)
        return;
    g_TpDismissTick = GetTickCount64();
    g_TpDismissGroup = m_nGroupId;
    m_AnimPhase = TFY_CLOSE;
    m_AnimT0 = g_TpDismissTick;
    SetTimer(TFY_TIMER_ANIM, 16, NULL);
}

VOID TaskPreview_Show(IN HWND hwndOwner, IN const RECT *prcAnchor,
                      IN const HWND *pahWnd, IN UINT cWindows, IN INT_PTR nGroupId)
{
    if (nGroupId == g_TpDismissGroup &&
        GetTickCount64() - g_TpDismissTick < 350)
        return;

    TaskPreview_Hide();

    if (cWindows == 0)
        return;

    CTaskPreviewWnd *pPreview = new CTaskPreviewWnd();
    if (!pPreview)
        return;

    if (!pPreview->Create(hwndOwner, CWindow::rcDefault, NULL))
    {
        delete pPreview;
        return;
    }

    g_pTaskPreview = pPreview;
    pPreview->Popup(hwndOwner, prcAnchor, pahWnd, cWindows, nGroupId);
}

VOID TaskPreview_Hide(VOID)
{
    if (g_pTaskPreview && g_pTaskPreview->IsWindow())
        g_pTaskPreview->DestroyWindow();
    g_pTaskPreview = NULL;
}

BOOL TaskPreview_IsVisibleFor(IN INT_PTR nGroupId)
{
    return g_pTaskPreview != NULL &&
           g_pTaskPreview->IsWindow() &&
           g_pTaskPreview->IsWindowVisible() &&
           g_pTaskPreview->m_AnimPhase != TFY_CLOSE &&
           g_pTaskPreview->m_nGroupId == nGroupId;
}

class CTrayCalendarWnd :
    public CWindowImpl<CTrayCalendarWnd, CWindow, CTrayFlyoutTraits>
{
public:
    DECLARE_WND_CLASS_EX(L"TrayCalendar", CS_DROPSHADOW, COLOR_WINDOW)

    SM2_FLYOUT_PALETTE m_Pal;
    HFONT m_hFontTime;
    HFONT m_hFontHeader;
    HFONT m_hFont;
    HFONT m_hFontSmall;
    int m_ViewYear;
    int m_ViewMonth;
    int m_iHotCell;
    int m_iHotNav;
    BOOL m_bTracking;
    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    SIZE m_size;
    RECT m_rcPrev, m_rcNext, m_rcGrid;
    int m_FirstDayOfWeek;

    CTrayCalendarWnd() : m_hFontTime(NULL), m_hFontHeader(NULL), m_hFont(NULL), m_hFontSmall(NULL),
                         m_ViewYear(0), m_ViewMonth(0), m_iHotCell(-1), m_iHotNav(0),
                         m_bTracking(FALSE), m_AnimPhase(TFY_NONE), m_AnimT0(0),
                         m_FirstDayOfWeek(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        ZeroMemory(&m_size, sizeof(m_size));
    }

    int Sc(int v) const { return ShellScaleForDpi(v); }
    int CellW() const { return Sc(38); }
    int CellH() const { return Sc(32); }

    static int DayOfWeek(int year, int month, int day)
    {
        SYSTEMTIME st;
        FILETIME ft;
        ZeroMemory(&st, sizeof(st));
        st.wYear = (WORD)year;
        st.wMonth = (WORD)month;
        st.wDay = (WORD)day;
        if (!SystemTimeToFileTime(&st, &ft))
            return 0;
        FileTimeToSystemTime(&ft, &st);
        return st.wDayOfWeek;
    }

    static int DaysInMonth(int year, int month)
    {
        static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
            return 29;
        return days[month - 1];
    }

    VOID Layout()
    {
        int pad = Sc(16);
        m_size.cx = pad * 2 + CellW() * 7;

        int y = pad;
        y += Sc(42);
        y += Sc(24);
        y += Sc(12);
        int headerY = y;
        y += Sc(30);
        y += Sc(22);
        SetRect(&m_rcGrid, pad, y, pad + CellW() * 7, y + CellH() * 6);
        y += CellH() * 6 + pad;
        m_size.cy = y;

        SetRect(&m_rcPrev, m_size.cx - pad - Sc(56), headerY, m_size.cx - pad - Sc(30), headerY + Sc(26));
        SetRect(&m_rcNext, m_size.cx - pad - Sc(26), headerY, m_size.cx - pad, headerY + Sc(26));
    }

    VOID Toggle(HWND hwndOwner, const RECT *prcAnchor)
    {
        MONITORINFO mi;
        POINT ptRef;
        int xPos, yPos;
        SYSTEMTIME stNow;

        StartMenu2_GetFlyoutPalette(&m_Pal);

        if (!m_hFontTime) m_hFontTime = TfyCreateFont(30, FW_LIGHT);
        if (!m_hFontHeader) m_hFontHeader = TfyCreateFont(14, FW_SEMIBOLD);
        if (!m_hFont) m_hFont = TfyCreateFont(12, FW_NORMAL);
        if (!m_hFontSmall) m_hFontSmall = TfyCreateFont(11, FW_NORMAL);

        GetLocalTime(&stNow);
        m_ViewYear = stNow.wYear;
        m_ViewMonth = stNow.wMonth;
        m_iHotCell = -1;
        m_iHotNav = 0;

        WCHAR szFirst[4];
        m_FirstDayOfWeek = 0;
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK, szFirst, _countof(szFirst)))
            m_FirstDayOfWeek = (szFirst[0] - L'0' + 1) % 7;

        Layout();

        ptRef.x = prcAnchor->right;
        ptRef.y = prcAnchor->top;
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST), &mi);

        xPos = prcAnchor->right - m_size.cx;
        if (xPos + m_size.cx > mi.rcMonitor.right) xPos = mi.rcMonitor.right - m_size.cx;
        if (xPos < mi.rcMonitor.left) xPos = mi.rcMonitor.left;
        yPos = prcAnchor->top - m_size.cy - Sc(6);
        if (yPos < mi.rcMonitor.top) yPos = prcAnchor->bottom + Sc(6);

        m_ptFinal.x = xPos;
        m_ptFinal.y = yPos;

        m_AnimPhase = TFY_OPEN;
        m_AnimT0 = GetTickCount64();

        SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
        SetWindowPos(HWND_TOPMOST, xPos, yPos + Sc(10), m_size.cx, m_size.cy,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::SetForegroundWindow(m_hWnd);
        SetTimer(TFY_TIMER_ANIM, 16, NULL);
        SetTimer(TFY_TIMER_TICK, 1000, NULL);
        InvalidateRect(NULL, FALSE);
    }

    VOID FadeOut();

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);
        RECT rc;
        GetClientRect(&rc);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

        HBRUSH hbr = CreateSolidBrush(m_Pal.PanelBg);
        FillRect(hdcMem, &rc, hbr);
        DeleteObject(hbr);
        HBRUSH hbrEdge = CreateSolidBrush(m_Pal.Border);
        FrameRect(hdcMem, &rc, hbrEdge);
        DeleteObject(hbrEdge);

        SetBkMode(hdcMem, TRANSPARENT);

        SYSTEMTIME stNow;
        GetLocalTime(&stNow);

        int pad = Sc(16);
        WCHAR szBuf[128];

        GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &stNow, NULL, szBuf, _countof(szBuf));
        RECT rcTime = { pad, pad, rc.right - pad, pad + Sc(42) };
        HGDIOBJ hFontOld = SelectObject(hdcMem, m_hFontTime);
        SetTextColor(hdcMem, m_Pal.PanelText);
        DrawTextW(hdcMem, szBuf, -1, &rcTime, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &stNow, NULL, szBuf, _countof(szBuf));
        RECT rcDate = { pad, rcTime.bottom, rc.right - pad, rcTime.bottom + Sc(24) };
        SelectObject(hdcMem, m_hFont);
        SetTextColor(hdcMem, m_Pal.DimText);
        DrawTextW(hdcMem, szBuf, -1, &rcDate, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        RECT rcSep = { pad, rcDate.bottom + Sc(4), rc.right - pad, rcDate.bottom + Sc(5) };
        HBRUSH hbrSep = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 90));
        FillRect(hdcMem, &rcSep, hbrSep);
        DeleteObject(hbrSep);

        SYSTEMTIME stView;
        ZeroMemory(&stView, sizeof(stView));
        stView.wYear = (WORD)m_ViewYear;
        stView.wMonth = (WORD)m_ViewMonth;
        stView.wDay = 1;
        GetDateFormatW(LOCALE_USER_DEFAULT, 0, &stView, L"MMMM yyyy", szBuf, _countof(szBuf));
        RECT rcHeader = { pad, m_rcPrev.top, m_rcPrev.left - Sc(6), m_rcPrev.bottom };
        SelectObject(hdcMem, m_hFontHeader);
        SetTextColor(hdcMem, m_Pal.PanelText);
        DrawTextW(hdcMem, szBuf, -1, &rcHeader, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        for (int nav = 1; nav <= 2; nav++)
        {
            const RECT *prcNav = (nav == 1) ? &m_rcPrev : &m_rcNext;
            if (m_iHotNav == nav)
            {
                HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
                FillRect(hdcMem, prcNav, hbrHot);
                DeleteObject(hbrHot);
            }
            int cxm = (prcNav->left + prcNav->right) / 2;
            int cym = (prcNav->top + prcNav->bottom) / 2;
            int r = Sc(4);
            POINT pts[3];
            if (nav == 1)
            {
                pts[0].x = cxm + r / 2; pts[0].y = cym - r;
                pts[1].x = cxm + r / 2; pts[1].y = cym + r;
                pts[2].x = cxm - r;     pts[2].y = cym;
            }
            else
            {
                pts[0].x = cxm - r / 2; pts[0].y = cym - r;
                pts[1].x = cxm - r / 2; pts[1].y = cym + r;
                pts[2].x = cxm + r;     pts[2].y = cym;
            }
            HBRUSH hbrTri = CreateSolidBrush(m_Pal.PanelText);
            HGDIOBJ hbrOld2 = SelectObject(hdcMem, hbrTri);
            HGDIOBJ hpenOld2 = SelectObject(hdcMem, GetStockObject(NULL_PEN));
            Polygon(hdcMem, pts, 3);
            SelectObject(hdcMem, hpenOld2);
            SelectObject(hdcMem, hbrOld2);
            DeleteObject(hbrTri);
        }

        SelectObject(hdcMem, m_hFontSmall);
        SetTextColor(hdcMem, m_Pal.DimText);
        for (int i = 0; i < 7; i++)
        {
            int dow = (m_FirstDayOfWeek + i) % 7;
            static const int localeIds[7] =
            {
                LOCALE_SABBREVDAYNAME7, LOCALE_SABBREVDAYNAME1, LOCALE_SABBREVDAYNAME2,
                LOCALE_SABBREVDAYNAME3, LOCALE_SABBREVDAYNAME4, LOCALE_SABBREVDAYNAME5,
                LOCALE_SABBREVDAYNAME6
            };
            if (!GetLocaleInfoW(LOCALE_USER_DEFAULT, localeIds[dow], szBuf, _countof(szBuf)))
                szBuf[0] = 0;
            szBuf[2] = 0;
            RECT rcDay = { m_rcGrid.left + i * CellW(), m_rcGrid.top - Sc(22),
                           m_rcGrid.left + (i + 1) * CellW(), m_rcGrid.top };
            DrawTextW(hdcMem, szBuf, -1, &rcDay, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        }

        int firstDow = DayOfWeek(m_ViewYear, m_ViewMonth, 1);
        int lead = (firstDow - m_FirstDayOfWeek + 7) % 7;
        int days = DaysInMonth(m_ViewYear, m_ViewMonth);

        SelectObject(hdcMem, m_hFont);
        for (int cell = 0; cell < 42; cell++)
        {
            int day = cell - lead + 1;
            if (day < 1 || day > days)
                continue;

            int col = cell % 7;
            int row = cell / 7;
            RECT rcCell = { m_rcGrid.left + col * CellW(), m_rcGrid.top + row * CellH(),
                            m_rcGrid.left + (col + 1) * CellW(), m_rcGrid.top + (row + 1) * CellH() };

            BOOL bToday = (m_ViewYear == stNow.wYear && m_ViewMonth == stNow.wMonth && day == stNow.wDay);

            if (bToday)
            {
                HBRUSH hbrToday = CreateSolidBrush(m_Pal.AccentBg);
                RECT rcFill = rcCell;
                InflateRect(&rcFill, -Sc(2), -Sc(2));
                FillRect(hdcMem, &rcFill, hbrToday);
                DeleteObject(hbrToday);
                SetTextColor(hdcMem, m_Pal.AccentText);
            }
            else if (cell == m_iHotCell)
            {
                HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
                RECT rcFill = rcCell;
                InflateRect(&rcFill, -Sc(2), -Sc(2));
                FillRect(hdcMem, &rcFill, hbrHot);
                DeleteObject(hbrHot);
                SetTextColor(hdcMem, m_Pal.PanelText);
            }
            else
            {
                SetTextColor(hdcMem, m_Pal.PanelText);
            }

            StringCchPrintfW(szBuf, _countof(szBuf), L"%d", day);
            DrawTextW(hdcMem, szBuf, -1, &rcCell, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        }

        SelectObject(hdcMem, hFontOld);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == TFY_TIMER_ANIM)
        {
            ULONGLONG now = GetTickCount64();
            if (m_AnimPhase == TFY_OPEN)
            {
                double t = (double)(now - m_AnimT0) / 160.0;
                double e = TfyEase(t);
                SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)(255.0 * e), LWA_ALPHA);
                SetWindowPos(NULL, m_ptFinal.x, m_ptFinal.y + (int)(Sc(10) * (1.0 - e)), 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (t >= 1.0)
                {
                    m_AnimPhase = TFY_NONE;
                    KillTimer(TFY_TIMER_ANIM);
                }
            }
            else if (m_AnimPhase == TFY_CLOSE)
            {
                double t = (double)(now - m_AnimT0) / 120.0;
                if (t >= 1.0)
                {
                    KillTimer(TFY_TIMER_ANIM);
                    DestroyWindow();
                    return 0;
                }
                SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)(255.0 * (1.0 - TfyEase(t))), LWA_ALPHA);
            }
            else
            {
                KillTimer(TFY_TIMER_ANIM);
            }
        }
        else if (wParam == TFY_TIMER_TICK)
        {
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    int CellHitTest(POINT pt)
    {
        if (!PtInRect(&m_rcGrid, pt))
            return -1;
        int col = (pt.x - m_rcGrid.left) / CellW();
        int row = (pt.y - m_rcGrid.top) / CellH();
        if (col < 0 || col > 6 || row < 0 || row > 5)
            return -1;
        return row * 7 + col;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int iCell = CellHitTest(pt);
        int iNav = PtInRect(&m_rcPrev, pt) ? 1 : (PtInRect(&m_rcNext, pt) ? 2 : 0);
        if (iCell != m_iHotCell || iNav != m_iHotNav)
        {
            m_iHotCell = iCell;
            m_iHotNav = iNav;
            InvalidateRect(NULL, FALSE);
        }
        if (!m_bTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
            TrackMouseEvent(&tme);
            m_bTracking = TRUE;
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_bTracking = FALSE;
        if (m_iHotCell != -1 || m_iHotNav != 0)
        {
            m_iHotCell = -1;
            m_iHotNav = 0;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&m_rcPrev, pt))
        {
            if (--m_ViewMonth < 1)
            {
                m_ViewMonth = 12;
                m_ViewYear--;
            }
            InvalidateRect(NULL, FALSE);
        }
        else if (PtInRect(&m_rcNext, pt))
        {
            if (++m_ViewMonth > 12)
            {
                m_ViewMonth = 1;
                m_ViewYear++;
            }
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == WA_INACTIVE)
            FadeOut();
        return 0;
    }

    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == VK_ESCAPE)
            FadeOut();
        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        KillTimer(TFY_TIMER_ANIM);
        KillTimer(TFY_TIMER_TICK);
        if (m_hFontTime) { DeleteObject(m_hFontTime); m_hFontTime = NULL; }
        if (m_hFontHeader) { DeleteObject(m_hFontHeader); m_hFontHeader = NULL; }
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall); m_hFontSmall = NULL; }
        return 0;
    }

    virtual void OnFinalMessage(HWND hWnd) override;

    BEGIN_MSG_MAP(CTrayCalendarWnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()
};

static CTrayCalendarWnd *g_pTrayCalendar = NULL;
static ULONGLONG g_CalDismissTick = 0;

void CTrayCalendarWnd::OnFinalMessage(HWND hWnd)
{
    if (g_pTrayCalendar == this)
        g_pTrayCalendar = NULL;
    delete this;
}

VOID CTrayCalendarWnd::FadeOut()
{
    if (m_AnimPhase == TFY_CLOSE)
        return;
    g_CalDismissTick = GetTickCount64();
    m_AnimPhase = TFY_CLOSE;
    m_AnimT0 = g_CalDismissTick;
    SetTimer(TFY_TIMER_ANIM, 16, NULL);
}

VOID TrayCalendar_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor)
{
    if (GetTickCount64() - g_CalDismissTick < 350)
        return;

    if (g_pTrayCalendar && g_pTrayCalendar->IsWindow())
    {
        if (g_pTrayCalendar->IsWindowVisible() &&
            g_pTrayCalendar->m_AnimPhase != TFY_CLOSE)
        {
            g_pTrayCalendar->FadeOut();
            return;
        }
        g_pTrayCalendar->DestroyWindow();
        g_pTrayCalendar = NULL;
    }

    CTrayCalendarWnd *pCalendar = new CTrayCalendarWnd();
    if (!pCalendar)
        return;

    if (!pCalendar->Create(hwndOwner, CWindow::rcDefault, NULL))
    {
        delete pCalendar;
        return;
    }

    g_pTrayCalendar = pCalendar;
    pCalendar->Toggle(hwndOwner, prcAnchor);
}

VOID TrayFlyouts_Destroy(VOID)
{
    TaskPreview_Hide();
    if (g_pTrayCalendar && g_pTrayCalendar->IsWindow())
        g_pTrayCalendar->DestroyWindow();
    g_pTrayCalendar = NULL;
    TrayFlyoutsAux_Destroy();
}

#include <mmsystem.h>

struct TFYVOLUME
{
    HMIXER hMixer;
    DWORD dwVolumeId;
    DWORD dwMuteId;
    DWORD cChannels;
    DWORD dwMin, dwMax;
    BOOL bValid;

    TFYVOLUME() : hMixer(NULL), dwVolumeId(0), dwMuteId(0), cChannels(1),
                  dwMin(0), dwMax(65535), bValid(FALSE) {}

    BOOL Open()
    {
        MIXERLINEW line;
        MIXERLINECONTROLSW ctrls;
        MIXERCONTROLW ctrl;

        Close();

        if (mixerOpen(&hMixer, 0, 0, 0, 0) != MMSYSERR_NOERROR)
            return FALSE;

        ZeroMemory(&line, sizeof(line));
        line.cbStruct = sizeof(line);
        line.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;
        if (mixerGetLineInfoW((HMIXEROBJ)hMixer, &line,
                              MIXER_GETLINEINFOF_COMPONENTTYPE) != MMSYSERR_NOERROR)
        {
            ZeroMemory(&line, sizeof(line));
            line.cbStruct = sizeof(line);
            line.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_HEADPHONES;
            if (mixerGetLineInfoW((HMIXEROBJ)hMixer, &line,
                                  MIXER_GETLINEINFOF_COMPONENTTYPE) != MMSYSERR_NOERROR)
            {
                Close();
                return FALSE;
            }
        }

        cChannels = line.cChannels;
        if (cChannels < 1) cChannels = 1;
        if (cChannels > 2) cChannels = 2;

        ZeroMemory(&ctrls, sizeof(ctrls));
        ZeroMemory(&ctrl, sizeof(ctrl));
        ctrls.cbStruct = sizeof(ctrls);
        ctrls.dwLineID = line.dwLineID;
        ctrls.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
        ctrls.cControls = 1;
        ctrls.cbmxctrl = sizeof(ctrl);
        ctrls.pamxctrl = &ctrl;
        if (mixerGetLineControlsW((HMIXEROBJ)hMixer, &ctrls,
                                  MIXER_GETLINECONTROLSF_ONEBYTYPE) != MMSYSERR_NOERROR)
        {
            Close();
            return FALSE;
        }
        dwVolumeId = ctrl.dwControlID;
        dwMin = ctrl.Bounds.dwMinimum;
        dwMax = ctrl.Bounds.dwMaximum;
        if (dwMax <= dwMin)
        {
            dwMin = 0;
            dwMax = 65535;
        }

        dwMuteId = (DWORD)-1;
        ZeroMemory(&ctrl, sizeof(ctrl));
        ctrls.dwControlType = MIXERCONTROL_CONTROLTYPE_MUTE;
        ctrls.cbmxctrl = sizeof(ctrl);
        ctrls.pamxctrl = &ctrl;
        if (mixerGetLineControlsW((HMIXEROBJ)hMixer, &ctrls,
                                  MIXER_GETLINECONTROLSF_ONEBYTYPE) == MMSYSERR_NOERROR)
        {
            dwMuteId = ctrl.dwControlID;
        }

        bValid = TRUE;
        return TRUE;
    }

    VOID Close()
    {
        if (hMixer)
            mixerClose(hMixer);
        hMixer = NULL;
        bValid = FALSE;
    }

    int GetVolume()
    {
        MIXERCONTROLDETAILS details;
        MIXERCONTROLDETAILS_UNSIGNED values[2];

        if (!bValid)
            return 0;

        ZeroMemory(&details, sizeof(details));
        ZeroMemory(values, sizeof(values));
        details.cbStruct = sizeof(details);
        details.dwControlID = dwVolumeId;
        details.cChannels = cChannels;
        details.cbDetails = sizeof(values[0]);
        details.paDetails = values;
        if (mixerGetControlDetailsW((HMIXEROBJ)hMixer, &details,
                                    MIXER_GETCONTROLDETAILSF_VALUE) != MMSYSERR_NOERROR)
            return 0;

        return MulDiv(values[0].dwValue - dwMin, 100, dwMax - dwMin);
    }

    VOID SetVolume(int nPercent)
    {
        MIXERCONTROLDETAILS details;
        MIXERCONTROLDETAILS_UNSIGNED values[2];
        DWORD i;

        if (!bValid)
            return;
        if (nPercent < 0) nPercent = 0;
        if (nPercent > 100) nPercent = 100;

        for (i = 0; i < cChannels; i++)
            values[i].dwValue = dwMin + MulDiv(nPercent, dwMax - dwMin, 100);

        ZeroMemory(&details, sizeof(details));
        details.cbStruct = sizeof(details);
        details.dwControlID = dwVolumeId;
        details.cChannels = cChannels;
        details.cbDetails = sizeof(values[0]);
        details.paDetails = values;
        mixerSetControlDetails((HMIXEROBJ)hMixer, &details,
                               MIXER_SETCONTROLDETAILSF_VALUE);
    }

    BOOL GetMute()
    {
        MIXERCONTROLDETAILS details;
        MIXERCONTROLDETAILS_BOOLEAN value;

        if (!bValid || dwMuteId == (DWORD)-1)
            return FALSE;

        ZeroMemory(&details, sizeof(details));
        ZeroMemory(&value, sizeof(value));
        details.cbStruct = sizeof(details);
        details.dwControlID = dwMuteId;
        details.cChannels = 1;
        details.cbDetails = sizeof(value);
        details.paDetails = &value;
        if (mixerGetControlDetailsW((HMIXEROBJ)hMixer, &details,
                                    MIXER_GETCONTROLDETAILSF_VALUE) != MMSYSERR_NOERROR)
            return FALSE;

        return value.fValue != 0;
    }

    VOID SetMute(BOOL bMute)
    {
        MIXERCONTROLDETAILS details;
        MIXERCONTROLDETAILS_BOOLEAN value;

        if (!bValid || dwMuteId == (DWORD)-1)
            return;

        value.fValue = bMute ? 1 : 0;
        ZeroMemory(&details, sizeof(details));
        details.cbStruct = sizeof(details);
        details.dwControlID = dwMuteId;
        details.cChannels = 1;
        details.cbDetails = sizeof(value);
        details.paDetails = &value;
        mixerSetControlDetails((HMIXEROBJ)hMixer, &details,
                               MIXER_SETCONTROLDETAILSF_VALUE);
    }
};

class CTrayVolumeWnd :
    public CWindowImpl<CTrayVolumeWnd, CWindow, CTrayFlyoutTraits>
{
public:
    DECLARE_WND_CLASS_EX(L"TrayVolumeFlyout", CS_DROPSHADOW, COLOR_WINDOW)

    SM2_FLYOUT_PALETTE m_Pal;
    TFYVOLUME m_Volume;
    HFONT m_hFont;
    HFONT m_hFontSmall;
    int m_nPercent;
    BOOL m_bMute;
    BOOL m_bDragging;
    int m_iHot;
    BOOL m_bTracking;
    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    SIZE m_size;
    RECT m_rcValue, m_rcTrack, m_rcMute, m_rcMixer;

    CTrayVolumeWnd() : m_hFont(NULL), m_hFontSmall(NULL), m_nPercent(0),
                       m_bMute(FALSE), m_bDragging(FALSE), m_iHot(0),
                       m_bTracking(FALSE), m_AnimPhase(TFY_NONE), m_AnimT0(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        ZeroMemory(&m_size, sizeof(m_size));
    }

    int Sc(int v) const { return ShellScaleForDpi(v); }

    VOID Layout()
    {
        m_size.cx = Sc(64);
        int pad = Sc(12);
        int y = pad;
        SetRect(&m_rcValue, 0, y, m_size.cx, y + Sc(22));
        y += Sc(26);
        SetRect(&m_rcTrack, (m_size.cx - Sc(4)) / 2, y, (m_size.cx + Sc(4)) / 2, y + Sc(160));
        y += Sc(160) + Sc(10);
        SetRect(&m_rcMute, (m_size.cx - Sc(30)) / 2, y, (m_size.cx + Sc(30)) / 2, y + Sc(30));
        y += Sc(34) + Sc(4);
        SetRect(&m_rcMixer, Sc(4), y, m_size.cx - Sc(4), y + Sc(22));
        y += Sc(24) + Sc(8);
        m_size.cy = y;
    }

    VOID Toggle(HWND hwndOwner, const RECT *prcAnchor)
    {
        MONITORINFO mi;
        POINT ptRef;
        int xPos, yPos;

        StartMenu2_GetFlyoutPalette(&m_Pal);

        if (!m_hFont) m_hFont = TfyCreateFont(14, FW_NORMAL);
        if (!m_hFontSmall) m_hFontSmall = TfyCreateFont(11, FW_NORMAL);

        m_Volume.Open();
        m_nPercent = m_Volume.GetVolume();
        m_bMute = m_Volume.GetMute();

        Layout();

        ptRef.x = prcAnchor->right;
        ptRef.y = prcAnchor->top;
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST), &mi);

        xPos = prcAnchor->right - m_size.cx - Sc(40);
        if (xPos + m_size.cx > mi.rcMonitor.right) xPos = mi.rcMonitor.right - m_size.cx;
        if (xPos < mi.rcMonitor.left) xPos = mi.rcMonitor.left;
        yPos = prcAnchor->top - m_size.cy - Sc(6);
        if (yPos < mi.rcMonitor.top) yPos = prcAnchor->bottom + Sc(6);

        m_ptFinal.x = xPos;
        m_ptFinal.y = yPos;

        m_AnimPhase = TFY_OPEN;
        m_AnimT0 = GetTickCount64();

        SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
        SetWindowPos(HWND_TOPMOST, xPos, yPos + Sc(10), m_size.cx, m_size.cy,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::SetForegroundWindow(m_hWnd);
        SetTimer(TFY_TIMER_ANIM, 16, NULL);
        InvalidateRect(NULL, FALSE);
    }

    VOID FadeOut();

    int TrackPercentFromY(int y)
    {
        int h = m_rcTrack.bottom - m_rcTrack.top;
        if (h <= 0)
            return 0;
        int pct = 100 - MulDiv(y - m_rcTrack.top, 100, h);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        return pct;
    }

    VOID ApplyVolume(int nPercent)
    {
        m_nPercent = nPercent;
        m_Volume.SetVolume(nPercent);
        if (m_bMute && nPercent > 0)
        {
            m_bMute = FALSE;
            m_Volume.SetMute(FALSE);
        }
        InvalidateRect(NULL, FALSE);
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);
        RECT rc;
        GetClientRect(&rc);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

        HBRUSH hbr = CreateSolidBrush(m_Pal.PanelBg);
        FillRect(hdcMem, &rc, hbr);
        DeleteObject(hbr);
        HBRUSH hbrEdge = CreateSolidBrush(m_Pal.Border);
        FrameRect(hdcMem, &rc, hbrEdge);
        DeleteObject(hbrEdge);

        SetBkMode(hdcMem, TRANSPARENT);
        HGDIOBJ hFontOld = SelectObject(hdcMem, m_hFont);

        WCHAR szBuf[16];
        StringCchPrintfW(szBuf, _countof(szBuf), L"%d", m_nPercent);
        SetTextColor(hdcMem, m_Pal.PanelText);
        DrawTextW(hdcMem, szBuf, -1, &m_rcValue,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

        HBRUSH hbrTrack = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 110));
        FillRect(hdcMem, &m_rcTrack, hbrTrack);
        DeleteObject(hbrTrack);

        int thumbY = m_rcTrack.bottom - MulDiv(m_nPercent, m_rcTrack.bottom - m_rcTrack.top, 100);
        RECT rcFill = { m_rcTrack.left, thumbY, m_rcTrack.right, m_rcTrack.bottom };
        HBRUSH hbrFill = CreateSolidBrush(m_Pal.AccentBg);
        FillRect(hdcMem, &rcFill, hbrFill);
        DeleteObject(hbrFill);

        RECT rcThumb = { (m_size.cx - Sc(22)) / 2, thumbY - Sc(5),
                         (m_size.cx + Sc(22)) / 2, thumbY + Sc(5) };
        HBRUSH hbrThumb = CreateSolidBrush(m_bDragging ? m_Pal.AccentBg : m_Pal.PanelText);
        FillRect(hdcMem, &rcThumb, hbrThumb);
        DeleteObject(hbrThumb);

        if (m_iHot == 1)
        {
            HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
            FillRect(hdcMem, &m_rcMute, hbrHot);
            DeleteObject(hbrHot);
        }

        {
            int cxm = (m_rcMute.left + m_rcMute.right) / 2 - Sc(3);
            int cym = (m_rcMute.top + m_rcMute.bottom) / 2;
            COLORREF crGlyph = m_bMute ? m_Pal.DimText : m_Pal.PanelText;
            HBRUSH hbrGlyph = CreateSolidBrush(crGlyph);
            HGDIOBJ hbrOld2 = SelectObject(hdcMem, hbrGlyph);
            HGDIOBJ hpenOld2 = SelectObject(hdcMem, GetStockObject(NULL_PEN));
            RECT rcBox = { cxm - Sc(7), cym - Sc(3), cxm - Sc(2), cym + Sc(3) };
            FillRect(hdcMem, &rcBox, hbrGlyph);
            POINT pts[3];
            pts[0].x = cxm - Sc(2); pts[0].y = cym - Sc(3);
            pts[1].x = cxm + Sc(3); pts[1].y = cym - Sc(8);
            pts[2].x = cxm + Sc(3); pts[2].y = cym + Sc(8);
            Polygon(hdcMem, pts, 3);
            SelectObject(hdcMem, hpenOld2);
            SelectObject(hdcMem, hbrOld2);
            DeleteObject(hbrGlyph);

            HPEN hpen = CreatePen(PS_SOLID, Sc(2), crGlyph);
            HGDIOBJ hpenOld3 = SelectObject(hdcMem, hpen);
            if (m_bMute)
            {
                MoveToEx(hdcMem, cxm + Sc(6), cym - Sc(5), NULL);
                LineTo(hdcMem, cxm + Sc(13), cym + Sc(5));
                MoveToEx(hdcMem, cxm + Sc(13), cym - Sc(5), NULL);
                LineTo(hdcMem, cxm + Sc(6), cym + Sc(5));
            }
            else
            {
                MoveToEx(hdcMem, cxm + Sc(6), cym - Sc(3), NULL);
                LineTo(hdcMem, cxm + Sc(8), cym);
                LineTo(hdcMem, cxm + Sc(6), cym + Sc(3));
                MoveToEx(hdcMem, cxm + Sc(9), cym - Sc(6), NULL);
                LineTo(hdcMem, cxm + Sc(12), cym);
                LineTo(hdcMem, cxm + Sc(9), cym + Sc(6));
            }
            SelectObject(hdcMem, hpenOld3);
            DeleteObject(hpen);
        }

        SelectObject(hdcMem, m_hFontSmall);
        SetTextColor(hdcMem, m_iHot == 2 ? m_Pal.HotBorder : m_Pal.DimText);
        DrawTextW(hdcMem, L"Mixer", -1, &m_rcMixer,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

        SelectObject(hdcMem, hFontOld);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == TFY_TIMER_ANIM)
        {
            ULONGLONG now = GetTickCount64();
            if (m_AnimPhase == TFY_OPEN)
            {
                double t = (double)(now - m_AnimT0) / 160.0;
                double e = TfyEase(t);
                SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)(255.0 * e), LWA_ALPHA);
                SetWindowPos(NULL, m_ptFinal.x, m_ptFinal.y + (int)(Sc(10) * (1.0 - e)), 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (t >= 1.0)
                {
                    m_AnimPhase = TFY_NONE;
                    KillTimer(TFY_TIMER_ANIM);
                }
            }
            else if (m_AnimPhase == TFY_CLOSE)
            {
                double t = (double)(now - m_AnimT0) / 120.0;
                if (t >= 1.0)
                {
                    KillTimer(TFY_TIMER_ANIM);
                    DestroyWindow();
                    return 0;
                }
                SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)(255.0 * (1.0 - TfyEase(t))), LWA_ALPHA);
            }
            else
            {
                KillTimer(TFY_TIMER_ANIM);
            }
        }
        return 0;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (m_bDragging)
        {
            ApplyVolume(TrackPercentFromY(pt.y));
            return 0;
        }

        int iHot = 0;
        if (PtInRect(&m_rcMute, pt)) iHot = 1;
        else if (PtInRect(&m_rcMixer, pt)) iHot = 2;
        if (iHot != m_iHot)
        {
            m_iHot = iHot;
            InvalidateRect(NULL, FALSE);
        }
        if (!m_bTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
            TrackMouseEvent(&tme);
            m_bTracking = TRUE;
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_bTracking = FALSE;
        if (m_iHot)
        {
            m_iHot = 0;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rcSlider = { 0, m_rcTrack.top - Sc(6), m_size.cx, m_rcTrack.bottom + Sc(6) };
        if (PtInRect(&rcSlider, pt))
        {
            m_bDragging = TRUE;
            SetCapture();
            ApplyVolume(TrackPercentFromY(pt.y));
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (m_bDragging)
        {
            m_bDragging = FALSE;
            ReleaseCapture();
            InvalidateRect(NULL, FALSE);
            return 0;
        }

        if (PtInRect(&m_rcMute, pt))
        {
            m_bMute = !m_bMute;
            m_Volume.SetMute(m_bMute);
            InvalidateRect(NULL, FALSE);
        }
        else if (PtInRect(&m_rcMixer, pt))
        {
            ShellExecuteW(NULL, NULL, L"sndvol32.exe", NULL, NULL, SW_SHOWNORMAL);
            FadeOut();
        }
        return 0;
    }

    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int pct = m_nPercent + (delta > 0 ? 5 : -5);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        ApplyVolume(pct);
        return 0;
    }

    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == WA_INACTIVE)
            FadeOut();
        return 0;
    }

    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == VK_ESCAPE)
            FadeOut();
        else if (wParam == VK_UP)
            ApplyVolume(min(m_nPercent + 5, 100));
        else if (wParam == VK_DOWN)
            ApplyVolume(max(m_nPercent - 5, 0));
        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        KillTimer(TFY_TIMER_ANIM);
        m_Volume.Close();
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall); m_hFontSmall = NULL; }
        return 0;
    }

    virtual void OnFinalMessage(HWND hWnd) override;

    BEGIN_MSG_MAP(CTrayVolumeWnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()
};

static CTrayVolumeWnd *g_pTrayVolume = NULL;
static ULONGLONG g_VolDismissTick = 0;

void CTrayVolumeWnd::OnFinalMessage(HWND hWnd)
{
    if (g_pTrayVolume == this)
        g_pTrayVolume = NULL;
    delete this;
}

VOID CTrayVolumeWnd::FadeOut()
{
    if (m_AnimPhase == TFY_CLOSE)
        return;
    g_VolDismissTick = GetTickCount64();
    m_AnimPhase = TFY_CLOSE;
    m_AnimT0 = g_VolDismissTick;
    SetTimer(TFY_TIMER_ANIM, 16, NULL);
}

VOID TrayVolume_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor)
{
    if (GetTickCount64() - g_VolDismissTick < 350)
        return;

    if (g_pTrayVolume && g_pTrayVolume->IsWindow())
    {
        if (g_pTrayVolume->IsWindowVisible() &&
            g_pTrayVolume->m_AnimPhase != TFY_CLOSE)
        {
            g_pTrayVolume->FadeOut();
            return;
        }
        g_pTrayVolume->DestroyWindow();
        g_pTrayVolume = NULL;
    }

    CTrayVolumeWnd *pVolume = new CTrayVolumeWnd();
    if (!pVolume)
        return;

    if (!pVolume->Create(hwndOwner, CWindow::rcDefault, NULL))
    {
        delete pVolume;
        return;
    }

    g_pTrayVolume = pVolume;
    pVolume->Toggle(hwndOwner, prcAnchor);
}

#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <wlanapi.h>

typedef DWORD (WINAPI *PFN_WLANOPENHANDLE)(DWORD, PVOID, PDWORD, PHANDLE);
typedef DWORD (WINAPI *PFN_WLANCLOSEHANDLE)(HANDLE, PVOID);
typedef DWORD (WINAPI *PFN_WLANENUMINTERFACES)(HANDLE, PVOID, PWLAN_INTERFACE_INFO_LIST *);
typedef DWORD (WINAPI *PFN_WLANGETNETWORKLIST)(HANDLE, const GUID *, DWORD, PVOID, PWLAN_AVAILABLE_NETWORK_LIST *);
typedef DWORD (WINAPI *PFN_WLANCONNECT)(HANDLE, const GUID *, const PWLAN_CONNECTION_PARAMETERS, PVOID);
typedef VOID (WINAPI *PFN_WLANFREEMEMORY)(PVOID);

struct TFYNETROW
{
    WCHAR szName[128];
    WCHAR szStatus[96];
    WCHAR szProfile[128];
    GUID ifGuid;
    int nType;
    int nSignal;
    BOOL bConnected;
    BOOL bHasProfile;
    RECT rc;
};

class CTrayNetworkWnd :
    public CWindowImpl<CTrayNetworkWnd, CWindow, CTrayFlyoutTraits>
{
public:
    DECLARE_WND_CLASS_EX(L"TrayNetworkFlyout", CS_DROPSHADOW, COLOR_WINDOW)

    SM2_FLYOUT_PALETTE m_Pal;
    CAtlArray<TFYNETROW> m_Rows;
    HFONT m_hFont;
    HFONT m_hFontSmall;
    HFONT m_hFontHeader;
    HMODULE m_hWlanApi;
    HANDLE m_hWlan;
    PFN_WLANCONNECT m_pfnConnect;
    PFN_WLANCLOSEHANDLE m_pfnClose;
    PFN_WLANFREEMEMORY m_pfnFree;
    int m_iHot;
    BOOL m_bTracking;
    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    SIZE m_size;
    RECT m_rcLink;

    CTrayNetworkWnd() : m_hFont(NULL), m_hFontSmall(NULL), m_hFontHeader(NULL),
                        m_hWlanApi(NULL), m_hWlan(NULL),
                        m_pfnConnect(NULL), m_pfnClose(NULL), m_pfnFree(NULL),
                        m_iHot(-1), m_bTracking(FALSE),
                        m_AnimPhase(TFY_NONE), m_AnimT0(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        ZeroMemory(&m_size, sizeof(m_size));
    }

    int Sc(int v) const { return ShellScaleForDpi(v); }

    VOID AddAdapters()
    {
        ULONG cbBuffer = 16 * 1024;
        PIP_ADAPTER_ADDRESSES pAddresses;
        ULONG ret;

        for (int attempt = 0; attempt < 3; attempt++)
        {
            pAddresses = (PIP_ADAPTER_ADDRESSES)HeapAlloc(hProcessHeap, 0, cbBuffer);
            if (!pAddresses)
                return;

            ret = GetAdaptersAddresses(AF_INET,
                                       GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                       NULL, pAddresses, &cbBuffer);
            if (ret == ERROR_BUFFER_OVERFLOW)
            {
                HeapFree(hProcessHeap, 0, pAddresses);
                continue;
            }
            if (ret != NO_ERROR)
            {
                HeapFree(hProcessHeap, 0, pAddresses);
                return;
            }
            break;
        }

        for (PIP_ADAPTER_ADDRESSES pCurrent = pAddresses;
             pCurrent != NULL && m_Rows.GetCount() < 12;
             pCurrent = pCurrent->Next)
        {
            if (pCurrent->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                pCurrent->IfType == IF_TYPE_TUNNEL)
                continue;

            TFYNETROW row;
            ZeroMemory(&row, sizeof(row));
            row.nType = (pCurrent->IfType == IF_TYPE_IEEE80211) ? 1 : 0;
            StringCchCopyW(row.szName, _countof(row.szName),
                           pCurrent->FriendlyName ? pCurrent->FriendlyName : L"Network adapter");

            if (pCurrent->OperStatus == IfOperStatusUp)
            {
                row.bConnected = TRUE;
                StringCchCopyW(row.szStatus, _countof(row.szStatus), L"Connected");

                for (PIP_ADAPTER_UNICAST_ADDRESS pAddr = pCurrent->FirstUnicastAddress;
                     pAddr != NULL; pAddr = pAddr->Next)
                {
                    if (pAddr->Address.lpSockaddr &&
                        pAddr->Address.lpSockaddr->sa_family == AF_INET)
                    {
                        BYTE *ip = (BYTE *)&((struct sockaddr_in *)pAddr->Address.lpSockaddr)->sin_addr;
                        StringCchPrintfW(row.szStatus, _countof(row.szStatus),
                                         L"Connected \x2014 %u.%u.%u.%u",
                                         ip[0], ip[1], ip[2], ip[3]);
                        break;
                    }
                }
            }
            else
            {
                StringCchCopyW(row.szStatus, _countof(row.szStatus), L"Not connected");
            }

            m_Rows.Add(row);
        }

        HeapFree(hProcessHeap, 0, pAddresses);
    }

    VOID AddWlanNetworks()
    {
        PFN_WLANOPENHANDLE pfnOpen;
        PFN_WLANENUMINTERFACES pfnEnum;
        PFN_WLANGETNETWORKLIST pfnList;
        DWORD dwVersion = 0;
        PWLAN_INTERFACE_INFO_LIST pInterfaces = NULL;

        if (!m_hWlanApi)
            m_hWlanApi = LoadLibraryW(L"wlanapi.dll");
        if (!m_hWlanApi)
            return;

        pfnOpen = (PFN_WLANOPENHANDLE)GetProcAddress(m_hWlanApi, "WlanOpenHandle");
        pfnEnum = (PFN_WLANENUMINTERFACES)GetProcAddress(m_hWlanApi, "WlanEnumInterfaces");
        pfnList = (PFN_WLANGETNETWORKLIST)GetProcAddress(m_hWlanApi, "WlanGetAvailableNetworkList");
        m_pfnConnect = (PFN_WLANCONNECT)GetProcAddress(m_hWlanApi, "WlanConnect");
        m_pfnClose = (PFN_WLANCLOSEHANDLE)GetProcAddress(m_hWlanApi, "WlanCloseHandle");
        m_pfnFree = (PFN_WLANFREEMEMORY)GetProcAddress(m_hWlanApi, "WlanFreeMemory");

        if (!pfnOpen || !pfnEnum || !pfnList || !m_pfnClose || !m_pfnFree)
            return;

        if (!m_hWlan &&
            pfnOpen(2, NULL, &dwVersion, &m_hWlan) != ERROR_SUCCESS)
        {
            m_hWlan = NULL;
            return;
        }

        if (pfnEnum(m_hWlan, NULL, &pInterfaces) != ERROR_SUCCESS || !pInterfaces)
            return;

        for (DWORD i = 0; i < pInterfaces->dwNumberOfItems && m_Rows.GetCount() < 16; i++)
        {
            PWLAN_AVAILABLE_NETWORK_LIST pNetworks = NULL;
            const GUID *pGuid = &pInterfaces->InterfaceInfo[i].InterfaceGuid;

            if (pfnList(m_hWlan, pGuid, 0, NULL, &pNetworks) != ERROR_SUCCESS || !pNetworks)
                continue;

            for (DWORD n = 0; n < pNetworks->dwNumberOfItems && m_Rows.GetCount() < 16; n++)
            {
                WLAN_AVAILABLE_NETWORK *pNet = &pNetworks->Network[n];
                WCHAR szSsid[DOT11_SSID_MAX_LENGTH + 1];
                UINT cch;

                if (pNet->dot11Ssid.uSSIDLength == 0)
                    continue;

                cch = MultiByteToWideChar(CP_UTF8, 0,
                                          (LPCSTR)pNet->dot11Ssid.ucSSID,
                                          pNet->dot11Ssid.uSSIDLength,
                                          szSsid, _countof(szSsid) - 1);
                szSsid[cch] = 0;

                BOOL bDuplicate = FALSE;
                for (SIZE_T r = 0; r < m_Rows.GetCount(); r++)
                {
                    if (m_Rows[r].nType == 2 && !wcscmp(m_Rows[r].szName, szSsid))
                    {
                        bDuplicate = TRUE;
                        if ((int)pNet->wlanSignalQuality > m_Rows[r].nSignal)
                            m_Rows[r].nSignal = pNet->wlanSignalQuality;
                        break;
                    }
                }
                if (bDuplicate)
                    continue;

                TFYNETROW row;
                ZeroMemory(&row, sizeof(row));
                row.nType = 2;
                row.ifGuid = *pGuid;
                row.nSignal = pNet->wlanSignalQuality;
                row.bConnected = (pNet->dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
                row.bHasProfile = (pNet->dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0;
                StringCchCopyW(row.szName, _countof(row.szName), szSsid);
                if (row.bHasProfile)
                    StringCchCopyW(row.szProfile, _countof(row.szProfile), pNet->strProfileName);
                StringCchCopyW(row.szStatus, _countof(row.szStatus),
                               row.bConnected ? L"Connected" :
                               (row.bHasProfile ? L"Saved" : L""));
                m_Rows.Add(row);
            }

            m_pfnFree(pNetworks);
        }

        m_pfnFree(pInterfaces);
    }

    VOID Layout()
    {
        int y = Sc(10) + Sc(28);
        m_size.cx = Sc(300);

        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            int nRowH = (m_Rows[i].nType == 2) ? Sc(38) : Sc(46);
            SetRect(&m_Rows[i].rc, Sc(8), y, m_size.cx - Sc(8), y + nRowH);
            y += nRowH;
        }

        y += Sc(8);
        SetRect(&m_rcLink, Sc(8), y, m_size.cx - Sc(8), y + Sc(26));
        y += Sc(26) + Sc(10);
        m_size.cy = y;
    }

    VOID Toggle(HWND hwndOwner, const RECT *prcAnchor)
    {
        MONITORINFO mi;
        POINT ptRef;
        int xPos, yPos;

        StartMenu2_GetFlyoutPalette(&m_Pal);

        if (!m_hFont) m_hFont = TfyCreateFont(12, FW_SEMIBOLD);
        if (!m_hFontSmall) m_hFontSmall = TfyCreateFont(11, FW_NORMAL);
        if (!m_hFontHeader) m_hFontHeader = TfyCreateFont(14, FW_SEMIBOLD);

        m_Rows.SetCount(0);
        AddWlanNetworks();
        AddAdapters();
        Layout();

        ptRef.x = prcAnchor->right;
        ptRef.y = prcAnchor->top;
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST), &mi);

        xPos = prcAnchor->right - m_size.cx - Sc(8);
        if (xPos + m_size.cx > mi.rcMonitor.right) xPos = mi.rcMonitor.right - m_size.cx;
        if (xPos < mi.rcMonitor.left) xPos = mi.rcMonitor.left;
        yPos = prcAnchor->top - m_size.cy - Sc(6);
        if (yPos < mi.rcMonitor.top) yPos = prcAnchor->bottom + Sc(6);

        m_ptFinal.x = xPos;
        m_ptFinal.y = yPos;

        m_AnimPhase = TFY_OPEN;
        m_AnimT0 = GetTickCount64();

        SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
        SetWindowPos(HWND_TOPMOST, xPos, yPos + Sc(10), m_size.cx, m_size.cy,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::SetForegroundWindow(m_hWnd);
        SetTimer(TFY_TIMER_ANIM, 16, NULL);
        InvalidateRect(NULL, FALSE);
    }

    VOID FadeOut();

    VOID DrawSignalBars(HDC hdc, int x, int yBottom, int nSignal, COLORREF crOn, COLORREF crOff)
    {
        for (int b = 0; b < 4; b++)
        {
            int hBar = Sc(4) + b * Sc(3);
            RECT rcBar = { x + b * Sc(5), yBottom - hBar, x + b * Sc(5) + Sc(3), yBottom };
            HBRUSH hbr = CreateSolidBrush(nSignal > b * 25 ? crOn : crOff);
            FillRect(hdc, &rcBar, hbr);
            DeleteObject(hbr);
        }
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);
        RECT rc;
        GetClientRect(&rc);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);

        HBRUSH hbr = CreateSolidBrush(m_Pal.PanelBg);
        FillRect(hdcMem, &rc, hbr);
        DeleteObject(hbr);
        HBRUSH hbrEdge = CreateSolidBrush(m_Pal.Border);
        FrameRect(hdcMem, &rc, hbrEdge);
        DeleteObject(hbrEdge);

        SetBkMode(hdcMem, TRANSPARENT);
        HGDIOBJ hFontOld = SelectObject(hdcMem, m_hFontHeader);

        RECT rcTitle = { Sc(12), Sc(8), rc.right - Sc(12), Sc(8) + Sc(26) };
        SetTextColor(hdcMem, m_Pal.PanelText);
        DrawTextW(hdcMem, L"Networks", -1, &rcTitle,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            TFYNETROW &row = m_Rows[i];
            RECT rcRow = row.rc;

            if ((int)i == m_iHot)
            {
                HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
                FillRect(hdcMem, &rcRow, hbrHot);
                DeleteObject(hbrHot);
            }

            int cyMid = (rcRow.top + rcRow.bottom) / 2;
            COLORREF crGlyph = row.bConnected ? m_Pal.PanelText : m_Pal.DimText;

            if (row.nType == 2 || row.nType == 1)
            {
                DrawSignalBars(hdcMem, rcRow.left + Sc(8), cyMid + Sc(7),
                               row.nType == 2 ? row.nSignal : (row.bConnected ? 100 : 0),
                               crGlyph, TfyMix(m_Pal.PanelBg, m_Pal.DimText, 70));
            }
            else
            {
                RECT rcMon = { rcRow.left + Sc(8), cyMid - Sc(8), rcRow.left + Sc(26), cyMid + Sc(4) };
                HBRUSH hbrMon = CreateSolidBrush(crGlyph);
                FrameRect(hdcMem, &rcMon, hbrMon);
                RECT rcInner = rcMon;
                InflateRect(&rcInner, -Sc(2), -Sc(2));
                FillRect(hdcMem, &rcInner, hbrMon);
                RECT rcStand = { rcRow.left + Sc(14), cyMid + Sc(4), rcRow.left + Sc(20), cyMid + Sc(7) };
                FillRect(hdcMem, &rcStand, hbrMon);
                DeleteObject(hbrMon);
            }

            RECT rcText = { rcRow.left + Sc(34), rcRow.top + Sc(4), rcRow.right - Sc(8), cyMid + Sc(4) };
            SelectObject(hdcMem, m_hFont);
            SetTextColor(hdcMem, m_Pal.PanelText);
            if (row.nType == 2)
            {
                rcText.top = rcRow.top;
                rcText.bottom = rcRow.bottom;
                RECT rcSsid = rcText;
                rcSsid.right -= Sc(76);
                DrawTextW(hdcMem, row.szName, -1, &rcSsid,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (row.szStatus[0])
                {
                    RECT rcTag = { rcText.right - Sc(72), rcRow.top, rcText.right, rcRow.bottom };
                    SelectObject(hdcMem, m_hFontSmall);
                    SetTextColor(hdcMem, row.bConnected ? m_Pal.HotBorder : m_Pal.DimText);
                    DrawTextW(hdcMem, row.szStatus, -1, &rcTag,
                              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
                }
            }
            else
            {
                DrawTextW(hdcMem, row.szName, -1, &rcText,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                RECT rcStatus = { rcText.left, cyMid + Sc(2), rcText.right, rcRow.bottom - Sc(2) };
                SelectObject(hdcMem, m_hFontSmall);
                SetTextColor(hdcMem, row.bConnected ? m_Pal.HotBorder : m_Pal.DimText);
                DrawTextW(hdcMem, row.szStatus, -1, &rcStatus,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }

        if (m_Rows.GetCount() == 0)
        {
            RECT rcNone = { Sc(12), Sc(40), rc.right - Sc(12), Sc(70) };
            SelectObject(hdcMem, m_hFontSmall);
            SetTextColor(hdcMem, m_Pal.DimText);
            DrawTextW(hdcMem, L"No networks found", -1, &rcNone,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        SelectObject(hdcMem, m_hFontSmall);
        SetTextColor(hdcMem, m_iHot == -2 ? m_Pal.HotBorder : m_Pal.DimText);
        DrawTextW(hdcMem, L"Open Network Connections", -1, &m_rcLink,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        SelectObject(hdcMem, hFontOld);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == TFY_TIMER_ANIM)
        {
            ULONGLONG now = GetTickCount64();
            if (m_AnimPhase == TFY_OPEN)
            {
                double t = (double)(now - m_AnimT0) / 160.0;
                double e = TfyEase(t);
                SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)(255.0 * e), LWA_ALPHA);
                SetWindowPos(NULL, m_ptFinal.x, m_ptFinal.y + (int)(Sc(10) * (1.0 - e)), 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (t >= 1.0)
                {
                    m_AnimPhase = TFY_NONE;
                    KillTimer(TFY_TIMER_ANIM);
                }
            }
            else if (m_AnimPhase == TFY_CLOSE)
            {
                double t = (double)(now - m_AnimT0) / 120.0;
                if (t >= 1.0)
                {
                    KillTimer(TFY_TIMER_ANIM);
                    DestroyWindow();
                    return 0;
                }
                SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)(255.0 * (1.0 - TfyEase(t))), LWA_ALPHA);
            }
            else
            {
                KillTimer(TFY_TIMER_ANIM);
            }
        }
        return 0;
    }

    int HitTest(POINT pt)
    {
        if (PtInRect(&m_rcLink, pt))
            return -2;
        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            if (PtInRect(&m_Rows[i].rc, pt))
                return (int)i;
        }
        return -1;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int iHot = HitTest(pt);
        if (iHot != m_iHot)
        {
            m_iHot = iHot;
            InvalidateRect(NULL, FALSE);
        }
        if (!m_bTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
            TrackMouseEvent(&tme);
            m_bTracking = TRUE;
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_bTracking = FALSE;
        if (m_iHot != -1)
        {
            m_iHot = -1;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int i = HitTest(pt);

        if (i == -2)
        {
            ShellExecuteW(NULL, NULL, L"ncpa.cpl", NULL, NULL, SW_SHOWNORMAL);
            FadeOut();
            return 0;
        }

        if (i < 0)
            return 0;

        TFYNETROW &row = m_Rows[i];
        if (row.nType == 2)
        {
            if (row.bHasProfile && !row.bConnected && m_hWlan && m_pfnConnect)
            {
                WLAN_CONNECTION_PARAMETERS params;
                ZeroMemory(&params, sizeof(params));
                params.wlanConnectionMode = wlan_connection_mode_profile;
                params.strProfile = row.szProfile;
                params.dot11BssType = dot11_BSS_type_any;
                if (m_pfnConnect(m_hWlan, &row.ifGuid, &params, NULL) == ERROR_SUCCESS)
                {
                    StringCchCopyW(row.szStatus, _countof(row.szStatus), L"Connecting...");
                    InvalidateRect(NULL, FALSE);
                }
            }
        }
        else
        {
            ShellExecuteW(NULL, NULL, L"ncpa.cpl", NULL, NULL, SW_SHOWNORMAL);
            FadeOut();
        }
        return 0;
    }

    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == WA_INACTIVE)
            FadeOut();
        return 0;
    }

    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == VK_ESCAPE)
            FadeOut();
        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        KillTimer(TFY_TIMER_ANIM);
        if (m_hWlan && m_pfnClose)
            m_pfnClose(m_hWlan, NULL);
        m_hWlan = NULL;
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall); m_hFontSmall = NULL; }
        if (m_hFontHeader) { DeleteObject(m_hFontHeader); m_hFontHeader = NULL; }
        return 0;
    }

    virtual void OnFinalMessage(HWND hWnd) override;

    BEGIN_MSG_MAP(CTrayNetworkWnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()
};

static CTrayNetworkWnd *g_pTrayNetwork = NULL;
static ULONGLONG g_NetDismissTick = 0;

void CTrayNetworkWnd::OnFinalMessage(HWND hWnd)
{
    if (g_pTrayNetwork == this)
        g_pTrayNetwork = NULL;
    delete this;
}

VOID CTrayNetworkWnd::FadeOut()
{
    if (m_AnimPhase == TFY_CLOSE)
        return;
    g_NetDismissTick = GetTickCount64();
    m_AnimPhase = TFY_CLOSE;
    m_AnimT0 = g_NetDismissTick;
    SetTimer(TFY_TIMER_ANIM, 16, NULL);
}

VOID TrayNetwork_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor)
{
    if (GetTickCount64() - g_NetDismissTick < 350)
        return;

    if (g_pTrayNetwork && g_pTrayNetwork->IsWindow())
    {
        if (g_pTrayNetwork->IsWindowVisible() &&
            g_pTrayNetwork->m_AnimPhase != TFY_CLOSE)
        {
            g_pTrayNetwork->FadeOut();
            return;
        }
        g_pTrayNetwork->DestroyWindow();
        g_pTrayNetwork = NULL;
    }

    CTrayNetworkWnd *pNetwork = new CTrayNetworkWnd();
    if (!pNetwork)
        return;

    if (!pNetwork->Create(hwndOwner, CWindow::rcDefault, NULL))
    {
        delete pNetwork;
        return;
    }

    g_pTrayNetwork = pNetwork;
    pNetwork->Toggle(hwndOwner, prcAnchor);
}

VOID TrayFlyoutsAux_Destroy(VOID)
{
    if (g_pTrayVolume && g_pTrayVolume->IsWindow())
        g_pTrayVolume->DestroyWindow();
    g_pTrayVolume = NULL;
    if (g_pTrayNetwork && g_pTrayNetwork->IsWindow())
        g_pTrayNetwork->DestroyWindow();
    g_pTrayNetwork = NULL;
}
