/*
 * PROJECT:     ReactOS Raspberry Pi 5 Hardware Diagnostic
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     End-to-end validation of the standard Windows hardware APIs
 */

#define COBJMACROS
#define WIN32_NO_STATUS
#include <windows.h>
#include <ntddvdeo.h>
#include <setupapi.h>
#include <cfg.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <pmi.h>
#include <sensorsapi.h>
#include <sensors.h>
#include <d3d9.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <reactos/rpi5vc4_xpdm.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define RPI5_DIAG_MAX_SENSORS 64
#define RPI5_DIAG_NAME_LENGTH 96
#define RPI5_DIAG_MAX_PROCESSORS (sizeof(DWORD_PTR) * 8)

typedef DWORD (WINAPI *PRPI5_GET_CURRENT_PROCESSOR_NUMBER)(VOID);
typedef BOOL (APIENTRY *PRPI5VC4_GET_OGL_STATS)(
    _Out_ PRPI5VC4_OGL_STATS Stats);
typedef IDirect3D9 *(WINAPI *PRPI5_DIRECT3D_CREATE9)(UINT SdkVersion);

typedef struct _RPI5_SENSOR_SAMPLE
{
    ISensor *Sensor;
    GUID Id;
    GUID Type;
    const PROPERTYKEY *DataKey;
    WCHAR Name[RPI5_DIAG_NAME_LENGTH];
    double FirstValue;
    double SecondValue;
} RPI5_SENSOR_SAMPLE, *PRPI5_SENSOR_SAMPLE;

typedef struct _RPI5_CPU_CONTEXT
{
    PRPI5_GET_CURRENT_PROCESSOR_NUMBER GetCurrentProcessorNumber;
    DWORD ObservedProcessor;
} RPI5_CPU_CONTEXT, *PRPI5_CPU_CONTEXT;

typedef struct _RPI5_EXPECTED_DEVICE
{
    PCWSTR Prefix;
    ULONG ExpectedCount;
    ULONG FoundCount;
    BOOL MustBeStarted;
} RPI5_EXPECTED_DEVICE, *PRPI5_EXPECTED_DEVICE;

static volatile LONG Rpi5LoadStop;

static VOID
Rpi5DiagPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...);

static LRESULT CALLBACK
Rpi5OpenGlWindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static BOOL
Rpi5OpenGlProcAvailable(
    _In_z_ PCSTR Name)
{
    PROC Procedure = wglGetProcAddress(Name);

    return Procedure != NULL &&
           Procedure != (PROC)(ULONG_PTR)1 &&
           Procedure != (PROC)(ULONG_PTR)2 &&
           Procedure != (PROC)(ULONG_PTR)3 &&
           Procedure != (PROC)(LONG_PTR)-1;
}

static BOOL
Rpi5OpenGlProcSetAvailable(
    _In_reads_(Count) const PCSTR *Names,
    _In_ ULONG Count,
    _Out_opt_ PCSTR *Missing)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (!Rpi5OpenGlProcAvailable(Names[Index]))
        {
            if (Missing)
                *Missing = Names[Index];
            return FALSE;
        }
    }
    if (Missing)
        *Missing = NULL;
    return TRUE;
}

static VOID
Rpi5ProbeDirect3D9(VOID)
{
    PRPI5_DIRECT3D_CREATE9 CreateDirect3D9;
    D3DADAPTER_IDENTIFIER9 Identifier;
    D3DCAPS9 Caps;
    IDirect3D9 *Direct3D;
    HMODULE Module;
    HRESULT Result;
    UINT AdapterCount;

    Rpi5DiagPrint("RPI5_D3D9_PROBE_BEGIN\n");
    Module = LoadLibraryW(L"d3d9.dll");
    if (Module == NULL)
    {
        Rpi5DiagPrint("RPI5_D3D9_PROBE_BLOCKED load-error=%lu\n",
                      GetLastError());
        return;
    }

    CreateDirect3D9 = (PRPI5_DIRECT3D_CREATE9)GetProcAddress(
        Module, "Direct3DCreate9");
    if (CreateDirect3D9 == NULL)
    {
        Rpi5DiagPrint("RPI5_D3D9_PROBE_BLOCKED proc-error=%lu\n",
                      GetLastError());
        FreeLibrary(Module);
        return;
    }

    Direct3D = CreateDirect3D9(D3D_SDK_VERSION);
    if (Direct3D == NULL)
    {
        Rpi5DiagPrint("RPI5_D3D9_PROBE_BLOCKED adapter-init=0 gl2-required=1\n");
        FreeLibrary(Module);
        return;
    }

    AdapterCount = IDirect3D9_GetAdapterCount(Direct3D);
    ZeroMemory(&Identifier, sizeof(Identifier));
    Result = IDirect3D9_GetAdapterIdentifier(Direct3D,
                                              D3DADAPTER_DEFAULT,
                                              0,
                                              &Identifier);
    ZeroMemory(&Caps, sizeof(Caps));
    if (SUCCEEDED(Result))
    {
        Result = IDirect3D9_GetDeviceCaps(Direct3D,
                                          D3DADAPTER_DEFAULT,
                                          D3DDEVTYPE_HAL,
                                          &Caps);
    }
    if (FAILED(Result))
    {
        Rpi5DiagPrint("RPI5_D3D9_PROBE_BLOCKED adapters=%u query=0x%08lx\n",
                      AdapterCount, Result);
    }
    else if (lstrcmpA(Identifier.Description,
                      "ReactOS Software Rasterizer") == 0)
    {
        Rpi5DiagPrint("RPI5_D3D9_PROBE_BLOCKED adapters=%u backend=software description=%s vendor=0x%04lx device-id=0x%04lx vs=0x%08lx ps=0x%08lx\n",
                      AdapterCount,
                      Identifier.Description,
                      Identifier.VendorId,
                      Identifier.DeviceId,
                      Caps.VertexShaderVersion,
                      Caps.PixelShaderVersion);
    }
    else
    {
        Rpi5DiagPrint("RPI5_D3D9_PROBE_PASS adapters=%u description=%s device=%s vendor=0x%04lx device-id=0x%04lx vs=0x%08lx ps=0x%08lx\n",
                      AdapterCount,
                      Identifier.Description,
                      Identifier.DeviceName,
                      Identifier.VendorId,
                      Identifier.DeviceId,
                      Caps.VertexShaderVersion,
                      Caps.PixelShaderVersion);
    }

    IDirect3D9_Release(Direct3D);
    FreeLibrary(Module);
}

