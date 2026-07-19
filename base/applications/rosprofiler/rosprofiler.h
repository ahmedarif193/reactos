/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared capture, analysis, and user-interface declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include "profiler_model.h"

#define RPERF_LOG_VERSION 1
#define RPERF_MAX_FRAMES 64
#define RPERF_MAX_SAMPLES 1000000
#define RPERF_MAX_SYMBOLS 262144
#define RPERF_MAX_NODES 2000000
#define RPERF_INVALID_NODE ((ULONG)-1)
#define RPERF_FLAME_CLASS L"RosProfilerFlameGraph"
#define RPERF_TIMELINE_CLASS L"RosProfilerTimeline"

#define WM_RPERF_CAPTURE_DONE (WM_APP + 1)
#define WM_RPERF_CAPTURE_PROGRESS (WM_APP + 2)
#define WM_RPERF_FLAME_HOVER (WM_APP + 3)
#define WM_RPERF_SET_SESSION (WM_APP + 4)
#define WM_RPERF_RESET_ZOOM (WM_APP + 5)
#define WM_RPERF_SET_SEARCH (WM_APP + 6)
#define WM_RPERF_ZOOM_ADDRESS (WM_APP + 7)
#define WM_RPERF_TIMELINE_SET_SESSION (WM_APP + 8)
#define WM_RPERF_TIMELINE_SET_RANGE (WM_APP + 9)
#define WM_RPERF_TIMELINE_RANGE_CHANGED (WM_APP + 10)

#define RPERF_SAMPLE_TRUNCATED 0x0001
#define RPERF_SAMPLE_UNWIND_FAILED 0x0002
#define RPERF_SAMPLE_STATE_KNOWN 0x0004
#define RPERF_SAMPLE_WAITING 0x0008
#define RPERF_SAMPLE_WAIT_REASON_SHIFT 8
#define RPERF_SAMPLE_WAIT_REASON_MASK 0xFF00
#define RPERF_SAMPLE_FLAGS_VALID (RPERF_SAMPLE_TRUNCATED | \
                                  RPERF_SAMPLE_UNWIND_FAILED | \
                                  RPERF_SAMPLE_STATE_KNOWN | \
                                  RPERF_SAMPLE_WAITING | \
                                  RPERF_SAMPLE_WAIT_REASON_MASK)

#define RPERF_FILTER_CPU_ONLY 0x00000001
#define RPERF_FILTER_VALID (RPERF_FILTER_CPU_ONLY)

typedef struct _RPERF_TIME_RANGE
{
    ULONGLONG StartUs;
    ULONGLONG EndUs;
} RPERF_TIME_RANGE;

typedef struct _RPERF_PROCESS_INFO
{
    DWORD ProcessId;
    ULONG ThreadCount;
    WCHAR Name[MAX_PATH];
} RPERF_PROCESS_INFO;

typedef struct _RPERF_SAMPLE
{
    ULONGLONG TimeUs;
    DWORD ProcessId;
    DWORD ThreadId;
    USHORT Depth;
    USHORT Flags;
    DWORD64 Frames[RPERF_MAX_FRAMES];
} RPERF_SAMPLE;

typedef struct _RPERF_SYMBOL
{
    DWORD64 Address;
    DWORD64 FunctionAddress;
    DWORD64 ModuleBase;
    DWORD64 Displacement;
    CHAR Module[64];
    CHAR Name[256];
    RPERF_SYMBOL_SOURCE_KIND Source;
    RPERF_SYMBOL_STATUS_KIND Status;
    ULONGLONG Inclusive;
    ULONGLONG Exclusive;
    SIZE_T AggregateIndex;
} RPERF_SYMBOL;

typedef struct _RPERF_CAPTURE_MODULE
{
    DWORD64 Base;
    DWORD64 Size;
    ULONG Architecture;
    ULONG Flags;
    ULONG TimeDateStamp;
    ULONG Checksum;
    UCHAR DebugId[16];
    ULONG DebugAge;
    WCHAR Path[MAX_PATH];
} RPERF_CAPTURE_MODULE;

typedef struct _RPERF_NODE
{
    DWORD64 Address;
    ULONGLONG Count;
    ULONG Parent;
    ULONG FirstChild;
    ULONG NextSibling;
    USHORT Depth;
} RPERF_NODE;

