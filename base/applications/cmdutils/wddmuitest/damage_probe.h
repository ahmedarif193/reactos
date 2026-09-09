/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Serial-only pixel verification of damaged GPU composition and scanout. */
typedef struct _DAMAGE_PROBE_WINDOW
{
    COLORREF Color;
    BOOL PatchVisible;
    RECT Patch;
    COLORREF PatchColor;
} DAMAGE_PROBE_WINDOW;

static LRESULT CALLBACK
DamageProbeWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    DAMAGE_PROBE_WINDOW *State;
    if (Message == WM_CREATE)
        SetWindowLongPtrW(Window, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCTW *)LParam)->lpCreateParams);
    State = (DAMAGE_PROBE_WINDOW *)GetWindowLongPtrW(Window, GWLP_USERDATA);
    if (Message == WM_PAINT && State != NULL)
    {
        PAINTSTRUCT Paint;
        RECT Client;
        HDC Dc = BeginPaint(Window, &Paint);
        HBRUSH Brush = CreateSolidBrush(State->Color);
        GetClientRect(Window, &Client);
        FillRect(Dc, &Client, Brush);
        DeleteObject(Brush);
        if (State->PatchVisible)
        {
            Brush = CreateSolidBrush(State->PatchColor);
            FillRect(Dc, &State->Patch, Brush);
            DeleteObject(Brush);
        }
        EndPaint(Window, &Paint);
        return 0;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static BOOL
DamageProbePixel(HDC Screen, LONG X, LONG Y, COLORREF Expected, ULONG Phase)
{
    POINT Point = {X, Y};
    COLORREF Actual = ReadSharedPrimaryPixel(Screen, Point);
    BOOL Match = Actual != CLR_INVALID && Actual == Expected;
    TestPrint("DWM_GPU_DAMAGE_PIXEL phase=%lu x=%ld y=%ld expected=%08lx actual=%08lx pass=%u\n",
              Phase, X, Y, Expected, Actual, Match);
    return Match;
}

static INT
RunDamageProbe(const RECT *WorkArea)
{
    static const WCHAR ClassName[] = L"WddmGpuDamageProbe";
    static const POINT Positions[] = {{65, 67}, {241, 149}, {417, 73}, {93, 247}};
    DAMAGE_PROBE_WINDOW Base = {RGB(24, 48, 96), FALSE, {523, 291, 570, 330}, RGB(48, 192, 80)};
    DAMAGE_PROBE_WINDOW Top = {RGB(192, 48, 24), FALSE, {0}, 0};
    WNDCLASSW Class = {0};
    HWND Background = NULL, Overlay = NULL;
    HDC Screen = NULL;
    LONG Left = WorkArea->left + 48, Y = WorkArea->top + 56;
    ULONG Phase, Failures = 0, Checks = 0;
    BOOL Complete = FALSE;

    if (WorkArea->right - Left < 640 || WorkArea->bottom - Y < 384)
        return 1;
    Class.lpfnWndProc = DamageProbeWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = ClassName;
    if (!RegisterClassW(&Class))
        return 1;
    Background = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
        L"GPU damage background", WS_POPUP, Left, Y, 640, 384,
        NULL, NULL, Class.hInstance, &Base);
    Overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
        L"GPU damage overlay", WS_POPUP, Left + Positions[0].x, Y + Positions[0].y,
        83, 77, Background, NULL, Class.hInstance, &Top);
    Screen = GetDC(NULL);
    if (!Background || !Overlay || !Screen)
        goto Cleanup;
    ShowWindow(Background, SW_SHOWNOACTIVATE);
    ShowWindow(Overlay, SW_SHOWNOACTIVATE);

