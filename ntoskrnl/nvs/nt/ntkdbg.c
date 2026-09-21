/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntkdbg.c
 * PURPOSE:     Memory manager debugger interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

#ifdef KDBG
#include <kdbg/kdb.h>

static const PCSTR MiKdbStateNames[] =
{
    "zeroed", "free", "standby", "modified", "bad", "active", "transition", "cached", "unusable"
};

static const PCSTR MiKdbVadNames[] = { "private", "mapped", "image", "system", "physical" };

static
BOOLEAN
MiKdbParseAddress(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _Out_ PULONG64 Value)
{
    ULONGLONG Parsed = 0;

    if (Argc < 2 || !KdbpGetHexNumber(Argv[1], &Parsed))
        return FALSE;

    *Value = Parsed;
    return TRUE;
}

static
PMI_ADDRESS_SPACE
MiKdbSpaceFor(
    _In_ ULONG64 Address)
{
    PEPROCESS Process = PsGetCurrentProcess();

    if (Address >= (ULONG64)(ULONG_PTR)MmSystemRangeStart)
        return &MiSystem.SystemSpace;

    return (MI_PROCESS_OF(Process) != NULL) ? MiSpaceOfProcess(Process) : NULL;
}

static
PMI_VAD
MiKdbFindVad(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Address)
{
    PMI_VAD_NODE Node = MiVadFind(&Space->VadRoot, Address >> PAGE_SHIFT);

    return (Node != NULL) ? CONTAINING_RECORD(Node, MI_VAD, Node) : NULL;
}

BOOLEAN
ExpKdbgExtPte(
    ULONG Argc,
    PCHAR Argv[])
{
    const MI_ARCH_DESCRIPTOR *Arch = MiSystem.Arch;
    PMI_ADDRESS_SPACE Space;
    ULONG64 Address, Frame;
    LONG Level;

    if (!MiKdbParseAddress(Argc, Argv, &Address) || (Space = MiKdbSpaceFor(Address)) == NULL)
    {
        KdbpPrint("Usage: !pte address\n");
        return TRUE;
    }

    Frame = Space->RootFrame;

    for (Level = Arch->PagingLevels - 1; Level >= 0; Level--)
    {
        ULONG Index = (ULONG)((Address >> Arch->Level[Level].Shift) & Arch->Level[Level].IndexMask);
        MI_PTE Pte = MiArchPteRead((PMI_PTE)MiArchMapFrame(Frame) + Index);

        KdbpPrint("L%ld table %I64x index %lu entry %016I64x", Level, Frame, Index, Pte);

        if (!MiArchPteIsValid(Pte))
        {
            KdbpPrint(" invalid kind %u prot %x value %I64x\n", MiSoftKind(Pte), MiSoftProtection(Pte),
                      MiSoftValue(Pte));
            return TRUE;
        }

        if (Level == 0 || MiArchPteIsBlock(Pte, (ULONG)Level))
        {
            KdbpPrint(" %s frame %I64x%s%s%s\n", (Level == 0) ? "page" : "block", MiArchPteFrame(Pte),
                      MiArchPteIsWritable(Pte) ? " W" : "", MiArchPteIsDirty(Pte) ? " D" : "",
                      MiArchPteIsCopyOnWrite(Pte) ? " COW" : "");
            return TRUE;
        }

        KdbpPrint(" table\n");
        Frame = MiArchPteFrame(Pte);
    }

    return TRUE;
}

BOOLEAN
ExpKdbgExtPfn(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG64 Frame;
    PMI_PFN Entry;

    if (!MiKdbParseAddress(Argc, Argv, &Frame) || Frame >= MiSystem.Pfn.FrameCount)
    {
        KdbpPrint("Usage: !pfn page-frame-number (below %lx)\n", MiSystem.Pfn.FrameCount);
        return TRUE;
    }

    Entry = &MiSystem.Pfn.Pfn[Frame];
    KdbpPrint("frame %I64x state %s share %ld ref %u flags %02x\n", Frame,
              (Entry->State < RTL_NUMBER_OF(MiKdbStateNames)) ? MiKdbStateNames[Entry->State] : "?",
              Entry->ShareCount, Entry->ReferenceCount, Entry->Flags);
    KdbpPrint("  pte address %I64x pte frame %lx original %016I64x used entries %ld\n", Entry->PteAddress,
              Entry->PteFrame, Entry->OriginalPte, Entry->UsedEntries);
    return TRUE;
}

