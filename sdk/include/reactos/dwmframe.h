/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared ABI between win32k (NtUserDwm* via NtUserCallOneParam)
 *              and the user-mode dwm.exe compositor.
 *
 * Zero-copy design: each window's FRONT buffer lives in a pageable section.
 * The frame pull returns METADATA only — Z-ordered DWM_WIN descriptors naming
 * each window's surface by (SurfaceId, Generation). dwm opens the section
 * (DWMOPENSURFACE), maps it read-only, and composes straight from the mapped
 * views; a Generation change (resize/recreate) tells dwm to remap. The kernel
 * never copies pixels into the pull buffer and never composites once attached.
 *
 * Every entry is rejected for processes other than the attached compositor.
 * Win10 equivalents of this contract (ours ride NtUserCallOneParam until the
 * D3DKMT flip-model path replaces them):
 *   DWMATTACH       ~ NtUserDwmStartRedirection(TRUE/FALSE)
 *   DWMOPENSURFACE  ~ NtUserDwmGetDxSharedSurface (section vs DX alloc)
 *   DWMGETFRAME     ~ the DWM change-feed (window list + dirty regions)
 */
#pragma once

#define DWM_FRAME_MAGIC   0x334d5744u   /* 'DWM3' (CPU + DX surfaces) */
#define DWM_MAX_WINDOWS    256

/* NtUserCallOneParam routine numbers of the DWM entry points (must match
 * win32ss/include/ntuser.h, which owns the routine numbering). */
#define DWM_ROUTINE_ATTACH       0xfffe0013
#define DWM_ROUTINE_GETFRAME     0xfffe0014
#define DWM_ROUTINE_OPENSURFACE  0xfffe0016
#define DWM_ROUTINE_DXSURFACE    0xfffe0017

/*
 * Internal win32k control channel to the canonical display driver (DrvEscape).
 * NtGdiExtEscape rejects the mutating codes below; they are ReactOS-private
 * kernel plumbing, not Windows public APIs. The values spell "DWM" in the
 * high bytes:
 *   SUPPRESS_CURSOR  - the compositor draws the cursor itself, so cdd stops
 *                      drawing the hardware/software cursor while suppressed.
 *   PRESENT_STATS    - read-only present-path counters (DXGK_PRESENT_STATS in
 *                      the escape output buffer) so a test can measure how much
 *                      scan-out work a GDI/cursor operation costs.
 */
#define CDD_ESCAPE_SUPPRESS_CURSOR  0x44574D01
#define CDD_ESCAPE_PRESENT_STATS    0x44574D04

/*
 * cdd -> dxgkrnl present-path IOCTLs (kernel side of the same contract).
 *   PRESENT_DIRTY_RECT - dirty-rectangle notification: cdd draws straight
 *                        into the mapped DOD primary and records the rectangle
 *                        with dxgkrnl, which scans it out through the
 *                        miniport's DxgkDdiPresentDisplayOnly. Input is a
 *                        variable-length DXGK_PRESENT_DIRTY_RECTS_INPUT.
 *                        Individual notifications accumulate damage; FLUSH
 *                        publishes one completed GDI frame.
 */
#define IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x920, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_DXGK_PRESENT_STATS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x924, METHOD_BUFFERED, FILE_ANY_ACCESS)

/*
 * Generic display-driver -> miniport escape conduit. The buffered payload is
 * handed to the WDDM miniport's DxgkDdiEscape verbatim and rewritten in place
 * with the miniport's reply; dxgkrnl never interprets it. This is how a
 * GDI display driver (framebuf/cdd) reaches vendor GPU services -- e.g. the
 * OpenGL ICD render escapes -- without dxgkrnl learning vendor protocols.
 */
#define IOCTL_VIDEO_DXGK_GPU_ESCAPE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x925, METHOD_BUFFERED, FILE_ANY_ACCESS)
#include <pshpack4.h>

typedef struct _DXGK_PRESENT_DIRTY_RECTS_INPUT
{
    ULONG StructSize;
    ULONG Flags;
    ULONG RectCount;
    ULONG SourcePitch;
    ULONG SourceSize;
    ULONG_PTR Source;
    RECTL Rects[ANYSIZE_ARRAY];
} DXGK_PRESENT_DIRTY_RECTS_INPUT, *PDXGK_PRESENT_DIRTY_RECTS_INPUT;

/* The core present packet has room for this many source subrectangles. */
#define DXGK_PRESENT_DIRTY_MAX_RECTS 64u

#define DXGK_PRESENT_DIRTY_FLUSH   0x00000004u

/*
 * Present-path counters (CDD_ESCAPE_PRESENT_STATS / IOCTL_VIDEO_DXGK_PRESENT_STATS).
 * DirtyRectRequests counts the present requests cdd sends for drawing that
 * reached the primary; ScanoutCopies counts the shadow->scan-out copies those
 * requests produced. Both are free-running since adapter start.
 */
typedef struct _DXGK_PRESENT_STATS
{
    ULONG StructSize;          /* out: sizeof(DXGK_PRESENT_STATS) */
    ULONG DirtyRectRequests;
    ULONG ScanoutCopies;
    ULONG PendingDirtyRect;    /* 1 = a recorded rect is not scanned out yet */
    ULONG PresentCalls;
    ULONGLONG PresentTotalUs;
    ULONGLONG PresentMaxUs;
    ULONG HardwarePointerSupported;
    ULONG PointerShapeCalls;
    ULONG PointerPositionCalls;
    ULONG PointerFailures;
    ULONG PresentQueueDepth;
    ULONG PresentQueueHighWatermark;
    ULONG PresentQueued;
    ULONG PresentCompleted;
    ULONG PresentFailed;
    ULONG PresentRejected;
    ULONG PresentSynchronous;
} DXGK_PRESENT_STATS, *PDXGK_PRESENT_STATS;

