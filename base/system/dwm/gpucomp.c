/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GPU-accelerated compositor effects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <wine/wglext.h>

#include <reactos/dwmframe.h>
#include <reactos/dwmgpuinterop.h>

#include "gpucomp.h"
#include "gpud3d.h"
#include "presenttrace.h"
#include "gpumaterial.h"
#include "gpushadow.h"
#include "gpudamage.h"
#include "gpugeometry.h"
#include "gpublurdamage.h"

DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR Param, DWORD Routine);

/*
 * The software compositor is the baseline and stays authoritative: it runs on
 * every display stack, including the basic-display fallback that publishes no
 * ICD at all.  This file adds the hardware path for the convolution work
 * (blur, and the glass and shadow effects built on it), which is where the
 * software cost concentrates: a separable blur is O(radius) memory traffic
 * per pixel on the CPU and a single filtered texture fetch per pixel on a
 * GPU.
 *
 * Everything here is conditional.  A display adapter that publishes no ICD,
 * or an ICD that resolves to a software rasteriser, or a missing entry point,
 * or a shader that fails to compile all end the same way: DwmGpuIsActive()
 * stays FALSE, and the compositor keeps calling the software routines.
 */

#define DWM_GPU_CLASS_NAME  L"DwmGpuCompositor"

/* Radius past which the separable kernel stops being worth a texture unit. */
#define DWM_GPU_MAX_RADIUS  64

/* GL 2.0 shader entry points. */
static PFNGLCREATESHADERPROC            pglCreateShader;
static PFNGLSHADERSOURCEPROC            pglShaderSource;
static PFNGLCOMPILESHADERPROC           pglCompileShader;
static PFNGLGETSHADERIVPROC             pglGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC        pglGetShaderInfoLog;
static PFNGLDELETESHADERPROC            pglDeleteShader;
static PFNGLCREATEPROGRAMPROC           pglCreateProgram;
static PFNGLATTACHSHADERPROC            pglAttachShader;
static PFNGLLINKPROGRAMPROC             pglLinkProgram;
static PFNGLGETPROGRAMIVPROC            pglGetProgramiv;
static PFNGLUSEPROGRAMPROC              pglUseProgram;
static PFNGLDELETEPROGRAMPROC           pglDeleteProgram;
static PFNGLGETUNIFORMLOCATIONPROC      pglGetUniformLocation;
static PFNGLUNIFORM1IPROC               pglUniform1i;
static PFNGLUNIFORM1FPROC               pglUniform1f;
static PFNGLUNIFORM2FPROC               pglUniform2f;
static PFNGLUNIFORM3FPROC               pglUniform3f;
static PFNGLUNIFORM1FVPROC              pglUniform1fv;
static PFNGLACTIVETEXTUREPROC           pglActiveTexture;

/* Framebuffer-object entry points, for the intermediate blur target. */
static PFNGLGENFRAMEBUFFERSPROC         pglGenFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC         pglBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC    pglFramebufferTexture2D;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC  pglCheckFramebufferStatus;
static PFNGLDELETEFRAMEBUFFERSPROC      pglDeleteFramebuffers;

static BOOL  g_gpuActive;
static BOOL  g_gpuProbed;
static HWND  g_gpuWindow;
static HDC   g_gpuDc;
static HGLRC g_gpuContext;
static char  g_gpuRenderer[128];

static GLuint g_blurProgram;
static GLint  g_blurSourceLoc;
static GLint  g_blurStepLoc;
static GLint  g_blurTapsLoc;
static GLint  g_blurOffsetsLoc;
static GLint  g_blurWeightsLoc;

static GLuint g_srcTexture;      /* uploaded composition region */
static GLuint g_passTexture;     /* horizontal-pass result */
static GLuint g_framebuffer;
static LONG   g_texWidth;
static LONG   g_texHeight;

static ULONG *g_readback;
static SIZE_T g_readbackBytes;

/*
 * A separable Gaussian: the horizontal pass writes into an offscreen target
 * and the vertical pass reads it back, so the cost is 2*(2r+1) fetches per
 * pixel instead of the (2r+1)^2 a single two-dimensional pass would need.
 * Step selects the axis, so one program serves both passes.
 */
static const char *const DwmGpuBlurVertexSource =
    "varying vec2 vTexCoord;\n"
    "void main()\n"
    "{\n"
    "    vTexCoord = gl_MultiTexCoord0.xy;\n"
    "    gl_Position = gl_Vertex;\n"
    "}\n";

/* Keep every uniform subscript constant. A loop with a uniform-dependent
 * break can survive shader lowering as indirect UBO reads on VC4, which
 * cannot be validated as bounded reads by the kernel command validator. */
#define DWM_GPU_BLUR_TAP(Index) \
    "    if (DWM_BLUR_TAPS > " #Index ") {\n" \
    "        vec2 offset = uStep * uOffsets[" #Index "];\n" \
    "        sum += texture2D(uSource, vTexCoord + offset) * uWeights[" #Index "];\n" \
    "        sum += texture2D(uSource, vTexCoord - offset) * uWeights[" #Index "];\n" \
    "    }\n"

static const char *const DwmGpuBlurFragmentSource =
    "uniform sampler2D uSource;\n"
    "uniform vec2 uStep;\n"
    "#ifndef DWM_BLUR_TAPS\n"
    "uniform int uTaps;\n"
    "#define DWM_BLUR_TAPS uTaps\n"
    "#endif\n"
    "uniform float uWeights[33];\n"
    "uniform float uOffsets[33];\n"
    "varying vec2 vTexCoord;\n"
    "void main()\n"
    "{\n"
    "    vec4 sum = texture2D(uSource, vTexCoord) * uWeights[0];\n"
    DWM_GPU_BLUR_TAP(1)  DWM_GPU_BLUR_TAP(2)
    DWM_GPU_BLUR_TAP(3)  DWM_GPU_BLUR_TAP(4)
    DWM_GPU_BLUR_TAP(5)  DWM_GPU_BLUR_TAP(6)
    DWM_GPU_BLUR_TAP(7)  DWM_GPU_BLUR_TAP(8)
    DWM_GPU_BLUR_TAP(9)  DWM_GPU_BLUR_TAP(10)
    DWM_GPU_BLUR_TAP(11) DWM_GPU_BLUR_TAP(12)
    DWM_GPU_BLUR_TAP(13) DWM_GPU_BLUR_TAP(14)
    DWM_GPU_BLUR_TAP(15) DWM_GPU_BLUR_TAP(16)
    DWM_GPU_BLUR_TAP(17) DWM_GPU_BLUR_TAP(18)
    DWM_GPU_BLUR_TAP(19) DWM_GPU_BLUR_TAP(20)
    DWM_GPU_BLUR_TAP(21) DWM_GPU_BLUR_TAP(22)
    DWM_GPU_BLUR_TAP(23) DWM_GPU_BLUR_TAP(24)
    DWM_GPU_BLUR_TAP(25) DWM_GPU_BLUR_TAP(26)
    DWM_GPU_BLUR_TAP(27) DWM_GPU_BLUR_TAP(28)
    DWM_GPU_BLUR_TAP(29) DWM_GPU_BLUR_TAP(30)
    DWM_GPU_BLUR_TAP(31) DWM_GPU_BLUR_TAP(32)
    "    gl_FragColor = sum;\n"
    "}\n";

#undef DWM_GPU_BLUR_TAP

static void *
DwmGpuGetProc(const char *Name)
{
    PROC Address = wglGetProcAddress(Name);

    /*
     * wglGetProcAddress reports absence as NULL, but some implementations
     * historically returned small sentinel values for a stub entry.  Treat
     * those as absent too rather than calling into them.
     */
    if (Address == NULL || (ULONG_PTR)Address <= 3 ||
        Address == (PROC)(ULONG_PTR)-1)
    {
        return NULL;
    }
    return (void *)Address;
}

static BOOL
DwmGpuHasExtension(const char *Extensions, const char *Name)
{
    SIZE_T Length = strlen(Name);
    const char *Match = Extensions;

    while (Match != NULL && (Match = strstr(Match, Name)) != NULL)
    {
        if ((Match == Extensions || Match[-1] == ' ') &&
            (Match[Length] == ' ' || Match[Length] == '\0'))
            return TRUE;
        Match += Length;
    }
    return FALSE;
}

static BOOL
DwmGpuLoadEntryPoints(void)
{
    pglCreateShader = (PFNGLCREATESHADERPROC)DwmGpuGetProc("glCreateShader");
    pglShaderSource = (PFNGLSHADERSOURCEPROC)DwmGpuGetProc("glShaderSource");
    pglCompileShader = (PFNGLCOMPILESHADERPROC)DwmGpuGetProc("glCompileShader");
    pglGetShaderiv = (PFNGLGETSHADERIVPROC)DwmGpuGetProc("glGetShaderiv");
    pglGetShaderInfoLog =
        (PFNGLGETSHADERINFOLOGPROC)DwmGpuGetProc("glGetShaderInfoLog");
    pglDeleteShader = (PFNGLDELETESHADERPROC)DwmGpuGetProc("glDeleteShader");
    pglCreateProgram = (PFNGLCREATEPROGRAMPROC)DwmGpuGetProc("glCreateProgram");
    pglAttachShader = (PFNGLATTACHSHADERPROC)DwmGpuGetProc("glAttachShader");
    pglLinkProgram = (PFNGLLINKPROGRAMPROC)DwmGpuGetProc("glLinkProgram");
    pglGetProgramiv = (PFNGLGETPROGRAMIVPROC)DwmGpuGetProc("glGetProgramiv");
    pglUseProgram = (PFNGLUSEPROGRAMPROC)DwmGpuGetProc("glUseProgram");
    pglDeleteProgram = (PFNGLDELETEPROGRAMPROC)DwmGpuGetProc("glDeleteProgram");
    pglGetUniformLocation =
        (PFNGLGETUNIFORMLOCATIONPROC)DwmGpuGetProc("glGetUniformLocation");
    pglUniform1i = (PFNGLUNIFORM1IPROC)DwmGpuGetProc("glUniform1i");
    pglUniform1f = (PFNGLUNIFORM1FPROC)DwmGpuGetProc("glUniform1f");
    pglUniform2f = (PFNGLUNIFORM2FPROC)DwmGpuGetProc("glUniform2f");
    pglUniform3f = (PFNGLUNIFORM3FPROC)DwmGpuGetProc("glUniform3f");
    pglUniform1fv = (PFNGLUNIFORM1FVPROC)DwmGpuGetProc("glUniform1fv");
    pglActiveTexture =
        (PFNGLACTIVETEXTUREPROC)DwmGpuGetProc("glActiveTexture");

    pglGenFramebuffers =
        (PFNGLGENFRAMEBUFFERSPROC)DwmGpuGetProc("glGenFramebuffers");
    pglBindFramebuffer =
        (PFNGLBINDFRAMEBUFFERPROC)DwmGpuGetProc("glBindFramebuffer");
    pglFramebufferTexture2D =
        (PFNGLFRAMEBUFFERTEXTURE2DPROC)DwmGpuGetProc("glFramebufferTexture2D");
    pglCheckFramebufferStatus =
        (PFNGLCHECKFRAMEBUFFERSTATUSPROC)
            DwmGpuGetProc("glCheckFramebufferStatus");
    pglDeleteFramebuffers =
        (PFNGLDELETEFRAMEBUFFERSPROC)DwmGpuGetProc("glDeleteFramebuffers");

    /* EXT_framebuffer_object spells the same calls with an EXT suffix. */
    if (pglGenFramebuffers == NULL)
    {
        pglGenFramebuffers =
            (PFNGLGENFRAMEBUFFERSPROC)DwmGpuGetProc("glGenFramebuffersEXT");
        pglBindFramebuffer =
            (PFNGLBINDFRAMEBUFFERPROC)DwmGpuGetProc("glBindFramebufferEXT");
        pglFramebufferTexture2D =
            (PFNGLFRAMEBUFFERTEXTURE2DPROC)
                DwmGpuGetProc("glFramebufferTexture2DEXT");
        pglCheckFramebufferStatus =
            (PFNGLCHECKFRAMEBUFFERSTATUSPROC)
                DwmGpuGetProc("glCheckFramebufferStatusEXT");
        pglDeleteFramebuffers =
            (PFNGLDELETEFRAMEBUFFERSPROC)
                DwmGpuGetProc("glDeleteFramebuffersEXT");
    }

    return pglCreateShader != NULL && pglShaderSource != NULL &&
           pglCompileShader != NULL && pglGetShaderiv != NULL &&
           pglDeleteShader != NULL && pglCreateProgram != NULL &&
           pglAttachShader != NULL && pglLinkProgram != NULL &&
           pglGetProgramiv != NULL && pglUseProgram != NULL &&
           pglDeleteProgram != NULL && pglGetUniformLocation != NULL &&
           pglUniform1i != NULL && pglUniform1f != NULL &&
           pglUniform2f != NULL && pglUniform3f != NULL && pglUniform1fv != NULL &&
           pglActiveTexture != NULL &&
           pglGenFramebuffers != NULL && pglBindFramebuffer != NULL &&
           pglFramebufferTexture2D != NULL &&
           pglCheckFramebufferStatus != NULL && pglDeleteFramebuffers != NULL;
}

/*
 * A GL_VERSION below 2.0 cannot run the blur program at all, and the string
 * always begins with the version number, so parsing the leading major digit
 * is sufficient and avoids depending on an extension string.
 */
static BOOL
DwmGpuVersionAtLeast2(const char *Version)
{
    if (Version == NULL || Version[0] < '0' || Version[0] > '9')
        return FALSE;
    if (Version[1] != '.')
        return Version[0] > '2' || (Version[0] == '2');
    return Version[0] >= '2';
}

/*
 * opengl32 falls back to its own rasteriser when the adapter publishes no
 * usable ICD.  That path is slower than the compositor's own software blur
 * and would be a regression, so it is rejected here and the caller keeps the
 * software path.
 */
static BOOL
DwmGpuRendererIsHardware(const char *Renderer)
{
    static const char *const SoftwareMarkers[] =
    {
        "Software", "software", "SWImplementation", "llvmpipe", "softpipe",
        "swrast", "GDI Generic"
    };
    SIZE_T i;

    if (Renderer == NULL || Renderer[0] == '\0')
        return FALSE;

    for (i = 0; i < RTL_NUMBER_OF(SoftwareMarkers); ++i)
    {
        if (strstr(Renderer, SoftwareMarkers[i]) != NULL)
            return FALSE;
    }
    return TRUE;
}

static GLuint
DwmGpuCompilePrefixed(GLenum Stage, const char *Source, const char *Prefix)
{
    GLuint Shader = pglCreateShader(Stage);
    GLint Status = 0;
    const char *Sources[] = {Prefix, Source};

    if (Shader == 0)
        return 0;
    pglShaderSource(Shader, Prefix ? 2 : 1, Prefix ? Sources : &Sources[1], NULL);
    pglCompileShader(Shader);
    pglGetShaderiv(Shader, GL_COMPILE_STATUS, &Status);
    if (Status == GL_FALSE)
    {
        pglDeleteShader(Shader);
        return 0;
    }
    return Shader;
}

static GLuint
DwmGpuCompile(GLenum Stage, const char *Source)
{
    return DwmGpuCompilePrefixed(Stage, Source, NULL);
}

