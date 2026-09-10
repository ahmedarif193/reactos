/*
 * PROJECT:     ReactOS NT Layer DLL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PE Control Flow Guard loader support
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)

VOID NTAPI LdrpGuardCheckIcall(VOID);
VOID NTAPI LdrpGuardDispatchIcall(VOID);
extern PLDR_DATA_TABLE_ENTRY LdrpImageEntry;

static BOOLEAN LdrpGuardEnabled;
static RTL_SRWLOCK LdrpGuardDynamicLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY LdrpGuardDynamicRanges = {&LdrpGuardDynamicRanges, &LdrpGuardDynamicRanges};

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
    RtlReleaseSRWLockExclusive(&LdrpGuardDynamicLock);
    return STATUS_SUCCESS;
}

VOID
NTAPI
RtlUnregisterCfgTargetRange(
    _In_ PVOID Base)
{
    PLIST_ENTRY Entry;

    RtlAcquireSRWLockExclusive(&LdrpGuardDynamicLock);
    for (Entry = LdrpGuardDynamicRanges.Flink; Entry != &LdrpGuardDynamicRanges; Entry = Entry->Flink)
    {
        PLDRP_GUARD_DYNAMIC_RANGE Range = CONTAINING_RECORD(Entry, LDRP_GUARD_DYNAMIC_RANGE, ListEntry);

        if (Range->Base == (ULONG_PTR)Base)
        {
            RemoveEntryList(Entry);
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

static BOOLEAN
LdrpGuardAddressInImage(
    _In_ ULONG_PTR Address,
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ SIZE_T Size)
{
    ULONG_PTR Base = (ULONG_PTR)LdrEntry->DllBase;

    return Address >= Base &&
           Address - Base <= LdrEntry->SizeOfImage &&
           Size <= LdrEntry->SizeOfImage - (Address - Base);
}

static BOOLEAN
LdrpGuardTargetIsExecutable(
    _In_ ULONG_PTR Target,
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PIMAGE_NT_HEADERS NtHeaders)
{
    PIMAGE_SECTION_HEADER Section;
    ULONG Index;
    ULONG_PTR RelativeTarget;

    RelativeTarget = Target - (ULONG_PTR)LdrEntry->DllBase;
    Section = IMAGE_FIRST_SECTION(NtHeaders);
    for (Index = 0; Index < NtHeaders->FileHeader.NumberOfSections; ++Index, ++Section)
    {
        ULONG SectionSize = max(Section->Misc.VirtualSize, Section->SizeOfRawData);

        if (RelativeTarget >= Section->VirtualAddress &&
            RelativeTarget - Section->VirtualAddress < SectionSize)
        {
            return (Section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        }
    }

    return FALSE;
}

static BOOLEAN
LdrpGuardTableContainsTarget(
    _In_ ULONG_PTR Target,
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PIMAGE_LOAD_CONFIG_DIRECTORY LoadConfig)
{
    const BYTE *Table;
    ULONGLONG Count;
    SIZE_T Stride;
    ULONGLONG Low, High;
    ULONG TargetRva;

    Count = LoadConfig->GuardCFFunctionCount;
    Stride = sizeof(ULONG) +
             ((LoadConfig->GuardFlags & IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK) >>
              IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_SHIFT);
    if (Stride < sizeof(ULONG) ||
        Count == 0 ||
        Count > MAXULONG_PTR / Stride ||
        !LdrpGuardAddressInImage((ULONG_PTR)LoadConfig->GuardCFFunctionTable,
                                LdrEntry, (SIZE_T)Count * Stride))
    {
        return FALSE;
    }

    TargetRva = (ULONG)(Target - (ULONG_PTR)LdrEntry->DllBase);
    Table = (const BYTE *)(ULONG_PTR)LoadConfig->GuardCFFunctionTable;
    Low = 0;
    High = Count;
    while (Low < High)
    {
        ULONGLONG Middle = Low + (High - Low) / 2;
        const BYTE *Entry = Table + (SIZE_T)Middle * Stride;
        ULONG EntryRva;

        RtlCopyMemory(&EntryRva, Entry, sizeof(EntryRva));
        if (EntryRva < TargetRva)
        {
            Low = Middle + 1;
        }
        else if (EntryRva > TargetRva)
        {
            High = Middle;
        }
        else
        {
            if (Stride > sizeof(ULONG) &&
                (Entry[sizeof(ULONG)] & IMAGE_GUARD_FLAG_FID_SUPPRESSED))
            {
                return FALSE;
            }
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
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

BOOLEAN
NTAPI
LdrpValidateUserCallTarget(
    _In_ PVOID Target)
{
    PLIST_ENTRY ListHead, NextEntry;
    PPEB Peb;

    if (((ULONG_PTR)Target & (sizeof(ULONG) - 1)) != 0)
        return FALSE;

    Peb = NtCurrentPeb();
    if (Peb->Ldr == NULL)
        return FALSE;

    ListHead = &Peb->Ldr->InLoadOrderModuleList;
    for (NextEntry = ListHead->Flink; NextEntry != ListHead; NextEntry = NextEntry->Flink)
    {
        PIMAGE_LOAD_CONFIG_DIRECTORY LoadConfig;
        PLDR_DATA_TABLE_ENTRY LdrEntry;
        PIMAGE_NT_HEADERS NtHeaders;
        ULONG ConfigSize;

        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (!LdrpGuardAddressInImage((ULONG_PTR)Target, LdrEntry, 1))
            continue;

        NtHeaders = RtlImageNtHeader(LdrEntry->DllBase);
        if (NtHeaders == NULL || !LdrpGuardTargetIsExecutable((ULONG_PTR)Target, LdrEntry, NtHeaders))
            return FALSE;

        LoadConfig = RtlImageDirectoryEntryToData(LdrEntry->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &ConfigSize);
        /* Instrumentation alone does not opt an image into CFG enforcement. */
        if (!(NtHeaders->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) ||
            LoadConfig == NULL ||
            ConfigSize < RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, GuardFlags) ||
            !(LoadConfig->GuardFlags & IMAGE_GUARD_CF_INSTRUMENTED))
        {
            return TRUE;
        }

        if (!(LoadConfig->GuardFlags & IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT))
            return FALSE;
        return LdrpGuardTableContainsTarget((ULONG_PTR)Target, LdrEntry, LoadConfig);
    }

    return LdrpGuardDynamicTargetIsExecutable(Target);
}

