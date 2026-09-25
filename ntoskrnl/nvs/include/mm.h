/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/mm.h
 * PURPOSE:     Shared memory manager definitions and interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <nvs/include/mienv.h>
#include <nvs/include/mipfndb.h>
#include <nvs/include/mivad.h>
#include <nvs/include/miasync.h>

struct _MI_SYSTEM;
struct _MI_SECTION_VIEW;

#define MI_TLB_BATCH_SIZE 32
#define MI_EMPTY_TABLE_CACHE_SIZE 8
#define MI_VAD_CACHE_SIZE 16

typedef struct _MI_EMPTY_TABLE
{
    ULONG64 Va;
    ULONG Frame;
    BOOLEAN Present;
} MI_EMPTY_TABLE, *PMI_EMPTY_TABLE;

typedef struct _MI_TLB_BATCH_ENTRY
{
    ULONG64 Va;
    struct _MI_SEGMENT *Segment;
    PMI_PTE Proto;
    ULONG Frame;
    BOOLEAN Private;
} MI_TLB_BATCH_ENTRY, *PMI_TLB_BATCH_ENTRY;

typedef struct _MI_TLB_BATCH
{
    ULONG Count;
    BOOLEAN DeferTables;
    MI_TLB_BATCH_ENTRY Entry[MI_TLB_BATCH_SIZE];
} MI_TLB_BATCH, *PMI_TLB_BATCH;

typedef struct _MI_ADDRESS_SPACE
{
    struct _MI_SYSTEM *System;
    ULONG RootFrame;
    BOOLEAN IsSystem;
    MI_RWLOCK Lock;
    MI_VAD_ROOT VadRoot;
    MI_VAD_ROOT AweRoot;
    MI_VAD_ROOT CloneRoot;
    struct _MI_VAD *FreeVads;
    ULONG FreeVadCount;
    ULONG64 LowestVa;
    ULONG64 HighestVa;
    ULONG64 BottomUpVa;
    ULONG64 TopDownVa;
    PVOID CommitOwner;
    BOOLEAN TrackExecutableWrites;

    volatile LONG64 CommittedPages;
    volatile LONG64 ResidentPages;
    volatile LONG64 PageTablePages;
    volatile LONG64 Faults;
    volatile LONG64 DemandZeroFaults;
    volatile LONG64 TransitionFaults;
    volatile LONG64 PrivatePages;
    volatile LONG64 LargePages;
    volatile LONG64 AwePages;
    volatile LONG64 PageFileFaults;
    volatile LONG64 PageFileGeneration;
    PMI_TLB_BATCH TlbBatch;
    volatile LONG64 PrototypeFaults;
    volatile LONG64 CopyOnWriteFaults;
    volatile LONG64 DirtyFaults;
    volatile LONG64 AccessFaults;

    ULONG64 TrimCursor;
    MI_EMPTY_TABLE EmptyTable[MI_EMPTY_TABLE_CACHE_SIZE];
    ULONG EmptyTableNext;
} MI_ADDRESS_SPACE, *PMI_ADDRESS_SPACE;

typedef struct _MI_SYSTEM
{
    const MI_ARCH_DESCRIPTOR *Arch;
    MI_PFN_DATABASE Pfn;
    MI_ADDRESS_SPACE SystemSpace;
    volatile LONG64 CommittedPages;
    LONG64 CommitLimit;
    volatile LONG64 AwePages;
    struct _MI_PAGEFILE *PageFile;
    struct _MI_SYSTEM_PTES *SystemPtes;
    MI_SPINLOCK SegmentListLock;
    LIST_ENTRY SegmentList;
    LIST_ENTRY UnusedSegmentList;
    ULONG UnusedSegmentCount;
    ULONG UnusedSegmentLimit;
    volatile LONG64 UnusedSegmentHits;
    PVOID Context;
    BOOLEAN (*ChargeOwnerCommit)(_In_ PVOID Owner, _In_ LONG64 Pages);
    VOID (*ReturnOwnerCommit)(_In_ PVOID Owner, _In_ LONG64 Pages);
    BOOLEAN (*ExpandCommit)(_Inout_ struct _MI_SYSTEM *System, _In_ LONG64 Pages, _In_ LONG64 Limit,
                            _In_ BOOLEAN Wait);
} MI_SYSTEM, *PMI_SYSTEM;

