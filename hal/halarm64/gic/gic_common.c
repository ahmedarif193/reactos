/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/gic/gic_common.c
 * PURPOSE:         Common GIC (Generic Interrupt Controller) Functions
 *
 * DESCRIPTION:
 *   This file contains GIC functionality that is shared between GICv2 and GICv3
 *   implementations, including:
 *   - Interrupt enable/disable operations
 *   - Interrupt trigger mode configuration
 *   - Priority configuration
 *   - SGI (Software Generated Interrupt) sending
 *   - Cache maintenance helpers
 *   - Unified interrupt acknowledge/EOI wrappers
 *
 * REFERENCES:
 *   - ARM GIC Architecture Specification (IHI 0069)
 */

#include "gic_internal.h"

/*
 * ============================================================================
 * Global State Variables
 * ============================================================================
 */

/*
 * GIC base addresses.
 *
 * GICD and GICC are initialized to QEMU virt defaults so that early Phase 0
 * code works before MADT parsing.  HalpArm64DiscoverGicFromMadt() overwrites
 * these with ACPI-provided values.
 *
 * GICR (redistributor) is GICv3-only.  Initializing it to a non-zero default
 * is dangerous on GICv2 systems because stale addresses could trigger MMIO
 * faults.  Leave it at 0; MADT parsing populates it when a GICR entry exists.
 */
ULONGLONG HalpGicdBase = HAL_ARM64_GICD_BASE_DEFAULT;
ULONGLONG HalpGiccBase = HAL_ARM64_GICC_BASE_DEFAULT;
ULONGLONG HalpGicrRegionBase = 0;                            /* Populated from MADT GICR entries */
ULONGLONG HalpGicrRegionLength = 0;
ULONG HalpGicrStride = HAL_ARM64_GICR_STRIDE_DEFAULT;
ULONG HalpGicrSgiOffset = HAL_ARM64_GICR_SGI_OFFSET_DEFAULT;
ULONG_PTR HalpGicrCpuBase[MAXIMUM_PROCESSORS] = {0};

/*
 * Multi-region redistributor tracking (Windows 11 ARM64 / Linux compatible)
 * These arrays track ALL GICR regions from ACPI MADT for multi-socket support.
 */
HALP_GIC_REDIST_REGION HalpGicRedistRegions[HALP_GIC_MAX_REDIST_REGIONS];
ULONG HalpGicRedistRegionCount = 0;
ULONG HalpGicRedistStride = 0;  /* 0 = auto-detect from GICR_TYPER.VLPIS */
BOOLEAN HalpGicRedistHasVlpis = FALSE;
BOOLEAN HalpGicRedistHasDirectLpi = FALSE;

/* GIC ITS state */
ULONGLONG HalpGicItsBase = 0;
ULONG HalpGicItsId = 0;
BOOLEAN HalpGicItsPresent = FALSE;
BOOLEAN HalpGicItsEnabled = FALSE;
KSPIN_LOCK HalpGicItsLock;

/* GIC MSI (v2m) state */
ULONGLONG HalpGicMsiFrameBase = 0;
USHORT HalpGicMsiSpiBase = 0;
USHORT HalpGicMsiSpiCount = 0;
ULONG HalpGicMsiFlags = 0;
BOOLEAN HalpGicMsiPresent = FALSE;

/* GIC detection state */
BOOLEAN HalpGicUseSysRegs = FALSE;
BOOLEAN HalpForceSysRegs = FALSE;
BOOLEAN HalpForceLegacyGic = FALSE;
ULONG HalpGicArchRev = 0;
BOOLEAN HalpGicParsedMadt = FALSE;
BOOLEAN HalpGicInterfaceSelected = FALSE;
BOOLEAN HalpGiccPresent = TRUE;

/* LPI state */
ULONG HalpGicLpiCount = 0;

/* Active interrupt tracking */
ULONG HalpArm64ActiveIntId[MAXIMUM_PROCESSORS] = {0};

/* Default interrupt affinity */
KAFFINITY HalpDefaultInterruptAffinity = 1;

/* Memory mapping mode */
BOOLEAN HalpUseIdentityMapping = TRUE;

/* Cache line size (cached for performance) */
static ULONG HalpGicCacheLineSize = 0;

