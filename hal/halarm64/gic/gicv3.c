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
     * - PMR = 0xFF: Allow all priority levels
     * - BPR1 = 0: No preemption grouping (all priority bits used)
     * - IGRPEN1 = 1: Enable Group 1 interrupts
     */
    HalpWriteIccPmr(0xFF);
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
 * HalpGicv3SetSpiAffinity - Set affinity for a specific SPI
 *
 * Configures GICD_IROUTER for a single SPI to route to the specified
 * CPU based on its MPIDR value.
 *
 * Parameters:
 *   IntId  - The SPI interrupt ID (32-1019)
 *   Mpidr  - MPIDR value of the target CPU
 */
VOID
HalpGicv3SetSpiAffinity(
    _In_ ULONG IntId,
    _In_ ULONGLONG Mpidr)
{
    ULONGLONG Route;

    if (IntId < 32 || IntId >= 1020)
    {
        DPRINT1("[arm64][GICv3] Cannot set affinity for INTID %lu (not an SPI)\n", IntId);
        return;
    }

    Route = HalpArm64IrouterFromMpidr(Mpidr);
    HalpMmioWrite64((ULONG_PTR)HalpGicdBase, GICD_IROUTER + (IntId * 8), Route);

    __asm__ __volatile__("dsb sy" ::: "memory");
    DPRINT("[arm64][GICv3] SPI %lu routed to MPIDR 0x%llx\n", IntId, Mpidr);
}

/*
 * HalpGicv3SetSpiAffinityAny - Set SPI to route to any available CPU
 *
 * Configures GICD_IROUTER for a single SPI with the Interrupt_Routing_Mode
 * bit set, causing the interrupt to be delivered to any PE that is
 * enabled to receive it (1-of-N routing).
 *
 * Parameters:
 *   IntId - The SPI interrupt ID (32-1019)
 */
VOID
HalpGicv3SetSpiAffinityAny(
    _In_ ULONG IntId)
{
    ULONGLONG Route;

    if (IntId < 32 || IntId >= 1020)
    {
        DPRINT1("[arm64][GICv3] Cannot set affinity for INTID %lu (not an SPI)\n", IntId);
        return;
    }

    /* Set bit 31 for "any" routing mode */
    Route = (1ULL << 31);
    HalpMmioWrite64((ULONG_PTR)HalpGicdBase, GICD_IROUTER + (IntId * 8), Route);

    __asm__ __volatile__("dsb sy" ::: "memory");
    DPRINT("[arm64][GICv3] SPI %lu routed to any CPU\n", IntId);
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

    /* Initialize all SPI affinity targets to CPU 0 */
    for (i = 0; i < HALP_GIC_MAX_SPI_COUNT; i++)
    {
        HalpGicSpiAffinityTarget[i] = 0;
    }

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

    DPRINT1("[arm64][GICv3] Registered CPU %lu with MPIDR 0x%llx (online CPUs: %ld)\n",
            CpuIndex, Mpidr, HalpGicOnlineCpuCount);
}

/*
 * HalpArm64SetGicAffinity - Set affinity for a GIC interrupt
 *
 * This function follows the Linux gic_set_affinity() pattern:
 * 1. Validate interrupt ID (must be SPI: 32-1019)
 * 2. Check if target CPU is valid and has a known MPIDR
 * 3. Disable interrupt if currently enabled
 * 4. Program GICD_IROUTER with target CPU's affinity
 * 5. Re-enable interrupt if it was enabled
 * 6. Update affinity tracking
 *
 * Parameters:
 *   InterruptId - GIC interrupt ID (must be SPI: 32-1019)
 *   TargetCpu   - Target CPU index
 *
 * Returns:
 *   STATUS_SUCCESS on success
 *   STATUS_INVALID_PARAMETER if parameters are invalid
 */
