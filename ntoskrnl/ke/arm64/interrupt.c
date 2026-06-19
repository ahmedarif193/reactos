/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/interrupt.c
 * PURPOSE:         ARM64 interrupt bring-up stubs and initialization
 */

/*
 * ARM64 Interrupt management and dispatch (HAL-backed)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <arm64pl011.h>
#include <reactos/smpdbg.h>

/* HAL extension: not yet declared in public headers for ARM64 */
extern ULONG FASTCALL HalGetInterruptSource(VOID);
extern VOID NTAPI HalPerformEndOfInterrupt(VOID);
extern BOOLEAN NTAPI HalArm64ProfileSample(ULONG Increment);

/* Simple vector→KINTERRUPT chain table (SPIs + optional LPIs) */
#define ARM64_LPI_BASE 8192
#define ARM64_LPI_COUNT 1024
#define ARM64_MAX_INTID (ARM64_LPI_BASE + ARM64_LPI_COUNT)
#define ARM64_SGI_IPI 0
#define ARM64_SGI_APC 1
#define ARM64_SGI_DPC 2
#define ARM64_SGI_FREEZE 3
static PKINTERRUPT KiArm64IntTable[ARM64_MAX_INTID] = {0};
static KSPIN_LOCK KiArm64IntTableLock;
/* Simple timer wiring for bring-up */
static KINTERRUPT KiArm64TimerInterrupt;
static KSPIN_LOCK KiArm64TimerLock;
static ULONGLONG KiArm64TimerPeriodTicks;
static ULONGLONG KiArm64TimerFrequency;
static KINTERRUPT KiArm64IpiInterrupt;
static KSPIN_LOCK KiArm64IpiLock;
static BOOLEAN KiArm64UseVirtualTimer = TRUE; /* Use virtual timer - physical timer doesn't work under HVF */
static KTRAP_FRAME KiArm64InterruptTrapFrame[MAXIMUM_PROCESSORS];
static PKTRAP_FRAME KiArm64CurrentInterruptTrapFrame[MAXIMUM_PROCESSORS];
static volatile LONG KiArm64DispatchEpoch[MAXIMUM_PROCESSORS];

static inline
PKINTERRUPT
KiArm64LoadInterruptHeadNoFence(
    _In_ ULONG IntId)
{
    return (PKINTERRUPT)ReadPointerNoFence((PVOID const volatile *)&KiArm64IntTable[IntId]);
}

ULONG KiTimerIsrCallCount = 0;
ULONG KiInitInterruptsCallCount = 0;
ULONG KiTimerStartedFlag = 0;
ULONG KiTimerCtlReadback = 0;

PKTRAP_FRAME
KiArm64ActiveInterruptTrapFrame(VOID)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();

    if (Cpu >= MAXIMUM_PROCESSORS)
    {
        Cpu = 0;
    }

    return KiArm64CurrentInterruptTrapFrame[Cpu];
}

static
PKTRAP_FRAME
KiArm64GetCurrentInterruptTrapFrame(VOID)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();

    if (Cpu >= MAXIMUM_PROCESSORS)
    {
        Cpu = 0;
    }

    return KiArm64CurrentInterruptTrapFrame[Cpu] ?
           KiArm64CurrentInterruptTrapFrame[Cpu] :
           &KiArm64InterruptTrapFrame[Cpu];
}

static inline void KiRawDebugPuts(const char *str) {
    UNREFERENCED_PARAMETER(str);
}

static __inline ULONGLONG KiArm64ReadCntFrq(void);

static
ULONG
KiArm64CurrentTimerIncrement(VOID)
{
    ULONG Increment = KeTimeIncrement;

    if (Increment == 0)
        Increment = KeMaximumIncrement;
    if (Increment == 0)
        Increment = 100000;
    if (KeMinimumIncrement && Increment < KeMinimumIncrement)
        Increment = KeMinimumIncrement;
    if (KeMaximumIncrement && Increment > KeMaximumIncrement)
        Increment = KeMaximumIncrement;

    return Increment;
}

