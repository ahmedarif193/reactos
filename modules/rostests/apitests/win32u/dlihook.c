/*
 * PROJECT:     ReactOS delayimport Library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Delay-import hook for win32u.dll
 * COPYRIGHT:   Copyright 2025 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include <windows.h>
#include <delayimp.h>
#include <stdio.h>

static
FARPROC
WINAPI
DliFailureHook(
    unsigned code,
    PDelayLoadInfo pdli)
{
    if (code == dliFailLoadLib)
    {
        fprintf(stderr, "Delayimport: Failed to load %s\n", pdli->szDll);
    }
    else if (code == dliFailGetProc)
    {
        fprintf(stderr,
                "Delayimport: Failed to import %s from %s\n",
                pdli->dlp.szProcName,
                pdli->szDll);
    }
    else
    {
        fprintf(stderr, "Delayimport: Unknown error code %d (%s, 0x%lx)\n", code, pdli->szDll, pdli->dwLastError);
    }

    return NULL;
}

PfnDliHook __pfnDliFailureHook2 = DliFailureHook;
