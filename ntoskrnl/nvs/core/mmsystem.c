/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/core/mmsystem.c
 * PURPOSE:     Shared memory manager system initialization
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

BOOLEAN
MiChargeCommit(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ LONG64 Pages)
{
    PMI_SYSTEM System = Space->System;

    if (MI_ATOMIC_ADD64(&System->CommittedPages, Pages) + Pages > System->CommitLimit)
    {
        MI_ATOMIC_ADD64(&System->CommittedPages, -Pages);
        return FALSE;
    }

    if (Pages > 0 && Space->CommitOwner != NULL && System->ChargeOwnerCommit != NULL &&
        !System->ChargeOwnerCommit(Space->CommitOwner, Pages))
    {
        MI_ATOMIC_ADD64(&System->CommittedPages, -Pages);
        return FALSE;
    }

    MI_ATOMIC_ADD64(&Space->CommittedPages, Pages);
    return TRUE;
}

VOID
MiReturnCommit(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ LONG64 Pages)
{
    if (Pages == 0)
        return;

    MI_ATOMIC_ADD64(&Space->System->CommittedPages, -Pages);
    MI_ATOMIC_ADD64(&Space->CommittedPages, -Pages);

    if (Space->CommitOwner != NULL && Space->System->ReturnOwnerCommit != NULL)
        Space->System->ReturnOwnerCommit(Space->CommitOwner, Pages);
}

static
NTSTATUS
MiSpaceInitialize(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_ADDRESS_SPACE Space,
    _In_ BOOLEAN IsSystem)
{
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    ULONG Root;

    RtlZeroMemory(Space, sizeof(*Space));
    Space->System = System;
    Space->IsSystem = IsSystem;
    Space->LowestVa = IsSystem ? Arch->SystemAddressStart : Arch->UserAddressStart;
    Space->HighestVa = IsSystem ? Arch->SystemAddressEnd : Arch->UserAddressEnd;
    MI_RW_INIT(&Space->Lock);
    MiVadRootInitialize(&Space->VadRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);
    MiVadRootInitialize(&Space->AweRoot, 0, System->Pfn.FrameCount - 1);
    MiVadRootInitialize(&Space->CloneRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);

    Root = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
    if (Root == MI_FRAME_INVALID)
        return STATUS_NO_MEMORY;

    MiPfnInitializePage(&System->Pfn, Root, 0, 0, 0, MI_PFN_FLAG_PAGE_TABLE);
    System->Pfn.Pfn[Root].PageTableOwner = Space;
    Space->RootFrame = Root;

    if (!IsSystem)
        MiArchInitializeProcessRoot(System->SystemSpace.RootFrame, Root);

    return STATUS_SUCCESS;
}

NTSTATUS
MiSystemInitialize(
    _Out_ PMI_SYSTEM System,
    _In_ PMI_PFN PfnArray,
    _In_ ULONG FrameCount,
    _In_ ULONG CpuCount,
    _In_ LONG64 CommitLimit)
{
    RtlZeroMemory(System, sizeof(*System));
    System->Arch = MiArchDescribe();
    System->CommitLimit = CommitLimit;
    MiPfnDbInitialize(&System->Pfn, PfnArray, FrameCount, CpuCount);
    System->Pfn.Owner = System;
    MI_SPIN_INIT(&System->SegmentListLock);
    InitializeListHead(&System->SegmentList);
    InitializeListHead(&System->UnusedSegmentList);
    System->Pfn.Repurpose = MiRepurposeStandbyPage;
    return STATUS_SUCCESS;
}

NTSTATUS
MiAddressSpaceCreate(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_ADDRESS_SPACE Space)
{
    return MiSpaceInitialize(System, Space, (BOOLEAN)(Space == &System->SystemSpace));
}

VOID
MiAddressSpaceDestroy(
    _Inout_ PMI_ADDRESS_SPACE Space)
{
    MiVadFlushCache(Space);
    MiPtFlushEmpty(Space);
    MI_ASSERT(Space->VadRoot.NodeCount == 0);
    MI_ASSERT(Space->AweRoot.NodeCount == 0 && Space->AwePages == 0);
    MI_ASSERT(Space->CloneRoot.NodeCount == 0);
    MI_ASSERT(MI_ATOMIC_READ64(&Space->PageTablePages) == 0);

    MiPfnShareDecrement(&Space->System->Pfn, Space->RootFrame, TRUE);
    Space->RootFrame = 0;
}

PMI_VAD
MiVadPopFree(PMI_ADDRESS_SPACE Space)
{
    PMI_VAD Vad = Space->FreeVads;

    if (Vad != NULL)
    {
        Space->FreeVads = (PMI_VAD)Vad->Node.Left;
        Space->FreeVadCount--;
        RtlZeroMemory(Vad, sizeof(*Vad));
    }
    return Vad;
}

PMI_VAD
MiVadAllocateAndLock(PMI_ADDRESS_SPACE Space)
{
    PMI_VAD Vad;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    Vad = MiVadPopFree(Space);
    if (Vad != NULL)
        return Vad;
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    Vad = MI_ALLOCATE(sizeof(*Vad));
    if (Vad != NULL)
        RtlZeroMemory(Vad, sizeof(*Vad));
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    return Vad;
}

BOOLEAN
MiVadCacheFree(PMI_ADDRESS_SPACE Space, PMI_VAD Vad)
{
    if (Space->FreeVadCount == MI_VAD_CACHE_SIZE)
        return FALSE;
    Vad->Node.Left = (PMI_VAD_NODE)Space->FreeVads;
    Space->FreeVads = Vad;
    Space->FreeVadCount++;
    return TRUE;
}

VOID
MiVadFlushCache(PMI_ADDRESS_SPACE Space)
{
    while (Space->FreeVads != NULL)
    {
        PMI_VAD Vad = Space->FreeVads;

        Space->FreeVads = (PMI_VAD)Vad->Node.Left;
        Space->FreeVadCount--;
        MI_FREE(Vad);
    }
    MI_ASSERT(Space->FreeVadCount == 0);
}
