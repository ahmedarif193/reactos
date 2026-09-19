/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * On-demand capture serviced by the host notification thread, independently
 * of the compositor. WM_COPYDATA copies wire data, never process pointers.
 */
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "presenttrace.h"

DPT_BANK g_DwmPresentTrace;
static PVOID volatile IcdControl;
static SRWLOCK IcdLock = SRWLOCK_INIT;
static ULONG IcdSession;
static DPT_SNAPSHOT Capture, LastCompleted;
static volatile LONG DispatchBusy;
static ULONGLONG CpuKernelStart, CpuUserStart;
static BOOL CpuStartValid;

/* Temporary on-demand cadence evidence; not part of the wire protocol. */
#define DWM_CADENCE_CAPACITY 4096
static struct
{
    ULONGLONG End, Interval, Work, Draw, Present;
} FrameSamples[DWM_CADENCE_CAPACITY];
static ULONGLONG FrameScratch[DWM_CADENCE_CAPACITY], FramePreviousEnd;
static LONG FrameEpoch;
static ULONG FrameSession, FrameCount, FrameLost;
static struct
{
    ULONGLONG End;
    DWM_GPU_WAIT_TIMING Timing;
    BOOL Complete;
} GpuWaitSamples[DWM_CADENCE_CAPACITY];
static LONG GpuWaitEpoch;
static ULONG GpuWaitSession, GpuWaitCount, GpuWaitLost;

void DwmPresentTraceGpuWait(DPT_SCOPE Scope, DWM_GPU_WAIT_TIMING *Timing, BOOL Complete)
{
    ULONGLONG End;
    if (!Scope.Epoch || DPT_READ(&g_DwmPresentTrace.Epoch) != Scope.Epoch)
        return;
    End = DptNow();
    Timing->Ticks[DWM_WAIT_TOTAL] = End - Scope.Start;
    InterlockedIncrement(&g_DwmPresentTrace.Writers);
    if (DPT_READ(&g_DwmPresentTrace.Epoch) == Scope.Epoch)
    {
        if (GpuWaitEpoch != Scope.Epoch)
        {
            GpuWaitEpoch = Scope.Epoch;
            GpuWaitSession = g_DwmPresentTrace.Data.Session;
            GpuWaitCount = GpuWaitLost = 0;
        }
        if (GpuWaitCount < DWM_CADENCE_CAPACITY)
        {
            GpuWaitSamples[GpuWaitCount].End = End;
            GpuWaitSamples[GpuWaitCount].Timing = *Timing;
            GpuWaitSamples[GpuWaitCount].Complete = Complete;
            ++GpuWaitCount;
        }
        else
            ++GpuWaitLost;
    }
    InterlockedDecrement(&g_DwmPresentTrace.Writers);
}

void DwmPresentTraceFrame(DPT_SCOPE Scope, ULONGLONG DrawStart,
                          ULONGLONG DrawEnd, ULONGLONG PresentEnd)
{
    if (!Scope.Epoch || DPT_READ(&g_DwmPresentTrace.Epoch) != Scope.Epoch)
        return;
    InterlockedIncrement(&g_DwmPresentTrace.Writers);
    if (DPT_READ(&g_DwmPresentTrace.Epoch) == Scope.Epoch)
    {
        if (FrameEpoch != Scope.Epoch)
        {
            FrameEpoch = Scope.Epoch;
            FrameSession = g_DwmPresentTrace.Data.Session;
            FrameCount = FrameLost = 0;
            FramePreviousEnd = 0;
        }
        if (FrameCount < DWM_CADENCE_CAPACITY)
        {
            FrameSamples[FrameCount].End = PresentEnd;
            FrameSamples[FrameCount].Interval = FramePreviousEnd ? PresentEnd - FramePreviousEnd : 0;
            FrameSamples[FrameCount].Work = PresentEnd - Scope.Start;
            FrameSamples[FrameCount].Draw = DrawEnd - DrawStart;
            FrameSamples[FrameCount].Present = PresentEnd - DrawEnd;
            ++FrameCount;
        }
        else
            ++FrameLost;
        FramePreviousEnd = PresentEnd;
    }
    InterlockedDecrement(&g_DwmPresentTrace.Writers);
}

