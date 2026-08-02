/*
 * PROJECT:     ReactOS HAL — ARM64 BCM2837 (Raspberry Pi 3) interrupt backend
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Interrupt-chip backend for the GIC-less BCM2837 SoC.
 *
 *              The Cortex-A53 cluster in the BCM2837 is synthesized without
 *              a GIC (ICC_* system registers are UNDEFINED).  Interrupts
 *              come from the BCM2836 ARM-local block (generic-timer lines,
 *              four mailboxes per core, PMU) and from the cascaded BCM2835
 *              ARMCTRL controller (64 VideoCore/peripheral IRQs + 8 ARM
 *              "basic" IRQs) whose output is routed to one core.
 *
 *              Neither block has priority hardware, so this backend
 *              emulates the GIC priority-mask contract in software:
 *
 *                - HalEnableSystemInterrupt() stores a GIC-style priority
 *                  byte per source (lower value = more urgent).
 *                - SetPriorityMask() keeps a per-core software PMR shadow.
 *                - AcknowledgeInterrupt() only delivers a pending source
 *                  whose priority is below the shadow; any pending source
 *                  at or above the shadow is masked at the controller and
 *                  marked deferred ("lazy IRQL"), then re-enabled when the
 *                  IRQL drops far enough.  Sources are level-triggered, so
 *                  a re-enabled line re-asserts by itself.
 *
 *              SGIs 0-3 (IPI/APC/DPC) map onto the per-core mailboxes and
 *              the generic-timer PPIs (INTID 26/27/29/30) onto the local
 *              core-timer rows, so the kernel's GIC-shaped expectations
 *              hold unchanged.
 *
 *              Clean-room implementation from the public Broadcom
 *              datasheets ("BCM2835 ARM Peripherals" §7, "BCM2836
 *              ARM-local peripherals / QA7") and from observation of the
 *              ACPI tables shipped by the Raspberry Pi 3 UEFI firmware.
 *
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <reactos/drivers/acpi/acpi.h>
#include <halacpi.h>
#include "halext.h"
#include "bcm2837.h"

#define NDEBUG
#include <debug.h>

/* ================================================================== */
/*  State                                                             */
/* ================================================================== */

BOOLEAN Bcm2837Detected = FALSE;

/* Physical addresses during Phase 0/identity, kernel VAs after Phase 1. */
static ULONGLONG Bcm2837LocalBase;
static ULONGLONG Bcm2837ArmctrlBase;

/* ARMCTRL output routing target (GPU_INT_ROUTING), always core 0. */
#define BCM2837_GPU_ROUTING_CORE    0

#define BCM2837_DEFAULT_PRIORITY    0xA0
#define BCM2837_SPURIOUS_INTID      1023

/*
 * Per-core software PMR shadow and deferred-source bookkeeping.
 * Indexed by MPIDR.Aff0 (0-3).  DeferredLocal mirrors the IRQ_SOURCE
 * bit layout (bits 0-3 timer rows, bits 4-7 mailboxes).
 */
static volatile UCHAR Bcm2837PmrShadow[BCM2837_CORE_COUNT];
static volatile ULONG Bcm2837DeferredLocal[BCM2837_CORE_COUNT];

/*
 * ARMCTRL bookkeeping.  Only the routing core ever defers/reenables
 * these, but enable/disable can run on any core, so use interlocked ops.
 */
static volatile LONG Bcm2837GpuEnabled[2];
static volatile LONG Bcm2837DeferredGpu[2];
static volatile LONG Bcm2837BasicEnabled;
static volatile LONG Bcm2837DeferredBasic;

/* GIC-style priority byte per source (lower = more urgent). */
static UCHAR Bcm2837SgiPriority[BCM2837_SGI_COUNT] =
{
    HAL_ARM64_SGI_IPI_PRIORITY,
    HAL_ARM64_SGI_APC_PRIORITY,
    HAL_ARM64_SGI_DPC_PRIORITY,
    HAL_ARM64_SGI_FREEZE_PRIORITY
};
static UCHAR Bcm2837GpuPriority[BCM2837_GPU_IRQ_COUNT];
static UCHAR Bcm2837BasicPriority[BCM2837_BASIC_IRQ_COUNT];
static UCHAR Bcm2837TimerRowPriority[4];

/* Logical processor number -> MPIDR.Aff0 core number. */
static UCHAR Bcm2837CoreForCpu[MAXIMUM_PROCESSORS];

