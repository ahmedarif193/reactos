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

static HICON
TfyFluentIcon(UINT nId, int cx)
{
    static struct { UINT nId; int cx; HICON hIcon; } s_Cache[48];

    for (UINT i = 0; i < _countof(s_Cache); i++)
    {
        if (s_Cache[i].nId == nId && s_Cache[i].cx == cx)
            return s_Cache[i].hIcon;
    }

    HICON hIcon = (HICON)LoadImageW(hExplorerInstance, MAKEINTRESOURCEW(nId),
                                    IMAGE_ICON, cx, cx, 0);
    for (UINT i = 0; i < _countof(s_Cache); i++)
    {
        if (!s_Cache[i].nId)
        {
            s_Cache[i].nId = nId;
            s_Cache[i].cx = cx;
            s_Cache[i].hIcon = hIcon;
            break;
        }
    }
    return hIcon;
}

static VOID
TfyDrawFluent(HDC hdc, const RECT *prc, UINT nId)
{
    int cx = prc->right - prc->left;
    int cy = prc->bottom - prc->top;
    int n = min(cx, cy);
    HICON hIcon = TfyFluentIcon(nId, n);

    if (hIcon)
        DrawIconEx(hdc, prc->left + (cx - n) / 2, prc->top + (cy - n) / 2,
                   hIcon, n, n, 0, NULL, DI_NORMAL);
}

struct TFYAA
{
    HDC hdc;
    HBITMAP hbm;
    HGDIOBJ hbmOld;
    PULONG pBits;
    RECT rc;
    int ss;
};

static HDC
TfyAABegin(TFYAA *pAA, HDC hdcRef, const RECT *prc, int ss, COLORREF crBg)
{
    BITMAPINFO bmi;
    int w = (prc->right - prc->left) * ss;
    int h = (prc->bottom - prc->top) * ss;

    ZeroMemory(pAA, sizeof(*pAA));
    pAA->rc = *prc;
    pAA->ss = ss;
    pAA->hdc = CreateCompatibleDC(hdcRef);
    if (!pAA->hdc)
        return NULL;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    pAA->hbm = CreateDIBSection(hdcRef, &bmi, DIB_RGB_COLORS, (PVOID *)&pAA->pBits, NULL, 0);
    if (!pAA->hbm)
    {
        DeleteDC(pAA->hdc);
        pAA->hdc = NULL;
        return NULL;
    }

    pAA->hbmOld = SelectObject(pAA->hdc, pAA->hbm);

    RECT rcFill = { 0, 0, w, h };
    HBRUSH hbr = CreateSolidBrush(crBg);
    FillRect(pAA->hdc, &rcFill, hbr);
    DeleteObject(hbr);
    return pAA->hdc;
}

static VOID
TfyAAEnd(TFYAA *pAA, HDC hdcTarget)
{
    BITMAPINFO bmi;
    int w = pAA->rc.right - pAA->rc.left;
    int h = pAA->rc.bottom - pAA->rc.top;
    int srcW = w * pAA->ss;
    ULONG count = pAA->ss * pAA->ss;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            ULONG red = 0, green = 0, blue = 0;

            for (int yy = 0; yy < pAA->ss; yy++)
            {
                PULONG pSrc = pAA->pBits + (y * pAA->ss + yy) * srcW + x * pAA->ss;
                for (int xx = 0; xx < pAA->ss; xx++)
                {
                    ULONG color = pSrc[xx];
                    blue += color & 0xFF;
                    green += (color >> 8) & 0xFF;
                    red += (color >> 16) & 0xFF;
                }
            }

            pAA->pBits[y * w + x] = (blue + count / 2) / count |
                                      ((green + count / 2) / count) << 8 |
                                      ((red + count / 2) / count) << 16;
        }
    }

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(hdcTarget, pAA->rc.left, pAA->rc.top, w, h, 0, 0, 0, h, pAA->pBits, &bmi, DIB_RGB_COLORS);

    SelectObject(pAA->hdc, pAA->hbmOld);
    DeleteObject(pAA->hbm);
    DeleteDC(pAA->hdc);
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
    int m_Level;
    int m_SelYear;
    int m_SelMonth;
    int m_SelDay;
    int m_iHotCell;
    int m_iHotNav;
    BOOL m_bTracking;
    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    SIZE m_size;
    RECT m_rcTime, m_rcDate, m_rcPrev, m_rcNext, m_rcHeader, m_rcGrid, m_rcLink;
    int m_FirstDayOfWeek;

    enum { NAV_NONE = 0, NAV_PREV, NAV_NEXT, NAV_HEADER, NAV_LINK, NAV_DATE };

    CTrayCalendarWnd() : m_hFontTime(NULL), m_hFontHeader(NULL), m_hFont(NULL), m_hFontSmall(NULL),
                         m_ViewYear(0), m_ViewMonth(0), m_Level(0),
                         m_SelYear(0), m_SelMonth(0), m_SelDay(0),
                         m_iHotCell(-1), m_iHotNav(NAV_NONE),
                         m_bTracking(FALSE), m_AnimPhase(TFY_NONE), m_AnimT0(0),
                         m_FirstDayOfWeek(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        ZeroMemory(&m_size, sizeof(m_size));
    }

    int Sc(int v) const { return ShellScaleForDpi(v); }
    int CellW() const { return Sc(40); }
    int CellH() const { return Sc(34); }
    int BigCellW() const { return CellW() * 7 / 4; }
    int BigCellH() const { return CellH() * 6 / 4; }

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

    static VOID AddMonths(int &year, int &month, int delta)
    {
        int idx = year * 12 + (month - 1) + delta;
        if (idx < 1601 * 12)
            idx = 1601 * 12;
        if (idx > 9999 * 12 + 11)
            idx = 9999 * 12 + 11;
        year = idx / 12;
        month = idx % 12 + 1;
    }

    int DecadeStart() const
    {
        return (m_ViewYear / 10) * 10;
    }

    VOID Layout()
    {
        int pad = Sc(16);
        m_size.cx = pad * 2 + CellW() * 7;

        int y = pad;
        SetRect(&m_rcTime, pad, y, m_size.cx - pad, y + Sc(44));
        y += Sc(44);
        SetRect(&m_rcDate, pad, y, m_size.cx - pad, y + Sc(24));
        y += Sc(24);
        y += Sc(14);
        int headerY = y;
        SetRect(&m_rcNext, m_size.cx - pad - Sc(28), headerY, m_size.cx - pad, headerY + Sc(28));
        SetRect(&m_rcPrev, m_rcNext.left - Sc(32), headerY, m_rcNext.left - Sc(4), headerY + Sc(28));
        SetRect(&m_rcHeader, pad, headerY, m_rcPrev.left - Sc(8), headerY + Sc(28));
        y += Sc(28);
        y += Sc(24);
        SetRect(&m_rcGrid, pad, y, pad + CellW() * 7, y + CellH() * 6);
        y += CellH() * 6 + Sc(10);
        RECT rcSep = { pad, y, m_size.cx - pad, y + 1 };
        y += Sc(10);
        SetRect(&m_rcLink, pad, y, m_size.cx - pad, y + Sc(22));
        y += Sc(22) + Sc(12);
        m_size.cy = y;
        (void)rcSep;
    }

    VOID GoToday()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        m_ViewYear = st.wYear;
        m_ViewMonth = st.wMonth;
        m_SelYear = st.wYear;
        m_SelMonth = st.wMonth;
        m_SelDay = st.wDay;
        m_Level = 0;
    }

    VOID Toggle(HWND hwndOwner, const RECT *prcAnchor)
    {
        MONITORINFO mi;
        POINT ptRef;
        int xPos, yPos;

        StartMenu2_GetFlyoutPalette(&m_Pal);

        if (!m_hFontTime) m_hFontTime = TfyCreateFont(32, FW_LIGHT);
        if (!m_hFontHeader) m_hFontHeader = TfyCreateFont(14, FW_SEMIBOLD);
        if (!m_hFont) m_hFont = TfyCreateFont(12, FW_NORMAL);
        if (!m_hFontSmall) m_hFontSmall = TfyCreateFont(11, FW_NORMAL);

        GoToday();
        m_iHotCell = -1;
        m_iHotNav = NAV_NONE;

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

    RECT CellRect(int cell) const
    {
        RECT rc;
        if (m_Level == 0)
        {
            int col = cell % 7, row = cell / 7;
            SetRect(&rc, m_rcGrid.left + col * CellW(), m_rcGrid.top + row * CellH(),
                    m_rcGrid.left + (col + 1) * CellW(), m_rcGrid.top + (row + 1) * CellH());
        }
        else
        {
            int col = cell % 4, row = cell / 4;
            SetRect(&rc, m_rcGrid.left + col * BigCellW(), m_rcGrid.top + row * BigCellH(),
                    m_rcGrid.left + (col + 1) * BigCellW(), m_rcGrid.top + (row + 1) * BigCellH());
        }
        return rc;
    }

    int CellCount() const { return m_Level == 0 ? 42 : 16; }

    VOID CellDate(int cell, int &year, int &month, int &day, BOOL &bDim) const
    {
        bDim = FALSE;
        if (m_Level == 0)
        {
            int firstDow = DayOfWeek(m_ViewYear, m_ViewMonth, 1);
            int lead = (firstDow - m_FirstDayOfWeek + 7) % 7;
            if (lead == 0)
                lead = 7;
            int d = cell - lead + 1;
            year = m_ViewYear;
            month = m_ViewMonth;
            if (d < 1)
            {
                AddMonths(year, month, -1);
                d += DaysInMonth(year, month);
                bDim = TRUE;
            }
            else if (d > DaysInMonth(year, month))
            {
                d -= DaysInMonth(year, month);
                AddMonths(year, month, 1);
                bDim = TRUE;
            }
            day = d;
        }
        else if (m_Level == 1)
        {
            year = m_ViewYear;
            month = cell + 1;
            day = 1;
            if (month > 12)
            {
                month -= 12;
                year++;
                bDim = TRUE;
            }
        }
        else
        {
            year = DecadeStart() - 1 + cell;
            month = 1;
            day = 1;
            bDim = (year < DecadeStart() || year > DecadeStart() + 9);
        }
    }

    VOID DrawCell(HDC hdc, const RECT *prc, LPCWSTR pszText, BOOL bToday, BOOL bSel, BOOL bHot, BOOL bDim)
    {
        RECT rcFill = *prc;
        InflateRect(&rcFill, -Sc(2), -Sc(2));
        if (bToday)
        {
            HBRUSH hbr = CreateSolidBrush(m_Pal.AccentBg);
            FillRect(hdc, &rcFill, hbr);
            DeleteObject(hbr);
        }
        else if (bHot)
        {
            HBRUSH hbr = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
            FillRect(hdc, &rcFill, hbr);
            DeleteObject(hbr);
        }
        if (bSel)
        {
            HBRUSH hbr = CreateSolidBrush(bToday ? m_Pal.AccentText : m_Pal.AccentBg);
            FrameRect(hdc, &rcFill, hbr);
            RECT rcInner = rcFill;
            InflateRect(&rcInner, -1, -1);
            FrameRect(hdc, &rcInner, hbr);
            DeleteObject(hbr);
        }
        SetTextColor(hdc, bToday ? m_Pal.AccentText : (bDim ? m_Pal.DimText : m_Pal.PanelText));
        DrawTextW(hdc, pszText, -1, (LPRECT)prc, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
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

        SYSTEMTIME stNow;
        GetLocalTime(&stNow);

        int pad = Sc(16);
        WCHAR szBuf[128];

        GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &stNow, NULL, szBuf, _countof(szBuf));
        HGDIOBJ hFontOld = SelectObject(hdcMem, m_hFontTime);
        SetTextColor(hdcMem, m_Pal.PanelText);
        DrawTextW(hdcMem, szBuf, -1, &m_rcTime, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &stNow, NULL, szBuf, _countof(szBuf));
        SelectObject(hdcMem, m_hFont);
        SetTextColor(hdcMem, m_iHotNav == NAV_DATE ? m_Pal.HotBorder : m_Pal.AccentBg);
        DrawTextW(hdcMem, szBuf, -1, &m_rcDate, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        RECT rcSep = { pad, m_rcDate.bottom + Sc(6), rc.right - pad, m_rcDate.bottom + Sc(7) };
        HBRUSH hbrSep = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 90));
        FillRect(hdcMem, &rcSep, hbrSep);

        if (m_Level == 0)
        {
            SYSTEMTIME stView;
            ZeroMemory(&stView, sizeof(stView));
            stView.wYear = (WORD)m_ViewYear;
            stView.wMonth = (WORD)m_ViewMonth;
            stView.wDay = 1;
            GetDateFormatW(LOCALE_USER_DEFAULT, 0, &stView, L"MMMM yyyy", szBuf, _countof(szBuf));
        }
        else if (m_Level == 1)
            StringCchPrintfW(szBuf, _countof(szBuf), L"%d", m_ViewYear);
        else
            StringCchPrintfW(szBuf, _countof(szBuf), L"%d - %d", DecadeStart(), DecadeStart() + 9);

        if (m_iHotNav == NAV_HEADER && m_Level < 2)
        {
            HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
            FillRect(hdcMem, &m_rcHeader, hbrHot);
            DeleteObject(hbrHot);
        }
        RECT rcHeaderText = m_rcHeader;
        rcHeaderText.left += Sc(4);
        SelectObject(hdcMem, m_hFontHeader);
        SetTextColor(hdcMem, m_Pal.PanelText);
        DrawTextW(hdcMem, szBuf, -1, &rcHeaderText, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        for (int nav = NAV_PREV; nav <= NAV_NEXT; nav++)
        {
            const RECT *prcNav = (nav == NAV_PREV) ? &m_rcPrev : &m_rcNext;
            if (m_iHotNav == nav)
            {
                HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
                FillRect(hdcMem, prcNav, hbrHot);
                DeleteObject(hbrHot);
            }
            int cxm = (prcNav->left + prcNav->right) / 2;
            int cym = (prcNav->top + prcNav->bottom) / 2;
            RECT rcIcon = { cxm - Sc(8), cym - Sc(8), cxm + Sc(8), cym + Sc(8) };
            TfyDrawFluent(hdcMem, &rcIcon, nav == NAV_PREV ? IDI_FLU_CHEVUP : IDI_FLU_CHEVDOWN);
        }

        if (m_Level == 0)
        {
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
                RECT rcDay = { m_rcGrid.left + i * CellW(), m_rcGrid.top - Sc(24),
                               m_rcGrid.left + (i + 1) * CellW(), m_rcGrid.top };
                DrawTextW(hdcMem, szBuf, -1, &rcDay, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            }
        }

        SelectObject(hdcMem, m_hFont);
        for (int cell = 0; cell < CellCount(); cell++)
        {
            int year, month, day;
            BOOL bDim;
            CellDate(cell, year, month, day, bDim);
            RECT rcCell = CellRect(cell);
            BOOL bToday, bSel;
            if (m_Level == 0)
            {
                bToday = (year == stNow.wYear && month == stNow.wMonth && day == stNow.wDay);
                bSel = (year == m_SelYear && month == m_SelMonth && day == m_SelDay);
                StringCchPrintfW(szBuf, _countof(szBuf), L"%d", day);
            }
            else if (m_Level == 1)
            {
                SYSTEMTIME stCell;
                ZeroMemory(&stCell, sizeof(stCell));
                stCell.wYear = (WORD)year;
                stCell.wMonth = (WORD)month;
                stCell.wDay = 1;
                bToday = (year == stNow.wYear && month == stNow.wMonth);
                bSel = (year == m_SelYear && month == m_SelMonth);
                if (!GetDateFormatW(LOCALE_USER_DEFAULT, 0, &stCell, L"MMM", szBuf, _countof(szBuf)))
                    StringCchPrintfW(szBuf, _countof(szBuf), L"%d", month);
            }
            else
            {
                bToday = (year == stNow.wYear);
                bSel = (year == m_SelYear);
                StringCchPrintfW(szBuf, _countof(szBuf), L"%d", year);
            }
            DrawCell(hdcMem, &rcCell, szBuf, bToday, bSel, cell == m_iHotCell, bDim);
        }

        RECT rcSep2 = { pad, m_rcGrid.bottom + Sc(10), rc.right - pad, m_rcGrid.bottom + Sc(11) };
        FillRect(hdcMem, &rcSep2, hbrSep);
        DeleteObject(hbrSep);

        SelectObject(hdcMem, m_hFont);
        SetTextColor(hdcMem, m_iHotNav == NAV_LINK ? m_Pal.HotBorder : m_Pal.AccentBg);
        DrawTextW(hdcMem, L"Date and time settings", -1, &m_rcLink,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

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
            InvalidateRect(&m_rcTime, FALSE);
        }
        return 0;
    }

    int CellHitTest(POINT pt) const
    {
        if (!PtInRect(&m_rcGrid, pt))
            return -1;
        int cw = m_Level == 0 ? CellW() : BigCellW();
        int ch = m_Level == 0 ? CellH() : BigCellH();
        int cols = m_Level == 0 ? 7 : 4;
        int rows = m_Level == 0 ? 6 : 4;
        int col = (pt.x - m_rcGrid.left) / cw;
        int row = (pt.y - m_rcGrid.top) / ch;
        if (col < 0 || col >= cols || row < 0 || row >= rows)
            return -1;
        return row * cols + col;
    }

    int NavHitTest(POINT pt) const
    {
        if (PtInRect(&m_rcPrev, pt)) return NAV_PREV;
        if (PtInRect(&m_rcNext, pt)) return NAV_NEXT;
        if (PtInRect(&m_rcHeader, pt) && m_Level < 2) return NAV_HEADER;
        if (PtInRect(&m_rcLink, pt)) return NAV_LINK;
        if (PtInRect(&m_rcDate, pt)) return NAV_DATE;
        return NAV_NONE;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int iCell = CellHitTest(pt);
        int iNav = NavHitTest(pt);
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
        if (m_iHotCell != -1 || m_iHotNav != NAV_NONE)
        {
            m_iHotCell = -1;
            m_iHotNav = NAV_NONE;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    VOID Navigate(int dir)
    {
        if (m_Level == 0)
            AddMonths(m_ViewYear, m_ViewMonth, dir);
        else if (m_Level == 1)
            m_ViewYear += dir;
        else
            m_ViewYear += dir * 10;
        if (m_ViewYear < 1601) m_ViewYear = 1601;
        if (m_ViewYear > 9999) m_ViewYear = 9999;
        InvalidateRect(NULL, FALSE);
    }

    VOID SelectCell(int cell)
    {
        int year, month, day;
        BOOL bDim;
        if (cell < 0 || cell >= CellCount())
            return;
        CellDate(cell, year, month, day, bDim);
        if (m_Level == 0)
        {
            m_SelYear = year;
            m_SelMonth = month;
            m_SelDay = day;
            m_ViewYear = year;
            m_ViewMonth = month;
        }
        else if (m_Level == 1)
        {
            m_ViewYear = year;
            m_ViewMonth = month;
            m_Level = 0;
        }
        else
        {
            m_ViewYear = year;
            m_Level = 1;
        }
        m_iHotCell = -1;
        InvalidateRect(NULL, FALSE);
    }

    VOID MoveSelection(int deltaDays)
    {
        SYSTEMTIME st;
        FILETIME ft;
        ULARGE_INTEGER ul;
        ZeroMemory(&st, sizeof(st));
        st.wYear = (WORD)m_SelYear;
        st.wMonth = (WORD)m_SelMonth;
        st.wDay = (WORD)m_SelDay;
        if (!SystemTimeToFileTime(&st, &ft))
            return;
        ul.LowPart = ft.dwLowDateTime;
        ul.HighPart = ft.dwHighDateTime;
        ul.QuadPart += (LONGLONG)deltaDays * 864000000000LL;
        ft.dwLowDateTime = ul.LowPart;
        ft.dwHighDateTime = ul.HighPart;
        if (!FileTimeToSystemTime(&ft, &st))
            return;
        m_SelYear = st.wYear;
        m_SelMonth = st.wMonth;
        m_SelDay = st.wDay;
        m_ViewYear = st.wYear;
        m_ViewMonth = st.wMonth;
        m_Level = 0;
        InvalidateRect(NULL, FALSE);
    }

    VOID OpenSettings()
    {
        ShellExecuteW(NULL, L"open", L"control.exe", L"timedate.cpl", NULL, SW_SHOWNORMAL);
        FadeOut();
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int nav = NavHitTest(pt);
        if (nav == NAV_PREV)
            Navigate(-1);
        else if (nav == NAV_NEXT)
            Navigate(1);
        else if (nav == NAV_HEADER)
        {
            m_Level++;
            m_iHotCell = -1;
            InvalidateRect(NULL, FALSE);
        }
        else if (nav == NAV_LINK)
            OpenSettings();
        else if (nav == NAV_DATE)
        {
            GoToday();
            InvalidateRect(NULL, FALSE);
        }
        else
            SelectCell(CellHitTest(pt));
        return 0;
    }

    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        Navigate(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
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
        switch (wParam)
        {
            case VK_ESCAPE:
                if (m_Level > 0)
                {
                    m_Level--;
                    InvalidateRect(NULL, FALSE);
                }
                else
                    FadeOut();
                break;
            case VK_PRIOR: Navigate(-1); break;
            case VK_NEXT: Navigate(1); break;
            case VK_HOME: GoToday(); InvalidateRect(NULL, FALSE); break;
            case VK_LEFT: if (m_Level == 0) MoveSelection(-1); else Navigate(-1); break;
            case VK_RIGHT: if (m_Level == 0) MoveSelection(1); else Navigate(1); break;
            case VK_UP: if (m_Level == 0) MoveSelection(-7); else Navigate(-1); break;
            case VK_DOWN: if (m_Level == 0) MoveSelection(7); else Navigate(1); break;
            case VK_RETURN:
                if (m_Level > 0)
                {
                    m_Level--;
                    InvalidateRect(NULL, FALSE);
                }
                break;
            case VK_BACK:
                if (m_Level < 2)
                {
                    m_Level++;
                    InvalidateRect(NULL, FALSE);
                }
                break;
        }
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
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
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
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>

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

struct TFYMIXROW
{
    ISimpleAudioVolume *pVolume;
    IAudioSessionControl2 *pControl;
    WCHAR szName[96];
    DWORD dwPid;
    int nPercent;
    BOOL bMute;
    RECT rcRow;
    RECT rcSlider;
    RECT rcMute;
};

static const PROPERTYKEY TFY_PKEY_Device_FriendlyName =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

struct TFYAUDIODEV
{
    WCHAR szId[256];
    WCHAR szName[128];
    BOOL bDefault;
};

static VOID
TfyGetDeviceFriendlyName(IMMDevice *pDevice, LPWSTR pszName, SIZE_T cchName)
{
    CComPtr<IPropertyStore> pProps;
    StringCchCopyW(pszName, cchName, L"Speakers");
    if (!pDevice || FAILED(pDevice->OpenPropertyStore(STGM_READ, &pProps)))
        return;
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(pProps->GetValue(TFY_PKEY_Device_FriendlyName, &var)) &&
        var.vt == VT_LPWSTR && var.pwszVal && var.pwszVal[0])
    {
        StringCchCopyW(pszName, cchName, var.pwszVal);
    }
    PropVariantClear(&var);
}

static VOID
TfyEnumRenderDevices(IMMDeviceEnumerator *pEnum, CAtlArray<TFYAUDIODEV> &Devices)
{
    CComPtr<IMMDeviceCollection> pCollection;
    CComPtr<IMMDevice> pDefault;
    LPWSTR pszDefaultId = NULL;
    UINT nCount = 0;
    Devices.SetCount(0);
    if (!pEnum)
        return;
    if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDefault)))
        pDefault->GetId(&pszDefaultId);
    if (SUCCEEDED(pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection)) &&
        SUCCEEDED(pCollection->GetCount(&nCount)))
    {
        for (UINT i = 0; i < nCount && Devices.GetCount() < 16; i++)
        {
            CComPtr<IMMDevice> pDevice;
            LPWSTR pszId = NULL;
            TFYAUDIODEV dev;
            ZeroMemory(&dev, sizeof(dev));
            if (FAILED(pCollection->Item(i, &pDevice)) || FAILED(pDevice->GetId(&pszId)) || !pszId)
                continue;
            StringCchCopyW(dev.szId, _countof(dev.szId), pszId);
            dev.bDefault = (pszDefaultId && !wcscmp(pszDefaultId, pszId));
            CoTaskMemFree(pszId);
            TfyGetDeviceFriendlyName(pDevice, dev.szName, _countof(dev.szName));
            Devices.Add(dev);
        }
    }
    if (pszDefaultId)
        CoTaskMemFree(pszDefaultId);
}