static BOOL
Rpi5TestOpenGl(
    _In_ BOOL FullRegression)
{
    static const WCHAR ClassName[] = L"Rpi5OpenGlTestWindow";
    static const PCSTR ShaderProcedures[] =
    {
        "glAttachShader",
        "glCompileShader",
        "glCreateProgram",
        "glCreateShader",
        "glDeleteProgram",
        "glDeleteShader",
        "glDetachShader",
        "glGetAttachedShaders",
        "glGetProgramInfoLog",
        "glGetProgramiv",
        "glGetShaderInfoLog",
        "glGetShaderiv",
        "glGetShaderSource",
        "glIsProgram",
        "glIsShader",
        "glLinkProgram",
        "glShaderSource",
        "glUseProgram",
        "glValidateProgram"
    };
    static const PCSTR OpenGl12Procedures[] =
    {
        "glBlendColor",
        "glBlendEquation",
        "glDrawRangeElements",
        "glTexImage3D",
        "glTexSubImage3D",
        "glCopyTexSubImage3D"
    };
    static const PCSTR OpenGl13Procedures[] =
    {
        "glActiveTexture",
        "glSampleCoverage",
        "glCompressedTexImage3D",
        "glCompressedTexImage2D",
        "glCompressedTexImage1D",
        "glCompressedTexSubImage3D",
        "glCompressedTexSubImage2D",
        "glCompressedTexSubImage1D",
        "glGetCompressedTexImage",
        "glClientActiveTexture",
        "glMultiTexCoord1d",
        "glMultiTexCoord1dv",
        "glMultiTexCoord1f",
        "glMultiTexCoord1fv",
        "glMultiTexCoord1i",
        "glMultiTexCoord1iv",
        "glMultiTexCoord1s",
        "glMultiTexCoord1sv",
        "glMultiTexCoord2d",
        "glMultiTexCoord2dv",
        "glMultiTexCoord2f",
        "glMultiTexCoord2fv",
        "glMultiTexCoord2i",
        "glMultiTexCoord2iv",
        "glMultiTexCoord2s",
        "glMultiTexCoord2sv",
        "glMultiTexCoord3d",
        "glMultiTexCoord3dv",
        "glMultiTexCoord3f",
        "glMultiTexCoord3fv",
        "glMultiTexCoord3i",
        "glMultiTexCoord3iv",
        "glMultiTexCoord3s",
        "glMultiTexCoord3sv",
        "glMultiTexCoord4d",
        "glMultiTexCoord4dv",
        "glMultiTexCoord4f",
        "glMultiTexCoord4fv",
        "glMultiTexCoord4i",
        "glMultiTexCoord4iv",
        "glMultiTexCoord4s",
        "glMultiTexCoord4sv",
        "glLoadTransposeMatrixf",
        "glLoadTransposeMatrixd",
        "glMultTransposeMatrixf",
        "glMultTransposeMatrixd"
    };
    static const PCSTR OpenGl14Procedures[] =
    {
        "glBlendFuncSeparate",
        "glMultiDrawArrays",
        "glMultiDrawElements",
        "glPointParameterf",
        "glPointParameterfv",
        "glPointParameteri",
        "glPointParameteriv",
        "glFogCoordf",
        "glFogCoordfv",
        "glFogCoordd",
        "glFogCoorddv",
        "glFogCoordPointer",
        "glSecondaryColor3b",
        "glSecondaryColor3bv",
        "glSecondaryColor3d",
        "glSecondaryColor3dv",
        "glSecondaryColor3f",
        "glSecondaryColor3fv",
        "glSecondaryColor3i",
        "glSecondaryColor3iv",
        "glSecondaryColor3s",
        "glSecondaryColor3sv",
        "glSecondaryColor3ub",
        "glSecondaryColor3ubv",
        "glSecondaryColor3ui",
        "glSecondaryColor3uiv",
        "glSecondaryColor3us",
        "glSecondaryColor3usv",
        "glSecondaryColorPointer",
        "glWindowPos2d",
        "glWindowPos2dv",
        "glWindowPos2f",
        "glWindowPos2fv",
        "glWindowPos2i",
        "glWindowPos2iv",
        "glWindowPos2s",
        "glWindowPos2sv",
        "glWindowPos3d",
        "glWindowPos3dv",
        "glWindowPos3f",
        "glWindowPos3fv",
        "glWindowPos3i",
        "glWindowPos3iv",
        "glWindowPos3s",
        "glWindowPos3sv"
    };
    static const PCSTR OpenGl15Procedures[] =
    {
        "glGenQueries",
        "glDeleteQueries",
        "glIsQuery",
        "glBeginQuery",
        "glEndQuery",
        "glGetQueryiv",
        "glGetQueryObjectiv",
        "glGetQueryObjectuiv",
        "glBindBuffer",
        "glDeleteBuffers",
        "glGenBuffers",
        "glIsBuffer",
        "glBufferData",
        "glBufferSubData",
        "glGetBufferSubData",
        "glMapBuffer",
        "glUnmapBuffer",
        "glGetBufferParameteriv",
        "glGetBufferPointerv"
    };
    static const PCSTR OpenGl20Procedures[] =
    {
        "glBlendEquationSeparate",
        "glDrawBuffers",
        "glStencilOpSeparate",
        "glStencilFuncSeparate",
        "glStencilMaskSeparate",
        "glAttachShader",
        "glBindAttribLocation",
        "glCompileShader",
        "glCreateProgram",
        "glCreateShader",
        "glDeleteProgram",
        "glDeleteShader",
        "glDetachShader",
        "glDisableVertexAttribArray",
        "glEnableVertexAttribArray",
        "glGetActiveAttrib",
        "glGetActiveUniform",
        "glGetAttachedShaders",
        "glGetAttribLocation",
        "glGetProgramiv",
        "glGetProgramInfoLog",
        "glGetShaderiv",
        "glGetShaderInfoLog",
        "glGetShaderSource",
        "glGetUniformLocation",
        "glGetUniformfv",
        "glGetUniformiv",
        "glGetVertexAttribdv",
        "glGetVertexAttribfv",
        "glGetVertexAttribiv",
        "glGetVertexAttribPointerv",
        "glIsProgram",
        "glIsShader",
        "glLinkProgram",
        "glShaderSource",
        "glUseProgram",
        "glUniform1f",
        "glUniform2f",
        "glUniform3f",
        "glUniform4f",
        "glUniform1i",
        "glUniform2i",
        "glUniform3i",
        "glUniform4i",
        "glUniform1fv",
        "glUniform2fv",
        "glUniform3fv",
        "glUniform4fv",
        "glUniform1iv",
        "glUniform2iv",
        "glUniform3iv",
        "glUniform4iv",
        "glUniformMatrix2fv",
        "glUniformMatrix3fv",
        "glUniformMatrix4fv",
        "glValidateProgram",
        "glVertexAttrib1d",
        "glVertexAttrib1dv",
        "glVertexAttrib1f",
        "glVertexAttrib1fv",
        "glVertexAttrib1s",
        "glVertexAttrib1sv",
        "glVertexAttrib2d",
        "glVertexAttrib2dv",
        "glVertexAttrib2f",
        "glVertexAttrib2fv",
        "glVertexAttrib2s",
        "glVertexAttrib2sv",
        "glVertexAttrib3d",
        "glVertexAttrib3dv",
        "glVertexAttrib3f",
        "glVertexAttrib3fv",
        "glVertexAttrib3s",
        "glVertexAttrib3sv",
        "glVertexAttrib4Nbv",
        "glVertexAttrib4Niv",
        "glVertexAttrib4Nsv",
        "glVertexAttrib4Nub",
        "glVertexAttrib4Nubv",
        "glVertexAttrib4Nuiv",
        "glVertexAttrib4Nusv",
        "glVertexAttrib4bv",
        "glVertexAttrib4d",
        "glVertexAttrib4dv",
        "glVertexAttrib4f",
        "glVertexAttrib4fv",
        "glVertexAttrib4iv",
        "glVertexAttrib4s",
        "glVertexAttrib4sv",
        "glVertexAttrib4ubv",
        "glVertexAttrib4uiv",
        "glVertexAttrib4usv",
        "glVertexAttribPointer"
    };
    static const PCSTR OpenGl3Procedures[] =
    {
        "glGenVertexArrays",
        "glBindVertexArray",
        "glGetStringi",
        "glMapBufferRange"
    };
    static const PCSTR WineD3dCapabilityProcedures[] =
    {
        "glBindAttribLocation",
        "glDisableVertexAttribArray",
        "glEnableVertexAttribArray",
        "glGetActiveAttrib",
        "glGetAttribLocation",
        "glVertexAttrib4f",
        "glVertexAttribPointer"
    };
    WNDCLASSW WindowClass;
    PIXELFORMATDESCRIPTOR PixelFormatDescriptor;
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start;
    LARGE_INTEGER End;
    const GLubyte *Vendor;
    const GLubyte *Renderer;
    const GLubyte *Version;
    const GLubyte *Extensions;
    PCSTR MissingOpenGl2 = NULL;
    PCSTR MissingOpenGl12 = NULL;
    PCSTR MissingOpenGl13 = NULL;
    PCSTR MissingOpenGl14 = NULL;
    PCSTR MissingOpenGl15 = NULL;
    PCSTR MissingOpenGl20 = NULL;
    PCSTR MissingOpenGl3;
    PCSTR MissingShader;
    HINSTANCE Instance;
    HWND Window = NULL;
    HDC DeviceContext = NULL;
    HGLRC RenderContext = NULL;
    BYTE Pixel[4];
    BYTE SecondPixel[4];
    BYTE ThirdPixel[4];
    INT PixelFormat;
    INT PixelFormatCount;
    ULONG Frame;
    ULONG Frames = 120;
    ULONGLONG ElapsedMicroseconds;
    BOOL Registered = FALSE;
    BOOL DoubleBuffered;
    BOOL ShaderMilestone;
    BOOL OpenGl12;
    BOOL OpenGl13;
    BOOL OpenGl14;
    BOOL OpenGl15;
    BOOL OpenGl20;
    BOOL OpenGl2;
    BOOL OpenGl3;
    UINT OpenGlMajor = 0;
    UINT OpenGlMinor = 0;
    PFNGLATTACHSHADERPROC AttachShader = NULL;
    PFNGLBINDATTRIBLOCATIONPROC BindAttribLocation = NULL;
    PFNGLCOMPILESHADERPROC CompileShader = NULL;
    PFNGLCREATEPROGRAMPROC CreateProgram = NULL;
    PFNGLCREATESHADERPROC CreateShader = NULL;
    PFNGLDELETEPROGRAMPROC DeleteProgram = NULL;
    PFNGLDELETESHADERPROC DeleteShader = NULL;
    PFNGLDETACHSHADERPROC DetachShader = NULL;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray = NULL;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = NULL;
    PFNGLGETACTIVEATTRIBPROC GetActiveAttrib = NULL;
    PFNGLGETATTACHEDSHADERSPROC GetAttachedShaders = NULL;
    PFNGLGETATTRIBLOCATIONPROC GetAttribLocation = NULL;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = NULL;
    PFNGLGETPROGRAMIVPROC GetProgramiv = NULL;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = NULL;
    PFNGLGETSHADERIVPROC GetShaderiv = NULL;
    PFNGLGETSHADERSOURCEPROC GetShaderSource = NULL;
    PFNGLISPROGRAMPROC IsProgram = NULL;
    PFNGLISSHADERPROC IsShader = NULL;
    PFNGLLINKPROGRAMPROC LinkProgram = NULL;
    PFNGLSHADERSOURCEPROC ShaderSource = NULL;
    PFNGLUSEPROGRAMPROC UseProgram = NULL;
    PFNGLVALIDATEPROGRAMPROC ValidateProgram = NULL;
    PFNGLVERTEXATTRIB4FPROC VertexAttrib4f = NULL;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = NULL;
    GLuint RejectedShader = 0;
    GLuint VertexShader = 0;
    GLuint FragmentShader = 0;
    GLuint Program = 0;
    GLuint AttachedShaders[2];
    GLsizei AttachedCount = 0;
    GLsizei TextLength = 0;
    GLint VertexCompiled = GL_FALSE;
    GLint FragmentCompiled = GL_FALSE;
    GLint LinkStatus = GL_FALSE;
    GLint ValidateStatus = GL_FALSE;
    GLint DeleteStatus = GL_FALSE;
    GLint InfoLogLength = 0;
    GLint ActiveAttributes = 0;
    GLint PositionLocation = -1;
    GLint ColorLocation = -1;
    GLint ActiveAttributeSize = 0;
    GLenum ActiveAttributeType = 0;
    CHAR ShaderLog[256];
    CHAR ShaderSourceCopy[256];
    CHAR ActiveAttributeName[32];
    ULONG ProgramHardwareTriangleBase = 0;
    ULONG ProgramTriangleBase = 0;
    PRPI5VC4_GET_OGL_STATS GetStats;
    RPI5VC4_OGL_STATS Stats;
    ULONG BenchmarkHardwareTriangleBase = 0;
    ULONG BenchmarkSoftwareTriangleBase = 0;
    ULONG BenchmarkTriangleFailureBase = 0;
    BOOL Success = TRUE;

    Rpi5DiagPrint("RPI5_GL_BASELINE_BEGIN required=1.1 frames=%lu mode=%s\n",
                  Frames,
                  FullRegression ? "full" : "focused");

    Instance = GetModuleHandleW(NULL);
    ZeroMemory(&WindowClass, sizeof(WindowClass));
    WindowClass.style = CS_OWNDC;
    WindowClass.lpfnWndProc = Rpi5OpenGlWindowProc;
    WindowClass.hInstance = Instance;
    WindowClass.lpszClassName = ClassName;
    if (RegisterClassW(&WindowClass))
    {
        Registered = TRUE;
    }
    else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_001_WINDOW_FAIL error=%lu\n",
                      GetLastError());
        return FALSE;
    }

    Window = CreateWindowExW(0,
                             ClassName,
                             L"RPi5 OpenGL baseline",
                             WS_POPUP | WS_VISIBLE,
                             0,
                             0,
                             128,
                             128,
                             NULL,
                             NULL,
                             Instance,
                             NULL);
    if (!Window)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_001_WINDOW_FAIL error=%lu\n",
                      GetLastError());
        Success = FALSE;
        goto Cleanup;
    }
    ShowWindow(Window, SW_SHOW);
    UpdateWindow(Window);
    DeviceContext = GetDC(Window);
    if (!DeviceContext)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_001_WINDOW_FAIL dc-error=%lu\n",
                      GetLastError());
        Success = FALSE;
        goto Cleanup;
    }
    if (FullRegression)
        Rpi5DiagPrint("RPI5_GL_TEST_001_WINDOW_PASS\n");

    ZeroMemory(&PixelFormatDescriptor, sizeof(PixelFormatDescriptor));
    PixelFormatDescriptor.nSize = sizeof(PixelFormatDescriptor);
    PixelFormatDescriptor.nVersion = 1;
    PixelFormatDescriptor.dwFlags = PFD_DRAW_TO_WINDOW |
                                    PFD_SUPPORT_OPENGL |
                                    PFD_DOUBLEBUFFER;
    PixelFormatDescriptor.iPixelType = PFD_TYPE_RGBA;
    PixelFormatDescriptor.cColorBits = 32;
    PixelFormatDescriptor.cDepthBits = 24;
    PixelFormatDescriptor.cStencilBits = 8;
    PixelFormatDescriptor.iLayerType = PFD_MAIN_PLANE;
    PixelFormatCount = DescribePixelFormat(DeviceContext, 0, 0, NULL);
    PixelFormat = ChoosePixelFormat(DeviceContext, &PixelFormatDescriptor);
    if (PixelFormat <= 0 ||
        !DescribePixelFormat(DeviceContext,
                             PixelFormat,
                             sizeof(PixelFormatDescriptor),
                             &PixelFormatDescriptor) ||
        !SetPixelFormat(DeviceContext,
                        PixelFormat,
                        &PixelFormatDescriptor))
    {
        Rpi5DiagPrint("RPI5_GL_TEST_002_PIXEL_FORMAT_FAIL count=%d selected=%d error=%lu\n",
                      PixelFormatCount, PixelFormat, GetLastError());
        Success = FALSE;
        goto Cleanup;
    }
    DoubleBuffered = !!(PixelFormatDescriptor.dwFlags & PFD_DOUBLEBUFFER);
    if (FullRegression)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_002_PIXEL_FORMAT_PASS count=%d selected=%d flags=0x%08lx color=%u depth=%u stencil=%u generic=%u accelerated=%u\n",
                      PixelFormatCount,
                      PixelFormat,
                      PixelFormatDescriptor.dwFlags,
                      PixelFormatDescriptor.cColorBits,
                      PixelFormatDescriptor.cDepthBits,
                      PixelFormatDescriptor.cStencilBits,
                      !!(PixelFormatDescriptor.dwFlags & PFD_GENERIC_FORMAT),
                      !!(PixelFormatDescriptor.dwFlags & PFD_GENERIC_ACCELERATED));
    }

    RenderContext = wglCreateContext(DeviceContext);
    if (!RenderContext || !wglMakeCurrent(DeviceContext, RenderContext))
    {
        Rpi5DiagPrint("RPI5_GL_TEST_003_CONTEXT_FAIL context=%p error=%lu\n",
                      RenderContext, GetLastError());
        Success = FALSE;
        goto Cleanup;
    }
    if (FullRegression)
        Rpi5DiagPrint("RPI5_GL_TEST_003_CONTEXT_PASS\n");

    Vendor = glGetString(GL_VENDOR);
    Renderer = glGetString(GL_RENDERER);
    Version = glGetString(GL_VERSION);
    Extensions = glGetString(GL_EXTENSIONS);
    if (!Vendor || !Renderer || !Version || !Extensions)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_004_IDENTITY_FAIL error=0x%04x\n",
                      glGetError());
        Success = FALSE;
        goto Cleanup;
    }
    if (FullRegression)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_004_IDENTITY_PASS vendor=%s renderer=%s version=%s\n",
                      Vendor, Renderer, Version);
        Rpi5DiagPrint("RPI5_GL_EXTENSIONS %s\n", Extensions);
    }
    sscanf((PCSTR)Version, "%u.%u", &OpenGlMajor, &OpenGlMinor);

    glViewport(0, 0, 128, 128);
    glDrawBuffer(DoubleBuffered ? GL_BACK : GL_FRONT);
    glReadBuffer(DoubleBuffered ? GL_BACK : GL_FRONT);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    GetStats = (PRPI5VC4_GET_OGL_STATS)
        wglGetProcAddress("Rpi5Vc4GetStats");
    ShaderMilestone = Rpi5OpenGlProcSetAvailable(
        ShaderProcedures,
        ARRAYSIZE(ShaderProcedures),
        &MissingShader);
    if (!FullRegression)
    {
        Rpi5DiagPrint("RPI5_GL_FOCUS_BEGIN first-test=21 skipped=5-20 setup=ready shader-entrypoints=%u\n",
                      ShaderMilestone);
        goto FocusedTests;
    }

    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    ZeroMemory(Pixel, sizeof(Pixel));
    glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    if (glGetError() != GL_NO_ERROR ||
        Pixel[0] > 32 || Pixel[1] > 32 || Pixel[2] < 224)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_005_CLEAR_READBACK_FAIL rgba=%u,%u,%u,%u\n",
                      Pixel[0], Pixel[1], Pixel[2], Pixel[3]);
        Success = FALSE;
    }
    else
    {
        Rpi5DiagPrint("RPI5_GL_TEST_005_CLEAR_READBACK_PASS rgba=%u,%u,%u,%u\n",
                      Pixel[0], Pixel[1], Pixel[2], Pixel[3]);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex2f(-0.8f, -0.8f);
    glVertex2f(0.8f, -0.8f);
    glVertex2f(0.0f, 0.8f);
    glEnd();
    glFinish();
    ZeroMemory(Pixel, sizeof(Pixel));
    ZeroMemory(SecondPixel, sizeof(SecondPixel));
    glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, SecondPixel);
    if (glGetError() != GL_NO_ERROR ||
        Pixel[0] < 224 || Pixel[1] > 32 || Pixel[2] > 32 ||
        SecondPixel[0] > 32 || SecondPixel[1] > 32 || SecondPixel[2] > 32)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_006_TRIANGLE_READBACK_FAIL center=%u,%u,%u,%u corner=%u,%u,%u,%u\n",
                      Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                      SecondPixel[0], SecondPixel[1],
                      SecondPixel[2], SecondPixel[3]);
        Success = FALSE;
    }
    else
    {
        Rpi5DiagPrint("RPI5_GL_TEST_006_TRIANGLE_READBACK_PASS center=%u,%u,%u,%u corner=%u,%u,%u,%u\n",
                      Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                      SecondPixel[0], SecondPixel[1],
                      SecondPixel[2], SecondPixel[3]);
    }

    if (DoubleBuffered && !SwapBuffers(DeviceContext))
    {
        Rpi5DiagPrint("RPI5_GL_TEST_007_SWAP_FAIL error=%lu\n", GetLastError());
        Success = FALSE;
    }
    else
    {
        Rpi5DiagPrint("RPI5_GL_TEST_007_SWAP_PASS double-buffered=%u\n",
                      DoubleBuffered);
    }

    ZeroMemory(&Stats, sizeof(Stats));
    Stats.Size = sizeof(Stats);
    if (!GetStats ||
        !GetStats(&Stats) ||
        Stats.Version != RPI5VC4_OGL_STATS_VERSION ||
        Stats.HardwareClearCount < 2 ||
        Stats.HardwareClearFailureCount != 0)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_008_V3D_CLEAR_FAIL proc=%p hw=%lu sw=%lu failures=%lu status=%lu size=%lux%lu\n",
                      GetStats,
                      Stats.HardwareClearCount,
                      Stats.SoftwareClearCount,
                      Stats.HardwareClearFailureCount,
                      Stats.LastHardwareStatus,
                      Stats.LastWidth,
                      Stats.LastHeight);
        Success = FALSE;
    }
    else
    {
        Rpi5DiagPrint("RPI5_GL_TEST_008_V3D_CLEAR_PASS hw=%lu sw=%lu failures=%lu status=%lu size=%lux%lu\n",
                      Stats.HardwareClearCount,
                      Stats.SoftwareClearCount,
                      Stats.HardwareClearFailureCount,
                      Stats.LastHardwareStatus,
                      Stats.LastWidth,
                      Stats.LastHeight);
    }

    if (Stats.HardwareTriangleCount != 1 ||
        Stats.SoftwareTriangleCount != 0 ||
        Stats.HardwareTriangleFailureCount != 0 ||
        Stats.LastTriangleStatus != RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS ||
        Stats.LastTriangleWidth != 128 ||
        Stats.LastTriangleHeight != 128 ||
        Stats.LastTriangleCoveredPixels == 0)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_009_V3D_TRIANGLE_FAIL hw=%lu sw=%lu failures=%lu status=%lu size=%lux%lu covered=%lu\n",
                      Stats.HardwareTriangleCount,
                      Stats.SoftwareTriangleCount,
                      Stats.HardwareTriangleFailureCount,
                      Stats.LastTriangleStatus,
                      Stats.LastTriangleWidth,
                      Stats.LastTriangleHeight,
                      Stats.LastTriangleCoveredPixels);
        Success = FALSE;
    }
    else
    {
        Rpi5DiagPrint("RPI5_GL_TEST_009_V3D_TRIANGLE_PASS hw=%lu sw=%lu failures=%lu status=%lu size=%lux%lu covered=%lu\n",
                      Stats.HardwareTriangleCount,
                      Stats.SoftwareTriangleCount,
                      Stats.HardwareTriangleFailureCount,
                      Stats.LastTriangleStatus,
                      Stats.LastTriangleWidth,
                      Stats.LastTriangleHeight,
                      Stats.LastTriangleCoveredPixels);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex2f(-0.9f, -0.8f);
    glVertex2f(-0.1f, -0.8f);
    glVertex2f(-0.5f, 0.8f);
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex2f(0.1f, -0.8f);
    glVertex2f(0.9f, -0.8f);
    glVertex2f(0.5f, 0.8f);
    glEnd();
    glFinish();
    ZeroMemory(Pixel, sizeof(Pixel));
    ZeroMemory(SecondPixel, sizeof(SecondPixel));
    glReadPixels(32, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
    glReadPixels(96, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, SecondPixel);
    ZeroMemory(&Stats, sizeof(Stats));
    Stats.Size = sizeof(Stats);
    if (glGetError() != GL_NO_ERROR ||
        !GetStats ||
        !GetStats(&Stats) ||
        Pixel[0] < 224 || Pixel[1] > 32 || Pixel[2] > 32 ||
        SecondPixel[0] > 32 || SecondPixel[1] < 224 || SecondPixel[2] > 32 ||
        Stats.HardwareTriangleCount != 2 ||
        Stats.SoftwareTriangleCount != 1 ||
        Stats.HardwareTriangleFailureCount != 0)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_010_TRIANGLE_COMPOSE_FAIL left=%u,%u,%u,%u right=%u,%u,%u,%u hw=%lu sw=%lu failures=%lu\n",
                      Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                      SecondPixel[0], SecondPixel[1],
                      SecondPixel[2], SecondPixel[3],
                      Stats.HardwareTriangleCount,
                      Stats.SoftwareTriangleCount,
                      Stats.HardwareTriangleFailureCount);
        Success = FALSE;
    }
    else
    {
        Rpi5DiagPrint("RPI5_GL_TEST_010_TRIANGLE_COMPOSE_PASS left=%u,%u,%u,%u right=%u,%u,%u,%u hw=%lu sw=%lu failures=%lu\n",
                      Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                      SecondPixel[0], SecondPixel[1],
                      SecondPixel[2], SecondPixel[3],
                      Stats.HardwareTriangleCount,
                      Stats.SoftwareTriangleCount,
                      Stats.HardwareTriangleFailureCount);
    }
    if (!ShaderMilestone)
    {
        Rpi5DiagPrint("RPI5_GL_TEST_011_SHADER_ENTRYPOINTS_FAIL missing=%s\n",
                      MissingShader ? MissingShader : "unknown");
        Success = FALSE;
    }
    else
    {
        Rpi5DiagPrint("RPI5_GL_TEST_011_SHADER_ENTRYPOINTS_PASS count=%lu\n",
                      (ULONG)ARRAYSIZE(ShaderProcedures));
    }

    if (ShaderMilestone)
    {
        AttachShader = (PFNGLATTACHSHADERPROC)
            wglGetProcAddress("glAttachShader");
        CompileShader = (PFNGLCOMPILESHADERPROC)
            wglGetProcAddress("glCompileShader");
        CreateProgram = (PFNGLCREATEPROGRAMPROC)
            wglGetProcAddress("glCreateProgram");
        CreateShader = (PFNGLCREATESHADERPROC)
            wglGetProcAddress("glCreateShader");
        DeleteProgram = (PFNGLDELETEPROGRAMPROC)
            wglGetProcAddress("glDeleteProgram");
        DeleteShader = (PFNGLDELETESHADERPROC)
            wglGetProcAddress("glDeleteShader");
        DetachShader = (PFNGLDETACHSHADERPROC)
            wglGetProcAddress("glDetachShader");
        GetAttachedShaders = (PFNGLGETATTACHEDSHADERSPROC)
            wglGetProcAddress("glGetAttachedShaders");
        GetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)
            wglGetProcAddress("glGetProgramInfoLog");
        GetProgramiv = (PFNGLGETPROGRAMIVPROC)
            wglGetProcAddress("glGetProgramiv");
        GetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)
            wglGetProcAddress("glGetShaderInfoLog");
        GetShaderiv = (PFNGLGETSHADERIVPROC)
            wglGetProcAddress("glGetShaderiv");
        GetShaderSource = (PFNGLGETSHADERSOURCEPROC)
            wglGetProcAddress("glGetShaderSource");
        IsProgram = (PFNGLISPROGRAMPROC)
            wglGetProcAddress("glIsProgram");
        IsShader = (PFNGLISSHADERPROC)
            wglGetProcAddress("glIsShader");
        LinkProgram = (PFNGLLINKPROGRAMPROC)
            wglGetProcAddress("glLinkProgram");
        ShaderSource = (PFNGLSHADERSOURCEPROC)
            wglGetProcAddress("glShaderSource");
        UseProgram = (PFNGLUSEPROGRAMPROC)
            wglGetProcAddress("glUseProgram");
        ValidateProgram = (PFNGLVALIDATEPROGRAMPROC)
            wglGetProcAddress("glValidateProgram");

        RejectedShader = CreateShader(GL_VERTEX_SHADER);
        if (RejectedShader != 0)
        {
            static const GLchar *RejectedSource =
                "#version 120\nvoid main(void) { gl_Position = vec4(0.0); }";

            ShaderSource(RejectedShader, 1, &RejectedSource, NULL);
            CompileShader(RejectedShader);
            VertexCompiled = GL_TRUE;
            ZeroMemory(ShaderLog, sizeof(ShaderLog));
            TextLength = 0;
            GetShaderiv(RejectedShader, GL_COMPILE_STATUS,
                        &VertexCompiled);
            GetShaderInfoLog(RejectedShader,
                             sizeof(ShaderLog),
                             &TextLength,
                             ShaderLog);
        }
        if (RejectedShader == 0 || VertexCompiled != GL_FALSE ||
            TextLength == 0 || ShaderLog[0] == '\0' ||
            glGetError() != GL_NO_ERROR)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_012_SHADER_REJECT_FAIL shader=%lu compiled=%ld log=%s\n",
                          RejectedShader, VertexCompiled, ShaderLog);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_GL_TEST_012_SHADER_REJECT_PASS shader=%lu log=%s\n",
                          RejectedShader, ShaderLog);
        }
        if (RejectedShader != 0)
        {
            DeleteShader(RejectedShader);
            if (IsShader(RejectedShader))
                Success = FALSE;
            RejectedShader = 0;
        }

        VertexShader = CreateShader(GL_VERTEX_SHADER);
        FragmentShader = CreateShader(GL_FRAGMENT_SHADER);
        if (VertexShader != 0 && FragmentShader != 0)
        {
            static const GLchar *VertexSource =
                "#version 120\n"
                "void main(void) {\n"
                "    gl_Position = ftransform();\n"
                "    gl_FrontColor = gl_Color;\n"
                "}\n";
            static const GLchar *FragmentSource =
                "#version 120\n"
                "void main(void) {\n"
                "    gl_FragColor = gl_Color;\n"
                "}\n";
            static const GLchar *UncompiledReplacement =
                "#version 120\nvoid main(void) { gl_Position = vec4(0.0); }";

            ShaderSource(VertexShader, 1, &VertexSource, NULL);
            ShaderSource(FragmentShader, 1, &FragmentSource, NULL);
            CompileShader(VertexShader);
            CompileShader(FragmentShader);
            VertexCompiled = GL_FALSE;
            FragmentCompiled = GL_FALSE;
            GetShaderiv(VertexShader, GL_COMPILE_STATUS,
                        &VertexCompiled);
            GetShaderiv(FragmentShader, GL_COMPILE_STATUS,
                        &FragmentCompiled);
            ZeroMemory(ShaderSourceCopy, sizeof(ShaderSourceCopy));
            TextLength = 0;
            GetShaderSource(VertexShader,
                            sizeof(ShaderSourceCopy),
                            &TextLength,
                            ShaderSourceCopy);
            if (lstrcmpA(ShaderSourceCopy, VertexSource) != 0)
                VertexCompiled = GL_FALSE;
            ShaderSource(VertexShader, 1, &UncompiledReplacement, NULL);
            GetShaderiv(VertexShader, GL_COMPILE_STATUS,
                        &VertexCompiled);
        }
        if (VertexShader == 0 || FragmentShader == 0 ||
            VertexCompiled != GL_TRUE || FragmentCompiled != GL_TRUE ||
            glGetError() != GL_NO_ERROR)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_013_SHADER_COMPILE_FAIL vertex=%lu/%ld fragment=%lu/%ld source-bytes=%ld\n",
                          VertexShader, VertexCompiled,
                          FragmentShader, FragmentCompiled,
                          TextLength);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_GL_TEST_013_SHADER_COMPILE_PASS vertex=%lu fragment=%lu source-bytes=%ld\n",
                          VertexShader, FragmentShader, TextLength);
        }

        if (VertexShader != 0 && FragmentShader != 0 &&
            VertexCompiled == GL_TRUE && FragmentCompiled == GL_TRUE)
        {
            Program = CreateProgram();
            if (Program != 0)
            {
                AttachShader(Program, VertexShader);
                AttachShader(Program, FragmentShader);
                LinkProgram(Program);
                ValidateProgram(Program);
                LinkStatus = GL_FALSE;
                ValidateStatus = GL_FALSE;
                AttachedCount = 0;
                ZeroMemory(AttachedShaders, sizeof(AttachedShaders));
                GetProgramiv(Program, GL_LINK_STATUS, &LinkStatus);
                GetProgramiv(Program, GL_VALIDATE_STATUS,
                             &ValidateStatus);
                GetProgramiv(Program, GL_INFO_LOG_LENGTH,
                             &InfoLogLength);
                DetachShader(Program, VertexShader);
                GetProgramiv(Program, GL_LINK_STATUS, &LinkStatus);
                AttachShader(Program, VertexShader);
                GetAttachedShaders(Program,
                                   ARRAYSIZE(AttachedShaders),
                                   &AttachedCount,
                                   AttachedShaders);
            }
        }
        if (Program == 0 || LinkStatus != GL_TRUE ||
            ValidateStatus != GL_TRUE || InfoLogLength != 0 ||
            AttachedCount != 2 ||
            !IsProgram(Program) || glGetError() != GL_NO_ERROR)
        {
            ZeroMemory(ShaderLog, sizeof(ShaderLog));
            TextLength = 0;
            if (Program != 0)
            {
                GetProgramInfoLog(Program,
                                  sizeof(ShaderLog),
                                  &TextLength,
                                  ShaderLog);
            }
            Rpi5DiagPrint("RPI5_GL_TEST_014_PROGRAM_LINK_FAIL program=%lu linked=%ld validated=%ld info-log-length=%ld attached=%ld log=%s\n",
                          Program, LinkStatus, ValidateStatus,
                          InfoLogLength, AttachedCount, ShaderLog);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_GL_TEST_014_PROGRAM_LINK_PASS program=%lu linked=%ld validated=%ld attached=%ld\n",
                          Program, LinkStatus, ValidateStatus,
                          AttachedCount);
        }

        ZeroMemory(&Stats, sizeof(Stats));
        Stats.Size = sizeof(Stats);
        if (GetStats && GetStats(&Stats))
        {
            ProgramHardwareTriangleBase =
                Stats.HardwareProgramTriangleCount;
            ProgramTriangleBase = Stats.HardwareTriangleCount;
        }
        if (Program != 0 && LinkStatus == GL_TRUE)
        {
            UseProgram(Program);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glScalef(0.5f, 0.5f, 1.0f);
            glBegin(GL_TRIANGLES);
            glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
            glVertex4f(-0.8f, -0.8f, 0.0f, 1.0f);
            glVertex4f(0.8f, -0.8f, 0.0f, 1.0f);
            glVertex4f(0.0f, 0.8f, 0.0f, 1.0f);
            glEnd();
            glFinish();
            ZeroMemory(Pixel, sizeof(Pixel));
            ZeroMemory(SecondPixel, sizeof(SecondPixel));
            glReadPixels(64, 64, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
            glReadPixels(85, 64, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, SecondPixel);
            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Size = sizeof(Stats);
            if (!GetStats || !GetStats(&Stats))
                ZeroMemory(&Stats, sizeof(Stats));
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
        }
        if (Program == 0 ||
            Pixel[0] < 224 || Pixel[1] > 32 || Pixel[2] > 32 ||
            SecondPixel[0] > 32 || SecondPixel[1] > 32 ||
            SecondPixel[2] > 32 ||
            Stats.HardwareTriangleCount != ProgramTriangleBase + 1 ||
            Stats.HardwareProgramTriangleCount !=
                ProgramHardwareTriangleBase + 1 ||
            Stats.LastProgramTriangleName != Program ||
            glGetError() != GL_NO_ERROR)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_015_PROGRAM_V3D_DRAW_FAIL program=%lu center=%u,%u,%u,%u transformed-outside=%u,%u,%u,%u hw=%lu program-hw=%lu last-program=%lu\n",
                          Program,
                          Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                          SecondPixel[0], SecondPixel[1],
                          SecondPixel[2], SecondPixel[3],
                          Stats.HardwareTriangleCount,
                          Stats.HardwareProgramTriangleCount,
                          Stats.LastProgramTriangleName);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_GL_TEST_015_PROGRAM_V3D_DRAW_PASS program=%lu center=%u,%u,%u,%u transformed-outside=%u,%u,%u,%u hw=%lu program-hw=%lu\n",
                          Program,
                          Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                          SecondPixel[0], SecondPixel[1],
                          SecondPixel[2], SecondPixel[3],
                          Stats.HardwareTriangleCount,
                          Stats.HardwareProgramTriangleCount);
        }

        if (VertexShader != 0 && FragmentShader != 0)
        {
            DeleteShader(VertexShader);
            DeleteShader(FragmentShader);
            DeleteStatus = GL_FALSE;
            GetShaderiv(VertexShader, GL_DELETE_STATUS, &DeleteStatus);
            if (DeleteStatus != GL_TRUE || !IsShader(VertexShader) ||
                !IsShader(FragmentShader))
            {
                Success = FALSE;
            }
        }
        if (UseProgram != NULL)
            UseProgram(0);
        if (Program != 0)
            DeleteProgram(Program);
        if (Program == 0 || IsProgram(Program) ||
            IsShader(VertexShader) || IsShader(FragmentShader) ||
            glGetError() != GL_NO_ERROR)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_016_PROGRAM_LIFETIME_FAIL program=%lu vertex=%lu fragment=%lu\n",
                          Program, VertexShader, FragmentShader);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_GL_TEST_016_PROGRAM_LIFETIME_PASS program=%lu vertex=%lu fragment=%lu\n",
                          Program, VertexShader, FragmentShader);
        }
        Program = 0;
        VertexShader = 0;
        FragmentShader = 0;
    }

    if (ShaderMilestone &&
        Rpi5OpenGlProcSetAvailable(
            WineD3dCapabilityProcedures,
            ARRAYSIZE(WineD3dCapabilityProcedures),
            &MissingOpenGl2))
    {
        static const GLchar *VertexSources[] =
        {
            "#version 120\n"
            "attribute vec4 pos;\n"
            "attribute vec4 color;\n"
            "varying vec4 out_color;\n",
            "void main()\n"
            "{\n"
            "    gl_Position = pos;\n"
            "    out_color = color;\n"
            "}\n"
        };
        static const GLchar *FragmentSource =
            "#version 120\n"
            "varying vec4 out_color;\n"
            "void main()\n"
            "{\n"
            "    gl_FragData[0] = out_color;\n"
            "}\n";
        static const GLfloat Positions[9] =
        {
            -0.8f, -0.8f, 0.0f,
             0.8f, -0.8f, 0.0f,
             0.0f,  0.8f, 0.0f
        };
        static const GLfloat QuadPositions[12] =
        {
            -0.75f, -0.75f, 0.0f,
             0.75f, -0.75f, 0.0f,
            -0.75f,  0.75f, 0.0f,
             0.75f,  0.75f, 0.0f
        };

        BindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)
            wglGetProcAddress("glBindAttribLocation");
        DisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)
            wglGetProcAddress("glDisableVertexAttribArray");
        EnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)
            wglGetProcAddress("glEnableVertexAttribArray");
        GetActiveAttrib = (PFNGLGETACTIVEATTRIBPROC)
            wglGetProcAddress("glGetActiveAttrib");
        GetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)
            wglGetProcAddress("glGetAttribLocation");
        VertexAttrib4f = (PFNGLVERTEXATTRIB4FPROC)
            wglGetProcAddress("glVertexAttrib4f");
        VertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)
            wglGetProcAddress("glVertexAttribPointer");

        VertexShader = CreateShader(GL_VERTEX_SHADER);
        FragmentShader = CreateShader(GL_FRAGMENT_SHADER);
        Program = CreateProgram();
        if (VertexShader != 0 && FragmentShader != 0 && Program != 0)
        {
            ShaderSource(VertexShader,
                         ARRAYSIZE(VertexSources),
                         VertexSources,
                         NULL);
            ShaderSource(FragmentShader, 1, &FragmentSource, NULL);
            AttachShader(Program, VertexShader);
            AttachShader(Program, FragmentShader);
            BindAttribLocation(Program, 0, "pos");
            BindAttribLocation(Program, 1, "color");
            CompileShader(VertexShader);
            CompileShader(FragmentShader);
            LinkProgram(Program);
            GetShaderiv(VertexShader, GL_COMPILE_STATUS, &VertexCompiled);
            GetShaderiv(FragmentShader, GL_COMPILE_STATUS,
                        &FragmentCompiled);
            GetProgramiv(Program, GL_LINK_STATUS, &LinkStatus);
            GetProgramiv(Program, GL_ACTIVE_ATTRIBUTES,
                         &ActiveAttributes);
            PositionLocation = GetAttribLocation(Program, "pos");
            ColorLocation = GetAttribLocation(Program, "color");
            ZeroMemory(ActiveAttributeName,
                       sizeof(ActiveAttributeName));
            TextLength = 0;
            GetActiveAttrib(Program,
                            0,
                            sizeof(ActiveAttributeName),
                            &TextLength,
                            &ActiveAttributeSize,
                            &ActiveAttributeType,
                            ActiveAttributeName);

            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Size = sizeof(Stats);
            if (GetStats && GetStats(&Stats))
            {
                ProgramHardwareTriangleBase =
                    Stats.HardwareProgramTriangleCount;
                ProgramTriangleBase = Stats.HardwareTriangleCount;
            }
            UseProgram(Program);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0,
                                Positions);
            VertexAttrib4f(1, 1.0f, 0.0f, 0.0f, 1.0f);
            EnableVertexAttribArray(0);
            DisableVertexAttribArray(1);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glFinish();
            DisableVertexAttribArray(0);
            ZeroMemory(Pixel, sizeof(Pixel));
            ZeroMemory(SecondPixel, sizeof(SecondPixel));
            glReadPixels(64, 64, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
            glReadPixels(0, 0, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, SecondPixel);
            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Size = sizeof(Stats);
            if (!GetStats || !GetStats(&Stats))
                ZeroMemory(&Stats, sizeof(Stats));
        }

        if (VertexShader == 0 || FragmentShader == 0 || Program == 0 ||
            VertexCompiled != GL_TRUE || FragmentCompiled != GL_TRUE ||
            LinkStatus != GL_TRUE || ActiveAttributes != 2 ||
            PositionLocation != 0 || ColorLocation != 1 ||
            lstrcmpA(ActiveAttributeName, "pos") != 0 ||
            ActiveAttributeSize != 1 ||
            ActiveAttributeType != GL_FLOAT_VEC4 ||
            Pixel[0] < 224 || Pixel[1] > 32 || Pixel[2] > 32 ||
            SecondPixel[0] > 32 || SecondPixel[1] > 32 ||
            SecondPixel[2] > 32 ||
            Stats.HardwareTriangleCount != ProgramTriangleBase + 1 ||
            Stats.HardwareProgramTriangleCount !=
                ProgramHardwareTriangleBase + 1 ||
            Stats.LastProgramTriangleName != Program ||
            glGetError() != GL_NO_ERROR)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_017_WINED3D_CAP_SHADER_FAIL program=%lu shaders=%lu/%lu compiled=%ld/%ld linked=%ld attributes=%ld locations=%ld/%ld active=%s/%ld/0x%04x center=%u,%u,%u,%u corner=%u,%u,%u,%u hw=%lu program-hw=%lu last-program=%lu\n",
                          Program, VertexShader, FragmentShader,
                          VertexCompiled, FragmentCompiled, LinkStatus,
                          ActiveAttributes, PositionLocation,
                          ColorLocation, ActiveAttributeName,
                          ActiveAttributeSize, ActiveAttributeType,
                          Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                          SecondPixel[0], SecondPixel[1],
                          SecondPixel[2], SecondPixel[3],
                          Stats.HardwareTriangleCount,
                          Stats.HardwareProgramTriangleCount,
                          Stats.LastProgramTriangleName);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_GL_TEST_017_WINED3D_CAP_SHADER_PASS program=%lu attributes=%ld locations=%ld/%ld center=%u,%u,%u,%u corner=%u,%u,%u,%u hw=%lu program-hw=%lu\n",
                          Program, ActiveAttributes, PositionLocation,
                          ColorLocation,
                          Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                          SecondPixel[0], SecondPixel[1],
                          SecondPixel[2], SecondPixel[3],
                          Stats.HardwareTriangleCount,
                          Stats.HardwareProgramTriangleCount);
        }

        ProgramHardwareTriangleBase = Stats.HardwareProgramTriangleCount;
        ProgramTriangleBase = Stats.HardwareTriangleCount;
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0,
                            QuadPositions);
        VertexAttrib4f(1, 0.0f, 1.0f, 0.0f, 1.0f);
        EnableVertexAttribArray(0);
        DisableVertexAttribArray(1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
        DisableVertexAttribArray(0);
        ZeroMemory(Pixel, sizeof(Pixel));
        ZeroMemory(SecondPixel, sizeof(SecondPixel));
        ZeroMemory(ThirdPixel, sizeof(ThirdPixel));
        glReadPixels(32, 32, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, Pixel);
        glReadPixels(96, 96, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, SecondPixel);
        glReadPixels(0, 0, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, ThirdPixel);
        ZeroMemory(&Stats, sizeof(Stats));
        Stats.Size = sizeof(Stats);
        if (!GetStats || !GetStats(&Stats))
            ZeroMemory(&Stats, sizeof(Stats));

        if (Pixel[0] > 32 || Pixel[1] < 224 || Pixel[2] > 32 ||
            SecondPixel[0] > 32 || SecondPixel[1] < 224 ||
            SecondPixel[2] > 32 ||
            ThirdPixel[0] > 32 || ThirdPixel[1] > 32 ||
            ThirdPixel[2] > 32 ||
            Stats.HardwareTriangleCount != ProgramTriangleBase + 1 ||
            Stats.HardwareProgramTriangleCount !=
                ProgramHardwareTriangleBase + 1 ||
            Stats.LastProgramTriangleName != Program ||
            glGetError() != GL_NO_ERROR)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_018_WINED3D_QUAD_STRIP_FAIL program=%lu lower=%u,%u,%u,%u upper=%u,%u,%u,%u corner=%u,%u,%u,%u hw=%lu program-hw=%lu last-program=%lu\n",
                          Program,
                          Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                          SecondPixel[0], SecondPixel[1],
                          SecondPixel[2], SecondPixel[3],
                          ThirdPixel[0], ThirdPixel[1],
                          ThirdPixel[2], ThirdPixel[3],
                          Stats.HardwareTriangleCount,
                          Stats.HardwareProgramTriangleCount,
                          Stats.LastProgramTriangleName);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_GL_TEST_018_WINED3D_QUAD_STRIP_PASS program=%lu lower=%u,%u,%u,%u upper=%u,%u,%u,%u corner=%u,%u,%u,%u hw=%lu program-hw=%lu\n",
                          Program,
                          Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                          SecondPixel[0], SecondPixel[1],
                          SecondPixel[2], SecondPixel[3],
                          ThirdPixel[0], ThirdPixel[1],
                          ThirdPixel[2], ThirdPixel[3],
                          Stats.HardwareTriangleCount,
                          Stats.HardwareProgramTriangleCount);
        }

        if (UseProgram != NULL)
            UseProgram(0);
        if (Program != 0)
            DeleteProgram(Program);
        if (VertexShader != 0 && IsShader(VertexShader))
            DeleteShader(VertexShader);
        if (FragmentShader != 0 && IsShader(FragmentShader))
            DeleteShader(FragmentShader);
        Program = 0;
        VertexShader = 0;
        FragmentShader = 0;
    }
    else
    {
        Rpi5DiagPrint("RPI5_GL_TEST_017_WINED3D_CAP_SHADER_FAIL missing=%s\n",
                      MissingOpenGl2 ? MissingOpenGl2 : "unknown");
        Success = FALSE;
    }

    {
        static const PCSTR FboProcedures[] =
        {
            "glBindFramebufferEXT",
            "glCheckFramebufferStatusEXT",
            "glDeleteFramebuffersEXT",
            "glFramebufferTexture2DEXT",
            "glGenFramebuffersEXT",
            "glGetFramebufferAttachmentParameterivEXT",
            "glIsFramebufferEXT"
        };
        PFNGLBINDFRAMEBUFFEREXTPROC BindFramebuffer;
        PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC CheckFramebufferStatus;
        PFNGLDELETEFRAMEBUFFERSEXTPROC DeleteFramebuffers;
        PFNGLFRAMEBUFFERTEXTURE2DEXTPROC FramebufferTexture2D;
        PFNGLGENFRAMEBUFFERSEXTPROC GenFramebuffers;
        PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC
            GetFramebufferAttachmentParameteriv;
        PFNGLISFRAMEBUFFEREXTPROC IsFramebuffer;
        PCSTR MissingFbo = NULL;
        BYTE TexturePixels[16 * 16 * 4];
        BYTE FboReadPixel[4];
        GLuint Texture = 0;
        GLuint Framebuffer = 0;
        GLenum FramebufferStatus = 0;
        GLenum DetachedStatus = 0;
        GLenum Error;
        GLint AttachmentType = 0;
        GLint AttachmentName = 0;
        GLint DetachedType = -1;
        GLint DetachedName = -1;
        GLboolean GeneratedIsObject = GL_TRUE;
        GLboolean BoundIsObject = GL_FALSE;
        GLboolean DeletedIsObject = GL_TRUE;
        ULONG HardwareClearBase = 0;

        if (!Rpi5OpenGlProcSetAvailable(FboProcedures,
                                         ARRAYSIZE(FboProcedures),
                                         &MissingFbo))
        {
            Rpi5DiagPrint("RPI5_GL_TEST_019_FBO_TARGET_FAIL missing=%s\n",
                          MissingFbo ? MissingFbo : "unknown");
            Success = FALSE;
        }
        else
        {
            BindFramebuffer = (PFNGLBINDFRAMEBUFFEREXTPROC)
                wglGetProcAddress("glBindFramebufferEXT");
            CheckFramebufferStatus =
                (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
                wglGetProcAddress("glCheckFramebufferStatusEXT");
            DeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glDeleteFramebuffersEXT");
            FramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
                wglGetProcAddress("glFramebufferTexture2DEXT");
            GenFramebuffers = (PFNGLGENFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glGenFramebuffersEXT");
            GetFramebufferAttachmentParameteriv =
                (PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC)
                wglGetProcAddress(
                    "glGetFramebufferAttachmentParameterivEXT");
            IsFramebuffer = (PFNGLISFRAMEBUFFEREXTPROC)
                wglGetProcAddress("glIsFramebufferEXT");

            ZeroMemory(TexturePixels, sizeof(TexturePixels));
            ZeroMemory(FboReadPixel, sizeof(FboReadPixel));
            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Size = sizeof(Stats);
            if (GetStats && GetStats(&Stats))
                HardwareClearBase = Stats.HardwareClearCount;

            glGenTextures(1, &Texture);
            glBindTexture(GL_TEXTURE_2D, Texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA,
                         16,
                         16,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         TexturePixels);
            GenFramebuffers(1, &Framebuffer);
            GeneratedIsObject = IsFramebuffer(Framebuffer);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffer);
            BoundIsObject = IsFramebuffer(Framebuffer);
            FramebufferTexture2D(GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_TEXTURE_2D,
                                 Texture,
                                 0);
            FramebufferStatus =
                CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
            GetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER_EXT,
                GL_COLOR_ATTACHMENT0_EXT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,
                &AttachmentType);
            GetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER_EXT,
                GL_COLOR_ATTACHMENT0_EXT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,
                &AttachmentName);

            glClearColor(0.2f, 0.4f, 0.8f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glReadPixels(8, 8, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, FboReadPixel);
            ZeroMemory(TexturePixels, sizeof(TexturePixels));
            glGetTexImage(GL_TEXTURE_2D,
                          0,
                          GL_RGBA,
                          GL_UNSIGNED_BYTE,
                          TexturePixels);
            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Size = sizeof(Stats);
            if (!GetStats || !GetStats(&Stats))
                ZeroMemory(&Stats, sizeof(Stats));

            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteTextures(1, &Texture);
            DetachedStatus =
                CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
            GetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER_EXT,
                GL_COLOR_ATTACHMENT0_EXT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,
                &DetachedType);
            GetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER_EXT,
                GL_COLOR_ATTACHMENT0_EXT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,
                &DetachedName);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
            DeleteFramebuffers(1, &Framebuffer);
            DeletedIsObject = IsFramebuffer(Framebuffer);
            Error = glGetError();

            if (Texture == 0 || Framebuffer == 0 ||
                GeneratedIsObject != GL_FALSE ||
                BoundIsObject != GL_TRUE ||
                DeletedIsObject != GL_FALSE ||
                FramebufferStatus != GL_FRAMEBUFFER_COMPLETE_EXT ||
                AttachmentType != GL_TEXTURE ||
                AttachmentName != (GLint)Texture ||
                DetachedStatus !=
                    GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT ||
                DetachedType != GL_NONE || DetachedName != 0 ||
                FboReadPixel[0] < 48 || FboReadPixel[0] > 54 ||
                FboReadPixel[1] < 99 || FboReadPixel[1] > 105 ||
                FboReadPixel[2] < 201 || FboReadPixel[2] > 207 ||
                FboReadPixel[3] != 255 ||
                TexturePixels[(8 * 16 + 8) * 4 + 0] < 48 ||
                TexturePixels[(8 * 16 + 8) * 4 + 0] > 54 ||
                TexturePixels[(8 * 16 + 8) * 4 + 1] < 99 ||
                TexturePixels[(8 * 16 + 8) * 4 + 1] > 105 ||
                TexturePixels[(8 * 16 + 8) * 4 + 2] < 201 ||
                TexturePixels[(8 * 16 + 8) * 4 + 2] > 207 ||
                TexturePixels[(8 * 16 + 8) * 4 + 3] != 255 ||
                Stats.HardwareClearCount != HardwareClearBase + 1 ||
                strstr((const char *)Extensions,
                       "GL_EXT_framebuffer_object") != NULL ||
                Error != GL_NO_ERROR)
            {
                Rpi5DiagPrint("RPI5_GL_TEST_019_FBO_TARGET_FAIL texture=%lu fbo=%lu generated=%u bound=%u deleted=%u status=0x%04x attachment=0x%04lx/%ld detached=0x%04x/0x%04lx/%ld read=%u,%u,%u,%u image=%u,%u,%u,%u hw-clear=%lu error=0x%04x advertised=%u\n",
                              Texture,
                              Framebuffer,
                              GeneratedIsObject,
                              BoundIsObject,
                              DeletedIsObject,
                              FramebufferStatus,
                              AttachmentType,
                              AttachmentName,
                              DetachedStatus,
                              DetachedType,
                              DetachedName,
                              FboReadPixel[0], FboReadPixel[1],
                              FboReadPixel[2], FboReadPixel[3],
                              TexturePixels[(8 * 16 + 8) * 4 + 0],
                              TexturePixels[(8 * 16 + 8) * 4 + 1],
                              TexturePixels[(8 * 16 + 8) * 4 + 2],
                              TexturePixels[(8 * 16 + 8) * 4 + 3],
                              Stats.HardwareClearCount,
                              Error,
                              strstr((const char *)Extensions,
                                     "GL_EXT_framebuffer_object") != NULL);
                Success = FALSE;
            }
            else
            {
                Rpi5DiagPrint("RPI5_GL_TEST_019_FBO_TARGET_PASS texture=%lu fbo=%lu status=0x%04x detached=0x%04x read=%u,%u,%u,%u image=%u,%u,%u,%u hw-clear=%lu advertised=0\n",
                              Texture,
                              Framebuffer,
                              FramebufferStatus,
                              DetachedStatus,
                              FboReadPixel[0], FboReadPixel[1],
                              FboReadPixel[2], FboReadPixel[3],
                              TexturePixels[(8 * 16 + 8) * 4 + 0],
                              TexturePixels[(8 * 16 + 8) * 4 + 1],
                              TexturePixels[(8 * 16 + 8) * 4 + 2],
                              TexturePixels[(8 * 16 + 8) * 4 + 3],
                              Stats.HardwareClearCount);
            }
        }
    }

    {
        PFNGLBINDFRAMEBUFFEREXTPROC BindFramebuffer =
            (PFNGLBINDFRAMEBUFFEREXTPROC)
                wglGetProcAddress("glBindFramebufferEXT");
        PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC CheckFramebufferStatus =
            (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
                wglGetProcAddress("glCheckFramebufferStatusEXT");
        PFNGLDELETEFRAMEBUFFERSEXTPROC DeleteFramebuffers =
            (PFNGLDELETEFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glDeleteFramebuffersEXT");
        PFNGLFRAMEBUFFERTEXTURE2DEXTPROC FramebufferTexture2D =
            (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
                wglGetProcAddress("glFramebufferTexture2DEXT");
        PFNGLGENFRAMEBUFFERSEXTPROC GenFramebuffers =
            (PFNGLGENFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glGenFramebuffersEXT");
        ULONG InitialWords[4 * 4];
        ULONG UnpackWords[4 * 4];
        ULONG ReadbackWords[4 * 4];
        GLuint Texture = 0;
        GLuint Framebuffer = 0;
        GLenum FramebufferStatus = 0;
        GLenum Error = GL_NO_ERROR;
        ULONG HardwareClearBase = 0;
        ULONG Index;
        BOOL ProceduresAvailable;

        ProceduresAvailable = BindFramebuffer != NULL &&
                              CheckFramebufferStatus != NULL &&
                              DeleteFramebuffers != NULL &&
                              FramebufferTexture2D != NULL &&
                              GenFramebuffers != NULL;
        if (!ProceduresAvailable)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_020_PACKED_BGRA_FAIL missing-fbo-procedure\n");
            Success = FALSE;
        }
        else
        {
            for (Index = 0; Index < ARRAYSIZE(InitialWords); Index++)
                InitialWords[Index] = 0x11111111;
            for (Index = 0; Index < ARRAYSIZE(UnpackWords); Index++)
                UnpackWords[Index] = 0xDEADBEEF;
            ZeroMemory(ReadbackWords, sizeof(ReadbackWords));
            UnpackWords[1 * 4 + 1] = 0xFF112233;
            UnpackWords[1 * 4 + 2] = 0x80445566;
            UnpackWords[2 * 4 + 1] = 0x40778899;
            UnpackWords[2 * 4 + 2] = 0x20AABBCC;

            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Size = sizeof(Stats);
            if (GetStats && GetStats(&Stats))
                HardwareClearBase = Stats.HardwareClearCount;

            glGenTextures(1, &Texture);
            glBindTexture(GL_TEXTURE_2D, Texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA8,
                         4,
                         4,
                         0,
                         GL_BGRA,
                         GL_UNSIGNED_INT_8_8_8_8_REV,
                         NULL);
            GenFramebuffers(1, &Framebuffer);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffer);
            FramebufferTexture2D(GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_TEXTURE_2D,
                                 Texture,
                                 0);
            FramebufferStatus =
                CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);

            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            0,
                            0,
                            4,
                            4,
                            GL_BGRA,
                            GL_UNSIGNED_INT_8_8_8_8_REV,
                            InitialWords);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 4);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            1,
                            1,
                            2,
                            2,
                            GL_BGRA,
                            GL_UNSIGNED_INT_8_8_8_8_REV,
                            UnpackWords);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glGetTexImage(GL_TEXTURE_2D,
                          0,
                          GL_BGRA,
                          GL_UNSIGNED_INT_8_8_8_8_REV,
                          ReadbackWords);

            glClearColor(0.996f, 0.729f, 0.745f, 0.792f);
            glClear(GL_COLOR_BUFFER_BIT);
            glGetTexImage(GL_TEXTURE_2D,
                          0,
                          GL_BGRA,
                          GL_UNSIGNED_INT_8_8_8_8_REV,
                          InitialWords);
            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Size = sizeof(Stats);
            if (!GetStats || !GetStats(&Stats))
                ZeroMemory(&Stats, sizeof(Stats));
            Error = glGetError();

            FramebufferTexture2D(GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_TEXTURE_2D,
                                 0,
                                 0);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
            DeleteFramebuffers(1, &Framebuffer);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteTextures(1, &Texture);

            if (Texture == 0 || Framebuffer == 0 ||
                FramebufferStatus != GL_FRAMEBUFFER_COMPLETE_EXT ||
                ReadbackWords[0] != 0x11111111 ||
                ReadbackWords[1 * 4 + 1] != 0xFF112233 ||
                ReadbackWords[1 * 4 + 2] != 0x80445566 ||
                ReadbackWords[2 * 4 + 1] != 0x40778899 ||
                ReadbackWords[2 * 4 + 2] != 0x20AABBCC ||
                InitialWords[0] != 0xC9FDB9BD ||
                Stats.HardwareClearCount != HardwareClearBase + 1 ||
                Error != GL_NO_ERROR)
            {
                Rpi5DiagPrint("RPI5_GL_TEST_020_PACKED_BGRA_FAIL texture=%lu fbo=%lu status=0x%04x base=%08lx patch=%08lx/%08lx/%08lx/%08lx clear=%08lx hw-clear=%lu error=0x%04x\n",
                              Texture,
                              Framebuffer,
                              FramebufferStatus,
                              ReadbackWords[0],
                              ReadbackWords[1 * 4 + 1],
                              ReadbackWords[1 * 4 + 2],
                              ReadbackWords[2 * 4 + 1],
                              ReadbackWords[2 * 4 + 2],
                              InitialWords[0],
                              Stats.HardwareClearCount,
                              Error);
                Success = FALSE;
            }
            else
            {
                Rpi5DiagPrint("RPI5_GL_TEST_020_PACKED_BGRA_PASS status=0x%04x base=%08lx patch=%08lx/%08lx/%08lx/%08lx clear=%08lx hw-clear=%lu\n",
                              FramebufferStatus,
                              ReadbackWords[0],
                              ReadbackWords[1 * 4 + 1],
                              ReadbackWords[1 * 4 + 2],
                              ReadbackWords[2 * 4 + 1],
                              ReadbackWords[2 * 4 + 2],
                              InitialWords[0],
                              Stats.HardwareClearCount);
            }
        }
    }

