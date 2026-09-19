/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Transactional RISC-V64 RVUW version 1 frame decoder
 */

#pragma once

#include "unwind.h"

#define RTL_RISCV64_MAX_RVUW_RECORD_SIZE (1024UL * 1024UL)
#define RTL_RISCV64_MAX_EPILOG_SCOPES 4096
#define RTL_RISCV64_MAX_CHAIN_DEPTH 8
#define RTL_RISCV64_UNWIND_FLAG_MASK                                            \
    (RISCV64_UNW_FLAG_EHANDLER | RISCV64_UNW_FLAG_UHANDLER |                   \
     RISCV64_UNW_FLAG_CHAININFO)
#define RTL_RISCV64_UNWIND_STATE_MASK                                           \
    (RISCV64_UNW_STATE_INTEGER | RISCV64_UNW_STATE_FLOATING |                  \
     RISCV64_UNW_STATE_VECTOR)
#define RTL_RISCV64_OPERAND_MASK 0x001FFFFFUL

typedef struct _RTL_RISCV64_RECORD_LAYOUT
{
    RISCV64_UNWIND_INFO_V1 Header;
    ULONG PrologCodesRva;
    ULONG EpilogScopesRva;
    ULONG EpilogCodesRva;
    ULONG TailRva;
} RTL_RISCV64_RECORD_LAYOUT;

BOOLEAN RtlpRiscv64AddUnsigned(ULONG64 Base, ULONG64 Offset, PULONG64 Result);
NTSTATUS RtlpRiscv64ReadImage(const RTL_RISCV64_UNWIND_VIEW *View,
                             ULONG Rva, PVOID Buffer, SIZE_T Size);
NTSTATUS RtlpRiscv64ReadRecordLayout(const RTL_RISCV64_UNWIND_VIEW *View,
                                    const RUNTIME_FUNCTION *Entry,
                                    RTL_RISCV64_RECORD_LAYOUT *Layout);
BOOLEAN RtlpRiscv64SameFunctionEntry(const RUNTIME_FUNCTION *Left,
                                    const RUNTIME_FUNCTION *Right);