static BOOL
DwmGpuBuildBlurProgram(void)
{
    GLuint Vertex;
    GLuint Fragment;
    GLint Status = 0;

    Vertex = DwmGpuCompile(GL_VERTEX_SHADER, DwmGpuBlurVertexSource);
    if (Vertex == 0)
        return FALSE;
    Fragment = DwmGpuCompile(GL_FRAGMENT_SHADER, DwmGpuBlurFragmentSource);
    if (Fragment == 0)
    {
        pglDeleteShader(Vertex);
        return FALSE;
    }

    g_blurProgram = pglCreateProgram();
    if (g_blurProgram == 0)
    {
        pglDeleteShader(Fragment);
        pglDeleteShader(Vertex);
        return FALSE;
    }
    pglAttachShader(g_blurProgram, Vertex);
    pglAttachShader(g_blurProgram, Fragment);
    pglLinkProgram(g_blurProgram);
    pglGetProgramiv(g_blurProgram, GL_LINK_STATUS, &Status);
    /* The program keeps its own reference to each stage once linked. */
    pglDeleteShader(Fragment);
    pglDeleteShader(Vertex);
    if (Status == GL_FALSE)
    {
        pglDeleteProgram(g_blurProgram);
        g_blurProgram = 0;
        return FALSE;
    }

    g_blurSourceLoc = pglGetUniformLocation(g_blurProgram, "uSource");
    g_blurStepLoc = pglGetUniformLocation(g_blurProgram, "uStep");
    g_blurTapsLoc = pglGetUniformLocation(g_blurProgram, "uTaps");
    g_blurOffsetsLoc = pglGetUniformLocation(g_blurProgram, "uOffsets");
    g_blurWeightsLoc = pglGetUniformLocation(g_blurProgram, "uWeights");
    return g_blurStepLoc >= 0 && g_blurTapsLoc >= 0 &&
           g_blurWeightsLoc >= 0 && g_blurOffsetsLoc >= 0;
}

static void
DwmGpuDestroyTargets(void)
{
    if (g_framebuffer != 0)
    {
        pglDeleteFramebuffers(1, &g_framebuffer);
        g_framebuffer = 0;
    }
    if (g_passTexture != 0)
    {
        glDeleteTextures(1, &g_passTexture);
        g_passTexture = 0;
    }
    if (g_srcTexture != 0)
    {
        glDeleteTextures(1, &g_srcTexture);
        g_srcTexture = 0;
    }
    g_texWidth = 0;
    g_texHeight = 0;
}

static BOOL
DwmGpuMakeTexture(GLuint *Texture, LONG Width, LONG Height)
{
    glGenTextures(1, Texture);
    if (*Texture == 0)
        return FALSE;
    glBindTexture(GL_TEXTURE_2D, *Texture);
    /*
     * Clamping to the edge keeps the kernel from wrapping the opposite side
     * of the region into the result along the borders.
     */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0,
                 GL_BGRA_EXT, GL_UNSIGNED_BYTE, NULL);
    return glGetError() == GL_NO_ERROR;
}

static BOOL
DwmGpuEnsureTargets(LONG Width, LONG Height)
{
    if (g_texWidth == Width && g_texHeight == Height && g_framebuffer != 0)
        return TRUE;

    DwmGpuDestroyTargets();
    if (!DwmGpuMakeTexture(&g_srcTexture, Width, Height) ||
        !DwmGpuMakeTexture(&g_passTexture, Width, Height))
    {
        DwmGpuDestroyTargets();
        return FALSE;
    }

    pglGenFramebuffers(1, &g_framebuffer);
    if (g_framebuffer == 0)
    {
        DwmGpuDestroyTargets();
        return FALSE;
    }

    g_texWidth = Width;
    g_texHeight = Height;
    return TRUE;
}

static BOOL
DwmGpuEnsureReadback(SIZE_T Bytes)
{
    if (g_readback != NULL && g_readbackBytes >= Bytes)
        return TRUE;
    if (g_readback != NULL)
        VirtualFree(g_readback, 0, MEM_RELEASE);
    g_readback = (ULONG *)VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_READWRITE);
    if (g_readback == NULL)
    {
        g_readbackBytes = 0;
        return FALSE;
    }
    g_readbackBytes = Bytes;
    return TRUE;
}

/*
 * Builds a linear-sampled Gaussian.  Taps are paired: one bilinear fetch
 * placed between two adjacent texels returns their weighted sum, so a kernel
 * of radius r needs about r/2 fetches per side instead of r.  Offsets[0] is
 * the centre tap and is sampled exactly.  Returns the tap count.
 */
static ULONG
DwmGpuBuildWeights(ULONG Radius, GLfloat *Weights, GLfloat *Offsets)
{
    double Discrete[DWM_GPU_MAX_RADIUS + 1];
    double Sigma = (double)Radius / 2.0;
    double TwoSigmaSq;
    double Total;
    ULONG Taps;
    ULONG i;

    if (Sigma < 1.0)
        Sigma = 1.0;
    TwoSigmaSq = 2.0 * Sigma * Sigma;

    Discrete[0] = 1.0;
    Total = 1.0;
    for (i = 1; i <= Radius; ++i)
    {
        Discrete[i] = exp(-((double)i * (double)i) / TwoSigmaSq);
        Total += 2.0 * Discrete[i];
    }

    Weights[0] = (GLfloat)(Discrete[0] / Total);
    Offsets[0] = 0.0f;
    Taps = 1;
    for (i = 1; i + 1 <= Radius; i += 2)
    {
        double wA = Discrete[i];
        double wB = Discrete[i + 1];
        double Pair = wA + wB;

        /* Sample where the two texels contribute in their own proportion. */
        Weights[Taps] = (GLfloat)(Pair / Total);
        Offsets[Taps] =
            (GLfloat)(((double)i * wA + (double)(i + 1) * wB) / Pair);
        ++Taps;
    }
    /* An odd radius leaves one unpaired outer tap. */
    if (i <= Radius)
    {
        Weights[Taps] = (GLfloat)(Discrete[i] / Total);
        Offsets[Taps] = (GLfloat)i;
        ++Taps;
    }
    return Taps;
}

/* Draws the full target as one quad with texture coordinates 0..1. */
static void
DwmGpuDrawQuad(void)
{
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glEnd();
}

BOOL
DwmGpuInitialize(HDC hdcScreen)
{
    WNDCLASSEXW Class;
    PIXELFORMATDESCRIPTOR Descriptor;
    const char *Renderer;
    const char *Version;
    int Format;

    if (g_gpuProbed)
        return g_gpuActive;
    g_gpuProbed = TRUE;

    UNREFERENCED_PARAMETER(hdcScreen);

    RtlZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = DWM_GPU_CLASS_NAME;
    if (RegisterClassExW(&Class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return FALSE;
    }

    /*
     * The context needs a window with a pixel format, but nothing is ever
     * presented through it: the blur renders to a framebuffer object and is
     * read back into the composition buffer, so the window stays hidden and
     * one pixel wide.
     */
    g_gpuWindow = CreateWindowExW(0, DWM_GPU_CLASS_NAME, L"", WS_POPUP,
                                  0, 0, 1, 1, NULL, NULL,
                                  GetModuleHandleW(NULL), NULL);
    if (g_gpuWindow == NULL)
        return FALSE;

    g_gpuDc = GetDC(g_gpuWindow);
    if (g_gpuDc == NULL)
    {
        DwmGpuShutdown();
        return FALSE;
    }

    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.nSize = sizeof(Descriptor);
    Descriptor.nVersion = 1;
    Descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    Descriptor.iPixelType = PFD_TYPE_RGBA;
    Descriptor.cColorBits = 32;
    Descriptor.iLayerType = PFD_MAIN_PLANE;

    Format = ChoosePixelFormat(g_gpuDc, &Descriptor);
    if (Format == 0 || !SetPixelFormat(g_gpuDc, Format, &Descriptor))
    {
        DwmGpuShutdown();
        return FALSE;
    }

    g_gpuContext = wglCreateContext(g_gpuDc);
    if (g_gpuContext == NULL || !wglMakeCurrent(g_gpuDc, g_gpuContext))
    {
        DwmGpuShutdown();
        return FALSE;
    }

    Version = (const char *)glGetString(GL_VERSION);
    Renderer = (const char *)glGetString(GL_RENDERER);
    if (!DwmGpuVersionAtLeast2(Version) ||
        !DwmGpuRendererIsHardware(Renderer) ||
        !DwmGpuLoadEntryPoints() ||
        !DwmGpuBuildBlurProgram())
    {
        DwmGpuShutdown();
        return FALSE;
    }

    lstrcpynA(g_gpuRenderer, Renderer, sizeof(g_gpuRenderer));
    g_gpuActive = TRUE;
    wglMakeCurrent(NULL, NULL);
    return TRUE;
}

BOOL
DwmGpuIsActive(void)
{
    return g_gpuActive;
}

const char *
DwmGpuRendererName(void)
{
    if (DwmD3dIsActive())
        return DwmD3dRendererName();
    return g_gpuActive ? g_gpuRenderer : NULL;
}

BOOL
DwmGpuBlurRect(ULONG *Composition, LONG Width, LONG Height,
               const RECT *Rect, ULONG Radius)
{
    GLfloat Weights[DWM_GPU_MAX_RADIUS + 1];
    GLfloat Offsets[DWM_GPU_MAX_RADIUS + 1];
    ULONG Taps;
    LONG RegionWidth;
    LONG RegionHeight;
    LONG Row;
    SIZE_T Bytes;
    BOOL Result = FALSE;

    if (!g_gpuActive || Composition == NULL || Rect == NULL || Radius == 0)
        return FALSE;
    if (Radius > DWM_GPU_MAX_RADIUS)
        return FALSE;

    RegionWidth = Rect->right - Rect->left;
    RegionHeight = Rect->bottom - Rect->top;
    if (RegionWidth <= 0 || RegionHeight <= 0 ||
        Rect->left < 0 || Rect->top < 0 ||
        Rect->right > Width || Rect->bottom > Height)
    {
        return FALSE;
    }

    Bytes = (SIZE_T)RegionWidth * (SIZE_T)RegionHeight * sizeof(ULONG);
    if (!DwmGpuEnsureReadback(Bytes))
        return FALSE;

    if (!wglMakeCurrent(g_gpuDc, g_gpuContext))
        return FALSE;

    if (!DwmGpuEnsureTargets(RegionWidth, RegionHeight))
        goto Done;

    /* Gather the region into the staging buffer, one scanline at a time. */
    for (Row = 0; Row < RegionHeight; ++Row)
    {
        RtlCopyMemory(g_readback + (SIZE_T)Row * RegionWidth,
                      Composition + (SIZE_T)(Rect->top + Row) * Width +
                          Rect->left,
                      (SIZE_T)RegionWidth * sizeof(ULONG));
    }

    glBindTexture(GL_TEXTURE_2D, g_srcTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, RegionWidth, RegionHeight,
                    GL_BGRA_EXT, GL_UNSIGNED_BYTE, g_readback);
    if (glGetError() != GL_NO_ERROR)
        goto Done;

    Taps = DwmGpuBuildWeights(Radius, Weights, Offsets);

    glViewport(0, 0, RegionWidth, RegionHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    pglUseProgram(g_blurProgram);
    if (g_blurSourceLoc >= 0)
        pglUniform1i(g_blurSourceLoc, 0);
    pglUniform1i(g_blurTapsLoc, (GLint)Taps);
    pglUniform1fv(g_blurWeightsLoc, (GLsizei)Taps, Weights);
    pglUniform1fv(g_blurOffsetsLoc, (GLsizei)Taps, Offsets);
    pglActiveTexture(GL_TEXTURE0);

    /* Horizontal pass: source texture into the intermediate target. */
    pglBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, g_passTexture, 0);
    if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        goto Unbind;
    glBindTexture(GL_TEXTURE_2D, g_srcTexture);
    pglUniform2f(g_blurStepLoc, 1.0f / (GLfloat)RegionWidth, 0.0f);
    DwmGpuDrawQuad();

    /* Vertical pass: intermediate back into the source texture. */
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, g_srcTexture, 0);
    if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        goto Unbind;
    glBindTexture(GL_TEXTURE_2D, g_passTexture);
    pglUniform2f(g_blurStepLoc, 0.0f, 1.0f / (GLfloat)RegionHeight);
    DwmGpuDrawQuad();

    glReadPixels(0, 0, RegionWidth, RegionHeight, GL_BGRA_EXT,
                 GL_UNSIGNED_BYTE, g_readback);
    if (glGetError() != GL_NO_ERROR)
        goto Unbind;

    for (Row = 0; Row < RegionHeight; ++Row)
    {
        RtlCopyMemory(Composition + (SIZE_T)(Rect->top + Row) * Width +
                          Rect->left,
                      g_readback + (SIZE_T)Row * RegionWidth,
                      (SIZE_T)RegionWidth * sizeof(ULONG));
    }
    Result = TRUE;

Unbind:
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    pglUseProgram(0);

Done:
    wglMakeCurrent(NULL, NULL);
    return Result;
}

void
DwmGpuShutdown(void)
{
    if (g_gpuContext != NULL)
    {
        wglMakeCurrent(g_gpuDc, g_gpuContext);
        DwmGpuDestroyTargets();
        if (g_blurProgram != 0)
        {
            pglDeleteProgram(g_blurProgram);
            g_blurProgram = 0;
        }
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_gpuContext);
        g_gpuContext = NULL;
    }
    if (g_gpuDc != NULL)
    {
        ReleaseDC(g_gpuWindow, g_gpuDc);
        g_gpuDc = NULL;
    }
    if (g_gpuWindow != NULL)
    {
        DestroyWindow(g_gpuWindow);
        g_gpuWindow = NULL;
    }
    if (g_readback != NULL)
    {
        VirtualFree(g_readback, 0, MEM_RELEASE);
        g_readback = NULL;
        g_readbackBytes = 0;
    }
    g_gpuActive = FALSE;
}

/* ------------------------------------------------------------------ */
/* Full-frame composition                                             */
/* ------------------------------------------------------------------ */

#define DWM_GPU_CLASS_COMPOSE  L"DwmGpuCompose"
#define DWM_GPU_TEXTURE_SLOTS  64

#include "gpuscenecache.h"

static DWM_GPU_SCENE_CACHE g_composeScene;
static BOOL g_composeLowerUnchanged[DWM_MAX_WINDOWS];
static BOOL g_composeCachedCapture[DWM_MAX_WINDOWS];
static DWM_WIN g_composeBlurOwner;
static BOOL g_composeBlurOwnerValid, g_composeBlurLowerUnchanged;
static ULONG g_composeBlurCall;
static ULONG g_composeBlurRadius;
static ULONGLONG g_composeFiltered, g_composeReused;
static void DwmGpuComposeAdvanceBlurCache(void);
static void DwmGpuComposePinBlurResults(const RECT *Repair);
static void DwmGpuComposeUnpinBlurResults(void);

void
DwmGpuComposePrepareWindow(const DWM_WIN *Window, ULONG Index)
{
    if (DwmD3dIsActive())
    {
        DwmD3dPrepareWindow(Window, Index);
        return;
    }
    g_composeBlurOwner = DwmGpuCacheBlurOwner(Window);
    g_composeBlurOwnerValid = Window->AnimFlags == 0;
    g_composeBlurLowerUnchanged = g_composeLowerUnchanged[Index];
    g_composeBlurCall = 0;
    DwmGpuComposeAdvanceBlurCache();
}

void
DwmGpuComposeBlurStats(ULONGLONG *Filtered, ULONGLONG *Reused)
{
    if (DwmD3dIsActive())
    {
        DwmD3dBlurStats(Filtered, Reused);
        return;
    }
    *Filtered = g_composeFiltered;
    *Reused = g_composeReused;
    g_composeFiltered = g_composeReused = 0;
}

/*
 * One resident texture per window backing store.  Keyed by the surface slot
 * and its generation, so a window that is recreated gets a fresh upload while
 * an unchanged one costs nothing at all.
 */
typedef struct _DWM_GPU_TEXTURE
{
    ULONG  SurfaceId;
    ULONG  Generation;
    ULONG  LastFrame;
    LONG   Width;
    LONG   Height;
    GLuint Texture;
    ULONG GlobalShare;
    ULONG ShareGeneration;
    ULONG Pitch;
    BOOL Client;
    ULONGLONG UpdateId;
    BOOL PublicationValid;
} DWM_GPU_TEXTURE;