/*
 * IRQL -> software PMR threshold.  Must match HalpArm64IrqlToPmr in
 * halarm64.c: a source is deliverable iff priority < threshold.
 */
static const UCHAR Bcm2837IrqlToPmr[HIGH_LEVEL + 1] =
{
    0xFF, 0xE0, 0xD0,               /* PASSIVE/APC/DISPATCH */
    0xC0, 0xB0, 0xA0, 0x90, 0x80,   /* device IRQLs 3-7 */
    0x70, 0x60, 0x50, 0x40, 0x30,   /* device IRQLs 8-12 */
    0x20,                           /* CLOCK_LEVEL */
    0x10,                           /* IPI_LEVEL */
    0x00                            /* HIGH_LEVEL */
};

/* Local core-timer row <-> generic-timer PPI INTID. */
static const UCHAR Bcm2837RowToIntId[4] = { 29, 30, 26, 27 };

/* ================================================================== */
/*  Low-level helpers                                                 */
/* ================================================================== */

FORCEINLINE
ULONG
Bcm2837CurrentCore(VOID)
{
    ULONGLONG Mpidr;
    __asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(Mpidr));
    return (ULONG)(Mpidr & (BCM2837_CORE_COUNT - 1));
}

FORCEINLINE
ULONGLONG
Bcm2837MaskInterrupts(VOID)
{
    ULONGLONG Daif;
    __asm__ __volatile__("mrs %0, daif" : "=r"(Daif));
    __asm__ __volatile__("msr daifset, #3" ::: "memory");
    return Daif;
}

FORCEINLINE
VOID
Bcm2837RestoreInterrupts(
    _In_ ULONGLONG Daif)
{
    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
}

FORCEINLINE
ULONG
Bcm2837LocalRead(
    _In_ ULONG Offset)
{
    return *HalpMmio((ULONG_PTR)Bcm2837LocalBase, Offset);
}

FORCEINLINE
VOID
Bcm2837LocalWrite(
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    *HalpMmio((ULONG_PTR)Bcm2837LocalBase, Offset) = Value;
    __asm__ __volatile__("dsb sy" ::: "memory");
}

FORCEINLINE
ULONG
Bcm2837ArmctrlRead(
    _In_ ULONG Offset)
{
    return *HalpMmio((ULONG_PTR)Bcm2837ArmctrlBase, Offset);
}

FORCEINLINE
VOID
Bcm2837ArmctrlWrite(
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    *HalpMmio((ULONG_PTR)Bcm2837ArmctrlBase, Offset) = Value;
    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * The EL1-accessible generic timers keep asserting their level output
 * until reprogrammed.  While one is being serviced, mask its output with
 * CNT*_CTL.IMASK instead of bouncing the local routing row; EndInterrupt
 * lifts the mask again.
 */
#define BCM2837_CNT_CTL_ENABLE  0x1ULL
#define BCM2837_CNT_CTL_IMASK   0x2ULL

static
VOID
Bcm2837SetTimerImask(
    _In_ ULONG IntId,
    _In_ BOOLEAN Mask)
{
    ULONGLONG Ctl;

    if (IntId == 27)
    {
        __asm__ __volatile__("mrs %0, cntv_ctl_el0" : "=r"(Ctl));
        if (!(Ctl & BCM2837_CNT_CTL_ENABLE))
            return;
        Ctl = Mask ? (Ctl | BCM2837_CNT_CTL_IMASK) : (Ctl & ~BCM2837_CNT_CTL_IMASK);
        __asm__ __volatile__("msr cntv_ctl_el0, %0" :: "r"(Ctl));
    }
    else if (IntId == 30)
    {
        __asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(Ctl));
        if (!(Ctl & BCM2837_CNT_CTL_ENABLE))
            return;
        Ctl = Mask ? (Ctl | BCM2837_CNT_CTL_IMASK) : (Ctl & ~BCM2837_CNT_CTL_IMASK);
        __asm__ __volatile__("msr cntp_ctl_el0, %0" :: "r"(Ctl));
    }
    __asm__ __volatile__("isb" ::: "memory");
}

static
LONG
Bcm2837IntIdToTimerRow(
    _In_ ULONG IntId)
{
    switch (IntId)
    {
        case 29: return BCM2837_SRC_CNTPS;
        case 30: return BCM2837_SRC_CNTPNS;
        case 26: return BCM2837_SRC_CNTHP;
        case 27: return BCM2837_SRC_CNTV;
        default: return -1;
    }
}

/* ================================================================== */
/*  Interrupt-chip operations                                         */
/* ================================================================== */

