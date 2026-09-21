/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_proc.c
 * PURPOSE:     Process address-space host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include <nvs/include/miproc.h>

static
NTSTATUS
WriteWithBalance(TEST_WORLD *World, PMI_PROCESS_MANAGER Manager, ULONG Cpu, ULONG64 Va, ULONG64 Value)
{
    NTSTATUS Status = UserWrite64(World, Cpu, Va, Value);
    ULONG Attempt;

    for (Attempt = 0; Status == STATUS_NO_MEMORY && Attempt < 8; Attempt++)
    {
        MiBalanceMemory(&World->System, Manager);
        Status = UserWrite64(World, Cpu, Va, Value);
    }

    return Status;
}

static
ULONG64
ReadWithBalance(TEST_WORLD *World, PMI_PROCESS_MANAGER Manager, ULONG Cpu, ULONG64 Va, NTSTATUS *Status)
{
    ULONG64 Value = UserRead64(World, Cpu, Va, Status);
    ULONG Attempt;

    for (Attempt = 0; *Status == STATUS_NO_MEMORY && Attempt < 8; Attempt++)
    {
        MiBalanceMemory(&World->System, Manager);
        Value = UserRead64(World, Cpu, Va, Status);
    }

    return Value;
}

static
void
ProcLifecycle(void)
{
    static TEST_WORLD World;
    static MI_PROCESS_MANAGER Manager;
    static MI_PROCESS Process[3];
    MI_PROCESS_COUNTERS Counters;
    ULONG64 Peb, Teb1, Teb2, Other;
    ULONG64 Value = 0x5A5A0001;
    ULONG64 Base = 0, Size;
    NTSTATUS Status;
    ULONG Held[1024];
    ULONG HeldCount = 0;
    ULONG Frame;
    ULONG i;

    WorldCreate(&World, 1024, 3, 1000000);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 4096);
    CHECK(NT_SUCCESS(MiSystemPtesInitialize(&World.System, 4096, 3)));
    CHECK(NT_SUCCESS(MiProcessManagerInitialize(&World.System, &Manager)));

    for (i = 0; i < 3; i++)
    {
        CHECK(NT_SUCCESS(MiProcessCreate(&World.System, &Manager, &Process[i])));
        WorldAttach(&World, i, &Process[i].Space);
    }
    CHECK(Manager.ProcessCount == 3);

    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Manager.UserSharedSystemVa + 0x320, &Value, 8,
                                         MachineWrite, FALSE)));
    for (i = 0; i < 3; i++)
    {
        CHECK(UserRead64(&World, i, MI_SHARED_USER_DATA_VA + 0x320, &Status) == 0x5A5A0001);
        CHECK(UserWrite64(&World, i, MI_SHARED_USER_DATA_VA + 0x320, 1) == STATUS_ACCESS_VIOLATION);
    }

    Process[0].Space.BottomUpVa = 0x100030000ULL;
    CHECK(NT_SUCCESS(MiProcessCreatePeb(&Process[0], 0x1000, &Peb)));
    CHECK(MiProcessCreatePeb(&Process[0], 0x1000, &Other) == STATUS_INVALID_PARAMETER);
    CHECK(NT_SUCCESS(MiProcessCreateTeb(&Process[0], 0x2000, &Teb1)));
    CHECK(NT_SUCCESS(MiProcessCreateTeb(&Process[0], 0x2000, &Teb2)));
    CHECK(Peb == Process[0].Space.BottomUpVa && Peb < Teb1 && Teb1 < Teb2);
    CHECK(Teb2 + 0x2000 - 1 < Process[0].Space.HighestVa - 0x3FFFFFFFULL);

    Base = 0;
    Size = 0x10000;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemoryEx(&Process[1].Space, &Base, &Size, MI_MEM_RESERVE, MI_PROT_READWRITE,
                                               ~0ULL)));
    CHECK(Base == Process[1].Space.LowestVa);
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Process[1].Space, &Base, &Size, MI_MEM_RELEASE)));
    Process[1].Space.BottomUpVa = 0x2000010000ULL;
    Base = 0;
    Size = 0x10000;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemoryEx(&Process[1].Space, &Base, &Size, MI_MEM_RESERVE, MI_PROT_READWRITE,
                                               ~0ULL)));
    CHECK(Base == 0x2000010000ULL);
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Process[1].Space, &Base, &Size, MI_MEM_RELEASE)));
    Base = 0;
    Size = 0x10000;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemoryEx(&Process[1].Space, &Base, &Size, MI_MEM_RESERVE, MI_PROT_READWRITE,
                                               0xFFFFFFFFULL)));
    CHECK(Base >= Process[1].Space.LowestVa && Base + Size - 1 <= 0xFFFFFFFFULL);
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Process[1].Space, &Base, &Size, MI_MEM_RELEASE)));
    Base = 0;
    Size = 0x10000;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemoryEx(&Process[1].Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_TOP_DOWN,
                                               MI_PROT_READWRITE, ~0ULL)));
    CHECK(Base > Process[1].Space.BottomUpVa && Base + Size - 1 <= Process[1].Space.HighestVa);
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Process[1].Space, &Base, &Size, MI_MEM_RELEASE)));
    Process[1].Space.TopDownVa = 0x7FF5FFFEFFFFULL;
    Base = 0;
    Size = 0x10000;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemoryEx(&Process[1].Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_TOP_DOWN,
                                               MI_PROT_READWRITE, ~0ULL)));
    CHECK(Base == 0x7FF5FFFE0000ULL);
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Process[1].Space, &Base, &Size, MI_MEM_RELEASE)));
    Base = 0;
    Size = 0x10000;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemoryEx(&Process[1].Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_TOP_DOWN,
                                               MI_PROT_READWRITE, 0xFFFFFFFFULL)));
    CHECK(Base == 0xFFFF0000ULL);
    CHECK(NT_SUCCESS(MiFreeVirtualMemory(&Process[1].Space, &Base, &Size, MI_MEM_RELEASE)));
    Base = 0;
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Teb1 + 0x30, Teb1)));
    CHECK(NT_SUCCESS(MiProcessDeleteTeb(&Process[0], Teb1)));
    CHECK(UserRead64(&World, 0, Teb1 + 0x30, &Status) != Teb1 && Status == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(MiProcessCreateTeb(&Process[0], 0x2000, &Other)));
    CHECK(Other == Teb1);

    Size = 300 * 4096ULL;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process[0].Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                             MI_PROT_READWRITE)));
    for (i = 0; i < 300; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + i * 4096ULL, i)));

    MiProcessQueryCounters(&Process[0], &Counters);
    CHECK(Counters.WorkingSetSize >= 300 * 4096ULL && Counters.PageFaultCount >= 300);
    CHECK(Counters.PagefileUsage == (300 + 1 + 2 + 2) * 4096ULL);
    CHECK(Counters.PeakWorkingSetSize == Counters.WorkingSetSize && Counters.PageTablePages != 0);

    CHECK(MiProcessSetWorkingSetLimits(&Process[0], 0, 10, FALSE) == STATUS_INVALID_PARAMETER);
    CHECK(MiProcessSetWorkingSetLimits(&Process[0], 20, 10, FALSE) == STATUS_INVALID_PARAMETER);
    CHECK(NT_SUCCESS(MiProcessSetWorkingSetLimits(&Process[0], 20, 100, TRUE)));
    CHECK(MI_ATOMIC_READ64(&Process[0].Space.ResidentPages) <= 100);
    CHECK(NT_SUCCESS(MiProcessSetWorkingSetLimits(&Process[0], ~0ULL, ~0ULL, FALSE)));
    CHECK(MI_ATOMIC_READ64(&Process[0].Space.ResidentPages) == 0);
    MiProcessQueryCounters(&Process[0], &Counters);
    CHECK(Counters.WorkingSetSize == 0 && Counters.PeakWorkingSetSize >= 300 * 4096ULL);

    while (MiWriteModifiedPages(&World.System, 1024) != 0)
        ;
    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, 0)) != MI_FRAME_INVALID)
        Held[HeldCount++] = Frame;
    for (i = 0; i < HeldCount; i++)
        MiPfnShareDecrement(&World.System.Pfn, Held[i], TRUE);

    CHECK(UserRead64(&World, 1, MI_SHARED_USER_DATA_VA + 0x320, &Status) == 0x5A5A0001);
    for (i = 0; i < 300; i += 13)
        CHECK(UserRead64(&World, 0, Base + i * 4096ULL, &Status) == i);

    for (i = 0; i < 3; i++)
    {
        WorldAttach(&World, i, NULL);
        MiProcessDelete(&Manager, &Process[i]);
    }

    CHECK(Manager.ProcessCount == 0);
    MiProcessManagerUninitialize(&World.System, &Manager);
    MiSystemPtesUninitialize(&World.System);
    WorldExpectClean(&World, 1024);
    WorldDestroy(&World);
}

