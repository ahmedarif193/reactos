/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_writeback.c
 * PURPOSE:     Memory writeback host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"

typedef struct _WRITEBACK_FILE
{
    TEST_FILE File;
    TEST_WORLD *World;
    ULONG Calls;
    ULONG64 Offset[64];
    ULONG Length[64];
    ULONG Pages[64];
    BOOLEAN Redirty;
    BOOLEAN Noncontiguous;
    PMI_SEGMENT Segment;
    ULONG64 ExtendOnWrite;
} WRITEBACK_FILE;

static
NTSTATUS
WriteFrames(PVOID Context, ULONG64 Offset, ULONG Length, const ULONG *Frames, ULONG PageCount)
{
    WRITEBACK_FILE *File = Context;
    ULONG Call = File->Calls++;
    ULONG i;

    CHECK(MiHostIrql == 0);
    CHECK(Call < RTL_NUMBER_OF(File->Offset));
    CHECK(PageCount > 0 && PageCount <= MI_MAX_FILE_IO_PAGES);
    CHECK(Length > (PageCount - 1) * PAGE_SIZE && Length <= PageCount * PAGE_SIZE);
    CHECK(Offset + Length <= File->File.Size);
    if (File->ExtendOnWrite != 0)
    {
        BOOLEAN Unlocked = MI_MUTEX_TRY_ACQUIRE(&File->Segment->Lock);

        CHECK(Unlocked);
        if (Unlocked)
        {
            MI_MUTEX_RELEASE(&File->Segment->Lock);
            CHECK(NT_SUCCESS(MiSegmentExtend(File->Segment, File->ExtendOnWrite)));
        }
        File->ExtendOnWrite = 0;
    }
    File->Offset[Call] = Offset;
    File->Length[Call] = Length;
    File->Pages[Call] = PageCount;
    for (i = 0; i < PageCount; i++)
    {
        PMI_PFN Pfn = &File->World->PfnArray[Frames[i]];
        ULONG Bytes = Length - i * PAGE_SIZE;
        PVOID Mapping;

        CHECK(Pfn->ShareCount > 0);
        CHECK((Pfn->Flags & MI_PFN_FLAG_PROTOTYPE) != 0);
        CHECK((MiSoftValue(Pfn->OriginalPte) << MI_SECTOR_SHIFT) == Offset + i * PAGE_SIZE);
        if (i != 0 && Frames[i] != Frames[i - 1] + 1)
            File->Noncontiguous = TRUE;
        if (Bytes > PAGE_SIZE)
            Bytes = PAGE_SIZE;
        if (!File->File.FailWrites)
        {
            Mapping = MiArchMapFrame(Frames[i]);
            memcpy(File->File.Data + Offset + i * PAGE_SIZE, Mapping, Bytes);
            MiArchUnmapFrame(Mapping);
        }
    }
    if (File->Redirty)
    {
        MiPfnSetModified(&File->World->System.Pfn, Frames[0]);
        File->Redirty = FALSE;
    }
    return File->File.FailWrites ? STATUS_UNEXPECTED_IO_ERROR : STATUS_SUCCESS;
}

static
void
WritebackSizeReentry(void)
{
    TEST_WORLD World;
    WRITEBACK_FILE File = { .World = &World, .ExtendOnWrite = PAGE_SIZE };
    MI_FILE_OPS Ops = { .Read = TestFileOps.Read, .WriteFrames = WriteFrames };
    ULONG i;

    WorldCreate(&World, 256, 1, 10000);
    FileCreate(&File.File, PAGE_SIZE);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, 512, MI_PROT_READWRITE,
                                     &Ops, &File, NULL, 0, &File.Segment)));
    CHECK(NT_SUCCESS(MiSegmentMarkDirty(File.Segment, 0, 512)));
    memset(File.File.Data, 0xCC, PAGE_SIZE);
    CHECK(NT_SUCCESS(MiSegmentFlush(File.Segment, 0, 512)));
    CHECK(File.Calls == 1 && File.Length[0] == 512 && File.Pages[0] == 1);
    CHECK(File.Segment->SizeInBytes == PAGE_SIZE && File.Segment->PageCount == 1);
    for (i = 0; i < PAGE_SIZE; i++)
        CHECK(File.File.Data[i] == (i < 512 ? (UCHAR)i : 0xCC));
    CHECK(MiSegmentDereferenceAndClose(File.Segment));
    WorldExpectClean(&World, 256);
    FileDestroy(&File.File);
    WorldDestroy(&World);
}