static
ULONG
Bcm2837AcknowledgeInterrupt(VOID)
{
    ULONG Core = Bcm2837CurrentCore();
    UCHAR Pmr = Bcm2837PmrShadow[Core];
    UCHAR BestPriority = Pmr;
    ULONG BestIntId = BCM2837_SPURIOUS_INTID;
    ULONG Source;
    ULONG Row;
    ULONG Mailbox;

    Source = Bcm2837LocalRead(BCM2837_LOCAL_IRQ_SOURCE(Core));
    if (Source == 0)
        return BCM2837_SPURIOUS_INTID;

    /* Find the highest-priority eligible source across every pending class. */
    for (Mailbox = 0; Mailbox < BCM2837_SGI_COUNT; Mailbox++)
    {
        UCHAR Priority;

        if (!(Source & (1u << (BCM2837_SRC_MAILBOX0 + Mailbox))))
            continue;

        Priority = Bcm2837SgiPriority[Mailbox];
        if (Priority < Pmr)
        {
            if (Priority < BestPriority ||
                (Priority == BestPriority && Mailbox < BestIntId))
            {
                BestPriority = Priority;
                BestIntId = Mailbox;
            }
            continue;
        }

        /* Defer: gate the mailbox until the IRQL drops. */
        Bcm2837LocalWrite(BCM2837_LOCAL_MAILBOX_INT_CONTROL(Core),
                          Bcm2837LocalRead(BCM2837_LOCAL_MAILBOX_INT_CONTROL(Core)) &
                          ~(1u << Mailbox));
        Bcm2837DeferredLocal[Core] |= 1u << (BCM2837_SRC_MAILBOX0 + Mailbox);
    }

    /* Generic-timer rows (only rows we enabled can be pending). */
    for (Row = 0; Row < 4; Row++)
    {
        ULONG IntId;
        UCHAR Priority;

        if (!(Source & (1u << Row)))
            continue;

        IntId = Bcm2837RowToIntId[Row];
        Priority = Bcm2837TimerRowPriority[Row];
        if (Priority < Pmr)
        {
            if (Priority < BestPriority ||
                (Priority == BestPriority && IntId < BestIntId))
            {
                BestPriority = Priority;
                BestIntId = IntId;
            }
            continue;
        }

        Bcm2837LocalWrite(BCM2837_LOCAL_TIMER_INT_CONTROL(Core),
                          Bcm2837LocalRead(BCM2837_LOCAL_TIMER_INT_CONTROL(Core)) &
                          ~(1u << Row));
        Bcm2837DeferredLocal[Core] |= 1u << Row;
    }

    /* Cascaded ARMCTRL output (routed to core 0 only). */
    if (Source & (1u << BCM2837_SRC_GPU))
    {
        ULONG Basic = Bcm2837ArmctrlRead(BCM2837_ARMCTRL_PENDING_BASIC);
        ULONG Bank;

        for (Bank = 0; Bank < 2; Bank++)
        {
            ULONG Pending;
            ULONG Bit;

            /*
             * Always read the full bank: the shortcut-flagged GPU IRQs
             * (7, 9, 10, 18, 19, 53-57, 62 — including SDHCI and USB) do
             * not set the bank summary bits in PENDING_BASIC, only their
             * own bit here.
             */
            Pending = Bcm2837ArmctrlRead(Bank ? BCM2837_ARMCTRL_PENDING_2
                                              : BCM2837_ARMCTRL_PENDING_1);
            while (Pending != 0)
            {
                ULONG Irq;
                UCHAR Priority;

                Bit = Pending & (0 - Pending);     /* lowest set bit */
                Pending &= ~Bit;
                Irq = (Bank * 32) + (31 - __builtin_clz(Bit));
                Priority = Bcm2837GpuPriority[Irq];

                if (Priority < Pmr)
                {
                    ULONG IntId = BCM2837_INTID_GPU_BASE + Irq;

                    if (Priority < BestPriority ||
                        (Priority == BestPriority && IntId < BestIntId))
                    {
                        BestPriority = Priority;
                        BestIntId = IntId;
                    }
                }
                else
                {
                    /* Defer: too low for the current IRQL. */
                    Bcm2837ArmctrlWrite(Bank ? BCM2837_ARMCTRL_DISABLE_2
                                             : BCM2837_ARMCTRL_DISABLE_1,
                                        Bit);
                    InterlockedOr(&Bcm2837DeferredGpu[Bank], (LONG)Bit);
                }
            }
        }

        {
            ULONG Pending = Basic & BCM2837_BASIC_IRQ_MASK;
            while (Pending != 0)
            {
                ULONG Bit = Pending & (0 - Pending);
                ULONG Irq = 31 - __builtin_clz(Bit);
                UCHAR Priority;

                Pending &= ~Bit;
                Priority = Bcm2837BasicPriority[Irq];

                if (Priority < Pmr)
                {
                    ULONG IntId = BCM2837_INTID_BASIC_BASE + Irq;

                    if (Priority < BestPriority ||
                        (Priority == BestPriority && IntId < BestIntId))
                    {
                        BestPriority = Priority;
                        BestIntId = IntId;
                    }
                }
                else
                {
                    Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_DISABLE_BASIC, Bit);
                    InterlockedOr(&Bcm2837DeferredBasic, (LONG)Bit);
                }
            }
        }
    }

    if (BestIntId != BCM2837_SPURIOUS_INTID)
    {
        if (BestIntId < BCM2837_SGI_COUNT)
        {
            ULONG Value = Bcm2837LocalRead(BCM2837_LOCAL_MAILBOX_RDCLR(Core,
                                                                       BestIntId));
            Bcm2837LocalWrite(BCM2837_LOCAL_MAILBOX_RDCLR(Core, BestIntId),
                              Value);
        }
        else if (Bcm2837IntIdToTimerRow(BestIntId) >= 0)
        {
            /* Silence the level output for the duration of the ISR. */
            Bcm2837SetTimerImask(BestIntId, TRUE);
        }
        else if (BestIntId >= BCM2837_INTID_BASIC_BASE)
        {
            ULONG Bit = 1u << (BestIntId - BCM2837_INTID_BASIC_BASE);

            Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_DISABLE_BASIC, Bit);
            InterlockedOr(&Bcm2837DeferredBasic, (LONG)Bit);
        }
        else
        {
            ULONG Irq = BestIntId - BCM2837_INTID_GPU_BASE;
            ULONG Bit = 1u << (Irq % 32);

            Bcm2837ArmctrlWrite((Irq / 32) ? BCM2837_ARMCTRL_DISABLE_2
                                           : BCM2837_ARMCTRL_DISABLE_1,
                                Bit);
            InterlockedOr(&Bcm2837DeferredGpu[Irq / 32], (LONG)Bit);
        }

        return BestIntId;
    }

    /* Stray QA7 sources: quiesce them so they cannot storm. */
    if (Source & (1u << BCM2837_SRC_PMU))
    {
        Bcm2837LocalWrite(BCM2837_LOCAL_PMU_INT_ROUTING_CLR, 0xF);
    }
    if (Source & (1u << BCM2837_SRC_LOCAL_TIMER))
    {
        Bcm2837LocalWrite(BCM2837_LOCAL_TIMER_CTRL_STATUS, 0);
        Bcm2837LocalWrite(BCM2837_LOCAL_TIMER_WRITE_FLAGS, 1u << 31);
    }
    if (Source & (1u << BCM2837_SRC_AXI))
    {
        Bcm2837LocalWrite(BCM2837_LOCAL_AXI_IRQ_CONTROL, 0);
    }

    return BCM2837_SPURIOUS_INTID;
}

