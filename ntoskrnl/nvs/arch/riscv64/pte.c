/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/riscv64/pte.c
 * PURPOSE:     RISC-V 64-bit page-table entry operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include "hardware.h"

/* A writable leaf is always readable: W without R is reserved. NT
 * PAGE_EXECUTE stays readable, as the loader expects for FILE_EXECUTE-only
 * image handles on the other NT targets. Copy-on-write leaves are read-only
 * with RSW bit 8 set. */
static const MI_PTE MiRiscvAccess[8] =
{
    0,
    MI_RISCV_PTE_READ,
    MI_RISCV_PTE_READ | MI_RISCV_PTE_EXECUTE,
    MI_RISCV_PTE_READ | MI_RISCV_PTE_EXECUTE,
    MI_RISCV_PTE_READ | MI_RISCV_PTE_WRITE,
    MI_RISCV_PTE_READ | MI_RISCV_PTE_COPYONWRITE,
    MI_RISCV_PTE_READ | MI_RISCV_PTE_WRITE | MI_RISCV_PTE_EXECUTE,
    MI_RISCV_PTE_READ | MI_RISCV_PTE_EXECUTE | MI_RISCV_PTE_COPYONWRITE
};

MI_PTE
MiArchPteMakeLeaf(
    _In_ ULONG64 Frame,
    _In_ ULONG Protection,
    _In_ ULONG Flags)
{
    MI_PTE Pte;

    if (!MI_PROT_IS_ACCESSIBLE(Protection) || (Protection & MI_PROT_GUARD))
        return 0;

    /* A is always set: with Svade a clear bit would fault on every access. */
    Pte = MI_RISCV_PTE_VALID | MI_RISCV_PTE_ACCESSED | MiRiscvAccess[Protection & MI_PROT_ACCESS_MASK] |
          ((Frame << MI_RISCV_PTE_PFN_SHIFT) & MI_RISCV_PTE_PFN_MASK);

    if (Flags & MI_LEAF_USER)
        Pte |= MI_RISCV_PTE_OWNER;
    else if (Flags & MI_LEAF_GLOBAL)
        Pte |= MI_RISCV_PTE_GLOBAL;

    /* A writable leaf without D is clean: the first store sets D, in
     * hardware with Svadu or through a page fault with Svade. */
    if ((Pte & MI_RISCV_PTE_WRITE) && (Flags & MI_LEAF_DIRTY))
        Pte |= MI_RISCV_PTE_DIRTY;

    /* Caching comes from the platform's physical memory attributes: device
     * ranges are already I/O and RAM stays cacheable, which the coherent DMA
     * the HAL requires allows. No Svpbmt type is encoded: a non-cacheable
     * alias of RAM would need cache-block maintenance against the cacheable
     * direct map. */
    return Pte;
}

MI_PTE
MiArchPteMakeTable(
    _In_ ULONG64 Frame,
    _In_ ULONG Flags)
{
    /* U, A and D are reserved in non-leaf entries. */
    UNREFERENCED_PARAMETER(Flags);
    return MI_RISCV_PTE_VALID | ((Frame << MI_RISCV_PTE_PFN_SHIFT) & MI_RISCV_PTE_PFN_MASK);
}

BOOLEAN
MiArchPteIsBlock(_In_ MI_PTE Pte, _In_ ULONG Level)
{
    return Level != 0 && MiRiscvPteIsLeaf(Pte);
}

MI_PTE
MiArchPteMakeBlock(_In_ ULONG64 Frame, _In_ ULONG Protection, _In_ ULONG Flags)
{
    /* Leaves have one format at every level. */
    return MiArchPteMakeLeaf(Frame, Protection, Flags);
}

BOOLEAN
MiArchPteIsValid(_In_ MI_PTE Pte)
{
    return (Pte & MI_RISCV_PTE_VALID) != 0;
}

ULONG64
MiArchPteFrame(_In_ MI_PTE Pte)
{
    return (Pte & MI_RISCV_PTE_PFN_MASK) >> MI_RISCV_PTE_PFN_SHIFT;
}

BOOLEAN
MiArchPteIsWritable(_In_ MI_PTE Pte)
{
    return MiRiscvPteIsLeaf(Pte) && (Pte & MI_RISCV_PTE_WRITE) != 0;
}

