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
#include "dcomposition.h"
#include <reactos/dwmframe.h>
#if defined(REACTOS_GRAPHICS_DRIVER_MODEL_WDDM)
#include "drivers/wddm/wddm_bridge.h"
#endif
DBG_DEFAULT_CHANNEL(UserPainting);

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
/* A paint bracket older than this composes anyway
 * (a hung painter must not freeze its window forever). */
#define COMPOSITION_PAINT_STALE_100NS (100LL * 10000LL) /* 100 ms */

typedef struct _REDIRECT_ENTRY
{
    PWND         Wnd;
    WND_REDIRECT Redirect;
    RECTL        WindowRect;   /* last position submitted to the compositor    */
    BOOL         WindowRectValid;
    LONG         PaintCount;   /* BeginPaint..EndPaint depth on this tree      */
    LONGLONG     PaintStart;   /* when the outer paint bracket opened          */
    BOOL         Damaged;      /* this window's backing changed since compose  */
    volatile LONG BackingDrawn;/* at least one GDI operation reached backing   */
    volatile LONG BackComplete;/* a complete GL client frame reached BACK      */
    PRECTL       BlurRects;     /* window-relative DwmEnableBlur region         */
    ULONG        BlurRectCount;
    ULONG        BlurFlags;     /* DWM_BLUR_*                                    */
    BOOL         Minimized;
    BOOL         Maximized;
    BOOL         MinRectValid;
    RECTL        MinRect;
    ULONG        AnimFlags;
    LONGLONG     AnimStart;
    LONGLONG     AnimDuration;
    RECTL        AnimRect;
    RECTL        AnimTarget;
    POINTL       AnimAnchor;
    RECTL        AnimDamage;
} REDIRECT_ENTRY;

#define COMPOSITION_ANIM_SCALE          65536
#define COMPOSITION_ANIM_MINIMIZE_100NS (200LL * 10000LL)
#define COMPOSITION_ANIM_RESTORE_100NS  (250LL * 10000LL)

static REDIRECT_ENTRY  g_Redirects[COMPOSITION_MAX_WINDOWS];
static ULONG           g_RedirectHighWater = 0;
static volatile LONG   g_CompositionDamaged = FALSE;
static volatile LONG   g_CompositionFullDamage = FALSE;
/* Position changes run under the exclusive USER lock, as does GETFRAME.
 * Retain the vacated and destination bounds until the compositor consumes
 * them; repainting only the new bounds leaves historical window copies. */
static RECTL           g_CompositionPositionDamage;
static BOOL            g_CompositionPositionDamageValid = FALSE;
static DWM_WIN         g_DwmFrameWindows[DWM_MAX_WINDOWS];
static RECTL           g_DwmFrameBlurRects[DWM_MAX_BLUR_RECTS];

/* dwm.exe is the ONLY compositor (Windows model — win32k tracks redirection
 * and damage, never composes). Attach enables redirection; detach or a
 * silent dwm (watchdog on g_DwmLastFrameTime, bumped by every GetFrame)
 * disables it and the desktop reverts to classic direct drawing. */
volatile BOOL          g_DwmAttached = FALSE;
volatile LONGLONG      g_DwmLastFrameTime = 0;
/* Damage wake event for dwm (referenced from dwm's handle), so dwm blocks
 * instead of polling when the desktop is idle. */
static PKEVENT         g_DwmWakeEvent = NULL;
/* The attached compositor process: every DWM entry point is rejected for
 * anyone else (frame metadata + window surfaces are session-global state). */
static PEPROCESS       g_DwmProcess = NULL;
static volatile PVOID  g_DwmGpuOutputWindow = NULL;
static volatile LONG   g_DwmGpuOutputWidth;
static volatile LONG   g_DwmGpuOutputHeight;
/* USER lock protects the claim tuple. Keep it separately from the published
 * HWND so forced teardown can disable presents and retry a failed release.
 * The PID is an identity already held by dxgkrnl, not a process to reopen. */
typedef struct _DWM_GPU_OUTPUT_OWNER
{
    LUID AdapterLuid;
    ULONG VidPnSourceId;
    HANDLE ProcessId;
    ULONG_PTR Window;
    ULONGLONG Generation;
} DWM_GPU_OUTPUT_OWNER;

static DWM_GPU_OUTPUT_OWNER g_DwmGpuOutputOwner;
static ULONGLONG g_DwmGpuOutputSequence;

static VOID
IntCompositionClearGpuOutputWindow(VOID)
{
    InterlockedExchangePointer(
        (PVOID volatile *)&g_DwmGpuOutputWindow, NULL);
    InterlockedExchange(&g_DwmGpuOutputWidth, 0);
    InterlockedExchange(&g_DwmGpuOutputHeight, 0);
}

static NTSTATUS
IntCompositionReleaseGpuOutput(_In_ BOOL Force)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (g_DwmGpuOutputOwner.Generation != 0)
    {
#if defined(REACTOS_GRAPHICS_DRIVER_MODEL_WDDM)
        Status = WddmBridgeSetCompositorSourceOwner(
                     &g_DwmGpuOutputOwner.AdapterLuid,
                     g_DwmGpuOutputOwner.VidPnSourceId,
                     g_DwmGpuOutputOwner.ProcessId,
                     g_DwmGpuOutputOwner.Window,
                     g_DwmGpuOutputOwner.Generation, 0, 0, FALSE);
#else
        Status = STATUS_NOT_SUPPORTED;
#endif
        /* Device/process rundown may have retired this exact generation.
         * Ownership release never acknowledges a GPU surface read. */
        if (Status == STATUS_NOT_FOUND)
            Status = STATUS_SUCCESS;
        if (NT_SUCCESS(Status))
            RtlZeroMemory(&g_DwmGpuOutputOwner, sizeof(g_DwmGpuOutputOwner));
    }

    if (NT_SUCCESS(Status) || Force)
        IntCompositionClearGpuOutputWindow();
    return Status;
}

