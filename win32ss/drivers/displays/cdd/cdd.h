/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM canonical GDI display driver - private declarations.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * cdd is the vendor-independent GDI display driver that sits on top of the
 * WDDM stack (dxgkrnl). It exposes the legacy Drv* GDI DDI that win32k/GDI
 * still expects, hands GDI a plain bitmap surface to draw into (the GDI engine
 * software rasterizer does all the painting), and presents the result through
 * the WDDM kernel. This is a cleanroom implementation: every internal symbol
 * uses the "Rcdd" prefix and no Microsoft cdd internals are reused. The module
 * is named cdd.dll only because that is the free-form string an INF places in
 * the registry's InstalledDisplayDrivers value.
 *
 * The implementation is derived from win32ss/drivers/displays/framebuf, with
 * the surface/present/escape seams factored out so the WDDM (D3DKMT) present
 * path can be slotted in additively.
 */

#ifndef _CDD_PCH_
#define _CDD_PCH_

#include <ntddk.h>
#include <stdarg.h>
#include <windef.h>
#include <wingdi.h>
#include <winddi.h>
#include <winioctl.h>
#include <ntddvdeo.h>

/* DWM composition contract (CDD_ESCAPE_*, IOCTL_VIDEO_DXGK_*): shared with
 * win32k and dxgkrnl. */
#include <reactos/dwmframe.h>

#define RCDD_PRESENT_SLOT_COUNT 3

typedef enum _RCDD_PRESENT_SLOT_STATE
{
   RcddPresentSlotFree,
   RcddPresentSlotCapturing,
   RcddPresentSlotQueued,
   RcddPresentSlotActive
} RCDD_PRESENT_SLOT_STATE;

typedef struct _RCDD_PRESENT_SLOT
{
   PVOID Buffer;
   SIZE_T BufferSize;
   RECTL Rects[DXGK_PRESENT_DIRTY_MAX_RECTS];
   ULONG RectCount;
   RCDD_PRESENT_SLOT_STATE State;
} RCDD_PRESENT_SLOT, *PRCDD_PRESENT_SLOT;

typedef struct _RCDD_PDEV
{
   HANDLE hDriver;             /* Handle to \Device\VideoN served by dxgkrnl */
   HDEV hDevEng;               /* GDI device handle (from DrvCompletePDEV)   */
   HSURF hSurfEng;             /* The primary surface handed to GDI          */
   ULONG ModeIndex;
   ULONG ScreenWidth;
   ULONG ScreenHeight;
   ULONG ScreenDelta;          /* Stride in bytes                            */
   BYTE BitsPerPixel;
   ULONG RedMask;
   ULONG GreenMask;
   ULONG BlueMask;
   BYTE PaletteShift;
   PVOID ScreenPtr;            /* Mapped WDDM scan-out (dxgkrnl shadow FB)    */
   HPALETTE DefaultPalette;
   PALETTEENTRY *PaletteEntries;

   /* Hardware pointer (declined by the software GPU -> GDI draws a SW cursor) */
   VIDEO_POINTER_CAPABILITIES HwPointerCapabilities;
   PVIDEO_POINTER_ATTRIBUTES HwPointerAttributes;
   ULONG HwPointerAttributesSize;
   POINTL HwPointerHotSpot;
   BOOL HwPointerSupported;
   BOOL HwPointerShapeValid;
   BOOL HwPointerVisible;
   BOOL SoftwarePointerActive;
   BOOL PointerPositionValid;
   LONG PointerX;
   LONG PointerY;

   /* DWM cursor state (driven by DrvEscape, see escape.c) */
   BOOL CursorSuppressed;      /* Compositor owns the cursor                  */

   /* Dirty-rect notification state (see present.c) */
   ULONG PendingRectCount;
   RECTL PendingRects[DXGK_PRESENT_DIRTY_MAX_RECTS];
   ULONG DrawSeq;              /* Bumped by every draw DDI entry              */
   ULONG SentSeq;              /* DrawSeq of the last notification sent       */
   RECTL SentRect;             /* Rect of the last notification sent          */

   /* Native CDD completes ordinary SynchronizeSurface requests through a
    * worker. Each queued entry owns an immutable copy of its dirty pixels;
    * the primary can therefore be painted again while PresentDisplayOnly is
    * in progress. DSS_RESERVED requests retain the synchronous command path. */
   HSEMAPHORE PresentLock;
   PEVENT PresentWakeEvent;
   PEVENT PresentExitEvent;
   PEVENT PresentDrainEvent;
   HANDLE PresentThread;
   BOOL PresentWorkerActive;
   BOOL PresentWorkerStop;
   ULONG PresentPendingCount;
   ULONG PresentQueueHighWatermark;
   ULONG PresentQueuedCount;
   ULONG PresentCompletedCount;
   ULONG PresentFailedCount;
   ULONG PresentRejectedCount;
   ULONG PresentSynchronousCount;
   RCDD_PRESENT_SLOT PresentSlots[RCDD_PRESENT_SLOT_COUNT];
} RCDD_PDEV, *PRCDD_PDEV;

