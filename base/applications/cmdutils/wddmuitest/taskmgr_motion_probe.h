/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Observe invariant pixels in the real Taskmgr11 window while its geometry
 * changes. An invariant covers every possible lagging position, so an
 * asynchronous move cannot itself be mistaken for a missing window. */
#define MOTION_DX 64
#define MOTION_DY 32
#define MOTION_POINTS 16
#define MOTION_EVENTS 32

typedef struct _PRIMARY_PROBE_INVALIDATE
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
    ULONGLONG Offset;
    ULONGLONG Length;
} PRIMARY_PROBE_INVALIDATE;
typedef NTSTATUS (APIENTRY *PRIMARY_PROBE_INVALIDATE_FN)(const PRIMARY_PROBE_INVALIDATE *);
C_ASSERT(sizeof(PRIMARY_PROBE_INVALIDATE) == 24);

typedef struct _MOTION_PRIMARY
{
    D3DKMT_OPENADAPTERFROMHDC Adapter;
    D3DKMT_CREATEDEVICE Device;
    D3DKMT_OPENRESOURCE Resource;
    D3DDDI_OPENALLOCATIONINFO Allocation;
    D3DKMT_LOCK Lock;
    PRIMARY_PROBE_INVALIDATE_FN Invalidate;
    LONG Width, Height;
    BOOL Locked;
} MOTION_PRIMARY;

static void
MotionClosePrimary(MOTION_PRIMARY *Primary)
{
    if (Primary->Locked)
    {
        D3DKMT_UNLOCK Unlock = {0};
        Unlock.hDevice = Primary->Device.hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Primary->Allocation.hAllocation;
        D3DKMTUnlock(&Unlock);
    }
    if (Primary->Resource.hResource)
    {
        D3DKMT_DESTROYALLOCATION Destroy = {0};
        Destroy.hDevice = Primary->Device.hDevice;
        Destroy.hResource = Primary->Resource.hResource;
        D3DKMTDestroyAllocation(&Destroy);
    }
    if (Primary->Device.hDevice)
    {
        D3DKMT_DESTROYDEVICE Destroy = {0};
        Destroy.hDevice = Primary->Device.hDevice;
        D3DKMTDestroyDevice(&Destroy);
    }
    if (Primary->Adapter.hAdapter)
    {
        D3DKMT_CLOSEADAPTER Close = {0};
        Close.hAdapter = Primary->Adapter.hAdapter;
        D3DKMTCloseAdapter(&Close);
    }
}

static BOOL
MotionOpenPrimary(MOTION_PRIMARY *Primary, HDC Screen)
{
    D3DKMT_GETSHAREDPRIMARYHANDLE Shared = {0};
    D3DKMT_QUERYRESOURCEINFO Query = {0};
    PVOID Runtime = NULL, Resource = NULL, Allocation = NULL;
    NTSTATUS Status;
    BOOL Result = FALSE;

    Primary->Width = GetDeviceCaps(Screen, HORZRES);
    Primary->Height = GetDeviceCaps(Screen, VERTRES);
    Primary->Invalidate = (PRIMARY_PROBE_INVALIDATE_FN)GetProcAddress(
        GetModuleHandleW(L"gdi32.dll"), "D3DKMTInvalidateCache");
    if (!Primary->Invalidate || Primary->Width <= 0 || Primary->Height <= 0 ||
        GetDeviceCaps(Screen, BITSPIXEL) != 32)
        return FALSE;
    Primary->Adapter.hDc = Screen;
    Status = D3DKMTOpenAdapterFromHdc(&Primary->Adapter);
    if (Status < 0) goto Done;
    Primary->Device.hAdapter = Primary->Adapter.hAdapter;
    Status = D3DKMTCreateDevice(&Primary->Device);
    if (Status < 0) goto Done;
    Shared.hAdapter = Primary->Adapter.hAdapter;
    Shared.VidPnSourceId = Primary->Adapter.VidPnSourceId;
    Status = D3DKMTGetSharedPrimaryHandle(&Shared);
    if (Status < 0) goto Done;
    Query.hDevice = Primary->Device.hDevice;
    Query.hGlobalShare = Shared.hSharedPrimary;
    Status = D3DKMTQueryResourceInfo(&Query);
    if (Status < 0 || Query.NumAllocations != 1 ||
        Query.PrivateRuntimeDataSize > 65536 ||
        Query.ResourcePrivateDriverDataSize > 65536 ||
        Query.TotalPrivateDriverDataSize > 65536) goto Done;
    if (Query.PrivateRuntimeDataSize)
        Runtime = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Query.PrivateRuntimeDataSize);
    if (Query.ResourcePrivateDriverDataSize)
        Resource = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Query.ResourcePrivateDriverDataSize);
    if (Query.TotalPrivateDriverDataSize)
        Allocation = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Query.TotalPrivateDriverDataSize);
    if ((Query.PrivateRuntimeDataSize && !Runtime) ||
        (Query.ResourcePrivateDriverDataSize && !Resource) ||
        (Query.TotalPrivateDriverDataSize && !Allocation)) goto Done;
    Primary->Resource.hDevice = Primary->Device.hDevice;
    Primary->Resource.hGlobalShare = Shared.hSharedPrimary;
    Primary->Resource.NumAllocations = 1;
    Primary->Resource.pOpenAllocationInfo = &Primary->Allocation;
    Primary->Resource.pPrivateRuntimeData = Runtime;
    Primary->Resource.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    Primary->Resource.pResourcePrivateDriverData = Resource;
    Primary->Resource.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Primary->Resource.pTotalPrivateDriverDataBuffer = Allocation;
    Primary->Resource.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Status = D3DKMTOpenResource(&Primary->Resource);
    if (Status < 0) goto Done;
    Primary->Lock.hDevice = Primary->Device.hDevice;
    Primary->Lock.hAllocation = Primary->Allocation.hAllocation;
    Primary->Lock.Flags.ReadOnly = 1;
    Primary->Lock.Flags.LockEntire = 1;
    Status = D3DKMTLock(&Primary->Lock);
    if (Status < 0) goto Done;
    Primary->Locked = TRUE;
    Result = Primary->Lock.pData != NULL;
