/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/gic/gic_redist.c
 * PURPOSE:         GIC Redistributor (GICR) Management with Multi-Region Support
 *
 * DESCRIPTION:
 *   This file contains GIC Redistributor (GICR) management code for GICv3+.
 *   The Redistributor is responsible for:
 *   - Per-CPU SGI (Software Generated Interrupt) configuration
 *   - Per-CPU PPI (Private Peripheral Interrupt) configuration
 *   - LPI (Locality-specific Peripheral Interrupt) configuration
 *   - Waking/sleeping CPU interface state
 *
 *   Multi-Region Support (Windows 11 ARM64 / Linux compatible):
 *   - Parses ALL GICR entries from ACPI MADT (not just the first one)
 *   - Supports multi-socket systems with separate redistributor regions
 *   - Handles non-contiguous redistributor memory layouts
 *   - Variable stride based on VLPI support (128KB or 256KB)
 *   - GICR_TYPER.Last bit detection for region boundaries
 *
 *   Following Linux's approach in drivers/irqchip/irq-gic-v3.c:
 *   - gic_iterate_rdists() - Iterate across all regions
 *   - gic_populate_rdist() - Find redistributor for current CPU
 *   - gic_acpi_parse_madt_redist() - Parse MADT GICR entries
 *
 * REFERENCES:
 *   - ARM GICv3 Architecture Specification (IHI 0069)
 *   - ACPI Specification (MADT GIC structures)
 *   - Linux kernel drivers/irqchip/irq-gic-v3.c
 */

#include "gic_internal.h"

/*
 * GICR_PIDR2 register offset and architecture mask for validation
 */
#define GICR_PIDR2_OFFSET       0xFFE8
#define GICR_PIDR2_ARCH_MASK    0xF0
#define GICR_PIDR2_ARCH_GICv3   0x30
#define GICR_PIDR2_ARCH_GICv4   0x40

/*
 * ============================================================================
 * Redistributor Region Initialization
 * ============================================================================
 */

/*
 * HalpArm64InitRedistRegions - Initialize redistributor region array from ACPI
 *
 * Populates HalpGicRedistRegions[] from the parsed ACPI MADT GICR entries.
 * This function translates from the ACPI info structure to the HAL's
 * internal region tracking structure.
 *
 * Called during early GIC initialization, before redistributor discovery.
 */
