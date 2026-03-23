/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Minimal x86-64 instruction decoder for MMIO emulation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * This is NOT a general-purpose instruction decoder. It only handles
 * the MOV/MOVZX instruction forms that compilers emit for volatile
 * MMIO register accesses. Unrecognized instructions cause the VM to
 * stop with a diagnostic error, which is correct - any guest MMIO
 * pattern we don't handle is a bug to fix, not silently ignore.
 *
 * Supported instruction forms:
 *   MOV r/m8, r8        (0x88)  - byte write
 *   MOV r/m16/32/64, r  (0x89)  - word/dword/qword write
 *   MOV r8, r/m8        (0x8A)  - byte read
 *   MOV r, r/m16/32/64  (0x8B)  - word/dword/qword read
 *   MOV r/m8, imm8      (0xC6)  - byte immediate write
 *   MOV r/m16/32/64, imm (0xC7) - word/dword/qword immediate write
 *   MOVZX r, r/m8       (0x0F B6) - zero-extending byte read
 *   MOVZX r, r/m16      (0x0F B7) - zero-extending word read
 */

#include <rosv/rosv.h>
#include <rosv/insn_decode.h>

#define NDEBUG
#include <debug.h>

/* ---- REX prefix bits ---------------------------------------------------- */

#define REX_PREFIX_BASE     0x40
#define REX_PREFIX_MASK     0xF0
#define REX_W               0x08    /* 64-bit operand size */
#define REX_R               0x04    /* ModR/M reg field extension */
#define REX_X               0x02    /* SIB index extension */
#define REX_B               0x01    /* ModR/M r/m or SIB base extension */

/* ---- ModR/M byte decoding ----------------------------------------------- */

#define MODRM_MOD(m)        (((m) >> 6) & 0x3)
#define MODRM_REG(m)        (((m) >> 3) & 0x7)
#define MODRM_RM(m)         ((m) & 0x7)

/* ---- Internal state for decoding ---------------------------------------- */

typedef struct _INSN_DECODE_STATE {
    const UCHAR *Bytes;
    ULONG Length;
    ULONG Pos;

    /* Prefix state */
    BOOLEAN HasRex;
    UCHAR Rex;
    BOOLEAN HasOperandSizeOverride;  /* 0x66 prefix */
    BOOLEAN HasAddressSizeOverride;  /* 0x67 prefix */
    BOOLEAN HasRepPrefix;            /* 0xF3 prefix */
    BOOLEAN HasRepnePrefix;          /* 0xF2 prefix */
    UCHAR SegmentOverride;           /* Segment override prefix (if any) */
} INSN_DECODE_STATE;

static
BOOLEAN
InsnFetchByte(
    _Inout_ INSN_DECODE_STATE *S,
    _Out_ UCHAR *Byte)
{
    if (S->Pos >= S->Length)
    {
        ROSV_ERR("InsnDecode: ran past instruction length (%u bytes)", S->Length);
        return FALSE;
    }
    *Byte = S->Bytes[S->Pos++];
    return TRUE;
}

static
BOOLEAN
InsnPeekByte(
    _In_ INSN_DECODE_STATE *S,
    _Out_ UCHAR *Byte)
{
    if (S->Pos >= S->Length)
        return FALSE;
    *Byte = S->Bytes[S->Pos];
    return TRUE;
}

/*
 * Consume all legacy prefixes and the optional REX prefix.
 * In x86-64, legacy prefixes can appear in any order, followed
 * by an optional REX prefix which must be immediately before the opcode.
 */
