/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/core/adopt.c
 * PURPOSE:     Adoption of boot-time memory mappings
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

static
NTSTATUS
MiSpaceAdoptCommon(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG RootFrame,
    _In_ BOOLEAN IsSystem)
{
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    PMI_PFN Root;

    if (RootFrame >= System->Pfn.FrameCount)
        return STATUS_INVALID_PARAMETER;

    Root = &System->Pfn.Pfn[RootFrame];
    RtlZeroMemory(Space, sizeof(*Space));
    Space->System = System;
    Space->IsSystem = IsSystem;
    Space->RootFrame = RootFrame;
    Space->LowestVa = IsSystem ? Arch->SystemAddressStart : Arch->UserAddressStart;
    Space->HighestVa = IsSystem ? Arch->SystemAddressEnd : Arch->UserAddressEnd;
    MI_RW_INIT(&Space->Lock);
    MiVadRootInitialize(&Space->VadRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);
    MiVadRootInitialize(&Space->AweRoot, 0, System->Pfn.FrameCount - 1);
    MiVadRootInitialize(&Space->CloneRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);

    if (Root->State == MiPageUnusable)
        MiPfnMarkInUse(&System->Pfn, RootFrame, 1);

    MI_ATOMIC_OR8(&Root->Flags, MI_PFN_FLAG_PAGE_TABLE | MI_PFN_FLAG_PINNED);
    Root->PageTableOwner = Space;
    return STATUS_SUCCESS;
}

NTSTATUS
MiAddressSpaceAdopt(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG RootFrame,
    _In_ BOOLEAN IsSystem)
{
    return MiSpaceAdoptCommon(System, Space, RootFrame, IsSystem);
}

static
VOID
MiAdoptTable(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG Frame,
    _In_ ULONG ParentFrame,
    _In_ ULONG ParentIndex,
    _In_ LONG Level)
{
    PMI_SYSTEM System = Space->System;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    PMI_PTE Table = MiArchMapFrame(Frame);
    ULONG Entries = (ULONG)Arch->Level[Level].EntryCount;
    PMI_PFN Entry = &System->Pfn.Pfn[Frame];
    ULONG Index;

    if (Entry->State == MiPageUnusable)
        MiPfnMarkInUse(&System->Pfn, Frame, 1);

    Entry->PteFrame = ParentFrame;
    Entry->PageTableOwner = Space;
    Entry->PteAddress = ((ULONG64)ParentFrame << PAGE_SHIFT) + (ULONG64)ParentIndex * sizeof(MI_PTE);
    MI_ATOMIC_OR8(&Entry->Flags, MI_PFN_FLAG_PAGE_TABLE | MI_PFN_FLAG_PINNED);
    MI_ATOMIC_ADD64(&Space->PageTablePages, 1);

    if (Level == 0)
        return;

    for (Index = 0; Index < Entries; Index++)
    {
        MI_PTE Pte = MiArchPteRead(&Table[Index]);
        ULONG64 Child = MiArchPteFrame(Pte);

        if (!MiArchPteIsValid(Pte) || MiArchPteIsBlock(Pte, (ULONG)Level) || Child >= System->Pfn.FrameCount)
            continue;

        if (MI_PFN_FLAGS(&System->Pfn.Pfn[Child]) & MI_PFN_FLAG_PAGE_TABLE)
            continue;

        MiAdoptTable(Space, (ULONG)Child, Frame, Index, Level - 1);
    }
}

NTSTATUS
MiSystemAdoptBootMappings(
    _Inout_ PMI_SYSTEM System)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    LONG Top = Arch->PagingLevels - 1;
    ULONG64 Span = 1ULL << Arch->Level[Top].Shift;
    PMI_PTE Root = MiArchMapFrame(Space->RootFrame);
    ULONG First = (ULONG)((Arch->SystemAddressStart >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    ULONG Last = (ULONG)((Arch->SystemAddressEnd >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    ULONG Index;

    for (Index = First; Index <= Last; Index++)
    {
        MI_PTE Pte = MiArchPteRead(&Root[Index]);
        ULONG64 Base = Arch->SystemAddressStart + (ULONG64)(Index - First) * Span;
        PMI_VAD Vad;

        if (!MiArchPteIsValid(Pte))
            continue;

        Vad = MI_ALLOCATE(sizeof(*Vad));
        if (Vad == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlZeroMemory(Vad, sizeof(*Vad));
        Vad->Type = MiVadSystem;
        Vad->Protection = MI_PROT_EXECUTE_READWRITE;
        Vad->Node.StartingVpn = Base >> PAGE_SHIFT;
        Vad->Node.EndingVpn = ((Base + Span - 1) >> PAGE_SHIFT);

        if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
        {
            MI_FREE(Vad);
            return STATUS_CONFLICTING_ADDRESSES;
        }

        if (!MiArchPteIsBlock(Pte, (ULONG)Top) && MiArchPteFrame(Pte) < System->Pfn.FrameCount &&
            !(MI_PFN_FLAGS(&System->Pfn.Pfn[MiArchPteFrame(Pte)]) & MI_PFN_FLAG_PAGE_TABLE))
        {
            MiAdoptTable(Space, (ULONG)MiArchPteFrame(Pte), Space->RootFrame, Index, Top - 1);
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MiSystemReserveTopLevelHole(
    _Inout_ PMI_SYSTEM System)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    PMI_VAD Vad;

    if (Arch->SystemReservedEnd <= Arch->SystemAddressStart)
        return STATUS_SUCCESS;

    Vad = MI_ALLOCATE(sizeof(*Vad));
    if (Vad == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Vad, sizeof(*Vad));
    Vad->Type = MiVadSystem;
    Vad->Protection = MI_PROT_NOACCESS;
    Vad->Node.StartingVpn = Arch->SystemAddressStart >> PAGE_SHIFT;
    Vad->Node.EndingVpn = (Arch->SystemReservedEnd >> PAGE_SHIFT) - 1;

    if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
    {
        MI_FREE(Vad);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MiSystemPopulateTopLevel(
    _Inout_ PMI_SYSTEM System)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    LONG Top = Arch->PagingLevels - 1;
    PMI_PTE Root = MiArchMapFrame(Space->RootFrame);
    ULONG64 Start = (Arch->SystemReservedEnd > Arch->SystemAddressStart) ? Arch->SystemReservedEnd
                                                                         : Arch->SystemAddressStart;
    ULONG First = (ULONG)((Start >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    ULONG Last = (ULONG)((Arch->SystemAddressEnd >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    ULONG Index;

    for (Index = First; Index <= Last; Index++)
    {
        ULONG Table;

        if (MiArchPteRead(&Root[Index]) != 0)
            continue;

        Table = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
        if (Table == MI_FRAME_INVALID)
            return STATUS_NO_MEMORY;

        MiPfnInitializePage(&System->Pfn, Table, ((ULONG64)Space->RootFrame << PAGE_SHIFT) +
                                                     (ULONG64)Index * sizeof(MI_PTE),
                            Space->RootFrame, 0, MI_PFN_FLAG_PAGE_TABLE | MI_PFN_FLAG_PINNED);
        System->Pfn.Pfn[Table].PageTableOwner = Space;
        MiArchPteWrite(&Root[Index], MiArchPteMakeTable(Table, 0));
        MI_ATOMIC_ADD64(&Space->PageTablePages, 1);
    }

    return STATUS_SUCCESS;
}
