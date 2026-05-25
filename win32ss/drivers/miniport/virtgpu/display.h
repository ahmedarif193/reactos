/*
 * PROJECT:     ReactOS VirtIO GPU Display Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     XPDM display driver paired with the VirtIO GPU miniport
 */

#ifndef _VIRTGPU_DISPLAY_PCH_
#define _VIRTGPU_DISPLAY_PCH_

#include <stdarg.h>
#include <stddef.h>
#include <windef.h>
#include <wingdi.h>
#include <winddi.h>
#include <winioctl.h>
#include <ntddvdeo.h>

#include "virtgpu_shared.h"

#define DEVICE_NAME L"virtgpu"
#define ALLOC_TAG 'dgrV'
#define VIRTGPU_DISP_MAX_ULONG ((ULONG)~0UL)

typedef struct _PDEV
{
    ULONG Signature;
    HANDLE hDriver;
    HDEV hDevEng;
    HSURF hSurfEng;
    HSURF hSurfFrameBuffer;
    SURFOBJ *psoFrameBuffer;
    ULONG ModeIndex;
    ULONG ScreenWidth;
    ULONG ScreenHeight;
    ULONG ScreenDelta;
    UCHAR BitsPerPixel;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    PVOID ScreenPtr;
    HPALETTE DefaultPalette;

    VIDEO_POINTER_CAPABILITIES PointerCapabilities;
    PVIDEO_POINTER_ATTRIBUTES PointerAttributes;
    ULONG PointerAttributesSize;
    POINTL PointerHotSpot;
    BOOL PointerSupported;
    BOOL PointerShapeValid;
    BOOL PointerVisible;
    BOOL DirectDrawEnabled;
    BOOL ThreeDEnabled;
    BOOL ContextInitSupported;
    BOOL ResourceUuidSupported;
    BOOL ResourceBlobSupported;
    ULONG PreferredCapsetId;
    ULONG PreferredCapsetVersion;
    ULONG SupportedCapsetMask;
    BOOL Gdi3DEnabled;
    ULONG Gdi3DContextId;
    ULONG Gdi3DPrimaryResourceId;
    ULONG Gdi3DPrimarySurfaceHandle;
    ULONG Gdi3DWidth;
    ULONG Gdi3DHeight;
    ULONGLONG Gdi3DLastFenceId;
    BOOL DirtyValid;
    RECTL DirtyRect;
    ULONG DirtyOps;
} PDEV, *PPDEV;

BOOL
VirtGpuDispInitScreenInfo(
    _Inout_ PPDEV ppdev,
    _In_ LPDEVMODEW DevMode,
    _Out_ PGDIINFO GdiInfo,
    _Out_ PDEVINFO DevInfo);

ULONG
APIENTRY
DrvGetModes(
    _In_ HANDLE hDriver,
    _In_ ULONG cjSize,
    _Out_opt_ DEVMODEW *pdm);

BOOL
VirtGpuDispInitPalette(
    _Inout_ PPDEV ppdev,
    _Inout_ PDEVINFO DevInfo);

BOOL
VirtGpuDispInitPointer(
    _Inout_ PPDEV ppdev);

VOID
VirtGpuDispDisablePointer(
    _Inout_ PPDEV ppdev);

BOOL
VirtGpuDispSetPointerShape(
    _Inout_ PPDEV ppdev,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ SURFOBJ *psoColor,
    _In_ LONG xHot,
    _In_ LONG yHot,
    _In_ LONG x,
    _In_ LONG y,
    _In_ FLONG fl);

BOOL
VirtGpuDispMovePointer(
    _Inout_ PPDEV ppdev,
    _In_ LONG x,
    _In_ LONG y);

VOID
VirtGpuDispFlushRect(
    _Inout_ PPDEV ppdev,
    _In_opt_ const RECTL *Rect);

VOID
VirtGpuDispCommitDirty(_Inout_ PPDEV ppdev);

VOID
VirtGpuDispFlushSurfaceRect(
    _Inout_ SURFOBJ *pso,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ const RECTL *Rect);

BOOL
APIENTRY
DrvEnableDriver(
    _In_ ULONG iEngineVersion,
    _In_ ULONG cj,
    _Out_ PDRVENABLEDATA pded);

DHPDEV
APIENTRY
DrvEnablePDEV(
    _In_ DEVMODEW *pdm,
    _In_ LPWSTR pwszLogAddress,
    _In_ ULONG cPat,
    _Out_ HSURF *phsurfPatterns,
    _In_ ULONG cjCaps,
    _Out_ ULONG *pdevcaps,
    _In_ ULONG cjDevInfo,
    _Out_ DEVINFO *pdi,
    _In_ HDEV hdev,
    _In_ LPWSTR pwszDeviceName,
    _In_ HANDLE hDriver);

VOID
APIENTRY
DrvCompletePDEV(
    _In_ DHPDEV dhpdev,
    _In_ HDEV hdev);

VOID
APIENTRY
DrvDisablePDEV(
    _In_ DHPDEV dhpdev);

HSURF
APIENTRY
DrvEnableSurface(
    _In_ DHPDEV dhpdev);

VOID
APIENTRY
DrvDisableSurface(
    _In_ DHPDEV dhpdev);

BOOL
APIENTRY
DrvAssertMode(
    _In_ DHPDEV dhpdev,
    _In_ BOOL bEnable);

ULONG
APIENTRY
DrvSetPointerShape(
    _In_ SURFOBJ *pso,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ SURFOBJ *psoColor,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ LONG xHot,
    _In_ LONG yHot,
    _In_ LONG x,
    _In_ LONG y,
    _Inout_opt_ RECTL *prcl,
    _In_ FLONG fl);

