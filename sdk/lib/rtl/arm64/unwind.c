/*
 * PROJECT:     ReactOS runtime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Basic unwind helpers needed for ARM64 user-mode runtime
 */

#include <rtl.h>

#ifndef PUNWIND_HISTORY_TABLE
typedef struct _UNWIND_HISTORY_TABLE UNWIND_HISTORY_TABLE, *PUNWIND_HISTORY_TABLE;
#endif

#define NDEBUG
#include <debug.h>

#define ARM64_UNWIND_FLAG_MASK            0x3u

typedef enum _ARM64_FNPDATA_FLAGS
{
    PdataRefToFullXdata = 0,
    PdataPackedUnwindFunction = 1,
    PdataPackedUnwindFragment = 2
} ARM64_FNPDATA_FLAGS;

typedef enum _ARM64_FNPDATA_CR
{
    PdataCrUnchained = 0,
    PdataCrUnchainedSavedLr = 1,
    PdataCrChainedWithPac = 2,
    PdataCrChained = 3
} ARM64_FNPDATA_CR;

typedef union _ARM64_UNWIND_XDATA_HEADER
{
    ULONG HeaderData;
    struct
    {
        ULONG FunctionLength : 18;
        ULONG Version : 2;
        ULONG ExceptionDataPresent : 1;
        ULONG EpilogInHeader : 1;
        ULONG EpilogCount : 5;
        ULONG CodeWords : 5;
    };
} ARM64_UNWIND_XDATA_HEADER, *PARM64_UNWIND_XDATA_HEADER;

#define RVA(base, addr) ((PVOID)((ULONG_PTR)(base) + (ULONG_PTR)(addr)))

static __inline ULONG
RtlpArm64FunctionLength(
    _In_ const RUNTIME_FUNCTION *FunctionEntry,
    _In_ ULONG64 ImageBase)
{
    if ((FunctionEntry != NULL) &&
        (FunctionEntry->Flag == PdataRefToFullXdata))
    {
        const ARM64_UNWIND_XDATA_HEADER *Xdata;

        Xdata = (const ARM64_UNWIND_XDATA_HEADER *)RVA(ImageBase,
                                                       FunctionEntry->UnwindData);
        return Xdata->FunctionLength << 2;
    }

    /* Packed format length is encoded in 4-byte units. */
    return FunctionEntry->FunctionLength << 2;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Out_ PULONG Length)
{
    PVOID ExceptionDirectory;
    ULONG DirectorySize;

    if (!RtlPcToFileHeader((PVOID)ControlPc, (PVOID *)ImageBase))
    {
        return NULL;
    }

    ExceptionDirectory = RtlImageDirectoryEntryToData((PVOID)*ImageBase,
                                                      TRUE,
                                                      IMAGE_DIRECTORY_ENTRY_EXCEPTION,
                                                      &DirectorySize);
    if (ExceptionDirectory == NULL)
    {
        return NULL;
    }

    *Length = DirectorySize / sizeof(RUNTIME_FUNCTION);
    return (PRUNTIME_FUNCTION)ExceptionDirectory;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    PRUNTIME_FUNCTION FunctionTable;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG TableLength;
    ULONG IndexLo, IndexHi, IndexMid;

    FunctionTable = RtlLookupFunctionTable(ControlPc, ImageBase, &TableLength);
    if (FunctionTable == NULL)
    {
        UNREFERENCED_PARAMETER(HistoryTable);
        return NULL;
    }

    ControlPc -= *ImageBase;

    IndexLo = 0;
    IndexHi = TableLength;
    while (IndexHi > IndexLo)
    {
        ULONG FunctionEnd;

        IndexMid = (IndexLo + IndexHi) / 2;
        FunctionEntry = &FunctionTable[IndexMid];

        FunctionEnd = FunctionEntry->BeginAddress +
                      RtlpArm64FunctionLength(FunctionEntry, *ImageBase);

        if (ControlPc < FunctionEntry->BeginAddress)
        {
            IndexHi = IndexMid;
        }
        else if (ControlPc >= FunctionEnd)
        {
            IndexLo = IndexMid + 1;
        }
        else
        {
            return FunctionEntry;
        }
    }

    return NULL;
}