static
VOID
Bcm2837EndInterrupt(
    _In_ ULONG IntId)
{
    /*
     * Level sources need no EOI: device ISRs deassert their own lines and
     * deferred sources are reopened by SetPriorityMask.  Only the timers
     * carry an ack-time IMASK that must be lifted again.
     */
    if (IntId == 27 || IntId == 30)
    {
        Bcm2837SetTimerImask(IntId, FALSE);
    }
}

static
VOID
Bcm2837EnableInterrupt(
    _In_ ULONG IntId)
{
    ULONGLONG Daif;
    ULONG Core;
    LONG Row;

    if (IntId < 16)
    {
        /* SGIs: the per-core mailboxes are opened in per-CPU init. */
        return;
    }

    Row = Bcm2837IntIdToTimerRow(IntId);
    if (Row >= 0)
    {
        /* Per-CPU: enable the row on the calling core (PPI semantics). */
        Daif = Bcm2837MaskInterrupts();
        Core = Bcm2837CurrentCore();
        Bcm2837DeferredLocal[Core] &= ~(1u << Row);
        Bcm2837LocalWrite(BCM2837_LOCAL_TIMER_INT_CONTROL(Core),
                          Bcm2837LocalRead(BCM2837_LOCAL_TIMER_INT_CONTROL(Core)) |
                          (1u << Row));
        Bcm2837SetTimerImask(IntId, FALSE);
        Bcm2837RestoreInterrupts(Daif);
        return;
    }

    if (IntId >= BCM2837_INTID_GPU_BASE &&
        IntId < BCM2837_INTID_GPU_BASE + BCM2837_GPU_IRQ_COUNT)
    {
        ULONG Irq = IntId - BCM2837_INTID_GPU_BASE;
        ULONG Bank = Irq / 32;
        ULONG Bit = 1u << (Irq % 32);

        InterlockedOr(&Bcm2837GpuEnabled[Bank], (LONG)Bit);
        InterlockedAnd(&Bcm2837DeferredGpu[Bank], (LONG)~Bit);
        Bcm2837ArmctrlWrite(Bank ? BCM2837_ARMCTRL_ENABLE_2
                                 : BCM2837_ARMCTRL_ENABLE_1,
                            Bit);
        return;
    }

    if (IntId >= BCM2837_INTID_BASIC_BASE && IntId < BCM2837_INTID_BASIC_LIMIT)
    {
        ULONG Bit = 1u << (IntId - BCM2837_INTID_BASIC_BASE);

        InterlockedOr(&Bcm2837BasicEnabled, (LONG)Bit);
        InterlockedAnd(&Bcm2837DeferredBasic, (LONG)~Bit);
        Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_ENABLE_BASIC, Bit);
        return;
    }

    DPRINT1("[BCM2837] EnableInterrupt: INTID %lu out of range\n", IntId);
}

