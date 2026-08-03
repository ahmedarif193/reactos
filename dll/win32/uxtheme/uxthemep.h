#ifndef _UXTHEME_PCH_
#define _UXTHEME_PCH_

#include <stdarg.h>

#include "resource.h"

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winnls.h>
#include <windowsx.h>
#include <undocuser.h>
#include <undocgdi.h>
#include <uxtheme.h>
#include <uxundoc.h>
#include <vfwmsgs.h>
#include <tmschema.h>

#define NTOS_MODE_USER
#include <ndk/ntndk.h>
#include <ndk/rtltypes.h>

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(uxtheme);

#include "msstyles.h"
#include "uxthemedll.h"

typedef struct tagTMERRINFO
{
    UINT nID;
    WCHAR szParam1[MAX_PATH];
    WCHAR szParam2[MAX_PATH];
    WCHAR szFile[MAX_PATH];
    WCHAR szLine[MAX_PATH];
    INT nLineNo;
} TMERRINFO, *PTMERRINFO;

HRESULT UXTHEME_LoadImage(HTHEME hTheme, int part_id, int state_id, const RECT *rect, BOOL glyph,
                          HBITMAP *bitmap, RECT *bitmap_rect, BOOL *has_alpha,
                          BOOL *has_default_transparent_colour, int *image_dpi);

HRESULT MSSTYLES_ReferenceTheme(PTHEME_FILE tf);
void MSSTYLES_ParseThemeIni(PTHEME_FILE tf);
#ifdef ENABLE_PNG_SUPPORT
EXTERN_C
BOOL
MSSTYLES_TryLoadPng(
    _In_ HINSTANCE hTheme,
    _In_ LPCWSTR szFile,
    _In_ LPCWSTR type,
    _Out_ HBITMAP *phBitmap);
EXTERN_C
BOOL
prepare_png_alpha(
    _In_ HBITMAP png,
    _Out_ BOOL* hasAlpha);
#endif /* ENABLE_PNG_SUPPORT */
/* The window context stores data for the window needed through the life of the window */
typedef struct _WND_DATA
{
    HTHEME hthemeWindow;
    HTHEME hthemeScrollbar;

    RECT rcCaptionButtons[4];
    UINT lastHitTest;
    BOOL HasAppDefinedRgn;
    BOOL HasThemeRgn;
    BOOL UpdatingRgn;
    BOOL DirtyThemeRegion;
    HBRUSH hTabBackgroundBrush;
    HBITMAP hTabBackgroundBmp;

    BOOL SCROLL_trackVertical;
    enum SCROLL_HITTEST SCROLL_trackHitTest;
    BOOL SCROLL_MovingThumb;  /* Is the moving thumb being displayed? */
    HWND SCROLL_TrackingWin;
    INT  SCROLL_TrackingBar;
    INT  SCROLL_TrackingPos;
    INT  SCROLL_TrackingVal;
} WND_DATA, *PWND_DATA;

/* The draw context stores data that are needed by the drawing operations in the non client area of the window */
typedef struct _DRAW_CONTEXT
{
    HWND hWnd;
    HDC hDC;
    HTHEME theme;
    HTHEME scrolltheme;
    HTHEME hPrevTheme;
    WINDOWINFO wi;
    BOOL Active; /* wi.dwWindowStatus isn't correct for mdi child windows */
    HRGN hRgn;
    int CaptionHeight;

    /* for double buffering */
    HDC hDCScreen;
    HBITMAP hbmpOld;
} DRAW_CONTEXT, *PDRAW_CONTEXT;

typedef enum
{
    CLOSEBUTTON,
    MAXBUTTON,
    MINBUTTON,
    HELPBUTTON
} CAPTIONBUTTON;

/*
The following values specify all possible button states
Note that not all of them are documented but it is easy to
find them by opening a theme file
*/
typedef enum {
    BUTTON_NORMAL = 1 ,
    BUTTON_HOT ,
    BUTTON_PRESSED ,
    BUTTON_DISABLED ,
    BUTTON_INACTIVE ,
    BUTTON_INACTIVE_HOT ,
    BUTTON_INACTIVE_PRESSED ,
    BUTTON_INACTIVE_DISABLED
} THEME_BUTTON_STATES;

