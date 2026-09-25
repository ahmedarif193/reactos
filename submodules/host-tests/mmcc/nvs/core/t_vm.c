/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_vm.c
 * PURPOSE:     Virtual memory operation host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include "ntprotectshim.h"

#define KB64 0x10000ULL

static void
VmWindowsProtection(void)
{
    ULONG Win32, Protection, Plain;

    for (Win32 = PAGE_NOACCESS; Win32 <= PAGE_EXECUTE_WRITECOPY; Win32 <<= 1)
    {
        CHECK(MiProtectionFromWin32(Win32, &Plain));
        CHECK(MiProtectionToWin32(Plain) == Win32);
        CHECK(!MiProtectionFromWin32(Win32 | PAGE_TARGETS_INVALID, &Protection));
        CHECK(MiAllocationProtectionFromWin32(Win32 | PAGE_TARGETS_INVALID, &Protection) ==
              MI_PROT_IS_EXECUTE(Plain));
        CHECK(Protection == (MI_PROT_IS_EXECUTE(Plain) ? Plain : 0));
        CHECK(!MiAllocationProtectionFromWin32(Win32 | 0x20000000, &Protection));
    }
    CHECK(!MiAllocationProtectionFromWin32(PAGE_TARGETS_INVALID, &Protection));
    CHECK(!MiAllocationProtectionFromWin32(PAGE_EXECUTE | PAGE_READWRITE | PAGE_TARGETS_INVALID, &Protection));
    CHECK(MiAllocationProtectionFromWin32(PAGE_EXECUTE_READWRITE | PAGE_GUARD | PAGE_TARGETS_INVALID,
                                        &Protection));
    CHECK(Protection == (MI_PROT_EXECUTE_READWRITE | MI_PROT_GUARD));
}

void
ProcessCreate(TEST_WORLD *World, PMI_ADDRESS_SPACE Space)
{
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World->System, Space)));
    WorldAttach(World, 0, Space);
}

void
ProcessDestroy(TEST_WORLD *World, PMI_ADDRESS_SPACE Space)
{
    MiCleanAddressSpace(Space);
    CHECK(Space->VadRoot.NodeCount == 0);
    CHECK(MI_ATOMIC_READ64(&Space->CommittedPages) == 0);
    CHECK(MI_ATOMIC_READ64(&Space->ResidentPages) == 0);
    CHECK(MI_ATOMIC_READ64(&Space->PrivatePages) == 0);
    CHECK(MI_ATOMIC_READ64(&Space->PageTablePages) == 0);
    CHECK(MiPtCheck(Space) == 0);
    WorldAttach(World, 0, NULL);
    MiAddressSpaceDestroy(Space);
}

void
WorldExpectClean(TEST_WORLD *World, ULONG Frames)
{
    MiPfnDrainCaches(&World->System.Pfn);
    CHECK(WorldCheck(World) == 0);
    CHECK(MI_ATOMIC_READ64(&World->System.CommittedPages) == 0);
    CHECK(MiPfnListCount(&World->System.Pfn, MiPageModified) == 0);
    CHECK(MiPfnAvailablePages(&World->System.Pfn) == Frames - 2);
    if (World->System.PageFile != NULL)
        CHECK(World->System.PageFile->SlotsInUse == 0);
}

static
NTSTATUS
Alloc(PMI_ADDRESS_SPACE Space, ULONG64 *Base, ULONG64 Size, ULONG Type, ULONG Protection)
{
    ULONG64 RegionSize = Size;

    return MiAllocateVirtualMemory(Space, Base, &RegionSize, Type, Protection);
}

static
NTSTATUS
Free(PMI_ADDRESS_SPACE Space, ULONG64 Base, ULONG64 Size, ULONG Type)
{
    return MiFreeVirtualMemory(Space, &Base, &Size, Type);
}

static
NTSTATUS
Protect(PMI_ADDRESS_SPACE Space, ULONG64 Base, ULONG64 Size, ULONG Protection, ULONG *Old)
{
    return MiProtectVirtualMemory(Space, &Base, &Size, Protection, Old);
}

static void
VmExecutableWriteTracking(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Code = 0;
    ULONG64 EcCode = 0;
    ULONG64 Sparse = 0x100000000ULL;
    ULONG64 Adjacent = Sparse + 0x40000000ULL;
    ULONG Old;

    WorldCreate(&World, 256, 2, 100000);
    World.Machine.StrictTlb = TRUE;
    ProcessCreate(&World, &Space);
    WorldAttach(&World, 1, &Space);

    CHECK(NT_SUCCESS(Alloc(&Space, &Code, PAGE_SIZE, MI_MEM_RESERVE | MI_MEM_COMMIT,
                           MI_PROT_EXECUTE_READWRITE)));
    CHECK(NT_SUCCESS(Alloc(&Space, &EcCode, PAGE_SIZE, MI_MEM_RESERVE | MI_MEM_COMMIT,
                           MI_PROT_EXECUTE_READWRITE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Code, 1)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, EcCode, 1)));
    MiVadLocate(&Space, EcCode)->EcCode = TRUE;

    CHECK(NT_SUCCESS(MiSetExecutableWriteTracking(&Space, TRUE)));
    CHECK(UserWrite64(&World, 0, Code, 2) == STATUS_EXECUTABLE_MEMORY_WRITE);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, EcCode, 2)));
    CHECK(NT_SUCCESS(MiFaultWithWriteAllowance(&Space, Code, MiFaultWrite, TRUE, TRUE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Code, 2)));
    CHECK(NT_SUCCESS(MiResetExecutableWriteTracking(&Space, Code, 1)));
    CHECK(UserWrite64(&World, 1, Code, 3) == STATUS_EXECUTABLE_MEMORY_WRITE);

    CHECK(NT_SUCCESS(Protect(&Space, Code, PAGE_SIZE, MI_PROT_READWRITE, &Old)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Code, 4)));
    CHECK(NT_SUCCESS(Protect(&Space, Code, PAGE_SIZE, MI_PROT_EXECUTE_READWRITE, &Old)));
    CHECK(UserWrite64(&World, 0, Code, 5) == STATUS_EXECUTABLE_MEMORY_WRITE);

    CHECK(NT_SUCCESS(Alloc(&Space, &Sparse, 0x40000000ULL, MI_MEM_RESERVE,
                           MI_PROT_EXECUTE_READWRITE)));
    CHECK(NT_SUCCESS(Alloc(&Space, &Adjacent, KB64, MI_MEM_RESERVE,
                           MI_PROT_EXECUTE_READWRITE)));
    CHECK(NT_SUCCESS(MiResetExecutableWriteTracking(&Space, Sparse, 0x40000000ULL + KB64)));
    CHECK(MiResetExecutableWriteTracking(&Space, Adjacent, KB64 + PAGE_SIZE) ==
          STATUS_MEMORY_NOT_ALLOCATED);

    CHECK(NT_SUCCESS(MiSetExecutableWriteTracking(&Space, FALSE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Code, 6)));
    CHECK(MiResetExecutableWriteTracking(&Space, Code, 1) == STATUS_NOT_SUPPORTED);

    CHECK(NT_SUCCESS(Free(&Space, Code, 0, MI_MEM_RELEASE)));
    CHECK(NT_SUCCESS(Free(&Space, EcCode, 0, MI_MEM_RELEASE)));
    CHECK(NT_SUCCESS(Free(&Space, Sparse, 0, MI_MEM_RELEASE)));
    CHECK(NT_SUCCESS(Free(&Space, Adjacent, 0, MI_MEM_RELEASE)));
    WorldAttach(&World, 1, NULL);
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
}

