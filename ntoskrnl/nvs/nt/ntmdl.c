/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntmdl.c
 * PURPOSE:     NT memory descriptor list interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

#define MI_MDL_PFN_END ((PFN_NUMBER)-1)

BOOLEAN MmTrackLockedPages = FALSE;

static
ULONG
MiMdlPages(
    _In_ PMDL Mdl)
{
    return ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), Mdl->ByteCount);
}

PMDL
NTAPI
MmCreateMdl(
    _In_opt_ PMDL Mdl,
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    SIZE_T Size;

    if (Mdl == NULL)
    {
        Size = MmSizeOfMdl(Base, Length);
        Mdl = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_MDL);
        if (Mdl == NULL)
            return NULL;
    }

    MmInitializeMdl(Mdl, Base, Length);
    return Mdl;
}

SIZE_T
NTAPI
MmSizeOfMdl(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    return sizeof(MDL) + (ADDRESS_AND_SIZE_TO_SPAN_PAGES(Base, Length) * sizeof(PFN_NUMBER));
}

VOID
NTAPI
MmBuildMdlForNonPagedPool(
    _Inout_ PMDL Mdl)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG Count = MiMdlPages(Mdl);
    BOOLEAN IoSpace = FALSE;
    ULONG i;

    ASSERT(Mdl->ByteCount != 0);
    ASSERT((Mdl->MdlFlags & (MDL_PAGES_LOCKED | MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL |
                             MDL_PARTIAL)) == 0);

    for (i = 0; i < Count; i++)
    {
        ULONG64 Physical = MiGetPhysicalAddress(&MiSystem.SystemSpace,
                                                (ULONG64)(ULONG_PTR)Mdl->StartVa + (ULONG64)i * PAGE_SIZE);

        ASSERT(Physical != 0);
        Pages[i] = (PFN_NUMBER)(Physical >> PAGE_SHIFT);

        if (Pages[i] >= MiSystem.Pfn.FrameCount)
            IoSpace = TRUE;
    }

    Mdl->Process = NULL;
    Mdl->MappedSystemVa = (PVOID)((PUCHAR)Mdl->StartVa + Mdl->ByteOffset);
    Mdl->MdlFlags |= MDL_SOURCE_IS_NONPAGED_POOL;

    if (IoSpace)
        Mdl->MdlFlags |= MDL_IO_SPACE;
}

VOID
NTAPI
MmProbeAndLockPages(
    _Inout_ PMDL Mdl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    PVOID Base = MmGetMdlVirtualAddress(Mdl);
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG Count = MiMdlPages(Mdl);
    PMI_ADDRESS_SPACE Space;
    ULONG Attempts = 0;
    NTSTATUS Status;
    ULONG i;

    ASSERT(Mdl->ByteCount != 0);
    ASSERT((Mdl->MdlFlags & (MDL_PAGES_LOCKED | MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL |
                             MDL_PARTIAL | MDL_IO_SPACE)) == 0);

    if (MI_IS_SYSTEM_VA(Base))
    {
        if (AccessMode != KernelMode)
            ExRaiseStatus(STATUS_ACCESS_VIOLATION);

        Space = &MiSystem.SystemSpace;
        Mdl->Process = NULL;
    }
    else
    {
        if ((ULONG_PTR)Base + Mdl->ByteCount > (ULONG_PTR)MmHighestUserAddress + 1 ||
            (ULONG_PTR)Base + Mdl->ByteCount < (ULONG_PTR)Base)
        {
            ExRaiseStatus(STATUS_ACCESS_VIOLATION);
        }

        Space = MiSpaceOfProcess(PsGetCurrentProcess());
        Mdl->Process = PsGetCurrentProcess();
    }

    do
    {
        Status = MiLockPages(Space, (ULONG64)(ULONG_PTR)Mdl->StartVa, Count,
                             (BOOLEAN)(AccessMode != KernelMode && Space != &MiSystem.SystemSpace),
                             (BOOLEAN)(Operation != IoReadAccess), (PMI_FRAME_NUMBER)Pages);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    if (!NT_SUCCESS(Status))
    {
        Mdl->Process = NULL;
        ExRaiseStatus(Status);
    }

    for (i = 0; i < Count; i++)
    {
        if (Pages[i] >= MiSystem.Pfn.FrameCount)
            Mdl->MdlFlags |= MDL_IO_SPACE;
    }

    Mdl->MdlFlags |= MDL_PAGES_LOCKED;
    if (Operation != IoReadAccess)
        Mdl->MdlFlags |= MDL_WRITE_OPERATION;
    else
        Mdl->MdlFlags &= ~MDL_WRITE_OPERATION;

    if (Mdl->Process != NULL)
        InterlockedExchangeAddSizeT(&Mdl->Process->NumberOfLockedPages, Count);
}

VOID
NTAPI
MmProbeAndLockProcessPages(
    _Inout_ PMDL Mdl,
    _In_ PEPROCESS Process,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, AccessMode, Operation);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (Attached)
        KeUnstackDetachProcess(&ApcState);

    if (!NT_SUCCESS(Status))
        ExRaiseStatus(Status);
}

