/*
 * PROJECT:     ReactOS OpenGL Gears Demo
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Classic spinning gears demo for WDDM OpenGL validation
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Gear rendering based on the classic glxgears by Brian Paul.
 */

#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GLXGEARS_CLASS_NAME "ReactOSGLXGearsWindow"
#define GLXGEARS_TITLE_BASE "ReactOS GLXGears"
#define GLXGEARS_DEFAULT_SECONDS 15

static HDC g_hdc;
static HGLRC g_glrc;
static DWORD g_start_ticks;
static DWORD g_duration_ms;
static DWORD g_last_fps_log_ticks;
static UINT g_frame_count;
static UINT g_last_fps_log_frame_count;
static char g_renderer[128];
static char g_vendor[128];
static char g_version[128];
static PIXELFORMATDESCRIPTOR g_pfd;

static GLuint g_gear1, g_gear2, g_gear3;
static GLfloat g_angle = 0.0f;

static void
DebugPrint(const char *Format, ...)
{
    char Buffer[512];
    va_list Args;

    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Args);
    va_end(Args);

    Buffer[sizeof(Buffer) - 1] = '\0';
    OutputDebugStringA(Buffer);
}

static DWORD
ParseDuration(LPCSTR CommandLine)
{
    const char *Option;
    char *End;
    unsigned long Seconds;

    if (!CommandLine)
        return GLXGEARS_DEFAULT_SECONDS * 1000;

    Option = strstr(CommandLine, "-seconds");
    if (!Option)
        Option = strstr(CommandLine, "/seconds");

    if (!Option)
        return GLXGEARS_DEFAULT_SECONDS * 1000;

    Option += 8;
    while (*Option == ' ' || *Option == '\t' || *Option == '=' || *Option == ':')
        ++Option;

    Seconds = strtoul(Option, &End, 10);
    if (End == Option)
        return GLXGEARS_DEFAULT_SECONDS * 1000;

    if (Seconds == 0)
        return 0;

    return (DWORD)min(Seconds, 600UL) * 1000;
}

static void
CopyGLString(char *Destination, size_t DestinationSize, const GLubyte *Source, const char *Fallback)
{
    if (!Destination || !DestinationSize)
        return;

    if (!Source)
        Source = (const GLubyte *)Fallback;

    _snprintf(Destination, DestinationSize - 1, "%s", (const char *)Source);
    Destination[DestinationSize - 1] = '\0';
}

static void
LogFps(BOOL FinalSample)
{
    DWORD NowTicks;
    DWORD ElapsedMs;
    UINT Frames;
    double Fps;

    NowTicks = GetTickCount();

    if (FinalSample)
    {
        ElapsedMs = NowTicks - g_start_ticks;
        Frames = g_frame_count;
    }
    else
    {
        ElapsedMs = NowTicks - g_last_fps_log_ticks;
        Frames = g_frame_count - g_last_fps_log_frame_count;
    }

    if (ElapsedMs == 0)
        return;

    Fps = ((double)Frames * 1000.0) / (double)ElapsedMs;

    if (FinalSample)
    {
        DebugPrint("GLXGEARS: average_fps=%.2f frames=%u elapsed_ms=%lu\r\n",
                   Fps, g_frame_count, NowTicks - g_start_ticks);
    }
    else
    {
        DebugPrint("GLXGEARS: fps=%.2f frames=%u sample_ms=%lu total_frames=%u\r\n",
                   Fps, Frames, ElapsedMs, g_frame_count);
        g_last_fps_log_ticks = NowTicks;
        g_last_fps_log_frame_count = g_frame_count;
    }
}

/*
 * Draw a gear wheel.
 *
 *   inner_radius - radius of hole at center
 *   outer_radius - radius at center of teeth
 *   width        - width of gear
 *   teeth        - number of teeth
 *   tooth_depth  - depth of tooth
 */