EXCEPTION_DISPOSITION
NTAPI
RtlpExecuteHandlerForUnwind(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _In_ PVOID DispatcherContext)
{
    PDISPATCHER_CONTEXT Dispatch = (PDISPATCHER_CONTEXT)DispatcherContext;

    return Dispatch->LanguageHandler(ExceptionRecord,
                                     EstablisherFrame,
                                     ContextRecord,
                                     Dispatch);
}

static __inline
BOOL
RtlpIsStackPointerValid(
    _In_ ULONG64 StackPointer,
    _In_ ULONG64 LowLimit,
    _In_ ULONG64 HighLimit)
{
    return (StackPointer >= LowLimit) &&
           (StackPointer < HighLimit) &&
           ((StackPointer & 0xF) == 0);
}

typedef enum _ARM64_UNWIND_OP_KIND
{
    Arm64UnwindOpAlloc = 0,
    Arm64UnwindOpSaveInt,
    Arm64UnwindOpSaveIntPair,
    Arm64UnwindOpSaveFp,
    Arm64UnwindOpSaveFpPair,
    Arm64UnwindOpSaveQ,
    Arm64UnwindOpSaveQPair
} ARM64_UNWIND_OP_KIND;

typedef struct _ARM64_UNWIND_OP
{
    ARM64_UNWIND_OP_KIND Kind;
    UCHAR Reg;
    UCHAR Reg2;
    ULONG Offset;
    ULONG Delta;
} ARM64_UNWIND_OP;

static __inline UCHAR
RtlpArm64GetUnwindByte(
    _In_reads_(CodeWords) const ULONG *UnwindWords,
    _In_ ULONG Index)
{
    ULONG Word = UnwindWords[Index >> 2];
    ULONG Shift = 24 - ((Index & 3u) * 8u);
    return (UCHAR)((Word >> Shift) & 0xFFu);
}

static __inline VOID
RtlpArm64PushUnwindOp(
    _Inout_updates_(MaxOps) ARM64_UNWIND_OP *Ops,
    _Inout_ PULONG Count,
    _In_ ULONG MaxOps,
    _In_ ARM64_UNWIND_OP_KIND Kind,
    _In_ UCHAR Reg,
    _In_ UCHAR Reg2,
    _In_ ULONG Offset,
    _In_ ULONG Delta)
{
    if (*Count >= MaxOps)
        return;

    Ops[*Count].Kind = Kind;
    Ops[*Count].Reg = Reg;
    Ops[*Count].Reg2 = Reg2;
    Ops[*Count].Offset = Offset;
    Ops[*Count].Delta = Delta;
    (*Count)++;
}

static __inline VOID
RtlpArm64SetIntContextPointer(
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ ULONG Reg,
    _In_ PULONG64 Address)
{
    if (!ContextPointers)
        return;

    if ((Reg >= 19) && (Reg <= 28))
    {
        (&ContextPointers->X19)[Reg - 19] = Address;
    }
    else if (Reg == 29)
    {
        ContextPointers->Fp = Address;
    }
    else if (Reg == 30)
    {
        ContextPointers->Lr = Address;
    }
}

static __inline VOID
RtlpArm64SetFpContextPointer(
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ ULONG Reg,
    _In_ PULONG64 Address)
{
    if (!ContextPointers)
        return;

    if ((Reg >= 8) && (Reg <= 15))
    {
        (&ContextPointers->D8)[Reg - 8] = Address;
    }
}

static __inline VOID
RtlpArm64RestoreIntReg(
    _Inout_ PCONTEXT ContextRecord,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ ULONG Reg,
    _In_ ULONG64 Address)
{
    if (Reg >= 31)
        return;

    RtlpArm64SetIntContextPointer(ContextPointers, Reg, (PULONG64)Address);
    ContextRecord->X[Reg] = *(ULONG64 *)Address;
}