static void
VmDynamicCodeEcExemption(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Plain = 0;
    ULONG64 EcCode = 0;
    ULONG64 Blocked = 0;
    ULONG64 Base;
    ULONG64 Size;
    ULONG Old;

    WorldCreate(&World, 256, 1, 100000);
    ProcessCreate(&World, &Space);
    WorldAttach(&World, 0, &Space);

    CHECK(NT_SUCCESS(Alloc(&Space, &Plain, KB64, MI_MEM_RESERVE, MI_PROT_EXECUTE_READWRITE)));
    CHECK(NT_SUCCESS(Alloc(&Space, &EcCode, KB64, MI_MEM_RESERVE, MI_PROT_EXECUTE_READWRITE)));
    MiVadLocate(&Space, EcCode)->EcCode = TRUE;

    Size = KB64;
    CHECK(MiAllocateVirtualMemoryBounded(&Space, &Blocked, &Size, MI_MEM_RESERVE, MI_PROT_EXECUTE_READWRITE, 0,
                                         0, 0, TRUE) == STATUS_DYNAMIC_CODE_BLOCKED);

    Base = Plain;
    Size = PAGE_SIZE;
    CHECK(MiAllocateVirtualMemoryBounded(&Space, &Base, &Size, MI_MEM_COMMIT, MI_PROT_EXECUTE_READWRITE, 0, 0, 0,
                                         TRUE) == STATUS_DYNAMIC_CODE_BLOCKED);
    Base = Plain;
    Size = PAGE_SIZE;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemoryBounded(&Space, &Base, &Size, MI_MEM_COMMIT, MI_PROT_READWRITE, 0, 0, 0,
                                                    TRUE)));
    Base = EcCode;
    Size = PAGE_SIZE;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemoryBounded(&Space, &Base, &Size, MI_MEM_COMMIT, MI_PROT_EXECUTE_READWRITE,
                                                    0, 0, 0, TRUE)));

    Base = Plain;
    Size = PAGE_SIZE;
    CHECK(MiProtectVirtualMemoryEx(&Space, &Base, &Size, MI_PROT_EXECUTE_READWRITE, &Old, TRUE) ==
          STATUS_DYNAMIC_CODE_BLOCKED);
    Base = EcCode;
    Size = PAGE_SIZE;
    CHECK(NT_SUCCESS(MiProtectVirtualMemoryEx(&Space, &Base, &Size, MI_PROT_EXECUTE_READ, &Old, TRUE)));

    CHECK(NT_SUCCESS(Free(&Space, Plain, 0, MI_MEM_RELEASE)));
    CHECK(NT_SUCCESS(Free(&Space, EcCode, 0, MI_MEM_RELEASE)));
    WorldAttach(&World, 0, NULL);
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
}

static
void
VmCommitDecommitBatch(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base = 0x40000000, Region;
    ULONG Cpu, Iteration;
    NTSTATUS Status;

    for (Iteration = 0; Iteration <= MI_SOFT_PROT_MASK; Iteration++)
    {
        ULONG Kind;

        for (Kind = MiSoftNone; Kind <= MiSoftResident; Kind++)
        {
            MI_PTE Pte = MiSoftMake((MI_SOFT_KIND)Kind, Iteration, 0);
            BOOLEAN Unbacked = Kind == MiSoftDemandZero || Kind == MiSoftDecommitted || Pte == 0;

            CHECK(MiSoftIsUnbacked(Pte) == Unbacked);
            CHECK(!MiSoftIsUnbacked(Pte | 1ULL));
            CHECK(!MiSoftIsUnbacked(Pte | (1ULL << MI_SOFT_VALUE_SHIFT)));
            CHECK(!MiSoftIsUnbacked(Pte | (1ULL << MI_SOFT_FILE_SHIFT)));
        }
    }
    WorldCreate(&World, 256, 4, 100000);
    World.Machine.StrictTlb = TRUE;
    ProcessCreate(&World, &Space);
    for (Cpu = 1; Cpu < 4; Cpu++)
        WorldAttach(&World, Cpu, &Space);
    CHECK(NT_SUCCESS(Alloc(&Space, &Base, (MI_EMPTY_TABLE_CACHE_SIZE + 2) * 2 * 1024 * 1024,
                            MI_MEM_RESERVE, MI_PROT_NOACCESS)));
    for (Iteration = 0; Iteration < (MI_EMPTY_TABLE_CACHE_SIZE + 2) * 32; Iteration++)
    {
        LONG64 Invalidations, Generation;
        MACHINE_FAULT_ROUTINE Fault = World.Machine.Fault;
        ULONG PreviousTable = 0, TableFrame, Cached = 0;
        BOOLEAN Reuse;

        Region = Base + (ULONG64)Iteration * KB64;
        Reuse = MiPtLookup(&Space, Region, &PreviousTable) != NULL;
        for (Cpu = 0; Cpu < MI_EMPTY_TABLE_CACHE_SIZE; Cpu++)
            Cached += Space.EmptyTable[Cpu].Present != FALSE;
        CHECK(NT_SUCCESS(Alloc(&Space, &Region, KB64, MI_MEM_COMMIT, MI_PROT_READWRITE)));
        CHECK(MiPtLookup(&Space, Region, &TableFrame) != NULL);
        if (Reuse)
            CHECK(TableFrame == PreviousTable);
        CHECK(UserRead64(&World, 0, Region, &Status) == 0 && NT_SUCCESS(Status));
        for (Cpu = 0; Cpu < 4; Cpu++)
            CHECK(NT_SUCCESS(UserWrite64(&World, Cpu, Region, Iteration + Cpu + 1)));
        CHECK(Space.CommittedPages == 16 && Space.PrivatePages == 1);
        Invalidations = World.Machine.TlbInvalidations;
        Generation = Space.PageFileGeneration;
        CHECK(NT_SUCCESS(Free(&Space, Region, KB64, MI_MEM_DECOMMIT)));
        CHECK(World.Machine.TlbInvalidations == Invalidations +
              ((Cached == MI_EMPTY_TABLE_CACHE_SIZE && !Reuse) ? 2 : 1));
        CHECK(Space.PageFileGeneration == Generation);
        CHECK(Space.CommittedPages == 0 && Space.PrivatePages == 0);
        CHECK(Space.PageTablePages >= World.System.Arch->PagingLevels - 1 &&
              Space.PageTablePages <= World.System.Arch->PagingLevels - 2 + MI_EMPTY_TABLE_CACHE_SIZE);
        CHECK(MiPtLookup(&Space, Region, NULL) != NULL);
        World.Machine.Fault = NULL;
        for (Cpu = 0; Cpu < 4; Cpu++)
        {
            UserRead64(&World, Cpu, Region, &Status);
            CHECK(Status == STATUS_ACCESS_VIOLATION);
        }
        World.Machine.Fault = Fault;
        CHECK(MiPtCheck(&Space) == 0);
        if (Iteration == 20)
        {
            CHECK(MiTrimAddressSpace(&Space, 1, TRUE) == 0);
            CHECK(Space.PageTablePages == 0);
            for (Cpu = 0; Cpu < MI_EMPTY_TABLE_CACHE_SIZE; Cpu++)
                CHECK(!Space.EmptyTable[Cpu].Present);
            CHECK(MiPtLookup(&Space, Region, NULL) == NULL);
        }
    }
    CHECK(NT_SUCCESS(Alloc(&Space, &Region, PAGE_SIZE, MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Region, 0xC01117)));
    CHECK(MiTrimAddressSpace(&Space, 0, TRUE) == 0);
    CHECK(Space.PageTablePages == World.System.Arch->PagingLevels - 1);
    for (Cpu = 0; Cpu < MI_EMPTY_TABLE_CACHE_SIZE; Cpu++)
        CHECK(!Space.EmptyTable[Cpu].Present);
    CHECK(UserRead64(&World, 0, Region, &Status) == 0xC01117 && NT_SUCCESS(Status));
    CHECK(NT_SUCCESS(Free(&Space, Base, 0, MI_MEM_RELEASE)));
    for (Cpu = 1; Cpu < 4; Cpu++)
        WorldAttach(&World, Cpu, NULL);
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
}

