/*
 *  ReactOS kernel
 *  Copyright (C) 2005 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * FILE:             base/services/umpnpmgr/install.c
 * PURPOSE:          Device installer
 * PROGRAMMER:       Eric Kohl (eric.kohl@reactos.org)
 *                   Hervé Poussineau (hpoussin@reactos.org)
 *                   Colin Finck (colin@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#define NDEBUG
#include <debug.h>


/* GLOBALS ******************************************************************/

HANDLE hUserToken = NULL;
HANDLE hInstallEvent = NULL;
HANDLE hNoPendingInstalls = NULL;

/* Device-install event list */
HANDLE hDeviceInstallListMutex;
LIST_ENTRY DeviceInstallListHead;
HANDLE hDeviceInstallListNotEmpty;

DWORD
CreatePnpInstallEventSecurity(
    _Out_ PSECURITY_DESCRIPTOR *EventSd);

/* FUNCTIONS *****************************************************************/

static BOOL
InstallDevice(PCWSTR DeviceInstance, BOOL ShowWizard)
{
    BOOL DeviceInstalled = FALSE;
    DWORD BytesWritten;
    DWORD Value;
    DWORD ErrCode;
    HANDLE hInstallEvent;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    LPVOID Environment = NULL;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    UUID RandomUuid;
    HKEY DeviceKey;
    SECURITY_ATTRIBUTES EventAttrs;
    PSECURITY_DESCRIPTOR EventSd;

    /* The following lengths are constant (see below), they cannot overflow */
    WCHAR CommandLine[116];
    WCHAR InstallEventName[73];
    WCHAR PipeName[74];
    WCHAR UuidString[39];

    DPRINT("InstallDevice(%S, %d)\n", DeviceInstance, ShowWizard);

    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

    if (RegOpenKeyExW(hEnumKey,
                      DeviceInstance,
                      0,
                      KEY_QUERY_VALUE,
                      &DeviceKey) == ERROR_SUCCESS)
    {
        WCHAR ClassName[64] = {0};
        DWORD Type = REG_NONE;
        DWORD Size = sizeof(ClassName);

        if (RegQueryValueExW(DeviceKey,
                             L"Class",
                             NULL,
                             &Type,
                             (PBYTE)ClassName,
                             &Size) == ERROR_SUCCESS &&
            Type == REG_SZ)
        {
            /* Ensure NUL-termination even if the registry data isn't. */
            ClassName[(RTL_NUMBER_OF(ClassName) - 1)] = UNICODE_NULL;

            /*
             * Win7+: "Other devices" are still uninstalled. Do not treat
             * Class="Unknown" as installed, so we still try to match an INF.
             */
            if (_wcsicmp(ClassName, L"Unknown") != 0)
            {
                DPRINT("No need to install: %S\n", DeviceInstance);
                RegCloseKey(DeviceKey);
                return TRUE;
            }
        }

        BytesWritten = sizeof(DWORD);
        if (RegQueryValueExW(DeviceKey,
                             L"ConfigFlags",
                             NULL,
                             NULL,
                             (PBYTE)&Value,
                             &BytesWritten) == ERROR_SUCCESS)
        {
            if (Value & CONFIGFLAG_FAILEDINSTALL)
            {
                DPRINT("No need to install: %S\n", DeviceInstance);
                RegCloseKey(DeviceKey);
                return TRUE;
            }
        }

        RegCloseKey(DeviceKey);
    }

    DPRINT1("Installing: %S\n", DeviceInstance);

    /* Create a random UUID for the named pipe & event*/
    UuidCreate(&RandomUuid);
    swprintf(UuidString, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        RandomUuid.Data1, RandomUuid.Data2, RandomUuid.Data3,
        RandomUuid.Data4[0], RandomUuid.Data4[1], RandomUuid.Data4[2],
        RandomUuid.Data4[3], RandomUuid.Data4[4], RandomUuid.Data4[5],
        RandomUuid.Data4[6], RandomUuid.Data4[7]);

    ErrCode = CreatePnpInstallEventSecurity(&EventSd);
    if (ErrCode != ERROR_SUCCESS)
    {
        DPRINT1("CreatePnpInstallEventSecurity failed with error %u\n", GetLastError());
        return FALSE;
    }

    /* Set up the security attributes for the event */
    EventAttrs.nLength = sizeof(SECURITY_ATTRIBUTES);
    EventAttrs.lpSecurityDescriptor = EventSd;
    EventAttrs.bInheritHandle = FALSE;

    /* Create the event */
    wcscpy(InstallEventName, L"Global\\PNP_Device_Install_Event_0.");
    wcscat(InstallEventName, UuidString);
    hInstallEvent = CreateEventW(&EventAttrs, TRUE, FALSE, InstallEventName);
    HeapFree(GetProcessHeap(), 0, EventSd);
    if (!hInstallEvent)
    {
        DPRINT1("CreateEventW('%ls') failed with error %lu\n", InstallEventName, GetLastError());
        goto cleanup;
    }

    /* Create the named pipe */
    wcscpy(PipeName, L"\\\\.\\pipe\\PNP_Device_Install_Pipe_0.");
    wcscat(PipeName, UuidString);
    hPipe = CreateNamedPipeW(PipeName, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE, 1, 512, 512, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE)
    {
        DPRINT1("CreateNamedPipeW failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* Launch rundll32 to call ClientSideInstallW */
    wcscpy(CommandLine, L"rundll32.exe newdev.dll,ClientSideInstall ");
    wcscat(CommandLine, PipeName);

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);

    if (hUserToken)
    {
        /* newdev has to run under the environment of the current user */
        if (!CreateEnvironmentBlock(&Environment, hUserToken, FALSE))
        {
            DPRINT1("CreateEnvironmentBlock failed with error %d\n", GetLastError());
            goto cleanup;
        }

        if (!CreateProcessAsUserW(hUserToken, NULL, CommandLine, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, Environment, NULL, &StartupInfo, &ProcessInfo))
        {
            DPRINT1("CreateProcessAsUserW failed with error %u\n", GetLastError());
            goto cleanup;
        }
    }
    else
    {
        /* FIXME: This is probably not correct, I guess newdev should never be run with SYSTEM privileges.

           Still, we currently do that in 2nd stage setup and probably Console mode as well, so allow it here.
           (ShowWizard is only set to FALSE for these two modes) */
        ASSERT(!ShowWizard);

        if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
        {
            DPRINT1("CreateProcessW failed with error %u\n", GetLastError());
            goto cleanup;
        }
    }

    /* Wait for the function to connect to our pipe */
    if (!ConnectNamedPipe(hPipe, NULL))
    {
        if (GetLastError() != ERROR_PIPE_CONNECTED)
        {
            DPRINT1("ConnectNamedPipe failed with error %u\n", GetLastError());
            goto cleanup;
        }
    }

    /* Pass the data. The following output is partly compatible to Windows XP SP2 (researched using a modified newdev.dll to log this stuff) */
    Value = sizeof(InstallEventName);
    WriteFile(hPipe, &Value, sizeof(Value), &BytesWritten, NULL);
    WriteFile(hPipe, InstallEventName, Value, &BytesWritten, NULL);

    /* I couldn't figure out what the following value means under WinXP. It's usually 0 in my tests, but was also 5 once.
       Therefore the following line is entirely ReactOS-specific. We use the value here to pass the ShowWizard variable. */
    WriteFile(hPipe, &ShowWizard, sizeof(ShowWizard), &BytesWritten, NULL);

    Value = (wcslen(DeviceInstance) + 1) * sizeof(WCHAR);
    WriteFile(hPipe, &Value, sizeof(Value), &BytesWritten, NULL);
    WriteFile(hPipe, DeviceInstance, Value, &BytesWritten, NULL);

    /* Close the pipe so the client's next ReadFile returns ERROR_BROKEN_PIPE,
       allowing it to exit its batch loop gracefully. */
    CloseHandle(hPipe);
    hPipe = INVALID_HANDLE_VALUE;

    /* Wait for newdev.dll to finish processing */
    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);

    /* If the event got signalled, this is success */
    DeviceInstalled = WaitForSingleObject(hInstallEvent, 0) == WAIT_OBJECT_0;

cleanup:
    if (hInstallEvent)
        CloseHandle(hInstallEvent);

    if (hPipe != INVALID_HANDLE_VALUE)
        CloseHandle(hPipe);

    if (Environment)
        DestroyEnvironmentBlock(Environment);

    if (ProcessInfo.hProcess)
        CloseHandle(ProcessInfo.hProcess);

    if (ProcessInfo.hThread)
        CloseHandle(ProcessInfo.hThread);

    if (!DeviceInstalled)
    {
        DPRINT1("InstallDevice failed for DeviceInstance '%ws'\n", DeviceInstance);
    }

    return DeviceInstalled;
}


