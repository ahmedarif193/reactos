/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Windowed WGL context, clear/readback, and swap smoke test
 */

#include <windows.h>
#include <wingdi.h>
#include <GL/gl.h>

#include "wine/test.h"

static LRESULT CALLBACK
WglSmokeWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static HWND
CreateWglSmokeWindow(void)
{
    WNDCLASSW cls;
    HWND hwnd;

    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc = WglSmokeWndProc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    cls.lpszClassName = L"ReactOSWglSmokeWindow";

    ok(RegisterClassW(&cls) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
       "RegisterClassW failed, error %lu\n", GetLastError());

    hwnd = CreateWindowExW(0,
                           cls.lpszClassName,
                           L"WGL smoke",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           64,
                           64,
                           160,
                           120,
                           NULL,
                           NULL,
                           cls.hInstance,
                           NULL);
    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (hwnd)
    {
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
    }

    return hwnd;
}

START_TEST(wgl_smoke)
{
    PIXELFORMATDESCRIPTOR requested;
    PIXELFORMATDESCRIPTOR actual;
    HWND hwnd;
    HDC hdc;
    HGLRC context;
    INT pixel_format;
    BOOL ret;
    const GLubyte *version;
    const GLubyte *vendor;
    const GLubyte *renderer;
    BYTE pixel[4] = {0};

    hwnd = CreateWglSmokeWindow();
    if (!hwnd)
        return;

    hdc = GetDC(hwnd);
    ok(hdc != NULL, "GetDC failed, error %lu\n", GetLastError());
    if (!hdc)
    {
        DestroyWindow(hwnd);
        UnregisterClassW(L"ReactOSWglSmokeWindow", GetModuleHandleW(NULL));
        return;
    }

    ZeroMemory(&requested, sizeof(requested));
    requested.nSize = sizeof(requested);
    requested.nVersion = 1;
    requested.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    requested.iPixelType = PFD_TYPE_RGBA;
    requested.cColorBits = 24;
    requested.cDepthBits = 16;
    requested.iLayerType = PFD_MAIN_PLANE;

    pixel_format = ChoosePixelFormat(hdc, &requested);
    ok(pixel_format > 0, "ChoosePixelFormat failed, error %lu\n", GetLastError());
    if (pixel_format <= 0)
    {
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        UnregisterClassW(L"ReactOSWglSmokeWindow", GetModuleHandleW(NULL));
        return;
    }

    ret = DescribePixelFormat(hdc, pixel_format, sizeof(actual), &actual);
    ok(ret != 0, "DescribePixelFormat(%d) failed, error %lu\n",
       pixel_format, GetLastError());
    ok((actual.dwFlags & PFD_DRAW_TO_WINDOW) != 0,
       "Pixel format 0x%08lx does not draw to window\n", actual.dwFlags);
    ok((actual.dwFlags & PFD_SUPPORT_OPENGL) != 0,
       "Pixel format 0x%08lx does not support OpenGL\n", actual.dwFlags);

    ret = SetPixelFormat(hdc, pixel_format, &actual);
    ok(ret != 0, "SetPixelFormat(%d) failed, error %lu\n",
       pixel_format, GetLastError());
    if (!ret)
    {
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        UnregisterClassW(L"ReactOSWglSmokeWindow", GetModuleHandleW(NULL));
        return;
    }

    context = wglCreateContext(hdc);
    ok(context != NULL, "wglCreateContext failed, error %lu\n", GetLastError());
    if (!context)
    {
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        UnregisterClassW(L"ReactOSWglSmokeWindow", GetModuleHandleW(NULL));
        return;
    }

    ret = wglMakeCurrent(hdc, context);
    ok(ret != FALSE, "wglMakeCurrent failed, error %lu\n", GetLastError());
    if (ret)
    {
        version = glGetString(GL_VERSION);
        vendor = glGetString(GL_VENDOR);
        renderer = glGetString(GL_RENDERER);
        ok(version != NULL && version[0] != 0, "GL_VERSION is empty\n");
        ok(vendor != NULL && vendor[0] != 0, "GL_VENDOR is empty\n");
        ok(renderer != NULL && renderer[0] != 0, "GL_RENDERER is empty\n");
        trace("GL vendor='%s' renderer='%s' version='%s' pfd_flags=0x%08lx\n",
              vendor ? (const char *)vendor : "(null)",
              renderer ? (const char *)renderer : "(null)",
              version ? (const char *)version : "(null)",
              actual.dwFlags);

        glViewport(0, 0, 1, 1);
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glFlush();
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        ok(pixel[0] >= 0xc0,
           "Expected red channel after clear, got rgba=(%u,%u,%u,%u)\n",
           pixel[0], pixel[1], pixel[2], pixel[3]);
        ok(pixel[1] <= 0x40 && pixel[2] <= 0x40,
           "Expected green/blue near zero after clear, got rgba=(%u,%u,%u,%u)\n",
           pixel[0], pixel[1], pixel[2], pixel[3]);

        ret = SwapBuffers(hdc);
        ok(ret != FALSE, "SwapBuffers failed, error %lu\n", GetLastError());
    }

    wglMakeCurrent(NULL, NULL);
    ret = wglDeleteContext(context);
    ok(ret != FALSE, "wglDeleteContext failed, error %lu\n", GetLastError());
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    UnregisterClassW(L"ReactOSWglSmokeWindow", GetModuleHandleW(NULL));
}
