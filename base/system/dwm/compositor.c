/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Window composition engine -- CPU-based Phase 1
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Overview
 * --------
 * This file implements the DWM composition engine.  It is responsible for:
 *
 *   1. Window list management -- tracking all visible top-level windows,
 *      their positions, sizes, and Z-order.
 *
 *   2. Redirection surface management -- allocating and managing the
 *      off-screen buffers that capture each window's rendered content.
 *
 *   3. Composition -- blitting all window surfaces into the full-screen
 *      composition buffer in bottom-to-top Z-order.
 *
 *   4. Presentation -- calling D3DKMTPresent to push the composed frame
 *      to the display through dxgkrnl.
 *
 * Phase 1 design
 * --------------
 * All compositing is done in software using memcpy.  Each window's
 * content is captured via PrintWindow (which renders the window into
 * a memory DC) and stored in a 32-bpp system-memory surface.  The
 * composition loop walks windows bottom-to-top and memcpy-blits each
 * surface into the composition buffer with clipping.
 *
 * This is architecturally identical to what Windows Vista DWM does,
 * except the GPU shader pass is replaced with CPU blitting.  The
 * window list, Z-order tracking, dirty region tracking, and present
 * coordination are all real and will carry forward to Phase 2.
 *
 * Phase 2 changes
 * ---------------
 * - Replace system-memory surfaces with D3DKMT shared allocations
 * - Replace memcpy blits with GPU texture sampling / shader pass
 * - CDD (Canonical Display Driver) will render directly to shared
 *   D3DKMT allocations instead of the GDI framebuffer
 *
 * Window capture strategy
 * -----------------------
 * Phase 1 uses PrintWindow to capture window content.  This works by
 * sending WM_PRINT/WM_PRINTCLIENT to the target window's thread,
 * which renders the window into a provided DC.  This is the same
 * mechanism that DWM uses when it needs to capture a window's content
 * for the first time or when WS_EX_REDIRECTED is not set.
 *
 * In the full DWM model, windows have WS_EX_REDIRECTED set which
 * causes all GDI output to go to the redirection surface instead of
 * the screen.  Phase 1 does not set this flag -- it captures on
 * demand.  Phase 2 will enable redirection.
 */

#include "dwm.h"

WINE_DEFAULT_DEBUG_CHANNEL(dwm);

static LONG gDwmpEnumWindowLogCount;
static LONG gDwmpCaptureWindowLogCount;

static BOOL
DwmpShouldLogSerialSample(
    _In_ LONG Sample)
{
    return (Sample <= 32 || ((Sample % 128) == 0));
}

static BOOL
DwmpShouldLogFrame(
    _In_ PDWM_CONTEXT pCtx)
{
    UINT64 FrameId = pCtx->FramesComposed + 1;

    return (FrameId <= 5 || (FrameId % 60) == 0);
}

/* ========================================================================
 * Redirection surface allocation / free
 *
 * All surfaces are 32-bpp XRGB (B8G8R8X8 byte order).  This matches
 * the D3DDDIFMT_X8R8G8B8 format used by WDDM for shared primary
 * surfaces, ensuring zero-copy transition to Phase 2 GPU surfaces.
 * ====================================================================== */

BOOL
DwmSurfaceAlloc(
    _Out_ PDWM_SURFACE pSurface,
    _In_ UINT Width,
    _In_ UINT Height)
{
    ZeroMemory(pSurface, sizeof(DWM_SURFACE));

    if (Width == 0 || Height == 0)
        return FALSE;

    if (Width > ((UINT)-1 / 4))
        return FALSE;

    pSurface->Width = Width;
    pSurface->Height = Height;
    pSurface->BytesPerPixel = 4;

    /* Stride: Width * 4 bytes, no extra padding needed for 32-bpp */
    pSurface->Stride = Width * pSurface->BytesPerPixel;
    if (Height > ((SIZE_T)-1 / pSurface->Stride))
    {
        ZeroMemory(pSurface, sizeof(DWM_SURFACE));
        return FALSE;
    }

    pSurface->AllocationSize = (SIZE_T)pSurface->Stride * Height;

    pSurface->pPixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                  pSurface->AllocationSize);
    if (!pSurface->pPixels)
    {
        ERR("DwmSurfaceAlloc: HeapAlloc failed for %ux%u (%Iu bytes)\n",
            Width, Height, pSurface->AllocationSize);
        ZeroMemory(pSurface, sizeof(DWM_SURFACE));
        return FALSE;
    }

    pSurface->Dirty = TRUE;
    return TRUE;
}

VOID
DwmSurfaceFree(
    _Inout_ PDWM_SURFACE pSurface)
{
    if (pSurface->pPixels)
    {
        HeapFree(GetProcessHeap(), 0, pSurface->pPixels);
        pSurface->pPixels = NULL;
    }
    ZeroMemory(pSurface, sizeof(DWM_SURFACE));
}

BOOL
DwmSurfaceResize(
    _Inout_ PDWM_SURFACE pSurface,
    _In_ UINT NewWidth,
    _In_ UINT NewHeight)
{
    /* No-op if dimensions haven't changed */
    if (pSurface->Width == NewWidth && pSurface->Height == NewHeight
        && pSurface->pPixels != NULL)
    {
        return TRUE;
    }

    DwmSurfaceFree(pSurface);
    return DwmSurfaceAlloc(pSurface, NewWidth, NewHeight);
}

/* ========================================================================
 * CPU blit -- core compositing primitive
 *
 * Copies a rectangular region from a source surface into the
 * composition buffer at the specified screen position.  Handles
 * clipping against the destination bounds.
 *
 * This is the Phase 1 equivalent of a GPU textured quad draw.
 * ====================================================================== */