NTSTATUS MiSystemInitialize(_Out_ PMI_SYSTEM System, _In_ PMI_PFN PfnArray, _In_ ULONG FrameCount,
                            _In_ ULONG CpuCount, _In_ LONG64 CommitLimit);
NTSTATUS MiAddressSpaceCreate(_Inout_ PMI_SYSTEM System, _Out_ PMI_ADDRESS_SPACE Space);
VOID MiAddressSpaceDestroy(_Inout_ PMI_ADDRESS_SPACE Space);
NTSTATUS MiAddressSpaceAdopt(_Inout_ PMI_SYSTEM System, _Out_ PMI_ADDRESS_SPACE Space, _In_ ULONG RootFrame,
                             _In_ BOOLEAN IsSystem);
NTSTATUS MiSystemAdoptBootMappings(_Inout_ PMI_SYSTEM System);
NTSTATUS MiSystemPopulateTopLevel(_Inout_ PMI_SYSTEM System);
NTSTATUS MiSystemReserveTopLevelHole(_Inout_ PMI_SYSTEM System);

PMI_PTE MiPtLookup(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _Out_opt_ PULONG TableFrame);
PMI_PTE MiPtEnsure(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _Out_ PULONG TableFrame);
PMI_PTE MiPtLookupLevel(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ ULONG Level,
                        _Out_opt_ PULONG TableFrame);
PMI_PTE MiPtEnsureLevel(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ ULONG Level,
                        _Out_ PULONG TableFrame);
VOID MiPtPruneEmpty(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress);
VOID MiPtFlushEmpty(_Inout_ PMI_ADDRESS_SPACE Space);
VOID MiPtCacheEmpty(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG TableFrame, _In_ ULONG64 VirtualAddress);
VOID MiPtWrite(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _Inout_ PMI_PTE Slot,
               _In_ ULONG TableFrame, _In_ MI_PTE Value);
ULONG64 MiPtSlotAddress(_In_ PMI_PTE Slot, _In_ ULONG TableFrame, _In_ ULONG64 VirtualAddress);
ULONG64 MiPtNextTableBoundary(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress);
BOOLEAN MiPtVirtualAddressFromSlot(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 SlotAddress,
                                   _Out_ PULONG64 VirtualAddress);
BOOLEAN MiPtTranslate(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _Out_ PULONG64 PhysicalAddress,
                      _Out_opt_ PMI_PTE LeafPte);
BOOLEAN MiPtTranslateRoot(_In_ const MI_ARCH_DESCRIPTOR *Arch, _In_ ULONG64 RootFrame,
                          _In_ ULONG64 VirtualAddress, _Out_ PULONG64 PhysicalAddress,
                          _Out_opt_ PMI_PTE LeafPte);
ULONG MiPtCheck(_In_ PMI_ADDRESS_SPACE Space);
NTSTATUS MiPtPinRange(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ ULONG64 Length);
NTSTATUS MiPtPinSystemRange(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ ULONG64 Length);
VOID MiPtUnpinRange(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ ULONG64 Length);

#define MI_ALLOCATION_GRANULARITY   0x10000ULL

