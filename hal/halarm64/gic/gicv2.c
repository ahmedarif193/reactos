/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/gic/gicv2.c
 * PURPOSE:         GICv2 (Legacy) Specific Implementation
 *
 * DESCRIPTION:
 *   This file contains GICv2-specific code for ARM64, including:
 *   - Legacy CPU Interface (GICC) initialization and management
 *   - GICv2 interrupt acknowledge/EOI via MMIO
 *   - SGI/PPI bank configuration for GICv2
 *   - Group 0 workaround for QEMU HVF compatibility
 *
 *   GICv2 is the legacy GIC version that uses MMIO for CPU interface access.
 *   It is typically used on systems without GICv3 system register support,
 *   or as a fallback when GICv3 mode fails (e.g., Apple Hypervisor Framework).
 *
 * COMPATIBILITY NOTES:
 *   - Default mode: Group 1 non-secure for Windows-compatible interrupt delivery.
 *   - HVF quirk mode (HalpGicv2ForceGroup0=TRUE): All interrupts use Group 0
 *     because QEMU Apple HVF has a bug where enabling Group 1 causes a hang.
 *   - Group 0 interrupts are delivered as IRQ (not FIQ) in non-secure mode
 *     when GICC_CTLR.EOImodeNS=0, which is our configuration.
 *
 * REFERENCES:
 *   - ARM GIC Architecture Specification (IHI 0048)
 *   - ARM GICv2 Programmer's Guide
 */

#include "gic_internal.h"

#define HALP_GICV2_SPI_BASE 32
#define HALP_GICV2_SPI_MAX 1020
#define HALP_GICV2_SPI_COUNT (HALP_GICV2_SPI_MAX - HALP_GICV2_SPI_BASE)

static UCHAR HalpGicv2CpuTarget[MAXIMUM_PROCESSORS];
static UCHAR HalpGicv2SpiCpu[HALP_GICV2_SPI_COUNT];

/*
 * ============================================================================
 * GICv2 CPU Interface (GICC) Functions
 * ============================================================================
 */

/*
 * HalpInitGicv2CpuInterface - Initialize the GICv2 CPU Interface
 *
 * Configures the GICC registers to enable interrupt delivery:
 * - Sets priority mask to allow all priorities
 * - Configures binary point for no preemption grouping
 * - Enables Group 0 interrupts (Group 1 disabled for HVF compatibility)
 *
 * This function should be called during HAL initialization and on
 * secondary processor startup.
 */
VOID
HalpInitGicv2CpuInterface(VOID)
{
    if (HalpGiccBase == 0)
    {
        DPRINT1("[arm64][GICv2] Cannot init CPU interface: GICC base is 0\n");
        return;
    }

    /*
     * Configure GICC (CPU Interface):
     * - BPR = 0x0: No preemption grouping (all 8 priority bits used)
     * - CTLR: Enable Group0 only when HalpGicv2ForceGroup0 is set (HVF quirk),
     *         otherwise enable both Group0 and Group1 for Windows-style delivery.
     *
     * Group 0 interrupts are delivered as IRQ (not FIQ) in non-secure mode
     * when GICC_CTLR.EOImodeNS=0, which is our configuration.
     */
    {
        ULONG GiccCtlr = HalpGicv2ForceGroup0
            ? GICC_CTLR_ENABLE_GRP0
            : (GICC_CTLR_ENABLE_GRP0 | GICC_CTLR_ENABLE_GRP1);
        *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_PMR) = 0x00;
        *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_BPR) = 0x0;
        *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_CTLR) = GiccCtlr;
    }

    /*
     * CRITICAL: Memory barrier after GICC configuration.
     *
     * ARM64 has a relaxed memory model. MMIO writes to device registers
     * may not complete immediately. DSB ensures that the GICC_CTLR
     * enable takes effect before any subsequent memory operations.
     *
     * ISB ensures the barrier effects are synchronized with the
     * instruction stream.
     */
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");

    DPRINT1("[arm64][GICv2] CPU interface initialized (PMR=0xFF, CTLR=0x%x)\n",
            HalpGicv2ForceGroup0 ? 0x1 : 0x3);
}

/*
 * HalpInitGicv2SgiPpi - Configure SGI/PPI bank for GICv2
 *
 * Configures interrupts 0-31 (SGIs and PPIs) in the distributor:
 * - Preserves already-enabled PPIs (e.g., timer)
 * - Clears pending interrupts
 * - Sets SGI/PPI group based on HalpGicv2ForceGroup0 flag
 * - Sets medium priority (0xA0) for all
 *
 * IMPORTANT: This function preserves PPIs that were enabled by
 * KeInitInterrupts() before HalInitSystem() was called.
 */
