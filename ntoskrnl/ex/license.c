/*
 * PROJECT:         LiberNT Operating System
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

static
NTSTATUS
ExpQueryLicenseValue(
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

NTSTATUS
NTAPI
NtQueryLicenseValue(
    _In_ PUNICODE_STRING ValueName,
    _Out_opt_ PULONG Type,
    _Out_writes_bytes_to_opt_(DataSize, *ResultDataSize) PVOID Data,
    _In_ ULONG DataSize,
    _Out_ PULONG ResultDataSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UNICODE_STRING CapturedName;
    PVOID Buffer = NULL;
    ULONG CapturedType = 0;
    ULONG CapturedSize = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode == KernelMode)
        return ExpQueryLicenseValue(ValueName, Type, Data, DataSize, ResultDataSize);

    if (ValueName == NULL || ResultDataSize == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        ProbeForWriteUlong(ResultDataSize);
        if (Type != NULL)
            ProbeForWriteUlong(Type);
        if (DataSize != 0)
            ProbeForWrite(Data, DataSize, 1);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = ProbeAndCaptureUnicodeString(&CapturedName, PreviousMode, ValueName);
    if (!NT_SUCCESS(Status))
        return Status;

    if (CapturedName.Buffer == NULL || CapturedName.Length == 0)
    {
        ReleaseCapturedUnicodeString(&CapturedName, PreviousMode);
        return STATUS_INVALID_PARAMETER;
    }

    if (DataSize != 0)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, DataSize, 'ciLE');
        if (Buffer == NULL)
        {
            ReleaseCapturedUnicodeString(&CapturedName, PreviousMode);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    Status = ExpQueryLicenseValue(&CapturedName, &CapturedType, Buffer, DataSize, &CapturedSize);
    ReleaseCapturedUnicodeString(&CapturedName, PreviousMode);

    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
    {
        _SEH2_TRY
        {
            if (Type != NULL)
                *Type = CapturedType;
            *ResultDataSize = CapturedSize;
            if (NT_SUCCESS(Status))
                RtlCopyMemory(Data, Buffer, CapturedSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    if (Buffer != NULL)
        ExFreePoolWithTag(Buffer, 'ciLE');
    return Status;
}