VOID
NTAPI
MmProbeAndLockSelectedPages(
    _Inout_ PMDL Mdl,
    _In_ LARGE_INTEGER PageList[],
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG Count = MiMdlPages(Mdl);
    ULONG64 *Addresses;
    SIZE_T Bytes = (SIZE_T)Count * sizeof(*Addresses);
    PEPROCESS Process = PsGetCurrentProcess();
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Attempts = 0;
    BOOLEAN UserPages = FALSE;
    ULONG i;

    ASSERT(Mdl->ByteCount != 0);
    ASSERT((Mdl->MdlFlags & (MDL_PAGES_LOCKED | MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL |
                             MDL_PARTIAL | MDL_IO_SPACE)) == 0);
    Mdl->Process = NULL;
    Addresses = ExAllocatePoolWithTag(NonPagedPool, Bytes, TAG_MDL);
    if (Addresses == NULL)
        ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

    _SEH2_TRY
    {
        if (AccessMode != KernelMode)
            ProbeForRead(PageList, Bytes, TYPE_ALIGNMENT(LARGE_INTEGER));
        RtlCopyMemory(Addresses, PageList, Bytes);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status))
    {
        do
        {
            Status = MiLockSelectedPages(MiSpaceOfProcess(Process), Addresses, Count,
                                          (BOOLEAN)(AccessMode != KernelMode),
                                          (BOOLEAN)(Operation != IoReadAccess), (PMI_FRAME_NUMBER)Pages);
        } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);
    }

    if (NT_SUCCESS(Status))
    {
        for (i = 0; i < Count; i++)
        {
            if (Addresses[i] < (ULONG64)(ULONG_PTR)MmSystemRangeStart)
                UserPages = TRUE;
            if (Pages[i] >= MiSystem.Pfn.FrameCount)
                Mdl->MdlFlags |= MDL_IO_SPACE;
        }
    }
    ExFreePoolWithTag(Addresses, TAG_MDL);
    if (!NT_SUCCESS(Status))
        ExRaiseStatus(Status);

    Mdl->MdlFlags |= MDL_PAGES_LOCKED;
    if (Operation != IoReadAccess)
        Mdl->MdlFlags |= MDL_WRITE_OPERATION;
    else
        Mdl->MdlFlags &= ~MDL_WRITE_OPERATION;
    if (UserPages)
    {
        Mdl->Process = Process;
        InterlockedExchangeAddSizeT(&Process->NumberOfLockedPages, Count);
    }
}

VOID
NTAPI
MmUnlockPages(
    _Inout_ PMDL Mdl)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG Count = MiMdlPages(Mdl);

    ASSERT(Mdl->MdlFlags & MDL_PAGES_LOCKED);
    ASSERT(!(Mdl->MdlFlags & (MDL_SOURCE_IS_NONPAGED_POOL | MDL_PARTIAL)));

    if (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
        MmUnmapLockedPages(Mdl->MappedSystemVa, Mdl);

    MiUnlockFrames(&MiSystem, (const MI_FRAME_NUMBER *)Pages, Count,
                   (BOOLEAN)((Mdl->MdlFlags & MDL_WRITE_OPERATION) != 0));

    if (Mdl->Process != NULL)
        InterlockedExchangeAddSizeT(&Mdl->Process->NumberOfLockedPages, -(SSIZE_T)Count);

    Mdl->MdlFlags &= ~(MDL_PAGES_LOCKED | MDL_IO_SPACE);
}

