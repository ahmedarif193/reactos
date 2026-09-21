/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/machine/machine.c
 * PURPOSE:     Host-native memory manager machine model
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "machine.h"
#include <sched.h>

PMACHINE MachineCurrent;
_Thread_local ULONG MachineCpu;

#if defined(MACHINE_ARM64)
#define HW_VALID(p)        (((p) & 1) != 0)
#define HW_IS_TABLE(p, l)  ((l) != 0 && ((p) & 3) == 3)
#define HW_IS_LEAF(p, l)   (((l) == 0 && ((p) & 3) == 3) || ((l) != 0 && ((p) & 3) == 1))
#define HW_FRAME(p)        (((p) & 0x0000FFFFFFFFF000ULL) >> 12)
#define HW_USER(p)         (((p) & (1ULL << 6)) != 0)
#define HW_WRITE_OK(p)     (((p) & (1ULL << 7)) == 0)
#define HW_EXEC_OK(p, u)   (((p) & ((u) ? (1ULL << 54) : (1ULL << 53))) == 0)
#define HW_ACCESSED(p)     (((p) & (1ULL << 10)) != 0)
#define HW_SET_ACCESSED(p) ((p) | (1ULL << 10))
#define HW_CAN_AUTO_DIRTY(p) (((p) & (1ULL << 51)) != 0)
#define HW_SET_DIRTY(p)    ((p) & ~(1ULL << 7))
#define HW_TABLE_USER_OK(p) 1
#define HW_TABLE_WRITE_OK(p) 1
#define HW_SPLIT_ROOTS     1
#define HW_BREAK_SENSITIVE (0x0000FFFFFFFFF000ULL | (7ULL << 2) | (3ULL << 8) | (1ULL << 11) | 2ULL)
#elif defined(MACHINE_AMD64)
#define HW_VALID(p)        (((p) & 1) != 0)
#define HW_IS_TABLE(p, l)  ((l) != 0 && ((p) & (1ULL << 7)) == 0)
#define HW_IS_LEAF(p, l)   ((l) == 0 || ((p) & (1ULL << 7)) != 0)
#define HW_FRAME(p)        (((p) & 0x000FFFFFFFFFF000ULL) >> 12)
#define HW_USER(p)         (((p) & (1ULL << 2)) != 0)
#define HW_WRITE_OK(p)     (((p) & (1ULL << 1)) != 0)
#define HW_EXEC_OK(p, u)   (((p) & (1ULL << 63)) == 0)
#define HW_ACCESSED(p)     (((p) & (1ULL << 5)) != 0)
#define HW_SET_ACCESSED(p) ((p) | (1ULL << 5))
#define HW_CAN_AUTO_DIRTY(p) 0
#define HW_SET_DIRTY(p)    ((p) | (1ULL << 6))
#define HW_TABLE_USER_OK(p) (((p) & (1ULL << 2)) != 0)
#define HW_TABLE_WRITE_OK(p) (((p) & (1ULL << 1)) != 0)
#define HW_SPLIT_ROOTS     0
#define HW_BREAK_SENSITIVE 0ULL
#else
#error "select a machine personality"
#endif

void
MachineCreate(PMACHINE Machine, ULONG64 FrameCount, ULONG CpuCount)
{
    memset(Machine, 0, sizeof(*Machine));
    if (posix_memalign((void **)&Machine->Ram, PAGE_SIZE, (size_t)FrameCount * PAGE_SIZE) != 0)
        abort();
    memset(Machine->Ram, 0, (size_t)FrameCount * PAGE_SIZE);
    Machine->FrameCount = FrameCount;
    Machine->CpuCount = CpuCount;
    Machine->StrictTlb = TRUE;
    MachineCurrent = Machine;
}

void
MachineDestroy(PMACHINE Machine)
{
    free(Machine->Ram);
    if (MachineCurrent == Machine)
        MachineCurrent = NULL;
}

PUCHAR
MachineFrame(PMACHINE Machine, ULONG64 Frame)
{
    if (Frame >= Machine->FrameCount)
    {
        __sync_fetch_and_add(&Machine->BadFrameAccesses, 1);
        return Machine->Ram;
    }

    return Machine->Ram + Frame * PAGE_SIZE;
}

void
MachineSetUserRoot(PMACHINE Machine, ULONG Cpu, ULONG64 RootFrame)
{
    Machine->Cpu[Cpu].UserRoot = RootFrame;
    memset(Machine->Cpu[Cpu].Tlb, 0, sizeof(Machine->Cpu[Cpu].Tlb));
}