static int DwmCompareTicks(const void *Left, const void *Right)
{
    ULONGLONG A = *(const ULONGLONG *)Left, B = *(const ULONGLONG *)Right;
    return (A > B) - (A < B);
}

static void DwmDumpGpuWait(ULONG Session, ULONGLONG Frequency, ULONGLONG Start)
{
    static const char *Names[] = {"end", "flush", "query", "removed", "sleep", "total"};
    ULONG Phase, Index, Worst[6] = {0}, Slot, Failed = 0;
    ULONGLONG Polls = 0, Sleeps = 0, MaxSleep = 0;
    char Line[384];
    if (GpuWaitSession != Session || !Frequency || !GpuWaitCount)
        return;
    for (Index = 0; Index < GpuWaitCount; ++Index)
    {
        Polls += GpuWaitSamples[Index].Timing.Polls;
        Sleeps += GpuWaitSamples[Index].Timing.Sleeps;
        MaxSleep = max(MaxSleep, GpuWaitSamples[Index].Timing.MaxSleep);
        Failed += !GpuWaitSamples[Index].Complete;
        for (Slot = 0; Slot < ARRAYSIZE(Worst); ++Slot)
            if (!Worst[Slot] || GpuWaitSamples[Index].Timing.Ticks[DWM_WAIT_TOTAL] >
                               GpuWaitSamples[Worst[Slot] - 1].Timing.Ticks[DWM_WAIT_TOTAL])
            {
                ULONG Move;
                for (Move = ARRAYSIZE(Worst) - 1; Move > Slot; --Move) Worst[Move] = Worst[Move - 1];
                Worst[Slot] = Index + 1;
                break;
            }
    }
    _snprintf(Line, sizeof(Line), "DWM_GPU_WAIT_COUNTS samples=%lu lost=%lu failed=%lu polls=%llu sleeps=%llu max_sleep_us=%llu\n",
              GpuWaitCount, GpuWaitLost, Failed, Polls, Sleeps, MaxSleep * 1000000 / Frequency);
    Line[sizeof(Line) - 1] = 0;
    OutputDebugStringA(Line);
    for (Phase = 0; Phase < DWM_WAIT_PHASE_COUNT; ++Phase)
    {
        ULONGLONG Sum = 0;
        for (Index = 0; Index < GpuWaitCount; ++Index)
        {
            FrameScratch[Index] = GpuWaitSamples[Index].Timing.Ticks[Phase];
            Sum += FrameScratch[Index];
        }
        qsort(FrameScratch, GpuWaitCount, sizeof(FrameScratch[0]), DwmCompareTicks);
        _snprintf(Line, sizeof(Line), "DWM_GPU_WAIT phase=%s samples=%lu avg_us=%llu p50_us=%llu p95_us=%llu p99_us=%llu max_us=%llu total_us=%llu\n",
                  Names[Phase], GpuWaitCount, Sum * 1000000 / Frequency / GpuWaitCount,
                  FrameScratch[(GpuWaitCount - 1) / 2] * 1000000 / Frequency,
                  FrameScratch[(GpuWaitCount - 1) * 95 / 100] * 1000000 / Frequency,
                  FrameScratch[(GpuWaitCount - 1) * 99 / 100] * 1000000 / Frequency,
                  FrameScratch[GpuWaitCount - 1] * 1000000 / Frequency, Sum * 1000000 / Frequency);
        Line[sizeof(Line) - 1] = 0;
        OutputDebugStringA(Line);
    }
    for (Slot = 0; Slot < ARRAYSIZE(Worst) && Worst[Slot]; ++Slot)
    {
        DWM_GPU_WAIT_TIMING *Timing;
        Index = Worst[Slot] - 1;
        Timing = &GpuWaitSamples[Index].Timing;
        _snprintf(Line, sizeof(Line), "DWM_GPU_WAIT_SLOW time_us=%llu total_us=%llu end_us=%llu flush_us=%llu query_us=%llu removed_us=%llu sleep_us=%llu polls=%lu sleeps=%lu max_sleep_us=%llu\n",
                  (GpuWaitSamples[Index].End - Start) * 1000000 / Frequency,
                  Timing->Ticks[DWM_WAIT_TOTAL] * 1000000 / Frequency,
                  Timing->Ticks[DWM_WAIT_END] * 1000000 / Frequency,
                  Timing->Ticks[DWM_WAIT_FLUSH] * 1000000 / Frequency,
                  Timing->Ticks[DWM_WAIT_QUERY] * 1000000 / Frequency,
                  Timing->Ticks[DWM_WAIT_REMOVED] * 1000000 / Frequency,
                  Timing->Ticks[DWM_WAIT_SLEEP] * 1000000 / Frequency,
                  Timing->Polls, Timing->Sleeps, Timing->MaxSleep * 1000000 / Frequency);
        Line[sizeof(Line) - 1] = 0;
        OutputDebugStringA(Line);
    }
}

