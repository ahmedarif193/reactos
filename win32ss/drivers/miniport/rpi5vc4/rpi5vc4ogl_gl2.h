/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bounded OpenGL 2 shader-program integration for the RPi5 ICD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

typedef struct _RPI5VC4_OGL_GL2_STATE RPI5VC4_OGL_GL2_STATE;
typedef RPI5VC4_OGL_GL2_STATE *PRPI5VC4_OGL_GL2_STATE;

BOOL
Rpi5OglGl2Initialize(
    _Outptr_ PRPI5VC4_OGL_GL2_STATE *State,
    _In_ GLcontext *Mesa);

VOID
Rpi5OglGl2Cleanup(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

BOOL
Rpi5OglGl2ProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

GLuint
Rpi5OglGl2CurrentProgramName(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

PROC
Rpi5OglGl2GetProcAddress(
    _In_z_ LPCSTR Name);

PRPI5VC4_OGL_GL2_STATE
Rpi5OglCurrentGl2State(VOID);
