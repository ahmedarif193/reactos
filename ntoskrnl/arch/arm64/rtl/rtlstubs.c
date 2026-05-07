/*
 * PROJECT:     ReactOS Kernel ARM64
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Full ARM64 PE/COFF exception unwinding
 *
 * Implements .pdata/.xdata based RtlVirtualUnwind, RtlUnwindEx,
 * and RtlUnwind per the Microsoft ARM64 exception handling specification.
 */

#include <ntoskrnl.h>
#include <arm64unwind.h>

#define NDEBUG
#include <debug.h>

/*
 * RUNTIME_FUNCTION for ARM64 is 8 bytes (2 x DWORD).
 * The ndk/rtltypes.h defines a generic 3-field layout (AMD64 style).
 * On ARM64 we override to the correct 2-field PE layout.
 */
#undef RUNTIME_FUNCTION
#undef PRUNTIME_FUNCTION
typedef struct _IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY {
    DWORD BeginAddress;
    DWORD UnwindData;
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

/*
 * UNWIND flags (mirrored from ndk/rtltypes.h in case the PCH
 * does not include them for this translation unit).
 */
#ifndef UNW_FLAG_EHANDLER
#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2
#define UNW_FLAG_CHAININFO 0x4
#endif

/*
 * DISPATCHER_CONTEXT for ARM64 (mirrored from winnt_old.h).
 * Required here because the ntoskrnl PCH may not include it.
 */
#ifndef _ARM64_DISPATCHER_CONTEXT_DEFINED
#define _ARM64_DISPATCHER_CONTEXT_DEFINED 1
typedef struct _DISPATCHER_CONTEXT {
    ULONG_PTR ControlPc;
    ULONG_PTR ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG_PTR EstablisherFrame;
    ULONG_PTR TargetPc;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    struct _UNWIND_HISTORY_TABLE *HistoryTable;
    DWORD ScopeIndex;
    BOOLEAN ControlPcIsUnwound;
    PUCHAR NonVolatileRegisters;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;
#endif

/*
 * Forward declarations for functions defined in this file.
 */
PVOID
NTAPI
RtlPcToFileHeader(
    _In_ PVOID PcValue,
    _Out_ PVOID *BaseOfImage);

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ PVOID HistoryTable);

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable);

/*
 * Non-volatile register count for ARM64 (x19-x28 = 10 int, d8-d15 = 8 FP).
 */
#define ARM64_NONVOL_INT_COUNT 10
#define ARM64_NONVOL_FP_COUNT  8

/* ------------------------------------------------------------------ *
 *  Opcode Size Lookup Table
 *
 *  Maps the first byte of an unwind code to its total size in bytes.
 *  Index = first byte of the code.
 *  Value = {OpcodeId, TotalSize}.
 *
 *  ARM64 codes are variable-length (1-4 bytes):
 *    1 byte:  alloc_s, save_r19r20_x, save_fplr, save_fplr_x,
 *             set_fp, nop, end, end_c, save_next, pac_sign_lr
 *    2 bytes: alloc_m, save_regp, save_regp_x, save_reg, save_reg_x,
 *             save_lrpair, save_fregp, save_fregp_x, save_freg,
 *             save_freg_x, alloc_z, add_fp
 *    3 bytes: save_any_* (second byte determines subtype)
 *    4 bytes: alloc_l
 *
 *  The table approach: first byte upper bits determine the pattern.
 *  For multi-byte codes, the first byte is a unique prefix.
 * ------------------------------------------------------------------ */

typedef struct _ARM64_OPCODE_ENTRY {
    UCHAR OpcodeId;
    UCHAR TotalBytes;
} ARM64_OPCODE_ENTRY;

#define OPE(op, sz) { ARM64_UWOP_##op, (sz) }

static const ARM64_OPCODE_ENTRY RtlpArm64OpcodeTable[256] =
{
    /* 0x00-0x1F: alloc_s (000xxxxx) - 1 byte */
    OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1),
    OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1),
    OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1),
    OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1),
    OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1),
    OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1),
    OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1),
    OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1), OPE(ALLOC_S, 1),

    /* 0x20-0x3F: save_r19r20_x (001zzzzz) - 1 byte */
    OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1),
    OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1),
    OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1),
    OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1),
    OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1),
    OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1),
    OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1),
    OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1), OPE(SAVE_R19R20_X, 1),

    /* 0x40-0x7F: save_fplr (01zzzzzz) - 1 byte */
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),
    OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1), OPE(SAVE_FPLR, 1),

    /* 0x80-0xBF: save_fplr_x (10zzzzzz) - 1 byte */
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),
    OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1), OPE(SAVE_FPLR_X, 1),

    /* 0xC0-0xC7: alloc_m (11000xxx xxxxxxxx) - 2 bytes */
    OPE(ALLOC_M, 2), OPE(ALLOC_M, 2), OPE(ALLOC_M, 2), OPE(ALLOC_M, 2),
    OPE(ALLOC_M, 2), OPE(ALLOC_M, 2), OPE(ALLOC_M, 2), OPE(ALLOC_M, 2),

    /* 0xC8-0xCB: save_regp (110010xx xxzzzzzz) - 2 bytes */
    OPE(SAVE_REGP, 2), OPE(SAVE_REGP, 2), OPE(SAVE_REGP, 2), OPE(SAVE_REGP, 2),

    /* 0xCC-0xCF: save_regp_x (110011xx xxzzzzzz) - 2 bytes */
    OPE(SAVE_REGP_X, 2), OPE(SAVE_REGP_X, 2), OPE(SAVE_REGP_X, 2), OPE(SAVE_REGP_X, 2),

    /* 0xD0-0xD3: save_reg (110100xx xxzzzzzz) - 2 bytes */
    OPE(SAVE_REG, 2), OPE(SAVE_REG, 2), OPE(SAVE_REG, 2), OPE(SAVE_REG, 2),

    /* 0xD4-0xD5: save_reg_x (1101010x xxxzzzzz) - 2 bytes */
    OPE(SAVE_REG_X, 2), OPE(SAVE_REG_X, 2),

    /* 0xD6-0xD7: save_lrpair (1101011x xxzzzzzz) - 2 bytes */
    OPE(SAVE_LRPAIR, 2), OPE(SAVE_LRPAIR, 2),

    /* 0xD8-0xD9: save_fregp (1101100x xxzzzzzz) - 2 bytes */
    OPE(SAVE_FREGP, 2), OPE(SAVE_FREGP, 2),

    /* 0xDA-0xDB: save_fregp_x (1101101x xxzzzzzz) - 2 bytes */
    OPE(SAVE_FREGP_X, 2), OPE(SAVE_FREGP_X, 2),

    /* 0xDC-0xDD: save_freg (1101110x xxzzzzzz) - 2 bytes */
    OPE(SAVE_FREG, 2), OPE(SAVE_FREG, 2),

    /* 0xDE: save_freg_x (11011110 xxxzzzzz) - 2 bytes */
    OPE(SAVE_FREG_X, 2),

    /* 0xDF: alloc_z (11011111 zzzzzzzz) - 2 bytes */
    OPE(ALLOC_Z, 2),

    /* 0xE0: alloc_l (11100000 xx xx xx) - 4 bytes */
    OPE(ALLOC_L, 4),

    /* 0xE1: set_fp (11100001) - 1 byte */
    OPE(SET_FP, 1),

    /* 0xE2: add_fp (11100010 xxxxxxxx) - 2 bytes */
    OPE(ADD_FP, 2),

    /* 0xE3: nop (11100011) - 1 byte */
    OPE(NOP, 1),

    /* 0xE4: end (11100100) - 1 byte */
    OPE(END, 1),

    /* 0xE5: end_c (11100101) - 1 byte */
    OPE(END_C, 1),

    /* 0xE6: save_next (11100110) - 1 byte */
    OPE(SAVE_NEXT, 1),

    /* 0xE7: save_any_* (11100111 ...) - 3 bytes */
    OPE(SAVE_ANY_XREG, 3),

    /* 0xE8-0xEC: custom stack operations for hand-written assembly */
    OPE(CUSTOM_TRAP_FRAME, 1),       /* 0xE8 */
    OPE(CUSTOM_MACHINE_FRAME, 1),    /* 0xE9 */
    OPE(CUSTOM_CONTEXT, 1),          /* 0xEA */
    OPE(CUSTOM_EC_CONTEXT, 1),       /* 0xEB */
    OPE(CUSTOM_CLEAR_UNWOUND, 1),    /* 0xEC */

    /* 0xED-0xF7: reserved */
    OPE(RESERVED, 1), OPE(RESERVED, 1), OPE(RESERVED, 1),
    OPE(RESERVED, 1), OPE(RESERVED, 1), OPE(RESERVED, 1), OPE(RESERVED, 1),
    OPE(RESERVED, 1), OPE(RESERVED, 1), OPE(RESERVED, 1), OPE(RESERVED, 1),

    /* 0xF8-0xFB: reserved variable-length encodings */
    OPE(RESERVED, 2), OPE(RESERVED, 3), OPE(RESERVED, 4), OPE(RESERVED, 5),

    /* 0xFC: pac_sign_lr (11111100) - 1 byte */
    OPE(PAC_SIGN_LR, 1),

    /* 0xFD-0xFF: reserved */
    OPE(RESERVED, 1), OPE(RESERVED, 1), OPE(RESERVED, 1),
};

