/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VM-exit dispatcher
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/vmx.h>
#include <rosv/vm.h>
#include <rosv/device.h>
#include <rosv/pty.h>
#include <rosv/virtio_net.h>
#include <rosv/virtio_console.h>
#include <intrin.h>

#define NDEBUG
#include <debug.h>

/* Well-known MSR indices for emulation */
#define IA32_EFER               0xC0000080
#define IA32_MISC_ENABLE        0x000001A0
#define IA32_MTRRCAP            0x000000FE
#define IA32_MTRR_DEF_TYPE      0x000002FF
#define IA32_PLATFORM_ID        0x00000017
#define IA32_BIOS_SIGN_ID       0x0000008B
#define IA32_ARCH_CAPABILITIES  0x0000010A
#define IA32_MTRR_PHYSBASE0     0x00000200
#define IA32_MTRR_PHYSMASK0     0x00000201
#define IA32_MTRR_PHYSBASE7     0x0000020E
#define IA32_MTRR_PHYSMASK7     0x0000020F
#define IA32_MTRR_FIX64K_00000  0x00000250
#define IA32_MTRR_FIX16K_80000  0x00000258
#define IA32_MTRR_FIX16K_A0000  0x00000259
#define IA32_MTRR_FIX4K_C0000   0x00000268
#define IA32_MTRR_FIX4K_F8000   0x0000026F
#define IA32_PAT                0x00000277
#define IA32_APIC_BASE          0x0000001B
#define IA32_PLATFORM_INFO      0x000000CE
#define IA32_TSC                0x00000010
#define IA32_TSC_AUX            0xC0000103
#define IA32_SYSENTER_CS        0x00000174
#define IA32_SYSENTER_ESP       0x00000175
#define IA32_SYSENTER_EIP       0x00000176
#define IA32_FS_BASE            0xC0000100
#define IA32_GS_BASE            0xC0000101
#define IA32_KERNEL_GS_BASE     0xC0000102
#define IA32_STAR               0xC0000081
#define IA32_LSTAR              0xC0000082
#define IA32_CSTAR              0xC0000083
#define IA32_FMASK              0xC0000084

#define CPUID_BIT(_bit)         (1U << (_bit))
#define CPUID_LEAF_TSC          0x00000015U
#define CPUID_LEAF_FREQ         0x00000016U

#define IA32_PLATFORM_INFO_MAX_NON_TURBO_SHIFT 8
#define IA32_PLATFORM_INFO_MAX_NON_TURBO_MASK  0xFFULL

/* VM-exit interruption-information field bits */
#define EXIT_INTR_INFO_VALID    (1U << 31)
#define EXIT_INTR_INFO_VECTOR(x) ((x) & 0xFF)
#define EXIT_INTR_INFO_TYPE(x)   (((x) >> 8) & 0x7)

/* CR access qualification decoding */
#define CR_ACCESS_CR_NUMBER(q)   ((q) & 0xF)
#define CR_ACCESS_TYPE(q)        (((q) >> 4) & 0x3)    /* 0=MOV to CR, 1=MOV from CR */
#define CR_ACCESS_REG(q)         (((q) >> 8) & 0xF)    /* GPR index */

/* I/O instruction qualification decoding */
#define IO_QUAL_SIZE(q)          ((q) & 0x7)            /* 0=1byte, 1=2byte, 3=4byte */
#define IO_QUAL_IN(q)            (((q) >> 3) & 1)       /* 1=IN, 0=OUT */
#define IO_QUAL_STRING(q)        (((q) >> 4) & 1)
#define IO_QUAL_REP(q)           (((q) >> 5) & 1)
#define IO_QUAL_IMMEDIATE(q)     (((q) >> 6) & 1)
#define IO_QUAL_PORT(q)          (((q) >> 16) & 0xFFFF)

#define ROSV_LAPIC_TIMER_PERIODIC (1U << 17)
#define ROSV_LAPIC_TIMER_BASE_HZ  100000000ULL

static
BOOLEAN
RosvShouldLogUnknownMsr(
    _In_ ULONG MsrIndex)
{
    static UCHAR LowMsrSeen[0x2000 / 8];
    static UCHAR HighMsrSeen[0x2000 / 8];
    PUCHAR Bitmap;
    ULONG BitIndex;
    UCHAR BitMask;

    if (MsrIndex <= 0x1FFF)
    {
        Bitmap = LowMsrSeen;
        BitIndex = MsrIndex;
    }
    else if (MsrIndex >= 0xC0000000 && MsrIndex <= 0xC0001FFF)
    {
        Bitmap = HighMsrSeen;
        BitIndex = MsrIndex - 0xC0000000;
    }
    else
    {
        return TRUE;
    }

    BitMask = (UCHAR)(1u << (BitIndex & 7));
    if (Bitmap[BitIndex >> 3] & BitMask)
        return FALSE;

    Bitmap[BitIndex >> 3] |= BitMask;
    return TRUE;
}

static
VOID
RosvAdvanceGuestRip(
    _Inout_ PROSV_VCPU Vcpu)
{
    ULONG64 InstrLen = RosvVmcsRead(VMCS_RO_EXIT_INSTR_LENGTH);
    ULONG64 GuestRip = RosvVmcsRead(VMCS_GUEST_RIP);

    UNREFERENCED_PARAMETER(Vcpu);

    RosvVmcsWrite(VMCS_GUEST_RIP, GuestRip + InstrLen);
}

static
BOOLEAN
RosvTryInjectGuestExternalInterrupt(
    _Inout_ PROSV_VCPU Vcpu)
{
    ULONG64 Rflags = RosvVmcsRead(VMCS_GUEST_RFLAGS);
    ULONG64 Interruptibility = RosvVmcsRead(VMCS_GUEST_INTERRUPTIBILITY);
    ULONG64 PendingInjection = RosvVmcsRead(VMCS_CTRL_ENTRY_INT_INFO);

    ROSV_ASSERT(Vcpu != NULL, "Vcpu must not be NULL");
    if (Vcpu == NULL)
        return FALSE;

    if (!(Rflags & 0x200) || (Interruptibility & 0xF))
        return FALSE;

    if (PendingInjection & EXIT_INTR_INFO_VALID)
        return FALSE;

    return TRUE;
}

static
BOOLEAN
RosvResolveLapicTimerVector(
    _In_ PROSV_VM Vm,
    _Out_ PUCHAR Vector,
    _Out_ PULONG64 ExpiredPeriodsOut)
{
    ULONG64 ExpiredPeriods;
    ULONG64 ElapsedTicks;
    ULONG64 FrequencyQpc;
    ULONG64 NowQpc;
    ULONG64 DeltaQpc;
    ULONG64 ApicBase;
    ULONG Svr;
    ULONG LvtTimer;
    ULONG InitCount;
    ULONG DivideConfig;
    ULONG DivideValue;
    UCHAR ResolvedVector;

    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");
    ROSV_ASSERT(Vector != NULL, "Vector must not be NULL");
    ROSV_ASSERT(ExpiredPeriodsOut != NULL, "ExpiredPeriodsOut must not be NULL");
    if (Vm == NULL || Vector == NULL || ExpiredPeriodsOut == NULL)
        return FALSE;

    Svr = Vm->Lapic.Svr;
    LvtTimer = Vm->Lapic.LvtTimer;
    InitCount = Vm->Lapic.TimerInitialCount;
    DivideConfig = Vm->Lapic.TimerDivideConfig;
    ApicBase = Vm->Lapic.ApicBaseMsr;

    if (InitCount == 0 || Vm->Lapic.TimerStartQpc == 0)
        return FALSE;

    switch (((DivideConfig >> 1) & 0x4) | (DivideConfig & 0x3))
    {
        case 0: DivideValue = 2; break;
        case 1: DivideValue = 4; break;
        case 2: DivideValue = 8; break;
        case 3: DivideValue = 16; break;
        case 4: DivideValue = 32; break;
        case 5: DivideValue = 64; break;
        case 6: DivideValue = 128; break;
        case 7: DivideValue = 1; break;
        default: DivideValue = 2; break;
    }

    {
        LARGE_INTEGER Counter;
        LARGE_INTEGER Frequency;
        Counter = KeQueryPerformanceCounter(&Frequency);
        NowQpc = (ULONG64)Counter.QuadPart;
        FrequencyQpc = (Frequency.QuadPart > 0) ? (ULONG64)Frequency.QuadPart : 1ULL;
    }

    if (NowQpc <= Vm->Lapic.TimerStartQpc)
        return FALSE;

    DeltaQpc = NowQpc - Vm->Lapic.TimerStartQpc;

    /*
     * Avoid ULONG64 overflow: DeltaQpc * BASE_HZ can exceed 2^64 after ~53s
     * when FrequencyQpc ~3.5 GHz.  Split into whole-seconds and remainder.
     */
    {
        ULONG64 WholeSec = DeltaQpc / FrequencyQpc;
        ULONG64 RemQpc   = DeltaQpc % FrequencyQpc;

        ElapsedTicks = WholeSec * ROSV_LAPIC_TIMER_BASE_HZ
                     + (RemQpc * ROSV_LAPIC_TIMER_BASE_HZ) / FrequencyQpc;
    }
    ElapsedTicks /= (ULONG64)DivideValue;

    if (LvtTimer & ROSV_LAPIC_TIMER_PERIODIC)
    {
        ExpiredPeriods = ElapsedTicks / (ULONG64)InitCount;
    }
    else
    {
        ExpiredPeriods = (ElapsedTicks >= (ULONG64)InitCount) ? 1ULL : 0ULL;
    }

    /* Masked/disabled LAPIC timer does not queue an unbounded interrupt backlog. */
    if ((ApicBase & (1ULL << 11)) == 0 || (Svr & (1U << 8)) == 0 || (LvtTimer & (1U << 16)))
    {
        Vm->Lapic.TimerDeliveredPeriods = ExpiredPeriods;
        return FALSE;
    }

    ResolvedVector = (UCHAR)(LvtTimer & 0xFF);
    if (ResolvedVector < 0x20)
        return FALSE;

    if (ExpiredPeriods <= Vm->Lapic.TimerDeliveredPeriods)
        return FALSE;

    *Vector = ResolvedVector;
    *ExpiredPeriodsOut = ExpiredPeriods;
    return TRUE;
}

