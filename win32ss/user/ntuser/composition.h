/*
 * PROJECT:     ReactOS Win32k / Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-side composition engine (DWM) — public interface.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * The composition engine turns the classic "every window draws straight into
 * the shared primary" model into a redirected/composited one:
 *
 *   - each visible top-level window gets an off-screen backing SURFACE
 *     (redirection surface); its DCs (and its children's) draw there,
 *   - a damage-driven compose pass blits the backings into the primary in
 *     Z-order under the device lock, so the canonical display driver presents
 *     the touched region,
 *   - redirected DCs stay DCTYPE_DIRECT and keep their window binding
 *     (WindowFromDC / GetClientRect must still resolve — the software OpenGL
 *     implementation sizes and presents through them); the DC_REDIRECTION
 *     flag stops DC_vPrepareDCsForBlit from reverting the surface to the
 *     PDEV primary.
 *
 * Gated behind gbCompositionEnabled; with it off the classic direct-to-primary
 * path is used unchanged.
 */

#pragma once

extern BOOL gbCompositionEnabled;

/* Per-top-level-window composition state (kept in a side table, not in WND).
 * Double-buffered: DCs draw into psurf (the live BACK buffer); the compositor
 * presents psurfFront, which is synced from psurf only when the window is
 * quiescent (no open paint/DC bracket) — so a half-painted window can never
 * reach the screen, even when overlap forces its re-assertion. */
typedef struct _WND_REDIRECT
{
    PSURFACE  psurf;        /* live backing (BACK) — the redirection target   */
    HBITMAP   hbmp;         /* bitmap owning psurf                            */
    PSURFACE  psurfFront;   /* last-complete frame (FRONT) — what composes    */
    HBITMAP   hbmpFront;    /* bitmap owning psurfFront                       */
    BOOL      FrontValid;   /* front has been synced at least once            */
    LONG      cx;           /* buffer dimensions                              */
    LONG      cy;
    LONGLONG  AllocFailTime;/* last backing-alloc failure (backoff, 100ns)    */
    /* FRONT is section-backed so dwm.exe maps it read-only (zero-copy):      */
    PVOID     FrontSection; /* referenced section object                      */
    PVOID     FrontView;    /* kernel view the FRONT surface wraps            */
    SIZE_T    FrontViewSize;
    ULONG     Generation;   /* bumped per FRONT (re)create — dwm remap key    */
} WND_REDIRECT, *PWND_REDIRECT;

/* Engine lifecycle. */
BOOL  IntCompositionIsEnabled(VOID);
NTSTATUS IntCompositionSetEnabled(BOOL bEnable);

/* dwm.exe attach/detach (ONEPARAM_ROUTINE_DWMATTACH): DWM_ATTACH exchange —
 * returns the damage wake + vblank pacing event handles in dwm's process. */
NTSTATUS IntCompositionDwmAttach(_In_ PVOID pUser);

/* User-mode dwm.exe frame pull (ONEPARAM_ROUTINE_DWMGETFRAME): fills the
 * dwm-provided buffer with the Z-ordered compositable windows + their backing
 * pixels. See sdk/include/reactos/dwmframe.h for the layout. */
NTSTATUS IntCompositionDwmGetFrame(_In_ PVOID pUser);

/* CDD present bracket for dwm (ONEPARAM_ROUTINE_DWMPRESENTSYNC): value 1 opens
 * / 0 closes the present bracket around dwm's BitBlt to the primary. */
VOID IntCompositionDwmSync(_In_ LONG value);

/* Open a window FRONT section into dwm's process (ONEPARAM_ROUTINE_
 * DWMOPENSURFACE); see DWM_OPEN_SURFACE in dwmframe.h. */
NTSTATUS IntCompositionDwmOpenSurface(_In_ PVOID pUser);

/* Layered-window alpha/colorkey for the dwm frame descriptor (layered.c). */
BOOL FASTCALL IntCompositionGetLayered(PWND pWnd, BYTE *pAlpha, COLORREF *pKey, DWORD *pFlags);

