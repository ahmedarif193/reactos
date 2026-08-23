/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpenGL 1.1 ICD with bounded V3D clear and triangle paths
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * Mesa 2.5 supplies the OpenGL 1.1 state tracker and software compatibility
 * rasterizer. Full, unmasked color clears and eligible first triangles are
 * replaced with kernel-built V3D 7.1 jobs. No MMIO address or arbitrary
 * command list crosses this ICD boundary.
 */

#include <stddef.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <string.h>

#include <context.h>
#include <blend.h>
#include <depth.h>
#include <dlist.h>
#include <light.h>
#include <macros.h>
#include <matrix.h>
#include <misc.h>
#include <shade.h>
#include <triangle.h>
#include <vb.h>
#include <xform.h>
#include <reactos/rpi5vc4_xpdm.h>

#include "rpi5vc4ogl_buffer.h"
#include "rpi5vc4ogl_fbo.h"
#include "rpi5vc4ogl_gl2.h"

extern void APIENTRY _mesa_CopyTexSubImage3D(
    GLenum Target,
    GLint Level,
    GLint XOffset,
    GLint YOffset,
    GLint ZOffset,
    GLint X,
    GLint Y,
    GLsizei Width,
    GLsizei Height);
extern void APIENTRY _mesa_TexImage3D(
    GLenum Target,
    GLint Level,
    GLint InternalFormat,
    GLsizei Width,
    GLsizei Height,
    GLsizei Depth,
    GLint Border,
    GLenum Format,
    GLenum Type,
    const GLvoid *Pixels);
extern void APIENTRY _mesa_TexSubImage3D(
    GLenum Target,
    GLint Level,
    GLint XOffset,
    GLint YOffset,
    GLint ZOffset,
    GLsizei Width,
    GLsizei Height,
    GLsizei Depth,
    GLenum Format,
    GLenum Type,
    const GLvoid *Pixels);

#define RPI5VC4_OGL_CONTEXT_SIGNATURE '1GlR'
#define RPI5VC4_OPENGL_ICD_DRIVER_VERSION 1
#define RPI5VC4_OPENGL_ENTRY_COUNT 336
#define RPI5VC4_OPENGL_PIXEL_FORMAT_COUNT 1
#define RPI5VC4_OPENGL_END_LIST_INDEX 1
#define RPI5VC4_OPENGL_CALL_LIST_INDEX 2
#define RPI5VC4_OPENGL_DELETE_LISTS_INDEX 4
#define RPI5VC4_OPENGL_BLEND_FUNC_INDEX 241
#define RPI5VC4_OPENGL_TEX_IMAGE_2D_INDEX 183
#define RPI5VC4_OPENGL_DRAW_BUFFER_INDEX 202
#define RPI5VC4_OPENGL_CLEAR_INDEX 203
#define RPI5VC4_OPENGL_READ_BUFFER_INDEX 254
#define RPI5VC4_OPENGL_GET_BOOLEANV_INDEX 258
#define RPI5VC4_OPENGL_GET_DOUBLEV_INDEX 260
#define RPI5VC4_OPENGL_GET_FLOATV_INDEX 262
#define RPI5VC4_OPENGL_GET_INTEGERV_INDEX 263
#define RPI5VC4_OPENGL_GET_TEX_IMAGE_INDEX 281
#define RPI5VC4_OPENGL_DRAW_ARRAYS_INDEX 310
#define RPI5VC4_OPENGL_DRAW_ELEMENTS_INDEX 311
#define RPI5VC4_OPENGL_TEX_SUB_IMAGE_2D_INDEX 333
#define RPI5VC4_OGL_MAX_DEFERRED_CLEARS 16

DECLARE_HANDLE(DHGLRC);

typedef struct _RPI5VC4_OGL_PROC_TABLE
{
    INT EntryCount;
    PROC Entries[RPI5VC4_OPENGL_ENTRY_COUNT];
} RPI5VC4_OGL_PROC_TABLE, *PRPI5VC4_OGL_PROC_TABLE;

typedef VOID (APIENTRY *PFN_SET_CURRENT_VALUE)(PVOID Value);
typedef PVOID (APIENTRY *PFN_GET_CURRENT_VALUE)(VOID);

typedef struct _RPI5VC4_OGL_LIST_VERTEX
{
    GLfloat Position[4];
    GLfloat Normal[3];
} RPI5VC4_OGL_LIST_VERTEX, *PRPI5VC4_OGL_LIST_VERTEX;

typedef struct _RPI5VC4_OGL_LIST_TRIANGLE
{
    ULONG Vertex[3];
    ULONG ProvokingVertex;
    GLenum ShadeModel;
} RPI5VC4_OGL_LIST_TRIANGLE, *PRPI5VC4_OGL_LIST_TRIANGLE;

typedef struct _RPI5VC4_OGL_CACHED_LIST
{
    struct _RPI5VC4_OGL_CACHED_LIST *Next;
    GLuint Name;
    GLfloat Material[4];
    GLfloat FinalNormal[3];
    GLenum FinalShadeModel;
    ULONG ObjectVertexCount;
    ULONG ObjectVertexCapacity;
    ULONG PositionCount;
    ULONG NormalCount;
    ULONG TriangleCount;
    ULONG TriangleCapacity;
    GLfloat (*ObjectPosition)[4];
    GLfloat (*ObjectNormal)[3];
    PULONG PositionMap;
    PULONG NormalMap;
    GLfloat (*UnitObjectNormal)[3];
    PRPI5VC4_OGL_LIST_TRIANGLE Triangles;
    GLfloat (*ClipPosition)[4];
    GLfloat (*NdcPosition)[2];
    GLfloat (*EyeNormal)[3];
    GLubyte (*VertexColor)[4];
    ULONG (*VertexColorWord)[4];
    PRPI5VC4_V3D_VERTEX OutputVertices;
} RPI5VC4_OGL_CACHED_LIST, *PRPI5VC4_OGL_CACHED_LIST;

typedef struct _RPI5VC4_OGL_LIST_BUILDER
{
    PRPI5VC4_OGL_CACHED_LIST List;
    BOOL InsidePrimitive;
    BOOL MaterialDefined;
    BOOL NormalDefined;
    BOOL ShadeModelDefined;
    GLenum PrimitiveMode;
    GLenum CurrentShadeModel;
    GLfloat CurrentNormal[3];
    PRPI5VC4_OGL_LIST_VERTEX PrimitiveVertices;
    ULONG PrimitiveVertexCount;
    ULONG PrimitiveVertexCapacity;
} RPI5VC4_OGL_LIST_BUILDER, *PRPI5VC4_OGL_LIST_BUILDER;

typedef struct _RPI5VC4_OGL_GRAPH_RESOURCE
{
    struct gl_texture_object *Texture;
    const GLubyte *Data;
    ULONG Width;
    ULONG Height;
    GLenum Format;
    ULONG Serial;
    ULONG LevelCount;
    BOOL InitialData;
    BOOL Produced;
} RPI5VC4_OGL_GRAPH_RESOURCE, *PRPI5VC4_OGL_GRAPH_RESOURCE;

typedef struct _RPI5VC4_OGL_DEFERRED_CLEAR
{
    struct gl_texture_object *Texture;
    PULONG Pixels;
    GLubyte *TextureData;
    ULONG Width;
    ULONG Height;
    GLenum TextureFormat;
    ULONG Color;
    BOOL Active;
} RPI5VC4_OGL_DEFERRED_CLEAR, *PRPI5VC4_OGL_DEFERRED_CLEAR;

typedef enum _RPI5VC4_OGL_GRAPH_DRAW_RESULT
{
    Rpi5OglGraphDrawNotHandled,
    Rpi5OglGraphDrawHandled,
    Rpi5OglGraphDrawFailed
} RPI5VC4_OGL_GRAPH_DRAW_RESULT;

typedef struct _RPI5VC4_OGL_CONTEXT
{
    ULONG Signature;
    HDC Hdc;
    GLvisual *Visual;
    GLframebuffer *FrameBuffer;
    GLcontext *MesaContext;
    PULONG BackBuffer;
    ULONG Width;
    ULONG Height;
    ULONG Stride;
    ULONG ClearColor;
    ULONG CurrentColor;
    GLenum BufferMode;
    BOOL HardwareClearFresh;
    ULONG HardwareClearColor;
    GLuint HardwareClearFramebuffer;
    ULONG HardwareClearGeneration;
    RPI5VC4_OGL_DEFERRED_CLEAR
        DeferredClears[RPI5VC4_OGL_MAX_DEFERRED_CLEARS];
    ULONG DeferredClearCount;
    PRPI5VC4_V3D_BATCH_REQUEST BatchRequest;
    PRPI5VC4_V3D_BATCH_RESULT BatchResult;
    ULONG BatchResultSize;
    ULONG BatchVertexCount;
    ULONG BatchProgramTriangleCount;
    GLuint BatchProgramName;
    ULONG BatchProgramFlags;
    BOOL BatchTextured;
    BOOL BatchTextureLinear;
    BOOL BatchTextureMipmap;
    ULONG BatchTextureGeneration;
    ULONG BatchTextureGeneration1;
    GLenum BatchDepthFunc;
    BOOL BatchBlendEnabled;
    BOOL BatchPreserveDestinationAlphaBlend;
    BOOL BatchDesktop2D;
    BOOL HardwareBatchActive;
    BOOL BatchDirectPresented;
    BOOL DesktopGraphActive;
    BOOL DesktopGraphReadbackPending;
    ULONG DesktopGraphResourceCount;
    ULONG DesktopGraphPassCount;
    ULONG DesktopGraphReadbackResource;
    ULONG DesktopGraphCacheId;
    ULONG DesktopGraphCacheSignature;
    RPI5VC4_OGL_GRAPH_RESOURCE
        DesktopGraphResources[RPI5VC4_V3D_GRAPH_MAX_RESOURCES];
    RPI5VC4_V3D_GRAPH_PASS
        DesktopGraphPasses[RPI5VC4_V3D_GRAPH_MAX_PASSES];
    BOOL TerrainGraphActive;
    ULONG TerrainGraphSetupPassCount;
    ULONG TerrainGraphExpectedMode;
    PRPI5VC4_V3D_TERRAIN_VERTEX TerrainVertices;
    PRPI5VC4_OGL_CACHED_LIST CachedLists;
    PRPI5VC4_OGL_BUFFER_STATE BufferState;
    PRPI5VC4_OGL_FBO_STATE FboState;
    PRPI5VC4_OGL_GL2_STATE Gl2State;
    RPI5VC4_OGL_STATS Stats;
} RPI5VC4_OGL_CONTEXT, *PRPI5VC4_OGL_CONTEXT;

static PFN_SET_CURRENT_VALUE Rpi5OglSetCurrentValue;
static PFN_GET_CURRENT_VALUE Rpi5OglGetCurrentValue;
static RPI5VC4_OGL_PROC_TABLE Rpi5OglProcTable;

#define USE_GL_FUNC(Name, Prototype, Arguments, Offset, Stack) \
    extern void APIENTRY _mesa_##Name Prototype;
#define USE_GL_FUNC_RET(Name, ReturnType, Prototype, Arguments, Offset, Stack) \
    extern ReturnType APIENTRY _mesa_##Name Prototype;
#include "glfuncs.h"
#undef USE_GL_FUNC_RET
#undef USE_GL_FUNC

static const PROC Rpi5OglDispatchEntries[] =
{
#define USE_GL_FUNC(Name, Prototype, Arguments, Offset, Stack) \
    (PROC)_mesa_##Name,
#define USE_GL_FUNC_RET(Name, ReturnType, Prototype, Arguments, Offset, Stack) \
    (PROC)_mesa_##Name,
#include "glfuncs.h"
#undef USE_GL_FUNC_RET
#undef USE_GL_FUNC
};

C_ASSERT(RTL_NUMBER_OF(Rpi5OglDispatchEntries) ==
         RPI5VC4_OPENGL_ENTRY_COUNT);
static BOOL
Rpi5OglSubmitBatch(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ BOOL DirectPresentAllowed);

static VOID
Rpi5OglResetDesktopGraph(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context);

static LONG
Rpi5OglFindDesktopGraphResource(
    _In_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ struct gl_texture_object *Texture);

static RPI5VC4_OGL_GRAPH_DRAW_RESULT
Rpi5OglRecordDesktopGraphDraw(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count);

static RPI5VC4_OGL_GRAPH_DRAW_RESULT
Rpi5OglRecordTerrainGraphDraw(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count);

static BOOL
Rpi5OglEnsureDesktopGraphReadback(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context);

static VOID
Rpi5OglDiscardDeferredBackBufferClear(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context);

static PRPI5VC4_OGL_CONTEXT
Rpi5OglValidateContext(
    _In_opt_ DHGLRC ContextHandle)
{
    PRPI5VC4_OGL_CONTEXT Context =
        (PRPI5VC4_OGL_CONTEXT)ContextHandle;

    if (Context == NULL ||
        Context->Signature != RPI5VC4_OGL_CONTEXT_SIGNATURE)
    {
        return NULL;
    }

    return Context;
}

static PRPI5VC4_OGL_CONTEXT
Rpi5OglCurrentContext(VOID)
{
    if (Rpi5OglGetCurrentValue == NULL)
        return NULL;

    return Rpi5OglValidateContext(
        (DHGLRC)Rpi5OglGetCurrentValue());
}

PRPI5VC4_OGL_GL2_STATE
Rpi5OglCurrentGl2State(VOID)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    return Context != NULL ? Context->Gl2State : NULL;
}

PRPI5VC4_OGL_FBO_STATE
Rpi5OglCurrentFboState(VOID)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    return Context != NULL ? Context->FboState : NULL;
}

PRPI5VC4_OGL_BUFFER_STATE
Rpi5OglCurrentBufferState(VOID)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    return Context != NULL ? Context->BufferState : NULL;
}

GLcontext *
gl_get_thread_context(VOID)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    return Context != NULL ? Context->MesaContext : NULL;
}

static PVOID
Rpi5OglResizeAllocation(
    _In_opt_ PVOID Allocation,
    _In_ SIZE_T Size)
{
    if (Allocation == NULL)
        return HeapAlloc(GetProcessHeap(), 0, Size);

    return HeapReAlloc(GetProcessHeap(), 0, Allocation, Size);
}

static VOID
Rpi5OglFreeCachedList(
    _In_opt_ PRPI5VC4_OGL_CACHED_LIST List)
{
    if (List == NULL)
        return;

    HeapFree(GetProcessHeap(), 0, List->ObjectPosition);
    HeapFree(GetProcessHeap(), 0, List->ObjectNormal);
    HeapFree(GetProcessHeap(), 0, List->PositionMap);
    HeapFree(GetProcessHeap(), 0, List->NormalMap);
    HeapFree(GetProcessHeap(), 0, List->UnitObjectNormal);
    HeapFree(GetProcessHeap(), 0, List->Triangles);
    HeapFree(GetProcessHeap(), 0, List->ClipPosition);
    HeapFree(GetProcessHeap(), 0, List->NdcPosition);
    HeapFree(GetProcessHeap(), 0, List->EyeNormal);
    HeapFree(GetProcessHeap(), 0, List->VertexColor);
    HeapFree(GetProcessHeap(), 0, List->VertexColorWord);
    HeapFree(GetProcessHeap(), 0, List->OutputVertices);
    HeapFree(GetProcessHeap(), 0, List);
}

static VOID
Rpi5OglRemoveCachedList(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLuint Name)
{
    PRPI5VC4_OGL_CACHED_LIST *Link = &Context->CachedLists;

    while (*Link != NULL)
    {
        PRPI5VC4_OGL_CACHED_LIST List = *Link;

        if (List->Name == Name)
        {
            *Link = List->Next;
            Rpi5OglFreeCachedList(List);
            return;
        }
        Link = &List->Next;
    }
}

static VOID
Rpi5OglFreeCachedLists(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    PRPI5VC4_OGL_CACHED_LIST List = Context->CachedLists;

    Context->CachedLists = NULL;
    while (List != NULL)
    {
        PRPI5VC4_OGL_CACHED_LIST Next = List->Next;

        Rpi5OglFreeCachedList(List);
        List = Next;
    }
}

static PRPI5VC4_OGL_CACHED_LIST
Rpi5OglFindCachedList(
    _In_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLuint Name)
{
    PRPI5VC4_OGL_CACHED_LIST List;

    for (List = Context->CachedLists; List != NULL; List = List->Next)
    {
        if (List->Name == Name)
            return List;
    }
    return NULL;
}

static BOOL
Rpi5OglGrowPrimitiveVertices(
    _Inout_ PRPI5VC4_OGL_LIST_BUILDER Builder,
    _In_ ULONG RequiredCount)
{
    PRPI5VC4_OGL_LIST_VERTEX Vertices;
    ULONG NewCapacity;

    if (RequiredCount <= Builder->PrimitiveVertexCapacity)
        return TRUE;

    NewCapacity = Builder->PrimitiveVertexCapacity != 0 ?
                  Builder->PrimitiveVertexCapacity * 2 : 64;
    while (NewCapacity < RequiredCount)
        NewCapacity *= 2;

    Vertices = Rpi5OglResizeAllocation(
        Builder->PrimitiveVertices,
        NewCapacity * sizeof(*Vertices));
    if (Vertices == NULL)
        return FALSE;

    Builder->PrimitiveVertices = Vertices;
    Builder->PrimitiveVertexCapacity = NewCapacity;
    return TRUE;
}

static BOOL
Rpi5OglGrowCachedObjectVertices(
    _Inout_ PRPI5VC4_OGL_CACHED_LIST List,
    _In_ ULONG RequiredCount)
{
    GLfloat (*Positions)[4];
    GLfloat (*Normals)[3];
    ULONG NewCapacity;

    if (RequiredCount <= List->ObjectVertexCapacity)
        return TRUE;

    NewCapacity = List->ObjectVertexCapacity != 0 ?
                  List->ObjectVertexCapacity * 2 : 128;
    while (NewCapacity < RequiredCount)
        NewCapacity *= 2;

    Positions = Rpi5OglResizeAllocation(
        List->ObjectPosition,
        NewCapacity * sizeof(*Positions));
    if (Positions == NULL)
        return FALSE;
    List->ObjectPosition = Positions;

    Normals = Rpi5OglResizeAllocation(
        List->ObjectNormal,
        NewCapacity * sizeof(*Normals));
    if (Normals == NULL)
        return FALSE;
    List->ObjectNormal = Normals;
    List->ObjectVertexCapacity = NewCapacity;
    return TRUE;
}

static BOOL
Rpi5OglGrowCachedTriangles(
    _Inout_ PRPI5VC4_OGL_CACHED_LIST List,
    _In_ ULONG RequiredCount)
{
    PRPI5VC4_OGL_LIST_TRIANGLE Triangles;
    ULONG NewCapacity;

    if (RequiredCount <= List->TriangleCapacity)
        return TRUE;

    NewCapacity = List->TriangleCapacity != 0 ?
                  List->TriangleCapacity * 2 : 128;
    while (NewCapacity < RequiredCount)
        NewCapacity *= 2;

    Triangles = Rpi5OglResizeAllocation(
        List->Triangles,
        NewCapacity * sizeof(*Triangles));
    if (Triangles == NULL)
        return FALSE;

    List->Triangles = Triangles;
    List->TriangleCapacity = NewCapacity;
    return TRUE;
}

static BOOL
Rpi5OglAppendCachedPrimitiveVertices(
    _Inout_ PRPI5VC4_OGL_LIST_BUILDER Builder,
    _Out_ PULONG BaseVertex)
{
    PRPI5VC4_OGL_CACHED_LIST List = Builder->List;
    ULONG RequiredCount = List->ObjectVertexCount +
                          Builder->PrimitiveVertexCount;
    ULONG Vertex;

    if (!Rpi5OglGrowCachedObjectVertices(List, RequiredCount))
        return FALSE;

    *BaseVertex = List->ObjectVertexCount;
    for (Vertex = 0; Vertex < Builder->PrimitiveVertexCount; Vertex++)
    {
        CopyMemory(List->ObjectPosition[*BaseVertex + Vertex],
                   Builder->PrimitiveVertices[Vertex].Position,
                   sizeof(List->ObjectPosition[0]));
        CopyMemory(List->ObjectNormal[*BaseVertex + Vertex],
                   Builder->PrimitiveVertices[Vertex].Normal,
                   sizeof(List->ObjectNormal[0]));
    }
    List->ObjectVertexCount = RequiredCount;
    return TRUE;
}

static BOOL
Rpi5OglAppendCachedTriangle(
    _Inout_ PRPI5VC4_OGL_LIST_BUILDER Builder,
    _In_ ULONG BaseVertex,
    _In_ ULONG Vertex0,
    _In_ ULONG Vertex1,
    _In_ ULONG Vertex2,
    _In_ ULONG ProvokingVertex)
{
    PRPI5VC4_OGL_CACHED_LIST List = Builder->List;
    PRPI5VC4_OGL_LIST_TRIANGLE Triangle;

    if (!Rpi5OglGrowCachedTriangles(List, List->TriangleCount + 1))
        return FALSE;

    Triangle = &List->Triangles[List->TriangleCount++];
    Triangle->Vertex[0] = BaseVertex + Vertex0;
    Triangle->Vertex[1] = BaseVertex + Vertex1;
    Triangle->Vertex[2] = BaseVertex + Vertex2;
    Triangle->ProvokingVertex = BaseVertex + ProvokingVertex;
    Triangle->ShadeModel = Builder->CurrentShadeModel;
    return TRUE;
}

static GLboolean
Rpi5OglVisitListBegin(
    _In_ PVOID User,
    _In_ GLenum Mode)
{
    PRPI5VC4_OGL_LIST_BUILDER Builder = User;

    if (Builder->InsidePrimitive ||
        !Builder->MaterialDefined ||
        !Builder->NormalDefined ||
        !Builder->ShadeModelDefined ||
        (Mode != GL_QUADS && Mode != GL_QUAD_STRIP))
    {
        return GL_FALSE;
    }

    Builder->InsidePrimitive = TRUE;
    Builder->PrimitiveMode = Mode;
    Builder->PrimitiveVertexCount = 0;
    return GL_TRUE;
}

static GLboolean
Rpi5OglVisitListEnd(
    _In_ PVOID User)
{
    PRPI5VC4_OGL_LIST_BUILDER Builder = User;
    ULONG BaseVertex;
    ULONG Vertex;

    if (!Builder->InsidePrimitive)
        return GL_FALSE;

    if (!Rpi5OglAppendCachedPrimitiveVertices(Builder, &BaseVertex))
        return GL_FALSE;

    if (Builder->PrimitiveMode == GL_QUADS)
    {
        if (Builder->PrimitiveVertexCount < 4 ||
            (Builder->PrimitiveVertexCount % 4) != 0)
        {
            return GL_FALSE;
        }

        for (Vertex = 0;
             Vertex < Builder->PrimitiveVertexCount;
             Vertex += 4)
        {
            if (!Rpi5OglAppendCachedTriangle(Builder,
                                             BaseVertex,
                                             Vertex,
                                             Vertex + 1,
                                             Vertex + 3,
                                             Vertex + 3) ||
                !Rpi5OglAppendCachedTriangle(Builder,
                                             BaseVertex,
                                             Vertex + 1,
                                             Vertex + 2,
                                             Vertex + 3,
                                             Vertex + 3))
            {
                return GL_FALSE;
            }
        }
    }
    else
    {
        if (Builder->PrimitiveVertexCount < 4 ||
            (Builder->PrimitiveVertexCount % 2) != 0)
        {
            return GL_FALSE;
        }

        for (Vertex = 3;
             Vertex < Builder->PrimitiveVertexCount;
             Vertex += 2)
        {
            if (!Rpi5OglAppendCachedTriangle(Builder,
                                             BaseVertex,
                                             Vertex - 3,
                                             Vertex - 2,
                                             Vertex - 1,
                                             Vertex) ||
                !Rpi5OglAppendCachedTriangle(Builder,
                                             BaseVertex,
                                             Vertex - 2,
                                             Vertex,
                                             Vertex - 1,
                                             Vertex))
            {
                return GL_FALSE;
            }
        }
    }

    Builder->InsidePrimitive = FALSE;
    Builder->PrimitiveVertexCount = 0;
    return GL_TRUE;
}

static GLboolean
Rpi5OglVisitListMaterial(
    _In_ PVOID User,
    _In_ GLenum Face,
    _In_ GLenum Name,
    _In_reads_(4) const GLfloat Parameters[4])
{
    PRPI5VC4_OGL_LIST_BUILDER Builder = User;

    if (Builder->InsidePrimitive ||
        Builder->MaterialDefined ||
        Builder->List->ObjectVertexCount != 0 ||
        Face != GL_FRONT ||
        Name != GL_AMBIENT_AND_DIFFUSE)
    {
        return GL_FALSE;
    }

    CopyMemory(Builder->List->Material,
               Parameters,
               sizeof(Builder->List->Material));
    Builder->MaterialDefined = TRUE;
    return GL_TRUE;
}

static GLboolean
Rpi5OglVisitListNormal(
    _In_ PVOID User,
    _In_reads_(3) const GLfloat Normal[3])
{
    PRPI5VC4_OGL_LIST_BUILDER Builder = User;

    CopyMemory(Builder->CurrentNormal,
               Normal,
               sizeof(Builder->CurrentNormal));
    Builder->NormalDefined = TRUE;
    return GL_TRUE;
}

static GLboolean
Rpi5OglVisitListShadeModel(
    _In_ PVOID User,
    _In_ GLenum Mode)
{
    PRPI5VC4_OGL_LIST_BUILDER Builder = User;

    if (Builder->InsidePrimitive ||
        (Mode != GL_FLAT && Mode != GL_SMOOTH))
    {
        return GL_FALSE;
    }

    Builder->CurrentShadeModel = Mode;
    Builder->ShadeModelDefined = TRUE;
    return GL_TRUE;
}

static GLboolean
Rpi5OglVisitListVertex(
    _In_ PVOID User,
    _In_reads_(4) const GLfloat Position[4])
{
    PRPI5VC4_OGL_LIST_BUILDER Builder = User;
    PRPI5VC4_OGL_LIST_VERTEX Vertex;

    if (!Builder->InsidePrimitive || !Builder->NormalDefined ||
        !Rpi5OglGrowPrimitiveVertices(
            Builder,
            Builder->PrimitiveVertexCount + 1))
    {
        return GL_FALSE;
    }

    Vertex = &Builder->PrimitiveVertices[Builder->PrimitiveVertexCount++];
    CopyMemory(Vertex->Position, Position, sizeof(Vertex->Position));
    CopyMemory(Vertex->Normal,
               Builder->CurrentNormal,
               sizeof(Vertex->Normal));
    return GL_TRUE;
}

static BOOL
Rpi5OglCompactCachedVertices(
    _Inout_ PRPI5VC4_OGL_CACHED_LIST List)
{
    ULONG ObjectVertexCount = List->ObjectVertexCount;
    GLfloat (*Positions)[4];
    GLfloat (*Normals)[3];
    PULONG PositionMap;
    PULONG NormalMap;
    ULONG Vertex;

    Positions = HeapAlloc(GetProcessHeap(), 0,
                          ObjectVertexCount * sizeof(*Positions));
    Normals = HeapAlloc(GetProcessHeap(), 0,
                        ObjectVertexCount * sizeof(*Normals));
    PositionMap = HeapAlloc(GetProcessHeap(), 0,
                            ObjectVertexCount * sizeof(*PositionMap));
    NormalMap = HeapAlloc(GetProcessHeap(), 0,
                          ObjectVertexCount * sizeof(*NormalMap));
    if (Positions == NULL || Normals == NULL ||
        PositionMap == NULL || NormalMap == NULL)
    {
        HeapFree(GetProcessHeap(), 0, Positions);
        HeapFree(GetProcessHeap(), 0, Normals);
        HeapFree(GetProcessHeap(), 0, PositionMap);
        HeapFree(GetProcessHeap(), 0, NormalMap);
        return FALSE;
    }

    for (Vertex = 0; Vertex < ObjectVertexCount; Vertex++)
    {
        ULONG Unique;

        for (Unique = 0; Unique < List->PositionCount; Unique++)
        {
            if (Positions[Unique][0] == List->ObjectPosition[Vertex][0] &&
                Positions[Unique][1] == List->ObjectPosition[Vertex][1] &&
                Positions[Unique][2] == List->ObjectPosition[Vertex][2] &&
                Positions[Unique][3] == List->ObjectPosition[Vertex][3])
            {
                break;
            }
        }
        if (Unique == List->PositionCount)
        {
            CopyMemory(Positions[Unique],
                       List->ObjectPosition[Vertex],
                       sizeof(Positions[Unique]));
            List->PositionCount++;
        }
        PositionMap[Vertex] = Unique;

        for (Unique = 0; Unique < List->NormalCount; Unique++)
        {
            if (Normals[Unique][0] == List->ObjectNormal[Vertex][0] &&
                Normals[Unique][1] == List->ObjectNormal[Vertex][1] &&
                Normals[Unique][2] == List->ObjectNormal[Vertex][2])
            {
                break;
            }
        }
        if (Unique == List->NormalCount)
        {
            CopyMemory(Normals[Unique],
                       List->ObjectNormal[Vertex],
                       sizeof(Normals[Unique]));
            List->NormalCount++;
        }
        NormalMap[Vertex] = Unique;
    }

    HeapFree(GetProcessHeap(), 0, List->ObjectPosition);
    HeapFree(GetProcessHeap(), 0, List->ObjectNormal);
    List->ObjectPosition = Positions;
    List->ObjectNormal = Normals;
    List->PositionMap = PositionMap;
    List->NormalMap = NormalMap;
    return TRUE;
}