PVOID
NTAPI
MmMapLockedPagesSpecifyCache(
    _In_ PMDL Mdl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_opt_ PVOID BaseAddress,
    _In_ ULONG BugCheckOnFailure,
    _In_ ULONG Priority)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG Count = MiMdlPages(Mdl);
    ULONG Protection = (Priority & MdlMappingNoWrite) ? MI_PROT_READONLY : MI_PROT_READWRITE;
    BOOLEAN WriteCombinedRam;
    NTSTATUS Status;
    ULONG64 Base;

    ASSERT(Mdl->ByteCount != 0);

    WriteCombinedRam = (BOOLEAN)(!(Mdl->MdlFlags & MDL_IO_SPACE) && Pages[0] < MiSystem.Pfn.FrameCount &&
                                 !(MI_PFN_FLAGS(&MiSystem.Pfn.Pfn[Pages[0]]) & MI_PFN_FLAG_PAGE_TABLE) &&
                                 MiSystem.Pfn.Pfn[Pages[0]].UsedEntries == MI_NT_PFN_WRITE_COMBINED);

    if (AccessMode == KernelMode)
    {
        ASSERT(!(Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_PARTIAL_HAS_BEEN_MAPPED)));
        ASSERT(Mdl->MdlFlags & (MDL_PAGES_LOCKED | MDL_PARTIAL | MDL_IO_SPACE | MDL_SOURCE_IS_NONPAGED_POOL));

        Status = MiMapFrames(&MiSystem, (const MI_FRAME_NUMBER *)Pages, Count,
                             (Mdl->MdlFlags & MDL_IO_SPACE) ? MiCacheTypeFromNt(CacheType)
                             : (WriteCombinedRam ? MiCacheWriteCombined
                                                 : ((CacheType & 0xFF) == MmNonCached ? MiCacheNone : MiCacheFull)),
                             Protection, &Base);
        if (!NT_SUCCESS(Status))
        {
            if (BugCheckOnFailure)
                KeBugCheckEx(NO_MORE_SYSTEM_PTES, 0, Count, 0, 0);

            return NULL;
        }

        Mdl->MappedSystemVa = (PVOID)(ULONG_PTR)(Base + Mdl->ByteOffset);
        Mdl->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;

        if (Mdl->MdlFlags & MDL_PARTIAL)
            Mdl->MdlFlags |= MDL_PARTIAL_HAS_BEEN_MAPPED;

        return Mdl->MappedSystemVa;
    }

    Base = (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress);
    Status = MiMapFramesUser(MiSpaceOfProcess(PsGetCurrentProcess()), (const MI_FRAME_NUMBER *)Pages, Count,
                             Protection,
                             (Mdl->MdlFlags & MDL_IO_SPACE)
                                 ? (((CacheType & 0xFF) == MmNonCached) ? MI_LEAF_NOCACHE
                                    : (((CacheType & 0xFF) == MmWriteCombined) ? MI_LEAF_WRITECOMBINE : 0))
                                 : (WriteCombinedRam ? MI_LEAF_WRITECOMBINE : 0),
                             &Base);
    if (!NT_SUCCESS(Status))
        ExRaiseStatus(Status);

    return (PVOID)(ULONG_PTR)(Base + Mdl->ByteOffset);
}

PVOID
NTAPI
MmMapLockedPages(
    _In_ PMDL Mdl,
    _In_ KPROCESSOR_MODE AccessMode)
{
    return MmMapLockedPagesSpecifyCache(Mdl, AccessMode, MmCached, NULL, TRUE, HighPagePriority);
}

VOID
NTAPI
MmUnmapLockedPages(
    _In_ PVOID BaseAddress,
    _In_ PMDL Mdl)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG Count = MiMdlPages(Mdl);
    ULONG64 Base = (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress);

    if (!MI_IS_SYSTEM_VA(BaseAddress))
    {
        MiUnmapFramesUser(MiSpaceOfProcess(PsGetCurrentProcess()), Base);
        return;
    }

    ASSERT(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA);

    if (Mdl->MdlFlags & MDL_FREE_EXTRA_PTES)
    {
        ULONG Extra = (ULONG)Pages[Count];

        Base -= (ULONG64)Extra << PAGE_SHIFT;
        Count += Extra;
        Mdl->MdlFlags &= ~MDL_FREE_EXTRA_PTES;
    }

    MiUnmapFrames(&MiSystem, Base, Count);
    Mdl->MdlFlags &= ~(MDL_MAPPED_TO_SYSTEM_VA | MDL_PARTIAL_HAS_BEEN_MAPPED);
    Mdl->MappedSystemVa = NULL;
}

