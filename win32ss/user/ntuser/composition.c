/*
 * PROJECT:     ReactOS Win32k / Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-side composition engine (DWM).
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * See composition.h for the model. Redirection keeps the DC a DIRECT window
 * DC and swaps only the surface/origin/clip into the backing; composition is
 * damage-driven and runs under the device lock (mouse safety included), so
 * backing reads and primary writes are serialized against all direct draws.
 */

#include <win32k.h>
#include "composition.h"
#include <reactos/dwmframe.h>
DBG_DEFAULT_CHANNEL(UserPainting);

/* Send a CDD_ESCAPE_* to the canonical display driver via the screen DC. */
static VOID
IntCompositionDriverEscape(_In_ ULONG iEsc, _In_ PVOID pvIn, _In_ ULONG cjIn)
{
    PDC pdcScreen;
    PPDEVOBJ ppdev;

    if (ScreenDeviceContext == NULL)
        return;
    pdcScreen = DC_LockDc(ScreenDeviceContext);
    if (pdcScreen == NULL)
        return;

    ppdev = pdcScreen->ppdev;
    if (ppdev != NULL && ppdev->DriverFunctions.Escape != NULL &&
        ppdev->pSurface != NULL)
    {
        ppdev->DriverFunctions.Escape(&ppdev->pSurface->SurfObj,
                                      iEsc, cjIn, pvIn, 0, NULL);
    }
    DC_UnlockDc(pdcScreen);
}

/* OFF until dwm.exe attaches (Windows model: no compositor -> direct draw;
 * redirection exists only while a compositor owns the frame). */
BOOL gbCompositionEnabled = FALSE;

/*
 * PWND -> redirection-surface map. A fixed side table keeps the WND structure
 * (shared with user mode) untouched. Sized for a comfortable desktop; windows
 * beyond the cap simply stay on the direct path.
 */
#define COMPOSITION_MAX_WINDOWS 512
#define COMPOSITION_COMPOSE_MAX COMPOSITION_MAX_WINDOWS
/* A paint bracket / pending first paint older than this composes anyway
 * (hung or starved painter must not freeze its window forever). */
#define COMPOSITION_PAINT_STALE_100NS (100LL * 10000LL) /* 100 ms */

typedef struct _REDIRECT_ENTRY
{
    PWND         Wnd;
    WND_REDIRECT Redirect;
    LONG         PaintCount;   /* BeginPaint..EndPaint depth on this tree      */
    LONGLONG     PaintStart;   /* when the bracket (or first damage) opened    */
    LONGLONG     FirstPendingSeen; /* first-paint-pending stale clock          */
    BOOL         Damaged;      /* this window's backing changed since compose  */
    BOOL         EverPainted;  /* completed at least one EndPaint              */
} REDIRECT_ENTRY;

static REDIRECT_ENTRY  g_Redirects[COMPOSITION_MAX_WINDOWS];
static ULONG           g_RedirectHighWater = 0;
static volatile BOOL   g_CompositionDamaged = FALSE;
static volatile BOOL   g_CompositionFullDamage = FALSE;

/* dwm.exe is the ONLY compositor (Windows model — win32k tracks redirection
 * and damage, never composes). Attach enables redirection; detach or a
 * silent dwm (watchdog on g_DwmLastFrameTime, bumped by every GetFrame)
 * disables it and the desktop reverts to classic direct drawing. */
volatile BOOL          g_DwmAttached = FALSE;
volatile LONGLONG      g_DwmLastFrameTime = 0;
/* The thread currently blitting dwm's composed frame to the primary — only
 * ITS drawing must not re-damage the composition (else an endless recompose);
 * damage from every other thread keeps landing normally. */
static volatile PVOID  g_DwmPresentThread = NULL;
/* Damage wake event for dwm (referenced from dwm's handle), so dwm blocks
 * instead of polling when the desktop is idle. */
static PKEVENT         g_DwmWakeEvent = NULL;
/* The attached compositor process: every DWM entry point is rejected for
 * anyone else (frame metadata + window surfaces are session-global state). */
static PEPROCESS       g_DwmProcess = NULL;
/* Scanout pacing event (referenced; also registered with the display path,
 * whose present timer signals it every period). */
static PKEVENT         g_DwmVblankEvent = NULL;

/* Wake dwm after marking damage (no-op when no dwm is attached). */
static VOID
IntCompositionDwmWake(VOID)
{
    if (g_DwmWakeEvent != NULL)
        KeSetEvent(g_DwmWakeEvent, IO_NO_INCREMENT, FALSE);
}

/* Mark the composition dirty (bFull: the whole frame) and wake dwm. */
static VOID
IntCompositionMarkDamage(_In_ BOOL bFull)
{
    if (bFull)
        g_CompositionFullDamage = TRUE;
    g_CompositionDamaged = TRUE;
    IntCompositionDwmWake();
}

/*
 * OpenGL (pixel-format) top-level windows: the SW OpenGL implementation holds
 * its window DC (fb->Hdc) across frames and presents via SetDIBitsToDevice on
 * it, so that DC must stay redirected across DceReleaseDC. Entries are cleared
 * on window destroy so a recycled PWND is never misclassified.
 */
#define COMPOSITION_MAX_GL 32
static PWND g_GlWindows[COMPOSITION_MAX_GL];

BOOL
IntCompositionIsEnabled(VOID)
{
    return gbCompositionEnabled;
}

static PWND
IntCompositionTopLevel(_In_ PWND Wnd)
{
    PWND ancestor = Wnd, desktop = UserGetDesktopWindow();

    if (Wnd == NULL || desktop == NULL)
        return NULL;

    while (ancestor->spwndParent != NULL && ancestor->spwndParent != desktop)
        ancestor = ancestor->spwndParent;
    return ancestor;
}

static REDIRECT_ENTRY *
IntCompositionFind(_In_ PWND Wnd)
{
    ULONG i;
    if (Wnd == NULL)
        return NULL;
    for (i = 0; i < g_RedirectHighWater; i++)
    {
        if (g_Redirects[i].Wnd == Wnd)
            return &g_Redirects[i];
    }
    return NULL;
}

static REDIRECT_ENTRY *
IntCompositionAlloc(_In_ PWND Wnd)
{
    ULONG i;
    for (i = 0; i < COMPOSITION_MAX_WINDOWS; i++)
    {
        if (g_Redirects[i].Wnd == NULL)
        {
            RtlZeroMemory(&g_Redirects[i], sizeof(REDIRECT_ENTRY));
            g_Redirects[i].Wnd = Wnd;
            if (i >= g_RedirectHighWater)
                g_RedirectHighWater = i + 1;
            return &g_Redirects[i];
        }
    }
    return NULL;
}

