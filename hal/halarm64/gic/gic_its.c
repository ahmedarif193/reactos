/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/gic/gic_its.c
 * PURPOSE:         GIC Interrupt Translation Service (ITS) Support
 *
 * DESCRIPTION:
 *   This file contains comprehensive GIC ITS (Interrupt Translation Service)
 *   support for ARM64, enabling MSI (Message Signaled Interrupts) for PCI devices.
 *
 *   Key features implemented:
 *   - Multi-ITS node support (for multi-socket systems)
 *   - Dynamic LPI allocation using bitmap allocator
 *   - Full ITS command queue implementation (MAPD, MAPC, MAPTI, INV, SYNC, DISCARD)
 *   - Per-device ITT (Interrupt Translation Table) management
 *   - Per-CPU LPI configuration tables (PROPBASE/PENDBASE)
 *   - Windows 11 ARM64 compatible MSI/MSI-X API
 *
 *   The ITS translates:
 *     DeviceID + EventID -> LPI (via device table and ITT)
 *     LPI -> Target CPU (via collection table)
 *
 *   MSI workflow:
 *   1. Device writes EventID to GITS_TRANSLATER at ITS base + 0x10000
 *   2. ITS looks up DeviceID (from transaction) + EventID in tables
 *   3. ITS routes resulting LPI to target CPU via collection
 *
 * REFERENCES:
 *   - ARM GICv3 Architecture Specification (IHI 0069)
 *   - ARM GIC ITS Specification (part of GICv3)
 *   - Linux drivers/irqchip/irq-gic-v3-its.c
 */

#define NDEBUG
#include "gic_internal.h"

/*
 * ============================================================================
 * Global ITS State Variables
 * ============================================================================
 */

/* Multi-ITS node support */
HALP_GIC_ITS_NODE HalpGicItsNodes[HALP_GIC_MAX_ITS_NODES] = {0};
ULONG HalpGicItsNodeCount = 0;
ULONG HalpGicItsListMap = 0;  /* Bitmap of used ITS list numbers */

/* LPI allocator */
HALP_LPI_ALLOCATOR HalpGicLpiAllocator = {0};
static ULONG HalpGicLpiBitmapBuffer[(HAL_ARM64_LPI_COUNT + 31) / 32] = {0};

/* Legacy single-ITS state (backward compatibility) */
BOOLEAN HalpGicItsInitialized = FALSE;
BOOLEAN HalpGicItsInitFailed = FALSE;
volatile LONG HalpGicItsInitState = 0;

/* ITS capability fields parsed from GITS_TYPER */
ULONG HalpGicItsLpiIdBits = 0;

/* Collection table state */
BOOLEAN HalpGicItsCollectionMapped[MAXIMUM_PROCESSORS] = {0};

/* LPI configuration tables (shared across all ITS nodes) */
UCHAR *HalpGicLpiConfig = NULL;
PHYSICAL_ADDRESS HalpGicLpiConfigPa = {0};
SIZE_T HalpGicLpiConfigSize = 0;
PVOID HalpGicLpiConfigRaw = NULL;

UCHAR *HalpGicLpiPending[MAXIMUM_PROCESSORS] = {0};
PHYSICAL_ADDRESS HalpGicLpiPendingPa[MAXIMUM_PROCESSORS] = {0};
PVOID HalpGicLpiPendingRaw[MAXIMUM_PROCESSORS] = {0};

#define HALP_GIC_LPI_CPU_PREPARED    1
#define HALP_GIC_LPI_CPU_ENABLED     2
#define HALP_GIC_LPI_CPU_FAILED     -1

#define HALP_GICR_CTLR_ENABLE_LPIS                 (1u << 0)
#define HALP_GICR_LPI_INNER_CACHEABILITY_WBRAWA   (7ULL << 7)
#define HALP_GICR_LPI_INNER_SHAREABLE             (1ULL << 10)
#define HALP_GICR_LPI_OUTER_CACHEABILITY_WBRAWA   (7ULL << 56)
#define HALP_GICR_LPI_OUTER_CACHEABILITY_MASK     (7ULL << 56)

static volatile LONG HalpGicLpiCpuTableState[MAXIMUM_PROCESSORS] = {0};

// Reverse map LPI -> (DeviceId, EventId) so a dropped LPI can be re-pended via the ITS INT command (Linux its_send_int). Populated at MAPTI, cleared at unmap.
typedef struct _HALP_GIC_LPI_TARGET { ULONG DeviceId; ULONG EventId; UCHAR Valid; } HALP_GIC_LPI_TARGET;
static HALP_GIC_LPI_TARGET HalpGicLpiTarget[HAL_ARM64_LPI_COUNT] = {0};

/*
 * ============================================================================
 * Helper Functions
 * ============================================================================
 */

static ULONG
HalpGicItsLog2(
    _In_ ULONG Value)
{
    ULONG Log = 0;

    while (Value > 1)
    {
        Value >>= 1;
        Log++;
    }

    return Log;
}

static ULONG
HalpGicItsRoundUpPow2(
    _In_ ULONG Value)
{
    ULONG Pow2 = 1;

    if (Value == 0)
        return 1;

    while ((Pow2 < Value) && (Pow2 < (1UL << 30)))
        Pow2 <<= 1;

    return Pow2;
}

static ULONG
HalpGicItsDeviceHash(
    _In_ USHORT RequesterId,
    _In_ ULONG BucketCount)
{
    ULONG Mask = BucketCount ? (BucketCount - 1) : 0;
    return (ULONG)RequesterId & Mask;
}

/*
 * ============================================================================
 * Memory Allocation
 * ============================================================================
 */

/*
 * HalpGicItsAllocAligned - Allocate physically contiguous aligned memory
 *
 * Allocates memory that is both physically contiguous and aligned to the
 * specified alignment. This is required for ITS tables that must be
 * naturally aligned.
 *
 * Parameters:
 *   Size      - Size in bytes to allocate
 *   Alignment - Required alignment (power of 2, minimum PAGE_SIZE)
 *   Physical  - Receives the physical address of the aligned buffer
 *   RawOut    - Optionally receives the raw allocation pointer for freeing
 *
 * Returns:
 *   Virtual address of the aligned buffer, or NULL on failure.
 */
PVOID
HalpGicItsAllocAligned(
    _In_ SIZE_T Size,
    _In_ SIZE_T Alignment,
    _Out_ PPHYSICAL_ADDRESS Physical,
    _Out_opt_ PVOID *RawOut)
{
    PHYSICAL_ADDRESS Low;
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Boundary;
    SIZE_T Total;
    PVOID Raw;
    ULONG_PTR Aligned;
    SIZE_T Align = Alignment;

    if (Size == 0)
        return NULL;

    /* Ensure minimum PAGE_SIZE alignment */
    if (Align < PAGE_SIZE)
        Align = PAGE_SIZE;

    /* Round up to power of 2 if necessary */
    if (Align & (Align - 1))
    {
        SIZE_T Pow2 = PAGE_SIZE;
        while (Pow2 < Align)
            Pow2 <<= 1;
        Align = Pow2;
    }

    Low.QuadPart = 0;
    High.QuadPart = ~0ULL;
    Boundary.QuadPart = 0;
    Total = Size + Align;

    Raw = MmAllocateContiguousMemorySpecifyCache(Total, Low, High, Boundary, MmCached);
    if (!Raw)
        return NULL;

    /* Align the pointer */
    {
        PHYSICAL_ADDRESS RawPa = MmGetPhysicalAddress(Raw);
        SIZE_T Delta = (SIZE_T)((0 - (ULONG_PTR)RawPa.QuadPart) & (Align - 1));

        Aligned = (ULONG_PTR)Raw + Delta;
        if (Physical)
            Physical->QuadPart = RawPa.QuadPart + (LONGLONG)Delta;
    }
    if (RawOut)
        *RawOut = Raw;

    RtlZeroMemory((PVOID)Aligned, Size);
    return (PVOID)Aligned;
}

/*
 * ============================================================================
 * LPI Allocator Implementation
 * ============================================================================
 */

/*
 * HalpGicItsInitLpiAllocator - Initialize the LPI bitmap allocator
 */
BOOLEAN
HalpGicItsInitLpiAllocator(VOID)
{
    if (HalpGicLpiAllocator.BitmapBuffer != NULL)
        return TRUE;  /* Already initialized */

    RtlZeroMemory(HalpGicLpiBitmapBuffer, sizeof(HalpGicLpiBitmapBuffer));

    HalpGicLpiAllocator.BitmapBuffer = HalpGicLpiBitmapBuffer;
    HalpGicLpiAllocator.TotalLpis = HAL_ARM64_LPI_COUNT;
    HalpGicLpiAllocator.AllocatedLpis = 0;
    KeInitializeSpinLock(&HalpGicLpiAllocator.Lock);

    RtlInitializeBitMap(&HalpGicLpiAllocator.Bitmap,
                        HalpGicLpiAllocator.BitmapBuffer,
                        HAL_ARM64_LPI_COUNT);
    RtlClearAllBits(&HalpGicLpiAllocator.Bitmap);

    DPRINT1("[arm64][ITS] LPI allocator initialized: %lu LPIs available\n",
            HAL_ARM64_LPI_COUNT);

    return TRUE;
}