static
VOID
RosvConsumeLapicTimerDelivery(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 ExpiredPeriods)
{
    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");
    if (Vm == NULL)
        return;

    /* Coalesce backlog and consume a single delivery for this VM-entry. */
    if (ExpiredPeriods > Vm->Lapic.TimerDeliveredPeriods + 1)
        Vm->Lapic.TimerDeliveredPeriods = ExpiredPeriods - 1;

    Vm->Lapic.TimerDeliveredPeriods++;
}

static
BOOLEAN
RosvHasPendingLapicTimer(
    _In_ PROSV_VM Vm)
{
    UCHAR Vector;
    ULONG64 ExpiredPeriods;

    if (Vm == NULL)
        return FALSE;

    return RosvResolveLapicTimerVector(Vm, &Vector, &ExpiredPeriods);
}

static
BOOLEAN
RosvTryInjectGuestTimer(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    static ULONG TimerRouteFailCount;
    static ULONG PitTimerInjectedCount;
    static ULONG LapicTimerInjectedCount;
    ULONG64 LapicExpiredPeriods = 0;
    BOOLEAN LapicPending;
    BOOLEAN PitPending;
    UCHAR Vector;

    ROSV_ASSERT(Vcpu != NULL, "Vcpu must not be NULL");
    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");
    if (Vcpu == NULL || Vm == NULL)
        return FALSE;

    ROSV_ASSERT(Vcpu->Vm == Vm, "Vcpu->Vm ownership mismatch");
    LapicPending = RosvResolveLapicTimerVector(Vm, &Vector, &LapicExpiredPeriods);
    PitPending = FALSE;

    if (!LapicPending)
    {
        PitPending = RosvInterruptHasPendingTimer(Vm);
        if (!PitPending)
        {
            if (TimerRouteFailCount < 4 || (TimerRouteFailCount % 4194304) == 0)
            {
                /* TODO(timer-hpet): Add HPET/PMTIMER emulation for robust non-PIT fallback paths. */
                ROSV_DEBUG("Timer route inactive: apic_base=0x%016llX lapic_svr=0x%08X "
                           "lapic_lvt=0x%08X lapic_init=0x%08X lapic_div=0x%08X "
                           "lapic_start=%llu lapic_delivered=%llu ioapic_rte2={low=0x%08X high=0x%08X}",
                           Vm->Lapic.ApicBaseMsr,
                           Vm->Lapic.Svr,
                           Vm->Lapic.LvtTimer,
                           Vm->Lapic.TimerInitialCount,
                           Vm->Lapic.TimerDivideConfig,
                           Vm->Lapic.TimerStartQpc,
                           Vm->Lapic.TimerDeliveredPeriods,
                           Vm->IoApic.RedirectionLow[2],
                           Vm->IoApic.RedirectionHigh[2]);
            }
            TimerRouteFailCount++;
            return FALSE;
        }
    }

    if (!RosvTryInjectGuestExternalInterrupt(Vcpu))
        return FALSE;

    if (LapicPending)
    {
        RosvConsumeLapicTimerDelivery(Vm, LapicExpiredPeriods);
        RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO, (1U << 31) | Vector);
        Vcpu->StatTimerInjected++;

        LapicTimerInjectedCount++;
        if (LapicTimerInjectedCount < 4 || (LapicTimerInjectedCount % 65536) == 0)
        {
            ROSV_DEBUG("Timer injected (LAPIC): vector=0x%02X rip=0x%llX rflags=0x%llX",
                       Vector,
                       RosvVmcsRead(VMCS_GUEST_RIP),
                       RosvVmcsRead(VMCS_GUEST_RFLAGS));
        }
        return TRUE;
    }

    /*
     * PIT channel 0 remains the bootstrap timer source until Linux switches
     * to a fully working LAPIC timer path.
     */
    if (RosvInterruptRequestTimer(Vm, &Vector))
    {
        RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO, (1U << 31) | Vector);
        Vcpu->StatTimerInjected++;
        PitTimerInjectedCount++;
        if (PitTimerInjectedCount < 8 || (PitTimerInjectedCount % 32) == 0)
        {
            ROSV_TRACE("Timer injected (PIT): vector=0x%02X rip=0x%llX rflags=0x%llX",
                       Vector,
                       RosvVmcsRead(VMCS_GUEST_RIP),
                       RosvVmcsRead(VMCS_GUEST_RFLAGS));
        }
        return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
RosvTryInjectGuestUartIrq(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    PROSV_UART_STATE Uart;
    UCHAR InterruptId;
    UCHAR Vector;

    if (!Vm || !Vm->Console)
        return FALSE;

    Uart = &Vm->Console->Uart;

    if (!Uart->InterruptPending)
        return FALSE;

    /*
     * Deliver only the interrupt sources we emulate:
     * - 0x04: Received data available (RDI)
     * - 0x02: THR empty (THRI)
     */
    InterruptId = (UCHAR)(Uart->Iir & 0x0E);
    if (InterruptId == 0x04)
    {
        if (!(Uart->Ier & UART_IER_RDI) || Uart->RxCount == 0)
            return FALSE;
    }
    else if (InterruptId == 0x02)
    {
        if (!(Uart->Ier & UART_IER_THRI))
            return FALSE;
    }
    else
    {
        return FALSE;
    }

    if (!RosvTryInjectGuestExternalInterrupt(Vcpu))
        return FALSE;

    if (!RosvInterruptRequestIrq(Vm, UART_COM1_IRQ, &Vector))
    {
        ROSV_ERR("UART inject: IRQ controller refused IRQ %u", UART_COM1_IRQ);
        return FALSE;
    }

    RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO, (1U << 31) | Vector);

    /*
     * Latch the request as edge-triggered at the hypervisor boundary.
     * UART logic will re-arm InterruptPending if unread bytes remain
     * after the guest consumes from RBR.
     */
    Uart->InterruptPending = FALSE;
    return TRUE;
}

static
BOOLEAN
RosvTryInjectGuestNicIrq(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    PROSV_RTL8139_STATE Nic;
    UCHAR Vector;

    if (!Vm || !Vm->Console)
        return FALSE;

    Nic = &Vm->Console->Rtl8139;
    if (!RosvRtl8139HasPendingInterrupt(Nic))
        return FALSE;

    if (!RosvTryInjectGuestExternalInterrupt(Vcpu))
        return FALSE;

    if (!RosvInterruptRequestIrq(Vm, Nic->PciInterruptLine, &Vector))
        return FALSE;

    RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO, (1U << 31) | Vector);
    RosvRtl8139ClearPendingInterrupt(Nic);
    return TRUE;
}

static
BOOLEAN
RosvTryInjectGuestVirtioBlkIrq(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    PROSV_VIRTIO_BLK_STATE VirtioBlk;
    UCHAR Vector;

    if (!Vm)
        return FALSE;

    VirtioBlk = &Vm->VirtioBlk;
    if (!RosvVirtioBlkHasPendingInterrupt(VirtioBlk))
        return FALSE;

    if (!RosvTryInjectGuestExternalInterrupt(Vcpu))
        return FALSE;

    if (!RosvInterruptRequestIrq(Vm, ROSV_VIRTIO_BLK_IRQ, &Vector))
        return FALSE;

    RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO, (1U << 31) | Vector);
    RosvVirtioBlkClearPendingInterrupt(VirtioBlk);
    return TRUE;
}

static
BOOLEAN
RosvTryInjectGuestVirtioNetIrq(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    PROSV_VIRTIO_NET_STATE VirtioNet;
    UCHAR Vector;

    if (!Vm)
        return FALSE;

    VirtioNet = &Vm->VirtioNet;
    if (!RosvVirtioNetHasPendingInterrupt(VirtioNet))
        return FALSE;

    /* Interrupt coalescing: defer injection if below packet/time thresholds.
     * GuestIsHalted=FALSE here since we're in exit dispatch (guest was running). */
    if (!RosvVirtioNetShouldInjectIrq(VirtioNet, FALSE))
        return FALSE;

    if (!RosvTryInjectGuestExternalInterrupt(Vcpu))
        return FALSE;

    if (!RosvInterruptRequestIrq(Vm, ROSV_VIRTIO_NET_IRQ, &Vector))
        return FALSE;

    RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO, (1U << 31) | Vector);
    RosvVirtioNetClearPendingInterrupt(VirtioNet);
    RosvVirtioNetRecordIrqInjected(VirtioNet);
    return TRUE;
}

static
BOOLEAN
RosvTryInjectGuestVirtioConIrq(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    PROSV_VIRTIO_CON_STATE VirtioCon;
    UCHAR Vector;

    if (!Vm)
        return FALSE;

    VirtioCon = &Vm->VirtioCon;
    if (!RosvVirtioConHasPendingInterrupt(VirtioCon))
        return FALSE;

    if (!RosvTryInjectGuestExternalInterrupt(Vcpu))
        return FALSE;

    if (!RosvInterruptRequestIrq(Vm, ROSV_VIRTIO_CON_IRQ, &Vector))
        return FALSE;

    ROSV_TRACE("virtio-con: injecting IRQ %u as vector 0x%02X",
               ROSV_VIRTIO_CON_IRQ, Vector);
    RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO, (1U << 31) | Vector);
    RosvVirtioConClearPendingInterrupt(VirtioCon);
    return TRUE;
}

/*
 * Try to inject any pending virtual interrupt. Returns TRUE if an interrupt
 * was injected. Checks devices in priority order.
 * If guest has IF=0 (interrupts disabled), enables interrupt-window exiting
 * so we get a VM-exit when the guest re-enables interrupts.
 */
