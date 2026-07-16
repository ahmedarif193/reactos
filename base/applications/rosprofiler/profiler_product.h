/* Production preflight, safe settings, and accessibility helpers. */
#pragma once

#include "profiler_recorder.h"

#define RPERF_PREFLIGHT_INTRUSIVE       0x00000001
#define RPERF_PREFLIGHT_PRIVILEGE       0x00000002
#define RPERF_PREFLIGHT_LOW_SPACE       0x00000004
#define RPERF_PREFLIGHT_UNKNOWN_RATE    0x00000008
#define RPERF_PREFLIGHT_FALLBACK        0x00000010

typedef struct _RPERF_PREFLIGHT_REPORT
{
    BOOL Ready;
    DWORD Status;
    ULONG Warnings;
    RPERF_RECORDER_CAPABILITIES Capabilities;
    FILETIME TargetCreationTime;
    WCHAR TargetImage[1024];
    ULONGLONG EstimatedBytesPerSecond;
    ULONGLONG EstimatedTotalBytes;
    ULONGLONG AvailableBytes;
    WCHAR Summary[1024];
} RPERF_PREFLIGHT_REPORT;

typedef struct _RPERF_SAFE_SETTINGS
{
    ULONG Version;
    RPERF_BACKEND_KIND Backend;
    RPERF_CAPTURE_SCOPE Scope;
    ULONG IntervalUs;
    ULONG DurationMs;
    ULONG MaximumFrames;
    BOOL IncludeUser;
    BOOL IncludeKernel;
    BOOL FollowChildren;
    WCHAR RecentFile[1024];
} RPERF_SAFE_SETTINGS;

BOOL RperfRunPreflight(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                       RPERF_PREFLIGHT_REPORT *Report);
VOID RperfInitializeSafeSettings(RPERF_SAFE_SETTINGS *Settings);
BOOL RperfLoadSafeSettings(RPERF_SAFE_SETTINGS *Settings);
BOOL RperfSaveSafeSettings(const RPERF_SAFE_SETTINGS *Settings);
BOOL RperfClearSettingsHistory(VOID);
VOID RperfAccessibilityNameWindow(HWND Window, PCWSTR Name);
VOID RperfAccessibilityNotifyViewChanged(HWND Window);
COLORREF RperfAccessibleMetricColor(ULONG Index, BOOL HighContrast);
INT RperfScaleForDpi(INT Value, UINT Dpi);
