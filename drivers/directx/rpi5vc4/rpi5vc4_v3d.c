/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2712 V3D 7.1 bring-up and control-list job execution.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Power/reset/MMU sequences follow the Linux v3d DRM driver (GPL-2.0+),
 * see rpi5vc4_v3d.h for the derivation notes.  Everything here is
 * defensive: any identification or power-up failure simply leaves the 3D
 * node disabled and the driver runs 2D-only.
 */

#include "rpi5vc4_v3d.h"
#include "rpi5vc4_mbox.h"

#define NDEBUG
#include <reactos/debug.h>

static ULONG
Rpi5V3dRead(
    _In_ PVOID Base,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + Offset));
}

static VOID
Rpi5V3dWrite(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + Offset), Value);
}

/* Poll (reg & Mask) == Want with a 100 ms ceiling; PASSIVE_LEVEL only. */
static BOOLEAN
Rpi5V3dWait(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ ULONG Want)
{
    ULONG Tries;

    for (Tries = 0; Tries < 2000; Tries++)
    {
        if ((Rpi5V3dRead(Base, Offset) & Mask) == Want)
            return TRUE;
        KeStallExecutionProcessor(50);
    }

    return FALSE;
}

/* ========================================================================
 * SMS power sequencing (BCM2712 only)
 * ====================================================================== */

static BOOLEAN
Rpi5V3dSmsPowerUp(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Sms = DeviceExtension->V3dSmsBase;
    ULONG Tries;
    ULONG State;

    /* Ask the state machine to leave the power-off state. */
    Rpi5V3dWrite(Sms, V3D_SMS_TEE_CS, V3D_SMS_CLEAR_POWER_OFF);

    if (!Rpi5V3dWait(Sms, V3D_SMS_TEE_CS, V3D_SMS_STATE_MASK, V3D_SMS_STATE_IDLE))
    {
        DPRINT1("RPI5VC4: V3D SMS power-up timed out (TEE_CS=0x%08lx)\n",
                Rpi5V3dRead(Sms, V3D_SMS_TEE_CS));
        return FALSE;
    }

    /* Pulse the SMS reset sequence and wait for it to settle. */
    Rpi5V3dWrite(Sms, V3D_SMS_REE_CS, 0x4);

    for (Tries = 0; Tries < 2000; Tries++)
    {
        State = Rpi5V3dRead(Sms, V3D_SMS_REE_CS) & V3D_SMS_STATE_MASK;
        if (State != V3D_SMS_STATE_ISOLATING_FOR_RESET &&
            State != V3D_SMS_STATE_RESETTING)
        {
            return TRUE;
        }
        KeStallExecutionProcessor(50);
    }

    DPRINT1("RPI5VC4: V3D SMS reset timed out (REE_CS=0x%08lx)\n",
            Rpi5V3dRead(Sms, V3D_SMS_REE_CS));
    return FALSE;
}

/* ========================================================================
 * GPU MMU
 * ====================================================================== */

/* DISPATCH-safe bounded MMUC flush + TLB clear (Linux v3d_mmu_flush_all
 * shape, micro-wait instead of the 100 ms PASSIVE helper). */
BOOLEAN
Rpi5V3dMmucFlushBounded(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Hub = DeviceExtension->V3dHubBase;
    ULONG Tries;

    if (Hub == NULL)
        return TRUE;

    Rpi5V3dWrite(Hub, V3D_MMUC_CONTROL,
                 V3D_MMUC_CONTROL_FLUSH | V3D_MMUC_CONTROL_ENABLE);
    for (Tries = 0; Tries < 200; Tries++)
    {
        if ((Rpi5V3dRead(Hub, V3D_MMUC_CONTROL) & V3D_MMUC_CONTROL_FLUSHING) == 0)
            break;
        KeStallExecutionProcessor(1);
    }
    if (Tries == 200)
        return FALSE;

    Rpi5V3dWrite(Hub, V3D_MMU_CTL,
                 Rpi5V3dRead(Hub, V3D_MMU_CTL) | V3D_MMU_CTL_TLB_CLEAR);
    for (Tries = 0; Tries < 200; Tries++)
    {
        if ((Rpi5V3dRead(Hub, V3D_MMU_CTL) & V3D_MMU_CTL_TLB_CLEARING) == 0)
            break;
        KeStallExecutionProcessor(1);
    }

    return Tries < 200;
}

static BOOLEAN
Rpi5V3dMmuFlushAll(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Hub = DeviceExtension->V3dHubBase;

    Rpi5V3dWrite(Hub, V3D_MMUC_CONTROL,
                 V3D_MMUC_CONTROL_FLUSH | V3D_MMUC_CONTROL_ENABLE);
    if (!Rpi5V3dWait(Hub, V3D_MMUC_CONTROL, V3D_MMUC_CONTROL_FLUSHING, 0))
    {
        DPRINT1("RPI5VC4: V3D MMUC flush timed out\n");
        return FALSE;
    }

    Rpi5V3dWrite(Hub, V3D_MMU_CTL,
                 Rpi5V3dRead(Hub, V3D_MMU_CTL) | V3D_MMU_CTL_TLB_CLEAR);
    if (!Rpi5V3dWait(Hub, V3D_MMU_CTL, V3D_MMU_CTL_TLB_CLEARING, 0))
    {
        DPRINT1("RPI5VC4: V3D MMU TLB clear timed out\n");
        return FALSE;
    }

    return TRUE;
}