NTSTATUS
NTAPI
LdrpInitializeGuard(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PIMAGE_LOAD_CONFIG_DIRECTORY LoadConfig;
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID *CheckSlot, *DispatchSlot;
    PVOID ProtectBase;
    SIZE_T ProtectSize;
    ULONG ConfigSize;
    ULONG OldProtection, IgnoredProtection;
    NTSTATUS Status;
    BOOLEAN GuardImage;

    NtHeaders = RtlImageNtHeader(LdrEntry->DllBase);
    LoadConfig = RtlImageDirectoryEntryToData(LdrEntry->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &ConfigSize);
    GuardImage = NtHeaders != NULL &&
                 (NtHeaders->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) != 0 &&
                 LoadConfig != NULL &&
                 ConfigSize >= RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, GuardFlags) &&
                 (LoadConfig->GuardFlags & IMAGE_GUARD_CF_INSTRUMENTED) != 0;
    if (LdrEntry == LdrpImageEntry)
        LdrpGuardEnabled = GuardImage;

    /* Native ARM64 leaves image-local thunks intact in a non-CFG process. */
    if (!LdrpGuardEnabled || !GuardImage)
        return STATUS_SUCCESS;

    if (ConfigSize < RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, GuardCFDispatchFunctionPointer) ||
        !(LoadConfig->GuardFlags & IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT) ||
        LoadConfig->GuardCFCheckFunctionPointer == 0 ||
        LoadConfig->GuardCFDispatchFunctionPointer == 0)
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    CheckSlot = (PVOID *)(ULONG_PTR)LoadConfig->GuardCFCheckFunctionPointer;
    DispatchSlot = (PVOID *)(ULONG_PTR)LoadConfig->GuardCFDispatchFunctionPointer;
    if (!LdrpGuardAddressInImage((ULONG_PTR)CheckSlot, LdrEntry, sizeof(*CheckSlot)) ||
        !LdrpGuardAddressInImage((ULONG_PTR)DispatchSlot, LdrEntry, sizeof(*DispatchSlot)))
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    ProtectBase = CheckSlot < DispatchSlot ? (PVOID)CheckSlot : (PVOID)DispatchSlot;
    ProtectSize = ((ULONG_PTR)(CheckSlot < DispatchSlot ? DispatchSlot : CheckSlot) + sizeof(PVOID)) - (ULONG_PTR)ProtectBase;
    Status = NtProtectVirtualMemory(NtCurrentProcess(), &ProtectBase, &ProtectSize, PAGE_READWRITE, &OldProtection);
    if (!NT_SUCCESS(Status))
        return Status;

    InterlockedExchangePointer((PVOID volatile *)CheckSlot, LdrpGuardCheckIcall);
    InterlockedExchangePointer((PVOID volatile *)DispatchSlot, LdrpGuardDispatchIcall);

    Status = NtProtectVirtualMemory(NtCurrentProcess(), &ProtectBase, &ProtectSize, OldProtection, &IgnoredProtection);
    return Status;
}

#else

NTSTATUS NTAPI RtlRegisterCfgTargetRange(_In_ PVOID Base, _In_ SIZE_T Size)
{
    UNREFERENCED_PARAMETER(Base);
    UNREFERENCED_PARAMETER(Size);
    return STATUS_NOT_SUPPORTED;
}

VOID NTAPI RtlUnregisterCfgTargetRange(_In_ PVOID Base)
{
    UNREFERENCED_PARAMETER(Base);
}

NTSTATUS NTAPI RtlSetCfgTargetValidity(_In_ PVOID Base, _In_ SIZE_T Size, _In_ ULONG Count, _Inout_updates_(Count) PVOID Targets)
{
    UNREFERENCED_PARAMETER(Base);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Targets);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
LdrpInitializeGuard(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    UNREFERENCED_PARAMETER(LdrEntry);
    return STATUS_SUCCESS;
}

#endif
