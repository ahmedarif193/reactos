/*
 * ReactOS Canonical Display Driver (CDD)
 *
 * Copyright (C) 2026 ReactOS Team
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
 *
 * OVERVIEW
 * --------
 * The Canonical Display Driver (CDD) replaces framebuf.dll for WDDM
 * adapters.  On Windows Vista+, CDD renders GDI operations to D3D-backed
 * surfaces instead of raw framebuffer memory, enabling DWM compositing.
 *
 * Phase 1: CDD talks to the WDDM miniport through EngDeviceIoControl
 * (same as framebuf) but structures its PDEV and surfaces to be D3DKMT-
 * ready.  GDI drawing goes through Eng* functions on a system-memory
 * backing store.  The key architectural difference from framebuf is that
 * the primary surface is a STYPE_DEVICE surface backed by a shadow bitmap,
 * which mirrors the model that D3DKMT allocations will use in Phase 2.
 *
 * Phase 2 (future): Replace EngDeviceIoControl framebuffer mapping with
 * D3DKMTCreateAllocation / D3DKMTLock to obtain GPU-backed surfaces that
 * DWM can composite.
 */

#ifndef _CDD_PCH_
#define _CDD_PCH_

#include <stdarg.h>
#include <windef.h>
#include <winioctl.h>
#include <wingdi.h>
#include <winddi.h>
#include <ntddvdeo.h>
#include <d3dkmthk.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS        ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL   ((NTSTATUS)0xC0000001L)
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED  ((NTSTATUS)0xC00000BBL)
#endif
#ifndef STATUS_INSUFFICIENT_RESOURCES
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009AL)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif
#ifndef STATUS_PENDING
#define STATUS_PENDING        ((NTSTATUS)0x00000103L)
#endif

#if DBG
static __inline VOID CDD_DBG(_In_z_ PCCH Format, ...)
{
    va_list ap;
    va_start(ap, Format);
    EngDebugPrint("CDD", (PCHAR)Format, ap);
    va_end(ap);
}
#else
#define CDD_DBG(...) do { } while (0)
#endif

/*
 * CDD_PDEV -- physical device structure
 *
 * Contains the standard GDI/miniport handles plus fields for D3DKMT
 * integration.  Phase 1 uses only the miniport path; the D3DKMT fields
 * are initialized to zero/NULL and will be populated in Phase 2.
 */
