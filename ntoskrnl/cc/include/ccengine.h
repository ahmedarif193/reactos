/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/include/ccengine.h
 * PURPOSE:     Cache engine structures and interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "ccenv.h"

struct _CC_BCB;

typedef struct _CC_BACKING_OPS
{
    NTSTATUS (*MapView)(_In_ PVOID Context, _In_ ULONG64 Offset, _In_ SIZE_T Length, _Out_ PVOID *Base);
    VOID (*UnmapView)(_In_ PVOID Context, _In_ PVOID Base);
    BOOLEAN (*IsResident)(_In_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length);
    NTSTATUS (*MakeResident)(_In_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length,
                             _In_ ULONG64 ValidDataLength);
    NTSTATUS (*MarkDirty)(_In_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length);
    NTSTATUS (*Flush)(_In_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length);
    BOOLEAN (*Purge)(_In_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG64 Length);
    NTSTATUS (*Extend)(_In_ PVOID Context, _In_ ULONG64 Size);
    NTSTATUS (*Prefetch)(_In_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length);
    NTSTATUS (*MakeViewResident)(_In_ PVOID Context, _In_ PVOID Base, _In_ ULONG Length);
} CC_BACKING_OPS, *PCC_BACKING_OPS;

struct _CC_MAP;

typedef struct _CC_VIEW
{
    PVOID BaseAddress;
    struct _CC_MAP *Map;
    ULONG64 FileOffset;
    LIST_ENTRY LruLink;
    volatile LONG ReferenceCount;
    UCHAR LruShard;
    BOOLEAN OnLru;
} CC_VIEW, *PCC_VIEW;

typedef struct _CC_SLOT
{
    PCC_VIEW View;
    ULONG64 DirtyMap;
} CC_SLOT, *PCC_SLOT;

typedef struct _CC_LEAF
{
    CC_SLOT Slot[CC_LEAF_SLOTS];
} CC_LEAF, *PCC_LEAF;

typedef struct _CC_LRU_SHARD
{
    CC_LOCK Lock;
    LIST_ENTRY List;
    ULONG Count;
} CC_CACHE_ALIGNED CC_LRU_SHARD, *PCC_LRU_SHARD;

typedef struct _CC_CACHE
{
    PCC_VIEW View;
    ULONG ViewCount;
    CC_LRU_SHARD Lru[CC_LRU_SHARDS];
    CC_LOCK ReclaimLock;
    ULONG ReclaimCursor;

    CC_LOCK DirtyLock;
    LIST_ENTRY DirtyMaps;
    volatile LONG64 TotalDirtyPages;
    LONG64 DirtyPageThreshold;

    ULONG BcbBytes;
    VOID (*BcbCreated)(_Inout_ struct _CC_BCB *Bcb);
    VOID (*BcbDeleting)(_Inout_ struct _CC_BCB *Bcb);

    volatile LONG64 ViewHits;
    volatile LONG64 ViewMisses;
    volatile LONG64 ViewReclaims;
    volatile LONG64 PagesFlushed;
    volatile LONG64 FlushCalls;
} CC_CACHE, *PCC_CACHE;

typedef struct _CC_MAP
{
    USHORT NodeTypeCode;
    USHORT NodeByteSize;
    volatile LONG OpenCount;
    ULONG64 FileSize;
    LIST_ENTRY BcbList;
    ULONG64 SectionSize;
    ULONG64 ValidDataLength;

    PCC_CACHE Cache;
    CC_BACKING_OPS Ops;
    PVOID Context;

    CC_LOCK IndexLock;
    PCC_LEAF *Leaf;
    ULONG LeafCount;
    ULONG ViewsAttached;

    CC_LOCK BcbLock;
    ULONG64 BcbGeneration;

    LIST_ENTRY DirtyLink;
    BOOLEAN OnDirtyList;
    volatile LONG64 DirtyPages;
    volatile LONG FlushesInProgress;
    LONG64 DirtyPageThreshold;
} CC_MAP, *PCC_MAP;

