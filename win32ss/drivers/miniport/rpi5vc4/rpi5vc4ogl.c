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

#include <context.h>
#include <matrix.h>
#include <triangle.h>
#include <vb.h>
#include <reactos/rpi5vc4_xpdm.h>

#include "rpi5vc4ogl_fbo.h"
#include "rpi5vc4ogl_gl2.h"

#define RPI5VC4_OGL_CONTEXT_SIGNATURE '1GlR'
#define RPI5VC4_OPENGL_ICD_DRIVER_VERSION 1
#define RPI5VC4_OPENGL_ENTRY_COUNT 336
#define RPI5VC4_OPENGL_PIXEL_FORMAT_COUNT 1
#define RPI5VC4_OPENGL_TEX_IMAGE_2D_INDEX 183
#define RPI5VC4_OPENGL_GET_TEX_IMAGE_INDEX 281
#define RPI5VC4_OPENGL_DRAW_ARRAYS_INDEX 310
#define RPI5VC4_OPENGL_TEX_SUB_IMAGE_2D_INDEX 333

DECLARE_HANDLE(DHGLRC);

typedef struct _RPI5VC4_OGL_PROC_TABLE
{
    INT EntryCount;
    PROC Entries[RPI5VC4_OPENGL_ENTRY_COUNT];
} RPI5VC4_OGL_PROC_TABLE, *PRPI5VC4_OGL_PROC_TABLE;

typedef VOID (APIENTRY *PFN_SET_CURRENT_VALUE)(PVOID Value);
typedef PVOID (APIENTRY *PFN_GET_CURRENT_VALUE)(VOID);

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
C_ASSERT(RPI5VC4_OGL_GL2_MAX_DRAW_VERTICES ==
         RPI5VC4_V3D_PRIMITIVE_MAX_VERTICES);

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

GLcontext *
gl_get_thread_context(VOID)
{
    PRPI5VC4_OGL_CONTEXT Context = Rpi5OglCurrentContext();

    return Context != NULL ? Context->MesaContext : NULL;
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
    PULONG Pixels;
    GLubyte *TextureData;
    ULONG Width;
    ULONG Height;
    GLenum TextureFormat;
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
        if (!Rpi5OglFboGetColorTarget(Context->FboState, &FboTarget))
            return FALSE;
        Target->TextureData = FboTarget.Data;
        Target->Width = FboTarget.Width;
        Target->Height = FboTarget.Height;
        Target->TextureFormat = FboTarget.Format;
        Target->Framebuffer = FboTarget.Framebuffer;
        Target->Generation = FboTarget.Generation;
        return TRUE;
    }

    if (!Rpi5OglRefreshDrawable(Context))
        return FALSE;
    Target->Pixels = Context->BackBuffer;
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
Rpi5OglPresent(
    _Inout_ PRPI5VC4_OGL_CONTEXT Context)
{
    BITMAPINFO BitmapInfo;
    INT Copied;

    if (!Rpi5OglRefreshDrawable(Context))
        return FALSE;

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
        Target.Width > RPI5VC4_V3D_CLEAR_MAX_WIDTH ||
        Target.Height > RPI5VC4_V3D_CLEAR_MAX_HEIGHT)
    {
        return FALSE;
    }

    PixelBytes = Target.Width * Target.Height * sizeof(ULONG);
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

