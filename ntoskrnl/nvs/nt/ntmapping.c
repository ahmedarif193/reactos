/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntmapping.c
 * PURPOSE:     NT reserved mapping interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef MM_HOST_TEST
#include <mmcc/nvs/core/ntmappingshim.h>
#else
#include <nvs/nt/mint.h>
#endif

PVOID
NTAPI
MmAllocateMappingAddress(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG PoolTag)
{
    ULONG Pages = (ULONG)BYTES_TO_PAGES(NumberOfBytes);
    ULONG64 Base;
    PMI_PTE Slot;
    PMI_PTE TagSlot;

    if (Pages == 0 || PoolTag == 0)
        return NULL;

    Base = MiReserveSystemPtes(&MiSystem, Pages + 2);
    if (Base == 0)
        return NULL;

    Slot = MiPtLookup(&MiSystem.SystemSpace, Base, NULL);
    TagSlot = MiPtLookup(&MiSystem.SystemSpace, Base + PAGE_SIZE, NULL);
    MiArchPteWrite(Slot, (MI_PTE)Pages << 1);
    MiArchPteWrite(TagSlot, (MI_PTE)PoolTag << 1);
    return (PVOID)(ULONG_PTR)(Base + 2 * PAGE_SIZE);
}

VOID
NTAPI
MmFreeMappingAddress(
    _In_ PVOID BaseAddress,
    _In_ ULONG PoolTag)
{
    ULONG64 Base = (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress) - 2 * PAGE_SIZE;
    PMI_PTE Slot = MiPtLookup(&MiSystem.SystemSpace, Base, NULL);
    PMI_PTE TagSlot = MiPtLookup(&MiSystem.SystemSpace, Base + PAGE_SIZE, NULL);
    ULONG Pages = (ULONG)(MiArchPteRead(Slot) >> 1);

    if ((ULONG)(MiArchPteRead(TagSlot) >> 1) != PoolTag)
        KeBugCheckEx(SYSTEM_PTE_MISUSE, 0x101, (ULONG_PTR)BaseAddress, PoolTag, MiArchPteRead(TagSlot) >> 1);

    MiArchPteWrite(Slot, 0);
    MiArchPteWrite(TagSlot, 0);
    MiReleaseSystemPtes(&MiSystem, Base, Pages + 2);
}

PVOID
NTAPI
MmMapLockedPagesWithReservedMapping(
    _In_ PVOID MappingAddress,
    _In_ ULONG PoolTag,
    _In_ PMDL Mdl,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    ULONG64 Returned = (ULONG64)(ULONG_PTR)ALIGN_DOWN_POINTER_BY(MappingAddress, 16);
    ULONG64 Base = (ULONG64)(ULONG_PTR)PAGE_ALIGN(MappingAddress);
    PMI_PTE Slot = MiPtLookup(&MiSystem.SystemSpace, Base - 2 * PAGE_SIZE, NULL);
    PMI_PTE TagSlot = MiPtLookup(&MiSystem.SystemSpace, Base - PAGE_SIZE, NULL);
    ULONG Count = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), Mdl->ByteCount);

    if ((ULONG)(MiArchPteRead(TagSlot) >> 1) != PoolTag)
        KeBugCheckEx(SYSTEM_PTE_MISUSE, 0x104, (ULONG_PTR)MappingAddress, PoolTag, MiArchPteRead(TagSlot) >> 1);

    if (Count > (ULONG)(MiArchPteRead(Slot) >> 1))
        return NULL;

    if (!NT_SUCCESS(MiSystemMapFrames(&MiSystem, Base, (const MI_FRAME_NUMBER *)MmGetMdlPfnArray(Mdl), Count,
                                      MI_PROT_READWRITE,
                                      ((CacheType & 0xFF) == MmNonCached) ? MI_LEAF_NOCACHE : 0, FALSE)))
    {
        return NULL;
    }

    Mdl->MappedSystemVa = (PVOID)(ULONG_PTR)(Returned + Mdl->ByteOffset);
    Mdl->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;
    return Mdl->MappedSystemVa;
}

VOID
NTAPI
MmUnmapReservedMapping(
    _In_ PVOID BaseAddress,
    _In_ ULONG PoolTag,
    _In_ PMDL Mdl)
{
    ULONG64 Base = (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress);

    UNREFERENCED_PARAMETER(PoolTag);

    MiSystemUnmap(&MiSystem, Base, ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), Mdl->ByteCount), FALSE);
    Mdl->MdlFlags &= ~(MDL_MAPPED_TO_SYSTEM_VA | MDL_PARTIAL_HAS_BEEN_MAPPED);
    Mdl->MappedSystemVa = NULL;
}