#define MI_MEM_COMMIT               0x00001000
#define MI_MEM_RESERVE              0x00002000
#define MI_MEM_DECOMMIT             0x00004000
#define MI_MEM_RELEASE              0x00008000
#define MI_MEM_FREE                 0x00010000
#define MI_MEM_PRIVATE              0x00020000
#define MI_MEM_MAPPED               0x00040000
#define MI_MEM_TOP_DOWN             0x00100000
#define MI_MEM_PHYSICAL             0x00400000
#define MI_MEM_IMAGE                0x01000000
#define MI_MEM_LARGE_PAGES          0x20000000
#define MI_MEM_ROTATE               0x00800000

typedef enum _MI_VAD_KIND
{
    MiVadPrivate = 0,
    MiVadMapped,
    MiVadImage,
    MiVadSystem,
    MiVadPhysical,
    MiVadLarge,
    MiVadAwe,
    MiVadRotate
} MI_VAD_KIND;

struct _MI_SEGMENT;

typedef struct _MI_VAD
{
    MI_VAD_NODE Node;
    ULONG Protection;
    UCHAR Type;
    UCHAR MaximumProtection;
    BOOLEAN MemCommit;
    BOOLEAN CopyOnWrite;
    BOOLEAN Inherit;
    BOOLEAN CacheView;
    BOOLEAN WritableUser;
    BOOLEAN LockedPages;
    BOOLEAN EcCode;
    volatile LONG PteTouched;
    struct _MI_SEGMENT *Segment;
    ULONG64 SegmentPageOffset;
    volatile LONG64 CommitCharge;
    PMI_FRAME_NUMBER RotateFrames;
} MI_VAD, *PMI_VAD;

#define MI_VAD_IS_DIRECT(v)   ((v)->Type == MiVadSystem || (v)->Type == MiVadPhysical || \
                              (v)->Type == MiVadLarge || (v)->Type == MiVadAwe || (v)->Type == MiVadRotate)

typedef struct _MI_CLONE_PAGE
{
    MI_PTE Proto;
    MI_MUTEX Lock;
    volatile LONG References;
    PMI_SYSTEM System;
} MI_CLONE_PAGE, *PMI_CLONE_PAGE;

typedef struct _MI_CLONE_REF
{
    MI_VAD_NODE Node;
    PMI_CLONE_PAGE Page;
    ULONG Protection;
} MI_CLONE_REF, *PMI_CLONE_REF;

NTSTATUS MiCloneAddressSpace(_Inout_ PMI_ADDRESS_SPACE Source, _Inout_ PMI_ADDRESS_SPACE Target);
PMI_CLONE_REF MiCloneLookup(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Va);
NTSTATUS MiCloneFault(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Va, _In_ ULONG Access);
BOOLEAN MiCloneDeletePage(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Va, _Inout_ PMI_PTE Slot,
                          _In_ ULONG TableFrame, _In_ MI_PTE NewValue);
BOOLEAN MiCloneProtectPage(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Va, _Inout_ PMI_PTE Slot,
                           _In_ ULONG TableFrame, _In_ ULONG Protection);
BOOLEAN MiCloneTrimPage(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Va, _Inout_ PMI_PTE Slot,
                        _In_ ULONG TableFrame);

typedef struct _MI_MEMORY_INFORMATION
{
    ULONG64 BaseAddress;
    ULONG64 AllocationBase;
    ULONG AllocationProtect;
    ULONG64 RegionSize;
    ULONG State;
    ULONG Protect;
    ULONG Type;
} MI_MEMORY_INFORMATION, *PMI_MEMORY_INFORMATION;

typedef enum _MI_FAULT_ACCESS
{
    MiFaultRead = 0,
    MiFaultWrite,
    MiFaultExecute
} MI_FAULT_ACCESS;

typedef struct _MI_PAGEFILE_OPS
{
    NTSTATUS (*Read)(_In_opt_ PVOID Context, _In_ ULONG64 Slot, _Out_ PVOID PageBuffer);
    NTSTATUS (*Write)(_In_opt_ PVOID Context, _In_ ULONG64 Slot, _In_ PVOID PageBuffer);
} MI_PAGEFILE_OPS, *PMI_PAGEFILE_OPS;