/**
 * @brief
 * Checks whether a device instance needs driver installation.
 *
 * @param[in] DeviceInstance
 * The device instance ID string.
 *
 * @return
 * TRUE if the device needs installation, FALSE if it can be skipped.
 */
static BOOL
DeviceNeedsInstall(PCWSTR DeviceInstance)
{
    HKEY DeviceKey;
    DWORD BytesRead;
    DWORD Value;

    if (RegOpenKeyExW(hEnumKey,
                      DeviceInstance,
                      0,
                      KEY_QUERY_VALUE,
                      &DeviceKey) == ERROR_SUCCESS)
    {
        WCHAR ClassName[64] = {0};
        DWORD Type = REG_NONE;
        DWORD Size = sizeof(ClassName);

        if (RegQueryValueExW(DeviceKey,
                             L"Class",
                             NULL,
                             &Type,
                             (PBYTE)ClassName,
                             &Size) == ERROR_SUCCESS &&
            Type == REG_SZ)
        {
            ClassName[(RTL_NUMBER_OF(ClassName) - 1)] = UNICODE_NULL;

            if (_wcsicmp(ClassName, L"Unknown") != 0)
            {
                RegCloseKey(DeviceKey);
                return FALSE;
            }
        }

        BytesRead = sizeof(DWORD);
        if (RegQueryValueExW(DeviceKey,
                             L"ConfigFlags",
                             NULL,
                             NULL,
                             (PBYTE)&Value,
                             &BytesRead) == ERROR_SUCCESS)
        {
            if (Value & CONFIGFLAG_FAILEDINSTALL)
            {
                RegCloseKey(DeviceKey);
                return FALSE;
            }
        }

        RegCloseKey(DeviceKey);
    }

    return TRUE;
}