typedef struct _RPERF_SESSION
{
    DWORD ProcessId;
    WCHAR ProcessName[MAX_PATH];
    WCHAR SourcePath[MAX_PATH];
    RPERF_BACKEND_KIND Backend;
    DWORD IntervalMs;
    DWORD RequestedDurationMs;
    ULONGLONG ElapsedUs;
    ULONGLONG UserTime100ns;
    ULONGLONG KernelTime100ns;
    RPERF_CAPTURE_COUNTERS Counters;
    ULONGLONG MissedThreads;
    ULONGLONG MissedTicks;
    ULONGLONG TruncatedStacks;
    ULONGLONG LostSamples;
    ULONGLONG StateTaggedSamples;
    ULONGLONG WaitingSamples;
    ULONGLONG FilterStartUs;
    ULONGLONG FilterEndUs;
    DWORD FilterThreadId;
    DWORD FilterFlags;
    SIZE_T FilteredSampleCount;
    RPERF_COMPLETION_REASON CompletionReason;
    DWORD CrossBitnessTargetBits;
    BOOL LogComplete;

    RPERF_SAMPLE *Samples;
    SIZE_T SampleCount;
    SIZE_T SampleCapacity;
    RPERF_SYMBOL *Symbols;
    SIZE_T SymbolCount;
    SIZE_T SymbolCapacity;
    SIZE_T *SymbolHash;
    SIZE_T SymbolHashCapacity;
    BOOL SymbolsSorted;
    SIZE_T *FunctionSymbols;
    SIZE_T FunctionCount;
    BOOL FunctionIndexValid;
    RPERF_CAPTURE_MODULE *Modules;
    SIZE_T ModuleCount;
    SIZE_T ModuleCapacity;
    RPERF_NODE *Nodes;
    SIZE_T NodeCount;
    SIZE_T NodeCapacity;
    SIZE_T *NodeHash;
    SIZE_T NodeHashCapacity;

    HANDLE StopEvent;
    HANDLE WorkerThread;
    HWND NotifyWindow;
    volatile LONG Capturing;
    DWORD CaptureError;
} RPERF_SESSION;

typedef VOID (CALLBACK *RPERF_SESSION_PROGRESS)(PVOID Context,
                                                ULONGLONG Completed,
                                                ULONGLONG Total);

VOID RperfSessionInitialize(RPERF_SESSION *Session);
VOID RperfSessionClear(RPERF_SESSION *Session);
BOOL RperfEnumerateProcesses(RPERF_PROCESS_INFO **Processes,
                             SIZE_T *ProcessCount);
VOID RperfFreeProcesses(RPERF_PROCESS_INFO *Processes);
BOOL RperfCaptureStart(RPERF_SESSION *Session,
                       DWORD ProcessId,
                       PCWSTR ProcessName,
                       DWORD IntervalMs,
                       DWORD DurationMs,
                       PCWSTR LogPath,
                       HWND NotifyWindow);
VOID RperfCaptureStop(RPERF_SESSION *Session);
VOID RperfCaptureWait(RPERF_SESSION *Session);
BOOL RperfLoadLog(RPERF_SESSION *Session, PCWSTR LogPath);
BOOL RperfBuildAnalysis(RPERF_SESSION *Session);
BOOL RperfBuildFilteredAnalysis(RPERF_SESSION *Session,
                                DWORD ThreadId,
                                ULONGLONG StartUs,
                                ULONGLONG EndUs,
                                DWORD FilterFlags);
BOOL RperfBuildFilteredAnalysisEx(RPERF_SESSION *Session,
                                  DWORD ThreadId,
                                  ULONGLONG StartUs,
                                  ULONGLONG EndUs,
                                  DWORD FilterFlags,
                                  HANDLE CancelEvent,
                                  RPERF_SESSION_PROGRESS Progress,
                                  PVOID ProgressContext);
const RPERF_SYMBOL *RperfFindSymbol(const RPERF_SESSION *Session, DWORD64 Address);
VOID RperfFormatSymbol(const RPERF_SESSION *Session,
                       DWORD64 Address,
                       PWSTR Buffer,
                       SIZE_T BufferCount);

BOOL RperfRegisterFlameGraph(HINSTANCE Instance);
VOID RperfUnregisterFlameGraph(HINSTANCE Instance);
BOOL RperfRegisterTimeline(HINSTANCE Instance);
VOID RperfUnregisterTimeline(HINSTANCE Instance);
