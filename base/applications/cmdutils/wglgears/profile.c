/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * One capture owns both the application's ICD and the desktop capture.
 * Buffer frame timings in memory; format and sort only after capture stops.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <reactos/dwmprof.h>
#include <reactos/dwmpresenttracenames.h>
#include "profile.h"

#define PROFILE_FRAME_CAPACITY 131072
#define PROFILE_TOP_FRAMES 20

typedef struct _PROFILE_FRAME
{
    ULONGLONG Start, DrawEnd, End, Gap;
    ULONG Number;
} PROFILE_FRAME;

typedef struct _PROFILE_COUNTER
{
    ULONGLONG Count, Sum, Minimum, Maximum, OverRefresh;
} PROFILE_COUNTER;

static struct
{
    HMODULE Library;
    HRESULT (WINAPI *StopCapture)(ULONG, DPT_SNAPSHOT *, ULONG);
    PFNWGLCONTROLPRESENTATIONTRACEROS IcdControl;
    DPT_SNAPSHOT Desktop;
    DPT_DOMAIN Icd;
    DPT_REQUEST Request;
    ULONG Session, Saved, Lost, FailedPresents;
    PROFILE_FRAME *Frames, Current;
    PROFILE_COUNTER Counter[4];
    ULONGLONG Frequency, Start, End, Previous, KernelStart, UserStart, RefreshTicks;
    LONG DesktopStatus, IcdStatus;
    BOOL CpuValid;
} Profile;

static ULONGLONG Now(void)
{
    LARGE_INTEGER Counter;
    QueryPerformanceCounter(&Counter);
    return Counter.QuadPart;
}

static double Milliseconds(ULONGLONG Ticks, ULONGLONG Frequency)
{
    return Frequency ? (double)Ticks * 1000.0 / Frequency : 0.0;
}

static BOOL CpuTimes(ULONGLONG *Kernel, ULONGLONG *User)
{
    FILETIME Created, Exited, K, U;
    if (!GetProcessTimes(GetCurrentProcess(), &Created, &Exited, &K, &U))
        return FALSE;
    *Kernel = ((ULONGLONG)K.dwHighDateTime << 32) | K.dwLowDateTime;
    *User = ((ULONGLONG)U.dwHighDateTime << 32) | U.dwLowDateTime;
    return TRUE;
}

BOOL WglGearsProfileStart(PROC IcdControl)
{
    HRESULT (WINAPI *StartCapture)(ULONG *);
    LARGE_INTEGER Frequency;
    HDC Dc;
    INT Refresh = 0;
    ULONG i;

    ZeroMemory(&Profile, sizeof(Profile));
    Profile.Frames = HeapAlloc(GetProcessHeap(), 0, PROFILE_FRAME_CAPACITY * sizeof(*Profile.Frames));
    if (!Profile.Frames || !QueryPerformanceFrequency(&Frequency) || Frequency.QuadPart <= 0)
    {
        HeapFree(GetProcessHeap(), 0, Profile.Frames);
        Profile.Frames = NULL;
        return FALSE;
    }
    Profile.Frequency = Frequency.QuadPart;
    for (i = 0; i < ARRAYSIZE(Profile.Counter); ++i)
        Profile.Counter[i].Minimum = ~(ULONGLONG)0;
    Dc = GetDC(NULL);
    if (Dc)
    {
        Refresh = GetDeviceCaps(Dc, VREFRESH);
        ReleaseDC(NULL, Dc);
    }
    Profile.RefreshTicks = Profile.Frequency / (Refresh > 1 ? Refresh : 60);
    Profile.DesktopStatus = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    Profile.IcdStatus = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    Profile.Library = LoadLibraryW(L"dwmprof.dll");
    if (Profile.Library)
    {
        StartCapture = (void *)GetProcAddress(Profile.Library, "DwmProfileStartCapture");
        Profile.StopCapture = (void *)GetProcAddress(Profile.Library, "DwmProfileStopCapture");
        Profile.DesktopStatus = E_NOINTERFACE;
        if (StartCapture && Profile.StopCapture)
            Profile.DesktopStatus = StartCapture(&Profile.Session);
    }
    Profile.IcdControl = (PFNWGLCONTROLPRESENTATIONTRACEROS)IcdControl;
    Profile.Request.Size = sizeof(Profile.Request);
    Profile.Request.Version = DPT_VERSION;
    Profile.Request.Operation = DPT_START;
    Profile.Request.Session = Profile.Session ? Profile.Session : (GetTickCount() | 1);
    if (Profile.IcdControl)
        Profile.IcdStatus = Profile.IcdControl(&Profile.Request, &Profile.Icd, sizeof(Profile.Icd)) ?
            S_OK : HRESULT_FROM_WIN32(GetLastError());
    printf("GEARS_PROFILE_ARMED session=%lu app_icd=%08lx desktop=%08lx\n",
           Profile.Session, Profile.IcdStatus, Profile.DesktopStatus);
    fflush(stdout);
    Profile.CpuValid = CpuTimes(&Profile.KernelStart, &Profile.UserStart);
    Profile.Start = Now();
    return TRUE;
}