VOID
DwmBlitSurface(
    _In_ PDWM_COMPOSITION_BUFFER pDst,
    _In_ INT DstX,
    _In_ INT DstY,
    _In_ PDWM_SURFACE pSrc,
    _In_ PRECT pSrcRect)
{
    INT SrcX, SrcY, SrcW, SrcH;
    INT ClipX, ClipY, ClipW, ClipH;
    INT Row;
    BYTE *pDstRow, *pSrcRow;

    if (!pDst || !pSrc || !pDst->pPixels || !pSrc->pPixels)
        return;

    /* Source rectangle */
    if (pSrcRect)
    {
        if (pSrcRect->left >= pSrcRect->right ||
            pSrcRect->top >= pSrcRect->bottom)
        {
            return;
        }

        SrcX = pSrcRect->left;
        SrcY = pSrcRect->top;
        SrcW = pSrcRect->right - pSrcRect->left;
        SrcH = pSrcRect->bottom - pSrcRect->top;
    }
    else
    {
        SrcX = 0;
        SrcY = 0;
        SrcW = (INT)pSrc->Width;
        SrcH = (INT)pSrc->Height;
    }

    /* Clamp source rect to source surface bounds */
    if (SrcX < 0) { SrcW += SrcX; DstX -= SrcX; SrcX = 0; }
    if (SrcY < 0) { SrcH += SrcY; DstY -= SrcY; SrcY = 0; }
    if (SrcX + SrcW > (INT)pSrc->Width)  SrcW = (INT)pSrc->Width - SrcX;
    if (SrcY + SrcH > (INT)pSrc->Height) SrcH = (INT)pSrc->Height - SrcY;

    /* Clamp to destination bounds */
    ClipX = DstX;
    ClipY = DstY;
    ClipW = SrcW;
    ClipH = SrcH;

    if (ClipX < 0) { SrcX -= ClipX; ClipW += ClipX; ClipX = 0; }
    if (ClipY < 0) { SrcY -= ClipY; ClipH += ClipY; ClipY = 0; }
    if (ClipX + ClipW > (INT)pDst->Width)  ClipW = (INT)pDst->Width - ClipX;
    if (ClipY + ClipH > (INT)pDst->Height) ClipH = (INT)pDst->Height - ClipY;

    /* Nothing to blit after clipping */
    if (ClipW <= 0 || ClipH <= 0)
        return;

    /* Row-by-row memcpy blit */
    for (Row = 0; Row < ClipH; Row++)
    {
        pDstRow = (BYTE*)pDst->pPixels +
                  (SIZE_T)(ClipY + Row) * pDst->Stride +
                  (SIZE_T)ClipX * 4;
        pSrcRow = (BYTE*)pSrc->pPixels +
                  (SIZE_T)(SrcY + Row) * pSrc->Stride +
                  (SIZE_T)SrcX * 4;
        memcpy(pDstRow, pSrcRow, (SIZE_T)ClipW * 4);
    }
}

/* ========================================================================
 * Dirty region management
 * ====================================================================== */

VOID
DwmMarkDirty(
    _Inout_ PDWM_DIRTY_REGION pDirty,
    _In_ PRECT pRect)
{
    if (!pDirty || !pRect || pRect->left >= pRect->right ||
        pRect->top >= pRect->bottom)
    {
        return;
    }

    if (!pDirty->HasDirty)
    {
        pDirty->rcBounds = *pRect;
        pDirty->HasDirty = TRUE;
    }
    else
    {
        /* Expand bounding box to include the new rect */
        if (pRect->left < pDirty->rcBounds.left)
            pDirty->rcBounds.left = pRect->left;
        if (pRect->top < pDirty->rcBounds.top)
            pDirty->rcBounds.top = pRect->top;
        if (pRect->right > pDirty->rcBounds.right)
            pDirty->rcBounds.right = pRect->right;
        if (pRect->bottom > pDirty->rcBounds.bottom)
            pDirty->rcBounds.bottom = pRect->bottom;
    }
}

VOID
DwmClearDirty(
    _Inout_ PDWM_DIRTY_REGION pDirty)
{
    ZeroMemory(pDirty, sizeof(DWM_DIRTY_REGION));
}

/* ========================================================================
 * Window list management
 * ====================================================================== */

/*
 * DwmpFindWindowEntry
 *
 * Searches the window list for an entry matching the given HWND.
 * Must be called with csWindowList held.
 */
static PDWM_WINDOW_ENTRY
DwmpFindWindowEntry(
    _In_ PDWM_CONTEXT pCtx,
    _In_ HWND hWnd)
{
    PDWM_WINDOW_ENTRY pEntry = pCtx->pWindowListHead;
    while (pEntry)
    {
        if (pEntry->hWnd == hWnd)
            return pEntry;
        pEntry = pEntry->pNext;
    }
    return NULL;
}

/*
 * DwmpAddWindowEntry
 *
 * Creates a new window tracking entry and adds it to the head of
 * the list.  Must be called with csWindowList held.
 */
static PDWM_WINDOW_ENTRY
DwmpAddWindowEntry(
    _Inout_ PDWM_CONTEXT pCtx,
    _In_ HWND hWnd)
{
    PDWM_WINDOW_ENTRY pEntry;

    pEntry = (PDWM_WINDOW_ENTRY)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                          sizeof(DWM_WINDOW_ENTRY));
    if (!pEntry)
    {
        ERR("DwmpAddWindowEntry: HeapAlloc failed\n");
        return NULL;
    }

    pEntry->hWnd = hWnd;
    pEntry->Visible = TRUE;

    /* Insert at head of list */
    pEntry->pNext = pCtx->pWindowListHead;
    pEntry->pPrev = NULL;
    if (pCtx->pWindowListHead)
        pCtx->pWindowListHead->pPrev = pEntry;
    pCtx->pWindowListHead = pEntry;
    pCtx->WindowCount++;

    return pEntry;
}

/*
 * DwmpRemoveWindowEntry
 *
 * Removes a window tracking entry from the list and frees its
 * redirection surface.  Must be called with csWindowList held.
 */
static VOID
DwmpRemoveWindowEntry(
    _Inout_ PDWM_CONTEXT pCtx,
    _Inout_ PDWM_WINDOW_ENTRY pEntry)
{
    /* Unlink from doubly-linked list */
    if (pEntry->pPrev)
        pEntry->pPrev->pNext = pEntry->pNext;
    else
        pCtx->pWindowListHead = pEntry->pNext;

    if (pEntry->pNext)
        pEntry->pNext->pPrev = pEntry->pPrev;

    pCtx->WindowCount--;

    /* Free the redirection surface */
    DwmSurfaceFree(&pEntry->Surface);

    HeapFree(GetProcessHeap(), 0, pEntry);
}

/*
 * DwmpEnumWindowsCallback
 *
 * EnumWindows callback.  For each visible top-level window, ensures
 * a tracking entry exists and updates its position/size.
 *
 * The lParam receives a pointer to the DWM_CONTEXT.
 * We also use the ZOrder field as a "touched" flag: during
 * enumeration we set it to UINT_MAX to mark entries as live.
 * After enumeration, entries not marked are stale and get removed.
 */

/* Temporary struct passed through the EnumWindows callback */
typedef struct _DWM_ENUM_CONTEXT
{
    PDWM_CONTEXT    pCtx;
    UINT            ZOrderCounter;
} DWM_ENUM_CONTEXT, *PDWM_ENUM_CONTEXT;