VOID
HalpArm64InitRedistRegions(VOID)
{
    ULONG Index;
    ULONG RegionCount;

    /*
     * Check if we have multi-region data from ACPI parsing.
     * This is populated by HalpAcpiDiscoverArm64Tables() in arm64.c.
     */
    RegionCount = HalpArm64GicInfo.GicrRegionCount;

    if (RegionCount > 0)
    {
        /*
         * We have explicit GICR region entries from MADT.
         * Copy them to our tracking array.
         */
        if (RegionCount > HALP_GIC_MAX_REDIST_REGIONS)
        {
            DPRINT1("[arm64][GICR] Warning: %lu GICR regions found, limiting to %u\n",
                    RegionCount, HALP_GIC_MAX_REDIST_REGIONS);
            RegionCount = HALP_GIC_MAX_REDIST_REGIONS;
        }

        for (Index = 0; Index < RegionCount; Index++)
        {
            PHALP_ARM64_GICR_REGION AcpiRegion = &HalpArm64GicInfo.GicrRegions[Index];
            PHALP_GIC_REDIST_REGION Region = &HalpGicRedistRegions[Index];

            Region->PhysicalBase.QuadPart = AcpiRegion->BaseAddress;
            Region->Length = AcpiRegion->Length;
            Region->SingleRedist = FALSE;  /* From GICR entry, multiple redistributors */
            Region->Mapped = FALSE;

            /*
             * Set up identity mapping for early boot.
             * The HAL uses identity-mapped addresses for MMIO access.
             */
            Region->VirtualBase = (ULONG_PTR)AcpiRegion->BaseAddress;
            Region->Mapped = TRUE;  /* Identity mapping is always "mapped" */

            DPRINT1("[arm64][GICR] Region %lu: phys=0x%llx len=0x%lx\n",
                    Index, Region->PhysicalBase.QuadPart, Region->Length);
        }

        HalpGicRedistRegionCount = RegionCount;
    }
    else if (HalpArm64GicInfo.GicrBase != 0)
    {
        /*
         * Fallback: No explicit GICR entries, but we have a single region
         * from legacy MADT parsing or default values.
         */
        HalpGicRedistRegions[0].PhysicalBase.QuadPart = HalpArm64GicInfo.GicrBase;
        HalpGicRedistRegions[0].Length = (ULONG)HalpArm64GicInfo.GicrLength;
        HalpGicRedistRegions[0].SingleRedist = FALSE;
        HalpGicRedistRegions[0].VirtualBase = (ULONG_PTR)HalpArm64GicInfo.GicrBase;
        HalpGicRedistRegions[0].Mapped = TRUE;
        HalpGicRedistRegionCount = 1;

        DPRINT1("[arm64][GICR] Legacy single region: phys=0x%llx len=0x%llx\n",
                HalpArm64GicInfo.GicrBase, HalpArm64GicInfo.GicrLength);
    }
    else
    {
        /*
         * No GICR information available - this could be a GICC-only system
         * where redistributor bases come from individual GICC entries.
         */
        DPRINT1("[arm64][GICR] No GICR regions found, will use per-CPU GICC entries\n");
        HalpGicRedistRegionCount = 0;
    }

    /*
     * Also process per-CPU GICC entries that have GicrBaseAddress set.
     * These are treated as single-redistributor regions.
     * Following Linux's gic_acpi_parse_madt_gicc() approach.
     */
    if (HalpArm64GicInfo.GiccEntryCount > 0)
    {
        for (Index = 0; Index < HalpArm64GicInfo.GiccEntryCount; Index++)
        {
            PHALP_ARM64_GICC_ENTRY GiccEntry = &HalpArm64GicInfo.GiccEntries[Index];

            if (GiccEntry->GicrBase != 0)
            {
                /*
                 * This GICC entry has a per-CPU redistributor base.
                 * Check if we already have a region covering this address.
                 */
                BOOLEAN AlreadyCovered = FALSE;
                ULONG RegIdx;

                for (RegIdx = 0; RegIdx < HalpGicRedistRegionCount; RegIdx++)
                {
                    PHALP_GIC_REDIST_REGION Reg = &HalpGicRedistRegions[RegIdx];
                    ULONGLONG RegStart = Reg->PhysicalBase.QuadPart;
                    ULONGLONG RegEnd = RegStart + Reg->Length;

                    if (GiccEntry->GicrBase >= RegStart && GiccEntry->GicrBase < RegEnd)
                    {
                        AlreadyCovered = TRUE;
                        break;
                    }
                }

                if (!AlreadyCovered && HalpGicRedistRegionCount < HALP_GIC_MAX_REDIST_REGIONS)
                {
                    /*
                     * Add this as a single-redistributor region.
                     * Size is 256KB (4 * 64KB) to be safe for VLPI support.
                     */
                    PHALP_GIC_REDIST_REGION Region =
                        &HalpGicRedistRegions[HalpGicRedistRegionCount];

                    Region->PhysicalBase.QuadPart = GiccEntry->GicrBase;
                    Region->Length = HALP_GICR_STRIDE_WITH_VLPI;  /* Conservative size */
                    Region->SingleRedist = TRUE;  /* Single CPU redistributor */
                    Region->VirtualBase = (ULONG_PTR)GiccEntry->GicrBase;
                    Region->Mapped = TRUE;

                    HalpGicRedistRegionCount++;

                    DPRINT("[arm64][GICR] Added single-redist region from GICC[%lu]: 0x%llx\n",
                           Index, GiccEntry->GicrBase);
                }
            }
        }
    }

    DPRINT1("[arm64][GICR] Total redistributor regions: %lu\n", HalpGicRedistRegionCount);
}

/*
 * HalpArm64MapRedistRegions - Map redistributor regions (for non-identity mapping)
 *
 * This function is a placeholder for systems that need explicit virtual
 * address mapping. In the current ReactOS ARM64 HAL, we use identity mapping.
 */
VOID
HalpArm64MapRedistRegions(VOID)
{
    /* Currently using identity mapping - no explicit mapping needed */
    DPRINT("[arm64][GICR] Using identity mapping for redistributor regions\n");
}

/*
 * ============================================================================
 * Redistributor Stride Calculation
 * ============================================================================
 */

/*
 * HalpArm64GetRedistStrideForTyper - Calculate redistributor stride from TYPER
 *
 * The redistributor frame size depends on whether VLPI is supported:
 * - Without VLPI: 64KB RD_base + 64KB SGI_base = 128KB stride
 * - With VLPI:    64KB RD_base + 64KB SGI_base + 64KB VLPI_base + 64KB reserved = 256KB
 *
 * This follows Linux's gic_iterate_rdists() logic:
 *   if (gic_data.redist_stride) {
 *       ptr += gic_data.redist_stride;
 *   } else {
 *       ptr += SZ_64K * 2;  // Skip RD_base + SGI_base
 *       if (typer & GICR_TYPER_VLPIS)
 *           ptr += SZ_64K * 2;  // Skip VLPI_base + reserved
 *   }
 */
ULONG
HalpArm64GetRedistStrideForTyper(
    _In_ ULONGLONG Typer)
{
    /*
     * If a fixed stride is configured, use it.
     * This can be set from ACPI or boot options.
     */
    if (HalpGicRedistStride != 0)
    {
        return HalpGicRedistStride;
    }

    /*
     * Calculate stride based on VLPI support.
     * GICR_TYPER.VLPIS (bit 1) indicates virtual LPI support.
     */
    if (Typer & HALP_GICR_TYPER_VLPIS)
    {
        /* VLPI supported: 256KB stride (4 x 64KB frames) */
        return HALP_GICR_STRIDE_WITH_VLPI;
    }
    else
    {
        /* No VLPI: 128KB stride (2 x 64KB frames) */
        return HALP_GICR_STRIDE_NO_VLPI;
    }
}