C_ASSERT(FIELD_OFFSET(CC_MAP, NodeTypeCode) == 0x00);
C_ASSERT(FIELD_OFFSET(CC_MAP, NodeByteSize) == 0x02);
C_ASSERT(FIELD_OFFSET(CC_MAP, OpenCount) == 0x04);
C_ASSERT(FIELD_OFFSET(CC_MAP, FileSize) == 0x08);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_MAP, BcbList) == 0x10);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_MAP, SectionSize) == 0x20);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_MAP, ValidDataLength) == 0x28);
C_ASSERT(FIELD_OFFSET(CC_VIEW, BaseAddress) == 0x00);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_VIEW, Map) == 0x08);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_VIEW, FileOffset) == 0x10);

typedef struct _CC_BCB
{
    USHORT NodeTypeCode;
    BOOLEAN Dirty;
    UCHAR Reserved;
    ULONG Length;
    ULONG64 FileOffset;
    LIST_ENTRY Link;
    ULONG64 BeyondLastByte;
    ULONG64 OldestLsn;
    ULONG64 NewestLsn;
    PCC_VIEW View;
    ULONG PinCount;
    CC_RESOURCE Resource;
    PCC_MAP Map;
    PVOID BaseAddress;

    LONG ReferenceCount;
    BOOLEAN Pinned;
    ULONG64 DirtyGeneration;
    PVOID OwnBase;
    ULONG64 OwnOffset;
} CC_BCB, *PCC_BCB;

C_ASSERT(FIELD_OFFSET(CC_BCB, NodeTypeCode) == 0x00);
C_ASSERT(FIELD_OFFSET(CC_BCB, Dirty) == 0x02);
C_ASSERT(FIELD_OFFSET(CC_BCB, Reserved) == 0x03);
C_ASSERT(FIELD_OFFSET(CC_BCB, Length) == 0x04);
C_ASSERT(FIELD_OFFSET(CC_BCB, FileOffset) == 0x08);
C_ASSERT(FIELD_OFFSET(CC_BCB, Link) == 0x10);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_BCB, BeyondLastByte) == 0x20);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_BCB, OldestLsn) == 0x28);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_BCB, NewestLsn) == 0x30);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_BCB, View) == 0x38);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_BCB, PinCount) == 0x40);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_BCB, Resource) == 0x48);
C_ASSERT(sizeof(PVOID) != 8 || sizeof(CC_RESOURCE) == 0x68);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_BCB, Map) == 0xB0);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_BCB, BaseAddress) == 0xB8);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(CC_BCB, ReferenceCount) == 0xC0);

#define CC_BCB_ALIGNMENT   (64 * 1024)

typedef struct _CC_VIEW_RANGE
{
    PCC_VIEW View;
    PVOID Address;
    ULONG64 FileOffset;
    ULONG Length;
} CC_VIEW_RANGE, *PCC_VIEW_RANGE;

typedef struct _CC_READ_AHEAD
{
    ULONG64 LastEnd;
    ULONG64 AheadEnd;
    ULONG Granularity;
    ULONG SequentialHits;
    BOOLEAN Disabled;
} CC_READ_AHEAD, *PCC_READ_AHEAD;

NTSTATUS CcCacheInitialize(_Out_ PCC_CACHE Cache, _In_ ULONG ViewCount, _In_ LONG64 DirtyPageThreshold);
VOID CcCacheUninitialize(_Inout_ PCC_CACHE Cache);
ULONG CcCacheTrim(_Inout_ PCC_CACHE Cache, _In_ ULONG ViewTarget);
ULONG CcCacheCheck(_In_ PCC_CACHE Cache);

VOID CcMapInitialize(_Out_ PCC_MAP Map, _In_ PCC_CACHE Cache, _In_ PCC_BACKING_OPS Ops, _In_ PVOID Context,
                     _In_ ULONG64 SectionSize, _In_ ULONG64 FileSize, _In_ ULONG64 ValidDataLength);
BOOLEAN CcMapUninitialize(_Inout_ PCC_MAP Map);
VOID CcMapSetSizes(_Inout_ PCC_MAP Map, _In_ ULONG64 SectionSize, _In_ ULONG64 FileSize,
                   _In_ ULONG64 ValidDataLength);

NTSTATUS CcViewAcquire(_Inout_ PCC_MAP Map, _In_ ULONG64 FileOffset, _In_ ULONG Length,
                       _Out_ PCC_VIEW_RANGE Range);