static BOOL  g_composeActive;
static HWND  g_composeWindow;
static HDC   g_composeDc;
static HGLRC g_composeContext;
static PFNGLBLITFRAMEBUFFERPROC g_composeBlitFramebuffer;
static BOOL  g_composeOutputRegistered;
static LONG  g_composeWidth;
static LONG  g_composeHeight;
static ULONG g_composeFrame;
static DWM_GPU_DAMAGE g_composeDamage;
static ULONG g_composeRetryShare;
static BOOL g_composeRetryable;
static RECT g_composeOcclusion[DWM_MAX_WINDOWS + 1];
static void (APIENTRY *g_addSwapHint)(GLint, GLint, GLsizei, GLsizei);
static DWM_GPU_TEXTURE g_composeTextures[DWM_GPU_TEXTURE_SLOTS];
static PFNWGLBINDSHAREDTEXTUREROS g_bindSharedTexture;
static PFNWGLUPDATESHAREDTEXTUREROS g_updateSharedTexture;

static void DwmGpuComposePruneBlurTargets(const DWM_WIN *Windows, ULONG Count);

static void
DwmGpuComposePruneTextures(void)
{
    ULONG Index;

    /* Damage can omit an unchanged live window. Retire from the complete
     * scene snapshot, not from the windows drawn in the previous frame. */
    for (Index = 0; Index < DWM_GPU_TEXTURE_SLOTS; ++Index)
    {
        DWM_GPU_TEXTURE *Slot = &g_composeTextures[Index];

        if (Slot->Texture != 0 &&
            !DwmGpuSceneHasSurface(g_composeScene.Windows, g_composeScene.Count,
                                  Slot->SurfaceId, Slot->Client))
        {
            glDeleteTextures(1, &Slot->Texture);
            RtlZeroMemory(Slot, sizeof(*Slot));
        }
    }
    DwmGpuComposePruneBlurTargets(g_composeScene.Windows, g_composeScene.Count);
}

void
DwmGpuComposeScene(const DWM_WIN *Windows, ULONG Count,
                    const RECTL *BlurRects, ULONG BlurRectCount,
                    LONG OriginX, LONG OriginY, BOOL RefreshBackdrop,
                    ULONG BlurRadius, const RECT *ShadowMargins)
{
    if (DwmD3dIsActive())
    {
        DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_PREPARE);
        DwmD3dScene(Windows, Count, BlurRects, BlurRectCount, OriginX, OriginY,
                    RefreshBackdrop, BlurRadius, ShadowMargins);
        DptEnd(&g_DwmPresentTrace, Trace, TRUE, 0);
        return;
    }
    DWM_GPU_SCENE_SPACE Space = {OriginX, OriginY, g_composeWidth, g_composeHeight,
                                 BlurRadius, *ShadowMargins};
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_PREPARE);
    g_composeBlurRadius = BlurRadius;
    DwmGpuCacheScene(&g_composeScene, Windows, Count, BlurRects, BlurRectCount,
                     &Space, RefreshBackdrop, g_composeLowerUnchanged);
    DwmGpuSceneOcclusion(Windows, Count, &Space, g_composeOcclusion);
    DptEnd(&g_DwmPresentTrace, Trace, TRUE, 0);
}

static const RECT *
DwmGpuComposeSceneCover(const DWM_WIN *Window)
{
    ULONG Index;

    if (Window->SurfaceId == (ULONG)-1)
        return &g_composeOcclusion[0];
    for (Index = 0; Index < g_composeScene.Count; ++Index)
    {
        if (g_composeScene.Windows[Index].SurfaceId == Window->SurfaceId)
            return &g_composeOcclusion[Index + 1];
    }
    return NULL;
}

/* Draws a window-space rectangle as a textured quad in clip space. */
static void
DwmGpuComposeQuadOriented(LONGLONG Left, LONGLONG Top, LONGLONG Right, LONGLONG Bottom,
                          GLfloat Alpha, BOOL FramebufferSource)
{
    GLfloat l = (GLfloat)(2.0 * Left / (double)g_composeWidth - 1.0);
    GLfloat r = (GLfloat)(2.0 * Right / (double)g_composeWidth - 1.0);
    /* Window space grows downward, clip space upward. */
    GLfloat t = (GLfloat)(1.0 - 2.0 * Top / (double)g_composeHeight);
    GLfloat b = (GLfloat)(1.0 - 2.0 * Bottom / (double)g_composeHeight);
    GLfloat SourceTop = FramebufferSource ? 1.0f : 0.0f;
    GLfloat SourceBottom = FramebufferSource ? 0.0f : 1.0f;

    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, SourceTop); glVertex2f(l, t);
    glTexCoord2f(1.0f, SourceTop); glVertex2f(r, t);
    glTexCoord2f(0.0f, SourceBottom); glVertex2f(l, b);
    glTexCoord2f(1.0f, SourceBottom); glVertex2f(r, b);
    glEnd();
}

static void
DwmGpuComposeQuad(LONGLONG Left, LONGLONG Top, LONGLONG Right, LONGLONG Bottom, GLfloat Alpha)
{
    DwmGpuComposeQuadOriented(Left, Top, Right, Bottom, Alpha, FALSE);
}

static ULONG
DwmGpuComposeUncoveredParts(const RECT *Bounds, const RECT *Cover, RECT *Parts)
{
    RECT Cut;

    if (Cover == NULL || Cover->left >= Cover->right || Cover->top >= Cover->bottom ||
        !DwmGpuDamageIntersects(Bounds, Cover))
    {
        Parts[0] = *Bounds;
        return 1;
    }
    SetRect(&Cut, max(Bounds->left, Cover->left), max(Bounds->top, Cover->top),
             min(Bounds->right, Cover->right), min(Bounds->bottom, Cover->bottom));
    SetRect(&Parts[0], Bounds->left, Bounds->top, Bounds->right, Cut.top);
    SetRect(&Parts[1], Bounds->left, Cut.bottom, Bounds->right, Bounds->bottom);
    SetRect(&Parts[2], Bounds->left, Cut.top, Cut.left, Cut.bottom);
    SetRect(&Parts[3], Cut.right, Cut.top, Bounds->right, Cut.bottom);
    return 4;
}

/* Keep quad interpolation while clipping opaque coverage. Scene covers are
 * chosen before drawing and never omit pixels needed by a backdrop capture. */
static void
DwmGpuComposeUncoveredQuad(LONGLONG Left, LONGLONG Top, LONGLONG Right, LONGLONG Bottom,
                           GLfloat Alpha, const RECT *Bounds, const RECT *Cover,
                           const RECT *SceneCover)
{
    RECT Parts[4], Visible[4];
    ULONG Count, Index, VisibleCount, Other;

    Count = DwmGpuComposeUncoveredParts(Bounds, SceneCover, Parts);
    for (Index = 0; Index < Count; ++Index)
    {
        const RECT *Part = &Parts[Index];

        if (Part->left >= Part->right || Part->top >= Part->bottom)
            continue;
        VisibleCount = DwmGpuComposeUncoveredParts(Part, Cover, Visible);
        for (Other = 0; Other < VisibleCount; ++Other)
        {
            const RECT *Rect = &Visible[Other];

            if (Rect->left >= Rect->right || Rect->top >= Rect->bottom)
                continue;
            glScissor(Rect->left, g_composeHeight - Rect->bottom,
                       Rect->right - Rect->left, Rect->bottom - Rect->top);
            DwmGpuComposeQuad(Left, Top, Right, Bottom, Alpha);
        }
    }
    glScissor(Bounds->left, g_composeHeight - Bounds->bottom,
               Bounds->right - Bounds->left, Bounds->bottom - Bounds->top);
}

static void DwmGpuComposeReleaseBlur(void);
static BOOL DwmGpuComposeMaterial(const DWM_WIN *Window, GLuint Texture,
                       LONGLONG Left, LONGLONG Top, LONG Width, LONG Height, GLfloat Alpha,
                       const RECT *Cover, const RECT *SceneCover);
static GLuint g_shadowProgram;
static struct { GLint Owner, Size, Sigma, Opacity, ScreenHeight, Offset, WindowAlpha; } g_shadowLoc;
/* Locations belong to this linked program and are refreshed after context
 * recreation. Never resolve uniform names in the per-window draw path. */
typedef struct _DWM_GPU_MATERIAL_PROGRAM
{
    GLuint Program;
    GLint Window, Backdrop, Glass, Whole, PixelAlpha, UseKey;
    GLint Opacity, Alpha, Radius, Saturation, Reflection;
    GLint Size, ScreenSize, ClientMin, ClientMax;
    GLint CaptureOrigin, CaptureSize, Brush, Colorization, Key;
} DWM_GPU_MATERIAL_PROGRAM;
static DWM_GPU_MATERIAL_PROGRAM g_materialPrograms[2];

static DWM_GPU_TEXTURE *
DwmGpuComposeFindTexture(const DWM_WIN *Window, BOOL Client)
{
    DWM_GPU_TEXTURE *Free = NULL;
    DWM_GPU_TEXTURE *Oldest = &g_composeTextures[0];
    ULONG i;

    for (i = 0; i < DWM_GPU_TEXTURE_SLOTS; ++i)
    {
        DWM_GPU_TEXTURE *Slot = &g_composeTextures[i];

        if (Slot->Texture != 0 && Slot->SurfaceId == Window->SurfaceId &&
            Slot->Client == Client)
            return Slot;
        if (Slot->Texture == 0 && Free == NULL)
            Free = Slot;
        if (Slot->LastFrame < Oldest->LastFrame)
            Oldest = Slot;
    }
    if (Free != NULL)
        return Free;

    /* Every slot is live; retire the one untouched for longest. */
    glDeleteTextures(1, &Oldest->Texture);
    RtlZeroMemory(Oldest, sizeof(*Oldest));
    return Oldest;
}

static BOOL
DwmGlComposeInitialize(LONG Width, LONG Height)
{
    WNDCLASSEXW Class;
    PIXELFORMATDESCRIPTOR Descriptor;
    const char *Version;
    const char *Renderer;
    int Format;

    if (g_composeActive)
        return TRUE;
    if (Width <= 0 || Height <= 0)
        return FALSE;

    RtlZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = DWM_GPU_CLASS_COMPOSE;
    if (RegisterClassExW(&Class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return FALSE;
    }

    /*
     * The compositor owns the whole screen, so its output surface is a
     * screen-sized window.  Presenting through it lets the ICD hand the
     * finished frame to scanout without the frame ever reaching the CPU.
     */
    g_composeWindow = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
                                      WS_EX_TRANSPARENT,
                                      DWM_GPU_CLASS_COMPOSE, L"",
                                      WS_POPUP, 0, 0, Width, Height,
                                      NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (g_composeWindow == NULL)
        return FALSE;

    /* Register before showing so win32k excludes this carrier from DWM's
     * input and redirection paths.  Show before acquiring its DC: a DC
     * acquired while hidden retains an empty visible clip, which makes Mesa
     * reject direct-primary presentation and copy through GDI instead. */
    {
        DWM_GPU_OUTPUT Output;

        RtlZeroMemory(&Output, sizeof(Output));
        Output.StructSize = sizeof(Output);
        Output.Window = (ULONGLONG)(ULONG_PTR)g_composeWindow;
        Output.Width = (ULONG)Width;
        Output.Height = (ULONG)Height;
        if (!SetPropW(g_composeWindow, DWM_PROP_GPU_OUTPUT, (HANDLE)1) ||
            (LONG)NtUserCallOneParam((DWORD_PTR)&Output,
                                     DWM_ROUTINE_SETGPUOUTPUT) < 0)
        {
            RemovePropW(g_composeWindow, DWM_PROP_GPU_OUTPUT);
            DwmGpuComposeShutdown();
            return FALSE;
        }
        g_composeOutputRegistered = TRUE;
    }

    /* Mesa only offers its direct-primary KMT present for a visible,
     * fullscreen, unobscured window. */
    if (!SetWindowPos(g_composeWindow, HWND_TOPMOST, 0, 0, Width, Height,
                      SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW))
    {
        DwmGpuComposeShutdown();
        return FALSE;
    }

    g_composeDc = GetDC(g_composeWindow);
    if (g_composeDc == NULL)
    {
        DwmGpuComposeShutdown();
        return FALSE;
    }

    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.nSize = sizeof(Descriptor);
    Descriptor.nVersion = 1;
    Descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
                         PFD_DOUBLEBUFFER;
    Descriptor.iPixelType = PFD_TYPE_RGBA;
    Descriptor.cColorBits = 32;
    Descriptor.iLayerType = PFD_MAIN_PLANE;

    Format = ChoosePixelFormat(g_composeDc, &Descriptor);
    if (Format == 0 || !SetPixelFormat(g_composeDc, Format, &Descriptor))
    {
        DwmGpuComposeShutdown();
        return FALSE;
    }

    g_composeContext = wglCreateContext(g_composeDc);
    if (g_composeContext == NULL ||
        !wglMakeCurrent(g_composeDc, g_composeContext))
    {
        DwmGpuComposeShutdown();
        return FALSE;
    }

    Version = (const char *)glGetString(GL_VERSION);
    Renderer = (const char *)glGetString(GL_RENDERER);
    if (!DwmGpuVersionAtLeast2(Version) ||
        !DwmGpuRendererIsHardware(Renderer))
    {
        DwmGpuComposeShutdown();
        return FALSE;
    }

    g_composeWidth = Width;
    g_composeHeight = Height;
    {
        PFNWGLGETPIXELFORMATATTRIBIVARBPROC QueryFormat =
            (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)
                DwmGpuGetProc("wglGetPixelFormatAttribivARB");
        INT Attribute = WGL_SWAP_METHOD_ARB;
        INT Method = WGL_SWAP_UNDEFINED_ARB;

        RtlZeroMemory(&g_composeDamage, sizeof(g_composeDamage));
        /* PFD_SWAP_* alone is only a hint. Query the actual WGL contract. */
        if (QueryFormat != NULL &&
            QueryFormat(g_composeDc, Format, 0, 1, &Attribute, &Method))
            g_composeDamage.SwapMethod = Method;
        const char *Extensions = (const char *)glGetString(GL_EXTENSIONS);
        int MajorVersion = 0;
        GLint SampleBuffers = 1;

        sscanf(Version, "%d", &MajorVersion);
        g_composeBlitFramebuffer = NULL;
        if (MajorVersion >= 3 || DwmGpuHasExtension(Extensions, "GL_ARB_framebuffer_object"))
            g_composeBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)DwmGpuGetProc("glBlitFramebuffer");
        else if (DwmGpuHasExtension(Extensions, "GL_EXT_framebuffer_blit"))
            g_composeBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)DwmGpuGetProc("glBlitFramebufferEXT");
        /* Multisample resolves cannot also resize the capture. */
        if (g_composeBlitFramebuffer != NULL)
        {
            glGetIntegerv(GL_SAMPLE_BUFFERS, &SampleBuffers);
            if (SampleBuffers != 0)
                g_composeBlitFramebuffer = NULL;
        }
        if (Extensions != NULL && strstr(Extensions, "GL_WIN_swap_hint") != NULL)
            g_addSwapHint = (void (APIENTRY *)(GLint, GLint, GLsizei, GLsizei))
                DwmGpuGetProc("glAddSwapHintRectWIN");
    }
    glViewport(0, 0, Width, Height);
    glDisable(GL_DEPTH_TEST);
    /* Desktop redirection surfaces contain exact 8-bit GDI colors. The GL
     * default dither state can change an opaque source channel by one code
     * value at some scanout pixels, even when no scaling or blend is needed. */
    glDisable(GL_DITHER);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* Resolving a WGL export requires this context. Calling the cached
     * telemetry export later does not require a current context. */
    DwmPresentTraceSetIcd((PFNWGLCONTROLPRESENTATIONTRACEROS)
        DwmGpuGetProc("wglControlPresentationTraceROS"));
    wglMakeCurrent(NULL, NULL);
    g_composeRetryShare = 0;
    g_composeRetryable = FALSE;
    g_composeActive = TRUE;
    {
        char Message[160];

        _snprintf(Message, sizeof(Message) - 1,
                  "DWM: GPU compose active on %s, %ldx%ld\n",
                  Renderer != NULL ? Renderer : "?", Width, Height);
        Message[sizeof(Message) - 1] = '\0';
        OutputDebugStringA(Message);
    }
    return TRUE;
}

