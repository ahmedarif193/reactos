/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/ntmappingshim.h
 * PURPOSE:     NT reserved mapping host-test compatibility definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "ntpagingshim.h"
#include <nvs/include/misys.h>
#include <setjmp.h>

extern PMI_SYSTEM MiMappingTestSystem;
extern jmp_buf MiMappingTestBugCheck;
extern ULONG_PTR MiMappingTestBugCheckCode;
#define MiSystem (*MiMappingTestSystem)
#define BYTES_TO_PAGES(Size) (((Size) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PAGE_ALIGN(Address) ((PVOID)((ULONG_PTR)(Address) & ~(ULONG_PTR)(PAGE_SIZE - 1)))
#define ALIGN_DOWN_POINTER_BY(Address, Alignment) ((PVOID)((ULONG_PTR)(Address) & ~((ULONG_PTR)(Alignment) - 1)))
#define ADDRESS_AND_SIZE_TO_SPAN_PAGES(Address, Size) BYTES_TO_PAGES(((ULONG_PTR)(Address) & (PAGE_SIZE - 1)) + (Size))
#define MmGetMdlVirtualAddress(Mdl) ((PVOID)((ULONG_PTR)(Mdl)->StartVa + (Mdl)->ByteOffset))
#define MDL_PARTIAL_HAS_BEEN_MAPPED 0x20
#define SYSTEM_PTE_MISUSE 0xDA
typedef enum { MmNonCached, MmCached, MmWriteCombined } MEMORY_CACHING_TYPE;

static inline void KeBugCheckEx(ULONG Code, ULONG_PTR Reason, ULONG_PTR Address, ULONG_PTR Tag, ULONG_PTR Actual)
{
    MiMappingTestBugCheckCode = Reason;
    longjmp(MiMappingTestBugCheck, 1);
}

PVOID MmAllocateMappingAddress(SIZE_T NumberOfBytes, ULONG PoolTag);
VOID MmFreeMappingAddress(PVOID BaseAddress, ULONG PoolTag);
PVOID MmMapLockedPagesWithReservedMapping(PVOID MappingAddress, ULONG PoolTag, PMDL Mdl, MEMORY_CACHING_TYPE CacheType);
VOID MmUnmapReservedMapping(PVOID BaseAddress, ULONG PoolTag, PMDL Mdl);
