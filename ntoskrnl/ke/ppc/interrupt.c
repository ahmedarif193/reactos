/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC interrupt objects and dispatch
 *
 * KiPpcTrapDispatch enters here with interrupts disabled, on the interrupted
 * kernel stack, with Pcr->CurrentIrql still at the interrupted level.
 * Every external source is serviced at KI_PPC_EXTERNAL_IRQL (the HAL reports
 * that IRQL for every vector), the decrementer at CLOCK_LEVEL.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Sources are routed to processor 0. Connect/disconnect run there at the
 * external interrupt level, excluding dispatch while the chains change. */
static KSPIN_LOCK KiPpcInterruptTableLock;
static PKINTERRUPT KiPpcInterruptTable[KI_PPC_MAX_INTERRUPT_VECTOR + 1];

VOID
NTAPI
KeInitializeInterrupt(
    PKINTERRUPT Interrupt,
    PKSERVICE_ROUTINE ServiceRoutine,
    PVOID ServiceContext,
    PKSPIN_LOCK SpinLock,
    ULONG Vector,
    KIRQL Irql,
    KIRQL SynchronizeIrql,
    KINTERRUPT_MODE InterruptMode,
    BOOLEAN ShareVector,
    CHAR ProcessorNumber,
    BOOLEAN FloatingSave)
{
    Interrupt->Type = InterruptObject;
    Interrupt->Size = sizeof(KINTERRUPT);
    KeInitializeSpinLock(&Interrupt->SpinLock);
    Interrupt->ActualLock = SpinLock ? SpinLock : &Interrupt->SpinLock;
    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->Vector = Vector;
    Interrupt->Irql = Irql;
    Interrupt->SynchronizeIrql = SynchronizeIrql;
    Interrupt->Mode = InterruptMode;
    Interrupt->ShareVector = ShareVector;
    Interrupt->Number = ProcessorNumber;
    Interrupt->FloatingSave = FloatingSave;
    Interrupt->TickCount = 0;
    Interrupt->Connected = FALSE;
    Interrupt->ServiceCount = 0;
    Interrupt->DispatchCount = 0;
    Interrupt->DispatchAddress = NULL;
    InitializeListHead(&Interrupt->InterruptListEntry);
}

