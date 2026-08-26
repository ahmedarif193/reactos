/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RTL thread-language query services
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
RtlGetThreadLangIdByIndex(
    _In_ ULONG Flags,
    _In_ ULONG Index,
    _Out_ PULONG LangId,
    _Out_opt_ PULONG LangIdCount)
{
    if ((Flags != 0) || (LangId == NULL))
        return STATUS_INVALID_PARAMETER;

    UNREFERENCED_PARAMETER(Index);

    *LangId = 0;
    if (LangIdCount != NULL)
        *LangIdCount = 0;
    return STATUS_NOT_FOUND;
}