static
ULONGLONG
KiArm64ComputeTimerPeriodTicks(
    _In_ ULONG Increment)
{
    ULONGLONG Frequency = KiArm64TimerFrequency;
    ULONGLONG Period;

    if (Frequency == 0)
        Frequency = KiArm64ReadCntFrq();
    if (Frequency == 0 || Frequency > 10000000000ULL)
        Frequency = 100000000ULL;

    Period = (Frequency * Increment) / 10000000ULL;
    return Period ? Period : 1;
}

BOOLEAN
NTAPI
KiIpiServiceRoutine(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

VOID
NTAPI
KiDispatchInterrupt(VOID);

VOID
NTAPI
KiDeliverApc(
    _In_ KPROCESSOR_MODE DeliveryMode,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame);

/*
 * ARM64 Generic Timer register accessors.
 *
 * CNTP_* registers control the EL1 physical timer (PPI 30 non-secure, 29 secure).
 * CNTV_* registers control the EL1 virtual timer (PPI 27).
 *
 * Timer control register (CTL) bits:
 *   Bit 0 (ENABLE):  1 = timer enabled
 *   Bit 1 (IMASK):   1 = interrupt masked (disabled)
 *   Bit 2 (ISTATUS): read-only, 1 = timer condition met
 *
 * TVAL is a signed countdown value; when it reaches zero, ISTATUS is set.
 * Writing TVAL also updates CVAL (compare value) = CNTPCT + TVAL.
 *
 * ISB is required after writing CTL to ensure the enable/disable takes effect
 * before subsequent code executes. For TVAL writes during ISR reload, ISB
 * is optional but recommended for deterministic timing.
 */

static __inline ULONGLONG KiArm64ReadCntFrq(void)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static __inline ULONG KiArm64ReadCntpCtl(void)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(v));
    return (ULONG)v;
}

static __inline ULONG KiArm64ReadCntvCtl(void)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, cntv_ctl_el0" : "=r"(v));
    return (ULONG)v;
}

static __inline VOID KiArm64WriteCntpTval(ULONGLONG v)
{
    /*
     * ISB is required after writing TVAL to ensure the timer reload takes
     * effect immediately. The ARM Architecture Reference Manual states that
     * changes to timer registers may not be visible until an ISB is executed.
     * Without ISB, the timer interrupt may not fire at the expected time.
     */
    __asm__ __volatile__("msr cntp_tval_el0, %0; isb" :: "r"(v) : "memory");
}