/**
 * @brief
 * Batch-installs all devices from a multi-sz device list using a single
 * rundll32.exe process. This avoids the ~170ms per-device process creation
 * overhead during boot.
 *
 * @param[in] DeviceList
 * A multi-sz string of device instance IDs to install.
 *
 * @return
 * The number of devices that were successfully installed.
 *
 * @remarks
 * The pipe protocol sends a sequence of (event_name, ShowWizard, device_id)
 * tuples, followed by a zero-length sentinel (DWORD 0) to signal end of batch.
 * Each device gets its own named event for success/failure signaling.
 * This function is intended for boot-time (Step 1) installation only.
 */
static DWORD
InstallDevicesBatch(PCWSTR DeviceList)
{
    BOOL Success;
    DWORD BytesWritten;
    DWORD Value;
    DWORD ErrCode;
    DWORD DevicesInstalled = 0;
    DWORD DeviceCount = 0;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    LPVOID Environment = NULL;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    UUID RandomUuid;
    SECURITY_ATTRIBUTES EventAttrs;
    PSECURITY_DESCRIPTOR EventSd = NULL;
    PCWSTR currentDev;

    /* Per-device tracking */
    HANDLE *DeviceEvents = NULL;
    PCWSTR *DeviceIds = NULL;
    DWORD i;

    /* The following lengths are constant (see below), they cannot overflow */
    WCHAR CommandLine[116];
    WCHAR PipeName[74];
    WCHAR UuidString[39];
    WCHAR InstallEventName[73];

    DPRINT("InstallDevicesBatch: starting\n");

    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

    /* First pass: count devices that actually need installation */
    for (currentDev = DeviceList;
         currentDev[0] != UNICODE_NULL;
         currentDev += lstrlenW(currentDev) + 1)
    {
        if (DeviceNeedsInstall(currentDev))
        {
            DeviceCount++;
        }
    }

    if (DeviceCount == 0)
    {
        DPRINT("InstallDevicesBatch: no devices need installation\n");
        return 0;
    }

    DPRINT("InstallDevicesBatch: %lu devices need installation\n", DeviceCount);

    /* Allocate per-device tracking arrays */
    DeviceEvents = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                             DeviceCount * sizeof(HANDLE));
    DeviceIds = HeapAlloc(GetProcessHeap(), 0,
                          DeviceCount * sizeof(PCWSTR));
    if (!DeviceEvents || !DeviceIds)
    {
        DPRINT1("InstallDevicesBatch: HeapAlloc failed for tracking arrays\n");
        goto cleanup;
    }

    /* Create the security descriptor for events (shared across all devices) */
    ErrCode = CreatePnpInstallEventSecurity(&EventSd);
    if (ErrCode != ERROR_SUCCESS)
    {
        DPRINT1("CreatePnpInstallEventSecurity failed with error %lu\n", ErrCode);
        goto cleanup;
    }

    EventAttrs.nLength = sizeof(SECURITY_ATTRIBUTES);
    EventAttrs.lpSecurityDescriptor = EventSd;
    EventAttrs.bInheritHandle = FALSE;

    /* Create a random UUID for the pipe */
    UuidCreate(&RandomUuid);
    swprintf(UuidString, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        RandomUuid.Data1, RandomUuid.Data2, RandomUuid.Data3,
        RandomUuid.Data4[0], RandomUuid.Data4[1], RandomUuid.Data4[2],
        RandomUuid.Data4[3], RandomUuid.Data4[4], RandomUuid.Data4[5],
        RandomUuid.Data4[6], RandomUuid.Data4[7]);

    /* Create the named pipe */
    wcscpy(PipeName, L"\\\\.\\pipe\\PNP_Device_Install_Pipe_0.");
    wcscat(PipeName, UuidString);
    hPipe = CreateNamedPipeW(PipeName, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE, 1, 4096, 4096, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE)
    {
        DPRINT1("InstallDevicesBatch: CreateNamedPipeW failed with error %lu\n", GetLastError());
        goto cleanup;
    }

    /* Launch a single rundll32 to call ClientSideInstallW */
    wcscpy(CommandLine, L"rundll32.exe newdev.dll,ClientSideInstall ");
    wcscat(CommandLine, PipeName);

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);

    if (hUserToken)
    {
        if (!CreateEnvironmentBlock(&Environment, hUserToken, FALSE))
        {
            DPRINT1("InstallDevicesBatch: CreateEnvironmentBlock failed with error %lu\n", GetLastError());
            goto cleanup;
        }

        if (!CreateProcessAsUserW(hUserToken, NULL, CommandLine, NULL, NULL, FALSE,
                                  CREATE_UNICODE_ENVIRONMENT, Environment, NULL,
                                  &StartupInfo, &ProcessInfo))
        {
            DPRINT1("InstallDevicesBatch: CreateProcessAsUserW failed with error %lu\n", GetLastError());
            goto cleanup;
        }
    }
    else
    {
        if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL,
                            &StartupInfo, &ProcessInfo))
        {
            DPRINT1("InstallDevicesBatch: CreateProcessW failed with error %lu\n", GetLastError());
            goto cleanup;
        }
    }

    /* Wait for the client to connect to our pipe */
    if (!ConnectNamedPipe(hPipe, NULL))
    {
        if (GetLastError() != ERROR_PIPE_CONNECTED)
        {
            DPRINT1("InstallDevicesBatch: ConnectNamedPipe failed with error %lu\n", GetLastError());
            goto cleanup;
        }
    }

    /* Second pass: send each device over the pipe */
    i = 0;
    for (currentDev = DeviceList;
         currentDev[0] != UNICODE_NULL;
         currentDev += lstrlenW(currentDev) + 1)
    {
        UUID DevUuid;

        if (!DeviceNeedsInstall(currentDev))
        {
            DPRINT("InstallDevicesBatch: skipping %S (no install needed)\n", currentDev);
            continue;
        }

        /* Guard against TOCTOU: DeviceNeedsInstall results may have changed
           since the counting pass if registry state was modified concurrently. */
        if (i >= DeviceCount)
        {
            DPRINT1("InstallDevicesBatch: device count exceeded (TOCTOU race), stopping at %lu\n", i);
            break;
        }

        DPRINT1("InstallDevicesBatch: sending device %lu/%lu: %S\n", i + 1, DeviceCount, currentDev);

        /* Create a per-device UUID for the event */
        UuidCreate(&DevUuid);
        swprintf(UuidString, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
            DevUuid.Data1, DevUuid.Data2, DevUuid.Data3,
            DevUuid.Data4[0], DevUuid.Data4[1], DevUuid.Data4[2],
            DevUuid.Data4[3], DevUuid.Data4[4], DevUuid.Data4[5],
            DevUuid.Data4[6], DevUuid.Data4[7]);

        /* Create the per-device event */
        wcscpy(InstallEventName, L"Global\\PNP_Device_Install_Event_0.");
        wcscat(InstallEventName, UuidString);
        DeviceEvents[i] = CreateEventW(&EventAttrs, TRUE, FALSE, InstallEventName);
        if (!DeviceEvents[i])
        {
            DPRINT1("InstallDevicesBatch: CreateEventW('%ls') failed with error %lu\n",
                    InstallEventName, GetLastError());
            /* Skip this device but continue with others */
            i++;
            continue;
        }
        DeviceIds[i] = currentDev;

        /* Write event name (size-prefixed) */
        Value = sizeof(InstallEventName);
        Success = WriteFile(hPipe, &Value, sizeof(Value), &BytesWritten, NULL);
        if (!Success) goto send_sentinel;
        Success = WriteFile(hPipe, InstallEventName, Value, &BytesWritten, NULL);
        if (!Success) goto send_sentinel;

        /* Write ShowWizard flag (always FALSE during boot) */
        Value = FALSE;
        Success = WriteFile(hPipe, &Value, sizeof(Value), &BytesWritten, NULL);
        if (!Success) goto send_sentinel;

        /* Write device instance ID (size-prefixed) */
        Value = (lstrlenW(currentDev) + 1) * sizeof(WCHAR);
        Success = WriteFile(hPipe, &Value, sizeof(Value), &BytesWritten, NULL);
        if (!Success) goto send_sentinel;
        Success = WriteFile(hPipe, currentDev, Value, &BytesWritten, NULL);
        if (!Success) goto send_sentinel;

        i++;
    }

