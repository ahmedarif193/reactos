/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Minimal x86-64 instruction decoder for MMIO emulation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Decodes guest instructions that faulted on an EPT violation to
 * determine the register, access size, and direction for MMIO
 * read/write emulation. Only handles the instruction forms commonly
 * emitted by compilers for volatile MMIO accesses.
 */

#pragma once

#include <rosv/rosv.h>

/* Maximum instruction bytes we fetch for decoding */
#define MMIO_INSN_MAX_BYTES     15

/* MMIO access type */
typedef enum _MMIO_ACCESS_TYPE {
    MmioAccessRead = 0,     /* MOV reg, [mem] / MOVZX reg, [mem] */
    MmioAccessWrite = 1,    /* MOV [mem], reg  / MOV [mem], imm  */
} MMIO_ACCESS_TYPE;

/* Decoded MMIO instruction info */
typedef struct _MMIO_INSN_INFO {
    MMIO_ACCESS_TYPE AccessType;
    ULONG AccessSize;           /* 1, 2, 4, or 8 bytes */
    ULONG RegIndex;             /* GPR index (0=RAX..15=R15), or (ULONG)-1 for immediate */
    ULONG64 ImmediateValue;     /* Only valid when RegIndex == (ULONG)-1 */
    ULONG InstructionLength;    /* Total instruction length in bytes */
    BOOLEAN Valid;              /* TRUE if decode succeeded */
} MMIO_INSN_INFO, *PMMIO_INSN_INFO;

/*
 * Decode an x86-64 instruction at InsnBytes[0..InsnLen-1] that caused
 * an EPT violation. Fills in Info with the access type, size, register,
 * and instruction length.
 *
 * Returns TRUE on success, FALSE if the instruction is not recognized.
 * InsnLen is the instruction length from VMCS (VMCS_RO_EXIT_INSTR_LENGTH).
 */
BOOLEAN
RosvMmioDecodeInstruction(
    _In_reads_(InsnLen) const UCHAR *InsnBytes,
    _In_ ULONG InsnLen,
    _Out_ PMMIO_INSN_INFO Info);