typedef struct _MI_PAGEFILE
{
    MI_SPINLOCK Lock;
    MI_PAGEFILE_OPS Ops;
    PVOID Context;
    PULONG64 Bitmap;
    ULONG64 SlotCount;
    ULONG64 SlotsInUse;
    ULONG64 Hint;
    volatile LONG64 PagesWritten;
    volatile LONG64 PagesRead;
} MI_PAGEFILE, *PMI_PAGEFILE;

BOOLEAN MiSpaceFindEmptyRange(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 PageCount, _In_ ULONG64 Alignment,
                              _In_ ULONG64 LowestVpn, _In_ ULONG64 HighestVpn, _In_ BOOLEAN TopDown,
                              _Out_ PULONG64 StartingVpn);
NTSTATUS MiAllocateVirtualMemory(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG64 BaseAddress,
                                 _Inout_ PULONG64 RegionSize, _In_ ULONG AllocationType, _In_ ULONG Protection);
NTSTATUS MiAllocateVirtualMemoryEx(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG64 BaseAddress,
                                   _Inout_ PULONG64 RegionSize, _In_ ULONG AllocationType, _In_ ULONG Protection,
                                   _In_ ULONG64 HighestAddress);
NTSTATUS MiAllocateVirtualMemoryBounded(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG64 BaseAddress,
                                        _Inout_ PULONG64 RegionSize, _In_ ULONG AllocationType,
                                        _In_ ULONG Protection, _In_ ULONG64 LowestAddress,
                                        _In_ ULONG64 HighestAddress, _In_ ULONG64 Alignment,
                                        _In_ BOOLEAN DenyDynamicCode);
NTSTATUS MiFreeVirtualMemory(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG64 BaseAddress,
                             _Inout_ PULONG64 RegionSize, _In_ ULONG FreeType);
NTSTATUS MiProtectVirtualMemory(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG64 BaseAddress,
                                _Inout_ PULONG64 RegionSize, _In_ ULONG NewProtection, _Out_ PULONG OldProtection);
NTSTATUS MiProtectVirtualMemoryEx(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG64 BaseAddress,
                                  _Inout_ PULONG64 RegionSize, _In_ ULONG NewProtection, _Out_ PULONG OldProtection,
                                  _In_ BOOLEAN DenyDynamicCode);
NTSTATUS MiQueryVirtualMemory(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Address,
                              _Out_ PMI_MEMORY_INFORMATION Information);
NTSTATUS MiRotatePopulate(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 BaseAddress, _In_ ULONG64 RegionSize);
VOID MiRotateReleaseLocked(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PMI_VAD Vad);
NTSTATUS MiRotateQuery(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ ULONG64 Size,
                       _Out_ PMI_FRAME_NUMBER Mapped, _Out_ PMI_FRAME_NUMBER Regular);
NTSTATUS MiRotateApply(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ ULONG64 Size,
                       _In_opt_ const MI_FRAME_NUMBER *Frames, _In_ ULONG LeafFlags);
VOID MiCleanAddressSpace(_Inout_ PMI_ADDRESS_SPACE Space);
NTSTATUS MiMapFramesUser(_Inout_ PMI_ADDRESS_SPACE Space, _In_ const MI_FRAME_NUMBER *Frames, _In_ ULONG PageCount,
                         _In_ ULONG Protection, _In_ ULONG LeafFlags, _In_ BOOLEAN LockedPages,
                         _Inout_ PULONG64 BaseAddress);
NTSTATUS MiUnmapFramesUser(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 BaseAddress, _In_ BOOLEAN LockedPages);
VOID MiUnmapFramesUserLocked(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PMI_VAD Vad);
NTSTATUS MiAllocateLargePages(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG64 BaseAddress,
                              _Inout_ PULONG64 RegionSize, _In_ ULONG AllocationType, _In_ ULONG Protection,
                              _In_ ULONG64 HighestAddress);
