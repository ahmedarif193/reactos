/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Native RISC-V64 image lookup and virtual unwind
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>
#include "unwind_private.h"

/* Function tables registered with RtlAddFunctionTable and
 * RtlInstallFunctionTableCallback (dynfntbl.c). */
PRUNTIME_FUNCTION
NTAPI
RtlpLookupDynamicFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _In_ PUNWIND_HISTORY_TABLE HistoryTable);

static NTSTATUS
NTAPI
RtlpRiscv64ReadLocalMemory(
    _In_opt_ PVOID ReadContext,
    _In_ ULONG64 Address,
    _Out_writes_bytes_all_(Size) PVOID Buffer,
    _In_ SIZE_T Size)
{
    UNREFERENCED_PARAMETER(ReadContext);
    return RtlpRiscv64ReadMemory(Buffer, (PVOID)(ULONG_PTR)Address, Size);
}

static PIMAGE_SECTION_HEADER
RtlpRiscv64FindImageSection(
    _In_ PIMAGE_NT_HEADERS NtHeaders,
    _In_ ULONG Rva,
    _In_ ULONG Size)
{
    PIMAGE_SECTION_HEADER Section;
    ULONG Index, SectionSize;
    ULONG64 RangeEnd, SectionEnd;

    RangeEnd = (ULONG64)Rva + Size;
    if ((RangeEnd > NtHeaders->OptionalHeader.SizeOfImage) ||
        (RangeEnd < Rva))
    {
        return NULL;
    }

    Section = IMAGE_FIRST_SECTION(NtHeaders);
    for (Index = 0; Index < NtHeaders->FileHeader.NumberOfSections;
         Index++, Section++)
    {
        SectionSize = max(Section->Misc.VirtualSize, Section->SizeOfRawData);
        SectionEnd = (ULONG64)Section->VirtualAddress + SectionSize;
        if ((Rva >= Section->VirtualAddress) &&
            (RangeEnd <= SectionEnd))
        {
            return Section;
        }
    }

    return NULL;
}

static BOOLEAN
RtlpRiscv64ValidateImageRange(
    _In_ PIMAGE_NT_HEADERS NtHeaders,
    _In_ ULONG Rva,
    _In_ ULONG Size,
    _In_ ULONG RequiredCharacteristics,
    _In_ ULONG ForbiddenCharacteristics)
{
    PIMAGE_SECTION_HEADER Section;

    Section = RtlpRiscv64FindImageSection(NtHeaders, Rva, Size);
    if (Section == NULL)
        return FALSE;

    return ((Section->Characteristics & RequiredCharacteristics) ==
            RequiredCharacteristics) &&
           ((Section->Characteristics & ForbiddenCharacteristics) == 0);
}

NTSTATUS NTAPI
RtlpRiscv64PrimaryEntry(ULONG64 ImageBase, PRUNTIME_FUNCTION *Entry)
{
    RUNTIME_FUNCTION Function;
    RISCV64_UNWIND_INFO_V1 Header;
    ULONG Depth;
    /* Called only after RtlVirtualUnwind2 has validated the complete chain.
     * Recheck every read, offset and depth before exposing the primary entry. */
    for (Depth = 0; *Entry && Depth <= RTL_RISCV64_MAX_CHAIN_DEPTH; Depth++) {
        if (!NT_SUCCESS(RtlpRiscv64ReadMemory(&Function, *Entry, sizeof(Function))) ||
            !NT_SUCCESS(RtlpRiscv64ReadMemory(&Header,
                (PVOID)(ImageBase + Function.UnwindData), sizeof(Header))) ||
            Header.Magic != RISCV64_UNWIND_MAGIC ||
            Header.Version != RISCV64_UNWIND_VERSION ||
            Header.RecordSize < sizeof(Header) ||
            Header.RecordSize > RTL_RISCV64_MAX_RVUW_RECORD_SIZE)
            return STATUS_BAD_FUNCTION_TABLE;
        if (!(Header.Flags & RISCV64_UNW_FLAG_CHAININFO)) return STATUS_SUCCESS;
        if (Header.RecordSize < sizeof(Header) + sizeof(Function))
            return STATUS_BAD_FUNCTION_TABLE;
        *Entry = (PVOID)(ImageBase + Function.UnwindData + Header.RecordSize - sizeof(Function));
    }
    return *Entry ? STATUS_BAD_FUNCTION_TABLE : STATUS_SUCCESS;
}