static
BOOLEAN
RosvTryInjectPendingInterrupts(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    BOOLEAN Injected;
    ULONG64 ProcCtls;

    ROSV_ASSERT(Vcpu != NULL, "Vcpu must not be NULL");
    if (Vcpu == NULL)
        return FALSE;

    if (Vm != NULL)
        ROSV_ASSERT(Vcpu->Vm == Vm, "Vcpu->Vm ownership mismatch");

    /*
     * Interrupt injection priority order — most frequent sources first
     * to minimize wasted checks during network-heavy workloads:
     *   1. virtio-net  (highest frequency during network I/O)
     *   2. Timer       (critical for guest scheduling)
     *   3. virtio-blk  (disk I/O, moderate frequency)
     *   4. RTL8139 NIC (legacy, rarely used)
     *   5. virtio-con  (console, low frequency)
     *   6. UART        (serial, lowest frequency)
     */
    /*
     * Track whether any device had a pending interrupt during the cascade.
     * Each TryInject function returns FALSE either because no interrupt is
     * pending or because the guest cannot accept one (IF=0).  We record
     * pending state from the early-exit checks to avoid a redundant re-scan
     * of all device flags after the cascade.
     */
    Injected = RosvTryInjectGuestVirtioNetIrq(Vcpu, Vm);
    if (!Injected)
        Injected = RosvTryInjectGuestTimer(Vcpu, Vm);
    if (!Injected)
        Injected = RosvTryInjectGuestVirtioBlkIrq(Vcpu, Vm);
    if (!Injected)
        Injected = RosvTryInjectGuestNicIrq(Vcpu, Vm);
    if (!Injected)
        Injected = RosvTryInjectGuestVirtioConIrq(Vcpu, Vm);
    if (!Injected)
        Injected = RosvTryInjectGuestUartIrq(Vcpu, Vm);

    /*
     * If we have a pending device interrupt but could not inject (guest IF=0),
     * enable interrupt-window exiting so we get a VM-exit as soon as the guest
     * executes STI or clears an interruptibility block.
     *
     * We already attempted all 6 device sources above. If none succeeded but
     * at least one had a pending flag set, we know the failure was due to the
     * guest not accepting interrupts.  Rather than re-scanning all device
     * pending flags (which includes an expensive KeQueryPerformanceCounter for
     * the LAPIC timer), use a lightweight check: if injection failed AND the
     * guest has IF=0/interruptibility, assume there is pending work and enable
     * INT_WINDOW_EXIT.  The next interrupt-window exit will re-attempt injection
     * through the full cascade.
     *
     * False-positive (enable INT_WINDOW when nothing is pending) costs one
     * extra VM-exit but is harmless.  False-negative cannot happen here since
     * all pending sources were already tried.
     */
    if (!Injected)
    {
        ULONG64 Rflags = RosvVmcsRead(VMCS_GUEST_RFLAGS);
        ULONG64 Interruptibility = RosvVmcsRead(VMCS_GUEST_INTERRUPTIBILITY);

        if (!(Rflags & 0x200) || (Interruptibility & 0xF))
        {
            ProcCtls = RosvVmcsRead(VMCS_CTRL_PROC_BASED);
            if (!(ProcCtls & VMX_PROC_INT_WINDOW_EXIT))
            {
                RosvVmcsWrite(VMCS_CTRL_PROC_BASED,
                              ProcCtls | VMX_PROC_INT_WINDOW_EXIT);
            }
        }
        else
        {
            /* Guest IF=1 but injection failed (PIC ISR) — disable INT_WINDOW */
            ProcCtls = RosvVmcsRead(VMCS_CTRL_PROC_BASED);
            if (ProcCtls & VMX_PROC_INT_WINDOW_EXIT)
            {
                RosvVmcsWrite(VMCS_CTRL_PROC_BASED,
                              ProcCtls & ~VMX_PROC_INT_WINDOW_EXIT);
            }
        }
    }
    else
    {
        /*
         * We successfully injected an interrupt. If interrupt-window exiting
         * was enabled from a previous failed injection attempt, disable it now
         * to avoid unnecessary VM-exits.
         */
        ProcCtls = RosvVmcsRead(VMCS_CTRL_PROC_BASED);
        if (ProcCtls & VMX_PROC_INT_WINDOW_EXIT)
        {
            RosvVmcsWrite(VMCS_CTRL_PROC_BASED,
                          ProcCtls & ~VMX_PROC_INT_WINDOW_EXIT);
        }
    }

    return Injected;
}

VOID
RosvVmTryInjectPendingInterrupts(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    ROSV_ASSERT(Vcpu != NULL, "Vcpu must not be NULL");
    if (Vcpu == NULL)
        return;

    if (Vm == NULL)
        Vm = Vcpu->Vm;

    RosvTryInjectPendingInterrupts(Vcpu, Vm);
}

ULONG64
RosvGetGuestRegByIndex(
    _In_ PROSV_GUEST_REGS Regs,
    _In_ ULONG Index)
{
    switch (Index)
    {
        case 0:  return Regs->Rax;
        case 1:  return Regs->Rcx;
        case 2:  return Regs->Rdx;
        case 3:  return Regs->Rbx;
        case 4:  return RosvVmcsRead(VMCS_GUEST_RSP); /* RSP from VMCS */
        case 5:  return Regs->Rbp;
        case 6:  return Regs->Rsi;
        case 7:  return Regs->Rdi;
        case 8:  return Regs->R8;
        case 9:  return Regs->R9;
        case 10: return Regs->R10;
        case 11: return Regs->R11;
        case 12: return Regs->R12;
        case 13: return Regs->R13;
        case 14: return Regs->R14;
        case 15: return Regs->R15;
        default:
            ROSV_ERR("RosvGetGuestRegByIndex: invalid index %u", Index);
            return 0;
    }
}

VOID
RosvSetGuestRegByIndex(
    _Inout_ PROSV_GUEST_REGS Regs,
    _In_ ULONG Index,
    _In_ ULONG64 Value)
{
    switch (Index)
    {
        case 0:  Regs->Rax = Value; break;
        case 1:  Regs->Rcx = Value; break;
        case 2:  Regs->Rdx = Value; break;
        case 3:  Regs->Rbx = Value; break;
        case 4:  RosvVmcsWrite(VMCS_GUEST_RSP, Value); break;
        case 5:  Regs->Rbp = Value; break;
        case 6:  Regs->Rsi = Value; break;
        case 7:  Regs->Rdi = Value; break;
        case 8:  Regs->R8 = Value;  break;
        case 9:  Regs->R9 = Value;  break;
        case 10: Regs->R10 = Value; break;
        case 11: Regs->R11 = Value; break;
        case 12: Regs->R12 = Value; break;
        case 13: Regs->R13 = Value; break;
        case 14: Regs->R14 = Value; break;
        case 15: Regs->R15 = Value; break;
        default:
            ROSV_ERR("RosvSetGuestRegByIndex: invalid index %u", Index);
            break;
    }
}

/* ---- CPUID exit handler ------------------------------------------------- */

static
VOID
RosvSanitizeCpuidResult(
    _In_ ULONG Leaf,
    _In_ ULONG Subleaf,
    _Inout_updates_(4) int CpuInfo[4])
{
    ROSV_ASSERT(CpuInfo != NULL, "CpuInfo must not be NULL");
    if (CpuInfo == NULL)
        return;

    if (Leaf == CPUID_LEAF_TSC)
    {
        if (CpuInfo[0] == 0 || CpuInfo[1] == 0 || CpuInfo[2] == 0)
        {
            ULONG64 PlatformInfo = __readmsr(IA32_PLATFORM_INFO);
            ULONG Ratio = (ULONG)((PlatformInfo >> IA32_PLATFORM_INFO_MAX_NON_TURBO_SHIFT) &
                                  IA32_PLATFORM_INFO_MAX_NON_TURBO_MASK);

            if (Ratio != 0)
            {
                /*
                 * Linux prefers CPUID.15H for early TSC calibration when no
                 * PM timer / HPET exists.  Publish a conservative 100 MHz
                 * crystal-based ratio derived from IA32_PLATFORM_INFO so the
                 * guest can keep TSC as a stable clocksource.
                 */
                CpuInfo[0] = 1;              /* denominator */
                CpuInfo[1] = (int)Ratio;     /* numerator */
                CpuInfo[2] = 100000000;      /* crystal clock in Hz */
                CpuInfo[3] = 0;
            }
        }
        return;
    }

    if (Leaf == CPUID_LEAF_FREQ)
    {
        if (CpuInfo[0] == 0)
        {
            ULONG64 PlatformInfo = __readmsr(IA32_PLATFORM_INFO);
            ULONG Ratio = (ULONG)((PlatformInfo >> IA32_PLATFORM_INFO_MAX_NON_TURBO_SHIFT) &
                                  IA32_PLATFORM_INFO_MAX_NON_TURBO_MASK);

            if (Ratio != 0)
            {
                CpuInfo[0] = (int)(Ratio * 100U); /* base frequency MHz */
                if (CpuInfo[1] == 0)
                    CpuInfo[1] = CpuInfo[0];
                if (CpuInfo[2] == 0)
                    CpuInfo[2] = 100;
            }
        }
        return;
    }

    /* Keep guest-visible CPU features conservative until full XSAVE/XSTATE virtualization is complete. */
    if (Leaf == 1)
    {
        CpuInfo[2] &= ~(int)CPUID_BIT(5);   /* VMX */
        CpuInfo[2] |= (int)CPUID_BIT(31);   /* Hypervisor present */
        /* TODO(cpu-idle): Virtualize MONITOR/MWAIT or intercept VMX_EXIT_MWAIT safely. */
        /* MONITOR/MWAIT is not virtualized (can park vCPU with no wake source). */
        CpuInfo[2] &= ~(int)CPUID_BIT(3);   /* MONITOR/MWAIT */
        /* TODO(apic-msr): Advertise x2APIC once APIC MSR space is virtualized. */
        CpuInfo[2] &= ~(int)CPUID_BIT(21);  /* x2APIC (MSR space not virtualized) */
        /* TODO(apic-timer): Advertise TSC deadline timer after LAPIC timer emulation. */
        CpuInfo[2] &= ~(int)CPUID_BIT(24);  /* TSC deadline timer (not virtualized) */
        CpuInfo[3] |= (int)CPUID_BIT(9);    /* APIC present */

        /* Hide XSAVE/AVX-family features to keep IFUNC selection on legacy-safe code paths. */
        CpuInfo[2] &= ~(int)CPUID_BIT(12);  /* FMA */
        CpuInfo[2] &= ~(int)CPUID_BIT(26);  /* XSAVE */
        CpuInfo[2] &= ~(int)CPUID_BIT(27);  /* OSXSAVE */
        CpuInfo[2] &= ~(int)CPUID_BIT(28);  /* AVX */
        CpuInfo[2] &= ~(int)CPUID_BIT(29);  /* F16C */
        return;
    }

    if (Leaf == 7 && Subleaf != 0)
    {
        CpuInfo[0] = 0;
        CpuInfo[1] = 0;
        CpuInfo[2] = 0;
        CpuInfo[3] = 0;
        return;
    }

    if (Leaf == 5)
    {
        CpuInfo[0] = 0;
        CpuInfo[1] = 0;
        CpuInfo[2] = 0;
        CpuInfo[3] = 0;
        return;
    }

    if (Leaf == 7 && Subleaf == 0)
    {
        CpuInfo[1] &= ~(int)CPUID_BIT(5);   /* AVX2 */
        CpuInfo[1] &= ~(int)CPUID_BIT(16);  /* AVX512F */
        CpuInfo[1] &= ~(int)CPUID_BIT(17);  /* AVX512DQ */
        CpuInfo[1] &= ~(int)CPUID_BIT(21);  /* AVX512IFMA */
        CpuInfo[1] &= ~(int)CPUID_BIT(26);  /* AVX512PF */
        CpuInfo[1] &= ~(int)CPUID_BIT(27);  /* AVX512ER */
        CpuInfo[1] &= ~(int)CPUID_BIT(28);  /* AVX512CD */
        CpuInfo[1] &= ~(int)CPUID_BIT(30);  /* AVX512BW */
        CpuInfo[1] &= ~(int)CPUID_BIT(31);  /* AVX512VL */

        CpuInfo[2] &= ~(int)CPUID_BIT(1);   /* AVX512VBMI */
        CpuInfo[2] &= ~(int)CPUID_BIT(3);   /* PKU (no XSAVE → can't activate) */
        CpuInfo[2] &= ~(int)CPUID_BIT(5);   /* WAITPKG */
        CpuInfo[2] &= ~(int)CPUID_BIT(6);   /* AVX512VBMI2 */
        CpuInfo[2] &= ~(int)CPUID_BIT(8);   /* GFNI */
        CpuInfo[2] &= ~(int)CPUID_BIT(9);   /* VAES */
        CpuInfo[2] &= ~(int)CPUID_BIT(10);  /* VPCLMULQDQ */
        CpuInfo[2] &= ~(int)CPUID_BIT(11);  /* AVX512VNNI */
        CpuInfo[2] &= ~(int)CPUID_BIT(12);  /* AVX512BITALG */
        CpuInfo[2] &= ~(int)CPUID_BIT(14);  /* AVX512VPOPCNTDQ */
        CpuInfo[2] &= ~(int)CPUID_BIT(16);  /* LA57 (5-level paging not virtualized) */

        CpuInfo[3] &= ~(int)CPUID_BIT(2);   /* AVX512_4VNNIW */
        CpuInfo[3] &= ~(int)CPUID_BIT(3);   /* AVX512_4FMAPS */
        CpuInfo[3] &= ~(int)CPUID_BIT(8);   /* AVX512VP2INTERSECT */
        return;
    }

    if (Leaf == 0xD)
    {
        CpuInfo[0] = 0;
        CpuInfo[1] = 0;
        CpuInfo[2] = 0;
        CpuInfo[3] = 0;
        return;
    }

    if ((Leaf & 0xF0000000U) == 0x40000000U)
    {
        if (Leaf == 0x40000000U)
        {
            CpuInfo[0] = 0x40000001;               /* max hypervisor leaf */
            CpuInfo[1] = 0x56534F52;               /* "ROSV" (little-endian) */
            CpuInfo[2] = 0;
            CpuInfo[3] = 0;
        }
        else
        {
            CpuInfo[0] = 0;
            CpuInfo[1] = 0;
            CpuInfo[2] = 0;
            CpuInfo[3] = 0;
        }
        return;
    }

    if (Leaf == 0x80000001U)
        CpuInfo[2] &= ~(int)CPUID_BIT(2); /* SVM */
}