void WglGearsProfileFrameStart(void)
{
    Profile.Current.Start = Now();
    Profile.Current.Gap = Profile.Previous ? Profile.Current.Start - Profile.Previous : 0;
}

void WglGearsProfileDrawEnd(void)
{
    Profile.Current.DrawEnd = Now();
}

static void Count(PROFILE_COUNTER *Counter, ULONGLONG Ticks)
{
    ++Counter->Count;
    Counter->Sum += Ticks;
    if (Ticks < Counter->Minimum) Counter->Minimum = Ticks;
    if (Ticks > Counter->Maximum) Counter->Maximum = Ticks;
    if (Ticks > Profile.RefreshTicks) ++Counter->OverRefresh;
}

void WglGearsProfileFrameEnd(BOOL Presented)
{
    PROFILE_FRAME *Frame = &Profile.Current;
    Frame->End = Now();
    Frame->Number = (ULONG)Profile.Counter[0].Count + 1;
    Count(&Profile.Counter[0], Frame->End - Frame->Start + Frame->Gap);
    Count(&Profile.Counter[1], Frame->DrawEnd - Frame->Start);
    Count(&Profile.Counter[2], Frame->End - Frame->DrawEnd);
    if (Profile.Previous) Count(&Profile.Counter[3], Frame->Gap);
    Profile.Previous = Frame->End;
    if (!Presented) ++Profile.FailedPresents;
    if (Profile.Saved < PROFILE_FRAME_CAPACITY)
        Profile.Frames[Profile.Saved++] = *Frame;
    else
        ++Profile.Lost;
}

static ULONGLONG Interval(const PROFILE_FRAME *Frame)
{
    return Frame->End - Frame->Start + Frame->Gap;
}

static int CompareFrames(const void *Left, const void *Right)
{
    ULONGLONG A = Interval(Left), B = Interval(Right);
    return A < B ? 1 : A > B ? -1 : 0;
}

static BOOL DumpDomain(const char *Name, const DPT_DOMAIN *Domain)
{
    ULONG Metric;
    BOOL Valid = TRUE;
    if (Domain->Size != sizeof(*Domain) || Domain->Version != DPT_VERSION || !Domain->Frequency)
    {
        printf("GEARS_PROFILE_DOMAIN_ERROR domain=%s size=%lu version=%lu\n", Name, Domain->Size, Domain->Version);
        return FALSE;
    }
    printf("GEARS_PROFILE_DOMAIN domain=%s pid=%lu start_qpc=%I64u stop_qpc=%I64u frequency=%I64u\n",
           Name, Domain->ProcessId, Domain->Start, Domain->Stop, Domain->Frequency);
    for (Metric = 0; Metric < DPT_METRIC_COUNT; ++Metric)
    {
        const DPT_COUNTER *Counter = &Domain->Counter[Metric];
        char Minimum[24], Average[24], Maximum[24];
        if (!Counter->Entered)
            continue;
        if (Counter->Completed > Counter->Entered || Counter->Failed > Counter->Completed ||
            Counter->MaxTicks > Counter->Ticks ||
            (Counter->MinTicks != ~(ULONGLONG)0 && Counter->MinTicks > Counter->MaxTicks))
            Valid = FALSE;
        if (Counter->Completed && Counter->MinTicks != ~(ULONGLONG)0)
        {
            _snprintf(Minimum, sizeof(Minimum), "%.3f", Milliseconds(Counter->MinTicks, Domain->Frequency));
            _snprintf(Average, sizeof(Average), "%.3f", Milliseconds(Counter->Ticks, Domain->Frequency) / Counter->Completed);
            _snprintf(Maximum, sizeof(Maximum), "%.3f", Milliseconds(Counter->MaxTicks, Domain->Frequency));
        }
        else
        {
            strcpy(Minimum, "--"); strcpy(Average, "--"); strcpy(Maximum, "--");
        }
        printf("%-10s %-26s %8I64u %9s %9s %9s %11.3f %6I64u %7I64u\n",
               Name, DptMetricName(Metric), Counter->Completed, Minimum, Average, Maximum,
               Milliseconds(Counter->Ticks, Domain->Frequency), Counter->Failed,
               Counter->Entered - Counter->Completed);
    }
    return Valid;
}