send_sentinel:
    /* Send zero-length sentinel to signal end of batch */
    Value = 0;
    WriteFile(hPipe, &Value, sizeof(Value), &BytesWritten, NULL);

    /* Wait for the single rundll32 process to finish */
    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);

    /* Check each per-device event for success */
    for (i = 0; i < DeviceCount; i++)
    {
        if (DeviceEvents[i])
        {
            if (WaitForSingleObject(DeviceEvents[i], 0) == WAIT_OBJECT_0)
            {
                DevicesInstalled++;
            }
            else if (DeviceIds[i])
            {
                DPRINT1("InstallDevicesBatch: installation failed for '%S'\n", DeviceIds[i]);
            }
        }
    }

    DPRINT("InstallDevicesBatch: %lu/%lu devices installed successfully\n",
           DevicesInstalled, DeviceCount);

cleanup:
    if (DeviceEvents)
    {
        for (i = 0; i < DeviceCount; i++)
        {
            if (DeviceEvents[i])
                CloseHandle(DeviceEvents[i]);
        }
        HeapFree(GetProcessHeap(), 0, DeviceEvents);
    }

    if (DeviceIds)
        HeapFree(GetProcessHeap(), 0, DeviceIds);

    if (EventSd)
        HeapFree(GetProcessHeap(), 0, EventSd);

    if (hPipe != INVALID_HANDLE_VALUE)
        CloseHandle(hPipe);

    if (Environment)
        DestroyEnvironmentBlock(Environment);

    if (ProcessInfo.hProcess)
        CloseHandle(ProcessInfo.hProcess);

    if (ProcessInfo.hThread)
        CloseHandle(ProcessInfo.hThread);

    return DevicesInstalled;
}


