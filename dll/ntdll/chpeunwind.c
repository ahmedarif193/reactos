/*
 * PROJECT:     ReactOS runtime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AMD64 unwind primitives for the ARM64EC ntdll bridge
 * COPYRIGHT:   Copyright 2010-2025 Timo Kreuzer <timo.kreuzer@reactos.org>
 * COPYRIGHT:   Adaptation Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *****************************************************************/

#include <ntdll.h>

#define RVA(m, b) ((PVOID)((ULONG_PTR)(b) + (ULONG_PTR)(m)))

#define NDEBUG
#include <debug.h>

#define UNWIND_HISTORY_TABLE_NONE 0
#define UNWIND_HISTORY_TABLE_GLOBAL 1
#define UNWIND_HISTORY_TABLE_LOCAL 2

#define UWOP_PUSH_NONVOL 0
#define UWOP_ALLOC_LARGE 1
#define UWOP_ALLOC_SMALL 2
#define UWOP_SET_FPREG 3
#define UWOP_SAVE_NONVOL 4
#define UWOP_SAVE_NONVOL_FAR 5
#if 0 // These are deprecated / not for x64
#define UWOP_SAVE_XMM 6
#define UWOP_SAVE_XMM_FAR 7
#else
#define UWOP_EPILOG 6
#define UWOP_SPARE_CODE 7
#endif
#define UWOP_SAVE_XMM128 8
#define UWOP_SAVE_XMM128_FAR 9
#define UWOP_PUSH_MACHFRAME 10


typedef unsigned char UBYTE;

typedef union _UNWIND_CODE
{
    struct
    {
        UBYTE CodeOffset;
        UBYTE UnwindOp:4;
        UBYTE OpInfo:4;
    };
    USHORT FrameOffset;
} UNWIND_CODE, *PUNWIND_CODE;

typedef struct _UNWIND_INFO
{
    UBYTE Version:3;
    UBYTE Flags:5;
    UBYTE SizeOfProlog;
    UBYTE CountOfCodes;
    UBYTE FrameRegister:4;
    UBYTE FrameOffset:4;
    UNWIND_CODE UnwindCode[1];
/*    union {
        OPTIONAL ULONG ExceptionHandler;
        OPTIONAL ULONG FunctionEntry;
    };
    OPTIONAL ULONG ExceptionData[];
*/
} UNWIND_INFO, *PUNWIND_INFO;

/* FUNCTIONS *****************************************************************/

/*! ChpepAmd64LookupFunctionTable
 * \brief Locates the table of RUNTIME_FUNCTION entries for a code address.
 * \param ControlPc
 *            Address of the code, for which the table should be searched.
 * \param ImageBase
 *            Pointer to a DWORD64 that receives the base address of the
 *            corresponding executable image.
 * \param Length
 *            Pointer to an ULONG that receives the number of table entries
 *            present in the table.
 */
PRUNTIME_FUNCTION
NTAPI
ChpepAmd64LookupFunctionTable(
    IN DWORD64 ControlPc,
    OUT PDWORD64 ImageBase,
    OUT PULONG Length)
{
    PVOID Table;
    ULONG Size;

    /* Find corresponding file header from code address */
    if (!RtlPcToFileHeader((PVOID)ControlPc, (PVOID*)ImageBase))
    {
        /* Nothing found */
        return NULL;
    }

    /* Locate the exception directory */
    Table = RtlImageDirectoryEntryToData((PVOID)*ImageBase,
                                         TRUE,
                                         IMAGE_DIRECTORY_ENTRY_EXCEPTION,
                                         &Size);

    /* Return the number of entries */
    *Length = Size / sizeof(RUNTIME_FUNCTION);

    /* Return the address of the table */
    return Table;
}

PRUNTIME_FUNCTION
NTAPI
ChpepAmd64LookupDynamicFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _In_ PUNWIND_HISTORY_TABLE HistoryTable);

/*! ChpepAmd64LookupFunctionEntry
 * \brief Locates the RUNTIME_FUNCTION entry corresponding to a code address.
 * \ref https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtllookupfunctionentry
 * \todo Implement HistoryTable
 */
