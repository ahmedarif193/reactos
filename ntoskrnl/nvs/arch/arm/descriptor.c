/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm/descriptor.c
 * PURPOSE:     ARM memory manager architecture descriptor
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/nvsenv.h>
#include "archdef.h"
#include <nvs/include/miarchcheck.h>

static const MI_ARCH_DESCRIPTOR MiArchDescriptor =
{
    MI_ARCH_ID_VALUE,
    MI_ARCH_NAME,
    MI_ARCH_PAGING_LEVELS,
    MI_ARCH_VA_BITS,
    MI_ARCH_PA_BITS,
    MI_ARCH_PTE_BYTES,
    PAGE_SIZE,
    MI_ARCH_PAGE_SHIFT,
    (1ULL * 1024 * 1024),
    MI_ARCH_LARGE_LEVEL,
    {
        { MI_ARCH_L0_SHIFT, MI_ARCH_L0_BITS, (1ULL << MI_ARCH_L0_BITS) - 1, 1ULL << MI_ARCH_L0_BITS },
        { MI_ARCH_L1_SHIFT, MI_ARCH_L1_BITS, (1ULL << MI_ARCH_L1_BITS) - 1, 1ULL << MI_ARCH_L1_BITS },
        { MI_ARCH_L2_SHIFT, MI_ARCH_L2_BITS, (1ULL << MI_ARCH_L2_BITS) - 1, 1ULL << MI_ARCH_L2_BITS },
        { MI_ARCH_L3_SHIFT, MI_ARCH_L3_BITS, (1ULL << MI_ARCH_L3_BITS) - 1, 1ULL << MI_ARCH_L3_BITS },
        { MI_ARCH_L4_SHIFT, MI_ARCH_L4_BITS, (1ULL << MI_ARCH_L4_BITS) - 1, 1ULL << MI_ARCH_L4_BITS },
    },
    0x00000000ULL,
    0x7FFEFFFFULL,
    0x80000000ULL,
    0xFFFFFFFFULL,
    FALSE,
    FALSE,
    FALSE,
    TRUE,
    TRUE,
    FALSE,
    TRUE,
    TRUE,
    TRUE,
    TRUE,
    NULL,
    0
};

const MI_ARCH_DESCRIPTOR *
MiArchDescribe(VOID)
{
    return &MiArchDescriptor;
}