/* Redirection surface for a window (NULL when composition is off or the window
 * is not redirected — callers then fall back to the primary). */
PSURFACE IntCompositionGetRedirectSurface(_In_ PWND Wnd);

/* Redirect a window DC into its top-level ancestor's backing surface (no-op
 * when composition is off). DcxFlags/hrgnClip are the owning DCE's — they pick
 * the window vs client origin and reproduce the DCX_INTERSECTRGN/EXCLUDERGN
 * clip inside the backing. Called from UserGetDCEx after the DC is set up, and
 * from DceResetActiveDCEs to re-derive a live redirect after a move/resize.
 * Revalidate mirrors the DCE's own bUpdateVisRgn discipline: when FALSE and the
 * DC is already redirected at the current backing/origin, the (expensive)
 * VisRgn rebuild is skipped — the existing clip is still valid. */
VOID IntCompositionRedirectDC(_In_opt_ PWND Wnd, _In_ HDC hDC, _In_ ULONG DcxFlags, _In_opt_ HRGN hrgnClip, _In_ BOOL Revalidate);

/* Undo a DC redirect (release the backing ref, restore the primary). Called
 * from DceReleaseDC and DceFreeWindowDCE. Runs even when composition has been
 * disabled meanwhile, so no backing reference is ever stranded. */
VOID IntCompositionUnredirectDC(_In_ HDC hDC);

/* Mark a window's top-level ancestor as OpenGL (pixel format set). Called from
 * NtGdiSetPixelFormat. GL windows are still composited, but their persistent DC
 * is kept redirected across release (see IntCompositionIsGLWindow). */
VOID IntCompositionMarkOpenGL(_In_ PWND Wnd);

/* TRUE if the window's top-level ancestor uses OpenGL. */
BOOL IntCompositionIsGLWindow(_In_opt_ PWND Wnd);

/* Window lifetime hooks (called from co_UserCreateWindowEx / co_UserFreeWindow /
 * co_WinPosSetWindowPos). */
VOID IntCompositionOnWindowCreate(_In_ PWND Wnd);
VOID IntCompositionOnWindowDestroy(_In_ PWND Wnd);
VOID IntCompositionOnWindowResize(_In_ PWND Wnd);

/* Damage: a window (NULL = the whole frame) changed — schedule a recompose. */
VOID IntCompositionDamageWindow(_In_opt_ PWND Wnd);

/* Damage from the GDI blit path (DC_vFinishBlit): a backing surface was drawn
 * into outside a paint cycle (e.g. an OpenGL present) — psurf resolves to its
 * window. Only flags the damage — the compose runs on the next tick. */
VOID IntCompositionDamageBacking(_In_opt_ PSURFACE psurf);

/* Damage from a direct write to the primary (drag/focus artists, desktop
 * paint): the whole frame is re-asserted on the next compose. */
VOID IntCompositionDamageFromGdi(VOID);

/* BeginPaint/EndPaint bracket: while open, the compositor won't sync the
 * window's front buffer from its (inconsistent) backing; the closing EndPaint
 * recomposes (and self-heals the bracket counter once the thread's paint
 * batch drains). Called from IntBeginPaint / IntEndPaint. */
VOID IntCompositionPaintBegin(_In_ PWND Wnd);
VOID IntCompositionPaintEnd(_In_ PWND Wnd);

/* Cache-DC hold bracket: draw sequences done via GetDC..ReleaseDC (button
 * states, status bars, carets, NC paints) are treated like paint brackets so
 * their intermediate states never present. GL windows are exempt (their DC is
 * held for the window's lifetime). Called from UserGetDCEx / DceReleaseDC for
 * DCX_CACHE DCs. */
VOID IntCompositionDcAcquire(_In_opt_ PWND Wnd);
VOID IntCompositionDcRelease(_In_opt_ PWND Wnd);

/* Compositor liveness watchdog, called from the message pump: a silent dwm
 * tears composition down and the desktop reverts to direct drawing. */
VOID IntCompositionWatchdog(VOID);