static
BOOLEAN
InsnParsePrefix(
    _Inout_ INSN_DECODE_STATE *S)
{
    UCHAR Byte;

    S->HasRex = FALSE;
    S->Rex = 0;
    S->HasOperandSizeOverride = FALSE;
    S->HasAddressSizeOverride = FALSE;
    S->HasRepPrefix = FALSE;
    S->HasRepnePrefix = FALSE;
    S->SegmentOverride = 0;

    for (;;)
    {
        if (!InsnPeekByte(S, &Byte))
            return FALSE;

        switch (Byte)
        {
            /* Operand-size override */
            case 0x66:
                S->HasOperandSizeOverride = TRUE;
                S->Pos++;
                continue;

            /* Address-size override */
            case 0x67:
                S->HasAddressSizeOverride = TRUE;
                S->Pos++;
                continue;

            /* REP/REPE prefix */
            case 0xF3:
                S->HasRepPrefix = TRUE;
                S->Pos++;
                continue;

            /* REPNE prefix */
            case 0xF2:
                S->HasRepnePrefix = TRUE;
                S->Pos++;
                continue;

            /* LOCK prefix */
            case 0xF0:
                S->Pos++;
                continue;

            /* Segment override prefixes (largely ignored in 64-bit mode) */
            case 0x26: case 0x2E: case 0x36: case 0x3E:
            case 0x64: case 0x65:
                S->SegmentOverride = Byte;
                S->Pos++;
                continue;

            default:
                break;
        }

        /* Check for REX prefix (0x40-0x4F in 64-bit mode) */
        if ((Byte & REX_PREFIX_MASK) == REX_PREFIX_BASE)
        {
            S->HasRex = TRUE;
            S->Rex = Byte;
            S->Pos++;
            /* REX must be immediately before the opcode - stop prefix parsing */
            break;
        }

        /* Not a prefix - this is the opcode byte */
        break;
    }

    return TRUE;
}

/*
 * Skip past the ModR/M byte and any SIB + displacement that follow it.
 * Returns the number of bytes consumed after (and including) the ModR/M byte.
 * This is needed to find where immediate operands start.
 */
static
ULONG
InsnModrmDisplacementSize(
    _In_ INSN_DECODE_STATE *S,
    _In_ UCHAR Modrm)
{
    ULONG Size = 1; /* The ModR/M byte itself */
    UCHAR Mod = MODRM_MOD(Modrm);
    UCHAR Rm = MODRM_RM(Modrm);

    if (Mod == 3)
    {
        /* Register-direct - no memory operand, no SIB/displacement */
        return Size;
    }

    /* Check for SIB byte: present when R/M = 4 (RSP encoding) and Mod != 3 */
    if (Rm == 4)
    {
        Size++; /* SIB byte */
        /* SIB with Mod=0 and base=5 means disp32 with no base.
         * S->Pos is right after ModR/M, so the SIB byte is at S->Bytes[S->Pos]. */
        if (Mod == 0)
        {
            if (S->Pos < S->Length)
            {
                UCHAR Sib = S->Bytes[S->Pos]; /* SIB is first byte after ModR/M */
                if ((Sib & 0x7) == 5)
                    Size += 4; /* disp32 */
            }
        }
        else if (Mod == 1)
        {
            Size += 1; /* disp8 */
        }
        else if (Mod == 2)
        {
            Size += 4; /* disp32 */
        }
    }
    else
    {
        /* No SIB byte */
        if (Mod == 0)
        {
            /* Mod=0, R/M=5 means RIP-relative (disp32) in 64-bit mode */
            if (Rm == 5)
                Size += 4; /* disp32 */
            /* Otherwise, [reg] with no displacement */
        }
        else if (Mod == 1)
        {
            Size += 1; /* disp8 */
        }
        else if (Mod == 2)
        {
            Size += 4; /* disp32 */
        }
    }

    return Size;
}

/*
 * Determine the operand size based on REX.W, 0x66 prefix, and opcode width bit.
 * For byte opcodes (0x88, 0x8A, 0xC6), size is always 1.
 * For word/dword/qword opcodes:
 *   - REX.W=1: 8 bytes (qword)
 *   - 0x66 prefix: 2 bytes (word)
 *   - Otherwise: 4 bytes (dword) - default in 64-bit mode
 */
static
ULONG
InsnOperandSize(
    _In_ INSN_DECODE_STATE *S,
    _In_ BOOLEAN IsByteOp)
{
    if (IsByteOp)
        return 1;

    if (S->HasRex && (S->Rex & REX_W))
        return 8;

    if (S->HasOperandSizeOverride)
        return 2;

    return 4;
}

/*
 * Extract the register index from ModR/M.reg field, extended by REX.R.
 * Returns 0-15 corresponding to RAX..R15.
 */
static
ULONG
InsnRegFromModrm(
    _In_ INSN_DECODE_STATE *S,
    _In_ UCHAR Modrm)
{
    ULONG Reg = MODRM_REG(Modrm);

    if (S->HasRex && (S->Rex & REX_R))
        Reg |= 8;

    return Reg;
}

/* ---- Public API --------------------------------------------------------- */

