/*
 * ReactOS Generic Framebuffer display driver
 *
 * Copyright (C) 2004 Filip Navara
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _FRAMEBUF_PCH_
#define _FRAMEBUF_PCH_

#include <stdarg.h>
#include <windef.h>
#include <wingdi.h>
#include <winddi.h>
#include <ntddvdeo.h>
#include <uefifb/uefifb_ioctl.h>

//#define EXPERIMENTAL_MOUSE_CURSOR_SUPPORT

#if DBG
static __inline VOID FB_DBG(_In_z_ PCCH Format, ...)
{
    va_list ap;
    va_start(ap, Format);
    EngDebugPrint("FRAMEBUF", (PCHAR)Format, ap);
    va_end(ap);
}
#else
#define FB_DBG(...) do { } while (0)
#endif

typedef struct _FB_ACCEL_BACKEND FB_ACCEL_BACKEND, *PFB_ACCEL_BACKEND;

typedef struct _PDEV
{
   HANDLE hDriver;
   HDEV hDevEng;
   HSURF hSurfEng;
   ULONG ModeIndex;
   ULONG ScreenWidth;
   ULONG ScreenHeight;
   ULONG ScreenDelta;
   BYTE BitsPerPixel;
   ULONG RedMask;
   ULONG GreenMask;
   ULONG BlueMask;
   BYTE PaletteShift;
   PVOID ScreenPtr;
   HPALETTE DefaultPalette;
   PALETTEENTRY *PaletteEntries;
   BOOLEAN UefiLinearOnly;
   BOOLEAN UefiLargeFramebuffer;
   BOOLEAN UefiCapsValid;
   BOOLEAN RosUefiFramebuffer;
   ULONG UefiChildCount;
   ULONG UefiPrimaryChild;
   ULONG UefiSelectedChild;
   ULONGLONG UefiFramebufferLength;
   ULONGLONG FramebufferBytes;
   BOOLEAN UsingFallbackSurface;
   PVOID FallbackMapping;
   PVOID FallbackSection;

   /* Shadow buffer for WC/uncached VRAM performance */
   PVOID ShadowBuffer;    /* System RAM shadow copy */
   PVOID ShadowSection;   /* Section handle for EngFreeSectionMem */
   PVOID VramPtr;         /* Real VRAM pointer (for flush target) */
   BOOLEAN UsingShadow;   /* Whether shadow is active */
   HSURF hShadowBitmap;   /* Shadow bitmap surface for Eng* punt */
   SURFOBJ *psoShadow;    /* Persistently locked shadow SURFOBJ */
   LONG CursorWidth;      /* Cached cursor dimensions for flush */
   LONG CursorHeight;
   LONG CursorHotX;       /* Hotspot offset within cursor bitmap */
   LONG CursorHotY;
   RECTL OldCursorRect;   /* Last known cursor bounds for flush on move/hide */

#ifdef EXPERIMENTAL_MOUSE_CURSOR_SUPPORT
   VIDEO_POINTER_ATTRIBUTES PointerAttributes;
   XLATEOBJ *PointerXlateObject;
   HSURF PointerColorSurface;
   HSURF PointerMaskSurface;
   HSURF PointerSaveSurface;
   POINTL PointerHotSpot;
#endif

   /* DirectX Support */
   DWORD iDitherFormat;
   ULONG MemHeight;
   ULONG MemWidth;
   DWORD dwHeap;
   VIDEOMEMORY* pvmList;
   BOOL bDDInitialized;
   DDPIXELFORMAT ddpfDisplay;
   const FB_ACCEL_BACKEND *AccelBackend;
   PVOID BackendContext;
} PDEV, *PPDEV;

typedef struct _FB_ACCEL_BACKEND
{
   BOOL (*RectCopy)(
      _In_ PPDEV ppdev,
      _In_ const RECTL *DestRect,
      _In_ const POINTL *SrcPoint);
   BOOL (*RectFill)(
      _In_ PPDEV ppdev,
      _In_ const RECTL *Rect,
      _In_ ULONG Color);
} FB_ACCEL_BACKEND, *PFB_ACCEL_BACKEND;

#define DEVICE_NAME	L"framebuf"
#define ALLOC_TAG	'FUBF'


