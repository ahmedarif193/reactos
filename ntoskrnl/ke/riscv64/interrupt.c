/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V supervisor interrupt dispatch and the DPC interrupt
 *
 * Entered from KiRiscvTrapHandler with sstatus.SIE clear, the frame on the
 * interrupted kernel stack and Pcr->CurrentIrql still at the interrupted
 * level (TrapFrame->PreviousIrql). Every source is dispatched at its own
 * IRQL through KfRaiseIrql/KfLowerIrql, so `sie` is always recomputed from
 * the IRQL model before sret. Nested traps are allowed at any time; another
 * interrupt can only nest while SIE is deliberately re-enabled around the
 * DISPATCH_LEVEL and APC_LEVEL work below, where the mask admits nothing
 * but strictly higher sources.
 *
 *   code 5 (STIP): clock. Raise to CLOCK_LEVEL, HAL rearms and updates time.
 *   code 1 (SSIP): APC/DPC request channel (Pcr->SoftwareInterrupts).
 *   code 9 (SEIP): claim a PLIC source and dispatch its KINTERRUPT chain.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define RISCV_SCAUSE_INTERRUPT   (1ULL << 63)
#define RISCV_INTERRUPT_SOFTWARE 1
#define RISCV_INTERRUPT_TIMER    5
#define RISCV_INTERRUPT_EXTERNAL 9
#define RISCV_PLIC_MAX_SOURCE    1023

/* This port owns a single hart. Raising to SYNCH_LEVEL masks SEIP while a
 * chain is changed, and the dispatch path never takes this table lock. */
static KSPIN_LOCK KiRiscvInterruptTableLock;
static PKINTERRUPT KiRiscvInterruptTable[RISCV_PLIC_MAX_SOURCE + 1];

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

    if (!Interrupt || !Interrupt->ServiceRoutine ||
        Interrupt->Vector == 0 || Interrupt->Vector > RISCV_PLIC_MAX_SOURCE ||
        Interrupt->Number != 0 || Interrupt->Irql != SYNCH_LEVEL ||
        Interrupt->SynchronizeIrql < Interrupt->Irql ||
        Interrupt->SynchronizeIrql > HIGH_LEVEL ||
        Interrupt->Mode != LevelSensitive)
        return FALSE;
    if (Interrupt->Connected)
        return TRUE;

    OldIrql = KeAcquireSpinLockRaiseToSynch(&KiRiscvInterruptTableLock);
    Head = KiRiscvInterruptTable[Interrupt->Vector];
    if (!Head)
    {
        InitializeListHead(&Interrupt->InterruptListEntry);
        KiRiscvInterruptTable[Interrupt->Vector] = Interrupt;
        KeMemoryBarrier();
        if (HalEnableSystemInterrupt(Interrupt->Vector,
                                     Interrupt->Irql, Interrupt->Mode))
            Connected = TRUE;
        else
            KiRiscvInterruptTable[Interrupt->Vector] = NULL;
    }
    else if (Interrupt->ShareVector && Head->ShareVector &&
             Interrupt->Mode == Head->Mode && Interrupt->Irql == Head->Irql)
    {
        InsertTailList(&Head->InterruptListEntry,
                       &Interrupt->InterruptListEntry);
        Connected = TRUE;
    }
    Interrupt->Connected = Connected;
    KeReleaseSpinLock(&KiRiscvInterruptTableLock, OldIrql);
    return Connected;
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(PKINTERRUPT Interrupt)
{
    PKINTERRUPT Head;
    PLIST_ENTRY Next;
    KIRQL OldIrql;

    if (!Interrupt || Interrupt->Vector == 0 ||
        Interrupt->Vector > RISCV_PLIC_MAX_SOURCE)
        return FALSE;
    OldIrql = KeAcquireSpinLockRaiseToSynch(&KiRiscvInterruptTableLock);
    Head = KiRiscvInterruptTable[Interrupt->Vector];
    if (!Head || !Interrupt->Connected)
    {
        KeReleaseSpinLock(&KiRiscvInterruptTableLock, OldIrql);
        return FALSE;
    }

    if (Head == Interrupt && IsListEmpty(&Head->InterruptListEntry))
    {
        HalDisableSystemInterrupt(Interrupt->Vector, Interrupt->Irql);
        KiRiscvInterruptTable[Interrupt->Vector] = NULL;
    }
    else if (Head == Interrupt)
    {
        Next = Head->InterruptListEntry.Flink;
        RemoveEntryList(&Head->InterruptListEntry);
        KiRiscvInterruptTable[Interrupt->Vector] =
            CONTAINING_RECORD(Next, KINTERRUPT, InterruptListEntry);
    }
    else
    {
        RemoveEntryList(&Interrupt->InterruptListEntry);
    }
    Interrupt->Connected = FALSE;
    InitializeListHead(&Interrupt->InterruptListEntry);
    KeMemoryBarrier();
    KeReleaseSpinLock(&KiRiscvInterruptTableLock, OldIrql);
    return TRUE;
}