/*
 * ============================================================================
 * Redistributor Discovery (Multi-Region)
 * ============================================================================
 */

/*
 * HalpArm64FindGicrInRegion - Search for redistributor in a single region
 *
 * Scans a redistributor region looking for a frame matching the target MPIDR.
 * Uses variable stride based on GICR_TYPER.VLPIS and respects GICR_TYPER.Last.
 *
 * This follows Linux's gic_iterate_rdists() inner loop logic.
 */
BOOLEAN
HalpArm64FindGicrInRegion(
    _In_ PHALP_GIC_REDIST_REGION Region,
    _In_ ULONGLONG Mpidr,
    _Out_opt_ PULONG OutOffset)
{
    ULONG_PTR Base;
    ULONG_PTR Current;
    ULONG_PTR RegionEnd;
    ULONGLONG Typer;
    ULONG TyperAffinity;
    ULONG TargetAffinity;
    ULONG Stride;
    ULONG Pidr2;
    ULONG FrameIndex;

    if (!Region || !Region->Mapped || Region->Length == 0)
    {
        return FALSE;
    }

    Base = Region->VirtualBase;
    RegionEnd = Base + Region->Length;
    TargetAffinity = HalpArm64AffinityFromMpidr(Mpidr);

    /*
     * Validate that this looks like a redistributor region.
     * Check GICR_PIDR2 for GICv3/v4 architecture revision.
     * Following Linux's gic_iterate_rdists() validation.
     */
    Pidr2 = *HalpMmio(Base, GICR_PIDR2_OFFSET);
    if ((Pidr2 & GICR_PIDR2_ARCH_MASK) != GICR_PIDR2_ARCH_GICv3 &&
        (Pidr2 & GICR_PIDR2_ARCH_MASK) != GICR_PIDR2_ARCH_GICv4)
    {
        DPRINT1("[arm64][GICR] Warning: No valid redistributor at 0x%p (PIDR2=0x%lx)\n",
                (PVOID)Base, Pidr2);
        return FALSE;
    }

    /*
     * Scan each redistributor frame in this region.
     */
    Current = Base;
    FrameIndex = 0;

    do
    {
        if (Current >= RegionEnd)
        {
            /* Exceeded region bounds */
            break;
        }

        /* Read GICR_TYPER to get affinity and capabilities */
        Typer = HalpMmioRead64(Current, GICR_TYPER);
        TyperAffinity = (ULONG)((Typer >> HALP_GICR_TYPER_AFFINITY_SHIFT) & 0xFFFFFFFFu);

        /* Check if this redistributor matches our target affinity */
        if (TyperAffinity == TargetAffinity)
        {
            DPRINT("[arm64][GICR] Found GICR for MPIDR 0x%llx at 0x%p (frame %lu)\n",
                   Mpidr, (PVOID)Current, FrameIndex);

            if (OutOffset)
            {
                *OutOffset = (ULONG)(Current - Base);
            }
            return TRUE;
        }

        /*
         * For single-redistributor regions (from GICC entries), we only
         * check the one redistributor and stop.
         */
        if (Region->SingleRedist)
        {
            break;
        }

        /* Check LAST bit - if set, this is the last redistributor in the region */
        if (Typer & HALP_GICR_TYPER_LAST)
        {
            DPRINT("[arm64][GICR] Hit LAST bit at frame %lu, GICR not in this region\n",
                   FrameIndex);
            break;
        }

        /* Calculate stride to next redistributor frame */
        Stride = HalpArm64GetRedistStrideForTyper(Typer);
        Current += Stride;
        FrameIndex++;

        /*
         * Safety limit to prevent infinite loops if LAST bit is never set.
         * MAXIMUM_PROCESSORS is a reasonable upper bound.
         */
    } while (FrameIndex < MAXIMUM_PROCESSORS);

    return FALSE;
}

/*
 * HalpArm64FindGicrForMpidr - Find the Redistributor for a specific CPU
 *
 * Searches ALL redistributor regions to find the Redistributor frame
 * matching the specified MPIDR value. This is the main redistributor
 * discovery function, following Linux's gic_populate_rdist() approach.
 *
 * Parameters:
 *   Mpidr - The MPIDR value of the target CPU
 *
 * Returns:
 *   Virtual base address of the matching Redistributor, or 0 if not found.
 */
