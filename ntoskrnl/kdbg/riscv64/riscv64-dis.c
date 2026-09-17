/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     KDBG RISC-V instruction words, lengths and branch targets
 *
 * No RISC-V disassembler is provided yet (ABI-127): instructions are printed
 * as words with their opcode class, compressed ones marked. Control-flow
 * decoding is exact for the base ISA and the C extension so single-step can
 * place a temporary breakpoint at the next instruction.
 */

#include <ntoskrnl.h>
#include "kdbg/kdb.h"
#define NDEBUG
#include <debug.h>

#define RISCV_OPCODE_LOAD      0x03
#define RISCV_OPCODE_MISC_MEM  0x0F
#define RISCV_OPCODE_OP_IMM    0x13
#define RISCV_OPCODE_AUIPC     0x17
#define RISCV_OPCODE_OP_IMM_32 0x1B
#define RISCV_OPCODE_STORE     0x23
#define RISCV_OPCODE_AMO       0x2F
#define RISCV_OPCODE_OP        0x33
#define RISCV_OPCODE_LUI       0x37
#define RISCV_OPCODE_OP_32     0x3B
#define RISCV_OPCODE_BRANCH    0x63
#define RISCV_OPCODE_JALR      0x67
#define RISCV_OPCODE_JAL       0x6F
#define RISCV_OPCODE_SYSTEM    0x73
#define RISCV_INST_ECALL       0x00000073UL
#define RISCV_INST_EBREAK      0x00100073UL
#define RISCV_INST_SRET        0x10200073UL
#define RISCV_INST_WFI         0x10500073UL
#define RISCV_INST_C_EBREAK    0x9002

static const CHAR *const KdbpRiscvRegisterNames[32] =
{
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static LONG64
KdbpRiscvSignExtend(ULONG64 Value, ULONG Bits)
{
    ULONG Shift = 64 - Bits;

    return ((LONG64)(Value << Shift)) >> Shift;
}

static BOOLEAN
KdbpRiscvReadInstruction(ULONG_PTR Address, PULONG Instruction, PLONG Length)
{
    USHORT Low, High;

    *Instruction = 0;
    *Length = 0;
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Low, (PVOID)Address, sizeof(Low))))
        return FALSE;
    if ((Low & 3) != 3)
    {
        *Instruction = Low;
        *Length = 2;
        return TRUE;
    }
    if (!NT_SUCCESS(KdbpSafeReadMemory(&High, (PVOID)(Address + 2), sizeof(High))))
        return FALSE;
    *Instruction = (ULONG)Low | ((ULONG)High << 16);
    *Length = 4;
    return TRUE;
}

static LONG64
KdbpRiscvJalOffset(ULONG Inst)
{
    ULONG64 Imm = (((Inst >> 31) & 1) << 20) |
                  (((Inst >> 21) & 0x3FF) << 1) |
                  (((Inst >> 20) & 1) << 11) |
                  (((Inst >> 12) & 0xFF) << 12);

    return KdbpRiscvSignExtend(Imm, 21);
}

static LONG64
KdbpRiscvBranchOffset(ULONG Inst)
{
    ULONG64 Imm = (((Inst >> 31) & 1) << 12) |
                  (((Inst >> 7) & 1) << 11) |
                  (((Inst >> 25) & 0x3F) << 5) |
                  (((Inst >> 8) & 0xF) << 1);

    return KdbpRiscvSignExtend(Imm, 13);
}

static LONG64
KdbpRiscvCjOffset(ULONG Inst)
{
    ULONG64 Imm = (((Inst >> 12) & 1) << 11) |
                  (((Inst >> 11) & 1) << 4) |
                  (((Inst >> 9) & 3) << 8) |
                  (((Inst >> 8) & 1) << 10) |
                  (((Inst >> 7) & 1) << 6) |
                  (((Inst >> 6) & 1) << 7) |
                  (((Inst >> 3) & 7) << 1) |
                  (((Inst >> 2) & 1) << 5);

    return KdbpRiscvSignExtend(Imm, 12);
}