/* A window is composited only if it is a visible top-level window. */
static BOOL
IntCompositionIsCompositable(_In_ PWND Wnd)
{
    if (Wnd == NULL || Wnd->head.pti == NULL)
        return FALSE;
    if (!(Wnd->style & WS_VISIBLE))
        return FALSE;
    /* Top-level: parent is the desktop. */
    if (Wnd->spwndParent == NULL || Wnd->spwndParent != UserGetDesktopWindow())
        return FALSE;
    /* The desktop window itself is the composition backdrop, not a layer. */
    if (Wnd == UserGetDesktopWindow())
        return FALSE;
    return TRUE;
}

static VOID
IntCompositionFreeSurface(_Inout_ PWND_REDIRECT r)
{
    if (r->psurf != NULL)
    {
        SURFACE_ShareUnlockSurface(r->psurf);
        r->psurf = NULL;
    }
    if (r->hbmp != NULL)
    {
        /* PUBLIC bitmaps: GreDeleteObject refuses them, EngDeleteSurface not. */
        EngDeleteSurface((HSURF)r->hbmp);
        r->hbmp = NULL;
    }
    if (r->psurfFront != NULL)
    {
        SURFACE_ShareUnlockSurface(r->psurfFront);
        r->psurfFront = NULL;
    }
    if (r->hbmpFront != NULL)
    {
        EngDeleteSurface((HSURF)r->hbmpFront);
        r->hbmpFront = NULL;
    }
    /* The FRONT wraps a section view: surface first, then view, then section.
     * dwm's own mapped view (its handle references the section) stays valid
     * until it notices the Generation change and remaps. */
    if (r->FrontView != NULL)
    {
        MmUnmapViewInSystemSpace(r->FrontView);
        r->FrontView = NULL;
    }
    if (r->FrontSection != NULL)
    {
        ObDereferenceObject(r->FrontSection);
        r->FrontSection = NULL;
    }
    r->FrontViewSize = 0;
    r->FrontValid = FALSE;
    r->cx = r->cy = 0;
}

static PSURFACE
IntCompositionCreateBuffer(_In_ LONG cx, _In_ LONG cy, _Out_ HBITMAP *phbmp)
{
    PSURFACE psurf;
    HBITMAP hbmp;

    hbmp = GreCreateBitmap(cx, cy, 1, 32, NULL);
    if (hbmp == NULL)
        return NULL;

    /* The buffer's lifetime is the window's, not the creating process's:
     * make it public so process GDI-handle cleanup neither deletes it under
     * us nor reports it as a leaked handle. */
    GreSetBitmapOwner(hbmp, GDI_OBJ_HMGR_PUBLIC);

    psurf = SURFACE_ShareLockSurface(hbmp);
    if (psurf == NULL)
    {
        EngDeleteSurface((HSURF)hbmp);
        return NULL;
    }

    /* Never let uninitialized bits reach the screen. */
    if (psurf->SurfObj.pvBits != NULL)
        RtlZeroMemory(psurf->SurfObj.pvBits, psurf->SurfObj.cjBits);

    *phbmp = hbmp;
    return psurf;
}

/* Monotonic FRONT generation: a (slot, generation) pair never repeats, so
 * dwm's mapped-view cache can never alias a recycled surface. */
static ULONG g_FrontGeneration = 0;

/* FRONT buffer over a pageable section: win32k keeps a system-space view the
 * GDI surface wraps; dwm maps the same section read-only in user space and
 * composes straight from it — no per-frame pixel copies out of the kernel. */
static PSURFACE
IntCompositionCreateSharedFront(_In_ LONG cx, _In_ LONG cy,
                                _Out_ HBITMAP *phbmp,
                                _Out_ PVOID *ppSection,
                                _Out_ PVOID *ppView,
                                _Out_ SIZE_T *pcbView)
{
    LARGE_INTEGER liSize;
    PVOID pSection = NULL, pView = NULL;
    SIZE_T cbView;
    PSURFACE psurf;
    HBITMAP hbmp;
    NTSTATUS Status;

    *phbmp = NULL;
    *ppSection = NULL;
    *ppView = NULL;
    *pcbView = 0;

    liSize.QuadPart = (LONGLONG)cx * 4 * cy;
    Status = MmCreateSection(&pSection, SECTION_ALL_ACCESS, NULL, &liSize,
                             PAGE_READWRITE, SEC_COMMIT, NULL, NULL);
    if (!NT_SUCCESS(Status))
        return NULL;

    cbView = (SIZE_T)liSize.QuadPart;
    Status = MmMapViewInSystemSpace(pSection, &pView, &cbView);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(pSection);
        return NULL;
    }

    RtlZeroMemory(pView, (SIZE_T)liSize.QuadPart);

    hbmp = GreCreateBitmapEx((ULONG)cx, (ULONG)cy, (ULONG)cx * 4, BMF_32BPP,
                             BMF_TOPDOWN | BMF_NOZEROINIT,
                             (ULONG)liSize.QuadPart, pView, 0);
    if (hbmp == NULL)
    {
        MmUnmapViewInSystemSpace(pView);
        ObDereferenceObject(pSection);
        return NULL;
    }
    GreSetBitmapOwner(hbmp, GDI_OBJ_HMGR_PUBLIC);

    psurf = SURFACE_ShareLockSurface(hbmp);
    if (psurf == NULL)
    {
        EngDeleteSurface((HSURF)hbmp);
        MmUnmapViewInSystemSpace(pView);
        ObDereferenceObject(pSection);
        return NULL;
    }

    *phbmp = hbmp;
    *ppSection = pSection;
    *ppView = pView;
    *pcbView = cbView;
    return psurf;
}

/* Ensure the window has back+front buffers matching its current window size.
 * On resize the old content is preserved (top-left aligned): a shrink issues
 * no WM_PAINT (everything stayed valid), so discarding the old bits would
 * present a black window. */