typedef struct _CDD_PDEV
{
    /* --- GDI engine handles ------------------------------------------------ */
    HANDLE hDriver;           /* Miniport device handle from GDI */
    HDEV   hDevEng;           /* GDI engine device handle */
    HSURF  hSurfEng;          /* Primary GDI surface handle */

    /* --- Display mode ------------------------------------------------------ */
    ULONG  ModeIndex;
    ULONG  ScreenWidth;
    ULONG  ScreenHeight;
    ULONG  ScreenDelta;       /* Bytes per scanline (stride) */
    BYTE   BitsPerPixel;
    ULONG  RedMask;
    ULONG  GreenMask;
    ULONG  BlueMask;
    BYTE   PaletteShift;
    DWORD  iDitherFormat;     /* BMF_* format for the surface */
    BOOLEAN SysmemFramebuffer;/* dxgkrnl-provided sysmem scanout buffer */

    /* --- Palette ----------------------------------------------------------- */
    HPALETTE      DefaultPalette;
    PALETTEENTRY *PaletteEntries;

    /* --- Primary surface backing store ------------------------------------- */
    PVOID  ScreenPtr;         /* Pointer to drawable backing store */
    PVOID  VramPtr;           /* Real VRAM mapping (NULL if no shadow) */

    /*
     * Shadow buffer model:
     * CDD always uses a shadow buffer (system RAM) as the GDI drawing
     * target.  This gives us:
     * 1. Fast cached writes for GDI operations
     * 2. Decoupling from VRAM write-combining performance issues
     * 3. A surface that D3DKMT can back in Phase 2
     *
     * The primary surface is STYPE_DEVICE (so hooks dispatch through us),
     * while hShadowBitmap is a STYPE_BITMAP that Eng* functions draw on
     * when we punt.
     */
    PVOID    ShadowBuffer;    /* System RAM shadow copy */
    PVOID    ShadowSection;   /* Section handle for EngFreeSectionMem */
    HSURF    hShadowBitmap;   /* Shadow bitmap surface for Eng* punt */
    SURFOBJ *psoShadow;       /* Persistently locked shadow SURFOBJ */

    /* --- Cursor tracking --------------------------------------------------- */
    LONG  CursorWidth;
    LONG  CursorHeight;
    LONG  CursorHotX;
    LONG  CursorHotY;
    RECTL OldCursorRect;

    /* --- D3DKMT state (Phase 2) ------------------------------------------- */
    /*
     * These fields will hold D3DKMT handles when CDD transitions to using
     * D3DKMTCreateAllocation for its primary surface.  Phase 1 leaves them
     * zeroed; their presence documents the target architecture.
     */
    ULONG  hD3DAdapter;       /* D3DKMT_HANDLE from OpenAdapter */
    ULONG  hD3DDevice;        /* D3DKMT_HANDLE from CreateDevice */
    ULONG  hD3DPrimaryAllocation; /* Shared primary allocation handle */
    ULONG  hD3DPrimaryResource;   /* Shared primary resource handle */
    ULONG  hD3DShadowAllocation;  /* Coherent shadow allocation handle */
    ULONG  hD3DShadowResource;    /* Coherent shadow resource handle */
    ULONG  VidPnSourceId;     /* VidPN source ID for this display */
    BOOLEAN D3DKMTConnected;  /* TRUE when drawing via shared primary */
    ULONG  LastPrimaryUpdateTick; /* Last tick when shared-primary scanout was nudged */
    ULONG  ShadowPitch;       /* Pitch returned by the shadow allocation */

    /* --- WDDM locking state ------------------------------------------------ */
    HSEMAPHORE hDevLock;      /* Device-level exclusive lock (CDDDEVLOCK) */
    HSEMAPHORE hShadowLock;   /* Shadow surface read/write semaphore */
    HSEMAPHORE hTileLock;     /* Tile lock for DrvLockDisplayArea */
    BOOLEAN    TileLocked;    /* TRUE while tile lock is held */
    BOOLEAN    DxInteropActive; /* TRUE between StartDxInterop/EndDxInterop */

    /* --- Dirty region tracking --------------------------------------------- */
    /*
     * CDD tracks dirty rectangles as a bounding box that accumulates all
     * dirty areas since the last present.  The present worker consumes
     * and resets this box each frame.
     *
     * Note: Win7 CDD uses Eng*Rgn GDI region APIs for precise tracking.
     * ReactOS win32k does not export those yet, so we use bounding-box
     * accumulation which is functionally correct (may over-present).
     */
    RECTL   DirtyRect;        /* Accumulated dirty bounding box */
    BOOLEAN DirtyValid;       /* TRUE when DirtyRect has pending work */

    /* --- Present worker thread --------------------------------------------- */
    HANDLE  hPresentThread;   /* System thread handle */
    PVOID   pPresentThread;   /* PETHREAD for referencing */
    /*
     * Event objects stored as opaque byte arrays because winddi.h
     * does not expose KEVENT/KTIMER types.  Size = 24 bytes on
     * amd64 (DISPATCHER_HEADER + LIST_ENTRY), rounded up for safety.
     */
    DECLSPEC_ALIGN(8) UCHAR PresentEvent[64];
    DECLSPEC_ALIGN(8) UCHAR PresentExitEvent[64];
    DECLSPEC_ALIGN(8) UCHAR PresentTimer[64];
    BOOLEAN PresentRunning;   /* FALSE once the worker has exited */

    /* --- W32kCddInterface state -------------------------------------------- */
    BOOLEAN CddInterfaceRegistered; /* TRUE after EngQueryW32kCddInterface */

    /* --- Redirection bitmap tracking --------------------------------------- */
    HSURF   hRedirBitmap;     /* Redirection bitmap for DWM */
    SURFOBJ *psoRedir;        /* Locked redirection SURFOBJ */

    /* --- DxgKrnl IOCTL communication --------------------------------------- */
    PVOID   pDxgkDeviceObject; /* DxgKrnl device object for IOCTL 0x0023E05B */
    PVOID   pDxgkFileObject;   /* File object reference for cleanup */

    /* --- Shared section for DWM -------------------------------------------- */
    PVOID   hSharedSection;    /* Section object for DWM surface sharing */
    PVOID   pSharedMapping;    /* Session-space mapping of shared section */
    SIZE_T  SharedMappingSize; /* Size of shared section */

} CDD_PDEV, *PCDD_PDEV;

/*
 * Win7+ DDI indices that may not be defined if NTDDI_VERSION < NTDDI_WIN7.
 * CDD needs these for the WDDM compositing extensions.
 */
