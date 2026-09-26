/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/generic/memory.c
 * PURPOSE:         HAL memory management
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* Share with Mm headers? */
#define MM_HAL_HEAP_START   (PVOID)(MM_HAL_VA_START + (1024 * 1024))

/* GLOBALS *******************************************************************/

PVOID HalpHeapStart = MM_HAL_HEAP_START;


/* PRIVATE FUNCTIONS *********************************************************/

PVOID
NTAPI
HalpMapPhysicalMemory64(IN PHYSICAL_ADDRESS PhysicalAddress,
                        IN PFN_COUNT PageCount)
{
    return HalpMapPhysicalMemory64Vista(PhysicalAddress, PageCount, TRUE);
}

VOID
NTAPI
HalpUnmapVirtualAddress(IN PVOID VirtualAddress,
                        IN PFN_COUNT PageCount)
{
    HalpUnmapVirtualAddressVista(VirtualAddress, PageCount, TRUE);
}

PVOID
NTAPI
HalpMapPhysicalMemory64Vista(IN PHYSICAL_ADDRESS PhysicalAddress,
                             IN PFN_COUNT PageCount,
                             IN BOOLEAN FlushCurrentTLB)
{
    PHARDWARE_PTE PointerPte;
    PFN_NUMBER UsedPages = 0;
    PVOID VirtualAddress, BaseAddress;

    /* Start at the current HAL heap base */
    BaseAddress = HalpHeapStart;
    VirtualAddress = BaseAddress;

    /* Loop until we have all the pages required */
    while (UsedPages < PageCount)
    {
        /* If this overflows past the HAL heap, it means there's no space */
        if (VirtualAddress == NULL) return NULL;

        /* Get the PTE for this address */
        PointerPte = HalAddressToPte(VirtualAddress);

        /* Go to the next page */
        VirtualAddress = (PVOID)((ULONG_PTR)VirtualAddress + PAGE_SIZE);

        /* Check if the page is available */
        if (PointerPte->Valid)
        {
            /* PTE has data, skip it and start with a new base address */
            BaseAddress = VirtualAddress;
            UsedPages = 0;
            continue;
        }

        /* PTE is available, keep going on this run */
        UsedPages++;
    }

    /* Take the base address of the page plus the actual offset in the address */
    VirtualAddress = (PVOID)((ULONG_PTR)BaseAddress +
                             BYTE_OFFSET(PhysicalAddress.LowPart));

    /* If we are starting at the heap, move the heap */
    if (BaseAddress == HalpHeapStart)
    {
        /* Past this allocation */
        HalpHeapStart = (PVOID)((ULONG_PTR)BaseAddress + (PageCount * PAGE_SIZE));
    }

    /* Loop pages that can be mapped */
    while (UsedPages--)
    {
        /* Fill out the PTE */
        PointerPte = HalAddressToPte(BaseAddress);
        PointerPte->PageFrameNumber = (PFN_NUMBER)(PhysicalAddress.QuadPart >> PAGE_SHIFT);
        PointerPte->Valid = 1;
        PointerPte->Write = 1;

        /* Move to the next address */
        PhysicalAddress.QuadPart += PAGE_SIZE;
        BaseAddress = (PVOID)((ULONG_PTR)BaseAddress + PAGE_SIZE);
    }

    /* Flush the TLB and return the address */
    if (FlushCurrentTLB)
        HalpFlushTLB();

    return VirtualAddress;
}

VOID
NTAPI
HalpUnmapVirtualAddressVista(IN PVOID VirtualAddress,
                             IN PFN_COUNT PageCount,
                             IN BOOLEAN FlushCurrentTLB)
{
    PHARDWARE_PTE PointerPte;
    ULONG i;

    /* Only accept valid addresses */
    if (VirtualAddress < (PVOID)MM_HAL_VA_START) return;

    /* Align it down to page size */
    VirtualAddress = (PVOID)((ULONG_PTR)VirtualAddress & ~(PAGE_SIZE - 1));

    /* Loop PTEs */
    PointerPte = HalAddressToPte(VirtualAddress);
    for (i = 0; i < PageCount; i++)
    {
        *(PULONG)PointerPte = 0;
        PointerPte++;
    }

    /* Flush the TLB */
    if (FlushCurrentTLB)
        HalpFlushTLB();

    /* Put the heap back */
    if (HalpHeapStart > VirtualAddress) HalpHeapStart = VirtualAddress;
}

