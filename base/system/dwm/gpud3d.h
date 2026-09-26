/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D compositor backend
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <windows.h>
#include <reactos/dwmframe.h>
#include "gpucomp.h"

#ifdef __cplusplus
extern "C" {
#endif

BOOL DwmD3dInitialize(LONG Width, LONG Height);
BOOL DwmD3dIsActive(void);
const char *DwmD3dRendererName(void);
void DwmD3dShutdown(void);
void DwmD3dScene(const DWM_WIN *Windows, ULONG Count, const RECTL *BlurRects,
                 ULONG BlurRectCount, LONG OriginX, LONG OriginY,
                 BOOL RefreshBackdrop, ULONG BlurRadius, const RECT *ShadowMargins);
void DwmD3dPrepareWindow(const DWM_WIN *Window, ULONG Index);
BOOL DwmD3dNeedsSurfacePixels(const DWM_WIN *Window);
BOOL DwmD3dBegin(ULONG BackdropColor, const BYTE *BackdropPixels,
                 BOOL RefreshBackdrop, const RECT *Damage, ULONG DamageCount);
BOOL DwmD3dWindow(const DWM_WIN *Window, const BYTE *Pixels, LONG OriginX, LONG OriginY);
BOOL DwmD3dBlurRect(const RECT *Rect, ULONG Radius);
BOOL DwmD3dBlurWindow(const DWM_WIN *Window, const RECTL *Rectangles,
                      LONG OriginX, LONG OriginY, ULONG Radius);
BOOL DwmD3dShadow(const RECT *Bounds, LONGLONG X, LONGLONG Y, LONG Width,
                  LONG Height, LONG Offset, LONG WideExtent, BOOL Active,
                  ULONG WideOpacity, ULONG TightOpacity, ULONG WindowAlpha);
void DwmD3dBlurStats(ULONGLONG *Filtered, ULONGLONG *Reused);
BOOL DwmD3dClientCopiesRetired(void);
DWM_GPU_RESULT DwmD3dEnd(void);
DWM_GPU_RESULT DwmD3dCheckOutput(void);

#ifdef __cplusplus
}
#endif
