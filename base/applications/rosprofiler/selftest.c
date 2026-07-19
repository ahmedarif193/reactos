/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Deterministic log, parser, and aggregation self-tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rosprofiler.h"
#include "profiler_recorder.h"
#include "profiler_recorder_internal.h"
#include "profiler_codec.h"
#include "profiler_controller.h"
#include "profiler_analysis.h"
#include "profiler_legacy_bridge.h"
#include "profiler_pe.h"
#include "profiler_symbolizer_dbghelp.h"

#include <reactos/rperf.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define RPERF_LIVE_CAPTURE_INTERVAL_MS 10
#define RPERF_LIVE_CAPTURE_DURATION_MS 750
#define RPERF_LIVE_CAPTURE_TIMEOUT_MS 5000
#define RPERF_WORKLOAD_TIMEOUT_MS 10000
#define RPERF_DESCENDING_SYMBOL_COUNT 16384
#define RPERF_STRESS_TIMEOUT_MS 10000

static volatile ULONG_PTR RperfWorkloadState = 0x4f1bbcdc;

static const CHAR RperfCompleteLog[] =
    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t1000\twall-clock-all-threads\n"
    "y\t1010\t1000\t1000\t10\tdemo\tHot\n"
    "y\t1018\t1000\t1000\t18\tdemo\tHot\n"
    "y\t2000\t2000\t2000\t0\tdemo\tParent\n"
    "y\t3000\t3000\t3000\t0\tdemo\tOther\n"
    "s\t100\t4242\t11\t0\t2\t1010\t2000\n"
    "s\t200\t4242\t11\t0\t2\t1018\t2000\n"
    "s\t300\t4242\t22\t0\t2\t3000\t2000\n"
    "e\t400\t500\t600\t1\t2\t3\t4\t1\t0\n";

static const CHAR RperfIncompleteLog[] =
    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t5\t0\twall-clock-all-threads\n"
    "y\t4010\t4000\t4000\t10\tdemo\tIncomplete\n"
    "s\t125\t4242\t33\t0\t1\t4010\n";

static const CHAR *RperfMalformedLogs[] =
{
    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "s\t12x\t4242\t11\t0\t1\t1010\n",

    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "s\t10\t99\t11\t0\t1\t1010\n",

    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "s\t10\t4242\t11\t0\t2\t1010\n",

    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "s\t10\t4242\t11\t0\t1\t1010\n"
    "e\t10\t0\t0\t0\t0\t0\t0\t1\t0\n"
    "unexpected\n",

    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "s\t20\t4242\t11\t0\t1\t1010\n"
    "s\t10\t4242\t11\t0\t1\t1010\n",

    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "y\t1000\t1010\t1000\t0\tdemo\tbad-function\n"
    "s\t10\t4242\t11\t0\t1\t1000\n",

    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "y\t1000\t1000\t1010\t0\tdemo\tbad-module\n"
    "s\t10\t4242\t11\t0\t1\t1000\n",

    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "s\t10\t4242\t11\t8\t1\t1010\n",

    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "s\t10\t4242\t11\t1540\t1\t1010\n",

    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t0\twall-clock-all-threads\n"
    "s\t10\t4242\t11\t16\t1\t1010\n"
};

static const CHAR RperfStateTaggedLog[] =
    "RPERF\t1\n"
    "p\t4242\tselftest.exe\n"
    "c\t10\t1000\twall-clock-all-threads\n"
    "y\t1010\t1000\t1000\t10\tdemo\tHot\n"
    "y\t2000\t2000\t2000\t0\tdemo\tParent\n"
    "y\t3000\t3000\t3000\t0\tdemo\tOther\n"
    "s\t100\t4242\t11\t4\t2\t1010\t2000\n"
    "s\t200\t4242\t11\t1548\t2\t1010\t2000\n"
    "s\t300\t4242\t22\t0\t2\t3000\t2000\n"
    "e\t400\t500\t600\t0\t0\t0\t0\t1\t0\n";

static DECLSPEC_NOINLINE ULONG_PTR
RperfBusyRecursiveWorkload(ULONG Depth,
                           ULONG_PTR Value)
{
    ULONG Index;

    for (Index = 0; Index < 20000; ++Index)
        Value = (Value * 1664525) + 1013904223 + Index;

    if (Depth != 0)
        Value ^= RperfBusyRecursiveWorkload(Depth - 1, Value + Depth);

    for (Index = 0; Index < 2000; ++Index)
        Value = (Value << 7) ^ (Value >> 3) ^ Index;

    RperfWorkloadState ^= Value;
    return Value + Depth + RperfWorkloadState;
}

static BOOL
RperfParseInheritedHandle(PCWSTR Text,
                          HANDLE *Handle)
{
    WCHAR *End;
    ULONGLONG Value;

    if (Text == NULL || *Text == UNICODE_NULL)
        return FALSE;

    errno = 0;
    Value = _wcstoui64(Text, &End, 16);
    if (errno == ERANGE || End == Text || *End != UNICODE_NULL || Value == 0 ||
        Value > (ULONGLONG)(ULONG_PTR)-1)
    {
        return FALSE;
    }

    *Handle = (HANDLE)(ULONG_PTR)Value;
    return TRUE;
}

static INT
RperfExecuteWorkload(HANDLE ReadyEvent,
                     HANDLE StopEvent,
                     HANDLE DoneEvent)
{
    DWORD StartTick;
    ULONG_PTR Value = RperfWorkloadState;
    INT Result = 0;

    if (!SetEvent(ReadyEvent))
        return 3;

    StartTick = GetTickCount();
    while (WaitForSingleObject(StopEvent, 0) == WAIT_TIMEOUT)
    {
        Value = RperfBusyRecursiveWorkload(8, Value);
        if (GetTickCount() - StartTick >= RPERF_WORKLOAD_TIMEOUT_MS)
        {
            Result = 4;
            break;
        }
    }

    if (DoneEvent != NULL)
        SetEvent(DoneEvent);
    UNREFERENCED_PARAMETER(Value);
    return Result;
}

static INT
RperfRunWorkload(PCWSTR ReadyHandleText,
                 PCWSTR StopHandleText)
{
    HANDLE ReadyEvent;
    HANDLE StopEvent;

    if (!RperfParseInheritedHandle(ReadyHandleText, &ReadyEvent) ||
        !RperfParseInheritedHandle(StopHandleText, &StopEvent))
    {
        return 2;
    }
    return RperfExecuteWorkload(ReadyEvent, StopEvent, NULL);
}

static INT
RperfRunNamedWorkload(PCWSTR ReadyName,
                      PCWSTR StopName,
                      PCWSTR DoneName)
{
    HANDLE ReadyEvent;
    HANDLE StopEvent;
    HANDLE DoneEvent;
    INT Result;

    ReadyEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, ReadyName);
    StopEvent = OpenEventW(SYNCHRONIZE, FALSE, StopName);
    DoneEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, DoneName);
    if (ReadyEvent == NULL || StopEvent == NULL || DoneEvent == NULL)
    {
        if (ReadyEvent != NULL)
            CloseHandle(ReadyEvent);
        if (StopEvent != NULL)
            CloseHandle(StopEvent);
        if (DoneEvent != NULL)
            CloseHandle(DoneEvent);
        return 5;
    }

    Result = RperfExecuteWorkload(ReadyEvent, StopEvent, DoneEvent);
    CloseHandle(DoneEvent);
    CloseHandle(StopEvent);
    CloseHandle(ReadyEvent);
    return Result;
}

static BOOL
RperfWriteFixture(PCWSTR Path,
                  PCSTR Contents)
{
    FILE *File;
    SIZE_T Length;
    BOOL Result;

    File = _wfopen(Path, L"wb");
    if (File == NULL)
        return FALSE;

    Length = strlen(Contents);
    Result = (fwrite(Contents, 1, Length, File) == Length);
    if (fclose(File) != 0)
        Result = FALSE;
    return Result;
}