static PSURFACE
IntCompositionEnsureSurface(_In_ PWND Wnd, _Inout_ PWND_REDIRECT r)
{
    LONG cx = Wnd->rcWindow.right - Wnd->rcWindow.left;
    LONG cy = Wnd->rcWindow.bottom - Wnd->rcWindow.top;
    PSURFACE psurfNew, psurfNewFront;
    HBITMAP hbmpNew, hbmpNewFront = NULL;
    PVOID pSectionNew = NULL, pViewNew = NULL;
    SIZE_T cbViewNew = 0;
    RECTL rcCopy;
    POINTL ptZero = {0, 0};

    if (cx <= 0 || cy <= 0)
        return NULL;

    if (r->psurf != NULL && r->cx == cx && r->cy == cy)
        return r->psurf;

    /* A failed allocation must not retry on every GetDC (multi-MB pool
     * attempts per paint flooded the log and burned CPU when paged pool ran
     * dry) - back off; the window draws direct meanwhile. */
    if (r->AllocFailTime != 0 &&
        ((LONGLONG)KeQueryInterruptTime() - r->AllocFailTime) < (2LL * 10000000LL))
    {
        return r->psurf;
    }

    psurfNew = IntCompositionCreateBuffer(cx, cy, &hbmpNew);
    if (psurfNew == NULL)
    {
        r->AllocFailTime = (LONGLONG)KeQueryInterruptTime();
        return r->psurf;
    }
    r->AllocFailTime = 0;

    psurfNewFront = IntCompositionCreateSharedFront(cx, cy, &hbmpNewFront,
                                                    &pSectionNew, &pViewNew,
                                                    &cbViewNew);

    rcCopy.left = 0;
    rcCopy.top = 0;
    rcCopy.right = min(cx, r->cx);
    rcCopy.bottom = min(cy, r->cy);

    if (r->psurf != NULL)
    {
        IntEngBitBlt(&psurfNew->SurfObj, &r->psurf->SurfObj,
                     NULL, NULL, NULL,
                     &rcCopy, &ptZero, NULL, NULL, NULL, ROP4_SRCCOPY);
    }
    if (psurfNewFront != NULL && r->psurfFront != NULL && r->FrontValid)
    {
        IntEngBitBlt(&psurfNewFront->SurfObj, &r->psurfFront->SurfObj,
                     NULL, NULL, NULL,
                     &rcCopy, &ptZero, NULL, NULL, NULL, ROP4_SRCCOPY);
    }

    {
        BOOL bFrontValid = r->FrontValid && (psurfNewFront != NULL) &&
                           (r->psurfFront != NULL);
        IntCompositionFreeSurface(r);
        r->psurf = psurfNew;
        r->hbmp = hbmpNew;
        r->psurfFront = psurfNewFront;
        r->hbmpFront = hbmpNewFront;
        r->FrontSection = pSectionNew;
        r->FrontView = pViewNew;
        r->FrontViewSize = cbViewNew;
        r->Generation = ++g_FrontGeneration;
        r->FrontValid = bFrontValid;
    }

    r->cx = cx;
    r->cy = cy;
    return r->psurf;
}

VOID
IntCompositionMarkOpenGL(_In_ PWND Wnd)
{
    PWND ancestor;
    ULONG i;

    /* Deliberately NOT gated on gbCompositionEnabled: a GL context created
     * before dwm attaches must already be marked when redirection turns on. */
    ancestor = IntCompositionTopLevel(Wnd);
    if (ancestor == NULL)
        return;

    for (i = 0; i < COMPOSITION_MAX_GL; i++)
        if (g_GlWindows[i] == ancestor)
            return; /* already marked */

    for (i = 0; i < COMPOSITION_MAX_GL; i++)
    {
        if (g_GlWindows[i] == NULL)
        {
            g_GlWindows[i] = ancestor;
            break;
        }
    }

    /* GL windows hold their cache DC for the window's lifetime, so the DC-hold
     * bracket does not apply to them — release any bracket the pre-mark
     * GetDC opened, or the window would only compose on the stale-guard. */
    {
        REDIRECT_ENTRY *e = IntCompositionFind(ancestor);
        if (e != NULL)
            e->PaintCount = 0;
    }
}

BOOL
IntCompositionIsGLWindow(_In_opt_ PWND Wnd)
{
    PWND ancestor = IntCompositionTopLevel(Wnd);
    ULONG i;

    if (ancestor == NULL)
        return FALSE;

    for (i = 0; i < COMPOSITION_MAX_GL; i++)
        if (g_GlWindows[i] == ancestor)
            return TRUE;
    return FALSE;
}

PSURFACE
IntCompositionGetRedirectSurface(_In_ PWND Wnd)
{
    REDIRECT_ENTRY *e;

    if (!gbCompositionEnabled)
        return NULL;

    e = IntCompositionFind(Wnd);
    if (e == NULL)
        return NULL;

    return e->Redirect.psurf;
}

VOID
IntCompositionOnWindowCreate(_In_ PWND Wnd)
{
    REDIRECT_ENTRY *e;

    if (!gbCompositionEnabled || !IntCompositionIsCompositable(Wnd))
        return;

    e = IntCompositionFind(Wnd);
    if (e == NULL)
        e = IntCompositionAlloc(Wnd);
    if (e == NULL)
        return;

    IntCompositionEnsureSurface(Wnd, &e->Redirect);
    IntCompositionDamageWindow(Wnd);
}

VOID
IntCompositionOnWindowDestroy(_In_ PWND Wnd)
{
    REDIRECT_ENTRY *e;
    ULONG i;

    /* Drop any GL mark: the PWND may be recycled for an unrelated window. */
    for (i = 0; i < COMPOSITION_MAX_GL; i++)
        if (g_GlWindows[i] == Wnd)
            g_GlWindows[i] = NULL;

    e = IntCompositionFind(Wnd);
    if (e == NULL)
        return;

    IntCompositionFreeSurface(&e->Redirect);
    e->Wnd = NULL;
    /* The area under the destroyed window must be re-asserted. */
    IntCompositionMarkDamage(TRUE);
}

/*
 * A window's position/size/Z-order changed. Resize the backing when needed
 * (DceResetActiveDCEs then re-redirects any live DCs at the new surface —
 * this hook must run before it) and always schedule a recompose so pure
 * moves/Z-order changes reach the screen without waiting for a paint.
 */
VOID
IntCompositionOnWindowResize(_In_ PWND Wnd)
{
    REDIRECT_ENTRY *e;

    if (!gbCompositionEnabled)
        return;

    /*
     * A move/resize/z-order change: recompose the WHOLE frame in strict
     * top-to-bottom order. A damage-scoped compose would blit only the moved
     * window's region and could re-assert a lower window over a topmost one
     * (the taskbar flashing above windows for a frame during a drag); the
     * vacated area under the old position also has to be repainted.
     */
    IntCompositionMarkDamage(TRUE);

    e = IntCompositionFind(Wnd);
    if (e == NULL)
    {
        IntCompositionOnWindowCreate(Wnd);
        return;
    }

    {
        PSURFACE psurfOld = e->Redirect.psurf;

        IntCompositionEnsureSurface(Wnd, &e->Redirect);

        /* The backing was replaced: a long-held redirected DC (CS_OWNDC GL
         * window -- Mesa keeps the SetPixelFormat DC for the context's
         * lifetime) still references the ORPHANED old backing and would
         * present into it invisibly from now on. Rebind every active DCE of
         * this window to the new backing, exactly as a user move/size does. */
        if (e->Redirect.psurf != psurfOld)
            DceResetActiveDCEs(Wnd);
    }
    e->Damaged = TRUE;
}

VOID
IntCompositionDamageWindow(_In_opt_ PWND Wnd)
{
    REDIRECT_ENTRY *e;

    if (!gbCompositionEnabled)
        return;

    if (Wnd != NULL &&
        (e = IntCompositionFind(IntCompositionTopLevel(Wnd))) != NULL)
    {
        e->Damaged = TRUE;
        IntCompositionMarkDamage(FALSE);
    }
    else
    {
        IntCompositionMarkDamage(TRUE);
    }
}

/*
 * Damage from the GDI blit path — no window context, only the DC's surface.
 * A backing surface maps to its window (pointer compare only: entries may be
 * mutating under us without the USER lock, worst case a stray Damaged flag);
 * a direct write to the primary (drag artists, desktop paint) means the whole
 * frame must be re-asserted.
 */
