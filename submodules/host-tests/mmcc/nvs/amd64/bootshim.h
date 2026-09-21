/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/amd64/bootshim.h
 * PURPOSE:     AMD64 bootstrap host-test compatibility definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <nvs/include/mienv.h>

typedef enum _TYPE_OF_MEMORY
{
    LoaderFree, LoaderMemoryData, LoaderBad, LoaderFirmwarePermanent,
    LoaderSpecialMemory, LoaderHALCachedMemory, LoaderBBTMemory, LoaderFirmwareTemporary
} TYPE_OF_MEMORY;

typedef struct _MEMORY_ALLOCATION_DESCRIPTOR
{
    LIST_ENTRY ListEntry;
    TYPE_OF_MEMORY MemoryType;
    ULONG64 BasePage;
    ULONG64 PageCount;
} MEMORY_ALLOCATION_DESCRIPTOR, *PMEMORY_ALLOCATION_DESCRIPTOR;

typedef struct _LOADER_PARAMETER_BLOCK_HOST
{
    LIST_ENTRY MemoryDescriptorListHead;
} LOADER_PARAMETER_BLOCK;

PMI_PTE MiAmd64BootSlot(ULONG64 Address, ULONG Level);
PVOID MiKmAllocate(SIZE_T Bytes);
VOID MiKmFree(PVOID Block);
VOID MiBootInvalidate(PVOID Address);
NTSTATUS MiAmd64PreparePhysicalMap(PLOADER_PARAMETER_BLOCK Loader);
#define __invlpg MiBootInvalidate
