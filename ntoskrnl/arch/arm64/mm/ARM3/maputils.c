/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/mm/ARM3/maputils.c
 * PURPOSE:         Small ARM64 MM helpers used by common ARM3 code
 *
 * Rationale:
 *  - The boot loader (FreeLdr) often maps initial images using upper-level
 *    block entries (L1/L2) for identity and hand-off mappings. Common code
 *    in ARM3 (e.g., MiReloadBootLoadedDrivers) that assumed a final-level
 *    leaf PTE would assert on ARM64 during bring-up.
 *  - These helpers let shared code ask simple questions without forcing a
 *    leaf-PTE assumption: "is this VA mapped at all?" and "is the given PTE
 *    a leaf entry?". The first uses MmGetPhysicalAddress which tolerates
 *    block descriptors; the second remains a straight check of the PTE.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

BOOLEAN
MiArm64VaIsMapped(
    _In_ PVOID Va)
{
    PHYSICAL_ADDRESS Pa = MmGetPhysicalAddress(Va);
    return (Pa.QuadPart != 0);
}

BOOLEAN
MiArm64PteIsLeaf(
    _In_ PMMPTE Pte)
{
    return (Pte->u.Hard.Valid == 1);
}

