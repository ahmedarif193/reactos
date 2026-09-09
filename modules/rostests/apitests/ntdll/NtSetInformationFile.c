/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for NtSetInformationFile
 * COPYRIGHT:   Copyright 2019 Thomas Faber (thomas.faber@reactos.org)
 */

#include "precomp.h"

static VOID
TestFileAllocationInformation(VOID)
{
    WCHAR TempPath[MAX_PATH], FileName[MAX_PATH];
    HANDLE File;
    IO_STATUS_BLOCK IoStatus;
    FILE_ALLOCATION_INFORMATION Allocation;
    FILE_STANDARD_INFORMATION Standard;
    LARGE_INTEGER Position;
    NTSTATUS Status;
    BOOL Result;
    DWORD Length;

    Length = GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    if (!Length || Length >= RTL_NUMBER_OF(TempPath))
    {
        skip("No usable temporary directory\n");
        return;
    }
    if (!GetTempFileNameW(TempPath, L"nsa", 0, FileName))
    {
        skip("Cannot create temporary file: %lu\n", GetLastError());
        return;
    }
    File = CreateFileW(FileName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open temporary file: %lu\n", GetLastError());
        DeleteFileW(FileName);
        return;
    }

    Allocation.AllocationSize.QuadPart = 65536;
    Status = NtSetInformationFile(File, &IoStatus, &Allocation, sizeof(Allocation), FileAllocationInformation);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryInformationFile(File, &IoStatus, &Standard, sizeof(Standard), FileStandardInformation);
        ok_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok(Standard.AllocationSize.QuadPart >= Allocation.AllocationSize.QuadPart, "Allocation is only %I64d bytes\n", Standard.AllocationSize.QuadPart);
            ok(Standard.EndOfFile.QuadPart == 0, "Allocation changed EOF to %I64d\n", Standard.EndOfFile.QuadPart);
        }
    }

    /* Exercise the Win32 caller as well as the native information class. */
    Position.QuadPart = 4096;
    Result = SetFilePointerEx(File, Position, NULL, FILE_BEGIN);
    ok(Result, "SetFilePointerEx failed: %lu\n", GetLastError());
    if (Result)
    {
        Result = SetEndOfFile(File);
        ok(Result, "SetEndOfFile failed: %lu\n", GetLastError());
        Status = NtQueryInformationFile(File, &IoStatus, &Standard, sizeof(Standard), FileStandardInformation);
        ok_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            ok(Standard.EndOfFile.QuadPart == Position.QuadPart, "EOF is %I64d, expected %I64d\n", Standard.EndOfFile.QuadPart, Position.QuadPart);
    }
    CloseHandle(File);
}

START_TEST(NtSetInformationFile)
{
    NTSTATUS Status;

    Status = NtSetInformationFile(NULL, NULL, NULL, 0, 0);
    ok(Status == STATUS_INVALID_INFO_CLASS, "Status = %lx\n", Status);

    Status = NtSetInformationFile(NULL, NULL, NULL, 0, 0x80000000);
    ok(Status == STATUS_INVALID_INFO_CLASS, "Status = %lx\n", Status);

    TestFileAllocationInformation();
}