struct _MI_SEGMENT *MiReleaseLargePagesLocked(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PMI_VAD Vad);
NTSTATUS MiProtectLargePagesLocked(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Start, _In_ ULONG64 End,
                                   _In_ ULONG Protection, _Out_ PULONG OldProtection);
NTSTATUS MiAweAllocatePages(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG Count, _Out_ PMI_FRAME_NUMBER Frames);
NTSTATUS MiAweFreePages(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG Count, _In_ const MI_FRAME_NUMBER *Frames);
NTSTATUS MiAweMapPages(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Base,
                       _In_opt_ const ULONG64 *Addresses, _In_ ULONG Count,
                       _In_opt_ const MI_FRAME_NUMBER *Frames);
VOID MiAweReleaseWindowLocked(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PMI_VAD Vad);
VOID MiAweDestroyPagesLocked(_Inout_ PMI_ADDRESS_SPACE Space);

#define STATUS_PENDING_COPY ((NTSTATUS)0x40000FF1)
#define STATUS_PENDING_PAGE_IN ((NTSTATUS)0x40000FF2)

NTSTATUS MiCopyOnWrite(_Inout_ PMI_ADDRESS_SPACE Space, _In_ PMI_VAD Vad, _In_ ULONG64 VirtualAddress,
                       _Inout_ PMI_PTE Slot, _In_ ULONG TableFrame, _In_ MI_PTE Pte);
NTSTATUS MiResolvePrototypeFault(_Inout_ PMI_ADDRESS_SPACE Space, _In_ PMI_VAD Vad, _In_ ULONG64 VirtualAddress,
                                 _Inout_ PMI_PTE Slot, _In_ ULONG TableFrame, _In_ MI_FAULT_ACCESS Access);
NTSTATUS MiMakePageValid(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _Inout_ PMI_PTE Slot,
                         _In_ ULONG TableFrame, _In_ ULONG Frame, _In_ ULONG Protection, _In_ BOOLEAN Dirty);
VOID MiReleasePageBacking(_Inout_ struct _MI_SYSTEM *System, _In_ ULONG Frame);
NTSTATUS MiSetPrivatePageProtection(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress,
                                    _In_ ULONG Protection);
BOOLEAN MiViewPageCommitted(_In_ PMI_VAD Vad, _In_ ULONG64 VirtualAddress);
ULONG MiViewPageProtection(_In_ PMI_ADDRESS_SPACE Space, _In_ PMI_VAD Vad, _In_ ULONG64 VirtualAddress,
                           _In_ MI_PTE Pte);

NTSTATUS MiFault(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ MI_FAULT_ACCESS Access,
                 _In_ BOOLEAN UserMode);
NTSTATUS MiFaultWithWriteAllowance(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress,
                                   _In_ MI_FAULT_ACCESS Access, _In_ BOOLEAN UserMode,
                                   _In_ BOOLEAN AllowExecutableWrite);
NTSTATUS MiSetExecutableWriteTracking(_Inout_ PMI_ADDRESS_SPACE Space, _In_ BOOLEAN Enable);
NTSTATUS MiResetExecutableWriteTracking(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Base,
                                        _In_ ULONG64 Size);
VOID MiArmExecutableWriteRangeLocked(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Start,
                                     _In_ ULONG64 End);
VOID MiRepurposeStandbyPage(_Inout_ PMI_PFN_DATABASE Db, _In_ ULONG Frame);
VOID MiDeletePte(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _Inout_ PMI_PTE Slot,
                 _In_ ULONG TableFrame, _In_ MI_PTE NewValue);
PMI_VAD MiVadLocate(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress);
PMI_VAD MiVadAllocateAndLock(_Inout_ PMI_ADDRESS_SPACE Space);
PMI_VAD MiVadPopFree(_Inout_ PMI_ADDRESS_SPACE Space);
BOOLEAN MiVadCacheFree(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PMI_VAD Vad);
VOID MiVadFlushCache(_Inout_ PMI_ADDRESS_SPACE Space);

