/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V software page-table entry conversion
 */

#include <ntoskrnl.h>

BOOLEAN
NTAPI
MiRiscvEncodeSwapPte(
    _In_ ULONG_PTR SwapEntry,
    _In_ ULONG Protection,
    _Out_ PMMPTE Pte)
{
    ULONG_PTR File = MM_SWAP_FILE_FROM_ENTRY(SwapEntry);
    ULONG_PTR Offset = MM_SWAP_OFFSET_FROM_ENTRY(SwapEntry);
    MMPTE TempPte = {{0}};

    if (Protection > 31)
        return FALSE;

    if (SwapEntry == MM_WAIT_ENTRY)
    {
        /* The full-width wait token is not a real file offset. */
        TempPte.u.Soft.PageFileHigh = MI_RISCV_PAGEFILE_WAIT;
    }
    else
    {
        if ((Offset == 0) || (Offset >= MI_RISCV_PAGEFILE_WAIT) ||
            (SwapEntry != MM_SWAP_ENTRY_FROM_FILE_OFFSET(File, Offset)))
        {
            return FALSE;
        }

        TempPte.u.Soft.PageFileLow = File;
        TempPte.u.Soft.PageFileHigh = Offset;
    }

    TempPte.u.Soft.Protection = Protection;
    *Pte = TempPte;
    return TRUE;
}

BOOLEAN
NTAPI
MiRiscvDecodeSwapPte(
    _In_ MMPTE Pte,
    _Out_ PULONG_PTR SwapEntry)
{
    ULONG_PTR Offset = Pte.u.Soft.PageFileHigh;

    if (Pte.u.Hard.Valid || Pte.u.Soft.Prototype || Pte.u.Soft.Transition ||
        Pte.u.Soft.Reserved0 || Pte.u.Soft.Reserved1 ||
        Pte.u.Soft.UsedPageTableEntries || (Offset == 0) ||
        (Offset > MI_RISCV_PAGEFILE_WAIT))
    {
        return FALSE;
    }

    if (Offset == MI_RISCV_PAGEFILE_WAIT)
    {
        if (Pte.u.Soft.PageFileLow != 0)
            return FALSE;

        *SwapEntry = MM_WAIT_ENTRY;
    }
    else
    {
        *SwapEntry = MM_SWAP_ENTRY_FROM_FILE_OFFSET(Pte.u.Soft.PageFileLow, Offset);
    }

    return TRUE;
}