VOID
APIENTRY
DrvMovePointer(
    _In_ SURFOBJ *pso,
    _In_ LONG x,
    _In_ LONG y,
    _Inout_opt_ RECTL *prcl);

VOID
APIENTRY
DrvSynchronize(
    _In_ DHPDEV dhpdev,
    _In_opt_ RECTL *prcl);

VOID
APIENTRY
DrvSynchronizeSurface(
    _In_ SURFOBJ *pso,
    _In_opt_ RECTL *prcl,
    _In_ FLONG fl);

ULONG_PTR
APIENTRY
DrvSaveScreenBits(
    _In_ SURFOBJ *pso,
    _In_ ULONG iMode,
    _In_ ULONG_PTR ident,
    _In_ RECTL *prcl);

HBITMAP
APIENTRY
DrvCreateDeviceBitmap(
    _In_ DHPDEV dhpdev,
    _In_ SIZEL sizl,
    _In_ ULONG iFormat);

VOID
APIENTRY
DrvDeleteDeviceBitmap(
    _In_ DHSURF dhsurf);

BOOL
APIENTRY
DrvGetDirectDrawInfo(
    _In_ DHPDEV dhpdev,
    _Out_ DD_HALINFO *pHalInfo,
    _Out_ DWORD *pdwNumHeaps,
    _Out_opt_ VIDEOMEMORY *pvmList,
    _Out_ DWORD *pdwNumFourCCCodes,
    _Out_opt_ DWORD *pdwFourCC);

BOOL
APIENTRY
DrvEnableDirectDraw(
    _In_ DHPDEV dhpdev,
    _Out_opt_ DD_CALLBACKS *pCallBacks,
    _Out_opt_ DD_SURFACECALLBACKS *pSurfaceCallBacks,
    _Out_opt_ DD_PALETTECALLBACKS *pPaletteCallBacks);

VOID
APIENTRY
DrvDisableDirectDraw(
    _In_ DHPDEV dhpdev);

ULONG
APIENTRY
DrvEscape(
    _In_ SURFOBJ *pso,
    _In_ ULONG iEsc,
    _In_ ULONG cjIn,
    _In_reads_bytes_(cjIn) PVOID pvIn,
    _In_ ULONG cjOut,
    _Out_writes_bytes_(cjOut) PVOID pvOut);

BOOL
APIENTRY
DrvBitBlt(
    _Inout_ SURFOBJ *psoTrg,
    _In_opt_ SURFOBJ *psoSrc,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclTrg,
    _In_opt_ POINTL *pptlSrc,
    _In_opt_ POINTL *pptlMask,
    _In_opt_ BRUSHOBJ *pbo,
    _In_opt_ POINTL *pptlBrush,
    _In_ ROP4 rop4);

BOOL
APIENTRY
DrvCopyBits(
    _Inout_ SURFOBJ *psoDest,
    _In_ SURFOBJ *psoSrc,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclDest,
    _In_ POINTL *pptlSrc);

BOOL
APIENTRY
DrvLineTo(
    _Inout_ SURFOBJ *pso,
    _In_opt_ CLIPOBJ *pco,
    _In_ BRUSHOBJ *pbo,
    _In_ LONG x1,
    _In_ LONG y1,
    _In_ LONG x2,
    _In_ LONG y2,
    _In_opt_ RECTL *prclBounds,
    _In_ MIX mix);

BOOL
APIENTRY
DrvPaint(
    _Inout_ SURFOBJ *pso,
    _In_opt_ CLIPOBJ *pco,
    _In_ BRUSHOBJ *pbo,
    _In_opt_ POINTL *pptlBrushOrg,
    _In_ MIX mix);

BOOL
APIENTRY
DrvStretchBlt(
    _Inout_ SURFOBJ *psoDest,
    _Inout_ SURFOBJ *psoSrc,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_opt_ COLORADJUSTMENT *pca,
    _In_ POINTL *pptlHTOrg,
    _In_ RECTL *prclDest,
    _In_ RECTL *prclSrc,
    _When_(psoMask, _In_) POINTL *pptlMask,
    _In_ ULONG iMode);

BOOL
APIENTRY
DrvStretchBltROP(
    _Inout_ SURFOBJ *psoDest,
    _Inout_ SURFOBJ *psoSrc,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_opt_ COLORADJUSTMENT *pca,
    _In_ POINTL *pptlHTOrg,
    _In_ RECTL *prclDest,
    _In_ RECTL *prclSrc,
    _When_(psoMask, _In_) POINTL *pptlMask,
    _In_ ULONG iMode,
    _In_opt_ BRUSHOBJ *pbo,
    _In_ DWORD rop4);

BOOL
APIENTRY
DrvAlphaBlend(
    _Inout_ SURFOBJ *psoDest,
    _In_ SURFOBJ *psoSrc,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclDest,
    _In_ RECTL *prclSrc,
    _In_ BLENDOBJ *pBlendObj);

BOOL
APIENTRY
DrvTransparentBlt(
    _Inout_ SURFOBJ *psoDst,
    _In_ SURFOBJ *psoSrc,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ RECTL *prclDst,
    _In_ RECTL *prclSrc,
    _In_ ULONG iTransColor,
    _In_ ULONG ulReserved);

BOOL
APIENTRY
DrvGradientFill(
    _Inout_ SURFOBJ *psoDest,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ TRIVERTEX *pVertex,
    _In_ ULONG nVertex,
    _In_ PVOID pMesh,
    _In_ ULONG nMesh,
    _In_ RECTL *prclExtents,
    _In_ POINTL *pptlDitherOrg,
    _In_ ULONG ulMode);

#endif
