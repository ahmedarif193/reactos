/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Verify rendered glass from the GPU primary, without GDI shadow readback. */
static BOOL BlurProbeInverted;

static LRESULT CALLBACK
BlurProbeWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_CREATE)
        SetWindowLongPtrW(Window, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCTW *)LParam)->lpCreateParams);
    if (Message == WM_PAINT)
    {
        PAINTSTRUCT Paint;
        RECT Rect;
        HDC Dc = BeginPaint(Window, &Paint);
        GetClientRect(Window, &Rect);
        if (GetWindowLongPtrW(Window, GWLP_USERDATA))
        {
            HBRUSH Brush = CreateSolidBrush(RGB(32, 32, 32));
            FillRect(Dc, &Rect, Brush);
            DeleteObject(Brush);
        }
        else
        {
            FillRect(Dc, &Rect, GetStockObject(BlurProbeInverted ? BLACK_BRUSH : WHITE_BRUSH));
            Rect.right /= 2;
            FillRect(Dc, &Rect, GetStockObject(BlurProbeInverted ? WHITE_BRUSH : BLACK_BRUSH));
        }
        EndPaint(Window, &Paint);
        return 0;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static void
BlurProbeSettle(void)
{
    DWORD Start = GetTickCount();
    do { PumpMessages(); Sleep(10); } while (GetTickCount() - Start < 1500);
}

static INT
RunBlurProbe(const RECT *WorkArea)
{
    static const WCHAR ClassName[] = L"WddmGpuBlurProbe";
    static const LONG Offsets[] = {-40, -2, 2, 40};
    WNDCLASSW Class = {0};
    HWND Background = NULL, Glass = NULL, Occluder = NULL;
    HDC Screen = NULL;
    HMODULE DwmApi = NULL;
    HRESULT (WINAPI *EnableBlur)(HWND, const DWM_BLURBEHIND *) = NULL;
    LONG Left = WorkArea->left + 80, Top = WorkArea->top + 80;
    ULONG Phase, Index, Failures = 0;
    BOOL Passed = FALSE;

    if (WorkArea->right - Left < 512 || WorkArea->bottom - Top < 256)
        return 1;
    Class.lpfnWndProc = BlurProbeWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = ClassName;
    if (!RegisterClassW(&Class))
        return 1;
    Background = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
        L"GPU blur pattern", WS_POPUP, Left, Top, 512, 256,
        NULL, NULL, Class.hInstance, NULL);
    Occluder = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
        L"GPU blur occluder", WS_POPUP, Left, Top, 512, 256,
        Background, NULL, Class.hInstance, (PVOID)1);
    Glass = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
        L"GPU blur sample", WS_POPUP, Left + 192, Top + 64, 128, 128,
        Background, NULL, Class.hInstance, (PVOID)1);
    Screen = GetDC(NULL);
    if (!Background || !Glass || !Occluder || !Screen)
        goto Cleanup;
    if (!SetPropW(Glass, DWM_PROP_SYSTEM_BACKDROP_TYPE, (HANDLE)DWM_BACKDROP_TRANSIENT) ||
        !SetPropW(Glass, DWM_PROP_BACKDROP_REGION, (HANDLE)DWM_BACKDROP_REGION_WINDOW) ||
        !SetPropW(Glass, DWM_PROP_BACKDROP_OPACITY, (HANDLE)1) ||
        !SetPropW(Glass, DWM_PROP_BACKDROP_COLOR, (HANDLE)(ULONG_PTR)(RGB(32,32,32) + 1)))
        goto Cleanup;
    ShowWindow(Background, SW_SHOWNOACTIVATE);
    for (Phase = 0; Phase < 13; ++Phase)
    {
        ULONG Value[4];
        BOOL Valid = TRUE;
        BlurProbeInverted = Phase >= 2;
        if (Phase == 1)
            ShowWindow(Glass, SW_SHOWNOACTIVATE);
        if (Phase == 3)
            SetWindowPos(Glass, HWND_TOPMOST, Left + 208, Top + 80, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
        if (Phase == 4)
            SetPropW(Glass, DWM_PROP_BACKDROP_OPACITY, (HANDLE)256);
        if (Phase >= 5)
        {
            DWM_BLURBEHIND Blur = {0};
            HRGN Region = NULL;
            if (!DwmApi)
            {
                DwmApi = LoadLibraryW(L"dwmapi.dll");
                if (DwmApi)
                    EnableBlur = (void *)GetProcAddress(DwmApi, "DwmEnableBlurBehindWindow");
            }
            RemovePropW(Glass, DWM_PROP_SYSTEM_BACKDROP_TYPE);
            Blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
            Blur.fEnable = Phase != 9;
            if (Phase == 6 || Phase == 7)
                Region = CreateRectRgn(0, 0, 128, 64);
            else if (Phase == 8)
                Region = CreateRectRgn(0, 0, 0, 0);
            Blur.hRgnBlur = Region;
            if (!EnableBlur || ((Phase >= 6 && Phase <= 8) && !Region) ||
                FAILED(EnableBlur(Glass, &Blur)))
                Valid = FALSE;
            if (Region) DeleteObject(Region);
        }
        /* Hide a lower window without repainting any other lower surface.
         * The same glass owner must not reuse its formerly longer prefix. */
        if (Phase == 10 || Phase == 12)
        {
            if (!SetWindowPos(Occluder, Glass, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW))
                Valid = FALSE;
            RedrawWindow(Occluder, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        }
        else if (Phase == 11)
            ShowWindow(Occluder, SW_HIDE);
        if (Phase < 10)
            RedrawWindow(Background, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        RedrawWindow(Glass, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        BlurProbeSettle();
        for (Index = 0; Index < 4; ++Index)
        {
            POINT Point = {Left + 256 + Offsets[Index],
                           Top + (Phase == 7 ? 176 : 128)};
            COLORREF Color = ReadSharedPrimaryPixel(Screen, Point);
            Value[Index] = GetRValue(Color);
            if (Color == CLR_INVALID ||
                abs((int)GetRValue(Color) - (int)GetGValue(Color)) > 4 ||
                abs((int)GetRValue(Color) - (int)GetBValue(Color)) > 4)
                Valid = FALSE;
            if (BlurProbeInverted && Phase != 4 && Phase != 9 && Phase != 10 && Phase != 12)
                Value[Index] = 255 - Value[Index];
        }
        if (Phase == 0 || Phase == 7 || Phase == 8)
            Valid = Valid && Value[0] <= 8 && Value[1] <= 8 &&
                    Value[2] >= 247 && Value[3] >= 247;
        else if (Phase == 4 || Phase == 9 || Phase == 10 || Phase == 12)
            Valid = Valid && Value[0] >= 28 && Value[0] <= 36 &&
                    Value[1] >= 28 && Value[1] <= 36 &&
                    Value[2] >= 28 && Value[2] <= 36 &&
                    Value[3] >= 28 && Value[3] <= 36;
        else
            Valid = Valid && Value[0] <= 32 && Value[3] >= 223 &&
                    Value[1] > Value[0] + 8 && Value[2] + 8 < Value[3] &&
                    Value[1] < Value[2];
        TestPrint("DWM_GPU_BLUR_PROBE phase=%lu values=%lu,%lu,%lu,%lu pass=%u\n",
                  Phase, Value[0], Value[1], Value[2], Value[3], Valid);
        if (!Valid) ++Failures;
    }
    Passed = Failures == 0;
Cleanup:
    if (DwmApi) FreeLibrary(DwmApi);
    if (Screen) ReleaseDC(NULL, Screen);
    if (Glass) DestroyWindow(Glass);
    if (Occluder) DestroyWindow(Occluder);
    if (Background) DestroyWindow(Background);
    UnregisterClassW(ClassName, Class.hInstance);
    TestPrint("DWM_GPU_BLUR_RESULT pass=%u failures=%lu\n", Passed, Failures);
    return Passed ? 0 : 1;
}