BOOLEAN
RosvMmioDecodeInstruction(
    _In_reads_(InsnLen) const UCHAR *InsnBytes,
    _In_ ULONG InsnLen,
    _Out_ PMMIO_INSN_INFO Info)
{
    INSN_DECODE_STATE S;
    UCHAR Opcode;
    UCHAR Modrm;
    ULONG ModrmSize;
    BOOLEAN IsByteOp;

    /* Initialize output */
    Info->Valid = FALSE;
    Info->AccessType = MmioAccessRead;
    Info->AccessSize = 0;
    Info->RegIndex = (ULONG)-1;
    Info->ImmediateValue = 0;
    Info->InstructionLength = InsnLen;

    if (InsnLen == 0 || InsnLen > MMIO_INSN_MAX_BYTES || InsnBytes == NULL)
    {
        ROSV_ERR("InsnDecode: invalid input (len=%u, bytes=%p)", InsnLen, InsnBytes);
        return FALSE;
    }

    /* Setup decode state */
    S.Bytes = InsnBytes;
    S.Length = InsnLen;
    S.Pos = 0;

    /* Parse prefixes */
    if (!InsnParsePrefix(&S))
    {
        ROSV_ERR("InsnDecode: prefix parsing failed");
        return FALSE;
    }

    /* Fetch opcode byte */
    if (!InsnFetchByte(&S, &Opcode))
    {
        ROSV_ERR("InsnDecode: no opcode byte");
        return FALSE;
    }

    /* Two-byte opcode escape (0x0F) */
    if (Opcode == 0x0F)
    {
        UCHAR Opcode2;
        if (!InsnFetchByte(&S, &Opcode2))
        {
            ROSV_ERR("InsnDecode: truncated 2-byte opcode");
            return FALSE;
        }

        switch (Opcode2)
        {
            case 0xB6: /* MOVZX r32/64, r/m8 */
            {
                if (!InsnFetchByte(&S, &Modrm))
                    return FALSE;

                if (MODRM_MOD(Modrm) == 3)
                {
                    /* Register-to-register, not a memory access */
                    ROSV_ERR("InsnDecode: MOVZX r/m8 with Mod=3 (reg-reg, not MMIO)");
                    return FALSE;
                }

                Info->AccessType = MmioAccessRead;
                Info->AccessSize = 1; /* Source is byte, but destination is dword/qword */
                Info->RegIndex = InsnRegFromModrm(&S, Modrm);
                Info->Valid = TRUE;
                return TRUE;
            }

            case 0xB7: /* MOVZX r32/64, r/m16 */
            {
                if (!InsnFetchByte(&S, &Modrm))
                    return FALSE;

                if (MODRM_MOD(Modrm) == 3)
                {
                    ROSV_ERR("InsnDecode: MOVZX r/m16 with Mod=3 (reg-reg, not MMIO)");
                    return FALSE;
                }

                Info->AccessType = MmioAccessRead;
                Info->AccessSize = 2; /* Source is word */
                Info->RegIndex = InsnRegFromModrm(&S, Modrm);
                Info->Valid = TRUE;
                return TRUE;
            }

            default:
                ROSV_ERR("InsnDecode: unhandled 2-byte opcode 0F %02X", Opcode2);
                return FALSE;
        }
    }

    /* Single-byte opcodes */
    switch (Opcode)
    {
        case 0x88: /* MOV r/m8, r8 (byte write) */
        case 0x89: /* MOV r/m16/32/64, r (write) */
        {
            IsByteOp = (Opcode == 0x88);

            if (!InsnFetchByte(&S, &Modrm))
                return FALSE;

            if (MODRM_MOD(Modrm) == 3)
            {
                ROSV_ERR("InsnDecode: MOV r/m,r with Mod=3 (reg-reg, not MMIO)");
                return FALSE;
            }

            Info->AccessType = MmioAccessWrite;
            Info->AccessSize = InsnOperandSize(&S, IsByteOp);
            Info->RegIndex = InsnRegFromModrm(&S, Modrm);
            Info->Valid = TRUE;
            return TRUE;
        }

        case 0x8A: /* MOV r8, r/m8 (byte read) */
        case 0x8B: /* MOV r16/32/64, r/m (read) */
        {
            IsByteOp = (Opcode == 0x8A);

            if (!InsnFetchByte(&S, &Modrm))
                return FALSE;

            if (MODRM_MOD(Modrm) == 3)
            {
                ROSV_ERR("InsnDecode: MOV r,r/m with Mod=3 (reg-reg, not MMIO)");
                return FALSE;
            }

            Info->AccessType = MmioAccessRead;
            Info->AccessSize = InsnOperandSize(&S, IsByteOp);
            Info->RegIndex = InsnRegFromModrm(&S, Modrm);
            Info->Valid = TRUE;
            return TRUE;
        }

        case 0xC6: /* MOV r/m8, imm8 (byte immediate write) */
        case 0xC7: /* MOV r/m16/32/64, imm16/32 (immediate write) */
        {
            ULONG ImmSize;

            IsByteOp = (Opcode == 0xC6);

            if (!InsnFetchByte(&S, &Modrm))
                return FALSE;

            /* Must be /0 (reg field = 0 for MOV) */
            if (MODRM_REG(Modrm) != 0)
            {
                ROSV_ERR("InsnDecode: opcode %02X with reg=%u (not MOV)", Opcode, MODRM_REG(Modrm));
                return FALSE;
            }

            if (MODRM_MOD(Modrm) == 3)
            {
                ROSV_ERR("InsnDecode: MOV r/m,imm with Mod=3 (reg-reg, not MMIO)");
                return FALSE;
            }

            Info->AccessType = MmioAccessWrite;
            Info->AccessSize = InsnOperandSize(&S, IsByteOp);
            Info->RegIndex = (ULONG)-1; /* Immediate source, no register */

            /* Skip ModR/M + SIB + displacement to find the immediate */
            ModrmSize = InsnModrmDisplacementSize(&S, Modrm);
            /* We already consumed ModR/M (1 byte), skip the rest */
            S.Pos += (ModrmSize - 1);

            /* Read the immediate value */
            if (IsByteOp)
            {
                ImmSize = 1;
            }
            else
            {
                /* In 64-bit mode, MOV r/m64, imm32 sign-extends the immediate.
                 * The immediate is always 16 or 32 bits, never 64. */
                ImmSize = (Info->AccessSize <= 2) ? Info->AccessSize : 4;
            }

            Info->ImmediateValue = 0;
            if (S.Pos + ImmSize > S.Length)
            {
                ROSV_ERR("InsnDecode: truncated immediate (need %u bytes at pos %u, have %u)",
                         ImmSize, S.Pos, S.Length);
                return FALSE;
            }

            /* Read immediate (little-endian) */
            switch (ImmSize)
            {
                case 1:
                    Info->ImmediateValue = S.Bytes[S.Pos];
                    /* Sign-extend for byte writes */
                    break;
                case 2:
                    Info->ImmediateValue = (ULONG64)S.Bytes[S.Pos] |
                                           ((ULONG64)S.Bytes[S.Pos + 1] << 8);
                    break;
                case 4:
                    Info->ImmediateValue = (ULONG64)S.Bytes[S.Pos] |
                                           ((ULONG64)S.Bytes[S.Pos + 1] << 8) |
                                           ((ULONG64)S.Bytes[S.Pos + 2] << 16) |
                                           ((ULONG64)S.Bytes[S.Pos + 3] << 24);
                    /* Sign-extend 32-bit immediate for 64-bit operations (REX.W) */
                    if (Info->AccessSize == 8 && (Info->ImmediateValue & 0x80000000))
                        Info->ImmediateValue |= 0xFFFFFFFF00000000ULL;
                    break;
            }

            Info->Valid = TRUE;
            return TRUE;
        }

        default:
            ROSV_ERR("InsnDecode: unhandled opcode 0x%02X at byte[%u] (prefixes: REX=%s%02X, 66=%u)",
                     Opcode, S.Pos - 1,
                     S.HasRex ? "0x" : "none/", S.HasRex ? S.Rex : 0,
                     (ULONG)S.HasOperandSizeOverride);
            /* Hex dump for debugging - dump up to first 8 bytes */
            if (InsnLen >= 8)
            {
                ROSV_ERR("InsnDecode: bytes[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X",
                         InsnBytes[0], InsnBytes[1], InsnBytes[2], InsnBytes[3],
                         InsnBytes[4], InsnBytes[5], InsnBytes[6], InsnBytes[7]);
            }
            else
            {
                /* Dump whatever bytes we have */
                ULONG i;
                for (i = 0; i < InsnLen; i++)
                {
                    ROSV_ERR("InsnDecode: byte[%u] = 0x%02X", i, InsnBytes[i]);
                }
            }
            return FALSE;
    }
}