static
VOID
Bcm2837DisableInterrupt(
    _In_ ULONG IntId)
{
    ULONGLONG Daif;
    ULONG Core;
    LONG Row;

    if (IntId < 16)
        return;

    Row = Bcm2837IntIdToTimerRow(IntId);
    if (Row >= 0)
    {
        Daif = Bcm2837MaskInterrupts();
        Core = Bcm2837CurrentCore();
        Bcm2837DeferredLocal[Core] &= ~(1u << Row);
        Bcm2837LocalWrite(BCM2837_LOCAL_TIMER_INT_CONTROL(Core),
                          Bcm2837LocalRead(BCM2837_LOCAL_TIMER_INT_CONTROL(Core)) &
                          ~(1u << Row));
        Bcm2837RestoreInterrupts(Daif);
        return;
    }

    if (IntId >= BCM2837_INTID_GPU_BASE &&
        IntId < BCM2837_INTID_GPU_BASE + BCM2837_GPU_IRQ_COUNT)
    {
        ULONG Irq = IntId - BCM2837_INTID_GPU_BASE;
        ULONG Bank = Irq / 32;
        ULONG Bit = 1u << (Irq % 32);

        InterlockedAnd(&Bcm2837GpuEnabled[Bank], (LONG)~Bit);
        InterlockedAnd(&Bcm2837DeferredGpu[Bank], (LONG)~Bit);
        Bcm2837ArmctrlWrite(Bank ? BCM2837_ARMCTRL_DISABLE_2
                                 : BCM2837_ARMCTRL_DISABLE_1,
                            Bit);
        return;
    }

    if (IntId >= BCM2837_INTID_BASIC_BASE && IntId < BCM2837_INTID_BASIC_LIMIT)
    {
        ULONG Bit = 1u << (IntId - BCM2837_INTID_BASIC_BASE);

        InterlockedAnd(&Bcm2837BasicEnabled, (LONG)~Bit);
        InterlockedAnd(&Bcm2837DeferredBasic, (LONG)~Bit);
        Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_DISABLE_BASIC, Bit);
        return;
    }
}

static
VOID
Bcm2837SetInterruptPriority(
    _In_ ULONG IntId,
    _In_ ULONG Priority)
{
    LONG Row;

    if (IntId < BCM2837_SGI_COUNT)
    {
        Bcm2837SgiPriority[IntId] = (UCHAR)Priority;
        return;
    }

    Row = Bcm2837IntIdToTimerRow(IntId);
    if (Row >= 0)
    {
        Bcm2837TimerRowPriority[Row] = (UCHAR)Priority;
        return;
    }

    if (IntId >= BCM2837_INTID_GPU_BASE &&
        IntId < BCM2837_INTID_GPU_BASE + BCM2837_GPU_IRQ_COUNT)
    {
        Bcm2837GpuPriority[IntId - BCM2837_INTID_GPU_BASE] = (UCHAR)Priority;
        return;
    }

    if (IntId >= BCM2837_INTID_BASIC_BASE && IntId < BCM2837_INTID_BASIC_LIMIT)
    {
        Bcm2837BasicPriority[IntId - BCM2837_INTID_BASIC_BASE] = (UCHAR)Priority;
        return;
    }

}