Done:
    if (Runtime) HeapFree(GetProcessHeap(), 0, Runtime);
    if (Resource) HeapFree(GetProcessHeap(), 0, Resource);
    if (Allocation) HeapFree(GetProcessHeap(), 0, Allocation);
    TestPrint("TASKMGR_FLICKER_PRIMARY status=%08lx mapped=%u width=%ld height=%ld\n",
              Status, Result, Primary->Width, Primary->Height);
    return Result;
}

static BOOL
MotionRead(MOTION_PRIMARY *Primary, SIZE_T Offset, PVOID Buffer, SIZE_T Length)
{
    PRIMARY_PROBE_INVALIDATE Invalidate = {0};
    SIZE_T Read = 0;
    SIZE_T Limit = (SIZE_T)Primary->Width * Primary->Height * sizeof(ULONG);
    if (Offset > Limit || Length > Limit - Offset)
        return FALSE;
    Invalidate.hDevice = Primary->Device.hDevice;
    Invalidate.hAllocation = Primary->Allocation.hAllocation;
    Invalidate.Offset = Offset;
    Invalidate.Length = Length;
    return Primary->Invalidate(&Invalidate) >= 0 &&
           ReadProcessMemory(GetCurrentProcess(), (BYTE *)Primary->Lock.pData + Offset,
                             Buffer, Length, &Read) && Read == Length;
}

static BOOL
MotionColorMatches(ULONG Actual, ULONG Expected)
{
    ULONG Shift;
    for (Shift = 0; Shift < 24; Shift += 8)
        if (abs((INT)((Actual >> Shift) & 255) - (INT)((Expected >> Shift) & 255)) > 4)
            return FALSE;
    return TRUE;
}

static LRESULT CALLBACK
MotionBackdropProcedure(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_PAINT && GetWindowLongPtrW(Window, GWLP_USERDATA))
    {
        PAINTSTRUCT Paint;
        RECT Client;
        LONG X, Y;
        HDC Dc = BeginPaint(Window, &Paint);
        HBRUSH Brushes[2] = {CreateSolidBrush(RGB(32, 32, 32)),
                            CreateSolidBrush(RGB(224, 224, 224))};
        GetClientRect(Window, &Client);
        for (Y = 0; Y < Client.bottom; Y += 64)
        for (X = 0; X < Client.right; X += 64)
        {
            RECT Tile = {X, Y, min(X + 64, Client.right), min(Y + 64, Client.bottom)};
            FillRect(Dc, &Tile, Brushes[((X / 64) + (Y / 64)) & 1]);
        }
        DeleteObject(Brushes[0]);
        DeleteObject(Brushes[1]);
        EndPaint(Window, &Paint);
        return 0;
    }
    return ColorWindowProcedure(Window, Message, WParam, LParam);
}