static BOOL
Rpi5OglAllocateCachedListScratch(
    _Inout_ PRPI5VC4_OGL_CACHED_LIST List)
{
    ULONG PositionCount = List->PositionCount;
    ULONG NormalCount = List->NormalCount;
    ULONG OutputVertexCount = List->TriangleCount * 3;
    ULONG Vertex;

    List->UnitObjectNormal = HeapAlloc(
        GetProcessHeap(), 0,
        NormalCount * sizeof(*List->UnitObjectNormal));
    List->ClipPosition = HeapAlloc(GetProcessHeap(), 0,
                                   PositionCount *
                                   sizeof(*List->ClipPosition));
    List->NdcPosition = HeapAlloc(GetProcessHeap(), 0,
                                  PositionCount *
                                  sizeof(*List->NdcPosition));
    List->EyeNormal = HeapAlloc(GetProcessHeap(), 0,
                                NormalCount *
                                sizeof(*List->EyeNormal));
    List->VertexColor = HeapAlloc(GetProcessHeap(), 0,
                                  NormalCount *
                                  sizeof(*List->VertexColor));
    List->VertexColorWord = HeapAlloc(
        GetProcessHeap(), 0,
        NormalCount * sizeof(*List->VertexColorWord));
    List->OutputVertices = HeapAlloc(
        GetProcessHeap(), 0,
        OutputVertexCount * sizeof(*List->OutputVertices));

    if (List->UnitObjectNormal == NULL ||
        List->ClipPosition == NULL ||
        List->NdcPosition == NULL ||
        List->EyeNormal == NULL ||
        List->VertexColor == NULL ||
        List->VertexColorWord == NULL ||
        List->OutputVertices == NULL)
    {
        return FALSE;
    }

    for (Vertex = 0; Vertex < NormalCount; Vertex++)
    {
        GLdouble X = List->ObjectNormal[Vertex][0];
        GLdouble Y = List->ObjectNormal[Vertex][1];
        GLdouble Z = List->ObjectNormal[Vertex][2];
        GLdouble Length = sqrt(X * X + Y * Y + Z * Z);
        GLdouble Scale = Length > 1.0e-30 ? 1.0 / Length : 1.0;

        List->UnitObjectNormal[Vertex][0] = (GLfloat)(X * Scale);
        List->UnitObjectNormal[Vertex][1] = (GLfloat)(Y * Scale);
        List->UnitObjectNormal[Vertex][2] = (GLfloat)(Z * Scale);
    }

    return List->ClipPosition != NULL &&
           List->NdcPosition != NULL &&
           List->EyeNormal != NULL &&
           List->VertexColor != NULL &&
           List->VertexColorWord != NULL &&
           List->OutputVertices != NULL;
}

static VOID
Rpi5OglBuildCachedList(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLuint Name)
{
    static const struct gl_display_list_visitor Visitor =
    {
        Rpi5OglVisitListBegin,
        Rpi5OglVisitListEnd,
        Rpi5OglVisitListMaterial,
        Rpi5OglVisitListNormal,
        Rpi5OglVisitListShadeModel,
        Rpi5OglVisitListVertex
    };
    RPI5VC4_OGL_LIST_BUILDER Builder;
    PRPI5VC4_OGL_CACHED_LIST List;

    Rpi5OglRemoveCachedList(Context, Name);
    List = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*List));
    if (List == NULL)
        return;

    ZeroMemory(&Builder, sizeof(Builder));
    Builder.List = List;
    if (!gl_visit_display_list(Context->MesaContext,
                               Name,
                               &Visitor,
                               &Builder) ||
        Builder.InsidePrimitive ||
        !Builder.MaterialDefined ||
        !Builder.NormalDefined ||
        !Builder.ShadeModelDefined ||
        List->ObjectVertexCount == 0 ||
        List->TriangleCount == 0 ||
        !Rpi5OglCompactCachedVertices(List) ||
        !Rpi5OglAllocateCachedListScratch(List))
    {
        HeapFree(GetProcessHeap(), 0, Builder.PrimitiveVertices);
        Rpi5OglFreeCachedList(List);
        return;
    }

    HeapFree(GetProcessHeap(), 0, Builder.PrimitiveVertices);
    List->Name = Name;
    List->FinalShadeModel = Builder.CurrentShadeModel;
    CopyMemory(List->FinalNormal,
               Builder.CurrentNormal,
               sizeof(List->FinalNormal));
    List->Next = Context->CachedLists;
    Context->CachedLists = List;
}

static BOOL
Rpi5OglQueryV3d(
    _In_ HDC Hdc)
{
    RPI5VC4_V3D_INFO Info;
    INT Returned;

    ZeroMemory(&Info, sizeof(Info));
    Returned = ExtEscape(Hdc,
                         RPI5VC4_ESCAPE_QUERY_V3D,
                         0,
                         NULL,
                         sizeof(Info),
                         (LPSTR)&Info);
    return Returned >= (INT)sizeof(Info) &&
           Info.AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
           Info.Version == 71 &&
           (Info.Flags & RPI5VC4_V3D_FLAG_IDENT_VALID) != 0;
}

static BOOL
Rpi5OglGetDrawableSize(
    _In_ HDC Hdc,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    HWND Window;
    RECT ClientRect;
    HBITMAP BitmapHandle;
    BITMAP Bitmap;

    Window = WindowFromDC(Hdc);
    if (Window != NULL && GetClientRect(Window, &ClientRect))
    {
        if (ClientRect.right > ClientRect.left &&
            ClientRect.bottom > ClientRect.top)
        {
            *Width = ClientRect.right - ClientRect.left;
            *Height = ClientRect.bottom - ClientRect.top;
            return TRUE;
        }
    }

    BitmapHandle = GetCurrentObject(Hdc, OBJ_BITMAP);
    if (BitmapHandle != NULL &&
        GetObjectW(BitmapHandle, sizeof(Bitmap), &Bitmap) == sizeof(Bitmap) &&
        Bitmap.bmWidth > 0 &&
        Bitmap.bmHeight != 0)
    {
        *Width = Bitmap.bmWidth;
        *Height = Bitmap.bmHeight > 0 ?
                  Bitmap.bmHeight : -Bitmap.bmHeight;
        return TRUE;
    }

    *Width = 1;
    *Height = 1;
    return FALSE;
}

static BOOL
Rpi5OglGetDirectPresentOrigin(
    _In_ PRPI5VC4_OGL_CONTEXT Context,
    _Out_ PULONG DestinationX,
    _Out_ PULONG DestinationY,
    _Out_opt_ PULONG VisibleWidth,
    _Out_opt_ PULONG VisibleHeight)
{
    POINT Origin = {0, 0};
    DWORD ObjectType;
    BOOL OriginValid;
    INT ScreenWidth;
    INT ScreenHeight;
    BOOL AllowClipping = VisibleWidth != NULL && VisibleHeight != NULL;

    ObjectType = GetObjectType(Context->Hdc);
    OriginValid = GetDCOrgEx(Context->Hdc, &Origin);
    ScreenWidth = GetDeviceCaps(Context->Hdc, HORZRES);
    ScreenHeight = GetDeviceCaps(Context->Hdc, VERTRES);
    if (ObjectType != OBJ_DC || !OriginValid ||
        Origin.x < 0 || Origin.y < 0 ||
        ScreenWidth <= 0 || ScreenHeight <= 0 ||
        Origin.x >= ScreenWidth || Origin.y >= ScreenHeight ||
        (!AllowClipping &&
         ((ULONGLONG)(ULONG)Origin.x + Context->Width >
              (ULONG)ScreenWidth ||
          (ULONGLONG)(ULONG)Origin.y + Context->Height >
              (ULONG)ScreenHeight)))
    {
        return FALSE;
    }

    *DestinationX = (ULONG)Origin.x;
    *DestinationY = (ULONG)Origin.y;
    if (AllowClipping)
    {
        *VisibleWidth = min(Context->Width,
                            (ULONG)ScreenWidth - *DestinationX);
        *VisibleHeight = min(Context->Height,
                             (ULONG)ScreenHeight - *DestinationY);
    }
    return TRUE;
}

static BOOL
Rpi5OglClipDirectPresentVertices(
    _Inout_updates_(VertexCount) RPI5VC4_V3D_VERTEX *Vertices,
    _In_ ULONG VertexCount,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG VisibleWidth,
    _In_ ULONG VisibleHeight)
{
    GLfloat WidthFraction;
    GLfloat HeightFraction;
    GLfloat BottomFraction;
    ULONG Vertex;

    if (VisibleWidth == Width && VisibleHeight == Height)
        return TRUE;
    if (Width == 0 || Height == 0 || VisibleWidth == 0 ||
        VisibleHeight == 0 || VisibleWidth > Width ||
        VisibleHeight > Height)
    {
        return FALSE;
    }

    WidthFraction = (GLfloat)VisibleWidth / (GLfloat)Width;
    HeightFraction = (GLfloat)VisibleHeight / (GLfloat)Height;
    BottomFraction = 1.0f - HeightFraction;
    for (Vertex = 0; Vertex < VertexCount; Vertex++)
    {
        GLfloat *Position = (GLfloat *)Vertices[Vertex].Position;
        GLfloat *TexCoord = (GLfloat *)Vertices[Vertex].TexCoord;
        GLfloat ExpectedU;
        GLfloat ExpectedV;
        GLfloat DeltaU;
        GLfloat DeltaV;

        if (Position[3] <= 0.0f)
            return FALSE;
        ExpectedU = Position[0] / Position[3] * 0.5f + 0.5f;
        ExpectedV = Position[1] / Position[3] * 0.5f + 0.5f;
        DeltaU = TexCoord[0] - ExpectedU;
        DeltaV = TexCoord[1] - ExpectedV;
        if (DeltaU < -0.0001f || DeltaU > 0.0001f ||
            DeltaV < -0.0001f || DeltaV > 0.0001f)
        {
            return FALSE;
        }
        TexCoord[0] = ExpectedU * WidthFraction;
        TexCoord[1] = BottomFraction + ExpectedV * HeightFraction;
    }
    return TRUE;
}

static BOOL
Rpi5OglResizeBackBuffer(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONGLONG BufferSize64;
    SIZE_T BufferSize;
    PVOID NewBuffer;

    if (Width == 0 || Height == 0)
        return FALSE;
    if (Context->BackBuffer != NULL &&
        Context->Width == Width &&
        Context->Height == Height)
    {
        return TRUE;
    }

    Rpi5OglDiscardDeferredBackBufferClear(Context);

    BufferSize64 = (ULONGLONG)Width * Height * sizeof(ULONG);
    if (BufferSize64 > 0xFFFFFFFFu)
        return FALSE;
    BufferSize = (SIZE_T)BufferSize64;

    if (Context->BackBuffer == NULL)
    {
        NewBuffer = HeapAlloc(GetProcessHeap(),
                              HEAP_ZERO_MEMORY,
                              BufferSize);
    }
    else
    {
        NewBuffer = HeapReAlloc(GetProcessHeap(),
                                HEAP_ZERO_MEMORY,
                                Context->BackBuffer,
                                BufferSize);
    }
    if (NewBuffer == NULL)
        return FALSE;

    Context->BackBuffer = NewBuffer;
    Context->Width = Width;
    Context->Height = Height;
    Context->Stride = Width * sizeof(ULONG);
    Context->HardwareClearFresh = FALSE;
    Context->HardwareBatchActive = FALSE;
    Context->BatchDirectPresented = FALSE;
    Context->BatchVertexCount = 0;
    Context->BatchRequest->DrawCount = 0;
    ZeroMemory(Context->BatchRequest->Draws,
               sizeof(Context->BatchRequest->Draws));
    ZeroMemory(Context->BatchRequest->ShaderUniforms,
               sizeof(Context->BatchRequest->ShaderUniforms));
    Context->BatchProgramFlags = 0;
    Context->BatchTextured = FALSE;
    Context->BatchTextureLinear = FALSE;
    Context->BatchTextureMipmap = FALSE;
    Context->BatchTextureGeneration = 0;
    Context->BatchTextureGeneration1 = 0;
    Context->BatchBlendEnabled = FALSE;
    Context->BatchPreserveDestinationAlphaBlend = FALSE;
    Context->BatchDesktop2D = FALSE;
    Rpi5OglResetDesktopGraph(Context);
    Context->DesktopGraphReadbackPending = FALSE;
    Context->DesktopGraphCacheId = 0;
    Context->DesktopGraphCacheSignature = 0;
    return TRUE;
}

static BOOL
Rpi5OglRefreshDrawable(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    ULONG Width;
    ULONG Height;

    Rpi5OglGetDrawableSize(Context->Hdc, &Width, &Height);
    return Rpi5OglResizeBackBuffer(Context, Width, Height);
}

static ULONG
Rpi5OglPackColor(
    _In_ GLubyte Red,
    _In_ GLubyte Green,
    _In_ GLubyte Blue,
    _In_ GLubyte Alpha)
{
    return ((ULONG)Alpha << 24) |
           ((ULONG)Red << 16) |
           ((ULONG)Green << 8) |
           Blue;
}

static VOID
Rpi5OglUnpackColor(
    _In_ ULONG Color,
    _Out_ GLubyte *Red,
    _Out_ GLubyte *Green,
    _Out_ GLubyte *Blue,
    _Out_ GLubyte *Alpha)
{
    *Blue = (GLubyte)Color;
    *Green = (GLubyte)(Color >> 8);
    *Red = (GLubyte)(Color >> 16);
    *Alpha = (GLubyte)(Color >> 24);
}

typedef struct _RPI5VC4_OGL_RENDER_TARGET
{
    struct gl_texture_object *Texture;
    struct gl_texture_object *DepthTexture;
    PULONG Pixels;
    GLubyte *TextureData;
    GLdepth *DepthData;
    BOOL HasColor;
    BOOL HasDepth;
    ULONG Width;
    ULONG Height;
    GLenum TextureFormat;
    GLenum DepthFormat;
    GLuint Framebuffer;
    ULONG Generation;
} RPI5VC4_OGL_RENDER_TARGET, *PRPI5VC4_OGL_RENDER_TARGET;

static BOOL
Rpi5OglResolveRenderTarget(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _Out_ PRPI5VC4_OGL_RENDER_TARGET Target)
{
    RPI5VC4_OGL_FBO_COLOR_TARGET FboTarget;
    GLuint Framebuffer;

    ZeroMemory(Target, sizeof(*Target));
    Framebuffer = Rpi5OglFboCurrentName(Context->FboState);
    if (Framebuffer != 0)
    {
        if (!Rpi5OglFboGetValidatedColorTarget(
                Context->FboState,
                "RPi5 framebuffer access",
                &FboTarget))
        {
            return FALSE;
        }
        Target->Texture = FboTarget.Texture;
        Target->DepthTexture = FboTarget.DepthTexture;
        Target->TextureData = FboTarget.Data;
        Target->DepthData = FboTarget.DepthData;
        Target->HasColor = FboTarget.HasColor;
        Target->HasDepth = FboTarget.HasDepth;
        Target->Width = FboTarget.Width;
        Target->Height = FboTarget.Height;
        Target->TextureFormat = FboTarget.Format;
        Target->DepthFormat = FboTarget.DepthFormat;
        Target->Framebuffer = FboTarget.Framebuffer;
        Target->Generation = FboTarget.Generation;
        return TRUE;
    }

    if (!Rpi5OglRefreshDrawable(Context))
        return FALSE;
    Target->Pixels = Context->BackBuffer;
    Target->HasColor = TRUE;
    Target->Width = Context->Width;
    Target->Height = Context->Height;
    return TRUE;
}

static BOOL
Rpi5OglTargetPixelValid(
    _In_ const RPI5VC4_OGL_RENDER_TARGET *Target,
    _In_ GLint X,
    _In_ GLint Y)
{
    return X >= 0 && Y >= 0 &&
           X < (GLint)Target->Width && Y < (GLint)Target->Height;
}

static VOID
Rpi5OglWriteTargetPixel(
    _Inout_ PRPI5VC4_OGL_RENDER_TARGET Target,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Color)
{
    GLubyte Red;
    GLubyte Green;
    GLubyte Blue;
    GLubyte Alpha;
    GLubyte *Texel;
    ULONG Components;

    if (Target->Pixels != NULL)
    {
        Target->Pixels[Y * Target->Width + X] = Color;
        return;
    }
    if (!Target->HasColor || Target->TextureData == NULL)
        return;

    Components = Target->TextureFormat == GL_RGBA ? 4 : 3;
    Texel = Target->TextureData +
            (Y * Target->Width + X) * Components;
    Rpi5OglUnpackColor(Color, &Red, &Green, &Blue, &Alpha);
    Texel[0] = Red;
    Texel[1] = Green;
    Texel[2] = Blue;
    if (Components == 4)
        Texel[3] = Alpha;
}

static ULONG
Rpi5OglReadTargetPixel(
    _In_ const RPI5VC4_OGL_RENDER_TARGET *Target,
    _In_ ULONG X,
    _In_ ULONG Y)
{
    const GLubyte *Texel;
    ULONG Components;

    if (Target->Pixels != NULL)
        return Target->Pixels[Y * Target->Width + X];
    if (!Target->HasColor || Target->TextureData == NULL)
        return 0;

    Components = Target->TextureFormat == GL_RGBA ? 4 : 3;
    Texel = Target->TextureData +
            (Y * Target->Width + X) * Components;
    return Rpi5OglPackColor(Texel[0], Texel[1], Texel[2],
                            Components == 4 ? Texel[3] : 255);
}

static VOID
Rpi5OglCopyTargetPixels(
    _Inout_ PRPI5VC4_OGL_RENDER_TARGET Target,
    _In_ ULONG DestinationX,
    _In_ ULONG DestinationY,
    _In_reads_(Width * Height) const ULONG *Pixels,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG Row;
    ULONG Column;

    if (Target->Pixels != NULL && DestinationX == 0 &&
        Width == Target->Width)
    {
        CopyMemory(Target->Pixels + DestinationY * Target->Width,
                   Pixels,
                   Width * Height * sizeof(*Pixels));
        return;
    }

    for (Row = 0; Row < Height; Row++)
    {
        for (Column = 0; Column < Width; Column++)
        {
            Rpi5OglWriteTargetPixel(Target,
                                    DestinationX + Column,
                                    DestinationY + Row,
                                    Pixels[Row * Width + Column]);
        }
    }
}

static BOOL
Rpi5OglDeferredClearMatchesTarget(
    _In_ const RPI5VC4_OGL_DEFERRED_CLEAR *Clear,
    _In_ const RPI5VC4_OGL_RENDER_TARGET *Target)
{
    if (!Clear->Active || Clear->Width != Target->Width ||
        Clear->Height != Target->Height)
    {
        return FALSE;
    }
    if (Target->Pixels != NULL)
        return Clear->Pixels == Target->Pixels;
    return Target->Texture != NULL &&
           Clear->Texture == Target->Texture &&
           Clear->TextureData == Target->TextureData &&
           Clear->TextureFormat == Target->TextureFormat;
}

static VOID
Rpi5OglMaterializeDeferredClearEntry(
    _Inout_ PRPI5VC4_OGL_DEFERRED_CLEAR Clear)
{
    ULONG PixelCount;
    ULONG Pixel;

    if (!Clear->Active)
        return;

    PixelCount = Clear->Width * Clear->Height;
    if (Clear->Pixels != NULL)
    {
        for (Pixel = 0; Pixel < PixelCount; Pixel++)
            Clear->Pixels[Pixel] = Clear->Color;
    }
    else if (Clear->TextureData != NULL &&
             Clear->TextureFormat == GL_RGBA)
    {
        GLubyte Red;
        GLubyte Green;
        GLubyte Blue;
        GLubyte Alpha;
        ULONG MemoryColor;
        PULONG TexturePixels = (PULONG)Clear->TextureData;

        Rpi5OglUnpackColor(Clear->Color,
                           &Red,
                           &Green,
                           &Blue,
                           &Alpha);
        MemoryColor = Red | ((ULONG)Green << 8) |
                      ((ULONG)Blue << 16) | ((ULONG)Alpha << 24);
        for (Pixel = 0; Pixel < PixelCount; Pixel++)
            TexturePixels[Pixel] = MemoryColor;
    }
    Clear->Active = FALSE;
}

static VOID
Rpi5OglDiscardDeferredClearEntry(
    _Inout_ PRPI5VC4_OGL_DEFERRED_CLEAR Clear)
{
    if (!Clear->Active)
        return;
    Clear->Active = FALSE;
}

static BOOL
Rpi5OglQueueDeferredClear(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ const RPI5VC4_OGL_RENDER_TARGET *Target,
    _In_ ULONG Color)
{
    PRPI5VC4_OGL_DEFERRED_CLEAR Clear = NULL;
    ULONG Index;

    if (Target->Pixels == NULL &&
        (Target->Texture == NULL || Target->TextureData == NULL ||
         Target->TextureFormat != GL_RGBA))
    {
        return FALSE;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Context->DeferredClears); Index++)
    {
        if (Rpi5OglDeferredClearMatchesTarget(
                &Context->DeferredClears[Index], Target))
        {
            Clear = &Context->DeferredClears[Index];
            break;
        }
        if (Clear == NULL && !Context->DeferredClears[Index].Active)
            Clear = &Context->DeferredClears[Index];
    }
    if (Clear == NULL)
    {
        Clear = &Context->DeferredClears[0];
        Rpi5OglMaterializeDeferredClearEntry(Clear);
    }

    Clear->Texture = Target->Texture;
    Clear->Pixels = Target->Pixels;
    Clear->TextureData = Target->TextureData;
    Clear->Width = Target->Width;
    Clear->Height = Target->Height;
    Clear->TextureFormat = Target->TextureFormat;
    Clear->Color = Color;
    Clear->Active = TRUE;
    Context->DeferredClearCount++;
    return TRUE;
}

static VOID
Rpi5OglMaterializeDeferredTargetClear(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ const RPI5VC4_OGL_RENDER_TARGET *Target)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Context->DeferredClears); Index++)
    {
        if (Rpi5OglDeferredClearMatchesTarget(
                &Context->DeferredClears[Index], Target))
        {
            Rpi5OglMaterializeDeferredClearEntry(
                &Context->DeferredClears[Index]);
        }
    }
}

static VOID
Rpi5OglDiscardDeferredTargetClear(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ const RPI5VC4_OGL_RENDER_TARGET *Target)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Context->DeferredClears); Index++)
    {
        if (Rpi5OglDeferredClearMatchesTarget(
                &Context->DeferredClears[Index], Target))
        {
            Rpi5OglDiscardDeferredClearEntry(
                &Context->DeferredClears[Index]);
        }
    }
}

VOID
Rpi5OglMaterializeTextureClear(
    _In_ struct gl_texture_object *Texture)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();
    ULONG Index;

    if (Context == NULL || Texture == NULL)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(Context->DeferredClears); Index++)
    {
        if (Context->DeferredClears[Index].Active &&
            Context->DeferredClears[Index].Texture == Texture)
        {
            Rpi5OglMaterializeDeferredClearEntry(
                &Context->DeferredClears[Index]);
        }
    }
}

BOOL
Rpi5OglRecordGraphTextureMipmap(
    _In_ struct gl_texture_object *Texture)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();
    PRPI5VC4_OGL_GRAPH_RESOURCE Resource;
    const struct gl_texture_image *Base;
    ULONG Width;
    ULONG Height;
    ULONG LevelCount = 1;
    ULONG PassIndex;
    LONG ResourceIndex;

    if (Context == NULL || Texture == NULL ||
        !Context->TerrainGraphActive)
    {
        return FALSE;
    }
    ResourceIndex = Rpi5OglFindDesktopGraphResource(Context, Texture);
    if (ResourceIndex < 0)
        return FALSE;
    Resource = &Context->DesktopGraphResources[ResourceIndex];
    Base = Texture->Image[0];
    if (!Resource->Produced || Base == NULL || Base->Data == NULL ||
        Base->Border != 0 || (ULONG)Base->Width != Resource->Width ||
        (ULONG)Base->Height != Resource->Height ||
        (Base->Format != GL_RGB && Base->Format != GL_RGBA))
    {
        return FALSE;
    }

    Width = Resource->Width;
    Height = Resource->Height;
    while (Width > 1u || Height > 1u)
    {
        const struct gl_texture_image *Image;

        Width = Width > 1u ? Width >> 1 : 1u;
        Height = Height > 1u ? Height >> 1 : 1u;
        if (LevelCount >= RPI5VC4_V3D_TEXTURE_MAX_LEVELS)
            return FALSE;
        Image = Texture->Image[LevelCount];
        if (Image == NULL || Image->Data == NULL || Image->Border != 0 ||
            (ULONG)Image->Width != Width ||
            (ULONG)Image->Height != Height ||
            Image->Format != Base->Format)
        {
            return FALSE;
        }
        LevelCount++;
    }

    for (PassIndex = Context->DesktopGraphPassCount;
         PassIndex != 0;
         PassIndex--)
    {
        PRPI5VC4_V3D_GRAPH_PASS Pass =
            &Context->DesktopGraphPasses[PassIndex - 1u];

        if (Pass->TargetResource == (ULONG)ResourceIndex)
        {
            Resource->LevelCount = LevelCount;
            Pass->Flags |= RPI5VC4_V3D_GRAPH_PASS_GENERATE_MIPMAP;
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
Rpi5OglDiscardDeferredTextureClear(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ struct gl_texture_object *Texture)
{
    ULONG Index;

    if (Texture == NULL)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(Context->DeferredClears); Index++)
    {
        if (Context->DeferredClears[Index].Active &&
            Context->DeferredClears[Index].Texture == Texture)
        {
            Rpi5OglDiscardDeferredClearEntry(
                &Context->DeferredClears[Index]);
        }
    }
}

static VOID
Rpi5OglDiscardDeferredBackBufferClear(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Context->DeferredClears); Index++)
    {
        if (Context->DeferredClears[Index].Active &&
            Context->DeferredClears[Index].Pixels == Context->BackBuffer)
        {
            Rpi5OglDiscardDeferredClearEntry(
                &Context->DeferredClears[Index]);
        }
    }
}

static BOOL
Rpi5OglPresent(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    RPI5VC4_OGL_RENDER_TARGET Target;
    BITMAPINFO BitmapInfo;
    INT Copied;

    if (!Rpi5OglRefreshDrawable(Context) ||
        !Rpi5OglEnsureDesktopGraphReadback(Context))
        return FALSE;

    ZeroMemory(&Target, sizeof(Target));
    Target.Pixels = Context->BackBuffer;
    Target.HasColor = TRUE;
    Target.Width = Context->Width;
    Target.Height = Context->Height;
    Rpi5OglMaterializeDeferredTargetClear(Context, &Target);

    ZeroMemory(&BitmapInfo, sizeof(BitmapInfo));
    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = Context->Width;
    BitmapInfo.bmiHeader.biHeight = Context->Height;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;
    BitmapInfo.bmiHeader.biSizeImage =
        Context->Stride * Context->Height;

    Copied = SetDIBitsToDevice(Context->Hdc,
                               0,
                               0,
                               Context->Width,
                               Context->Height,
                               0,
                               0,
                               0,
                               Context->Height,
                               Context->BackBuffer,
                               &BitmapInfo,
                               DIB_RGB_COLORS);
    GdiFlush();
    return Copied != 0;
}