/*
 * HalpGicItsAllocateLpi - Allocate contiguous LPIs
 *
 * Returns the first allocated LPI number (INTID), or 0 on failure.
 * The returned LPI is already in the 8192+ INTID range.
 */
ULONG
HalpGicItsAllocateLpi(
    _In_ ULONG Count)
{
    KIRQL OldIrql;
    ULONG Index;

    if (Count == 0 || Count > HalpGicLpiAllocator.TotalLpis)
        return 0;

    KeAcquireSpinLock(&HalpGicLpiAllocator.Lock, &OldIrql);

    /* Find contiguous clear bits */
    Index = RtlFindClearBits(&HalpGicLpiAllocator.Bitmap, Count, 0);
    if (Index == (ULONG)-1)
    {
        KeReleaseSpinLock(&HalpGicLpiAllocator.Lock, OldIrql);
        DPRINT1("[arm64][ITS] Failed to allocate %lu LPIs\n", Count);
        return 0;
    }

    /* Mark bits as allocated */
    RtlSetBits(&HalpGicLpiAllocator.Bitmap, Index, Count);
    HalpGicLpiAllocator.AllocatedLpis += Count;

    KeReleaseSpinLock(&HalpGicLpiAllocator.Lock, OldIrql);

    /* Return INTID (LPI base is 8192) */
    return HAL_ARM64_LPI_BASE + Index;
}

static BOOLEAN
HalpGicItsReserveLpi(
    _In_ ULONG Lpi)
{
    KIRQL OldIrql;
    ULONG Index;
    BOOLEAN Available;

    if (Lpi < HAL_ARM64_LPI_BASE)
        return FALSE;

    Index = Lpi - HAL_ARM64_LPI_BASE;
    if (Index >= HalpGicLpiAllocator.TotalLpis)
        return FALSE;

    KeAcquireSpinLock(&HalpGicLpiAllocator.Lock, &OldIrql);
    Available = RtlAreBitsClear(&HalpGicLpiAllocator.Bitmap, Index, 1);
    if (Available)
    {
        RtlSetBits(&HalpGicLpiAllocator.Bitmap, Index, 1);
        HalpGicLpiAllocator.AllocatedLpis++;
    }
    KeReleaseSpinLock(&HalpGicLpiAllocator.Lock, OldIrql);

    return Available;
}

/*
 * HalpGicItsFreeLpi - Free previously allocated LPIs
 */
VOID
HalpGicItsFreeLpi(
    _In_ ULONG LpiBase,
    _In_ ULONG Count)
{
    KIRQL OldIrql;
    ULONG Index;

    if (LpiBase < HAL_ARM64_LPI_BASE || Count == 0)
        return;

    Index = LpiBase - HAL_ARM64_LPI_BASE;
    if (Index >= HalpGicLpiAllocator.TotalLpis)
        return;

    KeAcquireSpinLock(&HalpGicLpiAllocator.Lock, &OldIrql);

    RtlClearBits(&HalpGicLpiAllocator.Bitmap, Index, Count);
    if (HalpGicLpiAllocator.AllocatedLpis >= Count)
        HalpGicLpiAllocator.AllocatedLpis -= Count;

    KeReleaseSpinLock(&HalpGicLpiAllocator.Lock, OldIrql);
}

/*
 * ============================================================================
 * ITS Command Queue - Multi-ITS Implementation
 * ============================================================================
 */

/*
 * HalpGicItsAllocCommandQueue - Allocate and initialize command queue for an ITS node
 */
static BOOLEAN
HalpGicItsAllocCommandQueue(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode)
{
    SIZE_T QueueBytes;
    PHYSICAL_ADDRESS Low;
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Boundary;
    ULONGLONG Cbaser;

    QueueBytes = HAL_ARM64_ITS_CMD_QUEUE_SIZE;
    Low.QuadPart = 0;
    High.QuadPart = ~0ULL;
    Boundary.QuadPart = 0;

    ItsNode->CmdQueueBase = MmAllocateContiguousMemorySpecifyCache(
        QueueBytes, Low, High, Boundary, MmCached);
    if (!ItsNode->CmdQueueBase)
    {
        DPRINT1("[arm64][ITS] Failed to allocate command queue for ITS %lu\n",
                ItsNode->ItsId);
        return FALSE;
    }

    RtlZeroMemory(ItsNode->CmdQueueBase, QueueBytes);
    HalpArm64CleanDcacheRange(ItsNode->CmdQueueBase, QueueBytes);
    ItsNode->CmdQueuePa = MmGetPhysicalAddress(ItsNode->CmdQueueBase);
    ItsNode->CmdQueueEntries = HAL_ARM64_ITS_CMD_QUEUE_ENTRIES;
    ItsNode->CmdWriteIndex = 0;

    /* Program GITS_CBASER */
    Cbaser = (ItsNode->CmdQueuePa.QuadPart & GITS_CBASER_ADDR_MASK) |
             ((HAL_ARM64_ITS_CMD_QUEUE_PAGES - 1) & GITS_CBASER_SIZE_MASK) |
             GITS_CBASER_VALID;

    /* Add shareability and cacheability attributes (following Linux) */
    Cbaser |= (1ULL << 10);  /* InnerShareable */
    Cbaser |= (1ULL << 59);  /* RaWaWb */

    HalpMmioWrite64(ItsNode->VirtualBase, GITS_CBASER, Cbaser);

    HalpMmioWrite64(ItsNode->VirtualBase, GITS_CWRITER, 0);

    DPRINT("[arm64][ITS] ITS %lu: Command queue at PA 0x%llx, %lu entries\n",
           ItsNode->ItsId, ItsNode->CmdQueuePa.QuadPart, ItsNode->CmdQueueEntries);

    return TRUE;
}

/*
 * HalpGicItsPostCommandOnNode - Post a command to an ITS node's command queue
 */
