/*
 * PROJECT:     ReactOS VirtIO GPU OpenGL ICD scaffold
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     XPDM OpenGL ICD entry points for the VirtIO GPU display driver
 */

#include <stdarg.h>
#include <stddef.h>
#include <math.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winioctl.h>
#include <GL/gl.h>

#include "virtgpu_shared.h"

#define VIRTGPU_OGL_CONTEXT_SIGNATURE 'cOgV'
#define VIRTGPU_OPENGL_ICD_DRIVER_VERSION 1
#define VIRTGPU_OPENGL_ENTRY_COUNT 336
#define VIRTGPU_OPENGL_PIXEL_FORMAT_COUNT 1
#define VIRTGPU_OGL_MAX_IMMEDIATE_VERTICES 4096
#define VIRTGPU_OGL_MODELVIEW_STACK_DEPTH 32
#define VIRTGPU_OGL_PROJECTION_STACK_DEPTH 2
#define VIRTGPU_OGL_TEXTURE_STACK_DEPTH 2
#define VIRTGPU_OGL_MAX_TEXTURES 256
#define VIRTGPU_OGL_MAX_DISPLAY_LISTS 256
#define VIRTGPU_OGL_INITIAL_LIST_COMMANDS 64
#define VIRTGPU_OGL_MAX_LIST_COMMANDS 4096
#define VIRTGPU_OGL_MAX_LIST_RECURSION 32
#define VIRTGPU_OGL_PI 3.14159265358979323846

#define VIRTGPU_OGL_CAP_ALPHA_TEST       0x00000001
#define VIRTGPU_OGL_CAP_BLEND            0x00000002
#define VIRTGPU_OGL_CAP_COLOR_MATERIAL   0x00000004
#define VIRTGPU_OGL_CAP_CULL_FACE        0x00000008
#define VIRTGPU_OGL_CAP_DEPTH_TEST       0x00000010
#define VIRTGPU_OGL_CAP_DITHER           0x00000020
#define VIRTGPU_OGL_CAP_FOG              0x00000040
#define VIRTGPU_OGL_CAP_LIGHTING         0x00000080
#define VIRTGPU_OGL_CAP_LINE_SMOOTH      0x00000100
#define VIRTGPU_OGL_CAP_LINE_STIPPLE     0x00000200
#define VIRTGPU_OGL_CAP_LOGIC_OP         0x00000400
#define VIRTGPU_OGL_CAP_NORMALIZE        0x00000800
#define VIRTGPU_OGL_CAP_POINT_SMOOTH     0x00001000
#define VIRTGPU_OGL_CAP_POLYGON_SMOOTH   0x00002000
#define VIRTGPU_OGL_CAP_POLYGON_STIPPLE  0x00004000
#define VIRTGPU_OGL_CAP_SCISSOR_TEST     0x00008000
#define VIRTGPU_OGL_CAP_STENCIL_TEST     0x00010000
#define VIRTGPU_OGL_CAP_TEXTURE_1D       0x00020000
#define VIRTGPU_OGL_CAP_TEXTURE_2D       0x00040000
#define VIRTGPU_OGL_CAP_CLIP_PLANE0      0x00100000

#define VIRTGPU_OGL_CLIENT_VERTEX_ARRAY  0x00000001
#define VIRTGPU_OGL_CLIENT_COLOR_ARRAY   0x00000002
#define VIRTGPU_OGL_CLIENT_NORMAL_ARRAY  0x00000004
#define VIRTGPU_OGL_CLIENT_TEXCOORD_ARRAY 0x00000008

#define VIRTGPU_OGL_VALID_CLEAR_MASK \
    (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_ACCUM_BUFFER_BIT)

/* Values match the upstream VirGL protocol and Gallium pipe ABI. */
#define VIRTGPU_OGL_VIRGL_CCMD_CREATE_OBJECT 1
#define VIRTGPU_OGL_VIRGL_CCMD_DESTROY_OBJECT 3
#define VIRTGPU_OGL_VIRGL_CCMD_SET_FRAMEBUFFER_STATE 5
#define VIRTGPU_OGL_VIRGL_CCMD_CLEAR 7
#define VIRTGPU_OGL_VIRGL_OBJECT_SURFACE 8
#define VIRTGPU_OGL_VIRGL_CMD0(Command, Object, Length) \
    ((ULONG)((Command) | ((Object) << 8) | ((Length) << 16)))
#define VIRTGPU_OGL_VIRGL_OBJ_SURFACE_SIZE 5
#define VIRTGPU_OGL_VIRGL_SET_FRAMEBUFFER_STATE_SIZE(ColorBuffers) ((ColorBuffers) + 2)
#define VIRTGPU_OGL_VIRGL_OBJ_CLEAR_SIZE 8

#define VIRTGPU_OGL_PIPE_TEXTURE_2D 2
#define VIRTGPU_OGL_PIPE_BIND_DEPTH_STENCIL (1 << 0)
#define VIRTGPU_OGL_PIPE_BIND_RENDER_TARGET (1 << 1)
#define VIRTGPU_OGL_PIPE_BIND_BLENDABLE (1 << 2)
#define VIRTGPU_OGL_PIPE_BIND_SAMPLER_VIEW (1 << 3)
#define VIRTGPU_OGL_PIPE_CLEAR_DEPTH (1 << 0)
#define VIRTGPU_OGL_PIPE_CLEAR_STENCIL (1 << 1)
#define VIRTGPU_OGL_PIPE_CLEAR_COLOR0 (1 << 2)
#define VIRTGPU_OGL_VIRGL_FORMAT_B8G8R8A8_UNORM 1
#define VIRTGPU_OGL_VIRGL_FORMAT_Z24_UNORM_S8_UINT 19
#define VIRTGPU_OGL_COLOR_SURFACE_HANDLE 1
#define VIRTGPU_OGL_DEPTH_STENCIL_SURFACE_HANDLE 2
#define VIRTGPU_OGL_CMDBUF_DWORDS 128
#define VIRTGPU_OGL_MAX_TRANSFER_SIZE (64 * 1024 * 1024)

DECLARE_HANDLE(DHGLRC);

typedef struct _VIRTGPU_OGL_PROC_TABLE
{
    INT EntryCount;
    PROC Entries[VIRTGPU_OPENGL_ENTRY_COUNT];
} VIRTGPU_OGL_PROC_TABLE, *PVIRTGPU_OGL_PROC_TABLE;

typedef VOID (APIENTRY *PFN_SETPROCTABLE)(const VIRTGPU_OGL_PROC_TABLE *);
typedef VOID (APIENTRY *PFN_SET_CURRENT_VALUE)(PVOID);
typedef PVOID (APIENTRY *PFN_GET_CURRENT_VALUE)(VOID);
typedef DHGLRC (APIENTRY *PFN_GET_CURRENT_DHGLRC)(VOID);

typedef struct _VIRTGPU_OGL_VERTEX
{
    GLfloat X;
    GLfloat Y;
    GLfloat Z;
    GLfloat W;
    ULONG OldVertexCount;
    COLORREF Color;
} VIRTGPU_OGL_VERTEX, *PVIRTGPU_OGL_VERTEX;

typedef struct _VIRTGPU_OGL_CMDBUF
{
    ULONG Dwords[VIRTGPU_OGL_CMDBUF_DWORDS];
    ULONG Count;
    BOOL Overflow;
} VIRTGPU_OGL_CMDBUF, *PVIRTGPU_OGL_CMDBUF;

typedef struct _VIRTGPU_OGL_TEXTURE
{
    BOOL Allocated;
    GLuint Name;
    GLenum Target;
    GLint Width;
    GLint Height;
    GLenum InternalFormat;
    GLenum Format;
    GLenum Type;
    GLenum MinFilter;
    GLenum MagFilter;
    GLenum WrapS;
    GLenum WrapT;
    ULONG DataSize;
    BYTE *Data;
} VIRTGPU_OGL_TEXTURE, *PVIRTGPU_OGL_TEXTURE;

typedef enum _VIRTGPU_OGL_LIST_OPCODE
{
    VIRTGPU_OGL_LIST_BEGIN,
    VIRTGPU_OGL_LIST_END,
    VIRTGPU_OGL_LIST_CALL_LIST,
    VIRTGPU_OGL_LIST_COLOR4F,
    VIRTGPU_OGL_LIST_NORMAL3F,
    VIRTGPU_OGL_LIST_TEXCOORD4F,
    VIRTGPU_OGL_LIST_VERTEX4F,
    VIRTGPU_OGL_LIST_MATRIX_MODE,
    VIRTGPU_OGL_LIST_LOAD_IDENTITY,
    VIRTGPU_OGL_LIST_LOAD_MATRIXF,
    VIRTGPU_OGL_LIST_MULT_MATRIXF,
    VIRTGPU_OGL_LIST_PUSH_MATRIX,
    VIRTGPU_OGL_LIST_POP_MATRIX,
    VIRTGPU_OGL_LIST_TRANSLATED,
    VIRTGPU_OGL_LIST_ROTATED,
    VIRTGPU_OGL_LIST_SCALED,
    VIRTGPU_OGL_LIST_ORTHO,
    VIRTGPU_OGL_LIST_FRUSTUM,
    VIRTGPU_OGL_LIST_VIEWPORT,
    VIRTGPU_OGL_LIST_SCISSOR,
    VIRTGPU_OGL_LIST_ENABLE,
    VIRTGPU_OGL_LIST_DISABLE,
    VIRTGPU_OGL_LIST_CLEAR_COLOR,
    VIRTGPU_OGL_LIST_CLEAR_DEPTH,
    VIRTGPU_OGL_LIST_CLEAR_STENCIL,
    VIRTGPU_OGL_LIST_CLEAR,
    VIRTGPU_OGL_LIST_DRAW_BUFFER,
    VIRTGPU_OGL_LIST_COLOR_MASK,
    VIRTGPU_OGL_LIST_DEPTH_MASK,
    VIRTGPU_OGL_LIST_STENCIL_MASK,
    VIRTGPU_OGL_LIST_DEPTH_RANGE,
    VIRTGPU_OGL_LIST_CULL_FACE,
    VIRTGPU_OGL_LIST_FRONT_FACE,
    VIRTGPU_OGL_LIST_LINE_WIDTH,
    VIRTGPU_OGL_LIST_POINT_SIZE,
    VIRTGPU_OGL_LIST_POLYGON_MODE,
    VIRTGPU_OGL_LIST_SHADE_MODEL,
    VIRTGPU_OGL_LIST_ALPHA_FUNC,
    VIRTGPU_OGL_LIST_BLEND_FUNC,
    VIRTGPU_OGL_LIST_STENCIL_FUNC,
    VIRTGPU_OGL_LIST_STENCIL_OP,
    VIRTGPU_OGL_LIST_DEPTH_FUNC,
    VIRTGPU_OGL_LIST_BIND_TEXTURE,
    VIRTGPU_OGL_LIST_TEX_PARAMETERI,
    VIRTGPU_OGL_LIST_TEX_IMAGE_1D,
    VIRTGPU_OGL_LIST_TEX_IMAGE_2D,
    VIRTGPU_OGL_LIST_TEX_SUB_IMAGE_1D,
    VIRTGPU_OGL_LIST_TEX_SUB_IMAGE_2D
} VIRTGPU_OGL_LIST_OPCODE;

typedef struct _VIRTGPU_OGL_LIST_COMMAND
{
    VIRTGPU_OGL_LIST_OPCODE Op;
    GLenum EnumArgs[4];
    GLint IntArgs[6];
    GLuint UintArgs[2];
    GLbitfield BitArgs;
    GLfloat FloatArgs[16];
    GLdouble DoubleArgs[8];
    ULONG DataSize;
    BYTE *Data;
} VIRTGPU_OGL_LIST_COMMAND, *PVIRTGPU_OGL_LIST_COMMAND;

typedef struct _VIRTGPU_OGL_DISPLAY_LIST
{
    BOOL Allocated;
    GLuint Name;
    ULONG Count;
    ULONG Capacity;
    PVIRTGPU_OGL_LIST_COMMAND Commands;
} VIRTGPU_OGL_DISPLAY_LIST, *PVIRTGPU_OGL_DISPLAY_LIST;

typedef struct _VIRTGPU_OGL_CONTEXT
{
    ULONG Signature;
    HDC hdc;
    ULONG ContextId;
    INT PixelFormat;
    GLenum LastError;
    GLbitfield EnableBits;
    GLclampf ClearColor[4];
    GLclampd ClearDepth;
    GLint ClearStencil;
    GLint DrawableWidth;
    GLint DrawableHeight;
    GLint VirglColorWidth;
    GLint VirglColorHeight;
    ULONG VirglColorResourceId;
    ULONG VirglColorSurfaceHandle;
    GLint VirglDepthStencilWidth;
    GLint VirglDepthStencilHeight;
    ULONG VirglDepthStencilResourceId;
    ULONG VirglDepthStencilSurfaceHandle;
    ULONGLONG LastVirglFenceId;
    BOOL VirglColorReady;
    BOOL VirglDepthStencilReady;
    BOOL VirglDisabled;
    BOOL VirglDepthStencilDisabled;
    GLint Viewport[4];
    GLint ScissorBox[4];
    GLboolean ColorMask[4];
    GLboolean DepthMask;
    GLuint StencilMask;
    GLenum MatrixMode;
    GLuint ModelViewStackTop;
    GLuint ProjectionStackTop;
    GLuint TextureStackTop;
    GLfloat ModelViewStack[VIRTGPU_OGL_MODELVIEW_STACK_DEPTH][16];
    GLfloat ProjectionStack[VIRTGPU_OGL_PROJECTION_STACK_DEPTH][16];
    GLfloat TextureStack[VIRTGPU_OGL_TEXTURE_STACK_DEPTH][16];
    GLclampd DepthRange[2];
    GLenum DrawBuffer;
    GLenum ReadBuffer;
    GLenum DepthFunc;
    GLenum AlphaFunc;
    GLclampf AlphaRef;
    GLenum BlendSrcFactor;
    GLenum BlendDstFactor;
    GLenum CullFaceMode;
    GLenum FrontFace;
    GLenum PolygonMode[2];
    GLenum ShadeModel;
    GLfloat PointSize;
    GLfloat LineWidth;
    GLenum StencilFunc;
    GLint StencilRef;
    GLuint StencilValueMask;
    GLenum StencilFail;
    GLenum StencilDepthFail;
    GLenum StencilDepthPass;
    GLint PackAlignment;
    GLint UnpackAlignment;
    GLuint NextTextureName;
    GLuint NextListName;
    GLuint ListBase;
    PVIRTGPU_OGL_DISPLAY_LIST RecordingList;
    GLenum RecordingMode;
    GLenum RecordingBeginMode;
    ULONG ListExecuteDepth;
    GLuint BoundTexture1D;
    GLuint BoundTexture2D;
    VIRTGPU_OGL_TEXTURE Textures[VIRTGPU_OGL_MAX_TEXTURES];
    VIRTGPU_OGL_DISPLAY_LIST DisplayLists[VIRTGPU_OGL_MAX_DISPLAY_LISTS];
    GLbitfield ClientArrayBits;
    GLint VertexArraySize;
    GLenum VertexArrayType;
    GLsizei VertexArrayStride;
    const GLvoid *VertexArrayPointer;
    GLint ColorArraySize;
    GLenum ColorArrayType;
    GLsizei ColorArrayStride;
    const GLvoid *ColorArrayPointer;
    GLenum NormalArrayType;
    GLsizei NormalArrayStride;
    const GLvoid *NormalArrayPointer;
    GLint TexCoordArraySize;
    GLenum TexCoordArrayType;
    GLsizei TexCoordArrayStride;
    const GLvoid *TexCoordArrayPointer;
    COLORREF CurrentColor;
    GLfloat CurrentNormal[3];
    GLfloat CurrentTexCoord[4];
    GLenum BeginMode;
    ULONG VertexCount;
    VIRTGPU_OGL_VERTEX Vertices[VIRTGPU_OGL_MAX_IMMEDIATE_VERTICES];
} VIRTGPU_OGL_CONTEXT, *PVIRTGPU_OGL_CONTEXT;

static PFN_SET_CURRENT_VALUE VirtGpuOglSetCurrentValue;
static PFN_GET_CURRENT_VALUE VirtGpuOglGetCurrentValue;
static PFN_GET_CURRENT_DHGLRC VirtGpuOglGetCurrentDHGLRC;
static VIRTGPU_OGL_PROC_TABLE VirtGpuOglProcTable;

static void APIENTRY
VirtGpuOglPixelStorei(GLenum Arg0, GLint Arg1);

static void APIENTRY
VirtGpuOglTexParameteri(GLenum Arg0, GLenum Arg1, GLint Arg2);

static void APIENTRY
VirtGpuOglTexParameteriv(GLenum Arg0, GLenum Arg1, const GLint *Arg2);

static void APIENTRY
VirtGpuOglGetTexParameteriv(GLenum Arg0, GLenum Arg1, GLint *Arg2);

static void APIENTRY
VirtGpuOglGetTexLevelParameteriv(GLenum Arg0, GLint Arg1, GLenum Arg2, GLint *Arg3);

static void APIENTRY
VirtGpuOglArrayElement(GLint Arg0);

static void APIENTRY
VirtGpuOglDrawElements(GLenum Arg0, GLsizei Arg1, GLenum Arg2, const GLvoid *Arg3);

static void APIENTRY
VirtGpuOglTexCoordPointer(GLint Arg0, GLenum Arg1, GLsizei Arg2, const GLvoid *Arg3);

static void APIENTRY
VirtGpuOglVertexPointer(GLint Arg0, GLenum Arg1, GLsizei Arg2, const GLvoid *Arg3);

static GLboolean APIENTRY
VirtGpuOglIsTexture(GLuint Arg0);

static BOOL
VirtGpuOglRecordingCompileOnly(_In_opt_ PVIRTGPU_OGL_CONTEXT Context);

static void APIENTRY
VirtGpuOglColor3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2);

static void APIENTRY
VirtGpuOglNormal3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2);

static void APIENTRY
VirtGpuOglTexCoord4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3);

static void APIENTRY
VirtGpuOglVertex4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3);

static GLfloat
VirtGpuOglColorFromByte(_In_ GLbyte Value);

static GLfloat
VirtGpuOglColorFromShort(_In_ GLshort Value);

static GLfloat
VirtGpuOglColorFromInt(_In_ GLint Value);

static GLfloat
VirtGpuOglColorFromUByte(_In_ GLubyte Value);

static GLfloat
VirtGpuOglColorFromUShort(_In_ GLushort Value);

static GLfloat
VirtGpuOglColorFromUInt(_In_ GLuint Value);

static ULONG
VirtGpuOglAlignedRowSize(_In_ ULONG RowSize, _In_ GLint Alignment);

static BOOL
VirtGpuOglEscapeIoControl(
    _In_ HDC hdc,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputSize) PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID OutputBuffer,
    _In_ ULONG OutputSize,
    _Out_opt_ PULONG Returned)
{
    PVIRTGPU_ESCAPE_3D_IOCTL Escape;
    ULONG HeaderSize = offsetof(VIRTGPU_ESCAPE_3D_IOCTL, Data);
    ULONG EscapeSize;
    INT Result;

    if (Returned != NULL)
        *Returned = 0;

    if ((InputSize != 0) && (InputBuffer == NULL))
        return FALSE;

    EscapeSize = HeaderSize + InputSize;
    Escape = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, EscapeSize);
    if (Escape == NULL)
        return FALSE;

    Escape->Size = EscapeSize;
    Escape->IoControlCode = IoControlCode;
    Escape->InputSize = InputSize;
    if (InputSize != 0)
        CopyMemory(Escape->Data, InputBuffer, InputSize);

    Result = ExtEscape(hdc,
                       VIRTGPU_ESCAPE_3D_IOCTL_CODE,
                       EscapeSize,
                       (LPCSTR)Escape,
                       OutputSize,
                       (LPSTR)OutputBuffer);
    HeapFree(GetProcessHeap(), 0, Escape);

    if (Result <= 0)
        return FALSE;

    if (Returned != NULL)
        *Returned = (ULONG)Result;
    return TRUE;
}

static PVIRTGPU_OGL_CONTEXT
VirtGpuOglValidateContext(_In_opt_ DHGLRC hglrc)
{
    PVIRTGPU_OGL_CONTEXT Context = (PVIRTGPU_OGL_CONTEXT)hglrc;

    if ((Context == NULL) ||
        (Context->Signature != VIRTGPU_OGL_CONTEXT_SIGNATURE))
    {
        return NULL;
    }

    return Context;
}

static PVIRTGPU_OGL_CONTEXT
VirtGpuOglCurrentContext(VOID)
{
    if (VirtGpuOglGetCurrentValue == NULL)
        return NULL;

    return VirtGpuOglValidateContext((DHGLRC)VirtGpuOglGetCurrentValue());
}

static VOID
VirtGpuOglSetCurrentContext(_In_opt_ PVIRTGPU_OGL_CONTEXT Context)
{
    if (VirtGpuOglSetCurrentValue != NULL)
        VirtGpuOglSetCurrentValue(Context);
}

static VOID
VirtGpuOglSetError(_In_opt_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLenum Error)
{
    if ((Context != NULL) && (Context->LastError == GL_NO_ERROR))
        Context->LastError = Error;
}

static VOID
VirtGpuOglUnsupportedCall(VOID)
{
    VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_OPERATION);
}

static VOID
VirtGpuOglUpdateDrawableSize(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ BOOL ResetViewport);

static GLboolean
VirtGpuOglIsEnabledInContext(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLenum Cap);

static GLfloat
VirtGpuOglClampFloat(_In_ GLfloat Value)
{
    if (Value < 0.0f)
        return 0.0f;
    if (Value > 1.0f)
        return 1.0f;
    return Value;
}

static GLdouble
VirtGpuOglClampDouble(_In_ GLdouble Value)
{
    if (Value < 0.0)
        return 0.0;
    if (Value > 1.0)
        return 1.0;
    return Value;
}

static BYTE
VirtGpuOglFloatToByte(_In_ GLfloat Value)
{
    Value = VirtGpuOglClampFloat(Value);
    return (BYTE)(Value * 255.0f + 0.5f);
}

static INT
VirtGpuOglRoundFloat(_In_ GLfloat Value)
{
    return (INT)(Value >= 0.0f ? Value + 0.5f : Value - 0.5f);
}

static COLORREF
VirtGpuOglColorFromFloat(
    _In_ GLfloat Red,
    _In_ GLfloat Green,
    _In_ GLfloat Blue)
{
    return RGB(VirtGpuOglFloatToByte(Red),
               VirtGpuOglFloatToByte(Green),
               VirtGpuOglFloatToByte(Blue));
}

static VOID
VirtGpuOglMatrixIdentity(_Out_writes_(16) GLfloat *Matrix)
{
    ZeroMemory(Matrix, 16 * sizeof(GLfloat));
    Matrix[0] = 1.0f;
    Matrix[5] = 1.0f;
    Matrix[10] = 1.0f;
    Matrix[15] = 1.0f;
}

static VOID
VirtGpuOglMatrixCopy(
    _Out_writes_(16) GLfloat *Destination,
    _In_reads_(16) const GLfloat *Source)
{
    CopyMemory(Destination, Source, 16 * sizeof(GLfloat));
}

static VOID
VirtGpuOglMatrixMultiply(
    _Out_writes_(16) GLfloat *Result,
    _In_reads_(16) const GLfloat *Left,
    _In_reads_(16) const GLfloat *Right)
{
    GLfloat Temp[16];
    ULONG Row;
    ULONG Column;

    for (Column = 0; Column < 4; ++Column)
    {
        for (Row = 0; Row < 4; ++Row)
        {
            Temp[Row + Column * 4] =
                Left[Row + 0 * 4] * Right[0 + Column * 4] +
                Left[Row + 1 * 4] * Right[1 + Column * 4] +
                Left[Row + 2 * 4] * Right[2 + Column * 4] +
                Left[Row + 3 * 4] * Right[3 + Column * 4];
        }
    }

    VirtGpuOglMatrixCopy(Result, Temp);
}

static VOID
VirtGpuOglMatrixVectorMultiply(
    _In_reads_(16) const GLfloat *Matrix,
    _In_reads_(4) const GLfloat *Vector,
    _Out_writes_(4) GLfloat *Result)
{
    ULONG Row;

    for (Row = 0; Row < 4; ++Row)
    {
        Result[Row] =
            Matrix[Row + 0 * 4] * Vector[0] +
            Matrix[Row + 1 * 4] * Vector[1] +
            Matrix[Row + 2 * 4] * Vector[2] +
            Matrix[Row + 3 * 4] * Vector[3];
    }
}

static GLfloat *
VirtGpuOglCurrentMatrix(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    switch (Context->MatrixMode)
    {
        case GL_MODELVIEW:
            return Context->ModelViewStack[Context->ModelViewStackTop];
        case GL_PROJECTION:
            return Context->ProjectionStack[Context->ProjectionStackTop];
        case GL_TEXTURE:
            return Context->TextureStack[Context->TextureStackTop];
        default:
            return Context->ModelViewStack[Context->ModelViewStackTop];
    }
}

static VOID
VirtGpuOglLoadCurrentMatrix(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_reads_(16) const GLfloat *Matrix)
{
    VirtGpuOglMatrixCopy(VirtGpuOglCurrentMatrix(Context), Matrix);
}

static VOID
VirtGpuOglMultCurrentMatrix(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_reads_(16) const GLfloat *Matrix)
{
    GLfloat Temp[16];
    GLfloat *Current;

    Current = VirtGpuOglCurrentMatrix(Context);
    VirtGpuOglMatrixMultiply(Temp, Current, Matrix);
    VirtGpuOglMatrixCopy(Current, Temp);
}

static VOID
VirtGpuOglMatrixFromDouble(
    _Out_writes_(16) GLfloat *Destination,
    _In_reads_(16) const GLdouble *Source)
{
    ULONG Index;

    for (Index = 0; Index < 16; ++Index)
        Destination[Index] = (GLfloat)Source[Index];
}

static VOID
VirtGpuOglTransformVertex(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ const VIRTGPU_OGL_VERTEX *Vertex,
    _Out_writes_(4) GLfloat *Clip)
{
    GLfloat Object[4];
    GLfloat Eye[4];

    Object[0] = Vertex->X;
    Object[1] = Vertex->Y;
    Object[2] = Vertex->Z;
    Object[3] = Vertex->W;

    VirtGpuOglMatrixVectorMultiply(Context->ModelViewStack[Context->ModelViewStackTop],
                                   Object,
                                   Eye);
    VirtGpuOglMatrixVectorMultiply(Context->ProjectionStack[Context->ProjectionStackTop],
                                   Eye,
                                   Clip);
}

