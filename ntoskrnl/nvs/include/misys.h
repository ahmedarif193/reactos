/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/misys.h
 * PURPOSE:     System virtual memory structures and interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <nvs/include/mm.h>

#define MI_SYSPTE_CLASSES         7
#define MI_SYSPTE_CLASS_MAX_PAGES (1u << (MI_SYSPTE_CLASSES - 1))
#define MI_SYSPTE_CACHE_DEPTH     16
#define MI_SYSPTE_CPU_CACHES      64

#define MI_KSTACK_CACHE_DEPTH     4

typedef enum _MI_CACHE_TYPE
{
    MiCacheNone = 0,
    MiCacheFull,
    MiCacheWriteCombined
} MI_CACHE_TYPE;

typedef struct _MI_SYSPTE_CPU_CACHE
{
    MI_SPINLOCK Lock;
    ULONG Depth[MI_SYSPTE_CLASSES];
    ULONG64 Entry[MI_SYSPTE_CLASSES][MI_SYSPTE_CACHE_DEPTH];
    ULONG StackDepth;
    ULONG64 Stack[MI_KSTACK_CACHE_DEPTH];
    ULONG64 Reservations;
    ULONG64 CacheHits;
} MI_CACHE_ALIGNED MI_SYSPTE_CPU_CACHE, *PMI_SYSPTE_CPU_CACHE;

typedef struct _MI_SYSTEM_PTES
{
    PMI_SYSTEM System;
    PMI_VAD Vad;
    ULONG64 Base;
    ULONG64 PageCount;
    MI_SPINLOCK Lock;
    PULONG64 Bitmap;
    ULONG64 Hint;
    volatile LONG64 FreePages;
    ULONG CacheCount;
    ULONG DefaultStackPages;
    MI_SYSPTE_CPU_CACHE Cache[MI_SYSPTE_CPU_CACHES];
} MI_SYSTEM_PTES, *PMI_SYSTEM_PTES;

typedef struct _MI_MDL
{
    PMI_ADDRESS_SPACE Space;
    ULONG64 StartVa;
    ULONG ByteOffset;
    ULONG ByteCount;
    ULONG PageCount;
    BOOLEAN Locked;
    BOOLEAN WriteAccess;
    ULONG64 MappedSystemVa;
    MI_FRAME_NUMBER Frames[1];
} MI_MDL, *PMI_MDL;

NTSTATUS MiSystemRegionCreate(_Inout_ PMI_SYSTEM System, _In_ ULONG64 PageCount, _In_ BOOLEAN PinTables,
                              _Out_ PMI_VAD *Vad, _Out_ PULONG64 Base);
VOID MiSystemRegionDelete(_Inout_ PMI_SYSTEM System, _Inout_ PMI_VAD Vad, _In_ BOOLEAN PinnedTables);
NTSTATUS MiSystemMapFrames(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress,
                           _In_ const MI_FRAME_NUMBER *Frames,
                           _In_ ULONG Count, _In_ ULONG Protection, _In_ ULONG LeafFlags, _In_ BOOLEAN OwnFrames);
VOID MiSystemUnmap(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress, _In_ ULONG Count,
                   _In_ BOOLEAN FreeFrames);
NTSTATUS MiSystemCommitPages(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress, _In_ ULONG Count,
                             _In_ ULONG Protection);
VOID MiSystemDecommitPages(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress, _In_ ULONG Count);

NTSTATUS MiSystemCommitPinned(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress, _In_ ULONG Count,
                              _In_ ULONG Protection);
VOID MiSystemDecommitPinned(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress, _In_ ULONG Count);
NTSTATUS MiSystemProtect(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress, _In_ ULONG Count,
                         _In_ ULONG Protection);

