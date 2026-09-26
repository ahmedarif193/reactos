/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/gic/gicv3.c
 * PURPOSE:         GICv3 Specific Implementation
 *
 * DESCRIPTION:
 *   This file contains GICv3-specific code for ARM64, including:
 *   - System register-based CPU interface (ICC_* registers)
 *   - GICv3 interrupt acknowledge/EOI via system registers
 *   - SPI affinity routing via GICD_IROUTER
 *   - SGI sending via ICC_SGI1R_EL1
 *
 *   GICv3 is the modern GIC architecture that uses system registers
 *   for CPU interface access, providing better performance and
 *   supporting more processors than GICv2.
 *
 * KEY DIFFERENCES FROM GICv2:
 *   - CPU interface accessed via system registers (ICC_*) instead of MMIO
 *   - Per-CPU interrupt configuration via Redistributor (GICR)
 *   - 64-bit affinity routing via GICD_IROUTER
 *   - Support for more CPUs (up to 2^32 vs GICv2's 8)
 *   - LPI support via ITS (handled in gic_its.c)
 *
 * REFERENCES:
 *   - ARM GICv3 Architecture Specification (IHI 0069)
 *   - ARM GICv3 Programmer's Guide
 */

#define NDEBUG
#include "gic_internal.h"

/*
 * ============================================================================
 * GICv3 CPU Interface Initialization
 * ============================================================================
 */

/*
 * HalpInitGicv3CpuInterface - Initialize GICv3 CPU Interface via System Registers
 *
 * Configures the ICC_* system registers to enable interrupt delivery:
 * - ICC_SRE_EL1: Enable system register access
 * - ICC_PMR_EL1: Set priority mask to allow all priorities
 * - ICC_BPR1_EL1: Configure binary point for no preemption grouping
 * - ICC_IGRPEN1_EL1: Enable Group 1 interrupts
 *
 * This function should be called during HAL initialization and on
 * secondary processor startup.
 */
VOID
HalpInitGicv3CpuInterface(VOID)
{
    ULONG Sre;

    /*
     * Enable system register access (SRE bit in ICC_SRE_EL1).
     * This must be done before accessing any other ICC_* registers.
     */
    Sre = HalpReadIccSre();
    Sre |= 0x1; /* SRE bit */
    HalpWriteIccSre(Sre);

    /* Verify SRE is enabled */
    Sre = HalpReadIccSre();
    if (!(Sre & 0x1))
    {
        DPRINT1("[arm64][GICv3] Warning: ICC_SRE.SRE not set, system registers may not work\n");
    }

    /*
     * Configure CPU interface:
     * - BPR1 = 0: No preemption grouping (all priority bits used)
     * - IGRPEN1 = 1: Enable Group 1 interrupts
     */
    HalpWriteIccPmr(0x00);
    HalpWriteIccBpr1(0);
    HalpWriteIccIgrpen1(1);

    /* Verify registers were actually set. */
    {
        ULONG Pmr = HalpReadIccPmr();
        ULONG Igrpen1 = HalpReadIccIgrpen1();
        DPRINT1("[arm64][GICv3] CPU interface initialized: SRE=0x%x PMR=0x%x IGRPEN1=0x%x\n", Sre, Pmr, Igrpen1);
    }
}

/*
 * ============================================================================
 * GICv3 Interrupt Acknowledge / EOI
 * ============================================================================
 */

/*
 * HalpGicv3AcknowledgeInterrupt - Acknowledge interrupt via ICC_IAR1_EL1
 *
 * Reads the ICC_IAR1_EL1 system register to get the interrupt ID of
 * the highest-priority pending Group 1 interrupt. This also activates
 * the interrupt.
 *
 * Returns:
 *   The interrupt ID (INTID), or 1023 for spurious interrupts.
 *
 * Notes:
 *   - Reading ICC_IAR1_EL1 has side effects (activates the interrupt)
 *   - The value must be saved and passed to HalpGicv3EndInterrupt()
 *   - For GICv3, the full 24-bit INTID range is supported
 */
ULONG
HalpGicv3AcknowledgeInterrupt(VOID)
{
    ULONG IntId;
    ULONG Cpu;

    /* Read ICC_IAR1_EL1 - this also activates the interrupt */
    IntId = HalpReadIccIar1();

    /* Track active interrupt for this CPU */
    Cpu = KeGetCurrentProcessorNumber();
    if (Cpu < MAXIMUM_PROCESSORS)
    {
        HalpArm64ActiveIntId[Cpu] = IntId;
    }

    return IntId;
}

/*
 * HalpGicv3EndInterrupt - Signal End-Of-Interrupt via ICC_EOIR1_EL1
 *
 * Writes to ICC_EOIR1_EL1 to signal that interrupt handling is complete.
 * This deactivates the interrupt (priority drop and deactivation are
 * combined in EOImode=0, which is our configuration).
 *
 * Parameters:
 *   IntId - The interrupt ID that was returned by HalpGicv3AcknowledgeInterrupt()
 *
 * Notes:
 *   - The IntId should match what was read from ICC_IAR1_EL1
 *   - The ISB in HalpWriteIccEoir1 ensures completion before return
 */
VOID
HalpGicv3EndInterrupt(
    _In_ ULONG IntId)
{
    HalpWriteIccEoir1(IntId);
}

/*
 * ============================================================================
 * GICv3 SGI (Software Generated Interrupt) Functions
 * ============================================================================
 */

/*
 * HalpGicv3SendSgi - Send SGI via ICC_SGI1R_EL1
 *
 * Sends a Software Generated Interrupt (SGI) to the specified target
 * processor(s) using the GICv3 system register interface.
 *
 * ICC_SGI1R_EL1 format:
 *   [55:48] Aff3 - Affinity level 3
 *   [47:44] RS   - Range Selector (not used in basic mode)
 *   [40:40] IRM  - Interrupt Routing Mode (0=list, 1=all except self)
 *   [39:32] Aff2 - Affinity level 2
 *   [27:24] INTID - SGI ID (0-15)
 *   [23:16] Aff1 - Affinity level 1
 *   [15:0]  TargetList - Bitmap of target CPUs within the affinity cluster
 *
 * Parameters:
 *   TargetSet - Bitmask of target processors
 *   SgiId     - SGI number (0-15)
 *
 */
static
BOOLEAN
HalpGicv3GetMpidrForCpu(
    _In_ ULONG Cpu,
    _Out_ PULONGLONG Mpidr)
{
    if (Cpu >= MAXIMUM_PROCESSORS)
        return FALSE;

    if (Cpu == KeGetCurrentProcessorNumber())
    {
        *Mpidr = HalpReadMpidr();
        return TRUE;
    }

    if (HalpGicCpuMpidrValid[Cpu])
    {
        *Mpidr = HalpGicCpuMpidr[Cpu];
        return TRUE;
    }

    if ((Cpu < HalpArm64GicInfo.GiccEntryCount) &&
        (HalpArm64GicInfo.GiccEntries[Cpu].Flags & 0x1))
    {
        *Mpidr = HalpArm64GicInfo.GiccEntries[Cpu].Mpidr;
        return TRUE;
    }

    return FALSE;
}

VOID
HalpGicv3SendSgi(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG SgiId)
{
    KAFFINITY Remaining;
    ULONG Cpu;
    ULONG MaxAffinityBits;

    if ((TargetSet == 0) || (SgiId > 15))
        return;

    Remaining = TargetSet;
    MaxAffinityBits = sizeof(KAFFINITY) * 8;

    for (Cpu = 0; (Cpu < MAXIMUM_PROCESSORS) && (Cpu < MaxAffinityBits) && (Remaining != 0); Cpu++)
    {
        KAFFINITY CpuMask = ((KAFFINITY)1 << Cpu);
        ULONGLONG Mpidr;
        ULONG Aff1;
        ULONG Aff2;
        ULONG Aff3;
        ULONG Rs;
        ULONG TargetList = 0;
        ULONG TargetCpu;
        ULONGLONG SgiVal;

        if ((Remaining & CpuMask) == 0)
            continue;

        if (!HalpGicv3GetMpidrForCpu(Cpu, &Mpidr))
            continue;

        Aff1 = (ULONG)((Mpidr >> 8) & 0xFF);
        Aff2 = (ULONG)((Mpidr >> 16) & 0xFF);
        Aff3 = (ULONG)((Mpidr >> 32) & 0xFF);
        Rs = (ULONG)(Mpidr & 0xFF) >> 4;

        for (TargetCpu = Cpu;
             (TargetCpu < MAXIMUM_PROCESSORS) && (TargetCpu < MaxAffinityBits);
             TargetCpu++)
        {
            KAFFINITY TargetMask = ((KAFFINITY)1 << TargetCpu);
            ULONGLONG TargetMpidr;

            if ((Remaining & TargetMask) == 0)
                continue;

            if (!HalpGicv3GetMpidrForCpu(TargetCpu, &TargetMpidr))
                continue;

            if ((((TargetMpidr >> 8) & 0xFF) != Aff1) ||
                (((TargetMpidr >> 16) & 0xFF) != Aff2) ||
                (((TargetMpidr >> 32) & 0xFF) != Aff3) ||
                ((((ULONG)(TargetMpidr & 0xFF) >> 4) != Rs)))
            {
                continue;
            }

            TargetList |= (1u << (TargetMpidr & 0xF));
            Remaining &= ~TargetMask;
        }

        if (TargetList == 0)
            continue;

        SgiVal = 0;
        SgiVal |= (ULONGLONG)(SgiId & 0xF) << 24;
        SgiVal |= (ULONGLONG)(Rs & 0xF) << 44;
        SgiVal |= (ULONGLONG)(Aff1 & 0xFF) << 16;
        SgiVal |= (ULONGLONG)(Aff2 & 0xFF) << 32;
        SgiVal |= (ULONGLONG)(Aff3 & 0xFF) << 48;
        SgiVal |= (ULONGLONG)(TargetList & 0xFFFF);

        HalpWriteIccSgi1r(SgiVal);
    }

    __asm__ __volatile__("dsb sy; sev" ::: "memory");
}

/*
 * ============================================================================
 * GICv3 SPI Affinity Routing
 * ============================================================================
 */

/*
 * HalpInitGicv3SpiRouting - Initialize SPI routing for GICv3
 *
 * Configures GICD_IROUTER for all SPIs to route to the current CPU.
 * In GICv3, each SPI has a 64-bit routing register that specifies
 * which PE (Processing Element) receives the interrupt.
 *
 * GICD_IROUTER format:
 *   [63:40] Reserved
 *   [39:32] Aff3
 *   [31]    Interrupt_Routing_Mode (0=1-of-N, 1=any)
 *   [30:24] Reserved
 *   [23:16] Aff2
 *   [15:8]  Aff1
 *   [7:0]   Aff0
 *
 * Parameters:
 *   Lines - Number of interrupt lines supported by the GIC
 */
VOID
HalpInitGicv3SpiRouting(
    _In_ ULONG Lines)
{
    ULONGLONG Route;
    ULONG i;

    /* Build routing value from current CPU's MPIDR */
    Route = HalpArm64IrouterFromMpidr(HalpReadMpidr());

    /* Configure routing for each SPI (32-1019) */
    for (i = 32; i < Lines; ++i)
    {
        HalpMmioWrite64((ULONG_PTR)HalpGicdBase, GICD_IROUTER + (i * 8), Route);
    }

    __asm__ __volatile__("dsb sy" ::: "memory");
    DPRINT("[arm64][GICv3] SPI routing configured to current CPU (route=0x%llx)\n", Route);
}

/*
 * ============================================================================
 * GICv3 Dynamic IRQ Affinity Routing
 * ============================================================================
 *
 * Windows 11 ARM64 GIC compatibility requires dynamic IRQ affinity routing.
 * Linux implements this in drivers/irqchip/irq-gic-v3.c via gic_set_affinity().
 *
 * Key concepts:
 * - Each SPI has a 64-bit GICD_IROUTER register controlling which CPU receives it
 * - GICD_IROUTER contains affinity values (Aff3, Aff2, Aff1, Aff0) from MPIDR
 * - IRM bit (bit 31): 0 = specific CPU routing, 1 = any CPU routing
 * - Affinity changes require disabling the interrupt, updating IROUTER, re-enabling
 *
 * Following Linux's approach (gic_set_affinity):
 * 1. Disable interrupt if enabled
 * 2. Program GICD_IROUTER with target CPU's affinity
 * 3. Re-enable interrupt if it was enabled
 * 4. Update tracking state
 */

/*
 * HalpGicv3InitAffinityTracking - Initialize affinity tracking state
 */
VOID
HalpGicv3InitAffinityTracking(VOID)
{
    ULONG i;
    ULONGLONG Mpidr;

    if (HalpGicAffinityInitialized)
        return;

    /* Initialize the spinlock */
    KeInitializeSpinLock(&HalpGicAffinityLock);

    /* Clear all CPU MPIDR entries */
    for (i = 0; i < MAXIMUM_PROCESSORS; i++)
    {
        HalpGicCpuMpidr[i] = 0;
        HalpGicCpuMpidrValid[i] = FALSE;
    }

    /* Register the boot CPU (CPU 0) */
    Mpidr = HalpReadMpidr();
    HalpGicCpuMpidr[0] = Mpidr;
    HalpGicCpuMpidrValid[0] = TRUE;
    HalpGicOnlineCpuCount = 1;

    HalpGicAffinityInitialized = TRUE;

    DPRINT1("[arm64][GICv3] Affinity tracking initialized (boot CPU MPIDR=0x%llx)\n", Mpidr);
}

/*
 * HalpGicv3RegisterCpu - Register a CPU's MPIDR for affinity routing
 */
VOID
HalpGicv3RegisterCpu(
    _In_ ULONG CpuIndex,
    _In_ ULONGLONG Mpidr)
{
    KIRQL OldIrql;

    if (CpuIndex >= MAXIMUM_PROCESSORS)
    {
        DPRINT1("[arm64][GICv3] Cannot register CPU %lu: index out of range\n", CpuIndex);
        return;
    }

    KeAcquireSpinLock(&HalpGicAffinityLock, &OldIrql);

    /* Increment online CPU count if this is a new registration. */
    if (!HalpGicCpuMpidrValid[CpuIndex])
    {
        InterlockedIncrement(&HalpGicOnlineCpuCount);
    }

    HalpGicCpuMpidr[CpuIndex] = Mpidr;
    HalpGicCpuMpidrValid[CpuIndex] = TRUE;

    KeReleaseSpinLock(&HalpGicAffinityLock, OldIrql);

    DPRINT("[arm64][GICv3] Registered CPU %lu with MPIDR 0x%llx (online CPUs: %ld)\n",
            CpuIndex, Mpidr, HalpGicOnlineCpuCount);
}