static BOOL
RperfCreateTemporaryLogPath(PCWSTR TemporaryDirectory,
                            PWSTR Path,
                            SIZE_T PathCount)
{
    WCHAR TemporaryPath[MAX_PATH];
    PWSTR Extension;

    if (PathCount < MAX_PATH ||
        GetTempFileNameW(TemporaryDirectory,
                         L"rpf",
                         0,
                         TemporaryPath) == 0)
    {
        return FALSE;
    }

    lstrcpynW(Path, TemporaryPath, (INT)PathCount);
    Extension = wcsrchr(Path, L'.');
    if (Extension == NULL ||
        (SIZE_T)(Extension - Path) + ARRAYSIZE(L".rperf") > PathCount)
    {
        DeleteFileW(TemporaryPath);
        Path[0] = UNICODE_NULL;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    lstrcpyW(Extension, L".rperf");

    if (!MoveFileW(TemporaryPath, Path))
    {
        DWORD Error = GetLastError();
        DeleteFileW(TemporaryPath);
        Path[0] = UNICODE_NULL;
        SetLastError(Error);
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfCheck(BOOL Condition,
           PCSTR TestName,
           PCSTR Expression,
           ULONG Line)
{
    if (Condition)
        return TRUE;

    printf("[FAIL] %s:%lu: %s\n", TestName, Line, Expression);
    return FALSE;
}

#define RPERF_CHECK(TestName, Expression) \
    do \
    { \
        if (!RperfCheck((Expression), (TestName), #Expression, __LINE__)) \
            goto Cleanup; \
    } while (0)

static BOOL
RperfTestRecordingCloneLimits(VOID)
{
    static const CHAR TestName[] = "recording clone limits";
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_RECORD Record;
    RPERF_RECORDING *Source = NULL;
    RPERF_RECORDING *Clone = NULL;
    BOOL Result = FALSE;

    RperfDefaultCaptureLimits(&Limits);
    Limits.MaxSamples = 2;
    Source = RperfRecordingCreate(&Limits);
    RPERF_CHECK(TestName, Source != NULL);

    ZeroMemory(&Record, sizeof(Record));
    Record.Header.Kind = RperfRecordSample;
    Record.Data.Sample.Depth = 1;
    Record.Data.Sample.Frames[0].Address = 0x1000;
    RPERF_CHECK(TestName, RperfRecordingAddRecord(Source, &Record));
    Record.Header.Sequence = 2;
    RPERF_CHECK(TestName, RperfRecordingAddRecord(Source, &Record));
    Source->Counters.AttemptedSamples = 3;
    Source->Counters.FailedSamples = 1;

    Clone = RperfRecordingCloneMutable(Source);
    RPERF_CHECK(TestName, Clone != NULL);
    RPERF_CHECK(TestName, Clone->RecordCount == Source->RecordCount);
    RPERF_CHECK(TestName, Clone->Counters.SuccessfulSamples == 2);
    RPERF_CHECK(TestName, Clone->Counters.AttemptedSamples == 3);
    RPERF_CHECK(TestName, Clone->Counters.FailedSamples == 1);
    Result = TRUE;

Cleanup:
    if (Clone != NULL)
        RperfRecordingRelease(Clone);
    if (Source != NULL)
        RperfRecordingRelease(Source);
    return Result;
}

static BOOL
RperfTestCompleteLog(PCWSTR Path)
{
    static const CHAR TestName[] = "complete log and analysis";
    RPERF_SESSION Session;
    const RPERF_SYMBOL *Hot;
    const RPERF_SYMBOL *Parent;
    const RPERF_SYMBOL *Other;
    BOOL Result = FALSE;

    RperfSessionInitialize(&Session);
    RPERF_CHECK(TestName, RperfWriteFixture(Path, RperfCompleteLog));
    RPERF_CHECK(TestName, RperfLoadLog(&Session, Path));
    RPERF_CHECK(TestName, Session.LogComplete);
    RPERF_CHECK(TestName, Session.Backend == RperfBackendIntrusive);
    RPERF_CHECK(TestName, Session.CompletionReason == RperfCompletionDuration);
    RPERF_CHECK(TestName, Session.CaptureError == ERROR_SUCCESS);
    RPERF_CHECK(TestName, Session.ProcessId == 4242);
    RPERF_CHECK(TestName, Session.IntervalMs == 10);
    RPERF_CHECK(TestName, Session.RequestedDurationMs == 1000);
    RPERF_CHECK(TestName, Session.ElapsedUs == 400);
    RPERF_CHECK(TestName, Session.SampleCount == 3);
    RPERF_CHECK(TestName, Session.Counters.AttemptedSamples == 4);
    RPERF_CHECK(TestName, Session.Counters.SuccessfulSamples == 3);
    RPERF_CHECK(TestName, Session.Counters.FailedSamples == 1);
    RPERF_CHECK(TestName, Session.Counters.SkippedSamples == 0);
    RPERF_CHECK(TestName, Session.Counters.TruncatedSamples == 3);
    RPERF_CHECK(TestName, Session.Counters.MissedCadenceTicks == 2);
    RPERF_CHECK(TestName, Session.Counters.LostRecords == 4);
    RPERF_CHECK(TestName, Session.FilteredSampleCount == 3);
    RPERF_CHECK(TestName, Session.SymbolCount == 4);
    RPERF_CHECK(TestName, Session.NodeCount == 4);
    RPERF_CHECK(TestName, Session.Nodes[0].Count == 3);

    Hot = RperfFindSymbol(&Session, 0x1000);
    Parent = RperfFindSymbol(&Session, 0x2000);
    Other = RperfFindSymbol(&Session, 0x3000);
    RPERF_CHECK(TestName, Hot != NULL);
    RPERF_CHECK(TestName, Hot->Address == 0x1010);
    RPERF_CHECK(TestName, Hot->FunctionAddress == 0x1000);
    RPERF_CHECK(TestName, Hot->Inclusive == 2);
    RPERF_CHECK(TestName, Hot->Exclusive == 2);
    RPERF_CHECK(TestName, Parent != NULL && Parent->Inclusive == 3);
    RPERF_CHECK(TestName, Parent->Exclusive == 0);
    RPERF_CHECK(TestName, Other != NULL && Other->Inclusive == 1);
    RPERF_CHECK(TestName, Other->Exclusive == 1);

    RPERF_CHECK(TestName,
                RperfBuildFilteredAnalysis(&Session, 11, 150, 250, 0));
    RPERF_CHECK(TestName, Session.FilteredSampleCount == 1);
    Hot = RperfFindSymbol(&Session, 0x1000);
    Parent = RperfFindSymbol(&Session, 0x2000);
    Other = RperfFindSymbol(&Session, 0x3000);
    RPERF_CHECK(TestName, Hot != NULL && Hot->Inclusive == 1);
    RPERF_CHECK(TestName, Hot->Exclusive == 1);
    RPERF_CHECK(TestName, Parent != NULL && Parent->Inclusive == 1);
    RPERF_CHECK(TestName, Other != NULL && Other->Inclusive == 0);
    RPERF_CHECK(TestName, Session.Nodes[0].Count == 1);

    RPERF_CHECK(TestName, RperfBuildAnalysis(&Session));
    RPERF_CHECK(TestName, Session.FilteredSampleCount == 3);
    Hot = RperfFindSymbol(&Session, 0x1000);
    RPERF_CHECK(TestName, Hot != NULL && Hot->Inclusive == 2);
    Result = TRUE;

Cleanup:
    RperfSessionClear(&Session);
    if (Result)
        printf("[PASS] %s\n", TestName);
    return Result;
}

static BOOL
RperfTestIncompleteLog(PCWSTR Path)
{
    static const CHAR TestName[] = "incomplete log salvage";
    RPERF_SESSION Session;
    const RPERF_SYMBOL *Symbol;
    BOOL Result = FALSE;

    RperfSessionInitialize(&Session);
    RPERF_CHECK(TestName, RperfWriteFixture(Path, RperfIncompleteLog));
    RPERF_CHECK(TestName, RperfLoadLog(&Session, Path));
    RPERF_CHECK(TestName, !Session.LogComplete);
    RPERF_CHECK(TestName,
                Session.CompletionReason == RperfCompletionIncomplete);
    RPERF_CHECK(TestName, Session.CaptureError == ERROR_HANDLE_EOF);
    RPERF_CHECK(TestName, Session.ElapsedUs == 125);
    RPERF_CHECK(TestName, Session.SampleCount == 1);
    RPERF_CHECK(TestName, Session.Counters.AttemptedSamples == 1);
    RPERF_CHECK(TestName, Session.Counters.SuccessfulSamples == 1);
    RPERF_CHECK(TestName, Session.Counters.FailedSamples == 0);
    RPERF_CHECK(TestName, Session.FilteredSampleCount == 1);
    RPERF_CHECK(TestName, Session.Nodes != NULL);
    RPERF_CHECK(TestName, Session.Nodes[0].Count == 1);
    Symbol = RperfFindSymbol(&Session, 0x4000);
    RPERF_CHECK(TestName, Symbol != NULL);
    RPERF_CHECK(TestName, Symbol->Inclusive == 1);
    RPERF_CHECK(TestName, Symbol->Exclusive == 1);
    Result = TRUE;

Cleanup:
    RperfSessionClear(&Session);
    if (Result)
        printf("[PASS] %s\n", TestName);
    return Result;
}

static BOOL
RperfTestMalformedLogs(PCWSTR Path)
{
    static const CHAR TestName[] = "malformed log rejection";
    RPERF_SESSION Session;
    SIZE_T Index;
    BOOL Loaded;
    BOOL Result = FALSE;

    RperfSessionInitialize(&Session);
    for (Index = 0; Index < ARRAYSIZE(RperfMalformedLogs); ++Index)
    {
        RperfSessionClear(&Session);
        Session.ProcessId = 777;
        RPERF_CHECK(TestName,
                    RperfWriteFixture(Path, RperfMalformedLogs[Index]));
        SetLastError(ERROR_SUCCESS);
        Loaded = RperfLoadLog(&Session, Path);
        RPERF_CHECK(TestName, !Loaded);
        RPERF_CHECK(TestName, GetLastError() == ERROR_BAD_FORMAT);
        RPERF_CHECK(TestName, Session.ProcessId == 777);
        RPERF_CHECK(TestName, Session.SampleCount == 0);
        RPERF_CHECK(TestName, Session.SymbolCount == 0);
    }
    Result = TRUE;

Cleanup:
    RperfSessionClear(&Session);
    if (Result)
        printf("[PASS] %s (%Iu cases)\n",
               TestName,
               ARRAYSIZE(RperfMalformedLogs));
    return Result;
}

static BOOL
RperfTestSchedulerStateFilter(PCWSTR Path)
{
    static const CHAR TestName[] = "scheduler-state CPU-only filter";
    RPERF_SESSION Session;
    const RPERF_SYMBOL *Hot;
    const RPERF_SYMBOL *Parent;
    const RPERF_SYMBOL *Other;
    BOOL Result = FALSE;

    RperfSessionInitialize(&Session);
    RPERF_CHECK(TestName, RperfWriteFixture(Path, RperfStateTaggedLog));
    RPERF_CHECK(TestName, RperfLoadLog(&Session, Path));
    RPERF_CHECK(TestName, Session.LogComplete);
    RPERF_CHECK(TestName, Session.SampleCount == 3);
    RPERF_CHECK(TestName, Session.StateTaggedSamples == 2);
    RPERF_CHECK(TestName, Session.WaitingSamples == 1);
    RPERF_CHECK(TestName, Session.FilterFlags == 0);
    RPERF_CHECK(TestName, Session.FilteredSampleCount == 3);
    RPERF_CHECK(TestName, (Session.Samples[1].Flags & RPERF_SAMPLE_WAITING) != 0);
    RPERF_CHECK(TestName, ((Session.Samples[1].Flags & RPERF_SAMPLE_WAIT_REASON_MASK) >> RPERF_SAMPLE_WAIT_REASON_SHIFT) == 6);

    RPERF_CHECK(TestName, RperfBuildFilteredAnalysis(&Session, 0, 0, (ULONGLONG)-1, RPERF_FILTER_CPU_ONLY));
    RPERF_CHECK(TestName, Session.FilterFlags == RPERF_FILTER_CPU_ONLY);
    RPERF_CHECK(TestName, Session.FilteredSampleCount == 2);
    Hot = RperfFindSymbol(&Session, 0x1000);
    Parent = RperfFindSymbol(&Session, 0x2000);
    Other = RperfFindSymbol(&Session, 0x3000);
    RPERF_CHECK(TestName, Hot != NULL && Hot->Inclusive == 1);
    RPERF_CHECK(TestName, Hot->Exclusive == 1);
    RPERF_CHECK(TestName, Parent != NULL && Parent->Inclusive == 2);
    RPERF_CHECK(TestName, Other != NULL && Other->Inclusive == 1);
    RPERF_CHECK(TestName, Session.Nodes[0].Count == 2);

    RPERF_CHECK(TestName, RperfBuildAnalysis(&Session));
    RPERF_CHECK(TestName, Session.FilterFlags == 0);
    RPERF_CHECK(TestName, Session.FilteredSampleCount == 3);
    Hot = RperfFindSymbol(&Session, 0x1000);
    RPERF_CHECK(TestName, Hot != NULL && Hot->Inclusive == 2);

    SetLastError(ERROR_SUCCESS);
    RPERF_CHECK(TestName, !RperfBuildFilteredAnalysis(&Session, 0, 0, (ULONGLONG)-1, 0x2));
    RPERF_CHECK(TestName, GetLastError() == ERROR_INVALID_PARAMETER);
    Result = TRUE;

Cleanup:
    RperfSessionClear(&Session);
    if (Result)
        printf("[PASS] %s\n", TestName);
    return Result;
}

static BOOL
RperfTestDescendingSymbols(PCWSTR Path)
{
    static const CHAR TestName[] = "descending-symbol index stress";
    RPERF_SESSION Session;
    FILE *File = NULL;
    DWORD64 Address;
    DWORD Started, Elapsed;
    SIZE_T Index;
    BOOL Result = FALSE;

    RperfSessionInitialize(&Session);
    File = _wfopen(Path, L"wt");
    RPERF_CHECK(TestName, File != NULL);
    RPERF_CHECK(TestName,
                fprintf(File,
                        "RPERF\t1\n"
                        "p\t4242\tselftest.exe\n"
                        "c\t10\t0\twall-clock-all-threads\n") >= 0);
    for (Index = RPERF_DESCENDING_SYMBOL_COUNT; Index != 0; --Index)
    {
        Address = 0x100000 + (DWORD64)Index * 0x10;
        RPERF_CHECK(TestName,
                    fprintf(File,
                            "y\t%I64x\t%I64x\t%I64x\t0\tstress\tpc%Iu\n",
                            Address,
                            Address,
                            Address,
                            Index) >= 0);
    }
    for (Index = 1; Index <= RPERF_DESCENDING_SYMBOL_COUNT; ++Index)
    {
        Address = 0x100000 + (DWORD64)Index * 0x10;
        RPERF_CHECK(TestName,
                    fprintf(File,
                            "s\t%Iu\t4242\t11\t0\t1\t%I64x\n",
                            Index,
                            Address) >= 0);
    }
    RPERF_CHECK(TestName,
                fprintf(File,
                        "e\t%u\t0\t0\t0\t0\t0\t0\t1\t0\n",
                        RPERF_DESCENDING_SYMBOL_COUNT) >= 0);
    {
        BOOL Closed = (fclose(File) == 0);
        File = NULL;
        RPERF_CHECK(TestName, Closed);
    }

    Started = GetTickCount();
    RPERF_CHECK(TestName, RperfLoadLog(&Session, Path));
    Elapsed = GetTickCount() - Started;
    RPERF_CHECK(TestName, Elapsed < RPERF_STRESS_TIMEOUT_MS);
    RPERF_CHECK(TestName,
                Session.SymbolCount == RPERF_DESCENDING_SYMBOL_COUNT);
    RPERF_CHECK(TestName, Session.SymbolsSorted);
    RPERF_CHECK(TestName,
                Session.FilteredSampleCount == RPERF_DESCENDING_SYMBOL_COUNT);
    RPERF_CHECK(TestName,
                Session.NodeCount == RPERF_DESCENDING_SYMBOL_COUNT + 1);
    RPERF_CHECK(TestName, RperfFindSymbol(&Session, 0x100010) != NULL);
    Address = 0x100000 +
              (DWORD64)RPERF_DESCENDING_SYMBOL_COUNT * 0x10;
    RPERF_CHECK(TestName, RperfFindSymbol(&Session, Address) != NULL);
    Result = TRUE;

Cleanup:
    if (File != NULL)
        fclose(File);
    RperfSessionClear(&Session);
    if (Result)
    {
        printf("[PASS] %s (%lu ms, %u symbols)\n",
               TestName,
               Elapsed,
               RPERF_DESCENDING_SYMBOL_COUNT);
    }
    return Result;
}

static BOOL
RperfTestLiveCapture(PCWSTR Path)
{
    static const CHAR TestName[] = "live capture integration";
    SECURITY_ATTRIBUTES SecurityAttributes;
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    RPERF_SESSION CaptureSession;
    RPERF_SESSION LoadedSession;
    WCHAR ExecutablePath[MAX_PATH];
    WCHAR CommandLine[MAX_PATH * 2];
    HANDLE ReadyEvent = NULL;
    HANDLE StopEvent = NULL;
    HANDLE ReadyHandles[2];
    DWORD WaitResult;
    DWORD ExitCode = (DWORD)-1;
    DWORD PathLength;
    SIZE_T Index;
    SIZE_T CapturedSamplesForReport = 0;
    ULONGLONG ExclusiveTotal = 0;
    USHORT MaximumDepth = 0;
    BOOL ProcessStarted = FALSE;
    BOOL CaptureStarted = FALSE;
    BOOL Result = FALSE;

    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));
    RperfSessionInitialize(&CaptureSession);
    RperfSessionInitialize(&LoadedSession);
    ZeroMemory(&SecurityAttributes, sizeof(SecurityAttributes));
    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.bInheritHandle = TRUE;

    ReadyEvent = CreateEventW(&SecurityAttributes, TRUE, FALSE, NULL);
    StopEvent = CreateEventW(&SecurityAttributes, TRUE, FALSE, NULL);
    RPERF_CHECK(TestName, ReadyEvent != NULL && StopEvent != NULL);

    PathLength = GetModuleFileNameW(NULL,
                                    ExecutablePath,
                                    ARRAYSIZE(ExecutablePath));
    RPERF_CHECK(TestName,
                PathLength != 0 && PathLength < ARRAYSIZE(ExecutablePath));
    _snwprintf(CommandLine,
               ARRAYSIZE(CommandLine),
               L"\"%s\" --workload %I64x %I64x",
               ExecutablePath,
               (ULONGLONG)(ULONG_PTR)ReadyEvent,
               (ULONGLONG)(ULONG_PTR)StopEvent);
    CommandLine[ARRAYSIZE(CommandLine) - 1] = UNICODE_NULL;

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    if (!CreateProcessW(ExecutablePath,
                        CommandLine,
                        NULL,
                        NULL,
                        TRUE,
                        0,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &ProcessInfo))
    {
        printf("[INFO] %s: CreateProcessW failed with error %lu\n",
               TestName,
               GetLastError());
        goto Cleanup;
    }
    ProcessStarted = TRUE;
    CloseHandle(ProcessInfo.hThread);
    ProcessInfo.hThread = NULL;

    ReadyHandles[0] = ReadyEvent;
    ReadyHandles[1] = ProcessInfo.hProcess;
    WaitResult = WaitForMultipleObjects(ARRAYSIZE(ReadyHandles),
                                        ReadyHandles,
                                        FALSE,
                                        RPERF_LIVE_CAPTURE_TIMEOUT_MS);
    if (WaitResult != WAIT_OBJECT_0)
    {
        if (WaitResult == WAIT_OBJECT_0 + 1 &&
            GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode))
        {
            printf("[INFO] %s: workload exited before ready with code %lu\n",
                   TestName,
                   ExitCode);
        }
        else
        {
            printf("[INFO] %s: workload readiness wait returned 0x%lx\n",
                   TestName,
                   WaitResult);
        }
        goto Cleanup;
    }

    if (!RperfCaptureStart(&CaptureSession,
                           ProcessInfo.dwProcessId,
                           L"rosprofiler_selftest.exe",
                           RPERF_LIVE_CAPTURE_INTERVAL_MS,
                           RPERF_LIVE_CAPTURE_DURATION_MS,
                           Path,
                           NULL))
    {
        printf("[INFO] %s: RperfCaptureStart failed with error %lu\n",
               TestName,
               GetLastError());
        goto Cleanup;
    }
    CaptureStarted = TRUE;

    WaitResult = WaitForSingleObject(CaptureSession.WorkerThread,
                                     RPERF_LIVE_CAPTURE_TIMEOUT_MS);
    if (WaitResult != WAIT_OBJECT_0)
    {
        printf("[INFO] %s: capture wait returned 0x%lx; stopping worker\n",
               TestName,
               WaitResult);
        RperfCaptureStop(&CaptureSession);
        goto Cleanup;
    }
    RperfCaptureWait(&CaptureSession);
    CaptureStarted = FALSE;

    if (CaptureSession.CaptureError != ERROR_SUCCESS ||
        CaptureSession.SampleCount == 0)
    {
        printf("[INFO] %s: capture error=%lu samples=%Iu missed_threads=%I64u "
               "missed_ticks=%I64u reason=%lu complete=%u\n",
               TestName,
               CaptureSession.CaptureError,
               CaptureSession.SampleCount,
               CaptureSession.MissedThreads,
               CaptureSession.MissedTicks,
               (DWORD)CaptureSession.CompletionReason,
               CaptureSession.LogComplete);
    }

    RPERF_CHECK(TestName, CaptureSession.CaptureError == ERROR_SUCCESS);
    RPERF_CHECK(TestName, CaptureSession.LogComplete);
    RPERF_CHECK(TestName,
                CaptureSession.CompletionReason == RperfCompletionDuration);
    RPERF_CHECK(TestName,
                CaptureSession.ProcessId == ProcessInfo.dwProcessId);
    RPERF_CHECK(TestName, CaptureSession.SampleCount != 0);
    RPERF_CHECK(TestName,
                CaptureSession.Counters.SuccessfulSamples ==
                    CaptureSession.SampleCount);
    RPERF_CHECK(TestName,
                CaptureSession.Counters.AttemptedSamples ==
                    CaptureSession.Counters.SuccessfulSamples +
                    CaptureSession.Counters.FailedSamples);
    RPERF_CHECK(TestName, CaptureSession.ElapsedUs != 0);
    RPERF_CHECK(TestName,
                CaptureSession.FilteredSampleCount ==
                CaptureSession.SampleCount);
    RPERF_CHECK(TestName, CaptureSession.SymbolCount != 0);
    RPERF_CHECK(TestName, CaptureSession.NodeCount > 1);
    RPERF_CHECK(TestName,
                CaptureSession.Nodes[0].Count == CaptureSession.SampleCount);

    for (Index = 0; Index < CaptureSession.SampleCount; ++Index)
    {
        RPERF_CHECK(TestName,
                    CaptureSession.Samples[Index].ProcessId ==
                    ProcessInfo.dwProcessId);
        RPERF_CHECK(TestName, CaptureSession.Samples[Index].Depth != 0);
        RPERF_CHECK(TestName,
                    CaptureSession.Samples[Index].Depth <= RPERF_MAX_FRAMES);
        MaximumDepth = max(MaximumDepth, CaptureSession.Samples[Index].Depth);
    }
#if defined(_M_ARM64)
    RPERF_CHECK(TestName, MaximumDepth >= 4);
#endif
    for (Index = 0; Index < CaptureSession.SymbolCount; ++Index)
    {
        RPERF_CHECK(TestName,
                    CaptureSession.Symbols[Index].Inclusive >=
                    CaptureSession.Symbols[Index].Exclusive);
        ExclusiveTotal += CaptureSession.Symbols[Index].Exclusive;
    }
    RPERF_CHECK(TestName, ExclusiveTotal == CaptureSession.SampleCount);
    for (Index = 1; Index < CaptureSession.NodeCount; ++Index)
    {
        ULONG Parent = CaptureSession.Nodes[Index].Parent;
        RPERF_CHECK(TestName, Parent < CaptureSession.NodeCount);
        RPERF_CHECK(TestName,
                    CaptureSession.Nodes[Index].Count <=
                    CaptureSession.Nodes[Parent].Count);
    }

    SetEvent(StopEvent);
    WaitResult = WaitForSingleObject(ProcessInfo.hProcess,
                                     RPERF_LIVE_CAPTURE_TIMEOUT_MS);
    RPERF_CHECK(TestName, WaitResult == WAIT_OBJECT_0);
    RPERF_CHECK(TestName,
                GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode));
    RPERF_CHECK(TestName, ExitCode == 0);

    RPERF_CHECK(TestName, RperfLoadLog(&LoadedSession, Path));
    RPERF_CHECK(TestName, LoadedSession.LogComplete);
    RPERF_CHECK(TestName,
                LoadedSession.CompletionReason == RperfCompletionDuration);
    RPERF_CHECK(TestName, LoadedSession.CaptureError == ERROR_SUCCESS);
    RPERF_CHECK(TestName,
                LoadedSession.ProcessId == ProcessInfo.dwProcessId);
    RPERF_CHECK(TestName,
                LoadedSession.SampleCount == CaptureSession.SampleCount);
    RPERF_CHECK(TestName,
                LoadedSession.Counters.AttemptedSamples ==
                    CaptureSession.Counters.AttemptedSamples);
    RPERF_CHECK(TestName,
                LoadedSession.Counters.SuccessfulSamples ==
                    CaptureSession.Counters.SuccessfulSamples);
    RPERF_CHECK(TestName,
                LoadedSession.Counters.FailedSamples ==
                    CaptureSession.Counters.FailedSamples);
    RPERF_CHECK(TestName,
                LoadedSession.ElapsedUs == CaptureSession.ElapsedUs);
    RPERF_CHECK(TestName,
                LoadedSession.FilteredSampleCount == LoadedSession.SampleCount);
    RPERF_CHECK(TestName, LoadedSession.Nodes != NULL);
    RPERF_CHECK(TestName,
                LoadedSession.Nodes[0].Count == LoadedSession.SampleCount);
    CapturedSamplesForReport = CaptureSession.SampleCount;
    Result = TRUE;

Cleanup:
    if (CaptureStarted)
    {
        RperfCaptureStop(&CaptureSession);
        RperfCaptureWait(&CaptureSession);
    }
    if (StopEvent != NULL)
        SetEvent(StopEvent);
    if (ProcessStarted && ProcessInfo.hProcess != NULL &&
        WaitForSingleObject(ProcessInfo.hProcess,
                            RPERF_LIVE_CAPTURE_TIMEOUT_MS) != WAIT_OBJECT_0)
    {
        TerminateProcess(ProcessInfo.hProcess, 5);
        WaitForSingleObject(ProcessInfo.hProcess,
                            RPERF_LIVE_CAPTURE_TIMEOUT_MS);
    }
    if (ProcessInfo.hThread != NULL)
        CloseHandle(ProcessInfo.hThread);
    if (ProcessInfo.hProcess != NULL)
        CloseHandle(ProcessInfo.hProcess);
    if (ReadyEvent != NULL)
        CloseHandle(ReadyEvent);
    if (StopEvent != NULL)
        CloseHandle(StopEvent);
    RperfSessionClear(&LoadedSession);
    RperfSessionClear(&CaptureSession);
    if (Result)
    {
        printf("[PASS] %s (%Iu samples, max depth %u)\n",
               TestName,
               CapturedSamplesForReport,
               MaximumDepth);
    }
    return Result;
}

static BOOL
RperfTestRecorderContract(PCWSTR Path)
{
    static const CHAR TestName[] =
        "recorder contract (fake backend -> v2 log -> GUI bridge)";
    RPERF_CAPTURE_CONFIGURATION Config;
    RPERF_RECORDER_CAPABILITIES Capabilities;
    RPERF_RECORDER *Recorder = NULL;
    RPERF_RECORDING *Recording = NULL;
    RPERF_RECORDING *Loaded = NULL;
    RPERF_CODEC_FORMAT Format = RperfCodecAuto;
    RPERF_SESSION Session;
    BOOL Result = FALSE;

    RperfSessionInitialize(&Session);
    RPERF_CHECK(TestName,
                RperfRecorderQueryCapabilities(RperfBackendFake,
                                               &Capabilities));
    RPERF_CHECK(TestName, Capabilities.Available);

    RperfInitializeCaptureConfiguration(&Config);
    Config.Backend = RperfBackendFake;
    Config.Scope = RperfScopeProcess;
    Config.ProcessId = GetCurrentProcessId();
    Config.IntervalUs = 1000;
    Config.DurationMs = 100;
    Config.TargetName = L"selftest-fake";
    Config.OutputPath = Path;
    RPERF_CHECK(TestName, RperfRecorderValidateConfiguration(&Config, NULL));
    RPERF_CHECK(TestName, RperfRecorderCreate(&Config, &Recorder));
    RPERF_CHECK(TestName, RperfRecorderStart(Recorder));
    RPERF_CHECK(TestName,
                RperfRecorderJoin(Recorder, RPERF_LIVE_CAPTURE_TIMEOUT_MS));
    RPERF_CHECK(TestName,
                RperfRecorderGetState(Recorder) == RperfRecorderStopped);
    Recording = RperfRecorderTakeRecording(Recorder);
    RPERF_CHECK(TestName, Recording != NULL);
    RPERF_CHECK(TestName, Recording->Info.Complete);
    RPERF_CHECK(TestName,
                RperfCodecSave(Path,
                               RperfCodecV2Binary,
                               Recording,
                               NULL, NULL, NULL));
    RPERF_CHECK(TestName,
                RperfCodecLoad(Path, RperfCodecAuto, &Config.Limits,
                               NULL, NULL, NULL, &Loaded, &Format));
    RPERF_CHECK(TestName, Format == RperfCodecV2Binary);
    RPERF_CHECK(TestName, Loaded->ModuleCount == 1);
    RPERF_CHECK(TestName,
                Loaded->Modules[0].Architecture ==
                    Recording->Modules[0].Architecture);
    RPERF_CHECK(TestName,
                Loaded->Modules[0].TimeDateStamp == 0x5a17c0de);
    RPERF_CHECK(TestName,
                Loaded->Modules[0].Checksum == 0x12345678);
    RPERF_CHECK(TestName, Loaded->Modules[0].DebugId[0] == 0x42);
    RPERF_CHECK(TestName, Loaded->Modules[0].DebugId[15] == 0xa5);
    RPERF_CHECK(TestName, Loaded->Modules[0].DebugAge == 7);
    RPERF_CHECK(TestName, Loaded->Symbols[0].Source ==
                              RperfSymbolSourceRosSym);
    RPERF_CHECK(TestName, Loaded->Symbols[0].Status ==
                              RperfSymbolStatusResolved);
    RPERF_CHECK(TestName,
                RperfLegacySessionFromRecording(Loaded, Path, &Session));
    RPERF_CHECK(TestName, Session.LogComplete);
    RPERF_CHECK(TestName, Session.Backend == RperfBackendFake);
    RPERF_CHECK(TestName, Session.SampleCount == 100);
    RPERF_CHECK(TestName, Session.Counters.AttemptedSamples == 100);
    RPERF_CHECK(TestName, Session.Counters.SuccessfulSamples == 100);
    RPERF_CHECK(TestName, Session.Counters.FailedSamples == 0);
    RPERF_CHECK(TestName, Session.FilteredSampleCount == 100);
    RPERF_CHECK(TestName, Session.Nodes != NULL);
    RPERF_CHECK(TestName, Session.Nodes[0].Count == 100);
    RPERF_CHECK(TestName, RperfFindSymbol(&Session, 0x10001000) != NULL);
    Result = TRUE;

Cleanup:
    if (Loaded != NULL)
        RperfRecordingRelease(Loaded);
    if (Recording != NULL)
        RperfRecordingRelease(Recording);
    if (Recorder != NULL)
        RperfRecorderDestroy(Recorder);
    RperfSessionClear(&Session);
    if (Result)
        printf("[PASS] %s\n", TestName);
    return Result;
}

static BOOL
RperfTestStreamedRecorderCapture(PCWSTR Path)
{
    static const CHAR TestName[] =
        "streamed recorder capture (fake backend -> live v2 stream)";
    RPERF_CAPTURE_CONFIGURATION Config;
    RPERF_CODEC_STREAM *Stream = NULL;
    RPERF_RECORDER *Recorder = NULL;
    RPERF_RECORDING *Recording = NULL;
    RPERF_RECORDING *Loaded = NULL;
    RPERF_CODEC_FORMAT Format = RperfCodecAuto;
    RPERF_SESSION Session;
    BOOL Result = FALSE;

    RperfSessionInitialize(&Session);
    RperfInitializeCaptureConfiguration(&Config);
    Config.Backend = RperfBackendFake;
    Config.Scope = RperfScopeProcess;
    Config.ProcessId = GetCurrentProcessId();
    Config.IntervalUs = 1000;
    Config.DurationMs = 100;
    Config.TargetName = L"selftest-stream";
    Config.OutputPath = Path;
    Stream = RperfCodecStreamOpen(Path, &Config.Limits);
    RPERF_CHECK(TestName, Stream != NULL);
    Config.RecordSink = RperfCodecStreamSink;
    Config.RecordSinkContext = Stream;
    RPERF_CHECK(TestName, RperfRecorderCreate(&Config, &Recorder));
    RPERF_CHECK(TestName, RperfRecorderStart(Recorder));
    RPERF_CHECK(TestName, RperfRecorderJoin(Recorder, RPERF_LIVE_CAPTURE_TIMEOUT_MS));
    Recording = RperfRecorderTakeRecording(Recorder);
    RPERF_CHECK(TestName, Recording != NULL);
    RPERF_CHECK(TestName, Recording->Info.Complete);
    RPERF_CHECK(TestName, RperfCodecStreamFinalize(Stream, Recording));
    Stream = NULL;
    RPERF_CHECK(TestName, RperfCodecLoad(Path, RperfCodecAuto, &Config.Limits, NULL, NULL, NULL, &Loaded, &Format));
    RPERF_CHECK(TestName, Format == RperfCodecV2Binary);
    RPERF_CHECK(TestName, Loaded->RecordCount == Recording->RecordCount);
    RPERF_CHECK(TestName, Loaded->ModuleCount == 1);
    RPERF_CHECK(TestName, Loaded->Modules[0].TimeDateStamp == 0x5a17c0de);
    RPERF_CHECK(TestName, RperfLegacySessionFromRecording(Loaded, Path, &Session));
    RPERF_CHECK(TestName, Session.LogComplete);
    RPERF_CHECK(TestName, Session.SampleCount == 100);
    RPERF_CHECK(TestName, Session.FilteredSampleCount == 100);
    RPERF_CHECK(TestName, Session.Nodes != NULL);
    RPERF_CHECK(TestName, Session.Nodes[0].Count == 100);
    RPERF_CHECK(TestName, RperfFindSymbol(&Session, 0x10001000) != NULL);
    Result = TRUE;

Cleanup:
    if (Stream != NULL)
        RperfCodecStreamAbort(Stream);
    if (Loaded != NULL)
        RperfRecordingRelease(Loaded);
    if (Recording != NULL)
        RperfRecordingRelease(Recording);
    if (Recorder != NULL)
        RperfRecorderDestroy(Recorder);
    RperfSessionClear(&Session);
    if (Result)
        printf("[PASS] %s\n", TestName);
    return Result;
}

typedef struct _RPERF_ASYNC_TEST_CONTEXT
{
    HANDLE CompleteEvent;
    HANDLE ProgressEnteredEvent;
    HANDLE ReleaseProgressEvent;
    volatile LONG BlockFirstProgress;
    volatile LONG ProgressBlocked;
    volatile LONG ProgressCalls;
    ULONGLONG Generation;
    RPERF_JOB_KIND Kind;
    DWORD Status;
} RPERF_ASYNC_TEST_CONTEXT;

static VOID CALLBACK
RperfAsyncTestProgress(PVOID Opaque,
                       ULONGLONG Generation,
                       RPERF_JOB_KIND Kind,
                       ULONGLONG Completed,
                       ULONGLONG Total)
{
    RPERF_ASYNC_TEST_CONTEXT *Context = Opaque;

    UNREFERENCED_PARAMETER(Generation);
    UNREFERENCED_PARAMETER(Kind);
    UNREFERENCED_PARAMETER(Completed);
    UNREFERENCED_PARAMETER(Total);
    InterlockedIncrement(&Context->ProgressCalls);
    if (InterlockedCompareExchange(&Context->BlockFirstProgress, 0, 0) != 0 &&
        InterlockedCompareExchange(&Context->ProgressBlocked, 1, 0) == 0)
    {
        SetEvent(Context->ProgressEnteredEvent);
        WaitForSingleObject(Context->ReleaseProgressEvent,
                            RPERF_STRESS_TIMEOUT_MS);
    }
}

static VOID CALLBACK
RperfAsyncTestComplete(PVOID Opaque,
                       ULONGLONG Generation,
                       RPERF_JOB_KIND Kind,
                       DWORD Status)
{
    RPERF_ASYNC_TEST_CONTEXT *Context = Opaque;

    Context->Generation = Generation;
    Context->Kind = Kind;
    Context->Status = Status;
    SetEvent(Context->CompleteEvent);
}

static VOID
RperfResetAsyncTestContext(RPERF_ASYNC_TEST_CONTEXT *Context,
                           BOOL BlockFirstProgress)
{
    ResetEvent(Context->CompleteEvent);
    ResetEvent(Context->ProgressEnteredEvent);
    ResetEvent(Context->ReleaseProgressEvent);
    InterlockedExchange(&Context->BlockFirstProgress,
                        BlockFirstProgress ? 1 : 0);
    InterlockedExchange(&Context->ProgressBlocked, 0);
    InterlockedExchange(&Context->ProgressCalls, 0);
    Context->Generation = 0;
    Context->Kind = 0;
    Context->Status = ERROR_IO_PENDING;
}

static BOOL
RperfTestAsyncPresentationController(PCWSTR Path)
{
    static const CHAR TestName[] =
        "async GUI preparation ownership and cancellation";
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_SESSION_CONTROLLER Controller;
    RPERF_ASYNC_TEST_CONTEXT Context;
    RPERF_SESSION *PreparedSession = NULL;
    ULONGLONG Generation = 0;
    BOOL ControllerInitialized = FALSE;
    BOOL Result = FALSE;

    ZeroMemory(&Context, sizeof(Context));
    Context.CompleteEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Context.ProgressEnteredEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Context.ReleaseProgressEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    RPERF_CHECK(TestName, Context.CompleteEvent != NULL);
    RPERF_CHECK(TestName, Context.ProgressEnteredEvent != NULL);
    RPERF_CHECK(TestName, Context.ReleaseProgressEvent != NULL);

    RperfControllerInitialize(&Controller);
    ControllerInitialized = TRUE;
    RperfDefaultCaptureLimits(&Limits);
    RperfResetAsyncTestContext(&Context, FALSE);
    RPERF_CHECK(TestName,
                RperfControllerBeginOpen(&Controller,
                                         Path,
                                         &Limits,
                                         RperfAsyncTestProgress,
                                         RperfAsyncTestComplete,
                                         &Context,
                                         &Generation));
    RPERF_CHECK(TestName,
                WaitForSingleObject(Context.CompleteEvent,
                                    RPERF_STRESS_TIMEOUT_MS) == WAIT_OBJECT_0);
    RPERF_CHECK(TestName, Context.Generation == Generation);
    RPERF_CHECK(TestName, Context.Kind == RperfJobOpen);
    RPERF_CHECK(TestName, Context.Status == ERROR_SUCCESS);
    RPERF_CHECK(TestName,
                RperfControllerCommitCompleted(&Controller, Generation));

    RperfResetAsyncTestContext(&Context, FALSE);
    RPERF_CHECK(TestName,
                RperfControllerBeginPrepareLegacy(&Controller,
                                                  Path,
                                                  RperfAsyncTestProgress,
                                                  RperfAsyncTestComplete,
                                                  &Context,
                                                  &Generation));
    RPERF_CHECK(TestName,
                WaitForSingleObject(Context.CompleteEvent,
                                    RPERF_STRESS_TIMEOUT_MS) == WAIT_OBJECT_0);
    RPERF_CHECK(TestName, Context.Generation == Generation);
    RPERF_CHECK(TestName, Context.Kind == RperfJobPrepareLegacy);
    RPERF_CHECK(TestName, Context.Status == ERROR_SUCCESS);
    RPERF_CHECK(TestName,
                InterlockedCompareExchange(&Context.ProgressCalls, 0, 0) > 0);
    RPERF_CHECK(TestName,
                RperfControllerCommitCompleted(&Controller, Generation));
    PreparedSession =
        RperfControllerTakePreparedSession(&Controller);
    RPERF_CHECK(TestName, PreparedSession != NULL);
    RPERF_CHECK(TestName, PreparedSession->SampleCount == 100);
    RPERF_CHECK(TestName, PreparedSession->Nodes != NULL);
    RPERF_CHECK(TestName, PreparedSession->Nodes[0].Count == 100);

    /* A cancel requested after worker completion must still reject output. */
    RperfResetAsyncTestContext(&Context, FALSE);
    RPERF_CHECK(TestName,
                RperfControllerBeginPrepareLegacy(&Controller,
                                                  Path,
                                                  RperfAsyncTestProgress,
                                                  RperfAsyncTestComplete,
                                                  &Context,
                                                  &Generation));
    RPERF_CHECK(TestName,
                WaitForSingleObject(Context.CompleteEvent,
                                    RPERF_STRESS_TIMEOUT_MS) == WAIT_OBJECT_0);
    RPERF_CHECK(TestName, Context.Status == ERROR_SUCCESS);
    RperfControllerCancel(&Controller);
    RPERF_CHECK(TestName,
                !RperfControllerCommitCompleted(&Controller, Generation));
    RPERF_CHECK(TestName, GetLastError() == ERROR_CANCELLED);
    RPERF_CHECK(TestName,
                RperfControllerTakePreparedSession(&Controller) == NULL);

    /* Also cancel while the conversion worker is actively inside a phase. */
    RperfResetAsyncTestContext(&Context, TRUE);
    RPERF_CHECK(TestName,
                RperfControllerBeginPrepareLegacy(&Controller,
                                                  Path,
                                                  RperfAsyncTestProgress,
                                                  RperfAsyncTestComplete,
                                                  &Context,
                                                  &Generation));
    RPERF_CHECK(TestName,
                WaitForSingleObject(Context.ProgressEnteredEvent,
                                    RPERF_STRESS_TIMEOUT_MS) == WAIT_OBJECT_0);
    RperfControllerCancel(&Controller);
    SetEvent(Context.ReleaseProgressEvent);
    RPERF_CHECK(TestName,
                WaitForSingleObject(Context.CompleteEvent,
                                    RPERF_STRESS_TIMEOUT_MS) == WAIT_OBJECT_0);
    RPERF_CHECK(TestName, Context.Generation == Generation);
    RPERF_CHECK(TestName, Context.Kind == RperfJobPrepareLegacy);
    RPERF_CHECK(TestName, Context.Status == ERROR_CANCELLED);
    RPERF_CHECK(TestName,
                !RperfControllerCommitCompleted(&Controller, Generation));
    RPERF_CHECK(TestName, GetLastError() == ERROR_CANCELLED);
    RPERF_CHECK(TestName,
                RperfControllerTakePreparedSession(&Controller) == NULL);

    RperfControllerDestroy(&Controller);
    ControllerInitialized = FALSE;
    RPERF_CHECK(TestName, PreparedSession->SampleCount == 100);
    RPERF_CHECK(TestName, PreparedSession->Nodes[0].Count == 100);
    Result = TRUE;

Cleanup:
    if (Context.ReleaseProgressEvent != NULL)
        SetEvent(Context.ReleaseProgressEvent);
    if (ControllerInitialized)
        RperfControllerDestroy(&Controller);
    if (PreparedSession != NULL)
    {
        RperfSessionClear(PreparedSession);
        HeapFree(GetProcessHeap(), 0, PreparedSession);
    }
    if (Context.ReleaseProgressEvent != NULL)
        CloseHandle(Context.ReleaseProgressEvent);
    if (Context.ProgressEnteredEvent != NULL)
        CloseHandle(Context.ProgressEnteredEvent);
    if (Context.CompleteEvent != NULL)
        CloseHandle(Context.CompleteEvent);
    if (Result)
        printf("[PASS] %s\n", TestName);
    return Result;
}

static BOOL
RperfTestRosProfImageRecords(VOID)
{
    static const CHAR TestName[] = "RosProf image metadata parser";
    static const CHAR ImagePath[] = "\\SystemRoot\\System32\\test-image.dll";
    union
    {
        ULONGLONG Alignment;
        UCHAR Bytes[256];
    } Fixture, Invalid;
    ROSPROF_IMAGE_RECORD_V1 *Image;
    RPERF_MODULE Module;
    PWSTR Path = NULL;
    ULONG BuildOffset, Index;
    BOOL Result = FALSE;

    ZeroMemory(&Fixture, sizeof(Fixture));
    Image = (ROSPROF_IMAGE_RECORD_V1 *)Fixture.Bytes;
    Image->Header.Type = ROSPROF_RECORD_IMAGE;
    Image->Header.Version = 1;
    Image->Header.Flags = ROSPROF_RECORD_FLAG_COMMITTED;
    Image->Header.HeaderSize = sizeof(Image->Header);
    Image->Event = ROSPROF_IMAGE_LOAD;
    Image->ImageFlags = ROSPROF_IMAGE_FLAG_BUILD_ID_RSDS |
        ((ULONG)IMAGE_FILE_MACHINE_AMD64 << ROSPROF_IMAGE_MACHINE_SHIFT);
    Image->ImageBase = 0x0000000180000000ULL;
    Image->ImageSize = 0x23000;
    Image->ImageKey = 0x1122334455667788ULL;
    Image->PathOffset = sizeof(*Image);
    Image->PathBytes = sizeof(ImagePath) - 1;
    CopyMemory(Fixture.Bytes + Image->PathOffset,
               ImagePath,
               Image->PathBytes);
    BuildOffset = RosProfAlignUpU32(Image->PathOffset + Image->PathBytes);
    Image->BuildIdOffset = BuildOffset;
    Image->BuildIdBytes = 20;
    Image->Checksum = 0x12345678;
    Image->TimeDateStamp = 0x66778899;
    for (Index = 0; Index < 16; Index++)
        Fixture.Bytes[BuildOffset + Index] = (UCHAR)(Index + 1);
    Fixture.Bytes[BuildOffset + 16] = 0x78;
    Fixture.Bytes[BuildOffset + 17] = 0x56;
    Fixture.Bytes[BuildOffset + 18] = 0x34;
    Fixture.Bytes[BuildOffset + 19] = 0x12;
    Image->Header.Size = RosProfAlignUpU32(BuildOffset + 20);

    RPERF_CHECK(TestName,
                RperfKernelDecodeImageRecord(&Image->Header,
                                             4096,
                                             ROSPROF_ARCH_X86,
                                             0x44,
                                             &Module,
                                             &Path));
    RPERF_CHECK(TestName, Path != NULL && lstrcmpW(Path,
        L"\\SystemRoot\\System32\\test-image.dll") == 0);
    RPERF_CHECK(TestName, Module.Id == Image->ImageKey);
    RPERF_CHECK(TestName, Module.ProcessKey == 0x44);
    RPERF_CHECK(TestName, Module.Architecture == ROSPROF_ARCH_AMD64);
    RPERF_CHECK(TestName, Module.Checksum == Image->Checksum);
    RPERF_CHECK(TestName, Module.TimeDateStamp == Image->TimeDateStamp);
    RPERF_CHECK(TestName, Module.DebugAge == 0x12345678);
    RPERF_CHECK(TestName,
                memcmp(Module.DebugId, Fixture.Bytes + BuildOffset, 16) == 0);
    HeapFree(GetProcessHeap(), 0, Path);
    Path = NULL;

    CopyMemory(&Invalid, &Fixture, sizeof(Invalid));
    ((ROSPROF_IMAGE_RECORD_V1 *)Invalid.Bytes)->PathOffset = 8;
    RPERF_CHECK(TestName,
                !RperfKernelDecodeImageRecord(
                    (ROSPROF_RECORD_HEADER *)Invalid.Bytes,
                    4096, ROSPROF_ARCH_X86, 1, &Module, &Path));

    CopyMemory(&Invalid, &Fixture, sizeof(Invalid));
    ((ROSPROF_IMAGE_RECORD_V1 *)Invalid.Bytes)->BuildIdOffset =
        sizeof(ROSPROF_IMAGE_RECORD_V1);
    RPERF_CHECK(TestName,
                !RperfKernelDecodeImageRecord(
                    (ROSPROF_RECORD_HEADER *)Invalid.Bytes,
                    4096, ROSPROF_ARCH_X86, 1, &Module, &Path));

    CopyMemory(&Invalid, &Fixture, sizeof(Invalid));
    ((ROSPROF_IMAGE_RECORD_V1 *)Invalid.Bytes)->BuildIdBytes = 16;
    RPERF_CHECK(TestName,
                !RperfKernelDecodeImageRecord(
                    (ROSPROF_RECORD_HEADER *)Invalid.Bytes,
                    4096, ROSPROF_ARCH_X86, 1, &Module, &Path));

    CopyMemory(&Invalid, &Fixture, sizeof(Invalid));
    Invalid.Bytes[sizeof(ROSPROF_IMAGE_RECORD_V1)] = 0xc0;
    RPERF_CHECK(TestName,
                !RperfKernelDecodeImageRecord(
                    (ROSPROF_RECORD_HEADER *)Invalid.Bytes,
                    4096, ROSPROF_ARCH_X86, 1, &Module, &Path));

    CopyMemory(&Invalid, &Fixture, sizeof(Invalid));
    ((ROSPROF_IMAGE_RECORD_V1 *)Invalid.Bytes)->PathBytes = MAXDWORD;
    RPERF_CHECK(TestName,
                !RperfKernelDecodeImageRecord(
                    (ROSPROF_RECORD_HEADER *)Invalid.Bytes,
                    4096, ROSPROF_ARCH_X86, 1, &Module, &Path));
    Result = TRUE;

Cleanup:
    if (Path != NULL)
        HeapFree(GetProcessHeap(), 0, Path);
    if (Result)
        printf("[PASS] %s (valid RSDS + 5 malformed cases)\n", TestName);
    return Result;
}

static VOID
RperfWriteEtwPointer(PUCHAR Data,
                     ULONG PointerSize,
                     ULONGLONG Value)
{
    CopyMemory(Data, &Value, PointerSize);
}

static BOOL
RperfTestRosProfSequenceOrdering(VOID)
{
    static const CHAR TestName[] = "RosProf sequence ordering";

    RPERF_CHECK(TestName, RperfKernelSequenceAfter(1, 0));
    RPERF_CHECK(TestName, !RperfKernelSequenceAfter(0, 0));
    RPERF_CHECK(TestName, RperfKernelSequenceAfter(0, MAXULONGLONG));
    RPERF_CHECK(TestName, !RperfKernelSequenceAfter(MAXULONGLONG, 0));

    printf("[PASS] %s\n", TestName);
    return TRUE;

Cleanup:
    return FALSE;
}

static BOOL
RperfTestEtwLifecycleRecords(VOID)
{
    static const CHAR TestName[] = "ETW lifecycle payload IDs";
    UCHAR Data[16];
    ULONG ProcessId = 4242, ThreadId = 31337;
    ULONG DecodedProcess, DecodedThread;
    ULONG PointerSize;
    BOOL Result = FALSE;

    for (PointerSize = 4; PointerSize <= 8; PointerSize += 4)
    {
        /* Process V0 starts with ProcessId. */
        ZeroMemory(Data, sizeof(Data));
        CopyMemory(Data, &ProcessId, sizeof(ProcessId));
        RPERF_CHECK(TestName,
                    RperfEtwDecodeLifecycleIds(Data,
                                               2 * sizeof(ULONG),
                                               0,
                                               PointerSize,
                                               FALSE,
                                               &DecodedProcess,
                                               &DecodedThread));
        RPERF_CHECK(TestName, DecodedProcess == ProcessId);
        RPERF_CHECK(TestName, DecodedThread == 0);

        /* Process V1 and later start with a pointer-sized unique key. */
        ZeroMemory(Data, sizeof(Data));
        RperfWriteEtwPointer(Data, PointerSize, 0x12345678);
        CopyMemory(Data + PointerSize, &ProcessId, sizeof(ProcessId));
        RPERF_CHECK(TestName,
                    RperfEtwDecodeLifecycleIds(Data,
                                               (USHORT)(PointerSize + sizeof(ProcessId)),
                                               1,
                                               PointerSize,
                                               FALSE,
                                               &DecodedProcess,
                                               &DecodedThread));
        RPERF_CHECK(TestName, DecodedProcess == ProcessId);
        RPERF_CHECK(TestName, DecodedThread == 0);
    }

    /* Thread V0 stores ThreadId before ProcessId. */
    CopyMemory(Data, &ThreadId, sizeof(ThreadId));
    CopyMemory(Data + sizeof(ThreadId), &ProcessId, sizeof(ProcessId));
    RPERF_CHECK(TestName,
                RperfEtwDecodeLifecycleIds(Data,
                                           2 * sizeof(ULONG),
                                           0,
                                           sizeof(PVOID),
                                           TRUE,
                                           &DecodedProcess,
                                           &DecodedThread));
    RPERF_CHECK(TestName, DecodedProcess == ProcessId);
    RPERF_CHECK(TestName, DecodedThread == ThreadId);

    /* Thread V1 and later store ProcessId before ThreadId. */
    CopyMemory(Data, &ProcessId, sizeof(ProcessId));
    CopyMemory(Data + sizeof(ProcessId), &ThreadId, sizeof(ThreadId));
    RPERF_CHECK(TestName,
                RperfEtwDecodeLifecycleIds(Data,
                                           2 * sizeof(ULONG),
                                           1,
                                           sizeof(PVOID),
                                           TRUE,
                                           &DecodedProcess,
                                           &DecodedThread));
    RPERF_CHECK(TestName, DecodedProcess == ProcessId);
    RPERF_CHECK(TestName, DecodedThread == ThreadId);
    RPERF_CHECK(TestName,
                !RperfEtwDecodeLifecycleIds(Data,
                                            sizeof(ULONG),
                                            1,
                                            sizeof(PVOID),
                                            TRUE,
                                            &DecodedProcess,
                                            &DecodedThread));
    Result = TRUE;

Cleanup:
    if (Result)
        printf("[PASS] %s\n", TestName);
    return Result;
}

static USHORT
RperfBuildEtwImageFixture(PUCHAR Data,
                          SIZE_T DataCapacity,
                          ULONG PointerSize,
                          UCHAR Version,
                          PCWSTR Path,
                          ULONGLONG Base,
                          ULONGLONG Size,
                          ULONG Checksum,
                          ULONG TimeDateStamp)
{
    ULONG ProcessId = 4242;
    ULONG Offset;
    SIZE_T PathBytes = (wcslen(Path) + 1) * sizeof(WCHAR);

    ZeroMemory(Data, DataCapacity);
    if (PointerSize != 4 && PointerSize != 8)
        return 0;
    Offset = Version >= 2 ? PointerSize * 3 + 8 * sizeof(ULONG) :
                            PointerSize * 2 + sizeof(ULONG);
    if (Offset + PathBytes > DataCapacity ||
        Offset + PathBytes > 0xffff)
    {
        return 0;
    }
    RperfWriteEtwPointer(Data, PointerSize, Base);
    RperfWriteEtwPointer(Data + PointerSize, PointerSize, Size);
    CopyMemory(Data + PointerSize * 2, &ProcessId, sizeof(ProcessId));
    if (Version >= 2)
    {
        ULONG ChecksumOffset = PointerSize * 2 + sizeof(ULONG);
        ULONGLONG DefaultBase = Base;

        CopyMemory(Data + ChecksumOffset, &Checksum, sizeof(Checksum));
        CopyMemory(Data + ChecksumOffset + sizeof(ULONG),
                   &TimeDateStamp, sizeof(TimeDateStamp));
        RperfWriteEtwPointer(Data + PointerSize * 2 + 4 * sizeof(ULONG),
                             PointerSize,
                             DefaultBase);
    }
    CopyMemory(Data + Offset, Path, PathBytes);
    return (USHORT)(Offset + PathBytes);
}

static BOOL
RperfTestEtwImageRecords(VOID)
{
    static const CHAR TestName[] = "ETW Image_Load v1/v2 parser";
    static const WCHAR ImagePath[] = L"C:\\Windows\\System32\\etw-test.dll";
    static const ULONG PointerSizes[] = {4, 8, 4, 8};
    static const UCHAR Versions[] = {1, 1, 2, 2};
    union
    {
        ULONGLONG Alignment;
        UCHAR Bytes[512];
    } Fixture, Invalid;
    RPERF_MODULE Module;
    PWSTR Path = NULL;
    ULONGLONG Base, Size = 0x25000;
    ULONG Checksum = 0x13572468;
    ULONG TimeDateStamp = 0x66778899;
    USHORT Length = 0;
    ULONG Index;
    BOOL Result = FALSE;

    for (Index = 0; Index < ARRAYSIZE(PointerSizes); ++Index)
    {
        ULONG PointerSize = PointerSizes[Index];
        ULONG Architecture = PointerSize == 8 ?
                             RPERF_ARCH_AMD64 : RPERF_ARCH_X86;
        UCHAR Version = Versions[Index];

        Base = PointerSize == 8 ? 0x0000000180000000ULL :
                                  0x71000000ULL;
        Length = RperfBuildEtwImageFixture(Fixture.Bytes,
                                            sizeof(Fixture.Bytes),
                                            PointerSize,
                                            Version,
                                            ImagePath,
                                            Base,
                                            Size,
                                            Checksum,
                                            TimeDateStamp);
        RPERF_CHECK(TestName, Length != 0);
        RPERF_CHECK(TestName,
                    RperfEtwDecodeImageRecord(Fixture.Bytes,
                                              Length,
                                              PointerSize,
                                              Architecture,
                                              Version,
                                              4096,
                                              0x1122334455667788ULL,
                                              &Module,
                                              &Path));
        RPERF_CHECK(TestName, Module.ProcessKey == 0x1122334455667788ULL);
        RPERF_CHECK(TestName, Module.Base == Base);
        RPERF_CHECK(TestName, Module.Size == Size);
        RPERF_CHECK(TestName, Module.Architecture == Architecture);
        RPERF_CHECK(TestName,
                    Module.Checksum == (Version >= 2 ? Checksum : 0));
        RPERF_CHECK(TestName,
                    Module.TimeDateStamp ==
                        (Version >= 2 ? TimeDateStamp : 0));
        RPERF_CHECK(TestName, Module.Path == Path);
        RPERF_CHECK(TestName, lstrcmpW(Path, ImagePath) == 0);
        HeapFree(GetProcessHeap(), 0, Path);
        Path = NULL;
    }

    Length = RperfBuildEtwImageFixture(Fixture.Bytes,
                                        sizeof(Fixture.Bytes),
                                        8,
                                        2,
                                        ImagePath,
                                        0x0000000180000000ULL,
                                        Size,
                                        Checksum,
                                        TimeDateStamp);
    RPERF_CHECK(TestName, Length != 0);
    RPERF_CHECK(TestName,
                !RperfEtwDecodeImageRecord(NULL, Length, 8,
                                           RPERF_ARCH_AMD64, 2, 4096, 1,
                                           &Module, &Path));
    RPERF_CHECK(TestName,
                !RperfEtwDecodeImageRecord(Fixture.Bytes, Length, 16,
                                           RPERF_ARCH_AMD64, 2, 4096, 1,
                                           &Module, &Path));
    RPERF_CHECK(TestName,
                !RperfEtwDecodeImageRecord(Fixture.Bytes, Length, 8,
                                           RPERF_ARCH_UNKNOWN, 2, 4096, 1,
                                           &Module, &Path));
    RPERF_CHECK(TestName,
                !RperfEtwDecodeImageRecord(Fixture.Bytes,
                                           (USHORT)(8 * 3 +
                                                    8 * sizeof(ULONG) - 1),
                                           8, RPERF_ARCH_AMD64, 2, 4096, 1,
                                           &Module, &Path));

    CopyMemory(&Invalid, &Fixture, sizeof(Invalid));
    {
        WCHAR NonTerminator = L'X';
        CopyMemory(Invalid.Bytes + Length - sizeof(WCHAR),
                   &NonTerminator,
                   sizeof(NonTerminator));
    }
    RPERF_CHECK(TestName,
                !RperfEtwDecodeImageRecord(Invalid.Bytes, Length, 8,
                                           RPERF_ARCH_AMD64, 2, 4096, 1,
                                           &Module, &Path));
    RPERF_CHECK(TestName,
                !RperfEtwDecodeImageRecord(Fixture.Bytes, Length - 1,
                                           8, RPERF_ARCH_AMD64, 2, 4096, 1,
                                           &Module, &Path));
    RPERF_CHECK(TestName,
                !RperfEtwDecodeImageRecord(
                    Fixture.Bytes, Length, 8, RPERF_ARCH_AMD64, 2,
                    (ULONG)(wcslen(ImagePath) * sizeof(WCHAR)),
                    1, &Module, &Path));
    Result = TRUE;

Cleanup:
    if (Path != NULL)
        HeapFree(GetProcessHeap(), 0, Path);
    if (Result)
        printf("[PASS] %s (4 layouts + 7 malformed cases)\n", TestName);
    return Result;
}

static DECLSPEC_NOINLINE BOOL
RperfTestOfflineSymbols(VOID)
{
    static const CHAR TestName[] =
        "offline DbgHelp identity and embedded symbols";
    WCHAR ImagePath[MAX_PATH];
    WCHAR ImageDirectory[MAX_PATH];
    PWSTR Slash, Backslash;
    DWORD ImageLength;
    RPERF_PE_IDENTITY Identity;
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_RECORDING *Recording = NULL;
    RPERF_RECORDING *Symbolized = NULL;
    RPERF_SYMBOL_PROVIDER *Provider = NULL;
    RPERF_DBGHELP_CONFIGURATION Configuration;
    RPERF_SYMBOLIZATION_SUMMARY Summary;
    RPERF_MODULE Module;
    RPERF_RECORD Record;
    const RPERF_MODEL_SYMBOL *Symbol;
    ULONGLONG Address = (ULONGLONG)(ULONG_PTR)RperfTestOfflineSymbols;
    BOOL Result = FALSE;

    ImageLength = GetModuleFileNameW(NULL, ImagePath,
                                     ARRAYSIZE(ImagePath));
    RPERF_CHECK(TestName,
                ImageLength != 0 && ImageLength < ARRAYSIZE(ImagePath));
    RPERF_CHECK(TestName, RperfReadPeIdentity(ImagePath, &Identity));
    RPERF_CHECK(TestName, Identity.ImageSize != 0);
    RPERF_CHECK(TestName,
                Identity.Architecture == RperfNativeArchitecture());

    RperfDefaultCaptureLimits(&Limits);
    Recording = RperfRecordingCreate(&Limits);
    RPERF_CHECK(TestName, Recording != NULL);
    Recording->Info.Backend = RperfBackendIntrusive;
    Recording->Info.Metric = RperfMetricCpuSamples;
    Recording->Info.ProducerArchitecture = Identity.Architecture;
    Recording->Info.AddressWidth = sizeof(PVOID) * 8;
    Recording->Info.Complete = TRUE;
    Recording->Info.CompletionReason = RperfCompletionDuration;
    RPERF_CHECK(TestName,
                RperfRecordingSetTargetName(Recording,
                                             L"rosprofiler_selftest"));
    ZeroMemory(&Module, sizeof(Module));
    Module.Id = 1;
    Module.ProcessKey = GetCurrentProcessId();
    Module.Base = (ULONGLONG)(ULONG_PTR)GetModuleHandleW(NULL);
    Module.Path = ImagePath;
    RPERF_CHECK(TestName, RperfEnrichModuleFromImage(&Module, ImagePath));
    RPERF_CHECK(TestName, RperfRecordingAddModule(Recording, &Module));
    ZeroMemory(&Record, sizeof(Record));
    Record.Header.Kind = RperfRecordSample;
    Record.Header.Sequence = 1;
    Record.Header.ProcessId = GetCurrentProcessId();
    Record.Header.ThreadId = GetCurrentThreadId();
    Record.Header.ProcessKey = GetCurrentProcessId();
    Record.Header.ThreadKey = GetCurrentThreadId();
    Record.Header.Cpu = RPERF_MODEL_ALL_CPUS;
    Record.Data.Sample.Depth = 1;
    Record.Data.Sample.Weight = 1;
    Record.Data.Sample.Frames[0].Address = Address;
    Record.Data.Sample.Frames[0].FunctionAddress = Address;
    Record.Data.Sample.Frames[0].ModuleId = Module.Id;
    Record.Data.Sample.Frames[0].Context = RperfContextUser;
    Record.Data.Sample.Frames[0].Resolution = RperfResolutionAddress;
    RPERF_CHECK(TestName, RperfRecordingAddRecord(Recording, &Record));
    RPERF_CHECK(TestName, RperfRecordingFreeze(Recording));

    lstrcpynW(ImageDirectory, ImagePath, ARRAYSIZE(ImageDirectory));
    Slash = wcsrchr(ImageDirectory, L'/');
    Backslash = wcsrchr(ImageDirectory, L'\\');
    if (Slash == NULL || (Backslash != NULL && Backslash > Slash))
        Slash = Backslash;
    RPERF_CHECK(TestName, Slash != NULL);
    *Slash = UNICODE_NULL;
    ZeroMemory(&Configuration, sizeof(Configuration));
    Configuration.ImageSearchPath = ImageDirectory;
    Configuration.SymbolSearchPath = ImageDirectory;
    Configuration.MaximumCacheEntries = 64;
    Provider = RperfCreateDbgHelpSymbolProvider(&Configuration);
    RPERF_CHECK(TestName, Provider != NULL);
    RPERF_CHECK(TestName,
                RperfSymbolizeRecording(Recording, Provider,
                                        NULL, NULL, NULL, &Symbolized));
    RPERF_CHECK(TestName,
                RperfQuerySymbolProviderSummary(Provider, &Summary));
    RPERF_CHECK(TestName, Summary.Attempted == 1);
    RPERF_CHECK(TestName, Summary.ModuleOffset == 0);
    Symbol = RperfRecordingFindSymbol(Symbolized, Address);
    RPERF_CHECK(TestName, Symbol != NULL);
    RPERF_CHECK(TestName,
                Symbol->Resolution >= RperfResolutionFunction);
    RPERF_CHECK(TestName, Symbol->Status == RperfSymbolStatusResolved);
    RPERF_CHECK(TestName, Symbol->Name != NULL &&
                              Symbol->Name[0] != ANSI_NULL);
    RPERF_CHECK(TestName,
                Symbol->Source == RperfSymbolSourceRosSym ||
                Symbol->Source == RperfSymbolSourcePdb ||
                Symbol->Source == RperfSymbolSourceDwarf ||
                Symbol->Source == RperfSymbolSourceCoff);
    Result = TRUE;

Cleanup:
    if (Provider != NULL)
        RperfDestroySymbolProvider(Provider);
    if (Symbolized != NULL)
        RperfRecordingRelease(Symbolized);
    if (Recording != NULL)
        RperfRecordingRelease(Recording);
    if (Result)
        printf("[PASS] %s\n", TestName);
    return Result;
}

typedef enum _RPERF_PDB_EXPECTATION
{
    RperfPdbExpectedMatch = 0,
    RperfPdbExpectedIdentityMismatch,
    RperfPdbExpectedSymbolsMissing
} RPERF_PDB_EXPECTATION;

static BOOL
RperfTestPdbFixture(PCWSTR ImagePath,
                    PCWSTR SymbolDirectory,
                    ULONGLONG RelativeAddress,
                    RPERF_PDB_EXPECTATION Expectation)
{
    static const CHAR TestName[] = "offline DbgHelp PDB fixture";
    RPERF_PE_IDENTITY Identity;
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_RECORDING *Recording = NULL;
    RPERF_RECORDING *Symbolized = NULL;
    RPERF_SYMBOL_PROVIDER *Provider = NULL;
    RPERF_DBGHELP_CONFIGURATION Configuration;
    RPERF_SYMBOLIZATION_SUMMARY Summary;
    RPERF_MODULE Module;
    RPERF_RECORD Record;
    const RPERF_MODEL_SYMBOL *Symbol;
    ULONGLONG Address;
    BOOL Result = FALSE;

    RPERF_CHECK(TestName, RperfReadPeIdentity(ImagePath, &Identity));
    RPERF_CHECK(TestName, Identity.ImageSize != 0);
    RPERF_CHECK(TestName, RelativeAddress < Identity.ImageSize);
    RPERF_CHECK(TestName, Identity.DebugAge != 0);
    RPERF_CHECK(TestName, !Identity.HasRosSym);

    RperfDefaultCaptureLimits(&Limits);
    Recording = RperfRecordingCreate(&Limits);
    RPERF_CHECK(TestName, Recording != NULL);
    Recording->Info.Backend = RperfBackendIntrusive;
    Recording->Info.Metric = RperfMetricCpuSamples;
    Recording->Info.ProducerArchitecture = Identity.Architecture;
    Recording->Info.AddressWidth = sizeof(PVOID) * 8;
    Recording->Info.Complete = TRUE;
    Recording->Info.CompletionReason = RperfCompletionDuration;
    RPERF_CHECK(TestName,
                RperfRecordingSetTargetName(Recording, L"pdb-fixture"));

    ZeroMemory(&Module, sizeof(Module));
    Module.Id = 1;
    Module.ProcessKey = 1;
    Module.Base = 0x0000000180000000ULL;
    Module.Path = (PWSTR)ImagePath;
    RPERF_CHECK(TestName, RperfEnrichModuleFromImage(&Module, ImagePath));
    RPERF_CHECK(TestName, RperfRecordingAddModule(Recording, &Module));
    Address = Module.Base + RelativeAddress;

    ZeroMemory(&Record, sizeof(Record));
    Record.Header.Kind = RperfRecordSample;
    Record.Header.Sequence = 1;
    Record.Header.ProcessId = 1;
    Record.Header.ThreadId = 1;
    Record.Header.ProcessKey = 1;
    Record.Header.ThreadKey = 1;
    Record.Header.Cpu = RPERF_MODEL_ALL_CPUS;
    Record.Data.Sample.Depth = 1;
    Record.Data.Sample.Weight = 1;
    Record.Data.Sample.Frames[0].Address = Address;
    Record.Data.Sample.Frames[0].FunctionAddress = Address;
    Record.Data.Sample.Frames[0].ModuleId = Module.Id;
    Record.Data.Sample.Frames[0].Context = RperfContextUser;
    Record.Data.Sample.Frames[0].Resolution = RperfResolutionAddress;
    RPERF_CHECK(TestName, RperfRecordingAddRecord(Recording, &Record));
    RPERF_CHECK(TestName, RperfRecordingFreeze(Recording));

    ZeroMemory(&Configuration, sizeof(Configuration));
    Configuration.ImageSearchPath = SymbolDirectory;
    Configuration.SymbolSearchPath = SymbolDirectory;
    Configuration.MaximumCacheEntries = 16;
    Provider = RperfCreateDbgHelpSymbolProvider(&Configuration);
    RPERF_CHECK(TestName, Provider != NULL);
    RPERF_CHECK(TestName,
                RperfSymbolizeRecording(Recording, Provider,
                                        NULL, NULL, NULL, &Symbolized));
    RPERF_CHECK(TestName,
                RperfQuerySymbolProviderSummary(Provider, &Summary));
    RPERF_CHECK(TestName, Summary.Attempted == 1);
    Symbol = RperfRecordingFindSymbol(Symbolized, Address);
    RPERF_CHECK(TestName, Symbol != NULL);
    if (Expectation == RperfPdbExpectedIdentityMismatch)
    {
        printf("PDB mismatch summary: pdb=%Iu offset=%Iu mismatch=%Iu "
               "missing=%Iu load=%Iu\n",
               Summary.Pdb, Summary.ModuleOffset,
               Summary.IdentityMismatch, Summary.SymbolsMissing,
               Summary.LoadErrors);
        RPERF_CHECK(TestName, Summary.Pdb == 0);
        RPERF_CHECK(TestName, Summary.ModuleOffset == 1);
        RPERF_CHECK(TestName, Summary.IdentityMismatch == 1);
        RPERF_CHECK(TestName,
                    Symbol->Source == RperfSymbolSourceModuleOffset);
        RPERF_CHECK(TestName,
                    Symbol->Status == RperfSymbolStatusIdentityMismatch);
    }
    else if (Expectation == RperfPdbExpectedSymbolsMissing)
    {
        printf("PDB invalid-file summary: pdb=%Iu offset=%Iu mismatch=%Iu "
               "missing=%Iu load=%Iu\n",
               Summary.Pdb, Summary.ModuleOffset,
               Summary.IdentityMismatch, Summary.SymbolsMissing,
               Summary.LoadErrors);
        RPERF_CHECK(TestName, Summary.Pdb == 0);
        RPERF_CHECK(TestName, Summary.ModuleOffset == 1);
        RPERF_CHECK(TestName, Summary.IdentityMismatch == 0);
        RPERF_CHECK(TestName, Summary.SymbolsMissing == 1);
        RPERF_CHECK(TestName,
                    Symbol->Source == RperfSymbolSourceModuleOffset);
        RPERF_CHECK(TestName,
                    Symbol->Status == RperfSymbolStatusSymbolsMissing);
    }
    else
    {
        RPERF_CHECK(TestName, Summary.Pdb == 1);
        RPERF_CHECK(TestName, Summary.ModuleOffset == 0);
        RPERF_CHECK(TestName, Summary.IdentityMismatch == 0);
        RPERF_CHECK(TestName, Symbol->Source == RperfSymbolSourcePdb);
        RPERF_CHECK(TestName, Symbol->Status == RperfSymbolStatusResolved);
        RPERF_CHECK(TestName, Symbol->Name != NULL);
        RPERF_CHECK(TestName,
                    strstr(Symbol->Name,
                           "ProfilePdbFixtureTarget") != NULL);
    }
    Result = TRUE;

Cleanup:
    if (Provider != NULL)
        RperfDestroySymbolProvider(Provider);
    if (Symbolized != NULL)
        RperfRecordingRelease(Symbolized);
    if (Recording != NULL)
        RperfRecordingRelease(Recording);
    if (Result)
    {
        PCSTR Outcome = "matching private symbol";

        if (Expectation == RperfPdbExpectedIdentityMismatch)
            Outcome = "mismatch rejected";
        else if (Expectation == RperfPdbExpectedSymbolsMissing)
            Outcome = "invalid PDB rejected";
        printf("[PASS] %s (%s)\n", TestName,
               Outcome);
    }
    return Result;
}

static int
RperfInspectSavedLog(PCWSTR Path,
                     BOOL ListModules)
{
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_RECORDING *Recording = NULL;
    RPERF_CODEC_FORMAT Format = RperfCodecAuto;
    ULONGLONG Kinds[RperfRecordSessionEnd + 1];
    SIZE_T Index;

    ZeroMemory(Kinds, sizeof(Kinds));
    RperfDefaultCaptureLimits(&Limits);
    if (!RperfCodecLoad(Path, RperfCodecAuto, &Limits,
                        NULL, NULL, NULL, &Recording, &Format))
    {
        wprintf(L"inspect failed: Win32 error %lu\n", GetLastError());
        return 1;
    }
    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        RPERF_RECORD_KIND Kind = Recording->Records[Index].Header.Kind;
        if (Kind >= RperfRecordSessionInfo &&
            Kind <= RperfRecordSessionEnd)
        {
            Kinds[Kind]++;
        }
    }
    wprintf(L"format=%u backend=%u metric=%u complete=%u reason=%u error=%lu\n",
            (unsigned)Format,
            (unsigned)Recording->Info.Backend,
            (unsigned)Recording->Info.Metric,
            Recording->Info.Complete,
            (unsigned)Recording->Info.CompletionReason,
            Recording->Info.CompletionError);
    wprintf(L"records=%Iu modules=%Iu symbols=%Iu samples=%I64u lost=%I64u\n",
            Recording->RecordCount,
            Recording->ModuleCount,
            Recording->SymbolCount,
            Kinds[RperfRecordSample],
            Kinds[RperfRecordLost]);
    wprintf(L"attempted=%I64u successful=%I64u failed=%I64u skipped=%I64u "
            L"truncated=%I64u lost_records=%I64u malformed=%I64u\n",
            Recording->Counters.AttemptedSamples,
            Recording->Counters.SuccessfulSamples,
            Recording->Counters.FailedSamples,
            Recording->Counters.SkippedSamples,
            Recording->Counters.TruncatedSamples,
            Recording->Counters.LostRecords,
            Recording->Counters.MalformedRecords);
    for (Index = RperfRecordSessionInfo;
         Index <= RperfRecordSessionEnd;
         ++Index)
    {
        if (Kinds[Index] != 0)
            wprintf(L"kind[%Iu]=%I64u\n", Index, Kinds[Index]);
    }
    if (ListModules)
    {
        for (Index = 0; Index < Recording->ModuleCount; ++Index)
        {
            const RPERF_MODULE *Module = &Recording->Modules[Index];

            wprintf(L"module[%Iu] id=%I64u process=0x%I64x "
                    L"base=0x%016I64x size=0x%I64x arch=%lu flags=0x%08lx "
                    L"path=%ls\n",
                    Index,
                    Module->Id,
                    Module->ProcessKey,
                    Module->Base,
                    Module->Size,
                    Module->Architecture,
                    Module->Flags,
                    Module->Path != NULL ? Module->Path : L"");
        }
    }
    RperfRecordingRelease(Recording);
    return 0;
}