static BOOL CALLBACK
DwmpEnumWindowsCallback(
    _In_ HWND hWnd,
    _In_ LPARAM lParam)
{
    PDWM_ENUM_CONTEXT pEnumCtx = (PDWM_ENUM_CONTEXT)lParam;
    PDWM_CONTEXT pCtx = pEnumCtx->pCtx;
    PDWM_WINDOW_ENTRY pEntry;
    RECT rcWnd;
    CHAR Title[128];
    INT Width, Height;

    /* Skip invisible windows */
    if (!IsWindowVisible(hWnd))
        return TRUE;

    /* Skip minimized windows (they don't contribute to composition) */
    if (IsIconic(hWnd))
        return TRUE;

    /* Get the window rectangle in screen coordinates */
    if (!GetWindowRect(hWnd, &rcWnd))
        return TRUE;

    /* Skip zero-size windows */
    Width = rcWnd.right - rcWnd.left;
    Height = rcWnd.bottom - rcWnd.top;
    if (Width <= 0 || Height <= 0)
        return TRUE;

    /*
     * EnumWindows returns windows in Z-order (top to bottom).
     * We assign ZOrder as a counter so that higher ZOrderCounter
     * values = lower in Z-order = drawn first during composition.
     * We invert this during composition so bottom-most gets drawn first.
     */

    /* Find or create entry */
    pEntry = DwmpFindWindowEntry(pCtx, hWnd);
    if (!pEntry)
    {
        pEntry = DwmpAddWindowEntry(pCtx, hWnd);
        if (!pEntry)
            return TRUE;
    }

    /* Update position */
    if (memcmp(&pEntry->rcWindow, &rcWnd, sizeof(RECT)) != 0)
    {
        /* Window moved or resized -- mark old and new positions dirty */
        DwmMarkDirty(&pCtx->DirtyRegion, &pEntry->rcWindow);
        DwmMarkDirty(&pCtx->DirtyRegion, &rcWnd);
        pEntry->rcWindow = rcWnd;
        pEntry->Surface.Dirty = TRUE;
    }

    /* Ensure surface is allocated at the correct size */
    if (!DwmSurfaceResize(&pEntry->Surface, (UINT)Width, (UINT)Height))
    {
        WARN("DwmpEnumWindowsCallback: Surface resize failed for HWND %p\n",
             hWnd);
    }

    if (DwmpShouldLogFrame(pCtx) && pEnumCtx->ZOrderCounter < 8)
    {
        CHAR ClassName[64];
        DWORD ExStyle;
        LONG Sample;
        DWORD Style;

        ClassName[0] = '\0';
        Title[0] = '\0';
        GetClassNameA(hWnd, ClassName, sizeof(ClassName));
        GetWindowTextA(hWnd, Title, sizeof(Title));
        Style = (DWORD)GetWindowLongPtrA(hWnd, GWL_STYLE);
        ExStyle = (DWORD)GetWindowLongPtrA(hWnd, GWL_EXSTYLE);

        Sample = InterlockedIncrement(&gDwmpEnumWindowLogCount);
        if (DwmpShouldLogSerialSample(Sample))
        {
            TRACE("DwmpEnumWindows[%ld]: frame=%I64u hwnd=%p z=%u class='%s' style=0x%08lx ex=0x%08lx rect=(%ld,%ld)-(%ld,%ld) title='%s'\n",
                  Sample,
                  pCtx->FramesComposed + 1,
                  hWnd,
                  pEnumCtx->ZOrderCounter,
                  ClassName,
                  Style,
                  ExStyle,
                  rcWnd.left,
                  rcWnd.top,
                  rcWnd.right,
                  rcWnd.bottom,
                  Title);
        }
    }

    /* Detect OpenGL windows: if a pixel format is set, the window has an
     * active OpenGL rendering context.  These need BitBlt-from-screen
     * capture (PrintWindow can't see GL content) and must be re-captured
     * every frame since GL content changes continuously. */
    {
        HDC hWndDC = GetDC(hWnd);
        if (hWndDC)
        {
            pEntry->IsOpenGLWindow = (GetPixelFormat(hWndDC) != 0);
            ReleaseDC(hWnd, hWndDC);
        }
    }

    pEntry->Visible = TRUE;
    pEntry->ZOrder = pEnumCtx->ZOrderCounter++;

    return TRUE;
}

/*
 * DwmUpdateWindowList
 *
 * Enumerates all visible top-level windows and synchronizes the
 * internal window list.  New windows are added, destroyed/hidden
 * windows are removed.
 */
VOID
DwmUpdateWindowList(
    _Inout_ PDWM_CONTEXT pCtx)
{
    DWM_ENUM_CONTEXT EnumCtx;
    PDWM_WINDOW_ENTRY pEntry, pNext;

    EnterCriticalSection(&pCtx->csWindowList);

    /* Mark all existing entries as not-yet-seen */
    for (pEntry = pCtx->pWindowListHead; pEntry; pEntry = pEntry->pNext)
    {
        pEntry->Visible = FALSE;
    }

    /* Enumerate all top-level windows (returns in Z-order, top-to-bottom) */
    EnumCtx.pCtx = pCtx;
    EnumCtx.ZOrderCounter = 0;
    EnumWindows(DwmpEnumWindowsCallback, (LPARAM)&EnumCtx);

    /*
     * EnumWindows does NOT return the desktop window (Progman).  Explicitly
     * add it as the bottom-most composition layer so the real wallpaper
     * is captured from the desktop paint path instead of falling back to
     * a hard-coded solid color background.
     */
    {
        HWND hDesktop = GetDesktopWindow();
        if (hDesktop)
        {
            PDWM_WINDOW_ENTRY pDesktop = DwmpFindWindowEntry(pCtx, hDesktop);
            if (!pDesktop)
            {
                pDesktop = DwmpAddWindowEntry(pCtx, hDesktop);
            }
            if (pDesktop)
            {
                RECT rcDesktop;
                rcDesktop.left = 0;
                rcDesktop.top = 0;
                rcDesktop.right = (LONG)pCtx->ScreenWidth;
                rcDesktop.bottom = (LONG)pCtx->ScreenHeight;

                if (memcmp(&pDesktop->rcWindow, &rcDesktop, sizeof(RECT)) != 0)
                {
                    pDesktop->rcWindow = rcDesktop;
                    pDesktop->Surface.Dirty = TRUE;
                }
                if (!DwmSurfaceResize(&pDesktop->Surface,
                                      pCtx->ScreenWidth, pCtx->ScreenHeight))
                {
                    WARN("DwmUpdateWindowList: Desktop surface resize failed\n");
                }
                pDesktop->Visible = TRUE;
                /* Assign highest ZOrder number = drawn first (bottom-most) */
                pDesktop->ZOrder = EnumCtx.ZOrderCounter++;
            }
        }
    }

    /* Remove entries for windows that are no longer visible */
    pEntry = pCtx->pWindowListHead;
    while (pEntry)
    {
        pNext = pEntry->pNext;
        if (!pEntry->Visible)
        {
            /* Mark the old position dirty so it gets repainted */
            DwmMarkDirty(&pCtx->DirtyRegion, &pEntry->rcWindow);
            DwmpRemoveWindowEntry(pCtx, pEntry);
        }
        pEntry = pNext;
    }

    LeaveCriticalSection(&pCtx->csWindowList);

    if (DwmpShouldLogFrame(pCtx))
    {
        TRACE("DwmUpdateWindowList: frame=%I64u windows=%u dirty(full=%u any=%u)\n",
              pCtx->FramesComposed + 1,
              pCtx->WindowCount,
              pCtx->DirtyRegion.FullRecomposite,
              pCtx->DirtyRegion.HasDirty);
    }
}

