/*
 * PROJECT:     ReactOS OpenGL Smoke Test
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Launch a simple rotating 3D scene on the desktop and report
 *              the active WGL/OpenGL renderer details.
 */

#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdio.h>

#define GLSMOKE_CLASS_NAME "ReactOSGLSmokeWindow"
#define GLSMOKE_TITLE_BASE "ReactOS GL Smoke"
#define GLSMOKE_DEFAULT_SECONDS 15
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

static void
GlSmokeDebugPrint(const char *Format, ...)
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
GlSmokeParseDuration(LPCSTR CommandLine)
{
    const char *Option;
    char *End;
    unsigned long Seconds;

    if (!CommandLine)
        return GLSMOKE_DEFAULT_SECONDS * 1000;

    Option = strstr(CommandLine, "-seconds");
    if (!Option)
        Option = strstr(CommandLine, "/seconds");

    if (!Option)
        return GLSMOKE_DEFAULT_SECONDS * 1000;

    Option += 8;
    while (*Option == ' ' || *Option == '\t' || *Option == '=' || *Option == ':')
        ++Option;

    Seconds = strtoul(Option, &End, 10);
    if (End == Option)
        return GLSMOKE_DEFAULT_SECONDS * 1000;

    if (Seconds == 0)
        return 0;

    return (DWORD)min(Seconds, 600UL) * 1000;
}

static void
GlSmokeCopyString(char *Destination, size_t DestinationSize, const GLubyte *Source, const char *Fallback)
{
    if (!Destination || !DestinationSize)
        return;

    if (!Source)
        Source = (const GLubyte *)Fallback;

    _snprintf(Destination, DestinationSize - 1, "%s", (const char *)Source);
    Destination[DestinationSize - 1] = '\0';
}

static void
GlSmokeSetWindowTitle(HWND hWnd)
{
    char Title[384];

    _snprintf(Title,
              sizeof(Title) - 1,
              GLSMOKE_TITLE_BASE " - %s",
              g_renderer[0] ? g_renderer : "renderer unavailable");
    Title[sizeof(Title) - 1] = '\0';
    SetWindowTextA(hWnd, Title);
}

static void
GlSmokeLogFps(BOOL FinalSample)
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
        GlSmokeDebugPrint("GLSMOKE: average_fps=%.2f frames=%u elapsed_ms=%lu\r\n",
                          Fps,
                          g_frame_count,
                          NowTicks - g_start_ticks);
    }
    else
    {
        GlSmokeDebugPrint("GLSMOKE: fps=%.2f frames=%u sample_ms=%lu total_frames=%u\r\n",
                          Fps,
                          Frames,
                          ElapsedMs,
                          g_frame_count);
        g_last_fps_log_ticks = NowTicks;
        g_last_fps_log_frame_count = g_frame_count;
    }
}

static void
GlSmokeLogRendererInfo(void)
{
    BOOL Generic = (g_pfd.dwFlags & PFD_GENERIC_FORMAT) != 0;
    BOOL GenericAccelerated = (g_pfd.dwFlags & PFD_GENERIC_ACCELERATED) != 0;

    GlSmokeDebugPrint("GLSMOKE: pixel format=%u flags=0x%08lx generic=%u generic_accel=%u\r\n",
                      GetPixelFormat(g_hdc),
                      g_pfd.dwFlags,
                      Generic,
                      GenericAccelerated);
    GlSmokeDebugPrint("GLSMOKE: vendor=%s\r\n", g_vendor[0] ? g_vendor : "unknown");
    GlSmokeDebugPrint("GLSMOKE: renderer=%s\r\n", g_renderer[0] ? g_renderer : "unknown");
    GlSmokeDebugPrint("GLSMOKE: version=%s\r\n", g_version[0] ? g_version : "unknown");
}

static BOOL
GlSmokeSetPixelFormat(HDC hdc)
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
        GlSmokeDebugPrint("GLSMOKE: ChoosePixelFormat failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    if (!DescribePixelFormat(hdc, PixelFormat, sizeof(g_pfd), &g_pfd))
    {
        GlSmokeDebugPrint("GLSMOKE: DescribePixelFormat failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    if (!SetPixelFormat(hdc, PixelFormat, &g_pfd))
    {
        GlSmokeDebugPrint("GLSMOKE: SetPixelFormat(%d) failed, error=%lu\r\n", PixelFormat, GetLastError());
        return FALSE;
    }

    return TRUE;
}

static void
GlSmokeResize(int Width, int Height)
{
    if (Height <= 0)
        Height = 1;

    glViewport(0, 0, Width, Height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0f, (GLfloat)Width / (GLfloat)Height, 0.1f, 100.0f);
    glMatrixMode(GL_MODELVIEW);
}

static void
GlSmokeDrawCube(float Angle)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.0f);
    glRotatef(Angle, 1.0f, 0.8f, 0.3f);

    glBegin(GL_QUADS);
    glColor3f(0.90f, 0.15f, 0.20f);
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);
    glVertex3f( 1.0f,  1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);

    glColor3f(0.15f, 0.65f, 0.95f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);
    glVertex3f( 1.0f,  1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);

    glColor3f(0.20f, 0.85f, 0.30f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);

    glColor3f(0.95f, 0.80f, 0.15f);
    glVertex3f(1.0f, -1.0f, -1.0f);
    glVertex3f(1.0f,  1.0f, -1.0f);
    glVertex3f(1.0f,  1.0f,  1.0f);
    glVertex3f(1.0f, -1.0f,  1.0f);

    glColor3f(0.90f, 0.40f, 0.90f);
    glVertex3f(-1.0f, 1.0f, -1.0f);
    glVertex3f(-1.0f, 1.0f,  1.0f);
    glVertex3f( 1.0f, 1.0f,  1.0f);
    glVertex3f( 1.0f, 1.0f, -1.0f);

    glColor3f(0.15f, 0.85f, 0.85f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glEnd();

    SwapBuffers(g_hdc);
    ++g_frame_count;

    if ((GetTickCount() - g_last_fps_log_ticks) >= 1000)
        GlSmokeLogFps(FALSE);
}