static INT
MotionMeasurePacing(HWND Window, MOTION_PRIMARY *Primary, LONG Left, LONG Top)
{
    typedef LONG (APIENTRY *QUERY)(D3DKMT_QUERYSTATISTICS *);
    struct PACE_EVENT
    {
        ULONGLONG Time, PollGap, QueryUs, ReadUs;
        ULONG Frame, Queued;
        LONG Edge;
    } *Events;
    static const char *Names[] = {"visible", "counter_only", "visible_profiled"};
    D3DKMT_QUERYSTATISTICS Stats = {0};
    QUERY Query = (QUERY)GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTQueryStatistics");
    HMODULE Profiler = LoadLibraryW(L"dwmprof.dll");
    HRESULT (WINAPI *StartCapture)(ULONG *) = NULL;
    HRESULT (WINAPI *StopCapture)(ULONG, DPT_SNAPSHOT *, ULONG) = NULL;
    DPT_SNAPSHOT *Capture = NULL;
    ULONG Session = 0, Phase, Failures = 0;
    ULONG Pixels[320];
    LONG StripX = Left + 1000 - 16, StripY = Top + 300;
    LONG CaptionY, Calibration[2] = {-1, -1};
    SIZE_T Offset = ((SIZE_T)StripY * Primary->Width + StripX) * sizeof(ULONG);
    POINT ClientOrigin = {0};
    DWORD_PTR Hit = 0;
    INPUT Button = {0};
    BOOL Held = FALSE;
    ULONG i, j;

    Events = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 2048 * sizeof(*Events));
    if (Profiler)
    {
        StartCapture = (void *)GetProcAddress(Profiler, "DwmProfileStartCapture");
        StopCapture = (void *)GetProcAddress(Profiler, "DwmProfileStopCapture");
        Capture = HeapAlloc(GetProcessHeap(), 0, sizeof(*Capture));
    }
    if (!Query || !Events || !Capture || !StartCapture || !StopCapture ||
        StripX + ARRAYSIZE(Pixels) >= Primary->Width || Top + 660 + 126 >= Primary->Height)
        goto Error;
    Stats.Type = D3DKMT_QUERYSTATISTICS_VIDPNSOURCE;
    Stats.AdapterLuid = Primary->Adapter.AdapterLuid;
    Stats.QueryVidPnSource.VidPnSourceId = Primary->Adapter.VidPnSourceId;
    ClientToScreen(Window, &ClientOrigin);
    CaptionY = Top + (ClientOrigin.y - Top) / 2;
    if (!SendMessageTimeoutW(Window, WM_NCHITTEST, 0,
        MAKELPARAM(Left + 500, CaptionY), SMTO_ABORTIFHUNG | SMTO_BLOCK,
        1000, &Hit) || Hit != HTCAPTION) goto Error;

    /* Track the right silhouette on one fixed primary-buffer scanline.
     * A settled move must displace it by exactly the commanded distance.
     * This measures visible-buffer progress independently of frame counters. */
    for (i = 0; i < 2; ++i)
    {
        SetWindowPos(Window, NULL, Left + i * 128, Top + i * 64, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        PumpMessages();
        Sleep(1500);
        if (!MotionRead(Primary, Offset, Pixels, sizeof(Pixels))) goto Error;
        for (j = 3; j < ARRAYSIZE(Pixels); ++j)
            if (!MotionColorMatches(Pixels[j], 0xff00ff) &&
                !MotionColorMatches(Pixels[j-1], 0xff00ff) &&
                !MotionColorMatches(Pixels[j-2], 0xff00ff)) Calibration[i] = StripX + j;
    }
    TestPrint("TASKMGR_PACING_CALIBRATION edge0=%ld edge1=%ld expected_delta=128 actual_delta=%ld\n",
              Calibration[0], Calibration[1], Calibration[1] - Calibration[0]);
    if (Calibration[0] < 0 || Calibration[1] - Calibration[0] != 128) goto Error;
    for (Phase = 0; Phase < ARRAYSIZE(Names); ++Phase)
    {
        LARGE_INTEGER Start, Now, Previous, A, B;
        ULONGLONG Elapsed, NextMove = 0, MaxGap = 0, MaxCursor = 0;
        ULONGLONG MaxQuery = 0, MaxRead = 0, QueryTotal = 0, ReadTotal = 0;
        ULONG Saved = 0, Samples = 0, Moves = 0, LastFrame = 0, Lost = 0;
        LONG LastEdge = -1, MinX = Left, MaxX = Left;
        BOOL HaveLast = FALSE;

        SetWindowPos(Window, NULL, Left, Top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetCursorPos(Left + 500, CaptionY);
        PumpMessages();
        Sleep(1500);
        if (Phase == 2 && FAILED(StartCapture(&Session))) goto Error;
        TestPrint("TASKMGR_PACING_BEGIN phase=%s duration_ms=15000 step_us=16000 caption_hit=%Iu\n",
                  Names[Phase], (SIZE_T)Hit);
        Button.type = INPUT_MOUSE;
        Button.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        if (SendInput(1, &Button, sizeof(Button)) != 1) goto Error;
        Held = TRUE;
        QueryPerformanceCounter(&Start);
        Previous = Start;
        for (;;)
        {
            ULONGLONG Gap, QueryUs, ReadUs = 0;
            ULONG Frame;
            LONG Edge = -1;
            QueryPerformanceCounter(&Now);
            Elapsed = ElapsedMicroseconds(Start, Now);
            if (Elapsed >= 15000000) break;
            Gap = ElapsedMicroseconds(Previous, Now);
            Previous = Now;
            MaxGap = max(MaxGap, Gap);
            if (Elapsed >= NextMove)
            {
                ULONG Step = (ULONG)(Elapsed / 16000) % 128;
                LONG Distance = (Step < 64 ? Step : 127 - Step) * 4;
                RECT Rect;
                QueryPerformanceCounter(&A);
                if (!SetCursorPos(Left + 500 + Distance, CaptionY + Distance/2)) ++Failures;
                QueryPerformanceCounter(&B);
                MaxCursor = max(MaxCursor, ElapsedMicroseconds(A, B));
                if (GetWindowRect(Window, &Rect))
                {
                    MinX = min(MinX, Rect.left);
                    MaxX = max(MaxX, Rect.left);
                }
                ++Moves;
                NextMove = (Elapsed / 16000 + 1) * 16000;
            }
            QueryPerformanceCounter(&A);
            if (Query(&Stats) < 0) goto Error;
            QueryPerformanceCounter(&B);
            QueryUs = ElapsedMicroseconds(A, B);
            QueryTotal += QueryUs;
            MaxQuery = max(MaxQuery, QueryUs);
            Frame = Stats.QueryResult.VidPnSourceInformation.GlobalInformation.Frame;
            if (Phase != 1)
            {
                QueryPerformanceCounter(&A);
                if (!MotionRead(Primary, Offset, Pixels, sizeof(Pixels))) goto Error;
                QueryPerformanceCounter(&B);
                ReadUs = ElapsedMicroseconds(A, B);
                ReadTotal += ReadUs;
                MaxRead = max(MaxRead, ReadUs);
                for (j = 3; j < ARRAYSIZE(Pixels); ++j)
                    if (!MotionColorMatches(Pixels[j], 0xff00ff) &&
                        !MotionColorMatches(Pixels[j-1], 0xff00ff) &&
                        !MotionColorMatches(Pixels[j-2], 0xff00ff)) Edge = StripX + j;
                if (Edge < 0) ++Failures;
            }
            QueryPerformanceCounter(&Now);
            if (!HaveLast || Frame != LastFrame || Edge != LastEdge)
            {
                if (Saved < 2048)
                {
                    Events[Saved].Time = ElapsedMicroseconds(Start, Now);
                    Events[Saved].PollGap = Gap;
                    Events[Saved].QueryUs = QueryUs;
                    Events[Saved].ReadUs = ReadUs;
                    Events[Saved].Frame = Frame;
                    Events[Saved].Queued = Stats.QueryResult.VidPnSourceInformation.GlobalInformation.QueuedPresent;
                    Events[Saved++].Edge = Edge;
                }
                else ++Lost;
                LastFrame = Frame;
                LastEdge = Edge;
                HaveLast = TRUE;
            }
            ++Samples;
            PumpMessages();
            Sleep(2);
        }
        Button.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        if (SendInput(1, &Button, sizeof(Button)) != 1) goto Error;
        Held = FALSE;
        if (Session)
        {
            HRESULT Status = StopCapture(Session, Capture, sizeof(*Capture));
            Session = 0;
            if (FAILED(Status)) goto Error;
        }
        TestPrint("TASKMGR_PACING_SUMMARY phase=%s samples=%lu moves=%lu events=%lu lost=%lu max_poll_gap_us=%llu max_cursor_us=%llu max_query_us=%llu max_read_us=%llu query_total_us=%llu read_total_us=%llu window_min_x=%ld window_max_x=%ld\n",
                  Names[Phase], Samples, Moves, Saved, Lost, MaxGap, MaxCursor,
                  MaxQuery, MaxRead, QueryTotal, ReadTotal, MinX, MaxX);
        for (i = 0; i < Saved; ++i)
            TestPrint("TASKMGR_PACING_EVENT phase=%s time_us=%llu frame=%lu edge=%ld queued=%lu poll_gap_us=%llu query_us=%llu read_us=%llu\n",
                      Names[Phase], Events[i].Time, Events[i].Frame, Events[i].Edge,
                      Events[i].Queued, Events[i].PollGap, Events[i].QueryUs, Events[i].ReadUs);
        if (Lost || Samples < 1000 || MaxX - MinX < 200) ++Failures;
    }
    goto Done;
Error:
    TestPrint("TASKMGR_PACING_ERROR last_error=%lu\n", GetLastError());
    ++Failures;
Done:
    if (Held)
    {
        Button.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &Button, sizeof(Button));
    }
    if (Session) StopCapture(Session, Capture, sizeof(*Capture));
    if (Capture) HeapFree(GetProcessHeap(), 0, Capture);
    if (Profiler) FreeLibrary(Profiler);
    if (Events) HeapFree(GetProcessHeap(), 0, Events);
    TestPrint("TASKMGR_PACING_END failures=%lu\n", Failures);
    return Failures ? 1 : 0;
}

