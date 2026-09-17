/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Bootstrap HAL initialization
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

typedef enum _RISCV_HAL_INITIALIZATION_FAILURE
{
    RiscvHalNoFailure = 0,
    RiscvHalInvalidBootPhase,
    RiscvHalInvalidLoaderContract,
    RiscvHalInvalidDeviceTree,
    RiscvHalSbiUnavailable,
    RiscvHalPciMappingFailed,
    RiscvHalPlicMappingFailed
} RISCV_HAL_INITIALIZATION_FAILURE;

volatile ULONG HalpRiscvInitializationPhase;
volatile ULONG HalpRiscvInitializationFailure;
ULONG64 HalpRiscvTimebaseFrequency;
ULONG64 HalpRiscvBootCounter;
ULONG HalpRiscvCurrentTimeIncrement = RISCV_HAL_MAXIMUM_INCREMENT;

VOID NTAPI KeSetTimeIncrement(ULONG MaximumIncrement, ULONG MinimumIncrement);

BOOLEAN
NTAPI
HalInitSystem(
    _In_ ULONG BootPhase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PRISCV64_LOADER_BLOCK RiscvBlock;

    if (BootPhase == 1)
    {
        if (HalpRiscvInitializationPhase != 1)
        {
            HalpRiscvInitializationFailure = RiscvHalInvalidBootPhase;
            return FALSE;
        }

        if (!HalpRiscvMapPciConfig())
        {
            HalpRiscvInitializationFailure = RiscvHalPciMappingFailed;
            return FALSE;
        }
        if (!HalpRiscvMapPlic())
        {
            HalpRiscvInitializationFailure = RiscvHalPlicMappingFailed;
            return FALSE;
        }

        /* Arm the first deadline, then unmask STIE through the kernel's
         * IRQL-owned `sie` model. Only then enable supervisor interrupts
         * globally: the phase-1 thread inherited SIE clear from the idle
         * loop, which unmasks around wfi only once a source is enabled.
         * Requests already recorded in Pcr->SoftwareInterrupts (DPCs
         * queued during phase 0/1) are taken as soon as SIE is set. */
        HalpRiscvFeatureFlags = KiRiscvQueryFeatureFlags();
        HalpRiscvStartClock();
        KiRiscvSetInterruptEnabled(RISCV_HAL_SIE_STIE, TRUE);
        HalpRiscvInitializationPhase = 2;
        __asm__ __volatile__("csrsi sstatus, 2" ::: "memory");
        return TRUE;
    }
    if ((BootPhase != 0) || (HalpRiscvInitializationPhase != 0))
    {
        HalpRiscvInitializationFailure = RiscvHalInvalidBootPhase;
        return FALSE;
    }
    if (LoaderBlock == NULL)
    {
        HalpRiscvInitializationFailure = RiscvHalInvalidLoaderContract;
        return FALSE;
    }

    RiscvBlock = &LoaderBlock->u.Riscv64;
    if ((RiscvBlock->Version != RISCV64_LOADER_BLOCK_VERSION) ||
        (RiscvBlock->Size != sizeof(*RiscvBlock)) ||
        ((RiscvBlock->Flags & RISCV64_LOADER_REQUIRED_FLAGS) !=
         RISCV64_LOADER_REQUIRED_FLAGS) ||
        (RiscvBlock->Flags &
         ~(RISCV64_LOADER_REQUIRED_FLAGS | RISCV64_LOADER_OPTIONAL_FLAGS)) ||
        (RiscvBlock->DeviceTree == 0) ||
        (RiscvBlock->DeviceTreeSize > MAXULONG))
    {
        HalpRiscvInitializationFailure = RiscvHalInvalidLoaderContract;
        return FALSE;
    }

    if (!HalpRiscvReadTimebaseFrequency(
            (const VOID *)(ULONG_PTR)RiscvBlock->DeviceTree,
            (SIZE_T)RiscvBlock->DeviceTreeSize,
            &HalpRiscvTimebaseFrequency))
    {
        HalpRiscvInitializationFailure = RiscvHalInvalidDeviceTree;
        return FALSE;
    }
    if (!HalpRiscvInitializePlic((const VOID *)(ULONG_PTR)RiscvBlock->DeviceTree,
                                 (SIZE_T)RiscvBlock->DeviceTreeSize,
                                 RiscvBlock->BootHartId))
    {
        HalpRiscvInitializationFailure = RiscvHalInvalidDeviceTree;
        return FALSE;
    }
    if (!HalpRiscvInitializePci((const VOID *)(ULONG_PTR)RiscvBlock->DeviceTree, (SIZE_T)RiscvBlock->DeviceTreeSize))
    {
        HalpRiscvInitializationFailure = RiscvHalInvalidDeviceTree;
        return FALSE;
    }
    if (HalpRiscvPciDmaCoherent())
        KeSetDmaIoCoherency(1);
    if (!HalpRiscvInitializeSbi())
    {
        HalpRiscvInitializationFailure = RiscvHalSbiUnavailable;
        return FALSE;
    }

    /* Reading time is part of the selected supervisor platform contract. An
     * unavailable CSR traps instead of silently substituting an invented QPC. */
    HalpRiscvBootCounter = HalpRiscvReadTime();
    KeSetTimeIncrement(RISCV_HAL_MAXIMUM_INCREMENT,
                       RISCV_HAL_MINIMUM_INCREMENT);
    HalpRiscvInitializationFailure = RiscvHalNoFailure;
    HalpRiscvInitializationPhase = 1;
    return TRUE;
}
