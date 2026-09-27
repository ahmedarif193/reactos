/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A completed GL fence makes coherent buffer readback visible.
 */

#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <wine/test.h>

#ifndef GL_ARB_buffer_storage
#define GL_MAP_PERSISTENT_BIT 0x0040
#define GL_MAP_COHERENT_BIT 0x0080
#define GL_DYNAMIC_STORAGE_BIT 0x0100
#define GL_CLIENT_STORAGE_BIT 0x0200
typedef void (APIENTRYP PFNGLBUFFERSTORAGEPROC)(GLenum, GLsizeiptr, const void *, GLbitfield);
#endif

#define WIDTH 640
#define HEIGHT 480
#define FRAME_COUNT 128
#define IMAGE_SIZE (WIDTH * HEIGHT * sizeof(DWORD))

static const struct
{
    const char *name;
    GLsizeiptr size;
    unsigned int offset;
    GLbitfield extra_flags;
    BOOL remap;
    BOOL flush_explicit;
    BOOL ordinary_storage;
    BOOL ordinary_map;
    BOOL map_before_fence;
}
buffer_tests[] =
{
    {"read-only", IMAGE_SIZE, 0, 0, FALSE, FALSE, FALSE, FALSE, FALSE},
    {"read-only-remap", IMAGE_SIZE, 0, 0, TRUE, FALSE, FALSE, FALSE, FALSE},
    {"read-write-remap", IMAGE_SIZE, 0, GL_MAP_WRITE_BIT, TRUE, FALSE, FALSE, FALSE, FALSE},
    {"read-write-flush-remap", IMAGE_SIZE, 0, GL_MAP_WRITE_BIT, TRUE, TRUE, FALSE, FALSE, FALSE},
    {"client-read-only-remap", IMAGE_SIZE, 0, GL_CLIENT_STORAGE_BIT, TRUE, FALSE, FALSE, FALSE, FALSE},
    {"client-no-flush-remap", IMAGE_SIZE, 0, GL_MAP_WRITE_BIT | GL_CLIENT_STORAGE_BIT, TRUE, FALSE, FALSE, FALSE, FALSE},
    {"client", IMAGE_SIZE, 0, GL_MAP_WRITE_BIT | GL_CLIENT_STORAGE_BIT, TRUE, TRUE, FALSE, FALSE, FALSE},
    {"client-persistent", IMAGE_SIZE, 0, GL_MAP_WRITE_BIT | GL_CLIENT_STORAGE_BIT, FALSE, TRUE, FALSE, FALSE, FALSE},
    {"chunk", 64 * 1024 * 1024, 0, GL_MAP_WRITE_BIT | GL_CLIENT_STORAGE_BIT, TRUE, TRUE, FALSE, FALSE, FALSE},
    {"chunk-offset", 64 * 1024 * 1024, 5 * 1024 * 1024, GL_MAP_WRITE_BIT | GL_CLIENT_STORAGE_BIT, TRUE, TRUE, FALSE, FALSE, FALSE},
    {"chunk-persistent", 64 * 1024 * 1024, 5 * 1024 * 1024, GL_MAP_WRITE_BIT | GL_CLIENT_STORAGE_BIT, FALSE, TRUE, FALSE, FALSE, FALSE},
    {"coherent-store-ordinary-map", IMAGE_SIZE, 0, 0, TRUE, FALSE, FALSE, TRUE, FALSE},
    {"ordinary-store-and-map", IMAGE_SIZE, 0, 0, TRUE, FALSE, TRUE, TRUE, FALSE},
    {"remap-before-fence", IMAGE_SIZE, 0, 0, TRUE, FALSE, FALSE, FALSE, TRUE},
};