VOID
HalpInitGicv2SgiPpi(VOID)
{
    ULONG SavedEnables;
    ULONG i;

    /*
     * Save currently enabled PPIs before reconfiguration.
     * SGIs (bits 0-15) are always enabled in GIC-v2 and cannot be disabled,
     * but PPIs (bits 16-31) need to be preserved if already enabled.
     *
     * This is critical because KeInitInterrupts() enables the timer PPI
     * before HalInitSystem() is called.
     */
    SavedEnables = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ISENABLER);
    DPRINT1("[arm64][GICv2] Saved PPI enables: 0x%08lx\n", SavedEnables);

    /* Clear pending interrupts but do NOT disable */
    *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICPENDR) = 0xFFFFFFFF;

    /*
     * Configure SGI/PPI groups for GIC-v2:
     * - Default: Group 1 non-secure (Windows-compatible behavior)
     * - HVF quirk mode (HalpGicv2ForceGroup0): Group 0 only
     *
     * Group 0 interrupts are still delivered as IRQ in non-secure mode
     * when GICC_CTLR.EOImodeNS=0.
     */
    *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IGROUPR) =
        HalpGicv2ForceGroup0 ? 0x00000000 : 0xFFFFFFFF;

    /* Set SGI/PPI priorities to medium (0xD0) */
    for (i = 0; i < 32; i += 4)
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IPRIORITYR + i) = 0xD0D0D0D0;
    }

    *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IPRIORITYR) =
        ((ULONG)HAL_ARM64_SGI_FREEZE_PRIORITY << 24) |
        ((ULONG)HAL_ARM64_SGI_DPC_PRIORITY << 16) |
        ((ULONG)HAL_ARM64_SGI_APC_PRIORITY << 8) |
        (ULONG)HAL_ARM64_SGI_IPI_PRIORITY;

    /*
     * Restore previously enabled interrupts.
     * Writing to GICD_ISENABLER sets enable bits (does not clear others).
     * This ensures timer PPI and SGIs remain enabled after reconfiguration.
     */
    if (SavedEnables != 0)
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ISENABLER) = SavedEnables;
        DPRINT1("[arm64][GICv2] Restored PPI enables: 0x%08lx\n", SavedEnables);
    }

    __asm__ __volatile__("dsb sy" ::: "memory");
    DPRINT1("[arm64][GICv2] SGI/PPI bank configured with preserved enables\n");
}

/*
 * ============================================================================
 * GICv2 Interrupt Acknowledge / EOI
 * ============================================================================
 */

/*
 * HalpGicv2AcknowledgeInterrupt - Acknowledge interrupt via GICC
 *
 * Reads GICC_IAR to get the interrupt ID of the highest-priority
 * pending interrupt. This also activates the interrupt.
 *
 * Returns:
 *   The interrupt ID (INTID), or 1023 for spurious interrupts.
 *
 * Notes:
 *   - Reading GICC_IAR has side effects (activates the interrupt)
 *   - The value must be saved and passed to HalpGicv2EndInterrupt()
 */
ULONG
HalpGicv2AcknowledgeInterrupt(VOID)
{
    ULONG IntId;
    ULONG Cpu;

    if (HalpGiccBase == 0)
    {
        DPRINT1("[arm64][GICv2] Cannot acknowledge: GICC base is 0\n");
        return 1023; /* Spurious */
    }

    IntId = *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_IAR);

    IntId &= 0x1FFF;

    /* Track active interrupt for this CPU */
    Cpu = KeGetCurrentProcessorNumber();
    if (Cpu < MAXIMUM_PROCESSORS)
    {
        HalpArm64ActiveIntId[Cpu] = IntId & 0x3FF;
    }

    return IntId;
}

/*
 * HalpGicv2EndInterrupt - Signal End-Of-Interrupt via GICC
 *
 * Writes to GICC_EOIR to signal that interrupt handling is complete.
 * This deactivates the interrupt and may allow a lower-priority
 * interrupt to be delivered.
 *
 * Parameters:
 *   IntId - The interrupt ID that was returned by HalpGicv2AcknowledgeInterrupt()
 *
 * Notes:
 *   - The IntId must match what was read from GICC_IAR
 *   - For GICv2, only the lower 10 bits are significant
 */
VOID
HalpGicv2EndInterrupt(
    _In_ ULONG IntId)
{
    if (HalpGiccBase == 0)
    {
        DPRINT1("[arm64][GICv2] Cannot EOI: GICC base is 0\n");
        return;
    }

    /* Write the interrupt ID to GICC_EOIR */
    *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_EOIR) = IntId & 0x1FFF;

    /* Memory barrier to ensure EOI completes */
    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * ============================================================================
 * GICv2 SPI Target Configuration
 * ============================================================================
 */

/*
 * HalpGicv2SetSpiTarget - Set target CPU for an SPI
 *
 * Configures which CPU(s) an SPI (Shared Peripheral Interrupt) is
 * routed to. In GICv2, this is done via GICD_ITARGETSR.
 *
 * Parameters:
 *   IntId     - The SPI interrupt ID (32-1019)
 *   CpuTarget - Bitmask of target CPUs (bit 0 = CPU0, etc.)
 */
