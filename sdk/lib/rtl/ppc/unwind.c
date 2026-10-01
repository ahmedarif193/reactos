/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC function table lookup and virtual unwind
 *
 * Windows NT PowerPC function table entries carry no unwind codes. The
 * unwinder decodes the prologue from BeginAddress up to PrologEndAddress (or
 * the control PC if it is still inside the prologue) and derives where each
 * nonvolatile register was saved. Every frame keeps the back chain at 0(r1),
 * so once the frame is allocated the caller's stack pointer is *(r1).
 * A control PC inside an epilogue is unwound by executing the remaining
 * epilogue instructions on the context.
 */

#include <rtl.h>
#include "unwind.h"

#define PPC_TOC_RESTORE 0x80410004 /* lwz r2,4(r1) after a cross-image call */

/* Value classes tracked while decoding a prologue. */
enum
{
    ValUnknown,
    ValCfa,        /* entry stack pointer + Offset */
    ValSpAlloc,    /* stack pointer after the frame allocation + Offset */
    ValStackDelta, /* allocated SP - entry SP (possibly dynamically aligned) */
    ValConstant,   /* Offset */
    ValLr,         /* the return address (mflr) */
    ValCr,         /* the condition register (mfcr) */
};

typedef struct _PPC_VALUE
{
    UCHAR Kind;
    LONG Offset;
} PPC_VALUE;

typedef struct _PPC_SLOT
{
    UCHAR Kind;    /* ValCfa or ValSpAlloc */
    LONG Offset;
} PPC_SLOT;

typedef struct _PPC_PROLOGUE
{
    BOOLEAN Allocated;
    BOOLEAN FramePointer;          /* r31 holds the allocated stack pointer */
    PPC_SLOT Gpr[32];
    PPC_SLOT Fpr[32];
    PPC_SLOT Lr;
    PPC_SLOT Cr;
} PPC_PROLOGUE;

static
ULONG
ReadInstruction(ULONG_PTR Address)
{
    return *(volatile ULONG *)Address;
}

static
PRUNTIME_FUNCTION
LookupImageFunctionEntry(
    _In_ ULONG_PTR ControlPc,
    _In_ PVOID ImageBase)
{
    PRUNTIME_FUNCTION Table;
    ULONG Size, Low, High, Middle;

    Table = RtlImageDirectoryEntryToData(ImageBase, TRUE,
                                         IMAGE_DIRECTORY_ENTRY_EXCEPTION, &Size);
    if (!Table || Size < sizeof(RUNTIME_FUNCTION))
        return NULL;

    /* The linker sorts the table by BeginAddress. */
    Low = 0;
    High = Size / sizeof(RUNTIME_FUNCTION);
    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        if (ControlPc < Table[Middle].BeginAddress)
            High = Middle;
        else if (ControlPc >= Table[Middle].EndAddress)
            Low = Middle + 1;
        else
            return &Table[Middle];
    }
    return NULL;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ ULONG_PTR ControlPc)
{
    PVOID Base;

    if (!RtlPcToFileHeader((PVOID)ControlPc, &Base))
        return NULL;
    return LookupImageFunctionEntry(ControlPc, Base);
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Out_ PULONG Length)
{
    PVOID Base;
    PRUNTIME_FUNCTION Table;

    *ImageBase = 0;
    *Length = 0;
    if (!RtlPcToFileHeader((PVOID)ControlPc, &Base))
        return NULL;
    Table = RtlImageDirectoryEntryToData(Base, TRUE,
                                         IMAGE_DIRECTORY_ENTRY_EXCEPTION, Length);
    *ImageBase = (ULONG_PTR)Base;
    return Table;
}

static
VOID
SetValue(PPC_VALUE *Value, UCHAR Kind, LONG Offset)
{
    Value->Kind = Kind;
    Value->Offset = Offset;
}

