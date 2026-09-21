/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     User address space layout of the calling process
 */

#include <kmt_test.h>

#define LAYOUT_GB 0x40000000ULL
#define LAYOUT_SHARED_USER_DATA 0x7FFE0000ULL

static
ULONG_PTR
TraceAllocation(
    _In_ ULONG AllocationType,
    _In_z_ PCSTR Label)
{
    PVOID Base = NULL;
    SIZE_T Size = 0x10000;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Base, 0, &Size, MEM_RESERVE | AllocationType, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return 0;

    trace("layout %s base=%p\n", Label, Base);
    Size = 0;
    Status = ZwFreeVirtualMemory(ZwCurrentProcess(), &Base, &Size, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return (ULONG_PTR)Base;
}

START_TEST(MmUserLayout)
{
    MEMORY_BASIC_INFORMATION Info;
    ULONG_PTR Address = 0, End, LastGb, BottomUp, TopDown;
    SIZE_T Length;
    NTSTATUS Status;
    ULONG Regions = 0, LowRegions = 0, HighRegions = 0;

    LastGb = (ULONG_PTR)MmHighestUserAddress + 1 - LAYOUT_GB;
    trace("layout highest=%p probe=%p\n", MmHighestUserAddress, (PVOID)MmUserProbeAddress);

    Status = ZwQueryVirtualMemory(ZwCurrentProcess(), NULL, MemoryBasicInformation, &Info, sizeof(Info), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Info.BaseAddress, NULL);
        ok_eq_hex(Info.State, MEM_FREE);
    }

    while (Address < (ULONG_PTR)MmHighestUserAddress && Regions < 4096)
    {
        Status = ZwQueryVirtualMemory(ZwCurrentProcess(), (PVOID)Address, MemoryBasicInformation, &Info,
                                      sizeof(Info), &Length);
        if (!NT_SUCCESS(Status))
            break;

        End = (ULONG_PTR)Info.BaseAddress + Info.RegionSize;
        if (End <= Address)
            break;

        if (Info.State != MEM_FREE)
        {
            trace("layout region base=%p alloc=%p size=0x%Ix state=0x%lx type=0x%lx protect=0x%lx\n",
                  Info.BaseAddress, Info.AllocationBase, Info.RegionSize, Info.State, Info.Type, Info.Protect);
            if ((ULONG_PTR)Info.BaseAddress < LAYOUT_GB)
                LowRegions++;
            if (Info.State == MEM_COMMIT && End > LastGb)
                HighRegions++;
        }

        Address = End;
        Regions++;
    }

    ok(Regions != 0, "No regions enumerated\n");
    ok_eq_ulong(LowRegions, 0);
    ok_eq_ulong(HighRegions, 0);

    Status = ZwQueryVirtualMemory(ZwCurrentProcess(), (PVOID)(ULONG_PTR)LAYOUT_SHARED_USER_DATA,
                                  MemoryBasicInformation, &Info, sizeof(Info), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Info.BaseAddress, (PVOID)(ULONG_PTR)LAYOUT_SHARED_USER_DATA);
        ok_eq_hex(Info.State, MEM_COMMIT);
        ok_eq_hex(Info.Protect, PAGE_READONLY);
        ok_eq_hex(Info.Type, MEM_PRIVATE);
    }

    BottomUp = TraceAllocation(0, "bottom-up");
    TopDown = TraceAllocation(MEM_TOP_DOWN, "top-down");
    ok(BottomUp >= LAYOUT_GB && BottomUp < LastGb, "bottom-up allocation at %p\n", (PVOID)BottomUp);
    ok(TopDown >= LAYOUT_GB && TopDown < LastGb, "top-down allocation at %p\n", (PVOID)TopDown);
    ok(TopDown > BottomUp, "top-down %p not above bottom-up %p\n", (PVOID)TopDown, (PVOID)BottomUp);
}
