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

/* GLOBALS *******************************************************************/

/* Captured from the loader block during Phase 0 (see ExpInitSystemPhase0) */
FIRMWARE_TYPE ExpFirmwareType = FirmwareTypeUnknown;

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
FIRMWARE_TYPE
NTAPI
ExGetFirmwareType(VOID)
{
    return ExpFirmwareType;
}

/*
 * @implemented
 *
 * Fetches a single firmware table identified by (provider signature, table id).
 * Only the raw SMBIOS (RSMB) provider is backed by real data; other providers
 * are not registered and report gracefully instead of failing hard.
 */
NTSTATUS
NTAPI
ExGetSystemFirmwareTable(IN ULONG FirmwareTableProviderSignature,
                         IN ULONG FirmwareTableID,
                         OUT PVOID FirmwareTableBuffer OPTIONAL,
                         IN ULONG BufferLength,
                         OUT PULONG ReturnLength OPTIONAL)
{
    NTSTATUS Status;
    ULONG DataSize = 0;

    UNREFERENCED_PARAMETER(FirmwareTableID);

    if (ReturnLength) *ReturnLength = 0;

    switch (FirmwareTableProviderSignature)
    {
        case SIG_RSMB:
        {
            /* One call retrieves the size and, when the buffer fits, the table */
            Status = ExpGetRawSMBiosTable(FirmwareTableBuffer, &DataSize, BufferLength);
            if (!NT_SUCCESS(Status) && (Status != STATUS_BUFFER_TOO_SMALL))
                return Status;
            if (DataSize == 0) return STATUS_NOT_FOUND;

            if (ReturnLength) *ReturnLength = DataSize;

            /* Report the required size when the buffer can't hold the table */
            if ((FirmwareTableBuffer == NULL) || (BufferLength < DataSize))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            return Status;
        }

        default:
            /* No provider registered for this signature */
            return STATUS_NOT_IMPLEMENTED;
    }
}

/*
 * @unimplemented
 *
 * ARM64 UEFI runtime-services variable access is not yet wired through the HAL,
 * so there is no real firmware variable path to route to. Parameters are still
 * validated so callers get a deterministic result.
 */
NTSTATUS
NTAPI
ExGetFirmwareEnvironmentVariable(IN PUNICODE_STRING VariableName,
                                 IN LPGUID VendorGuid,
                                 OUT PVOID Value OPTIONAL,
                                 IN OUT PULONG ValueLength,
                                 OUT PULONG Attributes OPTIONAL)
{
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(Attributes);

    if ((VariableName == NULL) || (VariableName->Buffer == NULL) ||
        (VendorGuid == NULL) || (ValueLength == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_IMPLEMENTED;
}

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
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
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
