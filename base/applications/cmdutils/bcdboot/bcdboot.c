/*
 * PROJECT:     ReactOS Boot Configuration Data boot file creation tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Provides the BCDBoot command and system image contract
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include <stdio.h>

#include <windef.h>
#include <winbase.h>

static VOID
PrintUsage(VOID)
{
    fputws(L"BCDBOOT boot file creation and repair tool.\n\n", stdout);
    fputws(L"BCDBOOT <source> [/l <locale>] [/s <volume-letter> [/f <firmware type>]]\n", stdout);
    fputws(L"        [/v] [/m [{OS Loader GUID}]] [/addlast | /p] [/d] [/c]\n", stdout);
    fputws(L"        [/bootex] [/offline]\n\n", stdout);
    fputws(L"  source       Location of the Windows directory used as the boot-file source.\n", stdout);
    fputws(L"  /s volume    System partition receiving the boot files.\n", stdout);
    fputws(L"  /f type      Firmware type: UEFI, BIOS, or ALL.\n", stdout);
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) WCHAR *argv[])
{
    if ((argc == 2) &&
        ((_wcsicmp(argv[1], L"/?") == 0) ||
         (_wcsicmp(argv[1], L"-?") == 0) ||
         (_wcsicmp(argv[1], L"/help") == 0) ||
         (_wcsicmp(argv[1], L"--help") == 0)))
    {
        PrintUsage();
        return ERROR_SUCCESS;
    }

    if (argc < 2)
    {
        PrintUsage();
        return ERROR_INVALID_PARAMETER;
    }

    fputws(L"BCDBOOT: Creating or repairing a BCD boot environment is not yet supported.\n", stderr);
    return ERROR_CALL_NOT_IMPLEMENTED;
}
