/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         AArch64 disassembler integration for KDBG
 * LICENSE:         GPL-2.0-or-later AND BSD-3-Clause
 */

#include <ntoskrnl.h>
#include "kdbg/kdb.h"

#define NDEBUG
#include <debug.h>

/* The decoder is allocation-free and never reads target memory itself. */
#include "arm64-disarm-impl.inc"

#define KDB_ARM64_SYSREG(Op0, Op1, CRn, CRm, Op2) \
    (USHORT)(((Op0) << 14) | ((Op1) << 11) | ((CRn) << 7) | ((CRm) << 3) | (Op2))

typedef struct _KDB_ARM64_SYSREG_NAME
{
    USHORT Encoding;
    PCSTR Name;
} KDB_ARM64_SYSREG_NAME;

static const KDB_ARM64_SYSREG_NAME KdbArm64SystemRegisters[] =
{
    { KDB_ARM64_SYSREG(3, 0, 0, 0, 0), "midr_el1" },
    { KDB_ARM64_SYSREG(3, 0, 0, 0, 5), "mpidr_el1" },
    { KDB_ARM64_SYSREG(3, 0, 0, 4, 0), "id_aa64pfr0_el1" },
    { KDB_ARM64_SYSREG(3, 0, 0, 5, 0), "id_aa64dfr0_el1" },
    { KDB_ARM64_SYSREG(3, 0, 0, 6, 0), "id_aa64isar0_el1" },
    { KDB_ARM64_SYSREG(3, 0, 0, 7, 0), "id_aa64mmfr0_el1" },
    { KDB_ARM64_SYSREG(3, 0, 1, 0, 0), "sctlr_el1" },
    { KDB_ARM64_SYSREG(3, 0, 1, 0, 2), "cpacr_el1" },
    { KDB_ARM64_SYSREG(3, 0, 2, 0, 0), "ttbr0_el1" },
    { KDB_ARM64_SYSREG(3, 0, 2, 0, 1), "ttbr1_el1" },
    { KDB_ARM64_SYSREG(3, 0, 2, 0, 2), "tcr_el1" },
    { KDB_ARM64_SYSREG(3, 0, 4, 0, 0), "spsr_el1" },
    { KDB_ARM64_SYSREG(3, 0, 4, 0, 1), "elr_el1" },
    { KDB_ARM64_SYSREG(3, 0, 4, 1, 0), "sp_el0" },
    { KDB_ARM64_SYSREG(3, 0, 4, 2, 2), "currentel" },
    { KDB_ARM64_SYSREG(3, 0, 5, 2, 0), "esr_el1" },
    { KDB_ARM64_SYSREG(3, 0, 6, 0, 0), "far_el1" },
    { KDB_ARM64_SYSREG(3, 0, 10, 2, 0), "mair_el1" },
    { KDB_ARM64_SYSREG(3, 0, 12, 0, 0), "vbar_el1" },
    { KDB_ARM64_SYSREG(3, 0, 13, 0, 4), "tpidr_el1" },
    { KDB_ARM64_SYSREG(3, 3, 0, 0, 1), "ctr_el0" },
    { KDB_ARM64_SYSREG(3, 3, 0, 0, 7), "dczid_el0" },
    { KDB_ARM64_SYSREG(3, 3, 4, 2, 0), "nzcv" },
    { KDB_ARM64_SYSREG(3, 3, 4, 2, 1), "daif" },
    { KDB_ARM64_SYSREG(3, 3, 4, 4, 0), "fpcr" },
    { KDB_ARM64_SYSREG(3, 3, 4, 4, 1), "fpsr" },
    { KDB_ARM64_SYSREG(3, 3, 13, 0, 2), "tpidr_el0" },
    { KDB_ARM64_SYSREG(3, 3, 13, 0, 3), "tpidrro_el0" },
    { KDB_ARM64_SYSREG(3, 3, 14, 0, 0), "cntfrq_el0" },
    { KDB_ARM64_SYSREG(3, 3, 14, 0, 1), "cntpct_el0" },
    { KDB_ARM64_SYSREG(3, 3, 14, 0, 2), "cntvct_el0" },
    { KDB_ARM64_SYSREG(3, 3, 14, 2, 0), "cntp_tval_el0" },
    { KDB_ARM64_SYSREG(3, 3, 14, 2, 1), "cntp_ctl_el0" },
    { KDB_ARM64_SYSREG(3, 3, 14, 2, 2), "cntp_cval_el0" },
    { KDB_ARM64_SYSREG(3, 3, 14, 3, 0), "cntv_tval_el0" },
    { KDB_ARM64_SYSREG(3, 3, 14, 3, 1), "cntv_ctl_el0" },
    { KDB_ARM64_SYSREG(3, 3, 14, 3, 2), "cntv_cval_el0" }
};

