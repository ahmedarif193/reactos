/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     File copy policy, decompression and temporary directory handling
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <apitest.h>
#include <winuser.h>
#include <winreg.h>
#include <winsvc.h>
#include <setupapi.h>
#include <strsafe.h>

static BOOL WriteTestFile(PCWSTR Path, const void *Data, DWORD Length)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Cannot create %ls: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Data, Length, &Written, NULL);
    ok(Result && Written == Length, "Cannot write %ls: %lu\n", Path, GetLastError());
    CloseHandle(File);
    return Result && Written == Length;
}

static void CheckTarget(PCWSTR Path, PCSTR Expected)
{
    HANDLE File;
    DWORD Read = 0;
    CHAR Data[16] = {0};
    BOOL Result;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (!Expected)
    {
        ok(File == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND,
           "Unexpected target file or error: %p/%lu\n", File, GetLastError());
    }
    else
    {
        ok(File != INVALID_HANDLE_VALUE, "Missing target file: %lu\n", GetLastError());
        if (File != INVALID_HANDLE_VALUE)
        {
            Result = ReadFile(File, Data, sizeof(Data) - 1, &Read, NULL);
            ok(Result && Read == strlen(Expected) && !memcmp(Data, Expected, Read),
               "Unexpected target contents: %lu bytes, '%s'\n", Read, Data);
        }
    }
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
}

struct QueueContext
{
    BOOL Overwrite;
    UINT TargetExists;
};

static UINT CALLBACK QueueCallback(PVOID Context, UINT Message, UINT_PTR Param1, UINT_PTR Param2)
{
    struct QueueContext *Queue = Context;
    if (Message == SPFILENOTIFY_TARGETEXISTS)
    {
        ++Queue->TargetExists;
        return Queue->Overwrite;
    }
    if (Message == SPFILENOTIFY_COPYERROR)
    {
        FILEPATHS_W *Paths = (FILEPATHS_W *)Param1;
        ok(FALSE, "Preserving a target reported a copy error: %u\n", Paths->Win32Error);
        return FILEOP_ABORT;
    }
    return FILEOP_DOIT;
}