#define DEVICE_NAME L"cdd"
#define ALLOC_TAG ' DDC'        /* Pool tag displays as "CDD "                */

/* ---- enable.c : driver entry + PDEV lifetime ---------------------------- */

BOOL APIENTRY
RcddEnableDriver(
   ULONG iEngineVersion,
   ULONG cj,
   PDRVENABLEDATA pded);

DHPDEV APIENTRY
RcddEnablePDEV(
   IN DEVMODEW *pdm,
   IN LPWSTR pwszLogAddress,
   IN ULONG cPat,
   OUT HSURF *phsurfPatterns,
   IN ULONG cjCaps,
   OUT ULONG *pdevcaps,
   IN ULONG cjDevInfo,
   OUT DEVINFO *pdi,
   IN HDEV hdev,
   IN LPWSTR pwszDeviceName,
   IN HANDLE hDriver);

VOID APIENTRY
RcddCompletePDEV(
   IN DHPDEV dhpdev,
   IN HDEV hdev);

VOID APIENTRY
RcddDisablePDEV(
   IN DHPDEV dhpdev);

/* ---- surface.c : primary surface lifetime ------------------------------- */

HSURF APIENTRY
RcddEnableSurface(
   IN DHPDEV dhpdev);

VOID APIENTRY
RcddDisableSurface(
   IN DHPDEV dhpdev);

/* ---- screen.c : modes + assert-mode ------------------------------------- */

BOOL APIENTRY
RcddAssertMode(
   IN DHPDEV dhpdev,
   IN BOOL bEnable);

ULONG APIENTRY
RcddGetModes(
   IN HANDLE hDriver,
   IN ULONG cjSize,
   OUT DEVMODEW *pdm);

BOOL
RcddInitScreenInfo(
   PRCDD_PDEV ppdev,
   LPDEVMODEW pDevMode,
   PGDIINFO pGdiInfo,
   PDEVINFO pDevInfo);

/* ---- palette.c ---------------------------------------------------------- */

BOOL APIENTRY
RcddSetPalette(
   IN DHPDEV dhpdev,
   IN PALOBJ *ppalo,
   IN FLONG fl,
   IN ULONG iStart,
   IN ULONG cColors);

BOOL
RcddInitDefaultPalette(
   PRCDD_PDEV ppdev,
   PDEVINFO pDevInfo);

BOOL APIENTRY
RcddSetPaletteEntries(
   IN DHPDEV dhpdev,
   IN PPALETTEENTRY ppalent,
   IN ULONG iStart,
   IN ULONG cColors);

/* ---- pointer.c ---------------------------------------------------------- */

ULONG APIENTRY
RcddSetPointerShape(
   IN SURFOBJ *pso,
   IN SURFOBJ *psoMask,
   IN SURFOBJ *psoColor,
   IN XLATEOBJ *pxlo,
   IN LONG xHot,
   IN LONG yHot,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl,
   IN FLONG fl);

VOID APIENTRY
RcddMovePointer(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl);

BOOL
RcddInitHardwarePointer(
   PRCDD_PDEV ppdev);

VOID
RcddDisableHardwarePointer(
   PRCDD_PDEV ppdev);

VOID
RcddSetCursorSuppressed(
   PRCDD_PDEV ppdev,
   SURFOBJ *pso,
   BOOL Suppressed);

/* ---- present.c : draw delegation + present seam -------------------------- */

BOOL APIENTRY
RcddBitBlt(
   IN SURFOBJ *psoTrg,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclTrg,
   IN POINTL *pptlSrc,
   IN POINTL *pptlMask,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrush,
   IN ROP4 rop4);

BOOL APIENTRY
RcddCopyBits(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDest,
   IN POINTL *pptlSrc);

VOID APIENTRY
RcddSynchronizeSurface(
   IN SURFOBJ *pso,
   IN RECTL *prcl,
   IN FLONG fl);

BOOL
RcddStartPresentWorker(
   IN PRCDD_PDEV ppdev);

VOID
RcddStopPresentWorker(
   IN PRCDD_PDEV ppdev);

VOID
RcddQueryPresentWorkerStats(
   IN PRCDD_PDEV ppdev,
   IN OUT PDXGK_PRESENT_STATS Stats);