static PCSTR
KdbpArm64SystemRegisterName(_In_ USHORT Encoding, _Out_writes_(BufferSize) PCHAR Buffer, _In_ ULONG BufferSize)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(KdbArm64SystemRegisters); Index++)
    {
        if (KdbArm64SystemRegisters[Index].Encoding == Encoding)
            return KdbArm64SystemRegisters[Index].Name;
    }

    _snprintf(Buffer, BufferSize, "s%u_%u_c%u_c%u_%u", (Encoding >> 14) & 3, (Encoding >> 11) & 7, (Encoding >> 7) & 15, (Encoding >> 3) & 15, Encoding & 7);
    Buffer[BufferSize - 1] = ANSI_NULL;
    return Buffer;
}

static PCSTR
KdbpArm64HintName(_In_ USHORT Hint)
{
    switch (Hint)
    {
        case 0x00: return "nop";
        case 0x01: return "yield";
        case 0x02: return "wfe";
        case 0x03: return "wfi";
        case 0x04: return "sev";
        case 0x05: return "sevl";
        case 0x07: return "xpaclri";
        case 0x08: return "pacia1716";
        case 0x0a: return "pacib1716";
        case 0x0c: return "autia1716";
        case 0x0e: return "autib1716";
        case 0x10: return "esb";
        case 0x11: return "psb csync";
        case 0x12: return "tsb csync";
        case 0x14: return "csdb";
        case 0x16: return "dgh";
        case 0x18: return "paciaz";
        case 0x19: return "paciasp";
        case 0x1a: return "pacibz";
        case 0x1b: return "pacibsp";
        case 0x1c: return "autiaz";
        case 0x1d: return "autiasp";
        case 0x1e: return "autibz";
        case 0x1f: return "autibsp";
        case 0x20: return "bti";
        case 0x22: return "bti c";
        case 0x24: return "bti j";
        case 0x26: return "bti jc";
        default: return NULL;
    }
}

static PCSTR
KdbpArm64BarrierName(_In_ USHORT Barrier)
{
    static const PCSTR Names[16] =
    {
        NULL, "oshld", "oshst", "osh", NULL, "nshld", "nshst", "nsh",
        NULL, "ishld", "ishst", "ish", NULL, "ld", "st", "sy"
    };

    return Barrier < RTL_NUMBER_OF(Names) ? Names[Barrier] : NULL;
}

static PCSTR
KdbpArm64GeneralRegisterName(_In_ ULONG Register, _Out_writes_(BufferSize) PCHAR Buffer, _In_ ULONG BufferSize)
{
    if (Register == 31)
        return "xzr";
    _snprintf(Buffer, BufferSize, "x%u", Register);
    Buffer[BufferSize - 1] = ANSI_NULL;
    return Buffer;
}