static BOOLEAN
HalpGicItsPostCommandOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_reads_(4) const UINT64 *Cmd)
{
    ULONG Next;
    ULONG ReadIndex;
    ULONG Index;
    ULONG Spins;
    ULONGLONG Target;
    KIRQL OldIrql;
    UINT64 *QueueEntry;

    if (!ItsNode->CmdQueueBase || ItsNode->CmdQueueEntries == 0)
        return FALSE;

    KeAcquireSpinLock(&ItsNode->CmdLock, &OldIrql);

    /* Wait for space in command queue */
    for (Spins = 100000; Spins != 0; --Spins)
    {
        ULONGLONG ReadOffset = HalpMmioRead64(ItsNode->VirtualBase, GITS_CREADR);
        ReadIndex = (ULONG)(ReadOffset / HAL_ARM64_ITS_CMD_ENTRY_SIZE);
        Next = (ItsNode->CmdWriteIndex + 1) % ItsNode->CmdQueueEntries;
        if (Next != ReadIndex)
            break;
        KeStallExecutionProcessor(1);
    }

    if (Spins == 0)
    {
        KeReleaseSpinLock(&ItsNode->CmdLock, OldIrql);
        DPRINT1("[arm64][ITS] Command queue full on ITS %lu\n", ItsNode->ItsId);
        return FALSE;
    }

    /* Write command to queue */
    Index = ItsNode->CmdWriteIndex;
    QueueEntry = (UINT64 *)((ULONG_PTR)ItsNode->CmdQueueBase + Index * HAL_ARM64_ITS_CMD_ENTRY_SIZE);
    QueueEntry[0] = Cmd[0];
    QueueEntry[1] = Cmd[1];
    QueueEntry[2] = Cmd[2];
    QueueEntry[3] = Cmd[3];

    HalpArm64CleanDcacheRange(QueueEntry, HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    /* Memory barrier before updating write pointer */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /* Update write pointer */
    ItsNode->CmdWriteIndex = Next;
    Target = (ULONGLONG)Next * HAL_ARM64_ITS_CMD_ENTRY_SIZE;
    HalpMmioWrite64(ItsNode->VirtualBase, GITS_CWRITER, Target);

    for (Spins = 20000; Spins != 0; --Spins)
    {
        if (HalpMmioRead64(ItsNode->VirtualBase, GITS_CREADR) == Target)
            break;
        KeStallExecutionProcessor(1);
    }

    KeReleaseSpinLock(&ItsNode->CmdLock, OldIrql);

    if (Spins == 0)
    {
        DPRINT1("[arm64][ITS] ITS %lu cmd 0x%02x stalled: CREADR=0x%llx CWRITER=0x%llx CTLR=0x%llx CBASER=0x%llx cmd2=0x%llx\n",
                ItsNode->ItsId, (ULONG)(Cmd[0] & 0xFF),
                HalpMmioRead64(ItsNode->VirtualBase, GITS_CREADR), Target,
                (ULONGLONG)*(volatile ULONG *)(ItsNode->VirtualBase + GITS_CTLR),
                HalpMmioRead64(ItsNode->VirtualBase, GITS_CBASER),
                Cmd[2]);
        return FALSE;
    }

    return TRUE;
}

/*
 * ============================================================================
 * ITS Command Building Functions
 * ============================================================================
 *
 * Following Linux's its_encode_* pattern from irq-gic-v3-its.c
 */

static VOID
HalpGicItsBuildMapdCmd(
    _Out_writes_(4) UINT64 *Cmd,
    _In_ ULONG DeviceId,
    _In_ ULONG IttEntries,
    _In_ ULONGLONG IttPa,
    _In_ BOOLEAN Valid)
{
    ULONG Log2Entries;
    ULONG SizeField;

    RtlZeroMemory(Cmd, HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    Log2Entries = HalpGicItsLog2(IttEntries);
    if (Log2Entries == 0)
        Log2Entries = 1;
    SizeField = Log2Entries - 1;

    /* Cmd[0]: Command + DeviceID */
    Cmd[0] = (UINT64)GITS_CMD_MAPD;
    Cmd[0] |= ((UINT64)DeviceId) << 32;

    /* Cmd[1]: Size (bits 4:0) */
    Cmd[1] = (UINT64)(SizeField & 0x1FULL);

    /* Cmd[2]: ITT address (bits 51:8) + Valid (bit 63) */
    Cmd[2] = ((IttPa >> 8) & ((1ULL << 44) - 1)) << 8;
    if (Valid)
        Cmd[2] |= (1ULL << 63);
}

static VOID
HalpGicItsBuildMapcCmd(
    _Out_writes_(4) UINT64 *Cmd,
    _In_ ULONG CollectionId,
    _In_ ULONGLONG Rdbase,
    _In_ BOOLEAN Valid)
{
    RtlZeroMemory(Cmd, HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    /* Cmd[0]: Command */
    Cmd[0] = (UINT64)GITS_CMD_MAPC;

    /* Cmd[2]: Target address (bits 51:16) + CollectionID (bits 15:0) + Valid (bit 63) */
    Cmd[2] = Rdbase & (((1ULL << 36) - 1) << 16);
    Cmd[2] |= (UINT64)(CollectionId & 0xFFFFu);
    if (Valid)
        Cmd[2] |= (1ULL << 63);
}

static VOID
HalpGicItsBuildMaptiCmd(
    _Out_writes_(4) UINT64 *Cmd,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG PhysId,
    _In_ ULONG CollectionId)
{
    RtlZeroMemory(Cmd, HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    /* Cmd[0]: Command + DeviceID */
    Cmd[0] = (UINT64)GITS_CMD_MAPTI;
    Cmd[0] |= ((UINT64)DeviceId) << 32;

    /* Cmd[1]: EventID (bits 31:0) + PhysID/LPI (bits 63:32) */
    Cmd[1] = (UINT64)EventId;
    Cmd[1] |= ((UINT64)PhysId) << 32;

    /* Cmd[2]: CollectionID (bits 15:0) */
    Cmd[2] = (UINT64)(CollectionId & 0xFFFFu);
}

// Linux its_build_int_cmd: INT makes the LPI for (DeviceId,EventId) pending again.
static VOID
HalpGicItsBuildIntCmd(
    _Out_writes_(4) UINT64 *Cmd,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId)
{
    RtlZeroMemory(Cmd, HAL_ARM64_ITS_CMD_ENTRY_SIZE);
    Cmd[0] = (UINT64)GITS_CMD_INT;
    Cmd[0] |= ((UINT64)DeviceId) << 32;
    Cmd[1] = (UINT64)EventId;
}

static VOID
HalpGicItsBuildSyncCmd(
    _Out_writes_(4) UINT64 *Cmd,
    _In_ ULONGLONG Rdbase)
{
    RtlZeroMemory(Cmd, HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    /* Cmd[0]: Command */
    Cmd[0] = (UINT64)GITS_CMD_SYNC;

    /* Cmd[2]: Target address (bits 51:16) */
    Cmd[2] = Rdbase & (((1ULL << 36) - 1) << 16);
}

static UINT64
HalpGicItsEncodeRdbase(
    _In_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG Cpu,
    _In_ ULONGLONG TargetAddress)
{
    ULONG_PTR GicrBase;
    ULONG ProcessorNumber;

    if (ItsNode->RdbasePhysical)
    {
        if ((LONGLONG)TargetAddress < 0)
        {
            PHYSICAL_ADDRESS TargetPa =
                MmGetPhysicalAddress((PVOID)(ULONG_PTR)TargetAddress);

            TargetAddress = (ULONGLONG)TargetPa.QuadPart;
        }

        return TargetAddress & (((1ULL << 36) - 1) << 16);
    }

    ProcessorNumber = Cpu;
    GicrBase = HalpGicrBase(Cpu);
    if (GicrBase != 0)
    {
        ProcessorNumber =
            (ULONG)((HalpMmioRead64(GicrBase, GICR_TYPER) >> 8) & 0xFFFF);
    }

    return ((UINT64)ProcessorNumber & 0xFFFFULL) << 16;
}

/*
 * ============================================================================
 * ITS Command Send Functions - Multi-ITS
 * ============================================================================
 */

BOOLEAN
HalpGicItsSendMapdOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG IttEntries,
    _In_ ULONGLONG IttPa,
    _In_ BOOLEAN Valid)
{
    UINT64 Cmd[4];

    HalpGicItsBuildMapdCmd(Cmd, DeviceId, IttEntries, IttPa, Valid);
    return HalpGicItsPostCommandOnNode(ItsNode, Cmd);
}

BOOLEAN
HalpGicItsSendMapcOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG CollectionId,
    _In_ ULONGLONG TargetAddress,
    _In_ BOOLEAN Valid)
{
    UINT64 Cmd[4];
    UINT64 Sync[4];
    UINT64 Rdbase;

    Rdbase = HalpGicItsEncodeRdbase(ItsNode, CollectionId, TargetAddress);

    HalpGicItsBuildMapcCmd(Cmd, CollectionId, Rdbase, Valid);
    if (!HalpGicItsPostCommandOnNode(ItsNode, Cmd))
        return FALSE;

    HalpGicItsBuildSyncCmd(Sync, Rdbase);
    return HalpGicItsPostCommandOnNode(ItsNode, Sync);
}

BOOLEAN
HalpGicItsSendMaptiOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG PhysId,
    _In_ ULONG CollectionId)
{
    UINT64 Cmd[4];

    HalpGicItsBuildMaptiCmd(Cmd, DeviceId, EventId, PhysId, CollectionId);
    return HalpGicItsPostCommandOnNode(ItsNode, Cmd);
}

BOOLEAN
HalpGicItsSendIntOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId)
{
    UINT64 Cmd[4];

    HalpGicItsBuildIntCmd(Cmd, DeviceId, EventId);
    return HalpGicItsPostCommandOnNode(ItsNode, Cmd);
}

BOOLEAN
HalpGicItsSendSyncOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG Cpu,
    _In_ ULONGLONG TargetAddress)
{
    UINT64 Cmd[4];

    HalpGicItsBuildSyncCmd(Cmd, HalpGicItsEncodeRdbase(ItsNode, Cpu, TargetAddress));
    return HalpGicItsPostCommandOnNode(ItsNode, Cmd);
}

/*
 * ============================================================================
 * Legacy Single-ITS Command Functions (Backward Compatibility)
 * ============================================================================
 */

/*
 * ============================================================================
 * LPI Configuration Functions
 * ============================================================================
 */

static VOID
HalpGicItsInvalidateLpiConfig(VOID)
{
    ULONG Node;

    for (Node = 0; Node < HalpGicItsNodeCount; ++Node)
    {
        PHALP_GIC_ITS_NODE ItsNode = &HalpGicItsNodes[Node];
        ULONG Cpu;

        if (!ItsNode->Enabled)
            continue;

        for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; ++Cpu)
        {
            UINT64 Cmd[4];

            if (!ItsNode->CollectionMapped[Cpu])
                continue;

            RtlZeroMemory(Cmd, HAL_ARM64_ITS_CMD_ENTRY_SIZE);
            Cmd[0] = (UINT64)GITS_CMD_INVALL;
            Cmd[2] = (UINT64)(Cpu & 0xFFFFu); /* ICID = collection = CPU */
            if (HalpGicItsPostCommandOnNode(ItsNode, Cmd))
                HalpGicItsSendSyncOnNode(ItsNode, Cpu, ItsNode->CollectionTarget[Cpu]);
        }
    }
}

