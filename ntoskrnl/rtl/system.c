/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RTL system-policy and global-data services
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

static
NTSTATUS
RtlpCopySystemGlobalData(
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size,
    _In_reads_bytes_(SourceSize) const VOID *Source,
    _In_ ULONG SourceSize)
{
    if ((Buffer == NULL) || (Size != SourceSize))
        return STATUS_INFO_LENGTH_MISMATCH;
    RtlCopyMemory(Buffer, Source, SourceSize);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlGetSystemGlobalData(
    _In_ ULONG DataId,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    ULONG Value32;
    ULONGLONG Value64;

    if ((DataId == 0) || (DataId >= 19))
        return STATUS_INVALID_PARAMETER;

    switch (DataId)
    {
        case 2:
            Value64 = KeQueryInterruptTime();
            return RtlpCopySystemGlobalData(Buffer, Size, &Value64, sizeof(Value64));
        case 7:
            Value32 = NtMajorVersion;
            return RtlpCopySystemGlobalData(Buffer, Size, &Value32, sizeof(Value32));
        case 8:
            Value32 = NtMinorVersion;
            return RtlpCopySystemGlobalData(Buffer, Size, &Value32, sizeof(Value32));
        case 10:
            Value32 = KdDebuggerEnabled;
            return RtlpCopySystemGlobalData(Buffer, Size, &Value32, sizeof(Value32));
        default:
            return STATUS_NOT_FOUND;
    }
}

BOOLEAN
NTAPI
RtlIsStateSeparationEnabled(VOID)
{
    return SharedUserData->DbgStateSeparationEnabled != 0;
}

NTSTATUS
NTAPI
RtlQueryElevationFlags(
    _Out_ PULONG Flags)
{
    if (Flags == NULL)
        return STATUS_INVALID_PARAMETER;

    /* SharedDataFlags stores these policy bits one position above the public result. */
    *Flags = ((SharedUserData->SharedDataFlags >> 1) & 7) | 8;
    return STATUS_SUCCESS;
}