static
VOID
RecordSlot(PPC_SLOT *Slot, const PPC_VALUE *Base, LONG Displacement)
{
    /* Keep the first save; later stores in the prologue are not saves. */
    if (Slot->Kind != ValUnknown)
        return;
    if (Base->Kind == ValCfa || Base->Kind == ValSpAlloc)
    {
        Slot->Kind = Base->Kind;
        Slot->Offset = Base->Offset + Displacement;
    }
}

/* The general register an instruction writes, or -1. */
static
LONG
WrittenGpr(ULONG Insn)
{
    ULONG Opcode = Insn >> 26;
    ULONG Rd = (Insn >> 21) & 31;
    ULONG Ra = (Insn >> 16) & 31;
    ULONG Xo = (Insn >> 1) & 0x3FF;

    switch (Opcode)
    {
    case 7: case 8: case 12: case 13: case 14: case 15:     /* D-form arithmetic */
    case 32: case 33: case 34: case 35: case 40: case 41:   /* loads */
    case 42: case 43: case 46:
        return Rd;
    case 20: case 21: case 23:                              /* rotates */
    case 24: case 25: case 26: case 27: case 28: case 29:   /* logical immediates */
        return Ra;
    case 31:
        switch (Xo)
        {
        case 28: case 60: case 124: case 284: case 316: case 412: case 444:
        case 476: case 24: case 536: case 792: case 824: case 26: case 922:
        case 954:                                           /* logical and shifts */
            return Ra;
        case 151: case 183: case 215: case 247: case 407: case 439: case 150:
        case 663: case 695: case 727: case 759: case 983: case 144: case 467:
        case 54: case 86: case 278: case 470: case 598: case 854: case 1014:
            return -1;                                      /* stores, mtspr, cache */
        default:
            return Rd;
        }
    default:
        return -1;
    }
}