static __inline VOID
RtlpArm64RestoreFpReg(
    _Inout_ PCONTEXT ContextRecord,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ ULONG Reg,
    _In_ ULONG64 Address)
{
    if (Reg >= 32)
        return;

    RtlpArm64SetFpContextPointer(ContextPointers, Reg, (PULONG64)Address);
    ContextRecord->V[Reg].Low = *(ULONG64 *)Address;
    ContextRecord->V[Reg].High = 0;
}

static __inline VOID
RtlpArm64RestoreQReg(
    _Inout_ PCONTEXT ContextRecord,
    _In_ ULONG Reg,
    _In_ ULONG64 Address)
{
    if (Reg >= 32)
        return;

    ContextRecord->V[Reg].Low = *(ULONG64 *)Address;
    ContextRecord->V[Reg].High = *(LONG64 *)(Address + sizeof(ULONG64));
}

static BOOLEAN
RtlpArm64DecodeUnwindOps(
    _In_reads_(CodeWords) const ULONG *UnwindWords,
    _In_ ULONG CodeWords,
    _Inout_updates_(MaxOps) ARM64_UNWIND_OP *Ops,
    _In_ ULONG MaxOps,
    _Out_ PULONG OpCount)
{
    ULONG Index = 0;
    ULONG Count = 0;
    ULONG CodeBytes = CodeWords * 4;

    while (Index < CodeBytes)
    {
        UCHAR Op = RtlpArm64GetUnwindByte(UnwindWords, Index++);

        if ((Op == 0xE4) || (Op == 0xE5))
            break;
        if (Op == 0xE3)
            continue;

        if ((Op & 0xE0u) == 0x00u)
        {
            ULONG Size = (ULONG)(Op & 0x1Fu) * 16u;
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpAlloc, 0, 0, 0, Size);
            continue;
        }

        if ((Op & 0xE0u) == 0x20u)
        {
            ULONG Z = (ULONG)(Op & 0x1Fu);
            ULONG Delta = Z * 8u;
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveIntPair, 19, 20, 0, Delta);
            continue;
        }

        if ((Op & 0xC0u) == 0x40u)
        {
            ULONG Z = (ULONG)(Op & 0x3Fu);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveIntPair, 29, 30, Z * 8u, 0);
            continue;
        }

        if ((Op & 0xC0u) == 0x80u)
        {
            ULONG Z = (ULONG)(Op & 0x3Fu);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveIntPair, 29, 30, 0, (Z + 1u) * 8u);
            continue;
        }

        if ((Op & 0xF8u) == 0xC0u)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG Size = (((ULONG)(Op & 0x7u) << 8) | Op2) * 16u;
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpAlloc, 0, 0, 0, Size);
            continue;
        }

        if ((Op & 0xFCu) == 0xC8u)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG X = ((ULONG)(Op & 0x3u) << 2) | ((ULONG)(Op2 >> 6) & 0x3u);
            ULONG Z = (ULONG)(Op2 & 0x3Fu);
            UCHAR Reg = (UCHAR)(19 + X);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveIntPair, Reg, Reg + 1, Z * 8u, 0);
            continue;
        }

        if ((Op & 0xFCu) == 0xCCu)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG X = ((ULONG)(Op & 0x3u) << 2) | ((ULONG)(Op2 >> 6) & 0x3u);
            ULONG Z = (ULONG)(Op2 & 0x3Fu);
            UCHAR Reg = (UCHAR)(19 + X);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveIntPair, Reg, Reg + 1, 0, (Z + 1u) * 8u);
            continue;
        }

        if ((Op & 0xFCu) == 0xD0u)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG X = ((ULONG)(Op & 0x3u) << 2) | ((ULONG)(Op2 >> 6) & 0x3u);
            ULONG Z = (ULONG)(Op2 & 0x3Fu);
            UCHAR Reg = (UCHAR)(19 + X);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveInt, Reg, 0, Z * 8u, 0);
            continue;
        }

        if ((Op & 0xFEu) == 0xD4u)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG X = ((ULONG)(Op & 0x1u) << 3) | ((ULONG)(Op2 >> 5) & 0x7u);
            ULONG Z = (ULONG)(Op2 & 0x1Fu);
            UCHAR Reg = (UCHAR)(19 + X);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveInt, Reg, 0, 0, (Z + 1u) * 8u);
            continue;
        }

        if ((Op & 0xFEu) == 0xD6u)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG X = ((ULONG)(Op & 0x1u) << 2) | ((ULONG)(Op2 >> 6) & 0x3u);
            ULONG Z = (ULONG)(Op2 & 0x3Fu);
            UCHAR Reg = (UCHAR)(19 + (X * 2u));
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveIntPair, Reg, 30, Z * 8u, 0);
            continue;
        }

        if ((Op & 0xFEu) == 0xD8u)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG X = ((ULONG)(Op & 0x1u) << 2) | ((ULONG)(Op2 >> 6) & 0x3u);
            ULONG Z = (ULONG)(Op2 & 0x3Fu);
            UCHAR Reg = (UCHAR)(8 + X);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveFpPair, Reg, Reg + 1, Z * 8u, 0);
            continue;
        }

        if ((Op & 0xFEu) == 0xDAu)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG X = ((ULONG)(Op & 0x1u) << 2) | ((ULONG)(Op2 >> 6) & 0x3u);
            ULONG Z = (ULONG)(Op2 & 0x3Fu);
            UCHAR Reg = (UCHAR)(8 + X);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveFpPair, Reg, Reg + 1, 0, (Z + 1u) * 8u);
            continue;
        }

        if ((Op & 0xFEu) == 0xDCu)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG X = ((ULONG)(Op & 0x1u) << 2) | ((ULONG)(Op2 >> 6) & 0x3u);
            ULONG Z = (ULONG)(Op2 & 0x3Fu);
            UCHAR Reg = (UCHAR)(8 + X);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveFp, Reg, 0, Z * 8u, 0);
            continue;
        }

        if (Op == 0xDEu)
        {
            if (Index >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG X = (ULONG)((Op2 >> 5) & 0x7u);
            ULONG Z = (ULONG)(Op2 & 0x1Fu);
            UCHAR Reg = (UCHAR)(8 + X);
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveFp, Reg, 0, 0, (Z + 1u) * 8u);
            continue;
        }

        if (Op == 0xDFu)
        {
            if (Index >= CodeBytes)
                break;
            (void)RtlpArm64GetUnwindByte(UnwindWords, Index++);
            return FALSE;
        }

        if (Op == 0xE0u)
        {
            if ((Index + 2) >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            UCHAR Op3 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            UCHAR Op4 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            ULONG Size = ((ULONG)Op2 << 16) | ((ULONG)Op3 << 8) | Op4;
            RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpAlloc, 0, 0, 0, Size * 16u);
            continue;
        }

        if (Op == 0xE1u)
        {
            continue;
        }

        if (Op == 0xE2u)
        {
            if (Index >= CodeBytes)
                break;
            (void)RtlpArm64GetUnwindByte(UnwindWords, Index++);
            continue;
        }

        if (Op == 0xE6u)
        {
            continue;
        }

        if (Op == 0xE7u)
        {
            if ((Index + 1) >= CodeBytes)
                break;
            UCHAR Op2 = RtlpArm64GetUnwindByte(UnwindWords, Index++);
            UCHAR Op3 = RtlpArm64GetUnwindByte(UnwindWords, Index++);

            if (Op2 & 0x80u)
                return FALSE;

            ULONG Pair = (Op2 >> 6) & 0x1u;
            ULONG PreIndex = (Op2 >> 5) & 0x1u;
            ULONG Reg = Op2 & 0x1Fu;
            ULONG Kind = (Op3 >> 6) & 0x3u;
            ULONG Offset = Op3 & 0x3Fu;
            ULONG Step = (PreIndex || Pair) ? 16u : 8u;
            ULONG Delta = 0;

            if (Kind == 2)
            {
                Step = 16u;
            }

            if (PreIndex)
            {
                Delta = Offset * Step;
                Offset = 0;
            }
            else
            {
                Offset = Offset * Step;
            }

            if (Kind == 0)
            {
                if (Pair)
                    RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveIntPair, (UCHAR)Reg, (UCHAR)(Reg + 1), Offset, Delta);
                else
                    RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveInt, (UCHAR)Reg, 0, Offset, Delta);
            }
            else if (Kind == 1)
            {
                if (Pair)
                    RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveFpPair, (UCHAR)Reg, (UCHAR)(Reg + 1), Offset, Delta);
                else
                    RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveFp, (UCHAR)Reg, 0, Offset, Delta);
            }
            else if (Kind == 2)
            {
                if (Pair)
                    RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveQPair, (UCHAR)Reg, (UCHAR)(Reg + 1), Offset, Delta);
                else
                    RtlpArm64PushUnwindOp(Ops, &Count, MaxOps, Arm64UnwindOpSaveQ, (UCHAR)Reg, 0, Offset, Delta);
            }
            else
            {
                return FALSE;
            }
            continue;
        }

        if (Op >= 0xE8u && Op <= 0xEFu)
        {
            return FALSE;
        }

        if (Op == 0xFCu)
        {
            continue;
        }

        return FALSE;
    }

    *OpCount = Count;
    return TRUE;
}