static BOOL
VirtGpuOglValidCompareFunc(_In_ GLenum Func)
{
    switch (Func)
    {
        case GL_NEVER:
        case GL_LESS:
        case GL_EQUAL:
        case GL_LEQUAL:
        case GL_GREATER:
        case GL_NOTEQUAL:
        case GL_GEQUAL:
        case GL_ALWAYS:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglValidBlendFactor(_In_ GLenum Factor)
{
    switch (Factor)
    {
        case GL_ZERO:
        case GL_ONE:
        case GL_SRC_COLOR:
        case GL_ONE_MINUS_SRC_COLOR:
        case GL_DST_COLOR:
        case GL_ONE_MINUS_DST_COLOR:
        case GL_SRC_ALPHA:
        case GL_ONE_MINUS_SRC_ALPHA:
        case GL_DST_ALPHA:
        case GL_ONE_MINUS_DST_ALPHA:
        case GL_SRC_ALPHA_SATURATE:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglValidStencilOp(_In_ GLenum Operation)
{
    switch (Operation)
    {
        case GL_KEEP:
        case GL_ZERO:
        case GL_REPLACE:
        case GL_INCR:
        case GL_DECR:
        case GL_INVERT:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglArrayTypeSize(_In_ GLenum Type, _Out_ PULONG Size)
{
    switch (Type)
    {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            *Size = 1;
            return TRUE;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            *Size = 2;
            return TRUE;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
            *Size = 4;
            return TRUE;
        case GL_DOUBLE:
            *Size = 8;
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglTypeAllowedForVertexArray(_In_ GLenum Type)
{
    switch (Type)
    {
        case GL_SHORT:
        case GL_INT:
        case GL_FLOAT:
        case GL_DOUBLE:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglTypeAllowedForColorArray(_In_ GLenum Type)
{
    switch (Type)
    {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
        case GL_DOUBLE:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglTypeAllowedForNormalArray(_In_ GLenum Type)
{
    switch (Type)
    {
        case GL_BYTE:
        case GL_SHORT:
        case GL_INT:
        case GL_FLOAT:
        case GL_DOUBLE:
            return TRUE;
        default:
            return FALSE;
    }
}

static GLdouble
VirtGpuOglReadArrayComponent(_In_ const GLvoid *Pointer, _In_ GLenum Type)
{
    switch (Type)
    {
        case GL_BYTE:
            return *(const GLbyte *)Pointer;
        case GL_UNSIGNED_BYTE:
            return *(const GLubyte *)Pointer;
        case GL_SHORT:
            return *(const GLshort *)Pointer;
        case GL_UNSIGNED_SHORT:
            return *(const GLushort *)Pointer;
        case GL_INT:
            return *(const GLint *)Pointer;
        case GL_UNSIGNED_INT:
            return *(const GLuint *)Pointer;
        case GL_FLOAT:
            return *(const GLfloat *)Pointer;
        case GL_DOUBLE:
            return *(const GLdouble *)Pointer;
        default:
            return 0.0;
    }
}

static GLfloat
VirtGpuOglReadArrayFloat(
    _In_ const GLvoid *Base,
    _In_ GLint Size,
    _In_ GLenum Type,
    _In_ GLsizei Stride,
    _In_ GLint Index,
    _In_ GLint Component,
    _In_ GLfloat DefaultValue)
{
    ULONG TypeSize;
    const BYTE *Element;

    if ((Base == NULL) ||
        (Index < 0) ||
        (Component < 0) ||
        (Component >= Size) ||
        !VirtGpuOglArrayTypeSize(Type, &TypeSize))
    {
        return DefaultValue;
    }

    if (Stride == 0)
        Stride = Size * (GLsizei)TypeSize;

    Element = (const BYTE *)Base + ((ULONG)Index * (ULONG)Stride);
    return (GLfloat)VirtGpuOglReadArrayComponent(Element + ((ULONG)Component * TypeSize),
                                                 Type);
}

static GLfloat
VirtGpuOglReadArrayColorComponent(
    _In_ const GLvoid *Base,
    _In_ GLint Size,
    _In_ GLenum Type,
    _In_ GLsizei Stride,
    _In_ GLint Index,
    _In_ GLint Component,
    _In_ GLfloat DefaultValue)
{
    ULONG TypeSize;
    const BYTE *Element;
    GLdouble Value;

    if ((Base == NULL) ||
        (Index < 0) ||
        (Component < 0) ||
        (Component >= Size) ||
        !VirtGpuOglArrayTypeSize(Type, &TypeSize))
    {
        return DefaultValue;
    }

    if (Stride == 0)
        Stride = Size * (GLsizei)TypeSize;

    Element = (const BYTE *)Base + ((ULONG)Index * (ULONG)Stride);
    Value = VirtGpuOglReadArrayComponent(Element + ((ULONG)Component * TypeSize),
                                         Type);

    switch (Type)
    {
        case GL_BYTE:
            return VirtGpuOglColorFromByte((GLbyte)Value);
        case GL_UNSIGNED_BYTE:
            return VirtGpuOglColorFromUByte((GLubyte)Value);
        case GL_SHORT:
            return VirtGpuOglColorFromShort((GLshort)Value);
        case GL_UNSIGNED_SHORT:
            return VirtGpuOglColorFromUShort((GLushort)Value);
        case GL_INT:
            return VirtGpuOglColorFromInt((GLint)Value);
        case GL_UNSIGNED_INT:
            return VirtGpuOglColorFromUInt((GLuint)Value);
        case GL_FLOAT:
        case GL_DOUBLE:
            return VirtGpuOglClampFloat((GLfloat)Value);
        default:
            return DefaultValue;
    }
}

static BOOL
VirtGpuOglClientArrayCapToBit(_In_ GLenum Array, _Out_ GLbitfield *Bit)
{
    switch (Array)
    {
        case GL_VERTEX_ARRAY:
            *Bit = VIRTGPU_OGL_CLIENT_VERTEX_ARRAY;
            return TRUE;
        case GL_COLOR_ARRAY:
            *Bit = VIRTGPU_OGL_CLIENT_COLOR_ARRAY;
            return TRUE;
        case GL_NORMAL_ARRAY:
            *Bit = VIRTGPU_OGL_CLIENT_NORMAL_ARRAY;
            return TRUE;
        case GL_TEXTURE_COORD_ARRAY:
            *Bit = VIRTGPU_OGL_CLIENT_TEXCOORD_ARRAY;
            return TRUE;
        default:
            return FALSE;
    }
}

static VOID
VirtGpuOglFreeTexture(_Inout_ PVIRTGPU_OGL_TEXTURE Texture)
{
    if (Texture->Data != NULL)
        HeapFree(GetProcessHeap(), 0, Texture->Data);
    ZeroMemory(Texture, sizeof(*Texture));
}

static VOID
VirtGpuOglInitializeTextureDefaults(_Inout_ PVIRTGPU_OGL_TEXTURE Texture)
{
    Texture->MinFilter = GL_NEAREST_MIPMAP_LINEAR;
    Texture->MagFilter = GL_LINEAR;
    Texture->WrapS = GL_REPEAT;
    Texture->WrapT = GL_REPEAT;
}

static PVIRTGPU_OGL_TEXTURE
VirtGpuOglFindTexture(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_TEXTURES; ++Index)
    {
        if (Context->Textures[Index].Allocated &&
            (Context->Textures[Index].Name == Name))
        {
            return &Context->Textures[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_TEXTURE
VirtGpuOglAllocateTextureName(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_TEXTURES; ++Index)
    {
        if (!Context->Textures[Index].Allocated)
        {
            ZeroMemory(&Context->Textures[Index], sizeof(Context->Textures[Index]));
            Context->Textures[Index].Allocated = TRUE;
            Context->Textures[Index].Name = Name;
            VirtGpuOglInitializeTextureDefaults(&Context->Textures[Index]);
            return &Context->Textures[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static PVIRTGPU_OGL_TEXTURE
VirtGpuOglBoundTexture(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLenum Target)
{
    GLuint Name;

    switch (Target)
    {
        case GL_TEXTURE_1D:
            Name = Context->BoundTexture1D;
            break;
        case GL_TEXTURE_2D:
            Name = Context->BoundTexture2D;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return NULL;
    }

    if (Name == 0)
        return NULL;

    return VirtGpuOglFindTexture(Context, Name);
}

static BOOL
VirtGpuOglValidTextureFilter(_In_ GLenum Parameter, _In_ GLint Value)
{
    switch (Parameter)
    {
        case GL_TEXTURE_MIN_FILTER:
            switch (Value)
            {
                case GL_NEAREST:
                case GL_LINEAR:
                case GL_NEAREST_MIPMAP_NEAREST:
                case GL_LINEAR_MIPMAP_NEAREST:
                case GL_NEAREST_MIPMAP_LINEAR:
                case GL_LINEAR_MIPMAP_LINEAR:
                    return TRUE;
                default:
                    return FALSE;
            }
        case GL_TEXTURE_MAG_FILTER:
            return (Value == GL_NEAREST) || (Value == GL_LINEAR);
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglValidTextureWrap(_In_ GLint Value)
{
    return (Value == GL_CLAMP) || (Value == GL_REPEAT);
}

static BOOL
VirtGpuOglTextureFormatBytes(_In_ GLenum Format, _In_ GLenum Type, _Out_ PULONG Bytes)
{
    if (Type != GL_UNSIGNED_BYTE)
        return FALSE;

    switch (Format)
    {
        case GL_RGB:
            *Bytes = 3;
            return TRUE;
        case GL_RGBA:
            *Bytes = 4;
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglValidateTextureImageParameters(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint InternalFormat,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLint Border,
    _In_ GLenum Format,
    _In_ GLenum Type)
{
    ULONG BytesPerPixel;

    if (((Target != GL_TEXTURE_1D) && (Target != GL_TEXTURE_2D)) ||
        (Level != 0) ||
        (Border != 0) ||
        (Width < 0) ||
        (Height < 0))
    {
        VirtGpuOglSetError(Context,
                           ((Width < 0) || (Height < 0)) ?
                           GL_INVALID_VALUE : GL_INVALID_ENUM);
        return FALSE;
    }

    if ((InternalFormat != 3) &&
        (InternalFormat != 4) &&
        (InternalFormat != GL_RGB) &&
        (InternalFormat != GL_RGBA))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return FALSE;
    }

    if (!VirtGpuOglTextureFormatBytes(Format, Type, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return FALSE;
    }

    return TRUE;
}

static BOOL
VirtGpuOglValidateTextureSubImageParameters(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLenum Format,
    _In_ GLenum Type)
{
    ULONG BytesPerPixel;

    if (((Target != GL_TEXTURE_1D) && (Target != GL_TEXTURE_2D)) ||
        (Level != 0) ||
        (Width < 0) ||
        (Height < 0))
    {
        VirtGpuOglSetError(Context,
                           ((Width < 0) || (Height < 0)) ?
                           GL_INVALID_VALUE : GL_INVALID_ENUM);
        return FALSE;
    }

    if (!VirtGpuOglTextureFormatBytes(Format, Type, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return FALSE;
    }

    return TRUE;
}

static BOOL
VirtGpuOglCopyTexturePixels(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Pixels,
    _Outptr_result_bytebuffer_(*DataSize) BYTE **DataOut,
    _Out_ PULONG DataSize)
{
    ULONG BytesPerPixel;
    ULONG RowSize;
    ULONG SourceStride;
    ULONGLONG ImageSize64;
    BYTE *Data = NULL;
    GLsizei Row;

    *DataOut = NULL;
    *DataSize = 0;

    if (!VirtGpuOglTextureFormatBytes(Format, Type, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return FALSE;
    }

    RowSize = (ULONG)Width * BytesPerPixel;
    ImageSize64 = (ULONGLONG)RowSize * (ULONGLONG)Height;
    if (ImageSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return FALSE;
    }

    if (ImageSize64 != 0)
    {
        Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (ULONG)ImageSize64);
        if (Data == NULL)
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            return FALSE;
        }
    }

    if ((Pixels != NULL) && (Data != NULL))
    {
        SourceStride = VirtGpuOglAlignedRowSize(RowSize, Context->UnpackAlignment);
        for (Row = 0; Row < Height; ++Row)
        {
            CopyMemory(Data + ((ULONG)Row * RowSize),
                       (const BYTE *)Pixels + ((ULONG)Row * SourceStride),
                       RowSize);
        }
    }

    *DataOut = Data;
    *DataSize = (ULONG)ImageSize64;
    return TRUE;
}

static BOOL
VirtGpuOglStoreTextureImage(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _Inout_ PVIRTGPU_OGL_TEXTURE Texture,
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint InternalFormat,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLint Border,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Pixels)
{
    ULONG BytesPerPixel;
    ULONG SourceStride;
    ULONG RowSize;
    ULONGLONG ImageSize64;
    BYTE *Data;
    GLsizei Row;

    if ((Texture == NULL) ||
        ((Target != GL_TEXTURE_1D) && (Target != GL_TEXTURE_2D)) ||
        (Level != 0) ||
        (Border != 0) ||
        (Width < 0) ||
        (Height < 0))
    {
        VirtGpuOglSetError(Context,
                           ((Width < 0) || (Height < 0)) ?
                           GL_INVALID_VALUE : GL_INVALID_ENUM);
        return FALSE;
    }

    if ((InternalFormat != 3) &&
        (InternalFormat != 4) &&
        (InternalFormat != GL_RGB) &&
        (InternalFormat != GL_RGBA))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return FALSE;
    }

    if (!VirtGpuOglTextureFormatBytes(Format, Type, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return FALSE;
    }

    if (Target == GL_TEXTURE_1D)
        Height = 1;

    RowSize = (ULONG)Width * BytesPerPixel;
    ImageSize64 = (ULONGLONG)RowSize * (ULONGLONG)Height;
    if (ImageSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return FALSE;
    }

    Data = NULL;
    if (ImageSize64 != 0)
    {
        Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (ULONG)ImageSize64);
        if (Data == NULL)
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            return FALSE;
        }
    }

    if ((Pixels != NULL) && (Data != NULL))
    {
        SourceStride = VirtGpuOglAlignedRowSize(RowSize, Context->UnpackAlignment);
        for (Row = 0; Row < Height; ++Row)
        {
            CopyMemory(Data + ((ULONG)Row * RowSize),
                       (const BYTE *)Pixels + ((ULONG)Row * SourceStride),
                       RowSize);
        }
    }

    if (Texture->Data != NULL)
        HeapFree(GetProcessHeap(), 0, Texture->Data);

    Texture->Target = Target;
    Texture->Width = Width;
    Texture->Height = Height;
    Texture->InternalFormat = InternalFormat;
    Texture->Format = Format;
    Texture->Type = Type;
    Texture->DataSize = (ULONG)ImageSize64;
    Texture->Data = Data;
    return TRUE;
}

static ULONG
VirtGpuOglFloatBits(_In_ GLfloat Value)
{
    ULONG Bits;

    CopyMemory(&Bits, &Value, sizeof(Bits));
    return Bits;
}

static GLfloat
VirtGpuOglColorFromByte(_In_ GLbyte Value)
{
    return Value < 0 ? (GLfloat)Value / 128.0f : (GLfloat)Value / 127.0f;
}

static GLfloat
VirtGpuOglColorFromShort(_In_ GLshort Value)
{
    return Value < 0 ? (GLfloat)Value / 32768.0f : (GLfloat)Value / 32767.0f;
}

static GLfloat
VirtGpuOglColorFromInt(_In_ GLint Value)
{
    return Value < 0 ? (GLfloat)((GLdouble)Value / 2147483648.0) :
                       (GLfloat)((GLdouble)Value / 2147483647.0);
}

static GLfloat
VirtGpuOglColorFromUByte(_In_ GLubyte Value)
{
    return (GLfloat)Value / 255.0f;
}

static GLfloat
VirtGpuOglColorFromUShort(_In_ GLushort Value)
{
    return (GLfloat)Value / 65535.0f;
}

static GLfloat
VirtGpuOglColorFromUInt(_In_ GLuint Value)
{
    return (GLfloat)((GLdouble)Value / 4294967295.0);
}

static VOID
VirtGpuOglCmdInit(_Out_ PVIRTGPU_OGL_CMDBUF Cmd)
{
    ZeroMemory(Cmd, sizeof(*Cmd));
}

static VOID
VirtGpuOglCmdEmit(_Inout_ PVIRTGPU_OGL_CMDBUF Cmd, _In_ ULONG Dword)
{
    if (Cmd->Count >= VIRTGPU_OGL_CMDBUF_DWORDS)
    {
        Cmd->Overflow = TRUE;
        return;
    }

    Cmd->Dwords[Cmd->Count++] = Dword;
}

static VOID
VirtGpuOglCmdEmitFloat(_Inout_ PVIRTGPU_OGL_CMDBUF Cmd, _In_ GLfloat Value)
{
    VirtGpuOglCmdEmit(Cmd, VirtGpuOglFloatBits(Value));
}

static VOID
VirtGpuOglCmdEmitDouble(_Inout_ PVIRTGPU_OGL_CMDBUF Cmd, _In_ GLdouble Value)
{
    ULONGLONG Bits;

    CopyMemory(&Bits, &Value, sizeof(Bits));
    VirtGpuOglCmdEmit(Cmd, (ULONG)Bits);
    VirtGpuOglCmdEmit(Cmd, (ULONG)(Bits >> 32));
}

static BOOL
VirtGpuOglSubmitCmd(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ PVIRTGPU_OGL_CMDBUF Cmd,
    _Out_opt_ PULONGLONG FenceId)
{
    ULONG HeaderSize = offsetof(VIRTGPU_3D_SUBMIT, Commands);
    ULONG CommandBytes;
    ULONG BufferSize;
    PVIRTGPU_3D_SUBMIT Submit;
    ULONG Returned;
    BOOL Success;

    if (FenceId != NULL)
        *FenceId = 0;

    if ((Context == NULL) ||
        (Context->hdc == NULL) ||
        (Context->ContextId == 0) ||
        (Cmd == NULL) ||
        Cmd->Overflow ||
        (Cmd->Count == 0))
    {
        return FALSE;
    }

    CommandBytes = Cmd->Count * sizeof(ULONG);
    BufferSize = HeaderSize + CommandBytes;
    Submit = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BufferSize);
    if (Submit == NULL)
        return FALSE;

    Submit->ContextId = Context->ContextId;
    Submit->Size = CommandBytes;
    CopyMemory(Submit->Commands, Cmd->Dwords, CommandBytes);

    Success = VirtGpuOglEscapeIoControl(Context->hdc,
                                        IOCTL_VIDEO_VIRTGPU_3D_SUBMIT,
                                        Submit,
                                        BufferSize,
                                        Submit,
                                        HeaderSize,
                                        &Returned) &&
              (Returned >= HeaderSize);
    if (Success)
    {
        Context->LastVirglFenceId = Submit->FenceId;
        if (FenceId != NULL)
            *FenceId = Submit->FenceId;
    }
    HeapFree(GetProcessHeap(), 0, Submit);
    return Success;
}

static BOOL
VirtGpuOglFenceCompleted(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ ULONGLONG FenceId)
{
    VIRTGPU_3D_FENCE Fence;
    ULONG Returned;

    if ((Context == NULL) || (FenceId == 0))
        return TRUE;

    ZeroMemory(&Fence, sizeof(Fence));
    Fence.FenceId = FenceId;
    return VirtGpuOglEscapeIoControl(Context->hdc,
                                     IOCTL_VIDEO_VIRTGPU_3D_WAIT_FENCE,
                                     &Fence,
                                     sizeof(Fence),
                                     &Fence,
                                     sizeof(Fence),
                                     &Returned) &&
           (Returned >= sizeof(Fence)) &&
           (Fence.Completed != 0);
}

static VOID
VirtGpuOglWaitForLastVirglFence(_In_ PVIRTGPU_OGL_CONTEXT Context)
{
    ULONG Attempt;
    ULONGLONG FenceId;

    if ((Context == NULL) || (Context->LastVirglFenceId == 0))
        return;

    FenceId = Context->LastVirglFenceId;
    for (Attempt = 0; Attempt < 256; ++Attempt)
    {
        if (VirtGpuOglFenceCompleted(Context, FenceId))
        {
            Context->LastVirglFenceId = 0;
            return;
        }

        Sleep(0);
    }
}

static BOOL
VirtGpuOglCreateVirglSurface(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ ULONG ResourceId,
    _In_ ULONG SurfaceHandle,
    _In_ ULONG Format)
{
    VIRTGPU_OGL_CMDBUF Cmd;

    VirtGpuOglCmdInit(&Cmd);
    VirtGpuOglCmdEmit(&Cmd,
                      VIRTGPU_OGL_VIRGL_CMD0(VIRTGPU_OGL_VIRGL_CCMD_CREATE_OBJECT,
                                             VIRTGPU_OGL_VIRGL_OBJECT_SURFACE,
                                             VIRTGPU_OGL_VIRGL_OBJ_SURFACE_SIZE));
    VirtGpuOglCmdEmit(&Cmd, SurfaceHandle);
    VirtGpuOglCmdEmit(&Cmd, ResourceId);
    VirtGpuOglCmdEmit(&Cmd, Format);
    VirtGpuOglCmdEmit(&Cmd, 0);
    VirtGpuOglCmdEmit(&Cmd, 0);
    return VirtGpuOglSubmitCmd(Context, &Cmd, NULL);
}

static VOID
VirtGpuOglDestroyVirglSurface(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ ULONG SurfaceHandle)
{
    VIRTGPU_OGL_CMDBUF Cmd;

    if ((Context == NULL) || (SurfaceHandle == 0) || (Context->ContextId == 0))
        return;

    VirtGpuOglCmdInit(&Cmd);
    VirtGpuOglCmdEmit(&Cmd,
                      VIRTGPU_OGL_VIRGL_CMD0(VIRTGPU_OGL_VIRGL_CCMD_DESTROY_OBJECT,
                                             VIRTGPU_OGL_VIRGL_OBJECT_SURFACE,
                                             1));
    VirtGpuOglCmdEmit(&Cmd, SurfaceHandle);
    (VOID)VirtGpuOglSubmitCmd(Context, &Cmd, NULL);
}

static BOOL
VirtGpuOglCreateVirglTarget(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ ULONG Format,
    _In_ ULONG Bind,
    _In_ ULONG SurfaceHandle,
    _In_ BOOL DisableContextOnFailure,
    _Out_ PULONG ResourceId)
{
    VIRTGPU_3D_CREATE_RESOURCE Resource;
    VIRTGPU_3D_CONTEXT_RESOURCE ContextResource;
    ULONG Returned;
    ULONG BackingSize;

    if ((Context == NULL) || Context->VirglDisabled || (ResourceId == NULL))
        return FALSE;

    *ResourceId = 0;
    VirtGpuOglUpdateDrawableSize(Context, FALSE);
    if ((Context->DrawableWidth <= 0) || (Context->DrawableHeight <= 0))
        return FALSE;

    BackingSize = (ULONG)Context->DrawableWidth *
                  (ULONG)Context->DrawableHeight *
                  4;

    ZeroMemory(&Resource, sizeof(Resource));
    Resource.Target = VIRTGPU_OGL_PIPE_TEXTURE_2D;
    Resource.Format = Format;
    Resource.Bind = Bind;
    Resource.Width = Context->DrawableWidth;
    Resource.Height = Context->DrawableHeight;
    Resource.Depth = 1;
    Resource.ArraySize = 1;
    Resource.BackingSize = BackingSize;

    if (!VirtGpuOglEscapeIoControl(Context->hdc,
                                   IOCTL_VIDEO_VIRTGPU_3D_CREATE_RESOURCE,
                                   &Resource,
                                   sizeof(Resource),
                                   &Resource,
                                   sizeof(Resource),
                                   &Returned) ||
        (Returned < sizeof(Resource)) ||
        (Resource.ResourceId == 0))
    {
        if (DisableContextOnFailure)
            Context->VirglDisabled = TRUE;
        return FALSE;
    }

    ZeroMemory(&ContextResource, sizeof(ContextResource));
    ContextResource.ContextId = Context->ContextId;
    ContextResource.ResourceId = Resource.ResourceId;
    if (!VirtGpuOglEscapeIoControl(Context->hdc,
                                   IOCTL_VIDEO_VIRTGPU_3D_ATTACH_RESOURCE,
                                   &ContextResource,
                                   sizeof(ContextResource),
                                   NULL,
                                   0,
                                   NULL))
    {
        VIRTGPU_3D_RESOURCE Destroy;

        Destroy.ResourceId = Resource.ResourceId;
        (VOID)VirtGpuOglEscapeIoControl(Context->hdc,
                                        IOCTL_VIDEO_VIRTGPU_3D_DESTROY_RESOURCE,
                                        &Destroy,
                                        sizeof(Destroy),
                                        NULL,
                                        0,
                                        NULL);
        if (DisableContextOnFailure)
            Context->VirglDisabled = TRUE;
        return FALSE;
    }

    if (!VirtGpuOglCreateVirglSurface(Context,
                                      Resource.ResourceId,
                                      SurfaceHandle,
                                      Format))
    {
        VIRTGPU_3D_RESOURCE Destroy;

        (VOID)VirtGpuOglEscapeIoControl(Context->hdc,
                                        IOCTL_VIDEO_VIRTGPU_3D_DETACH_RESOURCE,
                                        &ContextResource,
                                        sizeof(ContextResource),
                                        NULL,
                                        0,
                                        NULL);
        Destroy.ResourceId = Resource.ResourceId;
        (VOID)VirtGpuOglEscapeIoControl(Context->hdc,
                                        IOCTL_VIDEO_VIRTGPU_3D_DESTROY_RESOURCE,
                                        &Destroy,
                                        sizeof(Destroy),
                                        NULL,
                                        0,
                                        NULL);
        if (DisableContextOnFailure)
            Context->VirglDisabled = TRUE;
        return FALSE;
    }

    *ResourceId = Resource.ResourceId;
    return TRUE;
}

static BOOL
VirtGpuOglCreateVirglColorTarget(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    ULONG ResourceId;

    if (!VirtGpuOglCreateVirglTarget(Context,
                                     VIRTGPU_OGL_VIRGL_FORMAT_B8G8R8A8_UNORM,
                                     VIRTGPU_OGL_PIPE_BIND_RENDER_TARGET |
                                     VIRTGPU_OGL_PIPE_BIND_BLENDABLE |
                                     VIRTGPU_OGL_PIPE_BIND_SAMPLER_VIEW,
                                     VIRTGPU_OGL_COLOR_SURFACE_HANDLE,
                                     TRUE,
                                     &ResourceId))
    {
        return FALSE;
    }

    Context->VirglColorResourceId = ResourceId;
    Context->VirglColorSurfaceHandle = VIRTGPU_OGL_COLOR_SURFACE_HANDLE;
    Context->VirglColorWidth = Context->DrawableWidth;
    Context->VirglColorHeight = Context->DrawableHeight;
    Context->VirglColorReady = TRUE;
    return TRUE;
}

static BOOL
VirtGpuOglCreateVirglDepthStencilTarget(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    ULONG ResourceId;

    if ((Context == NULL) || Context->VirglDepthStencilDisabled)
        return FALSE;

    if (!VirtGpuOglCreateVirglTarget(Context,
                                     VIRTGPU_OGL_VIRGL_FORMAT_Z24_UNORM_S8_UINT,
                                     VIRTGPU_OGL_PIPE_BIND_DEPTH_STENCIL,
                                     VIRTGPU_OGL_DEPTH_STENCIL_SURFACE_HANDLE,
                                     FALSE,
                                     &ResourceId))
    {
        Context->VirglDepthStencilDisabled = TRUE;
        return FALSE;
    }

    Context->VirglDepthStencilResourceId = ResourceId;
    Context->VirglDepthStencilSurfaceHandle = VIRTGPU_OGL_DEPTH_STENCIL_SURFACE_HANDLE;
    Context->VirglDepthStencilWidth = Context->DrawableWidth;
    Context->VirglDepthStencilHeight = Context->DrawableHeight;
    Context->VirglDepthStencilReady = TRUE;
    return TRUE;
}

static VOID
VirtGpuOglDestroyVirglColorTarget(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    VIRTGPU_3D_CONTEXT_RESOURCE ContextResource;
    VIRTGPU_3D_RESOURCE Resource;

    if ((Context == NULL) || (Context->VirglColorResourceId == 0))
        return;

    VirtGpuOglWaitForLastVirglFence(Context);
    VirtGpuOglDestroyVirglSurface(Context, Context->VirglColorSurfaceHandle);

    ZeroMemory(&ContextResource, sizeof(ContextResource));
    ContextResource.ContextId = Context->ContextId;
    ContextResource.ResourceId = Context->VirglColorResourceId;
    (VOID)VirtGpuOglEscapeIoControl(Context->hdc,
                                    IOCTL_VIDEO_VIRTGPU_3D_DETACH_RESOURCE,
                                    &ContextResource,
                                    sizeof(ContextResource),
                                    NULL,
                                    0,
                                    NULL);

    ZeroMemory(&Resource, sizeof(Resource));
    Resource.ResourceId = Context->VirglColorResourceId;
    (VOID)VirtGpuOglEscapeIoControl(Context->hdc,
                                    IOCTL_VIDEO_VIRTGPU_3D_DESTROY_RESOURCE,
                                    &Resource,
                                    sizeof(Resource),
                                    NULL,
                                    0,
                                    NULL);

    Context->VirglColorResourceId = 0;
    Context->VirglColorSurfaceHandle = 0;
    Context->VirglColorWidth = 0;
    Context->VirglColorHeight = 0;
    Context->VirglColorReady = FALSE;
}

static VOID
VirtGpuOglDestroyVirglDepthStencilTarget(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    VIRTGPU_3D_CONTEXT_RESOURCE ContextResource;
    VIRTGPU_3D_RESOURCE Resource;

    if ((Context == NULL) || (Context->VirglDepthStencilResourceId == 0))
        return;

    VirtGpuOglWaitForLastVirglFence(Context);
    VirtGpuOglDestroyVirglSurface(Context, Context->VirglDepthStencilSurfaceHandle);

    ZeroMemory(&ContextResource, sizeof(ContextResource));
    ContextResource.ContextId = Context->ContextId;
    ContextResource.ResourceId = Context->VirglDepthStencilResourceId;
    (VOID)VirtGpuOglEscapeIoControl(Context->hdc,
                                    IOCTL_VIDEO_VIRTGPU_3D_DETACH_RESOURCE,
                                    &ContextResource,
                                    sizeof(ContextResource),
                                    NULL,
                                    0,
                                    NULL);

    ZeroMemory(&Resource, sizeof(Resource));
    Resource.ResourceId = Context->VirglDepthStencilResourceId;
    (VOID)VirtGpuOglEscapeIoControl(Context->hdc,
                                    IOCTL_VIDEO_VIRTGPU_3D_DESTROY_RESOURCE,
                                    &Resource,
                                    sizeof(Resource),
                                    NULL,
                                    0,
                                    NULL);

    Context->VirglDepthStencilResourceId = 0;
    Context->VirglDepthStencilSurfaceHandle = 0;
    Context->VirglDepthStencilWidth = 0;
    Context->VirglDepthStencilHeight = 0;
    Context->VirglDepthStencilReady = FALSE;
}

static BOOL
VirtGpuOglEnsureVirglColorTarget(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    VirtGpuOglUpdateDrawableSize(Context, FALSE);

    if (Context->VirglColorReady &&
        (Context->VirglColorWidth == Context->DrawableWidth) &&
        (Context->VirglColorHeight == Context->DrawableHeight))
    {
        return TRUE;
    }

    VirtGpuOglDestroyVirglDepthStencilTarget(Context);
    VirtGpuOglDestroyVirglColorTarget(Context);
    return VirtGpuOglCreateVirglColorTarget(Context);
}

static BOOL
VirtGpuOglEnsureVirglDepthStencilTarget(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    VirtGpuOglUpdateDrawableSize(Context, FALSE);

    if (Context->VirglDepthStencilReady &&
        (Context->VirglDepthStencilWidth == Context->DrawableWidth) &&
        (Context->VirglDepthStencilHeight == Context->DrawableHeight))
    {
        return TRUE;
    }

    VirtGpuOglDestroyVirglDepthStencilTarget(Context);
    return VirtGpuOglCreateVirglDepthStencilTarget(Context);
}

static GLbitfield
VirtGpuOglVirglClear(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLbitfield Mask)
{
    VIRTGPU_OGL_CMDBUF Cmd;
    ULONG ClearBuffers = 0;
    ULONG ColorBuffers = 0;
    ULONG DepthStencilSurface = 0;
    GLbitfield SubmittedMask = 0;
    BOOL ClearDepth = FALSE;
    BOOL ClearStencil = FALSE;

    if ((Context == NULL) ||
        (VirtGpuOglIsEnabledInContext(Context, GL_SCISSOR_TEST) == GL_TRUE))
    {
        return 0;
    }

    if (((Mask & GL_COLOR_BUFFER_BIT) != 0) &&
        (Context->ColorMask[0] == GL_TRUE) &&
        (Context->ColorMask[1] == GL_TRUE) &&
        (Context->ColorMask[2] == GL_TRUE) &&
        (Context->ColorMask[3] == GL_TRUE) &&
        VirtGpuOglEnsureVirglColorTarget(Context))
    {
        ClearBuffers |= VIRTGPU_OGL_PIPE_CLEAR_COLOR0;
        ColorBuffers = 1;
        SubmittedMask |= GL_COLOR_BUFFER_BIT;
    }

    ClearDepth = ((Mask & GL_DEPTH_BUFFER_BIT) != 0) &&
                 (Context->DepthMask == GL_TRUE);
    ClearStencil = ((Mask & GL_STENCIL_BUFFER_BIT) != 0) &&
                   (Context->StencilMask == 0xFFFFFFFF);

    if (ClearDepth || ClearStencil)
    {
        if (VirtGpuOglEnsureVirglDepthStencilTarget(Context))
        {
            DepthStencilSurface = Context->VirglDepthStencilSurfaceHandle;

            if (ClearDepth)
            {
                ClearBuffers |= VIRTGPU_OGL_PIPE_CLEAR_DEPTH;
                SubmittedMask |= GL_DEPTH_BUFFER_BIT;
            }
            if (ClearStencil)
            {
                ClearBuffers |= VIRTGPU_OGL_PIPE_CLEAR_STENCIL;
                SubmittedMask |= GL_STENCIL_BUFFER_BIT;
            }
        }
    }

    if (ClearBuffers == 0)
        return 0;

    VirtGpuOglCmdInit(&Cmd);
    VirtGpuOglCmdEmit(&Cmd,
                      VIRTGPU_OGL_VIRGL_CMD0(VIRTGPU_OGL_VIRGL_CCMD_SET_FRAMEBUFFER_STATE,
                                             0,
                                             VIRTGPU_OGL_VIRGL_SET_FRAMEBUFFER_STATE_SIZE(ColorBuffers)));
    VirtGpuOglCmdEmit(&Cmd, ColorBuffers);
    VirtGpuOglCmdEmit(&Cmd, DepthStencilSurface);
    if (ColorBuffers != 0)
        VirtGpuOglCmdEmit(&Cmd, Context->VirglColorSurfaceHandle);

    VirtGpuOglCmdEmit(&Cmd,
                      VIRTGPU_OGL_VIRGL_CMD0(VIRTGPU_OGL_VIRGL_CCMD_CLEAR,
                                             0,
                                             VIRTGPU_OGL_VIRGL_OBJ_CLEAR_SIZE));
    VirtGpuOglCmdEmit(&Cmd, ClearBuffers);
    VirtGpuOglCmdEmitFloat(&Cmd, Context->ClearColor[0]);
    VirtGpuOglCmdEmitFloat(&Cmd, Context->ClearColor[1]);
    VirtGpuOglCmdEmitFloat(&Cmd, Context->ClearColor[2]);
    VirtGpuOglCmdEmitFloat(&Cmd, Context->ClearColor[3]);
    VirtGpuOglCmdEmitDouble(&Cmd, Context->ClearDepth);
    VirtGpuOglCmdEmit(&Cmd, Context->ClearStencil);

    return VirtGpuOglSubmitCmd(Context, &Cmd, NULL) ? SubmittedMask : 0;
}

static BOOL
VirtGpuOglValidPixelAlignment(_In_ GLint Alignment)
{
    return (Alignment == 1) ||
           (Alignment == 2) ||
           (Alignment == 4) ||
           (Alignment == 8);
}

static ULONG
VirtGpuOglAlignedRowSize(_In_ ULONG RowSize, _In_ GLint Alignment)
{
    ULONG Align = (ULONG)Alignment;

    return (RowSize + Align - 1) & ~(Align - 1);
}

static PVIRTGPU_3D_TRANSFER
VirtGpuOglReadVirglColorTarget(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    PVIRTGPU_3D_TRANSFER Transfer;
    ULONG HeaderSize = offsetof(VIRTGPU_3D_TRANSFER, Data);
    ULONG Returned;
    ULONG BufferSize;
    ULONG ImageSize;
    ULONG Stride;
    ULONGLONG ImageSize64;
    LONG Width;
    LONG Height;

    if ((Context == NULL) ||
        (Context->hdc == NULL) ||
        !Context->VirglColorReady ||
        (Context->VirglColorResourceId == 0))
    {
        return NULL;
    }

    Width = Context->VirglColorWidth;
    Height = Context->VirglColorHeight;
    if ((Width <= 0) || (Height <= 0))
        return NULL;

    ImageSize64 = (ULONGLONG)(ULONG)Width * (ULONGLONG)(ULONG)Height * 4ULL;
    if ((ImageSize64 == 0) || (ImageSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE))
        return NULL;

    ImageSize = (ULONG)ImageSize64;
    Stride = (ULONG)Width * 4;
    BufferSize = HeaderSize + ImageSize;

    Transfer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BufferSize);
    if (Transfer == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return NULL;
    }

    Transfer->ContextId = Context->ContextId;
    Transfer->ResourceId = Context->VirglColorResourceId;
    Transfer->Width = (ULONG)Width;
    Transfer->Height = (ULONG)Height;
    Transfer->Depth = 1;
    Transfer->Stride = Stride;
    Transfer->LayerStride = ImageSize;
    Transfer->Size = ImageSize;

    VirtGpuOglWaitForLastVirglFence(Context);
    if (!VirtGpuOglEscapeIoControl(Context->hdc,
                                   IOCTL_VIDEO_VIRTGPU_3D_TRANSFER_FROM_HOST,
                                   Transfer,
                                   HeaderSize,
                                   Transfer,
                                   BufferSize,
                                   &Returned) ||
        (Returned < BufferSize))
    {
        HeapFree(GetProcessHeap(), 0, Transfer);
        return NULL;
    }

    return Transfer;
}

static BOOL
VirtGpuOglPresentVirglColorTarget(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    BITMAPINFO BitmapInfo;
    PVIRTGPU_3D_TRANSFER Transfer;
    LONG Width;
    LONG Height;
    INT Lines;

    Transfer = VirtGpuOglReadVirglColorTarget(Context);
    if (Transfer == NULL)
        return FALSE;

    Width = (LONG)Transfer->Width;
    Height = (LONG)Transfer->Height;

    ZeroMemory(&BitmapInfo, sizeof(BitmapInfo));
    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = Width;
    BitmapInfo.bmiHeader.biHeight = -Height;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;

    Lines = SetDIBitsToDevice(Context->hdc,
                              0,
                              0,
                              (DWORD)Width,
                              (DWORD)Height,
                              0,
                              0,
                              0,
                              (UINT)Height,
                              Transfer->Data,
                              &BitmapInfo,
                              DIB_RGB_COLORS);
    HeapFree(GetProcessHeap(), 0, Transfer);
    return Lines == Height;
}

static VOID
VirtGpuOglUpdateDrawableSize(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ BOOL ResetViewport)
{
    RECT ClipRect;
    INT Region;
    GLint Width = 0;
    GLint Height = 0;

    if ((Context == NULL) || (Context->hdc == NULL))
        return;

    Region = GetClipBox(Context->hdc, &ClipRect);
    if ((Region != ERROR) && (Region != NULLREGION))
    {
        Width = ClipRect.right - ClipRect.left;
        Height = ClipRect.bottom - ClipRect.top;
    }

    if ((Width <= 0) || (Height <= 0))
    {
        Width = GetDeviceCaps(Context->hdc, HORZRES);
        Height = GetDeviceCaps(Context->hdc, VERTRES);
    }

    if (Width <= 0)
        Width = 640;
    if (Height <= 0)
        Height = 480;

    Context->DrawableWidth = Width;
    Context->DrawableHeight = Height;

    if (ResetViewport)
    {
        Context->Viewport[0] = 0;
        Context->Viewport[1] = 0;
        Context->Viewport[2] = Width;
        Context->Viewport[3] = Height;
        Context->ScissorBox[0] = 0;
        Context->ScissorBox[1] = 0;
        Context->ScissorBox[2] = Width;
        Context->ScissorBox[3] = Height;
    }
}

static VOID
VirtGpuOglInitializeContextState(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    Context->EnableBits = VIRTGPU_OGL_CAP_DITHER;
    Context->ClearColor[0] = 0.0f;
    Context->ClearColor[1] = 0.0f;
    Context->ClearColor[2] = 0.0f;
    Context->ClearColor[3] = 0.0f;
    Context->ClearDepth = 1.0;
    Context->ClearStencil = 0;
    Context->ColorMask[0] = GL_TRUE;
    Context->ColorMask[1] = GL_TRUE;
    Context->ColorMask[2] = GL_TRUE;
    Context->ColorMask[3] = GL_TRUE;
    Context->DepthMask = GL_TRUE;
    Context->StencilMask = 0xFFFFFFFF;
    Context->MatrixMode = GL_MODELVIEW;
    Context->ModelViewStackTop = 0;
    Context->ProjectionStackTop = 0;
    Context->TextureStackTop = 0;
    VirtGpuOglMatrixIdentity(Context->ModelViewStack[0]);
    VirtGpuOglMatrixIdentity(Context->ProjectionStack[0]);
    VirtGpuOglMatrixIdentity(Context->TextureStack[0]);
    Context->DepthRange[0] = 0.0;
    Context->DepthRange[1] = 1.0;
    Context->DrawBuffer = GL_FRONT;
    Context->ReadBuffer = GL_FRONT;
    Context->DepthFunc = GL_LESS;
    Context->AlphaFunc = GL_ALWAYS;
    Context->AlphaRef = 0.0f;
    Context->BlendSrcFactor = GL_ONE;
    Context->BlendDstFactor = GL_ZERO;
    Context->CullFaceMode = GL_BACK;
    Context->FrontFace = GL_CCW;
    Context->PolygonMode[0] = GL_FILL;
    Context->PolygonMode[1] = GL_FILL;
    Context->ShadeModel = GL_SMOOTH;
    Context->PointSize = 1.0f;
    Context->LineWidth = 1.0f;
    Context->StencilFunc = GL_ALWAYS;
    Context->StencilRef = 0;
    Context->StencilValueMask = 0xFFFFFFFF;
    Context->StencilFail = GL_KEEP;
    Context->StencilDepthFail = GL_KEEP;
    Context->StencilDepthPass = GL_KEEP;
    Context->PackAlignment = 4;
    Context->UnpackAlignment = 4;
    Context->NextTextureName = 1;
    Context->NextListName = 1;
    Context->ListBase = 0;
    Context->BoundTexture1D = 0;
    Context->BoundTexture2D = 0;
    Context->ClientArrayBits = 0;
    Context->VertexArraySize = 4;
    Context->VertexArrayType = GL_FLOAT;
    Context->VertexArrayStride = 0;
    Context->VertexArrayPointer = NULL;
    Context->ColorArraySize = 4;
    Context->ColorArrayType = GL_FLOAT;
    Context->ColorArrayStride = 0;
    Context->ColorArrayPointer = NULL;
    Context->NormalArrayType = GL_FLOAT;
    Context->NormalArrayStride = 0;
    Context->NormalArrayPointer = NULL;
    Context->TexCoordArraySize = 4;
    Context->TexCoordArrayType = GL_FLOAT;
    Context->TexCoordArrayStride = 0;
    Context->TexCoordArrayPointer = NULL;
    Context->CurrentColor = RGB(255, 255, 255);
    Context->CurrentNormal[0] = 0.0f;
    Context->CurrentNormal[1] = 0.0f;
    Context->CurrentNormal[2] = 1.0f;
    Context->CurrentTexCoord[0] = 0.0f;
    Context->CurrentTexCoord[1] = 0.0f;
    Context->CurrentTexCoord[2] = 0.0f;
    Context->CurrentTexCoord[3] = 1.0f;
    Context->BeginMode = 0;
    Context->VertexCount = 0;
    VirtGpuOglUpdateDrawableSize(Context, TRUE);
}

static BOOL
VirtGpuOglCapToBit(_In_ GLenum Cap, _Out_ GLbitfield *Bit)
{
    if ((Cap >= GL_CLIP_PLANE0) && (Cap <= GL_CLIP_PLANE5))
    {
        *Bit = VIRTGPU_OGL_CAP_CLIP_PLANE0 << (Cap - GL_CLIP_PLANE0);
        return TRUE;
    }

    switch (Cap)
    {
        case GL_ALPHA_TEST:
            *Bit = VIRTGPU_OGL_CAP_ALPHA_TEST;
            return TRUE;
        case GL_BLEND:
            *Bit = VIRTGPU_OGL_CAP_BLEND;
            return TRUE;
        case GL_COLOR_MATERIAL:
            *Bit = VIRTGPU_OGL_CAP_COLOR_MATERIAL;
            return TRUE;
        case GL_CULL_FACE:
            *Bit = VIRTGPU_OGL_CAP_CULL_FACE;
            return TRUE;
        case GL_DEPTH_TEST:
            *Bit = VIRTGPU_OGL_CAP_DEPTH_TEST;
            return TRUE;
        case GL_DITHER:
            *Bit = VIRTGPU_OGL_CAP_DITHER;
            return TRUE;
        case GL_FOG:
            *Bit = VIRTGPU_OGL_CAP_FOG;
            return TRUE;
        case GL_LIGHTING:
            *Bit = VIRTGPU_OGL_CAP_LIGHTING;
            return TRUE;
        case GL_LINE_SMOOTH:
            *Bit = VIRTGPU_OGL_CAP_LINE_SMOOTH;
            return TRUE;
        case GL_LINE_STIPPLE:
            *Bit = VIRTGPU_OGL_CAP_LINE_STIPPLE;
            return TRUE;
        case GL_LOGIC_OP:
        case GL_COLOR_LOGIC_OP:
            *Bit = VIRTGPU_OGL_CAP_LOGIC_OP;
            return TRUE;
        case GL_NORMALIZE:
            *Bit = VIRTGPU_OGL_CAP_NORMALIZE;
            return TRUE;
        case GL_POINT_SMOOTH:
            *Bit = VIRTGPU_OGL_CAP_POINT_SMOOTH;
            return TRUE;
        case GL_POLYGON_SMOOTH:
            *Bit = VIRTGPU_OGL_CAP_POLYGON_SMOOTH;
            return TRUE;
        case GL_POLYGON_STIPPLE:
            *Bit = VIRTGPU_OGL_CAP_POLYGON_STIPPLE;
            return TRUE;
        case GL_SCISSOR_TEST:
            *Bit = VIRTGPU_OGL_CAP_SCISSOR_TEST;
            return TRUE;
        case GL_STENCIL_TEST:
            *Bit = VIRTGPU_OGL_CAP_STENCIL_TEST;
            return TRUE;
        case GL_TEXTURE_1D:
            *Bit = VIRTGPU_OGL_CAP_TEXTURE_1D;
            return TRUE;
        case GL_TEXTURE_2D:
            *Bit = VIRTGPU_OGL_CAP_TEXTURE_2D;
            return TRUE;
        default:
            return FALSE;
    }
}

static GLboolean
VirtGpuOglIsEnabledInContext(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLenum Cap)
{
    GLbitfield Bit;

    if ((Context == NULL) || !VirtGpuOglCapToBit(Cap, &Bit))
        return GL_FALSE;

    return (Context->EnableBits & Bit) ? GL_TRUE : GL_FALSE;
}

static ULONG
VirtGpuOglGetValueCount(_In_ GLenum Pname)
{
    switch (Pname)
    {
        case GL_VIEWPORT:
        case GL_SCISSOR_BOX:
        case GL_COLOR_WRITEMASK:
        case GL_CURRENT_COLOR:
        case GL_COLOR_CLEAR_VALUE:
        case GL_CURRENT_TEXTURE_COORDS:
            return 4;
        case GL_CURRENT_NORMAL:
            return 3;
        case GL_MAX_VIEWPORT_DIMS:
        case GL_DEPTH_RANGE:
        case GL_POLYGON_MODE:
            return 2;
        case GL_MODELVIEW_MATRIX:
        case GL_PROJECTION_MATRIX:
        case GL_TEXTURE_MATRIX:
            return 16;
        default:
            return 1;
    }
}

static VOID
VirtGpuOglApplyScissor(_In_ PVIRTGPU_OGL_CONTEXT Context)
{
    GLint Left;
    GLint Top;
    GLint Right;
    GLint Bottom;

    if (VirtGpuOglIsEnabledInContext(Context, GL_SCISSOR_TEST) != GL_TRUE)
        return;

    Left = Context->ScissorBox[0];
    Top = Context->DrawableHeight -
          (Context->ScissorBox[1] + Context->ScissorBox[3]);
    Right = Left + Context->ScissorBox[2];
    Bottom = Top + Context->ScissorBox[3];

    IntersectClipRect(Context->hdc, Left, Top, Right, Bottom);
}

static VOID
VirtGpuOglClearColorBuffer(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    HBRUSH Brush;
    HGDIOBJ OldBrush;
    COLORREF Color;
    RECT ClearRect;
    GLint Left;
    GLint Top;
    GLint Right;
    GLint Bottom;
    INT X;
    INT Y;
    BYTE Red;
    BYTE Green;
    BYTE Blue;

    VirtGpuOglUpdateDrawableSize(Context, FALSE);

    if ((Context->ColorMask[0] == GL_FALSE) &&
        (Context->ColorMask[1] == GL_FALSE) &&
        (Context->ColorMask[2] == GL_FALSE) &&
        (Context->ColorMask[3] == GL_FALSE))
    {
        return;
    }

    ClearRect.left = 0;
    ClearRect.top = 0;
    ClearRect.right = Context->DrawableWidth;
    ClearRect.bottom = Context->DrawableHeight;

    if (VirtGpuOglIsEnabledInContext(Context, GL_SCISSOR_TEST) == GL_TRUE)
    {
        Left = Context->ScissorBox[0];
        Top = Context->DrawableHeight -
              (Context->ScissorBox[1] + Context->ScissorBox[3]);
        Right = Left + Context->ScissorBox[2];
        Bottom = Top + Context->ScissorBox[3];

        if (Left > ClearRect.left)
            ClearRect.left = Left;
        if (Top > ClearRect.top)
            ClearRect.top = Top;
        if (Right < ClearRect.right)
            ClearRect.right = Right;
        if (Bottom < ClearRect.bottom)
            ClearRect.bottom = Bottom;
    }

    if ((ClearRect.left >= ClearRect.right) ||
        (ClearRect.top >= ClearRect.bottom))
    {
        return;
    }

    Color = VirtGpuOglColorFromFloat(Context->ClearColor[0],
                                     Context->ClearColor[1],
                                     Context->ClearColor[2]);

    if ((Context->ColorMask[0] != GL_TRUE) ||
        (Context->ColorMask[1] != GL_TRUE) ||
        (Context->ColorMask[2] != GL_TRUE))
    {
        Red = GetRValue(Color);
        Green = GetGValue(Color);
        Blue = GetBValue(Color);

        for (Y = ClearRect.top; Y < ClearRect.bottom; ++Y)
        {
            for (X = ClearRect.left; X < ClearRect.right; ++X)
            {
                COLORREF OldColor = GetPixel(Context->hdc, X, Y);
                SetPixel(Context->hdc,
                         X,
                         Y,
                         RGB(Context->ColorMask[0] == GL_TRUE ? Red : GetRValue(OldColor),
                             Context->ColorMask[1] == GL_TRUE ? Green : GetGValue(OldColor),
                             Context->ColorMask[2] == GL_TRUE ? Blue : GetBValue(OldColor)));
            }
        }
        return;
    }

    Brush = CreateSolidBrush(Color);
    if (Brush == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    OldBrush = SelectObject(Context->hdc, Brush);
    PatBlt(Context->hdc,
           ClearRect.left,
           ClearRect.top,
           ClearRect.right - ClearRect.left,
           ClearRect.bottom - ClearRect.top,
           PATCOPY);
    SelectObject(Context->hdc, OldBrush);

    DeleteObject(Brush);
}

static BOOL
VirtGpuOglBeginModeSupported(_In_ GLenum Mode)
{
    switch (Mode)
    {
        case GL_POINTS:
        case GL_LINES:
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
        case GL_TRIANGLES:
        case GL_TRIANGLE_STRIP:
        case GL_TRIANGLE_FAN:
        case GL_QUADS:
        case GL_QUAD_STRIP:
        case GL_POLYGON:
            return TRUE;
        default:
            return FALSE;
    }
}

static VOID
VirtGpuOglVertexToPoint(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ const VIRTGPU_OGL_VERTEX *Vertex,
    _Out_ POINT *Point)
{
    GLfloat Clip[4];
    GLfloat W;
    GLfloat X;
    GLfloat Y;

    VirtGpuOglTransformVertex(Context, Vertex, Clip);
    W = Clip[3];
    if (W == 0.0f)
        W = 1.0f;

    X = Clip[0] / W;
    Y = Clip[1] / W;

    Point->x = Context->Viewport[0] +
               VirtGpuOglRoundFloat((X * 0.5f + 0.5f) * Context->Viewport[2]);
    Point->y = Context->DrawableHeight -
               (Context->Viewport[1] +
                VirtGpuOglRoundFloat((Y * 0.5f + 0.5f) * Context->Viewport[3]));
}

static VOID
VirtGpuOglDrawLineRange(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ ULONG First,
    _In_ ULONG Count,
    _In_ BOOL Closed)
{
    HPEN Pen;
    HGDIOBJ OldPen;
    POINT Point;
    ULONG Index;

    if (Count < 2)
        return;

    Pen = CreatePen(PS_SOLID, 1, Context->Vertices[First].Color);
    if (Pen == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    OldPen = SelectObject(Context->hdc, Pen);
    VirtGpuOglVertexToPoint(Context, &Context->Vertices[First], &Point);
    MoveToEx(Context->hdc, Point.x, Point.y, NULL);

    for (Index = 1; Index < Count; ++Index)
    {
        VirtGpuOglVertexToPoint(Context, &Context->Vertices[First + Index], &Point);
        LineTo(Context->hdc, Point.x, Point.y);
    }

    if (Closed)
    {
        VirtGpuOglVertexToPoint(Context, &Context->Vertices[First], &Point);
        LineTo(Context->hdc, Point.x, Point.y);
    }

    SelectObject(Context->hdc, OldPen);
    DeleteObject(Pen);
}

static VOID
VirtGpuOglDrawPolygonVertices(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_reads_(Count) const VIRTGPU_OGL_VERTEX *Vertices,
    _In_ ULONG Count)
{
    POINT Points[4];
    HBRUSH Brush;
    HGDIOBJ OldBrush;
    HPEN Pen;
    HGDIOBJ OldPen;
    ULONG Index;

    if ((Count < 3) || (Count > 4))
        return;

    for (Index = 0; Index < Count; ++Index)
        VirtGpuOglVertexToPoint(Context, &Vertices[Index], &Points[Index]);

    Brush = CreateSolidBrush(Vertices[0].Color);
    Pen = CreatePen(PS_SOLID, 1, Vertices[0].Color);
    if ((Brush == NULL) || (Pen == NULL))
    {
        if (Brush != NULL)
            DeleteObject(Brush);
        if (Pen != NULL)
            DeleteObject(Pen);
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    OldBrush = SelectObject(Context->hdc, Brush);
    OldPen = SelectObject(Context->hdc, Pen);
    Polygon(Context->hdc, Points, Count);
    SelectObject(Context->hdc, OldPen);
    SelectObject(Context->hdc, OldBrush);
    DeleteObject(Pen);
    DeleteObject(Brush);
}

static VOID
VirtGpuOglDrawPolygonRange(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ ULONG First,
    _In_ ULONG Count)
{
    VirtGpuOglDrawPolygonVertices(Context, &Context->Vertices[First], Count);
}

static VOID
VirtGpuOglRenderImmediate(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    INT SavedDc;
    ULONG Index;

    VirtGpuOglUpdateDrawableSize(Context, FALSE);
    SavedDc = SaveDC(Context->hdc);
    VirtGpuOglApplyScissor(Context);

    switch (Context->BeginMode)
    {
        case GL_POINTS:
            for (Index = 0; Index < Context->VertexCount; ++Index)
            {
                POINT Point;
                VirtGpuOglVertexToPoint(Context, &Context->Vertices[Index], &Point);
                SetPixel(Context->hdc, Point.x, Point.y, Context->Vertices[Index].Color);
            }
            break;

        case GL_LINES:
            for (Index = 0; Index + 1 < Context->VertexCount; Index += 2)
                VirtGpuOglDrawLineRange(Context, Index, 2, FALSE);
            break;

        case GL_LINE_STRIP:
            VirtGpuOglDrawLineRange(Context, 0, Context->VertexCount, FALSE);
            break;

        case GL_LINE_LOOP:
            VirtGpuOglDrawLineRange(Context, 0, Context->VertexCount, TRUE);
            break;

        case GL_TRIANGLES:
            for (Index = 0; Index + 2 < Context->VertexCount; Index += 3)
                VirtGpuOglDrawPolygonRange(Context, Index, 3);
            break;

        case GL_TRIANGLE_STRIP:
            for (Index = 0; Index + 2 < Context->VertexCount; ++Index)
                VirtGpuOglDrawPolygonRange(Context, Index, 3);
            break;

        case GL_TRIANGLE_FAN:
            for (Index = 1; Index + 1 < Context->VertexCount; ++Index)
            {
                VIRTGPU_OGL_VERTEX Triangle[3];

                Triangle[0] = Context->Vertices[0];
                Triangle[1] = Context->Vertices[Index];
                Triangle[2] = Context->Vertices[Index + 1];
                VirtGpuOglDrawPolygonVertices(Context, Triangle, 3);
            }
            break;

        case GL_QUADS:
            for (Index = 0; Index + 3 < Context->VertexCount; Index += 4)
                VirtGpuOglDrawPolygonRange(Context, Index, 4);
            break;

        case GL_QUAD_STRIP:
            for (Index = 0; Index + 3 < Context->VertexCount; Index += 2)
                VirtGpuOglDrawPolygonRange(Context, Index, 4);
            break;

        case GL_POLYGON:
            if (Context->VertexCount >= 3)
            {
                for (Index = 1; Index + 1 < Context->VertexCount; ++Index)
                {
                    VIRTGPU_OGL_VERTEX Triangle[3];

                    Triangle[0] = Context->Vertices[0];
                    Triangle[1] = Context->Vertices[Index];
                    Triangle[2] = Context->Vertices[Index + 1];
                    VirtGpuOglDrawPolygonVertices(Context, Triangle, 3);
                }
            }
            break;
    }

    if (SavedDc != 0)
        RestoreDC(Context->hdc, SavedDc);
}

static VOID
VirtGpuOglAppendVertex(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLfloat X,
    _In_ GLfloat Y,
    _In_ GLfloat Z,
    _In_ GLfloat W)
{
    PVIRTGPU_OGL_VERTEX Vertex;

    if (Context->BeginMode == 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (Context->VertexCount >= VIRTGPU_OGL_MAX_IMMEDIATE_VERTICES - 3)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    Vertex = &Context->Vertices[Context->VertexCount++];
    Vertex->X = X;
    Vertex->Y = Y;
    Vertex->Z = Z;
    Vertex->W = W;
    Vertex->Color = Context->CurrentColor;
}

static BOOL
VirtGpuOglEmitArrayElement(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLint Index)
{
    GLfloat X;
    GLfloat Y;
    GLfloat Z;
    GLfloat W;
    ULONG OldVertexCount;

    if (((Context->ClientArrayBits & VIRTGPU_OGL_CLIENT_VERTEX_ARRAY) == 0) ||
        (Context->VertexArrayPointer == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return FALSE;
    }

    if ((Context->ClientArrayBits & VIRTGPU_OGL_CLIENT_COLOR_ARRAY) &&
        (Context->ColorArrayPointer != NULL))
    {
        GLfloat Red;
        GLfloat Green;
        GLfloat Blue;

        Red = VirtGpuOglReadArrayColorComponent(Context->ColorArrayPointer,
                                                Context->ColorArraySize,
                                                Context->ColorArrayType,
                                                Context->ColorArrayStride,
                                                Index,
                                                0,
                                                1.0f);
        Green = VirtGpuOglReadArrayColorComponent(Context->ColorArrayPointer,
                                                  Context->ColorArraySize,
                                                  Context->ColorArrayType,
                                                  Context->ColorArrayStride,
                                                  Index,
                                                  1,
                                                  1.0f);
        Blue = VirtGpuOglReadArrayColorComponent(Context->ColorArrayPointer,
                                                 Context->ColorArraySize,
                                                 Context->ColorArrayType,
                                                 Context->ColorArrayStride,
                                                 Index,
                                                 2,
                                                 1.0f);
        VirtGpuOglColor3f(Red, Green, Blue);
    }

    if ((Context->ClientArrayBits & VIRTGPU_OGL_CLIENT_NORMAL_ARRAY) &&
        (Context->NormalArrayPointer != NULL))
    {
        GLfloat Normal[3];

        Normal[0] = VirtGpuOglReadArrayFloat(Context->NormalArrayPointer,
                                             3,
                                             Context->NormalArrayType,
                                             Context->NormalArrayStride,
                                             Index,
                                             0,
                                             0.0f);
        Normal[1] = VirtGpuOglReadArrayFloat(Context->NormalArrayPointer,
                                             3,
                                             Context->NormalArrayType,
                                             Context->NormalArrayStride,
                                             Index,
                                             1,
                                             0.0f);
        Normal[2] = VirtGpuOglReadArrayFloat(Context->NormalArrayPointer,
                                             3,
                                             Context->NormalArrayType,
                                             Context->NormalArrayStride,
                                             Index,
                                             2,
                                             1.0f);
        VirtGpuOglNormal3f(Normal[0], Normal[1], Normal[2]);
    }

    if ((Context->ClientArrayBits & VIRTGPU_OGL_CLIENT_TEXCOORD_ARRAY) &&
        (Context->TexCoordArrayPointer != NULL))
    {
        GLfloat TexCoord[4];

        TexCoord[0] = VirtGpuOglReadArrayFloat(Context->TexCoordArrayPointer,
                                               Context->TexCoordArraySize,
                                               Context->TexCoordArrayType,
                                               Context->TexCoordArrayStride,
                                               Index,
                                               0,
                                               0.0f);
        TexCoord[1] = VirtGpuOglReadArrayFloat(Context->TexCoordArrayPointer,
                                               Context->TexCoordArraySize,
                                               Context->TexCoordArrayType,
                                               Context->TexCoordArrayStride,
                                               Index,
                                               1,
                                               0.0f);
        TexCoord[2] = VirtGpuOglReadArrayFloat(Context->TexCoordArrayPointer,
                                               Context->TexCoordArraySize,
                                               Context->TexCoordArrayType,
                                               Context->TexCoordArrayStride,
                                               Index,
                                               2,
                                               0.0f);
        TexCoord[3] = VirtGpuOglReadArrayFloat(Context->TexCoordArrayPointer,
                                               Context->TexCoordArraySize,
                                               Context->TexCoordArrayType,
                                               Context->TexCoordArrayStride,
                                               Index,
                                               3,
                                               1.0f);
        VirtGpuOglTexCoord4f(TexCoord[0], TexCoord[1], TexCoord[2], TexCoord[3]);
    }

    X = VirtGpuOglReadArrayFloat(Context->VertexArrayPointer,
                                 Context->VertexArraySize,
                                 Context->VertexArrayType,
                                 Context->VertexArrayStride,
                                 Index,
                                 0,
                                 0.0f);
    Y = VirtGpuOglReadArrayFloat(Context->VertexArrayPointer,
                                 Context->VertexArraySize,
                                 Context->VertexArrayType,
                                 Context->VertexArrayStride,
                                 Index,
                                 1,
                                 0.0f);
    Z = VirtGpuOglReadArrayFloat(Context->VertexArrayPointer,
                                 Context->VertexArraySize,
                                 Context->VertexArrayType,
                                 Context->VertexArrayStride,
                                 Index,
                                 2,
                                 0.0f);
    W = VirtGpuOglReadArrayFloat(Context->VertexArrayPointer,
                                 Context->VertexArraySize,
                                 Context->VertexArrayType,
                                 Context->VertexArrayStride,
                                 Index,
                                 3,
                                 1.0f);
    OldVertexCount = Context->VertexCount;
    VirtGpuOglVertex4f(X, Y, Z, W);

    return VirtGpuOglRecordingCompileOnly(Context) ||
           (Context->VertexCount > OldVertexCount);
}

static VOID
VirtGpuOglDrawRect(
    _In_ GLfloat X1,
    _In_ GLfloat Y1,
    _In_ GLfloat X2,
    _In_ GLfloat Y2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Context->BeginMode != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Context->BeginMode = GL_QUADS;
    Context->VertexCount = 0;
    VirtGpuOglAppendVertex(Context, X1, Y1, 0.0f, 1.0f);
    VirtGpuOglAppendVertex(Context, X2, Y1, 0.0f, 1.0f);
    VirtGpuOglAppendVertex(Context, X2, Y2, 0.0f, 1.0f);
    VirtGpuOglAppendVertex(Context, X1, Y2, 0.0f, 1.0f);
    VirtGpuOglRenderImmediate(Context);
    Context->BeginMode = 0;
    Context->VertexCount = 0;
}

static void APIENTRY VirtGpuOglColor3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2);
static void APIENTRY VirtGpuOglColor4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3);
static void APIENTRY VirtGpuOglNormal3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2);
static void APIENTRY VirtGpuOglTexCoord4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3);
static void APIENTRY VirtGpuOglVertex2f(GLfloat Arg0, GLfloat Arg1);
static void APIENTRY VirtGpuOglVertex3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2);
static void APIENTRY VirtGpuOglVertex4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3);
static void APIENTRY VirtGpuOglBegin(GLenum Arg0);
static void APIENTRY VirtGpuOglEnd(VOID);
static void APIENTRY VirtGpuOglCallList(GLuint Arg0);
static void APIENTRY VirtGpuOglCullFace(GLenum Arg0);
static void APIENTRY VirtGpuOglFrontFace(GLenum Arg0);
static void APIENTRY VirtGpuOglLineWidth(GLfloat Arg0);
static void APIENTRY VirtGpuOglPointSize(GLfloat Arg0);
static void APIENTRY VirtGpuOglPolygonMode(GLenum Arg0, GLenum Arg1);
static void APIENTRY VirtGpuOglScissor(GLint Arg0, GLint Arg1, GLsizei Arg2, GLsizei Arg3);
static void APIENTRY VirtGpuOglShadeModel(GLenum Arg0);
static void APIENTRY VirtGpuOglDrawBuffer(GLenum Arg0);
static void APIENTRY VirtGpuOglClear(GLbitfield Arg0);
static void APIENTRY VirtGpuOglClearColor(GLclampf Arg0, GLclampf Arg1, GLclampf Arg2, GLclampf Arg3);
static void APIENTRY VirtGpuOglClearStencil(GLint Arg0);
static void APIENTRY VirtGpuOglClearDepth(GLclampd Arg0);
static void APIENTRY VirtGpuOglStencilMask(GLuint Arg0);
static void APIENTRY VirtGpuOglColorMask(GLboolean Arg0, GLboolean Arg1, GLboolean Arg2, GLboolean Arg3);
static void APIENTRY VirtGpuOglDepthMask(GLboolean Arg0);
static void APIENTRY VirtGpuOglDisable(GLenum Arg0);
static void APIENTRY VirtGpuOglEnable(GLenum Arg0);
static void APIENTRY VirtGpuOglAlphaFunc(GLenum Arg0, GLclampf Arg1);
static void APIENTRY VirtGpuOglBlendFunc(GLenum Arg0, GLenum Arg1);
static void APIENTRY VirtGpuOglStencilFunc(GLenum Arg0, GLint Arg1, GLuint Arg2);
static void APIENTRY VirtGpuOglStencilOp(GLenum Arg0, GLenum Arg1, GLenum Arg2);
static void APIENTRY VirtGpuOglDepthFunc(GLenum Arg0);
static void APIENTRY VirtGpuOglDepthRange(GLclampd Arg0, GLclampd Arg1);
static void APIENTRY VirtGpuOglFrustum(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4, GLdouble Arg5);
static void APIENTRY VirtGpuOglLoadIdentity(VOID);
static void APIENTRY VirtGpuOglLoadMatrixf(const GLfloat * Arg0);
static void APIENTRY VirtGpuOglLoadMatrixd(const GLdouble * Arg0);
static void APIENTRY VirtGpuOglMatrixMode(GLenum Arg0);
static void APIENTRY VirtGpuOglMultMatrixf(const GLfloat * Arg0);
static void APIENTRY VirtGpuOglMultMatrixd(const GLdouble * Arg0);
static void APIENTRY VirtGpuOglOrtho(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4, GLdouble Arg5);
static void APIENTRY VirtGpuOglPopMatrix(VOID);
static void APIENTRY VirtGpuOglPushMatrix(VOID);
static void APIENTRY VirtGpuOglRotated(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3);
static void APIENTRY VirtGpuOglScaled(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2);
static void APIENTRY VirtGpuOglTranslated(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2);
static void APIENTRY VirtGpuOglViewport(GLint Arg0, GLint Arg1, GLsizei Arg2, GLsizei Arg3);
static void APIENTRY VirtGpuOglBindTexture(GLenum Arg0, GLuint Arg1);
static void APIENTRY VirtGpuOglTexImage1D(GLenum Arg0, GLint Arg1, GLint Arg2, GLsizei Arg3, GLint Arg4, GLenum Arg5, GLenum Arg6, const GLvoid * Arg7);
static void APIENTRY VirtGpuOglTexImage2D(GLenum Arg0, GLint Arg1, GLint Arg2, GLsizei Arg3, GLsizei Arg4, GLint Arg5, GLenum Arg6, GLenum Arg7, const GLvoid * Arg8);
static void APIENTRY VirtGpuOglTexSubImage1D(GLenum Arg0, GLint Arg1, GLint Arg2, GLsizei Arg3, GLenum Arg4, GLenum Arg5, const GLvoid * Arg6);
static void APIENTRY VirtGpuOglTexSubImage2D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4, GLsizei Arg5, GLenum Arg6, GLenum Arg7, const GLvoid * Arg8);

static BOOL
VirtGpuOglShouldRecordList(_In_opt_ PVIRTGPU_OGL_CONTEXT Context)
{
    return (Context != NULL) &&
           (Context->RecordingList != NULL) &&
           (Context->ListExecuteDepth == 0);
}

static BOOL
VirtGpuOglRecordingCompileOnly(_In_opt_ PVIRTGPU_OGL_CONTEXT Context)
{
    return VirtGpuOglShouldRecordList(Context) &&
           (Context->RecordingMode == GL_COMPILE);
}

static VOID
VirtGpuOglClearDisplayListCommands(_Inout_ PVIRTGPU_OGL_DISPLAY_LIST List)
{
    if (List->Commands != NULL)
    {
        ULONG Index;

        for (Index = 0; Index < List->Count; ++Index)
        {
            if (List->Commands[Index].Data != NULL)
            {
                HeapFree(GetProcessHeap(), 0, List->Commands[Index].Data);
                List->Commands[Index].Data = NULL;
            }
        }

        HeapFree(GetProcessHeap(), 0, List->Commands);
        List->Commands = NULL;
    }

    List->Count = 0;
    List->Capacity = 0;
}

static VOID
VirtGpuOglFreeDisplayList(_Inout_ PVIRTGPU_OGL_DISPLAY_LIST List)
{
    VirtGpuOglClearDisplayListCommands(List);
    ZeroMemory(List, sizeof(*List));
}

static PVIRTGPU_OGL_DISPLAY_LIST
VirtGpuOglFindDisplayList(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLuint Name)
{
    ULONG Index;

    if (Name == 0)
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_DISPLAY_LISTS; ++Index)
    {
        if (Context->DisplayLists[Index].Allocated &&
            (Context->DisplayLists[Index].Name == Name))
        {
            return &Context->DisplayLists[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_DISPLAY_LIST
VirtGpuOglAllocateDisplayListName(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLuint Name)
{
    ULONG Index;

    if (Name == 0)
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_DISPLAY_LISTS; ++Index)
    {
        if (!Context->DisplayLists[Index].Allocated)
        {
            Context->DisplayLists[Index].Allocated = TRUE;
            Context->DisplayLists[Index].Name = Name;
            return &Context->DisplayLists[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static BOOL
VirtGpuOglDisplayListRangeFree(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLuint First,
    _In_ GLsizei Count)
{
    GLsizei Index;

    for (Index = 0; Index < Count; ++Index)
    {
        if (VirtGpuOglFindDisplayList(Context, First + (GLuint)Index) != NULL)
            return FALSE;
    }

    return TRUE;
}

static PVIRTGPU_OGL_LIST_COMMAND
VirtGpuOglRecordListCommand(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ VIRTGPU_OGL_LIST_OPCODE Op)
{
    PVIRTGPU_OGL_DISPLAY_LIST List;
    PVIRTGPU_OGL_LIST_COMMAND Commands;
    PVIRTGPU_OGL_LIST_COMMAND Command;
    ULONG NewCapacity;
    SIZE_T NewSize;

    if (!VirtGpuOglShouldRecordList(Context))
        return NULL;

    List = Context->RecordingList;
    if (List->Count >= List->Capacity)
    {
        if (List->Capacity >= VIRTGPU_OGL_MAX_LIST_COMMANDS)
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            return NULL;
        }

        NewCapacity = (List->Capacity != 0) ?
                      (List->Capacity * 2) :
                      VIRTGPU_OGL_INITIAL_LIST_COMMANDS;
        if (NewCapacity > VIRTGPU_OGL_MAX_LIST_COMMANDS)
            NewCapacity = VIRTGPU_OGL_MAX_LIST_COMMANDS;

        NewSize = NewCapacity * sizeof(*List->Commands);
        if (List->Commands != NULL)
        {
            Commands = HeapReAlloc(GetProcessHeap(),
                                   HEAP_ZERO_MEMORY,
                                   List->Commands,
                                   NewSize);
        }
        else
        {
            Commands = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NewSize);
        }

        if (Commands == NULL)
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            return NULL;
        }

        List->Commands = Commands;
        List->Capacity = NewCapacity;
    }

    Command = &List->Commands[List->Count++];
    ZeroMemory(Command, sizeof(*Command));
    Command->Op = Op;
    return Command;
}

static VOID
VirtGpuOglExecuteDisplayList(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ PVIRTGPU_OGL_DISPLAY_LIST List)
{
    ULONG Index;

    if ((List == NULL) || !List->Allocated)
        return;

    if (Context->ListExecuteDepth >= VIRTGPU_OGL_MAX_LIST_RECURSION)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    ++Context->ListExecuteDepth;
    for (Index = 0; Index < List->Count; ++Index)
    {
        PVIRTGPU_OGL_LIST_COMMAND Command = &List->Commands[Index];

        switch (Command->Op)
        {
            case VIRTGPU_OGL_LIST_BEGIN:
                VirtGpuOglBegin(Command->EnumArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_END:
                VirtGpuOglEnd();
                break;
            case VIRTGPU_OGL_LIST_CALL_LIST:
                VirtGpuOglCallList(Command->UintArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_COLOR4F:
                VirtGpuOglColor4f(Command->FloatArgs[0],
                                  Command->FloatArgs[1],
                                  Command->FloatArgs[2],
                                  Command->FloatArgs[3]);
                break;
            case VIRTGPU_OGL_LIST_NORMAL3F:
                VirtGpuOglNormal3f(Command->FloatArgs[0],
                                   Command->FloatArgs[1],
                                   Command->FloatArgs[2]);
                break;
            case VIRTGPU_OGL_LIST_TEXCOORD4F:
                VirtGpuOglTexCoord4f(Command->FloatArgs[0],
                                     Command->FloatArgs[1],
                                     Command->FloatArgs[2],
                                     Command->FloatArgs[3]);
                break;
            case VIRTGPU_OGL_LIST_VERTEX4F:
                VirtGpuOglVertex4f(Command->FloatArgs[0],
                                   Command->FloatArgs[1],
                                   Command->FloatArgs[2],
                                   Command->FloatArgs[3]);
                break;
            case VIRTGPU_OGL_LIST_MATRIX_MODE:
                VirtGpuOglMatrixMode(Command->EnumArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_LOAD_IDENTITY:
                VirtGpuOglLoadIdentity();
                break;
            case VIRTGPU_OGL_LIST_LOAD_MATRIXF:
                VirtGpuOglLoadMatrixf(Command->FloatArgs);
                break;
            case VIRTGPU_OGL_LIST_MULT_MATRIXF:
                VirtGpuOglMultMatrixf(Command->FloatArgs);
                break;
            case VIRTGPU_OGL_LIST_PUSH_MATRIX:
                VirtGpuOglPushMatrix();
                break;
            case VIRTGPU_OGL_LIST_POP_MATRIX:
                VirtGpuOglPopMatrix();
                break;
            case VIRTGPU_OGL_LIST_TRANSLATED:
                VirtGpuOglTranslated(Command->DoubleArgs[0],
                                     Command->DoubleArgs[1],
                                     Command->DoubleArgs[2]);
                break;
            case VIRTGPU_OGL_LIST_ROTATED:
                VirtGpuOglRotated(Command->DoubleArgs[0],
                                  Command->DoubleArgs[1],
                                  Command->DoubleArgs[2],
                                  Command->DoubleArgs[3]);
                break;
            case VIRTGPU_OGL_LIST_SCALED:
                VirtGpuOglScaled(Command->DoubleArgs[0],
                                 Command->DoubleArgs[1],
                                 Command->DoubleArgs[2]);
                break;
            case VIRTGPU_OGL_LIST_ORTHO:
                VirtGpuOglOrtho(Command->DoubleArgs[0],
                                Command->DoubleArgs[1],
                                Command->DoubleArgs[2],
                                Command->DoubleArgs[3],
                                Command->DoubleArgs[4],
                                Command->DoubleArgs[5]);
                break;
            case VIRTGPU_OGL_LIST_FRUSTUM:
                VirtGpuOglFrustum(Command->DoubleArgs[0],
                                  Command->DoubleArgs[1],
                                  Command->DoubleArgs[2],
                                  Command->DoubleArgs[3],
                                  Command->DoubleArgs[4],
                                  Command->DoubleArgs[5]);
                break;
            case VIRTGPU_OGL_LIST_VIEWPORT:
                VirtGpuOglViewport(Command->IntArgs[0],
                                   Command->IntArgs[1],
                                   Command->IntArgs[2],
                                   Command->IntArgs[3]);
                break;
            case VIRTGPU_OGL_LIST_SCISSOR:
                VirtGpuOglScissor(Command->IntArgs[0],
                                  Command->IntArgs[1],
                                  Command->IntArgs[2],
                                  Command->IntArgs[3]);
                break;
            case VIRTGPU_OGL_LIST_ENABLE:
                VirtGpuOglEnable(Command->EnumArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_DISABLE:
                VirtGpuOglDisable(Command->EnumArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_CLEAR_COLOR:
                VirtGpuOglClearColor(Command->FloatArgs[0],
                                     Command->FloatArgs[1],
                                     Command->FloatArgs[2],
                                     Command->FloatArgs[3]);
                break;
            case VIRTGPU_OGL_LIST_CLEAR_DEPTH:
                VirtGpuOglClearDepth(Command->DoubleArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_CLEAR_STENCIL:
                VirtGpuOglClearStencil(Command->IntArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_CLEAR:
                VirtGpuOglClear(Command->BitArgs);
                break;
            case VIRTGPU_OGL_LIST_DRAW_BUFFER:
                VirtGpuOglDrawBuffer(Command->EnumArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_COLOR_MASK:
                VirtGpuOglColorMask((GLboolean)Command->IntArgs[0],
                                    (GLboolean)Command->IntArgs[1],
                                    (GLboolean)Command->IntArgs[2],
                                    (GLboolean)Command->IntArgs[3]);
                break;
            case VIRTGPU_OGL_LIST_DEPTH_MASK:
                VirtGpuOglDepthMask((GLboolean)Command->IntArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_STENCIL_MASK:
                VirtGpuOglStencilMask(Command->UintArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_DEPTH_RANGE:
                VirtGpuOglDepthRange(Command->DoubleArgs[0],
                                     Command->DoubleArgs[1]);
                break;
            case VIRTGPU_OGL_LIST_CULL_FACE:
                VirtGpuOglCullFace(Command->EnumArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_FRONT_FACE:
                VirtGpuOglFrontFace(Command->EnumArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_LINE_WIDTH:
                VirtGpuOglLineWidth(Command->FloatArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_POINT_SIZE:
                VirtGpuOglPointSize(Command->FloatArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_POLYGON_MODE:
                VirtGpuOglPolygonMode(Command->EnumArgs[0],
                                      Command->EnumArgs[1]);
                break;
            case VIRTGPU_OGL_LIST_SHADE_MODEL:
                VirtGpuOglShadeModel(Command->EnumArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_ALPHA_FUNC:
                VirtGpuOglAlphaFunc(Command->EnumArgs[0],
                                    Command->FloatArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_BLEND_FUNC:
                VirtGpuOglBlendFunc(Command->EnumArgs[0],
                                    Command->EnumArgs[1]);
                break;
            case VIRTGPU_OGL_LIST_STENCIL_FUNC:
                VirtGpuOglStencilFunc(Command->EnumArgs[0],
                                      Command->IntArgs[0],
                                      Command->UintArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_STENCIL_OP:
                VirtGpuOglStencilOp(Command->EnumArgs[0],
                                    Command->EnumArgs[1],
                                    Command->EnumArgs[2]);
                break;
            case VIRTGPU_OGL_LIST_DEPTH_FUNC:
                VirtGpuOglDepthFunc(Command->EnumArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_BIND_TEXTURE:
                VirtGpuOglBindTexture(Command->EnumArgs[0],
                                      Command->UintArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_TEX_PARAMETERI:
                VirtGpuOglTexParameteri(Command->EnumArgs[0],
                                        Command->EnumArgs[1],
                                        Command->IntArgs[0]);
                break;
            case VIRTGPU_OGL_LIST_TEX_IMAGE_1D:
            {
                GLint SavedUnpackAlignment = Context->UnpackAlignment;

                Context->UnpackAlignment = 1;
                VirtGpuOglTexImage1D(Command->EnumArgs[0],
                                     Command->IntArgs[0],
                                     Command->IntArgs[1],
                                     Command->IntArgs[2],
                                     Command->IntArgs[3],
                                     Command->EnumArgs[1],
                                     Command->EnumArgs[2],
                                     Command->Data);
                Context->UnpackAlignment = SavedUnpackAlignment;
                break;
            }
            case VIRTGPU_OGL_LIST_TEX_IMAGE_2D:
            {
                GLint SavedUnpackAlignment = Context->UnpackAlignment;

                Context->UnpackAlignment = 1;
                VirtGpuOglTexImage2D(Command->EnumArgs[0],
                                     Command->IntArgs[0],
                                     Command->IntArgs[1],
                                     Command->IntArgs[2],
                                     Command->IntArgs[3],
                                     Command->IntArgs[4],
                                     Command->EnumArgs[1],
                                     Command->EnumArgs[2],
                                     Command->Data);
                Context->UnpackAlignment = SavedUnpackAlignment;
                break;
            }
            case VIRTGPU_OGL_LIST_TEX_SUB_IMAGE_1D:
            {
                GLint SavedUnpackAlignment = Context->UnpackAlignment;

                Context->UnpackAlignment = 1;
                VirtGpuOglTexSubImage1D(Command->EnumArgs[0],
                                        Command->IntArgs[0],
                                        Command->IntArgs[1],
                                        Command->IntArgs[2],
                                        Command->EnumArgs[1],
                                        Command->EnumArgs[2],
                                        Command->Data);
                Context->UnpackAlignment = SavedUnpackAlignment;
                break;
            }
            case VIRTGPU_OGL_LIST_TEX_SUB_IMAGE_2D:
            {
                GLint SavedUnpackAlignment = Context->UnpackAlignment;

                Context->UnpackAlignment = 1;
                VirtGpuOglTexSubImage2D(Command->EnumArgs[0],
                                        Command->IntArgs[0],
                                        Command->IntArgs[1],
                                        Command->IntArgs[2],
                                        Command->IntArgs[3],
                                        Command->IntArgs[4],
                                        Command->EnumArgs[1],
                                        Command->EnumArgs[2],
                                        Command->Data);
                Context->UnpackAlignment = SavedUnpackAlignment;
                break;
            }
            default:
                break;
        }
    }
    --Context->ListExecuteDepth;
}

static BOOL
VirtGpuOglCallListValue(
    _In_ GLenum Type,
    _In_reads_bytes_(1) const GLvoid *Data,
    _In_ GLsizei Index,
    _Out_ GLuint *Value)
{
    const GLubyte *Bytes = Data;

    switch (Type)
    {
        case GL_BYTE:
            *Value = (GLuint)((const GLbyte *)Data)[Index];
            return TRUE;
        case GL_UNSIGNED_BYTE:
            *Value = ((const GLubyte *)Data)[Index];
            return TRUE;
        case GL_SHORT:
            *Value = (GLuint)((const GLshort *)Data)[Index];
            return TRUE;
        case GL_UNSIGNED_SHORT:
            *Value = ((const GLushort *)Data)[Index];
            return TRUE;
        case GL_INT:
            *Value = (GLuint)((const GLint *)Data)[Index];
            return TRUE;
        case GL_UNSIGNED_INT:
            *Value = ((const GLuint *)Data)[Index];
            return TRUE;
        case GL_FLOAT:
            *Value = (GLuint)((const GLfloat *)Data)[Index];
            return TRUE;
        case GL_2_BYTES:
            Bytes += Index * 2;
            *Value = ((GLuint)Bytes[0] << 8) | Bytes[1];
            return TRUE;
        case GL_3_BYTES:
            Bytes += Index * 3;
            *Value = ((GLuint)Bytes[0] << 16) |
                     ((GLuint)Bytes[1] << 8) |
                     Bytes[2];
            return TRUE;
        case GL_4_BYTES:
            Bytes += Index * 4;
            *Value = ((GLuint)Bytes[0] << 24) |
                     ((GLuint)Bytes[1] << 16) |
                     ((GLuint)Bytes[2] << 8) |
                     Bytes[3];
            return TRUE;
        default:
            return FALSE;
    }
}

/* Generated OpenGL stubs: ICD 1.1 dispatch plus versioned 1.2 through 4.0 lookup entries. */

static void APIENTRY
VirtGpuOglNewList(GLuint Arg0, GLenum Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_DISPLAY_LIST List;

    if (Context == NULL)
        return;

    if (Arg0 == 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg1 != GL_COMPILE) && (Arg1 != GL_COMPILE_AND_EXECUTE))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Context->BeginMode != 0) ||
        (Context->RecordingList != NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    List = VirtGpuOglFindDisplayList(Context, Arg0);
    if (List == NULL)
        List = VirtGpuOglAllocateDisplayListName(Context, Arg0);
    if (List == NULL)
        return;

    VirtGpuOglClearDisplayListCommands(List);
    Context->RecordingList = List;
    Context->RecordingMode = Arg1;
    Context->RecordingBeginMode = 0;
}

static void APIENTRY
VirtGpuOglEndList(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Context->RecordingList == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if ((Context->RecordingBeginMode != 0) || (Context->BeginMode != 0))
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);

    Context->RecordingList = NULL;
    Context->RecordingMode = 0;
    Context->RecordingBeginMode = 0;
}

static void APIENTRY
VirtGpuOglCallList(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    PVIRTGPU_OGL_DISPLAY_LIST List;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_CALL_LIST);
        if (Command != NULL)
            Command->UintArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    List = VirtGpuOglFindDisplayList(Context, Arg0);
    VirtGpuOglExecuteDisplayList(Context, List);
}

static void APIENTRY
VirtGpuOglCallLists(GLsizei Arg0, GLenum Arg1, const GLvoid * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    GLuint Value;

    if (Context == NULL)
        return;

    if (Arg0 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg0 == 0)
        return;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglCallListValue(Arg1, Arg2, 0, &Value))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        (VOID)VirtGpuOglCallListValue(Arg1, Arg2, Index, &Value);
        VirtGpuOglCallList(Context->ListBase + Value);
    }
}

static void APIENTRY
VirtGpuOglDeleteLists(GLuint Arg0, GLsizei Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    PVIRTGPU_OGL_DISPLAY_LIST List;

    if (Context == NULL)
        return;

    if (Arg1 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg1; ++Index)
    {
        List = VirtGpuOglFindDisplayList(Context, Arg0 + (GLuint)Index);
        if (List != NULL)
            VirtGpuOglFreeDisplayList(List);
    }
}

static GLuint APIENTRY
VirtGpuOglGenLists(GLsizei Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLuint Base;
    GLsizei Index;

    if (Context == NULL)
        return 0;

    if (Arg0 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return 0;
    }

    if (Arg0 == 0)
        return 0;

    for (Base = Context->NextListName; Base != 0; ++Base)
    {
        if ((Base + (GLuint)Arg0) < Base)
            break;

        if (!VirtGpuOglDisplayListRangeFree(Context, Base, Arg0))
            continue;

        for (Index = 0; Index < Arg0; ++Index)
        {
            if (VirtGpuOglAllocateDisplayListName(Context, Base + (GLuint)Index) == NULL)
            {
                VirtGpuOglDeleteLists(Base, Index);
                return 0;
            }
        }

        Context->NextListName = Base + (GLuint)Arg0;
        if (Context->NextListName == 0)
            Context->NextListName = 1;
        return Base;
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return 0;
}

static void APIENTRY
VirtGpuOglListBase(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context != NULL)
        Context->ListBase = Arg0;
}

static void APIENTRY
VirtGpuOglBegin(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if ((Context->BeginMode != 0) ||
        (VirtGpuOglShouldRecordList(Context) && (Context->RecordingBeginMode != 0)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (!VirtGpuOglBeginModeSupported(Arg0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_BEGIN);
        if (Command != NULL)
            Command->EnumArgs[0] = Arg0;
        Context->RecordingBeginMode = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->BeginMode = Arg0;
    Context->VertexCount = 0;
}

static void APIENTRY
VirtGpuOglBitmap(GLsizei Arg0, GLsizei Arg1, GLfloat Arg2, GLfloat Arg3, GLfloat Arg4, GLfloat Arg5, const GLubyte * Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglColor3b(GLbyte Arg0, GLbyte Arg1, GLbyte Arg2)
{
    VirtGpuOglColor3f(VirtGpuOglColorFromByte(Arg0),
                      VirtGpuOglColorFromByte(Arg1),
                      VirtGpuOglColorFromByte(Arg2));
}

static void APIENTRY
VirtGpuOglColor3bv(const GLbyte * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor3b(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglColor3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    VirtGpuOglColor3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglColor3dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor3d(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglColor3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_COLOR4F);
        if (Command != NULL)
        {
            Command->FloatArgs[0] = Arg0;
            Command->FloatArgs[1] = Arg1;
            Command->FloatArgs[2] = Arg2;
            Command->FloatArgs[3] = 1.0f;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->CurrentColor = VirtGpuOglColorFromFloat(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglColor3fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor3f(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglColor3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    VirtGpuOglColor3f(VirtGpuOglColorFromInt(Arg0),
                      VirtGpuOglColorFromInt(Arg1),
                      VirtGpuOglColorFromInt(Arg2));
}

static void APIENTRY
VirtGpuOglColor3iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor3i(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglColor3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    VirtGpuOglColor3f(VirtGpuOglColorFromShort(Arg0),
                      VirtGpuOglColorFromShort(Arg1),
                      VirtGpuOglColorFromShort(Arg2));
}

static void APIENTRY
VirtGpuOglColor3sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor3s(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglColor3ub(GLubyte Arg0, GLubyte Arg1, GLubyte Arg2)
{
    VirtGpuOglColor3f(VirtGpuOglColorFromUByte(Arg0),
                      VirtGpuOglColorFromUByte(Arg1),
                      VirtGpuOglColorFromUByte(Arg2));
}

static void APIENTRY
VirtGpuOglColor3ubv(const GLubyte * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor3ub(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglColor3ui(GLuint Arg0, GLuint Arg1, GLuint Arg2)
{
    VirtGpuOglColor3f(VirtGpuOglColorFromUInt(Arg0),
                      VirtGpuOglColorFromUInt(Arg1),
                      VirtGpuOglColorFromUInt(Arg2));
}

static void APIENTRY
VirtGpuOglColor3uiv(const GLuint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor3ui(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglColor3us(GLushort Arg0, GLushort Arg1, GLushort Arg2)
{
    VirtGpuOglColor3f(VirtGpuOglColorFromUShort(Arg0),
                      VirtGpuOglColorFromUShort(Arg1),
                      VirtGpuOglColorFromUShort(Arg2));
}

static void APIENTRY
VirtGpuOglColor3usv(const GLushort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor3us(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglColor4b(GLbyte Arg0, GLbyte Arg1, GLbyte Arg2, GLbyte Arg3)
{
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglColor3b(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglColor4bv(const GLbyte * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor4b(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglColor4d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglColor3d(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglColor4dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor4d(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglColor4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_COLOR4F);
        if (Command != NULL)
        {
            Command->FloatArgs[0] = Arg0;
            Command->FloatArgs[1] = Arg1;
            Command->FloatArgs[2] = Arg2;
            Command->FloatArgs[3] = Arg3;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    UNREFERENCED_PARAMETER(Arg3);
    Context->CurrentColor = VirtGpuOglColorFromFloat(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglColor4fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor4f(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglColor4i(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglColor3i(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglColor4iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor4i(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglColor4s(GLshort Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglColor3s(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglColor4sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor4s(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglColor4ub(GLubyte Arg0, GLubyte Arg1, GLubyte Arg2, GLubyte Arg3)
{
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglColor3ub(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglColor4ubv(const GLubyte * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor4ub(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglColor4ui(GLuint Arg0, GLuint Arg1, GLuint Arg2, GLuint Arg3)
{
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglColor3ui(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglColor4uiv(const GLuint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor4ui(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglColor4us(GLushort Arg0, GLushort Arg1, GLushort Arg2, GLushort Arg3)
{
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglColor3us(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglColor4usv(const GLushort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglColor4us(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglEdgeFlag(GLboolean Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEdgeFlagv(const GLboolean * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEnd(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        if (Context->RecordingBeginMode == 0)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }

        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_END);
        UNREFERENCED_PARAMETER(Command);
        Context->RecordingBeginMode = 0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    if (Context->BeginMode == 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    VirtGpuOglRenderImmediate(Context);
    Context->BeginMode = 0;
    Context->VertexCount = 0;
}

static void APIENTRY
VirtGpuOglIndexd(GLdouble Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglIndexdv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglIndexf(GLfloat Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglIndexfv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglIndexi(GLint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglIndexiv(const GLint * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglIndexs(GLshort Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglIndexsv(const GLshort * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglNormal3b(GLbyte Arg0, GLbyte Arg1, GLbyte Arg2)
{
    VirtGpuOglNormal3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglNormal3bv(const GLbyte * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglNormal3b(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglNormal3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    VirtGpuOglNormal3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglNormal3dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglNormal3d(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglNormal3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_NORMAL3F);
        if (Command != NULL)
        {
            Command->FloatArgs[0] = Arg0;
            Command->FloatArgs[1] = Arg1;
            Command->FloatArgs[2] = Arg2;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->CurrentNormal[0] = Arg0;
    Context->CurrentNormal[1] = Arg1;
    Context->CurrentNormal[2] = Arg2;
}

static void APIENTRY
VirtGpuOglNormal3fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglNormal3f(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglNormal3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    VirtGpuOglNormal3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglNormal3iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglNormal3i(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglNormal3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    VirtGpuOglNormal3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglNormal3sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglNormal3s(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglRasterPos2d(GLdouble Arg0, GLdouble Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos2dv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos2f(GLfloat Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos2fv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos2i(GLint Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos2iv(const GLint * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos2s(GLshort Arg0, GLshort Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos2sv(const GLshort * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos3dv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos3fv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos3iv(const GLint * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos3sv(const GLshort * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos4d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos4dv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos4fv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos4i(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos4iv(const GLint * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos4s(GLshort Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRasterPos4sv(const GLshort * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRectd(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    VirtGpuOglDrawRect((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglRectdv(const GLdouble * Arg0, const GLdouble * Arg1)
{
    if ((Arg0 != NULL) && (Arg1 != NULL))
        VirtGpuOglRectd(Arg0[0], Arg0[1], Arg1[0], Arg1[1]);
}

static void APIENTRY
VirtGpuOglRectf(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    VirtGpuOglDrawRect(Arg0, Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglRectfv(const GLfloat * Arg0, const GLfloat * Arg1)
{
    if ((Arg0 != NULL) && (Arg1 != NULL))
        VirtGpuOglRectf(Arg0[0], Arg0[1], Arg1[0], Arg1[1]);
}

static void APIENTRY
VirtGpuOglRecti(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    VirtGpuOglDrawRect((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglRectiv(const GLint * Arg0, const GLint * Arg1)
{
    if ((Arg0 != NULL) && (Arg1 != NULL))
        VirtGpuOglRecti(Arg0[0], Arg0[1], Arg1[0], Arg1[1]);
}

static void APIENTRY
VirtGpuOglRects(GLshort Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    VirtGpuOglDrawRect((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglRectsv(const GLshort * Arg0, const GLshort * Arg1)
{
    if ((Arg0 != NULL) && (Arg1 != NULL))
        VirtGpuOglRects(Arg0[0], Arg0[1], Arg1[0], Arg1[1]);
}

static void APIENTRY
VirtGpuOglTexCoord1d(GLdouble Arg0)
{
    VirtGpuOglTexCoord4f((GLfloat)Arg0, 0.0f, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglTexCoord1dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord1d(Arg0[0]);
}

static void APIENTRY
VirtGpuOglTexCoord1f(GLfloat Arg0)
{
    VirtGpuOglTexCoord4f(Arg0, 0.0f, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglTexCoord1fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord1f(Arg0[0]);
}

static void APIENTRY
VirtGpuOglTexCoord1i(GLint Arg0)
{
    VirtGpuOglTexCoord1f((GLfloat)Arg0);
}

static void APIENTRY
VirtGpuOglTexCoord1iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord1i(Arg0[0]);
}

static void APIENTRY
VirtGpuOglTexCoord1s(GLshort Arg0)
{
    VirtGpuOglTexCoord1f((GLfloat)Arg0);
}

static void APIENTRY
VirtGpuOglTexCoord1sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord1s(Arg0[0]);
}

static void APIENTRY
VirtGpuOglTexCoord2d(GLdouble Arg0, GLdouble Arg1)
{
    VirtGpuOglTexCoord4f((GLfloat)Arg0, (GLfloat)Arg1, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglTexCoord2dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord2d(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglTexCoord2f(GLfloat Arg0, GLfloat Arg1)
{
    VirtGpuOglTexCoord4f(Arg0, Arg1, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglTexCoord2fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord2f(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglTexCoord2i(GLint Arg0, GLint Arg1)
{
    VirtGpuOglTexCoord2f((GLfloat)Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglTexCoord2iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord2i(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglTexCoord2s(GLshort Arg0, GLshort Arg1)
{
    VirtGpuOglTexCoord2f((GLfloat)Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglTexCoord2sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord2s(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglTexCoord3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    VirtGpuOglTexCoord4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, 1.0f);
}

static void APIENTRY
VirtGpuOglTexCoord3dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord3d(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglTexCoord3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    VirtGpuOglTexCoord4f(Arg0, Arg1, Arg2, 1.0f);
}

static void APIENTRY
VirtGpuOglTexCoord3fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord3f(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglTexCoord3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    VirtGpuOglTexCoord3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglTexCoord3iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord3i(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglTexCoord3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    VirtGpuOglTexCoord3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglTexCoord3sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord3s(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglTexCoord4d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    VirtGpuOglTexCoord4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglTexCoord4dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord4d(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglTexCoord4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_TEXCOORD4F);
        if (Command != NULL)
        {
            Command->FloatArgs[0] = Arg0;
            Command->FloatArgs[1] = Arg1;
            Command->FloatArgs[2] = Arg2;
            Command->FloatArgs[3] = Arg3;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->CurrentTexCoord[0] = Arg0;
    Context->CurrentTexCoord[1] = Arg1;
    Context->CurrentTexCoord[2] = Arg2;
    Context->CurrentTexCoord[3] = Arg3;
}

static void APIENTRY
VirtGpuOglTexCoord4fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord4f(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglTexCoord4i(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    VirtGpuOglTexCoord4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglTexCoord4iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord4i(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglTexCoord4s(GLshort Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    VirtGpuOglTexCoord4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglTexCoord4sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglTexCoord4s(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglVertex2d(GLdouble Arg0, GLdouble Arg1)
{
    VirtGpuOglVertex2f((GLfloat)Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglVertex2dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex2d(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglVertex2f(GLfloat Arg0, GLfloat Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        if (Context->RecordingBeginMode == 0)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }

        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_VERTEX4F);
        if (Command != NULL)
        {
            Command->FloatArgs[0] = Arg0;
            Command->FloatArgs[1] = Arg1;
            Command->FloatArgs[2] = 0.0f;
            Command->FloatArgs[3] = 1.0f;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglAppendVertex(Context, Arg0, Arg1, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertex2fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex2f(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglVertex2i(GLint Arg0, GLint Arg1)
{
    VirtGpuOglVertex2f((GLfloat)Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglVertex2iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex2i(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglVertex2s(GLshort Arg0, GLshort Arg1)
{
    VirtGpuOglVertex2f((GLfloat)Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglVertex2sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex2s(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglVertex3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    VirtGpuOglVertex3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglVertex3dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex3d(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglVertex3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        if (Context->RecordingBeginMode == 0)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }

        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_VERTEX4F);
        if (Command != NULL)
        {
            Command->FloatArgs[0] = Arg0;
            Command->FloatArgs[1] = Arg1;
            Command->FloatArgs[2] = Arg2;
            Command->FloatArgs[3] = 1.0f;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglAppendVertex(Context, Arg0, Arg1, Arg2, 1.0f);
}

static void APIENTRY
VirtGpuOglVertex3fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex3f(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglVertex3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    VirtGpuOglVertex3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglVertex3iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex3i(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglVertex3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    VirtGpuOglVertex3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglVertex3sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex3s(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglVertex4d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    VirtGpuOglVertex4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglVertex4dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex4d(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglVertex4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        if (Context->RecordingBeginMode == 0)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }

        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_VERTEX4F);
        if (Command != NULL)
        {
            Command->FloatArgs[0] = Arg0;
            Command->FloatArgs[1] = Arg1;
            Command->FloatArgs[2] = Arg2;
            Command->FloatArgs[3] = Arg3;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglAppendVertex(Context, Arg0, Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglVertex4fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex4f(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglVertex4i(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    VirtGpuOglVertex4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglVertex4iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex4i(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglVertex4s(GLshort Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    VirtGpuOglVertex4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglVertex4sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglVertex4s(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglClipPlane(GLenum Arg0, const GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglColorMaterial(GLenum Arg0, GLenum Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCullFace(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FRONT:
        case GL_BACK:
        case GL_FRONT_AND_BACK:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_CULL_FACE);
        if (Command != NULL)
            Command->EnumArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->CullFaceMode = Arg0;
}

static void APIENTRY
VirtGpuOglFogf(GLenum Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFogfv(GLenum Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFogi(GLenum Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFogiv(GLenum Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFrontFace(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_CW:
        case GL_CCW:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_FRONT_FACE);
        if (Command != NULL)
            Command->EnumArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->FrontFace = Arg0;
}

static void APIENTRY
VirtGpuOglHint(GLenum Arg0, GLenum Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLightf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLightfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLighti(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLightiv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLightModelf(GLenum Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLightModelfv(GLenum Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLightModeli(GLenum Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLightModeliv(GLenum Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLineStipple(GLint Arg0, GLushort Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLineWidth(GLfloat Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (Arg0 <= 0.0f)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_LINE_WIDTH);
        if (Command != NULL)
            Command->FloatArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->LineWidth = Arg0;
}

static void APIENTRY
VirtGpuOglMaterialf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMaterialfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMateriali(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMaterialiv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPointSize(GLfloat Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (Arg0 <= 0.0f)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_POINT_SIZE);
        if (Command != NULL)
            Command->FloatArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->PointSize = Arg0;
}

static void APIENTRY
VirtGpuOglPolygonMode(GLenum Arg0, GLenum Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    switch (Arg1)
    {
        case GL_POINT:
        case GL_LINE:
        case GL_FILL:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    switch (Arg0)
    {
        case GL_FRONT:
            break;
        case GL_BACK:
            break;
        case GL_FRONT_AND_BACK:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_POLYGON_MODE);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->EnumArgs[1] = Arg1;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    if (Arg0 == GL_FRONT)
    {
        Context->PolygonMode[0] = Arg1;
    }
    else if (Arg0 == GL_BACK)
    {
        Context->PolygonMode[1] = Arg1;
    }
    else
    {
        Context->PolygonMode[0] = Arg1;
        Context->PolygonMode[1] = Arg1;
    }
}

static void APIENTRY
VirtGpuOglPolygonStipple(const GLubyte * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglScissor(GLint Arg0, GLint Arg1, GLsizei Arg2, GLsizei Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if ((Arg2 < 0) || (Arg3 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_SCISSOR);
        if (Command != NULL)
        {
            Command->IntArgs[0] = Arg0;
            Command->IntArgs[1] = Arg1;
            Command->IntArgs[2] = Arg2;
            Command->IntArgs[3] = Arg3;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->ScissorBox[0] = Arg0;
    Context->ScissorBox[1] = Arg1;
    Context->ScissorBox[2] = Arg2;
    Context->ScissorBox[3] = Arg3;
}

static void APIENTRY
VirtGpuOglShadeModel(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FLAT:
        case GL_SMOOTH:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_SHADE_MODEL);
        if (Command != NULL)
            Command->EnumArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->ShadeModel = Arg0;
}

static void APIENTRY
VirtGpuOglTexParameterf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    VirtGpuOglTexParameteri(Arg0, Arg1, (GLint)Arg2);
}

static void APIENTRY
VirtGpuOglTexParameterfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglTexParameterf(Arg0, Arg1, Arg2[0]);
}

static void APIENTRY
VirtGpuOglTexParameteri(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    PVIRTGPU_OGL_TEXTURE Texture;

    if (Context == NULL)
        return;

    if ((Arg0 != GL_TEXTURE_1D) && (Arg0 != GL_TEXTURE_2D))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    switch (Arg1)
    {
        case GL_TEXTURE_MIN_FILTER:
        case GL_TEXTURE_MAG_FILTER:
            if (!VirtGpuOglValidTextureFilter(Arg1, Arg2))
            {
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                return;
            }
            break;
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
            if (!VirtGpuOglValidTextureWrap(Arg2))
            {
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                return;
            }
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_TEX_PARAMETERI);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->EnumArgs[1] = Arg1;
            Command->IntArgs[0] = Arg2;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture == NULL)
        return;

    switch (Arg1)
    {
        case GL_TEXTURE_MIN_FILTER:
            Texture->MinFilter = Arg2;
            break;
        case GL_TEXTURE_MAG_FILTER:
            Texture->MagFilter = Arg2;
            break;
        case GL_TEXTURE_WRAP_S:
            Texture->WrapS = Arg2;
            break;
        case GL_TEXTURE_WRAP_T:
            Texture->WrapT = Arg2;
            break;
    }
}

static void APIENTRY
VirtGpuOglTexParameteriv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglTexParameteri(Arg0, Arg1, Arg2[0]);
}

static void APIENTRY
VirtGpuOglTexImage1D(GLenum Arg0, GLint Arg1, GLint Arg2, GLsizei Arg3, GLint Arg4, GLenum Arg5, GLenum Arg6, const GLvoid * Arg7)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    PVIRTGPU_OGL_TEXTURE Texture;
    BYTE *Data = NULL;
    ULONG DataSize = 0;

    if (Context == NULL)
        return;

    if (!VirtGpuOglValidateTextureImageParameters(Context,
                                                  Arg0,
                                                  Arg1,
                                                  Arg2,
                                                  Arg3,
                                                  1,
                                                  Arg4,
                                                  Arg5,
                                                  Arg6))
    {
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        if (!VirtGpuOglCopyTexturePixels(Context,
                                         Arg3,
                                         1,
                                         Arg5,
                                         Arg6,
                                         Arg7,
                                         &Data,
                                         &DataSize))
        {
            return;
        }

        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_TEX_IMAGE_1D);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->EnumArgs[1] = Arg5;
            Command->EnumArgs[2] = Arg6;
            Command->IntArgs[0] = Arg1;
            Command->IntArgs[1] = Arg2;
            Command->IntArgs[2] = Arg3;
            Command->IntArgs[3] = Arg4;
            Command->DataSize = DataSize;
            Command->Data = Data;
            Data = NULL;
        }

        if (Data != NULL)
            HeapFree(GetProcessHeap(), 0, Data);

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture != NULL)
    {
        (VOID)VirtGpuOglStoreTextureImage(Context,
                                          Texture,
                                          Arg0,
                                          Arg1,
                                          Arg2,
                                          Arg3,
                                          1,
                                          Arg4,
                                          Arg5,
                                          Arg6,
                                          Arg7);
    }
}

static void APIENTRY
VirtGpuOglTexImage2D(GLenum Arg0, GLint Arg1, GLint Arg2, GLsizei Arg3, GLsizei Arg4, GLint Arg5, GLenum Arg6, GLenum Arg7, const GLvoid * Arg8)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    PVIRTGPU_OGL_TEXTURE Texture;
    BYTE *Data = NULL;
    ULONG DataSize = 0;

    if (Context == NULL)
        return;

    if (!VirtGpuOglValidateTextureImageParameters(Context,
                                                  Arg0,
                                                  Arg1,
                                                  Arg2,
                                                  Arg3,
                                                  Arg4,
                                                  Arg5,
                                                  Arg6,
                                                  Arg7))
    {
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        if (!VirtGpuOglCopyTexturePixels(Context,
                                         Arg3,
                                         Arg4,
                                         Arg6,
                                         Arg7,
                                         Arg8,
                                         &Data,
                                         &DataSize))
        {
            return;
        }

        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_TEX_IMAGE_2D);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->EnumArgs[1] = Arg6;
            Command->EnumArgs[2] = Arg7;
            Command->IntArgs[0] = Arg1;
            Command->IntArgs[1] = Arg2;
            Command->IntArgs[2] = Arg3;
            Command->IntArgs[3] = Arg4;
            Command->IntArgs[4] = Arg5;
            Command->DataSize = DataSize;
            Command->Data = Data;
            Data = NULL;
        }

        if (Data != NULL)
            HeapFree(GetProcessHeap(), 0, Data);

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture != NULL)
    {
        (VOID)VirtGpuOglStoreTextureImage(Context,
                                          Texture,
                                          Arg0,
                                          Arg1,
                                          Arg2,
                                          Arg3,
                                          Arg4,
                                          Arg5,
                                          Arg6,
                                          Arg7,
                                          Arg8);
    }
}

static void APIENTRY
VirtGpuOglTexEnvf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexEnvfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexEnvi(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexEnviv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexGend(GLenum Arg0, GLenum Arg1, GLdouble Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexGendv(GLenum Arg0, GLenum Arg1, const GLdouble * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexGenf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexGenfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexGeni(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexGeniv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFeedbackBuffer(GLsizei Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSelectBuffer(GLsizei Arg0, GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static GLint APIENTRY
VirtGpuOglRenderMode(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglInitNames(VOID)
{
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLoadName(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPassThrough(GLfloat Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPopName(VOID)
{
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPushName(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawBuffer(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FRONT:
        case GL_FRONT_LEFT:
        case GL_NONE:
            if (VirtGpuOglShouldRecordList(Context))
            {
                Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_DRAW_BUFFER);
                if (Command != NULL)
                    Command->EnumArgs[0] = Arg0;

                if (VirtGpuOglRecordingCompileOnly(Context))
                    return;
            }

            Context->DrawBuffer = (Arg0 == GL_FRONT_LEFT) ? GL_FRONT : Arg0;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglClear(GLbitfield Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLbitfield VirglCleared;

    if (Context == NULL)
        return;

    if ((Arg0 & ~VIRTGPU_OGL_VALID_CLEAR_MASK) != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_CLEAR);
        if (Command != NULL)
            Command->BitArgs = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirglCleared = VirtGpuOglVirglClear(Context, Arg0);

    if (((Arg0 & GL_COLOR_BUFFER_BIT) != 0) &&
        (((VirglCleared & GL_COLOR_BUFFER_BIT) == 0) ||
         !VirtGpuOglPresentVirglColorTarget(Context)))
    {
        VirtGpuOglClearColorBuffer(Context);
    }
}

static void APIENTRY
VirtGpuOglClearAccum(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglClearIndex(GLfloat Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglClearColor(GLclampf Arg0, GLclampf Arg1, GLclampf Arg2, GLclampf Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_CLEAR_COLOR);
        if (Command != NULL)
        {
            Command->FloatArgs[0] = Arg0;
            Command->FloatArgs[1] = Arg1;
            Command->FloatArgs[2] = Arg2;
            Command->FloatArgs[3] = Arg3;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->ClearColor[0] = VirtGpuOglClampFloat(Arg0);
    Context->ClearColor[1] = VirtGpuOglClampFloat(Arg1);
    Context->ClearColor[2] = VirtGpuOglClampFloat(Arg2);
    Context->ClearColor[3] = VirtGpuOglClampFloat(Arg3);
}

static void APIENTRY
VirtGpuOglClearStencil(GLint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_CLEAR_STENCIL);
        if (Command != NULL)
            Command->IntArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->ClearStencil = Arg0;
}

static void APIENTRY
VirtGpuOglClearDepth(GLclampd Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_CLEAR_DEPTH);
        if (Command != NULL)
            Command->DoubleArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->ClearDepth = VirtGpuOglClampDouble(Arg0);
}

static void APIENTRY
VirtGpuOglStencilMask(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_STENCIL_MASK);
        if (Command != NULL)
            Command->UintArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->StencilMask = Arg0;
}

static void APIENTRY
VirtGpuOglColorMask(GLboolean Arg0, GLboolean Arg1, GLboolean Arg2, GLboolean Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_COLOR_MASK);
        if (Command != NULL)
        {
            Command->IntArgs[0] = Arg0 ? GL_TRUE : GL_FALSE;
            Command->IntArgs[1] = Arg1 ? GL_TRUE : GL_FALSE;
            Command->IntArgs[2] = Arg2 ? GL_TRUE : GL_FALSE;
            Command->IntArgs[3] = Arg3 ? GL_TRUE : GL_FALSE;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->ColorMask[0] = Arg0 ? GL_TRUE : GL_FALSE;
    Context->ColorMask[1] = Arg1 ? GL_TRUE : GL_FALSE;
    Context->ColorMask[2] = Arg2 ? GL_TRUE : GL_FALSE;
    Context->ColorMask[3] = Arg3 ? GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglDepthMask(GLboolean Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_DEPTH_MASK);
        if (Command != NULL)
            Command->IntArgs[0] = Arg0 ? GL_TRUE : GL_FALSE;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->DepthMask = Arg0 ? GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglIndexMask(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglAccum(GLenum Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDisable(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLbitfield Bit;

    if (Context == NULL)
        return;

    if (!VirtGpuOglCapToBit(Arg0, &Bit))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_DISABLE);
        if (Command != NULL)
            Command->EnumArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->EnableBits &= ~Bit;
}

static void APIENTRY
VirtGpuOglEnable(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLbitfield Bit;

    if (Context == NULL)
        return;

    if (!VirtGpuOglCapToBit(Arg0, &Bit))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_ENABLE);
        if (Command != NULL)
            Command->EnumArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->EnableBits |= Bit;
}

static void APIENTRY
VirtGpuOglPopAttrib(VOID)
{
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPushAttrib(GLbitfield Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMap1d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2, GLint Arg3, GLint Arg4, const GLdouble * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMap1f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2, GLint Arg3, GLint Arg4, const GLfloat * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMap2d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2, GLint Arg3, GLint Arg4, GLdouble Arg5, GLdouble Arg6, GLint Arg7, GLint Arg8, const GLdouble * Arg9)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    UNREFERENCED_PARAMETER(Arg8);
    UNREFERENCED_PARAMETER(Arg9);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMap2f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2, GLint Arg3, GLint Arg4, GLfloat Arg5, GLfloat Arg6, GLint Arg7, GLint Arg8, const GLfloat * Arg9)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    UNREFERENCED_PARAMETER(Arg8);
    UNREFERENCED_PARAMETER(Arg9);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMapGrid1d(GLint Arg0, GLdouble Arg1, GLdouble Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMapGrid1f(GLint Arg0, GLfloat Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMapGrid2d(GLint Arg0, GLdouble Arg1, GLdouble Arg2, GLint Arg3, GLdouble Arg4, GLdouble Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMapGrid2f(GLint Arg0, GLfloat Arg1, GLfloat Arg2, GLint Arg3, GLfloat Arg4, GLfloat Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalCoord1d(GLdouble Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalCoord1dv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalCoord1f(GLfloat Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalCoord1fv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalCoord2d(GLdouble Arg0, GLdouble Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalCoord2dv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalCoord2f(GLfloat Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalCoord2fv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalMesh1(GLenum Arg0, GLint Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalPoint1(GLint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalMesh2(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEvalPoint2(GLint Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglAlphaFunc(GLenum Arg0, GLclampf Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (!VirtGpuOglValidCompareFunc(Arg0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_ALPHA_FUNC);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->FloatArgs[0] = Arg1;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->AlphaFunc = Arg0;
    Context->AlphaRef = VirtGpuOglClampFloat(Arg1);
}

static void APIENTRY
VirtGpuOglBlendFunc(GLenum Arg0, GLenum Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (!VirtGpuOglValidBlendFactor(Arg0) ||
        !VirtGpuOglValidBlendFactor(Arg1))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_BLEND_FUNC);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->EnumArgs[1] = Arg1;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->BlendSrcFactor = Arg0;
    Context->BlendDstFactor = Arg1;
}

static void APIENTRY
VirtGpuOglLogicOp(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglStencilFunc(GLenum Arg0, GLint Arg1, GLuint Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (!VirtGpuOglValidCompareFunc(Arg0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_STENCIL_FUNC);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->IntArgs[0] = Arg1;
            Command->UintArgs[0] = Arg2;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->StencilFunc = Arg0;
    Context->StencilRef = Arg1;
    Context->StencilValueMask = Arg2;
}

static void APIENTRY
VirtGpuOglStencilOp(GLenum Arg0, GLenum Arg1, GLenum Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (!VirtGpuOglValidStencilOp(Arg0) ||
        !VirtGpuOglValidStencilOp(Arg1) ||
        !VirtGpuOglValidStencilOp(Arg2))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_STENCIL_OP);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->EnumArgs[1] = Arg1;
            Command->EnumArgs[2] = Arg2;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->StencilFail = Arg0;
    Context->StencilDepthFail = Arg1;
    Context->StencilDepthPass = Arg2;
}

static void APIENTRY
VirtGpuOglDepthFunc(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (!VirtGpuOglValidCompareFunc(Arg0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_DEPTH_FUNC);
        if (Command != NULL)
            Command->EnumArgs[0] = Arg0;

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->DepthFunc = Arg0;
}

static void APIENTRY
VirtGpuOglPixelZoom(GLfloat Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPixelTransferf(GLenum Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPixelTransferi(GLenum Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPixelStoref(GLenum Arg0, GLfloat Arg1)
{
    VirtGpuOglPixelStorei(Arg0, (GLint)Arg1);
}

static void APIENTRY
VirtGpuOglPixelStorei(GLenum Arg0, GLint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_PACK_ALIGNMENT:
            if (!VirtGpuOglValidPixelAlignment(Arg1))
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->PackAlignment = Arg1;
            break;
        case GL_UNPACK_ALIGNMENT:
            if (!VirtGpuOglValidPixelAlignment(Arg1))
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->UnpackAlignment = Arg1;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglPixelMapfv(GLenum Arg0, GLint Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPixelMapuiv(GLenum Arg0, GLint Arg1, const GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPixelMapusv(GLenum Arg0, GLint Arg1, const GLushort * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglReadBuffer(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FRONT:
        case GL_FRONT_LEFT:
        case GL_NONE:
            Context->ReadBuffer = (Arg0 == GL_FRONT_LEFT) ? GL_FRONT : Arg0;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglCopyPixels(GLint Arg0, GLint Arg1, GLsizei Arg2, GLsizei Arg3, GLenum Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglReadPixels(GLint Arg0, GLint Arg1, GLsizei Arg2, GLsizei Arg3, GLenum Arg4, GLenum Arg5, GLvoid * Arg6)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_3D_TRANSFER Transfer = NULL;
    PUCHAR Destination;
    PUCHAR SourcePixels = NULL;
    ULONG BytesPerPixel;
    ULONG DestinationStride;
    GLint Row;
    GLint Column;

    if (Context == NULL)
        return;

    if ((Arg2 < 0) || (Arg3 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg6 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg5 != GL_UNSIGNED_BYTE)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    switch (Arg4)
    {
        case GL_RGB:
            BytesPerPixel = 3;
            break;
        case GL_RGBA:
            BytesPerPixel = 4;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if ((Arg2 == 0) || (Arg3 == 0))
        return;

    VirtGpuOglUpdateDrawableSize(Context, FALSE);
    Transfer = VirtGpuOglReadVirglColorTarget(Context);
    if (Transfer != NULL)
        SourcePixels = Transfer->Data;

    Destination = Arg6;
    DestinationStride =
        VirtGpuOglAlignedRowSize((ULONG)Arg2 * BytesPerPixel,
                                 Context->PackAlignment);

    for (Row = 0; Row < Arg3; ++Row)
    {
        PUCHAR RowDestination = Destination + ((ULONG)Row * DestinationStride);
        GLint SourceY = Arg1 + Row;

        for (Column = 0; Column < Arg2; ++Column)
        {
            GLint SourceX = Arg0 + Column;
            BYTE Red = 0;
            BYTE Green = 0;
            BYTE Blue = 0;
            BYTE Alpha = 255;

            if ((SourceX >= 0) &&
                (SourceY >= 0) &&
                (SourceX < Context->DrawableWidth) &&
                (SourceY < Context->DrawableHeight))
            {
                if ((SourcePixels != NULL) &&
                    (SourceX < (GLint)Transfer->Width) &&
                    (SourceY < (GLint)Transfer->Height))
                {
                    ULONG Offset = (((ULONG)Transfer->Height - 1 - (ULONG)SourceY) *
                                    Transfer->Stride) +
                                   ((ULONG)SourceX * 4);
                    Blue = SourcePixels[Offset + 0];
                    Green = SourcePixels[Offset + 1];
                    Red = SourcePixels[Offset + 2];
                    Alpha = SourcePixels[Offset + 3];
                }
                else
                {
                    COLORREF Color;
                    GLint GdiY = Context->DrawableHeight - 1 - SourceY;

                    Color = GetPixel(Context->hdc, SourceX, GdiY);
                    if (Color != CLR_INVALID)
                    {
                        Red = GetRValue(Color);
                        Green = GetGValue(Color);
                        Blue = GetBValue(Color);
                    }
                }
            }

            RowDestination[Column * BytesPerPixel + 0] = Red;
            RowDestination[Column * BytesPerPixel + 1] = Green;
            RowDestination[Column * BytesPerPixel + 2] = Blue;
            if (BytesPerPixel == 4)
                RowDestination[Column * BytesPerPixel + 3] = Alpha;
        }
    }

    if (Transfer != NULL)
        HeapFree(GetProcessHeap(), 0, Transfer);
}

static void APIENTRY
VirtGpuOglDrawPixels(GLsizei Arg0, GLsizei Arg1, GLenum Arg2, GLenum Arg3, const GLvoid * Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetClipPlane(GLenum Arg0, GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetLightfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetLightiv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetMapdv(GLenum Arg0, GLenum Arg1, GLdouble * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetMapfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetMapiv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetMaterialfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetMaterialiv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetPixelMapfv(GLenum Arg0, GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetPixelMapuiv(GLenum Arg0, GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetPixelMapusv(GLenum Arg0, GLushort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetPolygonStipple(GLubyte * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetTexEnvfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetTexEnviv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetTexGendv(GLenum Arg0, GLenum Arg1, GLdouble * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetTexGenfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetTexGeniv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetTexImage(GLenum Arg0, GLint Arg1, GLenum Arg2, GLenum Arg3, GLvoid * Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TEXTURE Texture;
    ULONG SourceBytes;
    ULONG DestBytes;
    ULONG DestStride;
    ULONG Row;
    ULONG Column;

    if (Context == NULL)
        return;

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture == NULL)
        return;

    if ((Arg1 != 0) || (Arg4 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglTextureFormatBytes(Texture->Format, Texture->Type, &SourceBytes) ||
        !VirtGpuOglTextureFormatBytes(Arg2, Arg3, &DestBytes))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Texture->Data == NULL) || (Texture->Width <= 0) || (Texture->Height <= 0))
        return;

    DestStride = VirtGpuOglAlignedRowSize((ULONG)Texture->Width * DestBytes,
                                          Context->PackAlignment);
    for (Row = 0; Row < (ULONG)Texture->Height; ++Row)
    {
        const BYTE *Source = Texture->Data +
                             (Row * (ULONG)Texture->Width * SourceBytes);
        BYTE *Destination = (BYTE *)Arg4 + (Row * DestStride);

        for (Column = 0; Column < (ULONG)Texture->Width; ++Column)
        {
            BYTE Red = Source[Column * SourceBytes + 0];
            BYTE Green = Source[Column * SourceBytes + 1];
            BYTE Blue = Source[Column * SourceBytes + 2];
            BYTE Alpha = (SourceBytes == 4) ?
                         Source[Column * SourceBytes + 3] : 255;

            Destination[Column * DestBytes + 0] = Red;
            Destination[Column * DestBytes + 1] = Green;
            Destination[Column * DestBytes + 2] = Blue;
            if (DestBytes == 4)
                Destination[Column * DestBytes + 3] = Alpha;
        }
    }
}

static void APIENTRY
VirtGpuOglGetTexParameterfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    GLint Value;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetTexParameteriv(Arg0, Arg1, &Value);
    *Arg2 = (GLfloat)Value;
}

static void APIENTRY
VirtGpuOglGetTexParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TEXTURE Texture;

    if (Context == NULL)
        return;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture == NULL)
        return;

    switch (Arg1)
    {
        case GL_TEXTURE_MIN_FILTER:
            *Arg2 = (GLint)Texture->MinFilter;
            break;
        case GL_TEXTURE_MAG_FILTER:
            *Arg2 = (GLint)Texture->MagFilter;
            break;
        case GL_TEXTURE_WRAP_S:
            *Arg2 = (GLint)Texture->WrapS;
            break;
        case GL_TEXTURE_WRAP_T:
            *Arg2 = (GLint)Texture->WrapT;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetTexLevelParameterfv(GLenum Arg0, GLint Arg1, GLenum Arg2, GLfloat * Arg3)
{
    GLint Value;

    if (Arg3 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetTexLevelParameteriv(Arg0, Arg1, Arg2, &Value);
    *Arg3 = (GLfloat)Value;
}

static void APIENTRY
VirtGpuOglGetTexLevelParameteriv(GLenum Arg0, GLint Arg1, GLenum Arg2, GLint * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TEXTURE Texture;

    if (Context == NULL)
        return;

    if (Arg3 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture == NULL)
        return;

    if (Arg1 != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg2)
    {
        case GL_TEXTURE_WIDTH:
            *Arg3 = Texture->Width;
            break;
        case GL_TEXTURE_HEIGHT:
            *Arg3 = (Arg0 == GL_TEXTURE_1D) ? 1 : Texture->Height;
            break;
        case GL_TEXTURE_INTERNAL_FORMAT:
            *Arg3 = (GLint)Texture->InternalFormat;
            break;
        case GL_TEXTURE_BORDER:
            *Arg3 = 0;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static GLboolean APIENTRY
VirtGpuOglIsList(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return GL_FALSE;

    return (VirtGpuOglFindDisplayList(Context, Arg0) != NULL) ? GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglDepthRange(GLclampd Arg0, GLclampd Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_DEPTH_RANGE);
        if (Command != NULL)
        {
            Command->DoubleArgs[0] = Arg0;
            Command->DoubleArgs[1] = Arg1;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->DepthRange[0] = VirtGpuOglClampDouble(Arg0);
    Context->DepthRange[1] = VirtGpuOglClampDouble(Arg1);
}

static void APIENTRY
VirtGpuOglFrustum(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4, GLdouble Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLfloat Matrix[16];

    if (Context == NULL)
        return;

    if ((Arg0 == Arg1) ||
        (Arg2 == Arg3) ||
        (Arg4 == Arg5) ||
        (Arg4 <= 0.0) ||
        (Arg5 <= 0.0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_FRUSTUM);
        if (Command != NULL)
        {
            Command->DoubleArgs[0] = Arg0;
            Command->DoubleArgs[1] = Arg1;
            Command->DoubleArgs[2] = Arg2;
            Command->DoubleArgs[3] = Arg3;
            Command->DoubleArgs[4] = Arg4;
            Command->DoubleArgs[5] = Arg5;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglMatrixIdentity(Matrix);
    Matrix[0] = (GLfloat)((2.0 * Arg4) / (Arg1 - Arg0));
    Matrix[5] = (GLfloat)((2.0 * Arg4) / (Arg3 - Arg2));
    Matrix[8] = (GLfloat)((Arg1 + Arg0) / (Arg1 - Arg0));
    Matrix[9] = (GLfloat)((Arg3 + Arg2) / (Arg3 - Arg2));
    Matrix[10] = (GLfloat)(-(Arg5 + Arg4) / (Arg5 - Arg4));
    Matrix[11] = -1.0f;
    Matrix[14] = (GLfloat)(-(2.0 * Arg5 * Arg4) / (Arg5 - Arg4));
    Matrix[15] = 0.0f;
    VirtGpuOglMultCurrentMatrix(Context, Matrix);
}

static void APIENTRY
VirtGpuOglLoadIdentity(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_LOAD_IDENTITY);
        UNREFERENCED_PARAMETER(Command);

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglMatrixIdentity(VirtGpuOglCurrentMatrix(Context));
}

static void APIENTRY
VirtGpuOglLoadMatrixf(const GLfloat * Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (Arg0 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_LOAD_MATRIXF);
        if (Command != NULL)
            CopyMemory(Command->FloatArgs, Arg0, 16 * sizeof(GLfloat));

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglLoadCurrentMatrix(Context, Arg0);
}

static void APIENTRY
VirtGpuOglLoadMatrixd(const GLdouble * Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLfloat Matrix[16];

    if (Context == NULL)
        return;

    if (Arg0 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglMatrixFromDouble(Matrix, Arg0);

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_LOAD_MATRIXF);
        if (Command != NULL)
            CopyMemory(Command->FloatArgs, Matrix, sizeof(Matrix));

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglLoadCurrentMatrix(Context, Matrix);
}

static void APIENTRY
VirtGpuOglMatrixMode(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_MODELVIEW:
        case GL_PROJECTION:
        case GL_TEXTURE:
            if (VirtGpuOglShouldRecordList(Context))
            {
                Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_MATRIX_MODE);
                if (Command != NULL)
                    Command->EnumArgs[0] = Arg0;

                if (VirtGpuOglRecordingCompileOnly(Context))
                    return;
            }

            Context->MatrixMode = Arg0;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglMultMatrixf(const GLfloat * Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if (Arg0 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_MULT_MATRIXF);
        if (Command != NULL)
            CopyMemory(Command->FloatArgs, Arg0, 16 * sizeof(GLfloat));

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglMultCurrentMatrix(Context, Arg0);
}

static void APIENTRY
VirtGpuOglMultMatrixd(const GLdouble * Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLfloat Matrix[16];

    if (Context == NULL)
        return;

    if (Arg0 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglMatrixFromDouble(Matrix, Arg0);

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_MULT_MATRIXF);
        if (Command != NULL)
            CopyMemory(Command->FloatArgs, Matrix, sizeof(Matrix));

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglMultCurrentMatrix(Context, Matrix);
}

static void APIENTRY
VirtGpuOglOrtho(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4, GLdouble Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLfloat Matrix[16];

    if (Context == NULL)
        return;

    if ((Arg0 == Arg1) || (Arg2 == Arg3) || (Arg4 == Arg5))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_ORTHO);
        if (Command != NULL)
        {
            Command->DoubleArgs[0] = Arg0;
            Command->DoubleArgs[1] = Arg1;
            Command->DoubleArgs[2] = Arg2;
            Command->DoubleArgs[3] = Arg3;
            Command->DoubleArgs[4] = Arg4;
            Command->DoubleArgs[5] = Arg5;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglMatrixIdentity(Matrix);
    Matrix[0] = (GLfloat)(2.0 / (Arg1 - Arg0));
    Matrix[5] = (GLfloat)(2.0 / (Arg3 - Arg2));
    Matrix[10] = (GLfloat)(-2.0 / (Arg5 - Arg4));
    Matrix[12] = (GLfloat)(-(Arg1 + Arg0) / (Arg1 - Arg0));
    Matrix[13] = (GLfloat)(-(Arg3 + Arg2) / (Arg3 - Arg2));
    Matrix[14] = (GLfloat)(-(Arg5 + Arg4) / (Arg5 - Arg4));
    VirtGpuOglMultCurrentMatrix(Context, Matrix);
}

static void APIENTRY
VirtGpuOglPopMatrix(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLuint *Top;

    if (Context == NULL)
        return;

    if (VirtGpuOglRecordingCompileOnly(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_POP_MATRIX);
        UNREFERENCED_PARAMETER(Command);
        return;
    }

    switch (Context->MatrixMode)
    {
        case GL_MODELVIEW:
            Top = &Context->ModelViewStackTop;
            break;
        case GL_PROJECTION:
            Top = &Context->ProjectionStackTop;
            break;
        case GL_TEXTURE:
            Top = &Context->TextureStackTop;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (*Top == 0)
    {
        VirtGpuOglSetError(Context, GL_STACK_UNDERFLOW);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_POP_MATRIX);
        UNREFERENCED_PARAMETER(Command);

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    --(*Top);
}

static void APIENTRY
VirtGpuOglPushMatrix(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLfloat (*Stack)[16];
    GLuint *Top;
    GLuint Limit;

    if (Context == NULL)
        return;

    if (VirtGpuOglRecordingCompileOnly(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_PUSH_MATRIX);
        UNREFERENCED_PARAMETER(Command);
        return;
    }

    switch (Context->MatrixMode)
    {
        case GL_MODELVIEW:
            Stack = Context->ModelViewStack;
            Top = &Context->ModelViewStackTop;
            Limit = VIRTGPU_OGL_MODELVIEW_STACK_DEPTH;
            break;
        case GL_PROJECTION:
            Stack = Context->ProjectionStack;
            Top = &Context->ProjectionStackTop;
            Limit = VIRTGPU_OGL_PROJECTION_STACK_DEPTH;
            break;
        case GL_TEXTURE:
            Stack = Context->TextureStack;
            Top = &Context->TextureStackTop;
            Limit = VIRTGPU_OGL_TEXTURE_STACK_DEPTH;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (*Top + 1 >= Limit)
    {
        VirtGpuOglSetError(Context, GL_STACK_OVERFLOW);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_PUSH_MATRIX);
        UNREFERENCED_PARAMETER(Command);

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglMatrixCopy(Stack[*Top + 1], Stack[*Top]);
    ++(*Top);
}

static void APIENTRY
VirtGpuOglRotated(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLfloat Matrix[16];
    GLdouble Length;
    GLdouble X = Arg1;
    GLdouble Y = Arg2;
    GLdouble Z = Arg3;
    GLdouble Radians;
    GLdouble Sine;
    GLdouble Cosine;
    GLdouble OneMinusCosine;

    if (Context == NULL)
        return;

    Length = sqrt(X * X + Y * Y + Z * Z);
    if (Length == 0.0)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_ROTATED);
        if (Command != NULL)
        {
            Command->DoubleArgs[0] = Arg0;
            Command->DoubleArgs[1] = Arg1;
            Command->DoubleArgs[2] = Arg2;
            Command->DoubleArgs[3] = Arg3;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    X /= Length;
    Y /= Length;
    Z /= Length;
    Radians = Arg0 * (VIRTGPU_OGL_PI / 180.0);
    Sine = sin(Radians);
    Cosine = cos(Radians);
    OneMinusCosine = 1.0 - Cosine;

    VirtGpuOglMatrixIdentity(Matrix);
    Matrix[0] = (GLfloat)(X * X * OneMinusCosine + Cosine);
    Matrix[1] = (GLfloat)(Y * X * OneMinusCosine + Z * Sine);
    Matrix[2] = (GLfloat)(X * Z * OneMinusCosine - Y * Sine);
    Matrix[4] = (GLfloat)(X * Y * OneMinusCosine - Z * Sine);
    Matrix[5] = (GLfloat)(Y * Y * OneMinusCosine + Cosine);
    Matrix[6] = (GLfloat)(Y * Z * OneMinusCosine + X * Sine);
    Matrix[8] = (GLfloat)(X * Z * OneMinusCosine + Y * Sine);
    Matrix[9] = (GLfloat)(Y * Z * OneMinusCosine - X * Sine);
    Matrix[10] = (GLfloat)(Z * Z * OneMinusCosine + Cosine);
    VirtGpuOglMultCurrentMatrix(Context, Matrix);
}

static void APIENTRY
VirtGpuOglRotatef(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    VirtGpuOglRotated(Arg0, Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglScaled(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLfloat Matrix[16];

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_SCALED);
        if (Command != NULL)
        {
            Command->DoubleArgs[0] = Arg0;
            Command->DoubleArgs[1] = Arg1;
            Command->DoubleArgs[2] = Arg2;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglMatrixIdentity(Matrix);
    Matrix[0] = (GLfloat)Arg0;
    Matrix[5] = (GLfloat)Arg1;
    Matrix[10] = (GLfloat)Arg2;
    VirtGpuOglMultCurrentMatrix(Context, Matrix);
}

static void APIENTRY
VirtGpuOglScalef(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    VirtGpuOglScaled(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglTranslated(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLfloat Matrix[16];

    if (Context == NULL)
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_TRANSLATED);
        if (Command != NULL)
        {
            Command->DoubleArgs[0] = Arg0;
            Command->DoubleArgs[1] = Arg1;
            Command->DoubleArgs[2] = Arg2;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    VirtGpuOglMatrixIdentity(Matrix);
    Matrix[12] = (GLfloat)Arg0;
    Matrix[13] = (GLfloat)Arg1;
    Matrix[14] = (GLfloat)Arg2;
    VirtGpuOglMultCurrentMatrix(Context, Matrix);
}

static void APIENTRY
VirtGpuOglTranslatef(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    VirtGpuOglTranslated(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglViewport(GLint Arg0, GLint Arg1, GLsizei Arg2, GLsizei Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;

    if (Context == NULL)
        return;

    if ((Arg2 < 0) || (Arg3 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_VIEWPORT);
        if (Command != NULL)
        {
            Command->IntArgs[0] = Arg0;
            Command->IntArgs[1] = Arg1;
            Command->IntArgs[2] = Arg2;
            Command->IntArgs[3] = Arg3;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Context->Viewport[0] = Arg0;
    Context->Viewport[1] = Arg1;
    Context->Viewport[2] = Arg2;
    Context->Viewport[3] = Arg3;
}

static void APIENTRY
VirtGpuOglArrayElement(GLint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context != NULL)
        (VOID)VirtGpuOglEmitArrayElement(Context, Arg0);
}

static void APIENTRY
VirtGpuOglBindTexture(GLenum Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    PVIRTGPU_OGL_TEXTURE Texture;

    if (Context == NULL)
        return;

    if ((Arg0 != GL_TEXTURE_1D) && (Arg0 != GL_TEXTURE_2D))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_BIND_TEXTURE);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->UintArgs[0] = Arg1;
        }

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Texture = VirtGpuOglFindTexture(Context, Arg1);
    if (Arg1 != 0)
    {
        if (Texture == NULL)
            Texture = VirtGpuOglAllocateTextureName(Context, Arg1);
        if (Texture == NULL)
            return;

        if ((Texture->Target != 0) && (Texture->Target != Arg0))
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }
        Texture->Target = Arg0;
    }

    if (Arg0 == GL_TEXTURE_1D)
        Context->BoundTexture1D = Arg1;
    else
        Context->BoundTexture2D = Arg1;
}

static void APIENTRY
VirtGpuOglColorPointer(GLint Arg0, GLenum Arg1, GLsizei Arg2, const GLvoid * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (((Arg0 != 3) && (Arg0 != 4)) ||
        (Arg2 < 0) ||
        !VirtGpuOglTypeAllowedForColorArray(Arg1))
    {
        VirtGpuOglSetError(Context, (Arg2 < 0) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Context->ColorArraySize = Arg0;
    Context->ColorArrayType = Arg1;
    Context->ColorArrayStride = Arg2;
    Context->ColorArrayPointer = Arg3;
}

static void APIENTRY
VirtGpuOglDisableClientState(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLbitfield Bit;

    if (Context == NULL)
        return;

    if (!VirtGpuOglClientArrayCapToBit(Arg0, &Bit))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Context->ClientArrayBits &= ~Bit;
}

static void APIENTRY
VirtGpuOglDrawArrays(GLenum Arg0, GLint Arg1, GLsizei Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;

    if (Context == NULL)
        return;

    if (Arg2 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg2 == 0)
        return;

    if ((Context->BeginMode != 0) ||
        (VirtGpuOglShouldRecordList(Context) && (Context->RecordingBeginMode != 0)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (!VirtGpuOglBeginModeSupported(Arg0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((ULONG)Arg2 > VIRTGPU_OGL_MAX_IMMEDIATE_VERTICES - 3)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        VirtGpuOglBegin(Arg0);
        for (Index = 0; Index < Arg2; ++Index)
            VirtGpuOglArrayElement(Arg1 + Index);
        VirtGpuOglEnd();
        return;
    }

    Context->BeginMode = Arg0;
    Context->VertexCount = 0;
    for (Index = 0; Index < Arg2; ++Index)
    {
        if (!VirtGpuOglEmitArrayElement(Context, Arg1 + Index))
        {
            Context->BeginMode = 0;
            Context->VertexCount = 0;
            return;
        }
    }

    VirtGpuOglRenderImmediate(Context);
    Context->BeginMode = 0;
    Context->VertexCount = 0;
}

static void APIENTRY
VirtGpuOglDrawElements(GLenum Arg0, GLsizei Arg1, GLenum Arg2, const GLvoid * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    GLint ElementIndex;

    if (Context == NULL)
        return;

    if (Arg1 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg1 == 0)
        return;

    if (Arg3 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Context->BeginMode != 0) ||
        (VirtGpuOglShouldRecordList(Context) && (Context->RecordingBeginMode != 0)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (!VirtGpuOglBeginModeSupported(Arg0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((ULONG)Arg1 > VIRTGPU_OGL_MAX_IMMEDIATE_VERTICES - 3)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    if (VirtGpuOglShouldRecordList(Context))
    {
        VirtGpuOglBegin(Arg0);
        for (Index = 0; Index < Arg1; ++Index)
        {
            switch (Arg2)
            {
                case GL_UNSIGNED_BYTE:
                    ElementIndex = ((const GLubyte *)Arg3)[Index];
                    break;
                case GL_UNSIGNED_SHORT:
                    ElementIndex = ((const GLushort *)Arg3)[Index];
                    break;
                case GL_UNSIGNED_INT:
                    ElementIndex = (GLint)((const GLuint *)Arg3)[Index];
                    break;
                default:
                    VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                    VirtGpuOglEnd();
                    return;
            }

            VirtGpuOglArrayElement(ElementIndex);
        }
        VirtGpuOglEnd();
        return;
    }

    Context->BeginMode = Arg0;
    Context->VertexCount = 0;
    for (Index = 0; Index < Arg1; ++Index)
    {
        switch (Arg2)
        {
            case GL_UNSIGNED_BYTE:
                ElementIndex = ((const GLubyte *)Arg3)[Index];
                break;
            case GL_UNSIGNED_SHORT:
                ElementIndex = ((const GLushort *)Arg3)[Index];
                break;
            case GL_UNSIGNED_INT:
                ElementIndex = (GLint)((const GLuint *)Arg3)[Index];
                break;
            default:
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                Context->BeginMode = 0;
                Context->VertexCount = 0;
                return;
        }

        if (!VirtGpuOglEmitArrayElement(Context, ElementIndex))
        {
            Context->BeginMode = 0;
            Context->VertexCount = 0;
            return;
        }
    }

    VirtGpuOglRenderImmediate(Context);
    Context->BeginMode = 0;
    Context->VertexCount = 0;
}

static void APIENTRY
VirtGpuOglEdgeFlagPointer(GLsizei Arg0, const GLvoid * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEnableClientState(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLbitfield Bit;

    if (Context == NULL)
        return;

    if (!VirtGpuOglClientArrayCapToBit(Arg0, &Bit))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Context->ClientArrayBits |= Bit;
}

static void APIENTRY
VirtGpuOglIndexPointer(GLenum Arg0, GLsizei Arg1, const GLvoid * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglIndexub(GLubyte Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglIndexubv(const GLubyte * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglInterleavedArrays(GLenum Arg0, GLsizei Arg1, const GLvoid * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Stride;
    const BYTE *Pointer = Arg2;

    if (Context == NULL)
        return;

    if (Arg1 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->ClientArrayBits = 0;
    switch (Arg0)
    {
        case GL_V2F:
            Stride = (Arg1 != 0) ? Arg1 : (2 * (GLsizei)sizeof(GLfloat));
            VirtGpuOglVertexPointer(2, GL_FLOAT, Stride, Pointer);
            break;
        case GL_V3F:
            Stride = (Arg1 != 0) ? Arg1 : (3 * (GLsizei)sizeof(GLfloat));
            VirtGpuOglVertexPointer(3, GL_FLOAT, Stride, Pointer);
            break;
        case GL_C3F_V3F:
            Stride = (Arg1 != 0) ? Arg1 : (6 * (GLsizei)sizeof(GLfloat));
            VirtGpuOglColorPointer(3, GL_FLOAT, Stride, Pointer);
            VirtGpuOglVertexPointer(3,
                                    GL_FLOAT,
                                    Stride,
                                    Pointer + (3 * sizeof(GLfloat)));
            Context->ClientArrayBits |= VIRTGPU_OGL_CLIENT_COLOR_ARRAY;
            break;
        case GL_C4UB_V2F:
            Stride = (Arg1 != 0) ? Arg1 :
                     ((4 * (GLsizei)sizeof(GLubyte)) +
                      (2 * (GLsizei)sizeof(GLfloat)));
            VirtGpuOglColorPointer(4, GL_UNSIGNED_BYTE, Stride, Pointer);
            VirtGpuOglVertexPointer(2,
                                    GL_FLOAT,
                                    Stride,
                                    Pointer + (4 * sizeof(GLubyte)));
            Context->ClientArrayBits |= VIRTGPU_OGL_CLIENT_COLOR_ARRAY;
            break;
        case GL_C4UB_V3F:
            Stride = (Arg1 != 0) ? Arg1 :
                     ((4 * (GLsizei)sizeof(GLubyte)) +
                      (3 * (GLsizei)sizeof(GLfloat)));
            VirtGpuOglColorPointer(4, GL_UNSIGNED_BYTE, Stride, Pointer);
            VirtGpuOglVertexPointer(3,
                                    GL_FLOAT,
                                    Stride,
                                    Pointer + (4 * sizeof(GLubyte)));
            Context->ClientArrayBits |= VIRTGPU_OGL_CLIENT_COLOR_ARRAY;
            break;
        case GL_T2F_V3F:
            Stride = (Arg1 != 0) ? Arg1 : (5 * (GLsizei)sizeof(GLfloat));
            VirtGpuOglTexCoordPointer(2, GL_FLOAT, Stride, Pointer);
            VirtGpuOglVertexPointer(3,
                                    GL_FLOAT,
                                    Stride,
                                    Pointer + (2 * sizeof(GLfloat)));
            Context->ClientArrayBits |= VIRTGPU_OGL_CLIENT_TEXCOORD_ARRAY;
            break;
        case GL_T2F_C3F_V3F:
            Stride = (Arg1 != 0) ? Arg1 : (8 * (GLsizei)sizeof(GLfloat));
            VirtGpuOglTexCoordPointer(2, GL_FLOAT, Stride, Pointer);
            VirtGpuOglColorPointer(3,
                                   GL_FLOAT,
                                   Stride,
                                   Pointer + (2 * sizeof(GLfloat)));
            VirtGpuOglVertexPointer(3,
                                    GL_FLOAT,
                                    Stride,
                                    Pointer + (5 * sizeof(GLfloat)));
            Context->ClientArrayBits |= VIRTGPU_OGL_CLIENT_TEXCOORD_ARRAY |
                                        VIRTGPU_OGL_CLIENT_COLOR_ARRAY;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    Context->ClientArrayBits |= VIRTGPU_OGL_CLIENT_VERTEX_ARRAY;
}

static void APIENTRY
VirtGpuOglNormalPointer(GLenum Arg0, GLsizei Arg1, const GLvoid * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Arg1 < 0) || !VirtGpuOglTypeAllowedForNormalArray(Arg0))
    {
        VirtGpuOglSetError(Context, (Arg1 < 0) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Context->NormalArrayType = Arg0;
    Context->NormalArrayStride = Arg1;
    Context->NormalArrayPointer = Arg2;
}

static void APIENTRY
VirtGpuOglPolygonOffset(GLfloat Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexCoordPointer(GLint Arg0, GLenum Arg1, GLsizei Arg2, const GLvoid * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Arg0 < 1) ||
        (Arg0 > 4) ||
        (Arg2 < 0) ||
        !VirtGpuOglTypeAllowedForVertexArray(Arg1))
    {
        VirtGpuOglSetError(Context, (Arg2 < 0) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Context->TexCoordArraySize = Arg0;
    Context->TexCoordArrayType = Arg1;
    Context->TexCoordArrayStride = Arg2;
    Context->TexCoordArrayPointer = Arg3;
}

static void APIENTRY
VirtGpuOglVertexPointer(GLint Arg0, GLenum Arg1, GLsizei Arg2, const GLvoid * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Arg0 < 2) ||
        (Arg0 > 4) ||
        (Arg2 < 0) ||
        !VirtGpuOglTypeAllowedForVertexArray(Arg1))
    {
        VirtGpuOglSetError(Context, (Arg2 < 0) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Context->VertexArraySize = Arg0;
    Context->VertexArrayType = Arg1;
    Context->VertexArrayStride = Arg2;
    Context->VertexArrayPointer = Arg3;
}

static GLboolean APIENTRY
VirtGpuOglAreTexturesResident(GLsizei Arg0, const GLuint * Arg1, GLboolean * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    GLboolean AllResident = GL_TRUE;

    if (Context == NULL)
        return GL_FALSE;

    if (Arg0 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return GL_FALSE;
    }

    if ((Arg0 > 0) && ((Arg1 == NULL) || (Arg2 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return GL_FALSE;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        Arg2[Index] = VirtGpuOglIsTexture(Arg1[Index]);
        if (Arg2[Index] != GL_TRUE)
            AllResident = GL_FALSE;
    }

    return AllResident;
}

static void APIENTRY
VirtGpuOglCopyTexImage1D(GLenum Arg0, GLint Arg1, GLenum Arg2, GLint Arg3, GLint Arg4, GLsizei Arg5, GLint Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCopyTexImage2D(GLenum Arg0, GLint Arg1, GLenum Arg2, GLint Arg3, GLint Arg4, GLsizei Arg5, GLsizei Arg6, GLint Arg7)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCopyTexSubImage1D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLsizei Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCopyTexSubImage2D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLint Arg5, GLsizei Arg6, GLsizei Arg7)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDeleteTextures(GLsizei Arg0, const GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;

    if (Context == NULL)
        return;

    if (Arg0 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg0 > 0) && (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        PVIRTGPU_OGL_TEXTURE Texture;

        if (Arg1[Index] == 0)
            continue;

        Texture = VirtGpuOglFindTexture(Context, Arg1[Index]);
        if (Texture == NULL)
            continue;

        if (Context->BoundTexture1D == Arg1[Index])
            Context->BoundTexture1D = 0;
        if (Context->BoundTexture2D == Arg1[Index])
            Context->BoundTexture2D = 0;
        VirtGpuOglFreeTexture(Texture);
    }
}

static void APIENTRY
VirtGpuOglGenTextures(GLsizei Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;

    if (Context == NULL)
        return;

    if (Arg0 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg0 > 0) && (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        GLuint Name;

        do
        {
            Name = Context->NextTextureName++;
            if (Context->NextTextureName == 0)
                Context->NextTextureName = 1;
        } while ((Name == 0) || (VirtGpuOglFindTexture(Context, Name) != NULL));

        if (VirtGpuOglAllocateTextureName(Context, Name) == NULL)
            return;
        Arg1[Index] = Name;
    }
}

static void APIENTRY
VirtGpuOglGetPointerv(GLenum Arg0, GLvoid * * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg0)
    {
        case GL_VERTEX_ARRAY_POINTER:
            *Arg1 = (GLvoid *)Context->VertexArrayPointer;
            break;
        case GL_COLOR_ARRAY_POINTER:
            *Arg1 = (GLvoid *)Context->ColorArrayPointer;
            break;
        case GL_NORMAL_ARRAY_POINTER:
            *Arg1 = (GLvoid *)Context->NormalArrayPointer;
            break;
        case GL_TEXTURE_COORD_ARRAY_POINTER:
            *Arg1 = (GLvoid *)Context->TexCoordArrayPointer;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static GLboolean APIENTRY
VirtGpuOglIsTexture(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TEXTURE Texture;

    if (Context == NULL)
        return GL_FALSE;

    Texture = VirtGpuOglFindTexture(Context, Arg0);
    return ((Texture != NULL) && (Texture->Target != 0)) ? GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglPrioritizeTextures(GLsizei Arg0, const GLuint * Arg1, const GLclampf * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    if (Context == NULL)
        return;

    if (Arg0 < 0)
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglTexSubImage1D(GLenum Arg0, GLint Arg1, GLint Arg2, GLsizei Arg3, GLenum Arg4, GLenum Arg5, const GLvoid * Arg6)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    PVIRTGPU_OGL_TEXTURE Texture;
    BYTE *Data = NULL;
    ULONG DataSize = 0;
    ULONG BytesPerPixel;
    ULONG RowSize;

    if (Context == NULL)
        return;

    if (Arg2 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglValidateTextureSubImageParameters(Context,
                                                     Arg0,
                                                     Arg1,
                                                     Arg3,
                                                     1,
                                                     Arg4,
                                                     Arg5))
    {
        return;
    }

    if ((Arg3 == 0) || (Arg6 == NULL))
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        if (!VirtGpuOglCopyTexturePixels(Context,
                                         Arg3,
                                         1,
                                         Arg4,
                                         Arg5,
                                         Arg6,
                                         &Data,
                                         &DataSize))
        {
            return;
        }

        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_TEX_SUB_IMAGE_1D);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->EnumArgs[1] = Arg4;
            Command->EnumArgs[2] = Arg5;
            Command->IntArgs[0] = Arg1;
            Command->IntArgs[1] = Arg2;
            Command->IntArgs[2] = Arg3;
            Command->DataSize = DataSize;
            Command->Data = Data;
            Data = NULL;
        }

        if (Data != NULL)
            HeapFree(GetProcessHeap(), 0, Data);

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture == NULL)
        return;

    if ((Arg1 != 0) ||
        (Arg2 < 0) ||
        (Arg3 < 0) ||
        (Arg2 + Arg3 > Texture->Width) ||
        (Texture->Data == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglTextureFormatBytes(Arg4, Arg5, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Arg4 != Texture->Format) || (Arg5 != Texture->Type))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if ((Arg3 == 0) || (Arg6 == NULL))
        return;

    RowSize = (ULONG)Arg3 * BytesPerPixel;
    CopyMemory(Texture->Data + ((ULONG)Arg2 * BytesPerPixel), Arg6, RowSize);
}

static void APIENTRY
VirtGpuOglTexSubImage2D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4, GLsizei Arg5, GLenum Arg6, GLenum Arg7, const GLvoid * Arg8)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    PVIRTGPU_OGL_TEXTURE Texture;
    BYTE *Data = NULL;
    ULONG DataSize = 0;
    ULONG BytesPerPixel;
    ULONG SourceStride;
    ULONG RowSize;
    GLsizei Row;

    if (Context == NULL)
        return;

    if ((Arg2 < 0) || (Arg3 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglValidateTextureSubImageParameters(Context,
                                                     Arg0,
                                                     Arg1,
                                                     Arg4,
                                                     Arg5,
                                                     Arg6,
                                                     Arg7))
    {
        return;
    }

    if ((Arg4 == 0) || (Arg5 == 0) || (Arg8 == NULL))
        return;

    if (VirtGpuOglShouldRecordList(Context))
    {
        if (!VirtGpuOglCopyTexturePixels(Context,
                                         Arg4,
                                         Arg5,
                                         Arg6,
                                         Arg7,
                                         Arg8,
                                         &Data,
                                         &DataSize))
        {
            return;
        }

        Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_TEX_SUB_IMAGE_2D);
        if (Command != NULL)
        {
            Command->EnumArgs[0] = Arg0;
            Command->EnumArgs[1] = Arg6;
            Command->EnumArgs[2] = Arg7;
            Command->IntArgs[0] = Arg1;
            Command->IntArgs[1] = Arg2;
            Command->IntArgs[2] = Arg3;
            Command->IntArgs[3] = Arg4;
            Command->IntArgs[4] = Arg5;
            Command->DataSize = DataSize;
            Command->Data = Data;
            Data = NULL;
        }

        if (Data != NULL)
            HeapFree(GetProcessHeap(), 0, Data);

        if (VirtGpuOglRecordingCompileOnly(Context))
            return;
    }

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture == NULL)
        return;

    if ((Arg1 != 0) ||
        (Arg2 < 0) ||
        (Arg3 < 0) ||
        (Arg4 < 0) ||
        (Arg5 < 0) ||
        (Arg2 + Arg4 > Texture->Width) ||
        (Arg3 + Arg5 > Texture->Height) ||
        (Texture->Data == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglTextureFormatBytes(Arg6, Arg7, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Arg6 != Texture->Format) || (Arg7 != Texture->Type))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if ((Arg4 == 0) || (Arg5 == 0) || (Arg8 == NULL))
        return;

    RowSize = (ULONG)Arg4 * BytesPerPixel;
    SourceStride = VirtGpuOglAlignedRowSize(RowSize, Context->UnpackAlignment);
    for (Row = 0; Row < Arg5; ++Row)
    {
        CopyMemory(Texture->Data +
                   (((ULONG)(Arg3 + Row) * (ULONG)Texture->Width +
                     (ULONG)Arg2) * BytesPerPixel),
                   (const BYTE *)Arg8 + ((ULONG)Row * SourceStride),
                   RowSize);
    }
}

static void APIENTRY
VirtGpuOglPopClientAttrib(VOID)
{
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPushClientAttrib(GLbitfield Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawRangeElements(GLenum Arg0, GLuint Arg1, GLuint Arg2, GLsizei Arg3, GLenum Arg4, const void * Arg5)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    VirtGpuOglDrawElements(Arg0, Arg3, Arg4, Arg5);
}

static void APIENTRY
VirtGpuOglTexImage3D(GLenum Arg0, GLint Arg1, GLint Arg2, GLsizei Arg3, GLsizei Arg4, GLsizei Arg5, GLint Arg6, GLenum Arg7, GLenum Arg8, const void * Arg9)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    UNREFERENCED_PARAMETER(Arg8);
    UNREFERENCED_PARAMETER(Arg9);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexSubImage3D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLsizei Arg5, GLsizei Arg6, GLsizei Arg7, GLenum Arg8, GLenum Arg9, const void * Arg10)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    UNREFERENCED_PARAMETER(Arg8);
    UNREFERENCED_PARAMETER(Arg9);
    UNREFERENCED_PARAMETER(Arg10);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCopyTexSubImage3D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLint Arg5, GLint Arg6, GLsizei Arg7, GLsizei Arg8)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    UNREFERENCED_PARAMETER(Arg8);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglActiveTexture(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSampleCoverage(GLfloat Arg0, GLboolean Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCompressedTexImage3D(GLenum Arg0, GLint Arg1, GLenum Arg2, GLsizei Arg3, GLsizei Arg4, GLsizei Arg5, GLint Arg6, GLsizei Arg7, const void * Arg8)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    UNREFERENCED_PARAMETER(Arg8);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCompressedTexImage2D(GLenum Arg0, GLint Arg1, GLenum Arg2, GLsizei Arg3, GLsizei Arg4, GLint Arg5, GLsizei Arg6, const void * Arg7)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCompressedTexImage1D(GLenum Arg0, GLint Arg1, GLenum Arg2, GLsizei Arg3, GLint Arg4, GLsizei Arg5, const void * Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCompressedTexSubImage3D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLsizei Arg5, GLsizei Arg6, GLsizei Arg7, GLenum Arg8, GLsizei Arg9, const void * Arg10)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    UNREFERENCED_PARAMETER(Arg8);
    UNREFERENCED_PARAMETER(Arg9);
    UNREFERENCED_PARAMETER(Arg10);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCompressedTexSubImage2D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4, GLsizei Arg5, GLenum Arg6, GLsizei Arg7, const void * Arg8)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    UNREFERENCED_PARAMETER(Arg8);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCompressedTexSubImage1D(GLenum Arg0, GLint Arg1, GLint Arg2, GLsizei Arg3, GLenum Arg4, GLsizei Arg5, const void * Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetCompressedTexImage(GLenum Arg0, GLint Arg1, void * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBlendFuncSeparate(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLenum Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglBlendFunc(Arg0, Arg1);
}

static void APIENTRY
VirtGpuOglMultiDrawArrays(GLenum Arg0, const GLint * Arg1, const GLsizei * Arg2, GLsizei Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiDrawElements(GLenum Arg0, const GLsizei * Arg1, GLenum Arg2, const void *const * Arg3, GLsizei Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPointParameterf(GLenum Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPointParameterfv(GLenum Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPointParameteri(GLenum Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPointParameteriv(GLenum Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBlendColor(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBlendEquation(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGenQueries(GLsizei Arg0, GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDeleteQueries(GLsizei Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static GLboolean APIENTRY
VirtGpuOglIsQuery(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglBeginQuery(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEndQuery(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetQueryiv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetQueryObjectiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetQueryObjectuiv(GLuint Arg0, GLenum Arg1, GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBindBuffer(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDeleteBuffers(GLsizei Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGenBuffers(GLsizei Arg0, GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static GLboolean APIENTRY
VirtGpuOglIsBuffer(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglBufferData(GLenum Arg0, GLsizeiptr Arg1, const void * Arg2, GLenum Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBufferSubData(GLenum Arg0, GLintptr Arg1, GLsizeiptr Arg2, const void * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetBufferSubData(GLenum Arg0, GLintptr Arg1, GLsizeiptr Arg2, void * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void * APIENTRY
VirtGpuOglMapBuffer(GLenum Arg0, GLenum Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
    return NULL;
}

static GLboolean APIENTRY
VirtGpuOglUnmapBuffer(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglGetBufferParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetBufferPointerv(GLenum Arg0, GLenum Arg1, void * * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBlendEquationSeparate(GLenum Arg0, GLenum Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawBuffers(GLsizei Arg0, const GLenum * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Arg0 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg0 == 0)
    {
        Context->DrawBuffer = GL_NONE;
        return;
    }

    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg0 != 1)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    VirtGpuOglDrawBuffer(Arg1[0]);
}

static void APIENTRY
VirtGpuOglStencilOpSeparate(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLenum Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FRONT:
        case GL_BACK:
        case GL_FRONT_AND_BACK:
            VirtGpuOglStencilOp(Arg1, Arg2, Arg3);
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglStencilFuncSeparate(GLenum Arg0, GLenum Arg1, GLint Arg2, GLuint Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FRONT:
        case GL_BACK:
        case GL_FRONT_AND_BACK:
            VirtGpuOglStencilFunc(Arg1, Arg2, Arg3);
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglStencilMaskSeparate(GLenum Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FRONT:
        case GL_BACK:
        case GL_FRONT_AND_BACK:
            Context->StencilMask = Arg1;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglAttachShader(GLuint Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBindAttribLocation(GLuint Arg0, GLuint Arg1, const GLchar * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCompileShader(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static GLuint APIENTRY
VirtGpuOglCreateProgram(VOID)
{
    VirtGpuOglUnsupportedCall();
    return 0;
}

static GLuint APIENTRY
VirtGpuOglCreateShader(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglDeleteProgram(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDeleteShader(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDetachShader(GLuint Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDisableVertexAttribArray(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEnableVertexAttribArray(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetActiveAttrib(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLint * Arg4, GLenum * Arg5, GLchar * Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetActiveUniform(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLint * Arg4, GLenum * Arg5, GLchar * Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetAttachedShaders(GLuint Arg0, GLsizei Arg1, GLsizei * Arg2, GLuint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static GLint APIENTRY
VirtGpuOglGetAttribLocation(GLuint Arg0, const GLchar * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglGetProgramiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetProgramInfoLog(GLuint Arg0, GLsizei Arg1, GLsizei * Arg2, GLchar * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetShaderiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetShaderInfoLog(GLuint Arg0, GLsizei Arg1, GLsizei * Arg2, GLchar * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetShaderSource(GLuint Arg0, GLsizei Arg1, GLsizei * Arg2, GLchar * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static GLint APIENTRY
VirtGpuOglGetUniformLocation(GLuint Arg0, const GLchar * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglGetUniformfv(GLuint Arg0, GLint Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetUniformiv(GLuint Arg0, GLint Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetVertexAttribdv(GLuint Arg0, GLenum Arg1, GLdouble * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetVertexAttribfv(GLuint Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetVertexAttribiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetVertexAttribPointerv(GLuint Arg0, GLenum Arg1, void * * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static GLboolean APIENTRY
VirtGpuOglIsProgram(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static GLboolean APIENTRY
VirtGpuOglIsShader(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglLinkProgram(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglShaderSource(GLuint Arg0, GLsizei Arg1, const GLchar *const * Arg2, const GLint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUseProgram(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform1f(GLint Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform2f(GLint Arg0, GLfloat Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform3f(GLint Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform4f(GLint Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3, GLfloat Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform1i(GLint Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform2i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform3i(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform4i(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform1fv(GLint Arg0, GLsizei Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform2fv(GLint Arg0, GLsizei Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform3fv(GLint Arg0, GLsizei Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform4fv(GLint Arg0, GLsizei Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform1iv(GLint Arg0, GLsizei Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform2iv(GLint Arg0, GLsizei Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform3iv(GLint Arg0, GLsizei Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform4iv(GLint Arg0, GLsizei Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix2fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix3fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix4fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglValidateProgram(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib1d(GLuint Arg0, GLdouble Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib1dv(GLuint Arg0, const GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib1f(GLuint Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib1fv(GLuint Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib1s(GLuint Arg0, GLshort Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib1sv(GLuint Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib2d(GLuint Arg0, GLdouble Arg1, GLdouble Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib2dv(GLuint Arg0, const GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib2f(GLuint Arg0, GLfloat Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib2fv(GLuint Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib2s(GLuint Arg0, GLshort Arg1, GLshort Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib2sv(GLuint Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib3d(GLuint Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib3dv(GLuint Arg0, const GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib3f(GLuint Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib3fv(GLuint Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib3s(GLuint Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib3sv(GLuint Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nbv(GLuint Arg0, const GLbyte * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4Niv(GLuint Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nsv(GLuint Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nub(GLuint Arg0, GLubyte Arg1, GLubyte Arg2, GLubyte Arg3, GLubyte Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nubv(GLuint Arg0, const GLubyte * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nuiv(GLuint Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nusv(GLuint Arg0, const GLushort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4bv(GLuint Arg0, const GLbyte * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4d(GLuint Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4dv(GLuint Arg0, const GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4f(GLuint Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3, GLfloat Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4fv(GLuint Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4iv(GLuint Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4s(GLuint Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3, GLshort Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4sv(GLuint Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4ubv(GLuint Arg0, const GLubyte * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4uiv(GLuint Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttrib4usv(GLuint Arg0, const GLushort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribPointer(GLuint Arg0, GLint Arg1, GLenum Arg2, GLboolean Arg3, GLsizei Arg4, const void * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix2x3fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix3x2fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix2x4fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix4x2fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix3x4fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix4x3fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglColorMaski(GLuint Arg0, GLboolean Arg1, GLboolean Arg2, GLboolean Arg3, GLboolean Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetBooleani_v(GLenum Arg0, GLuint Arg1, GLboolean * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetIntegeri_v(GLenum Arg0, GLuint Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEnablei(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDisablei(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static GLboolean APIENTRY
VirtGpuOglIsEnabledi(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglBeginTransformFeedback(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEndTransformFeedback(VOID)
{
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBindBufferRange(GLenum Arg0, GLuint Arg1, GLuint Arg2, GLintptr Arg3, GLsizeiptr Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBindBufferBase(GLenum Arg0, GLuint Arg1, GLuint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTransformFeedbackVaryings(GLuint Arg0, GLsizei Arg1, const GLchar *const * Arg2, GLenum Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetTransformFeedbackVarying(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLsizei * Arg4, GLenum * Arg5, GLchar * Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglClampColor(GLenum Arg0, GLenum Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBeginConditionalRender(GLuint Arg0, GLenum Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEndConditionalRender(VOID)
{
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribIPointer(GLuint Arg0, GLint Arg1, GLenum Arg2, GLsizei Arg3, const void * Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetVertexAttribIiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetVertexAttribIuiv(GLuint Arg0, GLenum Arg1, GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI1i(GLuint Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI2i(GLuint Arg0, GLint Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI3i(GLuint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI4i(GLuint Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI1ui(GLuint Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI2ui(GLuint Arg0, GLuint Arg1, GLuint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI3ui(GLuint Arg0, GLuint Arg1, GLuint Arg2, GLuint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI4ui(GLuint Arg0, GLuint Arg1, GLuint Arg2, GLuint Arg3, GLuint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI1iv(GLuint Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI2iv(GLuint Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI3iv(GLuint Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI4iv(GLuint Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI1uiv(GLuint Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI2uiv(GLuint Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI3uiv(GLuint Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI4uiv(GLuint Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI4bv(GLuint Arg0, const GLbyte * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI4sv(GLuint Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI4ubv(GLuint Arg0, const GLubyte * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribI4usv(GLuint Arg0, const GLushort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetUniformuiv(GLuint Arg0, GLint Arg1, GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBindFragDataLocation(GLuint Arg0, GLuint Arg1, const GLchar * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static GLint APIENTRY
VirtGpuOglGetFragDataLocation(GLuint Arg0, const GLchar * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglUniform1ui(GLint Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform2ui(GLint Arg0, GLuint Arg1, GLuint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform3ui(GLint Arg0, GLuint Arg1, GLuint Arg2, GLuint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform4ui(GLint Arg0, GLuint Arg1, GLuint Arg2, GLuint Arg3, GLuint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform1uiv(GLint Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform2uiv(GLint Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform3uiv(GLint Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform4uiv(GLint Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexParameterIiv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    VirtGpuOglTexParameteriv(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglTexParameterIuiv(GLenum Arg0, GLenum Arg1, const GLuint * Arg2)
{
    GLint Value;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    Value = (GLint)Arg2[0];
    VirtGpuOglTexParameteriv(Arg0, Arg1, &Value);
}

static void APIENTRY
VirtGpuOglGetTexParameterIiv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    VirtGpuOglGetTexParameteriv(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglGetTexParameterIuiv(GLenum Arg0, GLenum Arg1, GLuint * Arg2)
{
    GLint Value;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetTexParameteriv(Arg0, Arg1, &Value);
    *Arg2 = (GLuint)Value;
}

static void APIENTRY
VirtGpuOglClearBufferiv(GLenum Arg0, GLint Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglClearBufferuiv(GLenum Arg0, GLint Arg1, const GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglClearBufferfv(GLenum Arg0, GLint Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglClearBufferfi(GLenum Arg0, GLint Arg1, GLfloat Arg2, GLint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static const GLubyte * APIENTRY
VirtGpuOglGetStringi(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
    return NULL;
}

static GLboolean APIENTRY
VirtGpuOglIsRenderbuffer(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglBindRenderbuffer(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDeleteRenderbuffers(GLsizei Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGenRenderbuffers(GLsizei Arg0, GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRenderbufferStorage(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLsizei Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetRenderbufferParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static GLboolean APIENTRY
VirtGpuOglIsFramebuffer(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglBindFramebuffer(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDeleteFramebuffers(GLsizei Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGenFramebuffers(GLsizei Arg0, GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static GLenum APIENTRY
VirtGpuOglCheckFramebufferStatus(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglFramebufferTexture1D(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLuint Arg3, GLint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFramebufferTexture2D(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLuint Arg3, GLint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFramebufferTexture3D(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLuint Arg3, GLint Arg4, GLint Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFramebufferRenderbuffer(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLuint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetFramebufferAttachmentParameteriv(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGenerateMipmap(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBlitFramebuffer(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLint Arg5, GLint Arg6, GLint Arg7, GLbitfield Arg8, GLenum Arg9)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    UNREFERENCED_PARAMETER(Arg8);
    UNREFERENCED_PARAMETER(Arg9);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglRenderbufferStorageMultisample(GLenum Arg0, GLsizei Arg1, GLenum Arg2, GLsizei Arg3, GLsizei Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFramebufferTextureLayer(GLenum Arg0, GLenum Arg1, GLuint Arg2, GLint Arg3, GLint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void * APIENTRY
VirtGpuOglMapBufferRange(GLenum Arg0, GLintptr Arg1, GLsizeiptr Arg2, GLbitfield Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
    return NULL;
}

static void APIENTRY
VirtGpuOglFlushMappedBufferRange(GLenum Arg0, GLintptr Arg1, GLsizeiptr Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBindVertexArray(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDeleteVertexArrays(GLsizei Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGenVertexArrays(GLsizei Arg0, GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static GLboolean APIENTRY
VirtGpuOglIsVertexArray(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglDrawArraysInstanced(GLenum Arg0, GLint Arg1, GLsizei Arg2, GLsizei Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawElementsInstanced(GLenum Arg0, GLsizei Arg1, GLenum Arg2, const void * Arg3, GLsizei Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexBuffer(GLenum Arg0, GLenum Arg1, GLuint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPrimitiveRestartIndex(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCopyBufferSubData(GLenum Arg0, GLenum Arg1, GLintptr Arg2, GLintptr Arg3, GLsizeiptr Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetUniformIndices(GLuint Arg0, GLsizei Arg1, const GLchar *const * Arg2, GLuint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetActiveUniformsiv(GLuint Arg0, GLsizei Arg1, const GLuint * Arg2, GLenum Arg3, GLint * Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetActiveUniformName(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLchar * Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static GLuint APIENTRY
VirtGpuOglGetUniformBlockIndex(GLuint Arg0, const GLchar * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglGetActiveUniformBlockiv(GLuint Arg0, GLuint Arg1, GLenum Arg2, GLint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetActiveUniformBlockName(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLchar * Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformBlockBinding(GLuint Arg0, GLuint Arg1, GLuint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawElementsBaseVertex(GLenum Arg0, GLsizei Arg1, GLenum Arg2, const void * Arg3, GLint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawRangeElementsBaseVertex(GLenum Arg0, GLuint Arg1, GLuint Arg2, GLsizei Arg3, GLenum Arg4, const void * Arg5, GLint Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawElementsInstancedBaseVertex(GLenum Arg0, GLsizei Arg1, GLenum Arg2, const void * Arg3, GLsizei Arg4, GLint Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiDrawElementsBaseVertex(GLenum Arg0, const GLsizei * Arg1, GLenum Arg2, const void *const * Arg3, GLsizei Arg4, const GLint * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglProvokingVertex(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static GLsync APIENTRY
VirtGpuOglFenceSync(GLenum Arg0, GLbitfield Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
    return NULL;
}

static GLboolean APIENTRY
VirtGpuOglIsSync(GLsync Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglDeleteSync(GLsync Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static GLenum APIENTRY
VirtGpuOglClientWaitSync(GLsync Arg0, GLbitfield Arg1, GLuint64 Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglWaitSync(GLsync Arg0, GLbitfield Arg1, GLuint64 Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetInteger64v(GLenum Arg0, GLint64 * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetSynciv(GLsync Arg0, GLenum Arg1, GLsizei Arg2, GLsizei * Arg3, GLint * Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetInteger64i_v(GLenum Arg0, GLuint Arg1, GLint64 * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetBufferParameteri64v(GLenum Arg0, GLenum Arg1, GLint64 * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFramebufferTexture(GLenum Arg0, GLenum Arg1, GLuint Arg2, GLint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexImage2DMultisample(GLenum Arg0, GLsizei Arg1, GLenum Arg2, GLsizei Arg3, GLsizei Arg4, GLboolean Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglTexImage3DMultisample(GLenum Arg0, GLsizei Arg1, GLenum Arg2, GLsizei Arg3, GLsizei Arg4, GLsizei Arg5, GLboolean Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetMultisamplefv(GLenum Arg0, GLuint Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSampleMaski(GLuint Arg0, GLbitfield Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBindFragDataLocationIndexed(GLuint Arg0, GLuint Arg1, GLuint Arg2, const GLchar * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static GLint APIENTRY
VirtGpuOglGetFragDataIndex(GLuint Arg0, const GLchar * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglGenSamplers(GLsizei Arg0, GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDeleteSamplers(GLsizei Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static GLboolean APIENTRY
VirtGpuOglIsSampler(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglBindSampler(GLuint Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSamplerParameteri(GLuint Arg0, GLenum Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSamplerParameteriv(GLuint Arg0, GLenum Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSamplerParameterf(GLuint Arg0, GLenum Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSamplerParameterfv(GLuint Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSamplerParameterIiv(GLuint Arg0, GLenum Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSamplerParameterIuiv(GLuint Arg0, GLenum Arg1, const GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetSamplerParameteriv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetSamplerParameterIiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetSamplerParameterfv(GLuint Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetSamplerParameterIuiv(GLuint Arg0, GLenum Arg1, GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglQueryCounter(GLuint Arg0, GLenum Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetQueryObjecti64v(GLuint Arg0, GLenum Arg1, GLint64 * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetQueryObjectui64v(GLuint Arg0, GLenum Arg1, GLuint64 * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribDivisor(GLuint Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribP1ui(GLuint Arg0, GLenum Arg1, GLboolean Arg2, GLuint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribP1uiv(GLuint Arg0, GLenum Arg1, GLboolean Arg2, const GLuint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribP2ui(GLuint Arg0, GLenum Arg1, GLboolean Arg2, GLuint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribP2uiv(GLuint Arg0, GLenum Arg1, GLboolean Arg2, const GLuint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribP3ui(GLuint Arg0, GLenum Arg1, GLboolean Arg2, GLuint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribP3uiv(GLuint Arg0, GLenum Arg1, GLboolean Arg2, const GLuint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribP4ui(GLuint Arg0, GLenum Arg1, GLboolean Arg2, GLuint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglVertexAttribP4uiv(GLuint Arg0, GLenum Arg1, GLboolean Arg2, const GLuint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMinSampleShading(GLfloat Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBlendEquationi(GLuint Arg0, GLenum Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBlendEquationSeparatei(GLuint Arg0, GLenum Arg1, GLenum Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBlendFunci(GLuint Arg0, GLenum Arg1, GLenum Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBlendFuncSeparatei(GLuint Arg0, GLenum Arg1, GLenum Arg2, GLenum Arg3, GLenum Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawArraysIndirect(GLenum Arg0, const void * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawElementsIndirect(GLenum Arg0, GLenum Arg1, const void * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform1d(GLint Arg0, GLdouble Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform2d(GLint Arg0, GLdouble Arg1, GLdouble Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform3d(GLint Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform4d(GLint Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform1dv(GLint Arg0, GLsizei Arg1, const GLdouble * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform2dv(GLint Arg0, GLsizei Arg1, const GLdouble * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform3dv(GLint Arg0, GLsizei Arg1, const GLdouble * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniform4dv(GLint Arg0, GLsizei Arg1, const GLdouble * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix2dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix3dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix4dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix2x3dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix2x4dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix3x2dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix3x4dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix4x2dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformMatrix4x3dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetUniformdv(GLuint Arg0, GLint Arg1, GLdouble * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static GLint APIENTRY
VirtGpuOglGetSubroutineUniformLocation(GLuint Arg0, GLenum Arg1, const GLchar * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static GLuint APIENTRY
VirtGpuOglGetSubroutineIndex(GLuint Arg0, GLenum Arg1, const GLchar * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
    return 0;
}

static void APIENTRY
VirtGpuOglGetActiveSubroutineUniformiv(GLuint Arg0, GLenum Arg1, GLuint Arg2, GLenum Arg3, GLint * Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetActiveSubroutineUniformName(GLuint Arg0, GLenum Arg1, GLuint Arg2, GLsizei Arg3, GLsizei * Arg4, GLchar * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetActiveSubroutineName(GLuint Arg0, GLenum Arg1, GLuint Arg2, GLsizei Arg3, GLsizei * Arg4, GLchar * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglUniformSubroutinesuiv(GLenum Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetUniformSubroutineuiv(GLenum Arg0, GLint Arg1, GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetProgramStageiv(GLuint Arg0, GLenum Arg1, GLenum Arg2, GLint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPatchParameteri(GLenum Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglPatchParameterfv(GLenum Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBindTransformFeedback(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDeleteTransformFeedbacks(GLsizei Arg0, const GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGenTransformFeedbacks(GLsizei Arg0, GLuint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static GLboolean APIENTRY
VirtGpuOglIsTransformFeedback(GLuint Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
    return GL_FALSE;
}

static void APIENTRY
VirtGpuOglPauseTransformFeedback(VOID)
{
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglResumeTransformFeedback(VOID)
{
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawTransformFeedback(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglDrawTransformFeedbackStream(GLenum Arg0, GLuint Arg1, GLuint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglBeginQueryIndexed(GLenum Arg0, GLuint Arg1, GLuint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglEndQueryIndexed(GLenum Arg0, GLuint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetQueryIndexediv(GLenum Arg0, GLuint Arg1, GLenum Arg2, GLint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglColorTable(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLenum Arg3, GLenum Arg4, const GLvoid * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglColorTableParameterfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglColorTableParameteriv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCopyColorTable(GLenum Arg0, GLenum Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetColorTable(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLvoid * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetColorTableParameterfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetColorTableParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglColorSubTable(GLenum Arg0, GLsizei Arg1, GLsizei Arg2, GLenum Arg3, GLenum Arg4, const GLvoid * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCopyColorSubTable(GLenum Arg0, GLsizei Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglConvolutionFilter1D(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLenum Arg3, GLenum Arg4, const GLvoid * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglConvolutionFilter2D(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLsizei Arg3, GLenum Arg4, GLenum Arg5, const GLvoid * Arg6)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglConvolutionParameterf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglConvolutionParameterfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglConvolutionParameteri(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglConvolutionParameteriv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCopyConvolutionFilter1D(GLenum Arg0, GLenum Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglCopyConvolutionFilter2D(GLenum Arg0, GLenum Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4, GLsizei Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetConvolutionFilter(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLvoid * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetConvolutionParameterfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetConvolutionParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetSeparableFilter(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLvoid * Arg3, GLvoid * Arg4, GLvoid * Arg5)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSeparableFilter2D(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLsizei Arg3, GLenum Arg4, GLenum Arg5, const GLvoid * Arg6, const GLvoid * Arg7)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetHistogram(GLenum Arg0, GLboolean Arg1, GLenum Arg2, GLenum Arg3, GLvoid * Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetHistogramParameterfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetHistogramParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetMinmax(GLenum Arg0, GLboolean Arg1, GLenum Arg2, GLenum Arg3, GLvoid * Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetMinmaxParameterfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglGetMinmaxParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglHistogram(GLenum Arg0, GLsizei Arg1, GLenum Arg2, GLboolean Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMinmax(GLenum Arg0, GLenum Arg1, GLboolean Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglResetHistogram(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglResetMinmax(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglClientActiveTexture(GLenum Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord1d(GLenum Arg0, GLdouble Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord1dv(GLenum Arg0, const GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord1f(GLenum Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord1fv(GLenum Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord1i(GLenum Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord1iv(GLenum Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord1s(GLenum Arg0, GLshort Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord1sv(GLenum Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord2d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord2dv(GLenum Arg0, const GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord2f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord2fv(GLenum Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord2i(GLenum Arg0, GLint Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord2iv(GLenum Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord2s(GLenum Arg0, GLshort Arg1, GLshort Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord2sv(GLenum Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord3d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord3dv(GLenum Arg0, const GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord3f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord3fv(GLenum Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord3i(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord3iv(GLenum Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord3s(GLenum Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord3sv(GLenum Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord4d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord4dv(GLenum Arg0, const GLdouble * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord4f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3, GLfloat Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord4fv(GLenum Arg0, const GLfloat * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord4i(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord4iv(GLenum Arg0, const GLint * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord4s(GLenum Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3, GLshort Arg4)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultiTexCoord4sv(GLenum Arg0, const GLshort * Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLoadTransposeMatrixf(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglLoadTransposeMatrixd(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultTransposeMatrixf(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglMultTransposeMatrixd(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFogCoordf(GLfloat Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFogCoordfv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFogCoordd(GLdouble Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFogCoorddv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglFogCoordPointer(GLenum Arg0, GLsizei Arg1, const GLvoid * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3b(GLbyte Arg0, GLbyte Arg1, GLbyte Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3bv(const GLbyte * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3dv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3fv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3iv(const GLint * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3sv(const GLshort * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3ub(GLubyte Arg0, GLubyte Arg1, GLubyte Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3ubv(const GLubyte * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3ui(GLuint Arg0, GLuint Arg1, GLuint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3uiv(const GLuint * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3us(GLushort Arg0, GLushort Arg1, GLushort Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColor3usv(const GLushort * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglSecondaryColorPointer(GLint Arg0, GLenum Arg1, GLsizei Arg2, const GLvoid * Arg3)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos2d(GLdouble Arg0, GLdouble Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos2dv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos2f(GLfloat Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos2fv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos2i(GLint Arg0, GLint Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos2iv(const GLint * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos2s(GLshort Arg0, GLshort Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos2sv(const GLshort * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos3dv(const GLdouble * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos3fv(const GLfloat * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos3iv(const GLint * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglUnsupportedCall();
}

static void APIENTRY
VirtGpuOglWindowPos3sv(const GLshort * Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
    VirtGpuOglUnsupportedCall();
}

static VOID APIENTRY
VirtGpuOglFinish(VOID)
{
}

static VOID APIENTRY
VirtGpuOglFlush(VOID)
{
}

static GLenum APIENTRY
VirtGpuOglGetError(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLenum Error;

    if (Context == NULL)
        return GL_INVALID_OPERATION;

    Error = Context->LastError;
    Context->LastError = GL_NO_ERROR;
    return Error;
}

static const GLubyte * APIENTRY
VirtGpuOglGetString(_In_ GLenum Name)
{
    static const GLubyte Vendor[] = "ReactOS";
    static const GLubyte Renderer[] = "VirtIO GPU transport ICD";
    static const GLubyte Version[] = "0.1 ReactOS VirtIO GPU transport scaffold";
    static const GLubyte Extensions[] = "";
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    switch (Name)
    {
        case GL_VENDOR:
            return Vendor;
        case GL_RENDERER:
            return Renderer;
        case GL_VERSION:
            return Version;
        case GL_EXTENSIONS:
            return Extensions;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return NULL;
    }
}

static VOID APIENTRY
VirtGpuOglGetBooleanv(_In_ GLenum Pname, _Out_ GLboolean *Params)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLbitfield Bit;

    if (Params == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Pname)
    {
        case GL_DOUBLEBUFFER:
            *Params = GL_FALSE;
            break;
        case GL_STEREO:
            *Params = GL_FALSE;
            break;
        case GL_DEPTH_WRITEMASK:
            *Params = (Context != NULL) ? Context->DepthMask : GL_TRUE;
            break;
        case GL_STENCIL_WRITEMASK:
            *Params = ((Context != NULL) && (Context->StencilMask != 0)) ?
                      GL_TRUE : GL_FALSE;
            break;
        case GL_COLOR_WRITEMASK:
            if (Context != NULL)
            {
                Params[0] = Context->ColorMask[0];
                Params[1] = Context->ColorMask[1];
                Params[2] = Context->ColorMask[2];
                Params[3] = Context->ColorMask[3];
            }
            break;
        default:
            if (Context == NULL)
            {
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            }
            else if (VirtGpuOglCapToBit(Pname, &Bit))
            {
                *Params = (Context->EnableBits & Bit) ? GL_TRUE : GL_FALSE;
            }
            else if (VirtGpuOglClientArrayCapToBit(Pname, &Bit))
            {
                *Params = (Context->ClientArrayBits & Bit) ? GL_TRUE : GL_FALSE;
            }
            else
            {
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            }
            break;
    }
}

static VOID APIENTRY
VirtGpuOglGetIntegerv(_In_ GLenum Pname, _Out_ GLint *Params)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLbitfield Bit;

    if (Params == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Pname)
    {
        case GL_MAX_TEXTURE_SIZE:
            *Params = 4096;
            break;
        case GL_MAX_MODELVIEW_STACK_DEPTH:
            *Params = VIRTGPU_OGL_MODELVIEW_STACK_DEPTH;
            break;
        case GL_MAX_PROJECTION_STACK_DEPTH:
            *Params = VIRTGPU_OGL_PROJECTION_STACK_DEPTH;
            break;
        case GL_MAX_TEXTURE_STACK_DEPTH:
            *Params = VIRTGPU_OGL_TEXTURE_STACK_DEPTH;
            break;
        case GL_MAX_VIEWPORT_DIMS:
            Params[0] = 16384;
            Params[1] = 16384;
            break;
        case GL_RED_BITS:
        case GL_GREEN_BITS:
        case GL_BLUE_BITS:
        case GL_ALPHA_BITS:
            *Params = 8;
            break;
        case GL_DEPTH_BITS:
            *Params = 0;
            break;
        case GL_STENCIL_BITS:
            *Params = 0;
            break;
        case GL_VIEWPORT:
            if (Context != NULL)
                CopyMemory(Params, Context->Viewport, sizeof(Context->Viewport));
            break;
        case GL_SCISSOR_BOX:
            if (Context != NULL)
                CopyMemory(Params, Context->ScissorBox, sizeof(Context->ScissorBox));
            break;
        case GL_MATRIX_MODE:
            *Params = (Context != NULL) ? (GLint)Context->MatrixMode : GL_MODELVIEW;
            break;
        case GL_MODELVIEW_STACK_DEPTH:
            *Params = (Context != NULL) ? (GLint)(Context->ModelViewStackTop + 1) : 1;
            break;
        case GL_PROJECTION_STACK_DEPTH:
            *Params = (Context != NULL) ? (GLint)(Context->ProjectionStackTop + 1) : 1;
            break;
        case GL_TEXTURE_STACK_DEPTH:
            *Params = (Context != NULL) ? (GLint)(Context->TextureStackTop + 1) : 1;
            break;
        case GL_VERTEX_ARRAY_SIZE:
            *Params = (Context != NULL) ? Context->VertexArraySize : 4;
            break;
        case GL_VERTEX_ARRAY_TYPE:
            *Params = (Context != NULL) ? (GLint)Context->VertexArrayType : GL_FLOAT;
            break;
        case GL_VERTEX_ARRAY_STRIDE:
            *Params = (Context != NULL) ? Context->VertexArrayStride : 0;
            break;
        case GL_COLOR_ARRAY_SIZE:
            *Params = (Context != NULL) ? Context->ColorArraySize : 4;
            break;
        case GL_COLOR_ARRAY_TYPE:
            *Params = (Context != NULL) ? (GLint)Context->ColorArrayType : GL_FLOAT;
            break;
        case GL_COLOR_ARRAY_STRIDE:
            *Params = (Context != NULL) ? Context->ColorArrayStride : 0;
            break;
        case GL_NORMAL_ARRAY_TYPE:
            *Params = (Context != NULL) ? (GLint)Context->NormalArrayType : GL_FLOAT;
            break;
        case GL_NORMAL_ARRAY_STRIDE:
            *Params = (Context != NULL) ? Context->NormalArrayStride : 0;
            break;
        case GL_TEXTURE_COORD_ARRAY_SIZE:
            *Params = (Context != NULL) ? Context->TexCoordArraySize : 4;
            break;
        case GL_TEXTURE_COORD_ARRAY_TYPE:
            *Params = (Context != NULL) ? (GLint)Context->TexCoordArrayType : GL_FLOAT;
            break;
        case GL_TEXTURE_COORD_ARRAY_STRIDE:
            *Params = (Context != NULL) ? Context->TexCoordArrayStride : 0;
            break;
        case GL_DRAW_BUFFER:
            *Params = (Context != NULL) ? (GLint)Context->DrawBuffer : GL_FRONT;
            break;
        case GL_READ_BUFFER:
            *Params = (Context != NULL) ? (GLint)Context->ReadBuffer : GL_FRONT;
            break;
        case GL_DEPTH_FUNC:
            *Params = (Context != NULL) ? (GLint)Context->DepthFunc : GL_LESS;
            break;
        case GL_ALPHA_TEST_FUNC:
            *Params = (Context != NULL) ? (GLint)Context->AlphaFunc : GL_ALWAYS;
            break;
        case GL_BLEND_SRC:
            *Params = (Context != NULL) ? (GLint)Context->BlendSrcFactor : GL_ONE;
            break;
        case GL_BLEND_DST:
            *Params = (Context != NULL) ? (GLint)Context->BlendDstFactor : GL_ZERO;
            break;
        case GL_CULL_FACE_MODE:
            *Params = (Context != NULL) ? (GLint)Context->CullFaceMode : GL_BACK;
            break;
        case GL_FRONT_FACE:
            *Params = (Context != NULL) ? (GLint)Context->FrontFace : GL_CCW;
            break;
        case GL_SHADE_MODEL:
            *Params = (Context != NULL) ? (GLint)Context->ShadeModel : GL_SMOOTH;
            break;
        case GL_POLYGON_MODE:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->PolygonMode[0];
                Params[1] = (GLint)Context->PolygonMode[1];
            }
            break;
        case GL_DEPTH_WRITEMASK:
            *Params = ((Context != NULL) && (Context->DepthMask == GL_FALSE)) ? 0 : 1;
            break;
        case GL_COLOR_WRITEMASK:
            if (Context != NULL)
            {
                Params[0] = Context->ColorMask[0] == GL_TRUE;
                Params[1] = Context->ColorMask[1] == GL_TRUE;
                Params[2] = Context->ColorMask[2] == GL_TRUE;
                Params[3] = Context->ColorMask[3] == GL_TRUE;
            }
            break;
        case GL_CURRENT_COLOR:
            if (Context != NULL)
            {
                Params[0] = GetRValue(Context->CurrentColor);
                Params[1] = GetGValue(Context->CurrentColor);
                Params[2] = GetBValue(Context->CurrentColor);
                Params[3] = 255;
            }
            break;
        case GL_CURRENT_NORMAL:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->CurrentNormal[0];
                Params[1] = (GLint)Context->CurrentNormal[1];
                Params[2] = (GLint)Context->CurrentNormal[2];
            }
            break;
        case GL_CURRENT_TEXTURE_COORDS:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->CurrentTexCoord[0];
                Params[1] = (GLint)Context->CurrentTexCoord[1];
                Params[2] = (GLint)Context->CurrentTexCoord[2];
                Params[3] = (GLint)Context->CurrentTexCoord[3];
            }
            break;
        case GL_COLOR_CLEAR_VALUE:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->ClearColor[0];
                Params[1] = (GLint)Context->ClearColor[1];
                Params[2] = (GLint)Context->ClearColor[2];
                Params[3] = (GLint)Context->ClearColor[3];
            }
            break;
        case GL_DEPTH_CLEAR_VALUE:
            *Params = (Context != NULL) ? (GLint)Context->ClearDepth : 1;
            break;
        case GL_STENCIL_CLEAR_VALUE:
            *Params = (Context != NULL) ? Context->ClearStencil : 0;
            break;
        case GL_STENCIL_WRITEMASK:
            *Params = (Context != NULL) ? (GLint)Context->StencilMask : -1;
            break;
        case GL_STENCIL_FUNC:
            *Params = (Context != NULL) ? (GLint)Context->StencilFunc : GL_ALWAYS;
            break;
        case GL_STENCIL_REF:
            *Params = (Context != NULL) ? Context->StencilRef : 0;
            break;
        case GL_STENCIL_VALUE_MASK:
            *Params = (Context != NULL) ? (GLint)Context->StencilValueMask : -1;
            break;
        case GL_STENCIL_FAIL:
            *Params = (Context != NULL) ? (GLint)Context->StencilFail : GL_KEEP;
            break;
        case GL_STENCIL_PASS_DEPTH_FAIL:
            *Params = (Context != NULL) ? (GLint)Context->StencilDepthFail : GL_KEEP;
            break;
        case GL_STENCIL_PASS_DEPTH_PASS:
            *Params = (Context != NULL) ? (GLint)Context->StencilDepthPass : GL_KEEP;
            break;
        case GL_PACK_ALIGNMENT:
            *Params = (Context != NULL) ? Context->PackAlignment : 4;
            break;
        case GL_UNPACK_ALIGNMENT:
            *Params = (Context != NULL) ? Context->UnpackAlignment : 4;
            break;
        default:
            if (Context == NULL)
            {
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            }
            else if (VirtGpuOglCapToBit(Pname, &Bit))
            {
                *Params = (Context->EnableBits & Bit) ? 1 : 0;
            }
            else if (VirtGpuOglClientArrayCapToBit(Pname, &Bit))
            {
                *Params = (Context->ClientArrayBits & Bit) ? 1 : 0;
            }
            else
            {
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            }
            break;
    }
}

static VOID APIENTRY
VirtGpuOglGetFloatv(_In_ GLenum Pname, _Out_ GLfloat *Params)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLint Values[16] = { 0 };
    ULONG Index;
    ULONG Count;

    if (Params == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Context != NULL) && (Pname == GL_COLOR_CLEAR_VALUE))
    {
        for (Index = 0; Index < 4; ++Index)
            Params[Index] = Context->ClearColor[Index];
        return;
    }

    if ((Context != NULL) && (Pname == GL_CURRENT_COLOR))
    {
        Params[0] = (GLfloat)GetRValue(Context->CurrentColor) / 255.0f;
        Params[1] = (GLfloat)GetGValue(Context->CurrentColor) / 255.0f;
        Params[2] = (GLfloat)GetBValue(Context->CurrentColor) / 255.0f;
        Params[3] = 1.0f;
        return;
    }

    if ((Context != NULL) && (Pname == GL_CURRENT_NORMAL))
    {
        Params[0] = Context->CurrentNormal[0];
        Params[1] = Context->CurrentNormal[1];
        Params[2] = Context->CurrentNormal[2];
        return;
    }

    if ((Context != NULL) && (Pname == GL_CURRENT_TEXTURE_COORDS))
    {
        Params[0] = Context->CurrentTexCoord[0];
        Params[1] = Context->CurrentTexCoord[1];
        Params[2] = Context->CurrentTexCoord[2];
        Params[3] = Context->CurrentTexCoord[3];
        return;
    }

    if (Context != NULL)
    {
        switch (Pname)
        {
            case GL_MODELVIEW_MATRIX:
                CopyMemory(Params,
                           Context->ModelViewStack[Context->ModelViewStackTop],
                           16 * sizeof(GLfloat));
                return;
            case GL_PROJECTION_MATRIX:
                CopyMemory(Params,
                           Context->ProjectionStack[Context->ProjectionStackTop],
                           16 * sizeof(GLfloat));
                return;
            case GL_TEXTURE_MATRIX:
                CopyMemory(Params,
                           Context->TextureStack[Context->TextureStackTop],
                           16 * sizeof(GLfloat));
                return;
            case GL_DEPTH_RANGE:
                Params[0] = (GLfloat)Context->DepthRange[0];
                Params[1] = (GLfloat)Context->DepthRange[1];
                return;
            case GL_POINT_SIZE:
                Params[0] = Context->PointSize;
                return;
            case GL_LINE_WIDTH:
                Params[0] = Context->LineWidth;
                return;
            case GL_ALPHA_TEST_REF:
                Params[0] = Context->AlphaRef;
                return;
        }
    }

    if (Pname == GL_DEPTH_CLEAR_VALUE)
    {
        Params[0] = (Context != NULL) ? (GLfloat)Context->ClearDepth : 1.0f;
        return;
    }

    VirtGpuOglGetIntegerv(Pname, Values);
    Count = VirtGpuOglGetValueCount(Pname);
    for (Index = 0; Index < Count; ++Index)
        Params[Index] = (GLfloat)Values[Index];
}

static VOID APIENTRY
VirtGpuOglGetDoublev(_In_ GLenum Pname, _Out_ GLdouble *Params)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLfloat Values[16] = { 0.0f };
    ULONG Index;
    ULONG Count;

    if (Params == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetFloatv(Pname, Values);
    Count = VirtGpuOglGetValueCount(Pname);
    for (Index = 0; Index < Count; ++Index)
        Params[Index] = (GLdouble)Values[Index];
}

static GLboolean APIENTRY
VirtGpuOglIsEnabled(_In_ GLenum Cap)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLbitfield Bit;

    if ((Context == NULL) || !VirtGpuOglCapToBit(Cap, &Bit))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return GL_FALSE;
    }

    return (Context->EnableBits & Bit) ? GL_TRUE : GL_FALSE;
}
static const PROC VirtGpuOglDispatchEntries[VIRTGPU_OPENGL_ENTRY_COUNT] =
{
    (PROC)VirtGpuOglNewList,
    (PROC)VirtGpuOglEndList,
    (PROC)VirtGpuOglCallList,
    (PROC)VirtGpuOglCallLists,
    (PROC)VirtGpuOglDeleteLists,
    (PROC)VirtGpuOglGenLists,
    (PROC)VirtGpuOglListBase,
    (PROC)VirtGpuOglBegin,
    (PROC)VirtGpuOglBitmap,
    (PROC)VirtGpuOglColor3b,
    (PROC)VirtGpuOglColor3bv,
    (PROC)VirtGpuOglColor3d,
    (PROC)VirtGpuOglColor3dv,
    (PROC)VirtGpuOglColor3f,
    (PROC)VirtGpuOglColor3fv,
    (PROC)VirtGpuOglColor3i,
    (PROC)VirtGpuOglColor3iv,
    (PROC)VirtGpuOglColor3s,
    (PROC)VirtGpuOglColor3sv,
    (PROC)VirtGpuOglColor3ub,
    (PROC)VirtGpuOglColor3ubv,
    (PROC)VirtGpuOglColor3ui,
    (PROC)VirtGpuOglColor3uiv,
    (PROC)VirtGpuOglColor3us,
    (PROC)VirtGpuOglColor3usv,
    (PROC)VirtGpuOglColor4b,
    (PROC)VirtGpuOglColor4bv,
    (PROC)VirtGpuOglColor4d,
    (PROC)VirtGpuOglColor4dv,
    (PROC)VirtGpuOglColor4f,
    (PROC)VirtGpuOglColor4fv,
    (PROC)VirtGpuOglColor4i,
    (PROC)VirtGpuOglColor4iv,
    (PROC)VirtGpuOglColor4s,
    (PROC)VirtGpuOglColor4sv,
    (PROC)VirtGpuOglColor4ub,
    (PROC)VirtGpuOglColor4ubv,
    (PROC)VirtGpuOglColor4ui,
    (PROC)VirtGpuOglColor4uiv,
    (PROC)VirtGpuOglColor4us,
    (PROC)VirtGpuOglColor4usv,
    (PROC)VirtGpuOglEdgeFlag,
    (PROC)VirtGpuOglEdgeFlagv,
    (PROC)VirtGpuOglEnd,
    (PROC)VirtGpuOglIndexd,
    (PROC)VirtGpuOglIndexdv,
    (PROC)VirtGpuOglIndexf,
    (PROC)VirtGpuOglIndexfv,
    (PROC)VirtGpuOglIndexi,
    (PROC)VirtGpuOglIndexiv,
    (PROC)VirtGpuOglIndexs,
    (PROC)VirtGpuOglIndexsv,
    (PROC)VirtGpuOglNormal3b,
    (PROC)VirtGpuOglNormal3bv,
    (PROC)VirtGpuOglNormal3d,
    (PROC)VirtGpuOglNormal3dv,
    (PROC)VirtGpuOglNormal3f,
    (PROC)VirtGpuOglNormal3fv,
    (PROC)VirtGpuOglNormal3i,
    (PROC)VirtGpuOglNormal3iv,
    (PROC)VirtGpuOglNormal3s,
    (PROC)VirtGpuOglNormal3sv,
    (PROC)VirtGpuOglRasterPos2d,
    (PROC)VirtGpuOglRasterPos2dv,
    (PROC)VirtGpuOglRasterPos2f,
    (PROC)VirtGpuOglRasterPos2fv,
    (PROC)VirtGpuOglRasterPos2i,
    (PROC)VirtGpuOglRasterPos2iv,
    (PROC)VirtGpuOglRasterPos2s,
    (PROC)VirtGpuOglRasterPos2sv,
    (PROC)VirtGpuOglRasterPos3d,
    (PROC)VirtGpuOglRasterPos3dv,
    (PROC)VirtGpuOglRasterPos3f,
    (PROC)VirtGpuOglRasterPos3fv,
    (PROC)VirtGpuOglRasterPos3i,
    (PROC)VirtGpuOglRasterPos3iv,
    (PROC)VirtGpuOglRasterPos3s,
    (PROC)VirtGpuOglRasterPos3sv,
    (PROC)VirtGpuOglRasterPos4d,
    (PROC)VirtGpuOglRasterPos4dv,
    (PROC)VirtGpuOglRasterPos4f,
    (PROC)VirtGpuOglRasterPos4fv,
    (PROC)VirtGpuOglRasterPos4i,
    (PROC)VirtGpuOglRasterPos4iv,
    (PROC)VirtGpuOglRasterPos4s,
    (PROC)VirtGpuOglRasterPos4sv,
    (PROC)VirtGpuOglRectd,
    (PROC)VirtGpuOglRectdv,
    (PROC)VirtGpuOglRectf,
    (PROC)VirtGpuOglRectfv,
    (PROC)VirtGpuOglRecti,
    (PROC)VirtGpuOglRectiv,
    (PROC)VirtGpuOglRects,
    (PROC)VirtGpuOglRectsv,
    (PROC)VirtGpuOglTexCoord1d,
    (PROC)VirtGpuOglTexCoord1dv,
    (PROC)VirtGpuOglTexCoord1f,
    (PROC)VirtGpuOglTexCoord1fv,
    (PROC)VirtGpuOglTexCoord1i,
    (PROC)VirtGpuOglTexCoord1iv,
    (PROC)VirtGpuOglTexCoord1s,
    (PROC)VirtGpuOglTexCoord1sv,
    (PROC)VirtGpuOglTexCoord2d,
    (PROC)VirtGpuOglTexCoord2dv,
    (PROC)VirtGpuOglTexCoord2f,
    (PROC)VirtGpuOglTexCoord2fv,
    (PROC)VirtGpuOglTexCoord2i,
    (PROC)VirtGpuOglTexCoord2iv,
    (PROC)VirtGpuOglTexCoord2s,
    (PROC)VirtGpuOglTexCoord2sv,
    (PROC)VirtGpuOglTexCoord3d,
    (PROC)VirtGpuOglTexCoord3dv,
    (PROC)VirtGpuOglTexCoord3f,
    (PROC)VirtGpuOglTexCoord3fv,
    (PROC)VirtGpuOglTexCoord3i,
    (PROC)VirtGpuOglTexCoord3iv,
    (PROC)VirtGpuOglTexCoord3s,
    (PROC)VirtGpuOglTexCoord3sv,
    (PROC)VirtGpuOglTexCoord4d,
    (PROC)VirtGpuOglTexCoord4dv,
    (PROC)VirtGpuOglTexCoord4f,
    (PROC)VirtGpuOglTexCoord4fv,
    (PROC)VirtGpuOglTexCoord4i,
    (PROC)VirtGpuOglTexCoord4iv,
    (PROC)VirtGpuOglTexCoord4s,
    (PROC)VirtGpuOglTexCoord4sv,
    (PROC)VirtGpuOglVertex2d,
    (PROC)VirtGpuOglVertex2dv,
    (PROC)VirtGpuOglVertex2f,
    (PROC)VirtGpuOglVertex2fv,
    (PROC)VirtGpuOglVertex2i,
    (PROC)VirtGpuOglVertex2iv,
    (PROC)VirtGpuOglVertex2s,
    (PROC)VirtGpuOglVertex2sv,
    (PROC)VirtGpuOglVertex3d,
    (PROC)VirtGpuOglVertex3dv,
    (PROC)VirtGpuOglVertex3f,
    (PROC)VirtGpuOglVertex3fv,
    (PROC)VirtGpuOglVertex3i,
    (PROC)VirtGpuOglVertex3iv,
    (PROC)VirtGpuOglVertex3s,
    (PROC)VirtGpuOglVertex3sv,
    (PROC)VirtGpuOglVertex4d,
    (PROC)VirtGpuOglVertex4dv,
    (PROC)VirtGpuOglVertex4f,
    (PROC)VirtGpuOglVertex4fv,
    (PROC)VirtGpuOglVertex4i,
    (PROC)VirtGpuOglVertex4iv,
    (PROC)VirtGpuOglVertex4s,
    (PROC)VirtGpuOglVertex4sv,
    (PROC)VirtGpuOglClipPlane,
    (PROC)VirtGpuOglColorMaterial,
    (PROC)VirtGpuOglCullFace,
    (PROC)VirtGpuOglFogf,
    (PROC)VirtGpuOglFogfv,
    (PROC)VirtGpuOglFogi,
    (PROC)VirtGpuOglFogiv,
    (PROC)VirtGpuOglFrontFace,
    (PROC)VirtGpuOglHint,
    (PROC)VirtGpuOglLightf,
    (PROC)VirtGpuOglLightfv,
    (PROC)VirtGpuOglLighti,
    (PROC)VirtGpuOglLightiv,
    (PROC)VirtGpuOglLightModelf,
    (PROC)VirtGpuOglLightModelfv,
    (PROC)VirtGpuOglLightModeli,
    (PROC)VirtGpuOglLightModeliv,
    (PROC)VirtGpuOglLineStipple,
    (PROC)VirtGpuOglLineWidth,
    (PROC)VirtGpuOglMaterialf,
    (PROC)VirtGpuOglMaterialfv,
    (PROC)VirtGpuOglMateriali,
    (PROC)VirtGpuOglMaterialiv,
    (PROC)VirtGpuOglPointSize,
    (PROC)VirtGpuOglPolygonMode,
    (PROC)VirtGpuOglPolygonStipple,
    (PROC)VirtGpuOglScissor,
    (PROC)VirtGpuOglShadeModel,
    (PROC)VirtGpuOglTexParameterf,
    (PROC)VirtGpuOglTexParameterfv,
    (PROC)VirtGpuOglTexParameteri,
    (PROC)VirtGpuOglTexParameteriv,
    (PROC)VirtGpuOglTexImage1D,
    (PROC)VirtGpuOglTexImage2D,
    (PROC)VirtGpuOglTexEnvf,
    (PROC)VirtGpuOglTexEnvfv,
    (PROC)VirtGpuOglTexEnvi,
    (PROC)VirtGpuOglTexEnviv,
    (PROC)VirtGpuOglTexGend,
    (PROC)VirtGpuOglTexGendv,
    (PROC)VirtGpuOglTexGenf,
    (PROC)VirtGpuOglTexGenfv,
    (PROC)VirtGpuOglTexGeni,
    (PROC)VirtGpuOglTexGeniv,
    (PROC)VirtGpuOglFeedbackBuffer,
    (PROC)VirtGpuOglSelectBuffer,
    (PROC)VirtGpuOglRenderMode,
    (PROC)VirtGpuOglInitNames,
    (PROC)VirtGpuOglLoadName,
    (PROC)VirtGpuOglPassThrough,
    (PROC)VirtGpuOglPopName,
    (PROC)VirtGpuOglPushName,
    (PROC)VirtGpuOglDrawBuffer,
    (PROC)VirtGpuOglClear,
    (PROC)VirtGpuOglClearAccum,
    (PROC)VirtGpuOglClearIndex,
    (PROC)VirtGpuOglClearColor,
    (PROC)VirtGpuOglClearStencil,
    (PROC)VirtGpuOglClearDepth,
    (PROC)VirtGpuOglStencilMask,
    (PROC)VirtGpuOglColorMask,
    (PROC)VirtGpuOglDepthMask,
    (PROC)VirtGpuOglIndexMask,
    (PROC)VirtGpuOglAccum,
    (PROC)VirtGpuOglDisable,
    (PROC)VirtGpuOglEnable,
    (PROC)VirtGpuOglFinish,
    (PROC)VirtGpuOglFlush,
    (PROC)VirtGpuOglPopAttrib,
    (PROC)VirtGpuOglPushAttrib,
    (PROC)VirtGpuOglMap1d,
    (PROC)VirtGpuOglMap1f,
    (PROC)VirtGpuOglMap2d,
    (PROC)VirtGpuOglMap2f,
    (PROC)VirtGpuOglMapGrid1d,
    (PROC)VirtGpuOglMapGrid1f,
    (PROC)VirtGpuOglMapGrid2d,
    (PROC)VirtGpuOglMapGrid2f,
    (PROC)VirtGpuOglEvalCoord1d,
    (PROC)VirtGpuOglEvalCoord1dv,
    (PROC)VirtGpuOglEvalCoord1f,
    (PROC)VirtGpuOglEvalCoord1fv,
    (PROC)VirtGpuOglEvalCoord2d,
    (PROC)VirtGpuOglEvalCoord2dv,
    (PROC)VirtGpuOglEvalCoord2f,
    (PROC)VirtGpuOglEvalCoord2fv,
    (PROC)VirtGpuOglEvalMesh1,
    (PROC)VirtGpuOglEvalPoint1,
    (PROC)VirtGpuOglEvalMesh2,
    (PROC)VirtGpuOglEvalPoint2,
    (PROC)VirtGpuOglAlphaFunc,
    (PROC)VirtGpuOglBlendFunc,
    (PROC)VirtGpuOglLogicOp,
    (PROC)VirtGpuOglStencilFunc,
    (PROC)VirtGpuOglStencilOp,
    (PROC)VirtGpuOglDepthFunc,
    (PROC)VirtGpuOglPixelZoom,
    (PROC)VirtGpuOglPixelTransferf,
    (PROC)VirtGpuOglPixelTransferi,
    (PROC)VirtGpuOglPixelStoref,
    (PROC)VirtGpuOglPixelStorei,
    (PROC)VirtGpuOglPixelMapfv,
    (PROC)VirtGpuOglPixelMapuiv,
    (PROC)VirtGpuOglPixelMapusv,
    (PROC)VirtGpuOglReadBuffer,
    (PROC)VirtGpuOglCopyPixels,
    (PROC)VirtGpuOglReadPixels,
    (PROC)VirtGpuOglDrawPixels,
    (PROC)VirtGpuOglGetBooleanv,
    (PROC)VirtGpuOglGetClipPlane,
    (PROC)VirtGpuOglGetDoublev,
    (PROC)VirtGpuOglGetError,
    (PROC)VirtGpuOglGetFloatv,
    (PROC)VirtGpuOglGetIntegerv,
    (PROC)VirtGpuOglGetLightfv,
    (PROC)VirtGpuOglGetLightiv,
    (PROC)VirtGpuOglGetMapdv,
    (PROC)VirtGpuOglGetMapfv,
    (PROC)VirtGpuOglGetMapiv,
    (PROC)VirtGpuOglGetMaterialfv,
    (PROC)VirtGpuOglGetMaterialiv,
    (PROC)VirtGpuOglGetPixelMapfv,
    (PROC)VirtGpuOglGetPixelMapuiv,
    (PROC)VirtGpuOglGetPixelMapusv,
    (PROC)VirtGpuOglGetPolygonStipple,
    (PROC)VirtGpuOglGetString,
    (PROC)VirtGpuOglGetTexEnvfv,
    (PROC)VirtGpuOglGetTexEnviv,
    (PROC)VirtGpuOglGetTexGendv,
    (PROC)VirtGpuOglGetTexGenfv,
    (PROC)VirtGpuOglGetTexGeniv,
    (PROC)VirtGpuOglGetTexImage,
    (PROC)VirtGpuOglGetTexParameterfv,
    (PROC)VirtGpuOglGetTexParameteriv,
    (PROC)VirtGpuOglGetTexLevelParameterfv,
    (PROC)VirtGpuOglGetTexLevelParameteriv,
    (PROC)VirtGpuOglIsEnabled,
    (PROC)VirtGpuOglIsList,
    (PROC)VirtGpuOglDepthRange,
    (PROC)VirtGpuOglFrustum,
    (PROC)VirtGpuOglLoadIdentity,
    (PROC)VirtGpuOglLoadMatrixf,
    (PROC)VirtGpuOglLoadMatrixd,
    (PROC)VirtGpuOglMatrixMode,
    (PROC)VirtGpuOglMultMatrixf,
    (PROC)VirtGpuOglMultMatrixd,
    (PROC)VirtGpuOglOrtho,
    (PROC)VirtGpuOglPopMatrix,
    (PROC)VirtGpuOglPushMatrix,
    (PROC)VirtGpuOglRotated,
    (PROC)VirtGpuOglRotatef,
    (PROC)VirtGpuOglScaled,
    (PROC)VirtGpuOglScalef,
    (PROC)VirtGpuOglTranslated,
    (PROC)VirtGpuOglTranslatef,
    (PROC)VirtGpuOglViewport,
    (PROC)VirtGpuOglArrayElement,
    (PROC)VirtGpuOglBindTexture,
    (PROC)VirtGpuOglColorPointer,
    (PROC)VirtGpuOglDisableClientState,
    (PROC)VirtGpuOglDrawArrays,
    (PROC)VirtGpuOglDrawElements,
    (PROC)VirtGpuOglEdgeFlagPointer,
    (PROC)VirtGpuOglEnableClientState,
    (PROC)VirtGpuOglIndexPointer,
    (PROC)VirtGpuOglIndexub,
    (PROC)VirtGpuOglIndexubv,
    (PROC)VirtGpuOglInterleavedArrays,
    (PROC)VirtGpuOglNormalPointer,
    (PROC)VirtGpuOglPolygonOffset,
    (PROC)VirtGpuOglTexCoordPointer,
    (PROC)VirtGpuOglVertexPointer,
    (PROC)VirtGpuOglAreTexturesResident,
    (PROC)VirtGpuOglCopyTexImage1D,
    (PROC)VirtGpuOglCopyTexImage2D,
    (PROC)VirtGpuOglCopyTexSubImage1D,
    (PROC)VirtGpuOglCopyTexSubImage2D,
    (PROC)VirtGpuOglDeleteTextures,
    (PROC)VirtGpuOglGenTextures,
    (PROC)VirtGpuOglGetPointerv,
    (PROC)VirtGpuOglIsTexture,
    (PROC)VirtGpuOglPrioritizeTextures,
    (PROC)VirtGpuOglTexSubImage1D,
    (PROC)VirtGpuOglTexSubImage2D,
    (PROC)VirtGpuOglPopClientAttrib,
    (PROC)VirtGpuOglPushClientAttrib,
};
typedef struct _VIRTGPU_OGL_NAMED_PROC
{
    LPCSTR Name;
    PROC Proc;
} VIRTGPU_OGL_NAMED_PROC, *PVIRTGPU_OGL_NAMED_PROC;

static const VIRTGPU_OGL_NAMED_PROC VirtGpuOglCoreProcTable[] =
{
    { "glDrawRangeElements", (PROC)VirtGpuOglDrawRangeElements },
    { "glTexImage3D", (PROC)VirtGpuOglTexImage3D },
    { "glTexSubImage3D", (PROC)VirtGpuOglTexSubImage3D },
    { "glCopyTexSubImage3D", (PROC)VirtGpuOglCopyTexSubImage3D },
    { "glActiveTexture", (PROC)VirtGpuOglActiveTexture },
    { "glSampleCoverage", (PROC)VirtGpuOglSampleCoverage },
    { "glCompressedTexImage3D", (PROC)VirtGpuOglCompressedTexImage3D },
    { "glCompressedTexImage2D", (PROC)VirtGpuOglCompressedTexImage2D },
    { "glCompressedTexImage1D", (PROC)VirtGpuOglCompressedTexImage1D },
    { "glCompressedTexSubImage3D", (PROC)VirtGpuOglCompressedTexSubImage3D },
    { "glCompressedTexSubImage2D", (PROC)VirtGpuOglCompressedTexSubImage2D },
    { "glCompressedTexSubImage1D", (PROC)VirtGpuOglCompressedTexSubImage1D },
    { "glGetCompressedTexImage", (PROC)VirtGpuOglGetCompressedTexImage },
    { "glBlendFuncSeparate", (PROC)VirtGpuOglBlendFuncSeparate },
    { "glMultiDrawArrays", (PROC)VirtGpuOglMultiDrawArrays },
    { "glMultiDrawElements", (PROC)VirtGpuOglMultiDrawElements },
    { "glPointParameterf", (PROC)VirtGpuOglPointParameterf },
    { "glPointParameterfv", (PROC)VirtGpuOglPointParameterfv },
    { "glPointParameteri", (PROC)VirtGpuOglPointParameteri },
    { "glPointParameteriv", (PROC)VirtGpuOglPointParameteriv },
    { "glBlendColor", (PROC)VirtGpuOglBlendColor },
    { "glBlendEquation", (PROC)VirtGpuOglBlendEquation },
    { "glGenQueries", (PROC)VirtGpuOglGenQueries },
    { "glDeleteQueries", (PROC)VirtGpuOglDeleteQueries },
    { "glIsQuery", (PROC)VirtGpuOglIsQuery },
    { "glBeginQuery", (PROC)VirtGpuOglBeginQuery },
    { "glEndQuery", (PROC)VirtGpuOglEndQuery },
    { "glGetQueryiv", (PROC)VirtGpuOglGetQueryiv },
    { "glGetQueryObjectiv", (PROC)VirtGpuOglGetQueryObjectiv },
    { "glGetQueryObjectuiv", (PROC)VirtGpuOglGetQueryObjectuiv },
    { "glBindBuffer", (PROC)VirtGpuOglBindBuffer },
    { "glDeleteBuffers", (PROC)VirtGpuOglDeleteBuffers },
    { "glGenBuffers", (PROC)VirtGpuOglGenBuffers },
    { "glIsBuffer", (PROC)VirtGpuOglIsBuffer },
    { "glBufferData", (PROC)VirtGpuOglBufferData },
    { "glBufferSubData", (PROC)VirtGpuOglBufferSubData },
    { "glGetBufferSubData", (PROC)VirtGpuOglGetBufferSubData },
    { "glMapBuffer", (PROC)VirtGpuOglMapBuffer },
    { "glUnmapBuffer", (PROC)VirtGpuOglUnmapBuffer },
    { "glGetBufferParameteriv", (PROC)VirtGpuOglGetBufferParameteriv },
    { "glGetBufferPointerv", (PROC)VirtGpuOglGetBufferPointerv },
    { "glBlendEquationSeparate", (PROC)VirtGpuOglBlendEquationSeparate },
    { "glDrawBuffers", (PROC)VirtGpuOglDrawBuffers },
    { "glStencilOpSeparate", (PROC)VirtGpuOglStencilOpSeparate },
    { "glStencilFuncSeparate", (PROC)VirtGpuOglStencilFuncSeparate },
    { "glStencilMaskSeparate", (PROC)VirtGpuOglStencilMaskSeparate },
    { "glAttachShader", (PROC)VirtGpuOglAttachShader },
    { "glBindAttribLocation", (PROC)VirtGpuOglBindAttribLocation },
    { "glCompileShader", (PROC)VirtGpuOglCompileShader },
    { "glCreateProgram", (PROC)VirtGpuOglCreateProgram },
    { "glCreateShader", (PROC)VirtGpuOglCreateShader },
    { "glDeleteProgram", (PROC)VirtGpuOglDeleteProgram },
    { "glDeleteShader", (PROC)VirtGpuOglDeleteShader },
    { "glDetachShader", (PROC)VirtGpuOglDetachShader },
    { "glDisableVertexAttribArray", (PROC)VirtGpuOglDisableVertexAttribArray },
    { "glEnableVertexAttribArray", (PROC)VirtGpuOglEnableVertexAttribArray },
    { "glGetActiveAttrib", (PROC)VirtGpuOglGetActiveAttrib },
    { "glGetActiveUniform", (PROC)VirtGpuOglGetActiveUniform },
    { "glGetAttachedShaders", (PROC)VirtGpuOglGetAttachedShaders },
    { "glGetAttribLocation", (PROC)VirtGpuOglGetAttribLocation },
    { "glGetProgramiv", (PROC)VirtGpuOglGetProgramiv },
    { "glGetProgramInfoLog", (PROC)VirtGpuOglGetProgramInfoLog },
    { "glGetShaderiv", (PROC)VirtGpuOglGetShaderiv },
    { "glGetShaderInfoLog", (PROC)VirtGpuOglGetShaderInfoLog },
    { "glGetShaderSource", (PROC)VirtGpuOglGetShaderSource },
    { "glGetUniformLocation", (PROC)VirtGpuOglGetUniformLocation },
    { "glGetUniformfv", (PROC)VirtGpuOglGetUniformfv },
    { "glGetUniformiv", (PROC)VirtGpuOglGetUniformiv },
    { "glGetVertexAttribdv", (PROC)VirtGpuOglGetVertexAttribdv },
    { "glGetVertexAttribfv", (PROC)VirtGpuOglGetVertexAttribfv },
    { "glGetVertexAttribiv", (PROC)VirtGpuOglGetVertexAttribiv },
    { "glGetVertexAttribPointerv", (PROC)VirtGpuOglGetVertexAttribPointerv },
    { "glIsProgram", (PROC)VirtGpuOglIsProgram },
    { "glIsShader", (PROC)VirtGpuOglIsShader },
    { "glLinkProgram", (PROC)VirtGpuOglLinkProgram },
    { "glShaderSource", (PROC)VirtGpuOglShaderSource },
    { "glUseProgram", (PROC)VirtGpuOglUseProgram },
    { "glUniform1f", (PROC)VirtGpuOglUniform1f },
    { "glUniform2f", (PROC)VirtGpuOglUniform2f },
    { "glUniform3f", (PROC)VirtGpuOglUniform3f },
    { "glUniform4f", (PROC)VirtGpuOglUniform4f },
    { "glUniform1i", (PROC)VirtGpuOglUniform1i },
    { "glUniform2i", (PROC)VirtGpuOglUniform2i },
    { "glUniform3i", (PROC)VirtGpuOglUniform3i },
    { "glUniform4i", (PROC)VirtGpuOglUniform4i },
    { "glUniform1fv", (PROC)VirtGpuOglUniform1fv },
    { "glUniform2fv", (PROC)VirtGpuOglUniform2fv },
    { "glUniform3fv", (PROC)VirtGpuOglUniform3fv },
    { "glUniform4fv", (PROC)VirtGpuOglUniform4fv },
    { "glUniform1iv", (PROC)VirtGpuOglUniform1iv },
    { "glUniform2iv", (PROC)VirtGpuOglUniform2iv },
    { "glUniform3iv", (PROC)VirtGpuOglUniform3iv },
    { "glUniform4iv", (PROC)VirtGpuOglUniform4iv },
    { "glUniformMatrix2fv", (PROC)VirtGpuOglUniformMatrix2fv },
    { "glUniformMatrix3fv", (PROC)VirtGpuOglUniformMatrix3fv },
    { "glUniformMatrix4fv", (PROC)VirtGpuOglUniformMatrix4fv },
    { "glValidateProgram", (PROC)VirtGpuOglValidateProgram },
    { "glVertexAttrib1d", (PROC)VirtGpuOglVertexAttrib1d },
    { "glVertexAttrib1dv", (PROC)VirtGpuOglVertexAttrib1dv },
    { "glVertexAttrib1f", (PROC)VirtGpuOglVertexAttrib1f },
    { "glVertexAttrib1fv", (PROC)VirtGpuOglVertexAttrib1fv },
    { "glVertexAttrib1s", (PROC)VirtGpuOglVertexAttrib1s },
    { "glVertexAttrib1sv", (PROC)VirtGpuOglVertexAttrib1sv },
    { "glVertexAttrib2d", (PROC)VirtGpuOglVertexAttrib2d },
    { "glVertexAttrib2dv", (PROC)VirtGpuOglVertexAttrib2dv },
    { "glVertexAttrib2f", (PROC)VirtGpuOglVertexAttrib2f },
    { "glVertexAttrib2fv", (PROC)VirtGpuOglVertexAttrib2fv },
    { "glVertexAttrib2s", (PROC)VirtGpuOglVertexAttrib2s },
    { "glVertexAttrib2sv", (PROC)VirtGpuOglVertexAttrib2sv },
    { "glVertexAttrib3d", (PROC)VirtGpuOglVertexAttrib3d },
    { "glVertexAttrib3dv", (PROC)VirtGpuOglVertexAttrib3dv },
    { "glVertexAttrib3f", (PROC)VirtGpuOglVertexAttrib3f },
    { "glVertexAttrib3fv", (PROC)VirtGpuOglVertexAttrib3fv },
    { "glVertexAttrib3s", (PROC)VirtGpuOglVertexAttrib3s },
    { "glVertexAttrib3sv", (PROC)VirtGpuOglVertexAttrib3sv },
    { "glVertexAttrib4Nbv", (PROC)VirtGpuOglVertexAttrib4Nbv },
    { "glVertexAttrib4Niv", (PROC)VirtGpuOglVertexAttrib4Niv },
    { "glVertexAttrib4Nsv", (PROC)VirtGpuOglVertexAttrib4Nsv },
    { "glVertexAttrib4Nub", (PROC)VirtGpuOglVertexAttrib4Nub },
    { "glVertexAttrib4Nubv", (PROC)VirtGpuOglVertexAttrib4Nubv },
    { "glVertexAttrib4Nuiv", (PROC)VirtGpuOglVertexAttrib4Nuiv },
    { "glVertexAttrib4Nusv", (PROC)VirtGpuOglVertexAttrib4Nusv },
    { "glVertexAttrib4bv", (PROC)VirtGpuOglVertexAttrib4bv },
    { "glVertexAttrib4d", (PROC)VirtGpuOglVertexAttrib4d },
    { "glVertexAttrib4dv", (PROC)VirtGpuOglVertexAttrib4dv },
    { "glVertexAttrib4f", (PROC)VirtGpuOglVertexAttrib4f },
    { "glVertexAttrib4fv", (PROC)VirtGpuOglVertexAttrib4fv },
    { "glVertexAttrib4iv", (PROC)VirtGpuOglVertexAttrib4iv },
    { "glVertexAttrib4s", (PROC)VirtGpuOglVertexAttrib4s },
    { "glVertexAttrib4sv", (PROC)VirtGpuOglVertexAttrib4sv },
    { "glVertexAttrib4ubv", (PROC)VirtGpuOglVertexAttrib4ubv },
    { "glVertexAttrib4uiv", (PROC)VirtGpuOglVertexAttrib4uiv },
    { "glVertexAttrib4usv", (PROC)VirtGpuOglVertexAttrib4usv },
    { "glVertexAttribPointer", (PROC)VirtGpuOglVertexAttribPointer },
    { "glUniformMatrix2x3fv", (PROC)VirtGpuOglUniformMatrix2x3fv },
    { "glUniformMatrix3x2fv", (PROC)VirtGpuOglUniformMatrix3x2fv },
    { "glUniformMatrix2x4fv", (PROC)VirtGpuOglUniformMatrix2x4fv },
    { "glUniformMatrix4x2fv", (PROC)VirtGpuOglUniformMatrix4x2fv },
    { "glUniformMatrix3x4fv", (PROC)VirtGpuOglUniformMatrix3x4fv },
    { "glUniformMatrix4x3fv", (PROC)VirtGpuOglUniformMatrix4x3fv },
    { "glColorMaski", (PROC)VirtGpuOglColorMaski },
    { "glGetBooleani_v", (PROC)VirtGpuOglGetBooleani_v },
    { "glGetIntegeri_v", (PROC)VirtGpuOglGetIntegeri_v },
    { "glEnablei", (PROC)VirtGpuOglEnablei },
    { "glDisablei", (PROC)VirtGpuOglDisablei },
    { "glIsEnabledi", (PROC)VirtGpuOglIsEnabledi },
    { "glBeginTransformFeedback", (PROC)VirtGpuOglBeginTransformFeedback },
    { "glEndTransformFeedback", (PROC)VirtGpuOglEndTransformFeedback },
    { "glBindBufferRange", (PROC)VirtGpuOglBindBufferRange },
    { "glBindBufferBase", (PROC)VirtGpuOglBindBufferBase },
    { "glTransformFeedbackVaryings", (PROC)VirtGpuOglTransformFeedbackVaryings },
    { "glGetTransformFeedbackVarying", (PROC)VirtGpuOglGetTransformFeedbackVarying },
    { "glClampColor", (PROC)VirtGpuOglClampColor },
    { "glBeginConditionalRender", (PROC)VirtGpuOglBeginConditionalRender },
    { "glEndConditionalRender", (PROC)VirtGpuOglEndConditionalRender },
    { "glVertexAttribIPointer", (PROC)VirtGpuOglVertexAttribIPointer },
    { "glGetVertexAttribIiv", (PROC)VirtGpuOglGetVertexAttribIiv },
    { "glGetVertexAttribIuiv", (PROC)VirtGpuOglGetVertexAttribIuiv },
    { "glVertexAttribI1i", (PROC)VirtGpuOglVertexAttribI1i },
    { "glVertexAttribI2i", (PROC)VirtGpuOglVertexAttribI2i },
    { "glVertexAttribI3i", (PROC)VirtGpuOglVertexAttribI3i },
    { "glVertexAttribI4i", (PROC)VirtGpuOglVertexAttribI4i },
    { "glVertexAttribI1ui", (PROC)VirtGpuOglVertexAttribI1ui },
    { "glVertexAttribI2ui", (PROC)VirtGpuOglVertexAttribI2ui },
    { "glVertexAttribI3ui", (PROC)VirtGpuOglVertexAttribI3ui },
    { "glVertexAttribI4ui", (PROC)VirtGpuOglVertexAttribI4ui },
    { "glVertexAttribI1iv", (PROC)VirtGpuOglVertexAttribI1iv },
    { "glVertexAttribI2iv", (PROC)VirtGpuOglVertexAttribI2iv },
    { "glVertexAttribI3iv", (PROC)VirtGpuOglVertexAttribI3iv },
    { "glVertexAttribI4iv", (PROC)VirtGpuOglVertexAttribI4iv },
    { "glVertexAttribI1uiv", (PROC)VirtGpuOglVertexAttribI1uiv },
    { "glVertexAttribI2uiv", (PROC)VirtGpuOglVertexAttribI2uiv },
    { "glVertexAttribI3uiv", (PROC)VirtGpuOglVertexAttribI3uiv },
    { "glVertexAttribI4uiv", (PROC)VirtGpuOglVertexAttribI4uiv },
    { "glVertexAttribI4bv", (PROC)VirtGpuOglVertexAttribI4bv },
    { "glVertexAttribI4sv", (PROC)VirtGpuOglVertexAttribI4sv },
    { "glVertexAttribI4ubv", (PROC)VirtGpuOglVertexAttribI4ubv },
    { "glVertexAttribI4usv", (PROC)VirtGpuOglVertexAttribI4usv },
    { "glGetUniformuiv", (PROC)VirtGpuOglGetUniformuiv },
    { "glBindFragDataLocation", (PROC)VirtGpuOglBindFragDataLocation },
    { "glGetFragDataLocation", (PROC)VirtGpuOglGetFragDataLocation },
    { "glUniform1ui", (PROC)VirtGpuOglUniform1ui },
    { "glUniform2ui", (PROC)VirtGpuOglUniform2ui },
    { "glUniform3ui", (PROC)VirtGpuOglUniform3ui },
    { "glUniform4ui", (PROC)VirtGpuOglUniform4ui },
    { "glUniform1uiv", (PROC)VirtGpuOglUniform1uiv },
    { "glUniform2uiv", (PROC)VirtGpuOglUniform2uiv },
    { "glUniform3uiv", (PROC)VirtGpuOglUniform3uiv },
    { "glUniform4uiv", (PROC)VirtGpuOglUniform4uiv },
    { "glTexParameterIiv", (PROC)VirtGpuOglTexParameterIiv },
    { "glTexParameterIuiv", (PROC)VirtGpuOglTexParameterIuiv },
    { "glGetTexParameterIiv", (PROC)VirtGpuOglGetTexParameterIiv },
    { "glGetTexParameterIuiv", (PROC)VirtGpuOglGetTexParameterIuiv },
    { "glClearBufferiv", (PROC)VirtGpuOglClearBufferiv },
    { "glClearBufferuiv", (PROC)VirtGpuOglClearBufferuiv },
    { "glClearBufferfv", (PROC)VirtGpuOglClearBufferfv },
    { "glClearBufferfi", (PROC)VirtGpuOglClearBufferfi },
    { "glGetStringi", (PROC)VirtGpuOglGetStringi },
    { "glIsRenderbuffer", (PROC)VirtGpuOglIsRenderbuffer },
    { "glBindRenderbuffer", (PROC)VirtGpuOglBindRenderbuffer },
    { "glDeleteRenderbuffers", (PROC)VirtGpuOglDeleteRenderbuffers },
    { "glGenRenderbuffers", (PROC)VirtGpuOglGenRenderbuffers },
    { "glRenderbufferStorage", (PROC)VirtGpuOglRenderbufferStorage },
    { "glGetRenderbufferParameteriv", (PROC)VirtGpuOglGetRenderbufferParameteriv },
    { "glIsFramebuffer", (PROC)VirtGpuOglIsFramebuffer },
    { "glBindFramebuffer", (PROC)VirtGpuOglBindFramebuffer },
    { "glDeleteFramebuffers", (PROC)VirtGpuOglDeleteFramebuffers },
    { "glGenFramebuffers", (PROC)VirtGpuOglGenFramebuffers },
    { "glCheckFramebufferStatus", (PROC)VirtGpuOglCheckFramebufferStatus },
    { "glFramebufferTexture1D", (PROC)VirtGpuOglFramebufferTexture1D },
    { "glFramebufferTexture2D", (PROC)VirtGpuOglFramebufferTexture2D },
    { "glFramebufferTexture3D", (PROC)VirtGpuOglFramebufferTexture3D },
    { "glFramebufferRenderbuffer", (PROC)VirtGpuOglFramebufferRenderbuffer },
    { "glGetFramebufferAttachmentParameteriv", (PROC)VirtGpuOglGetFramebufferAttachmentParameteriv },
    { "glGenerateMipmap", (PROC)VirtGpuOglGenerateMipmap },
    { "glBlitFramebuffer", (PROC)VirtGpuOglBlitFramebuffer },
    { "glRenderbufferStorageMultisample", (PROC)VirtGpuOglRenderbufferStorageMultisample },
    { "glFramebufferTextureLayer", (PROC)VirtGpuOglFramebufferTextureLayer },
    { "glMapBufferRange", (PROC)VirtGpuOglMapBufferRange },
    { "glFlushMappedBufferRange", (PROC)VirtGpuOglFlushMappedBufferRange },
    { "glBindVertexArray", (PROC)VirtGpuOglBindVertexArray },
    { "glDeleteVertexArrays", (PROC)VirtGpuOglDeleteVertexArrays },
    { "glGenVertexArrays", (PROC)VirtGpuOglGenVertexArrays },
    { "glIsVertexArray", (PROC)VirtGpuOglIsVertexArray },
    { "glDrawArraysInstanced", (PROC)VirtGpuOglDrawArraysInstanced },
    { "glDrawElementsInstanced", (PROC)VirtGpuOglDrawElementsInstanced },
    { "glTexBuffer", (PROC)VirtGpuOglTexBuffer },
    { "glPrimitiveRestartIndex", (PROC)VirtGpuOglPrimitiveRestartIndex },
    { "glCopyBufferSubData", (PROC)VirtGpuOglCopyBufferSubData },
    { "glGetUniformIndices", (PROC)VirtGpuOglGetUniformIndices },
    { "glGetActiveUniformsiv", (PROC)VirtGpuOglGetActiveUniformsiv },
    { "glGetActiveUniformName", (PROC)VirtGpuOglGetActiveUniformName },
    { "glGetUniformBlockIndex", (PROC)VirtGpuOglGetUniformBlockIndex },
    { "glGetActiveUniformBlockiv", (PROC)VirtGpuOglGetActiveUniformBlockiv },
    { "glGetActiveUniformBlockName", (PROC)VirtGpuOglGetActiveUniformBlockName },
    { "glUniformBlockBinding", (PROC)VirtGpuOglUniformBlockBinding },
    { "glDrawElementsBaseVertex", (PROC)VirtGpuOglDrawElementsBaseVertex },
    { "glDrawRangeElementsBaseVertex", (PROC)VirtGpuOglDrawRangeElementsBaseVertex },
    { "glDrawElementsInstancedBaseVertex", (PROC)VirtGpuOglDrawElementsInstancedBaseVertex },
    { "glMultiDrawElementsBaseVertex", (PROC)VirtGpuOglMultiDrawElementsBaseVertex },
    { "glProvokingVertex", (PROC)VirtGpuOglProvokingVertex },
    { "glFenceSync", (PROC)VirtGpuOglFenceSync },
    { "glIsSync", (PROC)VirtGpuOglIsSync },
    { "glDeleteSync", (PROC)VirtGpuOglDeleteSync },
    { "glClientWaitSync", (PROC)VirtGpuOglClientWaitSync },
    { "glWaitSync", (PROC)VirtGpuOglWaitSync },
    { "glGetInteger64v", (PROC)VirtGpuOglGetInteger64v },
    { "glGetSynciv", (PROC)VirtGpuOglGetSynciv },
    { "glGetInteger64i_v", (PROC)VirtGpuOglGetInteger64i_v },
    { "glGetBufferParameteri64v", (PROC)VirtGpuOglGetBufferParameteri64v },
    { "glFramebufferTexture", (PROC)VirtGpuOglFramebufferTexture },
    { "glTexImage2DMultisample", (PROC)VirtGpuOglTexImage2DMultisample },
    { "glTexImage3DMultisample", (PROC)VirtGpuOglTexImage3DMultisample },
    { "glGetMultisamplefv", (PROC)VirtGpuOglGetMultisamplefv },
    { "glSampleMaski", (PROC)VirtGpuOglSampleMaski },
    { "glBindFragDataLocationIndexed", (PROC)VirtGpuOglBindFragDataLocationIndexed },
    { "glGetFragDataIndex", (PROC)VirtGpuOglGetFragDataIndex },
    { "glGenSamplers", (PROC)VirtGpuOglGenSamplers },
    { "glDeleteSamplers", (PROC)VirtGpuOglDeleteSamplers },
    { "glIsSampler", (PROC)VirtGpuOglIsSampler },
    { "glBindSampler", (PROC)VirtGpuOglBindSampler },
    { "glSamplerParameteri", (PROC)VirtGpuOglSamplerParameteri },
    { "glSamplerParameteriv", (PROC)VirtGpuOglSamplerParameteriv },
    { "glSamplerParameterf", (PROC)VirtGpuOglSamplerParameterf },
    { "glSamplerParameterfv", (PROC)VirtGpuOglSamplerParameterfv },
    { "glSamplerParameterIiv", (PROC)VirtGpuOglSamplerParameterIiv },
    { "glSamplerParameterIuiv", (PROC)VirtGpuOglSamplerParameterIuiv },
    { "glGetSamplerParameteriv", (PROC)VirtGpuOglGetSamplerParameteriv },
    { "glGetSamplerParameterIiv", (PROC)VirtGpuOglGetSamplerParameterIiv },
    { "glGetSamplerParameterfv", (PROC)VirtGpuOglGetSamplerParameterfv },
    { "glGetSamplerParameterIuiv", (PROC)VirtGpuOglGetSamplerParameterIuiv },
    { "glQueryCounter", (PROC)VirtGpuOglQueryCounter },
    { "glGetQueryObjecti64v", (PROC)VirtGpuOglGetQueryObjecti64v },
    { "glGetQueryObjectui64v", (PROC)VirtGpuOglGetQueryObjectui64v },
    { "glVertexAttribDivisor", (PROC)VirtGpuOglVertexAttribDivisor },
    { "glVertexAttribP1ui", (PROC)VirtGpuOglVertexAttribP1ui },
    { "glVertexAttribP1uiv", (PROC)VirtGpuOglVertexAttribP1uiv },
    { "glVertexAttribP2ui", (PROC)VirtGpuOglVertexAttribP2ui },
    { "glVertexAttribP2uiv", (PROC)VirtGpuOglVertexAttribP2uiv },
    { "glVertexAttribP3ui", (PROC)VirtGpuOglVertexAttribP3ui },
    { "glVertexAttribP3uiv", (PROC)VirtGpuOglVertexAttribP3uiv },
    { "glVertexAttribP4ui", (PROC)VirtGpuOglVertexAttribP4ui },
    { "glVertexAttribP4uiv", (PROC)VirtGpuOglVertexAttribP4uiv },
    { "glMinSampleShading", (PROC)VirtGpuOglMinSampleShading },
    { "glBlendEquationi", (PROC)VirtGpuOglBlendEquationi },
    { "glBlendEquationSeparatei", (PROC)VirtGpuOglBlendEquationSeparatei },
    { "glBlendFunci", (PROC)VirtGpuOglBlendFunci },
    { "glBlendFuncSeparatei", (PROC)VirtGpuOglBlendFuncSeparatei },
    { "glDrawArraysIndirect", (PROC)VirtGpuOglDrawArraysIndirect },
    { "glDrawElementsIndirect", (PROC)VirtGpuOglDrawElementsIndirect },
    { "glUniform1d", (PROC)VirtGpuOglUniform1d },
    { "glUniform2d", (PROC)VirtGpuOglUniform2d },
    { "glUniform3d", (PROC)VirtGpuOglUniform3d },
    { "glUniform4d", (PROC)VirtGpuOglUniform4d },
    { "glUniform1dv", (PROC)VirtGpuOglUniform1dv },
    { "glUniform2dv", (PROC)VirtGpuOglUniform2dv },
    { "glUniform3dv", (PROC)VirtGpuOglUniform3dv },
    { "glUniform4dv", (PROC)VirtGpuOglUniform4dv },
    { "glUniformMatrix2dv", (PROC)VirtGpuOglUniformMatrix2dv },
    { "glUniformMatrix3dv", (PROC)VirtGpuOglUniformMatrix3dv },
    { "glUniformMatrix4dv", (PROC)VirtGpuOglUniformMatrix4dv },
    { "glUniformMatrix2x3dv", (PROC)VirtGpuOglUniformMatrix2x3dv },
    { "glUniformMatrix2x4dv", (PROC)VirtGpuOglUniformMatrix2x4dv },
    { "glUniformMatrix3x2dv", (PROC)VirtGpuOglUniformMatrix3x2dv },
    { "glUniformMatrix3x4dv", (PROC)VirtGpuOglUniformMatrix3x4dv },
    { "glUniformMatrix4x2dv", (PROC)VirtGpuOglUniformMatrix4x2dv },
    { "glUniformMatrix4x3dv", (PROC)VirtGpuOglUniformMatrix4x3dv },
    { "glGetUniformdv", (PROC)VirtGpuOglGetUniformdv },
    { "glGetSubroutineUniformLocation", (PROC)VirtGpuOglGetSubroutineUniformLocation },
    { "glGetSubroutineIndex", (PROC)VirtGpuOglGetSubroutineIndex },
    { "glGetActiveSubroutineUniformiv", (PROC)VirtGpuOglGetActiveSubroutineUniformiv },
    { "glGetActiveSubroutineUniformName", (PROC)VirtGpuOglGetActiveSubroutineUniformName },
    { "glGetActiveSubroutineName", (PROC)VirtGpuOglGetActiveSubroutineName },
    { "glUniformSubroutinesuiv", (PROC)VirtGpuOglUniformSubroutinesuiv },
    { "glGetUniformSubroutineuiv", (PROC)VirtGpuOglGetUniformSubroutineuiv },
    { "glGetProgramStageiv", (PROC)VirtGpuOglGetProgramStageiv },
    { "glPatchParameteri", (PROC)VirtGpuOglPatchParameteri },
    { "glPatchParameterfv", (PROC)VirtGpuOglPatchParameterfv },
    { "glBindTransformFeedback", (PROC)VirtGpuOglBindTransformFeedback },
    { "glDeleteTransformFeedbacks", (PROC)VirtGpuOglDeleteTransformFeedbacks },
    { "glGenTransformFeedbacks", (PROC)VirtGpuOglGenTransformFeedbacks },
    { "glIsTransformFeedback", (PROC)VirtGpuOglIsTransformFeedback },
    { "glPauseTransformFeedback", (PROC)VirtGpuOglPauseTransformFeedback },
    { "glResumeTransformFeedback", (PROC)VirtGpuOglResumeTransformFeedback },
    { "glDrawTransformFeedback", (PROC)VirtGpuOglDrawTransformFeedback },
    { "glDrawTransformFeedbackStream", (PROC)VirtGpuOglDrawTransformFeedbackStream },
    { "glBeginQueryIndexed", (PROC)VirtGpuOglBeginQueryIndexed },
    { "glEndQueryIndexed", (PROC)VirtGpuOglEndQueryIndexed },
    { "glGetQueryIndexediv", (PROC)VirtGpuOglGetQueryIndexediv },
    { "glColorTable", (PROC)VirtGpuOglColorTable },
    { "glColorTableParameterfv", (PROC)VirtGpuOglColorTableParameterfv },
    { "glColorTableParameteriv", (PROC)VirtGpuOglColorTableParameteriv },
    { "glCopyColorTable", (PROC)VirtGpuOglCopyColorTable },
    { "glGetColorTable", (PROC)VirtGpuOglGetColorTable },
    { "glGetColorTableParameterfv", (PROC)VirtGpuOglGetColorTableParameterfv },
    { "glGetColorTableParameteriv", (PROC)VirtGpuOglGetColorTableParameteriv },
    { "glColorSubTable", (PROC)VirtGpuOglColorSubTable },
    { "glCopyColorSubTable", (PROC)VirtGpuOglCopyColorSubTable },
    { "glConvolutionFilter1D", (PROC)VirtGpuOglConvolutionFilter1D },
    { "glConvolutionFilter2D", (PROC)VirtGpuOglConvolutionFilter2D },
    { "glConvolutionParameterf", (PROC)VirtGpuOglConvolutionParameterf },
    { "glConvolutionParameterfv", (PROC)VirtGpuOglConvolutionParameterfv },
    { "glConvolutionParameteri", (PROC)VirtGpuOglConvolutionParameteri },
    { "glConvolutionParameteriv", (PROC)VirtGpuOglConvolutionParameteriv },
    { "glCopyConvolutionFilter1D", (PROC)VirtGpuOglCopyConvolutionFilter1D },
    { "glCopyConvolutionFilter2D", (PROC)VirtGpuOglCopyConvolutionFilter2D },
    { "glGetConvolutionFilter", (PROC)VirtGpuOglGetConvolutionFilter },
    { "glGetConvolutionParameterfv", (PROC)VirtGpuOglGetConvolutionParameterfv },
    { "glGetConvolutionParameteriv", (PROC)VirtGpuOglGetConvolutionParameteriv },
    { "glGetSeparableFilter", (PROC)VirtGpuOglGetSeparableFilter },
    { "glSeparableFilter2D", (PROC)VirtGpuOglSeparableFilter2D },
    { "glGetHistogram", (PROC)VirtGpuOglGetHistogram },
    { "glGetHistogramParameterfv", (PROC)VirtGpuOglGetHistogramParameterfv },
    { "glGetHistogramParameteriv", (PROC)VirtGpuOglGetHistogramParameteriv },
    { "glGetMinmax", (PROC)VirtGpuOglGetMinmax },
    { "glGetMinmaxParameterfv", (PROC)VirtGpuOglGetMinmaxParameterfv },
    { "glGetMinmaxParameteriv", (PROC)VirtGpuOglGetMinmaxParameteriv },
    { "glHistogram", (PROC)VirtGpuOglHistogram },
    { "glMinmax", (PROC)VirtGpuOglMinmax },
    { "glResetHistogram", (PROC)VirtGpuOglResetHistogram },
    { "glResetMinmax", (PROC)VirtGpuOglResetMinmax },
    { "glClientActiveTexture", (PROC)VirtGpuOglClientActiveTexture },
    { "glMultiTexCoord1d", (PROC)VirtGpuOglMultiTexCoord1d },
    { "glMultiTexCoord1dv", (PROC)VirtGpuOglMultiTexCoord1dv },
    { "glMultiTexCoord1f", (PROC)VirtGpuOglMultiTexCoord1f },
    { "glMultiTexCoord1fv", (PROC)VirtGpuOglMultiTexCoord1fv },
    { "glMultiTexCoord1i", (PROC)VirtGpuOglMultiTexCoord1i },
    { "glMultiTexCoord1iv", (PROC)VirtGpuOglMultiTexCoord1iv },
    { "glMultiTexCoord1s", (PROC)VirtGpuOglMultiTexCoord1s },
    { "glMultiTexCoord1sv", (PROC)VirtGpuOglMultiTexCoord1sv },
    { "glMultiTexCoord2d", (PROC)VirtGpuOglMultiTexCoord2d },
    { "glMultiTexCoord2dv", (PROC)VirtGpuOglMultiTexCoord2dv },
    { "glMultiTexCoord2f", (PROC)VirtGpuOglMultiTexCoord2f },
    { "glMultiTexCoord2fv", (PROC)VirtGpuOglMultiTexCoord2fv },
    { "glMultiTexCoord2i", (PROC)VirtGpuOglMultiTexCoord2i },
    { "glMultiTexCoord2iv", (PROC)VirtGpuOglMultiTexCoord2iv },
    { "glMultiTexCoord2s", (PROC)VirtGpuOglMultiTexCoord2s },
    { "glMultiTexCoord2sv", (PROC)VirtGpuOglMultiTexCoord2sv },
    { "glMultiTexCoord3d", (PROC)VirtGpuOglMultiTexCoord3d },
    { "glMultiTexCoord3dv", (PROC)VirtGpuOglMultiTexCoord3dv },
    { "glMultiTexCoord3f", (PROC)VirtGpuOglMultiTexCoord3f },
    { "glMultiTexCoord3fv", (PROC)VirtGpuOglMultiTexCoord3fv },
    { "glMultiTexCoord3i", (PROC)VirtGpuOglMultiTexCoord3i },
    { "glMultiTexCoord3iv", (PROC)VirtGpuOglMultiTexCoord3iv },
    { "glMultiTexCoord3s", (PROC)VirtGpuOglMultiTexCoord3s },
    { "glMultiTexCoord3sv", (PROC)VirtGpuOglMultiTexCoord3sv },
    { "glMultiTexCoord4d", (PROC)VirtGpuOglMultiTexCoord4d },
    { "glMultiTexCoord4dv", (PROC)VirtGpuOglMultiTexCoord4dv },
    { "glMultiTexCoord4f", (PROC)VirtGpuOglMultiTexCoord4f },
    { "glMultiTexCoord4fv", (PROC)VirtGpuOglMultiTexCoord4fv },
    { "glMultiTexCoord4i", (PROC)VirtGpuOglMultiTexCoord4i },
    { "glMultiTexCoord4iv", (PROC)VirtGpuOglMultiTexCoord4iv },
    { "glMultiTexCoord4s", (PROC)VirtGpuOglMultiTexCoord4s },
    { "glMultiTexCoord4sv", (PROC)VirtGpuOglMultiTexCoord4sv },
    { "glLoadTransposeMatrixf", (PROC)VirtGpuOglLoadTransposeMatrixf },
    { "glLoadTransposeMatrixd", (PROC)VirtGpuOglLoadTransposeMatrixd },
    { "glMultTransposeMatrixf", (PROC)VirtGpuOglMultTransposeMatrixf },
    { "glMultTransposeMatrixd", (PROC)VirtGpuOglMultTransposeMatrixd },
    { "glFogCoordf", (PROC)VirtGpuOglFogCoordf },
    { "glFogCoordfv", (PROC)VirtGpuOglFogCoordfv },
    { "glFogCoordd", (PROC)VirtGpuOglFogCoordd },
    { "glFogCoorddv", (PROC)VirtGpuOglFogCoorddv },
    { "glFogCoordPointer", (PROC)VirtGpuOglFogCoordPointer },
    { "glSecondaryColor3b", (PROC)VirtGpuOglSecondaryColor3b },
    { "glSecondaryColor3bv", (PROC)VirtGpuOglSecondaryColor3bv },
    { "glSecondaryColor3d", (PROC)VirtGpuOglSecondaryColor3d },
    { "glSecondaryColor3dv", (PROC)VirtGpuOglSecondaryColor3dv },
    { "glSecondaryColor3f", (PROC)VirtGpuOglSecondaryColor3f },
    { "glSecondaryColor3fv", (PROC)VirtGpuOglSecondaryColor3fv },
    { "glSecondaryColor3i", (PROC)VirtGpuOglSecondaryColor3i },
    { "glSecondaryColor3iv", (PROC)VirtGpuOglSecondaryColor3iv },
    { "glSecondaryColor3s", (PROC)VirtGpuOglSecondaryColor3s },
    { "glSecondaryColor3sv", (PROC)VirtGpuOglSecondaryColor3sv },
    { "glSecondaryColor3ub", (PROC)VirtGpuOglSecondaryColor3ub },
    { "glSecondaryColor3ubv", (PROC)VirtGpuOglSecondaryColor3ubv },
    { "glSecondaryColor3ui", (PROC)VirtGpuOglSecondaryColor3ui },
    { "glSecondaryColor3uiv", (PROC)VirtGpuOglSecondaryColor3uiv },
    { "glSecondaryColor3us", (PROC)VirtGpuOglSecondaryColor3us },
    { "glSecondaryColor3usv", (PROC)VirtGpuOglSecondaryColor3usv },
    { "glSecondaryColorPointer", (PROC)VirtGpuOglSecondaryColorPointer },
    { "glWindowPos2d", (PROC)VirtGpuOglWindowPos2d },
    { "glWindowPos2dv", (PROC)VirtGpuOglWindowPos2dv },
    { "glWindowPos2f", (PROC)VirtGpuOglWindowPos2f },
    { "glWindowPos2fv", (PROC)VirtGpuOglWindowPos2fv },
    { "glWindowPos2i", (PROC)VirtGpuOglWindowPos2i },
    { "glWindowPos2iv", (PROC)VirtGpuOglWindowPos2iv },
    { "glWindowPos2s", (PROC)VirtGpuOglWindowPos2s },
    { "glWindowPos2sv", (PROC)VirtGpuOglWindowPos2sv },
    { "glWindowPos3d", (PROC)VirtGpuOglWindowPos3d },
    { "glWindowPos3dv", (PROC)VirtGpuOglWindowPos3dv },
    { "glWindowPos3f", (PROC)VirtGpuOglWindowPos3f },
    { "glWindowPos3fv", (PROC)VirtGpuOglWindowPos3fv },
    { "glWindowPos3i", (PROC)VirtGpuOglWindowPos3i },
    { "glWindowPos3iv", (PROC)VirtGpuOglWindowPos3iv },
    { "glWindowPos3s", (PROC)VirtGpuOglWindowPos3s },
    { "glWindowPos3sv", (PROC)VirtGpuOglWindowPos3sv },
};


static PROC
VirtGpuOglGetCoreProc(_In_ LPCSTR Name)
{
    ULONG Index;

    for (Index = 0;
         Index < sizeof(VirtGpuOglCoreProcTable) / sizeof(VirtGpuOglCoreProcTable[0]);
         ++Index)
    {
        if (lstrcmpA(Name, VirtGpuOglCoreProcTable[Index].Name) == 0)
            return VirtGpuOglCoreProcTable[Index].Proc;
    }

    return NULL;
}

static VOID
VirtGpuOglInitializeProcTable(VOID)
{
    if (VirtGpuOglProcTable.EntryCount == VIRTGPU_OPENGL_ENTRY_COUNT)
        return;

    VirtGpuOglProcTable.EntryCount = VIRTGPU_OPENGL_ENTRY_COUNT;
    CopyMemory(VirtGpuOglProcTable.Entries,
               VirtGpuOglDispatchEntries,
               sizeof(VirtGpuOglDispatchEntries));
}



static BOOL
VirtGpuOglQueryCaps(_In_ HDC hdc, _Out_ PVIRTGPU_3D_CAPS Caps)
{
    ULONG Returned;

    ZeroMemory(Caps, sizeof(*Caps));
    if (!VirtGpuOglEscapeIoControl(hdc,
                                   IOCTL_VIDEO_VIRTGPU_QUERY_3D_CAPS,
                                   NULL,
                                   0,
                                   Caps,
                                   sizeof(*Caps),
                                   &Returned))
    {
        return FALSE;
    }

    return (Returned >= sizeof(*Caps)) &&
           (Caps->Size == sizeof(*Caps)) &&
           (Caps->Enabled != 0) &&
           (Caps->PreferredCapsetId != 0);
}

static VOID
VirtGpuOglFillPixelFormat(_Out_ LPPIXELFORMATDESCRIPTOR PixelFormat)
{
    ZeroMemory(PixelFormat, sizeof(*PixelFormat));
    PixelFormat->nSize = sizeof(*PixelFormat);
    PixelFormat->nVersion = 1;
    PixelFormat->dwFlags = PFD_DRAW_TO_WINDOW |
                           PFD_SUPPORT_OPENGL;
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
    PixelFormat->iLayerType = PFD_MAIN_PLANE;
}

static ULONG
VirtGpuOglIoControl(
    _In_ HDC hdc,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputSize) PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID OutputBuffer,
    _In_ ULONG OutputSize)
{
    ULONG Returned = 0;

    if (!VirtGpuOglEscapeIoControl(hdc,
                                   IoControlCode,
                                   InputBuffer,
                                   InputSize,
                                   OutputBuffer,
                                   OutputSize,
                                   &Returned))
    {
        return 0;
    }

    return Returned != 0 ? Returned : 1;
}

ULONG WINAPI
VirtGpu3DIoControl(
    _In_ HDC hdc,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputSize) PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID OutputBuffer,
    _In_ ULONG OutputSize)
{
    return VirtGpuOglIoControl(hdc,
                               IoControlCode,
                               InputBuffer,
                               InputSize,
                               OutputBuffer,
                               OutputSize);
}

ULONG WINAPI
VirtGpu3DCurrentIoControl(
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputSize) PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID OutputBuffer,
    _In_ ULONG OutputSize)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return 0;

    return VirtGpuOglIoControl(Context->hdc,
                               IoControlCode,
                               InputBuffer,
                               InputSize,
                               OutputBuffer,
                               OutputSize);
}

BOOL WINAPI
VirtGpu3DQueryCaps(_In_ HDC hdc, _Out_ PVIRTGPU_3D_CAPS Caps)
{
    return VirtGpuOglQueryCaps(hdc, Caps);
}

BOOL WINAPI
VirtGpu3DGetCapset(
    _In_ HDC hdc,
    _Inout_updates_bytes_(BufferSize) PVIRTGPU_3D_GET_CAPSET Capset,
    _In_ ULONG BufferSize)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_GET_CAPSET,
                               Capset,
                               offsetof(VIRTGPU_3D_GET_CAPSET, Data),
                               Capset,
                               BufferSize) != 0;
}

BOOL WINAPI
VirtGpu3DCreateContext(
    _In_ HDC hdc,
    _Inout_ PVIRTGPU_3D_CREATE_CONTEXT Context)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_CREATE_CONTEXT,
                               Context,
                               sizeof(*Context),
                               Context,
                               sizeof(*Context)) >= sizeof(*Context);
}

BOOL WINAPI
VirtGpu3DDestroyContext(
    _In_ HDC hdc,
    _In_ PVIRTGPU_3D_CONTEXT Context)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_DESTROY_CONTEXT,
                               (PVOID)Context,
                               sizeof(*Context),
                               NULL,
                               0) != 0;
}

BOOL WINAPI
VirtGpu3DCreateResource(
    _In_ HDC hdc,
    _Inout_ PVIRTGPU_3D_CREATE_RESOURCE Resource)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_CREATE_RESOURCE,
                               Resource,
                               sizeof(*Resource),
                               Resource,
                               sizeof(*Resource)) >= sizeof(*Resource);
}

BOOL WINAPI
VirtGpu3DDestroyResource(
    _In_ HDC hdc,
    _In_ PVIRTGPU_3D_RESOURCE Resource)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_DESTROY_RESOURCE,
                               (PVOID)Resource,
                               sizeof(*Resource),
                               NULL,
                               0) != 0;
}

BOOL WINAPI
VirtGpu3DCreateBlob(
    _In_ HDC hdc,
    _Inout_updates_bytes_(BufferSize) PVIRTGPU_3D_CREATE_BLOB Blob,
    _In_ ULONG BufferSize)
{
    ULONG HeaderSize = offsetof(VIRTGPU_3D_CREATE_BLOB, Commands);

    return (BufferSize >= HeaderSize) &&
           (VirtGpuOglIoControl(hdc,
                                IOCTL_VIDEO_VIRTGPU_3D_CREATE_BLOB,
                                Blob,
                                BufferSize,
                                Blob,
                                HeaderSize) >= HeaderSize);
}

BOOL WINAPI
VirtGpu3DAssignUuid(
    _In_ HDC hdc,
    _Inout_ PVIRTGPU_3D_RESOURCE_UUID ResourceUuid)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_ASSIGN_UUID,
                               ResourceUuid,
                               sizeof(*ResourceUuid),
                               ResourceUuid,
                               sizeof(*ResourceUuid)) >= sizeof(*ResourceUuid);
}

BOOL WINAPI
VirtGpu3DMapBlob(
    _In_ HDC hdc,
    _Inout_ PVIRTGPU_3D_MAP_BLOB Map)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_MAP_BLOB,
                               Map,
                               sizeof(*Map),
                               Map,
                               sizeof(*Map)) >= sizeof(*Map);
}

BOOL WINAPI
VirtGpu3DUnmapBlob(
    _In_ HDC hdc,
    _In_ PVIRTGPU_3D_RESOURCE Resource)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_UNMAP_BLOB,
                               (PVOID)Resource,
                               sizeof(*Resource),
                               NULL,
                               0) != 0;
}

BOOL WINAPI
VirtGpu3DAttachResource(
    _In_ HDC hdc,
    _In_ PVIRTGPU_3D_CONTEXT_RESOURCE ContextResource)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_ATTACH_RESOURCE,
                               (PVOID)ContextResource,
                               sizeof(*ContextResource),
                               NULL,
                               0) != 0;
}

BOOL WINAPI
VirtGpu3DDetachResource(
    _In_ HDC hdc,
    _In_ PVIRTGPU_3D_CONTEXT_RESOURCE ContextResource)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_DETACH_RESOURCE,
                               (PVOID)ContextResource,
                               sizeof(*ContextResource),
                               NULL,
                               0) != 0;
}

BOOL WINAPI
VirtGpu3DTransferToHost(
    _In_ HDC hdc,
    _Inout_updates_bytes_(BufferSize) PVIRTGPU_3D_TRANSFER Transfer,
    _In_ ULONG BufferSize)
{
    ULONG HeaderSize = offsetof(VIRTGPU_3D_TRANSFER, Data);

    return (BufferSize >= HeaderSize) &&
           (VirtGpuOglIoControl(hdc,
                                IOCTL_VIDEO_VIRTGPU_3D_TRANSFER_TO_HOST,
                                Transfer,
                                BufferSize,
                                Transfer,
                                HeaderSize) >= HeaderSize);
}

BOOL WINAPI
VirtGpu3DTransferFromHost(
    _In_ HDC hdc,
    _Inout_updates_bytes_(BufferSize) PVIRTGPU_3D_TRANSFER Transfer,
    _In_ ULONG BufferSize)
{
    ULONG HeaderSize = offsetof(VIRTGPU_3D_TRANSFER, Data);

    return (BufferSize >= HeaderSize) &&
           (VirtGpuOglIoControl(hdc,
                                IOCTL_VIDEO_VIRTGPU_3D_TRANSFER_FROM_HOST,
                                Transfer,
                                HeaderSize,
                                Transfer,
                                BufferSize) >= HeaderSize);
}

BOOL WINAPI
VirtGpu3DSubmit(
    _In_ HDC hdc,
    _Inout_updates_bytes_(BufferSize) PVIRTGPU_3D_SUBMIT Submit,
    _In_ ULONG BufferSize)
{
    ULONG HeaderSize = offsetof(VIRTGPU_3D_SUBMIT, Commands);

    return (BufferSize >= HeaderSize) &&
           (VirtGpuOglIoControl(hdc,
                                IOCTL_VIDEO_VIRTGPU_3D_SUBMIT,
                                Submit,
                                BufferSize,
                                Submit,
                                HeaderSize) >= HeaderSize);
}

BOOL WINAPI
VirtGpu3DWaitFence(
    _In_ HDC hdc,
    _Inout_ PVIRTGPU_3D_FENCE Fence)
{
    return VirtGpuOglIoControl(hdc,
                               IOCTL_VIDEO_VIRTGPU_3D_WAIT_FENCE,
                               Fence,
                               sizeof(*Fence),
                               Fence,
                               sizeof(*Fence)) >= sizeof(*Fence);
}

BOOL WINAPI
DllMain(
    _In_ HINSTANCE hinstDLL,
    _In_ DWORD fdwReason,
    _In_opt_ LPVOID lpvReserved)
{
    UNREFERENCED_PARAMETER(hinstDLL);
    UNREFERENCED_PARAMETER(fdwReason);
    UNREFERENCED_PARAMETER(lpvReserved);
    return TRUE;
}

BOOL WINAPI
DrvValidateVersion(_In_ DWORD DriverVersion)
{
    return DriverVersion == VIRTGPU_OPENGL_ICD_DRIVER_VERSION;
}

VOID WINAPI
DrvSetCallbackProcs(_In_ INT nProcs, _In_reads_opt_(nProcs) PROC *pProcs)
{
    if ((pProcs == NULL) || (nProcs <= 0))
        return;

    if (nProcs >= 1)
        VirtGpuOglSetCurrentValue = (PFN_SET_CURRENT_VALUE)pProcs[0];
    if (nProcs >= 2)
        VirtGpuOglGetCurrentValue = (PFN_GET_CURRENT_VALUE)pProcs[1];
    if (nProcs >= 3)
        VirtGpuOglGetCurrentDHGLRC = (PFN_GET_CURRENT_DHGLRC)pProcs[2];
}

BOOL WINAPI
DrvCopyContext(_In_ DHGLRC hglrcSrc, _In_ DHGLRC hglrcDst, _In_ UINT mask)
{
    UNREFERENCED_PARAMETER(mask);

    return (VirtGpuOglValidateContext(hglrcSrc) != NULL) &&
           (VirtGpuOglValidateContext(hglrcDst) != NULL);
}

DHGLRC WINAPI
DrvCreateContext(_In_ HDC hdc)
{
    VIRTGPU_3D_CAPS Caps;
    VIRTGPU_3D_CREATE_CONTEXT Create;
    PVIRTGPU_OGL_CONTEXT Context;
    ULONG Returned;
    static const CHAR DebugName[] = "ReactOS VirtGpu ICD";

    if (!VirtGpuOglQueryCaps(hdc, &Caps))
        return NULL;

    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (Context == NULL)
        return NULL;

    ZeroMemory(&Create, sizeof(Create));
    Create.CapsetId = Caps.PreferredCapsetId;
    CopyMemory(Create.DebugName, DebugName, sizeof(DebugName));

    if (!VirtGpuOglEscapeIoControl(hdc,
                                   IOCTL_VIDEO_VIRTGPU_3D_CREATE_CONTEXT,
                                   &Create,
                                   sizeof(Create),
                                   &Create,
                                   sizeof(Create),
                                   &Returned) ||
        (Returned < sizeof(Create)) ||
        (Create.ContextId == 0))
    {
        HeapFree(GetProcessHeap(), 0, Context);
        return NULL;
    }

    Context->Signature = VIRTGPU_OGL_CONTEXT_SIGNATURE;
    Context->hdc = hdc;
    Context->ContextId = Create.ContextId;
    Context->PixelFormat = GetPixelFormat(hdc);
    Context->LastError = GL_NO_ERROR;
    VirtGpuOglInitializeContextState(Context);
    return (DHGLRC)Context;
}

DHGLRC WINAPI
DrvCreateLayerContext(_In_ HDC hdc, _In_ INT iLayerPlane)
{
    if (iLayerPlane != 0)
        return NULL;
    return DrvCreateContext(hdc);
}

BOOL WINAPI
DrvDeleteContext(_In_ DHGLRC hglrc)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglValidateContext(hglrc);
    VIRTGPU_3D_CONTEXT Destroy;
    BOOL Success = TRUE;
    ULONG Index;

    if (Context == NULL)
        return FALSE;

    if (VirtGpuOglCurrentContext() == Context)
        VirtGpuOglSetCurrentContext(NULL);

    VirtGpuOglDestroyVirglDepthStencilTarget(Context);
    VirtGpuOglDestroyVirglColorTarget(Context);
    for (Index = 0; Index < VIRTGPU_OGL_MAX_TEXTURES; ++Index)
        VirtGpuOglFreeTexture(&Context->Textures[Index]);
    for (Index = 0; Index < VIRTGPU_OGL_MAX_DISPLAY_LISTS; ++Index)
        VirtGpuOglFreeDisplayList(&Context->DisplayLists[Index]);

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.ContextId = Context->ContextId;
    if (Destroy.ContextId != 0)
    {
        Success = VirtGpuOglEscapeIoControl(Context->hdc,
                                            IOCTL_VIDEO_VIRTGPU_3D_DESTROY_CONTEXT,
                                            &Destroy,
                                            sizeof(Destroy),
                                            NULL,
                                            0,
                                            NULL);
    }

    Context->Signature = 0;
    HeapFree(GetProcessHeap(), 0, Context);
    return Success;
}

BOOL WINAPI
DrvDescribeLayerPlane(
    _In_ HDC hdc,
    _In_ INT iPixelFormat,
    _In_ INT iLayerPlane,
    _In_ UINT nBytes,
    _Out_writes_bytes_(nBytes) LPLAYERPLANEDESCRIPTOR plpd)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(iPixelFormat);
    UNREFERENCED_PARAMETER(iLayerPlane);

    if ((iLayerPlane != 0) ||
        (plpd == NULL) ||
        (nBytes < sizeof(*plpd)))
    {
        return FALSE;
    }

    ZeroMemory(plpd, sizeof(*plpd));
    plpd->nSize = sizeof(*plpd);
    plpd->nVersion = 1;
    plpd->dwFlags = LPD_SUPPORT_OPENGL;
    plpd->iPixelType = PFD_TYPE_RGBA;
    plpd->cColorBits = 32;
    plpd->cAlphaBits = 8;
    return TRUE;
}

INT WINAPI
DrvDescribePixelFormat(
    _In_ HDC hdc,
    _In_ INT iPixelFormat,
    _In_ UINT nBytes,
    _Out_writes_bytes_opt_(nBytes) LPPIXELFORMATDESCRIPTOR ppfd)
{
    VIRTGPU_3D_CAPS Caps;

    if (!VirtGpuOglQueryCaps(hdc, &Caps))
        return 0;

    if (ppfd == NULL)
        return VIRTGPU_OPENGL_PIXEL_FORMAT_COUNT;

    if ((iPixelFormat < 1) ||
        (iPixelFormat > VIRTGPU_OPENGL_PIXEL_FORMAT_COUNT) ||
        (nBytes < sizeof(*ppfd)))
    {
        return 0;
    }

    VirtGpuOglFillPixelFormat(ppfd);
    return VIRTGPU_OPENGL_PIXEL_FORMAT_COUNT;
}

INT WINAPI
DrvGetLayerPaletteEntries(
    _In_ HDC hdc,
    _In_ INT iLayerPlane,
    _In_ INT iStart,
    _In_ INT cEntries,
    _Out_writes_(cEntries) COLORREF *pcr)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(iLayerPlane);
    UNREFERENCED_PARAMETER(iStart);
    UNREFERENCED_PARAMETER(cEntries);
    UNREFERENCED_PARAMETER(pcr);

    if ((iLayerPlane != 0) || (iStart != 0) || (cEntries < 0))
        return 0;

    return 0;
}

PROC WINAPI
DrvGetProcAddress(_In_ LPCSTR lpProcName)
{
    PROC Proc;

    if (lpProcName == NULL)
        return NULL;

    if (lstrcmpA(lpProcName, "VirtGpu3DIoControl") == 0)
        return (PROC)VirtGpu3DIoControl;
    if (lstrcmpA(lpProcName, "VirtGpu3DCurrentIoControl") == 0)
        return (PROC)VirtGpu3DCurrentIoControl;
    if (lstrcmpA(lpProcName, "VirtGpu3DQueryCaps") == 0)
        return (PROC)VirtGpu3DQueryCaps;
    if (lstrcmpA(lpProcName, "VirtGpu3DGetCapset") == 0)
        return (PROC)VirtGpu3DGetCapset;
    if (lstrcmpA(lpProcName, "VirtGpu3DCreateContext") == 0)
        return (PROC)VirtGpu3DCreateContext;
    if (lstrcmpA(lpProcName, "VirtGpu3DDestroyContext") == 0)
        return (PROC)VirtGpu3DDestroyContext;
    if (lstrcmpA(lpProcName, "VirtGpu3DCreateResource") == 0)
        return (PROC)VirtGpu3DCreateResource;
    if (lstrcmpA(lpProcName, "VirtGpu3DDestroyResource") == 0)
        return (PROC)VirtGpu3DDestroyResource;
    if (lstrcmpA(lpProcName, "VirtGpu3DCreateBlob") == 0)
        return (PROC)VirtGpu3DCreateBlob;
    if (lstrcmpA(lpProcName, "VirtGpu3DAssignUuid") == 0)
        return (PROC)VirtGpu3DAssignUuid;
    if (lstrcmpA(lpProcName, "VirtGpu3DMapBlob") == 0)
        return (PROC)VirtGpu3DMapBlob;
    if (lstrcmpA(lpProcName, "VirtGpu3DUnmapBlob") == 0)
        return (PROC)VirtGpu3DUnmapBlob;
    if (lstrcmpA(lpProcName, "VirtGpu3DAttachResource") == 0)
        return (PROC)VirtGpu3DAttachResource;
    if (lstrcmpA(lpProcName, "VirtGpu3DDetachResource") == 0)
        return (PROC)VirtGpu3DDetachResource;
    if (lstrcmpA(lpProcName, "VirtGpu3DTransferToHost") == 0)
        return (PROC)VirtGpu3DTransferToHost;
    if (lstrcmpA(lpProcName, "VirtGpu3DTransferFromHost") == 0)
        return (PROC)VirtGpu3DTransferFromHost;
    if (lstrcmpA(lpProcName, "VirtGpu3DSubmit") == 0)
        return (PROC)VirtGpu3DSubmit;
    if (lstrcmpA(lpProcName, "VirtGpu3DWaitFence") == 0)
        return (PROC)VirtGpu3DWaitFence;

    Proc = VirtGpuOglGetCoreProc(lpProcName);
    if (Proc != NULL)
        return Proc;

    return NULL;
}

VOID WINAPI
DrvReleaseContext(_In_ DHGLRC hglrc)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglValidateContext(hglrc);

    if ((Context != NULL) && (VirtGpuOglCurrentContext() == Context))
        VirtGpuOglSetCurrentContext(NULL);
}

BOOL WINAPI
DrvRealizeLayerPalette(_In_ HDC hdc, _In_ INT iLayerPlane, _In_ BOOL bRealize)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(bRealize);

    return iLayerPlane == 0;
}

const void * WINAPI
DrvSetContext(_In_ HDC hdc, _In_ DHGLRC hglrc, _In_ PFN_SETPROCTABLE callback)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglValidateContext(hglrc);

    UNREFERENCED_PARAMETER(callback);

    if ((Context == NULL) || (hdc == NULL))
        return NULL;

    Context->hdc = hdc;
    Context->PixelFormat = GetPixelFormat(hdc);
    VirtGpuOglUpdateDrawableSize(Context, FALSE);
    VirtGpuOglSetCurrentContext(Context);
    VirtGpuOglInitializeProcTable();
    return &VirtGpuOglProcTable;
}

INT WINAPI
DrvSetLayerPaletteEntries(
    _In_ HDC hdc,
    _In_ INT iLayerPlane,
    _In_ INT iStart,
    _In_ INT cEntries,
    _In_reads_(cEntries) const COLORREF *pcr)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(iLayerPlane);
    UNREFERENCED_PARAMETER(iStart);
    UNREFERENCED_PARAMETER(cEntries);
    UNREFERENCED_PARAMETER(pcr);

    if ((iLayerPlane != 0) || (iStart != 0) || (cEntries < 0))
        return 0;

    return 0;
}

BOOL WINAPI
DrvSetPixelFormat(_In_ HDC hdc, _In_ INT iPixelFormat)
{
    VIRTGPU_3D_CAPS Caps;

    return (iPixelFormat >= 1) &&
           (iPixelFormat <= VIRTGPU_OPENGL_PIXEL_FORMAT_COUNT) &&
           VirtGpuOglQueryCaps(hdc, &Caps);
}

BOOL WINAPI
DrvShareLists(_In_ DHGLRC hglrc1, _In_ DHGLRC hglrc2)
{
    return (VirtGpuOglValidateContext(hglrc1) != NULL) &&
           (VirtGpuOglValidateContext(hglrc2) != NULL);
}

BOOL WINAPI
DrvSwapBuffers(_In_ HDC hdc)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context != NULL) && (Context->hdc == hdc))
        (VOID)VirtGpuOglPresentVirglColorTarget(Context);

    GdiFlush();
    return TRUE;
}

BOOL WINAPI
DrvSwapLayerBuffers(_In_ HDC hdc, _In_ UINT fuPlanes)
{
    UNREFERENCED_PARAMETER(hdc);
    return (fuPlanes == 0) || ((fuPlanes & WGL_SWAP_MAIN_PLANE) != 0);
}
