/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Initialize the APIC HAL
 * COPYRIGHT:   Copyright 2011 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include <hal.h>
#include "apicp.h"
#include <smp.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
ApicInitializeLocalApic(ULONG Cpu);

extern PPROCESSOR_IDENTITY HalpProcessorIdentity;
extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

/* FUNCTIONS ****************************************************************/

static
VOID
HalpEnsureBootProcessorFirst(VOID)
{
    PROCESSOR_IDENTITY Identity;
    ULONG BootApicId;
    ULONG i;

    BootApicId = ApicRead(APIC_ID) >> 24;
    for (i = 0; i < HalpApicInfoTable.ProcessorCount; i++)
    {
        if (HalpProcessorIdentity[i].LapicId == BootApicId)
        {
            if (i != 0)
            {
                Identity = HalpProcessorIdentity[0];
                HalpProcessorIdentity[0] = HalpProcessorIdentity[i];
                HalpProcessorIdentity[i] = Identity;
            }

            HalpProcessorIdentity[0].BSPCheck = TRUE;
            return;
        }
    }

    i = min(HalpApicInfoTable.ProcessorCount, MAXIMUM_PROCESSORS - 1);
    while (i != 0)
    {
        HalpProcessorIdentity[i] = HalpProcessorIdentity[i - 1];
        i--;
    }

    RtlZeroMemory(&HalpProcessorIdentity[0], sizeof(HalpProcessorIdentity[0]));
    HalpProcessorIdentity[0].ProcessorId = BootApicId;
    HalpProcessorIdentity[0].LapicId = (UCHAR)BootApicId;
    HalpProcessorIdentity[0].BSPCheck = TRUE;
    if (HalpApicInfoTable.ProcessorCount < MAXIMUM_PROCESSORS)
        HalpApicInfoTable.ProcessorCount++;
}

VOID
NTAPI
HalpInitProcessor(
    IN ULONG ProcessorNumber,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (ProcessorNumber == 0)
    {
        HalpParseApicTables(LoaderBlock);
    }

    /* Initialize the local APIC for this cpu */
    ApicInitializeLocalApic(ProcessorNumber);

    if (ProcessorNumber == 0)
        HalpEnsureBootProcessorFirst();

    HalpSetupProcessorsTable(ProcessorNumber);

    /* Initialize profiling data (but don't start it) */
    HalInitializeProfiling();

    /* Initialize the timer */
    //ApicInitializeTimer(ProcessorNumber);
}

VOID
HalpInitPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    DPRINT1("Using HAL: APIC %s %s\n",
            (HalpBuildType & PRCB_BUILD_UNIPROCESSOR) ? "UP" : "SMP",
            (HalpBuildType & PRCB_BUILD_DEBUG) ? "DBG" : "REL");

    HalpPrintApicTables();

    /* Enable clock interrupt handler */
    HalpEnableInterruptHandler(IDT_INTERNAL,
                               0,
                               APIC_CLOCK_VECTOR,
                               CLOCK2_LEVEL,
                               HalpClockInterrupt,
                               Latched);

    /* Enable profile interrupt handler */
    HalpEnableInterruptHandler(IDT_DEVICE,
                               0,
                               APIC_PROFILE_VECTOR,
                               APIC_PROFILE_LEVEL,
                               HalpProfileInterrupt,
                               Latched);
}

VOID
HalpInitPhase1(VOID)
{
    /* Initialize DMA. NT does this in Phase 0 */
    HalpInitDma();
}

/* EOF */
