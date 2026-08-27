/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Executive boot-mode and license queries
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS **********************************************************/

BOOLEAN
NTAPI
ExIsManufacturingModeEnabled(VOID)
{
    return FALSE;
}

BOOLEAN
NTAPI
ExIsSoftBoot(VOID)
{
    return FALSE;
}

BOOLEAN
NTAPI
ExQueryFastCacheDevLicense(VOID)
{
    return FALSE;
}

NTSTATUS
NTAPI
ZwQueryLicenseValue(
    _In_ PCUNICODE_STRING ValueName,
    _Out_opt_ PULONG Type,
    _Out_writes_bytes_to_opt_(DataSize, *ResultDataSize) PVOID Data,
    _In_ ULONG DataSize,
    _Out_ PULONG ResultDataSize)
{
    static const UNICODE_STRING LicenseKeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ProductOptions\\LicenseInformation");
    PKEY_VALUE_PARTIAL_INFORMATION ValueInformation;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG AllocationSize;
    ULONG ReturnLength;
    HANDLE KeyHandle;
    NTSTATUS Status;

    if ((ValueName == NULL) || (ValueName->Buffer == NULL) ||
        (ValueName->Length == 0) || (ResultDataSize == NULL) ||
        ((DataSize != 0) && (Data == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DataSize > MAXULONG - FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data))
        return STATUS_INTEGER_OVERFLOW;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)&LicenseKeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    AllocationSize = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + DataSize;
    ValueInformation = ExAllocatePoolWithTag(PagedPool, AllocationSize, 'ciLE');
    if (ValueInformation == NULL)
    {
        ZwClose(KeyHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(KeyHandle, (PUNICODE_STRING)ValueName, KeyValuePartialInformation, ValueInformation, AllocationSize, &ReturnLength);
    ZwClose(KeyHandle);
    if (NT_SUCCESS(Status) || (Status == STATUS_BUFFER_OVERFLOW) || (Status == STATUS_BUFFER_TOO_SMALL))
    {
        if (Type != NULL)
            *Type = ValueInformation->Type;
        *ResultDataSize = ValueInformation->DataLength;
        if (NT_SUCCESS(Status))
            RtlCopyMemory(Data, ValueInformation->Data, ValueInformation->DataLength);
        else
            Status = STATUS_BUFFER_TOO_SMALL;
    }

    ExFreePoolWithTag(ValueInformation, 'ciLE');
    return Status;
}
