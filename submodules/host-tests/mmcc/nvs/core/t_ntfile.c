/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_ntfile.c
 * PURPOSE:     NT memory-backed file I/O host-native tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include "ntfileshim.h"

typedef struct _NT_IO_FILE
{
    ULONG64 Offset;
    ULONG Length;
    ULONG Transferred;
    ULONG Calls;
    NTSTATUS Status;
    BOOLEAN Write;
} NT_IO_FILE;

NTSTATUS
MiPagingIo(PFILE_OBJECT Object, ULONG64 Offset, ULONG Length, PVOID Buffer, BOOLEAN Write, PULONG Transferred)
{
    NT_IO_FILE *File = Object->FsContext;

    File->Offset = Offset;
    File->Length = Length;
    File->Write = Write;
    File->Calls++;
    *Transferred = File->Transferred;
    if (!Write && NT_SUCCESS(File->Status))
        memset(Buffer, 0xBA, File->Transferred < Length ? File->Transferred : Length);
    return File->Status;
}

void
TestNtFile(void)
{
    static const ULONG Lengths[] = { 1, 62, 512, 1004, PAGE_SIZE - 1, PAGE_SIZE };
    NT_IO_FILE File = { .Transferred = PAGE_SIZE };
    FILE_OBJECT Object = { .FsContext = &File };
    MI_CONTROL_AREA Control = { .FileObject = &Object };
    UCHAR Buffer[PAGE_SIZE + 16];
    ULONG i, j;

    CHECK(NT_SUCCESS(MiControlRead(&Control, 0, 0, Buffer)));
    CHECK(NT_SUCCESS(MiControlWrite(&Control, 0, 0, Buffer)));
    CHECK(File.Calls == 0);

    for (i = 0; i < RTL_NUMBER_OF(Lengths); i++)
    {
        memset(Buffer, 0xCC, sizeof(Buffer));
        CHECK(NT_SUCCESS(MiControlRead(&Control, PAGE_SIZE * 3, Lengths[i], Buffer)));
        CHECK(File.Offset == PAGE_SIZE * 3 && File.Length == PAGE_SIZE && !File.Write);
        for (j = 0; j < Lengths[i]; j++)
            CHECK(Buffer[j] == 0xBA);
        CHECK(Buffer[PAGE_SIZE] == 0xCC);
        CHECK(NT_SUCCESS(MiControlWrite(&Control, PAGE_SIZE * 3, Lengths[i], Buffer)));
        CHECK(File.Offset == PAGE_SIZE * 3 && File.Length == PAGE_SIZE && File.Write);
    }

    File.Transferred = 62;
    memset(Buffer, 0xCC, sizeof(Buffer));
    CHECK(NT_SUCCESS(MiControlRead(&Control, 0, PAGE_SIZE, Buffer)));
    for (j = 0; j < PAGE_SIZE; j++)
        CHECK(Buffer[j] == (j < File.Transferred ? 0xBA : 0));
    CHECK(Buffer[PAGE_SIZE] == 0xCC);

    File.Status = STATUS_END_OF_FILE;
    memset(Buffer, 0xCC, sizeof(Buffer));
    CHECK(NT_SUCCESS(MiControlRead(&Control, 0, PAGE_SIZE, Buffer)));
    for (j = 0; j < PAGE_SIZE; j++)
        CHECK(Buffer[j] == 0);
    CHECK(Buffer[PAGE_SIZE] == 0xCC);

    File.Status = STATUS_UNEXPECTED_IO_ERROR;
    memset(Buffer, 0xCC, sizeof(Buffer));
    CHECK(MiControlRead(&Control, 0, PAGE_SIZE, Buffer) == STATUS_UNEXPECTED_IO_ERROR);
    CHECK(Buffer[0] == 0xCC && Buffer[PAGE_SIZE] == 0xCC);
    CHECK(MiControlWrite(&Control, 0, PAGE_SIZE, Buffer) == STATUS_UNEXPECTED_IO_ERROR);
}
