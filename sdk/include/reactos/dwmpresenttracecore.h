/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * Shared counter implementation for DWM, ICD and dxgkrnl. Disabled: one acquire
 * load, no clock call. Active: two clocks and atomic counter updates per scope.
 * Control waits for short counter writers only, never for the measured work.
 * An epoch prevents completions from an old capture corrupting a new capture.
 * All banks have static lifetime. Call control only at PASSIVE_LEVEL.
 */
#ifndef ROS_DWM_PRESENT_TRACE_CORE_H
#define ROS_DWM_PRESENT_TRACE_CORE_H
#include "dwmpresenttrace.h"
#include <string.h>

typedef struct _DPT_BANK
{
    volatile LONG Epoch, Writers, Control;
    DPT_DOMAIN Data;
} DPT_BANK;

typedef struct _DPT_SCOPE
{
    LONG Epoch;
    ULONG Metric;
    ULONGLONG Start;
} DPT_SCOPE;

#if defined(__GNUC__) || defined(__clang__)
#define DPT_READ(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define DPT_READ64(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#else
#define DPT_READ(p) InterlockedCompareExchange((p), 0, 0)
#define DPT_READ64(p) InterlockedCompareExchange64((volatile LONG64 *)(p), 0, 0)
#endif

static __inline ULONGLONG DptNow(void)
{
    LARGE_INTEGER Time;
#ifdef DPT_KERNEL
    Time = KeQueryPerformanceCounter(NULL);
#else
    QueryPerformanceCounter(&Time);
#endif
    return Time.QuadPart;
}

static __inline DPT_SCOPE DptBegin(DPT_BANK *Bank, ULONG Metric)
{
    DPT_SCOPE Scope = {0, 0, 0};
    LONG Epoch = DPT_READ(&Bank->Epoch);
    if (!(Epoch & 1))
        return Scope;
    InterlockedIncrement(&Bank->Writers);
    if (DPT_READ(&Bank->Epoch) == Epoch)
    {
        Scope.Epoch = Epoch;
        Scope.Metric = Metric;
        Scope.Start = DptNow();
        InterlockedIncrement64((volatile LONG64 *)&Bank->Data.Counter[Metric].Entered);
    }
    InterlockedDecrement(&Bank->Writers);
    return Scope;
}

static __inline void DptEnd(DPT_BANK *Bank, DPT_SCOPE Scope,
                            BOOL Success, ULONGLONG Bytes)
{
    DPT_COUNTER *Counter;
    ULONGLONG Ticks;
    LONG64 Maximum, Minimum;
    if (!Scope.Epoch || DPT_READ(&Bank->Epoch) != Scope.Epoch)
        return;
    Ticks = DptNow() - Scope.Start;
    InterlockedIncrement(&Bank->Writers);
    if (DPT_READ(&Bank->Epoch) == Scope.Epoch)
    {
        Counter = &Bank->Data.Counter[Scope.Metric];
        InterlockedIncrement64((volatile LONG64 *)&Counter->Completed);
        if (!Success)
            InterlockedIncrement64((volatile LONG64 *)&Counter->Failed);
        InterlockedExchangeAdd64((volatile LONG64 *)&Counter->Ticks, Ticks);
        if (Bytes)
            InterlockedExchangeAdd64((volatile LONG64 *)&Counter->Bytes, Bytes);
        Minimum = DPT_READ64(&Counter->MinTicks);
        while ((ULONGLONG)Minimum > Ticks)
        {
            LONG64 Previous = InterlockedCompareExchange64(
                (volatile LONG64 *)&Counter->MinTicks, Ticks, Minimum);
            if (Previous == Minimum)
                break;
            Minimum = Previous;
        }
        Maximum = DPT_READ64(&Counter->MaxTicks);
        while ((ULONGLONG)Maximum < Ticks)
        {
            LONG64 Previous = InterlockedCompareExchange64(
                (volatile LONG64 *)&Counter->MaxTicks, Ticks, Maximum);
            if (Previous == Maximum)
                break;
            Maximum = Previous;
        }
    }
    InterlockedDecrement(&Bank->Writers);
}

static __inline void DptCount(DPT_BANK *Bank, ULONG Metric, ULONGLONG Bytes)
{
    LONG Epoch = DPT_READ(&Bank->Epoch);
    DPT_COUNTER *Counter;
    if (!(Epoch & 1))
        return;
    InterlockedIncrement(&Bank->Writers);
    if (DPT_READ(&Bank->Epoch) == Epoch)
    {
        Counter = &Bank->Data.Counter[Metric];
        InterlockedIncrement64((volatile LONG64 *)&Counter->Entered);
        InterlockedIncrement64((volatile LONG64 *)&Counter->Completed);
        if (Bytes)
            InterlockedExchangeAdd64((volatile LONG64 *)&Counter->Bytes, Bytes);
    }
    InterlockedDecrement(&Bank->Writers);
}

static __inline LONG DptControl(DPT_BANK *Bank, const DPT_REQUEST *Request,
                                DPT_DOMAIN *Output, ULONG Bytes, ULONG ProcessId)
{
    LONG Result = 0;
    LARGE_INTEGER Frequency;
    if (!Request || Request->Size != sizeof(*Request) ||
        Request->Version != DPT_VERSION || !Request->Session ||
        Request->Operation < DPT_START || Request->Operation > DPT_QUERY ||
        !Output || Bytes < sizeof(*Output))
        return (LONG)0x80070057; /* E_INVALIDARG */
    if (InterlockedCompareExchange(&Bank->Control, 1, 0))
        return (LONG)0x800700aa; /* ERROR_BUSY */
    if (Request->Operation == DPT_START)
    {
        if (DPT_READ(&Bank->Epoch) & 1)
            Result = (LONG)0x800700aa;
        else
        {
            ULONG Metric;
            /* Stopped epochs admit no counter writes. Do not reset Writers:
             * a thread may still be rejecting an old begin/end token. */
            memset(&Bank->Data, 0, sizeof(Bank->Data));
            for (Metric = 0; Metric < DPT_METRIC_COUNT; ++Metric)
                Bank->Data.Counter[Metric].MinTicks = ~(ULONGLONG)0;
#ifdef DPT_KERNEL
            KeQueryPerformanceCounter(&Frequency);
#else
            QueryPerformanceFrequency(&Frequency);
#endif
            Bank->Data.Size = sizeof(Bank->Data);
            Bank->Data.Version = DPT_VERSION;
            Bank->Data.Session = Request->Session;
            Bank->Data.ProcessId = ProcessId;
            Bank->Data.Frequency = Frequency.QuadPart;
            Bank->Data.Start = DptNow();
            *Output = Bank->Data; /* Copy before enabling writers. */
            InterlockedIncrement(&Bank->Epoch);
        }
    }
    else if (Bank->Data.Session != Request->Session)
        Result = (LONG)0x80070057;
    else if (Request->Operation == DPT_QUERY && (DPT_READ(&Bank->Epoch) & 1))
        Result = (LONG)0x800700aa;
    else
    {
        if (DPT_READ(&Bank->Epoch) & 1)
        {
            InterlockedIncrement(&Bank->Epoch);
            while (DPT_READ(&Bank->Writers))
            {
#ifdef DPT_KERNEL
                LARGE_INTEGER Delay;
                Delay.QuadPart = -10000;
                KeDelayExecutionThread(KernelMode, FALSE, &Delay);
#else
                Sleep(1);
#endif
            }
            Bank->Data.Stop = DptNow();
        }
        *Output = Bank->Data;
    }
    InterlockedExchange(&Bank->Control, 0);
    return Result;
}
#endif
