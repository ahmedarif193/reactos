/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/mmkernel.h
 * PURPOSE:     Kernel-facing memory manager interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define MM_SYSLDR_NO_IMPORTS   ((PVOID)(ULONG_PTR)-2)
#define MM_SYSLDR_BOOT_LOADED  ((PVOID)(ULONG_PTR)-1)
#define MM_SYSLDR_SINGLE_ENTRY 0x1

typedef struct _PHYSICAL_MEMORY_RUN
{
    PFN_NUMBER BasePage;
    PFN_NUMBER PageCount;
} PHYSICAL_MEMORY_RUN, *PPHYSICAL_MEMORY_RUN;

typedef struct _PHYSICAL_MEMORY_DESCRIPTOR
{
    ULONG NumberOfRuns;
    PFN_NUMBER NumberOfPages;
    PHYSICAL_MEMORY_RUN Run[1];
} PHYSICAL_MEMORY_DESCRIPTOR, *PPHYSICAL_MEMORY_DESCRIPTOR;

extern PPHYSICAL_MEMORY_DESCRIPTOR MmPhysicalMemoryBlock;
extern ULONG MmProductType;

extern PVOID MmNonPagedPoolStart;
extern PVOID MmNonPagedPoolExpansionStart;
extern PVOID MmNonPagedPoolEnd;
extern PVOID MmPagedPoolStart;
extern PVOID MmPagedPoolEnd;
extern PVOID MmNonPagedSystemStart;
extern PVOID MmSystemCacheStart;
extern PVOID MmSystemCacheEnd;
extern PVOID MmSystemPtesStart[2];
extern PVOID MmSystemPtesEnd[2];
extern PVOID MmSessionBase;
extern SIZE_T MmSessionSize;
extern SIZE_T MmSizeOfNonPagedPoolInBytes;
extern SIZE_T MmMaximumNonPagedPoolInBytes;
extern SIZE_T MmSizeOfPagedPoolInBytes;
extern SIZE_T MmAllocationFragment;
extern SIZE_T MmLargeStackSize;
extern SIZE_T MmMinimumStackCommitInBytes;
extern SIZE_T MmHeapSegmentReserve;
extern SIZE_T MmHeapSegmentCommit;
extern SIZE_T MmHeapDeCommitTotalFreeThreshold;
extern SIZE_T MmHeapDeCommitFreeBlockThreshold;
extern ULONG MmNumberOfSystemPtes;
extern ULONG MmSecondaryColors;
extern ULONG MmReadClusterSize;
extern ULONG MmCritsectTimeoutSeconds;
extern ULONG MmConsumedPoolPercentage;
extern ULONG MmSpecialPoolTag;
extern ULONG MmProtectFreedNonPagedPool;
extern ULONG MmVerifyDriverBufferLength;
extern ULONG MmVerifyDriverBufferType;
extern ULONG MmVerifyDriverLevel;
extern WCHAR MmVerifyDriverBuffer[512];
extern ULONG_PTR MmSubsectionBase;
extern LARGE_INTEGER MmCriticalSectionTimeout;
extern PFN_NUMBER MmLowMemoryThreshold;
extern PFN_NUMBER MmHighMemoryThreshold;
extern PFN_NUMBER MmResidentAvailablePages;
extern PFN_NUMBER MmAllocatedNonPagedPool;
extern MMSUPPORT MmSystemCacheWs;
extern MM_PAGED_POOL_INFO MmPagedPoolInfo;
extern PKUSER_SHARED_DATA MmWriteableSharedUserData;
extern BOOLEAN MmDynamicPfn;
extern BOOLEAN MmMirroring;
extern BOOLEAN MmTrackPtes;
extern BOOLEAN MmTrackLockedPages;
extern BOOLEAN MmLargeSystemCache;
extern BOOLEAN MmMakeLowMemory;
extern BOOLEAN MmEnforceWriteProtection;
extern BOOLEAN MmZeroPageFile;

FORCEINLINE
BOOLEAN
MiIsMemoryTypeInvisible(
    _In_ TYPE_OF_MEMORY MemoryType)
{
    return ((MemoryType == LoaderFirmwarePermanent) ||
            (MemoryType == LoaderSpecialMemory) ||
            (MemoryType == LoaderHALCachedMemory) ||
            (MemoryType == LoaderBBTMemory));
}
