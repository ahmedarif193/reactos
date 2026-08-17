/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bounded texture-framebuffer integration for the RPi5 ICD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

typedef struct _RPI5VC4_OGL_FBO_STATE RPI5VC4_OGL_FBO_STATE;
typedef RPI5VC4_OGL_FBO_STATE *PRPI5VC4_OGL_FBO_STATE;
struct gl_texture_object;

VOID
Rpi5OglMaterializeTextureClear(
    _In_ struct gl_texture_object *Texture);

BOOL
Rpi5OglRecordGraphTextureMipmap(
    _In_ struct gl_texture_object *Texture);

typedef struct _RPI5VC4_OGL_FBO_COLOR_TARGET
{
    struct gl_texture_object *Texture;
    GLubyte *Data;
    struct gl_texture_object *DepthTexture;
    GLdepth *DepthData;
    BOOL HasColor;
    BOOL HasDepth;
    ULONG Width;
    ULONG Height;
    GLenum Format;
    GLenum DepthFormat;
    GLuint Framebuffer;
    ULONG Generation;
} RPI5VC4_OGL_FBO_COLOR_TARGET, *PRPI5VC4_OGL_FBO_COLOR_TARGET;

BOOL
Rpi5OglFboInitialize(
    _Outptr_ PRPI5VC4_OGL_FBO_STATE *State,
    _In_ GLcontext *Mesa,
    _In_ GLframebuffer *DefaultBuffer);

VOID
Rpi5OglFboCleanup(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State);

GLuint
Rpi5OglFboCurrentName(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State);

BOOL
Rpi5OglFboCurrentComplete(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State);

BOOL
Rpi5OglFboValidateCurrent(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _In_z_ PCSTR Function);

BOOL
Rpi5OglFboGetColorTarget(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _Out_ PRPI5VC4_OGL_FBO_COLOR_TARGET Target);

VOID
Rpi5OglFboTextureChanged(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _In_ struct gl_texture_object *Texture);

VOID
Rpi5OglFboTextureDeleted(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State,
    _Inout_ struct gl_texture_object *Texture);

VOID
Rpi5OglFboRestoreBinding(
    _In_opt_ PRPI5VC4_OGL_FBO_STATE State);

VOID APIENTRY
Rpi5OglFboDrawBuffer(
    _In_ GLenum Mode);

VOID APIENTRY
Rpi5OglFboGetIntegerv(
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters);

VOID APIENTRY
Rpi5OglFboReadBuffer(
    _In_ GLenum Mode);

VOID APIENTRY
Rpi5OglFboGetTexImage(
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _Out_ GLvoid *Pixels);

VOID APIENTRY
Rpi5OglFboTexImage2D(
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint InternalFormat,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLint Border,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Pixels);

VOID APIENTRY
Rpi5OglFboTexSubImage2D(
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint XOffset,
    _In_ GLint YOffset,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Pixels);

PROC
Rpi5OglFboGetProcAddress(
    _In_z_ LPCSTR Name);

PRPI5VC4_OGL_FBO_STATE
Rpi5OglCurrentFboState(VOID);