BOOL
DwmGpuComposeInitialize(LONG Width, LONG Height)
{
    /* Preserve the working ICD path on adapters that provide it. A native
     * Direct3D-only adapter can compose without an OpenGL ICD. */
    return DwmD3dIsActive() || DwmGlComposeInitialize(Width, Height) ||
           DwmD3dInitialize(Width, Height);
}

BOOL
DwmGpuComposeIsActive(void)
{
    return g_composeActive || DwmD3dIsActive();
}

static BOOL
DwmGpuComposeRepairBackBuffer(RECT *Draw)
{
    const RECT *Current = &g_composeDamage.Current;
    const RECT *Previous = &g_composeDamage.Previous;
    GLint ReadBuffer;

    if (g_composeBlitFramebuffer == NULL ||
        g_composeDamage.SwapMethod != WGL_SWAP_EXCHANGE_ARB ||
        g_composeDamage.ValidFrames < 2 ||
        (ULONGLONG)(Draw->right - Draw->left) * (Draw->bottom - Draw->top) <=
        (ULONGLONG)(Current->right - Current->left) * (Current->bottom - Current->top) +
        (ULONGLONG)(Previous->right - Previous->left) * (Previous->bottom - Previous->top))
        return TRUE;

    /* The front buffer already contains the preceding frame's changes.
     * Copy them into the two-frames-old back buffer instead of recomposing
     * the unchanged gap between distant updates, such as a taskbar icon
     * and an OpenGL window. Both buffers remain complete after the swap. */
    glDisable(GL_SCISSOR_TEST);
    glGetIntegerv(GL_READ_BUFFER, &ReadBuffer);
    glReadBuffer(GL_FRONT);
    g_composeBlitFramebuffer(Previous->left, g_composeHeight - Previous->bottom,
                             Previous->right, g_composeHeight - Previous->top,
                             Previous->left, g_composeHeight - Previous->bottom,
                             Previous->right, g_composeHeight - Previous->top,
                             GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glReadBuffer(ReadBuffer);
    if (glGetError() != GL_NO_ERROR)
        return FALSE;
    *Draw = *Current;
    return TRUE;
}

static BOOL
DwmGpuComposeBeginMeasured(const BYTE *BackdropPixels,
                    BOOL RefreshBackdrop, const RECT *Damage)
{
    DWM_WIN Backdrop;
    RECT Draw, Expanded;
    DWM_GPU_DAMAGE Preview;
    if (!g_composeActive)
        return FALSE;
    if (!wglMakeCurrent(g_composeDc, g_composeContext))
        return FALSE;

    g_composeRetryable = FALSE;
    DwmGpuComposePruneTextures();
    ++g_composeFrame;
    g_composeBlurOwnerValid = FALSE;
    if (Damage != NULL && !RefreshBackdrop)
    {
        Expanded = *Damage;
        DwmGpuDamageUnion(&Expanded, &g_composeScene.AnimationDamage);
        Damage = &Expanded;
    }
    /* Select reusable captures against both this damage and any repair
     * needed by the preserved back buffer, before expanding dependencies. */
    Preview = g_composeDamage;
    Draw = DwmGpuDamageBegin(&Preview, g_composeWidth, g_composeHeight, RefreshBackdrop ? NULL : Damage);
    DwmGpuComposePinBlurResults(&Draw);
    if (Damage != NULL && !RefreshBackdrop)
    {
        Expanded = *Damage;
        DwmGpuDamageExpandBlur(&Expanded, g_composeWidth, g_composeHeight,
                               g_composeScene.Windows, g_composeScene.Count,
                               g_composeScene.Space.OriginX, g_composeScene.Space.OriginY,
                               g_composeBlurRadius, g_composeCachedCapture);
        Damage = &Expanded;
    }
    Draw = DwmGpuDamageBegin(&g_composeDamage, g_composeWidth, g_composeHeight,
                             RefreshBackdrop ? NULL : Damage);
    if (!DwmGpuComposeRepairBackBuffer(&Draw))
        return FALSE;
    /* Exchange buffers also contain the previous frame's damage. Its union
     * can intersect another capture, so close the actual repair region too. */
    DwmGpuDamageExpandBlur(&Draw, g_composeWidth, g_composeHeight,
                           g_composeScene.Windows, g_composeScene.Count,
                           g_composeScene.Space.OriginX, g_composeScene.Space.OriginY,
                           g_composeBlurRadius, g_composeCachedCapture);
    g_composeDamage.Draw = Draw;
    glEnable(GL_SCISSOR_TEST);
    glScissor(Draw.left, g_composeHeight - Draw.bottom,
              Draw.right - Draw.left, Draw.bottom - Draw.top);
    /* The opaque wallpaper covers every pixel inside this scissor. Clearing
     * it first is redundant and can expand tiled rendering beyond damage. */
    /* Kernel surface slots cannot use this reserved wallpaper identity. */
    RtlZeroMemory(&Backdrop, sizeof(Backdrop));
    Backdrop.SurfaceId = (ULONG)-1;
    Backdrop.Generation = 1;
    Backdrop.cx = g_composeWidth;
    Backdrop.cy = g_composeHeight;
    Backdrop.Stride = g_composeWidth * sizeof(ULONG);
    Backdrop.Alpha = 255;
    Backdrop.Damaged = RefreshBackdrop;
    return DwmGpuComposeWindow(&Backdrop, BackdropPixels, 0, 0);
}

BOOL
DwmGpuComposeBegin(ULONG BackdropColor, const BYTE *BackdropPixels,
                    BOOL RefreshBackdrop, const RECT *Damage)
{
    if (DwmD3dIsActive())
    {
        DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_BEGIN);
        BOOL Result = DwmD3dBegin(BackdropColor, BackdropPixels, RefreshBackdrop, Damage);
        DptEnd(&g_DwmPresentTrace, Trace, Result, 0);
        return Result;
    }
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_BEGIN);
    BOOL Result = DwmGpuComposeBeginMeasured(BackdropPixels, RefreshBackdrop, Damage);
    ULONGLONG Bytes = Result ?
        (ULONGLONG)(g_composeDamage.Draw.right - g_composeDamage.Draw.left) *
        (g_composeDamage.Draw.bottom - g_composeDamage.Draw.top) * sizeof(ULONG) : 0;
    DptEnd(&g_DwmPresentTrace, Trace, Result, Bytes);
    return Result;
}

static BOOL
DwmGpuComposeLayerMeasured(const DWM_WIN *Window, const BYTE *Pixels,
                   LONG OriginX, LONG OriginY, BOOL Client)
{
    DWM_GPU_TEXTURE *Slot;
    GLfloat Alpha = 1.0f;
    DWM_GPU_WINDOW_GEOMETRY Geometry;
    RECT Bounds, Cover;
    const RECT *OpaqueClient = NULL;
    const RECT *SceneCover;

    if (!g_composeActive || Window == NULL ||
        Window->cx <= 0 || Window->cy <= 0 ||
        (ULONG)Window->cx > ((ULONG)-1) / sizeof(ULONG) ||
        Window->Stride < (ULONG)Window->cx * sizeof(ULONG) ||
        Window->Stride % sizeof(ULONG) != 0)
    {
        return FALSE;
    }

    /* Client publications must still be sampled before their acknowledgement
     * permits the producer to overwrite the imported storage. */
    SceneCover = Client ? NULL : DwmGpuComposeSceneCover(Window);
    if (!DwmGpuWindowGeometry(Window, OriginX, OriginY, &Geometry) ||
        !DwmGpuDamageBounds(&Bounds, g_composeWidth, g_composeHeight,
            Geometry.Left, Geometry.Top, Geometry.Left + Geometry.Width, Geometry.Top + Geometry.Height) ||
        !DwmGpuDamageIntersects(&Bounds, &g_composeDamage.Draw))
        return TRUE;
    Slot = DwmGpuComposeFindTexture(Window, Client);
    if (Slot == NULL)
        return FALSE;

    if (Window->BaseGlobalShare != 0)
    {
        if (Window->BaseWidth != (ULONG)Window->cx ||
            Window->BaseHeight != (ULONG)Window->cy ||
            Window->BaseFormat != DWM_WGL_SHARED_BGRA8)
            return FALSE;
        if (g_bindSharedTexture == NULL)
            g_bindSharedTexture = (PFNWGLBINDSHAREDTEXTUREROS)
                DwmGpuGetProc("wglBindSharedTextureROS");
        if (g_bindSharedTexture == NULL)
            return FALSE;
        if (g_updateSharedTexture == NULL)
            g_updateSharedTexture = (PFNWGLUPDATESHAREDTEXTUREROS)
                DwmGpuGetProc("wglUpdateSharedTextureROS");
        if (g_updateSharedTexture == NULL)
            return FALSE;
        if (Slot->Texture == 0 || Slot->GlobalShare != Window->BaseGlobalShare ||
            Slot->ShareGeneration != Window->BaseGeneration ||
            Slot->Width != Window->cx || Slot->Height != Window->cy ||
            Slot->Pitch != Window->BasePitch)
        {
            if (Slot->Texture != 0)
                glDeleteTextures(1, &Slot->Texture);
            RtlZeroMemory(Slot, sizeof(*Slot));
            glGenTextures(1, &Slot->Texture);
            if (Slot->Texture == 0)
                return FALSE;
            glBindTexture(GL_TEXTURE_2D, Slot->Texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (!g_bindSharedTexture(DWM_WGL_SHARED_TEXTURE_VERSION,
                                      Window->BaseGlobalShare, Window->BaseWidth,
                                      Window->BaseHeight, Window->BasePitch,
                                      Window->BaseFormat))
            {
                char Message[192];
                DWORD Error = GetLastError();

                _snprintf(Message, sizeof(Message),
                          "DWMGPU: import failed id=%lu client=%u share=%lx size=%lux%lu pitch=%lu error=%lu\n",
                          Window->SurfaceId, Client, Window->BaseGlobalShare,
                          Window->BaseWidth, Window->BaseHeight,
                          Window->BasePitch, Error);
                OutputDebugStringA(Message);
                if (!Client && Error == ERROR_INVALID_HANDLE &&
                    g_composeRetryShare != Window->BaseGlobalShare)
                {
                    g_composeRetryShare = Window->BaseGlobalShare;
                    g_composeRetryable = TRUE;
                }
                glDeleteTextures(1, &Slot->Texture);
                RtlZeroMemory(Slot, sizeof(*Slot));
                return FALSE;
            }
            Slot->SurfaceId = Window->SurfaceId;
            Slot->Client = Client;
            Slot->GlobalShare = Window->BaseGlobalShare;
            Slot->ShareGeneration = Window->BaseGeneration;
            Slot->Pitch = Window->BasePitch;
            Slot->Width = Window->cx;
            Slot->Height = Window->cy;
        }
        glBindTexture(GL_TEXTURE_2D, Slot->Texture);
        if (!Slot->PublicationValid || Slot->UpdateId != Window->BaseUpdateId)
        {
            if (!g_updateSharedTexture(DWM_WGL_SHARED_TEXTURE_VERSION))
                return FALSE;
            Slot->UpdateId = Window->BaseUpdateId;
            Slot->PublicationValid = TRUE;
        }
    }
    else if (Window->SurfaceId != (ULONG)-1)
    {
        /* Dynamic windows must be imported. Only the static wallpaper uses
         * the image upload path below. Never hide a failed import with a
         * per-frame CPU snapshot and glTexSubImage2D. */
        return FALSE;
    }
    else if (Slot->Texture == 0 ||
        Slot->Width != Window->cx || Slot->Height != Window->cy)
    {
        if (Pixels == NULL)
            return FALSE;
        if (Slot->Texture != 0)
            glDeleteTextures(1, &Slot->Texture);
        RtlZeroMemory(Slot, sizeof(*Slot));
        if (!DwmGpuMakeTexture(&Slot->Texture, Window->cx, Window->cy))
            return FALSE;
        Slot->SurfaceId = Window->SurfaceId;
        Slot->Width = Window->cx;
        Slot->Height = Window->cy;
        glBindTexture(GL_TEXTURE_2D, Slot->Texture);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, Window->Stride / sizeof(ULONG));
        {
            DPT_SCOPE UploadTrace = DptBegin(&g_DwmPresentTrace, DPT_CPU_UPLOAD);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Window->cx, Window->cy,
                        GL_BGRA_EXT, GL_UNSIGNED_BYTE, Pixels);
            DptEnd(&g_DwmPresentTrace, UploadTrace, TRUE, (ULONGLONG)Window->cx * Window->cy * 4);
        }
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, Slot->Texture);
        /*
         * Front/back publication changes the source generation, not the GL
         * storage geometry. Refresh its pixels without deleting/reallocating
         * the GPU texture on every publication. A generation change requires
         * an upload even when this frame's damage flag has been deferred.
         */
        if (Slot->Generation != Window->Generation || Window->Damaged != 0)
        {
            if (Pixels == NULL)
                return FALSE;
            glPixelStorei(GL_UNPACK_ROW_LENGTH, Window->Stride / sizeof(ULONG));
            {
                DPT_SCOPE UploadTrace = DptBegin(&g_DwmPresentTrace, DPT_CPU_UPLOAD);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Window->cx, Window->cy,
                                GL_BGRA_EXT, GL_UNSIGNED_BYTE, Pixels);
                DptEnd(&g_DwmPresentTrace, UploadTrace, TRUE, (ULONGLONG)Window->cx * Window->cy * 4);
            }
        }
    }
    Slot->Generation = Window->Generation;
    Slot->LastFrame = g_composeFrame;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    /* GDI BGRX backing stores have an undefined high byte. Only windows
     * explicitly using per-pixel alpha may take opacity from the texture. */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA,
               (Window->BlurFlags & DWM_BLUR_ENABLE) ? GL_MODULATE : GL_REPLACE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA,
               (Window->BlurFlags & DWM_BLUR_ENABLE) ? GL_TEXTURE : GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);

    if (Window->LayerFlags & DWM_LWA_ALPHA)
        Alpha = (GLfloat)(Window->Alpha / 255.0);
    if (Alpha <= 0.0f)
        return TRUE;

    if (!Client && Window->DxGlobalShare != 0 && Window->DxUpdateId != 0 &&
        Window->AnimFlags == 0 && Alpha >= 1.0f &&
        !(Window->LayerFlags & DWM_LWA_COLORKEY) &&
        !(Window->BlurFlags & DWM_BLUR_ENABLE) &&
        (Window->BackdropType != DWM_BACKDROP_TRANSIENT ||
         Window->BackdropRegion == DWM_BACKDROP_REGION_NONCLIENT) &&
        DwmGpuDamageBounds(&Cover, g_composeWidth, g_composeHeight,
            Geometry.Left + Window->DxClientX, Geometry.Top + Window->DxClientY,
            Geometry.Left + Window->DxClientX + Window->DxWidth,
            Geometry.Top + Window->DxClientY + Window->DxHeight))
        OpaqueClient = &Cover;

    if (Window->BackdropType == DWM_BACKDROP_TRANSIENT ||
        Window->CornerRadius != 0 || (Window->LayerFlags & DWM_LWA_COLORKEY))
        return DwmGpuComposeMaterial(Window, Slot->Texture, Geometry.Left, Geometry.Top, Geometry.Width, Geometry.Height, Alpha, OpaqueClient, SceneCover);
    /* BGRX rectangles with full global opacity overwrite their destination.
     * Avoid destination blending for the desktop and opaque window content;
     * glass/rounded/color-key materials returned through the shader above. */
    if (Alpha >= 1.0f && !(Window->BlurFlags & DWM_BLUR_ENABLE))
        glDisable(GL_BLEND);
    DwmGpuComposeUncoveredQuad(Geometry.Left, Geometry.Top, Geometry.Left + Geometry.Width, Geometry.Top + Geometry.Height, Alpha, &g_composeDamage.Draw, OpaqueClient, SceneCover);
    glEnable(GL_BLEND);
    return glGetError() == GL_NO_ERROR;
}

