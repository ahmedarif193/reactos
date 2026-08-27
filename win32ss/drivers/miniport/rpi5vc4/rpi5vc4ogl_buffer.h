/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpenGL buffer-object storage for the RPi5 ICD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#define RPI5VC4_OGL_MAX_TRANSFORM_FEEDBACK_BUFFERS 4
#define RPI5VC4_OGL_TRANSFORM_FEEDBACK_ALIGNMENT   4

typedef struct _RPI5VC4_OGL_BUFFER_STATE RPI5VC4_OGL_BUFFER_STATE;
typedef RPI5VC4_OGL_BUFFER_STATE *PRPI5VC4_OGL_BUFFER_STATE;

BOOL
Rpi5OglBufferInitialize(
    _Outptr_ PRPI5VC4_OGL_BUFFER_STATE *State,
    _In_ GLcontext *Mesa);

VOID
Rpi5OglBufferCleanup(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State);

GLuint
Rpi5OglBufferCurrentName(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum Target);

VOID
Rpi5OglBufferRestoreElementArrayBinding(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLuint Name);

BOOL
Rpi5OglBufferResolveRange(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLuint Name,
    _In_opt_ const GLvoid *Pointer,
    _In_ SIZE_T Bytes,
    _Out_ const BYTE **Data);

BOOL
Rpi5OglBufferReadElementIndices(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Indices,
    _In_ GLsizei Count,
    _Out_writes_(Count) GLuint *Values,
    _Out_opt_ GLuint *MaximumIndex);

BOOL
Rpi5OglBufferGetIntegerv(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters);

BOOL
Rpi5OglBufferGetIntegeri(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLenum ParameterName,
    _In_ GLuint Index,
    _Out_ GLint *Parameters);

BOOL
Rpi5OglBufferWriteTransformFeedback(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLuint Index,
    _In_reads_bytes_(Bytes) const VOID *Data,
    _In_ SIZE_T Bytes,
    _Inout_ PSIZE_T Position);

BOOL
Rpi5OglBufferCanWriteTransformFeedback(
    _In_opt_ PRPI5VC4_OGL_BUFFER_STATE State,
    _In_ GLuint Index,
    _In_ SIZE_T Bytes,
    _In_ SIZE_T Position);

PROC
Rpi5OglBufferGetProcAddress(
    _In_z_ LPCSTR Name);

PRPI5VC4_OGL_BUFFER_STATE
Rpi5OglCurrentBufferState(VOID);