static
VOID
Bcm2837SetTriggerMode(
    _In_ ULONG IntId,
    _In_ BOOLEAN EdgeTriggered)
{
    /* Everything behind the ARMCTRL and the local block is level-only. */
    UNREFERENCED_PARAMETER(IntId);
    UNREFERENCED_PARAMETER(EdgeTriggered);
}

static
VOID
Bcm2837SetPriorityMask(
    _In_ KIRQL Irql)
{
    UCHAR Pmr = Bcm2837IrqlToPmr[(Irql <= HIGH_LEVEL) ? Irql : HIGH_LEVEL];
    ULONGLONG Daif;
    ULONG Core;

    /*
     * Mask PSTATE.I for the whole body: KfLowerIrql calls this with
     * interrupts open, so a trap taken between reading MPIDR and the
     * updates below could migrate the thread and corrupt another core's
     * mask shadow and deferral state.
     */
    Daif = Bcm2837MaskInterrupts();
    Core = Bcm2837CurrentCore();

    Bcm2837PmrShadow[Core] = Pmr;
    __asm__ __volatile__("dsb sy" ::: "memory");

    /* Reopen deferred per-core sources that the new IRQL permits. */
    if (Bcm2837DeferredLocal[Core] != 0)
    {
        ULONG Deferred = Bcm2837DeferredLocal[Core];
        ULONG MailboxEnable = 0;
        ULONG Row;
        ULONG Mailbox;

        for (Row = 0; Row < 4; Row++)
        {
            if ((Deferred & (1u << Row)) && Bcm2837TimerRowPriority[Row] < Pmr)
            {
                Bcm2837DeferredLocal[Core] &= ~(1u << Row);
                Bcm2837LocalWrite(BCM2837_LOCAL_TIMER_INT_CONTROL(Core),
                                  Bcm2837LocalRead(BCM2837_LOCAL_TIMER_INT_CONTROL(Core)) |
                                  (1u << Row));
            }
        }

        for (Mailbox = 0; Mailbox < BCM2837_SGI_COUNT; Mailbox++)
        {
            if ((Deferred & (1u << (BCM2837_SRC_MAILBOX0 + Mailbox))) &&
                Bcm2837SgiPriority[Mailbox] < Pmr)
            {
                Bcm2837DeferredLocal[Core] &= ~(1u << (BCM2837_SRC_MAILBOX0 + Mailbox));
                MailboxEnable |= (1u << Mailbox);
            }
        }

        if (MailboxEnable != 0)
        {
            Bcm2837LocalWrite(BCM2837_LOCAL_MAILBOX_INT_CONTROL(Core),
                              Bcm2837LocalRead(BCM2837_LOCAL_MAILBOX_INT_CONTROL(Core)) |
                              MailboxEnable);
        }
    }

    /* ARMCTRL deferrals only ever exist on the routing core. */
    if (Core == BCM2837_GPU_ROUTING_CORE &&
        (Bcm2837DeferredGpu[0] | Bcm2837DeferredGpu[1] | Bcm2837DeferredBasic) != 0)
    {
        ULONG Bank;

        for (Bank = 0; Bank < 2; Bank++)
        {
            ULONG Pending = (ULONG)Bcm2837DeferredGpu[Bank];

            while (Pending != 0)
            {
                ULONG Bit = Pending & (0 - Pending);
                ULONG Irq = (Bank * 32) + (31 - __builtin_clz(Bit));

                Pending &= ~Bit;
                if (Bcm2837GpuPriority[Irq] >= Pmr)
                    continue;

                /*
                 * Clear the deferral even when the source got disabled
                 * meanwhile, or the bit would stay set forever and force
                 * this scan on every IRQL transition.
                 */
                InterlockedAnd(&Bcm2837DeferredGpu[Bank], (LONG)~Bit);
                if ((ULONG)Bcm2837GpuEnabled[Bank] & Bit)
                {
                    Bcm2837ArmctrlWrite(Bank ? BCM2837_ARMCTRL_ENABLE_2
                                             : BCM2837_ARMCTRL_ENABLE_1,
                                        Bit);

                    /* Back out if a concurrent disable lost the race. */
                    if (!((ULONG)Bcm2837GpuEnabled[Bank] & Bit))
                    {
                        Bcm2837ArmctrlWrite(Bank ? BCM2837_ARMCTRL_DISABLE_2
                                                 : BCM2837_ARMCTRL_DISABLE_1,
                                            Bit);
                    }
                }
            }
        }

        {
            ULONG Pending = (ULONG)Bcm2837DeferredBasic;

            while (Pending != 0)
            {
                ULONG Bit = Pending & (0 - Pending);
                ULONG Irq = 31 - __builtin_clz(Bit);

                Pending &= ~Bit;
                if (Bcm2837BasicPriority[Irq] >= Pmr)
                    continue;

                InterlockedAnd(&Bcm2837DeferredBasic, (LONG)~Bit);
                if ((ULONG)Bcm2837BasicEnabled & Bit)
                {
                    Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_ENABLE_BASIC, Bit);

                    if (!((ULONG)Bcm2837BasicEnabled & Bit))
                        Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_DISABLE_BASIC, Bit);
                }
            }
        }
    }

    Bcm2837RestoreInterrupts(Daif);
}

