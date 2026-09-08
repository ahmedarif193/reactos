/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Directory-query cursor and end-of-enumeration regression test
 */

#include "precomp.h"

START_TEST(NtQueryDirectoryFile)
{
    UNICODE_STRING Directory = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32");
    UNICODE_STRING Filter = RTL_CONSTANT_STRING(L"ntdll.dll");
    UNICODE_STRING Found;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG i;
    union
    {
        FILE_DIRECTORY_INFORMATION Information;
        UCHAR Bytes[512];
    } Buffer;

    InitializeObjectAttributes(&Attributes, &Directory, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, FILE_LIST_DIRECTORY | SYNCHRONIZE,
                        &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    /* An exact-name search has one result. A lost directory cursor repeats
     * that result forever; a lost completion status hides NO_MORE_FILES. */
    for (i = 0; i < 4; ++i)
    {
        RtlZeroMemory(&Buffer, sizeof(Buffer));
        Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus,
                                      &Buffer, sizeof(Buffer), FileDirectoryInformation,
                                      TRUE, i == 0 ? &Filter : NULL, i == 0 || i == 2);
        if (i == 0 || i == 2)
        {
            ok_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok(Buffer.Information.FileNameLength == Filter.Length,
                   "Name length %lu, expected %u\n", Buffer.Information.FileNameLength, Filter.Length);
                if (Buffer.Information.FileNameLength == Filter.Length)
                {
                    Found.Length = Found.MaximumLength = Filter.Length;
                    Found.Buffer = Buffer.Information.FileName;
                    ok(RtlEqualUnicodeString(&Found, &Filter, TRUE), "Unexpected filename\n");
                }
            }
        }
        else
        {
            ok_hex(Status, STATUS_NO_MORE_FILES);
        }
    }
    NtClose(Handle);
}