static void DwmDumpCadence(ULONG Session, ULONGLONG Frequency, ULONGLONG Start)
{
    ULONG Metric, First, Index, Count, Over17, Over25, Over34;
    char Line[384];
    static const char *Names[] = {"completion_interval", "work", "draw", "present"};
    if (FrameSession != Session || !Frequency || !FrameCount)
        return;
    for (First = 1; First <= 61; First += 60)
    {
        if (FrameCount <= First)
            continue;
        Count = FrameCount - First;
        for (Metric = 0; Metric < ARRAYSIZE(Names); ++Metric)
        {
            ULONGLONG Sum = 0;
            Over17 = Over25 = Over34 = 0;
            for (Index = First; Index < FrameCount; ++Index)
            {
                ULONGLONG Ticks = Metric == 0 ? FrameSamples[Index].Interval :
                                  Metric == 1 ? FrameSamples[Index].Work :
                                  Metric == 2 ? FrameSamples[Index].Draw : FrameSamples[Index].Present;
                FrameScratch[Index - First] = Ticks;
                Sum += Ticks;
                Over17 += Ticks * 60 > Frequency;
                Over25 += Ticks * 40 > Frequency;
                Over34 += Ticks * 30 > Frequency;
            }
            qsort(FrameScratch, Count, sizeof(FrameScratch[0]), DwmCompareTicks);
            _snprintf(Line, sizeof(Line), "DWM_CADENCE metric=%s skip=%lu samples=%lu lost=%lu avg_us=%llu p50_us=%llu p95_us=%llu p99_us=%llu max_us=%llu over_16_67=%lu over_25=%lu over_33_33=%lu\n",
                      Names[Metric], First, Count, FrameLost, Sum * 1000000 / Frequency / Count,
                      FrameScratch[(Count - 1) / 2] * 1000000 / Frequency,
                      FrameScratch[(Count - 1) * 95 / 100] * 1000000 / Frequency,
                      FrameScratch[(Count - 1) * 99 / 100] * 1000000 / Frequency,
                      FrameScratch[Count - 1] * 1000000 / Frequency, Over17, Over25, Over34);
            Line[sizeof(Line) - 1] = 0;
            OutputDebugStringA(Line);
        }
    }
    /* Associate the worst steady-state gaps with work and presentation time. */
    {
        ULONG Worst[6] = {0}, Slot;
        for (Index = 61; Index < FrameCount; ++Index)
            for (Slot = 0; Slot < ARRAYSIZE(Worst); ++Slot)
                if (!Worst[Slot] || FrameSamples[Index].Interval > FrameSamples[Worst[Slot]].Interval)
                {
                    ULONG Move;
                    for (Move = ARRAYSIZE(Worst) - 1; Move > Slot; --Move) Worst[Move] = Worst[Move - 1];
                    Worst[Slot] = Index;
                    break;
                }
        for (Slot = 0; Slot < ARRAYSIZE(Worst) && Worst[Slot]; ++Slot)
        {
            Index = Worst[Slot];
            _snprintf(Line, sizeof(Line), "DWM_CADENCE_GAP frame=%lu time_us=%llu interval_us=%llu work_us=%llu draw_us=%llu present_us=%llu\n",
                      Index, (FrameSamples[Index].End - Start) * 1000000 / Frequency,
                      FrameSamples[Index].Interval * 1000000 / Frequency,
                      FrameSamples[Index].Work * 1000000 / Frequency,
                      FrameSamples[Index].Draw * 1000000 / Frequency,
                      FrameSamples[Index].Present * 1000000 / Frequency);
            Line[sizeof(Line) - 1] = 0;
            OutputDebugStringA(Line);
        }
    }
}