static BOOL
DwmGpuComposeLayer(const DWM_WIN *Window, const BYTE *Pixels,
                   LONG OriginX, LONG OriginY, BOOL Client)
{
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_TEXTURE);
    BOOL Result = DwmGpuComposeLayerMeasured(Window, Pixels, OriginX, OriginY, Client);
    DptEnd(&g_DwmPresentTrace, Trace, Result, 0);
    return Result;
}

static BOOL
DwmGpuComposeWindowMeasured(const DWM_WIN *Window, const BYTE *Pixels,
                    LONG OriginX, LONG OriginY)
{
    DWM_WIN Client;
    if (!DwmGpuComposeLayer(Window, Pixels, OriginX, OriginY, FALSE))
        return FALSE;

    /* Any later client-layer capture includes this window's base pixels. */
    g_composeBlurOwnerValid = FALSE;
    if (Window->DxGlobalShare == 0 || Window->DxUpdateId == 0)
        return TRUE;
    if (Window->DxWidth > MAXLONG || Window->DxHeight > MAXLONG)
        return FALSE;
    Client = *Window;
    if (Window->AnimFlags == 0)
    {
        LONGLONG X = (LONGLONG)Window->x + Window->DxClientX;
        LONGLONG Y = (LONGLONG)Window->y + Window->DxClientY;

        if (X < (LONG)MINLONG || X > MAXLONG || Y < (LONG)MINLONG || Y > MAXLONG)
            return FALSE;
        Client.x = (LONG)X;
        Client.y = (LONG)Y;
    }
    Client.cx = Window->DxWidth;
    Client.cy = Window->DxHeight;
    Client.Stride = Window->DxPitch;
    Client.BaseGlobalShare = Window->DxGlobalShare;
    Client.BaseGeneration = Window->DxGeneration;
    Client.BaseUpdateId = Window->DxUpdateId;
    Client.BaseWidth = Window->DxWidth;
    Client.BaseHeight = Window->DxHeight;
    Client.BasePitch = Window->DxPitch;
    Client.BaseFormat = Window->DxFormat;
    Client.CornerRadius = 0;
    if (Window->AnimFlags != 0)
    {
        LONGLONG Left, Top, Right, Bottom;

        if (Window->cx <= 0 || Window->cy <= 0 || Window->AnimCx <= 0 || Window->AnimCy <= 0)
            return TRUE;
        Left = DwmGpuScaleWindowEdge(Window->DxClientX, Window->cx, Window->AnimCx);
        Top = DwmGpuScaleWindowEdge(Window->DxClientY, Window->cy, Window->AnimCy);
        Right = DwmGpuScaleWindowEdge((LONGLONG)Window->DxClientX + Window->DxWidth, Window->cx, Window->AnimCx);
        Bottom = DwmGpuScaleWindowEdge((LONGLONG)Window->DxClientY + Window->DxHeight, Window->cy, Window->AnimCy);
        if (Right <= Left || Bottom <= Top)
            return TRUE;
        if ((LONGLONG)Window->AnimX + Left < (LONG)MINLONG || (LONGLONG)Window->AnimX + Left > MAXLONG ||
            (LONGLONG)Window->AnimY + Top < (LONG)MINLONG || (LONGLONG)Window->AnimY + Top > MAXLONG ||
            Right - Left > MAXLONG || Bottom - Top > MAXLONG)
            return FALSE;
        Client.AnimX = (LONG)(Window->AnimX + Left);
        Client.AnimY = (LONG)(Window->AnimY + Top);
        Client.AnimCx = (LONG)(Right - Left);
        Client.AnimCy = (LONG)(Bottom - Top);
    }
    if (Client.BackdropRegion == DWM_BACKDROP_REGION_NONCLIENT)
        Client.BackdropType = 0;
    return DwmGpuComposeLayer(&Client, NULL, OriginX, OriginY, TRUE);
}

BOOL
DwmGpuComposeNeedsSurfacePixels(const DWM_WIN *Window)
{
    return DwmD3dIsActive() && DwmD3dNeedsSurfacePixels(Window);
}

BOOL
DwmGpuComposeWindow(const DWM_WIN *Window, const BYTE *Pixels,
                    LONG OriginX, LONG OriginY)
{
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_WINDOW);
    BOOL Result = DwmD3dIsActive() ? DwmD3dWindow(Window, Pixels, OriginX, OriginY) :
                    DwmGpuComposeWindowMeasured(Window, Pixels, OriginX, OriginY);
    DptEnd(&g_DwmPresentTrace, Trace, Result, 0);
    return Result;
}

static BOOL
DwmGpuComposeEndMeasured(void)
{
    BOOL Result;
    const RECT *PresentDamage = g_composeDamage.ValidFrames >= 2 ?
                                  &g_composeDamage.Current : &g_composeDamage.Draw;

    if (!g_composeActive)
        return FALSE;
    glDisable(GL_SCISSOR_TEST);
    /* Repairing the exchanged back buffer also redraws the previous frame's
     * damage. Those pixels already match scanout; publish only this frame's
     * changes once both buffers are initialized. Failed swaps reset history. */
    if (g_addSwapHint != NULL)
        g_addSwapHint(PresentDamage->left,
                      g_composeHeight - PresentDamage->bottom,
                      PresentDamage->right - PresentDamage->left,
                      PresentDamage->bottom - PresentDamage->top);
    Result = SwapBuffers(g_composeDc);
    DwmGpuDamageEnd(&g_composeDamage, Result);
    g_composeScene.Valid = Result;
    DwmGpuComposeUnpinBlurResults();
    /* This dedicated compositor thread retains its context between frames.
     * Unbinding invokes another WGL front-buffer flush and drops framebuffer
     * references; the next Begin would immediately acquire them again.
     * Shutdown explicitly unbinds before destroying the context. */
    return Result;
}

DWM_GPU_RESULT
DwmGpuComposeEnd(void)
{
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_SWAP);
    DWM_GPU_RESULT Result = DwmD3dIsActive() ? DwmD3dEnd() :
        (DwmGpuComposeEndMeasured() ? DWM_GPU_COMPLETE : DWM_GPU_FAILED);
    if (Result == DWM_GPU_COMPLETE)
        g_composeRetryShare = 0;
    DptEnd(&g_DwmPresentTrace, Trace, Result == DWM_GPU_COMPLETE, 0);
    return Result;
}

DWM_GPU_RESULT
DwmGpuComposeAbort(void)
{
    if (DwmD3dIsActive() || !g_composeActive || !g_composeRetryable)
        return DWM_GPU_FAILED;

    glDisable(GL_SCISSOR_TEST);
    DwmGpuDamageEnd(&g_composeDamage, FALSE);
    g_composeScene.Valid = FALSE;
    DwmGpuComposeUnpinBlurResults();
    g_composeRetryable = FALSE;
    return DWM_GPU_RETRY;
}

DWM_GPU_RESULT
DwmGpuComposeCheckOutput(void)
{
    return DwmD3dIsActive() ? DwmD3dCheckOutput() :
        (g_composeActive ? DWM_GPU_COMPLETE : DWM_GPU_FAILED);
}

void
DwmGpuComposeShutdown(void)
{
    ULONG i;

    DwmD3dShutdown();
    if (g_composeOutputRegistered)
    {
        DWM_GPU_OUTPUT Output;

        RtlZeroMemory(&Output, sizeof(Output));
        Output.StructSize = sizeof(Output);
        (void)NtUserCallOneParam((DWORD_PTR)&Output,
                                 DWM_ROUTINE_SETGPUOUTPUT);
        g_composeOutputRegistered = FALSE;
    }

    if (g_composeContext != NULL)
    {
        wglMakeCurrent(g_composeDc, g_composeContext);
        for (i = 0; i < DWM_GPU_TEXTURE_SLOTS; ++i)
        {
            if (g_composeTextures[i].Texture != 0)
                glDeleteTextures(1, &g_composeTextures[i].Texture);
        }
        RtlZeroMemory(g_composeTextures, sizeof(g_composeTextures));
        DwmGpuComposeReleaseBlur();
        for (i = 0; i < ARRAYSIZE(g_materialPrograms); ++i)
        {
            if (g_materialPrograms[i].Program != 0)
                pglDeleteProgram(g_materialPrograms[i].Program);
        }
        RtlZeroMemory(g_materialPrograms, sizeof(g_materialPrograms));
        if (g_shadowProgram != 0)
            pglDeleteProgram(g_shadowProgram);
        g_shadowProgram = 0;
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_composeContext);
        g_composeContext = NULL;
    }
    if (g_composeDc != NULL)
    {
        ReleaseDC(g_composeWindow, g_composeDc);
        g_composeDc = NULL;
    }
    if (g_composeWindow != NULL)
    {
        RemovePropW(g_composeWindow, DWM_PROP_GPU_OUTPUT);
        DestroyWindow(g_composeWindow);
        g_composeWindow = NULL;
    }
    g_composeActive = FALSE;
    g_composeRetryShare = 0;
    g_composeRetryable = FALSE;
    g_composeBlitFramebuffer = NULL;
    RtlZeroMemory(&g_composeDamage, sizeof(g_composeDamage));
    g_addSwapHint = NULL;
    g_composeScene.Valid = FALSE;
    g_composeBlurOwnerValid = FALSE;
    g_bindSharedTexture = NULL;
    g_updateSharedTexture = NULL;
}

/*
 * Blur a region of the frame that is already on the GPU.
 *
 * The piecemeal blur this replaces had to upload the region and read the
 * result back every frame, and that round trip cost more than the CPU blur it
 * was trying to beat.  Here the pixels are already in the back buffer, so the
 * region is copied framebuffer-to-texture with glCopyTexSubImage2D, filtered
 * through the same separable kernel, and drawn straight back -- the CPU never
 * sees a pixel.
 */
static GLuint g_composeBlurProgram;
static GLuint g_composeBlurPrograms[2 + DWM_GPU_MAX_RADIUS / 2];
static GLint  g_composeBlurSourceLoc;
static GLint  g_composeBlurStepLoc;
static GLint  g_composeBlurWeightsLoc;
static GLint  g_composeBlurOffsetsLoc;
static ULONG g_composeBlurKernelRadius;
static GLuint g_composeBlurTex;
static GLuint g_composeBlurPass;
static GLuint g_composeBlurCapture;
static GLuint g_composeFbo;
#define DWM_GPU_BLUR_TARGET_COUNT 4

typedef struct _DWM_GPU_BLUR_TARGET
{
    GLuint Texture, Pass, Capture;
    LONG Width, Height, FilterWidth, FilterHeight;
    BOOL Downsample;
    ULONGLONG LastUse;
    DWM_WIN Owner;
    RECT Rect;
    RECT Output;
    RECT Excluded;
    ULONG Radius, ResultFrame;
    BOOL ResultValid;
    BOOL Pinned;
} DWM_GPU_BLUR_TARGET;

static BOOL
DwmGpuBlurTargetMatches(const DWM_GPU_BLUR_TARGET *Target, LONG Width, LONG Height,
                        LONG FilterWidth, LONG FilterHeight, BOOL Downsample, BOOL NeedCapture, BOOL NeedPass)
{
    return Target->Texture != 0 && (!NeedPass || Target->Pass != 0) &&
           (!NeedCapture || Target->Capture != 0) &&
           Target->Width == Width && Target->Height == Height &&
           Target->FilterWidth == FilterWidth && Target->FilterHeight == FilterHeight &&
           Target->Downsample == Downsample;
}

static DWM_GPU_BLUR_TARGET g_composeBlurTargets[DWM_GPU_BLUR_TARGET_COUNT];
static ULONGLONG g_composeBlurTargetUse;
static DWM_GPU_BLUR_TARGET *g_composeBlurTarget;

/* Output is the complete consumer footprint, independent of frame damage.
 * Captured pixels outside it still supply the Gaussian's sampling margin. */
static BOOL
DwmGpuBlurOutputBounds(RECT *Output, const RECT *Capture,
                       LONGLONG Left, LONGLONG Top, LONG Width, LONG Height)
{
    Output->left = (LONG)max(Capture->left, min((LONGLONG)Capture->right, Left));
    Output->top = (LONG)max(Capture->top, min((LONGLONG)Capture->bottom, Top));
    Output->right = (LONG)max(Capture->left, min((LONGLONG)Capture->right, Left + Width));
    Output->bottom = (LONG)max(Capture->top, min((LONGLONG)Capture->bottom, Top + Height));
    return Output->left < Output->right && Output->top < Output->bottom;
}

static BOOL
DwmGpuBlurOutputCovers(const DWM_GPU_BLUR_TARGET *Target, const RECT *Output,
                       const RECT *Excluded)
{
    RECT Missing;

    if (Target->Output.left > Output->left || Target->Output.top > Output->top ||
        Target->Output.right < Output->right || Target->Output.bottom < Output->bottom)
        return FALSE;
    Missing.left = max(Target->Excluded.left, Output->left);
    Missing.top = max(Target->Excluded.top, Output->top);
    Missing.right = min(Target->Excluded.right, Output->right);
    Missing.bottom = min(Target->Excluded.bottom, Output->bottom);
    return Missing.left >= Missing.right || Missing.top >= Missing.bottom ||
           (Excluded != NULL && Excluded->left <= Missing.left && Excluded->top <= Missing.top &&
            Excluded->right >= Missing.right && Excluded->bottom >= Missing.bottom);
}

/* The material samples the backdrop only outside its client rectangle.
 * Keep this geometry aligned with uClientMin/uClientMax in the shader. */
static void
DwmGpuMaterialClientRect(RECT *Client, const DWM_WIN *Window, LONGLONG Left, LONGLONG Top,
                         LONG Width, LONG Height)
{
    LONGLONG Right = (LONGLONG)Window->ClientX + Window->ClientWidth;
    LONGLONG Bottom = (LONGLONG)Window->ClientY + Window->ClientHeight;
    LONGLONG X = min((LONGLONG)Window->ClientX + Window->BackdropNcExtendLeft, Right);
    LONGLONG Y = min((LONGLONG)Window->ClientY + Window->BackdropNcExtend, Bottom);

    RtlZeroMemory(Client, sizeof(*Client));
    if (Window->BackdropRegion == DWM_BACKDROP_REGION_NONCLIENT && Window->cx > 0 && Window->cy > 0)
    {
        /* Round inward: pixels on a scaled client boundary may still be
         * glass, so they must retain their filtered backdrop samples. */
        X = max(0, min(Window->cx, X));
        Y = max(0, min(Window->cy, Y));
        Right = max(0, min(Window->cx, Right));
        Bottom = max(0, min(Window->cy, Bottom));
        X = (X * Width + Window->cx - 1) / Window->cx;
        Y = (Y * Height + Window->cy - 1) / Window->cy;
        Right = Right * Width / Window->cx;
        Bottom = Bottom * Height / Window->cy;
        DwmGpuDamageBounds(Client, g_composeWidth, g_composeHeight, Left + X, Top + Y, Left + Right, Top + Bottom);
    }
}

static void
DwmGpuBlurFilterClips(const RECT *Capture, const RECT *Output,
                      LONG FilterWidth, LONG FilterHeight, ULONG KernelRadius,
                      RECT *Horizontal, RECT *Vertical)
{
    LONG Width = Capture->right - Capture->left;
    LONG Height = Capture->bottom - Capture->top;

    /* Filter textures have bottom-up rows. Include neighboring texels for
     * bilinear sampling, including odd-size downsampling and frame edges. */
    Vertical->left = max(0, (LONG)((LONGLONG)(Output->left - Capture->left) * FilterWidth / Width) - 1);
    Vertical->right = min(FilterWidth, (LONG)(((LONGLONG)(Output->right - Capture->left) * FilterWidth + Width - 1) / Width) + 1);
    Vertical->top = max(0, (LONG)((LONGLONG)(Capture->bottom - Output->bottom) * FilterHeight / Height) - 1);
    Vertical->bottom = min(FilterHeight, (LONG)(((LONGLONG)(Capture->bottom - Output->top) * FilterHeight + Height - 1) / Height) + 1);
    /* The vertical pass reads this horizontal result through the complete
     * kernel. The extra texel also covers interpolation at pixel centers. */
    Horizontal->left = max(0, Vertical->left - 1);
    Horizontal->right = min(FilterWidth, Vertical->right + 1);
    Horizontal->top = max(0, Vertical->top - (LONG)KernelRadius - 1);
    Horizontal->bottom = min(FilterHeight, Vertical->bottom + (LONG)KernelRadius + 1);
}