/*
 * ============================================================================
 * IRQ Affinity Tracking State (GICv3 Dynamic Affinity Routing)
 * ============================================================================
 *
 * Windows 11 ARM64 GIC compatibility requires dynamic IRQ affinity routing.
 * This allows SPIs to be dynamically routed to any CPU, supporting:
 * - SMP load balancing
 * - CPU hotplug scenarios
 * - Per-IRQ affinity control via Windows HAL APIs
 *
 * Each SPI (interrupt IDs 32-1019) can be individually routed to a specific
 * CPU by programming the corresponding GICD_IROUTER register.
 *
 * The tracking structure stores:
 * - Target CPU index for each SPI
 * - MPIDR value for each online CPU (for IROUTER programming)
 * - Spinlock for thread-safe affinity updates
 */

/* Maximum number of SPIs we track (INTIDs 32-1019 = 988 SPIs) */
#define HALP_GIC_MAX_SPI_COUNT 988

/* Array tracking target CPU for each SPI (indexed by INTID - 32) */
ULONG HalpGicSpiAffinityTarget[HALP_GIC_MAX_SPI_COUNT] = {0};

/* Array storing MPIDR values for each CPU (indexed by CPU number) */
ULONGLONG HalpGicCpuMpidr[MAXIMUM_PROCESSORS] = {0};

/* Spinlock protecting affinity updates */
KSPIN_LOCK HalpGicAffinityLock;

/* Flag indicating if affinity tracking is initialized */
BOOLEAN HalpGicAffinityInitialized = FALSE;

/* Number of online CPUs (for round-robin distribution) */
volatile LONG HalpGicOnlineCpuCount = 1;

/*
 * ============================================================================
 * Cache Maintenance Functions
 * ============================================================================
 */

/*
 * HalpArm64GetCacheLineSize - Get the data cache line size
 *
 * Reads the CTR_EL0 register to determine the minimum data cache line size.
 * The result is cached for subsequent calls.
 *
 * Returns:
 *   Cache line size in bytes (typically 64 bytes on modern ARM64)
 */
ULONG
HalpArm64GetCacheLineSize(VOID)
{
    ULONGLONG Ctr;
    ULONG Line;

    if (HalpGicCacheLineSize != 0)
        return HalpGicCacheLineSize;

    /* Read Cache Type Register */
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));

    /* DminLine is bits [19:16], gives log2 of words in minimum D-cache line */
    Line = 4u << ((ULONG)((Ctr >> 16) & 0xF));

    /* Validate the result - use 64 as fallback if invalid */
    if ((Line & (Line - 1)) != 0 || Line < 16)
        Line = 64;

    HalpGicCacheLineSize = Line;
    return Line;
}

/*
 * HalpArm64CleanDcacheRange - Clean data cache range
 *
 * Cleans (writes back) the specified memory range from the data cache.
 * This ensures that data written by the CPU is visible to other observers
 * (like DMA devices).
 *
 * Parameters:
 *   Base   - Virtual address of the start of the range
 *   Length - Number of bytes to clean
 */
VOID
HalpArm64CleanDcacheRange(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    ULONG_PTR Start;
    ULONG_PTR End;
    ULONG Line;

    if (!Base || Length == 0)
        return;

    Line = HalpArm64GetCacheLineSize();
    Start = (ULONG_PTR)Base & ~((ULONG_PTR)Line - 1);
    End = (ULONG_PTR)Base + Length;

    for (ULONG_PTR Address = Start; Address < End; Address += Line)
    {
        /* DC CVAC - Clean by VA to Point of Coherency */
        __asm__ __volatile__("dc cvac, %0" :: "r"(Address) : "memory");
    }

    /* Ensure all cache operations complete before continuing */
    __asm__ __volatile__("dsb ish" ::: "memory");
}

/*
 * HalpArm64CleanInvalidateDcacheRange - Clean and invalidate data cache range
 *
 * Cleans and invalidates the specified memory range from the data cache.
 * This is used before reading data that may have been modified by DMA.
 *
 * Parameters:
 *   Base   - Virtual address of the start of the range
 *   Length - Number of bytes to clean and invalidate
 */
VOID
HalpArm64CleanInvalidateDcacheRange(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    ULONG_PTR Start;
    ULONG_PTR End;
    ULONG Line;

    if (!Base || Length == 0)
        return;

    Line = HalpArm64GetCacheLineSize();
    Start = (ULONG_PTR)Base & ~((ULONG_PTR)Line - 1);
    End = (ULONG_PTR)Base + Length;

    /* Barrier before invalidate to ensure ordering */
    __asm__ __volatile__("dsb ish" ::: "memory");

    for (ULONG_PTR Address = Start; Address < End; Address += Line)
    {
        /* DC CIVAC - Clean and Invalidate by VA to Point of Coherency */
        __asm__ __volatile__("dc civac, %0" :: "r"(Address) : "memory");
    }

    /* Ensure all cache operations complete */
    __asm__ __volatile__("dsb ish" ::: "memory");
}

