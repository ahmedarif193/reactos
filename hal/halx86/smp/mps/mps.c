/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Source File for MPS specific functions
 * COPYRIGHT:   Copyright 2021 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include <smp.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

HALP_APIC_INFO_TABLE HalpApicInfoTable;

static PROCESSOR_IDENTITY HalpStaticProcessorIdentity[MAXIMUM_PROCESSORS];
const PPROCESSOR_IDENTITY HalpProcessorIdentity = HalpStaticProcessorIdentity;

/* FUNCTIONS ******************************************************************/

VOID
HalpParseApicTables(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    // TODO: Fill HalpStaticProcessorIdentity[].
    UNIMPLEMENTED;
}

VOID
HalpPrintApicTables(VOID)
{
#if DBG
    ULONG i;

    DPRINT1("Physical processor count: %lu\n", HalpApicInfoTable.ProcessorCount);
    for (i = 0; i < HalpApicInfoTable.ProcessorCount; i++)
    {
        DPRINT1(" Processor %lu: ProcessorId %lu, LapicId %u, ProcessorStarted %u, BSPCheck %u, ProcessorPrcb %p\n",
                i,
                HalpProcessorIdentity[i].ProcessorId,
                HalpProcessorIdentity[i].LapicId,
                HalpProcessorIdentity[i].ProcessorStarted,
                HalpProcessorIdentity[i].BSPCheck,
                HalpProcessorIdentity[i].ProcessorPrcb);
    }
#endif
}
