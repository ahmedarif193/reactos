/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Theme engine: Win11 light/dark palettes, fonts, DPI
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

Theme g_t;

int S(int px)
{
    return MulDiv(px, g_app.dpi, 96);
}

COLORREF Blend(COLORREF a, COLORREF b, int pctB)
{
    int pa = 100 - pctB;
    return RGB((GetRValue(a) * pa + GetRValue(b) * pctB) / 100,
               (GetGValue(a) * pa + GetGValue(b) * pctB) / 100,
               (GetBValue(a) * pa + GetBValue(b) * pctB) / 100);
}

BOOL Theme_SystemPrefersDark(void)
{
    /* Win10+ personalization key; absent on stock ReactOS -> light */
    HKEY hk;
    DWORD v = 1, cb = sizeof(v);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hk, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&v, &cb);
        RegCloseKey(hk);
    }
    return v == 0;
}

static HFONT MakeFont(int size, int weight, const WCHAR* face)
{
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(size, g_app.dpi, 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    StringCchCopyW(lf.lfFaceName, LF_FACESIZE, face);
    return CreateFontIndirectW(&lf);
}

static const WCHAR* PickFace(void)
{
    /* Try the Win11 face first, then Segoe UI, then whatever the
       system substitutes (ReactOS maps these in FontSubstitutes). */
    static WCHAR s_face[LF_FACESIZE];
    static BOOL s_done = FALSE;
    if (s_done) return s_face;

    const WCHAR* tries[] = { L"Segoe UI Variable Text", L"Segoe UI", L"Tahoma" };
    HDC dc = GetDC(NULL);
    StringCchCopyW(s_face, LF_FACESIZE, L"MS Shell Dlg 2");
    for (int i = 0; i < (int)_countof(tries); i++)
    {
        HFONT f = MakeFont(10, FW_NORMAL, tries[i]);
        if (!f) continue;
        HGDIOBJ old = SelectObject(dc, f);
        WCHAR got[LF_FACESIZE] = L"";
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, old);
        DeleteObject(f);
        /* GDI gives us *some* face even for unknown names; accept only when
           mapping resolved to a real installed family (non-empty). */
        if (got[0])
        {
            StringCchCopyW(s_face, LF_FACESIZE, tries[i]);
            /* exact hit? stop. Otherwise keep trying for a better one. */
            if (lstrcmpiW(got, tries[i]) == 0)
                break;
        }
    }
    s_done = TRUE;
    return s_face;
}

void Theme_Free(void)
{
    HFONT* fonts[] = { &g_t.fCaption, &g_t.fTitle, &g_t.fBody, &g_t.fBodySemi,
                       &g_t.fSmall, &g_t.fSmallSemi, &g_t.fMed, &g_t.fMedSemi, &g_t.fBig };
    for (int i = 0; i < (int)_countof(fonts); i++)
    {
        if (*fonts[i]) DeleteObject(*fonts[i]);
        *fonts[i] = NULL;
    }
}