ULONG_PTR
HalpArm64FindGicrForMpidr(
    _In_ ULONGLONG Mpidr)
{
    ULONG RegionIndex;
    ULONG Offset;
    ULONG TargetAffinity = HalpArm64AffinityFromMpidr(Mpidr);

    /*
     * Initialize redistributor regions if not already done.
     * This ensures we have the multi-region data from ACPI.
     */
    if (HalpGicRedistRegionCount == 0 &&
        (HalpArm64GicInfo.GicrRegionCount > 0 || HalpArm64GicInfo.GicrBase != 0))
    {
        HalpArm64InitRedistRegions();
    }

    /*
     * Search each redistributor region for the matching MPIDR.
     * Following Linux's gic_iterate_rdists() pattern.
     */
    for (RegionIndex = 0; RegionIndex < HalpGicRedistRegionCount; RegionIndex++)
    {
        PHALP_GIC_REDIST_REGION Region = &HalpGicRedistRegions[RegionIndex];

        if (HalpArm64FindGicrInRegion(Region, Mpidr, &Offset))
        {
            ULONG_PTR GicrBase = Region->VirtualBase + Offset;

            DPRINT("[arm64][GICR] CPU MPIDR 0x%llx (aff 0x%08lx): GICR at 0x%p (region %lu, offset 0x%lx)\n",
                   Mpidr, TargetAffinity, (PVOID)GicrBase, RegionIndex, Offset);

            return GicrBase;
        }
    }

    /*
     * Fallback: Check per-CPU GICC entries for redistributor base.
     * Some systems provide the redistributor base directly in GICC entries.
     */
    if (HalpArm64GicInfo.GiccEntryCount > 0)
    {
        ULONG Index;

        for (Index = 0; Index < HalpArm64GicInfo.GiccEntryCount; Index++)
        {
            PHALP_ARM64_GICC_ENTRY Entry = &HalpArm64GicInfo.GiccEntries[Index];

            if (Entry->GicrBase != 0 &&
                HalpArm64AffinityFromMpidr(Entry->Mpidr) == TargetAffinity)
            {
                DPRINT("[arm64][GICR] CPU MPIDR 0x%llx: GICR at 0x%llx (from GICC entry)\n",
                       Mpidr, Entry->GicrBase);
                return (ULONG_PTR)Entry->GicrBase;
            }
        }
    }

    /*
     * Legacy fallback: Use fixed stride calculation from single region.
     * This supports older HAL code paths.
     */
    if (HalpGicrRegionBase != 0 && HalpGicrStride != 0)
    {
        ULONG_PTR Base = (ULONG_PTR)HalpGicrRegionBase;
        ULONG MaxFrames = HalpGicrRegionLength ?
                          (ULONG)(HalpGicrRegionLength / HalpGicrStride) :
                          MAXIMUM_PROCESSORS;
        ULONG FrameIndex;

        for (FrameIndex = 0; FrameIndex < MaxFrames; FrameIndex++)
        {
            ULONG_PTR Current = Base + ((ULONG_PTR)FrameIndex * HalpGicrStride);
            ULONGLONG Typer = HalpMmioRead64(Current, GICR_TYPER);
            ULONG TyperAffinity = (ULONG)((Typer >> 32) & 0xFFFFFFFFu);

            if (TyperAffinity == TargetAffinity)
            {
                DPRINT("[arm64][GICR] CPU MPIDR 0x%llx: GICR at 0x%p (legacy scan, frame %lu)\n",
                       Mpidr, (PVOID)Current, FrameIndex);
                return Current;
            }

            if (Typer & GICR_TYPER_LAST)
            {
                break;
            }
        }
    }

    DPRINT1("[arm64][GICR] ERROR: No GICR found for MPIDR 0x%llx (affinity 0x%08lx)\n",
            Mpidr, TargetAffinity);
    return 0;
}

/*
 * ============================================================================
 * Redistributor Initialization
 * ============================================================================
 */

/*
 * HalpInitGicRedistributor - Initialize the Redistributor for a CPU
 *
 * Performs the following operations:
 * 1. Finds the redistributor for the specified CPU (if not cached)
 * 2. Wakes up the Redistributor (clears ProcessorSleep)
 * 3. Waits for ChildrenAsleep to clear
 * 4. Configures SGI/PPI bank (interrupts 0-31):
 *    - Disables all SGIs/PPIs
 *    - Clears pending state
 *    - Sets Group 1 (non-secure)
 *    - Sets medium priority (0xA0)
 *
 * Parameters:
 *   Cpu - The processor number to initialize
 */