static void
GlSmokeRenderFrame(void)
{
    DWORD ElapsedMs = GetTickCount() - g_start_ticks;
    float Angle = (float)(ElapsedMs % 12000) * 0.03f;

    if (g_glrc)
        GlSmokeDrawCube(Angle);
}

static BOOL
GlSmokeInit(HWND hWnd)
{
    const GLubyte *Renderer;
    const GLubyte *Vendor;
    const GLubyte *Version;
    RECT ClientRect;

    g_hdc = GetDC(hWnd);
    if (!g_hdc)
    {
        GlSmokeDebugPrint("GLSMOKE: GetDC failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    if (!GlSmokeSetPixelFormat(g_hdc))
        return FALSE;

    g_glrc = wglCreateContext(g_hdc);
    if (!g_glrc)
    {
        GlSmokeDebugPrint("GLSMOKE: wglCreateContext failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    if (!wglMakeCurrent(g_hdc, g_glrc))
    {
        GlSmokeDebugPrint("GLSMOKE: wglMakeCurrent failed, error=%lu\r\n", GetLastError());
        return FALSE;
    }

    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.08f, 0.12f, 1.0f);

    Renderer = glGetString(GL_RENDERER);
    Vendor = glGetString(GL_VENDOR);
    Version = glGetString(GL_VERSION);

    GlSmokeCopyString(g_renderer, sizeof(g_renderer), Renderer, "unknown");
    GlSmokeCopyString(g_vendor, sizeof(g_vendor), Vendor, "unknown");
    GlSmokeCopyString(g_version, sizeof(g_version), Version, "unknown");
    GlSmokeLogRendererInfo();
    GlSmokeSetWindowTitle(hWnd);

    if (GetClientRect(hWnd, &ClientRect))
        GlSmokeResize(ClientRect.right - ClientRect.left, ClientRect.bottom - ClientRect.top);

    return TRUE;
}

static void
GlSmokeCleanup(HWND hWnd)
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

static LRESULT CALLBACK
GlSmokeWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
            if (!GlSmokeInit(hWnd))
            {
                MessageBoxA(hWnd,
                            "OpenGL initialization failed. Check the serial log for GLSMOKE lines.",
                            GLSMOKE_TITLE_BASE,
                            MB_ICONERROR | MB_OK);
                return -1;
            }

            g_start_ticks = GetTickCount();
            g_last_fps_log_ticks = g_start_ticks;
            g_last_fps_log_frame_count = 0;
            return 0;

        case WM_SIZE:
            if (g_glrc)
                GlSmokeResize(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT Paint;

            BeginPaint(hWnd, &Paint);
            GlSmokeRenderFrame();
            EndPaint(hWnd, &Paint);
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            GlSmokeLogFps(TRUE);
            GlSmokeDebugPrint("GLSMOKE: rendered %u frames in %lu ms\r\n",
                              g_frame_count,
                              GetTickCount() - g_start_ticks);
            GlSmokeCleanup(hWnd);
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
    WindowClass.lpfnWndProc = GlSmokeWndProc;
    WindowClass.hInstance = hInstance;
    WindowClass.hCursor = LoadCursorA(NULL, IDC_ARROW);
    WindowClass.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    WindowClass.hIconSm = WindowClass.hIcon;
    WindowClass.lpszClassName = GLSMOKE_CLASS_NAME;

    if (!RegisterClassExA(&WindowClass))
        return 1;

    g_duration_ms = GlSmokeParseDuration(lpCmdLine);
    if (g_duration_ms != 0)
    {
        GlSmokeDebugPrint("GLSMOKE: starting for %lu ms\r\n", g_duration_ms);
    }
    else
    {
        GlSmokeDebugPrint("GLSMOKE: starting without timeout\r\n");
    }

    hWnd = CreateWindowExA(WS_EX_APPWINDOW,
                           GLSMOKE_CLASS_NAME,
                           GLSMOKE_TITLE_BASE,
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

        GlSmokeRenderFrame();
    }

    return 0;
}