NTSTATUS
NTAPI
MmAdvanceMdl(
    _Inout_ PMDL Mdl,
    _In_ ULONG NumberOfBytes)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG Total = MiMdlPages(Mdl);
    ULONG NewOffset;
    ULONG Skipped;

    if (NumberOfBytes >= Mdl->ByteCount)
        return STATUS_INVALID_PARAMETER_2;

    NewOffset = Mdl->ByteOffset + NumberOfBytes;
    Skipped = NewOffset >> PAGE_SHIFT;

    if (Skipped != 0)
    {
        ULONG Extra = (Mdl->MdlFlags & MDL_FREE_EXTRA_PTES) ? (ULONG)Pages[Total] : 0;

        if (Mdl->MdlFlags & MDL_PAGES_LOCKED)
        {
            MiUnlockFrames(&MiSystem, (const MI_FRAME_NUMBER *)Pages, Skipped,
                           (BOOLEAN)((Mdl->MdlFlags & MDL_WRITE_OPERATION) != 0));

            if (Mdl->Process != NULL)
                InterlockedExchangeAddSizeT(&Mdl->Process->NumberOfLockedPages, -(SSIZE_T)Skipped);
        }

        RtlMoveMemory(Pages, Pages + Skipped, (Total - Skipped) * sizeof(PFN_NUMBER));
        Mdl->StartVa = (PVOID)((PUCHAR)Mdl->StartVa + ((ULONG_PTR)Skipped << PAGE_SHIFT));

        if (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
        {
            Pages[Total - Skipped] = Extra + Skipped;
            Mdl->MdlFlags |= MDL_FREE_EXTRA_PTES;
        }
    }

    Mdl->ByteOffset = NewOffset & (PAGE_SIZE - 1);
    Mdl->ByteCount -= NumberOfBytes;

    if (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
        Mdl->MappedSystemVa = (PVOID)((PUCHAR)Mdl->MappedSystemVa + NumberOfBytes);

    return STATUS_SUCCESS;
}

PMDL
NTAPI
MmAllocatePagesForMdlEx(
    _In_ PHYSICAL_ADDRESS LowAddress,
    _In_ PHYSICAL_ADDRESS HighAddress,
    _In_ PHYSICAL_ADDRESS SkipBytes,
    _In_ SIZE_T TotalBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG Flags)
{
    static ULONG Cursor;
    ULONG64 Wanted = BYTES_TO_PAGES(TotalBytes);
    ULONG64 LowFrame = (ULONG64)(LowAddress.QuadPart + PAGE_SIZE - 1) >> PAGE_SHIFT;
    ULONG64 HighFrame = (ULONG64)HighAddress.QuadPart >> PAGE_SHIFT;
    PPFN_NUMBER Pages;
    ULONG64 Got = 0;
    PMDL Mdl;

    UNREFERENCED_PARAMETER(SkipBytes);
    if (Wanted == 0 || Wanted > 0xFFFFFFFFULL / PAGE_SIZE || LowFrame > HighFrame)
        return NULL;

    if (HighFrame >= MiSystem.Pfn.FrameCount)
        HighFrame = MiSystem.Pfn.FrameCount - 1;

    Mdl = MmCreateMdl(NULL, NULL, (SIZE_T)(Wanted << PAGE_SHIFT));
    if (Mdl == NULL)
        return NULL;

    Pages = MmGetMdlPfnArray(Mdl);

    while (Got < Wanted && MiChargeCommit(&MiSystem.SystemSpace, 1))
    {
        ULONG Frame = MiPfnAllocatePageInRange(&MiSystem.Pfn, (ULONG)LowFrame, (ULONG)HighFrame, &Cursor);

        if (Frame == MI_FRAME_INVALID)
        {
            MiReturnCommit(&MiSystem.SystemSpace, 1);
            break;
        }

        if (!(Flags & MM_DONT_ZERO_ALLOCATION))
            RtlZeroMemory(MiArchMapFrame(Frame), PAGE_SIZE);

        if ((CacheType & 0xFF) == MmWriteCombined)
            MiSystem.Pfn.Pfn[Frame].UsedEntries = MI_NT_PFN_WRITE_COMBINED;

        Pages[Got++] = Frame;
    }

    if (Got == 0 || ((Flags & MM_ALLOCATE_FULLY_REQUIRED) && Got != Wanted))
    {
        while (Got != 0)
        {
            MiPfnShareDecrement(&MiSystem.Pfn, (ULONG)Pages[--Got], TRUE);
            MiReturnCommit(&MiSystem.SystemSpace, 1);
        }

        ExFreePoolWithTag(Mdl, TAG_MDL);
        return NULL;
    }

    if (Got != Wanted)
        Pages[Got] = MI_MDL_PFN_END;

    Mdl->ByteCount = (ULONG)(Got << PAGE_SHIFT);
    Mdl->MdlFlags |= MDL_PAGES_LOCKED;
    Mdl->Process = NULL;
    return Mdl;
}

PMDL
NTAPI
MmAllocatePagesForMdl(
    _In_ PHYSICAL_ADDRESS LowAddress,
    _In_ PHYSICAL_ADDRESS HighAddress,
    _In_ PHYSICAL_ADDRESS SkipBytes,
    _In_ SIZE_T TotalBytes)
{
    return MmAllocatePagesForMdlEx(LowAddress, HighAddress, SkipBytes, TotalBytes, MmCached, 0);
}

PMDL
NTAPI
MmAllocateNodePagesForMdlEx(
    _In_ PHYSICAL_ADDRESS LowAddress,
    _In_ PHYSICAL_ADDRESS HighAddress,
    _In_ PHYSICAL_ADDRESS SkipBytes,
    _In_ SIZE_T TotalBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG IdealNode,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(IdealNode);
    return MmAllocatePagesForMdlEx(LowAddress, HighAddress, SkipBytes, TotalBytes, CacheType, Flags);
}

VOID
NTAPI
MmFreePagesFromMdl(
    _Inout_ PMDL Mdl)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG Count = MiMdlPages(Mdl);
    ULONG i;

    ASSERT(Mdl->MdlFlags & MDL_PAGES_LOCKED);
    ASSERT(!(Mdl->MdlFlags & MDL_IO_SPACE));

    for (i = 0; i < Count && Pages[i] != MI_MDL_PFN_END; i++)
    {
        MiSystem.Pfn.Pfn[Pages[i]].UsedEntries = 0;
        MiPfnShareDecrement(&MiSystem.Pfn, (ULONG)Pages[i], TRUE);
        MiReturnCommit(&MiSystem.SystemSpace, 1);
        Pages[i] = MI_MDL_PFN_END;
    }

    Mdl->MdlFlags &= ~MDL_PAGES_LOCKED;
}

