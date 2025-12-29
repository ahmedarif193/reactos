/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/interrupt.c
 * PURPOSE:         ARM64 interrupt bring-up stubs and initialization
 */

/*
 * ARM64 Interrupt management and dispatch (HAL-backed)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* HAL extension: not yet declared in public headers for ARM64 */
extern ULONG FASTCALL HalGetInterruptSource(VOID);

/* Simple vector→KINTERRUPT chain table (INTIDs up to 1023 for GICv2/v3) */
#define ARM64_MAX_INTID 1024
static PKINTERRUPT KiArm64IntTable[ARM64_MAX_INTID] = {0};
static KSPIN_LOCK KiArm64IntTableLock;
/* Simple timer wiring for bring-up */
static KINTERRUPT KiArm64TimerInterrupt;
static KSPIN_LOCK KiArm64TimerLock;
static ULONGLONG KiArm64TimerPeriodTicks;

static __inline ULONGLONG KiArm64ReadCntFrq(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v)); return v;
}
static __inline VOID KiArm64WriteCntpTval(ULONGLONG v)
{
    __asm__ __volatile__("msr cntp_tval_el0, %0" :: "r"(v));
}
static __inline VOID KiArm64WriteCntpCtl(ULONG v)
{
    __asm__ __volatile__("msr cntp_ctl_el0, %0" :: "r"((ULONGLONG)v));
}

static BOOLEAN NTAPI
KiArm64TimerIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID ServiceContext)
{
    ULONGLONG period = (ServiceContext) ? *(volatile ULONGLONG*)ServiceContext : KiArm64TimerPeriodTicks;
    UNREFERENCED_PARAMETER(Interrupt);

    /* Reload next tick */
    KiArm64WriteCntpTval(period);

    /* Light heartbeat (very sparse to avoid spam) */
    static volatile LONG tick;
    LONG t = InterlockedIncrement(&tick);
    if ((t & 0x3FF) == 1)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[arm64] Timer tick %ld\n", t);
    }
    return TRUE;
}

static VOID
KiArm64StartTimer(VOID)
{
    ULONGLONG frq = KiArm64ReadCntFrq();
    if (frq == 0) frq = 100000000ULL; /* safe default */
    /* Target ~100 Hz */
    KiArm64TimerPeriodTicks = frq / 100ULL;
    KiArm64WriteCntpTval(KiArm64TimerPeriodTicks);
    /* Enable and unmask: ENABLE=1, IMASK=0 */
    KiArm64WriteCntpCtl(1);
}

CODE_SEG("INIT")
VOID
NTAPI
KeInitInterrupts(VOID)
{
    KeInitializeSpinLock(&KiArm64IntTableLock);
    {
        CHAR Buf[96];
        CHAR Buf2[160];
        if (NT_SUCCESS(RtlStringCbPrintfA(Buf, sizeof(Buf),
                                          "[arm64] KeInitInterrupts: entry Pcr=%p Irql=%lu",
                                          KeArm64CurrentPcr,
                                          (ULONG)KeArm64CurrentIrql)))
        {
            KiArm64BootStageLog(Buf);
        }

        if (NT_SUCCESS(RtlStringCbPrintfA(Buf2, sizeof(Buf2),
                                          "[arm64] KeInitInterrupts: KINTERRUPT sizeof=%lu Offsets: ListEntry=%lu ServiceRoutine=%lu ServiceContext=%lu ActualLock=%lu Vector=%lu Irql=%lu Mode=%lu Connected=%lu",
                                          (ULONG)sizeof(KINTERRUPT),
                                          (ULONG)FIELD_OFFSET(KINTERRUPT, InterruptListEntry),
                                          (ULONG)FIELD_OFFSET(KINTERRUPT, ServiceRoutine),
                                          (ULONG)FIELD_OFFSET(KINTERRUPT, ServiceContext),
                                          (ULONG)FIELD_OFFSET(KINTERRUPT, ActualLock),
                                          (ULONG)FIELD_OFFSET(KINTERRUPT, Vector),
                                          (ULONG)FIELD_OFFSET(KINTERRUPT, Irql),
                                          (ULONG)FIELD_OFFSET(KINTERRUPT, Mode),
                                          (ULONG)FIELD_OFFSET(KINTERRUPT, Connected))))
        {
            KiArm64BootStageLog(Buf2);
        }
    }
    KiArm64BootStageLog("[arm64] KeInitInterrupts: HAL/GIC init");

    /* Wire the generic timer (PPI INTID 30) for a periodic heartbeat */
    {
        KiArm64BootStageLog("[arm64] KeInitInterrupts: before KeInitializeInterrupt(timer)");
        KeInitializeSpinLock(&KiArm64TimerLock);
        KeInitializeInterrupt(&KiArm64TimerInterrupt,
                              KiArm64TimerIsr,
                              &KiArm64TimerPeriodTicks,
                              &KiArm64TimerLock,
                              30, /* PPI: non-secure physical timer */
                              DISPATCH_LEVEL,
                              DISPATCH_LEVEL,
                              LevelSensitive,
                              FALSE,
                              0,
                              FALSE);
        KiArm64BootStageLog("[arm64] KeInitInterrupts: before KeConnectInterrupt(timer)");
        if (KeConnectInterrupt(&KiArm64TimerInterrupt))
        {
            KiArm64StartTimer();
            /* Ensure IRQs are unmasked at the CPU (clear I bit) */
            __asm__ __volatile__("msr daifclr, #2" ::: "memory");
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_TRACE_LEVEL,
                       "[arm64] Timer configured: CNTFRQ=%llu period=%llu ticks (INTID=30)\n",
                       KiArm64ReadCntFrq(),
                       KiArm64TimerPeriodTicks);
            KiArm64BootStageLog("[arm64] KeInitInterrupts: timer connected & enabled");
        }
        else
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[arm64] Timer connect failed (INTID=30)\n");
            KiArm64BootStageLog("[arm64] KeInitInterrupts: timer connect failed");
        }
    }
}