static
void
ProcBalanceAndCopy(void)
{
    static TEST_WORLD World;
    static MI_PROCESS_MANAGER Manager;
    static MI_PROCESS Process[3];
    ULONG64 Base[3] = { 0, 0, 0 };
    ULONG64 Small = 0;
    ULONG64 Size;
    ULONG64 Copied;
    ULONG64 ReadOnly = 0;
    NTSTATUS Status;
    ULONG Old;
    ULONG p, i;

    WorldCreate(&World, 1024, 3, 1000000);
    World.Machine.StrictTlb = TRUE;
    WorldAttachPageFile(&World, 8192);
    CHECK(NT_SUCCESS(MiSystemPtesInitialize(&World.System, 4096, 3)));
    CHECK(NT_SUCCESS(MiProcessManagerInitialize(&World.System, &Manager)));

    for (p = 0; p < 3; p++)
    {
        CHECK(NT_SUCCESS(MiProcessCreate(&World.System, &Manager, &Process[p])));
        WorldAttach(&World, p, &Process[p].Space);
    }

    Size = 30 * 4096ULL;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process[2].Space, &Small, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                             MI_PROT_READWRITE)));
    for (i = 0; i < 30; i++)
        CHECK(NT_SUCCESS(UserWrite64(&World, 2, Small + i * 4096ULL, 0x5000 + i)));

    for (p = 0; p < 2; p++)
    {
        Size = 700 * 4096ULL;
        CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process[p].Space, &Base[p], &Size,
                                                 MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
        for (i = 0; i < 700; i++)
            CHECK(NT_SUCCESS(WriteWithBalance(&World, &Manager, p, Base[p] + i * 4096ULL, ((ULONG64)p << 32) | i)));
    }

    CHECK(MI_ATOMIC_READ64(&Manager.PagesTrimmed) != 0);
    CHECK(MI_ATOMIC_READ64(&Process[2].Space.ResidentPages) >= 30);

    for (p = 0; p < 2; p++)
    {
        for (i = 0; i < 700; i += 3)
            CHECK(ReadWithBalance(&World, &Manager, p, Base[p] + i * 4096ULL, &Status) == (((ULONG64)p << 32) | i));
    }

    CHECK(NT_SUCCESS(MiCopyVirtualMemory(&Process[0].Space, Base[0] + 0x1FF8, &Process[2].Space, Small + 0x2FFC,
                                         0x3010, TRUE, &Copied)));
    CHECK(Copied == 0x3010);
    CHECK(UserRead64(&World, 2, Small + 0x2FFC + 8, &Status) == 2);
    CHECK(UserRead64(&World, 2, Small + 0x2FFC + 8 + 0x3000, &Status) == 5);
    CHECK(UserRead64(&World, 2, Small + 0x7000, &Status) == 0x5007);

    Size = 0x2000;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Process[2].Space, &ReadOnly, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                             MI_PROT_READWRITE)));
    CHECK(NT_SUCCESS(MiProtectVirtualMemory(&Process[2].Space, &(ULONG64){ReadOnly + 0x1000}, &(ULONG64){0x1000},
                                            MI_PROT_READONLY, &Old)));
    CHECK(MiCopyVirtualMemory(&Process[0].Space, Base[0], &Process[2].Space, ReadOnly + 0x800, 0x1000, TRUE,
                              &Copied) == STATUS_PARTIAL_COPY);
    CHECK(Copied == 0x800);
    CHECK(MiCopyVirtualMemory(&Process[0].Space, Base[0], &Process[2].Space, ReadOnly + 0x1000, 0x10, TRUE,
                              &Copied) == STATUS_ACCESS_VIOLATION);
    CHECK(MiCopyVirtualMemory(&Process[0].Space, 0x1000, &Process[2].Space, ReadOnly, 0x10, TRUE, &Copied) ==
          STATUS_ACCESS_VIOLATION);
    CHECK(MiCopyVirtualMemory(&World.System.SystemSpace, Manager.UserSharedSystemVa, &Process[2].Space,
                              ReadOnly, 0x10, TRUE, &Copied) == STATUS_ACCESS_VIOLATION);
    CHECK(NT_SUCCESS(MiCopyVirtualMemory(&World.System.SystemSpace, Manager.UserSharedSystemVa,
                                         &Process[2].Space, ReadOnly, 0x10, FALSE, &Copied)));

    for (p = 0; p < 3; p++)
    {
        WorldAttach(&World, p, NULL);
        MiProcessDelete(&Manager, &Process[p]);
    }

    MiProcessManagerUninitialize(&World.System, &Manager);
    MiSystemPtesUninitialize(&World.System);
    WorldExpectClean(&World, 1024);
    WorldDestroy(&World);
}

