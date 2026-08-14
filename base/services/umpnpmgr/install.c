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

/*
 * Returns TRUE if the given device-instance still needs installation.
 *
 * Filter rules (any one excludes the device from the batch):
 *   1. "Class" registry value already present       -> driver installed previously
 *   2. ConfigFlags has CONFIGFLAG_FAILEDINSTALL     -> previously failed, sticky
 *
 * This mirrors the early-out in InstallDevice(). Being started is not a safe
 * proxy for "already installed": DevInstallW still drives the INF/class
 * installer pipeline for started devices that have not been fully installed.
 */
static BOOL
DeviceNeedsInstall(PCWSTR DeviceInstance)
{
    HKEY DeviceKey;
    DWORD Value;
    DWORD BytesWritten;
    BOOL NeedsInstall = TRUE;

    if (RegOpenKeyExW(hEnumKey,
                      DeviceInstance,
                      0,
                      KEY_QUERY_VALUE,
                      &DeviceKey) != ERROR_SUCCESS)
    {
        /* No Enum subkey yet — definitely needs install. */
        return TRUE;
    }

    if (RegQueryValueExW(DeviceKey,
                         L"Class",
                         NULL,
                         NULL,
                         NULL,
                         NULL) == ERROR_SUCCESS)
    {
        /* Class already assigned — driver was installed in a prior boot. */
        NeedsInstall = FALSE;
        goto done;
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
            /* Previously failed — don't re-attempt during batch install. */
            NeedsInstall = FALSE;
        }
    }