BOOL WglGearsProfileStop(void)
{
    static const char *Names[] = {"present_interval", "draw", "SwapBuffers", "outside_frame"};
    ULONGLONG Kernel, User;
    ULONG i, First, Second;
    BOOL Valid;

    Profile.End = Now();
    Profile.CpuValid = Profile.CpuValid && CpuTimes(&Kernel, &User);
    Profile.Request.Operation = DPT_STOP;
    if (SUCCEEDED(Profile.IcdStatus))
        Profile.IcdStatus = Profile.IcdControl(&Profile.Request, &Profile.Icd, sizeof(Profile.Icd)) ?
            S_OK : HRESULT_FROM_WIN32(GetLastError());
    if (Profile.Session)
        Profile.DesktopStatus = Profile.StopCapture(Profile.Session, &Profile.Desktop, sizeof(Profile.Desktop));
    Valid = SUCCEEDED(Profile.IcdStatus) && SUCCEEDED(Profile.DesktopStatus) &&
            Profile.Desktop.Available == 7 && Profile.Saved && !Profile.Lost && !Profile.FailedPresents;

    printf("GEARS_PROFILE_BEGIN version=%u duration_ms=%.3f frames=%I64u saved=%lu lost=%lu failed_presents=%lu\n",
           DPT_VERSION, Milliseconds(Profile.End - Profile.Start, Profile.Frequency),
           Profile.Counter[0].Count, Profile.Saved, Profile.Lost, Profile.FailedPresents);
    printf("QPC wall times in ms; nested stages overlap. GPU dispatch-to-retire includes queuing and completion delivery.\n");
    printf("App samples measure SwapBuffers completion, not display scanout. Kernel counters include other GPU clients.\n");
    printf("domain     stage                         calls    min_ms    avg_ms    max_ms    total_ms failed pending\n");
    for (i = 0; i < ARRAYSIZE(Profile.Counter); ++i)
    {
        const PROFILE_COUNTER *Counter = &Profile.Counter[i];
        if (Counter->Count)
            printf("%-10s %-26s %8I64u %9.3f %9.3f %9.3f %11.3f %6u %7u\n",
                   "app", Names[i], Counter->Count, Milliseconds(Counter->Minimum, Profile.Frequency),
                   Milliseconds(Counter->Sum, Profile.Frequency) / Counter->Count,
                   Milliseconds(Counter->Maximum, Profile.Frequency), Milliseconds(Counter->Sum, Profile.Frequency), 0, 0);
    }
    if (SUCCEEDED(Profile.IcdStatus)) Valid = DumpDomain("app-ICD", &Profile.Icd) && Valid;
    if (SUCCEEDED(Profile.DesktopStatus))
    {
        static const char *Domains[] = {"DWM", "DWM-ICD", "kernel"};
        for (i = 0; i < ARRAYSIZE(Domains); ++i)
        {
            if ((Profile.Desktop.Available & (1u << i)) && SUCCEEDED(Profile.Desktop.Status[i]))
                Valid = DumpDomain(Domains[i], &Profile.Desktop.Domain[i]) && Valid;
            else
            {
                printf("GEARS_PROFILE_DOMAIN_ERROR domain=%s status=%08lx\n", Domains[i], Profile.Desktop.Status[i]);
                Valid = FALSE;
            }
        }
    }
    printf("GEARS_PROFILE_STATUS app_icd=%08lx desktop=%08lx available=%lx\n",
           Profile.IcdStatus, Profile.DesktopStatus, Profile.Desktop.Available);
    if (Profile.CpuValid)
        printf("GEARS_PROFILE_CPU app_kernel_ms=%.3f app_user_ms=%.3f dwm_kernel_ms=%.3f dwm_user_ms=%.3f\n",
               (double)(Kernel - Profile.KernelStart) / 10000.0, (double)(User - Profile.UserStart) / 10000.0,
               (double)Profile.Desktop.CpuKernel100ns / 10000.0, (double)Profile.Desktop.CpuUser100ns / 10000.0);

    /* Report every sampled second before sorting the retained frame buffer. */
    for (First = 0; First < Profile.Saved; First = i)
    {
        ULONG Worst = First;
        ULONGLONG Sum = 0;
        Second = (ULONG)((Profile.Frames[First].Start - Profile.Start) / Profile.Frequency);
        for (i = First; i < Profile.Saved &&
             (Profile.Frames[i].Start - Profile.Start) / Profile.Frequency == Second; ++i)
        {
            Sum += Interval(&Profile.Frames[i]);
            if (Interval(&Profile.Frames[i]) > Interval(&Profile.Frames[Worst])) Worst = i;
        }
        printf("GEARS_PROFILE_SECOND second=%lu frames=%lu avg_ms=%.3f max_ms=%.3f worst_frame=%lu\n",
               Second, i - First, Milliseconds(Sum, Profile.Frequency) / (i - First),
               Milliseconds(Interval(&Profile.Frames[Worst]), Profile.Frequency), Profile.Frames[Worst].Number);
    }
    qsort(Profile.Frames, Profile.Saved, sizeof(*Profile.Frames), CompareFrames);
    if (Profile.Saved)
    {
        printf("GEARS_PROFILE_PACING p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f refresh_budget_ms=%.3f over_budget=%I64u\n",
               Milliseconds(Interval(&Profile.Frames[(Profile.Saved - 1) / 2]), Profile.Frequency),
               Milliseconds(Interval(&Profile.Frames[(Profile.Saved - 1) / 20]), Profile.Frequency),
               Milliseconds(Interval(&Profile.Frames[(Profile.Saved - 1) / 100]), Profile.Frequency),
               Milliseconds(Profile.RefreshTicks, Profile.Frequency), Profile.Counter[0].OverRefresh);
        printf("Worst present intervals: frame start_s interval_ms draw_ms swap_ms outside_ms start_qpc end_qpc\n");
    }
    for (i = 0; i < Profile.Saved && i < PROFILE_TOP_FRAMES; ++i)
    {
        const PROFILE_FRAME *Frame = &Profile.Frames[i];
        printf("GEARS_PROFILE_STALL frame=%lu time_s=%.6f interval_ms=%.3f draw_ms=%.3f swap_ms=%.3f outside_ms=%.3f start_qpc=%I64u end_qpc=%I64u\n",
               Frame->Number, (double)(Frame->Start - Profile.Start) / Profile.Frequency,
               Milliseconds(Interval(Frame), Profile.Frequency), Milliseconds(Frame->DrawEnd - Frame->Start, Profile.Frequency),
               Milliseconds(Frame->End - Frame->DrawEnd, Profile.Frequency), Milliseconds(Frame->Gap, Profile.Frequency), Frame->Start, Frame->End);
    }
    printf("GEARS_PROFILE_END result=%s\n", Valid ? "complete" : "incomplete");
    fflush(stdout);
    if (Profile.Library) FreeLibrary(Profile.Library);
    HeapFree(GetProcessHeap(), 0, Profile.Frames);
    Profile.Frames = NULL;
    return Valid;
}
