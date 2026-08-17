/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bounded OpenGL 2 shader-program integration for the RPi5 ICD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <reactos/rpi5vc4_xpdm.h>

#include "rpi5vc4ogl_buffer.h"

#define RPI5VC4_OGL_GL2_MAX_DRAW_VERTICES 4u

typedef struct _RPI5VC4_OGL_GL2_STATE RPI5VC4_OGL_GL2_STATE;
typedef RPI5VC4_OGL_GL2_STATE *PRPI5VC4_OGL_GL2_STATE;
struct gl_texture_object;

typedef struct _RPI5VC4_OGL_GL2_TEXTURE_INFO
{
    struct gl_texture_object *Texture;
    const GLubyte *Data;
    ULONG Width;
    ULONG Height;
    GLenum Format;
    BOOL LinearFilter;
    BOOL MipmapFilter;
    GLenum WrapS;
    GLenum WrapT;
    ULONG LevelCount;
    ULONG Serial;
} RPI5VC4_OGL_GL2_TEXTURE_INFO,
  *PRPI5VC4_OGL_GL2_TEXTURE_INFO;

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
    _In_ GLcontext *Mesa,
    _In_ PRPI5VC4_OGL_BUFFER_STATE BufferState);

VOID
Rpi5OglGl2Cleanup(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

BOOL
Rpi5OglGl2ProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

BOOL
Rpi5OglGl2DesktopProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

BOOL
Rpi5OglGl2TerrainProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

BOOL
Rpi5OglGl2DepthProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

BOOL
Rpi5OglGl2ShadowProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

BOOL
Rpi5OglGl2BuildDiffuseProgramActive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

ULONG
Rpi5OglGl2TerrainMode(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

GLuint
Rpi5OglGl2CurrentProgramName(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

ULONG
Rpi5OglGl2BatchFlags(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

ULONG
Rpi5OglGl2IdeasMode(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

ULONG
Rpi5OglGl2JellyfishMode(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

BOOL
Rpi5OglGl2PreservesDestinationAlphaBlend(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

VOID
Rpi5OglGl2BlendFuncChanged(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

BOOL
Rpi5OglGl2GetNormalMatrix(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_writes_(RPI5VC4_V3D_NORMAL_MATRIX_WORDS) PULONG Matrix);

BOOL
Rpi5OglGl2GetIdeasUniforms(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_writes_(RPI5VC4_V3D_IDEAS_UNIFORM_WORDS) PULONG Uniforms);

BOOL
Rpi5OglGl2GetJellyfishUniforms(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_writes_(RPI5VC4_V3D_JELLYFISH_UNIFORM_WORDS) PULONG Uniforms);

BOOL
Rpi5OglGl2GetTerrainUniforms(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_writes_(RPI5VC4_V3D_TERRAIN_UNIFORM_WORDS) PULONG Uniforms);

RPI5VC4_OGL_GL2_DRAW_RESULT
Rpi5OglGl2BuildPrimitive(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count,
    _Out_writes_(RPI5VC4_OGL_GL2_MAX_DRAW_VERTICES)
        RPI5VC4_OGL_GL2_VERTEX *Vertices);

RPI5VC4_OGL_GL2_DRAW_RESULT
Rpi5OglGl2PrepareBatchDraw(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count);

BOOL
Rpi5OglGl2BuildBatch(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count,
    _In_ GLenum FrontFace,
    _In_ ULONG CullBits,
    _Out_writes_(OutputCapacity) PRPI5VC4_V3D_VERTEX Output,
    _Out_writes_opt_(OutputCapacity)
        PRPI5VC4_V3D_TEXCOORD HeightTexCoords,
    _In_ ULONG OutputCapacity,
    _Out_ PULONG OutputVertexCount,
    _Out_ PULONG OutputTriangleCount);

BOOL
Rpi5OglGl2BuildIndexedBatch(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Mode,
    _In_reads_(Count) const GLuint *Indices,
    _In_ GLsizei Count,
    _In_ GLuint MaximumIndex,
    _In_ GLenum FrontFace,
    _In_ ULONG CullBits,
    _Out_writes_(OutputCapacity) PRPI5VC4_V3D_VERTEX Output,
    _Out_writes_opt_(OutputCapacity)
        PRPI5VC4_V3D_TEXCOORD HeightTexCoords,
    _In_ ULONG OutputCapacity,
    _Out_ PULONG OutputVertexCount,
    _Out_ PULONG OutputTriangleCount);

BOOL
Rpi5OglGl2PrepareTexture(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ HDC Hdc,
    _Out_ PBOOL Textured,
    _Out_ PBOOL LinearFilter,
    _Out_ PBOOL MipmapFilter,
    _Out_ PULONG TextureGeneration,
    _Out_ PULONG TextureGeneration1);

struct gl_texture_object *
Rpi5OglGl2TextureForUnit(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ ULONG TextureUnit);

VOID
Rpi5OglGl2TextureBound(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLenum Target,
    _In_opt_ struct gl_texture_object *Texture);

BOOL
Rpi5OglGl2GetBoundDesktopTexture(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _Out_ PRPI5VC4_OGL_GL2_TEXTURE_INFO TextureInfo);

BOOL
Rpi5OglGl2GetTerrainTexture(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ ULONG Sampler,
    _Out_ PRPI5VC4_OGL_GL2_TEXTURE_INFO TextureInfo);

BOOL
Rpi5OglGl2BuildTerrainGrid(
    _In_ PRPI5VC4_OGL_GL2_STATE State,
    _In_ GLint First,
    _In_ GLsizei Count,
    _Out_writes_(OutputCapacity) PRPI5VC4_V3D_TERRAIN_VERTEX Output,
    _In_ ULONG OutputCapacity);

VOID
Rpi5OglGl2TextureChanged(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_opt_ struct gl_texture_object *Texture);

VOID
Rpi5OglGl2TextureDeleted(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State,
    _In_opt_ struct gl_texture_object *Texture);

VOID
Rpi5OglGl2InvalidateTextureUpload(
    _In_opt_ PRPI5VC4_OGL_GL2_STATE State);

PROC
Rpi5OglGl2GetProcAddress(
    _In_z_ LPCSTR Name);

PRPI5VC4_OGL_GL2_STATE
Rpi5OglCurrentGl2State(VOID);
