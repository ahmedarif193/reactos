/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     WGL_EXT_swap_control drawable ownership
 */

#include <windows.h>
#include <GL/gl.h>
#include <wine/test.h>

START_TEST(wgl_swap_control)
{
    BOOL (WINAPI *set_interval)(int);
    int (WINAPI *get_interval)(void);
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1 };
    HWND windows[2] = { NULL, NULL };
    HDC dcs[2] = { NULL, NULL };
    HGLRC contexts[2] = { NULL, NULL };
    BOOL ret;
    int i, format;

    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    for (i = 0; i < 2; ++i)
    {
        windows[i] = CreateWindowA("static", "swap control", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
        ok(windows[i] != NULL, "CreateWindow failed: %lu\n", GetLastError());
        if (!windows[i]) goto cleanup;
        dcs[i] = GetDC(windows[i]);
        ok(dcs[i] != NULL, "GetDC failed: %lu\n", GetLastError());
        if (!dcs[i]) goto cleanup;
        format = ChoosePixelFormat(dcs[i], &pfd);
        ok(format != 0, "ChoosePixelFormat failed: %lu\n", GetLastError());
        if (!format) goto cleanup;
        ret = SetPixelFormat(dcs[i], format, &pfd);
        ok(ret, "SetPixelFormat failed: %lu\n", GetLastError());
        if (!ret) goto cleanup;
        contexts[i] = wglCreateContext(dcs[i]);
        ok(contexts[i] != NULL, "wglCreateContext failed: %lu\n", GetLastError());
        if (!contexts[i]) goto cleanup;
    }
    ret = wglMakeCurrent(dcs[0], contexts[0]);
    ok(ret, "wglMakeCurrent failed: %lu\n", GetLastError());
    if (!ret) goto cleanup;
    set_interval = (void *)wglGetProcAddress("wglSwapIntervalEXT");
    get_interval = (void *)wglGetProcAddress("wglGetSwapIntervalEXT");
    if (!set_interval || !get_interval)
    {
        skip("WGL_EXT_swap_control is unavailable\n");
        goto cleanup;
    }
    ok(get_interval() == 1, "New drawable must default to interval 1\n");
    ok(set_interval(0), "Failed to disable synchronization\n");
    ok(get_interval() == 0, "Interval 0 was not retained\n");
    ok(wglMakeCurrent(dcs[0], contexts[1]), "Failed to change context on first drawable\n");
    ok(get_interval() == 0, "Interval must belong to the drawable, not the context\n");
    ok(wglMakeCurrent(dcs[1], contexts[1]), "Failed to select second drawable\n");
    ok(get_interval() == 1, "First drawable changed the second drawable's default\n");
    ok(set_interval(2), "Failed to set second drawable's interval\n");
    ok(get_interval() == 2, "Second drawable did not retain interval 2\n");
    ok(wglMakeCurrent(dcs[0], contexts[0]), "Failed to restore first drawable\n");
    ok(get_interval() == 0, "Second drawable changed the first drawable's interval\n");
    SetLastError(0xdeadbeef);
    ret = set_interval(-1);
    ok(!ret && GetLastError() == ERROR_INVALID_DATA, "Negative interval: ret %d, error %lu\n", ret, GetLastError());
    ok(get_interval() == 0, "Rejected interval changed drawable state\n");
    ok(wglMakeCurrent(NULL, NULL), "Failed to unbind context\n");
    SetLastError(0xdeadbeef);
    ret = set_interval(1);
    ok(!ret && GetLastError() == ERROR_DC_NOT_FOUND, "No current drawable: ret %d, error %lu\n", ret, GetLastError());

cleanup:
    wglMakeCurrent(NULL, NULL);
    for (i = 0; i < 2; ++i)
    {
        if (contexts[i]) wglDeleteContext(contexts[i]);
        if (dcs[i]) ReleaseDC(windows[i], dcs[i]);
        if (windows[i]) DestroyWindow(windows[i]);
    }
}
