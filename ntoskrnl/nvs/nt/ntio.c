/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntio.c
 * PURPOSE:     NT I/O memory mapping interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

PFN_NUMBER MmLowestPhysicalPage = (PFN_NUMBER)-1;
PFN_NUMBER MmHighestPhysicalPage;
PFN_COUNT MmNumberOfPhysicalPages;
PPHYSICAL_MEMORY_DESCRIPTOR MmPhysicalMemoryBlock;
BOOLEAN Mm64BitPhysicalAddress = TRUE;

PVOID
NTAPI
MmMapIoSpaceEx(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Protect)
{
    MI_CACHE_TYPE CacheType = (Protect & PAGE_NOCACHE) ? MiCacheNone
                                                        : ((Protect & PAGE_WRITECOMBINE) ? MiCacheWriteCombined
                                                                                         : MiCacheFull);
    ULONG64 Va;

    if (!NT_SUCCESS(MiMapIoSpace(&MiSystem, (ULONG64)PhysicalAddress.QuadPart, NumberOfBytes, CacheType, &Va)))
        return NULL;

    return (PVOID)(ULONG_PTR)Va;
}

PVOID
NTAPI
MmMapIoSpace(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    ULONG64 Va;

    if (!NT_SUCCESS(MiMapIoSpace(&MiSystem, (ULONG64)PhysicalAddress.QuadPart, NumberOfBytes,
                                 MiCacheTypeFromNt(CacheType), &Va)))
    {
        return NULL;
    }

    return (PVOID)(ULONG_PTR)Va;
}

VOID
NTAPI
MmUnmapIoSpace(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes)
{
    MiUnmapIoSpace(&MiSystem, (ULONG64)(ULONG_PTR)BaseAddress, NumberOfBytes);
}

PVOID
NTAPI
MmMapVideoDisplay(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    return MmMapIoSpace(PhysicalAddress, NumberOfBytes, CacheType);
}

VOID
NTAPI
MmUnmapVideoDisplay(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes)
{
    MmUnmapIoSpace(BaseAddress, NumberOfBytes);
}

PVOID
NTAPI
MmAllocateContiguousNodeMemory(
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS LowestAcceptableAddress,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
    _In_opt_ PHYSICAL_ADDRESS BoundaryAddressMultiple,
    _In_ ULONG Protect,
    _In_ NODE_REQUIREMENT PreferredNode)
{
    MI_CACHE_TYPE CacheType = (Protect & PAGE_NOCACHE) ? MiCacheNone
                                                        : ((Protect & PAGE_WRITECOMBINE) ? MiCacheWriteCombined
                                                                                         : MiCacheFull);
    ULONG Attempts = 0;
    NTSTATUS Status;
    ULONG64 Va;

    UNREFERENCED_PARAMETER(PreferredNode);

    do
    {
        Status = MiAllocateContiguousMemory(&MiSystem, NumberOfBytes, (ULONG64)LowestAcceptableAddress.QuadPart,
                                            (ULONG64)HighestAcceptableAddress.QuadPart,
                                            (ULONG64)BoundaryAddressMultiple.QuadPart, CacheType, &Va);
    } while (Status == STATUS_INSUFFICIENT_RESOURCES && Attempts++ < 2 &&
             MiBalanceMemory(&MiSystem, &MiProcessManager) != 0);

    return NT_SUCCESS(Status) ? (PVOID)(ULONG_PTR)Va : NULL;
}

PVOID
NTAPI
MmAllocateContiguousMemorySpecifyCacheNode(
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS LowestAcceptableAddress,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
    _In_opt_ PHYSICAL_ADDRESS BoundaryAddressMultiple,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ NODE_REQUIREMENT PreferredNode)
{
    ULONG Protect = PAGE_READWRITE;

    if ((CacheType & 0xFF) == MmNonCached)
        Protect |= PAGE_NOCACHE;
    else if ((CacheType & 0xFF) == MmWriteCombined)
        Protect |= PAGE_WRITECOMBINE;

    return MmAllocateContiguousNodeMemory(NumberOfBytes, LowestAcceptableAddress, HighestAcceptableAddress,
                                          BoundaryAddressMultiple, Protect, PreferredNode);
}