/*
 * ============================================================================
 * GIC Trigger Mode Configuration
 * ============================================================================
 */

/*
 * HalpArm64ProgramGicTrigger - Program GIC interrupt trigger mode
 *
 * Configures the trigger mode (edge vs level) for a GIC interrupt.
 * Programs the appropriate ICFGR register in either the distributor (for SPIs)
 * or the redistributor (for PPIs).
 *
 * GIC ICFGR register format:
 *   Each interrupt uses 2 bits:
 *     Bit [2n+1]: 0 = Level-sensitive, 1 = Edge-triggered
 *     Bit [2n]:   Reserved (should be written as 0)
 *
 * Parameters:
 *   IntId         - GIC interrupt ID (INTID)
 *   EdgeTriggered - TRUE for edge-triggered, FALSE for level-triggered
 *
 * Notes:
 *   - SGIs (0-15) have fixed edge-triggered configuration
 *   - PPIs (16-31) are configured via GICR_ICFGR1 in the redistributor
 *   - SPIs (32-1019) are configured via GICD_ICFGR in the distributor
 *   - LPIs (8192+) are always edge-triggered
 */
/*
 * Note: The main implementation of HalpArm64ProgramGicTrigger is still
 * in halarm64.c for now. This is a duplicate that can be enabled once
 * the migration is complete.
 */
#if 0 /* Disabled - implementation still in halarm64.c */
VOID
NTAPI
HalpArm64ProgramGicTrigger(
    _In_ ULONG IntId,
    _In_ BOOLEAN EdgeTriggered)
{
    ULONG RegOffset;
    ULONG BitPos;
    ULONG OldVal;
    ULONG NewVal;
    volatile ULONG *RegPtr;

    /*
     * SGIs (0-15): Fixed edge-triggered, cannot be reprogrammed.
     */
    if (IntId < 16)
    {
        DPRINT("[arm64][GIC] SGI %lu: trigger mode is fixed (edge-triggered)\n", IntId);
        return;
    }

    /*
     * PPIs (16-31): Configured via GICR_ICFGR1 in the redistributor.
     */
    if (IntId < 32)
    {
        ULONG Cpu = KeGetCurrentProcessorNumber();
        ULONG_PTR SgiBase = HalpGicrSgiBase(Cpu);

        if (SgiBase == 0)
        {
            DPRINT1("[arm64][GIC] PPI %lu: GICR SGI base unavailable for CPU %lu\n", IntId, Cpu);
            return;
        }

        /* Calculate bit position within GICR_ICFGR1 */
        BitPos = ((IntId - 16) % 16) * 2;

        RegPtr = HalpMmio(SgiBase, GICR_ICFGR1);
        OldVal = *RegPtr;

        /* Clear the 2-bit field and set new value */
        NewVal = OldVal & ~(3u << BitPos);
        if (EdgeTriggered)
        {
            NewVal |= (2u << BitPos);  /* Bit 1 = edge-triggered */
        }

        *RegPtr = NewVal;
        __asm__ __volatile__("dsb sy" ::: "memory");

        DPRINT("[arm64][GIC] PPI %lu: GICR_ICFGR1 0x%08lx -> 0x%08lx (%s)\n",
               IntId, OldVal, NewVal, EdgeTriggered ? "edge" : "level");
        return;
    }

    /*
     * SPIs (32-1019): Configured via GICD_ICFGR in the distributor.
     */
    if (IntId < 1020)
    {
        RegOffset = GICD_ICFGR + (IntId / 16) * 4;
        BitPos = (IntId % 16) * 2;

        RegPtr = HalpMmio((ULONG_PTR)HalpGicdBase, RegOffset);
        OldVal = *RegPtr;

        NewVal = OldVal & ~(3u << BitPos);
        if (EdgeTriggered)
        {
            NewVal |= (2u << BitPos);
        }

        *RegPtr = NewVal;
        __asm__ __volatile__("dsb sy" ::: "memory");

        DPRINT("[arm64][GIC] SPI %lu: GICD_ICFGR[0x%03lx] 0x%08lx -> 0x%08lx (%s)\n",
               IntId, RegOffset, OldVal, NewVal, EdgeTriggered ? "edge" : "level");
        return;
    }

    /*
     * Reserved range (1020-8191): Log and return.
     */
    if (IntId < 8192)
    {
        DPRINT1("[arm64][GIC] INTID %lu is in reserved/invalid range\n", IntId);
        return;
    }

    /*
     * LPIs (8192+): Always edge-triggered.
     */
    DPRINT("[arm64][GIC] LPI %lu: trigger mode is fixed (edge-triggered)\n", IntId);
}
#endif /* Disabled - implementation still in halarm64.c */

