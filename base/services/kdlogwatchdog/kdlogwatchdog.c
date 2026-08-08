/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     User-mode heartbeat for the kernel debug-log watchdog
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#include <stdarg.h>
#include <wchar.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winsvc.h>
#include <debug.h>

#ifndef KD_LOG_WATCHDOG_DEFAULT_SECONDS
#define KD_LOG_WATCHDOG_DEFAULT_SECONDS 0
#endif
#define KD_LOG_WATCHDOG_MINIMUM_SECONDS 5
#define KD_LOG_WATCHDOG_MAXIMUM_SECONDS 3600
#define KD_LOG_WATCHDOG_HEARTBEAT_MARGIN_SECONDS 2
#define KD_LOG_WATCHDOG_MILLISECONDS_PER_SECOND 1000
#define KD_LOG_WATCHDOG_LOAD_OPTIONS_LENGTH 1024
#define KD_LOG_WATCHDOG_STOP_WAIT_HINT_MS 1000

static const WCHAR ServiceName[] = L"KdLogWatchdog";
static SERVICE_STATUS_HANDLE ServiceStatusHandle;
static SERVICE_STATUS ServiceStatus;
static HANDLE StopEvent;
static DWORD HeartbeatMilliseconds;

static BOOL KdLogWatchdogIsSeparator(_In_ WCHAR Character)
{
    return Character == L' ' || Character == L'\t' || Character == L',' || Character == L'/';
}

static BOOL KdLogWatchdogHasDebugOption(_In_opt_ PCWSTR LoadOptions)
{
    static const WCHAR OptionName[] = L"DEBUG";
    PCWSTR Option;
    PCWSTR Cursor;

    if (LoadOptions == NULL)
        return FALSE;

    Option = LoadOptions;
    while ((Option = wcsstr(Option, OptionName)) != NULL)
    {
        Cursor = Option + ARRAYSIZE(OptionName) - 1;
        if ((Option == LoadOptions || KdLogWatchdogIsSeparator(Option[-1])) && (*Cursor == UNICODE_NULL || *Cursor == L'=' || KdLogWatchdogIsSeparator(*Cursor)))
            return TRUE;
        Option = Cursor;
    }

    return FALSE;
}

static DWORD KdLogWatchdogParseTimeout(_In_opt_ PCWSTR LoadOptions)
{
    static const WCHAR OptionName[] = L"KDWATCHDOG";
    PCWSTR Option;
    PCWSTR Cursor;
    DWORD Seconds;

    if (!KdLogWatchdogHasDebugOption(LoadOptions))
        return 0;

    Option = LoadOptions;
    while ((Option = wcsstr(Option, OptionName)) != NULL)
    {
        if (Option != LoadOptions && !KdLogWatchdogIsSeparator(Option[-1]))
        {
            Option += ARRAYSIZE(OptionName) - 1;
            continue;
        }

        Cursor = Option + ARRAYSIZE(OptionName) - 1;
        if (*Cursor == UNICODE_NULL || KdLogWatchdogIsSeparator(*Cursor))
            return KD_LOG_WATCHDOG_DEFAULT_SECONDS;
        if (*Cursor != L'=')
        {
            Option = Cursor;
            continue;
        }
        Cursor++;

        Seconds = 0;
        if (*Cursor < L'0' || *Cursor > L'9')
            return KD_LOG_WATCHDOG_DEFAULT_SECONDS;
        while (*Cursor >= L'0' && *Cursor <= L'9')
        {
            if (Seconds > (KD_LOG_WATCHDOG_MAXIMUM_SECONDS / 10))
                return KD_LOG_WATCHDOG_MAXIMUM_SECONDS;
            Seconds = (Seconds * 10) + (*Cursor++ - L'0');
        }
        if (*Cursor != UNICODE_NULL && !KdLogWatchdogIsSeparator(*Cursor))
            return KD_LOG_WATCHDOG_DEFAULT_SECONDS;
        if (Seconds == 0)
            return 0;
        if (Seconds < KD_LOG_WATCHDOG_MINIMUM_SECONDS)
            return KD_LOG_WATCHDOG_MINIMUM_SECONDS;
        if (Seconds > KD_LOG_WATCHDOG_MAXIMUM_SECONDS)
            return KD_LOG_WATCHDOG_MAXIMUM_SECONDS;
        return Seconds;
    }

    return KD_LOG_WATCHDOG_DEFAULT_SECONDS;
}

