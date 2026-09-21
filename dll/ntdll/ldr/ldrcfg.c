/*
 * PROJECT:     ReactOS NT Layer DLL
 * FILE:        dll/ntdll/ldr/ldrcfg.c
 * PURPOSE:     Dynamic Control Flow Guard target registration and validation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <ntdll.h>

#if defined(_M_ARM64)

static RTL_SRWLOCK LdrpGuardDynamicLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY LdrpGuardDynamicRanges = {&LdrpGuardDynamicRanges, &LdrpGuardDynamicRanges};
static volatile LONG LdrpGuardDynamicRangesPresent;

typedef struct _LDRP_GUARD_DYNAMIC_RANGE
{
    LIST_ENTRY ListEntry;
    ULONG_PTR Base;
    SIZE_T Size;
    SIZE_T SlotCount;
    UCHAR ValidSlots[1];
} LDRP_GUARD_DYNAMIC_RANGE, *PLDRP_GUARD_DYNAMIC_RANGE;

typedef struct _LDRP_GUARD_TARGET_INFO
{
    ULONG_PTR Offset;
    ULONG_PTR Flags;
} LDRP_GUARD_TARGET_INFO, *PLDRP_GUARD_TARGET_INFO;

NTSTATUS
NTAPI
RtlRegisterCfgTargetRange(
    _In_ PVOID Base,
    _In_ SIZE_T Size)
{
    PLDRP_GUARD_DYNAMIC_RANGE Range;
    SIZE_T AllocationSize, SlotCount;

    if (Base == NULL || Size == 0 || Size > MAXULONG_PTR - 15)
        return STATUS_INVALID_PARAMETER;
    SlotCount = (Size + 15) / 16;
    if (SlotCount > (MAXULONG_PTR - FIELD_OFFSET(LDRP_GUARD_DYNAMIC_RANGE, ValidSlots)) * 8)
        return STATUS_INTEGER_OVERFLOW;
    AllocationSize = FIELD_OFFSET(LDRP_GUARD_DYNAMIC_RANGE, ValidSlots) + (SlotCount + 7) / 8;
    Range = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, AllocationSize);
    if (Range == NULL)
        return STATUS_NO_MEMORY;
    Range->Base = (ULONG_PTR)Base;
    Range->Size = Size;
    Range->SlotCount = SlotCount;
    RtlAcquireSRWLockExclusive(&LdrpGuardDynamicLock);
    InsertTailList(&LdrpGuardDynamicRanges, &Range->ListEntry);
    InterlockedExchange(&LdrpGuardDynamicRangesPresent, 1);
    RtlReleaseSRWLockExclusive(&LdrpGuardDynamicLock);
    return STATUS_SUCCESS;
}

VOID
NTAPI
RtlUnregisterCfgTargetRange(
    _In_ PVOID Base)
{
    PLIST_ENTRY Entry;

    if (InterlockedCompareExchange(&LdrpGuardDynamicRangesPresent, 0, 0) == 0)
        return;

    RtlAcquireSRWLockExclusive(&LdrpGuardDynamicLock);
    for (Entry = LdrpGuardDynamicRanges.Flink; Entry != &LdrpGuardDynamicRanges; Entry = Entry->Flink)
    {
        PLDRP_GUARD_DYNAMIC_RANGE Range = CONTAINING_RECORD(Entry, LDRP_GUARD_DYNAMIC_RANGE, ListEntry);

        if (Range->Base == (ULONG_PTR)Base)
        {
            RemoveEntryList(Entry);
            if (IsListEmpty(&LdrpGuardDynamicRanges))
                InterlockedExchange(&LdrpGuardDynamicRangesPresent, 0);
            RtlReleaseSRWLockExclusive(&LdrpGuardDynamicLock);
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, Range);
            return;
        }
    }
    RtlReleaseSRWLockExclusive(&LdrpGuardDynamicLock);
}

NTSTATUS
NTAPI
RtlSetCfgTargetValidity(
    _In_ PVOID Base,
    _In_ SIZE_T Size,
    _In_ ULONG Count,
    _Inout_updates_(Count) PLDRP_GUARD_TARGET_INFO Targets)
{
    PLDRP_GUARD_DYNAMIC_RANGE Range = NULL;
    PLIST_ENTRY Entry;
    ULONG Index;
    ULONG_PTR PreviousOffset = 0;

    if (Base == NULL || Size == 0 || Count == 0 || Targets == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlAcquireSRWLockExclusive(&LdrpGuardDynamicLock);
    for (Entry = LdrpGuardDynamicRanges.Flink; Entry != &LdrpGuardDynamicRanges; Entry = Entry->Flink)
    {
        Range = CONTAINING_RECORD(Entry, LDRP_GUARD_DYNAMIC_RANGE, ListEntry);
        if (Range->Base == (ULONG_PTR)Base && Size <= Range->Size)
            break;
        Range = NULL;
    }
    if (Range == NULL)
    {
        RtlReleaseSRWLockExclusive(&LdrpGuardDynamicLock);
        return STATUS_INVALID_ADDRESS;
    }
    for (Index = 0; Index < Count; ++Index)
    {
        SIZE_T Slot;

        if ((Targets[Index].Offset & 15) != 0 || Targets[Index].Offset >= Size ||
            (Index != 0 && Targets[Index].Offset <= PreviousOffset) || (Targets[Index].Flags & ~1) != 0)
        {
            RtlReleaseSRWLockExclusive(&LdrpGuardDynamicLock);
            return STATUS_INVALID_PARAMETER;
        }
        PreviousOffset = Targets[Index].Offset;
        Slot = Targets[Index].Offset / 16;
        if (Targets[Index].Flags & 1)
            Range->ValidSlots[Slot / 8] |= (UCHAR)(1u << (Slot & 7));
        else
            Range->ValidSlots[Slot / 8] &= (UCHAR)~(1u << (Slot & 7));
        Targets[Index].Flags |= 2;
    }
    RtlReleaseSRWLockExclusive(&LdrpGuardDynamicLock);
    return STATUS_SUCCESS;
}

BOOLEAN
LdrpGuardDynamicTargetIsExecutable(
    _In_ PVOID Target)
{
    PLIST_ENTRY Entry;
    MEMORY_BASIC_INFORMATION MemoryInfo;
    ULONG Protection;
    NTSTATUS Status;
    BOOLEAN Valid = TRUE;

    Status = NtQueryVirtualMemory(NtCurrentProcess(), Target, MemoryBasicInformation, &MemoryInfo, sizeof(MemoryInfo), NULL);
    if (!NT_SUCCESS(Status) || MemoryInfo.State != MEM_COMMIT)
        return FALSE;

    Protection = MemoryInfo.Protect & 0xFF;
    if (Protection != PAGE_EXECUTE && Protection != PAGE_EXECUTE_READ &&
        Protection != PAGE_EXECUTE_READWRITE && Protection != PAGE_EXECUTE_WRITECOPY)
    {
        return FALSE;
    }

    RtlAcquireSRWLockShared(&LdrpGuardDynamicLock);
    for (Entry = LdrpGuardDynamicRanges.Flink; Entry != &LdrpGuardDynamicRanges; Entry = Entry->Flink)
    {
        PLDRP_GUARD_DYNAMIC_RANGE Range = CONTAINING_RECORD(Entry, LDRP_GUARD_DYNAMIC_RANGE, ListEntry);

        if ((ULONG_PTR)Target >= Range->Base && (ULONG_PTR)Target - Range->Base < Range->Size)
        {
            SIZE_T Slot = ((ULONG_PTR)Target - Range->Base) / 16;
            Valid = (Range->ValidSlots[Slot / 8] & (1u << (Slot & 7))) != 0;
            break;
        }
    }
    RtlReleaseSRWLockShared(&LdrpGuardDynamicLock);
    return Valid;
}


#endif