static
void
VmReserveCommitMatrix(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    MI_MEMORY_INFORMATION Info;
    ULONG64 Base = 0;
    ULONG64 Size = 0x100000;
    ULONG64 Other;
    NTSTATUS Status;
    ULONG Old;

    WorldCreate(&World, 2048, 1, 100000);
    World.Machine.StrictTlb = TRUE;
    ProcessCreate(&World, &Space);

    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Base, &Size, MI_MEM_RESERVE, MI_PROT_READWRITE)));
    CHECK(Base != 0 && (Base & (KB64 - 1)) == 0 && Size == 0x100000);

    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x3123, &Info)));
    CHECK(Info.State == MI_MEM_RESERVE && Info.AllocationBase == Base && Info.BaseAddress == Base + 0x3000);
    CHECK(Info.RegionSize == 0x100000 - 0x3000 && Info.Type == MI_MEM_PRIVATE && Info.Protect == 0);
    CHECK(Info.AllocationProtect == MI_PROT_READWRITE);

    UserRead64(&World, 0, Base, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(MI_ATOMIC_READ64(&Space.PageTablePages) == 0);

    Other = Base + 0x2000;
    CHECK(NT_SUCCESS(Alloc(&Space, &Other, 0x3000, MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 3);

    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base, &Info)));
    CHECK(Info.State == MI_MEM_RESERVE && Info.RegionSize == 0x2000);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x2000, &Info)));
    CHECK(Info.State == MI_MEM_COMMIT && Info.RegionSize == 0x3000 && Info.Protect == MI_PROT_READWRITE);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x5000, &Info)));
    CHECK(Info.State == MI_MEM_RESERVE && Info.RegionSize == 0x100000 - 0x5000);

    CHECK(UserRead64(&World, 0, Base + 0x2008, &Status) == 0 && NT_SUCCESS(Status));
    CHECK(MI_ATOMIC_READ64(&Space.ResidentPages) == 1);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 0x3000, 0x1122334455667788ULL)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 0x2FFC, 0xA1A2A3A4B1B2B3B4ULL)));
    CHECK(UserRead64(&World, 0, Base + 0x2FFC, &Status) == 0xA1A2A3A4B1B2B3B4ULL);
    UserRead64(&World, 0, Base + 0x5000, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);

    CHECK(NT_SUCCESS(Protect(&Space, Base + 0x3000, 0x1000, MI_PROT_READONLY, &Old)));
    CHECK(Old == MI_PROT_READWRITE);
    CHECK(UserWrite64(&World, 0, Base + 0x3100, 1) == STATUS_ACCESS_VIOLATION);
    UserRead64(&World, 0, Base + 0x3100, &Status);
    CHECK(NT_SUCCESS(Status));
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x2000, &Info)));
    CHECK(Info.RegionSize == 0x1000 && Info.Protect == MI_PROT_READWRITE);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x3000, &Info)));
    CHECK(Info.RegionSize == 0x1000 && Info.Protect == MI_PROT_READONLY);

    CHECK(NT_SUCCESS(Protect(&Space, Base + 0x3000, 0x1000, MI_PROT_NOACCESS, &Old)));
    CHECK(Old == MI_PROT_READONLY);
    UserRead64(&World, 0, Base + 0x3100, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(Protect(&Space, Base + 0x3000, 0x1000, MI_PROT_READWRITE, &Old)));
    CHECK(Old == MI_PROT_NOACCESS);
    CHECK((UserRead64(&World, 0, Base + 0x3000, &Status) >> 32) == 0x11223344ULL);

    CHECK(Protect(&Space, Base, 0x1000, MI_PROT_READONLY, &Old) == STATUS_NOT_COMMITTED);
    CHECK(Protect(&Space, Base + 0x3000, 0x1000, 0x7F, &Old) == STATUS_INVALID_PAGE_PROTECTION);

    CHECK(NT_SUCCESS(Free(&Space, Base + 0x3000, 0x1000, MI_MEM_DECOMMIT)));
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 2);
    UserRead64(&World, 0, Base + 0x3000, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x3000, &Info)));
    CHECK(Info.State == MI_MEM_RESERVE && Info.RegionSize == 0x1000);

    Other = Base + 0x3000;
    CHECK(NT_SUCCESS(Alloc(&Space, &Other, 0x1000, MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(UserRead64(&World, 0, Base + 0x3000, &Status) == 0);
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 3);

    CHECK(Free(&Space, Base + 0x1000, 0, MI_MEM_RELEASE) == STATUS_FREE_VM_NOT_AT_BASE);
    CHECK(Free(&Space, Base, 0x200000, MI_MEM_RELEASE) == STATUS_UNABLE_TO_FREE_VM);
    CHECK(Free(&Space, Base, 0x200000, MI_MEM_DECOMMIT) == STATUS_UNABLE_TO_DECOMMIT_VM);
    CHECK(Free(&Space, Base + 0x40000000ULL, 0, MI_MEM_RELEASE) == STATUS_MEMORY_NOT_ALLOCATED);
    CHECK(Free(&Space, Base, 0, 0x1234) == STATUS_INVALID_PARAMETER);

    Other = Base + 0x8000;
    CHECK(Alloc(&Space, &Other, 0x1000, MI_MEM_RESERVE, MI_PROT_READWRITE) == STATUS_CONFLICTING_ADDRESSES);
    Other = Base + 0x40000000ULL;
    CHECK(Alloc(&Space, &Other, 0x1000, MI_MEM_COMMIT, MI_PROT_READWRITE) == STATUS_CONFLICTING_ADDRESSES);
    Other = 0;
    CHECK(Alloc(&Space, &Other, 0x1000, MI_MEM_RESERVE, 0) == STATUS_INVALID_PAGE_PROTECTION);
    CHECK(Alloc(&Space, &Other, 0x1000, MI_MEM_RESERVE, MI_PROT_WRITECOPY) == STATUS_INVALID_PAGE_PROTECTION);
    CHECK(Alloc(&Space, &Other, 0, MI_MEM_RESERVE, MI_PROT_READWRITE) == STATUS_INVALID_PARAMETER);
    CHECK(Alloc(&Space, &Other, 0x1000, 0, MI_PROT_READWRITE) == STATUS_INVALID_PARAMETER);

    CHECK(NT_SUCCESS(Free(&Space, Base + 0x40000, 0x10000, MI_MEM_RELEASE)));
    CHECK(Space.VadRoot.NodeCount == 2);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x48000, &Info)));
    CHECK(Info.State == MI_MEM_FREE && Info.BaseAddress == Base + 0x48000 && Info.RegionSize == 0x8000);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x50000, &Info)));
    CHECK(Info.State == MI_MEM_RESERVE && Info.AllocationBase == Base + 0x50000);
    CHECK(UserRead64(&World, 0, Base + 0x2FFC, &Status) == 0xB1B2B3B4ULL);
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 3);

    CHECK(NT_SUCCESS(Free(&Space, Base, 0x10000, MI_MEM_RELEASE)));
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 0);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x10000, &Info)));
    CHECK(Info.State == MI_MEM_RESERVE && Info.AllocationBase == Base + 0x10000 && Info.RegionSize == 0x30000);
    CHECK(NT_SUCCESS(Free(&Space, Base + 0x30000, 0x10000, MI_MEM_RELEASE)));
    CHECK(NT_SUCCESS(Free(&Space, Base + 0x10000, 0, MI_MEM_RELEASE)));
    CHECK(NT_SUCCESS(Free(&Space, Base + 0x50000, 0, MI_MEM_RELEASE)));
    CHECK(Space.VadRoot.NodeCount == 0);
    CHECK(MiVadCheck(&Space.VadRoot) == 0);

    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 2048);
    WorldDestroy(&World);
}

