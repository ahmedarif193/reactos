/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC probed copies to and from user memory
 */

#include <ntoskrnl.h>

NTSTATUS
NTAPI
KiPpcCopyFromUser(PVOID Destination, const VOID *Source, SIZE_T Length)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Length)
        return STATUS_SUCCESS;
    if (((ULONG_PTR)Source + Length < (ULONG_PTR)Source) || ((ULONG_PTR)Source + Length > (ULONG_PTR)MmUserProbeAddress))
        return STATUS_ACCESS_VIOLATION;

    _SEH2_TRY
    {
        RtlCopyMemory(Destination, Source, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

NTSTATUS
NTAPI
KiPpcCopyToUser(PVOID Destination, const VOID *Source, SIZE_T Length)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Length)
        return STATUS_SUCCESS;
    if (((ULONG_PTR)Destination + Length < (ULONG_PTR)Destination) || ((ULONG_PTR)Destination + Length > (ULONG_PTR)MmUserProbeAddress))
        return STATUS_ACCESS_VIOLATION;

    _SEH2_TRY
    {
        RtlCopyMemory(Destination, Source, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}