/* Internal (defined below); MMU setup calls it before its definition. */
static BOOLEAN
Rpi5V3dMmuProgram(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

static BOOLEAN
Rpi5V3dMmuSetup(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PULONG PageTable;
    ULONG SlabPages;
    ULONG FirstPte;
    ULONG i;

    Low.QuadPart = 0;
    High.QuadPart = 0xFFFFFFFFFFULL;
    Boundary.QuadPart = 0;

    /*
     * One 32-bit PTE per 4 KB GPU page over the full 4 GB GPU VA space, so
     * every possible lookup lands inside the table (unmapped entries are
     * zero => PT_INVALID abort rather than a stray bus read).
     */
    /* Allocations are one-time; guarded so this routine is idempotent and
     * the TDR-reset path can call it to REBUILD the PTE image into the same
     * physical table (recovering a corrupted translation) without leaking
     * or moving the GPU-visible physical addresses. */
    if (DeviceExtension->V3dPageTable == NULL)
    {
        DeviceExtension->V3dPageTable = MmAllocateContiguousMemorySpecifyCache(
            RPI5VC4_V3D_PT_SIZE, Low, High, Boundary, MmWriteCombined);
        if (DeviceExtension->V3dPageTable == NULL)
        {
            DPRINT1("RPI5VC4: V3D page table alloc failed\n");
            return FALSE;
        }
        DeviceExtension->V3dPageTablePhys =
            MmGetPhysicalAddress(DeviceExtension->V3dPageTable);
    }

    if (DeviceExtension->V3dScratchPage == NULL)
    {
        DeviceExtension->V3dScratchPage = MmAllocateContiguousMemorySpecifyCache(
            PAGE_SIZE, Low, High, Boundary, MmWriteCombined);
        if (DeviceExtension->V3dScratchPage == NULL)
        {
            DPRINT1("RPI5VC4: V3D scratch page alloc failed\n");
            return FALSE;
        }
        DeviceExtension->V3dScratchPagePhys =
            MmGetPhysicalAddress(DeviceExtension->V3dScratchPage);
    }

    /*
     * Binner overflow pool: the CLE spills tile lists here when the
     * binning memory embedded in a job runs out (programmed into
     * CT0QMA/QMS at submit).
     */
    if (DeviceExtension->V3dOverflowVa == NULL)
    {
        DeviceExtension->V3dOverflowVa = MmAllocateContiguousMemorySpecifyCache(
            RPI5VC4_V3D_OVERFLOW_SIZE, Low, High, Boundary, MmWriteCombined);
        if (DeviceExtension->V3dOverflowVa == NULL)
        {
            DPRINT1("RPI5VC4: V3D overflow pool alloc failed\n");
            return FALSE;
        }
        DeviceExtension->V3dOverflowPhys =
            MmGetPhysicalAddress(DeviceExtension->V3dOverflowVa);
    }

    RtlZeroMemory(DeviceExtension->V3dPageTable, RPI5VC4_V3D_PT_SIZE);
    RtlZeroMemory(DeviceExtension->V3dScratchPage, PAGE_SIZE);

    /* Map the VRAM slab at RPI5VC4_V3D_SLAB_GPUVA, writeable. */
    PageTable = DeviceExtension->V3dPageTable;
    SlabPages = DeviceExtension->VramSize >> V3D_MMU_PAGE_SHIFT;
    FirstPte = RPI5VC4_V3D_SLAB_GPUVA >> V3D_MMU_PAGE_SHIFT;

    for (i = 0; i < SlabPages; i++)
    {
        ULONGLONG PagePhys = (ULONGLONG)DeviceExtension->VramPhysical.QuadPart +
                             ((ULONGLONG)i << V3D_MMU_PAGE_SHIFT);

        PageTable[FirstPte + i] =
            (ULONG)(PagePhys >> V3D_MMU_PAGE_SHIFT) |
            V3D_PTE_WRITEABLE | V3D_PTE_VALID;
    }

    /* Map the overflow pool right after the slab window. */
    DeviceExtension->V3dOverflowGpuVa =
        RPI5VC4_V3D_SLAB_GPUVA + DeviceExtension->VramSize;
    FirstPte = DeviceExtension->V3dOverflowGpuVa >> V3D_MMU_PAGE_SHIFT;

    for (i = 0; i < (RPI5VC4_V3D_OVERFLOW_SIZE >> V3D_MMU_PAGE_SHIFT); i++)
    {
        ULONGLONG PagePhys =
            (ULONGLONG)DeviceExtension->V3dOverflowPhys.QuadPart +
            ((ULONGLONG)i << V3D_MMU_PAGE_SHIFT);

        PageTable[FirstPte + i] =
            (ULONG)(PagePhys >> V3D_MMU_PAGE_SHIFT) |
            V3D_PTE_WRITEABLE | V3D_PTE_VALID;
    }

    __dsb(_ARM64_BARRIER_SY);
    KeMemoryBarrier();

    return Rpi5V3dMmuProgram(DeviceExtension);
}

static VOID
Rpi5V3dMmuWriteRegistersNoFlush(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_MMU_PT_PA_BASE,
                 (ULONG)(DeviceExtension->V3dPageTablePhys.QuadPart >>
                         V3D_MMU_PAGE_SHIFT));
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_MMU_CTL,
                 V3D_MMU_CTL_ENABLE |
                 V3D_MMU_CTL_TLB_STATS_ENABLE |
                 V3D_MMU_CTL_PT_INVALID_ENABLE |
                 V3D_MMU_CTL_PT_INVALID_ABORT |
                 V3D_MMU_CTL_PT_INVALID_INT |
                 V3D_MMU_CTL_WRITE_VIOLATION_ABORT |
                 V3D_MMU_CTL_WRITE_VIOLATION_INT |
                 V3D_MMU_CTL_CAP_EXCEEDED_ABORT |
                 V3D_MMU_CTL_CAP_EXCEEDED_INT);
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_MMU_ILLEGAL_ADDR,
                 (ULONG)(DeviceExtension->V3dScratchPagePhys.QuadPart >>
                         V3D_MMU_PAGE_SHIFT) |
                 V3D_MMU_ILLEGAL_ADDR_ENABLE);
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_MMUC_CONTROL,
                 V3D_MMUC_CONTROL_ENABLE);
}