/* ========================================================================
 * Window content capture
 *
 * Uses PrintWindow to capture a window's visual content into its
 * redirection surface.  PrintWindow sends WM_PRINT/WM_PRINTCLIENT
 * to the window, which renders into the provided DC.
 *
 * This is a Phase 1 workaround.  In Phase 2, windows will have
 * WS_EX_REDIRECTED set, causing all GDI output to go directly to
 * the shared D3DKMT redirection surface, eliminating the need for
 * capture.
 * ====================================================================== */

BOOL
DwmCaptureWindowContent(
    _Inout_ PDWM_CONTEXT pCtx,
    _Inout_ PDWM_WINDOW_ENTRY pEntry)
{
    HDC hDcScreen, hDcMem;
    HBITMAP hBitmap, hOldBitmap;
    BITMAPINFO bmi;
    CHAR ClassName[64];
    PCSTR CapturePath;
    CHAR Title[128];
    DWORD LastError;
    INT DibLines;
    LONG CaptureSample;
    BOOL UsedPrintWindow;
    UINT Width, Height;

    if (!pEntry->Surface.pPixels)
        return FALSE;

    Width = pEntry->Surface.Width;
    Height = pEntry->Surface.Height;

    ClassName[0] = '\0';
    Title[0] = '\0';
    GetClassNameA(pEntry->hWnd, ClassName, sizeof(ClassName));
    GetWindowTextA(pEntry->hWnd, Title, sizeof(Title));

    CapturePath = "PrintWindow";
    pEntry->CaptureFromScreen = FALSE;

    CaptureSample = InterlockedIncrement(&gDwmpCaptureWindowLogCount);
    if (DwmpShouldLogSerialSample(CaptureSample))
    {
        TRACE("DwmCaptureWindowContent[%ld]: frame=%I64u hwnd=%p class='%s' rect=(%ld,%ld)-(%ld,%ld) size=%ux%u dirty=%u title='%s'\n",
              CaptureSample,
              pCtx->FramesComposed + 1,
              pEntry->hWnd,
              ClassName,
              pEntry->rcWindow.left,
              pEntry->rcWindow.top,
              pEntry->rcWindow.right,
              pEntry->rcWindow.bottom,
              Width,
              Height,
              pEntry->Surface.Dirty,
              Title);
    }

    /* Create a memory DC and compatible bitmap */
    hDcScreen = GetDC(NULL);
    if (!hDcScreen)
        return FALSE;

    hDcMem = CreateCompatibleDC(hDcScreen);
    if (!hDcMem)
    {
        ReleaseDC(NULL, hDcScreen);
        return FALSE;
    }

    hBitmap = CreateCompatibleBitmap(hDcScreen, Width, Height);
    if (!hBitmap)
    {
        DeleteDC(hDcMem);
        ReleaseDC(NULL, hDcScreen);
        return FALSE;
    }

    hOldBitmap = (HBITMAP)SelectObject(hDcMem, hBitmap);

    /*
     * PrintWindow captures the window content into our memory DC.
     * PW_CLIENTONLY = 0 captures the full window including non-client area.
     *
     * If PrintWindow fails (e.g. the window doesn't handle WM_PRINT),
     * fall back to BitBlt from the screen.
     */
    /*
     * OpenGL windows: skip PrintWindow entirely.  PrintWindow sends
     * WM_PRINT which triggers GDI rendering — it cannot see OpenGL
     * content.  Use BitBlt from the screen position instead, which
     * captures whatever the GPU actually rendered there.
     *
     * Non-GL windows: use PrintWindow (captures correct content even
     * when partially occluded), with BitBlt fallback if it fails.
     */
    if (pEntry->IsOpenGLWindow)
    {
        UsedPrintWindow = FALSE;
        CapturePath = "BitBlt";
        pEntry->CaptureFromScreen = TRUE;
        if (DwmpShouldLogSerialSample(CaptureSample))
        {
            TRACE("DwmCaptureWindowContent[%ld]: hwnd=%p OpenGL=1 using BitBlt from screen\n",
                  CaptureSample,
                  pEntry->hWnd);
        }
    }
    else
    {
        SetLastError(ERROR_SUCCESS);
        UsedPrintWindow = PrintWindow(pEntry->hWnd, hDcMem, 0);
        LastError = GetLastError();
        if (DwmpShouldLogSerialSample(CaptureSample) || !UsedPrintWindow)
        {
            TRACE("DwmCaptureWindowContent[%ld]: hwnd=%p PrintWindow=%d gle=%lu\n",
                  CaptureSample,
                  pEntry->hWnd,
                  UsedPrintWindow,
                  LastError);
        }
    }
    if (!UsedPrintWindow)
    {
        if (pEntry->hWnd == GetDesktopWindow())
        {
            /*
             * Desktop window: PrintWindow fails for this special window,
             * and BitBlt from screen would capture ALL visible windows
             * (causing every window to appear twice in the composition).
             * Paint the desktop background directly instead.
             */
            CapturePath = "PaintDesktop";
            PaintDesktop(hDcMem);
        }
        else
        {
            /* Fallback: BitBlt from the screen at the window's position */
            CapturePath = "BitBlt";
            pEntry->CaptureFromScreen = TRUE;
            BitBlt(hDcMem, 0, 0, Width, Height,
                   hDcScreen,
                   pEntry->rcWindow.left, pEntry->rcWindow.top,
                   SRCCOPY);
        }
    }

    if (!UsedPrintWindow || DwmpShouldLogFrame(pCtx))
    {
        Title[0] = '\0';
        GetWindowTextA(pEntry->hWnd, Title, sizeof(Title));
        TRACE("DwmCaptureWindowContent: frame=%I64u hwnd=%p size=%ux%u path=%s title='%s'\n",
              pCtx->FramesComposed + 1,
              pEntry->hWnd,
              Width,
              Height,
              CapturePath,
              Title);
    }

    /*
     * Extract the pixel data from the bitmap into our surface buffer.
     * We use GetDIBits with a 32-bpp BITMAPINFO to get B8G8R8X8 data
     * (matches D3DDDIFMT_X8R8G8B8).
     */
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = Width;
    /* Negative height = top-down DIB (no vertical flip needed) */
    bmi.bmiHeader.biHeight = -(INT)Height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    DibLines = GetDIBits(hDcMem, hBitmap, 0, Height, pEntry->Surface.pPixels,
                         &bmi, DIB_RGB_COLORS);
    if (DibLines == 0)
    {
        ERR("DwmCaptureWindowContent[%ld]: hwnd=%p GetDIBits failed gle=%lu\n",
            CaptureSample,
            pEntry->hWnd,
            GetLastError());
        SelectObject(hDcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hDcMem);
        ReleaseDC(NULL, hDcScreen);
        return FALSE;
    }

    /* Cleanup */
    SelectObject(hDcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hDcMem);
    ReleaseDC(NULL, hDcScreen);

    pEntry->Surface.Dirty = FALSE;
    return TRUE;
}

