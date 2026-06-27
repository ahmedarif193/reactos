/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Winlogon
 * FILE:            base/system/winlogon/setup.c
 * PURPOSE:         Setup support functions
 * PROGRAMMERS:     Eric Kohl
 */

/* INCLUDES *****************************************************************/

#include "winlogon.h"

/* FUNCTIONS ****************************************************************/

DWORD
GetSetupType(VOID)
{
    DWORD dwError;
    HKEY hKey;
    DWORD dwType;
    DWORD dwSize;
    DWORD dwSetupType;

    TRACE("GetSetupType()\n");

    /* Open key */
    dwError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\Setup",
                            0,
                            KEY_QUERY_VALUE,
                            &hKey);
    if (dwError != ERROR_SUCCESS)
        return 0;

    /* Read key */
    dwSize = sizeof(DWORD);
    dwError = RegQueryValueExW(hKey,
                               L"SetupType",
                               NULL,
                               &dwType,
                               (LPBYTE)&dwSetupType,
                               &dwSize);

    /* Close key, and check if returned values are correct */
    RegCloseKey(hKey);
    if (dwError != ERROR_SUCCESS || dwType != REG_DWORD || dwSize != sizeof(DWORD))
        return 0;

    TRACE("GetSetupType() returns %lu\n", dwSetupType);
    return dwSetupType;
}


static
DWORD
WINAPI
RunSetupThreadProc(
    IN LPVOID lpParameter)
{
    PROCESS_INFORMATION ProcessInformation;
    STARTUPINFOW StartupInfo;
    WCHAR Shell[MAX_PATH];
    WCHAR CommandLine[MAX_PATH];
    BOOL Result;
    DWORD dwError;
    HKEY hKey;
    DWORD dwType;
    DWORD dwSize;
    DWORD dwExitCode;

    TRACE("RunSetup() called\n");
    DbgPrint("WL: RunSetup thread start\n");

    /* Open key */
    dwError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\Setup",
                            0,
                            KEY_QUERY_VALUE,
                            &hKey);
    if (dwError != ERROR_SUCCESS)
    {
        DbgPrint("WL: failed to open setup key, error %lu\n", dwError);
        return FALSE;
    }

    /* Read key */
    dwSize = sizeof(Shell);
    dwError = RegQueryValueExW(hKey,
                               L"CmdLine",
                               NULL,
                               &dwType,
                               (LPBYTE)Shell,
                               &dwSize);
    RegCloseKey(hKey);
    if (dwError != ERROR_SUCCESS)
    {
        DbgPrint("WL: failed to read setup command line, error %lu\n", dwError);
        return FALSE;
    }

    /* Finish string */
    Shell[dwSize / sizeof(WCHAR)] = UNICODE_NULL;

    /* Expand string (if applicable) */
    if (dwType == REG_EXPAND_SZ)
        ExpandEnvironmentStringsW(Shell, CommandLine, ARRAYSIZE(CommandLine));
    else if (dwType == REG_SZ)
        wcscpy(CommandLine, Shell);
    else
    {
        DbgPrint("WL: unsupported setup command line type %lu\n", dwType);
        return FALSE;
    }

    TRACE("Should run '%s' now\n", debugstr_w(CommandLine));
    DbgPrint("WL: setup command line '%S'\n", CommandLine);

    SwitchDesktop(WLSession->ApplicationDesktop);

    /* Start process */
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.lpReserved = NULL;
    StartupInfo.lpDesktop = L"WinSta0\\Default";
    StartupInfo.lpTitle = NULL;
    StartupInfo.dwFlags = 0;
    StartupInfo.cbReserved2 = 0;
    StartupInfo.lpReserved2 = 0;

    Result = CreateProcessW(NULL,
                            CommandLine,
                            NULL,
                            NULL,
                            FALSE,
                            DETACHED_PROCESS,
                            NULL,
                            NULL,
                            &StartupInfo,
                            &ProcessInformation);
    if (!Result)
    {
        DbgPrint("WL: failed to run setup process, error %lu\n", GetLastError());
        SwitchDesktop(WLSession->WinlogonDesktop);
        return FALSE;
    }
    DbgPrint("WL: setup process started pid=%lu thread=%lu\n",
            ProcessInformation.dwProcessId,
            ProcessInformation.dwThreadId);

    /* Wait for process termination */
    WaitForSingleObject(ProcessInformation.hProcess, INFINITE);

    GetExitCodeProcess(ProcessInformation.hProcess, &dwExitCode);
    DbgPrint("WL: setup process exited code=0x%08lx\n", dwExitCode);

    /* Close handles */
    CloseHandle(ProcessInformation.hThread);
    CloseHandle(ProcessInformation.hProcess);

    // SwitchDesktop(WLSession->WinlogonDesktop);

    TRACE ("RunSetup() done\n");

    return TRUE;
}


BOOL
RunSetup(VOID)
{
    HANDLE hThread;

    hThread = CreateThread(NULL,
                           0,
                           RunSetupThreadProc,
                           NULL,
                           0,
                           NULL);
    if (hThread == NULL)
        DbgPrint("WL: CreateThread for setup failed, error %lu\n", GetLastError());
    if (hThread != NULL)
    {
        DbgPrint("WL: setup thread created handle=%p\n", hThread);
        CloseHandle(hThread);
    }

    return hThread != NULL;
}

/* EOF */