START_TEST(wgl_fence_readback)
{
    PFNGLGENBUFFERSPROC pglGenBuffers = NULL;
    PFNGLBINDBUFFERPROC pglBindBuffer = NULL;
    PFNGLBUFFERSTORAGEPROC pglBufferStorage = NULL;
    PFNGLMAPBUFFERRANGEPROC pglMapBufferRange = NULL;
    PFNGLUNMAPBUFFERPROC pglUnmapBuffer = NULL;
    PFNGLDELETEBUFFERSPROC pglDeleteBuffers = NULL;
    PFNGLFENCESYNCPROC pglFenceSync = NULL;
    PFNGLCLIENTWAITSYNCPROC pglClientWaitSync = NULL;
    PFNGLDELETESYNCPROC pglDeleteSync = NULL;
    PFNGLGENFRAMEBUFFERSPROC pglGenFramebuffers = NULL;
    PFNGLBINDFRAMEBUFFERPROC pglBindFramebuffer = NULL;
    PFNGLFRAMEBUFFERTEXTURE2DPROC pglFramebufferTexture2D = NULL;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC pglCheckFramebufferStatus = NULL;
    PFNGLDELETEFRAMEBUFFERSPROC pglDeleteFramebuffers = NULL;
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1};
    HWND window = NULL;
    HDC dc = NULL;
    HGLRC context = NULL;
    GLuint texture = 0, buffer = 0, framebuffer = 0;
    GLsync fence = NULL;
    volatile DWORD *mapped = NULL;
    GLbitfield flags, map_flags;
    unsigned int test, frame, x, y, stale_frames, finish_stale_frames;
    DWORD expected, samples[16];
    GLenum result, error;
    const char *version, *extensions, *storage_extension;
    unsigned int major = 0, minor = 0;
    BOOL ret;
    int format;

    window = CreateWindowA("static", "fenced readback", WS_POPUP,
            0, 0, WIDTH, HEIGHT, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!window) goto cleanup;
    dc = GetDC(window);
    ok(dc != NULL, "GetDC failed: %lu\n", GetLastError());
    if (!dc) goto cleanup;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
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
    version = (const char *)glGetString(GL_VERSION);
    trace("Renderer: %s; version: %s\n", glGetString(GL_RENDERER), version);
    if (version) sscanf(version, "%u.%u", &major, &minor);
    extensions = (const char *)glGetString(GL_EXTENSIONS);
    storage_extension = extensions ? strstr(extensions, "GL_ARB_buffer_storage") : NULL;
    if (!(major > 4 || (major == 4 && minor >= 4)) &&
            !(storage_extension &&
              (storage_extension == extensions || storage_extension[-1] == ' ') &&
              (storage_extension[21] == ' ' || storage_extension[21] == '\0')))
    {
        skip("OpenGL 4.4 / ARB_buffer_storage unavailable\n");
        goto cleanup;
    }