VOID CcViewRelease(_Inout_ PCC_VIEW_RANGE Range);
VOID CcViewReference(_Inout_ PCC_VIEW View);
VOID CcViewDereference(_Inout_ PCC_VIEW View);
NTSTATUS CcViewMakeResident(_Inout_ PCC_MAP Map, _In_ PCC_VIEW_RANGE Range, _In_ BOOLEAN SkipRead);
BOOLEAN CcMapDetachViews(_Inout_ PCC_MAP Map, _In_ ULONG64 FileOffset, _In_ ULONG64 Length);

typedef NTSTATUS (*CC_MOVE_ROUTINE)(_In_opt_ PVOID MoveContext, _In_ PVOID CacheAddress,
                                    _In_ ULONG64 FileOffset, _In_ ULONG Length);

NTSTATUS CcCopyRange(_Inout_ PCC_MAP Map, _In_ ULONG64 FileOffset, _In_ ULONG64 Length, _In_ BOOLEAN ForWrite,
                     _In_ CC_MOVE_ROUTINE Move, _In_opt_ PVOID MoveContext, _Out_opt_ PULONG64 BytesMoved);
NTSTATUS CcPrefetchRange(_Inout_ PCC_MAP Map, _In_ ULONG64 FileOffset, _In_ ULONG64 Length);

NTSTATUS CcDirtyMark(_Inout_ PCC_MAP Map, _In_ ULONG64 FileOffset, _In_ ULONG Length);
NTSTATUS CcDirtyFlush(_Inout_ PCC_MAP Map, _In_ ULONG64 FileOffset, _In_ ULONG64 Length,
                      _In_ ULONG MaximumPages, _Out_opt_ PULONG PagesFlushed);
VOID CcDirtyDiscard(_Inout_ PCC_MAP Map, _In_ ULONG64 FileOffset, _In_ ULONG64 Length);
BOOLEAN CcDirtyQuery(_In_ PCC_MAP Map, _In_ ULONG64 FileOffset, _In_ ULONG64 Length);
BOOLEAN CcDirtyCanWrite(_In_ PCC_CACHE Cache, _In_opt_ PCC_MAP Map, _In_ ULONG BytesToWrite);
PCC_MAP CcDirtyNextMap(_Inout_ PCC_CACHE Cache);
ULONG CcDirtyLazyTarget(_In_ PCC_CACHE Cache);

NTSTATUS CcBcbAcquire(_Inout_ PCC_MAP Map, _In_ ULONG64 FileOffset, _In_ ULONG Length, _In_ BOOLEAN Pinned,
                      _In_ BOOLEAN OnlyExisting, _Out_ PCC_BCB *Bcb, _Out_ PBOOLEAN Created);
PVOID CcBcbAddress(_In_ PCC_BCB Bcb, _In_ ULONG64 FileOffset);
VOID CcBcbReference(_Inout_ PCC_BCB Bcb);
VOID CcBcbDereference(_Inout_ PCC_BCB Bcb);
VOID CcBcbPin(_Inout_ PCC_BCB Bcb);
VOID CcBcbUnpin(_Inout_ PCC_BCB Bcb);
NTSTATUS CcBcbMakeResident(_Inout_ PCC_BCB Bcb, _In_ BOOLEAN SkipRead);
NTSTATUS CcBcbSetDirty(_Inout_ PCC_BCB Bcb, _In_ ULONG64 Lsn);
ULONG64 CcBcbBeginFlush(_Inout_ PCC_MAP Map);
BOOLEAN CcBcbCompleteFlush(_Inout_ PCC_MAP Map, _In_ ULONG64 Start, _In_ ULONG64 End, _In_ ULONG64 Generation);
BOOLEAN CcBcbRangeBusy(_Inout_ PCC_MAP Map, _In_ ULONG64 Start, _In_ ULONG64 End);
BOOLEAN CcBcbDirtyLsns(_Inout_ PCC_MAP Map, _Out_ PULONG64 OldestLsn, _Out_ PULONG64 NewestLsn,
                       _Out_ PULONG DirtyBcbs);

VOID CcReadAheadInitialize(_Out_ PCC_READ_AHEAD State);
BOOLEAN CcReadAheadNote(_Inout_ PCC_READ_AHEAD State, _In_ ULONG64 FileOffset, _In_ ULONG Length,
                        _In_ ULONG64 FileSize, _Out_ PULONG64 AheadOffset, _Out_ PULONG AheadLength);