NTSTATUS
NTAPI
MmProtectMdlSystemAddress(
    _In_ PMDL Mdl,
    _In_ ULONG NewProtect)
{
    ULONG Protection;

    if (!(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA) || !MI_IS_SYSTEM_VA(Mdl->MappedSystemVa))
        return STATUS_NOT_MAPPED_VIEW;

    if (!MiProtectionFromWin32(NewProtect, &Protection) || MI_PROT_IS_COPY(Protection) ||
        (Protection & MI_PROT_GUARD))
    {
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    return MiSystemProtect(&MiSystem, (ULONG64)(ULONG_PTR)PAGE_ALIGN(Mdl->MappedSystemVa), MiMdlPages(Mdl),
                           Protection);
}

NTSTATUS
NTAPI
MmAllocateMdlForIoSpace(
    _In_ PMM_PHYSICAL_ADDRESS_LIST PhysicalAddressList,
    _In_ SIZE_T NumberOfEntries,
    _Out_ PMDL *NewMdl)
{
    SIZE_T Total = 0;
    PPFN_NUMBER Pages;
    PMDL Mdl;
    SIZE_T i, Page = 0;

    *NewMdl = NULL;

    for (i = 0; i < NumberOfEntries; i++)
    {
        if ((PhysicalAddressList[i].PhysicalAddress.QuadPart & (PAGE_SIZE - 1)) ||
            (PhysicalAddressList[i].NumberOfBytes & (PAGE_SIZE - 1)) || PhysicalAddressList[i].NumberOfBytes == 0)
        {
            return STATUS_INVALID_PARAMETER_1;
        }

        Total += PhysicalAddressList[i].NumberOfBytes;
    }

    if (Total == 0 || Total > 0xFFFFFFFFULL - PAGE_SIZE)
        return STATUS_INVALID_PARAMETER_2;

    Mdl = MmCreateMdl(NULL, NULL, Total);
    if (Mdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Pages = MmGetMdlPfnArray(Mdl);

    for (i = 0; i < NumberOfEntries; i++)
    {
        PFN_NUMBER First = (PFN_NUMBER)(PhysicalAddressList[i].PhysicalAddress.QuadPart >> PAGE_SHIFT);
        SIZE_T Count = PhysicalAddressList[i].NumberOfBytes >> PAGE_SHIFT;
        SIZE_T j;

        for (j = 0; j < Count; j++)
            Pages[Page++] = First + j;
    }

    Mdl->MdlFlags |= MDL_PAGES_LOCKED | MDL_IO_SPACE;
    *NewMdl = Mdl;
    return STATUS_SUCCESS;
}

PVOID
NTAPI
MmAllocateMappingAddress(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG PoolTag)
{
    ULONG Pages = (ULONG)BYTES_TO_PAGES(NumberOfBytes);
    ULONG64 Base;
    PMI_PTE Slot;

    if (Pages == 0 || PoolTag == 0)
        return NULL;

    Base = MiReserveSystemPtes(&MiSystem, Pages + 2);
    if (Base == 0)
        return NULL;

    Slot = MiPtLookup(&MiSystem.SystemSpace, Base, NULL);
    MiArchPteWrite(Slot, (MI_PTE)Pages << 1);
    MiArchPteWrite(Slot + 1, (MI_PTE)PoolTag << 1);
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
    ULONG Pages = (ULONG)(MiArchPteRead(Slot) >> 1);

    if ((ULONG)(MiArchPteRead(Slot + 1) >> 1) != PoolTag)
        KeBugCheckEx(SYSTEM_PTE_MISUSE, 0x101, (ULONG_PTR)BaseAddress, PoolTag, MiArchPteRead(Slot + 1) >> 1);

    MiArchPteWrite(Slot, 0);
    MiArchPteWrite(Slot + 1, 0);
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
    ULONG Count = MiMdlPages(Mdl);

    if ((ULONG)(MiArchPteRead(Slot + 1) >> 1) != PoolTag)
        KeBugCheckEx(SYSTEM_PTE_MISUSE, 0x104, (ULONG_PTR)MappingAddress, PoolTag, MiArchPteRead(Slot + 1) >> 1);

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

    MiSystemUnmap(&MiSystem, Base, MiMdlPages(Mdl), FALSE);
    Mdl->MdlFlags &= ~(MDL_MAPPED_TO_SYSTEM_VA | MDL_PARTIAL_HAS_BEEN_MAPPED);
    Mdl->MappedSystemVa = NULL;
}

VOID
NTAPI
MmMapMemoryDumpMdl(
    _Inout_ PMDL Mdl)
{
    if (!(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA))
        (VOID)MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached, NULL, FALSE, HighPagePriority);
}

HANDLE
NTAPI
MmSecureVirtualMemory(
    _In_ PVOID Address,
    _In_ SIZE_T Length,
    _In_ ULONG Mode)
{
    PMI_ADDRESS_SPACE Space = MiSpaceOfProcess(PsGetCurrentProcess());
    ULONG64 Va = (ULONG64)(ULONG_PTR)PAGE_ALIGN(Address);
    ULONG64 End = (ULONG64)(ULONG_PTR)Address + Length;

    if (Length == 0 || MI_IS_SYSTEM_VA(Address) || End < Va)
        return NULL;

    while (Va < End)
    {
        MI_MEMORY_INFORMATION Info;

        if (!NT_SUCCESS(MiQueryVirtualMemory(Space, Va, &Info)) || Info.State != MI_MEM_COMMIT ||
            !MI_PROT_IS_ACCESSIBLE(Info.Protect) ||
            (Mode == PAGE_READWRITE && !MI_PROT_IS_WRITABLE(Info.Protect) && !MI_PROT_IS_COPY(Info.Protect)))
        {
            return NULL;
        }

        Va = Info.BaseAddress + Info.RegionSize;
    }

    return (HANDLE)Address;
}

VOID
NTAPI
MmUnsecureVirtualMemory(
    _In_ HANDLE SecureMem)
{
    UNREFERENCED_PARAMETER(SecureMem);
}
