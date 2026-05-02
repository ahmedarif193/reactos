/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/gic/gic_init.c
 * PURPOSE:         GIC Detection and Initialization
 *
 * DESCRIPTION:
 *   This file contains GIC detection and initialization code for ARM64:
 *   - ACPI MADT parsing for GIC component discovery
 *   - GIC version detection (v2 vs v3)
 *   - Interface selection (system registers vs legacy MMIO)
 *   - Distributor initialization
 *
 *   The initialization sequence is:
 *   1. Parse MADT to discover GIC components (GICD, GICC, GICR, ITS, MSI frame)
 *   2. Detect GIC capabilities (version, system register support)
 *   3. Select interface mode (system registers for v3+, MMIO for v2)
 *   4. Initialize Distributor (disable, configure groups/priorities, enable)
 *   5. Initialize CPU interface (per-CPU)
 *
 * REFERENCES:
 *   - ARM GIC Architecture Specification (IHI 0048/0069)
 *   - ACPI Specification (MADT GIC structures)
 */

#include "gic_internal.h"

/*
 * ============================================================================
 * Local State
 * ============================================================================
 */

/* One-time logging flag */
static BOOLEAN HalpLoggedGicOnce = FALSE;

/*
 * ============================================================================
 * Global GIC Version Information
 * ============================================================================
 */

/* GIC version and capability tracking */
HALP_GIC_VERSION_INFO HalpGicVersionInfo = {0};

/* GICv4 VLPI/vPE state */
BOOLEAN HalpGicHasVlpis = FALSE;
BOOLEAN HalpGicHasDirectLpi = FALSE;
BOOLEAN HalpGicIsGicv4_1 = FALSE;

/* vPE ID allocator state */
RTL_BITMAP HalpGicVpeidBitmap;
PULONG HalpGicVpeidBitmapBuffer = NULL;
ULONG HalpGicVpeidCount = 0;
ULONG HalpGicVpeidAllocated = 0;
KSPIN_LOCK HalpGicVpeidLock;

/* VM tracking */
PHALP_GIC_VM HalpGicVmList = NULL;
ULONG HalpGicVmCount = 0;
KSPIN_LOCK HalpGicVmLock;

/* vPE table (indexed by vPE ID) */
PHALP_GIC_VPE *HalpGicVpeTable = NULL;
ULONG HalpGicVpeTableSize = 0;

/*
 * ============================================================================
 * Boot Option Parsing
 * ============================================================================
 */

static BOOLEAN
HalpHasLoaderOption(
    _In_opt_ PSTR Options,
    _In_ PCSTR Option)
{
    if (!Options || !Option || *Option == '\0')
        return FALSE;

    return (strstr(Options, Option) != NULL);
}

/*
 * ============================================================================
 * MADT Discovery
 * ============================================================================
 */

/*
 * HalpArm64DiscoverGicFromMadt - Parse MADT to discover GIC components
 *
 * Extracts GIC component information from the ACPI MADT (Multiple APIC
 * Description Table) using the halacpi_arm64 infrastructure.
 *
 * Discovers:
 * - GICD: Distributor base address and version
 * - GICC: Per-CPU interface entries (includes per-CPU GICR base for v3)
 * - GICR: Redistributor region (for v3)
 * - ITS: Interrupt Translation Service (for MSI/MSI-X)
 * - MSI Frame: GICv2m MSI frame (legacy MSI for v2)
 *
 * Parameters:
 *   LoaderBlock - Loader parameter block containing ACPI tables
 */
