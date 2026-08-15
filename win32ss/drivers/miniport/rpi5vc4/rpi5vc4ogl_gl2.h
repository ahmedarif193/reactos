/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bounded OpenGL 2 shader-program integration for the RPi5 ICD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#define RPI5VC4_OGL_GL2_MAX_DRAW_VERTICES 4u

typedef struct _RPI5VC4_OGL_GL2_STATE RPI5VC4_OGL_GL2_STATE;
typedef RPI5VC4_OGL_GL2_STATE *PRPI5VC4_OGL_GL2_STATE;

typedef struct _RPI5VC4_OGL_GL2_VERTEX
{
    GLfloat Position[4];
    GLubyte Color[4];
} RPI5VC4_OGL_GL2_VERTEX, *PRPI5VC4_OGL_GL2_VERTEX;

typedef enum _RPI5VC4_OGL_GL2_DRAW_RESULT
{
    Rpi5OglGl2DrawNotApplicable,
    Rpi5OglGl2DrawRejected,
    Rpi5OglGl2DrawReady
} RPI5VC4_OGL_GL2_DRAW_RESULT;

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

RPI5VC4_OGL_GL2_DRAW_RESULT
Rpi5OglGl2BuildPrimitive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count,
    _Out_writes_(RPI5VC4_OGL_GL2_MAX_DRAW_VERTICES)
        RPI5VC4_OGL_GL2_VERTEX *Vertices);

PROC
Rpi5OglGl2GetProcAddress(
    _In_z_ LPCSTR Name);

PRPI5VC4_OGL_GL2_STATE
Rpi5OglCurrentGl2State(VOID);