NTSTATUS
HalpArm64SetGicAffinity(
    _In_ ULONG InterruptId,
    _In_ ULONG TargetCpu)
{
    ULONGLONG Mpidr;
    ULONGLONG Route;
    ULONG RegOffset;
    ULONG BitPos;
    ULONG EnableBit;
    BOOLEAN WasEnabled;
    KIRQL OldIrql;
    ULONG SpiIndex;

    /*
     * Validate interrupt ID - must be an SPI (32-1019).
     * SGIs (0-15) and PPIs (16-31) are per-CPU and cannot be rerouted.
     * LPIs (8192+) use ITS for routing, not GICD_IROUTER.
     */
    if (InterruptId < 32 || InterruptId >= 1020)
    {
        DPRINT1("[arm64][GICv3] SetAffinity: INTID %lu is not an SPI (must be 32-1019)\n",
                InterruptId);
        return STATUS_INVALID_PARAMETER;
    }

    /* Validate target CPU */
    if (TargetCpu >= MAXIMUM_PROCESSORS)
    {
        DPRINT1("[arm64][GICv3] SetAffinity: target CPU %lu out of range\n", TargetCpu);
        return STATUS_INVALID_PARAMETER;
    }

    /* Calculate SPI index (INTID 32 = index 0) */
    SpiIndex = InterruptId - 32;

    /* Acquire spinlock for thread-safe update */
    KeAcquireSpinLock(&HalpGicAffinityLock, &OldIrql);

    /* Get MPIDR for target CPU */
    if (!HalpGicCpuMpidrValid[TargetCpu])
    {
        KeReleaseSpinLock(&HalpGicAffinityLock, OldIrql);
        DPRINT1("[arm64][GICv3] SetAffinity: CPU %lu has no registered MPIDR\n", TargetCpu);
        return STATUS_INVALID_PARAMETER;
    }
    Mpidr = HalpGicCpuMpidr[TargetCpu];

    /*
     * Check if interrupt is currently enabled.
     * Following Linux's gic_set_affinity: disable before changing routing.
     */
    RegOffset = GICD_ISENABLER + (InterruptId / 32) * 4;
    BitPos = InterruptId % 32;
    EnableBit = *HalpMmio((ULONG_PTR)HalpGicdBase, RegOffset);
    WasEnabled = (EnableBit & (1u << BitPos)) != 0;

    /* Disable interrupt if enabled */
    if (WasEnabled)
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase,
                  GICD_ICENABLER + (InterruptId / 32) * 4) = (1u << BitPos);
        __asm__ __volatile__("dsb sy" ::: "memory");
    }

    /*
     * Build GICD_IROUTER value from MPIDR.
     * GICD_IROUTER format:
     *   Bits [7:0]   = Aff0
     *   Bits [15:8]  = Aff1
     *   Bits [23:16] = Aff2
     *   Bits [39:32] = Aff3
     *   Bit [31]     = IRM (0 for specific CPU routing)
     */
    Route = HalpArm64IrouterFromMpidr(Mpidr);

    /* Program GICD_IROUTER for this SPI */
    HalpMmioWrite64((ULONG_PTR)HalpGicdBase, GICD_IROUTER + (InterruptId * 8), Route);

    /* Memory barrier to ensure IROUTER write completes */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /* Re-enable interrupt if it was enabled */
    if (WasEnabled)
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, RegOffset) = (1u << BitPos);
        __asm__ __volatile__("dsb sy" ::: "memory");
    }

    /* Update affinity tracking */
    HalpGicSpiAffinityTarget[SpiIndex] = TargetCpu;

    KeReleaseSpinLock(&HalpGicAffinityLock, OldIrql);

    DPRINT("[arm64][GICv3] SPI %lu affinity set to CPU %lu (MPIDR 0x%llx, route 0x%llx)\n",
           InterruptId, TargetCpu, Mpidr, Route);

    return STATUS_SUCCESS;
}

/*
 * HalpArm64GetGicAffinity - Get current affinity for a GIC interrupt
 */
ULONG
HalpArm64GetGicAffinity(
    _In_ ULONG InterruptId)
{
    ULONG SpiIndex;
    ULONG TargetCpu;
    KIRQL OldIrql;

    /* Validate interrupt ID - must be an SPI */
    if (InterruptId < 32 || InterruptId >= 1020)
    {
        return (ULONG)-1;
    }

    SpiIndex = InterruptId - 32;

    KeAcquireSpinLock(&HalpGicAffinityLock, &OldIrql);
    TargetCpu = HalpGicSpiAffinityTarget[SpiIndex];
    KeReleaseSpinLock(&HalpGicAffinityLock, OldIrql);

    return TargetCpu;
}

/*
 * HalpGicv3MigrateCpuIrqs - Migrate IRQs away from a CPU going offline
 *
 * This is called during CPU hotplug when a CPU goes offline.
 * All SPIs currently routed to the offline CPU are migrated to CPU 0.
 *
 * This follows Linux's approach for handling CPU offlining in the GIC driver.
 */