static LONG
ReadRegSzKey(
    IN HKEY hKey,
    IN LPCWSTR pszKey,
    OUT LPWSTR* pValue)
{
    LONG rc;
    DWORD dwType;
    DWORD cbData = 0;
    LPWSTR Value;

    if (!pValue)
        return ERROR_INVALID_PARAMETER;

    *pValue = NULL;
    rc = RegQueryValueExW(hKey, pszKey, NULL, &dwType, NULL, &cbData);
    if (rc != ERROR_SUCCESS)
        return rc;
    if (dwType != REG_SZ)
        return ERROR_FILE_NOT_FOUND;
    Value = HeapAlloc(GetProcessHeap(), 0, cbData + sizeof(WCHAR));
    if (!Value)
        return ERROR_NOT_ENOUGH_MEMORY;
    rc = RegQueryValueExW(hKey, pszKey, NULL, NULL, (LPBYTE)Value, &cbData);
    if (rc != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Value);
        return rc;
    }
    /* NULL-terminate the string */
    Value[cbData / sizeof(WCHAR)] = '\0';

    *pValue = Value;
    return ERROR_SUCCESS;
}


BOOL
SetupIsActive(VOID)
{
    HKEY hKey = NULL;
    DWORD regType, active, size;
    LONG rc;
    BOOL ret = FALSE;

    rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", 0, KEY_QUERY_VALUE, &hKey);
    if (rc != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(DWORD);
    rc = RegQueryValueExW(hKey, L"SystemSetupInProgress", NULL, &regType, (LPBYTE)&active, &size);
    if (rc != ERROR_SUCCESS)
        goto cleanup;
    if (regType != REG_DWORD || size != sizeof(DWORD))
        goto cleanup;

    ret = (active != 0);

cleanup:
    if (hKey != NULL)
        RegCloseKey(hKey);

    DPRINT("System setup in progress? %S\n", ret ? L"YES" : L"NO");

    return ret;
}


/**
 * @brief
 * Creates a security descriptor for the PnP event
 * installation.
 *
 * @param[out] EventSd
 * A pointer to an allocated security descriptor
 * for the event.
 *
 * @return
 * ERROR_SUCCESS is returned if the function has
 * successfully created the descriptor, otherwise
 * a Win32 error code is returned.
 *
 * @remarks
 * Only admins and local system have full power
 * over this event as privileged users can install
 * devices on a system.
 */
DWORD
CreatePnpInstallEventSecurity(
    _Out_ PSECURITY_DESCRIPTOR *EventSd)
{
    DWORD ErrCode;
    PACL Dacl = NULL;
    ULONG DaclSize;
    SECURITY_DESCRIPTOR AbsoluteSd;
    ULONG Size = 0;
    PSECURITY_DESCRIPTOR RelativeSd = NULL;
    PSID SystemSid = NULL, AdminsSid = NULL;
    static SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};

    if (!AllocateAndInitializeSid(&NtAuthority,
                                  1,
                                  SECURITY_LOCAL_SYSTEM_RID,
                                  0, 0, 0, 0, 0, 0, 0,
                                  &SystemSid))
    {
        return GetLastError();
    }

    if (!AllocateAndInitializeSid(&NtAuthority,
                                  2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0,
                                  &AdminsSid))
    {
        ErrCode = GetLastError();
        goto Quit;
    }

    DaclSize = sizeof(ACL) +
               sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(SystemSid) +
               sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(AdminsSid);

    Dacl = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, DaclSize);
    if (!Dacl)
    {
        ErrCode = ERROR_OUTOFMEMORY;
        goto Quit;
    }

    if (!InitializeAcl(Dacl, DaclSize, ACL_REVISION))
    {
        ErrCode = GetLastError();
        goto Quit;
    }

    if (!AddAccessAllowedAce(Dacl,
                             ACL_REVISION,
                             EVENT_ALL_ACCESS,
                             SystemSid))
    {
        ErrCode = GetLastError();
        goto Quit;
    }

    if (!AddAccessAllowedAce(Dacl,
                             ACL_REVISION,
                             EVENT_ALL_ACCESS,
                             AdminsSid))
    {
        ErrCode = GetLastError();
        goto Quit;
    }

    if (!InitializeSecurityDescriptor(&AbsoluteSd, SECURITY_DESCRIPTOR_REVISION))
    {
        ErrCode = GetLastError();
        goto Quit;
    }

    if (!SetSecurityDescriptorDacl(&AbsoluteSd, TRUE, Dacl, FALSE))
    {
        ErrCode = GetLastError();
        goto Quit;
    }

    if (!SetSecurityDescriptorOwner(&AbsoluteSd, SystemSid, FALSE))
    {
        ErrCode = GetLastError();
        goto Quit;
    }

    if (!SetSecurityDescriptorGroup(&AbsoluteSd, AdminsSid, FALSE))
    {
        ErrCode = GetLastError();
        goto Quit;
    }

    if (!MakeSelfRelativeSD(&AbsoluteSd, NULL, &Size) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        RelativeSd = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
        if (RelativeSd == NULL)
        {
            ErrCode = ERROR_OUTOFMEMORY;
            goto Quit;
        }

        if (!MakeSelfRelativeSD(&AbsoluteSd, RelativeSd, &Size))
        {
            ErrCode = GetLastError();
            goto Quit;
        }
    }

    *EventSd = RelativeSd;
    ErrCode = ERROR_SUCCESS;