static BOOLEAN
Rpi5V3dMmuProgram(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    Rpi5V3dMmuWriteRegistersNoFlush(DeviceExtension);
    return Rpi5V3dMmuFlushAll(DeviceExtension);
}

BOOLEAN
Rpi5V3dMmuProgramBounded(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    Rpi5V3dMmuWriteRegistersNoFlush(DeviceExtension);
    return Rpi5V3dMmucFlushBounded(DeviceExtension);
}

/* Bounded FULL SMS power-up front half (TEE handshake + REE pulse), the
 * ResetCore sequence at DISPATCH-safe micro-bounds.  Per-stage timings out. */
BOOLEAN
Rpi5V3dSmsPowerUpBounded(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG TeeUs,
    _Out_ PULONG ReeUs)
{
    PVOID Sms = DeviceExtension->V3dSmsBase;
    ULONG Tries;
    ULONG State;

    *TeeUs = 0;
    *ReeUs = 0;
    if (Sms == NULL)
        return FALSE;

    Rpi5V3dWrite(Sms, V3D_SMS_TEE_CS, V3D_SMS_CLEAR_POWER_OFF);
    for (Tries = 0; Tries < 5000; Tries++)
    {
        if ((Rpi5V3dRead(Sms, V3D_SMS_TEE_CS) & V3D_SMS_STATE_MASK) ==
            V3D_SMS_STATE_IDLE)
        {
            break;
        }
        KeStallExecutionProcessor(1);
    }
    *TeeUs = Tries;
    if (Tries == 5000)
        return FALSE;

    Rpi5V3dWrite(Sms, V3D_SMS_REE_CS, 0x4);
    for (Tries = 0; Tries < 5000; Tries++)
    {
        State = Rpi5V3dRead(Sms, V3D_SMS_REE_CS) & V3D_SMS_STATE_MASK;
        if (State != V3D_SMS_STATE_ISOLATING_FOR_RESET &&
            State != V3D_SMS_STATE_RESETTING)
        {
            *ReeUs = Tries;
            return TRUE;
        }
        KeStallExecutionProcessor(1);
    }
    *ReeUs = Tries;
    return FALSE;
}


/* Full core recovery after a CLE/PTB hang: SMS reset pulse, invariant
 * state, interrupt re-init, MMU reprogram (mirrors Linux v3d_reset). */