static __inline VOID KiArm64WriteCntpCtl(ULONG v)
{
    /*
     * ISB is required after writing CTL to ensure the timer enable/disable
     * takes effect immediately. Without ISB, subsequent instructions may
     * execute before the timer state change is visible.
     */
    __asm__ __volatile__("msr cntp_ctl_el0, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

static __inline VOID KiArm64WriteCntvTval(ULONGLONG v)
{
    /*
     * ISB is required after writing TVAL to ensure the timer reload takes
     * effect immediately. The ARM Architecture Reference Manual states that
     * changes to timer registers may not be visible until an ISB is executed.
     * Without ISB, the timer interrupt may not fire at the expected time.
     */
    __asm__ __volatile__("msr cntv_tval_el0, %0; isb" :: "r"(v) : "memory");
}

static __inline VOID KiArm64WriteCntvCtl(ULONG v)
{
    /*
     * ISB is required after writing CTL to ensure the timer enable/disable
     * takes effect immediately. Without ISB, subsequent instructions may
     * execute before the timer state change is visible.
     */
    __asm__ __volatile__("msr cntv_ctl_el0, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

/*
 * KeUpdateSystemTime - Update system time and call scheduler tick.
 * Declared in ntoskrnl/include/internal/ke.h
 */
VOID
FASTCALL
KeUpdateSystemTime(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG Increment,
    IN KIRQL Irql);

/*
 * ARM64 Timer ISR - Called at CLOCK_LEVEL/DISPATCH_LEVEL on each tick.
 *
 * This is the heart of the scheduler tick. We must:
 * 1. Reload the timer for the next tick
 * 2. Call KeUpdateSystemTime to update time and schedule
 *
 * The timer runs at ~100 Hz (10ms per tick = 100,000 100ns units).
 *
 * NOTE: Do NOT use DPRINT1 or any debug print in this ISR!
 * The print subsystem uses spinlocks and we can deadlock if the
 * timer fires while HalInitSystem is holding the debug print lock.
 */
static BOOLEAN NTAPI
KiArm64TimerIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID ServiceContext)
{
    ULONGLONG period;
    ULONG Increment;
    PKTRAP_FRAME TrapFrame;
    ULONG Cpu;
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(ServiceContext);

    KiTimerIsrCallCount++;

    Increment = KiArm64CurrentTimerIncrement();
    if (KeGetCurrentProcessorNumber() != 0 && KeMaximumIncrement > Increment)
    {
        Increment = KeMaximumIncrement;
    }
    period = KiArm64ComputeTimerPeriodTicks(Increment);
    KiArm64TimerPeriodTicks = period;

    /* Reload next tick first to minimize jitter. */
    if (KiArm64UseVirtualTimer)
        KiArm64WriteCntvTval(period);
    else
        KiArm64WriteCntpTval(period);

    /*
     * Call KeUpdateSystemTime to:
     * - Update SharedUserData->InterruptTime
     * - Update SharedUserData->SystemTime
     * - Update KeTickCount
     * - Call KeUpdateRunTime for thread/process accounting
     * - Check for timer expirations
     * - Handle debugger break-in
     *
     * The common clock path dereferences the trap frame for previous-mode
     * accounting. Keep this as a real frame, not a sentinel pointer.
     */
    TrapFrame = KiArm64GetCurrentInterruptTrapFrame();
    Cpu = KeGetCurrentProcessorNumber();

    /* SMP boot diagnostics: silent per-CPU tick counter only. The periodic
     * serial dump is disabled for this probe - at 115200 baud it perturbs the
     * timing race; the per-CPU counters are read via GDB at the stall instead. */
    SmpDbgTimerTick(Cpu);
    SmpDbgHeartbeat(Cpu);

    /*
     * KeUpdateRunTime accounts kernel/user/idle/DPC/interrupt time from the
     * interrupted context. Match the HAL clock contract used by the other
     * architectures: pass the IRQL that was active before the timer interrupt,
     * not CLOCK_LEVEL itself.
     *
     * On SMP, only the boot CPU advances global time. Other CPUs run their
     * local architected timer for per-CPU runtime and quantum accounting,
     * matching the x86 APIC clock + clock-IPI split.
     */
    if (Cpu == 0)
    {
        KeUpdateSystemTime(TrapFrame, Increment, TrapFrame->PreviousIrql);
    }
    else
    {
        static LONG KiArm64ApTickOffset[MAXIMUM_PROCESSORS];

        if (Cpu < MAXIMUM_PROCESSORS)
        {
            KiArm64ApTickOffset[Cpu] += (LONG)Increment;
            if (KiArm64ApTickOffset[Cpu] >= (LONG)KeMaximumIncrement)
            {
                KiArm64ApTickOffset[Cpu] -= (LONG)KeMaximumIncrement;
                KeUpdateRunTime(TrapFrame, TrapFrame->PreviousIrql);
            }
        }
    }

    /*
     * Clock-driven profiling: ARM64 has no separate profile timer, so the
     * architected-timer clock doubles as the profile source. HalArm64ProfileSample
     * accumulates the per-tick increment and returns TRUE only once the requested
     * profile interval has elapsed, so coarser intervals are honored instead of
     * sampling on every single tick.
     */
    if (HalArm64ProfileSample(Increment))
    {
        KeProfileInterrupt(TrapFrame);
    }

    return TRUE;
}

static BOOLEAN NTAPI
KiArm64IpiIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID ServiceContext)
{
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(ServiceContext);

    return KiIpiServiceRoutine(NULL, NULL);
}

/*
 * KiArm64StartTimer - Initialize and start the ARM64 generic timer.
 *
 * This function programs the architected timer to fire at 100 Hz (10ms period).
 * The timer is used for the system clock tick, which drives:
 *   - KeUpdateSystemTime (system time and interrupt time)
 *   - Thread quantum expiration (scheduler preemption)
 *   - Timer DPC expiration
 *   - Profiling (if enabled)
 *
 * We use the virtual timer (CNTV) by default as it's always accessible from EL1,
 * even under a hypervisor. The physical timer (CNTP) may require secure world
 * or hypervisor permissions on some platforms.
 *
 * Timer configuration:
 *   CNTFRQ_EL0: Counter frequency in Hz (typically 1-100 MHz)
 *   CNTV_TVAL_EL0: Countdown value (fires when reaches 0)
 *   CNTV_CTL_EL0: Control (bit 0=ENABLE, bit 1=IMASK)
 *
 * For 100 Hz timer with 24.576 MHz counter: TVAL = 24576000 / 100 = 245760 ticks
 * For 100 Hz timer with 100 MHz counter:    TVAL = 100000000 / 100 = 1000000 ticks
 */
static
VOID
KiArm64StartLocalTimer(VOID)
{
    ULONGLONG frq;
    ULONG Increment;
    ULONG ctl;

    /* Read counter frequency from CNTFRQ_EL0 */
    frq = KiArm64ReadCntFrq();

    /*
     * Validate frequency - if zero or unreasonable, use a safe default.
     * QEMU virt uses 62.5 MHz (62500000), real hardware varies widely:
     *   - Cortex-A53/A72: typically 19.2 MHz or 24.576 MHz
     *   - Apple M1: 24 MHz
     *   - QEMU: 62.5 MHz or 1 GHz depending on configuration
     */
    if (frq == 0 || frq > 10000000000ULL)
    {
        DPRINT1("[arm64] CNTFRQ_EL0 invalid (0x%llx), using 100 MHz default\n", frq);
        frq = 100000000ULL;
    }

    KiArm64TimerFrequency = frq;
    Increment = KiArm64CurrentTimerIncrement();
    KiArm64TimerPeriodTicks = KiArm64ComputeTimerPeriodTicks(Increment);

    if (KiArm64UseVirtualTimer)
    {
        /*
         * Configure virtual timer (CNTV):
         * 1. Set countdown value in CNTV_TVAL_EL0
         * 2. Enable timer with ENABLE=1, IMASK=0 in CNTV_CTL_EL0
         */
        KiArm64WriteCntvTval(KiArm64TimerPeriodTicks);
        KiArm64WriteCntvCtl(1); /* ENABLE=1, IMASK=0 */

        ctl = KiArm64ReadCntvCtl();
    }
    else
    {
        /*
         * Configure physical timer (CNTP):
         * 1. Set countdown value in CNTP_TVAL_EL0
         * 2. Enable timer with ENABLE=1, IMASK=0 in CNTP_CTL_EL0
         */
        KiArm64WriteCntpTval(KiArm64TimerPeriodTicks);
        KiArm64WriteCntpCtl(1); /* ENABLE=1, IMASK=0 */

        ctl = KiArm64ReadCntpCtl();
    }
    KiTimerCtlReadback = ctl;
    KiTimerStartedFlag = 1;
}

VOID
NTAPI
KeStartArm64ProcessorTimer(VOID)
{
    ULONG TimerIntId = KiArm64UseVirtualTimer ? 27 : 30;

    /*
     * The ARM generic timer is a per-CPU PPI. The interrupt object is
     * connected once on the BSP, but every processor must enable its own PPI
     * bank and program its own local timer registers.
     */
    HalEnableSystemInterrupt(TimerIntId, CLOCK_LEVEL, LevelSensitive);
    KiArm64StartLocalTimer();
}

CODE_SEG("INIT")
VOID
NTAPI
KeInitInterrupts(VOID)
{
    KiRawDebugPuts("[KeInitInterrupts] ENTRY\n");
    KiInitInterruptsCallCount = 1;

    KiRawDebugPuts("[KeInitInterrupts] KeInitializeSpinLock IntTableLock\n");
    KeInitializeSpinLock(&KiArm64IntTableLock);
    KiRawDebugPuts("[KeInitInterrupts] IntTableLock done\n");

    /* Wire SGIs for IPI/APC/DPC */
    KiRawDebugPuts("[KeInitInterrupts] IPI spinlock\n");
    KeInitializeSpinLock(&KiArm64IpiLock);
    KiRawDebugPuts("[KeInitInterrupts] IPI KeInitializeInterrupt\n");
    KeInitializeInterrupt(&KiArm64IpiInterrupt,
                          KiArm64IpiIsr,
                          NULL,
                          &KiArm64IpiLock,
                          ARM64_SGI_IPI,
                          IPI_LEVEL,
                          IPI_LEVEL,
                          Latched,
                          FALSE,
                          0,
                          FALSE);
    KiRawDebugPuts("[KeInitInterrupts] IPI KeConnectInterrupt\n");
    (VOID)KeConnectInterrupt(&KiArm64IpiInterrupt);
    KiRawDebugPuts("[KeInitInterrupts] IPI connect done\n");

    KiRawDebugPuts("[KeInitInterrupts] Timer setup\n");
    /*
     * Wire the generic timer (PPI) for a periodic clock tick.
     *
     * ARM64 Generic Timer PPIs:
     *   INTID 29 = Secure EL1 Physical Timer (CNTP_S)
     *   INTID 30 = Non-secure EL1 Physical Timer (CNTP_NS)
     *   INTID 27 = Virtual Timer (CNTV)
     *   INTID 26 = Hypervisor Timer (CNTHP)
     *
     * We use the physical timer (30) by default to match Windows behavior.
     * The virtual timer (27) can be enabled in virtualization scenarios.
     *
     * The clock ISR runs at CLOCK_LEVEL (13) which is higher than device
     * interrupts but lower than IPI_LEVEL. This allows the scheduler
     * tick to preempt device ISRs for accurate timing.
     */
    {
        ULONG TimerIntId = KiArm64UseVirtualTimer ? 27 : 30;

        KiRawDebugPuts("[KeInitInterrupts] Timer spinlock\n");
        KeInitializeSpinLock(&KiArm64TimerLock);
        KiRawDebugPuts("[KeInitInterrupts] Timer KeInitializeInterrupt\n");
        KeInitializeInterrupt(&KiArm64TimerInterrupt,
                              KiArm64TimerIsr,
                              &KiArm64TimerPeriodTicks,
                              &KiArm64TimerLock,
                              TimerIntId,
                              CLOCK_LEVEL,
                              CLOCK_LEVEL,
                              LevelSensitive,
                              FALSE,
                              0,
                              FALSE);
        KiRawDebugPuts("[KeInitInterrupts] Timer KeConnectInterrupt\n");
        if (KeConnectInterrupt(&KiArm64TimerInterrupt))
        {
            KiRawDebugPuts("[KeInitInterrupts] Timer connect OK, starting timer\n");
            KiArm64StartLocalTimer();
            KiRawDebugPuts("[KeInitInterrupts] Timer started\n");

            /*
             * KeInitInterrupts runs before HAL phase 0 configures the GIC.
             * Do not unmask DAIF or touch ICC_IGRPEN1_EL1 here; the executive
             * enables CPU interrupt delivery after HalInitSystem(0) completes.
             */
            KiRawDebugPuts("[KeInitInterrupts] IRQ delivery remains masked until HAL phase 0\n");
        }
    }
    KiRawDebugPuts("[KeInitInterrupts] EXIT\n");
}

/*
 * KeReenableTimerInterrupt - Re-enable the timer PPI after HAL initialization
 *
 * BACKGROUND:
 * The timer PPI (Private Peripheral Interrupt) is initially connected via
 * KeConnectInterrupt during early kernel initialization (KeInitInterrupts).
 * However, at that point, the HAL's GIC initialization hasn't run yet, so
 * HalpGicUseSysRegs is FALSE. This causes HalEnableSystemInterrupt to take
 * the wrong code path and the PPI is not actually enabled in the GIC.
 *
 * After HalInitSystem(0) completes, HalpGicUseSysRegs is properly set and
 * the GIC redistributor is configured. This function re-enables the timer
 * PPI to ensure it's properly routed through the GIC.
 *
 * This fixes the "5-second timer gap" bug where the timer would only fire
 * sporadically during USB enumeration because the PPI wasn't properly enabled.
 */
VOID
NTAPI
KeReenableTimerInterrupt(VOID)
{
    ULONG TimerIntId = KiArm64UseVirtualTimer ? 27 : 30;

    /*
     * Re-enable the timer interrupt in the GIC.
     * Now that HalInitSystem(0) has run, HalpGicUseSysRegs is properly set
     * and the GIC redistributor is configured. This call will properly
     * enable the PPI in the redistributor's ISENABLER0 register.
     */
    HalEnableSystemInterrupt(TimerIntId, CLOCK_LEVEL, LevelSensitive);
    KiArm64StartLocalTimer();
}

static
VOID
KiArm64DispatchChain(_In_ ULONG IntId)
{
    PKINTERRUPT Head, Interrupt;
    PLIST_ENTRY ListHead, NextEntry;
    KIRQL RaiseIrql;
    BOOLEAN Handled = FALSE;

    /* Snapshot head without taking the global lock on every interrupt. */
    Head = KiArm64LoadInterruptHeadNoFence(IntId);
    if (!Head) return;

    ListHead = &Head->InterruptListEntry;

    if (IsListEmpty(ListHead))
    {
        /* Single ISR */
        KxAcquireSpinLock(Head->ActualLock);
        (VOID)Head->ServiceRoutine(Head, Head->ServiceContext);
        KxReleaseSpinLock(Head->ActualLock);
        return;
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
}

static
VOID
KiArm64SoftwareInterrupt(_In_ ULONG IntId)
{
    KIRQL Level = (IntId == ARM64_SGI_DPC) ? DISPATCH_LEVEL : APC_LEVEL;
    KIRQL OldIrql;

    if (!HalBeginSystemInterrupt(Level, IntId, &OldIrql))
        return;

    HalPerformEndOfInterrupt();
    _enable();

    if (IntId == ARM64_SGI_DPC)
    {
        PKPRCB Prcb = KeGetCurrentPrcb();
        if (Prcb) Prcb->DpcInterruptRequested = FALSE;
        KiDispatchInterrupt();
    }
    else
    {
        KiDeliverApc(KernelMode, NULL, NULL);
    }

    _disable();
    KeLowerIrql(OldIrql);
}

VOID
KiArm64InterruptDispatchEntry(_In_ ULONG VectorId)
{
    ULONG IntId;
    ULONG Cpu;
    KIRQL OldIrql;
    KIRQL RequestIrql = DISPATCH_LEVEL;
    PKINTERRUPT Head;
    KTRAP_FRAME LocalTrapFrame;
    PKTRAP_FRAME SavedTrapFrame;
    BOOLEAN Begun;

    /* Ask HAL for current INTID */
    IntId = HalGetInterruptSource();

    if ((IntId == ARM64_SGI_APC) || (IntId == ARM64_SGI_DPC))
    {
        KiArm64SoftwareInterrupt(IntId);
        return;
    }

    if (IntId == ARM64_SGI_FREEZE)
    {
        HalPerformEndOfInterrupt();
        return;
    }

    if (IntId == ARM64_SGI_IPI)
    {
        /* Service the reschedule/generic-call IPI directly: KiIpiServiceRoutine is
         * lockless, and taking the shared chain lock here can deadlock KeIpiGenericCall. */
        if (HalBeginSystemInterrupt(IPI_LEVEL, IntId, &OldIrql))
        {
            KiIpiServiceRoutine(NULL, NULL);
            HalEndSystemInterrupt(OldIrql, NULL);
        }
        return;
    }

    Head = (IntId < ARM64_MAX_INTID) ? KiArm64IntTable[IntId] : NULL;
    if (Head != NULL)
    {
        RequestIrql = Head->Irql;
    }

    Begun = HalBeginSystemInterrupt(RequestIrql, IntId, &OldIrql);
    if (!Begun)
        return;

    Cpu = KeGetCurrentProcessorNumber();
    if (Cpu >= MAXIMUM_PROCESSORS)
    {
        Cpu = 0;
    }

    RtlZeroMemory(&LocalTrapFrame, sizeof(LocalTrapFrame));
    LocalTrapFrame.PreviousMode = (VectorId >= 8) ? UserMode : KernelMode;
    LocalTrapFrame.PreviousIrql = OldIrql;
    SavedTrapFrame = KiArm64CurrentInterruptTrapFrame[Cpu];
    KiArm64CurrentInterruptTrapFrame[Cpu] = &LocalTrapFrame;

    if (Head != NULL)
    {
        InterlockedIncrement(&KiArm64DispatchEpoch[Cpu]);
        _enable();
        KiArm64DispatchChain(IntId);
        _disable();
        InterlockedIncrement(&KiArm64DispatchEpoch[Cpu]);
    }

    KiArm64CurrentInterruptTrapFrame[Cpu] = SavedTrapFrame;

    HalEndSystemInterrupt(OldIrql, NULL);

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

    if (Vector >= ARM64_MAX_INTID) return FALSE;
    if (Interrupt->Connected) return TRUE;

    KeAcquireSpinLock(&KiArm64IntTableLock, &OldIrql);

    Head = KiArm64IntTable[Vector];
    if (!Head)
    {
        InitializeListHead(&Interrupt->InterruptListEntry);
        KeMemoryBarrier();
        KiArm64IntTable[Vector] = Interrupt;
        HalEnableSystemInterrupt(Vector, Interrupt->Irql, Interrupt->Mode);
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
            Interrupt->InterruptListEntry.Flink = &Head->InterruptListEntry;
            Interrupt->InterruptListEntry.Blink = Head->InterruptListEntry.Blink;
            KeMemoryBarrier();
            Head->InterruptListEntry.Blink->Flink = &Interrupt->InterruptListEntry;
            Head->InterruptListEntry.Blink = &Interrupt->InterruptListEntry;
            Interrupt->Connected = TRUE;
        }
    }

    KeReleaseSpinLock(&KiArm64IntTableLock, OldIrql);

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

    {
        ULONG Cpu;

        KeMemoryBarrier();
        for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors && Cpu < MAXIMUM_PROCESSORS; Cpu++)
        {
            LONG Epoch = KiArm64DispatchEpoch[Cpu];
            ULONG Spins = 0;

            if (!(Epoch & 1))
                continue;

            while (KiArm64DispatchEpoch[Cpu] == Epoch)
            {
                YieldProcessor();
                KeMemoryBarrier();
                if (++Spins > 2000000UL)
                    break;
            }
        }
    }

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