VOID
HalpInitGicRedistributor(
    _In_ ULONG Cpu)
{
    ULONG_PTR Base;
    ULONG_PTR SgiBase;
    volatile ULONG *Waker;
    ULONG WakerVal;
    ULONG Spins;
    ULONG i;
    ULONGLONG Mpidr;
    ULONGLONG Typer;

    /*
     * Find the redistributor for this CPU if not already cached.
     * Uses multi-region discovery.
     */
    if (Cpu < MAXIMUM_PROCESSORS && HalpGicrCpuBase[Cpu] == 0)
    {
        Mpidr = HalpReadMpidr();
        HalpGicrCpuBase[Cpu] = HalpArm64FindGicrForMpidr(Mpidr);

        if (HalpGicrCpuBase[Cpu] == 0)
        {
            DPRINT1("[arm64][GICR] ERROR: Could not find redistributor for CPU %lu (MPIDR 0x%llx)\n",
                    Cpu, Mpidr);
            return;
        }
    }

    Base = HalpGicrBase(Cpu);
    SgiBase = HalpGicrSgiBase(Cpu);

    if (Base == 0)
    {
        DPRINT1("[arm64][GICR] Cannot init redistributor for CPU %lu: base unavailable\n", Cpu);
        return;
    }

    DPRINT1("[arm64][GICR] Initializing redistributor for CPU %lu at 0x%p\n", Cpu, (PVOID)Base);

    /*
     * Read GICR_TYPER to discover capabilities and update global flags.
     * These flags affect how LPIs and VLPIs are handled.
     */
    Typer = HalpMmioRead64(Base, GICR_TYPER);

    if (Typer & HALP_GICR_TYPER_VLPIS)
    {
        HalpGicRedistHasVlpis = TRUE;
    }
    if (Typer & HALP_GICR_TYPER_DIRECTLPI)
    {
        HalpGicRedistHasDirectLpi = TRUE;
    }

    DPRINT("[arm64][GICR] CPU %lu TYPER: 0x%llx (VLPIS=%u DirectLPI=%u Last=%u)\n",
           Cpu, Typer,
           (Typer & HALP_GICR_TYPER_VLPIS) ? 1 : 0,
           (Typer & HALP_GICR_TYPER_DIRECTLPI) ? 1 : 0,
           (Typer & HALP_GICR_TYPER_LAST) ? 1 : 0);

    /*
     * Wake up the Redistributor.
     *
     * Clear the ProcessorSleep bit in GICR_WAKER. The Redistributor
     * is powered up when ProcessorSleep is 0.
     */
    Waker = HalpMmio(Base, GICR_WAKER);
    WakerVal = *Waker;

    WakerVal &= ~GICR_WAKER_PROCESSOR_SLEEP;
    *Waker = WakerVal;

    /*
     * Wait for ChildrenAsleep to clear.
     *
     * The Redistributor signals that all its children (the CPU interface)
     * are awake by clearing the ChildrenAsleep bit.
     */
    for (Spins = 0x100000; Spins != 0; --Spins)
    {
        if (((*Waker) & GICR_WAKER_CHILDREN_ASLEEP) == 0)
            break;
        __asm__ __volatile__("yield");
    }

    if (Spins == 0)
    {
        DPRINT1("[arm64][GICR] Warning: ChildrenAsleep did not clear for CPU %lu\n", Cpu);
    }

    /*
     * Configure SGI/PPI bank (interrupts 0-31).
     *
     * The SGI_base frame is at offset HalpGicrSgiOffset from the RD_base.
     * It contains per-CPU versions of the interrupt configuration registers.
     */

    /* Disable all SGIs/PPIs first */
    *HalpMmio(SgiBase, GICR_ICENABLER0) = 0xFFFFFFFF;

    /* Clear any pending interrupts */
    *HalpMmio(SgiBase, GICR_ICPENDR0) = 0xFFFFFFFF;

    /* Set all SGIs/PPIs to Group 1 (non-secure) */
    *HalpMmio(SgiBase, GICR_IGROUPR0) = 0xFFFFFFFF;

    /* Set priority to medium (0xA0) for all SGIs/PPIs */
    for (i = 0; i < 32; i += 4)
    {
        *HalpMmio(SgiBase, GICR_IPRIORITYR + i) = 0xA0A0A0A0;
    }

    /* Memory barrier before enabling interrupts */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * Register this CPU's MPIDR for dynamic IRQ affinity routing.
     *
     * For Windows 11 ARM64 GIC compatibility, we track each CPU's MPIDR
     * so that interrupts can be dynamically routed to any online CPU.
     * This is done here because the redistributor initialization runs
     * for each CPU during boot and AP startup.
     */
    HalpGicv3RegisterCpu(Cpu, HalpReadMpidr());

    /*
     * Initialize LPI support if ITS is present.
     *
     * Program PROPBASER and PENDBASER for this CPU's redistributor.
     * This enables MSI/MSI-X support through the ITS.
     * Following Linux's its_cpu_init() pattern.
     */
    if (HalpGicItsPresent && (Typer & HALP_GICR_TYPER_PLPIS))
    {
        if (HalpGicItsProgramCpuTables(Cpu))
        {
            DPRINT("[arm64][GICR] LPI tables programmed for CPU %lu\n", Cpu);

            /* Map collection for this CPU on all ITS nodes */
            HalpGicItsEnsureCollection(Cpu);
        }
        else
        {
            DPRINT1("[arm64][GICR] Warning: Failed to program LPI tables for CPU %lu\n", Cpu);
        }
    }

    DPRINT1("[arm64][GICR] Redistributor initialized for CPU %lu\n", Cpu);
}

/*
 * HalpArm64EnableCpuInterface - Enable the CPU interface for the current CPU
 *
 * This is a unified function that initializes the appropriate interface
 * based on the GIC version:
 * - GICv3: Initializes Redistributor + ICC_* system registers
 * - GICv2: Initializes GICC MMIO registers
 *
 * This function is typically called during secondary processor startup.
 *
 * Note: Implementation still in smp.c for now. This can be enabled once
 * the migration is complete.
 */
