/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite CcCopyWrite test user-mode part
 * PROGRAMMER:      Pierre Schweitzer <pierre@reactos.org>
 */

#include <kmt_test.h>

START_TEST(CcCopyWrite)
{
    HANDLE Handle;
    NTSTATUS Status;
    LARGE_INTEGER ByteOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PVOID Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, 4097);
    UNICODE_STRING BigFile = RTL_CONSTANT_STRING(L"\\Device\\Kmtest-CcCopyWrite\\BigFile");
    UNICODE_STRING SmallFile = RTL_CONSTANT_STRING(L"\\Device\\Kmtest-CcCopyWrite\\SmallFile");
    UNICODE_STRING VerySmallFile = RTL_CONSTANT_STRING(L"\\Device\\Kmtest-CcCopyWrite\\VerySmallFile");
    UNICODE_STRING NormalFile = RTL_CONSTANT_STRING(L"\\Device\\Kmtest-CcCopyWrite\\NormalFile");
    UNICODE_STRING BehaviourTestFile = RTL_CONSTANT_STRING(L"\\Device\\Kmtest-CcCopyWrite\\BehaviourTestFile");
    DWORD Error;
    BOOLEAN IsWin7 = GetNTVersion() == _WIN32_WINNT_WIN7;

    Error = KmtLoadAndOpenDriver(L"CcCopyWrite", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;

    InitializeObjectAttributes(&ObjectAttributes, &VerySmallFile, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_ALL_ACCESS, &ObjectAttributes, &IoStatusBlock, 0, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ByteOffset.QuadPart = 0;
    Status = NtWriteFile(Handle, NULL, NULL, NULL, &IoStatusBlock, Buffer, 62, &ByteOffset, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = NtFlushBuffersFile(Handle, &IoStatusBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);

    NtClose(Handle);

    InitializeObjectAttributes(&ObjectAttributes, &SmallFile, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_ALL_ACCESS, &ObjectAttributes, &IoStatusBlock, 0, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ByteOffset.QuadPart = 0;
    Status = NtWriteFile(Handle, NULL, NULL, NULL, &IoStatusBlock, Buffer, 512, &ByteOffset, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = NtFlushBuffersFile(Handle, &IoStatusBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);

    NtClose(Handle);

    InitializeObjectAttributes(&ObjectAttributes, &NormalFile, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_ALL_ACCESS, &ObjectAttributes, &IoStatusBlock, 0, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ByteOffset.QuadPart = 0;
    Status = NtWriteFile(Handle, NULL, NULL, NULL, &IoStatusBlock, Buffer, 1004, &ByteOffset, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = NtFlushBuffersFile(Handle, &IoStatusBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);

    NtClose(Handle);

    InitializeObjectAttributes(&ObjectAttributes, &BigFile, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_ALL_ACCESS, &ObjectAttributes, &IoStatusBlock, 0, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ByteOffset.QuadPart = 0;
    Status = NtWriteFile(Handle, NULL, NULL, NULL, &IoStatusBlock, Buffer, 4097, &ByteOffset, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = NtFlushBuffersFile(Handle, &IoStatusBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);

    NtClose(Handle);

    InitializeObjectAttributes(&ObjectAttributes, &BigFile, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_ALL_ACCESS, &ObjectAttributes, &IoStatusBlock, 0, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ByteOffset.QuadPart = 4097;
    Status = NtWriteFile(Handle, NULL, NULL, NULL, &IoStatusBlock, Buffer, 4097, &ByteOffset, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = NtFlushBuffersFile(Handle, &IoStatusBlock);
    ok_eq_hex(Status, STATUS_SUCCESS);

    NtClose(Handle);

    if (IsWin7)
    {
        skip(FALSE, "CcCopyWrite behavior path leaves Windows 7 cache state unusable after CcCopyRead\n");
    }
    else
    {
        InitializeObjectAttributes(&ObjectAttributes, &BehaviourTestFile, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Status = NtOpenFile(&Handle, FILE_ALL_ACCESS, &ObjectAttributes, &IoStatusBlock, 0, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
        ok_eq_hex(Status, STATUS_SUCCESS);

        ByteOffset.QuadPart = 4097;
        Status = NtWriteFile(Handle, NULL, NULL, NULL, &IoStatusBlock, Buffer, 4097, &ByteOffset, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        NtClose(Handle);
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
    KmtCloseDriver();
    KmtUnloadDriver();
}
