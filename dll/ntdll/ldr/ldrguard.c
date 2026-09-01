/*
 * PROJECT:     ReactOS NT Layer DLL
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
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
    MEMORY_BASIC_INFORMATION MemoryInfo;
    ULONG Protection;
    NTSTATUS Status;

    Status = NtQueryVirtualMemory(NtCurrentProcess(), Target, MemoryBasicInformation, &MemoryInfo, sizeof(MemoryInfo), NULL);
    if (!NT_SUCCESS(Status) || MemoryInfo.State != MEM_COMMIT)
        return FALSE;

    Protection = MemoryInfo.Protect & 0xFF;
    return Protection == PAGE_EXECUTE ||
           Protection == PAGE_EXECUTE_READ ||
           Protection == PAGE_EXECUTE_READWRITE ||
           Protection == PAGE_EXECUTE_WRITECOPY;
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
        if (LoadConfig == NULL ||
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

NTSTATUS
NTAPI
LdrpInitializeGuard(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    UNREFERENCED_PARAMETER(LdrEntry);
    return STATUS_SUCCESS;
}

#endif