#if 0 /* Disabled - implementation still in smp.c */
VOID
HalpArm64EnableCpuInterface(VOID)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();
    ULONGLONG Mpidr;

    DPRINT1("[arm64][GIC] Enabling CPU interface for CPU %lu\n", Cpu);

    if (HalpGicUseSysRegs)
    {
        /*
         * GICv3: Need to discover and cache the Redistributor base
         * for this CPU if not already known.
         */
        if (Cpu < MAXIMUM_PROCESSORS && HalpGicrCpuBase[Cpu] == 0)
        {
            Mpidr = HalpReadMpidr();
            HalpGicrCpuBase[Cpu] = HalpArm64FindGicrForMpidr(Mpidr);

            if (HalpGicrCpuBase[Cpu] == 0)
            {
                DPRINT1("[arm64][GIC] Warning: Could not find GICR for CPU %lu\n", Cpu);
            }
        }

        /* Initialize Redistributor */
        HalpInitGicRedistributor(Cpu);

        /* Initialize CPU interface via system registers */
        HalpInitGicv3CpuInterface();
    }
    else
    {
        /* GICv2: Configure SGI/PPI bank and CPU interface */
        HalpInitGicv2SgiPpi();
        HalpInitGicv2CpuInterface();
    }
}
#endif /* Disabled - implementation still in smp.c */

/*
 * ============================================================================
 * Redistributor SGI/PPI Management
 * ============================================================================
 */

/*
 * HalpGicrEnableSgiPpi - Enable an SGI or PPI on the current CPU
 *
 * Enables a specific SGI (0-15) or PPI (16-31) by writing to
 * GICR_ISENABLER0.
 *
 * Parameters:
 *   IntId - Interrupt ID (0-31)
 */
