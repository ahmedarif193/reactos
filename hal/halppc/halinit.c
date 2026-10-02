/*
 * PROJECT:     LiberNT PowerPC HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     PReP HAL initialization
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

ULONG_PTR HalpPpcIoBase;
ULONG HalpPpcDmaOffset;
ULONG HalpPpcPciMemoryBase;
ULONG HalpPpcTimebaseFrequency;
static ULONG HalpPpcInitializationPhase;

BOOLEAN
NTAPI
HalInitSystem(
    _In_ ULONG BootPhase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PPPC_LOADER_BLOCK PpcBlock;

    if (BootPhase == 1)
    {
        if (HalpPpcInitializationPhase != 1)
            return FALSE;

        /* Arm the decrementer and let interrupts in. Deferred DPCs queued
         * during phase 0 run as soon as the IRQL drops. */
        HalpPpcStartClock();
        HalpPpcInitializationPhase = 2;
        _enable();
        return TRUE;
    }
    if ((BootPhase != 0) || (HalpPpcInitializationPhase != 0) || (LoaderBlock == NULL))
        return FALSE;

    PpcBlock = &LoaderBlock->u.PowerPC;
    if ((PpcBlock->Version != PPC_LOADER_BLOCK_VERSION) || (PpcBlock->Size != sizeof(*PpcBlock)) || (PpcBlock->MachineType != PPC_MACHINE_PREP) || !PpcBlock->IsaIoVirtualBase)
        return FALSE;

    HalpPpcIoBase = PpcBlock->IsaIoVirtualBase;
    HalpPpcDmaOffset = PpcBlock->PciDmaOffset;
    HalpPpcPciMemoryBase = PpcBlock->PciMemoryPhysicalBase;
    HalpPpcTimebaseFrequency = PpcBlock->TimebaseFrequency ? PpcBlock->TimebaseFrequency : 25000000;

    HalpPpcInitializePic();

    /* Prefer a measured time base frequency over the firmware's report. */
    {
        ULONG Measured = HalpPpcCalibrateTimebase();

        if (Measured)
            HalpPpcTimebaseFrequency = Measured;
        DbgPrint("HAL: PReP, time base %lu Hz (firmware reported %lu Hz)\n", HalpPpcTimebaseFrequency, PpcBlock->TimebaseFrequency);
    }
    KeSetDmaIoCoherency(1);
    HalInitPnpDriver = HaliInitPnpDriver;
    KeSetTimeIncrement(PPC_HAL_MAXIMUM_INCREMENT, PPC_HAL_MINIMUM_INCREMENT);
    HalpPpcInitializationPhase = 1;
    return TRUE;
}
