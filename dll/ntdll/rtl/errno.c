/*
 * PROJECT:     ReactOS NT User-Mode Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Wine-compatible ntdll CRT errno storage
 */

#include <ntdll.h>

int*
CDECL
_errno(void)
{
    return (int*)&NtCurrentTeb()->TlsSlots[NTDLL_TLS_ERRNO];
}