static
BOOLEAN
RosvHandleCpuid(
    _Inout_ PROSV_VCPU Vcpu)
{
    int CpuInfo[4];
    ULONG Leaf;
    ULONG Subleaf;

    ROSV_ASSERT(Vcpu != NULL, "Vcpu must not be NULL");
    if (Vcpu == NULL)
        return FALSE;

    Leaf = (ULONG)Vcpu->GuestRegs.Rax;
    Subleaf = (ULONG)Vcpu->GuestRegs.Rcx;

    /* Execute CPUID on the host with the guest's requested leaf */
    __cpuidex(CpuInfo, (int)Leaf, (int)Subleaf);

    RosvSanitizeCpuidResult(Leaf, Subleaf, CpuInfo);

    Vcpu->GuestRegs.Rax = (ULONG64)(ULONG)CpuInfo[0];
    Vcpu->GuestRegs.Rbx = (ULONG64)(ULONG)CpuInfo[1];
    Vcpu->GuestRegs.Rcx = (ULONG64)(ULONG)CpuInfo[2];
    Vcpu->GuestRegs.Rdx = (ULONG64)(ULONG)CpuInfo[3];

    RosvAdvanceGuestRip(Vcpu);
    return TRUE;
}

/* ---- HLT exit handler --------------------------------------------------- */

static
BOOLEAN
RosvHandleHlt(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    LARGE_INTEGER Freq;
    ULONG64 Rflags = RosvVmcsRead(VMCS_GUEST_RFLAGS);

    /* Record wall-clock time of this HLT for safety-net spin detection */
    Vcpu->LastHltQpc = KeQueryPerformanceCounter(&Freq);

    /* If interrupts are disabled, this is a dead halt - stop the VM */
    if (!(Rflags & 0x200)) /* IF flag = bit 9 */
    {
        ROSV_ERR("  HLT with interrupts disabled at RIP=0x%016llX - guest is halted (panic), stopping VM",
                 RosvVmcsRead(VMCS_GUEST_RIP));
        ROSV_ERR("  Guest state dump:");
        ROSV_ERR("    RAX=0x%016llX RBX=0x%016llX RCX=0x%016llX RDX=0x%016llX",
                 Vcpu->GuestRegs.Rax, Vcpu->GuestRegs.Rbx,
                 Vcpu->GuestRegs.Rcx, Vcpu->GuestRegs.Rdx);
        ROSV_ERR("    RSI=0x%016llX RDI=0x%016llX RBP=0x%016llX RSP=0x%016llX",
                 Vcpu->GuestRegs.Rsi, Vcpu->GuestRegs.Rdi,
                 Vcpu->GuestRegs.Rbp, RosvVmcsRead(VMCS_GUEST_RSP));
        ROSV_ERR("    R8 =0x%016llX R9 =0x%016llX R10=0x%016llX R11=0x%016llX",
                 Vcpu->GuestRegs.R8, Vcpu->GuestRegs.R9,
                 Vcpu->GuestRegs.R10, Vcpu->GuestRegs.R11);
        ROSV_ERR("    CR0=0x%016llX CR3=0x%016llX CR4=0x%016llX EFER=0x%016llX",
                 RosvVmcsRead(VMCS_GUEST_CR0), RosvVmcsRead(VMCS_GUEST_CR3),
                 RosvVmcsRead(VMCS_GUEST_CR4), RosvVmcsRead(VMCS_GUEST_EFER));
        ROSV_ERR("    CS=0x%04X SS=0x%04X DS=0x%04X ES=0x%04X",
                 (ULONG)RosvVmcsRead(VMCS_GUEST_CS_SEL),
                 (ULONG)RosvVmcsRead(VMCS_GUEST_SS_SEL),
                 (ULONG)RosvVmcsRead(VMCS_GUEST_DS_SEL),
                 (ULONG)RosvVmcsRead(VMCS_GUEST_ES_SEL));
        ROSV_ERR("    ExitCount=%llu", Vcpu->ExitCount);
        return FALSE;
    }

    /*
     * HLT consumes the STI shadow: the guest did STI; HLT, so the
     * one-instruction interruptibility window is over. Clear the
     * interruptibility-state bits so interrupt injection works on
     * the next VM-entry (otherwise the STI-blocking bit prevents it).
     */
    RosvVmcsWrite(VMCS_GUEST_INTERRUPTIBILITY, 0);

    /* Advance past HLT and try to inject pending interrupts */
    RosvAdvanceGuestRip(Vcpu);

    /* Clear stale wake signal BEFORE checking for pending interrupts.
     * This closes the TOCTOU race: if a device signals between our
     * clear and check, the check will find the pending interrupt.
     * If it signals after our check, the event will be signaled
     * and KeWaitForSingleObject returns immediately. */
    KeClearEvent(&Vcpu->HaltWakeEvent);

    if (RosvTryInjectPendingInterrupts(Vcpu, Vm))
    {
        /* Interrupt was injected, guest has work to do - resume immediately */
        return TRUE;
    }

    /*
     * No pending interrupts: the guest is genuinely idle.
     * Yield the host CPU instead of busy-spinning back into VMRESUME.
     */
    {
        static ULONG HltYieldCount;
        LARGE_INTEGER Timeout;

        LARGE_INTEGER HltStart, HltEnd;

        /* 10ms relative timeout (100ns units, negative = relative) */
        Timeout.QuadPart = -(LONGLONG)(10 * 1000 * 10); /* 10ms */

        HltStart = KeQueryPerformanceCounter(NULL);

        KeWaitForSingleObject(
            &Vcpu->HaltWakeEvent,
            Executive,
            KernelMode,
            FALSE,
            &Timeout);

        HltEnd = KeQueryPerformanceCounter(NULL);
        Vcpu->StatHltTicks += (ULONG64)(HltEnd.QuadPart - HltStart.QuadPart);
        Vcpu->StatHltYield++;
        HltYieldCount++;
        if (HltYieldCount < 4 || (HltYieldCount % 131072) == 0)
        {
            ROSV_TRACE("HLT yield #%u: retrying interrupt injection", HltYieldCount);
        }

        /* After waking, try to inject interrupts again before resuming guest */
        RosvTryInjectPendingInterrupts(Vcpu, Vm);
    }

    return TRUE;
}

/* ---- CR access exit handler --------------------------------------------- */

