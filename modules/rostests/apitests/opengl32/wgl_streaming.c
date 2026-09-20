/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Repeated streamed vertex uploads remain visible to the GPU.
 */

#include <windows.h>
#include <GL/gl.h>
#include <wine/test.h>

#define WIDTH 800
#define HEIGHT 96
#define CELLS 72

START_TEST(wgl_streaming)
{
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1 };
    HWND window = NULL;
    HDC dc = NULL;
    HGLRC context = NULL;
    BYTE *pixels = NULL;
    BOOL (WINAPI *set_interval)(int);
    unsigned frame, band, cell, wrong, x, y, value;
    int format;
    BOOL ret;

    window = CreateWindowA("static", "streamed vertices", WS_POPUP,
                           0, 0, WIDTH, HEIGHT, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!window) goto cleanup;
    dc = GetDC(window);
    ok(dc != NULL, "GetDC failed: %lu\n", GetLastError());
    if (!dc) goto cleanup;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    format = ChoosePixelFormat(dc, &pfd);
    ok(format != 0, "ChoosePixelFormat failed: %lu\n", GetLastError());
    if (!format) goto cleanup;
    ret = SetPixelFormat(dc, format, &pfd);
    ok(ret, "SetPixelFormat failed: %lu\n", GetLastError());
    if (!ret) goto cleanup;
    context = wglCreateContext(dc);
    ok(context != NULL, "wglCreateContext failed: %lu\n", GetLastError());
    if (!context) goto cleanup;
    ret = wglMakeCurrent(dc, context);
    ok(ret, "wglMakeCurrent failed: %lu\n", GetLastError());
    if (!ret) goto cleanup;
    pixels = HeapAlloc(GetProcessHeap(), 0, WIDTH * HEIGHT * 3);
    ok(pixels != NULL, "Pixel allocation failed\n");
    if (!pixels) goto cleanup;
    trace("Renderer: %s\n", glGetString(GL_RENDERER));
    set_interval = (void *)wglGetProcAddress("wglSwapIntervalEXT");
    if (set_interval) set_interval(0);
    glViewport(0, 0, WIDTH, HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WIDTH, 0, HEIGHT, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DITHER);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    /* Changing colors and enough vertices to cross multiple upload-buffer
     * boundaries detect stale uploads. Read the producer's back buffer so
     * compositor damage, window visibility and scanout cannot hide a failure. */
    for (frame = 1; frame <= 500; ++frame)
    {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        for (band = 0; band < 3; ++band)
        {
            for (cell = 0; cell < CELLS; ++cell)
            {
                value = ((frame * 2654435761u) >> (cell % 24)) & 1;
                glColor3ub(value ? 224 : 32, value ? 224 : 32, value ? 224 : 32);
                x = 76 + cell * 9;
                y = 8 + band * 32;
                glVertex2i(x, y);
                glVertex2i(x + 9, y);
                glVertex2i(x + 9, y + 16);
                glVertex2i(x, y + 16);
            }
        }
        glEnd();
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, pixels);
        wrong = 0;
        for (band = 0; band < 3; ++band)
        {
            for (cell = 0; cell < CELLS; ++cell)
            {
                BYTE *pixel = pixels + ((16 + band * 32) * WIDTH + 80 + cell * 9) * 3;
                value = ((frame * 2654435761u) >> (cell % 24)) & 1;
                if ((pixel[0] > 128) != value ||
                    (pixel[1] > 128) != value || (pixel[2] > 128) != value)
                    ++wrong;
            }
        }
        ok(!wrong, "Frame %u has %u stale or corrupt cells\n", frame, wrong);
        ret = SwapBuffers(dc);
        if (!ret)
        {
            ok(ret, "SwapBuffers failed on frame %u: %lu\n", frame, GetLastError());
            break;
        }
    }
    ok(glGetError() == GL_NO_ERROR, "OpenGL error during streaming\n");

cleanup:
    HeapFree(GetProcessHeap(), 0, pixels);
    wglMakeCurrent(NULL, NULL);
    if (context) wglDeleteContext(context);
    if (dc) ReleaseDC(window, dc);
    if (window) DestroyWindow(window);
}