static
void
VmMemCommitAndLimits(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    MI_MEMORY_INFORMATION Info;
    ULONG64 Base = 0;
    ULONG64 Low = 0;
    ULONG64 High = 0;
    ULONG64 Fixed = USER_BASE;
    NTSTATUS Status;
    ULONG i;

    WorldCreate(&World, 2048, 1, 5000);
    ProcessCreate(&World, &Space);

    CHECK(NT_SUCCESS(Alloc(&Space, &Base, 16 << 20, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 4096);
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 4096);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base, &Info)));
    CHECK(Info.State == MI_MEM_COMMIT && Info.RegionSize == (16 << 20));

    for (i = 0; i < 64; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + (ULONG64)i * 0x40000 + 8, i + 1)));
    for (i = 0; i < 64; i++)
        CHECK(UserRead64(&World, 0, Base + (ULONG64)i * 0x40000 + 8, &Status) == i + 1);
    CHECK(MI_ATOMIC_READ64(&Space.ResidentPages) == 64);

    CHECK(NT_SUCCESS(Free(&Space, Base + 0x100000, 0x200000, MI_MEM_DECOMMIT)));
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 4096 - 512);
    UserRead64(&World, 0, Base + 0x140008, &Status);
    CHECK(Status == STATUS_ACCESS_VIOLATION);
    CHECK(UserRead64(&World, 0, Base + 0x300008, &Status) == 13);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x100000, &Info)));
    CHECK(Info.State == MI_MEM_RESERVE && Info.RegionSize == 0x200000);

    CHECK(Alloc(&Space, &Low, 2000 * 4096, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE) ==
          STATUS_COMMITMENT_LIMIT);
    Low = Base + 0x100000;
    CHECK(Alloc(&Space, &Low, 0x200000, MI_MEM_COMMIT, MI_PROT_READWRITE) == STATUS_SUCCESS);
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 4096);
    CHECK(UserRead64(&World, 0, Base + 0x140008, &Status) == 0);

    Low = 0;
    CHECK(NT_SUCCESS(Alloc(&Space, &Low, 0x10000, MI_MEM_RESERVE, MI_PROT_READONLY)));
    CHECK(NT_SUCCESS(Alloc(&Space, &High, 0x10000, MI_MEM_RESERVE | MI_MEM_TOP_DOWN, MI_PROT_READONLY)));
    CHECK(High > Low && High + 0x10000 - 1 <= Space.HighestVa);
    CHECK(NT_SUCCESS(Alloc(&Space, &Fixed, 0x1234, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_EXECUTE_READWRITE)));
    CHECK(Fixed == USER_BASE);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Fixed, &Info)));
    CHECK(Info.RegionSize == 0x2000 && Info.Protect == MI_PROT_EXECUTE_READWRITE);
    CHECK(NT_SUCCESS(MachineTouch(&World.Machine, 0, Fixed, MachineExecute, TRUE)));
    CHECK(MachineTouch(&World.Machine, 0, Base, MachineExecute, TRUE) == STATUS_ACCESS_VIOLATION);
    CHECK(MachineTouch(&World.Machine, 0, World.System.Arch->SystemAddressStart + 0x1000, MachineRead, TRUE) ==
          STATUS_ACCESS_VIOLATION);

    {
        ULONG64 Limited = 0;
        ULONG64 LimitedSize = 0x20000;
        ULONG64 Low32 = 0;

        CHECK(NT_SUCCESS(MiAllocateVirtualMemoryEx(&Space, &Limited, &LimitedSize, MI_MEM_RESERVE | MI_MEM_TOP_DOWN,
                                                   MI_PROT_READWRITE, 0x7FFEFFFFULL)));
        CHECK(Limited + 0x20000 - 1 <= 0x7FFEFFFFULL && Limited >= 0x7FF00000ULL);
        LimitedSize = 0x10000;
        CHECK(NT_SUCCESS(MiAllocateVirtualMemoryEx(&Space, &Low32, &LimitedSize, MI_MEM_RESERVE, MI_PROT_READWRITE,
                                                   0xFFFFFFFFULL)));
        CHECK(Low32 + 0x10000 - 1 <= 0xFFFFFFFFULL);
        Low32 = 0x100000000ULL;
        CHECK(MiAllocateVirtualMemoryEx(&Space, &Low32, &LimitedSize, MI_MEM_RESERVE, MI_PROT_READWRITE,
                                        0xFFFFFFFFULL) == STATUS_INVALID_PARAMETER);
        Low32 = 0;
        LimitedSize = 0x10000;
        CHECK(MiAllocateVirtualMemoryEx(&Space, &Low32, &LimitedSize, MI_MEM_RESERVE, MI_PROT_READWRITE,
                                        0x8000ULL) == STATUS_NO_MEMORY);
    }

    CHECK(NT_SUCCESS(Free(&Space, Base, 0, MI_MEM_RELEASE)));
    CHECK(MI_ATOMIC_READ64(&Space.CommittedPages) == 2);

    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 2048);
    WorldDestroy(&World);
}

