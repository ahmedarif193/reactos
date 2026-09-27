/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A clipped single-buffer present must not lose the GL context.
 */

#include <windows.h>
#include <GL/gl.h>
#include <wine/test.h>

static void test_window(BOOL child, BOOL double_buffer)
{
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1}, actual;
    HWND parent = NULL, window = NULL;
    HDC dc = NULL;
    HGLRC context = NULL;
    GLuint texture = 0;
    DWORD input[16], output[16];
    unsigned int phase, i;
    int format;
    BOOL ret;
    GLenum error;

    if (child)
    {
        parent = CreateWindowA("static", "hidden GL parent", WS_POPUP,
                80, 80, 40, 40, NULL, NULL, NULL, NULL);
        ok(parent != NULL, "CreateWindow(parent) failed: %lu\n", GetLastError());
        if (!parent) goto cleanup;
    }
    window = CreateWindowA("static", "hidden GL present",
            child ? WS_CHILD | WS_VISIBLE : WS_POPUP,
            child ? 0 : 80, child ? 0 : 80, 32, 32, parent, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!window) goto cleanup;
    dc = GetDC(window);
    ok(dc != NULL, "GetDC failed: %lu\n", GetLastError());
    if (!dc) goto cleanup;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    if (double_buffer) pfd.dwFlags |= PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    format = ChoosePixelFormat(dc, &pfd);
    ok(format != 0, "ChoosePixelFormat failed: %lu\n", GetLastError());
    if (!format) goto cleanup;
    ret = DescribePixelFormat(dc, format, sizeof(actual), &actual);
    ok(ret, "DescribePixelFormat failed: %lu\n", GetLastError());
    ok(!!(actual.dwFlags & PFD_DOUBLEBUFFER) == double_buffer,
            "Requested double buffer %u, got flags %#lx\n", double_buffer, actual.dwFlags);
    ret = SetPixelFormat(dc, format, &pfd);
    ok(ret, "SetPixelFormat failed: %lu\n", GetLastError());
    if (!ret) goto cleanup;
    context = wglCreateContext(dc);
    ok(context != NULL, "wglCreateContext failed: %lu\n", GetLastError());
    if (!context) goto cleanup;
    ret = wglMakeCurrent(dc, context);
    ok(ret, "wglMakeCurrent failed: %lu\n", GetLastError());
    if (!ret) goto cleanup;
    trace("child=%u double_buffer=%u renderer=%s version=%s\n",
            child, double_buffer, glGetString(GL_RENDERER), glGetString(GL_VERSION));
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (phase = 0; phase < 4; ++phase)
    {
        HWND target = child ? parent : window;
        if (phase) ShowWindow(target, phase & 1 ? SW_SHOWNOACTIVATE : SW_HIDE);
        trace("child=%u double_buffer=%u phase=%u visible=%u before glFinish\n",
                child, double_buffer, phase, IsWindowVisible(window));
        for (i = 0; i < 16; ++i) input[i] = 0xff113377u + phase * 0x000b0305u;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, input);
        glFinish();
        error = glGetError();
        ok(error == GL_NO_ERROR, "phase %u: glFinish error %#x\n", phase, error);
        /* Texture readback is independent of window pixel ownership. The
         * hidden front buffer itself has no defined visible pixels. */
        memset(output, 0, sizeof(output));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, output);
        error = glGetError();
        ok(error == GL_NO_ERROR, "phase %u: readback error %#x\n", phase, error);
        for (i = 0; i < 16; ++i)
            ok(output[i] == input[i], "phase %u pixel %u: %#lx expected %#lx\n",
                    phase, i, output[i], input[i]);
    }

cleanup:
    if (texture) glDeleteTextures(1, &texture);
    if (context)
    {
        wglMakeCurrent(NULL, NULL);
        ret = wglDeleteContext(context);
        ok(ret, "wglDeleteContext failed: %lu\n", GetLastError());
    }
    if (dc) ReleaseDC(window, dc);
    if (window) DestroyWindow(window);
    if (parent) DestroyWindow(parent);
}

START_TEST(wgl_hidden_present)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_window(FALSE, TRUE);
    test_window(TRUE, TRUE);
    test_window(FALSE, FALSE);
    test_window(TRUE, FALSE);
}