VOID
HalpGicrEnableSgiPpi(
    _In_ ULONG IntId)
{
    ULONG Cpu;
    ULONG_PTR SgiBase;

    if (IntId >= 32)
    {
        DPRINT1("[arm64][GICR] IntId %lu is not an SGI/PPI\n", IntId);
        return;
    }

    Cpu = KeGetCurrentProcessorNumber();
    SgiBase = HalpGicrSgiBase(Cpu);

    if (SgiBase == 0)
    {
        DPRINT1("[arm64][GICR] Cannot enable INTID %lu: SGI base unavailable\n", IntId);
        return;
    }

    *HalpMmio(SgiBase, GICR_ISENABLER0) = (1u << IntId);
    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * HalpGicrDisableSgiPpi - Disable an SGI or PPI on the current CPU
 *
 * Disables a specific SGI (0-15) or PPI (16-31) by writing to
 * GICR_ICENABLER0.
 *
 * Parameters:
 *   IntId - Interrupt ID (0-31)
 */
VOID
HalpGicrDisableSgiPpi(
    _In_ ULONG IntId)
{
    ULONG Cpu;
    ULONG_PTR SgiBase;

    if (IntId >= 32)
    {
        DPRINT1("[arm64][GICR] IntId %lu is not an SGI/PPI\n", IntId);
        return;
    }

    Cpu = KeGetCurrentProcessorNumber();
    SgiBase = HalpGicrSgiBase(Cpu);

    if (SgiBase == 0)
    {
        DPRINT1("[arm64][GICR] Cannot disable INTID %lu: SGI base unavailable\n", IntId);
        return;
    }

    *HalpMmio(SgiBase, GICR_ICENABLER0) = (1u << IntId);
    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * HalpGicrClearPending - Clear pending state for an SGI or PPI
 *
 * Clears the pending state for a specific SGI or PPI by writing to
 * GICR_ICPENDR0.
 *
 * Parameters:
 *   IntId - Interrupt ID (0-31)
 */
VOID
HalpGicrClearPending(
    _In_ ULONG IntId)
{
    ULONG Cpu;
    ULONG_PTR SgiBase;

    if (IntId >= 32)
    {
        DPRINT1("[arm64][GICR] IntId %lu is not an SGI/PPI\n", IntId);
        return;
    }

    Cpu = KeGetCurrentProcessorNumber();
    SgiBase = HalpGicrSgiBase(Cpu);

    if (SgiBase == 0)
    {
        DPRINT1("[arm64][GICR] Cannot clear pending INTID %lu: SGI base unavailable\n", IntId);
        return;
    }

    *HalpMmio(SgiBase, GICR_ICPENDR0) = (1u << IntId);
    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * ============================================================================
 * GICv4 Direct Injection (VLPI) Support
 * ============================================================================
 *
 * Following Linux's GICv4 direct injection implementation in:
 * - drivers/irqchip/irq-gic-v3-its.c (its_vpe_schedule, its_vpe_deschedule)
 * - drivers/irqchip/irq-gic-v3.c (gic_v4_vpe_sched/desched)
 *
 * GICv4 direct injection allows virtual interrupts (VLPIs) to be delivered
 * directly to a virtual processor (vPE) without hypervisor intervention.
 *
 * The mechanism works as follows:
 * 1. Each vPE has a Virtual Pending Table (VPT) that tracks pending VLPIs
 * 2. When a vPE is scheduled on a CPU, GICR_VPENDBASER is programmed with
 *    the VPT address to make the vPE "resident"
 * 3. The ITS uses VMAPP to know which redistributor the vPE is on
 * 4. Incoming VLPIs are directly delivered to the resident vPE
 * 5. When the vPE is descheduled, a doorbell interrupt can be generated
 *    if there are pending VLPIs
 *
 * GICR_VPROPBASER: Points to virtual LPI property table (per-VM or global)
 * GICR_VPENDBASER: Points to vPE's pending table + vPE ID + Valid bit
 */

/*
 * HalpGicrGetVlpiBase - Get VLPI frame base address for a CPU
 *
 * The VLPI_base frame is at offset 0x20000 from the RD_base.
 * This frame contains GICR_VPROPBASER and GICR_VPENDBASER registers.
 */
ULONG_PTR
HalpGicrGetVlpiBase(
    _In_ ULONG Cpu)
{
    ULONG_PTR RdBase;
    ULONGLONG Typer;

    RdBase = HalpGicrBase(Cpu);
    if (RdBase == 0)
        return 0;

    /* Check if VLPI is supported on this redistributor */
    Typer = HalpMmioRead64(RdBase, GICR_TYPER);
    if (!(Typer & HALP_GICR_TYPER_VLPIS))
        return 0;

    return RdBase + GICR_VLPI_OFFSET;
}

/*
 * HalpGicrProgramVpropbaser - Program GICR_VPROPBASER for a CPU
 *
 * Sets up the virtual LPI property table for GICv4. This table
 * contains the priority and enable configuration for VLPIs.
 *
 * In Linux's approach, VPROPBASER typically shares the same table
 * as physical LPIs (PROPBASER) or uses a per-VM virtual property table.
 *
 * For Windows 11 ARM64 / Hyper-V compatibility, we use the global
 * LPI property table to simplify the implementation.
 *
 * Parameters:
 *   Cpu - Target CPU
 *
 * Returns:
 *   TRUE on success, FALSE on failure
 */
BOOLEAN
HalpGicrProgramVpropbaser(
    _In_ ULONG Cpu)
{
    ULONG_PTR VlpiBase;
    ULONGLONG Vpropbaser;

    if (!HalpGicHasVlpis)
        return FALSE;

    VlpiBase = HalpGicrGetVlpiBase(Cpu);
    if (VlpiBase == 0)
    {
        DPRINT("[arm64][GICR] VLPI base not available for CPU %lu\n", Cpu);
        return FALSE;
    }

    /*
     * Use the same property table as physical LPIs.
     * This simplifies management - VLPIs use the same priority/enable
     * configuration as physical LPIs.
     *
     * VPROPBASER format:
     * - Bits [63]: Valid
     * - Bits [56:59]: OuterCacheability
     * - Bits [10:11]: Shareability
     * - Bits [7:9]: InnerCacheability
     * - Bits [4:0]: IDBits (number of ID bits - 1)
     * - Bits [12:51]: Physical address of property table
     */
    if (HalpGicLpiConfigPa.QuadPart == 0)
    {
        DPRINT("[arm64][GICR] LPI config table not initialized\n");
        return FALSE;
    }

    Vpropbaser = (HalpGicLpiConfigPa.QuadPart & GICR_VPROPBASER_ADDR_MASK);
    Vpropbaser |= ((ULONGLONG)(HalpGicItsLpiIdBits - 1) & GICR_VPROPBASER_IDBITS_MASK);
    /* Add inner-shareable, write-back attributes (matching PROPBASER) */
    Vpropbaser |= (1ULL << 10);  /* InnerShareable */
    Vpropbaser |= (7ULL << 7);   /* Write-Back, Read-Allocate, Write-Allocate */
    Vpropbaser |= GICR_VPROPBASER_VALID;

    HalpMmioWrite64(VlpiBase, GICR_VPROPBASER, Vpropbaser);

    __asm__ __volatile__("dsb sy" ::: "memory");

    DPRINT("[arm64][GICR] Programmed VPROPBASER for CPU %lu: 0x%llx\n", Cpu, Vpropbaser);

    return TRUE;
}

/*
 * HalpGicrProgramVpendbaser - Program GICR_VPENDBASER for vPE residency
 *
 * Makes a vPE resident on a CPU by programming GICR_VPENDBASER
 * with the vPE's Virtual Pending Table address.
 *
 * When VPENDBASER.Valid is set, the redistributor enables direct
 * injection of VLPIs for the specified vPE.
 *
 * GICR_VPENDBASER format:
 * - Bit [63]: Valid - When set, vPE is resident on this redistributor
 * - Bit [61]: PendingLast - Set by hardware when vPE becomes non-resident
 *                           with pending VLPIs
 * - Bit [60]: Dirty - Set by hardware when VPT needs writeback
 * - Bits [56:59]: OuterCacheability
 * - Bits [10:11]: Shareability
 * - Bits [7:9]: InnerCacheability
 * - Bits [0:15]: vPE ID (GICv4.1 uses doorbell interrupt number here)
 * - Bits [12:51]: Physical address of Virtual Pending Table
 *
 * Parameters:
 *   Cpu - Target CPU
 *   Vpe - vPE to make resident (NULL to clear residency)
 *
 * Returns:
 *   TRUE on success, FALSE on failure
 */
BOOLEAN
HalpGicrProgramVpendbaser(
    _In_ ULONG Cpu,
    _In_opt_ PHALP_GIC_VPE Vpe)
{
    ULONG_PTR VlpiBase;
    ULONGLONG Vpendbaser;
    ULONGLONG OldValue;
    ULONG Spins;

    if (!HalpGicHasVlpis)
        return FALSE;

    VlpiBase = HalpGicrGetVlpiBase(Cpu);
    if (VlpiBase == 0)
    {
        DPRINT("[arm64][GICR] VLPI base not available for CPU %lu\n", Cpu);
        return FALSE;
    }

    if (Vpe == NULL)
    {
        /*
         * Descheduling: Clear the Valid bit.
         * The hardware will set PendingLast if there are pending VLPIs.
         */
        OldValue = HalpMmioRead64(VlpiBase, GICR_VPENDBASER);

        /* Clear Valid bit */
        Vpendbaser = OldValue & ~GICR_VPENDBASER_Valid;
        HalpMmioWrite64(VlpiBase, GICR_VPENDBASER, Vpendbaser);

        /*
         * Wait for Dirty bit to clear.
         * The hardware clears Dirty when the VPT writeback is complete.
         * Following Linux's its_vpe_deschedule() approach.
         */
        for (Spins = 100000; Spins != 0; --Spins)
        {
            Vpendbaser = HalpMmioRead64(VlpiBase, GICR_VPENDBASER);
            if (!(Vpendbaser & GICR_VPENDBASER_Dirty))
                break;
            KeStallExecutionProcessor(1);
        }

        if (Spins == 0)
        {
            DPRINT1("[arm64][GICR] Warning: VPENDBASER Dirty bit did not clear for CPU %lu\n", Cpu);
        }

        __asm__ __volatile__("dsb sy" ::: "memory");

        DPRINT("[arm64][GICR] Cleared VPENDBASER for CPU %lu (PendingLast=%u)\n",
               Cpu, (Vpendbaser & GICR_VPENDBASER_PendingLast) ? 1 : 0);

        return TRUE;
    }

    /*
     * Scheduling: Set up VPENDBASER with the vPE's VPT address.
     */
    if (Vpe->VptPa.QuadPart == 0)
    {
        DPRINT1("[arm64][GICR] vPE %lu has no VPT allocated\n", Vpe->VpeId);
        return FALSE;
    }

    /*
     * Ensure VPROPBASER is programmed first.
     * This sets up the virtual LPI property table.
     */
    if (!HalpGicrProgramVpropbaser(Cpu))
    {
        DPRINT1("[arm64][GICR] Failed to program VPROPBASER for CPU %lu\n", Cpu);
        return FALSE;
    }

    /*
     * Read current value to check if a different vPE is resident.
     */
    OldValue = HalpMmioRead64(VlpiBase, GICR_VPENDBASER);
    if (OldValue & GICR_VPENDBASER_Valid)
    {
        /*
         * Another vPE is currently resident - need to deschedule it first.
         * This shouldn't happen if the caller is managing vPE residency correctly.
         */
        DPRINT1("[arm64][GICR] Warning: vPE already resident on CPU %lu, clearing first\n", Cpu);
        HalpGicrProgramVpendbaser(Cpu, NULL);
    }

    /*
     * Build VPENDBASER value:
     * - VPT physical address
     * - Cacheability and shareability attributes
     * - vPE ID (for GICv4.0) or doorbell LPI (for GICv4.1)
     * - Valid bit
     */
    Vpendbaser = (Vpe->VptPa.QuadPart & GICR_VPENDBASER_ADDR_MASK);
    /* Add inner-shareable, write-back attributes */
    Vpendbaser |= (1ULL << 10);  /* InnerShareable */
    Vpendbaser |= (7ULL << 7);   /* Write-Back, Read-Allocate, Write-Allocate */

    if (HalpGicIsGicv4_1)
    {
        /*
         * GICv4.1: Use doorbell LPI number in bits [0:15].
         * Also set VGRP0EN and VGRP1EN bits.
         */
        Vpendbaser |= (ULONGLONG)(Vpe->DoorbellLpi & 0xFFFF);
        Vpendbaser |= GICR_VPENDBASER_4_1_VGRP1EN;  /* Enable Group 1 VLPIs */
    }
    else
    {
        /*
         * GICv4.0: Use vPE ID in bits [0:15].
         */
        Vpendbaser |= (ULONGLONG)(Vpe->VpeId & 0xFFFF);
    }

    /* Set Valid bit to make vPE resident */
    Vpendbaser |= GICR_VPENDBASER_Valid;

    HalpMmioWrite64(VlpiBase, GICR_VPENDBASER, Vpendbaser);

    __asm__ __volatile__("dsb sy" ::: "memory");

    DPRINT("[arm64][GICR] Programmed VPENDBASER for CPU %lu, vPE %lu: 0x%llx\n",
           Cpu, Vpe->VpeId, Vpendbaser);

    return TRUE;
}