static NTSTATUS
IntCompositionClaimGpuOutput(_In_ const DWM_GPU_OUTPUT *Request)
{
#if defined(REACTOS_GRAPHICS_DRIVER_MODEL_WDDM)
    DWM_GPU_OUTPUT_OWNER Owner;
    NTSTATUS Status;

    if (g_DwmGpuOutputWindow != NULL)
    {
        if ((ULONG_PTR)g_DwmGpuOutputWindow == (ULONG_PTR)Request->Window &&
            g_DwmGpuOutputWidth == (LONG)Request->Width &&
            g_DwmGpuOutputHeight == (LONG)Request->Height)
        {
            return STATUS_SUCCESS;
        }
        return STATUS_DEVICE_BUSY;
    }

    /* Finish an earlier forced teardown before admitting another output.
     * Its adapter/PID must remain the saved values across topology changes. */
    Status = IntCompositionReleaseGpuOutput(FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    if (g_DwmGpuOutputSequence == MAXULONGLONG)
        return STATUS_INTEGER_OVERFLOW;

    RtlZeroMemory(&Owner, sizeof(Owner));
    Status = WddmBridgeQueryPrimarySource(&Owner.AdapterLuid,
                                         &Owner.VidPnSourceId);
    if (!NT_SUCCESS(Status))
        return Status;
    Owner.ProcessId = PsGetProcessId(g_DwmProcess);
    Owner.Window = (ULONG_PTR)Request->Window;
    Owner.Generation = ++g_DwmGpuOutputSequence;
    Status = WddmBridgeSetCompositorSourceOwner(
                 &Owner.AdapterLuid, Owner.VidPnSourceId, Owner.ProcessId,
                 Owner.Window, Owner.Generation,
                 Request->Width, Request->Height, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    g_DwmGpuOutputOwner = Owner;
    InterlockedExchange(&g_DwmGpuOutputWidth, (LONG)Request->Width);
    InterlockedExchange(&g_DwmGpuOutputHeight, (LONG)Request->Height);
    KeMemoryBarrier();
    InterlockedExchangePointer((PVOID volatile *)&g_DwmGpuOutputWindow,
                               (PVOID)Owner.Window);
    return STATUS_SUCCESS;
#else
    UNREFERENCED_PARAMETER(Request);
    return STATUS_NOT_SUPPORTED;
#endif
}

/* Native producers retain their GPU allocations until these events signal.
 * Keep publications independent of WND lifetime: destroying a window does
 * not finish a read from a frame which DWM has already received. */
typedef struct _DWM_DX_PUBLICATION
{
    PKEVENT ReadyEvent;
    PEPROCESS Compositor;
    ULONG SurfaceId;
    ULONG Generation;
    ULONGLONG UpdateId;
    BOOL Retired;
} DWM_DX_PUBLICATION;

static DWM_DX_PUBLICATION g_DxPublications[COMPOSITION_MAX_WINDOWS];
/* Scanout pacing event (referenced; also registered with the display path,
 * whose present timer signals it every period). */

static BOOL IntCompositionTreeHasPendingPaint(_In_ PWND Root);

static VOID
IntCompositionCompleteDxPublication(_Inout_ DWM_DX_PUBLICATION *Publication)
{
    if (Publication->SurfaceId < g_RedirectHighWater)
    {
        PWND_REDIRECT Redirect = &g_Redirects[Publication->SurfaceId].Redirect;
        if (Redirect->DxGeneration == Publication->Generation &&
            Redirect->DxPublishedUpdateId == Publication->UpdateId)
        {
            Redirect->DxConsumedUpdateId = Publication->UpdateId;
        }
    }
    KeSetEvent(Publication->ReadyEvent, IO_NO_INCREMENT, FALSE);
    ObDereferenceObject(Publication->ReadyEvent);
    ObDereferenceObject(Publication->Compositor);
    RtlZeroMemory(Publication, sizeof(*Publication));
}

static VOID
IntCompositionDrainDxPublications(_In_ PEPROCESS Compositor, _In_ BOOL RetiredOnly)
{
    ULONG Index;
    for (Index = 0; Index < ARRAYSIZE(g_DxPublications); ++Index)
    {
        DWM_DX_PUBLICATION *Publication = &g_DxPublications[Index];
        if (Publication->ReadyEvent != NULL && Publication->Compositor == Compositor &&
            (!RetiredOnly || Publication->Retired))
        {
            IntCompositionCompleteDxPublication(Publication);
        }
    }
}

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
        InterlockedExchange(&g_CompositionFullDamage, TRUE);
    InterlockedExchange(&g_CompositionDamaged, TRUE);
    IntCompositionDwmWake();
}

static BOOL
IntCompositionAccumulatePositionDamage(_In_ const RECTL *Rect)
{
    if (Rect == NULL || Rect->left >= Rect->right || Rect->top >= Rect->bottom)
        return FALSE;

    if (g_CompositionPositionDamageValid)
    {
        RECTL_bUnionRect(&g_CompositionPositionDamage,
                         &g_CompositionPositionDamage,
                         (PRECTL)Rect);
    }
    else
    {
        g_CompositionPositionDamage = *Rect;
        g_CompositionPositionDamageValid = TRUE;
    }

    return TRUE;
}

/* A DX publication replaces client content in its own shared allocation.
 * Recompose its bounds without republishing the unchanged GDI BACK surface.
 * Keep pending GDI damage intact and initialize FRONT on the first frame.
 * Called under the same exclusive USER lock as GETFRAME. */
static VOID
IntCompositionDamageDxPublication(_Inout_ REDIRECT_ENTRY *Entry,
                                  _In_ PWND TopWnd)
{
    if (!Entry->Redirect.FrontValid ||
        !IntCompositionAccumulatePositionDamage((PRECTL)&TopWnd->rcWindow))
    {
        Entry->Damaged = TRUE;
    }
    IntCompositionMarkDamage(FALSE);
}

/* Hold the display PDEV across direct surface access. The same lock guards
 * normal GDI draws and pointer exclusion, so BACK snapshots cannot race a
 * writer or a software-cursor update. */
static PPDEVOBJ
IntCompositionReferenceDevice(VOID)
{
    PDC pdcScreen;
    PPDEVOBJ ppdev;

    if (ScreenDeviceContext == NULL)
        return NULL;

    pdcScreen = DC_LockDc(ScreenDeviceContext);
    if (pdcScreen == NULL)
        return NULL;

    ppdev = pdcScreen->ppdev;
    if (ppdev != NULL)
        PDEVOBJ_vReference(ppdev);
    DC_UnlockDc(pdcScreen);

    return ppdev;
}

static PPDEVOBJ
IntCompositionLockDevice(VOID)
{
    PPDEVOBJ ppdev = IntCompositionReferenceDevice();

    if (ppdev != NULL)
        EngAcquireSemaphore(ppdev->hsemDevLock);
    return ppdev;
}

static VOID
IntCompositionUnlockDevice(_In_opt_ PPDEVOBJ ppdev)
{
    if (ppdev == NULL)
        return;
    EngReleaseSemaphore(ppdev->hsemDevLock);
    PDEVOBJ_vRelease(ppdev);
}

static VOID
IntCompositionDereferenceDevice(_In_opt_ PPDEVOBJ ppdev)
{
    if (ppdev != NULL)
        PDEVOBJ_vRelease(ppdev);
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

static VOID
IntCompositionFreeBlur(_Inout_ REDIRECT_ENTRY *Entry)
{
    if (Entry->BlurRects != NULL)
        ExFreePoolWithTag(Entry->BlurRects, 'rBwD');
    Entry->BlurRects = NULL;
    Entry->BlurRectCount = 0;
    Entry->BlurFlags = 0;
}

/* A window is composited only if it is a visible top-level window. */
static BOOL
IntCompositionIsCompositable(_In_ PWND Wnd)
{
    if (Wnd == NULL || Wnd->head.pti == NULL)
        return FALSE;
    /* The registered fullscreen OpenGL window is DWM's final GPU scanout
     * carrier.  Redirecting or returning it in GETFRAME would make DWM
     * compose its own previous output and can also force the ICD back through
     * a CPU/GDI present. */
    if (IntCompositionIsGpuOutputWindow(Wnd))
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
IntCompositionFreeDxSurface(_Inout_ PWND_REDIRECT r)
{
    if (r->DxReadyEvent != NULL)
    {
        if (r->DxInfo.Version == DWM_DX_SURFACE_INFO_VERSION_GPU &&
            r->DxPublishedUpdateId > r->DxConsumedUpdateId)
        {
            ULONG Index;
            for (Index = 0; Index < ARRAYSIZE(g_DxPublications); ++Index)
            {
                DWM_DX_PUBLICATION *Publication = &g_DxPublications[Index];
                if (Publication->ReadyEvent == r->DxReadyEvent &&
                    Publication->Generation == r->DxGeneration &&
                    Publication->UpdateId == r->DxPublishedUpdateId)
                {
                    Publication->Retired = TRUE;
                    break;
                }
            }
        }
        else
            KeSetEvent(r->DxReadyEvent, IO_NO_INCREMENT, FALSE);
        ObDereferenceObject(r->DxReadyEvent);
        r->DxReadyEvent = NULL;
    }
    r->DxGlobalShare = 0;
    r->DxGeneration = 0;
    RtlZeroMemory(&r->DxAdapterLuid, sizeof(r->DxAdapterLuid));
    r->DxWindow = 0;
    r->DxClientX = 0;
    r->DxClientY = 0;
    r->DxIssuedUpdateId = 0;
    r->DxPublishedUpdateId = 0;
    r->DxConsumedUpdateId = 0;
    RtlZeroMemory(&r->DxInfo, sizeof(r->DxInfo));
}

static VOID
IntCompositionFreeSurface(_Inout_ PWND_REDIRECT r, _In_ BOOL PreserveDx)
{
    if (!PreserveDx)
        IntCompositionFreeDxSurface(r);
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
    if (r->BackView != NULL)
    {
        MmUnmapViewInSystemSpace(r->BackView);
        r->BackView = NULL;
    }
    if (r->BackSection != NULL)
    {
        ObDereferenceObject(r->BackSection);
        r->BackSection = NULL;
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
    r->BackViewSize = 0;
    r->BackGlobalShare = 0;
    r->FrontViewSize = 0;
    r->FrontGlobalShare = 0;
    r->BaseGeneration = 0;
    r->BaseUpdateId = 0;
    r->BasePreviousUpdateId = 0;
    RECTL_vSetEmptyRect(&r->BaseDirtyRect);
    RECTL_vSetEmptyRect(&r->BackDirtyRect);
    r->BackDirtyValid = FALSE;
    r->FrontValid = FALSE;
    r->GdiIssuedUpdateId = 0;
    r->GdiAdmittedUpdateId = 0;
    r->GdiPublishedUpdateId = 0;
    r->GdiConsumedUpdateId = 0;
    r->cx = r->cy = 0;
}

static BOOL
IntCompositionGetBufferSize(
    _In_ LONG cx,
    _In_ LONG cy,
    _Out_ PULONG pStride,
    _Out_ PULONG pBytes)
{
    ULONG Stride;

    if (cx <= 0 || cy <= 0 || (ULONG)cx > MAXULONG / sizeof(ULONG))
        return FALSE;

    Stride = (ULONG)cx * sizeof(ULONG);
    if ((ULONG)cy > MAXULONG / Stride)
        return FALSE;

    *pStride = Stride;
    *pBytes = Stride * (ULONG)cy;
    return TRUE;
}

/* Monotonic FRONT generation: a (slot, generation) pair never repeats, so
 * dwm's mapped-view cache can never alias a recycled surface. */
static ULONG g_FrontGeneration = 0;
static ULONGLONG g_DxUpdateSequence = 0;
static ULONGLONG g_GdiUpdateSequence = 0;
static ULONGLONG g_BaseUpdateSequence = 0;

/* FRONT buffer over a pageable section: win32k keeps a system-space view the
 * GDI surface wraps; dwm maps the same section read-only in user space and
 * composes straight from it — no per-frame pixel copies out of the kernel. */
static PSURFACE
IntCompositionCreateSharedBuffer(_In_ LONG cx, _In_ LONG cy,
                                 _Out_ HBITMAP *phbmp,
                                 _Out_ PVOID *ppSection,
                                 _Out_ PVOID *ppView,
                                 _Out_ SIZE_T *pcbView,
                                 _Out_ PULONG pGlobalShare)
{
    LARGE_INTEGER liSize;
    PVOID pSection = NULL, pView = NULL;
    SIZE_T cbView;
    PSURFACE psurf;
    HBITMAP hbmp;
    ULONG Stride, Bytes;
    NTSTATUS Status;
    PPDEVOBJ ppdev;

    *phbmp = NULL;
    *ppSection = NULL;
    *ppView = NULL;
    *pcbView = 0;
    *pGlobalShare = 0;

    if (!IntCompositionGetBufferSize(cx, cy, &Stride, &Bytes))
        return NULL;

    ppdev = IntCompositionReferenceDevice();
    if (ppdev != NULL && ppdev->DriverFunctions.CreateDeviceBitmapEx != NULL)
    {
        SIZEL Size = {cx, cy};
        HANDLE SharedSurface = NULL;

        hbmp = ppdev->DriverFunctions.CreateDeviceBitmapEx(
            ppdev->dhpdev, Size, BMF_32BPP, CDBEX_REDIRECTION, NULL,
            DWM_DX_FORMAT_B8G8R8A8_UNORM,
#if (NTDDI_VERSION >= NTDDI_WIN8)
            0,
#endif
            &SharedSurface);
        IntCompositionDereferenceDevice(ppdev);
        if (hbmp != NULL && SharedSurface != NULL)
        {
            psurf = SURFACE_ShareLockSurface(hbmp);
            if (psurf != NULL && psurf->SurfObj.pvScan0 != NULL &&
                psurf->SurfObj.lDelta >= (LONG)Stride &&
                psurf->SurfObj.cjBits >= Bytes)
            {
                *phbmp = hbmp;
                *pGlobalShare = (ULONG)(ULONG_PTR)SharedSurface;
                return psurf;
            }
            if (psurf != NULL)
                SURFACE_ShareUnlockSurface(psurf);
            EngDeleteSurface((HSURF)hbmp);
        }
        else if (hbmp != NULL)
        {
            EngDeleteSurface((HSURF)hbmp);
        }
    }
    else
    {
        IntCompositionDereferenceDevice(ppdev);
    }

    liSize.QuadPart = Bytes;
    Status = MmCreateSection(&pSection, SECTION_ALL_ACCESS, NULL, &liSize, PAGE_READWRITE, SEC_COMMIT, NULL, NULL);
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

    hbmp = GreCreateBitmapEx((ULONG)cx, (ULONG)cy, Stride, BMF_32BPP, BMF_TOPDOWN | BMF_NOZEROINIT, Bytes, pView, 0);
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
    PVOID pBackSectionNew = NULL, pBackViewNew = NULL;
    PVOID pFrontSectionNew = NULL, pFrontViewNew = NULL;
    SIZE_T cbBackViewNew = 0, cbFrontViewNew = 0;
    ULONG BackGlobalShareNew = 0, FrontGlobalShareNew = 0;
    RECTL rcCopy;
    POINTL ptZero = {0, 0};
    PPDEVOBJ ppdev = NULL;
    ULONG Stride, Bytes;

    if ((Wnd->style & WS_MINIMIZE) && r->psurf != NULL)
        return r->psurf;

    r->rcClient.left = max(Wnd->rcClient.left - Wnd->rcWindow.left, 0);
    r->rcClient.top = max(Wnd->rcClient.top - Wnd->rcWindow.top, 0);
    r->rcClient.right = min(Wnd->rcClient.right - Wnd->rcWindow.left, cx);
    r->rcClient.bottom = min(Wnd->rcClient.bottom - Wnd->rcWindow.top, cy);

    if (!IntCompositionGetBufferSize(cx, cy, &Stride, &Bytes))
        return NULL;

    UNREFERENCED_PARAMETER(Stride);
    UNREFERENCED_PARAMETER(Bytes);

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

    psurfNew = IntCompositionCreateSharedBuffer(cx, cy, &hbmpNew,
                                                &pBackSectionNew,
                                                &pBackViewNew,
                                                &cbBackViewNew,
                                                &BackGlobalShareNew);
    if (psurfNew == NULL)
    {
        r->AllocFailTime = (LONGLONG)KeQueryInterruptTime();
        return r->psurf;
    }
    r->AllocFailTime = 0;

    psurfNewFront = IntCompositionCreateSharedBuffer(cx, cy, &hbmpNewFront,
                                                     &pFrontSectionNew,
                                                     &pFrontViewNew,
                                                     &cbFrontViewNew,
                                                     &FrontGlobalShareNew);
    if (psurfNewFront == NULL)
    {
        SURFACE_ShareUnlockSurface(psurfNew);
        EngDeleteSurface((HSURF)hbmpNew);
        if (pBackViewNew != NULL)
            MmUnmapViewInSystemSpace(pBackViewNew);
        if (pBackSectionNew != NULL)
            ObDereferenceObject(pBackSectionNew);
        r->AllocFailTime = (LONGLONG)KeQueryInterruptTime();
        return r->psurf;
    }

    rcCopy.left = 0;
    rcCopy.top = 0;
    rcCopy.right = min(cx, r->cx);
    rcCopy.bottom = min(cy, r->cy);

    if (rcCopy.right > 0 && rcCopy.bottom > 0)
    {
        ppdev = IntCompositionLockDevice();
        if (ppdev == NULL)
        {
            WND_REDIRECT NewRedirect;
            RtlZeroMemory(&NewRedirect, sizeof(NewRedirect));
            NewRedirect.psurf = psurfNew;
            NewRedirect.hbmp = hbmpNew;
            NewRedirect.BackSection = pBackSectionNew;
            NewRedirect.BackView = pBackViewNew;
            NewRedirect.BackGlobalShare = BackGlobalShareNew;
            NewRedirect.psurfFront = psurfNewFront;
            NewRedirect.hbmpFront = hbmpNewFront;
            NewRedirect.FrontSection = pFrontSectionNew;
            NewRedirect.FrontView = pFrontViewNew;
            NewRedirect.FrontGlobalShare = FrontGlobalShareNew;
            IntCompositionFreeSurface(&NewRedirect, FALSE);
            r->AllocFailTime = (LONGLONG)KeQueryInterruptTime();
            return r->psurf;
        }
    }

    if (ppdev != NULL && r->psurf != NULL)
    {
        IntEngBitBlt(&psurfNew->SurfObj, &r->psurf->SurfObj, NULL, NULL, NULL, &rcCopy, &ptZero, NULL, NULL, NULL, ROP4_SRCCOPY);
    }
    if (ppdev != NULL && r->psurfFront != NULL && r->FrontValid)
    {
        IntEngBitBlt(&psurfNewFront->SurfObj, &r->psurfFront->SurfObj, NULL, NULL, NULL, &rcCopy, &ptZero, NULL, NULL, NULL, ROP4_SRCCOPY);
    }
    IntCompositionUnlockDevice(ppdev);

    {
        BOOL bFrontValid = r->FrontValid && (ppdev != NULL) && (r->psurfFront != NULL);
        /* A GDI resize does not complete an outstanding GPU client read.
         * Keep that publication and its consumed event until the producer
         * replaces or unregisters it after DWM's fence. */
        IntCompositionFreeSurface(r, TRUE);
        r->psurf = psurfNew;
        r->hbmp = hbmpNew;
        r->BackSection = pBackSectionNew;
        r->BackView = pBackViewNew;
        r->BackViewSize = cbBackViewNew;
        r->BackGeneration = ++g_FrontGeneration;
        r->BackGlobalShare = BackGlobalShareNew;
        r->psurfFront = psurfNewFront;
        r->hbmpFront = hbmpNewFront;
        r->FrontSection = pFrontSectionNew;
        r->FrontView = pFrontViewNew;
        r->FrontViewSize = cbFrontViewNew;
        r->Generation = ++g_FrontGeneration;
        r->FrontGlobalShare = FrontGlobalShareNew;
        r->BaseGeneration = FrontGlobalShareNew != 0 ? r->Generation : 0;
        r->BaseUpdateId = bFrontValid ? ++g_BaseUpdateSequence : 0;
        r->BaseDirtyRect.left = r->BaseDirtyRect.top = 0;
        r->BaseDirtyRect.right = cx;
        r->BaseDirtyRect.bottom = cy;
        r->BackDirtyRect = r->BaseDirtyRect;
        r->BackDirtyValid = TRUE;
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
    e->WindowRect = Wnd->rcWindow;
    e->WindowRectValid = TRUE;
    e->Minimized = (Wnd->style & WS_MINIMIZE) != 0;
    IntCompositionDamageWindow(Wnd);
}

VOID
IntCompositionOnWindowDestroy(_In_ PWND Wnd)
{
    REDIRECT_ENTRY *e;
    ULONG i;

    if (Wnd != NULL &&
        g_DwmGpuOutputOwner.Window == (ULONG_PTR)UserHMGetHandle(Wnd))
    {
        (VOID)IntCompositionReleaseGpuOutput(TRUE);
    }

    /* Drop any GL mark: the PWND may be recycled for an unrelated window. */
    for (i = 0; i < COMPOSITION_MAX_GL; i++)
        if (g_GlWindows[i] == Wnd)
            g_GlWindows[i] = NULL;

    e = IntCompositionFind(Wnd);
    if (e == NULL)
        return;

    if (e->WindowRectValid &&
        IntCompositionAccumulatePositionDamage(&e->WindowRect))
    {
        IntCompositionMarkDamage(FALSE);
    }
    IntCompositionFreeSurface(&e->Redirect, FALSE);
    IntCompositionFreeBlur(e);
    e->Wnd = NULL;
    e->WindowRectValid = FALSE;
}

static VOID
IntCompositionDefaultMinimizeRect(_In_ PWND Wnd, _Out_ PRECTL prc)
{
    PMONITOR pMonitor = UserMonitorFromRect((PRECTL)&Wnd->rcWindow,
                                            MONITOR_DEFAULTTONEAREST);
    RECTL rcMonitor, rcWork;
    LONG cx, cy;

    if (pMonitor == NULL)
    {
        rcMonitor.left = 0;
        rcMonitor.top = 0;
        rcMonitor.right = UserGetSystemMetrics(SM_CXSCREEN);
        rcMonitor.bottom = UserGetSystemMetrics(SM_CYSCREEN);
        rcWork = rcMonitor;
    }
    else
    {
        rcMonitor = *(PRECTL)&pMonitor->rcMonitor;
        rcWork = *(PRECTL)&pMonitor->rcWork;
    }

    cx = UserGetSystemMetrics(SM_CXMINIMIZED);
    cy = UserGetSystemMetrics(SM_CYSIZE) + 2 * UserGetSystemMetrics(SM_CYSIZEFRAME);
    if (cx <= 0) cx = 160;
    if (cy <= 0) cy = 32;

    if (rcWork.bottom < rcMonitor.bottom)
    {
        prc->top = rcWork.bottom;
        prc->bottom = rcMonitor.bottom;
    }
    else if (rcWork.top > rcMonitor.top)
    {
        prc->top = rcMonitor.top;
        prc->bottom = rcWork.top;
    }
    else
    {
        prc->top = rcMonitor.bottom - cy;
        prc->bottom = rcMonitor.bottom;
    }

    if (rcWork.right < rcMonitor.right)
    {
        prc->left = rcWork.right;
        prc->right = rcMonitor.right;
        prc->top = (Wnd->rcWindow.top + Wnd->rcWindow.bottom) / 2 - cy / 2;
        prc->bottom = prc->top + cy;
    }
    else if (rcWork.left > rcMonitor.left)
    {
        prc->left = rcMonitor.left;
        prc->right = rcWork.left;
        prc->top = (Wnd->rcWindow.top + Wnd->rcWindow.bottom) / 2 - cy / 2;
        prc->bottom = prc->top + cy;
    }
    else
    {
        prc->left = (Wnd->rcWindow.left + Wnd->rcWindow.right) / 2 - cx / 2;
        prc->right = prc->left + cx;
    }
}

VOID
IntCompositionQueryMinimizeRect(_In_ PWND Wnd)
{
    REDIRECT_ENTRY *e;
    RECTL rc;
    BOOL FromShell;

    if (!gbCompositionEnabled || !g_DwmAttached)
        return;
    if (IntCompositionFind(Wnd) == NULL)
        return;

    FromShell = co_IntGetShellMinRect(UserHMGetHandle(Wnd), &rc);

    e = IntCompositionFind(Wnd);
    if (e == NULL)
        return;
    if (!FromShell)
        IntCompositionDefaultMinimizeRect(Wnd, &rc);
    e->MinRect = rc;
    e->MinRectValid = (rc.right > rc.left && rc.bottom > rc.top);
}

static VOID
IntCompositionStartAnimation(_Inout_ REDIRECT_ENTRY *Entry,
                             _In_ const RECTL *Window, _In_ const RECTL *Target,
                             _In_ ULONG Flags)
{
    if (!gspv.animationinfo.iMinAnimate)
    {
        Entry->AnimFlags = 0;
        return;
    }
    if (Window->right <= Window->left || Window->bottom <= Window->top ||
        Target->right <= Target->left || Target->bottom <= Target->top)
    {
        Entry->AnimFlags = 0;
        return;
    }

    Entry->AnimRect = *Window;
    Entry->AnimTarget = *Target;
    Entry->AnimAnchor.x = (Target->left + Target->right) / 2;
    Entry->AnimAnchor.y = Target->top;
    Entry->AnimFlags = Flags;
    Entry->AnimStart = (LONGLONG)KeQueryInterruptTime();
    Entry->AnimDuration = (Flags == DWM_ANIM_MINIMIZE)
                              ? COMPOSITION_ANIM_MINIMIZE_100NS
                              : COMPOSITION_ANIM_RESTORE_100NS;
    Entry->AnimDamage = (Flags == DWM_ANIM_MINIMIZE) ? *Window : *Target;
    if (Flags == DWM_ANIM_MOVE)
        RECTL_bUnionRect(&Entry->AnimDamage, (PRECTL)Window, (PRECTL)Target);
    IntCompositionMarkDamage(FALSE);
}

static ULONG
IntCompositionCubeRoot(_In_ ULONGLONG Value)
{
    ULONG Low = 0, High = COMPOSITION_ANIM_SCALE;

    while (Low < High)
    {
        ULONG Mid = (Low + High + 1) >> 1;

        if ((ULONGLONG)Mid * Mid * Mid <= Value)
            Low = Mid;
        else
            High = Mid - 1;
    }
    return Low;
}

static ULONG
IntCompositionEase(_In_ ULONG Progress, _In_ BOOL Accelerate)
{
    ULONGLONG t, p;

    if (Accelerate)
        Progress = COMPOSITION_ANIM_SCALE - Progress;
    t = IntCompositionCubeRoot((ULONGLONG)Progress << 32);
    p = ((3 * t * t) >> 16) - ((2 * t * t * t) >> 32);
    if (p > COMPOSITION_ANIM_SCALE)
        p = COMPOSITION_ANIM_SCALE;
    if (Accelerate)
        return COMPOSITION_ANIM_SCALE - (ULONG)p;
    return (ULONG)p;
}

static LONG
IntCompositionAnimEdge(_In_ LONG Edge, _In_ LONG Anchor, _In_ ULONG Scale)
{
    return Anchor + (LONG)(((LONGLONG)(Edge - Anchor) * Scale) /
                           COMPOSITION_ANIM_SCALE);
}

static BOOL
IntCompositionEvaluateAnimation(_Inout_ REDIRECT_ENTRY *Entry,
                                _In_ LONGLONG Now, _Out_ PRECTL prc,
                                _Out_ PRECTL prcDamage)
{
    LONGLONG Elapsed = Now - Entry->AnimStart;
    ULONG u, s;
    RECTL rc;

    if (!gspv.animationinfo.iMinAnimate || Entry->AnimDuration <= 0 || Elapsed >= Entry->AnimDuration)
    {
        RECTL_bUnionRect(prcDamage, &Entry->AnimDamage, &Entry->AnimRect);
        Entry->AnimFlags = 0;
        return FALSE;
    }
    if (Elapsed < 0)
        Elapsed = 0;

    u = (ULONG)((Elapsed * COMPOSITION_ANIM_SCALE) / Entry->AnimDuration);
    if (Entry->AnimFlags == DWM_ANIM_MINIMIZE)
        s = COMPOSITION_ANIM_SCALE - IntCompositionEase(u, TRUE);
    else
        s = IntCompositionEase(u, FALSE);

    if (Entry->AnimFlags == DWM_ANIM_MOVE)
    {
        rc.left = IntCompositionAnimEdge(Entry->AnimTarget.left,
                                         Entry->AnimRect.left, s);
        rc.top = IntCompositionAnimEdge(Entry->AnimTarget.top,
                                        Entry->AnimRect.top, s);
        rc.right = IntCompositionAnimEdge(Entry->AnimTarget.right,
                                          Entry->AnimRect.right, s);
        rc.bottom = IntCompositionAnimEdge(Entry->AnimTarget.bottom,
                                           Entry->AnimRect.bottom, s);
    }
    else
    {
        rc.left = IntCompositionAnimEdge(Entry->AnimRect.left, Entry->AnimAnchor.x, s);
        rc.top = IntCompositionAnimEdge(Entry->AnimRect.top, Entry->AnimAnchor.y, s);
        rc.right = IntCompositionAnimEdge(Entry->AnimRect.right, Entry->AnimAnchor.x, s);
        rc.bottom = IntCompositionAnimEdge(Entry->AnimRect.bottom, Entry->AnimAnchor.y, s);
    }
    if (rc.right <= rc.left)
        rc.right = rc.left + 1;
    if (rc.bottom <= rc.top)
        rc.bottom = rc.top + 1;

    RECTL_bUnionRect(prcDamage, &Entry->AnimDamage, &rc);
    Entry->AnimDamage = rc;
    *prc = rc;
    return TRUE;
}

/*
 * A window's position/size/Z-order changed. Resize the backing when needed
 * (DceResetActiveDCEs then re-redirects any live DCs at the new surface —
 * this hook must run before it) and always schedule a recompose so pure
 * moves/Z-order changes reach the screen without waiting for a paint.
 */
VOID
IntCompositionOnDisplayChangeBegin(VOID)
{
    ULONG i;

    if (!gbCompositionEnabled)
        return;

    for (i = 0; i < g_RedirectHighWater; i++)
    {
        REDIRECT_ENTRY *e = &g_Redirects[i];

        if (e->Wnd == NULL)
            continue;
        IntCompositionFreeSurface(&e->Redirect, FALSE);
        InterlockedExchange(&e->BackComplete, FALSE);
        e->Damaged = TRUE;
        DceResetActiveDCEs(e->Wnd);
    }
    IntCompositionMarkDamage(TRUE);
}

VOID
IntCompositionOnDisplayChangeEnd(VOID)
{
    ULONG i;

    if (!gbCompositionEnabled)
        return;

    for (i = 0; i < g_RedirectHighWater; i++)
    {
        REDIRECT_ENTRY *e = &g_Redirects[i];

        if (e->Wnd == NULL || !IntCompositionIsCompositable(e->Wnd))
            continue;
        IntCompositionEnsureSurface(e->Wnd, &e->Redirect);
        DceResetActiveDCEs(e->Wnd);
    }
    IntCompositionMarkDamage(TRUE);
}

VOID
IntCompositionOnWindowResize(_In_ PWND Wnd)
{
    REDIRECT_ENTRY *e;
    BOOL PositionDamaged = FALSE;
    BOOL Maximized;
    RECTL OldWindowRect;
    BOOL OldWindowRectValid;
    BOOL Minimized;

    if (!gbCompositionEnabled)
        return;

    e = IntCompositionFind(Wnd);
    if (e == NULL)
    {
        IntCompositionOnWindowCreate(Wnd);
        return;
    }

    OldWindowRect = e->WindowRect;
    OldWindowRectValid = e->WindowRectValid;
    Minimized = (Wnd->style & WS_MINIMIZE) != 0;
    Maximized = (Wnd->style & WS_MAXIMIZE) != 0;

    if (!IntCompositionIsCompositable(Wnd))
    {
        e->AnimFlags = 0;
    }
    else if (Minimized && !e->Minimized)
    {
        if (g_DwmAttached && OldWindowRectValid && e->MinRectValid)
            IntCompositionStartAnimation(e, &OldWindowRect, &e->MinRect,
                                         DWM_ANIM_MINIMIZE);
    }
    else if (!Minimized && e->Minimized)
    {
        if (g_DwmAttached && e->MinRectValid)
            IntCompositionStartAnimation(e, (PRECTL)&Wnd->rcWindow,
                                         &e->MinRect,
                                         DWM_ANIM_RESTORE);
    }
    else if (g_DwmAttached && OldWindowRectValid &&
             Maximized != e->Maximized)
    {
        IntCompositionStartAnimation(e, &OldWindowRect,
                                     (PRECTL)&Wnd->rcWindow, DWM_ANIM_MOVE);
    }
    else if (e->AnimFlags != 0 && OldWindowRectValid &&
             (OldWindowRect.left != Wnd->rcWindow.left ||
              OldWindowRect.top != Wnd->rcWindow.top ||
              OldWindowRect.right != Wnd->rcWindow.right ||
              OldWindowRect.bottom != Wnd->rcWindow.bottom))
    {
        e->AnimFlags = 0;
    }
    e->Minimized = Minimized;
    e->Maximized = Maximized;

    /* Recompose every layer intersecting both the vacated and destination
     * bounds. This preserves strict Z order without turning a small move into
     * a full-screen clear, blend and scan-out. */
    if (e->WindowRectValid)
        PositionDamaged |= IntCompositionAccumulatePositionDamage(&e->WindowRect);
    if (IntCompositionIsCompositable(Wnd))
    {
        PositionDamaged |= IntCompositionAccumulatePositionDamage((PRECTL)&Wnd->rcWindow);
        e->WindowRect = Wnd->rcWindow;
        e->WindowRectValid = TRUE;
    }
    else
    {
        e->WindowRectValid = FALSE;
    }
    if (PositionDamaged)
        IntCompositionMarkDamage(FALSE);

    {
        PSURFACE psurfOld = e->Redirect.psurf;

        IntCompositionEnsureSurface(Wnd, &e->Redirect);

        /* DceResetActiveDCEs, called by co_WinPosSetWindowPos immediately
         * after this hook, rebinds long-held DCs if the backing changed. */
        if (e->Redirect.psurf != psurfOld)
        {
            InterlockedExchange(&e->BackComplete, FALSE);
            e->Damaged = TRUE;
        }
    }
}

VOID
IntCompositionAnimateMove(_In_opt_ PWND Wnd, _In_opt_ const RECTL *From)
{
    REDIRECT_ENTRY *e;

    if (!gbCompositionEnabled || !g_DwmAttached || Wnd == NULL || From == NULL)
        return;
    if (!IntCompositionIsCompositable(Wnd))
        return;
    e = IntCompositionFind(Wnd);
    if (e == NULL)
        return;
    if (From->left == Wnd->rcWindow.left && From->top == Wnd->rcWindow.top &&
        From->right == Wnd->rcWindow.right &&
        From->bottom == Wnd->rcWindow.bottom)
        return;
    IntCompositionStartAnimation(e, From, (PRECTL)&Wnd->rcWindow,
                                 DWM_ANIM_MOVE);
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

VOID
IntCompositionDamageWindowMetadata(_In_opt_ PWND Wnd)
{
    REDIRECT_ENTRY *e;

    if (!gbCompositionEnabled)
        return;

    if (Wnd != NULL &&
        (e = IntCompositionFind(IntCompositionTopLevel(Wnd))) != NULL &&
        e->WindowRectValid &&
        IntCompositionAccumulatePositionDamage(&e->WindowRect))
    {
        IntCompositionMarkDamage(FALSE);
    }
    else
    {
        IntCompositionDamageWindow(Wnd);
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
IntCompositionDamageBacking(_In_opt_ PSURFACE psurf,
                              _In_ const RECTL *Bounds)
{
    ULONG i;

    if (!gbCompositionEnabled)
        return;

    /* A primary write by the attached compositor publishes its completed
     * frame; it is not new desktop damage. */
    if (PsGetCurrentProcess() == g_DwmProcess)
        return;

    if (psurf != NULL)
    {
        for (i = 0; i < g_RedirectHighWater; i++)
        {
            if (g_Redirects[i].Redirect.psurf == psurf)
            {
                PWND_REDIRECT Redirect = &g_Redirects[i].Redirect;
                RECTL SurfaceBounds = {0, 0, Redirect->cx, Redirect->cy};
                RECTL Dirty;

                if (!RECTL_bIntersectRect(&Dirty, (PRECTL)Bounds, &SurfaceBounds))
                    return;
                /* FinishBlit still owns the PDEV semaphore. GETFRAME takes
                 * that same semaphore before consuming these bounds. */
                if (Redirect->BackDirtyValid)
                    RECTL_bUnionRect(&Redirect->BackDirtyRect,
                                     &Redirect->BackDirtyRect, &Dirty);
                else
                    Redirect->BackDirtyRect = Dirty;
                InterlockedExchange(&Redirect->BackDirtyValid, TRUE);
                InterlockedExchange(&g_Redirects[i].BackingDrawn, TRUE);
                g_Redirects[i].Damaged = TRUE;
                /* A bounded GetDC/ReleaseDC or BeginPaint/EndPaint bracket
                 * publishes the completed backing when it closes. Waking the
                 * compositor for every primitive inside that bracket only
                 * makes it repeatedly present the previous FRONT while BACK
                 * is incomplete. */
                if (InterlockedCompareExchange(&g_Redirects[i].PaintCount,
                                               0, 0) == 0)
                {
                    IntCompositionMarkDamage(FALSE);
                }
                return;
            }
        }
    }

    IntCompositionMarkDamage(TRUE);
}

VOID
IntCompositionCommitOpenGLFrame(_In_opt_ PSURFACE psurf,
                                _In_ const RECTL *prcDest)
{
    ULONG i, j;

    if (!gbCompositionEnabled || psurf == NULL || prcDest == NULL)
        return;

    for (i = 0; i < g_RedirectHighWater; i++)
    {
        REDIRECT_ENTRY *e = &g_Redirects[i];

        if (e->Redirect.psurf != psurf || e->Wnd == NULL)
            continue;

        for (j = 0; j < COMPOSITION_MAX_GL; j++)
        {
            if (g_GlWindows[j] == e->Wnd)
                break;
        }
        if (j == COMPOSITION_MAX_GL)
            return;

        if (prcDest->left == e->Redirect.rcClient.left &&
            prcDest->top == e->Redirect.rcClient.top &&
            prcDest->right == e->Redirect.rcClient.right &&
            prcDest->bottom == e->Redirect.rcClient.bottom)
        {
            InterlockedExchange(&e->BackComplete, TRUE);
        }
        return;
    }
}

static NTSTATUS
IntCompositionLookupRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present,
    _Out_ REDIRECT_ENTRY **RedirectEntry)
{
    REDIRECT_ENTRY *Entry;
    PWND Wnd;

    if (RedirectEntry == NULL)
        return STATUS_INVALID_PARAMETER;
    *RedirectEntry = NULL;

    if (Present == NULL || Present->Size != sizeof(*Present) ||
        Present->Flags != 0 || Present->Reserved != 0 ||
        Present->WindowHandle == 0 || Present->SurfaceHandle == 0 ||
        Present->OwnerProcess == NULL || Present->GlobalShare == 0 ||
        Present->UpdateId == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Wnd = UserGetWindowObject((HWND)(ULONG_PTR)Present->WindowHandle);
    Wnd = IntCompositionTopLevel(Wnd);
    if (Wnd == NULL || Wnd->head.pti == NULL || Wnd->head.pti->ppi == NULL ||
        Wnd->head.pti->ppi->peProcess != Present->OwnerProcess)
    {
        return STATUS_ACCESS_DENIED;
    }

    Entry = IntCompositionFind(Wnd);
    if (Entry == NULL ||
        Entry->Redirect.hbmp !=
            (HBITMAP)(ULONG_PTR)Present->SurfaceHandle ||
        Entry->Redirect.BackGlobalShare != Present->GlobalShare)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *RedirectEntry = Entry;
    return STATUS_SUCCESS;
}

static NTSTATUS
IntCompositionValidateRedirectedBltRect(
    _In_ const REDIRECT_ENTRY *Entry,
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present)
{
    if (Present->DestinationRect.left < Entry->Redirect.rcClient.left ||
        Present->DestinationRect.top < Entry->Redirect.rcClient.top ||
        Present->DestinationRect.right > Entry->Redirect.rcClient.right ||
        Present->DestinationRect.bottom > Entry->Redirect.rcClient.bottom ||
        Present->DestinationRect.left >= Present->DestinationRect.right ||
        Present->DestinationRect.top >= Present->DestinationRect.bottom)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
IntCompositionAdmitRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present)
{
    REDIRECT_ENTRY *Entry;
    NTSTATUS Status;

    Status = IntCompositionLookupRedirectedBltPresent(Present, &Entry);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IntCompositionValidateRedirectedBltRect(Entry, Present);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Entry->Redirect.GdiIssuedUpdateId != Present->UpdateId ||
        Present->UpdateId <= Entry->Redirect.GdiConsumedUpdateId ||
        Entry->Redirect.GdiAdmittedUpdateId >= Present->UpdateId ||
        Entry->Redirect.GdiPublishedUpdateId >= Present->UpdateId)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Entry->Redirect.GdiAdmittedUpdateId = Present->UpdateId;
    return STATUS_SUCCESS;
}

NTSTATUS
IntCompositionValidateRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present)
{
    REDIRECT_ENTRY *Entry;
    NTSTATUS Status;

    Status = IntCompositionLookupRedirectedBltPresent(Present, &Entry);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IntCompositionValidateRedirectedBltRect(Entry, Present);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Entry->Redirect.GdiIssuedUpdateId != Present->UpdateId ||
        Entry->Redirect.GdiAdmittedUpdateId != Present->UpdateId ||
        Present->UpdateId <= Entry->Redirect.GdiConsumedUpdateId ||
        Entry->Redirect.GdiPublishedUpdateId >= Present->UpdateId)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
IntCompositionCancelRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present)
{
    REDIRECT_ENTRY *Entry;
    NTSTATUS Status;

    Status = IntCompositionLookupRedirectedBltPresent(Present, &Entry);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Entry->Redirect.GdiIssuedUpdateId != Present->UpdateId ||
        Entry->Redirect.GdiAdmittedUpdateId != Present->UpdateId ||
        Present->UpdateId <= Entry->Redirect.GdiConsumedUpdateId ||
        Entry->Redirect.GdiPublishedUpdateId >= Present->UpdateId)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Entry->Redirect.GdiAdmittedUpdateId =
        Entry->Redirect.GdiConsumedUpdateId;
    return STATUS_SUCCESS;
}

NTSTATUS
IntCompositionCompleteRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present,
    _In_reads_(DirtyRectCount) const RECT *DirtyRects,
    _In_ UINT DirtyRectCount,
    _In_reads_(ContextCount) const HANDLE *Contexts,
    _In_ UINT ContextCount)
{
    REDIRECT_ENTRY *Entry;
    PWND Wnd;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DirtyRects);
    UNREFERENCED_PARAMETER(DirtyRectCount);
    UNREFERENCED_PARAMETER(Contexts);
    UNREFERENCED_PARAMETER(ContextCount);

    Status = IntCompositionValidateRedirectedBltPresent(Present);
    if (!NT_SUCCESS(Status))
        return Status;

    Wnd = IntCompositionTopLevel(
              UserGetWindowObject(
                  (HWND)(ULONG_PTR)Present->WindowHandle));
    Entry = IntCompositionFind(Wnd);
    ASSERT(Entry != NULL);

    /* The GPU has completed its redirected blit. DestinationRect bounds its
     * writes even when the native presentation supplied several dirty rects. */
    {
        PPDEVOBJ ppdev = IntCompositionLockDevice();
        if (ppdev == NULL)
            return STATUS_DEVICE_NOT_READY;
        IntCompositionDamageBacking(Entry->Redirect.psurf,
                                      (const RECTL *)&Present->DestinationRect);
        IntCompositionUnlockDevice(ppdev);
    }
    Entry->Redirect.GdiPublishedUpdateId = Present->UpdateId;
    InterlockedExchange(&Entry->BackingDrawn, TRUE);
    if (Present->DestinationRect.left == Entry->Redirect.rcClient.left &&
        Present->DestinationRect.top == Entry->Redirect.rcClient.top &&
        Present->DestinationRect.right == Entry->Redirect.rcClient.right &&
        Present->DestinationRect.bottom == Entry->Redirect.rcClient.bottom)
    {
        InterlockedExchange(&Entry->BackComplete, TRUE);
    }
    Entry->Damaged = TRUE;
    IntCompositionMarkDamage(FALSE);
    return STATUS_SUCCESS;
}

BOOL
IntCompositionIsAttachedProcess(VOID)
{
    /* The attachment owns a process reference until its pointer is cleared.
     * Comparing identity does not dereference a concurrently detached owner. */
    return PsGetCurrentProcess() == InterlockedCompareExchangePointer(
        (PVOID volatile *)&g_DwmProcess, NULL, NULL);
}

BOOL
IntCompositionIsGpuOutputWindow(_In_opt_ PWND Window)
{
    ULONG_PTR Registered;

    if (Window == NULL || !g_DwmAttached)
        return FALSE;

    Registered = (ULONG_PTR)InterlockedCompareExchangePointer(
        (PVOID volatile *)&g_DwmGpuOutputWindow, NULL, NULL);
    return Registered != 0 &&
           Registered == (ULONG_PTR)UserHMGetHandle(Window);
}

BOOL
IntCompositionIsGpuOutputPresent(_In_opt_ HWND Window,
                                 _In_ const RECT *SourceRect,
                                 _In_ const RECT *DestinationRect)
{
    ULONG_PTR Registered;
    LONG Width, Height;

    if (Window == NULL || SourceRect == NULL || DestinationRect == NULL ||
        !g_DwmAttached || PsGetCurrentProcess() != g_DwmProcess)
    {
        return FALSE;
    }

    Registered = (ULONG_PTR)InterlockedCompareExchangePointer(
        (PVOID volatile *)&g_DwmGpuOutputWindow, NULL, NULL);
    Width = InterlockedCompareExchange(&g_DwmGpuOutputWidth, 0, 0);
    Height = InterlockedCompareExchange(&g_DwmGpuOutputHeight, 0, 0);
    return Registered == (ULONG_PTR)Window && Width > 0 && Height > 0 &&
           SourceRect->left == 0 && SourceRect->top == 0 &&
           SourceRect->right == Width && SourceRect->bottom == Height &&
           DestinationRect->left == 0 && DestinationRect->top == 0 &&
           DestinationRect->right == Width &&
           DestinationRect->bottom == Height;
}

VOID
IntCompositionDamageFromGdi(VOID)
{
    /* The compositor's primary publication is not new desktop damage. */
    if (gbCompositionEnabled &&
        PsGetCurrentProcess() != g_DwmProcess)
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

    e->Damaged = TRUE;
    if (!IntCompositionTreeHasPendingPaint(e->Wnd))
        IntCompositionMarkDamage(FALSE);
}

/* Cache-DC hold bracket for redirected windows. A classic common DC may be
 * retained indefinitely by an application, so its lifetime is not a valid
 * global scan-out transaction. Classic BeginPaint and multi-window USER
 * operations establish their own bounded present batches instead. */
UCHAR
IntCompositionDcAcquire(_In_opt_ PWND Wnd)
{
    if (Wnd == NULL || IntCompositionIsGLWindow(Wnd))
        return COMPOSITION_DC_NONE;

    if (!gbCompositionEnabled)
        return COMPOSITION_DC_NONE;

    IntCompositionPaintBegin(Wnd);
    return COMPOSITION_DC_REDIRECTED;
}

VOID
IntCompositionDcRelease(_In_opt_ PWND Wnd, _In_ UCHAR State)
{
    REDIRECT_ENTRY *e;
    LONG PaintCount;

    if (State == COMPOSITION_DC_NONE)
        return;

    if (Wnd == NULL || State != COMPOSITION_DC_REDIRECTED)
        return;

    e = IntCompositionFind(IntCompositionTopLevel(Wnd));
    if (e == NULL)
        return;

    PaintCount = e->PaintCount;
    if (PaintCount > 0)
        PaintCount = InterlockedDecrement(&e->PaintCount);

    /* If the session drew, flag damage; the compose runs on the batch-done
     * commit, the throttled tick, or the pre-idle flush (not per release, so
     * a burst of small draws doesn't serialize behind a compose each). */
    if (e->Damaged && PaintCount == 0 &&
        !IntCompositionTreeHasPendingPaint(e->Wnd))
        IntCompositionMarkDamage(FALSE);
}

/* A top-level backing is shared by its whole child tree. Do not publish its
 * first FRONT merely because one window completed EndPaint: controls later in
 * the tree may still have update regions, which would expose a zeroed/partial
 * backing as a large black frame. GETFRAME also requires proof that a GDI draw
 * reached the backing. The explicit stack bounds kernel stack use; overflow
 * conservatively means "still pending". */
static BOOL
IntCompositionTreeHasPendingPaint(_In_ PWND Root)
{
    PWND Stack[DWM_MAX_WINDOWS];
    ULONG Count = 0;

    Stack[Count++] = Root;
    while (Count != 0)
    {
        PWND Wnd = Stack[--Count];
        PWND Child;

        if (!(Wnd->style & WS_VISIBLE))
            continue;
        if (Wnd->hrgnUpdate != NULL || (Wnd->state & WNDS_INTERNALPAINT))
            return TRUE;

        for (Child = Wnd->spwndChild; Child != NULL; Child = Child->spwndNext)
        {
            if (Count == ARRAYSIZE(Stack))
                return TRUE;
            Stack[Count++] = Child;
        }
    }
    return FALSE;
}

static BOOL
IntCompositionHasVisibleChild(_In_ PWND Wnd)
{
    PWND Child;

    for (Child = Wnd->spwndChild; Child != NULL; Child = Child->spwndNext)
    {
        if (Child->style & WS_VISIBLE)
            return TRUE;
    }
    return FALSE;
}

static VOID
IntCompositionCopyRect(_Inout_ PWND_REDIRECT r, _In_ const RECTL *prc)
{
    POINTL Source;

    if (prc->left >= prc->right || prc->top >= prc->bottom)
        return;

    Source.x = prc->left;
    Source.y = prc->top;
    IntEngBitBlt(&r->psurf->SurfObj, &r->psurfFront->SurfObj,
                 NULL, NULL, NULL, (PRECTL)prc, &Source,
                 NULL, NULL, NULL, ROP4_SRCCOPY);
}

static VOID
IntCompositionExchangeBuffers(_Inout_ PWND_REDIRECT r)
{
    PVOID Pointer;
    SIZE_T Size;
    ULONG Generation;
    RECTL Rect;

    Pointer = r->psurf->SurfObj.pvBits;
    r->psurf->SurfObj.pvBits = r->psurfFront->SurfObj.pvBits;
    r->psurfFront->SurfObj.pvBits = Pointer;
    Pointer = r->psurf->SurfObj.pvScan0;
    r->psurf->SurfObj.pvScan0 = r->psurfFront->SurfObj.pvScan0;
    r->psurfFront->SurfObj.pvScan0 = Pointer;

    Pointer = r->BackSection;
    r->BackSection = r->FrontSection;
    r->FrontSection = Pointer;
    Pointer = r->BackView;
    r->BackView = r->FrontView;
    r->FrontView = Pointer;
    Size = r->BackViewSize;
    r->BackViewSize = r->FrontViewSize;
    r->FrontViewSize = Size;
    Generation = r->BackGeneration;
    r->BackGeneration = r->Generation;
    r->Generation = Generation;

    /* The next GL present replaces the whole client, but not the window's
     * non-client frame. Seed only those narrow bands in the new BACK so the
     * next exchange keeps the current frame without restoring a full-surface
     * copy. */
    Rect.left = 0;
    Rect.top = 0;
    Rect.right = r->cx;
    Rect.bottom = r->rcClient.top;
    IntCompositionCopyRect(r, &Rect);
    Rect.top = r->rcClient.bottom;
    Rect.bottom = r->cy;
    IntCompositionCopyRect(r, &Rect);
    Rect.left = 0;
    Rect.top = r->rcClient.top;
    Rect.right = r->rcClient.left;
    Rect.bottom = r->rcClient.bottom;
    IntCompositionCopyRect(r, &Rect);
    Rect.left = r->rcClient.right;
    Rect.right = r->cx;
    IntCompositionCopyRect(r, &Rect);
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
    DWM_FRAME_HEADER Frame;
    DWM_FRAME_HEADER Input;
    static PWND s_stack[DWM_MAX_WINDOWS];
    PWND pwndDesktop, pwndChild;
    PPDEVOBJ ppdev;
    SIZEL sizl = {0, 0};
    ULONG count = 0, blurRectCount = 0, n = 0, i, OutputBytes;
    LONGLONG now = (LONGLONG)KeQueryInterruptTime();
    LONG dirty, fullDamage;
    BOOL DeferredDamage = FALSE;
    BOOL PaintDamageDeferred = FALSE;
    BOOL ReadyDamage = FALSE;
    BOOL AnimRunning = FALSE;
    BOOL PositionDamageValid;
    RECTL PositionDamage;
    RECTL rcDmg = {0, 0, 0, 0};
    NTSTATUS Status = STATUS_SUCCESS;

    if (!gbCompositionEnabled)
        return STATUS_DEVICE_NOT_READY;
    if (PsGetCurrentProcess() != g_DwmProcess)
        return STATUS_ACCESS_DENIED;

    _SEH2_TRY
    {
        ProbeForWrite(pUser, sizeof(Input), sizeof(ULONG));
        Input = *(PDWM_FRAME_HEADER)pUser;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        return Status;
    if (Input.Magic != DWM_FRAME_MAGIC)
        return STATUS_INVALID_PARAMETER;
    if (Input.BufBytes < DWM_FRAME_BYTES)
        return STATUS_BUFFER_TOO_SMALL;

    /* Each new pull follows completion of the compositor's preceding GPU
     * reads. A removed window absent from this frame can now release an old
     * publication even when its old-tuple acknowledgement raced destruction. */
    IntCompositionDrainDxPublications(g_DwmProcess, TRUE);

    pwndDesktop = UserGetDesktopWindow();
    if (pwndDesktop == NULL || ScreenDeviceContext == NULL)
        return STATUS_DEVICE_NOT_READY;

    /* Position-only frames need metadata, not the display-device semaphore.
     * Taking hsemDevLock here while the syscall holds the global USER lock
     * serializes pointer input behind an unrelated scan-out. Reference the
     * PDEV only long enough to obtain the mode size; acquire its semaphore
     * lazily below if a backing really has to be copied. */
    ppdev = IntCompositionReferenceDevice();
    if (ppdev == NULL)
        return STATUS_DEVICE_NOT_READY;
    PDEVOBJ_sizl(ppdev, &sizl);
    IntCompositionDereferenceDevice(ppdev);
    ppdev = NULL;

    RtlZeroMemory(&Frame, sizeof(Frame));
    Frame.Magic = DWM_FRAME_MAGIC;
    Frame.BufBytes = Input.BufBytes;
    Frame.ScreenW = (ULONG)sizl.cx;
    Frame.ScreenH = (ULONG)sizl.cy;
    Frame.WinArrayBase = DWM_WINARRAY_BASE;
    Frame.BlurRectArrayBase = DWM_BLURRECTARRAY_BASE;

    /* Keep returning the current FRONT metadata even on an idle pull. DWM may
     * need to retry a full compose after OPENSURFACE raced a resize or a
     * present bracket failed; returning Count=0 here would make that retry
     * publish a windowless frame. Idle pulls do not copy BACK->FRONT below. */
    dirty = InterlockedCompareExchange(&g_CompositionDamaged, FALSE, FALSE);
    fullDamage = InterlockedCompareExchange(&g_CompositionFullDamage, FALSE, FALSE);

    /* Top-first walk; emit bottom-first so dwm blits back-to-front. */
    for (pwndChild = pwndDesktop->spwndChild;
         pwndChild != NULL && n < DWM_MAX_WINDOWS;
         pwndChild = pwndChild->spwndNext)
    {
        if (IntCompositionIsCompositable(pwndChild))
            s_stack[n++] = pwndChild;
    }

    /* A D3D present may have written a CDD redirection bitmap through the
     * GPU after GDI last touched it. Complete those writes and make their
     * system backing CPU-visible before BACK is copied into the published
     * FRONT snapshot. */
    if (gpmdev != NULL)
    {
        UINT64 RedirectionFence;

        if (GreSynchronizeRedirectionBitmaps(gpmdev, &RedirectionFence) != 0)
            return STATUS_GRAPHICS_ALLOCATION_BUSY;
    }

    /* Consume only the damage that existed before this snapshot. A GDI draw
     * cannot enter while the PDEV is locked; a later draw sets the flags again
     * after unlock and therefore cannot be lost by this frame. */
    dirty = InterlockedExchange(&g_CompositionDamaged, FALSE);
    fullDamage = InterlockedExchange(&g_CompositionFullDamage, FALSE);
    PositionDamageValid = g_CompositionPositionDamageValid;
    PositionDamage = g_CompositionPositionDamage;
    g_CompositionPositionDamageValid = FALSE;

    if (PositionDamageValid)
        rcDmg = PositionDamage;

    for (i = 0; i < n && count < DWM_MAX_WINDOWS; i++)
    {
        PWND w = s_stack[n - 1 - i];
        REDIRECT_ENTRY *e = IntCompositionFind(w);
        BOOL wasDamaged;
        BOOL BackingChanged = FALSE;
        BOOL BackingDeferred = FALSE;
        BOOL PaintDeferred = FALSE;
        BOOL NativeDxPublished;
        BOOL NativeDxPending;

        if (e == NULL || e->Redirect.cx <= 0 || e->Redirect.cy <= 0)
            continue;
        NativeDxPublished = e->Redirect.DxInfo.Version == DWM_DX_SURFACE_INFO_VERSION_GPU &&
                            e->Redirect.DxGlobalShare != 0 && e->Redirect.DxPublishedUpdateId != 0;
        NativeDxPending = NativeDxPublished &&
                          e->Redirect.DxPublishedUpdateId > e->Redirect.DxConsumedUpdateId;

        /* Sync a window's BACK->FRONT only when it is not mid-paint. The
         * device lock is acquired on the first actual copy and then held for
         * the remainder of the snapshot, serializing it against redirected
         * GDI drawing. */
        {
            LONG PaintCount = e->PaintCount;
            LONGLONG PaintAge = now - e->PaintStart;
            BOOL bBusy = (PaintCount > 0) &&
                         PaintAge < COMPOSITION_PAINT_STALE_100NS;
            BOOL bBackingDrawn =
                InterlockedCompareExchange(&e->BackingDrawn, FALSE, FALSE) != FALSE;
            BOOL bBackingDirty =
                InterlockedCompareExchange(&e->Redirect.BackDirtyValid, FALSE, FALSE) != FALSE;
            BOOL bTreePending = IntCompositionTreeHasPendingPaint(w);
            /* An OpenGL client can publish its first complete shared surface
             * without ever drawing through GDI. Waiting for BackingDrawn in
             * that case excludes the window from GETFRAME forever, so DWM
             * cannot acknowledge it and every later swap remains busy.
             * Pending GDI/non-client paints still defer publication below.
             */
            BOOL bDxPublished = e->Redirect.DxGlobalShare != 0 &&
                                e->Redirect.DxPublishedUpdateId != 0;
            BOOL bFirstPaintPending = !e->Redirect.FrontValid &&
                                      ((!bBackingDrawn && !bDxPublished) ||
                                       bTreePending);

            if ((!e->Redirect.FrontValid || bBackingDirty) &&
                !bBusy && !bTreePending &&
                !bFirstPaintPending &&
                e->Redirect.psurf != NULL && e->Redirect.psurfFront != NULL)
            {
                BOOL BackingPublished = FALSE;
                RECTL PublishedBounds = {0, 0, e->Redirect.cx, e->Redirect.cy};

                if (ppdev == NULL)
                    ppdev = IntCompositionLockDevice();

                if (ppdev == NULL)
                {
                    BackingDeferred = TRUE;
                    DeferredDamage = TRUE;
                }
                else if (InterlockedCompareExchange(&e->BackComplete, FALSE, TRUE) &&
                         IntCompositionIsGLWindow(w) &&
                         !IntCompositionHasVisibleChild(w) &&
                         e->Redirect.BackGlobalShare == 0 &&
                         e->Redirect.FrontGlobalShare == 0)
                {
                    IntCompositionExchangeBuffers(&e->Redirect);
                    BackingPublished = TRUE;
                }
                else
                {
                    POINTL Source;
                    if (e->Redirect.FrontValid && e->Redirect.BackDirtyValid)
                        PublishedBounds = e->Redirect.BackDirtyRect;
                    Source.x = PublishedBounds.left;
                    Source.y = PublishedBounds.top;
                    BackingPublished = IntEngBitBlt(
                        &e->Redirect.psurfFront->SurfObj,
                        &e->Redirect.psurf->SurfObj,
                        NULL, NULL, NULL, &PublishedBounds, &Source,
                        NULL, NULL, NULL, ROP4_SRCCOPY);
                    if (BackingPublished)
                        InterlockedExchange(&e->BackComplete, FALSE);
                }
                if (BackingPublished)
                {
                    e->Redirect.FrontValid = TRUE;
                    e->Redirect.BasePreviousUpdateId = e->Redirect.BaseUpdateId;
                    e->Redirect.BaseUpdateId = ++g_BaseUpdateSequence;
                    e->Redirect.BaseDirtyRect = PublishedBounds;
                    e->Redirect.BackDirtyValid = FALSE;
                    BackingChanged = TRUE;
                    if (e->Redirect.GdiPublishedUpdateId >
                        e->Redirect.GdiConsumedUpdateId)
                    {
                        e->Redirect.GdiConsumedUpdateId =
                            e->Redirect.GdiPublishedUpdateId;
                    }
                }
                else if (ppdev != NULL)
                {
                    BackingDeferred = TRUE;
                    DeferredDamage = TRUE;
                }
            }
            else if (e->Damaged && (!e->Redirect.FrontValid || bBackingDirty))
            {
                if (bBusy || bTreePending || bFirstPaintPending)
                    PaintDeferred = TRUE;
                else
                    DeferredDamage = TRUE;
            }
        }

        /* A native client present cannot wait for its owner to dispatch a
         * pending GDI paint: that thread may be inside Present waiting for
         * our consumed event. Keep the last complete GDI FRONT, or expose
         * BaseUpdateId 0 until one exists, and publish the completed GPU layer
         * independently. Never copy an unfinished GDI BACK for this case. */
        if ((!e->Redirect.FrontValid && !NativeDxPublished) || e->Redirect.psurfFront == NULL ||
            (e->Redirect.FrontSection == NULL &&
             e->Redirect.FrontGlobalShare == 0))
        {
            if (e->Damaged && !PaintDeferred)
                DeferredDamage = TRUE;
            continue;
        }

        if (BackingDeferred || PaintDeferred)
            wasDamaged = FALSE;
        else if (dirty || fullDamage)
            wasDamaged = InterlockedExchange((volatile LONG *)&e->Damaged, FALSE) != FALSE;
        else
            wasDamaged = FALSE;
        /* A GDI write can arrive after this entry's dirty hint was read on
         * the previous pull. Its pending bounds survive independently of the
         * metadata damage flag; publishing them must always wake a redraw. */
        wasDamaged |= BackingChanged || NativeDxPending;
        if (wasDamaged)
            ReadyDamage = TRUE;
        if (PaintDeferred)
            PaintDamageDeferred = TRUE;
        g_DwmFrameWindows[count].x = w->rcWindow.left;
        g_DwmFrameWindows[count].y = w->rcWindow.top;
        g_DwmFrameWindows[count].cx = e->Redirect.cx;
        g_DwmFrameWindows[count].cy = e->Redirect.cy;
        g_DwmFrameWindows[count].SurfaceId = (ULONG)(e - g_Redirects);
        g_DwmFrameWindows[count].Generation = e->Redirect.Generation;
        g_DwmFrameWindows[count].Stride =
            (ULONG)e->Redirect.psurfFront->SurfObj.lDelta;
        g_DwmFrameWindows[count].Damaged = wasDamaged ? 1 : 0;
        g_DwmFrameWindows[count].DxGlobalShare = e->Redirect.DxGlobalShare;
        g_DwmFrameWindows[count].DxGeneration = e->Redirect.DxGeneration;
        g_DwmFrameWindows[count].DxAdapterLuid = e->Redirect.DxAdapterLuid;
        g_DwmFrameWindows[count].DxUpdateId = e->Redirect.DxPublishedUpdateId;
        g_DwmFrameWindows[count].DxClientX = e->Redirect.DxClientX;
        g_DwmFrameWindows[count].DxClientY = e->Redirect.DxClientY;
        g_DwmFrameWindows[count].DxWidth = e->Redirect.DxInfo.Width;
        g_DwmFrameWindows[count].DxHeight = e->Redirect.DxInfo.Height;
        g_DwmFrameWindows[count].DxPitch = e->Redirect.DxInfo.Pitch;
        g_DwmFrameWindows[count].DxFormat = e->Redirect.DxInfo.Format;
        g_DwmFrameWindows[count].BaseGlobalShare =
            e->Redirect.FrontGlobalShare;
        g_DwmFrameWindows[count].BaseGeneration =
            e->Redirect.BaseGeneration;
        g_DwmFrameWindows[count].BaseUpdateId =
            e->Redirect.BaseUpdateId;
        g_DwmFrameWindows[count].BasePreviousUpdateId =
            e->Redirect.BasePreviousUpdateId;
        g_DwmFrameWindows[count].BaseDirtyRect =
            e->Redirect.BaseDirtyRect;
        g_DwmFrameWindows[count].BaseWidth = (ULONG)e->Redirect.cx;
        g_DwmFrameWindows[count].BaseHeight = (ULONG)e->Redirect.cy;
        g_DwmFrameWindows[count].BasePitch =
            (ULONG)e->Redirect.psurfFront->SurfObj.lDelta;
        g_DwmFrameWindows[count].BaseFormat =
            DWM_DX_FORMAT_B8G8R8A8_UNORM;
        g_DwmFrameWindows[count].BlurFlags = e->BlurFlags;
        g_DwmFrameWindows[count].BlurRectBase = blurRectCount;
        g_DwmFrameWindows[count].BlurRectCount = 0;
        g_DwmFrameWindows[count].BackdropType = 0;
        g_DwmFrameWindows[count].BackdropOpacity = 255;
        g_DwmFrameWindows[count].BackdropColor = 0;
        g_DwmFrameWindows[count].BackdropColorization = 0;
        g_DwmFrameWindows[count].BackdropRegion = 0;
        g_DwmFrameWindows[count].BackdropNcExtend = 0;
        g_DwmFrameWindows[count].BackdropNcExtendLeft = 0;
        g_DwmFrameWindows[count].CornerRadius = 0;
        {
            ULONG_PTR Radius = (ULONG_PTR)UserGetProp(w, AtomDwmCornerRadius,
                                                      FALSE);
            if (Radius != 0 && Radius <= 64)
                g_DwmFrameWindows[count].CornerRadius = (ULONG)Radius;
        }
        g_DwmFrameWindows[count].ClientX = e->Redirect.rcClient.left;
        g_DwmFrameWindows[count].ClientY = e->Redirect.rcClient.top;
        g_DwmFrameWindows[count].ClientWidth =
            e->Redirect.rcClient.right - e->Redirect.rcClient.left;
        g_DwmFrameWindows[count].ClientHeight =
            e->Redirect.rcClient.bottom - e->Redirect.rcClient.top;
        g_DwmFrameWindows[count].AnimFlags = 0;
        g_DwmFrameWindows[count].AnimX = 0;
        g_DwmFrameWindows[count].AnimY = 0;
        g_DwmFrameWindows[count].AnimCx = 0;
        g_DwmFrameWindows[count].AnimCy = 0;
        if (e->AnimFlags != 0)
        {
            RECTL rcAnim, rcAnimDamage;
            ULONG AnimFlags = e->AnimFlags;

            AnimRunning = TRUE;
            if (IntCompositionEvaluateAnimation(e, now, &rcAnim, &rcAnimDamage))
            {
                g_DwmFrameWindows[count].AnimFlags = AnimFlags;
                g_DwmFrameWindows[count].AnimX = rcAnim.left;
                g_DwmFrameWindows[count].AnimY = rcAnim.top;
                g_DwmFrameWindows[count].AnimCx = rcAnim.right - rcAnim.left;
                g_DwmFrameWindows[count].AnimCy = rcAnim.bottom - rcAnim.top;
            }
            RECTL_bUnionRect(&rcDmg, &rcDmg, &rcAnimDamage);
        }
        if ((e->BlurFlags & DWM_BLUR_ENABLE) &&
            !(e->BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW) &&
            e->BlurRectCount != 0 && e->BlurRects != NULL)
        {
            ULONG CopyCount = e->BlurRectCount;

            if (CopyCount > DWM_MAX_BLUR_RECTS - blurRectCount)
                CopyCount = DWM_MAX_BLUR_RECTS - blurRectCount;
            if (CopyCount != 0)
            {
                RtlCopyMemory(&g_DwmFrameBlurRects[blurRectCount],
                              e->BlurRects,
                              (SIZE_T)CopyCount * sizeof(RECTL));
                g_DwmFrameWindows[count].BlurRectCount = CopyCount;
                blurRectCount += CopyCount;
            }
        }
        {
            BYTE alpha = 255;
            COLORREF key = 0;
            DWORD lf = 0;
            IntCompositionGetLayered(w, &alpha, &key, &lf);
            if (!(w->style & (WS_MINIMIZE | WS_MAXIMIZE)) &&
                UserHasWindowEdge(w->style, w->ExStyle))
            {
                lf |= DWM_WINDOW_NC_SHADOW;
                if (gpqForeground != NULL &&
                    gpqForeground->spwndActive == w)
                {
                    lf |= DWM_WINDOW_ACTIVE;
                }
                if (AtomDwmDarkMode != 0 &&
                    UserGetProp(w, AtomDwmDarkMode, FALSE) != NULL)
                {
                    lf |= DWM_WINDOW_DARK;
                }
            }
            g_DwmFrameWindows[count].Alpha = alpha;
            g_DwmFrameWindows[count].ColorKey = (ULONG)key;
            g_DwmFrameWindows[count].LayerFlags = lf;
        }
        if (AtomDwmSystemBackdropType != 0)
        {
            ULONG_PTR Value = (ULONG_PTR)UserGetProp(
                w, AtomDwmSystemBackdropType, FALSE);

            if (Value >= DWM_BACKDROP_MAIN && Value <= DWM_BACKDROP_TABBED)
            {
                ULONG_PTR Encoded;

                g_DwmFrameWindows[count].BackdropType = (ULONG)Value;
                Encoded = (ULONG_PTR)UserGetProp(
                    w, AtomDwmBackdropOpacity, FALSE);
                if (Encoded != 0 && Encoded <= 256)
                    g_DwmFrameWindows[count].BackdropOpacity = (ULONG)(Encoded - 1);
                Encoded = (ULONG_PTR)UserGetProp(
                    w, AtomDwmBackdropColor, FALSE);
                if (Encoded != 0 && Encoded <= 0x01000000)
                    g_DwmFrameWindows[count].BackdropColor = (ULONG)(Encoded - 1);
                Encoded = (ULONG_PTR)UserGetProp(
                    w, AtomDwmBackdropColorization, FALSE);
                if (Encoded != 0 && Encoded <= 0x01000000)
                {
                    g_DwmFrameWindows[count].BackdropColorization =
                        (ULONG)(Encoded - 1);
                }
                else
                {
                    g_DwmFrameWindows[count].BackdropColorization =
                        g_DwmFrameWindows[count].BackdropColor;
                }
                Value = (ULONG_PTR)UserGetProp(
                    w, AtomDwmBackdropRegion, FALSE);
                if (Value == DWM_BACKDROP_REGION_NONCLIENT ||
                    Value == DWM_BACKDROP_REGION_WINDOW)
                {
                    g_DwmFrameWindows[count].BackdropRegion = (ULONG)Value;
                }
                if (AtomDwmBackdropNcExtend != 0)
                {
                    Encoded = (ULONG_PTR)UserGetProp(
                        w, AtomDwmBackdropNcExtend, FALSE);
                    if (Encoded != 0 && Encoded <= DWM_MAX_NC_EXTEND + 1)
                    {
                        g_DwmFrameWindows[count].BackdropNcExtend =
                            (ULONG)(Encoded - 1);
                    }
                }
                if (AtomDwmBackdropNcExtendLeft != 0)
                {
                    Encoded = (ULONG_PTR)UserGetProp(
                        w, AtomDwmBackdropNcExtendLeft, FALSE);
                    if (Encoded != 0 && Encoded <= DWM_MAX_NC_EXTEND + 1)
                    {
                        g_DwmFrameWindows[count].BackdropNcExtendLeft =
                            (ULONG)(Encoded - 1);
                    }
                }
            }
        }
        count++;

        if (wasDamaged)
            RECTL_bUnionRect(&rcDmg, &rcDmg, (RECTL *)&w->rcWindow);
    }

    IntCompositionUnlockDevice(ppdev);

    /* A deferred backing needs another compositor pass. Resource contention
     * wakes DWM immediately. An incomplete paint tree only retains the dirty
     * bit: its final EndPaint normally wakes DWM, while the idle poll remains
     * a bounded fallback without spinning on a long-running paint. */
    if (DeferredDamage || AnimRunning)
        IntCompositionMarkDamage(FALSE);
    else if (PaintDamageDeferred)
        InterlockedExchange(&g_CompositionDamaged, TRUE);

    Frame.Count = count;
    Frame.FullDamage = fullDamage ? 1 : 0;
    Frame.Dirty = (fullDamage || PositionDamageValid || ReadyDamage ||
                   AnimRunning) ? 1 : 0;
    Frame.DmgL = rcDmg.left;
    Frame.DmgT = rcDmg.top;
    Frame.DmgR = rcDmg.right;
    Frame.DmgB = rcDmg.bottom;
    Frame.BlurRectCount = blurRectCount;
    OutputBytes = DWM_BLURRECTARRAY_BASE +
                  blurRectCount * sizeof(RECTL);

    _SEH2_TRY
    {
        ProbeForWrite(pUser, OutputBytes, sizeof(ULONG));
        *(PDWM_FRAME_HEADER)pUser = Frame;
        if (count != 0)
            RtlCopyMemory((PUCHAR)pUser + DWM_WINARRAY_BASE, g_DwmFrameWindows, count * sizeof(DWM_WIN));
        if (blurRectCount != 0)
            RtlCopyMemory((PUCHAR)pUser + DWM_BLURRECTARRAY_BASE,
                          g_DwmFrameBlurRects,
                          blurRectCount * sizeof(RECTL));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        IntCompositionMarkDamage(TRUE);
    else
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

static BOOL
IntCompositionIsDwmProcess(VOID)
{
    static const UNICODE_STRING DwmSuffix = RTL_CONSTANT_STRING(L"\\System32\\dwm.exe");
    UNICODE_STRING SystemRoot;
    PUNICODE_STRING ImageName;
    PVOID Buffer;
    ULONG Length = 0;
    NTSTATUS Status;
    BOOL IsDwm = FALSE;

    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessImageFileNameWin32, NULL, 0, &Length);
    if (Status != STATUS_INFO_LENGTH_MISMATCH || Length < sizeof(UNICODE_STRING) || Length > 0x10000)
        return FALSE;

    Buffer = ExAllocatePoolWithTag(PagedPool, Length, 'mwDC');
    if (Buffer == NULL)
        return FALSE;

    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessImageFileNameWin32, Buffer, Length, &Length);
    if (NT_SUCCESS(Status))
    {
        ImageName = (PUNICODE_STRING)Buffer;
        RtlInitUnicodeString(&SystemRoot, SharedUserData->NtSystemRoot);
        if (ImageName->Buffer != NULL &&
            ImageName->Length <= ImageName->MaximumLength &&
            ImageName->Length == SystemRoot.Length + DwmSuffix.Length)
        {
            UNICODE_STRING RootPart;
            UNICODE_STRING SuffixPart;

            RootPart.Length = RootPart.MaximumLength = SystemRoot.Length;
            RootPart.Buffer = ImageName->Buffer;
            SuffixPart.Length = SuffixPart.MaximumLength = DwmSuffix.Length;
            SuffixPart.Buffer = (PWCHAR)((PUCHAR)ImageName->Buffer + SystemRoot.Length);
            IsDwm = RtlEqualUnicodeString(&RootPart, &SystemRoot, TRUE) &&
                    RtlEqualUnicodeString(&SuffixPart, &DwmSuffix, TRUE);
        }
    }
    ExFreePoolWithTag(Buffer, 'mwDC');
    return IsDwm;
}

/* Release the attached dwm, drop the wake-event reference, and clear the
 * compositor identity. */
static VOID
IntCompositionDwmTeardown(VOID)
{
    PEPROCESS Process;
    PKEVENT WakeEvent;

    g_DwmAttached = FALSE;
    (VOID)IntCompositionReleaseGpuOutput(TRUE);
    /* Damage raised from the GDI finish path holds this same PDEV lock while
     * reading/signaling g_DwmWakeEvent. Clear it before dropping the object
     * reference so KeSetEvent can never race a freed event. */
    {
        PPDEVOBJ ppdev = IntCompositionLockDevice();
        WakeEvent = InterlockedExchangePointer((PVOID volatile *)&g_DwmWakeEvent, NULL);
        IntCompositionUnlockDevice(ppdev);
    }

    if (WakeEvent != NULL)
        ObDereferenceObject(WakeEvent);

    Process = g_DwmProcess;
    g_DwmProcess = NULL;
    if (Process != NULL)
    {
        IntDCompositionDisconnectProcess(Process);
        ObDereferenceObject(Process);
    }
}

VOID
IntCompositionCleanupProcess(_In_ PEPROCESS Process)
{
    PWND Desktop;

    if (g_DwmProcess != Process)
        return;
    IntCompositionDwmTeardown();
    IntCompositionSetEnabled(FALSE);
    Desktop = UserGetDesktopWindow();
    if (Desktop != NULL)
        co_UserRedrawWindow(Desktop, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

NTSTATUS
IntCompositionDwmSetGpuOutput(_In_ PVOID pUser)
{
    DWM_GPU_OUTPUT Request;
    PWND Window;

    if (pUser == NULL)
        return STATUS_INVALID_PARAMETER;
    _SEH2_TRY
    {
        ProbeForRead(pUser, sizeof(Request), sizeof(ULONG));
        Request = *(PDWM_GPU_OUTPUT)pUser;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Request.StructSize != sizeof(Request) || Request.Reserved != 0 ||
        !g_DwmAttached || PsGetCurrentProcess() != g_DwmProcess)
    {
        return STATUS_ACCESS_DENIED;
    }
    if (Request.Window == 0)
        return IntCompositionReleaseGpuOutput(FALSE);
    if (Request.Width == 0 || Request.Height == 0 ||
        Request.Width > MAXLONG || Request.Height > MAXLONG ||
        Request.Window > MAXULONG_PTR)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Window = UserGetWindowObject((HWND)(ULONG_PTR)Request.Window);
    if (Window == NULL || Window->head.pti == NULL ||
        Window->head.pti->ppi != PsGetCurrentProcessWin32Process())
    {
        return STATUS_ACCESS_DENIED;
    }

    return IntCompositionClaimGpuOutput(&Request);
}

/*
 * dwm.exe attach/detach (ONEPARAM_ROUTINE_DWMATTACH, DWM_ATTACH exchange).
 * On attach, creates a damage wake event in the caller's handle table and
 * keeps a referenced pointer so dwm can block instead of polling when idle.
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
    HANDLE hWake = NULL;
    PKEVENT WakeEvent = NULL;
    PEPROCESS CurrentProcess = PsGetCurrentProcess();
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

    if (req.Attach > 1)
        return STATUS_INVALID_PARAMETER;
    if (g_DwmProcess != NULL && g_DwmProcess != CurrentProcess)
        return STATUS_ACCESS_DENIED;

    if (req.Attach)
    {
        if (!IntCompositionIsDwmProcess())
            return STATUS_ACCESS_DENIED;

        /* Allocate the mandatory wake channel before disturbing an existing
         * attachment. Re-attach is therefore failure-atomic. */
        hWake = IntCompositionCreateDwmEvent(&WakeEvent);
        if (hWake == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        IntCompositionDwmTeardown();
        g_DwmWakeEvent = WakeEvent;
        ObReferenceObject(CurrentProcess);
        g_DwmProcess = CurrentProcess;
        g_DwmAttached = TRUE;
        g_DwmLastFrameTime = (LONGLONG)KeQueryInterruptTime();

        _SEH2_TRY
        {
            ((PDWM_ATTACH)pUser)->hWake = hWake;
            ((PDWM_ATTACH)pUser)->hVblank = NULL;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IntCompositionDwmTeardown();
            ZwClose(hWake);
            return Status;
        }

        /* The compositor now owns the frame: turn redirection on, retarget
         * every live DC (held GL DCs included) into window backings, and
         * repaint the desktop so each window fills its fresh backing. */
        IntCompositionSetEnabled(TRUE);
        DceRedirectAllDCs();
        {
            PWND pwndDesktop = UserGetDesktopWindow();
            if (pwndDesktop != NULL)
            {
                co_UserRedrawWindow(pwndDesktop, NULL, NULL,
                                    RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
                                    RDW_ALLCHILDREN);
            }
        }
        IntCompositionDamageFromGdi();
    }
    else
    {
        /* The attached compositor drains its GPU before explicitly detaching.
         * A watchdog timeout cannot provide this completion guarantee. */
        IntCompositionDrainDxPublications(CurrentProcess, FALSE);
        /* No compositor: back to classic direct drawing. */
        IntCompositionDwmTeardown();
        IntCompositionSetEnabled(FALSE);

        _SEH2_TRY
        {
            ((PDWM_ATTACH)pUser)->hWake = NULL;
            ((PDWM_ATTACH)pUser)->hVblank = NULL;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    {
        PWND pwndDesktop = UserGetDesktopWindow();
        if (pwndDesktop != NULL)
            co_UserRedrawWindow(pwndDesktop, NULL, NULL,
                                RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
                                RDW_ALLCHILDREN);
    }

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

static BOOLEAN
IntCompositionUpdateDxPlacement(
    _In_ PWND SourceWnd,
    _In_ PWND TopWnd,
    _Inout_ PWND_REDIRECT Redirect)
{
    LONG Width = SourceWnd->rcClient.right - SourceWnd->rcClient.left;
    LONG Height = SourceWnd->rcClient.bottom - SourceWnd->rcClient.top;

    if (Width <= 0 || Height <= 0 ||
        (ULONG)Width != Redirect->DxInfo.Width ||
        (ULONG)Height != Redirect->DxInfo.Height)
    {
        return FALSE;
    }

    Redirect->DxClientX =
        SourceWnd->rcClient.left - TopWnd->rcWindow.left;
    Redirect->DxClientY =
        SourceWnd->rcClient.top - TopWnd->rcWindow.top;
    return TRUE;
}

NTSTATUS
IntCompositionDwmDxSurface(_In_ PVOID pUser)
{
    DWM_DX_SURFACE_EXCHANGE Request;
    REDIRECT_ENTRY *Entry = NULL;
    PWND SourceWnd = NULL;
    PWND TopWnd = NULL;
    PPROCESSINFO ProcessInfo;
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        ProbeForWrite(pUser, sizeof(Request), sizeof(ULONG));
        Request = *(PDWM_DX_SURFACE_EXCHANGE)pUser;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Request.StructSize != sizeof(Request))
        return STATUS_INFO_LENGTH_MISMATCH;

    if (Request.Action == DWM_DX_SURFACE_CONSUMED)
    {
        ULONG Index;
        for (Index = 0; Index < ARRAYSIZE(g_DxPublications); ++Index)
        {
            DWM_DX_PUBLICATION *Publication = &g_DxPublications[Index];
            if (Publication->ReadyEvent != NULL &&
                Publication->Compositor == PsGetCurrentProcess() &&
                Publication->SurfaceId == Request.SurfaceId &&
                Publication->Generation == Request.Generation &&
                Publication->UpdateId == Request.UpdateId)
            {
                IntCompositionCompleteDxPublication(Publication);
                goto CopyOutput;
            }
        }
        if (PsGetCurrentProcess() != g_DwmProcess)
            return STATUS_ACCESS_DENIED;
        if (Request.SurfaceId >= g_RedirectHighWater)
            return STATUS_INVALID_PARAMETER;

        Entry = &g_Redirects[Request.SurfaceId];
        if (Entry->Wnd == NULL ||
            Entry->Redirect.DxGeneration != Request.Generation ||
            Request.UpdateId == 0 ||
            Request.UpdateId != Entry->Redirect.DxPublishedUpdateId)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (Request.UpdateId > Entry->Redirect.DxConsumedUpdateId)
            Entry->Redirect.DxConsumedUpdateId = Request.UpdateId;
        if (Entry->Redirect.DxReadyEvent != NULL)
            KeSetEvent(Entry->Redirect.DxReadyEvent, IO_NO_INCREMENT, FALSE);
        goto CopyOutput;
    }

    if (!gbCompositionEnabled || !g_DwmAttached)
        return STATUS_DEVICE_NOT_READY;

    if (Request.Window == 0 ||
        Request.Window > (ULONGLONG)MAXULONG_PTR)
    {
        return STATUS_INVALID_HANDLE;
    }

    SourceWnd = UserGetWindowObject((HWND)(ULONG_PTR)Request.Window);
    TopWnd = IntCompositionTopLevel(SourceWnd);
    ProcessInfo = PsGetCurrentProcessWin32Process();
    if (SourceWnd == NULL && Request.Action == DWM_DX_SURFACE_UNREGISTER)
        return STATUS_NOT_FOUND;
    if (SourceWnd == NULL || TopWnd == NULL || SourceWnd->head.pti == NULL ||
        ProcessInfo == NULL || SourceWnd->head.pti->ppi != ProcessInfo)
    {
        return STATUS_ACCESS_DENIED;
    }

    Entry = IntCompositionFind(TopWnd);
    if (Entry == NULL &&
        (Request.Action == DWM_DX_SURFACE_REGISTER ||
         Request.Action == DWM_DX_SURFACE_PUBLISH) &&
        IntCompositionIsCompositable(TopWnd))
    {
        IntCompositionOnWindowCreate(TopWnd);
        Entry = IntCompositionFind(TopWnd);
    }
    if (Entry == NULL)
        return STATUS_NOT_FOUND;

    switch (Request.Action)
    {
        case DWM_DX_SURFACE_REGISTER:
        case DWM_DX_SURFACE_PUBLISH:
        {
            ULONG ClientWidth = SourceWnd->rcClient.right - SourceWnd->rcClient.left;
            ULONG ClientHeight = SourceWnd->rcClient.bottom - SourceWnd->rcClient.top;
            BOOL Publish = Request.Action == DWM_DX_SURFACE_PUBLISH;
            BOOL SameResource;
            PKEVENT ReadyEvent;
            DWM_DX_PUBLICATION *Publication = NULL;

            if (Request.GlobalShare == 0 || Request.ReadyEvent == 0 ||
                Request.ReadyEvent > (ULONGLONG)MAXULONG_PTR ||
                Request.Info.Magic != DWM_DX_SURFACE_INFO_MAGIC ||
                Request.Info.Width == 0 || Request.Info.Height == 0 ||
                Request.Info.Width != ClientWidth ||
                Request.Info.Height != ClientHeight ||
                Request.Info.Width > MAXLONG || Request.Info.Height > MAXLONG)
            {
                return STATUS_INVALID_PARAMETER;
            }
            if (Publish)
            {
                if (Request.Info.Version != DWM_DX_SURFACE_INFO_VERSION_GPU ||
                    Request.Info.Pitch != 0 || Request.Flags != 0 ||
                    (Request.Info.Format != DWM_DX_FORMAT_B8G8R8A8_UNORM &&
                     Request.Info.Format != DWM_DX_FORMAT_R8G8B8A8_UNORM) ||
                    Request.UpdateRect.left != 0 || Request.UpdateRect.top != 0 ||
                    (ULONG)Request.UpdateRect.right != ClientWidth ||
                    (ULONG)Request.UpdateRect.bottom != ClientHeight)
                {
                    return STATUS_INVALID_PARAMETER;
                }
            }
            else if (Request.Info.Version != DWM_DX_SURFACE_INFO_VERSION ||
                     Request.Info.Format != DWM_DX_FORMAT_B8G8R8A8_UNORM ||
                     Request.Info.Width > MAXULONG / sizeof(ULONG) ||
                     Request.Info.Pitch < Request.Info.Width * sizeof(ULONG) ||
                     Request.Info.Height > MAXULONG / Request.Info.Pitch)
            {
                return STATUS_INVALID_PARAMETER;
            }
            /* Replacing a registration must not release a buffer which the
             * compositor has already received and may still be reading. */
            if (Entry->Redirect.DxIssuedUpdateId > Entry->Redirect.DxConsumedUpdateId)
                return STATUS_DEVICE_BUSY;
            if (Publish)
            {
                ULONG Index;
#if defined(REACTOS_GRAPHICS_DRIVER_MODEL_WDDM)
                Status = WddmBridgeValidateSharedResourceOwner(&Request.AdapterLuid, Request.GlobalShare);
#else
                Status = STATUS_NOT_SUPPORTED;
#endif
                if (!NT_SUCCESS(Status))
                    return Status;
                for (Index = 0; Index < ARRAYSIZE(g_DxPublications); ++Index)
                {
                    if (g_DxPublications[Index].ReadyEvent == NULL)
                    {
                        Publication = &g_DxPublications[Index];
                        break;
                    }
                }
                if (Publication == NULL)
                    return STATUS_INSUFFICIENT_RESOURCES;
            }

            Status = ObReferenceObjectByHandle(
                (HANDLE)(ULONG_PTR)Request.ReadyEvent,
                EVENT_MODIFY_STATE,
                *ExEventObjectType,
                UserMode,
                (PVOID *)&ReadyEvent,
                NULL);
            if (!NT_SUCCESS(Status))
                return Status;
            if (Publish && ReadyEvent->Header.Type != NotificationEvent)
            {
                ObDereferenceObject(ReadyEvent);
                return STATUS_INVALID_PARAMETER;
            }

            SameResource = Entry->Redirect.DxWindow == Request.Window &&
                           Entry->Redirect.DxGlobalShare == Request.GlobalShare &&
                           RtlEqualMemory(&Entry->Redirect.DxAdapterLuid,
                                          &Request.AdapterLuid, sizeof(Request.AdapterLuid)) &&
                           RtlEqualMemory(&Entry->Redirect.DxInfo, &Request.Info, sizeof(Request.Info));

            if (Publish)
            {
                Request.SurfaceId = (ULONG)(Entry - g_Redirects);
                Request.Generation = Entry->Redirect.DxGeneration;
                if (!SameResource)
                {
                    Request.Generation = ++g_FrontGeneration;
                    if (Request.Generation == 0)
                        Request.Generation = ++g_FrontGeneration;
                }
                if (++g_DxUpdateSequence == 0)
                    ++g_DxUpdateSequence;
                Request.UpdateId = g_DxUpdateSequence;
                /* A failed output copy must not leave an admitted frame that
                 * its producer believes failed. USER excludes GETFRAME while
                 * the complete result is copied and then committed below. */
                _SEH2_TRY
                {
                    *(PDWM_DX_SURFACE_EXCHANGE)pUser = Request;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(Status))
                {
                    ObDereferenceObject(ReadyEvent);
                    return Status;
                }
            }

            if (Entry->Redirect.DxReadyEvent != NULL)
            {
                KeSetEvent(Entry->Redirect.DxReadyEvent,
                           IO_NO_INCREMENT,
                           FALSE);
                ObDereferenceObject(Entry->Redirect.DxReadyEvent);
            }

            Entry->Redirect.DxGlobalShare = Request.GlobalShare;
            Entry->Redirect.DxAdapterLuid = Request.AdapterLuid;
            Entry->Redirect.DxInfo = Request.Info;
            Entry->Redirect.DxWindow = Request.Window;
            Entry->Redirect.DxClientX =
                SourceWnd->rcClient.left - TopWnd->rcWindow.left;
            Entry->Redirect.DxClientY =
                SourceWnd->rcClient.top - TopWnd->rcWindow.top;
            Entry->Redirect.DxReadyEvent = ReadyEvent;
            if (!Publish)
            {
                Entry->Redirect.DxGeneration = ++g_FrontGeneration;
                if (Entry->Redirect.DxGeneration == 0)
                    Entry->Redirect.DxGeneration = ++g_FrontGeneration;
            }

            if (Publish)
            {
                Entry->Redirect.DxGeneration = Request.Generation;
                Entry->Redirect.DxIssuedUpdateId = Request.UpdateId;
                Entry->Redirect.DxPublishedUpdateId = Request.UpdateId;
                KeClearEvent(ReadyEvent);
                ObReferenceObject(ReadyEvent);
                Publication->ReadyEvent = ReadyEvent;
                ObReferenceObject(g_DwmProcess);
                Publication->Compositor = g_DwmProcess;
                Publication->SurfaceId = (ULONG)(Entry - g_Redirects);
                Publication->Generation = Entry->Redirect.DxGeneration;
                Publication->UpdateId = Request.UpdateId;
                IntCompositionDamageDxPublication(Entry, TopWnd);
                return STATUS_SUCCESS;
            }
            else
            {
                Entry->Redirect.DxIssuedUpdateId = 0;
                Entry->Redirect.DxPublishedUpdateId = 0;
                Entry->Redirect.DxConsumedUpdateId = 0;
            }

            Request.SurfaceId = (ULONG)(Entry - g_Redirects);
            Request.Generation = Entry->Redirect.DxGeneration;
            break;
        }

        case DWM_DX_SURFACE_UNREGISTER:
            if (Request.Window != Entry->Redirect.DxWindow ||
                Request.GlobalShare != Entry->Redirect.DxGlobalShare ||
                Request.Generation != Entry->Redirect.DxGeneration ||
                !RtlEqualMemory(&Request.AdapterLuid, &Entry->Redirect.DxAdapterLuid,
                                sizeof(Request.AdapterLuid)))
            {
                /* The old tuple was retired or replaced. Do not unregister
                 * a new owner's publication when an old swapchain releases. */
                return STATUS_NOT_FOUND;
            }
            if (Entry->Redirect.DxIssuedUpdateId > Entry->Redirect.DxConsumedUpdateId)
                return STATUS_DEVICE_BUSY;
            IntCompositionFreeDxSurface(&Entry->Redirect);
            IntCompositionDamageDxPublication(Entry, TopWnd);
            break;

        case DWM_DX_SURFACE_ISSUE:
            if (Request.Window != Entry->Redirect.DxWindow ||
                !IntCompositionUpdateDxPlacement(SourceWnd, TopWnd,
                                                 &Entry->Redirect) ||
                Request.GlobalShare != Entry->Redirect.DxGlobalShare ||
                !RtlEqualMemory(&Request.AdapterLuid,
                                &Entry->Redirect.DxAdapterLuid,
                                sizeof(Request.AdapterLuid)))
            {
                return STATUS_INVALID_PARAMETER;
            }
            if (Entry->Redirect.DxIssuedUpdateId >
                Entry->Redirect.DxConsumedUpdateId)
            {
                return STATUS_DEVICE_BUSY;
            }
            if (Entry->Redirect.DxReadyEvent == NULL)
                return STATUS_INVALID_DEVICE_STATE;

            if (++g_DxUpdateSequence == 0)
                ++g_DxUpdateSequence;
            Entry->Redirect.DxIssuedUpdateId = g_DxUpdateSequence;
            KeClearEvent(Entry->Redirect.DxReadyEvent);
            Request.SurfaceId = (ULONG)(Entry - g_Redirects);
            Request.Generation = Entry->Redirect.DxGeneration;
            Request.UpdateId = g_DxUpdateSequence;
            break;

        case DWM_DX_SURFACE_ISSUE_GDI:
            if (IntCompositionEnsureSurface(TopWnd, &Entry->Redirect) == NULL ||
                Entry->Redirect.BackGlobalShare == 0)
            {
                return STATUS_NOT_SUPPORTED;
            }
            if (Entry->Redirect.GdiIssuedUpdateId >
                Entry->Redirect.GdiConsumedUpdateId)
            {
                return STATUS_DEVICE_BUSY;
            }

            if (++g_GdiUpdateSequence == 0)
                ++g_GdiUpdateSequence;
            Entry->Redirect.GdiIssuedUpdateId = g_GdiUpdateSequence;
            Entry->Redirect.GdiAdmittedUpdateId =
                Entry->Redirect.GdiConsumedUpdateId;
            Entry->Redirect.GdiPublishedUpdateId =
                Entry->Redirect.GdiConsumedUpdateId;

            Request.GlobalShare = Entry->Redirect.BackGlobalShare;
            Request.SurfaceId = (ULONG)(Entry - g_Redirects);
            Request.Generation = Entry->Redirect.BackGeneration;
            Request.UpdateId = g_GdiUpdateSequence;
            Request.Info.Magic = DWM_DX_SURFACE_INFO_MAGIC;
            Request.Info.Version = DWM_DX_SURFACE_INFO_VERSION;
            Request.Info.Width = (ULONG)Entry->Redirect.cx;
            Request.Info.Height = (ULONG)Entry->Redirect.cy;
            Request.Info.Pitch =
                (ULONG)Entry->Redirect.psurf->SurfObj.lDelta;
            Request.Info.Format = DWM_DX_FORMAT_B8G8R8A8_UNORM;
            Request.UpdateRect = Entry->Redirect.rcClient;
            break;

        case DWM_DX_SURFACE_CANCEL_GDI:
            if (Request.UpdateId == 0 ||
                Request.UpdateId != Entry->Redirect.GdiIssuedUpdateId ||
                Request.UpdateId <= Entry->Redirect.GdiConsumedUpdateId)
            {
                return STATUS_INVALID_PARAMETER;
            }
            if (Entry->Redirect.GdiAdmittedUpdateId == Request.UpdateId)
                return STATUS_DEVICE_BUSY;
            Entry->Redirect.GdiConsumedUpdateId = Request.UpdateId;
            break;

        case DWM_DX_SURFACE_UPDATE:
            if (Request.Window != Entry->Redirect.DxWindow ||
                !IntCompositionUpdateDxPlacement(SourceWnd, TopWnd,
                                                 &Entry->Redirect) ||
                Request.UpdateId == 0 ||
                Request.UpdateId != Entry->Redirect.DxIssuedUpdateId)
            {
                return STATUS_INVALID_PARAMETER;
            }

            if (Request.Flags & DWM_DX_UPDATE_CANCEL)
            {
                if (Request.Flags != DWM_DX_UPDATE_CANCEL ||
                    Request.UpdateId <= Entry->Redirect.DxConsumedUpdateId)
                {
                    return STATUS_INVALID_PARAMETER;
                }
                if (Request.UpdateId == Entry->Redirect.DxPublishedUpdateId)
                    return STATUS_DEVICE_BUSY;
                Entry->Redirect.DxConsumedUpdateId = Request.UpdateId;
                if (Entry->Redirect.DxReadyEvent != NULL)
                    KeSetEvent(Entry->Redirect.DxReadyEvent,
                               IO_NO_INCREMENT,
                               FALSE);
                break;
            }
            /* The ICD update rectangle addresses the client-sized shared
             * allocation. The compositor applies rcClient when placing that
             * allocation in the top-level window backing store. */
            if ((Request.Flags & ~1u) != 0 ||
                Request.UpdateRect.left < 0 ||
                Request.UpdateRect.top < 0 ||
                Request.UpdateRect.right <= Request.UpdateRect.left ||
                Request.UpdateRect.bottom <= Request.UpdateRect.top ||
                (ULONG)Request.UpdateRect.right > Entry->Redirect.DxInfo.Width ||
                (ULONG)Request.UpdateRect.bottom > Entry->Redirect.DxInfo.Height)
            {
                return STATUS_INVALID_PARAMETER;
            }

            Entry->Redirect.DxPublishedUpdateId = Request.UpdateId;
            IntCompositionDamageDxPublication(Entry, TopWnd);
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

CopyOutput:
    _SEH2_TRY
    {
        *(PDWM_DX_SURFACE_EXCHANGE)pUser = Request;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

NTSTATUS
IntCompositionDwmSetBlur(_In_ PVOID pUser)
{
    DWM_BLUR_REQUEST Request;
    REDIRECT_ENTRY *Entry;
    PRECTL NewRects = NULL;
    ULONG NewRectCount = 0;
    PWND Wnd;
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        ProbeForRead(pUser, sizeof(Request), sizeof(ULONG));
        Request = *(PDWM_BLUR_REQUEST)pUser;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Request.StructSize != sizeof(Request) ||
        Request.Flags == 0 ||
        (Request.Flags & ~DWM_BLUR_REQUEST_VALID_FLAGS) != 0 ||
        Request.Window == 0 || Request.Window > (ULONGLONG)MAXULONG_PTR ||
        Request.Region > (ULONGLONG)MAXULONG_PTR)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!gbCompositionEnabled)
        return STATUS_DEVICE_NOT_READY;

    Wnd = UserGetWindowObject((HWND)(ULONG_PTR)Request.Window);
    if (Wnd == NULL)
        return STATUS_INVALID_HANDLE;

    if ((Request.Flags & DWM_BLUR_REQUEST_REGION) && Request.Region != 0)
    {
        PREGION Region = REGION_LockRgn((HRGN)(ULONG_PTR)Request.Region);
        SIZE_T Bytes;

        if (Region == NULL)
            return STATUS_INVALID_HANDLE;
        NewRectCount = Region->rdh.nCount;
        if (NewRectCount > DWM_MAX_BLUR_RECTS)
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        else if (NewRectCount != 0)
        {
            Bytes = (SIZE_T)NewRectCount * sizeof(*NewRects);
            NewRects = ExAllocatePoolWithTag(PagedPool, Bytes, 'rBwD');
            if (NewRects == NULL)
                Status = STATUS_NO_MEMORY;
            else
                RtlCopyMemory(NewRects, Region->Buffer, Bytes);
        }
        REGION_UnlockRgn(Region);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Entry = IntCompositionFind(Wnd);
    if (Entry == NULL)
        Entry = IntCompositionAlloc(Wnd);
    if (Entry == NULL)
    {
        if (NewRects != NULL)
            ExFreePoolWithTag(NewRects, 'rBwD');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Request.Flags & DWM_BLUR_REQUEST_ENABLE)
    {
        if (Request.Enable)
            Entry->BlurFlags |= DWM_BLUR_ENABLE;
        else
            Entry->BlurFlags &= ~DWM_BLUR_ENABLE;
    }
    if (Request.Flags & DWM_BLUR_REQUEST_TRANSITION)
    {
        if (Request.TransitionOnMaximized)
            Entry->BlurFlags |= DWM_BLUR_TRANSITION_ON_MAXIMIZED;
        else
            Entry->BlurFlags &= ~DWM_BLUR_TRANSITION_ON_MAXIMIZED;
    }
    if (Request.Flags & DWM_BLUR_REQUEST_REGION)
    {
        if (Entry->BlurRects != NULL)
            ExFreePoolWithTag(Entry->BlurRects, 'rBwD');
        Entry->BlurRects = NewRects;
        Entry->BlurRectCount = NewRectCount;
        if (Request.Region == 0)
            Entry->BlurFlags |= DWM_BLUR_REGION_ENTIRE_WINDOW;
        else
            Entry->BlurFlags &= ~DWM_BLUR_REGION_ENTIRE_WINDOW;
        NewRects = NULL;
    }

    Entry->Damaged = TRUE;
    IntCompositionMarkDamage(TRUE);
    return STATUS_SUCCESS;
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
    pdc->dhpdev = pdc->ppdev->dhpdev;
    pdc->flGraphicsCaps = pdc->ppdev->devinfo.flGraphicsCaps;
    pdc->flGraphicsCaps2 = pdc->ppdev->devinfo.flGraphicsCaps2;
    pdc->pdcattr->ulDirty_ |= DIRTY_CHARSET | DIRTY_BACKGROUND | DIRTY_TEXT | DIRTY_LINE | DIRTY_FILL;
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
    /* Public DC queries still use screen coordinates, including when a
     * move leaves the origin within the backing surface unchanged. */
    pdc->erclWindow = rcOwn;

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

    /* Keep the old surface+clip pair live while the replacement region is
     * built. Installing the backing before its VisRgn opened a race where a
     * drawing thread used screen-space clipping against backing coordinates. */
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

    pdc = DC_LockDc(hDC);
    if (pdc == NULL)
    {
        REGION_Delete(prgn);
        return;
    }
    if (pdc->dctype != DCTYPE_DIRECT)
    {
        DC_UnlockDc(pdc);
        REGION_Delete(prgn);
        return;
    }

    /* Publish surface, origin and visibility atomically under the DC lock. */
    DC_vSelectSurface(pdc, e->Redirect.psurf);
    pdc->ptlDCOrig.x = rcOwn.left - ancestor->rcWindow.left;
    pdc->ptlDCOrig.y = rcOwn.top - ancestor->rcWindow.top;
    pdc->dclevel.sizl.cx = e->Redirect.cx;
    pdc->dclevel.sizl.cy = e->Redirect.cy;
    pdc->fs |= DC_REDIRECTION | DC_DIRTY_RAO;
    REGION_bCopy(pdc->prgnVis, prgn);
    REGION_bOffsetRgn(pdc->prgnVis, -pdc->ptlDCOrig.x, -pdc->ptlDCOrig.y);
    DC_UnlockDc(pdc);
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
    if (g_DwmGpuOutputWindow == NULL && g_DwmGpuOutputOwner.Generation != 0)
        (VOID)IntCompositionReleaseGpuOutput(TRUE);

    if (!g_DwmAttached || g_DwmProcess == NULL)
        return;
    if ((LONGLONG)KeQueryInterruptTime() - g_DwmLastFrameTime <= (300LL * 1000000LL))
        return; /* alive within 30 s */

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
        g_CompositionPositionDamageValid = FALSE;
        IntCompositionMarkDamage(TRUE);
    }
    else
    {
        ULONG i;
        DceUnredirectAllDCs();
        for (i = 0; i < COMPOSITION_MAX_WINDOWS; i++)
        {
            if (g_Redirects[i].Wnd != NULL)
            {
                IntCompositionFreeSurface(&g_Redirects[i].Redirect, FALSE);
                IntCompositionFreeBlur(&g_Redirects[i]);
                g_Redirects[i].Wnd = NULL;
            }
        }
        for (i = 0; i < COMPOSITION_MAX_GL; i++)
            g_GlWindows[i] = NULL;
        g_CompositionPositionDamageValid = FALSE;
    }

    return STATUS_SUCCESS;
}