static LONG64
KdbpRiscvCbOffset(ULONG Inst)
{
    ULONG64 Imm = (((Inst >> 12) & 1) << 8) |
                  (((Inst >> 10) & 3) << 3) |
                  (((Inst >> 5) & 3) << 6) |
                  (((Inst >> 3) & 3) << 1) |
                  (((Inst >> 2) & 1) << 5);

    return KdbpRiscvSignExtend(Imm, 9);
}

static BOOLEAN
KdbpRiscvBranchTaken(ULONG Funct3, ULONG64 Rs1, ULONG64 Rs2)
{
    switch (Funct3)
    {
        case 0: return Rs1 == Rs2;
        case 1: return Rs1 != Rs2;
        case 4: return (LONG64)Rs1 < (LONG64)Rs2;
        case 5: return (LONG64)Rs1 >= (LONG64)Rs2;
        case 6: return Rs1 < Rs2;
        case 7: return Rs1 >= Rs2;
        default: return FALSE;
    }
}

/*!\brief Computes the address of the instruction executed after the one at
 *        Context->Pc, using the register state in Context.
 *
 * \retval TRUE   *NextPc holds the successor address.
 * \retval FALSE  The instruction cannot be stepped over (unreadable, ebreak,
 *                trap return) and *Reason names why.
 */
BOOLEAN
KdbpRiscvGetNextPc(
    IN PCONTEXT Context,
    OUT PULONG_PTR NextPc,
    OUT PCSTR *Reason)
{
    ULONG_PTR Pc = (ULONG_PTR)Context->Pc;
    ULONG Inst, Opcode, Funct3, Rs1, Rs2;
    LONG Length;

    *NextPc = 0;
    *Reason = NULL;
    if (!KdbpRiscvReadInstruction(Pc, &Inst, &Length))
    {
        *Reason = "instruction is not readable";
        return FALSE;
    }

    *NextPc = Pc + Length;
    if (Length == 2)
    {
        Funct3 = (Inst >> 13) & 7;
        switch (Inst & 3)
        {
            case 1:
                Rs1 = 8 + ((Inst >> 7) & 7);
                if (Funct3 == 5)
                    *NextPc = Pc + KdbpRiscvCjOffset(Inst);
                else if ((Funct3 == 6) && (Context->X[Rs1] == 0))
                    *NextPc = Pc + KdbpRiscvCbOffset(Inst);
                else if ((Funct3 == 7) && (Context->X[Rs1] != 0))
                    *NextPc = Pc + KdbpRiscvCbOffset(Inst);
                return TRUE;

            case 2:
                Rs1 = (Inst >> 7) & 31;
                Rs2 = (Inst >> 2) & 31;
                if ((Funct3 == 4) && (Rs2 == 0))
                {
                    if (Rs1 == 0)
                    {
                        *Reason = "c.ebreak cannot be stepped";
                        return FALSE;
                    }
                    *NextPc = (ULONG_PTR)Context->X[Rs1] & ~(ULONG_PTR)1;
                }
                return TRUE;

            default:
                return TRUE;
        }
    }

    Opcode = Inst & 0x7F;
    Funct3 = (Inst >> 12) & 7;
    Rs1 = (Inst >> 15) & 31;
    Rs2 = (Inst >> 20) & 31;
    switch (Opcode)
    {
        case RISCV_OPCODE_JAL:
            *NextPc = Pc + KdbpRiscvJalOffset(Inst);
            return TRUE;

        case RISCV_OPCODE_JALR:
            *NextPc = ((ULONG_PTR)Context->X[Rs1] + KdbpRiscvSignExtend(Inst >> 20, 12)) & ~(ULONG_PTR)1;
            return TRUE;

        case RISCV_OPCODE_BRANCH:
            if (KdbpRiscvBranchTaken(Funct3, Context->X[Rs1], Context->X[Rs2]))
                *NextPc = Pc + KdbpRiscvBranchOffset(Inst);
            return TRUE;

        case RISCV_OPCODE_SYSTEM:
            if (Inst == RISCV_INST_EBREAK)
            {
                *Reason = "ebreak cannot be stepped";
                return FALSE;
            }
            if (Inst == RISCV_INST_SRET)
            {
                *Reason = "sret target is the trap-return frame";
                return FALSE;
            }
            return TRUE;

        default:
            return TRUE;
    }
}