static BOOL
Rpi5OglHardwareClear(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    RPI5VC4_OGL_RENDER_TARGET Target;
    RPI5VC4_V3D_CLEAR_REQUEST Request;
    PRPI5VC4_V3D_CLEAR_RESULT Result;
    ULONG HeaderSize = FIELD_OFFSET(RPI5VC4_V3D_CLEAR_RESULT, Pixels);
    ULONG PixelBytes;
    ULONG ResultSize;
    INT Returned;
    BOOL Success;

    if (!Rpi5OglResolveRenderTarget(Context, &Target) ||
        !Target.HasColor || Context->BufferMode == GL_NONE ||
        Target.Width > RPI5VC4_V3D_CLEAR_MAX_WIDTH ||
        Target.Height > RPI5VC4_V3D_CLEAR_MAX_HEIGHT)
    {
        return FALSE;
    }

    PixelBytes = Target.Width * Target.Height * sizeof(ULONG);
    if ((Target.Pixels != NULL ||
         (Target.Framebuffer != 0 && Target.Texture != NULL &&
          Target.TextureData != NULL && Target.TextureFormat == GL_RGBA)) &&
        Rpi5OglQueueDeferredClear(Context, &Target, Context->ClearColor))
    {
        Context->HardwareClearFresh = TRUE;
        Context->HardwareClearColor = Context->ClearColor;
        Context->HardwareClearFramebuffer = Target.Framebuffer;
        Context->HardwareClearGeneration = Target.Generation;
        Context->Stats.SoftwareClearCount++;
        Context->Stats.LastWidth = Target.Width;
        Context->Stats.LastHeight = Target.Height;
        return TRUE;
    }

    ResultSize = HeaderSize + PixelBytes;
    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ResultSize);
    if (Result == NULL)
        return FALSE;

    ZeroMemory(&Request, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Request.Width = Target.Width;
    Request.Height = Target.Height;
    Request.ClearColor = Context->ClearColor;

    Returned = ExtEscape(Context->Hdc,
                         RPI5VC4_ESCAPE_RENDER_CLEAR,
                         sizeof(Request),
                         (LPCSTR)&Request,
                         ResultSize,
                         (LPSTR)Result);
    Context->Stats.LastHardwareStatus = Returned > 0 ?
                                        Result->Status : 0xFFFFFFFFu;
    Success = Returned >= (INT)ResultSize &&
              Result->Size == ResultSize &&
              Result->AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
              Result->Status == RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS &&
              (Result->Flags & RPI5VC4_V3D_SELFTEST_FLAG_PASSED) != 0 &&
              Result->Width == Target.Width &&
              Result->Height == Target.Height &&
              Result->Stride == Target.Width * sizeof(ULONG) &&
              Result->ClearColor == Context->ClearColor &&
              Result->PixelBytes == PixelBytes;
    if (Success)
    {
        Rpi5OglDiscardDeferredTargetClear(Context, &Target);
        Rpi5OglCopyTargetPixels(&Target,
                                0,
                                0,
                                Result->Pixels,
                                Target.Width,
                                Target.Height);
        Context->HardwareClearFresh = TRUE;
        Context->HardwareClearColor = Result->ClearColor;
        Context->HardwareClearFramebuffer = Target.Framebuffer;
        Context->HardwareClearGeneration = Target.Generation;
        Context->Stats.HardwareClearCount++;
        Context->Stats.LastWidth = Target.Width;
        Context->Stats.LastHeight = Target.Height;
    }
    else
    {
        Context->HardwareClearFresh = FALSE;
        Context->Stats.HardwareClearFailureCount++;
    }

    HeapFree(GetProcessHeap(), 0, Result);
    return Success;
}

static ULONG
Rpi5OglFloatWord(
    _In_ GLfloat Value)
{
    ULONG Word;

    C_ASSERT(sizeof(Word) == sizeof(Value));
    CopyMemory(&Word, &Value, sizeof(Word));
    return Word;
}

static VOID
Rpi5OglFillTriangleVertex(
    _Out_ PRPI5VC4_V3D_VERTEX Destination,
    _In_reads_(4) const GLfloat Position[4],
    _In_reads_(4) const GLubyte Color[4])
{
    ULONG Component;

    for (Component = 0; Component < 4; Component++)
    {
        Destination->Position[Component] =
            Rpi5OglFloatWord(Position[Component]);
        Destination->Color[Component] =
            Rpi5OglFloatWord((GLfloat)Color[Component] / 255.0f);
    }
}

static VOID
Rpi5OglFillCachedTriangleVertex(
    _Out_ PRPI5VC4_V3D_VERTEX Destination,
    _In_reads_(4) const GLfloat Position[4],
    _In_reads_(4) const ULONG Color[4])
{
    CopyMemory(Destination->Position,
               Position,
               sizeof(Destination->Position));
    CopyMemory(Destination->Color,
               Color,
               sizeof(Destination->Color));
}

static GLubyte
Rpi5OglFastColorByte(
    _In_ GLfloat Color,
    _In_ GLfloat Scale)
{
    GLfloat Clamped = Color < 1.0F ? Color : 1.0F;

    return (GLubyte)(GLint)(Clamped * Scale + 0.5F);
}

static VOID
Rpi5OglCacheColorWords(
    _Inout_ PRPI5VC4_OGL_CACHED_LIST List)
{
    const GLfloat ByteScale = 1.0F / 255.0F;
    ULONG Vertex;
    ULONG Component;

    for (Vertex = 0; Vertex < List->NormalCount; Vertex++)
    {
        for (Component = 0; Component < 4; Component++)
        {
            List->VertexColorWord[Vertex][Component] =
                Rpi5OglFloatWord(
                    (GLfloat)List->VertexColor[Vertex][Component] *
                    ByteScale);
        }
    }
}

static BOOL
Rpi5OglShadeCachedVerticesNoSpecular(
    _In_ GLcontext *Mesa,
    _Inout_ PRPI5VC4_OGL_CACHED_LIST List)
{
    struct gl_light *Light;
    GLfloat *BaseColor = Mesa->Light.BaseColor[0];
    GLfloat RedScale = Mesa->Visual->RedScale;
    GLfloat GreenScale = Mesa->Visual->GreenScale;
    GLfloat BlueScale = Mesa->Visual->BlueScale;
    GLubyte Alpha = (GLubyte)(GLint)(BaseColor[3] *
                                     Mesa->Visual->AlphaScale);
    ULONG Vertex;

    for (Light = Mesa->Light.FirstEnabled;
         Light != NULL;
         Light = Light->NextEnabled)
    {
        if (Light->MatSpecular[0][0] != 0.0F ||
            Light->MatSpecular[0][1] != 0.0F ||
            Light->MatSpecular[0][2] != 0.0F)
        {
            return FALSE;
        }
    }

    for (Vertex = 0; Vertex < List->NormalCount; Vertex++)
    {
        GLfloat Nx = List->EyeNormal[Vertex][0];
        GLfloat Ny = List->EyeNormal[Vertex][1];
        GLfloat Nz = List->EyeNormal[Vertex][2];
        GLfloat Red = BaseColor[0];
        GLfloat Green = BaseColor[1];
        GLfloat Blue = BaseColor[2];

        for (Light = Mesa->Light.FirstEnabled;
             Light != NULL;
             Light = Light->NextEnabled)
        {
            GLfloat Dot = Nx * Light->VP_inf_norm[0] +
                          Ny * Light->VP_inf_norm[1] +
                          Nz * Light->VP_inf_norm[2];

            if (Dot > 0.0F)
            {
                Red += Dot * Light->MatDiffuse[0][0];
                Green += Dot * Light->MatDiffuse[0][1];
                Blue += Dot * Light->MatDiffuse[0][2];
            }
        }

        List->VertexColor[Vertex][0] =
            Rpi5OglFastColorByte(Red, RedScale);
        List->VertexColor[Vertex][1] =
            Rpi5OglFastColorByte(Green, GreenScale);
        List->VertexColor[Vertex][2] =
            Rpi5OglFastColorByte(Blue, BlueScale);
        List->VertexColor[Vertex][3] = Alpha;
    }

    return TRUE;
}

static BOOL
Rpi5OglTriangleStateSupported(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_V3D_BLEND_STATE BlendState;

    if (Context == NULL ||
        !Rpi5OglGl2GetBlendState(Context->Gl2State, &BlendState))
    {
        return FALSE;
    }

    return
           Mesa->Visual != NULL &&
           Mesa->Visual->RGBAflag &&
           Mesa->RenderMode == GL_RENDER &&
           Mesa->Texture.Enabled == 0 &&
           Mesa->RasterMask ==
               ((BlendState.Flags & RPI5VC4_V3D_BLEND_FLAG_ENABLE) != 0 ?
                    BLEND_BIT : 0) &&
           !Mesa->Polygon.Unfilled &&
           !Mesa->Polygon.OffsetAny &&
           !Mesa->Polygon.SmoothFlag &&
           !Mesa->Polygon.StippleFlag &&
           Mesa->Viewport.X >= 0 &&
           Mesa->Viewport.Y >= 0 &&
           Mesa->Viewport.Width > 0 &&
           Mesa->Viewport.Height > 0 &&
           Mesa->Viewport.Width <= RPI5VC4_V3D_CLEAR_MAX_WIDTH &&
           Mesa->Viewport.Height <= RPI5VC4_V3D_CLEAR_MAX_HEIGHT;
}

static BOOL
Rpi5OglPreserveDestinationAlphaBlendEnabled(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    return Mesa->Color.BlendEnabled &&
           Context != NULL &&
           Rpi5OglGl2PreservesDestinationAlphaBlend(Context->Gl2State);
}

static BOOL
Rpi5OglBatchStateSupported(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    BOOL PreserveDestinationAlphaBlend =
        Rpi5OglPreserveDestinationAlphaBlendEnabled(Mesa);

    return Context != NULL &&
           Context->BatchRequest != NULL &&
           Mesa->Visual != NULL &&
           Mesa->Visual->RGBAflag &&
           Mesa->RenderMode == GL_RENDER &&
           Mesa->Texture.Enabled == 0 &&
           Mesa->RasterMask ==
               (DEPTH_BIT |
                (PreserveDestinationAlphaBlend ? BLEND_BIT : 0)) &&
           Mesa->Depth.Test &&
           (Mesa->Depth.Func == GL_LESS ||
            Mesa->Depth.Func == GL_LEQUAL) &&
           Mesa->Depth.Mask &&
           Mesa->Depth.Clear == 1.0f &&
           !Mesa->Polygon.Unfilled &&
           !Mesa->Polygon.OffsetAny &&
           !Mesa->Polygon.SmoothFlag &&
           !Mesa->Polygon.StippleFlag &&
           Mesa->Viewport.X == 0 &&
           Mesa->Viewport.Y == 0 &&
           Mesa->Viewport.Width == (GLsizei)Context->Width &&
           Mesa->Viewport.Height == (GLsizei)Context->Height &&
           Mesa->Viewport.Width <= RPI5VC4_V3D_CLEAR_MAX_WIDTH &&
           Mesa->Viewport.Height <= RPI5VC4_V3D_CLEAR_MAX_HEIGHT &&
           Mesa->Viewport.Near == 0.0f &&
           Mesa->Viewport.Far == 1.0f &&
           Context->BufferMode == GL_BACK &&
           Rpi5OglFboCurrentName(Context->FboState) == 0;
}

static BOOL
Rpi5OglIdeasBatchStateSupported(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    ULONG ExpectedRasterMask = Mesa->Depth.Test ? DEPTH_BIT : 0;

    return Context != NULL &&
           Context->BatchRequest != NULL &&
           Mesa->Visual != NULL &&
           Mesa->Visual->RGBAflag &&
           Mesa->RenderMode == GL_RENDER &&
           Mesa->Texture.Enabled == 0 &&
           Mesa->RasterMask == ExpectedRasterMask &&
           !Mesa->Color.BlendEnabled &&
           (!Mesa->Depth.Test ||
            ((Mesa->Depth.Func == GL_LESS || Mesa->Depth.Func == GL_LEQUAL) &&
             Mesa->Depth.Mask)) &&
           Mesa->Depth.Clear == 1.0f &&
           !Mesa->Polygon.Unfilled &&
           !Mesa->Polygon.OffsetAny &&
           !Mesa->Polygon.SmoothFlag &&
           !Mesa->Polygon.StippleFlag &&
           Mesa->Viewport.X == 0 &&
           Mesa->Viewport.Y == 0 &&
           Mesa->Viewport.Width == (GLsizei)Context->Width &&
           Mesa->Viewport.Height == (GLsizei)Context->Height &&
           Mesa->Viewport.Width <= RPI5VC4_V3D_CLEAR_MAX_WIDTH &&
           Mesa->Viewport.Height <= RPI5VC4_V3D_CLEAR_MAX_HEIGHT &&
           Mesa->Viewport.Near == 0.0f &&
           Mesa->Viewport.Far == 1.0f &&
           Context->BufferMode == GL_BACK &&
           Rpi5OglFboCurrentName(Context->FboState) == 0;
}

static BOOL
Rpi5OglDepthBatchStateSupported(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;

    if (Context == NULL ||
        !Rpi5OglGl2DepthProgramActive(Context->Gl2State) ||
        !Rpi5OglResolveRenderTarget(Context, &Target))
    {
        return FALSE;
    }

    return Context->BatchRequest != NULL &&
           Context->HardwareClearFresh &&
           Context->HardwareClearFramebuffer == Target.Framebuffer &&
           Context->HardwareClearGeneration == Target.Generation &&
           Target.Framebuffer != 0 &&
           Target.HasDepth &&
           Target.DepthTexture != NULL &&
           Target.DepthData != NULL &&
           Target.DepthFormat == GL_DEPTH_COMPONENT &&
           Target.Width != 0 &&
           Target.Height != 0 &&
           Target.Width <= RPI5VC4_V3D_CLEAR_MAX_WIDTH &&
           Target.Height <= RPI5VC4_V3D_CLEAR_MAX_HEIGHT &&
           Mesa->Visual != NULL &&
           Mesa->Visual->RGBAflag &&
           Mesa->RenderMode == GL_RENDER &&
           Mesa->Texture.Enabled == 0 &&
           (Mesa->RasterMask &
            ~(DEPTH_BIT | MASKING_BIT | NO_DRAW_BIT)) == 0 &&
           (Mesa->RasterMask & DEPTH_BIT) != 0 &&
           Mesa->Color.ColorMask == 0 &&
           !Mesa->Color.BlendEnabled &&
           Mesa->Depth.Test &&
           (Mesa->Depth.Func == GL_LESS ||
            Mesa->Depth.Func == GL_LEQUAL) &&
           Mesa->Depth.Mask &&
           Mesa->Depth.Clear == 1.0f &&
           !Mesa->Scissor.Enabled &&
           !Mesa->Polygon.Unfilled &&
           !Mesa->Polygon.OffsetAny &&
           !Mesa->Polygon.SmoothFlag &&
           !Mesa->Polygon.StippleFlag &&
           Mesa->Viewport.X == 0 &&
           Mesa->Viewport.Y == 0 &&
           Mesa->Viewport.Width == (GLsizei)Target.Width &&
           Mesa->Viewport.Height == (GLsizei)Target.Height &&
           Mesa->Viewport.Near == 0.0f &&
           Mesa->Viewport.Far == 1.0f &&
           Mesa->Color.DrawBuffer == GL_NONE &&
           Context->BufferMode == GL_NONE;
}

static BOOL
Rpi5OglShadowBatchStateSupported(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    return Context != NULL &&
           Rpi5OglGl2ShadowProgramActive(Context->Gl2State) &&
           Rpi5OglBatchStateSupported(Mesa);
}

static BOOL
Rpi5OglJellyfishBatchStateSupported(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    return Context != NULL &&
           Context->BatchRequest != NULL &&
           Mesa->Visual != NULL &&
           Mesa->Visual->RGBAflag &&
           Mesa->RenderMode == GL_RENDER &&
           Mesa->Texture.Enabled == 0 &&
           Mesa->RasterMask == BLEND_BIT &&
           Mesa->Color.BlendEnabled &&
           Mesa->Color.BlendSrc == GL_SRC_ALPHA &&
           Mesa->Color.BlendDst == GL_ONE_MINUS_SRC_ALPHA &&
           !Mesa->Depth.Test &&
           !Mesa->Polygon.Unfilled &&
           !Mesa->Polygon.OffsetAny &&
           !Mesa->Polygon.SmoothFlag &&
           !Mesa->Polygon.StippleFlag &&
           Mesa->Polygon.CullBits == 0 &&
           Mesa->Viewport.X == 0 &&
           Mesa->Viewport.Y == 0 &&
           Mesa->Viewport.Width == (GLsizei)Context->Width &&
           Mesa->Viewport.Height == (GLsizei)Context->Height &&
           Mesa->Viewport.Width <= RPI5VC4_V3D_CLEAR_MAX_WIDTH &&
           Mesa->Viewport.Height <= RPI5VC4_V3D_CLEAR_MAX_HEIGHT &&
           Mesa->Viewport.Near == 0.0f &&
           Mesa->Viewport.Far == 1.0f &&
           Context->BufferMode == GL_BACK &&
           Rpi5OglFboCurrentName(Context->FboState) == 0;
}

static BOOL
Rpi5OglRecordBatchDraw(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ ULONG FirstVertex,
    _In_ ULONG VertexCount,
    _In_ ULONG ShaderMode)
{
    PRPI5VC4_V3D_BATCH_REQUEST Request = Context->BatchRequest;
    PRPI5VC4_V3D_BATCH_DRAW Draw;
    ULONG Flags = 0;

    if (VertexCount == 0)
        return TRUE;
    if (Context->MesaContext->Depth.Test)
    {
        Flags = RPI5VC4_V3D_BATCH_FLAG_DEPTH_TEST |
                RPI5VC4_V3D_BATCH_FLAG_DEPTH_WRITE;
        if (Context->MesaContext->Depth.Func == GL_LEQUAL)
            Flags |= RPI5VC4_V3D_BATCH_FLAG_DEPTH_LEQUAL;
    }
    if (Context->MesaContext->Color.BlendEnabled)
        Flags |= RPI5VC4_V3D_BATCH_FLAG_BLEND_STANDARD;
    if (Request->DrawCount != 0)
    {
        Draw = &Request->Draws[Request->DrawCount - 1];
        if (Draw->Flags == Flags &&
            Draw->ShaderMode == ShaderMode &&
            Draw->FirstVertex + Draw->VertexCount == FirstVertex)
        {
            Draw->VertexCount += VertexCount;
            return TRUE;
        }
    }
    if (Request->DrawCount >= RPI5VC4_V3D_BATCH_MAX_DRAWS)
        return FALSE;
    Draw = &Request->Draws[Request->DrawCount++];
    Draw->FirstVertex = FirstVertex;
    Draw->VertexCount = VertexCount;
    Draw->Flags = Flags;
    Draw->ShaderMode = ShaderMode;
    return TRUE;
}

static BOOL
Rpi5OglDesktopBatchStateSupported(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;
    ULONG ExpectedRasterMask;

    if (Context == NULL ||
        !Rpi5OglGl2DesktopProgramActive(Context->Gl2State) ||
        !Rpi5OglResolveRenderTarget(Context, &Target))
    {
        return FALSE;
    }

    ExpectedRasterMask = Mesa->Color.BlendEnabled ? BLEND_BIT : 0;
    return Context->BatchRequest != NULL &&
           Target.HasColor &&
           Target.Width != 0 &&
           Target.Height != 0 &&
           Target.Width <= RPI5VC4_V3D_CLEAR_MAX_WIDTH &&
           Target.Height <= RPI5VC4_V3D_CLEAR_MAX_HEIGHT &&
           Mesa->Visual != NULL &&
           Mesa->Visual->RGBAflag &&
           Mesa->RenderMode == GL_RENDER &&
           Mesa->Texture.Enabled == 0 &&
           Mesa->RasterMask == ExpectedRasterMask &&
           (!Mesa->Color.BlendEnabled ||
            (Mesa->Color.BlendSrc == GL_SRC_ALPHA &&
             Mesa->Color.BlendDst == GL_ONE_MINUS_SRC_ALPHA)) &&
           !Mesa->Depth.Test &&
           !Mesa->Depth.Mask &&
           !Mesa->Polygon.Unfilled &&
           !Mesa->Polygon.OffsetAny &&
           !Mesa->Polygon.SmoothFlag &&
           !Mesa->Polygon.StippleFlag &&
           Mesa->Viewport.X == 0 &&
           Mesa->Viewport.Y == 0 &&
           Mesa->Viewport.Width == (GLsizei)Target.Width &&
           Mesa->Viewport.Height == (GLsizei)Target.Height &&
           Mesa->Viewport.Near == 0.0f &&
           Mesa->Viewport.Far == 1.0f &&
           Context->BufferMode != GL_NONE;
}

static BOOL
Rpi5OglStartDesktopBatch(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    GLcontext *Mesa = Context->MesaContext;

    if (Context->HardwareBatchActive ||
        !Rpi5OglDesktopBatchStateSupported(Mesa))
    {
        return FALSE;
    }

    Context->HardwareClearFresh = FALSE;
    Context->BatchVertexCount = 0;
    Context->BatchRequest->DrawCount = 0;
    ZeroMemory(Context->BatchRequest->Draws,
               sizeof(Context->BatchRequest->Draws));
    ZeroMemory(Context->BatchRequest->ShaderUniforms,
               sizeof(Context->BatchRequest->ShaderUniforms));
    Context->BatchProgramTriangleCount = 0;
    Context->BatchProgramName = 0;
    Context->BatchProgramFlags = 0;
    Context->BatchTextured = FALSE;
    Context->BatchTextureLinear = FALSE;
    Context->BatchTextureMipmap = FALSE;
    Context->BatchTextureGeneration = 0;
    Context->BatchTextureGeneration1 = 0;
    Context->BatchDepthFunc = GL_ALWAYS;
    Context->BatchBlendEnabled = Mesa->Color.BlendEnabled;
    Context->BatchPreserveDestinationAlphaBlend =
        Rpi5OglPreserveDestinationAlphaBlendEnabled(Mesa);
    Context->BatchDesktop2D = TRUE;
    Context->HardwareBatchActive = TRUE;
    return TRUE;
}

static BOOL
Rpi5OglStartJellyfishBatch(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    GLcontext *Mesa = Context->MesaContext;

    if (Context->HardwareBatchActive ||
        !Rpi5OglJellyfishBatchStateSupported(Mesa))
    {
        return FALSE;
    }

    Context->HardwareClearFresh = FALSE;
    Context->BatchVertexCount = 0;
    Context->BatchRequest->DrawCount = 0;
    ZeroMemory(Context->BatchRequest->Draws,
               sizeof(Context->BatchRequest->Draws));
    ZeroMemory(Context->BatchRequest->ShaderUniforms,
               sizeof(Context->BatchRequest->ShaderUniforms));
    Context->BatchProgramTriangleCount = 0;
    Context->BatchProgramName = 0;
    Context->BatchProgramFlags = 0;
    Context->BatchTextured = FALSE;
    Context->BatchTextureLinear = FALSE;
    Context->BatchTextureMipmap = FALSE;
    Context->BatchTextureGeneration = 0;
    Context->BatchTextureGeneration1 = 0;
    Context->BatchDepthFunc = GL_ALWAYS;
    Context->BatchBlendEnabled = TRUE;
    Context->BatchPreserveDestinationAlphaBlend = FALSE;
    Context->BatchDesktop2D = FALSE;
    Context->HardwareBatchActive = TRUE;
    return TRUE;
}

static BOOL
Rpi5OglStartDepthBatch(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    GLcontext *Mesa = Context->MesaContext;

    if (Context->HardwareBatchActive ||
        !Rpi5OglDepthBatchStateSupported(Mesa))
    {
        return FALSE;
    }

    Context->BatchVertexCount = 0;
    Context->BatchRequest->DrawCount = 0;
    ZeroMemory(Context->BatchRequest->Draws,
               sizeof(Context->BatchRequest->Draws));
    ZeroMemory(Context->BatchRequest->ShaderUniforms,
               sizeof(Context->BatchRequest->ShaderUniforms));
    Context->BatchProgramTriangleCount = 0;
    Context->BatchProgramName = 0;
    Context->BatchProgramFlags = 0;
    Context->BatchTextured = FALSE;
    Context->BatchTextureLinear = FALSE;
    Context->BatchTextureMipmap = FALSE;
    Context->BatchTextureGeneration = 0;
    Context->BatchTextureGeneration1 = 0;
    Context->BatchDepthFunc = Mesa->Depth.Func;
    Context->BatchBlendEnabled = FALSE;
    Context->BatchPreserveDestinationAlphaBlend = FALSE;
    Context->BatchDesktop2D = FALSE;
    Context->HardwareBatchActive = TRUE;
    return TRUE;
}

static BOOL
Rpi5OglStartShadowBatch(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    GLcontext *Mesa = Context->MesaContext;

    if (Context->HardwareBatchActive ||
        !Rpi5OglShadowBatchStateSupported(Mesa))
    {
        return FALSE;
    }

    Context->HardwareClearFresh = FALSE;
    Context->BatchVertexCount = 0;
    Context->BatchRequest->DrawCount = 0;
    ZeroMemory(Context->BatchRequest->Draws,
               sizeof(Context->BatchRequest->Draws));
    ZeroMemory(Context->BatchRequest->ShaderUniforms,
               sizeof(Context->BatchRequest->ShaderUniforms));
    Context->BatchProgramTriangleCount = 0;
    Context->BatchProgramName = 0;
    Context->BatchProgramFlags = 0;
    Context->BatchTextured = FALSE;
    Context->BatchTextureLinear = FALSE;
    Context->BatchTextureMipmap = FALSE;
    Context->BatchTextureGeneration = 0;
    Context->BatchTextureGeneration1 = 0;
    Context->BatchDepthFunc = Mesa->Depth.Func;
    Context->BatchBlendEnabled = FALSE;
    Context->BatchPreserveDestinationAlphaBlend = FALSE;
    Context->BatchDesktop2D = FALSE;
    Context->HardwareBatchActive = TRUE;
    return TRUE;
}

static BOOL
Rpi5OglCachedClipPositionVisible(
    _In_reads_(4) const GLfloat Position[4])
{
    GLfloat W = Position[3];

    return W > 0.0F &&
           Position[0] >= -W && Position[0] <= W &&
           Position[1] >= -W && Position[1] <= W &&
           Position[2] >= -W && Position[2] <= W;
}

static VOID
Rpi5OglMultiplyMatrices(
    _Out_writes_(16) GLfloat Result[16],
    _In_reads_(16) const GLfloat Left[16],
    _In_reads_(16) const GLfloat Right[16])
{
    GLfloat Product[16];
    ULONG Column;
    ULONG Row;

    for (Column = 0; Column < 4; Column++)
    {
        for (Row = 0; Row < 4; Row++)
        {
            Product[Column * 4 + Row] =
                Left[Row] * Right[Column * 4] +
                Left[4 + Row] * Right[Column * 4 + 1] +
                Left[8 + Row] * Right[Column * 4 + 2] +
                Left[12 + Row] * Right[Column * 4 + 3];
        }
    }
    CopyMemory(Result, Product, sizeof(Product));
}

static BOOL
Rpi5OglNormalMatrixIsOrthonormal(
    _In_reads_(16) const GLfloat Matrix[16])
{
    const GLfloat Epsilon = 0.0005F;
    GLfloat Length0 = Matrix[0] * Matrix[0] +
                      Matrix[1] * Matrix[1] +
                      Matrix[2] * Matrix[2];
    GLfloat Length1 = Matrix[4] * Matrix[4] +
                      Matrix[5] * Matrix[5] +
                      Matrix[6] * Matrix[6];
    GLfloat Length2 = Matrix[8] * Matrix[8] +
                      Matrix[9] * Matrix[9] +
                      Matrix[10] * Matrix[10];
    GLfloat Dot01 = Matrix[0] * Matrix[4] +
                    Matrix[1] * Matrix[5] +
                    Matrix[2] * Matrix[6];
    GLfloat Dot02 = Matrix[0] * Matrix[8] +
                    Matrix[1] * Matrix[9] +
                    Matrix[2] * Matrix[10];
    GLfloat Dot12 = Matrix[4] * Matrix[8] +
                    Matrix[5] * Matrix[9] +
                    Matrix[6] * Matrix[10];

    return Length0 > 1.0F - Epsilon && Length0 < 1.0F + Epsilon &&
           Length1 > 1.0F - Epsilon && Length1 < 1.0F + Epsilon &&
           Length2 > 1.0F - Epsilon && Length2 < 1.0F + Epsilon &&
           Dot01 > -Epsilon && Dot01 < Epsilon &&
           Dot02 > -Epsilon && Dot02 < Epsilon &&
           Dot12 > -Epsilon && Dot12 < Epsilon;
}