/* ========================================================================
 * Phase 2: D3DKMT composition allocation
 *
 * Creates a real WDDM allocation for the composition buffer so that
 * D3DKMTPresent has a genuine source allocation.  The allocation is
 * a CPU-visible system allocation (dxgkrnl backs it with NonPagedPool);
 * we lock it to obtain a user-mode VA and composite directly into it.
 *
 * This is what makes the kernel present/flip real: dwm composes a
 * COMPLETE frame into this allocation, then D3DKMTPresent(hSource=
 * hAllocation) routes through dxgkrnl -> the display-only miniport's
 * DxgkDdiPresentDisplayOnly, which blts the complete frame to the
 * scanout.  No partial frame is ever shown -> tear-free.
 *
 * The per-allocation private driver data is a {Width, Height, Bpp}
 * triple; dxgkrnl's DxgkCreateAllocation reads it to size the system
 * allocation (Width * Height * Bpp/8 bytes).
 * ====================================================================== */

typedef struct _DWM_ALLOC_PRIVATE
{
    UINT Width;
    UINT Height;
    UINT BitsPerPixel;
} DWM_ALLOC_PRIVATE;

static BOOL
DwmCreateCompositionAllocation(
    _Inout_ PDWM_CONTEXT pCtx)
{
    D3DKMT_CREATEALLOCATION CreateAlloc;
    D3DDDI_ALLOCATIONINFO   AllocInfo;
    DWM_ALLOC_PRIVATE       Priv;
    D3DKMT_LOCK             Lock;
    NTSTATUS                Status;

    if (!pCtx->D3dkmt.Initialized || pCtx->D3dkmt.hDevice == 0)
        return FALSE;

    /* Describe the allocation: a primary-sized 32-bpp surface. */
    Priv.Width        = pCtx->CompBuffer.Width;
    Priv.Height       = pCtx->CompBuffer.Height;
    Priv.BitsPerPixel = 32;

    ZeroMemory(&AllocInfo, sizeof(AllocInfo));
    AllocInfo.pPrivateDriverData    = &Priv;
    AllocInfo.PrivateDriverDataSize = sizeof(Priv);
    AllocInfo.VidPnSourceId         = pCtx->D3dkmt.VidPnSourceId;
    AllocInfo.Flags.Primary         = 1;

    ZeroMemory(&CreateAlloc, sizeof(CreateAlloc));
    CreateAlloc.hDevice        = pCtx->D3dkmt.hDevice;
    CreateAlloc.NumAllocations = 1;
    CreateAlloc.pAllocationInfo = &AllocInfo;

    Status = D3DKMTCreateAllocation(&CreateAlloc);
    if (Status != STATUS_SUCCESS)
    {
        ERR("DwmCreateCompositionAllocation: D3DKMTCreateAllocation failed 0x%08lX\n",
            Status);
        return FALSE;
    }

    pCtx->CompBuffer.hAllocation = AllocInfo.hAllocation;
    pCtx->CompBuffer.hResource   = CreateAlloc.hResource;

    TRACE("DwmCreateCompositionAllocation: hAllocation=0x%X hResource=0x%X (%ux%u)\n",
          AllocInfo.hAllocation,
          CreateAlloc.hResource,
          Priv.Width,
          Priv.Height);

    /*
     * Lock the allocation to get a user-mode VA, then composite directly
     * into it.  The same physical pages are visible to dxgkrnl in kernel
     * mode via the allocation's CpuAddress, so the present path reads the
     * exact frame we composed.
     */
    ZeroMemory(&Lock, sizeof(Lock));
    Lock.hDevice     = pCtx->D3dkmt.hDevice;
    Lock.hAllocation = pCtx->CompBuffer.hAllocation;
    Lock.Flags.Value = 0;

    Status = D3DKMTLock(&Lock);
    if (Status != STATUS_SUCCESS || Lock.pData == NULL)
    {
        D3DKMT_DESTROYALLOCATION DestroyAlloc;
        D3DKMT_HANDLE hAlloc = pCtx->CompBuffer.hAllocation;

        ERR("DwmCreateCompositionAllocation: D3DKMTLock failed 0x%08lX pData=%p\n",
            Status, Lock.pData);

        /*
         * Without a locked VA we cannot composite into the allocation, so the
         * kernel present would scan out stale memory.  Drop the allocation and
         * fall back to the heap buffer + GDI present path (DwmPresentFrame
         * keys the kernel flip on hAllocation != 0).
         */
        ZeroMemory(&DestroyAlloc, sizeof(DestroyAlloc));
        DestroyAlloc.hDevice          = pCtx->D3dkmt.hDevice;
        DestroyAlloc.hResource        = pCtx->CompBuffer.hResource;
        DestroyAlloc.phAllocationList = &hAlloc;
        DestroyAlloc.AllocationCount  = 1;
        D3DKMTDestroyAllocation(&DestroyAlloc);

        pCtx->CompBuffer.hAllocation = 0;
        pCtx->CompBuffer.hResource   = 0;
        return TRUE;
    }

    /*
     * Replace the heap composition buffer with the locked allocation VA.
     * Free the heap buffer we allocated earlier; the allocation memory
     * is the composition target from now on.
     */
    if (pCtx->CompBuffer.pPixels != NULL &&
        pCtx->CompBuffer.pPixels != Lock.pData)
    {
        HeapFree(GetProcessHeap(), 0, pCtx->CompBuffer.pPixels);
    }

    pCtx->CompBuffer.pPixels = Lock.pData;
    pCtx->CompBuffer.AllocationIsLockedVa = TRUE;

    TRACE("DwmCreateCompositionAllocation: locked allocation VA=%p — compositing "
          "directly into the WDDM allocation\n", Lock.pData);

    return TRUE;
}