#define HT_ISBUTTON(ht) ((ht) == HTMINBUTTON || (ht) == HTMAXBUTTON || (ht) == HTCLOSE || (ht) == HTHELP)

#define HASSIZEGRIP(Style, ExStyle, ParentStyle, WindowRect, ParentClientRect) \
            ((!(Style & WS_CHILD) && (Style & WS_THICKFRAME) && !(Style & WS_MAXIMIZE))  || \
             ((Style & WS_CHILD) && (ParentStyle & WS_THICKFRAME) && !(ParentStyle & WS_MAXIMIZE) && \
             (WindowRect.right - WindowRect.left == ParentClientRect.right) && \
             (WindowRect.bottom - WindowRect.top == ParentClientRect.bottom)))

#define HAS_MENU(hwnd,style)  ((((style) & (WS_CHILD | WS_POPUP)) != WS_CHILD) && GetMenu(hwnd))

#define BUTTON_GAP_SIZE 2

#define MENU_BAR_ITEMS_SPACE (12)

#define SCROLL_TIMER   0                /* Scroll timer id */

  /* Overlap between arrows and thumb */
#define SCROLL_ARROW_THUMB_OVERLAP 0

  /* Delay (in ms) before first repetition when holding the button down */
#define SCROLL_FIRST_DELAY   200

  /* Delay (in ms) between scroll repetitions */
#define SCROLL_REPEAT_DELAY  50

/* Minimum size of the thumb in pixels */
#define SCROLL_MIN_THUMB 6

/* Minimum size of the rectangle between the arrows */
#define SCROLL_MIN_RECT  4

LRESULT CALLBACK ThemeWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, WNDPROC DefWndProc);
void ThemeCalculateCaptionButtonsPos(HWND hWnd, HTHEME htheme);
LONG SCROLL_getObjectId(INT nBar);
void ThemeDrawScrollBarEx(PDRAW_CONTEXT pcontext, INT nBar, PSCROLLBARINFO psbi, POINT* pt);
void ThemeDrawScrollBar(PDRAW_CONTEXT pcontext, INT Bar, POINT* pt);
VOID NC_TrackScrollBar(HWND Wnd, WPARAM wParam, POINT Pt);
void ThemeInitDrawContext(PDRAW_CONTEXT pcontext, HWND hWnd, HRGN hRgn);
void ThemeCleanupDrawContext(PDRAW_CONTEXT pcontext);
PWND_DATA ThemeGetWndData(HWND hWnd);
HTHEME GetNCCaptionTheme(HWND hWnd, DWORD style);
HTHEME GetNCScrollbarTheme(HWND hWnd, DWORD style);

extern HINSTANCE hDllInst;
extern ATOM atWndContext;
extern BOOL g_bThemeHooksActive;

void UXTHEME_InitSystem(HINSTANCE hInst);
void UXTHEME_ReloadTheme(BOOL load);
BOOL CALLBACK UXTHEME_broadcast_theme_changed (HWND hWnd, LPARAM enable);

/* No alpha blending */
#define ALPHABLEND_NONE             0
/* "Cheap" binary alpha blending - but possibly faster */
#define ALPHABLEND_BINARY           1
/* Full alpha blending */
#define ALPHABLEND_FULL             2

extern DWORD gdwErrorInfoTlsIndex;

VOID UXTHEME_DeleteParseErrorInfo(VOID);

static inline
HRESULT
UXTHEME_MakeError(_In_ LONG error)
{
    if (error < 0)
        return (HRESULT)error;
    return HRESULT_FROM_WIN32(error);
}

static inline
HRESULT
UXTHEME_MakeLastError(VOID)
{
    return UXTHEME_MakeError(GetLastError());
}

HRESULT
UXTHEME_MakeParseError(
    _In_ UINT nID,
    _In_ LPCWSTR pszParam1,
    _In_ LPCWSTR pszParam2,
    _In_ LPCWSTR pszFile,
    _In_ LPCWSTR pszLine,
    _In_ INT nLineNo);

#endif /* _UXTHEME_PCH_ */
