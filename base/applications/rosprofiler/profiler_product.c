/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Production preflight, settings, and accessibility helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "profiler_product.h"

#include <stdio.h>

#define RPERF_SETTINGS_KEY L"Software\\ReactOS\\RosProfiler"
#define RPERF_SETTINGS_VERSION 1

static ULONGLONG
RperfSaturatingMultiply(ULONGLONG Left,
                        ULONGLONG Right)
{
    if (Left != 0 && Right > (ULONGLONG)-1 / Left)
        return (ULONGLONG)-1;
    return Left * Right;
}

static BOOL
RperfPreflightTarget(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                     RPERF_PREFLIGHT_REPORT *Report)
{
    HANDLE Process;
    FILETIME Exit, Kernel, User;
    DWORD Length = ARRAYSIZE(Report->TargetImage);

    if (Configuration->Scope == RperfScopeSystem)
    {
        lstrcpyW(Report->TargetImage, L"<system-wide>");
        return TRUE;
    }
    Process = OpenProcess(PROCESS_QUERY_INFORMATION,
                          FALSE,
                          Configuration->ProcessId);
    if (Process == NULL)
        return FALSE;
    if (!GetProcessTimes(Process,
                         &Report->TargetCreationTime,
                         &Exit, &Kernel, &User))
    {
        CloseHandle(Process);
        return FALSE;
    }
    if (!QueryFullProcessImageNameW(Process, 0,
                                    Report->TargetImage, &Length))
    {
        lstrcpynW(Report->TargetImage,
                  Configuration->TargetName != NULL ?
                  Configuration->TargetName : L"<unknown>",
                  ARRAYSIZE(Report->TargetImage));
    }
    CloseHandle(Process);
    return TRUE;
}

static VOID
RperfPreflightSpace(PCWSTR OutputPath,
                    RPERF_PREFLIGHT_REPORT *Report)
{
    WCHAR Root[MAX_PATH];
    ULARGE_INTEGER Available;
    PCWSTR Source = OutputPath;

    if (Source == NULL || Source[0] == UNICODE_NULL)
        return;
    if (Source[0] != UNICODE_NULL && Source[1] == L':')
    {
        Root[0] = Source[0];
        Root[1] = L':';
        Root[2] = L'\\';
        Root[3] = UNICODE_NULL;
    }
    else if (Source[0] == L'\\' && Source[1] == L'\\')
    {
        PCWSTR Cursor = Source + 2;
        ULONG Separators = 0;
        SIZE_T Length = 2;
        while (*Cursor != UNICODE_NULL && Separators < 2 &&
               Length + 1 < ARRAYSIZE(Root))
        {
            Root[Length++] = *Cursor;
            if (*Cursor++ == L'\\')
                Separators++;
        }
        Root[Length] = UNICODE_NULL;
    }
    else
    {
        lstrcpyW(Root, L".");
    }
    if (GetDiskFreeSpaceExW(Root, &Available, NULL, NULL))
        Report->AvailableBytes = Available.QuadPart;
}

