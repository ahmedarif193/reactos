/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Physical memory map from the Open Firmware /memory node
 */

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(MEMORY);

ULONG
AddMemoryDescriptor(
    _Inout_ PFREELDR_MEMORY_DESCRIPTOR List,
    _In_ ULONG MaxCount,
    _In_ PFN_NUMBER BasePage,
    _In_ PFN_NUMBER PageCount,
    _In_ TYPE_OF_MEMORY MemoryType);

#define OFW_MAX_DESCRIPTORS 64
#define OFW_MAX_RANGES      32

/* One extra zeroed slot terminates the list for AddMemoryDescriptor. */
static FREELDR_MEMORY_DESCRIPTOR OfwMemoryMap[OFW_MAX_DESCRIPTORS + 1];
static ULONG OfwMemoryMapCount;

/* The kernel reaches memory through the 256 MiB KSEG0 BAT only. */
#define OFW_MEMORY_LIMIT PPC_LOADER_KSEG0_SIZE

static
VOID
OfwAddRange(_In_ ULONG Base, _In_ ULONG Size, _In_ TYPE_OF_MEMORY Type, _In_ BOOLEAN Inner)
{
    ULONG First, Last;

    if (Base >= OFW_MEMORY_LIMIT || Size == 0)
        return;
    if (Size > OFW_MEMORY_LIMIT - Base)
        Size = OFW_MEMORY_LIMIT - Base;

    /* Free ranges shrink to whole pages, reserved ranges grow to them. */
    if (Inner)
    {
        First = (Base + PPC_PAGE_SIZE - 1) >> PPC_PAGE_SHIFT;
        Last = (Base + Size) >> PPC_PAGE_SHIFT;
    }
    else
    {
        First = Base >> PPC_PAGE_SHIFT;
        Last = (Base + Size + PPC_PAGE_SIZE - 1) >> PPC_PAGE_SHIFT;
    }
    if (Last <= First)
        return;

    OfwMemoryMapCount = AddMemoryDescriptor(OfwMemoryMap, OFW_MAX_DESCRIPTORS, First, Last - First, Type);
}

/* Reads a (base, size) list; the 40p root uses one address and one size cell. */
static
ULONG
OfwReadRanges(_In_ OFW_HANDLE Node, _In_z_ PCSTR Property, _Out_writes_(OFW_MAX_RANGES * 2) PULONG Ranges)
{
    LONG Length = OfwGetProp(Node, Property, Ranges, OFW_MAX_RANGES * 2 * sizeof(ULONG));
    ULONG Count, i;

    if (Length <= 0)
        return 0;

    Count = (ULONG)Length / (2 * sizeof(ULONG));
    for (i = 0; i < Count * 2; i++)
        Ranges[i] = OfwCellToHost(Ranges[i]);
    return Count;
}

PFREELDR_MEMORY_DESCRIPTOR
OfwMemGetMemoryMap(ULONG *MemoryMapSize)
{
    ULONG Ranges[OFW_MAX_RANGES * 2];
    OFW_HANDLE Memory;
    ULONG Count, i, Total = 0;

    RtlZeroMemory(OfwMemoryMap, sizeof(OfwMemoryMap));
    OfwMemoryMapCount = 0;

    Memory = OfwFindDevice("/memory");
    if (Memory == OFW_INVALID)
    {
        ERR("Open Firmware has no /memory node\n");
        *MemoryMapSize = 0;
        return OfwMemoryMap;
    }

    /* Every RAM range starts out owned by the firmware... */
    Count = OfwReadRanges(Memory, "reg", Ranges);
    for (i = 0; i < Count; i++)
    {
        TRACE("OFW RAM %08lx+%08lx\n", Ranges[2 * i], Ranges[2 * i + 1]);
        OfwAddRange(Ranges[2 * i], Ranges[2 * i + 1], LoaderFirmwarePermanent, FALSE);
        Total += Ranges[2 * i + 1];
    }
    OfwMachine.MemorySize = Total;

    /* ...except what it reports as available. Stage0 claimed its own image
     * and the loader image beforehand, so these are not included. */
    Count = OfwReadRanges(Memory, "available", Ranges);
    for (i = 0; i < Count; i++)
    {
        TRACE("OFW available %08lx+%08lx\n", Ranges[2 * i], Ranges[2 * i + 1]);
        OfwAddRange(Ranges[2 * i], Ranges[2 * i + 1], LoaderFree, TRUE);
    }

    /* Stage0 (with the big-endian firmware gate and the loader stack) must
     * survive until the kernel handoff; the kernel may then reclaim it. */
    OfwAddRange(OfwStage0Info->Stage0Base, OfwStage0Info->Stage0Size, LoaderFirmwareTemporary, FALSE);
    OfwAddRange(OfwStage0Info->ImageBase, OfwStage0Info->ImageSize, LoaderLoadedProgram, FALSE);
    if (OfwStage0Info->InitrdSize != 0)
        OfwAddRange(OfwStage0Info->InitrdBase, OfwStage0Info->InitrdSize, LoaderFirmwareTemporary, FALSE);

    /* Pages 0-3 hold the exception vectors; the kernel installs its own there. */
    OfwAddRange(0, 4 * PPC_PAGE_SIZE, LoaderFirmwarePermanent, FALSE);

    *MemoryMapSize = OfwMemoryMapCount;
    return OfwMemoryMap;
}

VOID
OfwGetExtendedBIOSData(PULONG ExtendedBIOSDataArea, PULONG ExtendedBIOSDataSize)
{
    *ExtendedBIOSDataArea = 0;
    *ExtendedBIOSDataSize = 0;
}