VOID
IntCompositionDamageBacking(_In_opt_ PSURFACE psurf)
{
    ULONG i;

    if (!gbCompositionEnabled)
        return;

    if (psurf != NULL)
    {
        for (i = 0; i < g_RedirectHighWater; i++)
        {
            if (g_Redirects[i].Redirect.psurf == psurf)
            {
                g_Redirects[i].Damaged = TRUE;
                IntCompositionMarkDamage(FALSE);
                return;
            }
        }
    }

    IntCompositionMarkDamage(TRUE);
}

VOID
IntCompositionDamageFromGdi(VOID)
{
    /* dwm's own presenting thread must not re-damage (feedback loop);
     * every other thread's primary write always does. */
    if (gbCompositionEnabled &&
        KeGetCurrentThread() != (PKTHREAD)g_DwmPresentThread)
    {
        IntCompositionMarkDamage(TRUE);
    }
}

/*
 * BeginPaint/EndPaint bracket. While a window tree is mid-paint the
 * compositor defers presenting its (inconsistent) backing — the screen keeps
 * the previous frame — and the EndPaint that closes the last bracket damages
 * and (when the thread has no further paints queued) composes the finished
 * frame. This is what keeps other windows from flickering through their
 * erase/redraw cycles while a GL app's message pump drives composes at 60Hz.
 */
VOID
IntCompositionPaintBegin(_In_ PWND Wnd)
{
    REDIRECT_ENTRY *e;

    if (!gbCompositionEnabled)
        return;

    e = IntCompositionFind(IntCompositionTopLevel(Wnd));
    if (e == NULL)
        return;

    if (InterlockedIncrement(&e->PaintCount) == 1)
        e->PaintStart = (LONGLONG)KeQueryInterruptTime();
}

VOID
IntCompositionPaintEnd(_In_ PWND Wnd)
{
    REDIRECT_ENTRY *e;

    if (!gbCompositionEnabled)
        return;

    e = IntCompositionFind(IntCompositionTopLevel(Wnd));
    if (e == NULL)
        return;

    if (e->PaintCount > 0)
        InterlockedDecrement(&e->PaintCount);

    /* Thread's whole paint batch drained: all of its windows are quiescent,
     * so force the bracket counter to 0 (self-heals any acquire/release drift
     * that would otherwise defer the window until the stale guard). */
    if (Wnd->head.pti->cPaintsReady == 0)
        e->PaintCount = 0;

    e->EverPainted = TRUE;
    e->Damaged = TRUE;
    IntCompositionMarkDamage(FALSE);
}

/*
 * Cache-DC hold bracket: GetDC..ReleaseDC draw sequences (button states,
 * status bars, carets, NC paints) share the paint bracket so their
 * intermediate states never sync into the front buffer. GL windows are
 * exempt — their DC is held for the window's lifetime.
 */
VOID
IntCompositionDcAcquire(_In_opt_ PWND Wnd)
{
    if (!gbCompositionEnabled || Wnd == NULL || IntCompositionIsGLWindow(Wnd))
        return;

    IntCompositionPaintBegin(Wnd);
}

VOID
IntCompositionDcRelease(_In_opt_ PWND Wnd)
{
    REDIRECT_ENTRY *e;

    if (!gbCompositionEnabled || Wnd == NULL || IntCompositionIsGLWindow(Wnd))
        return;

    e = IntCompositionFind(IntCompositionTopLevel(Wnd));
    if (e == NULL)
        return;

    if (e->PaintCount > 0)
        InterlockedDecrement(&e->PaintCount);

    /* If the session drew, flag damage; the compose runs on the batch-done
     * commit, the throttled tick, or the pre-idle flush (not per release, so
     * a burst of small draws doesn't serialize behind a compose each). */
    if (e->Damaged)
        IntCompositionMarkDamage(FALSE);
}

/*
 * NtUserDwmGetFrame (ONEPARAM_ROUTINE_DWMGETFRAME): hand the user-mode dwm.exe
 * one frame. Fills the dwm-provided buffer with [header][DWM_WIN...][pixels]:
 * the Z-ordered (bottom-to-top) compositable windows and a top-down copy of
 * each one's most-complete backing (live BACK when quiescent, last FRONT when
 * mid-paint). dwm blends + presents; the kernel compose pass stays yielded.
 * Runs under the USER lock (NtUserCallOneParam is exclusive).
 */