VOID
HalpArm64DiscoverGicFromMadt(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONGLONG Mpidr;
    BOOLEAN FoundGicd = FALSE;
    BOOLEAN FoundGicr = FALSE;
    BOOLEAN FoundCpuGicr = FALSE;
    BOOLEAN FoundGiccEntry = FALSE;
    BOOLEAN FoundGiccBase = FALSE;
    ULONG Cpu = KeGetCurrentProcessorNumber();
    ULONG Index;

    HalRawPuts("[GIC] DiscoverMadt ENTRY\n");

    if (HalpGicParsedMadt)
    {
        HalRawPuts("[GIC] DiscoverMadt already done\n");
        return;
    }

    HalpGicParsedMadt = TRUE;
    if (!LoaderBlock)
    {
        HalRawPuts("[GIC] DiscoverMadt no LoaderBlock\n");
        return;
    }

    Mpidr = HalpReadMpidr();

    /* Parse ACPI tables for ARM64 GIC information */
    HalRawPuts("[GIC] DiscoverMadt ACPI parse\n");
    HalpAcpiDiscoverArm64Tables(LoaderBlock);
    HalRawPuts("[GIC] DiscoverMadt ACPI done\n");

    /* Extract GICD (Distributor) base */
    if (HalpArm64GicInfo.GicdBase)
    {
        HalpGicdBase = HalpArm64GicInfo.GicdBase;
        FoundGicd = TRUE;
    }

    /*
     * Extract GICR (Redistributor) regions.
     * We now support multiple regions for multi-socket systems.
     */
    if (HalpArm64GicInfo.GicrRegionCount > 0 || HalpArm64GicInfo.GicrBase)
    {
        /* Initialize the multi-region redistributor tracking */
        HalpArm64InitRedistRegions();

        /* Set legacy single-region fields for compatibility */
        if (HalpArm64GicInfo.GicrBase)
        {
            HalpGicrRegionBase = HalpArm64GicInfo.GicrBase;
            HalpGicrRegionLength = HalpArm64GicInfo.GicrLength;
        }
        else if (HalpGicRedistRegionCount > 0)
        {
            /* Use first region for legacy code paths */
            HalpGicrRegionBase = HalpGicRedistRegions[0].PhysicalBase.QuadPart;
            HalpGicrRegionLength = HalpGicRedistRegions[0].Length;
        }
        FoundGicr = TRUE;
    }

    /* Extract GICC (CPU Interface) base for legacy mode */
    if (HalpArm64GicInfo.GiccBase)
    {
        HalpGiccBase = HalpArm64GicInfo.GiccBase;
        FoundGiccBase = TRUE;
    }

    /* Process per-CPU GICC entries to find current CPU's GICR base */
    if (HalpArm64GicInfo.GiccEntryCount)
    {
        FoundGiccEntry = TRUE;
        for (Index = 0; Index < HalpArm64GicInfo.GiccEntryCount; ++Index)
        {
            PHALP_ARM64_GICC_ENTRY Entry = &HalpArm64GicInfo.GiccEntries[Index];

            if (!Entry->GicrBase)
                continue;

            if (HalpArm64AffinityFromMpidr(Entry->Mpidr) ==
                HalpArm64AffinityFromMpidr(Mpidr))
            {
                if (Cpu < RTL_NUMBER_OF(HalpGicrCpuBase))
                {
                    HalpGicrCpuBase[Cpu] = (ULONG_PTR)Entry->GicrBase;
                    FoundCpuGicr = TRUE;
                }
                break;
            }
        }
    }

    /* Process ITS entries - now support multiple ITS nodes */
    if (HalpArm64GicInfo.ItsCount)
    {
        ULONG ItsNodeIdx = 0;

        for (Index = 0; Index < HalpArm64GicInfo.ItsCount && ItsNodeIdx < HALP_GIC_MAX_ITS_NODES; ++Index)
        {
            HALP_ARM64_GIC_ITS_ENTRY *ItsEntry = &HalpArm64GicInfo.ItsEntries[Index];
            if (ItsEntry->BaseAddress)
            {
                PHALP_GIC_ITS_NODE ItsNode = &HalpGicItsNodes[ItsNodeIdx];

                /* Initialize the ITS node structure */
                RtlZeroMemory(ItsNode, sizeof(*ItsNode));
                ItsNode->PhysicalBase.QuadPart = ItsEntry->BaseAddress;
                ItsNode->VirtualBase = (ULONG_PTR)ItsEntry->BaseAddress; /* Identity mapping */
                ItsNode->ItsId = ItsEntry->ItsId;
                ItsNode->ListNumber = ItsNodeIdx;
                ItsNode->InitState = 0;
                KeInitializeSpinLock(&ItsNode->CmdLock);
                KeInitializeSpinLock(&ItsNode->DeviceLock);

                /* Set legacy fields for backward compatibility (first ITS only) */
                if (ItsNodeIdx == 0)
                {
                    HalpGicItsBase = ItsEntry->BaseAddress;
                    HalpGicItsId = ItsEntry->ItsId;
                }

                HalpGicItsPresent = TRUE;
                ItsNodeIdx++;
            }
        }

        HalpGicItsNodeCount = ItsNodeIdx;
        if (HalpGicItsNodeCount > 0)
        {
            DPRINT1("[arm64][GIC] MADT: Found %lu ITS node(s)\n", HalpGicItsNodeCount);
        }
    }

    /* Process MSI frame entries (GICv2m) */
    if (HalpArm64GicInfo.MsiFrameCount)
    {
        for (Index = 0; Index < HalpArm64GicInfo.MsiFrameCount; ++Index)
        {
            HALP_ARM64_GIC_MSI_FRAME_ENTRY *Frame = &HalpArm64GicInfo.MsiFrames[Index];
            if (Frame->BaseAddress)
            {
                HalpGicMsiFrameBase = Frame->BaseAddress;
                HalpGicMsiSpiBase = Frame->SpiBase;
                HalpGicMsiSpiCount = Frame->SpiCount;
                HalpGicMsiFlags = Frame->Flags;
                HalpGicMsiPresent = TRUE;
                break;
            }
        }
    }

    /* Log discovery results */
    if (FoundGicd)
    {
        DPRINT1("[arm64][GIC] MADT: GICD @0x%llx\n", HalpGicdBase);
    }
    if (FoundGicr)
    {
        if (HalpGicRedistRegionCount > 1)
        {
            DPRINT1("[arm64][GIC] MADT: %lu GICR regions (multi-socket support)\n",
                    HalpGicRedistRegionCount);
        }
        else
        {
            DPRINT1("[arm64][GIC] MADT: GICR region @0x%llx len=0x%llx\n",
                    HalpGicrRegionBase, HalpGicrRegionLength);
        }
    }
    if (FoundGiccEntry && !FoundGiccBase)
    {
        HalpGiccBase = 0;
        HalpGiccPresent = FALSE;
        DPRINT1("[arm64][GIC] MADT: no GICC base, legacy CPU IF unavailable\n");
    }
    else if (FoundGiccBase)
    {
        HalpGiccPresent = TRUE;
    }
    if (!FoundCpuGicr && HalpGicrRegionBase)
    {
        ULONG_PTR Base = HalpArm64FindGicrForMpidr(Mpidr);
        if (Base && Cpu < RTL_NUMBER_OF(HalpGicrCpuBase))
        {
            HalpGicrCpuBase[Cpu] = Base;
            DPRINT1("[arm64][GIC] MADT: selected GICR CPU base @0x%p\n", (PVOID)Base);
        }
    }

    if (HalpGicItsPresent)
    {
        if (HalpGicItsNodeCount > 1)
        {
            for (Index = 0; Index < HalpGicItsNodeCount; ++Index)
            {
                DPRINT1("[arm64][GIC] MADT: GIC ITS[%lu] id=%lu @0x%llx\n",
                        Index, HalpGicItsNodes[Index].ItsId,
                        HalpGicItsNodes[Index].PhysicalBase.QuadPart);
            }
        }
        else
        {
            DPRINT1("[arm64][GIC] MADT: GIC ITS %lu @0x%llx\n",
                    HalpGicItsId, HalpGicItsBase);
        }
    }
    if (HalpGicMsiPresent)
    {
        DPRINT1("[arm64][GIC] MADT: GIC MSI frame @0x%llx SPI[%u..%u] flags=0x%lx\n",
                HalpGicMsiFrameBase,
                HalpGicMsiSpiBase,
                (ULONG)(HalpGicMsiSpiBase + HalpGicMsiSpiCount - 1),
                HalpGicMsiFlags);
    }

    HalRawPuts("[GIC] DiscoverMadt EXIT\n");
}

