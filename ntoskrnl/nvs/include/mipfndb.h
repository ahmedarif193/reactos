/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/mipfndb.h
 * PURPOSE:     Physical page database structures and interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define MI_FRAME_INVALID          0xFFFFFFFFUL

typedef ULONG_PTR MI_FRAME_NUMBER, *PMI_FRAME_NUMBER;
#define MI_PFN_SHARDS             16
#define MI_PFN_SHARD_SHIFT        10
#define MI_PFN_LIST_SHARD_SHIFT   4
#define MI_PFN_CPU_CACHES         64
#define MI_PFN_CACHE_DEPTH        64
#define MI_PFN_CACHE_BATCH        32

#define MI_PFN_FLAG_LOCK          0x01
#define MI_PFN_FLAG_MODIFIED      0x02
#define MI_PFN_FLAG_PROTOTYPE     0x04
#define MI_PFN_FLAG_DELETED       0x08
#define MI_PFN_FLAG_PAGE_TABLE    0x10
#define MI_PFN_FLAG_IN_FLIGHT     0x20
#define MI_PFN_FLAG_ZEROED        0x40
#define MI_PFN_FLAG_PINNED        0x80

#define MI_ALLOCATE_ZEROED        0x01
#define MI_ALLOCATE_NO_RECLAIM    0x02

typedef enum _MI_PAGE_STATE
{
    MiPageZeroed = 0,
    MiPageFree,
    MiPageStandby,
    MiPageModified,
    MiPageBad,
    MiPageListCount,
    MiPageActive = MiPageListCount,
    MiPageTransition,
    MiPageCached,
    MiPageUnusable
} MI_PAGE_STATE;

typedef struct _MI_PFN
{
    ULONG Flink;
    ULONG Blink;
    volatile LONG ShareCount;
    USHORT ReferenceCount;
    UCHAR State;
    volatile UCHAR Flags;
    ULONG64 PteAddress;
    union
    {
        MI_PTE OriginalPte;
        struct _MI_ADDRESS_SPACE *PageTableOwner;
    };
    ULONG PteFrame;
    volatile LONG UsedEntries;
} MI_PFN, *PMI_PFN;

C_ASSERT(sizeof(MI_PFN) == 40);

#define MI_PFN_FLAGS(e)            ((UCHAR)MI_ATOMIC_READ8(&(e)->Flags))

typedef struct _MI_PFN_LIST
{
    ULONG Head;
    ULONG Tail;
    ULONG64 Count;
} MI_PFN_LIST, *PMI_PFN_LIST;

typedef struct _MI_PFN_SHARD
{
    MI_SPINLOCK Lock;
    MI_PFN_LIST List[MiPageListCount];
} MI_CACHE_ALIGNED MI_PFN_SHARD, *PMI_PFN_SHARD;

typedef struct _MI_PFN_CPU_CACHE
{
    MI_SPINLOCK Lock;
    ULONG Depth;
    ULONG64 Allocations;
    ULONG64 Refills;
    ULONG64 Drains;
    ULONG Frame[MI_PFN_CACHE_DEPTH];
} MI_CACHE_ALIGNED MI_PFN_CPU_CACHE, *PMI_PFN_CPU_CACHE;

struct _MI_PFN_DATABASE;

typedef VOID (*MI_PFN_REPURPOSE_ROUTINE)(_Inout_ struct _MI_PFN_DATABASE *Db, _In_ ULONG Frame);

typedef struct _MI_PFN_DATABASE
{
    PMI_PFN Pfn;
    ULONG FrameCount;
    ULONG CacheCount;
    MI_PFN_REPURPOSE_ROUTINE Repurpose;
    PVOID Owner;
    MI_PFN_SHARD Shard[MI_PFN_SHARDS];
    MI_PFN_CPU_CACHE Cache[MI_PFN_CPU_CACHES];
    volatile LONG64 Repurposed;
    ULONG ContiguousHint;
    volatile LONG64 ZeroedOnDemand;
} MI_PFN_DATABASE, *PMI_PFN_DATABASE;

VOID MiPfnDbInitialize(_Out_ PMI_PFN_DATABASE Db, _In_ PMI_PFN Array, _In_ ULONG FrameCount, _In_ ULONG CpuCount);
VOID MiPfnMarkInUse(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG FirstFrame, _In_ ULONG Count);
VOID MiPfnDbAddRange(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG FirstFrame, _In_ ULONG Count);
ULONG MiPfnAllocateContiguous(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Count, _In_ ULONG LowestFrame,
                              _In_ ULONG HighestFrame, _In_ ULONG BoundaryFrames);
ULONG MiPfnAllocatePageInRange(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG LowestFrame, _In_ ULONG HighestFrame,
                               _Inout_ PULONG Cursor);
ULONG MiPfnAllocatePage(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Flags);
VOID MiPfnInitializePage(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame, _In_ ULONG64 PteAddress,
                         _In_ ULONG PteFrame, _In_ MI_PTE OriginalPte, _In_ UCHAR Flags);
KIRQL MiPfnLock(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame);
VOID MiPfnUnlock(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame, _In_ KIRQL OldIrql);
VOID MiPfnSetModified(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame);
ULONG MiPfnMarkMappedPagesModified(_Inout_ PMI_PFN_DATABASE Db, _In_ PMI_PTE *Slots, _In_ ULONG Count);
VOID MiPfnShareIncrement(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame);
VOID MiPfnShareDecrement(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame, _In_ BOOLEAN Delete);
VOID MiPfnShareDecrementEx(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame, _In_ BOOLEAN Delete,
                           _Inout_opt_ PMI_PTE LastShareSlot, _In_ MI_PTE LastShareValue);
BOOLEAN MiPfnShareIncrementIfMapped(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame, _In_ PMI_PTE Slot,
                                    _In_ MI_PTE Expected);
VOID MiPfnReferenceLocked(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame);
VOID MiPfnDereference(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame);
BOOLEAN MiPfnReactivate(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame, _In_ ULONG64 PteAddress);
BOOLEAN MiPfnReactivateEx(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame, _In_ ULONG64 PteAddress,
                          _Inout_opt_ PMI_PTE Slot, _In_ MI_PTE Expected, _In_ MI_PTE NewValue);
ULONG MiPfnTakeModified(_Inout_ PMI_PFN_DATABASE Db);
BOOLEAN MiPfnClearModified(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame);
BOOLEAN MiPfnWriteComplete(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame, _In_ MI_PTE NewOriginalPte,
                        _In_ BOOLEAN Success);
VOID MiPfnFreePage(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame);
ULONG64 MiPfnListCount(_In_ PMI_PFN_DATABASE Db, _In_ UCHAR State);
ULONG64 MiPfnAvailablePages(_In_ PMI_PFN_DATABASE Db);
ULONG64 MiPfnAllocationCount(_In_ PMI_PFN_DATABASE Db);
VOID MiPfnDrainCaches(_Inout_ PMI_PFN_DATABASE Db);
ULONG MiPfnDbCheck(_In_ PMI_PFN_DATABASE Db);
