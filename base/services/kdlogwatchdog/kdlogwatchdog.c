/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     User-mode heartbeat for the kernel debug-log watchdog
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winsvc.h>
#include <debug.h>

#define KD_LOG_WATCHDOG_HEARTBEAT_MS 4000

static const WCHAR ServiceName[] = L"KdLogWatchdog";
static SERVICE_STATUS_HANDLE ServiceStatusHandle;
static SERVICE_STATUS ServiceStatus;
static HANDLE StopEvent;

static VOID UpdateServiceStatus(_In_ DWORD State)
{
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ServiceStatus.dwCurrentState = State;
    ServiceStatus.dwControlsAccepted = State == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
    ServiceStatus.dwWin32ExitCode = NO_ERROR;
    ServiceStatus.dwServiceSpecificExitCode = 0;
    ServiceStatus.dwCheckPoint = 0;
    ServiceStatus.dwWaitHint = State == SERVICE_STOP_PENDING ? KD_LOG_WATCHDOG_HEARTBEAT_MS : 0;
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

    UpdateServiceStatus(SERVICE_RUNNING);
    DPRINT1("KDLOGWD: heartbeat service online, interval %lu ms\n", KD_LOG_WATCHDOG_HEARTBEAT_MS);
    for (;;)
    {
        WaitStatus = WaitForSingleObject(StopEvent, KD_LOG_WATCHDOG_HEARTBEAT_MS);
        if (WaitStatus != WAIT_TIMEOUT)
            break;
        DPRINT1("KDLOGWD: heartbeat\n");
    }

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
