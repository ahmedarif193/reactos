/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/riscv64/hardware.h
 * PURPOSE:     RISC-V 64-bit memory management hardware definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "archdef.h"

/* Set once the boot hart reported Svpbmt; firmware enables it with the
 * extension. Without it, physical memory attributes govern caching. */
extern BOOLEAN MiRiscvPbmtEnabled;

FORCEINLINE
BOOLEAN
MiRiscvPteIsTable(_In_ MI_PTE Pte)
{
    return (Pte & (MI_RISCV_PTE_VALID | MI_RISCV_PTE_LEAF_MASK)) == MI_RISCV_PTE_VALID;
}

FORCEINLINE
BOOLEAN
MiRiscvPteIsLeaf(_In_ MI_PTE Pte)
{
    return (Pte & MI_RISCV_PTE_VALID) && (Pte & MI_RISCV_PTE_LEAF_MASK);
}