static
BOOLEAN
RosvHandleCrAccess(
    _Inout_ PROSV_VCPU Vcpu)
{
    ULONG64 Qual = RosvVmcsRead(VMCS_RO_EXIT_QUALIFICATION);
    ULONG CrNum = (ULONG)CR_ACCESS_CR_NUMBER(Qual);
    ULONG AccessType = (ULONG)CR_ACCESS_TYPE(Qual);
    ULONG RegIndex = (ULONG)CR_ACCESS_REG(Qual);
    ULONG64 RegVal;

    if (AccessType == 0) /* MOV to CR */
    {
        RegVal = RosvGetGuestRegByIndex(&Vcpu->GuestRegs, RegIndex);

        switch (CrNum)
        {
            case 0:
            {
                /* Apply CR0 fixed-bit constraints from cached VMX capabilities */
                ULONG64 Cr0Fixed0 = Vcpu->VmxCaps.Cr0Fixed0;
                ULONG64 Cr0Fixed1 = Vcpu->VmxCaps.Cr0Fixed1;
                ULONG64 AdjustedCr0 = (RegVal | Cr0Fixed0) & Cr0Fixed1;

                if (AdjustedCr0 != (ULONG64)RegVal)
                {
                    ROSV_TRACE("CR0 adjusted: guest wrote 0x%llX, VMCS gets 0x%llX",
                               (unsigned long long)RegVal, (unsigned long long)AdjustedCr0);
                }

                RosvVmcsWrite(VMCS_GUEST_CR0, AdjustedCr0);
                RosvVmcsWrite(VMCS_CTRL_CR0_READ_SHADOW, RegVal);
                break;
            }
            case 3:
                RosvVmcsWrite(VMCS_GUEST_CR3, RegVal);
                /* Invalidate GVA→GPA translation cache on address space switch */
                RosvEptGvaCacheInvalidate();
                break;
            case 4:
                /* Force VMXE (CR4_FIXED0 requires it), shadow has guest's view */
                RosvVmcsWrite(VMCS_CTRL_CR4_READ_SHADOW, RegVal);
                RosvVmcsWrite(VMCS_GUEST_CR4, RegVal | 0x2000);
                break;
            default:
                ROSV_ERR("  Unsupported MOV to CR%u", CrNum);
                return FALSE;
        }
    }
    else if (AccessType == 1) /* MOV from CR */
    {
        switch (CrNum)
        {
            case 0:
                RegVal = RosvVmcsRead(VMCS_GUEST_CR0);
                break;
            case 3:
                RegVal = RosvVmcsRead(VMCS_GUEST_CR3);
                break;
            case 4:
                RegVal = RosvVmcsRead(VMCS_GUEST_CR4);
                break;
            default:
                ROSV_ERR("  Unsupported MOV from CR%u", CrNum);
                return FALSE;
        }

        RosvSetGuestRegByIndex(&Vcpu->GuestRegs, RegIndex, RegVal);
    }
    else
    {
        ROSV_ERR("  Unknown CR access type %u", AccessType);
        return FALSE;
    }

    RosvAdvanceGuestRip(Vcpu);
    return TRUE;
}

/* ---- GVA to HVA translation for string I/O ------------------------------ */

/*
 * Walk guest page tables (4-level, long mode) to translate a guest virtual
 * address to a guest physical address. This is a local copy of the logic in
 * ept_exit.c (which is static there) needed for string I/O emulation.
 */
static
BOOLEAN
RosvIoGvaToGpa(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 Gva,
    _Out_ PULONG64 Gpa)
{
    ULONG64 Cr3;
    ULONG64 Entry;
    ULONG64 TableGpa;
    NTSTATUS Status;
    ULONG Index;

    *Gpa = 0;

    Cr3 = RosvVmcsRead(VMCS_GUEST_CR3) & ~0xFFFULL;

    /* PML4 */
    Index = (ULONG)((Gva >> 39) & 0x1FF);
    Status = RosvMemoryCopyFromGpa(Vm, &Entry, Cr3 + Index * 8, sizeof(Entry));
    if (!NT_SUCCESS(Status) || !(Entry & 1))
        return FALSE;

    /* PDPT */
    TableGpa = Entry & 0x000FFFFFFFFFF000ULL;
    Index = (ULONG)((Gva >> 30) & 0x1FF);
    Status = RosvMemoryCopyFromGpa(Vm, &Entry, TableGpa + Index * 8, sizeof(Entry));
    if (!NT_SUCCESS(Status) || !(Entry & 1))
        return FALSE;

    if (Entry & 0x80) /* 1GB huge page */
    {
        *Gpa = (Entry & 0x000FFFFFC0000000ULL) | (Gva & 0x3FFFFFFFULL);
        return TRUE;
    }

    /* PD */
    TableGpa = Entry & 0x000FFFFFFFFFF000ULL;
    Index = (ULONG)((Gva >> 21) & 0x1FF);
    Status = RosvMemoryCopyFromGpa(Vm, &Entry, TableGpa + Index * 8, sizeof(Entry));
    if (!NT_SUCCESS(Status) || !(Entry & 1))
        return FALSE;

    if (Entry & 0x80) /* 2MB huge page */
    {
        *Gpa = (Entry & 0x000FFFFFFFE00000ULL) | (Gva & 0x1FFFFFULL);
        return TRUE;
    }

    /* PT */
    TableGpa = Entry & 0x000FFFFFFFFFF000ULL;
    Index = (ULONG)((Gva >> 12) & 0x1FF);
    Status = RosvMemoryCopyFromGpa(Vm, &Entry, TableGpa + Index * 8, sizeof(Entry));
    if (!NT_SUCCESS(Status) || !(Entry & 1))
        return FALSE;

    *Gpa = (Entry & 0x000FFFFFFFFFF000ULL) | (Gva & 0xFFFULL);
    return TRUE;
}

/*
 * Translate a guest virtual address to a host virtual address.
 * Returns NULL if the translation fails.
 */
static
PVOID
RosvIoGvaToHva(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 Gva)
{
    ULONG64 Gpa;

    if (!RosvIoGvaToGpa(Vm, Gva, &Gpa))
        return NULL;

    return RosvMemoryGpaToHva(Vm, Gpa);
}

/* ---- I/O instruction exit handler --------------------------------------- */

static
BOOLEAN
RosvHandleIo(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm)
{
    static ULONG PitIoTraceCount;
    static ULONG StringIoTraceCount;
    ULONG64 Qual = RosvVmcsRead(VMCS_RO_EXIT_QUALIFICATION);
    USHORT Port = (USHORT)IO_QUAL_PORT(Qual);
    ULONG Size;
    BOOLEAN IsIn = IO_QUAL_IN(Qual) ? TRUE : FALSE;
    BOOLEAN IsString = IO_QUAL_STRING(Qual) ? TRUE : FALSE;
    BOOLEAN IsRep = IO_QUAL_REP(Qual) ? TRUE : FALSE;
    ULONG Value;

    switch (IO_QUAL_SIZE(Qual))
    {
        case 0: Size = 1; break;
        case 1: Size = 2; break;
        case 3: Size = 4; break;
        default: Size = 1; break;
    }

    /*
     * String I/O (INS/OUTS with optional REP prefix).
     * These operate on ES:RDI (INS) or DS:RSI (OUTS) in guest memory,
     * repeating RCX times if REP-prefixed.
     */
    if (IsString)
    {
        ULONG64 Rdi = Vcpu->GuestRegs.Rdi;
        ULONG64 Rsi = Vcpu->GuestRegs.Rsi;
        ULONG64 Rcx = IsRep ? Vcpu->GuestRegs.Rcx : 1;
        ULONG64 Rflags = RosvVmcsRead(VMCS_GUEST_RFLAGS);
        BOOLEAN Df = (Rflags >> 10) & 1; /* Direction flag */
        LONG Step = Df ? -(LONG)Size : (LONG)Size;
        ULONG64 i;

        if (StringIoTraceCount < 8)
        {
            ROSV_TRACE("String I/O: %s port=0x%X size=%u count=%llu %s rdi=0x%llX rsi=0x%llX",
                       IsIn ? "INS" : "OUTS", Port, Size, Rcx,
                       IsRep ? "REP" : "", Rdi, Rsi);
            StringIoTraceCount++;
        }

        for (i = 0; i < Rcx; i++)
        {
            if (IsIn)
            {
                /* INS: read from I/O port, write to ES:RDI in guest memory */
                PVOID GuestHva;

                Value = 0xFFFFFFFF;
                if (Vm->Console)
                    RosvIoHandleIn(Vm->Console, Port, (UCHAR)Size, &Value);

                GuestHva = RosvIoGvaToHva(Vm, Rdi);
                if (GuestHva == NULL)
                {
                    ROSV_ERR("String I/O INS: GVA 0x%llX translation failed at iteration %llu", Rdi, i);
                    return FALSE;
                }

                RtlCopyMemory(GuestHva, &Value, Size);
                Rdi += Step;
            }
            else
            {
                /* OUTS: read from DS:RSI in guest memory, write to I/O port */
                PVOID GuestHva;

                GuestHva = RosvIoGvaToHva(Vm, Rsi);
                if (GuestHva == NULL)
                {
                    ROSV_ERR("String I/O OUTS: GVA 0x%llX translation failed at iteration %llu", Rsi, i);
                    return FALSE;
                }

                Value = 0;
                RtlCopyMemory(&Value, GuestHva, Size);

                if (Vm->Console)
                    RosvIoHandleOut(Vm->Console, Port, (UCHAR)Size, Value);

                Rsi += Step;
            }
        }

        /* Update guest registers */
        if (IsIn)
            Vcpu->GuestRegs.Rdi = Rdi;
        else
            Vcpu->GuestRegs.Rsi = Rsi;

        if (IsRep)
            Vcpu->GuestRegs.Rcx = 0;

        RosvAdvanceGuestRip(Vcpu);
        return TRUE;
    }

    /* Non-string (scalar) I/O */
    if (IsIn)
    {
        /* Call device I/O dispatch for port read */
        Value = 0xFFFFFFFF; /* Default: bus float */
        if (Vm->Console)
            RosvIoHandleIn(Vm->Console, Port, (UCHAR)Size, &Value);

        if ((Port == 0x61 || (Port >= 0x40 && Port <= 0x43)) &&
            ((PitIoTraceCount < 8) || ((PitIoTraceCount & 0x3FF) == 0)))
        {
            ROSV_TRACE("I/O IN port=0x%X size=%u value=0x%X rip=0x%llX",
                       Port,
                       Size,
                       Value,
                       RosvVmcsRead(VMCS_GUEST_RIP));
            PitIoTraceCount++;
        }

        /* Write result to guest EAX (masked by size) */
        switch (Size)
        {
            case 1:
                Vcpu->GuestRegs.Rax = (Vcpu->GuestRegs.Rax & ~0xFFULL) | (Value & 0xFF);
                break;
            case 2:
                Vcpu->GuestRegs.Rax = (Vcpu->GuestRegs.Rax & ~0xFFFFULL) | (Value & 0xFFFF);
                break;
            case 4:
                Vcpu->GuestRegs.Rax = (ULONG64)(Value & 0xFFFFFFFF);
                break;
        }
    }
    else
    {
        /* Port write: read value from guest EAX */
        Value = (ULONG)Vcpu->GuestRegs.Rax;
        switch (Size)
        {
            case 1: Value &= 0xFF; break;
            case 2: Value &= 0xFFFF; break;
        }
        if (Vm->Console)
            RosvIoHandleOut(Vm->Console, Port, (UCHAR)Size, Value);

        if ((Port == 0x61 || (Port >= 0x40 && Port <= 0x43)) &&
            ((PitIoTraceCount < 8) || ((PitIoTraceCount & 0x3FF) == 0)))
        {
            ROSV_TRACE("I/O OUT port=0x%X size=%u value=0x%X rip=0x%llX",
                       Port,
                       Size,
                       Value,
                       RosvVmcsRead(VMCS_GUEST_RIP));
            PitIoTraceCount++;
        }
    }

    RosvAdvanceGuestRip(Vcpu);
    return TRUE;
}

