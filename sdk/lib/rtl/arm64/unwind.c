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
#define ARM64_PACKED_REGF_SHIFT 13
#define ARM64_PACKED_REGF_MASK 0x7UL
#define ARM64_PACKED_REGI_SHIFT 16
#define ARM64_PACKED_REGI_MASK 0xFUL
#define ARM64_PACKED_HOME_PARAMS (1UL << 20)
#define ARM64_PACKED_CR_SHIFT 21
#define ARM64_PACKED_CR_MASK 0x3UL
#define ARM64_PACKED_FRAMESZ_SHIFT 23
#define ARM64_PACKED_FRAMESZ_MASK 0x1FFUL
#define ARM64_XDATA_FUNCTION_LENGTH_MASK 0x3FFFFUL
#define ARM64_XDATA_EXCEPTION_DATA (1UL << 20)
#define ARM64_XDATA_EPILOGUE_PACKED (1UL << 21)
#define ARM64_XDATA_EPILOGUE_COUNT_SHIFT 22
#define ARM64_XDATA_EPILOGUE_COUNT_MASK 0x1FUL
#define ARM64_XDATA_CODE_WORDS_SHIFT 27
#define ARM64_XDATA_CODE_WORDS_MASK 0x1FUL

#define ARM64_CR_UNCHAINED 0
#define ARM64_CR_UNCHAINED_SAVED_LR 1
#define ARM64_CR_CHAINED_PAC 2
#define ARM64_CR_CHAINED 3

typedef struct _ARM64_PACKED_INFO
{
    ULONG FunctionLength;
    ULONG FrameSize;
    ULONG RegI;
    ULONG RegF;
    BOOLEAN HomesParams;
    ULONG CR;
} ARM64_PACKED_INFO, *PARM64_PACKED_INFO;

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

static ULONG
RtlpArm64UnwindCodeSize(
    _In_ UCHAR Opcode)
{
    if (Opcode <= 0xBF ||
        Opcode == 0xE1 ||
        Opcode == 0xE3 ||
        Opcode == 0xE4 ||
        Opcode == 0xE5 ||
        Opcode == 0xE6 ||
        (Opcode >= 0xFC))
    {
        return 1;
    }

    if (((Opcode >= 0xC0) && (Opcode <= 0xDF)) ||
        Opcode == 0xE2 ||
        Opcode == 0xF8)
    {
        return 2;
    }

    if (Opcode == 0xE7 || Opcode == 0xF9)
        return 3;

    if (Opcode == 0xE0 || Opcode == 0xFA)
        return 4;

    if (Opcode == 0xFB)
        return 5;

    return 1;
}