VOID
HalpGicItsEnableLpi(
    _In_ ULONG Lpi)
{
    ULONG Index;

    if (!HalpGicLpiConfig || Lpi < HAL_ARM64_LPI_BASE)
        return;

    Index = Lpi - HAL_ARM64_LPI_BASE;
    if (Index >= HalpGicLpiCount)
        return;

    /* RMW: keep the per-IRQL priority programmed at connect (HalpGicItsSetLpiPriority),
     * only set GROUP1 | ENABLED. Clobbering it with the default would break PMR
     * masking for any LPI whose IRQL != the default's level (bugcheck 0x0F). */
    {
        UCHAR Old, New;
        do {
            Old = HalpGicLpiConfig[Index];
            New = (UCHAR)((Old & 0xFCu) | HAL_ARM64_LPI_PROP_GROUP1 | HAL_ARM64_LPI_PROP_ENABLED);
        } while ((UCHAR)_InterlockedCompareExchange8((volatile char *)&HalpGicLpiConfig[Index], (char)New, (char)Old) != Old);
    }
    HalpArm64CleanDcacheRange(&HalpGicLpiConfig[Index], sizeof(UCHAR));

    /*
     * The redistributor caches LPI configuration; the table write above is not
     * observed until an INVALL is issued, so the LPI can stay disabled or keep
     * its old priority. Invalidate all mapped collections because an MSI can be
     * routed to a CPU selected from affinity, not necessarily the current CPU.
     */
    HalpGicItsInvalidateLpiConfig();
}

VOID
HalpGicItsDisableLpi(
    _In_ ULONG Lpi)
{
    ULONG Index;

    if (!HalpGicLpiConfig || Lpi < HAL_ARM64_LPI_BASE)
        return;

    Index = Lpi - HAL_ARM64_LPI_BASE;
    if (Index >= HalpGicLpiCount)
        return;

    /* RMW: clear ENABLED, preserve the per-IRQL priority (like Linux). */
    {
        UCHAR Old, New;
        do {
            Old = HalpGicLpiConfig[Index];
            New = (UCHAR)((Old & 0xFCu) | HAL_ARM64_LPI_PROP_GROUP1);
        } while ((UCHAR)_InterlockedCompareExchange8((volatile char *)&HalpGicLpiConfig[Index], (char)New, (char)Old) != Old);
    }
    HalpArm64CleanDcacheRange(&HalpGicLpiConfig[Index], sizeof(UCHAR));
    HalpGicItsInvalidateLpiConfig();
}

VOID
HalpGicItsSetLpiPriority(
    _In_ ULONG Lpi,
    _In_ UCHAR Priority)
{
    ULONG Index;
    UCHAR Config;

    if (!HalpGicLpiConfig || Lpi < HAL_ARM64_LPI_BASE)
        return;

    Index = Lpi - HAL_ARM64_LPI_BASE;
    if (Index >= HalpGicLpiCount)
        return;

    {
        UCHAR Old;
        do {
            Old = HalpGicLpiConfig[Index];
            Config = (UCHAR)((Old & 0x03) | (Priority & 0xFC));  /* Keep enabled/group, update priority */
        } while ((UCHAR)_InterlockedCompareExchange8((volatile char *)&HalpGicLpiConfig[Index], (char)Config, (char)Old) != Old);
    }
    HalpArm64CleanDcacheRange(&HalpGicLpiConfig[Index], sizeof(UCHAR));
}

/*
 * ============================================================================
 * ITS BASER Table Setup
 * ============================================================================
 */

static BOOLEAN
HalpGicItsSetupBaserTableOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG Type,
    _In_ ULONG DesiredEntries,
    _Out_ PVOID *TableRaw,
    _Out_ PVOID *Table,
    _Out_ PPHYSICAL_ADDRESS TablePa,
    _Out_ PSIZE_T TableBytes,
    _Out_ PULONG ActualEntries)
{
    ULONG Index;

    for (Index = 0; Index < HAL_ARM64_GITS_BASER_COUNT; ++Index)
    {
        ULONGLONG Baser = HalpMmioRead64(ItsNode->VirtualBase, GITS_BASER + (Index * 8));
        ULONG BaserType = (ULONG)((Baser >> GITS_BASER_TYPE_SHIFT) & GITS_BASER_TYPE_MASK);
        ULONG EntrySize;
        ULONG PageField;
        SIZE_T PageSize;
        ULONG EntriesPerPage;
        ULONG MaxPages = 256;
        ULONG Entries;
        ULONG Pages;
        SIZE_T Bytes;
        PHYSICAL_ADDRESS Pa;
        PVOID Raw;
        PVOID Aligned;
        ULONGLONG NewBaser;

        if (BaserType != Type)
            continue;

        EntrySize = (ULONG)((Baser >> GITS_BASER_ENTRY_SHIFT) & GITS_BASER_ENTRY_MASK) + 1;
        PageField = (ULONG)((Baser >> GITS_BASER_PAGE_SHIFT) & GITS_BASER_PAGE_MASK);
        PageSize = 4096ULL << (PageField * 2);
        if (PageSize < PAGE_SIZE)
            PageSize = PAGE_SIZE;

        EntriesPerPage = (ULONG)(PageSize / EntrySize);
        if (EntriesPerPage == 0)
            return FALSE;

        Entries = DesiredEntries;
        if (Entries == 0)
            Entries = EntriesPerPage;

        if (Entries > EntriesPerPage * MaxPages)
            Entries = EntriesPerPage * MaxPages;

        Pages = (Entries + EntriesPerPage - 1) / EntriesPerPage;
        if (Pages == 0)
            Pages = 1;
        if (Pages > MaxPages)
            Pages = MaxPages;

        Bytes = (SIZE_T)Pages * PageSize;
        Aligned = HalpGicItsAllocAligned(Bytes, PageSize, &Pa, &Raw);
        if (!Aligned)
            return FALSE;

        HalpArm64CleanInvalidateDcacheRange(Aligned, Bytes);

        NewBaser = Baser;
        NewBaser &= ~(GITS_BASER_ADDR_MASK | GITS_BASER_SIZE_MASK | GITS_BASER_VALID);
        NewBaser |= (Pa.QuadPart & GITS_BASER_ADDR_MASK);
        NewBaser |= ((ULONGLONG)(Pages - 1) & GITS_BASER_SIZE_MASK);
        NewBaser |= GITS_BASER_VALID;

        HalpMmioWrite64(ItsNode->VirtualBase, GITS_BASER + (Index * 8), NewBaser);

        if (TableRaw) *TableRaw = Raw;
        if (Table) *Table = Aligned;
        if (TablePa) *TablePa = Pa;
        if (TableBytes) *TableBytes = Bytes;
        if (ActualEntries) *ActualEntries = EntriesPerPage * Pages;

        DPRINT("[arm64][ITS] ITS %lu BASER[%lu] type %lu: %lu entries, %llu bytes\n",
               ItsNode->ItsId, Index, Type, EntriesPerPage * Pages, (ULONGLONG)Bytes);

        return TRUE;
    }

    return FALSE;
}

/*
 * ============================================================================
 * Device Map Management (Per-ITS Node)
 * ============================================================================
 */

static BOOLEAN
HalpGicItsInitDeviceMapOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceLimit)
{
    ULONG Target;
    SIZE_T Bytes;

    if (ItsNode->DeviceBuckets)
        return TRUE;

    Target = DeviceLimit;
    if (Target == 0)
        Target = 1;
    if (Target > 4096)
        Target = 4096;

    ItsNode->DeviceBucketCount = 1;
    while (ItsNode->DeviceBucketCount < Target)
        ItsNode->DeviceBucketCount <<= 1;

    if (ItsNode->DeviceBucketCount < 64)
        ItsNode->DeviceBucketCount = 64;

    Bytes = (SIZE_T)ItsNode->DeviceBucketCount * sizeof(PHALP_ARM64_ITS_DEVICE);
    ItsNode->DeviceBuckets = ExAllocatePoolWithTag(NonPagedPool, Bytes, TAG_HAL);
    if (!ItsNode->DeviceBuckets)
        return FALSE;

    RtlZeroMemory(ItsNode->DeviceBuckets, Bytes);
    return TRUE;
}

static PHALP_ARM64_ITS_DEVICE
HalpGicItsFindDeviceOnNode(
    _In_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ USHORT RequesterId)
{
    PHALP_ARM64_ITS_DEVICE Device;
    ULONG Bucket;

    if (!ItsNode->DeviceBuckets || ItsNode->DeviceBucketCount == 0)
        return NULL;

    Bucket = HalpGicItsDeviceHash(RequesterId, ItsNode->DeviceBucketCount);
    Device = ItsNode->DeviceBuckets[Bucket];
    while (Device)
    {
        if (Device->RequesterId == RequesterId)
            return Device;
        Device = Device->Next;
    }

    return NULL;
}

static PHALP_ARM64_ITS_DEVICE
HalpGicItsAllocateDeviceOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ USHORT RequesterId)
{
    PHALP_ARM64_ITS_DEVICE Device;
    ULONG Bucket;

    if (!ItsNode->DeviceBuckets || ItsNode->DeviceBucketCount == 0)
        return NULL;

    Device = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Device), TAG_HAL);
    if (!Device)
        return NULL;

    RtlZeroMemory(Device, sizeof(*Device));
    Device->RequesterId = RequesterId;
    Device->ItsNode = ItsNode;
    Device->InitState = 0;

    Bucket = HalpGicItsDeviceHash(RequesterId, ItsNode->DeviceBucketCount);
    Device->Next = ItsNode->DeviceBuckets[Bucket];
    ItsNode->DeviceBuckets[Bucket] = Device;
    ItsNode->DeviceCount++;

    return Device;
}