/* ---- RDMSR exit handler ------------------------------------------------- */

static
BOOLEAN
RosvHandleRdmsr(
    _Inout_ PROSV_VCPU Vcpu)
{
    ULONG MsrIndex = (ULONG)Vcpu->GuestRegs.Rcx;
    ULONG64 Value = 0;

    switch (MsrIndex)
    {
        case IA32_PLATFORM_ID:
            Value = 0;
            break;

        case IA32_BIOS_SIGN_ID:
            Value = 0;
            break;

        case IA32_EFER:
            Value = RosvVmcsRead(VMCS_GUEST_EFER);
            break;

        case IA32_MISC_ENABLE:
            /* Pass through host value with safe filtering */
            Value = __readmsr(IA32_MISC_ENABLE);
            break;

        case IA32_MTRRCAP:
            Value = __readmsr(IA32_MTRRCAP);
            break;

        case IA32_MTRR_DEF_TYPE:
            /* Return WB as default memory type */
            Value = 0x06; /* WB, fixed/variable MTRRs disabled */
            break;

        case IA32_PAT:
            Value = RosvVmcsRead(VMCS_GUEST_PAT);
            break;

        case IA32_APIC_BASE:
            if (Vcpu->Vm != NULL)
                Value = Vcpu->Vm->Lapic.ApicBaseMsr;
            else
                Value = 0xFEE00000ULL | (1ULL << 11) | (1ULL << 8);
            break;

        case IA32_PLATFORM_INFO:
            /* Pass through host value so guest can determine TSC frequency */
            Value = __readmsr(IA32_PLATFORM_INFO);
            break;

        case IA32_ARCH_CAPABILITIES:
            /* Keep this masked until a virtualized capabilities policy exists. */
            Value = 0;
            break;

        case IA32_TSC:
            Value = __rdtsc();
            break;

        case IA32_TSC_AUX:
            Value = Vcpu->GuestTscAux;
            break;

        case IA32_SYSENTER_CS:
            Value = RosvVmcsRead(VMCS_GUEST_SYSENTER_CS);
            break;

        case IA32_SYSENTER_ESP:
            Value = RosvVmcsRead(VMCS_GUEST_SYSENTER_ESP);
            break;

        case IA32_SYSENTER_EIP:
            Value = RosvVmcsRead(VMCS_GUEST_SYSENTER_EIP);
            break;

        case IA32_FS_BASE:
            Value = RosvVmcsRead(VMCS_GUEST_FS_BASE);
            break;

        case IA32_GS_BASE:
            Value = RosvVmcsRead(VMCS_GUEST_GS_BASE);
            break;

        case IA32_KERNEL_GS_BASE:
            Value = Vcpu->GuestKernelGsBase;
            break;

        case IA32_STAR:
            Value = Vcpu->GuestStar;
            break;

        case IA32_LSTAR:
            Value = Vcpu->GuestLstar;
            break;

        case IA32_CSTAR:
            Value = Vcpu->GuestCstar;
            break;

        case IA32_FMASK:
            Value = Vcpu->GuestFmask;
            break;

        default:
            if ((MsrIndex >= IA32_MTRR_PHYSBASE0 && MsrIndex <= IA32_MTRR_PHYSMASK7) ||
                MsrIndex == IA32_MTRR_FIX64K_00000 ||
                MsrIndex == IA32_MTRR_FIX16K_80000 ||
                MsrIndex == IA32_MTRR_FIX16K_A0000 ||
                (MsrIndex >= IA32_MTRR_FIX4K_C0000 && MsrIndex <= IA32_MTRR_FIX4K_F8000))
            {
                /* MTRR virtualization is not exposed; return architectural zero. */
                Value = 0;
                break;
            }

            /* Unknown MSR - return 0 and log */
            if (RosvShouldLogUnknownMsr(MsrIndex))
                ROSV_WARN("  RDMSR: unhandled MSR 0x%08X, returning 0", MsrIndex);
            Value = 0;
            break;
    }

    /* Result goes in EDX:EAX */
    Vcpu->GuestRegs.Rax = Value & 0xFFFFFFFF;
    Vcpu->GuestRegs.Rdx = Value >> 32;

    RosvAdvanceGuestRip(Vcpu);
    return TRUE;
}

/* ---- WRMSR exit handler ------------------------------------------------- */

static
BOOLEAN
RosvHandleWrmsr(
    _Inout_ PROSV_VCPU Vcpu)
{
    ULONG MsrIndex = (ULONG)Vcpu->GuestRegs.Rcx;
    ULONG64 Value = (Vcpu->GuestRegs.Rdx << 32) | (Vcpu->GuestRegs.Rax & 0xFFFFFFFF);

    switch (MsrIndex)
    {
        case IA32_PLATFORM_ID:
        case IA32_BIOS_SIGN_ID:
            /* Read-only informational MSRs */
            break;

        case IA32_EFER:
        {
            /* Validate EFER: only allow SCE (bit 0), LME (bit 8), LMA (bit 10), NXE (bit 11) */
            ULONG64 ValidBits = (1ULL << 0) | (1ULL << 8) | (1ULL << 10) | (1ULL << 11);
            if (Value & ~ValidBits)
            {
                ROSV_WARN("WRMSR EFER: reserved bits set (0x%llX), masking",
                          (unsigned long long)Value);
                Value &= ValidBits;
            }
            RosvVmcsWrite(VMCS_GUEST_EFER, Value);
            break;
        }

        case IA32_PAT:
            RosvVmcsWrite(VMCS_GUEST_PAT, Value);
            break;

        case IA32_MTRR_DEF_TYPE:
            /* Guest setting MTRR default type - EPT controls memory types, ignore */
            break;

        case IA32_SYSENTER_CS:
            RosvVmcsWrite(VMCS_GUEST_SYSENTER_CS, Value);
            break;

        case IA32_SYSENTER_ESP:
            RosvVmcsWrite(VMCS_GUEST_SYSENTER_ESP, Value);
            break;

        case IA32_SYSENTER_EIP:
            RosvVmcsWrite(VMCS_GUEST_SYSENTER_EIP, Value);
            break;

        case IA32_FS_BASE:
            RosvVmcsWrite(VMCS_GUEST_FS_BASE, Value);
            break;

        case IA32_GS_BASE:
            RosvVmcsWrite(VMCS_GUEST_GS_BASE, Value);
            break;

        case IA32_KERNEL_GS_BASE:
            Vcpu->GuestKernelGsBase = Value;
            if (Vcpu->EntryLoadList != NULL)
                Vcpu->EntryLoadList[0].MsrValue = Value;  /* MSR_IDX_KERNEL_GS_BASE */
            break;

        case IA32_STAR:
            Vcpu->GuestStar = Value;
            if (Vcpu->EntryLoadList != NULL)
                Vcpu->EntryLoadList[1].MsrValue = Value;  /* MSR_IDX_STAR */
            break;

        case IA32_LSTAR:
            Vcpu->GuestLstar = Value;
            if (Vcpu->EntryLoadList != NULL)
                Vcpu->EntryLoadList[2].MsrValue = Value;  /* MSR_IDX_LSTAR */
            break;

        case IA32_CSTAR:
            Vcpu->GuestCstar = Value;
            if (Vcpu->EntryLoadList != NULL)
                Vcpu->EntryLoadList[3].MsrValue = Value;  /* MSR_IDX_CSTAR */
            break;

        case IA32_FMASK:
            Vcpu->GuestFmask = Value;
            if (Vcpu->EntryLoadList != NULL)
                Vcpu->EntryLoadList[4].MsrValue = Value;  /* MSR_IDX_FMASK */
            break;

        case IA32_MISC_ENABLE:
            /* Read-mostly MSR, ignore writes */
            break;

        case IA32_APIC_BASE:
            if (Vcpu->Vm != NULL)
            {
                ULONG64 NewApicBase;
                ULONG64 RequestedBase;
                static BOOLEAN WarnedX2ApicUnsupported;
                static BOOLEAN WarnedLapicBaseFixed;

                NewApicBase = Value & 0x00000000FFFFF900ULL; /* [31:12], bit11, bit8 */
                NewApicBase |= (1ULL << 8); /* single-vCPU VM always runs as BSP */

                if (Value & (1ULL << 10))
                {
                    if (!WarnedX2ApicUnsupported)
                    {
                        ROSV_WARN("WRMSR IA32_APIC_BASE requested x2APIC; x2APIC MSR space is not virtualized yet");
                        WarnedX2ApicUnsupported = TRUE;
                    }
                    /* TODO(apic-msr): Implement x2APIC register/MSI MSR virtualization. */
                }

                RequestedBase = NewApicBase & 0x00000000FFFFF000ULL;
                if (RequestedBase != 0x00000000FEE00000ULL)
                {
                    if (!WarnedLapicBaseFixed)
                    {
                        ROSV_WARN("WRMSR IA32_APIC_BASE requested base 0x%llX; forcing 0xFEE00000",
                                  RequestedBase);
                        WarnedLapicBaseFixed = TRUE;
                    }
                    NewApicBase = (NewApicBase & ~0x00000000FFFFF000ULL) | 0x00000000FEE00000ULL;
                }

                Vcpu->Vm->Lapic.ApicBaseMsr = NewApicBase;
            }
            break;

        case IA32_TSC:
            /* Ignore TSC writes */
            break;

        case IA32_TSC_AUX:
            /* IA32_TSC_AUX is a 32-bit architectural payload in x86-64. */
            Vcpu->GuestTscAux = (Value & 0xFFFFFFFFULL);
            break;

        case IA32_ARCH_CAPABILITIES:
            /* Read-only on hardware */
            break;

        default:
            if ((MsrIndex >= IA32_MTRR_PHYSBASE0 && MsrIndex <= IA32_MTRR_PHYSMASK7) ||
                MsrIndex == IA32_MTRR_FIX64K_00000 ||
                MsrIndex == IA32_MTRR_FIX16K_80000 ||
                MsrIndex == IA32_MTRR_FIX16K_A0000 ||
                (MsrIndex >= IA32_MTRR_FIX4K_C0000 && MsrIndex <= IA32_MTRR_FIX4K_F8000))
            {
                /* MTRR state is not virtualized; keep EPT WB semantics. */
                break;
            }

            if (RosvShouldLogUnknownMsr(MsrIndex))
                ROSV_WARN("  WRMSR: unhandled MSR 0x%08X = 0x%016llX (ignored)", MsrIndex, Value);
            break;
    }

    RosvAdvanceGuestRip(Vcpu);
    return TRUE;
}

