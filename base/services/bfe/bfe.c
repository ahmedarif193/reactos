/*
 * PROJECT:     ReactOS Base Filtering Engine (BFE) stub
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * FILE:        base/services/bfe/bfe.c
 * PURPOSE:     Minimal BFE service. Registers under the "BFE" service name
 *              so that WFP-aware user-mode code (netsh advfirewall, etc.)
 *              finds the service and can query basic status. All filter /
 *              callout management operations are stubs.
 *
 *              Real BFE implements the Windows Filtering Platform RPC
 *              interface; this implementation just runs the SCM service
 *              control loop and accepts START/STOP, so dependent services
 *              can be expressed via ServiceDependOn="BFE" in their INFs.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors.
 */

#include <windows.h>
#include <winsvc.h>
#include <stdio.h>

#define BFE_SERVICE_NAMEW L"BFE"

static SERVICE_STATUS_HANDLE g_BfeStatusHandle = NULL;
static SERVICE_STATUS        g_BfeStatus;
static HANDLE                g_BfeStopEvent = NULL;

static VOID
BfeReportStatus(DWORD State, DWORD ExitCode, DWORD WaitHint)
{
    static DWORD checkPoint = 1;

    g_BfeStatus.dwCurrentState = State;
    g_BfeStatus.dwWin32ExitCode = ExitCode;
    g_BfeStatus.dwWaitHint = WaitHint;

    if (State == SERVICE_START_PENDING)
        g_BfeStatus.dwControlsAccepted = 0;
    else
        g_BfeStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if (State == SERVICE_RUNNING || State == SERVICE_STOPPED)
        g_BfeStatus.dwCheckPoint = 0;
    else
        g_BfeStatus.dwCheckPoint = checkPoint++;

    SetServiceStatus(g_BfeStatusHandle, &g_BfeStatus);
}

static DWORD WINAPI
BfeControlHandler(DWORD Control, DWORD EventType, LPVOID EventData, LPVOID Ctx)
{
    (void)EventType; (void)EventData; (void)Ctx;

    switch (Control)
    {
        case SERVICE_CONTROL_STOP:
            BfeReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 1000);
            if (g_BfeStopEvent) SetEvent(g_BfeStopEvent);
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static VOID WINAPI
BfeServiceMain(DWORD Argc, LPWSTR* Argv)
{
    (void)Argc; (void)Argv;

    g_BfeStatusHandle = RegisterServiceCtrlHandlerExW(
        BFE_SERVICE_NAMEW, BfeControlHandler, NULL);
    if (g_BfeStatusHandle == NULL)
        return;

    g_BfeStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_BfeStatus.dwServiceSpecificExitCode = 0;

    BfeReportStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    g_BfeStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_BfeStopEvent == NULL)
    {
        BfeReportStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    /* Real BFE would bring up its RPC endpoint and start the WFP filter
     * engine here. This stub just reports RUNNING and waits for stop. */
    BfeReportStatus(SERVICE_RUNNING, NO_ERROR, 0);

    WaitForSingleObject(g_BfeStopEvent, INFINITE);
    CloseHandle(g_BfeStopEvent);
    g_BfeStopEvent = NULL;

    BfeReportStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

int wmain(int argc, wchar_t* argv[])
{
    SERVICE_TABLE_ENTRYW dispatchTable[] = {
        { (LPWSTR)BFE_SERVICE_NAMEW, BfeServiceMain },
        { NULL, NULL }
    };

    (void)argc; (void)argv;

    if (!StartServiceCtrlDispatcherW(dispatchTable))
        return (int)GetLastError();

    return 0;
}

/* EOF */