PVOID
NTAPI
MmAllocateContiguousMemorySpecifyCache(
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS LowestAcceptableAddress,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
    _In_opt_ PHYSICAL_ADDRESS BoundaryAddressMultiple,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    return MmAllocateContiguousMemorySpecifyCacheNode(NumberOfBytes, LowestAcceptableAddress,
                                                      HighestAcceptableAddress, BoundaryAddressMultiple, CacheType,
                                                      MM_ANY_NODE_OK);
}

PVOID
NTAPI
MmAllocateContiguousMemory(
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress)
{
    PHYSICAL_ADDRESS Zero;

    Zero.QuadPart = 0;
    return MmAllocateContiguousMemorySpecifyCache(NumberOfBytes, Zero, HighestAcceptableAddress, Zero, MmCached);
}

VOID
NTAPI
MmFreeContiguousMemorySpecifyCache(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    UNREFERENCED_PARAMETER(CacheType);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    MiFreeContiguousMemory(&MiSystem, (ULONG64)(ULONG_PTR)BaseAddress);
}

VOID
NTAPI
MmFreeContiguousMemory(
    _In_ PVOID BaseAddress)
{
    MiFreeContiguousMemory(&MiSystem, (ULONG64)(ULONG_PTR)BaseAddress);
}

PVOID
NTAPI
MmAllocateNonCachedMemory(
    _In_ SIZE_T NumberOfBytes)
{
    PHYSICAL_ADDRESS Zero, Highest;

    Zero.QuadPart = 0;
    Highest.QuadPart = -1;
    return MmAllocateContiguousMemorySpecifyCache(NumberOfBytes, Zero, Highest, Zero, MmNonCached);
}

VOID
NTAPI
MmFreeNonCachedMemory(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes)
{
    UNREFERENCED_PARAMETER(NumberOfBytes);
    MiFreeContiguousMemory(&MiSystem, (ULONG64)(ULONG_PTR)BaseAddress);
}

PHYSICAL_ADDRESS
NTAPI
MmGetPhysicalAddress(
    _In_ PVOID BaseAddress)
{
    PHYSICAL_ADDRESS Physical;

    Physical.QuadPart = (LONGLONG)MiGetPhysicalAddress(MiSpaceForAddress(BaseAddress),
                                                       (ULONG64)(ULONG_PTR)BaseAddress);
    return Physical;
}

PVOID
NTAPI
MmGetVirtualForPhysical(
    _In_ PHYSICAL_ADDRESS PhysicalAddress)
{
    ULONG64 Frame = (ULONG64)PhysicalAddress.QuadPart >> PAGE_SHIFT;

    if (Frame < MiSystem.Pfn.FrameCount)
    {
        PMI_PFN Entry = &MiSystem.Pfn.Pfn[Frame];
        ULONG64 Slot = Entry->PteAddress;
        ULONG64 Va, Mapped;

        if (Slot != 0 && !(MI_PFN_FLAGS(Entry) & (MI_PFN_FLAG_PROTOTYPE | MI_PFN_FLAG_PAGE_TABLE)) &&
            MiPtVirtualAddressFromSlot(&MiSystem.SystemSpace, Slot, &Va) &&
            MiPtTranslate(&MiSystem.SystemSpace, Va, &Mapped, NULL) && (Mapped >> PAGE_SHIFT) == Frame)
        {
            return (PVOID)(ULONG_PTR)(Va + BYTE_OFFSET(PhysicalAddress.LowPart));
        }
    }

    return MiArchMapFrame((ULONG64)PhysicalAddress.QuadPart >> PAGE_SHIFT) == NULL
               ? NULL
               : (PVOID)((PUCHAR)MiArchMapFrame((ULONG64)PhysicalAddress.QuadPart >> PAGE_SHIFT) +
                         BYTE_OFFSET(PhysicalAddress.LowPart));
}

