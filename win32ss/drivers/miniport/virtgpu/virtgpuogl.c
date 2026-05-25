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
#include <GL/glext.h>

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
#define VIRTGPU_OGL_MAX_SHADERS 128
#define VIRTGPU_OGL_MAX_PROGRAMS 64
#define VIRTGPU_OGL_MAX_ATTACHED_SHADERS 8
#define VIRTGPU_OGL_MAX_PROGRAM_BINDINGS 32
#define VIRTGPU_OGL_MAX_UNIFORMS 128
#define VIRTGPU_OGL_MAX_VERTEX_ATTRIBS 16
#define VIRTGPU_OGL_MAX_BUFFERS 256
#define VIRTGPU_OGL_MAX_QUERIES 128
#define VIRTGPU_OGL_MAX_RENDERBUFFERS 128
#define VIRTGPU_OGL_MAX_FRAMEBUFFERS 128
#define VIRTGPU_OGL_MAX_VERTEX_ARRAYS 128
#define VIRTGPU_OGL_MAX_SYNCS 128
#define VIRTGPU_OGL_MAX_SAMPLERS 128
#define VIRTGPU_OGL_MAX_TRANSFORM_FEEDBACKS 64
#define VIRTGPU_OGL_MAX_TEXTURE_UNITS 32
#define VIRTGPU_OGL_MAX_BUFFER_BINDINGS 16
#define VIRTGPU_OGL_MAX_TRANSFORM_FEEDBACK_VARYINGS 16
#define VIRTGPU_OGL_MAX_PATCH_VERTICES 32
#define VIRTGPU_OGL_NAME_STACK_DEPTH 64
#define VIRTGPU_OGL_MAX_EVAL_ORDER 32
#define VIRTGPU_OGL_PIXEL_MAP_COUNT 10
#define VIRTGPU_OGL_MAX_PIXEL_MAP_TABLE 4096
#define VIRTGPU_OGL_EVAL_MAP_COUNT 9
#define VIRTGPU_OGL_COLOR_TABLE_COUNT 3
#define VIRTGPU_OGL_CONVOLUTION_COUNT 3
#define VIRTGPU_OGL_MAX_NAME_LENGTH 64
#define VIRTGPU_OGL_MAX_UNIFORM_VALUES 16
#define VIRTGPU_OGL_FRAMEBUFFER_ATTACHMENTS 3
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
#define VIRTGPU_OGL_CLIENT_SECONDARY_COLOR_ARRAY 0x00000010
#define VIRTGPU_OGL_CLIENT_FOG_COORD_ARRAY 0x00000020

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
    GLint Depth;
    GLenum InternalFormat;
    GLenum Format;
    GLenum Type;
    GLenum MinFilter;
    GLenum MagFilter;
    GLenum WrapS;
    GLenum WrapT;
    GLenum WrapR;
    GLuint BufferName;
    GLenum BufferInternalFormat;
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

typedef struct _VIRTGPU_OGL_SHADER
{
    BOOL Allocated;
    BOOL DeletePending;
    BOOL Compiled;
    GLuint Name;
    GLenum Type;
    ULONG SourceLength;
    GLchar *Source;
} VIRTGPU_OGL_SHADER, *PVIRTGPU_OGL_SHADER;

typedef struct _VIRTGPU_OGL_PROGRAM_BINDING
{
    BOOL InUse;
    GLuint Index;
    GLchar Name[VIRTGPU_OGL_MAX_NAME_LENGTH];
} VIRTGPU_OGL_PROGRAM_BINDING, *PVIRTGPU_OGL_PROGRAM_BINDING;

typedef struct _VIRTGPU_OGL_UNIFORM
{
    BOOL InUse;
    GLint Location;
    GLenum Type;
    GLint Size;
    GLchar Name[VIRTGPU_OGL_MAX_NAME_LENGTH];
    GLfloat FloatValues[VIRTGPU_OGL_MAX_UNIFORM_VALUES];
    GLint IntValues[VIRTGPU_OGL_MAX_UNIFORM_VALUES];
} VIRTGPU_OGL_UNIFORM, *PVIRTGPU_OGL_UNIFORM;

typedef struct _VIRTGPU_OGL_PROGRAM
{
    BOOL Allocated;
    BOOL DeletePending;
    BOOL Linked;
    BOOL Validated;
    GLuint Name;
    GLuint AttachedShaders[VIRTGPU_OGL_MAX_ATTACHED_SHADERS];
    ULONG AttachedShaderCount;
    VIRTGPU_OGL_PROGRAM_BINDING Bindings[VIRTGPU_OGL_MAX_PROGRAM_BINDINGS];
    VIRTGPU_OGL_PROGRAM_BINDING FragDataBindings[VIRTGPU_OGL_MAX_PROGRAM_BINDINGS];
    VIRTGPU_OGL_UNIFORM Uniforms[VIRTGPU_OGL_MAX_UNIFORMS];
    GLint NextUniformLocation;
    GLenum TransformFeedbackBufferMode;
    GLsizei TransformFeedbackVaryingCount;
    GLchar TransformFeedbackVaryings[VIRTGPU_OGL_MAX_TRANSFORM_FEEDBACK_VARYINGS][VIRTGPU_OGL_MAX_NAME_LENGTH];
} VIRTGPU_OGL_PROGRAM, *PVIRTGPU_OGL_PROGRAM;

typedef struct _VIRTGPU_OGL_VERTEX_ATTRIB
{
    GLboolean Enabled;
    GLint Size;
    GLenum Type;
    GLboolean Normalized;
    GLsizei Stride;
    const GLvoid *Pointer;
    GLuint Divisor;
    GLfloat Current[4];
} VIRTGPU_OGL_VERTEX_ATTRIB, *PVIRTGPU_OGL_VERTEX_ATTRIB;

typedef struct _VIRTGPU_OGL_BUFFER
{
    BOOL Allocated;
    GLuint Name;
    GLenum Target;
    GLenum Usage;
    GLenum Access;
    GLsizeiptr Size;
    BYTE *Data;
    BOOL Mapped;
} VIRTGPU_OGL_BUFFER, *PVIRTGPU_OGL_BUFFER;

typedef struct _VIRTGPU_OGL_BUFFER_BINDING
{
    GLuint Buffer;
    GLintptr Offset;
    GLsizeiptr Size;
} VIRTGPU_OGL_BUFFER_BINDING, *PVIRTGPU_OGL_BUFFER_BINDING;

typedef struct _VIRTGPU_OGL_QUERY
{
    BOOL Allocated;
    GLuint Name;
    GLenum Target;
    BOOL Active;
    GLuint Result;
} VIRTGPU_OGL_QUERY, *PVIRTGPU_OGL_QUERY;

typedef struct _VIRTGPU_OGL_RENDERBUFFER
{
    BOOL Allocated;
    GLuint Name;
    GLenum InternalFormat;
    GLsizei Width;
    GLsizei Height;
    GLsizei Samples;
} VIRTGPU_OGL_RENDERBUFFER, *PVIRTGPU_OGL_RENDERBUFFER;

typedef struct _VIRTGPU_OGL_FRAMEBUFFER_ATTACHMENT
{
    GLenum ObjectType;
    GLuint ObjectName;
    GLenum TextureTarget;
    GLint TextureLevel;
    GLint TextureLayer;
} VIRTGPU_OGL_FRAMEBUFFER_ATTACHMENT, *PVIRTGPU_OGL_FRAMEBUFFER_ATTACHMENT;

typedef struct _VIRTGPU_OGL_FRAMEBUFFER
{
    BOOL Allocated;
    GLuint Name;
    VIRTGPU_OGL_FRAMEBUFFER_ATTACHMENT Attachments[VIRTGPU_OGL_FRAMEBUFFER_ATTACHMENTS];
} VIRTGPU_OGL_FRAMEBUFFER, *PVIRTGPU_OGL_FRAMEBUFFER;

typedef struct _VIRTGPU_OGL_VERTEX_ARRAY
{
    BOOL Allocated;
    GLuint Name;
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
    VIRTGPU_OGL_VERTEX_ATTRIB VertexAttribs[VIRTGPU_OGL_MAX_VERTEX_ATTRIBS];
} VIRTGPU_OGL_VERTEX_ARRAY, *PVIRTGPU_OGL_VERTEX_ARRAY;

typedef struct _VIRTGPU_OGL_SYNC
{
    BOOL Allocated;
    GLenum Condition;
    GLbitfield Flags;
    GLenum Status;
} VIRTGPU_OGL_SYNC, *PVIRTGPU_OGL_SYNC;

typedef struct _VIRTGPU_OGL_SAMPLER
{
    BOOL Allocated;
    GLuint Name;
    GLenum MinFilter;
    GLenum MagFilter;
    GLenum WrapS;
    GLenum WrapT;
    GLenum WrapR;
} VIRTGPU_OGL_SAMPLER, *PVIRTGPU_OGL_SAMPLER;

typedef struct _VIRTGPU_OGL_TRANSFORM_FEEDBACK
{
    BOOL Allocated;
    GLuint Name;
    BOOL Active;
    BOOL Paused;
    GLenum PrimitiveMode;
} VIRTGPU_OGL_TRANSFORM_FEEDBACK, *PVIRTGPU_OGL_TRANSFORM_FEEDBACK;

typedef struct _VIRTGPU_OGL_PIXEL_MAP
{
    GLint Size;
    GLfloat *Values;
} VIRTGPU_OGL_PIXEL_MAP, *PVIRTGPU_OGL_PIXEL_MAP;

typedef struct _VIRTGPU_OGL_IMAGE_TABLE
{
    BOOL Defined;
    GLenum Target;
    GLenum InternalFormat;
    GLenum Format;
    GLenum Type;
    GLenum BorderMode;
    GLsizei Width;
    GLsizei Height;
    GLfloat Scale[4];
    GLfloat Bias[4];
    GLfloat BorderColor[4];
    ULONG DataSize;
    BYTE *Data;
} VIRTGPU_OGL_IMAGE_TABLE, *PVIRTGPU_OGL_IMAGE_TABLE;

typedef struct _VIRTGPU_OGL_HISTOGRAM_STATE
{
    BOOL Defined;
    GLenum Target;
    GLenum InternalFormat;
    GLsizei Width;
    GLboolean Sink;
} VIRTGPU_OGL_HISTOGRAM_STATE, *PVIRTGPU_OGL_HISTOGRAM_STATE;

typedef struct _VIRTGPU_OGL_MINMAX_STATE
{
    BOOL Defined;
    GLenum Target;
    GLenum InternalFormat;
    GLboolean Sink;
} VIRTGPU_OGL_MINMAX_STATE, *PVIRTGPU_OGL_MINMAX_STATE;

typedef struct _VIRTGPU_OGL_EVAL_MAP1
{
    BOOL Defined;
    GLenum Target;
    GLint Components;
    GLdouble U1;
    GLdouble U2;
    GLint Order;
    GLdouble *Points;
} VIRTGPU_OGL_EVAL_MAP1, *PVIRTGPU_OGL_EVAL_MAP1;

typedef struct _VIRTGPU_OGL_EVAL_MAP2
{
    BOOL Defined;
    GLenum Target;
    GLint Components;
    GLdouble U1;
    GLdouble U2;
    GLdouble V1;
    GLdouble V2;
    GLint UOrder;
    GLint VOrder;
    GLdouble *Points;
} VIRTGPU_OGL_EVAL_MAP2, *PVIRTGPU_OGL_EVAL_MAP2;

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
    GLfloat PolygonOffsetFactor;
    GLfloat PolygonOffsetUnits;
    GLint LineStippleFactor;
    GLushort LineStipplePattern;
    GLenum StencilFunc;
    GLint StencilRef;
    GLuint StencilValueMask;
    GLenum StencilFail;
    GLenum StencilDepthFail;
    GLenum StencilDepthPass;
    GLint PackAlignment;
    GLint PackRowLength;
    GLint PackSkipRows;
    GLint PackSkipPixels;
    GLboolean PackSwapBytes;
    GLboolean PackLsbFirst;
    GLint UnpackAlignment;
    GLint UnpackRowLength;
    GLint UnpackSkipRows;
    GLint UnpackSkipPixels;
    GLboolean UnpackSwapBytes;
    GLboolean UnpackLsbFirst;
    GLuint NextTextureName;
    GLuint NextListName;
    GLuint NextShaderName;
    GLuint NextProgramName;
    GLuint NextBufferName;
    GLuint NextQueryName;
    GLuint NextRenderbufferName;
    GLuint NextFramebufferName;
    GLuint NextVertexArrayName;
    GLuint NextSamplerName;
    GLuint NextTransformFeedbackName;
    GLuint CurrentProgram;
    GLuint BoundArrayBuffer;
    GLuint BoundElementArrayBuffer;
    GLuint BoundCopyReadBuffer;
    GLuint BoundCopyWriteBuffer;
    GLuint BoundUniformBuffer;
    GLuint BoundTransformFeedbackBuffer;
    GLuint BoundDrawIndirectBuffer;
    GLuint CurrentQuery;
    GLenum ActiveTexture;
    GLenum ClientActiveTexture;
    GLuint ListBase;
    PVIRTGPU_OGL_DISPLAY_LIST RecordingList;
    GLenum RecordingMode;
    GLenum RecordingBeginMode;
    ULONG ListExecuteDepth;
    GLuint BoundTexture1D;
    GLuint BoundTexture2D;
    GLuint BoundTexture3D;
    GLuint BoundTextureBuffer;
    GLuint BoundRenderbuffer;
    GLuint BoundReadFramebuffer;
    GLuint BoundDrawFramebuffer;
    GLuint BoundVertexArray;
    GLuint BoundTransformFeedback;
    GLuint BoundSamplers[VIRTGPU_OGL_MAX_TEXTURE_UNITS];
    VIRTGPU_OGL_BUFFER_BINDING UniformBufferBindings[VIRTGPU_OGL_MAX_BUFFER_BINDINGS];
    VIRTGPU_OGL_BUFFER_BINDING TransformFeedbackBufferBindings[VIRTGPU_OGL_MAX_BUFFER_BINDINGS];
    VIRTGPU_OGL_TEXTURE Textures[VIRTGPU_OGL_MAX_TEXTURES];
    VIRTGPU_OGL_DISPLAY_LIST DisplayLists[VIRTGPU_OGL_MAX_DISPLAY_LISTS];
    VIRTGPU_OGL_SHADER Shaders[VIRTGPU_OGL_MAX_SHADERS];
    VIRTGPU_OGL_PROGRAM Programs[VIRTGPU_OGL_MAX_PROGRAMS];
    VIRTGPU_OGL_VERTEX_ATTRIB VertexAttribs[VIRTGPU_OGL_MAX_VERTEX_ATTRIBS];
    VIRTGPU_OGL_BUFFER Buffers[VIRTGPU_OGL_MAX_BUFFERS];
    VIRTGPU_OGL_QUERY Queries[VIRTGPU_OGL_MAX_QUERIES];
    VIRTGPU_OGL_RENDERBUFFER Renderbuffers[VIRTGPU_OGL_MAX_RENDERBUFFERS];
    VIRTGPU_OGL_FRAMEBUFFER Framebuffers[VIRTGPU_OGL_MAX_FRAMEBUFFERS];
    VIRTGPU_OGL_VERTEX_ARRAY VertexArrays[VIRTGPU_OGL_MAX_VERTEX_ARRAYS];
    VIRTGPU_OGL_SYNC Syncs[VIRTGPU_OGL_MAX_SYNCS];
    VIRTGPU_OGL_SAMPLER Samplers[VIRTGPU_OGL_MAX_SAMPLERS];
    VIRTGPU_OGL_TRANSFORM_FEEDBACK TransformFeedbacks[VIRTGPU_OGL_MAX_TRANSFORM_FEEDBACKS];
    BOOL DefaultTransformFeedbackActive;
    BOOL DefaultTransformFeedbackPaused;
    GLenum DefaultTransformFeedbackPrimitiveMode;
    GLuint PrimitiveRestartIndex;
    GLenum ProvokingVertexMode;
    GLint PatchVertices;
    GLfloat PatchDefaultOuterLevel[4];
    GLfloat PatchDefaultInnerLevel[2];
    BOOL ConditionalRenderActive;
    GLuint ConditionalRenderQuery;
    GLenum ConditionalRenderMode;
    GLenum ClampVertexColor;
    GLenum ClampFragmentColor;
    GLenum ClampReadColor;
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
    GLenum IndexArrayType;
    GLsizei IndexArrayStride;
    const GLvoid *IndexArrayPointer;
    GLsizei EdgeFlagArrayStride;
    const GLvoid *EdgeFlagArrayPointer;
    GLint SecondaryColorArraySize;
    GLenum SecondaryColorArrayType;
    GLsizei SecondaryColorArrayStride;
    const GLvoid *SecondaryColorArrayPointer;
    GLenum FogCoordArrayType;
    GLsizei FogCoordArrayStride;
    const GLvoid *FogCoordArrayPointer;
    GLint TexCoordArraySize;
    GLenum TexCoordArrayType;
    GLsizei TexCoordArrayStride;
    const GLvoid *TexCoordArrayPointer;
    COLORREF CurrentColor;
    GLfloat CurrentAlpha;
    GLfloat CurrentSecondaryColor[3];
    GLfloat CurrentFogCoord;
    GLfloat CurrentIndex;
    GLfloat CurrentNormal[3];
    GLfloat CurrentTexCoord[4];
    GLfloat CurrentRasterPosition[4];
    POINT CurrentRasterWindow;
    GLboolean CurrentRasterPositionValid;
    GLboolean EdgeFlag;
    GLfloat PixelZoomX;
    GLfloat PixelZoomY;
    GLfloat PointSizeMin;
    GLfloat PointSizeMax;
    GLfloat PointFadeThresholdSize;
    GLfloat PointDistanceAttenuation[3];
    GLfloat BlendColor[4];
    GLenum BlendEquationMode;
    GLenum LogicOpMode;
    GLclampf ClearAccum[4];
    GLfloat ClearIndex;
    GLuint IndexMask;
    GLdouble ClipPlanes[6][4];
    GLenum ColorMaterialFace;
    GLenum ColorMaterialMode;
    GLenum FogMode;
    GLfloat FogDensity;
    GLfloat FogStart;
    GLfloat FogEnd;
    GLfloat FogIndex;
    GLfloat FogColor[4];
    GLenum TexEnvMode;
    GLfloat TexEnvColor[4];
    GLenum TexGenMode[4];
    GLdouble TexGenObjectPlane[4][4];
    GLdouble TexGenEyePlane[4][4];
    BYTE PolygonStipple[128];
    VIRTGPU_OGL_PIXEL_MAP PixelMaps[VIRTGPU_OGL_PIXEL_MAP_COUNT];
    VIRTGPU_OGL_IMAGE_TABLE ColorTables[VIRTGPU_OGL_COLOR_TABLE_COUNT];
    VIRTGPU_OGL_IMAGE_TABLE ConvolutionFilters[VIRTGPU_OGL_CONVOLUTION_COUNT];
    VIRTGPU_OGL_HISTOGRAM_STATE Histogram;
    VIRTGPU_OGL_MINMAX_STATE Minmax;
    VIRTGPU_OGL_EVAL_MAP1 EvalMap1[VIRTGPU_OGL_EVAL_MAP_COUNT];
    VIRTGPU_OGL_EVAL_MAP2 EvalMap2[VIRTGPU_OGL_EVAL_MAP_COUNT];
    ULONGLONG EvalEnableBits;
    GLenum RenderMode;
    GLsizei FeedbackBufferSize;
    GLenum FeedbackBufferType;
    GLfloat *FeedbackBuffer;
    GLsizei FeedbackBufferUsed;
    GLsizei SelectBufferSize;
    GLuint *SelectBuffer;
    GLuint SelectHits;
    GLuint NameStack[VIRTGPU_OGL_NAME_STACK_DEPTH];
    ULONG NameStackDepth;
    GLdouble MapGrid1[3];
    GLdouble MapGrid2[6];
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
VirtGpuOglTexEnvi(GLenum Arg0, GLenum Arg1, GLint Arg2);

static void APIENTRY
VirtGpuOglTexParameteriv(GLenum Arg0, GLenum Arg1, const GLint *Arg2);

static void APIENTRY
VirtGpuOglConvolutionParameterfv(GLenum Arg0, GLenum Arg1, const GLfloat *Arg2);

static void APIENTRY
VirtGpuOglGetHistogramParameteriv(GLenum Arg0, GLenum Arg1, GLint *Arg2);

static void APIENTRY
VirtGpuOglGetMinmaxParameteriv(GLenum Arg0, GLenum Arg1, GLint *Arg2);

static void APIENTRY
VirtGpuOglGetTexParameteriv(GLenum Arg0, GLenum Arg1, GLint *Arg2);

static void APIENTRY
VirtGpuOglGetTexLevelParameteriv(GLenum Arg0, GLint Arg1, GLenum Arg2, GLint *Arg3);

static void APIENTRY
VirtGpuOglArrayElement(GLint Arg0);

static void APIENTRY
VirtGpuOglEvalPoint1(GLint Arg0);

static void APIENTRY
VirtGpuOglEvalPoint2(GLint Arg0, GLint Arg1);

static void APIENTRY
VirtGpuOglDrawElements(GLenum Arg0, GLsizei Arg1, GLenum Arg2, const GLvoid *Arg3);

static void APIENTRY
VirtGpuOglDrawPixels(GLsizei Arg0, GLsizei Arg1, GLenum Arg2, GLenum Arg3, const GLvoid *Arg4);

static void APIENTRY
VirtGpuOglReadPixels(GLint Arg0, GLint Arg1, GLsizei Arg2, GLsizei Arg3, GLenum Arg4, GLenum Arg5, GLvoid *Arg6);

static void APIENTRY
VirtGpuOglFogi(GLenum Arg0, GLint Arg1);

static VOID APIENTRY
VirtGpuOglGetBooleanv(GLenum Pname, GLboolean *Params);

static VOID APIENTRY
VirtGpuOglGetIntegerv(GLenum Pname, GLint *Params);

static void APIENTRY
VirtGpuOglGetQueryObjectuiv(GLuint Arg0, GLenum Arg1, GLuint *Arg2);

static void APIENTRY
VirtGpuOglTexCoordPointer(GLint Arg0, GLenum Arg1, GLsizei Arg2, const GLvoid *Arg3);

static void APIENTRY
VirtGpuOglVertexPointer(GLint Arg0, GLenum Arg1, GLsizei Arg2, const GLvoid *Arg3);

static GLboolean APIENTRY
VirtGpuOglIsTexture(GLuint Arg0);

static GLboolean APIENTRY
VirtGpuOglIsEnabled(GLenum Cap);

static BOOL
VirtGpuOglRecordingCompileOnly(_In_opt_ PVIRTGPU_OGL_CONTEXT Context);

static void APIENTRY
VirtGpuOglColor3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2);

static void APIENTRY
VirtGpuOglColor4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3);

static void APIENTRY
VirtGpuOglIndexf(GLfloat Arg0);

static void APIENTRY
VirtGpuOglSecondaryColor3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2);

static void APIENTRY
VirtGpuOglNormal3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2);

static void APIENTRY
VirtGpuOglTexCoord4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3);

static void APIENTRY
VirtGpuOglVertex4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3);

static void APIENTRY
VirtGpuOglWindowPos3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2);

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
VirtGpuOglValidLogicOp(_In_ GLenum Operation)
{
    switch (Operation)
    {
        case GL_CLEAR:
        case GL_SET:
        case GL_COPY:
        case GL_COPY_INVERTED:
        case GL_NOOP:
        case GL_AND:
        case GL_NAND:
        case GL_OR:
        case GL_NOR:
        case GL_XOR:
        case GL_EQUIV:
        case GL_AND_REVERSE:
        case GL_AND_INVERTED:
        case GL_OR_REVERSE:
        case GL_OR_INVERTED:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglPixelMapToIndex(_In_ GLenum Map, _Out_ PULONG Index)
{
    switch (Map)
    {
        case GL_PIXEL_MAP_I_TO_I:
            *Index = 0;
            return TRUE;
        case GL_PIXEL_MAP_S_TO_S:
            *Index = 1;
            return TRUE;
        case GL_PIXEL_MAP_I_TO_R:
            *Index = 2;
            return TRUE;
        case GL_PIXEL_MAP_I_TO_G:
            *Index = 3;
            return TRUE;
        case GL_PIXEL_MAP_I_TO_B:
            *Index = 4;
            return TRUE;
        case GL_PIXEL_MAP_I_TO_A:
            *Index = 5;
            return TRUE;
        case GL_PIXEL_MAP_R_TO_R:
            *Index = 6;
            return TRUE;
        case GL_PIXEL_MAP_G_TO_G:
            *Index = 7;
            return TRUE;
        case GL_PIXEL_MAP_B_TO_B:
            *Index = 8;
            return TRUE;
        case GL_PIXEL_MAP_A_TO_A:
            *Index = 9;
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglPixelMapSizePnameToMap(_In_ GLenum Pname, _Out_ GLenum *Map)
{
    switch (Pname)
    {
        case GL_PIXEL_MAP_I_TO_I_SIZE:
            *Map = GL_PIXEL_MAP_I_TO_I;
            return TRUE;
        case GL_PIXEL_MAP_S_TO_S_SIZE:
            *Map = GL_PIXEL_MAP_S_TO_S;
            return TRUE;
        case GL_PIXEL_MAP_I_TO_R_SIZE:
            *Map = GL_PIXEL_MAP_I_TO_R;
            return TRUE;
        case GL_PIXEL_MAP_I_TO_G_SIZE:
            *Map = GL_PIXEL_MAP_I_TO_G;
            return TRUE;
        case GL_PIXEL_MAP_I_TO_B_SIZE:
            *Map = GL_PIXEL_MAP_I_TO_B;
            return TRUE;
        case GL_PIXEL_MAP_I_TO_A_SIZE:
            *Map = GL_PIXEL_MAP_I_TO_A;
            return TRUE;
        case GL_PIXEL_MAP_R_TO_R_SIZE:
            *Map = GL_PIXEL_MAP_R_TO_R;
            return TRUE;
        case GL_PIXEL_MAP_G_TO_G_SIZE:
            *Map = GL_PIXEL_MAP_G_TO_G;
            return TRUE;
        case GL_PIXEL_MAP_B_TO_B_SIZE:
            *Map = GL_PIXEL_MAP_B_TO_B;
            return TRUE;
        case GL_PIXEL_MAP_A_TO_A_SIZE:
            *Map = GL_PIXEL_MAP_A_TO_A;
            return TRUE;
        default:
            return FALSE;
    }
}

static VOID
VirtGpuOglFreePixelMap(_Inout_ PVIRTGPU_OGL_PIXEL_MAP Map)
{
    if (Map->Values != NULL)
        HeapFree(GetProcessHeap(), 0, Map->Values);
    Map->Values = NULL;
    Map->Size = 0;
}

static BOOL
VirtGpuOglEvalMapTargetComponents(
    _In_ GLenum Target,
    _In_ BOOL Map2,
    _Out_ PULONG Index,
    _Out_ GLint *Components)
{
    GLenum Base = Map2 ? GL_MAP2_COLOR_4 : GL_MAP1_COLOR_4;
    GLenum Last = Map2 ? GL_MAP2_VERTEX_4 : GL_MAP1_VERTEX_4;

    if ((Target < Base) || (Target > Last))
        return FALSE;

    *Index = Target - Base;
    switch (*Index)
    {
        case GL_MAP1_COLOR_4 - GL_MAP1_COLOR_4:
            *Components = 4;
            return TRUE;
        case GL_MAP1_INDEX - GL_MAP1_COLOR_4:
            *Components = 1;
            return TRUE;
        case GL_MAP1_NORMAL - GL_MAP1_COLOR_4:
            *Components = 3;
            return TRUE;
        case GL_MAP1_TEXTURE_COORD_1 - GL_MAP1_COLOR_4:
            *Components = 1;
            return TRUE;
        case GL_MAP1_TEXTURE_COORD_2 - GL_MAP1_COLOR_4:
            *Components = 2;
            return TRUE;
        case GL_MAP1_TEXTURE_COORD_3 - GL_MAP1_COLOR_4:
            *Components = 3;
            return TRUE;
        case GL_MAP1_TEXTURE_COORD_4 - GL_MAP1_COLOR_4:
            *Components = 4;
            return TRUE;
        case GL_MAP1_VERTEX_3 - GL_MAP1_COLOR_4:
            *Components = 3;
            return TRUE;
        case GL_MAP1_VERTEX_4 - GL_MAP1_COLOR_4:
            *Components = 4;
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglEvalCapToBit(_In_ GLenum Cap, _Out_ ULONGLONG *Bit)
{
    ULONG Index;
    GLint Components;

    if (VirtGpuOglEvalMapTargetComponents(Cap, FALSE, &Index, &Components))
    {
        *Bit = 1ULL << Index;
        return TRUE;
    }

    if (VirtGpuOglEvalMapTargetComponents(Cap, TRUE, &Index, &Components))
    {
        *Bit = 1ULL << (VIRTGPU_OGL_EVAL_MAP_COUNT + Index);
        return TRUE;
    }

    if (Cap == GL_AUTO_NORMAL)
    {
        *Bit = 1ULL << (VIRTGPU_OGL_EVAL_MAP_COUNT * 2);
        return TRUE;
    }

    return FALSE;
}

static VOID
VirtGpuOglFreeEvalMap1(_Inout_ PVIRTGPU_OGL_EVAL_MAP1 Map)
{
    if (Map->Points != NULL)
        HeapFree(GetProcessHeap(), 0, Map->Points);
    ZeroMemory(Map, sizeof(*Map));
}

static VOID
VirtGpuOglFreeEvalMap2(_Inout_ PVIRTGPU_OGL_EVAL_MAP2 Map)
{
    if (Map->Points != NULL)
        HeapFree(GetProcessHeap(), 0, Map->Points);
    ZeroMemory(Map, sizeof(*Map));
}

static GLdouble
VirtGpuOglEvalParameter(_In_ GLdouble Value, _In_ GLdouble Low, _In_ GLdouble High)
{
    if (High == Low)
        return 0.0;
    return (Value - Low) / (High - Low);
}

static VOID
VirtGpuOglDeCasteljau(
    _In_reads_(VIRTGPU_OGL_MAX_EVAL_ORDER * 4) const GLdouble *Source,
    _In_ GLint Order,
    _In_ GLint Components,
    _In_ GLdouble T,
    _Out_writes_(4) GLdouble *Result)
{
    GLdouble Temp[VIRTGPU_OGL_MAX_EVAL_ORDER][4];
    GLint I;
    GLint K;
    GLint Component;

    if (T < 0.0)
        T = 0.0;
    else if (T > 1.0)
        T = 1.0;

    for (I = 0; I < Order; ++I)
    {
        for (Component = 0; Component < Components; ++Component)
            Temp[I][Component] = Source[(I * Components) + Component];
    }

    for (K = 1; K < Order; ++K)
    {
        for (I = 0; I < Order - K; ++I)
        {
            for (Component = 0; Component < Components; ++Component)
            {
                Temp[I][Component] =
                    (Temp[I][Component] * (1.0 - T)) +
                    (Temp[I + 1][Component] * T);
            }
        }
    }

    Result[0] = 0.0;
    Result[1] = 0.0;
    Result[2] = 0.0;
    Result[3] = 1.0;
    for (Component = 0; Component < Components; ++Component)
        Result[Component] = Temp[0][Component];
}

static VOID
VirtGpuOglApplyEvalResult(
    _In_ GLenum Target,
    _In_reads_(4) const GLdouble *Result)
{
    switch (Target)
    {
        case GL_MAP1_COLOR_4:
        case GL_MAP2_COLOR_4:
            VirtGpuOglColor4f((GLfloat)Result[0],
                              (GLfloat)Result[1],
                              (GLfloat)Result[2],
                              (GLfloat)Result[3]);
            break;
        case GL_MAP1_INDEX:
        case GL_MAP2_INDEX:
            VirtGpuOglIndexf((GLfloat)Result[0]);
            break;
        case GL_MAP1_NORMAL:
        case GL_MAP2_NORMAL:
            VirtGpuOglNormal3f((GLfloat)Result[0],
                               (GLfloat)Result[1],
                               (GLfloat)Result[2]);
            break;
        case GL_MAP1_TEXTURE_COORD_1:
        case GL_MAP2_TEXTURE_COORD_1:
            VirtGpuOglTexCoord4f((GLfloat)Result[0], 0.0f, 0.0f, 1.0f);
            break;
        case GL_MAP1_TEXTURE_COORD_2:
        case GL_MAP2_TEXTURE_COORD_2:
            VirtGpuOglTexCoord4f((GLfloat)Result[0], (GLfloat)Result[1], 0.0f, 1.0f);
            break;
        case GL_MAP1_TEXTURE_COORD_3:
        case GL_MAP2_TEXTURE_COORD_3:
            VirtGpuOglTexCoord4f((GLfloat)Result[0],
                                 (GLfloat)Result[1],
                                 (GLfloat)Result[2],
                                 1.0f);
            break;
        case GL_MAP1_TEXTURE_COORD_4:
        case GL_MAP2_TEXTURE_COORD_4:
            VirtGpuOglTexCoord4f((GLfloat)Result[0],
                                 (GLfloat)Result[1],
                                 (GLfloat)Result[2],
                                 (GLfloat)Result[3]);
            break;
        case GL_MAP1_VERTEX_3:
        case GL_MAP2_VERTEX_3:
            VirtGpuOglVertex4f((GLfloat)Result[0],
                               (GLfloat)Result[1],
                               (GLfloat)Result[2],
                               1.0f);
            break;
        case GL_MAP1_VERTEX_4:
        case GL_MAP2_VERTEX_4:
            VirtGpuOglVertex4f((GLfloat)Result[0],
                               (GLfloat)Result[1],
                               (GLfloat)Result[2],
                               (GLfloat)Result[3]);
            break;
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
        case GL_SECONDARY_COLOR_ARRAY:
            *Bit = VIRTGPU_OGL_CLIENT_SECONDARY_COLOR_ARRAY;
            return TRUE;
        case GL_FOG_COORD_ARRAY:
            *Bit = VIRTGPU_OGL_CLIENT_FOG_COORD_ARRAY;
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
    Texture->Depth = 1;
    Texture->MinFilter = GL_NEAREST_MIPMAP_LINEAR;
    Texture->MagFilter = GL_LINEAR;
    Texture->WrapS = GL_REPEAT;
    Texture->WrapT = GL_REPEAT;
    Texture->WrapR = GL_REPEAT;
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

static ULONG
VirtGpuOglStringLength(_In_opt_z_ const GLchar *String)
{
    ULONG Length = 0;

    if (String == NULL)
        return 0;

    while (String[Length] != 0)
        ++Length;
    return Length;
}

static BOOL
VirtGpuOglStringEquals(_In_opt_z_ const GLchar *Left, _In_opt_z_ const GLchar *Right)
{
    ULONG Index = 0;

    if ((Left == NULL) || (Right == NULL))
        return FALSE;

    while ((Left[Index] != 0) && (Right[Index] != 0))
    {
        if (Left[Index] != Right[Index])
            return FALSE;
        ++Index;
    }

    return Left[Index] == Right[Index];
}

static BOOL
VirtGpuOglReservedName(_In_opt_z_ const GLchar *Name)
{
    return (Name != NULL) &&
           (Name[0] == 'g') &&
           (Name[1] == 'l') &&
           (Name[2] == '_');
}

static VOID
VirtGpuOglCopyFixedName(
    _Out_writes_(DestinationLength) GLchar *Destination,
    _In_ ULONG DestinationLength,
    _In_z_ const GLchar *Source)
{
    ULONG Index;

    if ((Destination == NULL) || (DestinationLength == 0))
        return;

    for (Index = 0; (Index + 1 < DestinationLength) && (Source[Index] != 0); ++Index)
        Destination[Index] = Source[Index];
    Destination[Index] = 0;
}

static VOID
VirtGpuOglCopyNameResult(
    _In_z_ const GLchar *Source,
    _In_ GLsizei BufferSize,
    _Out_opt_ GLsizei *Length,
    _Out_writes_opt_(BufferSize) GLchar *Destination)
{
    ULONG SourceLength;
    ULONG CopyLength = 0;

    SourceLength = VirtGpuOglStringLength(Source);
    if (Length != NULL)
        *Length = (GLsizei)SourceLength;

    if ((Destination == NULL) || (BufferSize <= 0))
        return;

    CopyLength = SourceLength;
    if (CopyLength >= (ULONG)BufferSize)
        CopyLength = (ULONG)BufferSize - 1;

    if (CopyLength != 0)
        CopyMemory(Destination, Source, CopyLength);
    Destination[CopyLength] = 0;

    if (Length != NULL)
        *Length = (GLsizei)CopyLength;
}

static VOID
VirtGpuOglCopyEmptyInfoLog(
    _In_ GLsizei BufferSize,
    _Out_opt_ GLsizei *Length,
    _Out_writes_opt_(BufferSize) GLchar *InfoLog)
{
    if (Length != NULL)
        *Length = 0;

    if ((InfoLog != NULL) && (BufferSize > 0))
        InfoLog[0] = 0;
}

static PVIRTGPU_OGL_SHADER
VirtGpuOglFindShader(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_SHADERS; ++Index)
    {
        if (Context->Shaders[Index].Allocated &&
            (Context->Shaders[Index].Name == Name))
        {
            return &Context->Shaders[Index];
        }
    }

    return NULL;
}

static VOID
VirtGpuOglFreeShader(_Inout_ PVIRTGPU_OGL_SHADER Shader)
{
    if (Shader->Source != NULL)
        HeapFree(GetProcessHeap(), 0, Shader->Source);
    ZeroMemory(Shader, sizeof(*Shader));
}

static BOOL
VirtGpuOglProgramHasShader(
    _In_ PVIRTGPU_OGL_PROGRAM Program,
    _In_ GLuint ShaderName)
{
    ULONG Index;

    for (Index = 0; Index < Program->AttachedShaderCount; ++Index)
    {
        if (Program->AttachedShaders[Index] == ShaderName)
            return TRUE;
    }

    return FALSE;
}

static BOOL
VirtGpuOglShaderAttachedToAnotherProgram(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLuint ShaderName,
    _In_opt_ PVIRTGPU_OGL_PROGRAM IgnoredProgram)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAMS; ++Index)
    {
        if (Context->Programs[Index].Allocated &&
            (&Context->Programs[Index] != IgnoredProgram) &&
            VirtGpuOglProgramHasShader(&Context->Programs[Index], ShaderName))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static PVIRTGPU_OGL_SHADER
VirtGpuOglAllocateShader(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLenum Type)
{
    ULONG Index;
    ULONG Attempts;
    GLuint Name;

    if ((Type != GL_VERTEX_SHADER) && (Type != GL_FRAGMENT_SHADER))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return NULL;
    }

    for (Index = 0; Index < VIRTGPU_OGL_MAX_SHADERS; ++Index)
    {
        if (!Context->Shaders[Index].Allocated)
        {
            Name = 0;
            for (Attempts = 0; Attempts <= VIRTGPU_OGL_MAX_SHADERS; ++Attempts)
            {
                Name = Context->NextShaderName++;
                if (Context->NextShaderName == 0)
                    Context->NextShaderName = 1;
                if ((Name != 0) && (VirtGpuOglFindShader(Context, Name) == NULL))
                    break;
                Name = 0;
            }
            if (Name == 0)
            {
                VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
                return NULL;
            }

            ZeroMemory(&Context->Shaders[Index], sizeof(Context->Shaders[Index]));
            Context->Shaders[Index].Allocated = TRUE;
            Context->Shaders[Index].Name = Name;
            Context->Shaders[Index].Type = Type;
            return &Context->Shaders[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static PVIRTGPU_OGL_PROGRAM
VirtGpuOglFindProgram(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAMS; ++Index)
    {
        if (Context->Programs[Index].Allocated &&
            (Context->Programs[Index].Name == Name))
        {
            return &Context->Programs[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_PROGRAM
VirtGpuOglAllocateProgram(_Inout_ PVIRTGPU_OGL_CONTEXT Context)
{
    ULONG Index;
    ULONG Attempts;
    GLuint Name;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAMS; ++Index)
    {
        if (!Context->Programs[Index].Allocated)
        {
            Name = 0;
            for (Attempts = 0; Attempts <= VIRTGPU_OGL_MAX_PROGRAMS; ++Attempts)
            {
                Name = Context->NextProgramName++;
                if (Context->NextProgramName == 0)
                    Context->NextProgramName = 1;
                if ((Name != 0) && (VirtGpuOglFindProgram(Context, Name) == NULL))
                    break;
                Name = 0;
            }
            if (Name == 0)
            {
                VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
                return NULL;
            }

            ZeroMemory(&Context->Programs[Index], sizeof(Context->Programs[Index]));
            Context->Programs[Index].Allocated = TRUE;
            Context->Programs[Index].Name = Name;
            Context->Programs[Index].TransformFeedbackBufferMode = GL_INTERLEAVED_ATTRIBS;
            return &Context->Programs[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static VOID
VirtGpuOglFreeProgram(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _Inout_ PVIRTGPU_OGL_PROGRAM Program)
{
    PVIRTGPU_OGL_SHADER Shader;
    ULONG Index;
    GLuint ShaderName;

    for (Index = 0; Index < Program->AttachedShaderCount; ++Index)
    {
        ShaderName = Program->AttachedShaders[Index];
        Shader = VirtGpuOglFindShader(Context, ShaderName);
        if ((Shader != NULL) &&
            Shader->DeletePending &&
            !VirtGpuOglShaderAttachedToAnotherProgram(Context, ShaderName, Program))
        {
            VirtGpuOglFreeShader(Shader);
        }
    }

    if (Context->CurrentProgram == Program->Name)
        Context->CurrentProgram = 0;

    ZeroMemory(Program, sizeof(*Program));
}

static PVIRTGPU_OGL_PROGRAM_BINDING
VirtGpuOglFindAttribBinding(
    _Inout_ PVIRTGPU_OGL_PROGRAM Program,
    _In_z_ const GLchar *Name)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAM_BINDINGS; ++Index)
    {
        if (Program->Bindings[Index].InUse &&
            VirtGpuOglStringEquals(Program->Bindings[Index].Name, Name))
        {
            return &Program->Bindings[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_PROGRAM_BINDING
VirtGpuOglAllocateAttribBinding(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _Inout_ PVIRTGPU_OGL_PROGRAM Program)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAM_BINDINGS; ++Index)
    {
        if (!Program->Bindings[Index].InUse)
        {
            Program->Bindings[Index].InUse = TRUE;
            return &Program->Bindings[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static PVIRTGPU_OGL_PROGRAM_BINDING
VirtGpuOglFindFragDataBinding(
    _Inout_ PVIRTGPU_OGL_PROGRAM Program,
    _In_z_ const GLchar *Name)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAM_BINDINGS; ++Index)
    {
        if (Program->FragDataBindings[Index].InUse &&
            VirtGpuOglStringEquals(Program->FragDataBindings[Index].Name, Name))
        {
            return &Program->FragDataBindings[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_PROGRAM_BINDING
VirtGpuOglAllocateFragDataBinding(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _Inout_ PVIRTGPU_OGL_PROGRAM Program)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAM_BINDINGS; ++Index)
    {
        if (!Program->FragDataBindings[Index].InUse)
        {
            Program->FragDataBindings[Index].InUse = TRUE;
            return &Program->FragDataBindings[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static PVIRTGPU_OGL_UNIFORM
VirtGpuOglFindUniformByName(
    _Inout_ PVIRTGPU_OGL_PROGRAM Program,
    _In_z_ const GLchar *Name)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_UNIFORMS; ++Index)
    {
        if (Program->Uniforms[Index].InUse &&
            VirtGpuOglStringEquals(Program->Uniforms[Index].Name, Name))
        {
            return &Program->Uniforms[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_UNIFORM
VirtGpuOglFindUniformByLocation(
    _Inout_ PVIRTGPU_OGL_PROGRAM Program,
    _In_ GLint Location)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_UNIFORMS; ++Index)
    {
        if (Program->Uniforms[Index].InUse &&
            (Program->Uniforms[Index].Location == Location))
        {
            return &Program->Uniforms[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_UNIFORM
VirtGpuOglAllocateUniform(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _Inout_ PVIRTGPU_OGL_PROGRAM Program,
    _In_z_ const GLchar *Name)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_UNIFORMS; ++Index)
    {
        if (!Program->Uniforms[Index].InUse)
        {
            Program->Uniforms[Index].InUse = TRUE;
            Program->Uniforms[Index].Location = Program->NextUniformLocation++;
            Program->Uniforms[Index].Type = GL_FLOAT;
            Program->Uniforms[Index].Size = 1;
            Program->Uniforms[Index].FloatValues[3] = 1.0f;
            VirtGpuOglCopyFixedName(Program->Uniforms[Index].Name,
                                    VIRTGPU_OGL_MAX_NAME_LENGTH,
                                    Name);
            return &Program->Uniforms[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static ULONG
VirtGpuOglUniformComponentCount(_In_ GLenum Type)
{
    switch (Type)
    {
        case GL_FLOAT:
        case GL_DOUBLE:
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_BOOL:
            return 1;
        case GL_FLOAT_VEC2:
        case GL_DOUBLE_VEC2:
        case GL_INT_VEC2:
        case GL_UNSIGNED_INT_VEC2:
        case GL_BOOL_VEC2:
            return 2;
        case GL_FLOAT_VEC3:
        case GL_DOUBLE_VEC3:
        case GL_INT_VEC3:
        case GL_UNSIGNED_INT_VEC3:
        case GL_BOOL_VEC3:
            return 3;
        case GL_FLOAT_VEC4:
        case GL_DOUBLE_VEC4:
        case GL_INT_VEC4:
        case GL_UNSIGNED_INT_VEC4:
        case GL_BOOL_VEC4:
        case GL_FLOAT_MAT2:
        case GL_DOUBLE_MAT2:
            return 4;
        case GL_FLOAT_MAT2x3:
        case GL_FLOAT_MAT3x2:
        case GL_DOUBLE_MAT2x3:
        case GL_DOUBLE_MAT3x2:
            return 6;
        case GL_FLOAT_MAT2x4:
        case GL_FLOAT_MAT4x2:
        case GL_DOUBLE_MAT2x4:
        case GL_DOUBLE_MAT4x2:
            return 8;
        case GL_FLOAT_MAT3:
        case GL_DOUBLE_MAT3:
            return 9;
        case GL_FLOAT_MAT3x4:
        case GL_FLOAT_MAT4x3:
        case GL_DOUBLE_MAT3x4:
        case GL_DOUBLE_MAT4x3:
            return 12;
        case GL_FLOAT_MAT4:
        case GL_DOUBLE_MAT4:
            return 16;
        default:
            return 1;
    }
}

static ULONG
VirtGpuOglActiveUniformCount(_In_ PVIRTGPU_OGL_PROGRAM Program)
{
    ULONG Index;
    ULONG Count = 0;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_UNIFORMS; ++Index)
    {
        if (Program->Uniforms[Index].InUse)
            ++Count;
    }

    return Count;
}

static ULONG
VirtGpuOglActiveAttribCount(_In_ PVIRTGPU_OGL_PROGRAM Program)
{
    ULONG Index;
    ULONG Count = 0;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAM_BINDINGS; ++Index)
    {
        if (Program->Bindings[Index].InUse)
            ++Count;
    }

    return Count;
}

static VOID
VirtGpuOglFreeBuffer(_Inout_ PVIRTGPU_OGL_BUFFER Buffer)
{
    if (Buffer->Data != NULL)
        HeapFree(GetProcessHeap(), 0, Buffer->Data);
    ZeroMemory(Buffer, sizeof(*Buffer));
}

static PVIRTGPU_OGL_BUFFER
VirtGpuOglFindBuffer(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_BUFFERS; ++Index)
    {
        if (Context->Buffers[Index].Allocated &&
            (Context->Buffers[Index].Name == Name))
        {
            return &Context->Buffers[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_BUFFER
VirtGpuOglAllocateBufferName(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_BUFFERS; ++Index)
    {
        if (!Context->Buffers[Index].Allocated)
        {
            ZeroMemory(&Context->Buffers[Index], sizeof(Context->Buffers[Index]));
            Context->Buffers[Index].Allocated = TRUE;
            Context->Buffers[Index].Name = Name;
            Context->Buffers[Index].Usage = GL_STATIC_DRAW;
            Context->Buffers[Index].Access = GL_READ_WRITE;
            return &Context->Buffers[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static BOOL
VirtGpuOglBufferTargetBinding(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLenum Target,
    _Out_ GLuint *Binding)
{
    switch (Target)
    {
        case GL_ARRAY_BUFFER:
            *Binding = Context->BoundArrayBuffer;
            return TRUE;
        case GL_ELEMENT_ARRAY_BUFFER:
            *Binding = Context->BoundElementArrayBuffer;
            return TRUE;
        case GL_COPY_READ_BUFFER:
            *Binding = Context->BoundCopyReadBuffer;
            return TRUE;
        case GL_COPY_WRITE_BUFFER:
            *Binding = Context->BoundCopyWriteBuffer;
            return TRUE;
        case GL_UNIFORM_BUFFER:
            *Binding = Context->BoundUniformBuffer;
            return TRUE;
        case GL_TRANSFORM_FEEDBACK_BUFFER:
            *Binding = Context->BoundTransformFeedbackBuffer;
            return TRUE;
        case GL_DRAW_INDIRECT_BUFFER:
            *Binding = Context->BoundDrawIndirectBuffer;
            return TRUE;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return FALSE;
    }
}

static PVIRTGPU_OGL_BUFFER
VirtGpuOglBoundBuffer(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLenum Target)
{
    GLuint Name;

    if (!VirtGpuOglBufferTargetBinding(Context, Target, &Name))
        return NULL;

    if (Name == 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return NULL;
    }

    return VirtGpuOglFindBuffer(Context, Name);
}

static BOOL
VirtGpuOglBufferRangeValid(
    _In_ GLintptr Offset,
    _In_ GLsizeiptr Size,
    _In_ GLsizeiptr BufferSize)
{
    if ((Offset < 0) || (Size < 0) || (BufferSize < 0))
        return FALSE;

    return ((ULONGLONG)Offset <= (ULONGLONG)BufferSize) &&
           ((ULONGLONG)Size <= ((ULONGLONG)BufferSize - (ULONGLONG)Offset));
}

static PVIRTGPU_OGL_QUERY
VirtGpuOglFindQuery(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_QUERIES; ++Index)
    {
        if (Context->Queries[Index].Allocated &&
            (Context->Queries[Index].Name == Name))
        {
            return &Context->Queries[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_QUERY
VirtGpuOglAllocateQueryName(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_QUERIES; ++Index)
    {
        if (!Context->Queries[Index].Allocated)
        {
            ZeroMemory(&Context->Queries[Index], sizeof(Context->Queries[Index]));
            Context->Queries[Index].Allocated = TRUE;
            Context->Queries[Index].Name = Name;
            return &Context->Queries[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static BOOL
VirtGpuOglTextureUnitIndex(_In_ GLenum Unit, _Out_ PULONG Index)
{
    if ((Unit < GL_TEXTURE0) || (Unit > GL_TEXTURE31))
        return FALSE;

    *Index = (ULONG)(Unit - GL_TEXTURE0);
    return TRUE;
}

static PVIRTGPU_OGL_RENDERBUFFER
VirtGpuOglFindRenderbuffer(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_RENDERBUFFERS; ++Index)
    {
        if (Context->Renderbuffers[Index].Allocated &&
            (Context->Renderbuffers[Index].Name == Name))
        {
            return &Context->Renderbuffers[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_RENDERBUFFER
VirtGpuOglAllocateRenderbufferName(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_RENDERBUFFERS; ++Index)
    {
        if (!Context->Renderbuffers[Index].Allocated)
        {
            ZeroMemory(&Context->Renderbuffers[Index], sizeof(Context->Renderbuffers[Index]));
            Context->Renderbuffers[Index].Allocated = TRUE;
            Context->Renderbuffers[Index].Name = Name;
            return &Context->Renderbuffers[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static PVIRTGPU_OGL_FRAMEBUFFER
VirtGpuOglFindFramebuffer(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_FRAMEBUFFERS; ++Index)
    {
        if (Context->Framebuffers[Index].Allocated &&
            (Context->Framebuffers[Index].Name == Name))
        {
            return &Context->Framebuffers[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_FRAMEBUFFER
VirtGpuOglAllocateFramebufferName(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_FRAMEBUFFERS; ++Index)
    {
        if (!Context->Framebuffers[Index].Allocated)
        {
            ZeroMemory(&Context->Framebuffers[Index], sizeof(Context->Framebuffers[Index]));
            Context->Framebuffers[Index].Allocated = TRUE;
            Context->Framebuffers[Index].Name = Name;
            return &Context->Framebuffers[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static BOOL
VirtGpuOglFramebufferTargetBinding(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLenum Target,
    _Out_ GLuint *ReadBinding,
    _Out_ GLuint *DrawBinding)
{
    switch (Target)
    {
        case GL_FRAMEBUFFER:
            *ReadBinding = Context->BoundReadFramebuffer;
            *DrawBinding = Context->BoundDrawFramebuffer;
            return TRUE;
        case GL_READ_FRAMEBUFFER:
            *ReadBinding = Context->BoundReadFramebuffer;
            *DrawBinding = Context->BoundReadFramebuffer;
            return TRUE;
        case GL_DRAW_FRAMEBUFFER:
            *ReadBinding = Context->BoundDrawFramebuffer;
            *DrawBinding = Context->BoundDrawFramebuffer;
            return TRUE;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return FALSE;
    }
}

static PVIRTGPU_OGL_FRAMEBUFFER
VirtGpuOglBoundFramebuffer(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLenum Target)
{
    GLuint ReadName;
    GLuint DrawName;

    if (!VirtGpuOglFramebufferTargetBinding(Context, Target, &ReadName, &DrawName))
        return NULL;

    UNREFERENCED_PARAMETER(ReadName);
    if (DrawName == 0)
        return NULL;

    return VirtGpuOglFindFramebuffer(Context, DrawName);
}

static BOOL
VirtGpuOglFramebufferAttachmentIndex(_In_ GLenum Attachment, _Out_ PULONG Index)
{
    switch (Attachment)
    {
        case GL_COLOR_ATTACHMENT0:
            *Index = 0;
            return TRUE;
        case GL_DEPTH_ATTACHMENT:
            *Index = 1;
            return TRUE;
        case GL_STENCIL_ATTACHMENT:
            *Index = 2;
            return TRUE;
        default:
            return FALSE;
    }
}

static VOID
VirtGpuOglFormatComponentBits(
    _In_ GLenum InternalFormat,
    _Out_ GLint *RedBits,
    _Out_ GLint *GreenBits,
    _Out_ GLint *BlueBits,
    _Out_ GLint *AlphaBits,
    _Out_ GLint *DepthBits,
    _Out_ GLint *StencilBits)
{
    *RedBits = 0;
    *GreenBits = 0;
    *BlueBits = 0;
    *AlphaBits = 0;
    *DepthBits = 0;
    *StencilBits = 0;

    switch (InternalFormat)
    {
        case 3:
        case GL_RGB:
        case GL_RGB8:
            *RedBits = 8;
            *GreenBits = 8;
            *BlueBits = 8;
            break;
        case 4:
        case GL_RGBA:
        case GL_RGBA8:
            *RedBits = 8;
            *GreenBits = 8;
            *BlueBits = 8;
            *AlphaBits = 8;
            break;
        case GL_DEPTH_COMPONENT:
        case GL_DEPTH_COMPONENT16:
            *DepthBits = 16;
            break;
        case GL_DEPTH_COMPONENT24:
            *DepthBits = 24;
            break;
        case GL_DEPTH_COMPONENT32:
            *DepthBits = 32;
            break;
        case GL_STENCIL_INDEX:
        case GL_STENCIL_INDEX8:
            *StencilBits = 8;
            break;
        case GL_DEPTH_STENCIL:
            *DepthBits = 24;
            *StencilBits = 8;
            break;
    }
}

static PVIRTGPU_OGL_VERTEX_ARRAY
VirtGpuOglFindVertexArray(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_VERTEX_ARRAYS; ++Index)
    {
        if (Context->VertexArrays[Index].Allocated &&
            (Context->VertexArrays[Index].Name == Name))
        {
            return &Context->VertexArrays[Index];
        }
    }

    return NULL;
}

static VOID
VirtGpuOglSaveVertexArrayState(
    _In_ PVIRTGPU_OGL_CONTEXT Context,
    _Inout_ PVIRTGPU_OGL_VERTEX_ARRAY VertexArray)
{
    VertexArray->ClientArrayBits = Context->ClientArrayBits;
    VertexArray->VertexArraySize = Context->VertexArraySize;
    VertexArray->VertexArrayType = Context->VertexArrayType;
    VertexArray->VertexArrayStride = Context->VertexArrayStride;
    VertexArray->VertexArrayPointer = Context->VertexArrayPointer;
    VertexArray->ColorArraySize = Context->ColorArraySize;
    VertexArray->ColorArrayType = Context->ColorArrayType;
    VertexArray->ColorArrayStride = Context->ColorArrayStride;
    VertexArray->ColorArrayPointer = Context->ColorArrayPointer;
    VertexArray->NormalArrayType = Context->NormalArrayType;
    VertexArray->NormalArrayStride = Context->NormalArrayStride;
    VertexArray->NormalArrayPointer = Context->NormalArrayPointer;
    VertexArray->TexCoordArraySize = Context->TexCoordArraySize;
    VertexArray->TexCoordArrayType = Context->TexCoordArrayType;
    VertexArray->TexCoordArrayStride = Context->TexCoordArrayStride;
    VertexArray->TexCoordArrayPointer = Context->TexCoordArrayPointer;
    CopyMemory(VertexArray->VertexAttribs,
               Context->VertexAttribs,
               sizeof(Context->VertexAttribs));
}

static VOID
VirtGpuOglLoadVertexArrayState(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ PVIRTGPU_OGL_VERTEX_ARRAY VertexArray)
{
    Context->ClientArrayBits = VertexArray->ClientArrayBits;
    Context->VertexArraySize = VertexArray->VertexArraySize;
    Context->VertexArrayType = VertexArray->VertexArrayType;
    Context->VertexArrayStride = VertexArray->VertexArrayStride;
    Context->VertexArrayPointer = VertexArray->VertexArrayPointer;
    Context->ColorArraySize = VertexArray->ColorArraySize;
    Context->ColorArrayType = VertexArray->ColorArrayType;
    Context->ColorArrayStride = VertexArray->ColorArrayStride;
    Context->ColorArrayPointer = VertexArray->ColorArrayPointer;
    Context->NormalArrayType = VertexArray->NormalArrayType;
    Context->NormalArrayStride = VertexArray->NormalArrayStride;
    Context->NormalArrayPointer = VertexArray->NormalArrayPointer;
    Context->TexCoordArraySize = VertexArray->TexCoordArraySize;
    Context->TexCoordArrayType = VertexArray->TexCoordArrayType;
    Context->TexCoordArrayStride = VertexArray->TexCoordArrayStride;
    Context->TexCoordArrayPointer = VertexArray->TexCoordArrayPointer;
    CopyMemory(Context->VertexAttribs,
               VertexArray->VertexAttribs,
               sizeof(Context->VertexAttribs));
}

static PVIRTGPU_OGL_VERTEX_ARRAY
VirtGpuOglAllocateVertexArrayName(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_VERTEX_ARRAYS; ++Index)
    {
        if (!Context->VertexArrays[Index].Allocated)
        {
            ZeroMemory(&Context->VertexArrays[Index], sizeof(Context->VertexArrays[Index]));
            Context->VertexArrays[Index].Allocated = TRUE;
            Context->VertexArrays[Index].Name = Name;
            VirtGpuOglSaveVertexArrayState(Context, &Context->VertexArrays[Index]);
            return &Context->VertexArrays[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static PVIRTGPU_OGL_SYNC
VirtGpuOglFindSync(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_opt_ GLsync Sync)
{
    ULONG Index;

    if ((Context == NULL) || (Sync == NULL))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_SYNCS; ++Index)
    {
        if (Context->Syncs[Index].Allocated &&
            ((GLsync)&Context->Syncs[Index] == Sync))
        {
            return &Context->Syncs[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_TRANSFORM_FEEDBACK
VirtGpuOglFindTransformFeedback(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_TRANSFORM_FEEDBACKS; ++Index)
    {
        if (Context->TransformFeedbacks[Index].Allocated &&
            (Context->TransformFeedbacks[Index].Name == Name))
        {
            return &Context->TransformFeedbacks[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_OGL_TRANSFORM_FEEDBACK
VirtGpuOglAllocateTransformFeedbackName(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_TRANSFORM_FEEDBACKS; ++Index)
    {
        if (!Context->TransformFeedbacks[Index].Allocated)
        {
            ZeroMemory(&Context->TransformFeedbacks[Index], sizeof(Context->TransformFeedbacks[Index]));
            Context->TransformFeedbacks[Index].Allocated = TRUE;
            Context->TransformFeedbacks[Index].Name = Name;
            Context->TransformFeedbacks[Index].PrimitiveMode = GL_POINTS;
            return &Context->TransformFeedbacks[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static PVIRTGPU_OGL_SAMPLER
VirtGpuOglFindSampler(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_SAMPLERS; ++Index)
    {
        if (Context->Samplers[Index].Allocated &&
            (Context->Samplers[Index].Name == Name))
        {
            return &Context->Samplers[Index];
        }
    }

    return NULL;
}

static VOID
VirtGpuOglInitializeSamplerDefaults(_Inout_ PVIRTGPU_OGL_SAMPLER Sampler)
{
    Sampler->MinFilter = GL_NEAREST_MIPMAP_LINEAR;
    Sampler->MagFilter = GL_LINEAR;
    Sampler->WrapS = GL_REPEAT;
    Sampler->WrapT = GL_REPEAT;
    Sampler->WrapR = GL_REPEAT;
}

static PVIRTGPU_OGL_SAMPLER
VirtGpuOglAllocateSamplerName(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_ GLuint Name)
{
    ULONG Index;

    if ((Context == NULL) || (Name == 0))
        return NULL;

    for (Index = 0; Index < VIRTGPU_OGL_MAX_SAMPLERS; ++Index)
    {
        if (!Context->Samplers[Index].Allocated)
        {
            ZeroMemory(&Context->Samplers[Index], sizeof(Context->Samplers[Index]));
            Context->Samplers[Index].Allocated = TRUE;
            Context->Samplers[Index].Name = Name;
            VirtGpuOglInitializeSamplerDefaults(&Context->Samplers[Index]);
            return &Context->Samplers[Index];
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
        case GL_TEXTURE_3D:
            Name = Context->BoundTexture3D;
            break;
        case GL_TEXTURE_BUFFER:
            Name = Context->BoundTextureBuffer;
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
VirtGpuOglTransformFeedbackModeValid(_In_ GLenum Mode)
{
    return (Mode == GL_POINTS) || (Mode == GL_LINES) || (Mode == GL_TRIANGLES);
}

static BOOL
VirtGpuOglBufferRangeTargetBinding(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLenum Target,
    _In_ GLuint Index,
    _Outptr_ PVIRTGPU_OGL_BUFFER_BINDING *Binding)
{
    if (Index >= VIRTGPU_OGL_MAX_BUFFER_BINDINGS)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return FALSE;
    }

    switch (Target)
    {
        case GL_UNIFORM_BUFFER:
            *Binding = &Context->UniformBufferBindings[Index];
            return TRUE;
        case GL_TRANSFORM_FEEDBACK_BUFFER:
            *Binding = &Context->TransformFeedbackBufferBindings[Index];
            return TRUE;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return FALSE;
    }
}

static BOOL
VirtGpuOglQueryTargetValid(_In_ GLenum Target)
{
    return (Target == GL_SAMPLES_PASSED) ||
           (Target == GL_ANY_SAMPLES_PASSED) ||
           (Target == GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
}

static BOOL
VirtGpuOglElementTypeSize(_In_ GLenum Type, _Out_ PULONG Size)
{
    switch (Type)
    {
        case GL_UNSIGNED_BYTE:
            *Size = sizeof(GLubyte);
            return TRUE;
        case GL_UNSIGNED_SHORT:
            *Size = sizeof(GLushort);
            return TRUE;
        case GL_UNSIGNED_INT:
            *Size = sizeof(GLuint);
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglShaderTypeValid(_In_ GLenum ShaderType)
{
    switch (ShaderType)
    {
        case GL_VERTEX_SHADER:
        case GL_FRAGMENT_SHADER:
        case GL_GEOMETRY_SHADER:
        case GL_TESS_CONTROL_SHADER:
        case GL_TESS_EVALUATION_SHADER:
            return TRUE;
        default:
            return FALSE;
    }
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
    return (Value == GL_CLAMP) ||
           (Value == GL_REPEAT) ||
           (Value == GL_CLAMP_TO_EDGE);
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

static VOID
VirtGpuOglInitializeImageTableDefaults(_Inout_ PVIRTGPU_OGL_IMAGE_TABLE Table)
{
    ULONG Index;

    Table->BorderMode = GL_REDUCE;
    for (Index = 0; Index < 4; ++Index)
    {
        Table->Scale[Index] = 1.0f;
        Table->Bias[Index] = 0.0f;
        Table->BorderColor[Index] = 0.0f;
    }
}

static VOID
VirtGpuOglFreeImageTable(_Inout_ PVIRTGPU_OGL_IMAGE_TABLE Table)
{
    if (Table->Data != NULL)
        HeapFree(GetProcessHeap(), 0, Table->Data);
    ZeroMemory(Table, sizeof(*Table));
    VirtGpuOglInitializeImageTableDefaults(Table);
}

static BOOL
VirtGpuOglColorTableTargetToIndex(_In_ GLenum Target, _Out_ PULONG Index)
{
    switch (Target)
    {
        case GL_COLOR_TABLE:
        case GL_PROXY_COLOR_TABLE:
            *Index = 0;
            return TRUE;
        case GL_POST_CONVOLUTION_COLOR_TABLE:
        case GL_PROXY_POST_CONVOLUTION_COLOR_TABLE:
            *Index = 1;
            return TRUE;
        case GL_POST_COLOR_MATRIX_COLOR_TABLE:
        case GL_PROXY_POST_COLOR_MATRIX_COLOR_TABLE:
            *Index = 2;
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglConvolutionTargetToIndex(_In_ GLenum Target, _Out_ PULONG Index)
{
    switch (Target)
    {
        case GL_CONVOLUTION_1D:
            *Index = 0;
            return TRUE;
        case GL_CONVOLUTION_2D:
            *Index = 1;
            return TRUE;
        case GL_SEPARABLE_2D:
            *Index = 2;
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglStoreImageTable(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _Inout_ PVIRTGPU_OGL_IMAGE_TABLE Table,
    _In_ GLenum Target,
    _In_ GLenum InternalFormat,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Pixels)
{
    ULONG BytesPerPixel;
    ULONGLONG DataSize64;
    BYTE *Data = NULL;

    if ((Width < 0) || (Height < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return FALSE;
    }

    if (!VirtGpuOglTextureFormatBytes(Format, Type, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return FALSE;
    }

    DataSize64 = (ULONGLONG)(ULONG)Width * (ULONGLONG)(ULONG)Height * BytesPerPixel;
    if (DataSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return FALSE;
    }

    if (DataSize64 != 0)
    {
        Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)DataSize64);
        if (Data == NULL)
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            return FALSE;
        }

        if (Pixels != NULL)
            CopyMemory(Data, Pixels, (SIZE_T)DataSize64);
    }

    if (Table->Data != NULL)
        HeapFree(GetProcessHeap(), 0, Table->Data);

    Table->Defined = TRUE;
    Table->Target = Target;
    Table->InternalFormat = InternalFormat;
    Table->Format = Format;
    Table->Type = Type;
    Table->Width = Width;
    Table->Height = Height;
    Table->DataSize = (ULONG)DataSize64;
    Table->Data = Data;
    return TRUE;
}

static VOID
VirtGpuOglGetImageTableData(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ PVIRTGPU_OGL_IMAGE_TABLE Table,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _Out_opt_ GLvoid *Pixels)
{
    ULONG BytesPerPixel;
    ULONGLONG OutputSize64;

    if (Pixels == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglTextureFormatBytes(Format, Type, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    OutputSize64 = (ULONGLONG)(ULONG)Table->Width *
                   (ULONGLONG)(ULONG)Table->Height *
                   BytesPerPixel;
    if (OutputSize64 == 0)
        return;

    if ((Table->Data != NULL) &&
        (Table->Format == Format) &&
        (Table->Type == Type) &&
        (Table->DataSize >= OutputSize64))
    {
        CopyMemory(Pixels, Table->Data, (SIZE_T)OutputSize64);
    }
    else
    {
        ZeroMemory(Pixels, (SIZE_T)OutputSize64);
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
    ULONGLONG RowSize64;
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

    RowSize64 = (ULONGLONG)(ULONG)Width * BytesPerPixel;
    ImageSize64 = RowSize64 * (ULONGLONG)(ULONG)Height;
    if ((RowSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE) ||
        (ImageSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE))
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return FALSE;
    }

    RowSize = (ULONG)RowSize64;

    if (ImageSize64 != 0)
    {
        Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)ImageSize64);
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
    ULONGLONG RowSize64;
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

    RowSize64 = (ULONGLONG)(ULONG)Width * BytesPerPixel;
    ImageSize64 = RowSize64 * (ULONGLONG)(ULONG)Height;
    if ((RowSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE) ||
        (ImageSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE))
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return FALSE;
    }

    RowSize = (ULONG)RowSize64;

    Data = NULL;
    if (ImageSize64 != 0)
    {
        Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)ImageSize64);
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
    Texture->Depth = 1;
    Texture->InternalFormat = InternalFormat;
    Texture->Format = Format;
    Texture->Type = Type;
    Texture->DataSize = (ULONG)ImageSize64;
    Texture->Data = Data;
    return TRUE;
}

static BOOL
VirtGpuOglStoreTextureImage3D(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _Inout_ PVIRTGPU_OGL_TEXTURE Texture,
    _In_ GLenum Target,
    _In_ GLint Level,
    _In_ GLint InternalFormat,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _In_ GLsizei Depth,
    _In_ GLint Border,
    _In_ GLenum Format,
    _In_ GLenum Type,
    _In_opt_ const GLvoid *Pixels)
{
    ULONG BytesPerPixel;
    ULONG SourceStride;
    ULONG SourceLayerStride;
    ULONG RowSize;
    ULONGLONG RowSize64;
    ULONGLONG ImageSize64;
    BYTE *Data;
    GLsizei Slice;
    GLsizei Row;

    if ((Texture == NULL) ||
        (Target != GL_TEXTURE_3D) ||
        (Level != 0) ||
        (Border != 0) ||
        (Width < 0) ||
        (Height < 0) ||
        (Depth < 0))
    {
        VirtGpuOglSetError(Context,
                           ((Width < 0) || (Height < 0) || (Depth < 0)) ?
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

    RowSize64 = (ULONGLONG)(ULONG)Width * BytesPerPixel;
    ImageSize64 = RowSize64 * (ULONGLONG)(ULONG)Height * (ULONGLONG)(ULONG)Depth;
    if ((RowSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE) ||
        (ImageSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE))
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return FALSE;
    }

    RowSize = (ULONG)RowSize64;

    Data = NULL;
    if (ImageSize64 != 0)
    {
        Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)ImageSize64);
        if (Data == NULL)
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            return FALSE;
        }
    }

    if ((Pixels != NULL) && (Data != NULL))
    {
        SourceStride = VirtGpuOglAlignedRowSize(RowSize, Context->UnpackAlignment);
        SourceLayerStride = SourceStride * (ULONG)Height;
        for (Slice = 0; Slice < Depth; ++Slice)
        {
            for (Row = 0; Row < Height; ++Row)
            {
                CopyMemory(Data +
                           ((((ULONG)Slice * (ULONG)Height) + (ULONG)Row) * RowSize),
                           (const BYTE *)Pixels +
                           ((ULONG)Slice * SourceLayerStride) +
                           ((ULONG)Row * SourceStride),
                           RowSize);
            }
        }
    }

    if (Texture->Data != NULL)
        HeapFree(GetProcessHeap(), 0, Texture->Data);

    Texture->Target = Target;
    Texture->Width = Width;
    Texture->Height = Height;
    Texture->Depth = Depth;
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
    ULONG HeaderSize = offsetof(VIRTGPU_3D_BATCH, Commands);
    ULONG BatchCommandSize = sizeof(VIRTGPU_3D_BATCH_COMMAND);
    ULONG CommandBytes;
    ULONG BufferSize;
    PVIRTGPU_3D_BATCH Batch;
    PVIRTGPU_3D_BATCH_COMMAND BatchCommand;
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
    BufferSize = HeaderSize + BatchCommandSize + CommandBytes;
    Batch = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BufferSize);
    if (Batch == NULL)
        return FALSE;

    Batch->Version = VIRTGPU_3D_BATCH_VERSION;
    Batch->ContextId = Context->ContextId;
    Batch->CommandCount = 1;
    Batch->Size = BatchCommandSize + CommandBytes;

    BatchCommand = (PVIRTGPU_3D_BATCH_COMMAND)Batch->Commands;
    BatchCommand->OpCode = VIRTGPU_3D_BATCH_OP_SUBMIT;
    BatchCommand->Size = CommandBytes;
    CopyMemory(Batch->Commands + BatchCommandSize,
               Cmd->Dwords,
               CommandBytes);

    Success = VirtGpuOglEscapeIoControl(Context->hdc,
                                        IOCTL_VIDEO_VIRTGPU_3D_EXECUTE_BATCH,
                                        Batch,
                                        BufferSize,
                                        Batch,
                                        BufferSize,
                                        &Returned) &&
              (Returned >= HeaderSize);
    if (Success)
    {
        Context->LastVirglFenceId = Batch->FenceId;
        if (FenceId != NULL)
            *FenceId = Batch->FenceId;
    }
    HeapFree(GetProcessHeap(), 0, Batch);
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
    ULONGLONG BackingSize64;

    if ((Context == NULL) || Context->VirglDisabled || (ResourceId == NULL))
        return FALSE;

    *ResourceId = 0;
    VirtGpuOglUpdateDrawableSize(Context, FALSE);
    if ((Context->DrawableWidth <= 0) || (Context->DrawableHeight <= 0))
        return FALSE;

    BackingSize64 = (ULONGLONG)(ULONG)Context->DrawableWidth *
                    (ULONGLONG)(ULONG)Context->DrawableHeight *
                    4ULL;
    if ((BackingSize64 == 0) || (BackingSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE))
        return FALSE;
    BackingSize = (ULONG)BackingSize64;

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
    ULONG Index;

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
    Context->PolygonOffsetFactor = 0.0f;
    Context->PolygonOffsetUnits = 0.0f;
    Context->LineStippleFactor = 1;
    Context->LineStipplePattern = 0xFFFF;
    Context->StencilFunc = GL_ALWAYS;
    Context->StencilRef = 0;
    Context->StencilValueMask = 0xFFFFFFFF;
    Context->StencilFail = GL_KEEP;
    Context->StencilDepthFail = GL_KEEP;
    Context->StencilDepthPass = GL_KEEP;
    Context->PackAlignment = 4;
    Context->PackRowLength = 0;
    Context->PackSkipRows = 0;
    Context->PackSkipPixels = 0;
    Context->PackSwapBytes = GL_FALSE;
    Context->PackLsbFirst = GL_FALSE;
    Context->UnpackAlignment = 4;
    Context->UnpackRowLength = 0;
    Context->UnpackSkipRows = 0;
    Context->UnpackSkipPixels = 0;
    Context->UnpackSwapBytes = GL_FALSE;
    Context->UnpackLsbFirst = GL_FALSE;
    Context->NextTextureName = 1;
    Context->NextListName = 1;
    Context->NextShaderName = 1;
    Context->NextProgramName = 1;
    Context->NextBufferName = 1;
    Context->NextQueryName = 1;
    Context->NextRenderbufferName = 1;
    Context->NextFramebufferName = 1;
    Context->NextVertexArrayName = 1;
    Context->NextSamplerName = 1;
    Context->NextTransformFeedbackName = 1;
    Context->CurrentProgram = 0;
    Context->BoundArrayBuffer = 0;
    Context->BoundElementArrayBuffer = 0;
    Context->BoundCopyReadBuffer = 0;
    Context->BoundCopyWriteBuffer = 0;
    Context->BoundUniformBuffer = 0;
    Context->BoundTransformFeedbackBuffer = 0;
    Context->BoundDrawIndirectBuffer = 0;
    Context->CurrentQuery = 0;
    Context->ActiveTexture = GL_TEXTURE0;
    Context->ClientActiveTexture = GL_TEXTURE0;
    Context->ListBase = 0;
    Context->BoundTexture1D = 0;
    Context->BoundTexture2D = 0;
    Context->BoundTexture3D = 0;
    Context->BoundTextureBuffer = 0;
    Context->BoundRenderbuffer = 0;
    Context->BoundReadFramebuffer = 0;
    Context->BoundDrawFramebuffer = 0;
    Context->BoundVertexArray = 0;
    Context->BoundTransformFeedback = 0;
    for (Index = 0; Index < VIRTGPU_OGL_MAX_TEXTURE_UNITS; ++Index)
        Context->BoundSamplers[Index] = 0;
    for (Index = 0; Index < VIRTGPU_OGL_MAX_VERTEX_ATTRIBS; ++Index)
    {
        Context->VertexAttribs[Index].Enabled = GL_FALSE;
        Context->VertexAttribs[Index].Size = 4;
        Context->VertexAttribs[Index].Type = GL_FLOAT;
        Context->VertexAttribs[Index].Normalized = GL_FALSE;
        Context->VertexAttribs[Index].Stride = 0;
        Context->VertexAttribs[Index].Pointer = NULL;
        Context->VertexAttribs[Index].Divisor = 0;
        Context->VertexAttribs[Index].Current[0] = 0.0f;
        Context->VertexAttribs[Index].Current[1] = 0.0f;
        Context->VertexAttribs[Index].Current[2] = 0.0f;
        Context->VertexAttribs[Index].Current[3] = 1.0f;
    }
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
    Context->IndexArrayType = GL_FLOAT;
    Context->IndexArrayStride = 0;
    Context->IndexArrayPointer = NULL;
    Context->EdgeFlagArrayStride = 0;
    Context->EdgeFlagArrayPointer = NULL;
    Context->SecondaryColorArraySize = 3;
    Context->SecondaryColorArrayType = GL_FLOAT;
    Context->SecondaryColorArrayStride = 0;
    Context->SecondaryColorArrayPointer = NULL;
    Context->FogCoordArrayType = GL_FLOAT;
    Context->FogCoordArrayStride = 0;
    Context->FogCoordArrayPointer = NULL;
    Context->TexCoordArraySize = 4;
    Context->TexCoordArrayType = GL_FLOAT;
    Context->TexCoordArrayStride = 0;
    Context->TexCoordArrayPointer = NULL;
    Context->CurrentColor = RGB(255, 255, 255);
    Context->CurrentAlpha = 1.0f;
    Context->CurrentSecondaryColor[0] = 0.0f;
    Context->CurrentSecondaryColor[1] = 0.0f;
    Context->CurrentSecondaryColor[2] = 0.0f;
    Context->CurrentFogCoord = 0.0f;
    Context->CurrentIndex = 1.0f;
    Context->CurrentNormal[0] = 0.0f;
    Context->CurrentNormal[1] = 0.0f;
    Context->CurrentNormal[2] = 1.0f;
    Context->CurrentTexCoord[0] = 0.0f;
    Context->CurrentTexCoord[1] = 0.0f;
    Context->CurrentTexCoord[2] = 0.0f;
    Context->CurrentTexCoord[3] = 1.0f;
    Context->CurrentRasterPosition[0] = 0.0f;
    Context->CurrentRasterPosition[1] = 0.0f;
    Context->CurrentRasterPosition[2] = 0.0f;
    Context->CurrentRasterPosition[3] = 1.0f;
    Context->CurrentRasterWindow.x = 0;
    Context->CurrentRasterWindow.y = 0;
    Context->CurrentRasterPositionValid = GL_TRUE;
    Context->EdgeFlag = GL_TRUE;
    Context->PixelZoomX = 1.0f;
    Context->PixelZoomY = 1.0f;
    Context->PointSizeMin = 0.0f;
    Context->PointSizeMax = 1.0f;
    Context->PointFadeThresholdSize = 1.0f;
    Context->PointDistanceAttenuation[0] = 1.0f;
    Context->PointDistanceAttenuation[1] = 0.0f;
    Context->PointDistanceAttenuation[2] = 0.0f;
    Context->BlendColor[0] = 0.0f;
    Context->BlendColor[1] = 0.0f;
    Context->BlendColor[2] = 0.0f;
    Context->BlendColor[3] = 0.0f;
    Context->BlendEquationMode = GL_FUNC_ADD;
    Context->LogicOpMode = GL_COPY;
    Context->DefaultTransformFeedbackActive = FALSE;
    Context->DefaultTransformFeedbackPaused = FALSE;
    Context->DefaultTransformFeedbackPrimitiveMode = GL_POINTS;
    Context->PrimitiveRestartIndex = 0xFFFFFFFF;
    Context->ProvokingVertexMode = GL_LAST_VERTEX_CONVENTION;
    Context->PatchVertices = 3;
    Context->PatchDefaultOuterLevel[0] = 1.0f;
    Context->PatchDefaultOuterLevel[1] = 1.0f;
    Context->PatchDefaultOuterLevel[2] = 1.0f;
    Context->PatchDefaultOuterLevel[3] = 1.0f;
    Context->PatchDefaultInnerLevel[0] = 1.0f;
    Context->PatchDefaultInnerLevel[1] = 1.0f;
    Context->ConditionalRenderActive = FALSE;
    Context->ConditionalRenderQuery = 0;
    Context->ConditionalRenderMode = GL_QUERY_WAIT;
    Context->ClampVertexColor = GL_TRUE;
    Context->ClampFragmentColor = GL_TRUE;
    Context->ClampReadColor = GL_FIXED_ONLY;
    Context->ClearAccum[0] = 0.0f;
    Context->ClearAccum[1] = 0.0f;
    Context->ClearAccum[2] = 0.0f;
    Context->ClearAccum[3] = 0.0f;
    Context->ClearIndex = 0.0f;
    Context->IndexMask = 0xFFFFFFFF;
    for (Index = 0; Index < 6; ++Index)
    {
        Context->ClipPlanes[Index][0] = 0.0;
        Context->ClipPlanes[Index][1] = 0.0;
        Context->ClipPlanes[Index][2] = 0.0;
        Context->ClipPlanes[Index][3] = 0.0;
    }
    Context->ColorMaterialFace = GL_FRONT_AND_BACK;
    Context->ColorMaterialMode = GL_AMBIENT_AND_DIFFUSE;
    Context->FogMode = GL_EXP;
    Context->FogDensity = 1.0f;
    Context->FogStart = 0.0f;
    Context->FogEnd = 1.0f;
    Context->FogIndex = 0.0f;
    Context->FogColor[0] = 0.0f;
    Context->FogColor[1] = 0.0f;
    Context->FogColor[2] = 0.0f;
    Context->FogColor[3] = 0.0f;
    Context->TexEnvMode = GL_MODULATE;
    Context->TexEnvColor[0] = 0.0f;
    Context->TexEnvColor[1] = 0.0f;
    Context->TexEnvColor[2] = 0.0f;
    Context->TexEnvColor[3] = 0.0f;
    for (Index = 0; Index < 4; ++Index)
    {
        Context->TexGenMode[Index] = GL_EYE_LINEAR;
        Context->TexGenObjectPlane[Index][0] = (Index == 0) ? 1.0 : 0.0;
        Context->TexGenObjectPlane[Index][1] = (Index == 1) ? 1.0 : 0.0;
        Context->TexGenObjectPlane[Index][2] = (Index == 2) ? 1.0 : 0.0;
        Context->TexGenObjectPlane[Index][3] = (Index == 3) ? 1.0 : 0.0;
        Context->TexGenEyePlane[Index][0] = Context->TexGenObjectPlane[Index][0];
        Context->TexGenEyePlane[Index][1] = Context->TexGenObjectPlane[Index][1];
        Context->TexGenEyePlane[Index][2] = Context->TexGenObjectPlane[Index][2];
        Context->TexGenEyePlane[Index][3] = Context->TexGenObjectPlane[Index][3];
    }
    FillMemory(Context->PolygonStipple, sizeof(Context->PolygonStipple), 0xFF);
    for (Index = 0; Index < VIRTGPU_OGL_PIXEL_MAP_COUNT; ++Index)
    {
        Context->PixelMaps[Index].Size = 0;
        Context->PixelMaps[Index].Values = NULL;
    }
    for (Index = 0; Index < VIRTGPU_OGL_COLOR_TABLE_COUNT; ++Index)
        VirtGpuOglInitializeImageTableDefaults(&Context->ColorTables[Index]);
    for (Index = 0; Index < VIRTGPU_OGL_CONVOLUTION_COUNT; ++Index)
        VirtGpuOglInitializeImageTableDefaults(&Context->ConvolutionFilters[Index]);
    Context->Histogram.Target = GL_HISTOGRAM;
    Context->Histogram.InternalFormat = GL_RGBA;
    Context->Histogram.Width = 0;
    Context->Histogram.Sink = GL_FALSE;
    Context->Minmax.Target = GL_MINMAX;
    Context->Minmax.InternalFormat = GL_RGBA;
    Context->Minmax.Sink = GL_FALSE;
    for (Index = 0; Index < VIRTGPU_OGL_EVAL_MAP_COUNT; ++Index)
    {
        Context->EvalMap1[Index].Defined = FALSE;
        Context->EvalMap1[Index].Target = GL_MAP1_COLOR_4 + Index;
        Context->EvalMap1[Index].Components = 0;
        Context->EvalMap1[Index].Points = NULL;
        Context->EvalMap2[Index].Defined = FALSE;
        Context->EvalMap2[Index].Target = GL_MAP2_COLOR_4 + Index;
        Context->EvalMap2[Index].Components = 0;
        Context->EvalMap2[Index].Points = NULL;
    }
    Context->EvalEnableBits = 0;
    Context->RenderMode = GL_RENDER;
    Context->FeedbackBufferSize = 0;
    Context->FeedbackBufferType = GL_2D;
    Context->FeedbackBuffer = NULL;
    Context->FeedbackBufferUsed = 0;
    Context->SelectBufferSize = 0;
    Context->SelectBuffer = NULL;
    Context->SelectHits = 0;
    Context->NameStackDepth = 0;
    Context->MapGrid1[0] = 1.0;
    Context->MapGrid1[1] = 0.0;
    Context->MapGrid1[2] = 1.0;
    Context->MapGrid2[0] = 1.0;
    Context->MapGrid2[1] = 0.0;
    Context->MapGrid2[2] = 1.0;
    Context->MapGrid2[3] = 1.0;
    Context->MapGrid2[4] = 0.0;
    Context->MapGrid2[5] = 1.0;
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
        case GL_CURRENT_RASTER_COLOR:
        case GL_CURRENT_RASTER_SECONDARY_COLOR:
        case GL_CURRENT_RASTER_POSITION:
        case GL_CURRENT_RASTER_TEXTURE_COORDS:
        case GL_MAP2_GRID_DOMAIN:
            return 4;
        case GL_CURRENT_NORMAL:
        case GL_CURRENT_SECONDARY_COLOR:
            return 3;
        case GL_MAX_VIEWPORT_DIMS:
        case GL_DEPTH_RANGE:
        case GL_POLYGON_MODE:
        case GL_MAP1_GRID_DOMAIN:
        case GL_MAP2_GRID_SEGMENTS:
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

/* Generated OpenGL dispatch: ICD 1.1 table plus non-advertised compatibility lookup entries. */

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
    if (List == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

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

    if ((Arg1 < 0) ||
        ((ULONGLONG)(ULONG)Arg1 * 4ULL > VIRTGPU_OGL_MAX_TRANSFER_SIZE))
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

static VOID
VirtGpuOglSetRasterPosition4f(
    _In_ GLfloat X,
    _In_ GLfloat Y,
    _In_ GLfloat Z,
    _In_ GLfloat W)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    VIRTGPU_OGL_VERTEX Vertex;
    POINT Point;

    if (Context == NULL)
        return;

    Vertex.X = X;
    Vertex.Y = Y;
    Vertex.Z = Z;
    Vertex.W = W;
    Vertex.OldVertexCount = 0;
    Vertex.Color = Context->CurrentColor;

    VirtGpuOglVertexToPoint(Context, &Vertex, &Point);
    Context->CurrentRasterPosition[0] = X;
    Context->CurrentRasterPosition[1] = Y;
    Context->CurrentRasterPosition[2] = Z;
    Context->CurrentRasterPosition[3] = W;
    Context->CurrentRasterWindow = Point;
    Context->CurrentRasterPositionValid = GL_TRUE;
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG RowBytes;
    ULONG SourceStride;
    GLsizei Row;
    GLsizei Column;
    INT BaseX;
    INT BaseY;

    if (Context == NULL)
        return;

    if ((Arg0 < 0) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!Context->CurrentRasterPositionValid)
        return;

    if ((Arg0 > 0) && (Arg1 > 0) && (Arg6 != NULL))
    {
        RowBytes = ((ULONG)Arg0 + 7) / 8;
        SourceStride = VirtGpuOglAlignedRowSize(RowBytes, Context->UnpackAlignment);
        BaseX = Context->CurrentRasterWindow.x - VirtGpuOglRoundFloat(Arg2);
        BaseY = Context->CurrentRasterWindow.y + VirtGpuOglRoundFloat(Arg3) - Arg1 + 1;

        for (Row = 0; Row < Arg1; ++Row)
        {
            const GLubyte *SourceRow = Arg6 + ((ULONG)(Arg1 - 1 - Row) * SourceStride);

            for (Column = 0; Column < Arg0; ++Column)
            {
                GLubyte Mask = (GLubyte)(0x80 >> (Column & 7));

                if ((SourceRow[Column >> 3] & Mask) != 0)
                {
                    SetPixel(Context->hdc,
                             BaseX + Column,
                             BaseY + Row,
                             Context->CurrentColor);
                }
            }
        }
    }

    Context->CurrentRasterWindow.x += VirtGpuOglRoundFloat(Arg4);
    Context->CurrentRasterWindow.y -= VirtGpuOglRoundFloat(Arg5);
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
    Context->CurrentAlpha = 1.0f;
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
    VirtGpuOglColor4f(VirtGpuOglColorFromByte(Arg0),
                      VirtGpuOglColorFromByte(Arg1),
                      VirtGpuOglColorFromByte(Arg2),
                      VirtGpuOglColorFromByte(Arg3));
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
    VirtGpuOglColor4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
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

    Context->CurrentColor = VirtGpuOglColorFromFloat(Arg0, Arg1, Arg2);
    Context->CurrentAlpha = VirtGpuOglClampFloat(Arg3);
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
    VirtGpuOglColor4f(VirtGpuOglColorFromInt(Arg0),
                      VirtGpuOglColorFromInt(Arg1),
                      VirtGpuOglColorFromInt(Arg2),
                      VirtGpuOglColorFromInt(Arg3));
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
    VirtGpuOglColor4f(VirtGpuOglColorFromShort(Arg0),
                      VirtGpuOglColorFromShort(Arg1),
                      VirtGpuOglColorFromShort(Arg2),
                      VirtGpuOglColorFromShort(Arg3));
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
    VirtGpuOglColor4f(VirtGpuOglColorFromUByte(Arg0),
                      VirtGpuOglColorFromUByte(Arg1),
                      VirtGpuOglColorFromUByte(Arg2),
                      VirtGpuOglColorFromUByte(Arg3));
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
    VirtGpuOglColor4f(VirtGpuOglColorFromUInt(Arg0),
                      VirtGpuOglColorFromUInt(Arg1),
                      VirtGpuOglColorFromUInt(Arg2),
                      VirtGpuOglColorFromUInt(Arg3));
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
    VirtGpuOglColor4f(VirtGpuOglColorFromUShort(Arg0),
                      VirtGpuOglColorFromUShort(Arg1),
                      VirtGpuOglColorFromUShort(Arg2),
                      VirtGpuOglColorFromUShort(Arg3));
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context != NULL)
        Context->EdgeFlag = Arg0 ? GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglEdgeFlagv(const GLboolean * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglEdgeFlag(*Arg0);
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context != NULL)
        Context->CurrentIndex = (GLfloat)Arg0;
}

static void APIENTRY
VirtGpuOglIndexdv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglIndexd(Arg0[0]);
}

static void APIENTRY
VirtGpuOglIndexf(GLfloat Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context != NULL)
        Context->CurrentIndex = Arg0;
}

static void APIENTRY
VirtGpuOglIndexfv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglIndexf(Arg0[0]);
}

static void APIENTRY
VirtGpuOglIndexi(GLint Arg0)
{
    VirtGpuOglIndexf((GLfloat)Arg0);
}

static void APIENTRY
VirtGpuOglIndexiv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglIndexi(Arg0[0]);
}

static void APIENTRY
VirtGpuOglIndexs(GLshort Arg0)
{
    VirtGpuOglIndexf((GLfloat)Arg0);
}

static void APIENTRY
VirtGpuOglIndexsv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglIndexs(Arg0[0]);
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
    VirtGpuOglSetRasterPosition4f((GLfloat)Arg0, (GLfloat)Arg1, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglRasterPos2dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos2d(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglRasterPos2f(GLfloat Arg0, GLfloat Arg1)
{
    VirtGpuOglSetRasterPosition4f(Arg0, Arg1, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglRasterPos2fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos2f(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglRasterPos2i(GLint Arg0, GLint Arg1)
{
    VirtGpuOglSetRasterPosition4f((GLfloat)Arg0, (GLfloat)Arg1, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglRasterPos2iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos2i(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglRasterPos2s(GLshort Arg0, GLshort Arg1)
{
    VirtGpuOglSetRasterPosition4f((GLfloat)Arg0, (GLfloat)Arg1, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglRasterPos2sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos2s(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglRasterPos3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    VirtGpuOglSetRasterPosition4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, 1.0f);
}

static void APIENTRY
VirtGpuOglRasterPos3dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos3d(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglRasterPos3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    VirtGpuOglSetRasterPosition4f(Arg0, Arg1, Arg2, 1.0f);
}

static void APIENTRY
VirtGpuOglRasterPos3fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos3f(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglRasterPos3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    VirtGpuOglSetRasterPosition4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, 1.0f);
}

static void APIENTRY
VirtGpuOglRasterPos3iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos3i(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglRasterPos3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    VirtGpuOglSetRasterPosition4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, 1.0f);
}

static void APIENTRY
VirtGpuOglRasterPos3sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos3s(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglRasterPos4d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    VirtGpuOglSetRasterPosition4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglRasterPos4dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos4d(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglRasterPos4f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    VirtGpuOglSetRasterPosition4f(Arg0, Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglRasterPos4fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos4f(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglRasterPos4i(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    VirtGpuOglSetRasterPosition4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglRasterPos4iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos4i(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
}

static void APIENTRY
VirtGpuOglRasterPos4s(GLshort Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    VirtGpuOglSetRasterPosition4f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglRasterPos4sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglRasterPos4s(Arg0[0], Arg0[1], Arg0[2], Arg0[3]);
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if (Context == NULL)
        return;

    if ((Arg0 < GL_CLIP_PLANE0) || (Arg0 > GL_CLIP_PLANE5) || (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, (Arg1 == NULL) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Index = Arg0 - GL_CLIP_PLANE0;
    CopyMemory(Context->ClipPlanes[Index], Arg1, 4 * sizeof(GLdouble));
}

static void APIENTRY
VirtGpuOglColorMaterial(GLenum Arg0, GLenum Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

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

    switch (Arg1)
    {
        case GL_EMISSION:
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_AMBIENT_AND_DIFFUSE:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    Context->ColorMaterialFace = Arg0;
    Context->ColorMaterialMode = Arg1;
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FOG_MODE:
            VirtGpuOglFogi(Arg0, (GLint)Arg1);
            break;
        case GL_FOG_DENSITY:
            if (Arg1 < 0.0f)
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->FogDensity = Arg1;
            break;
        case GL_FOG_START:
            Context->FogStart = Arg1;
            break;
        case GL_FOG_END:
            Context->FogEnd = Arg1;
            break;
        case GL_FOG_INDEX:
            Context->FogIndex = Arg1;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglFogfv(GLenum Arg0, const GLfloat * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg0 == GL_FOG_COLOR)
    {
        if (Context != NULL)
            CopyMemory(Context->FogColor, Arg1, 4 * sizeof(GLfloat));
        return;
    }

    VirtGpuOglFogf(Arg0, Arg1[0]);
}

static void APIENTRY
VirtGpuOglFogi(GLenum Arg0, GLint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Arg0 == GL_FOG_MODE)
    {
        switch (Arg1)
        {
            case GL_LINEAR:
            case GL_EXP:
            case GL_EXP2:
                Context->FogMode = (GLenum)Arg1;
                return;
            default:
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                return;
        }
    }

    VirtGpuOglFogf(Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglFogiv(GLenum Arg0, const GLint * Arg1)
{
    GLfloat Color[4];

    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    if (Arg0 == GL_FOG_COLOR)
    {
        Color[0] = (GLfloat)Arg1[0];
        Color[1] = (GLfloat)Arg1[1];
        Color[2] = (GLfloat)Arg1[2];
        Color[3] = (GLfloat)Arg1[3];
        VirtGpuOglFogfv(Arg0, Color);
        return;
    }

    VirtGpuOglFogi(Arg0, Arg1[0]);
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    switch (Arg1)
    {
        case GL_FASTEST:
        case GL_NICEST:
        case GL_DONT_CARE:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    switch (Arg0)
    {
        case GL_PERSPECTIVE_CORRECTION_HINT:
        case GL_POINT_SMOOTH_HINT:
        case GL_LINE_SMOOTH_HINT:
        case GL_POLYGON_SMOOTH_HINT:
        case GL_FOG_HINT:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglLightf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg2);

    if ((Arg0 < GL_LIGHT0) || (Arg0 > GL_LIGHT7))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
        return;
    }

    switch (Arg1)
    {
        case GL_SPOT_EXPONENT:
        case GL_SPOT_CUTOFF:
        case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:
        case GL_QUADRATIC_ATTENUATION:
            break;
        default:
            VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglLightfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_POSITION:
        case GL_SPOT_DIRECTION:
            VirtGpuOglLightf(Arg0, GL_CONSTANT_ATTENUATION, 1.0f);
            break;
        default:
            VirtGpuOglLightf(Arg0, Arg1, Arg2[0]);
            break;
    }
}

static void APIENTRY
VirtGpuOglLighti(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    VirtGpuOglLightf(Arg0, Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglLightiv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    GLfloat Values[4];

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    Values[0] = (GLfloat)Arg2[0];
    Values[1] = (GLfloat)Arg2[1];
    Values[2] = (GLfloat)Arg2[2];
    Values[3] = (GLfloat)Arg2[3];
    VirtGpuOglLightfv(Arg0, Arg1, Values);
}

static void APIENTRY
VirtGpuOglLightModelf(GLenum Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    switch (Arg0)
    {
        case GL_LIGHT_MODEL_LOCAL_VIEWER:
        case GL_LIGHT_MODEL_TWO_SIDE:
            break;
        default:
            VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglLightModelfv(GLenum Arg0, const GLfloat * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    switch (Arg0)
    {
        case GL_LIGHT_MODEL_AMBIENT:
            break;
        default:
            VirtGpuOglLightModelf(Arg0, Arg1[0]);
            break;
    }
}

static void APIENTRY
VirtGpuOglLightModeli(GLenum Arg0, GLint Arg1)
{
    VirtGpuOglLightModelf(Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglLightModeliv(GLenum Arg0, const GLint * Arg1)
{
    GLfloat Values[4];

    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    Values[0] = (GLfloat)Arg1[0];
    Values[1] = (GLfloat)Arg1[1];
    Values[2] = (GLfloat)Arg1[2];
    Values[3] = (GLfloat)Arg1[3];
    VirtGpuOglLightModelfv(Arg0, Values);
}

static void APIENTRY
VirtGpuOglLineStipple(GLint Arg0, GLushort Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Arg0 <= 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->LineStippleFactor = Arg0;
    Context->LineStipplePattern = Arg1;
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
    UNREFERENCED_PARAMETER(Arg2);

    switch (Arg0)
    {
        case GL_FRONT:
        case GL_BACK:
        case GL_FRONT_AND_BACK:
            break;
        default:
            VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
            return;
    }

    switch (Arg1)
    {
        case GL_SHININESS:
            break;
        default:
            VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglMaterialfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_EMISSION:
        case GL_AMBIENT_AND_DIFFUSE:
        case GL_COLOR_INDEXES:
            switch (Arg0)
            {
                case GL_FRONT:
                case GL_BACK:
                case GL_FRONT_AND_BACK:
                    break;
                default:
                    VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
                    break;
            }
            break;
        default:
            VirtGpuOglMaterialf(Arg0, Arg1, Arg2[0]);
            break;
    }
}

static void APIENTRY
VirtGpuOglMateriali(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    VirtGpuOglMaterialf(Arg0, Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglMaterialiv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    GLfloat Values[4];

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    Values[0] = (GLfloat)Arg2[0];
    Values[1] = (GLfloat)Arg2[1];
    Values[2] = (GLfloat)Arg2[2];
    Values[3] = (GLfloat)Arg2[3];
    VirtGpuOglMaterialfv(Arg0, Arg1, Values);
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Arg0 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    CopyMemory(Context->PolygonStipple, Arg0, sizeof(Context->PolygonStipple));
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

    if ((Arg0 != GL_TEXTURE_1D) &&
        (Arg0 != GL_TEXTURE_2D) &&
        (Arg0 != GL_TEXTURE_3D) &&
        (Arg0 != GL_TEXTURE_BUFFER))
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
        case GL_TEXTURE_WRAP_R:
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
        case GL_TEXTURE_WRAP_R:
            Texture->WrapR = Arg2;
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

static BOOL
VirtGpuOglTexGenCoordIndex(_In_ GLenum Coord, _Out_ PULONG Index)
{
    switch (Coord)
    {
        case GL_S:
            *Index = 0;
            return TRUE;
        case GL_T:
            *Index = 1;
            return TRUE;
        case GL_R:
            *Index = 2;
            return TRUE;
        case GL_Q:
            *Index = 3;
            return TRUE;
        default:
            return FALSE;
    }
}

static void APIENTRY
VirtGpuOglTexEnvf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    VirtGpuOglTexEnvi(Arg0, Arg1, (GLint)Arg2);
}

static void APIENTRY
VirtGpuOglTexEnvfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg0 == GL_TEXTURE_ENV) && (Arg1 == GL_TEXTURE_ENV_COLOR))
    {
        if (Context != NULL)
            CopyMemory(Context->TexEnvColor, Arg2, 4 * sizeof(GLfloat));
        return;
    }

    VirtGpuOglTexEnvf(Arg0, Arg1, Arg2[0]);
}

static void APIENTRY
VirtGpuOglTexEnvi(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Arg0 != GL_TEXTURE_ENV) || (Arg1 != GL_TEXTURE_ENV_MODE))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    switch (Arg2)
    {
        case GL_MODULATE:
        case GL_DECAL:
        case GL_BLEND:
        case GL_REPLACE:
            Context->TexEnvMode = (GLenum)Arg2;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglTexEnviv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    GLfloat Color[4];

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    if ((Arg0 == GL_TEXTURE_ENV) && (Arg1 == GL_TEXTURE_ENV_COLOR))
    {
        Color[0] = (GLfloat)Arg2[0];
        Color[1] = (GLfloat)Arg2[1];
        Color[2] = (GLfloat)Arg2[2];
        Color[3] = (GLfloat)Arg2[3];
        VirtGpuOglTexEnvfv(Arg0, Arg1, Color);
        return;
    }

    VirtGpuOglTexEnvi(Arg0, Arg1, Arg2[0]);
}

static void APIENTRY
VirtGpuOglTexGend(GLenum Arg0, GLenum Arg1, GLdouble Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if ((Context == NULL) || !VirtGpuOglTexGenCoordIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Arg1 != GL_TEXTURE_GEN_MODE)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    switch ((GLenum)Arg2)
    {
        case GL_OBJECT_LINEAR:
        case GL_EYE_LINEAR:
        case GL_SPHERE_MAP:
            Context->TexGenMode[Index] = (GLenum)Arg2;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglTexGendv(GLenum Arg0, GLenum Arg1, const GLdouble * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if ((Arg2 == NULL) || (Context == NULL) || !VirtGpuOglTexGenCoordIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, (Arg2 == NULL) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    switch (Arg1)
    {
        case GL_TEXTURE_GEN_MODE:
            VirtGpuOglTexGend(Arg0, Arg1, Arg2[0]);
            break;
        case GL_OBJECT_PLANE:
            CopyMemory(Context->TexGenObjectPlane[Index], Arg2, 4 * sizeof(GLdouble));
            break;
        case GL_EYE_PLANE:
            CopyMemory(Context->TexGenEyePlane[Index], Arg2, 4 * sizeof(GLdouble));
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglTexGenf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    VirtGpuOglTexGend(Arg0, Arg1, (GLdouble)Arg2);
}

static void APIENTRY
VirtGpuOglTexGenfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    GLdouble Values[4];

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    Values[0] = (GLdouble)Arg2[0];
    Values[1] = (GLdouble)Arg2[1];
    Values[2] = (GLdouble)Arg2[2];
    Values[3] = (GLdouble)Arg2[3];
    VirtGpuOglTexGendv(Arg0, Arg1, Values);
}

static void APIENTRY
VirtGpuOglTexGeni(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    VirtGpuOglTexGend(Arg0, Arg1, (GLdouble)Arg2);
}

static void APIENTRY
VirtGpuOglTexGeniv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    GLdouble Values[4];

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    Values[0] = (GLdouble)Arg2[0];
    Values[1] = (GLdouble)Arg2[1];
    Values[2] = (GLdouble)Arg2[2];
    Values[3] = (GLdouble)Arg2[3];
    VirtGpuOglTexGendv(Arg0, Arg1, Values);
}

static void APIENTRY
VirtGpuOglFeedbackBuffer(GLsizei Arg0, GLenum Arg1, GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Context->RenderMode == GL_FEEDBACK)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (Arg0 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_2D:
        case GL_3D:
        case GL_3D_COLOR:
        case GL_3D_COLOR_TEXTURE:
        case GL_4D_COLOR_TEXTURE:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    Context->FeedbackBufferSize = Arg0;
    Context->FeedbackBufferType = Arg1;
    Context->FeedbackBuffer = Arg2;
    Context->FeedbackBufferUsed = 0;
}

static void APIENTRY
VirtGpuOglSelectBuffer(GLsizei Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Context->RenderMode == GL_SELECT)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (Arg0 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->SelectBufferSize = Arg0;
    Context->SelectBuffer = Arg1;
    Context->SelectHits = 0;
}

static GLint APIENTRY
VirtGpuOglRenderMode(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLenum OldMode;
    GLint Result = 0;

    if (Context == NULL)
        return 0;

    switch (Arg0)
    {
        case GL_RENDER:
        case GL_SELECT:
        case GL_FEEDBACK:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return 0;
    }

    if (Context->BeginMode != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return 0;
    }

    if ((Arg0 == GL_SELECT) &&
        ((Context->SelectBuffer == NULL) || (Context->SelectBufferSize == 0)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return 0;
    }

    if ((Arg0 == GL_FEEDBACK) &&
        ((Context->FeedbackBuffer == NULL) || (Context->FeedbackBufferSize == 0)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return 0;
    }

    OldMode = Context->RenderMode;
    if (OldMode == GL_SELECT)
        Result = (GLint)Context->SelectHits;
    else if (OldMode == GL_FEEDBACK)
        Result = Context->FeedbackBufferUsed;

    Context->RenderMode = Arg0;
    if (Arg0 == GL_SELECT)
    {
        Context->SelectHits = 0;
        Context->NameStackDepth = 0;
    }
    else if (Arg0 == GL_FEEDBACK)
    {
        Context->FeedbackBufferUsed = 0;
    }

    return Result;
}

static void APIENTRY
VirtGpuOglInitNames(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Context->RenderMode != GL_SELECT) || (Context->BeginMode != 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Context->NameStackDepth = 0;
}

static void APIENTRY
VirtGpuOglLoadName(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Context->RenderMode != GL_SELECT) ||
        (Context->BeginMode != 0) ||
        (Context->NameStackDepth == 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Context->NameStack[Context->NameStackDepth - 1] = Arg0;
}

static void APIENTRY
VirtGpuOglPassThrough(GLfloat Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Context->RenderMode != GL_FEEDBACK)
        return;

    if ((Context->FeedbackBuffer != NULL) &&
        (Context->FeedbackBufferUsed + 2 <= Context->FeedbackBufferSize))
    {
        Context->FeedbackBuffer[Context->FeedbackBufferUsed++] = (GLfloat)GL_PASS_THROUGH_TOKEN;
        Context->FeedbackBuffer[Context->FeedbackBufferUsed++] = Arg0;
    }
}

static void APIENTRY
VirtGpuOglPopName(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Context->RenderMode != GL_SELECT) ||
        (Context->BeginMode != 0) ||
        (Context->NameStackDepth == 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    --Context->NameStackDepth;
}

static void APIENTRY
VirtGpuOglPushName(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Context->RenderMode != GL_SELECT) || (Context->BeginMode != 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (Context->NameStackDepth >= VIRTGPU_OGL_NAME_STACK_DEPTH)
    {
        VirtGpuOglSetError(Context, GL_STACK_OVERFLOW);
        return;
    }

    Context->NameStack[Context->NameStackDepth++] = Arg0;
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    Context->ClearAccum[0] = VirtGpuOglClampFloat(Arg0);
    Context->ClearAccum[1] = VirtGpuOglClampFloat(Arg1);
    Context->ClearAccum[2] = VirtGpuOglClampFloat(Arg2);
    Context->ClearAccum[3] = VirtGpuOglClampFloat(Arg3);
}

static void APIENTRY
VirtGpuOglClearIndex(GLfloat Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context != NULL)
        Context->ClearIndex = Arg0;
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context != NULL)
        Context->IndexMask = Arg0;
}

static void APIENTRY
VirtGpuOglAccum(GLenum Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    switch (Arg0)
    {
        case GL_ACCUM:
        case GL_LOAD:
        case GL_RETURN:
        case GL_MULT:
        case GL_ADD:
            break;
        default:
            VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglDisable(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_LIST_COMMAND Command;
    GLbitfield Bit;
    ULONGLONG EvalBit;

    if (Context == NULL)
        return;

    if (!VirtGpuOglCapToBit(Arg0, &Bit))
    {
        if (VirtGpuOglEvalCapToBit(Arg0, &EvalBit))
        {
            if (VirtGpuOglShouldRecordList(Context))
            {
                Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_DISABLE);
                if (Command != NULL)
                    Command->EnumArgs[0] = Arg0;

                if (VirtGpuOglRecordingCompileOnly(Context))
                    return;
            }

            Context->EvalEnableBits &= ~EvalBit;
        }
        else
        {
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        }
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
    ULONGLONG EvalBit;

    if (Context == NULL)
        return;

    if (!VirtGpuOglCapToBit(Arg0, &Bit))
    {
        if (VirtGpuOglEvalCapToBit(Arg0, &EvalBit))
        {
            if (VirtGpuOglShouldRecordList(Context))
            {
                Command = VirtGpuOglRecordListCommand(Context, VIRTGPU_OGL_LIST_ENABLE);
                if (Command != NULL)
                    Command->EnumArgs[0] = Arg0;

                if (VirtGpuOglRecordingCompileOnly(Context))
                    return;
            }

            Context->EvalEnableBits |= EvalBit;
        }
        else
        {
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        }
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
    /* Attribute stack restore is not needed by the current software fallback path. */
}

static void APIENTRY
VirtGpuOglPushAttrib(GLbitfield Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
}

static VOID
VirtGpuOglMap1Common(
    _In_ GLenum Target,
    _In_ GLdouble U1,
    _In_ GLdouble U2,
    _In_ GLint Stride,
    _In_ GLint Order,
    _In_ const VOID *Points,
    _In_ BOOL FloatPoints)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    GLint Components;
    GLdouble *Copy;
    GLint I;
    GLint Component;

    if (Context == NULL)
        return;

    if (!VirtGpuOglEvalMapTargetComponents(Target, FALSE, &Index, &Components))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Points == NULL) ||
        (U1 == U2) ||
        (Stride < Components) ||
        (Order < 1) ||
        (Order > VIRTGPU_OGL_MAX_EVAL_ORDER))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Copy = HeapAlloc(GetProcessHeap(),
                     HEAP_ZERO_MEMORY,
                     (SIZE_T)Order * (SIZE_T)Components * sizeof(GLdouble));
    if (Copy == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    for (I = 0; I < Order; ++I)
    {
        for (Component = 0; Component < Components; ++Component)
        {
            if (FloatPoints)
            {
                const GLfloat *Source = (const GLfloat *)Points;
                Copy[(I * Components) + Component] = Source[(I * Stride) + Component];
            }
            else
            {
                const GLdouble *Source = (const GLdouble *)Points;
                Copy[(I * Components) + Component] = Source[(I * Stride) + Component];
            }
        }
    }

    VirtGpuOglFreeEvalMap1(&Context->EvalMap1[Index]);
    Context->EvalMap1[Index].Defined = TRUE;
    Context->EvalMap1[Index].Target = Target;
    Context->EvalMap1[Index].Components = Components;
    Context->EvalMap1[Index].U1 = U1;
    Context->EvalMap1[Index].U2 = U2;
    Context->EvalMap1[Index].Order = Order;
    Context->EvalMap1[Index].Points = Copy;
}

static VOID
VirtGpuOglMap2Common(
    _In_ GLenum Target,
    _In_ GLdouble U1,
    _In_ GLdouble U2,
    _In_ GLint UStride,
    _In_ GLint UOrder,
    _In_ GLdouble V1,
    _In_ GLdouble V2,
    _In_ GLint VStride,
    _In_ GLint VOrder,
    _In_ const VOID *Points,
    _In_ BOOL FloatPoints)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    GLint Components;
    GLdouble *Copy;
    GLint U;
    GLint V;
    GLint Component;

    if (Context == NULL)
        return;

    if (!VirtGpuOglEvalMapTargetComponents(Target, TRUE, &Index, &Components))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Points == NULL) ||
        (U1 == U2) ||
        (V1 == V2) ||
        (UStride < Components) ||
        (VStride < Components) ||
        (UOrder < 1) ||
        (VOrder < 1) ||
        (UOrder > VIRTGPU_OGL_MAX_EVAL_ORDER) ||
        (VOrder > VIRTGPU_OGL_MAX_EVAL_ORDER))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Copy = HeapAlloc(GetProcessHeap(),
                     HEAP_ZERO_MEMORY,
                     (SIZE_T)UOrder * (SIZE_T)VOrder * (SIZE_T)Components * sizeof(GLdouble));
    if (Copy == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    for (U = 0; U < UOrder; ++U)
    {
        for (V = 0; V < VOrder; ++V)
        {
            for (Component = 0; Component < Components; ++Component)
            {
                ULONG Destination = (((ULONG)U * (ULONG)VOrder + (ULONG)V) *
                                     (ULONG)Components) + (ULONG)Component;
                ULONG SourceOffset = ((ULONG)U * (ULONG)UStride) +
                                     ((ULONG)V * (ULONG)VStride) +
                                     (ULONG)Component;

                if (FloatPoints)
                {
                    const GLfloat *Source = (const GLfloat *)Points;
                    Copy[Destination] = Source[SourceOffset];
                }
                else
                {
                    const GLdouble *Source = (const GLdouble *)Points;
                    Copy[Destination] = Source[SourceOffset];
                }
            }
        }
    }

    VirtGpuOglFreeEvalMap2(&Context->EvalMap2[Index]);
    Context->EvalMap2[Index].Defined = TRUE;
    Context->EvalMap2[Index].Target = Target;
    Context->EvalMap2[Index].Components = Components;
    Context->EvalMap2[Index].U1 = U1;
    Context->EvalMap2[Index].U2 = U2;
    Context->EvalMap2[Index].V1 = V1;
    Context->EvalMap2[Index].V2 = V2;
    Context->EvalMap2[Index].UOrder = UOrder;
    Context->EvalMap2[Index].VOrder = VOrder;
    Context->EvalMap2[Index].Points = Copy;
}

static VOID
VirtGpuOglEvalMap1Value(
    _In_ const VIRTGPU_OGL_EVAL_MAP1 *Map,
    _In_ GLdouble U,
    _Out_writes_(4) GLdouble *Result)
{
    VirtGpuOglDeCasteljau(Map->Points,
                          Map->Order,
                          Map->Components,
                          VirtGpuOglEvalParameter(U, Map->U1, Map->U2),
                          Result);
}

static VOID
VirtGpuOglEvalMap2Value(
    _In_ const VIRTGPU_OGL_EVAL_MAP2 *Map,
    _In_ GLdouble U,
    _In_ GLdouble V,
    _Out_writes_(4) GLdouble *Result)
{
    GLdouble UResults[VIRTGPU_OGL_MAX_EVAL_ORDER * 4];
    GLdouble USource[VIRTGPU_OGL_MAX_EVAL_ORDER * 4];
    GLint VIndex;
    GLint UIndex;
    GLint Component;

    for (VIndex = 0; VIndex < Map->VOrder; ++VIndex)
    {
        GLdouble UResult[4];

        for (UIndex = 0; UIndex < Map->UOrder; ++UIndex)
        {
            for (Component = 0; Component < Map->Components; ++Component)
            {
                USource[(UIndex * Map->Components) + Component] =
                    Map->Points[(((UIndex * Map->VOrder) + VIndex) *
                                 Map->Components) + Component];
            }
        }

        VirtGpuOglDeCasteljau(USource,
                              Map->UOrder,
                              Map->Components,
                              VirtGpuOglEvalParameter(U, Map->U1, Map->U2),
                              UResult);

        for (Component = 0; Component < Map->Components; ++Component)
            UResults[(VIndex * Map->Components) + Component] = UResult[Component];
    }

    VirtGpuOglDeCasteljau(UResults,
                          Map->VOrder,
                          Map->Components,
                          VirtGpuOglEvalParameter(V, Map->V1, Map->V2),
                          Result);
}

static VOID
VirtGpuOglEvalCoord1Common(_In_ GLdouble U)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Pass;
    ULONG Index;

    if (Context == NULL)
        return;

    for (Pass = 0; Pass < 2; ++Pass)
    {
        for (Index = 0; Index < VIRTGPU_OGL_EVAL_MAP_COUNT; ++Index)
        {
            GLenum Target = GL_MAP1_COLOR_4 + Index;
            GLdouble Result[4];
            BOOL IsVertex = (Target == GL_MAP1_VERTEX_3) || (Target == GL_MAP1_VERTEX_4);

            if ((Pass == 0) == IsVertex)
                continue;

            if (((Context->EvalEnableBits & (1ULL << Index)) == 0) ||
                !Context->EvalMap1[Index].Defined)
            {
                continue;
            }

            VirtGpuOglEvalMap1Value(&Context->EvalMap1[Index], U, Result);
            VirtGpuOglApplyEvalResult(Target, Result);
        }
    }
}

static VOID
VirtGpuOglEvalCoord2Common(_In_ GLdouble U, _In_ GLdouble V)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Pass;
    ULONG Index;

    if (Context == NULL)
        return;

    for (Pass = 0; Pass < 2; ++Pass)
    {
        for (Index = 0; Index < VIRTGPU_OGL_EVAL_MAP_COUNT; ++Index)
        {
            GLenum Target = GL_MAP2_COLOR_4 + Index;
            GLdouble Result[4];
            BOOL IsVertex = (Target == GL_MAP2_VERTEX_3) || (Target == GL_MAP2_VERTEX_4);

            if ((Pass == 0) == IsVertex)
                continue;

            if (((Context->EvalEnableBits &
                  (1ULL << (VIRTGPU_OGL_EVAL_MAP_COUNT + Index))) == 0) ||
                !Context->EvalMap2[Index].Defined)
            {
                continue;
            }

            VirtGpuOglEvalMap2Value(&Context->EvalMap2[Index], U, V, Result);
            VirtGpuOglApplyEvalResult(Target, Result);
        }
    }
}

static void APIENTRY
VirtGpuOglMap1d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2, GLint Arg3, GLint Arg4, const GLdouble * Arg5)
{
    VirtGpuOglMap1Common(Arg0, Arg1, Arg2, Arg3, Arg4, Arg5, FALSE);
}

static void APIENTRY
VirtGpuOglMap1f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2, GLint Arg3, GLint Arg4, const GLfloat * Arg5)
{
    VirtGpuOglMap1Common(Arg0, Arg1, Arg2, Arg3, Arg4, Arg5, TRUE);
}

static void APIENTRY
VirtGpuOglMap2d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2, GLint Arg3, GLint Arg4, GLdouble Arg5, GLdouble Arg6, GLint Arg7, GLint Arg8, const GLdouble * Arg9)
{
    VirtGpuOglMap2Common(Arg0, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6, Arg7, Arg8, Arg9, FALSE);
}

static void APIENTRY
VirtGpuOglMap2f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2, GLint Arg3, GLint Arg4, GLfloat Arg5, GLfloat Arg6, GLint Arg7, GLint Arg8, const GLfloat * Arg9)
{
    VirtGpuOglMap2Common(Arg0, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6, Arg7, Arg8, Arg9, TRUE);
}

static void APIENTRY
VirtGpuOglMapGrid1d(GLint Arg0, GLdouble Arg1, GLdouble Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Arg0 < 1)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->MapGrid1[0] = Arg0;
    Context->MapGrid1[1] = Arg1;
    Context->MapGrid1[2] = Arg2;
}

static void APIENTRY
VirtGpuOglMapGrid1f(GLint Arg0, GLfloat Arg1, GLfloat Arg2)
{
    VirtGpuOglMapGrid1d(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglMapGrid2d(GLint Arg0, GLdouble Arg1, GLdouble Arg2, GLint Arg3, GLdouble Arg4, GLdouble Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Arg0 < 1) || (Arg3 < 1))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->MapGrid2[0] = Arg0;
    Context->MapGrid2[1] = Arg1;
    Context->MapGrid2[2] = Arg2;
    Context->MapGrid2[3] = Arg3;
    Context->MapGrid2[4] = Arg4;
    Context->MapGrid2[5] = Arg5;
}

static void APIENTRY
VirtGpuOglMapGrid2f(GLint Arg0, GLfloat Arg1, GLfloat Arg2, GLint Arg3, GLfloat Arg4, GLfloat Arg5)
{
    VirtGpuOglMapGrid2d(Arg0, Arg1, Arg2, Arg3, Arg4, Arg5);
}

static void APIENTRY
VirtGpuOglEvalCoord1d(GLdouble Arg0)
{
    VirtGpuOglEvalCoord1Common(Arg0);
}

static void APIENTRY
VirtGpuOglEvalCoord1dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglEvalCoord1d(Arg0[0]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglEvalCoord1f(GLfloat Arg0)
{
    VirtGpuOglEvalCoord1Common(Arg0);
}

static void APIENTRY
VirtGpuOglEvalCoord1fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglEvalCoord1f(Arg0[0]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglEvalCoord2d(GLdouble Arg0, GLdouble Arg1)
{
    VirtGpuOglEvalCoord2Common(Arg0, Arg1);
}

static void APIENTRY
VirtGpuOglEvalCoord2dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglEvalCoord2d(Arg0[0], Arg0[1]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglEvalCoord2f(GLfloat Arg0, GLfloat Arg1)
{
    VirtGpuOglEvalCoord2Common(Arg0, Arg1);
}

static void APIENTRY
VirtGpuOglEvalCoord2fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglEvalCoord2f(Arg0[0], Arg0[1]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglEvalMesh1(GLenum Arg0, GLint Arg1, GLint Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLint I;

    if (Context == NULL)
        return;

    if ((Arg0 != GL_POINT) && (Arg0 != GL_LINE))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Context->BeginMode != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    VirtGpuOglBegin((Arg0 == GL_POINT) ? GL_POINTS : GL_LINE_STRIP);
    for (I = Arg1; I <= Arg2; ++I)
        VirtGpuOglEvalPoint1(I);
    VirtGpuOglEnd();
}

static void APIENTRY
VirtGpuOglEvalPoint1(GLint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLdouble U;

    if (Context == NULL)
        return;

    U = Context->MapGrid1[1];
    if (Context->MapGrid1[0] != 0.0)
    {
        U += ((Context->MapGrid1[2] - Context->MapGrid1[1]) *
              (GLdouble)Arg0) / Context->MapGrid1[0];
    }

    VirtGpuOglEvalCoord1Common(U);
}

static void APIENTRY
VirtGpuOglEvalMesh2(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLint I;
    GLint J;

    if (Context == NULL)
        return;

    if ((Arg0 != GL_POINT) && (Arg0 != GL_LINE) && (Arg0 != GL_FILL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Context->BeginMode != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (Arg0 == GL_POINT)
    {
        VirtGpuOglBegin(GL_POINTS);
        for (J = Arg3; J <= Arg4; ++J)
            for (I = Arg1; I <= Arg2; ++I)
                VirtGpuOglEvalPoint2(I, J);
        VirtGpuOglEnd();
    }
    else if (Arg0 == GL_LINE)
    {
        for (J = Arg3; J <= Arg4; ++J)
        {
            VirtGpuOglBegin(GL_LINE_STRIP);
            for (I = Arg1; I <= Arg2; ++I)
                VirtGpuOglEvalPoint2(I, J);
            VirtGpuOglEnd();
        }
        for (I = Arg1; I <= Arg2; ++I)
        {
            VirtGpuOglBegin(GL_LINE_STRIP);
            for (J = Arg3; J <= Arg4; ++J)
                VirtGpuOglEvalPoint2(I, J);
            VirtGpuOglEnd();
        }
    }
    else
    {
        for (J = Arg3; J < Arg4; ++J)
        {
            VirtGpuOglBegin(GL_QUAD_STRIP);
            for (I = Arg1; I <= Arg2; ++I)
            {
                VirtGpuOglEvalPoint2(I, J);
                VirtGpuOglEvalPoint2(I, J + 1);
            }
            VirtGpuOglEnd();
        }
    }
}

static void APIENTRY
VirtGpuOglEvalPoint2(GLint Arg0, GLint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLdouble U;
    GLdouble V;

    if (Context == NULL)
        return;

    U = Context->MapGrid2[1];
    V = Context->MapGrid2[4];
    if (Context->MapGrid2[0] != 0.0)
    {
        U += ((Context->MapGrid2[2] - Context->MapGrid2[1]) *
              (GLdouble)Arg0) / Context->MapGrid2[0];
    }
    if (Context->MapGrid2[3] != 0.0)
    {
        V += ((Context->MapGrid2[5] - Context->MapGrid2[4]) *
              (GLdouble)Arg1) / Context->MapGrid2[3];
    }

    VirtGpuOglEvalCoord2Common(U, V);
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (!VirtGpuOglValidLogicOp(Arg0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Context->LogicOpMode = Arg0;
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

static BOOL
VirtGpuOglDrawPixelFormatBytes(
    _In_ GLenum Format,
    _In_ GLenum Type,
    _Out_ PULONG Bytes)
{
    if (Type != GL_UNSIGNED_BYTE)
        return FALSE;

    switch (Format)
    {
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_ALPHA:
        case GL_LUMINANCE:
        case GL_COLOR_INDEX:
        case GL_STENCIL_INDEX:
            *Bytes = 1;
            return TRUE;
        case GL_LUMINANCE_ALPHA:
            *Bytes = 2;
            return TRUE;
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

static COLORREF
VirtGpuOglColorFromPixel(
    _In_ GLenum Format,
    _In_reads_(4) const BYTE *Pixel)
{
    switch (Format)
    {
        case GL_RED:
            return RGB(Pixel[0], 0, 0);
        case GL_GREEN:
            return RGB(0, Pixel[0], 0);
        case GL_BLUE:
            return RGB(0, 0, Pixel[0]);
        case GL_ALPHA:
            return RGB(255, 255, 255);
        case GL_LUMINANCE:
        case GL_COLOR_INDEX:
        case GL_STENCIL_INDEX:
            return RGB(Pixel[0], Pixel[0], Pixel[0]);
        case GL_LUMINANCE_ALPHA:
            return RGB(Pixel[0], Pixel[0], Pixel[0]);
        case GL_RGB:
            return RGB(Pixel[0], Pixel[1], Pixel[2]);
        case GL_RGBA:
            return RGB(Pixel[0], Pixel[1], Pixel[2]);
        default:
            return RGB(0, 0, 0);
    }
}

static BOOL
VirtGpuOglValidPixelTransferParameter(_In_ GLenum Parameter)
{
    switch (Parameter)
    {
        case GL_MAP_COLOR:
        case GL_MAP_STENCIL:
        case GL_INDEX_SHIFT:
        case GL_INDEX_OFFSET:
        case GL_RED_SCALE:
        case GL_RED_BIAS:
        case GL_GREEN_SCALE:
        case GL_GREEN_BIAS:
        case GL_BLUE_SCALE:
        case GL_BLUE_BIAS:
        case GL_ALPHA_SCALE:
        case GL_ALPHA_BIAS:
        case GL_DEPTH_SCALE:
        case GL_DEPTH_BIAS:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
VirtGpuOglValidPixelMapParameter(_In_ GLenum Map)
{
    ULONG Index;

    return VirtGpuOglPixelMapToIndex(Map, &Index);
}

static VOID
VirtGpuOglStorePixelMap(
    _In_ GLenum Map,
    _In_ GLint Size,
    _In_ const VOID *Values,
    _In_ GLenum Type)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    GLfloat *Copy;
    GLint I;

    if (Context == NULL)
        return;

    if (!VirtGpuOglPixelMapToIndex(Map, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Size < 1) || (Size > VIRTGPU_OGL_MAX_PIXEL_MAP_TABLE) || (Values == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Copy = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)Size * sizeof(GLfloat));
    if (Copy == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    for (I = 0; I < Size; ++I)
    {
        if (Type == GL_FLOAT)
            Copy[I] = ((const GLfloat *)Values)[I];
        else if (Type == GL_UNSIGNED_INT)
            Copy[I] = (GLfloat)((const GLuint *)Values)[I];
        else
            Copy[I] = (GLfloat)((const GLushort *)Values)[I];
    }

    VirtGpuOglFreePixelMap(&Context->PixelMaps[Index]);
    Context->PixelMaps[Index].Size = Size;
    Context->PixelMaps[Index].Values = Copy;
}

static void APIENTRY
VirtGpuOglPixelZoom(GLfloat Arg0, GLfloat Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    Context->PixelZoomX = Arg0;
    Context->PixelZoomY = Arg1;
}

static void APIENTRY
VirtGpuOglPixelTransferf(GLenum Arg0, GLfloat Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    if (!VirtGpuOglValidPixelTransferParameter(Arg0))
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
}

static void APIENTRY
VirtGpuOglPixelTransferi(GLenum Arg0, GLint Arg1)
{
    VirtGpuOglPixelTransferf(Arg0, (GLfloat)Arg1);
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
        case GL_PACK_ROW_LENGTH:
            if (Arg1 < 0)
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->PackRowLength = Arg1;
            break;
        case GL_PACK_SKIP_ROWS:
            if (Arg1 < 0)
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->PackSkipRows = Arg1;
            break;
        case GL_PACK_SKIP_PIXELS:
            if (Arg1 < 0)
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->PackSkipPixels = Arg1;
            break;
        case GL_PACK_SWAP_BYTES:
            Context->PackSwapBytes = Arg1 ? GL_TRUE : GL_FALSE;
            break;
        case GL_PACK_LSB_FIRST:
            Context->PackLsbFirst = Arg1 ? GL_TRUE : GL_FALSE;
            break;
        case GL_UNPACK_ALIGNMENT:
            if (!VirtGpuOglValidPixelAlignment(Arg1))
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->UnpackAlignment = Arg1;
            break;
        case GL_UNPACK_ROW_LENGTH:
            if (Arg1 < 0)
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->UnpackRowLength = Arg1;
            break;
        case GL_UNPACK_SKIP_ROWS:
            if (Arg1 < 0)
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->UnpackSkipRows = Arg1;
            break;
        case GL_UNPACK_SKIP_PIXELS:
            if (Arg1 < 0)
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            Context->UnpackSkipPixels = Arg1;
            break;
        case GL_UNPACK_SWAP_BYTES:
            Context->UnpackSwapBytes = Arg1 ? GL_TRUE : GL_FALSE;
            break;
        case GL_UNPACK_LSB_FIRST:
            Context->UnpackLsbFirst = Arg1 ? GL_TRUE : GL_FALSE;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglPixelMapfv(GLenum Arg0, GLint Arg1, const GLfloat * Arg2)
{
    VirtGpuOglStorePixelMap(Arg0, Arg1, Arg2, GL_FLOAT);
}

static void APIENTRY
VirtGpuOglPixelMapuiv(GLenum Arg0, GLint Arg1, const GLuint * Arg2)
{
    VirtGpuOglStorePixelMap(Arg0, Arg1, Arg2, GL_UNSIGNED_INT);
}

static void APIENTRY
VirtGpuOglPixelMapusv(GLenum Arg0, GLint Arg1, const GLushort * Arg2)
{
    VirtGpuOglStorePixelMap(Arg0, Arg1, Arg2, GL_UNSIGNED_SHORT);
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONGLONG Size64;
    ULONG Size;
    GLint OldPackRowLength;
    GLint OldPackSkipRows;
    GLint OldPackSkipPixels;
    GLint OldUnpackRowLength;
    GLint OldUnpackSkipRows;
    GLint OldUnpackSkipPixels;

    if (Context == NULL)
        return;

    if ((Arg2 < 0) || (Arg3 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg2 == 0) || (Arg3 == 0))
        return;

    if (Arg4 != GL_COLOR)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Size64 = (ULONGLONG)(ULONG)Arg2 * (ULONGLONG)(ULONG)Arg3 * 4ULL;
    if (Size64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    Size = (ULONG)Size64;
    Pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (Pixels == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    OldPackRowLength = Context->PackRowLength;
    OldPackSkipRows = Context->PackSkipRows;
    OldPackSkipPixels = Context->PackSkipPixels;
    OldUnpackRowLength = Context->UnpackRowLength;
    OldUnpackSkipRows = Context->UnpackSkipRows;
    OldUnpackSkipPixels = Context->UnpackSkipPixels;

    Context->PackRowLength = 0;
    Context->PackSkipRows = 0;
    Context->PackSkipPixels = 0;
    Context->UnpackRowLength = 0;
    Context->UnpackSkipRows = 0;
    Context->UnpackSkipPixels = 0;

    VirtGpuOglReadPixels(Arg0, Arg1, Arg2, Arg3, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    if (Context->LastError == GL_NO_ERROR)
        VirtGpuOglDrawPixels(Arg2, Arg3, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);

    Context->PackRowLength = OldPackRowLength;
    Context->PackSkipRows = OldPackSkipRows;
    Context->PackSkipPixels = OldPackSkipPixels;
    Context->UnpackRowLength = OldUnpackRowLength;
    Context->UnpackSkipRows = OldUnpackSkipRows;
    Context->UnpackSkipPixels = OldUnpackSkipPixels;

    HeapFree(GetProcessHeap(), 0, Pixels);
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

    Destination = (PUCHAR)Arg6;
    DestinationStride =
        VirtGpuOglAlignedRowSize((ULONG)((Context->PackRowLength > 0) ?
                                         Context->PackRowLength :
                                         Arg2) * BytesPerPixel,
                                 Context->PackAlignment);
    Destination += ((ULONG)Context->PackSkipRows * DestinationStride) +
                   ((ULONG)Context->PackSkipPixels * BytesPerPixel);

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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG BytesPerPixel;
    ULONG SourceStride;
    GLsizei Row;
    GLsizei Column;
    INT ZoomX;
    INT ZoomY;
    INT XDirection;
    INT YDirection;

    if (Context == NULL)
        return;

    if ((Arg0 < 0) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg4 == NULL) && (Arg0 != 0) && (Arg1 != 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglDrawPixelFormatBytes(Arg2, Arg3, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (!Context->CurrentRasterPositionValid || (Arg0 == 0) || (Arg1 == 0))
        return;

    ZoomX = VirtGpuOglRoundFloat(Context->PixelZoomX);
    ZoomY = VirtGpuOglRoundFloat(Context->PixelZoomY);
    if (ZoomX == 0)
        ZoomX = (Context->PixelZoomX < 0.0f) ? -1 : 1;
    if (ZoomY == 0)
        ZoomY = (Context->PixelZoomY < 0.0f) ? -1 : 1;

    XDirection = (ZoomX < 0) ? -1 : 1;
    YDirection = (ZoomY < 0) ? 1 : -1;
    if (ZoomX < 0)
        ZoomX = -ZoomX;
    if (ZoomY < 0)
        ZoomY = -ZoomY;

    SourceStride =
        VirtGpuOglAlignedRowSize((ULONG)((Context->UnpackRowLength > 0) ?
                                         Context->UnpackRowLength :
                                         Arg0) * BytesPerPixel,
                                 Context->UnpackAlignment);

    for (Row = 0; Row < Arg1; ++Row)
    {
        const BYTE *SourceRow = (const BYTE *)Arg4 +
                                ((ULONG)(Context->UnpackSkipRows + Row) * SourceStride) +
                                ((ULONG)Context->UnpackSkipPixels * BytesPerPixel);

        for (Column = 0; Column < Arg0; ++Column)
        {
            COLORREF Color = VirtGpuOglColorFromPixel(Arg2,
                                                      SourceRow + ((ULONG)Column * BytesPerPixel));
            INT BaseX = Context->CurrentRasterWindow.x + (Column * ZoomX * XDirection);
            INT BaseY = Context->CurrentRasterWindow.y + (Row * ZoomY * YDirection);
            INT X;
            INT Y;

            for (Y = 0; Y < ZoomY; ++Y)
            {
                for (X = 0; X < ZoomX; ++X)
                {
                    SetPixel(Context->hdc,
                             BaseX + (X * XDirection),
                             BaseY + (Y * YDirection),
                             Color);
                }
            }
        }
    }
}

static void APIENTRY
VirtGpuOglGetClipPlane(GLenum Arg0, GLdouble * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg0 < GL_CLIP_PLANE0) || (Arg0 > GL_CLIP_PLANE5))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    CopyMemory(Arg1, Context->ClipPlanes[Arg0 - GL_CLIP_PLANE0], 4 * sizeof(GLdouble));
}

static void APIENTRY
VirtGpuOglGetLightfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_POSITION:
            Arg2[0] = (Arg1 == GL_DIFFUSE) ? 1.0f : 0.0f;
            Arg2[1] = (Arg1 == GL_DIFFUSE) ? 1.0f : 0.0f;
            Arg2[2] = (Arg1 == GL_DIFFUSE) ? 1.0f : 0.0f;
            Arg2[3] = 1.0f;
            break;
        case GL_SPOT_DIRECTION:
            Arg2[0] = 0.0f;
            Arg2[1] = 0.0f;
            Arg2[2] = -1.0f;
            break;
        case GL_SPOT_EXPONENT:
        case GL_SPOT_CUTOFF:
        case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:
        case GL_QUADRATIC_ATTENUATION:
            Arg2[0] = (Arg1 == GL_CONSTANT_ATTENUATION) ? 1.0f : 0.0f;
            break;
        default:
            VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetLightiv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    GLfloat Values[4] = { 0.0f };
    ULONG Index;

    VirtGpuOglGetLightfv(Arg0, Arg1, Values);
    if (Arg2 != NULL)
    {
        for (Index = 0; Index < 4; ++Index)
            Arg2[Index] = (GLint)Values[Index];
    }
}

static void APIENTRY
VirtGpuOglGetMapdv(GLenum Arg0, GLenum Arg1, GLdouble * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    GLint Components;
    ULONG Count;
    ULONG I;

    if ((Context == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglEvalMapTargetComponents(Arg0, FALSE, &Index, &Components))
    {
        PVIRTGPU_OGL_EVAL_MAP1 Map = &Context->EvalMap1[Index];

        switch (Arg1)
        {
            case GL_COEFF:
                Count = Map->Defined ? (ULONG)(Map->Order * Map->Components) : 1;
                for (I = 0; I < Count; ++I)
                    Arg2[I] = (Map->Defined && (Map->Points != NULL)) ? Map->Points[I] : 0.0;
                break;
            case GL_ORDER:
                Arg2[0] = Map->Defined ? Map->Order : 1;
                break;
            case GL_DOMAIN:
                Arg2[0] = Map->Defined ? Map->U1 : 0.0;
                Arg2[1] = Map->Defined ? Map->U2 : 1.0;
                break;
            default:
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                break;
        }
        return;
    }

    if (VirtGpuOglEvalMapTargetComponents(Arg0, TRUE, &Index, &Components))
    {
        PVIRTGPU_OGL_EVAL_MAP2 Map = &Context->EvalMap2[Index];

        switch (Arg1)
        {
            case GL_COEFF:
                Count = Map->Defined ? (ULONG)(Map->UOrder * Map->VOrder * Map->Components) : 1;
                for (I = 0; I < Count; ++I)
                    Arg2[I] = (Map->Defined && (Map->Points != NULL)) ? Map->Points[I] : 0.0;
                break;
            case GL_ORDER:
                Arg2[0] = Map->Defined ? Map->UOrder : 1;
                Arg2[1] = Map->Defined ? Map->VOrder : 1;
                break;
            case GL_DOMAIN:
                Arg2[0] = Map->Defined ? Map->U1 : 0.0;
                Arg2[1] = Map->Defined ? Map->U2 : 1.0;
                Arg2[2] = Map->Defined ? Map->V1 : 0.0;
                Arg2[3] = Map->Defined ? Map->V2 : 1.0;
                break;
            default:
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                break;
        }
        return;
    }

    VirtGpuOglSetError(Context, GL_INVALID_ENUM);
}

static void APIENTRY
VirtGpuOglGetMapfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    GLdouble Values[VIRTGPU_OGL_MAX_EVAL_ORDER * VIRTGPU_OGL_MAX_EVAL_ORDER * 4];
    ULONG Index;
    ULONG Count = 1;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    ZeroMemory(Values, sizeof(Values));
    VirtGpuOglGetMapdv(Arg0, Arg1, Values);

    if (Arg1 == GL_COEFF)
    {
        ULONG MapIndex;
        GLint Components;
        PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

        if (Context == NULL)
            return;

        if (VirtGpuOglEvalMapTargetComponents(Arg0, FALSE, &MapIndex, &Components) &&
            Context->EvalMap1[MapIndex].Defined)
        {
            Count = (ULONG)(Context->EvalMap1[MapIndex].Order * Components);
        }
        else if (VirtGpuOglEvalMapTargetComponents(Arg0, TRUE, &MapIndex, &Components) &&
                 Context->EvalMap2[MapIndex].Defined)
        {
            Count = (ULONG)(Context->EvalMap2[MapIndex].UOrder *
                            Context->EvalMap2[MapIndex].VOrder *
                            Components);
        }
    }
    else if (Arg1 == GL_ORDER)
    {
        Count = ((Arg0 >= GL_MAP2_COLOR_4) && (Arg0 <= GL_MAP2_VERTEX_4)) ? 2 : 1;
    }
    else if (Arg1 == GL_DOMAIN)
    {
        Count = ((Arg0 >= GL_MAP2_COLOR_4) && (Arg0 <= GL_MAP2_VERTEX_4)) ? 4 : 2;
    }

    for (Index = 0; Index < Count; ++Index)
        Arg2[Index] = (GLfloat)Values[Index];
}

static void APIENTRY
VirtGpuOglGetMapiv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    GLdouble Values[VIRTGPU_OGL_MAX_EVAL_ORDER * VIRTGPU_OGL_MAX_EVAL_ORDER * 4];
    ULONG Index;
    ULONG Count = 1;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    ZeroMemory(Values, sizeof(Values));
    VirtGpuOglGetMapdv(Arg0, Arg1, Values);

    if (Arg1 == GL_COEFF)
    {
        ULONG MapIndex;
        GLint Components;
        PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

        if (Context == NULL)
            return;

        if (VirtGpuOglEvalMapTargetComponents(Arg0, FALSE, &MapIndex, &Components) &&
            Context->EvalMap1[MapIndex].Defined)
        {
            Count = (ULONG)(Context->EvalMap1[MapIndex].Order * Components);
        }
        else if (VirtGpuOglEvalMapTargetComponents(Arg0, TRUE, &MapIndex, &Components) &&
                 Context->EvalMap2[MapIndex].Defined)
        {
            Count = (ULONG)(Context->EvalMap2[MapIndex].UOrder *
                            Context->EvalMap2[MapIndex].VOrder *
                            Components);
        }
    }
    else if (Arg1 == GL_ORDER)
    {
        Count = ((Arg0 >= GL_MAP2_COLOR_4) && (Arg0 <= GL_MAP2_VERTEX_4)) ? 2 : 1;
    }
    else if (Arg1 == GL_DOMAIN)
    {
        Count = ((Arg0 >= GL_MAP2_COLOR_4) && (Arg0 <= GL_MAP2_VERTEX_4)) ? 4 : 2;
    }

    for (Index = 0; Index < Count; ++Index)
        Arg2[Index] = (GLint)Values[Index];
}

static void APIENTRY
VirtGpuOglGetMaterialfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_EMISSION:
        case GL_AMBIENT_AND_DIFFUSE:
            Arg2[0] = (Arg1 == GL_DIFFUSE) ? 0.8f : 0.0f;
            Arg2[1] = (Arg1 == GL_DIFFUSE) ? 0.8f : 0.0f;
            Arg2[2] = (Arg1 == GL_DIFFUSE) ? 0.8f : 0.0f;
            Arg2[3] = 1.0f;
            break;
        case GL_SHININESS:
            Arg2[0] = 0.0f;
            break;
        case GL_COLOR_INDEXES:
            Arg2[0] = 0.0f;
            Arg2[1] = 1.0f;
            Arg2[2] = 1.0f;
            break;
        default:
            VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetMaterialiv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    GLfloat Values[4] = { 0.0f };
    ULONG Index;

    VirtGpuOglGetMaterialfv(Arg0, Arg1, Values);
    if (Arg2 != NULL)
    {
        for (Index = 0; Index < 4; ++Index)
            Arg2[Index] = (GLint)Values[Index];
    }
}

static void APIENTRY
VirtGpuOglGetPixelMapfv(GLenum Arg0, GLfloat * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    GLint I;

    if ((Context == NULL) || (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglPixelMapToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Context->PixelMaps[Index].Values == NULL)
    {
        Arg1[0] = 0.0f;
        return;
    }

    for (I = 0; I < Context->PixelMaps[Index].Size; ++I)
        Arg1[I] = Context->PixelMaps[Index].Values[I];
}

static void APIENTRY
VirtGpuOglGetPixelMapuiv(GLenum Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    GLint I;

    if ((Context == NULL) || (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglPixelMapToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Context->PixelMaps[Index].Values == NULL)
    {
        Arg1[0] = 0;
        return;
    }

    for (I = 0; I < Context->PixelMaps[Index].Size; ++I)
        Arg1[I] = (GLuint)Context->PixelMaps[Index].Values[I];
}

static void APIENTRY
VirtGpuOglGetPixelMapusv(GLenum Arg0, GLushort * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    GLint I;

    if ((Context == NULL) || (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglPixelMapToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Context->PixelMaps[Index].Values == NULL)
    {
        Arg1[0] = 0;
        return;
    }

    for (I = 0; I < Context->PixelMaps[Index].Size; ++I)
        Arg1[I] = (GLushort)Context->PixelMaps[Index].Values[I];
}

static void APIENTRY
VirtGpuOglGetPolygonStipple(GLubyte * Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg0 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    CopyMemory(Arg0, Context->PolygonStipple, sizeof(Context->PolygonStipple));
}

static void APIENTRY
VirtGpuOglGetTexEnvfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg0 != GL_TEXTURE_ENV)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    switch (Arg1)
    {
        case GL_TEXTURE_ENV_MODE:
            Arg2[0] = (GLfloat)Context->TexEnvMode;
            break;
        case GL_TEXTURE_ENV_COLOR:
            CopyMemory(Arg2, Context->TexEnvColor, 4 * sizeof(GLfloat));
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetTexEnviv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    GLfloat Values[4] = { 0.0f };
    ULONG Index;

    VirtGpuOglGetTexEnvfv(Arg0, Arg1, Values);
    if (Arg2 != NULL)
    {
        for (Index = 0; Index < 4; ++Index)
            Arg2[Index] = (GLint)Values[Index];
    }
}

static void APIENTRY
VirtGpuOglGetTexGendv(GLenum Arg0, GLenum Arg1, GLdouble * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if ((Context == NULL) || (Arg2 == NULL) || !VirtGpuOglTexGenCoordIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, (Arg2 == NULL) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    switch (Arg1)
    {
        case GL_TEXTURE_GEN_MODE:
            Arg2[0] = (GLdouble)Context->TexGenMode[Index];
            break;
        case GL_OBJECT_PLANE:
            CopyMemory(Arg2, Context->TexGenObjectPlane[Index], 4 * sizeof(GLdouble));
            break;
        case GL_EYE_PLANE:
            CopyMemory(Arg2, Context->TexGenEyePlane[Index], 4 * sizeof(GLdouble));
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetTexGenfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    GLdouble Values[4] = { 0.0 };
    ULONG Index;

    VirtGpuOglGetTexGendv(Arg0, Arg1, Values);
    if (Arg2 != NULL)
    {
        for (Index = 0; Index < 4; ++Index)
            Arg2[Index] = (GLfloat)Values[Index];
    }
}

static void APIENTRY
VirtGpuOglGetTexGeniv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    GLdouble Values[4] = { 0.0 };
    ULONG Index;

    VirtGpuOglGetTexGendv(Arg0, Arg1, Values);
    if (Arg2 != NULL)
    {
        for (Index = 0; Index < 4; ++Index)
            Arg2[Index] = (GLint)Values[Index];
    }
}

static void APIENTRY
VirtGpuOglGetTexImage(GLenum Arg0, GLint Arg1, GLenum Arg2, GLenum Arg3, GLvoid * Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TEXTURE Texture;
    ULONG SourceBytes;
    ULONG DestBytes;
    ULONG DestStride;
    ULONG DestLayerStride;
    ULONG SourceLayerStride;
    ULONG Depth;
    ULONG Slice;
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

    if ((Texture->Data == NULL) ||
        (Texture->Width <= 0) ||
        (Texture->Height <= 0) ||
        (Texture->Depth <= 0))
    {
        return;
    }

    DestStride = VirtGpuOglAlignedRowSize((ULONG)Texture->Width * DestBytes,
                                          Context->PackAlignment);
    DestLayerStride = DestStride * (ULONG)Texture->Height;
    SourceLayerStride = (ULONG)Texture->Width * (ULONG)Texture->Height * SourceBytes;
    Depth = (Arg0 == GL_TEXTURE_3D) ? (ULONG)Texture->Depth : 1;
    for (Slice = 0; Slice < Depth; ++Slice)
    {
        for (Row = 0; Row < (ULONG)Texture->Height; ++Row)
        {
            const BYTE *Source = Texture->Data +
                                 (Slice * SourceLayerStride) +
                                 (Row * (ULONG)Texture->Width * SourceBytes);
            BYTE *Destination = (BYTE *)Arg4 +
                                (Slice * DestLayerStride) +
                                (Row * DestStride);

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
        case GL_TEXTURE_WRAP_R:
            *Arg2 = (GLint)Texture->WrapR;
            break;
        case GL_TEXTURE_BUFFER_DATA_STORE_BINDING:
            *Arg2 = (GLint)Texture->BufferName;
            break;
        case GL_TEXTURE_BUFFER_FORMAT:
            *Arg2 = (GLint)Texture->BufferInternalFormat;
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
        case GL_TEXTURE_DEPTH:
            *Arg3 = (Arg0 == GL_TEXTURE_3D) ? Texture->Depth : 1;
            break;
        case GL_TEXTURE_INTERNAL_FORMAT:
            *Arg3 = (GLint)Texture->InternalFormat;
            break;
        case GL_TEXTURE_BUFFER_DATA_STORE_BINDING:
            *Arg3 = (GLint)Texture->BufferName;
            break;
        case GL_TEXTURE_BUFFER_FORMAT:
            *Arg3 = (GLint)Texture->BufferInternalFormat;
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

    if ((Arg0 != GL_TEXTURE_1D) &&
        (Arg0 != GL_TEXTURE_2D) &&
        (Arg0 != GL_TEXTURE_3D))
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

    switch (Arg0)
    {
        case GL_TEXTURE_1D:
            Context->BoundTexture1D = Arg1;
            break;
        case GL_TEXTURE_2D:
            Context->BoundTexture2D = Arg1;
            break;
        case GL_TEXTURE_3D:
            Context->BoundTexture3D = Arg1;
            break;
        case GL_TEXTURE_BUFFER:
            Context->BoundTextureBuffer = Arg1;
            break;
    }
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
    PVIRTGPU_OGL_BUFFER ElementBuffer;
    const GLvoid *Elements = Arg3;
    GLsizei Index;
    GLint ElementIndex;
    ULONG ElementSize;
    ULONGLONG IndexBytes;
    SIZE_T Offset;

    if (Context == NULL)
        return;

    if (Arg1 < 0)
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

    if (!VirtGpuOglElementTypeSize(Arg2, &ElementSize))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Arg1 == 0)
        return;

    if (Context->BoundElementArrayBuffer != 0)
    {
        ElementBuffer = VirtGpuOglFindBuffer(Context, Context->BoundElementArrayBuffer);
        if (ElementBuffer == NULL)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }

        Offset = (SIZE_T)Arg3;
        IndexBytes = (ULONGLONG)(ULONG)Arg1 * ElementSize;
        if (!VirtGpuOglBufferRangeValid((GLintptr)Offset,
                                        (GLsizeiptr)IndexBytes,
                                        ElementBuffer->Size))
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }

        Elements = ElementBuffer->Data + Offset;
    }
    else if (Arg3 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
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
                    ElementIndex = ((const GLubyte *)Elements)[Index];
                    break;
                case GL_UNSIGNED_SHORT:
                    ElementIndex = ((const GLushort *)Elements)[Index];
                    break;
                case GL_UNSIGNED_INT:
                    ElementIndex = (GLint)((const GLuint *)Elements)[Index];
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
                ElementIndex = ((const GLubyte *)Elements)[Index];
                break;
            case GL_UNSIGNED_SHORT:
                ElementIndex = ((const GLushort *)Elements)[Index];
                break;
            case GL_UNSIGNED_INT:
                ElementIndex = (GLint)((const GLuint *)Elements)[Index];
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Arg0 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->EdgeFlagArrayStride = Arg0;
    Context->EdgeFlagArrayPointer = Arg1;
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_SHORT:
        case GL_INT:
        case GL_FLOAT:
        case GL_DOUBLE:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (Arg1 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->IndexArrayType = Arg0;
    Context->IndexArrayStride = Arg1;
    Context->IndexArrayPointer = Arg2;
}

static void APIENTRY
VirtGpuOglIndexub(GLubyte Arg0)
{
    VirtGpuOglIndexf((GLfloat)Arg0);
}

static void APIENTRY
VirtGpuOglIndexubv(const GLubyte * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglIndexub(Arg0[0]);
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    Context->PolygonOffsetFactor = Arg0;
    Context->PolygonOffsetUnits = Arg1;
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

static BYTE *
VirtGpuOglReadRgbaPixels(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLint X,
    _In_ GLint Y,
    _In_ GLsizei Width,
    _In_ GLsizei Height,
    _Out_ PULONG Size)
{
    BYTE *Pixels;
    ULONGLONG BufferSize64;
    ULONG BufferSize;
    GLint OldPackAlignment;
    GLint OldPackRowLength;
    GLint OldPackSkipRows;
    GLint OldPackSkipPixels;

    *Size = 0;
    if ((Width < 0) || (Height < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return NULL;
    }

    BufferSize64 = (ULONGLONG)(ULONG)Width * (ULONGLONG)(ULONG)Height * 4ULL;
    if (BufferSize64 > VIRTGPU_OGL_MAX_TRANSFER_SIZE)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return NULL;
    }

    BufferSize = (ULONG)BufferSize64;
    Pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BufferSize);
    if (Pixels == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return NULL;
    }

    OldPackAlignment = Context->PackAlignment;
    OldPackRowLength = Context->PackRowLength;
    OldPackSkipRows = Context->PackSkipRows;
    OldPackSkipPixels = Context->PackSkipPixels;
    Context->PackAlignment = 1;
    Context->PackRowLength = 0;
    Context->PackSkipRows = 0;
    Context->PackSkipPixels = 0;

    VirtGpuOglReadPixels(X, Y, Width, Height, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);

    Context->PackAlignment = OldPackAlignment;
    Context->PackRowLength = OldPackRowLength;
    Context->PackSkipRows = OldPackSkipRows;
    Context->PackSkipPixels = OldPackSkipPixels;

    *Size = BufferSize;
    return Pixels;
}

static VOID
VirtGpuOglSetTemporaryUnpack(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _Out_writes_(4) GLint *OldState)
{
    OldState[0] = Context->UnpackAlignment;
    OldState[1] = Context->UnpackRowLength;
    OldState[2] = Context->UnpackSkipRows;
    OldState[3] = Context->UnpackSkipPixels;
    Context->UnpackAlignment = 1;
    Context->UnpackRowLength = 0;
    Context->UnpackSkipRows = 0;
    Context->UnpackSkipPixels = 0;
}

static VOID
VirtGpuOglRestoreTemporaryUnpack(_Inout_ PVIRTGPU_OGL_CONTEXT Context, _In_reads_(4) const GLint *OldState)
{
    Context->UnpackAlignment = OldState[0];
    Context->UnpackRowLength = OldState[1];
    Context->UnpackSkipRows = OldState[2];
    Context->UnpackSkipPixels = OldState[3];
}

static void APIENTRY
VirtGpuOglCopyTexImage1D(GLenum Arg0, GLint Arg1, GLenum Arg2, GLint Arg3, GLint Arg4, GLsizei Arg5, GLint Arg6)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONG Size;
    GLint OldUnpack[4];

    if (Context == NULL)
        return;

    Pixels = VirtGpuOglReadRgbaPixels(Context, Arg3, Arg4, Arg5, 1, &Size);
    UNREFERENCED_PARAMETER(Size);
    if (Pixels == NULL)
        return;

    VirtGpuOglSetTemporaryUnpack(Context, OldUnpack);
    VirtGpuOglTexImage1D(Arg0, Arg1, Arg2, Arg5, Arg6, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    VirtGpuOglRestoreTemporaryUnpack(Context, OldUnpack);
    HeapFree(GetProcessHeap(), 0, Pixels);
}

static void APIENTRY
VirtGpuOglCopyTexImage2D(GLenum Arg0, GLint Arg1, GLenum Arg2, GLint Arg3, GLint Arg4, GLsizei Arg5, GLsizei Arg6, GLint Arg7)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONG Size;
    GLint OldUnpack[4];

    if (Context == NULL)
        return;

    Pixels = VirtGpuOglReadRgbaPixels(Context, Arg3, Arg4, Arg5, Arg6, &Size);
    UNREFERENCED_PARAMETER(Size);
    if (Pixels == NULL)
        return;

    VirtGpuOglSetTemporaryUnpack(Context, OldUnpack);
    VirtGpuOglTexImage2D(Arg0, Arg1, Arg2, Arg5, Arg6, Arg7, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    VirtGpuOglRestoreTemporaryUnpack(Context, OldUnpack);
    HeapFree(GetProcessHeap(), 0, Pixels);
}

static void APIENTRY
VirtGpuOglCopyTexSubImage1D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLsizei Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONG Size;
    GLint OldUnpack[4];

    if (Context == NULL)
        return;

    Pixels = VirtGpuOglReadRgbaPixels(Context, Arg3, Arg4, Arg5, 1, &Size);
    UNREFERENCED_PARAMETER(Size);
    if (Pixels == NULL)
        return;

    VirtGpuOglSetTemporaryUnpack(Context, OldUnpack);
    VirtGpuOglTexSubImage1D(Arg0, Arg1, Arg2, Arg5, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    VirtGpuOglRestoreTemporaryUnpack(Context, OldUnpack);
    HeapFree(GetProcessHeap(), 0, Pixels);
}

static void APIENTRY
VirtGpuOglCopyTexSubImage2D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLint Arg5, GLsizei Arg6, GLsizei Arg7)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONG Size;
    GLint OldUnpack[4];

    if (Context == NULL)
        return;

    Pixels = VirtGpuOglReadRgbaPixels(Context, Arg4, Arg5, Arg6, Arg7, &Size);
    UNREFERENCED_PARAMETER(Size);
    if (Pixels == NULL)
        return;

    VirtGpuOglSetTemporaryUnpack(Context, OldUnpack);
    VirtGpuOglTexSubImage2D(Arg0, Arg1, Arg2, Arg3, Arg6, Arg7, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    VirtGpuOglRestoreTemporaryUnpack(Context, OldUnpack);
    HeapFree(GetProcessHeap(), 0, Pixels);
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
        if (Context->BoundTexture3D == Arg1[Index])
            Context->BoundTexture3D = 0;
        if (Context->BoundTextureBuffer == Arg1[Index])
            Context->BoundTextureBuffer = 0;
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
        (Arg2 > Texture->Width) ||
        (Arg3 > Texture->Width - Arg2) ||
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
        (Arg2 > Texture->Width) ||
        (Arg4 > Texture->Width - Arg2) ||
        (Arg3 > Texture->Height) ||
        (Arg5 > Texture->Height - Arg3) ||
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
    /* Client attrib stack restore is not needed by the current fallback path. */
}

static void APIENTRY
VirtGpuOglPushClientAttrib(GLbitfield Arg0)
{
    UNREFERENCED_PARAMETER(Arg0);
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TEXTURE Texture;

    if (Context == NULL)
        return;

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture != NULL)
    {
        (VOID)VirtGpuOglStoreTextureImage3D(Context,
                                            Texture,
                                            Arg0,
                                            Arg1,
                                            Arg2,
                                            Arg3,
                                            Arg4,
                                            Arg5,
                                            Arg6,
                                            Arg7,
                                            Arg8,
                                            Arg9);
    }
}

static void APIENTRY
VirtGpuOglTexSubImage3D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLsizei Arg5, GLsizei Arg6, GLsizei Arg7, GLenum Arg8, GLenum Arg9, const void * Arg10)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TEXTURE Texture;
    ULONG BytesPerPixel;
    ULONG SourceStride;
    ULONG SourceLayerStride;
    ULONG RowSize;
    GLsizei Slice;
    GLsizei Row;

    if (Context == NULL)
        return;

    if ((Arg2 < 0) || (Arg3 < 0) || (Arg4 < 0) ||
        (Arg5 < 0) || (Arg6 < 0) || (Arg7 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Texture = VirtGpuOglBoundTexture(Context, Arg0);
    if (Texture == NULL)
        return;

    if ((Arg1 != 0) ||
        (Arg2 > Texture->Width) ||
        (Arg5 > Texture->Width - Arg2) ||
        (Arg3 > Texture->Height) ||
        (Arg6 > Texture->Height - Arg3) ||
        (Arg4 > Texture->Depth) ||
        (Arg7 > Texture->Depth - Arg4) ||
        (Texture->Data == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglTextureFormatBytes(Arg8, Arg9, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Arg8 != Texture->Format) || (Arg9 != Texture->Type))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if ((Arg5 == 0) || (Arg6 == 0) || (Arg7 == 0) || (Arg10 == NULL))
        return;

    RowSize = (ULONG)Arg5 * BytesPerPixel;
    SourceStride = VirtGpuOglAlignedRowSize(RowSize, Context->UnpackAlignment);
    SourceLayerStride = SourceStride * (ULONG)Arg6;
    for (Slice = 0; Slice < Arg7; ++Slice)
    {
        for (Row = 0; Row < Arg6; ++Row)
        {
            CopyMemory(Texture->Data +
                       (((((ULONG)(Arg4 + Slice) * (ULONG)Texture->Height) +
                          (ULONG)(Arg3 + Row)) * (ULONG)Texture->Width +
                         (ULONG)Arg2) * BytesPerPixel),
                       (const BYTE *)Arg10 +
                       ((ULONG)Slice * SourceLayerStride) +
                       ((ULONG)Row * SourceStride),
                       RowSize);
        }
    }
}

static void APIENTRY
VirtGpuOglCopyTexSubImage3D(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLint Arg5, GLint Arg6, GLsizei Arg7, GLsizei Arg8)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONG Size;
    GLint OldUnpack[4];

    if (Context == NULL)
        return;

    Pixels = VirtGpuOglReadRgbaPixels(Context, Arg5, Arg6, Arg7, Arg8, &Size);
    UNREFERENCED_PARAMETER(Size);
    if (Pixels == NULL)
        return;

    VirtGpuOglSetTemporaryUnpack(Context, OldUnpack);
    VirtGpuOglTexSubImage3D(Arg0, Arg1, Arg2, Arg3, Arg4, Arg7, Arg8, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    VirtGpuOglRestoreTemporaryUnpack(Context, OldUnpack);
    HeapFree(GetProcessHeap(), 0, Pixels);
}

static void APIENTRY
VirtGpuOglActiveTexture(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Arg0 < GL_TEXTURE0) || (Arg0 > GL_TEXTURE31))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Context->ActiveTexture = Arg0;
}

static void APIENTRY
VirtGpuOglSampleCoverage(GLfloat Arg0, GLboolean Arg1)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
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
    VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
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
    VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
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
    VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
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
    VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
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
    VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
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
    VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
}

static void APIENTRY
VirtGpuOglGetCompressedTexImage(GLenum Arg0, GLint Arg1, void * Arg2)
{
    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
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
    GLsizei Index;

    if (Arg3 < 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    if ((Arg3 > 0) && ((Arg1 == NULL) || (Arg2 == NULL)))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg3; ++Index)
        VirtGpuOglDrawArrays(Arg0, Arg1[Index], Arg2[Index]);
}

static void APIENTRY
VirtGpuOglMultiDrawElements(GLenum Arg0, const GLsizei * Arg1, GLenum Arg2, const void *const * Arg3, GLsizei Arg4)
{
    GLsizei Index;

    if (Arg4 < 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    if ((Arg4 > 0) && ((Arg1 == NULL) || (Arg3 == NULL)))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg4; ++Index)
        VirtGpuOglDrawElements(Arg0, Arg1[Index], Arg2, Arg3[Index]);
}

static void APIENTRY
VirtGpuOglPointParameterf(GLenum Arg0, GLfloat Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_POINT_SIZE_MIN:
            Context->PointSizeMin = Arg1;
            break;
        case GL_POINT_SIZE_MAX:
            Context->PointSizeMax = Arg1;
            break;
        case GL_POINT_FADE_THRESHOLD_SIZE:
            Context->PointFadeThresholdSize = Arg1;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglPointParameterfv(GLenum Arg0, const GLfloat * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg0 == GL_POINT_DISTANCE_ATTENUATION)
    {
        if (Context != NULL)
            CopyMemory(Context->PointDistanceAttenuation, Arg1, 3 * sizeof(GLfloat));
        return;
    }

    VirtGpuOglPointParameterf(Arg0, Arg1[0]);
}

static void APIENTRY
VirtGpuOglPointParameteri(GLenum Arg0, GLint Arg1)
{
    VirtGpuOglPointParameterf(Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglPointParameteriv(GLenum Arg0, const GLint * Arg1)
{
    GLfloat Values[3];

    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    Values[0] = (GLfloat)Arg1[0];
    Values[1] = (GLfloat)Arg1[1];
    Values[2] = (GLfloat)Arg1[2];
    VirtGpuOglPointParameterfv(Arg0, Values);
}

static void APIENTRY
VirtGpuOglBlendColor(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    Context->BlendColor[0] = VirtGpuOglClampFloat(Arg0);
    Context->BlendColor[1] = VirtGpuOglClampFloat(Arg1);
    Context->BlendColor[2] = VirtGpuOglClampFloat(Arg2);
    Context->BlendColor[3] = VirtGpuOglClampFloat(Arg3);
}

static void APIENTRY
VirtGpuOglBlendEquation(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FUNC_ADD:
        case GL_FUNC_SUBTRACT:
        case GL_FUNC_REVERSE_SUBTRACT:
            Context->BlendEquationMode = Arg0;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGenQueries(GLsizei Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    GLuint Name;

    if (Context == NULL)
        return;

    if ((Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, (Arg0 < 0) ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        do
        {
            Name = Context->NextQueryName++;
            if (Context->NextQueryName == 0)
                Context->NextQueryName = 1;
        } while ((Name == 0) || (VirtGpuOglFindQuery(Context, Name) != NULL));

        if (VirtGpuOglAllocateQueryName(Context, Name) == NULL)
            return;
        Arg1[Index] = Name;
    }
}

static void APIENTRY
VirtGpuOglDeleteQueries(GLsizei Arg0, const GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    PVIRTGPU_OGL_QUERY Query;

    if (Context == NULL)
        return;

    if ((Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        Query = VirtGpuOglFindQuery(Context, Arg1[Index]);
        if (Query != NULL)
        {
            if (Context->CurrentQuery == Query->Name)
                Context->CurrentQuery = 0;
            ZeroMemory(Query, sizeof(*Query));
        }
    }
}

static GLboolean APIENTRY
VirtGpuOglIsQuery(GLuint Arg0)
{
    return (VirtGpuOglFindQuery(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglBeginQuery(GLenum Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_QUERY Query;

    if (Context == NULL)
        return;

    if (!VirtGpuOglQueryTargetValid(Arg0) || (Arg1 == 0) || (Context->CurrentQuery != 0))
    {
        VirtGpuOglSetError(Context, !VirtGpuOglQueryTargetValid(Arg0) ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
        return;
    }

    Query = VirtGpuOglFindQuery(Context, Arg1);
    if (Query == NULL)
        Query = VirtGpuOglAllocateQueryName(Context, Arg1);
    if (Query == NULL)
        return;

    Query->Target = Arg0;
    Query->Active = TRUE;
    Query->Result = 0;
    Context->CurrentQuery = Arg1;
}

static void APIENTRY
VirtGpuOglEndQuery(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_QUERY Query;

    if (Context == NULL)
        return;

    if (!VirtGpuOglQueryTargetValid(Arg0) && (Arg0 != GL_TIMESTAMP))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Query = VirtGpuOglFindQuery(Context, Context->CurrentQuery);
    if ((Query == NULL) || !Query->Active)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Query->Active = FALSE;
    Query->Result = 0;
    Context->CurrentQuery = 0;
}

static void APIENTRY
VirtGpuOglGetQueryiv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglQueryTargetValid(Arg0) && (Arg0 != GL_TIMESTAMP))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    switch (Arg1)
    {
        case GL_QUERY_COUNTER_BITS:
            *Arg2 = 0;
            break;
        case GL_CURRENT_QUERY:
            *Arg2 = (GLint)Context->CurrentQuery;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetQueryObjectiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    GLuint Value = 0;

    VirtGpuOglGetQueryObjectuiv(Arg0, Arg1, &Value);
    if (Arg2 != NULL)
        *Arg2 = (GLint)Value;
}

static void APIENTRY
VirtGpuOglGetQueryObjectuiv(GLuint Arg0, GLenum Arg1, GLuint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_QUERY Query;

    Query = VirtGpuOglFindQuery(Context, Arg0);
    if ((Query == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_QUERY_RESULT:
            *Arg2 = Query->Result;
            break;
        case GL_QUERY_RESULT_AVAILABLE:
            *Arg2 = Query->Active ? GL_FALSE : GL_TRUE;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglBindBuffer(GLenum Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_ARRAY_BUFFER:
            Context->BoundArrayBuffer = Arg1;
            break;
        case GL_ELEMENT_ARRAY_BUFFER:
            Context->BoundElementArrayBuffer = Arg1;
            break;
        case GL_COPY_READ_BUFFER:
            Context->BoundCopyReadBuffer = Arg1;
            break;
        case GL_COPY_WRITE_BUFFER:
            Context->BoundCopyWriteBuffer = Arg1;
            break;
        case GL_UNIFORM_BUFFER:
            Context->BoundUniformBuffer = Arg1;
            break;
        case GL_TRANSFORM_FEEDBACK_BUFFER:
            Context->BoundTransformFeedbackBuffer = Arg1;
            break;
        case GL_DRAW_INDIRECT_BUFFER:
            Context->BoundDrawIndirectBuffer = Arg1;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (Arg1 != 0)
    {
        Buffer = VirtGpuOglFindBuffer(Context, Arg1);
        if (Buffer == NULL)
            Buffer = VirtGpuOglAllocateBufferName(Context, Arg1);
        if (Buffer != NULL)
            Buffer->Target = Arg0;
    }
}

static void APIENTRY
VirtGpuOglDeleteBuffers(GLsizei Arg0, const GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    ULONG BindingIndex;
    PVIRTGPU_OGL_BUFFER Buffer;

    if (Context == NULL)
        return;

    if ((Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        Buffer = VirtGpuOglFindBuffer(Context, Arg1[Index]);
        if (Buffer != NULL)
        {
            if (Context->BoundArrayBuffer == Buffer->Name)
                Context->BoundArrayBuffer = 0;
            if (Context->BoundElementArrayBuffer == Buffer->Name)
                Context->BoundElementArrayBuffer = 0;
            if (Context->BoundCopyReadBuffer == Buffer->Name)
                Context->BoundCopyReadBuffer = 0;
            if (Context->BoundCopyWriteBuffer == Buffer->Name)
                Context->BoundCopyWriteBuffer = 0;
            if (Context->BoundUniformBuffer == Buffer->Name)
                Context->BoundUniformBuffer = 0;
            if (Context->BoundTransformFeedbackBuffer == Buffer->Name)
                Context->BoundTransformFeedbackBuffer = 0;
            if (Context->BoundDrawIndirectBuffer == Buffer->Name)
                Context->BoundDrawIndirectBuffer = 0;
            for (BindingIndex = 0; BindingIndex < VIRTGPU_OGL_MAX_BUFFER_BINDINGS; ++BindingIndex)
            {
                if (Context->UniformBufferBindings[BindingIndex].Buffer == Buffer->Name)
                    ZeroMemory(&Context->UniformBufferBindings[BindingIndex],
                               sizeof(Context->UniformBufferBindings[BindingIndex]));
                if (Context->TransformFeedbackBufferBindings[BindingIndex].Buffer == Buffer->Name)
                    ZeroMemory(&Context->TransformFeedbackBufferBindings[BindingIndex],
                               sizeof(Context->TransformFeedbackBufferBindings[BindingIndex]));
            }
            VirtGpuOglFreeBuffer(Buffer);
        }
    }
}

static void APIENTRY
VirtGpuOglGenBuffers(GLsizei Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;
    GLsizei Index;
    GLuint Name;
    ULONG Attempts;
    BOOL Found;

    if (Context == NULL)
        return;

    if ((Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        Found = FALSE;
        for (Attempts = 0; Attempts <= VIRTGPU_OGL_MAX_BUFFERS; ++Attempts)
        {
            Name = Context->NextBufferName++;
            if (Context->NextBufferName == 0)
                Context->NextBufferName = 1;
            if ((Name != 0) && (VirtGpuOglFindBuffer(Context, Name) == NULL))
            {
                Found = TRUE;
                break;
            }
        }

        if (!Found || (VirtGpuOglAllocateBufferName(Context, Name) == NULL))
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            while (Index > 0)
            {
                --Index;
                Buffer = VirtGpuOglFindBuffer(Context, Arg1[Index]);
                if (Buffer != NULL)
                    VirtGpuOglFreeBuffer(Buffer);
                Arg1[Index] = 0;
            }
            return;
        }
        Arg1[Index] = Name;
    }
}

static GLboolean APIENTRY
VirtGpuOglIsBuffer(GLuint Arg0)
{
    return (VirtGpuOglFindBuffer(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglBufferData(GLenum Arg0, GLsizeiptr Arg1, const void * Arg2, GLenum Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;
    BYTE *Data = NULL;

    if ((Context == NULL) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg3)
    {
        case GL_STREAM_DRAW:
        case GL_STATIC_DRAW:
        case GL_DYNAMIC_DRAW:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    Buffer = VirtGpuOglBoundBuffer(Context, Arg0);
    if (Buffer == NULL)
        return;

    if (Buffer->Mapped)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (Arg1 > 0)
    {
        Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)Arg1);
        if (Data == NULL)
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            return;
        }
        if (Arg2 != NULL)
            CopyMemory(Data, Arg2, (SIZE_T)Arg1);
    }

    if (Buffer->Data != NULL)
        HeapFree(GetProcessHeap(), 0, Buffer->Data);
    Buffer->Data = Data;
    Buffer->Size = Arg1;
    Buffer->Usage = Arg3;
}

static void APIENTRY
VirtGpuOglBufferSubData(GLenum Arg0, GLintptr Arg1, GLsizeiptr Arg2, const void * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;

    Buffer = VirtGpuOglBoundBuffer(Context, Arg0);
    if (Buffer == NULL)
        return;

    if (!VirtGpuOglBufferRangeValid(Arg1, Arg2, Buffer->Size) ||
        ((Arg2 > 0) && (Arg3 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg2 != 0)
        CopyMemory(Buffer->Data + (SIZE_T)Arg1, Arg3, (SIZE_T)Arg2);
}

static void APIENTRY
VirtGpuOglGetBufferSubData(GLenum Arg0, GLintptr Arg1, GLsizeiptr Arg2, void * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;

    Buffer = VirtGpuOglBoundBuffer(Context, Arg0);
    if (Buffer == NULL)
        return;

    if (!VirtGpuOglBufferRangeValid(Arg1, Arg2, Buffer->Size) ||
        ((Arg2 > 0) && (Arg3 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg2 != 0)
        CopyMemory(Arg3, Buffer->Data + (SIZE_T)Arg1, (SIZE_T)Arg2);
}

static void * APIENTRY
VirtGpuOglMapBuffer(GLenum Arg0, GLenum Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;

    if ((Arg1 != GL_READ_ONLY) && (Arg1 != GL_WRITE_ONLY) && (Arg1 != GL_READ_WRITE))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return NULL;
    }

    Buffer = VirtGpuOglBoundBuffer(Context, Arg0);
    if (Buffer == NULL)
        return NULL;

    if (Buffer->Mapped)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return NULL;
    }

    Buffer->Mapped = TRUE;
    Buffer->Access = Arg1;
    return Buffer->Data;
}

static GLboolean APIENTRY
VirtGpuOglUnmapBuffer(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;

    Buffer = VirtGpuOglBoundBuffer(Context, Arg0);
    if (Buffer == NULL)
        return GL_FALSE;

    if (!Buffer->Mapped)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return GL_FALSE;
    }

    Buffer->Mapped = FALSE;
    return GL_TRUE;
}

static void APIENTRY
VirtGpuOglGetBufferParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;

    Buffer = VirtGpuOglBoundBuffer(Context, Arg0);
    if ((Buffer == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_BUFFER_SIZE:
            *Arg2 = (GLint)Buffer->Size;
            break;
        case GL_BUFFER_USAGE:
            *Arg2 = (GLint)Buffer->Usage;
            break;
        case GL_BUFFER_ACCESS:
            *Arg2 = (GLint)Buffer->Access;
            break;
        case GL_BUFFER_MAPPED:
            *Arg2 = Buffer->Mapped ? GL_TRUE : GL_FALSE;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetBufferPointerv(GLenum Arg0, GLenum Arg1, void * * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;

    Buffer = VirtGpuOglBoundBuffer(Context, Arg0);
    if ((Buffer == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg1 != GL_BUFFER_MAP_POINTER)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    *Arg2 = Buffer->Mapped ? Buffer->Data : NULL;
}

static void APIENTRY
VirtGpuOglBlendEquationSeparate(GLenum Arg0, GLenum Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);
    VirtGpuOglBlendEquation(Arg0);
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

static PVIRTGPU_OGL_UNIFORM
VirtGpuOglCurrentUniform(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_ GLint Location)
{
    PVIRTGPU_OGL_PROGRAM Program;

    if (Location == -1)
        return NULL;

    Program = VirtGpuOglFindProgram(Context, Context->CurrentProgram);
    if ((Program == NULL) || !Program->Linked)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return NULL;
    }

    return VirtGpuOglFindUniformByLocation(Program, Location);
}

static VOID
VirtGpuOglStoreUniformFloat(
    _In_ GLint Location,
    _In_ GLenum Type,
    _In_ GLsizei Count,
    _In_ ULONG Components,
    _In_reads_(Count * Components) const GLfloat *Values)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_UNIFORM Uniform;
    ULONG ValueCount;
    ULONG Index;

    if (Location == -1)
        return;

    if ((Context == NULL) || (Count < 0) || ((Count > 0) && (Values == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }
    if (Count == 0)
        return;

    Uniform = VirtGpuOglCurrentUniform(Context, Location);
    if (Uniform == NULL)
    {
        if (Context->LastError == GL_NO_ERROR)
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    ValueCount = (ULONG)Count * Components;
    if (ValueCount > VIRTGPU_OGL_MAX_UNIFORM_VALUES)
        ValueCount = VIRTGPU_OGL_MAX_UNIFORM_VALUES;

    Uniform->Type = Type;
    Uniform->Size = Count;
    for (Index = 0; Index < ValueCount; ++Index)
    {
        Uniform->FloatValues[Index] = Values[Index];
        Uniform->IntValues[Index] = (GLint)Values[Index];
    }
}

static VOID
VirtGpuOglStoreUniformInt(
    _In_ GLint Location,
    _In_ GLenum Type,
    _In_ GLsizei Count,
    _In_ ULONG Components,
    _In_reads_(Count * Components) const GLint *Values)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_UNIFORM Uniform;
    ULONG ValueCount;
    ULONG Index;

    if (Location == -1)
        return;

    if ((Context == NULL) || (Count < 0) || ((Count > 0) && (Values == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }
    if (Count == 0)
        return;

    Uniform = VirtGpuOglCurrentUniform(Context, Location);
    if (Uniform == NULL)
    {
        if (Context->LastError == GL_NO_ERROR)
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    ValueCount = (ULONG)Count * Components;
    if (ValueCount > VIRTGPU_OGL_MAX_UNIFORM_VALUES)
        ValueCount = VIRTGPU_OGL_MAX_UNIFORM_VALUES;

    Uniform->Type = Type;
    Uniform->Size = Count;
    for (Index = 0; Index < ValueCount; ++Index)
    {
        Uniform->IntValues[Index] = Values[Index];
        Uniform->FloatValues[Index] = (GLfloat)Values[Index];
    }
}

static VOID
VirtGpuOglStoreUniformUInt(
    _In_ GLint Location,
    _In_ GLenum Type,
    _In_ GLsizei Count,
    _In_ ULONG Components,
    _In_reads_(Count * Components) const GLuint *Values)
{
    GLint Converted[VIRTGPU_OGL_MAX_UNIFORM_VALUES];
    ULONG ValueCount;
    ULONG Index;

    if (Location == -1)
        return;

    if ((Count < 0) || ((Count > 0) && (Values == NULL)))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    ValueCount = (ULONG)Count * Components;
    if (ValueCount > VIRTGPU_OGL_MAX_UNIFORM_VALUES)
        ValueCount = VIRTGPU_OGL_MAX_UNIFORM_VALUES;

    for (Index = 0; Index < ValueCount; ++Index)
        Converted[Index] = (GLint)Values[Index];

    VirtGpuOglStoreUniformInt(Location, Type, Count, Components, Converted);
}

static VOID
VirtGpuOglStoreUniformDouble(
    _In_ GLint Location,
    _In_ GLenum Type,
    _In_ GLsizei Count,
    _In_ ULONG Components,
    _In_reads_(Count * Components) const GLdouble *Values)
{
    GLfloat Converted[VIRTGPU_OGL_MAX_UNIFORM_VALUES];
    ULONG ValueCount;
    ULONG Index;

    if (Location == -1)
        return;

    if ((Count < 0) || ((Count > 0) && (Values == NULL)))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    ValueCount = (ULONG)Count * Components;
    if (ValueCount > VIRTGPU_OGL_MAX_UNIFORM_VALUES)
        ValueCount = VIRTGPU_OGL_MAX_UNIFORM_VALUES;

    for (Index = 0; Index < ValueCount; ++Index)
        Converted[Index] = (GLfloat)Values[Index];

    VirtGpuOglStoreUniformFloat(Location, Type, Count, Components, Converted);
}

static VOID
VirtGpuOglSetVertexAttribCurrent(
    _In_ GLuint Index,
    _In_ GLfloat X,
    _In_ GLfloat Y,
    _In_ GLfloat Z,
    _In_ GLfloat W)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Index >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->VertexAttribs[Index].Current[0] = X;
    Context->VertexAttribs[Index].Current[1] = Y;
    Context->VertexAttribs[Index].Current[2] = Z;
    Context->VertexAttribs[Index].Current[3] = W;
}

static BOOL
VirtGpuOglValidVertexAttribType(_In_ GLenum Type)
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
VirtGpuOglValidVertexAttribIntegerType(_In_ GLenum Type)
{
    switch (Type)
    {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_INT:
        case GL_UNSIGNED_INT:
            return TRUE;
        default:
            return FALSE;
    }
}

static GLfloat
VirtGpuOglNormalizeByte(_In_ GLbyte Value)
{
    return (Value == -128) ? -1.0f : ((GLfloat)Value / 127.0f);
}

static GLfloat
VirtGpuOglNormalizeShort(_In_ GLshort Value)
{
    return (Value == -32768) ? -1.0f : ((GLfloat)Value / 32767.0f);
}

static GLfloat
VirtGpuOglNormalizeInt(_In_ GLint Value)
{
    return (Value == (-2147483647 - 1)) ? -1.0f : ((GLfloat)Value / 2147483647.0f);
}

static void APIENTRY
VirtGpuOglAttachShader(GLuint Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_SHADER Shader;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    Shader = VirtGpuOglFindShader(Context, Arg1);
    if ((Program == NULL) || (Shader == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglProgramHasShader(Program, Arg1))
        return;

    if (Program->AttachedShaderCount >= VIRTGPU_OGL_MAX_ATTACHED_SHADERS)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Program->AttachedShaders[Program->AttachedShaderCount++] = Arg1;
    Program->Linked = FALSE;
    Program->Validated = FALSE;
}

static void APIENTRY
VirtGpuOglBindAttribLocation(GLuint Arg0, GLuint Arg1, const GLchar * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_PROGRAM_BINDING Binding;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg1 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS) || VirtGpuOglReservedName(Arg2))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Binding = VirtGpuOglFindAttribBinding(Program, Arg2);
    if (Binding == NULL)
        Binding = VirtGpuOglAllocateAttribBinding(Context, Program);
    if (Binding == NULL)
        return;

    Binding->Index = Arg1;
    VirtGpuOglCopyFixedName(Binding->Name, VIRTGPU_OGL_MAX_NAME_LENGTH, Arg2);
    Program->Linked = FALSE;
    Program->Validated = FALSE;
}

static void APIENTRY
VirtGpuOglCompileShader(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SHADER Shader;

    Shader = VirtGpuOglFindShader(Context, Arg0);
    if (Shader == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Shader->Compiled = TRUE;
}

static GLuint APIENTRY
VirtGpuOglCreateProgram(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;

    if (Context == NULL)
        return 0;

    Program = VirtGpuOglAllocateProgram(Context);
    return (Program != NULL) ? Program->Name : 0;
}

static GLuint APIENTRY
VirtGpuOglCreateShader(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SHADER Shader;

    if (Context == NULL)
        return 0;

    Shader = VirtGpuOglAllocateShader(Context, Arg0);
    return (Shader != NULL) ? Shader->Name : 0;
}

static void APIENTRY
VirtGpuOglDeleteProgram(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;

    if (Arg0 == 0)
        return;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if (Program == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Program->DeletePending = TRUE;
    if (Context->CurrentProgram != Arg0)
        VirtGpuOglFreeProgram(Context, Program);
}

static void APIENTRY
VirtGpuOglDeleteShader(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SHADER Shader;

    if (Arg0 == 0)
        return;

    Shader = VirtGpuOglFindShader(Context, Arg0);
    if (Shader == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Shader->DeletePending = TRUE;
    if (!VirtGpuOglShaderAttachedToAnotherProgram(Context, Arg0, NULL))
        VirtGpuOglFreeShader(Shader);
}

static void APIENTRY
VirtGpuOglDetachShader(GLuint Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_SHADER Shader;
    ULONG Index;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    Shader = VirtGpuOglFindShader(Context, Arg1);
    if ((Program == NULL) || (Shader == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Program->AttachedShaderCount; ++Index)
    {
        if (Program->AttachedShaders[Index] == Arg1)
        {
            MoveMemory(&Program->AttachedShaders[Index],
                       &Program->AttachedShaders[Index + 1],
                       (Program->AttachedShaderCount - Index - 1) * sizeof(Program->AttachedShaders[0]));
            --Program->AttachedShaderCount;
            Program->Linked = FALSE;
            Program->Validated = FALSE;

            if (Shader->DeletePending &&
                !VirtGpuOglShaderAttachedToAnotherProgram(Context, Arg1, Program))
            {
                VirtGpuOglFreeShader(Shader);
            }
            return;
        }
    }

    VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
}

static void APIENTRY
VirtGpuOglDisableVertexAttribArray(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg0 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->VertexAttribs[Arg0].Enabled = GL_FALSE;
}

static void APIENTRY
VirtGpuOglEnableVertexAttribArray(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg0 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->VertexAttribs[Arg0].Enabled = GL_TRUE;
}

static void APIENTRY
VirtGpuOglGetActiveAttrib(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLint * Arg4, GLenum * Arg5, GLchar * Arg6)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    ULONG Index;
    ULONG ActiveIndex = 0;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg2 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAM_BINDINGS; ++Index)
    {
        if (!Program->Bindings[Index].InUse)
            continue;

        if (ActiveIndex == Arg1)
        {
            if (Arg4 != NULL)
                *Arg4 = 1;
            if (Arg5 != NULL)
                *Arg5 = GL_FLOAT;
            VirtGpuOglCopyNameResult(Program->Bindings[Index].Name, Arg2, Arg3, Arg6);
            return;
        }
        ++ActiveIndex;
    }

    VirtGpuOglSetError(Context, GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglGetActiveUniform(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLint * Arg4, GLenum * Arg5, GLchar * Arg6)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    ULONG Index;
    ULONG ActiveIndex = 0;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg2 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < VIRTGPU_OGL_MAX_UNIFORMS; ++Index)
    {
        if (!Program->Uniforms[Index].InUse)
            continue;

        if (ActiveIndex == Arg1)
        {
            if (Arg4 != NULL)
                *Arg4 = Program->Uniforms[Index].Size;
            if (Arg5 != NULL)
                *Arg5 = Program->Uniforms[Index].Type;
            VirtGpuOglCopyNameResult(Program->Uniforms[Index].Name, Arg2, Arg3, Arg6);
            return;
        }
        ++ActiveIndex;
    }

    VirtGpuOglSetError(Context, GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglGetAttachedShaders(GLuint Arg0, GLsizei Arg1, GLsizei * Arg2, GLuint * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    ULONG Count;
    ULONG Index;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Count = Program->AttachedShaderCount;
    if (Count > (ULONG)Arg1)
        Count = (ULONG)Arg1;

    if (Arg3 != NULL)
    {
        for (Index = 0; Index < Count; ++Index)
            Arg3[Index] = Program->AttachedShaders[Index];
    }
    if (Arg2 != NULL)
        *Arg2 = (GLsizei)Count;
}

static GLint APIENTRY
VirtGpuOglGetAttribLocation(GLuint Arg0, const GLchar * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_PROGRAM_BINDING Binding;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return -1;
    }

    if (VirtGpuOglReservedName(Arg1))
        return -1;

    Binding = VirtGpuOglFindAttribBinding(Program, Arg1);
    return (Binding != NULL) ? (GLint)Binding->Index : -1;
}

static void APIENTRY
VirtGpuOglGetProgramiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_DELETE_STATUS:
            *Arg2 = Program->DeletePending ? GL_TRUE : GL_FALSE;
            break;
        case GL_LINK_STATUS:
            *Arg2 = Program->Linked ? GL_TRUE : GL_FALSE;
            break;
        case GL_VALIDATE_STATUS:
            *Arg2 = Program->Validated ? GL_TRUE : GL_FALSE;
            break;
        case GL_INFO_LOG_LENGTH:
            *Arg2 = 1;
            break;
        case GL_ATTACHED_SHADERS:
            *Arg2 = (GLint)Program->AttachedShaderCount;
            break;
        case GL_ACTIVE_UNIFORMS:
            *Arg2 = (GLint)VirtGpuOglActiveUniformCount(Program);
            break;
        case GL_ACTIVE_UNIFORM_MAX_LENGTH:
            *Arg2 = VIRTGPU_OGL_MAX_NAME_LENGTH;
            break;
        case GL_ACTIVE_ATTRIBUTES:
            *Arg2 = (GLint)VirtGpuOglActiveAttribCount(Program);
            break;
        case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
            *Arg2 = VIRTGPU_OGL_MAX_NAME_LENGTH;
            break;
        case GL_ACTIVE_UNIFORM_BLOCKS:
            *Arg2 = 0;
            break;
        case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH:
            *Arg2 = 1;
            break;
        case GL_TRANSFORM_FEEDBACK_VARYINGS:
            *Arg2 = Program->TransformFeedbackVaryingCount;
            break;
        case GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
            *Arg2 = (GLint)Program->TransformFeedbackBufferMode;
            break;
        case GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH:
            *Arg2 = VIRTGPU_OGL_MAX_NAME_LENGTH;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetProgramInfoLog(GLuint Arg0, GLsizei Arg1, GLsizei * Arg2, GLchar * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((VirtGpuOglFindProgram(Context, Arg0) == NULL) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglCopyEmptyInfoLog(Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglGetShaderiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SHADER Shader;

    Shader = VirtGpuOglFindShader(Context, Arg0);
    if ((Shader == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_SHADER_TYPE:
            *Arg2 = (GLint)Shader->Type;
            break;
        case GL_DELETE_STATUS:
            *Arg2 = Shader->DeletePending ? GL_TRUE : GL_FALSE;
            break;
        case GL_COMPILE_STATUS:
            *Arg2 = Shader->Compiled ? GL_TRUE : GL_FALSE;
            break;
        case GL_INFO_LOG_LENGTH:
            *Arg2 = 1;
            break;
        case GL_SHADER_SOURCE_LENGTH:
            *Arg2 = (GLint)Shader->SourceLength + 1;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetShaderInfoLog(GLuint Arg0, GLsizei Arg1, GLsizei * Arg2, GLchar * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((VirtGpuOglFindShader(Context, Arg0) == NULL) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglCopyEmptyInfoLog(Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglGetShaderSource(GLuint Arg0, GLsizei Arg1, GLsizei * Arg2, GLchar * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SHADER Shader;
    static const GLchar EmptySource[] = "";

    Shader = VirtGpuOglFindShader(Context, Arg0);
    if ((Shader == NULL) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglCopyNameResult((Shader->Source != NULL) ? Shader->Source : EmptySource,
                             Arg1,
                             Arg2,
                             Arg3);
}

static GLint APIENTRY
VirtGpuOglGetUniformLocation(GLuint Arg0, const GLchar * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_UNIFORM Uniform;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return -1;
    }

    if (!Program->Linked)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return -1;
    }

    if (VirtGpuOglReservedName(Arg1))
        return -1;

    Uniform = VirtGpuOglFindUniformByName(Program, Arg1);
    if (Uniform == NULL)
        Uniform = VirtGpuOglAllocateUniform(Context, Program, Arg1);

    return (Uniform != NULL) ? Uniform->Location : -1;
}

static void APIENTRY
VirtGpuOglGetUniformfv(GLuint Arg0, GLint Arg1, GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_UNIFORM Uniform;
    ULONG Count;
    ULONG Index;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Uniform = VirtGpuOglFindUniformByLocation(Program, Arg1);
    if (Uniform == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Count = VirtGpuOglUniformComponentCount(Uniform->Type);
    for (Index = 0; Index < Count; ++Index)
        Arg2[Index] = Uniform->FloatValues[Index];
}

static void APIENTRY
VirtGpuOglGetUniformiv(GLuint Arg0, GLint Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_UNIFORM Uniform;
    ULONG Count;
    ULONG Index;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Uniform = VirtGpuOglFindUniformByLocation(Program, Arg1);
    if (Uniform == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Count = VirtGpuOglUniformComponentCount(Uniform->Type);
    for (Index = 0; Index < Count; ++Index)
        Arg2[Index] = Uniform->IntValues[Index];
}

static void APIENTRY
VirtGpuOglGetVertexAttribdv(GLuint Arg0, GLenum Arg1, GLdouble * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_VERTEX_ATTRIB Attrib;

    if ((Context == NULL) || (Arg0 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Attrib = &Context->VertexAttribs[Arg0];
    switch (Arg1)
    {
        case GL_CURRENT_VERTEX_ATTRIB:
            Arg2[0] = (GLdouble)Attrib->Current[0];
            Arg2[1] = (GLdouble)Attrib->Current[1];
            Arg2[2] = (GLdouble)Attrib->Current[2];
            Arg2[3] = (GLdouble)Attrib->Current[3];
            break;
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *Arg2 = (GLdouble)Attrib->Enabled;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *Arg2 = (GLdouble)Attrib->Size;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *Arg2 = (GLdouble)Attrib->Stride;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *Arg2 = (GLdouble)Attrib->Type;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *Arg2 = (GLdouble)Attrib->Normalized;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
            *Arg2 = (GLdouble)Attrib->Divisor;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetVertexAttribfv(GLuint Arg0, GLenum Arg1, GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_VERTEX_ATTRIB Attrib;

    if ((Context == NULL) || (Arg0 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Attrib = &Context->VertexAttribs[Arg0];
    switch (Arg1)
    {
        case GL_CURRENT_VERTEX_ATTRIB:
            CopyMemory(Arg2, Attrib->Current, sizeof(Attrib->Current));
            break;
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *Arg2 = (GLfloat)Attrib->Enabled;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *Arg2 = (GLfloat)Attrib->Size;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *Arg2 = (GLfloat)Attrib->Stride;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *Arg2 = (GLfloat)Attrib->Type;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *Arg2 = (GLfloat)Attrib->Normalized;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
            *Arg2 = (GLfloat)Attrib->Divisor;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetVertexAttribiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_VERTEX_ATTRIB Attrib;

    if ((Context == NULL) || (Arg0 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Attrib = &Context->VertexAttribs[Arg0];
    switch (Arg1)
    {
        case GL_CURRENT_VERTEX_ATTRIB:
            Arg2[0] = (GLint)Attrib->Current[0];
            Arg2[1] = (GLint)Attrib->Current[1];
            Arg2[2] = (GLint)Attrib->Current[2];
            Arg2[3] = (GLint)Attrib->Current[3];
            break;
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *Arg2 = Attrib->Enabled;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *Arg2 = Attrib->Size;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *Arg2 = Attrib->Stride;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *Arg2 = (GLint)Attrib->Type;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *Arg2 = Attrib->Normalized;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
            *Arg2 = (GLint)Attrib->Divisor;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetVertexAttribPointerv(GLuint Arg0, GLenum Arg1, void * * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg0 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg1 != GL_VERTEX_ATTRIB_ARRAY_POINTER)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    *Arg2 = (void *)Context->VertexAttribs[Arg0].Pointer;
}

static GLboolean APIENTRY
VirtGpuOglIsProgram(GLuint Arg0)
{
    return (VirtGpuOglFindProgram(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

static GLboolean APIENTRY
VirtGpuOglIsShader(GLuint Arg0)
{
    return (VirtGpuOglFindShader(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglLinkProgram(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_SHADER Shader;
    ULONG Index;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if (Program == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Program->AttachedShaderCount; ++Index)
    {
        Shader = VirtGpuOglFindShader(Context, Program->AttachedShaders[Index]);
        if ((Shader == NULL) || !Shader->Compiled)
        {
            Program->Linked = FALSE;
            Program->Validated = FALSE;
            return;
        }
    }

    Program->Linked = TRUE;
    Program->Validated = FALSE;
}

static void APIENTRY
VirtGpuOglShaderSource(GLuint Arg0, GLsizei Arg1, const GLchar *const * Arg2, const GLint * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SHADER Shader;
    GLchar *Source;
    ULONG TotalLength = 0;
    ULONG PartLength;
    ULONG Offset = 0;
    GLsizei Index;

    Shader = VirtGpuOglFindShader(Context, Arg0);
    if ((Shader == NULL) || (Arg1 < 0) || ((Arg1 > 0) && (Arg2 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg1; ++Index)
    {
        if (Arg2[Index] == NULL)
            continue;

        PartLength = ((Arg3 != NULL) && (Arg3[Index] >= 0)) ?
                     (ULONG)Arg3[Index] :
                     VirtGpuOglStringLength(Arg2[Index]);
        if (TotalLength > (0xFFFFFFFFUL - PartLength - 1))
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            return;
        }
        TotalLength += PartLength;
    }

    Source = HeapAlloc(GetProcessHeap(), 0, TotalLength + 1);
    if (Source == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    for (Index = 0; Index < Arg1; ++Index)
    {
        if (Arg2[Index] == NULL)
            continue;

        PartLength = ((Arg3 != NULL) && (Arg3[Index] >= 0)) ?
                     (ULONG)Arg3[Index] :
                     VirtGpuOglStringLength(Arg2[Index]);
        if (PartLength != 0)
            CopyMemory(&Source[Offset], Arg2[Index], PartLength);
        Offset += PartLength;
    }
    Source[Offset] = 0;

    if (Shader->Source != NULL)
        HeapFree(GetProcessHeap(), 0, Shader->Source);
    Shader->Source = Source;
    Shader->SourceLength = TotalLength;
    Shader->Compiled = FALSE;
}

static void APIENTRY
VirtGpuOglUseProgram(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_PROGRAM Previous;
    GLuint PreviousName;

    if (Context == NULL)
        return;

    if (Arg0 != 0)
    {
        Program = VirtGpuOglFindProgram(Context, Arg0);
        if ((Program == NULL) || !Program->Linked)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }
    }

    PreviousName = Context->CurrentProgram;
    Context->CurrentProgram = Arg0;

    if ((PreviousName != 0) && (PreviousName != Arg0))
    {
        Previous = VirtGpuOglFindProgram(Context, PreviousName);
        if ((Previous != NULL) && Previous->DeletePending)
            VirtGpuOglFreeProgram(Context, Previous);
    }
}

static void APIENTRY
VirtGpuOglUniform1f(GLint Arg0, GLfloat Arg1)
{
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT, 1, 1, &Arg1);
}

static void APIENTRY
VirtGpuOglUniform2f(GLint Arg0, GLfloat Arg1, GLfloat Arg2)
{
    GLfloat Values[2];

    Values[0] = Arg1;
    Values[1] = Arg2;
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_VEC2, 1, 2, Values);
}

static void APIENTRY
VirtGpuOglUniform3f(GLint Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    GLfloat Values[3];

    Values[0] = Arg1;
    Values[1] = Arg2;
    Values[2] = Arg3;
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_VEC3, 1, 3, Values);
}

static void APIENTRY
VirtGpuOglUniform4f(GLint Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3, GLfloat Arg4)
{
    GLfloat Values[4];

    Values[0] = Arg1;
    Values[1] = Arg2;
    Values[2] = Arg3;
    Values[3] = Arg4;
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_VEC4, 1, 4, Values);
}

static void APIENTRY
VirtGpuOglUniform1i(GLint Arg0, GLint Arg1)
{
    VirtGpuOglStoreUniformInt(Arg0, GL_INT, 1, 1, &Arg1);
}

static void APIENTRY
VirtGpuOglUniform2i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    GLint Values[2];

    Values[0] = Arg1;
    Values[1] = Arg2;
    VirtGpuOglStoreUniformInt(Arg0, GL_INT_VEC2, 1, 2, Values);
}

static void APIENTRY
VirtGpuOglUniform3i(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    GLint Values[3];

    Values[0] = Arg1;
    Values[1] = Arg2;
    Values[2] = Arg3;
    VirtGpuOglStoreUniformInt(Arg0, GL_INT_VEC3, 1, 3, Values);
}

static void APIENTRY
VirtGpuOglUniform4i(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4)
{
    GLint Values[4];

    Values[0] = Arg1;
    Values[1] = Arg2;
    Values[2] = Arg3;
    Values[3] = Arg4;
    VirtGpuOglStoreUniformInt(Arg0, GL_INT_VEC4, 1, 4, Values);
}

static void APIENTRY
VirtGpuOglUniform1fv(GLint Arg0, GLsizei Arg1, const GLfloat * Arg2)
{
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT, Arg1, 1, Arg2);
}

static void APIENTRY
VirtGpuOglUniform2fv(GLint Arg0, GLsizei Arg1, const GLfloat * Arg2)
{
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_VEC2, Arg1, 2, Arg2);
}

static void APIENTRY
VirtGpuOglUniform3fv(GLint Arg0, GLsizei Arg1, const GLfloat * Arg2)
{
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_VEC3, Arg1, 3, Arg2);
}

static void APIENTRY
VirtGpuOglUniform4fv(GLint Arg0, GLsizei Arg1, const GLfloat * Arg2)
{
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_VEC4, Arg1, 4, Arg2);
}

static void APIENTRY
VirtGpuOglUniform1iv(GLint Arg0, GLsizei Arg1, const GLint * Arg2)
{
    VirtGpuOglStoreUniformInt(Arg0, GL_INT, Arg1, 1, Arg2);
}

static void APIENTRY
VirtGpuOglUniform2iv(GLint Arg0, GLsizei Arg1, const GLint * Arg2)
{
    VirtGpuOglStoreUniformInt(Arg0, GL_INT_VEC2, Arg1, 2, Arg2);
}

static void APIENTRY
VirtGpuOglUniform3iv(GLint Arg0, GLsizei Arg1, const GLint * Arg2)
{
    VirtGpuOglStoreUniformInt(Arg0, GL_INT_VEC3, Arg1, 3, Arg2);
}

static void APIENTRY
VirtGpuOglUniform4iv(GLint Arg0, GLsizei Arg1, const GLint * Arg2)
{
    VirtGpuOglStoreUniformInt(Arg0, GL_INT_VEC4, Arg1, 4, Arg2);
}

static void APIENTRY
VirtGpuOglUniformMatrix2fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_MAT2, Arg1, 4, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix3fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_MAT3, Arg1, 9, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix4fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_MAT4, Arg1, 16, Arg3);
}

static void APIENTRY
VirtGpuOglValidateProgram(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if (Program == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Program->Validated = Program->Linked;
}

static void APIENTRY
VirtGpuOglVertexAttrib1d(GLuint Arg0, GLdouble Arg1)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, 0.0f, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib1dv(GLuint Arg0, const GLdouble * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib1d(Arg0, Arg1[0]);
}

static void APIENTRY
VirtGpuOglVertexAttrib1f(GLuint Arg0, GLfloat Arg1)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, Arg1, 0.0f, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib1fv(GLuint Arg0, const GLfloat * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib1f(Arg0, Arg1[0]);
}

static void APIENTRY
VirtGpuOglVertexAttrib1s(GLuint Arg0, GLshort Arg1)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, 0.0f, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib1sv(GLuint Arg0, const GLshort * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib1s(Arg0, Arg1[0]);
}

static void APIENTRY
VirtGpuOglVertexAttrib2d(GLuint Arg0, GLdouble Arg1, GLdouble Arg2)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib2dv(GLuint Arg0, const GLdouble * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib2d(Arg0, Arg1[0], Arg1[1]);
}

static void APIENTRY
VirtGpuOglVertexAttrib2f(GLuint Arg0, GLfloat Arg1, GLfloat Arg2)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, Arg1, Arg2, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib2fv(GLuint Arg0, const GLfloat * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib2f(Arg0, Arg1[0], Arg1[1]);
}

static void APIENTRY
VirtGpuOglVertexAttrib2s(GLuint Arg0, GLshort Arg1, GLshort Arg2)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib2sv(GLuint Arg0, const GLshort * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib2s(Arg0, Arg1[0], Arg1[1]);
}

static void APIENTRY
VirtGpuOglVertexAttrib3d(GLuint Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib3dv(GLuint Arg0, const GLdouble * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib3d(Arg0, Arg1[0], Arg1[1], Arg1[2]);
}

static void APIENTRY
VirtGpuOglVertexAttrib3f(GLuint Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, Arg1, Arg2, Arg3, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib3fv(GLuint Arg0, const GLfloat * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib3f(Arg0, Arg1[0], Arg1[1], Arg1[2]);
}

static void APIENTRY
VirtGpuOglVertexAttrib3s(GLuint Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib3sv(GLuint Arg0, const GLshort * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib3s(Arg0, Arg1[0], Arg1[1], Arg1[2]);
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nbv(GLuint Arg0, const GLbyte * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     VirtGpuOglNormalizeByte(Arg1[0]),
                                     VirtGpuOglNormalizeByte(Arg1[1]),
                                     VirtGpuOglNormalizeByte(Arg1[2]),
                                     VirtGpuOglNormalizeByte(Arg1[3]));
}

static void APIENTRY
VirtGpuOglVertexAttrib4Niv(GLuint Arg0, const GLint * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     VirtGpuOglNormalizeInt(Arg1[0]),
                                     VirtGpuOglNormalizeInt(Arg1[1]),
                                     VirtGpuOglNormalizeInt(Arg1[2]),
                                     VirtGpuOglNormalizeInt(Arg1[3]));
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nsv(GLuint Arg0, const GLshort * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     VirtGpuOglNormalizeShort(Arg1[0]),
                                     VirtGpuOglNormalizeShort(Arg1[1]),
                                     VirtGpuOglNormalizeShort(Arg1[2]),
                                     VirtGpuOglNormalizeShort(Arg1[3]));
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nub(GLuint Arg0, GLubyte Arg1, GLubyte Arg2, GLubyte Arg3, GLubyte Arg4)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1 / 255.0f,
                                     (GLfloat)Arg2 / 255.0f,
                                     (GLfloat)Arg3 / 255.0f,
                                     (GLfloat)Arg4 / 255.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nubv(GLuint Arg0, const GLubyte * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib4Nub(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nuiv(GLuint Arg0, const GLuint * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1[0] / 4294967295.0f,
                                     (GLfloat)Arg1[1] / 4294967295.0f,
                                     (GLfloat)Arg1[2] / 4294967295.0f,
                                     (GLfloat)Arg1[3] / 4294967295.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib4Nusv(GLuint Arg0, const GLushort * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1[0] / 65535.0f,
                                     (GLfloat)Arg1[1] / 65535.0f,
                                     (GLfloat)Arg1[2] / 65535.0f,
                                     (GLfloat)Arg1[3] / 65535.0f);
}

static void APIENTRY
VirtGpuOglVertexAttrib4bv(GLuint Arg0, const GLbyte * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1[0],
                                     (GLfloat)Arg1[1],
                                     (GLfloat)Arg1[2],
                                     (GLfloat)Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttrib4d(GLuint Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, (GLfloat)Arg4);
}

static void APIENTRY
VirtGpuOglVertexAttrib4dv(GLuint Arg0, const GLdouble * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib4d(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttrib4f(GLuint Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3, GLfloat Arg4)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, Arg1, Arg2, Arg3, Arg4);
}

static void APIENTRY
VirtGpuOglVertexAttrib4fv(GLuint Arg0, const GLfloat * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib4f(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttrib4iv(GLuint Arg0, const GLint * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1[0],
                                     (GLfloat)Arg1[1],
                                     (GLfloat)Arg1[2],
                                     (GLfloat)Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttrib4s(GLuint Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3, GLshort Arg4)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, (GLfloat)Arg4);
}

static void APIENTRY
VirtGpuOglVertexAttrib4sv(GLuint Arg0, const GLshort * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttrib4s(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttrib4ubv(GLuint Arg0, const GLubyte * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1[0],
                                     (GLfloat)Arg1[1],
                                     (GLfloat)Arg1[2],
                                     (GLfloat)Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttrib4uiv(GLuint Arg0, const GLuint * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1[0],
                                     (GLfloat)Arg1[1],
                                     (GLfloat)Arg1[2],
                                     (GLfloat)Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttrib4usv(GLuint Arg0, const GLushort * Arg1)
{
    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1[0],
                                     (GLfloat)Arg1[1],
                                     (GLfloat)Arg1[2],
                                     (GLfloat)Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttribPointer(GLuint Arg0, GLint Arg1, GLenum Arg2, GLboolean Arg3, GLsizei Arg4, const void * Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_VERTEX_ATTRIB Attrib;

    if ((Context == NULL) ||
        (Arg0 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS) ||
        (Arg1 < 1) ||
        (Arg1 > 4) ||
        (Arg4 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglValidVertexAttribType(Arg2))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Attrib = &Context->VertexAttribs[Arg0];
    Attrib->Size = Arg1;
    Attrib->Type = Arg2;
    Attrib->Normalized = Arg3 ? GL_TRUE : GL_FALSE;
    Attrib->Stride = Arg4;
    Attrib->Pointer = Arg5;
}

static void APIENTRY
VirtGpuOglUniformMatrix2x3fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_MAT2x3, Arg1, 6, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix3x2fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_MAT3x2, Arg1, 6, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix2x4fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_MAT2x4, Arg1, 8, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix4x2fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_MAT4x2, Arg1, 8, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix3x4fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_MAT3x4, Arg1, 12, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix4x3fv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLfloat * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformFloat(Arg0, GL_FLOAT_MAT4x3, Arg1, 12, Arg3);
}

static void APIENTRY
VirtGpuOglColorMaski(GLuint Arg0, GLboolean Arg1, GLboolean Arg2, GLboolean Arg3, GLboolean Arg4)
{
    if (Arg0 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglColorMask(Arg1, Arg2, Arg3, Arg4);
}

static void APIENTRY
VirtGpuOglGetBooleani_v(GLenum Arg0, GLuint Arg1, GLboolean * Arg2)
{
    if (Arg1 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetBooleanv(Arg0, Arg2);
}

static void APIENTRY
VirtGpuOglGetIntegeri_v(GLenum Arg0, GLuint Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    if (Context == NULL)
        return;

    if (Arg1 < VIRTGPU_OGL_MAX_BUFFER_BINDINGS)
    {
        switch (Arg0)
        {
            case GL_UNIFORM_BUFFER_BINDING:
                *Arg2 = (GLint)Context->UniformBufferBindings[Arg1].Buffer;
                return;
            case GL_UNIFORM_BUFFER_START:
                *Arg2 = (GLint)Context->UniformBufferBindings[Arg1].Offset;
                return;
            case GL_UNIFORM_BUFFER_SIZE:
                *Arg2 = (GLint)Context->UniformBufferBindings[Arg1].Size;
                return;
            case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
                *Arg2 = (GLint)Context->TransformFeedbackBufferBindings[Arg1].Buffer;
                return;
            case GL_TRANSFORM_FEEDBACK_BUFFER_START:
                *Arg2 = (GLint)Context->TransformFeedbackBufferBindings[Arg1].Offset;
                return;
            case GL_TRANSFORM_FEEDBACK_BUFFER_SIZE:
                *Arg2 = (GLint)Context->TransformFeedbackBufferBindings[Arg1].Size;
                return;
            default:
                break;
        }
    }

    if ((Arg0 == GL_UNIFORM_BUFFER_BINDING) ||
        (Arg0 == GL_UNIFORM_BUFFER_START) ||
        (Arg0 == GL_UNIFORM_BUFFER_SIZE) ||
        (Arg0 == GL_TRANSFORM_FEEDBACK_BUFFER_BINDING) ||
        (Arg0 == GL_TRANSFORM_FEEDBACK_BUFFER_START) ||
        (Arg0 == GL_TRANSFORM_FEEDBACK_BUFFER_SIZE))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg1 != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetIntegerv(Arg0, Arg2);
}

static void APIENTRY
VirtGpuOglEnablei(GLenum Arg0, GLuint Arg1)
{
    if (Arg1 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglEnable(Arg0);
}

static void APIENTRY
VirtGpuOglDisablei(GLenum Arg0, GLuint Arg1)
{
    if (Arg1 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglDisable(Arg0);
}

static GLboolean APIENTRY
VirtGpuOglIsEnabledi(GLenum Arg0, GLuint Arg1)
{
    if (Arg1 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return GL_FALSE;
    }

    return VirtGpuOglIsEnabled(Arg0);
}

static void APIENTRY
VirtGpuOglBeginTransformFeedback(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TRANSFORM_FEEDBACK TransformFeedback;

    if (Context == NULL)
        return;

    if (!VirtGpuOglTransformFeedbackModeValid(Arg0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    TransformFeedback = VirtGpuOglFindTransformFeedback(Context, Context->BoundTransformFeedback);
    if (((TransformFeedback != NULL) && TransformFeedback->Active) ||
        ((TransformFeedback == NULL) && Context->DefaultTransformFeedbackActive))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (TransformFeedback != NULL)
    {
        TransformFeedback->Active = TRUE;
        TransformFeedback->Paused = FALSE;
        TransformFeedback->PrimitiveMode = Arg0;
    }
    else
    {
        Context->DefaultTransformFeedbackActive = TRUE;
        Context->DefaultTransformFeedbackPaused = FALSE;
        Context->DefaultTransformFeedbackPrimitiveMode = Arg0;
    }
}

static void APIENTRY
VirtGpuOglEndTransformFeedback(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TRANSFORM_FEEDBACK TransformFeedback;

    if (Context == NULL)
        return;

    TransformFeedback = VirtGpuOglFindTransformFeedback(Context, Context->BoundTransformFeedback);
    if (TransformFeedback != NULL)
    {
        if (!TransformFeedback->Active)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }

        TransformFeedback->Active = FALSE;
        TransformFeedback->Paused = FALSE;
        return;
    }

    if (!Context->DefaultTransformFeedbackActive)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Context->DefaultTransformFeedbackActive = FALSE;
    Context->DefaultTransformFeedbackPaused = FALSE;
}

static void APIENTRY
VirtGpuOglBindBufferRange(GLenum Arg0, GLuint Arg1, GLuint Arg2, GLintptr Arg3, GLsizeiptr Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer = NULL;
    PVIRTGPU_OGL_BUFFER_BINDING Binding;

    if (Context == NULL)
        return;

    if (!VirtGpuOglBufferRangeTargetBinding(Context, Arg0, Arg1, &Binding))
        return;

    if ((Arg3 < 0) || (Arg4 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg2 != 0)
    {
        Buffer = VirtGpuOglFindBuffer(Context, Arg2);
        if (Buffer == NULL)
            Buffer = VirtGpuOglAllocateBufferName(Context, Arg2);
        if (Buffer == NULL)
            return;
        if (!VirtGpuOglBufferRangeValid(Arg3, Arg4, Buffer->Size))
        {
            VirtGpuOglSetError(Context, GL_INVALID_VALUE);
            return;
        }
        Buffer->Target = Arg0;
    }

    Binding->Buffer = Arg2;
    Binding->Offset = Arg3;
    Binding->Size = Arg4;

    if (Arg0 == GL_UNIFORM_BUFFER)
        Context->BoundUniformBuffer = Arg2;
    else
        Context->BoundTransformFeedbackBuffer = Arg2;
}

static void APIENTRY
VirtGpuOglBindBufferBase(GLenum Arg0, GLuint Arg1, GLuint Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;
    GLsizeiptr Size = 0;

    if (Context == NULL)
        return;

    if (Arg2 != 0)
    {
        Buffer = VirtGpuOglFindBuffer(Context, Arg2);
        if (Buffer == NULL)
            Buffer = VirtGpuOglAllocateBufferName(Context, Arg2);
        if (Buffer == NULL)
            return;
        Size = Buffer->Size;
    }

    VirtGpuOglBindBufferRange(Arg0, Arg1, Arg2, 0, Size);
}

static void APIENTRY
VirtGpuOglTransformFeedbackVaryings(GLuint Arg0, GLsizei Arg1, const GLchar *const * Arg2, GLenum Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    GLsizei Index;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) ||
        (Arg1 < 0) ||
        (Arg1 > VIRTGPU_OGL_MAX_TRANSFORM_FEEDBACK_VARYINGS) ||
        ((Arg1 > 0) && (Arg2 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg3 != GL_INTERLEAVED_ATTRIBS) && (Arg3 != GL_SEPARATE_ATTRIBS))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    ZeroMemory(Program->TransformFeedbackVaryings,
               sizeof(Program->TransformFeedbackVaryings));
    Program->TransformFeedbackBufferMode = Arg3;
    Program->TransformFeedbackVaryingCount = Arg1;

    for (Index = 0; Index < Arg1; ++Index)
    {
        if (Arg2[Index] == NULL)
        {
            VirtGpuOglSetError(Context, GL_INVALID_VALUE);
            Program->TransformFeedbackVaryingCount = Index;
            return;
        }

        VirtGpuOglCopyFixedName(Program->TransformFeedbackVaryings[Index],
                                VIRTGPU_OGL_MAX_NAME_LENGTH,
                                Arg2[Index]);
    }

    Program->Linked = FALSE;
    Program->Validated = FALSE;
}

static void APIENTRY
VirtGpuOglGetTransformFeedbackVarying(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLsizei * Arg4, GLenum * Arg5, GLchar * Arg6)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) ||
        (Arg1 >= (GLuint)Program->TransformFeedbackVaryingCount) ||
        (Arg2 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg4 != NULL)
        *Arg4 = 1;
    if (Arg5 != NULL)
        *Arg5 = GL_FLOAT;
    VirtGpuOglCopyNameResult(Program->TransformFeedbackVaryings[Arg1],
                             Arg2,
                             Arg3,
                             Arg6);
}

static void APIENTRY
VirtGpuOglClampColor(GLenum Arg0, GLenum Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Arg1 != GL_FALSE) && (Arg1 != GL_TRUE) && (Arg1 != GL_FIXED_ONLY))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    switch (Arg0)
    {
        case GL_CLAMP_VERTEX_COLOR:
            Context->ClampVertexColor = Arg1;
            break;
        case GL_CLAMP_FRAGMENT_COLOR:
            Context->ClampFragmentColor = Arg1;
            break;
        case GL_CLAMP_READ_COLOR:
            Context->ClampReadColor = Arg1;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglBeginConditionalRender(GLuint Arg0, GLenum Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (VirtGpuOglFindQuery(Context, Arg0) == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_QUERY_WAIT:
        case GL_QUERY_NO_WAIT:
        case GL_QUERY_BY_REGION_WAIT:
        case GL_QUERY_BY_REGION_NO_WAIT:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (Context->ConditionalRenderActive)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Context->ConditionalRenderActive = TRUE;
    Context->ConditionalRenderQuery = Arg0;
    Context->ConditionalRenderMode = Arg1;
}

static void APIENTRY
VirtGpuOglEndConditionalRender(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (!Context->ConditionalRenderActive)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Context->ConditionalRenderActive = FALSE;
    Context->ConditionalRenderQuery = 0;
}

static void APIENTRY
VirtGpuOglVertexAttribIPointer(GLuint Arg0, GLint Arg1, GLenum Arg2, GLsizei Arg3, const void * Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_VERTEX_ATTRIB Attrib;

    if ((Context == NULL) ||
        (Arg0 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS) ||
        (Arg1 < 1) ||
        (Arg1 > 4) ||
        (Arg3 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglValidVertexAttribIntegerType(Arg2))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Attrib = &Context->VertexAttribs[Arg0];
    Attrib->Size = Arg1;
    Attrib->Type = Arg2;
    Attrib->Normalized = GL_FALSE;
    Attrib->Stride = Arg3;
    Attrib->Pointer = Arg4;
}

static void APIENTRY
VirtGpuOglGetVertexAttribIiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLfloat Values[4];
    ULONG Index;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetVertexAttribfv(Arg0, Arg1, Values);
    for (Index = 0; Index < 4; ++Index)
        Arg2[Index] = (GLint)Values[Index];
}

static void APIENTRY
VirtGpuOglGetVertexAttribIuiv(GLuint Arg0, GLenum Arg1, GLuint * Arg2)
{
    GLint Values[4];
    ULONG Index;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetVertexAttribIiv(Arg0, Arg1, Values);
    for (Index = 0; Index < 4; ++Index)
        Arg2[Index] = (GLuint)Values[Index];
}

static void APIENTRY
VirtGpuOglVertexAttribI1i(GLuint Arg0, GLint Arg1)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, 0.0f, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttribI2i(GLuint Arg0, GLint Arg1, GLint Arg2)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttribI3i(GLuint Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttribI4i(GLuint Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1,
                                     (GLfloat)Arg2,
                                     (GLfloat)Arg3,
                                     (GLfloat)Arg4);
}

static void APIENTRY
VirtGpuOglVertexAttribI1ui(GLuint Arg0, GLuint Arg1)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, 0.0f, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttribI2ui(GLuint Arg0, GLuint Arg1, GLuint Arg2)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttribI3ui(GLuint Arg0, GLuint Arg1, GLuint Arg2, GLuint Arg3)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, 1.0f);
}

static void APIENTRY
VirtGpuOglVertexAttribI4ui(GLuint Arg0, GLuint Arg1, GLuint Arg2, GLuint Arg3, GLuint Arg4)
{
    VirtGpuOglSetVertexAttribCurrent(Arg0,
                                     (GLfloat)Arg1,
                                     (GLfloat)Arg2,
                                     (GLfloat)Arg3,
                                     (GLfloat)Arg4);
}

static void APIENTRY
VirtGpuOglVertexAttribI1iv(GLuint Arg0, const GLint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI1i(Arg0, Arg1[0]);
}

static void APIENTRY
VirtGpuOglVertexAttribI2iv(GLuint Arg0, const GLint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI2i(Arg0, Arg1[0], Arg1[1]);
}

static void APIENTRY
VirtGpuOglVertexAttribI3iv(GLuint Arg0, const GLint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI3i(Arg0, Arg1[0], Arg1[1], Arg1[2]);
}

static void APIENTRY
VirtGpuOglVertexAttribI4iv(GLuint Arg0, const GLint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI4i(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttribI1uiv(GLuint Arg0, const GLuint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI1ui(Arg0, Arg1[0]);
}

static void APIENTRY
VirtGpuOglVertexAttribI2uiv(GLuint Arg0, const GLuint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI2ui(Arg0, Arg1[0], Arg1[1]);
}

static void APIENTRY
VirtGpuOglVertexAttribI3uiv(GLuint Arg0, const GLuint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI3ui(Arg0, Arg1[0], Arg1[1], Arg1[2]);
}

static void APIENTRY
VirtGpuOglVertexAttribI4uiv(GLuint Arg0, const GLuint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI4ui(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttribI4bv(GLuint Arg0, const GLbyte * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI4i(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttribI4sv(GLuint Arg0, const GLshort * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI4i(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttribI4ubv(GLuint Arg0, const GLubyte * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI4ui(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglVertexAttribI4usv(GLuint Arg0, const GLushort * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglVertexAttribI4ui(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
}

static void APIENTRY
VirtGpuOglGetUniformuiv(GLuint Arg0, GLint Arg1, GLuint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_UNIFORM Uniform;
    ULONG Count;
    ULONG Index;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Uniform = VirtGpuOglFindUniformByLocation(Program, Arg1);
    if (Uniform == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Count = VirtGpuOglUniformComponentCount(Uniform->Type);
    for (Index = 0; Index < Count; ++Index)
        Arg2[Index] = (GLuint)Uniform->IntValues[Index];
}

static void APIENTRY
VirtGpuOglBindFragDataLocation(GLuint Arg0, GLuint Arg1, const GLchar * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_PROGRAM_BINDING Binding;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (VirtGpuOglReservedName(Arg2))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Binding = VirtGpuOglFindFragDataBinding(Program, Arg2);
    if (Binding == NULL)
        Binding = VirtGpuOglAllocateFragDataBinding(Context, Program);
    if (Binding == NULL)
        return;

    Binding->Index = Arg1;
    VirtGpuOglCopyFixedName(Binding->Name, VIRTGPU_OGL_MAX_NAME_LENGTH, Arg2);
    Program->Linked = FALSE;
    Program->Validated = FALSE;
}

static GLint APIENTRY
VirtGpuOglGetFragDataLocation(GLuint Arg0, const GLchar * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_PROGRAM_BINDING Binding;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return -1;
    }

    Binding = VirtGpuOglFindFragDataBinding(Program, Arg1);
    return (Binding != NULL) ? (GLint)Binding->Index : -1;
}

static void APIENTRY
VirtGpuOglUniform1ui(GLint Arg0, GLuint Arg1)
{
    VirtGpuOglStoreUniformUInt(Arg0, GL_UNSIGNED_INT, 1, 1, &Arg1);
}

static void APIENTRY
VirtGpuOglUniform2ui(GLint Arg0, GLuint Arg1, GLuint Arg2)
{
    GLuint Values[2];

    Values[0] = Arg1;
    Values[1] = Arg2;
    VirtGpuOglStoreUniformUInt(Arg0, GL_UNSIGNED_INT_VEC2, 1, 2, Values);
}

static void APIENTRY
VirtGpuOglUniform3ui(GLint Arg0, GLuint Arg1, GLuint Arg2, GLuint Arg3)
{
    GLuint Values[3];

    Values[0] = Arg1;
    Values[1] = Arg2;
    Values[2] = Arg3;
    VirtGpuOglStoreUniformUInt(Arg0, GL_UNSIGNED_INT_VEC3, 1, 3, Values);
}

static void APIENTRY
VirtGpuOglUniform4ui(GLint Arg0, GLuint Arg1, GLuint Arg2, GLuint Arg3, GLuint Arg4)
{
    GLuint Values[4];

    Values[0] = Arg1;
    Values[1] = Arg2;
    Values[2] = Arg3;
    Values[3] = Arg4;
    VirtGpuOglStoreUniformUInt(Arg0, GL_UNSIGNED_INT_VEC4, 1, 4, Values);
}

static void APIENTRY
VirtGpuOglUniform1uiv(GLint Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    VirtGpuOglStoreUniformUInt(Arg0, GL_UNSIGNED_INT, Arg1, 1, Arg2);
}

static void APIENTRY
VirtGpuOglUniform2uiv(GLint Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    VirtGpuOglStoreUniformUInt(Arg0, GL_UNSIGNED_INT_VEC2, Arg1, 2, Arg2);
}

static void APIENTRY
VirtGpuOglUniform3uiv(GLint Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    VirtGpuOglStoreUniformUInt(Arg0, GL_UNSIGNED_INT_VEC3, Arg1, 3, Arg2);
}

static void APIENTRY
VirtGpuOglUniform4uiv(GLint Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    VirtGpuOglStoreUniformUInt(Arg0, GL_UNSIGNED_INT_VEC4, Arg1, 4, Arg2);
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
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLclampf OldColor[4];
    GLint OldStencil;

    if ((Context == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg0 == GL_STENCIL)
    {
        if (Arg1 != 0)
        {
            VirtGpuOglSetError(Context, GL_INVALID_VALUE);
            return;
        }

        OldStencil = Context->ClearStencil;
        VirtGpuOglClearStencil(Arg2[0]);
        VirtGpuOglClear(GL_STENCIL_BUFFER_BIT);
        Context->ClearStencil = OldStencil;
        return;
    }

    if ((Arg0 == GL_COLOR) && (Arg1 == 0))
    {
        CopyMemory(OldColor, Context->ClearColor, sizeof(OldColor));
        VirtGpuOglClearColor(VirtGpuOglClampFloat((GLfloat)Arg2[0]),
                             VirtGpuOglClampFloat((GLfloat)Arg2[1]),
                             VirtGpuOglClampFloat((GLfloat)Arg2[2]),
                             VirtGpuOglClampFloat((GLfloat)Arg2[3]));
        VirtGpuOglClear(GL_COLOR_BUFFER_BIT);
        CopyMemory(Context->ClearColor, OldColor, sizeof(OldColor));
        return;
    }

    VirtGpuOglSetError(Context, (Arg0 == GL_COLOR) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
}

static void APIENTRY
VirtGpuOglClearBufferuiv(GLenum Arg0, GLint Arg1, const GLuint * Arg2)
{
    GLint Values[4];

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    Values[0] = (GLint)Arg2[0];
    Values[1] = (GLint)Arg2[1];
    Values[2] = (GLint)Arg2[2];
    Values[3] = (GLint)Arg2[3];
    VirtGpuOglClearBufferiv(Arg0, Arg1, Values);
}

static void APIENTRY
VirtGpuOglClearBufferfv(GLenum Arg0, GLint Arg1, const GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLclampf OldColor[4];
    GLclampd OldDepth;

    if ((Context == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg0)
    {
        case GL_COLOR:
            if (Arg1 != 0)
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            CopyMemory(OldColor, Context->ClearColor, sizeof(OldColor));
            VirtGpuOglClearColor(Arg2[0], Arg2[1], Arg2[2], Arg2[3]);
            VirtGpuOglClear(GL_COLOR_BUFFER_BIT);
            CopyMemory(Context->ClearColor, OldColor, sizeof(OldColor));
            break;
        case GL_DEPTH:
            if (Arg1 != 0)
            {
                VirtGpuOglSetError(Context, GL_INVALID_VALUE);
                return;
            }
            OldDepth = Context->ClearDepth;
            VirtGpuOglClearDepth(Arg2[0]);
            VirtGpuOglClear(GL_DEPTH_BUFFER_BIT);
            Context->ClearDepth = OldDepth;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglClearBufferfi(GLenum Arg0, GLint Arg1, GLfloat Arg2, GLint Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLclampd OldDepth;
    GLint OldStencil;

    if (Context == NULL)
        return;

    if ((Arg0 != GL_DEPTH_STENCIL) || (Arg1 != 0))
    {
        VirtGpuOglSetError(Context, (Arg1 != 0) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    OldDepth = Context->ClearDepth;
    OldStencil = Context->ClearStencil;
    VirtGpuOglClearDepth(Arg2);
    VirtGpuOglClearStencil(Arg3);
    VirtGpuOglClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    Context->ClearDepth = OldDepth;
    Context->ClearStencil = OldStencil;
}

static const GLubyte * APIENTRY
VirtGpuOglGetStringi(GLenum Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Arg0 != GL_EXTENSIONS)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return NULL;
    }

    if (Arg1 != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return NULL;
    }

    VirtGpuOglSetError(Context, GL_INVALID_VALUE);
    return NULL;
}

static GLboolean APIENTRY
VirtGpuOglIsRenderbuffer(GLuint Arg0)
{
    return (VirtGpuOglFindRenderbuffer(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglBindRenderbuffer(GLenum Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_RENDERBUFFER Renderbuffer;

    if (Context == NULL)
        return;

    if (Arg0 != GL_RENDERBUFFER)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Arg1 != 0)
    {
        Renderbuffer = VirtGpuOglFindRenderbuffer(Context, Arg1);
        if (Renderbuffer == NULL)
            Renderbuffer = VirtGpuOglAllocateRenderbufferName(Context, Arg1);
        if (Renderbuffer == NULL)
            return;
    }

    Context->BoundRenderbuffer = Arg1;
}

static void APIENTRY
VirtGpuOglDeleteRenderbuffers(GLsizei Arg0, const GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    ULONG FbIndex;
    ULONG AttachmentIndex;

    if ((Context == NULL) || (Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        PVIRTGPU_OGL_RENDERBUFFER Renderbuffer;

        Renderbuffer = VirtGpuOglFindRenderbuffer(Context, Arg1[Index]);
        if (Renderbuffer == NULL)
            continue;

        if (Context->BoundRenderbuffer == Arg1[Index])
            Context->BoundRenderbuffer = 0;

        for (FbIndex = 0; FbIndex < VIRTGPU_OGL_MAX_FRAMEBUFFERS; ++FbIndex)
        {
            if (!Context->Framebuffers[FbIndex].Allocated)
                continue;

            for (AttachmentIndex = 0;
                 AttachmentIndex < VIRTGPU_OGL_FRAMEBUFFER_ATTACHMENTS;
                 ++AttachmentIndex)
            {
                if ((Context->Framebuffers[FbIndex].Attachments[AttachmentIndex].ObjectType == GL_RENDERBUFFER) &&
                    (Context->Framebuffers[FbIndex].Attachments[AttachmentIndex].ObjectName == Arg1[Index]))
                {
                    ZeroMemory(&Context->Framebuffers[FbIndex].Attachments[AttachmentIndex],
                               sizeof(Context->Framebuffers[FbIndex].Attachments[AttachmentIndex]));
                }
            }
        }

        ZeroMemory(Renderbuffer, sizeof(*Renderbuffer));
    }
}

static void APIENTRY
VirtGpuOglGenRenderbuffers(GLsizei Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    GLuint Name;

    if ((Context == NULL) || (Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        do
        {
            Name = Context->NextRenderbufferName++;
            if (Context->NextRenderbufferName == 0)
                Context->NextRenderbufferName = 1;
        } while ((Name == 0) || (VirtGpuOglFindRenderbuffer(Context, Name) != NULL));

        if (VirtGpuOglAllocateRenderbufferName(Context, Name) == NULL)
            return;
        Arg1[Index] = Name;
    }
}

static void APIENTRY
VirtGpuOglRenderbufferStorage(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLsizei Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_RENDERBUFFER Renderbuffer;

    if (Context == NULL)
        return;

    if ((Arg0 != GL_RENDERBUFFER) || (Arg2 < 0) || (Arg3 < 0))
    {
        VirtGpuOglSetError(Context, (Arg0 != GL_RENDERBUFFER) ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return;
    }

    Renderbuffer = VirtGpuOglFindRenderbuffer(Context, Context->BoundRenderbuffer);
    if (Renderbuffer == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    Renderbuffer->InternalFormat = Arg1;
    Renderbuffer->Width = Arg2;
    Renderbuffer->Height = Arg3;
    Renderbuffer->Samples = 0;
}

static void APIENTRY
VirtGpuOglGetRenderbufferParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_RENDERBUFFER Renderbuffer;
    GLint RedBits;
    GLint GreenBits;
    GLint BlueBits;
    GLint AlphaBits;
    GLint DepthBits;
    GLint StencilBits;

    if ((Context == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg0 != GL_RENDERBUFFER)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Renderbuffer = VirtGpuOglFindRenderbuffer(Context, Context->BoundRenderbuffer);
    if (Renderbuffer == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    VirtGpuOglFormatComponentBits(Renderbuffer->InternalFormat,
                                  &RedBits,
                                  &GreenBits,
                                  &BlueBits,
                                  &AlphaBits,
                                  &DepthBits,
                                  &StencilBits);

    switch (Arg1)
    {
        case GL_RENDERBUFFER_WIDTH:
            *Arg2 = Renderbuffer->Width;
            break;
        case GL_RENDERBUFFER_HEIGHT:
            *Arg2 = Renderbuffer->Height;
            break;
        case GL_RENDERBUFFER_INTERNAL_FORMAT:
            *Arg2 = (GLint)Renderbuffer->InternalFormat;
            break;
        case GL_RENDERBUFFER_SAMPLES:
            *Arg2 = Renderbuffer->Samples;
            break;
        case GL_RENDERBUFFER_RED_SIZE:
            *Arg2 = RedBits;
            break;
        case GL_RENDERBUFFER_GREEN_SIZE:
            *Arg2 = GreenBits;
            break;
        case GL_RENDERBUFFER_BLUE_SIZE:
            *Arg2 = BlueBits;
            break;
        case GL_RENDERBUFFER_ALPHA_SIZE:
            *Arg2 = AlphaBits;
            break;
        case GL_RENDERBUFFER_DEPTH_SIZE:
            *Arg2 = DepthBits;
            break;
        case GL_RENDERBUFFER_STENCIL_SIZE:
            *Arg2 = StencilBits;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static GLboolean APIENTRY
VirtGpuOglIsFramebuffer(GLuint Arg0)
{
    return (VirtGpuOglFindFramebuffer(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglBindFramebuffer(GLenum Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_FRAMEBUFFER Framebuffer;

    if (Context == NULL)
        return;

    switch (Arg0)
    {
        case GL_FRAMEBUFFER:
        case GL_READ_FRAMEBUFFER:
        case GL_DRAW_FRAMEBUFFER:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (Arg1 != 0)
    {
        Framebuffer = VirtGpuOglFindFramebuffer(Context, Arg1);
        if (Framebuffer == NULL)
            Framebuffer = VirtGpuOglAllocateFramebufferName(Context, Arg1);
        if (Framebuffer == NULL)
            return;
    }

    if ((Arg0 == GL_FRAMEBUFFER) || (Arg0 == GL_READ_FRAMEBUFFER))
        Context->BoundReadFramebuffer = Arg1;
    if ((Arg0 == GL_FRAMEBUFFER) || (Arg0 == GL_DRAW_FRAMEBUFFER))
        Context->BoundDrawFramebuffer = Arg1;
}

static void APIENTRY
VirtGpuOglDeleteFramebuffers(GLsizei Arg0, const GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    PVIRTGPU_OGL_FRAMEBUFFER Framebuffer;

    if ((Context == NULL) || (Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        Framebuffer = VirtGpuOglFindFramebuffer(Context, Arg1[Index]);
        if (Framebuffer == NULL)
            continue;

        if (Context->BoundReadFramebuffer == Arg1[Index])
            Context->BoundReadFramebuffer = 0;
        if (Context->BoundDrawFramebuffer == Arg1[Index])
            Context->BoundDrawFramebuffer = 0;
        ZeroMemory(Framebuffer, sizeof(*Framebuffer));
    }
}

static void APIENTRY
VirtGpuOglGenFramebuffers(GLsizei Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    GLuint Name;

    if ((Context == NULL) || (Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        do
        {
            Name = Context->NextFramebufferName++;
            if (Context->NextFramebufferName == 0)
                Context->NextFramebufferName = 1;
        } while ((Name == 0) || (VirtGpuOglFindFramebuffer(Context, Name) != NULL));

        if (VirtGpuOglAllocateFramebufferName(Context, Name) == NULL)
            return;
        Arg1[Index] = Name;
    }
}

static GLenum APIENTRY
VirtGpuOglCheckFramebufferStatus(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_FRAMEBUFFER Framebuffer;
    GLuint ReadName;
    GLuint DrawName;
    GLuint Name;
    ULONG Index;
    BOOL HasAttachment = FALSE;

    if (Context == NULL)
        return 0;

    if (!VirtGpuOglFramebufferTargetBinding(Context, Arg0, &ReadName, &DrawName))
        return 0;

    Name = (Arg0 == GL_READ_FRAMEBUFFER) ? ReadName : DrawName;
    if (Name == 0)
        return GL_FRAMEBUFFER_COMPLETE;

    Framebuffer = VirtGpuOglFindFramebuffer(Context, Name);
    if (Framebuffer == NULL)
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;

    for (Index = 0; Index < VIRTGPU_OGL_FRAMEBUFFER_ATTACHMENTS; ++Index)
    {
        PVIRTGPU_OGL_FRAMEBUFFER_ATTACHMENT Attachment = &Framebuffer->Attachments[Index];

        if (Attachment->ObjectName == 0)
            continue;

        HasAttachment = TRUE;
        if (Attachment->ObjectType == GL_RENDERBUFFER)
        {
            PVIRTGPU_OGL_RENDERBUFFER Renderbuffer;

            Renderbuffer = VirtGpuOglFindRenderbuffer(Context, Attachment->ObjectName);
            if ((Renderbuffer == NULL) ||
                (Renderbuffer->Width <= 0) ||
                (Renderbuffer->Height <= 0))
            {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
        }
        else if (Attachment->ObjectType == GL_TEXTURE)
        {
            PVIRTGPU_OGL_TEXTURE Texture;

            Texture = VirtGpuOglFindTexture(Context, Attachment->ObjectName);
            if ((Texture == NULL) ||
                (Texture->Width <= 0) ||
                (Texture->Height <= 0) ||
                (Texture->Data == NULL))
            {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
        }
        else
        {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
    }

    return HasAttachment ? GL_FRAMEBUFFER_COMPLETE :
                           GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
}

static void APIENTRY
VirtGpuOglFramebufferTexture1D(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLuint Arg3, GLint Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_FRAMEBUFFER Framebuffer;
    ULONG Index;

    if ((Context == NULL) || (Arg2 != GL_TEXTURE_1D) || (Arg4 < 0))
    {
        VirtGpuOglSetError(Context, (Arg4 < 0) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Framebuffer = VirtGpuOglBoundFramebuffer(Context, Arg0);
    if ((Framebuffer == NULL) ||
        !VirtGpuOglFramebufferAttachmentIndex(Arg1, &Index))
    {
        VirtGpuOglSetError(Context, (Framebuffer == NULL) ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }

    Framebuffer->Attachments[Index].ObjectType = (Arg3 != 0) ? GL_TEXTURE : 0;
    Framebuffer->Attachments[Index].ObjectName = Arg3;
    Framebuffer->Attachments[Index].TextureTarget = Arg2;
    Framebuffer->Attachments[Index].TextureLevel = Arg4;
    Framebuffer->Attachments[Index].TextureLayer = 0;
}

static void APIENTRY
VirtGpuOglFramebufferTexture2D(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLuint Arg3, GLint Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_FRAMEBUFFER Framebuffer;
    ULONG Index;

    if ((Context == NULL) || (Arg2 != GL_TEXTURE_2D) || (Arg4 < 0))
    {
        VirtGpuOglSetError(Context, (Arg4 < 0) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Framebuffer = VirtGpuOglBoundFramebuffer(Context, Arg0);
    if ((Framebuffer == NULL) ||
        !VirtGpuOglFramebufferAttachmentIndex(Arg1, &Index))
    {
        VirtGpuOglSetError(Context, (Framebuffer == NULL) ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }

    Framebuffer->Attachments[Index].ObjectType = (Arg3 != 0) ? GL_TEXTURE : 0;
    Framebuffer->Attachments[Index].ObjectName = Arg3;
    Framebuffer->Attachments[Index].TextureTarget = Arg2;
    Framebuffer->Attachments[Index].TextureLevel = Arg4;
    Framebuffer->Attachments[Index].TextureLayer = 0;
}

static void APIENTRY
VirtGpuOglFramebufferTexture3D(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLuint Arg3, GLint Arg4, GLint Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_FRAMEBUFFER Framebuffer;
    ULONG Index;

    if ((Context == NULL) || (Arg2 != GL_TEXTURE_3D) || (Arg4 < 0) || (Arg5 < 0))
    {
        VirtGpuOglSetError(Context, ((Arg4 < 0) || (Arg5 < 0)) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Framebuffer = VirtGpuOglBoundFramebuffer(Context, Arg0);
    if ((Framebuffer == NULL) ||
        !VirtGpuOglFramebufferAttachmentIndex(Arg1, &Index))
    {
        VirtGpuOglSetError(Context, (Framebuffer == NULL) ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }

    Framebuffer->Attachments[Index].ObjectType = (Arg3 != 0) ? GL_TEXTURE : 0;
    Framebuffer->Attachments[Index].ObjectName = Arg3;
    Framebuffer->Attachments[Index].TextureTarget = Arg2;
    Framebuffer->Attachments[Index].TextureLevel = Arg4;
    Framebuffer->Attachments[Index].TextureLayer = Arg5;
}

static void APIENTRY
VirtGpuOglFramebufferRenderbuffer(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLuint Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_FRAMEBUFFER Framebuffer;
    ULONG Index;

    if ((Context == NULL) || (Arg2 != GL_RENDERBUFFER))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Framebuffer = VirtGpuOglBoundFramebuffer(Context, Arg0);
    if ((Framebuffer == NULL) ||
        !VirtGpuOglFramebufferAttachmentIndex(Arg1, &Index))
    {
        VirtGpuOglSetError(Context, (Framebuffer == NULL) ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }

    Framebuffer->Attachments[Index].ObjectType = (Arg3 != 0) ? GL_RENDERBUFFER : 0;
    Framebuffer->Attachments[Index].ObjectName = Arg3;
    Framebuffer->Attachments[Index].TextureTarget = 0;
    Framebuffer->Attachments[Index].TextureLevel = 0;
    Framebuffer->Attachments[Index].TextureLayer = 0;
}

static void APIENTRY
VirtGpuOglGetFramebufferAttachmentParameteriv(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLint * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_FRAMEBUFFER Framebuffer;
    PVIRTGPU_OGL_FRAMEBUFFER_ATTACHMENT Attachment;
    ULONG Index;

    if ((Context == NULL) || (Arg3 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Framebuffer = VirtGpuOglBoundFramebuffer(Context, Arg0);
    if ((Framebuffer == NULL) ||
        !VirtGpuOglFramebufferAttachmentIndex(Arg1, &Index))
    {
        VirtGpuOglSetError(Context, (Framebuffer == NULL) ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }

    Attachment = &Framebuffer->Attachments[Index];
    switch (Arg2)
    {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            *Arg3 = (GLint)((Attachment->ObjectName != 0) ? Attachment->ObjectType : GL_NONE);
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
            *Arg3 = (GLint)Attachment->ObjectName;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
            *Arg3 = Attachment->TextureLevel;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
            *Arg3 = Attachment->TextureLayer;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGenerateMipmap(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (VirtGpuOglBoundTexture(Context, Arg0) == NULL)
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
}

static void APIENTRY
VirtGpuOglBlitFramebuffer(GLint Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4, GLint Arg5, GLint Arg6, GLint Arg7, GLbitfield Arg8, GLenum Arg9)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    const GLbitfield ValidMask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;

    UNREFERENCED_PARAMETER(Arg0);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);

    if (Context == NULL)
        return;

    if ((Arg8 & ~ValidMask) != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if ((Arg9 != GL_NEAREST) && (Arg9 != GL_LINEAR))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }
}

static void APIENTRY
VirtGpuOglRenderbufferStorageMultisample(GLenum Arg0, GLsizei Arg1, GLenum Arg2, GLsizei Arg3, GLsizei Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_RENDERBUFFER Renderbuffer;

    if ((Context == NULL) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglRenderbufferStorage(Arg0, Arg2, Arg3, Arg4);
    if (Context->LastError != GL_NO_ERROR)
        return;

    Renderbuffer = VirtGpuOglFindRenderbuffer(Context, Context->BoundRenderbuffer);
    if (Renderbuffer != NULL)
    {
        Renderbuffer->Samples = Arg1;
    }
}

static void APIENTRY
VirtGpuOglFramebufferTextureLayer(GLenum Arg0, GLenum Arg1, GLuint Arg2, GLint Arg3, GLint Arg4)
{
    VirtGpuOglFramebufferTexture3D(Arg0, Arg1, GL_TEXTURE_3D, Arg2, Arg3, Arg4);
}

static void * APIENTRY
VirtGpuOglMapBufferRange(GLenum Arg0, GLintptr Arg1, GLsizeiptr Arg2, GLbitfield Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;
    const GLbitfield ValidAccess =
        GL_MAP_READ_BIT |
        GL_MAP_WRITE_BIT |
        GL_MAP_INVALIDATE_RANGE_BIT |
        GL_MAP_INVALIDATE_BUFFER_BIT |
        GL_MAP_FLUSH_EXPLICIT_BIT |
        GL_MAP_UNSYNCHRONIZED_BIT;

    if ((Arg1 < 0) || (Arg2 < 0) || ((Arg3 & ~ValidAccess) != 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return NULL;
    }

    if (((Arg3 & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT)) == 0) ||
        (((Arg3 & GL_MAP_READ_BIT) != 0) &&
         ((Arg3 & (GL_MAP_INVALIDATE_RANGE_BIT |
                   GL_MAP_INVALIDATE_BUFFER_BIT |
                   GL_MAP_UNSYNCHRONIZED_BIT)) != 0)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return NULL;
    }

    Buffer = VirtGpuOglBoundBuffer(Context, Arg0);
    if (Buffer == NULL)
        return NULL;

    if (Buffer->Mapped)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return NULL;
    }

    if (!VirtGpuOglBufferRangeValid(Arg1, Arg2, Buffer->Size))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return NULL;
    }

    Buffer->Mapped = TRUE;
    Buffer->Access = ((Arg3 & GL_MAP_READ_BIT) != 0) ?
                     (((Arg3 & GL_MAP_WRITE_BIT) != 0) ? GL_READ_WRITE : GL_READ_ONLY) :
                     GL_WRITE_ONLY;
    return Buffer->Data + (SIZE_T)Arg1;
}

static void APIENTRY
VirtGpuOglFlushMappedBufferRange(GLenum Arg0, GLintptr Arg1, GLsizeiptr Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER Buffer;

    Buffer = VirtGpuOglBoundBuffer(Context, Arg0);
    if (Buffer == NULL)
        return;

    if (!Buffer->Mapped)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if (!VirtGpuOglBufferRangeValid(Arg1, Arg2, Buffer->Size))
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglBindVertexArray(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_VERTEX_ARRAY OldVertexArray;
    PVIRTGPU_OGL_VERTEX_ARRAY NewVertexArray;

    if (Context == NULL)
        return;

    OldVertexArray = VirtGpuOglFindVertexArray(Context, Context->BoundVertexArray);
    if (OldVertexArray != NULL)
        VirtGpuOglSaveVertexArrayState(Context, OldVertexArray);

    if (Arg0 == 0)
    {
        Context->BoundVertexArray = 0;
        return;
    }

    NewVertexArray = VirtGpuOglFindVertexArray(Context, Arg0);
    if (NewVertexArray == NULL)
        NewVertexArray = VirtGpuOglAllocateVertexArrayName(Context, Arg0);
    if (NewVertexArray == NULL)
        return;

    Context->BoundVertexArray = Arg0;
    VirtGpuOglLoadVertexArrayState(Context, NewVertexArray);
}

static void APIENTRY
VirtGpuOglDeleteVertexArrays(GLsizei Arg0, const GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    PVIRTGPU_OGL_VERTEX_ARRAY VertexArray;

    if ((Context == NULL) || (Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        VertexArray = VirtGpuOglFindVertexArray(Context, Arg1[Index]);
        if (VertexArray == NULL)
            continue;

        if (Context->BoundVertexArray == Arg1[Index])
            Context->BoundVertexArray = 0;
        ZeroMemory(VertexArray, sizeof(*VertexArray));
    }
}

static void APIENTRY
VirtGpuOglGenVertexArrays(GLsizei Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_VERTEX_ARRAY VertexArray;
    GLsizei Index;
    GLuint Name;
    ULONG Attempts;
    BOOL Found;

    if ((Context == NULL) || (Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        Found = FALSE;
        for (Attempts = 0; Attempts <= VIRTGPU_OGL_MAX_VERTEX_ARRAYS; ++Attempts)
        {
            Name = Context->NextVertexArrayName++;
            if (Context->NextVertexArrayName == 0)
                Context->NextVertexArrayName = 1;
            if ((Name != 0) && (VirtGpuOglFindVertexArray(Context, Name) == NULL))
            {
                Found = TRUE;
                break;
            }
        }

        if (!Found || (VirtGpuOglAllocateVertexArrayName(Context, Name) == NULL))
        {
            VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
            while (Index > 0)
            {
                --Index;
                VertexArray = VirtGpuOglFindVertexArray(Context, Arg1[Index]);
                if (VertexArray != NULL)
                    ZeroMemory(VertexArray, sizeof(*VertexArray));
                Arg1[Index] = 0;
            }
            return;
        }
        Arg1[Index] = Name;
    }
}

static GLboolean APIENTRY
VirtGpuOglIsVertexArray(GLuint Arg0)
{
    return (VirtGpuOglFindVertexArray(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

typedef struct _VIRTGPU_OGL_DRAW_ARRAYS_INDIRECT_COMMAND
{
    GLuint Count;
    GLuint PrimCount;
    GLuint First;
    GLuint BaseInstance;
} VIRTGPU_OGL_DRAW_ARRAYS_INDIRECT_COMMAND, *PVIRTGPU_OGL_DRAW_ARRAYS_INDIRECT_COMMAND;

typedef struct _VIRTGPU_OGL_DRAW_ELEMENTS_INDIRECT_COMMAND
{
    GLuint Count;
    GLuint PrimCount;
    GLuint FirstIndex;
    GLuint BaseVertex;
    GLuint BaseInstance;
} VIRTGPU_OGL_DRAW_ELEMENTS_INDIRECT_COMMAND, *PVIRTGPU_OGL_DRAW_ELEMENTS_INDIRECT_COMMAND;

static const VOID *
VirtGpuOglIndirectCommandPointer(
    _Inout_ PVIRTGPU_OGL_CONTEXT Context,
    _In_opt_ const VOID *Pointer,
    _In_ ULONG CommandSize)
{
    PVIRTGPU_OGL_BUFFER Buffer;
    SIZE_T Offset;

    if (Context->BoundDrawIndirectBuffer == 0)
        return Pointer;

    Buffer = VirtGpuOglFindBuffer(Context, Context->BoundDrawIndirectBuffer);
    if (Buffer == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return NULL;
    }

    Offset = (SIZE_T)Pointer;
    if (!VirtGpuOglBufferRangeValid((GLintptr)Offset,
                                    (GLsizeiptr)CommandSize,
                                    Buffer->Size))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return NULL;
    }

    return Buffer->Data + Offset;
}

static void APIENTRY
VirtGpuOglDrawArraysInstanced(GLenum Arg0, GLint Arg1, GLsizei Arg2, GLsizei Arg3)
{
    GLsizei Index;

    if (Arg3 < 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg3; ++Index)
        VirtGpuOglDrawArrays(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglDrawElementsInstanced(GLenum Arg0, GLsizei Arg1, GLenum Arg2, const void * Arg3, GLsizei Arg4)
{
    GLsizei Index;

    if (Arg4 < 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg4; ++Index)
        VirtGpuOglDrawElements(Arg0, Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglTexBuffer(GLenum Arg0, GLenum Arg1, GLuint Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TEXTURE Texture;

    if (Context == NULL)
        return;

    if (Arg0 != GL_TEXTURE_BUFFER)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Texture = VirtGpuOglBoundTexture(Context, GL_TEXTURE_BUFFER);
    if (Texture == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if ((Arg2 != 0) && (VirtGpuOglFindBuffer(Context, Arg2) == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Texture->BufferName = Arg2;
    Texture->BufferInternalFormat = Arg1;
    Texture->InternalFormat = Arg1;
    Texture->Format = Arg1;
    Texture->Target = GL_TEXTURE_BUFFER;
}

static void APIENTRY
VirtGpuOglPrimitiveRestartIndex(GLuint Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context != NULL)
        Context->PrimitiveRestartIndex = Arg0;
}

static void APIENTRY
VirtGpuOglCopyBufferSubData(GLenum Arg0, GLenum Arg1, GLintptr Arg2, GLintptr Arg3, GLsizeiptr Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER ReadBuffer;
    PVIRTGPU_OGL_BUFFER WriteBuffer;

    if ((Context == NULL) || (Arg2 < 0) || (Arg3 < 0) || (Arg4 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    ReadBuffer = VirtGpuOglBoundBuffer(Context, Arg0);
    WriteBuffer = VirtGpuOglBoundBuffer(Context, Arg1);
    if ((ReadBuffer == NULL) || (WriteBuffer == NULL))
        return;

    if (!VirtGpuOglBufferRangeValid(Arg2, Arg4, ReadBuffer->Size) ||
        !VirtGpuOglBufferRangeValid(Arg3, Arg4, WriteBuffer->Size))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg4 != 0)
        MoveMemory(WriteBuffer->Data + (SIZE_T)Arg3,
                   ReadBuffer->Data + (SIZE_T)Arg2,
                   (SIZE_T)Arg4);
}

static void APIENTRY
VirtGpuOglGetUniformIndices(GLuint Arg0, GLsizei Arg1, const GLchar *const * Arg2, GLuint * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    GLsizei NameIndex;
    ULONG UniformIndex;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) ||
        (Arg1 < 0) ||
        ((Arg1 > 0) && ((Arg2 == NULL) || (Arg3 == NULL))))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (NameIndex = 0; NameIndex < Arg1; ++NameIndex)
    {
        Arg3[NameIndex] = GL_INVALID_INDEX;
        if (Arg2[NameIndex] == NULL)
            continue;

        for (UniformIndex = 0; UniformIndex < VIRTGPU_OGL_MAX_UNIFORMS; ++UniformIndex)
        {
            if (Program->Uniforms[UniformIndex].InUse &&
                VirtGpuOglStringEquals(Program->Uniforms[UniformIndex].Name, Arg2[NameIndex]))
            {
                Arg3[NameIndex] = UniformIndex;
                break;
            }
        }
    }
}

static void APIENTRY
VirtGpuOglGetActiveUniformsiv(GLuint Arg0, GLsizei Arg1, const GLuint * Arg2, GLenum Arg3, GLint * Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    GLsizei Index;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) ||
        (Arg1 < 0) ||
        ((Arg1 > 0) && ((Arg2 == NULL) || (Arg4 == NULL))))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg1; ++Index)
    {
        PVIRTGPU_OGL_UNIFORM Uniform;

        if (Arg2[Index] >= VIRTGPU_OGL_MAX_UNIFORMS)
        {
            VirtGpuOglSetError(Context, GL_INVALID_VALUE);
            return;
        }

        Uniform = &Program->Uniforms[Arg2[Index]];
        if (!Uniform->InUse)
        {
            VirtGpuOglSetError(Context, GL_INVALID_VALUE);
            return;
        }

        switch (Arg3)
        {
            case GL_UNIFORM_TYPE:
                Arg4[Index] = (GLint)Uniform->Type;
                break;
            case GL_UNIFORM_SIZE:
                Arg4[Index] = Uniform->Size;
                break;
            case GL_UNIFORM_NAME_LENGTH:
                Arg4[Index] = (GLint)VirtGpuOglStringLength(Uniform->Name) + 1;
                break;
            case GL_UNIFORM_BLOCK_INDEX:
                Arg4[Index] = -1;
                break;
            case GL_UNIFORM_OFFSET:
            case GL_UNIFORM_ARRAY_STRIDE:
            case GL_UNIFORM_MATRIX_STRIDE:
                Arg4[Index] = 0;
                break;
            case GL_UNIFORM_IS_ROW_MAJOR:
                Arg4[Index] = GL_FALSE;
                break;
            default:
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                return;
        }
    }
}

static void APIENTRY
VirtGpuOglGetActiveUniformName(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLchar * Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || (Arg1 >= VIRTGPU_OGL_MAX_UNIFORMS) || (Arg2 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!Program->Uniforms[Arg1].InUse)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglCopyNameResult(Program->Uniforms[Arg1].Name, Arg2, Arg3, Arg4);
}

static GLuint APIENTRY
VirtGpuOglGetUniformBlockIndex(GLuint Arg0, const GLchar * Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);
    if (VirtGpuOglFindProgram(VirtGpuOglCurrentContext(), Arg0) == NULL)
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
    return GL_INVALID_INDEX;
}

static void APIENTRY
VirtGpuOglGetActiveUniformBlockiv(GLuint Arg0, GLuint Arg1, GLenum Arg2, GLint * Arg3)
{
    UNREFERENCED_PARAMETER(Arg1);
    if ((VirtGpuOglFindProgram(VirtGpuOglCurrentContext(), Arg0) == NULL) || (Arg3 == NULL))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    switch (Arg2)
    {
        case GL_UNIFORM_BLOCK_BINDING:
        case GL_UNIFORM_BLOCK_DATA_SIZE:
        case GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
            *Arg3 = 0;
            break;
        case GL_UNIFORM_BLOCK_NAME_LENGTH:
            *Arg3 = 1;
            break;
        default:
            VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetActiveUniformBlockName(GLuint Arg0, GLuint Arg1, GLsizei Arg2, GLsizei * Arg3, GLchar * Arg4)
{
    UNREFERENCED_PARAMETER(Arg1);
    if ((VirtGpuOglFindProgram(VirtGpuOglCurrentContext(), Arg0) == NULL) || (Arg2 < 0))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglCopyNameResult("", Arg2, Arg3, Arg4);
}

static void APIENTRY
VirtGpuOglUniformBlockBinding(GLuint Arg0, GLuint Arg1, GLuint Arg2)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    if (VirtGpuOglFindProgram(VirtGpuOglCurrentContext(), Arg0) == NULL)
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglDrawElementsBaseVertex(GLenum Arg0, GLsizei Arg1, GLenum Arg2, const void * Arg3, GLint Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_BUFFER ElementBuffer;
    const void *Elements = Arg3;
    GLuint *Adjusted;
    GLsizei Index;
    GLint ElementIndex;
    ULONG ElementSize;
    ULONGLONG IndexBytes;
    SIZE_T Offset;

    if (Arg4 == 0)
    {
        VirtGpuOglDrawElements(Arg0, Arg1, Arg2, Arg3);
        return;
    }

    if ((Context == NULL) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglElementTypeSize(Arg2, &ElementSize))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Arg1 == 0)
        return;

    if (Context->BoundElementArrayBuffer != 0)
    {
        ElementBuffer = VirtGpuOglFindBuffer(Context, Context->BoundElementArrayBuffer);
        if (ElementBuffer == NULL)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }

        Offset = (SIZE_T)Arg3;
        IndexBytes = (ULONGLONG)(ULONG)Arg1 * ElementSize;
        if (!VirtGpuOglBufferRangeValid((GLintptr)Offset,
                                        (GLsizeiptr)IndexBytes,
                                        ElementBuffer->Size))
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }

        Elements = ElementBuffer->Data + Offset;
    }
    else if (Arg3 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Adjusted = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)Arg1 * sizeof(GLuint));
    if (Adjusted == NULL)
    {
        VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
        return;
    }

    for (Index = 0; Index < Arg1; ++Index)
    {
        switch (Arg2)
        {
            case GL_UNSIGNED_BYTE:
                ElementIndex = ((const GLubyte *)Elements)[Index] + Arg4;
                break;
            case GL_UNSIGNED_SHORT:
                ElementIndex = ((const GLushort *)Elements)[Index] + Arg4;
                break;
            case GL_UNSIGNED_INT:
                ElementIndex = (GLint)((const GLuint *)Elements)[Index] + Arg4;
                break;
            default:
                HeapFree(GetProcessHeap(), 0, Adjusted);
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                return;
        }

        if (ElementIndex < 0)
        {
            HeapFree(GetProcessHeap(), 0, Adjusted);
            VirtGpuOglSetError(Context, GL_INVALID_VALUE);
            return;
        }

        Adjusted[Index] = (GLuint)ElementIndex;
    }

    VirtGpuOglDrawElements(Arg0, Arg1, GL_UNSIGNED_INT, Adjusted);
    HeapFree(GetProcessHeap(), 0, Adjusted);
}

static void APIENTRY
VirtGpuOglDrawRangeElementsBaseVertex(GLenum Arg0, GLuint Arg1, GLuint Arg2, GLsizei Arg3, GLenum Arg4, const void * Arg5, GLint Arg6)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglDrawElementsBaseVertex(Arg0, Arg3, Arg4, Arg5, Arg6);
}

static void APIENTRY
VirtGpuOglDrawElementsInstancedBaseVertex(GLenum Arg0, GLsizei Arg1, GLenum Arg2, const void * Arg3, GLsizei Arg4, GLint Arg5)
{
    GLsizei Index;

    if (Arg4 < 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg4; ++Index)
        VirtGpuOglDrawElementsBaseVertex(Arg0, Arg1, Arg2, Arg3, Arg5);
}

static void APIENTRY
VirtGpuOglMultiDrawElementsBaseVertex(GLenum Arg0, const GLsizei * Arg1, GLenum Arg2, const void *const * Arg3, GLsizei Arg4, const GLint * Arg5)
{
    GLsizei Index;

    if ((Arg4 < 0) || ((Arg4 > 0) && ((Arg1 == NULL) || (Arg3 == NULL) || (Arg5 == NULL))))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg4; ++Index)
        VirtGpuOglDrawElementsBaseVertex(Arg0, Arg1[Index], Arg2, Arg3[Index], Arg5[Index]);
}

static void APIENTRY
VirtGpuOglProvokingVertex(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Arg0 != GL_FIRST_VERTEX_CONVENTION) && (Arg0 != GL_LAST_VERTEX_CONVENTION))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Context->ProvokingVertexMode = Arg0;
}

static GLsync APIENTRY
VirtGpuOglFenceSync(GLenum Arg0, GLbitfield Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if (Context == NULL)
        return NULL;

    if ((Arg0 != GL_SYNC_GPU_COMMANDS_COMPLETE) || (Arg1 != 0))
    {
        VirtGpuOglSetError(Context, (Arg0 != GL_SYNC_GPU_COMMANDS_COMPLETE) ?
                           GL_INVALID_ENUM : GL_INVALID_VALUE);
        return NULL;
    }

    for (Index = 0; Index < VIRTGPU_OGL_MAX_SYNCS; ++Index)
    {
        if (!Context->Syncs[Index].Allocated)
        {
            Context->Syncs[Index].Allocated = TRUE;
            Context->Syncs[Index].Condition = Arg0;
            Context->Syncs[Index].Flags = Arg1;
            Context->Syncs[Index].Status = GL_SIGNALED;
            return (GLsync)&Context->Syncs[Index];
        }
    }

    VirtGpuOglSetError(Context, GL_OUT_OF_MEMORY);
    return NULL;
}

static GLboolean APIENTRY
VirtGpuOglIsSync(GLsync Arg0)
{
    return (VirtGpuOglFindSync(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglDeleteSync(GLsync Arg0)
{
    PVIRTGPU_OGL_SYNC Sync;

    if (Arg0 == NULL)
        return;

    Sync = VirtGpuOglFindSync(VirtGpuOglCurrentContext(), Arg0);
    if (Sync != NULL)
        ZeroMemory(Sync, sizeof(*Sync));
}

static GLenum APIENTRY
VirtGpuOglClientWaitSync(GLsync Arg0, GLbitfield Arg1, GLuint64 Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SYNC Sync;

    UNREFERENCED_PARAMETER(Arg2);

    if ((Arg1 & ~GL_SYNC_FLUSH_COMMANDS_BIT) != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }

    Sync = VirtGpuOglFindSync(Context, Arg0);
    if (Sync == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }

    Sync->Status = GL_SIGNALED;
    return GL_ALREADY_SIGNALED;
}

static void APIENTRY
VirtGpuOglWaitSync(GLsync Arg0, GLbitfield Arg1, GLuint64 Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SYNC Sync;

    UNREFERENCED_PARAMETER(Arg2);

    if (Arg1 != 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Sync = VirtGpuOglFindSync(Context, Arg0);
    if (Sync == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Sync->Status = GL_SIGNALED;
}

static void APIENTRY
VirtGpuOglGetInteger64v(GLenum Arg0, GLint64 * Arg1)
{
    GLint Values[16] = { 0 };
    ULONG Index;

    if (Arg1 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetIntegerv(Arg0, Values);
    for (Index = 0; Index < 16; ++Index)
        Arg1[Index] = Values[Index];
}

static void APIENTRY
VirtGpuOglGetSynciv(GLsync Arg0, GLenum Arg1, GLsizei Arg2, GLsizei * Arg3, GLint * Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SYNC Sync;
    GLint Value;

    if ((Arg2 < 0) || ((Arg2 > 0) && (Arg4 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Sync = VirtGpuOglFindSync(Context, Arg0);
    if (Sync == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_OBJECT_TYPE:
            Value = GL_SYNC_FENCE;
            break;
        case GL_SYNC_STATUS:
            Value = (GLint)Sync->Status;
            break;
        case GL_SYNC_CONDITION:
            Value = (GLint)Sync->Condition;
            break;
        case GL_SYNC_FLAGS:
            Value = (GLint)Sync->Flags;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    if (Arg3 != NULL)
        *Arg3 = (Arg2 > 0) ? 1 : 0;
    if (Arg2 > 0)
        Arg4[0] = Value;
}

static void APIENTRY
VirtGpuOglGetInteger64i_v(GLenum Arg0, GLuint Arg1, GLint64 * Arg2)
{
    GLint Value = 0;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetIntegeri_v(Arg0, Arg1, &Value);
    *Arg2 = Value;
}

static void APIENTRY
VirtGpuOglGetBufferParameteri64v(GLenum Arg0, GLenum Arg1, GLint64 * Arg2)
{
    GLint Value;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetBufferParameteriv(Arg0, Arg1, &Value);
    *Arg2 = Value;
}

static void APIENTRY
VirtGpuOglFramebufferTexture(GLenum Arg0, GLenum Arg1, GLuint Arg2, GLint Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TEXTURE Texture;

    if (Arg2 == 0)
    {
        VirtGpuOglFramebufferTexture2D(Arg0, Arg1, GL_TEXTURE_2D, 0, Arg3);
        return;
    }

    Texture = VirtGpuOglFindTexture(Context, Arg2);
    if (Texture == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Texture->Target)
    {
        case GL_TEXTURE_1D:
            VirtGpuOglFramebufferTexture1D(Arg0, Arg1, GL_TEXTURE_1D, Arg2, Arg3);
            break;
        case GL_TEXTURE_2D:
            VirtGpuOglFramebufferTexture2D(Arg0, Arg1, GL_TEXTURE_2D, Arg2, Arg3);
            break;
        case GL_TEXTURE_3D:
            VirtGpuOglFramebufferTexture3D(Arg0, Arg1, GL_TEXTURE_3D, Arg2, Arg3, 0);
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            break;
    }
}

static void APIENTRY
VirtGpuOglTexImage2DMultisample(GLenum Arg0, GLsizei Arg1, GLenum Arg2, GLsizei Arg3, GLsizei Arg4, GLboolean Arg5)
{
    UNREFERENCED_PARAMETER(Arg5);
    if (Arg1 < 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglTexImage2D(Arg0, 0, Arg2, Arg3, Arg4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

static void APIENTRY
VirtGpuOglTexImage3DMultisample(GLenum Arg0, GLsizei Arg1, GLenum Arg2, GLsizei Arg3, GLsizei Arg4, GLsizei Arg5, GLboolean Arg6)
{
    UNREFERENCED_PARAMETER(Arg6);
    if (Arg1 < 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglTexImage3D(Arg0, 0, Arg2, Arg3, Arg4, Arg5, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

static void APIENTRY
VirtGpuOglGetMultisamplefv(GLenum Arg0, GLuint Arg1, GLfloat * Arg2)
{
    UNREFERENCED_PARAMETER(Arg1);

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    if (Arg0 != GL_SAMPLE_POSITION)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
        return;
    }

    Arg2[0] = 0.5f;
    Arg2[1] = 0.5f;
}

static void APIENTRY
VirtGpuOglSampleMaski(GLuint Arg0, GLbitfield Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);
    if (Arg0 != 0)
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglBindFragDataLocationIndexed(GLuint Arg0, GLuint Arg1, GLuint Arg2, const GLchar * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglBindFragDataLocation(Arg0, Arg1, Arg3);
}

static GLint APIENTRY
VirtGpuOglGetFragDataIndex(GLuint Arg0, const GLchar * Arg1)
{
    return (VirtGpuOglGetFragDataLocation(Arg0, Arg1) >= 0) ? 0 : -1;
}

static void APIENTRY
VirtGpuOglGenSamplers(GLsizei Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    GLuint Name;

    if ((Context == NULL) || (Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        do
        {
            Name = Context->NextSamplerName++;
            if (Context->NextSamplerName == 0)
                Context->NextSamplerName = 1;
        } while ((Name == 0) || (VirtGpuOglFindSampler(Context, Name) != NULL));

        if (VirtGpuOglAllocateSamplerName(Context, Name) == NULL)
            return;
        Arg1[Index] = Name;
    }
}

static void APIENTRY
VirtGpuOglDeleteSamplers(GLsizei Arg0, const GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    ULONG Unit;
    PVIRTGPU_OGL_SAMPLER Sampler;

    if ((Context == NULL) || (Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        Sampler = VirtGpuOglFindSampler(Context, Arg1[Index]);
        if (Sampler == NULL)
            continue;

        for (Unit = 0; Unit < VIRTGPU_OGL_MAX_TEXTURE_UNITS; ++Unit)
        {
            if (Context->BoundSamplers[Unit] == Arg1[Index])
                Context->BoundSamplers[Unit] = 0;
        }
        ZeroMemory(Sampler, sizeof(*Sampler));
    }
}

static GLboolean APIENTRY
VirtGpuOglIsSampler(GLuint Arg0)
{
    return (VirtGpuOglFindSampler(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglBindSampler(GLuint Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SAMPLER Sampler;

    if (Context == NULL)
        return;

    if (Arg0 >= VIRTGPU_OGL_MAX_TEXTURE_UNITS)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (Arg1 != 0)
    {
        Sampler = VirtGpuOglFindSampler(Context, Arg1);
        if (Sampler == NULL)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }
    }

    Context->BoundSamplers[Arg0] = Arg1;
}

static void APIENTRY
VirtGpuOglSamplerParameteri(GLuint Arg0, GLenum Arg1, GLint Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SAMPLER Sampler;

    Sampler = VirtGpuOglFindSampler(Context, Arg0);
    if (Sampler == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
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
            if (Arg1 == GL_TEXTURE_MIN_FILTER)
                Sampler->MinFilter = Arg2;
            else
                Sampler->MagFilter = Arg2;
            break;
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:
            if (!VirtGpuOglValidTextureWrap(Arg2))
            {
                VirtGpuOglSetError(Context, GL_INVALID_ENUM);
                return;
            }
            if (Arg1 == GL_TEXTURE_WRAP_S)
                Sampler->WrapS = Arg2;
            else if (Arg1 == GL_TEXTURE_WRAP_T)
                Sampler->WrapT = Arg2;
            else
                Sampler->WrapR = Arg2;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglSamplerParameteriv(GLuint Arg0, GLenum Arg1, const GLint * Arg2)
{
    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSamplerParameteri(Arg0, Arg1, Arg2[0]);
}

static void APIENTRY
VirtGpuOglSamplerParameterf(GLuint Arg0, GLenum Arg1, GLfloat Arg2)
{
    VirtGpuOglSamplerParameteri(Arg0, Arg1, (GLint)Arg2);
}

static void APIENTRY
VirtGpuOglSamplerParameterfv(GLuint Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglSamplerParameterf(Arg0, Arg1, Arg2[0]);
}

static void APIENTRY
VirtGpuOglSamplerParameterIiv(GLuint Arg0, GLenum Arg1, const GLint * Arg2)
{
    VirtGpuOglSamplerParameteriv(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglSamplerParameterIuiv(GLuint Arg0, GLenum Arg1, const GLuint * Arg2)
{
    GLint Value;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    Value = (GLint)Arg2[0];
    VirtGpuOglSamplerParameteri(Arg0, Arg1, Value);
}

static void APIENTRY
VirtGpuOglGetSamplerParameteriv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_SAMPLER Sampler;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Sampler = VirtGpuOglFindSampler(Context, Arg0);
    if (Sampler == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_TEXTURE_MIN_FILTER:
            *Arg2 = (GLint)Sampler->MinFilter;
            break;
        case GL_TEXTURE_MAG_FILTER:
            *Arg2 = (GLint)Sampler->MagFilter;
            break;
        case GL_TEXTURE_WRAP_S:
            *Arg2 = (GLint)Sampler->WrapS;
            break;
        case GL_TEXTURE_WRAP_T:
            *Arg2 = (GLint)Sampler->WrapT;
            break;
        case GL_TEXTURE_WRAP_R:
            *Arg2 = (GLint)Sampler->WrapR;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetSamplerParameterIiv(GLuint Arg0, GLenum Arg1, GLint * Arg2)
{
    VirtGpuOglGetSamplerParameteriv(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglGetSamplerParameterfv(GLuint Arg0, GLenum Arg1, GLfloat * Arg2)
{
    GLint Value;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetSamplerParameteriv(Arg0, Arg1, &Value);
    *Arg2 = (GLfloat)Value;
}

static void APIENTRY
VirtGpuOglGetSamplerParameterIuiv(GLuint Arg0, GLenum Arg1, GLuint * Arg2)
{
    GLint Value;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetSamplerParameteriv(Arg0, Arg1, &Value);
    *Arg2 = (GLuint)Value;
}

static void APIENTRY
VirtGpuOglQueryCounter(GLuint Arg0, GLenum Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_QUERY Query;

    if (Context == NULL)
        return;

    if (Arg1 != GL_TIMESTAMP)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Query = VirtGpuOglFindQuery(Context, Arg0);
    if (Query == NULL)
        Query = VirtGpuOglAllocateQueryName(Context, Arg0);
    if (Query == NULL)
        return;

    Query->Target = Arg1;
    Query->Active = FALSE;
    Query->Result = GetTickCount();
}

static void APIENTRY
VirtGpuOglGetQueryObjecti64v(GLuint Arg0, GLenum Arg1, GLint64 * Arg2)
{
    GLuint Value;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetQueryObjectuiv(Arg0, Arg1, &Value);
    *Arg2 = (GLint64)Value;
}

static void APIENTRY
VirtGpuOglGetQueryObjectui64v(GLuint Arg0, GLenum Arg1, GLuint64 * Arg2)
{
    GLuint Value;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetQueryObjectuiv(Arg0, Arg1, &Value);
    *Arg2 = (GLuint64)Value;
}

static void APIENTRY
VirtGpuOglVertexAttribDivisor(GLuint Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg0 >= VIRTGPU_OGL_MAX_VERTEX_ATTRIBS))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->VertexAttribs[Arg0].Divisor = Arg1;
}

static GLint
VirtGpuOglSignExtendPacked(_In_ GLuint Value, _In_ ULONG Bits)
{
    GLuint SignBit = 1u << (Bits - 1);
    GLuint Mask = (1u << Bits) - 1u;

    Value &= Mask;
    return (GLint)((Value ^ SignBit) - SignBit);
}

static GLfloat
VirtGpuOglPackedComponentFloat(
    _In_ GLint Value,
    _In_ ULONG Bits,
    _In_ BOOL Signed,
    _In_ GLboolean Normalized)
{
    if (Normalized == GL_TRUE)
    {
        if (Signed)
        {
            GLint MaxValue = (1 << (Bits - 1)) - 1;
            GLint MinValue = -(1 << (Bits - 1));

            if (Value <= MinValue)
                return -1.0f;
            return (GLfloat)Value / (GLfloat)MaxValue;
        }
        return (GLfloat)Value / (GLfloat)((1u << Bits) - 1u);
    }

    return (GLfloat)Value;
}

static VOID
VirtGpuOglSetVertexAttribPacked(
    _In_ GLuint Index,
    _In_ ULONG ComponentCount,
    _In_ GLenum Type,
    _In_ GLboolean Normalized,
    _In_ GLuint Packed)
{
    BOOL Signed;
    GLint Values[4];
    GLfloat Converted[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ULONG Component;
    ULONG Bits;

    if ((Type != GL_UNSIGNED_INT_2_10_10_10_REV) &&
        (Type != GL_INT_2_10_10_10_REV))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
        return;
    }

    Signed = (Type == GL_INT_2_10_10_10_REV);
    if (Signed)
    {
        Values[0] = VirtGpuOglSignExtendPacked(Packed, 10);
        Values[1] = VirtGpuOglSignExtendPacked(Packed >> 10, 10);
        Values[2] = VirtGpuOglSignExtendPacked(Packed >> 20, 10);
        Values[3] = VirtGpuOglSignExtendPacked(Packed >> 30, 2);
    }
    else
    {
        Values[0] = (GLint)(Packed & 0x3ffu);
        Values[1] = (GLint)((Packed >> 10) & 0x3ffu);
        Values[2] = (GLint)((Packed >> 20) & 0x3ffu);
        Values[3] = (GLint)((Packed >> 30) & 0x3u);
    }

    for (Component = 0; Component < ComponentCount; ++Component)
    {
        Bits = (Component == 3) ? 2 : 10;
        Converted[Component] = VirtGpuOglPackedComponentFloat(Values[Component],
                                                              Bits,
                                                              Signed,
                                                              Normalized);
    }

    VirtGpuOglSetVertexAttribCurrent(Index,
                                     Converted[0],
                                     Converted[1],
                                     Converted[2],
                                     Converted[3]);
}

static void APIENTRY
VirtGpuOglVertexAttribP1ui(GLuint Arg0, GLenum Arg1, GLboolean Arg2, GLuint Arg3)
{
    VirtGpuOglSetVertexAttribPacked(Arg0, 1, Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglVertexAttribP1uiv(GLuint Arg0, GLenum Arg1, GLboolean Arg2, const GLuint * Arg3)
{
    if (Arg3 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttribP1ui(Arg0, Arg1, Arg2, Arg3[0]);
}

static void APIENTRY
VirtGpuOglVertexAttribP2ui(GLuint Arg0, GLenum Arg1, GLboolean Arg2, GLuint Arg3)
{
    VirtGpuOglSetVertexAttribPacked(Arg0, 2, Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglVertexAttribP2uiv(GLuint Arg0, GLenum Arg1, GLboolean Arg2, const GLuint * Arg3)
{
    if (Arg3 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttribP2ui(Arg0, Arg1, Arg2, Arg3[0]);
}

static void APIENTRY
VirtGpuOglVertexAttribP3ui(GLuint Arg0, GLenum Arg1, GLboolean Arg2, GLuint Arg3)
{
    VirtGpuOglSetVertexAttribPacked(Arg0, 3, Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglVertexAttribP3uiv(GLuint Arg0, GLenum Arg1, GLboolean Arg2, const GLuint * Arg3)
{
    if (Arg3 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttribP3ui(Arg0, Arg1, Arg2, Arg3[0]);
}

static void APIENTRY
VirtGpuOglVertexAttribP4ui(GLuint Arg0, GLenum Arg1, GLboolean Arg2, GLuint Arg3)
{
    VirtGpuOglSetVertexAttribPacked(Arg0, 4, Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglVertexAttribP4uiv(GLuint Arg0, GLenum Arg1, GLboolean Arg2, const GLuint * Arg3)
{
    if (Arg3 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglVertexAttribP4ui(Arg0, Arg1, Arg2, Arg3[0]);
}

static void APIENTRY
VirtGpuOglMinSampleShading(GLfloat Arg0)
{
    if (Arg0 < 0.0f)
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglBlendEquationi(GLuint Arg0, GLenum Arg1)
{
    if (Arg0 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglBlendEquation(Arg1);
}

static void APIENTRY
VirtGpuOglBlendEquationSeparatei(GLuint Arg0, GLenum Arg1, GLenum Arg2)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglBlendEquationi(Arg0, Arg1);
}

static void APIENTRY
VirtGpuOglBlendFunci(GLuint Arg0, GLenum Arg1, GLenum Arg2)
{
    if (Arg0 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglBlendFunc(Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglBlendFuncSeparatei(GLuint Arg0, GLenum Arg1, GLenum Arg2, GLenum Arg3, GLenum Arg4)
{
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    VirtGpuOglBlendFunci(Arg0, Arg1, Arg2);
}

static void APIENTRY
VirtGpuOglDrawArraysIndirect(GLenum Arg0, const void * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    const VIRTGPU_OGL_DRAW_ARRAYS_INDIRECT_COMMAND *Command;

    if (Context == NULL)
        return;

    Command = VirtGpuOglIndirectCommandPointer(Context, Arg1, sizeof(*Command));
    if (Command == NULL)
        return;

    if (Command->Count > (GLuint)0x7fffffff)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglDrawArraysInstanced(Arg0,
                                  (GLint)Command->First,
                                  (GLsizei)Command->Count,
                                  (GLsizei)Command->PrimCount);
}

static void APIENTRY
VirtGpuOglDrawElementsIndirect(GLenum Arg0, GLenum Arg1, const void * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    const VIRTGPU_OGL_DRAW_ELEMENTS_INDIRECT_COMMAND *Command;
    const BYTE *Indices;
    ULONG ElementSize;
    ULONGLONG IndexOffset;

    if (Context == NULL)
        return;

    Command = VirtGpuOglIndirectCommandPointer(Context, Arg2, sizeof(*Command));
    if (Command == NULL)
        return;

    if (Command->Count > (GLuint)0x7fffffff)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglElementTypeSize(Arg1, &ElementSize))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    IndexOffset = (ULONGLONG)Command->FirstIndex * ElementSize;
    if (IndexOffset > (ULONGLONG)(SIZE_T)-1)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Indices = (const BYTE *)(ULONG_PTR)IndexOffset;
    VirtGpuOglDrawElementsInstancedBaseVertex(Arg0,
                                              (GLsizei)Command->Count,
                                              Arg1,
                                              Indices,
                                              (GLsizei)Command->PrimCount,
                                              (GLint)Command->BaseVertex);
}

static void APIENTRY
VirtGpuOglUniform1d(GLint Arg0, GLdouble Arg1)
{
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE, 1, 1, &Arg1);
}

static void APIENTRY
VirtGpuOglUniform2d(GLint Arg0, GLdouble Arg1, GLdouble Arg2)
{
    GLdouble Values[2];

    Values[0] = Arg1;
    Values[1] = Arg2;
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_VEC2, 1, 2, Values);
}

static void APIENTRY
VirtGpuOglUniform3d(GLint Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    GLdouble Values[3];

    Values[0] = Arg1;
    Values[1] = Arg2;
    Values[2] = Arg3;
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_VEC3, 1, 3, Values);
}

static void APIENTRY
VirtGpuOglUniform4d(GLint Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4)
{
    GLdouble Values[4];

    Values[0] = Arg1;
    Values[1] = Arg2;
    Values[2] = Arg3;
    Values[3] = Arg4;
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_VEC4, 1, 4, Values);
}

static void APIENTRY
VirtGpuOglUniform1dv(GLint Arg0, GLsizei Arg1, const GLdouble * Arg2)
{
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE, Arg1, 1, Arg2);
}

static void APIENTRY
VirtGpuOglUniform2dv(GLint Arg0, GLsizei Arg1, const GLdouble * Arg2)
{
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_VEC2, Arg1, 2, Arg2);
}

static void APIENTRY
VirtGpuOglUniform3dv(GLint Arg0, GLsizei Arg1, const GLdouble * Arg2)
{
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_VEC3, Arg1, 3, Arg2);
}

static void APIENTRY
VirtGpuOglUniform4dv(GLint Arg0, GLsizei Arg1, const GLdouble * Arg2)
{
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_VEC4, Arg1, 4, Arg2);
}

static void APIENTRY
VirtGpuOglUniformMatrix2dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_MAT2, Arg1, 4, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix3dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_MAT3, Arg1, 9, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix4dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_MAT4, Arg1, 16, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix2x3dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_MAT2x3, Arg1, 6, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix2x4dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_MAT2x4, Arg1, 8, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix3x2dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_MAT3x2, Arg1, 6, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix3x4dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_MAT3x4, Arg1, 12, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix4x2dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_MAT4x2, Arg1, 8, Arg3);
}

static void APIENTRY
VirtGpuOglUniformMatrix4x3dv(GLint Arg0, GLsizei Arg1, GLboolean Arg2, const GLdouble * Arg3)
{
    UNREFERENCED_PARAMETER(Arg2);
    VirtGpuOglStoreUniformDouble(Arg0, GL_DOUBLE_MAT4x3, Arg1, 12, Arg3);
}

static void APIENTRY
VirtGpuOglGetUniformdv(GLuint Arg0, GLint Arg1, GLdouble * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_PROGRAM Program;
    PVIRTGPU_OGL_UNIFORM Uniform;
    ULONG ValueCount;
    ULONG Index;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Program = VirtGpuOglFindProgram(Context, Arg0);
    if ((Program == NULL) || !Program->Linked)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Uniform = VirtGpuOglFindUniformByLocation(Program, Arg1);
    if (Uniform == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    ValueCount = VirtGpuOglUniformComponentCount(Uniform->Type) * (ULONG)Uniform->Size;
    if (ValueCount > VIRTGPU_OGL_MAX_UNIFORM_VALUES)
        ValueCount = VIRTGPU_OGL_MAX_UNIFORM_VALUES;

    for (Index = 0; Index < ValueCount; ++Index)
        Arg2[Index] = (GLdouble)Uniform->FloatValues[Index];
}

static GLint APIENTRY
VirtGpuOglGetSubroutineUniformLocation(GLuint Arg0, GLenum Arg1, const GLchar * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((VirtGpuOglFindProgram(Context, Arg0) == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return -1;
    }

    if (!VirtGpuOglShaderTypeValid(Arg1))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return -1;
    }

    return -1;
}

static GLuint APIENTRY
VirtGpuOglGetSubroutineIndex(GLuint Arg0, GLenum Arg1, const GLchar * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((VirtGpuOglFindProgram(Context, Arg0) == NULL) || (Arg2 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return GL_INVALID_INDEX;
    }

    if (!VirtGpuOglShaderTypeValid(Arg1))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return GL_INVALID_INDEX;
    }

    return GL_INVALID_INDEX;
}

static void APIENTRY
VirtGpuOglGetActiveSubroutineUniformiv(GLuint Arg0, GLenum Arg1, GLuint Arg2, GLenum Arg3, GLint * Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    UNREFERENCED_PARAMETER(Arg2);

    if ((VirtGpuOglFindProgram(Context, Arg0) == NULL) || (Arg4 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglShaderTypeValid(Arg1))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    switch (Arg3)
    {
        case GL_NUM_COMPATIBLE_SUBROUTINES:
            *Arg4 = 0;
            break;
        case GL_COMPATIBLE_SUBROUTINES:
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetActiveSubroutineUniformName(GLuint Arg0, GLenum Arg1, GLuint Arg2, GLsizei Arg3, GLsizei * Arg4, GLchar * Arg5)
{
    UNREFERENCED_PARAMETER(Arg2);
    if ((VirtGpuOglFindProgram(VirtGpuOglCurrentContext(), Arg0) == NULL) ||
        !VirtGpuOglShaderTypeValid(Arg1) ||
        (Arg3 < 0))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(),
                           !VirtGpuOglShaderTypeValid(Arg1) ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglCopyNameResult("", Arg3, Arg4, Arg5);
}

static void APIENTRY
VirtGpuOglGetActiveSubroutineName(GLuint Arg0, GLenum Arg1, GLuint Arg2, GLsizei Arg3, GLsizei * Arg4, GLchar * Arg5)
{
    UNREFERENCED_PARAMETER(Arg2);
    if ((VirtGpuOglFindProgram(VirtGpuOglCurrentContext(), Arg0) == NULL) ||
        !VirtGpuOglShaderTypeValid(Arg1) ||
        (Arg3 < 0))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(),
                           !VirtGpuOglShaderTypeValid(Arg1) ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglCopyNameResult("", Arg3, Arg4, Arg5);
}

static void APIENTRY
VirtGpuOglUniformSubroutinesuiv(GLenum Arg0, GLsizei Arg1, const GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg2);

    if (!VirtGpuOglShaderTypeValid(Arg0))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
        return;
    }

    if (Arg1 != 0)
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglGetUniformSubroutineuiv(GLenum Arg0, GLint Arg1, GLuint * Arg2)
{
    UNREFERENCED_PARAMETER(Arg1);

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglShaderTypeValid(Arg0))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
        return;
    }

    *Arg2 = 0;
}

static void APIENTRY
VirtGpuOglGetProgramStageiv(GLuint Arg0, GLenum Arg1, GLenum Arg2, GLint * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((VirtGpuOglFindProgram(Context, Arg0) == NULL) || (Arg3 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglShaderTypeValid(Arg1))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    switch (Arg2)
    {
        case GL_ACTIVE_SUBROUTINES:
        case GL_ACTIVE_SUBROUTINE_UNIFORMS:
        case GL_ACTIVE_SUBROUTINE_UNIFORM_LOCATIONS:
            *Arg3 = 0;
            break;
        case GL_ACTIVE_SUBROUTINE_MAX_LENGTH:
        case GL_ACTIVE_SUBROUTINE_UNIFORM_MAX_LENGTH:
            *Arg3 = 1;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglPatchParameteri(GLenum Arg0, GLint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (Arg0 != GL_PATCH_VERTICES)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Arg1 <= 0) || (Arg1 > VIRTGPU_OGL_MAX_PATCH_VERTICES))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->PatchVertices = Arg1;
}

static void APIENTRY
VirtGpuOglPatchParameterfv(GLenum Arg0, const GLfloat * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg1 == NULL))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg0)
    {
        case GL_PATCH_DEFAULT_OUTER_LEVEL:
            CopyMemory(Context->PatchDefaultOuterLevel, Arg1, sizeof(Context->PatchDefaultOuterLevel));
            break;
        case GL_PATCH_DEFAULT_INNER_LEVEL:
            CopyMemory(Context->PatchDefaultInnerLevel, Arg1, sizeof(Context->PatchDefaultInnerLevel));
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglBindTransformFeedback(GLenum Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TRANSFORM_FEEDBACK TransformFeedback;

    if (Context == NULL)
        return;

    if (Arg0 != GL_TRANSFORM_FEEDBACK)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    TransformFeedback = VirtGpuOglFindTransformFeedback(Context, Context->BoundTransformFeedback);
    if (((TransformFeedback != NULL) && TransformFeedback->Active) ||
        ((TransformFeedback == NULL) && Context->DefaultTransformFeedbackActive))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    if ((Arg1 != 0) && (VirtGpuOglFindTransformFeedback(Context, Arg1) == NULL))
    {
        if (VirtGpuOglAllocateTransformFeedbackName(Context, Arg1) == NULL)
            return;
    }

    Context->BoundTransformFeedback = Arg1;
}

static void APIENTRY
VirtGpuOglDeleteTransformFeedbacks(GLsizei Arg0, const GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    PVIRTGPU_OGL_TRANSFORM_FEEDBACK TransformFeedback;

    if (Context == NULL)
        return;

    if ((Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        TransformFeedback = VirtGpuOglFindTransformFeedback(Context, Arg1[Index]);
        if (TransformFeedback == NULL)
            continue;
        if (TransformFeedback->Active)
        {
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
            return;
        }
        if (Context->BoundTransformFeedback == TransformFeedback->Name)
            Context->BoundTransformFeedback = 0;
        ZeroMemory(TransformFeedback, sizeof(*TransformFeedback));
    }
}

static void APIENTRY
VirtGpuOglGenTransformFeedbacks(GLsizei Arg0, GLuint * Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLsizei Index;
    GLuint Name;

    if ((Context == NULL) || (Arg0 < 0) || ((Arg0 > 0) && (Arg1 == NULL)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < Arg0; ++Index)
    {
        do
        {
            Name = Context->NextTransformFeedbackName++;
            if (Context->NextTransformFeedbackName == 0)
                Context->NextTransformFeedbackName = 1;
        } while ((Name == 0) || (VirtGpuOglFindTransformFeedback(Context, Name) != NULL));

        if (VirtGpuOglAllocateTransformFeedbackName(Context, Name) == NULL)
            return;
        Arg1[Index] = Name;
    }
}

static GLboolean APIENTRY
VirtGpuOglIsTransformFeedback(GLuint Arg0)
{
    return (VirtGpuOglFindTransformFeedback(VirtGpuOglCurrentContext(), Arg0) != NULL) ?
           GL_TRUE : GL_FALSE;
}

static void APIENTRY
VirtGpuOglPauseTransformFeedback(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TRANSFORM_FEEDBACK TransformFeedback;

    if (Context == NULL)
        return;

    TransformFeedback = VirtGpuOglFindTransformFeedback(Context, Context->BoundTransformFeedback);
    if (TransformFeedback != NULL)
    {
        if (!TransformFeedback->Active || TransformFeedback->Paused)
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        else
            TransformFeedback->Paused = TRUE;
        return;
    }

    if (!Context->DefaultTransformFeedbackActive || Context->DefaultTransformFeedbackPaused)
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
    else
        Context->DefaultTransformFeedbackPaused = TRUE;
}

static void APIENTRY
VirtGpuOglResumeTransformFeedback(VOID)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    PVIRTGPU_OGL_TRANSFORM_FEEDBACK TransformFeedback;

    if (Context == NULL)
        return;

    TransformFeedback = VirtGpuOglFindTransformFeedback(Context, Context->BoundTransformFeedback);
    if (TransformFeedback != NULL)
    {
        if (!TransformFeedback->Active || !TransformFeedback->Paused)
            VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        else
            TransformFeedback->Paused = FALSE;
        return;
    }

    if (!Context->DefaultTransformFeedbackActive || !Context->DefaultTransformFeedbackPaused)
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
    else
        Context->DefaultTransformFeedbackPaused = FALSE;
}

static void APIENTRY
VirtGpuOglDrawTransformFeedback(GLenum Arg0, GLuint Arg1)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (!VirtGpuOglTransformFeedbackModeValid(Arg0))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Arg1 != 0) && (VirtGpuOglFindTransformFeedback(Context, Arg1) == NULL))
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglDrawTransformFeedbackStream(GLenum Arg0, GLuint Arg1, GLuint Arg2)
{
    if (Arg2 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglDrawTransformFeedback(Arg0, Arg1);
}

static void APIENTRY
VirtGpuOglBeginQueryIndexed(GLenum Arg0, GLuint Arg1, GLuint Arg2)
{
    if (Arg1 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglBeginQuery(Arg0, Arg2);
}

static void APIENTRY
VirtGpuOglEndQueryIndexed(GLenum Arg0, GLuint Arg1)
{
    if (Arg1 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglEndQuery(Arg0);
}

static void APIENTRY
VirtGpuOglGetQueryIndexediv(GLenum Arg0, GLuint Arg1, GLenum Arg2, GLint * Arg3)
{
    if (Arg1 != 0)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetQueryiv(Arg0, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglColorTable(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLenum Arg3, GLenum Arg4, const GLvoid * Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if ((Context == NULL) || !VirtGpuOglColorTableTargetToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    (VOID)VirtGpuOglStoreImageTable(Context,
                                    &Context->ColorTables[Index],
                                    Arg0,
                                    Arg1,
                                    Arg2,
                                    1,
                                    Arg3,
                                    Arg4,
                                    Arg5);
}

static void APIENTRY
VirtGpuOglColorTableParameterfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    PVIRTGPU_OGL_IMAGE_TABLE Table;

    if ((Context == NULL) ||
        (Arg2 == NULL) ||
        !VirtGpuOglColorTableTargetToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, (Arg2 == NULL) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Table = &Context->ColorTables[Index];
    switch (Arg1)
    {
        case GL_COLOR_TABLE_SCALE:
            CopyMemory(Table->Scale, Arg2, sizeof(Table->Scale));
            break;
        case GL_COLOR_TABLE_BIAS:
            CopyMemory(Table->Bias, Arg2, sizeof(Table->Bias));
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglColorTableParameteriv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    GLfloat Values[4];
    ULONG Index;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < 4; ++Index)
        Values[Index] = (GLfloat)Arg2[Index];
    VirtGpuOglColorTableParameterfv(Arg0, Arg1, Values);
}

static void APIENTRY
VirtGpuOglCopyColorTable(GLenum Arg0, GLenum Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONG Size;

    if (Context == NULL)
        return;

    Pixels = VirtGpuOglReadRgbaPixels(Context, Arg2, Arg3, Arg4, 1, &Size);
    UNREFERENCED_PARAMETER(Size);
    if (Pixels == NULL)
        return;

    VirtGpuOglColorTable(Arg0, Arg1, Arg4, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    HeapFree(GetProcessHeap(), 0, Pixels);
}

static void APIENTRY
VirtGpuOglGetColorTable(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLvoid * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if ((Context == NULL) || !VirtGpuOglColorTableTargetToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    VirtGpuOglGetImageTableData(Context, &Context->ColorTables[Index], Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglGetColorTableParameterfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLint Value;
    ULONG Index;
    PVIRTGPU_OGL_IMAGE_TABLE Table;
    GLint RedBits;
    GLint GreenBits;
    GLint BlueBits;
    GLint AlphaBits;
    GLint DepthBits;
    GLint StencilBits;

    if ((Context == NULL) ||
        (Arg2 == NULL) ||
        !VirtGpuOglColorTableTargetToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, (Arg2 == NULL) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Table = &Context->ColorTables[Index];
    switch (Arg1)
    {
        case GL_COLOR_TABLE_SCALE:
            CopyMemory(Arg2, Table->Scale, sizeof(Table->Scale));
            return;
        case GL_COLOR_TABLE_BIAS:
            CopyMemory(Arg2, Table->Bias, sizeof(Table->Bias));
            return;
        case GL_COLOR_TABLE_FORMAT:
            Value = (GLint)Table->InternalFormat;
            break;
        case GL_COLOR_TABLE_WIDTH:
            Value = Table->Width;
            break;
        case GL_COLOR_TABLE_RED_SIZE:
        case GL_COLOR_TABLE_GREEN_SIZE:
        case GL_COLOR_TABLE_BLUE_SIZE:
        case GL_COLOR_TABLE_ALPHA_SIZE:
        case GL_COLOR_TABLE_LUMINANCE_SIZE:
        case GL_COLOR_TABLE_INTENSITY_SIZE:
            VirtGpuOglFormatComponentBits(Table->InternalFormat,
                                          &RedBits,
                                          &GreenBits,
                                          &BlueBits,
                                          &AlphaBits,
                                          &DepthBits,
                                          &StencilBits);
            UNREFERENCED_PARAMETER(DepthBits);
            UNREFERENCED_PARAMETER(StencilBits);
            Value = (Arg1 == GL_COLOR_TABLE_RED_SIZE) ? RedBits :
                    (Arg1 == GL_COLOR_TABLE_GREEN_SIZE) ? GreenBits :
                    (Arg1 == GL_COLOR_TABLE_BLUE_SIZE) ? BlueBits :
                    (Arg1 == GL_COLOR_TABLE_ALPHA_SIZE) ? AlphaBits : 0;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            return;
    }

    Arg2[0] = (GLfloat)Value;
}

static void APIENTRY
VirtGpuOglGetColorTableParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    GLfloat Values[4] = { 0.0f };
    ULONG Index;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetColorTableParameterfv(Arg0, Arg1, Values);
    if ((Arg1 != GL_COLOR_TABLE_SCALE) && (Arg1 != GL_COLOR_TABLE_BIAS))
    {
        Arg2[0] = (GLint)Values[0];
        return;
    }

    for (Index = 0; Index < 4; ++Index)
        Arg2[Index] = (GLint)Values[Index];
}

static void APIENTRY
VirtGpuOglColorSubTable(GLenum Arg0, GLsizei Arg1, GLsizei Arg2, GLenum Arg3, GLenum Arg4, const GLvoid * Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    ULONG BytesPerPixel;
    PVIRTGPU_OGL_IMAGE_TABLE Table;

    if ((Context == NULL) || !VirtGpuOglColorTableTargetToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Table = &Context->ColorTables[Index];
    if ((Arg1 < 0) ||
        (Arg2 < 0) ||
        (Arg1 > Table->Width) ||
        (Arg2 > Table->Width - Arg1))
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglTextureFormatBytes(Arg3, Arg4, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if ((Arg2 == 0) || (Arg5 == NULL) || (Table->Data == NULL))
        return;

    if ((Table->Format != Arg3) || (Table->Type != Arg4))
    {
        VirtGpuOglSetError(Context, GL_INVALID_OPERATION);
        return;
    }

    CopyMemory(Table->Data + ((ULONG)Arg1 * BytesPerPixel),
               Arg5,
               (ULONG)Arg2 * BytesPerPixel);
}

static void APIENTRY
VirtGpuOglCopyColorSubTable(GLenum Arg0, GLsizei Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONG Size;

    if (Context == NULL)
        return;

    Pixels = VirtGpuOglReadRgbaPixels(Context, Arg2, Arg3, Arg4, 1, &Size);
    UNREFERENCED_PARAMETER(Size);
    if (Pixels == NULL)
        return;

    VirtGpuOglColorSubTable(Arg0, Arg1, Arg4, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    HeapFree(GetProcessHeap(), 0, Pixels);
}

static void APIENTRY
VirtGpuOglConvolutionFilter1D(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLenum Arg3, GLenum Arg4, const GLvoid * Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if ((Context == NULL) ||
        !VirtGpuOglConvolutionTargetToIndex(Arg0, &Index) ||
        (Arg0 == GL_CONVOLUTION_2D))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    (VOID)VirtGpuOglStoreImageTable(Context,
                                    &Context->ConvolutionFilters[Index],
                                    Arg0,
                                    Arg1,
                                    Arg2,
                                    1,
                                    Arg3,
                                    Arg4,
                                    Arg5);
}

static void APIENTRY
VirtGpuOglConvolutionFilter2D(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLsizei Arg3, GLenum Arg4, GLenum Arg5, const GLvoid * Arg6)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if ((Context == NULL) ||
        !VirtGpuOglConvolutionTargetToIndex(Arg0, &Index) ||
        (Arg0 == GL_CONVOLUTION_1D))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    (VOID)VirtGpuOglStoreImageTable(Context,
                                    &Context->ConvolutionFilters[Index],
                                    Arg0,
                                    Arg1,
                                    Arg2,
                                    Arg3,
                                    Arg4,
                                    Arg5,
                                    Arg6);
}

static void APIENTRY
VirtGpuOglConvolutionParameterf(GLenum Arg0, GLenum Arg1, GLfloat Arg2)
{
    GLfloat Values[4];

    Values[0] = Arg2;
    Values[1] = Arg2;
    Values[2] = Arg2;
    Values[3] = Arg2;
    VirtGpuOglConvolutionParameterfv(Arg0, Arg1, Values);
}

static void APIENTRY
VirtGpuOglConvolutionParameterfv(GLenum Arg0, GLenum Arg1, const GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    PVIRTGPU_OGL_IMAGE_TABLE Table;

    if ((Context == NULL) ||
        (Arg2 == NULL) ||
        !VirtGpuOglConvolutionTargetToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, (Arg2 == NULL) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Table = &Context->ConvolutionFilters[Index];
    switch (Arg1)
    {
        case GL_CONVOLUTION_FILTER_SCALE:
            CopyMemory(Table->Scale, Arg2, sizeof(Table->Scale));
            break;
        case GL_CONVOLUTION_FILTER_BIAS:
            CopyMemory(Table->Bias, Arg2, sizeof(Table->Bias));
            break;
        case GL_CONVOLUTION_BORDER_COLOR:
            CopyMemory(Table->BorderColor, Arg2, sizeof(Table->BorderColor));
            break;
        case GL_CONVOLUTION_BORDER_MODE:
            Table->BorderMode = (GLenum)Arg2[0];
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglConvolutionParameteri(GLenum Arg0, GLenum Arg1, GLint Arg2)
{
    GLfloat Values[4];

    Values[0] = (GLfloat)Arg2;
    Values[1] = (GLfloat)Arg2;
    Values[2] = (GLfloat)Arg2;
    Values[3] = (GLfloat)Arg2;
    VirtGpuOglConvolutionParameterfv(Arg0, Arg1, Values);
}

static void APIENTRY
VirtGpuOglConvolutionParameteriv(GLenum Arg0, GLenum Arg1, const GLint * Arg2)
{
    GLfloat Values[4];
    ULONG Index;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Index = 0; Index < 4; ++Index)
        Values[Index] = (GLfloat)Arg2[Index];
    VirtGpuOglConvolutionParameterfv(Arg0, Arg1, Values);
}

static void APIENTRY
VirtGpuOglCopyConvolutionFilter1D(GLenum Arg0, GLenum Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONG Size;

    if (Context == NULL)
        return;

    Pixels = VirtGpuOglReadRgbaPixels(Context, Arg2, Arg3, Arg4, 1, &Size);
    UNREFERENCED_PARAMETER(Size);
    if (Pixels == NULL)
        return;

    VirtGpuOglConvolutionFilter1D(Arg0, Arg1, Arg4, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    HeapFree(GetProcessHeap(), 0, Pixels);
}

static void APIENTRY
VirtGpuOglCopyConvolutionFilter2D(GLenum Arg0, GLenum Arg1, GLint Arg2, GLint Arg3, GLsizei Arg4, GLsizei Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    BYTE *Pixels;
    ULONG Size;

    if (Context == NULL)
        return;

    Pixels = VirtGpuOglReadRgbaPixels(Context, Arg2, Arg3, Arg4, Arg5, &Size);
    UNREFERENCED_PARAMETER(Size);
    if (Pixels == NULL)
        return;

    VirtGpuOglConvolutionFilter2D(Arg0, Arg1, Arg4, Arg5, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);
    HeapFree(GetProcessHeap(), 0, Pixels);
}

static void APIENTRY
VirtGpuOglGetConvolutionFilter(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLvoid * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if ((Context == NULL) || !VirtGpuOglConvolutionTargetToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    VirtGpuOglGetImageTableData(Context, &Context->ConvolutionFilters[Index], Arg1, Arg2, Arg3);
}

static void APIENTRY
VirtGpuOglGetConvolutionParameterfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;
    PVIRTGPU_OGL_IMAGE_TABLE Table;

    if ((Context == NULL) ||
        (Arg2 == NULL) ||
        !VirtGpuOglConvolutionTargetToIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, (Arg2 == NULL) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Table = &Context->ConvolutionFilters[Index];
    switch (Arg1)
    {
        case GL_CONVOLUTION_FILTER_SCALE:
            CopyMemory(Arg2, Table->Scale, sizeof(Table->Scale));
            break;
        case GL_CONVOLUTION_FILTER_BIAS:
            CopyMemory(Arg2, Table->Bias, sizeof(Table->Bias));
            break;
        case GL_CONVOLUTION_BORDER_COLOR:
            CopyMemory(Arg2, Table->BorderColor, sizeof(Table->BorderColor));
            break;
        case GL_CONVOLUTION_BORDER_MODE:
            Arg2[0] = (GLfloat)Table->BorderMode;
            break;
        case GL_CONVOLUTION_FORMAT:
            Arg2[0] = (GLfloat)Table->InternalFormat;
            break;
        case GL_CONVOLUTION_WIDTH:
            Arg2[0] = (GLfloat)Table->Width;
            break;
        case GL_CONVOLUTION_HEIGHT:
            Arg2[0] = (GLfloat)Table->Height;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetConvolutionParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    GLfloat Values[4] = { 0.0f };
    ULONG Index;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetConvolutionParameterfv(Arg0, Arg1, Values);
    if ((Arg1 != GL_CONVOLUTION_FILTER_SCALE) &&
        (Arg1 != GL_CONVOLUTION_FILTER_BIAS) &&
        (Arg1 != GL_CONVOLUTION_BORDER_COLOR))
    {
        Arg2[0] = (GLint)Values[0];
        return;
    }

    for (Index = 0; Index < 4; ++Index)
        Arg2[Index] = (GLint)Values[Index];
}

static void APIENTRY
VirtGpuOglGetSeparableFilter(GLenum Arg0, GLenum Arg1, GLenum Arg2, GLvoid * Arg3, GLvoid * Arg4, GLvoid * Arg5)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG BytesPerPixel;
    ULONG Index;
    PVIRTGPU_OGL_IMAGE_TABLE Table;

    UNREFERENCED_PARAMETER(Arg5);

    if ((Context == NULL) ||
        !VirtGpuOglConvolutionTargetToIndex(Arg0, &Index) ||
        (Arg0 != GL_SEPARABLE_2D))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (!VirtGpuOglTextureFormatBytes(Arg1, Arg2, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Table = &Context->ConvolutionFilters[Index];
    if ((Arg3 != NULL) && (Table->Width > 0))
        ZeroMemory(Arg3, (ULONG)Table->Width * BytesPerPixel);
    if ((Arg4 != NULL) && (Table->Height > 0))
        ZeroMemory(Arg4, (ULONG)Table->Height * BytesPerPixel);
}

static void APIENTRY
VirtGpuOglSeparableFilter2D(GLenum Arg0, GLenum Arg1, GLsizei Arg2, GLsizei Arg3, GLenum Arg4, GLenum Arg5, const GLvoid * Arg6, const GLvoid * Arg7)
{
    UNREFERENCED_PARAMETER(Arg7);

    VirtGpuOglConvolutionFilter2D(Arg0, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6);
}

static void APIENTRY
VirtGpuOglGetHistogram(GLenum Arg0, GLboolean Arg1, GLenum Arg2, GLenum Arg3, GLvoid * Arg4)
{
    UNREFERENCED_PARAMETER(Arg1);
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG BytesPerPixel;

    if ((Context == NULL) || (Arg0 != GL_HISTOGRAM))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Arg4 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglTextureFormatBytes(Arg2, Arg3, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Context->Histogram.Width > 0)
        ZeroMemory(Arg4, (ULONG)Context->Histogram.Width * BytesPerPixel);
}

static void APIENTRY
VirtGpuOglGetHistogramParameterfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    GLint Value = 0;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetHistogramParameteriv(Arg0, Arg1, &Value);
    Arg2[0] = (GLfloat)Value;
}

static void APIENTRY
VirtGpuOglGetHistogramParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    GLint RedBits;
    GLint GreenBits;
    GLint BlueBits;
    GLint AlphaBits;
    GLint DepthBits;
    GLint StencilBits;

    if ((Context == NULL) || (Arg0 != GL_HISTOGRAM))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_HISTOGRAM_WIDTH:
            *Arg2 = Context->Histogram.Width;
            break;
        case GL_HISTOGRAM_FORMAT:
            *Arg2 = (GLint)Context->Histogram.InternalFormat;
            break;
        case GL_HISTOGRAM_SINK:
            *Arg2 = Context->Histogram.Sink;
            break;
        case GL_HISTOGRAM_RED_SIZE:
        case GL_HISTOGRAM_GREEN_SIZE:
        case GL_HISTOGRAM_BLUE_SIZE:
        case GL_HISTOGRAM_ALPHA_SIZE:
        case GL_HISTOGRAM_LUMINANCE_SIZE:
            VirtGpuOglFormatComponentBits(Context->Histogram.InternalFormat,
                                          &RedBits,
                                          &GreenBits,
                                          &BlueBits,
                                          &AlphaBits,
                                          &DepthBits,
                                          &StencilBits);
            UNREFERENCED_PARAMETER(DepthBits);
            UNREFERENCED_PARAMETER(StencilBits);
            *Arg2 = (Arg1 == GL_HISTOGRAM_RED_SIZE) ? RedBits :
                    (Arg1 == GL_HISTOGRAM_GREEN_SIZE) ? GreenBits :
                    (Arg1 == GL_HISTOGRAM_BLUE_SIZE) ? BlueBits :
                    (Arg1 == GL_HISTOGRAM_ALPHA_SIZE) ? AlphaBits : 0;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglGetMinmax(GLenum Arg0, GLboolean Arg1, GLenum Arg2, GLenum Arg3, GLvoid * Arg4)
{
    UNREFERENCED_PARAMETER(Arg1);
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG BytesPerPixel;

    if ((Context == NULL) || (Arg0 != GL_MINMAX))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Arg4 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    if (!VirtGpuOglTextureFormatBytes(Arg2, Arg3, &BytesPerPixel))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    ZeroMemory(Arg4, 2 * BytesPerPixel);
}

static void APIENTRY
VirtGpuOglGetMinmaxParameterfv(GLenum Arg0, GLenum Arg1, GLfloat * Arg2)
{
    GLint Value = 0;

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    VirtGpuOglGetMinmaxParameteriv(Arg0, Arg1, &Value);
    Arg2[0] = (GLfloat)Value;
}

static void APIENTRY
VirtGpuOglGetMinmaxParameteriv(GLenum Arg0, GLenum Arg1, GLint * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg0 != GL_MINMAX))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Arg2 == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    switch (Arg1)
    {
        case GL_MINMAX_FORMAT:
            *Arg2 = (GLint)Context->Minmax.InternalFormat;
            break;
        case GL_MINMAX_SINK:
            *Arg2 = Context->Minmax.Sink;
            break;
        default:
            VirtGpuOglSetError(Context, GL_INVALID_ENUM);
            break;
    }
}

static void APIENTRY
VirtGpuOglHistogram(GLenum Arg0, GLsizei Arg1, GLenum Arg2, GLboolean Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || ((Arg0 != GL_HISTOGRAM) && (Arg0 != GL_PROXY_HISTOGRAM)))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    if (Arg1 < 0)
    {
        VirtGpuOglSetError(Context, GL_INVALID_VALUE);
        return;
    }

    Context->Histogram.Defined = TRUE;
    Context->Histogram.Target = Arg0;
    Context->Histogram.Width = Arg1;
    Context->Histogram.InternalFormat = Arg2;
    Context->Histogram.Sink = Arg3;
}

static void APIENTRY
VirtGpuOglMinmax(GLenum Arg0, GLenum Arg1, GLboolean Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if ((Context == NULL) || (Arg0 != GL_MINMAX))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Context->Minmax.Defined = TRUE;
    Context->Minmax.Target = Arg0;
    Context->Minmax.InternalFormat = Arg1;
    Context->Minmax.Sink = Arg2;
}

static void APIENTRY
VirtGpuOglResetHistogram(GLenum Arg0)
{
    if (Arg0 != GL_HISTOGRAM)
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
}

static void APIENTRY
VirtGpuOglResetMinmax(GLenum Arg0)
{
    if (Arg0 != GL_MINMAX)
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
}

static void APIENTRY
VirtGpuOglClientActiveTexture(GLenum Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();
    ULONG Index;

    if (Context == NULL)
        return;

    if (!VirtGpuOglTextureUnitIndex(Arg0, &Index))
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return;
    }

    Context->ClientActiveTexture = Arg0;
}

static BOOL
VirtGpuOglValidateMultiTexCoordUnit(_In_ GLenum Unit)
{
    ULONG Index;

    if (!VirtGpuOglTextureUnitIndex(Unit, &Index))
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_ENUM);
        return FALSE;
    }

    return TRUE;
}

static VOID
VirtGpuOglMultiTexCoord4fSet(
    _In_ GLenum Unit,
    _In_ GLfloat S,
    _In_ GLfloat T,
    _In_ GLfloat R,
    _In_ GLfloat Q)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (!VirtGpuOglValidateMultiTexCoordUnit(Unit) || (Context == NULL))
        return;

    if (Unit == Context->ActiveTexture)
        VirtGpuOglTexCoord4f(S, T, R, Q);
}

static void APIENTRY
VirtGpuOglMultiTexCoord1d(GLenum Arg0, GLdouble Arg1)
{
    VirtGpuOglMultiTexCoord4fSet(Arg0, (GLfloat)Arg1, 0.0f, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglMultiTexCoord1dv(GLenum Arg0, const GLdouble * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord1d(Arg0, Arg1[0]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord1f(GLenum Arg0, GLfloat Arg1)
{
    VirtGpuOglMultiTexCoord4fSet(Arg0, Arg1, 0.0f, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglMultiTexCoord1fv(GLenum Arg0, const GLfloat * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord1f(Arg0, Arg1[0]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord1i(GLenum Arg0, GLint Arg1)
{
    VirtGpuOglMultiTexCoord1f(Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglMultiTexCoord1iv(GLenum Arg0, const GLint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord1i(Arg0, Arg1[0]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord1s(GLenum Arg0, GLshort Arg1)
{
    VirtGpuOglMultiTexCoord1f(Arg0, (GLfloat)Arg1);
}

static void APIENTRY
VirtGpuOglMultiTexCoord1sv(GLenum Arg0, const GLshort * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord1s(Arg0, Arg1[0]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord2d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2)
{
    VirtGpuOglMultiTexCoord4fSet(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglMultiTexCoord2dv(GLenum Arg0, const GLdouble * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord2d(Arg0, Arg1[0], Arg1[1]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord2f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2)
{
    VirtGpuOglMultiTexCoord4fSet(Arg0, Arg1, Arg2, 0.0f, 1.0f);
}

static void APIENTRY
VirtGpuOglMultiTexCoord2fv(GLenum Arg0, const GLfloat * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord2f(Arg0, Arg1[0], Arg1[1]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord2i(GLenum Arg0, GLint Arg1, GLint Arg2)
{
    VirtGpuOglMultiTexCoord2f(Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglMultiTexCoord2iv(GLenum Arg0, const GLint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord2i(Arg0, Arg1[0], Arg1[1]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord2s(GLenum Arg0, GLshort Arg1, GLshort Arg2)
{
    VirtGpuOglMultiTexCoord2f(Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglMultiTexCoord2sv(GLenum Arg0, const GLshort * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord2s(Arg0, Arg1[0], Arg1[1]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord3d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3)
{
    VirtGpuOglMultiTexCoord4fSet(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, 1.0f);
}

static void APIENTRY
VirtGpuOglMultiTexCoord3dv(GLenum Arg0, const GLdouble * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord3d(Arg0, Arg1[0], Arg1[1], Arg1[2]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord3f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3)
{
    VirtGpuOglMultiTexCoord4fSet(Arg0, Arg1, Arg2, Arg3, 1.0f);
}

static void APIENTRY
VirtGpuOglMultiTexCoord3fv(GLenum Arg0, const GLfloat * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord3f(Arg0, Arg1[0], Arg1[1], Arg1[2]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord3i(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3)
{
    VirtGpuOglMultiTexCoord3f(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglMultiTexCoord3iv(GLenum Arg0, const GLint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord3i(Arg0, Arg1[0], Arg1[1], Arg1[2]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord3s(GLenum Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3)
{
    VirtGpuOglMultiTexCoord3f(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3);
}

static void APIENTRY
VirtGpuOglMultiTexCoord3sv(GLenum Arg0, const GLshort * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord3s(Arg0, Arg1[0], Arg1[1], Arg1[2]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord4d(GLenum Arg0, GLdouble Arg1, GLdouble Arg2, GLdouble Arg3, GLdouble Arg4)
{
    VirtGpuOglMultiTexCoord4fSet(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, (GLfloat)Arg4);
}

static void APIENTRY
VirtGpuOglMultiTexCoord4dv(GLenum Arg0, const GLdouble * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord4d(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord4f(GLenum Arg0, GLfloat Arg1, GLfloat Arg2, GLfloat Arg3, GLfloat Arg4)
{
    VirtGpuOglMultiTexCoord4fSet(Arg0, Arg1, Arg2, Arg3, Arg4);
}

static void APIENTRY
VirtGpuOglMultiTexCoord4fv(GLenum Arg0, const GLfloat * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord4f(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord4i(GLenum Arg0, GLint Arg1, GLint Arg2, GLint Arg3, GLint Arg4)
{
    VirtGpuOglMultiTexCoord4f(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, (GLfloat)Arg4);
}

static void APIENTRY
VirtGpuOglMultiTexCoord4iv(GLenum Arg0, const GLint * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord4i(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglMultiTexCoord4s(GLenum Arg0, GLshort Arg1, GLshort Arg2, GLshort Arg3, GLshort Arg4)
{
    VirtGpuOglMultiTexCoord4f(Arg0, (GLfloat)Arg1, (GLfloat)Arg2, (GLfloat)Arg3, (GLfloat)Arg4);
}

static void APIENTRY
VirtGpuOglMultiTexCoord4sv(GLenum Arg0, const GLshort * Arg1)
{
    if (Arg1 != NULL)
        VirtGpuOglMultiTexCoord4s(Arg0, Arg1[0], Arg1[1], Arg1[2], Arg1[3]);
    else
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
}

static void APIENTRY
VirtGpuOglLoadTransposeMatrixf(const GLfloat * Arg0)
{
    GLfloat Matrix[16];
    ULONG Row;
    ULONG Column;

    if (Arg0 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Row = 0; Row < 4; ++Row)
        for (Column = 0; Column < 4; ++Column)
            Matrix[(Row * 4) + Column] = Arg0[(Column * 4) + Row];

    VirtGpuOglLoadMatrixf(Matrix);
}

static void APIENTRY
VirtGpuOglLoadTransposeMatrixd(const GLdouble * Arg0)
{
    GLdouble Matrix[16];
    ULONG Row;
    ULONG Column;

    if (Arg0 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Row = 0; Row < 4; ++Row)
        for (Column = 0; Column < 4; ++Column)
            Matrix[(Row * 4) + Column] = Arg0[(Column * 4) + Row];

    VirtGpuOglLoadMatrixd(Matrix);
}

static void APIENTRY
VirtGpuOglMultTransposeMatrixf(const GLfloat * Arg0)
{
    GLfloat Matrix[16];
    ULONG Row;
    ULONG Column;

    if (Arg0 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Row = 0; Row < 4; ++Row)
        for (Column = 0; Column < 4; ++Column)
            Matrix[(Row * 4) + Column] = Arg0[(Column * 4) + Row];

    VirtGpuOglMultMatrixf(Matrix);
}

static void APIENTRY
VirtGpuOglMultTransposeMatrixd(const GLdouble * Arg0)
{
    GLdouble Matrix[16];
    ULONG Row;
    ULONG Column;

    if (Arg0 == NULL)
    {
        VirtGpuOglSetError(VirtGpuOglCurrentContext(), GL_INVALID_VALUE);
        return;
    }

    for (Row = 0; Row < 4; ++Row)
        for (Column = 0; Column < 4; ++Column)
            Matrix[(Row * 4) + Column] = Arg0[(Column * 4) + Row];

    VirtGpuOglMultMatrixd(Matrix);
}

static void APIENTRY
VirtGpuOglFogCoordf(GLfloat Arg0)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context != NULL)
        Context->CurrentFogCoord = Arg0;
}

static void APIENTRY
VirtGpuOglFogCoordfv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglFogCoordf(Arg0[0]);
}

static void APIENTRY
VirtGpuOglFogCoordd(GLdouble Arg0)
{
    VirtGpuOglFogCoordf((GLfloat)Arg0);
}

static void APIENTRY
VirtGpuOglFogCoorddv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglFogCoordd(Arg0[0]);
}

static void APIENTRY
VirtGpuOglFogCoordPointer(GLenum Arg0, GLsizei Arg1, const GLvoid * Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if (((Arg0 != GL_FLOAT) && (Arg0 != GL_DOUBLE)) || (Arg1 < 0))
    {
        VirtGpuOglSetError(Context, (Arg1 < 0) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Context->FogCoordArrayType = Arg0;
    Context->FogCoordArrayStride = Arg1;
    Context->FogCoordArrayPointer = Arg2;
}

static void APIENTRY
VirtGpuOglSecondaryColor3b(GLbyte Arg0, GLbyte Arg1, GLbyte Arg2)
{
    VirtGpuOglSecondaryColor3f(VirtGpuOglColorFromByte(Arg0),
                               VirtGpuOglColorFromByte(Arg1),
                               VirtGpuOglColorFromByte(Arg2));
}

static void APIENTRY
VirtGpuOglSecondaryColor3bv(const GLbyte * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglSecondaryColor3b(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglSecondaryColor3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    VirtGpuOglSecondaryColor3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglSecondaryColor3dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglSecondaryColor3d(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglSecondaryColor3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    Context->CurrentSecondaryColor[0] = VirtGpuOglClampFloat(Arg0);
    Context->CurrentSecondaryColor[1] = VirtGpuOglClampFloat(Arg1);
    Context->CurrentSecondaryColor[2] = VirtGpuOglClampFloat(Arg2);
}

static void APIENTRY
VirtGpuOglSecondaryColor3fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglSecondaryColor3f(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglSecondaryColor3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    VirtGpuOglSecondaryColor3f(VirtGpuOglColorFromInt(Arg0),
                               VirtGpuOglColorFromInt(Arg1),
                               VirtGpuOglColorFromInt(Arg2));
}

static void APIENTRY
VirtGpuOglSecondaryColor3iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglSecondaryColor3i(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglSecondaryColor3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    VirtGpuOglSecondaryColor3f(VirtGpuOglColorFromShort(Arg0),
                               VirtGpuOglColorFromShort(Arg1),
                               VirtGpuOglColorFromShort(Arg2));
}

static void APIENTRY
VirtGpuOglSecondaryColor3sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglSecondaryColor3s(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglSecondaryColor3ub(GLubyte Arg0, GLubyte Arg1, GLubyte Arg2)
{
    VirtGpuOglSecondaryColor3f(VirtGpuOglColorFromUByte(Arg0),
                               VirtGpuOglColorFromUByte(Arg1),
                               VirtGpuOglColorFromUByte(Arg2));
}

static void APIENTRY
VirtGpuOglSecondaryColor3ubv(const GLubyte * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglSecondaryColor3ub(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglSecondaryColor3ui(GLuint Arg0, GLuint Arg1, GLuint Arg2)
{
    VirtGpuOglSecondaryColor3f(VirtGpuOglColorFromUInt(Arg0),
                               VirtGpuOglColorFromUInt(Arg1),
                               VirtGpuOglColorFromUInt(Arg2));
}

static void APIENTRY
VirtGpuOglSecondaryColor3uiv(const GLuint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglSecondaryColor3ui(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglSecondaryColor3us(GLushort Arg0, GLushort Arg1, GLushort Arg2)
{
    VirtGpuOglSecondaryColor3f(VirtGpuOglColorFromUShort(Arg0),
                               VirtGpuOglColorFromUShort(Arg1),
                               VirtGpuOglColorFromUShort(Arg2));
}

static void APIENTRY
VirtGpuOglSecondaryColor3usv(const GLushort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglSecondaryColor3us(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglSecondaryColorPointer(GLint Arg0, GLenum Arg1, GLsizei Arg2, const GLvoid * Arg3)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    if ((Arg0 != 3) || (Arg2 < 0) || !VirtGpuOglTypeAllowedForColorArray(Arg1))
    {
        VirtGpuOglSetError(Context, (Arg2 < 0) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }

    Context->SecondaryColorArraySize = Arg0;
    Context->SecondaryColorArrayType = Arg1;
    Context->SecondaryColorArrayStride = Arg2;
    Context->SecondaryColorArrayPointer = Arg3;
}

static void APIENTRY
VirtGpuOglWindowPos2d(GLdouble Arg0, GLdouble Arg1)
{
    VirtGpuOglWindowPos3f((GLfloat)Arg0, (GLfloat)Arg1, 0.0f);
}

static void APIENTRY
VirtGpuOglWindowPos2dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglWindowPos2d(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglWindowPos2f(GLfloat Arg0, GLfloat Arg1)
{
    VirtGpuOglWindowPos3f(Arg0, Arg1, 0.0f);
}

static void APIENTRY
VirtGpuOglWindowPos2fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglWindowPos2f(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglWindowPos2i(GLint Arg0, GLint Arg1)
{
    VirtGpuOglWindowPos3f((GLfloat)Arg0, (GLfloat)Arg1, 0.0f);
}

static void APIENTRY
VirtGpuOglWindowPos2iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglWindowPos2i(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglWindowPos2s(GLshort Arg0, GLshort Arg1)
{
    VirtGpuOglWindowPos3f((GLfloat)Arg0, (GLfloat)Arg1, 0.0f);
}

static void APIENTRY
VirtGpuOglWindowPos2sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglWindowPos2s(Arg0[0], Arg0[1]);
}

static void APIENTRY
VirtGpuOglWindowPos3d(GLdouble Arg0, GLdouble Arg1, GLdouble Arg2)
{
    VirtGpuOglWindowPos3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglWindowPos3dv(const GLdouble * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglWindowPos3d(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglWindowPos3f(GLfloat Arg0, GLfloat Arg1, GLfloat Arg2)
{
    PVIRTGPU_OGL_CONTEXT Context = VirtGpuOglCurrentContext();

    if (Context == NULL)
        return;

    Context->CurrentRasterPosition[0] = Arg0;
    Context->CurrentRasterPosition[1] = Arg1;
    Context->CurrentRasterPosition[2] = Arg2;
    Context->CurrentRasterPosition[3] = 1.0f;
    Context->CurrentRasterWindow.x = VirtGpuOglRoundFloat(Arg0);
    Context->CurrentRasterWindow.y = VirtGpuOglRoundFloat(Arg1);
    Context->CurrentRasterPositionValid = GL_TRUE;
}

static void APIENTRY
VirtGpuOglWindowPos3fv(const GLfloat * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglWindowPos3f(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglWindowPos3i(GLint Arg0, GLint Arg1, GLint Arg2)
{
    VirtGpuOglWindowPos3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglWindowPos3iv(const GLint * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglWindowPos3i(Arg0[0], Arg0[1], Arg0[2]);
}

static void APIENTRY
VirtGpuOglWindowPos3s(GLshort Arg0, GLshort Arg1, GLshort Arg2)
{
    VirtGpuOglWindowPos3f((GLfloat)Arg0, (GLfloat)Arg1, (GLfloat)Arg2);
}

static void APIENTRY
VirtGpuOglWindowPos3sv(const GLshort * Arg0)
{
    if (Arg0 != NULL)
        VirtGpuOglWindowPos3s(Arg0[0], Arg0[1], Arg0[2]);
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
    static const GLubyte Version[] = "1.1 ReactOS VirtIO GPU transport scaffold";
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
    ULONGLONG EvalBit;

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
        case GL_CURRENT_RASTER_POSITION_VALID:
            *Params = (Context != NULL) ? Context->CurrentRasterPositionValid : GL_TRUE;
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
            else if (VirtGpuOglEvalCapToBit(Pname, &EvalBit))
            {
                *Params = (Context->EvalEnableBits & EvalBit) ? GL_TRUE : GL_FALSE;
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
    ULONGLONG EvalBit;
    GLenum PixelMap;
    ULONG PixelMapIndex;

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
        case GL_NUM_EXTENSIONS:
            *Params = 0;
            break;
        case GL_MAX_RENDERBUFFER_SIZE:
            *Params = 4096;
            break;
        case GL_MAX_COLOR_ATTACHMENTS:
            *Params = 1;
            break;
        case GL_MAX_SAMPLES:
            *Params = 1;
            break;
        case GL_MAX_EVAL_ORDER:
            *Params = VIRTGPU_OGL_MAX_EVAL_ORDER;
            break;
        case GL_MAX_PIXEL_MAP_TABLE:
            *Params = 4096;
            break;
        case GL_MAX_NAME_STACK_DEPTH:
            *Params = VIRTGPU_OGL_NAME_STACK_DEPTH;
            break;
        case GL_MAX_ATTRIB_STACK_DEPTH:
            *Params = 1;
            break;
        case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH:
            *Params = 1;
            break;
        case GL_MAX_VERTEX_ATTRIBS:
            *Params = VIRTGPU_OGL_MAX_VERTEX_ATTRIBS;
            break;
        case GL_MAX_VERTEX_UNIFORM_COMPONENTS:
            *Params = VIRTGPU_OGL_MAX_UNIFORM_VALUES;
            break;
        case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:
            *Params = VIRTGPU_OGL_MAX_UNIFORM_VALUES;
            break;
        case GL_MAX_VARYING_FLOATS:
            *Params = 0;
            break;
        case GL_MAX_UNIFORM_BUFFER_BINDINGS:
            *Params = VIRTGPU_OGL_MAX_BUFFER_BINDINGS;
            break;
        case GL_MAX_UNIFORM_BLOCK_SIZE:
            *Params = VIRTGPU_OGL_MAX_TRANSFER_SIZE;
            break;
        case GL_MAX_VERTEX_UNIFORM_BLOCKS:
        case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:
        case GL_MAX_COMBINED_UNIFORM_BLOCKS:
            *Params = 0;
            break;
        case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
            *Params = 1;
            break;
        case GL_MAX_TRANSFORM_FEEDBACK_BUFFERS:
            *Params = VIRTGPU_OGL_MAX_BUFFER_BINDINGS;
            break;
        case GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS:
        case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS:
            *Params = VIRTGPU_OGL_MAX_UNIFORM_VALUES;
            break;
        case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
            *Params = VIRTGPU_OGL_MAX_TRANSFORM_FEEDBACK_VARYINGS;
            break;
        case GL_MAX_TEXTURE_BUFFER_SIZE:
            *Params = VIRTGPU_OGL_MAX_TRANSFER_SIZE;
            break;
        case GL_MAX_PATCH_VERTICES:
            *Params = VIRTGPU_OGL_MAX_PATCH_VERTICES;
            break;
        case GL_MAX_TESS_GEN_LEVEL:
        case GL_MAX_TESS_CONTROL_UNIFORM_COMPONENTS:
        case GL_MAX_TESS_EVALUATION_UNIFORM_COMPONENTS:
        case GL_MAX_TESS_CONTROL_TEXTURE_IMAGE_UNITS:
        case GL_MAX_TESS_EVALUATION_TEXTURE_IMAGE_UNITS:
        case GL_MAX_TESS_CONTROL_OUTPUT_COMPONENTS:
        case GL_MAX_TESS_PATCH_COMPONENTS:
        case GL_MAX_TESS_CONTROL_TOTAL_OUTPUT_COMPONENTS:
        case GL_MAX_TESS_EVALUATION_OUTPUT_COMPONENTS:
        case GL_MAX_TESS_CONTROL_UNIFORM_BLOCKS:
        case GL_MAX_TESS_EVALUATION_UNIFORM_BLOCKS:
        case GL_MAX_TESS_CONTROL_INPUT_COMPONENTS:
        case GL_MAX_TESS_EVALUATION_INPUT_COMPONENTS:
            *Params = 0;
            break;
        case GL_CURRENT_PROGRAM:
            *Params = (Context != NULL) ? (GLint)Context->CurrentProgram : 0;
            break;
        case GL_ARRAY_BUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundArrayBuffer : 0;
            break;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundElementArrayBuffer : 0;
            break;
        case GL_COPY_READ_BUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundCopyReadBuffer : 0;
            break;
        case GL_COPY_WRITE_BUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundCopyWriteBuffer : 0;
            break;
        case GL_UNIFORM_BUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundUniformBuffer : 0;
            break;
        case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundTransformFeedbackBuffer : 0;
            break;
        case GL_DRAW_INDIRECT_BUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundDrawIndirectBuffer : 0;
            break;
        case GL_TEXTURE_BINDING_BUFFER:
            *Params = (Context != NULL) ? (GLint)Context->BoundTextureBuffer : 0;
            break;
        case GL_RENDERBUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundRenderbuffer : 0;
            break;
        case GL_FRAMEBUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundDrawFramebuffer : 0;
            break;
        case GL_READ_FRAMEBUFFER_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundReadFramebuffer : 0;
            break;
        case GL_VERTEX_ARRAY_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundVertexArray : 0;
            break;
        case GL_TRANSFORM_FEEDBACK_BINDING:
            *Params = (Context != NULL) ? (GLint)Context->BoundTransformFeedback : 0;
            break;
        case GL_TRANSFORM_FEEDBACK_ACTIVE:
            if (Context != NULL)
            {
                PVIRTGPU_OGL_TRANSFORM_FEEDBACK TransformFeedback;

                TransformFeedback = VirtGpuOglFindTransformFeedback(Context,
                                                                    Context->BoundTransformFeedback);
                *Params = (TransformFeedback != NULL) ?
                          (TransformFeedback->Active ? GL_TRUE : GL_FALSE) :
                          (Context->DefaultTransformFeedbackActive ? GL_TRUE : GL_FALSE);
            }
            break;
        case GL_TRANSFORM_FEEDBACK_PAUSED:
            if (Context != NULL)
            {
                PVIRTGPU_OGL_TRANSFORM_FEEDBACK TransformFeedback;

                TransformFeedback = VirtGpuOglFindTransformFeedback(Context,
                                                                    Context->BoundTransformFeedback);
                *Params = (TransformFeedback != NULL) ?
                          (TransformFeedback->Paused ? GL_TRUE : GL_FALSE) :
                          (Context->DefaultTransformFeedbackPaused ? GL_TRUE : GL_FALSE);
            }
            break;
        case GL_PRIMITIVE_RESTART_INDEX:
            *Params = (Context != NULL) ? (GLint)Context->PrimitiveRestartIndex : -1;
            break;
        case GL_PROVOKING_VERTEX:
            *Params = (Context != NULL) ? (GLint)Context->ProvokingVertexMode : GL_LAST_VERTEX_CONVENTION;
            break;
        case GL_PATCH_VERTICES:
            *Params = (Context != NULL) ? Context->PatchVertices : 3;
            break;
        case GL_CLAMP_VERTEX_COLOR:
            *Params = (Context != NULL) ? (GLint)Context->ClampVertexColor : GL_TRUE;
            break;
        case GL_CLAMP_FRAGMENT_COLOR:
            *Params = (Context != NULL) ? (GLint)Context->ClampFragmentColor : GL_TRUE;
            break;
        case GL_CLAMP_READ_COLOR:
            *Params = (Context != NULL) ? (GLint)Context->ClampReadColor : GL_FIXED_ONLY;
            break;
        case GL_ACTIVE_TEXTURE:
            *Params = (Context != NULL) ? (GLint)Context->ActiveTexture : GL_TEXTURE0;
            break;
        case GL_CLIENT_ACTIVE_TEXTURE:
            *Params = (Context != NULL) ? (GLint)Context->ClientActiveTexture : GL_TEXTURE0;
            break;
        case GL_SAMPLER_BINDING:
            if (Context != NULL)
            {
                ULONG TextureUnit = (ULONG)(Context->ActiveTexture - GL_TEXTURE0);
                *Params = (TextureUnit < VIRTGPU_OGL_MAX_TEXTURE_UNITS) ?
                          (GLint)Context->BoundSamplers[TextureUnit] : 0;
            }
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
        case GL_ATTRIB_STACK_DEPTH:
        case GL_CLIENT_ATTRIB_STACK_DEPTH:
            *Params = 0;
            break;
        case GL_RENDER_MODE:
            *Params = (Context != NULL) ? (GLint)Context->RenderMode : GL_RENDER;
            break;
        case GL_FEEDBACK_BUFFER_SIZE:
            *Params = (Context != NULL) ? Context->FeedbackBufferSize : 0;
            break;
        case GL_FEEDBACK_BUFFER_TYPE:
            *Params = (Context != NULL) ? (GLint)Context->FeedbackBufferType : GL_2D;
            break;
        case GL_SELECTION_BUFFER_SIZE:
            *Params = (Context != NULL) ? Context->SelectBufferSize : 0;
            break;
        case GL_NAME_STACK_DEPTH:
            *Params = (Context != NULL) ? (GLint)Context->NameStackDepth : 0;
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
        case GL_SECONDARY_COLOR_ARRAY_SIZE:
            *Params = (Context != NULL) ? Context->SecondaryColorArraySize : 3;
            break;
        case GL_SECONDARY_COLOR_ARRAY_TYPE:
            *Params = (Context != NULL) ? (GLint)Context->SecondaryColorArrayType : GL_FLOAT;
            break;
        case GL_SECONDARY_COLOR_ARRAY_STRIDE:
            *Params = (Context != NULL) ? Context->SecondaryColorArrayStride : 0;
            break;
        case GL_FOG_COORD_ARRAY_TYPE:
            *Params = (Context != NULL) ? (GLint)Context->FogCoordArrayType : GL_FLOAT;
            break;
        case GL_FOG_COORD_ARRAY_STRIDE:
            *Params = (Context != NULL) ? Context->FogCoordArrayStride : 0;
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
        case GL_LOGIC_OP_MODE:
            *Params = (Context != NULL) ? (GLint)Context->LogicOpMode : GL_COPY;
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
                Params[3] = VirtGpuOglRoundFloat(Context->CurrentAlpha * 255.0f);
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
        case GL_CURRENT_INDEX:
            *Params = (Context != NULL) ? (GLint)Context->CurrentIndex : 1;
            break;
        case GL_CURRENT_RASTER_INDEX:
            *Params = (Context != NULL) ? (GLint)Context->CurrentIndex : 1;
            break;
        case GL_CURRENT_RASTER_POSITION:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->CurrentRasterPosition[0];
                Params[1] = (GLint)Context->CurrentRasterPosition[1];
                Params[2] = (GLint)Context->CurrentRasterPosition[2];
                Params[3] = (GLint)Context->CurrentRasterPosition[3];
            }
            break;
        case GL_CURRENT_RASTER_POSITION_VALID:
            *Params = ((Context != NULL) && (Context->CurrentRasterPositionValid == GL_TRUE)) ? 1 : 0;
            break;
        case GL_CURRENT_RASTER_COLOR:
            if (Context != NULL)
            {
                Params[0] = GetRValue(Context->CurrentColor);
                Params[1] = GetGValue(Context->CurrentColor);
                Params[2] = GetBValue(Context->CurrentColor);
                Params[3] = VirtGpuOglRoundFloat(Context->CurrentAlpha * 255.0f);
            }
            break;
        case GL_CURRENT_SECONDARY_COLOR:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->CurrentSecondaryColor[0];
                Params[1] = (GLint)Context->CurrentSecondaryColor[1];
                Params[2] = (GLint)Context->CurrentSecondaryColor[2];
            }
            break;
        case GL_CURRENT_RASTER_SECONDARY_COLOR:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->CurrentSecondaryColor[0];
                Params[1] = (GLint)Context->CurrentSecondaryColor[1];
                Params[2] = (GLint)Context->CurrentSecondaryColor[2];
                Params[3] = 1;
            }
            break;
        case GL_CURRENT_FOG_COORD:
            *Params = (Context != NULL) ? (GLint)Context->CurrentFogCoord : 0;
            break;
        case GL_CURRENT_RASTER_TEXTURE_COORDS:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->CurrentTexCoord[0];
                Params[1] = (GLint)Context->CurrentTexCoord[1];
                Params[2] = (GLint)Context->CurrentTexCoord[2];
                Params[3] = (GLint)Context->CurrentTexCoord[3];
            }
            break;
        case GL_CURRENT_RASTER_DISTANCE:
            *Params = 0;
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
        case GL_INDEX_CLEAR_VALUE:
            *Params = (Context != NULL) ? (GLint)Context->ClearIndex : 0;
            break;
        case GL_INDEX_WRITEMASK:
            *Params = (Context != NULL) ? (GLint)Context->IndexMask : -1;
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
        case GL_PACK_ROW_LENGTH:
            *Params = (Context != NULL) ? Context->PackRowLength : 0;
            break;
        case GL_PACK_SKIP_ROWS:
            *Params = (Context != NULL) ? Context->PackSkipRows : 0;
            break;
        case GL_PACK_SKIP_PIXELS:
            *Params = (Context != NULL) ? Context->PackSkipPixels : 0;
            break;
        case GL_PACK_SWAP_BYTES:
            *Params = ((Context != NULL) && (Context->PackSwapBytes == GL_TRUE)) ? 1 : 0;
            break;
        case GL_PACK_LSB_FIRST:
            *Params = ((Context != NULL) && (Context->PackLsbFirst == GL_TRUE)) ? 1 : 0;
            break;
        case GL_UNPACK_ALIGNMENT:
            *Params = (Context != NULL) ? Context->UnpackAlignment : 4;
            break;
        case GL_UNPACK_ROW_LENGTH:
            *Params = (Context != NULL) ? Context->UnpackRowLength : 0;
            break;
        case GL_UNPACK_SKIP_ROWS:
            *Params = (Context != NULL) ? Context->UnpackSkipRows : 0;
            break;
        case GL_UNPACK_SKIP_PIXELS:
            *Params = (Context != NULL) ? Context->UnpackSkipPixels : 0;
            break;
        case GL_UNPACK_SWAP_BYTES:
            *Params = ((Context != NULL) && (Context->UnpackSwapBytes == GL_TRUE)) ? 1 : 0;
            break;
        case GL_UNPACK_LSB_FIRST:
            *Params = ((Context != NULL) && (Context->UnpackLsbFirst == GL_TRUE)) ? 1 : 0;
            break;
        case GL_MAP1_GRID_SEGMENTS:
            *Params = (Context != NULL) ? (GLint)Context->MapGrid1[0] : 1;
            break;
        case GL_MAP1_GRID_DOMAIN:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->MapGrid1[1];
                Params[1] = (GLint)Context->MapGrid1[2];
            }
            break;
        case GL_MAP2_GRID_SEGMENTS:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->MapGrid2[0];
                Params[1] = (GLint)Context->MapGrid2[3];
            }
            break;
        case GL_MAP2_GRID_DOMAIN:
            if (Context != NULL)
            {
                Params[0] = (GLint)Context->MapGrid2[1];
                Params[1] = (GLint)Context->MapGrid2[2];
                Params[2] = (GLint)Context->MapGrid2[4];
                Params[3] = (GLint)Context->MapGrid2[5];
            }
            break;
        default:
            if (VirtGpuOglPixelMapSizePnameToMap(Pname, &PixelMap) &&
                (Context != NULL) &&
                VirtGpuOglPixelMapToIndex(PixelMap, &PixelMapIndex))
            {
                *Params = (Context->PixelMaps[PixelMapIndex].Size > 0) ?
                          Context->PixelMaps[PixelMapIndex].Size : 1;
            }
            else if (Context == NULL)
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
            else if (VirtGpuOglEvalCapToBit(Pname, &EvalBit))
            {
                *Params = (Context->EvalEnableBits & EvalBit) ? 1 : 0;
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
        Params[3] = Context->CurrentAlpha;
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

    if ((Context != NULL) && (Pname == GL_CURRENT_INDEX))
    {
        Params[0] = Context->CurrentIndex;
        return;
    }

    if ((Context != NULL) && (Pname == GL_CURRENT_RASTER_POSITION))
    {
        CopyMemory(Params, Context->CurrentRasterPosition, 4 * sizeof(GLfloat));
        return;
    }

    if ((Context != NULL) && (Pname == GL_CURRENT_RASTER_COLOR))
    {
        Params[0] = (GLfloat)GetRValue(Context->CurrentColor) / 255.0f;
        Params[1] = (GLfloat)GetGValue(Context->CurrentColor) / 255.0f;
        Params[2] = (GLfloat)GetBValue(Context->CurrentColor) / 255.0f;
        Params[3] = Context->CurrentAlpha;
        return;
    }

    if ((Context != NULL) && (Pname == GL_CURRENT_RASTER_TEXTURE_COORDS))
    {
        CopyMemory(Params, Context->CurrentTexCoord, 4 * sizeof(GLfloat));
        return;
    }

    if ((Context != NULL) && (Pname == GL_CURRENT_SECONDARY_COLOR))
    {
        CopyMemory(Params, Context->CurrentSecondaryColor, 3 * sizeof(GLfloat));
        return;
    }

    if ((Context != NULL) && (Pname == GL_CURRENT_RASTER_SECONDARY_COLOR))
    {
        CopyMemory(Params, Context->CurrentSecondaryColor, 3 * sizeof(GLfloat));
        Params[3] = 1.0f;
        return;
    }

    if ((Context != NULL) && (Pname == GL_CURRENT_FOG_COORD))
    {
        Params[0] = Context->CurrentFogCoord;
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
            case GL_INDEX_CLEAR_VALUE:
                Params[0] = Context->ClearIndex;
                return;
            case GL_ACCUM_CLEAR_VALUE:
                CopyMemory(Params, Context->ClearAccum, 4 * sizeof(GLfloat));
                return;
            case GL_ZOOM_X:
                Params[0] = Context->PixelZoomX;
                return;
            case GL_ZOOM_Y:
                Params[0] = Context->PixelZoomY;
                return;
            case GL_MAP1_GRID_DOMAIN:
                Params[0] = (GLfloat)Context->MapGrid1[1];
                Params[1] = (GLfloat)Context->MapGrid1[2];
                return;
            case GL_MAP2_GRID_DOMAIN:
                Params[0] = (GLfloat)Context->MapGrid2[1];
                Params[1] = (GLfloat)Context->MapGrid2[2];
                Params[2] = (GLfloat)Context->MapGrid2[4];
                Params[3] = (GLfloat)Context->MapGrid2[5];
                return;
            case GL_BLEND_COLOR:
                CopyMemory(Params, Context->BlendColor, 4 * sizeof(GLfloat));
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
    ULONGLONG EvalBit;

    if (Context == NULL)
    {
        VirtGpuOglSetError(Context, GL_INVALID_ENUM);
        return GL_FALSE;
    }

    if (!VirtGpuOglCapToBit(Cap, &Bit))
    {
        if (VirtGpuOglEvalCapToBit(Cap, &EvalBit))
            return (Context->EvalEnableBits & EvalBit) ? GL_TRUE : GL_FALSE;

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
VirtGpu3DCreateVirglContext(
    _In_ HDC hdc,
    _Out_ PVIRTGPU_3D_CREATE_CONTEXT Context,
    _In_opt_ LPCSTR DebugName)
{
    VIRTGPU_3D_CAPS Caps;
    static const CHAR DefaultName[] = "ReactOS VirtGpu VirGL";

    if ((Context == NULL) || !VirtGpuOglQueryCaps(hdc, &Caps))
        return FALSE;

    ZeroMemory(Context, sizeof(*Context));
    Context->CapsetId = Caps.PreferredCapsetId;
    if (DebugName == NULL)
        DebugName = DefaultName;

    lstrcpynA(Context->DebugName,
              DebugName,
              VIRTGPU_SHARED_CONTEXT_NAME_SIZE);

    return VirtGpu3DCreateContext(hdc, Context);
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
VirtGpu3DExecuteBatch(
    _In_ HDC hdc,
    _Inout_updates_bytes_(BufferSize) PVIRTGPU_3D_BATCH Batch,
    _In_ ULONG BufferSize)
{
    ULONG HeaderSize = offsetof(VIRTGPU_3D_BATCH, Commands);

    return (Batch != NULL) &&
           (BufferSize >= HeaderSize) &&
           (Batch->Size <= BufferSize - HeaderSize) &&
           (VirtGpuOglIoControl(hdc,
                                IOCTL_VIDEO_VIRTGPU_3D_EXECUTE_BATCH,
                                Batch,
                                BufferSize,
                                Batch,
                                BufferSize) >= HeaderSize);
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
    PVIRTGPU_OGL_CONTEXT Source = VirtGpuOglValidateContext(hglrcSrc);
    PVIRTGPU_OGL_CONTEXT Destination = VirtGpuOglValidateContext(hglrcDst);

    if ((Source == NULL) || (Destination == NULL))
        return FALSE;

    if ((mask == 0) || (Source == Destination))
        return TRUE;

    return FALSE;
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
    for (Index = 0; Index < VIRTGPU_OGL_MAX_PROGRAMS; ++Index)
        VirtGpuOglFreeProgram(Context, &Context->Programs[Index]);
    for (Index = 0; Index < VIRTGPU_OGL_MAX_SHADERS; ++Index)
        VirtGpuOglFreeShader(&Context->Shaders[Index]);
    for (Index = 0; Index < VIRTGPU_OGL_MAX_BUFFERS; ++Index)
        VirtGpuOglFreeBuffer(&Context->Buffers[Index]);
    for (Index = 0; Index < VIRTGPU_OGL_PIXEL_MAP_COUNT; ++Index)
        VirtGpuOglFreePixelMap(&Context->PixelMaps[Index]);
    for (Index = 0; Index < VIRTGPU_OGL_COLOR_TABLE_COUNT; ++Index)
        VirtGpuOglFreeImageTable(&Context->ColorTables[Index]);
    for (Index = 0; Index < VIRTGPU_OGL_CONVOLUTION_COUNT; ++Index)
        VirtGpuOglFreeImageTable(&Context->ConvolutionFilters[Index]);
    for (Index = 0; Index < VIRTGPU_OGL_EVAL_MAP_COUNT; ++Index)
    {
        VirtGpuOglFreeEvalMap1(&Context->EvalMap1[Index]);
        VirtGpuOglFreeEvalMap2(&Context->EvalMap2[Index]);
    }

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
    if (lstrcmpA(lpProcName, "VirtGpu3DCreateVirglContext") == 0)
        return (PROC)VirtGpu3DCreateVirglContext;
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
    if (lstrcmpA(lpProcName, "VirtGpu3DExecuteBatch") == 0)
        return (PROC)VirtGpu3DExecuteBatch;
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
    PVIRTGPU_OGL_CONTEXT Source = VirtGpuOglValidateContext(hglrc1);
    PVIRTGPU_OGL_CONTEXT Destination = VirtGpuOglValidateContext(hglrc2);

    if ((Source == NULL) || (Destination == NULL))
        return FALSE;

    return Source == Destination;
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
