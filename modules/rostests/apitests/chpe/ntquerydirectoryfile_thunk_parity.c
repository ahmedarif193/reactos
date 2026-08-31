/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 NtQueryDirectoryFile export-thunk parity test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define STATUS_SUCCESS ((NTSTATUS)0x00000000)

typedef NTSTATUS (NTAPI *PNT_QUERY_DIRECTORY_FILE)(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan);
typedef BOOLEAN (WINAPI *PRTL_IS_EC_CODE)(ULONG_PTR Address);

typedef struct _FILE_NAMES_INFORMATION_LOCAL
{
    ULONG NextEntryOffset;
    ULONG FileIndex;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAMES_INFORMATION_LOCAL, *PFILE_NAMES_INFORMATION_LOCAL;

static int
log_result(PCSTR Format, ...)
{
    CHAR Buffer[512];
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);

    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    OutputDebugStringA(Buffer);
    return Length;
}

#define printf log_result

static BOOL
create_empty_file(PCWSTR Path)
{
    HANDLE File;

    File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    CloseHandle(File);
    return TRUE;
}

static BOOL
name_equals(PFILE_NAMES_INFORMATION_LOCAL Entry, PCWSTR Expected)
{
    SIZE_T ExpectedLength = wcslen(Expected) * sizeof(WCHAR);

    return Entry->FileNameLength == ExpectedLength && !memcmp(Entry->FileName, Expected, ExpectedLength);
}

int
main(void)
{
    static const WCHAR DirectoryName[] = L"chpe_ntdir_case";
    static const WCHAR AlphaPath[] = L"chpe_ntdir_case\\alpha.txt";
    static const WCHAR BetaPath[] = L"chpe_ntdir_case\\beta.bin";
    HMODULE NtDll;
    PNT_QUERY_DIRECTORY_FILE NtQueryDirectoryFileDynamic;
    PRTL_IS_EC_CODE RtlIsEcCode;
    HANDLE Directory;
    IO_STATUS_BLOCK IoStatus;
    BYTE Buffer[1024];
    PFILE_NAMES_INFORMATION_LOCAL Entry;
    UNICODE_STRING Pattern;
    NTSTATUS Status, FilterStatus;
    BOOL FoundAlpha = FALSE, FoundBeta = FALSE, FilteredAlpha = FALSE;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_NTQUERYDIRECTORYFILE_TEST_BEGIN\n");

    NtDll = GetModuleHandleW(L"ntdll.dll");
    NtQueryDirectoryFileDynamic = NtDll ? (PNT_QUERY_DIRECTORY_FILE)GetProcAddress(NtDll, "NtQueryDirectoryFile") : NULL;
    RtlIsEcCode = NtDll ? (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode") : NULL;
    if (!NtQueryDirectoryFileDynamic)
    {
        printf("FAIL GetProcAddress NtQueryDirectoryFile error=%lu\n", GetLastError());
        return 1;
    }

    RemoveDirectoryW(DirectoryName);
    if (!CreateDirectoryW(DirectoryName, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        printf("FAIL CreateDirectory error=%lu\n", GetLastError());
        return 2;
    }

    if (!create_empty_file(AlphaPath) || !create_empty_file(BetaPath))
    {
        printf("FAIL CreateFile error=%lu\n", GetLastError());
        return 3;
    }

    Directory = CreateFileW(DirectoryName, FILE_LIST_DIRECTORY | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (Directory == INVALID_HANDLE_VALUE)
    {
        printf("FAIL OpenDirectory error=%lu\n", GetLastError());
        return 4;
    }

    ZeroMemory(Buffer, sizeof(Buffer));
    Status = NtQueryDirectoryFileDynamic(Directory, NULL, NULL, NULL, &IoStatus, Buffer, sizeof(Buffer), FileNamesInformation, FALSE, NULL, TRUE);
    if (Status == STATUS_SUCCESS)
    {
        Entry = (PFILE_NAMES_INFORMATION_LOCAL)Buffer;
        for (;;)
        {
            if (name_equals(Entry, L"alpha.txt")) FoundAlpha = TRUE;
            if (name_equals(Entry, L"beta.bin")) FoundBeta = TRUE;
            if (!Entry->NextEntryOffset) break;
            Entry = (PFILE_NAMES_INFORMATION_LOCAL)((PBYTE)Entry + Entry->NextEntryOffset);
        }
    }

    Pattern.Buffer = L"alpha*";
    Pattern.Length = 6 * sizeof(WCHAR);
    Pattern.MaximumLength = Pattern.Length + sizeof(WCHAR);
    ZeroMemory(Buffer, sizeof(Buffer));
    FilterStatus = NtQueryDirectoryFileDynamic(Directory, NULL, NULL, NULL, &IoStatus, Buffer, sizeof(Buffer), FileNamesInformation, TRUE, &Pattern, TRUE);
    if (FilterStatus == STATUS_SUCCESS) FilteredAlpha = name_equals((PFILE_NAMES_INFORMATION_LOCAL)Buffer, L"alpha.txt");

    printf("NTQUERYDIRECTORYFILE export=1 ec=%d status=0x%08lx alpha=%d beta=%d filter_status=0x%08lx filtered_alpha=%d\n", RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)NtQueryDirectoryFileDynamic) : -1, Status, FoundAlpha, FoundBeta, FilterStatus, FilteredAlpha);

    CloseHandle(Directory);
    DeleteFileW(AlphaPath);
    DeleteFileW(BetaPath);
    RemoveDirectoryW(DirectoryName);

    if (Status != STATUS_SUCCESS || !FoundAlpha || !FoundBeta || FilterStatus != STATUS_SUCCESS || !FilteredAlpha)
    {
        printf("CHPE_NTQUERYDIRECTORYFILE_TEST_FAIL\n");
        return 5;
    }

    printf("CHPE_NTQUERYDIRECTORYFILE_TEST_PASS\n");
    return 0;
}
