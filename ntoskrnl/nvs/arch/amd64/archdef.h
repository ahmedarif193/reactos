/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/archdef.h
 * PURPOSE:     AMD64 memory manager architecture definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define MI_ARCH_ID_VALUE        0x00000002
#define MI_ARCH_NAME            "x64"
#define MI_ARCH_PAGING_LEVELS   4
#define MI_ARCH_VA_BITS         48
#define MI_ARCH_PA_BITS         48
#define MI_ARCH_PTE_BYTES       8
#define MI_ARCH_PAGE_SHIFT      12
#define MI_ARCH_LARGE_LEVEL     1

#define MI_ARCH_L0_SHIFT       12
#define MI_ARCH_L0_BITS        9
#define MI_ARCH_L1_SHIFT       21
#define MI_ARCH_L1_BITS        9
#define MI_ARCH_L2_SHIFT       30
#define MI_ARCH_L2_BITS        9
#define MI_ARCH_L3_SHIFT       39
#define MI_ARCH_L3_BITS        9
#define MI_ARCH_L4_SHIFT       0
#define MI_ARCH_L4_BITS        0

#define MI_AMD64_DIRECT_BASE   0xFFFFA00000000000ULL
#define MI_AMD64_DIRECT_BYTES  (1ULL << 44)
#define MI_AMD64_SELF_INDEX    493
#define MI_AMD64_HYPER_INDEX   494
