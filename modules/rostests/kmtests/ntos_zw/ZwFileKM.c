/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite ZwCreateFile/ZwReadFile API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(ZwFileKM)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE FileHandle = NULL;
    FILE_STANDARD_INFORMATION StandardInfo;
    UCHAR Header[2];
    LARGE_INTEGER Offset;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, L"\\SystemRoot\\System32\\ntdll.dll");
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = ZwCreateFile(&FileHandle, FILE_GENERIC_READ, &ObjectAttributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok_eq_ulongptr(IoStatus.Information, FILE_OPENED);

    Status = ZwQueryInformationFile(FileHandle, &IoStatus, &StandardInfo, sizeof(StandardInfo), FileStandardInformation);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(StandardInfo.EndOfFile.QuadPart > 0x10000, "ntdll size %I64d\n", StandardInfo.EndOfFile.QuadPart);
    ok_bool_false(StandardInfo.Directory, "ntdll is a directory");

    Offset.QuadPart = 0;
    Status = ZwReadFile(FileHandle, NULL, NULL, NULL, &IoStatus, Header, sizeof(Header), &Offset, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulongptr(IoStatus.Information, sizeof(Header));
    ok(Header[0] == 'M' && Header[1] == 'Z', "ntdll header %02x %02x\n", Header[0], Header[1]);

    Status = ZwClose(FileHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlInitUnicodeString(&Name, L"\\SystemRoot\\System32\\KmtNoSuchFile.dll");
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateFile(&FileHandle, FILE_GENERIC_READ, &ObjectAttributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
}
