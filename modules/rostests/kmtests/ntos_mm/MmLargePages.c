/*
 * PROJECT:     ReactOS kernel-mode tests
 * FILE:        modules/rostests/kmtests/ntos_mm/MmLargePages.c
 * PURPOSE:     Large-page virtual memory regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <kmt_test.h>

static void
LargeSectionViews(ULONG Large)
{
    HANDLE Section = NULL;
    LARGE_INTEGER Maximum, Offset;
    PVOID Base = NULL, Small = NULL, Region, Buffer = NULL;
    SIZE_T Size, Length;
    PHYSICAL_ADDRESS First, Physical;
    MEMORY_BASIC_INFORMATION Info;
    PMDL Mdl = NULL;
    NTSTATUS Status;
    ULONG Old, i;

    Maximum.QuadPart = Large + PAGE_SIZE;
    Status = ZwCreateSection(&Section, SECTION_ALL_ACCESS, NULL, &Maximum, PAGE_READWRITE,
                             SEC_COMMIT | SEC_LARGE_PAGES, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_4);
    if (NT_SUCCESS(Status))
        ZwClose(Section);
    Section = NULL;
    Maximum.QuadPart = 2 * (LONGLONG)Large;
    Status = ZwCreateSection(&Section, SECTION_ALL_ACCESS, NULL, &Maximum, PAGE_READWRITE,
                             SEC_RESERVE | SEC_LARGE_PAGES, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_6);
    if (NT_SUCCESS(Status))
        ZwClose(Section);
    Section = NULL;
    Status = ZwCreateSection(&Section, SECTION_ALL_ACCESS, NULL, &Maximum, PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Size = Large;
    Offset.QuadPart = 0;
    Status = ZwMapViewOfSection(Section, NtCurrentProcess(), &Base, 0, 0, &Offset, &Size,
                                ViewUnmap, MEM_LARGE_PAGES, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_9);
    if (NT_SUCCESS(Status))
        ZwUnmapViewOfSection(NtCurrentProcess(), Base);
    ZwClose(Section);
    Section = NULL;
    Base = NULL;
    Status = ZwCreateSection(&Section, SECTION_ALL_ACCESS, NULL, &Maximum, PAGE_READWRITE,
                             SEC_COMMIT | SEC_LARGE_PAGES, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    for (i = 0; i < 3; i++)
    {
        Region = i == 0 ? (PVOID)(ULONG_PTR)(Large + 0x10000) : NULL;
        Offset.QuadPart = i == 1 ? 0x10000 : 0;
        Size = i == 2 ? Large + PAGE_SIZE : Large;
        Status = ZwMapViewOfSection(Section, NtCurrentProcess(), &Region, 0, 0, &Offset, &Size,
                                    ViewUnmap, MEM_LARGE_PAGES, PAGE_READWRITE);
        ok_eq_hex(Status, STATUS_MAPPED_ALIGNMENT);
        if (NT_SUCCESS(Status))
            ZwUnmapViewOfSection(NtCurrentProcess(), Region);
    }
    Offset.QuadPart = 0;
    Size = 0;
    Status = ZwMapViewOfSection(Section, NtCurrentProcess(), &Base, 0, 0, &Offset, &Size,
                                ViewShare, MEM_LARGE_PAGES, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Finish;
    ok(((ULONG_PTR)Base & (Large - 1)) == 0, "Unaligned large-section view %p\n", Base);
    ok_eq_size(Size, 2 * (SIZE_T)Large);
    Status = ZwQueryVirtualMemory(NtCurrentProcess(), Base, MemoryBasicInformation, &Info, sizeof(Info), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(Info.Type, MEM_MAPPED);
        ok_eq_hex(Info.State, MEM_COMMIT);
        ok_eq_size(Info.RegionSize, Size);
    }
    First = MmGetPhysicalAddress(Base);
    ok((First.QuadPart & (Large - 1)) == 0, "Unaligned large-section backing %I64x\n", First.QuadPart);
    for (i = 0; i < Large / PAGE_SIZE; i++)
    {
        volatile ULONG *Word = (PULONG)((PUCHAR)Base + (SIZE_T)i * PAGE_SIZE);

        KmtStartSeh();
        ok_eq_ulong(*Word, 0);
        *Word = 0xFACE0000 + i;
        KmtEndSeh(STATUS_SUCCESS);
        Physical = MmGetPhysicalAddress((PVOID)Word);
        ok_eq_longlong(Physical.QuadPart, First.QuadPart + (LONGLONG)i * PAGE_SIZE);
    }
    Offset.QuadPart = 0x10000;
    Size = PAGE_SIZE;
    Status = ZwMapViewOfSection(Section, NtCurrentProcess(), &Small, 0, 0, &Offset, &Size,
                                ViewUnmap, 0, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Finish;
    KmtStartSeh();
    ok_eq_ulong(*(PULONG)Small, 0xFACE0000 + 0x10000 / PAGE_SIZE);
    *(PULONG)Small = 0xAABBCCDD;
    ok_eq_ulong(*(PULONG)((PUCHAR)Base + 0x10000), 0xAABBCCDD);
    KmtEndSeh(STATUS_SUCCESS);
    Physical = MmGetPhysicalAddress(Small);
    ok_eq_longlong(Physical.QuadPart, First.QuadPart + 0x10000);
    Region = Base;
    Length = Large;
    Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Region, &Length, PAGE_READONLY, &Old);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(Old, PAGE_READWRITE);
        KmtStartSeh();
        *(volatile ULONG *)Base = 1;
        KmtEndSeh(STATUS_ACCESS_VIOLATION);
        Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Region, &Length, PAGE_READWRITE, &Old);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    Mdl = IoAllocateMdl((PUCHAR)Base + 0x10000, PAGE_SIZE, FALSE, FALSE, NULL);
    ok(Mdl != NULL, "Large-section MDL allocation failed\n");
    if (Mdl == NULL)
        goto Finish;
    KmtStartSeh();
    MmProbeAndLockPages(Mdl, KernelMode, IoModifyAccess);
    KmtEndSeh(STATUS_SUCCESS);
    if (!(Mdl->MdlFlags & MDL_PAGES_LOCKED))
        goto Finish;
    Buffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
    ok(Buffer != NULL, "Large-section MDL mapping failed\n");
    Status = ZwClose(Section);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Section = NULL;
    Status = ZwUnmapViewOfSection(NtCurrentProcess(), Small);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Small = NULL;
    Status = ZwUnmapViewOfSection(NtCurrentProcess(), Base);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Base = NULL;
    if (Buffer != NULL)
        ok_eq_ulong(*(PULONG)Buffer, 0xAABBCCDD);
Finish:
    if (Base != NULL)
        ZwUnmapViewOfSection(NtCurrentProcess(), Base);
    if (Small != NULL)
        ZwUnmapViewOfSection(NtCurrentProcess(), Small);
    if (Section != NULL)
        ZwClose(Section);
    if (Mdl != NULL)
    {
        if (Mdl->MdlFlags & MDL_PAGES_LOCKED)
            MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }
}

START_TEST(MmLargePages)
{
    ULONG Large = SharedUserData->LargePageMinimum;
    PVOID Base = NULL, Region, Buffer;
    SIZE_T Size, Length;
    ULONG Old, i;
    NTSTATUS Status;
    PHYSICAL_ADDRESS First, Current;
    MEMORY_BASIC_INFORMATION Info;
    PMDL Mdl;
    FILE_SEGMENT_ELEMENT Selected[3];
    PPFN_NUMBER Frames;

    ok(Large >= PAGE_SIZE && (Large & (Large - 1)) == 0, "LargePageMinimum = %lu\n", Large);
    if (Large < PAGE_SIZE || (Large & (Large - 1)) != 0)
        return;

    Size = Large + PAGE_SIZE;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size,
                                     MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    ok(!NT_SUCCESS(Status), "Unaligned large-page size accepted: %lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Size = 0;
        ZwFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
        Base = NULL;
    }

    Size = 2 * (SIZE_T)Large;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size,
                                     MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok(((ULONG_PTR)Base & (Large - 1)) == 0, "Unaligned large-page base %p\n", Base);
    ok_eq_size(Size, 2 * (SIZE_T)Large);
    Status = ZwQueryVirtualMemory(NtCurrentProcess(), Base, MemoryBasicInformation, &Info, sizeof(Info), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(Info.Type, MEM_PRIVATE);
        ok_eq_hex(Info.State, MEM_COMMIT);
        ok_eq_size(Info.RegionSize, Size);
    }
    First = MmGetPhysicalAddress(Base);
    ok((First.QuadPart & (Large - 1)) == 0, "Unaligned physical block %I64x\n", First.QuadPart);
    for (i = 0; i < Large / PAGE_SIZE; i++)
    {
        volatile ULONG *Word = (PULONG)((PUCHAR)Base + (SIZE_T)i * PAGE_SIZE);
        KmtStartSeh();
        ok_eq_ulong(*Word, 0);
        *Word = 0xABCD0000 + i;
        ok_eq_ulong(*Word, 0xABCD0000 + i);
        KmtEndSeh(STATUS_SUCCESS);
        Current = MmGetPhysicalAddress((PVOID)Word);
        ok_eq_longlong(Current.QuadPart, First.QuadPart + (LONGLONG)i * PAGE_SIZE);
    }

    Region = (PUCHAR)Base + Large;
    Length = Large;
    Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Region, &Length, PAGE_READONLY, &Old);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(Old, PAGE_READWRITE);
        KmtStartSeh();
        *(volatile ULONG *)Region = 1;
        KmtEndSeh(STATUS_ACCESS_VIOLATION);
        Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Region, &Length, PAGE_NOACCESS, &Old);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            KmtStartSeh();
            i = *(volatile ULONG *)Region;
            KmtEndSeh(STATUS_ACCESS_VIOLATION);
        }
        Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Region, &Length, PAGE_READWRITE, &Old);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    Mdl = IoAllocateMdl(Base, 3 * PAGE_SIZE, FALSE, FALSE, NULL);
    ok(Mdl != NULL, "MDL allocation failed\n");
    if (Mdl != NULL)
    {
        Selected[0].Buffer = (PVOID64)(ULONG_PTR)((PUCHAR)Base + 3 * PAGE_SIZE);
        Selected[1].Buffer = (PVOID64)(ULONG_PTR)((PUCHAR)Base + PAGE_SIZE);
        Selected[2].Buffer = Selected[0].Buffer;
        KmtStartSeh();
        MmProbeAndLockSelectedPages(Mdl, Selected, KernelMode, IoModifyAccess);
        KmtEndSeh(STATUS_SUCCESS);
        if (Mdl->MdlFlags & MDL_PAGES_LOCKED)
        {
            Frames = MmGetMdlPfnArray(Mdl);
            ok_eq_longlong(Frames[0], (First.QuadPart >> PAGE_SHIFT) + 3);
            ok_eq_longlong(Frames[1], (First.QuadPart >> PAGE_SHIFT) + 1);
            ok_eq_longlong(Frames[2], Frames[0]);
            Buffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
            ok(Buffer != NULL, "Selected-page MDL mapping failed\n");
            if (Buffer != NULL)
            {
                ok_eq_ulong(*(PULONG)Buffer, 0xABCD0003);
                ok_eq_ulong(*(PULONG)((PUCHAR)Buffer + PAGE_SIZE), 0xABCD0001);
                ok_eq_ulong(*(PULONG)((PUCHAR)Buffer + 2 * PAGE_SIZE), 0xABCD0003);
            }
            Size = 0;
            Status = ZwFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                Base = NULL;
                if (Buffer != NULL)
                    ok_eq_ulong(*(PULONG)Buffer, 0xABCD0003);
            }
            MmUnlockPages(Mdl);
        }
        IoFreeMdl(Mdl);
    }
    if (Base != NULL)
    {
        Size = 0;
        Status = ZwFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    LargeSectionViews(Large);
}
