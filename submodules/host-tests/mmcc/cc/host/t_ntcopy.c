/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/cc/host/t_ntcopy.c
 * PURPOSE:     NT cached copy host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccharness.h"
#include <cc/api/ccnt.h>

static NTSTATUS
NtCopyReadStatus(PFILE_OBJECT File, PLARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait, PVOID Buffer,
                  PIO_STATUS_BLOCK IoStatus, PBOOLEAN Result)
{
    volatile NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        *Result = CcCopyRead(File, Offset, Length, Wait, Buffer, IoStatus);
    }
    _SEH2_EXCEPT(1)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

void
TestNtCopy(void)
{
    CC_NT_MAP Map = { 0 };
    TEST_FILE Backing;
    CC_CACHE Cache;
    SECTION_OBJECT_POINTERS Pointers = { &Map };
    FILE_OBJECT File = { .SectionObjectPointer = &Pointers };
    LARGE_INTEGER Offset = { .QuadPart = 0 };
    IO_STATUS_BLOCK IoStatus;
    UCHAR Buffer[10];
    BOOLEAN Result = 0xFF;
    const ULONG64 FileSize = 2 * CC_VIEW_SIZE;

    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    FileCreate(&Backing, FileSize);
    CcMapInitialize(&Map.Map, &Cache, &Backing.Ops, &Backing, FileSize, FileSize, FileSize);
    Map.ReferenceCount = 1;

    CHECK(NtCopyReadStatus(&File, NULL, 0, FALSE, NULL, &IoStatus, &Result) == STATUS_ACCESS_VIOLATION);
    CHECK(Result == 0xFF && Map.ReferenceCount == 1 && Backing.DiskReads == 0);
    CHECK(NtCopyReadStatus(NULL, &Offset, 10, FALSE, Buffer, &IoStatus, &Result) == STATUS_ACCESS_VIOLATION);
    CHECK(Result == 0xFF && Map.ReferenceCount == 1 && Backing.DiskReads == 0);

    CHECK(NtCopyReadStatus(&File, &Offset, 0, TRUE, NULL, &IoStatus, &Result) == STATUS_SUCCESS);
    CHECK(Result && IoStatus.Status == STATUS_SUCCESS && IoStatus.Information == 0);
    CHECK(Map.ReferenceCount == 1 && Backing.DiskReads == 0);

    Result = 0xFF;
    memset(&IoStatus, 0xAB, sizeof(IoStatus));
    CHECK(NtCopyReadStatus(&File, &Offset, 10, TRUE, NULL, &IoStatus, &Result) == STATUS_INVALID_USER_BUFFER);
    CHECK(Result == 0xFF && (ULONG)IoStatus.Status == 0xABABABAB);
    CHECK(IoStatus.Information == (ULONG_PTR)0xABABABABABABABABULL);
    CHECK(Map.ReferenceCount == 1 && Backing.DiskReads == 1);

    CHECK(NtCopyReadStatus(&File, &Offset, 10, FALSE, Buffer, &IoStatus, &Result) == STATUS_SUCCESS);
    CHECK(Result && IoStatus.Status == STATUS_SUCCESS && IoStatus.Information == 10);
    CHECK(Map.ReferenceCount == 1 && Backing.DiskReads == 1);
    CHECK(memcmp(Buffer, Backing.Disk, sizeof(Buffer)) == 0);

    Offset.QuadPart = PAGE_SIZE;
    memset(&IoStatus, 0xAB, sizeof(IoStatus));
    CHECK(NtCopyReadStatus(&File, &Offset, 10, FALSE, Buffer, &IoStatus, &Result) == STATUS_SUCCESS);
    CHECK(!Result && (ULONG)IoStatus.Status == 0xABABABAB);
    CHECK(IoStatus.Information == (ULONG_PTR)0xABABABABABABABABULL);
    CHECK(Backing.PrefetchCalls == 1 && Backing.PrefetchOffset == PAGE_SIZE && Backing.PrefetchLength == 10);
    CHECK(Backing.DiskReads == 2 && Backing.Resident[1] == 2);
    CHECK(Map.ReferenceCount == 1);

    Offset.QuadPart = PAGE_SIZE * 2;
    CHECK(NtCopyReadStatus(&File, &Offset, 10, TRUE, Buffer, &IoStatus, &Result) == STATUS_SUCCESS);
    CHECK(Result && IoStatus.Status == STATUS_SUCCESS && IoStatus.Information == 10);
    CHECK(Map.ReferenceCount == 1);
    Result = 0xFF;
    CHECK(NtCopyReadStatus(&File, &Offset, 10, TRUE, Buffer, NULL, &Result) == STATUS_ACCESS_VIOLATION);
    CHECK(Result == 0xFF && Map.ReferenceCount == 1);

    Offset.QuadPart = PAGE_SIZE * 3;
    memset(Buffer, 0xA5, sizeof(Buffer));
    CHECK(!CcCopyWrite(&File, &Offset, sizeof(Buffer), FALSE, Buffer));
    CHECK(Backing.PrefetchCalls == 2 && Backing.PrefetchOffset == (ULONG64)Offset.QuadPart);
    CHECK(Backing.PrefetchLength == sizeof(Buffer) && Backing.Resident[3] == 2);
    CHECK(Backing.Pages[Offset.QuadPart] == Backing.Disk[Offset.QuadPart] && Backing.Dirty[3] == 0);
    CHECK(CcCopyWrite(&File, &Offset, sizeof(Buffer), FALSE, Buffer));
    CHECK(memcmp(Backing.Pages + Offset.QuadPart, Buffer, sizeof(Buffer)) == 0);
    CHECK(Backing.PrefetchCalls == 2 && Backing.Dirty[3] == 1);

    Offset.QuadPart = PAGE_SIZE * 4;
    Backing.DeferPrefetch = TRUE;
    memset(&IoStatus, 0xAB, sizeof(IoStatus));
    CHECK(NtCopyReadStatus(&File, &Offset, sizeof(Buffer), FALSE, Buffer, &IoStatus, &Result) == STATUS_SUCCESS);
    CHECK(!Result && Backing.PrefetchCalls == 3 && Backing.Resident[4] == 0);
    CHECK(Buffer[0] == 0xA5 && (ULONG)IoStatus.Status == 0xABABABAB);
    CHECK(IoStatus.Information == (ULONG_PTR)0xABABABABABABABABULL && Map.ReferenceCount == 1);
    Backing.DeferPrefetch = FALSE;
    Backing.PrefetchStatus = STATUS_UNEXPECTED_IO_ERROR;
    CHECK(!CcCopyWrite(&File, &Offset, sizeof(Buffer), FALSE, Buffer));
    CHECK(Backing.PrefetchCalls == 4 && Backing.Resident[4] == 0 && Backing.Dirty[4] == 0);
    CHECK(Map.ReferenceCount == 1 && Map.Map.ValidDataLength == FileSize);
    Backing.PrefetchStatus = STATUS_SUCCESS;
    CHECK(CcCopyWrite(&File, &Offset, 0, FALSE, NULL));
    CHECK(Backing.PrefetchCalls == 4 && Backing.Resident[4] == 0);
    CHECK(NT_SUCCESS(CcDirtyFlush(&Map.Map, 0, FileSize, ~0u, NULL)));

    CHECK(CcMapUninitialize(&Map.Map));
    CcCacheUninitialize(&Cache);
    CHECK(Backing.MappedViews == 0 && Backing.Errors == 0);
    FileDestroy(&Backing);
}
