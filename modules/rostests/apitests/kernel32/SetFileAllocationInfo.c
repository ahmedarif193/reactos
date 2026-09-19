/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "precomp.h"

START_TEST(SetFileAllocationInfo)
{
    BOOL (WINAPI *SetInfo)(HANDLE, FILE_INFO_BY_HANDLE_CLASS, void *, DWORD);
    NTSTATUS (NTAPI *QueryInfo)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
    WCHAR path[MAX_PATH], name[MAX_PATH];
    FILE_ALLOCATION_INFO allocation;
    FILE_STANDARD_INFORMATION standard;
    IO_STATUS_BLOCK iosb;
    BYTE data[4096], readback[512];
    HANDLE file;
    DWORD count;
    NTSTATUS status;
    BOOL ret;

    SetInfo = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetFileInformationByHandle");
    QueryInfo = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationFile");
    if (!SetInfo || !QueryInfo)
    {
        skip("File information APIs unavailable\n");
        return;
    }
    if (!GetTempPathW(_countof(path), path) || !GetTempFileNameW(path, L"alc", 0, name))
    {
        skip("Cannot create temporary file: %lu\n", GetLastError());
        return;
    }
    file = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile failed: %lu\n", GetLastError());
    if (file == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(name);
        return;
    }

    memset(data, 0x5a, sizeof(data));
    ret = WriteFile(file, data, sizeof(data), &count, NULL);
    ok(ret && count == sizeof(data), "Write failed: %lu, %lu bytes\n", GetLastError(), count);

    allocation.AllocationSize.QuadPart = 65536;
    ret = SetInfo(file, FileAllocationInfo, &allocation, sizeof(allocation));
    ok(ret, "Preallocation failed: %lu\n", GetLastError());
    status = QueryInfo(file, &iosb, &standard, sizeof(standard), FileStandardInformation);
    ok(NT_SUCCESS(status), "Query failed: %lx\n", status);
    if (NT_SUCCESS(status))
    {
        ok(standard.EndOfFile.QuadPart == sizeof(data), "Preallocation changed EOF: %I64d\n", standard.EndOfFile.QuadPart);
        ok(standard.AllocationSize.QuadPart >= 65536, "Allocation too small: %I64d\n", standard.AllocationSize.QuadPart);
    }

    allocation.AllocationSize.QuadPart = sizeof(readback);
    ret = SetInfo(file, FileAllocationInfo, &allocation, sizeof(allocation));
    ok(ret, "Shrinking allocation failed: %lu\n", GetLastError());
    status = QueryInfo(file, &iosb, &standard, sizeof(standard), FileStandardInformation);
    ok(NT_SUCCESS(status), "Query failed: %lx\n", status);
    if (NT_SUCCESS(status))
        ok(standard.EndOfFile.QuadPart == sizeof(readback), "EOF not truncated: %I64d\n", standard.EndOfFile.QuadPart);
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    ret = ReadFile(file, readback, sizeof(readback), &count, NULL);
    ok(ret && count == sizeof(readback), "Read failed: %lu, %lu bytes\n", GetLastError(), count);
    ok(!memcmp(readback, data, sizeof(readback)), "Preallocation damaged file contents\n");

    ret = SetInfo(file, FileAllocationInfo, &allocation, sizeof(allocation) - 1);
    ok(!ret, "Short input accepted\n");
    allocation.AllocationSize.QuadPart = -1;
    ret = SetInfo(file, FileAllocationInfo, &allocation, sizeof(allocation));
    ok(!ret, "Negative allocation accepted\n");
    allocation.AllocationSize.QuadPart = 0;
    ret = SetInfo(INVALID_HANDLE_VALUE, FileAllocationInfo, &allocation, sizeof(allocation));
    ok(!ret && GetLastError() == ERROR_INVALID_HANDLE, "Invalid handle returned %d, error %lu\n", ret, GetLastError());
    CloseHandle(file);
}