void Theme_Apply(BOOL dark, int dpi)
{
    (void)dpi;
    Theme_Free();
    ZeroMemory(&g_t, sizeof(g_t));
    g_t.dark = dark;

    if (!dark)
    {
        g_t.winBg        = RGB(0xF3, 0xF3, 0xF3);
        g_t.captionText  = RGB(0x1B, 0x1B, 0x1B);
        g_t.cardBg       = RGB(0xFB, 0xFB, 0xFB);
        g_t.cardBorder   = RGB(0xE5, 0xE5, 0xE5);
        g_t.listBg       = RGB(0xFC, 0xFC, 0xFC);
        g_t.headerBg     = RGB(0xF3, 0xF3, 0xF3);
        g_t.hoverBg      = RGB(0xEA, 0xEA, 0xEA);
        g_t.selBg        = RGB(0xD5, 0xE4, 0xF2);
        g_t.railHover    = RGB(0xEA, 0xEA, 0xEA);
        g_t.railSel      = RGB(0xE6, 0xE6, 0xE6);
        g_t.textMain     = RGB(0x1B, 0x1B, 0x1B);
        g_t.textSec      = RGB(0x5D, 0x5D, 0x5D);
        g_t.textDis      = RGB(0xA0, 0xA0, 0xA0);
        g_t.accent       = RGB(0x00, 0x67, 0xC0);
        g_t.accentHover  = RGB(0x19, 0x75, 0xC5);
        g_t.accentPressed= RGB(0x31, 0x83, 0xCA);
        g_t.accentText   = RGB(0xFF, 0xFF, 0xFF);
        g_t.divider      = RGB(0xE5, 0xE5, 0xE5);
        g_t.inputBg      = RGB(0xFF, 0xFF, 0xFF);
        g_t.inputBorder  = RGB(0xD1, 0xD1, 0xD1);
        g_t.scrollThumb  = RGB(0xC5, 0xC5, 0xC5);
        g_t.scrollThumbHot = RGB(0xA6, 0xA6, 0xA6);
        g_t.closeHover   = RGB(0xC4, 0x2B, 0x1C);
        g_t.dangerText   = RGB(0xC4, 0x2B, 0x1C);
        g_t.graph[GR_CPU]  = RGB(0x11, 0x6D, 0xBB);
        g_t.graph[GR_MEM]  = RGB(0x8A, 0x51, 0xBF);
        g_t.graph[GR_DISK] = RGB(0x3E, 0x8E, 0x3E);
        g_t.graph[GR_NET]  = RGB(0xC0, 0x68, 0x21);
    }
    else
    {
        g_t.winBg        = RGB(0x20, 0x20, 0x20);
        g_t.captionText  = RGB(0xFF, 0xFF, 0xFF);
        g_t.cardBg       = RGB(0x2B, 0x2B, 0x2B);
        g_t.cardBorder   = RGB(0x1D, 0x1D, 0x1D);
        g_t.listBg       = RGB(0x24, 0x24, 0x24);
        g_t.headerBg     = RGB(0x20, 0x20, 0x20);
        g_t.hoverBg      = RGB(0x2D, 0x2D, 0x2D);
        g_t.selBg        = RGB(0x2C, 0x44, 0x59);
        g_t.railHover    = RGB(0x2D, 0x2D, 0x2D);
        g_t.railSel      = RGB(0x32, 0x32, 0x32);
        g_t.textMain     = RGB(0xFF, 0xFF, 0xFF);
        g_t.textSec      = RGB(0xC5, 0xC5, 0xC5);
        g_t.textDis      = RGB(0x71, 0x71, 0x71);
        g_t.accent       = RGB(0x4C, 0xC2, 0xFF);
        g_t.accentHover  = RGB(0x47, 0xB1, 0xE8);
        g_t.accentPressed= RGB(0x42, 0xA1, 0xD2);
        g_t.accentText   = RGB(0x00, 0x00, 0x00);
        g_t.divider      = RGB(0x3A, 0x3A, 0x3A);
        g_t.inputBg      = RGB(0x2D, 0x2D, 0x2D);
        g_t.inputBorder  = RGB(0x45, 0x45, 0x45);
        g_t.scrollThumb  = RGB(0x4D, 0x4D, 0x4D);
        g_t.scrollThumbHot = RGB(0x6A, 0x6A, 0x6A);
        g_t.closeHover   = RGB(0xC4, 0x2B, 0x1C);
        g_t.dangerText   = RGB(0xFF, 0x99, 0xA4);
        g_t.graph[GR_CPU]  = RGB(0x4F, 0xA3, 0xE3);
        g_t.graph[GR_MEM]  = RGB(0xB5, 0x86, 0xF0);
        g_t.graph[GR_DISK] = RGB(0x6C, 0xCB, 0x5F);
        g_t.graph[GR_NET]  = RGB(0xE8, 0x97, 0x5C);
    }

    const WCHAR* face = PickFace();
    g_t.fCaption   = MakeFont(9,  FW_NORMAL,   face);
    g_t.fTitle     = MakeFont(15, FW_SEMIBOLD, face);
    g_t.fBody      = MakeFont(10, FW_NORMAL,   face);
    g_t.fBodySemi  = MakeFont(10, FW_SEMIBOLD, face);
    g_t.fSmall     = MakeFont(8,  FW_NORMAL,   face);
    g_t.fSmallSemi = MakeFont(8,  FW_SEMIBOLD, face);
    g_t.fMed       = MakeFont(12, FW_NORMAL,   face);
    g_t.fMedSemi   = MakeFont(12, FW_SEMIBOLD, face);
    g_t.fBig       = MakeFont(20, FW_LIGHT,    face);
}

/* ------------------------------------------------------------------ */
/*  Heat map colors (Win11 taskmgr yellow->orange->red ramp)           */
/* ------------------------------------------------------------------ */

static COLORREF LerpC(COLORREF a, COLORREF b, double t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

COLORREF HeatBg(double t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    if (!g_t.dark)
    {
        /* very high utilization flips to alarm red */
        if (t >= 0.98) return RGB(0xE8, 0x11, 0x23);
        COLORREF c0 = RGB(0xFF, 0xFC, 0xF2);   /* near idle   */
        COLORREF c1 = RGB(0xFF, 0xE8, 0xA6);
        COLORREF c2 = RGB(0xFC, 0xB8, 0x2E);
        COLORREF c3 = RGB(0xF2, 0x71, 0x1E);   /* saturated   */
        if (t < 0.33) return LerpC(c0, c1, t / 0.33);
        if (t < 0.66) return LerpC(c1, c2, (t - 0.33) / 0.33);
        return LerpC(c2, c3, (t - 0.66) / 0.34);
    }
    else
    {
        if (t >= 0.98) return RGB(0x9E, 0x1B, 0x22);
        COLORREF c0 = RGB(0x33, 0x31, 0x25);
        COLORREF c1 = RGB(0x54, 0x49, 0x1E);
        COLORREF c2 = RGB(0x8A, 0x66, 0x16);
        COLORREF c3 = RGB(0xB4, 0x63, 0x11);
        if (t < 0.33) return LerpC(c0, c1, t / 0.33);
        if (t < 0.66) return LerpC(c1, c2, (t - 0.33) / 0.33);
        return LerpC(c2, c3, (t - 0.66) / 0.34);
    }
}

COLORREF HeatText(double t)
{
    if (t >= 0.98) return RGB(0xFF, 0xFF, 0xFF);
    return g_t.dark ? RGB(0xF5, 0xF0, 0xE0) : RGB(0x1B, 0x1B, 0x1B);
}