PRUNTIME_FUNCTION
NTAPI
ChpepAmd64LookupFunctionEntry(
    IN DWORD64 ControlPc,
    OUT PDWORD64 ImageBase,
    OUT PUNWIND_HISTORY_TABLE HistoryTable)
{
    PRUNTIME_FUNCTION FunctionTable, FunctionEntry;
    ULONG TableLength;
    ULONG IndexLo, IndexHi, IndexMid;

    /* Find the corresponding table */
    FunctionTable = ChpepAmd64LookupFunctionTable(ControlPc, ImageBase, &TableLength);

    /* If no table is found, try dynamic function tables */
    if (!FunctionTable)
    {
        return ChpepAmd64LookupDynamicFunctionEntry(ControlPc, ImageBase, HistoryTable);
    }

    /* Use relative virtual address */
    ControlPc -= *ImageBase;

    /* Do a binary search */
    IndexLo = 0;
    IndexHi = TableLength;
    while (IndexHi > IndexLo)
    {
        IndexMid = (IndexLo + IndexHi) / 2;
        FunctionEntry = &FunctionTable[IndexMid];

        if (ControlPc < FunctionEntry->BeginAddress)
        {
            /* Continue search in lower half */
            IndexHi = IndexMid;
        }
        else if (ControlPc >= FunctionEntry->EndAddress)
        {
            /* Continue search in upper half */
            IndexLo = IndexMid + 1;
        }
        else
        {
            /* ControlPc is within limits, return entry */
            return FunctionEntry;
        }
    }

    /* Nothing found, return NULL */
    return NULL;
}

static
__inline
ULONG
UnwindOpSlots(
    _In_ UNWIND_CODE UnwindCode)
{
    static const UCHAR UnwindOpExtraSlotTable[] =
    {
        0, // UWOP_PUSH_NONVOL
        1, // UWOP_ALLOC_LARGE (or 3, special cased in lookup code)
        0, // UWOP_ALLOC_SMALL
        0, // UWOP_SET_FPREG
        1, // UWOP_SAVE_NONVOL
        2, // UWOP_SAVE_NONVOL_FAR
        1, // UWOP_EPILOG // previously UWOP_SAVE_XMM
        2, // UWOP_SPARE_CODE // previously UWOP_SAVE_XMM_FAR
        1, // UWOP_SAVE_XMM128
        2, // UWOP_SAVE_XMM128_FAR
        0, // UWOP_PUSH_MACHFRAME
        2, // UWOP_SET_FPREG_LARGE
    };

    if ((UnwindCode.UnwindOp == UWOP_ALLOC_LARGE) &&
        (UnwindCode.OpInfo != 0))
    {
        return 3;
    }
    else
    {
        return UnwindOpExtraSlotTable[UnwindCode.UnwindOp] + 1;
    }
}

static
__inline
void
SetReg(
    _Inout_ PCONTEXT Context,
    _In_ BYTE Reg,
    _In_ DWORD64 Value)
{
    ((DWORD64*)(&Context->Rax))[Reg] = Value;
}

static
__inline
void
SetRegFromStackValue(
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ BYTE Reg,
    _In_ PDWORD64 ValuePointer)
{
    SetReg(Context, Reg, *ValuePointer);
    if (ContextPointers != NULL)
    {
        ContextPointers->IntegerContext[Reg] = ValuePointer;
    }
}

static
__inline
DWORD64
GetReg(
    _In_ PCONTEXT Context,
    _In_ BYTE Reg)
{
    return ((DWORD64*)(&Context->Rax))[Reg];
}

static
__inline
void
PopReg(
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ BYTE Reg)
{
    SetRegFromStackValue(Context, ContextPointers, Reg, (PDWORD64)Context->Rsp);
    Context->Rsp += sizeof(DWORD64);
}

static
__inline
void
SetXmmReg(
    _Inout_ PCONTEXT Context,
    _In_ BYTE Reg,
    _In_ M128A Value)
{
    ((M128A*)(&Context->Xmm0))[Reg] = Value;
}

static
__inline
void
SetXmmRegFromStackValue(
    _Out_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ BYTE Reg,
    _In_ M128A *ValuePointer)
{
    SetXmmReg(Context, Reg, *ValuePointer);
    if (ContextPointers != NULL)
    {
        ContextPointers->FloatingContext[Reg] = ValuePointer;
    }
}