#ifndef INDEX_DrvCreateDeviceBitmapEx
#define INDEX_DrvCreateDeviceBitmapEx         94L
#endif
#ifndef INDEX_DrvDeleteDeviceBitmapEx
#define INDEX_DrvDeleteDeviceBitmapEx         95L
#endif
#ifndef INDEX_DrvAssociateSharedSurface
#define INDEX_DrvAssociateSharedSurface       96L
#endif
#ifndef INDEX_DrvSynchronizeRedirectionBitmaps
#define INDEX_DrvSynchronizeRedirectionBitmaps 97L
#endif
#ifndef INDEX_DrvAccumulateD3DDirtyRect
#define INDEX_DrvAccumulateD3DDirtyRect       98L
#endif
#ifndef INDEX_DrvStartDxInterop
#define INDEX_DrvStartDxInterop              99L
#endif
#ifndef INDEX_DrvEndDxInterop
#define INDEX_DrvEndDxInterop               100L
#endif
#ifndef INDEX_DrvLockDisplayArea
#define INDEX_DrvLockDisplayArea             101L
#endif
#ifndef INDEX_DrvUnlockDisplayArea
#define INDEX_DrvUnlockDisplayArea           102L
#endif

#define DEVICE_NAME  L"cdd"
#define ALLOC_TAG    'DDCr'  /* 'rCDD' in little-endian memory view */
#define ALLOC_TAG_BITMAP 'BDCr'  /* 'rCDB' */
#define ALLOC_TAG_INTF   'IDCr'  /* 'rCDI' */

/*
 * W32kCddInterface -- callback table that win32k uses to notify CDD
 * of WDDM events.  Registered via EngQueryW32kCddInterface.
 *
 * Each callback receives an opaque context (the CDD_PDEV pointer)
 * as its first parameter.
 */
#define CDD_INTERFACE_VERSION  1
#define CDD_INTERFACE_COUNT   17

typedef NTSTATUS (APIENTRY *PFN_CDD_DEVICE_OPEN_CLOSE)(PVOID Context, BOOLEAN Open);
typedef NTSTATUS (APIENTRY *PFN_CDD_SURFACE_COMPLETE)(PVOID Context, HSURF hSurf, SIZEL Size, LONG Pitch, ULONG Format);
typedef NTSTATUS (APIENTRY *PFN_CDD_DEVICE_CONTROL)(PVOID Context, ULONG IoCtl, PVOID pData, ULONG DataLen);
typedef VOID     (APIENTRY *PFN_CDD_NOTIFY_VSYNC)(PVOID Context, ULONG VidPnSourceId);
typedef NTSTATUS (APIENTRY *PFN_CDD_NOTIFY_NEW_MODE)(PVOID Context, ULONG Width, ULONG Height, ULONG Pitch, ULONG BitsPerPixel);
typedef NTSTATUS (APIENTRY *PFN_CDD_NOTIFY_DPI_CHANGE)(PVOID Context, ULONG Dpi);
typedef NTSTATUS (APIENTRY *PFN_CDD_NOTIFY_POWER_EVENT)(PVOID Context, ULONG PowerState);
typedef NTSTATUS (APIENTRY *PFN_CDD_NOTIFY_MODE_CHANGE)(PVOID Context);
typedef NTSTATUS (APIENTRY *PFN_CDD_NOTIFY_MULTIMON)(PVOID Context, ULONG MonitorCount, BOOLEAN Arrival);
typedef NTSTATUS (APIENTRY *PFN_CDD_NOTIFY_SESSION_ATTACH)(PVOID Context, BOOLEAN Attach);
typedef HBITMAP  (APIENTRY *PFN_CDD_CREATE_DEVICE_BITMAP)(PVOID Context, SIZEL Size, ULONG Format);
typedef VOID     (APIENTRY *PFN_CDD_DELETE_DEVICE_BITMAP)(PVOID Context, HBITMAP hBitmap);
typedef HBITMAP  (APIENTRY *PFN_CDD_CREATE_DEVICE_BITMAP_REDIR)(PVOID Context, SIZEL Size, ULONG Format);
typedef VOID     (APIENTRY *PFN_CDD_HANDLE_DISABLE_ALL)(PVOID Context);
typedef VOID     (APIENTRY *PFN_CDD_DISABLE_DRIVER_1)(PVOID Context);
typedef VOID     (APIENTRY *PFN_CDD_DISABLE_DRIVER_2)(PVOID Context);
typedef NTSTATUS (APIENTRY *PFN_CDD_RESET_DEVICE)(PVOID Context);

