/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/ppc/archdef.h
 * PURPOSE:     Windows NT PowerPC memory manager architecture definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

/* Software page tables with 64-bit entries: a 4-entry root (VA[31:30]) and
 * two 512-entry levels. The hashed page table caches them. */
#define MI_ARCH_ID_VALUE        0x00000006
#define MI_ARCH_NAME            "ppc"
#define MI_ARCH_PAGING_LEVELS   3
#define MI_ARCH_VA_BITS         32
#define MI_ARCH_PA_BITS         32
#define MI_ARCH_PTE_BYTES       8
#define MI_ARCH_PAGE_SHIFT      12
#define MI_ARCH_LARGE_LEVEL     1

/* KSEG0 is a block translation with no page tables behind it. */
#define MI_ARCH_HAS_TRANSLATION_WINDOW

#define MI_ARCH_L0_SHIFT       12
#define MI_ARCH_L0_BITS        9
#define MI_ARCH_L1_SHIFT       21
#define MI_ARCH_L1_BITS        9
#define MI_ARCH_L2_SHIFT       30
#define MI_ARCH_L2_BITS        2
#define MI_ARCH_L3_SHIFT       0
#define MI_ARCH_L3_BITS        0
#define MI_ARCH_L4_SHIFT       0
#define MI_ARCH_L4_BITS        0