/*
 * ============================================================================
 * SGI (Software Generated Interrupt) Functions
 * ============================================================================
 */

/*
 * HalpArm64SendSgi - Send a Software Generated Interrupt
 *
 * Sends an SGI to the specified target processor(s). Uses GICv3 system
 * registers if available, otherwise falls back to GICv2 MMIO.
 *
 * Parameters:
 *   TargetSet - Bitmask of target processors
 *   SgiId     - SGI number (0-15)
 */
VOID
HalpArm64SendSgi(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG SgiId)
{
    ULONG TargetList;

    if ((TargetSet == 0) || (SgiId > 15))
        return;

    if (HalpGicUseSysRegs)
    {
        /* GICv3: Use ICC_SGI1R_EL1 system register */
        HalpGicv3SendSgi(TargetSet, SgiId);
    }
    else
    {
        /* GICv2: Use GICD_SGIR MMIO register */
        TargetList = (ULONG)(TargetSet & 0xFF);
        if (TargetList == 0)
            return;

        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_SGIR) = (SgiId & 0xF) | (TargetList << 16);
    }
}

/*
 * ============================================================================
 * Unified Interrupt Acknowledge / EOI Functions
 * ============================================================================
 */

/*
 * HalpGicAcknowledgeInterrupt - Acknowledge an interrupt
 *
 * Reads the interrupt acknowledge register to get the interrupt ID.
 * Uses the appropriate mechanism based on GIC version.
 *
 * Returns:
 *   The interrupt ID (INTID) of the highest-priority pending interrupt,
 *   or 1023 (spurious) if no interrupt is pending.
 */
ULONG
HalpGicAcknowledgeInterrupt(VOID)
{
    if (HalpGicUseSysRegs)
    {
        return HalpGicv3AcknowledgeInterrupt();
    }
    else
    {
        return HalpGicv2AcknowledgeInterrupt();
    }
}

/*
 * HalpGicEndInterrupt - Signal End-Of-Interrupt
 *
 * Writes to the EOI register to indicate that interrupt handling is complete.
 *
 * Parameters:
 *   IntId - The interrupt ID that was acknowledged
 */
VOID
HalpGicEndInterrupt(
    _In_ ULONG IntId)
{
    if (HalpGicUseSysRegs)
    {
        HalpGicv3EndInterrupt(IntId);
    }
    else
    {
        HalpGicv2EndInterrupt(IntId);
    }
}

/*
 * ============================================================================
 * Interrupt Enable / Disable Functions
 * ============================================================================
 */

/*
 * HalpGicEnableInterrupt - Enable an interrupt in the GIC
 *
 * Sets the enable bit for the specified interrupt. For SPIs (32+), this
 * writes to GICD_ISENABLER. For SGIs/PPIs (0-31), this writes to either
 * GICD_ISENABLER0 (GICv2) or GICR_ISENABLER0 (GICv3).
 *
 * Parameters:
 *   IntId - The interrupt ID to enable
 */
VOID
HalpGicEnableInterrupt(
    _In_ ULONG IntId)
{
    ULONG RegOffset;
    ULONG BitPos;

    if (IntId >= 1020 && IntId < 8192)
    {
        DPRINT1("[arm64][GIC] Cannot enable reserved INTID %lu\n", IntId);
        return;
    }

    if (IntId < 32)
    {
        /* SGI/PPI: Use redistributor for GICv3, distributor for GICv2 */
        if (HalpGicUseSysRegs)
        {
            ULONG Cpu = KeGetCurrentProcessorNumber();
            ULONG_PTR SgiBase = HalpGicrSgiBase(Cpu);

            if (SgiBase == 0)
            {
                DPRINT1("[arm64][GIC] Cannot enable INTID %lu: GICR not available\n", IntId);
                return;
            }

            *HalpMmio(SgiBase, GICR_ISENABLER0) = (1u << IntId);
        }
        else
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ISENABLER) = (1u << IntId);
        }
    }
    else if (IntId < 1020)
    {
        /* SPI: Use distributor */
        RegOffset = GICD_ISENABLER + (IntId / 32) * 4;
        BitPos = IntId % 32;

        *HalpMmio((ULONG_PTR)HalpGicdBase, RegOffset) = (1u << BitPos);
    }
    else
    {
        /* LPI: Enabled via ITS configuration table */
        HalpGicItsEnableLpi(IntId - HAL_ARM64_LPI_BASE);
    }

    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * HalpGicDisableInterrupt - Disable an interrupt in the GIC
 *
 * Clears the enable bit for the specified interrupt.
 *
 * Parameters:
 *   IntId - The interrupt ID to disable
 */