/*
 * ============================================================================
 * Device Management Public API
 * ============================================================================
 */

/*
 * HalpGicItsSelectNodeForDevice - Select ITS node for a device
 *
 * For multi-ITS systems, select which ITS node should handle a device.
 * Currently uses round-robin based on DeviceID.
 */
PHALP_GIC_ITS_NODE
HalpGicItsSelectNodeForDevice(
    _In_ ULONG DeviceId)
{
    ULONG Index;

    if (HalpGicItsNodeCount == 0)
        return NULL;

    if (HalpGicItsNodeCount == 1)
        return &HalpGicItsNodes[0];

    /* Round-robin distribution across ITS nodes */
    Index = DeviceId % HalpGicItsNodeCount;
    return &HalpGicItsNodes[Index];
}

/*
 * HalpGicItsCreateDevice - Create a device on an ITS node
 */
PHALP_ARM64_ITS_DEVICE
HalpGicItsCreateDevice(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG NrEvents)
{
    PHALP_ARM64_ITS_DEVICE Device;
    KIRQL OldIrql;
    ULONG Entries;
    SIZE_T IttBytes;
    PHYSICAL_ADDRESS IttPa;

    if (!ItsNode || !ItsNode->Enabled)
        return NULL;

    if (DeviceId >= ItsNode->MaxDeviceId)
        return NULL;

    KeAcquireSpinLock(&ItsNode->DeviceLock, &OldIrql);
    Device = HalpGicItsFindDeviceOnNode(ItsNode, (USHORT)DeviceId);
    if (!Device)
        Device = HalpGicItsAllocateDeviceOnNode(ItsNode, (USHORT)DeviceId);
    KeReleaseSpinLock(&ItsNode->DeviceLock, OldIrql);

    if (!Device)
        return NULL;

    /* Check if already initialized */
    if (Device->InitState == 2)
    {
        __asm__ __volatile__("dmb ish" ::: "memory");
        return Device;
    }
    if (Device->InitState == -1)
        return NULL;

    if (InterlockedCompareExchange(&Device->InitState, 1, 0) != 0)
    {
        /* Someone else is initializing, wait */
        for (ULONG Spins = 100000; Spins != 0; --Spins)
        {
            if (Device->InitState == 2)
            {
                __asm__ __volatile__("dmb ish" ::: "memory");
                return Device;
            }
            if (Device->InitState == -1)
                return NULL;
            KeStallExecutionProcessor(1);
        }
        return NULL;
    }

    /* Calculate ITT size */
    Entries = NrEvents;
    if (Entries < 2)
        Entries = 2;
    Entries = HalpGicItsRoundUpPow2(Entries);
    Device->MaxEvents = Entries;

    IttBytes = (SIZE_T)Entries * ItsNode->IttEntrySize;
    if (IttBytes < HAL_ARM64_ITS_ITT_ALIGN)
        IttBytes = HAL_ARM64_ITS_ITT_ALIGN;

    Device->IttVa = HalpGicItsAllocAligned(IttBytes, HAL_ARM64_ITS_ITT_ALIGN, &IttPa, NULL);
    if (!Device->IttVa)
    {
        InterlockedExchange(&Device->InitState, -1);
        return NULL;
    }

    HalpArm64CleanInvalidateDcacheRange(Device->IttVa, IttBytes);

    Device->IttEntries = Entries;
    Device->IttPa = IttPa;
    Device->IttSize = IttBytes;

    /* Allocate event tracking arrays */
    Device->EventToLpi = ExAllocatePoolWithTag(NonPagedPool,
                                                Entries * sizeof(ULONG), TAG_HAL);
    Device->EventToCollection = ExAllocatePoolWithTag(NonPagedPool,
                                                       Entries * sizeof(ULONG), TAG_HAL);
    if (!Device->EventToLpi || !Device->EventToCollection)
    {
        if (Device->EventToLpi)
            ExFreePoolWithTag(Device->EventToLpi, TAG_HAL);
        if (Device->EventToCollection)
            ExFreePoolWithTag(Device->EventToCollection, TAG_HAL);
        InterlockedExchange(&Device->InitState, -1);
        return NULL;
    }

    RtlZeroMemory(Device->EventToLpi, Entries * sizeof(ULONG));
    RtlZeroMemory(Device->EventToCollection, Entries * sizeof(ULONG));

    /* Send MAPD command to map device to ITT */
    if (!HalpGicItsSendMapdOnNode(ItsNode, DeviceId, Entries, IttPa.QuadPart, TRUE))
    {
        ExFreePoolWithTag(Device->EventToLpi, TAG_HAL);
        ExFreePoolWithTag(Device->EventToCollection, TAG_HAL);
        InterlockedExchange(&Device->InitState, -1);
        return NULL;
    }

    InterlockedExchange(&Device->InitState, 2);

    DPRINT("[arm64][ITS] Created device %u on ITS %lu: %lu events, ITT at PA 0x%llx\n",
           DeviceId, ItsNode->ItsId, Entries, IttPa.QuadPart);

    return Device;
}

/*
 * ============================================================================
 * Collection Management
 * ============================================================================
 */

BOOLEAN
HalpGicItsEnsureCollectionOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG Cpu)
{
    ULONGLONG Target;

    if (Cpu >= MAXIMUM_PROCESSORS)
        return FALSE;

    if (!ItsNode || !ItsNode->Enabled)
        return FALSE;

    if (ItsNode->CollectionMapped[Cpu])
        return TRUE;

    /* Get target redistributor address for this CPU */
    Target = (ULONGLONG)HalpGicrBase(Cpu);
    if (Target == 0)
        return FALSE;

    if (!HalpGicItsSendMapcOnNode(ItsNode, Cpu, Target, TRUE))
        return FALSE;

    ItsNode->CollectionMapped[Cpu] = TRUE;
    ItsNode->CollectionTarget[Cpu] = Target;

    DPRINT("[arm64][ITS] Mapped collection %lu to GICR 0x%llx on ITS %lu\n",
           Cpu, Target, ItsNode->ItsId);

    return TRUE;
}

BOOLEAN
HalpGicItsEnsureCollection(
    _In_ ULONG Cpu)
{
    ULONG Index;
    BOOLEAN Result = TRUE;

    /* Ensure collection on all ITS nodes */
    for (Index = 0; Index < HalpGicItsNodeCount; ++Index)
    {
        if (!HalpGicItsEnsureCollectionOnNode(&HalpGicItsNodes[Index], Cpu))
            Result = FALSE;
    }

    return Result;
}

/*
 * ============================================================================
 * LPI Table Initialization
 * ============================================================================
 */

static BOOLEAN
HalpGicItsInitLpiTables(VOID)
{
    ULONG MaxBits = 0;
    ULONG Bits;
    SIZE_T PropSize;
    UCHAR DefaultProp;

    while ((1u << MaxBits) < HAL_ARM64_LPI_COUNT && MaxBits < 31)
        MaxBits++;

    Bits = MaxBits;
    if (Bits < HAL_ARM64_LPI_ID_BITS_DEFAULT)
        Bits = HAL_ARM64_LPI_ID_BITS_DEFAULT;
    if (Bits > HAL_ARM64_LPI_ID_BITS_MAX)
        Bits = HAL_ARM64_LPI_ID_BITS_MAX;

    HalpGicItsLpiIdBits = Bits;
    HalpGicLpiCount = 1u << Bits;

    /* PROPBASE size must be 64KB aligned */
    PropSize = HalpGicLpiCount;
    PropSize = (PropSize + 0xFFFF) & ~((SIZE_T)0xFFFF);

    HalpGicLpiConfig = HalpGicItsAllocAligned(PropSize, 0x10000,
                                              &HalpGicLpiConfigPa,
                                              &HalpGicLpiConfigRaw);
    if (!HalpGicLpiConfig)
    {
        DPRINT1("[arm64][ITS] Failed to allocate LPI config table\n");
        return FALSE;
    }

    HalpGicLpiConfigSize = PropSize;

    /* Initialize all LPIs as disabled with default priority */
    DefaultProp = (UCHAR)(HAL_ARM64_LPI_PROP_PRIO_DEFAULT | HAL_ARM64_LPI_PROP_GROUP1);
    RtlFillMemory(HalpGicLpiConfig, PropSize, DefaultProp);
    HalpArm64CleanDcacheRange(HalpGicLpiConfig, PropSize);

    DPRINT1("[arm64][ITS] LPI config table: %lu LPIs, PA 0x%llx, size %llu\n",
            HalpGicLpiCount, HalpGicLpiConfigPa.QuadPart, (ULONGLONG)PropSize);

    return TRUE;
}