static BOOL
Rpi5OglExecuteCachedList(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _Inout_ PRPI5VC4_OGL_CACHED_LIST List)
{
    GLcontext *Mesa = Context->MesaContext;
    GLfloat ModelViewProjection[16];
    ULONG PositionVertex;
    ULONG TriangleIndex;
    ULONG OutputCount = 0;

    if (Mesa == NULL || Mesa->CompileFlag ||
        !Context->HardwareBatchActive ||
        Mesa->Light.ColorMaterialEnabled ||
        Mesa->Transform.AnyClip)
    {
        return FALSE;
    }

    gl_Materialfv(Mesa,
                  GL_FRONT,
                  GL_AMBIENT_AND_DIFFUSE,
                  List->Material);
    if (Mesa->NewModelViewMatrix)
        gl_analyze_modelview_matrix(Mesa);
    if (Mesa->NewProjectionMatrix)
        gl_analyze_projection_matrix(Mesa);
    if (Mesa->NewState)
        gl_update_state(Mesa);

    if (!Rpi5OglBatchStateSupported(Mesa) ||
        !Mesa->Light.Enabled ||
        !Mesa->Light.Fast ||
        Mesa->Light.Model.TwoSide)
    {
        return FALSE;
    }

    Rpi5OglMultiplyMatrices(ModelViewProjection,
                            Mesa->ProjectionMatrix,
                            Mesa->ModelViewMatrix);
    gl_xform_points_4fv(List->PositionCount,
                        List->ClipPosition,
                        ModelViewProjection,
                        List->ObjectPosition);
    if (Mesa->Transform.Normalize &&
        Rpi5OglNormalMatrixIsOrthonormal(Mesa->ModelViewInv))
    {
        gl_xform_normals_3fv(List->NormalCount,
                             List->EyeNormal,
                             Mesa->ModelViewInv,
                             List->UnitObjectNormal,
                             GL_FALSE);
    }
    else
    {
        gl_xform_normals_3fv(List->NormalCount,
                             List->EyeNormal,
                             Mesa->ModelViewInv,
                             List->ObjectNormal,
                             Mesa->Transform.Normalize);
    }
    if (!Rpi5OglShadeCachedVerticesNoSpecular(Mesa, List))
    {
        gl_color_shade_vertices_fast(Mesa,
                                     0,
                                     List->NormalCount,
                                     List->EyeNormal,
                                     List->VertexColor);
    }
    Rpi5OglCacheColorWords(List);

    for (PositionVertex = 0;
         PositionVertex < List->PositionCount;
         PositionVertex++)
    {
        GLfloat OneOverW;

        if (!Rpi5OglCachedClipPositionVisible(
                List->ClipPosition[PositionVertex]))
        {
            return FALSE;
        }

        OneOverW = 1.0F / List->ClipPosition[PositionVertex][3];
        List->NdcPosition[PositionVertex][0] =
            List->ClipPosition[PositionVertex][0] * OneOverW;
        List->NdcPosition[PositionVertex][1] =
            List->ClipPosition[PositionVertex][1] * OneOverW;
    }

    for (TriangleIndex = 0;
         TriangleIndex < List->TriangleCount;
         TriangleIndex += 2)
    {
        PRPI5VC4_OGL_LIST_TRIANGLE FirstTriangle =
            &List->Triangles[TriangleIndex];
        PRPI5VC4_OGL_LIST_TRIANGLE SecondTriangle =
            &List->Triangles[TriangleIndex + 1];
        const GLfloat *Position0 =
            List->NdcPosition[
                List->PositionMap[FirstTriangle->Vertex[0]]];
        const GLfloat *Position1 =
            List->NdcPosition[
                List->PositionMap[FirstTriangle->Vertex[1]]];
        const GLfloat *Position2 =
            List->NdcPosition[
                List->PositionMap[SecondTriangle->Vertex[1]]];
        const GLfloat *Position3 =
            List->NdcPosition[
                List->PositionMap[FirstTriangle->Vertex[2]]];
        GLfloat Ex = Position2[0] - Position0[0];
        GLfloat Ey = Position2[1] - Position0[1];
        GLfloat Fx = Position3[0] - Position1[0];
        GLfloat Fy = Position3[1] - Position1[1];
        GLfloat Area = Ex * Fy - Ey * Fx;
        ULONG Facing;
        ULONG SubTriangle;

        if (Area == 0.0F)
            continue;

        Facing = ((Area < 0.0F) ? 1u : 0u) ^
                 ((Mesa->Polygon.FrontFace == GL_CW) ? 1u : 0u);
        if (((Facing + 1) & Mesa->Polygon.CullBits) != 0)
            continue;

        for (SubTriangle = 0; SubTriangle < 2; SubTriangle++)
        {
            PRPI5VC4_OGL_LIST_TRIANGLE Triangle =
                &List->Triangles[TriangleIndex + SubTriangle];
            ULONG Vertex;

            for (Vertex = 0; Vertex < 3; Vertex++)
            {
                ULONG LogicalPosition = Triangle->Vertex[Vertex];
                ULONG LogicalColor = Triangle->ShadeModel == GL_FLAT ?
                                     Triangle->ProvokingVertex :
                                     LogicalPosition;
                ULONG PositionIndex =
                    List->PositionMap[LogicalPosition];
                ULONG ColorIndex = List->NormalMap[LogicalColor];

                Rpi5OglFillCachedTriangleVertex(
                    &List->OutputVertices[OutputCount + Vertex],
                    List->ClipPosition[PositionIndex],
                    List->VertexColorWord[ColorIndex]);
            }
            OutputCount += 3;
        }
    }

    if (OutputCount >
        RPI5VC4_V3D_BATCH_MAX_VERTICES - Context->BatchVertexCount)
    {
        return FALSE;
    }

    if (OutputCount != 0)
    {
        CopyMemory(&Context->BatchRequest->Vertices[
                       Context->BatchVertexCount],
                   List->OutputVertices,
                   OutputCount * sizeof(*List->OutputVertices));
        Context->BatchVertexCount += OutputCount;
    }

    gl_ShadeModel(Mesa, List->FinalShadeModel);
    CopyMemory(Mesa->Current.Normal,
               List->FinalNormal,
               sizeof(List->FinalNormal));
    Mesa->VB->MonoNormal = GL_FALSE;
    return TRUE;
}

static BOOL
Rpi5OglSubmitPrimitive(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLcontext *Mesa,
    _In_reads_(VertexCount) const RPI5VC4_V3D_VERTEX *Vertices,
    _In_ ULONG PrimitiveType,
    _In_ ULONG VertexCount,
    _In_ GLuint ProgramName)
{
    RPI5VC4_OGL_RENDER_TARGET Target;
    RPI5VC4_V3D_TRIANGLE_REQUEST Request;
    RPI5VC4_V3D_BLEND_STATE BlendState;
    PRPI5VC4_V3D_TRIANGLE_RESULT Result;
    ULONG HeaderSize = FIELD_OFFSET(RPI5VC4_V3D_TRIANGLE_RESULT, Pixels);
    ULONG Width;
    ULONG Height;
    ULONG PixelBytes;
    ULONG ResultSize;
    INT Returned;
    BOOL Success;

    if (Context == NULL ||
        !Rpi5OglTriangleStateSupported(Mesa) ||
        Vertices == NULL ||
        (PrimitiveType != RPI5VC4_V3D_PRIMITIVE_TRIANGLES &&
         PrimitiveType != RPI5VC4_V3D_PRIMITIVE_TRIANGLE_STRIP) ||
        ((PrimitiveType == RPI5VC4_V3D_PRIMITIVE_TRIANGLES &&
          VertexCount != 3) ||
         (PrimitiveType == RPI5VC4_V3D_PRIMITIVE_TRIANGLE_STRIP &&
          VertexCount != 4)) ||
        !Rpi5OglResolveRenderTarget(Context, &Target) ||
        !Context->HardwareClearFresh ||
        Context->HardwareClearFramebuffer != Target.Framebuffer ||
        Context->HardwareClearGeneration != Target.Generation)
    {
        return FALSE;
    }
    if (!Rpi5OglGl2GetBlendState(Context->Gl2State, &BlendState))
        return FALSE;

    Width = Mesa->Viewport.Width;
    Height = Mesa->Viewport.Height;
    if (Mesa->Viewport.X + (GLint)Width > (GLint)Target.Width ||
        Mesa->Viewport.Y + (GLint)Height > (GLint)Target.Height)
    {
        return FALSE;
    }

    PixelBytes = Width * Height * sizeof(ULONG);
    ResultSize = HeaderSize + PixelBytes;
    Context->HardwareClearFresh = FALSE;
    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ResultSize);
    if (Result == NULL)
        return FALSE;

    ZeroMemory(&Request, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Request.Width = Width;
    Request.Height = Height;
    Request.ClearColor = Context->HardwareClearColor;
    Request.PrimitiveType = PrimitiveType;
    Request.VertexCount = VertexCount;
    CopyMemory(&Request.BlendState,
               &BlendState,
               sizeof(Request.BlendState));
    CopyMemory(Request.Vertices,
               Vertices,
               VertexCount * sizeof(*Vertices));

    Returned = ExtEscape(Context->Hdc,
                         RPI5VC4_ESCAPE_RENDER_TRIANGLE,
                         sizeof(Request),
                         (LPCSTR)&Request,
                         ResultSize,
                         (LPSTR)Result);
    Context->Stats.LastTriangleStatus = Returned > 0 ?
                                        Result->Status : 0xFFFFFFFFu;
    Success = Returned >= (INT)ResultSize &&
              Result->Size == ResultSize &&
              Result->AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
              Result->Status == RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS &&
              (Result->Flags & RPI5VC4_V3D_SELFTEST_FLAG_PASSED) != 0 &&
              Result->Width == Width &&
              Result->Height == Height &&
              Result->Stride == Width * sizeof(ULONG) &&
              Result->ClearColor == Context->HardwareClearColor &&
              Result->PixelBytes == PixelBytes &&
              Result->PrimitiveType == PrimitiveType &&
              Result->VertexCount == VertexCount;
    if (Success)
    {
        Rpi5OglDiscardDeferredTargetClear(Context, &Target);
        Rpi5OglCopyTargetPixels(&Target,
                                Mesa->Viewport.X,
                                Mesa->Viewport.Y,
                                Result->Pixels,
                                Width,
                                Height);
        Context->Stats.HardwareTriangleCount++;
        if (ProgramName != 0)
        {
            Context->Stats.HardwareProgramTriangleCount++;
            Context->Stats.LastProgramTriangleName = ProgramName;
        }
        Context->Stats.LastTriangleWidth = Width;
        Context->Stats.LastTriangleHeight = Height;
        Context->Stats.LastTriangleCoveredPixels =
            Result->CoveredPixelCount;
    }
    else
    {
        Context->Stats.HardwareTriangleFailureCount++;
    }

    HeapFree(GetProcessHeap(), 0, Result);
    return Success;
}

static BOOL
Rpi5OglHardwareTriangle(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint ProvokingVertex)
{
    const GLuint VertexIndices[3] = {Vertex0, Vertex1, Vertex2};
    RPI5VC4_V3D_VERTEX Vertices[3];
    struct vertex_buffer *VertexBuffer = Mesa->VB;
    ULONG Vertex;

    if (VertexBuffer == NULL ||
        VertexBuffer->Color == NULL ||
        Vertex0 >= VB_SIZE ||
        Vertex1 >= VB_SIZE ||
        Vertex2 >= VB_SIZE ||
        ProvokingVertex >= VB_SIZE)
    {
        return FALSE;
    }

    for (Vertex = 0; Vertex < RTL_NUMBER_OF(VertexIndices); Vertex++)
    {
        GLuint ColorVertex = Mesa->Light.ShadeModel == GL_FLAT ?
                             ProvokingVertex : VertexIndices[Vertex];

        Rpi5OglFillTriangleVertex(
            &Vertices[Vertex],
            VertexBuffer->Clip[VertexIndices[Vertex]],
            VertexBuffer->Color[ColorVertex]);
    }
    return Rpi5OglSubmitPrimitive(
        Context,
        Mesa,
        Vertices,
        RPI5VC4_V3D_PRIMITIVE_TRIANGLES,
        RTL_NUMBER_OF(Vertices),
        Rpi5OglGl2CurrentProgramName(Context->Gl2State));
}

static VOID APIENTRY
Rpi5OglApiEndList(VOID)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();
    GLuint Name = 0;

    if (Context != NULL &&
        Context->MesaContext->CurrentListPtr != NULL)
    {
        Name = Context->MesaContext->CurrentListNum;
    }

    _mesa_EndList();
    if (Context != NULL && Name != 0 &&
        Context->MesaContext->CurrentListPtr == NULL)
    {
        Rpi5OglBuildCachedList(Context, Name);
    }
}

static VOID APIENTRY
Rpi5OglApiCallList(
    _In_ GLuint Name)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();
    PRPI5VC4_OGL_CACHED_LIST List = Context != NULL ?
        Rpi5OglFindCachedList(Context, Name) : NULL;

    if (List != NULL && Rpi5OglExecuteCachedList(Context, List))
        return;
    _mesa_CallList(Name);
}

static VOID APIENTRY
Rpi5OglApiDeleteLists(
    _In_ GLuint Name,
    _In_ GLsizei Range)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();
    GLsizei Index;

    if (Context != NULL && Range > 0)
    {
        for (Index = 0; Index < Range; Index++)
            Rpi5OglRemoveCachedList(Context, Name + (GLuint)Index);
    }
    _mesa_DeleteLists(Name, Range);
}

static VOID APIENTRY
Rpi5OglApiClear(
    _In_ GLbitfield Mask)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();
    RPI5VC4_OGL_RENDER_TARGET Target;

    if (Context == NULL ||
        !Rpi5OglFboValidateCurrent(Context->FboState, "glClear"))
    {
        return;
    }
    _mesa_Clear(Mask);
    if ((Mask & GL_DEPTH_BUFFER_BIT) != 0)
    {
        Context->HardwareClearFresh = FALSE;
        if (Context->MesaContext->Depth.Mask &&
            Context->MesaContext->Depth.Clear == 1.0f &&
            !Context->MesaContext->Scissor.Enabled &&
            Rpi5OglResolveRenderTarget(Context, &Target) &&
            Target.Framebuffer != 0 &&
            Target.HasDepth &&
            Target.DepthTexture != NULL &&
            Target.DepthData != NULL &&
            Target.DepthFormat == GL_DEPTH_COMPONENT)
        {
            Context->HardwareClearFresh = TRUE;
            Context->HardwareClearColor = Context->ClearColor;
            Context->HardwareClearFramebuffer = Target.Framebuffer;
            Context->HardwareClearGeneration = Target.Generation;
        }
    }
}

static VOID APIENTRY
Rpi5OglApiBlendFunc(
    _In_ GLenum Source,
    _In_ GLenum Destination)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    if (Context == NULL)
        return;
    Rpi5OglGl2BlendFunc(Context->Gl2State, Source, Destination);
}

static VOID APIENTRY
Rpi5OglDrawArrays(
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();
    RPI5VC4_OGL_GL2_VERTEX
        Gl2Vertices[RPI5VC4_V3D_PRIMITIVE_MAX_VERTICES];
    RPI5VC4_V3D_VERTEX Vertices[RPI5VC4_V3D_PRIMITIVE_MAX_VERTICES];
    RPI5VC4_OGL_GL2_DRAW_RESULT DrawResult;
    ULONG PrimitiveType;
    ULONG VertexCount;
    ULONG OutputVertexCount;
    ULONG OutputTriangleCount;
    ULONG Vertex;
    BOOL Textured;
    BOOL LinearTextureFilter;
    BOOL MipmapTextureFilter;
    ULONG TextureGeneration;
    ULONG TextureGeneration1;
    ULONG ProgramFlags;
    ULONG NormalMatrix[RPI5VC4_V3D_NORMAL_MATRIX_WORDS];
    PRPI5VC4_V3D_TEXCOORD HeightTexCoords;
    ULONG OutputCapacity;
    BOOL Desktop2D;
    RPI5VC4_OGL_GRAPH_DRAW_RESULT GraphDrawResult;
    BOOL Ideas;
    BOOL Jellyfish;
    BOOL Terrain;
    BOOL Depth;
    BOOL Shadow;
    BOOL ShadowSceneContinuation;

    if (Context == NULL ||
        !Rpi5OglFboValidateCurrent(Context->FboState, "glDrawArrays"))
        return;
    DrawResult = Rpi5OglGl2PrepareBatchDraw(Context->Gl2State,
                                            Mode,
                                            First,
                                            Count);
    if (DrawResult == Rpi5OglGl2DrawReady)
    {
        if (Count == 0)
            return;
        if (Context->MesaContext->NewState)
            gl_update_state(Context->MesaContext);
        Desktop2D = Rpi5OglGl2DesktopProgramActive(Context->Gl2State);
        Terrain = Rpi5OglGl2TerrainProgramActive(Context->Gl2State);
        ProgramFlags = Rpi5OglGl2BatchFlags(Context->Gl2State);
        Ideas = (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_IDEAS) != 0;
        Jellyfish =
            (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_JELLYFISH) != 0;
        Depth =
            (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_OUTPUT_DEPTH) != 0;
        Shadow =
            (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_SHADOW) != 0;
        ShadowSceneContinuation =
            !Shadow &&
            Rpi5OglGl2BuildDiffuseProgramActive(Context->Gl2State) &&
            Context->HardwareBatchActive &&
            Context->BatchVertexCount != 0 &&
            (Context->BatchProgramFlags &
             RPI5VC4_V3D_BATCH_FLAG_SHADOW) != 0;
        if (Terrain)
        {
            GraphDrawResult = Rpi5OglRecordTerrainGraphDraw(Context,
                                                            Mode,
                                                            First,
                                                            Count);
            if (GraphDrawResult == Rpi5OglGraphDrawHandled)
                return;
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 terrain render graph)");
            return;
        }
        if (Context->TerrainGraphActive)
            Rpi5OglResetDesktopGraph(Context);
        if (Desktop2D)
        {
            GraphDrawResult = Rpi5OglRecordDesktopGraphDraw(Context,
                                                            Mode,
                                                            First,
                                                            Count);
            if (GraphDrawResult == Rpi5OglGraphDrawHandled)
                return;
            if (GraphDrawResult == Rpi5OglGraphDrawFailed)
            {
                gl_error(Context->MesaContext,
                         GL_INVALID_OPERATION,
                         "glDrawArrays(RPi5 desktop render graph)");
                return;
            }
        }
        if (Desktop2D && !Context->HardwareBatchActive &&
            !Rpi5OglStartDesktopBatch(Context))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 desktop batch start)");
            return;
        }
        if (Jellyfish && !Context->HardwareBatchActive &&
            !Rpi5OglStartJellyfishBatch(Context))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 jellyfish batch start)");
            return;
        }
        if (Depth && !Context->HardwareBatchActive &&
            !Rpi5OglStartDepthBatch(Context))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 depth batch start)");
            return;
        }
        if (Shadow && !Context->HardwareBatchActive &&
            !Rpi5OglStartShadowBatch(Context))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 shadow batch start)");
            return;
        }
        if (!Context->HardwareBatchActive ||
            !(Desktop2D ?
                  Rpi5OglDesktopBatchStateSupported(Context->MesaContext) :
              Depth ?
                  Rpi5OglDepthBatchStateSupported(Context->MesaContext) :
              Shadow ?
                  Rpi5OglShadowBatchStateSupported(Context->MesaContext) :
              ShadowSceneContinuation ?
                  Rpi5OglBatchStateSupported(Context->MesaContext) :
              Jellyfish ?
                  Rpi5OglJellyfishBatchStateSupported(Context->MesaContext) :
              Ideas ?
                  Rpi5OglIdeasBatchStateSupported(Context->MesaContext) :
                  Rpi5OglBatchStateSupported(Context->MesaContext)) ||
            (!Desktop2D && !Ideas && !Jellyfish &&
             Context->BatchDepthFunc != Context->MesaContext->Depth.Func) ||
            Context->BatchBlendEnabled !=
                Context->MesaContext->Color.BlendEnabled ||
            Context->BatchPreserveDestinationAlphaBlend !=
                Rpi5OglPreserveDestinationAlphaBlendEnabled(
                    Context->MesaContext))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 GL2 batch state)");
            return;
        }
        Rpi5OglMaterializeTextureClear(Rpi5OglGl2TextureForUnit(
            Context->Gl2State, RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY));
        if (Jellyfish)
        {
            Rpi5OglMaterializeTextureClear(Rpi5OglGl2TextureForUnit(
                Context->Gl2State,
                RPI5VC4_V3D_TEXTURE_SLOT_SECONDARY));
        }
        if (!Rpi5OglGl2PrepareTexture(Context->Gl2State,
                                      Context->Hdc,
                                      &Textured,
                                      &LinearTextureFilter,
                                      &MipmapTextureFilter,
                                      &TextureGeneration,
                                      &TextureGeneration1))
        {
            return;
        }
        ZeroMemory(NormalMatrix, sizeof(NormalMatrix));
        if ((ProgramFlags &
             (RPI5VC4_V3D_BATCH_FLAG_NORMAL_MAP |
              RPI5VC4_V3D_BATCH_FLAG_HEIGHT_MAP)) != 0 &&
            !Rpi5OglGl2GetNormalMatrix(Context->Gl2State, NormalMatrix))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 normal matrix)");
            return;
        }
        if (Context->BatchVertexCount != 0 &&
            !ShadowSceneContinuation &&
            (Context->BatchProgramFlags != ProgramFlags ||
             (!Jellyfish &&
              (Context->BatchTextured != Textured ||
               (Textured &&
                (Context->BatchTextureLinear != LinearTextureFilter ||
                 Context->BatchTextureMipmap != MipmapTextureFilter ||
                 Context->BatchTextureGeneration != TextureGeneration)))) ||
             (Jellyfish && Context->BatchTextured && Textured &&
              (Context->BatchTextureLinear != LinearTextureFilter ||
               Context->BatchTextureMipmap != MipmapTextureFilter ||
               Context->BatchTextureGeneration != TextureGeneration ||
               Context->BatchTextureGeneration1 != TextureGeneration1)) ||
             ((ProgramFlags &
               (RPI5VC4_V3D_BATCH_FLAG_NORMAL_MAP |
                RPI5VC4_V3D_BATCH_FLAG_HEIGHT_MAP)) != 0 &&
              memcmp(Context->BatchRequest->NormalMatrix,
                     NormalMatrix,
                     sizeof(NormalMatrix)) != 0)))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 mixed batch texture state)");
            return;
        }
        if (Context->BatchVertexCount == 0)
        {
            CopyMemory(Context->BatchRequest->NormalMatrix,
                       NormalMatrix,
                       sizeof(NormalMatrix));
        }
        HeightTexCoords = NULL;
        OutputCapacity = RPI5VC4_V3D_BATCH_MAX_VERTICES -
                         Context->BatchVertexCount;
        if ((ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_HEIGHT_MAP) != 0)
        {
            if (Context->BatchVertexCount >=
                RPI5VC4_V3D_HEIGHT_MAX_VERTICES)
            {
                gl_error(Context->MesaContext,
                         GL_INVALID_OPERATION,
                         "glDrawArrays(RPi5 height batch capacity)");
                return;
            }
            OutputCapacity = RPI5VC4_V3D_HEIGHT_MAX_VERTICES -
                             Context->BatchVertexCount;
            HeightTexCoords =
                (PRPI5VC4_V3D_TEXCOORD)((PUCHAR)
                    Context->BatchRequest->Vertices +
                    RPI5VC4_V3D_BATCH_MAX_VERTICES *
                    sizeof(RPI5VC4_V3D_VERTEX)) +
                Context->BatchVertexCount;
        }
        else if (Jellyfish)
        {
            if (Context->BatchVertexCount >=
                RPI5VC4_V3D_JELLYFISH_MAX_VERTICES)
            {
                gl_error(Context->MesaContext,
                         GL_INVALID_OPERATION,
                         "glDrawArrays(RPi5 jellyfish batch capacity)");
                return;
            }
            OutputCapacity = RPI5VC4_V3D_JELLYFISH_MAX_VERTICES -
                             Context->BatchVertexCount;
            HeightTexCoords =
                (PRPI5VC4_V3D_TEXCOORD)((PUCHAR)
                    Context->BatchRequest->Vertices +
                    RPI5VC4_V3D_BATCH_MAX_VERTICES *
                    sizeof(RPI5VC4_V3D_VERTEX)) +
                Context->BatchVertexCount;
        }
        if (!Rpi5OglGl2BuildBatch(
                Context->Gl2State,
                Mode,
                First,
                Count,
                Context->MesaContext->Polygon.FrontFace,
                Context->MesaContext->Polygon.CullBits,
                &Context->BatchRequest->Vertices[Context->BatchVertexCount],
                HeightTexCoords,
                OutputCapacity,
                &OutputVertexCount,
                &OutputTriangleCount))
        {
            return;
        }
        if ((Ideas || Jellyfish || Shadow || ShadowSceneContinuation) &&
            !Rpi5OglRecordBatchDraw(Context,
                                    Context->BatchVertexCount,
                                    OutputVertexCount,
                                    Shadow ?
                                        RPI5VC4_V3D_SHADOW_MODE_GROUND :
                                    ShadowSceneContinuation ?
                                        RPI5VC4_V3D_SHADOW_MODE_COLOR :
                                    Jellyfish ?
                                        Rpi5OglGl2JellyfishMode(
                                            Context->Gl2State) :
                                        Rpi5OglGl2IdeasMode(
                                            Context->Gl2State)))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 shader draw ranges)");
            return;
        }
        Context->BatchVertexCount += OutputVertexCount;
        Context->BatchProgramTriangleCount += OutputTriangleCount;
        if (!ShadowSceneContinuation)
        {
            Context->BatchProgramName =
                Rpi5OglGl2CurrentProgramName(Context->Gl2State);
            Context->BatchProgramFlags = ProgramFlags;
            if (!Jellyfish || Textured)
            {
                Context->BatchTextured = Textured;
                Context->BatchTextureLinear = LinearTextureFilter;
                Context->BatchTextureMipmap = MipmapTextureFilter;
                Context->BatchTextureGeneration = TextureGeneration;
                Context->BatchTextureGeneration1 = TextureGeneration1;
            }
        }
        if (Desktop2D && !Rpi5OglSubmitBatch(Context, TRUE))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 desktop submission)");
        }
        if (Depth && !Rpi5OglSubmitBatch(Context, FALSE))
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawArrays(RPi5 depth submission)");
        }
        return;
    }
    if (DrawResult == Rpi5OglGl2DrawRejected)
        return;
    DrawResult = Rpi5OglGl2BuildPrimitive(Context->Gl2State,
                                          Mode,
                                          First,
                                          Count,
                                          Gl2Vertices);
    if (DrawResult == Rpi5OglGl2DrawNotApplicable)
    {
        if (Context->TerrainGraphActive)
            Rpi5OglResetDesktopGraph(Context);
        Rpi5OglMaterializeTextureClear(
            Context->MesaContext->Texture.Current2D);
        _mesa_DrawArrays(Mode, First, Count);
        return;
    }
    if (DrawResult == Rpi5OglGl2DrawRejected)
        return;

    PrimitiveType = Mode == GL_TRIANGLE_STRIP ?
                    RPI5VC4_V3D_PRIMITIVE_TRIANGLE_STRIP :
                    RPI5VC4_V3D_PRIMITIVE_TRIANGLES;
    VertexCount = (ULONG)Count;
    for (Vertex = 0; Vertex < VertexCount; Vertex++)
    {
        Rpi5OglFillTriangleVertex(&Vertices[Vertex],
                                  Gl2Vertices[Vertex].Position,
                                  Gl2Vertices[Vertex].Color);
    }
    if (!Rpi5OglSubmitPrimitive(
            Context,
            Context->MesaContext,
            Vertices,
            PrimitiveType,
            VertexCount,
            Rpi5OglGl2CurrentProgramName(Context->Gl2State)))
    {
        gl_error(Context->MesaContext,
                 GL_INVALID_OPERATION,
                 "glDrawArrays(RPi5 V3D submission)");
    }
}