FocusedTests:
    {
        PFNGLBINDFRAMEBUFFEREXTPROC BindFramebuffer =
            (PFNGLBINDFRAMEBUFFEREXTPROC)
                wglGetProcAddress("glBindFramebufferEXT");
        PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC CheckFramebufferStatus =
            (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
                wglGetProcAddress("glCheckFramebufferStatusEXT");
        PFNGLDELETEFRAMEBUFFERSEXTPROC DeleteFramebuffers =
            (PFNGLDELETEFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glDeleteFramebuffersEXT");
        PFNGLFRAMEBUFFERTEXTURE2DEXTPROC FramebufferTexture2D =
            (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
                wglGetProcAddress("glFramebufferTexture2DEXT");
        PFNGLGENFRAMEBUFFERSEXTPROC GenFramebuffers =
            (PFNGLGENFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glGenFramebuffersEXT");
        BYTE Level0[3 * 5 * 4];
        BYTE Level1[1 * 2 * 4];
        BYTE Level2[1 * 1 * 4];
        BYTE TextureReadback[3 * 5 * 4];
        BYTE SampleBase[4];
        BYTE SamplePositiveRepeat[4];
        BYTE SampleNegativeRepeat[4];
        BYTE FboPixel[4];
        GLuint Texture = 0;
        GLuint Framebuffer = 0;
        GLenum FramebufferStatus = 0;
        GLenum Error = GL_NO_ERROR;
        GLint ZeroWidth = -1;
        GLint ZeroHeight = -1;
        GLint Level0Width = -1;
        GLint Level0Height = -1;
        GLint Level1Width = -1;
        GLint Level1Height = -1;
        GLint Level2Width = -1;
        GLint Level2Height = -1;
        ULONG HardwareClearBase = 0;
        ULONG SoftwareClearBase = 0;
        ULONG HardwareClearFailureBase = 0;
        ULONG Index;
        BOOL ProceduresAvailable;

        ProceduresAvailable = BindFramebuffer != NULL &&
                              CheckFramebufferStatus != NULL &&
                              DeleteFramebuffers != NULL &&
                              FramebufferTexture2D != NULL &&
                              GenFramebuffers != NULL;
        if (!ProceduresAvailable)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_021_NPOT_FAIL missing-fbo-procedure\n");
            Success = FALSE;
        }
        else
        {
            ZeroMemory(Level0, sizeof(Level0));
            ZeroMemory(Level1, sizeof(Level1));
            ZeroMemory(Level2, sizeof(Level2));
            ZeroMemory(TextureReadback, sizeof(TextureReadback));
            ZeroMemory(SampleBase, sizeof(SampleBase));
            ZeroMemory(SamplePositiveRepeat,
                       sizeof(SamplePositiveRepeat));
            ZeroMemory(SampleNegativeRepeat,
                       sizeof(SampleNegativeRepeat));
            ZeroMemory(FboPixel, sizeof(FboPixel));
            for (Index = 0; Index < 3 * 5; Index++)
                Level0[Index * 4 + 3] = 255;
            for (Index = 0; Index < 1 * 2; Index++)
                Level1[Index * 4 + 3] = 255;
            Level2[3] = 255;
            Level0[(0 * 3 + 0) * 4 + 0] = 255;
            Level0[(2 * 3 + 1) * 4 + 2] = 255;
            Level0[(4 * 3 + 2) * 4 + 1] = 255;

            glGenTextures(1, &Texture);
            glBindTexture(GL_TEXTURE_2D, Texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA,
                         0,
                         0,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         Level0);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                     0,
                                     GL_TEXTURE_WIDTH,
                                     &ZeroWidth);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                     0,
                                     GL_TEXTURE_HEIGHT,
                                     &ZeroHeight);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA,
                         3,
                         5,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         Level0);
            glTexImage2D(GL_TEXTURE_2D,
                         1,
                         GL_RGBA,
                         1,
                         2,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         Level1);
            glTexImage2D(GL_TEXTURE_2D,
                         2,
                         GL_RGBA,
                         1,
                         1,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         Level2);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                     0,
                                     GL_TEXTURE_WIDTH,
                                     &Level0Width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                     0,
                                     GL_TEXTURE_HEIGHT,
                                     &Level0Height);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                     1,
                                     GL_TEXTURE_WIDTH,
                                     &Level1Width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                     1,
                                     GL_TEXTURE_HEIGHT,
                                     &Level1Height);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                     2,
                                     GL_TEXTURE_WIDTH,
                                     &Level2Width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                     2,
                                     GL_TEXTURE_HEIGHT,
                                     &Level2Height);

            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glTexEnvi(GL_TEXTURE_ENV,
                      GL_TEXTURE_ENV_MODE,
                      GL_REPLACE);
            glEnable(GL_TEXTURE_2D);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            glBegin(GL_TRIANGLES);
            glTexCoord2f(0.1f, 0.1f);
            glVertex2f(-1.0f, -1.0f);
            glTexCoord2f(0.1f, 0.1f);
            glVertex2f(-0.34f, -1.0f);
            glTexCoord2f(0.1f, 0.1f);
            glVertex2f(-0.67f, 1.0f);
            glTexCoord2f(1.5f, 1.5f);
            glVertex2f(-0.32f, -1.0f);
            glTexCoord2f(1.5f, 1.5f);
            glVertex2f(0.32f, -1.0f);
            glTexCoord2f(1.5f, 1.5f);
            glVertex2f(0.0f, 1.0f);
            glTexCoord2f(-0.1f, -0.1f);
            glVertex2f(0.34f, -1.0f);
            glTexCoord2f(-0.1f, -0.1f);
            glVertex2f(1.0f, -1.0f);
            glTexCoord2f(-0.1f, -0.1f);
            glVertex2f(0.67f, 1.0f);
            glEnd();
            glDisable(GL_TEXTURE_2D);
            glFinish();
            glReadPixels(21, 64, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, SampleBase);
            glReadPixels(64, 64, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE,
                         SamplePositiveRepeat);
            glReadPixels(106, 64, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE,
                         SampleNegativeRepeat);

            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Size = sizeof(Stats);
            if (GetStats && GetStats(&Stats))
            {
                HardwareClearBase = Stats.HardwareClearCount;
                SoftwareClearBase = Stats.SoftwareClearCount;
                HardwareClearFailureBase =
                    Stats.HardwareClearFailureCount;
            }
            GenFramebuffers(1, &Framebuffer);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffer);
            FramebufferTexture2D(GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_TEXTURE_2D,
                                 Texture,
                                 0);
            FramebufferStatus =
                CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
            glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glReadPixels(1, 2, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, FboPixel);
            glGetTexImage(GL_TEXTURE_2D,
                          0,
                          GL_RGBA,
                          GL_UNSIGNED_BYTE,
                          TextureReadback);
            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Size = sizeof(Stats);
            if (!GetStats || !GetStats(&Stats))
                ZeroMemory(&Stats, sizeof(Stats));
            Error = glGetError();

            FramebufferTexture2D(GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_TEXTURE_2D,
                                 0,
                                 0);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
            DeleteFramebuffers(1, &Framebuffer);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteTextures(1, &Texture);

            if (Texture == 0 || Framebuffer == 0 ||
                ZeroWidth != 0 || ZeroHeight != 0 ||
                Level0Width != 3 || Level0Height != 5 ||
                Level1Width != 1 || Level1Height != 2 ||
                Level2Width != 1 || Level2Height != 1 ||
                SampleBase[0] < 224 || SampleBase[1] > 32 ||
                SampleBase[2] > 32 ||
                SamplePositiveRepeat[0] > 32 ||
                SamplePositiveRepeat[1] > 32 ||
                SamplePositiveRepeat[2] < 224 ||
                SampleNegativeRepeat[0] > 32 ||
                SampleNegativeRepeat[1] < 224 ||
                SampleNegativeRepeat[2] > 32 ||
                FramebufferStatus != GL_FRAMEBUFFER_COMPLETE_EXT ||
                FboPixel[0] < 60 || FboPixel[0] > 66 ||
                FboPixel[1] < 124 || FboPixel[1] > 130 ||
                FboPixel[2] < 188 || FboPixel[2] > 194 ||
                FboPixel[3] != 255 ||
                TextureReadback[(2 * 3 + 1) * 4 + 0] != FboPixel[0] ||
                TextureReadback[(2 * 3 + 1) * 4 + 1] != FboPixel[1] ||
                TextureReadback[(2 * 3 + 1) * 4 + 2] != FboPixel[2] ||
                TextureReadback[(2 * 3 + 1) * 4 + 3] != FboPixel[3] ||
                Stats.HardwareClearCount != HardwareClearBase ||
                Stats.SoftwareClearCount != SoftwareClearBase + 1 ||
                Stats.HardwareClearFailureCount !=
                    HardwareClearFailureBase ||
                strstr((const char *)Extensions,
                       "GL_ARB_texture_non_power_of_two") != NULL ||
                Error != GL_NO_ERROR)
            {
                Rpi5DiagPrint("RPI5_GL_TEST_021_NPOT_FAIL texture=%lu fbo=%lu zero=%ldx%ld levels=%ldx%ld/%ldx%ld/%ldx%ld base=%u,%u,%u,%u positive=%u,%u,%u,%u negative=%u,%u,%u,%u status=0x%04x fbo-pixel=%u,%u,%u,%u image=%u,%u,%u,%u hw-clear=%lu sw-clear=%lu hw-failures=%lu error=0x%04x advertised=%u\n",
                              Texture,
                              Framebuffer,
                              ZeroWidth,
                              ZeroHeight,
                              Level0Width,
                              Level0Height,
                              Level1Width,
                              Level1Height,
                              Level2Width,
                              Level2Height,
                              SampleBase[0], SampleBase[1],
                              SampleBase[2], SampleBase[3],
                              SamplePositiveRepeat[0],
                              SamplePositiveRepeat[1],
                              SamplePositiveRepeat[2],
                              SamplePositiveRepeat[3],
                              SampleNegativeRepeat[0],
                              SampleNegativeRepeat[1],
                              SampleNegativeRepeat[2],
                              SampleNegativeRepeat[3],
                              FramebufferStatus,
                              FboPixel[0], FboPixel[1],
                              FboPixel[2], FboPixel[3],
                              TextureReadback[(2 * 3 + 1) * 4 + 0],
                              TextureReadback[(2 * 3 + 1) * 4 + 1],
                              TextureReadback[(2 * 3 + 1) * 4 + 2],
                              TextureReadback[(2 * 3 + 1) * 4 + 3],
                              Stats.HardwareClearCount,
                              Stats.SoftwareClearCount,
                              Stats.HardwareClearFailureCount,
                              Error,
                              strstr((const char *)Extensions,
                                     "GL_ARB_texture_non_power_of_two") !=
                                  NULL);
                Success = FALSE;
            }
            else
            {
                Rpi5DiagPrint("RPI5_GL_TEST_021_NPOT_PASS zero=0x0 levels=3x5/1x2/1x1 base=%u,%u,%u,%u positive=%u,%u,%u,%u negative=%u,%u,%u,%u status=0x%04x fbo=%u,%u,%u,%u deferred-clear=1 advertised=0\n",
                              SampleBase[0], SampleBase[1],
                              SampleBase[2], SampleBase[3],
                              SamplePositiveRepeat[0],
                              SamplePositiveRepeat[1],
                              SamplePositiveRepeat[2],
                              SamplePositiveRepeat[3],
                              SampleNegativeRepeat[0],
                              SampleNegativeRepeat[1],
                              SampleNegativeRepeat[2],
                              SampleNegativeRepeat[3],
                              FramebufferStatus,
                              FboPixel[0], FboPixel[1],
                              FboPixel[2], FboPixel[3]);
            }
        }
    }

    {
        BYTE Level2D[3 * 5 * 4];
        BYTE Level1D[3 * 4];
        BYTE PositiveRepeat[4];
        BYTE NegativeRepeat[4];
        BYTE ClampCenter[4];
        BYTE OneDimensional[4];
        GLuint Texture2D = 0;
        GLuint Texture1D = 0;
        GLint Texture1DWidth = -1;
        GLenum Error;
        ULONG Index;

        ZeroMemory(Level2D, sizeof(Level2D));
        ZeroMemory(Level1D, sizeof(Level1D));
        ZeroMemory(PositiveRepeat, sizeof(PositiveRepeat));
        ZeroMemory(NegativeRepeat, sizeof(NegativeRepeat));
        ZeroMemory(ClampCenter, sizeof(ClampCenter));
        ZeroMemory(OneDimensional, sizeof(OneDimensional));
        for (Index = 0; Index < 3 * 5; Index++)
            Level2D[Index * 4 + 3] = 255;
        for (Index = 0; Index < 3; Index++)
            Level1D[Index * 4 + 3] = 255;
        Level2D[(2 * 3 + 1) * 4 + 2] = 255;
        Level2D[(4 * 3 + 2) * 4 + 1] = 255;
        Level1D[0 * 4 + 0] = 255;
        Level1D[1 * 4 + 2] = 255;
        Level1D[2 * 4 + 1] = 255;

        glGenTextures(1, &Texture2D);
        glBindTexture(GL_TEXTURE_2D, Texture2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     3,
                     5,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     Level2D);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_TRIANGLES);
        glTexCoord2f(1.5f, 1.5f);
        glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.5f, 1.5f);
        glVertex2f(-0.34f, -1.0f);
        glTexCoord2f(1.5f, 1.5f);
        glVertex2f(-0.67f, 1.0f);
        glTexCoord2f(-1.0f / 6.0f, -0.1f);
        glVertex2f(-0.32f, -1.0f);
        glTexCoord2f(-1.0f / 6.0f, -0.1f);
        glVertex2f(0.32f, -1.0f);
        glTexCoord2f(-1.0f / 6.0f, -0.1f);
        glVertex2f(0.0f, 1.0f);
        glEnd();
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glBegin(GL_TRIANGLES);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(0.34f, -1.0f);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(1.0f, -1.0f);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(0.67f, 1.0f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glFinish();
        glReadPixels(21, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, PositiveRepeat);
        glReadPixels(64, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, NegativeRepeat);
        glReadPixels(106, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, ClampCenter);

        glGenTextures(1, &Texture1D);
        glBindTexture(GL_TEXTURE_1D, Texture1D);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER,
                        GL_NEAREST);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER,
                        GL_NEAREST);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S,
                        GL_REPEAT);
        glTexImage1D(GL_TEXTURE_1D,
                     0,
                     GL_RGBA,
                     3,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     Level1D);
        glGetTexLevelParameteriv(GL_TEXTURE_1D,
                                 0,
                                 GL_TEXTURE_WIDTH,
                                 &Texture1DWidth);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_1D);
        glBegin(GL_TRIANGLES);
        glTexCoord1f(1.5f);
        glVertex2f(-0.8f, -0.8f);
        glTexCoord1f(1.5f);
        glVertex2f(0.8f, -0.8f);
        glTexCoord1f(1.5f);
        glVertex2f(0.0f, 0.8f);
        glEnd();
        glDisable(GL_TEXTURE_1D);
        glFinish();
        glReadPixels(64, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, OneDimensional);
        Error = glGetError();

        glBindTexture(GL_TEXTURE_1D, 0);
        glDeleteTextures(1, &Texture1D);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &Texture2D);

        if (Texture2D == 0 || Texture1D == 0 ||
            Texture1DWidth != 3 ||
            PositiveRepeat[0] > 32 || PositiveRepeat[1] > 32 ||
            PositiveRepeat[2] < 224 ||
            NegativeRepeat[0] > 32 || NegativeRepeat[1] < 224 ||
            NegativeRepeat[2] > 32 ||
            ClampCenter[0] > 32 || ClampCenter[1] > 32 ||
            ClampCenter[2] < 224 ||
            OneDimensional[0] > 32 ||
            OneDimensional[1] > 32 ||
            OneDimensional[2] < 224 ||
            Error != GL_NO_ERROR)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_022_NPOT_FILTER_FAIL textures=%lu/%lu width1d=%ld positive=%u,%u,%u,%u negative=%u,%u,%u,%u clamp=%u,%u,%u,%u one-d=%u,%u,%u,%u error=0x%04x\n",
                          Texture2D,
                          Texture1D,
                          Texture1DWidth,
                          PositiveRepeat[0], PositiveRepeat[1],
                          PositiveRepeat[2], PositiveRepeat[3],
                          NegativeRepeat[0], NegativeRepeat[1],
                          NegativeRepeat[2], NegativeRepeat[3],
                          ClampCenter[0], ClampCenter[1],
                          ClampCenter[2], ClampCenter[3],
                          OneDimensional[0], OneDimensional[1],
                          OneDimensional[2], OneDimensional[3],
                          Error);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_GL_TEST_022_NPOT_FILTER_PASS linear-positive=%u,%u,%u,%u linear-negative=%u,%u,%u,%u clamp=%u,%u,%u,%u one-d=%u,%u,%u,%u width1d=3 advertised=0\n",
                          PositiveRepeat[0], PositiveRepeat[1],
                          PositiveRepeat[2], PositiveRepeat[3],
                          NegativeRepeat[0], NegativeRepeat[1],
                          NegativeRepeat[2], NegativeRepeat[3],
                          ClampCenter[0], ClampCenter[1],
                          ClampCenter[2], ClampCenter[3],
                          OneDimensional[0], OneDimensional[1],
                          OneDimensional[2], OneDimensional[3]);
        }
    }

    {
        const GLfloat BorderColor[4] = {1.0f, 1.0f, 0.0f, 1.0f};
        BYTE Level1D[3 * 4];
        BYTE Level2D[3 * 3 * 4];
        BYTE MirrorPositive[4];
        BYTE MirrorNegative[4];
        BYTE ClampEdge[4];
        BYTE ClampBorder[4];
        BYTE LegacyClamp[4];
        BYTE TwoDimensional[4];
        GLuint Texture1D = 0;
        GLuint Texture2D = 0;
        GLint MaxTextureSize = 0;
        GLint Level1DWidth = -1;
        GLint Level2DWidth = -1;
        GLint Level2DHeight = -1;
        GLenum Level1DError;
        GLenum Level2DError;
        GLenum Error;
        ULONG Index;

        ZeroMemory(Level1D, sizeof(Level1D));
        ZeroMemory(Level2D, sizeof(Level2D));
        ZeroMemory(MirrorPositive, sizeof(MirrorPositive));
        ZeroMemory(MirrorNegative, sizeof(MirrorNegative));
        ZeroMemory(ClampEdge, sizeof(ClampEdge));
        ZeroMemory(ClampBorder, sizeof(ClampBorder));
        ZeroMemory(LegacyClamp, sizeof(LegacyClamp));
        ZeroMemory(TwoDimensional, sizeof(TwoDimensional));
        for (Index = 0; Index < 3; Index++)
            Level1D[Index * 4 + 3] = 255;
        for (Index = 0; Index < 3 * 3; Index++)
            Level2D[Index * 4 + 3] = 255;
        Level1D[0 * 4 + 0] = 255;
        Level1D[1 * 4 + 1] = 255;
        Level1D[2 * 4 + 2] = 255;
        Level2D[(0 * 3 + 2) * 4 + 0] = 255;
        Level2D[(0 * 3 + 2) * 4 + 2] = 255;

        while (glGetError() != GL_NO_ERROR)
            ;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &MaxTextureSize);

        glGenTextures(1, &Texture1D);
        glBindTexture(GL_TEXTURE_1D, Texture1D);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER,
                        GL_LINEAR);
        glTexParameterfv(GL_TEXTURE_1D,
                         GL_TEXTURE_BORDER_COLOR,
                         BorderColor);
        glTexImage1D(GL_TEXTURE_1D,
                     0,
                     GL_RGBA,
                     3,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     Level1D);
        glTexImage1D(GL_TEXTURE_1D,
                     1,
                     GL_RGBA,
                     1,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     Level1D);
        glTexImage1D(GL_TEXTURE_1D,
                     1,
                     GL_RGBA,
                     MaxTextureSize,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     NULL);
        Level1DError = glGetError();
        glGetTexLevelParameteriv(GL_TEXTURE_1D,
                                 1,
                                 GL_TEXTURE_WIDTH,
                                 &Level1DWidth);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_1D);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S,
                        GL_MIRRORED_REPEAT);
        glBegin(GL_TRIANGLES);
        glTexCoord1f(7.0f / 6.0f);
        glVertex2f(-1.0f, -1.0f);
        glTexCoord1f(7.0f / 6.0f);
        glVertex2f(-0.62f, -1.0f);
        glTexCoord1f(7.0f / 6.0f);
        glVertex2f(-0.81f, 1.0f);
        glEnd();

        glBegin(GL_TRIANGLES);
        glTexCoord1f(-1.0f / 6.0f);
        glVertex2f(-0.58f, -1.0f);
        glTexCoord1f(-1.0f / 6.0f);
        glVertex2f(-0.20f, -1.0f);
        glTexCoord1f(-1.0f / 6.0f);
        glVertex2f(-0.39f, 1.0f);
        glEnd();

        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_EDGE);
        glBegin(GL_TRIANGLES);
        glTexCoord1f(-10.0f);
        glVertex2f(-0.16f, -1.0f);
        glTexCoord1f(-10.0f);
        glVertex2f(0.16f, -1.0f);
        glTexCoord1f(-10.0f);
        glVertex2f(0.0f, 1.0f);
        glEnd();

        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_BORDER);
        glBegin(GL_TRIANGLES);
        glTexCoord1f(-10.0f);
        glVertex2f(0.20f, -1.0f);
        glTexCoord1f(-10.0f);
        glVertex2f(0.58f, -1.0f);
        glTexCoord1f(-10.0f);
        glVertex2f(0.39f, 1.0f);
        glEnd();

        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glBegin(GL_TRIANGLES);
        glTexCoord1f(0.0f);
        glVertex2f(0.62f, -1.0f);
        glTexCoord1f(0.0f);
        glVertex2f(1.0f, -1.0f);
        glTexCoord1f(0.0f);
        glVertex2f(0.81f, 1.0f);
        glEnd();
        glDisable(GL_TEXTURE_1D);
        glFinish();
        glReadPixels(12, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, MirrorPositive);
        glReadPixels(38, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, MirrorNegative);
        glReadPixels(64, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, ClampEdge);
        glReadPixels(90, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, ClampBorder);
        glReadPixels(116, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, LegacyClamp);

        glGenTextures(1, &Texture2D);
        glBindTexture(GL_TEXTURE_2D, Texture2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_MIRRORED_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     3,
                     3,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     Level2D);
        glTexImage2D(GL_TEXTURE_2D,
                     1,
                     GL_RGBA,
                     1,
                     1,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     Level2D);
        glTexImage2D(GL_TEXTURE_2D,
                     1,
                     GL_RGBA,
                     1,
                     MaxTextureSize,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     NULL);
        Level2DError = glGetError();
        glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                 1,
                                 GL_TEXTURE_WIDTH,
                                 &Level2DWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,
                                 1,
                                 GL_TEXTURE_HEIGHT,
                                 &Level2DHeight);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_TRIANGLES);
        glTexCoord2f(7.0f / 6.0f, -10.0f);
        glVertex2f(-0.8f, -0.8f);
        glTexCoord2f(7.0f / 6.0f, -10.0f);
        glVertex2f(0.8f, -0.8f);
        glTexCoord2f(7.0f / 6.0f, -10.0f);
        glVertex2f(0.0f, 0.8f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glFinish();
        glReadPixels(64, 64, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, TwoDimensional);
        Error = glGetError();

        glBindTexture(GL_TEXTURE_1D, 0);
        glDeleteTextures(1, &Texture1D);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &Texture2D);

        if (Texture1D == 0 || Texture2D == 0 ||
            MaxTextureSize < 2 ||
            Level1DError != GL_INVALID_VALUE ||
            Level2DError != GL_INVALID_VALUE ||
            Level1DWidth != 1 ||
            Level2DWidth != 1 || Level2DHeight != 1 ||
            MirrorPositive[0] > 32 ||
            MirrorPositive[1] > 32 ||
            MirrorPositive[2] < 224 ||
            MirrorNegative[0] < 224 ||
            MirrorNegative[1] > 32 ||
            MirrorNegative[2] > 32 ||
            ClampEdge[0] < 224 || ClampEdge[1] > 32 ||
            ClampEdge[2] > 32 ||
            ClampBorder[0] < 224 || ClampBorder[1] < 224 ||
            ClampBorder[2] > 32 ||
            LegacyClamp[0] < 224 ||
            LegacyClamp[1] < 96 || LegacyClamp[1] > 160 ||
            LegacyClamp[2] > 32 ||
            TwoDimensional[0] < 224 ||
            TwoDimensional[1] > 32 ||
            TwoDimensional[2] < 224 ||
            strstr((const char *)Extensions,
                   "GL_ARB_texture_non_power_of_two") != NULL ||
            Error != GL_NO_ERROR)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_023_NPOT_WRAP_FAIL textures=%lu/%lu max=%ld level-errors=0x%04x/0x%04x levels=%ld/%ldx%ld mirror-positive=%u,%u,%u,%u mirror-negative=%u,%u,%u,%u edge=%u,%u,%u,%u border=%u,%u,%u,%u clamp=%u,%u,%u,%u two-d=%u,%u,%u,%u error=0x%04x advertised=%u\n",
                          Texture1D,
                          Texture2D,
                          MaxTextureSize,
                          Level1DError,
                          Level2DError,
                          Level1DWidth,
                          Level2DWidth,
                          Level2DHeight,
                          MirrorPositive[0], MirrorPositive[1],
                          MirrorPositive[2], MirrorPositive[3],
                          MirrorNegative[0], MirrorNegative[1],
                          MirrorNegative[2], MirrorNegative[3],
                          ClampEdge[0], ClampEdge[1],
                          ClampEdge[2], ClampEdge[3],
                          ClampBorder[0], ClampBorder[1],
                          ClampBorder[2], ClampBorder[3],
                          LegacyClamp[0], LegacyClamp[1],
                          LegacyClamp[2], LegacyClamp[3],
                          TwoDimensional[0], TwoDimensional[1],
                          TwoDimensional[2], TwoDimensional[3],
                          Error,
                          strstr((const char *)Extensions,
                                 "GL_ARB_texture_non_power_of_two") !=
                              NULL);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_GL_TEST_023_NPOT_WRAP_PASS max=%ld level-errors=0x%04x/0x%04x levels=1/1x1 mirror-positive=%u,%u,%u,%u mirror-negative=%u,%u,%u,%u edge=%u,%u,%u,%u border=%u,%u,%u,%u clamp=%u,%u,%u,%u two-d=%u,%u,%u,%u advertised=0\n",
                          MaxTextureSize,
                          Level1DError,
                          Level2DError,
                          MirrorPositive[0], MirrorPositive[1],
                          MirrorPositive[2], MirrorPositive[3],
                          MirrorNegative[0], MirrorNegative[1],
                          MirrorNegative[2], MirrorNegative[3],
                          ClampEdge[0], ClampEdge[1],
                          ClampEdge[2], ClampEdge[3],
                          ClampBorder[0], ClampBorder[1],
                          ClampBorder[2], ClampBorder[3],
                          LegacyClamp[0], LegacyClamp[1],
                          LegacyClamp[2], LegacyClamp[3],
                          TwoDimensional[0], TwoDimensional[1],
                          TwoDimensional[2], TwoDimensional[3]);
        }
    }

    {
        static const PCSTR FboProcedures[] =
        {
            "glBindFramebufferEXT",
            "glBindRenderbufferEXT",
            "glCheckFramebufferStatusEXT",
            "glDeleteFramebuffersEXT",
            "glDeleteRenderbuffersEXT",
            "glFramebufferRenderbufferEXT",
            "glFramebufferTexture1DEXT",
            "glFramebufferTexture2DEXT",
            "glFramebufferTexture3DEXT",
            "glGenFramebuffersEXT",
            "glGenerateMipmapEXT",
            "glGenRenderbuffersEXT",
            "glGetFramebufferAttachmentParameterivEXT",
            "glGetRenderbufferParameterivEXT",
            "glIsFramebufferEXT",
            "glIsRenderbufferEXT",
            "glRenderbufferStorageEXT"
        };
        PFNGLBINDFRAMEBUFFEREXTPROC BindFramebuffer;
        PFNGLBINDRENDERBUFFEREXTPROC BindRenderbuffer;
        PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC CheckFramebufferStatus;
        PFNGLDELETEFRAMEBUFFERSEXTPROC DeleteFramebuffers;
        PFNGLDELETERENDERBUFFERSEXTPROC DeleteRenderbuffers;
        PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC FramebufferRenderbuffer;
        PFNGLGENFRAMEBUFFERSEXTPROC GenFramebuffers;
        PFNGLGENERATEMIPMAPEXTPROC GenerateMipmap;
        PFNGLGENRENDERBUFFERSEXTPROC GenRenderbuffers;
        PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC
            GetFramebufferAttachmentParameteriv;
        PFNGLGETRENDERBUFFERPARAMETERIVEXTPROC
            GetRenderbufferParameteriv;
        PFNGLISFRAMEBUFFEREXTPROC IsFramebuffer;
        PFNGLISRENDERBUFFEREXTPROC IsRenderbuffer;
        PFNGLRENDERBUFFERSTORAGEEXTPROC RenderbufferStorage;
        PCSTR MissingFbo = NULL;
        GLuint Framebuffers[2] = {0, 0};
        GLuint Renderbuffers[3] = {0, 0, 0};
        GLuint Texture = 0;
        BYTE BaseImage[3 * 5 * 4];
        BYTE Pixel[4];
        BYTE MipPixel[4];
        GLint FramebufferBinding = -1;
        GLint RenderbufferBinding = -1;
        GLint MaxColorAttachments = 0;
        GLint MaxRenderbufferSize = 0;
        GLint Width = -1;
        GLint Height = -1;
        GLint InternalFormat = 0;
        GLint RedBits = 0;
        GLint AlphaBits = 0;
        GLint DepthBits = 0;
        GLint StencilBits = 0;
        GLint ColorObjectType = 0;
        GLint ColorObjectName = 0;
        GLint DeletedColorObjectName = 0;
        GLint DeletedDepthObjectType = -1;
        GLint MipWidth1 = -1;
        GLint MipHeight1 = -1;
        GLint MipWidth2 = -1;
        GLint MipHeight2 = -1;
        GLenum CompleteStatus = 0;
        GLenum MismatchStatus = 0;
        GLenum RetainedStatus = 0;
        GLenum Error;
        GLboolean GeneratedFramebufferIsObject = GL_TRUE;
        GLboolean GeneratedRenderbufferIsObject = GL_TRUE;
        GLboolean BoundFramebufferIsObject = GL_FALSE;
        GLboolean BoundRenderbufferIsObject = GL_FALSE;
        GLboolean DeletedRenderbufferIsObject = GL_TRUE;
        ULONG Index;

        if (!Rpi5OpenGlProcSetAvailable(FboProcedures,
                                        ARRAYSIZE(FboProcedures),
                                        &MissingFbo))
        {
            Rpi5DiagPrint("RPI5_GL_TEST_024_FBO_OBJECT_FAIL missing=%s\n",
                          MissingFbo ? MissingFbo : "unknown");
            Success = FALSE;
        }
        else
        {
            BindFramebuffer = (PFNGLBINDFRAMEBUFFEREXTPROC)
                wglGetProcAddress("glBindFramebufferEXT");
            BindRenderbuffer = (PFNGLBINDRENDERBUFFEREXTPROC)
                wglGetProcAddress("glBindRenderbufferEXT");
            CheckFramebufferStatus =
                (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
                    wglGetProcAddress("glCheckFramebufferStatusEXT");
            DeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glDeleteFramebuffersEXT");
            DeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSEXTPROC)
                wglGetProcAddress("glDeleteRenderbuffersEXT");
            FramebufferRenderbuffer =
                (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)
                    wglGetProcAddress("glFramebufferRenderbufferEXT");
            GenFramebuffers = (PFNGLGENFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glGenFramebuffersEXT");
            GenerateMipmap = (PFNGLGENERATEMIPMAPEXTPROC)
                wglGetProcAddress("glGenerateMipmapEXT");
            GenRenderbuffers = (PFNGLGENRENDERBUFFERSEXTPROC)
                wglGetProcAddress("glGenRenderbuffersEXT");
            GetFramebufferAttachmentParameteriv =
                (PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC)
                    wglGetProcAddress(
                        "glGetFramebufferAttachmentParameterivEXT");
            GetRenderbufferParameteriv =
                (PFNGLGETRENDERBUFFERPARAMETERIVEXTPROC)
                    wglGetProcAddress("glGetRenderbufferParameterivEXT");
            IsFramebuffer = (PFNGLISFRAMEBUFFEREXTPROC)
                wglGetProcAddress("glIsFramebufferEXT");
            IsRenderbuffer = (PFNGLISRENDERBUFFEREXTPROC)
                wglGetProcAddress("glIsRenderbufferEXT");
            RenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEEXTPROC)
                wglGetProcAddress("glRenderbufferStorageEXT");

            while (glGetError() != GL_NO_ERROR)
                ;
            glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS_EXT,
                          &MaxColorAttachments);
            glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE_EXT,
                          &MaxRenderbufferSize);

            GenFramebuffers(2, Framebuffers);
            GenRenderbuffers(3, Renderbuffers);
            GeneratedFramebufferIsObject = IsFramebuffer(Framebuffers[0]);
            GeneratedRenderbufferIsObject =
                IsRenderbuffer(Renderbuffers[0]);

            BindRenderbuffer(GL_RENDERBUFFER_EXT, Renderbuffers[0]);
            BoundRenderbufferIsObject = IsRenderbuffer(Renderbuffers[0]);
            RenderbufferStorage(GL_RENDERBUFFER_EXT, GL_RGBA8, 7, 5);
            GetRenderbufferParameteriv(GL_RENDERBUFFER_EXT,
                                       GL_RENDERBUFFER_WIDTH_EXT,
                                       &Width);
            GetRenderbufferParameteriv(GL_RENDERBUFFER_EXT,
                                       GL_RENDERBUFFER_HEIGHT_EXT,
                                       &Height);
            GetRenderbufferParameteriv(
                GL_RENDERBUFFER_EXT,
                GL_RENDERBUFFER_INTERNAL_FORMAT_EXT,
                &InternalFormat);
            GetRenderbufferParameteriv(GL_RENDERBUFFER_EXT,
                                       GL_RENDERBUFFER_RED_SIZE_EXT,
                                       &RedBits);
            GetRenderbufferParameteriv(GL_RENDERBUFFER_EXT,
                                       GL_RENDERBUFFER_ALPHA_SIZE_EXT,
                                       &AlphaBits);

            BindRenderbuffer(GL_RENDERBUFFER_EXT, Renderbuffers[1]);
            RenderbufferStorage(GL_RENDERBUFFER_EXT,
                                GL_DEPTH24_STENCIL8_EXT, 7, 5);
            GetRenderbufferParameteriv(GL_RENDERBUFFER_EXT,
                                       GL_RENDERBUFFER_DEPTH_SIZE_EXT,
                                       &DepthBits);
            GetRenderbufferParameteriv(GL_RENDERBUFFER_EXT,
                                       GL_RENDERBUFFER_STENCIL_SIZE_EXT,
                                       &StencilBits);
            BindRenderbuffer(GL_RENDERBUFFER_EXT, Renderbuffers[2]);
            RenderbufferStorage(GL_RENDERBUFFER_EXT,
                                GL_DEPTH_COMPONENT24, 4, 4);

            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffers[0]);
            BoundFramebufferIsObject = IsFramebuffer(Framebuffers[0]);
            FramebufferRenderbuffer(GL_FRAMEBUFFER_EXT,
                                    GL_COLOR_ATTACHMENT0_EXT,
                                    GL_RENDERBUFFER_EXT,
                                    Renderbuffers[0]);
            FramebufferRenderbuffer(GL_FRAMEBUFFER_EXT,
                                    GL_DEPTH_ATTACHMENT_EXT,
                                    GL_RENDERBUFFER_EXT,
                                    Renderbuffers[1]);
            FramebufferRenderbuffer(GL_FRAMEBUFFER_EXT,
                                    GL_STENCIL_ATTACHMENT_EXT,
                                    GL_RENDERBUFFER_EXT,
                                    Renderbuffers[1]);
            CompleteStatus = CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,
                          &FramebufferBinding);
            glGetIntegerv(GL_RENDERBUFFER_BINDING_EXT,
                          &RenderbufferBinding);
            GetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER_EXT,
                GL_COLOR_ATTACHMENT0_EXT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,
                &ColorObjectType);
            GetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER_EXT,
                GL_COLOR_ATTACHMENT0_EXT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,
                &ColorObjectName);

            glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
            glClearDepth(0.625);
            glClearStencil(0x5a);
            glClear(GL_COLOR_BUFFER_BIT |
                    GL_DEPTH_BUFFER_BIT |
                    GL_STENCIL_BUFFER_BIT);
            ZeroMemory(Pixel, sizeof(Pixel));
            glReadPixels(3, 2, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, Pixel);

            FramebufferRenderbuffer(GL_FRAMEBUFFER_EXT,
                                    GL_DEPTH_ATTACHMENT_EXT,
                                    GL_RENDERBUFFER_EXT,
                                    Renderbuffers[2]);
            MismatchStatus = CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
            FramebufferRenderbuffer(GL_FRAMEBUFFER_EXT,
                                    GL_DEPTH_ATTACHMENT_EXT,
                                    GL_RENDERBUFFER_EXT,
                                    Renderbuffers[1]);

            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffers[1]);
            DeleteRenderbuffers(1, &Renderbuffers[0]);
            DeletedRenderbufferIsObject =
                IsRenderbuffer(Renderbuffers[0]);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffers[0]);
            RetainedStatus = CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
            GetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER_EXT,
                GL_COLOR_ATTACHMENT0_EXT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,
                &DeletedColorObjectName);

            DeleteRenderbuffers(1, &Renderbuffers[1]);
            GetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER_EXT,
                GL_DEPTH_ATTACHMENT_EXT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,
                &DeletedDepthObjectType);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);

            for (Index = 0; Index < ARRAYSIZE(BaseImage) / 4; Index++)
            {
                BaseImage[Index * 4 + 0] = 32;
                BaseImage[Index * 4 + 1] = 160;
                BaseImage[Index * 4 + 2] = 224;
                BaseImage[Index * 4 + 3] = 255;
            }
            glGenTextures(1, &Texture);
            glBindTexture(GL_TEXTURE_2D, Texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 3, 5, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, BaseImage);
            GenerateMipmap(GL_TEXTURE_2D);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 1,
                                     GL_TEXTURE_WIDTH, &MipWidth1);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 1,
                                     GL_TEXTURE_HEIGHT, &MipHeight1);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 2,
                                     GL_TEXTURE_WIDTH, &MipWidth2);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 2,
                                     GL_TEXTURE_HEIGHT, &MipHeight2);
            ZeroMemory(MipPixel, sizeof(MipPixel));
            glGetTexImage(GL_TEXTURE_2D, 2,
                          GL_RGBA, GL_UNSIGNED_BYTE, MipPixel);
            Error = glGetError();

            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteTextures(1, &Texture);
            DeleteFramebuffers(2, Framebuffers);
            DeleteRenderbuffers(1, &Renderbuffers[2]);
            BindRenderbuffer(GL_RENDERBUFFER_EXT, 0);

            if (Framebuffers[0] == 0 || Framebuffers[1] == 0 ||
                Renderbuffers[0] == 0 || Renderbuffers[1] == 0 ||
                Renderbuffers[2] == 0 ||
                GeneratedFramebufferIsObject ||
                GeneratedRenderbufferIsObject ||
                !BoundFramebufferIsObject || !BoundRenderbufferIsObject ||
                DeletedRenderbufferIsObject ||
                MaxColorAttachments != 1 || MaxRenderbufferSize < 1024 ||
                Width != 7 || Height != 5 ||
                InternalFormat != GL_RGBA8 ||
                RedBits != 8 || AlphaBits != 8 ||
                DepthBits != 24 || StencilBits != 8 ||
                CompleteStatus != GL_FRAMEBUFFER_COMPLETE_EXT ||
                MismatchStatus !=
                    GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT ||
                RetainedStatus != GL_FRAMEBUFFER_COMPLETE_EXT ||
                FramebufferBinding != (GLint)Framebuffers[0] ||
                RenderbufferBinding != (GLint)Renderbuffers[2] ||
                ColorObjectType != GL_RENDERBUFFER_EXT ||
                ColorObjectName != (GLint)Renderbuffers[0] ||
                DeletedColorObjectName != (GLint)Renderbuffers[0] ||
                DeletedDepthObjectType != GL_NONE ||
                Pixel[0] < 48 || Pixel[0] > 80 ||
                Pixel[1] < 112 || Pixel[1] > 144 ||
                Pixel[2] < 176 || Pixel[2] > 208 || Pixel[3] < 248 ||
                MipWidth1 != 1 || MipHeight1 != 2 ||
                MipWidth2 != 1 || MipHeight2 != 1 ||
                MipPixel[0] != 32 || MipPixel[1] != 160 ||
                MipPixel[2] != 224 || MipPixel[3] != 255 ||
                Error != GL_NO_ERROR ||
                strstr((const char *)Extensions,
                       "GL_EXT_framebuffer_object") == NULL)
            {
                Rpi5DiagPrint("RPI5_GL_TEST_024_FBO_OBJECT_FAIL fbo=%lu/%lu rb=%lu/%lu/%lu generated=%u/%u bound=%u/%u deleted=%u limits=%ld/%ld storage=%ldx%ld/0x%04lx/%ld/%ld/%ld/%ld status=0x%04x/0x%04x/0x%04x binding=%ld/%ld attachment=0x%04lx/%ld/%ld deleted-depth=0x%04lx pixel=%u,%u,%u,%u mip=%ldx%ld/%ldx%ld/%u,%u,%u,%u error=0x%04x advertised=%u\n",
                              Framebuffers[0], Framebuffers[1],
                              Renderbuffers[0], Renderbuffers[1],
                              Renderbuffers[2],
                              GeneratedFramebufferIsObject,
                              GeneratedRenderbufferIsObject,
                              BoundFramebufferIsObject,
                              BoundRenderbufferIsObject,
                              DeletedRenderbufferIsObject,
                              MaxColorAttachments,
                              MaxRenderbufferSize,
                              Width, Height, InternalFormat,
                              RedBits, AlphaBits, DepthBits, StencilBits,
                              CompleteStatus, MismatchStatus,
                              RetainedStatus,
                              FramebufferBinding, RenderbufferBinding,
                              ColorObjectType, ColorObjectName,
                              DeletedColorObjectName,
                              DeletedDepthObjectType,
                              Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                              MipWidth1, MipHeight1,
                              MipWidth2, MipHeight2,
                              MipPixel[0], MipPixel[1],
                              MipPixel[2], MipPixel[3],
                              Error,
                              strstr((const char *)Extensions,
                                     "GL_EXT_framebuffer_object") != NULL);
                Success = FALSE;
            }
            else
            {
                Rpi5DiagPrint("RPI5_GL_TEST_024_FBO_OBJECT_PASS limits=1/%ld storage=7x5/rgba8/d24s8 status=0x%04x mismatch=0x%04x retained=0x%04x pixel=%u,%u,%u,%u mip=1x2/1x1/%u,%u,%u,%u advertised=1\n",
                              MaxRenderbufferSize,
                              CompleteStatus,
                              MismatchStatus,
                              RetainedStatus,
                              Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                              MipPixel[0], MipPixel[1],
                              MipPixel[2], MipPixel[3]);
            }
        }
    }

    {
        static const GLfloat Triangle[6] =
        {
            -0.5f, -0.5f,
             0.5f, -0.5f,
             0.0f,  0.5f
        };
        PFNGLBINDFRAMEBUFFEREXTPROC BindFramebuffer =
            (PFNGLBINDFRAMEBUFFEREXTPROC)
                wglGetProcAddress("glBindFramebufferEXT");
        PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC CheckFramebufferStatus =
            (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
                wglGetProcAddress("glCheckFramebufferStatusEXT");
        PFNGLDELETEFRAMEBUFFERSEXTPROC DeleteFramebuffers =
            (PFNGLDELETEFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glDeleteFramebuffersEXT");
        PFNGLFRAMEBUFFERTEXTURE2DEXTPROC FramebufferTexture2D =
            (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
                wglGetProcAddress("glFramebufferTexture2DEXT");
        PFNGLGENFRAMEBUFFERSEXTPROC GenFramebuffers =
            (PFNGLGENFRAMEBUFFERSEXTPROC)
                wglGetProcAddress("glGenFramebuffersEXT");
        PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC
            GetFramebufferAttachmentParameteriv =
                (PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC)
                    wglGetProcAddress(
                        "glGetFramebufferAttachmentParameterivEXT");
        BYTE TextureImage[4 * 4 * 4];
        BYTE RetainedPixel[4];
        GLuint Framebuffers[2] = {0, 0};
        GLuint Texture = 0;
        GLuint DeletedTexture = 0;
        GLenum MissingStatus = 0;
        GLenum CompleteStatus = 0;
        GLenum RetainedStatus = 0;
        GLenum ClearError = GL_NO_ERROR;
        GLenum ImmediateError = GL_NO_ERROR;
        GLenum ArrayError = GL_NO_ERROR;
        GLenum ReadError = GL_NO_ERROR;
        GLenum RetainedError = GL_NO_ERROR;
        GLint RetainedObjectName = -1;
        GLboolean DeletedTextureIsObject = GL_TRUE;
        ULONG Index;
        BOOL ProceduresAvailable;

        ProceduresAvailable = BindFramebuffer != NULL &&
                              CheckFramebufferStatus != NULL &&
                              DeleteFramebuffers != NULL &&
                              FramebufferTexture2D != NULL &&
                              GenFramebuffers != NULL &&
                              GetFramebufferAttachmentParameteriv != NULL;
        if (!ProceduresAvailable)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_025_FBO_VALIDATION_FAIL missing-fbo-procedure\n");
            Success = FALSE;
        }
        else
        {
            while (glGetError() != GL_NO_ERROR)
                ;
            GenFramebuffers(2, Framebuffers);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffers[0]);
            MissingStatus = CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);

            glClear(GL_COLOR_BUFFER_BIT);
            ClearError = glGetError();

            glBegin(GL_TRIANGLES);
            glVertex2f(-0.5f, -0.5f);
            glVertex2f(0.5f, -0.5f);
            glVertex2f(0.0f, 0.5f);
            glEnd();
            ImmediateError = glGetError();

            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(2, GL_FLOAT, 0, Triangle);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glDisableClientState(GL_VERTEX_ARRAY);
            ArrayError = glGetError();

            ZeroMemory(RetainedPixel, sizeof(RetainedPixel));
            glReadPixels(0, 0, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, RetainedPixel);
            ReadError = glGetError();

            for (Index = 0; Index < ARRAYSIZE(TextureImage) / 4; Index++)
            {
                TextureImage[Index * 4 + 0] = 16;
                TextureImage[Index * 4 + 1] = 32;
                TextureImage[Index * 4 + 2] = 48;
                TextureImage[Index * 4 + 3] = 255;
            }
            glGenTextures(1, &Texture);
            DeletedTexture = Texture;
            glBindTexture(GL_TEXTURE_2D, Texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, TextureImage);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffers[1]);
            FramebufferTexture2D(GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_TEXTURE_2D,
                                 Texture,
                                 0);
            CompleteStatus = CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);

            glBindTexture(GL_TEXTURE_2D, 0);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffers[0]);
            glDeleteTextures(1, &Texture);
            DeletedTextureIsObject = glIsTexture(DeletedTexture);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, Framebuffers[1]);
            RetainedStatus = CheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
            GetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER_EXT,
                GL_COLOR_ATTACHMENT0_EXT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,
                &RetainedObjectName);

            glClearColor(0.15f, 0.35f, 0.55f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ZeroMemory(RetainedPixel, sizeof(RetainedPixel));
            glReadPixels(2, 2, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, RetainedPixel);
            RetainedError = glGetError();

            FramebufferTexture2D(GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_TEXTURE_2D,
                                 0,
                                 0);
            BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
            DeleteFramebuffers(2, Framebuffers);

            if (Framebuffers[0] == 0 || Framebuffers[1] == 0 ||
                DeletedTexture == 0 || DeletedTextureIsObject ||
                MissingStatus !=
                    GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT ||
                CompleteStatus != GL_FRAMEBUFFER_COMPLETE_EXT ||
                RetainedStatus != GL_FRAMEBUFFER_COMPLETE_EXT ||
                ClearError != GL_INVALID_FRAMEBUFFER_OPERATION_EXT ||
                ImmediateError !=
                    GL_INVALID_FRAMEBUFFER_OPERATION_EXT ||
                ArrayError != GL_INVALID_FRAMEBUFFER_OPERATION_EXT ||
                ReadError != GL_INVALID_FRAMEBUFFER_OPERATION_EXT ||
                RetainedObjectName != (GLint)DeletedTexture ||
                RetainedPixel[0] < 32 || RetainedPixel[0] > 48 ||
                RetainedPixel[1] < 80 || RetainedPixel[1] > 96 ||
                RetainedPixel[2] < 132 || RetainedPixel[2] > 148 ||
                RetainedPixel[3] != 255 ||
                RetainedError != GL_NO_ERROR)
            {
                Rpi5DiagPrint("RPI5_GL_TEST_025_FBO_VALIDATION_FAIL fbo=%lu/%lu texture=%lu is-object=%u status=0x%04x/0x%04x/0x%04x errors=0x%04x/0x%04x/0x%04x/0x%04x retained-name=%ld pixel=%u,%u,%u,%u retained-error=0x%04x\n",
                              Framebuffers[0], Framebuffers[1],
                              DeletedTexture,
                              DeletedTextureIsObject,
                              MissingStatus,
                              CompleteStatus,
                              RetainedStatus,
                              ClearError,
                              ImmediateError,
                              ArrayError,
                              ReadError,
                              RetainedObjectName,
                              RetainedPixel[0], RetainedPixel[1],
                              RetainedPixel[2], RetainedPixel[3],
                              RetainedError);
                Success = FALSE;
            }
            else
            {
                Rpi5DiagPrint("RPI5_GL_TEST_025_FBO_VALIDATION_PASS incomplete=0x%04x errors=0x%04x/0x%04x/0x%04x/0x%04x retained=0x%04x/%ld pixel=%u,%u,%u,%u\n",
                              MissingStatus,
                              ClearError,
                              ImmediateError,
                              ArrayError,
                              ReadError,
                              RetainedStatus,
                              RetainedObjectName,
                              RetainedPixel[0], RetainedPixel[1],
                              RetainedPixel[2], RetainedPixel[3]);
            }
        }
    }

    {
        PFNGLBLENDCOLORPROC BlendColor =
            (PFNGLBLENDCOLORPROC)wglGetProcAddress("glBlendColor");
        PFNGLBLENDEQUATIONPROC BlendEquation =
            (PFNGLBLENDEQUATIONPROC)wglGetProcAddress("glBlendEquation");
        PFNGLBLENDEQUATIONSEPARATEPROC BlendEquationSeparate =
            (PFNGLBLENDEQUATIONSEPARATEPROC)
                wglGetProcAddress("glBlendEquationSeparate");
        PFNGLBLENDFUNCSEPARATEPROC BlendFuncSeparate =
            (PFNGLBLENDFUNCSEPARATEPROC)
                wglGetProcAddress("glBlendFuncSeparate");
        RPI5VC4_OGL_STATS BlendStatsBefore;
        RPI5VC4_OGL_STATS BlendStatsAfter;
        GLfloat BlendColorQuery[4] = {0};
        GLint EquationRgb = 0;
        GLint EquationAlpha = 0;
        GLint SourceRgb = 0;
        GLint DestinationRgb = 0;
        GLint SourceAlpha = 0;
        GLint DestinationAlpha = 0;
        GLenum BlendError = GL_NO_ERROR;
        BOOL BlendProcedures = BlendColor != NULL &&
                               BlendEquation != NULL &&
                               BlendEquationSeparate != NULL &&
                               BlendFuncSeparate != NULL;

        ZeroMemory(&BlendStatsBefore, sizeof(BlendStatsBefore));
        BlendStatsBefore.Size = sizeof(BlendStatsBefore);
        ZeroMemory(&BlendStatsAfter, sizeof(BlendStatsAfter));
        BlendStatsAfter.Size = sizeof(BlendStatsAfter);
        ZeroMemory(Pixel, sizeof(Pixel));
        if (!BlendProcedures || GetStats == NULL ||
            !GetStats(&BlendStatsBefore))
        {
            Rpi5DiagPrint("RPI5_GL_TEST_026_BLEND_FAIL procedures=%u stats=%p\n",
                          BlendProcedures,
                          GetStats);
            Success = FALSE;
        }
        else
        {
            while (glGetError() != GL_NO_ERROR)
                ;
            glDisable(GL_BLEND);
            BlendColor(0.25f, 0.5f, 0.75f, 0.5f);
            BlendEquation(GL_FUNC_REVERSE_SUBTRACT);
            BlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
            BlendFuncSeparate(GL_CONSTANT_COLOR,
                              GL_ONE,
                              GL_ZERO,
                              GL_ONE);
            glClearColor(0.1f, 0.2f, 0.3f, 0.75f);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_BLEND);
            glBegin(GL_TRIANGLES);
            glColor4f(0.8f, 0.4f, 0.2f, 0.25f);
            glVertex2f(-0.8f, -0.8f);
            glVertex2f(0.8f, -0.8f);
            glVertex2f(0.0f, 0.8f);
            glEnd();
            glFinish();
            glReadPixels(64,
                         64,
                         1,
                         1,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         Pixel);
            glGetFloatv(GL_BLEND_COLOR, BlendColorQuery);
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &EquationRgb);
            glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &EquationAlpha);
            glGetIntegerv(GL_BLEND_SRC_RGB, &SourceRgb);
            glGetIntegerv(GL_BLEND_DST_RGB, &DestinationRgb);
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &SourceAlpha);
            glGetIntegerv(GL_BLEND_DST_ALPHA, &DestinationAlpha);
            BlendError = glGetError();
            GetStats(&BlendStatsAfter);

            glDisable(GL_BLEND);
            BlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
            BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            BlendColor(0.0f, 0.0f, 0.0f, 0.0f);

            if (BlendError != GL_NO_ERROR ||
                Pixel[0] > 4 || Pixel[1] > 4 ||
                Pixel[2] < 34 || Pixel[2] > 44 ||
                Pixel[3] < 186 || Pixel[3] > 196 ||
                BlendColorQuery[0] != 0.25f ||
                BlendColorQuery[1] != 0.5f ||
                BlendColorQuery[2] != 0.75f ||
                BlendColorQuery[3] != 0.5f ||
                EquationRgb != GL_FUNC_REVERSE_SUBTRACT ||
                EquationAlpha != GL_FUNC_ADD ||
                SourceRgb != GL_CONSTANT_COLOR ||
                DestinationRgb != GL_ONE ||
                SourceAlpha != GL_ZERO ||
                DestinationAlpha != GL_ONE ||
                BlendStatsAfter.HardwareTriangleCount !=
                    BlendStatsBefore.HardwareTriangleCount + 1 ||
                BlendStatsAfter.SoftwareTriangleCount !=
                    BlendStatsBefore.SoftwareTriangleCount ||
                BlendStatsAfter.HardwareTriangleFailureCount !=
                    BlendStatsBefore.HardwareTriangleFailureCount)
            {
                Rpi5DiagPrint("RPI5_GL_TEST_026_BLEND_FAIL pixel=%u,%u,%u,%u color=%g,%g,%g,%g equation=0x%04lx/0x%04lx factors=0x%04lx/0x%04lx/0x%04lx/0x%04lx hw=%lu/%lu sw=%lu/%lu failures=%lu/%lu error=0x%04x\n",
                              Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                              BlendColorQuery[0], BlendColorQuery[1],
                              BlendColorQuery[2], BlendColorQuery[3],
                              EquationRgb, EquationAlpha,
                              SourceRgb, DestinationRgb,
                              SourceAlpha, DestinationAlpha,
                              BlendStatsBefore.HardwareTriangleCount,
                              BlendStatsAfter.HardwareTriangleCount,
                              BlendStatsBefore.SoftwareTriangleCount,
                              BlendStatsAfter.SoftwareTriangleCount,
                              BlendStatsBefore.HardwareTriangleFailureCount,
                              BlendStatsAfter.HardwareTriangleFailureCount,
                              BlendError);
                Success = FALSE;
            }
            else
            {
                Rpi5DiagPrint("RPI5_GL_TEST_026_BLEND_PASS pixel=%u,%u,%u,%u color=0.25,0.5,0.75,0.5 equation=reverse-subtract/add factors=constant-color/one/zero/one hardware=1 software=0\n",
                              Pixel[0], Pixel[1], Pixel[2], Pixel[3]);
            }
        }
    }

    {
        static const GLfloat IndexedTriangle[6] =
        {
            -0.8f, -0.8f,
             0.8f, -0.8f,
             0.0f,  0.8f
        };
        static const GLubyte Indices[3] = {0, 1, 2};
        PFNGLDRAWRANGEELEMENTSPROC DrawRangeElements =
            (PFNGLDRAWRANGEELEMENTSPROC)
                wglGetProcAddress("glDrawRangeElements");
        RPI5VC4_OGL_STATS RangeStatsBefore;
        RPI5VC4_OGL_STATS RangeStatsAfter;
        GLenum RangeError = GL_NO_ERROR;
        GLenum CountError = GL_NO_ERROR;
        GLenum BeginEndError = GL_NO_ERROR;
        GLenum DrawError = GL_NO_ERROR;

        ZeroMemory(&RangeStatsBefore, sizeof(RangeStatsBefore));
        RangeStatsBefore.Size = sizeof(RangeStatsBefore);
        ZeroMemory(&RangeStatsAfter, sizeof(RangeStatsAfter));
        RangeStatsAfter.Size = sizeof(RangeStatsAfter);
        ZeroMemory(Pixel, sizeof(Pixel));
        ZeroMemory(SecondPixel, sizeof(SecondPixel));
        if (DrawRangeElements == NULL || GetStats == NULL ||
            !GetStats(&RangeStatsBefore))
        {
            Rpi5DiagPrint("RPI5_GL_TEST_027_DRAW_RANGE_FAIL procedure=%p stats=%p\n",
                          DrawRangeElements,
                          GetStats);
            Success = FALSE;
        }
        else
        {
            while (glGetError() != GL_NO_ERROR)
                ;
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_TEXTURE_2D);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            DrawRangeElements(GL_TRIANGLES,
                              2,
                              1,
                              3,
                              GL_UNSIGNED_BYTE,
                              Indices);
            RangeError = glGetError();
            DrawRangeElements(GL_TRIANGLES,
                              0,
                              2,
                              -1,
                              GL_UNSIGNED_BYTE,
                              Indices);
            CountError = glGetError();
            glBegin(GL_TRIANGLES);
            DrawRangeElements(GL_TRIANGLES,
                              0,
                              2,
                              3,
                              GL_UNSIGNED_BYTE,
                              Indices);
            glEnd();
            BeginEndError = glGetError();

            glColor4ub(0, 255, 0, 255);
            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(2, GL_FLOAT, 0, IndexedTriangle);
            DrawRangeElements(GL_TRIANGLES,
                              0,
                              2,
                              3,
                              GL_UNSIGNED_BYTE,
                              Indices);
            glDisableClientState(GL_VERTEX_ARRAY);
            glFinish();
            glReadPixels(64,
                         64,
                         1,
                         1,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         Pixel);
            glReadPixels(0,
                         0,
                         1,
                         1,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         SecondPixel);
            DrawError = glGetError();
            GetStats(&RangeStatsAfter);

            if (RangeError != GL_INVALID_VALUE ||
                CountError != GL_INVALID_VALUE ||
                BeginEndError != GL_INVALID_OPERATION ||
                DrawError != GL_NO_ERROR ||
                Pixel[0] > 32 || Pixel[1] < 224 || Pixel[2] > 32 ||
                SecondPixel[0] > 32 ||
                SecondPixel[1] > 32 ||
                SecondPixel[2] > 32 ||
                RangeStatsAfter.HardwareTriangleCount !=
                    RangeStatsBefore.HardwareTriangleCount + 1 ||
                RangeStatsAfter.SoftwareTriangleCount !=
                    RangeStatsBefore.SoftwareTriangleCount ||
                RangeStatsAfter.HardwareTriangleFailureCount !=
                    RangeStatsBefore.HardwareTriangleFailureCount)
            {
                Rpi5DiagPrint("RPI5_GL_TEST_027_DRAW_RANGE_FAIL errors=0x%04x/0x%04x/0x%04x/0x%04x center=%u,%u,%u,%u corner=%u,%u,%u,%u hw=%lu/%lu sw=%lu/%lu failures=%lu/%lu\n",
                              RangeError,
                              CountError,
                              BeginEndError,
                              DrawError,
                              Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                              SecondPixel[0], SecondPixel[1],
                              SecondPixel[2], SecondPixel[3],
                              RangeStatsBefore.HardwareTriangleCount,
                              RangeStatsAfter.HardwareTriangleCount,
                              RangeStatsBefore.SoftwareTriangleCount,
                              RangeStatsAfter.SoftwareTriangleCount,
                              RangeStatsBefore.HardwareTriangleFailureCount,
                              RangeStatsAfter.HardwareTriangleFailureCount);
                Success = FALSE;
            }
            else
            {
                Rpi5DiagPrint("RPI5_GL_TEST_027_DRAW_RANGE_PASS errors=invalid-range/invalid-count/inside-begin-end center=%u,%u,%u,%u corner=%u,%u,%u,%u hardware=1 software=0\n",
                              Pixel[0], Pixel[1], Pixel[2], Pixel[3],
                              SecondPixel[0], SecondPixel[1],
                              SecondPixel[2], SecondPixel[3]);
            }
        }
    }

    {
        PFNGLTEXIMAGE3DPROC TexImage3D =
            (PFNGLTEXIMAGE3DPROC)wglGetProcAddress("glTexImage3D");
        PFNGLTEXSUBIMAGE3DPROC TexSubImage3D =
            (PFNGLTEXSUBIMAGE3DPROC)wglGetProcAddress("glTexSubImage3D");
        GLubyte Upload[5 * 4 * 3 * 4];
        GLubyte Packed[5 * 4 * 3 * 4];
        GLubyte Tight[3 * 2 * 2 * 4];
        GLuint Patch = 0xFF3366CC;
        GLuint Texture3D = 0;
        GLint Width3D = 0;
        GLint Height3D = 0;
        GLint Depth3D = 0;
        GLint Binding3D = 0;
        GLint Maximum3D = 0;
        GLenum UploadError = GL_NO_ERROR;
        GLenum QueryError = GL_NO_ERROR;
        GLenum PatchError = GL_NO_ERROR;
        GLenum MismatchError = GL_NO_ERROR;
        GLenum FormatError = GL_NO_ERROR;
        GLenum LevelError = GL_NO_ERROR;
        BOOL UploadMatches = TRUE;
        BOOL PatchMatches = FALSE;
        ULONG X;
        ULONG Y;
        ULONG Z;

        ZeroMemory(Upload, sizeof(Upload));
        FillMemory(Packed, sizeof(Packed), 0xCD);
        ZeroMemory(Tight, sizeof(Tight));
        if (TexImage3D == NULL || TexSubImage3D == NULL)
        {
            Rpi5DiagPrint("RPI5_GL_TEST_028_TEXTURE_3D_FAIL procedures=%p/%p\n",
                          TexImage3D,
                          TexSubImage3D);
            Success = FALSE;
        }
        else
        {
            for (Z = 0; Z < 2; Z++)
            {
                for (Y = 0; Y < 2; Y++)
                {
                    for (X = 0; X < 3; X++)
                    {
                        ULONG Offset =
                            (((Z + 1) * 4 + (Y + 1)) * 5 + X + 1) * 4;
                        Upload[Offset + 0] =
                            (GLubyte)(16 + Z * 80 + Y * 24 + X * 4);
                        Upload[Offset + 1] = Upload[Offset + 0] + 1;
                        Upload[Offset + 2] = Upload[Offset + 0] + 2;
                        Upload[Offset + 3] = 255;
                    }
                }
            }

            while (glGetError() != GL_NO_ERROR)
                ;
            glGenTextures(1, &Texture3D);
            glBindTexture(GL_TEXTURE_3D, Texture3D);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 5);
            glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 4);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
            glPixelStorei(GL_UNPACK_SKIP_IMAGES, 1);
            TexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 3, 2, 2, 0,
                       GL_RGBA, GL_UNSIGNED_BYTE, Upload);
            UploadError = glGetError();

            glGetTexLevelParameteriv(GL_TEXTURE_3D, 0,
                                     GL_TEXTURE_WIDTH, &Width3D);
            glGetTexLevelParameteriv(GL_TEXTURE_3D, 0,
                                     GL_TEXTURE_HEIGHT, &Height3D);
            glGetTexLevelParameteriv(GL_TEXTURE_3D, 0,
                                     GL_TEXTURE_DEPTH, &Depth3D);
            glGetIntegerv(GL_TEXTURE_BINDING_3D, &Binding3D);
            glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &Maximum3D);
            QueryError = glGetError();

            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_PACK_ROW_LENGTH, 5);
            glPixelStorei(GL_PACK_IMAGE_HEIGHT, 4);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
            glPixelStorei(GL_PACK_SKIP_ROWS, 1);
            glPixelStorei(GL_PACK_SKIP_IMAGES, 1);
            glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA,
                          GL_UNSIGNED_BYTE, Packed);
            if (QueryError == GL_NO_ERROR)
                QueryError = glGetError();
            for (Z = 0; Z < 2; Z++)
            {
                for (Y = 0; Y < 2; Y++)
                {
                    for (X = 0; X < 3; X++)
                    {
                        ULONG Offset =
                            (((Z + 1) * 4 + (Y + 1)) * 5 + X + 1) * 4;
                        if (memcmp(Packed + Offset, Upload + Offset, 4) != 0)
                            UploadMatches = FALSE;
                    }
                }
            }

            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
            TexSubImage3D(GL_TEXTURE_3D, 0, 1, 0, 1, 1, 1, 1,
                          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, &Patch);
            PatchError = glGetError();

            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            glPixelStorei(GL_PACK_SKIP_IMAGES, 0);
            glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA,
                          GL_UNSIGNED_BYTE, Tight);
            if (QueryError == GL_NO_ERROR)
                QueryError = glGetError();
            PatchMatches =
                Tight[((1 * 2 + 0) * 3 + 1) * 4 + 0] == 0xCC &&
                Tight[((1 * 2 + 0) * 3 + 1) * 4 + 1] == 0x66 &&
                Tight[((1 * 2 + 0) * 3 + 1) * 4 + 2] == 0x33 &&
                Tight[((1 * 2 + 0) * 3 + 1) * 4 + 3] == 0xFF;

            TexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0,
                       GL_RGBA, GL_UNSIGNED_SHORT_5_6_5, Upload);
            MismatchError = glGetError();
            glGetTexImage(GL_TEXTURE_3D, 0, GL_COLOR_INDEX,
                          GL_UNSIGNED_BYTE, Tight);
            FormatError = glGetError();
            glGetTexImage(GL_TEXTURE_3D, 99, GL_RGBA,
                          GL_UNSIGNED_BYTE, Tight);
            LevelError = glGetError();

            if (UploadError != GL_NO_ERROR || QueryError != GL_NO_ERROR ||
                PatchError != GL_NO_ERROR ||
                Width3D != 3 || Height3D != 2 || Depth3D != 2 ||
                Binding3D != (GLint)Texture3D || Maximum3D < 256 ||
                !UploadMatches || !PatchMatches ||
                MismatchError != GL_INVALID_OPERATION ||
                FormatError != GL_INVALID_ENUM ||
                LevelError != GL_INVALID_VALUE)
            {
                Rpi5DiagPrint("RPI5_GL_TEST_028_TEXTURE_3D_FAIL texture=%lu dimensions=%ldx%ldx%ld binding=%ld max=%ld matches=%u/%u errors=0x%04x/0x%04x/0x%04x/0x%04x/0x%04x/0x%04x\n",
                              Texture3D,
                              Width3D,
                              Height3D,
                              Depth3D,
                              Binding3D,
                              Maximum3D,
                              UploadMatches,
                              PatchMatches,
                              UploadError,
                              QueryError,
                              PatchError,
                              MismatchError,
                              FormatError,
                              LevelError);
                Success = FALSE;
            }
            else
            {
                Rpi5DiagPrint("RPI5_GL_TEST_028_TEXTURE_3D_PASS dimensions=3x2x2 pack-image=1 packed-subimage=1 errors=invalid-operation/invalid-enum/invalid-value\n");
            }
        }

        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_IMAGES, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
        if (Texture3D != 0)
            glDeleteTextures(1, &Texture3D);
    }

    OpenGl12 = Rpi5OpenGlProcSetAvailable(OpenGl12Procedures,
                                          ARRAYSIZE(OpenGl12Procedures),
                                          &MissingOpenGl12);
    OpenGl13 = Rpi5OpenGlProcSetAvailable(OpenGl13Procedures,
                                          ARRAYSIZE(OpenGl13Procedures),
                                          &MissingOpenGl13);
    OpenGl14 = Rpi5OpenGlProcSetAvailable(OpenGl14Procedures,
                                          ARRAYSIZE(OpenGl14Procedures),
                                          &MissingOpenGl14);
    OpenGl15 = Rpi5OpenGlProcSetAvailable(OpenGl15Procedures,
                                          ARRAYSIZE(OpenGl15Procedures),
                                          &MissingOpenGl15);
    OpenGl20 = Rpi5OpenGlProcSetAvailable(OpenGl20Procedures,
                                          ARRAYSIZE(OpenGl20Procedures),
                                          &MissingOpenGl20);
    OpenGl2 = OpenGlMajor >= 2 && ShaderMilestone && OpenGl12 &&
              OpenGl13 && OpenGl14 && OpenGl15 && OpenGl20;
    OpenGl3 = Rpi5OpenGlProcSetAvailable(OpenGl3Procedures,
                                         ARRAYSIZE(OpenGl3Procedures),
                                         &MissingOpenGl3);
    if (OpenGlMajor < 3)
        OpenGl3 = FALSE;
    if (OpenGl2)
        MissingOpenGl2 = NULL;
    else if (OpenGlMajor < 2)
        MissingOpenGl2 = "GL_VERSION";
    else if (!ShaderMilestone)
        MissingOpenGl2 = MissingShader;
    else if (!OpenGl12)
        MissingOpenGl2 = MissingOpenGl12;
    else if (!OpenGl13)
        MissingOpenGl2 = MissingOpenGl13;
    else if (!OpenGl14)
        MissingOpenGl2 = MissingOpenGl14;
    else if (!OpenGl15)
        MissingOpenGl2 = MissingOpenGl15;
    else
        MissingOpenGl2 = MissingOpenGl20;
    Rpi5DiagPrint("RPI5_GL_CAPABILITY api=%u.%u level1=1 stages=1.2:%u,1.3:%u,1.4:%u,1.5:%u,2.0:%u shader-milestone=%u level2=%u level3=%u missing2=%s missing3=%s\n",
                  OpenGlMajor,
                  OpenGlMinor,
                  OpenGl12,
                  OpenGl13,
                  OpenGl14,
                  OpenGl15,
                  OpenGl20,
                  ShaderMilestone,
                  OpenGl2,
                  OpenGl3,
                  MissingOpenGl2 ? MissingOpenGl2 : "none",
                  MissingOpenGl3 ? MissingOpenGl3 : "none");

    ZeroMemory(&Stats, sizeof(Stats));
    Stats.Size = sizeof(Stats);
    if (GetStats)
        GetStats(&Stats);
    BenchmarkHardwareTriangleBase = Stats.HardwareTriangleCount;
    BenchmarkSoftwareTriangleBase = Stats.SoftwareTriangleCount;
    BenchmarkTriangleFailureBase = Stats.HardwareTriangleFailureCount;

    if (!QueryPerformanceFrequency(&Frequency) ||
        !QueryPerformanceCounter(&Start))
    {
        Rpi5DiagPrint("RPI5_GL_BENCH_FAIL timer-error=%lu\n", GetLastError());
        Success = FALSE;
        goto Cleanup;
    }
    for (Frame = 0; Frame < Frames; Frame++)
    {
        GLfloat XOffset = (GLfloat)(Frame & 7) / 64.0f;

        glClearColor(0.02f, 0.03f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glColor3f(0.9f, 0.2f, 0.1f);
        glVertex2f(-0.8f + XOffset, -0.8f);
        glColor3f(0.1f, 0.9f, 0.2f);
        glVertex2f(0.8f + XOffset, -0.8f);
        glColor3f(0.2f, 0.1f, 0.9f);
        glVertex2f(XOffset, 0.8f);
        glEnd();
        if (DoubleBuffered)
            SwapBuffers(DeviceContext);
        else
            glFinish();
    }
    glFinish();
    QueryPerformanceCounter(&End);
    ElapsedMicroseconds = (ULONGLONG)(End.QuadPart - Start.QuadPart) *
                          1000000ULL / (ULONGLONG)Frequency.QuadPart;
    if (!ElapsedMicroseconds)
        ElapsedMicroseconds = 1;
    Rpi5DiagPrint("RPI5_GL_BENCH workload=clear-triangle-swap frames=%lu width=128 height=128 elapsed_us=%I64u fps_x100=%I64u megapixels_x100=%I64u\n",
                  Frames,
                  ElapsedMicroseconds,
                  (ULONGLONG)Frames * 100000000ULL / ElapsedMicroseconds,
                  (ULONGLONG)Frames * 128ULL * 128ULL * 100ULL /
                      ElapsedMicroseconds);
    if (GetStats)
    {
        ZeroMemory(&Stats, sizeof(Stats));
        Stats.Size = sizeof(Stats);
        if (GetStats(&Stats))
        {
            Rpi5DiagPrint("RPI5_GL_BENCH_BACKEND hardware_clears=%lu software_clears=%lu clear_failures=%lu clear_status=%lu hardware_triangles=%lu software_triangles=%lu triangle_failures=%lu triangle_status=%lu covered=%lu program-triangles=%lu\n",
                          Stats.HardwareClearCount,
                          Stats.SoftwareClearCount,
                          Stats.HardwareClearFailureCount,
                          Stats.LastHardwareStatus,
                          Stats.HardwareTriangleCount,
                          Stats.SoftwareTriangleCount,
                          Stats.HardwareTriangleFailureCount,
                          Stats.LastTriangleStatus,
                          Stats.LastTriangleCoveredPixels,
                          Stats.HardwareProgramTriangleCount);
            if (Stats.HardwareTriangleCount - BenchmarkHardwareTriangleBase != Frames ||
                Stats.SoftwareTriangleCount != BenchmarkSoftwareTriangleBase ||
                Stats.HardwareTriangleFailureCount != BenchmarkTriangleFailureBase)
            {
                Rpi5DiagPrint("RPI5_GL_BENCH_FAIL triangle-backend-mismatch expected_delta=%lu actual_delta=%lu software_delta=%lu failure_delta=%lu\n",
                              Frames,
                              Stats.HardwareTriangleCount - BenchmarkHardwareTriangleBase,
                              Stats.SoftwareTriangleCount - BenchmarkSoftwareTriangleBase,
                              Stats.HardwareTriangleFailureCount - BenchmarkTriangleFailureBase);
                Success = FALSE;
            }
        }
    }

Cleanup:
    if (RenderContext)
    {
        if (UseProgram != NULL)
            UseProgram(0);
        if (Program != 0 && DeleteProgram != NULL)
            DeleteProgram(Program);
        if (VertexShader != 0 && DeleteShader != NULL &&
            (IsShader == NULL || IsShader(VertexShader)))
        {
            DeleteShader(VertexShader);
        }
        if (FragmentShader != 0 && DeleteShader != NULL &&
            (IsShader == NULL || IsShader(FragmentShader)))
        {
            DeleteShader(FragmentShader);
        }
        if (RejectedShader != 0 && DeleteShader != NULL &&
            (IsShader == NULL || IsShader(RejectedShader)))
        {
            DeleteShader(RejectedShader);
        }
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(RenderContext);
    }
    if (DeviceContext && Window)
        ReleaseDC(Window, DeviceContext);
    if (Window)
        DestroyWindow(Window);
    if (Registered)
        UnregisterClassW(ClassName, Instance);
    Rpi5DiagPrint(Success ? "RPI5_GL_BASELINE_PASS\n" :
                               "RPI5_GL_BASELINE_FAIL\n");
    return Success;
}

