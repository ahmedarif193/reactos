/*
 * PROJECT:     ReactOS live kernel dump utility
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Write a live kernel dump to a caller-selected file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ndk/iotypes.h>
#include <ndk/kdfuncs.h>
#include <ndk/kdtypes.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>
#include <stdio.h>

static BOOL GetDefaultDumpPath(_Out_writes_(PathLength) PWCHAR Path, _In_ DWORD PathLength)
{
    DWORD Length;
    static const WCHAR DumpName[] = L"\\MEMORY.DMP";

    Length = GetWindowsDirectoryW(Path, PathLength);
    if ((Length == 0) || (Length >= PathLength))
        return FALSE;

    if (Length + ARRAYSIZE(DumpName) > PathLength)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    CopyMemory(&Path[Length], DumpName, sizeof(DumpName));
    return TRUE;
}

int wmain(int argc, WCHAR *argv[])
{
    SYSDBG_LIVEDUMP_CONTROL Control = {0};
    WCHAR DefaultPath[MAX_PATH];
    PCWSTR DumpPath;
    BOOLEAN OldPrivilege;
    NTSTATUS Status;
    HANDLE DumpFile;

    if (argc > 2)
    {
        fwprintf(stderr, L"Usage: livedump [dump-file]\n");
        return 2;
    }

    if (argc == 2)
    {
        DumpPath = argv[1];
    }
    else
    {
        if (!GetDefaultDumpPath(DefaultPath, ARRAYSIZE(DefaultPath)))
        {
            fwprintf(stderr, L"Unable to determine the dump path (error %lu).\n", GetLastError());
            return 1;
        }

        DumpPath = DefaultPath;
    }

    DumpFile = CreateFileW(DumpPath, GENERIC_WRITE | SYNCHRONIZE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING, NULL);
    if (DumpFile == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"Unable to create %ls (error %lu).\n", DumpPath, GetLastError());
        return 1;
    }

    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &OldPrivilege);
    if (!NT_SUCCESS(Status))
    {
        fwprintf(stderr, L"Unable to enable SeDebugPrivilege (status 0x%08lx).\n", Status);
        CloseHandle(DumpFile);
        return 1;
    }

    /* Leaving BugCheckCode zero lets the kernel stamp its LIVE_SYSTEM_DUMP default. */
    Control.Version = SYSDBG_LIVEDUMP_CONTROL_VERSION;
    Control.DumpFileHandle = DumpFile;

    wprintf(L"Writing live kernel dump to %ls...\n", DumpPath);
    Status = NtSystemDebugControl(SysDbgGetLiveKernelDump, &Control, sizeof(Control), NULL, 0, NULL);

    RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, OldPrivilege, FALSE, &OldPrivilege);
    CloseHandle(DumpFile);

    if (!NT_SUCCESS(Status))
    {
        fwprintf(stderr, L"Live kernel dump failed (status 0x%08lx).\n", Status);
        return 1;
    }

    wprintf(L"Live kernel dump completed.\n");
    return 0;
}