START_TEST(SetupInstallFile)
{
    /* SZDD with six literal bytes, expanding to "source". */
    static const BYTE CompressedSource[] = {
        0x53, 0x5a, 0x44, 0x44, 0x88, 0xf0, 0x27, 0x33, 0x41, 0,
        6, 0, 0, 0, 0x3f, 's', 'o', 'u', 'r', 'c', 'e'};
    static const DWORD Styles[] = {SP_COPY_NOOVERWRITE, SP_COPY_FORCE_NOOVERWRITE, SP_COPY_REPLACEONLY, 0};
    WCHAR Root[MAX_PATH], Source[MAX_PATH], Target[MAX_PATH], Absent[MAX_PATH];
    WCHAR Temp[MAX_PATH], Tmp[MAX_PATH], Bad[MAX_PATH];
    DWORD Length, TempLength, TmpLength, Error, ExpectedError;
    unsigned i, InvalidTemp, SourceExists, TargetExists, Compressed;
    BOOL Result, ExpectedResult;
    HSPFILEQ Queue;
    struct QueueContext Context;

    Length = GetTempPathW(ARRAYSIZE(Root), Root);
    ok(Length && Length + 40 < ARRAYSIZE(Root), "No usable temporary path: %lu\n", Length);
    if (!Length || Length + 40 >= ARRAYSIZE(Root))
        return;
    StringCchPrintfW(Root + Length, ARRAYSIZE(Root) - Length, L"SetupCopy-%lu-%lu", GetCurrentProcessId(), GetTickCount());
    Result = CreateDirectoryW(Root, NULL);
    ok(Result, "Cannot create test directory: %lu\n", GetLastError());
    if (!Result)
        return;
    StringCchPrintfW(Source, ARRAYSIZE(Source), L"%ls\\source.txt", Root);
    StringCchPrintfW(Target, ARRAYSIZE(Target), L"%ls\\target.txt", Root);
    StringCchPrintfW(Absent, ARRAYSIZE(Absent), L"%ls\\absent.txt", Root);
    StringCchPrintfW(Bad, ARRAYSIZE(Bad), L"%ls\\missing\\temp", Root);
    TempLength = GetEnvironmentVariableW(L"TEMP", Temp, ARRAYSIZE(Temp));
    TmpLength = GetEnvironmentVariableW(L"TMP", Tmp, ARRAYSIZE(Tmp));
    ok(TempLength < ARRAYSIZE(Temp) && TmpLength < ARRAYSIZE(Tmp), "Temporary environment paths too long\n");
    if (TempLength >= ARRAYSIZE(Temp) || TmpLength >= ARRAYSIZE(Tmp))
        goto Cleanup;

    for (Compressed = 0; Compressed != 2; ++Compressed)
    {
        if (!WriteTestFile(Source, Compressed ? (const void *)CompressedSource : "source",
                           Compressed ? sizeof(CompressedSource) : 6))
            break;
        for (InvalidTemp = 0; InvalidTemp != 2; ++InvalidTemp)
        {
            Result = SetEnvironmentVariableW(L"TEMP", InvalidTemp ? Bad : Root);
            ok(Result, "Cannot set TEMP: %lu\n", GetLastError());
            Result = SetEnvironmentVariableW(L"TMP", InvalidTemp ? Bad : Root);
            ok(Result, "Cannot set TMP: %lu\n", GetLastError());
            for (SourceExists = 0; SourceExists != 2; ++SourceExists)
            for (TargetExists = 0; TargetExists != 2; ++TargetExists)
            for (i = 0; i != ARRAYSIZE(Styles); ++i)
            {
                DeleteFileW(Target);
                if (TargetExists && !WriteTestFile(Target, "target", 6))
                    continue;
                ExpectedError = !SourceExists && !(TargetExists && Styles[i] == SP_COPY_FORCE_NOOVERWRITE)
                                    ? ERROR_FILE_NOT_FOUND : ERROR_SUCCESS;
                ExpectedResult = SourceExists &&
                    (TargetExists ? !(Styles[i] & (SP_COPY_NOOVERWRITE | SP_COPY_FORCE_NOOVERWRITE))
                                  : Styles[i] != SP_COPY_REPLACEONLY);
                SetLastError(0xdeadbeef);
                Result = SetupInstallFileW(NULL, NULL, SourceExists ? Source : Absent, NULL,
                                          Target, SP_COPY_SOURCE_ABSOLUTE | Styles[i], NULL, NULL);
                Error = GetLastError();
                ok(Result == ExpectedResult && Error == ExpectedError,
                   "compressed=%u badtemp=%u source=%u target=%u style=%lx: got %u/%lu, expected %u/%lu\n",
                   Compressed, InvalidTemp, SourceExists, TargetExists, Styles[i],
                   Result, Error, ExpectedResult, ExpectedError);
                CheckTarget(Target, ExpectedResult ? "source" : TargetExists ? "target" : NULL);
            }
        }
    }

    for (i = 0; i != 3; ++i)
    {
        if (!WriteTestFile(Target, "target", 6))
            break;
        Context.Overwrite = i != 0;
        Context.TargetExists = 0;
        Queue = SetupOpenFileQueue();
        ok(Queue != INVALID_HANDLE_VALUE, "Cannot create file queue: %lu\n", GetLastError());
        if (Queue != INVALID_HANDLE_VALUE)
        {
            Result = SetupQueueCopyW(Queue, Root, NULL, L"source.txt", NULL, NULL, Root, L"target.txt",
                                     i == 2 ? SP_COPY_FORCE_NOOVERWRITE : SP_COPY_NOOVERWRITE);
            ok(Result, "Cannot queue copy: %lu\n", GetLastError());
            Result = SetupCommitFileQueueW(NULL, Queue, QueueCallback, &Context);
            ok(Result, "A preserved target must not fail the queue: %lu\n", GetLastError());
            ok(Context.TargetExists == (i == 2 ? 0 : 1), "Unexpected target-exists notifications: %u\n", Context.TargetExists);
            CheckTarget(Target, i == 1 ? "source" : "target");
            SetupCloseFileQueue(Queue);
        }
    }
    SetEnvironmentVariableW(L"TEMP", TempLength ? Temp : NULL);
    SetEnvironmentVariableW(L"TMP", TmpLength ? Tmp : NULL);

Cleanup:
    DeleteFileW(Source);
    DeleteFileW(Target);
    Result = RemoveDirectoryW(Root);
    ok(Result, "Temporary copy files leaked: %lu\n", GetLastError());
}