typedef struct _W32KCDD_INTERFACE
{
    PFN_CDD_DEVICE_OPEN_CLOSE       DeviceOpenClose;        /* [0] */
    PFN_CDD_SURFACE_COMPLETE        SurfaceComplete;        /* [1] */
    PFN_CDD_DEVICE_CONTROL          DeviceControl;          /* [2] */
    PFN_CDD_NOTIFY_VSYNC            NotifyVSync;            /* [3] */
    PFN_CDD_NOTIFY_NEW_MODE         NotifyNewMode;          /* [4] */
    PFN_CDD_NOTIFY_DPI_CHANGE       NotifyDpiChange;        /* [5] */
    PFN_CDD_NOTIFY_POWER_EVENT      NotifyPowerEvent;       /* [6] */
    PFN_CDD_NOTIFY_MODE_CHANGE      NotifyModeChange;       /* [7] */
    PFN_CDD_NOTIFY_MULTIMON         NotifyMultimon;         /* [8] */
    PFN_CDD_NOTIFY_SESSION_ATTACH   NotifySessionAttach;    /* [9] */
    PFN_CDD_CREATE_DEVICE_BITMAP    CreateDeviceBitmap;     /* [10] */
    PFN_CDD_DELETE_DEVICE_BITMAP    DeleteDeviceBitmap;     /* [11] */
    PFN_CDD_CREATE_DEVICE_BITMAP_REDIR CreateDeviceBitmapRedirect; /* [12] */
    PFN_CDD_HANDLE_DISABLE_ALL      HandleDisableAll;       /* [13] */
    PFN_CDD_DISABLE_DRIVER_1        DisableDriver1;         /* [14] */
    PFN_CDD_DISABLE_DRIVER_2        DisableDriver2;         /* [15] */
    PFN_CDD_RESET_DEVICE            ResetDevice;            /* [16] */
} W32KCDD_INTERFACE, *PW32KCDD_INTERFACE;

/*
 * EngQueryW32kCddInterface prototype -- exported by win32k.sys (ordinal 133).
 * CDD calls this to register its callback table with the GDI engine.
 */
typedef NTSTATUS (APIENTRY *PFN_EngQueryW32kCddInterface)(
    _In_ ULONG InterfaceVersion,
    _In_ PVOID Context,
    _In_ PW32KCDD_INTERFACE pInterface,
    _In_ ULONG InterfaceCount);

/* Present worker timer interval: ~16ms for ~60 Hz frame pacing */
#define CDD_PRESENT_TIMER_MS  16

#define CDD_DISP_DRIVERSPEC_SYSMEM_FB 0x0001

#ifndef IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT
#define IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x920, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

ULONGLONG APIENTRY
EngGetTickCount(VOID);

#ifndef EngGetTickCount32
#define EngGetTickCount32() ((ULONG)EngGetTickCount())
#endif


/* --- cdd_enable.c --------------------------------------------------------- */

BOOL APIENTRY
DrvEnableDriver(
    ULONG iEngineVersion,
    ULONG cj,
    PDRVENABLEDATA pded);

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


/* --- cdd_screen.c --------------------------------------------------------- */

BOOL
CddInitScreenInfo(
    PCDD_PDEV ppdev,
    LPDEVMODEW pDevMode,
    PGDIINFO pGdiInfo,
    PDEVINFO pDevInfo);

DWORD
CddGetAvailableModes(
    HANDLE hDriver,
    PVIDEO_MODE_INFORMATION *ModeInfo,
    DWORD *ModeInfoSize);

BOOL
CddInitDefaultPalette(
    PCDD_PDEV ppdev,
    PDEVINFO pDevInfo);

BOOL APIENTRY
CddSetPaletteHw(
    IN DHPDEV dhpdev,
    IN PPALETTEENTRY ppalent,
    IN ULONG iStart,
    IN ULONG cColors);


/* --- cdd_surface.c -------------------------------------------------------- */

/* (DrvEnableSurface / DrvDisableSurface are declared above) */

VOID
CddNotifyPrimaryUpdate(
    _In_ PCDD_PDEV ppdev);

VOID
CddPresentPrimaryRect(
    _In_ PCDD_PDEV ppdev,
    _In_opt_ const RECTL *prcl);


/* --- cdd_drawing.c -------------------------------------------------------- */

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
DrvCopyBits(
    SURFOBJ *psoDst,
    SURFOBJ *psoSrc,
    CLIPOBJ *pco,
    XLATEOBJ *pxlo,
    RECTL *prclDst,
    POINTL *pptlSrc);

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


/* --- Internal helpers (cdd_drawing.c) ------------------------------------- */

VOID
CddShadowFlushRect(
    _In_ PCDD_PDEV ppdev,
    _In_ const RECTL *prcl);

