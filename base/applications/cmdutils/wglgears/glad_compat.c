/*
 * PROJECT:     ReactOS Raspberry Pi 5 OpenGL diagnostics
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Load the optional WGL entry points used by upstream wglgears
 */

#include <windows.h>
#include "glad/gl.h"
#include "glad/wgl.h"

int GLAD_WGL_ARB_create_context;
int GLAD_WGL_EXT_swap_control;
PFNWGLCHOOSEPIXELFORMATARBPROC glad_wglChoosePixelFormatARB;
PFNWGLGETSWAPINTERVALEXTPROC glad_wglGetSwapIntervalEXT;

int
gladLoaderLoadWGL(
    HDC DeviceContext)
{
    UNREFERENCED_PARAMETER(DeviceContext);

    glad_wglChoosePixelFormatARB =
        (PFNWGLCHOOSEPIXELFORMATARBPROC)
            wglGetProcAddress("wglChoosePixelFormatARB");
    glad_wglGetSwapIntervalEXT =
        (PFNWGLGETSWAPINTERVALEXTPROC)
            wglGetProcAddress("wglGetSwapIntervalEXT");
    GLAD_WGL_ARB_create_context =
        glad_wglChoosePixelFormatARB != NULL;
    GLAD_WGL_EXT_swap_control = glad_wglGetSwapIntervalEXT != NULL;
    return 1;
}

int
gladLoaderLoadGL(void)
{
    return 1;
}

void
gladLoaderUnloadGL(void)
{
}