static ULONG
RtlpArm64AlignUp(
    _In_ ULONG Value,
    _In_ ULONG Alignment)
{
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

static VOID
RtlpArm64DecodePacked(
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Out_ PARM64_PACKED_INFO Info)
{
    ULONG UnwindData = FunctionEntry->UnwindData;

    RtlZeroMemory(Info, sizeof(*Info));

    Info->FunctionLength = ((UnwindData >> ARM64_PACKED_FUNCTION_LENGTH_SHIFT) &
                            ARM64_PACKED_FUNCTION_LENGTH_MASK) * sizeof(ULONG);
    Info->RegF = (UnwindData >> ARM64_PACKED_REGF_SHIFT) & ARM64_PACKED_REGF_MASK;
    Info->RegI = (UnwindData >> ARM64_PACKED_REGI_SHIFT) & ARM64_PACKED_REGI_MASK;
    Info->HomesParams = !!(UnwindData & ARM64_PACKED_HOME_PARAMS);
    Info->CR = (UnwindData >> ARM64_PACKED_CR_SHIFT) & ARM64_PACKED_CR_MASK;
    Info->FrameSize = ((UnwindData >> ARM64_PACKED_FRAMESZ_SHIFT) &
                       ARM64_PACKED_FRAMESZ_MASK) * 16;
}

static VOID
RtlpArm64NormalizeLr(
    _Inout_ PCONTEXT Context)
{
    ULONG64 Pc = Context->Lr & ((1ULL << 48) - 1);

    if (Pc & (1ULL << 47))
        Pc |= 0xFFFFULL << 48;

    Context->Lr = Pc;
}

static VOID
RtlpArm64RestoreRegister(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Register,
    _In_ ULONG64 Address)
{
    if (Register <= 30)
        Context->X[Register] = *(PULONG64)(ULONG_PTR)Address;
}

static VOID
RtlpArm64RestoreRegisterRange(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Register,
    _In_ ULONG Count,
    _In_ LONG Position)
{
    ULONG Index;
    ULONG Offset = (Position > 0) ? (ULONG)Position : 0;

    for (Index = 0; Index < Count; Index++)
    {
        RtlpArm64RestoreRegister(Context,
                                 Register + Index,
                                 Context->Sp + ((Offset + Index) * sizeof(ULONG64)));
    }

    if (Position < 0)
        Context->Sp += (ULONG64)(-Position) * sizeof(ULONG64);
}

static VOID
RtlpArm64RestoreFpRegisterRange(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Register,
    _In_ ULONG Count,
    _In_ LONG Position)
{
    ULONG Index;
    ULONG Offset = (Position > 0) ? (ULONG)Position : 0;

    for (Index = 0; Index < Count; Index++)
    {
        Context->V[Register + Index].Low =
            *(PULONG64)(ULONG_PTR)(Context->Sp + ((Offset + Index) * sizeof(ULONG64)));
        Context->V[Register + Index].High = 0;
    }

    if (Position < 0)
        Context->Sp += (ULONG64)(-Position) * sizeof(ULONG64);
}

static VOID
RtlpArm64RestoreRegisterPair(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Register,
    _In_ ULONG64 Address,
    _In_ ULONG ExtraPairs)
{
    ULONG Pair;

    for (Pair = 0; Pair <= ExtraPairs; Pair++)
    {
        RtlpArm64RestoreRegister(Context, Register + (Pair * 2), Address + (Pair * 16));
        RtlpArm64RestoreRegister(Context, Register + (Pair * 2) + 1, Address + (Pair * 16) + sizeof(ULONG64));
    }
}

static BOOLEAN
RtlpArm64UnwindPacked(
    _In_ PARM64_PACKED_INFO Info,
    _Inout_ PCONTEXT Context)
{
    ULONG IntSize;
    ULONG FpSize;
    ULONG RegSave;
    ULONG LocalSize;
    ULONG IntRegs;
    ULONG FpRegs;
    ULONG SavedRegs;

    IntSize = Info->RegI * sizeof(ULONG64);
    FpSize = Info->RegF * sizeof(ULONG64);

    if (Info->CR == ARM64_CR_UNCHAINED_SAVED_LR)
        IntSize += sizeof(ULONG64);

    if (Info->RegF != 0)
        FpSize += sizeof(ULONG64);

    RegSave = RtlpArm64AlignUp(IntSize + FpSize +
                               (Info->HomesParams ? (8 * sizeof(ULONG64)) : 0),
                               16);
    if (Info->FrameSize < RegSave)
        return FALSE;

    LocalSize = Info->FrameSize - RegSave;
    IntRegs = IntSize / sizeof(ULONG64);
    FpRegs = FpSize / sizeof(ULONG64);
    SavedRegs = RegSave / sizeof(ULONG64);

    if ((Info->CR == ARM64_CR_CHAINED) ||
        (Info->CR == ARM64_CR_CHAINED_PAC))
    {
        Context->Sp = Context->Fp;
        RtlpArm64RestoreRegisterRange(Context, 29, 2, 0);
    }

    Context->Sp += LocalSize;

    if (FpRegs != 0)
        RtlpArm64RestoreFpRegisterRange(Context, 8, FpRegs, IntRegs);

    if (Info->CR == ARM64_CR_UNCHAINED_SAVED_LR)
        RtlpArm64RestoreRegisterRange(Context, 30, 1, IntRegs - 1);

    RtlpArm64RestoreRegisterRange(Context, 19, Info->RegI, -(LONG)SavedRegs);

    if (Info->CR == ARM64_CR_CHAINED_PAC)
        RtlpArm64NormalizeLr(Context);

    return TRUE;
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
    ULONG SaveNextPairs = 0;
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
        ARM64_PACKED_INFO PackedInfo;
        BOOLEAN ChainedFunction;

        RtlpArm64DecodePacked(FunctionEntry, &PackedInfo);
        ChainedFunction = (PackedInfo.CR == ARM64_CR_CHAINED) ||
                          (PackedInfo.CR == ARM64_CR_CHAINED_PAC);
        FunctionLength = RtlpArm64FunctionLength((ULONG_PTR)ImageBase, FunctionEntry);
        if (ControlRva < FunctionEntry->BeginAddress ||
            ControlRva >= FunctionEntry->BeginAddress + FunctionLength)
        {
            return NULL;
        }

        *EstablisherFrame = ChainedFunction ? Context->Fp : Context->Sp;
        if (!RtlpArm64UnwindPacked(&PackedInfo, Context))
            return NULL;

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
        CodeIdx += RtlpArm64UnwindCodeSize(Opcode);
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
            ULONG Register;

            if (Opcode <= 0x1F)
            {
                Context->Sp += (Opcode & 0x1F) * 16;
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0x20 && Opcode <= 0x3F)
            {
                RtlpArm64RestoreRegisterPair(Context, 19, Context->Sp, SaveNextPairs);
                Context->Sp += (Opcode & 0x1F) * 8;
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0x40 && Opcode <= 0x7F)
            {
                Offset = (Opcode & 0x3F) * 8;
                Context->Fp = *(ULONG64 *)(Context->Sp + Offset);
                Context->Lr = *(ULONG64 *)(Context->Sp + Offset + sizeof(ULONG64));
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0x80 && Opcode <= 0xBF)
            {
                Offset = (Opcode & 0x3F) + 1;
                Context->Fp = *(ULONG64 *)Context->Sp;
                Context->Lr = *(ULONG64 *)(Context->Sp + sizeof(ULONG64));
                Context->Sp += Offset * 8;
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0xC0 && Opcode <= 0xC7)
            {
                Size = 2;
                Offset = (((Opcode & 0x07) << 8) | UnwindCodes[CodeIdx + 1]) * 16;
                Context->Sp += Offset;
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0xC8 && Opcode <= 0xCB)
            {
                UCHAR Next = UnwindCodes[CodeIdx + 1];

                Size = 2;
                Register = 19 + ((Opcode & 0x03) << 2) + ((Next >> 6) & 0x03);
                Offset = (Next & 0x3F) * 8;
                RtlpArm64RestoreRegisterPair(Context, Register, Context->Sp + Offset, SaveNextPairs);
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0xCC && Opcode <= 0xCF)
            {
                UCHAR Next = UnwindCodes[CodeIdx + 1];

                Size = 2;
                Register = 19 + ((Opcode & 0x03) << 2) + ((Next >> 6) & 0x03);
                RtlpArm64RestoreRegisterPair(Context, Register, Context->Sp, SaveNextPairs);
                Context->Sp += ((Next & 0x3F) + 1) * 8;
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0xD0 && Opcode <= 0xD3)
            {
                UCHAR Next = UnwindCodes[CodeIdx + 1];

                Size = 2;
                Register = 19 + ((Opcode & 0x03) << 2) + ((Next >> 6) & 0x03);
                Offset = (Next & 0x3F) * 8;
                RtlpArm64RestoreRegister(Context, Register, Context->Sp + Offset);
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0xD4 && Opcode <= 0xD5)
            {
                UCHAR Next = UnwindCodes[CodeIdx + 1];

                Size = 2;
                Register = 19 + ((Opcode & 0x01) << 3) + ((Next >> 5) & 0x07);
                RtlpArm64RestoreRegister(Context, Register, Context->Sp);
                Context->Sp += ((Next & 0x1F) + 1) * 8;
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0xD6 && Opcode <= 0xD7)
            {
                UCHAR Next = UnwindCodes[CodeIdx + 1];

                Size = 2;
                Register = 19 + (2 * (((Opcode & 0x01) << 2) + ((Next >> 6) & 0x03)));
                Offset = (Next & 0x3F) * 8;
                RtlpArm64RestoreRegister(Context, Register, Context->Sp + Offset);
                Context->Lr = *(ULONG64 *)(Context->Sp + Offset + sizeof(ULONG64));
                SaveNextPairs = 0;
            }
            else if (Opcode >= 0xD8 && Opcode <= 0xDE)
            {
                Size = 2;
                if ((Opcode == 0xDA) || (Opcode == 0xDB) || (Opcode == 0xDE))
                {
                    Offset = (Opcode == 0xDE) ? (UnwindCodes[CodeIdx + 1] & 0x1F) :
                                                 (UnwindCodes[CodeIdx + 1] & 0x3F);
                    Context->Sp += (Offset + 1) * 8;
                }
                SaveNextPairs = 0;
            }
            else if (Opcode == 0xDF)
            {
                Size = 2;
                SaveNextPairs = 0;
            }
            else if (Opcode == 0xE0)
            {
                Size = 4;
                Offset = ((ULONG)UnwindCodes[CodeIdx + 1] << 16) |
                         ((ULONG)UnwindCodes[CodeIdx + 2] << 8) |
                         UnwindCodes[CodeIdx + 3];
                Context->Sp += Offset * 16;
                SaveNextPairs = 0;
            }
            else if (Opcode == 0xE1)
            {
                Context->Sp = Context->Fp;
                SaveNextPairs = 0;
            }
            else if (Opcode == 0xE2)
            {
                Size = 2;
                Context->Sp = Context->Fp - (UnwindCodes[CodeIdx + 1] * 8);
                SaveNextPairs = 0;
            }
            else if (Opcode == 0xE3)
            {
                SaveNextPairs = 0;
            }
            else if (Opcode == 0xE4 || Opcode == 0xE5)
            {
                break;
            }
            else if (Opcode == 0xE6)
            {
                SaveNextPairs++;
            }
            else if (Opcode == 0xE7)
            {
                Size = 3;
            }
            else
                Size = RtlpArm64UnwindCodeSize(Opcode);

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
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _Inout_opt_ PVOID HistoryTable)
{
    EXCEPTION_RECORD LocalExceptionRecord;
    DISPATCHER_CONTEXT DispatcherContext;
    PEXCEPTION_ROUTINE ExceptionRoutine;
    EXCEPTION_DISPOSITION Disposition;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase, EstablisherFrame;
    CONTEXT UnwindContext, FrameContext;
    ULONG_PTR StackLow, StackHigh;
    ULONG FrameCount;
    BOOLEAN HaveTarget;

    if (ContextRecord == NULL)
        return;

    if (ExceptionRecord == NULL)
    {
        RtlZeroMemory(&LocalExceptionRecord, sizeof(LocalExceptionRecord));
        LocalExceptionRecord.ExceptionCode = STATUS_UNWIND;
        LocalExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)ContextRecord->Pc;
        ExceptionRecord = &LocalExceptionRecord;
    }

    ExceptionRecord->ExceptionFlags = EXCEPTION_UNWINDING;
    if (TargetFrame == NULL)
        ExceptionRecord->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;

    HaveTarget = (TargetFrame != NULL) || (TargetIp != NULL);
    UnwindContext = *ContextRecord;
    RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
    DispatcherContext.ContextRecord = ContextRecord;
    DispatcherContext.HistoryTable = HistoryTable;
    DispatcherContext.TargetPc = (ULONG64)(ULONG_PTR)TargetIp;

    RtlpGetStackLimits(&StackLow, &StackHigh);
    if (TargetFrame != NULL)
        StackHigh = (ULONG_PTR)TargetFrame + 1;

    for (FrameCount = 0; FrameCount < 1024; FrameCount++)
    {
        if ((UnwindContext.Sp < StackLow) ||
            (UnwindContext.Sp > StackHigh) ||
            (UnwindContext.Sp & (sizeof(ULONG64) - 1)))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return;
        }

        FunctionEntry = RtlLookupFunctionEntry(UnwindContext.Pc, &ImageBase, NULL);
        if (FunctionEntry == NULL)
        {
            if ((UnwindContext.Lr == 0) ||
                (UnwindContext.Lr == UnwindContext.Pc))
            {
                break;
            }

            UnwindContext.Pc = UnwindContext.Lr;
            continue;
        }

        FrameContext = UnwindContext;
        DispatcherContext.ControlPc = UnwindContext.Pc;
        ExceptionRoutine = RtlVirtualUnwind(UNW_FLAG_UHANDLER,
                                            ImageBase,
                                            UnwindContext.Pc,
                                            FunctionEntry,
                                            &UnwindContext,
                                            &DispatcherContext.HandlerData,
                                            &EstablisherFrame,
                                            NULL);

        if ((EstablisherFrame < StackLow) ||
            (EstablisherFrame >= StackHigh) ||
            (EstablisherFrame & (sizeof(ULONG64) - 1)))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return;
        }

        if (ExceptionRoutine != NULL)
        {
            if (HaveTarget && (EstablisherFrame == (ULONG64)(ULONG_PTR)TargetFrame))
                ExceptionRecord->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;

            DispatcherContext.ImageBase = ImageBase;
            DispatcherContext.FunctionEntry = FunctionEntry;
            DispatcherContext.LanguageHandler = ExceptionRoutine;
            DispatcherContext.EstablisherFrame = EstablisherFrame;
            DispatcherContext.ScopeIndex = 0;

            do
            {
                Disposition = ExceptionRoutine(ExceptionRecord,
                                               (PVOID)EstablisherFrame,
                                               ContextRecord,
                                               &DispatcherContext);

                ExceptionRecord->ExceptionFlags &= ~(EXCEPTION_TARGET_UNWIND |
                                                     EXCEPTION_COLLIDED_UNWIND);

                if (Disposition == ExceptionContinueExecution)
                {
                    if (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                        RtlRaiseStatus(STATUS_NONCONTINUABLE_EXCEPTION);
                    return;
                }

                if (Disposition == ExceptionCollidedUnwind)
                {
                    UnwindContext = *ContextRecord;
                    DispatcherContext.ContextRecord = ContextRecord;
                    EstablisherFrame = DispatcherContext.EstablisherFrame;
                    ExceptionRecord->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
                }
                else if (Disposition != ExceptionContinueSearch)
                {
                    RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
                }
            }
            while (Disposition == ExceptionCollidedUnwind);
        }

        if (HaveTarget && (EstablisherFrame == (ULONG64)(ULONG_PTR)TargetFrame))
        {
            if (TargetIp != NULL)
                FrameContext.Pc = (ULONG64)(ULONG_PTR)TargetIp;

            FrameContext.X0 = (ULONG64)(ULONG_PTR)ReturnValue;
            *ContextRecord = FrameContext;
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