static INT
RunTaskmgrMotionProbe(const RECT *WorkArea, BOOL Pacing)
{
    static const char *Names[] = {"stationary", "cursor", "window", "caption_drag",
                                 "pattern_stationary", "pattern_caption_drag"};
    struct { POINT Point; ULONG Color; } Points[MOTION_POINTS];
    struct { ULONG Sample, Point, Actual, Frame; } Events[MOTION_EVENTS];
    MOTION_PRIMARY Primary = {0};
    PROCESS_INFORMATION Process = {0};
    STARTUPINFOW Startup = {sizeof(Startup)};
    TASKMGR_WINDOW_SEARCH Search = {0};
    WINDOWPLACEMENT Placement = {sizeof(Placement)};
    POINT Cursor = {0};
    HWND Background = NULL, Console = GetConsoleWindow();
    HWND Existing = FindWindowW(L"TaskManager11Frame", NULL);
    HDC Screen = NULL;
    WCHAR Path[MAX_PATH];
    ULONG *Images[3] = {0};
    SIZE_T ImageBytes, Offset;
    LONG Left, Top, X, Y, Dx, Dy;
    ULONG i, j, Phase, Count = 0, Failures = 0;
    BOOL Topmost = FALSE, Positioned = FALSE, HaveCursor = GetCursorPos(&Cursor);
    BOOL ButtonHeld = FALSE;
    INPUT Button = {0};
    DWORD Begin;

    if (WorkArea->right - WorkArea->left < 1160 || WorkArea->bottom - WorkArea->top < 760)
        return 1;
    Left = WorkArea->left + (WorkArea->right - WorkArea->left - 1000 - MOTION_DX) / 2;
    Top = WorkArea->top + (WorkArea->bottom - WorkArea->top - 660 - MOTION_DY) / 2;
    if (Console) ShowWindow(Console, SW_HIDE);
    if (Existing)
        Search.Window = Existing;
    else
    {
        if (!GetSystemDirectoryW(Path, ARRAYSIZE(Path) - 32)) goto Error;
        lstrcatW(Path, L"\\taskmgr11.exe");
        if (!CreateProcessW(Path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
            goto Error;
        CloseHandle(Process.hThread);
        Search.ProcessId = Process.dwProcessId;
        Begin = GetTickCount();
        do
        {
            EnumWindows(FindTaskmgrWindow, (LPARAM)&Search);
            if (Search.Window) break;
            PumpMessages();
            Sleep(100);
        } while (GetTickCount() - Begin < 15000);
    }
    if (!Search.Window || !GetWindowPlacement(Search.Window, &Placement)) goto Error;
    Topmost = (GetWindowLongPtrW(Search.Window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    Background = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        BackgroundClassName, L"Taskmgr11 flicker probe backdrop", WS_POPUP,
        WorkArea->left, WorkArea->top, WorkArea->right - WorkArea->left,
        WorkArea->bottom - WorkArea->top, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!Background) goto Error;
    SetWindowLongPtrW(Background, GWLP_WNDPROC, (LONG_PTR)MotionBackdropProcedure);
    ShowWindow(Background, SW_SHOWNOACTIVATE);
    UpdateWindow(Background);
    ShowWindow(Search.Window, SW_RESTORE);
    if (!SetWindowPos(Search.Window, HWND_TOPMOST, Left, Top, 1000, 660, SWP_SHOWWINDOW))
        goto Error;
    Positioned = TRUE;
    SetForegroundWindow(Search.Window);
    SetCursorPos(WorkArea->right - 20, WorkArea->bottom - 20);
    Screen = GetDC(NULL);
    if (!Screen || !MotionOpenPrimary(&Primary, Screen)) goto Error;
    if (Pacing)
    {
        Failures += MotionMeasurePacing(Search.Window, &Primary, Left, Top);
        goto Cleanup;
    }
    ImageBytes = ((SIZE_T)659 * Primary.Width + 1000) * sizeof(ULONG);
    Offset = ((SIZE_T)Top * Primary.Width + Left) * sizeof(ULONG);
    for (i = 0; i < ARRAYSIZE(Images); ++i)
    {
        Images[i] = HeapAlloc(GetProcessHeap(), 0, ImageBytes);
        if (!Images[i]) goto Error;
        PumpMessages();
        Sleep(i ? 600 : 2000);
        if (!MotionRead(&Primary, Offset, Images[i], ImageBytes)) goto Error;
    }
    /* Require the entire swept rectangle to be one stable, non-backdrop
     * color in all three references. This excludes text and animated data. */
    for (Y = 100; Y < 620 && Count < MOTION_POINTS; Y += 24)
    for (X = 100; X < 960 && Count < MOTION_POINTS; X += 40)
    {
        ULONG Expected = Images[0][(SIZE_T)Y * Primary.Width + X] & 0xffffff;
        BOOL Stable = TRUE;
        if (MotionColorMatches(Expected, 0xff00ff)) continue;
        for (i = 0; i < ARRAYSIZE(Images) && Stable; ++i)
        for (Dy = 0; Dy <= MOTION_DY && Stable; ++Dy)
        for (Dx = 0; Dx <= MOTION_DX; ++Dx)
        {
            if (!MotionColorMatches(Images[i][(SIZE_T)(Y-Dy) * Primary.Width + X-Dx], Expected))
            {
                Stable = FALSE;
                break;
            }
        }
        if (Stable)
        {
            Points[Count].Point.x = Left + X;
            Points[Count].Point.y = Top + Y;
            Points[Count].Color = Expected;
            TestPrint("TASKMGR_FLICKER_POINT index=%lu x=%ld y=%ld expected=%06lx\n",
                      Count, Left+X, Top+Y, Expected);
            ++Count;
        }
    }
    for (i = 0; i < ARRAYSIZE(Images); ++i)
    {
        HeapFree(GetProcessHeap(), 0, Images[i]);
        Images[i] = NULL;
    }
    if (Count < 4)
    {
        TestPrint("TASKMGR_FLICKER_ERROR insufficient_invariants=%lu\n", Count);
        goto Error;
    }
    TestPrint("TASKMGR_FLICKER_BEGIN window=%p class=TaskManager11Frame rect=%ld,%ld,1000,660 points=%lu\n",
              Search.Window, Left, Top, Count);
    /* A deliberate hide must expose the magenta backdrop through this same
     * mapping. It validates the detector separately from the motion phases. */
    ShowWindow(Search.Window, SW_HIDE);
    PumpMessages();
    Sleep(1500);
    j = 0;
    for (i = 0; i < Count; ++i)
    {
        ULONG Pixel;
        Offset = ((SIZE_T)Points[i].Point.y * Primary.Width + Points[i].Point.x) * sizeof(ULONG);
        if (MotionRead(&Primary, Offset, &Pixel, sizeof(Pixel)) &&
            MotionColorMatches(Pixel, 0xff00ff)) ++j;
    }
    TestPrint("TASKMGR_FLICKER_CONTROL deliberately_hidden=1 backdrop_samples=%lu expected=%lu\n", j, Count);
    ShowWindow(Search.Window, SW_SHOWNOACTIVATE);
    SetForegroundWindow(Search.Window);
    PumpMessages();
    Sleep(1500);
    if (j != Count) goto Error;
    for (Phase = 0; Phase < ARRAYSIZE(Names); ++Phase)
    {
        ULONG Samples = 0, Bad = 0, ReadFailures = 0, Moves = 0, Saved = 0;
        ULONG Before = 0, After = 0, Queued = 0;
        DWORD NextMove, Duration = Phase == 0 || Phase == 4 ? 5000 : 10000;
        LARGE_INTEGER Previous, Now, CallStart, CallEnd;
        ULONGLONG MaxGap = 0, MaxCursor = 0, MaxMove = 0, MaxRead = 0;
        LONG CaptionY = Top + 16, MinX = Left, MaxX = Left;
        BOOL HaveFrames;
        BOOL CaptionDrag = Phase == 3 || Phase == 5;

        SetWindowPos(Search.Window, HWND_TOPMOST, Left, Top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        SetCursorPos(WorkArea->right - 20, WorkArea->bottom - 20);
        PumpMessages();
        Sleep(100);
        if (Phase == 4)
        {
            /* The foreground's swept patches were proved uniform above.
             * At each fixed screen point both the background and foreground
             * therefore stay constant during movement, including blur edges. */
            SetWindowLongPtrW(Background, GWLP_USERDATA, 1);
            InvalidateRect(Background, NULL, FALSE);
            UpdateWindow(Background);
            PumpMessages();
            Sleep(1500);
            for (j = 0; j < Count; ++j)
            {
                Offset = ((SIZE_T)Points[j].Point.y * Primary.Width + Points[j].Point.x) * sizeof(ULONG);
                if (!MotionRead(&Primary, Offset, &Points[j].Color, sizeof(ULONG))) goto Error;
                Points[j].Color &= 0xffffff;
                TestPrint("TASKMGR_FLICKER_PATTERN_POINT index=%lu x=%ld y=%ld expected=%06lx\n",
                          j, Points[j].Point.x, Points[j].Point.y, Points[j].Color);
            }
        }
        if (CaptionDrag)
        {
            POINT ClientOrigin = {0};
            DWORD_PTR Hit = 0;
            ClientToScreen(Search.Window, &ClientOrigin);
            CaptionY = Top + (ClientOrigin.y - Top) / 2;
            if (!SendMessageTimeoutW(Search.Window, WM_NCHITTEST, 0,
                MAKELPARAM(Left + 500, CaptionY), SMTO_ABORTIFHUNG | SMTO_BLOCK,
                1000, &Hit) || Hit != HTCAPTION)
            {
                TestPrint("TASKMGR_FLICKER_ERROR caption_hit=%Iu\n", (SIZE_T)Hit);
                goto Error;
            }
            SetCursorPos(Left + 500, CaptionY);
            Button.type = INPUT_MOUSE;
            Button.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            if (SendInput(1, &Button, sizeof(Button)) != 1) goto Error;
            ButtonHeld = TRUE;
            TestPrint("TASKMGR_FLICKER_DRAG_INPUT caption_hit=%Iu button_down=1\n", (SIZE_T)Hit);
        }
        HaveFrames = QueryDesktopFrames(&Before, &Queued);
        QueryPerformanceCounter(&Previous);
        Begin = NextMove = GetTickCount();
        while (GetTickCount() - Begin < Duration)
        {
            DWORD Tick = GetTickCount();
            QueryPerformanceCounter(&Now);
            if (Samples) MaxGap = max(MaxGap, ElapsedMicroseconds(Previous, Now));
            Previous = Now;
            if ((LONG)(Tick - NextMove) >= 0)
            {
                ULONG Step = Moves % 64;
                LONG Distance = (Step < 32 ? Step : 63-Step) * 2;
                QueryPerformanceCounter(&CallStart);
                if (Phase == 2 && !SetWindowPos(Search.Window, NULL,
                    Left + Distance, Top + Distance/2, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) ++Failures;
                QueryPerformanceCounter(&CallEnd);
                if (Phase == 2) MaxMove = max(MaxMove, ElapsedMicroseconds(CallStart, CallEnd));
                QueryPerformanceCounter(&CallStart);
                if (Phase == 1 && !SetCursorPos(Left + 100 + (Moves * 17) % 800,
                                               Top + 100 + (Moves * 11) % 460)) ++Failures;
                if (CaptionDrag && !SetCursorPos(Left + 500 + Distance, CaptionY + Distance/2))
                    ++Failures;
                QueryPerformanceCounter(&CallEnd);
                if (Phase == 1 || CaptionDrag) MaxCursor = max(MaxCursor, ElapsedMicroseconds(CallStart, CallEnd));
                if (CaptionDrag)
                {
                    RECT Moved;
                    if (GetWindowRect(Search.Window, &Moved))
                    {
                        MinX = min(MinX, Moved.left);
                        MaxX = max(MaxX, Moved.left);
                    }
                }
                ++Moves;
                NextMove = Tick + 16;
            }
            QueryPerformanceCounter(&CallStart);
            for (j = 0; j < Count; ++j)
            {
                ULONG Pixel = 0;
                Offset = ((SIZE_T)Points[j].Point.y * Primary.Width + Points[j].Point.x) * sizeof(ULONG);
                if (!MotionRead(&Primary, Offset, &Pixel, sizeof(Pixel)))
                    ++ReadFailures;
                else if (!MotionColorMatches(Pixel, Points[j].Color))
                {
                    ++Bad;
                    if (Saved < MOTION_EVENTS)
                    {
                        Events[Saved].Sample = Samples;
                        Events[Saved].Point = j;
                        Events[Saved].Actual = Pixel & 0xffffff;
                        QueryDesktopFrames(&Events[Saved].Frame, &Queued);
                        ++Saved;
                    }
                }
            }
            QueryPerformanceCounter(&CallEnd);
            MaxRead = max(MaxRead, ElapsedMicroseconds(CallStart, CallEnd));
            ++Samples;
            PumpMessages();
            Sleep(2);
        }
        if (ButtonHeld)
        {
            Button.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            if (SendInput(1, &Button, sizeof(Button)) != 1) ++Failures;
            else ButtonHeld = FALSE;
        }
        HaveFrames = QueryDesktopFrames(&After, &Queued) && HaveFrames;
        for (j = 0; j < Saved; ++j)
            TestPrint("TASKMGR_FLICKER_BAD phase=%s sample=%lu point=%lu expected=%06lx actual=%06lx frame=%lu\n",
                      Names[Phase], Events[j].Sample, Events[j].Point,
                      Points[Events[j].Point].Color, Events[j].Actual, Events[j].Frame);
        TestPrint("TASKMGR_FLICKER_PHASE name=%s samples=%lu points=%lu bad_pixels=%lu read_failures=%lu moves=%lu frames=%lu frame_stats=%u max_sample_gap_us=%llu\n",
                  Names[Phase], Samples, Count, Bad, ReadFailures, Moves,
                  After-Before, HaveFrames, MaxGap);
        TestPrint("TASKMGR_FLICKER_CALLS phase=%s max_cursor_us=%llu max_setwindowpos_us=%llu max_read_batch_us=%llu window_min_x=%ld window_max_x=%ld\n",
                  Names[Phase], MaxCursor, MaxMove, MaxRead, MinX, MaxX);
        if (Bad || ReadFailures || Samples < Duration/40 || !HaveFrames ||
            ((Phase == 2 || CaptionDrag) && After == Before) ||
            (CaptionDrag && MaxX - MinX < MOTION_DX/2)) ++Failures;
    }
    goto Cleanup;
Error:
    TestPrint("TASKMGR_FLICKER_ERROR setup=%lu\n", GetLastError());
    ++Failures;
Cleanup:
    if (ButtonHeld)
    {
        Button.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &Button, sizeof(Button));
    }
    for (i = 0; i < ARRAYSIZE(Images); ++i)
        if (Images[i]) HeapFree(GetProcessHeap(), 0, Images[i]);
    MotionClosePrimary(&Primary);
    if (Screen) ReleaseDC(NULL, Screen);
    if (Positioned)
    {
        SetWindowPlacement(Search.Window, &Placement);
        SetWindowPos(Search.Window, Topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (Background) DestroyWindow(Background);
    if (!Existing && Search.Window) PostMessageW(Search.Window, WM_CLOSE, 0, 0);
    if (Process.hProcess) CloseHandle(Process.hProcess);
    if (HaveCursor) SetCursorPos(Cursor.x, Cursor.y);
    TestPrint("TASKMGR_FLICKER_END failures=%lu\n", Failures);
    return Failures ? 1 : 0;
}