static VOID
KdbpArm64FormatInstruction(_In_ const struct Da64Inst *Decoded, _In_ ULONG_PTR Address, _Out_writes_(128) PCHAR Buffer)
{
    CHAR RegisterName[32];
    CHAR GeneralRegister[8];
    PCSTR Name;
    PCSTR RegisterText;
    ULONG Length;
    USHORT Encoding;
    ULONG Register;

    if (Decoded->mnem == DA64I_UNKNOWN)
    {
        Buffer[0] = ANSI_NULL;
        return;
    }

    if (Decoded->mnem == DA64I_MRS || Decoded->mnem == DA64I_MSR)
    {
        BOOLEAN Read = Decoded->mnem == DA64I_MRS;
        Register = Decoded->ops[Read ? 0 : 1].reg;
        Encoding = Decoded->ops[Read ? 1 : 0].sysreg;
        RegisterText = KdbpArm64GeneralRegisterName(Register, GeneralRegister, sizeof(GeneralRegister));
        Name = KdbpArm64SystemRegisterName(Encoding, RegisterName, sizeof(RegisterName));
        if (Read)
            _snprintf(Buffer, 128, "mrs %s, %s", RegisterText, Name);
        else
            _snprintf(Buffer, 128, "msr %s, %s", Name, RegisterText);
        Buffer[127] = ANSI_NULL;
        return;
    }

    if (Decoded->mnem == DA64I_SYS || Decoded->mnem == DA64I_SYSL)
    {
        BOOLEAN Load = Decoded->mnem == DA64I_SYSL;
        Register = Decoded->ops[Load ? 0 : 1].reg;
        Encoding = Decoded->ops[Load ? 1 : 0].sysreg;
        RegisterText = KdbpArm64GeneralRegisterName(Register, GeneralRegister, sizeof(GeneralRegister));
        if (Load)
            _snprintf(Buffer, 128, "sysl %s, #%u, c%u, c%u, #%u", RegisterText, (Encoding >> 11) & 7, (Encoding >> 7) & 15, (Encoding >> 3) & 15, Encoding & 7);
        else
            _snprintf(Buffer, 128, "sys #%u, c%u, c%u, #%u, %s", (Encoding >> 11) & 7, (Encoding >> 7) & 15, (Encoding >> 3) & 15, Encoding & 7, RegisterText);
        Buffer[127] = ANSI_NULL;
        return;
    }

    if (Decoded->mnem == DA64I_HINT)
    {
        Name = KdbpArm64HintName(Decoded->ops[0].uimm16);
        if (Name != NULL)
        {
            _snprintf(Buffer, 128, "%s", Name);
            return;
        }
    }

    if (Decoded->mnem == DA64I_DMB || Decoded->mnem == DA64I_DSB)
    {
        Name = KdbpArm64BarrierName(Decoded->ops[0].uimm16);
        if (Name != NULL)
        {
            _snprintf(Buffer, 128, "%s %s", Decoded->mnem == DA64I_DMB ? "dmb" : "dsb", Name);
            return;
        }
    }

    if (Decoded->mnem == DA64I_ISB && Decoded->ops[0].uimm16 == 15)
    {
        _snprintf(Buffer, 128, "isb");
        return;
    }

    if (Decoded->mnem == DA64I_RET && Decoded->ops[0].reg == 30)
    {
        _snprintf(Buffer, 128, "ret");
        return;
    }

    da64_format_abs(Decoded, Address, Buffer);
    Length = strlen(Buffer);
    if (Length >= sizeof(", lsl #0") - 1 &&
        strcmp(Buffer + Length - (sizeof(", lsl #0") - 1), ", lsl #0") == 0)
    {
        Buffer[Length - (sizeof(", lsl #0") - 1)] = ANSI_NULL;
    }
}

LONG
KdbpGetInstLength(IN ULONG_PTR Address)
{
    UNREFERENCED_PARAMETER(Address);
    return sizeof(ULONG);
}

LONG
KdbpDisassemble(IN ULONG_PTR Address, IN ULONG IntelSyntax)
{
    struct Da64Inst Decoded;
    CHAR Buffer[128];
    ULONG Inst;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(IntelSyntax);

    Status = KdbpSafeReadMemory(&Inst, (PVOID)Address, sizeof(Inst));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("<unreadable: 0x%08lx>", Status);
        return -1;
    }

    da64_decode(Inst, &Decoded);
    KdbpArm64FormatInstruction(&Decoded, Address, Buffer);
    if (Buffer[0] == ANSI_NULL)
        KdbpPrint("%08lx  .inst 0x%08lx", Inst, Inst);
    else
        KdbpPrint("%08lx  %s", Inst, Buffer);
    return sizeof(ULONG);
}