NTSTATUS MiSystemPtesInitialize(_Inout_ PMI_SYSTEM System, _In_ ULONG64 PageCount, _In_ ULONG CpuCount);
VOID MiSystemPtesUninitialize(_Inout_ PMI_SYSTEM System);
ULONG64 MiReserveSystemPtes(_Inout_ PMI_SYSTEM System, _In_ ULONG PageCount);
ULONG64 MiSystemPteCacheHits(_In_ PMI_SYSTEM System);
VOID MiReleaseSystemPtes(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress, _In_ ULONG PageCount);

NTSTATUS MiCreateKernelStack(_Inout_ PMI_SYSTEM System, _In_ ULONG ReservePages, _In_ ULONG CommitPages,
                             _Out_ PULONG64 StackTop);
VOID MiDeleteKernelStack(_Inout_ PMI_SYSTEM System, _In_ ULONG64 StackTop, _In_ ULONG ReservePages);
NTSTATUS MiGrowKernelStack(_Inout_ PMI_SYSTEM System, _In_ ULONG64 StackTop, _In_ ULONG ReservePages,
                           _In_ ULONG64 NewLimit);

NTSTATUS MiMapIoSpace(_Inout_ PMI_SYSTEM System, _In_ ULONG64 PhysicalAddress, _In_ ULONG64 Bytes,
                      _In_ MI_CACHE_TYPE CacheType, _Out_ PULONG64 VirtualAddress);
VOID MiUnmapIoSpace(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress, _In_ ULONG64 Bytes);

NTSTATUS MiAllocateContiguousMemory(_Inout_ PMI_SYSTEM System, _In_ ULONG64 Bytes, _In_ ULONG64 LowestAddress,
                                    _In_ ULONG64 HighestAddress, _In_ ULONG64 BoundaryBytes,
                                    _In_ MI_CACHE_TYPE CacheType, _Out_ PULONG64 VirtualAddress);
VOID MiFreeContiguousMemory(_Inout_ PMI_SYSTEM System, _In_ ULONG64 VirtualAddress);

NTSTATUS MiLockPages(_Inout_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 StartVa, _In_ ULONG PageCount,
                     _In_ BOOLEAN UserMode, _In_ BOOLEAN WriteAccess, _Out_ PMI_FRAME_NUMBER Frames);
NTSTATUS MiLockSelectedPages(_Inout_ PMI_ADDRESS_SPACE Space, _In_ const ULONG64 *Addresses,
                             _In_ ULONG PageCount, _In_ BOOLEAN UserMode, _In_ BOOLEAN WriteAccess,
                             _Out_ PMI_FRAME_NUMBER Frames);
VOID MiUnlockFrames(_Inout_ PMI_SYSTEM System, _In_ const MI_FRAME_NUMBER *Frames, _In_ ULONG PageCount,
                    _In_ BOOLEAN WriteAccess);
NTSTATUS MiMapFrames(_Inout_ PMI_SYSTEM System, _In_ const MI_FRAME_NUMBER *Frames, _In_ ULONG PageCount,
                     _In_ MI_CACHE_TYPE CacheType, _In_ ULONG Protection, _Out_ PULONG64 Base);
VOID MiUnmapFrames(_Inout_ PMI_SYSTEM System, _In_ ULONG64 Base, _In_ ULONG PageCount);
PMI_MDL MiMdlAllocate(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress, _In_ ULONG ByteCount);
VOID MiMdlFree(_Inout_ PMI_MDL Mdl);
NTSTATUS MiProbeAndLockPages(_Inout_ PMI_MDL Mdl, _In_ BOOLEAN UserMode, _In_ BOOLEAN WriteAccess);
VOID MiUnlockPages(_Inout_ PMI_MDL Mdl);
NTSTATUS MiBuildMdlForSystemRange(_Inout_ PMI_MDL Mdl);
NTSTATUS MiMapLockedPages(_Inout_ PMI_MDL Mdl, _In_ MI_CACHE_TYPE CacheType, _Out_ PULONG64 SystemVa);
VOID MiUnmapLockedPages(_Inout_ PMI_MDL Mdl);
ULONG64 MiGetPhysicalAddress(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 VirtualAddress);
