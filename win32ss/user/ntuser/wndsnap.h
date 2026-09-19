#pragma once

extern BOOL g_bWindowSnapEnabled;

/* Private server-side class used only by the composited snap preview. */
#define SNAP_PREVIEW_CLASS_NAME L"ReactOSSnapPreview"

/* How far the pointer may drift off the edge before the armed snap is dropped */
#define SNAP_RELEASE_SLACK 12

/* Snap preview: a layered popup composed and animated by the compositor */
UINT FASTCALL IntSnapPreviewEdge(VOID);
VOID FASTCALL co_IntSnapPreviewShow(PWND pwndDrag, UINT Edge, const RECT *prcTarget, POINT ptCursor);
VOID FASTCALL co_IntSnapPreviewHide(VOID);
VOID FASTCALL co_IntSnapPreviewDestroy(VOID);

/* Snap logic */
UINT FASTCALL IntGetWindowSnapEdge(PWND Wnd);
VOID FASTCALL co_IntCalculateSnapPosition(PWND Wnd, UINT Edge, OUT RECT *Pos);
VOID FASTCALL co_IntSnapWindow(PWND Wnd, UINT Edge);
VOID FASTCALL IntSetSnapEdge(PWND Wnd, UINT Edge);
VOID FASTCALL IntSetSnapInfo(PWND Wnd, UINT Edge, IN const RECT *Pos OPTIONAL);
UINT GetSnapActivationPoint(PWND Wnd, POINT pt);
BOOL IsPointHoldingSnapEdge(UINT Edge, POINT pt);

#define GetSnapSetting(gspvmember) (IsSnapEnabled() ? (gspv.gspvmember) : 0)

FORCEINLINE BOOL
IsSnapEnabled(VOID)
{
    return g_bWindowSnapEnabled;
}

FORCEINLINE VOID
co_IntUnsnapWindow(PWND Wnd)
{
    co_IntSnapWindow(Wnd, HTNOWHERE);
}

FORCEINLINE BOOLEAN
IntIsWindowSnapped(PWND Wnd)
{
    return (Wnd->ExStyle2 & (WS_EX2_VERTICALLYMAXIMIZEDLEFT | WS_EX2_VERTICALLYMAXIMIZEDRIGHT)) != 0;
}

FORCEINLINE BOOLEAN
IntIsSnapAllowedForWindow(PWND Wnd)
{
    /* We want to forbid snapping operations on the TaskBar and on child windows.
     * We use a heuristic for detecting the TaskBar by its typical Style & ExStyle. */
    const UINT style = Wnd->style;
    const UINT tbws = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    const UINT tbes = WS_EX_TOOLWINDOW;
    BOOLEAN istb = (style & tbws) == tbws && (Wnd->ExStyle & (tbes | WS_EX_APPWINDOW)) == tbes;
    BOOLEAN thickframe = (style & WS_THICKFRAME) && (style & (WS_DLGFRAME | WS_BORDER)) != WS_DLGFRAME;
    return thickframe && !(style & WS_CHILD) && !istb;
}