BOOL APIENTRY
DrvEnableDirectDraw(
    DHPDEV dhpdev,
    DD_CALLBACKS *pCallbacks,
    DD_SURFACECALLBACKS *pSurfaceCallbacks,
    DD_PALETTECALLBACKS *pPaletteCallbacks);

VOID APIENTRY
DrvDisableDirectDraw(
    DHPDEV dhpdev);

DHPDEV APIENTRY
DrvEnablePDEV(
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
DrvCompletePDEV(
   IN DHPDEV dhpdev,
   IN HDEV hdev);

VOID APIENTRY
DrvDisablePDEV(
   IN DHPDEV dhpdev);

HSURF APIENTRY
DrvEnableSurface(
   IN DHPDEV dhpdev);

VOID APIENTRY
DrvDisableSurface(
   IN DHPDEV dhpdev);

BOOLEAN
FbQueryUefiCaps(
   _Inout_ PPDEV ppdev);

VOID
FbEnsureUefiChildSelection(
   _Inout_ PPDEV ppdev,
   _In_opt_ const DEVMODEW *pdm);

BOOLEAN
FbMapFramebufferFallback(
   _Inout_ PPDEV ppdev,
   ULONGLONG Length);

VOID
FbSelectAccelerationBackend(
   _Inout_ PPDEV ppdev);

VOID
FbCleanupAccelerationBackend(
   _Inout_ PPDEV ppdev);

BOOL
FbBackendRectCopy(
   _In_ PPDEV ppdev,
   _In_ const RECTL *DestRect,
   _In_ const POINTL *SrcPoint);

BOOL
FbBackendRectFill(
   _In_ PPDEV ppdev,
   _In_ const RECTL *Rect,
   _In_ ULONG Color);

BOOL APIENTRY
DrvAssertMode(
   IN DHPDEV dhpdev,
   IN BOOL bEnable);

ULONG APIENTRY
DrvGetModes(
   IN HANDLE hDriver,
   IN ULONG cjSize,
   OUT DEVMODEW *pdm);

BOOL APIENTRY
DrvSetPalette(
   IN DHPDEV dhpdev,
   IN PALOBJ *ppalo,
   IN FLONG fl,
   IN ULONG iStart,
   IN ULONG cColors);

ULONG APIENTRY
DrvSetPointerShape(
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
DrvMovePointer(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl);

BOOL APIENTRY
DrvCopyBits(
   SURFOBJ *psoDst,
   SURFOBJ *psoSrc,
   CLIPOBJ *pco,
   XLATEOBJ *pxlo,
   RECTL *prclDst,
   POINTL *pptlSrc);

BOOL APIENTRY
DrvBitBlt(
   SURFOBJ *psoDst,
   SURFOBJ *psoSrc,
   SURFOBJ *psoMask,
   CLIPOBJ *pco,
   XLATEOBJ *pxlo,
   RECTL *prclDst,
   POINTL *pptlSrc,
   POINTL *pptlMask,
   BRUSHOBJ *pbo,
   POINTL *pptlBrush,
   ROP4 rop4);

BOOL APIENTRY
DrvRealizeBrush(
   BRUSHOBJ *pbo,
   SURFOBJ *psoTarget,
   SURFOBJ *psoPattern,
   SURFOBJ *psoMask,
   XLATEOBJ *pxlo,
   ULONG iHatch);

BOOL
IntInitScreenInfo(
   PPDEV ppdev,
   LPDEVMODEW pDevMode,
   PGDIINFO pGdiInfo,
   PDEVINFO pDevInfo);

DWORD
GetAvailableModes(
   HANDLE hDriver,
   PVIDEO_MODE_INFORMATION *ModeInfo,
   DWORD *ModeInfoSize);

BOOL
IntInitDefaultPalette(
   PPDEV ppdev,
   PDEVINFO pDevInfo);

BOOL APIENTRY
IntSetPalette(
   IN DHPDEV dhpdev,
   IN PPALETTEENTRY ppalent,
   IN ULONG iStart,
   IN ULONG cColors);

VOID
FbShadowFlushRect(
   _In_ PPDEV ppdev,
   _In_ const RECTL *prcl);

VOID
FbShadowFlushRects(
   _In_ PPDEV ppdev,
   _In_opt_ const RECTL *prcl1,
   _In_opt_ const RECTL *prcl2);

BOOL APIENTRY
DrvTextOut(
   SURFOBJ *pso,
   STROBJ *pstro,
   FONTOBJ *pfo,
   CLIPOBJ *pco,
   RECTL *prclExtra,
   RECTL *prclOpaque,
   BRUSHOBJ *pboFore,
   BRUSHOBJ *pboOpaque,
   POINTL *pptlOrg,
   MIX mix);

BOOL APIENTRY
DrvLineTo(
   SURFOBJ *pso,
   CLIPOBJ *pco,
   BRUSHOBJ *pbo,
   LONG x1,
   LONG y1,
   LONG x2,
   LONG y2,
   RECTL *prclBounds,
   MIX mix);

BOOL APIENTRY
DrvStrokePath(
   SURFOBJ *pso,
   PATHOBJ *ppo,
   CLIPOBJ *pco,
   XFORMOBJ *pxo,
   BRUSHOBJ *pbo,
   POINTL *pptlBrushOrg,
   LINEATTRS *plineattrs,
   MIX mix);

BOOL APIENTRY
DrvFillPath(
   SURFOBJ *pso,
   PATHOBJ *ppo,
   CLIPOBJ *pco,
   BRUSHOBJ *pbo,
   POINTL *pptlBrushOrg,
   MIX mix,
   FLONG flOptions);

BOOL APIENTRY
DrvStrokeAndFillPath(
   SURFOBJ *pso,
   PATHOBJ *ppo,
   CLIPOBJ *pco,
   XFORMOBJ *pxo,
   BRUSHOBJ *pboStroke,
   LINEATTRS *plineattrs,
   BRUSHOBJ *pboFill,
   POINTL *pptlBrushOrg,
   MIX mixFill,
   FLONG flOptions);

BOOL APIENTRY
DrvPaint(
   SURFOBJ *pso,
   CLIPOBJ *pco,
   BRUSHOBJ *pbo,
   POINTL *pptlBrushOrg,
   MIX mix);

BOOL APIENTRY
DrvStretchBlt(
   SURFOBJ *psoDest,
   SURFOBJ *psoSrc,
   SURFOBJ *psoMask,
   CLIPOBJ *pco,
   XLATEOBJ *pxlo,
   COLORADJUSTMENT *pca,
   POINTL *pptlHTOrg,
   RECTL *prclDest,
   RECTL *prclSrc,
   POINTL *pptlMask,
   ULONG iMode);

BOOL APIENTRY
DrvStretchBltROP(
   SURFOBJ *psoDest,
   SURFOBJ *psoSrc,
   SURFOBJ *psoMask,
   CLIPOBJ *pco,
   XLATEOBJ *pxlo,
   COLORADJUSTMENT *pca,
   POINTL *pptlHTOrg,
   RECTL *prclDest,
   RECTL *prclSrc,
   POINTL *pptlMask,
   ULONG iMode,
   BRUSHOBJ *pbo,
   ROP4 rop4);

BOOL APIENTRY
DrvAlphaBlend(
   SURFOBJ *psoDest,
   SURFOBJ *psoSrc,
   CLIPOBJ *pco,
   XLATEOBJ *pxlo,
   RECTL *prclDest,
   RECTL *prclSrc,
   BLENDOBJ *pBlendObj);

BOOL APIENTRY
DrvTransparentBlt(
   SURFOBJ *psoDst,
   SURFOBJ *psoSrc,
   CLIPOBJ *pco,
   XLATEOBJ *pxlo,
   RECTL *prclDst,
   RECTL *prclSrc,
   ULONG iTransColor,
   ULONG ulReserved);

BOOL APIENTRY
DrvGradientFill(
   SURFOBJ *pso,
   CLIPOBJ *pco,
   XLATEOBJ *pxlo,
   TRIVERTEX *pVertex,
   ULONG nVertex,
   PVOID pMesh,
   ULONG nMesh,
   RECTL *prclExtents,
   POINTL *pptlDitherOrg,
   ULONG ulMode);

BOOL APIENTRY
DrvPlgBlt(
   SURFOBJ *psoTrg,
   SURFOBJ *psoSrc,
   SURFOBJ *psoMsk,
   CLIPOBJ *pco,
   XLATEOBJ *pxlo,
   COLORADJUSTMENT *pca,
   POINTL *pptlBrushOrg,
   POINTFIX *pptfx,
   RECTL *prclSrc,
   POINTL *pptlMask,
   ULONG iMode);

#endif /* _FRAMEBUF_PCH_ */