/*! RtlpTryToUnwindEpilog
 * \brief Helper function that tries to unwind epilog instructions.
 * \return TRUE if we have been in an epilog and it could be unwound.
 *         FALSE if the instructions were not allowed for an epilog.
 * \ref
 *  https://docs.microsoft.com/en-us/cpp/build/unwind-procedure
 *  https://docs.microsoft.com/en-us/cpp/build/prolog-and-epilog
 * \todo
 *  - Test and compare with Windows behaviour
 */
static
__inline
BOOLEAN
RtlpTryToUnwindEpilog(
    _Inout_ PCONTEXT Context,
    _In_ ULONG64 ControlPc,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ ULONG64 ImageBase,
    _In_ PRUNTIME_FUNCTION FunctionEntry)
{
    CONTEXT LocalContext;
    BYTE *InstrPtr;
    DWORD Instr;
    BYTE Reg, Mod;
    ULONG64 EndAddress;

    /* Make a local copy of the context */
    LocalContext = *Context;

    InstrPtr = (BYTE*)ControlPc;

    /* Check if first instruction of epilog is "add rsp, x" */
    Instr = *(DWORD*)InstrPtr;
    if ( (Instr & 0x00fffdff) == 0x00c48148 )
    {
        if ( (Instr & 0x0000ff00) == 0x8300 )
        {
            /* This is "add rsp, 0x??" */
            LocalContext.Rsp += Instr >> 24;
            InstrPtr += 4;
        }
        else
        {
            /* This is "add rsp, 0x???????? */
            LocalContext.Rsp += *(DWORD*)(InstrPtr + 3);
            InstrPtr += 7;
        }
    }
    /* Check if first instruction of epilog is "lea rsp, ..." */
    else if ( (Instr & 0x38fffe) == 0x208d48 )
    {
        /* Get the register */
        Reg = (Instr >> 16) & 0x7;

        /* REX.R */
        Reg += (Instr & 1) * 8;

        LocalContext.Rsp = GetReg(&LocalContext, Reg);

        /* Get addressing mode */
        Mod = (Instr >> 22) & 0x3;
        if (Mod == 0)
        {
            /* No displacement */
            InstrPtr += 3;
        }
        else if (Mod == 1)
        {
            /* 1 byte displacement */
            LocalContext.Rsp += (LONG)(CHAR)(Instr >> 24);
            InstrPtr += 4;
        }
        else if (Mod == 2)
        {
            /* 4 bytes displacement */
            LocalContext.Rsp += *(LONG*)(InstrPtr + 3);
            InstrPtr += 7;
        }
    }

    /* Loop the following instructions before the ret */
    EndAddress = FunctionEntry->EndAddress + ImageBase - 1;
    while ((DWORD64)InstrPtr < EndAddress)
    {
        Instr = *(DWORD*)InstrPtr;

        /* Check for a simple pop */
        if ( (Instr & 0xf8) == 0x58 )
        {
            /* Opcode pops a basic register from stack */
            Reg = Instr & 0x7;
            PopReg(&LocalContext, ContextPointers, Reg);
            InstrPtr++;
            continue;
        }

        /* Check for REX + pop */
        if ( (Instr & 0xf8fb) == 0x5841 )
        {
            /* Opcode is pop r8 .. r15 */
            Reg = ((Instr >> 8) & 0x7) + 8;
            PopReg(&LocalContext, ContextPointers, Reg);
            InstrPtr += 2;
            continue;
        }

        /* Opcode not allowed for Epilog */
        return FALSE;
    }

    // check for popfq

    // also allow end with jmp imm, jmp [target], iretq

    /* Check if we are at the ret instruction */
    if ((DWORD64)InstrPtr != EndAddress)
    {
        /* If we went past the end of the function, something is broken! */
        ASSERT((DWORD64)InstrPtr <= EndAddress);
        return FALSE;
    }

    /* Make sure this is really a ret instruction */
    if (*InstrPtr != 0xc3)
    {
        return FALSE;
    }

    /* Unwind is finished, pop new Rip from Stack */
    LocalContext.Rip = *(DWORD64*)LocalContext.Rsp;
    LocalContext.Rsp += sizeof(DWORD64);

    *Context = LocalContext;
    return TRUE;
}