#define DAMAGE_CHECK(X, Ypos, Color) do { ++Checks; if (!DamageProbePixel(Screen, (X), (Ypos), (Color), Phase)) ++Failures; } while (0)
    for (Phase = 0; Phase < ARRAYSIZE(Positions); ++Phase)
    {
        Top.Color = (Phase & 1) ? RGB(160, 48, 192) : RGB(192, 48, 24);
        if (!SetWindowPos(Overlay, HWND_TOPMOST, Left + Positions[Phase].x,
                          Y + Positions[Phase].y, 83, 77, SWP_NOACTIVATE))
            goto Cleanup;
        InvalidateRect(Overlay, NULL, FALSE);
        BlurProbeSettle();
        DAMAGE_CHECK(Left + Positions[Phase].x + 20, Y + Positions[Phase].y + 20, Top.Color);
        DAMAGE_CHECK(Left + Positions[Phase].x + 72, Y + Positions[Phase].y + 65, Top.Color);
        DAMAGE_CHECK(Left + 8, Y + 8, Base.Color);
        if (Phase != 0)
        {
            DAMAGE_CHECK(Left + Positions[Phase - 1].x + 20,
                         Y + Positions[Phase - 1].y + 20, Base.Color);
            DAMAGE_CHECK(Left + Positions[Phase - 1].x + 72,
                         Y + Positions[Phase - 1].y + 65, Base.Color);
        }
    }
    /* Small GDI invalidations update and erase a patch across tile edges. */
    for (; Phase < 8; ++Phase)
    {
        Base.PatchVisible = (Phase & 1) == 0;
        InvalidateRect(Background, &Base.Patch, FALSE);
        BlurProbeSettle();
        DAMAGE_CHECK(Left + Base.Patch.left + 2, Y + Base.Patch.top + 2,
                     Base.PatchVisible ? Base.PatchColor : Base.Color);
        DAMAGE_CHECK(Left + Base.Patch.right - 2, Y + Base.Patch.bottom - 2,
                     Base.PatchVisible ? Base.PatchColor : Base.Color);
        DAMAGE_CHECK(Left + Base.Patch.left - 2, Y + Base.Patch.top + 2, Base.Color);
        DAMAGE_CHECK(Left + Positions[3].x + 20, Y + Positions[3].y + 20, Top.Color);
    }
    if (!SetWindowPos(Overlay, HWND_TOPMOST, 0, 0, 114, 96,
                      SWP_NOMOVE | SWP_NOACTIVATE))
        goto Cleanup;
    BlurProbeSettle();
    DAMAGE_CHECK(Left + Positions[3].x + 102, Y + Positions[3].y + 86, Top.Color);
    ShowWindow(Overlay, SW_HIDE);
    BlurProbeSettle();
    ++Phase;
    DAMAGE_CHECK(Left + Positions[3].x + 20, Y + Positions[3].y + 20, Base.Color);
    DAMAGE_CHECK(Left + Positions[3].x + 102, Y + Positions[3].y + 86, Base.Color);
    Complete = TRUE;
#undef DAMAGE_CHECK
Cleanup:
    if (Overlay) DestroyWindow(Overlay);
    if (Background) DestroyWindow(Background);
    if (Screen) ReleaseDC(NULL, Screen);
    UnregisterClassW(ClassName, Class.hInstance);
    TestPrint("DWM_GPU_DAMAGE_RESULT complete=%u checks=%lu failures=%lu pass=%u\n",
              Complete, Checks, Failures, Complete && Failures == 0);
    return Complete && Failures == 0 ? 0 : 1;
}