/* Decode the prologue instructions in [Begin, End). */
static
VOID
DecodePrologue(
    _In_ ULONG_PTR Begin,
    _In_ ULONG_PTR End,
    _Out_ PPC_PROLOGUE *Prologue)
{
    PPC_VALUE Gpr[32];
    ULONG_PTR Address;
    ULONG i;

    RtlZeroMemory(Prologue, sizeof(*Prologue));
    for (i = 0; i < 32; i++)
        SetValue(&Gpr[i], ValUnknown, 0);
    SetValue(&Gpr[1], ValCfa, 0);

    for (Address = Begin; Address < End; Address += 4)
    {
        ULONG Insn = ReadInstruction(Address);
        ULONG Opcode = Insn >> 26;
        ULONG Rd = (Insn >> 21) & 31;
        ULONG Ra = (Insn >> 16) & 31;
        ULONG Rb = (Insn >> 11) & 31;
        LONG Simm = (SHORT)(Insn & 0xFFFF);
        ULONG Xo = (Insn >> 1) & 0x3FF;

        if ((Insn & 0xFC1FFFFF) == 0x7C0802A6)          /* mflr rD */
        {
            SetValue(&Gpr[Rd], ValLr, 0);
        }
        else if ((Insn & 0xFC1FFFFF) == 0x7C000026)     /* mfcr rD */
        {
            SetValue(&Gpr[Rd], ValCr, 0);
        }
        else if (Opcode == 36 || Opcode == 37)          /* stw, stwu */
        {
            PPC_VALUE Stored = Gpr[Rd];
            if (Stored.Kind == ValLr)
                RecordSlot(&Prologue->Lr, &Gpr[Ra], Simm);
            else if (Stored.Kind == ValCr)
                RecordSlot(&Prologue->Cr, &Gpr[Ra], Simm);
            else if (Opcode == 36 && Rd >= 13)
                RecordSlot(&Prologue->Gpr[Rd], &Gpr[Ra], Simm);

            if (Opcode == 37 && Ra == 1)
            {
                /* The first allocation defines the allocated stack pointer.
                 * Later updates (stack probes) move it by an amount only the
                 * back chain records, so the allocated pointer is taken from
                 * the context instead. */
                Prologue->Allocated = TRUE;
                SetValue(&Gpr[1], ValSpAlloc, 0);
            }
            else if (Opcode == 37)
            {
                if (Gpr[Ra].Kind == ValCfa || Gpr[Ra].Kind == ValSpAlloc)
                    Gpr[Ra].Offset += Simm;
                else
                    SetValue(&Gpr[Ra], ValUnknown, 0);
            }
        }
        else if (Opcode == 47)                          /* stmw rS,d(rA) */
        {
            for (i = Rd; i < 32; i++)
                RecordSlot(&Prologue->Gpr[i], &Gpr[Ra], Simm + (LONG)(i - Rd) * 4);
        }
        else if (Opcode == 54 || Opcode == 55)          /* stfd, stfdu */
        {
            RecordSlot(&Prologue->Fpr[Rd], &Gpr[Ra], Simm);
        }
        else if (Opcode == 31 && Xo == 183)             /* stwux rS,rA,rB */
        {
            if (Ra == 1)
            {
                Prologue->Allocated = TRUE;
                SetValue(&Gpr[1], ValSpAlloc, 0);
                if (Rb != 1)
                    SetValue(&Gpr[Rb], ValStackDelta, 0);
            }
            else
            {
                SetValue(&Gpr[Ra], ValUnknown, 0);
            }
        }
        else if (Opcode == 31 && Xo == 151)             /* stwx rS,rA,rB */
        {
            PPC_VALUE Base = Gpr[Rb];
            if (Ra != 0)
            {
                if (Gpr[Ra].Kind == ValConstant)
                    Base.Offset += Gpr[Ra].Offset;
                else if (Base.Kind == ValConstant)
                {
                    Base.Offset += Gpr[Ra].Offset;
                    Base.Kind = Gpr[Ra].Kind;
                }
                else
                    Base.Kind = ValUnknown;
            }
            if (Gpr[Rd].Kind == ValLr)
                RecordSlot(&Prologue->Lr, &Base, 0);
            else if (Gpr[Rd].Kind == ValCr)
                RecordSlot(&Prologue->Cr, &Base, 0);
            else if (Rd >= 13)
                RecordSlot(&Prologue->Gpr[Rd], &Base, 0);
        }
        else if (Opcode == 31 && (Xo == 40 || Xo == 8)) /* subf, subfc */
        {
            /* Realigned prologues recover the incoming SP by subtracting
             * the indexed allocation delta from the allocated SP. */
            if (Gpr[Rb].Kind == ValSpAlloc && Gpr[Ra].Kind == ValStackDelta)
                SetValue(&Gpr[Rd], ValCfa, Gpr[Rb].Offset);
            else if (Gpr[Ra].Kind == ValConstant &&
                     (Gpr[Rb].Kind == ValCfa || Gpr[Rb].Kind == ValSpAlloc ||
                      Gpr[Rb].Kind == ValConstant))
                SetValue(&Gpr[Rd], Gpr[Rb].Kind, Gpr[Rb].Offset - Gpr[Ra].Offset);
            else
                SetValue(&Gpr[Rd], ValUnknown, 0);
        }
        else if (Opcode == 31 && Xo == 444 && Rd == Rb)   /* mr rA,rS */
        {
            Gpr[Ra] = Gpr[Rd];
            if (Ra == 31 && Gpr[Rd].Kind == ValSpAlloc && Gpr[Rd].Offset == 0)
                Prologue->FramePointer = TRUE;
        }
        else if (Opcode == 14 || Opcode == 12 || Opcode == 13) /* addi, addic[.] */
        {
            /* Only addi treats r0 as zero; addic uses its register value. */
            if (Ra == 0 && Opcode == 14)
                SetValue(&Gpr[Rd], ValConstant, Simm);
            else if (Gpr[Ra].Kind == ValCfa || Gpr[Ra].Kind == ValSpAlloc ||
                     Gpr[Ra].Kind == ValConstant)
                SetValue(&Gpr[Rd], Gpr[Ra].Kind, Gpr[Ra].Offset + Simm);
            else
                SetValue(&Gpr[Rd], ValUnknown, 0);
        }
        else if (Opcode == 15)                          /* addis rD,rA,SIMM */
        {
            if (Ra == 0)
                SetValue(&Gpr[Rd], ValConstant, Simm << 16);
            else if (Gpr[Ra].Kind == ValConstant)
                SetValue(&Gpr[Rd], ValConstant, Gpr[Ra].Offset + (Simm << 16));
            else
                SetValue(&Gpr[Rd], ValUnknown, 0);
        }
        else if (Opcode == 24)                          /* ori rA,rS,UIMM */
        {
            if (Gpr[Rd].Kind == ValConstant)
                SetValue(&Gpr[Ra], ValConstant, Gpr[Rd].Offset | (Insn & 0xFFFF));
            else
                SetValue(&Gpr[Ra], ValUnknown, 0);
        }
        else
        {
            /* Any other instruction that writes a general register makes
             * that register untracked. Branches in stack-probe loops write
             * none; their effect on r1 is covered by the allocation rules. */
            LONG Destination = WrittenGpr(Insn);
            if (Destination >= 0 && Destination != 1)
                SetValue(&Gpr[Destination], ValUnknown, 0);
        }
    }
}