/*!

    \ref https://docs.microsoft.com/en-us/cpp/build/unwind-data-definitions-in-c
*/
static
ULONG64
GetEstablisherFrame(
    _In_ PCONTEXT Context,
    _In_ PUNWIND_INFO UnwindInfo,
    _In_ ULONG_PTR CodeOffset)
{
    ULONG i;

    /* Check if we have a frame register */
    if (UnwindInfo->FrameRegister == 0)
    {
        /* No frame register means we use Rsp */
        return Context->Rsp;
    }

    if ((CodeOffset >= UnwindInfo->SizeOfProlog) ||
        ((UnwindInfo->Flags & UNW_FLAG_CHAININFO) != 0))
    {
        return GetReg(Context, UnwindInfo->FrameRegister) -
               UnwindInfo->FrameOffset * 16;
    }

    /* Loop all unwind ops */
    for (i = 0;
         i < UnwindInfo->CountOfCodes;
         i += UnwindOpSlots(UnwindInfo->UnwindCode[i]))
    {
        /* Skip codes past our code offset */
        if (UnwindInfo->UnwindCode[i].CodeOffset > CodeOffset)
        {
            continue;
        }

        /* Check for SET_FPREG */
        if (UnwindInfo->UnwindCode[i].UnwindOp == UWOP_SET_FPREG)
        {
            return GetReg(Context, UnwindInfo->FrameRegister) -
                       UnwindInfo->FrameOffset * 16;
        }
    }

    return Context->Rsp;
}

PEXCEPTION_ROUTINE
NTAPI
ChpepAmd64VirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT Context,
    _Outptr_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    PUNWIND_INFO UnwindInfo;
    ULONG_PTR ControlRva, CodeOffset;
    ULONG i, Offset;
    UNWIND_CODE UnwindCode;
    BYTE Reg;
    PULONG LanguageHandler;

    /* Get relative virtual address */
    ControlRva = ControlPc - ImageBase;

    /* Sanity checks */
    if ( (ControlRva < FunctionEntry->BeginAddress) ||
         (ControlRva >= FunctionEntry->EndAddress) )
    {
        return NULL;
    }

    /* Get a pointer to the unwind info */
    UnwindInfo = RVA(ImageBase, FunctionEntry->UnwindData);

    /* The language specific handler data follows the unwind info */
    LanguageHandler = ALIGN_UP_POINTER_BY(&UnwindInfo->UnwindCode[UnwindInfo->CountOfCodes], sizeof(ULONG));

    /* Calculate relative offset to function start */
    CodeOffset = ControlRva - FunctionEntry->BeginAddress;

    *EstablisherFrame = GetEstablisherFrame(Context, UnwindInfo, CodeOffset);

    /* A no-handler stack walk may feed us the terminal frame produced by the
       previous unwind. Do not let it dereference the null stack pointer. */
    if ((HandlerType == UNW_FLAG_NHANDLER) && (Context->Rsp == 0))
    {
        return NULL;
    }

    /* Check if we are in the function epilog and try to finish it */
    if ((CodeOffset > UnwindInfo->SizeOfProlog) && (UnwindInfo->CountOfCodes > 0))
    {
        if (RtlpTryToUnwindEpilog(Context, ControlPc, ContextPointers, ImageBase, FunctionEntry))
        {
            /* There's no exception routine */
            return NULL;
        }
    }

    /* Skip all Ops with an offset greater than the current Offset */
    i = 0;
    while ((i < UnwindInfo->CountOfCodes) &&
           (UnwindInfo->UnwindCode[i].CodeOffset > CodeOffset))
    {
        i += UnwindOpSlots(UnwindInfo->UnwindCode[i]);
    }