static BOOL
Rpi5OglTriangleStateSupported(
    _In_ GLcontext *Mesa)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    return Context != NULL &&
           Mesa->Visual != NULL &&
           Mesa->Visual->RGBAflag &&
           Mesa->RenderMode == GL_RENDER &&
           Mesa->Texture.Enabled == 0 &&
           Mesa->RasterMask == 0 &&
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
    ULONG Vertex;

    if (Context == NULL)
        return;
    DrawResult = Rpi5OglGl2BuildPrimitive(Context->Gl2State,
                                          Mode,
                                          First,
                                          Count,
                                          Gl2Vertices);
    if (DrawResult == Rpi5OglGl2DrawNotApplicable)
    {
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

static VOID
Rpi5OglSoftwareTriangle(
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint ProvokingVertex)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;
    triangle_func DriverTriangle = Mesa->Driver.TriangleFunc;
    triangle_func SoftwareTriangle;

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

static VOID
Rpi5OglTriangle(
    _In_ GLcontext *Mesa,
    _In_ GLuint Vertex0,
    _In_ GLuint Vertex1,
    _In_ GLuint Vertex2,
    _In_ GLuint ProvokingVertex)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

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

static const CHAR *
Rpi5OglRendererString(VOID)
{
    return "RPi5-V3D-7.1-Hybrid-GL1";
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

    Context->HardwareClearFresh = FALSE;

    if (!Rpi5OglResolveRenderTarget(Context, &Target))
        return;

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
        if (Mode != GL_COLOR_ATTACHMENT0_EXT)
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

    if (!Rpi5OglResolveRenderTarget(Context, &Target))
    {
        ZeroMemory(Red, Count * sizeof(*Red));
        ZeroMemory(Green, Count * sizeof(*Green));
        ZeroMemory(Blue, Count * sizeof(*Blue));
        ZeroMemory(Alpha, Count * sizeof(*Alpha));
        return;
    }

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

    if (!Rpi5OglResolveRenderTarget(Context, &Target))
    {
        ZeroMemory(Red, Count * sizeof(*Red));
        ZeroMemory(Green, Count * sizeof(*Green));
        ZeroMemory(Blue, Count * sizeof(*Blue));
        ZeroMemory(Alpha, Count * sizeof(*Alpha));
        return;
    }

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
    Context->HardwareClearFresh = FALSE;
    Rpi5OglFboTextureChanged(Context->FboState, Texture->Name);
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
        Texture->Name);
}

static VOID
Rpi5OglTextureDeleted(
    _In_ GLcontext *Mesa,
    _In_ struct gl_texture_object *Texture)
{
    PRPI5VC4_OGL_CONTEXT Context = Mesa->DriverCtx;

    Context->HardwareClearFresh = FALSE;
    Rpi5OglFboTextureDeleted(Context->FboState, Texture->Name);
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
    Mesa->Driver.DeleteTexture = Rpi5OglTextureDeleted;
    if (Rpi5OglTriangleStateSupported(Mesa))
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
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_TEX_IMAGE_2D_INDEX] =
        (PROC)Rpi5OglFboTexImage2D;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_GET_TEX_IMAGE_INDEX] =
        (PROC)Rpi5OglFboGetTexImage;
    Rpi5OglProcTable.Entries[RPI5VC4_OPENGL_DRAW_ARRAYS_INDEX] =
        (PROC)Rpi5OglDrawArrays;
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
    if (!Rpi5OglFboInitialize(&Context->FboState,
                              Context->MesaContext))
    {
        goto Failure;
    }
    if (!Rpi5OglGl2Initialize(&Context->Gl2State,
                               Context->MesaContext))
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
    Rpi5OglGl2Cleanup(Context->Gl2State);
    Rpi5OglFboCleanup(Context->FboState);
    if (Context->MesaContext != NULL)
        gl_destroy_context(Context->MesaContext);
    if (Context->FrameBuffer != NULL)
        gl_destroy_framebuffer(Context->FrameBuffer);
    if (Context->Visual != NULL)
        gl_destroy_visual(Context->Visual);
    if (Context->BackBuffer != NULL)
        HeapFree(GetProcessHeap(), 0, Context->BackBuffer);
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
    Rpi5OglGl2Cleanup(Context->Gl2State);
    Rpi5OglFboCleanup(Context->FboState);
    gl_destroy_context(Context->MesaContext);
    gl_destroy_framebuffer(Context->FrameBuffer);
    gl_destroy_visual(Context->Visual);
    HeapFree(GetProcessHeap(), 0, Context->BackBuffer);
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

PROC WINAPI
DrvGetProcAddress(
    _In_ LPCSTR Name)
{
    PROC Procedure;

    if (Name != NULL && lstrcmpA(Name, "Rpi5Vc4GetStats") == 0)
        return (PROC)Rpi5Vc4GetStats;
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
        Context->HardwareClearFresh = FALSE;
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
    gl_ResizeBuffersMESA(Context->MesaContext);
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