void
TestWriteback(void)
{
    TEST_WORLD World;
    WRITEBACK_FILE File = { .World = &World };
    MI_FILE_OPS Ops = { .Read = TestFileOps.Read, .Write = TestFileOps.Write, .WriteFrames = WriteFrames };
    PMI_SEGMENT Segment;
    ULONG Pages = MI_MAX_FILE_IO_PAGES + 3;
    ULONG64 Size = (Pages - 1) * PAGE_SIZE + 317;
    ULONG64 i;
    ULONG Calls;

    WritebackSizeReentry();
    WorldCreate(&World, 512, 1, 100000);
    FileCreate(&File.File, Size);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, Size, MI_PROT_READWRITE,
                                     &Ops, &File, NULL, 0, &Segment)));
    for (i = 0; i < Pages; i++)
        CHECK(NT_SUCCESS(MiSegmentMarkDirty(Segment, ((i * 2) % Pages) * PAGE_SIZE, 1)));
    memset(File.File.Data, 0xCC, Size);
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0, Size)));
    CHECK(File.Calls == 2);
    CHECK(File.Offset[0] == 0 && File.Pages[0] == MI_MAX_FILE_IO_PAGES);
    CHECK(File.Length[0] == MI_MAX_FILE_IO_PAGES * PAGE_SIZE);
    CHECK(File.Offset[1] == MI_MAX_FILE_IO_PAGES * PAGE_SIZE && File.Pages[1] == 3);
    CHECK(File.Length[1] == 2 * PAGE_SIZE + 317);
    CHECK(File.Noncontiguous);
    CHECK(Segment->PagesWritten == Pages);
    for (i = 0; i < Size; i++)
        CHECK(File.File.Data[i] == (UCHAR)((i >> PAGE_SHIFT) * 31 + i));
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0, Size)));
    CHECK(File.Calls == 2);

    Calls = File.Calls;
    CHECK(NT_SUCCESS(MiSegmentMarkDirty(Segment, PAGE_SIZE, 1)));
    CHECK(NT_SUCCESS(MiSegmentMarkDirty(Segment, 3 * PAGE_SIZE, 1)));
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, PAGE_SIZE + 7, 3 * PAGE_SIZE - 14)));
    CHECK(File.Calls == Calls + 2);
    CHECK(File.Offset[Calls] == PAGE_SIZE && File.Length[Calls] == PAGE_SIZE);
    CHECK(File.Offset[Calls + 1] == 3 * PAGE_SIZE && File.Length[Calls + 1] == PAGE_SIZE);

    Calls = File.Calls;
    CHECK(NT_SUCCESS(MiSegmentMarkDirty(Segment, 5 * PAGE_SIZE, 4 * PAGE_SIZE)));
    File.File.FailWrites = TRUE;
    CHECK(MiSegmentFlush(Segment, 5 * PAGE_SIZE, 4 * PAGE_SIZE) == STATUS_UNEXPECTED_IO_ERROR);
    CHECK(File.Calls == Calls + 1 && File.Pages[Calls] == 4);
    CHECK(WorldCheck(&World) == 0);
    File.File.FailWrites = FALSE;
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 5 * PAGE_SIZE, 4 * PAGE_SIZE)));
    CHECK(File.Calls == Calls + 2 && File.Pages[Calls + 1] == 4);
    CHECK(File.Offset[Calls + 1] == 5 * PAGE_SIZE);

    Calls = File.Calls;
    CHECK(NT_SUCCESS(MiSegmentMarkDirty(Segment, 0, 2 * PAGE_SIZE)));
    File.Redirty = TRUE;
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0, 2 * PAGE_SIZE)));
    CHECK(File.Calls == Calls + 1 && File.Pages[Calls] == 2);
    CHECK(NT_SUCCESS(MiSegmentFlush(Segment, 0, 2 * PAGE_SIZE)));
    CHECK(File.Calls == Calls + 2 && File.Pages[Calls + 1] == 1);
    CHECK(File.Offset[Calls + 1] == 0);

    Calls = File.Calls;
    CHECK(NT_SUCCESS(MiSegmentMarkDirty(Segment, PAGE_SIZE, 2 * PAGE_SIZE)));
    Segment->FileOps.Write = NULL;
    CHECK(MiSegmentDereferenceAndClose(Segment));
    CHECK(File.Calls == Calls + 1 && File.Pages[Calls] == 2);
    WorldExpectClean(&World, 512);
    FileDestroy(&File.File);
    WorldDestroy(&World);
}