RepeatChainedInfo:

    /* Process the remaining unwind ops */
    while (i < UnwindInfo->CountOfCodes)
    {
        /* A frame-register unwind can also produce a terminal stack pointer. */
        if ((HandlerType == UNW_FLAG_NHANDLER) && (Context->Rsp == 0))
        {
            return NULL;
        }

        UnwindCode = UnwindInfo->UnwindCode[i];
        switch (UnwindCode.UnwindOp)
        {
            case UWOP_PUSH_NONVOL:
                Reg = UnwindCode.OpInfo;
                PopReg(Context, ContextPointers, Reg);
                i++;
                break;

            case UWOP_ALLOC_LARGE:
                if (UnwindCode.OpInfo)
                {
                    Offset = *(ULONG*)(&UnwindInfo->UnwindCode[i+1]);
                    Context->Rsp += Offset;
                    i += 3;
                }
                else
                {
                    Offset = UnwindInfo->UnwindCode[i+1].FrameOffset;
                    Context->Rsp += Offset * 8;
                    i += 2;
                }
                break;

            case UWOP_ALLOC_SMALL:
                Context->Rsp += (UnwindCode.OpInfo + 1) * 8;
                i++;
                break;

            case UWOP_SET_FPREG:
                Reg = UnwindInfo->FrameRegister;
                Context->Rsp = GetReg(Context, Reg) - UnwindInfo->FrameOffset * 16;
                i++;
                break;

            case UWOP_SAVE_NONVOL:
                Reg = UnwindCode.OpInfo;
                Offset = UnwindInfo->UnwindCode[i + 1].FrameOffset;
                /* The slot stores offset / 8; adding it to a DWORD64* scales it back to bytes.
                 * See https://github.com/dotnet/runtime/blob/421be955e4b70cddf583b10f5ad99814b713fb87/src/coreclr/unwinder/amd64/unwinder.cpp#L831 */
                SetRegFromStackValue(Context, ContextPointers, Reg, (DWORD64*)Context->Rsp + Offset);
                i += 2;
                break;

            case UWOP_SAVE_NONVOL_FAR:
                Reg = UnwindCode.OpInfo;
                Offset = *(ULONG*)(&UnwindInfo->UnwindCode[i + 1]);
                SetRegFromStackValue(Context, ContextPointers, Reg, (PDWORD64)(Context->Rsp + Offset));
                i += 3;
                break;

            case UWOP_EPILOG:
                i += 2;
                break;

            case UWOP_SPARE_CODE:
                ASSERT(FALSE);
                i += 3;
                break;

            case UWOP_SAVE_XMM128:
                Reg = UnwindCode.OpInfo;
                Offset = UnwindInfo->UnwindCode[i + 1].FrameOffset;
                /* The slot stores offset / 16; adding it to an M128A* scales it back to bytes.
                 * See https://github.com/dotnet/runtime/blob/421be955e4b70cddf583b10f5ad99814b713fb87/src/coreclr/unwinder/amd64/unwinder.cpp#L890 */
                SetXmmRegFromStackValue(Context, ContextPointers, Reg, (M128A*)Context->Rsp + Offset);
                i += 2;
                break;

            case UWOP_SAVE_XMM128_FAR:
                Reg = UnwindCode.OpInfo;
                Offset = *(ULONG*)(&UnwindInfo->UnwindCode[i + 1]);
                SetXmmRegFromStackValue(Context, ContextPointers, Reg, (M128A*)(Context->Rsp + Offset));
                i += 3;
                break;

            case UWOP_PUSH_MACHFRAME:
                /* OpInfo is 1, when an error code was pushed, otherwise 0. */
                Context->Rsp += UnwindCode.OpInfo * sizeof(DWORD64);

                /* Now pop the MACHINE_FRAME (RIP/RSP only. And yes, "magic numbers", deal with it) */
                Context->Rip = *(PDWORD64)(Context->Rsp + 0x00);
                Context->Rsp = *(PDWORD64)(Context->Rsp + 0x18);
                ASSERT((i + 1) == UnwindInfo->CountOfCodes);
                goto Exit;
        }
    }

    /* Check for chained info */
    if (UnwindInfo->Flags & UNW_FLAG_CHAININFO)
    {
        /* See https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-160#chained-unwind-info-structures */
        FunctionEntry = (PRUNTIME_FUNCTION)&(UnwindInfo->UnwindCode[(UnwindInfo->CountOfCodes + 1) & ~1]);
        UnwindInfo = RVA(ImageBase, FunctionEntry->UnwindData);
        i = 0;
        goto RepeatChainedInfo;
    }

    /* Unwind is finished, pop new Rip from Stack */
    if (Context->Rsp != 0)
    {
        Context->Rip = *(DWORD64*)Context->Rsp;
        Context->Rsp += sizeof(DWORD64);
    }

Exit:

    /* Check if we have a handler and return it */
    if (UnwindInfo->Flags & (HandlerType & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)))
    {
        *HandlerData = (LanguageHandler + 1);
        return RVA(ImageBase, *LanguageHandler);
    }

    return NULL;
}