static
VOID
KiArm64DispatchChain(_In_ ULONG IntId,
                     _In_ KIRQL OldIrql)
{
    PKINTERRUPT Head, Interrupt;
    PLIST_ENTRY ListHead, NextEntry;
    KIRQL RaiseIrql;
    BOOLEAN Handled = FALSE;

    if (IntId >= ARM64_MAX_INTID) goto Done;

    /* Snapshot head without holding the global lock for long */
    Head = (PKINTERRUPT)InterlockedCompareExchangePointer((PVOID *)&KiArm64IntTable[IntId],
                                                          (PVOID)KiArm64IntTable[IntId],
                                                          (PVOID)KiArm64IntTable[IntId]);
    if (!Head) goto Done;

    ListHead = &Head->InterruptListEntry;

    if (IsListEmpty(ListHead))
    {
        /* Single ISR */
        KxAcquireSpinLock(Head->ActualLock);
        (VOID)Head->ServiceRoutine(Head, Head->ServiceContext);
        KxReleaseSpinLock(Head->ActualLock);
        goto Done;
    }

    /* Chained ISR list (parity with i386 path) */
    NextEntry = ListHead; /* head is an entry */
    Interrupt = Head;

    for (;;)
    {
        /* Elevate for synchronization if needed */
        RaiseIrql = 0;
        if (Interrupt->SynchronizeIrql > Interrupt->Irql)
        {
            RaiseIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
        }

        KxAcquireSpinLock(Interrupt->ActualLock);
        Handled = Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
        KxReleaseSpinLock(Interrupt->ActualLock);

        if (Interrupt->SynchronizeIrql > Interrupt->Irql)
        {
            ASSERT(RaiseIrql == Interrupt->Irql);
            KfLowerIrql(RaiseIrql);
        }

        if ((Handled) && (Interrupt->Mode == LevelSensitive)) break;

        NextEntry = NextEntry->Flink;
        if (NextEntry == ListHead)
        {
            if (Interrupt->Mode == LevelSensitive) break;
            if (!Handled) break;
        }

        Interrupt = CONTAINING_RECORD(NextEntry, KINTERRUPT, InterruptListEntry);
    }

Done:
    /* End the interrupt through HAL */
    HalEndSystemInterrupt(OldIrql, NULL);
}

VOID
KiArm64InterruptDispatchEntry(_In_ ULONG VectorId)
{
    ULONG IntId;
    KIRQL OldIrql;
    BOOLEAN Begun;

    /* Ask HAL for current INTID */
    IntId = HalGetInterruptSource();

    /* Spurious or unsupported */
    if ((IntId == 0) || (IntId >= ARM64_MAX_INTID))
        return;

    Begun = HalBeginSystemInterrupt(DISPATCH_LEVEL, IntId, &OldIrql);
    if (!Begun) return;

    /* Dispatch to kernel’s ISR chain */
    KiArm64DispatchChain(IntId, OldIrql);
    UNREFERENCED_PARAMETER(VectorId);
}

/*
 * KINTERRUPT support (connect/disconnect/synchronize)
 */

