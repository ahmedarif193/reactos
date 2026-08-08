/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-2.0-or-later
 * PURPOSE:         Cursor realization and presentation parity tests
 */

#include "precomp.h"

#define TEST_CURSOR_WIDTH 32
#define TEST_CURSOR_HEIGHT 32
#define MOVE_SAMPLE_COUNT 64
#define SWITCH_SAMPLE_COUNT 96
#define HIGH_RESOLUTION_PIXELS (2560ULL * 1440ULL)
#define WIN11_P95_LATENCY_BUDGET_US 20000

struct cursor_shape
{
    const char *name;
    HCURSOR cursor;
    BOOL has_color;
    BOOL has_alpha;
};

struct latency_stats
{
    ULONGLONG samples[MOVE_SAMPLE_COUNT > SWITCH_SAMPLE_COUNT ? MOVE_SAMPLE_COUNT : SWITCH_SAMPLE_COUNT];
    ULONGLONG total;
    UINT count;
};

struct paint_state
{
    HWND hwnd;
    HANDLE ready_event;
    HANDLE thread;
    volatile LONG stop;
    volatile LONG operations;
    volatile LONG failures;
    volatile LONG in_paint;
};

static const char cursor_window_class[] = "CursorPresentationTestWindow";
static HCURSOR window_cursor;
static LARGE_INTEGER performance_frequency;
static BOOL high_resolution_mode;