/*
 * ============================================================================
 * GIC Interface Selection
 * ============================================================================
 */

/*
 * HalpArm64SelectGicInterface - Select GIC interface mode
 *
 * Determines whether to use GICv3 system registers or GICv2 legacy
 * MMIO interface based on:
 * - Boot options (GICV3, GICV2, etc.)
 * - CPU feature detection (ID_AA64PFR0_EL1.GIC)
 * - MADT information (presence of GICC vs GICR)
 * - Platform quirks (Apple HVF compatibility)
 *
 * Parameters:
 *   LoaderBlock - Loader parameter block containing boot options
 */
VOID
HalpArm64SelectGicInterface(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONGLONG Pfr0 = 0;
    ULONG Pfr0Gic = 0;
    ULONGLONG Midr = 0;
    UCHAR Implementer = 0;

    HalRawPuts("[GIC] SelectIf ENTRY\n");

    if (HalpGicInterfaceSelected)
    {
        HalRawPuts("[GIC] SelectIf already done\n");
        return;
    }

    HalpGicInterfaceSelected = TRUE;

    /* Check for boot options forcing interface mode */
    if (LoaderBlock && LoaderBlock->LoadOptions)
    {
        if (HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICV3") ||
            HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICSYSREG"))
        {
            HalpForceSysRegs = TRUE;
            HalpForceLegacyGic = FALSE;
            DPRINT1("[arm64][GIC] Forcing GICv3 system-register interface via boot option\n");
        }
        else if (HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICV2") ||
                 HalpHasLoaderOption(LoaderBlock->LoadOptions, "NOGICSYSREG") ||
                 HalpHasLoaderOption(LoaderBlock->LoadOptions, "LEGACYGIC"))
        {
            HalpForceLegacyGic = TRUE;
            DPRINT1("[arm64][GIC] Forcing legacy GIC interface via boot option\n");
        }
    }

    /* Discover GIC components from MADT */
    HalRawPuts("[GIC] SelectIf MADT parse\n");
    HalpArm64DiscoverGicFromMadt(LoaderBlock);
    HalRawPuts("[GIC] SelectIf MADT done\n");

    /*
     * Apple HVF quirk: Apple's Hypervisor Framework has issues with GICv3
     * system register IRQ delivery. Prefer legacy GICC if available.
     */
    if (!HalpForceSysRegs && !HalpForceLegacyGic && HalpGiccPresent)
    {
        Midr = HalpReadMidr();
        Implementer = (UCHAR)((Midr >> 24) & 0xFF);
        if (Implementer == 0x61) /* Apple */
        {
            HalpForceLegacyGic = TRUE;
            DPRINT1("[arm64][GIC] Apple MIDR detected; preferring legacy GIC for HVF compatibility\n");
        }
    }

    /* If GICC is not present, must use system registers */
    if (!HalpGiccPresent)
    {
        if (HalpForceLegacyGic)
        {
            DPRINT1("[arm64][GIC] Legacy GIC forced but MADT reports no GICC; using sysregs\n");
        }
        HalpForceLegacyGic = FALSE;
        HalpForceSysRegs = TRUE;
    }

    /* Read CPU GIC capabilities from ID_AA64PFR0_EL1 */
    Pfr0 = HalpReadPfr0();
    Pfr0Gic = (ULONG)((Pfr0 >> 24) & 0xF);

    /* Attempt to enable system register interface */
    if (HalpForceSysRegs)
    {
        ULONG Sre = HalpReadIccSre();
        Sre |= 0x1;
        HalpWriteIccSre(Sre);
        Sre = HalpReadIccSre();
        if (Sre & 0x1)
        {
            HalpGicUseSysRegs = TRUE;
        }
        else
        {
            HalpGicUseSysRegs = FALSE;
            if (!HalpGiccPresent)
            {
                DPRINT1("[arm64][GIC] GICV3 forced but SRE enable failed and no GICC present\n");
            }
            else
            {
                DPRINT1("[arm64][GIC] GICV3 forced but SRE enable failed, falling back to legacy\n");
            }
        }
    }
    else if (!HalpForceLegacyGic && Pfr0Gic >= 1)
    {
        /* Auto-detect: try to enable system registers if CPU supports them */
        ULONG Sre = HalpReadIccSre();
        Sre |= 0x1;
        HalpWriteIccSre(Sre);
        Sre = HalpReadIccSre();
        if (Sre & 0x1)
        {
            HalpGicUseSysRegs = TRUE;
        }
        else
        {
            HalpGicUseSysRegs = FALSE;
        }
    }
    else
    {
        HalpGicUseSysRegs = FALSE;
    }

    DPRINT1("[arm64][GIC] Interface selection: %s (PFR0.GIC=%lu)\n",
            HalpGicUseSysRegs ? "GICv3 system-registers" : "GICv2 legacy (GICC)",
            Pfr0Gic);
}

/*
 * ============================================================================
 * Distributor Initialization
 * ============================================================================
 */

/*
 * HalpInitGicDistributor - Initialize the GIC Distributor
 *
 * Configures the GICD registers:
 * 1. Disables the distributor
 * 2. Configures interrupt groups (Group 0 for v2, Group 1 for v3)
 * 3. Sets interrupt priorities
 * 4. Configures SPI routing (targets for v2, affinity for v3)
 * 5. Enables the distributor
 *
 * Parameters:
 *   GicVersion - GIC architecture version (2 or 3+)
 *   UseSysRegs - TRUE if using system register interface
 */
VOID
HalpInitGicDistributor(
    _In_ ULONG GicVersion,
    _In_ BOOLEAN UseSysRegs)
{
    ULONG Typer;
    ULONG Lines;
    ULONG Nregs;
    ULONG i;

    DPRINT1("[arm64][GIC] Initializing distributor (version=%lu, sysregs=%lu)\n",
            GicVersion, UseSysRegs ? 1UL : 0UL);

    /* Disable distributor while we configure */
    *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR) = 0;

    /* Determine number of interrupt lines */
    Typer = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_TYPER);
    Lines = 32 * ((Typer & GICD_TYPER_ITLINES_MASK) + 1);
    if (Lines > 1020)
        Lines = 1020;
    Nregs = (Lines + 31) / 32;

    DPRINT1("[arm64][GIC] GICD typer=0x%08lx lines=%lu nregs=%lu\n",
            Typer, Lines, Nregs);

    /*
     * Configure SPI interrupt groups and clear pending.
     *
     * For GIC-v3 with system registers: Use Group 1 (non-secure) as standard.
     * For GIC-v2 (MMIO interface): Use Group 0 because QEMU HVF has a bug
     * where enabling Group 1 via GICD_CTLR causes a hang.
     */
    for (i = 1; i < Nregs; ++i) /* Start at 1 to skip SGI/PPI */
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICENABLER + i * 4) = 0xFFFFFFFF;
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICPENDR + i * 4) = 0xFFFFFFFF;
        if (UseSysRegs)
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IGROUPR + i * 4) = 0xFFFFFFFF; /* G1 */
        }
        else
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IGROUPR + i * 4) = 0x00000000; /* G0 */
        }
    }

    /* Set priorities to medium (0xA0) */
    for (i = 32; i < Lines; i += 4)
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IPRIORITYR + (i & ~3)) = 0xA0A0A0A0;
    }

    /* Configure SPI routing */
    if (UseSysRegs)
    {
        /*
         * GICv3: Initialize affinity tracking and route SPIs.
         *
         * For Windows 11 ARM64 GIC compatibility, we support dynamic
         * IRQ affinity routing. During initial setup, we route all SPIs
         * to the boot CPU (CPU 0). Later, when all CPUs are online,
         * HalpGicv3SetSpiAffinityRoundRobin can be called to distribute
         * interrupts across CPUs for load balancing.
         */
        HalpGicv3InitAffinityTracking();
        HalpInitGicv3SpiRouting(Lines);
    }
    else
    {
        /* GICv2: Use CPU targets via GICD_ITARGETSR */
        HalpInitGicv2SpiTargets(Lines);
    }

    /*
     * Configure SGI/PPI bank for GICv2.
     * For GICv3, this is handled via the Redistributor.
     */
    if (!UseSysRegs)
    {
        HalpInitGicv2SgiPpi();
    }

    /* Memory barrier before enabling distributor */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * Enable the distributor.
     *
     * For GICv2: Only enable Group 0 due to QEMU HVF bug.
     * For GICv3: Enable both groups.
     */
    if (UseSysRegs)
    {
        /* GICv3: EnableGrp0 + EnableGrp1NS + ARE_NS */
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR) =
            GICD_CTLR_ENABLE_GRP0 | GICD_CTLR_ENABLE_GRP1_NS | GICD_CTLR_ARE_NS;
    }
    else
    {
        /* GICv2: EnableGrp0 only */
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR) = GICD_CTLR_ENABLE_GRP0;
    }

    __asm__ __volatile__("dsb sy" ::: "memory");

    DPRINT1("[arm64][GIC] Distributor initialized and enabled\n");
}