static BOOLEAN
HalpGicItsPrepareCpuTables(
    _In_ ULONG Cpu)
{
    SIZE_T PendingSize;

    if ((Cpu >= MAXIMUM_PROCESSORS) || (HalpGicItsLpiIdBits == 0) || (HalpGicLpiConfig == NULL))
        return FALSE;

    /* PENDBASE size: 1 bit per LPI, 64KB aligned */
    PendingSize = (HalpGicLpiCount + 7) / 8;
    PendingSize = (PendingSize + 0xFFFF) & ~((SIZE_T)0xFFFF);

    if (!HalpGicLpiPending[Cpu])
    {
        HalpGicLpiPending[Cpu] = HalpGicItsAllocAligned(PendingSize, 0x10000, &HalpGicLpiPendingPa[Cpu], &HalpGicLpiPendingRaw[Cpu]);
        if (!HalpGicLpiPending[Cpu])
        {
            DPRINT1("[arm64][ITS] Failed to allocate pending table for CPU %lu\n", Cpu);
            return FALSE;
        }

        RtlZeroMemory(HalpGicLpiPending[Cpu], PendingSize);
        HalpArm64CleanInvalidateDcacheRange(HalpGicLpiPending[Cpu], PendingSize);
    }

    InterlockedExchange(&HalpGicLpiCpuTableState[Cpu], HALP_GIC_LPI_CPU_PREPARED);
    return TRUE;
}

static BOOLEAN
HalpGicItsValidateCpuTables(
    _In_ ULONG Cpu,
    _In_ ULONG_PTR Base,
    _In_ ULONGLONG ExpectedProp,
    _In_ ULONGLONG ExpectedPend)
{
    ULONGLONG Prop;
    ULONGLONG Pend;
    ULONGLONG Outer;
    ULONG Ctlr;

    Ctlr = *HalpMmio(Base, GICR_CTLR);
    if (!(Ctlr & HALP_GICR_CTLR_ENABLE_LPIS))
        return FALSE;

    __asm__ __volatile__("dsb sy" ::: "memory");
    Pend = HalpMmioRead64(Base, GICR_PENDBASER);
    if ((Pend & GICR_PENDBASER_ADDR_MASK) != (ExpectedPend & GICR_PENDBASER_ADDR_MASK))
        return FALSE;
    if ((Pend & HALP_GICR_LPI_INNER_CACHEABILITY_WBRAWA) != HALP_GICR_LPI_INNER_CACHEABILITY_WBRAWA)
        return FALSE;
    Outer = Pend & HALP_GICR_LPI_OUTER_CACHEABILITY_MASK;
    if ((Outer != 0) && (Outer != HALP_GICR_LPI_OUTER_CACHEABILITY_WBRAWA))
        return FALSE;

    __asm__ __volatile__("dsb sy" ::: "memory");
    Prop = HalpMmioRead64(Base, GICR_PROPBASER);
    if ((Prop & GICR_PROPBASER_ADDR_MASK) != (ExpectedProp & GICR_PROPBASER_ADDR_MASK))
        return FALSE;
    if ((Prop & HALP_GICR_LPI_INNER_CACHEABILITY_WBRAWA) != HALP_GICR_LPI_INNER_CACHEABILITY_WBRAWA)
        return FALSE;
    Outer = Prop & HALP_GICR_LPI_OUTER_CACHEABILITY_MASK;
    if ((Outer != 0) && (Outer != HALP_GICR_LPI_OUTER_CACHEABILITY_WBRAWA))
        return FALSE;

    InterlockedExchange(&HalpGicLpiCpuTableState[Cpu], HALP_GIC_LPI_CPU_ENABLED);
    return TRUE;
}

static BOOLEAN
HalpGicItsProgramCpuTablesLocal(
    _In_ ULONG Cpu)
{
    ULONG CurrentCpu;
    ULONG_PTR Base;
    ULONGLONG Typer;
    ULONGLONG Prop;
    ULONGLONG Pend;
    ULONG Ctlr;

    CurrentCpu = KeGetCurrentProcessorNumber();
    if ((Cpu >= MAXIMUM_PROCESSORS) || (Cpu != CurrentCpu) || (HalpGicLpiCpuTableState[Cpu] < HALP_GIC_LPI_CPU_PREPARED))
        return FALSE;

    Base = HalpGicrBase(Cpu);
    if ((Base == 0) || (HalpGicLpiConfig == NULL) || (HalpGicLpiPending[Cpu] == NULL))
        return FALSE;

    Typer = HalpMmioRead64(Base, GICR_TYPER);
    if (!(Typer & HALP_GICR_TYPER_PLPIS))
        return FALSE;

    Prop = (HalpGicLpiConfigPa.QuadPart & GICR_PROPBASER_ADDR_MASK);
    Prop |= ((ULONGLONG)HalpGicItsLpiIdBits & GICR_PROPBASER_IDBITS_MASK);
    Prop |= HALP_GICR_LPI_INNER_SHAREABLE;
    Prop |= HALP_GICR_LPI_INNER_CACHEABILITY_WBRAWA;
    Prop |= HALP_GICR_LPI_OUTER_CACHEABILITY_WBRAWA;

    Pend = (HalpGicLpiPendingPa[Cpu].QuadPart & GICR_PENDBASER_ADDR_MASK);
    Pend |= HALP_GICR_LPI_INNER_SHAREABLE;
    Pend |= HALP_GICR_LPI_INNER_CACHEABILITY_WBRAWA;
    Pend |= HALP_GICR_LPI_OUTER_CACHEABILITY_WBRAWA;

    Ctlr = *HalpMmio(Base, GICR_CTLR);
    if (!(Ctlr & HALP_GICR_CTLR_ENABLE_LPIS))
    {
        __asm__ __volatile__("dsb sy" ::: "memory");
        HalpMmioWrite64(Base, GICR_PROPBASER, Prop);
        __asm__ __volatile__("dsb sy" ::: "memory");
        HalpMmioWrite64(Base, GICR_PENDBASER, Pend);
        __asm__ __volatile__("dsb sy" ::: "memory");
        *HalpMmio(Base, GICR_CTLR) = Ctlr | HALP_GICR_CTLR_ENABLE_LPIS;
        __asm__ __volatile__("dsb sy" ::: "memory");
    }

    return HalpGicItsValidateCpuTables(Cpu, Base, Prop, Pend);
}

static ULONG_PTR NTAPI
HalpGicItsProgramCpuTablesIpi(
    _In_ ULONG_PTR Argument)
{
    ULONG Cpu;

    UNREFERENCED_PARAMETER(Argument);
    Cpu = KeGetCurrentProcessorNumber();
    if (!HalpGicItsProgramCpuTablesLocal(Cpu))
    {
        InterlockedExchange(&HalpGicLpiCpuTableState[Cpu], HALP_GIC_LPI_CPU_FAILED);
        return FALSE;
    }

    return TRUE;
}

/*
 * HalpGicItsProgramCpuTables - Program the current CPU's PROPBASE/PENDBASE
 */
BOOLEAN
HalpGicItsProgramCpuTables(
    _In_ ULONG Cpu)
{
    if (!HalpGicItsProgramCpuTablesLocal(Cpu))
        return FALSE;

    DPRINT("[arm64][ITS] Programmed local LPI tables for CPU %lu\n", Cpu);
    return TRUE;
}

static NTSTATUS
HalpGicItsValidateTargetCpu(
    _In_ ULONG Cpu)
{
    KAFFINITY ActiveProcessors;

    if ((Cpu >= MAXIMUM_PROCESSORS) || (Cpu >= (sizeof(KAFFINITY) * 8)))
        return STATUS_INVALID_PARAMETER;

    ActiveProcessors = KeQueryActiveProcessors();
    if (!(ActiveProcessors & ((KAFFINITY)1 << Cpu)))
        return STATUS_INVALID_PARAMETER;

    if (InterlockedCompareExchange(&HalpGicLpiCpuTableState[Cpu], 0, 0) != HALP_GIC_LPI_CPU_ENABLED)
        return STATUS_DEVICE_NOT_READY;

    return STATUS_SUCCESS;
}

/*
 * ============================================================================
 * ITS Node Probing
 * ============================================================================
 */

/*
 * HalpGicItsProbeNode - Probe and initialize a single ITS node
 *
 * Following Linux's its_probe_one() pattern:
 * 1. Read GITS_TYPER for capabilities
 * 2. Allocate command queue
 * 3. Allocate device table (BASER type 0x1)
 * 4. Allocate collection table (BASER type 0x4)
 * 5. Enable the ITS
 */