BOOLEAN
Rpi5V3dResetCore(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Core = DeviceExtension->V3dCoreBase;
    PVOID Hub = DeviceExtension->V3dHubBase;

    if (!DeviceExtension->V3dReady || Core == NULL || Hub == NULL ||
        DeviceExtension->V3dSmsBase == NULL)
        return FALSE;

    /* The SMS reset pulse (in Rpi5V3dSmsPowerUp) is the light path and
     * preserves the V3D clocks/PLLs.  A firmware DOMAIN power-cycle resets
     * those clocks and ResetCore does not re-establish them, so every
     * post-power-cycle job hangs (the observed 122fps->TDR->0.2fps spiral
     * that survives the power-cycle).  Do the soft SMS reset first; only
     * escalate to the destructive power-cycle after several consecutive
     * resets show the soft path isn't clearing the wedge. */
    DeviceExtension->V3dResetCount++;
    if ((DeviceExtension->V3dResetCount & 7) == 0)
    {
        DPRINT1("RPI5VC4: escalating to V3D domain power-cycle (reset #%lu)\n",
                DeviceExtension->V3dResetCount);
        if (Rpi5MboxSetDomainState(DeviceExtension, RPI5_MBOX_DOMAIN_V3D, FALSE))
        {
            KeStallExecutionProcessor(1000);
            if (!Rpi5MboxSetDomainState(DeviceExtension, RPI5_MBOX_DOMAIN_V3D, TRUE))
                DPRINT1("RPI5VC4: V3D domain re-power failed\n");
            KeStallExecutionProcessor(1000);
        }
    }

    if (!Rpi5V3dSmsPowerUp(DeviceExtension))
        return FALSE;

    Rpi5V3dWrite(Core, V3D_CTL_L2TFLSTA, 0);
    Rpi5V3dWrite(Core, V3D_CTL_L2TFLEND, ~0u);

    Rpi5V3dWrite(Core, V3D_CTL_INT_MSK_SET, ~0u);
    Rpi5V3dWrite(Core, V3D_CTL_INT_CLR, ~0u);
    Rpi5V3dWrite(Hub, V3D_HUB_INT_MSK_SET, ~0u);
    Rpi5V3dWrite(Hub, V3D_HUB_INT_CLR, ~0u);

    if (DeviceExtension->V3dIrqConnected)
    {
        Rpi5V3dWrite(Core, V3D_CTL_INT_MSK_CLR,
                     V3D_INT_FLDONE | V3D_INT_FRDONE | V3D_INT_OUTOMEM |
                     V3D_V7_INT_CSDDONE);
        Rpi5V3dWrite(Hub, V3D_HUB_INT_MSK_CLR, V3D_HUB_INT_TFUC);
    }

    /* REBUILD the page table (not just re-point the base): the spiral
     * signature is a post-reset job dying at CT0CA=start+1 with BPCA=0
     * and, in some dumps, the control thread branching to a wild address
     * (0xafc6xxxx) + MMU fault — i.e. translation is corrupt and survives
     * a bare MmuProgram, which reuses the same PTEs.  MmuSetup restores
     * the full slab/overflow PTE image so a recovered job translates
     * cleanly (this is what Initialize does on the first, working job). */
    if (!Rpi5V3dMmuSetup(DeviceExtension))
        return FALSE;

    DeviceExtension->V3dLastBfc = 0;
    DeviceExtension->V3dLastRfc = 0;

    Rpi5V3dWrite(Core, V3D_ERR_STAT, 0xFFFFFFFFu);
    DPRINT1("RPI5VC4: reset ERR_STAT after clear=%08lx\n",
            Rpi5V3dRead(Core, V3D_ERR_STAT));

    return TRUE;
}

/* ========================================================================
 * Initialization / teardown
 * ====================================================================== */

BOOLEAN
Rpi5V3dInitialize(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PHYSICAL_ADDRESS Phys;
    ULONG Ident1;
    ULONG Version;

    DeviceExtension->V3dReady = FALSE;

    if (DeviceExtension->VramVa == NULL || DeviceExtension->VramSize == 0)
        return FALSE;

    Phys.QuadPart = RPI5_V3D_SMS_PHYS;
    DeviceExtension->V3dSmsBase = MmMapIoSpace(Phys, RPI5_V3D_SMS_LENGTH,
                                               MmNonCached);
    Phys.QuadPart = RPI5_V3D_HUB_PHYS;
    DeviceExtension->V3dHubBase = MmMapIoSpace(Phys, RPI5_V3D_HUB_LENGTH,
                                               MmNonCached);
    Phys.QuadPart = RPI5_V3D_CORE0_PHYS;
    DeviceExtension->V3dCoreBase = MmMapIoSpace(Phys, RPI5_V3D_CORE0_LENGTH,
                                                MmNonCached);

    if (DeviceExtension->V3dSmsBase == NULL ||
        DeviceExtension->V3dHubBase == NULL ||
        DeviceExtension->V3dCoreBase == NULL)
    {
        DPRINT1("RPI5VC4: V3D register mapping failed\n");
        goto Fail;
    }

    /* Linux probe parity (reset_control_reset before any job): the UEFI
     * firmware used this core for GOP; power-cycle the V3D domain once so
     * the first job starts from clean silicon, not firmware leftovers.
     * Safe only HERE: the full clock setup below re-establishes rates. */
    if (Rpi5MboxSetDomainState(DeviceExtension, RPI5_MBOX_DOMAIN_V3D, FALSE))
    {
        KeStallExecutionProcessor(1000);
        if (!Rpi5MboxSetDomainState(DeviceExtension, RPI5_MBOX_DOMAIN_V3D, TRUE))
            DPRINT1("RPI5VC4: init V3D domain re-power failed\n");
        KeStallExecutionProcessor(1000);
    }

    if (!Rpi5V3dSmsPowerUp(DeviceExtension))
        goto Fail;

    /* Identify the hub: TVER.REV must be a plausible V3D 4.x/7.x. */
    Ident1 = Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_HUB_IDENT1);
    if (Ident1 == 0 || Ident1 == 0xFFFFFFFF)
    {
        DPRINT1("RPI5VC4: V3D hub not responding (IDENT1=0x%08lx)\n", Ident1);
        goto Fail;
    }

    Version = ((Ident1 & V3D_HUB_IDENT1_TVER_MASK) >> V3D_HUB_IDENT1_TVER_SHIFT) * 10 +
              ((Ident1 & V3D_HUB_IDENT1_REV_MASK) >> V3D_HUB_IDENT1_REV_SHIFT);
    if (Version < 41 ||
        ((Ident1 & V3D_HUB_IDENT1_NCORES_MASK) >> V3D_HUB_IDENT1_NCORES_SHIFT) < 1)
    {
        DPRINT1("RPI5VC4: unsupported V3D version %lu (IDENT1=0x%08lx)\n",
                Version, Ident1);
        goto Fail;
    }

    DeviceExtension->V3dVersion = Version;
    DeviceExtension->V3dHubIdent[0] =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_HUB_IDENT0);
    DeviceExtension->V3dHubIdent[1] = Ident1;
    DeviceExtension->V3dHubIdent[2] =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_HUB_IDENT2);
    DeviceExtension->V3dHubIdent[3] =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_HUB_IDENT3);
    DeviceExtension->V3dCoreIdent[0] =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_IDENT0);
    DeviceExtension->V3dCoreIdent[1] =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_IDENT1);
    DeviceExtension->V3dCoreIdent[2] =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_IDENT2);

    /* Invariant core state: full-range L2T flushes. */
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_L2TFLSTA, 0);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_L2TFLEND, ~0u);

    /*
     * Completion is polled from the fence queue (root-enumerated devnode:
     * no interrupt resource), so mask every interrupt line and clear any
     * stale status.  INT_STS still latches FLDONE/FRDONE for the poll.
     */
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_INT_MSK_SET, ~0u);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_INT_CLR, ~0u);
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_HUB_INT_MSK_SET, ~0u);
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_HUB_INT_CLR, ~0u);

    if (!Rpi5V3dMmuSetup(DeviceExtension))
        goto Fail;

    /* Poll-driven completion: the fence poll timer needs real 1ms ticks,
     * not the default 10-15ms clock granularity. */
    DPRINT1("RPI5VC4: timer resolution granted %lu\n",
            ExSetTimerResolution(10000, TRUE));

    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_ERR_STAT, 0xFFFFFFFFu);
    DPRINT1("RPI5VC4: init ERR_STAT after clear=%08lx\n",
            Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_ERR_STAT));

    DeviceExtension->V3dReady = TRUE;
    DPRINT1("RPI5VC4: V3D %lu.%lu online — hub 0x%08lx/0x%08lx core 0x%08lx, "
            "slab GPU VA 0x%08x (%lu KB)\n",
            Version / 10, Version % 10,
            DeviceExtension->V3dHubIdent[0],
            DeviceExtension->V3dHubIdent[1],
            DeviceExtension->V3dCoreIdent[0],
            RPI5VC4_V3D_SLAB_GPUVA,
            DeviceExtension->VramSize / 1024);
    return TRUE;