static
VOID
MiKdbPrintVad(
    _In_ PMI_VAD Vad)
{
    KdbpPrint("%016I64x - %016I64x %-8s prot %02x commit %I64d%s\n", Vad->Node.StartingVpn << PAGE_SHIFT,
              ((Vad->Node.EndingVpn + 1) << PAGE_SHIFT) - 1,
              (Vad->Type < RTL_NUMBER_OF(MiKdbVadNames)) ? MiKdbVadNames[Vad->Type] : "?", Vad->Protection,
              Vad->CommitCharge, Vad->MemCommit ? " memcommit" : "");
}

BOOLEAN
ExpKdbgExtVad(
    ULONG Argc,
    PCHAR Argv[])
{
    PMI_ADDRESS_SPACE Space;
    PMI_VAD_NODE Node;
    ULONG64 Address;
    ULONG Count = 0;

    if (MiKdbParseAddress(Argc, Argv, &Address))
    {
        PMI_VAD Vad;

        Space = MiKdbSpaceFor(Address);
        Vad = (Space != NULL) ? MiKdbFindVad(Space, Address) : NULL;

        if (Vad == NULL)
            KdbpPrint("No VAD contains %I64x\n", Address);
        else
            MiKdbPrintVad(Vad);

        return TRUE;
    }

    Space = MiKdbSpaceFor(0);
    if (Space == NULL)
    {
        KdbpPrint("The current process has no address space\n");
        return TRUE;
    }

    for (Node = MiVadFirst(&Space->VadRoot); Node != NULL && Count < 512; Node = MiVadNext(Node), Count++)
        MiKdbPrintVad(CONTAINING_RECORD(Node, MI_VAD, Node));

    KdbpPrint("%lu of %lu VADs shown\n", Count, (ULONG)Space->VadRoot.NodeCount);
    return TRUE;
}

BOOLEAN
ExpKdbgExtAddress(
    ULONG Argc,
    PCHAR Argv[])
{
    PMI_ADDRESS_SPACE Space;
    ULONG64 Address, Physical;
    PMI_VAD Vad;

    if (!MiKdbParseAddress(Argc, Argv, &Address) || (Space = MiKdbSpaceFor(Address)) == NULL)
    {
        KdbpPrint("Usage: !address address\n");
        return TRUE;
    }

    Vad = MiKdbFindVad(Space, Address);
    if (Vad != NULL)
        MiKdbPrintVad(Vad);
    else
        KdbpPrint("%I64x is not inside any VAD\n", Address);

    if (MiPtTranslate(Space, Address, &Physical, NULL))
        KdbpPrint("physical %I64x\n", Physical);
    else
        KdbpPrint("not resident\n");

    return TRUE;
}

BOOLEAN
ExpKdbgExtVm(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG64 Counts[MiPageListCount] = { 0 };
    ULONG Shard, List;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    for (Shard = 0; Shard < MI_PFN_SHARDS; Shard++)
    {
        for (List = 0; List < MiPageListCount; List++)
            Counts[List] += MiSystem.Pfn.Shard[Shard].List[List].Count;
    }

    KdbpPrint("physical pages %lu frames %lu\n", (ULONG)MmNumberOfPhysicalPages, MiSystem.Pfn.FrameCount);
    KdbpPrint("zeroed %I64u free %I64u standby %I64u modified %I64u bad %I64u\n", Counts[MiPageZeroed],
              Counts[MiPageFree], Counts[MiPageStandby], Counts[MiPageModified], Counts[MiPageBad]);
    KdbpPrint("committed %I64d limit %I64d repurposed %I64d processes %lu\n", MiSystem.CommittedPages,
              MiSystem.CommitLimit, MiSystem.Pfn.Repurposed, MiProcessManager.ProcessCount);
    KdbpPrint("system space: resident %I64d page tables %I64d faults %I64d\n", MiSystem.SystemSpace.ResidentPages,
              MiSystem.SystemSpace.PageTablePages, MiSystem.SystemSpace.Faults);

    if (MiSystem.SystemPtes != NULL)
    {
        KdbpPrint("system PTEs: base %I64x pages %I64u free %I64d\n", MiSystem.SystemPtes->Base,
                  MiSystem.SystemPtes->PageCount, MiSystem.SystemPtes->FreePages);
    }

    if (MiSystem.PageFile != NULL)
    {
        KdbpPrint("pagefile: slots %I64u in use %I64u written %I64d read %I64d\n", MiSystem.PageFile->SlotCount,
                  MiSystem.PageFile->SlotsInUse, MiSystem.PageFile->PagesWritten, MiSystem.PageFile->PagesRead);
    }

    return TRUE;
}
#endif