static int
RperfAnalyzeSavedLog(PCWSTR Path,
                     PCWSTR ImageSearchPath,
                     PCWSTR SymbolSearchPath)
{
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_DBGHELP_CONFIGURATION Configuration;
    RPERF_SYMBOLIZATION_SUMMARY Summary;
    RPERF_SYMBOL_PROVIDER *Provider = NULL;
    RPERF_RECORDING *Recording = NULL;
    RPERF_RECORDING *Symbolized = NULL;
    RPERF_ANALYSIS *Analysis = NULL;
    RPERF_FILTER Filter;
    RPERF_CODEC_FORMAT Format = RperfCodecAuto;
    int Result = 1;

    RperfDefaultCaptureLimits(&Limits);
    if (!RperfCodecLoad(Path, RperfCodecAuto, &Limits,
                        NULL, NULL, NULL, &Recording, &Format))
    {
        printf("analysis load failed: Win32 error %lu\n", GetLastError());
        goto Cleanup;
    }
    ZeroMemory(&Configuration, sizeof(Configuration));
    Configuration.ImageSearchPath = ImageSearchPath;
    Configuration.SymbolSearchPath = SymbolSearchPath;
    Provider = RperfCreateDbgHelpSymbolProvider(&Configuration);
    if (Provider == NULL)
    {
        printf("symbol provider failed: Win32 error %lu\n", GetLastError());
        goto Cleanup;
    }
    if (!RperfSymbolizeRecording(Recording, Provider,
                                 NULL, NULL, NULL, &Symbolized))
    {
        printf("symbolization failed: Win32 error %lu\n", GetLastError());
        goto Cleanup;
    }
    if (!RperfQuerySymbolProviderSummary(Provider, &Summary))
    {
        printf("symbol summary failed: Win32 error %lu\n", GetLastError());
        goto Cleanup;
    }
    RperfInitializeFilter(&Filter);
    Analysis = RperfAnalysisBuild(Symbolized, &Filter, NULL);
    if (Analysis == NULL)
    {
        printf("analysis build failed: Win32 error %lu\n", GetLastError());
        goto Cleanup;
    }
    printf("analysis format=%u samples=%I64u weight=%I64u functions=%Iu "
           "topdown=%Iu bottomup=%Iu edges=%Iu symbols=%Iu\n",
           (unsigned)Format,
           Analysis->TotalSamples,
           Analysis->TotalWeight,
           Analysis->FunctionCount,
           Analysis->TopDownCount,
           Analysis->BottomUpCount,
           Analysis->EdgeCount,
           Symbolized->SymbolCount);
    printf("symbolization attempted=%I64u pdb=%I64u rsym=%I64u "
           "dwarf=%I64u coff=%I64u export=%I64u offset=%I64u "
           "missing=%I64u mismatch=%I64u symbols_missing=%I64u "
           "load_error=%I64u\n",
           Summary.Attempted,
           Summary.Pdb,
           Summary.RosSym,
           Summary.Dwarf,
           Summary.Coff,
           Summary.Export,
           Summary.ModuleOffset,
           Summary.ImageMissing,
           Summary.IdentityMismatch,
           Summary.SymbolsMissing,
           Summary.LoadErrors);
    Result = 0;

Cleanup:
    if (Analysis != NULL)
        RperfAnalysisRelease(Analysis);
    if (Symbolized != NULL)
        RperfRecordingRelease(Symbolized);
    if (Provider != NULL)
        RperfDestroySymbolProvider(Provider);
    if (Recording != NULL)
        RperfRecordingRelease(Recording);
    return Result;
}