/* Return TRUE and execute the epilogue if ControlPc is inside one. */
static
BOOLEAN
UnwindEpilogue(
    _In_ ULONG_PTR ControlPc,
    _In_ ULONG_PTR EndAddress,
    _Inout_ PCONTEXT Context,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    ULONG_PTR Address;
    CONTEXT Work = *Context;
    PULONG Gpr = &Work.Gpr0;
    double *Fpr = &Work.Fpr0;
    ULONG Count;

    /* First pass: check that everything up to blr is epilogue code. */
    for (Address = ControlPc, Count = 0; Address < EndAddress && Count < 64; Address += 4, Count++)
    {
        ULONG Insn = ReadInstruction(Address);
        ULONG Opcode = Insn >> 26;
        ULONG Xo = (Insn >> 1) & 0x3FF;

        if (Insn == 0x4E800020)                         /* blr */
            break;
        if (Opcode == 32 || Opcode == 50 || Opcode == 46 || Opcode == 14)
            continue;                                   /* lwz, lfd, lmw, addi */
        if ((Insn & 0xFC1FFFFF) == 0x7C0803A6)          /* mtlr */
            continue;
        if ((Insn & 0xFC100FFF) == 0x7C000120)          /* mtcrf */
            continue;
        if (Opcode == 31 && Xo == 444 && ((Insn >> 21) & 31) == ((Insn >> 11) & 31))
            continue;                                   /* mr */
        return FALSE;
    }
    if (Address >= EndAddress || ReadInstruction(Address) != 0x4E800020)
        return FALSE;

    /* Second pass: execute the epilogue on a copy of the context. */
    for (Address = ControlPc; ReadInstruction(Address) != 0x4E800020; Address += 4)
    {
        ULONG Insn = ReadInstruction(Address);
        ULONG Opcode = Insn >> 26;
        ULONG Rd = (Insn >> 21) & 31;
        ULONG Ra = (Insn >> 16) & 31;
        LONG Simm = (SHORT)(Insn & 0xFFFF);
        ULONG_PTR Effective = (Ra ? Gpr[Ra] : 0) + Simm;
        ULONG i;

        if (Opcode == 32)
        {
            if (ContextPointers && Rd >= 14)
                (&ContextPointers->IntegerContext14)[Rd - 14] = (PULONG)Effective;
            Gpr[Rd] = *(PULONG)Effective;
        }
        else if (Opcode == 50)
        {
            if (ContextPointers && Rd >= 14)
                (&ContextPointers->FloatingContext14)[Rd - 14] = (PULONGLONG)Effective;
            Fpr[Rd] = *(double *)Effective;
        }
        else if (Opcode == 46)
        {
            for (i = Rd; i < 32; i++, Effective += 4)
            {
                if (ContextPointers && i >= 14)
                    (&ContextPointers->IntegerContext14)[i - 14] = (PULONG)Effective;
                Gpr[i] = *(PULONG)Effective;
            }
        }
        else if (Opcode == 14)
        {
            Gpr[Rd] = (Ra ? Gpr[Ra] : 0) + Simm;
        }
        else if ((Insn & 0xFC1FFFFF) == 0x7C0803A6)
        {
            Work.Lr = Gpr[Rd];
        }
        else if ((Insn & 0xFC100FFF) == 0x7C000120)
        {
            ULONG Mask = (Insn >> 12) & 0xFF, Field, Bits = 0;
            for (Field = 0; Field < 8; Field++)
                if (Mask & (0x80 >> Field))
                    Bits |= 0xFUL << (28 - 4 * Field);
            Work.Cr = (Work.Cr & ~Bits) | (Gpr[Rd] & Bits);
        }
        else
        {
            Gpr[Ra] = Gpr[Rd];                          /* mr */
        }
    }

    Work.Iar = Work.Lr;
    *Context = Work;
    return TRUE;
}