Quit:
    if (SystemSid)
    {
        FreeSid(SystemSid);
    }

    if (AdminsSid)
    {
        FreeSid(AdminsSid);
    }

    if (Dacl)
    {
        HeapFree(GetProcessHeap(), 0, Dacl);
    }

    if (ErrCode != ERROR_SUCCESS)
    {
        if (RelativeSd)
        {
            HeapFree(GetProcessHeap(), 0, RelativeSd);
        }
    }

    return ErrCode;
}


static BOOL
IsConsoleBoot(VOID)
{
    HKEY ControlKey = NULL;
    LPWSTR SystemStartOptions = NULL;
    LPWSTR CurrentOption, NextOption; /* Pointers into SystemStartOptions */
    BOOL ConsoleBoot = FALSE;
    LONG rc;

    rc = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control",
        0,
        KEY_QUERY_VALUE,
        &ControlKey);

    rc = ReadRegSzKey(ControlKey, L"SystemStartOptions", &SystemStartOptions);
    if (rc != ERROR_SUCCESS)
        goto cleanup;

    /* Check for CONSOLE switch in SystemStartOptions */
    CurrentOption = SystemStartOptions;
    while (CurrentOption)
    {
        NextOption = wcschr(CurrentOption, L' ');
        if (NextOption)
            *NextOption = L'\0';
        if (_wcsicmp(CurrentOption, L"CONSOLE") == 0)
        {
            DPRINT("Found %S. Switching to console boot\n", CurrentOption);
            ConsoleBoot = TRUE;
            goto cleanup;
        }
        CurrentOption = NextOption ? NextOption + 1 : NULL;
    }

