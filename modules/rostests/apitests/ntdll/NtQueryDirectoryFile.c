/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Directory-query cursor and end-of-enumeration regression test
 */

#include "precomp.h"

static VOID
TestMissingDirectoryEntry(VOID)
{
    UNICODE_STRING Directory = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32");
    UNICODE_STRING Filters[] =
    {
        RTL_CONSTANT_STRING(L"__reactos_query_directory_missing__.inf"),
        RTL_CONSTANT_STRING(L"__reactos_query_directory_missing__*.inf")
    };
    FILE_INFORMATION_CLASS Classes[] =
    {
        FileDirectoryInformation,
        FileFullDirectoryInformation,
        FileBothDirectoryInformation
    };
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Buffer[256];
    ULONG i, j, k;

    InitializeObjectAttributes(&Attributes, &Directory, OBJ_CASE_INSENSITIVE, NULL, NULL);
    for (i = 0; i < RTL_NUMBER_OF(Filters); ++i)
    {
        for (j = 0; j < RTL_NUMBER_OF(Classes); ++j)
        {
            Status = NtOpenFile(&Handle, FILE_LIST_DIRECTORY | SYNCHRONIZE, &Attributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
            ok_hex(Status, STATUS_SUCCESS);
            if (!NT_SUCCESS(Status))
                continue;

            /* Only the first query reports NO_SUCH_FILE. Continuing or
             * restarting the same unsuccessful scan reports NO_MORE_FILES. */
            for (k = 0; k < 3; ++k)
            {
                Status = NtQueryDirectoryFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, sizeof(Buffer), Classes[j], TRUE, k == 0 ? &Filters[i] : NULL, k != 1);
                ok(Status == (k == 0 ? STATUS_NO_SUCH_FILE : STATUS_NO_MORE_FILES), "Filter %lu, class %u, query %lu: status %08lx\n", i, Classes[j], k, Status);
            }
            NtClose(Handle);
        }
    }
}

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
    TestMissingDirectoryEntry();
}