VOID
HalpGicDisableInterrupt(
    _In_ ULONG IntId)
{
    ULONG RegOffset;
    ULONG BitPos;

    if (IntId >= 1020 && IntId < 8192)
    {
        DPRINT1("[arm64][GIC] Cannot disable reserved INTID %lu\n", IntId);
        return;
    }

    if (IntId < 32)
    {
        /* SGI/PPI: Use redistributor for GICv3, distributor for GICv2 */
        if (HalpGicUseSysRegs)
        {
            ULONG Cpu = KeGetCurrentProcessorNumber();
            ULONG_PTR SgiBase = HalpGicrSgiBase(Cpu);

            if (SgiBase == 0)
            {
                DPRINT1("[arm64][GIC] Cannot disable INTID %lu: GICR not available\n", IntId);
                return;
            }

            *HalpMmio(SgiBase, GICR_ICENABLER0) = (1u << IntId);
        }
        else
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICENABLER) = (1u << IntId);
        }
    }
    else if (IntId < 1020)
    {
        /* SPI: Use distributor */
        RegOffset = GICD_ICENABLER + (IntId / 32) * 4;
        BitPos = IntId % 32;

        *HalpMmio((ULONG_PTR)HalpGicdBase, RegOffset) = (1u << BitPos);
    }
    else
    {
        /* LPI: Disable via ITS configuration table - not implemented */
        DPRINT("[arm64][GIC] LPI disable not fully implemented for INTID %lu\n", IntId);
    }

    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * HalpGicSetInterruptPriority - Set interrupt priority
 *
 * Sets the priority for the specified interrupt. Lower values = higher priority.
 *
 * Parameters:
 *   IntId    - The interrupt ID
 *   Priority - Priority value (0-255, typically 0xA0 for medium priority)
 */
VOID
HalpGicSetInterruptPriority(
    _In_ ULONG IntId,
    _In_ ULONG Priority)
{
    ULONG RegOffset;
    ULONG ByteOffset;
    ULONG Shift;
    ULONG Value;
    volatile ULONG *RegPtr;

    if (IntId >= 1020 && IntId < 8192)
    {
        DPRINT1("[arm64][GIC] Cannot set priority for reserved INTID %lu\n", IntId);
        return;
    }

    if (IntId < 32)
    {
        /* SGI/PPI: Use redistributor for GICv3, distributor for GICv2 */
        if (HalpGicUseSysRegs)
        {
            ULONG Cpu = KeGetCurrentProcessorNumber();
            ULONG_PTR SgiBase = HalpGicrSgiBase(Cpu);

            if (SgiBase == 0)
            {
                DPRINT1("[arm64][GIC] Cannot set priority: GICR not available\n");
                return;
            }

            RegOffset = GICR_IPRIORITYR + (IntId & ~3);
            RegPtr = HalpMmio(SgiBase, RegOffset);
        }
        else
        {
            RegOffset = GICD_IPRIORITYR + (IntId & ~3);
            RegPtr = HalpMmio((ULONG_PTR)HalpGicdBase, RegOffset);
        }
    }
    else if (IntId < 1020)
    {
        /* SPI: Use distributor */
        RegOffset = GICD_IPRIORITYR + (IntId & ~3);
        RegPtr = HalpMmio((ULONG_PTR)HalpGicdBase, RegOffset);
    }
    else
    {
        /* LPI: Priority set via configuration table - not implemented here */
        DPRINT("[arm64][GIC] LPI priority not implemented for INTID %lu\n", IntId);
        return;
    }

    /* Calculate byte offset within the 32-bit register */
    ByteOffset = IntId & 3;
    Shift = ByteOffset * 8;

    /* Read-modify-write the priority */
    Value = *RegPtr;
    Value &= ~(0xFFu << Shift);
    Value |= ((Priority & 0xFF) << Shift);
    *RegPtr = Value;

    __asm__ __volatile__("dsb sy" ::: "memory");
}
