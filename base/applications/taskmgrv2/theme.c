/*
 * theme.c — Win11 light/dark color tables, fonts, DPI helpers,
 *            GDI+ init/shutdown, fluent geometry.
 */
#include "precomp.h"

/* -----------------------------------------------------------------------
 * GDI+ flat-C types we need (avoid pulling in gdiplus.h C++ header).
 * ReactOS ships the GDI+ flat API in gdiplus.dll; we import it.
 * ----------------------------------------------------------------------- */
typedef float REAL;
typedef DWORD GpStatus;
typedef struct GdiplusStartupInput {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GdiplusStartupInput;

/* Imported from gdiplus via the import lib */
GpStatus WINAPI GdiplusStartup(ULONG_PTR *token,
                                const GdiplusStartupInput *input,
                                void *output);
void     WINAPI GdiplusShutdown(ULONG_PTR token);

/* GDI+ flat API for drawing (declarations only needed here) */
typedef void GpGraphics;
typedef void GpBrush;
typedef void GpPen;
typedef void GpPath;

GpStatus WINAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
GpStatus WINAPI GdipDeleteGraphics(GpGraphics *graphics);
GpStatus WINAPI GdipCreateSolidFill(DWORD argb, GpBrush **brush);
GpStatus WINAPI GdipDeleteBrush(GpBrush *brush);
GpStatus WINAPI GdipFillRoundedRectangleI(GpGraphics *graphics, GpBrush *brush,
                                           INT x, INT y, INT w, INT h, INT radius);
/* Note: GdipFillRoundedRectangleI may not exist in all versions; we use path approach */
GpStatus WINAPI GdipCreatePath(INT fillMode, GpPath **path);
GpStatus WINAPI GdipDeletePath(GpPath *path);
GpStatus WINAPI GdipAddPathArcI(GpPath *path, INT x, INT y, INT w, INT h,
                                 REAL startAngle, REAL sweepAngle);
GpStatus WINAPI GdipAddPathLineI(GpPath *path, INT x1, INT y1, INT x2, INT y2);
GpStatus WINAPI GdipClosePathFigure(GpPath *path);
GpStatus WINAPI GdipFillPath(GpGraphics *graphics, GpBrush *brush, GpPath *path);
GpStatus WINAPI GdipSetSmoothingMode(GpGraphics *graphics, INT smoothingMode);

/* -----------------------------------------------------------------------
 * Color tables
 * ----------------------------------------------------------------------- */
static const MODERN_THEME s_lightTheme = {
    RGB(243, 243, 243),  /* clrBg */
    RGB(250, 250, 250),  /* clrBgAlt */
    RGB( 28,  28,  28),  /* clrText */
    RGB( 91,  91,  91),  /* clrTextDim */
    RGB(  0, 120, 212),  /* clrAccent */
    RGB(255, 255, 255),  /* clrAccentText */
    RGB(229, 229, 229),  /* clrDivider */
    RGB(  0, 120, 212),  /* clrGraphLine */
    RGB(  0, 120, 212),  /* clrGraphFill */
    RGB( 27,  58,  87),  /* clrGraphKernel */
    RGB(224, 224, 224),  /* clrGraphGrid */
    RGB(214, 214, 214),  /* clrCardBorder */
    64                   /* graphFillAlpha */
};

static const MODERN_THEME s_darkTheme = {
    RGB( 32,  32,  32),  /* clrBg */
    RGB( 43,  43,  43),  /* clrBgAlt */
    RGB(242, 242, 242),  /* clrText */
    RGB(166, 166, 166),  /* clrTextDim */
    RGB( 96, 205, 255),  /* clrAccent */
    RGB(  0,   0,   0),  /* clrAccentText */
    RGB( 58,  58,  58),  /* clrDivider */
    RGB( 96, 205, 255),  /* clrGraphLine */
    RGB( 96, 205, 255),  /* clrGraphFill */
    RGB(158, 230, 255),  /* clrGraphKernel */
    RGB( 58,  58,  58),  /* clrGraphGrid */
    RGB( 63,  63,  63),  /* clrCardBorder */
    64                   /* graphFillAlpha */
};

/* -----------------------------------------------------------------------
 * Module state
 * ----------------------------------------------------------------------- */
static ULONG_PTR  s_gdipToken   = 0;
static BOOL       s_bDark       = FALSE;
static UINT       s_dpi         = 96;

/* Cached GDI objects — recreated on theme change */
static HBRUSH     s_hBrushBg    = NULL;
static HBRUSH     s_hBrushBgAlt = NULL;
static HBRUSH     s_hBrushAccent= NULL;
static HPEN       s_hPenDivider = NULL;
static HPEN       s_hPenGraph   = NULL;
static HPEN       s_hPenKernel  = NULL;

/* Cached fonts */
static HFONT      s_hFontUI        = NULL;
static HFONT      s_hFontUISmall   = NULL;
static HFONT      s_hFontTitle     = NULL;
static HFONT      s_hFontHeading   = NULL;
static HFONT      s_hFontMono      = NULL;
static HFONT      s_hFontStatNum   = NULL;

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */
static void DeleteCachedObjects(void)
{
    if (s_hBrushBg)     { DeleteObject(s_hBrushBg);     s_hBrushBg     = NULL; }
    if (s_hBrushBgAlt)  { DeleteObject(s_hBrushBgAlt);  s_hBrushBgAlt  = NULL; }
    if (s_hBrushAccent) { DeleteObject(s_hBrushAccent);  s_hBrushAccent = NULL; }
    if (s_hPenDivider)  { DeleteObject(s_hPenDivider);   s_hPenDivider  = NULL; }
    if (s_hPenGraph)    { DeleteObject(s_hPenGraph);     s_hPenGraph    = NULL; }
    if (s_hPenKernel)   { DeleteObject(s_hPenKernel);    s_hPenKernel   = NULL; }
}

static void DeleteCachedFonts(void)
{
    if (s_hFontUI)      { DeleteObject(s_hFontUI);      s_hFontUI      = NULL; }
    if (s_hFontUISmall) { DeleteObject(s_hFontUISmall); s_hFontUISmall = NULL; }
    if (s_hFontTitle)   { DeleteObject(s_hFontTitle);   s_hFontTitle   = NULL; }
    if (s_hFontHeading) { DeleteObject(s_hFontHeading); s_hFontHeading = NULL; }
    if (s_hFontMono)    { DeleteObject(s_hFontMono);    s_hFontMono    = NULL; }
    if (s_hFontStatNum) { DeleteObject(s_hFontStatNum); s_hFontStatNum = NULL; }
}

/* Convert point size to LOGFONT lfHeight (negative = character height). */
static int PointsToHeight(int pts)
{
    /* Use screen DC for accurate conversion */
    HDC hdc = GetDC(NULL);
    int h = -MulDiv(pts, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    return h;
}

/* Try each face name in turn; return the first that EnumFontFamiliesEx finds. */
typedef struct {
    BOOL found;
} ENUM_FONT_CTX;

static int CALLBACK EnumFontProc(const LOGFONTW *lf, const TEXTMETRICW *tm,
                                  DWORD fontType, LPARAM lParam)
{
    ENUM_FONT_CTX *ctx = (ENUM_FONT_CTX *)lParam;
    ctx->found = TRUE;
    return 0; /* stop enumeration */
}

static BOOL FontExists(const WCHAR *faceName)
{
    HDC hdc = GetDC(NULL);
    LOGFONTW lf;
    ENUM_FONT_CTX ctx = { FALSE };
    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, faceName, LF_FACESIZE);
    EnumFontFamiliesExW(hdc, &lf, EnumFontProc, (LPARAM)&ctx, 0);
    ReleaseDC(NULL, hdc);
    return ctx.found;
}

static HFONT CreateFont_Helper(int pts, int weight, BOOL italic,
                                const WCHAR *face1, const WCHAR *face2)
{
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight  = PointsToHeight(pts);
    lf.lfWeight  = weight;
    lf.lfItalic  = (BYTE)italic;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;

    if (face1 && FontExists(face1))
        lstrcpynW(lf.lfFaceName, face1, LF_FACESIZE);
    else if (face2)
        lstrcpynW(lf.lfFaceName, face2, LF_FACESIZE);

    return CreateFontIndirectW(&lf);
}

static void CreateCachedFonts(void)
{
    s_hFontUI      = CreateFont_Helper(9,  FW_NORMAL, FALSE,
                                        L"Segoe UI Variable Display", L"Segoe UI");
    s_hFontUISmall = CreateFont_Helper(8,  FW_NORMAL, FALSE,
                                        L"Segoe UI Variable Display", L"Segoe UI");
    s_hFontTitle   = CreateFont_Helper(14, FW_SEMIBOLD, FALSE,
                                        L"Segoe UI Variable Display", L"Segoe UI");
    s_hFontHeading = CreateFont_Helper(18, FW_SEMIBOLD, FALSE,
                                        L"Segoe UI Variable Display", L"Segoe UI");
    s_hFontMono    = CreateFont_Helper(9,  FW_NORMAL, FALSE,
                                        L"Cascadia Mono", L"Consolas");
    s_hFontStatNum = CreateFont_Helper(24, FW_LIGHT, FALSE,
                                        L"Segoe UI Variable Display", L"Segoe UI");
}

static void CreateCachedBrushes(void)
{
    const MODERN_THEME *t = Theme_Current();
    s_hBrushBg     = CreateSolidBrush(t->clrBg);
    s_hBrushBgAlt  = CreateSolidBrush(t->clrBgAlt);
    s_hBrushAccent = CreateSolidBrush(t->clrAccent);
    s_hPenDivider  = CreatePen(PS_SOLID, 1, t->clrDivider);
    s_hPenGraph    = CreatePen(PS_SOLID, 1, t->clrGraphLine);
    s_hPenKernel   = CreatePen(PS_SOLID, 1, t->clrGraphKernel);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
BOOL Theme_Init(void)
{
    GdiplusStartupInput gsi;
    ZeroMemory(&gsi, sizeof(gsi));
    gsi.GdiplusVersion = 1;

    if (GdiplusStartup(&s_gdipToken, &gsi, NULL) != 0)
        return FALSE;

    CreateCachedFonts();
    CreateCachedBrushes();
    return TRUE;
}

void Theme_Shutdown(void)
{
    DeleteCachedObjects();
    DeleteCachedFonts();
    if (s_gdipToken)
    {
        GdiplusShutdown(s_gdipToken);
        s_gdipToken = 0;
    }
}

const MODERN_THEME *Theme_Current(void)
{
    return s_bDark ? &s_darkTheme : &s_lightTheme;
}

void Theme_SetDark(BOOL bDark)
{
    if ((bDark ? TRUE : FALSE) == s_bDark)
        return;
    s_bDark = bDark ? TRUE : FALSE;
    DeleteCachedObjects();
    CreateCachedBrushes();
}

BOOL Theme_IsDark(void)
{
    return s_bDark;
}

HFONT Theme_FontUI(void)        { return s_hFontUI; }
HFONT Theme_FontUISmall(void)   { return s_hFontUISmall; }
HFONT Theme_FontTitle(void)     { return s_hFontTitle; }
HFONT Theme_FontHeading(void)   { return s_hFontHeading; }
HFONT Theme_FontMono(void)      { return s_hFontMono; }
HFONT Theme_FontStatNumber(void){ return s_hFontStatNum; }

int Theme_DpiScale(int unscaledPx)
{
    return MulDiv(unscaledPx, (int)s_dpi, 96);
}

void Theme_SetShellDpi(UINT dpi)
{
    s_dpi = (dpi > 0) ? dpi : 96;
    /* Fonts need to be recreated for new DPI */
    DeleteCachedFonts();
    CreateCachedFonts();
}

UINT Theme_GetShellDpi(void)
{
    return s_dpi;
}

HBRUSH Theme_BrushBg(void)      { return s_hBrushBg; }
HBRUSH Theme_BrushBgAlt(void)   { return s_hBrushBgAlt; }
HBRUSH Theme_BrushAccent(void)  { return s_hBrushAccent; }
HPEN   Theme_PenDivider(void)   { return s_hPenDivider; }
HPEN   Theme_PenGraphLine(void) { return s_hPenGraph; }
HPEN   Theme_PenGraphKernel(void){ return s_hPenKernel; }

/* -----------------------------------------------------------------------
 * Rounded rectangle fill using GDI+ path
 * ----------------------------------------------------------------------- */
void Theme_FillRoundRect(HDC hdc, const RECT *rc, int radius, COLORREF clr)
{
    GpGraphics *g = NULL;
    GpBrush    *b = NULL;
    GpPath     *p = NULL;
    DWORD       argb;
    INT x, y, w, h, r;

    if (!hdc || !rc) return;

    x = rc->left;
    y = rc->top;
    w = rc->right  - rc->left;
    h = rc->bottom - rc->top;
    r = radius;

    /* COLORREF is BGR; GDI+ ARGB is 0xAARRGGBB */
    argb = 0xFF000000
         | ((clr & 0xFF) << 16)   /* R */
         | (((clr >> 8) & 0xFF) << 8)  /* G */
         | ((clr >> 16) & 0xFF);       /* B */

    if (GdipCreateFromHDC(hdc, &g) != 0) goto fallback;
    GdipSetSmoothingMode(g, 4 /* SmoothingModeAntiAlias */);

    if (GdipCreateSolidFill(argb, &b) != 0) goto fallback;
    if (GdipCreatePath(0 /* FillModeAlternate */, &p) != 0) goto fallback;

    /* Build rounded rect path: 4 arcs + 4 lines */
    GdipAddPathArcI(p, x,         y,         r*2, r*2, 180.0f, 90.0f);
    GdipAddPathArcI(p, x+w-r*2,  y,         r*2, r*2, 270.0f, 90.0f);
    GdipAddPathArcI(p, x+w-r*2,  y+h-r*2,  r*2, r*2,   0.0f, 90.0f);
    GdipAddPathArcI(p, x,         y+h-r*2,  r*2, r*2,  90.0f, 90.0f);
    GdipClosePathFigure(p);
    GdipFillPath(g, b, p);

    GdipDeletePath(p);
    GdipDeleteBrush(b);
    GdipDeleteGraphics(g);
    return;

fallback:
    /* GDI fallback: plain filled rect */
    {
        HBRUSH hbr = CreateSolidBrush(clr);
        FillRect(hdc, rc, hbr);
        DeleteObject(hbr);
    }
    if (p) GdipDeletePath(p);
    if (b) GdipDeleteBrush(b);
    if (g) GdipDeleteGraphics(g);
}

/* -----------------------------------------------------------------------
 * 1px hairline
 * ----------------------------------------------------------------------- */
void Theme_DrawHairline(HDC hdc, int x1, int y1, int x2, int y2)
{
    HPEN hOld = (HPEN)SelectObject(hdc, Theme_PenDivider());
    MoveToEx(hdc, x1, y1, NULL);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, hOld);
}

/* -----------------------------------------------------------------------
 * Broadcast theme change to all registered pages (called by shell).
 * The shell itself will call pfnTheme on each cached page HWND.
 * This stub exists so callers can call it without ifdefs.
 * ----------------------------------------------------------------------- */
void Theme_BroadcastChange(void)
{
    /* Shell handles broadcasting; nothing needed here */
}
