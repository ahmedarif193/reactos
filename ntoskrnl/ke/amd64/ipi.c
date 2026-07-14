/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IPI code for x64
 * COPYRIGHT:   Copyright 2023 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* A hardware IPI is shared by several reasons. Keep scheduler requests durable
   until the target processor claims them. */
static volatile LONG KiIpiDpcRequest[MAXIMUM_PROCESSORS];

/* FUNCTIONS *****************************************************************/

VOID
FASTCALL
KiIpiSend(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG IpiRequest)
{
    /* Check if we can send the IPI directly */
    if (IpiRequest == IPI_APC)
    {
        HalSendSoftwareInterrupt(TargetSet, APC_LEVEL);
    }
    else if (IpiRequest == IPI_DPC)
    {
        KAFFINITY Set = TargetSet;
        ULONG Processor;

        while (BitScanForwardAffinity(&Processor, Set))
        {
            ASSERT(Processor < MAXIMUM_PROCESSORS);
            InterlockedExchange((PLONG)&KiIpiDpcRequest[Processor], 1);
            Set &= ~AFFINITY_MASK(Processor);
        }

        KeMemoryBarrier();

        /* A hardware IPI breaks an idle target out of HLT immediately. */
        HalRequestIpi(TargetSet);
    }
    else if (IpiRequest == IPI_FREEZE)
    {
        /* On x64 the freeze IPI is an NMI */
        HalSendNMI(TargetSet);
    }
    else
    {
        ASSERT(FALSE);
    }
}

BOOLEAN
NTAPI
KiIpiClaimDpcRequest(VOID)
{
    ULONG Processor = KeGetCurrentProcessorNumber();

    ASSERT(Processor < MAXIMUM_PROCESSORS);
    return InterlockedExchange((PLONG)&KiIpiDpcRequest[Processor], 0) != 0;
}

ULONG_PTR
NTAPI
KeIpiGenericCall(
    _In_ PKIPI_BROADCAST_WORKER Function,
    _In_ ULONG_PTR Argument)
{
    __debugbreak();
    return 0;
}