static BOOL
TfySetDefaultRenderDevice(LPCWSTR pszId)
{
    HKEY hKey;
    BOOL bRet = FALSE;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Wine\\Drivers\\wdmaud.drv", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        bRet = RegSetValueExW(hKey, L"DefaultOutput", 0, REG_SZ, (const BYTE *)pszId,
                              (DWORD)((wcslen(pszId) + 1) * sizeof(WCHAR))) == ERROR_SUCCESS;
        RegCloseKey(hKey);
    }
    return bRet;
}

VOID TrayMixer_Open(const RECT *prcAnchor);

class CTrayVolumeWnd :
    public CWindowImpl<CTrayVolumeWnd, CWindow, CTrayFlyoutTraits>
{
public:
    DECLARE_WND_CLASS_EX(L"TrayVolumeFlyout", CS_DROPSHADOW, COLOR_WINDOW)

    SM2_FLYOUT_PALETTE m_Pal;
    TFYVOLUME m_Fallback;
    CComPtr<IMMDeviceEnumerator> m_pEnum;
    CComPtr<IMMDevice> m_pDevice;
    CComPtr<IAudioEndpointVolume> m_pEndpointVolume;
    CComPtr<IAudioSessionManager2> m_pSessionMgr;
    CAtlArray<TFYMIXROW> m_Rows;
    BOOL m_bComVolume;
    HFONT m_hFont;
    HFONT m_hFontSmall;
    WCHAR m_szDevice[96];
    int m_nMaster;
    BOOL m_bMasterMute;
    int m_iDragRow;
    int m_iHotRow;
    BOOL m_bHotMute;
    BOOL m_bHotMixer;
    BOOL m_bTracking;
    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    SIZE m_size;
    RECT m_rcMasterRow, m_rcMasterSlider, m_rcMasterMute, m_rcMixerLink;

    CTrayVolumeWnd() : m_bComVolume(FALSE), m_hFont(NULL), m_hFontSmall(NULL),
                       m_nMaster(0), m_bMasterMute(FALSE),
                       m_iDragRow(-2), m_iHotRow(-2), m_bHotMute(FALSE), m_bHotMixer(FALSE),
                       m_bTracking(FALSE), m_AnimPhase(TFY_NONE), m_AnimT0(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        ZeroMemory(&m_size, sizeof(m_size));
        m_szDevice[0] = 0;
    }

    int Sc(int v) const { return ShellScaleForDpi(v); }

    VOID FreeRows()
    {
        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            if (m_Rows[i].pVolume)
                m_Rows[i].pVolume->Release();
            if (m_Rows[i].pControl)
                m_Rows[i].pControl->Release();
        }
        m_Rows.SetCount(0);
    }

    VOID ResolveSessionName(TFYMIXROW *pRow)
    {
        if (pRow->szName[0])
            return;

        if (pRow->dwPid)
        {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pRow->dwPid);
            if (!hProcess)
                hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pRow->dwPid);
            if (hProcess)
            {
                WCHAR szPath[MAX_PATH];
                DWORD cch = _countof(szPath);
                if (QueryFullProcessImageNameW(hProcess, 0, szPath, &cch))
                {
                    LPCWSTR pszFile = PathFindFileNameW(szPath);
                    StringCchCopyW(pRow->szName, _countof(pRow->szName), pszFile);
                    PathRemoveExtensionW(pRow->szName);
                }
                CloseHandle(hProcess);
            }
        }

        if (!pRow->szName[0])
            StringCchCopyW(pRow->szName, _countof(pRow->szName), L"System Sounds");
    }

    VOID BuildAudio()
    {
        CComPtr<IAudioSessionEnumerator> pSessions;
        int nCount = 0;
        FreeRows();
        m_pSessionMgr.Release();
        m_pEndpointVolume.Release();
        m_pDevice.Release();
        m_pEnum.Release();
        m_bComVolume = FALSE;
        StringCchCopyW(m_szDevice, _countof(m_szDevice), L"Speakers");

        if (SUCCEEDED(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARG(IMMDeviceEnumerator, &m_pEnum))) &&
            SUCCEEDED(m_pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &m_pDevice)))
        {
            TfyGetDeviceFriendlyName(m_pDevice, m_szDevice, _countof(m_szDevice));
            if (SUCCEEDED(m_pDevice->Activate(IID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER,
                                              NULL, (void **)&m_pEndpointVolume)))
            {
                float fLevel = 0.0f;
                BOOL bMute = FALSE;
                if (SUCCEEDED(m_pEndpointVolume->GetMasterVolumeLevelScalar(&fLevel)))
                {
                    m_nMaster = (int)(fLevel * 100.0f + 0.5f);
                    m_pEndpointVolume->GetMute(&bMute);
                    m_bMasterMute = bMute;
                    m_bComVolume = TRUE;
                }
            }

            if (SUCCEEDED(m_pDevice->Activate(IID_IAudioSessionManager2, CLSCTX_INPROC_SERVER,
                                              NULL, (void **)&m_pSessionMgr)) &&
                SUCCEEDED(m_pSessionMgr->GetSessionEnumerator(&pSessions)) &&
                SUCCEEDED(pSessions->GetCount(&nCount)))
            {
                for (int i = 0; i < nCount && m_Rows.GetCount() < 8; i++)
                {
                    CComPtr<IAudioSessionControl> pControl;
                    TFYMIXROW row;
                    AudioSessionState state;
                    LPWSTR pszName = NULL;

                    ZeroMemory(&row, sizeof(row));

                    if (FAILED(pSessions->GetSession(i, &pControl)))
                        continue;
                    if (FAILED(pControl->QueryInterface(IID_PPV_ARG(IAudioSessionControl2, &row.pControl))))
                        continue;
                    if (SUCCEEDED(row.pControl->GetState(&state)) &&
                        state == AudioSessionStateExpired)
                    {
                        row.pControl->Release();
                        continue;
                    }
                    if (FAILED(pControl->QueryInterface(IID_PPV_ARG(ISimpleAudioVolume, &row.pVolume))))
                    {
                        row.pControl->Release();
                        continue;
                    }

                    row.pControl->GetProcessId(&row.dwPid);
                    if (SUCCEEDED(row.pControl->GetDisplayName(&pszName)) && pszName)
                    {
                        StringCchCopyW(row.szName, _countof(row.szName), pszName);
                        CoTaskMemFree(pszName);
                    }
                    ResolveSessionName(&row);

                    float fLevel = 1.0f;
                    BOOL bMute = FALSE;
                    row.pVolume->GetMasterVolume(&fLevel);
                    row.pVolume->GetMute(&bMute);
                    row.nPercent = (int)(fLevel * 100.0f + 0.5f);
                    row.bMute = bMute;

                    m_Rows.Add(row);
                }
            }
        }

        if (!m_bComVolume)
        {
            m_Fallback.Open();
            m_nMaster = m_Fallback.GetVolume();
            m_bMasterMute = m_Fallback.GetMute();
        }
    }

    VOID LayoutSliderRow(const RECT *prcRow, RECT *prcMute, RECT *prcSlider)
    {
        SetRect(prcMute, prcRow->left + Sc(4), (prcRow->top + prcRow->bottom) / 2 - Sc(13),
                prcRow->left + Sc(30), (prcRow->top + prcRow->bottom) / 2 + Sc(13));
        SetRect(prcSlider, prcMute->right + Sc(8),
                (prcRow->top + prcRow->bottom) / 2 - Sc(2),
                prcRow->right - Sc(40), (prcRow->top + prcRow->bottom) / 2 + Sc(2));
    }

    VOID Layout()
    {
        m_size.cx = Sc(320);
        int y = Sc(10);

        y += Sc(20);
        SetRect(&m_rcMasterRow, Sc(8), y, m_size.cx - Sc(8), y + Sc(40));
        LayoutSliderRow(&m_rcMasterRow, &m_rcMasterMute, &m_rcMasterSlider);
        y += Sc(40) + Sc(6);

        if (m_Rows.GetCount() > 0)
        {
            y += Sc(9);
            for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
            {
                TFYMIXROW &row = m_Rows[i];
                SetRect(&row.rcRow, Sc(8), y + Sc(16), m_size.cx - Sc(8), y + Sc(16) + Sc(36));
                LayoutSliderRow(&row.rcRow, &row.rcMute, &row.rcSlider);
                y += Sc(54);
            }
        }

        y += Sc(6);
        SetRect(&m_rcMixerLink, Sc(12), y, m_size.cx - Sc(12), y + Sc(22));
        y += Sc(22) + Sc(10);
        m_size.cy = y;
    }

    VOID Toggle(HWND hwndOwner, const RECT *prcAnchor)
    {
        MONITORINFO mi;
        POINT ptRef;
        int xPos, yPos;

        StartMenu2_GetFlyoutPalette(&m_Pal);

        if (!m_hFont) m_hFont = TfyCreateFont(12, FW_NORMAL);
        if (!m_hFontSmall) m_hFontSmall = TfyCreateFont(11, FW_NORMAL);

        BuildAudio();
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

    int SliderPercentFromX(const RECT *prcSlider, int x)
    {
        int w = prcSlider->right - prcSlider->left;
        if (w <= 0)
            return 0;
        int pct = MulDiv(x - prcSlider->left, 100, w);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        return pct;
    }

    VOID ApplyMaster(int nPercent)
    {
        m_nMaster = nPercent;
        if (m_bComVolume && m_pEndpointVolume)
        {
            m_pEndpointVolume->SetMasterVolumeLevelScalar((float)nPercent / 100.0f, NULL);
            if (m_bMasterMute && nPercent > 0)
            {
                m_bMasterMute = FALSE;
                m_pEndpointVolume->SetMute(FALSE, NULL);
            }
        }
        else
        {
            m_Fallback.SetVolume(nPercent);
            if (m_bMasterMute && nPercent > 0)
            {
                m_bMasterMute = FALSE;
                m_Fallback.SetMute(FALSE);
            }
        }
        InvalidateRect(NULL, FALSE);
    }

    VOID ApplySession(int iRow, int nPercent)
    {
        if (iRow < 0 || iRow >= (int)m_Rows.GetCount())
            return;
        TFYMIXROW &row = m_Rows[iRow];
        row.nPercent = nPercent;
        if (row.pVolume)
        {
            row.pVolume->SetMasterVolume((float)nPercent / 100.0f, NULL);
            if (row.bMute && nPercent > 0)
            {
                row.bMute = FALSE;
                row.pVolume->SetMute(FALSE, NULL);
            }
        }
        InvalidateRect(NULL, FALSE);
    }

    VOID DrawSpeakerGlyph(HDC hdc, const RECT *prc, BOOL bMute, BOOL bHot)
    {
        if (bHot)
        {
            HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
            FillRect(hdc, prc, hbrHot);
            DeleteObject(hbrHot);
        }

        int cxm = (prc->left + prc->right) / 2;
        int cym = (prc->top + prc->bottom) / 2;
        RECT rcIcon = { cxm - Sc(10), cym - Sc(10), cxm + Sc(10), cym + Sc(10) };
        TfyDrawFluent(hdc, &rcIcon, bMute ? IDI_FLU_SPKMUTE : IDI_FLU_SPK2);
    }

    VOID DrawSlider(HDC hdc, const RECT *prcSlider, int nPercent, BOOL bDragging)
    {
        HBRUSH hbrTrack = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 110));
        FillRect(hdc, prcSlider, hbrTrack);
        DeleteObject(hbrTrack);

        int xFill = prcSlider->left + MulDiv(nPercent, prcSlider->right - prcSlider->left, 100);
        RECT rcFill = { prcSlider->left, prcSlider->top, xFill, prcSlider->bottom };
        HBRUSH hbrFill = CreateSolidBrush(m_Pal.AccentBg);
        FillRect(hdc, &rcFill, hbrFill);
        DeleteObject(hbrFill);

        RECT rcThumb = { xFill - Sc(4), (prcSlider->top + prcSlider->bottom) / 2 - Sc(9),
                         xFill + Sc(4), (prcSlider->top + prcSlider->bottom) / 2 + Sc(9) };
        HBRUSH hbrThumb = CreateSolidBrush(bDragging ? m_Pal.AccentBg : m_Pal.PanelText);
        FillRect(hdc, &rcThumb, hbrThumb);
        DeleteObject(hbrThumb);
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
        HGDIOBJ hFontOld = SelectObject(hdcMem, m_hFontSmall);

        WCHAR szBuf[128];
        RECT rcDevice = { Sc(12), Sc(8), rc.right - Sc(12), Sc(8) + Sc(18) };
        SetTextColor(hdcMem, m_Pal.DimText);
        StringCchPrintfW(szBuf, _countof(szBuf), L"%s \x2014 %d", m_szDevice, m_nMaster);
        DrawTextW(hdcMem, szBuf, -1, &rcDevice,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        DrawSpeakerGlyph(hdcMem, &m_rcMasterMute, m_bMasterMute,
                         m_iHotRow == -1 && m_bHotMute);
        DrawSlider(hdcMem, &m_rcMasterSlider, m_nMaster, m_iDragRow == -1);

        SelectObject(hdcMem, m_hFont);
        SetTextColor(hdcMem, m_Pal.PanelText);
        RECT rcMasterVal = { m_rcMasterSlider.right + Sc(6), m_rcMasterRow.top,
                             m_rcMasterRow.right, m_rcMasterRow.bottom };
        StringCchPrintfW(szBuf, _countof(szBuf), L"%d", m_nMaster);
        DrawTextW(hdcMem, szBuf, -1, &rcMasterVal,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

        if (m_Rows.GetCount() > 0)
        {
            RECT rcSep = { Sc(12), m_rcMasterRow.bottom + Sc(8),
                           rc.right - Sc(12), m_rcMasterRow.bottom + Sc(9) };
            HBRUSH hbrSep = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 90));
            FillRect(hdcMem, &rcSep, hbrSep);
            DeleteObject(hbrSep);
        }

        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            TFYMIXROW &row = m_Rows[i];

            RECT rcName = { row.rcRow.left + Sc(4), row.rcRow.top - Sc(16),
                            row.rcRow.right - Sc(4), row.rcRow.top };
            SelectObject(hdcMem, m_hFontSmall);
            SetTextColor(hdcMem, m_Pal.PanelText);
            DrawTextW(hdcMem, row.szName, -1, &rcName,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

            DrawSpeakerGlyph(hdcMem, &row.rcMute, row.bMute,
                             m_iHotRow == (int)i && m_bHotMute);
            DrawSlider(hdcMem, &row.rcSlider, row.nPercent, m_iDragRow == (int)i);

            SelectObject(hdcMem, m_hFont);
            SetTextColor(hdcMem, m_Pal.PanelText);
            RECT rcVal = { row.rcSlider.right + Sc(6), row.rcRow.top,
                           row.rcRow.right, row.rcRow.bottom };
            StringCchPrintfW(szBuf, _countof(szBuf), L"%d", row.nPercent);
            DrawTextW(hdcMem, szBuf, -1, &rcVal,
                      DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        }

        SelectObject(hdcMem, m_hFontSmall);
        SetTextColor(hdcMem, m_bHotMixer ? m_Pal.HotBorder : m_Pal.DimText);
        DrawTextW(hdcMem, L"Open volume mixer", -1, &m_rcMixerLink,
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

    int RowHitTest(POINT pt, BOOL *pbMute, const RECT **pprcSlider)
    {
        RECT rcMasterHit = m_rcMasterRow;
        InflateRect(&rcMasterHit, 0, Sc(4));
        *pbMute = FALSE;
        *pprcSlider = NULL;

        if (PtInRect(&m_rcMasterMute, pt))
        {
            *pbMute = TRUE;
            return -1;
        }
        if (PtInRect(&rcMasterHit, pt))
        {
            *pprcSlider = &m_rcMasterSlider;
            return -1;
        }

        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            RECT rcRowHit = m_Rows[i].rcRow;
            InflateRect(&rcRowHit, 0, Sc(4));
            if (PtInRect(&m_Rows[i].rcMute, pt))
            {
                *pbMute = TRUE;
                return (int)i;
            }
            if (PtInRect(&rcRowHit, pt))
            {
                *pprcSlider = &m_Rows[i].rcSlider;
                return (int)i;
            }
        }
        return -2;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (m_iDragRow != -2)
        {
            const RECT *prcSlider = (m_iDragRow == -1) ? &m_rcMasterSlider
                                    : &m_Rows[m_iDragRow].rcSlider;
            int pct = SliderPercentFromX(prcSlider, pt.x);
            if (m_iDragRow == -1)
                ApplyMaster(pct);
            else
                ApplySession(m_iDragRow, pct);
            return 0;
        }

        BOOL bMute;
        const RECT *prcSlider;
        int iRow = RowHitTest(pt, &bMute, &prcSlider);
        BOOL bMixer = PtInRect(&m_rcMixerLink, pt);

        if (iRow != m_iHotRow || bMute != m_bHotMute || bMixer != m_bHotMixer)
        {
            m_iHotRow = iRow;
            m_bHotMute = bMute;
            m_bHotMixer = bMixer;
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
        if (m_iHotRow != -2 || m_bHotMixer)
        {
            m_iHotRow = -2;
            m_bHotMute = FALSE;
            m_bHotMixer = FALSE;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        BOOL bMute;
        const RECT *prcSlider;
        int iRow = RowHitTest(pt, &bMute, &prcSlider);

        if (iRow != -2 && !bMute && prcSlider)
        {
            m_iDragRow = iRow;
            SetCapture();
            int pct = SliderPercentFromX(prcSlider, pt.x);
            if (iRow == -1)
                ApplyMaster(pct);
            else
                ApplySession(iRow, pct);
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (m_iDragRow != -2)
        {
            m_iDragRow = -2;
            ReleaseCapture();
            InvalidateRect(NULL, FALSE);
            return 0;
        }

        BOOL bMute;
        const RECT *prcSlider;
        int iRow = RowHitTest(pt, &bMute, &prcSlider);

        if (bMute)
        {
            if (iRow == -1)
            {
                m_bMasterMute = !m_bMasterMute;
                if (m_bComVolume && m_pEndpointVolume)
                    m_pEndpointVolume->SetMute(m_bMasterMute, NULL);
                else
                    m_Fallback.SetMute(m_bMasterMute);
            }
            else if (iRow >= 0 && iRow < (int)m_Rows.GetCount())
            {
                TFYMIXROW &row = m_Rows[iRow];
                row.bMute = !row.bMute;
                if (row.pVolume)
                    row.pVolume->SetMute(row.bMute, NULL);
            }
            InvalidateRect(NULL, FALSE);
        }
        else if (PtInRect(&m_rcMixerLink, pt))
        {
            RECT rcSelf;
            GetWindowRect(&rcSelf);
            TrayMixer_Open(&rcSelf);
            FadeOut();
        }
        return 0;
    }

    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int pct = m_nMaster + (delta > 0 ? 5 : -5);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        ApplyMaster(pct);
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
            ApplyMaster(min(m_nMaster + 5, 100));
        else if (wParam == VK_DOWN)
            ApplyMaster(max(m_nMaster - 5, 0));
        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        KillTimer(TFY_TIMER_ANIM);
        FreeRows();
        m_pSessionMgr.Release();
        m_pEndpointVolume.Release();
        m_pDevice.Release();
        m_pEnum.Release();
        m_Fallback.Close();
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

struct TFYMIXCOL
{
    ISimpleAudioVolume *pVolume;
    IAudioSessionControl2 *pControl;
    IAudioMeterInformation *pMeter;
    WCHAR szName[96];
    DWORD dwPid;
    int nPercent;
    BOOL bMute;
    BOOL bActive;
    BOOL bSystem;
    HICON hIcon;
    float fPeak;
    RECT rcCol;
    RECT rcSlider;
    RECT rcMeter;
    RECT rcMute;
};



static HICON
TfyExtractAppIcon(LPCWSTR pszPath)
{
    HICON hIcon = NULL;

    if (!pszPath || !pszPath[0])
        return NULL;
    if (ExtractIconExW(pszPath, 0, &hIcon, NULL, 1) < 1)
        return NULL;
    return hIcon;
}

#define TFY_TIMER_MIXSTATE 7
#define TFY_TIMER_MIXMETER 8

class CVolumeMixerWnd :
    public CWindowImpl<CVolumeMixerWnd, CWindow, CTrayFlyoutTraits>
{
public:
    DECLARE_WND_CLASS_EX(L"ROSVolumeMixer", CS_DROPSHADOW, COLOR_WINDOW)

    SM2_FLYOUT_PALETTE m_Pal;
    CComPtr<IMMDeviceEnumerator> m_pEnum;
    CComPtr<IMMDevice> m_pDevice;
    CComPtr<IAudioEndpointVolume> m_pEndpointVolume;
    CComPtr<IAudioMeterInformation> m_pMasterMeter;
    CComPtr<IAudioSessionManager2> m_pSessionMgr;
    CAtlArray<TFYMIXCOL> m_Cols;
    CAtlArray<TFYAUDIODEV> m_Devices;
    HFONT m_hFont;
    HFONT m_hFontSmall;
    WCHAR m_szDevice[128];
    int m_nMaster;
    BOOL m_bMasterMute;
    BOOL m_bMasterActive;
    float m_fMasterPeak;
    BOOL m_bDevHot;
    BOOL m_bMenu;
    BOOL m_bTracking;
    int m_iDragCol;
    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    RECT m_rcMasterCol, m_rcMasterSlider, m_rcMasterMeter, m_rcMasterMute;
    RECT m_rcDeviceGroup, m_rcAppsGroup, m_rcDevLabel;
    SIZE m_size;
    int m_nTick;

    CVolumeMixerWnd() : m_hFont(NULL), m_hFontSmall(NULL),
                        m_nMaster(0), m_bMasterMute(FALSE), m_bMasterActive(TRUE),
                        m_fMasterPeak(0.0f), m_bDevHot(FALSE), m_bMenu(FALSE), m_bTracking(FALSE),
                        m_iDragCol(-2), m_AnimPhase(TFY_NONE), m_AnimT0(0), m_nTick(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        ZeroMemory(&m_size, sizeof(m_size));
        ZeroMemory(&m_rcDevLabel, sizeof(m_rcDevLabel));
        ZeroMemory(&m_rcMasterMeter, sizeof(m_rcMasterMeter));
        m_szDevice[0] = 0;
    }

    int Sc(int v) const { return ShellScaleForDpi(v); }

    VOID FreeCols()
    {
        for (SIZE_T i = 0; i < m_Cols.GetCount(); i++)
        {
            if (m_Cols[i].pVolume)
                m_Cols[i].pVolume->Release();
            if (m_Cols[i].pControl)
                m_Cols[i].pControl->Release();
            if (m_Cols[i].pMeter)
                m_Cols[i].pMeter->Release();
            if (m_Cols[i].hIcon)
                DestroyIcon(m_Cols[i].hIcon);
        }
        m_Cols.SetCount(0);
    }

    VOID ResolveColumn(TFYMIXCOL *pCol)
    {
        WCHAR szPath[MAX_PATH];

        szPath[0] = 0;
        if (pCol->dwPid)
        {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pCol->dwPid);
            if (!hProcess)
                hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pCol->dwPid);
            if (hProcess)
            {
                DWORD cch = _countof(szPath);
                if (!QueryFullProcessImageNameW(hProcess, 0, szPath, &cch))
                    szPath[0] = 0;
                CloseHandle(hProcess);
            }
        }

        if (!pCol->szName[0] && szPath[0])
        {
            StringCchCopyW(pCol->szName, _countof(pCol->szName), PathFindFileNameW(szPath));
            PathRemoveExtensionW(pCol->szName);
        }
        if (!pCol->szName[0])
            StringCchCopyW(pCol->szName, _countof(pCol->szName), L"System Sounds");

        pCol->hIcon = pCol->bSystem ? NULL : TfyExtractAppIcon(szPath);
    }

    VOID BuildAudio()
    {
        CComPtr<IAudioSessionEnumerator> pSessions;
        int nCount = 0;
        BOOL bHaveSystem = FALSE;
        TFYMIXCOL sysCol;

        ZeroMemory(&sysCol, sizeof(sysCol));
        FreeCols();
        m_pSessionMgr.Release();
        m_pMasterMeter.Release();
        m_pEndpointVolume.Release();
        m_pDevice.Release();
        m_pEnum.Release();
        m_fMasterPeak = 0.0f;
        StringCchCopyW(m_szDevice, _countof(m_szDevice), L"Speakers");
        m_Devices.SetCount(0);
        if (SUCCEEDED(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARG(IMMDeviceEnumerator, &m_pEnum))))
        {
            TfyEnumRenderDevices(m_pEnum, m_Devices);
            if (SUCCEEDED(m_pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &m_pDevice)))
                TfyGetDeviceFriendlyName(m_pDevice, m_szDevice, _countof(m_szDevice));
        }

        if (m_pDevice &&
            SUCCEEDED(m_pDevice->Activate(IID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER,
                                          NULL, (void **)&m_pEndpointVolume)))
        {
            float fLevel = 0.0f;
            BOOL bMute = FALSE;
            if (SUCCEEDED(m_pEndpointVolume->GetMasterVolumeLevelScalar(&fLevel)))
                m_nMaster = (int)(fLevel * 100.0f + 0.5f);
            m_pEndpointVolume->GetMute(&bMute);
            m_bMasterMute = bMute;
        }
        if (m_pDevice)
            m_pDevice->Activate(IID_IAudioMeterInformation, CLSCTX_INPROC_SERVER,
                                NULL, (void **)&m_pMasterMeter);

        if (m_pDevice &&
            SUCCEEDED(m_pDevice->Activate(IID_IAudioSessionManager2, CLSCTX_INPROC_SERVER,
                                          NULL, (void **)&m_pSessionMgr)) &&
            SUCCEEDED(m_pSessionMgr->GetSessionEnumerator(&pSessions)) &&
            SUCCEEDED(pSessions->GetCount(&nCount)))
        {
            for (int i = 0; i < nCount && m_Cols.GetCount() < 8; i++)
            {
                CComPtr<IAudioSessionControl> pControl;
                TFYMIXCOL col;
                AudioSessionState state = AudioSessionStateInactive;
                LPWSTR pszName = NULL;

                ZeroMemory(&col, sizeof(col));

                if (FAILED(pSessions->GetSession(i, &pControl)))
                    continue;
                if (FAILED(pControl->QueryInterface(IID_PPV_ARG(IAudioSessionControl2, &col.pControl))))
                    continue;
                if (SUCCEEDED(col.pControl->GetState(&state)) &&
                    state == AudioSessionStateExpired)
                {
                    col.pControl->Release();
                    continue;
                }
                col.bActive = (state == AudioSessionStateActive);
                col.bSystem = (col.pControl->IsSystemSoundsSession() == S_OK);
                if (FAILED(pControl->QueryInterface(IID_PPV_ARG(ISimpleAudioVolume, &col.pVolume))))
                {
                    col.pControl->Release();
                    continue;
                }
                pControl->QueryInterface(IID_PPV_ARG(IAudioMeterInformation, &col.pMeter));

                col.pControl->GetProcessId(&col.dwPid);
                if (!col.bSystem &&
                    SUCCEEDED(col.pControl->GetDisplayName(&pszName)) && pszName)
                {
                    StringCchCopyW(col.szName, _countof(col.szName), pszName);
                    CoTaskMemFree(pszName);
                }
                if (col.bSystem)
                    StringCchCopyW(col.szName, _countof(col.szName), L"System Sounds");
                ResolveColumn(&col);

                float fLevel = 1.0f;
                BOOL bMute = FALSE;
                col.pVolume->GetMasterVolume(&fLevel);
                col.pVolume->GetMute(&bMute);
                col.nPercent = (int)(fLevel * 100.0f + 0.5f);
                col.bMute = bMute;

                if (col.bSystem && !bHaveSystem)
                {
                    bHaveSystem = TRUE;
                    sysCol = col;
                }
                else if (col.bSystem)
                {
                    col.pVolume->Release();
                    col.pControl->Release();
                    if (col.pMeter)
                        col.pMeter->Release();
                }
                else
                {
                    m_Cols.Add(col);
                }
            }
        }

        if (!bHaveSystem)
        {
            sysCol.bSystem = TRUE;
            sysCol.bActive = TRUE;
            sysCol.nPercent = 100;
            StringCchCopyW(sysCol.szName, _countof(sysCol.szName), L"System Sounds");
        }
        CAtlArray<TFYMIXCOL> apps;
        for (SIZE_T i = 0; i < m_Cols.GetCount(); i++)
            apps.Add(m_Cols[i]);
        m_Cols.SetCount(0);
        m_Cols.Add(sysCol);
        for (SIZE_T i = 0; i < apps.GetCount(); i++)
            m_Cols.Add(apps[i]);
    }

    VOID LayoutColumn(RECT *prcCol, RECT *prcSlider, RECT *prcMeter, RECT *prcMute, int x, int cx)
    {
        SetRect(prcCol, x, Sc(46), x + cx, m_size.cy - Sc(12));
        int cxm = x + cx / 2;
        SetRect(prcSlider, cxm - Sc(24), Sc(140), cxm + Sc(2), m_size.cy - Sc(66));
        SetRect(prcMeter, cxm + Sc(6), Sc(140), cxm + Sc(15), m_size.cy - Sc(66));
        SetRect(prcMute, cxm - Sc(14), m_size.cy - Sc(58), cxm + Sc(14), m_size.cy - Sc(30));
    }

    VOID Layout()
    {
        int devW = Sc(118);
        int colW = Sc(100);
        int nApps = (int)m_Cols.GetCount();

        m_size.cy = Sc(400);
        m_size.cx = Sc(14) + devW + Sc(14) + nApps * colW + Sc(14);
        int x = Sc(14);
        SetRect(&m_rcDeviceGroup, x, Sc(44), x + devW, m_size.cy - Sc(12));
        LayoutColumn(&m_rcMasterCol, &m_rcMasterSlider, &m_rcMasterMeter, &m_rcMasterMute, x, devW);
        SetRect(&m_rcDevLabel, x + Sc(6), Sc(102), x + devW - Sc(6), Sc(124));

        x += devW + Sc(14);
        SetRect(&m_rcAppsGroup, x, Sc(44), x + nApps * colW, m_size.cy - Sc(12));
        for (int i = 0; i < nApps; i++)
        {
            TFYMIXCOL &col = m_Cols[i];
            LayoutColumn(&col.rcCol, &col.rcSlider, &col.rcMeter, &col.rcMute, x + i * colW, colW);
        }
    }

    VOID DrawMuteGlyph(HDC hdc, const RECT *prc, BOOL bMute)
    {
        int cxm = (prc->left + prc->right) / 2;
        int cym = (prc->top + prc->bottom) / 2;
        RECT rcIcon = { cxm - Sc(11), cym - Sc(11), cxm + Sc(11), cym + Sc(11) };
        HBRUSH hbrFrame = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 90));
        FrameRect(hdc, prc, hbrFrame);
        DeleteObject(hbrFrame);
        TfyDrawFluent(hdc, &rcIcon, bMute ? IDI_FLU_SPKMUTE : IDI_FLU_SPK2);
    }

    VOID DrawAppTile(HDC hdc, const RECT *prc)
    {
        TFYAA aa;
        HDC hdcAA = TfyAABegin(&aa, hdc, prc, 3, m_Pal.PanelBg);
        int ss = 3;
        int w = (prc->right - prc->left) * ss;
        int h = (prc->bottom - prc->top) * ss;

        HBRUSH hbrTile = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.AccentBg, 70));
        HPEN hpenTile = CreatePen(PS_SOLID, ss, TfyMix(m_Pal.PanelBg, m_Pal.AccentBg, 150));
        HGDIOBJ hbrOld = SelectObject(hdcAA, hbrTile);
        HGDIOBJ hpenOld = SelectObject(hdcAA, hpenTile);
        RoundRect(hdcAA, ss, ss, w - ss, h - ss, Sc(8) * ss, Sc(8) * ss);
        SelectObject(hdcAA, hpenOld);
        SelectObject(hdcAA, hbrOld);
        DeleteObject(hbrTile);
        DeleteObject(hpenTile);

        int barW = Sc(3) * ss;
        int baseY = h * 3 / 4;
        int heights[3] = { h / 3, h / 2, h / 4 };
        HBRUSH hbrBar = CreateSolidBrush(m_Pal.AccentBg);
        for (int i = 0; i < 3; i++)
        {
            int x = w / 2 + (i - 1) * Sc(6) * ss - barW / 2;
            RECT rcBar = { x, baseY - heights[i], x + barW, baseY };
            FillRect(hdcAA, &rcBar, hbrBar);
        }
        DeleteObject(hbrBar);
        TfyAAEnd(&aa, hdc);
    }

    VOID DrawVSlider(HDC hdc, const RECT *prc, int nPercent, BOOL bDragging)
    {
        TFYAA aa;
        HDC hdcAA = TfyAABegin(&aa, hdc, prc, 4, m_Pal.PanelBg);
        int ss = 4;
        int w = (prc->right - prc->left) * ss;
        int h = (prc->bottom - prc->top) * ss;
        int cxm = w / 2;
        int inset = Sc(6) * ss;
        int yThumb = h - inset - MulDiv(nPercent, h - 2 * inset, 100);

        RECT rcTrack = { cxm - ss, inset, cxm + ss, h - inset };
        HBRUSH hbrTrack = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 130));
        FillRect(hdcAA, &rcTrack, hbrTrack);
        DeleteObject(hbrTrack);

        HBRUSH hbrThumb = CreateSolidBrush(bDragging ? m_Pal.HotBorder : m_Pal.AccentBg);
        HPEN hpenThumb = CreatePen(PS_SOLID, ss, TfyMix(m_Pal.AccentBg, m_Pal.PanelText, 60));
        HGDIOBJ hbrOld = SelectObject(hdcAA, hbrThumb);
        HGDIOBJ hpenOld = SelectObject(hdcAA, hpenThumb);
        RoundRect(hdcAA, cxm - Sc(6) * ss, yThumb - Sc(11) * ss,
                  cxm + Sc(6) * ss, yThumb + Sc(11) * ss, Sc(3) * ss, Sc(3) * ss);
        SelectObject(hdcAA, hpenOld);
        SelectObject(hdcAA, hbrOld);
        DeleteObject(hbrThumb);
        DeleteObject(hpenThumb);

        TfyAAEnd(&aa, hdc);
    }

    int ThumbY(const RECT *prcSlider, int nPercent) const
    {
        int inset = Sc(6);
        int h = prcSlider->bottom - prcSlider->top;
        return prcSlider->top + h - inset - MulDiv(nPercent, h - 2 * inset, 100);
    }

    VOID DrawMeter(HDC hdc, const RECT *prc, float fPeak)
    {
        RECT rcInner = *prc;
        HBRUSH hbrFrame = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 110));
        FrameRect(hdc, prc, hbrFrame);
        DeleteObject(hbrFrame);
        InflateRect(&rcInner, -1, -1);
        HBRUSH hbrBg = CreateSolidBrush(TfyMix(m_Pal.PanelBg, RGB(0, 0, 0), 60));
        FillRect(hdc, &rcInner, hbrBg);
        DeleteObject(hbrBg);
        if (fPeak <= 0.0f)
            return;
        if (fPeak > 1.0f)
            fPeak = 1.0f;
        int h = rcInner.bottom - rcInner.top;
        int hFill = (int)(h * fPeak + 0.5f);
        if (hFill < 1)
            hFill = 1;
        RECT rcFill = { rcInner.left, rcInner.bottom - hFill, rcInner.right, rcInner.bottom };
        HBRUSH hbrFill = CreateSolidBrush(RGB(64, 186, 84));
        FillRect(hdc, &rcFill, hbrFill);
        DeleteObject(hbrFill);
        if (fPeak > 0.85f)
        {
            RECT rcHot = rcFill;
            rcHot.bottom = rcInner.top + (int)(h * 0.15f);
            HBRUSH hbrHot = CreateSolidBrush(RGB(232, 72, 56));
            FillRect(hdc, &rcHot, hbrHot);
            DeleteObject(hbrHot);
        }
    }

    VOID DrawGroup(HDC hdc, const RECT *prc, LPCWSTR pszLabel)
    {
        HBRUSH hbrFrame = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 100));
        FrameRect(hdc, prc, hbrFrame);
        DeleteObject(hbrFrame);

        SIZE sz;
        HGDIOBJ hOldFont = SelectObject(hdc, m_hFontSmall);
        GetTextExtentPoint32W(hdc, pszLabel, lstrlenW(pszLabel), &sz);
        RECT rcLabel = { prc->left + Sc(10), prc->top - sz.cy / 2,
                         prc->left + Sc(10) + sz.cx + Sc(8), prc->top + sz.cy / 2 + 1 };
        HBRUSH hbrBg = CreateSolidBrush(m_Pal.PanelBg);
        FillRect(hdc, &rcLabel, hbrBg);
        DeleteObject(hbrBg);
        SetTextColor(hdc, m_Pal.DimText);
        DrawTextW(hdc, pszLabel, -1, &rcLabel,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        SelectObject(hdc, hOldFont);
    }

    VOID DrawDeviceDropdown(HDC hdc)
    {
        RECT rc = m_rcDevLabel;
        if (m_bDevHot || m_bMenu)
        {
            HBRUSH hbrFill = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 140));
            FillRect(hdc, &rc, hbrFill);
            DeleteObject(hbrFill);
            HBRUSH hbrFrame = CreateSolidBrush(m_Pal.HotBorder);
            FrameRect(hdc, &rc, hbrFrame);
            DeleteObject(hbrFrame);
        }
        RECT rcArrow = { rc.right - Sc(16), (rc.top + rc.bottom) / 2 - Sc(6),
                         rc.right - Sc(4), (rc.top + rc.bottom) / 2 + Sc(6) };
        TfyDrawFluent(hdc, &rcArrow, IDI_FLU_CHEVDOWN);
        RECT rcText = { rc.left + Sc(4), rc.top, rcArrow.left - Sc(2), rc.bottom };
        SelectObject(hdc, m_hFontSmall);
        SetTextColor(hdc, m_Pal.PanelText);
        DrawTextW(hdc, m_szDevice, -1, &rcText,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    VOID DrawColumn(HDC hdc, const RECT *prcCol, const RECT *prcSlider, const RECT *prcMeter,
                    const RECT *prcMute, HICON hIcon, int nKind, LPCWSTR pszName,
                    int nPercent, BOOL bMute, float fPeak, BOOL bDragging)
    {
        int cxm = (prcCol->left + prcCol->right) / 2;
        RECT rcIcon = { cxm - Sc(16), Sc(62), cxm + Sc(16), Sc(62) + Sc(32) };

        if (hIcon)
            DrawIconEx(hdc, rcIcon.left, rcIcon.top, hIcon, Sc(32), Sc(32), 0, NULL, DI_NORMAL);
        else if (nKind == 1)
            TfyDrawFluent(hdc, &rcIcon, IDI_FLU_SPK2);
        else if (nKind == 2)
            TfyDrawFluent(hdc, &rcIcon, IDI_FLU_DESKTOP);
        else
            DrawAppTile(hdc, &rcIcon);

        if (pszName)
        {
            RECT rcName = { prcCol->left + Sc(4), Sc(100), prcCol->right - Sc(4), Sc(134) };
            SelectObject(hdc, m_hFontSmall);
            SetTextColor(hdc, m_Pal.PanelText);
            DrawTextW(hdc, pszName, -1, &rcName,
                      DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        DrawVSlider(hdc, prcSlider, nPercent, bDragging);
        DrawMeter(hdc, prcMeter, bMute ? 0.0f : fPeak);
        DrawMuteGlyph(hdc, prcMute, bMute);
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

        WCHAR szHeader[192];
        StringCchPrintfW(szHeader, _countof(szHeader), L"Volume Mixer - %s", m_szDevice);
        RECT rcHeader = { Sc(14), Sc(8), rc.right - Sc(14), Sc(30) };
        SelectObject(hdcMem, m_hFont);
        SetTextColor(hdcMem, m_Pal.PanelText);
        DrawTextW(hdcMem, szHeader, -1, &rcHeader,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        DrawGroup(hdcMem, &m_rcDeviceGroup, L"Device");
        DrawGroup(hdcMem, &m_rcAppsGroup, L"Applications");

        if (m_Cols.GetCount())
        {
            int yMaster = ThumbY(&m_rcMasterSlider, m_nMaster);
            RECT rcLine = { m_rcAppsGroup.left + Sc(6), yMaster, m_rcAppsGroup.right - Sc(6), yMaster + 1 };
            HBRUSH hbrLine = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 120));
            FillRect(hdcMem, &rcLine, hbrLine);
            DeleteObject(hbrLine);
        }

        HBRUSH hbrSep = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 90));
        for (SIZE_T i = 1; i < m_Cols.GetCount(); i++)
        {
            RECT rcSep = { m_Cols[i].rcCol.left, Sc(66), m_Cols[i].rcCol.left + 1, m_Cols[i].rcMute.bottom - Sc(6) };
            FillRect(hdcMem, &rcSep, hbrSep);
        }
        DeleteObject(hbrSep);

        DrawColumn(hdcMem, &m_rcMasterCol, &m_rcMasterSlider, &m_rcMasterMeter, &m_rcMasterMute,
                   NULL, 1, NULL, m_nMaster, m_bMasterMute, m_fMasterPeak, m_iDragCol == -1);
        DrawDeviceDropdown(hdcMem);

        for (SIZE_T i = 0; i < m_Cols.GetCount(); i++)
        {
            TFYMIXCOL &col = m_Cols[i];
            DrawColumn(hdcMem, &col.rcCol, &col.rcSlider, &col.rcMeter, &col.rcMute,
                       col.hIcon, col.bSystem ? 2 : 0, col.szName, col.nPercent, col.bMute,
                       col.fPeak, m_iDragCol == (int)i);
        }

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

    int HitTestSlider(POINT pt, const RECT *prcSlider) const
    {
        RECT rcHit = *prcSlider;
        InflateRect(&rcHit, Sc(4), Sc(6));
        return PtInRect(&rcHit, pt);
    }

    VOID ApplyColPercent(int iCol, int nPercent)
    {
        if (nPercent < 0) nPercent = 0;
        if (nPercent > 100) nPercent = 100;

        if (iCol == -1)
        {
            m_nMaster = nPercent;
            if (m_pEndpointVolume)
                m_pEndpointVolume->SetMasterVolumeLevelScalar(nPercent / 100.0f, NULL);
        }
        else if (iCol >= 0 && iCol < (int)m_Cols.GetCount())
        {
            TFYMIXCOL &col = m_Cols[iCol];
            col.nPercent = nPercent;
            if (col.pVolume)
                col.pVolume->SetMasterVolume(nPercent / 100.0f, NULL);
        }
        InvalidateRect(NULL, FALSE);
    }

    int PercentFromY(const RECT *prcSlider, int y) const
    {
        int inset = Sc(6);
        int h = prcSlider->bottom - prcSlider->top - 2 * inset;
        if (h <= 0)
            return 0;
        return MulDiv(prcSlider->bottom - inset - y, 100, h);
    }

    VOID ShowDeviceMenu()
    {
        if (m_Devices.GetCount() == 0 || m_bMenu)
            return;
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu)
            return;
        for (SIZE_T i = 0; i < m_Devices.GetCount(); i++)
            AppendMenuW(hMenu, MF_STRING | (m_Devices[i].bDefault ? MF_CHECKED : 0),
                        (UINT_PTR)(i + 1), m_Devices[i].szName);
        POINT pt = { m_rcDevLabel.left, m_rcDevLabel.bottom + Sc(2) };
        ClientToScreen(&pt);
        m_bMenu = TRUE;
        InvalidateRect(NULL, FALSE);
        int cmd = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                                   pt.x, pt.y, m_hWnd, NULL);
        m_bMenu = FALSE;
        DestroyMenu(hMenu);
        if (cmd > 0 && (SIZE_T)cmd <= m_Devices.GetCount() && !m_Devices[cmd - 1].bDefault)
        {
            TfySetDefaultRenderDevice(m_Devices[cmd - 1].szId);
            BuildAudio();
            Layout();
            Reposition();
        }
        InvalidateRect(NULL, FALSE);
    }

    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (PtInRect(&m_rcDevLabel, pt))
        {
            ShowDeviceMenu();
            return 0;
        }
        if (HitTestSlider(pt, &m_rcMasterSlider))
        {
            m_iDragCol = -1;
            SetCapture();
            ApplyColPercent(-1, PercentFromY(&m_rcMasterSlider, pt.y));
            return 0;
        }
        if (PtInRect(&m_rcMasterMute, pt))
        {
            m_bMasterMute = !m_bMasterMute;
            if (m_pEndpointVolume)
                m_pEndpointVolume->SetMute(m_bMasterMute, NULL);
            InvalidateRect(NULL, FALSE);
            return 0;
        }

        for (SIZE_T i = 0; i < m_Cols.GetCount(); i++)
        {
            TFYMIXCOL &col = m_Cols[i];
            if (HitTestSlider(pt, &col.rcSlider))
            {
                m_iDragCol = (int)i;
                SetCapture();
                ApplyColPercent((int)i, PercentFromY(&col.rcSlider, pt.y));
                return 0;
            }
            if (PtInRect(&col.rcMute, pt))
            {
                col.bMute = !col.bMute;
                if (col.pVolume)
                    col.pVolume->SetMute(col.bMute, NULL);
                InvalidateRect(NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (m_iDragCol < -1)
        {
            BOOL bHot = PtInRect(&m_rcDevLabel, pt);
            if (bHot != m_bDevHot)
            {
                m_bDevHot = bHot;
                InvalidateRect(&m_rcDevLabel, FALSE);
            }
            if (!m_bTracking)
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
                m_bTracking = TrackMouseEvent(&tme);
            }
            return 0;
        }
        const RECT *prc = (m_iDragCol == -1) ? &m_rcMasterSlider
                                             : &m_Cols[m_iDragCol].rcSlider;
        ApplyColPercent(m_iDragCol, PercentFromY(prc, pt.y));
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_bTracking = FALSE;
        if (m_bDevHot)
        {
            m_bDevHot = FALSE;
            InvalidateRect(&m_rcDevLabel, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (m_iDragCol >= -1)
        {
            m_iDragCol = -2;
            ReleaseCapture();
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(&pt);
        int delta = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 4 : -4;

        if (PtInRect(&m_rcMasterCol, pt))
        {
            ApplyColPercent(-1, m_nMaster + delta);
            return 0;
        }
        for (SIZE_T i = 0; i < m_Cols.GetCount(); i++)
        {
            if (PtInRect(&m_Cols[i].rcCol, pt))
            {
                ApplyColPercent((int)i, m_Cols[i].nPercent + delta);
                return 0;
            }
        }
        return 0;
    }

    static float SmoothPeak(float fOld, float fNew)
    {
        if (fNew >= fOld)
            return fNew;
        float f = fOld - 0.08f;
        return f > fNew ? f : fNew;
    }

    BOOL UpdateMeters()
    {
        BOOL bChanged = FALSE;
        float fMaster = m_nMaster / 100.0f;
        float fPeak = 0.0f;

        if (m_pMasterMeter && SUCCEEDED(m_pMasterMeter->GetPeakValue(&fPeak)))
        {
            float f = SmoothPeak(m_fMasterPeak, m_bMasterMute ? 0.0f : fPeak * fMaster);
            if (f != m_fMasterPeak)
            {
                m_fMasterPeak = f;
                bChanged = TRUE;
            }
        }
        for (SIZE_T i = 0; i < m_Cols.GetCount(); i++)
        {
            TFYMIXCOL &col = m_Cols[i];
            fPeak = 0.0f;
            if (col.pMeter)
                col.pMeter->GetPeakValue(&fPeak);
            float f = SmoothPeak(col.fPeak, col.bMute ? 0.0f : fPeak * (col.nPercent / 100.0f) * fMaster);
            if (f != col.fPeak)
            {
                col.fPeak = f;
                bChanged = TRUE;
            }
        }
        return bChanged;
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
            return 0;
        }

        if (wParam == TFY_TIMER_MIXMETER)
        {
            if (UpdateMeters() && m_AnimPhase != TFY_CLOSE)
                InvalidateRect(NULL, FALSE);
            return 0;
        }

        if (wParam != TFY_TIMER_MIXSTATE)
            return 0;

        BOOL bRebuild = FALSE;
        m_nTick++;

        if (m_pEndpointVolume && m_iDragCol != -1)
        {
            float fLevel = 0.0f;
            BOOL bMute = FALSE;
            if (SUCCEEDED(m_pEndpointVolume->GetMasterVolumeLevelScalar(&fLevel)))
                m_nMaster = (int)(fLevel * 100.0f + 0.5f);
            if (SUCCEEDED(m_pEndpointVolume->GetMute(&bMute)))
                m_bMasterMute = bMute;
        }

        for (SIZE_T i = 0; i < m_Cols.GetCount(); i++)
        {
            TFYMIXCOL &col = m_Cols[i];
            AudioSessionState state;
            if (col.pControl && SUCCEEDED(col.pControl->GetState(&state)))
            {
                if (state == AudioSessionStateExpired)
                    bRebuild = TRUE;
                col.bActive = (state == AudioSessionStateActive);
            }
            if (m_iDragCol != (int)i && col.pVolume)
            {
                float fLevel = 1.0f;
                BOOL bMute = FALSE;
                if (SUCCEEDED(col.pVolume->GetMasterVolume(&fLevel)))
                    col.nPercent = (int)(fLevel * 100.0f + 0.5f);
                if (SUCCEEDED(col.pVolume->GetMute(&bMute)))
                    col.bMute = bMute;
            }
        }

        if (!bRebuild && (m_nTick % 4) == 0 && m_pSessionMgr)
        {
            CComPtr<IAudioSessionEnumerator> pSessions;
            int nCount = 0;
            int nLive = 0;
            if (SUCCEEDED(m_pSessionMgr->GetSessionEnumerator(&pSessions)) &&
                SUCCEEDED(pSessions->GetCount(&nCount)))
            {
                for (int i = 0; i < nCount; i++)
                {
                    CComPtr<IAudioSessionControl> pControl;
                    AudioSessionState state = AudioSessionStateInactive;
                    if (FAILED(pSessions->GetSession(i, &pControl)))
                        continue;
                    if (SUCCEEDED(pControl->GetState(&state)) && state == AudioSessionStateExpired)
                        continue;
                    nLive++;
                }
                int nHave = 0;
                for (SIZE_T i = 0; i < m_Cols.GetCount(); i++)
                    if (m_Cols[i].pControl)
                        nHave++;
                if (nLive != nHave)
                    bRebuild = TRUE;
            }
        }

        if (bRebuild && m_iDragCol < -1 && !m_bMenu && m_AnimPhase != TFY_CLOSE)
        {
            BuildAudio();
            Layout();
            Reposition();
        }

        InvalidateRect(NULL, FALSE);
        return 0;
    }

    VOID Reposition()
    {
        RECT rcWin;
        GetWindowRect(&rcWin);
        m_ptFinal.x = rcWin.right - m_size.cx;
        m_ptFinal.y = rcWin.bottom - m_size.cy;
        SetWindowPos(NULL, m_ptFinal.x, m_ptFinal.y, m_size.cx, m_size.cy,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == VK_ESCAPE)
            FadeOut();
        return 0;
    }

    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == WA_INACTIVE && !m_bMenu)
            FadeOut();
        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        KillTimer(TFY_TIMER_ANIM);
        KillTimer(TFY_TIMER_MIXSTATE);
        KillTimer(TFY_TIMER_MIXMETER);
        FreeCols();
        m_pSessionMgr.Release();
        m_pMasterMeter.Release();
        m_pEndpointVolume.Release();
        m_pDevice.Release();
        m_pEnum.Release();
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall); m_hFontSmall = NULL; }
        return 0;
    }

    VOID FadeOut();

    VOID Open(const RECT *prcAnchor)
    {
        MONITORINFO mi;
        POINT ptRef;
        int xPos, yPos;

        StartMenu2_GetFlyoutPalette(&m_Pal);
        if (!m_hFont) m_hFont = TfyCreateFont(12, FW_SEMIBOLD);
        if (!m_hFontSmall) m_hFontSmall = TfyCreateFont(11, FW_NORMAL);

        BuildAudio();
        Layout();
        ptRef.x = prcAnchor->right;
        ptRef.y = prcAnchor->bottom;
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST), &mi);

        xPos = prcAnchor->right - m_size.cx;
        if (xPos + m_size.cx > mi.rcMonitor.right) xPos = mi.rcMonitor.right - m_size.cx;
        if (xPos < mi.rcMonitor.left) xPos = mi.rcMonitor.left;
        yPos = prcAnchor->bottom - m_size.cy;
        if (yPos < mi.rcMonitor.top) yPos = mi.rcMonitor.top;

        m_ptFinal.x = xPos;
        m_ptFinal.y = yPos;

        m_AnimPhase = TFY_OPEN;
        m_AnimT0 = GetTickCount64();

        SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
        SetWindowPos(HWND_TOPMOST, xPos, yPos + Sc(10), m_size.cx, m_size.cy,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::SetForegroundWindow(m_hWnd);
        SetTimer(TFY_TIMER_ANIM, 16, NULL);
        SetTimer(TFY_TIMER_MIXSTATE, 500, NULL);
        SetTimer(TFY_TIMER_MIXMETER, 50, NULL);
        InvalidateRect(NULL, FALSE);
    }

    virtual void OnFinalMessage(HWND hWnd) override;

    BEGIN_MSG_MAP(CVolumeMixerWnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()
};