BOOLEAN
NTAPI
KeConnectInterrupt(PKINTERRUPT Interrupt)
{
    PKINTERRUPT Head;
    KIRQL OldIrql;
    BOOLEAN Connected = FALSE;

    if (!Interrupt || !Interrupt->ServiceRoutine || (Interrupt->Vector > KI_PPC_MAX_INTERRUPT_VECTOR) || (Interrupt->Number != 0) || (Interrupt->Irql != KI_PPC_EXTERNAL_IRQL) || (Interrupt->SynchronizeIrql < Interrupt->Irql) || (Interrupt->SynchronizeIrql > HIGH_LEVEL))
        return FALSE;
    if (Interrupt->Connected)
        return TRUE;

    KeSetSystemAffinityThread(1);
    OldIrql = KfRaiseIrql(KI_PPC_EXTERNAL_IRQL);
    KeAcquireSpinLockAtDpcLevel(&KiPpcInterruptTableLock);
    Head = KiPpcInterruptTable[Interrupt->Vector];
    if (!Head)
    {
        InitializeListHead(&Interrupt->InterruptListEntry);
        KiPpcInterruptTable[Interrupt->Vector] = Interrupt;
        KeMemoryBarrier();
        if (HalEnableSystemInterrupt(Interrupt->Vector, Interrupt->Irql, Interrupt->Mode))
            Connected = TRUE;
        else
            KiPpcInterruptTable[Interrupt->Vector] = NULL;
    }
    else if (Interrupt->ShareVector && Head->ShareVector && (Interrupt->Mode == Head->Mode) && (Interrupt->Irql == Head->Irql))
    {
        InsertTailList(&Head->InterruptListEntry, &Interrupt->InterruptListEntry);
        Connected = TRUE;
    }
    Interrupt->Connected = Connected;
    KeReleaseSpinLockFromDpcLevel(&KiPpcInterruptTableLock);
    KfLowerIrql(OldIrql);
    KeRevertToUserAffinityThread();
    return Connected;
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(PKINTERRUPT Interrupt)
{
    PKINTERRUPT Head;
    PLIST_ENTRY Next;
    KIRQL OldIrql;

    if (!Interrupt || (Interrupt->Vector > KI_PPC_MAX_INTERRUPT_VECTOR))
        return FALSE;

    KeSetSystemAffinityThread(1);
    OldIrql = KfRaiseIrql(KI_PPC_EXTERNAL_IRQL);
    KeAcquireSpinLockAtDpcLevel(&KiPpcInterruptTableLock);
    Head = KiPpcInterruptTable[Interrupt->Vector];
    if (!Head || !Interrupt->Connected)
    {
        KeReleaseSpinLockFromDpcLevel(&KiPpcInterruptTableLock);
        KfLowerIrql(OldIrql);
        KeRevertToUserAffinityThread();
        return FALSE;
    }

    if ((Head == Interrupt) && IsListEmpty(&Head->InterruptListEntry))
    {
        HalDisableSystemInterrupt(Interrupt->Vector, Interrupt->Irql);
        KiPpcInterruptTable[Interrupt->Vector] = NULL;
    }
    else if (Head == Interrupt)
    {
        Next = Head->InterruptListEntry.Flink;
        RemoveEntryList(&Head->InterruptListEntry);
        KiPpcInterruptTable[Interrupt->Vector] = CONTAINING_RECORD(Next, KINTERRUPT, InterruptListEntry);
    }
    else
    {
        RemoveEntryList(&Interrupt->InterruptListEntry);
    }
    Interrupt->Connected = FALSE;
    InitializeListHead(&Interrupt->InterruptListEntry);
    KeMemoryBarrier();
    KeReleaseSpinLockFromDpcLevel(&KiPpcInterruptTableLock);
    KfLowerIrql(OldIrql);
    KeRevertToUserAffinityThread();
    return TRUE;
}

VOID
NTAPI
KiPpcExternalInterrupt(_Inout_ PKTRAP_FRAME TrapFrame)
{
    PKPCR Pcr = KeGetPcr();
    PKINTERRUPT Head, Interrupt;
    PLIST_ENTRY Entry;
    KIRQL OldIrql, RaisedIrql;
    ULONG Vector;
    BOOLEAN Handled;

    UNREFERENCED_PARAMETER(TrapFrame);

    /* At the device level or above, mask the controller until KfLowerIrql
     * drops below that level. The UP idle loop may run at SYNCH_LEVEL (2). */
    if (Pcr->CurrentIrql >= KI_PPC_EXTERNAL_IRQL)
    {
        Pcr->PendingExternal = TRUE;
        HalpPpcDeferExternal();
        return;
    }

    Vector = HalpPpcClaimInterrupt();
    if (Vector == HALP_PPC_SPURIOUS_INTERRUPT)
        return;

    OldIrql = KfRaiseIrql(KI_PPC_EXTERNAL_IRQL);
    Head = (Vector <= KI_PPC_MAX_INTERRUPT_VECTOR) ? KiPpcInterruptTable[Vector] : NULL;
    if (Head)
    {
        Interrupt = Head;
        Entry = &Head->InterruptListEntry;

        /* The clock may preempt a device ISR; another external source is
         * deferred by the IRQL check above. */
        _enable();
        for (;;)
        {
            RaisedIrql = KI_PPC_EXTERNAL_IRQL;
            if (Interrupt->SynchronizeIrql > KI_PPC_EXTERNAL_IRQL)
                RaisedIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
            KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
            Handled = Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
            KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
            if (Interrupt->SynchronizeIrql > KI_PPC_EXTERNAL_IRQL)
                KfLowerIrql(RaisedIrql);
            if (Handled && (Interrupt->Mode == LevelSensitive))
                break;
            Entry = Entry->Flink;
            if (Entry == &Head->InterruptListEntry)
                break;
            Interrupt = CONTAINING_RECORD(Entry, KINTERRUPT, InterruptListEntry);
        }
        _disable();
    }
    HalpPpcCompleteInterrupt(Vector);
    KfLowerIrql(OldIrql);
    _disable();
}

VOID
NTAPI
KiPpcClockInterrupt(_Inout_ PKTRAP_FRAME TrapFrame)
{
    PKPCR Pcr = KeGetPcr();
    KIRQL OldIrql;

    /* At or above CLOCK_LEVEL the tick waits for KfLowerIrql. */
    if (Pcr->CurrentIrql >= CLOCK_LEVEL)
    {
        Pcr->PendingClock = TRUE;
        HalpPpcParkClock();
        return;
    }

    OldIrql = KfRaiseIrql(CLOCK_LEVEL);
    HalpPpcClockInterrupt(TrapFrame);
    KfLowerIrql(OldIrql);
    _disable();
}

/* Retire DPCs and perform the pending thread switch at DISPATCH_LEVEL.
 * Entered with interrupts enabled at DISPATCH_LEVEL. Deferred routines run
 * on the processor-owned DPC stack. */
VOID
NTAPI
KiDispatchInterrupt(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD NewThread, OldThread;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    _disable();
    if ((Prcb->DpcData[0].DpcQueueDepth) || (Prcb->TimerRequest) || (Prcb->DeferredReadyListHead.Next))
        KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
    _enable();

    if (Prcb->QuantumEnd)
    {
        Prcb->QuantumEnd = FALSE;
        KiQuantumEnd();
        return;
    }

    if (Prcb->NextThread == NULL)
        return;

    KiAcquirePrcbLock(Prcb);
    OldThread = Prcb->CurrentThread;
    if ((Prcb->NextThread == NULL) || KiConsumeSelfNextThread(Prcb, OldThread))
    {
        KiReleasePrcbLock(Prcb);
        return;
    }

    ASSERT(OldThread != Prcb->IdleThread);
    NewThread = Prcb->NextThread;
    KiSetThreadSwapBusy(OldThread);
    Prcb->NextThread = NULL;
    Prcb->CurrentThread = NewThread;
    NewThread->State = Running;
    OldThread->WaitReason = WrDispatchInt;

    /* Releases the PRCB lock. */
    KxQueueReadyThread(OldThread, Prcb);
    KiSwapContext(APC_LEVEL, OldThread);
}
