/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ex/efi.c
 * PURPOSE:         I/O Functions for EFI Machines
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
NtAddBootEntry(IN PBOOT_ENTRY Entry,
               IN ULONG Id)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtAddDriverEntry(IN PEFI_DRIVER_ENTRY Entry,
                 IN ULONG Id)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtDeleteBootEntry(IN ULONG Id)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtDeleteDriverEntry(IN ULONG Id)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtEnumerateBootEntries(IN PVOID Buffer,
                       IN PULONG BufferLength)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtEnumerateDriverEntries(IN PVOID Buffer,
                        IN PULONG BufferLength)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtModifyBootEntry(IN PBOOT_ENTRY BootEntry)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtModifyDriverEntry(IN PEFI_DRIVER_ENTRY DriverEntry)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtQueryBootEntryOrder(IN PULONG Ids,
                      IN PULONG Count)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtQueryDriverEntryOrder(IN PULONG Ids,
                        IN PULONG Count)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtQueryBootOptions(IN PBOOT_OPTIONS BootOptions,
                   IN PULONG BootOptionsLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ULONG RequiredLength = sizeof(BOOT_OPTIONS);
    ULONG CapturedLength = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    /* BootOptionsLength is mandatory */
    if (BootOptionsLength == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        /* Probe the length pointer */
        if (PreviousMode != KernelMode)
            ProbeForWriteUlong(BootOptionsLength);
            
        CapturedLength = *BootOptionsLength;

        /* Handle the case where the caller is just querying the required length */
        if (BootOptions == NULL)
        {
            *BootOptionsLength = RequiredLength;
            Status = STATUS_BUFFER_TOO_SMALL;
            _SEH2_YIELD(return Status);
        }

        /* Probe caller buffer using the CAPTURED length */
        if (PreviousMode != KernelMode)
            ProbeForWrite(BootOptions, CapturedLength, sizeof(ULONG));

        /* Verify the buffer is large enough using the CAPTURED length */
        if (CapturedLength < RequiredLength)
        {
            *BootOptionsLength = RequiredLength;
            Status = STATUS_BUFFER_TOO_SMALL;
            _SEH2_YIELD(return Status);
        }

        /* Fill BOOT_OPTIONS */
        RtlZeroMemory(BootOptions, RequiredLength);
        BootOptions->Version = 1;
        BootOptions->Length = RequiredLength;
        BootOptions->Timeout = 0;
        BootOptions->CurrentBootEntryId = 0;
        BootOptions->NextBootEntryId = 0;
        BootOptions->HeadlessRedirection[0] = L'\0';

        /* Update the length parameter with the actual written size */
        *BootOptionsLength = RequiredLength;
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
NtSetBootEntryOrder(IN PULONG Ids,
                    IN PULONG Count)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetDriverEntryOrder(IN PULONG Ids,
                      IN PULONG Count)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetBootOptions(IN PBOOT_OPTIONS BootOptions,
                 IN ULONG FieldsToChange)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtTranslateFilePath(PFILE_PATH InputFilePath,
                    ULONG OutputType,
                    PFILE_PATH OutputFilePath,
                    ULONG OutputFilePathLength)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
