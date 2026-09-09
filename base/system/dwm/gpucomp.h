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
 * These entry points are the optional hardware path: they engage only on a
 * display adapter that publishes a real OpenGL ICD able to run shaders, and
 * every one of them reports failure rather than degrading, so the caller can
 * fall back to the software routine for that frame without any visible
 * difference beyond speed.
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
 * Full-frame composition on the GPU. Dynamic windows use shared allocation
 * imports; completed publication IDs invalidate derived GPU textures only
 * when their content changes. Moving an unchanged window does not upload it.
 * Wallpaper has a separate cached upload. Blur and final presentation remain
 * on the GPU. Calls report failure to the compositor; runtime recovery policy
 * is owned by that caller.
 */
struct _DWM_WIN;

BOOL DwmGpuComposeInitialize(LONG Width, LONG Height);
BOOL DwmGpuComposeIsActive(void);

/* Starts with cached wallpaper. NULL damage requests a full redraw. Buffer
 * preservation is queried from WGL; unknown swap methods redraw in full. */
BOOL DwmGpuComposeBegin(ULONG BackdropColor, const BYTE *BackdropPixels,
                        BOOL RefreshBackdrop, const RECT *Damage);

/* Describe the scene before Begin so damage can include blur dependencies. */
void DwmGpuComposeScene(const struct _DWM_WIN *Windows, ULONG Count,
                        const RECTL *BlurRects, ULONG BlurRectCount,
                        LONG OriginX, LONG OriginY, BOOL RefreshBackdrop,
                        ULONG BlurRadius, const RECT *ShadowMargins);
void DwmGpuComposePrepareWindow(const struct _DWM_WIN *Window, ULONG Index);
void DwmGpuComposeBlurStats(ULONGLONG *Filtered, ULONGLONG *Reused);

/*
 * Draws shared base and optional shared client content. Pixels is NULL for
 * dynamic windows; only the reserved wallpaper surface accepts CPU pixels.
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

/* Presents the frame. */
BOOL DwmGpuComposeEnd(void);

void DwmGpuComposeShutdown(void);
