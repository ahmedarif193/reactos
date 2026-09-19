/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/riscv64/archdef.h
 * PURPOSE:     RISC-V 64-bit memory manager architecture definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

/* Sv39: three levels of 512 entries over a 39-bit virtual address. */
#define MI_ARCH_ID_VALUE        0x00000005
#define MI_ARCH_NAME            "riscv64"
#define MI_ARCH_PAGING_LEVELS   3
#define MI_ARCH_VA_BITS         39
#define MI_ARCH_PA_BITS         56
#define MI_ARCH_PTE_BYTES       8
#define MI_ARCH_PAGE_SHIFT      12
#define MI_ARCH_LARGE_LEVEL     1

#define MI_ARCH_L0_SHIFT       12
#define MI_ARCH_L0_BITS        9
#define MI_ARCH_L1_SHIFT       21
#define MI_ARCH_L1_BITS        9
#define MI_ARCH_L2_SHIFT       30
#define MI_ARCH_L2_BITS        9
#define MI_ARCH_L3_SHIFT       0
#define MI_ARCH_L3_BITS        0
#define MI_ARCH_L4_SHIFT       0
#define MI_ARCH_L4_BITS        0

/* Root entries below this index translate the user half. */
#define MI_RISCV_ROOT_KERNEL_INDEX 256