static void pump_messages(void)
{
    MSG msg;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static LRESULT CALLBACK cursor_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    PAINTSTRUCT paint;

    switch (message)
    {
        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;

        case WM_SETCURSOR:
            if (window_cursor)
            {
                SetCursor(window_cursor);
                return TRUE;
            }
            break;
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static HWND create_cursor_window(void)
{
    WNDCLASSA class_info;
    HINSTANCE instance = GetModuleHandleA(NULL);
    HWND hwnd;
    int x, y, width, height;

    ZeroMemory(&class_info, sizeof(class_info));
    class_info.lpfnWndProc = cursor_window_proc;
    class_info.hInstance = instance;
    class_info.hCursor = NULL;
    class_info.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    class_info.lpszClassName = cursor_window_class;
    if (!RegisterClassA(&class_info) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    x = 0;
    y = 0;
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, cursor_window_class, "Cursor presentation parity", WS_POPUP, x, y, width, height, NULL, NULL, instance, NULL);
    if (!hwnd)
        return NULL;

    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    UpdateWindow(hwnd);
    pump_messages();
    return hwnd;
}

static HCURSOR create_monochrome_cursor(void)
{
    BYTE and_mask[TEST_CURSOR_HEIGHT * TEST_CURSOR_WIDTH / 8];
    BYTE xor_mask[TEST_CURSOR_HEIGHT * TEST_CURSOR_WIDTH / 8];
    UINT row;

    memset(and_mask, 0xff, sizeof(and_mask));
    memset(xor_mask, 0, sizeof(xor_mask));
    for (row = 0; row < TEST_CURSOR_HEIGHT; ++row)
    {
        xor_mask[row * TEST_CURSOR_WIDTH / 8 + row / 8] |= 0x80 >> (row & 7);
        xor_mask[row * TEST_CURSOR_WIDTH / 8 + (TEST_CURSOR_WIDTH - 1 - row) / 8] |= 0x80 >> ((TEST_CURSOR_WIDTH - 1 - row) & 7);
    }
    return CreateCursor(GetModuleHandleA(NULL), 1, 1, TEST_CURSOR_WIDTH, TEST_CURSOR_HEIGHT, and_mask, xor_mask);
}

static HCURSOR create_color_cursor(BOOL alpha)
{
    BITMAPINFO bitmap_info;
    ICONINFO icon_info;
    BYTE mask_bits[TEST_CURSOR_HEIGHT * TEST_CURSOR_WIDTH / 8];
    HBITMAP color_bitmap = NULL, mask_bitmap = NULL;
    HDC screen_dc = NULL, memory_dc = NULL;
    HGDIOBJ old_bitmap = NULL;
    DWORD *pixels = NULL;
    HCURSOR cursor = NULL;
    RECT rect = { 0, 0, TEST_CURSOR_WIDTH, TEST_CURSOR_HEIGHT };
    UINT x, y;

    memset(mask_bits, 0, sizeof(mask_bits));
    mask_bitmap = CreateBitmap(TEST_CURSOR_WIDTH, TEST_CURSOR_HEIGHT, 1, 1, mask_bits);
    if (!mask_bitmap)
        goto cleanup;

    screen_dc = GetDC(NULL);
    if (!screen_dc)
        goto cleanup;

    if (alpha)
    {
        ZeroMemory(&bitmap_info, sizeof(bitmap_info));
        bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
        bitmap_info.bmiHeader.biWidth = TEST_CURSOR_WIDTH;
        bitmap_info.bmiHeader.biHeight = -TEST_CURSOR_HEIGHT;
        bitmap_info.bmiHeader.biPlanes = 1;
        bitmap_info.bmiHeader.biBitCount = 32;
        bitmap_info.bmiHeader.biCompression = BI_RGB;
        color_bitmap = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
        if (!color_bitmap)
            goto cleanup;

        for (y = 0; y < TEST_CURSOR_HEIGHT; ++y)
        {
            for (x = 0; x < TEST_CURSOR_WIDTH; ++x)
            {
                BYTE opacity = (x > 3 && x < TEST_CURSOR_WIDTH - 4 && y > 3 && y < TEST_CURSOR_HEIGHT - 4) ? 0xc0 : 0x60;
                pixels[y * TEST_CURSOR_WIDTH + x] = ((DWORD)opacity << 24) | ((DWORD)(opacity / 2) << 16) | ((DWORD)(opacity / 3) << 8) | opacity / 4;
            }
        }
    }
    else
    {
        color_bitmap = CreateCompatibleBitmap(screen_dc, TEST_CURSOR_WIDTH, TEST_CURSOR_HEIGHT);
        memory_dc = CreateCompatibleDC(screen_dc);
        if (!color_bitmap || !memory_dc)
            goto cleanup;
        old_bitmap = SelectObject(memory_dc, color_bitmap);
        FillRect(memory_dc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SetTextColor(memory_dc, RGB(0, 0, 0));
        SetBkColor(memory_dc, RGB(255, 255, 255));
        MoveToEx(memory_dc, 0, 0, NULL);
        LineTo(memory_dc, TEST_CURSOR_WIDTH, TEST_CURSOR_HEIGHT);
        MoveToEx(memory_dc, TEST_CURSOR_WIDTH - 1, 0, NULL);
        LineTo(memory_dc, 0, TEST_CURSOR_HEIGHT - 1);
        SelectObject(memory_dc, old_bitmap);
        old_bitmap = NULL;
    }

    ZeroMemory(&icon_info, sizeof(icon_info));
    icon_info.fIcon = FALSE;
    icon_info.xHotspot = 1;
    icon_info.yHotspot = 1;
    icon_info.hbmMask = mask_bitmap;
    icon_info.hbmColor = color_bitmap;
    cursor = CreateIconIndirect(&icon_info);

cleanup:
    if (old_bitmap)
        SelectObject(memory_dc, old_bitmap);
    if (memory_dc)
        DeleteDC(memory_dc);
    if (screen_dc)
        ReleaseDC(NULL, screen_dc);
    if (color_bitmap)
        DeleteObject(color_bitmap);
    if (mask_bitmap)
        DeleteObject(mask_bitmap);
    return cursor;
}

static void test_cursor_bitmap_contract(const struct cursor_shape *shape)
{
    ICONINFO icon_info;
    BITMAP bitmap;
    BOOL ret;

    ZeroMemory(&icon_info, sizeof(icon_info));
    ret = GetIconInfo(shape->cursor, &icon_info);
    ok(ret, "%s: GetIconInfo failed, error %lu\n", shape->name, GetLastError());
    if (!ret)
        return;

    ok(!icon_info.fIcon, "%s: cursor was reported as an icon\n", shape->name);
    ok(icon_info.xHotspot == 1 && icon_info.yHotspot == 1, "%s: hotspot is %lu,%lu instead of 1,1\n", shape->name, icon_info.xHotspot, icon_info.yHotspot);
    ok(icon_info.hbmMask != NULL, "%s: GetIconInfo returned no mask bitmap\n", shape->name);
    ok(!!icon_info.hbmColor == shape->has_color, "%s: color bitmap presence is %d, expected %d\n", shape->name, !!icon_info.hbmColor, shape->has_color);

    if (icon_info.hbmMask && GetObjectA(icon_info.hbmMask, sizeof(bitmap), &bitmap) == sizeof(bitmap))
    {
        ok(bitmap.bmWidth == TEST_CURSOR_WIDTH, "%s: mask width is %ld, expected %d\n", shape->name, bitmap.bmWidth, TEST_CURSOR_WIDTH);
        ok(bitmap.bmHeight == (shape->has_color ? TEST_CURSOR_HEIGHT : TEST_CURSOR_HEIGHT * 2), "%s: mask height is %ld, expected %d\n", shape->name, bitmap.bmHeight, shape->has_color ? TEST_CURSOR_HEIGHT : TEST_CURSOR_HEIGHT * 2);
        ok(bitmap.bmBitsPixel == 1, "%s: mask depth is %u, expected 1\n", shape->name, bitmap.bmBitsPixel);
    }
    else
    {
        ok(FALSE, "%s: failed to query the mask bitmap\n", shape->name);
    }

    if (icon_info.hbmColor && GetObjectA(icon_info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap))
    {
        ok(bitmap.bmWidth == TEST_CURSOR_WIDTH && bitmap.bmHeight == TEST_CURSOR_HEIGHT, "%s: color bitmap is %ldx%ld, expected %dx%d\n", shape->name, bitmap.bmWidth, bitmap.bmHeight, TEST_CURSOR_WIDTH, TEST_CURSOR_HEIGHT);
        if (shape->has_alpha)
            ok(bitmap.bmBitsPixel == 32, "%s: alpha bitmap depth is %u, expected 32\n", shape->name, bitmap.bmBitsPixel);
    }

    if (icon_info.hbmMask)
        DeleteObject(icon_info.hbmMask);
    if (icon_info.hbmColor)
        DeleteObject(icon_info.hbmColor);
}

static ULONGLONG qpc_microseconds(LARGE_INTEGER start, LARGE_INTEGER end)
{
    return (ULONGLONG)((end.QuadPart - start.QuadPart) * 1000000 / performance_frequency.QuadPart);
}

static void record_latency(struct latency_stats *stats, LARGE_INTEGER start, LARGE_INTEGER end)
{
    ULONGLONG value = qpc_microseconds(start, end);

    stats->samples[stats->count++] = value;
    stats->total += value;
}

static void report_latency(const char *shape, const char *phase, struct latency_stats *stats, LONG paint_operations, UINT paint_active_samples)
{
    ULONGLONG sorted[MOVE_SAMPLE_COUNT > SWITCH_SAMPLE_COUNT ? MOVE_SAMPLE_COUNT : SWITCH_SAMPLE_COUNT];
    ULONGLONG value;
    UINT i, j, p95;

    memcpy(sorted, stats->samples, stats->count * sizeof(sorted[0]));
    for (i = 1; i < stats->count; ++i)
    {
        value = sorted[i];
        for (j = i; j && sorted[j - 1] > value; --j)
            sorted[j] = sorted[j - 1];
        sorted[j] = value;
    }

    p95 = (stats->count * 95 + 99) / 100 - 1;
    trace("cursor-parity: shape=%s phase=%s samples=%u min_us=%llu median_us=%llu p95_us=%llu max_us=%llu mean_us=%llu paint_ops=%ld paint_active_samples=%u\n", shape, phase, stats->count, (unsigned long long)sorted[0], (unsigned long long)sorted[stats->count / 2], (unsigned long long)sorted[p95], (unsigned long long)sorted[stats->count - 1], (unsigned long long)(stats->total / stats->count), paint_operations, paint_active_samples);
    if (high_resolution_mode)
        ok(sorted[p95] <= WIN11_P95_LATENCY_BUDGET_US, "%s/%s: p95 cursor latency %llu us exceeds the Win11 high-resolution budget of %u us\n", shape, phase, (unsigned long long)sorted[p95], WIN11_P95_LATENCY_BUDGET_US);
}

static DWORD WINAPI paint_thread_proc(void *parameter)
{
    struct paint_state *state = parameter;
    HDC dc;
    RECT rect;
    LONG operation;

    dc = GetDC(state->hwnd);
    if (!dc || !GetClientRect(state->hwnd, &rect))
    {
        InterlockedIncrement(&state->failures);
        SetEvent(state->ready_event);
        if (dc)
            ReleaseDC(state->hwnd, dc);
        return 1;
    }

    while (!InterlockedCompareExchange(&state->stop, 0, 0))
    {
        operation = InterlockedIncrement(&state->operations);
        InterlockedExchange(&state->in_paint, 1);
        if (operation == 1)
            SetEvent(state->ready_event);
        if (!PatBlt(dc, 0, 0, rect.right, rect.bottom, (operation & 1) ? BLACKNESS : WHITENESS))
            InterlockedIncrement(&state->failures);
        GdiFlush();
        InterlockedExchange(&state->in_paint, 0);
        if (!(operation & 7))
            Sleep(0);
    }

    ReleaseDC(state->hwnd, dc);
    return 0;
}

static BOOL start_paint_thread(struct paint_state *state, HWND hwnd)
{
    DWORD wait_result;

    ZeroMemory(state, sizeof(*state));
    state->hwnd = hwnd;
    state->ready_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    ok(state->ready_event != NULL, "CreateEvent failed, error %lu\n", GetLastError());
    if (!state->ready_event)
        return FALSE;

    state->thread = CreateThread(NULL, 0, paint_thread_proc, state, 0, NULL);
    ok(state->thread != NULL, "CreateThread failed, error %lu\n", GetLastError());
    if (!state->thread)
    {
        CloseHandle(state->ready_event);
        state->ready_event = NULL;
        return FALSE;
    }

    wait_result = WaitForSingleObject(state->ready_event, 10000);
    ok(wait_result == WAIT_OBJECT_0, "paint thread did not complete its first frame, wait result %#lx\n", wait_result);
    return wait_result == WAIT_OBJECT_0;
}

static LONG stop_paint_thread(struct paint_state *state)
{
    DWORD wait_result;
    LONG operations;

    InterlockedExchange(&state->stop, 1);
    wait_result = WaitForSingleObject(state->thread, 30000);
    ok(wait_result == WAIT_OBJECT_0, "paint thread did not stop, wait result %#lx\n", wait_result);
    operations = InterlockedCompareExchange(&state->operations, 0, 0);
    ok(!InterlockedCompareExchange(&state->failures, 0, 0), "paint thread had %ld GDI failures\n", InterlockedCompareExchange(&state->failures, 0, 0));
    CloseHandle(state->thread);
    CloseHandle(state->ready_event);
    state->thread = NULL;
    state->ready_event = NULL;
    return operations;
}

static BOOL wait_for_active_paint(struct paint_state *state)
{
    DWORD start = GetTickCount();

    while (!InterlockedCompareExchange(&state->in_paint, 0, 0) && GetTickCount() - start < 5000)
        Sleep(0);
    return !!InterlockedCompareExchange(&state->in_paint, 0, 0);
}

static void measure_cursor_moves(const char *shape, const char *phase, POINT first, POINT second, struct paint_state *paint)
{
    struct latency_stats stats = { { 0 }, 0, 0 };
    LARGE_INTEGER start, end;
    POINT actual, expected;
    UINT failures = 0, position_failures = 0, i;
    BOOL ret;
    LONG start_operations = 0, end_operations = 0;
    UINT paint_active_samples = 0;

    if (paint)
    {
        ok(wait_for_active_paint(paint), "%s/%s: no in-flight paint was observed before measurement\n", shape, phase);
        start_operations = InterlockedCompareExchange(&paint->operations, 0, 0);
    }

    for (i = 0; i < MOVE_SAMPLE_COUNT; ++i)
    {
        expected = (i & 1) ? first : second;
        if (paint && InterlockedCompareExchange(&paint->in_paint, 0, 0))
            ++paint_active_samples;
        QueryPerformanceCounter(&start);
        ret = SetCursorPos(expected.x, expected.y);
        QueryPerformanceCounter(&end);
        record_latency(&stats, start, end);
        if (!ret)
            ++failures;
        if (!GetCursorPos(&actual) || actual.x != expected.x || actual.y != expected.y)
            ++position_failures;
        if (!(i & 7))
        {
            pump_messages();
            Sleep(0);
        }
    }

    ok(!failures, "%s/%s: SetCursorPos failed %u times\n", shape, phase, failures);
    ok(!position_failures, "%s/%s: GetCursorPos disagreed %u times\n", shape, phase, position_failures);
    if (paint)
    {
        end_operations = InterlockedCompareExchange(&paint->operations, 0, 0);
        ok(paint_active_samples != 0, "%s/%s: no cursor calls overlapped an in-flight paint\n", shape, phase);
    }
    report_latency(shape, phase, &stats, end_operations - start_operations, paint_active_samples);
}

static void run_cursor_shape(HWND hwnd, const struct cursor_shape *shape, POINT adjacent_first, POINT adjacent_second, POINT far_first, POINT far_second)
{
    struct paint_state paint;
    BOOL painting;
    LONG operations;

    window_cursor = shape->cursor;
    SetCursor(shape->cursor);
    pump_messages();
    ok(GetCursor() == shape->cursor, "%s: GetCursor returned %p instead of %p\n", shape->name, GetCursor(), shape->cursor);

    measure_cursor_moves(shape->name, "adjacent-idle", adjacent_first, adjacent_second, NULL);
    measure_cursor_moves(shape->name, "teleport-idle", far_first, far_second, NULL);

    painting = start_paint_thread(&paint, hwnd);
    if (!painting)
    {
        if (paint.thread)
            stop_paint_thread(&paint);
        skip("%s: paint-contention measurements unavailable\n", shape->name);
        return;
    }

    measure_cursor_moves(shape->name, "adjacent-paint", adjacent_first, adjacent_second, &paint);
    measure_cursor_moves(shape->name, "teleport-paint", far_first, far_second, &paint);
    operations = stop_paint_thread(&paint);
    trace("cursor-parity: shape=%s phase=paint-worker-total paint_ops=%ld\n", shape->name, operations);
}

static void measure_cursor_switches(HWND hwnd, struct cursor_shape *shapes, UINT shape_count)
{
    struct latency_stats stats = { { 0 }, 0, 0 };
    struct paint_state paint;
    LARGE_INTEGER start, end;
    UINT failures = 0, i;
    UINT paint_active_samples = 0;
    BOOL painting;
    LONG operations;

    painting = start_paint_thread(&paint, hwnd);
    if (!painting)
    {
        if (paint.thread)
            stop_paint_thread(&paint);
        skip("cursor-switch paint-contention measurement unavailable\n");
        return;
    }
    ok(wait_for_active_paint(&paint), "no in-flight paint was observed before cursor shape changes\n");

    for (i = 0; i < SWITCH_SAMPLE_COUNT; ++i)
    {
        window_cursor = shapes[i % shape_count].cursor;
        if (InterlockedCompareExchange(&paint.in_paint, 0, 0))
            ++paint_active_samples;
        QueryPerformanceCounter(&start);
        SetCursor(window_cursor);
        QueryPerformanceCounter(&end);
        record_latency(&stats, start, end);
        if (GetCursor() != window_cursor)
            ++failures;
        if (!(i & 7))
            Sleep(0);
    }

    operations = stop_paint_thread(&paint);
    ok(!failures, "GetCursor disagreed after %u shape changes\n", failures);
    ok(paint_active_samples != 0, "no cursor shape changes overlapped an in-flight paint\n");
    report_latency("mixed", "shape-change-paint", &stats, operations, paint_active_samples);
}

static int tracked_show_cursor(BOOL show, int *show_delta)
{
    *show_delta += show ? 1 : -1;
    return ShowCursor(show);
}

static BOOL normalize_cursor_show_count(int *show_delta)
{
    int result, guard = 0;

    do
    {
        result = tracked_show_cursor(TRUE, show_delta);
    } while (result < 0 && ++guard < 128);
    if (result < 0)
        return FALSE;

    guard = 0;
    do
    {
        result = tracked_show_cursor(FALSE, show_delta);
    } while (result >= 0 && ++guard < 128);
    if (result >= 0)
        return FALSE;

    return tracked_show_cursor(TRUE, show_delta) == 0;
}

static void test_cursor_visibility(HCURSOR cursor, int *show_delta)
{
    CURSORINFO cursor_info;
    int count;
    BOOL ret;
    BOOL cursor_suppressed = FALSE;

    window_cursor = cursor;
    SetCursor(cursor);
    pump_messages();

    ZeroMemory(&cursor_info, sizeof(cursor_info));
    cursor_info.cbSize = sizeof(cursor_info);
    ret = GetCursorInfo(&cursor_info);
    ok(ret, "GetCursorInfo for a visible cursor failed, error %lu\n", GetLastError());
    if (ret)
    {
        cursor_suppressed = !!(cursor_info.flags & CURSOR_SUPPRESSED);
        if (cursor_suppressed)
            skip("cursor visibility is suppressed by the active input/display hardware path\n");
        else
            ok(cursor_info.flags & CURSOR_SHOWING, "visible cursor flags are %#lx\n", cursor_info.flags);
        ok(cursor_info.hCursor == cursor, "visible cursor handle is %p, expected %p\n", cursor_info.hCursor, cursor);
    }

    count = tracked_show_cursor(FALSE, show_delta);
    ok(count == -1, "ShowCursor(FALSE) returned %d, expected -1\n", count);
    ZeroMemory(&cursor_info, sizeof(cursor_info));
    cursor_info.cbSize = sizeof(cursor_info);
    ret = GetCursorInfo(&cursor_info);
    ok(ret, "GetCursorInfo for a hidden cursor failed, error %lu\n", GetLastError());
    if (ret)
        ok(!(cursor_info.flags & CURSOR_SHOWING), "hidden cursor flags are %#lx\n", cursor_info.flags);

    count = tracked_show_cursor(TRUE, show_delta);
    ok(count == 0, "ShowCursor(TRUE) returned %d, expected 0\n", count);
    ZeroMemory(&cursor_info, sizeof(cursor_info));
    cursor_info.cbSize = sizeof(cursor_info);
    ret = GetCursorInfo(&cursor_info);
    ok(ret, "GetCursorInfo after showing the cursor failed, error %lu\n", GetLastError());
    if (ret)
    {
        if (cursor_info.flags & CURSOR_SUPPRESSED)
        {
            if (!cursor_suppressed)
                skip("cursor visibility became suppressed by the active input/display hardware path\n");
        }
        else
            ok(cursor_info.flags & CURSOR_SHOWING, "restored cursor flags are %#lx\n", cursor_info.flags);
        ok(cursor_info.hCursor == cursor, "restored cursor handle is %p, expected %p\n", cursor_info.hCursor, cursor);
    }
}

static void trace_display_environment(void)
{
    typedef LONG (WINAPI *DWM_IS_COMPOSITION_ENABLED)(BOOL *enabled);
    DWM_IS_COMPOSITION_ENABLED is_composition_enabled;
    DEVMODEA mode;
    HMODULE dwmapi;
    HDC screen_dc;
    BOOL composition_enabled = FALSE;
    LONG composition_result = -1;

    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode))
    {
        high_resolution_mode = (ULONGLONG)mode.dmPelsWidth * mode.dmPelsHeight >= HIGH_RESOLUTION_PIXELS;
        trace("cursor-parity: display=%lux%lu bpp=%lu frequency=%lu virtual=%dx%d origin=%d,%d cursor=%dx%d\n", mode.dmPelsWidth, mode.dmPelsHeight, mode.dmBitsPerPel, mode.dmDisplayFrequency, GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN), GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), GetSystemMetrics(SM_CXCURSOR), GetSystemMetrics(SM_CYCURSOR));
    }

    screen_dc = GetDC(NULL);
    if (screen_dc)
    {
        trace("cursor-parity: dpi=%dx%d technology=%d\n", GetDeviceCaps(screen_dc, LOGPIXELSX), GetDeviceCaps(screen_dc, LOGPIXELSY), GetDeviceCaps(screen_dc, TECHNOLOGY));
        ReleaseDC(NULL, screen_dc);
    }

    dwmapi = LoadLibraryA("dwmapi.dll");
    is_composition_enabled = dwmapi ? (DWM_IS_COMPOSITION_ENABLED)GetProcAddress(dwmapi, "DwmIsCompositionEnabled") : NULL;
    if (is_composition_enabled)
        composition_result = is_composition_enabled(&composition_enabled);
    trace("cursor-parity: DwmIsCompositionEnabled result=%ld enabled=%d\n", composition_result, composition_enabled);
    if (dwmapi)
        FreeLibrary(dwmapi);
}

