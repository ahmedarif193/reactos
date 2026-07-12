/*
 * PROJECT:     ReactOS Kernel32.dll
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GetLogicalProcessorInformationEx
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#include "k32_vista.h"

#include <ndk/exfuncs.h>

/*
 * @implemented
 */
BOOL
WINAPI
GetLogicalProcessorInformationEx(
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
    _Out_writes_bytes_to_opt_(*ReturnedLength, *ReturnedLength)
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer,
    _Inout_ PDWORD ReturnedLength)
{
    NTSTATUS Status;
    ULONG Length;

    if (ReturnedLength == NULL)
    {
        BaseSetLastNTError(STATUS_INVALID_PARAMETER);
        return FALSE;
    }

    Length = *ReturnedLength;
    Status = NtQuerySystemInformationEx(SystemLogicalProcessorAndGroupInformation,
                                        &RelationshipType,
                                        sizeof(RelationshipType),
                                        Buffer,
                                        Length,
                                        &Length);
    *ReturnedLength = Length;

    if (!NT_SUCCESS(Status))
    {
        /* callers grow their buffer on ERROR_INSUFFICIENT_BUFFER */
        if (Status == STATUS_INFO_LENGTH_MISMATCH)
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        else
            BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}