static CVolumeMixerWnd *g_pVolumeMixer = NULL;
static ULONGLONG g_MixerDismissTick = 0;

void CVolumeMixerWnd::OnFinalMessage(HWND hWnd)
{
    if (g_pVolumeMixer == this)
        g_pVolumeMixer = NULL;
    delete this;
}

VOID CVolumeMixerWnd::FadeOut()
{
    if (m_AnimPhase == TFY_CLOSE)
        return;
    g_MixerDismissTick = GetTickCount64();
    m_AnimPhase = TFY_CLOSE;
    m_AnimT0 = g_MixerDismissTick;
    SetTimer(TFY_TIMER_ANIM, 16, NULL);
}

VOID TrayMixer_Open(const RECT *prcAnchor)
{
    if (GetTickCount64() - g_MixerDismissTick < 350)
        return;

    if (g_pVolumeMixer && g_pVolumeMixer->IsWindow())
    {
        g_pVolumeMixer->DestroyWindow();
        g_pVolumeMixer = NULL;
    }

    CVolumeMixerWnd *pMixer = new CVolumeMixerWnd();
    if (!pMixer)
        return;

    if (!pMixer->Create(NULL, CWindow::rcDefault, NULL))
    {
        delete pMixer;
        return;
    }

    g_pVolumeMixer = pMixer;
    pMixer->Open(prcAnchor);
}