static VOID APIENTRY
Rpi5OglDrawElements(
    _In_ GLenum Mode,
    _In_ GLsizei Count,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Indices)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();
    RPI5VC4_OGL_GL2_DRAW_RESULT DrawResult;
    GLuint *IndexValues = NULL;
    GLuint MaximumIndex = 0;
    SIZE_T IndexBytes;
    ULONG ProgramFlags;
    BOOL Textured;
    BOOL LinearTextureFilter;
    BOOL MipmapTextureFilter;
    ULONG TextureGeneration;
    ULONG TextureGeneration1;
    ULONG OutputVertexCount;
    ULONG OutputTriangleCount;
    ULONG OutputCapacity;
    PRPI5VC4_V3D_TEXCOORD AuxiliaryTexCoords = NULL;
    BOOL Jellyfish;

    if (Context == NULL ||
        !Rpi5OglFboValidateCurrent(Context->FboState, "glDrawElements"))
    {
        return;
    }
    DrawResult = Rpi5OglGl2PrepareBatchDraw(Context->Gl2State,
                                            Mode,
                                            0,
                                            Count);
    if (DrawResult == Rpi5OglGl2DrawNotApplicable)
    {
        if (Context->TerrainGraphActive)
            Rpi5OglResetDesktopGraph(Context);
        Rpi5OglMaterializeTextureClear(
            Context->MesaContext->Texture.Current2D);
        _mesa_DrawElements(Mode, Count, Type, Indices);
        return;
    }
    if (DrawResult == Rpi5OglGl2DrawRejected || Count == 0)
        return;
    if (Context->MesaContext->NewState)
        gl_update_state(Context->MesaContext);

    if (Rpi5OglGl2TerrainProgramActive(Context->Gl2State))
    {
        Rpi5OglResetDesktopGraph(Context);
        gl_error(Context->MesaContext,
                 GL_INVALID_OPERATION,
                 "glDrawElements(RPi5 terrain topology)");
        return;
    }
    if (Context->TerrainGraphActive)
        Rpi5OglResetDesktopGraph(Context);

    ProgramFlags = Rpi5OglGl2BatchFlags(Context->Gl2State);
    Jellyfish =
        (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_JELLYFISH) != 0;
    if (Jellyfish && !Context->HardwareBatchActive &&
        !Rpi5OglStartJellyfishBatch(Context))
    {
        gl_error(Context->MesaContext,
                 GL_INVALID_OPERATION,
                 "glDrawElements(RPi5 jellyfish batch start)");
        return;
    }
    if ((ProgramFlags != RPI5VC4_V3D_BATCH_FLAG_IDEAS && !Jellyfish) ||
        !Context->HardwareBatchActive ||
        !(Jellyfish ?
              Rpi5OglJellyfishBatchStateSupported(Context->MesaContext) :
              Rpi5OglIdeasBatchStateSupported(Context->MesaContext)) ||
        Context->BatchBlendEnabled !=
            Context->MesaContext->Color.BlendEnabled ||
        Context->BatchPreserveDestinationAlphaBlend !=
            Rpi5OglPreserveDestinationAlphaBlendEnabled(
                Context->MesaContext))
    {
        gl_error(Context->MesaContext,
                 GL_INVALID_OPERATION,
                 "glDrawElements(RPi5 shader batch state)");
        return;
    }
    if ((SIZE_T)Count > (SIZE_T)-1 / sizeof(*IndexValues))
    {
        gl_error(Context->MesaContext,
                 GL_OUT_OF_MEMORY,
                 "glDrawElements(RPi5 index allocation)");
        return;
    }
    IndexBytes = (SIZE_T)Count * sizeof(*IndexValues);
    IndexValues = HeapAlloc(GetProcessHeap(), 0, IndexBytes);
    if (IndexValues == NULL)
    {
        gl_error(Context->MesaContext,
                 GL_OUT_OF_MEMORY,
                 "glDrawElements(RPi5 index allocation)");
        return;
    }
    if (!Rpi5OglBufferReadElementIndices(Context->BufferState,
                                         Type,
                                         Indices,
                                         Count,
                                         IndexValues,
                                         &MaximumIndex))
    {
        goto Done;
    }

    Rpi5OglMaterializeTextureClear(Rpi5OglGl2TextureForUnit(
        Context->Gl2State, RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY));
    if (Jellyfish)
    {
        Rpi5OglMaterializeTextureClear(Rpi5OglGl2TextureForUnit(
            Context->Gl2State, RPI5VC4_V3D_TEXTURE_SLOT_SECONDARY));
    }
    if (!Rpi5OglGl2PrepareTexture(Context->Gl2State,
                                  Context->Hdc,
                                  &Textured,
                                  &LinearTextureFilter,
                                  &MipmapTextureFilter,
                                  &TextureGeneration,
                                  &TextureGeneration1))
    {
        goto Done;
    }
    if (!Textured || MipmapTextureFilter ||
        (Context->BatchVertexCount != 0 && Context->BatchTextured &&
         (Context->BatchProgramFlags != ProgramFlags ||
          Context->BatchTextureLinear != LinearTextureFilter ||
          Context->BatchTextureMipmap != MipmapTextureFilter ||
          Context->BatchTextureGeneration != TextureGeneration ||
          (Jellyfish &&
           Context->BatchTextureGeneration1 != TextureGeneration1))))
    {
        gl_error(Context->MesaContext,
                 GL_INVALID_OPERATION,
                 "glDrawElements(RPi5 shader texture state)");
        goto Done;
    }

    OutputCapacity = RPI5VC4_V3D_BATCH_MAX_VERTICES -
                     Context->BatchVertexCount;
    if (Jellyfish)
    {
        if (Context->BatchVertexCount >=
            RPI5VC4_V3D_JELLYFISH_MAX_VERTICES)
        {
            gl_error(Context->MesaContext,
                     GL_INVALID_OPERATION,
                     "glDrawElements(RPi5 jellyfish batch capacity)");
            goto Done;
        }
        OutputCapacity = RPI5VC4_V3D_JELLYFISH_MAX_VERTICES -
                         Context->BatchVertexCount;
        AuxiliaryTexCoords =
            (PRPI5VC4_V3D_TEXCOORD)((PUCHAR)
                Context->BatchRequest->Vertices +
                RPI5VC4_V3D_BATCH_MAX_VERTICES *
                sizeof(RPI5VC4_V3D_VERTEX)) +
            Context->BatchVertexCount;
    }
    if (!Rpi5OglGl2BuildIndexedBatch(
            Context->Gl2State,
            Mode,
            IndexValues,
            Count,
            MaximumIndex,
            Context->MesaContext->Polygon.FrontFace,
            Context->MesaContext->Polygon.CullBits,
            &Context->BatchRequest->Vertices[Context->BatchVertexCount],
            AuxiliaryTexCoords,
            OutputCapacity,
            &OutputVertexCount,
            &OutputTriangleCount))
    {
        goto Done;
    }
    if (!Rpi5OglRecordBatchDraw(Context,
                                Context->BatchVertexCount,
                                OutputVertexCount,
                                Jellyfish ?
                                    Rpi5OglGl2JellyfishMode(
                                        Context->Gl2State) :
                                    Rpi5OglGl2IdeasMode(Context->Gl2State)))
    {
        gl_error(Context->MesaContext,
                 GL_INVALID_OPERATION,
                 "glDrawElements(RPi5 shader draw ranges)");
        goto Done;
    }

    Context->BatchVertexCount += OutputVertexCount;
    Context->BatchProgramTriangleCount += OutputTriangleCount;
    Context->BatchProgramName =
        Rpi5OglGl2CurrentProgramName(Context->Gl2State);
    Context->BatchProgramFlags = ProgramFlags;
    Context->BatchTextured = Textured;
    Context->BatchTextureLinear = LinearTextureFilter;
    Context->BatchTextureMipmap = MipmapTextureFilter;
    Context->BatchTextureGeneration = TextureGeneration;
    Context->BatchTextureGeneration1 = TextureGeneration1;

Done:
    HeapFree(GetProcessHeap(), 0, IndexValues);
}

static VOID APIENTRY
Rpi5OglDrawRangeElements(
    _In_ GLenum Mode,
    _In_ GLuint Start,
    _In_ GLuint End,
    _In_ GLsizei Count,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Indices)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    if (Context == NULL)
        return;
    if (End < Start)
    {
        gl_error(Context->MesaContext,
                 GL_INVALID_VALUE,
                 "glDrawRangeElements(end < start)");
        return;
    }

    Rpi5OglDrawElements(Mode, Count, Type, Indices);
}

static VOID
Rpi5OglSoftwareTriangle(
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint ProvokingVertex)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;
    triangle_func DriverTriangle = Mesa->Driver.TriangleFunc;
    triangle_func SoftwareTriangle;

    if (Rpi5OglResolveRenderTarget(Context, &Target))
        Rpi5OglMaterializeDeferredTargetClear(Context, &Target);
    Rpi5OglMaterializeTextureClear(Mesa->Texture.Current2D);
    Context->HardwareClearFresh = FALSE;
    Context->Stats.SoftwareTriangleCount++;
    Mesa->Driver.TriangleFunc = NULL;
    gl_set_triangle_function(Mesa);
    SoftwareTriangle = Mesa->Driver.TriangleFunc;
    Mesa->Driver.TriangleFunc = DriverTriangle;
    if (SoftwareTriangle != NULL && SoftwareTriangle != DriverTriangle)
    {
        SoftwareTriangle(Mesa,
                         Vertex0,
                         Vertex1,
                         Vertex2,
                         ProvokingVertex);
    }
}

static BOOL
Rpi5OglAppendBatchTriangle(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint ProvokingVertex)
{
    const GLuint VertexIndices[3] = {Vertex0, Vertex1, Vertex2};
    struct vertex_buffer *VertexBuffer = Mesa->VB;
    ULONG Vertex;

    if (!Context->HardwareBatchActive ||
        VertexBuffer == NULL ||
        VertexBuffer->Color == NULL ||
        Vertex0 >= VB_SIZE ||
        Vertex1 >= VB_SIZE ||
        Vertex2 >= VB_SIZE ||
        ProvokingVertex >= VB_SIZE ||
        Context->BatchVertexCount >
            RPI5VC4_V3D_BATCH_MAX_VERTICES - 3)
    {
        return FALSE;
    }

    for (Vertex = 0; Vertex < RTL_NUMBER_OF(VertexIndices); Vertex++)
    {
        GLuint ColorVertex = Mesa->Light.ShadeModel == GL_FLAT ?
                             ProvokingVertex : VertexIndices[Vertex];

        Rpi5OglFillTriangleVertex(
            &Context->BatchRequest->Vertices[Context->BatchVertexCount++],
            VertexBuffer->Clip[VertexIndices[Vertex]],
            VertexBuffer->Color[ColorVertex]);
    }
    return TRUE;
}

static VOID
Rpi5OglBatchTriangle(
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint ProvokingVertex)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    if (!Rpi5OglFboValidateCurrent(Context->FboState,
                                   "glBegin/glEnd batch triangle"))
    {
        return;
    }

    if (Context->HardwareBatchActive)
    {
        if (!Rpi5OglAppendBatchTriangle(Context,
                                        Mesa,
                                        Vertex0,
                                        Vertex1,
                                        Vertex2,
                                        ProvokingVertex))
        {
            Context->HardwareBatchActive = FALSE;
            Context->Stats.HardwareTriangleFailureCount++;
            gl_error(Mesa,
                     GL_OUT_OF_MEMORY,
                     "RPi5 V3D triangle batch");
        }
        return;
    }

    Rpi5OglSoftwareTriangle(Mesa,
                            Vertex0,
                            Vertex1,
                            Vertex2,
                            ProvokingVertex);
}

static VOID
Rpi5OglBatchQuad(
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint Vertex3,
    _In_ GLuint ProvokingVertex)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    if (Context->HardwareBatchActive &&
        Context->BatchVertexCount <=
            RPI5VC4_V3D_BATCH_MAX_VERTICES - 6)
    {
        if (Rpi5OglAppendBatchTriangle(Context,
                                       Mesa,
                                       Vertex0,
                                       Vertex1,
                                       Vertex3,
                                       ProvokingVertex) &&
            Rpi5OglAppendBatchTriangle(Context,
                                       Mesa,
                                       Vertex1,
                                       Vertex2,
                                       Vertex3,
                                       ProvokingVertex))
        {
            return;
        }
        Context->HardwareBatchActive = FALSE;
        Context->Stats.HardwareTriangleFailureCount++;
        gl_error(Mesa, GL_OUT_OF_MEMORY, "RPi5 V3D quad batch");
        return;
    }

    Rpi5OglSoftwareTriangle(Mesa,
                            Vertex0,
                            Vertex1,
                            Vertex3,
                            ProvokingVertex);
    Rpi5OglSoftwareTriangle(Mesa,
                            Vertex1,
                            Vertex2,
                            Vertex3,
                            ProvokingVertex);
}

static BOOL
Rpi5OglBatchPixelBounds(
    _In_reads_(VertexCount) const RPI5VC4_V3D_VERTEX *Vertices,
    _In_ ULONG VertexCount,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _Out_ PULONG MinimumX,
    _Out_ PULONG MinimumY,
    _Out_ PULONG MaximumX,
    _Out_ PULONG MaximumY)
{
    double MinX = Width;
    double MinY = Height;
    double MaxX = 0.0;
    double MaxY = 0.0;
    ULONG Vertex;

    for (Vertex = 0; Vertex < VertexCount; Vertex++)
    {
        const GLfloat *Position =
            (const GLfloat *)Vertices[Vertex].Position;
        double W = Position[3];
        double X;
        double Y;

        if (W <= 0.0)
            return FALSE;
        X = (Position[0] / W * 0.5 + 0.5) * Width;
        Y = (Position[1] / W * 0.5 + 0.5) * Height;
        if (X < MinX)
            MinX = X;
        if (Y < MinY)
            MinY = Y;
        if (X > MaxX)
            MaxX = X;
        if (Y > MaxY)
            MaxY = Y;
    }

    if (MinX < 0.0)
        MinX = 0.0;
    if (MinY < 0.0)
        MinY = 0.0;
    if (MaxX > Width)
        MaxX = Width;
    if (MaxY > Height)
        MaxY = Height;
    *MinimumX = (ULONG)floor(MinX);
    *MinimumY = (ULONG)floor(MinY);
    *MaximumX = (ULONG)ceil(MaxX);
    *MaximumY = (ULONG)ceil(MaxY);
    return *MaximumX > *MinimumX && *MaximumY > *MinimumY;
}

static VOID
Rpi5OglResetDesktopGraph(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    if (Context->TerrainGraphActive && Context->TerrainVertices != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Context->TerrainVertices);
        Context->TerrainVertices = NULL;
    }
    Context->DesktopGraphActive = FALSE;
    Context->TerrainGraphActive = FALSE;
    Context->TerrainGraphSetupPassCount = 0;
    Context->TerrainGraphExpectedMode = 0;
    Context->DesktopGraphResourceCount = 0;
    Context->DesktopGraphPassCount = 0;
    Context->DesktopGraphReadbackResource = 0xFFFFFFFFu;
    ZeroMemory(Context->DesktopGraphResources,
               sizeof(Context->DesktopGraphResources));
    ZeroMemory(Context->DesktopGraphPasses,
               sizeof(Context->DesktopGraphPasses));
}

static LONG
Rpi5OglFindDesktopGraphResource(
    _In_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ struct gl_texture_object *Texture)
{
    ULONG Index;

    for (Index = 0; Index < Context->DesktopGraphResourceCount; Index++)
    {
        if (Context->DesktopGraphResources[Index].Texture == Texture)
            return (LONG)Index;
    }
    return -1;
}

static LONG
Rpi5OglAddDesktopGraphSource(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ const RPI5VC4_OGL_GL2_TEXTURE_INFO *TextureInfo)
{
    LONG Existing;
    PRPI5VC4_OGL_GRAPH_RESOURCE Resource;

    Existing = Rpi5OglFindDesktopGraphResource(Context,
                                               TextureInfo->Texture);
    if (Existing >= 0)
    {
        Resource = &Context->DesktopGraphResources[Existing];
        if (Resource->Width != TextureInfo->Width ||
            Resource->Height != TextureInfo->Height ||
            Resource->Format != TextureInfo->Format)
        {
            return -1;
        }
        if (Resource->InitialData && !Context->TerrainGraphActive)
        {
            Resource->Data = TextureInfo->Data;
            Resource->Serial = TextureInfo->Serial;
            Resource->LevelCount = TextureInfo->LevelCount;
        }
        return Existing;
    }
    if (Context->DesktopGraphResourceCount >=
        RPI5VC4_V3D_GRAPH_MAX_RESOURCES)
    {
        return -1;
    }

    Resource = &Context->DesktopGraphResources[
                    Context->DesktopGraphResourceCount];
    Resource->Texture = TextureInfo->Texture;
    Resource->Data = TextureInfo->Data;
    Resource->Width = TextureInfo->Width;
    Resource->Height = TextureInfo->Height;
    Resource->Format = TextureInfo->Format;
    Resource->Serial = TextureInfo->Serial;
    Resource->LevelCount = TextureInfo->LevelCount;
    Resource->InitialData = TRUE;
    return (LONG)Context->DesktopGraphResourceCount++;
}

static LONG
Rpi5OglAddDesktopGraphTarget(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ const RPI5VC4_OGL_RENDER_TARGET *Target,
    _Out_ PBOOL WasProduced)
{
    LONG Existing;
    PRPI5VC4_OGL_GRAPH_RESOURCE Resource;

    *WasProduced = FALSE;
    if (Target->Texture == NULL || Target->TextureFormat != GL_RGBA)
        return -1;
    Existing = Rpi5OglFindDesktopGraphResource(Context, Target->Texture);
    if (Existing >= 0)
    {
        Resource = &Context->DesktopGraphResources[Existing];
        if (Resource->Width != Target->Width ||
            Resource->Height != Target->Height ||
            Resource->Format != Target->TextureFormat)
        {
            return -1;
        }
        *WasProduced = Resource->Produced;
        Resource->Produced = TRUE;
        return Existing;
    }
    if (Context->DesktopGraphResourceCount >=
        RPI5VC4_V3D_GRAPH_MAX_RESOURCES)
    {
        return -1;
    }

    Resource = &Context->DesktopGraphResources[
                    Context->DesktopGraphResourceCount];
    Resource->Texture = Target->Texture;
    Resource->Data = Target->TextureData;
    Resource->Width = Target->Width;
    Resource->Height = Target->Height;
    Resource->Format = Target->TextureFormat;
    Resource->LevelCount = 1;
    Resource->Produced = TRUE;
    return (LONG)Context->DesktopGraphResourceCount++;
}

static ULONG
Rpi5OglDesktopGraphSignature(
    _In_ PRPI5VC4_OGL_CONTEXT Context)
{
    ULONG Hash = 2166136261u;
    ULONG Index;

#define RPI5VC4_GRAPH_HASH(Value) \
    do { Hash = (Hash ^ (ULONG)(Value)) * 16777619u; } while (0)
    RPI5VC4_GRAPH_HASH(Context->DesktopGraphResourceCount);
    RPI5VC4_GRAPH_HASH(Context->DesktopGraphReadbackResource);
    for (Index = 0; Index < Context->DesktopGraphResourceCount; Index++)
    {
        const RPI5VC4_OGL_GRAPH_RESOURCE *Resource =
            &Context->DesktopGraphResources[Index];
        ULONG_PTR Texture = (ULONG_PTR)Resource->Texture;

        RPI5VC4_GRAPH_HASH(Texture);
#if defined(_WIN64)
        RPI5VC4_GRAPH_HASH(Texture >> 32);
#endif
        RPI5VC4_GRAPH_HASH(Resource->Width);
        RPI5VC4_GRAPH_HASH(Resource->Height);
        RPI5VC4_GRAPH_HASH(Resource->Format);
        RPI5VC4_GRAPH_HASH(Resource->LevelCount);
        RPI5VC4_GRAPH_HASH(Resource->InitialData);
        if (Resource->InitialData)
            RPI5VC4_GRAPH_HASH(Resource->Serial);
    }
#undef RPI5VC4_GRAPH_HASH
    return Hash != 0 ? Hash : 1;
}

static BOOL
Rpi5OglGraphResourcePixelBytes(
    _In_ const RPI5VC4_OGL_GRAPH_RESOURCE *Resource,
    _Out_ PULONG PixelBytes)
{
    ULONG Width = Resource->Width;
    ULONG Height = Resource->Height;
    ULONG Total = 0;
    ULONG Level;

    if (Resource->LevelCount == 0 ||
        Resource->LevelCount > RPI5VC4_V3D_TEXTURE_MAX_LEVELS)
    {
        return FALSE;
    }
    for (Level = 0; Level < Resource->LevelCount; Level++)
    {
        ULONG LevelBytes = Width * Height * 4u;

        if (Total > 0xFFFFFFFFu - LevelBytes)
            return FALSE;
        Total += LevelBytes;
        Width = Width > 1u ? Width >> 1 : 1u;
        Height = Height > 1u ? Height >> 1 : 1u;
    }
    *PixelBytes = Total;
    return TRUE;
}

static BOOL
Rpi5OglPackGraphResource(
    _In_ const RPI5VC4_OGL_GRAPH_RESOURCE *Resource,
    _Out_writes_bytes_(PixelBytes) GLubyte *Output,
    _In_ ULONG PixelBytes)
{
    ULONG Width = Resource->Width;
    ULONG Height = Resource->Height;
    ULONG OutputOffset = 0;
    ULONG Level;

    if (Resource->Texture == NULL)
        return FALSE;
    for (Level = 0; Level < Resource->LevelCount; Level++)
    {
        const struct gl_texture_image *Image =
            Resource->Texture->Image[Level];
        ULONG Components;
        ULONG PixelCount;
        ULONG Pixel;

        if (Image == NULL || Image->Data == NULL || Image->Border != 0 ||
            (ULONG)Image->Width != Width ||
            (ULONG)Image->Height != Height ||
            (Image->Format != GL_RGB && Image->Format != GL_RGBA))
        {
            return FALSE;
        }
        Components = Image->Format == GL_RGBA ? 4u : 3u;
        PixelCount = Width * Height;
        if (PixelCount > PixelBytes / 4u ||
            OutputOffset > PixelBytes - PixelCount * 4u)
            return FALSE;
        for (Pixel = 0; Pixel < PixelCount; Pixel++)
        {
            const GLubyte *Source =
                (const GLubyte *)Image->Data + Pixel * Components;
            GLubyte *Destination = Output + OutputOffset + Pixel * 4u;

            Destination[0] = Source[0];
            Destination[1] = Source[1];
            Destination[2] = Source[2];
            Destination[3] = Components == 4u ? Source[3] : 0xffu;
        }
        OutputOffset += PixelCount * 4u;
        Width = Width > 1u ? Width >> 1 : 1u;
        Height = Height > 1u ? Height >> 1 : 1u;
    }
    return OutputOffset == PixelBytes;
}

static BOOL
Rpi5OglSubmitDesktopGraph(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    ULONG HeaderSize =
        FIELD_OFFSET(RPI5VC4_V3D_RENDER_GRAPH_REQUEST, Pixels);
    ULONG Signature = Rpi5OglDesktopGraphSignature(Context);
    ULONG Attempt;
    ULONG ResourceIndex;
    ULONG PassCount = Context->DesktopGraphPassCount;
    ULONG ReadbackResource = Context->DesktopGraphReadbackResource;
    BOOL Success = FALSE;

    for (Attempt = 0; Attempt < 2 && !Success; Attempt++)
    {
        PRPI5VC4_V3D_RENDER_GRAPH_REQUEST Request;
        RPI5VC4_V3D_RENDER_GRAPH_RESULT Result;
        ULONG PixelBytes = 0;
        BOOL Initialize = Context->DesktopGraphCacheId == 0 ||
                          Context->DesktopGraphCacheSignature != Signature;
        ULONG TerrainBytes = Context->TerrainGraphActive && Initialize ?
            RPI5VC4_V3D_TERRAIN_VERTEX_COUNT *
                sizeof(RPI5VC4_V3D_TERRAIN_VERTEX) : 0;
        ULONG RequestSize;
        ULONG PixelOffset;
        INT Returned;

        if (Initialize)
        {
            for (ResourceIndex = 0;
                 ResourceIndex < Context->DesktopGraphResourceCount;
                 ResourceIndex++)
            {
                const RPI5VC4_OGL_GRAPH_RESOURCE *Resource =
                    &Context->DesktopGraphResources[ResourceIndex];

                if (Resource->InitialData)
                {
                    ULONG ResourceBytes;

                    if (!Rpi5OglGraphResourcePixelBytes(
                            Resource, &ResourceBytes) ||
                        PixelBytes > 0xFFFFFFFFu - ResourceBytes)
                    {
                        PixelBytes = 0xFFFFFFFFu;
                        break;
                    }
                    PixelBytes += ResourceBytes;
                }
            }
        }
        if ((TerrainBytes != 0 && Context->TerrainVertices == NULL) ||
            PixelBytes > 0xFFFFFFFFu - HeaderSize ||
            TerrainBytes > 0xFFFFFFFFu - HeaderSize - PixelBytes)
            break;
        RequestSize = HeaderSize + PixelBytes + TerrainBytes;
        Request = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequestSize);
        if (Request == NULL)
            break;

        Request->Size = RequestSize;
        Request->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
        Request->CacheId = Initialize ? 0 : Context->DesktopGraphCacheId;
        Request->Flags = Initialize ?
            RPI5VC4_V3D_GRAPH_REQUEST_INITIALIZE : 0;
        Request->ResourceCount = Context->DesktopGraphResourceCount;
        Request->PassCount = PassCount;
        Request->ReadbackResource = ReadbackResource;
        if (TerrainBytes != 0)
        {
            Request->TerrainVertexCount =
                RPI5VC4_V3D_TERRAIN_VERTEX_COUNT;
            Request->TerrainVertexBytes = TerrainBytes;
            Request->TerrainVertexOffset = HeaderSize + PixelBytes;
        }
        CopyMemory(Request->Passes,
                   Context->DesktopGraphPasses,
                   PassCount * sizeof(Request->Passes[0]));

        PixelOffset = HeaderSize;
        for (ResourceIndex = 0;
             ResourceIndex < Context->DesktopGraphResourceCount;
             ResourceIndex++)
        {
            const RPI5VC4_OGL_GRAPH_RESOURCE *Source =
                &Context->DesktopGraphResources[ResourceIndex];
            PRPI5VC4_V3D_GRAPH_RESOURCE Destination =
                &Request->Resources[ResourceIndex];
            ULONG ResourceBytes;

            Destination->Width = Source->Width;
            Destination->Height = Source->Height;
            Destination->Format = RPI5VC4_V3D_TEXTURE_FORMAT_RGBA8;
            Destination->LevelCount = Source->LevelCount;
            if (Source->InitialData)
            {
                Destination->Flags =
                    RPI5VC4_V3D_GRAPH_RESOURCE_INITIAL_DATA;
                Destination->RowStride = Source->Width * 4u;
                if (!Rpi5OglGraphResourcePixelBytes(
                        Source, &ResourceBytes))
                {
                    break;
                }
                Destination->PixelBytes = ResourceBytes;
                if (Initialize)
                {
                    GLubyte *Output = (GLubyte *)Request + PixelOffset;

                    Destination->PixelOffset = PixelOffset;
                    if (!Rpi5OglPackGraphResource(
                            Source, Output, ResourceBytes))
                    {
                        break;
                    }
                    PixelOffset += Destination->PixelBytes;
                }
            }
        }
        if (ResourceIndex != Context->DesktopGraphResourceCount)
        {
            HeapFree(GetProcessHeap(), 0, Request);
            break;
        }
        if (TerrainBytes != 0)
        {
            CopyMemory((PUCHAR)Request + Request->TerrainVertexOffset,
                       Context->TerrainVertices,
                       TerrainBytes);
        }

        ZeroMemory(&Result, sizeof(Result));
        Returned = ExtEscape(Context->Hdc,
                             RPI5VC4_ESCAPE_RENDER_GRAPH,
                             RequestSize,
                             (LPCSTR)Request,
                             sizeof(Result),
                             (LPSTR)&Result);
        HeapFree(GetProcessHeap(), 0, Request);
        if (Returned >= (INT)sizeof(Result) &&
            Result.Size == sizeof(Result) &&
            Result.AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
            Result.Status == RPI5VC4_V3D_SELFTEST_STATUS_TEXTURE_MISMATCH &&
            !Initialize)
        {
            Context->DesktopGraphCacheId = 0;
            continue;
        }
        Success = Returned >= (INT)sizeof(Result) &&
                  Result.Size == sizeof(Result) &&
                  Result.AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
                  Result.Status == RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS &&
                  Result.CacheId != 0 &&
                  Result.PassCount == PassCount &&
                  Result.FailedPass == 0xFFFFFFFFu &&
                  (Result.Flags &
                   RPI5VC4_V3D_GRAPH_RESULT_DIRECT_PRESENTED) != 0;
        Context->Stats.LastHardwareStatus = Returned > 0 ?
                                            Result.Status : 0xFFFFFFFFu;
        Context->Stats.LastTriangleStatus =
            Context->Stats.LastHardwareStatus;
        if (Success)
        {
            Context->DesktopGraphCacheId = Result.CacheId;
            Context->DesktopGraphCacheSignature = Signature;
        }
    }

    if (!Success)
    {
        Context->Stats.HardwareClearFailureCount++;
        Context->Stats.HardwareTriangleFailureCount++;
        Rpi5OglResetDesktopGraph(Context);
        return FALSE;
    }

    {
        ULONG HardwareTriangles = 0;
        ULONG PassIndex;

        for (PassIndex = 0; PassIndex < PassCount; PassIndex++)
        {
            HardwareTriangles +=
                Context->DesktopGraphPasses[PassIndex].ShaderMode ==
                    RPI5VC4_V3D_GRAPH_SHADER_TERRAIN ?
                    RPI5VC4_V3D_TERRAIN_INDEX_COUNT / 3u : 2u;
        }
        Context->Stats.HardwareTriangleCount += HardwareTriangles;
        Context->Stats.HardwareProgramTriangleCount += HardwareTriangles;
    }
    Context->BatchDirectPresented = TRUE;
    Context->Stats.HardwareClearCount++;
    if (Context->HardwareBatchActive &&
        Context->BatchVertexCount == 0)
    {
        /*
         * A full, directly presented graph supersedes an earlier canvas
         * clear.  Do not let the empty clear batch erase that frame during
         * DrvSwapBuffers or cancel the graph's direct-present indication.
        */
        Context->HardwareBatchActive = FALSE;
        Context->BatchRequest->DrawCount = 0;
    }
    for (ResourceIndex = 0;
         ResourceIndex < Context->DesktopGraphResourceCount;
         ResourceIndex++)
    {
        if (Context->DesktopGraphResources[ResourceIndex].Produced)
        {
            Rpi5OglDiscardDeferredTextureClear(
                Context,
                Context->DesktopGraphResources[ResourceIndex].Texture);
        }
    }
    Rpi5OglDiscardDeferredBackBufferClear(Context);
    Rpi5OglGl2InvalidateTextureUpload(Context->Gl2State);
    if (Context->TerrainGraphActive)
    {
        ZeroMemory(Context->DesktopGraphPasses,
                   sizeof(Context->DesktopGraphPasses));
        Context->DesktopGraphPassCount = 0;
        Context->TerrainGraphExpectedMode =
            RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NOISE;
        Context->DesktopGraphReadbackPending = FALSE;
        Context->DesktopGraphReadbackResource = 0xFFFFFFFFu;
        return TRUE;
    }
    Rpi5OglResetDesktopGraph(Context);
    Context->DesktopGraphReadbackPending = TRUE;
    Context->DesktopGraphReadbackResource = ReadbackResource;
    return TRUE;
}