BOOLEAN
HalpGicItsProbeNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode)
{
    ULONGLONG Typer;
    ULONGLONG Ctlr;
    ULONG DeviceLimit;
    LONG State;

    if (!ItsNode || ItsNode->VirtualBase == 0)
        return FALSE;

    /* Check initialization state */
    State = InterlockedCompareExchange(&ItsNode->InitState, 1, 0);
    if (State == 2)
        return TRUE;
    if (State == -1)
        return FALSE;
    if (State == 1)
    {
        /* Another thread is initializing, wait */
        for (ULONG Spins = 100000; Spins != 0; --Spins)
        {
            State = ItsNode->InitState;
            if (State == 2)
                return TRUE;
            if (State == -1)
                return FALSE;
            KeStallExecutionProcessor(1);
        }
        return FALSE;
    }

    if (ItsNode->VirtualBase == (ULONG_PTR)ItsNode->PhysicalBase.QuadPart)
    {
        PVOID ItsDeviceVa = MmMapIoSpace(ItsNode->PhysicalBase, 0x20000, MmNonCached);
        if (ItsDeviceVa != NULL)
            ItsNode->VirtualBase = (ULONG_PTR)ItsDeviceVa;
    }

    DPRINT1("[arm64][ITS] Probing ITS %lu at PA 0x%llx VA 0x%llx\n",
            ItsNode->ItsId, ItsNode->PhysicalBase.QuadPart, (ULONGLONG)ItsNode->VirtualBase);

    /* Parse GITS_TYPER */
    Typer = HalpMmioRead64(ItsNode->VirtualBase, GITS_TYPER);
    ItsNode->Typer = Typer;
    ItsNode->DeviceIdBits = ((ULONG)(Typer >> GITS_TYPER_DEVBITS_SHIFT) & GITS_TYPER_DEVBITS_MASK) + 1;
    ItsNode->EventIdBits = ((ULONG)(Typer >> GITS_TYPER_IDBITS_SHIFT) & GITS_TYPER_IDBITS_MASK) + 1;
    ItsNode->IttEntrySize = ((ULONG)(Typer >> GITS_TYPER_ITT_ENTRY_SIZE_SHIFT) & GITS_TYPER_ITT_ENTRY_SIZE_MASK) + 1;
    ItsNode->HasVlpis = (Typer & (1ULL << 1)) != 0;
    ItsNode->HasVmovp = (Typer & (1ULL << 26)) != 0;
    ItsNode->RdbasePhysical = (Typer & GITS_TYPER_PTA) != 0;

    if (ItsNode->DeviceIdBits >= 31)
        DeviceLimit = 0x7FFFFFFF;
    else
        DeviceLimit = (1U << ItsNode->DeviceIdBits);

    ItsNode->MaxDeviceId = DeviceLimit;

    DPRINT1("[arm64][ITS] ITS %lu: DevBits=%lu EvtBits=%lu ITT_entry=%lu VLPIS=%u VMOVP=%u PTA=%u\n",
            ItsNode->ItsId, ItsNode->DeviceIdBits, ItsNode->EventIdBits,
            ItsNode->IttEntrySize, ItsNode->HasVlpis ? 1 : 0, ItsNode->HasVmovp ? 1 : 0,
            ItsNode->RdbasePhysical ? 1 : 0);

    /* Allocate command queue */
    if (!HalpGicItsAllocCommandQueue(ItsNode))
        goto Fail;

    /* Allocate device table */
    if (!HalpGicItsSetupBaserTableOnNode(ItsNode, GITS_BASER_TYPE_DEVICE,
                                          DeviceLimit,
                                          &ItsNode->DeviceTableRaw,
                                          &ItsNode->DeviceTableBase,
                                          &ItsNode->DeviceTablePa,
                                          &ItsNode->DeviceTableSize,
                                          &ItsNode->DeviceTableEntries))
    {
        DPRINT1("[arm64][ITS] Failed to set up device table for ITS %lu\n", ItsNode->ItsId);
        goto Fail;
    }

    /* Initialize device hash table */
    if (!HalpGicItsInitDeviceMapOnNode(ItsNode, ItsNode->DeviceTableEntries))
    {
        DPRINT1("[arm64][ITS] Failed to init device map for ITS %lu\n", ItsNode->ItsId);
        goto Fail;
    }

    /* Allocate collection table */
    if (!HalpGicItsSetupBaserTableOnNode(ItsNode, GITS_BASER_TYPE_COLLECTION,
                                          MAXIMUM_PROCESSORS,
                                          &ItsNode->CollectionTableRaw,
                                          &ItsNode->CollectionTableBase,
                                          &ItsNode->CollectionTablePa,
                                          &ItsNode->CollectionTableSize,
                                          &ItsNode->CollectionTableEntries))
    {
        DPRINT("[arm64][ITS] No collection table for ITS %lu (may use GICR address)\n",
               ItsNode->ItsId);
        /* Collection table is optional - some implementations use GICR addresses directly */
    }

    /* Enable the ITS */
    Ctlr = *(volatile ULONG *)(ItsNode->VirtualBase + GITS_CTLR);
    Ctlr |= 1ULL;  /* Enable */
    if (ItsNode->HasVlpis)
        Ctlr |= (1ULL << 1);  /* ImDe - Immediate delivery for VLPIs */
    *(volatile ULONG *)(ItsNode->VirtualBase + GITS_CTLR) = (ULONG)Ctlr;

    ItsNode->Enabled = TRUE;
    InterlockedExchange(&ItsNode->InitState, 2);

    DPRINT1("[arm64][ITS] ITS %lu initialized successfully\n", ItsNode->ItsId);
    return TRUE;

Fail:
    InterlockedExchange(&ItsNode->InitState, -1);
    return FALSE;
}

/*
 * HalpGicItsInitAllNodes - Initialize all ITS nodes
 */
BOOLEAN
HalpGicItsInitAllNodes(VOID)
{
    ULONG Index;
    ULONG InitCount = 0;
    ULONG Cpu;
    ULONG ReadyCount = 0;
    KAFFINITY ActiveProcessors;

    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        DPRINT1("[arm64][ITS] Cannot initialize at IRQL %u\n", KeGetCurrentIrql());
        return FALSE;
    }

    if (HalpGicItsNodeCount == 0)
    {
        DPRINT1("[arm64][ITS] No ITS nodes to initialize\n");
        return FALSE;
    }

    /* Initialize LPI allocator first */
    if (!HalpGicItsInitLpiAllocator())
    {
        DPRINT1("[arm64][ITS] Failed to initialize LPI allocator\n");
        return FALSE;
    }

    /* Initialize LPI tables (shared across all ITS nodes) */
    if (!HalpGicItsInitLpiTables())
    {
        DPRINT1("[arm64][ITS] Failed to initialize LPI tables\n");
        return FALSE;
    }

    /* Probe each ITS node */
    for (Index = 0; Index < HalpGicItsNodeCount; ++Index)
    {
        if (HalpGicItsProbeNode(&HalpGicItsNodes[Index]))
        {
            InitCount++;
            HalpGicItsListMap |= (1u << Index);
        }
    }

    if (InitCount == 0)
    {
        DPRINT1("[arm64][ITS] Failed to initialize any ITS nodes\n");
        return FALSE;
    }

    ActiveProcessors = KeQueryActiveProcessors();
    if (ActiveProcessors == 0)
    {
        DPRINT1("[arm64][ITS] No active processors available for LPI initialization\n");
        return FALSE;
    }

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        if ((Cpu >= (sizeof(KAFFINITY) * 8)) || !(ActiveProcessors & ((KAFFINITY)1 << Cpu)))
            continue;

        if (!HalpGicCpuMpidrValid[Cpu] || (HalpGicrBase(Cpu) == 0))
        {
            DPRINT1("[arm64][ITS] Active CPU %lu has no redistributor\n", Cpu);
            return FALSE;
        }

        if (!HalpGicItsPrepareCpuTables(Cpu))
            return FALSE;
    }

    HalpArm64CleanDcacheRange(HalpGicLpiConfig, HalpGicLpiConfigSize);
    KeMemoryBarrier();
    KeIpiGenericCall(HalpGicItsProgramCpuTablesIpi, 0);

    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; Cpu++)
    {
        if ((Cpu >= (sizeof(KAFFINITY) * 8)) || !(ActiveProcessors & ((KAFFINITY)1 << Cpu)))
            continue;

        if (HalpGicLpiCpuTableState[Cpu] != HALP_GIC_LPI_CPU_ENABLED)
        {
            DPRINT1("[arm64][ITS] CPU %lu failed local LPI table initialization\n", Cpu);
            return FALSE;
        }

        if (!HalpGicItsEnsureCollection(Cpu))
        {
            DPRINT1("[arm64][ITS] Failed to map ITS collection for CPU %lu\n", Cpu);
            return FALSE;
        }

        ReadyCount++;
    }

    HalpGicItsInitialized = TRUE;
    HalpGicItsEnabled = TRUE;

    DPRINT1("[arm64][ITS] Initialized %lu ITS nodes and %lu local LPI redistributors\n", InitCount, ReadyCount);
    return TRUE;
}

/*
 * HalpGicItsInitialize - Legacy single-ITS initialization
 */
