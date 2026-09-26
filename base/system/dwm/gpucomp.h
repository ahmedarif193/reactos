/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GPU-accelerated compositor effects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <windows.h>

/*
 * The compositor keeps its software path as the always-available baseline.
 * The standalone blur uses an OpenGL ICD. Full-frame composition can also
 * use a native Direct3D 11 device on the output adapter. Each backend reports
 * failed imports and rendering operations to the caller; it never disguises
 * a CPU texture upload as shared GPU composition.
 */

/*
 * Probes the adapter behind hdcScreen and brings up a rendering context.
 * Returns FALSE when no usable GPU is present, which is the normal result on
 * a software-only display stack and is not an error.
 */
BOOL DwmGpuInitialize(HDC hdcScreen);

/* Whether the hardware path is available for this session. */
BOOL DwmGpuIsActive(void);

/* Human-readable adapter description, or NULL when inactive. Never freed. */
const char *DwmGpuRendererName(void);

/*
 * Blurs Rect in place inside a Width x Height BGRA composition buffer, using
 * a separable Gaussian of the given radius. Returns FALSE if the hardware
 * path cannot service this request, leaving Composition untouched so the
 * caller can run the software blur instead.
 */
BOOL DwmGpuBlurRect(ULONG *Composition, LONG Width, LONG Height,
                    const RECT *Rect, ULONG Radius);

void DwmGpuShutdown(void);

/*
 * Full-frame composition on the GPU. Windows use shared allocation imports
 * where supported. The D3D backend uploads completed GDI dirty bounds from
 * section-backed windows; publication IDs preserve unchanged GPU textures.
 * Moving an unchanged window does not upload it.
 * Wallpaper has a separate cached upload. Blur and final presentation remain
 * on the GPU. Calls report failure to the compositor; runtime recovery policy
 * is owned by that caller.
 */
struct _DWM_WIN;

typedef enum _DWM_GPU_RESULT
{
    DWM_GPU_FAILED,
    DWM_GPU_COMPLETE,
    DWM_GPU_DEFERRED,
    DWM_GPU_RETRY
} DWM_GPU_RESULT;

BOOL DwmGpuComposeInitialize(LONG Width, LONG Height);
BOOL DwmGpuComposeIsActive(void);

/* Starts with cached wallpaper. NULL damage requests a full redraw; several
 * rectangles may be repaired separately. Buffer preservation follows the
 * selected backend's actual swap contract. */
BOOL DwmGpuComposeBegin(ULONG BackdropColor, const BYTE *BackdropPixels,
                        BOOL RefreshBackdrop, const RECT *Damage, ULONG DamageCount);

/* Describe the scene before Begin so damage can include blur dependencies. */
void DwmGpuComposeScene(const struct _DWM_WIN *Windows, ULONG Count,
                        const RECTL *BlurRects, ULONG BlurRectCount,
                        LONG OriginX, LONG OriginY, BOOL RefreshBackdrop,
                        ULONG BlurRadius, const RECT *ShadowMargins);
void DwmGpuComposePrepareWindow(const struct _DWM_WIN *Window, ULONG Index);
void DwmGpuComposeBlurStats(ULONGLONG *Filtered, ULONGLONG *Reused);
BOOL DwmGpuComposeNeedsSurfacePixels(const struct _DWM_WIN *Window);

/*
 * Draws the window base and optional shared client content. Supply the raw
 * GDI FRONT section pixels only when NeedsSurfacePixels requests an update.
 */
BOOL DwmGpuComposeWindow(const struct _DWM_WIN *Window, const BYTE *Pixels,
                         LONG OriginX, LONG OriginY);

/* Blurs the frame behind Rect in place, on the GPU, with no readback. */
BOOL DwmGpuComposeBlurRect(const RECT *Rect, ULONG Radius);

/* Apply an explicit blur-behind region using a single GPU backdrop capture. */
BOOL DwmGpuComposeBlurWindow(const struct _DWM_WIN *Window,
                             const RECTL *Rectangles,
                             LONG OriginX, LONG OriginY, ULONG Radius);

BOOL DwmGpuComposeShadow(const RECT *Bounds, LONGLONG X, LONGLONG Y,
                          LONG Width, LONG Height, LONG Offset, LONG WideExtent,
                          BOOL Active, ULONG WideOpacity, ULONG TightOpacity,
                          ULONG WindowAlpha);

/* TRUE when the frame's copies of new client publications have completed on
 * the GPU, so they may be acknowledged before the frame is presented. */
BOOL DwmGpuComposeClientCopiesRetired(void);

/* COMPLETE means presentation was accepted. DEFERRED means the output is
 * temporarily unavailable and GPU reads have completed, so client copies
 * may be acknowledged without counting the frame as presented. */
DWM_GPU_RESULT DwmGpuComposeEnd(void);

/* Discards an unpresented frame. A stale GDI FRONT import may be retried
 * because a window can disappear between GETFRAME and OpenResource. */
DWM_GPU_RESULT DwmGpuComposeAbort(void);

/* Tests a deferred output without rendering, presenting or rotating buffers. */
DWM_GPU_RESULT DwmGpuComposeCheckOutput(void);

void DwmGpuComposeShutdown(void);