static void
Gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
     GLint teeth, GLfloat tooth_depth)
{
    GLint i;
    GLfloat r0, r1, r2;
    GLfloat angle, da;
    GLfloat u, v, len;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0f;
    r2 = outer_radius + tooth_depth / 2.0f;

    da = 2.0f * (GLfloat)M_PI / teeth / 4.0f;

    glShadeModel(GL_FLAT);

    glNormal3f(0.0f, 0.0f, 1.0f);

    /* Front face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++)
    {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r0 * (GLfloat)cos(angle), r0 * (GLfloat)sin(angle), width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(angle), r1 * (GLfloat)sin(angle), width * 0.5f);
        if (i < teeth)
        {
            glVertex3f(r0 * (GLfloat)cos(angle), r0 * (GLfloat)sin(angle), width * 0.5f);
            glVertex3f(r1 * (GLfloat)cos(angle + 3 * da), r1 * (GLfloat)sin(angle + 3 * da), width * 0.5f);
        }
    }
    glEnd();

    /* Front sides of teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++)
    {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;

        glVertex3f(r1 * (GLfloat)cos(angle), r1 * (GLfloat)sin(angle), width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(angle + da), r2 * (GLfloat)sin(angle + da), width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(angle + 2 * da), r2 * (GLfloat)sin(angle + 2 * da), width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(angle + 3 * da), r1 * (GLfloat)sin(angle + 3 * da), width * 0.5f);
    }
    glEnd();

    glNormal3f(0.0f, 0.0f, -1.0f);

    /* Back face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++)
    {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r1 * (GLfloat)cos(angle), r1 * (GLfloat)sin(angle), -width * 0.5f);
        glVertex3f(r0 * (GLfloat)cos(angle), r0 * (GLfloat)sin(angle), -width * 0.5f);
        if (i < teeth)
        {
            glVertex3f(r1 * (GLfloat)cos(angle + 3 * da), r1 * (GLfloat)sin(angle + 3 * da), -width * 0.5f);
            glVertex3f(r0 * (GLfloat)cos(angle), r0 * (GLfloat)sin(angle), -width * 0.5f);
        }
    }
    glEnd();

    /* Back sides of teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++)
    {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;

        glVertex3f(r1 * (GLfloat)cos(angle + 3 * da), r1 * (GLfloat)sin(angle + 3 * da), -width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(angle + 2 * da), r2 * (GLfloat)sin(angle + 2 * da), -width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(angle + da), r2 * (GLfloat)sin(angle + da), -width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(angle), r1 * (GLfloat)sin(angle), -width * 0.5f);
    }
    glEnd();

    /* Outward faces of teeth */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i < teeth; i++)
    {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;

        glVertex3f(r1 * (GLfloat)cos(angle), r1 * (GLfloat)sin(angle), width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(angle), r1 * (GLfloat)sin(angle), -width * 0.5f);
        u = r2 * (GLfloat)cos(angle + da) - r1 * (GLfloat)cos(angle);
        v = r2 * (GLfloat)sin(angle + da) - r1 * (GLfloat)sin(angle);
        len = (GLfloat)sqrt(u * u + v * v);
        u /= len;
        v /= len;
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r2 * (GLfloat)cos(angle + da), r2 * (GLfloat)sin(angle + da), width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(angle + da), r2 * (GLfloat)sin(angle + da), -width * 0.5f);
        glNormal3f((GLfloat)cos(angle), (GLfloat)sin(angle), 0.0f);
        glVertex3f(r2 * (GLfloat)cos(angle + 2 * da), r2 * (GLfloat)sin(angle + 2 * da), width * 0.5f);
        glVertex3f(r2 * (GLfloat)cos(angle + 2 * da), r2 * (GLfloat)sin(angle + 2 * da), -width * 0.5f);
        u = r1 * (GLfloat)cos(angle + 3 * da) - r2 * (GLfloat)cos(angle + 2 * da);
        v = r1 * (GLfloat)sin(angle + 3 * da) - r2 * (GLfloat)sin(angle + 2 * da);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r1 * (GLfloat)cos(angle + 3 * da), r1 * (GLfloat)sin(angle + 3 * da), width * 0.5f);
        glVertex3f(r1 * (GLfloat)cos(angle + 3 * da), r1 * (GLfloat)sin(angle + 3 * da), -width * 0.5f);
        glNormal3f((GLfloat)cos(angle), (GLfloat)sin(angle), 0.0f);
    }

    glVertex3f(r1 * (GLfloat)cos(0), r1 * (GLfloat)sin(0), width * 0.5f);
    glVertex3f(r1 * (GLfloat)cos(0), r1 * (GLfloat)sin(0), -width * 0.5f);

    glEnd();

    glShadeModel(GL_SMOOTH);

    /* Inside radius cylinder */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++)
    {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;
        glNormal3f(-(GLfloat)cos(angle), -(GLfloat)sin(angle), 0.0f);
        glVertex3f(r0 * (GLfloat)cos(angle), r0 * (GLfloat)sin(angle), -width * 0.5f);
        glVertex3f(r0 * (GLfloat)cos(angle), r0 * (GLfloat)sin(angle), width * 0.5f);
    }
    glEnd();
}