static
NTSTATUS
AllocBounded(PMI_ADDRESS_SPACE Space, ULONG64 *Base, ULONG64 Size, ULONG Type, ULONG64 Lowest, ULONG64 Highest,
             ULONG64 Alignment)
{
    ULONG64 RegionSize = Size;

    return MiAllocateVirtualMemoryBounded(Space, Base, &RegionSize, Type, MI_PROT_READWRITE, Lowest, Highest,
                                          Alignment, FALSE);
}

static
void
VmAddressRequirements(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base;

    WorldCreate(&World, 256, 1, 5000);
    ProcessCreate(&World, &Space);

    Base = 0;
    CHECK(NT_SUCCESS(AllocBounded(&Space, &Base, 0x10000, MI_MEM_RESERVE, 0x40000000ULL, ~0ULL, 0)));
    CHECK(Base >= 0x40000000ULL && (Base & 0xFFFF) == 0);

    Base = 0;
    CHECK(NT_SUCCESS(AllocBounded(&Space, &Base, 0x10000, MI_MEM_RESERVE, 0x40010000ULL, 0x7FFFFFFFULL, 0x200000)));
    CHECK(Base >= 0x40010000ULL && Base + 0x10000 - 1 <= 0x7FFFFFFFULL && (Base & 0x1FFFFF) == 0);

    Base = 0;
    CHECK(NT_SUCCESS(AllocBounded(&Space, &Base, 0x10000, MI_MEM_RESERVE | MI_MEM_TOP_DOWN, 0x20000000ULL,
                                  0x3000FFFFULL, 0)));
    CHECK(Base == 0x30000000ULL);

    Base = 0;
    CHECK(NT_SUCCESS(AllocBounded(&Space, &Base, 0x10000, MI_MEM_RESERVE | MI_MEM_COMMIT, 0x20000000ULL,
                                  0x3000FFFFULL, 0)));
    CHECK(Base >= 0x20000000ULL && Base + 0x10000 - 1 <= 0x3000FFFFULL);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base, 0x1234)));
    CHECK(Free(&Space, Base, 0, MI_MEM_RELEASE) == STATUS_SUCCESS);

    Base = 0;
    CHECK(AllocBounded(&Space, &Base, 0x20000, MI_MEM_RESERVE, 0x50000000ULL, 0x5000FFFFULL, 0) == STATUS_NO_MEMORY);

    Base = USER_BASE;
    CHECK(AllocBounded(&Space, &Base, 0x10000, MI_MEM_RESERVE, 0x40000000ULL, ~0ULL, 0) ==
          STATUS_INVALID_PARAMETER);

    Base = 0;
    CHECK(AllocBounded(&Space, &Base, 0x200000, MI_MEM_RESERVE | MI_MEM_COMMIT | MI_MEM_LARGE_PAGES, 0x40000000ULL,
                       ~0ULL, 0) == STATUS_NOT_SUPPORTED);

    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
}

void
TestVm(void)
{
    VmWindowsProtection();
    VmExecutableWriteTracking();
    VmDynamicCodeEcExemption();
    VmCommitDecommitBatch();
    VmReserveCommitMatrix();
    VmMemCommitAndLimits();
    VmAddressRequirements();
}