int
wmain(int argc,
      WCHAR **argv)
{
    WCHAR TemporaryDirectory[MAX_PATH];
    WCHAR CompletePath[MAX_PATH];
    WCHAR IncompletePath[MAX_PATH];
    WCHAR MalformedPath[MAX_PATH];
    WCHAR LivePath[MAX_PATH];
    DWORD Length;
    ULONG Failed = 0;

    if (argc == 4 && lstrcmpW(argv[1], L"--workload") == 0)
        return RperfRunWorkload(argv[2], argv[3]);
    if (argc == 5 && lstrcmpW(argv[1], L"--named-workload") == 0)
        return RperfRunNamedWorkload(argv[2], argv[3], argv[4]);
    if (argc == 3 &&
        (lstrcmpW(argv[1], L"--inspect-log") == 0 ||
         lstrcmpW(argv[1], L"--inspect-log-modules") == 0))
    {
        return RperfInspectSavedLog(
            argv[2],
            lstrcmpW(argv[1], L"--inspect-log-modules") == 0);
    }
    if ((argc == 4 || argc == 5) &&
        lstrcmpW(argv[1], L"--analyze-log") == 0)
    {
        return RperfAnalyzeSavedLog(argv[2], argv[3],
                                    argc == 5 ? argv[4] : NULL);
    }
    if (argc == 5 &&
        (lstrcmpW(argv[1], L"--pdb-smoke") == 0 ||
         lstrcmpW(argv[1], L"--pdb-mismatch-smoke") == 0 ||
         lstrcmpW(argv[1], L"--pdb-invalid-smoke") == 0))
    {
        WCHAR *End;
        ULONGLONG RelativeAddress;
        RPERF_PDB_EXPECTATION Expectation = RperfPdbExpectedMatch;

        errno = 0;
        RelativeAddress = _wcstoui64(argv[4], &End, 0);
        if (errno == ERANGE || End == argv[4] || *End != UNICODE_NULL)
        {
            fputs("invalid PDB fixture relative address\n", stderr);
            return 2;
        }
        if (lstrcmpW(argv[1], L"--pdb-mismatch-smoke") == 0)
            Expectation = RperfPdbExpectedIdentityMismatch;
        else if (lstrcmpW(argv[1], L"--pdb-invalid-smoke") == 0)
            Expectation = RperfPdbExpectedSymbolsMissing;
        return RperfTestPdbFixture(argv[2], argv[3], RelativeAddress,
                                   Expectation) ? 0 : 1;
    }

    CompletePath[0] = UNICODE_NULL;
    IncompletePath[0] = UNICODE_NULL;
    MalformedPath[0] = UNICODE_NULL;
    LivePath[0] = UNICODE_NULL;

    Length = GetTempPathW(ARRAYSIZE(TemporaryDirectory), TemporaryDirectory);
    if (Length == 0 || Length >= ARRAYSIZE(TemporaryDirectory) ||
        GetTempFileNameW(TemporaryDirectory, L"rpf", 0, CompletePath) == 0 ||
        GetTempFileNameW(TemporaryDirectory, L"rpf", 0, IncompletePath) == 0 ||
        GetTempFileNameW(TemporaryDirectory, L"rpf", 0, MalformedPath) == 0 ||
        !RperfCreateTemporaryLogPath(TemporaryDirectory,
                                     LivePath,
                                     ARRAYSIZE(LivePath)))
    {
        printf("[FAIL] fixture setup: Win32 error %lu\n", GetLastError());
        Failed = 1;
        goto Cleanup;
    }

    if (!RperfTestRecordingCloneLimits())
        Failed++;
    if (!RperfTestCompleteLog(CompletePath))
        Failed++;
    if (!RperfTestIncompleteLog(IncompletePath))
        Failed++;
    if (!RperfTestMalformedLogs(MalformedPath))
        Failed++;
    if (!RperfTestSchedulerStateFilter(MalformedPath))
        Failed++;
    if (!RperfTestDescendingSymbols(MalformedPath))
        Failed++;
    if (!RperfTestRecorderContract(MalformedPath))
        Failed++;
    if (!RperfTestStreamedRecorderCapture(MalformedPath))
        Failed++;
    if (!RperfTestAsyncPresentationController(MalformedPath))
        Failed++;
    if (!RperfTestRosProfImageRecords())
        Failed++;
    if (!RperfTestRosProfSequenceOrdering())
        Failed++;
    if (!RperfTestEtwLifecycleRecords())
        Failed++;
    if (!RperfTestEtwImageRecords())
        Failed++;
    if (!RperfTestOfflineSymbols())
        Failed++;
    if (!RperfTestLiveCapture(LivePath))
        Failed++;

Cleanup:
    if (CompletePath[0] != UNICODE_NULL)
        DeleteFileW(CompletePath);
    if (IncompletePath[0] != UNICODE_NULL)
        DeleteFileW(IncompletePath);
    if (MalformedPath[0] != UNICODE_NULL)
        DeleteFileW(MalformedPath);
    if (LivePath[0] != UNICODE_NULL)
        DeleteFileW(LivePath);

    if (Failed != 0)
    {
        printf("rosprofiler self-test: %lu test group(s) failed\n", Failed);
        return 1;
    }

    printf("rosprofiler self-test: all checks passed\n");
    return 0;
}
