/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     Exercise a windowed WGL producer through DWM and display scanout.
 */

#include <windows.h>
#include <GL/gl.h>
#include <wine/test.h>
#include <reactos/dwmprof.h>
#include <reactos/dwmpresenttracenames.h>

static const WCHAR class_name[] = L"WglDesktopRegression";
static const COLORREF background = RGB(32, 48, 64), cover_color = RGB(208, 48, 160);
static const DWORD window_style = WS_POPUP | WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
static unsigned failures, presents, pixel_checks;
static const COLORREF calibration[] = {
    RGB(64, 64, 64), RGB(160, 64, 64), RGB(64, 160, 64),
    RGB(64, 64, 160), RGB(160, 160, 160), RGB(208, 48, 160)
};

static void log_message(const char *format, ...)
{
    char buffer[768];
    va_list args;
    va_start(args, format);
    _vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = 0;
    trace("%s", buffer);
    OutputDebugStringA(buffer);
}

static BOOL check(BOOL result, const char *stage)
{
    ok(result, "%s failed: %lu\n", stage, GetLastError());
    if (!result)
    {
        ++failures;
        log_message("WGL_DESKTOP_ERROR stage=%s error=%lu\n", stage, GetLastError());
    }
    return result;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        COLORREF color = (COLORREF)GetWindowLongPtrW(window, GWLP_USERDATA);
        if (color)
        {
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(dc, &paint.rcPaint, brush);
            DeleteObject(brush);
            if (color == background)
            {
                unsigned index;
                for (index = 0; index < ARRAYSIZE(calibration); ++index)
                {
                    RECT patch = {936, 48 + index * 80, 1000, 104 + index * 80};
                    brush = CreateSolidBrush(calibration[index]);
                    FillRect(dc, &patch, brush);
                    DeleteObject(brush);
                }
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void pump(DWORD milliseconds)
{
    DWORD start = GetTickCount();
    do
    {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(5);
    } while (GetTickCount() - start < milliseconds);
}

static void color(unsigned phase, unsigned quadrant, BYTE value[4])
{
    static const BYTE palette[4][3] = {{200, 48, 64}, {48, 184, 72}, {48, 72, 200}, {200, 176, 48}};
    unsigned component;
    for (component = 0; component < 3; ++component)
        value[component] = 24 + (palette[quadrant][component] + phase * 19) % 208;
    value[3] = 255;
}

static BOOL render(HDC dc, unsigned phase, int width, int height, BOOL verify)
{
    BYTE pixels[16];
    GLenum error;
    unsigned quadrant;
    for (quadrant = 0; quadrant < 4; ++quadrant)
        color(phase, quadrant, &pixels[quadrant * 4]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, 1);
    glEnd();
    if (verify)
    {
        for (quadrant = 0; quadrant < 4; ++quadrant)
        {
            BYTE actual[4], expected[4];
            unsigned component;
            BOOL match = TRUE;
            int x = width * (quadrant & 1 ? 3 : 1) / 4;
            int y = height - 1 - height * (quadrant & 2 ? 3 : 1) / 4;
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
            color(phase, quadrant, expected);
            for (component = 0; component < 3; ++component)
            {
                int difference = (int)actual[component] - expected[component];
                if (difference < -1 || difference > 1) match = FALSE;
            }
            ++pixel_checks;
            ok(match, "phase %u quadrant %u: got %u,%u,%u expected %u,%u,%u\n", phase, quadrant,
               actual[0], actual[1], actual[2], expected[0], expected[1], expected[2]);
            if (!match) { ++failures; return FALSE; }
        }
    }
    error = glGetError();
    ok(error == GL_NO_ERROR, "GL error %#x at phase %u\n", error, phase);
    if (error != GL_NO_ERROR)
    {
        ++failures;
        log_message("WGL_DESKTOP_ERROR stage=render phase=%u gl_error=%x\n", phase, error);
        return FALSE;
    }
    if (!check(SwapBuffers(dc), "swap")) return FALSE;
    ++presents;
    return TRUE;
}

static void sample(unsigned phase, unsigned index, int x, int y, const BYTE value[4])
{
    log_message("WGL_DESKTOP_SAMPLE phase=%u index=%u x=%d y=%d rgb=%u,%u,%u\n",
        phase, index, x, y, value[0], value[1], value[2]);
}

static void samples(unsigned phase, HWND window, HWND cover)
{
    RECT client, cover_rect;
    POINT origin = {0, 0};
    BYTE value[4];
    unsigned quadrant;
    BOOL covered = IsWindowVisible(cover);
    GetClientRect(window, &client);
    ClientToScreen(window, &origin);
    GetWindowRect(cover, &cover_rect);
    for (quadrant = 0; quadrant < 4; ++quadrant)
    {
        POINT point = {origin.x + client.right * (quadrant & 1 ? 3 : 1) / 4,
                       origin.y + client.bottom * (quadrant & 2 ? 3 : 1) / 4};
        color(phase, quadrant, value);
        if (IsIconic(window))
        {
            value[0] = GetRValue(background); value[1] = GetGValue(background); value[2] = GetBValue(background);
            point.x = 32 + 800 * (quadrant & 1 ? 3 : 1) / 4;
            point.y = 64 + 600 * (quadrant & 2 ? 3 : 1) / 4;
        }
        if (covered && PtInRect(&cover_rect, point))
        {
            value[0] = GetRValue(cover_color); value[1] = GetGValue(cover_color); value[2] = GetBValue(cover_color);
        }
        sample(phase, quadrant, point.x, point.y, value);
    }
    /* Check pixels vacated by the cursor, away from the window border. */
    if (!IsIconic(window))
    {
        color(phase, 0, value);
        sample(phase, 4, origin.x + 48, origin.y + 48, value);
    }
    if (covered)
    {
        value[0] = GetRValue(cover_color); value[1] = GetGValue(cover_color); value[2] = GetBValue(cover_color);
        sample(phase, 5, cover_rect.left + 32, cover_rect.top + 32, value);
    }
}

START_TEST(wgl_desktop)
{
    WNDCLASSW wc = {0};
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1};
    HWND backdrop = NULL, window = NULL, cover = NULL;
    HDC dc = NULL;
    HGLRC context = NULL;
    GLuint texture = 0;
    RECT bounds = {0, 0, 800, 600};
    POINT old_cursor;
    HMODULE profiler = NULL;
    HRESULT (WINAPI *start_profile)(ULONG *) = NULL;
    HRESULT (WINAPI *stop_profile)(ULONG, DPT_SNAPSHOT *, ULONG) = NULL;
    ULONG session = 0;
    unsigned phase;
    int format;
    BOOL complete = FALSE, have_cursor = GetCursorPos(&old_cursor);
    static const char *names[] = {"visible", "gdi_overlap", "move", "uncover", "resize", "minimize", "restore", "cursor"};

    failures = presents = pixel_checks = 0;
    log_message("WGL_DESKTOP_BEGIN physical_display=unverified\n");
    for (phase = 0; phase < ARRAYSIZE(calibration); ++phase)
        log_message("WGL_DESKTOP_CALIBRATION index=%u x=976 y=%u rgb=%u,%u,%u\n",
            phase, 84 + phase * 80, GetRValue(calibration[phase]),
            GetGValue(calibration[phase]), GetBValue(calibration[phase]));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = class_name;
    if (!check(RegisterClassW(&wc) != 0, "class")) return;
    backdrop = CreateWindowW(class_name, L"Desktop regression background", WS_POPUP,
        8, 8, 1020, 692, NULL, NULL, wc.hInstance, NULL);
    if (!check(backdrop != NULL, "background")) goto cleanup;
    SetWindowLongPtrW(backdrop, GWLP_USERDATA, background);
    ShowWindow(backdrop, SW_SHOWNOACTIVATE);
    UpdateWindow(backdrop);
    AdjustWindowRectEx(&bounds, window_style, FALSE, WS_EX_WINDOWEDGE);
    window = CreateWindowExW(WS_EX_WINDOWEDGE, class_name, L"WGL desktop regression",
        window_style, 32, 32, bounds.right - bounds.left, bounds.bottom - bounds.top,
        NULL, NULL, wc.hInstance, NULL);
    cover = CreateWindowW(class_name, L"GDI overlap", WS_POPUP, 170, 150, 300, 220,
        NULL, NULL, wc.hInstance, NULL);
    if (!check(window != NULL && cover != NULL, "windows")) goto cleanup;
    SetWindowLongPtrW(cover, GWLP_USERDATA, cover_color);
    dc = GetDC(window);
    if (!check(dc != NULL, "dc")) goto cleanup;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    format = ChoosePixelFormat(dc, &pfd);
    if (!check(format && SetPixelFormat(dc, format, &pfd), "pixel_format")) goto cleanup;
    context = wglCreateContext(dc);
    if (!check(context && wglMakeCurrent(dc, context), "context")) goto cleanup;
    log_message("WGL_DESKTOP_CONTEXT renderer=%s version=%s client=800x600 style=%08lx desktop=%dx%d\n",
        glGetString(GL_RENDERER), glGetString(GL_VERSION), GetWindowLongW(window, GWL_STYLE),
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    profiler = LoadLibraryW(L"dwmprof.dll");
    if (profiler)
    {
        start_profile = (void *)GetProcAddress(profiler, "DwmProfileStartCapture");
        stop_profile = (void *)GetProcAddress(profiler, "DwmProfileStopCapture");
        if (start_profile && stop_profile)
        {
            HRESULT status = start_profile(&session);
            log_message("WGL_DESKTOP_PROFILE start=%08lx session=%lu\n", status, session);
            if (FAILED(status)) session = 0;
        }
    }
    for (phase = 0; phase < ARRAYSIZE(names); ++phase)
    {
        DWORD start;
        RECT client;
        POINT cursor = {48, 48};
        if (phase == 1)
        {
            ShowWindow(cover, SW_SHOWNOACTIVATE);
            SetWindowPos(cover, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            UpdateWindow(cover);
        }
        if (phase == 2) SetWindowPos(window, cover, 104, 48, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        if (phase == 3) ShowWindow(cover, SW_HIDE);
        if (phase == 4)
        {
            SetRect(&bounds, 0, 0, 640, 480);
            AdjustWindowRectEx(&bounds, window_style, FALSE, WS_EX_WINDOWEDGE);
            SetWindowPos(window, NULL, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (phase == 5) ShowWindow(window, SW_MINIMIZE);
        if (phase == 6) ShowWindow(window, SW_RESTORE);
        if (phase == 7)
        {
            RECT cover_rect;
            ShowWindow(cover, SW_SHOWNOACTIVATE);
            SetWindowPos(cover, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            UpdateWindow(cover);
            GetWindowRect(cover, &cover_rect);
            SetCursorPos(cover_rect.left + 32, cover_rect.top + 32);
            pump(100);
        }
        GetClientRect(window, &client);
        if (!IsIconic(window))
        {
            ClientToScreen(window, &cursor);
            SetCursorPos(cursor.x, cursor.y);
            if (!render(dc, phase, client.right, client.bottom, TRUE)) goto cleanup;
        }
        pump(150);
        SetCursorPos(980, 660);
        log_message("WGL_DESKTOP_PHASE phase=%u name=%s tick=%lu\n", phase, names[phase], GetTickCount());
        samples(phase, window, cover);
        start = GetTickCount();
        do
        {
            if (!IsIconic(window) && !render(dc, phase, client.right, client.bottom, FALSE)) goto cleanup;
            pump(50);
        } while (GetTickCount() - start < 1800);
    }
    complete = TRUE;

cleanup:
    if (session)
    {
        DPT_SNAPSHOT snapshot = {0};
        HRESULT status = stop_profile(session, &snapshot, sizeof(snapshot));
        ULONG domain, metric;
        log_message("WGL_DESKTOP_PROFILE stop=%08lx available=%lx\n", status, snapshot.Available);
        if (SUCCEEDED(status))
            for (domain = 0; domain < 3; ++domain)
                for (metric = 0; metric < DPT_METRIC_COUNT; ++metric)
                {
                    const DPT_COUNTER *counter = &snapshot.Domain[domain].Counter[metric];
                    if (!(snapshot.Available & (1 << domain)) || !counter->Entered) continue;
                    ok(counter->Failed == 0, "domain %lu stage %s failed %I64u times\n",
                        domain, DptMetricName(metric), counter->Failed);
                    if (counter->Failed) ++failures;
                    log_message("WGL_DESKTOP_COUNTER domain=%lu stage=%s entered=%I64u completed=%I64u failed=%I64u\n",
                        domain, DptMetricName(metric), counter->Entered, counter->Completed, counter->Failed);
                }
    }
    if (texture) glDeleteTextures(1, &texture);
    wglMakeCurrent(NULL, NULL);
    if (context) wglDeleteContext(context);
    if (dc) ReleaseDC(window, dc);
    if (cover) DestroyWindow(cover);
    if (window) DestroyWindow(window);
    if (backdrop) DestroyWindow(backdrop);
    UnregisterClassW(class_name, wc.hInstance);
    if (profiler) FreeLibrary(profiler);
    if (have_cursor) SetCursorPos(old_cursor.x, old_cursor.y);
    log_message("WGL_DESKTOP_END complete=%u failures=%u pixel_checks=%u presents=%u physical_display=unverified\n",
        complete, failures, pixel_checks, presents);
}