static
ULONG_PTR
SlotAddress(const PPC_SLOT *Slot, ULONG_PTR Cfa, ULONG_PTR SpAlloc)
{
    return (Slot->Kind == ValCfa ? Cfa : SpAlloc) + Slot->Offset;
}

PEXCEPTION_ROUTINE
NTAPI
RtlpPpcVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG_PTR ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT Context,
    _Out_opt_ PVOID *HandlerData,
    _Out_ PULONG_PTR EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _Out_opt_ PBOOLEAN InFunction,
    _Out_opt_ PBOOLEAN FaultFrame,
    _Out_opt_ PBOOLEAN Epilogue,
    _In_ ULONG LowStackLimit,
    _In_ ULONG HighStackLimit)
{
    PPC_PROLOGUE Prologue;
    ULONG_PTR PrologEnd, Cfa, SpAlloc, ReturnAddress;
    PEXCEPTION_ROUTINE Handler = NULL;
    ULONG i;

    if (HandlerData)
        *HandlerData = NULL;
    if (InFunction)
        *InFunction = FALSE;
    if (FaultFrame)
        *FaultFrame = FALSE;
    if (Epilogue)
        *Epilogue = FALSE;

    /* A kernel-built exception frame restores its saved context. */
    if (NT_SUCCESS(RtlpPpcUnwindSpecialFrame(ControlPc, Context)))
    {
        *EstablisherFrame = Context->Gpr1;
        if (FaultFrame)
            *FaultFrame = TRUE;
        return NULL;
    }

    /* Leaf function without a table entry: the return address is in LR. */
    if (!FunctionEntry)
    {
        *EstablisherFrame = Context->Gpr1;
        Context->Iar = Context->Lr;
        Context->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
        return NULL;
    }

    PrologEnd = FunctionEntry->PrologEndAddress & ~3UL;
    if (PrologEnd < FunctionEntry->BeginAddress || PrologEnd > FunctionEntry->EndAddress)
        PrologEnd = FunctionEntry->BeginAddress;

    if (ControlPc >= PrologEnd &&
        UnwindEpilogue(ControlPc, FunctionEntry->EndAddress, Context, ContextPointers))
    {
        if (Epilogue)
            *Epilogue = TRUE;
        *EstablisherFrame = Context->Gpr1;
        Context->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
        return NULL;
    }

    DecodePrologue(FunctionEntry->BeginAddress, min(ControlPc, PrologEnd), &Prologue);

    if (Prologue.Allocated)
    {
        SpAlloc = Prologue.FramePointer ? Context->Gpr31 : Context->Gpr1;
        Cfa = *(PULONG)Context->Gpr1;                   /* back chain */
    }
    else
    {
        SpAlloc = Context->Gpr1;
        Cfa = Context->Gpr1;
    }

    /* The body of the function is covered by its language handler. */
    if (ControlPc >= PrologEnd && FunctionEntry->ExceptionHandler &&
        (HandlerType & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)))
    {
        Handler = FunctionEntry->ExceptionHandler;
        if (HandlerData)
            *HandlerData = FunctionEntry->HandlerData;
    }

    if (InFunction && ControlPc >= PrologEnd)
        *InFunction = TRUE;

    for (i = 14; i < 32; i++)
    {
        if (Prologue.Gpr[i].Kind != ValUnknown)
        {
            PULONG Slot = (PULONG)SlotAddress(&Prologue.Gpr[i], Cfa, SpAlloc);
            (&Context->Gpr0)[i] = *Slot;
            if (ContextPointers)
                (&ContextPointers->IntegerContext14)[i - 14] = Slot;
        }
        if (Prologue.Fpr[i].Kind != ValUnknown)
        {
            double *Slot = (double *)SlotAddress(&Prologue.Fpr[i], Cfa, SpAlloc);
            (&Context->Fpr0)[i] = *Slot;
            if (ContextPointers)
                (&ContextPointers->FloatingContext14)[i - 14] = (PULONGLONG)Slot;
        }
    }
    if (Prologue.Cr.Kind != ValUnknown)
        Context->Cr = *(PULONG)SlotAddress(&Prologue.Cr, Cfa, SpAlloc);

    if (Prologue.Lr.Kind != ValUnknown)
        ReturnAddress = *(PULONG)SlotAddress(&Prologue.Lr, Cfa, SpAlloc);
    else
        ReturnAddress = Context->Lr;

    *EstablisherFrame = Cfa;
    Context->Gpr1 = Cfa;
    Context->Iar = ReturnAddress;
    Context->Lr = ReturnAddress;

    /* A call that may have crossed images restores the caller's TOC from
     * 4(r1) right after the return; the glue saved it there. */
    if (ReturnAddress && Cfa >= LowStackLimit && Cfa <= HighStackLimit &&
        !(Cfa & 7) && ReadInstruction(ReturnAddress) == PPC_TOC_RESTORE)
        Context->Gpr2 = *(PULONG)(Cfa + 4);

    Context->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    return Handler;
}