static
void
FaultGuardAndCounters(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    MI_MEMORY_INFORMATION Info;
    ULONG64 Base = 0;
    NTSTATUS Status;
    ULONG Old;

    WorldCreate(&World, 1024, 1, 100000);
    World.Machine.StrictTlb = TRUE;
    ProcessCreate(&World, &Space);

    CHECK(NT_SUCCESS(Alloc(&Space, &Base, 0x10000, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(Protect(&Space, Base + 0x1000, 0x1000, MI_PROT_READWRITE | MI_PROT_GUARD, &Old)));
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x1000, &Info)));
    CHECK(Info.Protect == (MI_PROT_READWRITE | MI_PROT_GUARD) && Info.RegionSize == 0x1000);

    CHECK(UserWrite64(&World, 0, Base + 0x1000, 5) == STATUS_GUARD_PAGE_VIOLATION);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 0x1000, 5)));
    CHECK(UserRead64(&World, 0, Base + 0x1000, &Status) == 5);
    CHECK(NT_SUCCESS(MiQueryVirtualMemory(&Space, Base + 0x1000, &Info)));
    CHECK(Info.Protect == MI_PROT_READWRITE);

    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 0x2000, 7)));
    CHECK(NT_SUCCESS(Protect(&Space, Base + 0x2000, 0x1000, MI_PROT_READWRITE | MI_PROT_GUARD, &Old)));
    UserRead64(&World, 0, Base + 0x2000, &Status);
    CHECK(Status == STATUS_GUARD_PAGE_VIOLATION);
    CHECK(UserRead64(&World, 0, Base + 0x2000, &Status) == 7 && NT_SUCCESS(Status));

    CHECK(MI_ATOMIC_READ64(&Space.DemandZeroFaults) == 2);
    CHECK(MI_ATOMIC_READ64(&Space.TransitionFaults) == 1);

    UserRead64(&World, 0, Base + 0x3000, &Status);
    CHECK(MI_ATOMIC_READ64(&Space.DemandZeroFaults) == 3);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 0x3000, 9)));
    CHECK(MI_ATOMIC_READ64(&Space.DirtyFaults) == 1);
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + 0x3008, 9)));
    CHECK(MI_ATOMIC_READ64(&Space.DirtyFaults) == 1);

    {
        ULONG64 Stack = 0;
        ULONG64 Top, Guard, Size;
        ULONG Grown = 0;
        ULONG64 Probe;

        CHECK(NT_SUCCESS(Alloc(&Space, &Stack, 0x100000, MI_MEM_RESERVE, MI_PROT_READWRITE)));
        Top = Stack + 0x100000;
        Guard = Top - 0x3000;
        Size = 0x2000;
        Probe = Top - 0x2000;
        CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Probe, &Size, MI_MEM_COMMIT, MI_PROT_READWRITE)));
        Size = 0x1000;
        CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Guard, &Size, MI_MEM_COMMIT,
                                                 MI_PROT_READWRITE | MI_PROT_GUARD)));

        for (Probe = Top - 8; Probe >= Top - 0x20000; Probe -= 0x1000)
        {
            NTSTATUS Touch = UserWrite64(&World, 0, Probe, Probe);

            if (Touch == STATUS_GUARD_PAGE_VIOLATION)
            {
                ULONG64 Next = (Probe & ~0xFFFULL) - 0x1000;

                Size = 0x1000;
                CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Next, &Size, MI_MEM_COMMIT,
                                                         MI_PROT_READWRITE | MI_PROT_GUARD)));
                Touch = UserWrite64(&World, 0, Probe, Probe);
                Grown++;
            }

            CHECK(NT_SUCCESS(Touch));
        }

        CHECK(Grown == 0x20000 / 0x1000 - 2);
        for (Probe = Top - 8; Probe >= Top - 0x20000; Probe -= 0x1000)
            CHECK(UserRead64(&World, 0, Probe, &Status) == Probe);

        CHECK(NT_SUCCESS(Free(&Space, Stack, 0, MI_MEM_RELEASE)));
    }

    CHECK(MachineAccessMemory(&World.Machine, 0, Base, NULL, 1, MachineRead, FALSE) == STATUS_SUCCESS);
    CHECK(UserRead64(&World, 0, 0x1000, &Status) == 0xDEADDEADDEADDEADULL && Status == STATUS_ACCESS_VIOLATION);
    CHECK(MiFault(&Space, Space.HighestVa + 0x100000, MiFaultRead, TRUE) == STATUS_ACCESS_VIOLATION);

    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 1024);
    WorldDestroy(&World);
}

static
void
FaultOutOfMemory(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Touched = 0;
    ULONG i;

    WorldCreate(&World, 256, 1, 100000);
    ProcessCreate(&World, &Space);

    CHECK(NT_SUCCESS(Alloc(&Space, &Base, 1024 * 4096, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));

    for (i = 0; i < 1024; i++)
    {
        Status = UserWrite64(&World, 0, Base + (ULONG64)i * PAGE_SIZE, i);
        if (!NT_SUCCESS(Status))
            break;
        Touched++;
    }

    CHECK(Status == STATUS_NO_MEMORY);
    CHECK(Touched > 200 && Touched < 256);

    for (i = 0; i < Touched; i++)
        CHECK(UserRead64(&World, 0, Base + (ULONG64)i * PAGE_SIZE, NULL) == i);

    CHECK(NT_SUCCESS(Free(&Space, Base, 64 * 4096, MI_MEM_DECOMMIT)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + (ULONG64)Touched * PAGE_SIZE, 1)));

    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
}

static
void
FaultTableOutOfMemory(void)
{
    ULONG Available, Mode;

    for (Mode = 0; Mode < 2; Mode++)
    for (Available = 0; Available < 4; Available++)
    {
        TEST_WORLD World;
        MI_ADDRESS_SPACE Space;
        PMI_SEGMENT Segment = NULL;
        ULONG64 Base = 0, Size = PAGE_SIZE;
        ULONG Held[64];
        ULONG Count = 0;
        ULONG Frame, i;

        WorldCreate(&World, 64, 1, 1000);
        ProcessCreate(&World, &Space);
        if (Mode == 0)
        {
            CHECK(NT_SUCCESS(Alloc(&Space, &Base, PAGE_SIZE, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                   MI_PROT_READWRITE)));
        }
        else
        {
            CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentPageFileBacked, PAGE_SIZE,
                                             MI_PROT_READWRITE, NULL, NULL, NULL, 0, &Segment)));
            CHECK(NT_SUCCESS(MiMapView(&Space, Segment, &Base, 0, &Size, MI_PROT_READWRITE, 0)));
        }

        while ((Frame = MiPfnAllocatePage(&World.System.Pfn, MI_ALLOCATE_NO_RECLAIM)) != MI_FRAME_INVALID)
            Held[Count++] = Frame;
        for (i = 0; i < Available; i++)
            MiPfnFreePage(&World.System.Pfn, Held[--Count]);

        CHECK(MiFault(&Space, Base, MiFaultWrite, TRUE) == STATUS_NO_MEMORY);
        CHECK(Space.ResidentPages == 0 && Space.PrivatePages == 0);
        CHECK(Space.PageTablePages == 0);
        if (Mode == 0)
            CHECK(NT_SUCCESS(Free(&Space, Base, 0, MI_MEM_RELEASE)));
        else
            CHECK(NT_SUCCESS(MiUnmapView(&Space, Base)));
        CHECK(Space.PageTablePages == 0);
        CHECK(MiPtCheck(&Space) == 0);

        for (i = 0; i < Count; i++)
            MiPfnFreePage(&World.System.Pfn, Held[i]);
        ProcessDestroy(&World, &Space);
        if (Segment != NULL)
            MiSegmentDereference(Segment);
        WorldExpectClean(&World, 64);
        WorldDestroy(&World);
    }
}

void
TestFault(void)
{
    FaultGuardAndCounters();
    FaultOutOfMemory();
    FaultTableOutOfMemory();
}