BOOLEAN
NTAPI
MmIsAddressValid(
    _In_ PVOID VirtualAddress)
{
    ULONG64 Physical;

    if (!MI_IS_SYSTEM_VA(VirtualAddress) && PsGetCurrentProcess()->Vm.Instance.VmWorkingSetList == NULL)
        return FALSE;

    return MiPtTranslate(MiSpaceForAddress(VirtualAddress), (ULONG64)(ULONG_PTR)VirtualAddress, &Physical, NULL);
}

BOOLEAN
NTAPI
MmIsNonPagedSystemAddressValid(
    _In_ PVOID VirtualAddress)
{
    return (BOOLEAN)(MI_IS_SYSTEM_VA(VirtualAddress) && MmIsAddressValid(VirtualAddress));
}

LOGICAL
NTAPI
MmIsIoSpaceActive(
    _In_ PHYSICAL_ADDRESS StartAddress,
    _In_ SIZE_T NumberOfBytes)
{
    ULONG64 First = (ULONG64)StartAddress.QuadPart >> PAGE_SHIFT;
    ULONG64 Last = ((ULONG64)StartAddress.QuadPart + NumberOfBytes - 1) >> PAGE_SHIFT;
    ULONG64 Frame;

    for (Frame = First; Frame <= Last; Frame++)
    {
        if (Frame < MiSystem.Pfn.FrameCount && MiSystem.Pfn.Pfn[Frame].State != MiPageUnusable)
            return FALSE;
    }

    return TRUE;
}

BOOLEAN
NTAPI
MmIsThisAnNtAsSystem(VOID)
{
    return (BOOLEAN)(SharedUserData->NtProductType != NtProductWinNt);
}

PPHYSICAL_MEMORY_RANGE
NTAPI
MmGetPhysicalMemoryRangesEx(
    _In_opt_ PVOID PartitionObject)
{
    PPHYSICAL_MEMORY_RANGE Ranges;
    ULONG Runs = (MmPhysicalMemoryBlock != NULL) ? MmPhysicalMemoryBlock->NumberOfRuns : 0;
    ULONG i;

    UNREFERENCED_PARAMETER(PartitionObject);

    Ranges = ExAllocatePoolWithTag(NonPagedPool, (Runs + 1) * sizeof(PHYSICAL_MEMORY_RANGE), 'hPmM');
    if (Ranges == NULL)
        return NULL;

    for (i = 0; i < Runs; i++)
    {
        Ranges[i].BaseAddress.QuadPart = (LONGLONG)MmPhysicalMemoryBlock->Run[i].BasePage << PAGE_SHIFT;
        Ranges[i].NumberOfBytes.QuadPart = (LONGLONG)MmPhysicalMemoryBlock->Run[i].PageCount << PAGE_SHIFT;
    }

    Ranges[Runs].BaseAddress.QuadPart = 0;
    Ranges[Runs].NumberOfBytes.QuadPart = 0;
    return Ranges;
}

PPHYSICAL_MEMORY_RANGE
NTAPI
MmGetPhysicalMemoryRanges(VOID)
{
    return MmGetPhysicalMemoryRangesEx(NULL);
}

NTSTATUS
NTAPI
MmAddPhysicalMemory(
    _In_ PPHYSICAL_ADDRESS StartAddress,
    _Inout_ PLARGE_INTEGER NumberOfBytes)
{
    UNREFERENCED_PARAMETER(StartAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
MmRemovePhysicalMemory(
    _In_ PPHYSICAL_ADDRESS StartAddress,
    _Inout_ PLARGE_INTEGER NumberOfBytes)
{
    UNREFERENCED_PARAMETER(StartAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
MmMarkPhysicalMemoryAsBad(
    _In_ PPHYSICAL_ADDRESS StartAddress,
    _Inout_ PLARGE_INTEGER NumberOfBytes)
{
    UNREFERENCED_PARAMETER(StartAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
MmMarkPhysicalMemoryAsGood(
    _In_ PPHYSICAL_ADDRESS StartAddress,
    _Inout_ PLARGE_INTEGER NumberOfBytes)
{
    UNREFERENCED_PARAMETER(StartAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    return STATUS_NOT_SUPPORTED;
}