static VOID
RtlpArm64ApplyUnwindOps(
    _In_reads_(OpCount) const ARM64_UNWIND_OP *Ops,
    _In_ ULONG OpCount,
    _Inout_ PCONTEXT ContextRecord,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    while (OpCount--)
    {
        const ARM64_UNWIND_OP *Op = &Ops[OpCount];
        ULONG64 Address = ContextRecord->Sp + Op->Offset;

        switch (Op->Kind)
        {
            case Arm64UnwindOpAlloc:
                ContextRecord->Sp += Op->Delta;
                continue;
            case Arm64UnwindOpSaveInt:
                RtlpArm64RestoreIntReg(ContextRecord, ContextPointers, Op->Reg, Address);
                break;
            case Arm64UnwindOpSaveIntPair:
                RtlpArm64RestoreIntReg(ContextRecord, ContextPointers, Op->Reg, Address);
                RtlpArm64RestoreIntReg(ContextRecord, ContextPointers, Op->Reg2, Address + sizeof(ULONG64));
                break;
            case Arm64UnwindOpSaveFp:
                RtlpArm64RestoreFpReg(ContextRecord, ContextPointers, Op->Reg, Address);
                break;
            case Arm64UnwindOpSaveFpPair:
                RtlpArm64RestoreFpReg(ContextRecord, ContextPointers, Op->Reg, Address);
                RtlpArm64RestoreFpReg(ContextRecord, ContextPointers, Op->Reg2, Address + sizeof(ULONG64));
                break;
            case Arm64UnwindOpSaveQ:
                RtlpArm64RestoreQReg(ContextRecord, Op->Reg, Address);
                break;
            case Arm64UnwindOpSaveQPair:
                RtlpArm64RestoreQReg(ContextRecord, Op->Reg, Address);
                RtlpArm64RestoreQReg(ContextRecord, Op->Reg2, Address + 16);
                break;
            default:
                break;
        }

        if (Op->Delta)
        {
            ContextRecord->Sp += Op->Delta;
        }
    }
}