BOOL
RperfRunPreflight(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                  RPERF_PREFLIGHT_REPORT *Report)
{
    ULONGLONG RecordBytes, SamplesPerSecond;
    DWORD Error;

    if (Configuration == NULL || Report == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(Report, sizeof(*Report));
    if (!RperfRecorderQueryCapabilities(Configuration->Backend,
                                        &Report->Capabilities) ||
        !Report->Capabilities.Available)
    {
        Report->Status = Report->Capabilities.Status != ERROR_SUCCESS ?
                         Report->Capabilities.Status : GetLastError();
        _snwprintf(Report->Summary, ARRAYSIZE(Report->Summary),
                   L"The requested recorder is unavailable: %s",
                   Report->Capabilities.Description);
        return TRUE;
    }
    if (!RperfRecorderValidateConfiguration(Configuration,
                                             &Report->Capabilities))
    {
        Report->Status = GetLastError();
        lstrcpyW(Report->Summary,
                 L"The requested scope, stack, event, or interval is not "
                 L"supported by this recorder.");
        return TRUE;
    }
    if (!RperfPreflightTarget(Configuration, Report))
    {
        Report->Status = GetLastError();
        lstrcpyW(Report->Summary,
                 L"The selected target no longer exists or cannot be queried.");
        return TRUE;
    }
    if (Configuration->Backend == RperfBackendIntrusive)
        Report->Warnings |= RPERF_PREFLIGHT_INTRUSIVE |
                            RPERF_PREFLIGHT_UNKNOWN_RATE;
    if (Report->Capabilities.RequiresPrivilege)
        Report->Warnings |= RPERF_PREFLIGHT_PRIVILEGE;
    RecordBytes = 128 +
        RperfSaturatingMultiply(Configuration->Limits.MaxFrames,
                                sizeof(ULONGLONG));
    SamplesPerSecond = 1000000 / Configuration->IntervalUs;
    if (SamplesPerSecond == 0)
        SamplesPerSecond = 1;
    Report->EstimatedBytesPerSecond =
        RperfSaturatingMultiply(RecordBytes, SamplesPerSecond);
    if (Configuration->DurationMs != 0)
    {
        Report->EstimatedTotalBytes =
            RperfSaturatingMultiply(Report->EstimatedBytesPerSecond,
                                    Configuration->DurationMs) / 1000;
    }
    RperfPreflightSpace(Configuration->OutputPath, Report);
    if (Report->EstimatedTotalBytes != 0 && Report->AvailableBytes != 0 &&
        Report->EstimatedTotalBytes > Report->AvailableBytes * 9 / 10)
        Report->Warnings |= RPERF_PREFLIGHT_LOW_SPACE;
    Error = ERROR_SUCCESS;
    if (Report->Warnings & RPERF_PREFLIGHT_LOW_SPACE)
        Error = ERROR_DISK_FULL;
    Report->Status = Error;
    Report->Ready = Error == ERROR_SUCCESS;
    _snwprintf(Report->Summary, ARRAYSIZE(Report->Summary),
               L"Target: %s\r\nRecorder: %s\r\nEstimated rate: "
               L"%I64u bytes/s\r\nAvailable space: %I64u bytes%s",
               Report->TargetImage,
               Report->Capabilities.Description,
               Report->EstimatedBytesPerSecond,
               Report->AvailableBytes,
               (Report->Warnings & RPERF_PREFLIGHT_INTRUSIVE) ?
               L"\r\nWarning: this recorder suspends target threads." : L"");
    Report->Summary[ARRAYSIZE(Report->Summary) - 1] = UNICODE_NULL;
    return TRUE;
}

VOID
RperfInitializeSafeSettings(RPERF_SAFE_SETTINGS *Settings)
{
    if (Settings == NULL)
        return;
    ZeroMemory(Settings, sizeof(*Settings));
    Settings->Version = RPERF_SETTINGS_VERSION;
    Settings->Backend = RperfRecorderPreferredBackend();
    Settings->Scope = RperfScopeProcess;
    Settings->IntervalUs = 10000;
    Settings->MaximumFrames = 64;
    Settings->IncludeUser = TRUE;
}

static BOOL
RperfReadDword(HKEY Key,
               PCWSTR Name,
               PULONG Value)
{
    DWORD Type, Bytes = sizeof(*Value);
    return RegQueryValueExW(Key, Name, NULL, &Type,
                            (PBYTE)Value, &Bytes) == ERROR_SUCCESS &&
           Type == REG_DWORD && Bytes == sizeof(*Value);
}

BOOL
RperfLoadSafeSettings(RPERF_SAFE_SETTINGS *Settings)
{
    HKEY Key;
    DWORD Type, Bytes;
    ULONG Value;
    if (Settings == NULL)
        return FALSE;
    RperfInitializeSafeSettings(Settings);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RPERF_SETTINGS_KEY,
                      0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return TRUE;
    if (RperfReadDword(Key, L"Version", &Value) &&
        Value == RPERF_SETTINGS_VERSION)
    {
        if (RperfReadDword(Key, L"Backend", &Value))
            Settings->Backend = (RPERF_BACKEND_KIND)Value;
        if (RperfReadDword(Key, L"Scope", &Value))
            Settings->Scope = (RPERF_CAPTURE_SCOPE)Value;
        RperfReadDword(Key, L"IntervalUs", &Settings->IntervalUs);
        RperfReadDword(Key, L"DurationMs", &Settings->DurationMs);
        RperfReadDword(Key, L"MaximumFrames", &Settings->MaximumFrames);
        if (RperfReadDword(Key, L"IncludeUser", &Value))
            Settings->IncludeUser = Value != 0;
        if (RperfReadDword(Key, L"IncludeKernel", &Value))
            Settings->IncludeKernel = Value != 0;
        if (RperfReadDword(Key, L"FollowChildren", &Value))
            Settings->FollowChildren = Value != 0;
        Bytes = sizeof(Settings->RecentFile);
        if (RegQueryValueExW(Key, L"RecentFile", NULL, &Type,
                            (PBYTE)Settings->RecentFile, &Bytes) !=
                ERROR_SUCCESS ||
            (Type != REG_SZ && Type != REG_EXPAND_SZ) ||
            Bytes < sizeof(WCHAR))
            Settings->RecentFile[0] = UNICODE_NULL;
        Settings->RecentFile[ARRAYSIZE(Settings->RecentFile) - 1] = UNICODE_NULL;
    }
    RegCloseKey(Key);
    return TRUE;
}

static BOOL
RperfWriteDword(HKEY Key,
                PCWSTR Name,
                ULONG Value)
{
    return RegSetValueExW(Key, Name, 0, REG_DWORD,
                          (const BYTE *)&Value,
                          sizeof(Value)) == ERROR_SUCCESS;
}

BOOL
RperfSaveSafeSettings(const RPERF_SAFE_SETTINGS *Settings)
{
    HKEY Key;
    DWORD Disposition;
    SIZE_T RecentBytes;
    BOOL Result;
    if (Settings == NULL || Settings->Version != RPERF_SETTINGS_VERSION)
        return FALSE;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RPERF_SETTINGS_KEY,
                        0, NULL, 0, KEY_SET_VALUE, NULL,
                        &Key, &Disposition) != ERROR_SUCCESS)
        return FALSE;
    UNREFERENCED_PARAMETER(Disposition);
    Result = RperfWriteDword(Key, L"Version", Settings->Version) &&
             RperfWriteDword(Key, L"Backend", Settings->Backend) &&
             RperfWriteDword(Key, L"Scope", Settings->Scope) &&
             RperfWriteDword(Key, L"IntervalUs", Settings->IntervalUs) &&
             RperfWriteDword(Key, L"DurationMs", Settings->DurationMs) &&
             RperfWriteDword(Key, L"MaximumFrames", Settings->MaximumFrames) &&
             RperfWriteDword(Key, L"IncludeUser", Settings->IncludeUser) &&
             RperfWriteDword(Key, L"IncludeKernel", Settings->IncludeKernel) &&
             RperfWriteDword(Key, L"FollowChildren", Settings->FollowChildren);
    RecentBytes = (wcslen(Settings->RecentFile) + 1) * sizeof(WCHAR);
    if (Result)
        Result = RegSetValueExW(Key, L"RecentFile", 0, REG_SZ,
                               (const BYTE *)Settings->RecentFile,
                               (DWORD)RecentBytes) == ERROR_SUCCESS;
    RegCloseKey(Key);
    return Result;
}

