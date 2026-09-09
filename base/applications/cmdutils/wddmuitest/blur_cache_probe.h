/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Measure GPU filter reuse while own and disjoint lower surfaces repaint. */
static INT
RunBlurCacheProbe(const RECT *WorkArea)
{
    static const WCHAR ClassName[] = L"WddmGpuBlurCacheProbe";
    static const LONG Offsets[] = {-40, -2, 2, 40};
    WNDCLASSW Class = {0};
    HWND Background = NULL, Glass = NULL, Side = NULL;
    HWND Console = GetConsoleWindow();
    BOOL ConsoleVisible = Console && IsWindowVisible(Console), Passed = FALSE;
    HDC Screen = NULL;
    HMODULE Profiler = NULL;
    HRESULT (WINAPI *StartCapture)(ULONG *) = NULL;
    HRESULT (WINAPI *StopCapture)(ULONG, DPT_SNAPSHOT *, ULONG) = NULL;
    DPT_SNAPSHOT *Capture = NULL;
    ULONG Session = 0, Phase, Step, Index, Failures = 0;
    LONG Left = WorkArea->left + 80, Top = WorkArea->top + 80;

    if (WorkArea->right - Left < 1024 || WorkArea->bottom - Top < 256)
        return 1;
    Class.lpfnWndProc = BlurProbeWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = ClassName;
    if (!RegisterClassW(&Class))
        return 1;
    Profiler = LoadLibraryW(L"dwmprof.dll");
    if (Profiler)
    {
        StartCapture = (void *)GetProcAddress(Profiler, "DwmProfileStartCapture");
        StopCapture = (void *)GetProcAddress(Profiler, "DwmProfileStopCapture");
        Capture = HeapAlloc(GetProcessHeap(), 0, sizeof(*Capture));
    }
    Background = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
        L"Cache probe pattern", WS_POPUP, Left, Top, 512, 256,
        NULL, NULL, Class.hInstance, NULL);
    Side = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
        L"Cache probe disjoint repaint", WS_POPUP, Left + 768, Top, 128, 128,
        NULL, NULL, Class.hInstance, NULL);
    Glass = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
        L"Cache probe glass", WS_POPUP, Left + 192, Top + 64, 128, 128,
        Background, NULL, Class.hInstance, (PVOID)1);
    Screen = GetDC(NULL);
    if (!Background || !Side || !Glass || !Screen || !StartCapture || !StopCapture || !Capture)
        goto Cleanup;
    if (!SetPropW(Glass, DWM_PROP_SYSTEM_BACKDROP_TYPE, (HANDLE)DWM_BACKDROP_TRANSIENT) ||
        !SetPropW(Glass, DWM_PROP_BACKDROP_REGION, (HANDLE)DWM_BACKDROP_REGION_WINDOW) ||
        !SetPropW(Glass, DWM_PROP_BACKDROP_OPACITY, (HANDLE)1) ||
        !SetPropW(Glass, DWM_PROP_BACKDROP_COLOR, (HANDLE)(ULONG_PTR)(RGB(32,32,32) + 1)))
        goto Cleanup;
    if (ConsoleVisible) ShowWindow(Console, SW_HIDE);
    BlurProbeInverted = FALSE;
    ShowWindow(Side, SW_SHOWNOACTIVATE);
    ShowWindow(Background, SW_SHOWNOACTIVATE);
    ShowWindow(Glass, SW_SHOWNOACTIVATE);
    BlurProbeSettle();
    for (Phase = 0; Phase < 3; ++Phase)
    {
        ULONG Value[4];
        BOOL Valid = TRUE, Pixels = TRUE;
        HRESULT Status = StartCapture(&Session);
        if (FAILED(Status))
        {
            TestPrint("DWM_BLUR_CACHE_ERROR stage=start status=0x%08lx\n", Status);
            ++Failures;
            break;
        }
        for (Step = 0; Step < 80; ++Step)
        {
            BlurProbeInverted = (Step & 1) != 0;
            if (Phase == 1)
                RedrawWindow(Side, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
            else if (Phase == 2)
                RedrawWindow(Background, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
            RedrawWindow(Glass, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
            PumpMessages();
            Sleep(40);
        }
        BlurProbeSettle();
        Status = StopCapture(Session, Capture, sizeof(*Capture));
        if (SUCCEEDED(Status)) Session = 0;
        if (FAILED(Status) || Capture->Available != 7 || Capture->Status[0] < 0 ||
            Capture->Domain[0].Frequency == 0)
        {
            TestPrint("DWM_BLUR_CACHE_ERROR stage=stop status=0x%08lx\n", Status);
            ++Failures;
            break;
        }
        for (Index = 0; Index < ARRAYSIZE(Offsets); ++Index)
        {
            POINT Point = {Left + 256 + Offsets[Index], Top + 128};
            COLORREF Color = ReadSharedPrimaryPixel(Screen, Point);
            Value[Index] = GetRValue(Color);
            if (Color == CLR_INVALID ||
                abs((int)GetRValue(Color) - (int)GetGValue(Color)) > 4 ||
                abs((int)GetRValue(Color) - (int)GetBValue(Color)) > 4)
                Pixels = FALSE;
            if (Phase == 2) Value[Index] = 255 - Value[Index];
        }
        Pixels = Pixels && Value[0] <= 32 && Value[3] >= 223 &&
                 Value[1] > Value[0] + 8 && Value[2] + 8 < Value[3] && Value[1] < Value[2];
        Valid = Pixels && Capture->Domain[0].Counter[DPT_FRAME].Completed >= 20 &&
                (Phase == 2 ? Capture->Domain[0].Counter[DPT_BLUR_FILTER].Completed >= 20 :
                 (Capture->Domain[0].Counter[DPT_BLUR_FILTER].Completed == 0 &&
                  Capture->Domain[0].Counter[DPT_BLUR_HIT].Completed >= 20));
        TestPrint("DWM_BLUR_CACHE_PHASE phase=%lu frames=%llu filtered=%llu reused=%llu cpu_us=%llu wall_us=%llu prepare_us=%llu filter_us=%llu values=%lu,%lu,%lu,%lu pixels=%u pass=%u\n",
            Phase, Capture->Domain[0].Counter[DPT_FRAME].Completed,
            Capture->Domain[0].Counter[DPT_BLUR_FILTER].Completed,
            Capture->Domain[0].Counter[DPT_BLUR_HIT].Completed,
            (Capture->CpuKernel100ns + Capture->CpuUser100ns) / 10,
            (Capture->Domain[0].Stop - Capture->Domain[0].Start) * 1000000 / Capture->Domain[0].Frequency,
            Capture->Domain[0].Counter[DPT_PREPARE].Ticks * 1000000 / Capture->Domain[0].Frequency,
            Capture->Domain[0].Counter[DPT_BLUR].Ticks * 1000000 / Capture->Domain[0].Frequency,
            Value[0], Value[1], Value[2], Value[3], Pixels, Valid);
        if (!Valid) ++Failures;
    }
    Passed = Failures == 0;
Cleanup:
    if (Session && StopCapture) StopCapture(Session, Capture, sizeof(*Capture));
    if (Capture) HeapFree(GetProcessHeap(), 0, Capture);
    if (Profiler) FreeLibrary(Profiler);
    if (Screen) ReleaseDC(NULL, Screen);
    if (Glass) DestroyWindow(Glass);
    if (Side) DestroyWindow(Side);
    if (Background) DestroyWindow(Background);
    if (ConsoleVisible) ShowWindow(Console, SW_SHOWNOACTIVATE);
    UnregisterClassW(ClassName, Class.hInstance);
    TestPrint("DWM_BLUR_CACHE_RESULT pass=%u failures=%lu\n", Passed, Failures);
    return Passed ? 0 : 1;
}