Fail:
    Rpi5V3dTeardown(DeviceExtension);
    return FALSE;
}

VOID
Rpi5V3dTeardown(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    DeviceExtension->V3dReady = FALSE;

    if (DeviceExtension->V3dHubBase != NULL)
    {
        /* Disable the GPU MMU before its page table goes away. */
        Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_MMU_CTL, 0);
        Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_MMUC_CONTROL, 0);
    }

    if (DeviceExtension->V3dPageTable != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(DeviceExtension->V3dPageTable,
                                           RPI5VC4_V3D_PT_SIZE,
                                           MmWriteCombined);
        DeviceExtension->V3dPageTable = NULL;
    }

    if (DeviceExtension->V3dScratchPage != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(DeviceExtension->V3dScratchPage,
                                           PAGE_SIZE,
                                           MmWriteCombined);
        DeviceExtension->V3dScratchPage = NULL;
    }

    if (DeviceExtension->V3dOverflowVa != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(DeviceExtension->V3dOverflowVa,
                                           RPI5VC4_V3D_OVERFLOW_SIZE,
                                           MmWriteCombined);
        DeviceExtension->V3dOverflowVa = NULL;
    }

    if (DeviceExtension->V3dCoreBase != NULL)
    {
        MmUnmapIoSpace(DeviceExtension->V3dCoreBase, RPI5_V3D_CORE0_LENGTH);
        DeviceExtension->V3dCoreBase = NULL;
    }

    if (DeviceExtension->V3dHubBase != NULL)
    {
        MmUnmapIoSpace(DeviceExtension->V3dHubBase, RPI5_V3D_HUB_LENGTH);
        DeviceExtension->V3dHubBase = NULL;
    }

    if (DeviceExtension->V3dSmsBase != NULL)
    {
        MmUnmapIoSpace(DeviceExtension->V3dSmsBase, RPI5_V3D_SMS_LENGTH);
        DeviceExtension->V3dSmsBase = NULL;
    }
}

/* ========================================================================
 * Interrupt delivery (best effort)
 *
 * The devnode is root-enumerated with no interrupt resource, so the SPI is
 * connected fully-specified from the DTB fact (SPI 250).  On this ARM64
 * HAL the root IRQ arbiter may refuse or misroute the vector — the ISR is
 * therefore purely an accelerator: it queues the same completion DPC the
 * poll timer drives, and the poll timer stays armed as the backstop.
 * ====================================================================== */