static VOID
HalpGicv2SetSpiTarget(
    _In_ ULONG IntId,
    _In_ ULONG CpuTarget)
{
    volatile UCHAR *TargetRegister;

    if (IntId < 32 || IntId >= 1020)
    {
        DPRINT1("[arm64][GICv2] Cannot set target for INTID %lu (not an SPI)\n", IntId);
        return;
    }

    /* Each GICD_ITARGETSR byte controls one interrupt independently. */
    TargetRegister = (volatile UCHAR *)HalpMmio((ULONG_PTR)HalpGicdBase,
                                                GICD_ITARGETSR + IntId);
    *TargetRegister = (UCHAR)CpuTarget;

    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * The target bit seen by a GICv2 CPU interface is implementation-defined and
 * need not match the operating-system processor number. The SGI target
 * registers are banked, so each processor can discover its own one-hot bit.
 */
VOID
HalpGicv2RegisterCpuTarget(
    _In_ ULONG ProcessorNumber)
{
    ULONG TargetWord;
    UCHAR CpuTarget;

    if (ProcessorNumber >= MAXIMUM_PROCESSORS || HalpGicdBase == 0)
        return;

    TargetWord = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ITARGETSR);
    CpuTarget = (UCHAR)TargetWord;
    if (CpuTarget == 0 || (CpuTarget & (CpuTarget - 1)) != 0)
    {
        DPRINT1("[arm64][GICv2] CPU %lu has invalid target bit 0x%02x\n",
                ProcessorNumber,
                CpuTarget);
        return;
    }

    HalpGicv2CpuTarget[ProcessorNumber] = CpuTarget;
    __asm__ __volatile__("dmb ishst" ::: "memory");
    DPRINT("[arm64][GICv2] CPU %lu target bit 0x%02x\n",
           ProcessorNumber,
           CpuTarget);
}

/* Called with HalpArm64VectorLock held; the first assignment remains stable. */
BOOLEAN
HalpGicv2ConfigureSpiAffinity(
    _In_ ULONG IntId,
    _Out_ PKAFFINITY Affinity)
{
    ULONG CandidateCpu[MAXIMUM_PROCESSORS];
    ULONG CandidateCount = 0;
    ULONG ProcessorCount;
    ULONG Cpu;
    ULONG SelectedCpu;
    ULONG Index;
    UCHAR CpuTarget;
    BOOLEAN NewAssignment = FALSE;

    if (!Affinity)
        return FALSE;

    *Affinity = 0;
    if (IntId < HALP_GICV2_SPI_BASE || IntId >= HALP_GICV2_SPI_MAX)
        return FALSE;

    Index = IntId - HALP_GICV2_SPI_BASE;
    if (HalpGicv2SpiCpu[Index] != 0)
    {
        SelectedCpu = HalpGicv2SpiCpu[Index] - 1;
    }
    else
    {
        ProcessorCount = KeNumberProcessors;
        if (ProcessorCount > MAXIMUM_PROCESSORS)
            ProcessorCount = MAXIMUM_PROCESSORS;
        if (ProcessorCount > sizeof(KAFFINITY) * 8)
            ProcessorCount = sizeof(KAFFINITY) * 8;

        __asm__ __volatile__("dmb ishld" ::: "memory");
        for (Cpu = 0; Cpu < ProcessorCount; Cpu++)
        {
            CpuTarget = HalpGicv2CpuTarget[Cpu];
            if (CpuTarget != 0 && (CpuTarget & (CpuTarget - 1)) == 0)
                CandidateCpu[CandidateCount++] = Cpu;
        }

        if (CandidateCount == 0)
            return FALSE;

        SelectedCpu = CandidateCpu[Index % CandidateCount];
        HalpGicv2SpiCpu[Index] = (UCHAR)(SelectedCpu + 1);
        NewAssignment = TRUE;
    }

    CpuTarget = HalpGicv2CpuTarget[SelectedCpu];
    if (CpuTarget == 0)
        return FALSE;

    HalpGicv2SetSpiTarget(IntId, CpuTarget);
    *Affinity = (KAFFINITY)1 << SelectedCpu;
    if (NewAssignment)
    {
        DPRINT("[arm64][GICv2] SPI %lu assigned to CPU %lu target 0x%02x\n",
               IntId,
               SelectedCpu,
               CpuTarget);
    }
    return TRUE;
}

/*
 * HalpInitGicv2SpiTargets - Initialize all SPI targets to CPU0
 *
 * Sets all SPIs to target CPU0 during initialization.
 * This is called from HalpInitGicDistributor() for GICv2.
 *
 * Parameters:
 *   Lines - Number of interrupt lines supported by the GIC
 */
VOID
HalpInitGicv2SpiTargets(
    _In_ ULONG Lines)
{
    ULONG i;
    ULONG TargetWord;
    UCHAR BootTarget;

    RtlZeroMemory(HalpGicv2SpiCpu, sizeof(HalpGicv2SpiCpu));

    /* The boot processor's target bit is implementation-defined. */
    BootTarget = HalpGicv2CpuTarget[0];
    if (BootTarget == 0)
        BootTarget = 0x01;
    TargetWord = (ULONG)BootTarget * 0x01010101u;

    /* SPIs start at interrupt 32 */
    for (i = 32; i < Lines; i += 4)
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ITARGETSR + (i & ~3)) = TargetWord;
    }

    __asm__ __volatile__("dsb sy" ::: "memory");
    DPRINT("[arm64][GICv2] SPI targets set to CPU0\n");
}