static void
DwmGpuBlurFilterExclusions(const RECT *Capture, const RECT *Excluded,
                           LONG FilterWidth, LONG FilterHeight, ULONG KernelRadius,
                           RECT *Horizontal, RECT *Vertical)
{
    LONG Width = Capture->right - Capture->left;
    LONG Height = Capture->bottom - Capture->top;

    /* Inset the unused interior for bilinear neighbors. The horizontal
     * result must also supply the vertical kernel along every glass edge. */
    Vertical->left = (LONG)(((LONGLONG)(Excluded->left - Capture->left) * FilterWidth + Width - 1) / Width) + 1;
    Vertical->right = (LONG)((LONGLONG)(Excluded->right - Capture->left) * FilterWidth / Width) - 1;
    Vertical->top = (LONG)(((LONGLONG)(Capture->bottom - Excluded->bottom) * FilterHeight + Height - 1) / Height) + 1;
    Vertical->bottom = (LONG)((LONGLONG)(Capture->bottom - Excluded->top) * FilterHeight / Height) - 1;
    Horizontal->left = Vertical->left + 1;
    Horizontal->right = Vertical->right - 1;
    Horizontal->top = Vertical->top + (LONG)KernelRadius + 1;
    Horizontal->bottom = Vertical->bottom - (LONG)KernelRadius - 1;
}

static void
DwmGpuDrawFilter(const RECT *Clip, const RECT *Excluded)
{
    RECT Hole, Parts[4];
    ULONG Index;

    Hole.left = max(Clip->left, Excluded->left);
    Hole.top = max(Clip->top, Excluded->top);
    Hole.right = min(Clip->right, Excluded->right);
    Hole.bottom = min(Clip->bottom, Excluded->bottom);
    if (Hole.left >= Hole.right || Hole.top >= Hole.bottom)
    {
        glScissor(Clip->left, Clip->top, Clip->right - Clip->left, Clip->bottom - Clip->top);
        DwmGpuDrawQuad();
        return;
    }
    SetRect(&Parts[0], Clip->left, Clip->top, Clip->right, Hole.top);
    SetRect(&Parts[1], Clip->left, Hole.bottom, Clip->right, Clip->bottom);
    SetRect(&Parts[2], Clip->left, Hole.top, Hole.left, Hole.bottom);
    SetRect(&Parts[3], Hole.right, Hole.top, Clip->right, Hole.bottom);
    for (Index = 0; Index < ARRAYSIZE(Parts); ++Index)
    {
        const RECT *Part = &Parts[Index];

        if (Part->left < Part->right && Part->top < Part->bottom)
        {
            glScissor(Part->left, Part->top, Part->right - Part->left, Part->bottom - Part->top);
            DwmGpuDrawQuad();
        }
    }
}

static void
DwmGpuComposeUnpinBlurResults(void)
{
    ULONG Index;

    for (Index = 0; Index < DWM_GPU_BLUR_TARGET_COUNT; ++Index)
        g_composeBlurTargets[Index].Pinned = FALSE;
    RtlZeroMemory(g_composeCachedCapture, sizeof(g_composeCachedCapture));
}

static void
DwmGpuComposePinBlurResults(const RECT *Repair)
{
    ULONG Index, Slot, Pinned = 0;

    DwmGpuComposeUnpinBlurResults();
    /* Keep one working target for misses and uncached secondary captures.
     * They must never evict pixels excluded from this frame's repair area. */
    for (Index = 0; Index < g_composeScene.Count && Pinned < DWM_GPU_BLUR_TARGET_COUNT - 1; ++Index)
    {
        const DWM_WIN *Window = &g_composeScene.Windows[Index];
        BOOL Glass = Window->BackdropType == DWM_BACKDROP_TRANSIENT &&
                     Window->BackdropRegion != 0 && Window->BackdropOpacity < 255;
        BOOL Blur = (Window->BlurFlags & DWM_BLUR_ENABLE) &&
                    ((Window->BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW) || Window->BlurRectCount != 0);
        ULONG Radius = Glass ? DwmGpuMaterialBlurRadius(Window) : g_composeBlurRadius;
        DWM_WIN Owner;

        if (!g_composeLowerUnchanged[Index] || Window->AnimFlags != 0 ||
            Window->cx <= 0 || Window->cy <= 0 ||
            ((Window->LayerFlags & DWM_LWA_ALPHA) && Window->Alpha == 0))
            continue;
        /* A later capture includes already drawn content. Only a window
         * with a single capture can omit its entire backdrop dependency. */
        if ((!Glass && !Blur) || (Glass && Blur) ||
            (Glass && Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW &&
             Window->DxGlobalShare != 0 && Window->DxUpdateId != 0))
            continue;
        Owner = DwmGpuCacheBlurOwner(Window);
        for (Slot = 0; Slot < DWM_GPU_BLUR_TARGET_COUNT; ++Slot)
        {
            DWM_GPU_BLUR_TARGET *Target = &g_composeBlurTargets[Slot];
            LONG Width = Target->Rect.right - Target->Rect.left;
            LONG Height = Target->Rect.bottom - Target->Rect.top;
            BOOL Downsample = Radius >= 8 && Width >= 2 && Height >= 2;
            RECT Output, Excluded = {0};

            if (Glass)
                DwmGpuMaterialClientRect(&Excluded, Window, (LONGLONG)Window->x - g_composeScene.Space.OriginX,
                                        (LONGLONG)Window->y - g_composeScene.Space.OriginY, Window->cx, Window->cy);

            if (Target->Pinned || !Target->ResultValid ||
                Target->ResultFrame != g_composeFrame - 1 || Target->Radius != Radius ||
                memcmp(&Target->Owner, &Owner, sizeof(Owner)) != 0 ||
                !DwmGpuDamageIntersects(&Target->Rect, Repair) ||
                !DwmGpuBlurOutputBounds(&Output, &Target->Rect,
                    (LONGLONG)Window->x - g_composeScene.Space.OriginX,
                    (LONGLONG)Window->y - g_composeScene.Space.OriginY, Window->cx, Window->cy) ||
                !DwmGpuBlurOutputCovers(Target, &Output, &Excluded) ||
                !DwmGpuBlurTargetMatches(Target, Width, Height,
                    Downsample ? (Width + 1) / 2 : Width, Downsample ? (Height + 1) / 2 : Height,
                    Downsample, Downsample && g_composeBlitFramebuffer == NULL, Radius != 0))
                continue;
            /* Matching ownership and continuous lower-scene validity also
             * preserve the capture geometry and explicit region contents. */
            Target->Pinned = TRUE;
            g_composeCachedCapture[Index] = TRUE;
            ++Pinned;
            break;
        }
    }
}

static void
DwmGpuComposeAdvanceBlurCache(void)
{
    ULONG Index;

    if (!g_composeBlurOwnerValid || !g_composeBlurLowerUnchanged)
        return;
    /* Damage may skip drawing this window for several frames. Carry the
     * dependency check forward even then, so an unchanged backdrop survives
     * until the window next needs drawing. Failed presents invalidate the
     * scene and prevent the next frame from extending this validity. */
    for (Index = 0; Index < DWM_GPU_BLUR_TARGET_COUNT; ++Index)
    {
        DWM_GPU_BLUR_TARGET *Target = &g_composeBlurTargets[Index];

        if (Target->ResultValid && Target->ResultFrame == g_composeFrame - 1 &&
            memcmp(&Target->Owner, &g_composeBlurOwner, sizeof(Target->Owner)) == 0)
            Target->ResultFrame = g_composeFrame;
    }
}

static void
DwmGpuComposeDeleteBlurTarget(DWM_GPU_BLUR_TARGET *Target)
{
    if (Target->Texture != 0)
        glDeleteTextures(1, &Target->Texture);
    if (Target->Pass != 0)
        glDeleteTextures(1, &Target->Pass);
    if (Target->Capture != 0)
        glDeleteTextures(1, &Target->Capture);
    RtlZeroMemory(Target, sizeof(*Target));
}

static void
DwmGpuComposePruneBlurTargets(const DWM_WIN *Windows, ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < DWM_GPU_BLUR_TARGET_COUNT; ++Index)
    {
        DWM_GPU_BLUR_TARGET *Target = &g_composeBlurTargets[Index];

        if (Target->Texture != 0 &&
            !DwmGpuSceneHasSurface(Windows, Count, Target->Owner.SurfaceId, FALSE))
        {
            if (g_composeBlurTarget == Target)
            {
                g_composeBlurTarget = NULL;
                g_composeBlurTex = g_composeBlurPass = g_composeBlurCapture = 0;
            }
            DwmGpuComposeDeleteBlurTarget(Target);
        }
    }
}

/* GL names belong to their context. Never reuse them after a fallback and
 * subsequent compositor initialization. Called with that context current. */
static void
DwmGpuComposeReleaseBlur(void)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(g_composeBlurPrograms); ++Index)
    {
        if (g_composeBlurPrograms[Index] != 0)
            pglDeleteProgram(g_composeBlurPrograms[Index]);
        g_composeBlurPrograms[Index] = 0;
    }
    if (g_composeFbo != 0)
        pglDeleteFramebuffers(1, &g_composeFbo);
    {
        ULONG Index;

        for (Index = 0; Index < DWM_GPU_BLUR_TARGET_COUNT; ++Index)
            DwmGpuComposeDeleteBlurTarget(&g_composeBlurTargets[Index]);
    }
    g_composeBlurProgram = g_composeFbo = 0;
    g_composeBlurKernelRadius = 0;
    g_composeBlurTex = g_composeBlurPass = g_composeBlurCapture = 0;
    g_composeBlurTargetUse = 0;
    g_composeBlurTarget = NULL;
}

static BOOL
DwmGpuComposeBuildBlur(ULONG Taps)
{
    GLuint Vertex;
    GLuint Fragment;
    GLuint Program;
    GLint Status = 0;
    char Prefix[64];

    if (Taps == 0 || Taps >= ARRAYSIZE(g_composeBlurPrograms))
        return FALSE;
    Program = g_composeBlurPrograms[Taps];
    if (Program != 0 && Program == g_composeBlurProgram)
        return TRUE;
    if (!DwmGpuLoadEntryPoints())
        return FALSE;
    if (Program != 0)
        goto SelectProgram;

    Vertex = DwmGpuCompile(GL_VERTEX_SHADER, DwmGpuBlurVertexSource);
    if (Vertex == 0)
        return FALSE;
    /* The radius fixes the number of samples before drawing. Specializing
     * that count removes unused taps and per-fragment uniform branches. */
    _snprintf(Prefix, sizeof(Prefix), "#define DWM_BLUR_TAPS %lu\n", Taps);
    Fragment = DwmGpuCompilePrefixed(GL_FRAGMENT_SHADER, DwmGpuBlurFragmentSource, Prefix);
    if (Fragment == 0)
    {
        pglDeleteShader(Vertex);
        return FALSE;
    }
    Program = pglCreateProgram();
    if (Program == 0)
    {
        pglDeleteShader(Fragment);
        pglDeleteShader(Vertex);
        return FALSE;
    }
    pglAttachShader(Program, Vertex);
    pglAttachShader(Program, Fragment);
    pglLinkProgram(Program);
    pglGetProgramiv(Program, GL_LINK_STATUS, &Status);
    pglDeleteShader(Fragment);
    pglDeleteShader(Vertex);
    if (Status == GL_FALSE)
    {
        pglDeleteProgram(Program);
        return FALSE;
    }
    g_composeBlurPrograms[Taps] = Program;

SelectProgram:
    g_composeBlurProgram = Program;
    g_composeBlurKernelRadius = 0;
    g_composeBlurSourceLoc =
        pglGetUniformLocation(g_composeBlurProgram, "uSource");
    g_composeBlurStepLoc =
        pglGetUniformLocation(g_composeBlurProgram, "uStep");
    g_composeBlurWeightsLoc =
        pglGetUniformLocation(g_composeBlurProgram, "uWeights");
    g_composeBlurOffsetsLoc =
        pglGetUniformLocation(g_composeBlurProgram, "uOffsets");
    if (g_composeBlurStepLoc < 0 ||
        g_composeBlurWeightsLoc < 0 || g_composeBlurOffsetsLoc < 0)
    {
        DwmGpuComposeReleaseBlur();
        return FALSE;
    }

    if (g_composeFbo == 0)
        pglGenFramebuffers(1, &g_composeFbo);
    if (g_composeFbo == 0)
        DwmGpuComposeReleaseBlur();
    return g_composeFbo != 0;
}

/* Four LRU entries bound GPU storage. Finished pixels can additionally be
 * reused when their exact owner/capture and the lower scene are unchanged. */
static BOOL
DwmGpuComposeEnsureBlurTargets(LONG Width, LONG Height,
                              LONG FilterWidth, LONG FilterHeight,
                              BOOL Downsample, BOOL Cacheable,
                              const RECT *Rect, ULONG Radius, const RECT *Output, const RECT *Excluded)
{
    DWM_GPU_BLUR_TARGET *Target = NULL;
    DWM_GPU_BLUR_TARGET *Reusable = NULL;
    DWM_GPU_BLUR_TARGET *Oldest = NULL;
    ULONG Index;
    BOOL NeedCapture = Downsample && g_composeBlitFramebuffer == NULL;

    for (Index = 0; Index < DWM_GPU_BLUR_TARGET_COUNT; ++Index)
    {
        DWM_GPU_BLUR_TARGET *Current = &g_composeBlurTargets[Index];

        if (Current->Pinned &&
            (!Cacheable || !g_composeBlurLowerUnchanged || !Current->ResultValid ||
             Current->ResultFrame != g_composeFrame || Current->Radius != Radius ||
             !EqualRect(&Current->Rect, Rect) ||
             !DwmGpuBlurOutputCovers(Current, Output, Excluded) ||
             memcmp(&Current->Owner, &g_composeBlurOwner, sizeof(Current->Owner)) != 0))
            continue;
        if (DwmGpuBlurTargetMatches(Current, Width, Height, FilterWidth, FilterHeight, Downsample, NeedCapture, Radius != 0))
        {
            /* Equal geometry does not identify equal pixels. Preserve each
             * owner's result before reusing scratch storage or evicting LRU. */
            if (Cacheable && Current->ResultValid &&
                memcmp(&Current->Owner, &g_composeBlurOwner, sizeof(Current->Owner)) == 0)
            {
                if (Target == NULL || Current->Pinned)
                    Target = Current;
                if (Current->Pinned)
                    break;
            }
            if (!Current->ResultValid && (Reusable == NULL || Current->LastUse < Reusable->LastUse))
                Reusable = Current;
        }
        if (!Current->Pinned && (Oldest == NULL || Current->LastUse < Oldest->LastUse))
            Oldest = Current;
    }
    if (Target == NULL)
        Target = Reusable != NULL ? Reusable : Oldest;
    if (Target == NULL)
        return FALSE;
    if (!DwmGpuBlurTargetMatches(Target, Width, Height, FilterWidth, FilterHeight, Downsample, NeedCapture, Radius != 0))
    {
        g_composeBlurTex = g_composeBlurPass = g_composeBlurCapture = 0;
        DwmGpuComposeDeleteBlurTarget(Target);
        if (!DwmGpuMakeTexture(&Target->Texture, FilterWidth, FilterHeight) ||
            (Radius != 0 && !DwmGpuMakeTexture(&Target->Pass, FilterWidth, FilterHeight)) ||
            (NeedCapture &&
             !DwmGpuMakeTexture(&Target->Capture, Width, Height)))
        {
            DwmGpuComposeDeleteBlurTarget(Target);
            return FALSE;
        }
        Target->Width = Width;
        Target->Height = Height;
        Target->FilterWidth = FilterWidth;
        Target->FilterHeight = FilterHeight;
        Target->Downsample = Downsample;
    }
    Target->LastUse = ++g_composeBlurTargetUse;
    g_composeBlurTex = Target->Texture;
    g_composeBlurPass = Target->Pass;
    g_composeBlurCapture = Target->Capture;
    g_composeBlurTarget = Target;
    return TRUE;
}