static BOOLEAN
NTAPI
Rpi5V3dInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID ServiceContext)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = ServiceContext;
    ULONG Status;

    UNREFERENCED_PARAMETER(Interrupt);

    if (DeviceExtension == NULL || !DeviceExtension->V3dReady)
        return FALSE;

    Status = Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_INT_STS);
    if ((Status & (V3D_INT_FLDONE | V3D_INT_FRDONE | V3D_INT_OUTOMEM |
                   V3D_V7_INT_CSDDONE)) == 0)
    {
        if (DeviceExtension->V3dHubBase != NULL &&
            (Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_HUB_INT_STS) &
             V3D_HUB_INT_TFUC) != 0)
        {
            Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_HUB_INT_MSK_SET,
                         V3D_HUB_INT_TFUC);
            InterlockedExchange(&DeviceExtension->V3dIsrMasked, 1);
            InterlockedIncrement(&DeviceExtension->V3dIsrCount);
            KeInsertQueueDpc(&DeviceExtension->V3dPollDpc, NULL, NULL);
            return TRUE;
        }
        return FALSE;
    }

    /*
     * Level-triggered SPI: quiesce the source BEFORE EOI or the line
     * re-asserts forever and storms this core at DIRQL (observed: poll
     * DPC + both TDR paths starved while the rest of the system ran).
     * Mask the completion sources here; the DPC consumes INT_STS with
     * job attribution and unmasks on the way out.
     */
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_INT_MSK_SET,
                 V3D_INT_FLDONE | V3D_INT_FRDONE | V3D_INT_OUTOMEM |
                 V3D_V7_INT_CSDDONE);
    InterlockedExchange(&DeviceExtension->V3dIsrMasked, 1);
    InterlockedIncrement(&DeviceExtension->V3dIsrCount);

    KeInsertQueueDpc(&DeviceExtension->V3dPollDpc, NULL, NULL);
    return TRUE;
}

BOOLEAN
Rpi5V3dConnectInterrupt(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    IO_CONNECT_INTERRUPT_PARAMETERS Params;
    NTSTATUS Status;

    if (!DeviceExtension->V3dReady ||
        DeviceExtension->PhysicalDeviceObject == NULL)
    {
        return FALSE;
    }

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Version = CONNECT_FULLY_SPECIFIED;
    Params.FullySpecified.PhysicalDeviceObject =
        DeviceExtension->PhysicalDeviceObject;
    Params.FullySpecified.InterruptObject = &DeviceExtension->V3dInterrupt;
    Params.FullySpecified.ServiceRoutine = Rpi5V3dInterruptService;
    Params.FullySpecified.ServiceContext = DeviceExtension;
    Params.FullySpecified.SynchronizeIrql = (KIRQL)(DISPATCH_LEVEL + 1);
    Params.FullySpecified.FloatingSave = FALSE;
    Params.FullySpecified.ShareVector = FALSE;
    Params.FullySpecified.Vector = RPI5_V3D_CORE_INTID;
    Params.FullySpecified.Irql = (KIRQL)(DISPATCH_LEVEL + 1);
    Params.FullySpecified.InterruptMode = LevelSensitive;
    Params.FullySpecified.ProcessorEnableMask = 1;

    /*
     * The v3d node lists two SPIs (250, 249); which one carries the core
     * completions (FLDONE/FRDONE) vs the hub events (TFUC/MMU) is not proven
     * on silicon, and the single shared ServiceRoutine reads BOTH status
     * registers.  So connect both lines and require only that ONE succeeds:
     * making either line fatal-then-optional (as before) meant a boot where
     * the core-carrying line failed to connect lost every core completion to
     * the 1 ms poll (isr=0), which starves the bin->render kick and parks.
     */
    Status = IoConnectInterruptEx(&Params);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("RPI5VC4: V3D SPI %u connect failed 0x%08lx\n",
               RPI5_V3D_CORE_SPI, Status);
        DeviceExtension->V3dInterrupt = NULL;
    }

    Params.FullySpecified.InterruptObject = &DeviceExtension->V3dInterrupt2;
    Params.FullySpecified.Vector = RPI5_V3D_INTID2;
    Status = IoConnectInterruptEx(&Params);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("RPI5VC4: V3D SPI %u connect failed 0x%08lx\n",
               RPI5_V3D_SPI2, Status);
        DeviceExtension->V3dInterrupt2 = NULL;
    }

    if (DeviceExtension->V3dInterrupt == NULL &&
        DeviceExtension->V3dInterrupt2 == NULL)
    {
        DPRINT1("RPI5VC4: V3D both interrupt lines failed — polling only\n");
        return FALSE;
    }

    /* Unmask the completion interrupts now that a handler exists. */
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_INT_MSK_CLR,
                 V3D_INT_FLDONE | V3D_INT_FRDONE | V3D_INT_OUTOMEM |
                 V3D_V7_INT_CSDDONE);
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_HUB_INT_MSK_CLR,
                 V3D_HUB_INT_TFUC);

    DeviceExtension->V3dIrqConnected = TRUE;
    DPRINT1("RPI5VC4: V3D interrupt connected (core=%u/INTID%u hub=%u/INTID%u)\n",
            DeviceExtension->V3dInterrupt != NULL, RPI5_V3D_CORE_INTID,
            DeviceExtension->V3dInterrupt2 != NULL, RPI5_V3D_INTID2);
    return TRUE;
}