/* Windows NT PPC returns the caller's control PC and leaves Iar unchanged.
 * The private decoder instead advances Iar for the ReactOS frame walker. */
ULONG
NTAPI
RtlVirtualUnwind(
    _In_ ULONG_PTR ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT Context,
    _Out_ PBOOLEAN InFunction,
    _Out_ PULONG EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ ULONG LowStackLimit,
    _In_ ULONG HighStackLimit)
{
    ULONG_PTR Frame;
    ULONG OldIar, OldFlags, NextPc;
    BOOLEAN FaultFrame, Epilogue;

    if (!Context)
        return 0;

    /* The NT PPC leaf path reads LR without writing the two frame outputs. */
    if (!FunctionEntry)
        return Context->Lr - sizeof(ULONG);

    if (!InFunction || !EstablisherFrame)
        return 0;

    OldIar = Context->Iar;
    OldFlags = Context->ContextFlags;
    RtlpPpcVirtualUnwind(UNW_FLAG_NHANDLER, ControlPc, FunctionEntry,
                          Context, NULL, &Frame, ContextPointers,
                          InFunction, &FaultFrame, &Epilogue,
                          LowStackLimit, HighStackLimit);
    *EstablisherFrame = (ULONG)Frame;
    NextPc = Context->Iar;
    Context->Iar = OldIar;
    Context->ContextFlags = OldFlags;

    return FaultFrame || Epilogue || !NextPc ? NextPc : NextPc - sizeof(ULONG);
}