static BOOL
Rpi5OglTerrainStateSupported(
    _In_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ const RPI5VC4_OGL_RENDER_TARGET *Target,
    _In_ ULONG ShaderMode)
{
    GLcontext *Mesa = Context->MesaContext;
    BOOLEAN Main = ShaderMode == RPI5VC4_V3D_GRAPH_SHADER_TERRAIN;
    BOOLEAN Overlay =
        ShaderMode == RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_OVERLAY;
    ULONG ExpectedRasterMask =
        (Mesa->Depth.Test ? DEPTH_BIT : 0) |
        (Mesa->Color.BlendEnabled ? BLEND_BIT : 0);

    /*
     * CanvasGeneric enables back-face culling.  Every triangle in the exact
     * recognized Terrain grid and fullscreen quads is CCW, so bit 2 culls no
     * submitted primitive and is equivalent to culling disabled here.
     */

    return Context->BatchRequest != NULL && Target->HasColor &&
           (!Context->HardwareBatchActive ||
            Context->BatchVertexCount == 0) &&
           Target->Width != 0 && Target->Height != 0 &&
           Target->Width <= RPI5VC4_V3D_CLEAR_MAX_WIDTH &&
           Target->Height <= RPI5VC4_V3D_CLEAR_MAX_HEIGHT &&
           Mesa->Visual != NULL && Mesa->Visual->RGBAflag &&
           Mesa->RenderMode == GL_RENDER && Mesa->Texture.Enabled == 0 &&
           Mesa->RasterMask == ExpectedRasterMask &&
           (Overlay ?
                (Mesa->Color.BlendEnabled &&
                 Mesa->Color.BlendSrc == GL_SRC_ALPHA &&
                 Mesa->Color.BlendDst == GL_ONE) :
                !Mesa->Color.BlendEnabled) &&
           (!Main ||
                (Mesa->Depth.Test &&
                 (Mesa->Depth.Func == GL_LESS ||
                  Mesa->Depth.Func == GL_LEQUAL) &&
                 Mesa->Depth.Mask && Mesa->Depth.Clear == 1.0f)) &&
           (!Mesa->Depth.Test ||
                ((Mesa->Depth.Func == GL_LESS ||
                  Mesa->Depth.Func == GL_LEQUAL) &&
                 Mesa->Depth.Clear == 1.0f)) &&
           !Mesa->Polygon.Unfilled && !Mesa->Polygon.OffsetAny &&
           !Mesa->Polygon.SmoothFlag && !Mesa->Polygon.StippleFlag &&
           (Mesa->Polygon.CullBits == 0 ||
            (Mesa->Polygon.CullBits == 2 &&
             Mesa->Polygon.FrontFace == GL_CCW)) &&
           Mesa->Viewport.X == 0 && Mesa->Viewport.Y == 0 &&
           Mesa->Viewport.Width == (GLsizei)Target->Width &&
           Mesa->Viewport.Height == (GLsizei)Target->Height &&
           Mesa->Viewport.Near == 0.0f && Mesa->Viewport.Far == 1.0f &&
           Context->BufferMode != GL_NONE;
}

static ULONG
Rpi5OglTerrainNextMode(
    _In_ ULONG ShaderMode)
{
    switch (ShaderMode)
    {
        case RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_LUMINANCE:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NOISE;
        case RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NOISE:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NORMAL;
        case RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NORMAL:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN;
        case RPI5VC4_V3D_GRAPH_SHADER_TERRAIN:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_BLOOM_HORIZONTAL;
        case RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_BLOOM_HORIZONTAL:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_BLOOM_VERTICAL;
        case RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_BLOOM_VERTICAL:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_OVERLAY;
        case RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_OVERLAY:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_TILT_HORIZONTAL;
        case RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_TILT_HORIZONTAL:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_TILT_VERTICAL;
        case RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_TILT_VERTICAL:
            return RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NOISE;
        default:
            return 0;
    }
}

static ULONG
Rpi5OglTerrainSourceFlags(
    _In_ const RPI5VC4_OGL_GL2_TEXTURE_INFO *TextureInfo,
    _In_ const RPI5VC4_OGL_GRAPH_RESOURCE *Resource)
{
    ULONG Flags = TextureInfo->LinearFilter ?
        RPI5VC4_V3D_GRAPH_SOURCE_LINEAR : 0;

    if (TextureInfo->MipmapFilter && Resource->LevelCount > 1u)
    {
        Flags |= RPI5VC4_V3D_GRAPH_SOURCE_MIPMAP;
    }
    if (TextureInfo->WrapS == GL_REPEAT)
        Flags |= RPI5VC4_V3D_GRAPH_SOURCE_REPEAT_S;
    if (TextureInfo->WrapT == GL_REPEAT)
        Flags |= RPI5VC4_V3D_GRAPH_SOURCE_REPEAT_T;
    return Flags;
}

static RPI5VC4_OGL_GRAPH_DRAW_RESULT
Rpi5OglRecordTerrainGraphDraw(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count)
{
    RPI5VC4_OGL_RENDER_TARGET Target;
    RPI5VC4_OGL_GL2_TEXTURE_INFO
        Textures[RPI5VC4_V3D_GRAPH_MAX_SOURCES];
    RPI5VC4_V3D_VERTEX Vertices[RPI5VC4_V3D_GRAPH_MAX_VERTICES];
    PRPI5VC4_V3D_GRAPH_PASS Pass;
    ULONG ShaderMode = Rpi5OglGl2TerrainMode(Context->Gl2State);
    ULONG ExpectedSources =
        ShaderMode == RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NOISE ? 0u :
        ShaderMode == RPI5VC4_V3D_GRAPH_SHADER_TERRAIN ? 6u : 1u;
    ULONG VertexCount = 0;
    ULONG TriangleCount = 0;
    ULONG SourceIndex;
    LONG SourceResources[RPI5VC4_V3D_GRAPH_MAX_SOURCES] = {0};
    LONG TargetResource = -1;
    BOOL WasProduced = FALSE;
    BOOL Main = ShaderMode == RPI5VC4_V3D_GRAPH_SHADER_TERRAIN;
    BOOL Final = ShaderMode ==
        RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_TILT_VERTICAL;

    ZeroMemory(&Target, sizeof(Target));
    ZeroMemory(Textures, sizeof(Textures));
    ZeroMemory(Vertices, sizeof(Vertices));
    if (!Context->TerrainGraphActive)
    {
        if (ShaderMode !=
            RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_LUMINANCE)
        {
            return Rpi5OglGraphDrawFailed;
        }
        Rpi5OglResetDesktopGraph(Context);
        Context->DesktopGraphActive = TRUE;
        Context->TerrainGraphActive = TRUE;
        Context->TerrainGraphExpectedMode = ShaderMode;
    }
    if (ShaderMode == 0 ||
        ShaderMode != Context->TerrainGraphExpectedMode ||
        Context->DesktopGraphPassCount >= RPI5VC4_V3D_GRAPH_MAX_PASSES ||
        !Rpi5OglResolveRenderTarget(Context, &Target) ||
        !Rpi5OglTerrainStateSupported(Context, &Target, ShaderMode))
    {
        Rpi5OglResetDesktopGraph(Context);
        return Rpi5OglGraphDrawFailed;
    }

    if (Main)
    {
        if (Mode != GL_TRIANGLES || First != 0 ||
            Count != RPI5VC4_V3D_TERRAIN_INDEX_COUNT)
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
        if (Context->TerrainVertices == NULL)
        {
            Context->TerrainVertices = HeapAlloc(
                GetProcessHeap(),
                0,
                RPI5VC4_V3D_TERRAIN_VERTEX_COUNT *
                    sizeof(RPI5VC4_V3D_TERRAIN_VERTEX));
            if (Context->TerrainVertices == NULL)
            {
                Rpi5OglResetDesktopGraph(Context);
                return Rpi5OglGraphDrawFailed;
            }
            if (!Rpi5OglGl2BuildTerrainGrid(
                    Context->Gl2State,
                    First,
                    Count,
                    Context->TerrainVertices,
                    RPI5VC4_V3D_TERRAIN_VERTEX_COUNT))
            {
                HeapFree(GetProcessHeap(), 0, Context->TerrainVertices);
                Context->TerrainVertices = NULL;
                Rpi5OglResetDesktopGraph(Context);
                return Rpi5OglGraphDrawFailed;
            }
        }
        VertexCount = RPI5VC4_V3D_TERRAIN_VERTEX_COUNT;
        TriangleCount = RPI5VC4_V3D_TERRAIN_INDEX_COUNT / 3u;
    }
    else if (!Rpi5OglGl2BuildBatch(
                 Context->Gl2State,
                 Mode,
                 First,
                 Count,
                 Context->MesaContext->Polygon.FrontFace,
                 Context->MesaContext->Polygon.CullBits,
                 Vertices,
                 NULL,
                 RTL_NUMBER_OF(Vertices),
                 &VertexCount,
                 &TriangleCount) ||
             VertexCount != RPI5VC4_V3D_GRAPH_MAX_VERTICES ||
             TriangleCount != 2u)
    {
        Rpi5OglResetDesktopGraph(Context);
        return Rpi5OglGraphDrawFailed;
    }

    for (SourceIndex = 0; SourceIndex < ExpectedSources; SourceIndex++)
    {
        LONG Existing;

        if (!Rpi5OglGl2GetTerrainTexture(
                Context->Gl2State, SourceIndex, &Textures[SourceIndex]))
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
        Existing = Rpi5OglFindDesktopGraphResource(
            Context, Textures[SourceIndex].Texture);
        if (Existing < 0 ||
            !Context->DesktopGraphResources[Existing].Produced)
        {
            Rpi5OglMaterializeTextureClear(
                Textures[SourceIndex].Texture);
        }
        SourceResources[SourceIndex] =
            Rpi5OglAddDesktopGraphSource(
                Context, &Textures[SourceIndex]);
        if (SourceResources[SourceIndex] < 0)
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
    }

    if (!Final)
    {
        if (Target.Framebuffer == 0 || Target.Texture == NULL)
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
        TargetResource = Rpi5OglAddDesktopGraphTarget(
            Context, &Target, &WasProduced);
        if (TargetResource < 0)
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
    }
    else if (Target.Framebuffer != 0 || ExpectedSources != 1u)
    {
        Rpi5OglResetDesktopGraph(Context);
        return Rpi5OglGraphDrawFailed;
    }

    Pass = &Context->DesktopGraphPasses[Context->DesktopGraphPassCount];
    ZeroMemory(Pass, sizeof(*Pass));
    Pass->ShaderMode = ShaderMode;
    Pass->SourceCount = ExpectedSources;
    Pass->VertexCount = VertexCount;
    Pass->ClearColor = Context->ClearColor;
    Pass->MinimumX = 0;
    Pass->MinimumY = 0;
    Pass->MaximumX = Target.Width;
    Pass->MaximumY = Target.Height;
    for (SourceIndex = 0; SourceIndex < ExpectedSources; SourceIndex++)
    {
        Pass->SourceResources[SourceIndex] =
            (ULONG)SourceResources[SourceIndex];
        Pass->SourceFlags[SourceIndex] = Rpi5OglTerrainSourceFlags(
            &Textures[SourceIndex],
            &Context->DesktopGraphResources[
                SourceResources[SourceIndex]]);
    }
    if (ExpectedSources != 0)
        Pass->SourceResource = Pass->SourceResources[0];
    if (Main)
    {
        if (!Rpi5OglGl2GetTerrainUniforms(
                Context->Gl2State, Pass->ShaderUniforms))
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
        if (Context->MesaContext->Depth.Func == GL_LEQUAL)
            Pass->Flags |= RPI5VC4_V3D_BATCH_FLAG_DEPTH_LEQUAL;
    }
    else
    {
        CopyMemory(Pass->Vertices,
                   Vertices,
                   sizeof(Pass->Vertices));
    }
    if (ShaderMode == RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_OVERLAY)
    {
        Pass->Flags = RPI5VC4_V3D_GRAPH_PASS_LOAD_TARGET |
                      RPI5VC4_V3D_GRAPH_PASS_BLEND_ADDITIVE;
    }

    if (Final)
    {
        ULONG DestinationX;
        ULONG DestinationY;
        ULONG VisibleWidth;
        ULONG VisibleHeight;

        if (!Rpi5OglGetDirectPresentOrigin(Context,
                                           &DestinationX,
                                           &DestinationY,
                                           &VisibleWidth,
                                           &VisibleHeight) ||
            !Rpi5OglClipDirectPresentVertices(Pass->Vertices,
                                               Pass->VertexCount,
                                               Context->Width,
                                               Context->Height,
                                               VisibleWidth,
                                               VisibleHeight))
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
        for (SourceIndex = 0;
             SourceIndex < Pass->VertexCount;
             SourceIndex++)
        {
            Pass->Vertices[SourceIndex].Color[0] =
                Pass->Vertices[SourceIndex].TexCoord[0];
            Pass->Vertices[SourceIndex].Color[1] =
                Pass->Vertices[SourceIndex].TexCoord[1];
        }
        Pass->TargetResource = RPI5VC4_V3D_GRAPH_DEFAULT_TARGET;
        Pass->DestinationX = DestinationX;
        Pass->DestinationY = DestinationY;
        Context->DesktopGraphReadbackResource = Pass->SourceResource;
    }
    else
    {
        Pass->TargetResource = (ULONG)TargetResource;
        if (ExpectedSources == 0)
            Pass->SourceResource = (ULONG)TargetResource;
    }

    Context->DesktopGraphPassCount++;
    Context->TerrainGraphExpectedMode = Rpi5OglTerrainNextMode(ShaderMode);
    if (ShaderMode == RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_LUMINANCE)
        Context->TerrainGraphSetupPassCount =
            Context->DesktopGraphPassCount;
    if (Final)
    {
        return Rpi5OglSubmitDesktopGraph(Context) ?
            Rpi5OglGraphDrawHandled : Rpi5OglGraphDrawFailed;
    }
    UNREFERENCED_PARAMETER(WasProduced);
    return Rpi5OglGraphDrawHandled;
}

static RPI5VC4_OGL_GRAPH_DRAW_RESULT
Rpi5OglRecordDesktopGraphDraw(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLenum Mode,
    _In_ GLint First,
    _In_ GLsizei Count)
{
    RPI5VC4_OGL_RENDER_TARGET Target;
    RPI5VC4_OGL_GL2_TEXTURE_INFO TextureInfo;
    RPI5VC4_V3D_VERTEX Vertices[RPI5VC4_V3D_GRAPH_MAX_VERTICES];
    ULONG VertexCount;
    ULONG TriangleCount;
    ULONG MinimumX;
    ULONG MinimumY;
    ULONG MaximumX;
    ULONG MaximumY;
    ULONG ProgramFlags;
    BOOL FullTarget;
    BOOL BackgroundStart;
    BOOL StartGraph;
    BOOL WasProduced;
    LONG ExistingSourceResource;
    LONG SourceResource;
    LONG TargetResource;
    PRPI5VC4_V3D_GRAPH_PASS Pass;

    ZeroMemory(&Target, sizeof(Target));
    ZeroMemory(&TextureInfo, sizeof(TextureInfo));
    VertexCount = 0;
    TriangleCount = 0;
    MinimumX = MinimumY = MaximumX = MaximumY = 0;
    ProgramFlags = 0;
#define RPI5VC4_REJECT_GRAPH() \
    do { \
        return Context->DesktopGraphActive ? \
            Rpi5OglGraphDrawFailed : Rpi5OglGraphDrawNotHandled; \
    } while (0)

    if (!Rpi5OglDesktopBatchStateSupported(Context->MesaContext))
        RPI5VC4_REJECT_GRAPH();
    if (!Rpi5OglResolveRenderTarget(Context, &Target))
        RPI5VC4_REJECT_GRAPH();
    if (!Rpi5OglGl2GetBoundDesktopTexture(Context->Gl2State,
                                          &TextureInfo))
    {
        RPI5VC4_REJECT_GRAPH();
    }
    ExistingSourceResource = Rpi5OglFindDesktopGraphResource(
                                 Context, TextureInfo.Texture);
    if (ExistingSourceResource < 0 ||
        !Context->DesktopGraphResources[ExistingSourceResource].Produced)
    {
        Rpi5OglMaterializeTextureClear(TextureInfo.Texture);
    }
    if (!TextureInfo.LinearFilter)
        RPI5VC4_REJECT_GRAPH();
    if (!Rpi5OglGl2BuildBatch(Context->Gl2State,
                              Mode,
                              First,
                              Count,
                              Context->MesaContext->Polygon.FrontFace,
                              Context->MesaContext->Polygon.CullBits,
                              Vertices,
                              NULL,
                              RTL_NUMBER_OF(Vertices),
                              &VertexCount,
                              &TriangleCount))
    {
        RPI5VC4_REJECT_GRAPH();
    }
    if (VertexCount != RPI5VC4_V3D_GRAPH_MAX_VERTICES ||
        TriangleCount != 2)
    {
        RPI5VC4_REJECT_GRAPH();
    }
    if (!Rpi5OglBatchPixelBounds(Vertices,
                                 VertexCount,
                                 Target.Width,
                                 Target.Height,
                                 &MinimumX,
                                 &MinimumY,
                                 &MaximumX,
                                 &MaximumY))
    {
        RPI5VC4_REJECT_GRAPH();
    }

    FullTarget = MinimumX == 0 && MinimumY == 0 &&
                 MaximumX == Target.Width && MaximumY == Target.Height;
    ProgramFlags = Rpi5OglGl2BatchFlags(Context->Gl2State);
    BackgroundStart =
        !Context->DesktopGraphActive &&
        Target.Framebuffer != 0 && Target.Texture != NULL &&
        TextureInfo.Width == Context->Width &&
        TextureInfo.Height == Context->Height &&
        ProgramFlags == 0 &&
        Context->MesaContext->Color.BlendEnabled &&
        !Rpi5OglPreserveDestinationAlphaBlendEnabled(Context->MesaContext) &&
        Context->HardwareClearFresh &&
        Context->HardwareClearFramebuffer == Target.Framebuffer &&
        Context->HardwareClearGeneration == Target.Generation &&
        FullTarget;
    StartGraph = !Context->DesktopGraphActive &&
                 Target.Framebuffer != 0 && Target.Texture != NULL &&
                 TextureInfo.Width == Context->Width &&
                 TextureInfo.Height == Context->Height &&
                 FullTarget &&
                 (BackgroundStart ||
                  ProgramFlags ==
                      RPI5VC4_V3D_BATCH_FLAG_DESKTOP_BLUR_HORIZONTAL);
    if (StartGraph)
    {
        Rpi5OglResetDesktopGraph(Context);
        Context->DesktopGraphActive = TRUE;
        TargetResource = Rpi5OglAddDesktopGraphTarget(Context,
                                                       &Target,
                                                       &WasProduced);
        if (TargetResource < 0 || WasProduced)
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
        if (BackgroundStart)
            Context->HardwareClearFresh = FALSE;
    }
    else if (!Context->DesktopGraphActive)
    {
        RPI5VC4_REJECT_GRAPH();
    }

    if (Context->DesktopGraphPassCount >= RPI5VC4_V3D_GRAPH_MAX_PASSES)
    {
        Rpi5OglResetDesktopGraph(Context);
        return Rpi5OglGraphDrawFailed;
    }
    SourceResource = Rpi5OglAddDesktopGraphSource(Context, &TextureInfo);
    if (SourceResource < 0)
    {
        Rpi5OglResetDesktopGraph(Context);
        return Rpi5OglGraphDrawFailed;
    }

    Pass = &Context->DesktopGraphPasses[Context->DesktopGraphPassCount];
    Pass->SourceResource = SourceResource;
    Pass->Flags = ProgramFlags |
                  RPI5VC4_V3D_BATCH_FLAG_TEXTURED |
                  RPI5VC4_V3D_BATCH_FLAG_TEXTURE_LINEAR;
    Pass->VertexCount = VertexCount;
    Pass->ClearColor = Context->ClearColor;
    Pass->MinimumX = MinimumX;
    Pass->MinimumY = MinimumY;
    Pass->MaximumX = MaximumX;
    Pass->MaximumY = MaximumY;
    CopyMemory(Pass->Vertices,
               Vertices,
               VertexCount * sizeof(Vertices[0]));
    if (Context->MesaContext->Color.BlendEnabled)
    {
        Pass->Flags |= Rpi5OglPreserveDestinationAlphaBlendEnabled(
                           Context->MesaContext) ?
            RPI5VC4_V3D_GRAPH_PASS_BLEND_PRESERVE_ALPHA :
            RPI5VC4_V3D_GRAPH_PASS_BLEND_STANDARD;
    }

    if (Target.Framebuffer == 0)
    {
        ULONG DestinationX;
        ULONG DestinationY;
        ULONG VisibleWidth;
        ULONG VisibleHeight;
        BOOL PresentOriginValid;
        BOOL PresentVerticesValid = FALSE;
        const RPI5VC4_OGL_GRAPH_RESOURCE *Source;

        Source = SourceResource >= 0 ?
            &Context->DesktopGraphResources[SourceResource] : NULL;
        PresentOriginValid = Rpi5OglGetDirectPresentOrigin(Context,
                                                           &DestinationX,
                                                           &DestinationY,
                                                           &VisibleWidth,
                                                           &VisibleHeight);
        if (PresentOriginValid)
        {
            PresentVerticesValid = Rpi5OglClipDirectPresentVertices(
                Pass->Vertices,
                Pass->VertexCount,
                Context->Width,
                Context->Height,
                VisibleWidth,
                VisibleHeight);
        }
        if (!FullTarget || ProgramFlags != 0 ||
            Context->MesaContext->Color.BlendEnabled ||
            Source == NULL || !Source->Produced ||
            Source->Width != Context->Width ||
            Source->Height != Context->Height ||
            !PresentOriginValid || !PresentVerticesValid)
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
        Context->DesktopGraphReadbackResource = SourceResource;
        Pass->TargetResource = RPI5VC4_V3D_GRAPH_DEFAULT_TARGET;
        Pass->DestinationX = DestinationX;
        Pass->DestinationY = DestinationY;
        Context->DesktopGraphPassCount++;
        return Rpi5OglSubmitDesktopGraph(Context) ?
            Rpi5OglGraphDrawHandled : Rpi5OglGraphDrawFailed;
    }

    if (!StartGraph)
    {
        TargetResource = Rpi5OglAddDesktopGraphTarget(Context,
                                                       &Target,
                                                       &WasProduced);
        if (TargetResource < 0)
        {
            Rpi5OglResetDesktopGraph(Context);
            return Rpi5OglGraphDrawFailed;
        }
    }
    Pass->TargetResource = TargetResource;
    if (!StartGraph && (WasProduced || !FullTarget))
        Pass->Flags |= RPI5VC4_V3D_GRAPH_PASS_LOAD_TARGET;
    Context->DesktopGraphPassCount++;
#undef RPI5VC4_REJECT_GRAPH
    return Rpi5OglGraphDrawHandled;
}

static BOOL
Rpi5OglEnsureDesktopGraphReadback(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    RPI5VC4_V3D_READ_GRAPH_REQUEST Request;
    PRPI5VC4_V3D_READ_GRAPH_RESULT Result;
    ULONG HeaderSize = FIELD_OFFSET(RPI5VC4_V3D_READ_GRAPH_RESULT,
                                    Pixels);
    ULONG PixelBytes;
    ULONG ResultSize;
    INT Returned;
    BOOL Success;

    if (!Context->DesktopGraphReadbackPending)
        return TRUE;
    PixelBytes = Context->Width * Context->Height * sizeof(ULONG);
    ResultSize = HeaderSize + PixelBytes;
    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ResultSize);
    if (Result == NULL)
        return FALSE;

    ZeroMemory(&Request, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Request.CacheId = Context->DesktopGraphCacheId;
    Request.Resource = Context->DesktopGraphReadbackResource;
    Returned = ExtEscape(Context->Hdc,
                         RPI5VC4_ESCAPE_READ_GRAPH,
                         sizeof(Request),
                         (LPCSTR)&Request,
                         ResultSize,
                         (LPSTR)Result);
    Success = Returned >= (INT)ResultSize &&
              Result->Size == ResultSize &&
              Result->AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
              Result->Status == RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS &&
              Result->CacheId == Context->DesktopGraphCacheId &&
              Result->Resource == Context->DesktopGraphReadbackResource &&
              Result->Width == Context->Width &&
              Result->Height == Context->Height &&
              Result->Stride == Context->Stride &&
              Result->PixelBytes == PixelBytes;
    if (Success)
    {
        Rpi5OglDiscardDeferredBackBufferClear(Context);
        CopyMemory(Context->BackBuffer, Result->Pixels, PixelBytes);
        Context->DesktopGraphReadbackPending = FALSE;
    }
    HeapFree(GetProcessHeap(), 0, Result);
    return Success;
}

static UCHAR
Rpi5OglBlendComponent(
    _In_ UCHAR Source,
    _In_ UCHAR Destination,
    _In_ UCHAR SourceAlpha)
{
    return (UCHAR)(((ULONG)Source * SourceAlpha +
                    (ULONG)Destination * (255u - SourceAlpha) + 127u) /
                   255u);
}

static VOID
Rpi5OglCopyDesktopBatchPixels(
    _Inout_ PRPI5VC4_OGL_RENDER_TARGET Target,
    _In_reads_(Target->Width * Target->Height) const ULONG *Pixels,
    _In_ ULONG MinimumX,
    _In_ ULONG MinimumY,
    _In_ ULONG MaximumX,
    _In_ ULONG MaximumY,
    _In_ BOOL Blend,
    _In_ BOOL PreserveDestinationAlpha)
{
    ULONG X;
    ULONG Y;

    for (Y = MinimumY; Y < MaximumY; Y++)
    {
        for (X = MinimumX; X < MaximumX; X++)
        {
            ULONG Source = Pixels[Y * Target->Width + X];

            if (Blend)
            {
                ULONG Destination = Rpi5OglReadTargetPixel(Target, X, Y);
                UCHAR SourceRed;
                UCHAR SourceGreen;
                UCHAR SourceBlue;
                UCHAR SourceAlpha;
                UCHAR DestinationRed;
                UCHAR DestinationGreen;
                UCHAR DestinationBlue;
                UCHAR DestinationAlpha;
                UCHAR OutputAlpha;

                Rpi5OglUnpackColor(Source,
                                   &SourceRed,
                                   &SourceGreen,
                                   &SourceBlue,
                                   &SourceAlpha);
                Rpi5OglUnpackColor(Destination,
                                   &DestinationRed,
                                   &DestinationGreen,
                                   &DestinationBlue,
                                   &DestinationAlpha);
                OutputAlpha = PreserveDestinationAlpha ? DestinationAlpha :
                    Rpi5OglBlendComponent(SourceAlpha,
                                          DestinationAlpha,
                                          SourceAlpha);
                Source = Rpi5OglPackColor(
                    Rpi5OglBlendComponent(SourceRed,
                                          DestinationRed,
                                          SourceAlpha),
                    Rpi5OglBlendComponent(SourceGreen,
                                          DestinationGreen,
                                          SourceAlpha),
                    Rpi5OglBlendComponent(SourceBlue,
                                          DestinationBlue,
                                          SourceAlpha),
                    OutputAlpha);
            }
            Rpi5OglWriteTargetPixel(Target, X, Y, Source);
        }
    }
}