BOOLEAN
HalpGicItsInitialize(VOID)
{
    LONG State;

    DPRINT1("[arm64][ITS] HalpGicItsInitialize: entry, InitFailed=%d Initialized=%d Present=%d NodeCount=%lu\n",
            (int)HalpGicItsInitFailed, (int)HalpGicItsInitialized,
            (int)HalpGicItsPresent, HalpGicItsNodeCount);

    State = InterlockedCompareExchange(&HalpGicItsInitState, 0, 0);
    if (State == 2)
        return TRUE;

    if ((State < 0) || HalpGicItsInitFailed)
    {
        DPRINT1("[arm64][ITS] HalpGicItsInitialize: returning FALSE (InitFailed)\n");
        return FALSE;
    }

    if (!HalpGicItsPresent || HalpGicItsNodeCount == 0)
    {
        DPRINT1("[arm64][ITS] No ITS present (Present=%d NodeCount=%lu)\n",
                (int)HalpGicItsPresent, HalpGicItsNodeCount);
        return FALSE;
    }

    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        DPRINT1("[arm64][ITS] Deferring initialization requested at IRQL %u\n", KeGetCurrentIrql());
        return FALSE;
    }

    State = InterlockedCompareExchange(&HalpGicItsInitState, 1, 0);
    if (State == 2)
        return TRUE;

    if (State == 1)
    {
        while ((State = InterlockedCompareExchange(&HalpGicItsInitState, 0, 0)) == 1)
            KeStallExecutionProcessor(1);

        return (State == 2);
    }

    if (State != 0)
        return FALSE;

    if (!HalpGicItsInitAllNodes())
    {
        HalpGicItsInitFailed = TRUE;
        InterlockedExchange(&HalpGicItsInitState, -1);
        return FALSE;
    }

    InterlockedExchange(&HalpGicItsInitState, 2);
    return TRUE;
}

/*
 * ============================================================================
 * MSI Allocation API
 * ============================================================================
 */

/*
 * HalpGicItsAllocateMsi - Allocate an MSI for a device
 *
 * This is the main API for PCI MSI allocation. It:
 * 1. Selects an ITS node for the device
 * 2. Creates/gets the device structure
 * 3. Allocates an LPI
 * 4. Sends MAPTI command to map EventID to LPI
 * 5. Returns MSI address (GITS_TRANSLATER) and data (EventID)
 */
NTSTATUS
HalpGicItsAllocateMsi(
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG TargetCpu,
    _In_ ULONG RequestedLpi,
    _Out_ PULONG Lpi,
    _Out_ PPHYSICAL_ADDRESS MsiAddress,
    _Out_ PULONG MsiData)
{
    PHALP_GIC_ITS_NODE ItsNode;
    PHALP_ARM64_ITS_DEVICE Device;
    ULONG AllocatedLpi;
    NTSTATUS Status;

    if (!HalpGicItsInitialized)
    {
        if (!HalpGicItsInitialize())
            return STATUS_DEVICE_NOT_READY;
    }

    Status = HalpGicItsValidateTargetCpu(TargetCpu);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Select ITS node for this device */
    ItsNode = HalpGicItsSelectNodeForDevice(DeviceId);
    if (!ItsNode || !ItsNode->Enabled)
        return STATUS_DEVICE_NOT_READY;

    /* Get or create device */
    Device = HalpGicItsCreateDevice(ItsNode, DeviceId, EventId + 1);
    if (!Device)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Check if EventID is valid */
    if (EventId >= Device->MaxEvents)
        return STATUS_INVALID_PARAMETER;

    /* Check if already allocated */
    if (Device->EventToLpi && Device->EventToLpi[EventId] != 0)
    {
        if (RequestedLpi != 0 && Device->EventToLpi[EventId] != RequestedLpi)
            return STATUS_CONFLICTING_ADDRESSES;

        /* Already allocated, return existing mapping */
        *Lpi = Device->EventToLpi[EventId];
        MsiAddress->QuadPart = ItsNode->PhysicalBase.QuadPart + GITS_TRANSLATER;
        *MsiData = EventId;
        return STATUS_SUCCESS;
    }

    /* Ensure collection for target CPU */
    if (!HalpGicItsEnsureCollectionOnNode(ItsNode, TargetCpu))
        return STATUS_UNSUCCESSFUL;

    /* A resource-assigned vector must reach the ISR connected to that INTID. */
    if (RequestedLpi != 0)
    {
        if (!HalpGicItsReserveLpi(RequestedLpi))
            return STATUS_CONFLICTING_ADDRESSES;
        AllocatedLpi = RequestedLpi;
    }
    else
    {
        AllocatedLpi = HalpGicItsAllocateLpi(1);
    }
    if (AllocatedLpi == 0)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Enable LPI in config table */
    HalpGicItsEnableLpi(AllocatedLpi);

    /* Send MAPTI command */
    if (!HalpGicItsSendMaptiOnNode(ItsNode, DeviceId, EventId, AllocatedLpi, TargetCpu))
    {
        HalpGicItsFreeLpi(AllocatedLpi, 1);
        return STATUS_UNSUCCESSFUL;
    }

    /* Send SYNC */
    HalpGicItsSendSyncOnNode(ItsNode, TargetCpu, ItsNode->CollectionTarget[TargetCpu]);

    /* Record mapping */
    if (Device->EventToLpi)
        Device->EventToLpi[EventId] = AllocatedLpi;
    if (Device->EventToCollection)
        Device->EventToCollection[EventId] = TargetCpu;
    Device->AllocatedEvents++;

    if ((AllocatedLpi >= HAL_ARM64_LPI_BASE) && ((AllocatedLpi - HAL_ARM64_LPI_BASE) < HAL_ARM64_LPI_COUNT))
    {
        HalpGicLpiTarget[AllocatedLpi - HAL_ARM64_LPI_BASE].DeviceId = DeviceId;
        HalpGicLpiTarget[AllocatedLpi - HAL_ARM64_LPI_BASE].EventId = EventId;
        HalpGicLpiTarget[AllocatedLpi - HAL_ARM64_LPI_BASE].Valid = 1;
    }

    /* Return MSI info */
    *Lpi = AllocatedLpi;
    MsiAddress->QuadPart = ItsNode->PhysicalBase.QuadPart + GITS_TRANSLATER;
    *MsiData = EventId;

    DPRINT("[arm64][ITS] Allocated MSI: Dev=%u Event=%u -> LPI=%u, MSI@0x%llx data=0x%x\n",
           DeviceId, EventId, AllocatedLpi, MsiAddress->QuadPart, *MsiData);

    return STATUS_SUCCESS;
}

// Re-pend a dropped LPI via ITS INT (Linux its_send_int). Caller must be at <= DISPATCH_LEVEL: PostCommandOnNode takes CmdLock via KeAcquireSpinLock.
VOID
HalpGicItsRependLpi(
    _In_ ULONG Lpi)
{
    ULONG Index;
    ULONG DeviceId;
    ULONG EventId;
    PHALP_GIC_ITS_NODE ItsNode;

    if (!HalpGicItsEnabled || Lpi < HAL_ARM64_LPI_BASE)
        return;
    Index = Lpi - HAL_ARM64_LPI_BASE;
    if (Index >= HAL_ARM64_LPI_COUNT || !HalpGicLpiTarget[Index].Valid)
        return;
    DeviceId = HalpGicLpiTarget[Index].DeviceId;
    EventId = HalpGicLpiTarget[Index].EventId;
    ItsNode = HalpGicItsSelectNodeForDevice(DeviceId);
    if (!ItsNode || !ItsNode->Enabled)
        return;
    if (!HalpGicItsSendIntOnNode(ItsNode, DeviceId, EventId))
        DPRINT1("[arm64][ITS] LPI %lu re-pend INT failed (dev %lu evt %lu)\n", Lpi, DeviceId, EventId);
}

/*
 * ============================================================================
 * GICv4 Virtual LPI (VLPI) and vPE Support
 * ============================================================================
 *
 * Following Linux's GICv4 implementation in drivers/irqchip/irq-gic-v3-its.c
 *
 * GICv4 adds Virtual LPIs (VLPIs) for direct virtual interrupt injection:
 * - vPE (virtual Processing Element) represents a virtual CPU
 * - VLPIs are interrupts targeted at a vPE rather than a physical CPU
 * - Direct injection bypasses the hypervisor for interrupt delivery
 *
 * Key structures:
 * - Virtual Pending Table (VPT): Per-vPE pending state
 * - Virtual Property Table (VPROP): Per-VM LPI configuration
 * - vPE Table: ITS BASER type 0x2, maps vPE IDs to VPT addresses
 *
 * Commands:
 * - VMAPP: Map vPE to ITS
 * - VMAPTI: Map virtual interrupt to LPI
 * - VMOVI: Move virtual interrupt between vPEs
 * - VMOVP: Move vPE to different redistributor
 * - VINVALL: Invalidate all VLPIs for a vPE
 * - VSYNC: Synchronize vPE state
 */

/*
 * ============================================================================
 * GICv4 ITS Command Building Functions
 * ============================================================================
 */

/*
 * ============================================================================
 * GICv4 ITS Command Send Functions
 * ============================================================================
 */

/*
 * ============================================================================
 * vPE Support Initialization
 * ============================================================================
 */

/*
 * ============================================================================
 * vPE Allocation and Management
 * ============================================================================
 */

/*
 * ============================================================================
 * VLPI Mapping Functions
 * ============================================================================
 */
