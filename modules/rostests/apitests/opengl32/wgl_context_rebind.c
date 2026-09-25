/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Moving a current context between drawables must release the old binding.
 */

#include <windows.h>
#include <GL/gl.h>
#include <wine/test.h>

typedef BOOL (WINAPI *MAKE_CURRENT_READ)(HDC, HDC, HGLRC);

static void test_rebind(BOOL use_arb)
{
    PIXELFORMATDESCRIPTOR pfd = {0};
    HWND window[2] = {NULL, NULL};
    HDC dc[2] = {NULL, NULL};
    HGLRC context = NULL;
    MAKE_CURRENT_READ make_read;
    unsigned int i, round, target;
    int format;
    BOOL ret;
    GLenum error;

    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    for (i = 0; i < 2; ++i)
    {
        window[i] = CreateWindowA("static", "WGL context lifetime", WS_POPUP,
                0, 0, 64, 64, NULL, NULL, NULL, NULL);
        ok(window[i] != NULL, "CreateWindow(%u) failed: %lu\n", i, GetLastError());
        if (!window[i]) goto cleanup;
        dc[i] = GetDC(window[i]);
        ok(dc[i] != NULL, "GetDC(%u) failed: %lu\n", i, GetLastError());
        if (!dc[i]) goto cleanup;
        format = ChoosePixelFormat(dc[i], &pfd);
        ok(format != 0, "ChoosePixelFormat(%u) failed: %lu\n", i, GetLastError());
        if (!format) goto cleanup;
        ret = SetPixelFormat(dc[i], format, &pfd);
        ok(ret, "SetPixelFormat(%u) failed: %lu\n", i, GetLastError());
        if (!ret) goto cleanup;
    }

    for (round = 0; round < 8; ++round)
    {
        context = wglCreateContext(dc[0]);
        ok(context != NULL, "round %u: create context failed: %lu\n", round, GetLastError());
        if (!context) goto cleanup;
        ret = wglMakeCurrent(dc[0], context);
        ok(ret, "round %u: initial bind failed: %lu\n", round, GetLastError());
        if (!ret) goto cleanup;
        if (!round)
            trace("ARB=%u renderer=%s\n", use_arb, glGetString(GL_RENDERER));
        make_read = (MAKE_CURRENT_READ)(void *)wglGetProcAddress("wglMakeContextCurrentARB");
        if (use_arb && !make_read)
        {
            skip("WGL_ARB_make_current_read is unavailable\n");
            goto cleanup;
        }

        for (i = 0; i < 8; ++i)
        {
            target = i & 1;
            /* Deliberately keep the context current while changing the DC. */
            ret = use_arb ? make_read(dc[target], dc[target], context) :
                           wglMakeCurrent(dc[target], context);
            ok(ret, "ARB=%u round %u move %u failed: %lu\n", use_arb, round, i, GetLastError());
            if (!ret) goto cleanup;
            ok(wglGetCurrentContext() == context, "round %u move %u: wrong context\n", round, i);
            ok(wglGetCurrentDC() == dc[target], "round %u move %u: wrong DC\n", round, i);
            glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            error = glGetError();
            ok(error == GL_NO_ERROR, "round %u move %u: GL error %#x\n", round, i, error);
        }
        ret = wglMakeCurrent(NULL, NULL);
        ok(ret, "round %u: unbind failed: %lu\n", round, GetLastError());
        ret = wglDeleteContext(context);
        ok(ret, "round %u: delete context failed: %lu\n", round, GetLastError());
        if (!ret) goto cleanup;
        context = NULL;
    }

cleanup:
    if (context)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(context);
    }
    for (i = 0; i < 2; ++i)
    {
        if (dc[i]) ReleaseDC(window[i], dc[i]);
        if (window[i]) DestroyWindow(window[i]);
    }
}

START_TEST(wgl_context_rebind)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_rebind(FALSE);
    test_rebind(TRUE);
}