typedef enum _MI_SEGMENT_KIND
{
    MiSegmentDataFile = 0,
    MiSegmentImage,
    MiSegmentPageFileBacked
} MI_SEGMENT_KIND;

typedef VOID (*MI_READ_COMPLETION)(_In_opt_ PVOID Context, _In_ NTSTATUS Status);

typedef struct _MI_FILE_OPS
{
    NTSTATUS (*Read)(_In_opt_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length, _Out_ PVOID Buffer);
    NTSTATUS (*Write)(_In_opt_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length, _In_ PVOID Buffer);
    VOID (*Release)(_In_opt_ PVOID Context);
    NTSTATUS (*WriteFrames)(_In_opt_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length,
                            _In_ const ULONG *Frames, _In_ ULONG PageCount);
    NTSTATUS (*ReadAsync)(_In_opt_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Frame,
                          _In_ PVOID Buffer, _In_ MI_READ_COMPLETION Completion,
                          _In_opt_ PVOID CompletionContext);
    BOOLEAN WholePageReads;
    /* Optional synchronous clustered read. Success requires every frame to be filled. */
    NTSTATUS (*ReadPages)(_In_opt_ PVOID Context, _In_ ULONG64 Offset,
                          _In_ const ULONG *Frames, _In_ ULONG PageCount);
} MI_FILE_OPS, *PMI_FILE_OPS;

#define MI_MAX_FILE_IO_PAGES 16
#define MI_MAX_FILE_WRITE_PAGES 64

typedef struct _MI_SEGMENT_LAYOUT
{
    ULONG64 FirstPage;
    ULONG64 PageCount;
    ULONG64 FileOffset;
    ULONG64 FileBytes;
    ULONG Protection;
} MI_SEGMENT_LAYOUT, *PMI_SEGMENT_LAYOUT;

#define MI_SECTOR_SHIFT 9

#define MI_SEGMENT_ACTIVE    0
#define MI_SEGMENT_UNUSED    1
#define MI_SEGMENT_DELETING  2

typedef struct _MI_SEGMENT
{
    LIST_ENTRY SystemLink;
    LIST_ENTRY UnusedLink;
    UCHAR State;
    struct _MI_SYSTEM *System;
    MI_MUTEX Lock;
    MI_MUTEX FlushLock;
    volatile LONG ReferenceCount;
    volatile LONG MappedViews;
    volatile LONG TruncationViews;
    volatile LONG WritableUserViews;
    ULONG ActiveWriters;
    UCHAR Kind;
    BOOLEAN Reserved;
    ULONG Protection;
    volatile LONG64 SizeInBytes;
    volatile LONG64 PageCount;
    ULONG64 ChunkCount;
    ULONG64 ChunkCapacity;
    PMI_PTE * volatile Chunk;
    PVOID RetiredDirectories;
    LONG64 CommitCharge;
    MI_FILE_OPS FileOps;
    PVOID FileContext;
    PMI_SEGMENT_LAYOUT Layout;
    ULONG LayoutCount;
    PULONG LargeFrames;
    volatile LONG64 PagesRead;
    volatile LONG64 PagesWritten;
    LIST_ENTRY PendingReads;
    LIST_ENTRY ReadDrains;
    ULONG PendingReadCount;
    ULONG64 ReadGeneration;
} MI_SEGMENT, *PMI_SEGMENT;

NTSTATUS MiSegmentCreate(_Inout_ struct _MI_SYSTEM *System, _In_ UCHAR Kind, _In_ ULONG64 SizeInBytes,
                         _In_ ULONG Protection, _In_opt_ PMI_FILE_OPS FileOps, _In_opt_ PVOID FileContext,
                         _In_opt_ PMI_SEGMENT_LAYOUT Layout, _In_ ULONG LayoutCount, _Out_ PMI_SEGMENT *Segment);