static BOOL
Rpi5OglSubmitBatch(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ BOOL DirectPresentAllowed)
{
    RPI5VC4_OGL_RENDER_TARGET Target;
    PRPI5VC4_V3D_BATCH_REQUEST Request = Context->BatchRequest;
    PRPI5VC4_V3D_BATCH_RESULT Result;
    ULONG RequestSize;
    ULONG ResultHeaderSize =
        FIELD_OFFSET(RPI5VC4_V3D_BATCH_RESULT, Pixels);
    ULONG PixelBytes;
    ULONG ResultSize;
    ULONG DestinationX = 0;
    ULONG DestinationY = 0;
    PVOID NewResult;
    ULONG VertexCount = Context->BatchVertexCount;
    ULONG ProgramTriangleCount = Context->BatchProgramTriangleCount;
    GLuint ProgramName = Context->BatchProgramName;
    ULONG ProgramFlags = Context->BatchProgramFlags;
    BOOL Textured = Context->BatchTextured;
    BOOL LinearTextureFilter = Context->BatchTextureLinear;
    BOOL MipmapTextureFilter = Context->BatchTextureMipmap;
    ULONG TextureGeneration = Context->BatchTextureGeneration;
    ULONG TextureGeneration1 = Context->BatchTextureGeneration1;
    GLenum DepthFunc = Context->BatchDepthFunc;
    BOOL BlendEnabled = Context->BatchBlendEnabled;
    BOOL PreserveDestinationAlphaBlend =
        Context->BatchPreserveDestinationAlphaBlend;
    BOOL Desktop2D = Context->BatchDesktop2D;
    BOOL DirectPresent;
    BOOL HeightMapped;
    BOOL Ideas;
    BOOL Jellyfish;
    BOOL Shadow;
    BOOL OutputDepth;
    ULONG MinimumX = 0;
    ULONG MinimumY = 0;
    ULONG MaximumX = 0;
    ULONG MaximumY = 0;
    INT Returned;
    BOOL Success;
    ULONG Pixel;

    if (!Context->HardwareBatchActive)
        return TRUE;

    Context->HardwareBatchActive = FALSE;
    Context->BatchDirectPresented = FALSE;
    Context->BatchVertexCount = 0;
    Context->BatchProgramTriangleCount = 0;
    Context->BatchProgramName = 0;
    Context->BatchProgramFlags = 0;
    Context->BatchTextured = FALSE;
    Context->BatchTextureLinear = FALSE;
    Context->BatchTextureMipmap = FALSE;
    Context->BatchTextureGeneration = 0;
    Context->BatchTextureGeneration1 = 0;
    Context->BatchBlendEnabled = FALSE;
    Context->BatchPreserveDestinationAlphaBlend = FALSE;
    Context->BatchDesktop2D = FALSE;
    OutputDepth =
        (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_OUTPUT_DEPTH) != 0;
    if (VertexCount == 0)
        return Rpi5OglHardwareClear(Context);
    if (!Rpi5OglResolveRenderTarget(Context, &Target) ||
        (OutputDepth ?
             (!Target.HasDepth ||
              Target.DepthTexture == NULL ||
              Target.DepthData == NULL ||
              Target.DepthFormat != GL_DEPTH_COMPONENT ||
              Target.Framebuffer == 0) :
             !Target.HasColor) ||
        (!Desktop2D && !OutputDepth &&
         (Target.Pixels != Context->BackBuffer ||
          Target.Width != Context->Width ||
          Target.Height != Context->Height)) ||
        (Desktop2D &&
         !Rpi5OglBatchPixelBounds(Request->Vertices,
                                  VertexCount,
                                  Target.Width,
                                  Target.Height,
                                  &MinimumX,
                                  &MinimumY,
                                  &MaximumX,
                                  &MaximumY)))
    {
        return FALSE;
    }

    DirectPresent = !OutputDepth && DirectPresentAllowed &&
                    Target.Framebuffer == 0 &&
                    (!Desktop2D ||
                     (MinimumX == 0 && MinimumY == 0 &&
                      MaximumX == Target.Width &&
                      MaximumY == Target.Height && !BlendEnabled)) &&
                    Rpi5OglGetDirectPresentOrigin(Context,
                                                  &DestinationX,
                                                  &DestinationY,
                                                  NULL,
                                                  NULL);
    PixelBytes = DirectPresent ? 0 :
                 Target.Width * Target.Height * sizeof(ULONG);
    ResultSize = ResultHeaderSize + PixelBytes;
    if (Context->BatchResult == NULL)
    {
        NewResult = HeapAlloc(GetProcessHeap(),
                              HEAP_ZERO_MEMORY,
                              ResultSize);
    }
    else if (Context->BatchResultSize < ResultSize)
    {
        NewResult = HeapReAlloc(GetProcessHeap(),
                                HEAP_ZERO_MEMORY,
                                Context->BatchResult,
                                ResultSize);
    }
    else
    {
        NewResult = Context->BatchResult;
    }
    if (NewResult == NULL)
        return FALSE;

    Context->BatchResult = NewResult;
    Context->BatchResultSize = ResultSize;
    Result = Context->BatchResult;
    RequestSize = FIELD_OFFSET(RPI5VC4_V3D_BATCH_REQUEST, Vertices) +
                  VertexCount * sizeof(RPI5VC4_V3D_VERTEX);
    HeightMapped =
        (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_HEIGHT_MAP) != 0;
    Jellyfish =
        (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_JELLYFISH) != 0;
    Shadow =
        (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_SHADOW) != 0;
    if (HeightMapped || Jellyfish)
    {
        PRPI5VC4_V3D_TEXCOORD Source =
            (PRPI5VC4_V3D_TEXCOORD)((PUCHAR)Request->Vertices +
                RPI5VC4_V3D_BATCH_MAX_VERTICES *
                sizeof(RPI5VC4_V3D_VERTEX));
        PRPI5VC4_V3D_TEXCOORD Destination =
            (PRPI5VC4_V3D_TEXCOORD)&Request->Vertices[VertexCount];

        if ((!Jellyfish &&
             VertexCount > RPI5VC4_V3D_HEIGHT_MAX_VERTICES) ||
            (Jellyfish &&
             VertexCount > RPI5VC4_V3D_JELLYFISH_MAX_VERTICES))
            return FALSE;
        MoveMemory(Destination,
                   Source,
                   VertexCount * sizeof(*Source));
        RequestSize += VertexCount * sizeof(*Source);
    }
    Request->Size = RequestSize;
    Request->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Request->Width = Target.Width;
    Request->Height = Target.Height;
    Request->ClearColor = Context->ClearColor;
    Request->Flags = ProgramFlags;
    Ideas = (ProgramFlags & RPI5VC4_V3D_BATCH_FLAG_IDEAS) != 0;
    if (Shadow && Request->DrawCount == 0)
        return FALSE;
    if (!Desktop2D && !Ideas && !Jellyfish)
    {
        Request->Flags |= RPI5VC4_V3D_BATCH_FLAG_DEPTH_TEST |
                          RPI5VC4_V3D_BATCH_FLAG_DEPTH_WRITE;
        if (DepthFunc == GL_LEQUAL)
            Request->Flags |= RPI5VC4_V3D_BATCH_FLAG_DEPTH_LEQUAL;
    }
    if (PreserveDestinationAlphaBlend && !Desktop2D)
        Request->Flags |=
            RPI5VC4_V3D_BATCH_FLAG_BLEND_PRESERVE_ALPHA;
    if (Jellyfish && BlendEnabled)
        Request->Flags |= RPI5VC4_V3D_BATCH_FLAG_BLEND_STANDARD;
    if (DirectPresent)
        Request->Flags |= RPI5VC4_V3D_BATCH_FLAG_DIRECT_PRESENT;
    if (Textured)
    {
        Request->Flags |= RPI5VC4_V3D_BATCH_FLAG_TEXTURED;
        if (LinearTextureFilter)
            Request->Flags |= RPI5VC4_V3D_BATCH_FLAG_TEXTURE_LINEAR;
        if (MipmapTextureFilter)
            Request->Flags |= RPI5VC4_V3D_BATCH_FLAG_TEXTURE_MIPMAP;
    }
    Request->VertexCount = VertexCount;
    Request->DestinationX = DestinationX;
    Request->DestinationY = DestinationY;
    Request->TextureGeneration = Textured ? TextureGeneration : 0;
    Request->TextureGeneration1 =
        Textured && Jellyfish ? TextureGeneration1 : 0;
    if (Ideas)
    {
        if (Request->DrawCount == 0 ||
            !Rpi5OglGl2GetIdeasUniforms(Context->Gl2State,
                                        Request->ShaderUniforms))
        {
            return FALSE;
        }
    }
    if (Jellyfish)
    {
        if (Request->DrawCount == 0 ||
            !Rpi5OglGl2GetJellyfishUniforms(Context->Gl2State,
                                            Request->ShaderUniforms))
        {
            return FALSE;
        }
    }
    if ((ProgramFlags &
         (RPI5VC4_V3D_BATCH_FLAG_NORMAL_MAP |
          RPI5VC4_V3D_BATCH_FLAG_HEIGHT_MAP)) == 0)
    {
        ZeroMemory(Request->NormalMatrix, sizeof(Request->NormalMatrix));
    }

    Returned = ExtEscape(Context->Hdc,
                         RPI5VC4_ESCAPE_RENDER_BATCH,
                         RequestSize,
                         (LPCSTR)Request,
                         ResultSize,
                         (LPSTR)Result);
    Context->Stats.LastHardwareStatus = Returned >= (INT)ResultHeaderSize ?
                                        Result->Status : 0xFFFFFFFFu;
    Context->Stats.LastTriangleStatus =
        Context->Stats.LastHardwareStatus;
    Success = Returned >= (INT)ResultSize &&
              Result->Size == ResultSize &&
              Result->AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
              Result->Status == RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS &&
              (Result->Flags & RPI5VC4_V3D_SELFTEST_FLAG_PASSED) != 0 &&
              Result->Width == Target.Width &&
              Result->Height == Target.Height &&
              ((!DirectPresent &&
                Result->Stride == Target.Width * sizeof(ULONG)) ||
               (DirectPresent && Result->Stride != 0)) &&
              Result->ClearColor == Context->ClearColor &&
              Result->PixelBytes == PixelBytes &&
              Result->VertexCount == VertexCount &&
              Result->DestinationX == DestinationX &&
              Result->DestinationY == DestinationY;
    if (!Success)
    {
        Context->Stats.HardwareClearFailureCount++;
        Context->Stats.HardwareTriangleFailureCount++;
        return FALSE;
    }

    if (OutputDepth)
    {
        Rpi5OglDiscardDeferredTextureClear(Context,
                                           Target.DepthTexture);
    }
    else if (!DirectPresent && Desktop2D)
        Rpi5OglMaterializeDeferredTargetClear(Context, &Target);
    else
        Rpi5OglDiscardDeferredTargetClear(Context, &Target);

    if (!DirectPresent)
    {
        if (OutputDepth)
        {
            for (Pixel = 0;
                 Pixel < Target.Width * Target.Height;
                 Pixel++)
            {
                Target.DepthData[Pixel] =
                    (GLdepth)(Result->Pixels[Pixel] >> 8);
            }
            Rpi5OglGl2InvalidateTextureUpload(Context->Gl2State);
        }
        else if (Desktop2D)
        {
            Rpi5OglCopyDesktopBatchPixels(&Target,
                                          Result->Pixels,
                                          MinimumX,
                                          MinimumY,
                                          MaximumX,
                                          MaximumY,
                                          BlendEnabled,
                                          PreserveDestinationAlphaBlend);
            if (Target.Framebuffer != 0)
                Rpi5OglGl2InvalidateTextureUpload(Context->Gl2State);
        }
        else
        {
            Rpi5OglCopyTargetPixels(&Target,
                                    0,
                                    0,
                                    Result->Pixels,
                                    Target.Width,
                                    Target.Height);
        }
    }
    else
    {
        Context->BatchDirectPresented = TRUE;
    }
    Context->HardwareClearFresh = FALSE;
    Context->Stats.HardwareClearCount++;
    Context->Stats.HardwareTriangleCount += VertexCount / 3;
    Context->Stats.HardwareProgramTriangleCount += ProgramTriangleCount;
    if (ProgramTriangleCount != 0)
        Context->Stats.LastProgramTriangleName = ProgramName;
    Context->Stats.LastWidth = Target.Width;
    Context->Stats.LastHeight = Target.Height;
    Context->Stats.LastTriangleWidth = Target.Width;
    Context->Stats.LastTriangleHeight = Target.Height;
    return TRUE;
}

static VOID
Rpi5OglTriangle(
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint ProvokingVertex)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    if (!Rpi5OglFboValidateCurrent(Context->FboState,
                                   "glBegin/glEnd primitive"))
    {
        return;
    }

    if (!Rpi5OglHardwareTriangle(Context,
                                 Mesa,
                                 Vertex0,
                                 Vertex1,
                                 Vertex2,
                                 ProvokingVertex))
    {
        Rpi5OglSoftwareTriangle(Mesa,
                                Vertex0,
                                Vertex1,
                                Vertex2,
                                ProvokingVertex);
    }
}

static VOID
Rpi5OglRejectPoints(
    _In_ GLcontext *Mesa,
    _In_ GLuint First,
    _In_ GLuint Last)
{
    UNREFERENCED_PARAMETER(First);
    UNREFERENCED_PARAMETER(Last);
    (void)Rpi5OglFboValidateCurrent(
        ((PRPI5VC4_OGL_CONTEXT)Mesa->DriverCtx)->FboState,
        "glBegin/glEnd points");
}

static VOID
Rpi5OglRejectLine(
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint ProvokingVertex)
{
    UNREFERENCED_PARAMETER(Vertex0);
    UNREFERENCED_PARAMETER(Vertex1);
    UNREFERENCED_PARAMETER(ProvokingVertex);
    (void)Rpi5OglFboValidateCurrent(
        ((PRPI5VC4_OGL_CONTEXT)Mesa->DriverCtx)->FboState,
        "glBegin/glEnd line");
}

static VOID
Rpi5OglRejectTriangle(
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint ProvokingVertex)
{
    UNREFERENCED_PARAMETER(Vertex0);
    UNREFERENCED_PARAMETER(Vertex1);
    UNREFERENCED_PARAMETER(Vertex2);
    UNREFERENCED_PARAMETER(ProvokingVertex);
    (void)Rpi5OglFboValidateCurrent(
        ((PRPI5VC4_OGL_CONTEXT)Mesa->DriverCtx)->FboState,
        "glBegin/glEnd triangle");
}

static VOID
Rpi5OglRejectQuad(
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint Vertex3,
    _In_ GLuint ProvokingVertex)
{
    UNREFERENCED_PARAMETER(Vertex0);
    UNREFERENCED_PARAMETER(Vertex1);
    UNREFERENCED_PARAMETER(Vertex2);
    UNREFERENCED_PARAMETER(Vertex3);
    UNREFERENCED_PARAMETER(ProvokingVertex);
    (void)Rpi5OglFboValidateCurrent(
        ((PRPI5VC4_OGL_CONTEXT)Mesa->DriverCtx)->FboState,
        "glBegin/glEnd quad");
}

static VOID
Rpi5OglRejectRect(
    _In_ GLcontext *Mesa,
    _In_ GLint X,
    _In_ GLint Y,
    _In_ GLint Width,
    _In_ GLint Height)
{
    UNREFERENCED_PARAMETER(X);
    UNREFERENCED_PARAMETER(Y);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    (void)Rpi5OglFboValidateCurrent(
        ((PRPI5VC4_OGL_CONTEXT)Mesa->DriverCtx)->FboState,
        "glRect");
}

static const CHAR *
Rpi5OglRendererString(VOID)
{
    return "RPi5-V3D-7.1-Hybrid-GL2";
}

static const GLubyte *
Rpi5OglGetString(
    _In_ GLcontext *Mesa,
    _In_ GLenum Name)
{
    static const GLubyte Version[] = "2.0 RPi5 V3D 7.1";
    static const GLubyte Extensions[] =
        "GL_EXT_paletted_texture GL_EXT_bgra GL_WIN_swap_hint "
        "GL_EXT_framebuffer_object GL_ARB_depth_texture";

    if (Name == GL_VERSION && !INSIDE_BEGIN_END(Mesa))
        return Version;
    if (Name == GL_EXTENSIONS && !INSIDE_BEGIN_END(Mesa))
        return Extensions;
    return gl_GetString(Mesa, Name);
}

static VOID APIENTRY
Rpi5OglGetBooleanv(
    _In_ GLenum ParameterName,
    _Out_ GLboolean *Parameters)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    if (Context != NULL &&
        Rpi5OglGl2GetBooleanv(Context->Gl2State,
                              ParameterName,
                              Parameters))
    {
        return;
    }
    _mesa_GetBooleanv(ParameterName, Parameters);
}

static VOID APIENTRY
Rpi5OglGetDoublev(
    _In_ GLenum ParameterName,
    _Out_ GLdouble *Parameters)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    if (Context != NULL &&
        Rpi5OglGl2GetDoublev(Context->Gl2State,
                             ParameterName,
                             Parameters))
    {
        return;
    }
    _mesa_GetDoublev(ParameterName, Parameters);
}

static VOID APIENTRY
Rpi5OglGetFloatv(
    _In_ GLenum ParameterName,
    _Out_ GLfloat *Parameters)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    if (Context != NULL &&
        Rpi5OglGl2GetFloatv(Context->Gl2State,
                            ParameterName,
                            Parameters))
    {
        return;
    }
    _mesa_GetFloatv(ParameterName, Parameters);
}

static VOID APIENTRY
Rpi5OglGetIntegerv(
    _In_ GLenum ParameterName,
    _Out_ GLint *Parameters)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    if (Context != NULL &&
        Rpi5OglGl2GetIntegerv(Context->Gl2State,
                              ParameterName,
                              Parameters))
    {
        return;
    }
    if (Context != NULL &&
        Rpi5OglBufferGetIntegerv(Context->BufferState,
                                 ParameterName,
                                 Parameters))
    {
        return;
    }
    Rpi5OglFboGetIntegerv(ParameterName, Parameters);
}

static VOID
Rpi5OglClearIndex(
    _In_ GLcontext *Mesa,
    _In_ GLuint Index)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    Context->ClearColor = Index;
}

static VOID
Rpi5OglClearColor(
    _In_ GLcontext *Mesa,
    _In_ GLubyte Red,
    _In_ GLubyte Green,
    _In_ GLubyte Blue,
    _In_ GLubyte Alpha)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    Context->ClearColor = Rpi5OglPackColor(Red, Green, Blue, Alpha);
}

static VOID
Rpi5OglSoftwareClear(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context,
    _In_ GLboolean All,
    _In_ GLint X,
    _In_ GLint Y,
    _In_ GLint Width,
    _In_ GLint Height)
{
    RPI5VC4_OGL_RENDER_TARGET Target;
    GLint Row;
    GLint Column;

    if (Context->BufferMode == GL_NONE ||
        !Rpi5OglResolveRenderTarget(Context, &Target) ||
        !Target.HasColor)
    {
        Context->HardwareClearFresh = FALSE;
        return;
    }
    Rpi5OglMaterializeDeferredTargetClear(Context, &Target);
    Context->HardwareClearFresh = FALSE;

    if (All)
    {
        X = 0;
        Y = 0;
        Width = Target.Width;
        Height = Target.Height;
    }
    if (X < 0)
    {
        Width += X;
        X = 0;
    }
    if (Y < 0)
    {
        Height += Y;
        Y = 0;
    }
    if (X + Width > (GLint)Target.Width)
        Width = Target.Width - X;
    if (Y + Height > (GLint)Target.Height)
        Height = Target.Height - Y;
    if (Width <= 0 || Height <= 0)
        return;

    for (Row = 0; Row < Height; Row++)
    {
        for (Column = 0; Column < Width; Column++)
        {
            Rpi5OglWriteTargetPixel(&Target,
                                    X + Column,
                                    Y + Row,
                                    Context->ClearColor);
        }
    }
    Context->Stats.SoftwareClearCount++;
}

static VOID
Rpi5OglClear(
    _In_ GLcontext *Mesa,
    _In_ GLboolean All,
    _In_ GLint X,
    _In_ GLint Y,
    _In_ GLint Width,
    _In_ GLint Height)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    if (Context->DesktopGraphActive && !Context->TerrainGraphActive)
        Rpi5OglResetDesktopGraph(Context);
    if (All && Rpi5OglHardwareClear(Context))
    {
        if (Rpi5OglFboCurrentName(Context->FboState) == 0 &&
            Context->BufferMode == GL_FRONT)
            Rpi5OglPresent(Context);
        return;
    }

    Rpi5OglSoftwareClear(Context, All, X, Y, Width, Height);
}

static VOID
Rpi5OglBatchClearColorAndDepth(
    _In_ GLcontext *Mesa,
    _In_ GLboolean All,
    _In_ GLint X,
    _In_ GLint Y,
    _In_ GLint Width,
    _In_ GLint Height)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;

    if (!All ||
        X != 0 ||
        Y != 0 ||
        Width != (GLint)Context->Width ||
        Height != (GLint)Context->Height ||
        !Rpi5OglBatchStateSupported(Mesa))
    {
        Rpi5OglClear(Mesa, All, X, Y, Width, Height);
        gl_clear_depth_buffer(Mesa);
        return;
    }

    if (Rpi5OglResolveRenderTarget(Context, &Target))
        Rpi5OglDiscardDeferredTargetClear(Context, &Target);
    Context->HardwareClearFresh = FALSE;
    Context->BatchVertexCount = 0;
    Context->BatchRequest->DrawCount = 0;
    ZeroMemory(Context->BatchRequest->Draws,
               sizeof(Context->BatchRequest->Draws));
    ZeroMemory(Context->BatchRequest->ShaderUniforms,
               sizeof(Context->BatchRequest->ShaderUniforms));
    Context->BatchProgramTriangleCount = 0;
    Context->BatchProgramName = 0;
    Context->BatchProgramFlags = 0;
    Context->BatchTextured = FALSE;
    Context->BatchTextureLinear = FALSE;
    Context->BatchTextureMipmap = FALSE;
    Context->BatchTextureGeneration = 0;
    Context->BatchTextureGeneration1 = 0;
    Context->BatchDepthFunc = Mesa->Depth.Func;
    Context->BatchBlendEnabled = Mesa->Color.BlendEnabled;
    Context->BatchPreserveDestinationAlphaBlend =
        Rpi5OglPreserveDestinationAlphaBlendEnabled(Mesa);
    Context->BatchDesktop2D = FALSE;
    Context->HardwareBatchActive = TRUE;
}

static VOID
Rpi5OglSetIndex(
    _In_ GLcontext *Mesa,
    _In_ GLuint Index)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    Context->CurrentColor = Index;
}

static VOID
Rpi5OglSetColor(
    _In_ GLcontext *Mesa,
    _In_ GLubyte Red,
    _In_ GLubyte Green,
    _In_ GLubyte Blue,
    _In_ GLubyte Alpha)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    Context->CurrentColor = Rpi5OglPackColor(Red, Green, Blue, Alpha);
}

static GLboolean
Rpi5OglSetBuffer(
    _In_ GLcontext *Mesa,
    _In_ GLenum Mode)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    GLuint Framebuffer = Rpi5OglFboCurrentName(Context->FboState);

    if (Framebuffer != 0)
    {
        if (Mode != GL_COLOR_ATTACHMENT0_EXT && Mode != GL_NONE)
            return GL_FALSE;
    }
    else if (Mode != GL_FRONT && Mode != GL_BACK)
    {
        return GL_FALSE;
    }
    if (Context->BufferMode != Mode)
        Context->HardwareClearFresh = FALSE;
    Context->BufferMode = Mode;
    return GL_TRUE;
}

static VOID
Rpi5OglGetBufferSize(
    _In_ GLcontext *Mesa,
    _Out_ GLuint *Width,
    _Out_ GLuint *Height)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;

    if (!Rpi5OglResolveRenderTarget(Context, &Target))
    {
        *Width = Context->Width != 0 ? Context->Width : 1;
        *Height = Context->Height != 0 ? Context->Height : 1;
        return;
    }

    *Width = Target.Width;
    *Height = Target.Height;
}

static VOID
Rpi5OglWriteColorSpan(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_ GLint X,
    _In_ GLint Y,
    _In_reads_(Count) const GLubyte Red[],
    _In_reads_(Count) const GLubyte Green[],
    _In_reads_(Count) const GLubyte Blue[],
    _In_reads_(Count) const GLubyte Alpha[],
    _In_reads_opt_(Count) const GLubyte Mask[])
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;
    GLuint Index;

    Context->HardwareClearFresh = FALSE;
    if (!Rpi5OglResolveRenderTarget(Context, &Target))
        return;
    Rpi5OglMaterializeDeferredTargetClear(Context, &Target);

    for (Index = 0; Index < Count; Index++)
    {
        GLint PixelX = X + Index;

        if ((Mask == NULL || Mask[Index]) &&
            Rpi5OglTargetPixelValid(&Target, PixelX, Y))
        {
            Rpi5OglWriteTargetPixel(
                &Target,
                PixelX,
                Y,
                Rpi5OglPackColor(Red[Index],
                                 Green[Index],
                                 Blue[Index],
                                 Alpha[Index]));
        }
    }
}

static VOID
Rpi5OglWriteMonoSpan(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_ GLint X,
    _In_ GLint Y,
    _In_reads_opt_(Count) const GLubyte Mask[])
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;
    GLuint Index;

    Context->HardwareClearFresh = FALSE;
    if (!Rpi5OglResolveRenderTarget(Context, &Target))
        return;
    Rpi5OglMaterializeDeferredTargetClear(Context, &Target);

    for (Index = 0; Index < Count; Index++)
    {
        GLint PixelX = X + Index;

        if ((Mask == NULL || Mask[Index]) &&
            Rpi5OglTargetPixelValid(&Target, PixelX, Y))
        {
            Rpi5OglWriteTargetPixel(&Target,
                                    PixelX,
                                    Y,
                                    Context->CurrentColor);
        }
    }
}

static VOID
Rpi5OglWriteColorPixels(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_reads_(Count) const GLint X[],
    _In_reads_(Count) const GLint Y[],
    _In_reads_(Count) const GLubyte Red[],
    _In_reads_(Count) const GLubyte Green[],
    _In_reads_(Count) const GLubyte Blue[],
    _In_reads_(Count) const GLubyte Alpha[],
    _In_reads_opt_(Count) const GLubyte Mask[])
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;
    GLuint Index;

    Context->HardwareClearFresh = FALSE;
    if (!Rpi5OglResolveRenderTarget(Context, &Target))
        return;
    Rpi5OglMaterializeDeferredTargetClear(Context, &Target);

    for (Index = 0; Index < Count; Index++)
    {
        if ((Mask == NULL || Mask[Index]) &&
            Rpi5OglTargetPixelValid(&Target, X[Index], Y[Index]))
        {
            Rpi5OglWriteTargetPixel(
                &Target,
                X[Index],
                Y[Index],
                Rpi5OglPackColor(Red[Index],
                                 Green[Index],
                                 Blue[Index],
                                 Alpha[Index]));
        }
    }
}

static VOID
Rpi5OglWriteMonoPixels(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_reads_(Count) const GLint X[],
    _In_reads_(Count) const GLint Y[],
    _In_reads_opt_(Count) const GLubyte Mask[])
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;
    GLuint Index;

    Context->HardwareClearFresh = FALSE;
    if (!Rpi5OglResolveRenderTarget(Context, &Target))
        return;
    Rpi5OglMaterializeDeferredTargetClear(Context, &Target);

    for (Index = 0; Index < Count; Index++)
    {
        if ((Mask == NULL || Mask[Index]) &&
            Rpi5OglTargetPixelValid(&Target, X[Index], Y[Index]))
        {
            Rpi5OglWriteTargetPixel(&Target,
                                    X[Index],
                                    Y[Index],
                                    Context->CurrentColor);
        }
    }
}

static VOID
Rpi5OglReadColorSpan(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_ GLint X,
    _In_ GLint Y,
    _Out_writes_(Count) GLubyte Red[],
    _Out_writes_(Count) GLubyte Green[],
    _Out_writes_(Count) GLubyte Blue[],
    _Out_writes_(Count) GLubyte Alpha[])
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;
    GLuint Index;

    if (Rpi5OglFboCurrentName(Context->FboState) == 0 &&
        !Rpi5OglEnsureDesktopGraphReadback(Context))
    {
        ZeroMemory(Red, Count * sizeof(*Red));
        ZeroMemory(Green, Count * sizeof(*Green));
        ZeroMemory(Blue, Count * sizeof(*Blue));
        ZeroMemory(Alpha, Count * sizeof(*Alpha));
        return;
    }
    if (!Rpi5OglResolveRenderTarget(Context, &Target))
    {
        ZeroMemory(Red, Count * sizeof(*Red));
        ZeroMemory(Green, Count * sizeof(*Green));
        ZeroMemory(Blue, Count * sizeof(*Blue));
        ZeroMemory(Alpha, Count * sizeof(*Alpha));
        return;
    }
    Rpi5OglMaterializeDeferredTargetClear(Context, &Target);

    for (Index = 0; Index < Count; Index++)
    {
        GLint PixelX = X + Index;
        ULONG Color = 0;

        if (Rpi5OglTargetPixelValid(&Target, PixelX, Y))
            Color = Rpi5OglReadTargetPixel(&Target, PixelX, Y);
        Rpi5OglUnpackColor(Color,
                           &Red[Index],
                           &Green[Index],
                           &Blue[Index],
                           &Alpha[Index]);
    }
}