static
ULONG
RangeCheck(TEST_WORLD *World, PMI_ADDRESS_SPACE Space, ULONG64 Base, ULONG Pages)
{
    ULONG Errors = 0;
    ULONG i;

    for (i = 0; i < Pages; i++)
    {
        ULONG64 Va = Base + (ULONG64)i * PAGE_SIZE;
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);
        MI_PTE Pte = Slot ? MiArchPteRead(Slot) : 0;
        PMI_PFN Entry;

        if (MiArchPteIsValid(Pte))
        {
            Entry = &World->PfnArray[MiArchPteFrame(Pte)];
            if (Entry->State != MiPageActive || Entry->PteAddress != MiPtSlotAddress(Slot, TableFrame, Va))
            {
                if (getenv("MM_DEBUG"))
                    fprintf(stderr, "page %u valid frame %llu state %u pteaddr %llx slot %llx\n", i,
                            MiArchPteFrame(Pte), Entry->State, Entry->PteAddress,
                            MiPtSlotAddress(Slot, TableFrame, Va));
                Errors++;
            }
        }
        else if (MiSoftKind(Pte) == MiSoftTransition)
        {
            Entry = &World->PfnArray[MiSoftValue(Pte)];
            if (Entry->State == MiPageActive || Entry->PteAddress != MiPtSlotAddress(Slot, TableFrame, Va))
            {
                if (getenv("MM_DEBUG"))
                    fprintf(stderr, "page %u transition frame %llu state %u pteaddr %llx slot %llx orig %llx\n", i,
                            MiSoftValue(Pte), Entry->State, Entry->PteAddress,
                            MiPtSlotAddress(Slot, TableFrame, Va), (ULONG64)Entry->OriginalPte);
                Errors++;
            }
        }
    }

    return Errors;
}

static
void
PagingRoundTrip(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base = 0;
    const ULONG Pages = 600;
    NTSTATUS Status;
    ULONG Trimmed;
    ULONG i;

    WorldCreate(&World, 512, 1, 100000);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 2048);
    ProcessCreate(&World, &Space);
    World.Paging.ProbeLock = &Space.Lock;

    CHECK(NT_SUCCESS(Alloc(&Space, &Base, (ULONG64)Pages * 4096, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));

    for (i = 0; i < Pages; i++)
    {
        Status = UserWrite64(&World, 0, Base + (ULONG64)i * PAGE_SIZE + 16, 0xC0DE0000ULL + i);
        if (Status == STATUS_NO_MEMORY)
        {
            CHECK(RangeCheck(&World, &Space, Base, Pages) == 0);
            Trimmed = MiTrimAddressSpace(&Space, 128, TRUE);
            CHECK(Trimmed != 0);
            CHECK(RangeCheck(&World, &Space, Base, Pages) == 0);
            CHECK(MiWriteModifiedPages(&World.System, 128) != 0);
            CHECK(RangeCheck(&World, &Space, Base, Pages) == 0);
            Status = UserWrite64(&World, 0, Base + (ULONG64)i * PAGE_SIZE + 16, 0xC0DE0000ULL + i);
        }

        CHECK(NT_SUCCESS(Status));
        if (RangeCheck(&World, &Space, Base, Pages) != 0)
        {
            CHECK(!"range check after write");
            fprintf(stderr, "first violation after writing page %u\n", i);
            break;
        }
    }

    CHECK(MI_ATOMIC_READ64(&World.Paging.PageFile.PagesWritten) != 0);

    for (i = 0; i < Pages; i++)
    {
        ULONG64 Value = UserRead64(&World, 0, Base + (ULONG64)i * PAGE_SIZE + 16, &Status);

        if (Status == STATUS_NO_MEMORY)
        {
            MiTrimAddressSpace(&Space, 128, TRUE);
            MiWriteModifiedPages(&World.System, 128);
            Value = UserRead64(&World, 0, Base + (ULONG64)i * PAGE_SIZE + 16, &Status);
        }

        if (getenv("MM_DEBUG") && !(NT_SUCCESS(Status) && Value == 0xC0DE0000ULL + i))
            fprintf(stderr, "page %u status %x value %llx\n", i, Status, Value);
        CHECK(NT_SUCCESS(Status) && Value == 0xC0DE0000ULL + i);
    }

    CHECK(MI_ATOMIC_READ64(&Space.PageFileFaults) != 0);
    CHECK(World.Paging.IoUnderLock == 0);
    CHECK(MI_ATOMIC_READ64(&World.System.Pfn.Repurposed) != 0);
    CHECK(World.Paging.PageFile.SlotsInUse != 0);
    CHECK(MI_ATOMIC_READ64(&Space.PrivatePages) == Pages);

    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

static
void
PagingAgingAndSoftFaults(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base = 0;
    NTSTATUS Status;
    ULONG i;

    WorldCreate(&World, 1024, 1, 100000);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 512);
    ProcessCreate(&World, &Space);

    CHECK(NT_SUCCESS(Alloc(&Space, &Base, 64 * 4096, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    for (i = 0; i < 64; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + (ULONG64)i * PAGE_SIZE, i + 100)));

    CHECK(MiTrimAddressSpace(&Space, 64, FALSE) == 0);
    for (i = 0; i < 32; i++)
        UserRead64(&World, 0, Base + (ULONG64)i * PAGE_SIZE, &Status);
    CHECK(MiTrimAddressSpace(&Space, 64, FALSE) == 32);
    CHECK(MI_ATOMIC_READ64(&Space.ResidentPages) == 32);
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageModified) == 32);

    for (i = 32; i < 40; i++)
        CHECK(UserRead64(&World, 0, Base + (ULONG64)i * PAGE_SIZE, &Status) == i + 100);
    CHECK(MI_ATOMIC_READ64(&Space.TransitionFaults) == 8);
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageModified) == 24);

    CHECK(MiWriteModifiedPages(&World.System, 1000) == 24);
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageStandby) == 24);
    CHECK(World.Paging.PageFile.SlotsInUse == 24);

    for (i = 40; i < 48; i++)
        CHECK(UserRead64(&World, 0, Base + (ULONG64)i * PAGE_SIZE, &Status) == i + 100);
    CHECK(MI_ATOMIC_READ64(&Space.TransitionFaults) == 16);
    CHECK(MiTrimAddressSpace(&Space, 1000, TRUE) == 48);
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageStandby) == 24);
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageModified) == 40);

    World.Paging.FailWrites = 1;
    CHECK(MiWriteModifiedPages(&World.System, 1000) == 0);
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageModified) == 40);
    World.Paging.FailWrites = 0;
    CHECK(MiWriteModifiedPages(&World.System, 1000) == 40);
    CHECK(World.Paging.PageFile.SlotsInUse == 64);

    CHECK(NT_SUCCESS(Free(&Space, Base, 16 * 4096, MI_MEM_DECOMMIT)));
    CHECK(World.Paging.PageFile.SlotsInUse == 48);

    for (i = 16; i < 64; i++)
        CHECK(UserRead64(&World, 0, Base + (ULONG64)i * PAGE_SIZE, &Status) == i + 100);

    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 1024);
    WorldDestroy(&World);
}