void DwmPresentTraceSetIcd(PFNWGLCONTROLPRESENTATIONTRACEROS Control)
{
    AcquireSRWLockExclusive(&IcdLock);
    if (IcdControl != (PVOID)Control && IcdControl != NULL && IcdSession != 0)
    {
        DPT_REQUEST Request = {sizeof(Request), DPT_VERSION, DPT_STOP, IcdSession};
        DPT_DOMAIN Domain;
        ((PFNWGLCONTROLPRESENTATIONTRACEROS)IcdControl)(&Request, &Domain, sizeof(Domain));
        IcdSession = 0;
    }
    InterlockedExchangePointer(&IcdControl, (PVOID)Control);
    ReleaseSRWLockExclusive(&IcdLock);
}

static BOOL DwmTraceCpu(ULONGLONG *Kernel, ULONGLONG *User)
{
    FILETIME Created, Exited, K, U;
    if (!GetProcessTimes(GetCurrentProcess(), &Created, &Exited, &K, &U))
        return FALSE;
    *Kernel = ((ULONGLONG)K.dwHighDateTime << 32) | K.dwLowDateTime;
    *User = ((ULONGLONG)U.dwHighDateTime << 32) | U.dwLowDateTime;
    return TRUE;
}

static LONG DwmTraceKernel(const DPT_REQUEST *Request, DPT_DOMAIN *Domain)
{
    HDC Dc = GetDC(NULL);
    INT Result;
    if (!Dc)
        return E_FAIL;
    Result = ExtEscape(Dc, CDD_ESCAPE_PRESENT_CAPTURE, sizeof(*Request),
                       (LPCSTR)Request, sizeof(*Domain), (LPSTR)Domain);
    ReleaseDC(NULL, Dc);
    return Result > 0 ? S_OK : E_FAIL;
}