static VOID
Rpi5DiagPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[1024];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    fflush(stdout);
    OutputDebugStringA(Buffer);
}

static VOID
Rpi5WideToUtf8(
    _In_opt_z_ PCWSTR Source,
    _Out_writes_(DestinationCount) PCHAR Destination,
    _In_ SIZE_T DestinationCount)
{
    INT Length;

    if (!Source)
        Source = L"(null)";
    if (!DestinationCount)
        return;
    Length = WideCharToMultiByte(CP_UTF8, 0, Source, -1, Destination,
                                 (INT)DestinationCount, NULL, NULL);
    if (!Length)
    {
        Destination[0] = '?';
        if (DestinationCount > 1)
            Destination[1] = ANSI_NULL;
    }
}

static BOOL
Rpi5StartsWithInsensitive(
    _In_z_ PCWSTR String,
    _In_z_ PCWSTR Prefix)
{
    SIZE_T Length = wcslen(Prefix);

    return _wcsnicmp(String, Prefix, Length) == 0;
}

static HANDLE
Rpi5OpenFirstInterface(
    _In_ const GUID *InterfaceGuid,
    _In_ DWORD Flags,
    _Out_ PULONG Found)
{
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *Detail = NULL;
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    HDEVINFO DeviceInfo;
    HANDLE Device = INVALID_HANDLE_VALUE;
    DWORD Required;
    DWORD SavedError = ERROR_NOT_FOUND;
    ULONG Index;

    *Found = 0;
    DeviceInfo = SetupDiGetClassDevsW(InterfaceGuid, NULL, NULL,
                                      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DeviceInfo == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    for (Index = 0;; Index++)
    {
        ZeroMemory(&InterfaceData, sizeof(InterfaceData));
        InterfaceData.cbSize = sizeof(InterfaceData);
        if (!SetupDiEnumDeviceInterfaces(DeviceInfo, NULL, InterfaceGuid,
                                         Index, &InterfaceData))
        {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                SavedError = GetLastError();
            break;
        }
        (*Found)++;
        if (Index != 0)
            continue;

        Required = 0;
        SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData, NULL, 0,
                                         &Required, NULL);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            Required < sizeof(*Detail))
        {
            SavedError = GetLastError();
            continue;
        }
        Detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Required);
        if (!Detail)
        {
            SavedError = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        Detail->cbSize = sizeof(*Detail);
        if (!SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData,
                                              Detail, Required, NULL, NULL))
        {
            SavedError = GetLastError();
            HeapFree(GetProcessHeap(), 0, Detail);
            Detail = NULL;
            continue;
        }
        Device = CreateFileW(Detail->DevicePath,
                             GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,
                             OPEN_EXISTING,
                             Flags,
                             NULL);
        if (Device == INVALID_HANDLE_VALUE)
            SavedError = GetLastError();
        HeapFree(GetProcessHeap(), 0, Detail);
        Detail = NULL;
    }

    SetupDiDestroyDeviceInfoList(DeviceInfo);
    if (Device == INVALID_HANDLE_VALUE)
        SetLastError(SavedError);
    return Device;
}