/* Verify WGL producer -> shared surface -> DWM -> primary, not glReadPixels. */
static INT
RunGlPrimaryProbe(const RECT *WorkArea, BOOL MixedGdi)
{
    static const WCHAR ClassName[] = L"WddmGlPrimaryProbe";
    static const WCHAR ParentClass[] = L"WddmGlMixedParent";
    static const POINT Sizes[] = {{257, 193}, {319, 241}, {384, 256}};
    static const COLORREF Colors[] = {RGB(192, 48, 24), RGB(32, 192, 80), RGB(48, 64, 208)};
    typedef BOOL (WINAPI *SET_SWAP_INTERVAL)(INT);
    WNDCLASSW Class = {0};
    PIXELFORMATDESCRIPTOR Pfd = {0};
    HWND Window = NULL, Parent = NULL;
    DAMAGE_PROBE_WINDOW ParentState = {RGB(24, 48, 96), FALSE,
        {5, 5, 21, 21}, RGB(192, 160, 32)};
    HDC Dc = NULL, Screen = NULL;
    HGLRC Context = NULL;
    SET_SWAP_INTERVAL SetSwapInterval;
    ULONG Phase, Checks = 0, Failures = 0;
    BOOL Complete = FALSE;
    LONG Left = WorkArea->left + 80, Top = WorkArea->top + 96;
    LONG ParentLeft = Left - 32, ParentTop = Top - 32;

    Class.style = CS_OWNDC;
    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = ClassName;
    if (!RegisterClassW(&Class))
        return 1;
    if (MixedGdi)
    {
        WNDCLASSW ParentWindowClass = {0};
        ParentWindowClass.lpfnWndProc = DamageProbeWindowProc;
        ParentWindowClass.hInstance = Class.hInstance;
        ParentWindowClass.lpszClassName = ParentClass;
        if (!RegisterClassW(&ParentWindowClass))
            goto Cleanup;
        Parent = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ParentClass,
            L"GDI parent of GL child", WS_POPUP | WS_CLIPCHILDREN,
            ParentLeft, ParentTop, 640, 480, NULL, NULL, Class.hInstance, &ParentState);
        if (!Parent)
            goto Cleanup;
        ShowWindow(Parent, SW_SHOWNOACTIVATE);
    }
    Window = CreateWindowExW(MixedGdi ? 0 : WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
        L"WGL shared primary probe", (MixedGdi ? WS_CHILD : WS_POPUP) |
        WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        MixedGdi ? 32 : Left, MixedGdi ? 32 : Top,
        Sizes[0].x, Sizes[0].y, Parent, NULL, Class.hInstance, NULL);
    if (!Window)
        goto Cleanup;
    Dc = GetDC(Window);
    Screen = GetDC(NULL);
    if (!Dc || !Screen)
        goto Cleanup;
    Pfd.nSize = sizeof(Pfd);
    Pfd.nVersion = 1;
    Pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Pfd.iPixelType = PFD_TYPE_RGBA;
    Pfd.cColorBits = 24;
    Pfd.cAlphaBits = 8;
    Pfd.iLayerType = PFD_MAIN_PLANE;
    {
        INT Format = ChoosePixelFormat(Dc, &Pfd);
        if (!Format || !SetPixelFormat(Dc, Format, &Pfd))
            goto Cleanup;
    }
    Context = wglCreateContext(Dc);
    if (!Context || !wglMakeCurrent(Dc, Context))
        goto Cleanup;
    TestPrint("DWM_GL_PRIMARY_RENDERER %s mixed=%u\n", glGetString(GL_RENDERER), MixedGdi);
    SetSwapInterval = (SET_SWAP_INTERVAL)wglGetProcAddress("wglSwapIntervalEXT");
    if (SetSwapInterval)
        SetSwapInterval(0);
    ShowWindow(Window, SW_SHOWNOACTIVATE);
    glDisable(GL_DITHER);
    for (Phase = 0; Phase < 9; ++Phase)
    {
        POINT Size = Sizes[Phase / 3];
        LONG X = Left + (Phase % 3) * 41, Y = Top + (Phase % 3) * 29;
        COLORREF Base = Colors[Phase % 3];
        COLORREF Patch = Colors[(Phase + 1) % 3];
        ULONG Repeat;
        if (!SetWindowPos(Window, MixedGdi ? HWND_TOP : HWND_TOPMOST,
                         MixedGdi ? X - ParentLeft : X,
                         MixedGdi ? Y - ParentTop : Y,
                         Size.x, Size.y, SWP_NOACTIVATE))
            goto Cleanup;
        if (MixedGdi)
        {
            /* A real parent GDI repaint shares the top-level redirection
             * entry with the GL child and must survive its publication. */
            ParentState.PatchVisible = (Phase & 1) != 0;
            InvalidateRect(Parent, &ParentState.Patch, FALSE);
        }
        PumpMessages();
        glViewport(0, 0, Size.x, Size.y);
        /* Immediate presents may legitimately be dropped until DWM releases
         * its previous surface. Repaint unchanged test content long enough
         * to obtain a completed publication; no benchmark is altered. */
        for (Repeat = 0; Repeat < 6; ++Repeat)
        {
            glDisable(GL_SCISSOR_TEST);
            glClearColor(GetRValue(Base) / 255.0f, GetGValue(Base) / 255.0f,
                         GetBValue(Base) / 255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);
            glScissor(37, 41, 89, 73);
            glClearColor(GetRValue(Patch) / 255.0f, GetGValue(Patch) / 255.0f,
                         GetBValue(Patch) / 255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
            if (glGetError() != GL_NO_ERROR || !SwapBuffers(Dc))
                goto Cleanup;
            PumpMessages();
            Sleep(100);
        }
        BlurProbeSettle();
#define GL_PRIMARY_CHECK(Px, Py, Expected) do { \
    COLORREF Actual; POINT Position = {(Px), (Py)}; BOOL Match; \
    Actual = ReadSharedPrimaryPixel(Screen, Position); \
    Match = Actual != CLR_INVALID && Actual == (Expected); \
    ++Checks; if (!Match) ++Failures; \
    TestPrint("DWM_GL_PRIMARY_PIXEL phase=%lu x=%ld y=%ld expected=%08lx actual=%08lx pass=%u mixed=%u\n", \
              Phase, Position.x, Position.y, (COLORREF)(Expected), Actual, Match, MixedGdi); \
} while (0)
        GL_PRIMARY_CHECK(X + 8, Y + 8, Base);
        GL_PRIMARY_CHECK(X + Size.x - 8, Y + Size.y - 8, Base);
        GL_PRIMARY_CHECK(X + 45, Y + Size.y - 50, Patch);
        GL_PRIMARY_CHECK(X + 118, Y + Size.y - 105, Patch);
        if (MixedGdi)
        {
            GL_PRIMARY_CHECK(ParentLeft + 8, ParentTop + 8,
                ParentState.PatchVisible ? ParentState.PatchColor : ParentState.Color);
            GL_PRIMARY_CHECK(ParentLeft + 600, ParentTop + 440, ParentState.Color);
        }
#undef GL_PRIMARY_CHECK
    }
    Complete = TRUE;
Cleanup:
    if (Context)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(Context);
    }
    if (Dc) ReleaseDC(Window, Dc);
    if (Screen) ReleaseDC(NULL, Screen);
    if (Window) DestroyWindow(Window);
    if (Parent) DestroyWindow(Parent);
    if (MixedGdi) UnregisterClassW(ParentClass, Class.hInstance);
    UnregisterClassW(ClassName, Class.hInstance);
    TestPrint("DWM_GL_PRIMARY_RESULT complete=%u checks=%lu failures=%lu pass=%u mixed=%u\n",
              Complete, Checks, Failures, Complete && Failures == 0, MixedGdi);
    return Complete && Failures == 0 ? 0 : 1;
}
