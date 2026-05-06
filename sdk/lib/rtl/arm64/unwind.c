/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 user-mode PE/COFF unwinding support
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

#ifndef UNW_FLAG_EHANDLER
#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2
#endif

#define ARM64_UNWIND_FLAG_MASK 0x3UL
#define ARM64_PACKED_FUNCTION_LENGTH_SHIFT 2
#define ARM64_PACKED_FUNCTION_LENGTH_MASK 0x7FFUL
#define ARM64_XDATA_FUNCTION_LENGTH_MASK 0x3FFFFUL
#define ARM64_XDATA_EXCEPTION_DATA (1UL << 20)
#define ARM64_XDATA_EPILOGUE_PACKED (1UL << 21)
#define ARM64_XDATA_EPILOGUE_COUNT_SHIFT 22
#define ARM64_XDATA_EPILOGUE_COUNT_MASK 0x1FUL
#define ARM64_XDATA_CODE_WORDS_SHIFT 27
#define ARM64_XDATA_CODE_WORDS_MASK 0x1FUL

static ULONG
RtlpArm64FunctionLength(
    _In_ ULONG_PTR ImageBase,
    _In_ PRUNTIME_FUNCTION FunctionEntry)
{
    ULONG UnwindData;
    PULONG Xdata;

    UnwindData = FunctionEntry->UnwindData;
    if ((UnwindData & ARM64_UNWIND_FLAG_MASK) != 0)
    {
        return ((UnwindData >> ARM64_PACKED_FUNCTION_LENGTH_SHIFT) &
                ARM64_PACKED_FUNCTION_LENGTH_MASK) * sizeof(ULONG);
    }

    Xdata = (PULONG)(ImageBase + UnwindData);
    return (Xdata[0] & ARM64_XDATA_FUNCTION_LENGTH_MASK) * sizeof(ULONG);
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ PVOID HistoryTable)
{
    PIMAGE_DOS_HEADER DosHeader = NULL;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_DATA_DIRECTORY ExceptionDir;
    PRUNTIME_FUNCTION FunctionTable, FunctionEntry;
    ULONG TableLength;
    ULONG_PTR ControlRva;
    ULONG IndexLow, IndexHigh, IndexMid;
    ULONG_PTR Cookie = 0;
    PLIST_ENTRY ListHead, Entry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;

    (VOID)HistoryTable;

    if (!NT_SUCCESS(LdrLockLoaderLock(0, NULL, &Cookie)))
    {
        *ImageBase = 0;
        return NULL;
    }

    ListHead = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        LdrEntry = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (ControlPc >= (ULONG_PTR)LdrEntry->DllBase &&
            ControlPc < (ULONG_PTR)LdrEntry->DllBase + LdrEntry->SizeOfImage)
        {
            DosHeader = (PIMAGE_DOS_HEADER)LdrEntry->DllBase;
            break;
        }
    }

    LdrUnlockLoaderLock(0, Cookie);

    if (DosHeader == NULL || DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        *ImageBase = 0;
        return NULL;
    }

    *ImageBase = (DWORD64)(ULONG_PTR)DosHeader;
    NtHeaders = (PIMAGE_NT_HEADERS)((ULONG_PTR)DosHeader + DosHeader->e_lfanew);
    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE)
    {
        *ImageBase = 0;
        return NULL;
    }

    ExceptionDir = &NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (ExceptionDir->VirtualAddress == 0 || ExceptionDir->Size == 0)
        return NULL;

    FunctionTable = (PRUNTIME_FUNCTION)((ULONG_PTR)DosHeader + ExceptionDir->VirtualAddress);
    TableLength = ExceptionDir->Size / sizeof(RUNTIME_FUNCTION);
    ControlRva = (ULONG_PTR)(ControlPc - *ImageBase);

    IndexLow = 0;
    IndexHigh = TableLength;
    while (IndexHigh > IndexLow)
    {
        IndexMid = (IndexLow + IndexHigh) / 2;
        FunctionEntry = &FunctionTable[IndexMid];

        if (ControlRva < FunctionEntry->BeginAddress)
        {
            IndexHigh = IndexMid;
            continue;
        }

        if (ControlRva >= FunctionEntry->BeginAddress +
                          RtlpArm64FunctionLength((ULONG_PTR)*ImageBase, FunctionEntry))
        {
            IndexLow = IndexMid + 1;
            continue;
        }

        return FunctionEntry;
    }

    return NULL;
}

