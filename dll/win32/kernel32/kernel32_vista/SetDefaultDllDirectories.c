/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     SetDefaultDllDirectories
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/loader.c
 */

#include "k32_vista.h"

#ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR 0x00000200
#endif
#ifndef LOAD_LIBRARY_SEARCH_USER_DIRS
#define LOAD_LIBRARY_SEARCH_USER_DIRS       0x00000400
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32        0x00000800
#endif
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS    0x00001000
#endif

BOOL
WINAPI
SetDefaultDllDirectories(
    _In_ DWORD DirectoryFlags)
{
    if (!DirectoryFlags ||
        (DirectoryFlags & ~(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                            LOAD_LIBRARY_SEARCH_USER_DIRS |
                            LOAD_LIBRARY_SEARCH_SYSTEM32 |
                            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    RtlEnterCriticalSection(&BaseDllDirectoryLock);
    BaseDefaultDllDirectoriesFlags = DirectoryFlags;
    RtlLeaveCriticalSection(&BaseDllDirectoryLock);
    return TRUE;
}