NTSTATUS
IntCompositionDwmGetFrame(_In_ PVOID pUser)
{
    PDWM_FRAME_HEADER hdr = (PDWM_FRAME_HEADER)pUser;
    PUCHAR base = (PUCHAR)pUser;
    PDWM_WIN wins = (PDWM_WIN)(base + DWM_WINARRAY_BASE);
    static PWND s_stack[DWM_MAX_WINDOWS];
    PWND pwndDesktop, pwndChild;
    PDC pdcScreen;
    SIZEL sizl = {0, 0};
    ULONG bufBytes = 0, count = 0, n = 0, i;
    LONGLONG now = (LONGLONG)KeQueryInterruptTime();
    BOOL dirty;
    RECTL rcDmg = {0, 0, 0, 0};
    NTSTATUS Status = STATUS_SUCCESS;

    if (!gbCompositionEnabled)
        return STATUS_DEVICE_NOT_READY;
    if (PsGetCurrentProcess() != g_DwmProcess)
        return STATUS_ACCESS_DENIED;

    _SEH2_TRY
    {
        ProbeForWrite(hdr, sizeof(*hdr), sizeof(ULONG));
        if (hdr->Magic != DWM_FRAME_MAGIC)
            Status = STATUS_INVALID_PARAMETER;
        else
            bufBytes = hdr->BufBytes;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        return Status;
    if (bufBytes < DWM_FRAME_BYTES)
        return STATUS_BUFFER_TOO_SMALL;

    pwndDesktop = UserGetDesktopWindow();
    if (pwndDesktop == NULL || ScreenDeviceContext == NULL)
        return STATUS_DEVICE_NOT_READY;

    pdcScreen = DC_LockDc(ScreenDeviceContext);
    if (pdcScreen == NULL)
        return STATUS_DEVICE_NOT_READY;
    PDEVOBJ_sizl(pdcScreen->ppdev, &sizl);
    DC_UnlockDc(pdcScreen);

    /* Idle fast-path: nothing changed since the last pull — tell dwm to skip
     * compose+present entirely (no copies). Keeps the self-heal clock fed. */
    dirty = (g_CompositionDamaged || g_CompositionFullDamage);
    if (!dirty)
    {
        _SEH2_TRY
        {
            ProbeForWrite(hdr, sizeof(*hdr), sizeof(ULONG));
            hdr->ScreenW = (ULONG)sizl.cx;
            hdr->ScreenH = (ULONG)sizl.cy;
            hdr->Count = 0;
            hdr->FullDamage = 0;
            hdr->Dirty = 0;
            hdr->WinArrayBase = DWM_WINARRAY_BASE;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        g_DwmLastFrameTime = (LONGLONG)KeQueryInterruptTime();
        return Status;
    }

    /* Top-first walk; emit bottom-first so dwm blits back-to-front. */
    for (pwndChild = pwndDesktop->spwndChild;
         pwndChild != NULL && n < DWM_MAX_WINDOWS;
         pwndChild = pwndChild->spwndNext)
    {
        if (IntCompositionIsCompositable(pwndChild))
            s_stack[n++] = pwndChild;
    }

    _SEH2_TRY
    {
        ProbeForWrite(base, bufBytes, sizeof(ULONG));

        for (i = 0; i < n && count < DWM_MAX_WINDOWS; i++)
        {
            PWND w = s_stack[n - 1 - i];
            REDIRECT_ENTRY *e = IntCompositionFind(w);

            if (e == NULL || e->Redirect.cx <= 0 || e->Redirect.cy <= 0)
                continue;

            /* Same quiescence discipline as the kernel SYNC pass: sync a
             * window's BACK->FRONT only when it is not mid-paint, so a
             * half-drawn backing is never presented. Busy / first-paint windows
             * keep their last complete FRONT. dwm always receives the FRONT —
             * never a live backing — exactly like the in-kernel compositor. */
            {
                BOOL bBusy = (e->PaintCount > 0) &&
                             (now - e->PaintStart) < COMPOSITION_PAINT_STALE_100NS;
                BOOL bFirstPaintPending = !e->EverPainted && (w->hrgnUpdate != NULL);

                if (bFirstPaintPending)
                {
                    if (e->FirstPendingSeen == 0)
                        e->FirstPendingSeen = now;
                    else if ((now - e->FirstPendingSeen) >= COMPOSITION_PAINT_STALE_100NS)
                        bFirstPaintPending = FALSE;
                }
                else
                {
                    e->FirstPendingSeen = 0;
                }

                if (!bBusy && !bFirstPaintPending &&
                    e->Redirect.psurf != NULL && e->Redirect.psurfFront != NULL)
                {
                    RECTL rcBuf;
                    POINTL ptZero = {0, 0};
                    rcBuf.left = 0;
                    rcBuf.top = 0;
                    rcBuf.right = e->Redirect.cx;
                    rcBuf.bottom = e->Redirect.cy;
                    IntEngBitBlt(&e->Redirect.psurfFront->SurfObj,
                                 &e->Redirect.psurf->SurfObj,
                                 NULL, NULL, NULL, &rcBuf, &ptZero,
                                 NULL, NULL, NULL, ROP4_SRCCOPY);
                    e->Redirect.FrontValid = TRUE;
                }
            }

            /* Present the FRONT (a complete frame); skip until one exists.
             * The pixels stay in the window's section — emit only metadata. */
            if (!e->Redirect.FrontValid || e->Redirect.psurfFront == NULL ||
                e->Redirect.FrontSection == NULL)
                continue;

            wins[count].x = w->rcWindow.left;
            wins[count].y = w->rcWindow.top;
            wins[count].cx = e->Redirect.cx;
            wins[count].cy = e->Redirect.cy;
            wins[count].SurfaceId = (ULONG)(e - g_Redirects);
            wins[count].Generation = e->Redirect.Generation;
            wins[count].Stride = (ULONG)e->Redirect.cx * 4;
            wins[count].Damaged = e->Damaged ? 1 : 0;
            {
                /* Defaults hold when the window is not layered (GetLayered
                 * leaves the outputs untouched then). */
                BYTE alpha = 255;
                COLORREF key = 0;
                DWORD lf = 0;
                IntCompositionGetLayered(w, &alpha, &key, &lf);
                wins[count].Alpha = alpha;
                wins[count].ColorKey = (ULONG)key;
                wins[count].LayerFlags = lf;
            }
            count++;

            /* Union the changed windows' rects for a damage-scoped present. */
            if (e->Damaged)
                RECTL_bUnionRect(&rcDmg, &rcDmg, (RECTL *)&w->rcWindow);
            e->Damaged = FALSE;
        }

        hdr->ScreenW = (ULONG)sizl.cx;
        hdr->ScreenH = (ULONG)sizl.cy;
        hdr->Count = count;
        hdr->FullDamage = g_CompositionFullDamage ? 1 : 0;
        hdr->Dirty = 1;
        hdr->WinArrayBase = DWM_WINARRAY_BASE;
        hdr->DmgL = rcDmg.left;
        hdr->DmgT = rcDmg.top;
        hdr->DmgR = rcDmg.right;
        hdr->DmgB = rcDmg.bottom;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    g_CompositionFullDamage = FALSE;
    g_CompositionDamaged = FALSE;
    g_DwmLastFrameTime = (LONGLONG)KeQueryInterruptTime();
    return Status;
}

/* Create a synchronization event in the caller's handle table and return a
 * referenced pointer for kernel-side signaling. Both NULL on failure. */
static HANDLE
IntCompositionCreateDwmEvent(_Out_ PKEVENT *ppEvent)
{
    OBJECT_ATTRIBUTES oa;
    HANDLE hEvent = NULL;
    PKEVENT pkev = NULL;
    NTSTATUS Status;

    *ppEvent = NULL;

    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    Status = ZwCreateEvent(&hEvent, EVENT_ALL_ACCESS, &oa,
                           SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(Status))
        return NULL;

    Status = ObReferenceObjectByHandle(hEvent, EVENT_MODIFY_STATE,
                                       *ExEventObjectType, UserMode,
                                       (PVOID *)&pkev, NULL);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(hEvent);
        return NULL;
    }

    *ppEvent = pkev;
    return hEvent;
}

/* Register (or clear, Event == NULL) dwm's vblank event with the display
 * path: the dxgkrnl present timer — the scanout cadence — signals it. */
static VOID
IntCompositionRegisterVblank(_In_opt_ PKEVENT Event)
{
    ULONGLONG payload = (ULONGLONG)(ULONG_PTR)Event;

    IntCompositionDriverEscape(CDD_ESCAPE_REGISTER_VBLANK,
                               &payload, sizeof(payload));
}

/* Release the attached dwm: unregister the vblank event from the display path
 * (before its reference goes away), drop both event references, clear the
 * compositor identity. */
static VOID
IntCompositionDwmTeardown(VOID)
{
    IntCompositionRegisterVblank(NULL);
    if (g_DwmVblankEvent != NULL)
    {
        ObDereferenceObject(g_DwmVblankEvent);
        g_DwmVblankEvent = NULL;
    }
    if (g_DwmWakeEvent != NULL)
    {
        ObDereferenceObject(g_DwmWakeEvent);
        g_DwmWakeEvent = NULL;
    }
    g_DwmAttached = FALSE;
    g_DwmProcess = NULL;
}

/*
 * dwm.exe attach/detach (ONEPARAM_ROUTINE_DWMATTACH, DWM_ATTACH exchange).
 * On attach, creates two events in the caller's (dwm's) handle table and
 * keeps referenced pointers: the damage wake event (signaled by the damage
 * paths so dwm blocks instead of polling when idle) and the vblank event
 * (signaled by the display path every scanout period for frame pacing).
 * Detach/re-attach unregisters and releases the previous references.
 */
NTSTATUS
IntCompositionDwmAttach(_In_ PVOID pUser)
{
#if defined(REACTOS_GRAPHICS_DRIVER_MODEL_XPDM)
    /* No composition on the legacy model: XPDM drivers draw direct. */
    UNREFERENCED_PARAMETER(pUser);
    return STATUS_NOT_SUPPORTED;
#else
    DWM_ATTACH req;
    HANDLE hWake = NULL, hVblank = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        ProbeForWrite(pUser, sizeof(DWM_ATTACH), sizeof(ULONG));
        req = *(PDWM_ATTACH)pUser;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* Tear down the previous attachment. */
    IntCompositionDwmTeardown();

    if (req.Attach)
    {
        hWake = IntCompositionCreateDwmEvent(&g_DwmWakeEvent);
        hVblank = IntCompositionCreateDwmEvent(&g_DwmVblankEvent);
        if (g_DwmVblankEvent != NULL)
            IntCompositionRegisterVblank(g_DwmVblankEvent);

        g_DwmProcess = PsGetCurrentProcess();
        g_DwmAttached = TRUE;
        g_DwmLastFrameTime = (LONGLONG)KeQueryInterruptTime();

        /* The compositor now owns the frame: turn redirection on, retarget
         * every live DC (held GL DCs included) into window backings, and
         * repaint the desktop so each window fills its fresh backing. */
        IntCompositionSetEnabled(TRUE);
        DceRedirectAllDCs();
        IntCompositionDamageFromGdi();
    }
    else
    {
        /* No compositor: back to classic direct drawing. */
        IntCompositionSetEnabled(FALSE);
    }

    {
        PWND pwndDesktop = UserGetDesktopWindow();
        if (pwndDesktop != NULL)
            co_UserRedrawWindow(pwndDesktop, NULL, NULL,
                                RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
                                RDW_ALLCHILDREN);
    }

    _SEH2_TRY
    {
        ((PDWM_ATTACH)pUser)->hWake = hWake;
        ((PDWM_ATTACH)pUser)->hVblank = hVblank;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
#endif
}

/*
 * DWMOPENSURFACE: open a read-only handle to a window's FRONT section in
 * dwm's process (see DWM_OPEN_SURFACE in dwmframe.h). The (SurfaceId,
 * Generation) pair comes from the frame metadata; a stale generation means
 * the surface was recreated since that frame — dwm re-pulls.
 */
NTSTATUS
IntCompositionDwmOpenSurface(_In_ PVOID pUser)
{
    DWM_OPEN_SURFACE req;
    REDIRECT_ENTRY *e;
    HANDLE hSection = NULL;
    NTSTATUS Status;

    if (!gbCompositionEnabled)
        return STATUS_DEVICE_NOT_READY;
    if (PsGetCurrentProcess() != g_DwmProcess)
        return STATUS_ACCESS_DENIED;

    _SEH2_TRY
    {
        ProbeForWrite(pUser, sizeof(DWM_OPEN_SURFACE), sizeof(ULONG));
        req = *(PDWM_OPEN_SURFACE)pUser;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (req.SurfaceId >= g_RedirectHighWater)
        return STATUS_INVALID_PARAMETER;

    e = &g_Redirects[req.SurfaceId];
    if (e->Wnd == NULL ||
        e->Redirect.Generation != req.Generation ||
        e->Redirect.FrontSection == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = ObOpenObjectByPointer(e->Redirect.FrontSection, 0, NULL,
                                   SECTION_MAP_READ, MmSectionObjectType,
                                   UserMode, &hSection);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        ((PDWM_OPEN_SURFACE)pUser)->hSection = hSection;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        ObCloseHandle(hSection, UserMode);
    return Status;
}

/*
 * CDD present bracket for dwm (ONEPARAM_ROUTINE_DWMPRESENTSYNC): value=1 opens
 * the bracket before dwm's BitBlt to the primary, value=0 closes it after — so
 * the dxgkrnl present worker never scans out a half-composed primary. Same
 * CDD_ESCAPE_COMPOSITION_SYNC the in-kernel BLIT pass uses.
 */
VOID
IntCompositionDwmSync(_In_ LONG value)
{
    if (PsGetCurrentProcess() != g_DwmProcess)
        return;

    /* Suppress self-damage from dwm's presenting thread only. */
    g_DwmPresentThread = value ? (PVOID)KeGetCurrentThread() : NULL;

    IntCompositionDriverEscape(CDD_ESCAPE_COMPOSITION_SYNC,
                               &value, sizeof(LONG));
}


/* Subtract pwnd's window rect (narrowed by its window region, if any) from
 * VisRgn — the sibling/child occlusion step of the classic DCE clipping. */
static VOID
IntCompositionClipOutWindow(_Inout_ PREGION VisRgn, _In_ PWND pwnd)
{
    PREGION ClipRgn = IntSysCreateRectpRgnIndirect(&pwnd->rcWindow);

    if (ClipRgn == NULL)
        return;

    if (pwnd->hrgnClip && !(pwnd->style & WS_MINIMIZE))
    {
        PREGION WndClipRgn = REGION_LockRgn(pwnd->hrgnClip);
        if (WndClipRgn)
        {
            REGION_bOffsetRgn(ClipRgn,
                              -pwnd->rcWindow.left,
                              -pwnd->rcWindow.top);
            IntGdiCombineRgn(ClipRgn, ClipRgn, WndClipRgn, RGN_AND);
            REGION_bOffsetRgn(ClipRgn,
                              pwnd->rcWindow.left,
                              pwnd->rcWindow.top);
            REGION_UnlockRgn(WndClipRgn);
        }
    }
    IntGdiCombineRgn(VisRgn, VisRgn, ClipRgn, RGN_DIFF);
    REGION_Delete(ClipRgn);
}

/*
 * Classic DCE visibility semantics, bounded at the top-level ancestor: clip by
 * ancestor client areas, siblings above (per WS_CLIPSIBLINGS / DCX_CLIPSIBLINGS),
 * children (DCX_CLIPCHILDREN) and window regions exactly like
 * VIS_ComputeVisibleRegion, but STOP the parent walk at the top-level window -
 * occlusion by other top-level windows must not clip a redirected paint. NULL
 * means "clipped to nothing" (hidden window) - callers substitute an empty rgn.
 */
static PREGION
IntCompositionVisRgnBounded(
    _In_ PWND Wnd,
    _In_ PWND Ancestor,
    _In_ BOOL ClientArea,
    _In_ BOOL ClipChildren,
    _In_ BOOL ClipSiblings)
{
    PREGION VisRgn, ClipRgn;
    PWND PreviousWindow, CurrentWindow, CurrentSibling;

    if (Wnd == NULL || !(Wnd->style & WS_VISIBLE))
        return NULL;

    VisRgn = IntSysCreateRectpRgnIndirect(ClientArea ? (PRECTL)&Wnd->rcClient
                                                     : (PRECTL)&Wnd->rcWindow);
    if (VisRgn == NULL)
        return NULL;

    PreviousWindow = Wnd;
    CurrentWindow = Wnd->spwndParent;
    while (CurrentWindow != NULL && PreviousWindow != Ancestor)
    {
        if (!VerifyWnd(CurrentWindow) || !(CurrentWindow->style & WS_VISIBLE))
        {
            REGION_Delete(VisRgn);
            return NULL;
        }

        ClipRgn = IntSysCreateRectpRgnIndirect(&CurrentWindow->rcClient);
        if (ClipRgn != NULL)
        {
            IntGdiCombineRgn(VisRgn, VisRgn, ClipRgn, RGN_AND);
            REGION_Delete(ClipRgn);
        }

        if ((PreviousWindow->style & WS_CLIPSIBLINGS) ||
            (PreviousWindow == Wnd && ClipSiblings))
        {
            CurrentSibling = CurrentWindow->spwndChild;
            while (CurrentSibling != NULL && CurrentSibling != PreviousWindow)
            {
                if ((CurrentSibling->style & WS_VISIBLE) &&
                    !(CurrentSibling->ExStyle & WS_EX_TRANSPARENT))
                {
                    IntCompositionClipOutWindow(VisRgn, CurrentSibling);
                }
                CurrentSibling = CurrentSibling->spwndNext;
            }
        }

        PreviousWindow = CurrentWindow;
        CurrentWindow = CurrentWindow->spwndParent;
    }

    if (ClipChildren)
    {
        CurrentWindow = Wnd->spwndChild;
        while (CurrentWindow != NULL)
        {
            if ((CurrentWindow->style & WS_VISIBLE) &&
                !(CurrentWindow->ExStyle & WS_EX_TRANSPARENT))
            {
                IntCompositionClipOutWindow(VisRgn, CurrentWindow);
            }
            CurrentWindow = CurrentWindow->spwndNext;
        }
    }

    if (Wnd->hrgnClip && !(Wnd->style & WS_MINIMIZE))
    {
        PREGION WndClipRgn = REGION_LockRgn(Wnd->hrgnClip);
        if (WndClipRgn)
        {
            REGION_bOffsetRgn(VisRgn, -Wnd->rcWindow.left, -Wnd->rcWindow.top);
            IntGdiCombineRgn(VisRgn, VisRgn, WndClipRgn, RGN_AND);
            REGION_bOffsetRgn(VisRgn, Wnd->rcWindow.left, Wnd->rcWindow.top);
            REGION_UnlockRgn(WndClipRgn);
        }
    }

    return VisRgn;
}

/* Restore a locked, redirected DC to the PDEV primary (mirrors DC_vUpdateDC). */
static VOID
IntCompositionRestoreDC(_Inout_ PDC pdc)
{
    if (pdc->dclevel.pSurface != NULL)
        SURFACE_ShareUnlockSurface(pdc->dclevel.pSurface);
    pdc->dclevel.pSurface = PDEVOBJ_pSurface(pdc->ppdev);
    PDEVOBJ_sizl(pdc->ppdev, &pdc->dclevel.sizl);
    pdc->dctype = DCTYPE_DIRECT;
    pdc->fs &= ~DC_REDIRECTION;
}

/*
 * Redirect a window DC so drawing lands in the top-level ancestor's backing
 * surface instead of the primary. Idempotent: origin and clip are derived
 * absolutely from the window rects, so re-running it (UserGetDCEx reuse,
 * DceResetActiveDCEs after a move/resize) always yields the current state.
 */
VOID
IntCompositionRedirectDC(_In_opt_ PWND Wnd, _In_ HDC hDC, _In_ ULONG DcxFlags, _In_opt_ HRGN hrgnClip, _In_ BOOL Revalidate)
{
    PWND ancestor = NULL;
    REDIRECT_ENTRY *e = NULL;
    PDC pdc;
    RECTL rcOwn, rcSurf;
    PREGION prgn;
    BOOL bRedirect = FALSE;

    if (gbCompositionEnabled && Wnd != NULL)
    {
        ancestor = IntCompositionTopLevel(Wnd);
        if (ancestor != NULL)
        {
            e = IntCompositionFind(ancestor);
            if (e == NULL || e->Redirect.psurf == NULL)
            {
                /* The backing may not exist yet — GL apps fetch their DC (and
                 * bind the GL context to it) before the window is shown, so
                 * the create/show hooks haven't allocated one. Create it here
                 * even for a still-HIDDEN window (IsCompositable refuses
                 * those, which broke this very path): the redirect must be
                 * live before the app's first present, or a held CS_OWNDC DC
                 * keeps drawing into a direct DC with a stale VisRgn after
                 * the window is shown — an eternally black GL window. */
                if (ancestor->head.pti != NULL &&
                    ancestor->spwndParent == UserGetDesktopWindow() &&
                    ancestor != UserGetDesktopWindow())
                {
                    if (e == NULL)
                        e = IntCompositionAlloc(ancestor);
                    if (e != NULL)
                    {
                        IntCompositionEnsureSurface(ancestor, &e->Redirect);
                        IntCompositionDamageWindow(ancestor);
                    }
                }
            }
            bRedirect = (e != NULL && e->Redirect.psurf != NULL);
        }
    }

    pdc = DC_LockDc(hDC);
    if (pdc == NULL)
        return;

    if (!bRedirect)
    {
        /* A reused DC that was previously redirected (recycled cache entry,
         * or composition disabled meanwhile): restore the primary so the
         * backing reference is not stranded. */
        if (pdc->fs & DC_REDIRECTION)
            IntCompositionRestoreDC(pdc);
        DC_UnlockDc(pdc);
        return;
    }

    if (pdc->dctype != DCTYPE_DIRECT)
    {
        DC_UnlockDc(pdc);
        return;
    }

    /*
     * Surface-level redirect: keep the DC a DIRECT *window* DC and swap only
     * the surface it draws into. The DC_REDIRECTION flag makes
     * DC_vPrepareDCsForBlit skip the DC_vUpdateDC surface-revert (dclife.c),
     * so the backing sticks across blits. The origin maps the window/client
     * top-left from screen space to backing space, absolutely (idempotent).
     */
    rcOwn = (DcxFlags & DCX_WINDOW) ? Wnd->rcWindow : Wnd->rcClient;

    /* Cached DCE with a still-valid clip (no DCHF_VALIDATEVISRGN, no new clip
     * region) already redirected at the current backing and origin: the VisRgn
     * rebuild below would recompute the identical region — the dominant
     * per-GetDC cost on paint-heavy trees. Skip it. */
    if (!Revalidate &&
        (pdc->fs & DC_REDIRECTION) &&
        pdc->dclevel.pSurface == e->Redirect.psurf &&
        pdc->ptlDCOrig.x == rcOwn.left - ancestor->rcWindow.left &&
        pdc->ptlDCOrig.y == rcOwn.top - ancestor->rcWindow.top)
    {
        DC_UnlockDc(pdc);
        return;
    }

    DC_vSelectSurface(pdc, e->Redirect.psurf);
    pdc->ptlDCOrig.x = rcOwn.left - ancestor->rcWindow.left;
    pdc->ptlDCOrig.y = rcOwn.top - ancestor->rcWindow.top;
    pdc->dclevel.sizl.cx = e->Redirect.cx;
    pdc->dclevel.sizl.cy = e->Redirect.cy;
    pdc->fs |= DC_REDIRECTION;
    DC_UnlockDc(pdc);

    /*
     * VisRgn: classic DCE clipping (children/siblings/parent-clip/window
     * region), computed WITHIN the top-level ancestor - occlusion by other
     * top-level windows must not clip a redirected paint (every window renders
     * fully into its backing; the compositor handles overlap), but sibling and
     * child clipping keep their classic meaning: a lower sibling's erase (tab
     * control under a property-sheet page) must not wipe an upper sibling's
     * already-rendered content out of the shared backing.
     */
    if (DcxFlags & DCX_PARENTCLIP)
    {
        PWND parent = Wnd->spwndParent;
        if (parent != NULL)
            prgn = IntCompositionVisRgnBounded(parent, ancestor, TRUE, FALSE,
                                               (parent->style & WS_CLIPSIBLINGS) != 0);
        else
            prgn = NULL;
    }
    else
    {
        prgn = IntCompositionVisRgnBounded(Wnd, ancestor,
                                           !(DcxFlags & DCX_WINDOW),
                                           (DcxFlags & DCX_CLIPCHILDREN) != 0,
                                           (DcxFlags & DCX_CLIPSIBLINGS) != 0);
    }

    if (prgn == NULL)
        prgn = IntSysCreateRectpRgn(0, 0, 0, 0);
    if (prgn == NULL)
        return;

    /* Screen -> backing coordinates, clamped to the backing extent. */
    REGION_bOffsetRgn(prgn, -ancestor->rcWindow.left, -ancestor->rcWindow.top);
    rcSurf.left = 0;
    rcSurf.top = 0;
    rcSurf.right = e->Redirect.cx;
    rcSurf.bottom = e->Redirect.cy;
    {
        PREGION prgnBounds = IntSysCreateRectpRgnIndirect(&rcSurf);
        if (prgnBounds != NULL)
        {
            IntGdiCombineRgn(prgn, prgn, prgnBounds, RGN_AND);
            REGION_Delete(prgnBounds);
        }
    }

    if (DcxFlags & (DCX_INTERSECTRGN | DCX_EXCLUDERGN))
    {
        PREGION prgnClip = NULL;

        if (hrgnClip != NULL && hrgnClip != HRGN_WINDOW)
            prgnClip = REGION_LockRgn(hrgnClip);

        if (prgnClip != NULL)
        {
            /* The DCE clip region is in screen coordinates; bring it into
             * backing coordinates before combining. */
            PREGION prgnTmp = IntSysCreateRectpRgn(0, 0, 0, 0);
            if (prgnTmp != NULL)
            {
                IntGdiCombineRgn(prgnTmp, prgnClip, NULL, RGN_COPY);
                REGION_bOffsetRgn(prgnTmp,
                                  -ancestor->rcWindow.left,
                                  -ancestor->rcWindow.top);
                IntGdiCombineRgn(prgn, prgn, prgnTmp,
                                 (DcxFlags & DCX_INTERSECTRGN) ? RGN_AND : RGN_DIFF);
                REGION_Delete(prgnTmp);
            }
            REGION_UnlockRgn(prgnClip);
        }
        else if (DcxFlags & DCX_INTERSECTRGN)
        {
            /* Intersect with nothing: empty, like DceUpdateVisRgn. */
            REGION_SetRectRgn(prgn, 0, 0, 0, 0);
        }
    }

    /* LockWindowUpdate suppression applies to redirected paints too. */
    if (IntIsLockUpdateSuppressed(Wnd, DcxFlags))
        REGION_SetRectRgn(prgn, 0, 0, 0, 0);

    GdiSelectVisRgn(hDC, prgn);
    REGION_Delete(prgn);
}

/*
 * Compositor liveness watchdog, called from the message pump (a periodic
 * PASSIVE context that holds the USER lock). There is no kernel compositor
 * to fall back to: if the attached dwm goes silent, redirection is torn
 * down and the desktop reverts to classic direct drawing (then repaints).
 */
VOID
IntCompositionWatchdog(VOID)
{
    if (!g_DwmAttached || g_DwmProcess == NULL)
        return;
    if ((LONGLONG)KeQueryInterruptTime() - g_DwmLastFrameTime <= (20LL * 1000000LL))
        return; /* alive within 2 s */

    ERR("DWM went silent - tearing down composition, reverting to direct draw\n");

    IntCompositionDwmTeardown();
    IntCompositionSetEnabled(FALSE);

    {
        PWND pwndDesktop = UserGetDesktopWindow();
        if (pwndDesktop != NULL)
            co_UserRedrawWindow(pwndDesktop, NULL, NULL,
                                RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
                                RDW_ALLCHILDREN);
    }
}

/*
 * Undo a DC redirect: release the backing-surface ref and restore the PDEV
 * primary. Called when a cache DC is released/recycled and when a window's
 * DCEs are freed. Deliberately NOT gated on gbCompositionEnabled: a redirect
 * applied before composition was disabled must still be undone.
 */
VOID
IntCompositionUnredirectDC(_In_ HDC hDC)
{
    PDC pdc;

    pdc = DC_LockDc(hDC);
    if (pdc == NULL)
        return;

    if (pdc->fs & DC_REDIRECTION)
        IntCompositionRestoreDC(pdc);

    DC_UnlockDc(pdc);
}

NTSTATUS
IntCompositionSetEnabled(_In_ BOOL bEnable)
{
    if (bEnable == gbCompositionEnabled)
        return STATUS_SUCCESS;

    gbCompositionEnabled = bEnable;

    if (bEnable)
    {
        g_CompositionFullDamage = TRUE;
        g_CompositionDamaged = TRUE;
    }
    else
    {
        ULONG i;
        DceUnredirectAllDCs();
        for (i = 0; i < COMPOSITION_MAX_WINDOWS; i++)
        {
            if (g_Redirects[i].Wnd != NULL)
            {
                IntCompositionFreeSurface(&g_Redirects[i].Redirect);
                g_Redirects[i].Wnd = NULL;
            }
        }
        for (i = 0; i < COMPOSITION_MAX_GL; i++)
            g_GlWindows[i] = NULL;
    }

    return STATUS_SUCCESS;
}
