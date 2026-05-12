#pragma once

/* -----------------------------------------------------------------------
 * theme.h — Win11 light/dark color tables, accent color, fonts,
 *            DPI scaling helpers, GDI+ init/shutdown,
 *            fluent geometry helpers (rounded rect, hairline).
 * ----------------------------------------------------------------------- */

typedef struct _MODERN_THEME {
    COLORREF clrBg;           /* page background */
    COLORREF clrBgAlt;        /* sidebar / card background */
    COLORREF clrText;         /* primary text */
    COLORREF clrTextDim;      /* secondary text */
    COLORREF clrAccent;       /* selection / accent pill */
    COLORREF clrAccentText;   /* text on accent */
    COLORREF clrDivider;      /* 1px hairline */
    COLORREF clrGraphLine;    /* CPU graph stroke */
    COLORREF clrGraphFill;    /* base color for the alpha gradient */
    COLORREF clrGraphKernel;  /* kernel-times overlay */
    COLORREF clrGraphGrid;    /* graph grid lines */
    COLORREF clrCardBorder;   /* mini-graph border */
    BYTE     graphFillAlpha;  /* 0..255 */
} MODERN_THEME;

BOOL               Theme_Init(void);
void               Theme_Shutdown(void);
const MODERN_THEME *Theme_Current(void);
void               Theme_SetDark(BOOL bDark);
BOOL               Theme_IsDark(void);

HFONT              Theme_FontUI(void);          /* Segoe UI Variable Display 9pt, fallback Segoe UI 9pt */
HFONT              Theme_FontUISmall(void);     /* 8pt */
HFONT              Theme_FontTitle(void);       /* 14pt SemiBold */
HFONT              Theme_FontHeading(void);     /* 18pt SemiBold */
HFONT              Theme_FontMono(void);        /* Cascadia Mono fallback Consolas 9pt */
HFONT              Theme_FontStatNumber(void);  /* 24pt Light — large stat numbers */

int                Theme_DpiScale(int unscaledPx);
void               Theme_SetShellDpi(UINT dpi);
UINT               Theme_GetShellDpi(void);

HBRUSH             Theme_BrushBg(void);
HBRUSH             Theme_BrushBgAlt(void);
HBRUSH             Theme_BrushAccent(void);
HPEN               Theme_PenDivider(void);
HPEN               Theme_PenGraphLine(void);
HPEN               Theme_PenGraphKernel(void);

void               Theme_FillRoundRect(HDC hdc, const RECT *rc, int radius, COLORREF clr);
void               Theme_DrawHairline(HDC hdc, int x1, int y1, int x2, int y2);

/* Notify all open pages that the theme has changed — called by shell. */
void               Theme_BroadcastChange(void);