#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <wlanapi.h>
#include <commctrl.h>

typedef DWORD (WINAPI *PFN_WLANOPENHANDLE)(DWORD, PVOID, PDWORD, PHANDLE);
typedef DWORD (WINAPI *PFN_WLANCLOSEHANDLE)(HANDLE, PVOID);
typedef DWORD (WINAPI *PFN_WLANENUMINTERFACES)(HANDLE, PVOID, PWLAN_INTERFACE_INFO_LIST *);
typedef DWORD (WINAPI *PFN_WLANGETNETWORKLIST)(HANDLE, const GUID *, DWORD, PVOID, PWLAN_AVAILABLE_NETWORK_LIST *);
typedef DWORD (WINAPI *PFN_WLANCONNECT)(HANDLE, const GUID *, const PWLAN_CONNECTION_PARAMETERS, PVOID);
typedef DWORD (WINAPI *PFN_WLANDISCONNECT)(HANDLE, const GUID *, PVOID);
typedef DWORD (WINAPI *PFN_WLANSCAN)(HANDLE, const GUID *, const PDOT11_SSID, const PWLAN_RAW_DATA, PVOID);
typedef DWORD (WINAPI *PFN_WLANSETPROFILE)(HANDLE, const GUID *, DWORD, LPCWSTR, LPCWSTR, BOOL, PVOID, DWORD *);
typedef DWORD (WINAPI *PFN_WLANREGISTERNOTIFICATION)(HANDLE, DWORD, BOOL, WLAN_NOTIFICATION_CALLBACK, PVOID, PVOID, PDWORD);
typedef DWORD (WINAPI *PFN_WLANQUERYINTERFACE)(HANDLE, const GUID *, WLAN_INTF_OPCODE, PVOID, PDWORD, PVOID *, WLAN_OPCODE_VALUE_TYPE *);
typedef DWORD (WINAPI *PFN_WLANSETINTERFACE)(HANDLE, const GUID *, WLAN_INTF_OPCODE, DWORD, const VOID *, PVOID);
typedef VOID (WINAPI *PFN_WLANFREEMEMORY)(PVOID);