PVOID
MiArchMapFrame(_In_ ULONG64 Frame)
{
    return MachineFrame(MachineCurrent, Frame);
}

VOID
MiArchUnmapFrame(_In_ PVOID Mapping)
{
    UNREFERENCED_PARAMETER(Mapping);
}

MI_PTE
MiArchPteRead(_In_ PMI_PTE Slot)
{
    return __atomic_load_n(Slot, __ATOMIC_SEQ_CST);
}

BOOLEAN
MiArchPteCompareExchange(_Inout_ PMI_PTE Slot, _In_ MI_PTE Expected, _In_ MI_PTE Value)
{
    return __atomic_compare_exchange_n(Slot, &Expected, Value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static
void
MachineCpuAcquire(PMACHINE Machine, ULONG Cpu)
{
    unsigned Spins = 0;

    while (__sync_lock_test_and_set(&Machine->Cpu[Cpu].Busy, 1))
    {
        if (++Spins > 64)
        {
            sched_yield();
            Spins = 0;
        }
    }
}

static
void
MachineCpuRelease(PMACHINE Machine, ULONG Cpu)
{
    __sync_lock_release(&Machine->Cpu[Cpu].Busy);
}

VOID
MiArchTlbInvalidateAll(_In_ BOOLEAN AllProcessors)
{
    PMACHINE Machine = MachineCurrent;
    ULONG Cpu;

    if (Machine->TlbDisabled)
        return;

    for (Cpu = 0; Cpu < Machine->CpuCount; Cpu++)
    {
        ULONG i;

        if (!AllProcessors && Cpu != MachineCpu)
            continue;

        MachineCpuAcquire(Machine, Cpu);
        for (i = 0; i < MACHINE_TLB_ENTRIES; i++)
            Machine->Cpu[Cpu].Tlb[i].Valid = FALSE;
        MachineCpuRelease(Machine, Cpu);
    }

    __sync_fetch_and_add(&Machine->TlbInvalidations, 1);
}

VOID
MiArchTlbInvalidate(_In_ ULONG64 VirtualAddress, _In_ ULONG64 PageCount, _In_ BOOLEAN AllProcessors)
{
    PMACHINE Machine = MachineCurrent;
    ULONG64 First = VirtualAddress >> PAGE_SHIFT;
    ULONG Cpu;

    if (Machine->TlbDisabled)
        return;

    for (Cpu = 0; Cpu < Machine->CpuCount; Cpu++)
    {
        ULONG i;

        if (!AllProcessors && Cpu != MachineCpu)
            continue;

        MachineCpuAcquire(Machine, Cpu);
        for (i = 0; i < MACHINE_TLB_ENTRIES; i++)
        {
            MACHINE_TLB_ENTRY *Entry = &Machine->Cpu[Cpu].Tlb[i];

            if (Entry->Valid && Entry->Vpn >= First && Entry->Vpn < First + PageCount)
                Entry->Valid = FALSE;
        }
        MachineCpuRelease(Machine, Cpu);
    }

    __sync_fetch_and_add(&Machine->TlbInvalidations, 1);
}

VOID
MiArchPteWrite(_Inout_ PMI_PTE Slot, _In_ MI_PTE Value)
{
    PMACHINE Machine = MachineCurrent;
    MI_PTE Old = __atomic_load_n(Slot, __ATOMIC_SEQ_CST);
    BOOLEAN Broke = FALSE;

    if (MiArchPteNeedsBreak(Old, Value))
    {
        __atomic_store_n(Slot, 0, __ATOMIC_SEQ_CST);
        MiArchTlbInvalidateAll(TRUE);
        Broke = TRUE;
    }

    if (HW_VALID(Old) && HW_VALID(Value) && ((Old ^ Value) & HW_BREAK_SENSITIVE) != 0 && !Broke)
        __sync_fetch_and_add(&Machine->BreakBeforeMakeViolations, 1);

    __atomic_store_n(Slot, Value, __ATOMIC_SEQ_CST);
}

static
ULONG64
MachineRoot(PMACHINE Machine, ULONG Cpu, ULONG64 VirtualAddress)
{
    if (HW_SPLIT_ROOTS)
        return (VirtualAddress >> 63) ? Machine->SystemRoot : Machine->Cpu[Cpu].UserRoot;

    return Machine->Cpu[Cpu].UserRoot ? Machine->Cpu[Cpu].UserRoot : Machine->SystemRoot;
}

static
PMI_PTE
MachineWalk(PMACHINE Machine, ULONG64 Root, ULONG64 VirtualAddress, PBOOLEAN TableUser, PBOOLEAN TableWrite,
            PULONG LeafLevel)
{
    const MI_ARCH_DESCRIPTOR *Arch = MiArchDescribe();
    ULONG64 Frame = Root;
    LONG Level;

    *TableUser = TRUE;
    *TableWrite = TRUE;

    if (Root == 0)
        return NULL;

    for (Level = Arch->PagingLevels - 1; Level >= 0; Level--)
    {
        ULONG Index = (ULONG)((VirtualAddress >> Arch->Level[Level].Shift) & Arch->Level[Level].IndexMask);
        PMI_PTE Slot = (PMI_PTE)MachineFrame(Machine, Frame) + Index;
        MI_PTE Entry = __atomic_load_n(Slot, __ATOMIC_SEQ_CST);

        if (!HW_VALID(Entry))
            return (Level == 0) ? Slot : NULL;

        if (HW_IS_LEAF(Entry, Level))
        {
            *LeafLevel = (ULONG)Level;
            return Slot;
        }

        if (!HW_IS_TABLE(Entry, Level))
            return NULL;

        *TableUser = *TableUser && HW_TABLE_USER_OK(Entry);
        *TableWrite = *TableWrite && HW_TABLE_WRITE_OK(Entry);
        Frame = HW_FRAME(Entry);
    }

    return NULL;
}

BOOLEAN
MachineProbe(PMACHINE Machine, ULONG Cpu, ULONG64 VirtualAddress, PULONG64 Frame, PULONG64 LeafPte)
{
    BOOLEAN TableUser, TableWrite;
    ULONG LeafLevel = 0;
    PMI_PTE Slot = MachineWalk(Machine, MachineRoot(Machine, Cpu, VirtualAddress), VirtualAddress,
                               &TableUser, &TableWrite, &LeafLevel);
    MI_PTE Entry = Slot ? __atomic_load_n(Slot, __ATOMIC_SEQ_CST) : 0;

    if (LeafPte != NULL)
        *LeafPte = Entry;
    if (Frame != NULL)
        *Frame = HW_VALID(Entry) ? HW_FRAME(Entry) + (LeafLevel ? ((VirtualAddress >> PAGE_SHIFT) &
            ((1ULL << (MiArchDescribe()->Level[LeafLevel].Shift - PAGE_SHIFT)) - 1)) : 0) : 0;

    return Slot != NULL && HW_VALID(Entry);
}

static
BOOLEAN
MachineTranslate(PMACHINE Machine, ULONG Cpu, ULONG64 VirtualAddress, MACHINE_ACCESS Access, BOOLEAN UserMode,
                 PULONG64 Frame)
{
    const MI_ARCH_DESCRIPTOR *Arch = MiArchDescribe();
    ULONG64 Root = MachineRoot(Machine, Cpu, VirtualAddress);
    ULONG64 Vpn = VirtualAddress >> PAGE_SHIFT;
    MACHINE_TLB_ENTRY *Tlb = &Machine->Cpu[Cpu].Tlb[Vpn % MACHINE_TLB_ENTRIES];
    BOOLEAN TableUser, TableWrite;
    ULONG LeafLevel = 0;
    PMI_PTE Slot;
    MI_PTE Entry;

    if (!Machine->TlbDisabled && Tlb->Valid && Tlb->Vpn == Vpn && Tlb->Root == Root &&
        (!UserMode || Tlb->User) && (Access != MachineWrite || Tlb->Writable) &&
        (Access != MachineExecute || Tlb->Executable))
    {
        __sync_fetch_and_add(&Machine->TlbHits, 1);

        if (Machine->StrictTlb)
        {
            Slot = MachineWalk(Machine, Root, VirtualAddress, &TableUser, &TableWrite, &LeafLevel);
            Entry = Slot ? __atomic_load_n(Slot, __ATOMIC_SEQ_CST) : 0;

            ULONG64 CurrentFrame = HW_FRAME(Entry) + (LeafLevel ? (Vpn &
                ((1ULL << (Arch->Level[LeafLevel].Shift - PAGE_SHIFT)) - 1)) : 0);

            if (!HW_VALID(Entry) || CurrentFrame != Tlb->Frame ||
                (Access == MachineWrite && !HW_WRITE_OK(Entry)))
            {
                __sync_fetch_and_add(&Machine->StaleTlbUses, 1);
            }
        }

        *Frame = Tlb->Frame;
        return TRUE;
    }

    __sync_fetch_and_add(&Machine->TlbMisses, 1);

    for (;;)
    {
        MI_PTE Updated;

        Slot = MachineWalk(Machine, Root, VirtualAddress, &TableUser, &TableWrite, &LeafLevel);
        if (Slot == NULL)
            return FALSE;

        Entry = __atomic_load_n(Slot, __ATOMIC_SEQ_CST);
        if (!HW_VALID(Entry))
            return FALSE;

        if (UserMode && (!HW_USER(Entry) || !TableUser))
            return FALSE;
        if (Access == MachineExecute && !HW_EXEC_OK(Entry, UserMode))
            return FALSE;

        Updated = Entry;

        if (!HW_ACCESSED(Entry))
        {
            if (!Machine->HardwareAccessDirty)
                return FALSE;
            Updated = HW_SET_ACCESSED(Updated);
        }

        if (Access == MachineWrite)
        {
            if (!TableWrite)
                return FALSE;

            if (!HW_WRITE_OK(Entry))
            {
                if (!Machine->HardwareAccessDirty || !HW_CAN_AUTO_DIRTY(Entry))
                    return FALSE;
                Updated = HW_SET_DIRTY(Updated);
            }
#if defined(MACHINE_AMD64)
            Updated = HW_SET_DIRTY(Updated);
#endif
        }

#if defined(MACHINE_AMD64)
        Updated = HW_SET_ACCESSED(Updated);
#endif

        if (Updated == Entry ||
            __atomic_compare_exchange_n(Slot, &Entry, Updated, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
            Entry = Updated;
            break;
        }
    }

    *Frame = HW_FRAME(Entry) + (LeafLevel ? ((VirtualAddress >> PAGE_SHIFT) &
                                             ((1ULL << (Arch->Level[LeafLevel].Shift - PAGE_SHIFT)) - 1)) : 0);

    Tlb->Valid = FALSE;
    Tlb->Root = Root;
    Tlb->Vpn = Vpn;
    Tlb->Frame = *Frame;
    Tlb->User = HW_USER(Entry) && TableUser;
    Tlb->Writable = HW_WRITE_OK(Entry) && TableWrite;
    Tlb->Executable = HW_EXEC_OK(Entry, UserMode);
    Tlb->Valid = TRUE;
    return TRUE;
}

NTSTATUS
MachineAccessMemory(PMACHINE Machine, ULONG Cpu, ULONG64 VirtualAddress, PVOID Buffer, SIZE_T Length,
                    MACHINE_ACCESS Access, BOOLEAN UserMode)
{
    PUCHAR Cursor = Buffer;

    MachineCpu = Cpu;

    while (Length != 0)
    {
        SIZE_T Chunk = PAGE_SIZE - (SIZE_T)(VirtualAddress & (PAGE_SIZE - 1));
        ULONG64 Frame = 0;
        ULONG Attempts = 0;

        if (Chunk > Length)
            Chunk = Length;

        MachineCpuAcquire(Machine, Cpu);

        while (!MachineTranslate(Machine, Cpu, VirtualAddress, Access, UserMode, &Frame))
        {
            NTSTATUS Status;

            MachineCpuRelease(Machine, Cpu);
            __sync_fetch_and_add(&Machine->Faults, 1);
            if (Machine->Fault == NULL || ++Attempts > 16)
                return STATUS_ACCESS_VIOLATION;

            Status = Machine->Fault(Machine->FaultContext, Cpu, VirtualAddress, Access, UserMode);
            if (!NT_SUCCESS(Status) || Status == STATUS_GUARD_PAGE_VIOLATION)
                return Status;

            MachineCpuAcquire(Machine, Cpu);
        }

        if (Cursor != NULL)
        {
            PUCHAR Physical = MachineFrame(Machine, Frame) + (VirtualAddress & (PAGE_SIZE - 1));

            if (Access == MachineWrite)
                memcpy(Physical, Cursor, Chunk);
            else
                memcpy(Cursor, Physical, Chunk);

            Cursor += Chunk;
        }

        MachineCpuRelease(Machine, Cpu);
        VirtualAddress += Chunk;
        Length -= Chunk;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MachineTouch(PMACHINE Machine, ULONG Cpu, ULONG64 VirtualAddress, MACHINE_ACCESS Access, BOOLEAN UserMode)
{
    return MachineAccessMemory(Machine, Cpu, VirtualAddress, NULL, 1, Access, UserMode);
}