BOOLEAN
MiArchPteIsHardwareWritable(_In_ MI_PTE Pte)
{
    return MiArchPteIsWritable(Pte);
}

BOOLEAN
MiArchPteIsCopyOnWrite(_In_ MI_PTE Pte)
{
    return MiRiscvPteIsLeaf(Pte) && (Pte & MI_RISCV_PTE_COPYONWRITE) != 0;
}

ULONG
MiArchPteLeafFlags(_In_ MI_PTE Pte)
{
    /* Leaves carry no cache attribute (see MiArchPteMakeLeaf). */
    UNREFERENCED_PARAMETER(Pte);
    return 0;
}

BOOLEAN
MiArchPteIsDirty(_In_ MI_PTE Pte)
{
    return MiRiscvPteIsLeaf(Pte) && (Pte & (MI_RISCV_PTE_WRITE | MI_RISCV_PTE_DIRTY)) ==
                                     (MI_RISCV_PTE_WRITE | MI_RISCV_PTE_DIRTY);
}

BOOLEAN
MiArchPteIsAccessed(_In_ MI_PTE Pte)
{
    return MiRiscvPteIsLeaf(Pte) && (Pte & MI_RISCV_PTE_ACCESSED) != 0;
}

BOOLEAN
MiArchPteIsLeafDescriptor(_In_ MI_PTE Pte)
{
    return MiRiscvPteIsLeaf(Pte);
}

BOOLEAN
MiArchIsSelfMapAddress(_In_ ULONG64 VirtualAddress)
{
    /* Sv39 has no recursive mapping; page tables are reached through the
     * direct map. */
    UNREFERENCED_PARAMETER(VirtualAddress);
    return FALSE;
}

BOOLEAN
MiArchPteIsUser(_In_ MI_PTE Pte)
{
    return MiRiscvPteIsLeaf(Pte) && (Pte & MI_RISCV_PTE_OWNER) != 0;
}

BOOLEAN
MiArchPteIsExecutable(_In_ MI_PTE Pte, _In_ BOOLEAN UserMode)
{
    /* Supervisor code never executes from a U leaf, and user code only
     * executes from U leaves. */
    return MiRiscvPteIsLeaf(Pte) && (Pte & MI_RISCV_PTE_EXECUTE) &&
           (((Pte & MI_RISCV_PTE_OWNER) != 0) == (UserMode != FALSE));
}

MI_PTE
MiArchPteSetDirty(_In_ MI_PTE Pte, _In_ BOOLEAN Dirty)
{
    if (!MiRiscvPteIsLeaf(Pte) || !(Pte & MI_RISCV_PTE_WRITE))
        return Pte;

    return Dirty ? (Pte | MI_RISCV_PTE_DIRTY) : (Pte & ~MI_RISCV_PTE_DIRTY);
}

MI_PTE
MiArchPteSetAccessed(_In_ MI_PTE Pte, _In_ BOOLEAN Accessed)
{
    if (!MiRiscvPteIsLeaf(Pte))
        return Pte;

    return Accessed ? (Pte | MI_RISCV_PTE_ACCESSED) : (Pte & ~MI_RISCV_PTE_ACCESSED);
}

BOOLEAN
MiArchPteNeedsBreak(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    /* Sv39 permits replacing a valid leaf in place; the caller invalidates
     * the translation afterwards. */
    UNREFERENCED_PARAMETER(Old);
    UNREFERENCED_PARAMETER(New);
    return FALSE;
}

VOID
MiArchInitializeProcessRoot(_In_ ULONG64 SystemRootFrame, _In_ ULONG64 ProcessRootFrame)
{
    PMI_PTE System = MiArchMapFrame(SystemRootFrame);
    PMI_PTE Process = MiArchMapFrame(ProcessRootFrame);
    ULONG Index;

    MI_ASSERT(SystemRootFrame != ProcessRootFrame);

    /* The system root owns a populated table for every supervisor slot, so
     * sharing the root entries shares all kernel mappings. */
    for (Index = 0; Index < 512; Index++)
    {
        MI_PTE Value = (Index >= MI_RISCV_ROOT_KERNEL_INDEX) ? MiArchPteRead(&System[Index]) : 0;

        MiArchPteWrite(&Process[Index], Value);
    }

    MiArchUnmapFrame(Process);
    MiArchUnmapFrame(System);
}