NTSTATUS MiSegmentAdoptFrame(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Page, _In_ ULONG Frame);
NTSTATUS MiSegmentCreateReserved(_Inout_ struct _MI_SYSTEM *System, _In_ ULONG64 SizeInBytes, _In_ ULONG Protection,
                                 _In_opt_ PMI_FILE_OPS FileOps, _In_opt_ PVOID FileContext,
                                 _Out_ PMI_SEGMENT *SegmentOut);
NTSTATUS MiSegmentCommitPages(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 FirstPage, _In_ ULONG64 PageCount);
NTSTATUS MiSegmentCreateLarge(_Inout_ PMI_SYSTEM System, _In_ ULONG64 SizeInBytes, _In_ ULONG Protection,
                              _In_opt_ PMI_FILE_OPS FileOps, _In_opt_ PVOID FileContext,
                              _Out_ PMI_SEGMENT *SegmentOut);
VOID MiSegmentReleaseAdoptedFrame(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Page);
VOID MiSegmentReference(_Inout_ PMI_SEGMENT Segment);
BOOLEAN MiSegmentTryReference(_Inout_ PMI_SEGMENT Segment);
BOOLEAN MiTlbBatchBegin(_Inout_ PMI_ADDRESS_SPACE Space, _Out_ PMI_TLB_BATCH Batch);
VOID MiTlbBatchEnd(_Inout_ PMI_ADDRESS_SPACE Space, _In_ BOOLEAN Owner);
VOID MiTlbBatchAdd(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ ULONG Frame,
                   _In_opt_ PMI_SEGMENT Segment, _In_opt_ PMI_PTE Proto, _In_ BOOLEAN Private);
BOOLEAN MiSegmentDereferenceAndClose(_Inout_ PMI_SEGMENT Segment);
ULONG MiSegmentPurgeUnused(_Inout_ PMI_SYSTEM System, _In_ ULONG MaximumCount);
VOID MiSegmentDereference(_Inout_ PMI_SEGMENT Segment);
NTSTATUS MiSegmentExtend(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 NewSizeInBytes);
NTSTATUS MiSegmentFlush(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Offset, _In_ ULONG64 Length);
BOOLEAN MiSegmentPurge(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Offset, _In_ ULONG64 Length);
BOOLEAN MiSegmentIsResident(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Offset, _In_ ULONG64 Length);
NTSTATUS MiSegmentMakeResident(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Offset, _In_ ULONG64 Length);
NTSTATUS MiSegmentFaultIn(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Page);
NTSTATUS MiSegmentMakeResidentBeyond(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Offset, _In_ ULONG64 Length,
                                     _In_ ULONG64 ValidDataLength);
NTSTATUS MiSegmentPrefetch(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Offset, _In_ ULONG64 Length);
VOID MiSegmentDrainReads(_Inout_ PMI_SEGMENT Segment, _Inout_ PMI_ASYNC_DRAIN Drain);
NTSTATUS MiSegmentMarkDirty(_Inout_ PMI_SEGMENT Segment, _In_ ULONG64 Offset, _In_ ULONG64 Length);

NTSTATUS MiMapView(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PMI_SEGMENT Segment, _Inout_ PULONG64 BaseAddress,
                   _In_ ULONG64 SectionOffset, _Inout_ PULONG64 ViewSize, _In_ ULONG Protection,
                   _In_ ULONG AllocationType);
NTSTATUS MiMapViewEx(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PMI_SEGMENT Segment, _Inout_ PULONG64 BaseAddress,
                     _In_ ULONG64 SectionOffset, _Inout_ PULONG64 ViewSize, _In_ ULONG Protection,
                     _In_ ULONG AllocationType, _In_ ULONG64 HighestAddress, _In_ ULONG MaximumProtection,
                     _In_ BOOLEAN Inherit);
