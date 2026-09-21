/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/cc/host/t_ntlayout.c
 * PURPOSE:     NT cache manager layout regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccharness.h"
#include <cc/api/ccnt.h>

static ULONG64
NtLayoutU64(PVOID Object, ULONG Offset)
{
    ULONG64 Value;

    memcpy(&Value, (PUCHAR)Object + Offset, sizeof(Value));
    return Value;
}

void
TestNtLayout(void)
{
    CC_NT_MAP Map = { 0 };
    TEST_FILE Backing;
    CC_CACHE Cache;
    SECTION_OBJECT_POINTERS Pointers = { &Map };
    FILE_OBJECT File = { .SectionObjectPointer = &Pointers };
    CC_VIEW_RANGE Range;
    LARGE_INTEGER Offset = { .QuadPart = 128 };
    LARGE_INTEGER Lsn = { .QuadPart = 0x123456789ABULL };
    PVOID Bcb, Buffer;
    const ULONG64 FileSize = 0x123456789ABULL;
    const ULONG64 SectionSize = FileSize + CC_VIEW_SIZE;
    const ULONG64 ValidDataLength = FileSize - 123;

    CHECK(FIELD_OFFSET(CC_NT_MAP, Map) == 0);
    CHECK(FIELD_OFFSET(CC_MAP, FileSize) == 0x08);
    CHECK(FIELD_OFFSET(CC_MAP, BcbList) == 0x10);
    CHECK(FIELD_OFFSET(CC_MAP, SectionSize) == 0x20);
    CHECK(FIELD_OFFSET(CC_MAP, ValidDataLength) == 0x28);
    CHECK(FIELD_OFFSET(CC_VIEW, BaseAddress) == 0x00);
    CHECK(FIELD_OFFSET(CC_VIEW, Map) == 0x08);
    CHECK(FIELD_OFFSET(CC_VIEW, FileOffset) == 0x10);
    CHECK(FIELD_OFFSET(CC_NT_BCB, Public) == 0x00);
    CHECK(FIELD_OFFSET(CC_BCB, Length) == 0x04);
    CHECK(FIELD_OFFSET(CC_BCB, FileOffset) == 0x08);
    CHECK(FIELD_OFFSET(CC_BCB, Link) == 0x10);
    CHECK(FIELD_OFFSET(CC_BCB, OldestLsn) == 0x28);
    CHECK(FIELD_OFFSET(CC_BCB, NewestLsn) == 0x30);
    CHECK(FIELD_OFFSET(CC_BCB, View) == 0x38);
    CHECK(FIELD_OFFSET(CC_BCB, PinCount) == 0x40);
    CHECK(FIELD_OFFSET(CC_BCB, Map) == 0xB0);

    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    Cache.BcbBytes = sizeof(CC_NT_BCB);
    Cache.BcbCreated = CcNtBcbCreated;
    Cache.BcbDeleting = CcNtBcbDeleting;
    FileCreate(&Backing, 2 * CC_VIEW_SIZE);
    CcMapInitialize(&Map.Map, &Cache, &Backing.Ops, &Backing, Backing.Size, Backing.Size, Backing.Size);
    CHECK(NtLayoutU64(Pointers.SharedCacheMap, 0x08) == Backing.Size);
    CHECK((PVOID)CcGetFileSizePointer(&File) == &Map.Map.FileSize);
    CHECK(NT_SUCCESS(CcViewAcquire(&Map.Map, CC_VIEW_SIZE, PAGE_SIZE, &Range)));
    CHECK(NtLayoutU64(Range.View, 0x00) == (ULONG_PTR)Range.Address);
    CHECK(NtLayoutU64(Range.View, 0x08) == (ULONG_PTR)Pointers.SharedCacheMap);
    CHECK(NtLayoutU64(Range.View, 0x10) == CC_VIEW_SIZE);
    CcViewRelease(&Range);

    Map.ReferenceCount = 1;
    Map.PinAccess = TRUE;
    CHECK(CcPinRead(&File, &Offset, 31, PIN_WAIT, &Bcb, &Buffer));
    CHECK((PVOID)Map.Map.BcbList.Flink == (PUCHAR)Bcb + 0x10);
    CHECK(((PPUBLIC_BCB)Bcb)->NodeTypeCode == 0x2FD);
    CHECK(((PPUBLIC_BCB)Bcb)->NodeByteSize == 0);
    CHECK(((PPUBLIC_BCB)Bcb)->MappedLength == 31);
    CHECK(((PPUBLIC_BCB)Bcb)->MappedFileOffset.QuadPart == 128);
    CcSetDirtyPinnedData(Bcb, &Lsn);
    CHECK(((PUCHAR)Bcb)[2] == 1);
    if (Bcb == (PVOID)CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb))
    {
        CHECK(NtLayoutU64(Bcb, 0x20) == 159);
        CHECK(NtLayoutU64(Bcb, 0x28) == (ULONG64)Lsn.QuadPart);
        CHECK(NtLayoutU64(Bcb, 0x30) == (ULONG64)Lsn.QuadPart);
        CHECK(NtLayoutU64(Bcb, 0xB0) == (ULONG_PTR)Pointers.SharedCacheMap);
        CHECK(NtLayoutU64(Bcb, 0xB8) == (ULONG_PTR)Buffer);
    }
    CcUnpinData(Bcb);
    CHECK(NT_SUCCESS(CcDirtyFlush(&Map.Map, 0, Backing.Size, ~0u, NULL)));

    CcMapSetSizes(&Map.Map, SectionSize, FileSize, ValidDataLength);
    CHECK(NtLayoutU64(Pointers.SharedCacheMap, 0x08) == FileSize);
    CHECK(NtLayoutU64(Pointers.SharedCacheMap, 0x20) == SectionSize);
    CHECK(NtLayoutU64(Pointers.SharedCacheMap, 0x28) == ValidDataLength);
    if ((PVOID)CcGetFileSizePointer(&File) == &Map.Map.FileSize)
    {
        CcGetFileSizePointer(&File)->QuadPart = FileSize - 7;
        CHECK(Map.Map.FileSize == FileSize - 7);
    }

    CHECK(CcMapUninitialize(&Map.Map));
    CcCacheUninitialize(&Cache);
    CHECK(Backing.MappedViews == 0 && Backing.Errors == 0);
    FileDestroy(&Backing);
}