LONG
KdbpGetInstLength(IN ULONG_PTR Address)
{
    ULONG Inst;
    LONG Length;

    if (!KdbpRiscvReadInstruction(Address, &Inst, &Length))
        return -1;
    return Length;
}

static PCSTR
KdbpRiscvOpcodeClass(ULONG Inst)
{
    switch (Inst & 0x7F)
    {
        case RISCV_OPCODE_LOAD: return "load";
        case RISCV_OPCODE_MISC_MEM: return "fence";
        case RISCV_OPCODE_OP_IMM: return "op-imm";
        case RISCV_OPCODE_AUIPC: return "auipc";
        case RISCV_OPCODE_OP_IMM_32: return "op-imm-32";
        case RISCV_OPCODE_STORE: return "store";
        case RISCV_OPCODE_AMO: return "amo";
        case RISCV_OPCODE_OP: return "op";
        case RISCV_OPCODE_LUI: return "lui";
        case RISCV_OPCODE_OP_32: return "op-32";
        case RISCV_OPCODE_BRANCH: return "branch";
        case RISCV_OPCODE_JALR: return "jalr";
        case RISCV_OPCODE_JAL: return "jal";
        case RISCV_OPCODE_SYSTEM:
            if (Inst == RISCV_INST_ECALL) return "ecall";
            if (Inst == RISCV_INST_EBREAK) return "ebreak";
            if (Inst == RISCV_INST_SRET) return "sret";
            if (Inst == RISCV_INST_WFI) return "wfi";
            return "system";
        case 0x07: return "fp-load";
        case 0x27: return "fp-store";
        case 0x53: return "fp-op";
        default: return ".insn";
    }
}

LONG
KdbpDisassemble(IN ULONG_PTR Address, IN ULONG IntelSyntax)
{
    ULONG Inst;
    LONG Length;
    ULONG Opcode;

    UNREFERENCED_PARAMETER(IntelSyntax);

    if (!KdbpRiscvReadInstruction(Address, &Inst, &Length))
    {
        KdbpPrint("<unreadable>");
        return -1;
    }

    if (Length == 2)
    {
        if (Inst == RISCV_INST_C_EBREAK)
            KdbpPrint("    %04lx  c.ebreak", Inst);
        else
            KdbpPrint("    %04lx  c.insn (compressed, quadrant %lu funct3 %lu)", Inst, Inst & 3, (Inst >> 13) & 7);
        return Length;
    }

    Opcode = Inst & 0x7F;
    KdbpPrint("%08lx  %s", Inst, KdbpRiscvOpcodeClass(Inst));
    if (Opcode == RISCV_OPCODE_JAL)
    {
        KdbpPrint(" %s, 0x%p", KdbpRiscvRegisterNames[(Inst >> 7) & 31], (PVOID)(Address + KdbpRiscvJalOffset(Inst)));
    }
    else if (Opcode == RISCV_OPCODE_BRANCH)
    {
        KdbpPrint(" %s, %s, 0x%p", KdbpRiscvRegisterNames[(Inst >> 15) & 31], KdbpRiscvRegisterNames[(Inst >> 20) & 31], (PVOID)(Address + KdbpRiscvBranchOffset(Inst)));
    }
    else if (Opcode == RISCV_OPCODE_JALR)
    {
        KdbpPrint(" %s, %I64d(%s)", KdbpRiscvRegisterNames[(Inst >> 7) & 31], KdbpRiscvSignExtend(Inst >> 20, 12), KdbpRiscvRegisterNames[(Inst >> 15) & 31]);
    }
    return Length;
}

BOOLEAN
KdbpDisassemblerSelfTest(VOID)
{
    BOOLEAN Passed = TRUE;

    /* jal x0, +16 from 0x1000 and beq x1, x2, -8: encoders checked by hand. */
    if (KdbpRiscvJalOffset(0x0100006F) != 16)
        Passed = FALSE;
    if (KdbpRiscvBranchOffset(0xFE208CE3) != -8)
        Passed = FALSE;
    /* c.j -2 (0xBFFD) and c.beqz s0, +4 (0xC011). */
    if (KdbpRiscvCjOffset(0xBFFD) != -2)
        Passed = FALSE;
    if (KdbpRiscvCbOffset(0xC011) != 4)
        Passed = FALSE;
    return Passed;
}