VOID
HalpGicv3MigrateCpuIrqs(
    _In_ ULONG CpuIndex)
{
    ULONG i;
    ULONG IntId;
    KIRQL OldIrql;
    ULONG MigratedCount = 0;

    if (CpuIndex >= MAXIMUM_PROCESSORS || CpuIndex == 0)
    {
        /* Cannot migrate away from CPU 0 (bootstrap processor) */
        return;
    }

    DPRINT1("[arm64][GICv3] Migrating IRQs from CPU %lu to CPU 0\n", CpuIndex);

    KeAcquireSpinLock(&HalpGicAffinityLock, &OldIrql);

    for (i = 0; i < HALP_GIC_MAX_SPI_COUNT; i++)
    {
        if (HalpGicSpiAffinityTarget[i] == CpuIndex)
        {
            IntId = i + 32;  /* Convert index to INTID */

            /* Update tracking first */
            HalpGicSpiAffinityTarget[i] = 0;

            /* Release lock temporarily to call SetAffinity */
            KeReleaseSpinLock(&HalpGicAffinityLock, OldIrql);

            /* Migrate to CPU 0 */
            HalpArm64SetGicAffinity(IntId, 0);

            /* Re-acquire lock */
            KeAcquireSpinLock(&HalpGicAffinityLock, &OldIrql);

            MigratedCount++;
        }
    }

    /* Mark CPU as offline by clearing its MPIDR */
    HalpGicCpuMpidr[CpuIndex] = 0;
    HalpGicCpuMpidrValid[CpuIndex] = FALSE;

    /* Decrement online CPU count */
    if (HalpGicOnlineCpuCount > 1)
    {
        InterlockedDecrement(&HalpGicOnlineCpuCount);
    }

    KeReleaseSpinLock(&HalpGicAffinityLock, OldIrql);

    DPRINT1("[arm64][GICv3] Migrated %lu IRQs from CPU %lu to CPU 0\n",
            MigratedCount, CpuIndex);
}

/*
 * HalpGicv3SetSpiAffinityRoundRobin - Distribute SPIs across CPUs
 *
 * This function distributes all SPIs across available CPUs using
 * round-robin for load balancing. Called during initial GIC setup
 * after all CPUs are registered.
 *
 * This provides better interrupt load distribution compared to
 * routing all interrupts to CPU 0.
 */
VOID
HalpGicv3SetSpiAffinityRoundRobin(
    _In_ ULONG Lines)
{
    ULONG i;
    ULONG CpuCount;
    ULONG CurrentCpu = 0;
    ULONG ValidSpiCount = 0;
    KIRQL OldIrql;

    if (Lines < 32)
    {
        DPRINT1("[arm64][GICv3] No SPIs to distribute (Lines=%lu)\n", Lines);
        return;
    }

    if (Lines > 1020)
        Lines = 1020;

    KeAcquireSpinLock(&HalpGicAffinityLock, &OldIrql);

    /* Count online CPUs with valid MPIDR */
    CpuCount = 0;
    for (i = 0; i < MAXIMUM_PROCESSORS; i++)
    {
        if (HalpGicCpuMpidrValid[i])
            CpuCount++;
    }

    if (CpuCount == 0)
    {
        KeReleaseSpinLock(&HalpGicAffinityLock, OldIrql);
        DPRINT1("[arm64][GICv3] No CPUs registered for round-robin distribution\n");
        return;
    }

    KeReleaseSpinLock(&HalpGicAffinityLock, OldIrql);

    DPRINT1("[arm64][GICv3] Distributing SPIs 32-%lu across %lu CPUs (round-robin)\n",
            Lines - 1, CpuCount);

    /* Distribute SPIs across CPUs */
    for (i = 32; i < Lines; i++)
    {
        ULONG SearchCount = 0;

        /* Find next CPU with a registered MPIDR. */
        while ((SearchCount < MAXIMUM_PROCESSORS) &&
               !HalpGicCpuMpidrValid[CurrentCpu])
        {
            CurrentCpu = (CurrentCpu + 1) % MAXIMUM_PROCESSORS;
            SearchCount++;
        }
        if (SearchCount == MAXIMUM_PROCESSORS)
            break;

        /* Set affinity for this SPI */
        if (HalpArm64SetGicAffinity(i, CurrentCpu) == STATUS_SUCCESS)
        {
            ValidSpiCount++;
        }

        /* Move to next CPU */
        CurrentCpu = (CurrentCpu + 1) % MAXIMUM_PROCESSORS;
    }

    DPRINT1("[arm64][GICv3] Distributed %lu SPIs across %lu CPUs\n",
            ValidSpiCount, CpuCount);
}