/*
 * ============================================================================
 * GIC Version Detection (GICv3.1/v4 Feature Detection)
 * ============================================================================
 */

/*
 * HalpGicDetectVersion - Detect GIC version and capabilities
 *
 * Reads GICD_PIDR2, GICD_TYPER, and GICR_TYPER to populate
 * the HalpGicVersionInfo structure with detected capabilities.
 *
 * Following Linux's approach in drivers/irqchip/irq-gic-v3.c:
 * - gic_enable_quirks() for version detection
 * - gic_dist_init() for TYPER parsing
 */
VOID
HalpGicDetectVersion(VOID)
{
    ULONG Pidr2;
    ULONG Typer;
    ULONG ArchRev;
    ULONG ItLines;
    ULONG_PTR GicrBase;
    ULONGLONG GicrTyper;
    ULONG Index;
    BOOLEAN AllHaveVlpis = TRUE;
    BOOLEAN AnyHaveRvpeid = FALSE;

    if (HalpGicVersionInfo.Initialized)
        return;

    if (HalpGicdBase == 0)
    {
        DPRINT1("[arm64][GIC] Cannot detect version: GICD base not set\n");
        return;
    }

    /*
     * Read GICD_PIDR2 to determine GIC architecture version.
     * Bits [7:4] contain ArchRev:
     *   0x1 = GICv1
     *   0x2 = GICv2
     *   0x3 = GICv3
     *   0x4 = GICv4
     */
    Pidr2 = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_PIDR2);
    ArchRev = (Pidr2 >> GICD_PIDR2_ARCHREV_SHIFT) & 0xF;

    HalpGicVersionInfo.Architecture = ArchRev;

    /*
     * Read GICD_TYPER for interrupt line count and features.
     */
    Typer = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_TYPER);

    /* Calculate total interrupt lines */
    ItLines = 32 * ((Typer & GICD_TYPER_ITLINES_MASK) + 1);
    if (ItLines > 1020)
        ItLines = 1020;

    HalpGicVersionInfo.TotalInterruptLines = ItLines;
    HalpGicVersionInfo.MaxSpiId = ItLines - 1;

    /*
     * Detect GICv3.1 features from GICD_TYPER.
     */
    if (Typer & GICD_TYPER_ESPI)
    {
        /* Extended SPI range supported (GICv3.1+) */
        ULONG EspiRange = (Typer >> GICD_TYPER_ESPI_RANGE_SHIFT) & GICD_TYPER_ESPI_RANGE_MASK;
        HalpGicVersionInfo.HasExtendedSpiRange = TRUE;
        HalpGicVersionInfo.ExtendedSpiStart = 4096;
        HalpGicVersionInfo.ExtendedSpiCount = 32 * (EspiRange + 1);
        HalpGicVersionInfo.MaxSpiId = 4096 + HalpGicVersionInfo.ExtendedSpiCount - 1;
    }

    if (Typer & GICD_TYPER_MBIS)
    {
        HalpGicVersionInfo.HasMessageBasedSpi = TRUE;
    }

    if (Typer & GICD_TYPER_RSS)
    {
        HalpGicVersionInfo.HasRangeSelector = TRUE;
    }

    if (Typer & GICD_TYPER_LPIS)
    {
        ULONG LpiBits = ((Typer >> GICD_TYPER_NUM_LPIS_SHIFT) & GICD_TYPER_NUM_LPIS_MASK) + 1;
        if (LpiBits > 0)
        {
            HalpGicVersionInfo.MaxLpiId = (1UL << (LpiBits + 8192)) - 1;
        }
    }

    /*
     * Scan redistributors to detect GICv4 features.
     *
     * Following Linux's gic_populate_rdist():
     * - Check GICR_TYPER.VLPIS for VLPI support
     * - Check GICR_TYPER.DirectLPI for direct injection
     * - Check GICR_TYPER.RVPEID for GICv4.1
     */
    if (HalpGicRedistRegionCount > 0 || HalpGicrRegionBase != 0)
    {
        /* Check the boot CPU's redistributor first */
        ULONG Cpu = KeGetCurrentProcessorNumber();
        GicrBase = HalpGicrBase(Cpu);

        if (GicrBase != 0)
        {
            GicrTyper = HalpMmioRead64(GicrBase, GICR_TYPER);

            if (GicrTyper & HALP_GICR_TYPER_VLPIS)
            {
                HalpGicVersionInfo.HasVlpis = TRUE;
            }
            else
            {
                AllHaveVlpis = FALSE;
            }

            if (GicrTyper & HALP_GICR_TYPER_DIRECTLPI)
            {
                HalpGicVersionInfo.HasDirectLpi = TRUE;
            }

            if (GicrTyper & GICR_TYPER_RVPEID)
            {
                AnyHaveRvpeid = TRUE;
            }
        }
    }

    /*
     * Check ITS nodes for virtual LPI support.
     */
    if (HalpGicItsNodeCount > 0)
    {
        for (Index = 0; Index < HalpGicItsNodeCount; Index++)
        {
            PHALP_GIC_ITS_NODE ItsNode = &HalpGicItsNodes[Index];
            ULONGLONG ItsTyper;

            if (ItsNode->VirtualBase == 0)
                continue;

            ItsTyper = HalpMmioRead64(ItsNode->VirtualBase, GITS_TYPER);

            if (ItsTyper & GITS_TYPER_VIRTUAL)
            {
                HalpGicVersionInfo.HasItsVirtual = TRUE;

                /* Get vPE ID bits from GITS_TYPER */
                HalpGicVersionInfo.VpeidBits =
                    ((ItsTyper >> GITS_TYPER_VPEID_BITS_SHIFT) & GITS_TYPER_VPEID_BITS_MASK) + 1;

                if (HalpGicVersionInfo.VpeidBits > HALP_GIC_MAX_VPEID_BITS)
                    HalpGicVersionInfo.VpeidBits = HALP_GIC_MAX_VPEID_BITS;

                HalpGicVersionInfo.MaxVpeid = (1UL << HalpGicVersionInfo.VpeidBits) - 1;
                break;  /* Use first ITS that supports VLPIs */
            }
        }
    }

    /* Set global VLPI support flags */
    HalpGicHasVlpis = HalpGicVersionInfo.HasVlpis &&
                       HalpGicVersionInfo.HasItsVirtual &&
                       AllHaveVlpis;
    HalpGicHasDirectLpi = HalpGicVersionInfo.HasDirectLpi;
    HalpGicIsGicv4_1 = AnyHaveRvpeid;

    if (HalpGicIsGicv4_1)
    {
        HalpGicVersionInfo.HasRvpeid = TRUE;
    }

    HalpGicVersionInfo.Initialized = TRUE;

    /* Log detected capabilities */
    DPRINT1("[arm64][GIC] Version detection complete:\n");
    DPRINT1("[arm64][GIC]   Architecture: GICv%lu\n", ArchRev);
    DPRINT1("[arm64][GIC]   Interrupt lines: %lu (max SPI INTID: %lu)\n",
            HalpGicVersionInfo.TotalInterruptLines, HalpGicVersionInfo.MaxSpiId);

    if (HalpGicVersionInfo.HasExtendedSpiRange)
    {
        DPRINT1("[arm64][GIC]   Extended SPI: %lu SPIs starting at INTID %lu\n",
                HalpGicVersionInfo.ExtendedSpiCount, HalpGicVersionInfo.ExtendedSpiStart);
    }

    if (HalpGicVersionInfo.HasMessageBasedSpi)
    {
        DPRINT1("[arm64][GIC]   Message-based SPIs: supported\n");
    }

    if (HalpGicHasVlpis)
    {
        DPRINT1("[arm64][GIC]   GICv4 VLPIs: supported (vPE ID bits: %lu, max vPEs: %lu)\n",
                HalpGicVersionInfo.VpeidBits, HalpGicVersionInfo.MaxVpeid + 1);
    }

    if (HalpGicHasDirectLpi)
    {
        DPRINT1("[arm64][GIC]   Direct LPI injection: supported\n");
    }

    if (HalpGicIsGicv4_1)
    {
        DPRINT1("[arm64][GIC]   GICv4.1 features: supported\n");
    }
}