static DWORD KdLogWatchdogReadTimeout(VOID)
{
    HKEY ControlKey;
    WCHAR LoadOptions[KD_LOG_WATCHDOG_LOAD_OPTIONS_LENGTH];
    DWORD Type;
    DWORD Size;
    DWORD Terminator;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_QUERY_VALUE, &ControlKey);
    if (Error != ERROR_SUCCESS)
        return KD_LOG_WATCHDOG_DEFAULT_SECONDS;

    Type = 0;
    Size = sizeof(LoadOptions);
    Error = RegQueryValueExW(ControlKey, L"SystemStartOptions", NULL, &Type, (LPBYTE)LoadOptions, &Size);
    RegCloseKey(ControlKey);
    if (Error != ERROR_SUCCESS || Type != REG_SZ)
        return KD_LOG_WATCHDOG_DEFAULT_SECONDS;

    Terminator = min(Size / sizeof(LoadOptions[0]), ARRAYSIZE(LoadOptions) - 1);
    LoadOptions[Terminator] = UNICODE_NULL;
    return KdLogWatchdogParseTimeout(LoadOptions);
}

static VOID UpdateServiceStatus(_In_ DWORD State)
{
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ServiceStatus.dwCurrentState = State;
    ServiceStatus.dwControlsAccepted = State == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
    ServiceStatus.dwWin32ExitCode = NO_ERROR;
    ServiceStatus.dwServiceSpecificExitCode = 0;
    ServiceStatus.dwCheckPoint = 0;
    ServiceStatus.dwWaitHint = State == SERVICE_STOP_PENDING ? (HeartbeatMilliseconds != 0 ? HeartbeatMilliseconds : KD_LOG_WATCHDOG_STOP_WAIT_HINT_MS) : 0;
    SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
}

static DWORD WINAPI ServiceControlHandler(_In_ DWORD Control, _In_ DWORD EventType, _In_opt_ LPVOID EventData, _In_opt_ LPVOID Context)
{
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(EventData);
    UNREFERENCED_PARAMETER(Context);

    /*
     * SERVICE_CONTROL_SHUTDOWN is deliberately not accepted: the kernel
     * watchdog stays armed until PopGracefulShutdown, so the heartbeat must
     * keep running while user-mode shutdown drains or a quiet shutdown longer
     * than the timeout would bugcheck. An explicit stop is the supported way
     * to starve the watchdog on purpose.
     */
    if (Control == SERVICE_CONTROL_STOP)
    {
        UpdateServiceStatus(SERVICE_STOP_PENDING);
        SetEvent(StopEvent);
        return NO_ERROR;
    }
    if (Control == SERVICE_CONTROL_INTERROGATE)
    {
        SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static VOID WINAPI ServiceMain(_In_ DWORD ArgumentCount, _In_reads_(ArgumentCount) LPWSTR *Arguments)
{
    DWORD TimeoutSeconds;
    DWORD WaitStatus;

    UNREFERENCED_PARAMETER(ArgumentCount);
    UNREFERENCED_PARAMETER(Arguments);

    ServiceStatusHandle = RegisterServiceCtrlHandlerExW(ServiceName, ServiceControlHandler, NULL);
    if (ServiceStatusHandle == NULL)
        return;

    StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (StopEvent == NULL)
    {
        UpdateServiceStatus(SERVICE_STOPPED);
        return;
    }

    TimeoutSeconds = KdLogWatchdogReadTimeout();
    if (TimeoutSeconds == 0)
    {
        HeartbeatMilliseconds = 0;
        DPRINT1("KDLOGWD: heartbeat service disabled for this boot\n");
        goto Stop;
    }

    HeartbeatMilliseconds = (TimeoutSeconds - KD_LOG_WATCHDOG_HEARTBEAT_MARGIN_SECONDS) * KD_LOG_WATCHDOG_MILLISECONDS_PER_SECOND;
    UpdateServiceStatus(SERVICE_RUNNING);
    DPRINT1("KDLOGWD: heartbeat service online, timeout %lu seconds, interval %lu ms\n", TimeoutSeconds, HeartbeatMilliseconds);
    for (;;)
    {
        WaitStatus = WaitForSingleObject(StopEvent, HeartbeatMilliseconds);
        if (WaitStatus != WAIT_TIMEOUT)
            break;
        DPRINT1("KDLOGWD: heartbeat\n");
    }

Stop:
    CloseHandle(StopEvent);
    StopEvent = NULL;
    UpdateServiceStatus(SERVICE_STOPPED);
}

int wmain(_In_ int ArgumentCount, _In_reads_(ArgumentCount) WCHAR *Arguments[])
{
    SERVICE_TABLE_ENTRYW ServiceTable[] = {{(LPWSTR)ServiceName, ServiceMain}, {NULL, NULL}};

    UNREFERENCED_PARAMETER(ArgumentCount);
    UNREFERENCED_PARAMETER(Arguments);
    return StartServiceCtrlDispatcherW(ServiceTable) ? 0 : (int)GetLastError();
}
