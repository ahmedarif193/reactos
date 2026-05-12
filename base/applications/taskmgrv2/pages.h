#pragma once

/* -----------------------------------------------------------------------
 * pages.h — Common page vtbl typedefs and MODERN_PAGE_ID enum.
 * Included by shell.c and every page translation unit.
 * ----------------------------------------------------------------------- */

typedef enum _MODERN_PAGE_ID {
    MPAGE_PROCESSES = 0,
    MPAGE_PERFORMANCE,
    MPAGE_APPHISTORY,
    MPAGE_STARTUP,
    MPAGE_USERS,
    MPAGE_DETAILS,
    MPAGE_SERVICES,
    MPAGE_SETTINGS,
    MPAGE__COUNT
} MODERN_PAGE_ID;

typedef HWND (CALLBACK *PAGE_CREATE_FN) (HWND hHost, HINSTANCE hInst);
typedef void (CALLBACK *PAGE_TICK_FN)   (HWND hPage);
typedef void (CALLBACK *PAGE_RESIZE_FN) (HWND hPage, int cx, int cy);
typedef void (CALLBACK *PAGE_DESTROY_FN)(HWND hPage);
typedef void (CALLBACK *PAGE_THEME_FN)  (HWND hPage); /* called on dark/light flip */

typedef struct _PAGE_VTBL {
    LPCWSTR          displayName; /* IDS_NAV_* string pointer */
    int              iconResId;   /* IDB_NAV_* strip cell index */
    PAGE_CREATE_FN   pfnCreate;
    PAGE_TICK_FN     pfnTick;     /* nullable */
    PAGE_RESIZE_FN   pfnResize;   /* nullable */
    PAGE_DESTROY_FN  pfnDestroy;  /* nullable */
    PAGE_THEME_FN    pfnTheme;    /* nullable */
} PAGE_VTBL;