/*
 * HalpGicHasVlpiSupport - Check if VLPI support is available
 */
BOOLEAN
HalpGicHasVlpiSupport(VOID)
{
    if (!HalpGicVersionInfo.Initialized)
        HalpGicDetectVersion();

    return HalpGicHasVlpis;
}

/*
 * HalpGicHasExtendedSpi - Check if extended SPI range is available
 */
BOOLEAN
HalpGicHasExtendedSpi(VOID)
{
    if (!HalpGicVersionInfo.Initialized)
        HalpGicDetectVersion();

    return HalpGicVersionInfo.HasExtendedSpiRange;
}

/*
 * ============================================================================
 * HAL GIC Initialization Entry Point
 * ============================================================================
 */

/*
 * HalpInitGic - Main GIC initialization entry point
 *
 * Called from HalInitSystem() during HAL phase 0 initialization.
 * Performs complete GIC initialization:
 * 1. Interface selection
 * 2. GIC version detection
 * 3. Distributor initialization
 * 4. CPU interface initialization
 *
 * Parameters:
 *   LoaderBlock - Loader parameter block
 *
 * Returns:
 *   TRUE on success, FALSE on failure.
 */
BOOLEAN
HalpInitGic(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONGLONG Pfr0;
    ULONG Pfr0Gic;
    ULONG Pidr2;

    /* Select GIC interface (system registers vs legacy) */
    HalpArm64SelectGicInterface(LoaderBlock);

    /* Probe GIC capabilities */
    Pfr0 = HalpReadPfr0();
    Pfr0Gic = (ULONG)((Pfr0 >> 24) & 0xF);

    /* Read distributor architecture revision from GICD_PIDR2 */
    Pidr2 = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_PIDR2);
    HalpGicArchRev = ((Pidr2 >> 4) & 0xF);
    if (HalpGicArchRev == 0)
        HalpGicArchRev = HalpGicUseSysRegs ? 3 : 2;

    DPRINT1("[arm64][GIC] GIC probe: PFR0.GIC=%lu SRE=%lu PIDR2=0x%08lx ARCHREV=%lu\n",
            Pfr0Gic,
            HalpGicUseSysRegs ? 1UL : 0UL,
            Pidr2,
            HalpGicArchRev);
    DPRINT1("[arm64][GIC] Using %s CPU interface\n",
            HalpGicUseSysRegs ? "GICv3 system-register" : "GICv2 legacy (GICC)");

    /* Initialize the Distributor */
    HalpInitGicDistributor(HalpGicArchRev, HalpGicUseSysRegs);

    /* Initialize CPU interface */
    if (HalpGicUseSysRegs)
    {
        HalpInitGicRedistributor(KeGetCurrentProcessorNumber());
        HalpInitGicv3CpuInterface();
    }
    else
    {
        HalpInitGicv2CpuInterface();
    }

    /*
     * Detect GIC version and capabilities after basic initialization.
     * This populates HalpGicVersionInfo with GICv3.1/v4 feature information.
     */
    HalpGicDetectVersion();

    /*
     * Initialize vPE/VLPI tracking locks for GICv4.
     * These are needed even if GICv4 is not present to avoid
     * uninitialized spinlock issues.
     */
    KeInitializeSpinLock(&HalpGicVpeidLock);
    KeInitializeSpinLock(&HalpGicVmLock);

    return TRUE;
}

/*
 * HalpGicLogStatus - Log GIC status (called after KD is initialized)
 *
 * Called from HalInitSystem() phase 1 to log GIC configuration
 * after the debugger is available.
 */
VOID
HalpGicLogStatus(VOID)
{
    if (HalpLoggedGicOnce)
        return;

    HalpLoggedGicOnce = TRUE;

    DPRINT1("[arm64][GIC] GIC probe: ARCHREV=%lu\n", HalpGicArchRev);
    DPRINT1("[arm64][GIC] Using %s CPU interface\n",
            HalpGicUseSysRegs ? "GICv3 system-register" : "GICv2 legacy (GICC)");

    if (HalpGicItsPresent)
    {
        DPRINT1("[arm64][GIC] ITS present at 0x%llx\n", HalpGicItsBase);
    }
    if (HalpGicMsiPresent)
    {
        DPRINT1("[arm64][GIC] GICv2m MSI frame at 0x%llx\n", HalpGicMsiFrameBase);
    }
}