static void
InitGears(void)
{
    static GLfloat pos[4] = { 5.0f, 5.0f, 10.0f, 0.0f };
    static GLfloat red[4] = { 0.8f, 0.1f, 0.0f, 1.0f };
    static GLfloat green[4] = { 0.0f, 0.8f, 0.2f, 1.0f };
    static GLfloat blue[4] = { 0.2f, 0.2f, 1.0f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);

    /* Gear 1: red */
    g_gear1 = glGenLists(1);
    glNewList(g_gear1, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
    Gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glEndList();

    /* Gear 2: green */
    g_gear2 = glGenLists(1);
    glNewList(g_gear2, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
    Gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glEndList();

    /* Gear 3: blue */
    g_gear3 = glGenLists(1);
    glNewList(g_gear3, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
    Gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glEndList();

    glEnable(GL_NORMALIZE);
}

static void
DrawGears(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
    glRotatef(20.0f, 1.0f, 0.0f, 0.0f);

    glPushMatrix();
    glTranslatef(-3.0f, -2.0f, 0.0f);
    glRotatef(g_angle, 0.0f, 0.0f, 1.0f);
    glCallList(g_gear1);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(3.1f, -2.0f, 0.0f);
    glRotatef(-2.0f * g_angle - 9.0f, 0.0f, 0.0f, 1.0f);
    glCallList(g_gear2);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-3.1f, 4.2f, 0.0f);
    glRotatef(-2.0f * g_angle - 25.0f, 0.0f, 0.0f, 1.0f);
    glCallList(g_gear3);
    glPopMatrix();

    glPopMatrix();

    SwapBuffers(g_hdc);
    ++g_frame_count;

    if ((GetTickCount() - g_last_fps_log_ticks) >= 1000)
        LogFps(FALSE);
}

static BOOL
SetupPixelFormat(HDC hdc)
{
    PIXELFORMATDESCRIPTOR Requested;
    int PixelFormat;

    ZeroMemory(&Requested, sizeof(Requested));
    Requested.nSize = sizeof(Requested);
    Requested.nVersion = 1;
    Requested.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Requested.iPixelType = PFD_TYPE_RGBA;
    Requested.cColorBits = 24;
    Requested.cDepthBits = 24;
    Requested.iLayerType = PFD_MAIN_PLANE;

    PixelFormat = ChoosePixelFormat(hdc, &Requested);
    if (!PixelFormat)
    {
        DebugPrint("GLXGEARS: ChoosePixelFormat failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    if (!DescribePixelFormat(hdc, PixelFormat, sizeof(g_pfd), &g_pfd))
    {
        DebugPrint("GLXGEARS: DescribePixelFormat failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    if (!SetPixelFormat(hdc, PixelFormat, &g_pfd))
    {
        DebugPrint("GLXGEARS: SetPixelFormat(%d) failed, error=%lu\r\n", PixelFormat, GetLastError());
        return FALSE;
    }

    return TRUE;
}

static void
Reshape(int Width, int Height)
{
    GLfloat h;

    if (Height <= 0)
        Height = 1;

    h = (GLfloat)Height / (GLfloat)Width;

    glViewport(0, 0, Width, Height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -h, h, 5.0, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -40.0f);
}

static void
LogRendererInfo(void)
{
    BOOL Generic = (g_pfd.dwFlags & PFD_GENERIC_FORMAT) != 0;
    BOOL GenericAccelerated = (g_pfd.dwFlags & PFD_GENERIC_ACCELERATED) != 0;

    DebugPrint("GLXGEARS: pixel format=%u flags=0x%08lx generic=%u generic_accel=%u\r\n",
               GetPixelFormat(g_hdc), g_pfd.dwFlags, Generic, GenericAccelerated);
    DebugPrint("GLXGEARS: vendor=%s\r\n", g_vendor[0] ? g_vendor : "unknown");
    DebugPrint("GLXGEARS: renderer=%s\r\n", g_renderer[0] ? g_renderer : "unknown");
    DebugPrint("GLXGEARS: version=%s\r\n", g_version[0] ? g_version : "unknown");
}

static BOOL
InitGL(HWND hWnd)
{
    const GLubyte *Renderer;
    const GLubyte *Vendor;
    const GLubyte *Version;
    RECT ClientRect;
    char Title[384];

    g_hdc = GetDC(hWnd);
    if (!g_hdc)
    {
        DebugPrint("GLXGEARS: GetDC failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    if (!SetupPixelFormat(g_hdc))
        return FALSE;

    g_glrc = wglCreateContext(g_hdc);
    if (!g_glrc)
    {
        DebugPrint("GLXGEARS: wglCreateContext failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    if (!wglMakeCurrent(g_hdc, g_glrc))
    {
        DebugPrint("GLXGEARS: wglMakeCurrent failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    Renderer = glGetString(GL_RENDERER);
    Vendor = glGetString(GL_VENDOR);
    Version = glGetString(GL_VERSION);

    CopyGLString(g_renderer, sizeof(g_renderer), Renderer, "unknown");
    CopyGLString(g_vendor, sizeof(g_vendor), Vendor, "unknown");
    CopyGLString(g_version, sizeof(g_version), Version, "unknown");
    LogRendererInfo();

    _snprintf(Title, sizeof(Title) - 1, GLXGEARS_TITLE_BASE " - %s",
              g_renderer[0] ? g_renderer : "renderer unavailable");
    Title[sizeof(Title) - 1] = '\0';
    SetWindowTextA(hWnd, Title);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    InitGears();

    if (GetClientRect(hWnd, &ClientRect))
        Reshape(ClientRect.right - ClientRect.left, ClientRect.bottom - ClientRect.top);

    return TRUE;
}

static void
Cleanup(HWND hWnd)
{
    if (g_glrc)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_glrc);
        g_glrc = NULL;
    }

    if (g_hdc)
    {
        ReleaseDC(hWnd, g_hdc);
        g_hdc = NULL;
    }
}

static void
AnimateFrame(void)
{
    /* ~60 degrees per second */
    DWORD ElapsedMs = GetTickCount() - g_start_ticks;
    g_angle = (GLfloat)(ElapsedMs % 6000) * 0.06f;

    if (g_glrc)
        DrawGears();
}

static LRESULT CALLBACK
WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
            if (!InitGL(hWnd))
            {
                MessageBoxA(hWnd,
                            "OpenGL initialization failed. Check the serial log for GLXGEARS lines.",
                            GLXGEARS_TITLE_BASE,
                            MB_ICONERROR | MB_OK);
                return -1;
            }

            g_start_ticks = GetTickCount();
            g_last_fps_log_ticks = g_start_ticks;
            g_last_fps_log_frame_count = 0;
            return 0;

        case WM_SIZE:
            if (g_glrc)
                Reshape(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT Paint;

            BeginPaint(hWnd, &Paint);
            AnimateFrame();
            EndPaint(hWnd, &Paint);
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            LogFps(TRUE);
            DebugPrint("GLXGEARS: rendered %u frames in %lu ms\r\n",
                       g_frame_count, GetTickCount() - g_start_ticks);
            Cleanup(hWnd);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEXA WindowClass;
    HWND hWnd;
    MSG Message;

    (void)hPrevInstance;
    (void)nCmdShow;

    ZeroMemory(&WindowClass, sizeof(WindowClass));
    WindowClass.cbSize = sizeof(WindowClass);
    WindowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    WindowClass.lpfnWndProc = WndProc;
    WindowClass.hInstance = hInstance;
    WindowClass.hCursor = LoadCursorA(NULL, IDC_ARROW);
    WindowClass.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    WindowClass.hIconSm = WindowClass.hIcon;
    WindowClass.lpszClassName = GLXGEARS_CLASS_NAME;

    if (!RegisterClassExA(&WindowClass))
        return 1;

    g_duration_ms = ParseDuration(lpCmdLine);
    if (g_duration_ms != 0)
        DebugPrint("GLXGEARS: starting for %lu ms\r\n", g_duration_ms);
    else
        DebugPrint("GLXGEARS: starting without timeout\r\n");

    hWnd = CreateWindowExA(WS_EX_APPWINDOW,
                           GLXGEARS_CLASS_NAME,
                           GLXGEARS_TITLE_BASE,
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT,
                           CW_USEDEFAULT,
                           960,
                           720,
                           NULL,
                           NULL,
                           hInstance,
                           NULL);
    if (!hWnd)
        return 2;

    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);

    for (;;)
    {
        while (PeekMessageA(&Message, NULL, 0, 0, PM_REMOVE))
        {
            if (Message.message == WM_QUIT)
                return (int)Message.wParam;

            TranslateMessage(&Message);
            DispatchMessageA(&Message);
        }

        if (!IsWindow(hWnd))
            break;

        if (g_duration_ms && (GetTickCount() - g_start_ticks) >= g_duration_ms)
        {
            DestroyWindow(hWnd);
            continue;
        }

        AnimateFrame();
    }

    return 0;
}