static BOOL
DwmGpuComposeFilterRectMeasured(const RECT *Rect, ULONG Radius, BOOL Restore, const RECT *Output, const RECT *Excluded)
{
    GLfloat Weights[DWM_GPU_MAX_RADIUS + 1];
    GLfloat Offsets[DWM_GPU_MAX_RADIUS + 1];
    ULONG Taps;
    LONG Width;
    LONG Height;
    LONG FilterWidth;
    LONG FilterHeight;
    BOOL Downsample;
    BOOL Result = FALSE;
    BOOL Cacheable = g_composeBlurOwnerValid && g_composeBlurCall++ == 0;
    DWM_GPU_BLUR_TARGET *Target;
    ULONG KernelRadius;
    RECT RequiredOutput, RequiredExcluded = {0}, Horizontal, Vertical;
    RECT HorizontalExcluded = {0}, VerticalExcluded = {0};
    BOOL ClipOutput;

    if (!g_composeActive || Rect == NULL ||
        Radius > DWM_GPU_MAX_RADIUS)
    {
        return FALSE;
    }

    Width = Rect->right - Rect->left;
    Height = Rect->bottom - Rect->top;
    if (Width <= 0 || Height <= 0 ||
        Rect->left < 0 || Rect->top < 0 ||
        Rect->right > g_composeWidth || Rect->bottom > g_composeHeight)
    {
        return FALSE;
    }
    RequiredOutput = *Rect;
    if (Output != NULL && !Restore)
    {
        RequiredOutput.left = max(Rect->left, Output->left);
        RequiredOutput.top = max(Rect->top, Output->top);
        RequiredOutput.right = min(Rect->right, Output->right);
        RequiredOutput.bottom = min(Rect->bottom, Output->bottom);
        if (RequiredOutput.left >= RequiredOutput.right || RequiredOutput.top >= RequiredOutput.bottom)
            return FALSE;
    }
    if (Excluded != NULL && !Restore)
    {
        RequiredExcluded.left = max(RequiredOutput.left, Excluded->left);
        RequiredExcluded.top = max(RequiredOutput.top, Excluded->top);
        RequiredExcluded.right = min(RequiredOutput.right, Excluded->right);
        RequiredExcluded.bottom = min(RequiredOutput.bottom, Excluded->bottom);
        if (RequiredExcluded.left >= RequiredExcluded.right || RequiredExcluded.top >= RequiredExcluded.bottom)
            RtlZeroMemory(&RequiredExcluded, sizeof(RequiredExcluded));
    }
    ClipOutput = !EqualRect(&RequiredOutput, Rect);
    Downsample = Radius >= 8 && Width >= 2 && Height >= 2;
    FilterWidth = Downsample ? (Width + 1) / 2 : Width;
    FilterHeight = Downsample ? (Height + 1) / 2 : Height;
    KernelRadius = Downsample ? (Radius + 1) / 2 : Radius;
    Taps = 1 + (KernelRadius + 1) / 2;
    if ((Radius != 0 && !DwmGpuComposeBuildBlur(Taps)) ||
        !DwmGpuComposeEnsureBlurTargets(Width, Height, FilterWidth,
                                         FilterHeight, Downsample, Cacheable, Rect, Radius, &RequiredOutput, &RequiredExcluded))
    {
        return FALSE;
    }

    Target = g_composeBlurTarget;
    if (Cacheable && g_composeBlurLowerUnchanged && Target->ResultValid &&
        Target->ResultFrame == g_composeFrame && Target->Radius == Radius &&
        EqualRect(&Target->Rect, Rect) &&
        DwmGpuBlurOutputCovers(Target, &RequiredOutput, &RequiredExcluded) &&
        memcmp(&Target->Owner, &g_composeBlurOwner, sizeof(Target->Owner)) == 0)
    {
        DptCount(&g_DwmPresentTrace, DPT_BLUR_HIT, (ULONGLONG)Width * Height * 4);
        ++g_composeReused;
        Target->ResultFrame = g_composeFrame;
        Result = TRUE;
        goto Done;
    }
    Target->ResultValid = FALSE;
    /* Scratch from animated or client-layer captures still belongs to this
     * window, even when its pixels cannot be reused on the next frame. */
    Target->Owner = g_composeBlurOwner;
    if (Radius != 0)
    {
        DptCount(&g_DwmPresentTrace, DPT_BLUR_FILTER, (ULONGLONG)Width * Height * 4);
        ++g_composeFiltered;
    }

    pglActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
    /* Screen-space damage cannot clip a filter target at its own origin. */
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, FilterWidth, FilterHeight);
    if (Downsample && g_composeBlitFramebuffer != NULL)
    {
        /* Capture directly at the filter size using the same bilinear
         * reduction, avoiding a full-size texture and its extra copy pass. */
        pglBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        pglBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_composeFbo);
        pglFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_composeBlurTex, 0);
        if (pglCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            goto Done;
        g_composeBlitFramebuffer(Rect->left, g_composeHeight - Rect->bottom, Rect->right, g_composeHeight - Rect->top, 0, 0, FilterWidth, FilterHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        if (glGetError() != GL_NO_ERROR)
            goto Done;
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, Downsample ? g_composeBlurCapture : g_composeBlurTex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Rect->left, g_composeHeight - Rect->bottom, Width, Height);
        if (glGetError() != GL_NO_ERROR)
            goto Done;
        if (Downsample)
        {
            /* Texture coordinates retain the complete capture for odd sizes. */
            pglBindFramebuffer(GL_FRAMEBUFFER, g_composeFbo);
            pglUseProgram(0);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_composeBlurTex, 0);
            if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                goto Done;
            DwmGpuDrawQuad();
        }
    }
    /* A disabled blur keeps the material's tint and transparency, sampling
     * a full-resolution GPU capture without either Gaussian pass. */
    if (Radius == 0)
    {
        Result = TRUE;
        goto CacheResult;
    }
    pglUseProgram(g_composeBlurProgram);
    if (g_composeBlurKernelRadius != KernelRadius)
    {
        DwmGpuBuildWeights(KernelRadius, Weights, Offsets);
        if (g_composeBlurSourceLoc >= 0)
            pglUniform1i(g_composeBlurSourceLoc, 0);
        pglUniform1fv(g_composeBlurWeightsLoc, (GLsizei)Taps, Weights);
        pglUniform1fv(g_composeBlurOffsetsLoc, (GLsizei)Taps, Offsets);
        g_composeBlurKernelRadius = KernelRadius;
    }
    pglActiveTexture(GL_TEXTURE0);

    pglBindFramebuffer(GL_FRAMEBUFFER, g_composeFbo);
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, g_composeBlurPass, 0);
    if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        goto Done;
    glBindTexture(GL_TEXTURE_2D, g_composeBlurTex);
    pglUniform2f(g_composeBlurStepLoc, 1.0f / (GLfloat)FilterWidth, 0.0f);
    SetRect(&Horizontal, 0, 0, FilterWidth, FilterHeight);
    Vertical = Horizontal;
    if (ClipOutput)
        DwmGpuBlurFilterClips(Rect, &RequiredOutput, FilterWidth, FilterHeight, KernelRadius, &Horizontal, &Vertical);
    if (RequiredExcluded.left < RequiredExcluded.right && RequiredExcluded.top < RequiredExcluded.bottom)
        DwmGpuBlurFilterExclusions(Rect, &RequiredExcluded, FilterWidth, FilterHeight, KernelRadius, &HorizontalExcluded, &VerticalExcluded);
    glEnable(GL_SCISSOR_TEST);
    DwmGpuDrawFilter(&Horizontal, &HorizontalExcluded);

    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, g_composeBlurTex, 0);
    if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        goto Done;
    glBindTexture(GL_TEXTURE_2D, g_composeBlurPass);
    pglUniform2f(g_composeBlurStepLoc, 0.0f, 1.0f / (GLfloat)FilterHeight);
    DwmGpuDrawFilter(&Vertical, &VerticalExcluded);
    Result = glGetError() == GL_NO_ERROR;
CacheResult:
    if (Result && Cacheable)
    {
        Target->Rect = *Rect;
        Target->Output = RequiredOutput;
        Target->Excluded = RequiredExcluded;
        Target->Radius = Radius;
        Target->ResultFrame = g_composeFrame;
        Target->ResultValid = TRUE;
    }

Done:
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    pglUseProgram(0);
    glViewport(0, 0, g_composeWidth, g_composeHeight);
    glEnable(GL_SCISSOR_TEST);
    glScissor(g_composeDamage.Draw.left, g_composeHeight - g_composeDamage.Draw.bottom,
              g_composeDamage.Draw.right - g_composeDamage.Draw.left,
              g_composeDamage.Draw.bottom - g_composeDamage.Draw.top);
    if (Result && Restore)
    {
        /* Put the blurred region back where it came from. */
        glBindTexture(GL_TEXTURE_2D, g_composeBlurTex);
        /* glCopyTexSubImage2D and the filter targets use bottom-up rows;
         * window uploads use top-down rows. Restore the framebuffer with
         * its own orientation rather than vertically mirroring the glass. */
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        DwmGpuComposeQuadOriented(Rect->left, Rect->top,
                                  Rect->right, Rect->bottom, 1.0f, TRUE);
        Result = glGetError() == GL_NO_ERROR;
    }
    glEnable(GL_BLEND);
    return Result;
}

static BOOL
DwmGpuComposeFilterRegion(const RECT *Rect, ULONG Radius, BOOL Restore, const RECT *Output, const RECT *Excluded)
{
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_BLUR);
    BOOL Result = DwmGpuComposeFilterRectMeasured(Rect, Radius, Restore, Output, Excluded);
    DptEnd(&g_DwmPresentTrace, Trace, Result, 0);
    return Result;
}

static BOOL
DwmGpuComposeFilterRect(const RECT *Rect, ULONG Radius, BOOL Restore)
{
    return DwmGpuComposeFilterRegion(Rect, Radius, Restore, NULL, NULL);
}

BOOL
DwmGpuComposeBlurRect(const RECT *Rect, ULONG Radius)
{
    if (DwmD3dIsActive())
    {
        DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_BLUR);
        BOOL Result = DwmD3dBlurRect(Rect, Radius);
        DptEnd(&g_DwmPresentTrace, Trace, Result, 0);
        return Result;
    }
    return DwmGpuComposeFilterRect(Rect, Radius, TRUE);
}

/* Translate in wide arithmetic: a window or region can be partly off-screen.
 * Kernel regions are canonical non-overlapping rectangles in window space. */
static BOOL
DwmGpuClipWindowBlurRect(const DWM_WIN *Window, const RECTL *Region,
                         LONG OriginX, LONG OriginY, RECT *Out)
{
    DWM_GPU_WINDOW_GEOMETRY Geometry;
    LONGLONG Left, Top, Right, Bottom;

    if (!DwmGpuWindowGeometry(Window, OriginX, OriginY, &Geometry))
        return FALSE;
    Left = Geometry.Left + DwmGpuScaleWindowEdge(max(Region->left, 0), Window->cx, Geometry.Width);
    Top = Geometry.Top + DwmGpuScaleWindowEdge(max(Region->top, 0), Window->cy, Geometry.Height);
    Right = Geometry.Left + DwmGpuScaleWindowEdge(min(Region->right, Window->cx), Window->cx, Geometry.Width);
    Bottom = Geometry.Top + DwmGpuScaleWindowEdge(min(Region->bottom, Window->cy), Window->cy, Geometry.Height);

    Left = max(Left, 0);
    Top = max(Top, 0);
    Right = min(Right, g_composeWidth);
    Bottom = min(Bottom, g_composeHeight);
    if (Right <= Left || Bottom <= Top)
        return FALSE;
    SetRect(Out, (LONG)Left, (LONG)Top, (LONG)Right, (LONG)Bottom);
    return TRUE;
}

BOOL
DwmGpuComposeBlurWindow(const DWM_WIN *Window, const RECTL *Rectangles,
                        LONG OriginX, LONG OriginY, ULONG Radius)
{
    if (DwmD3dIsActive())
    {
        DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_BLUR);
        BOOL Result = DwmD3dBlurWindow(Window, Rectangles, OriginX, OriginY, Radius);
        DptEnd(&g_DwmPresentTrace, Trace, Result, 0);
        return Result;
    }
    RECTL Entire = {0, 0, Window->cx, Window->cy};
    RECT Capture = {0, 0, 0, 0}, Region, Output;
    DWM_GPU_WINDOW_GEOMETRY Geometry;
    ULONG Count = Window->BlurRectCount, Index;
    BOOL HaveRegion = FALSE;
    GLfloat Alpha = (Window->LayerFlags & DWM_LWA_ALPHA) ?
                       Window->Alpha / 255.0f : 1.0f;

    if (!(Window->BlurFlags & DWM_BLUR_ENABLE) ||
        !DwmGpuWindowGeometry(Window, OriginX, OriginY, &Geometry) || Alpha == 0.0f)
        return TRUE;
    if (Window->BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW)
    {
        Rectangles = &Entire;
        Count = 1;
    }
    if (Count > DWM_MAX_BLUR_RECTS || (Count != 0 && Rectangles == NULL))
        return FALSE;
    for (Index = 0; Index < Count; ++Index)
    {
        if (!DwmGpuClipWindowBlurRect(Window, &Rectangles[Index],
                                      OriginX, OriginY, &Region))
            continue;
        if (!HaveRegion)
            Capture = Region;
        else
        {
            Capture.left = min(Capture.left, Region.left);
            Capture.top = min(Capture.top, Region.top);
            Capture.right = max(Capture.right, Region.right);
            Capture.bottom = max(Capture.bottom, Region.bottom);
        }
        HaveRegion = TRUE;
    }
    if (!HaveRegion)
        return TRUE;
    if (!DwmGpuDamageIntersects(&Capture, &g_composeDamage.Draw))
        return TRUE;
    if (Radius == 0 || Radius > DWM_GPU_MAX_RADIUS)
        return FALSE;
    Capture.left = max(0, Capture.left - (LONG)Radius);
    Capture.top = max(0, Capture.top - (LONG)Radius);
    Capture.right += min((LONG)Radius, g_composeWidth - Capture.right);
    Capture.bottom += min((LONG)Radius, g_composeHeight - Capture.bottom);
    /* Filter once from the original lower scene. Repeatedly filtering and
     * restoring individual region rectangles would contaminate neighbors. */
    if (!DwmGpuBlurOutputBounds(&Output, &Capture,
            Geometry.Left, Geometry.Top, Geometry.Width, Geometry.Height) ||
        !DwmGpuComposeFilterRegion(&Capture, Radius, FALSE, &Output, NULL))
        return FALSE;
    glBindTexture(GL_TEXTURE_2D, g_composeBlurTex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    for (Index = 0; Index < Count; ++Index)
    {
        GLfloat U0, U1, V0, V1, Left, Top, Right, Bottom;

        if (!DwmGpuClipWindowBlurRect(Window, &Rectangles[Index],
                                      OriginX, OriginY, &Region))
            continue;
        U0 = (GLfloat)(Region.left - Capture.left) / (Capture.right - Capture.left);
        U1 = (GLfloat)(Region.right - Capture.left) / (Capture.right - Capture.left);
        V0 = (GLfloat)(Capture.bottom - Region.top) / (Capture.bottom - Capture.top);
        V1 = (GLfloat)(Capture.bottom - Region.bottom) / (Capture.bottom - Capture.top);
        Left = 2.0f * Region.left / g_composeWidth - 1.0f;
        Right = 2.0f * Region.right / g_composeWidth - 1.0f;
        Top = 1.0f - 2.0f * Region.top / g_composeHeight;
        Bottom = 1.0f - 2.0f * Region.bottom / g_composeHeight;
        glBegin(GL_QUADS);
        glTexCoord2f(U0, V0); glVertex2f(Left, Top);
        glTexCoord2f(U1, V0); glVertex2f(Right, Top);
        glTexCoord2f(U1, V1); glVertex2f(Right, Bottom);
        glTexCoord2f(U0, V1); glVertex2f(Left, Bottom);
        glEnd();
    }
    return glGetError() == GL_NO_ERROR;
}