static
VOID
KiRiscvExternalInterrupt(_Inout_ PKTRAP_FRAME TrapFrame)
{
    PKINTERRUPT Head, Interrupt;
    PLIST_ENTRY Entry;
    KIRQL OldIrql, RaisedIrql;
    ULONG Source;
    BOOLEAN Handled;

    if (KeGetCurrentIrql() >= SYNCH_LEVEL)
        KiRiscvTrapStop(TrapFrame);
    Source = HalpRiscvClaimPlicInterrupt();
    if (Source == 0)
        return; /* A claim of zero is a PLIC spurious notification. */

    OldIrql = KfRaiseIrql(SYNCH_LEVEL);
    Head = Source <= RISCV_PLIC_MAX_SOURCE ?
           KiRiscvInterruptTable[Source] : NULL;
    if (Head)
    {
        Interrupt = Head;
        Entry = &Head->InterruptListEntry;
        _enable(); /* The clock may preempt a device ISR. SEIP stays masked. */
        do
        {
            RaisedIrql = SYNCH_LEVEL;
            if (Interrupt->SynchronizeIrql > SYNCH_LEVEL)
                RaisedIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
            KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
            Handled = Interrupt->ServiceRoutine(Interrupt,
                                                 Interrupt->ServiceContext);
            KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
            if (Interrupt->SynchronizeIrql > SYNCH_LEVEL)
                KfLowerIrql(RaisedIrql);
            if (Handled && Interrupt->Mode == LevelSensitive)
                break;
            Entry = Entry->Flink;
            if (Entry == &Head->InterruptListEntry)
                break;
            Interrupt = CONTAINING_RECORD(Entry, KINTERRUPT,
                                          InterruptListEntry);
        } while (TRUE);
        _disable();
    }
    HalpRiscvCompletePlicInterrupt(Source);
    KfLowerIrql(OldIrql);
}

/* Retire DPCs and perform the pending thread switch at DISPATCH_LEVEL.
 * Same contract as the amd64/i386 KiDispatchInterrupt: entered with
 * interrupts enabled at DISPATCH_LEVEL, returns at DISPATCH_LEVEL or above
 * (the thread that switches back may have been at SYNCH_LEVEL; the caller
 * lowers). Deferred routines execute on the processor-owned DPC stack; the
 * thread stack is restored before scheduling. */
VOID
NTAPI
KiDispatchInterrupt(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD NewThread, OldThread;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    /* KiRetireDpcList expects interrupts disabled and re-enables them
     * around each deferred routine. */
    _disable();
    if ((Prcb->DpcData[0].DpcQueueDepth) ||
        (Prcb->TimerRequest) ||
        (Prcb->DeferredReadyListHead.Next))
    {
        KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
    }
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

    /* The idle thread never runs below DISPATCH_LEVEL, so it can never be
     * the interrupted thread here; its own loop performs its switches. */
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

/* SSIP is the shared request line for APC_LEVEL and DISPATCH_LEVEL work.
 * KiRiscvUpdateInterruptMask raises SSIP whenever a request bit is set and
 * unmasks SSIE only while an eligible request exists, so this runs only
 * when there is work below the interrupted IRQL. Requests made while
 * dispatching (a DPC that readies a thread, a switched-in thread with a
 * pending kernel APC) are picked up by re-evaluating the request word
 * after each lower, before sret. */
static
VOID
KiRiscvDeliverSoftwareInterrupts(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKPCR Pcr = KeGetPcr();
    KIRQL Irql;

    /* Acknowledge first: the mask update below re-raises SSIP for any
     * request that stays ineligible at the interrupted IRQL. */
    __asm__ __volatile__("csrci sip, 2" ::: "memory");
    for (;;)
    {
        Irql = Pcr->CurrentIrql;
        if ((Irql < DISPATCH_LEVEL) &&
            (Pcr->SoftwareInterrupts & (1 << DISPATCH_LEVEL)))
        {
            KiRiscvClearSoftwareInterrupt(DISPATCH_LEVEL);
            KfRaiseIrql(DISPATCH_LEVEL);
            _enable();
            KiDispatchInterrupt();
            _disable();
            KfLowerIrql(Irql);
            continue;
        }

        if ((Irql < APC_LEVEL) &&
            (Pcr->SoftwareInterrupts & (1 << APC_LEVEL)))
        {
            KiRiscvClearSoftwareInterrupt(APC_LEVEL);
            KfRaiseIrql(APC_LEVEL);
            _enable();
            KiDeliverApc(KiUserTrap(TrapFrame) ? UserMode : KernelMode, NULL, TrapFrame);
            _disable();
            KfLowerIrql(Irql);
            continue;
        }
        break;
    }
}

static
VOID
KiRiscvClockInterrupt(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    KIRQL OldIrql;

    /* The mask clears STIE at CLOCK_LEVEL and above: delivery there means
     * `sie` and Pcr->CurrentIrql disagree. */
    if (KeGetCurrentIrql() >= CLOCK_LEVEL)
        KiRiscvTrapStop(TrapFrame);

    /* The HAL rearms the deadline (which clears STIP) and updates time. */
    OldIrql = KfRaiseIrql(CLOCK_LEVEL);
    HalpRiscvClockInterrupt(TrapFrame);
    KfLowerIrql(OldIrql);
}

VOID
NTAPI
KiRiscvInterruptDispatch(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    ULONG64 Code = TrapFrame->Scause & ~RISCV_SCAUSE_INTERRUPT;

    ASSERT(TrapFrame->Scause & RISCV_SCAUSE_INTERRUPT);
    ASSERT(TrapFrame->PreviousIrql == KeGetCurrentIrql());

    switch (Code)
    {
        case RISCV_INTERRUPT_TIMER:
            KiRiscvClockInterrupt(TrapFrame);
            return;

        case RISCV_INTERRUPT_SOFTWARE:
            KiRiscvDeliverSoftwareInterrupts(TrapFrame);
            return;

        case RISCV_INTERRUPT_EXTERNAL:
            KiRiscvExternalInterrupt(TrapFrame);
            return;

        default:
            DbgPrint("\n*** Unknown supervisor interrupt cause %I64u\n", Code);
            break;
    }
    KiRiscvTrapStop(TrapFrame);
}
