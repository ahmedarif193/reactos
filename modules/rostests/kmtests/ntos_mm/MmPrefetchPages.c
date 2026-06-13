/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-mode test for MmPrefetchPages
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include <kmt_test.h>

#define TAG_PREFETCH 'fPmK'
#define PREFETCH_PAGES 3

START_TEST(MmPrefetchPages)
{
    NTSTATUS Status;
    HANDLE FileHandle = NULL;
    PFILE_OBJECT FileObject = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\kmtest-MmPrefetch.tmp");
    LARGE_INTEGER FileOffset;
    PUCHAR Buffer;
    UCHAR ReadListBuffer[sizeof(READ_LIST) + (PREFETCH_PAGES - 1) * sizeof(FILE_SEGMENT_ELEMENT)];
    PREAD_LIST ReadList = (PREAD_LIST)ReadListBuffer;
    PREAD_LIST ReadLists[1];
    ULONG i;

    Status = MmPrefetchPages(0, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = MmPrefetchPages(1, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PREFETCH_PAGES * PAGE_SIZE, TAG_PREFETCH);
    if (skip(Buffer != NULL, "Failed to allocate scratch buffer\n"))
        return;
    RtlFillMemory(Buffer, PREFETCH_PAGES * PAGE_SIZE, 0xAB);

    InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&FileHandle, GENERIC_WRITE | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_SUPERSEDE, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (skip(NT_SUCCESS(Status) && FileHandle != NULL, "Failed to create scratch file: 0x%lx\n", Status))
    {
        ExFreePoolWithTag(Buffer, TAG_PREFETCH);
        return;
    }
    FileOffset.QuadPart = 0;
    Status = ZwWriteFile(FileHandle, NULL, NULL, NULL, &IoStatusBlock, Buffer, PREFETCH_PAGES * PAGE_SIZE, &FileOffset, NULL);
    ok(NT_SUCCESS(Status), "ZwWriteFile failed: 0x%lx\n", Status);
    ZwClose(FileHandle);
    FileHandle = NULL;
    ExFreePoolWithTag(Buffer, TAG_PREFETCH);

    InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&FileHandle, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | FILE_DELETE_ON_CLOSE, NULL, 0);
    if (skip(NT_SUCCESS(Status) && FileHandle != NULL, "Failed to reopen scratch file: 0x%lx\n", Status))
        return;

    Status = ObReferenceObjectByHandle(FileHandle, FILE_READ_DATA, *IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    if (skip(NT_SUCCESS(Status) && FileObject != NULL, "ObReferenceObjectByHandle failed: 0x%lx\n", Status))
    {
        ZwClose(FileHandle);
        return;
    }

    RtlZeroMemory(ReadList, sizeof(ReadListBuffer));
    ReadList->FileObject = NULL;
    ReadList->NumberOfEntries = 1;
    ReadList->IsImage = FALSE;
    ReadList->List[0].Alignment = 0;
    ReadLists[0] = ReadList;
    Status = MmPrefetchPages(1, ReadLists);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(ReadList, sizeof(ReadListBuffer));
    ReadList->FileObject = FileObject;
    ReadList->NumberOfEntries = PREFETCH_PAGES;
    ReadList->IsImage = FALSE;
    for (i = 0; i < PREFETCH_PAGES; i++)
        ReadList->List[i].Alignment = (ULONGLONG)i * PAGE_SIZE;
    ReadLists[0] = ReadList;
    Status = MmPrefetchPages(1, ReadLists);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ObDereferenceObject(FileObject);
    ZwClose(FileHandle);
}
