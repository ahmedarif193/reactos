/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Baseline RAM and platform device caching policy
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

NTHALAPI BOOLEAN NTAPI HalpRiscvIsDeviceMemory(PHYSICAL_ADDRESS Address, SIZE_T Length);

BOOLEAN
NTAPI
MiRiscvValidateIoMapping(PHYSICAL_ADDRESS Address, SIZE_T Length, MEMORY_CACHING_TYPE CacheType)
{
    ULONG64 LastByte = (MI_RISCV_PFN_MAX << PAGE_SHIFT) + PAGE_SIZE - 1;
    PFN_NUMBER FirstPfn, LastPfn, Index;
    PHYSICAL_ADDRESS PageAddress;
    SIZE_T PageLength;

    if (!Length || Address.QuadPart < 0 || (ULONG64)Address.QuadPart > LastByte ||
        Length - 1 > LastByte - (ULONG64)Address.QuadPart ||
        (CacheType != MmCached && CacheType != MmNonCached))
        return FALSE;
    FirstPfn = (ULONG64)Address.QuadPart >> PAGE_SHIFT;
    LastPfn = ((ULONG64)Address.QuadPart + Length - 1) >> PAGE_SHIFT;
    if (LastPfn - FirstPfn >= MAXULONG)
        return FALSE;
    PageAddress.QuadPart = FirstPfn << PAGE_SHIFT;
    PageLength = (LastPfn - FirstPfn + 1) << PAGE_SHIFT;

    if (CacheType == MmNonCached && !HalpRiscvIsDeviceMemory(PageAddress, PageLength))
        return FALSE;
    for (Index = FirstPfn; Index <= LastPfn; ++Index)
    {
        PMMPFN Pfn = MiGetPfnEntry(Index);

        /* Never admit a mixed RAM/device range or a differently cached RAM
         * alias. The QEMU device PMA applies only to pages outside RAM. */
        if (CacheType == MmNonCached)
        {
            if (Pfn)
                return FALSE;
        }
        else if (!Pfn || Pfn->u3.e1.CacheAttribute != MiCached)
        {
            return FALSE;
        }
    }
    return TRUE;
}

DECLSPEC_NORETURN
VOID
NTAPI
MiRiscvUnsupportedPteCacheOperation(
    _In_ PMMPTE Pte,
    _In_ ULONG Operation)
{
    /* No baseline PTE cache override exists. In particular, do not overwrite
     * access or PFN bits, or claim a successful cache-mode transition. */
    KeBugCheckEx(MEMORY_MANAGEMENT, 0x52564341, (ULONG_PTR)Pte, Operation, STATUS_NOT_SUPPORTED);
}

BOOLEAN
NTAPI
MiRiscvIsCachedMdl(_In_ PMDL Mdl)
{
    PPFN_NUMBER Pages;
    PFN_COUNT Count, Index;

    if (Mdl->MdlFlags & MDL_IO_SPACE)
        return FALSE;

    Pages = MmGetMdlPfnArray(Mdl);
    Count = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), Mdl->ByteCount);
    for (Index = 0; Index < Count; ++Index)
    {
        PMMPFN Pfn;

        if (Pages[Index] == LIST_HEAD)
            break;
        Pfn = MiGetPfnEntry(Pages[Index]);
        if (!Pfn || Pfn->u3.e1.CacheAttribute != MiCached)
            return FALSE;
    }

    return Index != 0;
}