VOID
Rpi5V3dDisconnectInterrupt(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->V3dInterrupt != NULL)
    {
        IO_DISCONNECT_INTERRUPT_PARAMETERS Params;

        if (DeviceExtension->V3dCoreBase != NULL)
        {
            Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_INT_MSK_SET,
                         ~0u);
        }

        RtlZeroMemory(&Params, sizeof(Params));
        Params.Version = CONNECT_FULLY_SPECIFIED;
        Params.ConnectionContext.InterruptObject =
            DeviceExtension->V3dInterrupt;
        IoDisconnectInterruptEx(&Params);

        DeviceExtension->V3dInterrupt = NULL;
    }
    if (DeviceExtension->V3dInterrupt2 != NULL)
    {
        IO_DISCONNECT_INTERRUPT_PARAMETERS Params;

        RtlZeroMemory(&Params, sizeof(Params));
        Params.Version = CONNECT_FULLY_SPECIFIED;
        Params.ConnectionContext.InterruptObject =
            DeviceExtension->V3dInterrupt2;
        IoDisconnectInterruptEx(&Params);

        DeviceExtension->V3dInterrupt2 = NULL;
    }
    DeviceExtension->V3dIrqConnected = FALSE;
}

/* ========================================================================
 * Phased job submission (callable at DISPATCH_LEVEL)
 * ====================================================================== */

/*
 * Invalidate the caches a new job reads through (L2T + slice caches),
 * mirroring the Linux v3d per-job invalidate.  The CPU wrote the control
 * lists/textures through write-combined mappings, so no clean pass is
 * needed first.
 */
static VOID
Rpi5V3dInvalidateCaches(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Core = DeviceExtension->V3dCoreBase;

    /*
     * Exactly Linux v3d_invalidate_caches on v3d 4.1+: a POSTED L2T
     * flush (no completion wait) plus slice invalidates.  The TMUWCF
     * drain + wait loops belong to v3d_clean_caches (post-CSD, run
     * with no CL active); doing them here raced the concurrent binner
     * and parked the queued render (enqueue latched, never armed).
     */
    Rpi5V3dWrite(Core, V3D_CTL_L2TCACTL,
                 V3D_L2TCACTL_L2TFLS | V3D_L2TCACTL_FLM_FLUSH);

    Rpi5V3dWrite(Core, V3D_CTL_SLCACTL, V3D_SLCACTL_INVALIDATE_ALL);
}

BOOLEAN
Rpi5V3dSubmitCsd(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST ULONG *CsdCfg)
{
    PVOID Core = DeviceExtension->V3dCoreBase;
    ULONG i;

    if (!DeviceExtension->V3dReady || CsdCfg == NULL)
        return FALSE;

    Rpi5V3dWrite(Core, V3D_CTL_INT_CLR, V3D_V7_INT_CSDDONE);
    Rpi5V3dInvalidateCaches(DeviceExtension);

    /* Genuine kick order (Linux v3d_csd_job_run): CFG1..CFG7 staged,
     * then the CFG0 write launches the dispatch. */
    for (i = 1; i <= 7; i++)
        Rpi5V3dWrite(Core, V3D_V7_CSD_QUEUED_CFG(i), CsdCfg[i]);
    __dsb(_ARM64_BARRIER_SY);
    Rpi5V3dWrite(Core, V3D_V7_CSD_QUEUED_CFG0, CsdCfg[0]);

    return TRUE;
}

BOOLEAN
Rpi5V3dCsdDone(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Core = DeviceExtension->V3dCoreBase;

    if (!DeviceExtension->V3dReady)
        return TRUE;

    if (Rpi5V3dRead(Core, V3D_CTL_INT_STS) & V3D_V7_INT_CSDDONE)
    {
        Rpi5V3dWrite(Core, V3D_CTL_INT_CLR, V3D_V7_INT_CSDDONE);
        return TRUE;
    }

    return FALSE;
}

BOOLEAN
Rpi5V3dSubmitBin(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG BclStart,
    _In_ ULONG BclEnd,
    _In_ ULONG Qma,
    _In_ ULONG Qms,
    _In_ ULONG Qts)
{
    PVOID Core = DeviceExtension->V3dCoreBase;

    if (!DeviceExtension->V3dReady || BclEnd <= BclStart)
        return FALSE;

    Rpi5V3dWrite(Core, V3D_CTL_INT_CLR, V3D_INT_FLDONE | V3D_INT_OUTOMEM);

    /* Linux v3d_bin_job_run: clear leftover overflow bookkeeping from the
     * previous job or the PTB wedges the next flush (BFC freezes). */
    Rpi5V3dWrite(Core, V3D_PTB_BPOS, 0);

    Rpi5V3dInvalidateCaches(DeviceExtension);

    /* Per-job tile allocation, else the device-global overflow pool. */
    if (Qms != 0)
    {
        Rpi5V3dWrite(Core, V3D_CLE_CT0QMA, Qma);
        Rpi5V3dWrite(Core, V3D_CLE_CT0QMS, Qms);
    }
    else
    {
        Rpi5V3dWrite(Core, V3D_CLE_CT0QMA, DeviceExtension->V3dOverflowGpuVa);
        Rpi5V3dWrite(Core, V3D_CLE_CT0QMS, RPI5VC4_V3D_OVERFLOW_SIZE);
    }

    /* Binning control list on thread 0: base first, end kicks it.
     * Linux writel embeds a dsb(st) before every doorbell. */
    __dsb(_ARM64_BARRIER_SY);
    Rpi5V3dWrite(Core, V3D_CLE_CT0QTS, V3D_CLE_CT0QTS_ENABLE | (Qts & ~7u));
    Rpi5V3dWrite(Core, V3D_CLE_CT0QBA, BclStart);
    Rpi5V3dWrite(Core, V3D_CLE_CT0QEA, BclEnd);

    return TRUE;
}