NTSTATUS MiUnmapView(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 BaseAddress);
NTSTATUS MiMapCacheView(_Inout_ PMI_SEGMENT Segment, _Inout_ PULONG64 BaseAddress,
                        _In_ ULONG64 SectionOffset, _Inout_ PULONG64 ViewSize);
NTSTATUS MiMapLargeSection(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PMI_SEGMENT Segment,
                           _Inout_ PULONG64 BaseAddress, _In_ ULONG64 SectionOffset, _Inout_ PULONG64 ViewSize,
                           _In_ ULONG Protection, _In_ ULONG AllocationType, _In_ ULONG64 HighestAddress,
                           _In_ ULONG MaximumProtection, _In_ BOOLEAN Inherit);
NTSTATUS MiCloneLargeViewLocked(_Inout_ PMI_ADDRESS_SPACE Source, _Inout_ PMI_ADDRESS_SPACE Target,
                                _Inout_ PMI_VAD Vad);
BOOLEAN MiViewProtectionAllowed(_In_ PMI_SEGMENT Segment, _In_ ULONG Maximum, _In_ ULONG Protection);
NTSTATUS MiFlushVirtualMemory(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PULONG64 BaseAddress,
                              _Inout_ PULONG64 RegionSize);
NTSTATUS MiSetRangeModified(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 BaseAddress, _In_ ULONG64 Length);

NTSTATUS MiProtectMappedView(_Inout_ PMI_ADDRESS_SPACE Space, _In_ PMI_VAD Vad, _In_ ULONG64 Start, _In_ ULONG64 End,
                             _In_ ULONG Protection);
NTSTATUS MiSetMappedViewProtection(_Inout_ PMI_ADDRESS_SPACE Space, _In_ PMI_VAD Vad, _In_ ULONG64 Start,
                                   _In_ ULONG64 End, _In_ ULONG Protection);
VOID MiRemoveMappedView(_Inout_ PMI_ADDRESS_SPACE Space, _Inout_ PMI_VAD Vad);
BOOLEAN MiTrimPrototypePage(_Inout_ PMI_ADDRESS_SPACE Space, _In_ PMI_VAD Vad, _In_ ULONG64 VirtualAddress,
                            _Inout_ PMI_PTE Slot, _In_ ULONG TableFrame);
NTSTATUS MiWritePrototypePage(_Inout_ struct _MI_SYSTEM *System, _In_ ULONG Frame);

ULONG MiTrimAddressSpace(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG PageTarget, _In_ BOOLEAN Aggressive);
ULONG MiWriteModifiedPages(_Inout_ PMI_SYSTEM System, _In_ ULONG MaximumPages);

NTSTATUS MiPageFileInitialize(_Out_ PMI_PAGEFILE PageFile, _In_ PMI_PAGEFILE_OPS Ops, _In_opt_ PVOID Context,
                              _In_ ULONG64 SlotCount);
VOID MiPageFileUninitialize(_Inout_ PMI_PAGEFILE PageFile);
ULONG64 MiPageFileReserveSlot(_Inout_ PMI_PAGEFILE PageFile);
VOID MiPageFileReleaseSlot(_Inout_ PMI_PAGEFILE PageFile, _In_ ULONG64 Slot);
NTSTATUS MiPageFileExtend(_Inout_ PMI_PAGEFILE PageFile, _In_ ULONG64 SlotCount);

BOOLEAN MiChargeSystemCommit(_Inout_ PMI_SYSTEM System, _In_ LONG64 Pages, _In_ BOOLEAN Wait);
BOOLEAN MiChargeCommit(_Inout_ PMI_ADDRESS_SPACE Space, _In_ LONG64 Pages);
VOID MiReturnCommit(_Inout_ PMI_ADDRESS_SPACE Space, _In_ LONG64 Pages);
