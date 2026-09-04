/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * FILE:             base/services/wlansvc/wlansvc.c
 * PURPOSE:          WLAN Service
 * PROGRAMMER:       Christoph von Wittich
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ******************************************************************/

#define SERVICE_NAME L"WLAN Service"

SERVICE_STATUS_HANDLE ServiceStatusHandle;
SERVICE_STATUS SvcStatus;
static WCHAR ServiceName[] = L"WlanSvc";
static HANDLE ScanStopEvent;
static HANDLE ScanThread;

DWORD WINAPI RpcThreadRoutine(LPVOID lpParameter);

/* FUNCTIONS *****************************************************************/

static void UpdateServiceStatus(HANDLE hServiceStatus, DWORD NewStatus, DWORD Increment)
{
    if (Increment > 0)
        SvcStatus.dwCheckPoint += Increment;
    else
        SvcStatus.dwCheckPoint = 0;

    SvcStatus.dwCurrentState = NewStatus;
    SetServiceStatus(hServiceStatus, &SvcStatus);
}

static DWORD WINAPI
ServiceControlHandler(DWORD dwControl,
                      DWORD dwEventType,
                      LPVOID lpEventData,
                      LPVOID lpContext)
{
    switch (dwControl)
    {
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_STOP:
            UpdateServiceStatus(ServiceStatusHandle, SERVICE_STOP_PENDING, 1);
            if (ScanStopEvent)
                SetEvent(ScanStopEvent);
            if (ScanThread)
            {
                WaitForSingleObject(ScanThread, INFINITE);
                CloseHandle(ScanThread);
                ScanThread = NULL;
            }
            if (ScanStopEvent)
            {
                CloseHandle(ScanStopEvent);
                ScanStopEvent = NULL;
            }
            RpcMgmtStopServerListening(NULL);
            WlanSvcCleanup();
            UpdateServiceStatus(ServiceStatusHandle, SERVICE_STOPPED, 0);
            break;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
    return NO_ERROR;
}

static DWORD WINAPI
ScanThreadRoutine(LPVOID lpParameter)
{
    PLIST_ENTRY entry;
    PWLANSVC_INTERFACE iface;

    UNREFERENCED_PARAMETER(lpParameter);

    if (WaitForSingleObject(ScanStopEvent, 5000) != WAIT_TIMEOUT)
        return 0;

    do
    {
        EnterCriticalSection(&WlanSvcLock);
        WlanSvcRefreshInterfaces();
        for (entry = WlanSvcInterfaceListHead.Flink;
             entry != &WlanSvcInterfaceListHead;
             entry = entry->Flink)
        {
            iface = CONTAINING_RECORD(entry, WLANSVC_INTERFACE, ListEntry);
            if (!iface->Connected)
                WlanSvcDoScan(iface, NULL);
        }
        LeaveCriticalSection(&WlanSvcLock);
    } while (WaitForSingleObject(ScanStopEvent, 60000) == WAIT_TIMEOUT);

    return 0;
}

static VOID CALLBACK
ServiceMain(DWORD argc, LPWSTR *argv)
{
    HANDLE hThread;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    DPRINT("ServiceMain() called\n");

    SvcStatus.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    SvcStatus.dwCurrentState            = SERVICE_START_PENDING;
    SvcStatus.dwControlsAccepted        = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SvcStatus.dwCheckPoint              = 0;
    SvcStatus.dwWin32ExitCode           = NO_ERROR;
    SvcStatus.dwServiceSpecificExitCode = 0;
    SvcStatus.dwWaitHint                = 4000;

    ServiceStatusHandle = RegisterServiceCtrlHandlerExW(ServiceName,
                                                        ServiceControlHandler,
                                                        NULL);

    UpdateServiceStatus(ServiceStatusHandle, SERVICE_RUNNING, 0);

    hThread = CreateThread(NULL,
                           0,
                           (LPTHREAD_START_ROUTINE)
                           RpcThreadRoutine,
                           NULL,
                           0,
                           NULL);

    if (!hThread)
    {
        DPRINT("Can't create RpcThread\n");
        UpdateServiceStatus(ServiceStatusHandle, SERVICE_STOPPED, 0);
        return;
    }
    else
    {
        CloseHandle(hThread);
    }

    ScanStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ScanStopEvent)
    {
        ScanThread = CreateThread(NULL, 0, ScanThreadRoutine, NULL, 0, NULL);
        if (!ScanThread)
        {
            CloseHandle(ScanStopEvent);
            ScanStopEvent = NULL;
        }
    }

    DPRINT("ServiceMain() done\n");
}

int
wmain(int argc, WCHAR *argv[])
{
    SERVICE_TABLE_ENTRYW ServiceTable[2] =
    {
        {ServiceName, ServiceMain},
        {NULL, NULL}
    };

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    DPRINT("wlansvc: main() started\n");

    StartServiceCtrlDispatcherW(ServiceTable);

    DPRINT("wlansvc: main() done\n");

    ExitThread(0);

    return 0;
}

/* EOF */