/* LayerFlags bits (match winuser LWA_*). */
#define DWM_LWA_COLORKEY 0x00000001u
#define DWM_LWA_ALPHA    0x00000002u

typedef struct _DWM_WIN
{
    LONG  x, y;          /* window top-left in screen coords    */
    LONG  cx, cy;        /* backing dimensions (== window size) */
    ULONG SurfaceId;     /* kernel surface slot                  */
    ULONG Generation;    /* bumps on recreate — remap the view  */
    ULONG Stride;        /* bytes per row (cx * 4, top-down)    */
    ULONG Damaged;       /* window changed since last frame     */
    ULONG Alpha;         /* constant alpha 0-255 (255 = opaque) */
    ULONG ColorKey;      /* BGRX colorkey when LWA_COLORKEY set */
    ULONG LayerFlags;    /* DWM_LWA_* (0 = fully opaque)         */
    ULONG DxGlobalShare; /* D3DKMT shared client surface, or 0   */
    ULONG DxGeneration;  /* changes when the shared resource does */
    LUID  DxAdapterLuid;
    ULONGLONG DxUpdateId;/* latest completed GPU update          */
    LONG  DxClientX;     /* client origin inside window backing  */
    LONG  DxClientY;
    ULONG DxWidth;
    ULONG DxHeight;
    ULONG DxPitch;
    ULONG DxFormat;
} DWM_WIN, *PDWM_WIN;

typedef struct _DWM_FRAME_HEADER
{
    ULONG Magic;         /* DWM_FRAME_MAGIC                          */
    ULONG BufBytes;      /* in : total size of this buffer          */
    ULONG ScreenW;       /* out                                     */
    ULONG ScreenH;       /* out                                     */
    ULONG Count;         /* out : DWM_WIN slots filled (<= MAX)     */
    ULONG FullDamage;    /* out : whole frame must be re-presented  */
    ULONG Dirty;         /* out : 0 = nothing changed, skip present */
    ULONG WinArrayBase;  /* out : byte offset of DWM_WIN[0]         */
    LONG  DmgL, DmgT;    /* out : changed-region union, screen px   */
    LONG  DmgR, DmgB;    /*       (present only this; empty if R<=L) */
} DWM_FRAME_HEADER, *PDWM_FRAME_HEADER;

/* DWMOPENSURFACE exchange: dwm names a surface from the frame metadata; the
 * kernel opens a read-only section handle for it in dwm's process. */
typedef struct _DWM_OPEN_SURFACE
{
    ULONG  SurfaceId;    /* in  : from DWM_WIN                     */
    ULONG  Generation;   /* in  : must match the current surface   */
    HANDLE hSection;     /* out : SECTION_MAP_READ handle          */
} DWM_OPEN_SURFACE, *PDWM_OPEN_SURFACE;

/* DWMATTACH exchange. On attach the kernel returns a damage event in the
 * caller's handle table. hVblank is retained for compatibility with older
 * ReactOS dwm binaries and is always NULL. */
typedef struct _DWM_ATTACH
{
    ULONG  Attach;       /* in  : 1 attach, 0 detach               */
    HANDLE hWake;        /* out : damage wake event                */
    HANDLE hVblank;      /* out : reserved, always NULL            */
} DWM_ATTACH, *PDWM_ATTACH;

/* Runtime-private metadata stored in the D3DKMT shared resource. This is an
 * OS presentation contract, not miniport-private data: DWM and any OpenGL ICD
 * can validate the same linear client-surface description after OpenResource. */
#define DWM_DX_SURFACE_INFO_MAGIC   0x53585744u /* 'DWXS' */
#define DWM_DX_SURFACE_INFO_VERSION 1u
#define DWM_DX_FORMAT_B8G8R8A8_UNORM 87u

typedef struct _DWM_DX_SHARED_SURFACE_INFO
{
    ULONG Magic;
    ULONG Version;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    ULONG Format;
} DWM_DX_SHARED_SURFACE_INFO, *PDWM_DX_SHARED_SURFACE_INFO;

#define DWM_DX_SURFACE_REGISTER  1u
#define DWM_DX_SURFACE_ISSUE     2u
#define DWM_DX_SURFACE_UPDATE    3u
#define DWM_DX_SURFACE_CONSUMED  4u

#define DWM_DX_UPDATE_CANCEL     0x80000000u

/* Fixed-width NtUser exchange used by dwmapi, OpenGL32 and dwm.exe. Window is
 * a zero-extended HWND so the structure has one layout for native and WOW64
 * clients. REGISTER/ISSUE/UPDATE are restricted to the window owner;
 * CONSUMED is restricted to the attached compositor. */
typedef struct _DWM_DX_SURFACE_EXCHANGE
{
    ULONG StructSize;
    ULONG Action;
    ULONGLONG Window;
    LUID AdapterLuid;
    ULONG GlobalShare;
    ULONG SurfaceId;
    ULONG Generation;
    ULONG Flags;
    DWM_DX_SHARED_SURFACE_INFO Info;
    ULONGLONG ReadyEvent;
    ULONGLONG UpdateId;
    RECTL UpdateRect;
} DWM_DX_SURFACE_EXCHANGE, *PDWM_DX_SURFACE_EXCHANGE;

#include <poppack.h>

/* Fixed layout: window array right after the header; the buffer carries only
 * header + descriptors (pixels live in the per-window sections). */
#define DWM_WINARRAY_BASE  ((ULONG)sizeof(DWM_FRAME_HEADER))
#define DWM_FRAME_BYTES    (DWM_WINARRAY_BASE + DWM_MAX_WINDOWS * (ULONG)sizeof(DWM_WIN))