VOID
CddShadowFlushRects(
    _In_ PCDD_PDEV ppdev,
    _In_opt_ const RECTL *prcl1,
    _In_opt_ const RECTL *prcl2);


/* --- cdd_wddm.c -- WDDM DDI extensions and W32kCddInterface -------------- */

/* DDI entries: device bitmap management */
HBITMAP APIENTRY
DrvCreateDeviceBitmap(
    IN DHPDEV dhpdev,
    IN SIZEL sizl,
    IN ULONG iFormat);

VOID APIENTRY
DrvDeleteDeviceBitmap(
    IN DHSURF dhsurf);

/* DDI entries: font synthesis (stubs for DWM) */
PFD_GLYPHATTR APIENTRY
DrvSynthesizeFont(
    IN SURFOBJ *pso,
    IN FONTOBJ *pfo,
    IN ULONG iMode);

BOOL APIENTRY
DrvGetSynthesizedFontFiles(
    IN DHPDEV dhpdev,
    IN FONTOBJ *pfo,
    IN ULONG iMode,
    OUT PVOID *ppvData,
    OUT PULONG pcjData);

/* DDI entries: WDDM redirection bitmap and composition sync */
HBITMAP APIENTRY
DrvCreateDeviceBitmapEx(
    IN DHPDEV dhpdev,
    IN SIZEL sizl,
    IN ULONG iFormat,
    IN FLONG fl);

VOID APIENTRY
DrvDeleteDeviceBitmapEx(
    IN DHSURF dhsurf);

BOOL APIENTRY
DrvAssociateSharedSurface(
    IN SURFOBJ *pso,
    IN HANDLE hShared,
    IN HANDLE hSecure);

BOOL APIENTRY
DrvSynchronizeRedirectionBitmaps(
    IN DHPDEV dhpdev,
    OUT RECTL *prclDirty);

BOOL APIENTRY
DrvAccumulateD3DDirtyRect(
    IN DHPDEV dhpdev,
    IN RECTL *prcl);

/* DDI entries: DX/GDI interop */
BOOL APIENTRY
DrvStartDxInterop(
    IN SURFOBJ *pso,
    IN BOOL bExclusive);

BOOL APIENTRY
DrvEndDxInterop(
    IN SURFOBJ *pso,
    IN BOOL bExclusive);

/* DDI entries: display area lock and present */
BOOL APIENTRY
DrvLockDisplayArea(
    IN DHPDEV dhpdev,
    IN RECTL *prcl);

BOOL APIENTRY
DrvUnlockDisplayArea(
    IN DHPDEV dhpdev,
    IN RECTL *prcl);

/* Present worker and locking */
VOID
CddInitLocking(
    _Inout_ PCDD_PDEV ppdev);

VOID
CddDestroyLocking(
    _Inout_ PCDD_PDEV ppdev);

NTSTATUS
CddStartPresentWorker(
    _Inout_ PCDD_PDEV ppdev);

VOID
CddStopPresentWorker(
    _Inout_ PCDD_PDEV ppdev);

VOID
CddAccumulateDirtyRect(
    _Inout_ PCDD_PDEV ppdev,
    _In_ const RECTL *prcl);

NTSTATUS
CddRegisterW32kInterface(
    _Inout_ PCDD_PDEV ppdev);

/* DxgKrnl IOCTL communication */
NTSTATUS
CddOpenDxgkDevice(
    _Inout_ PCDD_PDEV ppdev);

VOID
CddCloseDxgkDevice(
    _Inout_ PCDD_PDEV ppdev);

NTSTATUS
CddSendDxgkNotify(
    _In_ PCDD_PDEV ppdev,
    _In_ PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputLength);

/* Shared section for DWM */
NTSTATUS
CddCreateSharedSection(
    _Inout_ PCDD_PDEV ppdev,
    _In_ SIZE_T Size);

VOID
CddDestroySharedSection(
    _Inout_ PCDD_PDEV ppdev);

/*
 * DxgKrnl IOCTL code -- the single IOCTL that CDD sends to DxgKrnl
 * for present completion notification and surface registration.
 * CTL_CODE(FILE_DEVICE_VIDEO=0x23, 0x816, METHOD_NEITHER, FILE_RW_ACCESS)
 */
#define IOCTL_CDD_DXGKRNL_NOTIFY \
    ((ULONG)0x0023E05BUL)

#define CDD_DXGK_DEVICE_NAME L"\\Device\\DxgKrnl"

#endif /* _CDD_PCH_ */