BOOLEAN
NTAPI
RtlpUnwindInternal(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable,
    _In_ ULONG HandlerType)
{
    DISPATCHER_CONTEXT DispatcherContext;
    PEXCEPTION_ROUTINE ExceptionRoutine;
    EXCEPTION_DISPOSITION Disposition;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG_PTR StackLow, StackHigh;
    ULONG64 ImageBase, EstablisherFrame;
    CONTEXT UnwindContext;

    RtlpGetStackLimits(&StackLow, &StackHigh);
    if (TargetFrame != NULL)
    {
        StackHigh = (ULONG64)TargetFrame + 1;
    }

    UnwindContext = *ContextRecord;

    DispatcherContext.ContextRecord =
        (HandlerType == UNW_FLAG_UHANDLER) ? ContextRecord : &UnwindContext;
    DispatcherContext.HistoryTable = HistoryTable;
    DispatcherContext.TargetPc = (ULONG_PTR)TargetIp;
    DispatcherContext.ControlPcIsUnwound = FALSE;
    DispatcherContext.NonVolatileRegisters = NULL;

    while (TRUE)
    {
        if (!RtlpIsStackPointerValid(UnwindContext.Sp, StackLow, StackHigh))
        {
            if (HandlerType == UNW_FLAG_EHANDLER)
            {
                ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
                return FALSE;
            }

            RtlRaiseStatus(STATUS_BAD_STACK);
        }

        FunctionEntry = RtlLookupFunctionEntry(UnwindContext.Pc, &ImageBase, HistoryTable);
        if (FunctionEntry == NULL)
        {
            EstablisherFrame = UnwindContext.Sp;
            UnwindContext.Pc = UnwindContext.Lr;
            if (UnwindContext.Pc == 0)
            {
                break;
            }

            if (HandlerType == UNW_FLAG_UHANDLER)
            {
                *ContextRecord = UnwindContext;
            }
            continue;
        }

        DispatcherContext.ControlPc = UnwindContext.Pc;

        ExceptionRoutine = RtlVirtualUnwind(HandlerType,
                                            ImageBase,
                                            UnwindContext.Pc,
                                            FunctionEntry,
                                            &UnwindContext,
                                            &DispatcherContext.HandlerData,
                                            &EstablisherFrame,
                                            NULL);

        if ((EstablisherFrame < StackLow) ||
            (EstablisherFrame >= StackHigh) ||
            (EstablisherFrame & 0xF))
        {
            if (HandlerType == UNW_FLAG_EHANDLER)
            {
                ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
                return FALSE;
            }

            RtlRaiseStatus(STATUS_BAD_STACK);
        }

        if (ExceptionRoutine != NULL)
        {
            if (EstablisherFrame == (ULONG64)TargetFrame)
            {
                ExceptionRecord->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;
            }

            DispatcherContext.ImageBase = ImageBase;
            DispatcherContext.FunctionEntry = FunctionEntry;
            DispatcherContext.LanguageHandler = ExceptionRoutine;
            DispatcherContext.EstablisherFrame = EstablisherFrame;
            DispatcherContext.ScopeIndex = 0;

            UnwindContext.X0 = (ULONG64)ReturnValue;

            Disposition = RtlpExecuteHandlerForUnwind(ExceptionRecord,
                                                      (PVOID)EstablisherFrame,
                                                      DispatcherContext.ContextRecord,
                                                      &DispatcherContext);

            ExceptionRecord->ExceptionFlags &= ~EXCEPTION_TARGET_UNWIND;

            if (HandlerType == UNW_FLAG_EHANDLER)
            {
                if (Disposition == ExceptionContinueExecution)
                {
                    if (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                    {
                        RtlRaiseStatus(EXCEPTION_NONCONTINUABLE_EXCEPTION);
                    }

                    return TRUE;
                }
                else if (Disposition == ExceptionNestedException)
                {
                    ExceptionRecord->ExceptionFlags |= EXCEPTION_NESTED_CALL;
                }
            }

            if ((Disposition != ExceptionContinueSearch) &&
                (Disposition != ExceptionContinueExecution) &&
                (Disposition != ExceptionNestedException))
            {
                RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
            }
        }

        if (EstablisherFrame == (ULONG64)TargetFrame)
        {
            break;
        }

        if (HandlerType == UNW_FLAG_UHANDLER)
        {
            *ContextRecord = UnwindContext;
        }
    }

    if (HandlerType == UNW_FLAG_EHANDLER)
    {
        return FALSE;
    }

    if (ExceptionRecord->ExceptionCode != STATUS_UNWIND_CONSOLIDATE)
    {
        ContextRecord->Pc = (ULONG64)TargetIp;
    }

    ContextRecord->X0 = (ULONG64)ReturnValue;

    RtlRestoreContext(ContextRecord, ExceptionRecord);

    ASSERT(FALSE);
    return FALSE;
}

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable)
{
    EXCEPTION_RECORD LocalExceptionRecord;

    RtlCaptureContext(ContextRecord);

    if (ExceptionRecord == NULL)
    {
        LocalExceptionRecord.ExceptionCode = STATUS_UNWIND;
        LocalExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)ContextRecord->Pc;
        LocalExceptionRecord.ExceptionRecord = NULL;
        LocalExceptionRecord.NumberParameters = 0;
        ExceptionRecord = &LocalExceptionRecord;
    }

    ExceptionRecord->ExceptionFlags = EXCEPTION_UNWINDING;
    if (TargetFrame == NULL)
    {
        ExceptionRecord->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;
    }

    RtlpUnwindInternal(TargetFrame,
                       TargetIp,
                       ExceptionRecord,
                       ReturnValue,
                       ContextRecord,
                       HistoryTable,
                       UNW_FLAG_UHANDLER);
}

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    CONTEXT Context;

    RtlUnwindEx(TargetFrame,
                TargetIp,
                ExceptionRecord,
                ReturnValue,
                &Context,
                NULL);
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_ PVOID *HandlerData,
    _Out_ PDWORD64 EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    ULONG Flag = 0;
    ULONG FrameSize = 0;
    ULONG RegI = 0, RegF = 0, Cr = 0, Home = 0;
    ULONG64 FrameBytes = 0;
    ULONG64 FrameBase;
    ULONG Offset = 0;
    ULONG64 SavedFp = ContextRecord->Fp;
    ULONG64 SavedLr = ContextRecord->Lr;
    PEXCEPTION_ROUTINE Handler = NULL;
    PVOID LocalHandlerData = NULL;
    BOOLEAN UseXdata = FALSE;
    ARM64_UNWIND_OP Ops[128];
    ULONG OpCount = 0;

    UNREFERENCED_PARAMETER(HandlerType);
    UNREFERENCED_PARAMETER(ImageBase);
    UNREFERENCED_PARAMETER(ControlPc);

    if (HandlerData)
        *HandlerData = NULL;
    if (ContextPointers)
        RtlZeroMemory(ContextPointers, sizeof(*ContextPointers));

    FrameBase = ContextRecord->Sp;

    if (FunctionEntry != NULL)
    {
        if (FunctionEntry->Flag != PdataRefToFullXdata)
        {
            Flag = FunctionEntry->Flag & ARM64_UNWIND_FLAG_MASK;
            FrameSize = FunctionEntry->FrameSize;
            RegI = FunctionEntry->RegI;
            RegF = FunctionEntry->RegF;
            Cr = FunctionEntry->CR;
            Home = FunctionEntry->H;
        }
        else
        {
            const ARM64_UNWIND_XDATA_HEADER *Xdata;
            ULONG EpilogCount;
            ULONG EpilogTableCount;
            const ULONG *UnwindWords;
            const ULONG *HandlerRvaPtr;

            Xdata = (const ARM64_UNWIND_XDATA_HEADER *)RVA(ImageBase,
                                                          FunctionEntry->UnwindData);
            UseXdata = TRUE;
            EpilogCount = Xdata->EpilogCount;
            EpilogTableCount = (Xdata->EpilogInHeader && (EpilogCount > 0)) ?
                               (EpilogCount - 1) : EpilogCount;

            UnwindWords = (const ULONG *)(Xdata + 1);
            HandlerRvaPtr = UnwindWords + Xdata->CodeWords + EpilogTableCount;

            if (Xdata->ExceptionDataPresent)
            {
                Handler = (PEXCEPTION_ROUTINE)RVA(ImageBase, *HandlerRvaPtr);
                LocalHandlerData = (PVOID)(HandlerRvaPtr + 1);
            }

            if (!RtlpArm64DecodeUnwindOps(UnwindWords,
                                          Xdata->CodeWords,
                                          Ops,
                                          RTL_NUMBER_OF(Ops),
                                          &OpCount))
            {
                UseXdata = FALSE;
            }
        }
    }

    if (RegF)
    {
        RegF += 1;
    }

    FrameBytes = (ULONG64)FrameSize * 16;

    if (UseXdata)
    {
        if (OpCount != 0)
        {
            RtlpArm64ApplyUnwindOps(Ops, OpCount, ContextRecord, ContextPointers);
        }
        SavedFp = ContextRecord->Fp;
        SavedLr = ContextRecord->Lr;
    }
    else if (Flag != 0 && FrameBytes != 0)
    {
        ULONG SaveArea;

        SaveArea = 0;

        if (RegI)
        {
            SaveArea += RegI * 8;
        }
        if (Cr == 1)
        {
            SaveArea += 8;
        }
        if (RegF)
        {
            SaveArea += RegF * 8;
        }
        if (Home)
        {
            SaveArea += 8 * 8;
        }

        SaveArea = (SaveArea + 0xF) & ~0xF;

        /* Restore integer non-volatiles */
        for (ULONG Index = 0; Index < RegI; Index++)
        {
            RtlpArm64RestoreIntReg(ContextRecord, ContextPointers, 19 + Index, FrameBase + Offset);
            Offset += sizeof(ULONG64);
        }

        if (Cr == 1)
        {
            RtlpArm64RestoreIntReg(ContextRecord, ContextPointers, 30, FrameBase + Offset);
            SavedLr = ContextRecord->Lr;
            Offset += sizeof(ULONG64);
        }

        for (ULONG Index = 0; Index < RegF; Index++)
        {
            RtlpArm64RestoreFpReg(ContextRecord, ContextPointers, 8 + Index, FrameBase + Offset);
            Offset += sizeof(ULONG64);
        }

        Offset += Home * 8 * 8;

        if (FrameBytes >= 16)
        {
            ULONG64 Pair = FrameBase + FrameBytes - 16;
            RtlpArm64RestoreIntReg(ContextRecord, ContextPointers, 29, Pair);
            RtlpArm64RestoreIntReg(ContextRecord, ContextPointers, 30, Pair + 8);
            SavedFp = ContextRecord->Fp;
            SavedLr = ContextRecord->Lr;
        }

        ContextRecord->Sp = FrameBase + FrameBytes;
    }
    else if (ContextRecord->Fp != 0)
    {
        SavedFp = *(ULONG64 *)(ContextRecord->Fp);
        SavedLr = *(ULONG64 *)(ContextRecord->Fp + sizeof(ULONG64));
        ContextRecord->Sp = ContextRecord->Fp + 2 * sizeof(ULONG64);
    }
    else
    {
        SavedLr = *(ULONG64 *)(ContextRecord->Sp);
        ContextRecord->Sp += sizeof(ULONG64);
    }

    ContextRecord->Fp = SavedFp;
    ContextRecord->Lr = SavedLr;
    ContextRecord->Pc = SavedLr;

    if (EstablisherFrame)
    {
        *EstablisherFrame = ContextRecord->Sp;
    }

    if (Handler != NULL)
    {
        if (HandlerData)
            *HandlerData = LocalHandlerData;
        return Handler;
    }

    return NULL;
}