BOOL
RperfClearSettingsHistory(VOID)
{
    LONG Status = RegDeleteKeyW(HKEY_CURRENT_USER, RPERF_SETTINGS_KEY);
    return Status == ERROR_SUCCESS || Status == ERROR_FILE_NOT_FOUND;
}

VOID
RperfAccessibilityNameWindow(HWND Window,
                             PCWSTR Name)
{
    if (Window == NULL || Name == NULL)
        return;
    SetWindowTextW(Window, Name);
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, Window, OBJID_WINDOW, CHILDID_SELF);
}

VOID
RperfAccessibilityNotifyViewChanged(HWND Window)
{
    if (Window != NULL)
        NotifyWinEvent(EVENT_OBJECT_REORDER, Window, OBJID_CLIENT, CHILDID_SELF);
}

COLORREF
RperfAccessibleMetricColor(ULONG Index,
                           BOOL HighContrast)
{
    static const COLORREF Palette[] =
    {
        RGB(0, 114, 178), RGB(213, 94, 0), RGB(0, 158, 115),
        RGB(204, 121, 167), RGB(230, 159, 0), RGB(86, 180, 233)
    };
    if (HighContrast)
        return GetSysColor(Index & 1 ? COLOR_HIGHLIGHT : COLOR_WINDOWTEXT);
    return Palette[Index % ARRAYSIZE(Palette)];
}

INT
RperfScaleForDpi(INT Value,
                 UINT Dpi)
{
    return MulDiv(Value, Dpi != 0 ? Dpi : 96, 96);
}