START_TEST(CursorPresentation)
{
    struct cursor_shape shapes[] =
    {
        { "monochrome", NULL, FALSE, FALSE },
        { "color", NULL, TRUE, FALSE },
        { "alpha", NULL, TRUE, TRUE }
    };
    HCURSOR original_cursor;
    HMONITOR test_monitor;
    HDESK input_desktop;
    HWND hwnd = NULL;
    MONITORINFO monitor_info;
    POINT original_position, adjacent_first, adjacent_second, far_first, far_second;
    RECT original_clip;
    BOOL have_original_position, have_original_clip;
    int show_delta = 0;
    int monitor_width, monitor_height;
    UINT i;

    input_desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS);
    if (!input_desktop)
    {
        skip("cursor presentation requires access to the interactive input desktop\n");
        return;
    }
    CloseDesktop(input_desktop);

    if (!QueryPerformanceFrequency(&performance_frequency) || !performance_frequency.QuadPart)
    {
        skip("QueryPerformanceFrequency is unavailable\n");
        return;
    }

    trace_display_environment();
    if (!high_resolution_mode)
        skip("high-resolution cursor latency budget is not enforced below 2560x1440\n");
    original_cursor = GetCursor();
    have_original_position = GetCursorPos(&original_position);
    have_original_clip = GetClipCursor(&original_clip);
    if (!have_original_position || !have_original_clip)
    {
        skip("cursor position or clipping state is unavailable on this desktop\n");
        return;
    }

    shapes[0].cursor = create_monochrome_cursor();
    shapes[1].cursor = create_color_cursor(FALSE);
    shapes[2].cursor = create_color_cursor(TRUE);
    for (i = 0; i < sizeof(shapes) / sizeof(shapes[0]); ++i)
    {
        ok(shapes[i].cursor != NULL, "%s cursor creation failed, error %lu\n", shapes[i].name, GetLastError());
        if (shapes[i].cursor)
            test_cursor_bitmap_contract(&shapes[i]);
    }
    if (!shapes[0].cursor || !shapes[1].cursor || !shapes[2].cursor)
        goto cleanup;

    hwnd = create_cursor_window();
    ok(hwnd != NULL, "failed to create the cursor presentation window, error %lu\n", GetLastError());
    if (!hwnd)
        goto cleanup;

    ClipCursor(NULL);
    if (!normalize_cursor_show_count(&show_delta))
    {
        skip("could not normalize the thread cursor display count\n");
        goto cleanup;
    }

    ZeroMemory(&monitor_info, sizeof(monitor_info));
    monitor_info.cbSize = sizeof(monitor_info);
    test_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    if (!test_monitor || !GetMonitorInfoA(test_monitor, &monitor_info))
    {
        ok(FALSE, "failed to query the test monitor, error %lu\n", GetLastError());
        goto cleanup;
    }
    monitor_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
    monitor_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
    trace("cursor-parity: test-monitor rect=%ld,%ld-%ld,%ld primary=%d\n", monitor_info.rcMonitor.left, monitor_info.rcMonitor.top, monitor_info.rcMonitor.right, monitor_info.rcMonitor.bottom, !!(monitor_info.dwFlags & MONITORINFOF_PRIMARY));
    adjacent_first.x = monitor_info.rcMonitor.left + monitor_width / 2;
    adjacent_first.y = monitor_info.rcMonitor.top + monitor_height / 2;
    adjacent_second.x = adjacent_first.x + (monitor_width > 2 ? 1 : 0);
    adjacent_second.y = adjacent_first.y + (monitor_height > 2 ? 1 : 0);
    far_first.x = monitor_info.rcMonitor.left + (monitor_width > 128 ? 64 : 0);
    far_first.y = monitor_info.rcMonitor.top + (monitor_height > 128 ? 64 : 0);
    far_second.x = monitor_info.rcMonitor.right - (monitor_width > 128 ? 65 : 1);
    far_second.y = monitor_info.rcMonitor.bottom - (monitor_height > 128 ? 65 : 1);

    SetCursorPos(adjacent_first.x, adjacent_first.y);
    test_cursor_visibility(shapes[2].cursor, &show_delta);
    for (i = 0; i < sizeof(shapes) / sizeof(shapes[0]); ++i)
        run_cursor_shape(hwnd, &shapes[i], adjacent_first, adjacent_second, far_first, far_second);
    measure_cursor_switches(hwnd, shapes, sizeof(shapes) / sizeof(shapes[0]));

cleanup:
    window_cursor = original_cursor;
    SetCursor(original_cursor);
    if (hwnd)
    {
        ClipCursor(NULL);
        SetCursorPos(original_position.x, original_position.y);
        pump_messages();
        DestroyWindow(hwnd);
        UnregisterClassA(cursor_window_class, GetModuleHandleA(NULL));
    }
    if (have_original_clip)
        ClipCursor(&original_clip);
    while (show_delta > 0)
    {
        ShowCursor(FALSE);
        --show_delta;
    }
    while (show_delta < 0)
    {
        ShowCursor(TRUE);
        ++show_delta;
    }
    for (i = 0; i < sizeof(shapes) / sizeof(shapes[0]); ++i)
    {
        if (shapes[i].cursor)
            DestroyCursor(shapes[i].cursor);
    }
}