static BOOL
Rpi5TestPmi(VOID)
{
    PMI_CAPABILITIES Capabilities;
    PMI_CONFIGURATION Configuration;
    PMI_MEASUREMENT_DATA FirstMeasurement;
    PMI_MEASUREMENT_DATA SecondMeasurement;
    PMI_EVENT Event;
    OVERLAPPED Overlapped;
    HANDLE Device;
    HANDLE EventHandle;
    ULONG Found;
    DWORD Returned;
    BOOL Success = TRUE;

    Device = Rpi5OpenFirstInterface(&GUID_DEVICE_POWER_METER,
                                    FILE_ATTRIBUTE_NORMAL, &Found);
    if (Device == INVALID_HANDLE_VALUE || Found != 1)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL interfaces=%lu error=%lu\n",
                      Found, GetLastError());
        if (Device != INVALID_HANDLE_VALUE)
            CloseHandle(Device);
        return FALSE;
    }

    ZeroMemory(&Capabilities, sizeof(Capabilities));
    Capabilities.Version = PMI_VERSION;
    Capabilities.Size = sizeof(Capabilities);
    Capabilities.CapabilityType = PmiReportedCapabilities;
    if (!DeviceIoControl(Device, IOCTL_PMI_GET_CAPABILITIES,
                         &Capabilities, sizeof(Capabilities),
                         &Capabilities, sizeof(Capabilities),
                         &Returned, NULL) ||
        Returned != sizeof(Capabilities) ||
        Capabilities.Version != PMI_VERSION ||
        Capabilities.Size != sizeof(Capabilities) ||
        Capabilities.CapabilityType != PmiReportedCapabilities ||
        !(Capabilities.Capabilities.ReportedCapabilities.Flags &
          PMI_CAPABILITIES_SUPPORT_MEASUREMENT) ||
        Capabilities.Capabilities.ReportedCapabilities.MeasurementUnit !=
          PmiMeasurementUnitMilliWatt ||
        Capabilities.Capabilities.ReportedCapabilities.MeasurementType !=
          PmiMeasurementTypeOutput ||
        Capabilities.Capabilities.ReportedCapabilities.Writeable ||
        wcscmp(Capabilities.Capabilities.ReportedCapabilities.ModelNumber,
               L"DA9091") != 0)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL reported-capabilities error=%lu bytes=%lu\n",
                      GetLastError(), Returned);
        Success = FALSE;
        goto Done;
    }
    Rpi5DiagPrint("RPI5_PMI_CAPABILITIES model=DA9091 flags=0x%08lx unit=mW type=output writable=0\n",
                  Capabilities.Capabilities.ReportedCapabilities.Flags);

    ZeroMemory(&Capabilities, sizeof(Capabilities));
    Capabilities.Version = PMI_VERSION;
    Capabilities.Size = sizeof(Capabilities);
    Capabilities.CapabilityType = PmiMeteredHardware;
    SetLastError(ERROR_SUCCESS);
    if (DeviceIoControl(Device, IOCTL_PMI_GET_CAPABILITIES,
                        &Capabilities, sizeof(Capabilities),
                        &Capabilities, sizeof(Capabilities),
                        &Returned, NULL) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL metered-hardware error=%lu bytes=%lu\n",
                      GetLastError(), Returned);
        Success = FALSE;
        goto Done;
    }
    Rpi5DiagPrint("RPI5_PMI_METERED_HARDWARE unavailable scope=pmic-managed-rails\n");

    ZeroMemory(&Configuration, sizeof(Configuration));
    Configuration.Version = PMI_VERSION;
    Configuration.Size = sizeof(Configuration);
    Configuration.ConfigurationType = PmiMeasurementConfiguration;
    if (!DeviceIoControl(Device, IOCTL_PMI_GET_CONFIGURATION,
                         &Configuration, sizeof(Configuration),
                         &Configuration, sizeof(Configuration),
                         &Returned, NULL) ||
        Returned != sizeof(Configuration) ||
        Configuration.Version != PMI_VERSION ||
        Configuration.Size != sizeof(Configuration) ||
        Configuration.ConfigurationType != PmiMeasurementConfiguration ||
        Configuration.Configuration.MeasurementConfiguration.AveragingInterval != 0)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL configuration error=%lu bytes=%lu\n",
                      GetLastError(), Returned);
        Success = FALSE;
        goto Done;
    }

    ZeroMemory(&FirstMeasurement, sizeof(FirstMeasurement));
    if (!DeviceIoControl(Device, IOCTL_PMI_GET_MEASUREMENT,
                         NULL, 0, &FirstMeasurement,
                         sizeof(FirstMeasurement), &Returned, NULL) ||
        Returned != sizeof(FirstMeasurement) ||
        FirstMeasurement.Version != PMI_VERSION ||
        FirstMeasurement.CurrentPower == 0 ||
        FirstMeasurement.CurrentPower == 0xFFFFFFFFUL ||
        FirstMeasurement.CurrentPower > 100000)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL first-measurement error=%lu bytes=%lu value=%lu\n",
                      GetLastError(), Returned, FirstMeasurement.CurrentPower);
        Success = FALSE;
        goto Done;
    }
    Sleep(100);
    ZeroMemory(&SecondMeasurement, sizeof(SecondMeasurement));
    if (!DeviceIoControl(Device, IOCTL_PMI_GET_MEASUREMENT,
                         NULL, 0, &SecondMeasurement,
                         sizeof(SecondMeasurement), &Returned, NULL) ||
        Returned != sizeof(SecondMeasurement) ||
        SecondMeasurement.Version != PMI_VERSION ||
        SecondMeasurement.CurrentPower == 0 ||
        SecondMeasurement.CurrentPower == 0xFFFFFFFFUL ||
        SecondMeasurement.CurrentPower > 100000)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL second-measurement error=%lu bytes=%lu value=%lu\n",
                      GetLastError(), Returned, SecondMeasurement.CurrentPower);
        Success = FALSE;
        goto Done;
    }
    Rpi5DiagPrint("RPI5_PMI_MEASUREMENT first=%lu_mW second=%lu_mW\n",
                  FirstMeasurement.CurrentPower,
                  SecondMeasurement.CurrentPower);

    SetLastError(ERROR_SUCCESS);
    if (DeviceIoControl(Device, IOCTL_PMI_SET_CONFIGURATION,
                        &Configuration, sizeof(Configuration),
                        NULL, 0, &Returned, NULL) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL read-only-set error=%lu\n",
                      GetLastError());
        Success = FALSE;
        goto Done;
    }
    CloseHandle(Device);
    Device = Rpi5OpenFirstInterface(&GUID_DEVICE_POWER_METER,
                                    FILE_FLAG_OVERLAPPED, &Found);
    if (Device == INVALID_HANDLE_VALUE || Found != 1)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL notify-open interfaces=%lu error=%lu\n",
                      Found, GetLastError());
        if (Device != INVALID_HANDLE_VALUE)
            CloseHandle(Device);
        return FALSE;
    }

    EventHandle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!EventHandle)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL notify-event error=%lu\n", GetLastError());
        Success = FALSE;
        goto Done;
    }
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = EventHandle;
    ZeroMemory(&Event, sizeof(Event));
    Event.Version = PMI_VERSION;
    Event.EventType = PmiCapabilitiesChangedEvent;
    SetLastError(ERROR_SUCCESS);
    if (DeviceIoControl(Device, IOCTL_PMI_REGISTER_EVENT_NOTIFY,
                        &Event, sizeof(Event), &Event, sizeof(Event),
                        NULL, &Overlapped))
    {
        Rpi5DiagPrint("RPI5_PMI_NOTIFY completed event=%u\n", Event.EventType);
    }
    else if (GetLastError() != ERROR_IO_PENDING)
    {
        Rpi5DiagPrint("RPI5_PMI_FAIL notify-queue error=%lu\n", GetLastError());
        Success = FALSE;
    }
    else
    {
        if (!CancelIo(Device))
        {
            Rpi5DiagPrint("RPI5_PMI_FAIL notify-cancel error=%lu\n", GetLastError());
            Success = FALSE;
        }
        else
        {
            WaitForSingleObject(EventHandle, 2000);
            SetLastError(ERROR_SUCCESS);
            if (GetOverlappedResult(Device, &Overlapped, &Returned, FALSE) ||
                GetLastError() != ERROR_OPERATION_ABORTED)
            {
                Rpi5DiagPrint("RPI5_PMI_FAIL notify-cancel-result error=%lu bytes=%lu\n",
                              GetLastError(), Returned);
                Success = FALSE;
            }
            else
            {
                Rpi5DiagPrint("RPI5_PMI_NOTIFY pending-and-cancelled\n");
            }
        }
    }
    CloseHandle(EventHandle);