static
void
PagingInPageError(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG Held[128];
    ULONG HeldCount = 0;
    ULONG64 Base = 0;
    NTSTATUS Status;
    ULONG Frame;
    ULONG i;

    WorldCreate(&World, 128, 1, 100000);
    WorldAttachPageFile(&World, 512);
    ProcessCreate(&World, &Space);

    CHECK(NT_SUCCESS(Alloc(&Space, &Base, 16 * 4096, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    for (i = 0; i < 16; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + (ULONG64)i * PAGE_SIZE, i + 1)));

    CHECK(MiTrimAddressSpace(&Space, 16, TRUE) == 16);
    CHECK(MiWriteModifiedPages(&World.System, 16) == 16);

    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
        Held[HeldCount++] = Frame;

    CHECK(MiPfnListCount(&World.System.Pfn, MiPageStandby) == 0);
    UserRead64(&World, 0, Base, &Status);
    CHECK(Status == STATUS_NO_MEMORY);

    for (i = 0; i < HeldCount; i++)
        MiPfnShareDecrement(&World.System.Pfn, Held[i], TRUE);

    World.Paging.FailReads = 1;
    UserRead64(&World, 0, Base, &Status);
    CHECK(Status == STATUS_IN_PAGE_ERROR);
    World.Paging.FailReads = 0;

    for (i = 0; i < 16; i++)
        CHECK(UserRead64(&World, 0, Base + (ULONG64)i * PAGE_SIZE, &Status) == i + 1);
    CHECK(MI_ATOMIC_READ64(&Space.PageFileFaults) == 16);

    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 128);
    WorldDestroy(&World);
}

static
void
PagingCleanPrivateAccounting(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Spaces[2];
    ULONG64 Bases[2] = { 0, 0 };
    ULONG Held[128];
    ULONG Count = 0;
    ULONG Frame;
    ULONG i, j;
    NTSTATUS Status;

    WorldCreate(&World, 128, 2, 100000);
    World.Machine.StrictTlb = TRUE;

    for (i = 0; i < 2; i++)
    {
        CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World.System, &Spaces[i])));
        WorldAttach(&World, i, &Spaces[i]);
        CHECK(NT_SUCCESS(Alloc(&Spaces[i], &Bases[i], 16 * PAGE_SIZE, MI_MEM_RESERVE | MI_MEM_COMMIT,
                               MI_PROT_READWRITE)));
        for (j = 0; j < 16; j++)
            CHECK(UserRead64(&World, i, Bases[i] + (ULONG64)j * PAGE_SIZE, &Status) == 0 && NT_SUCCESS(Status));
        CHECK(MI_ATOMIC_READ64(&Spaces[i].PrivatePages) == 16);
        CHECK(MiTrimAddressSpace(&Spaces[i], 16, TRUE) == 16);
        CHECK(MI_ATOMIC_READ64(&Spaces[i].PrivatePages) == 16);
    }

    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
        Held[Count++] = Frame;

    CHECK(MI_ATOMIC_READ64(&World.System.Pfn.Repurposed) == 32);
    for (i = 0; i < 2; i++)
        CHECK(MI_ATOMIC_READ64(&Spaces[i].PrivatePages) == 0);

    for (i = 0; i < Count; i++)
        MiPfnShareDecrement(&World.System.Pfn, Held[i], TRUE);

    for (i = 0; i < 2; i++)
    {
        CHECK(UserRead64(&World, i, Bases[i], &Status) == 0 && NT_SUCCESS(Status));
        CHECK(MI_ATOMIC_READ64(&Spaces[i].PrivatePages) == 1);
        MiCleanAddressSpace(&Spaces[i]);
        CHECK(MI_ATOMIC_READ64(&Spaces[i].PrivatePages) == 0);
        WorldAttach(&World, i, NULL);
        MiAddressSpaceDestroy(&Spaces[i]);
    }

    WorldExpectClean(&World, 128);
    WorldDestroy(&World);
}

static ULONG ExpandCalls;
static LONG64 ExpandMaximum;

static
BOOLEAN
TestExpandCommit(PMI_SYSTEM System, LONG64 Pages, LONG64 Limit, BOOLEAN Wait)
{
    __sync_fetch_and_add(&ExpandCalls, 1);
    if (Pages == 0 || !Wait || Limit + Pages > ExpandMaximum)
        return FALSE;

    if (!NT_SUCCESS(MiPageFileExtend(System->PageFile, System->PageFile->SlotCount + (ULONG64)Pages)))
        return FALSE;

    MI_ATOMIC_ADD64(&System->CommitLimit, Pages);
    return TRUE;
}

static
void
PagingCommitExpansion(void)
{
    TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base[4] = { 0, 0, 0, 0 };
    ULONG64 Slots[64];
    ULONG64 Slot;
    ULONG Count = 0;
    ULONG i;

    WorldCreate(&World, 256, 1, 64);
    WorldAttachPageFile(&World, 16);
    World.System.ExpandCommit = TestExpandCommit;
    ExpandCalls = 0;
    ExpandMaximum = 128;

    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World.System, &Space)));
    CHECK(NT_SUCCESS(Alloc(&Space, &Base[0], 32 * PAGE_SIZE, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(ExpandCalls == 0);

    CHECK(NT_SUCCESS(Alloc(&Space, &Base[1], 40 * PAGE_SIZE, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(ExpandCalls == 1);
    CHECK(MI_ATOMIC_READ64(&World.System.CommitLimit) == 104);
    CHECK(World.Paging.PageFile.SlotCount == 56);

    CHECK(NT_SUCCESS(Alloc(&Space, &Base[2], 28 * PAGE_SIZE, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(ExpandCalls == 2);

    CHECK(!NT_SUCCESS(Alloc(&Space, &Base[3], 40 * PAGE_SIZE, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(MI_ATOMIC_READ64(&World.System.CommitLimit) == 104);
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 100);

    while (Count < 64 && (Slot = MiPageFileReserveSlot(&World.Paging.PageFile)) != 0)
        Slots[Count++] = Slot;
    CHECK(Count == 55);
    for (i = 0; i < Count; i++)
        MiPageFileReleaseSlot(&World.Paging.PageFile, Slots[i]);
    CHECK(World.Paging.PageFile.SlotsInUse == 0);

    MiCleanAddressSpace(&Space);
    CHECK(MI_ATOMIC_READ64(&World.System.CommittedPages) == 0);
    MiAddressSpaceDestroy(&Space);
    WorldDestroy(&World);
}

void
TestPaging(void)
{
    PagingRoundTrip();
    PagingAgingAndSoftFaults();
    PagingInPageError();
    PagingCleanPrivateAccounting();
    PagingCommitExpansion();
}