BOOLEAN
KdbpDisassemblerSelfTest(VOID)
{
    typedef struct _KDB_ARM64_DISASM_TEST
    {
        ULONG Instruction;
        ULONG_PTR Address;
        enum Da64InstKind Kind;
        PCSTR Text;
    } KDB_ARM64_DISASM_TEST;
    static const KDB_ARM64_DISASM_TEST Tests[] =
    {
        { 0xd503201f, 0x1000, DA64I_HINT, "nop" },
        { 0x8b020020, 0x1000, DA64I_ADD_SHIFT, "add x0, x1, x2" },
        { 0xd10083ff, 0x1000, DA64I_SUB_IMM, "sub sp, sp, #0x20" },
        { 0xd2a24683, 0x1000, DA64I_MOVZ, "movz x3, #0x1234, lsl #16" },
        { 0x729579a4, 0x1000, DA64I_MOVK, "movk w4, #0xabcd" },
        { 0x90000025, 0x1000, DA64I_ADRP, "adrp x5, #0x5000" },
        { 0xf9400ce6, 0x1000, DA64I_LDR_IMM, "ldr x6, [x7, #0x18]" },
        { 0xb81f0fe8, 0x1000, DA64I_STRW_PRE, "str w8, [sp, #-0x10]!" },
        { 0xa9bf7bfd, 0x1000, DA64I_STPX_PRE, "stp x29, x30, [sp, #-0x10]!" },
        { 0xa8c17bfd, 0x1000, DA64I_LDPX_POST, "ldp x29, x30, [sp], #0x10" },
        { 0x14000010, 0x1000, DA64I_B, "b #0x1040" },
        { 0x94000020, 0x1000, DA64I_BL, "bl #0x1080" },
        { 0xb4000109, 0x1000, DA64I_CBZ, "cbz x9, #0x1020" },
        { 0x3618008a, 0x1000, DA64I_TBZ, "tbz w10, #3, #0x1010" },
        { 0xd61f0160, 0x1000, DA64I_BR, "br x11" },
        { 0xd65f03c0, 0x1000, DA64I_RET, "ret" },
        { 0xd53bd04c, 0x1000, DA64I_MRS, "mrs x12, tpidr_el0" },
        { 0xd50b7522, 0x1000, DA64I_SYS, "sys #3, c7, c5, #1, x2" },
        { 0xd5297803, 0x1000, DA64I_SYSL, "sysl x3, #1, c7, c8, #0" },
        { 0xd538f2e4, 0x1000, DA64I_MRS, "mrs x4, s3_0_c15_c2_7" },
        { 0xd518f2e5, 0x1000, DA64I_MSR, "msr s3_0_c15_c2_7, x5" },
        { 0xd50342df, 0x1000, DA64I_MSR_DAIFSet, "msr daifset, #2" },
        { 0xd5033bbf, 0x1000, DA64I_DMB, "dmb ish" },
        { 0xd5033fdf, 0x1000, DA64I_ISB, "isb" },
        { 0xd5033f9f, 0x1000, DA64I_DSB, "dsb sy" },
        { 0xc85ffdcd, 0x1000, DA64I_LDAXRX, "ldaxr x13, [x14]" },
        { 0xc80ffe30, 0x1000, DA64I_STLXRX, "stlxr w15, x16, [x17]" },
        { 0x1e622820, 0x1000, DA64I_FADD, "fadd d0, d1, d2" },
        { 0x9e660072, 0x1000, DA64I_FMOV_TOGP, "fmov x18, d3" },
        { 0x4ea684a4, 0x1000, DA64I_ADD_VEC, "add v4.4s, v5.4s, v6.4s" },
        { 0x4e0a2107, 0x1000, DA64I_TBL2, "tbl v7.16b, {v8.16b-v9.16b}, v10.16b" },
        { 0x9ad55e93, 0x1000, DA64I_CRC32CX, "crc32cx w19, w20, x21" },
        { 0xd503233f, 0x1000, DA64I_HINT, "paciasp" },
        { 0xd50323bf, 0x1000, DA64I_HINT, "autiasp" }
    };
    struct Da64Inst Decoded;
    CHAR Buffer[128];
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Tests); Index++)
    {
        da64_decode(Tests[Index].Instruction, &Decoded);
        KdbpArm64FormatInstruction(&Decoded, Tests[Index].Address, Buffer);
        if (Decoded.mnem != Tests[Index].Kind || strcmp(Buffer, Tests[Index].Text) != 0)
        {
            KdbpPrint("selftest: ARM64 decoder case %lu failed: kind 0x%x, text '%s'.\n", Index, Decoded.mnem, Buffer);
            return FALSE;
        }
    }
    return TRUE;
}

#undef KDB_ARM64_SYSREG