static
void
ProcAdoptedSharedPage(void)
{
    static TEST_WORLD World;
    static MI_PROCESS_MANAGER Manager;
    static MI_PROCESS Process;
    NTSTATUS Status;
    ULONG Frame;

    WorldCreate(&World, 512, 1, 1000000);
    CHECK(NT_SUCCESS(MiSystemPtesInitialize(&World.System, 1024, 1)));
    Frame = MiPfnAllocatePage(&World.System.Pfn, MI_ALLOCATE_ZEROED);
    *(ULONG64 *)(MachineFrame(&World.Machine, Frame) + 0x14) = 0x7FFE7FFE;

    CHECK(NT_SUCCESS(MiProcessManagerInitializeEx(&World.System, &Manager, Frame, 0xFFFFF78000000000ULL)));
    CHECK(NT_SUCCESS(MiProcessCreate(&World.System, &Manager, &Process)));
    WorldAttach(&World, 0, &Process.Space);
    CHECK(UserRead64(&World, 0, MI_SHARED_USER_DATA_VA + 0x14, &Status) == 0x7FFE7FFE);
    *(ULONG64 *)(MachineFrame(&World.Machine, Frame) + 0x14) = 0x1234;
    CHECK(UserRead64(&World, 0, MI_SHARED_USER_DATA_VA + 0x14, &Status) == 0x1234);
    CHECK(MiTrimAddressSpace(&Process.Space, 100, TRUE) == 1);
    CHECK(World.PfnArray[Frame].State == MiPageActive && World.PfnArray[Frame].ShareCount == 1);
    CHECK(UserRead64(&World, 0, MI_SHARED_USER_DATA_VA + 0x14, &Status) == 0x1234);

    WorldAttach(&World, 0, NULL);
    MiProcessDelete(&Manager, &Process);
    CHECK(World.PfnArray[Frame].State == MiPageActive);
    MiProcessManagerUninitialize(&World.System, &Manager);
    MiSystemPtesUninitialize(&World.System);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

void
TestProcess(void)
{
    ProcAdoptedSharedPage();
    ProcLifecycle();
    ProcBalanceAndCopy();
}