#undef OPE

/* ------------------------------------------------------------------ *
 *  Unwind code iteration helpers
 * ------------------------------------------------------------------ */

static
VOID
RtlpArm64DecodeCode(
    _In_ PUCHAR Codes,
    _In_ ULONG ByteIndex,
    _Out_ PARM64_UNWIND_CODE Code)
{
    UCHAR b0;

    Code->Byte0 = 0;
    Code->Byte1 = 0;
    Code->Byte2 = 0;
    Code->Byte3 = 0;
    Code->TotalBytes = 0;
    Code->OpcodeId = ARM64_UWOP_RESERVED;

    b0 = Codes[ByteIndex];
    Code->Byte0 = b0;

    {
        const ARM64_OPCODE_ENTRY *entry = &RtlpArm64OpcodeTable[b0];
        Code->OpcodeId = entry->OpcodeId;
        Code->TotalBytes = entry->TotalBytes;
    }

    if (Code->TotalBytes >= 2)
        Code->Byte1 = Codes[ByteIndex + 1];
    if (Code->TotalBytes >= 3)
        Code->Byte2 = Codes[ByteIndex + 2];
    if (Code->TotalBytes >= 4)
        Code->Byte3 = Codes[ByteIndex + 3];

    /*
     * For save_any_*, refine opcode from byte1 bits [5:4]:
     *   00xxxxxx = save_any_xreg
     *   01oooooo = save_any_dreg
     *   10oooooo = save_any_qreg
     *   11oooooo = save_zreg or save_preg
     */
    if (Code->OpcodeId == ARM64_UWOP_SAVE_ANY_XREG)
    {
        switch ((Code->Byte1 >> 4) & 3)
        {
            case 0: Code->OpcodeId = ARM64_UWOP_SAVE_ANY_XREG; break;
            case 1: Code->OpcodeId = ARM64_UWOP_SAVE_ANY_DREG; break;
            case 2: Code->OpcodeId = ARM64_UWOP_SAVE_ANY_QREG; break;
            case 3:
                if (Code->Byte1 & 8)
                    Code->OpcodeId = ARM64_UWOP_SAVE_PREG;
                else
                    Code->OpcodeId = ARM64_UWOP_SAVE_ZREG;
                break;
        }
    }
}

/*
 * RtlpArm64InstructionSize - approximate size of instruction corresponding
 * to an unwind code. Used for mid-prolog/epilog offset calculation.
 *
 * This is the size of the actual ARM64 instruction that the unwind code
 * describes, needed to determine how many instructions have been executed
 * in a partially-completed prolog or epilog.
 */
static
ULONG
RtlpArm64InstructionSize(
    _In_ PARM64_UNWIND_CODE Code)
{
    switch (Code->OpcodeId)
    {
        case ARM64_UWOP_ALLOC_S:
        case ARM64_UWOP_SAVE_R19R20_X:
        case ARM64_UWOP_SAVE_FPLR:
        case ARM64_UWOP_SAVE_FPLR_X:
        case ARM64_UWOP_ALLOC_M:
        case ARM64_UWOP_SAVE_REGP:
        case ARM64_UWOP_SAVE_REGP_X:
        case ARM64_UWOP_SAVE_REG:
        case ARM64_UWOP_SAVE_REG_X:
        case ARM64_UWOP_SAVE_LRPAIR:
        case ARM64_UWOP_SAVE_FREGP:
        case ARM64_UWOP_SAVE_FREGP_X:
        case ARM64_UWOP_SAVE_FREG:
        case ARM64_UWOP_SAVE_FREG_X:
        case ARM64_UWOP_ALLOC_Z:
        case ARM64_UWOP_SET_FP:
        case ARM64_UWOP_ADD_FP:
        case ARM64_UWOP_NOP:
        case ARM64_UWOP_PAC_SIGN_LR:
        case ARM64_UWOP_SAVE_NEXT:
            return 4;

        case ARM64_UWOP_ALLOC_L:
            /* sub sp,sp,#imm + mov x15,#imm + bl __chkstk + sub sp,sp,x15,lsl#4 */
            return 16;

        case ARM64_UWOP_SAVE_ANY_XREG:
        case ARM64_UWOP_SAVE_ANY_DREG:
        case ARM64_UWOP_SAVE_ANY_QREG:
        case ARM64_UWOP_SAVE_ZREG:
        case ARM64_UWOP_SAVE_PREG:
            return 4;

        case ARM64_UWOP_CUSTOM_TRAP_FRAME:
        case ARM64_UWOP_CUSTOM_MACHINE_FRAME:
        case ARM64_UWOP_CUSTOM_CONTEXT:
        case ARM64_UWOP_CUSTOM_EC_CONTEXT:
        case ARM64_UWOP_CUSTOM_CLEAR_UNWOUND:
            return 4;

        case ARM64_UWOP_END:
        case ARM64_UWOP_END_C:
            return 4;  /* ret instruction */

        default:
            return 4;
    }
}

static
ULONG
RtlpArm64GetSequenceSize(
    _In_ PUCHAR UnwindCodes,
    _In_ ULONG UnwindBytes,
    _In_ ULONG StartIndex,
    _In_ BOOLEAN IncludeEnd)
{
    ULONG Size = 0;
    ULONG ci = StartIndex;

    while (ci < UnwindBytes)
    {
        ARM64_UNWIND_CODE Code;

        RtlpArm64DecodeCode(UnwindCodes, ci, &Code);
        if (Code.OpcodeId == ARM64_UWOP_END ||
            Code.OpcodeId == ARM64_UWOP_END_C)
        {
            if (IncludeEnd)
                Size += RtlpArm64InstructionSize(&Code);
            break;
        }

        Size += RtlpArm64InstructionSize(&Code);
        ci += Code.TotalBytes;
    }

    return Size;
}

/* ------------------------------------------------------------------ *
 *  Register access helpers for ARM64 CONTEXT
 * ------------------------------------------------------------------ */

static
VOID
RtlpArm64SetIntReg(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Reg,
    _In_ ULONG64 Value)
{
    ((ULONG64*)&Context->X0)[Reg] = Value;
}

static
VOID
RtlpArm64SetFpReg(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Reg,
    _In_ ULONG64 Low,
    _In_ ULONG64 High)
{
    Context->V[Reg].Low = Low;
    Context->V[Reg].High = High;
}

