/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Mapping NT 6+ national language support data files
 * COPYRIGHT:   Adapted from Wine dlls/ntdll/locale.c and dlls/ntdll/unix/env.c
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#define NLS_SECTION_SORTKEYS  9
#define NLS_SECTION_CASEMAP   10
#define NLS_SECTION_CODEPAGE  11
#define NLS_SECTION_NORMALIZE 12

#define NLS_NORMALIZATION_C   1
#define NLS_NORMALIZATION_D   2
#define NLS_NORMALIZATION_KC  5
#define NLS_NORMALIZATION_KD  6
#define NLS_NORMALIZATION_IDN 13

static NTSTATUS
NlsBuildCodePageFileName(
    _In_ ULONG CodePage,
    _Out_writes_(FileNameLength) PWCHAR FileName,
    _In_ USHORT FileNameLength)
{
    WCHAR DigitsBuffer[11];
    USHORT DigitsLength;
    UNICODE_STRING Digits;
    UNICODE_STRING Name;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&Name, FileName, FileNameLength * sizeof(WCHAR));
    Status = RtlAppendUnicodeToString(&Name, L"c_");
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitEmptyUnicodeString(&Digits, DigitsBuffer, sizeof(DigitsBuffer));
    Status = RtlIntegerToUnicodeString(CodePage, 10, &Digits);
    if (!NT_SUCCESS(Status)) return Status;

    DigitsLength = Digits.Length;
    while (DigitsLength < 3 * sizeof(WCHAR))
    {
        Status = RtlAppendUnicodeToString(&Name, L"0");
        if (!NT_SUCCESS(Status)) return Status;
        DigitsLength += sizeof(WCHAR);
    }

    Status = RtlAppendUnicodeStringToString(&Name, &Digits);
    if (!NT_SUCCESS(Status)) return Status;
    return RtlAppendUnicodeToString(&Name, L".nls");
}

static NTSTATUS
NlsGetFileName(
    _In_ ULONG Type,
    _In_ ULONG Id,
    _Out_writes_(FileNameLength) PWCHAR FileName,
    _In_ USHORT FileNameLength)
{
    PCWSTR Source;
    UNICODE_STRING Name;

    switch (Type)
    {
        case NLS_SECTION_SORTKEYS:
            if (Id) return STATUS_INVALID_PARAMETER_1;
            Source = L"sortdefault.nls";
            break;

        case NLS_SECTION_CASEMAP:
            if (Id) return STATUS_UNSUCCESSFUL;
            Source = L"l_intl.nls";
            break;

        case NLS_SECTION_CODEPAGE:
            return NlsBuildCodePageFileName(Id, FileName, FileNameLength);

        case NLS_SECTION_NORMALIZE:
            switch (Id)
            {
                case NLS_NORMALIZATION_C:
                    Source = L"normnfc.nls";
                    break;
                case NLS_NORMALIZATION_D:
                    Source = L"normnfd.nls";
                    break;
                case NLS_NORMALIZATION_KC:
                    Source = L"normnfkc.nls";
                    break;
                case NLS_NORMALIZATION_KD:
                    Source = L"normnfkd.nls";
                    break;
                case NLS_NORMALIZATION_IDN:
                    Source = L"normidna.nls";
                    break;
                default:
                    return STATUS_OBJECT_NAME_NOT_FOUND;
            }
            break;

        default:
            return STATUS_INVALID_PARAMETER_1;
    }

    RtlInitEmptyUnicodeString(&Name, FileName, FileNameLength * sizeof(WCHAR));
    return RtlAppendUnicodeToString(&Name, Source);
}

static NTSTATUS
NlsMapFile(
    _In_ PCWSTR FileName,
    _Out_ PVOID *BaseAddress,
    _Out_ PSIZE_T ViewSize)
{
    WCHAR PathBuffer[MAX_PATH + 32];
    UNICODE_STRING Path;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    HANDLE SectionHandle;
    SIZE_T Size = 0;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&Path, PathBuffer, sizeof(PathBuffer));
    Status = RtlAppendUnicodeToString(&Path, L"\\??\\");
    if (!NT_SUCCESS(Status)) return Status;
    Status = RtlAppendUnicodeToString(&Path, SharedUserData->NtSystemRoot);
    if (!NT_SUCCESS(Status)) return Status;
    Status = RtlAppendUnicodeToString(&Path, L"\\System32\\");
    if (!NT_SUCCESS(Status)) return Status;
    Status = RtlAppendUnicodeToString(&Path, FileName);
    if (!NT_SUCCESS(Status)) return Status;

    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&FileHandle, FILE_READ_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) return Status;

    Status = NtCreateSection(&SectionHandle, SECTION_MAP_READ, NULL, NULL, PAGE_READONLY, SEC_COMMIT, FileHandle);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status)) return Status;

    *BaseAddress = NULL;
    Status = NtMapViewOfSection(SectionHandle, NtCurrentProcess(), BaseAddress, 0, 0, NULL, &Size, ViewShare, 0, PAGE_READONLY);
    NtClose(SectionHandle);
    if (!NT_SUCCESS(Status)) return Status;

    *ViewSize = Size;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtGetNlsSectionPtr(
    _In_ ULONG Type,
    _In_ ULONG Id,
    _In_opt_ PVOID Unknown,
    _Out_ PVOID *BaseAddress,
    _Out_ PSIZE_T ViewSize)
{
    WCHAR FileName[20];
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Unknown);

    if (!BaseAddress || !ViewSize) return STATUS_INVALID_PARAMETER;
    Status = NlsGetFileName(Type, Id, FileName, RTL_NUMBER_OF(FileName));
    if (!NT_SUCCESS(Status)) return Status;
    return NlsMapFile(FileName, BaseAddress, ViewSize);
}

NTSTATUS
NTAPI
NtInitializeNlsFiles(
    _Out_ PVOID *BaseAddress,
    _Out_ PLCID DefaultLocaleId,
    _Out_opt_ PLARGE_INTEGER DefaultCasingTableSize)
{
    SIZE_T ViewSize;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DefaultCasingTableSize);

    if (!BaseAddress || !DefaultLocaleId) return STATUS_INVALID_PARAMETER;
    Status = NlsMapFile(L"locale.nls", BaseAddress, &ViewSize);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryDefaultLocale(FALSE, DefaultLocaleId);
    if (!NT_SUCCESS(Status)) NtUnmapViewOfSection(NtCurrentProcess(), *BaseAddress);
    return Status;
}

NTSTATUS
NTAPI
RtlGetLocaleFileMappingAddress(
    _Out_ PVOID *BaseAddress,
    _Out_ PLCID DefaultLocaleId,
    _Out_opt_ PLARGE_INTEGER DefaultCasingTableSize)
{
    static PVOID CachedAddress;
    static LCID CachedLocaleId;
    PVOID Address;
    NTSTATUS Status;

    if (!BaseAddress || !DefaultLocaleId) return STATUS_INVALID_PARAMETER;
    if (!CachedAddress)
    {
        Status = NtInitializeNlsFiles(&Address, &CachedLocaleId, DefaultCasingTableSize);
        if (!NT_SUCCESS(Status)) return Status;
        if (InterlockedCompareExchangePointer(&CachedAddress, Address, NULL)) NtUnmapViewOfSection(NtCurrentProcess(), Address);
    }

    *BaseAddress = CachedAddress;
    *DefaultLocaleId = CachedLocaleId;
    return STATUS_SUCCESS;
}