#define TFY_WM_WLANNOTIFY (WM_APP + 41)
#define TFY_TIMER_SCAN 9
#define TFY_TIMER_POLL 10
#define TFY_NET_EDIT_ID 101

#define TFY_NETHIT_NONE     -1
#define TFY_NETHIT_LINK     -2
#define TFY_NETHIT_REFRESH  -3
#define TFY_NETHIT_AIRPLANE -4
#define TFY_NETHIT_WIFI     -5
#define TFY_NETHIT_BUTTON   0x100
#define TFY_NETHIT_BUTTON2  0x200
#define TFY_NETHIT_CHECK    0x300

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
    BOOL bSecure;
    BOOL bConnectable;
    DOT11_AUTH_ALGORITHM auth;
    DOT11_CIPHER_ALGORITHM cipher;
    RECT rc;
    RECT rcButton;
    RECT rcButton2;
    RECT rcCheck;
    RECT rcEdit;
};

static VOID
TfyXmlEscape(LPCWSTR pszIn, LPWSTR pszOut, SIZE_T cchOut)
{
    SIZE_T o = 0;
    for (; *pszIn && o + 8 < cchOut; pszIn++)
    {
        LPCWSTR pszRep = NULL;
        switch (*pszIn)
        {
            case L'&': pszRep = L"&amp;"; break;
            case L'<': pszRep = L"&lt;"; break;
            case L'>': pszRep = L"&gt;"; break;
            case L'"': pszRep = L"&quot;"; break;
            case L'\'': pszRep = L"&apos;"; break;
        }
        if (pszRep)
        {
            while (*pszRep)
                pszOut[o++] = *pszRep++;
        }
        else
        {
            pszOut[o++] = *pszIn;
        }
    }
    pszOut[o] = 0;
}