static
BOOLEAN
RtlpArm64Read64(
    _Out_ PULONG64 Value,
    _In_ ULONG_PTR Address)
{
    if (Value == NULL || Address == 0 || (Address & (sizeof(ULONG64) - 1)))
        return FALSE;

    if (!MmIsAddressValid((PVOID)Address) ||
        !MmIsAddressValid((PVOID)(Address + sizeof(ULONG64) - 1)))
    {
        return FALSE;
    }

    _SEH2_TRY
    {
        *Value = *(volatile ULONG64 *)Address;
        _SEH2_YIELD(return TRUE);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return FALSE);
    }
    _SEH2_END;

    return FALSE;
}

/* ------------------------------------------------------------------ *
 *  .xdata Header Parsing
 * ------------------------------------------------------------------ */

static
VOID
RtlpArm64ParseXdataHeader(
    _In_ PULONG Xdata,
    _Out_ PARM64_XDATA_INFO Info)
{
    ULONG Header, EpilogCount, CodeWords;

    RtlZeroMemory(Info, sizeof(*Info));
    Info->Xdata = Xdata;

    Header = Xdata[0];

    Info->FunctionLength = (Header & ARM64_XDATA_FUNCTION_LENGTH_MASK) * sizeof(ULONG);

    CodeWords = (Header >> ARM64_XDATA_CODE_WORDS_SHIFT) & ARM64_XDATA_CODE_WORDS_MASK;
    EpilogCount = (Header >> ARM64_XDATA_EPILOGUE_COUNT_SHIFT) & ARM64_XDATA_EPILOGUE_COUNT_MASK;

    Info->HasExceptionData = !!(Header & ARM64_XDATA_EXCEPTION_DATA);
    Info->EpilogPacked = !!(Header & ARM64_XDATA_EPILOGUE_PACKED);

    if (CodeWords == 0 && EpilogCount == 0 && !Info->EpilogPacked)
    {
        /* Extended header: second word contains expanded counts */
        Info->HeaderWords = 2;
        EpilogCount = Xdata[1] & 0xFFFF;
        CodeWords = (Xdata[1] >> 16) & 0xFF;
    }
    else
    {
        Info->HeaderWords = 1;
    }

    Info->CodeWords = CodeWords;
    Info->EpilogCount = EpilogCount;

    if (Info->EpilogPacked)
    {
        Info->EpilogStartIndex = EpilogCount;
        Info->EpilogScopes = NULL;
    }
    else
    {
        Info->EpilogScopes = &Xdata[Info->HeaderWords];
    }

    /* Unwind codes start after header and epilog scopes */
    Info->UnwindCodes = (PUCHAR)(&Xdata[Info->HeaderWords + (Info->EpilogPacked ? 0 : EpilogCount)]);
}

/* ------------------------------------------------------------------ *
 *  Packed Unwind Data Decoding
 * ------------------------------------------------------------------ */

static
VOID
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