#define LOAD(name) do { p##name = (void *)wglGetProcAddress(#name); \
    if ((ULONG_PTR)p##name <= 3 || (ULONG_PTR)p##name == ~(ULONG_PTR)0) \
    { skip(#name " unavailable\n"); goto cleanup; } } while (0)
    LOAD(glGenBuffers);
    LOAD(glBindBuffer);
    LOAD(glBufferStorage);
    LOAD(glMapBufferRange);
    LOAD(glUnmapBuffer);
    LOAD(glDeleteBuffers);
    LOAD(glFenceSync);
    LOAD(glClientWaitSync);
    LOAD(glDeleteSync);
    LOAD(glGenFramebuffers);
    LOAD(glBindFramebuffer);
    LOAD(glFramebufferTexture2D);
    LOAD(glCheckFramebufferStatus);
    LOAD(glDeleteFramebuffers);
#undef LOAD

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    pglGenFramebuffers(1, &framebuffer);
    pglBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    result = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
    ok(result == GL_FRAMEBUFFER_COMPLETE, "Framebuffer status %#x\n", result);
    if (result != GL_FRAMEBUFFER_COMPLETE) goto cleanup;
    glDisable(GL_DITHER);
    for (test = 0; test < sizeof(buffer_tests) / sizeof(buffer_tests[0]); ++test)
    {
        /* Compare remapping with keeping an otherwise identical coherent
         * buffer mapped. A completed fence must expose the GPU writes in
         * both cases, independently of client-storage and write hints. */
        stale_frames = finish_stale_frames = 0;
        flags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | buffer_tests[test].extra_flags;
        if (buffer_tests[test].ordinary_storage)
            flags &= ~(GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
        map_flags = flags & ~GL_CLIENT_STORAGE_BIT;
        if (buffer_tests[test].ordinary_map)
            map_flags &= ~(GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
        if (buffer_tests[test].flush_explicit) map_flags |= GL_MAP_FLUSH_EXPLICIT_BIT;
        pglGenBuffers(1, &buffer);
        pglBindBuffer(GL_ARRAY_BUFFER, buffer);
        pglBufferStorage(GL_ARRAY_BUFFER, buffer_tests[test].size, NULL, flags | GL_DYNAMIC_STORAGE_BIT);
        pglBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        if (!buffer_tests[test].remap)
        {
            mapped = pglMapBufferRange(GL_ARRAY_BUFFER, 0, buffer_tests[test].size, map_flags);
            ok(mapped != NULL, "%s: Persistent map failed, GL error %#x\n", buffer_tests[test].name, glGetError());
            if (!mapped) goto cleanup;
        }
        for (frame = 0; frame < FRAME_COUNT; ++frame)
        {
            unsigned int stale = 0, finish_stale = 0, first_stale = 0;

            expected = 0xff000000 | ((frame + 1) * 0x010307u & 0xffffff);
            glClearColor((expected & 0xff) / 255.0f, ((expected >> 8) & 0xff) / 255.0f,
                    ((expected >> 16) & 0xff) / 255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void *)(ULONG_PTR)buffer_tests[test].offset);
            /* Mapping before the fence is a separate ordering control. Do not
             * replace the map-after-wait cases: those must also be correct. */
            if (buffer_tests[test].map_before_fence)
            {
                mapped = pglMapBufferRange(GL_ARRAY_BUFFER, 0, buffer_tests[test].size, map_flags);
                ok(mapped != NULL, "%s frame %u: Map failed, GL error %#x\n", buffer_tests[test].name, frame, glGetError());
                if (!mapped) goto cleanup;
            }
            fence = pglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            ok(fence != NULL, "Frame %u: FenceSync failed\n", frame);
            if (!fence) break;
            result = pglClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, ~(GLuint64)0 >> 1);
            ok(result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED,
                    "Frame %u: ClientWaitSync returned %#x\n", frame, result);
            if (result != GL_ALREADY_SIGNALED && result != GL_CONDITION_SATISFIED) break;

            if (buffer_tests[test].remap && !buffer_tests[test].map_before_fence)
            {
                mapped = pglMapBufferRange(GL_ARRAY_BUFFER, 0, buffer_tests[test].size, map_flags);
                ok(mapped != NULL, "%s frame %u: Map failed, GL error %#x\n", buffer_tests[test].name, frame, glGetError());
                if (!mapped) goto cleanup;
            }

            /* Snapshot all samples before reporting: console I/O must not give an
             * incomplete transfer time to finish between individual checks. */
            for (y = 0; y < 4; ++y)
                for (x = 0; x < 4; ++x)
                    samples[y * 4 + x] = mapped[buffer_tests[test].offset / sizeof(DWORD) + (60 + y * 120) * WIDTH + 80 + x * 160];
            glFinish();
            for (y = 0; y < 4; ++y)
                for (x = 0; x < 4; ++x)
                {
                    if (samples[y * 4 + x] != expected)
                    {
                        if (!stale) first_stale = y * 4 + x;
                        ++stale;
                    }
                    finish_stale += mapped[buffer_tests[test].offset / sizeof(DWORD) + (60 + y * 120) * WIDTH + 80 + x * 160] != expected;
                }
            stale_frames += !!stale;
            finish_stale_frames += !!finish_stale;
            ok(!stale, "%s frame %u: %u stale samples after fence, sample %u = %#lx, expected %#lx\n",
                    buffer_tests[test].name, frame, stale, first_stale, samples[first_stale], expected);
            ok(!finish_stale, "%s frame %u: %u stale samples after Finish\n", buffer_tests[test].name, frame, finish_stale);
            if (buffer_tests[test].remap)
            {
                pglUnmapBuffer(GL_ARRAY_BUFFER);
                mapped = NULL;
            }
            pglDeleteSync(fence);
            fence = NULL;
            error = glGetError();
            ok(error == GL_NO_ERROR, "Frame %u: GL error %#x\n", frame, error);
            if (error != GL_NO_ERROR) break;
        }
        trace("%s: %u frames, %u stale after fence, %u stale after Finish\n",
                buffer_tests[test].name, frame, stale_frames, finish_stale_frames);
        if (mapped) pglUnmapBuffer(GL_ARRAY_BUFFER);
        mapped = NULL;
        pglDeleteBuffers(1, &buffer);
        buffer = 0;
        if (fence) goto cleanup;
    }

cleanup:
    if (fence) pglDeleteSync(fence);
    if (mapped) pglUnmapBuffer(GL_ARRAY_BUFFER);
    if (buffer) pglDeleteBuffers(1, &buffer);
    if (framebuffer) pglDeleteFramebuffers(1, &framebuffer);
    if (texture) glDeleteTextures(1, &texture);
    if (context)
    {
        wglMakeCurrent(NULL, NULL);
        ret = wglDeleteContext(context);
        ok(ret, "wglDeleteContext failed: %lu\n", GetLastError());
    }
    if (dc) ReleaseDC(window, dc);
    if (window) DestroyWindow(window);
}