static void
DwmGpuMaterialColor(GLint Location, ULONG Color)
{
    pglUniform3f(Location,
                  (Color & 255) / 255.0f,
                  ((Color >> 8) & 255) / 255.0f,
                  ((Color >> 16) & 255) / 255.0f);
}

static BOOL
DwmGpuComposeBuildMaterial(DWM_GPU_MATERIAL_PROGRAM *Shader, BOOL Interior)
{
    GLuint Vertex, Fragment;
    GLint Linked = GL_FALSE;

    if (Shader->Program == 0)
    {
        if (!DwmGpuLoadEntryPoints())
            return FALSE;
        Vertex = DwmGpuCompile(GL_VERTEX_SHADER, DwmGpuBlurVertexSource);
        Fragment = DwmGpuCompilePrefixed(GL_FRAGMENT_SHADER, DwmGpuMaterialSource,
                                         Interior ? "#define DWM_MATERIAL_INTERIOR\n" : NULL);
        if (Vertex != 0 && Fragment != 0)
        {
            Shader->Program = pglCreateProgram();
            if (Shader->Program != 0)
            {
                pglAttachShader(Shader->Program, Vertex);
                pglAttachShader(Shader->Program, Fragment);
                pglLinkProgram(Shader->Program);
                pglGetProgramiv(Shader->Program, GL_LINK_STATUS, &Linked);
            }
        }
        if (Vertex != 0) pglDeleteShader(Vertex);
        if (Fragment != 0) pglDeleteShader(Fragment);
        if (!Linked)
        {
            if (Shader->Program != 0) pglDeleteProgram(Shader->Program);
            Shader->Program = 0;
            return FALSE;
        }
#define MAT_CACHE(Name) \
        Shader->Name = pglGetUniformLocation(Shader->Program, "u" #Name)
        MAT_CACHE(Window);
        MAT_CACHE(Backdrop);
        MAT_CACHE(Glass);
        MAT_CACHE(Whole);
        MAT_CACHE(PixelAlpha);
        MAT_CACHE(UseKey);
        MAT_CACHE(Opacity);
        MAT_CACHE(Alpha);
        MAT_CACHE(Radius);
        MAT_CACHE(Saturation);
        MAT_CACHE(Reflection);
        MAT_CACHE(Size);
        MAT_CACHE(ScreenSize);
        MAT_CACHE(ClientMin);
        MAT_CACHE(ClientMax);
        MAT_CACHE(CaptureOrigin);
        MAT_CACHE(CaptureSize);
        MAT_CACHE(Brush);
        MAT_CACHE(Colorization);
        MAT_CACHE(Key);
#undef MAT_CACHE
    }
    return TRUE;
}

/* Scissor the original quad so interpolation is identical in every part.
 * The inset is in source pixels, rounded inward after destination scaling.
 * Its extra pixel keeps every interior sample inside the flat coverage. */
static ULONG
DwmGpuMaterialParts(const DWM_WIN *Window, LONGLONG Left, LONGLONG Top,
                     LONG Width, LONG Height, RECT *Parts)
{
    RECT Bounds, Inner;
    ULONG Radius = min(Window->CornerRadius, (ULONG)min(Window->cx / 2, Window->cy / 2));
    LONGLONG InsetX, InsetY;

    if (!DwmGpuDamageBounds(&Bounds, g_composeWidth, g_composeHeight, Left, Top, Left + Width, Top + Height))
        return 0;
    Bounds.left = max(Bounds.left, g_composeDamage.Draw.left);
    Bounds.top = max(Bounds.top, g_composeDamage.Draw.top);
    Bounds.right = min(Bounds.right, g_composeDamage.Draw.right);
    Bounds.bottom = min(Bounds.bottom, g_composeDamage.Draw.bottom);
    if (Bounds.left >= Bounds.right || Bounds.top >= Bounds.bottom)
        return 0;
    Parts[0] = Bounds;
    if (Radius == 0)
        return 1;
    InsetX = ((LONGLONG)(Radius + 1) * Width + Window->cx - 1) / Window->cx;
    InsetY = ((LONGLONG)(Radius + 1) * Height + Window->cy - 1) / Window->cy;
    if (!DwmGpuDamageBounds(&Inner, g_composeWidth, g_composeHeight,
                            Left + InsetX, Top + InsetY, Left + Width - InsetX, Top + Height - InsetY))
        return 1;
    Inner.left = max(Inner.left, Bounds.left);
    Inner.top = max(Inner.top, Bounds.top);
    Inner.right = min(Inner.right, Bounds.right);
    Inner.bottom = min(Inner.bottom, Bounds.bottom);
    if (Inner.left >= Inner.right || Inner.top >= Inner.bottom)
        return 1;
    Parts[0] = Inner;
    SetRect(&Parts[1], Bounds.left, Bounds.top, Bounds.right, Inner.top);
    SetRect(&Parts[2], Bounds.left, Inner.bottom, Bounds.right, Bounds.bottom);
    SetRect(&Parts[3], Bounds.left, Inner.top, Inner.left, Inner.bottom);
    SetRect(&Parts[4], Inner.right, Inner.top, Bounds.right, Inner.bottom);
    return 5;
}

static BOOL
DwmGpuComposeMaterial(const DWM_WIN *Window, GLuint Texture,
                       LONGLONG Left, LONGLONG Top, LONG Width, LONG Height, GLfloat Alpha,
                       const RECT *Cover, const RECT *SceneCover)
{
    BOOL Glass = Window->BackdropType == DWM_BACKDROP_TRANSIENT &&
                 Window->BackdropRegion != 0 && Window->BackdropOpacity < 255;
    LONG Radius = DwmGpuMaterialBlurRadius(Window);
    RECT Capture, Output, Excluded, Parts[5];
    ULONG Count, Index;
    DWM_GPU_MATERIAL_PROGRAM *Shader, *Previous = NULL;

    Count = DwmGpuMaterialParts(Window, Left, Top, Width, Height, Parts);
    if (Count == 0)
        return TRUE;
    if (!DwmGpuComposeBuildMaterial(&g_materialPrograms[0], FALSE) ||
        (Count == 5 && !DwmGpuComposeBuildMaterial(&g_materialPrograms[1], TRUE)))
        return FALSE;

    /* Capture the already-composed lower windows with a sampling margin.
     * Filtering does not write back: the material shader samples the result
     * only within glass pixels, preserving corners and opaque foreground. */
    Capture.left = max(0, Left - Radius);
    Capture.top = max(0, Top - Radius);
    Capture.right = min(g_composeWidth, Left + Width + Radius);
    Capture.bottom = min(g_composeHeight, Top + Height + Radius);
    if (Capture.right <= Capture.left || Capture.bottom <= Capture.top)
        return TRUE;
    pglActiveTexture(GL_TEXTURE0);
    DwmGpuMaterialClientRect(&Excluded, Window, Left, Top, Width, Height);
    if (Glass &&
        (!DwmGpuBlurOutputBounds(&Output, &Capture, Left, Top, Width, Height) ||
         !DwmGpuComposeFilterRegion(&Capture, Radius, FALSE, &Output, &Excluded)))
        return FALSE;

    pglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, Glass ? g_composeBlurTex : Texture);
    pglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Texture);
    for (Index = 0; Index < Count; ++Index)
    {
        BOOL Interior = Count == 5 && Index == 0;
        const RECT *Part = &Parts[Index];

        if (Part->left >= Part->right || Part->top >= Part->bottom)
            continue;
        Shader = &g_materialPrograms[Interior ? 1 : 0];
        if (Shader != Previous)
        {
            pglUseProgram(Shader->Program);
            pglUniform1i(Shader->Window, 0);
            pglUniform1i(Shader->Backdrop, 1);
            pglUniform1i(Shader->Glass, Glass);
            pglUniform1i(Shader->Whole, Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW);
            pglUniform1i(Shader->PixelAlpha, !!(Window->BlurFlags & DWM_BLUR_ENABLE));
            pglUniform1i(Shader->UseKey, !!(Window->LayerFlags & DWM_LWA_COLORKEY));
            pglUniform1f(Shader->Opacity, min(Window->BackdropOpacity, 255) / 255.0f);
            pglUniform1f(Shader->Alpha, Alpha);
            pglUniform1f(Shader->Radius, min(Window->CornerRadius,
                          (ULONG)min(Window->cx / 2, Window->cy / 2)));
            pglUniform1f(Shader->Saturation, (100.0f + DWM_MATERIAL_SATURATION) / 100.0f);
            pglUniform1f(Shader->Reflection, DWM_MATERIAL_REFLECT_STRENGTH / 255.0f);
            pglUniform2f(Shader->Size, Window->cx, Window->cy);
            pglUniform2f(Shader->ScreenSize, g_composeWidth, g_composeHeight);
            pglUniform2f(Shader->ClientMin,
                          min(Window->ClientX + (LONG)Window->BackdropNcExtendLeft,
                              Window->ClientX + Window->ClientWidth),
                          min(Window->ClientY + (LONG)Window->BackdropNcExtend,
                              Window->ClientY + Window->ClientHeight));
            pglUniform2f(Shader->ClientMax, Window->ClientX + Window->ClientWidth,
                          Window->ClientY + Window->ClientHeight);
            pglUniform2f(Shader->CaptureOrigin, Capture.left,
                          g_composeHeight - Capture.bottom);
            pglUniform2f(Shader->CaptureSize, Capture.right - Capture.left,
                          Capture.bottom - Capture.top);
            DwmGpuMaterialColor(Shader->Brush, Window->BackdropColor);
            DwmGpuMaterialColor(Shader->Colorization, Window->BackdropColorization);
            DwmGpuMaterialColor(Shader->Key, Window->ColorKey);
            Previous = Shader;
        }
        /* The glass shader already combines its backdrop into RGB. A fully
         * opaque interior therefore needs no destination blending. */
        if (Interior && Alpha >= 1.0f && !(Window->BlurFlags & DWM_BLUR_ENABLE))
            glDisable(GL_BLEND);
        else
            glEnable(GL_BLEND);
        glScissor(Part->left, g_composeHeight - Part->bottom,
                  Part->right - Part->left, Part->bottom - Part->top);
        DwmGpuComposeUncoveredQuad(Left, Top, Left + Width, Top + Height, Alpha, &Parts[Index], Cover, SceneCover);
    }
    glEnable(GL_BLEND);
    glScissor(g_composeDamage.Draw.left, g_composeHeight - g_composeDamage.Draw.bottom,
              g_composeDamage.Draw.right - g_composeDamage.Draw.left,
              g_composeDamage.Draw.bottom - g_composeDamage.Draw.top);
    pglUseProgram(0);
    pglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    pglActiveTexture(GL_TEXTURE0);
    return glGetError() == GL_NO_ERROR;
}

static BOOL
DwmGpuComposeShadowMeasured(const RECT *Bounds, LONGLONG X, LONGLONG Y,
                    LONG Width, LONG Height, LONG Offset, LONG WideExtent,
                    BOOL Active, ULONG WideOpacity, ULONG TightOpacity,
                    ULONG WindowAlpha)
{
    LONG InnerLeft, InnerTop, InnerRight, InnerBottom;
    GLuint Vertex, Fragment;
    GLint Linked = GL_FALSE;

    if (!g_composeActive || Width <= 0 || Height <= 0 || WideExtent <= 0)
        return FALSE;
    if (WindowAlpha == 0)
        return TRUE;
    if (!DwmGpuDamageIntersects(Bounds, &g_composeDamage.Draw))
        return TRUE;
    if (g_shadowProgram == 0)
    {
        Vertex = DwmGpuCompile(GL_VERTEX_SHADER, DwmGpuBlurVertexSource);
        Fragment = DwmGpuCompile(GL_FRAGMENT_SHADER, DwmGpuShadowSource);
        if (Vertex && Fragment)
        {
            g_shadowProgram = pglCreateProgram();
            if (g_shadowProgram)
            {
                pglAttachShader(g_shadowProgram, Vertex);
                pglAttachShader(g_shadowProgram, Fragment);
                pglLinkProgram(g_shadowProgram);
                pglGetProgramiv(g_shadowProgram, GL_LINK_STATUS, &Linked);
            }
        }
        if (Vertex) pglDeleteShader(Vertex);
        if (Fragment) pglDeleteShader(Fragment);
        if (!Linked)
        {
            if (g_shadowProgram) pglDeleteProgram(g_shadowProgram);
            g_shadowProgram = 0;
            return FALSE;
        }
#define SHADOW_LOCATION(Name) g_shadowLoc.Name = pglGetUniformLocation(g_shadowProgram, "u" #Name)
        SHADOW_LOCATION(Owner); SHADOW_LOCATION(Size); SHADOW_LOCATION(Sigma);
        SHADOW_LOCATION(Opacity); SHADOW_LOCATION(ScreenHeight);
        SHADOW_LOCATION(Offset); SHADOW_LOCATION(WindowAlpha);
#undef SHADOW_LOCATION
    }
    pglUseProgram(g_shadowProgram);
    pglUniform2f(g_shadowLoc.Owner, (GLfloat)X, (GLfloat)Y);
    pglUniform2f(g_shadowLoc.Size, Width, Height);
    pglUniform2f(g_shadowLoc.Sigma, WideExtent / 3.0f,
                WideExtent * (Active ? 2.0f : 1.0f) / 9.0f);
    pglUniform2f(g_shadowLoc.Opacity, WideOpacity, TightOpacity);
    pglUniform1f(g_shadowLoc.ScreenHeight, g_composeHeight);
    pglUniform1f(g_shadowLoc.Offset, Offset);
    pglUniform1f(g_shadowLoc.WindowAlpha, WindowAlpha);
    glEnable(GL_BLEND);
    InnerLeft = (LONG)max(Bounds->left, min((LONGLONG)Bounds->right, X));
    InnerRight = (LONG)max(Bounds->left, min((LONGLONG)Bounds->right, X + Width));
    InnerTop = (LONG)max(Bounds->top, min((LONGLONG)Bounds->bottom, Y));
    InnerBottom = (LONG)max(Bounds->top, min((LONGLONG)Bounds->bottom, Y + Height));
    if (Bounds->top < InnerTop)
        DwmGpuComposeQuad(Bounds->left, Bounds->top, Bounds->right, InnerTop, 1.0f);
    if (InnerBottom < Bounds->bottom)
        DwmGpuComposeQuad(Bounds->left, InnerBottom, Bounds->right, Bounds->bottom, 1.0f);
    if (InnerTop < InnerBottom)
    {
        if (Bounds->left < InnerLeft)
            DwmGpuComposeQuad(Bounds->left, InnerTop, InnerLeft, InnerBottom, 1.0f);
        if (InnerRight < Bounds->right)
            DwmGpuComposeQuad(InnerRight, InnerTop, Bounds->right, InnerBottom, 1.0f);
    }
    pglUseProgram(0);
    return glGetError() == GL_NO_ERROR;
}

BOOL
DwmGpuComposeShadow(const RECT *Bounds, LONGLONG X, LONGLONG Y,
                    LONG Width, LONG Height, LONG Offset, LONG WideExtent,
                    BOOL Active, ULONG WideOpacity, ULONG TightOpacity,
                    ULONG WindowAlpha)
{
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_SHADOW);
    BOOL Result = DwmD3dIsActive() ?
                    DwmD3dShadow(Bounds, X, Y, Width, Height, Offset, WideExtent, Active, WideOpacity, TightOpacity, WindowAlpha) :
                    DwmGpuComposeShadowMeasured(Bounds, X, Y, Width, Height, Offset, WideExtent, Active, WideOpacity, TightOpacity, WindowAlpha);
    DptEnd(&g_DwmPresentTrace, Trace, Result, 0);
    return Result;
}
