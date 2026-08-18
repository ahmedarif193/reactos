/*
 * PROJECT:     ReactOS Raspberry Pi 5 OpenGL diagnostics
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minimal WGL extension loader ABI used by upstream wglgears
 */

#pragma once

#include <windows.h>

#define WGL_DRAW_TO_WINDOW_ARB            0x2001
#define WGL_SUPPORT_OPENGL_ARB            0x2010
#define WGL_DOUBLE_BUFFER_ARB             0x2011
#define WGL_COLOR_BITS_ARB                0x2014
#define WGL_DEPTH_BITS_ARB                0x2022
#define WGL_SAMPLE_BUFFERS_ARB            0x2041
#define WGL_SAMPLES_ARB                   0x2042
#define WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB  0x20A9

typedef BOOL (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(
    HDC DeviceContext,
    const int *IntegerAttributes,
    const FLOAT *FloatAttributes,
    UINT MaximumFormats,
    int *Formats,
    UINT *FormatCount);

typedef int (WINAPI *PFNWGLGETSWAPINTERVALEXTPROC)(void);

#ifdef __cplusplus
extern "C" {
#endif

extern int GLAD_WGL_ARB_create_context;
extern int GLAD_WGL_EXT_swap_control;
extern PFNWGLCHOOSEPIXELFORMATARBPROC glad_wglChoosePixelFormatARB;
extern PFNWGLGETSWAPINTERVALEXTPROC glad_wglGetSwapIntervalEXT;

#define wglChoosePixelFormatARB glad_wglChoosePixelFormatARB
#define wglGetSwapIntervalEXT glad_wglGetSwapIntervalEXT

int gladLoaderLoadWGL(HDC DeviceContext);

#ifdef __cplusplus
}
#endif