NTSTATUS NTAPI
RtlpRiscv64ReadScope(PDISPATCHER_CONTEXT Dispatcher, ULONG Index,
                    PULONG Count, RISCV64_SCOPE_RECORD *Scope)
{
    PIMAGE_NT_HEADERS Nt;
    ULONG64 Rva;
    ULONG Size;
    NTSTATUS Status;

    /* Dispatch reaches here only for an image entry or a registered dynamic
     * one. Dynamic code has no sections: its records are checked by shape and
     * read through the checked reader only. */
    Nt = RtlImageNtHeader((PVOID)Dispatcher->ImageBase);
#define SCOPE_RANGE_VALID(Rva, Size, Required, Forbidden) \
    (!Nt || RtlpRiscv64ValidateImageRange(Nt, (Rva), (Size), (Required), (Forbidden)))
    Rva = (ULONG64)Dispatcher->HandlerData - Dispatcher->ImageBase;
    if (Rva > MAXULONG || (Rva & 3) ||
        !SCOPE_RANGE_VALID((ULONG)Rva, sizeof(ULONG),
            IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))
        return STATUS_BAD_FUNCTION_TABLE;
    Status = RtlpRiscv64ReadMemory(Count, Dispatcher->HandlerData, sizeof(*Count));
    if (!NT_SUCCESS(Status) || *Count > 256) return STATUS_BAD_FUNCTION_TABLE;
    Size = sizeof(ULONG) + *Count * sizeof(*Scope);
    if (!SCOPE_RANGE_VALID((ULONG)Rva, Size,
            IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))
        return STATUS_BAD_FUNCTION_TABLE;
    if (!Scope) return STATUS_SUCCESS;
    if (Index >= *Count) return STATUS_BAD_FUNCTION_TABLE;
    Status = RtlpRiscv64ReadMemory(Scope,
        (PUCHAR)Dispatcher->HandlerData + sizeof(ULONG) + Index * sizeof(*Scope),
        sizeof(*Scope));
    if (!NT_SUCCESS(Status) || Scope->BeginAddress >= Scope->EndAddress ||
        ((Scope->BeginAddress | Scope->EndAddress | Scope->JumpTarget) & 1) ||
        !SCOPE_RANGE_VALID(Scope->BeginAddress,
            Scope->EndAddress - Scope->BeginAddress,
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_WRITE) ||
        (Scope->HandlerAddress != 1 &&
         ((Scope->HandlerAddress & 1) || !Scope->HandlerAddress ||
          !SCOPE_RANGE_VALID(Scope->HandlerAddress, 2,
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_WRITE))) ||
        (!Scope->JumpTarget && Scope->HandlerAddress == 1) ||
        (Scope->JumpTarget && !SCOPE_RANGE_VALID(
            Scope->JumpTarget, 2, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE,
            IMAGE_SCN_MEM_WRITE)))
        return STATUS_BAD_FUNCTION_TABLE;
#undef SCOPE_RANGE_VALID
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpRiscv64ValidateRecordImageRanges(
    _In_ const RTL_RISCV64_UNWIND_VIEW *View,
    _In_ PIMAGE_NT_HEADERS NtHeaders,
    _In_ const RUNTIME_FUNCTION *FunctionEntry,
    _Inout_updates_(RTL_RISCV64_MAX_CHAIN_DEPTH + 1)
        RUNTIME_FUNCTION *SeenEntries,
    _In_ ULONG Depth)
{
    RTL_RISCV64_RECORD_LAYOUT Layout;
    RISCV64_UNWIND_HANDLER_V1 Handler;
    RUNTIME_FUNCTION ParentEntry;
    ULONG Index;
    NTSTATUS Status;

    if (Depth > RTL_RISCV64_MAX_CHAIN_DEPTH)
        return STATUS_BAD_FUNCTION_TABLE;

    for (Index = 0; Index < Depth; Index++)
    {
        if (RtlpRiscv64SameFunctionEntry(FunctionEntry,
                                         &SeenEntries[Index]))
        {
            return STATUS_BAD_FUNCTION_TABLE;
        }
    }
    SeenEntries[Depth] = *FunctionEntry;

    Status = RtlpRiscv64ReadRecordLayout(View, FunctionEntry, &Layout);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!RtlpRiscv64ValidateImageRange(
            NtHeaders,
            FunctionEntry->BeginAddress,
            FunctionEntry->EndAddress - FunctionEntry->BeginAddress,
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE,
            IMAGE_SCN_MEM_WRITE) ||
        !RtlpRiscv64ValidateImageRange(
            NtHeaders,
            FunctionEntry->UnwindData,
            Layout.Header.RecordSize,
            IMAGE_SCN_MEM_READ,
            IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    if (Layout.Header.Flags & RISCV64_UNW_FLAG_CHAININFO)
    {
        Status = RtlpRiscv64ReadImage(View,
                                      Layout.TailRva,
                                      &ParentEntry,
                                      sizeof(ParentEntry));
        if (!NT_SUCCESS(Status))
            return Status;

        return RtlpRiscv64ValidateRecordImageRanges(View,
                                                     NtHeaders,
                                                     &ParentEntry,
                                                     SeenEntries,
                                                     Depth + 1);
    }

    if (Layout.Header.Flags &
        (RISCV64_UNW_FLAG_EHANDLER | RISCV64_UNW_FLAG_UHANDLER))
    {
        Status = RtlpRiscv64ReadImage(View,
                                      Layout.TailRva,
                                      &Handler,
                                      sizeof(Handler));
        if (!NT_SUCCESS(Status))
            return Status;

        if (!RtlpRiscv64ValidateImageRange(
                NtHeaders,
                Handler.ExceptionHandlerRva,
                1,
                IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE,
                IMAGE_SCN_MEM_WRITE) ||
            ((Handler.HandlerDataRva != 0) &&
             !RtlpRiscv64ValidateImageRange(
                 NtHeaders,
                 Handler.HandlerDataRva,
                 sizeof(ULONG),
                 IMAGE_SCN_MEM_READ,
                 IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE)))
        {
            return STATUS_BAD_FUNCTION_TABLE;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpRiscv64GetStaticFunctionTable(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Out_ PRUNTIME_FUNCTION *FunctionTable,
    _Out_ PULONG EntryCount,
    _Out_opt_ PIMAGE_NT_HEADERS *ReturnedNtHeaders)
{
    PIMAGE_DATA_DIRECTORY Directory;
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID BaseAddress;
    ULONG64 ImageEnd;

    *ImageBase = 0;
    *FunctionTable = NULL;
    *EntryCount = 0;
    if (ReturnedNtHeaders != NULL)
        *ReturnedNtHeaders = NULL;

    if (!RtlPcToFileHeader((PVOID)(ULONG_PTR)ControlPc, &BaseAddress))
        return STATUS_NOT_FOUND;

    NtHeaders = RtlImageNtHeader(BaseAddress);
    if ((NtHeaders == NULL) ||
        (NtHeaders->OptionalHeader.NumberOfRvaAndSizes <=
         IMAGE_DIRECTORY_ENTRY_EXCEPTION) ||
        !RtlpRiscv64AddUnsigned((ULONG64)(ULONG_PTR)BaseAddress,
                                NtHeaders->OptionalHeader.SizeOfImage,
                                &ImageEnd) ||
        (ControlPc < (ULONG64)(ULONG_PTR)BaseAddress) ||
        (ControlPc >= ImageEnd))
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    *ImageBase = (ULONG64)(ULONG_PTR)BaseAddress;
    if (ReturnedNtHeaders != NULL)
        *ReturnedNtHeaders = NtHeaders;

    Directory = &NtHeaders->OptionalHeader.DataDirectory
                    [IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if ((Directory->VirtualAddress == 0) && (Directory->Size == 0))
        return STATUS_NOT_FOUND;
    if ((Directory->VirtualAddress == 0) ||
        (Directory->Size == 0) ||
        (Directory->Size % sizeof(RUNTIME_FUNCTION)) ||
        !RtlpRiscv64ValidateImageRange(
            NtHeaders,
            Directory->VirtualAddress,
            Directory->Size,
            IMAGE_SCN_MEM_READ,
            IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))
    {
        return STATUS_BAD_FUNCTION_TABLE;
    }

    *FunctionTable = (PRUNTIME_FUNCTION)(ULONG_PTR)
                     (*ImageBase + Directory->VirtualAddress);
    *EntryCount = Directory->Size / sizeof(RUNTIME_FUNCTION);
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlpRiscv64ValidateFunctionTable(
    _In_ ULONG64 ImageBase,
    _In_ PIMAGE_NT_HEADERS NtHeaders,
    _In_reads_(EntryCount) PRUNTIME_FUNCTION FunctionTable,
    _In_ ULONG EntryCount)
{
    RUNTIME_FUNCTION Entry;
    ULONG Index, PreviousEnd = 0;
    NTSTATUS Status;

    for (Index = 0; Index < EntryCount; Index++)
    {
        Status = RtlpRiscv64ReadMemory(&Entry,
                                    &FunctionTable[Index],
                                    sizeof(Entry));
        if (!NT_SUCCESS(Status))
            return STATUS_BAD_FUNCTION_TABLE;

        if (((Entry.BeginAddress | Entry.EndAddress) & 1) ||
            (Entry.BeginAddress >= Entry.EndAddress) ||
            (Entry.EndAddress > NtHeaders->OptionalHeader.SizeOfImage) ||
            (Index && (Entry.BeginAddress < PreviousEnd)) ||
            (Entry.UnwindData & 3) ||
            !RtlpRiscv64ValidateImageRange(
                NtHeaders,
                Entry.BeginAddress,
                Entry.EndAddress - Entry.BeginAddress,
                IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE,
                IMAGE_SCN_MEM_WRITE) ||
            !RtlpRiscv64ValidateImageRange(
                NtHeaders,
                Entry.UnwindData,
                sizeof(RISCV64_UNWIND_INFO_V1),
                IMAGE_SCN_MEM_READ,
                IMAGE_SCN_MEM_WRITE))
        {
            UNREFERENCED_PARAMETER(ImageBase);
            return STATUS_BAD_FUNCTION_TABLE;
        }

        PreviousEnd = Entry.EndAddress;
    }

    return STATUS_SUCCESS;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Out_ PULONG Length)
{
    PRUNTIME_FUNCTION FunctionTable;
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG64 Base;
    ULONG EntryCount;
    NTSTATUS Status;

    *ImageBase = 0;
    *Length = 0;
    Status = RtlpRiscv64GetStaticFunctionTable(ControlPc,
                                                &Base,
                                                &FunctionTable,
                                                &EntryCount,
                                                &NtHeaders);
    if (!NT_SUCCESS(Status))
        return NULL;

    Status = RtlpRiscv64ValidateFunctionTable(Base,
                                              NtHeaders,
                                              FunctionTable,
                                              EntryCount);
    if (!NT_SUCCESS(Status))
        return NULL;

    *ImageBase = (ULONG_PTR)Base;
    *Length = EntryCount * sizeof(RUNTIME_FUNCTION);
    return FunctionTable;
}

NTSTATUS
NTAPI
RtlpRiscv64LookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Out_ PRUNTIME_FUNCTION *FunctionEntry)
{
    PRUNTIME_FUNCTION FunctionTable;
    RUNTIME_FUNCTION Entry;
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG64 ControlRva;
    ULONG EntryCount, Low, High, Middle;
    ULONG LowerRva, UpperRva;
    NTSTATUS Status;

    *FunctionEntry = NULL;
    *ImageBase = 0;
    Status = RtlpRiscv64GetStaticFunctionTable(ControlPc,
                                                ImageBase,
                                                &FunctionTable,
                                                &EntryCount,
                                                &NtHeaders);
    if ((Status == STATUS_NOT_FOUND) && (*ImageBase == 0))
    {
        /* Code outside every loaded image, such as JIT output, is described
         * by the dynamic function tables. */
        *FunctionEntry = RtlpLookupDynamicFunctionEntry(ControlPc, ImageBase, NULL);
        return *FunctionEntry ? STATUS_SUCCESS : STATUS_NOT_FOUND;
    }
    if (!NT_SUCCESS(Status))
        return Status;

    ControlRva = ControlPc - *ImageBase;
    Low = 0;
    High = EntryCount;
    LowerRva = 0;
    UpperRva = NtHeaders->OptionalHeader.SizeOfImage;
    /* A lookup must be logarithmic: registry lock tracing performs several
     * walks per operation. Validate each visited interval against both its
     * image sections and the enclosing search interval. The unwinder then
     * validates the selected record and its complete chain before use.
     * RtlLookupFunctionTable still validates a whole table before exposing it. */
    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        Status = RtlpRiscv64ReadMemory(&Entry,
                                    &FunctionTable[Middle],
                                    sizeof(Entry));
        if (!NT_SUCCESS(Status))
        {
            *ImageBase = 0;
            return STATUS_BAD_FUNCTION_TABLE;
        }

        Status = RtlpRiscv64ValidateFunctionTable(*ImageBase, NtHeaders, &Entry, 1);
        if (!NT_SUCCESS(Status) || Entry.BeginAddress < LowerRva ||
            Entry.EndAddress > UpperRva)
        {
            *ImageBase = 0;
            return STATUS_BAD_FUNCTION_TABLE;
        }

        if (ControlRva < Entry.BeginAddress)
        {
            UpperRva = Entry.BeginAddress;
            High = Middle;
        }
        else if (ControlRva >= Entry.EndAddress)
        {
            LowerRva = Entry.EndAddress;
            Low = Middle + 1;
        }
        else
        {
            *FunctionEntry = &FunctionTable[Middle];
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable)
{
    PRUNTIME_FUNCTION Entry;
    UNREFERENCED_PARAMETER(HistoryTable);
    if (!NT_SUCCESS(RtlpRiscv64LookupFunctionEntry(ControlPc, ImageBase, &Entry)))
        return NULL;
    return Entry;
}

/* A function entry outside every image must be the one a dynamic function
 * table registers for ControlPc, relative to the same base. */
static
BOOLEAN
RtlpRiscv64IsDynamicFunctionEntry(
    _In_ ULONG_PTR ImageBase,
    _In_ ULONG_PTR ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry)
{
    PRUNTIME_FUNCTION Registered;
    DWORD64 RegisteredBase = 0;

    Registered = RtlpLookupDynamicFunctionEntry(ControlPc, &RegisteredBase, NULL);
    return (Registered != NULL) && (RegisteredBase == ImageBase) &&
           (Registered->BeginAddress == FunctionEntry->BeginAddress) &&
           (Registered->EndAddress == FunctionEntry->EndAddress) &&
           (Registered->UnwindData == FunctionEntry->UnwindData);
}

NTSTATUS
NTAPI
RtlVirtualUnwind2(
    _In_ ULONG HandlerType,
    _In_ ULONG_PTR ImageBase,
    _In_ ULONG_PTR ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_opt_ PBOOLEAN MachineFrameUnwound,
    _Out_opt_ PVOID *HandlerData,
    _Out_opt_ PULONG_PTR EstablisherFrame,
    _Out_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _Out_opt_ PULONG_PTR LowLimit,
    _Out_opt_ PULONG_PTR HighLimit,
    _Out_opt_ PEXCEPTION_ROUTINE *HandlerRoutine,
    _In_ ULONG UnwindFlags)
{
    RTL_RISCV64_UNWIND_RESULT Result;
    RTL_RISCV64_UNWIND_VIEW View;
    RUNTIME_FUNCTION LocalFunctionEntry;
    RUNTIME_FUNCTION SeenEntries[RTL_RISCV64_MAX_CHAIN_DEPTH + 1];
    PIMAGE_NT_HEADERS NtHeaders;
    PRUNTIME_FUNCTION DecodedFunctionEntry;
    ULONG_PTR StackLow, StackHigh;
    NTSTATUS Status;

    if ((ContextRecord == NULL) || (UnwindFlags != 0))
        return STATUS_INVALID_PARAMETER;

    RtlpGetStackLimits(&StackLow, &StackHigh);
    Status = RtlpRiscv64UnwindUserException(ControlPc, StackLow, StackHigh, ContextRecord);
    if (Status != STATUS_NOT_FOUND) {
        if (!NT_SUCCESS(Status)) return Status;
        if (MachineFrameUnwound) *MachineFrameUnwound = TRUE;
        if (HandlerData) *HandlerData = NULL;
        if (HandlerRoutine) *HandlerRoutine = NULL;
        if (EstablisherFrame) *EstablisherFrame = ContextRecord->Sp;
        if (ContextPointers) RtlZeroMemory(ContextPointers, sizeof(*ContextPointers));
        if (LowLimit) *LowLimit = StackLow;
        if (HighLimit) *HighLimit = StackHigh;
        return STATUS_SUCCESS;
    }
    RtlZeroMemory(&View, sizeof(View));
    View.ImageBase = ImageBase;
    View.StackLow = StackLow;
    View.StackHigh = StackHigh;
    View.ReadMemory = RtlpRiscv64ReadLocalMemory;
    DecodedFunctionEntry = FunctionEntry;

    if (FunctionEntry != NULL)
    {
        Status = RtlpRiscv64ReadMemory(&LocalFunctionEntry,
                                    FunctionEntry,
                                    sizeof(LocalFunctionEntry));
        if (!NT_SUCCESS(Status))
            return STATUS_BAD_FUNCTION_TABLE;

        NtHeaders = RtlImageNtHeader((PVOID)ImageBase);
        if (NtHeaders != NULL)
        {
            View.ImageSize = NtHeaders->OptionalHeader.SizeOfImage;

            RtlZeroMemory(SeenEntries, sizeof(SeenEntries));
            Status = RtlpRiscv64ValidateRecordImageRanges(
                &View,
                NtHeaders,
                &LocalFunctionEntry,
                SeenEntries,
                0);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        else
        {
            /* Dynamic code has no sections to check its records against.
             * Its RVAs span the 32-bit range from the registered base and
             * every read still goes through the checked reader. */
            if (!RtlpRiscv64IsDynamicFunctionEntry(ImageBase, ControlPc, &LocalFunctionEntry))
                return STATUS_INVALID_IMAGE_FORMAT;
            View.ImageSize = MAXULONG;
        }

        DecodedFunctionEntry = &LocalFunctionEntry;
    }

    Status = RtlpRiscv64UnwindFrame(HandlerType,
                                     ControlPc,
                                     DecodedFunctionEntry,
                                     &View,
                                     ContextRecord,
                                     &Result,
                                     ContextPointers);
    if (!NT_SUCCESS(Status))
        return Status;

    if (MachineFrameUnwound != NULL)
        *MachineFrameUnwound = FALSE;
    if (HandlerData != NULL)
        *HandlerData = Result.HandlerData;
    if (EstablisherFrame != NULL)
        *EstablisherFrame = (ULONG_PTR)Result.EstablisherFrame;
    if (LowLimit != NULL)
        *LowLimit = StackLow;
    if (HighLimit != NULL)
        *HighLimit = StackHigh;
    if (HandlerRoutine != NULL)
        *HandlerRoutine = Result.LanguageHandler;

    return STATUS_SUCCESS;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_opt_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Out_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    PEXCEPTION_ROUTINE Handler;
    NTSTATUS Status;

    Handler = NULL;
    Status = RtlVirtualUnwind2(HandlerType,
                               (ULONG_PTR)ImageBase,
                               (ULONG_PTR)ControlPc,
                               FunctionEntry,
                               ContextRecord,
                               NULL,
                               HandlerData,
                               (PULONG_PTR)EstablisherFrame,
                               ContextPointers,
                               NULL,
                               NULL,
                               &Handler,
                               0);
    if (!NT_SUCCESS(Status))
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);

    return Handler;
}
