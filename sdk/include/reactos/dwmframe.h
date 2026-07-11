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
 *   DWMPRESENTSYNC  ~ DWM's present serialization against the scanout
 */
#pragma once

#define DWM_FRAME_MAGIC   0x324d5744u   /* 'DWM2' (metadata-only frames) */
#define DWM_MAX_WINDOWS    256

/*
 * DWM control channel to the canonical display driver (DrvEscape/ExtEscape).
 * The values are ours (they spell "DWM" in the high bytes) and are matched by
 * the d3dkmt dwm apitest:
 *   SUPPRESS_CURSOR  - the compositor draws the cursor itself, so cdd stops
 *                      drawing the hardware/software cursor while suppressed.
 *   COMPOSITION_SYNC - present/vblank acknowledge: cdd flushes the composed
 *                      frame to the WDDM scan-out and acks so the compositor
 *                      can pace frames.
 *   REGISTER_VBLANK  - win32k hands dxgkrnl a referenced PKEVENT (ULONGLONG
 *                      payload, 0 unregisters); the present timer signals it
 *                      every scanout period for dwm frame pacing.
 */
#define CDD_ESCAPE_SUPPRESS_CURSOR  0x44574D01
#define CDD_ESCAPE_COMPOSITION_SYNC 0x44574D02
#define CDD_ESCAPE_REGISTER_VBLANK  0x44574D03

#include <pshpack4.h>

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

/* DWMATTACH exchange. On attach the kernel returns two events in the caller's
 * handle table: hWake signals pending damage (dwm blocks on it when idle);
 * hVblank is signaled by the display path every scanout period (dwm paces
 * composed frames on it). Either may be NULL — dwm falls back to timeouts. */
typedef struct _DWM_ATTACH
{
    ULONG  Attach;       /* in  : 1 attach, 0 detach               */
    HANDLE hWake;        /* out : damage wake event                */
    HANDLE hVblank;      /* out : scanout pacing event             */
} DWM_ATTACH, *PDWM_ATTACH;

#include <poppack.h>

/* Fixed layout: window array right after the header; the buffer carries only
 * header + descriptors (pixels live in the per-window sections). */
#define DWM_WINARRAY_BASE  ((ULONG)sizeof(DWM_FRAME_HEADER))
#define DWM_FRAME_BYTES    (DWM_WINARRAY_BASE + DWM_MAX_WINDOWS * (ULONG)sizeof(DWM_WIN))