done:
    RegCloseKey(DeviceKey);
    return NeedsInstall;
}


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
        if (RegQueryValueExW(DeviceKey,
                             L"Class",
                             NULL,
                             NULL,
                             NULL,
                             NULL) == ERROR_SUCCESS)
        {
            DPRINT("No need to install: %S\n", DeviceInstance);
            RegCloseKey(DeviceKey);
            return TRUE;
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
    _swprintf(UuidString, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
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


static BOOL
WaitForBatchClientConnect(
    _In_ HANDLE hPipe,
    _In_ HANDLE hProcess)
{
    DWORD ErrCode;
    DWORD WaitMode = PIPE_WAIT;

    while (TRUE)
    {
        if (ConnectNamedPipe(hPipe, NULL))
            break;

        ErrCode = GetLastError();
        if (ErrCode == ERROR_PIPE_CONNECTED)
            break;

        if (ErrCode != ERROR_PIPE_LISTENING)
            return FALSE;

        if (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0)
        {
            SetLastError(ERROR_PIPE_NOT_CONNECTED);
            return FALSE;
        }

        Sleep(50);
    }

    if (!SetNamedPipeHandleState(hPipe, &WaitMode, NULL, NULL))
        return FALSE;

    return TRUE;
}


/*
 * Batch-install every device in a multi-sz list via a single
 * rundll32.exe invocation of newdev.dll,ClientSideInstallW.
 *
 * Used at boot to amortize the rundll32 process-spawn cost (previously
 * ~1 spawn per device, e.g. 53 spawns on the livecd first boot). The
 * spawned child reads the whole device list from the named pipe and
 * calls DevInstallW for each entry, signalling the install event once
 * at the end.
 *
 * Returns TRUE if the batch child ran to completion and signalled the
 * event (individual device failures inside the batch do NOT make this
 * return FALSE — they match the per-device loop's tolerant behaviour).
 * Returns FALSE on infrastructure failure (pipe/process creation, or
 * child crash before signalling), in which case the caller should fall
 * back to the per-device InstallDevice loop.
 */
static BOOL
InstallDevicesBatch(PCWSTR MultiSzDeviceList, DWORD DeviceCount)
{
    BOOL BatchInstalled = FALSE;
    DWORD BytesWritten;
    DWORD Value;
    DWORD ErrCode;
    DWORD PipeBufferSize;
    HANDLE hInstallEvent = NULL;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    LPVOID Environment = NULL;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    UUID RandomUuid;
    SECURITY_ATTRIBUTES EventAttrs;
    PSECURITY_DESCRIPTOR EventSd;
    PCWSTR currentDev;
    DWORD i;

    /* The following lengths are constant (see InstallDevice for rationale) */
    WCHAR CommandLine[124];
    WCHAR InstallEventName[73];
    WCHAR PipeName[74];
    WCHAR UuidString[39];

    DPRINT1("Installing: batch[%lu devices]\n", DeviceCount);

    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

    if (DeviceCount == 0 || MultiSzDeviceList == NULL)
        return TRUE;

    /* Random UUID for the pipe/event names (same pattern as InstallDevice) */
    UuidCreate(&RandomUuid);
    swprintf(UuidString,
        RTL_NUMBER_OF(UuidString),
        L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
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

    EventAttrs.nLength = sizeof(SECURITY_ATTRIBUTES);
    EventAttrs.lpSecurityDescriptor = EventSd;
    EventAttrs.bInheritHandle = FALSE;

    wcscpy(InstallEventName, L"Global\\PNP_Device_Install_Event_0.");
    wcscat(InstallEventName, UuidString);
    hInstallEvent = CreateEventW(&EventAttrs, TRUE, FALSE, InstallEventName);
    HeapFree(GetProcessHeap(), 0, EventSd);
    if (!hInstallEvent)
    {
        DPRINT1("CreateEventW('%ls') failed with error %lu\n", InstallEventName, GetLastError());
        goto cleanup;
    }

    /* 64 KB pipe buffer — 53 devices ≈ 8 KB, 64 KB leaves room to grow. */
    PipeBufferSize = 64 * 1024;

    wcscpy(PipeName, L"\\\\.\\pipe\\PNP_Device_Install_Pipe_0.");
    wcscat(PipeName, UuidString);
    hPipe = CreateNamedPipeW(PipeName, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_NOWAIT, 1,
                             PipeBufferSize, PipeBufferSize, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE)
    {
        DPRINT1("CreateNamedPipeW failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* Reuse the existing ClientSideInstallW entry point. The private marker
     * selects batch mode without adding another newdev.dll export. */
    wcscpy(CommandLine, L"rundll32.exe newdev.dll,ClientSideInstall ");
    wcscat(CommandLine, PipeName);

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);

    if (hUserToken)
    {
        /* Match InstallDevice(): even silent installs should run in the
         * current user's environment once an interactive logon is known. */
        if (!CreateEnvironmentBlock(&Environment, hUserToken, FALSE))
        {
            DPRINT1("CreateEnvironmentBlock(batch) failed with error %d\n", GetLastError());
            goto cleanup;
        }

        if (!CreateProcessAsUserW(hUserToken, NULL, CommandLine, NULL, NULL, FALSE,
                                  CREATE_UNICODE_ENVIRONMENT, Environment, NULL,
                                  &StartupInfo, &ProcessInfo))
        {
            DPRINT1("CreateProcessAsUserW(batch) failed with error %u\n", GetLastError());
            goto cleanup;
        }
    }
    else
    {
        /* Step 1 runs before any interactive user token is reported. */
        if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
        {
            DPRINT1("CreateProcessW(batch) failed with error %u\n", GetLastError());
            goto cleanup;
        }
    }

    /* Wait for the child to connect to our pipe */
    if (!WaitForBatchClientConnect(hPipe, ProcessInfo.hProcess))
    {
        DPRINT1("WaitForBatchClientConnect failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* Prologue: event name size + event name */
    Value = sizeof(InstallEventName);
    if (!WriteFile(hPipe, &Value, sizeof(Value), &BytesWritten, NULL) ||
        !WriteFile(hPipe, InstallEventName, Value, &BytesWritten, NULL))
    {
        DPRINT1("WriteFile(EventName) failed with error %u\n", GetLastError());
        goto cleanup;
    }

    /* ShowWizard — always FALSE for batch. */
    {
        BOOL ShowWizardFalse = FALSE;
        if (!WriteFile(hPipe, &ShowWizardFalse, sizeof(ShowWizardFalse), &BytesWritten, NULL))
        {
            DPRINT1("WriteFile(ShowWizard) failed with error %u\n", GetLastError());
            goto cleanup;
        }
    }

    /* Batch-specific payload: marker, DeviceCount, then N pairs. */
    Value = NEWDEV_INSTALL_BATCH_MARKER;
    if (!WriteFile(hPipe, &Value, sizeof(Value), &BytesWritten, NULL))
    {
        DPRINT1("WriteFile(BatchMarker) failed with error %u\n", GetLastError());
        goto cleanup;
    }

    if (!WriteFile(hPipe, &DeviceCount, sizeof(DeviceCount), &BytesWritten, NULL))
    {
        DPRINT1("WriteFile(DeviceCount) failed with error %u\n", GetLastError());
        goto cleanup;
    }

    i = 0;
    for (currentDev = MultiSzDeviceList;
         currentDev[0] != UNICODE_NULL && i < DeviceCount;
         currentDev += lstrlenW(currentDev) + 1, i++)
    {
        /* Same per-device DPRINT1 format as the legacy InstallDevice path —
         * this is what the serial log used to show before we hand control to
         * rundll32, keeping "where is the installs?" answerable in batch mode
         * without dragging DbgPrint into newdev.dll. */
        DPRINT1("Installing: %S\n", currentDev);

        Value = (lstrlenW(currentDev) + 1) * sizeof(WCHAR);
        if (!WriteFile(hPipe, &Value, sizeof(Value), &BytesWritten, NULL) ||
            !WriteFile(hPipe, currentDev, Value, &BytesWritten, NULL))
        {
            DPRINT1("WriteFile(DeviceInstance[%lu]) failed with error %u\n", i, GetLastError());
            goto cleanup;
        }
    }

    /* Wait for the batch child to finish processing */
    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);

    /* Batch success is reported by the shared install event being signalled
     * exactly once at the end of the child's loop. */
    BatchInstalled = WaitForSingleObject(hInstallEvent, 0) == WAIT_OBJECT_0;

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

    if (!BatchInstalled)
    {
        DPRINT1("InstallDevicesBatch failed for %lu device(s); caller will fall back\n", DeviceCount);
    }

    return BatchInstalled;
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

    /* Compute the size needed for the DACL and allocate it */
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

    /* Retrieve the size needed for the relative SD */
    if (MakeSelfRelativeSD(&AbsoluteSd, NULL, &Size) ||
        (GetLastError() != ERROR_INSUFFICIENT_BUFFER))
    {
        ErrCode = GetLastError();
        goto Quit;
    }

    /* Build the relative SD */
    RelativeSd = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (RelativeSd == NULL)
    {
        ErrCode = ERROR_OUTOFMEMORY;
        goto Quit;
    }
    if (!MakeSelfRelativeSD(&AbsoluteSd, RelativeSd, &Size))
    {
        ErrCode = GetLastError();
        HeapFree(GetProcessHeap(), 0, RelativeSd);
        goto Quit;
    }

    *EventSd = RelativeSd;
    ErrCode = ERROR_SUCCESS;

Quit:
    if (SystemSid)
        FreeSid(SystemSid);

    if (AdminsSid)
        FreeSid(AdminsSid);

    if (Dacl)
        HeapFree(GetProcessHeap(), 0, Dacl);

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


/* Loop to install all queued devices installations */
DWORD
WINAPI
DeviceInstallThread(LPVOID lpParameter)
{
    DeviceInstallParams* Params;

    UNREFERENCED_PARAMETER(lpParameter);

    // Step 1: install all drivers which were configured during the boot

    DPRINT("Step 1: Installing devices configured during the boot\n");

    PWSTR deviceList;

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
        }
        else if (status != CR_SUCCESS)
        {
            DPRINT1("PNP_GetDeviceList failed with error %u\n", status);
            goto Cleanup;
        }
        else // status == CR_SUCCESS
        {
            break;
        }
    }

    /*
     * Walk the raw multi-sz device list and build a filtered copy that
     * contains ONLY devices which still need installation. Every already-
     * installed device (Class assigned) or previously-failed device
     * (CONFIGFLAG_FAILEDINSTALL) is dropped here, before we ever spawn
     * rundll32 — this is where most of the boot-time savings come from on
     * warm boots.
     */
    DWORD totalCount = 0, filteredCount = 0;
    SIZE_T totalBytes = 0;
    PWSTR filteredList = NULL;

    for (PWSTR currentDev = deviceList;
         currentDev[0] != UNICODE_NULL;
         currentDev += lstrlenW(currentDev) + 1)
    {
        totalCount++;
        totalBytes += (lstrlenW(currentDev) + 1) * sizeof(WCHAR);
    }
    totalBytes += sizeof(WCHAR); /* final multi-sz terminator */

    filteredList = HeapAlloc(GetProcessHeap(), 0, totalBytes);
    if (filteredList)
    {
        PWSTR outCursor = filteredList;
        for (PWSTR currentDev = deviceList;
             currentDev[0] != UNICODE_NULL;
             currentDev += lstrlenW(currentDev) + 1)
        {
            if (DeviceNeedsInstall(currentDev))
            {
                SIZE_T cch = lstrlenW(currentDev) + 1;
                memcpy(outCursor, currentDev, cch * sizeof(WCHAR));
                outCursor += cch;
                filteredCount++;
            }
            else
            {
                DPRINT("No need to install: %S\n", currentDev);
            }
        }
        *outCursor = UNICODE_NULL; /* multi-sz terminator */
    }

    DPRINT1("Boot device install: %lu candidate(s), %lu need install\n",
            totalCount, filteredCount);

    if (filteredList != NULL && filteredCount != 0)
    {
        /* Try the batch path first — one rundll32 spawn for the whole
         * filtered boot device list. Falls back to the legacy per-device
         * loop if the batch child fails to signal completion (e.g. an
         * older newdev.dll without the private batch protocol, or an
         * infrastructure failure). */
        if (!InstallDevicesBatch(filteredList, filteredCount))
        {
            DPRINT1("Batch install failed, falling back to per-device loop\n");

            for (PWSTR currentDev = filteredList;
                 currentDev[0] != UNICODE_NULL;
                 currentDev += lstrlenW(currentDev) + 1)
            {
                InstallDevice(currentDev, FALSE);
            }
        }
    }
    else if (filteredList == NULL)
    {
        /* Allocation failed — fall back to the legacy per-device loop over
         * the unfiltered list. Each InstallDevice call will still skip
         * already-installed entries via its own RegQueryValueExW check. */
        DPRINT1("Filtered list allocation failed, falling back to per-device loop\n");

        for (PWSTR currentDev = deviceList;
             currentDev[0] != UNICODE_NULL;
             currentDev += lstrlenW(currentDev) + 1)
        {
            InstallDevice(currentDev, FALSE);
        }
    }
    /* else: nothing to install, skip the spawn entirely. */

    if (filteredList)
        HeapFree(GetProcessHeap(), 0, filteredList);

Cleanup:
    HeapFree(GetProcessHeap(), 0, deviceList);

    // Step 2: start the wait-loop for newly added devices
Step2:

    DPRINT("Step 2: Starting the wait-loop\n");

    WaitForSingleObject(hInstallEvent, INFINITE);

    BOOL showWizard = !SetupIsActive() && !IsConsoleBoot();

    while (TRUE)
    {
        /*
         * Drain the queue into a local list under a single mutex hold, then
         * release the mutex and process the whole burst. When the kernel
         * triggers a flurry of DeviceInstall events (e.g. after Step 1
         * completes and PnP starts its way through all boot devices), this
         * lets us batch them into one rundll32 spawn instead of firing one
         * CreateProcess per event — which was the main residual slowness
         * after the Step 1 batch optimization.
         */
        LIST_ENTRY LocalBurst;
        BOOL BurstEmpty;
        InitializeListHead(&LocalBurst);

        WaitForSingleObject(hDeviceInstallListMutex, INFINITE);
        while (!IsListEmpty(&DeviceInstallListHead))
        {
            PLIST_ENTRY entry = RemoveHeadList(&DeviceInstallListHead);
            InsertTailList(&LocalBurst, entry);
        }
        BurstEmpty = IsListEmpty(&LocalBurst);
        if (BurstEmpty)
            SetEvent(hNoPendingInstalls);
        else
            ResetEvent(hNoPendingInstalls);
        ReleaseMutex(hDeviceInstallListMutex);

        if (BurstEmpty)
        {
            WaitForSingleObject(hDeviceInstallListNotEmpty, INFINITE);
            continue;
        }

        /*
         * If the burst contains more than one device AND we don't need the
         * UI wizard, batch them through a single ClientSideInstallW
         * rundll32 spawn. Otherwise fall back to the legacy per-device
         * path (which still respects ShowWizard and drives the wizard UI
         * for interactive installs).
         */
        BOOL burstShowWizard = showWizard && !IsUISuppressionAllowed();
        if (!burstShowWizard)
        {
            /* Count the burst and build a multi-sz list for the batch child. */
            DWORD burstCount = 0;
            SIZE_T burstBytes = sizeof(WCHAR); /* final multi-sz NUL */

            for (PLIST_ENTRY e = LocalBurst.Flink; e != &LocalBurst; e = e->Flink)
            {
                DeviceInstallParams* p = CONTAINING_RECORD(e, DeviceInstallParams, ListEntry);
                burstCount++;
                burstBytes += (lstrlenW(p->DeviceIds) + 1) * sizeof(WCHAR);
            }

            PWSTR burstList = NULL;
            if (burstCount > 1)
            {
                burstList = HeapAlloc(GetProcessHeap(), 0, burstBytes);
            }

            if (burstList != NULL)
            {
                /* Filter out devices that no longer need install. */
                DWORD filteredBurstCount = 0;
                PWSTR outCursor = burstList;
                for (PLIST_ENTRY e = LocalBurst.Flink; e != &LocalBurst; e = e->Flink)
                {
                    DeviceInstallParams* p = CONTAINING_RECORD(e, DeviceInstallParams, ListEntry);
                    if (DeviceNeedsInstall(p->DeviceIds))
                    {
                        SIZE_T cch = lstrlenW(p->DeviceIds) + 1;
                        memcpy(outCursor, p->DeviceIds, cch * sizeof(WCHAR));
                        outCursor += cch;
                        filteredBurstCount++;
                    }
                }
                *outCursor = UNICODE_NULL;

                if (filteredBurstCount == 0 ||
                    InstallDevicesBatch(burstList, filteredBurstCount))
                {
                    /* Burst handled (batch succeeded or nothing to do). */
                    HeapFree(GetProcessHeap(), 0, burstList);
                    while (!IsListEmpty(&LocalBurst))
                    {
                        PLIST_ENTRY e = RemoveHeadList(&LocalBurst);
                        Params = CONTAINING_RECORD(e, DeviceInstallParams, ListEntry);
                        HeapFree(GetProcessHeap(), 0, Params);
                    }
                    continue;
                }

                HeapFree(GetProcessHeap(), 0, burstList);
                /* Fall through to per-device loop on batch failure. */
                DPRINT1("Step 2 batch failed, falling back to per-device loop\n");
            }
        }

        /* Per-device path: either a single-device burst, a wizard is wanted,
         * or the batch attempt failed — process each entry individually. */
        while (!IsListEmpty(&LocalBurst))
        {
            PLIST_ENTRY e = RemoveHeadList(&LocalBurst);
            Params = CONTAINING_RECORD(e, DeviceInstallParams, ListEntry);
            InstallDevice(Params->DeviceIds, burstShowWizard);
            HeapFree(GetProcessHeap(), 0, Params);
        }
    }

    return 0;
}
