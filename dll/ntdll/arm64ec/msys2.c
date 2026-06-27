/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64EC fallback for the private MSYS2 current-directory hook
 */

#include <ntdllp.h>

ULONG
NTAPI
RtlGetCurrentDirectory_U_RtlpMsysDecoy(
    _In_ ULONG MaximumLength,
    _Out_writes_bytes_(MaximumLength) PWSTR Buffer)
{
    return RtlGetCurrentDirectory_U(MaximumLength, Buffer);
}
