/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/arch/common/generic/memory.c
 * PURPOSE:         Phase 0 physical memory allocation every HAL shares
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES ******************************************************************/

#include <ntifs.h>
#include <arc/arc.h>

/* GLOBALS *******************************************************************/

static ULONG HalpUsedAllocDescriptors;
static MEMORY_ALLOCATION_DESCRIPTOR HalpAllocationDescriptorArray[64];

/* FUNCTIONS *****************************************************************/

ULONG64
NTAPI
HalpAllocPhysicalMemory(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                        IN ULONG64 MaxAddress,
                        IN PFN_NUMBER PageCount,
                        IN BOOLEAN Aligned)
{
    ULONG UsedDescriptors;
    ULONG64 PhysicalAddress;
    PFN_NUMBER MaxPage, BasePage, Alignment;
    PLIST_ENTRY NextEntry;
    PMEMORY_ALLOCATION_DESCRIPTOR MdBlock, NewBlock, FreeBlock;

    /* Highest page we'll go */
    MaxPage = MaxAddress >> PAGE_SHIFT;

    /* We need at least two blocks */
    if ((HalpUsedAllocDescriptors + 2) > RTL_NUMBER_OF(HalpAllocationDescriptorArray)) return 0;

    /* Remember how many we have now */
    UsedDescriptors = HalpUsedAllocDescriptors;

    /* Loop the loader block memory descriptors */
    NextEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
    while (NextEntry != &LoaderBlock->MemoryDescriptorListHead)
    {
        /* Get the block */
        MdBlock = CONTAINING_RECORD(NextEntry,
                                    MEMORY_ALLOCATION_DESCRIPTOR,
                                    ListEntry);

        /* No alignment by default */
        Alignment = 0;

        /* Unless requested, in which case we use a 64KB block alignment */
        if (Aligned) Alignment = ((MdBlock->BasePage + 0x0F) & ~0x0F) - MdBlock->BasePage;

        /* Search for free memory */
        if ((MdBlock->MemoryType == LoaderFree) ||
            (MdBlock->MemoryType == LoaderFirmwareTemporary))
        {
            /* Make sure the page is within bounds, including alignment */
            BasePage = MdBlock->BasePage;
            if ((BasePage) &&
                (MdBlock->PageCount >= PageCount + Alignment) &&
                (BasePage + PageCount + Alignment <= MaxPage))
            {
                /* We found an address */
                PhysicalAddress = ((ULONG64)BasePage + Alignment) << PAGE_SHIFT;
                break;
            }
        }

        /* Keep trying */
        NextEntry = NextEntry->Flink;
    }

    /* If we didn't find anything, get out of here */
    if (NextEntry == &LoaderBlock->MemoryDescriptorListHead) return 0;

    /* Okay, now get a descriptor */
    NewBlock = &HalpAllocationDescriptorArray[HalpUsedAllocDescriptors];
    NewBlock->PageCount = (ULONG)PageCount;
    NewBlock->BasePage = MdBlock->BasePage + Alignment;
    NewBlock->MemoryType = LoaderHALCachedMemory;

    /* Update count */
    UsedDescriptors++;
    HalpUsedAllocDescriptors = UsedDescriptors;

    /* Check if we had any alignment */
    if (Alignment)
    {
        /* Check if we had leftovers */
        if (MdBlock->PageCount > (PageCount + Alignment))
        {
            /* Get the next descriptor */
            FreeBlock = &HalpAllocationDescriptorArray[UsedDescriptors];
            FreeBlock->PageCount = MdBlock->PageCount - Alignment - (ULONG)PageCount;
            FreeBlock->BasePage = MdBlock->BasePage + Alignment + (ULONG)PageCount;
            FreeBlock->MemoryType = MdBlock->MemoryType;

            /* One more */
            HalpUsedAllocDescriptors++;

            /* Insert it into the list */
            InsertHeadList(&MdBlock->ListEntry, &FreeBlock->ListEntry);
        }

        /* Trim the original block to the alignment only */
        MdBlock->PageCount = Alignment;

        /* Insert the descriptor after the original one */
        InsertHeadList(&MdBlock->ListEntry, &NewBlock->ListEntry);
    }
    else
    {
        /* Consume memory from this block */
        MdBlock->BasePage += (ULONG)PageCount;
        MdBlock->PageCount -= (ULONG)PageCount;

        /* Insert the descriptor before the original one */
        InsertTailList(&MdBlock->ListEntry, &NewBlock->ListEntry);

        /* Remove the entry if the whole block was allocated */
        if (MdBlock->PageCount == 0) RemoveEntryList(&MdBlock->ListEntry);
    }

    /* Return the address */
    return PhysicalAddress;
}

/* EOF */