Done:
    CloseHandle(Device);
    Rpi5DiagPrint(Success ? "RPI5_PMI_PASS\n" : "RPI5_PMI_FAIL\n");
    return Success;
}

static BOOL
Rpi5GetSensorString(
    _In_ ISensor *Sensor,
    _In_ const PROPERTYKEY *Key,
    _Out_writes_(Count) PWCHAR Buffer,
    _In_ SIZE_T Count)
{
    PROPVARIANT Value;
    HRESULT Result;

    PropVariantInit(&Value);
    Result = ISensor_GetProperty(Sensor, Key, &Value);
    if (FAILED(Result) || Value.vt != VT_LPWSTR || !Value.pwszVal)
    {
        PropVariantClear(&Value);
        return FALSE;
    }
    lstrcpynW(Buffer, Value.pwszVal, (INT)Count);
    PropVariantClear(&Value);
    return TRUE;
}

static BOOL
Rpi5ReadSensor(
    _In_ ISensor *Sensor,
    _In_ const PROPERTYKEY *Key,
    _Out_ double *Reading)
{
    ISensorDataReport *Report = NULL;
    PROPVARIANT Value;
    HRESULT Result;

    Result = ISensor_GetData(Sensor, &Report);
    if (FAILED(Result) || !Report)
        return FALSE;
    PropVariantInit(&Value);
    Result = ISensorDataReport_GetSensorValue(Report, Key, &Value);
    if (SUCCEEDED(Result) && Value.vt == VT_R8)
        *Reading = Value.dblVal;
    else
        Result = E_FAIL;
    PropVariantClear(&Value);
    ISensorDataReport_Release(Report);
    return SUCCEEDED(Result);
}