static
ULONG
Bcm2837GetPriorityMask(VOID)
{
    return Bcm2837PmrShadow[Bcm2837CurrentCore()];
}

static
VOID
Bcm2837SendSgi(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG SgiId)
{
    ULONG Cpu;

    if (SgiId >= BCM2837_SGI_COUNT)
        return;

    __asm__ __volatile__("dsb sy" ::: "memory");

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS && Cpu < 64; Cpu++)
    {
        ULONG Core;

        if (!(TargetSet & (1ULL << Cpu)))
            continue;

        Core = Bcm2837CoreForCpu[Cpu];
        if (Core >= BCM2837_CORE_COUNT)
            continue;

        Bcm2837LocalWrite(BCM2837_LOCAL_MAILBOX_SET(Core, SgiId), 1);
    }

    __asm__ __volatile__("sev" ::: "memory");
}

static
VOID
Bcm2837InitializeProcessor(
    _In_ ULONG ProcessorNumber)
{
    ULONG Core = Bcm2837CurrentCore();

    if (ProcessorNumber < MAXIMUM_PROCESSORS)
        Bcm2837CoreForCpu[ProcessorNumber] = (UCHAR)Core;

    /* Block everything until the kernel lowers this core's IRQL. */
    Bcm2837PmrShadow[Core] = 0x00;
    Bcm2837DeferredLocal[Core] = 0;

    /* Open the four mailboxes (SGI transport); IRQ only, no FIQ. */
    Bcm2837LocalWrite(BCM2837_LOCAL_MAILBOX_INT_CONTROL(Core), 0x0F);
}

static
BOOLEAN
Bcm2837Phase0Initialize(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    /*
     * Quiesce the ARMCTRL: firmware may have left sources enabled.
     * The local core-timer rows are deliberately left alone — the kernel
     * has already routed its clock PPI through EnableInterrupt by now
     * (and the priority tables were seeded at probe time for the same
     * reason).
     */
    Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_DISABLE_1, 0xFFFFFFFF);
    Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_DISABLE_2, 0xFFFFFFFF);
    Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_DISABLE_BASIC, 0xFF);
    Bcm2837ArmctrlWrite(BCM2837_ARMCTRL_FIQ_CONTROL, 0);

    Bcm2837GpuEnabled[0] = Bcm2837GpuEnabled[1] = 0;
    Bcm2837DeferredGpu[0] = Bcm2837DeferredGpu[1] = 0;
    Bcm2837BasicEnabled = 0;
    Bcm2837DeferredBasic = 0;

    /* Route the ARMCTRL output (IRQ and FIQ) to core 0. */
    Bcm2837LocalWrite(BCM2837_LOCAL_GPU_INT_ROUTING, BCM2837_GPU_ROUTING_CORE);

    /* Quiesce the unused QA7 sources: PMU, local timer, AXI counter. */
    Bcm2837LocalWrite(BCM2837_LOCAL_PMU_INT_ROUTING_CLR, 0xF);
    Bcm2837LocalWrite(BCM2837_LOCAL_TIMER_CTRL_STATUS, 0);
    Bcm2837LocalWrite(BCM2837_LOCAL_TIMER_WRITE_FLAGS, 1u << 31);
    Bcm2837LocalWrite(BCM2837_LOCAL_AXI_IRQ_CONTROL, 0);

    /* Per-CPU setup for the BSP (APs get it via InitializeProcessor). */
    Bcm2837InitializeProcessor(KeGetCurrentProcessorNumber());

    DPRINT1("[BCM2837] Interrupt backend initialized (local @%I64x, ARMCTRL @%I64x)\n",
            Bcm2837LocalBase, Bcm2837ArmctrlBase);

    return TRUE;
}