VOID
NTAPI
KeInitializeInterrupt(IN PKINTERRUPT Interrupt,
                      IN PKSERVICE_ROUTINE ServiceRoutine,
                      IN PVOID ServiceContext,
                      IN PKSPIN_LOCK SpinLock,
                      IN ULONG Vector,
                      IN KIRQL Irql,
                      IN KIRQL SynchronizeIrql,
                      IN KINTERRUPT_MODE InterruptMode,
                      IN BOOLEAN ShareVector,
                      IN CHAR ProcessorNumber,
                      IN BOOLEAN FloatingSave)
{
    Interrupt->Type = InterruptObject;
    Interrupt->Size = sizeof(KINTERRUPT);
    if (!SpinLock) SpinLock = &Interrupt->SpinLock;
    KeInitializeSpinLock(&Interrupt->SpinLock);

    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->ActualLock = SpinLock;
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
KeConnectInterrupt(IN PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;
    PKINTERRUPT Head;
    ULONG Vector = Interrupt->Vector;
    CHAR Buf[128];
    CHAR Buf2[128];

    if (Vector >= ARM64_MAX_INTID) return FALSE;
    if (Interrupt->Connected) return TRUE;

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KeConnectInterrupt: entry Int=%p Vec=%lu Irql=%lu Mode=%lu Share=%lu",
                                      Interrupt,
                                      (ULONG)Vector,
                                      (ULONG)Interrupt->Irql,
                                      (ULONG)Interrupt->Mode,
                                      (ULONG)Interrupt->ShareVector)))
    {
        KiArm64BootStageLog(Buf);
    }

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf2,
                                      sizeof(Buf2),
                                      "[arm64] KeConnectInterrupt: before KeAcquireSpinLock Vec=%lu",
                                      (ULONG)Vector)))
    {
        KiArm64BootStageLog(Buf2);
    }

    KeAcquireSpinLock(&KiArm64IntTableLock, &OldIrql);

    Head = KiArm64IntTable[Vector];
    if (!Head)
    {
        InitializeListHead(&Interrupt->InterruptListEntry);
        KiArm64IntTable[Vector] = Interrupt;
        if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                          sizeof(Buf),
                                          "[arm64] KeConnectInterrupt: enabling via HAL vec=%lu irql=%lu mode=%lu",
                                          (ULONG)Vector,
                                          (ULONG)Interrupt->Irql,
                                          (ULONG)Interrupt->Mode)))
        {
            KiArm64BootStageLog(Buf);
        }
        HalEnableSystemInterrupt(Vector, Interrupt->Irql, Interrupt->Mode);
        KiArm64BootStageLog("[arm64] KeConnectInterrupt: HalEnableSystemInterrupt returned");
        Interrupt->Connected = TRUE;
    }
    else
    {
        if ((Interrupt->ShareVector == 0) || (Head->ShareVector == 0) ||
            (Interrupt->Mode != Head->Mode))
        {
            Interrupt->Connected = FALSE;
        }
        else
        {
            InsertTailList(&Head->InterruptListEntry, &Interrupt->InterruptListEntry);
            Interrupt->Connected = TRUE;
        }
    }

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf2,
                                      sizeof(Buf2),
                                      "[arm64] KeConnectInterrupt: before KeReleaseSpinLock Vec=%lu OldIrql=%lu",
                                      (ULONG)Vector,
                                      (ULONG)OldIrql)))
    {
        KiArm64BootStageLog(Buf2);
    }

    KeReleaseSpinLock(&KiArm64IntTableLock, OldIrql);

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf2,
                                      sizeof(Buf2),
                                      "[arm64] KeConnectInterrupt: after KeReleaseSpinLock Vec=%lu",
                                      (ULONG)Vector)))
    {
        KiArm64BootStageLog(Buf2);
    }
    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KeConnectInterrupt: exit Vec=%lu Connected=%lu",
                                      (ULONG)Vector,
                                      (ULONG)Interrupt->Connected)))
    {
        KiArm64BootStageLog(Buf);
    }
    return Interrupt->Connected;
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(IN PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;
    PKINTERRUPT Head;
    ULONG Vector = Interrupt->Vector;

    KeAcquireSpinLock(&KiArm64IntTableLock, &OldIrql);
    Head = KiArm64IntTable[Vector];
    if (!Head || !Interrupt->Connected)
        goto Done;

    if (IsListEmpty(&Head->InterruptListEntry))
    {
        /* Single interrupt case */
        ASSERT(Head == Interrupt);
        HalDisableSystemInterrupt(Vector, Interrupt->Irql);
        KiArm64IntTable[Vector] = NULL;
        Interrupt->Connected = FALSE;
    }
    else if (Head == Interrupt)
    {
        /* Move head to next */
        PLIST_ENTRY NewHeadEntry = Head->InterruptListEntry.Flink;
        RemoveTailList(NewHeadEntry);
        KiArm64IntTable[Vector] = CONTAINING_RECORD(NewHeadEntry, KINTERRUPT, InterruptListEntry);
        Interrupt->Connected = FALSE;
    }
    else
    {
        /* Remove from chain */
        RemoveEntryList(&Interrupt->InterruptListEntry);
        Interrupt->Connected = FALSE;
    }

Done:
    KeReleaseSpinLock(&KiArm64IntTableLock, OldIrql);
    return TRUE;
}

BOOLEAN
NTAPI
KeSynchronizeExecution(IN OUT PKINTERRUPT Interrupt,
                       IN PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
                       IN PVOID SynchronizeContext OPTIONAL)
{
    BOOLEAN Success;
    KIRQL OldIrql;
    OldIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
    KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
    Success = SynchronizeRoutine(SynchronizeContext);
    KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
    KeLowerIrql(OldIrql);
    return Success;
}

/* Legacy alias retained for parity with other arches */
VOID KiUnexpectedInterrupt(VOID)
{
    KeBugCheck(TRAP_CAUSE_UNKNOWN);
}
