/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/miarch.h
 * PURPOSE:     Memory manager architecture backend interface
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define MI_MAX_PAGING_LEVELS 5

typedef enum _MI_TLB_SCOPE
{
    MiTlbLocal = 0,
    MiTlbAllProcessors
} MI_TLB_SCOPE;

typedef struct _MI_LEVEL_DESCRIPTOR
{
    UCHAR Shift;
    UCHAR IndexBits;
    ULONG64 IndexMask;
    ULONG64 EntryCount;
} MI_LEVEL_DESCRIPTOR, *PMI_LEVEL_DESCRIPTOR;

typedef struct _MI_ARCH_DESCRIPTOR
{
    ULONG ArchId;
    PCSTR Name;

    UCHAR PagingLevels;
    UCHAR VirtualAddressBits;
    UCHAR PhysicalAddressBits;
    UCHAR PteBytes;

    ULONG PageSize;
    ULONG PageShift;
    ULONG64 LargePageSize;
    UCHAR LargePageLevel;

    MI_LEVEL_DESCRIPTOR Level[MI_MAX_PAGING_LEVELS];

    ULONG64 UserAddressStart;
    ULONG64 UserAddressEnd;
    ULONG64 SystemAddressStart;
    ULONG64 SystemAddressEnd;

    BOOLEAN HasRecursiveMap;
    BOOLEAN HasHardwareDirtyBit;
    BOOLEAN HasHardwareAccessedBit;
    BOOLEAN RequiresBreakBeforeMake;
    BOOLEAN RequiresExplicitIcacheSync;
    BOOLEAN HasCoherentDcache;
    BOOLEAN HasAsidTagging;
    BOOLEAN HasExecuteNever;
    BOOLEAN HasGlobalPages;
    BOOLEAN SupportsLargePages;

    const ULONG *ProtectionToPteMask;
    ULONG ProtectionToPteMaskCount;
} MI_ARCH_DESCRIPTOR, *PMI_ARCH_DESCRIPTOR;

typedef struct _MI_PHYSICAL_RANGE
{
    ULONG64 BasePage;
    ULONG64 PageCount;
    ULONG Flags;
} MI_PHYSICAL_RANGE, *PMI_PHYSICAL_RANGE;

const MI_ARCH_DESCRIPTOR *MiArchDescribe(VOID);

VOID MiArchInvalidateTlbSingle(_In_ PVOID VirtualAddress, _In_ MI_TLB_SCOPE Scope);
VOID MiArchInvalidateTlbRange(_In_ PVOID BaseAddress, _In_ SIZE_T Size, _In_ MI_TLB_SCOPE Scope);
VOID MiArchInvalidateTlbAll(_In_ MI_TLB_SCOPE Scope);