static VOID
Rpi5OglReadColorPixels(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_reads_(Count) const GLint X[],
    _In_reads_(Count) const GLint Y[],
    _Out_writes_(Count) GLubyte Red[],
    _Out_writes_(Count) GLubyte Green[],
    _Out_writes_(Count) GLubyte Blue[],
    _Out_writes_(Count) GLubyte Alpha[],
    _In_reads_opt_(Count) const GLubyte Mask[])
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    RPI5VC4_OGL_RENDER_TARGET Target;
    GLuint Index;

    if (Rpi5OglFboCurrentName(Context->FboState) == 0 &&
        !Rpi5OglEnsureDesktopGraphReadback(Context))
    {
        ZeroMemory(Red, Count * sizeof(*Red));
        ZeroMemory(Green, Count * sizeof(*Green));
        ZeroMemory(Blue, Count * sizeof(*Blue));
        ZeroMemory(Alpha, Count * sizeof(*Alpha));
        return;
    }
    if (!Rpi5OglResolveRenderTarget(Context, &Target))
    {
        ZeroMemory(Red, Count * sizeof(*Red));
        ZeroMemory(Green, Count * sizeof(*Green));
        ZeroMemory(Blue, Count * sizeof(*Blue));
        ZeroMemory(Alpha, Count * sizeof(*Alpha));
        return;
    }
    Rpi5OglMaterializeDeferredTargetClear(Context, &Target);

    for (Index = 0; Index < Count; Index++)
    {
        ULONG Color = 0;

        if (Mask != NULL && !Mask[Index])
            continue;
        if (Rpi5OglTargetPixelValid(&Target, X[Index], Y[Index]))
            Color = Rpi5OglReadTargetPixel(&Target, X[Index], Y[Index]);
        Rpi5OglUnpackColor(Color,
                           &Red[Index],
                           &Green[Index],
                           &Blue[Index],
                           &Alpha[Index]);
    }
}

static VOID
Rpi5OglWriteIndexSpan(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_ GLint X,
    _In_ GLint Y,
    _In_reads_(Count) const GLuint Index[],
    _In_reads_opt_(Count) const GLubyte Mask[])
{
    UNREFERENCED_PARAMETER(Mesa);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(X);
    UNREFERENCED_PARAMETER(Y);
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Mask);
}

static VOID
Rpi5OglWriteIndexPixels(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_reads_(Count) const GLint X[],
    _In_reads_(Count) const GLint Y[],
    _In_reads_(Count) const GLuint Index[],
    _In_reads_opt_(Count) const GLubyte Mask[])
{
    UNREFERENCED_PARAMETER(Mesa);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(X);
    UNREFERENCED_PARAMETER(Y);
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Mask);
}

static VOID
Rpi5OglReadIndexSpan(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_ GLint X,
    _In_ GLint Y,
    _Out_writes_(Count) GLuint Index[])
{
    UNREFERENCED_PARAMETER(Mesa);
    UNREFERENCED_PARAMETER(X);
    UNREFERENCED_PARAMETER(Y);
    ZeroMemory(Index, Count * sizeof(*Index));
}

static VOID
Rpi5OglReadIndexPixels(
    _In_ GLcontext *Mesa,
    _In_ GLuint Count,
    _In_reads_(Count) const GLint X[],
    _In_reads_(Count) const GLint Y[],
    _Out_writes_(Count) GLuint Index[],
    _In_reads_opt_(Count) const GLubyte Mask[])
{
    UNREFERENCED_PARAMETER(Mesa);
    UNREFERENCED_PARAMETER(X);
    UNREFERENCED_PARAMETER(Y);
    UNREFERENCED_PARAMETER(Mask);
    ZeroMemory(Index, Count * sizeof(*Index));
}

static VOID
Rpi5OglFinish(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    if (!Rpi5OglSubmitBatch(Context, FALSE))
    {
        gl_error(Mesa,
                 GL_INVALID_OPERATION,
                 "glFinish(RPi5 V3D batch submission)");
        return;
    }
    if (Rpi5OglFboCurrentName(Context->FboState) == 0 &&
        Context->BufferMode == GL_FRONT)
        Rpi5OglPresent(Context);
}

static VOID
Rpi5OglTextureImageChanged(
    _In_ GLcontext *Mesa,
    _In_ GLenum Target,
    _In_ struct gl_texture_object *Texture,
    _In_ GLint Level,
    _In_ GLint InternalFormat,
    _In_ const struct gl_texture_image *Image)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(InternalFormat);
    UNREFERENCED_PARAMETER(Image);
    Rpi5OglDiscardDeferredTextureClear(Context, Texture);
    Context->HardwareClearFresh = FALSE;
    Rpi5OglFboTextureChanged(Context->FboState, Texture);
    Rpi5OglGl2TextureChanged(Context->Gl2State, Texture);
}

static VOID
Rpi5OglTextureSubImageChanged(
    _In_ GLcontext *Mesa,
    _In_ GLenum Target,
    _In_ struct gl_texture_object *Texture,
    _In_ GLint Level,
    _In_ GLint XOffset,
    _In_ GLint YOffset,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLint InternalFormat,
    _In_ const struct gl_texture_image *Image)
{
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(XOffset);
    UNREFERENCED_PARAMETER(YOffset);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(InternalFormat);
    UNREFERENCED_PARAMETER(Image);
    ((PRPI5VC4_OGL_CONTEXT)Mesa->DriverCtx)->HardwareClearFresh = FALSE;
    Rpi5OglFboTextureChanged(
        ((PRPI5VC4_OGL_CONTEXT)Mesa->DriverCtx)->FboState,
        Texture);
    Rpi5OglGl2TextureChanged(
        ((PRPI5VC4_OGL_CONTEXT)Mesa->DriverCtx)->Gl2State,
        Texture);
}

static VOID
Rpi5OglTextureParameterChanged(
    _In_ GLcontext *Mesa,
    _In_ GLenum Target,
    _In_ struct gl_texture_object *Texture,
    _In_ GLenum Name,
    _In_ const GLfloat *Parameters)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Parameters);
    Rpi5OglGl2TextureChanged(Context->Gl2State, Texture);
}

static VOID
Rpi5OglTextureDeleted(
    _In_ GLcontext *Mesa,
    _In_ struct gl_texture_object *Texture)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    if (Context->TerrainGraphActive)
        Rpi5OglResetDesktopGraph(Context);
    Rpi5OglDiscardDeferredTextureClear(Context, Texture);
    Context->HardwareClearFresh = FALSE;
    Rpi5OglFboTextureDeleted(Context->FboState, Texture);
    Rpi5OglGl2TextureDeleted(Context->Gl2State, Texture);
}

static VOID
Rpi5OglTextureBound(
    _In_ GLcontext *Mesa,
    _In_ GLenum Target,
    _In_ struct gl_texture_object *Texture)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    Rpi5OglGl2TextureBound(Context->Gl2State, Target, Texture);
}

static VOID
Rpi5OglSetupDriver(
    _Inout_ GLcontext *Mesa)
{
    Mesa->Driver.RendererString = Rpi5OglRendererString;
    Mesa->Driver.UpdateState = Rpi5OglSetupDriver;
    Mesa->Driver.ClearIndex = Rpi5OglClearIndex;
    Mesa->Driver.ClearColor = Rpi5OglClearColor;
    Mesa->Driver.Clear = Rpi5OglClear;
    Mesa->Driver.ClearColorAndDepth = NULL;
    Mesa->Driver.Index = Rpi5OglSetIndex;
    Mesa->Driver.Color = Rpi5OglSetColor;
    Mesa->Driver.SetBuffer = Rpi5OglSetBuffer;
    Mesa->Driver.GetBufferSize = Rpi5OglGetBufferSize;
    Mesa->Driver.WriteColorSpan = Rpi5OglWriteColorSpan;
    Mesa->Driver.WriteMonocolorSpan = Rpi5OglWriteMonoSpan;
    Mesa->Driver.WriteMonoindexSpan = Rpi5OglWriteMonoSpan;
    Mesa->Driver.WriteColorPixels = Rpi5OglWriteColorPixels;
    Mesa->Driver.WriteMonocolorPixels = Rpi5OglWriteMonoPixels;
    Mesa->Driver.WriteMonoindexPixels = Rpi5OglWriteMonoPixels;
    Mesa->Driver.WriteIndexSpan = Rpi5OglWriteIndexSpan;
    Mesa->Driver.WriteIndexPixels = Rpi5OglWriteIndexPixels;
    Mesa->Driver.ReadIndexSpan = Rpi5OglReadIndexSpan;
    Mesa->Driver.ReadColorSpan = Rpi5OglReadColorSpan;
    Mesa->Driver.ReadIndexPixels = Rpi5OglReadIndexPixels;
    Mesa->Driver.ReadColorPixels = Rpi5OglReadColorPixels;
    Mesa->Driver.Finish = Rpi5OglFinish;
    Mesa->Driver.Flush = Rpi5OglFinish;
    Mesa->Driver.TexImage = Rpi5OglTextureImageChanged;
    Mesa->Driver.TexSubImage = Rpi5OglTextureSubImageChanged;
    Mesa->Driver.TexParameter = Rpi5OglTextureParameterChanged;
    Mesa->Driver.BindTexture = Rpi5OglTextureBound;
    Mesa->Driver.DeleteTexture = Rpi5OglTextureDeleted;
    Mesa->Driver.PointsFunc = NULL;
    Mesa->Driver.LineFunc = NULL;
    Mesa->Driver.TriangleFunc = NULL;
    Mesa->Driver.QuadFunc = NULL;
    Mesa->Driver.RectFunc = NULL;
    if (!Rpi5OglFboCurrentComplete(
            ((PRPI5VC4_OGL_CONTEXT)Mesa->DriverCtx)->FboState))
    {
        Mesa->Driver.PointsFunc = Rpi5OglRejectPoints;
        Mesa->Driver.LineFunc = Rpi5OglRejectLine;
        Mesa->Driver.TriangleFunc = Rpi5OglRejectTriangle;
        Mesa->Driver.QuadFunc = Rpi5OglRejectQuad;
        Mesa->Driver.RectFunc = Rpi5OglRejectRect;
    }
    else if (Rpi5OglBatchStateSupported(Mesa))
    {
        Mesa->Driver.ClearColorAndDepth =
            Rpi5OglBatchClearColorAndDepth;
        Mesa->Driver.TriangleFunc = Rpi5OglBatchTriangle;
        Mesa->Driver.QuadFunc = Rpi5OglBatchQuad;
    }
    else if (Rpi5OglTriangleStateSupported(Mesa))
        Mesa->Driver.TriangleFunc = Rpi5OglTriangle;
}

static VOID
Rpi5OglInitializeProcTable(VOID)
{
    if (Rpi5OglProcTable.EntryCount == RPI5VC4_OPENGL_ENTRY_COUNT)
        return;

    Rpi5OglProcTable.EntryCount = RPI5VC4_OPENGL_ENTRY_COUNT;
    CopyMemory(Rpi5OglProcTable.Entries,
               Rpi5OglDispatchEntries,
               sizeof(Rpi5OglDispatchEntries));
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_END_LIST_INDEX] =
        (PROC)Rpi5OglApiEndList;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_CALL_LIST_INDEX] =
        (PROC)Rpi5OglApiCallList;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_DELETE_LISTS_INDEX] =
        (PROC)Rpi5OglApiDeleteLists;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_BLEND_FUNC_INDEX] =
        (PROC)Rpi5OglApiBlendFunc;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_TEX_IMAGE_2D_INDEX] =
        (PROC)Rpi5OglFboTexImage2D;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_DRAW_BUFFER_INDEX] =
        (PROC)Rpi5OglFboDrawBuffer;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_CLEAR_INDEX] =
        (PROC)Rpi5OglApiClear;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_READ_BUFFER_INDEX] =
        (PROC)Rpi5OglFboReadBuffer;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_GET_BOOLEANV_INDEX] =
        (PROC)Rpi5OglGetBooleanv;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_GET_DOUBLEV_INDEX] =
        (PROC)Rpi5OglGetDoublev;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_GET_FLOATV_INDEX] =
        (PROC)Rpi5OglGetFloatv;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_GET_INTEGERV_INDEX] =
        (PROC)Rpi5OglGetIntegerv;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_GET_TEX_IMAGE_INDEX] =
        (PROC)Rpi5OglFboGetTexImage;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_DRAW_ARRAYS_INDEX] =
        (PROC)Rpi5OglDrawArrays;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_DRAW_ELEMENTS_INDEX] =
        (PROC)Rpi5OglDrawElements;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_TEX_SUB_IMAGE_2D_INDEX] =
        (PROC)Rpi5OglFboTexSubImage2D;
}

static VOID
Rpi5OglFillPixelFormat(
    _Out_ LPPIXELFORMATDESCRIPTOR PixelFormat)
{
    ZeroMemory(PixelFormat, sizeof(*PixelFormat));
    PixelFormat->nSize = sizeof(*PixelFormat);
    PixelFormat->nVersion = 1;
    PixelFormat->dwFlags = PFD_DRAW_TO_WINDOW |
                           PFD_SUPPORT_OPENGL |
                           PFD_DOUBLEBUFFER |
                           PFD_SWAP_COPY;
    PixelFormat->iPixelType = PFD_TYPE_RGBA;
    PixelFormat->cColorBits = 32;
    PixelFormat->cRedBits = 8;
    PixelFormat->cRedShift = 16;
    PixelFormat->cGreenBits = 8;
    PixelFormat->cGreenShift = 8;
    PixelFormat->cBlueBits = 8;
    PixelFormat->cBlueShift = 0;
    PixelFormat->cAlphaBits = 8;
    PixelFormat->cAlphaShift = 24;
    PixelFormat->cDepthBits = 24;
    PixelFormat->cStencilBits = 8;
    PixelFormat->iLayerType = PFD_MAIN_PLANE;
}

static BOOL APIENTRY
Rpi5Vc4GetStats(
    _Out_ PRPI5VC4_OGL_STATS Stats)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    if (Context == NULL || Stats == NULL ||
        Stats->Size < sizeof(*Stats))
    {
        return FALSE;
    }

    *Stats = Context->Stats;
    return TRUE;
}

BOOL WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Reserved);
    return TRUE;
}

BOOL WINAPI
DrvValidateVersion(
    _In_ DWORD DriverVersion)
{
    return DriverVersion == RPI5VC4_OPENGL_ICD_DRIVER_VERSION;
}

VOID WINAPI
DrvSetCallbackProcs(
    _In_ INT Count,
    _In_reads_opt_(Count) PROC *Callbacks)
{
    if (Callbacks == NULL || Count <= 0)
        return;
    if (Count >= 1)
        Rpi5OglSetCurrentValue = (PFN_SET_CURRENT_VALUE)Callbacks[0];
    if (Count >= 2)
        Rpi5OglGetCurrentValue = (PFN_GET_CURRENT_VALUE)Callbacks[1];
}

BOOL WINAPI
DrvCopyContext(
    _In_ DHGLRC SourceHandle,
    _In_ DHGLRC DestinationHandle,
    _In_ UINT Mask)
{
    PRPI5VC4_OGL_CONTEXT Source =
        Rpi5OglValidateContext(SourceHandle);
    PRPI5VC4_OGL_CONTEXT Destination =
        Rpi5OglValidateContext(DestinationHandle);

    if (Source == NULL || Destination == NULL)
        return FALSE;
    gl_copy_context(Source->MesaContext,
                    Destination->MesaContext,
                    Mask);
    return TRUE;
}

DHGLRC WINAPI
DrvCreateContext(
    _In_ HDC Hdc)
{
    PRPI5VC4_OGL_CONTEXT Context;
    ULONG Width;
    ULONG Height;

    if (Hdc == NULL || GetPixelFormat(Hdc) != 1 ||
        !Rpi5OglQueryV3d(Hdc))
    {
        return NULL;
    }

    Context = HeapAlloc(GetProcessHeap(),
                        HEAP_ZERO_MEMORY,
                        sizeof(*Context));
    if (Context == NULL)
        return NULL;
    Context->BatchRequest = HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        FIELD_OFFSET(RPI5VC4_V3D_BATCH_REQUEST, Vertices) +
        RPI5VC4_V3D_BATCH_MAX_VERTICES * sizeof(RPI5VC4_V3D_VERTEX) +
        RPI5VC4_V3D_JELLYFISH_MAX_VERTICES *
        sizeof(RPI5VC4_V3D_TEXCOORD));
    if (Context->BatchRequest == NULL)
        goto Failure;

    Context->Hdc = Hdc;
    Context->Visual = gl_create_visual(GL_TRUE,
                                      GL_FALSE,
                                      GL_TRUE,
                                      24,
                                      8,
                                      0,
                                      0,
                                      255.0f,
                                      255.0f,
                                      255.0f,
                                      255.0f,
                                      8,
                                      8,
                                      8,
                                      8);
    if (Context->Visual == NULL)
        goto Failure;

    Context->FrameBuffer = gl_create_framebuffer(Context->Visual);
    if (Context->FrameBuffer == NULL)
        goto Failure;

    Context->MesaContext = gl_create_context(Context->Visual,
                                             NULL,
                                             Context);
    if (Context->MesaContext == NULL)
        goto Failure;
    Context->MesaContext->AllowNpotTextures = GL_TRUE;
    Context->MesaContext->API.GetString = Rpi5OglGetString;
    if (!Rpi5OglBufferInitialize(&Context->BufferState,
                                 Context->MesaContext))
    {
        goto Failure;
    }
    if (!Rpi5OglFboInitialize(&Context->FboState,
                              Context->MesaContext,
                              Context->FrameBuffer))
    {
        goto Failure;
    }
    if (!Rpi5OglGl2Initialize(&Context->Gl2State,
                              Context->MesaContext,
                              Context->BufferState))
    {
        goto Failure;
    }

    Rpi5OglGetDrawableSize(Hdc, &Width, &Height);
    if (!Rpi5OglResizeBackBuffer(Context, Width, Height))
        goto Failure;

    Context->Signature = RPI5VC4_OGL_CONTEXT_SIGNATURE;
    Context->ClearColor = 0;
    Context->CurrentColor = 0xFFFFFFFF;
    Context->BufferMode = GL_BACK;
    Context->Stats.Size = sizeof(Context->Stats);
    Context->Stats.Version = RPI5VC4_OGL_STATS_VERSION;
    return (DHGLRC)Context;

Failure:
    Rpi5OglFreeCachedLists(Context);
    Rpi5OglGl2Cleanup(Context->Gl2State);
    Rpi5OglFboCleanup(Context->FboState);
    Rpi5OglBufferCleanup(Context->BufferState);
    if (Context->MesaContext != NULL)
        gl_destroy_context(Context->MesaContext);
    if (Context->FrameBuffer != NULL)
        gl_destroy_framebuffer(Context->FrameBuffer);
    if (Context->Visual != NULL)
        gl_destroy_visual(Context->Visual);
    if (Context->BackBuffer != NULL)
        HeapFree(GetProcessHeap(), 0, Context->BackBuffer);
    if (Context->BatchResult != NULL)
        HeapFree(GetProcessHeap(), 0, Context->BatchResult);
    if (Context->BatchRequest != NULL)
        HeapFree(GetProcessHeap(), 0, Context->BatchRequest);
    if (Context->TerrainVertices != NULL)
        HeapFree(GetProcessHeap(), 0, Context->TerrainVertices);
    HeapFree(GetProcessHeap(), 0, Context);
    return NULL;
}

DHGLRC WINAPI
DrvCreateLayerContext(
    _In_ HDC Hdc,
    _In_ INT LayerPlane)
{
    return LayerPlane == 0 ? DrvCreateContext(Hdc) : NULL;
}

BOOL WINAPI
DrvDeleteContext(
    _In_ DHGLRC ContextHandle)
{
    PRPI5VC4_OGL_CONTEXT Context =
        Rpi5OglValidateContext(ContextHandle);

    if (Context == NULL)
        return FALSE;
    if (Rpi5OglCurrentContext() == Context)
    {
        gl_make_current(NULL, NULL);
        if (Rpi5OglSetCurrentValue != NULL)
            Rpi5OglSetCurrentValue(NULL);
    }

    Context->Signature = 0;
    Rpi5OglFreeCachedLists(Context);
    Rpi5OglGl2Cleanup(Context->Gl2State);
    Rpi5OglFboCleanup(Context->FboState);
    Rpi5OglBufferCleanup(Context->BufferState);
    gl_destroy_context(Context->MesaContext);
    gl_destroy_framebuffer(Context->FrameBuffer);
    gl_destroy_visual(Context->Visual);
    HeapFree(GetProcessHeap(), 0, Context->BackBuffer);
    if (Context->BatchResult != NULL)
        HeapFree(GetProcessHeap(), 0, Context->BatchResult);
    if (Context->TerrainVertices != NULL)
        HeapFree(GetProcessHeap(), 0, Context->TerrainVertices);
    HeapFree(GetProcessHeap(), 0, Context->BatchRequest);
    HeapFree(GetProcessHeap(), 0, Context);
    return TRUE;
}

BOOL WINAPI
DrvDescribeLayerPlane(
    _In_ HDC Hdc,
    _In_ INT PixelFormat,
    _In_ INT LayerPlane,
    _In_ UINT Bytes,
    _Out_writes_bytes_(Bytes) LPLAYERPLANEDESCRIPTOR Descriptor)
{
    UNREFERENCED_PARAMETER(Hdc);

    if (PixelFormat != 1 || LayerPlane != 0 ||
        Descriptor == NULL || Bytes < sizeof(*Descriptor))
    {
        return FALSE;
    }

    ZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->nSize = sizeof(*Descriptor);
    Descriptor->nVersion = 1;
    Descriptor->dwFlags = LPD_SUPPORT_OPENGL;
    Descriptor->iPixelType = PFD_TYPE_RGBA;
    Descriptor->cColorBits = 32;
    Descriptor->cAlphaBits = 8;
    return TRUE;
}

INT WINAPI
DrvDescribePixelFormat(
    _In_ HDC Hdc,
    _In_ INT PixelFormat,
    _In_ UINT Bytes,
    _Out_writes_bytes_opt_(Bytes) LPPIXELFORMATDESCRIPTOR Descriptor)
{
    if (!Rpi5OglQueryV3d(Hdc))
        return 0;
    if (Descriptor == NULL)
        return RPI5VC4_OPENGL_PIXEL_FORMAT_COUNT;
    if (PixelFormat != 1 || Bytes < sizeof(*Descriptor))
        return 0;

    Rpi5OglFillPixelFormat(Descriptor);
    return RPI5VC4_OPENGL_PIXEL_FORMAT_COUNT;
}

INT WINAPI
DrvGetLayerPaletteEntries(
    _In_ HDC Hdc,
    _In_ INT LayerPlane,
    _In_ INT Start,
    _In_ INT Count,
    _Out_writes_(Count) COLORREF *Colors)
{
    UNREFERENCED_PARAMETER(Hdc);
    UNREFERENCED_PARAMETER(LayerPlane);
    UNREFERENCED_PARAMETER(Start);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Colors);
    return 0;
}

static LPCSTR WINAPI
Rpi5OglWglGetExtensionsStringArb(
    _In_ HDC Hdc)
{
    UNREFERENCED_PARAMETER(Hdc);
    return "WGL_ARB_extensions_string";
}

PROC WINAPI
DrvGetProcAddress(
    _In_ LPCSTR Name)
{
    PROC Procedure;

    if (Name != NULL && lstrcmpA(Name, "wglGetExtensionsStringARB") == 0)
        return (PROC)Rpi5OglWglGetExtensionsStringArb;
    if (Name != NULL && lstrcmpA(Name, "Rpi5Vc4GetStats") == 0)
        return (PROC)Rpi5Vc4GetStats;
    if (Name != NULL && lstrcmpA(Name, "glDrawRangeElements") == 0)
        return (PROC)Rpi5OglDrawRangeElements;
    if (Name != NULL &&
        (lstrcmpA(Name, "glCopyTexSubImage3D") == 0 ||
         lstrcmpA(Name, "glCopyTexSubImage3DEXT") == 0))
    {
        return (PROC)_mesa_CopyTexSubImage3D;
    }
    if (Name != NULL &&
        (lstrcmpA(Name, "glTexImage3D") == 0 ||
         lstrcmpA(Name, "glTexImage3DEXT") == 0))
    {
        return (PROC)_mesa_TexImage3D;
    }
    if (Name != NULL &&
        (lstrcmpA(Name, "glTexSubImage3D") == 0 ||
         lstrcmpA(Name, "glTexSubImage3DEXT") == 0))
    {
        return (PROC)_mesa_TexSubImage3D;
    }
    if (Name != NULL &&
        (Procedure = Rpi5OglBufferGetProcAddress(Name)) != NULL)
    {
        return Procedure;
    }
    if (Name != NULL &&
        (Procedure = Rpi5OglFboGetProcAddress(Name)) != NULL)
    {
        return Procedure;
    }
    if (Name != NULL && (Procedure = Rpi5OglGl2GetProcAddress(Name)) != NULL)
        return Procedure;
    return NULL;
}

VOID WINAPI
DrvReleaseContext(
    _In_ DHGLRC ContextHandle)
{
    PRPI5VC4_OGL_CONTEXT Context =
        Rpi5OglValidateContext(ContextHandle);

    if (Context != NULL && Rpi5OglCurrentContext() == Context)
    {
        gl_make_current(NULL, NULL);
        if (Rpi5OglSetCurrentValue != NULL)
            Rpi5OglSetCurrentValue(NULL);
    }
}

BOOL WINAPI
DrvRealizeLayerPalette(
    _In_ HDC Hdc,
    _In_ INT LayerPlane,
    _In_ BOOL Realize)
{
    UNREFERENCED_PARAMETER(Hdc);
    UNREFERENCED_PARAMETER(Realize);
    return LayerPlane == 0;
}

const VOID * WINAPI
DrvSetContext(
    _In_ HDC Hdc,
    _In_ DHGLRC ContextHandle,
    _In_ PVOID SetProcTable)
{
    PRPI5VC4_OGL_CONTEXT Context =
        Rpi5OglValidateContext(ContextHandle);

    UNREFERENCED_PARAMETER(SetProcTable);
    if (Context == NULL || Hdc == NULL ||
        Rpi5OglSetCurrentValue == NULL)
    {
        return NULL;
    }

    if (Context->Hdc != Hdc)
    {
        Rpi5OglDiscardDeferredBackBufferClear(Context);
        Context->HardwareClearFresh = FALSE;
    }
    Context->Hdc = Hdc;
    if (!Rpi5OglRefreshDrawable(Context))
        return NULL;

    Rpi5OglSetCurrentValue(Context);
    gl_make_current(Context->MesaContext, Context->FrameBuffer);
    Rpi5OglSetupDriver(Context->MesaContext);
    if (Context->MesaContext->Viewport.Width == 0 &&
        Context->MesaContext->Viewport.Height == 0)
    {
        gl_Viewport(Context->MesaContext,
                    0,
                    0,
                    Context->Width,
                    Context->Height);
    }
    if (Rpi5OglFboCurrentName(Context->FboState) == 0)
        gl_ResizeBuffersMESA(Context->MesaContext);
    else
        Rpi5OglFboRestoreBinding(Context->FboState);
    Rpi5OglInitializeProcTable();
    return &Rpi5OglProcTable;
}

INT WINAPI
DrvSetLayerPaletteEntries(
    _In_ HDC Hdc,
    _In_ INT LayerPlane,
    _In_ INT Start,
    _In_ INT Count,
    _In_reads_(Count) const COLORREF *Colors)
{
    UNREFERENCED_PARAMETER(Hdc);
    UNREFERENCED_PARAMETER(LayerPlane);
    UNREFERENCED_PARAMETER(Start);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Colors);
    return 0;
}

BOOL WINAPI
DrvSetPixelFormat(
    _In_ HDC Hdc,
    _In_ INT PixelFormat)
{
    return PixelFormat == 1 && Rpi5OglQueryV3d(Hdc);
}

BOOL WINAPI
DrvShareLists(
    _In_ DHGLRC SourceHandle,
    _In_ DHGLRC DestinationHandle)
{
    PRPI5VC4_OGL_CONTEXT Source =
        Rpi5OglValidateContext(SourceHandle);
    PRPI5VC4_OGL_CONTEXT Destination =
        Rpi5OglValidateContext(DestinationHandle);

    return Source != NULL && Source == Destination;
}

BOOL WINAPI
DrvSwapBuffers(
    _In_ HDC Hdc)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    if (Context == NULL || Context->Hdc != Hdc)
        return FALSE;
    if (!Rpi5OglSubmitBatch(Context, TRUE))
        return FALSE;
    if (Context->BatchDirectPresented)
    {
        Context->BatchDirectPresented = FALSE;
        return TRUE;
    }
    return Rpi5OglPresent(Context);
}

BOOL WINAPI
DrvSwapLayerBuffers(
    _In_ HDC Hdc,
    _In_ UINT Planes)
{
    if (Planes != 0 && (Planes & WGL_SWAP_MAIN_PLANE) == 0)
        return FALSE;
    return DrvSwapBuffers(Hdc);
}