LONG APIENTRY
RcddSynchronizeRedirectionBitmaps(
   IN DHPDEV dhpdev,
   OUT UINT64 *puiFenceID);

BOOL APIENTRY
RcddAccumulateD3DDirtyRect(
   IN SURFOBJ *psoSurf,
   IN CDDDXGK_REDIRBITMAPPRESENTINFO *pDirty);

VOID APIENTRY
RcddLockDisplayArea(
   IN DHPDEV dhpdev,
   IN OPTIONAL RECTL *prcl);

VOID APIENTRY
RcddUnlockDisplayArea(
   IN DHPDEV dhpdev,
   IN OPTIONAL RECTL *prcl);

BOOL APIENTRY
RcddTextOut(
   IN SURFOBJ *pso,
   IN STROBJ *pstro,
   IN FONTOBJ *pfo,
   IN CLIPOBJ *pco,
   IN RECTL *prclExtra,
   IN RECTL *prclOpaque,
   IN BRUSHOBJ *pboFore,
   IN BRUSHOBJ *pboOpaque,
   IN POINTL *pptlOrg,
   IN MIX mix);

BOOL APIENTRY
RcddLineTo(
   IN SURFOBJ *pso,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN LONG x1,
   IN LONG y1,
   IN LONG x2,
   IN LONG y2,
   IN RECTL *prclBounds,
   IN MIX mix);

BOOL APIENTRY
RcddPaint(
   IN SURFOBJ *pso,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN MIX mix);

BOOL APIENTRY
RcddPlgBlt(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN COLORADJUSTMENT *pca,
   IN POINTL *pptlBrushOrg,
   IN POINTFIX *pptfx,
   IN RECTL *prclSrc,
   IN POINTL *pptlMask,
   IN ULONG iMode);

BOOL APIENTRY
RcddStrokePath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN XFORMOBJ *pxo,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN LINEATTRS *plineattrs,
   IN MIX mix);

BOOL APIENTRY
RcddFillPath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN MIX mix,
   IN FLONG flOptions);

BOOL APIENTRY
RcddStrokeAndFillPath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN XFORMOBJ *pxo,
   IN BRUSHOBJ *pboStroke,
   IN LINEATTRS *plineattrs,
   IN BRUSHOBJ *pboFill,
   IN POINTL *pptlBrushOrg,
   IN MIX mixFill,
   IN FLONG flOptions);

BOOL APIENTRY
RcddStretchBlt(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN COLORADJUSTMENT *pca,
   IN POINTL *pptlHTOrg,
   IN RECTL *prclDest,
   IN RECTL *prclSrc,
   IN POINTL *pptlMask,
   IN ULONG iMode);

BOOL APIENTRY
RcddStretchBltROP(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN COLORADJUSTMENT *pca,
   IN POINTL *pptlHTOrg,
   IN RECTL *prclDest,
   IN RECTL *prclSrc,
   IN POINTL *pptlMask,
   IN ULONG iMode,
   IN BRUSHOBJ *pbo,
   IN DWORD rop4);

BOOL APIENTRY
RcddAlphaBlend(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDest,
   IN RECTL *prclSrc,
   IN BLENDOBJ *pBlendObj);

BOOL APIENTRY
RcddTransparentBlt(
   IN SURFOBJ *psoDst,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDst,
   IN RECTL *prclSrc,
   IN ULONG iTransColor,
   IN ULONG ulReserved);

BOOL APIENTRY
RcddGradientFill(
   IN SURFOBJ *psoDest,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN TRIVERTEX *pVertex,
   IN ULONG nVertex,
   IN PVOID pMesh,
   IN ULONG nMesh,
   IN RECTL *prclExtents,
   IN POINTL *pptlDitherOrg,
   IN ULONG ulMode);

/*
 * RcddPresent - explicit present seam.
 *
 * Records damage in the mapped primary. GDI synchronization publishes the
 * completed pixels to dxgkrnl, which captures an immutable frame before
 * dispatching the miniport present.
 */
VOID
RcddPresent(
   PRCDD_PDEV ppdev,
   const RECTL *prcl);

VOID
RcddPresentEx(
   PRCDD_PDEV ppdev,
   const RECTL *prcl,
   ULONG Flags);

/* ---- escape.c : DWM composition contract -------------------------------- */

ULONG APIENTRY
RcddEscape(
   IN SURFOBJ *pso,
   IN ULONG iEsc,
   IN ULONG cjIn,
   IN PVOID pvIn,
   IN ULONG cjOut,
   OUT PVOID pvOut);

#endif /* _CDD_PCH_ */