/* ---- Exception/NMI exit handler ----------------------------------------- */

static
BOOLEAN
RosvHandleException(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm)
{
    ULONG64 IntrInfo = RosvVmcsRead(VMCS_RO_EXIT_INT_INFO);
    ULONG64 ErrorCode = RosvVmcsRead(VMCS_RO_EXIT_INT_ERRCODE);
    ULONG64 GuestRip = RosvVmcsRead(VMCS_GUEST_RIP);
    ULONG Vector = EXIT_INTR_INFO_VECTOR((ULONG)IntrInfo);

    if (!(IntrInfo & EXIT_INTR_INFO_VALID))
    {
        ROSV_ERR("  Interrupt info not valid");
        return FALSE;
    }

    /* During early boot debugging, log ALL exceptions with full state */
    switch (Vector)
    {
        case 2: /* NMI - re-inject into guest */
            RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO,
                          (1ULL << 31) |   /* valid */
                          (2ULL << 8)  |   /* type = NMI */
                          2);             /* vector = 2 */
            return TRUE;

        case 14: /* #PF - Page Fault */
        {
            ULONG64 Cr2 = RosvVmcsRead(VMCS_RO_EXIT_QUALIFICATION);
            ROSV_ERR("  Guest #PF at RIP=0x%016llX CR2=0x%016llX error_code=0x%llX",
                     GuestRip, Cr2, ErrorCode);
            ROSV_ERR("    %s %s %s %s %s",
                     (ErrorCode & 1) ? "PRESENT" : "not-present",
                     (ErrorCode & 2) ? "WRITE" : "READ",
                     (ErrorCode & 4) ? "USER" : "KERNEL",
                     (ErrorCode & 8) ? "RSVD" : "",
                     (ErrorCode & 16) ? "IFETCH" : "");
            ROSV_ERR("    RAX=0x%016llX RBX=0x%016llX RCX=0x%016llX RDX=0x%016llX",
                     Vcpu->GuestRegs.Rax, Vcpu->GuestRegs.Rbx,
                     Vcpu->GuestRegs.Rcx, Vcpu->GuestRegs.Rdx);
            ROSV_ERR("    RSI=0x%016llX RDI=0x%016llX RSP=0x%016llX",
                     Vcpu->GuestRegs.Rsi, Vcpu->GuestRegs.Rdi,
                     RosvVmcsRead(VMCS_GUEST_RSP));
            ROSV_ERR("    CR0=0x%016llX CR3=0x%016llX CR4=0x%016llX",
                     RosvVmcsRead(VMCS_GUEST_CR0), RosvVmcsRead(VMCS_GUEST_CR3),
                     RosvVmcsRead(VMCS_GUEST_CR4));
            /* Stop VM so we can see the fault details */
            return FALSE;
        }

        case 6: /* #UD - Undefined Opcode */
        {
            ULONG64 InstrGpa;
            ROSV_ERR("  Guest #UD at RIP=0x%016llX", GuestRip);
            ROSV_ERR("    RAX=0x%016llX RCX=0x%016llX RDX=0x%016llX",
                     Vcpu->GuestRegs.Rax, Vcpu->GuestRegs.Rcx, Vcpu->GuestRegs.Rdx);
            ROSV_ERR("    CR0=0x%016llX CR4=0x%016llX EFER=0x%016llX",
                     RosvVmcsRead(VMCS_GUEST_CR0),
                     RosvVmcsRead(VMCS_GUEST_CR4),
                     RosvVmcsRead(VMCS_GUEST_EFER));
            /* Try to read instruction bytes via guest-linear → guest-physical mapping.
             * For kernel addresses (0xFFFFFFFF8xxxxxxx), try physical = RIP - 0xFFFFFFFF80000000 */
            InstrGpa = GuestRip;
            if (GuestRip >= 0xFFFFFFFF80000000ULL)
                InstrGpa = GuestRip - 0xFFFFFFFF80000000ULL;
            {
                PVOID InstrHva = RosvMemoryGpaToHva(Vm, InstrGpa);
                if (InstrHva)
                {
                    UCHAR *Bytes = (UCHAR *)InstrHva;
                    ROSV_ERR("    Instruction bytes at GPA=0x%llX: %02X %02X %02X %02X %02X %02X %02X %02X",
                             InstrGpa, Bytes[0], Bytes[1], Bytes[2], Bytes[3],
                             Bytes[4], Bytes[5], Bytes[6], Bytes[7]);
                }
            }
            return FALSE;
        }

        case 8: /* #DF - Double Fault */
            ROSV_ERR("  Guest #DF at RIP=0x%016llX error_code=0x%llX", GuestRip, ErrorCode);
            RosvVmxDumpVmcs();
            /* Reinject #DF into guest: type=3 (hw exception), bit 11 (error code), bit 31 (valid) */
            RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO,
                          (1ULL << 31) | (1ULL << 11) | (3ULL << 8) | 8);
            RosvVmcsWrite(VMCS_CTRL_ENTRY_EXCEPTION_ERRCODE, ErrorCode);
            return TRUE;

        default:
        {
            /*
             * Reinject any intercepted exception back into the guest.
             * The guest kernel has its own IDT handlers for #GP, #UD, etc.
             * Exceptions with error codes: #DF(8), #TS(10), #NP(11), #SS(12), #GP(13), #PF(14), #AC(17)
             */
            BOOLEAN HasErrorCode = (Vector == 8 || Vector == 10 || Vector == 11 ||
                                    Vector == 12 || Vector == 13 || Vector == 14 || Vector == 17);
            ULONG64 EntryInfo = (1ULL << 31) | (3ULL << 8) | Vector; /* valid + hw exception + vector */

            ROSV_ERR("  Reinjecting exception #%u at RIP=0x%016llX error_code=0x%llX",
                     Vector, GuestRip, ErrorCode);

            if (HasErrorCode)
            {
                EntryInfo |= (1ULL << 11); /* deliver error code */
                RosvVmcsWrite(VMCS_CTRL_ENTRY_EXCEPTION_ERRCODE, ErrorCode);
            }
            RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO, EntryInfo);
            return TRUE;
        }
    }
}

/* ---- EPT violation handler ---------------------------------------------- */

static
BOOLEAN
RosvHandleEptViolation(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm)
{
    ULONG64 Qual = RosvVmcsRead(VMCS_RO_EXIT_QUALIFICATION);
    ULONG64 GuestPhysAddr = RosvVmcsRead(VMCS_RO_GUEST_PHYS_ADDR);

    return RosvEptHandleViolation(Vcpu, Vm, GuestPhysAddr, Qual);
}

/* ---- EPT misconfiguration handler --------------------------------------- */

static
BOOLEAN
RosvHandleEptMisconfig(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm)
{
    ULONG64 GuestPhysAddr = RosvVmcsRead(VMCS_RO_GUEST_PHYS_ADDR);
    ULONG64 GuestRip = RosvVmcsRead(VMCS_GUEST_RIP);

    ROSV_ERR("VM-exit EPT_MISCONFIG: GPA=0x%016llX RIP=0x%016llX (BUG in EPT setup!)",
             GuestPhysAddr, GuestRip);

    return RosvEptHandleMisconfiguration(Vcpu, Vm, GuestPhysAddr);
}

/* ---- Main VM-exit dispatcher -------------------------------------------- */