static LRESULT CALLBACK
TfyNetEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                       UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_KEYDOWN && (wParam == VK_RETURN || wParam == VK_ESCAPE))
    {
        ::PostMessageW(::GetParent(hWnd), WM_KEYDOWN, wParam, lParam);
        return 0;
    }
    if (uMsg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE))
        return 0;
    if (uMsg == WM_NCDESTROY)
        RemoveWindowSubclass(hWnd, TfyNetEditSubclassProc, uIdSubclass);
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

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
    PFN_WLANOPENHANDLE m_pfnOpen;
    PFN_WLANENUMINTERFACES m_pfnEnum;
    PFN_WLANGETNETWORKLIST m_pfnList;
    PFN_WLANCONNECT m_pfnConnect;
    PFN_WLANDISCONNECT m_pfnDisconnect;
    PFN_WLANSCAN m_pfnScan;
    PFN_WLANSETPROFILE m_pfnSetProfile;
    PFN_WLANREGISTERNOTIFICATION m_pfnRegister;
    PFN_WLANQUERYINTERFACE m_pfnQuery;
    PFN_WLANSETINTERFACE m_pfnSet;
    PFN_WLANCLOSEHANDLE m_pfnClose;
    PFN_WLANFREEMEMORY m_pfnFree;
    HWND m_hwndEdit;
    HBRUSH m_hbrEdit;
    int m_iHot;
    int m_iSel;
    int m_iHotBtn;
    BOOL m_bAutoConnect;
    BOOL m_bPassMode;
    BOOL m_bScanning;
    BOOL m_bConnecting;
    BOOL m_bTracking;
    int m_nIfaces;
    WCHAR m_szSelName[128];
    WCHAR m_szBanner[128];
    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    RECT m_rcAnchor;
    SIZE m_size;
    RECT m_rcLink;
    RECT m_rcRefresh;
    RECT m_rcAirplane;
    RECT m_rcWifi;
    BOOL m_bWifiOn;
    BOOL m_bAirplane;

    CTrayNetworkWnd() : m_hFont(NULL), m_hFontSmall(NULL), m_hFontHeader(NULL),
                        m_hWlanApi(NULL), m_hWlan(NULL),
                        m_pfnOpen(NULL), m_pfnEnum(NULL), m_pfnList(NULL),
                        m_pfnConnect(NULL), m_pfnDisconnect(NULL), m_pfnScan(NULL),
                        m_pfnSetProfile(NULL), m_pfnRegister(NULL), m_pfnQuery(NULL), m_pfnSet(NULL),
                        m_pfnClose(NULL), m_pfnFree(NULL),
                        m_hwndEdit(NULL), m_hbrEdit(NULL),
                        m_iHot(TFY_NETHIT_NONE), m_iSel(-1), m_iHotBtn(0),
                        m_bAutoConnect(TRUE), m_bPassMode(FALSE), m_bScanning(FALSE),
                        m_bConnecting(FALSE), m_bTracking(FALSE), m_nIfaces(0),
                        m_AnimPhase(TFY_NONE), m_AnimT0(0), m_bWifiOn(TRUE), m_bAirplane(FALSE)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        ZeroMemory(&m_rcAnchor, sizeof(m_rcAnchor));
        ZeroMemory(&m_size, sizeof(m_size));
        ZeroMemory(&m_rcLink, sizeof(m_rcLink));
        ZeroMemory(&m_rcRefresh, sizeof(m_rcRefresh));
        m_szSelName[0] = 0;
        m_szBanner[0] = 0;
    }

    int Sc(int v) const { return ShellScaleForDpi(v); }

    static VOID WINAPI WlanNotifyCallback(PWLAN_NOTIFICATION_DATA pData, PVOID pContext)
    {
        HWND hWnd = (HWND)pContext;
        if (!pData || !hWnd)
            return;
        if (pData->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM &&
            pData->NotificationSource != WLAN_NOTIFICATION_SOURCE_MSM)
            return;
        ::PostMessageW(hWnd, TFY_WM_WLANNOTIFY, (WPARAM)pData->NotificationCode,
                       (LPARAM)pData->NotificationSource);
    }

    BOOL LoadWlan()
    {
        DWORD dwVersion = 0;
        DWORD dwPrev = 0;
        if (!m_hWlanApi)
            m_hWlanApi = LoadLibraryW(L"wlanapi.dll");
        if (!m_hWlanApi)
            return FALSE;
        m_pfnOpen = (PFN_WLANOPENHANDLE)GetProcAddress(m_hWlanApi, "WlanOpenHandle");
        m_pfnEnum = (PFN_WLANENUMINTERFACES)GetProcAddress(m_hWlanApi, "WlanEnumInterfaces");
        m_pfnList = (PFN_WLANGETNETWORKLIST)GetProcAddress(m_hWlanApi, "WlanGetAvailableNetworkList");
        m_pfnConnect = (PFN_WLANCONNECT)GetProcAddress(m_hWlanApi, "WlanConnect");
        m_pfnDisconnect = (PFN_WLANDISCONNECT)GetProcAddress(m_hWlanApi, "WlanDisconnect");
        m_pfnScan = (PFN_WLANSCAN)GetProcAddress(m_hWlanApi, "WlanScan");
        m_pfnSetProfile = (PFN_WLANSETPROFILE)GetProcAddress(m_hWlanApi, "WlanSetProfile");
        m_pfnRegister = (PFN_WLANREGISTERNOTIFICATION)GetProcAddress(m_hWlanApi, "WlanRegisterNotification");
        m_pfnQuery = (PFN_WLANQUERYINTERFACE)GetProcAddress(m_hWlanApi, "WlanQueryInterface");
        m_pfnSet = (PFN_WLANSETINTERFACE)GetProcAddress(m_hWlanApi, "WlanSetInterface");
        m_pfnClose = (PFN_WLANCLOSEHANDLE)GetProcAddress(m_hWlanApi, "WlanCloseHandle");
        m_pfnFree = (PFN_WLANFREEMEMORY)GetProcAddress(m_hWlanApi, "WlanFreeMemory");
        if (!m_pfnOpen || !m_pfnEnum || !m_pfnList || !m_pfnClose || !m_pfnFree)
            return FALSE;
        if (!m_hWlan)
        {
            if (m_pfnOpen(2, NULL, &dwVersion, &m_hWlan) != ERROR_SUCCESS)
            {
                m_hWlan = NULL;
                return FALSE;
            }
            if (m_pfnRegister)
                m_pfnRegister(m_hWlan, WLAN_NOTIFICATION_SOURCE_ACM | WLAN_NOTIFICATION_SOURCE_MSM, TRUE,
                              WlanNotifyCallback, (PVOID)m_hWnd, NULL, &dwPrev);
        }
        return TRUE;
    }

    static BOOL LoadAirplane(BOOL *pbWifiRestore)
    {
        HKEY hKey;
        DWORD dwValue = 0, cb = sizeof(dwValue), dwType = 0;
        BOOL bAirplane = FALSE;
        *pbWifiRestore = TRUE;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ReactOS\\Explorer", 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
            return FALSE;
        if (RegQueryValueExW(hKey, L"AirplaneMode", NULL, &dwType, (LPBYTE)&dwValue, &cb) == ERROR_SUCCESS && dwType == REG_DWORD)
            bAirplane = (dwValue != 0);
        cb = sizeof(dwValue);
        if (RegQueryValueExW(hKey, L"WifiRestore", NULL, &dwType, (LPBYTE)&dwValue, &cb) == ERROR_SUCCESS && dwType == REG_DWORD)
            *pbWifiRestore = (dwValue != 0);
        RegCloseKey(hKey);
        return bAirplane;
    }

    static VOID SaveAirplane(BOOL bAirplane, BOOL bWifiRestore)
    {
        HKEY hKey;
        DWORD dwValue;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ReactOS\\Explorer", 0, NULL, 0,
                            KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
            return;
        dwValue = bAirplane ? 1 : 0;
        RegSetValueExW(hKey, L"AirplaneMode", 0, REG_DWORD, (const BYTE *)&dwValue, sizeof(dwValue));
        dwValue = bWifiRestore ? 1 : 0;
        RegSetValueExW(hKey, L"WifiRestore", 0, REG_DWORD, (const BYTE *)&dwValue, sizeof(dwValue));
        RegCloseKey(hKey);
    }

    BOOL QueryRadio()
    {
        PWLAN_INTERFACE_INFO_LIST pInterfaces = NULL;
        BOOL bAny = FALSE, bOn = FALSE;
        if (!LoadWlan() || !m_pfnQuery)
            return TRUE;
        if (m_pfnEnum(m_hWlan, NULL, &pInterfaces) != ERROR_SUCCESS || !pInterfaces)
            return TRUE;
        for (DWORD i = 0; i < pInterfaces->dwNumberOfItems; i++)
        {
            DWORD cb = 0;
            PVOID pData = NULL;
            WLAN_OPCODE_VALUE_TYPE vt;
            if (m_pfnQuery(m_hWlan, &pInterfaces->InterfaceInfo[i].InterfaceGuid,
                           wlan_intf_opcode_radio_state, NULL, &cb, &pData, &vt) == ERROR_SUCCESS && pData)
            {
                WLAN_RADIO_STATE *prs = (WLAN_RADIO_STATE *)pData;
                bAny = TRUE;
                for (DWORD k = 0; k < prs->dwNumberOfPhys && k < 64; k++)
                {
                    if (prs->PhyRadioState[k].dot11SoftwareRadioState == dot11_radio_state_on)
                        bOn = TRUE;
                }
                m_pfnFree(pData);
            }
        }
        m_pfnFree(pInterfaces);
        return bAny ? bOn : TRUE;
    }

    VOID SetRadio(BOOL bOn)
    {
        PWLAN_INTERFACE_INFO_LIST pInterfaces = NULL;
        if (!LoadWlan() || !m_pfnSet)
            return;
        if (m_pfnEnum(m_hWlan, NULL, &pInterfaces) != ERROR_SUCCESS || !pInterfaces)
            return;
        for (DWORD i = 0; i < pInterfaces->dwNumberOfItems; i++)
        {
            WLAN_PHY_RADIO_STATE rs;
            ZeroMemory(&rs, sizeof(rs));
            rs.dwPhyIndex = 0;
            rs.dot11SoftwareRadioState = bOn ? dot11_radio_state_on : dot11_radio_state_off;
            rs.dot11HardwareRadioState = dot11_radio_state_on;
            m_pfnSet(m_hWlan, &pInterfaces->InterfaceInfo[i].InterfaceGuid,
                     wlan_intf_opcode_radio_state, sizeof(rs), &rs, NULL);
        }
        m_pfnFree(pInterfaces);
    }

    VOID ToggleAirplane()
    {
        BOOL bRestore = TRUE;
        LoadAirplane(&bRestore);
        if (!m_bAirplane)
        {
            bRestore = m_bWifiOn;
            m_bAirplane = TRUE;
            SaveAirplane(TRUE, bRestore);
            SetRadio(FALSE);
        }
        else
        {
            m_bAirplane = FALSE;
            SaveAirplane(FALSE, bRestore);
            SetRadio(bRestore);
        }
        EndScan();
        Rebuild();
        if (!m_bAirplane && m_bWifiOn)
            StartScan();
        Relayout();
    }

    VOID ToggleWifi()
    {
        BOOL bOn = !m_bWifiOn;
        SetRadio(bOn);
        m_bWifiOn = bOn;
        EndScan();
        Rebuild();
        if (bOn)
            StartScan();
        Relayout();
    }

    VOID UnloadWlan()
    {
        DWORD dwPrev = 0;
        if (m_hWlan && m_pfnRegister)
            m_pfnRegister(m_hWlan, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, NULL, NULL, NULL, &dwPrev);
        if (m_hWlan && m_pfnClose)
            m_pfnClose(m_hWlan, NULL);
        m_hWlan = NULL;
    }

    VOID AddAdapters()
    {
        ULONG cbBuffer = 16 * 1024;
        PIP_ADAPTER_ADDRESSES pAddresses = NULL;
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
                pAddresses = NULL;
                continue;
            }
            if (ret != NO_ERROR)
            {
                HeapFree(hProcessHeap, 0, pAddresses);
                return;
            }
            break;
        }
        if (!pAddresses)
            return;
        for (PIP_ADAPTER_ADDRESSES pCurrent = pAddresses;
             pCurrent != NULL && m_Rows.GetCount() < 20;
             pCurrent = pCurrent->Next)
        {
            if (pCurrent->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                pCurrent->IfType == IF_TYPE_TUNNEL ||
                pCurrent->IfType == IF_TYPE_IEEE80211)
                continue;
            TFYNETROW row;
            ZeroMemory(&row, sizeof(row));
            row.nType = 0;
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
        PWLAN_INTERFACE_INFO_LIST pInterfaces = NULL;
        m_nIfaces = 0;
        if (!LoadWlan())
            return;
        if (m_pfnEnum(m_hWlan, NULL, &pInterfaces) != ERROR_SUCCESS || !pInterfaces)
            return;
        m_nIfaces = (int)pInterfaces->dwNumberOfItems;
        for (DWORD i = 0; i < pInterfaces->dwNumberOfItems; i++)
        {
            PWLAN_AVAILABLE_NETWORK_LIST pNetworks = NULL;
            const GUID *pGuid = &pInterfaces->InterfaceInfo[i].InterfaceGuid;
            if (m_pfnList(m_hWlan, pGuid, 0, NULL, &pNetworks) != ERROR_SUCCESS || !pNetworks)
                continue;
            for (DWORD n = 0; n < pNetworks->dwNumberOfItems && m_Rows.GetCount() < 24; n++)
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
                    TFYNETROW &ex = m_Rows[r];
                    if (ex.nType == 2 && !wcscmp(ex.szName, szSsid))
                    {
                        bDuplicate = TRUE;
                        if ((int)pNet->wlanSignalQuality > ex.nSignal)
                            ex.nSignal = pNet->wlanSignalQuality;
                        if (pNet->dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED)
                            ex.bConnected = TRUE;
                        if ((pNet->dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) && !ex.bHasProfile)
                        {
                            ex.bHasProfile = TRUE;
                            StringCchCopyW(ex.szProfile, _countof(ex.szProfile), pNet->strProfileName);
                        }
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
                row.bSecure = pNet->bSecurityEnabled;
                row.bConnectable = pNet->bNetworkConnectable;
                row.auth = pNet->dot11DefaultAuthAlgorithm;
                row.cipher = pNet->dot11DefaultCipherAlgorithm;
                StringCchCopyW(row.szName, _countof(row.szName), szSsid);
                if (row.bHasProfile)
                    StringCchCopyW(row.szProfile, _countof(row.szProfile), pNet->strProfileName);
                if (row.bConnected)
                    StringCchCopyW(row.szStatus, _countof(row.szStatus), L"Connected");
                m_Rows.Add(row);
            }
            m_pfnFree(pNetworks);
        }
        m_pfnFree(pInterfaces);
        for (SIZE_T a = 0; a < m_Rows.GetCount(); a++)
        {
            for (SIZE_T b = a + 1; b < m_Rows.GetCount(); b++)
            {
                TFYNETROW &ra = m_Rows[a];
                TFYNETROW &rb = m_Rows[b];
                BOOL bSwap = FALSE;
                if (rb.bConnected && !ra.bConnected)
                    bSwap = TRUE;
                else if (rb.bConnected == ra.bConnected && rb.nSignal > ra.nSignal)
                    bSwap = TRUE;
                if (bSwap)
                {
                    TFYNETROW t = ra;
                    ra = rb;
                    rb = t;
                }
            }
        }
    }

    VOID Rebuild()
    {
        WCHAR szKeep[128];
        StringCchCopyW(szKeep, _countof(szKeep), m_szSelName);
        BOOL bRestore = TRUE;
        m_Rows.SetCount(0);
        AddWlanNetworks();
        AddAdapters();
        m_bAirplane = LoadAirplane(&bRestore);
        m_bWifiOn = QueryRadio();
        if (m_nIfaces > 0 && !m_bScanning)
        {
            if (m_bAirplane)
                StringCchCopyW(m_szBanner, _countof(m_szBanner), L"Airplane mode is on");
            else if (!m_bWifiOn)
                StringCchCopyW(m_szBanner, _countof(m_szBanner), L"Wi-Fi is turned off");
            else if (!wcscmp(m_szBanner, L"Airplane mode is on") || !wcscmp(m_szBanner, L"Wi-Fi is turned off"))
                m_szBanner[0] = 0;
        }
        m_iSel = -1;
        if (szKeep[0])
        {
            for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
            {
                if (m_Rows[i].nType == 2 && !wcscmp(m_Rows[i].szName, szKeep))
                {
                    m_iSel = (int)i;
                    break;
                }
            }
        }
        if (m_iSel < 0)
        {
            m_bPassMode = FALSE;
            m_szSelName[0] = 0;
            HideEdit();
        }
        if (m_bConnecting)
        {
            for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
            {
                if (m_Rows[i].nType == 2 && !wcscmp(m_Rows[i].szName, szKeep) && !m_Rows[i].bConnected)
                    StringCchCopyW(m_Rows[i].szStatus, _countof(m_Rows[i].szStatus), L"Connecting...");
            }
        }
    }

    VOID StartScan()
    {
        PWLAN_INTERFACE_INFO_LIST pInterfaces = NULL;
        if (!LoadWlan() || !m_pfnScan || !m_bWifiOn || m_bAirplane)
            return;
        if (m_pfnEnum(m_hWlan, NULL, &pInterfaces) != ERROR_SUCCESS || !pInterfaces)
            return;
        for (DWORD i = 0; i < pInterfaces->dwNumberOfItems; i++)
        {
            if (m_pfnScan(m_hWlan, &pInterfaces->InterfaceInfo[i].InterfaceGuid, NULL, NULL, NULL) == ERROR_SUCCESS)
                m_bScanning = TRUE;
        }
        m_pfnFree(pInterfaces);
        if (m_bScanning)
        {
            StringCchCopyW(m_szBanner, _countof(m_szBanner), L"Searching for networks...");
            SetTimer(TFY_TIMER_SCAN, 8000, NULL);
        }
    }

    VOID EndScan()
    {
        if (!m_bScanning)
            return;
        m_bScanning = FALSE;
        KillTimer(TFY_TIMER_SCAN);
        if (!wcscmp(m_szBanner, L"Searching for networks..."))
            m_szBanner[0] = 0;
    }

    VOID Layout()
    {
        int y = Sc(10) + Sc(28);
        m_size.cx = Sc(320);
        SetRect(&m_rcRefresh, m_size.cx - Sc(12) - Sc(22), Sc(10), m_size.cx - Sc(12), Sc(10) + Sc(22));
        if (m_szBanner[0] || (m_nIfaces == 0 && !m_Rows.GetCount()))
            y += Sc(20);
        if (m_nIfaces > 0)
        {
            SetRect(&m_rcAirplane, Sc(8), y, m_size.cx - Sc(8), y + Sc(36));
            y += Sc(36);
            SetRect(&m_rcWifi, Sc(8), y, m_size.cx - Sc(8), y + Sc(36));
            y += Sc(36) + Sc(6);
        }
        else
        {
            SetRectEmpty(&m_rcAirplane);
            SetRectEmpty(&m_rcWifi);
        }
        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            TFYNETROW &row = m_Rows[i];
            int nRowH = (row.nType == 2) ? Sc(40) : Sc(46);
            BOOL bExpanded = (row.nType == 2 && (int)i == m_iSel);
            SetRect(&row.rcButton, 0, 0, 0, 0);
            SetRect(&row.rcButton2, 0, 0, 0, 0);
            SetRect(&row.rcCheck, 0, 0, 0, 0);
            SetRect(&row.rcEdit, 0, 0, 0, 0);
            if (bExpanded)
            {
                int yb = y + nRowH;
                if (m_bPassMode)
                {
                    yb += Sc(22);
                    SetRect(&row.rcEdit, Sc(46), yb, m_size.cx - Sc(20), yb + Sc(24));
                    yb += Sc(24) + Sc(8);
                    SetRect(&row.rcButton, m_size.cx - Sc(20) - Sc(160), yb, m_size.cx - Sc(20) - Sc(84), yb + Sc(26));
                    SetRect(&row.rcButton2, m_size.cx - Sc(20) - Sc(76), yb, m_size.cx - Sc(20), yb + Sc(26));
                    yb += Sc(26) + Sc(10);
                }
                else
                {
                    if (!row.bConnected)
                    {
                        SetRect(&row.rcCheck, Sc(46), yb + Sc(4), m_size.cx - Sc(20), yb + Sc(24));
                        yb += Sc(26);
                    }
                    SetRect(&row.rcButton, m_size.cx - Sc(20) - Sc(96), yb, m_size.cx - Sc(20), yb + Sc(26));
                    yb += Sc(26) + Sc(10);
                }
                nRowH = yb - y;
            }
            SetRect(&row.rc, Sc(8), y, m_size.cx - Sc(8), y + nRowH);
            y += nRowH;
        }
        y += Sc(8);
        SetRect(&m_rcLink, Sc(8), y, m_size.cx - Sc(8), y + Sc(26));
        y += Sc(26) + Sc(10);
        m_size.cy = y;
        if (m_hwndEdit && m_iSel >= 0 && m_bPassMode)
        {
            TFYNETROW &row = m_Rows[m_iSel];
            ::SetWindowPos(m_hwndEdit, NULL, row.rcEdit.left, row.rcEdit.top,
                           row.rcEdit.right - row.rcEdit.left, row.rcEdit.bottom - row.rcEdit.top,
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }

    VOID Reposition()
    {
        MONITORINFO mi;
        POINT ptRef;
        int xPos, yPos;
        ptRef.x = m_rcAnchor.right;
        ptRef.y = m_rcAnchor.top;
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST), &mi);
        xPos = m_rcAnchor.right - m_size.cx - Sc(8);
        if (xPos + m_size.cx > mi.rcMonitor.right) xPos = mi.rcMonitor.right - m_size.cx;
        if (xPos < mi.rcMonitor.left) xPos = mi.rcMonitor.left;
        yPos = m_rcAnchor.top - m_size.cy - Sc(6);
        if (yPos < mi.rcMonitor.top) yPos = mi.rcMonitor.top;
        m_ptFinal.x = xPos;
        m_ptFinal.y = yPos;
        if (m_AnimPhase == TFY_NONE)
            SetWindowPos(NULL, xPos, yPos, m_size.cx, m_size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    VOID Relayout()
    {
        Layout();
        Reposition();
        InvalidateRect(NULL, FALSE);
    }

    VOID Toggle(HWND hwndOwner, const RECT *prcAnchor)
    {
        StartMenu2_GetFlyoutPalette(&m_Pal);
        if (!m_hFont) m_hFont = TfyCreateFont(12, FW_SEMIBOLD);
        if (!m_hFontSmall) m_hFontSmall = TfyCreateFont(11, FW_NORMAL);
        if (!m_hFontHeader) m_hFontHeader = TfyCreateFont(14, FW_SEMIBOLD);
        m_rcAnchor = *prcAnchor;
        Rebuild();
        if (m_nIfaces == 0)
            StringCchCopyW(m_szBanner, _countof(m_szBanner), L"No wireless adapter");
        else
            StartScan();
        Layout();
        Reposition();
        m_AnimPhase = TFY_OPEN;
        m_AnimT0 = GetTickCount64();
        SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
        SetWindowPos(HWND_TOPMOST, m_ptFinal.x, m_ptFinal.y + Sc(10), m_size.cx, m_size.cy,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::SetForegroundWindow(m_hWnd);
        SetTimer(TFY_TIMER_ANIM, 16, NULL);
        InvalidateRect(NULL, FALSE);
    }

    VOID FadeOut();

    VOID HideEdit()
    {
        if (m_hwndEdit)
            ::ShowWindow(m_hwndEdit, SW_HIDE);
    }

    VOID ShowEdit()
    {
        if (!m_hwndEdit)
        {
            m_hwndEdit = CreateWindowExW(0, L"EDIT", NULL,
                                         WS_CHILD | ES_PASSWORD | ES_AUTOHSCROLL | ES_LEFT,
                                         0, 0, Sc(200), Sc(24), m_hWnd, (HMENU)(INT_PTR)TFY_NET_EDIT_ID,
                                         hExplorerInstance, NULL);
            if (!m_hwndEdit)
                return;
            ::SendMessageW(m_hwndEdit, WM_SETFONT, (WPARAM)m_hFont, FALSE);
            ::SendMessageW(m_hwndEdit, EM_SETLIMITTEXT, 64, 0);
            SetWindowSubclass(m_hwndEdit, TfyNetEditSubclassProc, 1, 0);
        }
        ::SetWindowTextW(m_hwndEdit, L"");
        ::ShowWindow(m_hwndEdit, SW_SHOWNOACTIVATE);
        ::SetFocus(m_hwndEdit);
    }

    LPCWSTR AuthName(const TFYNETROW &row) const
    {
        switch (row.auth)
        {
            case DOT11_AUTH_ALGO_RSNA_PSK: return L"WPA2PSK";
            case DOT11_AUTH_ALGO_WPA_PSK: return L"WPAPSK";
            case DOT11_AUTH_ALGO_WPA3_SAE: return L"WPA3SAE";
            case DOT11_AUTH_ALGO_RSNA: return L"WPA2";
            case DOT11_AUTH_ALGO_WPA: return L"WPA";
            case DOT11_AUTH_ALGO_80211_SHARED_KEY: return L"shared";
            default: return L"open";
        }
    }

    LPCWSTR CipherName(const TFYNETROW &row) const
    {
        switch (row.cipher)
        {
            case DOT11_CIPHER_ALGO_CCMP: return L"AES";
            case DOT11_CIPHER_ALGO_TKIP: return L"TKIP";
            case DOT11_CIPHER_ALGO_WEP:
            case DOT11_CIPHER_ALGO_WEP40:
            case DOT11_CIPHER_ALGO_WEP104: return L"WEP";
            default: return row.bSecure ? L"AES" : L"none";
        }
    }

    BOOL NeedsKey(const TFYNETROW &row) const
    {
        return row.bSecure && !row.bHasProfile;
    }

    BOOL IsEnterprise(const TFYNETROW &row) const
    {
        return row.auth == DOT11_AUTH_ALGO_RSNA || row.auth == DOT11_AUTH_ALGO_WPA;
    }

    BOOL SaveProfile(TFYNETROW &row, LPCWSTR pszKey)
    {
        WCHAR szSsid[512], szKey[512], szXml[2048];
        DWORD dwReason = 0;
        if (!m_hWlan || !m_pfnSetProfile)
            return FALSE;
        TfyXmlEscape(row.szName, szSsid, _countof(szSsid));
        szKey[0] = 0;
        if (pszKey && pszKey[0])
            TfyXmlEscape(pszKey, szKey, _countof(szKey));
        if (row.bSecure)
        {
            StringCchPrintfW(szXml, _countof(szXml),
                L"<?xml version=\"1.0\"?>"
                L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
                L"<name>%s</name><SSIDConfig><SSID><name>%s</name></SSID></SSIDConfig>"
                L"<connectionType>ESS</connectionType><connectionMode>%s</connectionMode>"
                L"<MSM><security><authEncryption><authentication>%s</authentication>"
                L"<encryption>%s</encryption><useOneX>false</useOneX></authEncryption>"
                L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
                L"<keyMaterial>%s</keyMaterial></sharedKey></security></MSM></WLANProfile>",
                szSsid, szSsid, m_bAutoConnect ? L"auto" : L"manual",
                AuthName(row), CipherName(row), szKey);
        }
        else
        {
            StringCchPrintfW(szXml, _countof(szXml),
                L"<?xml version=\"1.0\"?>"
                L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
                L"<name>%s</name><SSIDConfig><SSID><name>%s</name></SSID></SSIDConfig>"
                L"<connectionType>ESS</connectionType><connectionMode>%s</connectionMode>"
                L"<MSM><security><authEncryption><authentication>open</authentication>"
                L"<encryption>none</encryption><useOneX>false</useOneX></authEncryption>"
                L"</security></MSM></WLANProfile>",
                szSsid, szSsid, m_bAutoConnect ? L"auto" : L"manual");
        }
        DWORD dwRet = m_pfnSetProfile(m_hWlan, &row.ifGuid, 0, szXml, NULL, TRUE, NULL, &dwReason);
        SecureZeroMemory(szXml, sizeof(szXml));
        SecureZeroMemory(szKey, sizeof(szKey));
        if (dwRet != ERROR_SUCCESS)
            return FALSE;
        row.bHasProfile = TRUE;
        StringCchCopyW(row.szProfile, _countof(row.szProfile), row.szName);
        return TRUE;
    }

    VOID SetRowFailed(TFYNETROW &row, LPCWSTR pszText)
    {
        StringCchCopyW(row.szStatus, _countof(row.szStatus), pszText);
        m_bConnecting = FALSE;
        KillTimer(TFY_TIMER_POLL);
    }

    VOID ConnectRow(int i, LPCWSTR pszKey)
    {
        if (i < 0 || i >= (int)m_Rows.GetCount())
            return;
        TFYNETROW &row = m_Rows[i];
        if (row.nType != 2 || !m_hWlan || !m_pfnConnect)
            return;
        if (IsEnterprise(row) && !row.bHasProfile)
        {
            SetRowFailed(row, L"Enterprise networks are not supported");
            InvalidateRect(NULL, FALSE);
            return;
        }
        if (NeedsKey(row) && (!pszKey || !pszKey[0]))
        {
            m_bPassMode = TRUE;
            Relayout();
            ShowEdit();
            Relayout();
            return;
        }
        WLAN_CONNECTION_PARAMETERS params;
        ZeroMemory(&params, sizeof(params));
        params.dot11BssType = dot11_BSS_type_infrastructure;
        DOT11_SSID ssid;
        ZeroMemory(&ssid, sizeof(ssid));
        if (row.bHasProfile || row.bSecure || m_bAutoConnect)
        {
            if (!row.bHasProfile || (pszKey && pszKey[0]))
            {
                if (!SaveProfile(row, pszKey))
                {
                    SetRowFailed(row, L"Can't save the network profile");
                    m_bPassMode = FALSE;
                    HideEdit();
                    Relayout();
                    return;
                }
            }
            params.wlanConnectionMode = wlan_connection_mode_profile;
            params.strProfile = row.szProfile;
        }
        else
        {
            int cb = WideCharToMultiByte(CP_UTF8, 0, row.szName, -1, (LPSTR)ssid.ucSSID,
                                         DOT11_SSID_MAX_LENGTH, NULL, NULL);
            ssid.uSSIDLength = (cb > 0) ? (ULONG)(cb - 1) : 0;
            params.wlanConnectionMode = wlan_connection_mode_discovery_unsecure;
            params.pDot11Ssid = &ssid;
        }
        m_bPassMode = FALSE;
        HideEdit();
        StringCchCopyW(m_szSelName, _countof(m_szSelName), row.szName);
        if (m_pfnConnect(m_hWlan, &row.ifGuid, &params, NULL) == ERROR_SUCCESS)
        {
            m_bConnecting = TRUE;
            StringCchCopyW(row.szStatus, _countof(row.szStatus), L"Connecting...");
            SetTimer(TFY_TIMER_POLL, 2000, NULL);
        }
        else
        {
            SetRowFailed(row, L"Can't connect to this network");
        }
        Relayout();
    }

    VOID DisconnectRow(int i)
    {
        if (i < 0 || i >= (int)m_Rows.GetCount())
            return;
        TFYNETROW &row = m_Rows[i];
        if (row.nType != 2 || !m_hWlan || !m_pfnDisconnect)
            return;
        m_pfnDisconnect(m_hWlan, &row.ifGuid, NULL);
        row.bConnected = FALSE;
        StringCchCopyW(row.szStatus, _countof(row.szStatus), L"Disconnecting...");
        SetTimer(TFY_TIMER_POLL, 1500, NULL);
        Relayout();
    }

    VOID SubmitPassword()
    {
        WCHAR szKey[128];
        if (!m_hwndEdit || m_iSel < 0)
            return;
        szKey[0] = 0;
        ::GetWindowTextW(m_hwndEdit, szKey, _countof(szKey));
        if (wcslen(szKey) < 8)
        {
            StringCchCopyW(m_szBanner, _countof(m_szBanner), L"The security key must be at least 8 characters");
            Relayout();
            return;
        }
        m_szBanner[0] = 0;
        ConnectRow(m_iSel, szKey);
        SecureZeroMemory(szKey, sizeof(szKey));
        if (m_hwndEdit)
            ::SetWindowTextW(m_hwndEdit, L"");
    }

    VOID CancelPassword()
    {
        m_bPassMode = FALSE;
        HideEdit();
        if (!wcscmp(m_szBanner, L"The security key must be at least 8 characters"))
            m_szBanner[0] = 0;
        Relayout();
    }

    VOID SelectRow(int i)
    {
        if (m_bPassMode)
            CancelPassword();
        if (i == m_iSel)
        {
            m_iSel = -1;
            m_szSelName[0] = 0;
        }
        else
        {
            m_iSel = i;
            if (i >= 0 && m_Rows[i].nType == 2)
                StringCchCopyW(m_szSelName, _countof(m_szSelName), m_Rows[i].szName);
            else
                m_szSelName[0] = 0;
        }
        Relayout();
    }

    VOID DrawToggle(HDC hdc, const RECT *prc, LPCWSTR pszLabel, BOOL bOn, BOOL bHot, BOOL bEnabled, UINT nIcon)
    {
        if (bHot && bEnabled)
        {
            HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
            FillRect(hdc, prc, hbrHot);
            DeleteObject(hbrHot);
        }
        int cyMid = (prc->top + prc->bottom) / 2;
        COLORREF crText = bEnabled ? m_Pal.PanelText : m_Pal.DimText;
        RECT rcIcon = { prc->left + Sc(8), cyMid - Sc(11), prc->left + Sc(30), cyMid + Sc(11) };
        TfyDrawFluent(hdc, &rcIcon, nIcon);
        RECT rcSw = { prc->right - Sc(8) - Sc(44), cyMid - Sc(10), prc->right - Sc(8), cyMid + Sc(10) };
        RECT rcLabel = { prc->left + Sc(38), prc->top, rcSw.left - Sc(40), prc->bottom };
        SelectObject(hdc, m_hFont);
        SetTextColor(hdc, crText);
        DrawTextW(hdc, pszLabel, -1, &rcLabel, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT rcState = { rcSw.left - Sc(36), prc->top, rcSw.left - Sc(6), prc->bottom };
        SelectObject(hdc, m_hFontSmall);
        SetTextColor(hdc, bEnabled ? m_Pal.DimText : TfyMix(m_Pal.PanelBg, m_Pal.DimText, 150));
        DrawTextW(hdc, bOn ? L"On" : L"Off", -1, &rcState, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);

        TFYAA aa;
        int ss = 3;
        HDC hdcAA = TfyAABegin(&aa, hdc, &rcSw, ss, (bHot && bEnabled) ? TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130) : m_Pal.PanelBg);
        int w = (rcSw.right - rcSw.left) * ss;
        int h = (rcSw.bottom - rcSw.top) * ss;
        COLORREF crTrack = bOn ? m_Pal.AccentBg : m_Pal.PanelBg;
        COLORREF crEdge = bOn ? m_Pal.AccentBg : m_Pal.DimText;
        COLORREF crKnob = bOn ? m_Pal.AccentText : m_Pal.PanelText;
        if (!bEnabled)
        {
            crTrack = TfyMix(m_Pal.PanelBg, crTrack, 90);
            crEdge = TfyMix(m_Pal.PanelBg, crEdge, 90);
            crKnob = TfyMix(m_Pal.PanelBg, crKnob, 90);
        }
        HBRUSH hbrTrack = CreateSolidBrush(crTrack);
        HPEN hpenTrack = CreatePen(PS_SOLID, 2 * ss, crEdge);
        HGDIOBJ hbrOld = SelectObject(hdcAA, hbrTrack);
        HGDIOBJ hpenOld = SelectObject(hdcAA, hpenTrack);
        RoundRect(hdcAA, ss, ss, w - ss, h - ss, h, h);
        SelectObject(hdcAA, hpenOld);
        SelectObject(hdcAA, hbrOld);
        DeleteObject(hbrTrack);
        DeleteObject(hpenTrack);
        int r = (h / 2) - Sc(4) * ss;
        int cx = bOn ? (w - h / 2) : (h / 2);
        HBRUSH hbrKnob = CreateSolidBrush(crKnob);
        hbrOld = SelectObject(hdcAA, hbrKnob);
        hpenOld = SelectObject(hdcAA, GetStockObject(NULL_PEN));
        Ellipse(hdcAA, cx - r, h / 2 - r, cx + r, h / 2 + r);
        SelectObject(hdcAA, hpenOld);
        SelectObject(hdcAA, hbrOld);
        DeleteObject(hbrKnob);
        TfyAAEnd(&aa, hdc);
    }

    VOID DrawLock(HDC hdc, int x, int y, COLORREF cr)
    {
        HPEN hPen = CreatePen(PS_SOLID, Sc(1), cr);
        HBRUSH hbr = CreateSolidBrush(cr);
        HGDIOBJ hOldPen = SelectObject(hdc, hPen);
        HGDIOBJ hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Arc(hdc, x + Sc(2), y, x + Sc(8), y + Sc(7), x + Sc(8), y + Sc(4), x + Sc(2), y + Sc(4));
        SelectObject(hdc, hbr);
        RECT rcBody = { x, y + Sc(4), x + Sc(10), y + Sc(11) };
        FillRect(hdc, &rcBody, hbr);
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hbr);
        DeleteObject(hPen);
    }

    VOID DrawButton(HDC hdc, const RECT *prc, LPCWSTR pszText, BOOL bHot, BOOL bPrimary)
    {
        COLORREF crFill = bPrimary ? m_Pal.HotBorder : TfyMix(m_Pal.PanelBg, m_Pal.DimText, 60);
        if (bHot)
            crFill = TfyMix(crFill, RGB(255, 255, 255), 40);
        HBRUSH hbr = CreateSolidBrush(crFill);
        FillRect(hdc, prc, hbr);
        DeleteObject(hbr);
        HBRUSH hbrEdge = CreateSolidBrush(TfyMix(crFill, m_Pal.PanelText, 60));
        FrameRect(hdc, prc, hbrEdge);
        DeleteObject(hbrEdge);
        HGDIOBJ hOld = SelectObject(hdc, m_hFont);
        SetTextColor(hdc, bPrimary ? RGB(255, 255, 255) : m_Pal.PanelText);
        DrawTextW(hdc, pszText, -1, (LPRECT)prc, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        SelectObject(hdc, hOld);
    }

    VOID DrawCheck(HDC hdc, const RECT *prc, BOOL bChecked, LPCWSTR pszText, BOOL bHot)
    {
        int cy = (prc->top + prc->bottom) / 2;
        RECT rcBox = { prc->left, cy - Sc(7), prc->left + Sc(14), cy + Sc(7) };
        HBRUSH hbrBox = CreateSolidBrush(bChecked ? m_Pal.HotBorder : m_Pal.PanelBg);
        FillRect(hdc, &rcBox, hbrBox);
        DeleteObject(hbrBox);
        HBRUSH hbrEdge = CreateSolidBrush(bHot ? m_Pal.PanelText : m_Pal.DimText);
        FrameRect(hdc, &rcBox, hbrEdge);
        DeleteObject(hbrEdge);
        if (bChecked)
        {
            HPEN hPen = CreatePen(PS_SOLID, Sc(2), RGB(255, 255, 255));
            HGDIOBJ hOldPen = SelectObject(hdc, hPen);
            MoveToEx(hdc, rcBox.left + Sc(3), cy, NULL);
            LineTo(hdc, rcBox.left + Sc(6), cy + Sc(3));
            LineTo(hdc, rcBox.right - Sc(3), cy - Sc(4));
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);
        }
        RECT rcText = { rcBox.right + Sc(8), prc->top, prc->right, prc->bottom };
        HGDIOBJ hOld = SelectObject(hdc, m_hFontSmall);
        SetTextColor(hdc, m_Pal.PanelText);
        DrawTextW(hdc, pszText, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(hdc, hOld);
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
        RECT rcTitle = { Sc(12), Sc(8), m_rcRefresh.left - Sc(6), Sc(8) + Sc(26) };
        SetTextColor(hdcMem, m_Pal.PanelText);
        DrawTextW(hdcMem, L"Networks", -1, &rcTitle,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (m_nIfaces > 0)
        {
            if (m_iHot == TFY_NETHIT_REFRESH)
            {
                HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
                FillRect(hdcMem, &m_rcRefresh, hbrHot);
                DeleteObject(hbrHot);
            }
            RECT rcIcon = { m_rcRefresh.left + Sc(3), m_rcRefresh.top + Sc(3),
                            m_rcRefresh.right - Sc(3), m_rcRefresh.bottom - Sc(3) };
            TfyDrawFluent(hdcMem, &rcIcon, IDI_FLU_REFRESH);
        }
        if (m_szBanner[0])
        {
            RECT rcBanner = { Sc(12), Sc(10) + Sc(26), rc.right - Sc(12), Sc(10) + Sc(26) + Sc(20) };
            SelectObject(hdcMem, m_hFontSmall);
            SetTextColor(hdcMem, m_Pal.DimText);
            DrawTextW(hdcMem, m_szBanner, -1, &rcBanner,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (m_nIfaces > 0)
        {
            DrawToggle(hdcMem, &m_rcAirplane, L"Airplane mode", m_bAirplane,
                       m_iHot == TFY_NETHIT_AIRPLANE, TRUE, IDI_FLU_AIRPLANE);
            DrawToggle(hdcMem, &m_rcWifi, L"Wi-Fi", m_bWifiOn && !m_bAirplane,
                       m_iHot == TFY_NETHIT_WIFI, !m_bAirplane, IDI_FLU_WIFI1);
        }
        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            TFYNETROW &row = m_Rows[i];
            RECT rcRow = row.rc;
            BOOL bExpanded = (row.nType == 2 && (int)i == m_iSel);
            if ((int)i == m_iHot || bExpanded)
            {
                HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, bExpanded ? 90 : 130));
                FillRect(hdcMem, &rcRow, hbrHot);
                DeleteObject(hbrHot);
            }
            int nHeadH = (row.nType == 2) ? Sc(40) : Sc(46);
            int cyMid = rcRow.top + nHeadH / 2;
            RECT rcNetIcon = { rcRow.left + Sc(8), cyMid - Sc(11),
                               rcRow.left + Sc(30), cyMid + Sc(11) };
            if (row.nType == 2)
            {
                UINT nWifi = row.nSignal >= 75 ? IDI_FLU_WIFI1 :
                             row.nSignal >= 50 ? IDI_FLU_WIFI2 :
                             row.nSignal >= 25 ? IDI_FLU_WIFI3 : IDI_FLU_WIFI4;
                TfyDrawFluent(hdcMem, &rcNetIcon, nWifi);
                if (row.bSecure)
                {
                    RECT rcLock = { rcNetIcon.right - Sc(9), rcNetIcon.bottom - Sc(9),
                                    rcNetIcon.right + Sc(3), rcNetIcon.bottom + Sc(3) };
                    RECT rcLockBg = rcLock;
                    InflateRect(&rcLockBg, -Sc(1), -Sc(1));
                    HBRUSH hbrLockBg = CreateSolidBrush(m_Pal.PanelBg);
                    FillRect(hdcMem, &rcLockBg, hbrLockBg);
                    DeleteObject(hbrLockBg);
                    TfyDrawFluent(hdcMem, &rcLock, IDI_FLU_LOCK);
                }
            }
            else
            {
                TfyDrawFluent(hdcMem, &rcNetIcon,
                              row.bConnected ? IDI_FLU_NETADAPTER : IDI_FLU_GLOBE);
            }
            RECT rcText = { rcRow.left + Sc(38), rcRow.top + Sc(4), rcRow.right - Sc(8), cyMid + Sc(4) };
            SelectObject(hdcMem, m_hFont);
            SetTextColor(hdcMem, m_Pal.PanelText);
            if (row.nType == 2)
            {
                RECT rcSsid = { rcRow.left + Sc(38), rcRow.top, rcRow.right - Sc(86), rcRow.top + nHeadH };
                if (bExpanded)
                {
                    rcSsid.bottom = rcRow.top + nHeadH / 2 + Sc(3);
                    rcSsid.top = rcRow.top + Sc(2);
                }
                DrawTextW(hdcMem, row.szName, -1, &rcSsid,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (bExpanded)
                {
                    RECT rcInfo = { rcSsid.left, rcSsid.bottom, rcRow.right - Sc(8), rcRow.top + nHeadH };
                    SelectObject(hdcMem, m_hFontSmall);
                    SetTextColor(hdcMem, m_Pal.DimText);
                    LPCWSTR pszInfo = row.bSecure ? (IsEnterprise(row) ? L"Secured (enterprise)" : L"Secured") : L"Open";
                    DrawTextW(hdcMem, pszInfo, -1, &rcInfo,
                              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                if (row.szStatus[0])
                {
                    RECT rcTag = { rcRow.right - Sc(84), rcRow.top, rcRow.right - Sc(8), rcRow.top + nHeadH };
                    SelectObject(hdcMem, m_hFontSmall);
                    SetTextColor(hdcMem, row.bConnected ? m_Pal.HotBorder : m_Pal.DimText);
                    DrawTextW(hdcMem, row.szStatus, -1, &rcTag,
                              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                if (bExpanded)
                {
                    if (m_bPassMode)
                    {
                        RECT rcPrompt = { rcRow.left + Sc(38), rcRow.top + nHeadH, rcRow.right - Sc(8), rcRow.top + nHeadH + Sc(22) };
                        SelectObject(hdcMem, m_hFontSmall);
                        SetTextColor(hdcMem, m_Pal.PanelText);
                        DrawTextW(hdcMem, L"Enter the network security key", -1, &rcPrompt,
                                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                        HBRUSH hbrEditEdge = CreateSolidBrush(m_Pal.DimText);
                        RECT rcEditFrame = row.rcEdit;
                        InflateRect(&rcEditFrame, 1, 1);
                        FrameRect(hdcMem, &rcEditFrame, hbrEditEdge);
                        DeleteObject(hbrEditEdge);
                        DrawButton(hdcMem, &row.rcButton, L"Next", m_iHotBtn == 1, TRUE);
                        DrawButton(hdcMem, &row.rcButton2, L"Cancel", m_iHotBtn == 2, FALSE);
                    }
                    else
                    {
                        if (!row.bConnected)
                            DrawCheck(hdcMem, &row.rcCheck, m_bAutoConnect, L"Connect automatically", m_iHotBtn == 3);
                        DrawButton(hdcMem, &row.rcButton, row.bConnected ? L"Disconnect" : L"Connect",
                                   m_iHotBtn == 1, !row.bConnected);
                    }
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
        if (m_Rows.GetCount() == 0 && !m_szBanner[0])
        {
            RECT rcNone = { Sc(12), Sc(40), rc.right - Sc(12), Sc(70) };
            SelectObject(hdcMem, m_hFontSmall);
            SetTextColor(hdcMem, m_Pal.DimText);
            DrawTextW(hdcMem, L"No networks found", -1, &rcNone,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        {
            int cyLink = (m_rcLink.top + m_rcLink.bottom) / 2;
            RECT rcGear = { m_rcLink.left, cyLink - Sc(8),
                            m_rcLink.left + Sc(16), cyLink + Sc(8) };
            TfyDrawFluent(hdcMem, &rcGear, IDI_FLU_SETTINGS);
        }
        RECT rcLinkText = m_rcLink;
        rcLinkText.left += Sc(22);
        SelectObject(hdcMem, m_hFontSmall);
        SetTextColor(hdcMem, m_iHot == TFY_NETHIT_LINK ? m_Pal.HotBorder : m_Pal.DimText);
        DrawTextW(hdcMem, L"Open Network and Sharing Center", -1, &rcLinkText,
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

    LRESULT OnCtlColorEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        HDC hdc = (HDC)wParam;
        COLORREF crBg = TfyMix(m_Pal.PanelBg, m_Pal.PanelText, 20);
        SetBkColor(hdc, crBg);
        SetTextColor(hdc, m_Pal.PanelText);
        if (m_hbrEdit)
            DeleteObject(m_hbrEdit);
        m_hbrEdit = CreateSolidBrush(crBg);
        return (LRESULT)m_hbrEdit;
    }

    LRESULT OnWlanNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if ((DWORD)lParam == WLAN_NOTIFICATION_SOURCE_MSM)
        {
            if ((int)wParam == wlan_notification_msm_radio_state_change)
            {
                EndScan();
                Rebuild();
                Relayout();
            }
            return 0;
        }
        switch ((int)wParam)
        {
            case wlan_notification_acm_scan_complete:
            case wlan_notification_acm_scan_fail:
                EndScan();
                Rebuild();
                Relayout();
                break;
            case wlan_notification_acm_connection_start:
                m_bConnecting = TRUE;
                break;
            case wlan_notification_acm_connection_complete:
                m_bConnecting = FALSE;
                KillTimer(TFY_TIMER_POLL);
                m_szBanner[0] = 0;
                Rebuild();
                Relayout();
                break;
            case wlan_notification_acm_connection_attempt_fail:
                m_bConnecting = FALSE;
                KillTimer(TFY_TIMER_POLL);
                Rebuild();
                for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
                {
                    if (m_Rows[i].nType == 2 && m_szSelName[0] && !wcscmp(m_Rows[i].szName, m_szSelName) &&
                        !m_Rows[i].bConnected)
                        StringCchCopyW(m_Rows[i].szStatus, _countof(m_Rows[i].szStatus), L"Can't connect");
                }
                Relayout();
                break;
            case wlan_notification_acm_disconnected:
            case wlan_notification_acm_interface_arrival:
            case wlan_notification_acm_interface_removal:
            case wlan_notification_acm_profile_change:
                Rebuild();
                Relayout();
                break;
        }
        return 0;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == TFY_TIMER_SCAN)
        {
            EndScan();
            Rebuild();
            Relayout();
            return 0;
        }
        if (wParam == TFY_TIMER_POLL)
        {
            Rebuild();
            for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
            {
                if (m_Rows[i].nType == 2 && m_Rows[i].bConnected && m_bConnecting)
                {
                    m_bConnecting = FALSE;
                    KillTimer(TFY_TIMER_POLL);
                }
            }
            if (!m_bConnecting)
                KillTimer(TFY_TIMER_POLL);
            Relayout();
            return 0;
        }
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

    int HitTest(POINT pt, int *pnBtn)
    {
        *pnBtn = 0;
        if (m_nIfaces > 0 && PtInRect(&m_rcRefresh, pt))
            return TFY_NETHIT_REFRESH;
        if (m_nIfaces > 0 && PtInRect(&m_rcAirplane, pt))
            return TFY_NETHIT_AIRPLANE;
        if (m_nIfaces > 0 && PtInRect(&m_rcWifi, pt))
            return TFY_NETHIT_WIFI;
        if (PtInRect(&m_rcLink, pt))
            return TFY_NETHIT_LINK;
        for (SIZE_T i = 0; i < m_Rows.GetCount(); i++)
        {
            TFYNETROW &row = m_Rows[i];
            if (!PtInRect(&row.rc, pt))
                continue;
            if ((int)i == m_iSel && row.nType == 2)
            {
                if (PtInRect(&row.rcButton, pt)) *pnBtn = 1;
                else if (PtInRect(&row.rcButton2, pt)) *pnBtn = 2;
                else if (PtInRect(&row.rcCheck, pt)) *pnBtn = 3;
                else if (PtInRect(&row.rcEdit, pt)) *pnBtn = 4;
            }
            return (int)i;
        }
        return TFY_NETHIT_NONE;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int nBtn = 0;
        int iHot = HitTest(pt, &nBtn);
        if (iHot != m_iHot || nBtn != m_iHotBtn)
        {
            m_iHot = iHot;
            m_iHotBtn = nBtn;
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
        if (m_iHot != TFY_NETHIT_NONE || m_iHotBtn)
        {
            m_iHot = TFY_NETHIT_NONE;
            m_iHotBtn = 0;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int nBtn = 0;
        int i = HitTest(pt, &nBtn);
        if (i == TFY_NETHIT_LINK)
        {
            ShellExecuteW(NULL, NULL, L"ncpa.cpl", NULL, NULL, SW_SHOWNORMAL);
            FadeOut();
            return 0;
        }
        if (i == TFY_NETHIT_REFRESH)
        {
            if (!m_bScanning)
            {
                StartScan();
                Relayout();
            }
            return 0;
        }
        if (i == TFY_NETHIT_AIRPLANE)
        {
            ToggleAirplane();
            return 0;
        }
        if (i == TFY_NETHIT_WIFI)
        {
            if (!m_bAirplane)
                ToggleWifi();
            return 0;
        }
        if (i < 0)
            return 0;
        TFYNETROW &row = m_Rows[i];
        if (row.nType != 2)
        {
            ShellExecuteW(NULL, NULL, L"ncpa.cpl", NULL, NULL, SW_SHOWNORMAL);
            FadeOut();
            return 0;
        }
        if (i == m_iSel)
        {
            if (nBtn == 1)
            {
                if (m_bPassMode)
                    SubmitPassword();
                else if (row.bConnected)
                    DisconnectRow(i);
                else
                    ConnectRow(i, NULL);
                return 0;
            }
            if (nBtn == 2)
            {
                CancelPassword();
                return 0;
            }
            if (nBtn == 3)
            {
                m_bAutoConnect = !m_bAutoConnect;
                InvalidateRect(NULL, FALSE);
                return 0;
            }
            if (nBtn == 4)
            {
                if (m_hwndEdit)
                    ::SetFocus(m_hwndEdit);
                return 0;
            }
        }
        SelectRow(i);
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
        {
            if (m_bPassMode)
                CancelPassword();
            else
                FadeOut();
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            if (m_bPassMode)
                SubmitPassword();
            else if (m_iSel >= 0 && m_Rows[m_iSel].nType == 2)
            {
                if (m_Rows[m_iSel].bConnected)
                    DisconnectRow(m_iSel);
                else
                    ConnectRow(m_iSel, NULL);
            }
            return 0;
        }
        if (wParam == VK_DOWN || wParam == VK_UP)
        {
            int n = (int)m_Rows.GetCount();
            if (n == 0)
                return 0;
            int i = m_iSel;
            do
            {
                i = (wParam == VK_DOWN) ? i + 1 : i - 1;
                if (i < 0) i = n - 1;
                if (i >= n) i = 0;
            } while (m_Rows[i].nType != 2 && i != m_iSel && m_iSel >= 0);
            if (m_Rows[i].nType == 2 && i != m_iSel)
                SelectRow(i);
            return 0;
        }
        if (wParam == VK_F5 && !m_bScanning)
        {
            StartScan();
            Relayout();
        }
        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        KillTimer(TFY_TIMER_ANIM);
        KillTimer(TFY_TIMER_SCAN);
        KillTimer(TFY_TIMER_POLL);
        UnloadWlan();
        if (m_hwndEdit)
        {
            ::DestroyWindow(m_hwndEdit);
            m_hwndEdit = NULL;
        }
        if (m_hbrEdit) { DeleteObject(m_hbrEdit); m_hbrEdit = NULL; }
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
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        MESSAGE_HANDLER(TFY_WM_WLANNOTIFY, OnWlanNotify)
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
    HideEdit();
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

typedef BOOLEAN (CALLBACK *TFY_PWRSCHEMESENUMPROC)(UINT, DWORD, LPWSTR, DWORD, LPWSTR, PVOID, LPARAM);
typedef BOOLEAN (WINAPI *PFN_ENUMPWRSCHEMES)(TFY_PWRSCHEMESENUMPROC, LPARAM);
typedef BOOLEAN (WINAPI *PFN_GETACTIVEPWRSCHEME)(PUINT);
typedef BOOLEAN (WINAPI *PFN_SETACTIVEPWRSCHEME)(UINT, PVOID, PVOID);

static HMODULE g_hPowrProf = NULL;
static PFN_ENUMPWRSCHEMES g_pfnEnumPwrSchemes = NULL;
static PFN_GETACTIVEPWRSCHEME g_pfnGetActivePwrScheme = NULL;
static PFN_SETACTIVEPWRSCHEME g_pfnSetActivePwrScheme = NULL;

static BOOL TfyLoadPowrProf()
{
    if (g_hPowrProf)
        return g_pfnEnumPwrSchemes && g_pfnGetActivePwrScheme && g_pfnSetActivePwrScheme;
    g_hPowrProf = LoadLibraryW(L"powrprof.dll");
    if (!g_hPowrProf)
        return FALSE;
    g_pfnEnumPwrSchemes = (PFN_ENUMPWRSCHEMES)GetProcAddress(g_hPowrProf, "EnumPwrSchemes");
    g_pfnGetActivePwrScheme = (PFN_GETACTIVEPWRSCHEME)GetProcAddress(g_hPowrProf, "GetActivePwrScheme");
    g_pfnSetActivePwrScheme = (PFN_SETACTIVEPWRSCHEME)GetProcAddress(g_hPowrProf, "SetActivePwrScheme");
    return g_pfnEnumPwrSchemes && g_pfnGetActivePwrScheme && g_pfnSetActivePwrScheme;
}

struct TFYPWRSCHEME
{
    UINT uiIndex;
    WCHAR szName[128];
    WCHAR szDesc[256];
    RECT rc;
};

#define TFY_PWRHIT_NONE  -1
#define TFY_PWRHIT_LINK  -2

class CTrayPowerWnd :
    public CWindowImpl<CTrayPowerWnd, CWindow, CTrayFlyoutTraits>
{
public:
    DECLARE_WND_CLASS_EX(L"TrayPowerFlyout", CS_DROPSHADOW, COLOR_WINDOW)

    SM2_FLYOUT_PALETTE m_Pal;
    HFONT m_hFontHeader;
    HFONT m_hFont;
    HFONT m_hFontSmall;
    CAtlArray<TFYPWRSCHEME> m_Schemes;
    UINT m_uiActive;
    BOOL m_bHaveActive;
    SYSTEM_POWER_STATUS m_sps;
    int m_iHot;
    BOOL m_bTracking;
    int m_AnimPhase;
    ULONGLONG m_AnimT0;
    POINT m_ptFinal;
    SIZE m_size;
    RECT m_rcIcon, m_rcHeader, m_rcSub, m_rcPlanLabel, m_rcLink;

    CTrayPowerWnd() : m_hFontHeader(NULL), m_hFont(NULL), m_hFontSmall(NULL),
                      m_uiActive(0), m_bHaveActive(FALSE), m_iHot(TFY_PWRHIT_NONE),
                      m_bTracking(FALSE), m_AnimPhase(TFY_NONE), m_AnimT0(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_sps, sizeof(m_sps));
        ZeroMemory(&m_ptFinal, sizeof(m_ptFinal));
        ZeroMemory(&m_size, sizeof(m_size));
    }

    int Sc(int v) const { return ShellScaleForDpi(v); }

    static BOOLEAN CALLBACK SchemeEnumProc(UINT uiIndex, DWORD dwName, LPWSTR sName,
                                           DWORD dwDesc, LPWSTR sDesc, PVOID pp, LPARAM lParam)
    {
        CTrayPowerWnd *pThis = (CTrayPowerWnd *)lParam;
        TFYPWRSCHEME scheme;
        if (!pThis || pThis->m_Schemes.GetCount() >= 8)
            return TRUE;
        ZeroMemory(&scheme, sizeof(scheme));
        scheme.uiIndex = uiIndex;
        if (sName)
            StringCchCopyW(scheme.szName, _countof(scheme.szName), sName);
        if (sDesc)
            StringCchCopyW(scheme.szDesc, _countof(scheme.szDesc), sDesc);
        pThis->m_Schemes.Add(scheme);
        return TRUE;
    }

    VOID Build()
    {
        ZeroMemory(&m_sps, sizeof(m_sps));
        if (!GetSystemPowerStatus(&m_sps))
        {
            m_sps.ACLineStatus = 255;
            m_sps.BatteryFlag = 255;
            m_sps.BatteryLifePercent = 255;
            m_sps.BatteryLifeTime = (DWORD)-1;
        }
        m_Schemes.SetCount(0);
        m_bHaveActive = FALSE;
        if (TfyLoadPowrProf())
        {
            g_pfnEnumPwrSchemes(SchemeEnumProc, (LPARAM)this);
            m_bHaveActive = g_pfnGetActivePwrScheme(&m_uiActive);
        }
    }

    BOOL HasBattery() const
    {
        return !(m_sps.BatteryFlag & 128) && m_sps.BatteryFlag != 255 && m_sps.BatteryLifePercent != 255;
    }

    UINT BatteryIcon() const
    {
        if (!HasBattery())
            return IDI_FLU_PLUG;
        if (m_sps.BatteryFlag & 8)
            return IDI_FLU_BATTCHG;
        int p = m_sps.BatteryLifePercent;
        if (p <= 12) return IDI_FLU_BATT0;
        if (p <= 37) return IDI_FLU_BATT1;
        if (p <= 62) return IDI_FLU_BATT2;
        if (p <= 87) return IDI_FLU_BATT3;
        return IDI_FLU_BATT4;
    }

    VOID HeaderText(LPWSTR pszHeader, SIZE_T cchHeader, LPWSTR pszSub, SIZE_T cchSub) const
    {
        pszSub[0] = 0;
        if (!HasBattery())
        {
            StringCchCopyW(pszHeader, cchHeader, L"No battery is detected");
            StringCchCopyW(pszSub, cchSub, m_sps.ACLineStatus == 1 ? L"Plugged in" : L"");
            return;
        }
        int p = m_sps.BatteryLifePercent;
        if (m_sps.ACLineStatus == 1)
        {
            if (p >= 100)
                StringCchCopyW(pszHeader, cchHeader, L"Fully charged (100%)");
            else if (m_sps.BatteryFlag & 8)
                StringCchPrintfW(pszHeader, cchHeader, L"%d%% available (plugged in, charging)", p);
            else
                StringCchPrintfW(pszHeader, cchHeader, L"%d%% available (plugged in, not charging)", p);
        }
        else
        {
            StringCchPrintfW(pszHeader, cchHeader, L"%d%% remaining", p);
            if (m_sps.BatteryLifeTime != (DWORD)-1)
            {
                DWORD mins = m_sps.BatteryLifeTime / 60;
                if (mins >= 60)
                    StringCchPrintfW(pszSub, cchSub, L"%u hr %02u min remaining", mins / 60, mins % 60);
                else
                    StringCchPrintfW(pszSub, cchSub, L"%u min remaining", mins);
            }
        }
    }

    VOID Layout()
    {
        int pad = Sc(12);
        int y = pad;
        m_size.cx = Sc(300);
        SetRect(&m_rcIcon, pad, y, pad + Sc(44), y + Sc(44));
        SetRect(&m_rcHeader, pad + Sc(56), y, m_size.cx - pad, y + Sc(24));
        SetRect(&m_rcSub, pad + Sc(56), y + Sc(24), m_size.cx - pad, y + Sc(44));
        y += Sc(44) + Sc(12);
        if (m_Schemes.GetCount())
        {
            y += Sc(8);
            SetRect(&m_rcPlanLabel, pad, y, m_size.cx - pad, y + Sc(20));
            y += Sc(22);
            for (SIZE_T i = 0; i < m_Schemes.GetCount(); i++)
            {
                SetRect(&m_Schemes[i].rc, Sc(8), y, m_size.cx - Sc(8), y + Sc(28));
                y += Sc(28);
            }
            y += Sc(8);
        }
        else
        {
            SetRectEmpty(&m_rcPlanLabel);
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
        if (!m_hFontHeader) m_hFontHeader = TfyCreateFont(13, FW_SEMIBOLD);
        if (!m_hFont) m_hFont = TfyCreateFont(12, FW_NORMAL);
        if (!m_hFontSmall) m_hFontSmall = TfyCreateFont(11, FW_NORMAL);

        Build();
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
        SetTimer(TFY_TIMER_TICK, 2000, NULL);
        InvalidateRect(NULL, FALSE);
    }

    VOID FadeOut();

    VOID DrawRadio(HDC hdc, const RECT *prc, BOOL bChecked, BOOL bHot)
    {
        RECT rcDot = { prc->left + Sc(10), (prc->top + prc->bottom) / 2 - Sc(7),
                       prc->left + Sc(24), (prc->top + prc->bottom) / 2 + Sc(7) };
        TFYAA aa;
        int ss = 3;
        HDC hdcAA = TfyAABegin(&aa, hdc, &rcDot, ss, bHot ? TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130) : m_Pal.PanelBg);
        int w = (rcDot.right - rcDot.left) * ss;
        int h = (rcDot.bottom - rcDot.top) * ss;
        HPEN hpen = CreatePen(PS_SOLID, ss, bChecked ? m_Pal.AccentBg : m_Pal.DimText);
        HGDIOBJ hpenOld = SelectObject(hdcAA, hpen);
        HGDIOBJ hbrOld = SelectObject(hdcAA, GetStockObject(NULL_BRUSH));
        Ellipse(hdcAA, ss, ss, w - ss, h - ss);
        SelectObject(hdcAA, hbrOld);
        SelectObject(hdcAA, hpenOld);
        DeleteObject(hpen);
        if (bChecked)
        {
            HBRUSH hbr = CreateSolidBrush(m_Pal.AccentBg);
            hbrOld = SelectObject(hdcAA, hbr);
            hpenOld = SelectObject(hdcAA, GetStockObject(NULL_PEN));
            Ellipse(hdcAA, w / 2 - Sc(3) * ss, h / 2 - Sc(3) * ss, w / 2 + Sc(3) * ss, h / 2 + Sc(3) * ss);
            SelectObject(hdcAA, hpenOld);
            SelectObject(hdcAA, hbrOld);
            DeleteObject(hbr);
        }
        TfyAAEnd(&aa, hdc);
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

        WCHAR szHeader[128], szSub[128];
        HeaderText(szHeader, _countof(szHeader), szSub, _countof(szSub));
        TfyDrawFluent(hdcMem, &m_rcIcon, BatteryIcon());
        HGDIOBJ hFontOld = SelectObject(hdcMem, m_hFontHeader);
        SetTextColor(hdcMem, m_Pal.PanelText);
        DrawTextW(hdcMem, szHeader, -1, &m_rcHeader, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdcMem, m_hFontSmall);
        SetTextColor(hdcMem, m_Pal.DimText);
        DrawTextW(hdcMem, szSub, -1, &m_rcSub, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        HBRUSH hbrSep = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.DimText, 90));
        RECT rcSep = { Sc(12), m_rcIcon.bottom + Sc(12), rc.right - Sc(12), m_rcIcon.bottom + Sc(13) };
        FillRect(hdcMem, &rcSep, hbrSep);

        if (m_Schemes.GetCount())
        {
            SelectObject(hdcMem, m_hFontSmall);
            SetTextColor(hdcMem, m_Pal.DimText);
            DrawTextW(hdcMem, L"Select a power plan:", -1, &m_rcPlanLabel, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            for (SIZE_T i = 0; i < m_Schemes.GetCount(); i++)
            {
                TFYPWRSCHEME &scheme = m_Schemes[i];
                BOOL bHot = (m_iHot == (int)i);
                BOOL bChecked = m_bHaveActive && scheme.uiIndex == m_uiActive;
                if (bHot)
                {
                    HBRUSH hbrHot = CreateSolidBrush(TfyMix(m_Pal.PanelBg, m_Pal.HotFill, 130));
                    FillRect(hdcMem, &scheme.rc, hbrHot);
                    DeleteObject(hbrHot);
                }
                DrawRadio(hdcMem, &scheme.rc, bChecked, bHot);
                RECT rcText = { scheme.rc.left + Sc(34), scheme.rc.top, scheme.rc.right - Sc(8), scheme.rc.bottom };
                SelectObject(hdcMem, m_hFont);
                SetTextColor(hdcMem, m_Pal.PanelText);
                DrawTextW(hdcMem, scheme.szName, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            RECT rcSep2 = { Sc(12), m_rcLink.top - Sc(9), rc.right - Sc(12), m_rcLink.top - Sc(8) };
            FillRect(hdcMem, &rcSep2, hbrSep);
        }
        DeleteObject(hbrSep);

        {
            int cyLink = (m_rcLink.top + m_rcLink.bottom) / 2;
            RECT rcGear = { m_rcLink.left + Sc(4), cyLink - Sc(8), m_rcLink.left + Sc(20), cyLink + Sc(8) };
            TfyDrawFluent(hdcMem, &rcGear, IDI_FLU_SETTINGS);
        }
        RECT rcLinkText = m_rcLink;
        rcLinkText.left += Sc(26);
        SelectObject(hdcMem, m_hFontSmall);
        SetTextColor(hdcMem, m_iHot == TFY_PWRHIT_LINK ? m_Pal.HotBorder : m_Pal.DimText);
        DrawTextW(hdcMem, L"More power options", -1, &rcLinkText, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

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

    int HitTest(POINT pt) const
    {
        if (PtInRect(&m_rcLink, pt))
            return TFY_PWRHIT_LINK;
        for (SIZE_T i = 0; i < m_Schemes.GetCount(); i++)
            if (PtInRect(&m_Schemes[i].rc, pt))
                return (int)i;
        return TFY_PWRHIT_NONE;
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
        if (m_iHot != TFY_PWRHIT_NONE)
        {
            m_iHot = TFY_PWRHIT_NONE;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int i = HitTest(pt);
        if (i == TFY_PWRHIT_LINK)
        {
            ShellExecuteW(NULL, L"open", L"control.exe", L"powercfg.cpl", NULL, SW_SHOWNORMAL);
            FadeOut();
            return 0;
        }
        if (i >= 0 && i < (int)m_Schemes.GetCount())
        {
            if (TfyLoadPowrProf() && g_pfnSetActivePwrScheme(m_Schemes[i].uiIndex, NULL, NULL))
            {
                m_uiActive = m_Schemes[i].uiIndex;
                m_bHaveActive = TRUE;
            }
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == TFY_TIMER_TICK)
        {
            SYSTEM_POWER_STATUS sps;
            if (GetSystemPowerStatus(&sps) && memcmp(&sps, &m_sps, sizeof(sps)))
            {
                m_sps = sps;
                InvalidateRect(NULL, FALSE);
            }
            return 0;
        }
        if (wParam != TFY_TIMER_ANIM)
            return 0;
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
        if (m_hFontHeader) { DeleteObject(m_hFontHeader); m_hFontHeader = NULL; }
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall); m_hFontSmall = NULL; }
        return 0;
    }

    virtual void OnFinalMessage(HWND hWnd) override;

    BEGIN_MSG_MAP(CTrayPowerWnd)
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

static CTrayPowerWnd *g_pTrayPower = NULL;
static ULONGLONG g_PowerDismissTick = 0;

void CTrayPowerWnd::OnFinalMessage(HWND hWnd)
{
    if (g_pTrayPower == this)
        g_pTrayPower = NULL;
    delete this;
}

VOID CTrayPowerWnd::FadeOut()
{
    if (m_AnimPhase == TFY_CLOSE)
        return;
    g_PowerDismissTick = GetTickCount64();
    m_AnimPhase = TFY_CLOSE;
    m_AnimT0 = g_PowerDismissTick;
    SetTimer(TFY_TIMER_ANIM, 16, NULL);
}

VOID TrayPower_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor)
{
    if (GetTickCount64() - g_PowerDismissTick < 350)
        return;

    if (g_pTrayPower && g_pTrayPower->IsWindow())
    {
        if (g_pTrayPower->IsWindowVisible() && g_pTrayPower->m_AnimPhase != TFY_CLOSE)
        {
            g_pTrayPower->FadeOut();
            return;
        }
        g_pTrayPower->DestroyWindow();
        g_pTrayPower = NULL;
    }

    CTrayPowerWnd *pPower = new CTrayPowerWnd();
    if (!pPower)
        return;
    if (!pPower->Create(hwndOwner, CWindow::rcDefault, NULL))
    {
        delete pPower;
        return;
    }
    g_pTrayPower = pPower;
    pPower->Toggle(hwndOwner, prcAnchor);
}

VOID TrayFlyoutsAux_Destroy(VOID)
{
    if (g_pTrayPower && g_pTrayPower->IsWindow())
        g_pTrayPower->DestroyWindow();
    g_pTrayPower = NULL;
    if (g_pTrayVolume && g_pTrayVolume->IsWindow())
        g_pTrayVolume->DestroyWindow();
    g_pTrayVolume = NULL;
    if (g_pTrayNetwork && g_pTrayNetwork->IsWindow())
        g_pTrayNetwork->DestroyWindow();
    g_pTrayNetwork = NULL;
}