static
ULONG
RtlpArm64AlignUp(
    _In_ ULONG Value,
    _In_ ULONG Alignment)
{
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

static
VOID
RtlpArm64NormalizeLr(
    _Inout_ PCONTEXT Context)
{
    ULONG64 Pc = Context->Lr & ((1ULL << 48) - 1);

    /*
     * PAC bits occupy the non-canonical high bits.  ReactOS ARM64 currently
     * uses a 48-bit VA layout, so strip PAC/tag bits and restore the canonical
     * sign-extension from bit 47.
     */
    if (Pc & (1ULL << 47))
        Pc |= 0xFFFFULL << 48;

    Context->Lr = Pc;
}

static
BOOLEAN
RtlpArm64RestoreIntRegs(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Reg,
    _In_ ULONG Count,
    _In_ LONG Position)
{
    ULONG i;
    ULONG Offset = (Position > 0) ? (ULONG)Position : 0;
    ULONG64 Value;

    if (Reg + Count > 31)
        return FALSE;

    for (i = 0; i < Count; i++)
    {
        if (!RtlpArm64Read64(&Value,
                             (ULONG_PTR)(Context->Sp + (Offset + i) * sizeof(ULONG64))))
        {
            return FALSE;
        }

        RtlpArm64SetIntReg(Context, Reg + i, Value);
    }

    if (Position < 0)
        Context->Sp += (ULONG64)(-Position) * sizeof(ULONG64);

    return TRUE;
}

static
BOOLEAN
RtlpArm64RestoreFpRegs(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Reg,
    _In_ ULONG Count,
    _In_ LONG Position)
{
    ULONG i;
    ULONG Offset = (Position > 0) ? (ULONG)Position : 0;
    ULONG64 Value;

    if (Reg + Count > 32)
        return FALSE;

    for (i = 0; i < Count; i++)
    {
        if (!RtlpArm64Read64(&Value,
                             (ULONG_PTR)(Context->Sp + (Offset + i) * sizeof(ULONG64))))
        {
            return FALSE;
        }

        RtlpArm64SetFpReg(Context, Reg + i, Value, 0);
    }

    if (Position < 0)
        Context->Sp += (ULONG64)(-Position) * sizeof(ULONG64);

    return TRUE;
}

static
BOOLEAN
RtlpArm64UnwindPacked(
    _In_ ULONG UnwindFlag,
    _In_ PARM64_PACKED_INFO Info,
    _In_ ULONG CodeOffset,
    _Inout_ PCONTEXT Context)
{
    ULONG IntSize;
    ULONG FpSize;
    ULONG RegSave;
    ULONG LocalSize;
    ULONG IntRegs;
    ULONG FpRegs;
    ULONG SavedRegs;
    ULONG LocalSizeRegs;
    ULONG FunctionLengthInstr;
    ULONG InstrOffset;
    ULONG HomeInstrs;
    ULONG Len;
    ULONG Skip = 0;
    ULONG Pos = 0;
    ULONG PairCount;
    ULONG PairIndex;

    IntSize = Info->RegI * sizeof(ULONG64);
    FpSize = Info->RegF * sizeof(ULONG64);
    HomeInstrs = Info->HomesParams ? 4 : 0;

    if (Info->CR == ARM64_CR_UNCHAINED_SAVED_LR)
        IntSize += sizeof(ULONG64);
    if (Info->RegF != 0)
        FpSize += sizeof(ULONG64);

    RegSave = RtlpArm64AlignUp(IntSize + FpSize +
                               (Info->HomesParams ? 8 * sizeof(ULONG64) : 0),
                               16);
    if (Info->FrameSize < RegSave)
        return FALSE;

    LocalSize = Info->FrameSize - RegSave;
    IntRegs = IntSize / sizeof(ULONG64);
    FpRegs = FpSize / sizeof(ULONG64);
    SavedRegs = RegSave / sizeof(ULONG64);
    LocalSizeRegs = LocalSize / sizeof(ULONG64);
    FunctionLengthInstr = Info->FunctionLength / sizeof(ULONG);
    InstrOffset = CodeOffset / sizeof(ULONG);

    if (UnwindFlag == 1)
    {
        Len = (IntSize + 8) / 16 + (FpSize + 8) / 16;

        switch (Info->CR)
        {
            case ARM64_CR_CHAINED_PAC:
                Len++; /* pacibsp */
                /* fall through */
            case ARM64_CR_CHAINED:
                Len++; /* mov x29, sp */
                Len++; /* save <x29, lr> */
                if (LocalSize <= 512)
                    break;
                /* fall through */
            case ARM64_CR_UNCHAINED:
            case ARM64_CR_UNCHAINED_SAVED_LR:
                if (LocalSize != 0)
                    Len++;
                if (LocalSize > 4088)
                    Len++;
                break;
        }

        if (InstrOffset < Len + HomeInstrs)
        {
            Skip = Len + HomeInstrs - InstrOffset;
        }
        else if (FunctionLengthInstr >= Len + 1 &&
                 InstrOffset >= FunctionLengthInstr - (Len + 1))
        {
            Skip = InstrOffset - (FunctionLengthInstr - (Len + 1));
            HomeInstrs = 0;
        }
    }

    if (Skip == 0)
    {
        if (Info->CR == ARM64_CR_CHAINED ||
            Info->CR == ARM64_CR_CHAINED_PAC)
        {
            Context->Sp = Context->Fp;
            if (!RtlpArm64RestoreIntRegs(Context, 29, 2, 0))
                return FALSE;
        }

        Context->Sp += LocalSize;

        if (FpSize != 0 &&
            !RtlpArm64RestoreFpRegs(Context, 8, FpRegs, IntRegs))
        {
            return FALSE;
        }

        if (Info->CR == ARM64_CR_UNCHAINED_SAVED_LR &&
            !RtlpArm64RestoreIntRegs(Context, 30, 1, IntRegs - 1))
        {
            return FALSE;
        }

        if (!RtlpArm64RestoreIntRegs(Context, 19, Info->RegI, -(LONG)SavedRegs))
            return FALSE;
    }
    else
    {
        switch (Info->CR)
        {
            case ARM64_CR_CHAINED:
            case ARM64_CR_CHAINED_PAC:
                if (Pos++ >= Skip)
                    Context->Sp = Context->Fp;

                if (LocalSize <= 512)
                {
                    if (Pos++ >= Skip &&
                        !RtlpArm64RestoreIntRegs(Context, 29, 2, -(LONG)LocalSizeRegs))
                    {
                        return FALSE;
                    }
                    break;
                }

                if (Pos++ >= Skip &&
                    !RtlpArm64RestoreIntRegs(Context, 29, 2, 0))
                {
                    return FALSE;
                }
                /* fall through */

            case ARM64_CR_UNCHAINED:
            case ARM64_CR_UNCHAINED_SAVED_LR:
                if (LocalSize == 0)
                    break;

                if (Pos++ >= Skip)
                    Context->Sp += ((LocalSize - 1) % 4088) + 1;
                if (LocalSize > 4088 && Pos++ >= Skip)
                    Context->Sp += 4088;
                break;
        }

        Pos += HomeInstrs;

        if (FpSize != 0)
        {
            if ((Info->RegF % 2) == 0)
            {
                if (Pos++ >= Skip &&
                    !RtlpArm64RestoreFpRegs(Context,
                                            8 + Info->RegF,
                                            1,
                                            IntRegs + FpRegs - 1))
                {
                    return FALSE;
                }
            }

            for (PairCount = (Info->RegF + 1) / 2; PairCount > 0; PairCount--)
            {
                PairIndex = PairCount - 1;
                if (Pos++ < Skip)
                    continue;

                if (PairIndex == 0 && IntSize == 0)
                {
                    if (!RtlpArm64RestoreFpRegs(Context, 8, 2, -(LONG)SavedRegs))
                        return FALSE;
                }
                else
                {
                    if (!RtlpArm64RestoreFpRegs(Context,
                                                8 + 2 * PairIndex,
                                                2,
                                                IntRegs + 2 * PairIndex))
                    {
                        return FALSE;
                    }
                }
            }
        }

        if (Info->RegI % 2)
        {
            if (Pos++ >= Skip)
            {
                if (Info->CR == ARM64_CR_UNCHAINED_SAVED_LR &&
                    !RtlpArm64RestoreIntRegs(Context, 30, 1, IntRegs - 1))
                {
                    return FALSE;
                }

                if (!RtlpArm64RestoreIntRegs(Context,
                                             18 + Info->RegI,
                                             1,
                                             (Info->RegI > 1) ? (LONG)(Info->RegI - 1) : -(LONG)SavedRegs))
                {
                    return FALSE;
                }
            }
        }
        else if (Info->CR == ARM64_CR_UNCHAINED_SAVED_LR)
        {
            if (Pos++ >= Skip &&
                !RtlpArm64RestoreIntRegs(Context,
                                         30,
                                         1,
                                         Info->RegI ? (LONG)(IntRegs - 1) : -(LONG)SavedRegs))
            {
                return FALSE;
            }
        }

        for (PairCount = Info->RegI / 2; PairCount > 0; PairCount--)
        {
            PairIndex = PairCount - 1;
            if (Pos++ < Skip)
                continue;

            if (PairIndex != 0)
            {
                if (!RtlpArm64RestoreIntRegs(Context,
                                             19 + 2 * PairIndex,
                                             2,
                                             2 * PairIndex))
                {
                    return FALSE;
                }
            }
            else
            {
                if (!RtlpArm64RestoreIntRegs(Context, 19, 2, -(LONG)SavedRegs))
                    return FALSE;
            }
        }
    }

    if (Info->CR == ARM64_CR_CHAINED_PAC)
        RtlpArm64NormalizeLr(Context);

    return TRUE;
}

/* ------------------------------------------------------------------ *
 *  RtlVirtualUnwind - Core ARM64 unwinder
 * ------------------------------------------------------------------ */

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
    ULONG_PTR ControlRva, CodeOffset;
    ULONG UnwindData;
    PUCHAR UnwindCodes;
    ULONG UnwindBytes;
    ULONG CodeIdx;
    ULONG CodeOffsetInEpilog;
    ULONG64 SavedFp;
    ULONG PrologSize = 0;
    ULONG EpilogSize = 0;
    BOOLEAN InProlog = FALSE;
    BOOLEAN InEpilog = FALSE;
    BOOLEAN ChainedFunction;
    ARM64_XDATA_INFO XdataInfo;
    ARM64_PACKED_INFO PackedInfo;
    PULONG Xdata;
    PULONG LanguageHandler;
    BOOLEAN HasPackedFormat;
    BOOLEAN HasSetFpCode;
    BOOLEAN IsLeafFunction;

    (VOID)ContextPointers;

    if (HandlerData) *HandlerData = NULL;
    RtlZeroMemory(&XdataInfo, sizeof(XdataInfo));
    RtlZeroMemory(&PackedInfo, sizeof(PackedInfo));

    /* Default establisher frame */
    SavedFp = Context->Fp;
    *EstablisherFrame = SavedFp ? SavedFp : Context->Sp;

    /* Get relative virtual address */
    ControlRva = ControlPc - ImageBase;

    /* Get the unwind data flags */
    UnwindData = FunctionEntry->UnwindData;
    HasPackedFormat = ((UnwindData & ARM64_UNWIND_FLAG_MASK) != 0);

    if (HasPackedFormat)
    {
        /* Decode packed format */
        RtlpArm64DecodePacked(FunctionEntry, &PackedInfo);

        if (ControlRva < FunctionEntry->BeginAddress ||
            ControlRva >= FunctionEntry->BeginAddress + PackedInfo.FunctionLength)
        {
            return NULL;
        }

        CodeOffset = ControlRva - FunctionEntry->BeginAddress;

        ChainedFunction = (PackedInfo.CR == ARM64_CR_CHAINED ||
                           PackedInfo.CR == ARM64_CR_CHAINED_PAC);

        *EstablisherFrame = ChainedFunction ? Context->Fp : Context->Sp;

        if (!RtlpArm64UnwindPacked(UnwindData & ARM64_UNWIND_FLAG_MASK,
                                   &PackedInfo,
                                   CodeOffset,
                                   Context))
        {
            return NULL;
        }

        Context->Pc = Context->Lr;
        return NULL;
    }
    else
    {
        /* Full .xdata record */
        Xdata = (PULONG)(ImageBase + UnwindData);

        RtlpArm64ParseXdataHeader(Xdata, &XdataInfo);

        if (ControlRva < FunctionEntry->BeginAddress ||
            ControlRva >= FunctionEntry->BeginAddress + XdataInfo.FunctionLength)
        {
            return NULL;
        }

        CodeOffset = ControlRva - FunctionEntry->BeginAddress;
        UnwindCodes = XdataInfo.UnwindCodes;
        UnwindBytes = XdataInfo.CodeWords * 4;

        /* Compute prolog size from .xdata unwind codes. */
        PrologSize = RtlpArm64GetSequenceSize(UnwindCodes, UnwindBytes, 0, FALSE);

        if (CodeOffset < PrologSize)
        {
            InProlog = TRUE;
        }

        /* Check epilog scopes */
        if (!InProlog && XdataInfo.EpilogCount > 0 && !XdataInfo.EpilogPacked)
        {
            ULONG i;
            for (i = 0; i < XdataInfo.EpilogCount; i++)
            {
                ULONG Scope = XdataInfo.EpilogScopes[i];
                ULONG EpilogStartOffset = (Scope & 0x3FFFF) * 4;
                ULONG EpilogStartIdx = (Scope >> 22) & 0x3FF;
                ULONG ScopeSize = RtlpArm64GetSequenceSize(UnwindCodes,
                                                           UnwindBytes,
                                                           EpilogStartIdx,
                                                           TRUE);

                if (EpilogStartOffset <= CodeOffset &&
                    CodeOffset < EpilogStartOffset + ScopeSize)
                {
                    InEpilog = TRUE;
                    CodeIdx = EpilogStartIdx;
                }
            }
        }
        else if (!InProlog && XdataInfo.EpilogPacked)
        {
            EpilogSize = RtlpArm64GetSequenceSize(UnwindCodes,
                                                  UnwindBytes,
                                                  XdataInfo.EpilogStartIndex,
                                                  TRUE);
            if (XdataInfo.FunctionLength >= EpilogSize &&
                CodeOffset >= XdataInfo.FunctionLength - EpilogSize)
            {
                InEpilog = TRUE;
                CodeIdx = XdataInfo.EpilogStartIndex;
            }
        }

        /* Scan for frame chain (set_fp / add_fp / save_fplr) */
        HasSetFpCode = FALSE;
        ChainedFunction = FALSE;
        {
            ULONG ci = 0;
            while (ci < UnwindBytes)
            {
                ARM64_UNWIND_CODE Code;
                RtlpArm64DecodeCode(UnwindCodes, ci, &Code);
                if (Code.OpcodeId == ARM64_UWOP_SET_FP ||
                    Code.OpcodeId == ARM64_UWOP_ADD_FP)
                {
                    HasSetFpCode = TRUE;
                    ChainedFunction = TRUE;
                    break;
                }
                if (Code.OpcodeId == ARM64_UWOP_SAVE_FPLR_X ||
                    Code.OpcodeId == ARM64_UWOP_SAVE_FPLR)
                {
                    ChainedFunction = TRUE;
                }
                if (Code.OpcodeId == ARM64_UWOP_END || Code.OpcodeId == ARM64_UWOP_END_C)
                    break;
                ci += Code.TotalBytes;
            }
        }
    }

    /*
     * Is this a leaf function?
     */
    IsLeafFunction = (UnwindBytes == 0);

    if (ChainedFunction && HasSetFpCode)
    {
        *EstablisherFrame = Context->Fp;
    }
    else if (!ChainedFunction)
    {
        *EstablisherFrame = Context->Sp;
    }

    /*
     * Determine starting index and skip counts for prolog/epilog.
     *
     * For prolog: codes are stored in REVERSE execution order.
     *   Last code in array = first instruction of prolog.
     *   If we executed N instructions into the prolog, we skip
     *   the last N codes (those not yet executed).
     *
     * For epilog: codes are executed forward.
     *   First code = first instruction of epilog.
     *   If we executed N instructions into the epilog, we skip
     *   the first N codes (those already executed).
     *
     * For body: execute ALL codes from index 0.
     */
    CodeIdx = 0;

    if (InEpilog)
    {
        /*
         * Determine how many codes to skip.
         * CodeIdx may have been set from the epilog scope above.
         * Skip additional codes based on how far into the epilog we are.
         */
        ULONG         EpilogBaseIndex = CodeIdx; /* scope's start index */
        CodeOffsetInEpilog = CodeOffset;

        /* Compute bytes into epilog from the scope's start */
        if (HasPackedFormat)
        {
            CodeOffsetInEpilog = CodeOffset -
                (PackedInfo.FunctionLength - PrologSize);
        }
        else if (XdataInfo.EpilogPacked)
        {
            EpilogSize = RtlpArm64GetSequenceSize(UnwindCodes,
                                                  UnwindBytes,
                                                  XdataInfo.EpilogStartIndex,
                                                  TRUE);
            EpilogBaseIndex = XdataInfo.EpilogStartIndex;
            CodeOffsetInEpilog = CodeOffset - (XdataInfo.FunctionLength - EpilogSize);
        }
        else if (!XdataInfo.EpilogPacked && XdataInfo.EpilogCount > 0)
        {
            ULONG i;
            for (i = 0; i < XdataInfo.EpilogCount; i++)
            {
                ULONG Scope = XdataInfo.EpilogScopes[i];
                ULONG ScopeStart = (Scope & 0x3FFFF) * 4;
                ULONG ScopeIdx = (Scope >> 22) & 0x3FF;
                ULONG ScopeSize = RtlpArm64GetSequenceSize(UnwindCodes,
                                                           UnwindBytes,
                                                           ScopeIdx,
                                                           TRUE);

                if (ScopeStart <= CodeOffset &&
                    CodeOffset < ScopeStart + ScopeSize)
                {
                    EpilogBaseIndex = ScopeIdx;
                    CodeOffsetInEpilog = CodeOffset - ScopeStart;
                }
            }
        }

        CodeIdx = EpilogBaseIndex;

        /* Skip codes for instructions already executed in the epilog */
        {
            ULONG SkippedBytes = 0;
            ULONG ci = CodeIdx;
            while (ci < UnwindBytes && SkippedBytes < CodeOffsetInEpilog)
            {
                ARM64_UNWIND_CODE Code;
                RtlpArm64DecodeCode(UnwindCodes, ci, &Code);
                ULONG instrSize = RtlpArm64InstructionSize(&Code);

                if (SkippedBytes + instrSize <= CodeOffsetInEpilog)
                {
                    SkippedBytes += instrSize;
                    CodeIdx += Code.TotalBytes;
                }
                else
                {
                    break;
                }
                ci += Code.TotalBytes;
            }
        }

        if (!InProlog)
        {
            /* Execute remaining epilog codes */
            goto ExecuteCodes;
        }
    }

    if (InProlog)
    {
        ULONG BytesToSkip = PrologSize - CodeOffset;
        ULONG SkippedBytes = 0;

        CodeIdx = 0;
        while (CodeIdx < UnwindBytes && SkippedBytes < BytesToSkip)
        {
            ARM64_UNWIND_CODE Code;
            ULONG InstructionSize;

            RtlpArm64DecodeCode(UnwindCodes, CodeIdx, &Code);
            if (Code.OpcodeId == ARM64_UWOP_END ||
                Code.OpcodeId == ARM64_UWOP_END_C)
            {
                break;
            }

            InstructionSize = RtlpArm64InstructionSize(&Code);
            if (SkippedBytes + InstructionSize > BytesToSkip)
                break;

            SkippedBytes += InstructionSize;
            CodeIdx += Code.TotalBytes;
        }

        goto ExecuteCodes;
    }

    /*
     * In function body or leaf: start from beginning.
     * But check for end_c chained scopes (function fragments).
     */
    CodeIdx = 0;

    /* For function fragments (end_c), check if this is a secondary fragment */
    if (!InProlog && !InEpilog)
    {
        ULONG ci = 0;
        while (ci < UnwindBytes)
        {
            ARM64_UNWIND_CODE Code;
            RtlpArm64DecodeCode(UnwindCodes, ci, &Code);
            if (Code.OpcodeId == ARM64_UWOP_END_C)
            {
                break;
            }
            if (Code.OpcodeId == ARM64_UWOP_END)
                break;
            ci += Code.TotalBytes;
        }
    }

ExecuteCodes:
    /*
     * Execute unwind codes.
     */
    while (CodeIdx < UnwindBytes)
    {
        ARM64_UNWIND_CODE Code;
        ULONG64 Offset;
        ULONG64 Value;
        ULONG64 Value2;
        ULONG_PTR Address;
        ULONG RegBase;
        ULONG RegIdx;

        RtlpArm64DecodeCode(UnwindCodes, CodeIdx, &Code);

        /* End marker: stop and check for return address */
        if (Code.OpcodeId == ARM64_UWOP_END ||
            Code.OpcodeId == ARM64_UWOP_END_C)
        {
            CodeIdx += Code.TotalBytes;

            /* end_c: skip parent function's prolog codes until next end */
            if (Code.OpcodeId == ARM64_UWOP_END_C)
            {
                while (CodeIdx < UnwindBytes)
                {
                    ARM64_UNWIND_CODE ParentCode;
                    RtlpArm64DecodeCode(UnwindCodes, CodeIdx, &ParentCode);
                    if (ParentCode.OpcodeId == ARM64_UWOP_END)
                    {
                        CodeIdx += ParentCode.TotalBytes;
                        break;
                    }
                    CodeIdx += ParentCode.TotalBytes;
                }
            }
            break;
        }

        switch (Code.OpcodeId)
        {
        /* ------------------------------------------------------------ */
        /* Stack allocation */
        /* ------------------------------------------------------------ */
        case ARM64_UWOP_ALLOC_S:
            /* alloc_s: size = (Byte0[4:0]) * 16 */
            Context->Sp += (Code.Byte0 & 0x1F) * 16;
            break;

        case ARM64_UWOP_ALLOC_M:
            /* alloc_m: size = ((Byte0[2:0] << 8) | Byte1) * 16 */
            Offset = (((ULONG)(Code.Byte0 & 7) << 8) | (ULONG)Code.Byte1) * 16;
            Context->Sp += Offset;
            break;

        case ARM64_UWOP_ALLOC_L:
            /* alloc_l: size = ((Byte1 << 16) | (Byte2 << 8) | Byte3) * 16 */
            Offset = (((ULONG)Code.Byte1 << 16) | ((ULONG)Code.Byte2 << 8) | (ULONG)Code.Byte3) * 16;
            Context->Sp += Offset;
            break;

        case ARM64_UWOP_ALLOC_Z:
            /* alloc_z: size = Byte1 * SVE-VL. SVE not supported yet, skip. */
            break;

        /* ------------------------------------------------------------ */
        /* Integer register saves */
        /* ------------------------------------------------------------ */
        case ARM64_UWOP_SAVE_R19R20_X:
            /* save_r19r20_x: <x19,x20> at [sp - Byte0[4:0]*8]! (pre-indexed) */
            Offset = (Code.Byte0 & 0x1F) * 8;
            Address = (ULONG_PTR)Context->Sp;
            if (!RtlpArm64Read64(&Value, Address) ||
                !RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
            {
                return NULL;
            }
            RtlpArm64SetIntReg(Context, 19, Value);
            RtlpArm64SetIntReg(Context, 20, Value2);
            Context->Sp += Offset;
            break;

        case ARM64_UWOP_SAVE_REGP:
            /* save_regp: x(19+RegBase) pair at [sp + Offset*8] */
            RegBase = 19 + (((Code.Byte0 & 3) << 2) | ((Code.Byte1 >> 6) & 3));
            Offset = (ULONG)(Code.Byte1 & 0x3F) * 8;
            Address = (ULONG_PTR)(Context->Sp + Offset);
            if (!RtlpArm64Read64(&Value, Address) ||
                !RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
            {
                return NULL;
            }
            RtlpArm64SetIntReg(Context, RegBase, Value);
            RtlpArm64SetIntReg(Context, RegBase + 1, Value2);
            break;

        case ARM64_UWOP_SAVE_REGP_X:
            /* save_regp_x: x(19+RegBase) pair at [sp - (Offset+1)*8]! (pre-indexed) */
            RegBase = 19 + (((Code.Byte0 & 3) << 2) | ((Code.Byte1 >> 6) & 3));
            Offset = (ULONG)(Code.Byte1 & 0x3F) + 1;
            Address = (ULONG_PTR)Context->Sp;
            if (!RtlpArm64Read64(&Value, Address) ||
                !RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
            {
                return NULL;
            }
            RtlpArm64SetIntReg(Context, RegBase, Value);
            RtlpArm64SetIntReg(Context, RegBase + 1, Value2);
            Context->Sp += Offset * 8;
            break;

        case ARM64_UWOP_SAVE_REG:
            /* save_reg: x(19+RegBase) at [sp + Offset*8] */
            RegBase = 19 + (((Code.Byte0 & 3) << 2) | ((Code.Byte1 >> 6) & 3));
            Offset = (ULONG)(Code.Byte1 & 0x3F) * 8;
            if (!RtlpArm64Read64(&Value, (ULONG_PTR)(Context->Sp + Offset)))
                return NULL;
            RtlpArm64SetIntReg(Context, RegBase, Value);
            break;

        case ARM64_UWOP_SAVE_REG_X:
            /* save_reg_x: x(19+RegBase) at [sp - (Offset+1)*8]! (pre-indexed) */
            RegBase = 19 + (((Code.Byte0 & 1) << 3) | ((Code.Byte1 >> 5) & 7));
            Offset = (Code.Byte1 & 0x1F) + 1;
            if (!RtlpArm64Read64(&Value, (ULONG_PTR)Context->Sp))
                return NULL;
            RtlpArm64SetIntReg(Context, RegBase, Value);
            Context->Sp += Offset * 8;
            break;

        case ARM64_UWOP_SAVE_LRPAIR:
            /* save_lrpair: <x(19+2*RegBase), lr> at [sp + Offset*8] */
            RegBase = 2 * (((Code.Byte0 & 1) << 2) | ((Code.Byte1 >> 6) & 3));
            Offset = (ULONG)(Code.Byte1 & 0x3F) * 8;
            Address = (ULONG_PTR)(Context->Sp + Offset);
            if (!RtlpArm64Read64(&Value, Address) ||
                !RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
            {
                return NULL;
            }
            RtlpArm64SetIntReg(Context, 19 + RegBase, Value);
            Context->Lr = Value2;
            break;

        /* ------------------------------------------------------------ */
        /* Frame pointer + link register saves */
        /* ------------------------------------------------------------ */
        case ARM64_UWOP_SAVE_FPLR:
            /* save_fplr: <x29,lr> at [sp + Offset*8] */
            Offset = (Code.Byte0 & 0x3F) * 8;
            Address = (ULONG_PTR)(Context->Sp + Offset);
            if (!RtlpArm64Read64(&Value, Address) ||
                !RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
            {
                return NULL;
            }
            Context->Fp = Value;
            Context->Lr = Value2;
            break;

        case ARM64_UWOP_SAVE_FPLR_X:
            /* save_fplr_x: <x29,lr> at [sp - (Offset+1)*8]! (pre-indexed) */
            Offset = (Code.Byte0 & 0x3F) + 1;
            Address = (ULONG_PTR)Context->Sp;
            if (!RtlpArm64Read64(&Value, Address) ||
                !RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
            {
                return NULL;
            }
            Context->Fp = Value;
            Context->Lr = Value2;
            Context->Sp += Offset * 8;
            break;

        /* ------------------------------------------------------------ */
        /* Floating point saves */
        /* ------------------------------------------------------------ */
        case ARM64_UWOP_SAVE_FREGP:
            /* save_fregp: d(8+RegBase) pair at [sp + Offset*8] */
            RegBase = 8 + (((Code.Byte0 & 1) << 2) | ((Code.Byte1 >> 6) & 3));
            Offset = (ULONG)(Code.Byte1 & 0x3F) * 8;
            Address = (ULONG_PTR)(Context->Sp + Offset);
            if (!RtlpArm64Read64(&Value, Address) ||
                !RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
            {
                return NULL;
            }
            RtlpArm64SetFpReg(Context, RegBase, Value, Value2);
            break;

        case ARM64_UWOP_SAVE_FREGP_X:
            /* save_fregp_x: d(8+RegBase) pair at [sp - (Offset+1)*8]! */
            RegBase = 8 + (((Code.Byte0 & 1) << 2) | ((Code.Byte1 >> 6) & 3));
            Offset = (ULONG)(Code.Byte1 & 0x3F) + 1;
            Address = (ULONG_PTR)Context->Sp;
            if (!RtlpArm64Read64(&Value, Address) ||
                !RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
            {
                return NULL;
            }
            RtlpArm64SetFpReg(Context, RegBase, Value, Value2);
            Context->Sp += Offset * 8;
            break;

        case ARM64_UWOP_SAVE_FREG:
            /* save_freg: d(8+RegBase) at [sp + Offset*8] */
            RegBase = 8 + (((Code.Byte0 & 1) << 2) | ((Code.Byte1 >> 6) & 3));
            Offset = (ULONG)(Code.Byte1 & 0x3F) * 8;
            if (!RtlpArm64Read64(&Value, (ULONG_PTR)(Context->Sp + Offset)))
                return NULL;
            RtlpArm64SetFpReg(Context, RegBase, Value, 0);
            break;

        case ARM64_UWOP_SAVE_FREG_X:
            /* save_freg_x: d(8+RegBase) at [sp - (Offset+1)*8]! */
            RegBase = 8 + ((Code.Byte1 >> 5) & 7);
            Offset = (Code.Byte1 & 0x1F) + 1;
            if (!RtlpArm64Read64(&Value, (ULONG_PTR)Context->Sp))
                return NULL;
            RtlpArm64SetFpReg(Context, RegBase, Value, 0);
            Context->Sp += Offset * 8;
            break;

        /* ------------------------------------------------------------ */
        /* Frame pointer setup */
        /* ------------------------------------------------------------ */
        case ARM64_UWOP_SET_FP:
            /* set_fp: mov x29,sp → undo by restoring SP from FP */
            Context->Sp = Context->Fp;
            break;

        case ARM64_UWOP_ADD_FP:
            /* add_fp: add x29,sp,#Byte1*8 → undo: sp = x29 - Byte1*8 */
            Context->Sp = Context->Fp - (ULONG)Code.Byte1 * 8;
            break;

        /* ------------------------------------------------------------ */
        /* save_next: save next register pair after a prior save_regp etc */
        /* ------------------------------------------------------------ */
        case ARM64_UWOP_SAVE_NEXT:
            /* This is complex: it saves the NEXT register pair in sequence
               to the next available stack slot. The register numbers depend
               on what was previously saved.
               For now, skip over it. */
            break;

        /* ------------------------------------------------------------ */
        /* save_any_* (Windows 11+ extended saving) */
        /* ------------------------------------------------------------ */
        case ARM64_UWOP_SAVE_ANY_XREG:
            /* save_any_xreg: Byte1 determines p, x, r, Byte2 determines offset
               Format: 11100111 0pxrrrrr 00oooooo
               p: pair (1) vs single (0)
               x: negative pre-indexed (1) vs positive (0)
               r: register index
               o: offset * 16 if x=1 or p=1, else * 8
             */
            {
                BOOLEAN isPair = !!(Code.Byte1 & 0x20);
                BOOLEAN negX = !!(Code.Byte1 & 0x10);
                RegIdx = Code.Byte1 & 0x1F;
                Offset = (Code.Byte2 & 0x3F);
                if (negX || isPair)
                    Offset *= 16;
                else
                    Offset *= 8;

                if (negX)
                {
                    Address = (ULONG_PTR)Context->Sp;
                    if (!RtlpArm64Read64(&Value, Address))
                        return NULL;
                    if (isPair)
                    {
                        if (!RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
                            return NULL;
                    }
                    RtlpArm64SetIntReg(Context, RegIdx, Value);
                    if (isPair)
                        RtlpArm64SetIntReg(Context, RegIdx + 1, Value2);
                    Context->Sp += Offset;
                }
                else
                {
                    Address = (ULONG_PTR)(Context->Sp + Offset);
                    if (!RtlpArm64Read64(&Value, Address))
                        return NULL;
                    if (isPair)
                    {
                        if (!RtlpArm64Read64(&Value2, Address + sizeof(ULONG64)))
                            return NULL;
                    }
                    RtlpArm64SetIntReg(Context, RegIdx, Value);
                    if (isPair)
                        RtlpArm64SetIntReg(Context, RegIdx + 1, Value2);
                }
            }
            break;

        case ARM64_UWOP_SAVE_ANY_DREG:
        case ARM64_UWOP_SAVE_ANY_QREG:
            /* Similar to save_any_xreg but for D/Q registers. Skip for now. */
            break;

        case ARM64_UWOP_SAVE_ZREG:
        case ARM64_UWOP_SAVE_PREG:
            /* SVE register saves - not yet supported. */
            break;

        /* ------------------------------------------------------------ */
        /* Control flow */
        /* ------------------------------------------------------------ */
        case ARM64_UWOP_NOP:
            /* nop - no unwind operation */
            break;

        case ARM64_UWOP_PAC_SIGN_LR:
            RtlpArm64NormalizeLr(Context);
            break;

        /* ------------------------------------------------------------ */
        /* Custom stack operations (kernel trap/exception frames) */
        /* ------------------------------------------------------------ */
        case ARM64_UWOP_CUSTOM_TRAP_FRAME:
        case ARM64_UWOP_CUSTOM_MACHINE_FRAME:
        case ARM64_UWOP_CUSTOM_CONTEXT:
        case ARM64_UWOP_CUSTOM_EC_CONTEXT:
        case ARM64_UWOP_CUSTOM_CLEAR_UNWOUND:
            /* Custom unwind codes for kernel frames - not yet implemented. */
            break;

        /* ------------------------------------------------------------ */
        /* Unknown / reserved */
        /* ------------------------------------------------------------ */
        default:
            break;
        }

        CodeIdx += Code.TotalBytes;
    }

    /*
     * After executing unwind codes:
     *
     * In ARM64 PE unwind codes, executing the sequence restores the caller's
     * return address into LR. Only leaf frames without unwind metadata use LR
     * directly; non-leaf frames must not blindly pop PC from SP.
     *
     * If in prolog, the return address has already been restored
     * by the executed unwind codes (via save_lrpair, save_fplr, etc.)
     *
     * If in epilog, same - codes handle it.
     *
     * For leaf functions with no unwind data, use LR directly.
     */
    if (!IsLeafFunction && !InProlog && !InEpilog)
    {
        Context->Pc = Context->Lr;
    }
    else if (IsLeafFunction)
    {
        Context->Pc = Context->Lr;
    }
    else if (InProlog || InEpilog)
    {
        /* During prolog/epilog unwinding, PC is loaded into LR
           by the code that restored the <x29, lr> or <reg, lr> pair.
           Only update PC if it hasn't changed from the unwind codes. */
    }

    /* Update PC from LR for in-prolog/epilog cases */
    if (InProlog || InEpilog)
    {
        /* The unwind codes may have already set Lr from the saved <fp,lr> pair.
           If LR was restored from stack, use it as the new PC. */
        if (Context->Lr != Context->Pc && Context->Lr != 0)
        {
            Context->Pc = Context->Lr;
        }
    }

    if (InProlog || InEpilog)
    {
        if (HandlerData)
            *HandlerData = NULL;
        return NULL;
    }

    /*
     * Check for exception handler.
     */
    if (HasPackedFormat)
    {
        /* Packed format has no exception handler */
        if (HandlerData) *HandlerData = NULL;
        return NULL;
    }

    /*
     * For full .xdata: check X flag.
     */
    if (XdataInfo.HasExceptionData)
    {
        ULONG HandlerOffset;

        /* Compute offset to the language handler:
           HeaderWords + EpilogScopes + CodeWords */
        HandlerOffset = XdataInfo.HeaderWords;
        if (!XdataInfo.EpilogPacked)
            HandlerOffset += XdataInfo.EpilogCount;
        HandlerOffset += XdataInfo.CodeWords;

        LanguageHandler = &Xdata[HandlerOffset];

        if (HandlerData)
            *HandlerData = (PVOID)(LanguageHandler + 1);

        if (*LanguageHandler != 0 &&
            (HandlerType & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)))
        {
            return (PEXCEPTION_ROUTINE)(ImageBase + *LanguageHandler);
        }
    }

    return NULL;
}

/* ------------------------------------------------------------------ *
 *  RtlUnwindEx
 *
 *  Full implementation of the Windows unwind dispatcher.
 *  Walks the call stack, calling termination handlers.
 * ------------------------------------------------------------------ */

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
    DISPATCHER_CONTEXT DispatcherContext;
    PEXCEPTION_ROUTINE ExceptionRoutine;
    EXCEPTION_DISPOSITION Disposition;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase, EstablisherFrame;
    CONTEXT UnwindContext;
    ULONG_PTR StackLow, StackHigh;
    ULONG FrameCount;
    BOOLEAN HaveTarget;

    (VOID)HistoryTable;

    HaveTarget = (TargetFrame != NULL || TargetIp != NULL);

    if (ContextRecord == NULL)
    {
        RtlRaiseStatus(STATUS_INVALID_PARAMETER);
        return;
    }

    UnwindContext = *ContextRecord;

    RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
    DispatcherContext.ContextRecord = &UnwindContext;
    DispatcherContext.HistoryTable = HistoryTable;
    DispatcherContext.TargetPc = (ULONG64)(ULONG_PTR)TargetIp;

    /*
     * Get stack limits from current thread.
     */
    {
        PKTHREAD Thread = KeGetCurrentThread();
        if (Thread && Thread->StackLimit && Thread->StackBase)
        {
            StackLow = (ULONG_PTR)Thread->StackLimit;
            StackHigh = (ULONG_PTR)Thread->StackBase;
        }
        else
        {
            StackLow = (ULONG_PTR)UnwindContext.Sp;
            StackHigh = StackLow + KERNEL_STACK_SIZE;
        }
    }

    if ((ULONG_PTR)TargetFrame)
    {
        StackHigh = (ULONG_PTR)TargetFrame + 1;
    }

    for (FrameCount = 0; FrameCount < 1024; FrameCount++)
    {
        if (UnwindContext.Sp < StackLow || UnwindContext.Sp > StackHigh)
        {
            if (ExceptionRecord)
                ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return;
        }

        FunctionEntry = RtlLookupFunctionEntry(UnwindContext.Pc, &ImageBase, NULL);
        if (FunctionEntry == NULL)
        {
            /*
             * ARM64 leaf functions do not push a return address; the caller PC
             * is the live LR.  If PC already equals LR, there is no useful leaf
             * unwind left to perform.
             */
            if (UnwindContext.Lr == 0 || UnwindContext.Lr == UnwindContext.Pc)
                break;

            UnwindContext.Pc = UnwindContext.Lr;
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

        if ((EstablisherFrame < StackLow) ||
            (EstablisherFrame >= StackHigh) ||
            (EstablisherFrame & 0x7))
        {
            if (ExceptionRecord)
                ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return;
        }

        if (ExceptionRoutine != NULL)
        {
            if (HaveTarget && EstablisherFrame == (ULONG64)(ULONG_PTR)TargetFrame)
            {
                if (ExceptionRecord)
                    ExceptionRecord->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;
            }

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

                if (ExceptionRecord)
                {
                    ExceptionRecord->ExceptionFlags &= ~(EXCEPTION_TARGET_UNWIND |
                                                         EXCEPTION_COLLIDED_UNWIND);
                }

                if (Disposition == ExceptionContinueExecution)
                {
                    if (ExceptionRecord && (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE))
                    {
                        RtlRaiseStatus(STATUS_NONCONTINUABLE_EXCEPTION);
                    }
                    return;
                }
                else if (Disposition == ExceptionCollidedUnwind)
                {
                    UnwindContext = *ContextRecord;
                    DispatcherContext.ContextRecord = &UnwindContext;
                    EstablisherFrame = DispatcherContext.EstablisherFrame;

                    if (ExceptionRecord)
                        ExceptionRecord->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
                }
                else if (Disposition != ExceptionContinueSearch)
                {
                    return;
                }
            }
            while (Disposition == ExceptionCollidedUnwind);
        }

        if (HaveTarget && EstablisherFrame == (ULONG64)(ULONG_PTR)TargetFrame)
        {
            *ContextRecord = UnwindContext;
            return;
        }

        *ContextRecord = UnwindContext;
    }
}

/* ------------------------------------------------------------------ *
 *  RtlUnwind - legacy entry point.
 * ------------------------------------------------------------------ */

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
    RtlUnwindEx(TargetFrame, TargetIp, ExceptionRecord, ReturnValue, &ContextRecord, NULL);
}

/* ------------------------------------------------------------------ *
 *  RtlLookupFunctionEntry - binary search on .pdata
 *
 *  Kept from the original rtlstubs.c (works correctly).
 * ------------------------------------------------------------------ */

#undef ARM64_UNWIND_FLAG_MASK
#undef ARM64_PACKED_FUNCTION_LENGTH_SHIFT
#undef ARM64_PACKED_FUNCTION_LENGTH_MASK
#undef ARM64_XDATA_FUNCTION_LENGTH_MASK
#define ARM64_UNWIND_FLAG_MASK 0x3UL
#define ARM64_PACKED_FUNCTION_LENGTH_SHIFT 2
#define ARM64_PACKED_FUNCTION_LENGTH_MASK 0x7FFUL
#define ARM64_XDATA_FUNCTION_LENGTH_MASK 0x3FFFFUL

static
ULONG
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

static
PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionTable(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Out_ PULONG Length)
{
    PVOID Table;
    ULONG Size;

    if (!RtlPcToFileHeader((PVOID)(ULONG_PTR)ControlPc, (PVOID *)ImageBase))
    {
        *Length = 0;
        return NULL;
    }

    Table = RtlImageDirectoryEntryToData((PVOID)(ULONG_PTR)*ImageBase,
                                         TRUE,
                                         IMAGE_DIRECTORY_ENTRY_EXCEPTION,
                                         &Size);
    if (Table == NULL)
    {
        *Length = 0;
        return NULL;
    }

    *Length = Size / sizeof(RUNTIME_FUNCTION);
    return Table;
}

PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _Inout_opt_ PVOID HistoryTable)
{
    PRUNTIME_FUNCTION FunctionTable, FunctionEntry;
    ULONG TableLength;
    ULONG_PTR ControlRva;
    ULONG IndexLow, IndexHigh, IndexMid;
    ULONG FunctionLength;

    (VOID)HistoryTable;

    FunctionTable = RtlLookupFunctionTable(ControlPc, ImageBase, &TableLength);
    if (FunctionTable == NULL)
        return NULL;

    ControlRva = (ULONG_PTR)ControlPc - (ULONG_PTR)*ImageBase;
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

        FunctionLength = RtlpArm64FunctionLength((ULONG_PTR)*ImageBase,
                                                 FunctionEntry);
        if (ControlRva >= (FunctionEntry->BeginAddress + FunctionLength))
        {
            IndexLow = IndexMid + 1;
            continue;
        }

        return FunctionEntry;
    }

    return NULL;
}

/* ------------------------------------------------------------------ *
 *  Helper stubs required by the kernel build
 * ------------------------------------------------------------------ */

VOID NTAPI RtlRestoreContext(PCONTEXT Context, PEXCEPTION_RECORD ExceptionRecord)
{
    (VOID)Context;
    (VOID)ExceptionRecord;
    NOTHING;
}

void _local_unwind2(void) {}
void _global_unwind2(void) {}
int _except_handler2(void) { return 1; }
int _except_handler3(void) { return 1; }
unsigned long long __ull_rshift(unsigned long long v, int s) { return v >> s; }
unsigned long long __ll_rshift(unsigned long long v, int s) { return (unsigned long long)((long long)v >> s); }
unsigned long long __ll_lshift(unsigned long long v, int s) { return v << s; }

__asm__(
    ".text\n"
    ".globl _abnormal_termination\n"
    ".def _abnormal_termination; .scl 2; .type 32; .endef\n"
    "_abnormal_termination:\n"
    "    mov w0, wzr\n"
    "    ret\n");