BOOLEAN
Rpi5V3dSubmitRender(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG RclStart,
    _In_ ULONG RclEnd,
    _In_ BOOLEAN InvalidateCaches)
{
    PVOID Core = DeviceExtension->V3dCoreBase;

    if (!DeviceExtension->V3dReady || RclEnd <= RclStart)
        return FALSE;

    Rpi5V3dWrite(Core, V3D_CTL_INT_CLR, V3D_INT_FRDONE);

    /* Linux v3d_render_job_run invalidates before EVERY render kick: the
     * bin just wrote the tile lists through L2T and the render must not
     * fetch stale lines. */
    UNREFERENCED_PARAMETER(InvalidateCaches);
    Rpi5V3dInvalidateCaches(DeviceExtension);

    /* Render control list on thread 1. */
    __dsb(_ARM64_BARRIER_SY);
    Rpi5V3dWrite(Core, V3D_CLE_CT1QBA, RclStart);
    Rpi5V3dWrite(Core, V3D_CLE_CT1QEA, RclEnd);

    return TRUE;
}

BOOLEAN
Rpi5V3dSubmitTfu(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST ULONG *TfuRegs)
{
    PVOID Hub = DeviceExtension->V3dHubBase;

    if (!DeviceExtension->V3dReady || TfuRegs == NULL)
        return FALSE;

    /* Reject a kick while the unit is busy (single job in flight). */
    if (Rpi5V3dRead(Hub, V3D_V7_TFU_CS) & V3D_TFU_CS_BUSY)
        return FALSE;

    Rpi5V3dWrite(Hub, V3D_HUB_INT_CLR, V3D_HUB_INT_TFUC);

    Rpi5V3dWrite(Hub, V3D_V7_TFU_IIA,     TfuRegs[1]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_ICA,     TfuRegs[2]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_IIS,     TfuRegs[3]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_IUA,     TfuRegs[4]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_IOC_REG, TfuRegs[5]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_IOA,     TfuRegs[6]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_IOS,     TfuRegs[7]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_COEF0,   TfuRegs[8]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_COEF1,   TfuRegs[9]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_COEF2,   TfuRegs[10]);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_COEF3,   TfuRegs[11]);

    /* ICFG write with IOC kicks the conversion. */
    __dsb(_ARM64_BARRIER_SY);
    Rpi5V3dWrite(Hub, V3D_V7_TFU_ICFG, TfuRegs[0] | V3D_TFU_ICFG_IOC);

    return TRUE;
}

BOOLEAN
Rpi5V3dTfuDone(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Hub = DeviceExtension->V3dHubBase;

    if (!DeviceExtension->V3dReady)
        return TRUE;

    if (Rpi5V3dRead(Hub, V3D_HUB_INT_STS) & V3D_HUB_INT_TFUC)
    {
        Rpi5V3dWrite(Hub, V3D_HUB_INT_CLR, V3D_HUB_INT_TFUC);
        return TRUE;
    }

    return FALSE;
}

VOID
Rpi5V3dPollDone(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PBOOLEAN BinDone,
    _Out_ PBOOLEAN RenderDone)
{
    ULONG Status;
    ULONG Clear = 0;
    ULONG Bfc, Rfc;

    *BinDone = FALSE;
    *RenderDone = FALSE;

    if (!DeviceExtension->V3dReady)
        return;

    Status = Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_INT_STS);

    if (Status & V3D_INT_FLDONE)
    {
        *BinDone = TRUE;
        Clear |= V3D_INT_FLDONE;
    }
    if (Status & V3D_INT_FRDONE)
    {
        *RenderDone = TRUE;
        Clear |= V3D_INT_FRDONE;
    }
    if (Status & V3D_INT_OUTOMEM)
    {
        /* Binner ran out of tile-list memory: hand it the overflow region
         * and let it continue (Linux v3d_irq.c OOM path).  Without this
         * any bin that outgrows its QMA allocation parks forever. */
        Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_PTB_BPOA,
                     (ULONG)DeviceExtension->V3dOverflowGpuVa);
        Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_PTB_BPOS,
                     RPI5VC4_V3D_OVERFLOW_SIZE);
        Clear |= V3D_INT_OUTOMEM;
    }

    if (Clear != 0)
        Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_INT_CLR, Clear);

    /* Flush counters are the ground truth: a lost/uncaptured INT latch
     * must not strand a finished job (poll-driven completion). */
    Bfc = Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_BFC) & 0xff;
    Rfc = Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_RFC) & 0xff;
    if (Bfc != DeviceExtension->V3dLastBfc)
    {
        DeviceExtension->V3dLastBfc = Bfc;
        *BinDone = TRUE;
    }
    if (Rfc != DeviceExtension->V3dLastRfc)
    {
        DeviceExtension->V3dLastRfc = Rfc;
        *RenderDone = TRUE;
    }
}