static LONG DwmTraceLocal(const DPT_REQUEST *Request)
{
    LONG Result;
    PFNWGLCONTROLPRESENTATIONTRACEROS Icd;
    ULONGLONG K, U;
    if (Request->Operation == DPT_QUERY)
        return (LastCompleted.Size && (!Request->Session || LastCompleted.Session == Request->Session)) ?
               S_OK : HRESULT_FROM_WIN32(ERROR_NOT_READY);
    if (Request->Operation == DPT_STOP && Capture.Session == Request->Session && Capture.Domain[0].Stop)
        return S_OK;

    Result = DptControl(&g_DwmPresentTrace, Request, &Capture.Domain[0],
                        sizeof(DPT_DOMAIN), GetCurrentProcessId());
    if (Result < 0)
        return Result;
    if (Request->Operation == DPT_START)
    {
        Capture.Size = sizeof(Capture);
        Capture.Version = DPT_VERSION;
        Capture.Session = Request->Session;
        Capture.Available = 1;
        Capture.CpuKernel100ns = Capture.CpuUser100ns = 0;
        ZeroMemory(&Capture.Domain[1], 2 * sizeof(DPT_DOMAIN));
        CpuStartValid = DwmTraceCpu(&CpuKernelStart, &CpuUserStart);
    }
    else if (CpuStartValid && DwmTraceCpu(&K, &U))
    {
        Capture.CpuKernel100ns = K - CpuKernelStart;
        Capture.CpuUser100ns = U - CpuUserStart;
    }
    Capture.Status[0] = S_OK;
    AcquireSRWLockExclusive(&IcdLock);
    Icd = (PFNWGLCONTROLPRESENTATIONTRACEROS)IcdControl;
    if (Request->Operation == DPT_START || (Capture.Available & 2))
    {
        Capture.Status[1] = E_NOTIMPL;
        if (Icd)
            Capture.Status[1] = Icd(Request, &Capture.Domain[1], sizeof(DPT_DOMAIN)) ?
                               S_OK : HRESULT_FROM_WIN32(GetLastError());
        if (Capture.Status[1] >= 0)
        {
            Capture.Available |= 2;
            IcdSession = Request->Operation == DPT_START ? Request->Session : 0;
        }
    }
    ReleaseSRWLockExclusive(&IcdLock);
    if (Request->Operation == DPT_START || (Capture.Available & 4))
    {
        Capture.Status[2] = DwmTraceKernel(Request, &Capture.Domain[2]);
        if (Capture.Status[2] >= 0)
            Capture.Available |= 4;
    }
    if (Request->Operation == DPT_STOP)
    {
        LastCompleted = Capture;
        DwmDumpCadence(Request->Session, Capture.Domain[0].Frequency, Capture.Domain[0].Start);
        DwmDumpGpuWait(Request->Session, Capture.Domain[0].Frequency, Capture.Domain[0].Start);
    }
    return S_OK;
}

/* Only the DWM host calls this export, on its notification thread. */
LRESULT WINAPI DwmPresentationTraceDispatch(HWND Window, HWND Reply,
                                            const COPYDATASTRUCT *Data)
{
    DPT_REQUEST Request;
    COPYDATASTRUCT Response;
    DWORD_PTR Result;
    LONG Status;
    BOOL Sent;
    BOOL Acquired;
    if (!Data || Data->dwData != DPT_COPYDATA ||
        Data->cbData != sizeof(Request) || !Data->lpData || !IsWindow(Reply))
        return FALSE;
    memcpy(&Request, Data->lpData, sizeof(Request));
    if (Request.Size != sizeof(Request) || Request.Version != DPT_VERSION ||
        (!Request.Session && Request.Operation != DPT_QUERY) || Request.Operation < DPT_START || Request.Operation > DPT_QUERY)
        return FALSE;
    Acquired = InterlockedCompareExchange(&DispatchBusy, 1, 0) == 0;
    Status = Acquired ? DwmTraceLocal(&Request) : HRESULT_FROM_WIN32(ERROR_BUSY);
    Response.dwData = DPT_REPLY;
    Response.cbData = Status < 0 ? sizeof(Status) : sizeof(Capture);
    Response.lpData = Status < 0 ? (PVOID)&Status :
        Request.Operation == DPT_QUERY ? (PVOID)&LastCompleted : (PVOID)&Capture;
    Sent = SendMessageTimeoutW(Reply, WM_COPYDATA, (WPARAM)Window,
        (LPARAM)&Response, SMTO_BLOCK, 2000, &Result) && Result;
    /* A failed START reply must not leave orphan instrumentation enabled. */
    if (!Sent && Status >= 0 && Request.Operation == DPT_START)
    {
        Request.Operation = DPT_STOP;
        DwmTraceLocal(&Request);
    }
    if (Acquired)
        InterlockedExchange(&DispatchBusy, 0);
    return Sent;
}
