/*
 * PROJECT:     ReactOS Raspberry Pi 5 OpenGL diagnostics
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GLAD compatibility declarations for the system OpenGL 1.1 ABI
 */

#pragma once

#include <GL/gl.h>

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

#ifdef __cplusplus
extern "C" {
#endif

int gladLoaderLoadGL(void);
void gladLoaderUnloadGL(void);

#ifdef __cplusplus
}
#endif
