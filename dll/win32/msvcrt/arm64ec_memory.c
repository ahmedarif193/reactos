/*
 * PROJECT:     ReactOS Microsoft C Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native ARM64EC memory export entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winnt.h>
#include <stddef.h>

#undef RtlMoveMemory

DECLSPEC_IMPORT VOID NTAPI RtlMoveMemory(VOID *Destination, const VOID *Source, SIZE_T Length);

void * __cdecl
memmove(void *Destination,
        const void *Source,
        size_t Length)
{
    RtlMoveMemory(Destination, Source, Length);
    return Destination;
}
