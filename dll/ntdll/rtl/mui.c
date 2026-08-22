/*
 * PROJECT:     ReactOS NT User-Mode DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Preferred user-interface language queries
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntdll.h>
#include <winnls.h>

#define NDEBUG
#include <debug.h>

typedef struct _RTL_MUI_REGISTRY_VALUE
{
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    PCWSTR Languages;
    ULONG CharacterCount;
} RTL_MUI_REGISTRY_VALUE, *PRTL_MUI_REGISTRY_VALUE;

static NTSTATUS
RtlpOpenUserLanguageKey(PHANDLE KeyHandle)
{
    static const UNICODE_STRING LanguageKeyName = RTL_CONSTANT_STRING(L"Control Panel\\International\\User Profile");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE CurrentUser;
    NTSTATUS Status;

    Status = RtlOpenCurrentUser(KEY_QUERY_VALUE, &CurrentUser);
    if (!NT_SUCCESS(Status)) return Status;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)&LanguageKeyName, OBJ_CASE_INSENSITIVE, CurrentUser, NULL);
    Status = NtOpenKey(KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    NtClose(CurrentUser);
    return Status;
}

static VOID
RtlpFreeRegistryLanguages(PRTL_MUI_REGISTRY_VALUE Value)
{
    if (Value->Information) RtlFreeHeap(RtlGetProcessHeap(), 0, Value->Information);
    RtlZeroMemory(Value, sizeof(*Value));
}

static NTSTATUS
RtlpReadRegistryLanguages(PRTL_MUI_REGISTRY_VALUE Value)
{
    static const UNICODE_STRING LanguageValueName = RTL_CONSTANT_STRING(L"Languages");
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    HANDLE KeyHandle;
    ULONG ReturnLength;
    NTSTATUS Status;

    RtlZeroMemory(Value, sizeof(*Value));
    Status = RtlpOpenUserLanguageKey(&KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    ReturnLength = 0;
    Status = NtQueryValueKey(KeyHandle, (PUNICODE_STRING)&LanguageValueName, KeyValuePartialInformation, NULL, 0, &ReturnLength);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
    {
        NtClose(KeyHandle);
        return Status;
    }

    Information = RtlAllocateHeap(RtlGetProcessHeap(), 0, ReturnLength);
    if (!Information)
    {
        NtClose(KeyHandle);
        return STATUS_NO_MEMORY;
    }

    Status = NtQueryValueKey(KeyHandle, (PUNICODE_STRING)&LanguageValueName, KeyValuePartialInformation, Information, ReturnLength, &ReturnLength);
    NtClose(KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Information);
        return Status;
    }

    if (Information->Type != REG_MULTI_SZ || Information->DataLength < 2 * sizeof(WCHAR) || Information->DataLength % sizeof(WCHAR))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Information);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    Value->Information = Information;
    Value->Languages = (PCWSTR)Information->Data;
    Value->CharacterCount = Information->DataLength / sizeof(WCHAR);
    if (Value->Languages[Value->CharacterCount - 1] || Value->Languages[Value->CharacterCount - 2])
    {
        RtlpFreeRegistryLanguages(Value);
        return STATUS_REGISTRY_CORRUPT;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpFormatLanguageId(PCWSTR LanguageName, PWSTR Buffer, PULONG Length)
{
    static const WCHAR HexDigits[] = L"0123456789ABCDEF";
    LCID Locale;
    LANGID LanguageId;
    NTSTATUS Status;

    Status = RtlLocaleNameToLcid(LanguageName, &Locale, RTL_LOCALE_ALLOW_NEUTRAL_NAMES);
    if (!NT_SUCCESS(Status)) return Status;

    LanguageId = LANGIDFROMLCID(Locale);
    Buffer[0] = HexDigits[(LanguageId >> 12) & 0xf];
    Buffer[1] = HexDigits[(LanguageId >> 8) & 0xf];
    Buffer[2] = HexDigits[(LanguageId >> 4) & 0xf];
    Buffer[3] = HexDigits[LanguageId & 0xf];
    Buffer[4] = UNICODE_NULL;
    *Length = 4;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpFormatLanguageName(PCWSTR LanguageName, PWSTR Buffer, PULONG Length)
{
    UNICODE_STRING LocaleName;
    LCID Locale;
    NTSTATUS Status;

    Status = RtlLocaleNameToLcid(LanguageName, &Locale, RTL_LOCALE_ALLOW_NEUTRAL_NAMES);
    if (!NT_SUCCESS(Status)) return Status;

    LocaleName.Buffer = Buffer;
    LocaleName.Length = 0;
    LocaleName.MaximumLength = LOCALE_NAME_MAX_LENGTH * sizeof(WCHAR);
    Status = RtlLcidToLocaleName(Locale, &LocaleName, RTL_LOCALE_ALLOW_NEUTRAL_NAMES, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    *Length = LocaleName.Length / sizeof(WCHAR);
    Buffer[*Length] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpWritePreferredLanguages(DWORD Flags, PCWSTR Languages, ULONG LanguageCharacters, PULONG Count, PWSTR Buffer, PULONG Size)
{
    WCHAR Converted[LOCALE_NAME_MAX_LENGTH];
    PCWSTR Current;
    ULONG Available;
    ULONG ConvertedLength;
    ULONG LanguageCount = 0;
    ULONG RequiredSize = 1;
    NTSTATUS Status;

    Current = Languages;
    Available = LanguageCharacters;
    while (Available && *Current)
    {
        ULONG InputLength = 0;

        while (InputLength < Available && Current[InputLength]) ++InputLength;
        if (InputLength == Available) return STATUS_REGISTRY_CORRUPT;

        if (Flags & MUI_LANGUAGE_ID) Status = RtlpFormatLanguageId(Current, Converted, &ConvertedLength);
        else Status = RtlpFormatLanguageName(Current, Converted, &ConvertedLength);
        if (NT_SUCCESS(Status))
        {
            ++LanguageCount;
            RequiredSize += ConvertedLength + 1;
        }

        Current += InputLength + 1;
        Available -= InputLength + 1;
    }

    if (!LanguageCount) return STATUS_NOT_FOUND;
    if (Buffer && *Size) Buffer[0] = UNICODE_NULL;
    if (Buffer && *Size < RequiredSize)
    {
        *Size = RequiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Buffer)
    {
        PWSTR Output = Buffer;

        Current = Languages;
        Available = LanguageCharacters;
        while (Available && *Current)
        {
            ULONG InputLength = 0;

            while (InputLength < Available && Current[InputLength]) ++InputLength;
            if (Flags & MUI_LANGUAGE_ID) Status = RtlpFormatLanguageId(Current, Converted, &ConvertedLength);
            else Status = RtlpFormatLanguageName(Current, Converted, &ConvertedLength);
            if (NT_SUCCESS(Status))
            {
                RtlCopyMemory(Output, Converted, (ConvertedLength + 1) * sizeof(WCHAR));
                Output += ConvertedLength + 1;
            }
            Current += InputLength + 1;
            Available -= InputLength + 1;
        }
        *Output = UNICODE_NULL;
    }

    *Count = LanguageCount;
    *Size = RequiredSize;
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpGetDefaultPreferredLanguage(DWORD Flags, PULONG Count, PWSTR Buffer, PULONG Size)
{
    WCHAR Languages[LOCALE_NAME_MAX_LENGTH + 1];
    UNICODE_STRING LocaleName;
    LANGID LanguageId;
    NTSTATUS Status;

    Status = NtQueryDefaultUILanguage(&LanguageId);
    if (!NT_SUCCESS(Status)) return Status;

    LocaleName.Buffer = Languages;
    LocaleName.Length = 0;
    LocaleName.MaximumLength = LOCALE_NAME_MAX_LENGTH * sizeof(WCHAR);
    Status = RtlLcidToLocaleName(LanguageId, &LocaleName, RTL_LOCALE_ALLOW_NEUTRAL_NAMES, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    Languages[LocaleName.Length / sizeof(WCHAR)] = UNICODE_NULL;
    Languages[LocaleName.Length / sizeof(WCHAR) + 1] = UNICODE_NULL;
    return RtlpWritePreferredLanguages(Flags, Languages, LocaleName.Length / sizeof(WCHAR) + 2, Count, Buffer, Size);
}

NTSTATUS
NTAPI
RtlGetUserPreferredUILanguages(DWORD Flags, ULONG Reserved, PULONG Count, PWSTR Buffer, PULONG Size)
{
    RTL_MUI_REGISTRY_VALUE Value;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Reserved);

    if (!Count || !Size || (Flags & ~(MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID)) || ((Flags & MUI_LANGUAGE_NAME) && (Flags & MUI_LANGUAGE_ID)) || (*Size && !Buffer)) return STATUS_INVALID_PARAMETER;

    Status = RtlpReadRegistryLanguages(&Value);
    if (NT_SUCCESS(Status))
    {
        Status = RtlpWritePreferredLanguages(Flags, Value.Languages, Value.CharacterCount, Count, Buffer, Size);
        RtlpFreeRegistryLanguages(&Value);
        if (Status != STATUS_NOT_FOUND) return Status;
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND && Status != STATUS_OBJECT_PATH_NOT_FOUND && Status != STATUS_OBJECT_TYPE_MISMATCH && Status != STATUS_REGISTRY_CORRUPT)
    {
        return Status;
    }

    return RtlpGetDefaultPreferredLanguage(Flags, Count, Buffer, Size);
}