/* ========================================================================
 * Compositor initialization / shutdown
 * ====================================================================== */

BOOL
DwmCompositorInit(
    _Inout_ PDWM_CONTEXT pCtx)
{
    TRACE("DwmCompositorInit: Allocating %ux%u composition buffer\n",
          pCtx->ScreenWidth, pCtx->ScreenHeight);

    /* Allocate the full-screen composition buffer */
    ZeroMemory(&pCtx->CompBuffer, sizeof(pCtx->CompBuffer));
    pCtx->CompBuffer.Width = pCtx->ScreenWidth;
    pCtx->CompBuffer.Height = pCtx->ScreenHeight;
    pCtx->CompBuffer.BytesPerPixel = 4;
    if (pCtx->ScreenWidth > ((UINT)-1 / pCtx->CompBuffer.BytesPerPixel))
    {
        ERR("DwmCompositorInit: Invalid composition width %u\n",
            pCtx->ScreenWidth);
        return FALSE;
    }

    pCtx->CompBuffer.Stride = pCtx->ScreenWidth * 4;
    if (pCtx->ScreenHeight > ((SIZE_T)-1 / pCtx->CompBuffer.Stride))
    {
        ERR("DwmCompositorInit: Invalid composition height %u\n",
            pCtx->ScreenHeight);
        ZeroMemory(&pCtx->CompBuffer, sizeof(pCtx->CompBuffer));
        return FALSE;
    }

    pCtx->CompBuffer.AllocationSize = (SIZE_T)pCtx->CompBuffer.Stride *
                                      pCtx->ScreenHeight;

    pCtx->CompBuffer.pPixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                         pCtx->CompBuffer.AllocationSize);
    if (!pCtx->CompBuffer.pPixels)
    {
        ERR("DwmCompositorInit: Failed to allocate composition buffer "
            "(%Iu bytes)\n", pCtx->CompBuffer.AllocationSize);
        return FALSE;
    }

    /*
     * Phase 2: Allocate the composition buffer as a real D3DKMT allocation
     * so D3DKMTPresent has a genuine source allocation and the kernel
     * present/flip path is exercised (tear-free complete-frame blt through
     * the display-only miniport).  On success this swaps pPixels to the
     * locked allocation VA so compositing writes straight into it.
     *
     * If allocation/lock fails we keep the heap buffer and DwmPresentFrame
     * falls back to the GDI BitBlt path (Phase 1 behaviour) so the desktop
     * still updates.
     */
    if (!DwmCreateCompositionAllocation(pCtx))
    {
        WARN("DwmCompositorInit: D3DKMT composition allocation unavailable; "
             "using heap buffer + GDI present fallback\n");
    }

    /* Initialize dirty region to force a full first compose */
    ZeroMemory(&pCtx->DirtyRegion, sizeof(pCtx->DirtyRegion));
    pCtx->DirtyRegion.FullRecomposite = TRUE;

    /* Initialize window list */
    pCtx->pWindowListHead = NULL;
    pCtx->WindowCount = 0;

    pCtx->FramesComposed = 0;
    pCtx->FramesPresented = 0;

    TRACE("DwmCompositorInit: Composition buffer at %p (%ux%u, %Iu bytes)\n",
          pCtx->CompBuffer.pPixels,
          pCtx->CompBuffer.Width,
          pCtx->CompBuffer.Height,
          pCtx->CompBuffer.AllocationSize);

    return TRUE;
}

VOID
DwmCompositorShutdown(
    _Inout_ PDWM_CONTEXT pCtx)
{
    PDWM_WINDOW_ENTRY pEntry, pNext;

    TRACE("DwmCompositorShutdown: Cleaning up\n");

    /* Free all window entries and their surfaces */
    EnterCriticalSection(&pCtx->csWindowList);

    pEntry = pCtx->pWindowListHead;
    while (pEntry)
    {
        pNext = pEntry->pNext;
        DwmSurfaceFree(&pEntry->Surface);
        HeapFree(GetProcessHeap(), 0, pEntry);
        pEntry = pNext;
    }
    pCtx->pWindowListHead = NULL;
    pCtx->WindowCount = 0;

    LeaveCriticalSection(&pCtx->csWindowList);

    /*
     * Phase 2: tear down the D3DKMT composition allocation.  If pPixels is
     * the locked allocation VA it must NOT be HeapFree'd — unlock and
     * destroy the allocation instead.
     */
    if (pCtx->CompBuffer.AllocationIsLockedVa)
    {
        if (pCtx->CompBuffer.hAllocation != 0 && pCtx->D3dkmt.hDevice != 0)
        {
            D3DKMT_UNLOCK Unlock;
            D3DKMT_HANDLE hAlloc = pCtx->CompBuffer.hAllocation;

            ZeroMemory(&Unlock, sizeof(Unlock));
            Unlock.hDevice        = pCtx->D3dkmt.hDevice;
            Unlock.NumAllocations = 1;
            Unlock.phAllocations  = &hAlloc;
            D3DKMTUnlock(&Unlock);
        }
        pCtx->CompBuffer.pPixels = NULL;
        pCtx->CompBuffer.AllocationIsLockedVa = FALSE;
    }
    else if (pCtx->CompBuffer.pPixels)
    {
        HeapFree(GetProcessHeap(), 0, pCtx->CompBuffer.pPixels);
        pCtx->CompBuffer.pPixels = NULL;
    }

    if (pCtx->CompBuffer.hAllocation != 0 && pCtx->D3dkmt.hDevice != 0)
    {
        D3DKMT_DESTROYALLOCATION DestroyAlloc;
        D3DKMT_HANDLE hAlloc = pCtx->CompBuffer.hAllocation;

        ZeroMemory(&DestroyAlloc, sizeof(DestroyAlloc));
        DestroyAlloc.hDevice        = pCtx->D3dkmt.hDevice;
        DestroyAlloc.hResource      = pCtx->CompBuffer.hResource;
        DestroyAlloc.phAllocationList = &hAlloc;
        DestroyAlloc.AllocationCount  = 1;
        D3DKMTDestroyAllocation(&DestroyAlloc);

        pCtx->CompBuffer.hAllocation = 0;
        pCtx->CompBuffer.hResource   = 0;
    }
}