static
BOOLEAN
Bcm2837Phase1MapRuntimeMmio(VOID)
{
    PHYSICAL_ADDRESS Pa;
    PVOID Va;

    Pa.QuadPart = BCM2837_LOCAL_BASE;
    Va = MmMapIoSpace(Pa, BCM2837_LOCAL_LENGTH, MmNonCached);
    if (!Va)
    {
        DPRINT1("[BCM2837] Phase1: MmMapIoSpace failed for ARM-local block\n");
        return FALSE;
    }
    Bcm2837LocalBase = (ULONGLONG)(ULONG_PTR)Va;

    Pa.QuadPart = BCM2837_ARMCTRL_BASE;
    Va = MmMapIoSpace(Pa, BCM2837_ARMCTRL_LENGTH, MmNonCached);
    if (!Va)
    {
        DPRINT1("[BCM2837] Phase1: MmMapIoSpace failed for ARMCTRL block\n");
        return FALSE;
    }
    Bcm2837ArmctrlBase = (ULONGLONG)(ULONG_PTR)Va;

    return TRUE;
}

static const HAL_ARM64_INTERRUPT_CONTROLLER Bcm2837InterruptOps =
{
    "BCM2836 local intc + ARMCTRL",
    Bcm2837AcknowledgeInterrupt,
    Bcm2837EndInterrupt,
    Bcm2837EnableInterrupt,
    Bcm2837DisableInterrupt,
    Bcm2837SetInterruptPriority,
    Bcm2837SetTriggerMode,
    Bcm2837SetPriorityMask,
    Bcm2837GetPriorityMask,
    Bcm2837SendSgi,
    Bcm2837InitializeProcessor,
    Bcm2837Phase0Initialize,
    Bcm2837Phase1MapRuntimeMmio,
};

/* ================================================================== */
/*  Platform detection                                                */
/* ================================================================== */

BOOLEAN
Bcm2837InterruptProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PDESCRIPTION_HEADER Fadt;
    ULONG Cpu;

    if (Bcm2837Detected)
        return TRUE;

    Fadt = HalAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
    if (!Fadt)
        return FALSE;

    if (RtlCompareMemory(Fadt->OEMID, BCM2837_ACPI_OEM_ID, 6) != 6)
        return FALSE;

    if (RtlCompareMemory(Fadt->OEMTableID, BCM2837_ACPI_OEM_TABLE_ID, 4) != 4)
        return FALSE;

    Bcm2837Detected = TRUE;

    Bcm2837LocalBase = BCM2837_LOCAL_BASE;
    Bcm2837ArmctrlBase = BCM2837_ARMCTRL_BASE;

    /* Until proven otherwise, logical CPU n sits on core n. */
    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
        Bcm2837CoreForCpu[Cpu] = (UCHAR)(Cpu & (BCM2837_CORE_COUNT - 1));

    /*
     * Seed the priority tables before anything can enable a source:
     * KeInitInterrupts programs the clock PPI between this probe and
     * Phase 0, and a zero (highest) priority would outrank the IPI.
     */
    for (Cpu = 0; Cpu < BCM2837_GPU_IRQ_COUNT; Cpu++)
        Bcm2837GpuPriority[Cpu] = BCM2837_DEFAULT_PRIORITY;
    for (Cpu = 0; Cpu < BCM2837_BASIC_IRQ_COUNT; Cpu++)
        Bcm2837BasicPriority[Cpu] = BCM2837_DEFAULT_PRIORITY;
    for (Cpu = 0; Cpu < 4; Cpu++)
        Bcm2837TimerRowPriority[Cpu] = BCM2837_DEFAULT_PRIORITY;

    /*
     * Registration also neutralizes the legacy GIC plumbing: the firmware
     * MADT carries Windows-compatibility GICC decoys pointing at the
     * ARM-local block, which must never be dereferenced as a GIC.
     */
    HalpArm64RegisterInterruptController(&Bcm2837InterruptOps);

    DPRINT1("[BCM2837] Raspberry Pi 3 detected — using BCM2836 local intc + ARMCTRL backend\n");

    return TRUE;
}