static DWORD WINAPI
Rpi5LoadThread(
    _In_opt_ PVOID Context)
{
    volatile ULONG Value = (ULONG)(ULONG_PTR)Context + 1;

    while (!InterlockedCompareExchange(&Rpi5LoadStop, 0, 0))
        Value = Value * 1664525u + 1013904223u;
    return Value;
}

static ULONG
Rpi5StartLoadThreads(
    _Out_writes_(RPI5_DIAG_MAX_PROCESSORS) HANDLE *Threads)
{
    SYSTEM_INFO SystemInfo;
    ULONG Count;
    ULONG Index;

    GetSystemInfo(&SystemInfo);
    Count = SystemInfo.dwNumberOfProcessors;
    if (Count > RPI5_DIAG_MAX_PROCESSORS)
        Count = RPI5_DIAG_MAX_PROCESSORS;
    if (Count > 1)
        Count--;
    InterlockedExchange(&Rpi5LoadStop, FALSE);
    for (Index = 0; Index < Count; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, Rpi5LoadThread,
                                      (PVOID)(ULONG_PTR)Index, 0, NULL);
        if (!Threads[Index])
            break;
        SetThreadAffinityMask(Threads[Index], ((DWORD_PTR)1) << Index);
        SetThreadPriority(Threads[Index], THREAD_PRIORITY_BELOW_NORMAL);
    }
    return Index;
}

static VOID
Rpi5StopLoadThreads(
    _In_reads_(Count) HANDLE *Threads,
    _In_ ULONG Count)
{
    ULONG Index;

    InterlockedExchange(&Rpi5LoadStop, TRUE);
    if (Count)
        WaitForMultipleObjects(Count, Threads, TRUE, 5000);
    for (Index = 0; Index < Count; Index++)
        CloseHandle(Threads[Index]);
}

static BOOL
Rpi5SensorValuePlausible(
    _In_ const GUID *Type,
    _In_ double Value)
{
    if (IsEqualGUID(Type, &SENSOR_TYPE_VOLTAGE))
        return Value >= 0.0 && Value <= 20.0;
    if (IsEqualGUID(Type, &SENSOR_TYPE_CURRENT))
        return Value >= 0.0 && Value <= 20.0;
    if (IsEqualGUID(Type, &SENSOR_TYPE_ELECTRICAL_POWER))
        return Value >= 0.0 && Value <= 100.0;
    return FALSE;
}

static BOOL
Rpi5TestSensors(VOID)
{
    RPI5_SENSOR_SAMPLE Samples[RPI5_DIAG_MAX_SENSORS];
    HANDLE LoadThreads[RPI5_DIAG_MAX_PROCESSORS];
    ISensorCollection *Collection = NULL;
    ISensorManager *Manager = NULL;
    ULONG VoltageCount = 0;
    ULONG CurrentCount = 0;
    ULONG PowerCount = 0;
    ULONG ChangedCount = 0;
    ULONG FirstReadCount = 0;
    ULONG SampleCount = 0;
    ULONG CollectionCount = 0;
    ULONG LoadCount = 0;
    ULONG Index;
    ULONG Other;
    HRESULT Result;
    BOOL Success = TRUE;

    ZeroMemory(Samples, sizeof(Samples));
    ZeroMemory(LoadThreads, sizeof(LoadThreads));
    Result = CoCreateInstance(&CLSID_SensorManager, NULL,
                              CLSCTX_INPROC_SERVER, &IID_ISensorManager,
                              (void **)&Manager);
    if (FAILED(Result))
    {
        Rpi5DiagPrint("RPI5_SENSOR_FAIL manager hr=0x%08lx\n", Result);
        return FALSE;
    }
    Result = ISensorManager_GetSensorsByCategory(Manager,
                                                 &SENSOR_CATEGORY_ALL,
                                                 &Collection);
    if (FAILED(Result) || !Collection ||
        FAILED(ISensorCollection_GetCount(Collection, &CollectionCount)))
    {
        Rpi5DiagPrint("RPI5_SENSOR_FAIL enumerate hr=0x%08lx\n", Result);
        Success = FALSE;
        goto Done;
    }

    for (Index = 0; Index < CollectionCount; Index++)
    {
        ISensor *Sensor = NULL;
        WCHAR Manufacturer[RPI5_DIAG_NAME_LENGTH];
        WCHAR Model[RPI5_DIAG_NAME_LENGTH];
        BSTR FriendlyName = NULL;
        const PROPERTYKEY *DataKey;
        GUID Type;
        GUID Id;

        Result = ISensorCollection_GetAt(Collection, Index, &Sensor);
        if (FAILED(Result) || !Sensor)
        {
            Success = FALSE;
            continue;
        }
        if (!Rpi5GetSensorString(Sensor, &SENSOR_PROPERTY_MANUFACTURER,
                                 Manufacturer, ARRAYSIZE(Manufacturer)) ||
            !Rpi5GetSensorString(Sensor, &SENSOR_PROPERTY_MODEL,
                                 Model, ARRAYSIZE(Model)) ||
            wcscmp(Manufacturer, L"Raspberry Pi") != 0 ||
            wcscmp(Model, L"Raspberry Pi 5 firmware telemetry") != 0)
        {
            ISensor_Release(Sensor);
            continue;
        }
        if (SampleCount >= RPI5_DIAG_MAX_SENSORS ||
            FAILED(ISensor_GetID(Sensor, &Id)) ||
            FAILED(ISensor_GetType(Sensor, &Type)) ||
            FAILED(ISensor_GetFriendlyName(Sensor, &FriendlyName)) ||
            !FriendlyName || !FriendlyName[0])
        {
            SysFreeString(FriendlyName);
            ISensor_Release(Sensor);
            Success = FALSE;
            continue;
        }
        if (IsEqualGUID(&Type, &SENSOR_TYPE_VOLTAGE))
        {
            DataKey = &SENSOR_DATA_TYPE_VOLTAGE_VOLTS;
            VoltageCount++;
        }
        else if (IsEqualGUID(&Type, &SENSOR_TYPE_CURRENT))
        {
            DataKey = &SENSOR_DATA_TYPE_CURRENT_AMPS;
            CurrentCount++;
        }
        else if (IsEqualGUID(&Type, &SENSOR_TYPE_ELECTRICAL_POWER))
        {
            DataKey = &SENSOR_DATA_TYPE_ELECTRICAL_POWER_WATTS;
            PowerCount++;
        }
        else
        {
            CHAR Name[256];

            Rpi5WideToUtf8(FriendlyName, Name, sizeof(Name));
            Rpi5DiagPrint("RPI5_SENSOR_FAIL unexpected-type name=%s\n", Name);
            SysFreeString(FriendlyName);
            ISensor_Release(Sensor);
            Success = FALSE;
            continue;
        }
        for (Other = 0; Other < SampleCount; Other++)
        {
            if (IsEqualGUID(&Id, &Samples[Other].Id) ||
                !_wcsicmp(FriendlyName, Samples[Other].Name))
            {
                CHAR Name[256];

                Rpi5WideToUtf8(FriendlyName, Name, sizeof(Name));
                Rpi5DiagPrint("RPI5_SENSOR_FAIL duplicate name-or-id=%s\n", Name);
                Success = FALSE;
            }
        }
        Samples[SampleCount].Sensor = Sensor;
        Samples[SampleCount].Id = Id;
        Samples[SampleCount].Type = Type;
        Samples[SampleCount].DataKey = DataKey;
        lstrcpynW(Samples[SampleCount].Name, FriendlyName,
                  ARRAYSIZE(Samples[SampleCount].Name));
        if (wcsstr(FriendlyName, L"temperature") ||
            wcsstr(FriendlyName, L"Temperature") ||
            wcsstr(FriendlyName, L"legacy") ||
            wcsstr(FriendlyName, L"Legacy"))
        {
            CHAR Name[256];

            Rpi5WideToUtf8(FriendlyName, Name, sizeof(Name));
            Rpi5DiagPrint("RPI5_SENSOR_FAIL duplicate-alias=%s\n", Name);
            Success = FALSE;
        }
        if (!Rpi5ReadSensor(Sensor, DataKey,
                            &Samples[SampleCount].FirstValue) ||
            !Rpi5SensorValuePlausible(&Type,
                                      Samples[SampleCount].FirstValue))
        {
            CHAR Name[256];

            Rpi5WideToUtf8(FriendlyName, Name, sizeof(Name));
            Rpi5DiagPrint("RPI5_SENSOR_FAIL first-read name=%s\n", Name);
            Success = FALSE;
        }
        else
        {
            FirstReadCount++;
        }
        SampleCount++;
        SysFreeString(FriendlyName);
    }

    if (FirstReadCount != SampleCount)
    {
        Rpi5DiagPrint("RPI5_SENSOR_FAIL live-read transport=%lu/%lu\n",
                      FirstReadCount, SampleCount);
        goto Done;
    }

    LoadCount = Rpi5StartLoadThreads(LoadThreads);
    Sleep(1000);
    for (Index = 0; Index < SampleCount; Index++)
    {
        CHAR Name[256];
        PCSTR Kind;
        double Difference;

        if (!Rpi5ReadSensor(Samples[Index].Sensor, Samples[Index].DataKey,
                            &Samples[Index].SecondValue) ||
            !Rpi5SensorValuePlausible(&Samples[Index].Type,
                                      Samples[Index].SecondValue))
        {
            Rpi5WideToUtf8(Samples[Index].Name, Name, sizeof(Name));
            Rpi5DiagPrint("RPI5_SENSOR_FAIL second-read name=%s\n", Name);
            Success = FALSE;
            continue;
        }
        Difference = Samples[Index].SecondValue - Samples[Index].FirstValue;
        if (Difference < 0.0)
            Difference = -Difference;
        if (Difference > 0.0000001)
            ChangedCount++;
        Kind = IsEqualGUID(&Samples[Index].Type, &SENSOR_TYPE_VOLTAGE) ?
               "voltage_V" :
               (IsEqualGUID(&Samples[Index].Type, &SENSOR_TYPE_CURRENT) ?
                "current_A" : "power_W");
        Rpi5WideToUtf8(Samples[Index].Name, Name, sizeof(Name));
        Rpi5DiagPrint("RPI5_SENSOR_VALUE type=%s name=\"%s\" first=%.6f second=%.6f\n",
                      Kind, Name, Samples[Index].FirstValue,
                      Samples[Index].SecondValue);
    }
    Rpi5StopLoadThreads(LoadThreads, LoadCount);
    LoadCount = 0;

    if (SampleCount != 38 || VoltageCount != 14 || CurrentCount != 12 ||
        PowerCount != 12 || ChangedCount == 0)
    {
        Rpi5DiagPrint("RPI5_SENSOR_FAIL count=%lu voltage=%lu current=%lu power=%lu changed=%lu\n",
                      SampleCount, VoltageCount, CurrentCount, PowerCount,
                      ChangedCount);
        Success = FALSE;
    }
    else
    {
        Rpi5DiagPrint("RPI5_SENSOR_SUMMARY count=38 voltage=14 current=12 power=12 changed=%lu\n",
                      ChangedCount);
    }

Done:
    if (LoadCount)
        Rpi5StopLoadThreads(LoadThreads, LoadCount);
    for (Index = 0; Index < SampleCount; Index++)
        ISensor_Release(Samples[Index].Sensor);
    if (Collection)
        ISensorCollection_Release(Collection);
    ISensorManager_Release(Manager);
    Rpi5DiagPrint(Success ? "RPI5_SENSOR_PASS\n" : "RPI5_SENSOR_FAIL\n");
    return Success;
}

static DWORD WINAPI
Rpi5ProcessorThread(
    _Inout_ PVOID Parameter)
{
    PRPI5_CPU_CONTEXT Context = Parameter;

    Context->ObservedProcessor = Context->GetCurrentProcessorNumber();
    return 0;
}

static BOOL
Rpi5TestProcessors(VOID)
{
    PRPI5_GET_CURRENT_PROCESSOR_NUMBER GetProcessorNumber;
    PRPI5_CPU_CONTEXT Context;
    SYSTEM_INFO SystemInfo;
    HANDLE Thread;
    DWORD_PTR PreviousAffinity;
    DWORD ResumeResult;
    DWORD WaitResult;
    DWORD Error;
    DWORD CleanupError;
    ULONG Index;
    BOOL ThreadExited;
    BOOL Success = TRUE;

    GetProcessorNumber = (PRPI5_GET_CURRENT_PROCESSOR_NUMBER)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "GetCurrentProcessorNumber");
    GetSystemInfo(&SystemInfo);
    if (!GetProcessorNumber || SystemInfo.dwNumberOfProcessors != 4)
    {
        Rpi5DiagPrint("RPI5_CPU_FAIL processors=%lu api=%p\n",
                      SystemInfo.dwNumberOfProcessors, GetProcessorNumber);
        return FALSE;
    }

    for (Index = 0; Index < SystemInfo.dwNumberOfProcessors; Index++)
    {
        Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            sizeof(*Context));
        if (!Context)
        {
            Rpi5DiagPrint("RPI5_CPU_FAIL target=%lu allocation error=%lu\n",
                          Index, ERROR_NOT_ENOUGH_MEMORY);
            Success = FALSE;
            break;
        }
        Context->GetCurrentProcessorNumber = GetProcessorNumber;
        Context->ObservedProcessor = 0xFFFFFFFFUL;
        Thread = CreateThread(NULL, 0, Rpi5ProcessorThread, Context,
                              CREATE_SUSPENDED, NULL);
        if (!Thread)
        {
            Rpi5DiagPrint("RPI5_CPU_FAIL target=%lu create error=%lu\n",
                          Index, GetLastError());
            HeapFree(GetProcessHeap(), 0, Context);
            Success = FALSE;
            break;
        }

        Error = ERROR_SUCCESS;
        ResumeResult = (DWORD)-1;
        WaitResult = WAIT_FAILED;
        PreviousAffinity = SetThreadAffinityMask(Thread,
                                                  ((DWORD_PTR)1) << Index);
        if (!PreviousAffinity)
        {
            Error = GetLastError();
        }
        else
        {
            ResumeResult = ResumeThread(Thread);
            if (ResumeResult == (DWORD)-1)
            {
                Error = GetLastError();
            }
            else
            {
                WaitResult = WaitForSingleObject(Thread, 5000);
                if (WaitResult == WAIT_FAILED)
                    Error = GetLastError();
                else if (WaitResult != WAIT_OBJECT_0)
                    Error = ERROR_TIMEOUT;
            }
        }

        if (!PreviousAffinity || ResumeResult == (DWORD)-1 ||
            WaitResult != WAIT_OBJECT_0 ||
            Context->ObservedProcessor != Index)
        {
            Rpi5DiagPrint("RPI5_CPU_FAIL target=%lu observed=%lu error=%lu\n",
                          Index, Context->ObservedProcessor, Error);
            Success = FALSE;
        }
        else
        {
            Rpi5DiagPrint("RPI5_CPU_SCHEDULABLE cpu=%lu\n", Index);
        }

        ThreadExited = WaitResult == WAIT_OBJECT_0;
        if (!ThreadExited && WaitForSingleObject(Thread, 0) == WAIT_OBJECT_0)
            ThreadExited = TRUE;
        if (!ThreadExited)
        {
            CleanupError = ERROR_SUCCESS;
            if (TerminateThread(Thread, ERROR_TIMEOUT))
            {
                WaitResult = WaitForSingleObject(Thread, INFINITE);
                ThreadExited = WaitResult == WAIT_OBJECT_0;
                if (!ThreadExited)
                {
                    CleanupError = WaitResult == WAIT_FAILED ?
                        GetLastError() : ERROR_GEN_FAILURE;
                }
            }
            else
            {
                CleanupError = GetLastError();
            }
            if (!ThreadExited)
            {
                Rpi5DiagPrint("RPI5_CPU_FAIL target=%lu cleanup error=%lu\n",
                              Index, CleanupError);
            }
        }
        CloseHandle(Thread);
        if (!ThreadExited)
        {
            /* Keep the worker context valid if termination itself failed. */
            Success = FALSE;
            break;
        }
        HeapFree(GetProcessHeap(), 0, Context);
    }
    Rpi5DiagPrint(Success ? "RPI5_CPU_PASS processors=4\n" :
                              "RPI5_CPU_FAIL\n");
    return Success;
}