/* ========================================================================
 * D3DKMT Present
 *
 * Pushes the composed frame to the display through dxgkrnl.
 * ====================================================================== */

BOOL
DwmPresentFrame(
    _Inout_ PDWM_CONTEXT pCtx)
{
    D3DKMT_PRESENT Present;
    NTSTATUS Status;
    UINT64 FrameId = pCtx->FramesPresented + 1;
    BOOL GdiPresented = FALSE;
    BOOL KernelPresented = FALSE;

    if (!pCtx->D3dkmt.Initialized)
        return FALSE;

    if (!pCtx->CompBuffer.pPixels)
        return FALSE;

    ZeroMemory(&Present, sizeof(Present));
    Status = STATUS_NOT_IMPLEMENTED;

    /*
     * Phase 2 (tear-free flip): present the composition buffer as a real
     * D3DKMT allocation through the kernel.  dwm composed a COMPLETE frame
     * into this allocation; D3DKMTPresent routes it through dxgkrnl to the
     * display-only miniport's DxgkDdiPresentDisplayOnly, which blts the
     * whole frame to the scanout in one shot -> no partial frame is ever
     * shown.  This is the path the desktop tearing fix needs.
     */
    if (pCtx->CompBuffer.hAllocation != 0)
    {
        Present.hDevice = pCtx->D3dkmt.hDevice;
        Present.hWindow = NULL;
        Present.VidPnSourceId = pCtx->D3dkmt.VidPnSourceId;
        Present.hSource = pCtx->CompBuffer.hAllocation;
        Present.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
        Present.Flags.Blt = 1;
        Present.Flags.SrcRectValid = 1;
        Present.Flags.DstRectValid = 1;
        Present.Flags.RestrictVidPnSource = 1;

        /* Set the source and destination rects to full screen */
        Present.SrcRect.left = 0;
        Present.SrcRect.top = 0;
        Present.SrcRect.right = pCtx->CompBuffer.Width;
        Present.SrcRect.bottom = pCtx->CompBuffer.Height;
        Present.DstRect = Present.SrcRect;

        Status = D3DKMTPresent(&Present);
        if (Status == STATUS_SUCCESS)
        {
            KernelPresented = TRUE;
        }
        else
        {
            /* Fall back to GDI below so the desktop still updates. */
            TRACE("DwmPresentFrame: D3DKMTPresent returned 0x%08lX (falling "
                  "back to GDI)\n", Status);
        }
    }
    else if (DwmpShouldLogFrame(pCtx))
    {
        TRACE("DwmPresentFrame: frame=%I64u skipping D3DKMTPresent; no D3DKMT composition allocation yet\n",
              FrameId);
    }

    /*
     * GDI fallback path.  Only used when the kernel present did not succeed
     * (no allocation, or D3DKMTPresent failed).  This keeps the desktop
     * visible on adapters/configs where the kernel flip is unavailable, and
     * preserves the original Phase-1 behaviour.  When the kernel present
     * works, we deliberately skip this -- the flip already put the complete
     * frame on screen, and a second GDI blit would defeat tear-freedom.
     *
     * Suppress mouse safety (cursor hide/show) during the full-screen blit
     * to avoid cursor flicker.  Escape 0x44574D01 = DWM_ESCAPE_SUPPRESS_CURSOR.
     */
    if (!KernelPresented)
    {
        HDC hDcScreen;
        HDC hDcMem;
        HBITMAP hBitmap, hOldBitmap;
        BITMAPINFO bmi;
        LONG Suppress;
        int LinesSet;

        hDcScreen = GetDC(NULL);
        if (hDcScreen)
        {
            hDcMem = CreateCompatibleDC(hDcScreen);
            if (hDcMem)
            {
                hBitmap = CreateCompatibleBitmap(hDcScreen,
                                                  pCtx->CompBuffer.Width,
                                                  pCtx->CompBuffer.Height);
                if (hBitmap)
                {
                    hOldBitmap = (HBITMAP)SelectObject(hDcMem, hBitmap);

                    /* Upload our pixel buffer to the bitmap */
                    ZeroMemory(&bmi, sizeof(bmi));
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = pCtx->CompBuffer.Width;
                    bmi.bmiHeader.biHeight = -(INT)pCtx->CompBuffer.Height;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;

                    LinesSet = SetDIBits(hDcMem, hBitmap, 0,
                                          pCtx->CompBuffer.Height,
                                          pCtx->CompBuffer.pPixels,
                                          &bmi, DIB_RGB_COLORS);
                    if (LinesSet == (int)pCtx->CompBuffer.Height)
                    {
                        /* Suppress cursor hide/show during present blit */
                        Suppress = 1;
                        ExtEscape(hDcScreen, 0x44574D01, sizeof(Suppress),
                                  (LPCSTR)&Suppress, 0, NULL);

                        GdiPresented = BitBlt(hDcScreen, 0, 0,
                                              pCtx->CompBuffer.Width,
                                              pCtx->CompBuffer.Height,
                                              hDcMem, 0, 0, SRCCOPY);

                        /* Re-enable cursor safety */
                        Suppress = 0;
                        ExtEscape(hDcScreen, 0x44574D01, sizeof(Suppress),
                                  (LPCSTR)&Suppress, 0, NULL);
                    }
                    else
                    {
                        WARN("DwmPresentFrame: SetDIBits copied %d/%u lines\n",
                             LinesSet, pCtx->CompBuffer.Height);
                    }

                    SelectObject(hDcMem, hOldBitmap);
                    DeleteObject(hBitmap);
                }
                DeleteDC(hDcMem);
            }
            ReleaseDC(NULL, hDcScreen);
        }
    }

    if (DwmpShouldLogFrame(pCtx))
    {
        TRACE("DwmPresentFrame: frame=%I64u windows=%u comp=%ux%u srcAlloc=0x%X "
              "path=%s status=0x%08lX\n",
              FrameId,
              pCtx->WindowCount,
              pCtx->CompBuffer.Width,
              pCtx->CompBuffer.Height,
              Present.hSource,
              KernelPresented ? "D3DKMTPresent(flip)" : (GdiPresented ? "GDI" : "none"),
              Status);
    }

    if (!GdiPresented && !KernelPresented)
    {
        WARN("DwmPresentFrame: frame=%I64u was not presented\n", FrameId);
        return FALSE;
    }

    pCtx->FramesPresented++;
    return TRUE;
}