static VOID
RtlpArm64GetXdataLayout(
    _In_ PULONG Xdata,
    _Out_ PULONG CodeWords,
    _Out_ PULONG EpilogCount,
    _Out_ PULONG HeaderWords,
    _Out_ PBOOLEAN EpilogPacked)
{
    ULONG Header = Xdata[0];

    *CodeWords = (Header >> ARM64_XDATA_CODE_WORDS_SHIFT) &
                 ARM64_XDATA_CODE_WORDS_MASK;
    *EpilogCount = (Header >> ARM64_XDATA_EPILOGUE_COUNT_SHIFT) &
                   ARM64_XDATA_EPILOGUE_COUNT_MASK;
    *EpilogPacked = !!(Header & ARM64_XDATA_EPILOGUE_PACKED);

    if (*CodeWords == 0 && *EpilogCount == 0 && !*EpilogPacked)
    {
        *HeaderWords = 2;
        *EpilogCount = Xdata[1] & 0xFFFF;
        *CodeWords = (Xdata[1] >> 16) & 0xFF;
    }
    else
    {
        *HeaderWords = 1;
    }
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT Context,
    _Out_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Inout_opt_ PVOID ContextPointers)
{
    ULONG_PTR ControlRva;
    ULONG UnwindData, Header, CodeWords, EpilogCount, HeaderWords;
    ULONG FunctionLength, PrologSize = 0, CodeIdx = 0;
    BOOLEAN HasPackedFormat, HasExceptionData, EpilogPacked, InProlog = FALSE;
    PULONG Xdata;
    PUCHAR UnwindCodes = NULL;
    ULONG UnwindBytes = 0;

    (VOID)ContextPointers;

    if (HandlerData)
        *HandlerData = NULL;
    *EstablisherFrame = Context->Fp ? Context->Fp : Context->Sp;

    ControlRva = ControlPc - ImageBase;
    UnwindData = FunctionEntry->UnwindData;
    HasPackedFormat = ((UnwindData & ARM64_UNWIND_FLAG_MASK) != 0);

    if (HasPackedFormat)
    {
        FunctionLength = RtlpArm64FunctionLength((ULONG_PTR)ImageBase, FunctionEntry);
        if (ControlRva < FunctionEntry->BeginAddress ||
            ControlRva >= FunctionEntry->BeginAddress + FunctionLength)
        {
            return NULL;
        }

        Context->Pc = Context->Lr;
        return NULL;
    }

    Xdata = (PULONG)(ImageBase + UnwindData);
    Header = Xdata[0];
    FunctionLength = (Header & ARM64_XDATA_FUNCTION_LENGTH_MASK) * sizeof(ULONG);
    if (ControlRva < FunctionEntry->BeginAddress ||
        ControlRva >= FunctionEntry->BeginAddress + FunctionLength)
    {
        return NULL;
    }

    HasExceptionData = !!(Header & ARM64_XDATA_EXCEPTION_DATA);
    RtlpArm64GetXdataLayout(Xdata, &CodeWords, &EpilogCount, &HeaderWords, &EpilogPacked);
    UnwindCodes = (PUCHAR)(&Xdata[HeaderWords + (EpilogPacked ? 0 : EpilogCount)]);
    UnwindBytes = CodeWords * sizeof(ULONG);

    while (CodeIdx < UnwindBytes)
    {
        UCHAR Opcode = UnwindCodes[CodeIdx];

        if (Opcode == 0xE4 || Opcode == 0xE5)
            break;

        PrologSize += sizeof(ULONG);

        if (Opcode <= 0xBF || Opcode == 0xE1 || Opcode == 0xE3 ||
            Opcode == 0xE6 || Opcode >= 0xFC)
            CodeIdx += 1;
        else if ((Opcode >= 0xC0 && Opcode <= 0xDF) || Opcode == 0xE2)
            CodeIdx += 2;
        else if (Opcode == 0xE7)
            CodeIdx += 3;
        else if (Opcode == 0xE0)
            CodeIdx += 4;
        else
            CodeIdx += 1;
    }

    if ((ControlRva - FunctionEntry->BeginAddress) < PrologSize)
        InProlog = TRUE;

    if (!InProlog)
    {
        CodeIdx = 0;
        while (CodeIdx < UnwindBytes)
        {
            UCHAR Opcode = UnwindCodes[CodeIdx];
            ULONG Offset, Size = 1;

            if (Opcode <= 0x1F)
            {
                Context->Sp += (Opcode & 0x1F) * 16;
            }
            else if (Opcode >= 0x20 && Opcode <= 0x3F)
            {
                Context->Sp += (Opcode & 0x1F) * 8;
            }
            else if (Opcode >= 0x40 && Opcode <= 0x7F)
            {
                Offset = (Opcode & 0x3F) * 8;
                Context->Fp = *(ULONG64 *)(Context->Sp + Offset);
                Context->Lr = *(ULONG64 *)(Context->Sp + Offset + sizeof(ULONG64));
            }
            else if (Opcode >= 0x80 && Opcode <= 0xBF)
            {
                Offset = (Opcode & 0x3F) + 1;
                Context->Fp = *(ULONG64 *)Context->Sp;
                Context->Lr = *(ULONG64 *)(Context->Sp + sizeof(ULONG64));
                Context->Sp += Offset * 8;
            }
            else if (Opcode == 0xE1)
            {
                Context->Sp = Context->Fp;
            }
            else if (Opcode == 0xE4 || Opcode == 0xE5)
            {
                break;
            }
            else if ((Opcode >= 0xC0 && Opcode <= 0xDF) || Opcode == 0xE2)
            {
                Size = 2;
            }
            else if (Opcode == 0xE7)
            {
                Size = 3;
            }
            else if (Opcode == 0xE0)
            {
                Size = 4;
            }

            CodeIdx += Size;
        }
    }

    Context->Pc = Context->Lr;

    if (HasExceptionData)
    {
        ULONG HandlerOffset = HeaderWords + (EpilogPacked ? 0 : EpilogCount) + CodeWords;

        if (Xdata[HandlerOffset] != 0 &&
            (HandlerType & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)))
        {
            if (HandlerData)
                *HandlerData = &Xdata[HandlerOffset + 1];
            return (PEXCEPTION_ROUTINE)(ImageBase + Xdata[HandlerOffset]);
        }
    }

    return NULL;
}

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _Inout_opt_ PVOID HistoryTable)
{
    DISPATCHER_CONTEXT DispatcherContext;
    PEXCEPTION_ROUTINE ExceptionRoutine;
    EXCEPTION_DISPOSITION Disposition;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase, EstablisherFrame;
    CONTEXT UnwindContext;

    (VOID)TargetIp;
    (VOID)ReturnValue;
    (VOID)HistoryTable;

    if (ContextRecord == NULL)
        return;

    UnwindContext = *ContextRecord;
    RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));

    while (TRUE)
    {
        FunctionEntry = RtlLookupFunctionEntry(UnwindContext.Pc, &ImageBase, NULL);
        if (FunctionEntry == NULL)
        {
            if (UnwindContext.Sp == 0)
                break;
            UnwindContext.Pc = *(ULONG64 *)UnwindContext.Sp;
            UnwindContext.Sp += sizeof(ULONG64);
            continue;
        }

        DispatcherContext.ControlPc = UnwindContext.Pc;
        ExceptionRoutine = RtlVirtualUnwind(UNW_FLAG_UHANDLER,
                                            ImageBase,
                                            UnwindContext.Pc,
                                            FunctionEntry,
                                            &UnwindContext,
                                            &DispatcherContext.HandlerData,
                                            &EstablisherFrame,
                                            NULL);

        if (ExceptionRoutine != NULL)
        {
            DispatcherContext.ImageBase = ImageBase;
            DispatcherContext.FunctionEntry = FunctionEntry;
            DispatcherContext.LanguageHandler = ExceptionRoutine;
            DispatcherContext.EstablisherFrame = EstablisherFrame;
            DispatcherContext.ContextRecord = &UnwindContext;

            Disposition = ExceptionRoutine(ExceptionRecord,
                                           (PVOID)EstablisherFrame,
                                           ContextRecord,
                                           &DispatcherContext);
            if (Disposition != ExceptionContinueSearch)
                return;
        }

        if (TargetFrame && EstablisherFrame == (ULONG64)(ULONG_PTR)TargetFrame)
        {
            *ContextRecord = UnwindContext;
            return;
        }

        *ContextRecord = UnwindContext;
    }
}

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    CONTEXT ContextRecord;

    RtlCaptureContext(&ContextRecord);
    RtlUnwindEx(TargetFrame,
                TargetIp,
                ExceptionRecord,
                ReturnValue,
                &ContextRecord,
                NULL);
}

VOID
NTAPI
RtlRestoreContext(
    _In_ PCONTEXT Context,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord)
{
    (VOID)Context;
    (VOID)ExceptionRecord;
}

BOOLEAN
NTAPI
RtlAddFunctionTable(
    _In_ PRUNTIME_FUNCTION FunctionTable,
    _In_ ULONG EntryCount,
    _In_ ULONG_PTR BaseAddress)
{
    (VOID)FunctionTable;
    (VOID)EntryCount;
    (VOID)BaseAddress;
    return FALSE;
}

BOOLEAN
NTAPI
RtlDeleteFunctionTable(
    _In_ PRUNTIME_FUNCTION FunctionTable)
{
    (VOID)FunctionTable;
    return FALSE;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Out_ PULONG Length)
{
    (VOID)ControlPc;
    if (ImageBase)
        *ImageBase = 0;
    if (Length)
        *Length = 0;
    return NULL;
}