cleanup:
    if (ControlKey != NULL)
        RegCloseKey(ControlKey);
    HeapFree(GetProcessHeap(), 0, SystemStartOptions);
    return ConsoleBoot;
}


FORCEINLINE
BOOL
IsUISuppressionAllowed(VOID)
{
    /* Display the newdev.dll wizard UI only if it's allowed */
    return (g_IsUISuppressed || GetSuppressNewUIValue());
}


/**
 * @brief
 * Checks whether a device instance ID appears in a multi-sz device list.
 *
 * @param[in] DeviceList
 * A double-NUL-terminated multi-sz string of device instance IDs.
 *
 * @param[in] DeviceInstance
 * The device instance ID to search for.
 *
 * @return
 * TRUE if DeviceInstance is found in DeviceList, FALSE otherwise.
 */
static BOOL
IsDeviceInMultiSzList(PCWSTR DeviceList, PCWSTR DeviceInstance)
{
    PCWSTR current;

    if (!DeviceList || !DeviceInstance)
        return FALSE;

    for (current = DeviceList;
         current[0] != UNICODE_NULL;
         current += lstrlenW(current) + 1)
    {
        if (_wcsicmp(current, DeviceInstance) == 0)
            return TRUE;
    }

    return FALSE;
}


/* Loop to install all queued devices installations */
DWORD
WINAPI
DeviceInstallThread(LPVOID lpParameter)
{
    PLIST_ENTRY ListEntry;
    DeviceInstallParams* Params;
    PWSTR deviceList = NULL;

    UNREFERENCED_PARAMETER(lpParameter);

    // Step 1: install all drivers which were configured during the boot

    DPRINT("Step 1: Installing devices configured during the boot\n");

    while (TRUE)
    {
        ULONG devListSize;
        DWORD status = PNP_GetDeviceListSize(NULL, NULL, &devListSize, 0);
        if (status != CR_SUCCESS)
        {
            goto Step2;
        }

        deviceList = HeapAlloc(GetProcessHeap(), 0, devListSize * sizeof(WCHAR));
        if (!deviceList)
        {
            goto Step2;
        }

        status = PNP_GetDeviceList(NULL, NULL, deviceList, &devListSize, 0);
        if (status == CR_BUFFER_SMALL)
        {
            HeapFree(GetProcessHeap(), 0, deviceList);
            deviceList = NULL;
        }
        else if (status != CR_SUCCESS)
        {
            DPRINT1("PNP_GetDeviceList failed with error %u\n", status);
            HeapFree(GetProcessHeap(), 0, deviceList);
            deviceList = NULL;
            goto Step2;
        }
        else // status == CR_SUCCESS
        {
            break;
        }
    }

    InstallDevicesBatch(deviceList);

    /*
     * Drain any device-install events that were queued by PnpEventThread
     * during the batch. These devices were already attempted above, so
     * re-processing them in Step 2 would be redundant and waste time
     * (e.g., ~370ms per device for a new rundll32 process creation +
     * SetupDi failure).
     */
    {
        DWORD drained = 0;

        WaitForSingleObject(hDeviceInstallListMutex, INFINITE);
        while (!IsListEmpty(&DeviceInstallListHead))
        {
            ListEntry = RemoveHeadList(&DeviceInstallListHead);
            Params = CONTAINING_RECORD(ListEntry, DeviceInstallParams, ListEntry);

            if (IsDeviceInMultiSzList(deviceList, Params->DeviceIds))
            {
                /* This device was already processed in the batch -- skip it */
                DPRINT("Step 1->2 drain: skipping already-batched device '%S'\n",
                       Params->DeviceIds);
                drained++;
            }
            else
            {
                /* New device that appeared after the batch list was captured.
                 * Put it back at the head so Step 2 picks it up. */
                InsertHeadList(&DeviceInstallListHead, &Params->ListEntry);
                break;
            }

            HeapFree(GetProcessHeap(), 0, Params);
        }
        ReleaseMutex(hDeviceInstallListMutex);

        if (drained > 0)
        {
            DPRINT1("Step 1->2 drain: discarded %lu already-batched events\n", drained);
        }
    }

    HeapFree(GetProcessHeap(), 0, deviceList);
    deviceList = NULL;

    // Step 2: start the wait-loop for newly added devices
Step2:

    DPRINT("Step 2: Starting the wait-loop\n");

    WaitForSingleObject(hInstallEvent, INFINITE);

    BOOL showWizard = !SetupIsActive() && !IsConsoleBoot();

    while (TRUE)
    {
        /* Dequeue the next oldest device-install event */
        WaitForSingleObject(hDeviceInstallListMutex, INFINITE);
        ListEntry = (IsListEmpty(&DeviceInstallListHead)
                        ? NULL : RemoveHeadList(&DeviceInstallListHead));
        ReleaseMutex(hDeviceInstallListMutex);

        if (ListEntry == NULL)
        {
            SetEvent(hNoPendingInstalls);
            WaitForSingleObject(hDeviceInstallListNotEmpty, INFINITE);
        }
        else
        {
            ResetEvent(hNoPendingInstalls);
            Params = CONTAINING_RECORD(ListEntry, DeviceInstallParams, ListEntry);
            InstallDevice(Params->DeviceIds, showWizard && !IsUISuppressionAllowed());
            HeapFree(GetProcessHeap(), 0, Params);
        }
    }

    return 0;
}