/* ========================================================================
 * Single-frame composition
 *
 * Walks the window list in bottom-to-top Z-order and blits each
 * window's redirection surface into the composition buffer.
 * ====================================================================== */

VOID
DwmComposeSingleFrame(
    _Inout_ PDWM_CONTEXT pCtx)
{
    PDWM_WINDOW_ENTRY pEntry;
    UINT TotalWindows;
    UINT MaxZOrder = 0;
    INT Z;
    DWORD tEntry, tBeforePresent, tAfterPresent; /* TEMP fps instrumentation */

    tEntry = GetTickCount();

    EnterCriticalSection(&pCtx->csWindowList);

    TotalWindows = pCtx->WindowCount;
    if (TotalWindows == 0)
    {
        if (DwmpShouldLogFrame(pCtx))
        {
            TRACE("DwmComposeSingleFrame: frame=%I64u has no visible windows\n",
                  pCtx->FramesComposed + 1);
        }
        LeaveCriticalSection(&pCtx->csWindowList);
        return;
    }

    /*
     * Find the maximum Z-order value.  EnumWindows gives top-to-bottom,
     * so ZOrder=0 is the topmost window.  We composite bottom-to-top
     * (painter's algorithm), so we iterate from MaxZOrder down to 0.
     */
    for (pEntry = pCtx->pWindowListHead; pEntry; pEntry = pEntry->pNext)
    {
        if (pEntry->ZOrder > MaxZOrder)
            MaxZOrder = pEntry->ZOrder;
    }

    /*
     * Clear the composition buffer to black.  The desktop window is now
     * captured as the bottom-most layer (via GetDesktopWindow() in
     * DwmUpdateWindowList), so the real wallpaper is composited from the
     * desktop paint path.  The zero-fill is just a safety net for areas
     * not covered by any window.
     */
    if (pCtx->DirtyRegion.FullRecomposite || pCtx->DirtyRegion.HasDirty)
    {
        ZeroMemory(pCtx->CompBuffer.pPixels, pCtx->CompBuffer.AllocationSize);
    }

    /*
     * Composite bottom-to-top (painter's algorithm).
     * Z-order 0 = topmost, MaxZOrder = bottommost.
     * We draw from MaxZOrder down to 0 so topmost windows overdraw.
     */
    for (Z = (INT)MaxZOrder; Z >= 0; Z--)
    {
        for (pEntry = pCtx->pWindowListHead; pEntry; pEntry = pEntry->pNext)
        {
            if ((INT)pEntry->ZOrder != Z)
                continue;
            if (!pEntry->Visible)
                continue;
            if (!pEntry->Surface.pPixels)
                continue;

            /* Any surface sourced from live screen pixels must be refreshed
             * every frame. Otherwise, occluding windows get baked into a
             * lower layer and later show up as duplicates/ghosting. */
            if (pEntry->IsOpenGLWindow || pEntry->CaptureFromScreen)
                pEntry->Surface.Dirty = TRUE;

            /* Capture the window content if the surface is dirty */
            if (pEntry->Surface.Dirty)
            {
                if (!DwmCaptureWindowContent(pCtx, pEntry))
                    continue;
            }

            /* Blit the window surface to the composition buffer */
            DwmBlitSurface(&pCtx->CompBuffer,
                           pEntry->rcWindow.left,
                           pEntry->rcWindow.top,
                           &pEntry->Surface,
                           NULL);
        }
    }

    LeaveCriticalSection(&pCtx->csWindowList);

    /* Present the composed frame */
    tBeforePresent = GetTickCount();
    DwmPresentFrame(pCtx);
    tAfterPresent = GetTickCount();

    /* Clear dirty state for next cycle */
    DwmClearDirty(&pCtx->DirtyRegion);
    pCtx->DirtyRegion.FullRecomposite = FALSE;

    pCtx->FramesComposed++;

    if (DwmpShouldLogFrame(pCtx))
    {
        /* TEMP fps instrumentation: capture(compose) vs present breakdown. */
        TRACE("DwmComposeSingleFrame: frame=%I64u composed windows=%u presented=%I64u "
              "captureMs=%lu presentMs=%lu totalMs=%lu\n",
              pCtx->FramesComposed,
              TotalWindows,
              pCtx->FramesPresented,
              (unsigned long)(tBeforePresent - tEntry),
              (unsigned long)(tAfterPresent - tBeforePresent),
              (unsigned long)(tAfterPresent - tEntry));
    }
}

/* ========================================================================
 * Main composition loop
 *
 * Runs until ShutdownRequested is set.  Each iteration:
 *   1. Updates the window list (enumerate top-level windows)
 *   2. Composites all windows into the back buffer
 *   3. Presents the back buffer to the display
 *   4. Waits for the next composition tick (~16ms for 60Hz)
 * ====================================================================== */

VOID
DwmCompositorRun(
    _Inout_ PDWM_CONTEXT pCtx)
{
    DWORD WaitResult;

    TRACE("DwmCompositorRun: Entering composition loop (%ux%u, %ums interval)\n",
          pCtx->ScreenWidth, pCtx->ScreenHeight, DWM_COMPOSE_INTERVAL_MS);

    while (!pCtx->ShutdownRequested)
    {
        /*
         * Step 1: Update the window list.
         * This adds newly created windows, removes destroyed ones,
         * and updates positions of moved/resized windows.
         */
        DwmUpdateWindowList(pCtx);

        /*
         * Step 2: Compose and present a single frame.
         * Walks the window list bottom-to-top, blits each surface into
         * the composition buffer, then presents to the display.
         */
        DwmComposeSingleFrame(pCtx);

        /*
         * Step 3: Wait for the next composition tick.
         * We use WaitForSingleObject on the stop event with a timeout
         * of DWM_COMPOSE_INTERVAL_MS.  This serves double duty:
         *   - Provides the composition timing (~60 Hz)
         *   - Wakes up immediately if the service is being stopped
         *
         * Phase 2 will replace this with VSync signaling via
         * D3DKMTWaitForVerticalBlankEvent for proper frame pacing.
         */
        WaitResult = WaitForSingleObject(pCtx->hStopEvent,
                                         DWM_COMPOSE_INTERVAL_MS);
        if (WaitResult == WAIT_OBJECT_0)
        {
            /* Stop event signaled -- exit the loop */
            TRACE("DwmCompositorRun: Stop event signaled\n");
            break;
        }
        /* WAIT_TIMEOUT is the normal case -- continue composing */
    }

    TRACE("DwmCompositorRun: Exited composition loop "
          "(composed=%I64u, presented=%I64u)\n",
          pCtx->FramesComposed, pCtx->FramesPresented);
}