static BOOL
Rpi5TestPnp(VOID)
{
    RPI5_EXPECTED_DEVICE Expected[] =
    {
        {L"ACPI\\ACPI0003\\", 1, 0, FALSE},
        {L"ACPI\\ACPI0004\\", 2, 0, FALSE},
        {L"ACPI\\ACPI0007\\", 4, 0, TRUE},
        {L"ACPI\\ACPI000C\\", 1, 0, TRUE},
        {L"ACPI\\ACPI000D\\", 1, 0, TRUE},
        {L"ACPI\\AMZN0001\\", 1, 0, FALSE},
        {L"ACPI\\BCM2712\\", 1, 0, TRUE},
        {L"ACPI\\RPI0005\\", 1, 0, TRUE},
        {L"ACPI\\BRCM5D12\\", 2, 0, TRUE},
        {L"ACPI\\PNP0C0B\\", 4, 0, FALSE},
        {L"ACPI\\PNP0D10\\", 2, 0, TRUE},
        {L"ACPI\\ThermalZone\\", 2, 0, FALSE},
        {L"SD\\VID_02D0&PID_4345&FN_1\\", 1, 0, TRUE},
        {L"SD\\VID_02D0&PID_4345&FN_2\\", 1, 0, TRUE},
        {L"SD\\VID_02D0&PID_4345&FN_3\\", 1, 0, TRUE},
        {L"SD\\VID_03&OID_5344&PID_", 1, 0, TRUE},
        {L"USB\\ROOT_HUB\\", 2, 0, TRUE},
        {L"ROOT\\RPI5VC4\\", 0, 0, FALSE},
        {L"DETECTEDInternal\\rpi5vc4\\", 0, 0, FALSE},
    };
    SP_DEVINFO_DATA DeviceData;
    HDEVINFO DeviceInfo;
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    ULONG Status;
    ULONG Problem;
    ULONG Index;
    ULONG ExpectedIndex;
    CONFIGRET ConfigResult;
    BOOL Success = TRUE;

    DeviceInfo = SetupDiGetClassDevsW(NULL, NULL, NULL,
                                      DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (DeviceInfo == INVALID_HANDLE_VALUE)
    {
        Rpi5DiagPrint("RPI5_PNP_FAIL enumerate error=%lu\n", GetLastError());
        return FALSE;
    }

    for (Index = 0;; Index++)
    {
        ZeroMemory(&DeviceData, sizeof(DeviceData));
        DeviceData.cbSize = sizeof(DeviceData);
        if (!SetupDiEnumDeviceInfo(DeviceInfo, Index, &DeviceData))
        {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                Success = FALSE;
            break;
        }
        if (!SetupDiGetDeviceInstanceIdW(DeviceInfo, &DeviceData,
                                         InstanceId, ARRAYSIZE(InstanceId),
                                         NULL))
            continue;
        Status = 0;
        Problem = 0;
        ConfigResult = CM_Get_DevNode_Status(&Status, &Problem,
                                             DeviceData.DevInst, 0);
        if (ConfigResult != CR_SUCCESS)
        {
            Rpi5DiagPrint("RPI5_PNP_FAIL status-query cr=0x%lx\n",
                          ConfigResult);
            Success = FALSE;
            continue;
        }

        if (Problem == CM_PROB_FAILED_INSTALL)
        {
            CHAR Id[512];

            Rpi5WideToUtf8(InstanceId, Id, sizeof(Id));
            Rpi5DiagPrint("RPI5_PNP_FAIL code28 id=%s\n", Id);
            Success = FALSE;
        }
        if (wcsstr(InstanceId, L"VEN_6930&DEV_7062"))
        {
            Rpi5DiagPrint("RPI5_PNP_FAIL phantom-pci-present\n");
            Success = FALSE;
        }

        for (ExpectedIndex = 0; ExpectedIndex < ARRAYSIZE(Expected);
             ExpectedIndex++)
        {
            if (!Rpi5StartsWithInsensitive(InstanceId,
                                           Expected[ExpectedIndex].Prefix))
                continue;
            Expected[ExpectedIndex].FoundCount++;
            if ((Status & DN_HAS_PROBLEM) || Problem ||
                (Expected[ExpectedIndex].MustBeStarted &&
                 !(Status & DN_STARTED)))
            {
                CHAR Id[512];

                Rpi5WideToUtf8(InstanceId, Id, sizeof(Id));
                Rpi5DiagPrint("RPI5_PNP_FAIL id=%s status=0x%08lx problem=%lu\n",
                              Id, Status, Problem);
                Success = FALSE;
            }
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(DeviceInfo);

    for (ExpectedIndex = 0; ExpectedIndex < ARRAYSIZE(Expected);
         ExpectedIndex++)
    {
        CHAR Prefix[256];

        Rpi5WideToUtf8(Expected[ExpectedIndex].Prefix, Prefix,
                       sizeof(Prefix));
        Rpi5DiagPrint("RPI5_PNP_DEVICE prefix=%s found=%lu expected=%lu\n",
                      Prefix, Expected[ExpectedIndex].FoundCount,
                      Expected[ExpectedIndex].ExpectedCount);
        if (Expected[ExpectedIndex].FoundCount !=
            Expected[ExpectedIndex].ExpectedCount)
            Success = FALSE;
    }
    Rpi5DiagPrint(Success ? "RPI5_PNP_PASS\n" : "RPI5_PNP_FAIL\n");
    return Success;
}

static BOOL
Rpi5TestV3d(VOID)
{
    static const WCHAR DisplayName[] = L"\\\\.\\DISPLAY1";
    RPI5VC4_V3D_INFO Info;
    HANDLE Display;
    DWORD Returned;
    BOOL Success;

    Rpi5DiagPrint("RPI5_V3D_BEGIN display=DISPLAY1\n");
    Display = CreateFileW(DisplayName,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
    if (Display == INVALID_HANDLE_VALUE)
    {
        Rpi5DiagPrint("RPI5_V3D_FAIL open-error=%lu\n", GetLastError());
        return FALSE;
    }

    ZeroMemory(&Info, sizeof(Info));
    Returned = 0;
    Success = DeviceIoControl(Display,
                              IOCTL_VIDEO_RPI5VC4_QUERY_V3D,
                              NULL,
                              0,
                              &Info,
                              sizeof(Info),
                              &Returned,
                              NULL);
    CloseHandle(Display);
    if (!Success)
    {
        Rpi5DiagPrint("RPI5_V3D_FAIL query-error=%lu\n", GetLastError());
        return FALSE;
    }

    Rpi5DiagPrint("RPI5_V3D_INFO abi=%lu flags=0x%08lx version=%lu cores=%lu sms=%08lx/%08lx hub=%08lx/%08lx/%08lx/%08lx core=%08lx/%08lx/%08lx mmu=%08lx bytes=%lu\n",
                  Info.AbiVersion,
                  Info.Flags,
                  Info.Version,
                  Info.CoreCount,
                  Info.SmsReeCs,
                  Info.SmsTeeCs,
                  Info.HubIdent[0],
                  Info.HubIdent[1],
                  Info.HubIdent[2],
                  Info.HubIdent[3],
                  Info.CoreIdent[0],
                  Info.CoreIdent[1],
                  Info.CoreIdent[2],
                  Info.MmuDebugInfo,
                  Returned);

    if (Returned != sizeof(Info) ||
        Info.Size != sizeof(Info) ||
        Info.AbiVersion != RPI5VC4_XPDM_ABI_VERSION ||
        (Info.Flags & RPI5VC4_V3D_FLAG_IDENT_VALID) == 0 ||
        Info.Version != 71 ||
        Info.CoreCount == 0)
    {
        Rpi5DiagPrint("RPI5_V3D_FAIL invalid-response\n");
        return FALSE;
    }

    Rpi5DiagPrint("RPI5_V3D_PASS\n");
    return TRUE;
}

static BOOL
Rpi5TestV3dRender(VOID)
{
    RPI5VC4_V3D_SELFTEST Result;
    HDC Display;
    ULONG RequestedEscape;
    INT Returned;

    Rpi5DiagPrint("RPI5_V3D_RENDER_BEGIN display=DISPLAY1\n");
    Display = GetDC(NULL);
    if (Display == NULL)
    {
        Rpi5DiagPrint("RPI5_V3D_RENDER_FAIL dc-error=%lu\n",
                      GetLastError());
        return FALSE;
    }

    RequestedEscape = RPI5VC4_ESCAPE_RUN_V3D_SELFTEST;
    Returned = ExtEscape(Display,
                         QUERYESCSUPPORT,
                         sizeof(RequestedEscape),
                         (LPCSTR)&RequestedEscape,
                         0,
                         NULL);
    if (Returned <= 0)
    {
        ReleaseDC(NULL, Display);
        Rpi5DiagPrint("RPI5_V3D_RENDER_FAIL escape-unsupported result=%d\n",
                      Returned);
        return FALSE;
    }

    ZeroMemory(&Result, sizeof(Result));
    Returned = ExtEscape(Display,
                         RPI5VC4_ESCAPE_RUN_V3D_SELFTEST,
                         0,
                         NULL,
                         sizeof(Result),
                         (LPSTR)&Result);
    ReleaseDC(NULL, Display);
    if (Returned <= 0)
    {
        Rpi5DiagPrint("RPI5_V3D_RENDER_FAIL escape-result=%d\n", Returned);
        return FALSE;
    }

    Rpi5DiagPrint("RPI5_V3D_RENDER_INFO abi=%lu status=%lu flags=0x%08lx expected=%08lx pixels=%08lx/%08lx/%08lx mismatches=%lu bfc=%lu/%lu ct0=%08lx/%08lx binpolls=%lu rfc=%lu/%lu irq=%08lx ct1=%08lx/%08lx mmu=%08lx/%08lx err=%08lx polls=%lu lists=%lu/%lu/%lu bytes=%lu\n",
                  Result.AbiVersion,
                  Result.Status,
                  Result.Flags,
                  Result.ExpectedPixel,
                  Result.FirstPixel,
                  Result.CenterPixel,
                  Result.LastPixel,
                  Result.MismatchCount,
                  Result.BfcBefore,
                  Result.BfcAfter,
                  Result.Ct0Current,
                  Result.Ct0End,
                  Result.BinningPollCount,
                  Result.RfcBefore,
                  Result.RfcAfter,
                  Result.CoreInterruptStatus,
                  Result.Ct1Current,
                  Result.Ct1End,
                  Result.MmuControl,
                  Result.MmuViolationAddress,
                  Result.ErrorStatus,
                  Result.PollCount,
                  Result.BinningControlListBytes,
                  Result.RenderControlListBytes,
                  Result.GenericTileListBytes,
                  (ULONG)Returned);

    if ((ULONG)Returned != sizeof(Result) ||
        Result.Size != sizeof(Result) ||
        Result.AbiVersion != RPI5VC4_XPDM_ABI_VERSION ||
        Result.Status != RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS ||
        (Result.Flags & RPI5VC4_V3D_SELFTEST_FLAG_PASSED) == 0 ||
        Result.MismatchCount != 0 ||
        Result.FirstPixel != Result.ExpectedPixel ||
        Result.CenterPixel != Result.ExpectedPixel ||
        Result.LastPixel != Result.ExpectedPixel)
    {
        Rpi5DiagPrint("RPI5_V3D_RENDER_FAIL invalid-response\n");
        return FALSE;
    }

    Rpi5DiagPrint("RPI5_V3D_RENDER_PASS\n");
    return TRUE;
}

static BOOL
Rpi5TestV3dClear(VOID)
{
    const ULONG Width = 128;
    const ULONG Height = 128;
    const ULONG ClearColor = 0xFF0000FF;
    RPI5VC4_V3D_CLEAR_REQUEST Request;
    PRPI5VC4_V3D_CLEAR_RESULT Result;
    ULONG HeaderSize = FIELD_OFFSET(RPI5VC4_V3D_CLEAR_RESULT, Pixels);
    ULONG PixelBytes = Width * Height * sizeof(ULONG);
    ULONG ResultSize = HeaderSize + PixelBytes;
    ULONG RequestedEscape;
    ULONG Index;
    ULONG Mismatches = 0;
    HDC Display;
    INT Returned;
    BOOL Success;

    Rpi5DiagPrint("RPI5_V3D_CLEAR_BEGIN width=%lu height=%lu color=%08lx\n",
                  Width, Height, ClearColor);
    Display = GetDC(NULL);
    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ResultSize);
    if (Display == NULL || Result == NULL)
    {
        if (Result)
            HeapFree(GetProcessHeap(), 0, Result);
        if (Display)
            ReleaseDC(NULL, Display);
        Rpi5DiagPrint("RPI5_V3D_CLEAR_FAIL allocation-or-dc error=%lu\n",
                      GetLastError());
        return FALSE;
    }

    RequestedEscape = RPI5VC4_ESCAPE_RENDER_CLEAR;
    Returned = ExtEscape(Display,
                         QUERYESCSUPPORT,
                         sizeof(RequestedEscape),
                         (LPCSTR)&RequestedEscape,
                         0,
                         NULL);
    if (Returned <= 0)
    {
        Rpi5DiagPrint("RPI5_V3D_CLEAR_FAIL escape-unsupported result=%d\n",
                      Returned);
        Success = FALSE;
        goto Cleanup;
    }

    ZeroMemory(&Request, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Request.Width = Width;
    Request.Height = Height;
    Request.ClearColor = ClearColor;
    Returned = ExtEscape(Display,
                         RPI5VC4_ESCAPE_RENDER_CLEAR,
                         sizeof(Request),
                         (LPCSTR)&Request,
                         ResultSize,
                         (LPSTR)Result);
    if (Returned > 0)
    {
        for (Index = 0; Index < Width * Height; Index++)
        {
            if (Result->Pixels[Index] != ClearColor)
                Mismatches++;
        }
    }

    Rpi5DiagPrint("RPI5_V3D_CLEAR_INFO returned=%d size=%lu abi=%lu status=%lu flags=0x%08lx stride=%lu pixels=%lu mismatches=%lu bfc=%lu/%lu rfc=%lu/%lu ct0=%08lx/%08lx ct1=%08lx/%08lx mmu=%08lx/%08lx err=%08lx\n",
                  Returned,
                  Result->Size,
                  Result->AbiVersion,
                  Result->Status,
                  Result->Flags,
                  Result->Stride,
                  Result->PixelBytes,
                  Mismatches,
                  Result->Diagnostics.BfcBefore,
                  Result->Diagnostics.BfcAfter,
                  Result->Diagnostics.RfcBefore,
                  Result->Diagnostics.RfcAfter,
                  Result->Diagnostics.Ct0Current,
                  Result->Diagnostics.Ct0End,
                  Result->Diagnostics.Ct1Current,
                  Result->Diagnostics.Ct1End,
                  Result->Diagnostics.MmuControl,
                  Result->Diagnostics.MmuViolationAddress,
                  Result->Diagnostics.ErrorStatus);
    Success = Returned == (INT)ResultSize &&
              Result->Size == ResultSize &&
              Result->AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
              Result->Status == RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS &&
              (Result->Flags & RPI5VC4_V3D_SELFTEST_FLAG_PASSED) != 0 &&
              Result->Width == Width &&
              Result->Height == Height &&
              Result->Stride == Width * sizeof(ULONG) &&
              Result->ClearColor == ClearColor &&
              Result->PixelBytes == PixelBytes &&
              Mismatches == 0;
    Rpi5DiagPrint(Success ? "RPI5_V3D_CLEAR_PASS\n" :
                              "RPI5_V3D_CLEAR_FAIL invalid-response\n");

Cleanup:
    HeapFree(GetProcessHeap(), 0, Result);
    ReleaseDC(NULL, Display);
    return Success;
}

static BOOL
Rpi5TestV3dTriangle(VOID)
{
    const ULONG Width = 128;
    const ULONG Height = 128;
    const ULONG ClearColor = 0xFF000000;
    RPI5VC4_V3D_TRIANGLE_REQUEST Request;
    PRPI5VC4_V3D_TRIANGLE_RESULT Result;
    ULONG HeaderSize = FIELD_OFFSET(RPI5VC4_V3D_TRIANGLE_RESULT, Pixels);
    ULONG PixelBytes = Width * Height * sizeof(ULONG);
    ULONG ResultSize = HeaderSize + PixelBytes;
    ULONG RequestedEscape;
    ULONG Index;
    ULONG CoveredPixels = 0;
    ULONG CenterPixel;
    HDC Display;
    INT Returned;
    BOOL Success;

    Rpi5DiagPrint("RPI5_V3D_TRIANGLE_BEGIN width=%lu height=%lu clear=%08lx\n",
                  Width, Height, ClearColor);
    Display = GetDC(NULL);
    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ResultSize);
    if (Display == NULL || Result == NULL)
    {
        if (Result)
            HeapFree(GetProcessHeap(), 0, Result);
        if (Display)
            ReleaseDC(NULL, Display);
        Rpi5DiagPrint("RPI5_V3D_TRIANGLE_FAIL allocation-or-dc error=%lu\n",
                      GetLastError());
        return FALSE;
    }

    RequestedEscape = RPI5VC4_ESCAPE_RENDER_TRIANGLE;
    Returned = ExtEscape(Display,
                         QUERYESCSUPPORT,
                         sizeof(RequestedEscape),
                         (LPCSTR)&RequestedEscape,
                         0,
                         NULL);
    if (Returned <= 0)
    {
        Rpi5DiagPrint("RPI5_V3D_TRIANGLE_FAIL escape-unsupported result=%d\n",
                      Returned);
        Success = FALSE;
        goto Cleanup;
    }

    ZeroMemory(&Request, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Request.Width = Width;
    Request.Height = Height;
    Request.ClearColor = ClearColor;
    Request.PrimitiveType = RPI5VC4_V3D_PRIMITIVE_TRIANGLES;
    Request.VertexCount = 3;

    Request.Vertices[0].Position[0] = 0xBF400000; /* -0.75f */
    Request.Vertices[0].Position[1] = 0xBF400000;
    Request.Vertices[0].Position[3] = 0x3F800000; /* 1.0f */
    Request.Vertices[0].Color[0] = 0x3F800000;
    Request.Vertices[0].Color[3] = 0x3F800000;

    Request.Vertices[1].Position[0] = 0x3F400000; /* 0.75f */
    Request.Vertices[1].Position[1] = 0xBF400000;
    Request.Vertices[1].Position[3] = 0x3F800000;
    Request.Vertices[1].Color[1] = 0x3F800000;
    Request.Vertices[1].Color[3] = 0x3F800000;

    Request.Vertices[2].Position[1] = 0x3F400000;
    Request.Vertices[2].Position[3] = 0x3F800000;
    Request.Vertices[2].Color[2] = 0x3F800000;
    Request.Vertices[2].Color[3] = 0x3F800000;

    Returned = ExtEscape(Display,
                         RPI5VC4_ESCAPE_RENDER_TRIANGLE,
                         sizeof(Request),
                         (LPCSTR)&Request,
                         ResultSize,
                         (LPSTR)Result);
    if (Returned > 0)
    {
        for (Index = 0; Index < Width * Height; Index++)
        {
            if (Result->Pixels[Index] != ClearColor)
                CoveredPixels++;
        }
    }
    CenterPixel = Returned > 0 ?
        Result->Pixels[(Height / 2) * Width + (Width / 2)] : 0;

    Rpi5DiagPrint("RPI5_V3D_TRIANGLE_INFO returned=%d size=%lu abi=%lu status=%lu flags=0x%08lx primitive=%lu vertices=%lu stride=%lu pixels=%lu covered=%lu/%lu center=%08lx corners=%08lx/%08lx bfc=%lu/%lu rfc=%lu/%lu ct0=%08lx/%08lx ct1=%08lx/%08lx mmu=%08lx/%08lx err=%08lx\n",
                  Returned,
                  Result->Size,
                  Result->AbiVersion,
                  Result->Status,
                  Result->Flags,
                  Result->PrimitiveType,
                  Result->VertexCount,
                  Result->Stride,
                  Result->PixelBytes,
                  Result->CoveredPixelCount,
                  CoveredPixels,
                  CenterPixel,
                  Returned > 0 ? Result->Pixels[0] : 0,
                  Returned > 0 ? Result->Pixels[Width * Height - 1] : 0,
                  Result->Diagnostics.BfcBefore,
                  Result->Diagnostics.BfcAfter,
                  Result->Diagnostics.RfcBefore,
                  Result->Diagnostics.RfcAfter,
                  Result->Diagnostics.Ct0Current,
                  Result->Diagnostics.Ct0End,
                  Result->Diagnostics.Ct1Current,
                  Result->Diagnostics.Ct1End,
                  Result->Diagnostics.MmuControl,
                  Result->Diagnostics.MmuViolationAddress,
                  Result->Diagnostics.ErrorStatus);
    Success = Returned == (INT)ResultSize &&
              Result->Size == ResultSize &&
              Result->AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
              Result->Status == RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS &&
              (Result->Flags & RPI5VC4_V3D_SELFTEST_FLAG_PASSED) != 0 &&
              Result->Width == Width &&
              Result->Height == Height &&
              Result->Stride == Width * sizeof(ULONG) &&
              Result->ClearColor == ClearColor &&
              Result->PixelBytes == PixelBytes &&
              Result->CoveredPixelCount == CoveredPixels &&
              Result->PrimitiveType == RPI5VC4_V3D_PRIMITIVE_TRIANGLES &&
              Result->VertexCount == 3 &&
              CoveredPixels != 0 &&
              CenterPixel != ClearColor &&
              Result->Pixels[0] == ClearColor &&
              Result->Pixels[Width * Height - 1] == ClearColor;
    Rpi5DiagPrint(Success ? "RPI5_V3D_TRIANGLE_PASS\n" :
                              "RPI5_V3D_TRIANGLE_FAIL invalid-response\n");

Cleanup:
    HeapFree(GetProcessHeap(), 0, Result);
    ReleaseDC(NULL, Display);
    return Success;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) WCHAR *argv[])
{
    HRESULT Result;
    ULONG Failures = 0;
    int Argument;
    BOOL FullOpenGlRegression = FALSE;

    for (Argument = 1; Argument < argc; Argument++)
    {
        if (!_wcsicmp(argv[Argument], L"--full-gl") ||
            !_wcsicmp(argv[Argument], L"/full-gl"))
        {
            FullOpenGlRegression = TRUE;
        }
    }

    Rpi5DiagPrint("RPI5_HWTEST_BEGIN gl-mode=%s\n",
                  FullOpenGlRegression ? "full" : "focused");
    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(Result))
    {
        Rpi5DiagPrint("RPI5_HWTEST_FAIL com=0x%08lx\n", Result);
        return 1;
    }
    if (!Rpi5TestPmi())
        Failures++;
    if (!Rpi5TestSensors())
        Failures++;
    if (!Rpi5TestProcessors())
        Failures++;
    if (!Rpi5TestPnp())
        Failures++;
    if (!Rpi5TestV3d())
        Failures++;
    if (!Rpi5TestV3dRender())
        Failures++;
    if (!Rpi5TestV3dClear())
        Failures++;
    if (!Rpi5TestV3dTriangle())
        Failures++;
    if (!Rpi5TestOpenGl(FullOpenGlRegression))
        Failures++;
    Rpi5ProbeDirect3D9();
    CoUninitialize();

    if (Failures)
    {
        Rpi5DiagPrint("RPI5_HWTEST_FAIL failures=%lu\n", Failures);
        return 1;
    }
    Rpi5DiagPrint("RPI5_HWTEST_PASS\n");
    return 0;
}
