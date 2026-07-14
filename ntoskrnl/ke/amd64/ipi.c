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

/* TLB SHOOTDOWN *************************************************************/

typedef struct DECLSPEC_CACHEALIGN _KI_TB_FLUSH_PACKET
{
    volatile LONG Active;
    volatile KAFFINITY Pending;
    volatile KAFFINITY Outstanding;
    PVOID Address;
    BOOLEAN FlushEntire;
} KI_TB_FLUSH_PACKET, *PKI_TB_FLUSH_PACKET;

static KI_TB_FLUSH_PACKET KiTbFlushPacket[MAXIMUM_PROCESSORS];

FORCEINLINE
VOID
KiFlushLocalTb(
    _In_opt_ PVOID Address,
    _In_ BOOLEAN FlushEntire)
{
    if (FlushEntire)
    {
        KeFlushCurrentTb();
    }
    else
    {
        __invlpg(Address);
    }
}

VOID
NTAPI
KiIpiSendTbFlush(
    _In_opt_ PVOID Address,
    _In_ BOOLEAN FlushEntire)
{
    KAFFINITY Targets;
    BOOLEAN InterruptsEnabled;
    KIRQL OldIrql;
    PKPRCB Prcb;
    PKI_TB_FLUSH_PACKET Packet;

    InterruptsEnabled = (__readeflags() & EFLAGS_INTERRUPT_MASK) != 0;
    OldIrql = KeRaiseIrqlToSynchLevel();

    Prcb = KeGetCurrentPrcb();
    ASSERT(Prcb->Number < MAXIMUM_PROCESSORS);

    KiFlushLocalTb(Address, FlushEntire);

    Targets = (KAFFINITY)KeActiveProcessors & ~Prcb->SetMember;
    if (Targets == 0)
    {
        KeLowerIrql(OldIrql);
        return;
    }

    Packet = &KiTbFlushPacket[Prcb->Number];
    ASSERT(Packet->Active == 0);
    ASSERT(Packet->Pending == 0);
    ASSERT(Packet->Outstanding == 0);

    Packet->Address = Address;
    Packet->FlushEntire = FlushEntire;
    Packet->Pending = Targets;
    Packet->Outstanding = Targets;
    KeMemoryBarrier();
    InterlockedExchange((PLONG)&Packet->Active, 1);

    HalRequestIpi(Targets);

    while (Packet->Outstanding != 0)
    {
        if (!InterruptsEnabled)
        {
            KiIpiProcessTbFlush();
        }

        YieldProcessor();
    }

    ASSERT(Packet->Pending == 0);
    InterlockedExchange((PLONG)&Packet->Active, 0);
    KeLowerIrql(OldIrql);
}

VOID
NTAPI
KiIpiProcessTbFlush(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Cpu = Prcb->Number;
    ULONG Source;

    ASSERT(Cpu < MAXIMUM_PROCESSORS);

    for (Source = 0; Source < (ULONG)KeNumberProcessors; Source++)
    {
        PKI_TB_FLUSH_PACKET Packet = &KiTbFlushPacket[Source];

        if ((Packet->Active != 0) &&
            InterlockedBitTestAndResetAffinity(&Packet->Pending, Cpu))
        {
            KiFlushLocalTb(Packet->Address, Packet->FlushEntire);
            KeMemoryBarrier();
            InterlockedBitTestAndResetAffinity(&Packet->Outstanding, Cpu);
        }
    }
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