BOOLEAN
RosvVmExitDispatch(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ ULONG ExitReason,
    _In_ ULONG64 ExitQualification,
    _In_ ULONG64 GuestRip,
    _In_ ULONG InstructionLength)
{
    PROSV_VM Vm;
    ULONG InstrLength = InstructionLength;

    ROSV_ASSERT(Vcpu != NULL, "VM-exit dispatch requires a vCPU context");
    if (Vcpu == NULL)
        return FALSE;

    /*
     * Derive VM from the vCPU owner instead of trusting a caller-supplied pointer.
     * This keeps VM-exit handling coherent even if a stack-local VM argument gets stale.
     */
    Vm = Vcpu->Vm;
    ROSV_ASSERT(Vm != NULL, "vCPU has no parent VM");
    if (Vm == NULL)
        return FALSE;

    /*
     * Exit info (reason, qualification, RIP, instruction length) is already
     * read by the vcpu.c run loop and passed in as parameters -- no redundant
     * VMCS reads here.
     */

    /*
     * IDT vectoring re-injection: if a VM-exit occurred while the CPU was
     * delivering an interrupt or exception through the IDT, the interrupted
     * event is captured in VMCS_RO_IDT_VECTOR_INFO. We must re-inject it
     * so the event gets delivered on the next VM-entry.
     *
     * Exception: do NOT re-inject if the exit reason is EXCEPTION_NMI (0),
     * because the exit itself was caused by the event delivery (e.g. a #PF
     * during exception delivery). The exception handler already deals with
     * the vectoring event in that case.
     */
    {
        ULONG64 IdtVectoringInfo = RosvVmcsRead(VMCS_RO_IDT_VECTOR_INFO);
        if ((IdtVectoringInfo & (1ULL << 31)) && ExitReason != VMX_EXIT_EXCEPTION_NMI)
        {
            ULONG64 EntryInfo;
            ULONG IdtType = (ULONG)((IdtVectoringInfo >> 8) & 0x7);

            /* If the interrupted event has an error code, copy it */
            if (IdtVectoringInfo & (1ULL << 11))
            {
                ULONG64 IdtErrCode = RosvVmcsRead(VMCS_RO_IDT_VECTOR_ERRCODE);
                RosvVmcsWrite(VMCS_CTRL_ENTRY_EXCEPTION_ERRCODE, IdtErrCode);
            }

            /* For software interrupts/exceptions (types 4, 5, 6), set instruction length */
            if (IdtType == 4 || IdtType == 5 || IdtType == 6)
            {
                ULONG64 IdtInsnLen = RosvVmcsRead(VMCS_RO_EXIT_INSTR_LENGTH);
                RosvVmcsWrite(VMCS_CTRL_ENTRY_INSTR_LENGTH, IdtInsnLen);
            }

            /* Copy IDT vectoring info to VM-entry interrupt info.
             * Keep bits [11:0] (vector + type + error code valid),
             * set bit 31 (valid). Bit 12 is undefined in IDT vectoring info. */
            EntryInfo = (IdtVectoringInfo & 0xFFF) | (1ULL << 31);
            RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO, EntryInfo);

            ROSV_TRACE("IDT vectoring re-inject: info=0x%08llX -> entry=0x%08llX (exit_reason=%u)",
                       IdtVectoringInfo, EntryInfo, ExitReason);
        }
    }

    /*
     * Fast-path host external interrupts: they can arrive at high rates
     * (for example mouse movement). Keep this path minimal to avoid guest
     * starvation under interrupt bursts.
     */
    if (ExitReason == VMX_EXIT_EXTERNAL_INT)
    {
        RosvTryInjectPendingInterrupts(Vcpu, Vm);
        return TRUE;
    }

    /* Ring buffer recording done in vcpu.c run loop */

    /*
     * Pump all vcon-bound PTYs bidirectionally on every VM-exit.
     * This keeps data flowing between vcon ports and their bound PTYs
     * as the guest produces output and consumes input.
     */
    if (Vm != NULL)
    {
        RosvPtyPumpAllVconBound(&Vm->PtyManager);
    }

    /* Log first exit as a milestone, then gate verbose per-exit tracing */
    if (Vcpu->ExitCount == 1)
    {
        ROSV_TRACE("VM-EXIT #1: reason=%u qual=0x%016llX RIP=0x%016llX len=%u",
                   ExitReason, ExitQualification, GuestRip, InstrLength);
    }
    else
    {
        DPRINT("VM-EXIT #%llu: reason=%u qual=0x%016llX RIP=0x%016llX len=%u\n",
               Vcpu->ExitCount, ExitReason, ExitQualification,
               GuestRip, InstrLength);
    }

    switch (ExitReason)
    {
        case VMX_EXIT_EXCEPTION_NMI:
            return RosvHandleException(Vcpu, Vm);

        case VMX_EXIT_INT_WINDOW:
        {
            RosvTryInjectPendingInterrupts(Vcpu, Vm);
            return TRUE;
        }

        case VMX_EXIT_PREEMPT_TIMER:
        {
            static ULONG PreemptTimerExitCount;
            PreemptTimerExitCount++;
            if (PreemptTimerExitCount < 4 || (PreemptTimerExitCount % 65536) == 0)
            {
                ROSV_DEBUG("VM-exit PREEMPT_TIMER #%u rip=0x%llX rflags=0x%llX vmx_preempt_timer=%llu",
                           PreemptTimerExitCount,
                           GuestRip,
                           RosvVmcsRead(VMCS_GUEST_RFLAGS),
                           RosvVmcsRead(VMCS_GUEST_PREEMPT_TIMER));
            }
            /* Try to deliver pending emulated interrupts, including queued timer ticks. */
            RosvTryInjectPendingInterrupts(Vcpu, Vm);
            return TRUE;
        }

        case VMX_EXIT_TRIPLE_FAULT:
            ROSV_ERR("VM-exit TRIPLE FAULT at RIP=0x%016llX", GuestRip);
            ROSV_ERR("  Exit count = %llu", Vcpu->ExitCount);
            RosvVmxDumpVmcs();
            return FALSE;

        case VMX_EXIT_CPUID:
            return RosvHandleCpuid(Vcpu);

        case VMX_EXIT_HLT:
        {
            static ULONG HltExitCount;
            HltExitCount++;
            if (HltExitCount < 4 || (HltExitCount % 262144) == 0)
            {
                ROSV_DEBUG("VM-exit HLT #%u rip=0x%llX rflags=0x%llX",
                           HltExitCount,
                           GuestRip,
                           RosvVmcsRead(VMCS_GUEST_RFLAGS));
            }
            return RosvHandleHlt(Vcpu, Vm);
        }

        case VMX_EXIT_INVD:
            /* INVD - just advance RIP (cache invalidation is harmless under EPT) */
            RosvAdvanceGuestRip(Vcpu);
            return TRUE;

        case VMX_EXIT_CR_ACCESS:
            return RosvHandleCrAccess(Vcpu);

        case VMX_EXIT_DR_ACCESS:
        {
            ULONG64 Qual = RosvVmcsRead(VMCS_RO_EXIT_QUALIFICATION);
            ULONG DrNum = (ULONG)(Qual & 0x7);
            BOOLEAN IsRead = (BOOLEAN)((Qual >> 4) & 1);
            ULONG RegIdx = (ULONG)((Qual >> 8) & 0xF);

            if (IsRead)
            {
                /* Return safe default: DR6=0xFFFF0FF0, DR7=0x400, others=0 */
                ULONG64 Val = (DrNum == 6) ? 0xFFFF0FF0ULL :
                              (DrNum == 7) ? 0x400ULL : 0ULL;
                RosvSetGuestRegByIndex(&Vcpu->GuestRegs, RegIdx, Val);
            }
            /* For writes: discard (no-op) */

            RosvAdvanceGuestRip(Vcpu);
            return TRUE;
        }

        case VMX_EXIT_IO:
            if (!RosvHandleIo(Vcpu, Vm))
                return FALSE;
            /*
             * IO exits include UART register writes (IER, THR).
             * The UART may now have a pending interrupt (e.g. THR-empty
             * after enabling THRI). Inject it immediately so the 8250
             * driver's interrupt-driven TX path isn't starved by the
             * host-wall-clock timer that runs later in the vCPU loop.
             */
            RosvTryInjectPendingInterrupts(Vcpu, Vm);
            return TRUE;

        case VMX_EXIT_RDMSR:
            return RosvHandleRdmsr(Vcpu);

        case VMX_EXIT_WRMSR:
            return RosvHandleWrmsr(Vcpu);

        case VMX_EXIT_ENTRY_FAIL_GUEST:
            ROSV_ERR("VM-exit ENTRY FAIL (guest state): RIP=0x%016llX", GuestRip);
            RosvVmxDumpVmcs();
            RosvVmxDumpFailure();
            return FALSE;

        case VMX_EXIT_ENTRY_FAIL_MSR:
            ROSV_ERR("VM-exit ENTRY FAIL (MSR loading): RIP=0x%016llX", GuestRip);
            RosvVmxDumpVmcs();
            RosvVmxDumpFailure();
            return FALSE;

        case VMX_EXIT_EPT_VIOLATION:
            return RosvHandleEptViolation(Vcpu, Vm);

        case VMX_EXIT_EPT_MISCONFIG:
            return RosvHandleEptMisconfig(Vcpu, Vm);

        case VMX_EXIT_RDTSC:
        case VMX_EXIT_RDTSCP:
        {
            /* Pass through host TSC */
            ULONG64 Tsc = __rdtsc();
            Vcpu->GuestRegs.Rax = Tsc & 0xFFFFFFFF;
            Vcpu->GuestRegs.Rdx = Tsc >> 32;
            if (ExitReason == VMX_EXIT_RDTSCP)
                Vcpu->GuestRegs.Rcx = (ULONG64)(ULONG)(Vcpu->GuestTscAux & 0xFFFFFFFFULL);
            RosvAdvanceGuestRip(Vcpu);
            return TRUE;
        }

        case VMX_EXIT_XSETBV:
        {
            ULONG Xcr = (ULONG)Vcpu->GuestRegs.Rcx;
            ULONG64 Value = (Vcpu->GuestRegs.Rdx << 32) | (Vcpu->GuestRegs.Rax & 0xFFFFFFFF);
            ROSV_TRACE("VM-exit XSETBV: XCR%u = 0x%016llX", Xcr, Value);

            if (Xcr != 0)
            {
                ROSV_ERR("XSETBV: unsupported XCR%u, injecting #GP", Xcr);
                RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO,
                              (1ULL << 31) | (1ULL << 11) | (3ULL << 8) | 13);
                RosvVmcsWrite(VMCS_CTRL_ENTRY_EXCEPTION_ERRCODE, 0);
                return TRUE;
            }

            /* XCR0 bit 0 (x87) must always be set */
            if (!(Value & 1))
            {
                ROSV_ERR("XSETBV: XCR0 bit 0 (x87) not set (0x%llX), injecting #GP",
                         (unsigned long long)Value);
                RosvVmcsWrite(VMCS_CTRL_ENTRY_INT_INFO,
                              (1ULL << 31) | (1ULL << 11) | (3ULL << 8) | 13);
                RosvVmcsWrite(VMCS_CTRL_ENTRY_EXCEPTION_ERRCODE, 0);
                return TRUE;
            }

            /* Only allow x87 and SSE (bits 0-1), block AVX/etc since we hide XSAVE from guest */
            if (Value & ~0x3ULL)
            {
                ROSV_WARN("XSETBV: XCR0 has unsupported bits 0x%llX, masking to x87+SSE",
                          (unsigned long long)Value);
                Value &= 0x3ULL;
            }

            {
                ULONG Lo = (ULONG)(Value & 0xFFFFFFFF);
                ULONG Hi = (ULONG)(Value >> 32);
                __asm__ __volatile__("xsetbv" :: "c"(0), "a"(Lo), "d"(Hi));
            }

            RosvAdvanceGuestRip(Vcpu);
            return TRUE;
        }

        case VMX_EXIT_WBINVD:
            /* WBINVD - harmless, just advance RIP */
            RosvAdvanceGuestRip(Vcpu);
            return TRUE;

        case VMX_EXIT_MONITOR:
        case VMX_EXIT_MWAIT:
            /* MONITOR/MWAIT are not virtualized; treat as no-op in guest context. */
            RosvAdvanceGuestRip(Vcpu);
            return TRUE;

        case VMX_EXIT_PAUSE:
            /* PAUSE - just advance RIP */
            RosvAdvanceGuestRip(Vcpu);
            return TRUE;

        default:
            ROSV_ERR("VM-exit UNHANDLED: reason=%u (0x%X) qual=0x%016llX RIP=0x%016llX",
                     ExitReason, ExitReason, ExitQualification, GuestRip);
            RosvVmxDumpExitReason(ExitReason, ExitQualification);
            return FALSE;
    }
}
