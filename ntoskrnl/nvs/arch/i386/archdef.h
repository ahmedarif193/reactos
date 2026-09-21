/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/i386/archdef.h
 * PURPOSE:     i386 memory manager architecture definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define MI_ARCH_ID_VALUE        0x00000001
#define MI_ARCH_NAME            "x86"
#define MI_ARCH_PAGING_LEVELS   2
#define MI_ARCH_VA_BITS         32
#define MI_ARCH_PA_BITS         32
#define MI_ARCH_PTE_BYTES       4
#define MI_ARCH_PAGE_SHIFT      12
#define MI_ARCH_LARGE_LEVEL     1

#define MI_ARCH_L0_SHIFT       12
#define MI_ARCH_L0_BITS        10
#define MI_ARCH_L1_SHIFT       22
#define MI_ARCH_L1_BITS        10
#define MI_ARCH_L2_SHIFT       0
#define MI_ARCH_L2_BITS        0
#define MI_ARCH_L3_SHIFT       0
#define MI_ARCH_L3_BITS        0
#define MI_ARCH_L4_SHIFT       0
#define MI_ARCH_L4_BITS        0
