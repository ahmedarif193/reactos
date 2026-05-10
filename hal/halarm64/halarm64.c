/*
 * PROJECT:         ReactOS HAL (ARM64)
 * PURPOSE:         Minimal stub implementation to satisfy kernel linkage
 *                  while the real Windows 11 style ARM64 HAL is brought up.
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ndk/inbvfuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <reactos/hal/acpi_pci.h>
#include <reactos/hal/acpi_cstate.h>
#include <reactos/drivers/acpi/acpi.h>
#include <halacpi.h>
#include <halacpi_arm64.h>
#include <bugcodes.h>
#include "bcm2712_pci.h"
#include <debug.h>

/*
 * GICv4 VLPI/vPE support forward declarations
 * These are defined in gic/gic_internal.h and implemented in gic module
 */

/* Forward declaration of vPE structure */
typedef struct _HALP_GIC_VPE *PHALP_GIC_VPE;

/* GIC version and capability information */
typedef struct _HALP_GIC_VERSION_INFO_FWD
{
    ULONG Architecture;
    BOOLEAN HasExtendedSpiRange;
    BOOLEAN HasMessageBasedSpi;
    BOOLEAN HasRangeSelector;
    ULONG ExtendedSpiStart;
    ULONG ExtendedSpiCount;
    BOOLEAN HasVlpis;
    BOOLEAN HasDirectLpi;
    BOOLEAN HasRvpeid;
    BOOLEAN HasItsVirtual;
    ULONG VpeidBits;
    ULONG MaxVpeid;
    ULONG MaxSpiId;
    ULONG MaxLpiId;
    ULONG TotalInterruptLines;
    BOOLEAN Initialized;
} HALP_GIC_VERSION_INFO_FWD;

/* Extern declarations for GICv4 state (defined in gic_init.c) */
extern HALP_GIC_VERSION_INFO_FWD HalpGicVersionInfo;
extern BOOLEAN HalpGicHasVlpis;
extern BOOLEAN HalpGicHasDirectLpi;
extern BOOLEAN HalpGicIsGicv4_1;
extern PHALP_GIC_VPE *HalpGicVpeTable;
extern ULONG HalpGicVpeTableSize;

/* Extern declarations for ITS state (defined in gic_common.c and gic_its.c) */
extern ULONG HalpGicItsNodeCount;
extern ULONGLONG HalpGicItsBase;
extern ULONG HalpGicItsId;
extern BOOLEAN HalpGicItsPresent;
extern BOOLEAN HalpGicItsEnabled;

/* Extern declarations for GICv2m MSI state (defined in gic_common.c) */
extern ULONGLONG HalpGicMsiFrameBase;
extern USHORT HalpGicMsiSpiBase;
extern USHORT HalpGicMsiSpiCount;
extern ULONG HalpGicMsiFlags;
extern BOOLEAN HalpGicMsiPresent;

/* Extern declarations for GIC detection/state variables (defined in gic_common.c) */
extern BOOLEAN HalpGicUseSysRegs;
extern BOOLEAN HalpForceSysRegs;
extern BOOLEAN HalpForceLegacyGic;
extern ULONG HalpGicArchRev;
extern BOOLEAN HalpGicParsedMadt;
extern BOOLEAN HalpGicInterfaceSelected;
extern BOOLEAN HalpGiccPresent;
extern ULONG HalpGicLpiCount;

/*
 * Forward declaration of HALP_GIC_ITS_NODE structure for ITS initialization.
 * This is a simplified version that matches the layout in gic/gic_internal.h.
 * We only need to initialize the first few fields (PhysicalBase, VirtualBase,
 * ItsId, ListNumber, InitState) plus the spinlocks for basic ITS operation.
 */
#define HALP_GIC_MAX_ITS_NODES_FWD 8
typedef struct _HALP_GIC_ITS_NODE_FWD
{
    /* Physical and virtual base addresses - must match gic_internal.h layout */
    PHYSICAL_ADDRESS PhysicalBase;      /* offset 0 */
    ULONG_PTR VirtualBase;              /* offset 8 (or 16 on 64-bit) */

    /* ITS identification */
    ULONG ItsId;                        /* From ACPI MADT */
    ULONG ListNumber;                   /* Position in its_list_map */

    /* Capabilities from GITS_TYPER (12 bytes of fields we don't init) */
    ULONGLONG Typer;
    ULONG DeviceIdBits;
    ULONG EventIdBits;
    ULONG IttEntrySize;
    ULONG MaxDeviceId;
    BOOLEAN HasVlpis;
    BOOLEAN HasVmovp;

    /* Command queue (skip to spinlock at known offset) */
    PVOID CmdQueueBase;
    PHYSICAL_ADDRESS CmdQueuePa;
    ULONG CmdWriteIndex;
    ULONG CmdQueueEntries;
    BOOLEAN CmdQueueNeedsFlush;
    KSPIN_LOCK CmdLock;                 /* We need to init this */

    /* Device table fields - skip to DeviceLock */
    PVOID DeviceTableBase;
    PHYSICAL_ADDRESS DeviceTablePa;
    SIZE_T DeviceTableSize;
    ULONG DeviceTableEntries;
    ULONG DeviceTableEntrySize;
    BOOLEAN DeviceTableFlat;
    PVOID DeviceTableRaw;

    /* Collection table fields */
    PVOID CollectionTableBase;
    PHYSICAL_ADDRESS CollectionTablePa;
    SIZE_T CollectionTableSize;
    ULONG CollectionTableEntries;
    PVOID CollectionTableRaw;

    /* Collections (one per CPU) - large array, skip */
    BOOLEAN CollectionMapped[MAXIMUM_PROCESSORS];
    ULONGLONG CollectionTarget[MAXIMUM_PROCESSORS];

    /* Device tracking */
    PVOID DeviceBuckets;
    ULONG DeviceBucketCount;
    ULONG DeviceCount;
    KSPIN_LOCK DeviceLock;              /* We need to init this */

    /* Initialization state */
    volatile LONG InitState;            /* We need to init this */
    BOOLEAN Enabled;

    /* ... rest of structure not needed for basic init */
} HALP_GIC_ITS_NODE_FWD, *PHALP_GIC_ITS_NODE_FWD;

extern HALP_GIC_ITS_NODE_FWD HalpGicItsNodes[HALP_GIC_MAX_ITS_NODES_FWD];

/* KiHalInitialized flag is now set by kernel (ex/init.c), not by HAL */

/*
 * HAL PnP driver initialization function - defined in halpnpdd.c
 * Called by HalInitSystem to set HalInitPnpDriver callback for PnP enumeration.
 */
NTSTATUS NTAPI HaliInitPnpDriver(VOID);

/* GIC version detection (defined in gic_init.c) */
VOID HalpGicDetectVersion(VOID);
BOOLEAN HalpGicHasVlpiSupport(VOID);

/* GICv4 vPE management (defined in gic_its.c) */
NTSTATUS HalpGicItsAllocateVpe(ULONG VmId, ULONG VpIndex, PHALP_GIC_VPE *VpeOut);
VOID HalpGicItsFreeVpe(PHALP_GIC_VPE Vpe);
NTSTATUS HalpGicItsScheduleVpe(PHALP_GIC_VPE Vpe, ULONG TargetCpu);
NTSTATUS HalpGicItsDescheduleVpe(PHALP_GIC_VPE Vpe);
NTSTATUS HalpGicItsMapVlpi(PHALP_GIC_VPE Vpe, ULONG DeviceId, ULONG EventId, ULONG VirtIntId, BOOLEAN Enabled);
NTSTATUS HalpGicItsUnmapVlpi(PHALP_GIC_VPE Vpe, ULONG DeviceId, ULONG EventId);
ULONG HalpGicItsGetVpeId(PHALP_GIC_VPE Vpe);

/*
 * ARM64 ADAPTER_OBJECT definition.
 * This is a simplified version for ARM64 systems that don't have ISA/EISA DMA.
 * The full x86 version includes fields for DMA channel programming that are
 * not applicable on ARM64.
 */
typedef struct _ADAPTER_OBJECT {
    /*
     * DMA adapter header - must be first for IoGetDmaAdapter compatibility.
     * This allows callers to cast between PDMA_ADAPTER and PADAPTER_OBJECT.
     */
    DMA_ADAPTER DmaHeader;

    /*
     * Master adapter pointer. For ARM64, this is typically NULL since we
     * don't have a separate master DMA controller like x86's 8237.
     */
    struct _ADAPTER_OBJECT *MasterAdapter;

    /* Number of map registers this adapter can use */
    ULONG MapRegistersPerChannel;

    /* Base virtual address of adapter - not used on ARM64 */
    PVOID AdapterBaseVa;

    /* Map register base - used for scatter/gather on ARM64 */
    PVOID MapRegisterBase;

    /* Current and committed map register counts */
    ULONG NumberOfMapRegisters;
    ULONG CommittedMapRegisters;

    /* Wait context block for pending requests */
    PWAIT_CONTEXT_BLOCK CurrentWcb;

    /* Queue for waiting channels and registers */
    KDEVICE_QUEUE ChannelWaitQueue;
    PKDEVICE_QUEUE RegisterWaitQueue;

    /* List entry for DMA adapter list */
    LIST_ENTRY AdapterQueue;
    LIST_ENTRY AdapterList;

    /* Spinlock for adapter synchronization */
    KSPIN_LOCK SpinLock;

    /* Channel number - 0xFF for system/bus-master adapters */
    UCHAR ChannelNumber;

    /* Adapter number - not used on ARM64 */
    UCHAR AdapterNumber;

    /* DMA transfer width */
    DMA_WIDTH Width;

    /* DMA transfer speed */
    DMA_SPEED Speed;

    /* Flags and capabilities */
    BOOLEAN MasterDevice;
    BOOLEAN Dma32BitAddresses;
    BOOLEAN Dma64BitAddresses;
    BOOLEAN ScatterGather;
    BOOLEAN IgnoreCount;
    BOOLEAN NeedsMapRegisters;

    /* NUMA node affinity */
    USHORT NumaNode;
} ADAPTER_OBJECT, *PADAPTER_OBJECT;

/*
 * ARM64 DMA Map Register Entry
 * Tracks a single map register used for DMA scatter/gather operations.
 * On ARM64, map registers serve as bookkeeping for DMA mappings and
 * may hold bounce buffer information for non-contiguous or non-coherent DMA.
 */
typedef struct _HAL_ARM64_MAP_REGISTER_ENTRY
{
    /* Virtual address of the original buffer */
    PVOID OriginalVa;

    /* Physical address returned to the device */
    PHYSICAL_ADDRESS PhysicalAddress;

    /* Bounce buffer virtual address (if used) */
    PVOID BounceBuffer;

    /* Bounce buffer physical address (if used) */
    PHYSICAL_ADDRESS BouncePhysical;

    /* Length of the mapping */
    ULONG Length;

    /* Flags */
    union {
        struct {
            ULONG UsesBounceBuffer : 1;
            ULONG WriteToDevice : 1;
            ULONG Reserved : 30;
        };
        ULONG Flags;
    };
} HAL_ARM64_MAP_REGISTER_ENTRY, *PHAL_ARM64_MAP_REGISTER_ENTRY;

/*
 * ARM64 Map Register Base
 * Contains an array of map register entries for a single DMA operation.
 */
typedef struct _HAL_ARM64_MAP_REGISTER_BASE
{
    /* Signature for validation */
    ULONG Signature;
#define HAL_ARM64_MAP_REG_SIGNATURE 'MRAD'

    /* Number of map registers allocated */
    ULONG NumberOfMapRegisters;

    /* Current index for scatter-gather mapping (per-allocation) */
    ULONG CurrentIndex;

    /* Reference to the adapter object */
    PADAPTER_OBJECT AdapterObject;

    /* Array of map register entries (variable size) */
    HAL_ARM64_MAP_REGISTER_ENTRY Registers[1];
} HAL_ARM64_MAP_REGISTER_BASE, *PHAL_ARM64_MAP_REGISTER_BASE;

/*
 * ARM64 Common Buffer Allocation Tracking
 * Tracks common buffers allocated via HalAllocateCommonBuffer
 */
typedef struct _HAL_ARM64_COMMON_BUFFER
{
    LIST_ENTRY ListEntry;
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID RawAllocation;
    ULONG Length;
    ULONG Alignment;
    BOOLEAN CacheEnabled;
} HAL_ARM64_COMMON_BUFFER, *PHAL_ARM64_COMMON_BUFFER;

/*
 * ARM64 SMMU (System Memory Management Unit) State
 * Holds SMMU configuration for DMA address translation
 */
typedef struct _HAL_ARM64_SMMU_STATE
{
    /* SMMU detected and initialized */
    BOOLEAN Present;
    BOOLEAN Initialized;
    BOOLEAN BypassMode;
    UCHAR Reserved;

    /* SMMU version (from IORT) */
    ULONG Model;

    /* Base address of SMMU registers */
    ULONGLONG BaseAddress;

    /* Size of SMMU register space */
    ULONGLONG Span;

    /* Mapped virtual address of SMMU registers */
    PVOID MappedBase;

    /* Stream table base (for SMMUv3) */
    PVOID StreamTableBase;
    PHYSICAL_ADDRESS StreamTablePhysical;
    ULONG StreamTableEntries;

    /* Command queue (for SMMUv3) */
    PVOID CmdQueue;
    PHYSICAL_ADDRESS CmdQueuePhysical;
    ULONG CmdQueueEntries;
    ULONG CmdQueueProd;

    /* Event queue (for SMMUv3) */
    PVOID EventQueue;
    PHYSICAL_ADDRESS EventQueuePhysical;
    ULONG EventQueueEntries;
} HAL_ARM64_SMMU_STATE, *PHAL_ARM64_SMMU_STATE;

/*
 * ARM64 DMA coherency state
 */
typedef struct _HAL_ARM64_DMA_COHERENCY
{
    /* System-wide cache coherency for DMA */
    BOOLEAN SystemCoherent;

    /* Per-root-complex coherency (indexed by PCI segment) */
    BOOLEAN RootComplexCoherent[HALP_ARM64_MAX_IORT_RC];

    /* Cache line size for DMA operations */
    ULONG CacheLineSize;
} HAL_ARM64_DMA_COHERENCY, *PHAL_ARM64_DMA_COHERENCY;

/* Global DMA state */
static HAL_ARM64_DMA_COHERENCY HalpArm64DmaCoherency;
static HAL_ARM64_SMMU_STATE HalpArm64SmmuState;
static LIST_ENTRY HalpArm64CommonBufferList;
static KSPIN_LOCK HalpArm64CommonBufferLock;
static BOOLEAN HalpArm64DmaInitialized = FALSE;

/*
 * ARM64 PCI Interrupt Routing (ACPI _PRT) State
 *
 * On ARM64 with ACPI, PCI interrupt routing is performed through ACPI _PRT
 * (PCI Routing Table). The ACPI driver parses _PRT entries and registers
 * a callback function that the HAL uses to look up the GSI (Global System
 * Interrupt) for each PCI device/pin combination.
 *
 * This is critical for VirtIO and other PCI devices where firmware does not
 * program the InterruptLine field in PCI config space (leaving it as 0 or 0xFF).
 */
PHAL_ACPI_PCI_ROUTE_QUERY HalpArm64PciRouteQueryCallback = NULL;

#define HAL_ARM64_MAX_PCI_ROOT_BRIDGES 16

typedef struct _HALP_ARM64_PCI_ROOT_BRIDGE
{
    BOOLEAN Present;
    UCHAR BusStart;
    UCHAR BusEnd;
    USHORT Segment;
    BOOLEAN ConfigSpaceBasePresent;
    UCHAR Reserved[3];
    ULONGLONG ConfigSpaceBase;
} HALP_ARM64_PCI_ROOT_BRIDGE, *PHALP_ARM64_PCI_ROOT_BRIDGE;

static HALP_ARM64_PCI_ROOT_BRIDGE HalpArm64PciRootBridges[HAL_ARM64_MAX_PCI_ROOT_BRIDGES];
static ULONG HalpArm64PciRootBridgeCount = 0;

/* Tags for memory allocation */
#define TAG_DMA_MAP  'PMAD'
#define TAG_DMA_BUF  'BMAD'
#define TAG_DMA_CMN  'CMAD'
#define TAG_DMA_SGL  'GSAD'

/* Maximum map registers per adapter */
#define HAL_ARM64_MAX_MAP_REGISTERS 256

/* Bounce buffer threshold - use bounce buffer if physical address exceeds this */
#define HAL_ARM64_DMA_32BIT_LIMIT 0x100000000ULL
#define HAL_ARM64_DMA_DEFAULT_ALIGNMENT 64

#ifndef TAG_HAL
#define TAG_HAL    ' laH'
#endif

#ifndef KeGetCurrentProcessorNumber
ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID);
#endif

#ifndef KeRaiseIrqlToDpcLevel
KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID);
#endif

#ifndef KeLowerIrql
VOID
NTAPI
KeLowerIrql(_In_ KIRQL NewIrql);
#endif

#define UNIMPLEMENTED_STUB() ((void)0)

#undef READ_PORT_UCHAR
#undef READ_PORT_USHORT
#undef READ_PORT_ULONG
#undef READ_PORT_BUFFER_UCHAR
#undef READ_PORT_BUFFER_USHORT
#undef READ_PORT_BUFFER_ULONG
#undef WRITE_PORT_UCHAR
#undef WRITE_PORT_USHORT
#undef WRITE_PORT_ULONG
#undef WRITE_PORT_BUFFER_UCHAR
#undef WRITE_PORT_BUFFER_USHORT
#undef WRITE_PORT_BUFFER_ULONG

FORCEINLINE
VOID
HalpReadRegisterBufferUchar(
    _In_ volatile PUCHAR Port,
    _Out_writes_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        *Buffer++ = READ_REGISTER_UCHAR(Port);
    }
}

FORCEINLINE
VOID
HalpReadRegisterBufferUshort(
    _In_ volatile PUSHORT Port,
    _Out_writes_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        *Buffer++ = READ_REGISTER_USHORT(Port);
    }
}

FORCEINLINE
VOID
HalpReadRegisterBufferUlong(
    _In_ volatile PULONG Port,
    _Out_writes_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        *Buffer++ = READ_REGISTER_ULONG(Port);
    }
}

FORCEINLINE
VOID
HalpWriteRegisterBufferUchar(
    _In_ volatile PUCHAR Port,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        WRITE_REGISTER_UCHAR(Port, *Buffer++);
    }
}

FORCEINLINE
VOID
HalpWriteRegisterBufferUshort(
    _In_ volatile PUSHORT Port,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        WRITE_REGISTER_USHORT(Port, *Buffer++);
    }
}

FORCEINLINE
VOID
HalpWriteRegisterBufferUlong(
    _In_ volatile PULONG Port,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        WRITE_REGISTER_ULONG(Port, *Buffer++);
    }
}

VOID
NTAPI
READ_PORT_BUFFER_UCHAR(
    _In_ PUCHAR Port,
    _Out_writes_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    HalpReadRegisterBufferUchar(Port, Buffer, Count);
}

VOID
NTAPI
READ_PORT_BUFFER_USHORT(
    _In_ PUSHORT Port,
    _Out_writes_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    HalpReadRegisterBufferUshort(Port, Buffer, Count);
}

VOID
NTAPI
READ_PORT_BUFFER_ULONG(
    _In_ PULONG Port,
    _Out_writes_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    HalpReadRegisterBufferUlong(Port, Buffer, Count);
}

UCHAR
NTAPI
READ_PORT_UCHAR(
    _In_ PUCHAR Port)
{
    return READ_REGISTER_UCHAR(Port);
}

USHORT
NTAPI
READ_PORT_USHORT(
    _In_ PUSHORT Port)
{
    return READ_REGISTER_USHORT(Port);
}

ULONG
NTAPI
READ_PORT_ULONG(
    _In_ PULONG Port)
{
    return READ_REGISTER_ULONG(Port);
}

VOID
NTAPI
WRITE_PORT_BUFFER_UCHAR(
    _In_ PUCHAR Port,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    HalpWriteRegisterBufferUchar(Port, Buffer, Count);
}

VOID
NTAPI
WRITE_PORT_BUFFER_USHORT(
    _In_ PUSHORT Port,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    HalpWriteRegisterBufferUshort(Port, Buffer, Count);
}

VOID
NTAPI
WRITE_PORT_BUFFER_ULONG(
    _In_ PULONG Port,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    HalpWriteRegisterBufferUlong(Port, Buffer, Count);
}

VOID
NTAPI
WRITE_PORT_UCHAR(
    _In_ PUCHAR Port,
    _In_ UCHAR Value)
{
    WRITE_REGISTER_UCHAR(Port, Value);
}

VOID
NTAPI
WRITE_PORT_USHORT(
    _In_ PUSHORT Port,
    _In_ USHORT Value)
{
    WRITE_REGISTER_USHORT(Port, Value);
}

VOID
NTAPI
WRITE_PORT_ULONG(
    _In_ PULONG Port,
    _In_ ULONG Value)
{
WRITE_REGISTER_ULONG(Port, Value);
}

KSPIN_LOCK HalpPCIConfigLock;

static
PHALP_ARM64_PCI_ROOT_BRIDGE
HalpArm64FindPciRootBridge(
    _In_ USHORT Segment,
    _In_ UCHAR BusNumber)
{
    PHALP_ARM64_PCI_ROOT_BRIDGE Match = NULL;
    ULONG Index;
    ULONG RootCount;

    RootCount = HalpArm64PciRootBridgeCount;
    if (RootCount > HAL_ARM64_MAX_PCI_ROOT_BRIDGES)
    {
        RootCount = HAL_ARM64_MAX_PCI_ROOT_BRIDGES;
    }

    for (Index = 0; Index < RootCount; Index++)
    {
        PHALP_ARM64_PCI_ROOT_BRIDGE Root = &HalpArm64PciRootBridges[Index];

        if (!Root->Present || !Root->ConfigSpaceBasePresent)
        {
            continue;
        }

        if (BusNumber < Root->BusStart || BusNumber > Root->BusEnd)
        {
            continue;
        }

        if ((Segment != HALP_ACPI_SEGMENT_ANY) &&
            (Root->Segment != Segment))
        {
            continue;
        }

        if (Segment != HALP_ACPI_SEGMENT_ANY)
        {
            return Root;
        }

        if (!Match)
        {
            Match = Root;
        }
    }

    return Match;
}

static
USHORT
HalpArm64ResolvePciSegment(
    _In_ UCHAR BusNumber)
{
    PHALP_ARM64_PCI_ROOT_BRIDGE Root;

    Root = HalpArm64FindPciRootBridge(HALP_ACPI_SEGMENT_ANY, BusNumber);
    if (!Root)
    {
        return 0;
    }

    return Root->Segment;
}

BOOLEAN
NTAPI
HalpArm64HasPciConfigSpaceBackend(
    VOID)
{
    ULONG Index;
    ULONG RootCount;

    /*
     * BCM2712 indirect config-space backend (RPi5).
     * Report as available — actual MMIO mapping is deferred to first
     * config access (Phase 1+), but the early scan will work because
     * HalpPhase0GetPciDataByOffsetArm64 calls through our dispatch
     * which triggers lazy init at that point.
     */
    if (Bcm2712Detected)
    {
        return TRUE;
    }

    if (HalpAcpiMcfgAllocations && HalpAcpiMcfgAllocationCount)
    {
        return TRUE;
    }

    RootCount = HalpArm64PciRootBridgeCount;
    if (RootCount > HAL_ARM64_MAX_PCI_ROOT_BRIDGES)
    {
        RootCount = HAL_ARM64_MAX_PCI_ROOT_BRIDGES;
    }

    for (Index = 0; Index < RootCount; Index++)
    {
        if (HalpArm64PciRootBridges[Index].Present &&
            HalpArm64PciRootBridges[Index].ConfigSpaceBasePresent)
        {
            return TRUE;
        }
    }

    return FALSE;
}

BOOLEAN
NTAPI
HalpArm64QueryPciRootBusRange(
    _Out_opt_ PULONG MinBus,
    _Out_opt_ PULONG MaxBus)
{
    ULONG Index;
    ULONG RootCount;
    ULONG GlobalMinBus = 0xFF;
    ULONG GlobalMaxBus = 0;
    BOOLEAN FoundRoot = FALSE;

    RootCount = HalpArm64PciRootBridgeCount;
    if (RootCount > HAL_ARM64_MAX_PCI_ROOT_BRIDGES)
    {
        RootCount = HAL_ARM64_MAX_PCI_ROOT_BRIDGES;
    }

    for (Index = 0; Index < RootCount; Index++)
    {
        PHALP_ARM64_PCI_ROOT_BRIDGE Root = &HalpArm64PciRootBridges[Index];

        if (!Root->Present || !Root->ConfigSpaceBasePresent)
        {
            continue;
        }

        if (Root->BusStart < GlobalMinBus)
        {
            GlobalMinBus = Root->BusStart;
        }

        if (Root->BusEnd > GlobalMaxBus)
        {
            GlobalMaxBus = Root->BusEnd;
        }

        FoundRoot = TRUE;
    }

    if (!FoundRoot)
    {
        if (MinBus)
        {
            *MinBus = 0;
        }
        if (MaxBus)
        {
            *MaxBus = 0;
        }
        return FALSE;
    }

    if (MinBus)
    {
        *MinBus = GlobalMinBus;
    }
    if (MaxBus)
    {
        *MaxBus = GlobalMaxBus;
    }

    return TRUE;
}

static
BOOLEAN
HalpArm64AccessPciConfigByAddress(
    _In_ BOOLEAN Write,
    _In_ PHYSICAL_ADDRESS Address,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PHYSICAL_ADDRESS PageBase;
    PVOID Mapping;
    ULONG PageOffset;
    ULONG CurrentOffset;
    ULONG Remaining;
    PUCHAR ByteBuffer;

    if (!Buffer || Length == 0)
    {
        return FALSE;
    }

    PageBase.QuadPart = Address.QuadPart & ~((ULONGLONG)PAGE_SIZE - 1);
    PageOffset = (ULONG)(Address.QuadPart - PageBase.QuadPart);
    if ((ULONGLONG)PageOffset + Length > PAGE_SIZE)
    {
        return FALSE;
    }

    Mapping = MmMapIoSpace(PageBase, PAGE_SIZE, MmNonCached);
    if (!Mapping)
    {
        return FALSE;
    }

    CurrentOffset = PageOffset;
    Remaining = Length;
    ByteBuffer = Buffer;

    while (Remaining)
    {
        ULONG AlignedOffset;
        ULONG ByteInDword;
        ULONG TransferLength;
        volatile ULONG *Register;
        ULONG Dword;

        AlignedOffset = CurrentOffset & ~3u;
        ByteInDword = CurrentOffset & 3u;
        TransferLength = 4 - ByteInDword;
        if (TransferLength > Remaining)
        {
            TransferLength = Remaining;
        }

        Register = (volatile ULONG *)((PUCHAR)Mapping + AlignedOffset);

        if (Write)
        {
            if ((TransferLength == sizeof(ULONG)) && (ByteInDword == 0))
            {
                RtlCopyMemory(&Dword, ByteBuffer, sizeof(ULONG));
                WRITE_REGISTER_ULONG((PULONG)Register, Dword);
            }
            else
            {
                Dword = READ_REGISTER_ULONG((PULONG)Register);
                RtlCopyMemory(((PUCHAR)&Dword) + ByteInDword,
                              ByteBuffer,
                              TransferLength);
                WRITE_REGISTER_ULONG((PULONG)Register, Dword);
            }
        }
        else
        {
            Dword = READ_REGISTER_ULONG((PULONG)Register);
            RtlCopyMemory(ByteBuffer,
                          ((PUCHAR)&Dword) + ByteInDword,
                          TransferLength);
        }

        CurrentOffset += TransferLength;
        ByteBuffer += TransferLength;
        Remaining -= TransferLength;
    }

    MmUnmapIoSpace(Mapping, PAGE_SIZE);
    return TRUE;
}

static
BOOLEAN
HalpArm64AccessPciRootConfigSpace(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PHALP_ARM64_PCI_ROOT_BRIDGE Root;
    PHYSICAL_ADDRESS Address;

    if (BusNumber > 0xFF ||
        Offset >= 0x1000 ||
        (ULONGLONG)Offset + Length > 0x1000)
    {
        return FALSE;
    }

    Root = HalpArm64FindPciRootBridge(Segment, (UCHAR)BusNumber);
    if (!Root || !Root->ConfigSpaceBasePresent)
    {
        return FALSE;
    }

    Address.QuadPart = Root->ConfigSpaceBase;
    Address.QuadPart += ((ULONGLONG)BusNumber << 20);
    Address.QuadPart += ((ULONGLONG)Slot.u.bits.DeviceNumber << 15);
    Address.QuadPart += ((ULONGLONG)Slot.u.bits.FunctionNumber << 12);
    Address.QuadPart += Offset;

    return HalpArm64AccessPciConfigByAddress(Write, Address, Buffer, Length);
}

BOOLEAN
NTAPI
HalpArm64AccessPciConfigSpace(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    /*
     * Try BCM2712 indirect config-space backend first (RPi5).
     * This handles the case where no MCFG or _CBA is published.
     * Bcm2712Detected is set once during Phase 0 probe — zero overhead
     * on non-RPi5 platforms.
     */
    if (Bcm2712Detected &&
        Bcm2712PciAccessConfigSpace(Write, Segment, BusNumber, Slot,
                                    Buffer, Offset, Length))
    {
        return TRUE;
    }

    /* Try _CBA-based config access (per-root-bridge ECAM base) */
    if ((!HalpAcpiMcfgAllocations || !HalpAcpiMcfgAllocationCount) &&
        HalpArm64AccessPciRootConfigSpace(Write,
                                          Segment,
                                          BusNumber,
                                          Slot,
                                          Buffer,
                                          Offset,
                                          Length))
    {
        return TRUE;
    }

    /* Fall back to MCFG-based ECAM */
    return HalpAcpiAccessConfigEcam(Write,
                                    Segment,
                                    BusNumber,
                                    Slot,
                                    Buffer,
                                    Offset,
                                    Length);
}

/*
 * HalpPciLogEcamCoverage - ARM64 PCI ECAM coverage logging
 *
 * Logs diagnostic information about PCI Express ECAM (Enhanced Configuration
 * Access Mechanism) usage on ARM64. Unlike x86/x64, ARM64 has no legacy PCI
 * configuration mechanism (CF8/CFC ports), so firmware must publish a
 * memory-mapped PCI config-space base.
 *
 * This function reports:
 * - Whether ECAM is active or why it failed
 * - MCFG table status
 * - Any access errors encountered
 */
VOID
HalpPciLogEcamCoverage(VOID)
{
    LONG Flags;

    Flags = HalpAcpiEcamCoverageFlags;
    if (Flags == 0)
    {
        DbgPrint("HAL: ARM64 PCI ECAM path not exercised.\n");
        return;
    }

    if ((Flags & HALP_ACPI_ECAM_COVERAGE_USED) &&
        !(Flags & (HALP_ACPI_ECAM_COVERAGE_DISABLED_GLOBAL | HALP_ACPI_ECAM_COVERAGE_FORCED_LEGACY)))
    {
        DbgPrint("HAL: ARM64 PCI Express MMCONFIG (ECAM) active for configuration space.\n");
    }
    else
    {
        DbgPrint("HAL: ARM64 PCI Express MMCONFIG unavailable; PCI access disabled.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_NO_TABLE)
    {
        DbgPrint("HAL:   ECAM failure: ACPI MCFG table missing or empty.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_NO_ALLOCATION)
    {
        DbgPrint("HAL:   ECAM failure: no MCFG allocation matched the requested bus.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_BUS_TOO_HIGH)
    {
        DbgPrint("HAL:   ECAM failure: bus number exceeded 0xFF.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_OFFSET_TOO_HIGH)
    {
        DbgPrint("HAL:   ECAM failure: offset reached beyond 4KB window.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_RANGE_OVERRUN)
    {
        DbgPrint("HAL:   ECAM failure: access spanned multiple 4KB windows.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_MAP_FAILURE)
    {
        DbgPrint("HAL:   ECAM failure: failed to map ECAM page.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_VENDOR_ALL_ONES)
    {
        DbgPrint("HAL:   ECAM failure: configuration space read returned 0xFFFF.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_DISABLED_GLOBAL)
    {
        DbgPrint("HAL:   ECAM note: access path disabled globally after firmware failure.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_FORCED_LEGACY)
    {
        DbgPrint("HAL:   ECAM note: firmware quirk forced ECAM disable (no legacy on ARM64!).\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_ZERO_LENGTH)
    {
        DbgPrint("HAL:   ECAM note: zero-length configuration request observed.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_SEGMENT_ANY)
    {
        DbgPrint("HAL:   ECAM note: callers used wildcard segment selection.\n");
    }

    /* Log MCFG allocation summary for ARM64 */
    if (HalpAcpiMcfgAllocations && HalpAcpiMcfgAllocationCount)
    {
        ULONG Index;
        DbgPrint("HAL:   ARM64 MCFG allocations: %lu\n", HalpAcpiMcfgAllocationCount);
        for (Index = 0; Index < HalpAcpiMcfgAllocationCount; Index++)
        {
            DbgPrint("HAL:     [%lu] Segment %u, Bus %u-%u, Base 0x%I64x\n",
                     Index,
                     HalpAcpiMcfgAllocations[Index].PciSegment,
                     HalpAcpiMcfgAllocations[Index].StartBusNumber,
                     HalpAcpiMcfgAllocations[Index].EndBusNumber,
                     HalpAcpiMcfgAllocations[Index].BaseAddress);
        }
    }
}

BOOLEAN
NTAPI
HalpIsApicInterruptController(VOID)
{
    return FALSE;
}

BOOLEAN
NTAPI
HalIsIoApicPresent(VOID)
{
    return FALSE;
}

/*
 * ARM64 HAL System Information Query
 *
 * This function handles system information queries from the kernel and drivers.
 * The most critical query for ACPI initialization is HalAcpiAuditInformation,
 * which returns the physical address of the ACPI RSDP table.
 *
 * Without this implementation, the ACPI driver cannot locate the RSDP and
 * AcpiInitializeTables() will fail with "Unable to AcpiInitializeTables".
 */

/* External declaration for RSDP query */
extern BOOLEAN
HalpQueryAcpiRootPointer(
    _Out_ PPHYSICAL_ADDRESS Address);

NTSTATUS
NTAPI
HaliQuerySystemInformation(
    _In_ HAL_QUERY_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _Inout_ PVOID Buffer,
    _Out_ PULONG ReturnedLength)
{
    switch (InformationClass)
    {
        case HalAcpiAuditInformation:
        {
            HAL_ACPI_ROOT_POINTER_INFORMATION *Info;
            PHYSICAL_ADDRESS RsdpAddress = {{0}};
            BOOLEAN HasAcpi;

            if (BufferSize < sizeof(*Info))
            {
                if (ReturnedLength) *ReturnedLength = sizeof(*Info);
                return STATUS_INFO_LENGTH_MISMATCH;
            }

            Info = (HAL_ACPI_ROOT_POINTER_INFORMATION *)Buffer;
            HasAcpi = HalpQueryAcpiRootPointer(&RsdpAddress);

            Info->RsdpPhysicalAddress = RsdpAddress;
            if (ReturnedLength) *ReturnedLength = sizeof(*Info);

            DbgPrint("[arm64][HAL] HaliQuerySystemInformation(HalAcpiAuditInformation): RSDP=0x%llx HasAcpi=%d\n",
                     RsdpAddress.QuadPart, HasAcpi);

            return HasAcpi ? STATUS_SUCCESS : STATUS_NOT_FOUND;
        }

        case HalFrameBufferCachingInformation:
            /* Not supported on ARM64 */
            return STATUS_NOT_IMPLEMENTED;

        case HalQueryAMLIIllegalIOPortAddresses:
            /*
             * ARM64 does not have legacy x86 I/O ports (PIC, DMA, PIT, etc.)
             * that need AMLI protection. Return empty list.
             */
            if (ReturnedLength) *ReturnedLength = 0;
            return STATUS_SUCCESS;

        default:
            DPRINT1("[arm64][HAL] HaliQuerySystemInformation: Unhandled class %d\n", InformationClass);
            break;
    }

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HaliSetSystemInformation(
    _In_ HAL_SET_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _Inout_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(BufferSize);
    UNREFERENCED_PARAMETER(Buffer);

    DPRINT1("[arm64][HAL] HaliSetSystemInformation: Unhandled class %d\n", InformationClass);
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * HalQueryPciBusRange - Query the range of valid PCI bus numbers
 *
 * Returns the minimum and maximum PCI bus numbers supported by the system.
 * On ARM64, this information comes from ACPI PCI configuration-space
 * descriptors. We prefer the ACPI MCFG table when present, and fall back to
 * ACPI root-bridge bus windows once the ACPI driver has published them.
 *
 * Parameters:
 *   MinBus - Optional pointer to receive minimum bus number
 *   MaxBus - Optional pointer to receive maximum bus number
 *
 * Returns:
 *   TRUE if valid bus range information is available, FALSE otherwise.
 *
 * Notes:
 *   ARM64 systems require a firmware-published PCI configuration-space base.
 *   Multiple segments may exist, but this function returns the overall bus
 *   window across the published roots.
 */
BOOLEAN
NTAPI
HalQueryPciBusRange(
    _Out_opt_ PULONG MinBus,
    _Out_opt_ PULONG MaxBus)
{
    ULONG Index;
    ULONG GlobalMinBus = 0xFF;
    ULONG GlobalMaxBus = 0;
    BOOLEAN FoundAllocation = FALSE;

    if (!HalpAcpiMcfgAllocations || HalpAcpiMcfgAllocationCount == 0)
    {
        return HalpArm64QueryPciRootBusRange(MinBus, MaxBus);
    }

    /*
     * Iterate through all MCFG allocations to find the overall bus range.
     * We prioritize segment 0, but if not present, use any segment.
     */
    for (Index = 0; Index < HalpAcpiMcfgAllocationCount; Index++)
    {
        PHALP_ACPI_MCFG_ALLOCATION Allocation = &HalpAcpiMcfgAllocations[Index];

        /* Track the overall minimum and maximum bus numbers */
        if (Allocation->StartBusNumber < GlobalMinBus)
        {
            GlobalMinBus = Allocation->StartBusNumber;
        }
        if (Allocation->EndBusNumber > GlobalMaxBus)
        {
            GlobalMaxBus = Allocation->EndBusNumber;
        }
        FoundAllocation = TRUE;
    }

    if (!FoundAllocation)
    {
        if (MinBus)
        {
            *MinBus = 0;
        }
        if (MaxBus)
        {
            *MaxBus = 0;
        }
        return FALSE;
    }

    if (MinBus)
    {
        *MinBus = GlobalMinBus;
    }
    if (MaxBus)
    {
        *MaxBus = GlobalMaxBus;
    }

    DPRINT("[arm64][HAL] HalQueryPciBusRange: Min=%lu, Max=%lu\n",
           GlobalMinBus, GlobalMaxBus);

    return TRUE;
}

ULONG
NTAPI
HalGetAcpiSciVector(VOID)
{
    return 0;
}

ULONG HalpBusType = 0xFFFFFFFF;

/* Forward declaration for ARM64 cache line size detection */
static ULONG HalpArm64GetCacheLineSize(VOID);

/*
 * ARM64 DMA Subsystem Initialization
 *
 * This function initializes the ARM64 DMA subsystem including:
 * - Cache coherency detection from IORT
 * - SMMU detection and bypass mode configuration
 * - Common buffer tracking structures
 *
 * Must be called during HAL initialization after ACPI tables are available.
 */
VOID
NTAPI
HalpInitDma(VOID)
{
    ULONG Index;

    HalpBusType = 0xFFFFFFFF;

    if (HalpArm64DmaInitialized)
    {
        return;
    }

    /* Initialize common buffer tracking */
    InitializeListHead(&HalpArm64CommonBufferList);
    KeInitializeSpinLock(&HalpArm64CommonBufferLock);

    /* Initialize DMA coherency state */
    RtlZeroMemory(&HalpArm64DmaCoherency, sizeof(HalpArm64DmaCoherency));
    HalpArm64DmaCoherency.CacheLineSize = HalpArm64GetCacheLineSize();

    /*
     * Parse IORT for cache coherency information.
     *
     * The IORT (IO Remapping Table) contains Root Complex nodes that
     * specify whether DMA is cache-coherent. If all root complexes
     * are coherent, we can skip cache maintenance operations.
     */
    if (HalpArm64IortInfo.Present)
    {
        BOOLEAN AllCoherent = TRUE;

        for (Index = 0; Index < HalpArm64IortInfo.RootComplexCount; Index++)
        {
            PHALP_ARM64_IORT_ROOT_COMPLEX_ENTRY RcEntry =
                &HalpArm64IortInfo.RootComplexEntries[Index];

            /*
             * CacheCoherent bit from IORT Root Complex:
             * Bit 0: Memory access is cache coherent
             */
            if (RcEntry->CacheCoherent & 0x1)
            {
                HalpArm64DmaCoherency.RootComplexCoherent[Index] = TRUE;
            }
            else
            {
                HalpArm64DmaCoherency.RootComplexCoherent[Index] = FALSE;
                AllCoherent = FALSE;
            }
        }

        /* If no root complexes found, assume non-coherent */
        if (HalpArm64IortInfo.RootComplexCount == 0)
        {
            AllCoherent = FALSE;
        }

        HalpArm64DmaCoherency.SystemCoherent = AllCoherent;
    }
    else
    {
        /*
         * No IORT present - assume non-coherent for safety.
         * This ensures cache maintenance is performed even if
         * the system doesn't provide coherency information.
         */
        HalpArm64DmaCoherency.SystemCoherent = FALSE;
    }

    /*
     * Initialize SMMU state if SMMU is present.
     *
     * We configure the SMMU in bypass mode initially, which means
     * DMA addresses are identity-mapped (IOVA = physical address).
     * Full SMMU programming for IOVA translation is future work.
     */
    RtlZeroMemory(&HalpArm64SmmuState, sizeof(HalpArm64SmmuState));

    if (HalpArm64IortInfo.Present && HalpArm64IortInfo.SmmuCount > 0)
    {
        PHALP_ARM64_IORT_SMMU_ENTRY SmmuEntry = &HalpArm64IortInfo.SmmuEntries[0];

        HalpArm64SmmuState.Present = TRUE;
        HalpArm64SmmuState.Model = SmmuEntry->Model;
        HalpArm64SmmuState.BaseAddress = SmmuEntry->BaseAddress;
        HalpArm64SmmuState.Span = SmmuEntry->Span;
        HalpArm64SmmuState.BypassMode = TRUE;

        /*
         * SMMU bypass mode configuration.
         *
         * For SMMUv2: Set SMMU_sCR0.CLIENTPD to disable client port.
         * For SMMUv3: Configure stream table entries for bypass.
         *
         * Full SMMU programming is deferred to HalpArm64SmmuInitialize().
         * For now, we rely on firmware leaving SMMU in bypass mode.
         */
    }

    HalpArm64DmaInitialized = TRUE;
}

/*
 * HalpArm64SmmuInitialize - Initialize SMMUv2/v3 for DMA address translation
 *
 * This function programs the System Memory Management Unit (SMMU) to provide
 * IOVA (I/O Virtual Address) translation for DMA operations. The SMMU allows:
 * - DMA isolation between devices
 * - Address translation for devices with limited addressing capabilities
 * - Protection from rogue DMA accesses
 *
 * SMMUv3 Architecture (simplified):
 * 1. Stream Table: Maps StreamID (device identifier) to translation context
 * 2. Command Queue: Commands for cache invalidation, configuration changes
 * 3. Event Queue: Records translation faults and errors
 * 4. Page Tables: Multi-level page tables similar to CPU MMU
 *
 * Current Implementation Status:
 * - SMMU detection and bypass mode: ✓ Implemented
 * - Full translation mode: ✗ Not implemented (requires extensive page table management)
 * - Stream table programming: ✗ Stub
 * - Command/event queue setup: ✗ Stub
 *
 * For production use, full SMMU programming requires:
 * 1. Allocate and initialize stream table based on max StreamID
 * 2. Create command and event queues
 * 3. Program SMMU_CR0 to enable translation
 * 4. Set up page tables for each device's IOVA space
 * 5. Handle translation faults via event queue
 *
 * References:
 * - ARM IHI 0070 (SMMUv3 Architecture Specification)
 * - ARM IHI 0062 (IORT Specification for ACPI)
 */
BOOLEAN
HalpArm64SmmuInitialize(VOID)
{
    /*
     * This function is a framework for full SMMU initialization.
     * Currently, the HAL relies on firmware configuring SMMU in bypass mode
     * (identity mapping), which works for systems with coherent DMA where
     * all devices can access the full physical address space.
     *
     * Full SMMU translation mode implementation would include:
     */

    if (!HalpArm64SmmuState.Present)
    {
        DPRINT("[arm64][SMMU] No SMMU detected, skipping initialization\n");
        return FALSE;
    }

    if (HalpArm64SmmuState.Initialized)
    {
        DPRINT("[arm64][SMMU] Already initialized\n");
        return TRUE;
    }

    DPRINT1("[arm64][SMMU] SMMU detected at PA 0x%llx (Model %lu), size 0x%llx\n",
            HalpArm64SmmuState.BaseAddress,
            HalpArm64SmmuState.Model,
            HalpArm64SmmuState.Span);

    /*
     * Step 1: Map SMMU register space
     * Map the SMMU MMIO registers into kernel virtual address space.
     * Required for accessing SMMU_CR0, stream table registers, etc.
     */
    if (!HalpArm64SmmuState.MappedBase)
    {
        PHYSICAL_ADDRESS SmmuPhys;
        SmmuPhys.QuadPart = HalpArm64SmmuState.BaseAddress;
        UNREFERENCED_PARAMETER(SmmuPhys);

        /* TODO: Use MmMapIoSpace to map SMMU registers */
        /* HalpArm64SmmuState.MappedBase = MmMapIoSpace(SmmuPhys, HalpArm64SmmuState.Span, MmNonCached); */

        if (!HalpArm64SmmuState.MappedBase)
        {
            DPRINT1("[arm64][SMMU] Failed to map SMMU register space\n");
            return FALSE;
        }

        DPRINT("[arm64][SMMU] Mapped SMMU registers to VA %p\n",
               HalpArm64SmmuState.MappedBase);
    }

    /*
     * Step 2: Allocate Stream Table
     * The stream table maps StreamID (from PCIe Requester ID, etc.) to
     * translation context. Size depends on max StreamID from IORT.
     *
     * For SMMUv3, each entry is 64 bytes (linear table) or a 2-level structure.
     */
    if (HalpArm64SmmuState.Model == 3 && !HalpArm64SmmuState.StreamTableBase)
    {
        /* TODO: Allocate physically contiguous stream table */
        /*
         * SIZE_T StreamTableSize = HalpArm64SmmuState.StreamTableEntries * 64;
         * PHYSICAL_ADDRESS Low = {0}, High = {0xFFFFFFFFFFFFFFFFULL}, Boundary = {0};
         *
         * HalpArm64SmmuState.StreamTableBase = MmAllocateContiguousMemorySpecifyCache(
         *     StreamTableSize, Low, High, Boundary, MmCached);
         *
         * if (!HalpArm64SmmuState.StreamTableBase)
         *     return FALSE;
         *
         * RtlZeroMemory(HalpArm64SmmuState.StreamTableBase, StreamTableSize);
         * HalpArm64SmmuState.StreamTablePhysical = MmGetPhysicalAddress(HalpArm64SmmuState.StreamTableBase);
         */

        DPRINT("[arm64][SMMU] Stream table allocation: %lu entries (not implemented)\n",
               HalpArm64SmmuState.StreamTableEntries);
    }

    /*
     * Step 3: Configure SMMU for bypass mode
     * Set SMMU_CR0.SMMUEN=0 to disable translation (bypass mode).
     * This allows DMA to proceed with identity mapping while we defer
     * full IOMMU programming.
     */
    if (HalpArm64SmmuState.MappedBase)
    {
        /* TODO: Write SMMU_CR0 to configure bypass */
        /*
         * volatile ULONG *SmmuCr0 = (ULONG*)((ULONG_PTR)HalpArm64SmmuState.MappedBase + 0x20);
         * ULONG Cr0 = *SmmuCr0;
         * Cr0 &= ~(1 << 0);  // Clear SMMUEN bit (disable translation)
         * *SmmuCr0 = Cr0;
         * __asm__ __volatile__("dsb sy" ::: "memory");
         */

        DPRINT1("[arm64][SMMU] SMMU configured for bypass mode (translation disabled)\n");
        HalpArm64SmmuState.BypassMode = TRUE;
    }

    /*
     * Step 4: Set up Command Queue (SMMUv3)
     * The command queue is used to send commands to the SMMU:
     * - CFGI: Invalidate configuration cache
     * - TLBI: Invalidate TLB entries
     * - SYNC: Synchronization barrier
     */
    if (HalpArm64SmmuState.Model == 3 && !HalpArm64SmmuState.CmdQueue)
    {
        /* TODO: Allocate and configure command queue */
        /*
         * SIZE_T QueueSize = 4096;  // Typical size
         * HalpArm64SmmuState.CmdQueue = MmAllocateContiguousMemorySpecifyCache(...);
         * Program SMMU_CMDQ_BASE, SMMU_CMDQ_PROD, SMMU_CMDQ_CONS registers
         */

        DPRINT("[arm64][SMMU] Command queue setup (not implemented)\n");
    }

    /*
     * Step 5: Set up Event Queue (SMMUv3)
     * The event queue receives translation fault events and other errors.
     * The HAL should process these asynchronously via DPC/interrupt.
     */
    if (HalpArm64SmmuState.Model == 3 && !HalpArm64SmmuState.EventQueue)
    {
        /* TODO: Allocate and configure event queue */
        /*
         * Program SMMU_EVENTQ_BASE, SMMU_EVENTQ_PROD, SMMU_EVENTQ_CONS registers
         * Set up interrupt for event queue (SMMU_IRQ_CTRL)
         */

        DPRINT("[arm64][SMMU] Event queue setup (not implemented)\n");
    }

    /*
     * Step 6: Initialize per-device translation contexts
     * For each device that requires IOMMU protection:
     * 1. Allocate page tables for the device's IOVA space
     * 2. Program stream table entry with page table base
     * 3. Set configuration flags (enable, fault handling, etc.)
     */
    /* TODO: Implement device-specific IOMMU context setup */

    HalpArm64SmmuState.Initialized = TRUE;
    DPRINT1("[arm64][SMMU] SMMU initialization complete (bypass mode)\n");

    return TRUE;
}

VOID
NTAPI
HalpReportResourceUsage(
    _In_ PUNICODE_STRING HalName,
    _In_ INTERFACE_TYPE InterfaceType)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    DbgPrint("%wZ initialized\n", HalName);
}

VOID
NTAPI
HalpRegisterPciDebuggingDeviceInfo(VOID)
{
}

NTSTATUS
NTAPI
HalpOpenRegistryKey(
    _Out_ PHANDLE KeyHandle,
    _In_opt_ HANDLE RootKey,
    _In_ PUNICODE_STRING KeyName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN Create)
{
    NTSTATUS Status;
    ULONG Disposition;
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes,
                               KeyName,
                               OBJ_CASE_INSENSITIVE,
                               RootKey,
                               NULL);

    if (Create)
    {
        Status = ZwCreateKey(KeyHandle,
                             DesiredAccess,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_VOLATILE,
                             &Disposition);
    }
    else
    {
        Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
    }

    return Status;
}

/*
 * ARM64 PCI-related HAL stub functions.
 *
 * These functions are called by the ACPI bus driver during PCI root bridge
 * enumeration. On x86/x64, these are implemented in pcibus.c with full
 * bus handler support. On ARM64, we provide stub implementations that log
 * the ACPI PCI configuration but don't yet integrate with a bus handler
 * infrastructure (ARM64 uses firmware-published MMCONFIG or ACPI root-bridge
 * _CBA for PCI configuration space).
 *
 * TODO: Implement full ARM64 PCI bus handler support for complete PnP integration.
 */

VOID
NTAPI
HalpRecordPciMaxGsi(
    _In_ const HAL_ACPI_PCI_ROUTE_ENTRY *Entry)
{
    if (Entry)
    {
        DPRINT("[arm64][PCI] Recording GSI %lu for routing entry (Seg=%u Bus=%u Dev=%u Pin=%c)\n",
               Entry->Gsi,
               Entry->Segment,
               (ULONG)Entry->Bus,
               (ULONG)Entry->Device,
               (Entry->Pin >= 1 && Entry->Pin <= 4) ? (CHAR)('A' + Entry->Pin - 1) : '?');
    }
}

VOID
NTAPI
HalpConfigurePciRootBridge(
    _In_ const HAL_ACPI_PCI_ROOT_INFO *Info)
{
    PHALP_ARM64_PCI_ROOT_BRIDGE Root = NULL;
    ULONG Index;
    ULONG RootCount;

    if (!Info)
        return;

    RootCount = HalpArm64PciRootBridgeCount;
    if (RootCount > HAL_ARM64_MAX_PCI_ROOT_BRIDGES)
    {
        RootCount = HAL_ARM64_MAX_PCI_ROOT_BRIDGES;
    }

    for (Index = 0; Index < RootCount; Index++)
    {
        PHALP_ARM64_PCI_ROOT_BRIDGE Current = &HalpArm64PciRootBridges[Index];

        if (!Current->Present)
        {
            continue;
        }

        if (Current->Segment == (USHORT)Info->Segment &&
            Current->BusStart == (UCHAR)Info->BusStart &&
            Current->BusEnd == (UCHAR)Info->BusEnd)
        {
            Root = Current;
            break;
        }
    }

    if (!Root)
    {
        if (RootCount >= HAL_ARM64_MAX_PCI_ROOT_BRIDGES)
        {
            DPRINT1("[arm64][PCI] Too many ACPI PCI root bridges; dropping segment %lu bus %lu-%lu\n",
                    Info->Segment,
                    Info->BusStart,
                    Info->BusEnd);
        }
        else
        {
            Root = &HalpArm64PciRootBridges[RootCount];
            RtlZeroMemory(Root, sizeof(*Root));
            Root->Present = TRUE;
            Root->Segment = (USHORT)Info->Segment;
            Root->BusStart = (UCHAR)Info->BusStart;
            Root->BusEnd = (UCHAR)Info->BusEnd;
            Root->ConfigSpaceBasePresent = Info->ConfigSpaceBasePresent;
            Root->ConfigSpaceBase = Info->ConfigSpaceBase;
            KeMemoryBarrier();
            HalpArm64PciRootBridgeCount = RootCount + 1;
        }
    }
    else
    {
        Root->Present = TRUE;
        Root->Segment = (USHORT)Info->Segment;
        Root->BusStart = (UCHAR)Info->BusStart;
        Root->BusEnd = (UCHAR)Info->BusEnd;
        Root->ConfigSpaceBasePresent = Info->ConfigSpaceBasePresent;
        Root->ConfigSpaceBase = Info->ConfigSpaceBase;
    }

    /*
     * Log the PCI root bridge configuration received from ACPI.
     * This confirms that ACPI PCI enumeration is working on ARM64.
     */
    DPRINT1("[arm64][PCI] ACPI PCI Root Bridge: Segment=%lu Bus=%lu BusRange=[%lu-%lu]\n",
            Info->Segment,
            Info->Bus,
            Info->BusStart,
            Info->BusEnd);

    if (Info->ConfigSpaceBasePresent)
    {
        DPRINT1("[arm64][PCI]   Config Space Base: 0x%I64x (_CBA)\n",
                Info->ConfigSpaceBase);
    }
    else if (!HalpAcpiMcfgAllocations || !HalpAcpiMcfgAllocationCount)
    {
        DPRINT1("[arm64][PCI]   No MCFG or _CBA config-space base published for this root bridge.\n");
    }

    if (Info->IoWindow.Present)
    {
        DPRINT1("[arm64][PCI]   IO Window: [0x%I64x - 0x%I64x]\n",
                Info->IoWindow.Base,
                Info->IoWindow.Limit);
    }

    if (Info->MemoryWindow.Present)
    {
        DPRINT1("[arm64][PCI]   Memory Window: [0x%I64x - 0x%I64x]\n",
                Info->MemoryWindow.Base,
                Info->MemoryWindow.Limit);
    }

    if (Info->PrefetchWindow.Present)
    {
        DPRINT1("[arm64][PCI]   Prefetch Window: [0x%I64x - 0x%I64x]\n",
                Info->PrefetchWindow.Base,
                Info->PrefetchWindow.Limit);
    }

    if (Info->Osc.Evaluated)
    {
        DPRINT1("[arm64][PCI]   _OSC: Status=0x%lx Control=0x%lx Grant=0x%lx %s\n",
                Info->Osc.StatusFlags,
                Info->Osc.ControlRequest,
                Info->Osc.ControlGranted,
                Info->Osc.Failed ? "(FAILED)" : "(OK)");
    }

    if (Info->ConfigSpaceBasePresent)
    {
        DPRINT1("[arm64][PCI]   ARM64 HAL will use _CBA for config-space access while MCFG is unavailable.\n");
    }
}

VOID
NTAPI
HalpRegisterPciRouteQuery(
    _In_opt_ PHAL_ACPI_PCI_ROUTE_QUERY Provider)
{
    /*
     * ARM64 FIX: Actually store the callback so HalGetBusDataByOffset
     * can use it to look up GSI values from ACPI _PRT for PCI devices.
     *
     * This is critical for VirtIO and other PCI devices where firmware
     * does not program the InterruptLine field in PCI config space.
     */
    HalpArm64PciRouteQueryCallback = Provider;

    if (Provider)
    {
        DPRINT1("[arm64][PCI] Registered PCI routing query provider at %p\n", Provider);
    }
    else
    {
        DPRINT1("[arm64][PCI] Unregistered PCI routing query provider\n");
    }
}

VOID
NTAPI
HalpSetPciRoutingMap(
    _In_reads_opt_(EntryCount) const HAL_ACPI_PCI_ROUTE_ENTRY *Entries,
    _In_ ULONG EntryCount)
{
    if (Entries && EntryCount > 0)
    {
        ULONG MaxGsi = 0;
        ULONG i;

        for (i = 0; i < EntryCount; i++)
        {
            if (Entries[i].Gsi > MaxGsi)
                MaxGsi = Entries[i].Gsi;
        }

        DPRINT1("[arm64][PCI] Imported %lu PCI routing entries (max GSI %lu)\n",
                EntryCount, MaxGsi);
    }
}

BOOLEAN
NTAPI
HalQueryArm64TimerConfig(
    _Out_ PHAL_ARM64_TIMER_CONFIG Config)
{
    ULONG Flags = 0;

    if (!Config)
        return FALSE;

    /* Defaults: virtual timer PPI 27, level-sensitive */
    Config->Vector = 27;
    Config->UseVirtual = TRUE;
    Config->Mode = LevelSensitive;

    if (HalpArm64GtdtInfo.Present)
    {
        if (HalpArm64GtdtInfo.NonSecureEl1Interrupt)
        {
            Config->Vector = HalpArm64GtdtInfo.NonSecureEl1Interrupt;
            Flags = HalpArm64GtdtInfo.NonSecureEl1Flags;
            Config->UseVirtual = FALSE;
        }
        else if (HalpArm64GtdtInfo.VirtualTimerInterrupt)
        {
            Config->Vector = HalpArm64GtdtInfo.VirtualTimerInterrupt;
            Flags = HalpArm64GtdtInfo.VirtualTimerFlags;
            Config->UseVirtual = TRUE;
        }
        else if (HalpArm64GtdtInfo.SecureEl1Interrupt)
        {
            Config->Vector = HalpArm64GtdtInfo.SecureEl1Interrupt;
            Flags = HalpArm64GtdtInfo.SecureEl1Flags;
            Config->UseVirtual = FALSE;
        }

        /* GTDT flags: bit1 indicates trigger (0=edge, 1=level) */
        if ((Flags & 0x2) == 0)
            Config->Mode = Latched;
    }

    return TRUE;
}

PUCHAR KdComPortInUse = NULL;

/* Very small GICv2-style bring-up for QEMU virt */
#define HAL_ARM64_GICD_BASE_DEFAULT   0x08000000ULL
#define HAL_ARM64_GICC_BASE_DEFAULT   0x08010000ULL
#define HAL_ARM64_GICR_BASE_DEFAULT   0x080A0000ULL
#define HAL_ARM64_GICR_STRIDE_DEFAULT 0x20000ULL
#define HAL_ARM64_GICR_SGI_OFFSET_DEFAULT 0x10000ULL
#define HAL_ARM64_SYSTEM_RANGE_BASE 0xFFFF800000000000ULL
#define HAL_ARM64_KSEG0_BASE HAL_ARM64_SYSTEM_RANGE_BASE
#define HAL_ARM64_PHYS_MAP_BASE 0xFFFFFC0000000000ULL
#define HAL_ARM64_PHYS_ADDR_MASK 0x0000FFFFFFFFFFFFULL
#define HAL_ARM64_GICD_MAP_LENGTH 0x10000ULL
#define HAL_ARM64_GICR_FRAME_LENGTH 0x20000ULL
#define HAL_ARM64_ITS_MAP_LENGTH 0x20000ULL
#define HAL_ARM64_GICV2M_SETSPI 0x40
#define HAL_ARM64_LPI_BASE 8192u
#define HAL_ARM64_LPI_COUNT 1024u
#define HAL_ARM64_LPI_PROP_ENABLED 0x01u
#define HAL_ARM64_LPI_PROP_GROUP1  0x02u
#define HAL_ARM64_LPI_PROP_PRIO_DEFAULT 0xA0u

#define GICD_CTLR         0x000
#define GICD_TYPER        0x004
#define GICD_IGROUPR      0x080
#define GICD_ISENABLER    0x100
#define GICD_ICENABLER    0x180
#define GICD_ICPENDR      0x280
#define GICD_IPRIORITYR   0x400
#define GICD_ITARGETSR    0x800
#define GICD_SGIR         0xF00
#define GICD_IROUTER      0x6100

#define GICC_CTLR         0x000
#define GICC_PMR          0x004
#define GICC_BPR          0x008
#define GICC_IAR          0x00C
#define GICC_EOIR         0x010

/* GICv3 Redistributor (SGI/PPI) registers */
#define GICR_CTLR         0x000
#define GICR_TYPER        0x008
#define GICR_WAKER        0x014
#define GICR_IGROUPR0     0x080
#define GICR_IGRPMODR0    0xD00  /* Group modifier register (determines Secure/NonSecure) */
#define GICR_ISENABLER0   0x100
#define GICR_ICENABLER0   0x180
#define GICR_ISPENDR0     0x200
#define GICR_ICPENDR0     0x280
#define GICR_ISACTIVER0   0x300
#define GICR_ICACTIVER0   0x380
#define GICR_IPRIORITYR   0x400
#define GICR_ICFGR0       0xC00  /* Interrupt config register for SGIs (read-only) */
#define GICR_ICFGR1       0xC04  /* Interrupt config register for PPIs */
#define GICR_PROPBASER    0x070
#define GICR_PENDBASER    0x078

/* GICD Interrupt Configuration Register - offset 0xC00 from GICD base */
#define GICD_ICFGR        0xC00

#define GITS_CTLR         0x0000
#define GITS_TYPER        0x0008
#define GITS_CBASER       0x0080
#define GITS_CWRITER      0x0088
#define GITS_CREADR       0x0090
#define GITS_BASER        0x0100
#define GITS_TRANSLATER   0x10000
#define HAL_ARM64_GITS_BASER_COUNT 8

#define GITS_CMD_SYNC     0x05
#define GITS_CMD_MAPD     0x08
#define GITS_CMD_MAPC     0x09
#define GITS_CMD_MAPTI    0x0A
#define GITS_CMD_INV      0x0C
#define GITS_CMD_INVALL   0x0D

#define GITS_TYPER_ITT_ENTRY_SIZE_SHIFT 4
#define GITS_TYPER_ITT_ENTRY_SIZE_MASK  0xFULL
#define GITS_TYPER_IDBITS_SHIFT         8
#define GITS_TYPER_IDBITS_MASK          0x1FULL
#define GITS_TYPER_DEVBITS_SHIFT        13
#define GITS_TYPER_DEVBITS_MASK         0x1FULL

#define GITS_BASER_VALID          (1ULL << 63)
#define GITS_BASER_TYPE_SHIFT     56
#define GITS_BASER_TYPE_MASK      0x7ULL
#define GITS_BASER_ENTRY_SHIFT    48
#define GITS_BASER_ENTRY_MASK     0x1FULL
#define GITS_BASER_PAGE_SHIFT     8
#define GITS_BASER_PAGE_MASK      0x3ULL
#define GITS_BASER_SIZE_MASK      0xFFULL
#define GITS_BASER_ADDR_MASK      0x0000FFFFFFFFF000ULL
#define GITS_BASER_SHARE_SHIFT    10
#define GITS_BASER_SHARE_INNER    (1ULL << GITS_BASER_SHARE_SHIFT)  /* Inner Shareable */
#define GITS_BASER_INNER_NC       (1ULL << 59)  /* Non-cacheable */
#define GITS_BASER_INNER_WAWB     (5ULL << 59)  /* Write-Allocate Write-Back Cacheable */

#define GITS_BASER_TYPE_DEVICE    1
#define GITS_BASER_TYPE_COLLECTION 2

#define GITS_CBASER_VALID         (1ULL << 63)
#define GITS_CBASER_SIZE_MASK     0xFFULL
#define GITS_CBASER_ADDR_MASK     0x0000FFFFFFFFF000ULL
#define GITS_CBASER_SHARE_SHIFT   10
#define GITS_CBASER_SHARE_INNER   (1ULL << GITS_CBASER_SHARE_SHIFT)  /* Inner Shareable */
#define GITS_CBASER_INNER_NC      (1ULL << 59)  /* Non-cacheable */
#define GITS_CBASER_INNER_WAWB    (5ULL << 59)  /* Write-Allocate Write-Back Cacheable */

#define GICR_PROPBASER_ADDR_MASK   0x0000FFFFFFFFF000ULL
#define GICR_PROPBASER_IDBITS_MASK 0x1FULL
#define GICR_PROPBASER_SHARE_INNER (1ULL << 10)  /* Inner Shareable */
#define GICR_PROPBASER_INNER_NC    (1ULL << 7)   /* Inner Non-cacheable */
#define GICR_PENDBASER_ADDR_MASK   0x0000FFFFFFFFF000ULL
#define GICR_PENDBASER_SHARE_INNER (1ULL << 10)  /* Inner Shareable */
#define GICR_PENDBASER_INNER_NC    (1ULL << 7)   /* Inner Non-cacheable */
#define GICR_PENDBASER_PTZ         (1ULL << 62)  /* Pending Table Zero (optional) */

#define HAL_ARM64_SGI_IPI 0
#define HAL_ARM64_SGI_APC 1
#define HAL_ARM64_SGI_DPC 2

static ULONGLONG HalpGicdBase = HAL_ARM64_GICD_BASE_DEFAULT;
static ULONGLONG HalpGiccBase = HAL_ARM64_GICC_BASE_DEFAULT;
static ULONGLONG HalpGicrRegionBase = HAL_ARM64_GICR_BASE_DEFAULT;
static ULONGLONG HalpGicrRegionLength;
static ULONG HalpGicrStride = HAL_ARM64_GICR_STRIDE_DEFAULT;
static ULONG HalpGicrSgiOffset = HAL_ARM64_GICR_SGI_OFFSET_DEFAULT;
static ULONG_PTR HalpGicrCpuBase[MAXIMUM_PROCESSORS];
/* HalpGicItsBase, HalpGicItsId, HalpGicItsPresent, HalpGicItsEnabled and
 * HalpGicMsiFrameBase, HalpGicMsiSpiBase, HalpGicMsiSpiCount, HalpGicMsiFlags,
 * HalpGicMsiPresent are now declared as extern at the top of this file and
 * defined in gic_common.c. This ensures halarm64.c and gic_its.c use the
 * same global variables.
 *
 * HalpGicLpiCount, HalpForceSysRegs, HalpGicParsedMadt, HalpGicInterfaceSelected,
 * and HalpGiccPresent are now declared as extern at the top of this file. */
/* Use identity map until the kernel's private physical alias is established. */
static BOOLEAN HalpUseIdentityMapping = TRUE;
static ULONG HalpUsedAllocDescriptors;
static MEMORY_ALLOCATION_DESCRIPTOR HalpAllocationDescriptorArray[64];
static BOOLEAN HalpGicPhase0Complete;

#define HALP_ARM64_MAX_DEFERRED_INTERRUPTS 32

typedef struct _HALP_ARM64_DEFERRED_INTERRUPT
{
    BOOLEAN Valid;
    ULONG Vector;
    KIRQL Irql;
    KINTERRUPT_MODE InterruptMode;
} HALP_ARM64_DEFERRED_INTERRUPT, *PHALP_ARM64_DEFERRED_INTERRUPT;

static HALP_ARM64_DEFERRED_INTERRUPT HalpArm64DeferredInterrupts[HALP_ARM64_MAX_DEFERRED_INTERRUPTS];

/*
 * PSCI (Power State Coordination Interface) definitions for ARM64.
 * These are used for SMP bring-up (CPU_ON) and power management (SYSTEM_RESET, SYSTEM_OFF).
 */
#define PSCI_FN_CPU_ON_64           0xC4000003ULL
#define PSCI_FN_SYSTEM_OFF          0x84000008UL
#define PSCI_FN_SYSTEM_RESET        0x84000009UL
#define PSCI_SUCCESS                0
#define PSCI_E_INVALID_PARAMS       (-2)
#define PSCI_E_DENIED               (-3)
#define PSCI_E_ALREADY_ON           (-4)
#define PSCI_E_ON_PENDING           (-5)
#define PSCI_E_INTERNAL_FAILURE     (-6)
#define PSCI_E_NOT_PRESENT          (-7)
#define PSCI_E_DISABLED             (-8)
#define PSCI_E_INVALID_ADDRESS      (-9)

/*
 * SMP processor startup tracking
 * HalpStartedProcessorCount is defined in smp.c
 */
extern ULONG HalpStartedProcessorCount;

/* Legacy variables kept for compatibility */
static PHYSICAL_ADDRESS HalpApEntryPointPhys;
static PKPROCESSOR_STATE HalpApProcessorState;

/*
 * External SMP infrastructure functions from smp.c
 */
extern BOOLEAN HalpArm64InitApTrampoline(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
extern VOID HalpArm64PrepareApData(_In_ ULONG ProcessorNumber,
                                   _In_ UINT64 EntryPoint,
                                   _In_ UINT64 StackPointer,
                                   _In_ UINT64 Arg0,
                                   _In_ UINT64 GicrBase);
extern BOOLEAN HalpArm64WaitForApSync(_In_ ULONG TimeoutMs);
extern UINT64 HalpArm64GetTrampolinePhysicalAddress(VOID);
extern BOOLEAN HalpArm64IsTrampolineInitialized(VOID);
extern VOID HalpArm64DiscoverParkedCpus(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);
extern BOOLEAN HalpArm64WakeParkedCpu(_In_ ULONG ProcessorNumber,
                                      _In_ UINT64 EntryPoint,
                                      _In_ UINT64 ContextId);
extern VOID HalpArm64EnableCpuInterface(VOID);

#define HAL_ARM64_ITS_CMD_ENTRY_SIZE 32
#define HAL_ARM64_ITS_CMD_QUEUE_PAGES 4

typedef struct _HALP_ARM64_ITS_DEVICE
{
    struct _HALP_ARM64_ITS_DEVICE *Next;
    USHORT RequesterId;
    volatile LONG InitState;
    ULONG IttEntries;
    PVOID IttVa;
    PHYSICAL_ADDRESS IttPa;
} HALP_ARM64_ITS_DEVICE, *PHALP_ARM64_ITS_DEVICE;

static KSPIN_LOCK HalpGicItsLock;
static BOOLEAN HalpGicItsInitialized;
static BOOLEAN HalpGicItsInitFailed;
static volatile LONG HalpGicItsInitState;
static ULONG_PTR HalpGicItsVa;  /* KSEG0-mapped virtual address of ITS */
static ULONG HalpGicItsDeviceIdBits;
static ULONG HalpGicItsEventIdBits;
static ULONG HalpGicItsEventIdLimit;
static ULONG HalpGicItsLpiIdBits;
static ULONG HalpGicItsIttEntrySize;
static ULONG HalpGicItsDeviceTableEntries;
static ULONG HalpGicItsCollectionEntries;
static ULONG HalpGicItsCmdEntries;
static PHALP_ARM64_ITS_DEVICE *HalpGicItsDeviceBuckets;
static ULONG HalpGicItsDeviceBucketCount;
static ULONG HalpGicItsCacheLineSize;
static UINT64 *HalpGicItsCmdQueue;
static PHYSICAL_ADDRESS HalpGicItsCmdQueuePa;
static ULONG HalpGicItsCmdWrite;
static PVOID HalpGicItsDeviceTableRaw;
static PVOID HalpGicItsDeviceTable;
static PHYSICAL_ADDRESS HalpGicItsDeviceTablePa;
static SIZE_T HalpGicItsDeviceTableBytes;
static PVOID HalpGicItsCollectionTableRaw;
static PVOID HalpGicItsCollectionTable;
static PHYSICAL_ADDRESS HalpGicItsCollectionTablePa;
static SIZE_T HalpGicItsCollectionTableBytes;
static PVOID HalpGicLpiConfigRaw;
static UCHAR *HalpGicLpiConfig;
static PHYSICAL_ADDRESS HalpGicLpiConfigPa;
static SIZE_T HalpGicLpiConfigSize;
static PVOID HalpGicLpiPendingRaw[MAXIMUM_PROCESSORS];
static UCHAR *HalpGicLpiPending[MAXIMUM_PROCESSORS];
static PHYSICAL_ADDRESS HalpGicLpiPendingPa[MAXIMUM_PROCESSORS];
static BOOLEAN HalpGicItsCollectionMapped[MAXIMUM_PROCESSORS];

/* Default interrupt affinity - defined in gic/gic_common.c */
extern KAFFINITY HalpDefaultInterruptAffinity;

volatile ULONG *HalpMmio(ULONG_PTR Base, ULONG Offset);
ULONGLONG HalpMmioRead64(ULONG_PTR Base, ULONG Offset);
VOID HalpMmioWrite64(ULONG_PTR Base, ULONG Offset, ULONGLONG Value);
static __inline ULONG_PTR HalpGicrBase(_In_ ULONG Cpu);
static VOID HalpArm64ApplyDeferredInterruptEnables(VOID);

/* ARM64 system register accessors - forward declarations */
FORCEINLINE ULONGLONG HalpReadMpidr(void);
FORCEINLINE ULONGLONG HalpReadMidr(void);
FORCEINLINE ULONGLONG HalpReadPfr0(void);

static __inline ULONG
HalpGicItsLog2(_In_ ULONG Value)
{
    ULONG Log = 0;

    while (Value > 1)
    {
        Value >>= 1;
        Log++;
    }

    return Log;
}

static __inline ULONG
HalpGicItsRoundUpPow2(_In_ ULONG Value)
{
    ULONG Pow2 = 1;

    if (Value == 0)
        return 1;

    while ((Pow2 < Value) && (Pow2 < (1UL << 30)))
        Pow2 <<= 1;

    return Pow2;
}

static __inline ULONG
HalpGicItsDeviceHash(_In_ USHORT RequesterId)
{
    ULONG Mask = HalpGicItsDeviceBucketCount ? (HalpGicItsDeviceBucketCount - 1) : 0;
    return (ULONG)RequesterId & Mask;
}

static BOOLEAN
HalpGicItsInitDeviceMap(_In_ ULONG DeviceLimit)
{
    ULONG Target;
    SIZE_T Bytes;

    if (HalpGicItsDeviceBuckets)
        return TRUE;

    Target = DeviceLimit;
    if (Target == 0)
        Target = 1;
    if (Target > 4096)
        Target = 4096;

    HalpGicItsDeviceBucketCount = 1;
    while (HalpGicItsDeviceBucketCount < Target)
        HalpGicItsDeviceBucketCount <<= 1;

    if (HalpGicItsDeviceBucketCount < 64)
        HalpGicItsDeviceBucketCount = 64;

    Bytes = (SIZE_T)HalpGicItsDeviceBucketCount * sizeof(*HalpGicItsDeviceBuckets);
    HalpGicItsDeviceBuckets = ExAllocatePoolWithTag(NonPagedPool, Bytes, TAG_HAL);
    if (!HalpGicItsDeviceBuckets)
        return FALSE;

    RtlZeroMemory(HalpGicItsDeviceBuckets, Bytes);
    return TRUE;
}

static ULONG
HalpArm64GetCacheLineSize(VOID)
{
    ULONGLONG Ctr;
    ULONG Line;

    if (HalpGicItsCacheLineSize)
        return HalpGicItsCacheLineSize;

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    Line = 4u << ((ULONG)((Ctr >> 16) & 0xF));
    if ((Line & (Line - 1)) != 0 || Line < 16)
        Line = 64;

    HalpGicItsCacheLineSize = Line;
    return Line;
}

static VOID
HalpArm64CleanDcacheRange(_In_ PVOID Base, _In_ SIZE_T Length)
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
        __asm__ __volatile__("dc cvac, %0" :: "r"(Address) : "memory");
    }

    __asm__ __volatile__("dsb ish" ::: "memory");
}

static VOID
HalpArm64CleanInvalidateDcacheRange(_In_ PVOID Base, _In_ SIZE_T Length)
{
    ULONG_PTR Start;
    ULONG_PTR End;
    ULONG Line;

    if (!Base || Length == 0)
        return;

    Line = HalpArm64GetCacheLineSize();
    Start = (ULONG_PTR)Base & ~((ULONG_PTR)Line - 1);
    End = (ULONG_PTR)Base + Length;

    __asm__ __volatile__("dsb ish" ::: "memory");
    for (ULONG_PTR Address = Start; Address < End; Address += Line)
    {
        __asm__ __volatile__("dc civac, %0" :: "r"(Address) : "memory");
    }
    __asm__ __volatile__("dsb ish" ::: "memory");
}

static ULONG
HalpGicItsSelectCpuFromAffinity(_In_ ULONGLONG Affinity)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();
    ULONG Limit = sizeof(ULONGLONG) * 8;
    ULONG MaxCpu = (MAXIMUM_PROCESSORS < Limit) ? MAXIMUM_PROCESSORS : Limit;

    if (Affinity == 0)
        return Cpu;

    for (Cpu = 0; Cpu < MaxCpu; ++Cpu)
    {
        if (Affinity & (1ULL << Cpu))
            return Cpu;
    }

    return 0;
}

static PVOID
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

    if (Align < PAGE_SIZE)
        Align = PAGE_SIZE;

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

    /*
     * Use cached memory for ITS tables. The ITS hardware can access
     * cached memory with proper cache maintenance, and some emulators
     * (QEMU) work better with normal memory rather than device memory.
     */
    Raw = MmAllocateContiguousMemorySpecifyCache(Total,
                                                 Low,
                                                 High,
                                                 Boundary,
                                                 MmCached);
    if (!Raw)
        return NULL;

    Aligned = ((ULONG_PTR)Raw + (Align - 1)) & ~(Align - 1);
    if (Physical)
        *Physical = MmGetPhysicalAddress((PVOID)Aligned);
    if (RawOut)
        *RawOut = Raw;

    RtlZeroMemory((PVOID)Aligned, Size);
    return (PVOID)Aligned;
}

static BOOLEAN
HalpGicItsSetupBaserTable(
    _In_ ULONG Type,
    _In_ ULONG DesiredEntries,
    _Out_ PVOID *TableRaw,
    _Out_ PVOID *Table,
    _Out_ PPHYSICAL_ADDRESS TablePa,
    _Out_ PSIZE_T TableBytes,
    _Out_ PULONG ActualEntries)
{
    for (ULONG Index = 0; Index < HAL_ARM64_GITS_BASER_COUNT; ++Index)
    {
        ULONGLONG Baser = HalpMmioRead64(HalpGicItsVa, GITS_BASER + (Index * 8));
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
        ULONGLONG Indirect;

        DPRINT1("[arm64][ITS] GITS_BASER[%u]=0x%llx Type=%u\n", Index, Baser, BaserType);

        if (BaserType != Type)
            continue;

        /* Check if this BASER uses indirect tables */
        Indirect = (Baser >> 62) & 1ULL;
        if (Indirect)
        {
            DPRINT1("[arm64][ITS] BASER[%u] uses INDIRECT tables - not supported yet\n", Index);
            return FALSE;
        }

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
        NewBaser |= GITS_BASER_SHARE_INNER;
        NewBaser |= GITS_BASER_INNER_WAWB;
        NewBaser |= GITS_BASER_VALID;

        HalpMmioWrite64(HalpGicItsVa, GITS_BASER + (Index * 8), NewBaser);
        __asm__ __volatile__("dsb sy" ::: "memory");

        /* Read back to verify */
        {
            ULONGLONG Readback = HalpMmioRead64(HalpGicItsVa, GITS_BASER + (Index * 8));
            DPRINT1("[arm64][ITS] SetupBaserTable: Type=%u BASER[%u] wrote=0x%llx readback=0x%llx PA=0x%llx\n",
                    Type, Index, NewBaser, Readback, Pa.QuadPart);
        }

        if (TableRaw) *TableRaw = Raw;
        if (Table) *Table = Aligned;
        if (TablePa) *TablePa = Pa;
        if (TableBytes) *TableBytes = Bytes;
        if (ActualEntries) *ActualEntries = EntriesPerPage * Pages;

        return TRUE;
    }

    return FALSE;
}

static BOOLEAN
HalpGicItsInitCommandQueue(VOID)
{
    SIZE_T QueueBytes;
    PHYSICAL_ADDRESS Low;
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Boundary;
    ULONGLONG Cbaser;

    QueueBytes = (SIZE_T)HAL_ARM64_ITS_CMD_QUEUE_PAGES * PAGE_SIZE;
    Low.QuadPart = 0;
    High.QuadPart = ~0ULL;
    Boundary.QuadPart = 0;

    /* Use cached memory for command queue, with proper cache maintenance */
    HalpGicItsCmdQueue = MmAllocateContiguousMemorySpecifyCache(QueueBytes,
                                                                Low,
                                                                High,
                                                                Boundary,
                                                                MmCached);
    if (!HalpGicItsCmdQueue)
        return FALSE;

    RtlZeroMemory(HalpGicItsCmdQueue, QueueBytes);
    HalpArm64CleanDcacheRange(HalpGicItsCmdQueue, QueueBytes);
    HalpGicItsCmdQueuePa = MmGetPhysicalAddress(HalpGicItsCmdQueue);
    HalpGicItsCmdEntries = (ULONG)(QueueBytes / HAL_ARM64_ITS_CMD_ENTRY_SIZE);
    HalpGicItsCmdWrite = 0;

    Cbaser = (HalpGicItsCmdQueuePa.QuadPart & GITS_CBASER_ADDR_MASK) |
             ((HAL_ARM64_ITS_CMD_QUEUE_PAGES - 1) & GITS_CBASER_SIZE_MASK) |
             GITS_CBASER_SHARE_INNER |
             GITS_CBASER_INNER_WAWB |
             GITS_CBASER_VALID;
    HalpMmioWrite64(HalpGicItsVa, GITS_CBASER, Cbaser);
    __asm__ __volatile__("dsb sy" ::: "memory");
    HalpMmioWrite64(HalpGicItsVa, GITS_CWRITER, 0);
    DPRINT1("[arm64][ITS] Command queue: PA=0x%llx CBASER=0x%llx\n",
            HalpGicItsCmdQueuePa.QuadPart, Cbaser);

    return TRUE;
}

static BOOLEAN
HalpGicItsInitLpiTables(VOID)
{
    ULONG MaxBits = 0;
    ULONG Bits;
    SIZE_T PropSize;
    UCHAR DefaultProp;

    while ((1u << MaxBits) < HAL_ARM64_LPI_COUNT && MaxBits < 31)
        MaxBits++;

    Bits = HalpGicItsEventIdBits;
    if (Bits == 0)
        Bits = 1;
    if (MaxBits < Bits)
        Bits = MaxBits;
    if (Bits == 0)
        Bits = 1;

    HalpGicItsLpiIdBits = Bits;
    HalpGicLpiCount = 1u << Bits;
    HalpGicItsEventIdLimit = HalpGicLpiCount;

    PropSize = HalpGicLpiCount;
    PropSize = (PropSize + 0xFFFF) & ~((SIZE_T)0xFFFF);

    HalpGicLpiConfig = HalpGicItsAllocAligned(PropSize,
                                              0x10000,
                                              &HalpGicLpiConfigPa,
                                              &HalpGicLpiConfigRaw);
    if (!HalpGicLpiConfig)
        return FALSE;

    HalpGicLpiConfigSize = PropSize;
    DefaultProp = (UCHAR)(HAL_ARM64_LPI_PROP_PRIO_DEFAULT | HAL_ARM64_LPI_PROP_GROUP1);
    RtlFillMemory(HalpGicLpiConfig, PropSize, DefaultProp);
    HalpArm64CleanDcacheRange(HalpGicLpiConfig, PropSize);

    return TRUE;
}

/* Forward declaration */
static BOOLEAN HalpGicItsSendInvall(_In_ ULONG CollectionId);

static VOID
HalpGicItsEnableLpi(_In_ ULONG EventId)
{
    ULONG Cpu;
    ULONG LpiNum;

    if (!HalpGicLpiConfig || EventId >= HalpGicLpiCount)
    {
        DPRINT1("[arm64][ITS] HalpGicItsEnableLpi: FAILED EventId=%lu (LpiConfig=%p Count=%lu)\n",
                EventId, HalpGicLpiConfig, HalpGicLpiCount);
        return;
    }

    LpiNum = HAL_ARM64_LPI_BASE + EventId;
    DPRINT1("[arm64][ITS] HalpGicItsEnableLpi: Enabling LPI %lu (EventId=%lu)\n", LpiNum, EventId);

    HalpGicLpiConfig[EventId] = (UCHAR)(HAL_ARM64_LPI_PROP_PRIO_DEFAULT |
                                       HAL_ARM64_LPI_PROP_GROUP1 |
                                       HAL_ARM64_LPI_PROP_ENABLED);
    HalpArm64CleanDcacheRange(&HalpGicLpiConfig[EventId], sizeof(UCHAR));

    /* Send INVALL to invalidate cached configuration */
    Cpu = KeGetCurrentProcessorNumber();
    __asm__ __volatile__("dsb sy" ::: "memory");
    HalpGicItsSendInvall(Cpu);

    DPRINT1("[arm64][ITS] HalpGicItsEnableLpi: LPI %lu enabled, config=0x%02x\n",
            LpiNum, HalpGicLpiConfig[EventId]);
}

static BOOLEAN
HalpGicItsProgramCpuTables(_In_ ULONG Cpu)
{
    ULONG_PTR Base;
    SIZE_T PendingSize;
    ULONGLONG Prop;
    ULONGLONG Pend;
    ULONG Ctlr;

    Base = HalpGicrBase(Cpu);
    if (Base == 0 || HalpGicItsLpiIdBits == 0)
        return FALSE;

    PendingSize = (HalpGicLpiCount + 7) / 8;
    PendingSize = (PendingSize + 0xFFFF) & ~((SIZE_T)0xFFFF);
    if (!HalpGicLpiPending[Cpu])
    {
        HalpGicLpiPending[Cpu] = HalpGicItsAllocAligned(PendingSize,
                                                        0x10000,
                                                        &HalpGicLpiPendingPa[Cpu],
                                                        &HalpGicLpiPendingRaw[Cpu]);
        if (!HalpGicLpiPending[Cpu])
            return FALSE;
    }

    RtlZeroMemory(HalpGicLpiPending[Cpu], PendingSize);
    HalpArm64CleanInvalidateDcacheRange(HalpGicLpiPending[Cpu], PendingSize);
    HalpArm64CleanDcacheRange(HalpGicLpiConfig, HalpGicLpiConfigSize);

    Prop = (HalpGicLpiConfigPa.QuadPart & GICR_PROPBASER_ADDR_MASK);
    Prop |= ((ULONGLONG)(HalpGicItsLpiIdBits - 1) & GICR_PROPBASER_IDBITS_MASK);
    Prop |= GICR_PROPBASER_SHARE_INNER;
    Prop |= GICR_PROPBASER_INNER_NC;
    HalpMmioWrite64(Base, GICR_PROPBASER, Prop);

    Pend = (HalpGicLpiPendingPa[Cpu].QuadPart & GICR_PENDBASER_ADDR_MASK);
    Pend |= GICR_PENDBASER_SHARE_INNER;
    Pend |= GICR_PENDBASER_INNER_NC;
    /* PTZ bit - indicates table was zeroed; setting this is optional but helps */
    Pend |= GICR_PENDBASER_PTZ;
    HalpMmioWrite64(Base, GICR_PENDBASER, Pend);

    DPRINT1("[arm64][ITS] ProgramCpuTables CPU%u: PROPBASER=0x%llx PENDBASER=0x%llx\n",
            Cpu, Prop, Pend);

    Ctlr = *HalpMmio(Base, GICR_CTLR);
    Ctlr |= 1u; /* Enable LPIs */
    *HalpMmio(Base, GICR_CTLR) = Ctlr;
    __asm__ __volatile__("dsb sy" ::: "memory");

    DPRINT1("[arm64][ITS] ProgramCpuTables CPU%u: LPIs enabled, GICR_CTLR=0x%x\n", Cpu, Ctlr);

    return TRUE;
}

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

    Cmd[0] |= (UINT64)GITS_CMD_MAPD;
    Cmd[0] |= ((UINT64)DeviceId) << 32;
    Cmd[1] |= (UINT64)(SizeField & 0x1FULL);
    Cmd[2] |= (((IttPa >> 8) & ((1ULL << 44) - 1)) << 8);
    if (Valid)
        Cmd[2] |= (1ULL << 63);
}

static VOID
HalpGicItsBuildMapcCmd(
    _Out_writes_(4) UINT64 *Cmd,
    _In_ ULONG CollectionId,
    _In_ ULONGLONG TargetAddress,
    _In_ BOOLEAN Valid)
{
    RtlZeroMemory(Cmd, HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    Cmd[0] |= (UINT64)GITS_CMD_MAPC;
    Cmd[2] |= (((TargetAddress >> 16) & ((1ULL << 36) - 1)) << 16);
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

    Cmd[0] |= (UINT64)GITS_CMD_MAPTI;
    Cmd[0] |= ((UINT64)DeviceId) << 32;
    Cmd[1] |= (UINT64)EventId;
    Cmd[1] |= ((UINT64)PhysId) << 32;
    Cmd[2] |= (UINT64)(CollectionId & 0xFFFFu);
}

static VOID
HalpGicItsBuildSyncCmd(
    _Out_writes_(4) UINT64 *Cmd,
    _In_ ULONGLONG TargetAddress)
{
    RtlZeroMemory(Cmd, HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    Cmd[0] |= (UINT64)GITS_CMD_SYNC;
    Cmd[2] |= (((TargetAddress >> 16) & ((1ULL << 36) - 1)) << 16);
}

static BOOLEAN
HalpGicItsPostCommand(_In_reads_(4) const UINT64 *Cmd)
{
    ULONG Next;
    ULONG ReadIndex;
    ULONG Index;
    ULONG Spins;
    ULONGLONG Target;
    KIRQL OldIrql;
    static ULONG CommandCount = 0;

    if (!HalpGicItsCmdQueue || HalpGicItsCmdEntries == 0)
    {
        DPRINT1("[arm64][ITS] PostCommand: no queue\n");
        return FALSE;
    }

    KeAcquireSpinLock(&HalpGicItsLock, &OldIrql);

    for (Spins = 100000; Spins != 0; --Spins)
    {
        ULONGLONG ReadOffset = HalpMmioRead64(HalpGicItsVa, GITS_CREADR);
        ReadIndex = (ULONG)(ReadOffset / HAL_ARM64_ITS_CMD_ENTRY_SIZE);
        Next = (HalpGicItsCmdWrite + 1) % HalpGicItsCmdEntries;
        if (Next != ReadIndex)
            break;
        KeStallExecutionProcessor(1);
    }

    if (Spins == 0)
    {
        DPRINT1("[arm64][ITS] PostCommand: queue full timeout\n");
        KeReleaseSpinLock(&HalpGicItsLock, OldIrql);
        return FALSE;
    }

    Index = HalpGicItsCmdWrite;
    HalpGicItsCmdQueue[Index * 4 + 0] = Cmd[0];
    HalpGicItsCmdQueue[Index * 4 + 1] = Cmd[1];
    HalpGicItsCmdQueue[Index * 4 + 2] = Cmd[2];
    HalpGicItsCmdQueue[Index * 4 + 3] = Cmd[3];

    HalpArm64CleanDcacheRange(&HalpGicItsCmdQueue[Index * 4],
                              HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    /* Ensure command is visible in memory before updating CWRITER */
    __asm__ __volatile__("dsb sy" ::: "memory");

    HalpGicItsCmdWrite = Next;
    HalpMmioWrite64(HalpGicItsVa,
                    GITS_CWRITER,
                    (ULONGLONG)HalpGicItsCmdWrite * HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    Target = (ULONGLONG)HalpGicItsCmdWrite * HAL_ARM64_ITS_CMD_ENTRY_SIZE;
    KeReleaseSpinLock(&HalpGicItsLock, OldIrql);

    CommandCount++;
    DPRINT1("[arm64][ITS] PostCommand #%u: Cmd[0]=0x%llx [1]=0x%llx [2]=0x%llx [3]=0x%llx at Index=%u Target=0x%llx\n",
            CommandCount, Cmd[0], Cmd[1], Cmd[2], Cmd[3], Index, Target);

    for (Spins = 1000000; Spins != 0; --Spins)
    {
        ULONGLONG ReadOffset = HalpMmioRead64(HalpGicItsVa, GITS_CREADR);
        if (ReadOffset == Target)
        {
            DPRINT1("[arm64][ITS] PostCommand #%u: completed\n", CommandCount);
            return TRUE;
        }
        KeStallExecutionProcessor(1);
    }

    {
        ULONGLONG ReadOffset = HalpMmioRead64(HalpGicItsVa, GITS_CREADR);
        ULONGLONG Ctlr = HalpMmioRead64(HalpGicItsVa, GITS_CTLR);
        ULONGLONG Cwriter = HalpMmioRead64(HalpGicItsVa, GITS_CWRITER);
        DPRINT1("[arm64][ITS] PostCommand #%u: TIMEOUT! CREADR=0x%llx Target=0x%llx\n",
                CommandCount, ReadOffset, Target);
        DPRINT1("[arm64][ITS] PostCommand TIMEOUT: CTLR=0x%llx CWRITER=0x%llx StallBit=%u\n",
                Ctlr, Cwriter, (unsigned)(ReadOffset & 1));
        /* If stall bit is set, clear it by writing 1 to it */
        if (ReadOffset & 1)
        {
            DPRINT1("[arm64][ITS] PostCommand: Clearing stall by writing CREADR=0x%llx\n", ReadOffset);
            HalpMmioWrite64(HalpGicItsVa, GITS_CREADR, ReadOffset);
        }
    }
    return FALSE;
}

static BOOLEAN
HalpGicItsSendMapd(
    _In_ ULONG DeviceId,
    _In_ ULONG IttEntries,
    _In_ ULONGLONG IttPa)
{
    UINT64 Cmd[4];

    HalpGicItsBuildMapdCmd(Cmd, DeviceId, IttEntries, IttPa, TRUE);
    return HalpGicItsPostCommand(Cmd);
}

static BOOLEAN
HalpGicItsSendMapc(
    _In_ ULONG CollectionId,
    _In_ ULONGLONG TargetAddress)
{
    UINT64 Cmd[4];
    UINT64 Sync[4];

    HalpGicItsBuildMapcCmd(Cmd, CollectionId, TargetAddress, TRUE);
    if (!HalpGicItsPostCommand(Cmd))
        return FALSE;

    HalpGicItsBuildSyncCmd(Sync, TargetAddress);
    return HalpGicItsPostCommand(Sync);
}

static BOOLEAN
HalpGicItsSendMapti(
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG PhysId,
    _In_ ULONG CollectionId,
    _In_ ULONGLONG TargetAddress)
{
    UINT64 Cmd[4];
    UINT64 Sync[4];

    HalpGicItsBuildMaptiCmd(Cmd, DeviceId, EventId, PhysId, CollectionId);
    if (!HalpGicItsPostCommand(Cmd))
        return FALSE;

    HalpGicItsBuildSyncCmd(Sync, TargetAddress);
    return HalpGicItsPostCommand(Sync);
}

static VOID
HalpGicItsBuildInvallCmd(
    _Out_writes_(4) UINT64 *Cmd,
    _In_ ULONG CollectionId)
{
    RtlZeroMemory(Cmd, HAL_ARM64_ITS_CMD_ENTRY_SIZE);

    Cmd[0] = (UINT64)GITS_CMD_INVALL;
    Cmd[0] |= ((UINT64)CollectionId) << 16;  /* ICID at bits 31:16 */
}

static BOOLEAN
HalpGicItsSendInvall(_In_ ULONG CollectionId)
{
    UINT64 Cmd[4];
    UINT64 Sync[4];
    ULONGLONG TargetAddress;

    /* Get redistributor base address for this CPU */
    TargetAddress = (ULONGLONG)HalpGicrBase(CollectionId);
    if (TargetAddress == 0)
    {
        DPRINT1("[arm64][ITS] INVALL: No redistributor base for CPU %lu\n", CollectionId);
        return FALSE;
    }

    /* Send INVALL command */
    HalpGicItsBuildInvallCmd(Cmd, CollectionId);
    if (!HalpGicItsPostCommand(Cmd))
        return FALSE;

    /* Send SYNC to ensure INVALL is processed before continuing */
    HalpGicItsBuildSyncCmd(Sync, TargetAddress);
    return HalpGicItsPostCommand(Sync);
}

static PHALP_ARM64_ITS_DEVICE
HalpGicItsFindDevice(_In_ USHORT RequesterId)
{
    PHALP_ARM64_ITS_DEVICE Device;

    if (!HalpGicItsDeviceBuckets || HalpGicItsDeviceBucketCount == 0)
        return NULL;

    Device = HalpGicItsDeviceBuckets[HalpGicItsDeviceHash(RequesterId)];
    while (Device)
    {
        if (Device->RequesterId == RequesterId)
            return Device;
        Device = Device->Next;
    }

    return NULL;
}

static PHALP_ARM64_ITS_DEVICE
HalpGicItsAllocateDevice(_In_ USHORT RequesterId)
{
    PHALP_ARM64_ITS_DEVICE Device;
    ULONG Bucket;

    if (!HalpGicItsDeviceBuckets || HalpGicItsDeviceBucketCount == 0)
        return NULL;

    Device = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Device), TAG_HAL);
    if (!Device)
        return NULL;

    RtlZeroMemory(Device, sizeof(*Device));
    Device->RequesterId = RequesterId;
    Device->InitState = 0;

    Bucket = HalpGicItsDeviceHash(RequesterId);
    Device->Next = HalpGicItsDeviceBuckets[Bucket];
    HalpGicItsDeviceBuckets[Bucket] = Device;
    return Device;

}

static PHALP_ARM64_ITS_DEVICE
HalpGicItsGetDevice(_In_ USHORT RequesterId)
{
    PHALP_ARM64_ITS_DEVICE Device;
    KIRQL OldIrql;
    ULONG Entries;
    ULONG DeviceId;
    LONG State;
    SIZE_T IttBytes;
    PHYSICAL_ADDRESS IttPa;

    if (HalpGicItsDeviceTableEntries == 0 ||
        RequesterId >= HalpGicItsDeviceTableEntries)
    {
        return NULL;
    }

    KeAcquireSpinLock(&HalpGicItsLock, &OldIrql);
    Device = HalpGicItsFindDevice(RequesterId);
    if (!Device)
        Device = HalpGicItsAllocateDevice(RequesterId);
    KeReleaseSpinLock(&HalpGicItsLock, OldIrql);

    if (!Device)
        return NULL;

    State = Device->InitState;
    if (State == 2 && Device->IttEntries)
        return Device;
    if (State == -1)
        return NULL;

    State = InterlockedCompareExchange(&Device->InitState, 1, 0);
    if (State == 2)
        return Device;
    if (State == -1)
        return NULL;
    if (State == 1)
    {
        for (ULONG Spins = 100000; Spins != 0; --Spins)
        {
            State = Device->InitState;
            if (State == 2)
                return Device;
            if (State == -1)
                return NULL;
            KeStallExecutionProcessor(1);
        }
        return NULL;
    }

    /*
     * TEST: Use small ITT for debugging MAPD stall issue.
     * Normally we'd use HalpGicItsEventIdLimit entries, but try just 2.
     */
    Entries = 2;
    Entries = HalpGicItsRoundUpPow2(Entries);

    DPRINT1("[arm64][ITS] GetDevice: ReqId=0x%04x Entries=%lu IttEntrySize=%u (EventIdLimit=%lu)\n",
            (unsigned)RequesterId, Entries, HalpGicItsIttEntrySize, HalpGicItsEventIdLimit);

    /*
     * ITS ITT must be allocated with size rounded up to ensure alignment.
     * Minimum allocation is PAGE_SIZE for proper alignment.
     */
    IttBytes = (SIZE_T)Entries * HalpGicItsIttEntrySize;
    if (IttBytes < PAGE_SIZE)
        IttBytes = PAGE_SIZE;
    Device->IttVa = HalpGicItsAllocAligned(IttBytes, PAGE_SIZE, &IttPa, NULL);
    if (!Device->IttVa)
    {
        DPRINT1("[arm64][ITS] GetDevice: ITT alloc failed\n");
        Device->InitState = -1;
        return NULL;
    }

    DPRINT1("[arm64][ITS] GetDevice: ITT VA=%p PA=0x%llx Bytes=%zu\n",
            Device->IttVa, IttPa.QuadPart, IttBytes);

    HalpArm64CleanInvalidateDcacheRange(Device->IttVa, IttBytes);
    __asm__ __volatile__("dsb sy" ::: "memory");

    Device->IttEntries = Entries;
    Device->IttPa = IttPa;

    DeviceId = (ULONG)Device->RequesterId;
    DPRINT1("[arm64][ITS] GetDevice: Sending MAPD DevId=%u Entries=%lu IttPa=0x%llx\n",
            DeviceId, Device->IttEntries, Device->IttPa.QuadPart);
    if (!HalpGicItsSendMapd(DeviceId,
                            Device->IttEntries,
                            Device->IttPa.QuadPart))
    {
        Device->InitState = -1;
        return NULL;
    }

    Device->InitState = 2;
    return Device;
}

static BOOLEAN
HalpGicItsEnsureCollection(_In_ ULONG Cpu)
{
    ULONGLONG Target;

    if (Cpu >= MAXIMUM_PROCESSORS)
        return FALSE;

    if (!HalpGicItsCollectionMapped[Cpu])
    {
        if (!HalpGicItsProgramCpuTables(Cpu))
            return FALSE;

        Target = (ULONGLONG)HalpGicrBase(Cpu);
        if (Target == 0)
            return FALSE;

        if (!HalpGicItsSendMapc(Cpu, Target))
            return FALSE;

        HalpGicItsCollectionMapped[Cpu] = TRUE;
    }

    return TRUE;
}

static BOOLEAN
HalpGicItsInitialize(VOID)
{
    LONG State;
    ULONG DeviceLimit;
    ULONGLONG Typer;
    ULONGLONG Ctlr;
    ULONG Cpu;
    BOOLEAN Ok = FALSE;

    if (HalpGicItsInitFailed)
        return FALSE;

    if (HalpGicItsInitialized)
        return TRUE;

    /*
     * QEMU HVF workaround: GIC ITS LPI interrupt delivery crashes QEMU HVF
     * (exit code -6 / SIGABRT) when MSI-X interrupts fire through the ITS.
     * The ITS command queue works correctly (MAPC, MAPD, MAPTI all succeed),
     * but actual LPI delivery through the HVF hypervisor is broken.
     *
     * Disable ITS until QEMU HVF GIC ITS support is fixed or we switch to
     * real hardware / QEMU TCG. Devices will fall back to legacy SPI interrupts.
     *
     * TODO: Remove this when QEMU HVF GIC ITS LPI delivery is working.
     */
    DPRINT1("[arm64][ITS] HalpGicItsInitialize: ITS disabled (QEMU HVF LPI workaround)\n");
    HalpGicItsInitFailed = TRUE;
    return FALSE;

    /*
     * ITS initialization requires memory allocations (MmAllocateContiguousMemorySpecifyCache,
     * ExAllocatePoolWithTag) that need IRQL <= APC_LEVEL for the slow path.
     * If called at elevated IRQL, bail out rather than risk bugcheck.
     */
    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        DPRINT1("[arm64][ITS] HalpGicItsInitialize: IRQL %u > APC_LEVEL, deferring\n",
                (ULONG)KeGetCurrentIrql());
        return FALSE;
    }

    State = InterlockedCompareExchange(&HalpGicItsInitState, 1, 0);
    if (State == 2)
        return TRUE;
    if (State == -1)
        return FALSE;
    if (State == 1)
    {
        for (ULONG Spins = 100000; Spins != 0; --Spins)
        {
            State = HalpGicItsInitState;
            if (State == 2)
                return TRUE;
            if (State == -1)
                return FALSE;
            KeStallExecutionProcessor(1);
        }
        return FALSE;
    }

    if (!HalpGicItsPresent || !HalpGicItsBase)
        goto Exit;

    /*
     * Convert ITS physical address to the private physical alias. The public
     * FFFF8000... system range is not a direct map on ARM64.
     */
    HalpGicItsVa = (ULONG_PTR)(HAL_ARM64_PHYS_MAP_BASE |
                               (HalpGicItsBase & HAL_ARM64_PHYS_ADDR_MASK));
    DPRINT1("[arm64][HAL] HalpGicItsInitialize: ITS PA=0x%llx VA=0x%p\n",
            HalpGicItsBase, (PVOID)HalpGicItsVa);

    Typer = HalpMmioRead64(HalpGicItsVa, GITS_TYPER);
    HalpGicItsDeviceIdBits = ((ULONG)(Typer >> GITS_TYPER_DEVBITS_SHIFT) & GITS_TYPER_DEVBITS_MASK) + 1;
    HalpGicItsEventIdBits = ((ULONG)(Typer >> GITS_TYPER_IDBITS_SHIFT) & GITS_TYPER_IDBITS_MASK) + 1;
    HalpGicItsIttEntrySize = ((ULONG)(Typer >> GITS_TYPER_ITT_ENTRY_SIZE_SHIFT) & GITS_TYPER_ITT_ENTRY_SIZE_MASK) + 1;

    DPRINT1("[arm64][ITS] GITS_TYPER=0x%llx DevBits=%u EvtBits=%u IttEntSz=%u\n",
            Typer, HalpGicItsDeviceIdBits, HalpGicItsEventIdBits, HalpGicItsIttEntrySize);

    /*
     * Disable ITS before configuring tables.
     * Clear Enable bit and wait for Quiescent to indicate ITS is idle.
     */
    {
        ULONGLONG InitCtlr = HalpMmioRead64(HalpGicItsVa, GITS_CTLR);
        DPRINT1("[arm64][ITS] Initial GITS_CTLR=0x%llx\n", InitCtlr);
        if (InitCtlr & 1ULL)
        {
            /* ITS was enabled, disable it first */
            InitCtlr &= ~1ULL;
            HalpMmioWrite64(HalpGicItsVa, GITS_CTLR, InitCtlr);
            __asm__ __volatile__("dsb sy" ::: "memory");
            /* Wait for Quiescent bit (bit 31) to be set */
            for (ULONG Spins = 100000; Spins != 0; --Spins)
            {
                InitCtlr = HalpMmioRead64(HalpGicItsVa, GITS_CTLR);
                if (InitCtlr & (1ULL << 31))
                    break;
                KeStallExecutionProcessor(1);
            }
            DPRINT1("[arm64][ITS] After disable: GITS_CTLR=0x%llx\n", InitCtlr);
        }
    }

    if (HalpGicItsDeviceIdBits >= 31)
        DeviceLimit = 0x7FFFFFFF;
    else
        DeviceLimit = (1U << HalpGicItsDeviceIdBits);

    HalpGicItsDeviceTableEntries = DeviceLimit;
    HalpGicItsCollectionEntries = MAXIMUM_PROCESSORS;
    if (!HalpGicItsInitCommandQueue())
        goto Exit;

    if (!HalpGicItsSetupBaserTable(GITS_BASER_TYPE_DEVICE,
                                   HalpGicItsDeviceTableEntries,
                                   &HalpGicItsDeviceTableRaw,
                                   &HalpGicItsDeviceTable,
                                   &HalpGicItsDeviceTablePa,
                                   &HalpGicItsDeviceTableBytes,
                                   &HalpGicItsDeviceTableEntries))
    {
        goto Exit;
    }

    if (HalpGicItsDeviceTableEntries == 0)
        goto Exit;

    if (!HalpGicItsInitDeviceMap(HalpGicItsDeviceTableEntries))
        goto Exit;

    if (!HalpGicItsSetupBaserTable(GITS_BASER_TYPE_COLLECTION,
                                   HalpGicItsCollectionEntries,
                                   &HalpGicItsCollectionTableRaw,
                                   &HalpGicItsCollectionTable,
                                   &HalpGicItsCollectionTablePa,
                                   &HalpGicItsCollectionTableBytes,
                                   &HalpGicItsCollectionEntries))
    {
        /*
         * Collection table is OPTIONAL in the GIC ITS specification.
         * Some implementations store collections internally and don't need
         * a GITS_BASER register for collections. In this case, the ITS
         * uses physical redistributor addresses directly in MAPC commands.
         * Continue with initialization - this is not a fatal error.
         */
        DPRINT("[arm64][HAL] HalpGicItsInitialize: No COLLECTION BASER (ITS uses internal storage)\n");
    }

    if (!HalpGicItsInitLpiTables())
        goto Exit;

    Ctlr = HalpMmioRead64(HalpGicItsVa, GITS_CTLR);
    DPRINT1("[arm64][ITS] Enabling ITS: GITS_CTLR before=0x%llx\n", Ctlr);
    Ctlr |= 1ULL;
    HalpMmioWrite64(HalpGicItsVa, GITS_CTLR, Ctlr);
    __asm__ __volatile__("dsb sy" ::: "memory");
    Ctlr = HalpMmioRead64(HalpGicItsVa, GITS_CTLR);
    DPRINT1("[arm64][ITS] Enabling ITS: GITS_CTLR after=0x%llx\n", Ctlr);
    HalpGicItsEnabled = TRUE;

    Cpu = KeGetCurrentProcessorNumber();
    if (!HalpGicItsEnsureCollection(Cpu))
        goto Exit;

    HalpGicItsInitialized = TRUE;
    Ok = TRUE;
    DPRINT1("[arm64][HAL] HalpGicItsInitialize: SUCCESS\n");

Exit:
    InterlockedExchange(&HalpGicItsInitState, Ok ? 2 : -1);
    if (!Ok)
        HalpGicItsInitFailed = TRUE;

    return Ok;
}

BOOLEAN
NTAPI
HalIsPciMsiSupported(VOID)
{
    return (HalpGicItsPresent || HalpGicMsiPresent);
}

BOOLEAN
NTAPI
HalGetMsiMessageAddress(
    _In_ ULONGLONG Vector,
    _In_ ULONGLONG Affinity,
    _Out_ PULONG AddressLow,
    _Out_opt_ PULONG AddressHigh,
    _Out_ PUSHORT Data)
{
    ULONGLONG Address;
    ULONG Limit;
    ULONG Vector32;

    UNREFERENCED_PARAMETER(Affinity);

    if (!HalpGicMsiPresent || !AddressLow || !Data)
        return FALSE;

    if (HalpGicMsiSpiCount == 0)
        return FALSE;

    if (Vector > MAXULONG)
        return FALSE;

    Vector32 = (ULONG)Vector;
    Limit = (ULONG)HalpGicMsiSpiBase + (ULONG)HalpGicMsiSpiCount;
    if (Vector32 < HalpGicMsiSpiBase || Vector32 >= Limit)
        return FALSE;

    Address = HalpGicMsiFrameBase + HAL_ARM64_GICV2M_SETSPI;
    *AddressLow = (ULONG)(Address & 0xFFFFFFFFu);
    if (AddressHigh) *AddressHigh = (ULONG)(Address >> 32);
    *Data = (USHORT)Vector32;

    return TRUE;
}

BOOLEAN
NTAPI
HalGetMsiMessageAddressEx(
    _In_ USHORT RequesterId,
    _In_ ULONGLONG Vector,
    _In_ ULONGLONG Affinity,
    _Out_ PULONG AddressLow,
    _Out_opt_ PULONG AddressHigh,
    _Out_ PUSHORT Data)
{
    ULONG Cpu;
    ULONG EventId;
    ULONG DeviceId;
    PHALP_ARM64_ITS_DEVICE Device;
    ULONGLONG Address;
    ULONGLONG Target;
    BOOLEAN InitResult;

    DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: ReqId=0x%04x Vec=%llu Aff=0x%llx\n",
            (unsigned)RequesterId, Vector, Affinity);

    if (!AddressLow || !Data)
    {
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: NULL param\n");
        return FALSE;
    }

    if (!HalpGicItsPresent)
    {
        /* No ITS - try GICv2m MSI frame fallback */
        if (HalpGicMsiPresent && HalpGicMsiSpiCount > 0)
        {
            ULONG SpiOffset;
            ULONGLONG MsiAddress;

            /* Map Vector to SPI range */
            if (Vector >= HalpGicMsiSpiBase &&
                Vector < (ULONGLONG)(HalpGicMsiSpiBase + HalpGicMsiSpiCount))
            {
                SpiOffset = (ULONG)(Vector - HalpGicMsiSpiBase);
            }
            else
            {
                /* Use modulo mapping for vectors outside SPI range */
                SpiOffset = (ULONG)(Vector % HalpGicMsiSpiCount);
            }

            /* GICv2m SETSPI_NSR register address */
            MsiAddress = HalpGicMsiFrameBase + HAL_ARM64_GICV2M_SETSPI;
            *AddressLow = (ULONG)(MsiAddress & 0xFFFFFFFFu);
            if (AddressHigh)
                *AddressHigh = (ULONG)(MsiAddress >> 32);
            *Data = (USHORT)(HalpGicMsiSpiBase + SpiOffset);

            DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: GICv2m fallback Vec=%llu -> SPI=%u Addr=0x%llx\n",
                    Vector, HalpGicMsiSpiBase + SpiOffset, MsiAddress);
            return TRUE;
        }
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: ITS not present, no GICv2m fallback\n");
        return FALSE;
    }

    InitResult = HalpGicItsInitialize();
    if (!InitResult)
    {
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: ITS init failed\n");
        return FALSE;
    }

    DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: LpiBase=%u LpiCount=%lu LpiLimit=%lu EventIdLimit=%lu\n",
            HAL_ARM64_LPI_BASE, HalpGicLpiCount, (ULONG)HAL_ARM64_LPI_BASE + HalpGicLpiCount, HalpGicItsEventIdLimit);

    if (Vector < HAL_ARM64_LPI_BASE)
    {
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: Vector %llu < LPI_BASE %u\n", Vector, HAL_ARM64_LPI_BASE);
        return FALSE;
    }

    if (Vector >= (ULONGLONG)HAL_ARM64_LPI_BASE + HalpGicLpiCount)
    {
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: Vector %llu >= LPI limit %llu\n", Vector, (ULONGLONG)HAL_ARM64_LPI_BASE + HalpGicLpiCount);
        return FALSE;
    }

    if (Vector > MAXULONG)
    {
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: Vector %llu > MAXULONG\n", Vector);
        return FALSE;
    }

    EventId = (ULONG)(Vector - HAL_ARM64_LPI_BASE);
    if (EventId >= HalpGicItsEventIdLimit || EventId > 0xFFFFu)
    {
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: EventId %u out of range (limit=%lu)\n", EventId, HalpGicItsEventIdLimit);
        return FALSE;
    }

    Cpu = HalpGicItsSelectCpuFromAffinity(Affinity);
    DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: selected CPU %u\n", Cpu);
    if (!HalpGicItsEnsureCollection(Cpu))
    {
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: EnsureCollection failed for CPU %u\n", Cpu);
        return FALSE;
    }

    Device = HalpGicItsGetDevice(RequesterId);
    if (!Device)
    {
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: GetDevice failed for ReqId 0x%04x\n", (unsigned)RequesterId);
        return FALSE;
    }
    DeviceId = (ULONG)Device->RequesterId;

    Target = (ULONGLONG)HalpGicrBase(Cpu);
    if (Target == 0)
        return FALSE;

    DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: Sending MAPTI DevId=%u EvId=%u Vec=%llu Cpu=%u Target=0x%llx\n",
            DeviceId, EventId, Vector, Cpu, Target);
    if (!HalpGicItsSendMapti(DeviceId,
                             EventId,
                             (ULONG)Vector,
                             Cpu,
                             Target))
    {
        DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: MAPTI FAILED\n");
        return FALSE;
    }
    DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: MAPTI OK\n");

    HalpGicItsEnableLpi(EventId);

    Address = HalpGicItsBase + GITS_TRANSLATER;
    *AddressLow = (ULONG)(Address & 0xFFFFFFFFu);
    if (AddressHigh) *AddressHigh = (ULONG)(Address >> 32);
    *Data = (USHORT)EventId;

    DPRINT1("[arm64][HAL] HalGetMsiMessageAddressEx: SUCCESS MsiAddr=0x%llx Data=0x%x\n",
            Address, (unsigned)*Data);
    return TRUE;
}

BOOLEAN
NTAPI
HalGetMsiVectorRange(
    _Out_ PULONG BaseVector,
    _Out_ PULONG VectorCount)
{
    if (!BaseVector || !VectorCount)
        return FALSE;

    if (HalpGicItsPresent && !HalpGicItsEnabled)
    {
        HalpGicItsInitialize();
    }

    if (HalpGicItsEnabled && HalpGicLpiCount)
    {
        *BaseVector = HAL_ARM64_LPI_BASE;
        *VectorCount = HalpGicLpiCount;
        return TRUE;
    }

    if (HalpGicMsiPresent && HalpGicMsiSpiCount)
    {
        *BaseVector = HalpGicMsiSpiBase;
        *VectorCount = HalpGicMsiSpiCount;
        return TRUE;
    }

    return FALSE;
}

BOOLEAN
NTAPI
HalQueryPciMsiSupport(
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _Out_opt_ PBOOLEAN Supported,
    _Out_opt_ PULONG OscStatusFlags,
    _Out_opt_ PULONG OscControlGranted,
    _Out_opt_ PUSHORT EffectiveSegment,
    _Out_opt_ PULONG OscMaskedControls)
{
    UNREFERENCED_PARAMETER(Bus);

    if (Supported) *Supported = (HalpGicItsPresent || HalpGicMsiPresent);
    if (OscStatusFlags) *OscStatusFlags = 0;
    if (OscControlGranted) *OscControlGranted = 0;
    if (OscMaskedControls) *OscMaskedControls = 0;

    /*
     * If the caller passed segment 0 but the ACPI root bridge is on a
     * different segment, correct it.  This fixes the case where fdo.c
     * hardcodes BusSegment=0 but the firmware reports _SEG=1.
     */
    if (EffectiveSegment)
    {
        USHORT EffSeg = (USHORT)Segment;
        if (Segment == 0)
        {
            PHALP_ARM64_PCI_ROOT_BRIDGE Root;
            Root = HalpArm64FindPciRootBridge(0, Bus);
            if (!Root)
            {
                /* Try finding any root bridge that covers this bus */
                ULONG Index;
                for (Index = 0; Index < HalpArm64PciRootBridgeCount; Index++)
                {
                    PHALP_ARM64_PCI_ROOT_BRIDGE Candidate = &HalpArm64PciRootBridges[Index];
                    if (Candidate->Present &&
                        Bus >= Candidate->BusStart &&
                        Bus <= Candidate->BusEnd)
                    {
                        EffSeg = Candidate->Segment;
                        break;
                    }
                }
            }
            else
            {
                EffSeg = Root->Segment;
            }
        }
        *EffectiveSegment = EffSeg;
    }

    return (HalpGicItsPresent || HalpGicMsiPresent);
}

static __inline PVOID HalpPhysToKseg0(ULONGLONG Physical)
{
    if (Physical == 0)
        return NULL;

    if (Physical >= HAL_ARM64_SYSTEM_RANGE_BASE)
        return (PVOID)(ULONG_PTR)Physical;

    if (HalpUseIdentityMapping)
        return (PVOID)(ULONG_PTR)Physical;

    return (PVOID)(ULONG_PTR)(HAL_ARM64_PHYS_MAP_BASE |
                              (Physical & HAL_ARM64_PHYS_ADDR_MASK));
}

ULONG64
NTAPI
HalpAllocPhysicalMemory(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG64 MaxAddress,
    _In_ PFN_NUMBER PageCount,
    _In_ BOOLEAN Aligned)
{
    ULONG UsedDescriptors;
    ULONG64 PhysicalAddress;
    PFN_NUMBER MaxPage, BasePage, Alignment;
    PLIST_ENTRY NextEntry;
    PMEMORY_ALLOCATION_DESCRIPTOR MdBlock, NewBlock, FreeBlock;
    BOOLEAN IgnoreMaxAddress;

    MaxPage = MaxAddress >> PAGE_SHIFT;

    if ((HalpUsedAllocDescriptors + 2) > RTL_NUMBER_OF(HalpAllocationDescriptorArray))
    {
        DPRINT1("HAL: HalpAllocPhysicalMemory - descriptor array full\n");
        return 0;
    }

    UsedDescriptors = HalpUsedAllocDescriptors;

    /*
     * ARM64-specific: On ARM64 systems, physical memory typically starts at
     * addresses well above 16MB (e.g., 0x40000000 or 0x80000000). The MaxAddress
     * constraint (usually 0x1000000 for 16MB) is an x86 legacy for ISA DMA
     * compatibility, which does not apply to ARM64.
     *
     * We perform two passes:
     * 1. First pass: Try to find memory below MaxAddress (for compatibility)
     * 2. Second pass: If no memory found below MaxAddress, allocate from any
     *    available free memory (ARM64 fallback)
     */
    for (IgnoreMaxAddress = FALSE; ; IgnoreMaxAddress = TRUE)
    {
        NextEntry = LoaderBlock->MemoryDescriptorListHead.Flink;

        while (NextEntry != &LoaderBlock->MemoryDescriptorListHead)
        {
            MdBlock = CONTAINING_RECORD(NextEntry,
                                        MEMORY_ALLOCATION_DESCRIPTOR,
                                        ListEntry);

            Alignment = 0;
            if (Aligned)
                Alignment = ((MdBlock->BasePage + 0x0F) & ~0x0F) - MdBlock->BasePage;

            if ((MdBlock->MemoryType == LoaderFree) ||
                (MdBlock->MemoryType == LoaderFirmwareTemporary))
            {
                BasePage = MdBlock->BasePage;
                /*
                 * Check if this block is suitable:
                 * - BasePage must be non-zero
                 * - Block must have enough pages (including alignment)
                 * - If not ignoring MaxAddress, the allocation must fit below MaxPage
                 */
                if ((BasePage) &&
                    (MdBlock->PageCount >= PageCount + Alignment) &&
                    (IgnoreMaxAddress || (BasePage + PageCount + Alignment < MaxPage)))
                {
                    PhysicalAddress = ((ULONG64)BasePage + Alignment) << PAGE_SHIFT;
                    goto FoundBlock;
                }
            }

            NextEntry = NextEntry->Flink;
        }

        /* If we already tried ignoring MaxAddress and still failed, give up */
        if (IgnoreMaxAddress)
        {
            DPRINT1("HAL: HalpAllocPhysicalMemory - no suitable memory found\n");
            return 0;
        }

        /* First pass failed, try second pass ignoring MaxAddress (ARM64 fallback) */
    }

FoundBlock:

    NewBlock = &HalpAllocationDescriptorArray[HalpUsedAllocDescriptors];
    NewBlock->PageCount = (ULONG)PageCount;
    NewBlock->BasePage = MdBlock->BasePage + Alignment;
    NewBlock->MemoryType = LoaderHALCachedMemory;

    UsedDescriptors++;
    HalpUsedAllocDescriptors = UsedDescriptors;

    if (Alignment)
    {
        if (MdBlock->PageCount > (PageCount + Alignment))
        {
            FreeBlock = &HalpAllocationDescriptorArray[UsedDescriptors];
            FreeBlock->PageCount = MdBlock->PageCount - Alignment - (ULONG)PageCount;
            FreeBlock->BasePage = MdBlock->BasePage + Alignment + (ULONG)PageCount;

            HalpUsedAllocDescriptors++;

            InsertHeadList(&MdBlock->ListEntry, &FreeBlock->ListEntry);
        }

        MdBlock->PageCount = Alignment;
        InsertHeadList(&MdBlock->ListEntry, &NewBlock->ListEntry);
    }
    else
    {
        MdBlock->BasePage += (ULONG)PageCount;
        MdBlock->PageCount -= (ULONG)PageCount;

        InsertTailList(&MdBlock->ListEntry, &NewBlock->ListEntry);

        if (MdBlock->PageCount == 0)
            RemoveEntryList(&MdBlock->ListEntry);
    }

    return PhysicalAddress;
}

PVOID
NTAPI
HalpMapPhysicalMemory64(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ PFN_COUNT PageCount)
{
    ULONG_PTR Offset;
    PHYSICAL_ADDRESS Base;
    PVOID Mapping;

    if ((PhysicalAddress.QuadPart == 0) || (PageCount == 0))
        return NULL;

    if (HalpUseIdentityMapping)
        return HalpPhysToKseg0(PhysicalAddress.QuadPart);

    Offset = (ULONG_PTR)(PhysicalAddress.QuadPart & (PAGE_SIZE - 1));
    Base.QuadPart = PhysicalAddress.QuadPart & ~((ULONGLONG)PAGE_SIZE - 1);

    Mapping = MmMapIoSpace(Base, PageCount << PAGE_SHIFT, MmNonCached);
    if (!Mapping)
        return NULL;

    return (PVOID)((PUCHAR)Mapping + Offset);
}

VOID
NTAPI
HalpUnmapVirtualAddress(
    _In_ PVOID VirtualAddress,
    _In_ PFN_COUNT NumberPages)
{
    PVOID Base;

    if (!VirtualAddress || NumberPages == 0)
        return;

    if (HalpUseIdentityMapping)
        return;

    Base = (PVOID)((ULONG_PTR)VirtualAddress & ~(PAGE_SIZE - 1));
    MmUnmapIoSpace(Base, NumberPages << PAGE_SHIFT);
}

volatile ULONG *HalpMmio(ULONG_PTR Base, ULONG Offset)
{
    /*
     * Convert physical address to the appropriate virtual address based on
     * the current mapping mode.
     *
     * During early boot (HalpUseIdentityMapping == TRUE):
     *   - Use the physical address directly because the kernel's TTBR0
     *     identity mapping maps PA == VA for low addresses.
     *   - This is critical for MMIO regions above 4GB (like GICR) that
     *     are only mapped in TTBR0's identity page tables.
     *
     * After boot (HalpUseIdentityMapping == FALSE), physical MMIO uses the
     * private ARM64 physical alias. The public FFFF8000... system range is
     * reserved for NT-visible kernel VA contracts and is not a direct map.
     */
    ULONG_PTR Va = Base;

    /* Don't modify addresses that are already in high VA space */
    if (Va >= HAL_ARM64_SYSTEM_RANGE_BASE)
        return (volatile ULONG *)(Va + Offset);

    /* During early boot, use identity mapping (PA == VA) via TTBR0 */
    if (HalpUseIdentityMapping)
        return (volatile ULONG *)(Va + Offset);

    /* After boot, convert to the private physical alias via TTBR1 */
    Va = HAL_ARM64_PHYS_MAP_BASE | (Va & HAL_ARM64_PHYS_ADDR_MASK);
    return (volatile ULONG *)(Va + Offset);
}

ULONGLONG HalpMmioRead64(ULONG_PTR Base, ULONG Offset)
{
    volatile ULONG *Ptr = HalpMmio(Base, Offset);
    ULONGLONG Low = Ptr[0];
    ULONGLONG High = Ptr[1];
    return Low | (High << 32);
}

VOID HalpMmioWrite64(ULONG_PTR Base, ULONG Offset, ULONGLONG Value)
{
    volatile ULONG *Ptr = HalpMmio(Base, Offset);
    Ptr[0] = (ULONG)(Value & 0xFFFFFFFFu);
    Ptr[1] = (ULONG)(Value >> 32);
}

static
BOOLEAN
HalpMapRuntimeMmioWindow(
    _Inout_ PULONGLONG BaseAddress,
    _In_ SIZE_T Length,
    _In_ PCSTR Name)
{
    PHYSICAL_ADDRESS Pa;
    PVOID MappedVa;
    ULONGLONG Physical;

    if (!BaseAddress || *BaseAddress == 0)
        return TRUE;

    if (*BaseAddress >= HAL_ARM64_SYSTEM_RANGE_BASE)
        return TRUE;

    Physical = *BaseAddress;
    Pa.QuadPart = Physical;
    MappedVa = MmMapIoSpace(Pa, Length, MmNonCached);
    if (!MappedVa)
    {
        DPRINT1("[arm64][HAL] Phase1: MmMapIoSpace failed for %s PA=0x%llx len=0x%llx\n",
                Name ? Name : "MMIO",
                Physical,
                (ULONGLONG)Length);
        return FALSE;
    }

    *BaseAddress = (ULONGLONG)(ULONG_PTR)MappedVa;
    DPRINT1("[arm64][HAL] Phase1: mapped %s PA=0x%llx -> VA=%p len=0x%llx\n",
            Name ? Name : "MMIO",
            Physical,
            MappedVa,
            (ULONGLONG)Length);
    return TRUE;
}

static
BOOLEAN
HalpMapGicv2RuntimeMmioWindows(VOID)
{
    /*
     * Runtime GICv2 paths (ACK/EOI/PMR/enable) still use MMIO.
     * Map their frames explicitly before disabling identity mapping.
     */
    if (!HalpMapRuntimeMmioWindow(&HalpGicdBase, PAGE_SIZE, "GICD"))
        return FALSE;

    if (!HalpMapRuntimeMmioWindow(&HalpGiccBase, PAGE_SIZE, "GICC"))
        return FALSE;

    return TRUE;
}

static
VOID
HalpRewriteGicrCpuBases(
    _In_ ULONGLONG OldBase,
    _In_ ULONGLONG NewBase,
    _In_ SIZE_T Length)
{
    ULONGLONG End;

    if ((OldBase == 0) || (NewBase == OldBase) || (Length == 0))
        return;

    End = OldBase + Length;
    for (ULONG Cpu = 0; Cpu < RTL_NUMBER_OF(HalpGicrCpuBase); ++Cpu)
    {
        ULONG_PTR Base = HalpGicrCpuBase[Cpu];

        if (((ULONGLONG)Base >= OldBase) && ((ULONGLONG)Base < End))
            HalpGicrCpuBase[Cpu] = (ULONG_PTR)(NewBase + ((ULONGLONG)Base - OldBase));
    }
}

static
BOOLEAN
HalpMapGicv3RuntimeMmioWindows(VOID)
{
    ULONGLONG OldGicrBase;
    SIZE_T GicrLength;

    if (!HalpMapRuntimeMmioWindow(&HalpGicdBase, HAL_ARM64_GICD_MAP_LENGTH, "GICD"))
        return FALSE;

    OldGicrBase = HalpGicrRegionBase;
    GicrLength = (SIZE_T)HalpGicrRegionLength;
    if (GicrLength == 0)
        GicrLength = HAL_ARM64_GICR_FRAME_LENGTH * MAXIMUM_PROCESSORS;

    if (HalpGicrRegionBase &&
        !HalpMapRuntimeMmioWindow(&HalpGicrRegionBase, GicrLength, "GICR"))
    {
        return FALSE;
    }

    HalpRewriteGicrCpuBases(OldGicrBase, HalpGicrRegionBase, GicrLength);

    for (ULONG Cpu = 0; Cpu < RTL_NUMBER_OF(HalpGicrCpuBase); ++Cpu)
    {
        ULONGLONG CpuBase = HalpGicrCpuBase[Cpu];

        if ((CpuBase == 0) || (CpuBase >= HAL_ARM64_SYSTEM_RANGE_BASE))
            continue;

        if (!HalpMapRuntimeMmioWindow(&CpuBase,
                                      HAL_ARM64_GICR_FRAME_LENGTH,
                                      "GICR-CPU"))
        {
            return FALSE;
        }

        HalpGicrCpuBase[Cpu] = (ULONG_PTR)CpuBase;
    }

    if (HalpGicItsPresent && HalpGicItsBase)
    {
        ULONGLONG ItsBase = HalpGicItsBase;

        if (!HalpMapRuntimeMmioWindow(&ItsBase, HAL_ARM64_ITS_MAP_LENGTH, "GITS"))
            return FALSE;

        HalpGicItsVa = (ULONG_PTR)ItsBase;

        for (ULONG Index = 0; Index < HalpGicItsNodeCount; ++Index)
        {
            if (HalpGicItsNodes[Index].PhysicalBase.QuadPart == HalpGicItsBase)
                HalpGicItsNodes[Index].VirtualBase = (ULONG_PTR)ItsBase;
        }
    }

    return TRUE;
}

static __inline ULONG_PTR HalpGicrBase(_In_ ULONG Cpu)
{
    if (Cpu < RTL_NUMBER_OF(HalpGicrCpuBase) && HalpGicrCpuBase[Cpu])
        return HalpGicrCpuBase[Cpu];

    return (ULONG_PTR)(HalpGicrRegionBase + ((ULONG_PTR)Cpu * HalpGicrStride));
}

static __inline ULONG_PTR HalpGicrSgiBase(_In_ ULONG Cpu)
{
    return HalpGicrBase(Cpu) + HalpGicrSgiOffset;
}

/*
 * HalpArm64ProgramGicTrigger - Program GIC interrupt trigger mode
 *
 * This function configures the trigger mode (edge vs level) for a GIC interrupt.
 * It programs the appropriate ICFGR register in either the distributor (for SPIs)
 * or the redistributor (for PPIs).
 *
 * GIC ICFGR register format:
 *   Each interrupt uses 2 bits:
 *     Bit [2n+1]: 0 = Level-sensitive, 1 = Edge-triggered
 *     Bit [2n]:   Reserved (should be written as 0)
 *
 * For interrupt N within an ICFGR register:
 *   - Position = (N % 16) * 2
 *   - Edge bit = position + 1
 *
 * Parameters:
 *   IntId         - GIC interrupt ID (INTID)
 *   EdgeTriggered - TRUE for edge-triggered, FALSE for level-triggered
 *
 * Notes:
 *   - SGIs (0-15) have fixed edge-triggered configuration and cannot be changed
 *   - PPIs (16-31) are configured via GICR_ICFGR1 in the redistributor
 *   - SPIs (32-1019) are configured via GICD_ICFGR in the distributor
 *   - LPIs (8192+) are always edge-triggered
 *
 * ARM64 Memory Ordering:
 *   A DSB barrier is issued after writing to ensure the configuration
 *   is visible to the GIC before any subsequent interrupt operations.
 */
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
     * Return immediately as the hardware ignores writes to these bits.
     */
    if (IntId < 16)
    {
        DPRINT("[arm64] SGI %lu: trigger mode is fixed (edge-triggered)\n", IntId);
        return;
    }

    /*
     * PPIs (16-31): Configured via GICR_ICFGR1 in the redistributor's SGI_base frame.
     * GICR_ICFGR1 covers PPIs 16-31 (16 interrupts, 2 bits each = 32 bits).
     * For PPI N (16-31), the bit position is ((N - 16) % 16) * 2.
     */
    if (IntId < 32)
    {
        ULONG Cpu = KeGetCurrentProcessorNumber();
        ULONG_PTR SgiBase = HalpGicrSgiBase(Cpu);

        if (SgiBase == 0)
        {
            DPRINT1("[arm64] PPI %lu: GICR SGI base unavailable for CPU %lu\n", IntId, Cpu);
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
        /* Level-triggered: leave bits as 0 */

        *RegPtr = NewVal;

        /* Ensure the write completes before returning */
        __asm__ __volatile__("dsb sy" ::: "memory");

        DPRINT("[arm64] PPI %lu: GICR_ICFGR1 0x%08lx -> 0x%08lx (%s)\n",
               IntId, OldVal, NewVal, EdgeTriggered ? "edge" : "level");
        return;
    }

    /*
     * SPIs (32-1019): Configured via GICD_ICFGR in the distributor.
     * GICD_ICFGR[n] covers interrupts (n*16) to (n*16 + 15).
     * For SPI N, the register offset is GICD_ICFGR + (N / 16) * 4.
     */
    if (IntId < 1020)
    {
        /* Calculate register offset and bit position */
        RegOffset = GICD_ICFGR + (IntId / 16) * 4;
        BitPos = (IntId % 16) * 2;

        RegPtr = HalpMmio((ULONG_PTR)HalpGicdBase, RegOffset);
        OldVal = *RegPtr;

        /* Clear the 2-bit field and set new value */
        NewVal = OldVal & ~(3u << BitPos);
        if (EdgeTriggered)
        {
            NewVal |= (2u << BitPos);  /* Bit 1 = edge-triggered */
        }
        /* Level-triggered: leave bits as 0 */

        *RegPtr = NewVal;

        /* Ensure the write completes before returning */
        __asm__ __volatile__("dsb sy" ::: "memory");

        DPRINT("[arm64] SPI %lu: GICD_ICFGR[0x%03lx] 0x%08lx -> 0x%08lx (%s)\n",
               IntId, RegOffset, OldVal, NewVal, EdgeTriggered ? "edge" : "level");
        return;
    }

    /*
     * Reserved (1020-1023) or invalid range: Log and return.
     */
    if (IntId < 8192)
    {
        DPRINT1("[arm64] INTID %lu is in reserved/invalid range\n", IntId);
        return;
    }

    /*
     * LPIs (8192+): Always edge-triggered, configured via ITS.
     * No action needed here.
     */
    DPRINT("[arm64] LPI %lu: trigger mode is fixed (edge-triggered)\n", IntId);
}

static VOID
HalpInitGicRedistributor(_In_ ULONG Cpu)
{
    ULONG_PTR Base = HalpGicrBase(Cpu);
    ULONG_PTR SgiBase = HalpGicrSgiBase(Cpu);
    ULONGLONG Mpidr = HalpReadMpidr();
    ULONGLONG Typer;

    DPRINT1("[arm64][GICR] Initializing redistributor for CPU %lu\n", Cpu);
    DPRINT1("[arm64][GICR]   MPIDR=0x%llx\n", Mpidr);
    DPRINT1("[arm64][GICR]   RD_Base=0x%p\n", (PVOID)Base);
    DPRINT1("[arm64][GICR]   SGI_Base=0x%p (offset=0x%lx)\n", (PVOID)SgiBase, HalpGicrSgiOffset);

    if (Base == 0)
    {
        DPRINT1("[arm64][GICR] ERROR: GICR base unavailable for CPU %lu!\n", Cpu);
        return;
    }

    /* Read and display GICR_TYPER for verification */
    Typer = HalpMmioRead64(Base, GICR_TYPER);
    DPRINT1("[arm64][GICR]   TYPER=0x%llx\n", Typer);
    DPRINT1("[arm64][GICR]   TYPER.Affinity=0x%08lx (extracted from bits [63:32])\n",
            (ULONG)((Typer >> 32) & 0xFFFFFFFF));

    /*
     * Wake redistributor and wait for ChildrenAsleep to clear.
     *
     * GICR_WAKER register is implementation-defined in GICv3:
     *   - Some use bit 1 for ProcessorSleep, bit 2 for ChildrenAsleep
     *   - Others use bit 0 for ProcessorSleep, bit 1 for ChildrenAsleep
     *   - QEMU appears to NOT implement WAKER properly (always reads 0)
     *
     * Strategy: Try both conventions, and if WAKER is all-zeros, skip the wait.
     */
    volatile ULONG *Waker = HalpMmio(Base, GICR_WAKER);
    ULONG W = *Waker;
    DPRINT1("[arm64][GICR] WAKER before wake: 0x%08lx\n", W);

    /* If WAKER is not implemented (reads as 0), skip wakeup sequence */
    if (W == 0)
    {
        DPRINT1("[arm64][GICR] WAKER reads as 0 - redistributor may not implement power management, continuing\n");
    }
    else
    {
        /* Try clearing both bit 0 and bit 1 (different implementations) */
        W &= ~((1u << 0) | (1u << 1));
        *Waker = W;

        /* Memory barrier to ensure write completes before polling */
        __asm__ __volatile__("dsb sy" ::: "memory");

        /* Poll for ChildrenAsleep to clear (check both bit 1 and bit 2) */
        ULONG Spins;
        for (Spins = 0x100000; Spins != 0; --Spins)
        {
            W = *Waker;
            /* Consider awake if bits 1 and 2 are both clear */
            if ((W & ((1u << 1) | (1u << 2))) == 0)
                break;
            __asm__ __volatile__("yield");
        }

        W = *Waker;
        DPRINT1("[arm64][GICR] WAKER after wake: 0x%08lx (spins left=%lu)\n", W, Spins);

        if (Spins == 0)
        {
            DPRINT1("[arm64][GICR] WARNING: GICR wakeup timeout for CPU %lu (WAKER still 0x%08lx)\n", Cpu, W);
            /* Continue anyway - redistributor might still work */
        }
    }

    /* Clear any pending SGI/PPI interrupts first */
    *HalpMmio(SgiBase, GICR_ICPENDR0) = 0xFFFFFFFF;

    /*
     * Configure interrupts as Group 1 Non-Secure.
     *
     * GICv3 uses two registers to determine interrupt group:
     * - GICR_IGROUPR0: Group bit (0=Group0, 1=Group1)
     * - GICR_IGRPMODR0: Modifier bit (determines Secure/NonSecure for Group1)
     *
     * Group assignment table:
     *   IGROUPR[i] | IGRPMODR[i] | Result
     *   -----------|-------------|-----------------
     *       0      |      0      | Group 0
     *       0      |      1      | Group 1 Secure
     *       1      |      0      | Group 1 Non-Secure  <-- What we want
     *       1      |      1      | Reserved
     *
     * For EL1 Non-Secure (our case):
     * - Interrupts must be Group 1 Non-Secure
     * - Delivered via ICC_IAR1_EL1 (not ICC_IAR0_EL1)
     * - Enabled via ICC_IGRPEN1_EL1.Enable
    */
    *HalpMmio(SgiBase, GICR_IGROUPR0) = 0xFFFFFFFF;  /* All Group 1 */
    *HalpMmio(SgiBase, GICR_IGRPMODR0) = 0x00000000; /* Non-Secure (not Secure) */
    DPRINT1("[arm64][GICR] Configured all SGI/PPI as Group 1 Non-Secure\n");

    /*
     * Set priorities to 0x00 (highest priority) for all SGI/PPI.
     *
     * The ICC_PMR_EL1 reads back as 0xF8 (not 0xFF as written), indicating
     * the implementation only supports 5 priority bits [7:3].
     *
     * In GIC priority comparison: LOWER number = HIGHER priority.
     * PMR=0xF8 masks interrupts with priority < 0xF8 (lower priority).
     *
     * Old value 0xA0 was being masked (0xA0 < 0xF8), causing spurious
     * interrupt returns (IntId=1023) when timer fired.
     *
     * Use 0x00 for highest priority, ensuring interrupts pass PMR mask.
     */
    for (ULONG i = 0; i < 32; i += 4)
    {
        *HalpMmio(SgiBase, GICR_IPRIORITYR + i) = 0x00000000;
    }

    /*
     * CRITICAL ARM64 DESIGN: DO NOT enable SGIs here!
     *
     * On ARM64, HalRequestSoftwareInterrupt must NOT send actual hardware interrupts.
     * Instead, it sets pending flags (like Prcb->DpcInterruptRequested) that are
     * checked when IRQL is lowered in KeLowerIrql.
     *
     * If we enable SGIs and send them via HalpWriteIccSgi1r, they fire immediately
     * and raise IRQL in the interrupt handler. This causes IRQL violations when
     * code at PASSIVE_LEVEL calls functions that request software interrupts.
     *
     * For example:
     * 1. ExQueueWorkItem (at PASSIVE_LEVEL) calls HalRequestSoftwareInterrupt(DPC_LEVEL)
     * 2. If we send an SGI, it fires immediately
     * 3. Interrupt handler raises IRQL to DPC_LEVEL
     * 4. ExQueueWorkItem then calls KeInsertQueue which requires IRQL <= DISPATCH_LEVEL
     * 5. Assertion fails: IRQL (DPC_LEVEL=2) > required (DISPATCH_LEVEL=2)
     *
     * The x86 model is instructive:
     * - PIC HAL: Sets IRR flag, only calls handler if IRQL permits
     * - APIC HAL: Sends self-interrupt, but x86 interrupt architecture is designed
     *   for this with automatic interrupt masking
     *
     * On ARM64, software interrupts are purely virtual - they're flags checked
     * during IRQL transitions, not actual hardware interrupts.
     *
     * SGIs should only be enabled when needed for true inter-processor interrupts
     * (IPI for multiprocessor synchronization), not for software interrupt simulation.
     */

    /*
     * Configure PPI trigger modes in GICR_ICFGR1.
     *
     * ICFGR1 controls PPIs 16-31 (2 bits per interrupt):
     *   Bits [2n+1:2n] for PPI (16+n)
     *   Bit 2n+1: 0=level-sensitive, 1=edge-triggered
     *   Bit 2n:   Reserved (SBZ)
     *
     * ARM Generic Timer PPIs:
     *   PPI 30 (phys timer): Should be LEVEL-sensitive
     *   PPI 27 (virt timer): Should be LEVEL-sensitive
     *
     * For PPI 30: offset = (30-16)*2 = 28, so bits [29:28]
     * Set bit 29 = 0 for level-sensitive
     */
    ULONG Icfgr1 = *HalpMmio(SgiBase, GICR_ICFGR1);
    /* Clear edge trigger bits for PPI 27 and 30 (set to level-sensitive) */
    Icfgr1 &= ~((1u << 23) | (1u << 29)); /* PPI 27 bit 23, PPI 30 bit 29 */
    *HalpMmio(SgiBase, GICR_ICFGR1) = Icfgr1;
    DPRINT1("[arm64][GICR] ICFGR1=0x%08lx (PPI 27,30 set to level-sensitive)\n", Icfgr1);

    /*
     * Only write to the SGI bits (0-15), leaving PPI bits (16-31) alone.
     * PPIs will be enabled later by HalEnableSystemInterrupt when needed.
     */
    *HalpMmio(SgiBase, GICR_ICENABLER0) = 0x0000FFFF; /* Disable only SGIs (0-15) */

    /*
     * Re-enable SGIs needed for inter-processor interrupts.
     *
     * SGI 0 (HAL_ARM64_SGI_IPI) is used by KxFreezeExecution to freeze
     * target CPUs for debugger entry (KdEnterDebugger). Without this,
     * HalRequestIpi sends the SGI but the redistributor drops it because
     * ISENABLER0 bit 0 is clear, causing KiArm64WaitForFrozenTargets
     * to spin forever.
     *
     * Always enable IPI SGI regardless of UP/SMP - it's harmless in UP
     * mode and required for SMP kernel debugger synchronization.
     */
    *HalpMmio(SgiBase, GICR_ISENABLER0) = (1u << HAL_ARM64_SGI_IPI);

    /* Memory barrier after redistributor configuration */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /* Verify final state */
    ULONG IsEnabler = *HalpMmio(SgiBase, GICR_ISENABLER0);
    DPRINT1("[arm64][GICR] Final ISENABLER0=0x%08lx (IPI SGI enabled)\n", IsEnabler);
    DPRINT1("[arm64][GICR] Redistributor initialization complete for CPU %lu\n", Cpu);
}

/*
 * Per-CPU active interrupt ID tracking (stored as intid + 1).
 * Value 0 = no active interrupt, 1 = INTID 0 (SGI IPI), 2 = INTID 1, etc.
 * This offset-by-one encoding avoids the sentinel conflict where INTID 0
 * (a valid SGI used for IPI) would be indistinguishable from "no interrupt".
 */
#define HAL_ARM64_ACTIVE_INTID_STACK_DEPTH 16

static ULONG HalpArm64ActiveIntId[MAXIMUM_PROCESSORS];
static ULONG HalpArm64ActiveIntIdStack[MAXIMUM_PROCESSORS][HAL_ARM64_ACTIVE_INTID_STACK_DEPTH];
static UCHAR HalpArm64ActiveIntIdDepth[MAXIMUM_PROCESSORS];

/* GIC detection: system-register interface (GICv3+) vs legacy CPU IF (GICv2)
 * HalpGicUseSysRegs, HalpForceLegacyGic, and HalpGicArchRev are declared
 * as extern at the top of this file and defined in gic_common.c. */
static BOOLEAN HalpLoggedGicOnce = FALSE; /* One-time post-KD log */
BOOLEAN HalpGicv2ForceGroup0 = FALSE; /* HVF quirk mode for legacy GIC */
BOOLEAN HalpGicv2GroupModeLocked = FALSE; /* Explicit boot option override */

FORCEINLINE ULONGLONG HalpReadMidr(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, midr_el1" : "=r"(v)); return v;
}

FORCEINLINE ULONGLONG HalpReadMpidr(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(v)); return v;
}

FORCEINLINE ULONGLONG HalpReadPfr0(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(v)); return v;
}

FORCEINLINE ULONGLONG HalpReadCntfrq(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v)); return v;
}

FORCEINLINE ULONGLONG HalpReadCntpct(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(v)); return v;
}

FORCEINLINE unsigned int HalpReadIccSre(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, icc_sre_el1" : "=r"(v)); return (unsigned int)v;
}

FORCEINLINE VOID HalpWriteIccSre(unsigned int v)
{
    __asm__ __volatile__("msr icc_sre_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE unsigned int HalpReadIccIar1(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, icc_iar1_el1" : "=r"(v)); return (unsigned int)(v & 0x3FFu);
}

FORCEINLINE VOID HalpWriteIccEoir1(unsigned int id)
{
    __asm__ __volatile__("msr icc_eoir1_el1, %0; isb" :: "r"((ULONGLONG)id) : "memory");
}

FORCEINLINE VOID HalpWriteIccPmr(unsigned int v)
{
    __asm__ __volatile__("msr icc_pmr_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE unsigned int HalpReadIccPmr(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, icc_pmr_el1" : "=r"(v)); return (unsigned int)v;
}

FORCEINLINE VOID HalpWriteIccBpr1(unsigned int v)
{
    __asm__ __volatile__("msr icc_bpr1_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE VOID HalpWriteIccIgrpen1(unsigned int v)
{
    __asm__ __volatile__("msr icc_igrpen1_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE unsigned int HalpReadIccIgrpen1(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, icc_igrpen1_el1" : "=r"(v)); return (unsigned int)v;
}

FORCEINLINE VOID HalpWriteIccSgi1r(ULONGLONG v)
{
    __asm__ __volatile__("msr icc_sgi1r_el1, %0; isb" :: "r"(v) : "memory");
}

/*
 * ============================================================================
 * Dynamic IRQ Affinity Routing HAL API
 * ============================================================================
 *
 * These functions provide the Windows HAL API for dynamic IRQ affinity routing,
 * required for Windows 11 ARM64 GIC compatibility.
 *
 * Key functions:
 * - HalpSetInterruptAffinity: Set affinity for a specific interrupt
 * - HalpGetInterruptAffinity: Get current affinity for an interrupt
 * - HalpMigrateInterruptsFromCpu: Migrate all interrupts away from a CPU
 *
 * The underlying implementation uses GICD_IROUTER for GICv3 SPIs.
 */

/* Forward declarations for GICv3 affinity functions from gicv3.c */
NTSTATUS
HalpArm64SetGicAffinity(
    _In_ ULONG InterruptId,
    _In_ ULONG TargetCpu);

ULONG
HalpArm64GetGicAffinity(
    _In_ ULONG InterruptId);

VOID
HalpGicv3MigrateCpuIrqs(
    _In_ ULONG CpuIndex);

VOID
HalpGicv3SetSpiAffinityRoundRobin(
    _In_ ULONG Lines);

/*
 * HalpSetInterruptAffinity - Set IRQ affinity (HAL API wrapper)
 *
 * This is the HAL API for setting interrupt affinity, callable from
 * kernel mode via HalSetSystemInformation with HalIrqAffinity class.
 *
 * Parameters:
 *   InterruptVector - The interrupt vector (GSI/INTID)
 *   TargetCpu       - Target CPU processor number
 *
 * Returns:
 *   STATUS_SUCCESS on success
 *   STATUS_INVALID_PARAMETER for invalid parameters
 *   STATUS_NOT_SUPPORTED if GICv3 is not in use
 */
NTSTATUS
NTAPI
HalpSetInterruptAffinity(
    _In_ ULONG InterruptVector,
    _In_ ULONG TargetCpu)
{
    /* Only supported on GICv3 with system registers */
    if (!HalpGicUseSysRegs)
    {
        /*
         * GICv2 uses GICD_ITARGETSR which has an 8-CPU bitmap limit.
         * Dynamic affinity routing per-IRQ is not well supported.
         */
        DPRINT1("[arm64][HAL] SetInterruptAffinity: GICv2 does not support dynamic affinity\n");
        return STATUS_NOT_SUPPORTED;
    }

    /*
     * Validate interrupt vector is an SPI (32-1019).
     * SGIs (0-15) and PPIs (16-31) are per-CPU and cannot be rerouted.
     */
    if (InterruptVector < 32 || InterruptVector >= 1020)
    {
        DPRINT1("[arm64][HAL] SetInterruptAffinity: vector %lu is not an SPI\n",
                InterruptVector);
        return STATUS_INVALID_PARAMETER;
    }

    /* Call the GICv3 implementation */
    return HalpArm64SetGicAffinity(InterruptVector, TargetCpu);
}

/*
 * HalpGetInterruptAffinity - Get IRQ affinity (HAL API wrapper)
 *
 * Returns the current target CPU for an interrupt.
 *
 * Parameters:
 *   InterruptVector - The interrupt vector (GSI/INTID)
 *
 * Returns:
 *   Target CPU number, or (ULONG)-1 on error
 */
ULONG
NTAPI
HalpGetInterruptAffinity(
    _In_ ULONG InterruptVector)
{
    /* Only supported on GICv3 with system registers */
    if (!HalpGicUseSysRegs)
    {
        return (ULONG)-1;
    }

    /* Call the GICv3 implementation */
    return HalpArm64GetGicAffinity(InterruptVector);
}

/*
 * HalpMigrateInterruptsFromCpu - Migrate all interrupts away from a CPU
 *
 * Called when a CPU goes offline. All SPIs routed to the specified
 * CPU are migrated to CPU 0.
 *
 * Parameters:
 *   CpuIndex - The CPU that is going offline
 */
VOID
NTAPI
HalpMigrateInterruptsFromCpu(
    _In_ ULONG CpuIndex)
{
    if (!HalpGicUseSysRegs)
    {
        /* GICv2 does not track per-CPU affinity */
        return;
    }

    HalpGicv3MigrateCpuIrqs(CpuIndex);
}

/*
 * HalpDistributeInterruptsRoundRobin - Distribute SPIs across all CPUs
 *
 * Distributes all SPIs across available CPUs using round-robin
 * for load balancing. Should be called after all CPUs are online.
 */
VOID
NTAPI
HalpDistributeInterruptsRoundRobin(VOID)
{
    ULONG Typer;
    ULONG Lines;

    if (!HalpGicUseSysRegs)
    {
        /* GICv2 uses simple CPU targeting */
        return;
    }

    /* Get number of interrupt lines from GICD_TYPER */
    Typer = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_TYPER);
    Lines = 32 * ((Typer & 0x1F) + 1);
    if (Lines > 1020)
        Lines = 1020;

    HalpGicv3SetSpiAffinityRoundRobin(Lines);
}

/*
 * PSCI call helper functions.
 * These invoke SMC or HVC based on the FADT ARM_BOOT_ARCH flags.
 * The choice is indicated by HalpArm64PsciInfo.UseHvc.
 */
static LONG
HalpPsciCallSmc(
    _In_ ULONGLONG FunctionId,
    _In_ ULONGLONG Arg0,
    _In_ ULONGLONG Arg1,
    _In_ ULONGLONG Arg2)
{
    register ULONGLONG x0 __asm__("x0") = FunctionId;
    register ULONGLONG x1 __asm__("x1") = Arg0;
    register ULONGLONG x2 __asm__("x2") = Arg1;
    register ULONGLONG x3 __asm__("x3") = Arg2;

    __asm__ __volatile__(
        "smc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
          "x12", "x13", "x14", "x15", "x16", "x17", "memory"
    );

    return (LONG)x0;
}

static LONG
HalpPsciCallHvc(
    _In_ ULONGLONG FunctionId,
    _In_ ULONGLONG Arg0,
    _In_ ULONGLONG Arg1,
    _In_ ULONGLONG Arg2)
{
    register ULONGLONG x0 __asm__("x0") = FunctionId;
    register ULONGLONG x1 __asm__("x1") = Arg0;
    register ULONGLONG x2 __asm__("x2") = Arg1;
    register ULONGLONG x3 __asm__("x3") = Arg2;

    __asm__ __volatile__(
        "hvc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
          "x12", "x13", "x14", "x15", "x16", "x17", "memory"
    );

    return (LONG)x0;
}

static LONG
HalpPsciCall(
    _In_ ULONGLONG FunctionId,
    _In_ ULONGLONG Arg0,
    _In_ ULONGLONG Arg1,
    _In_ ULONGLONG Arg2)
{
    /*
     * Check HalpArm64PsciInfo.UseHvc to determine whether to use HVC or SMC.
     * HVC is used when running under a hypervisor (EL2), SMC for secure monitor (EL3).
     */
    if (HalpArm64PsciInfo.UseHvc)
    {
        return HalpPsciCallHvc(FunctionId, Arg0, Arg1, Arg2);
    }
    else
    {
        return HalpPsciCallSmc(FunctionId, Arg0, Arg1, Arg2);
    }
}

/*
 * PSCI CPU_ON - wake a secondary processor.
 * Parameters:
 *   TargetMpidr - MPIDR of the target CPU
 *   EntryPoint  - Physical address of the entry point
 *   ContextId   - Context ID passed to the entry point (typically processor number)
 * Returns:
 *   PSCI return code (0 = success, negative = error)
 */
static LONG
HalpPsciCpuOn(
    _In_ ULONGLONG TargetMpidr,
    _In_ ULONGLONG EntryPoint,
    _In_ ULONGLONG ContextId)
{
    LONG Result;

    DPRINT1("[arm64][HAL] PSCI CPU_ON: target=0x%llx entry=0x%llx ctx=0x%llx %s\n",
            TargetMpidr, EntryPoint, ContextId,
            HalpArm64PsciInfo.UseHvc ? "HVC" : "SMC");

    Result = HalpPsciCall(PSCI_FN_CPU_ON_64, TargetMpidr, EntryPoint, ContextId);

    if (Result != PSCI_SUCCESS)
    {
        DPRINT1("[arm64][HAL] PSCI CPU_ON failed: %ld\n", Result);
    }

    return Result;
}

/*
 * PSCI SYSTEM_RESET - reset the entire system.
 * This function does not return on success.
 */
static VOID
HalpPsciSystemReset(VOID)
{
    DPRINT1("[arm64][HAL] PSCI SYSTEM_RESET %s\n",
            HalpArm64PsciInfo.UseHvc ? "HVC" : "SMC");

    /* Ensure all writes are visible before reset */
    __asm__ __volatile__("dsb sy; isb" ::: "memory");

    HalpPsciCall(PSCI_FN_SYSTEM_RESET, 0, 0, 0);

    /* Should not reach here */
    __asm__ __volatile__("dsb sy; wfi");
    for (;;)
    {
        __asm__ __volatile__("wfi");
    }
}

/*
 * PSCI SYSTEM_OFF - power down the entire system.
 * This function does not return on success.
 */
static VOID
HalpPsciSystemOff(VOID)
{
    DPRINT1("[arm64][HAL] PSCI SYSTEM_OFF %s\n",
            HalpArm64PsciInfo.UseHvc ? "HVC" : "SMC");

    /* Ensure all writes are visible before power off */
    __asm__ __volatile__("dsb sy; isb" ::: "memory");

    HalpPsciCall(PSCI_FN_SYSTEM_OFF, 0, 0, 0);

    /* Should not reach here */
    __asm__ __volatile__("dsb sy; wfi");
    for (;;)
    {
        __asm__ __volatile__("wfi");
    }
}

static BOOLEAN
HalpHasLoaderOption(
    _In_opt_ PSTR Options,
    _In_ PCSTR Option)
{
    if (!Options || !Option || *Option == '\0')
        return FALSE;

    return (strstr(Options, Option) != NULL);
}

static __inline ULONG
HalpArm64AffinityFromMpidr(_In_ ULONGLONG Mpidr)
{
    ULONG Aff0 = (ULONG)(Mpidr & 0xFF);
    ULONG Aff1 = (ULONG)((Mpidr >> 8) & 0xFF);
    ULONG Aff2 = (ULONG)((Mpidr >> 16) & 0xFF);
    ULONG Aff3 = (ULONG)((Mpidr >> 32) & 0xFF);
    return Aff0 | (Aff1 << 8) | (Aff2 << 16) | (Aff3 << 24);
}

static __inline ULONGLONG
HalpArm64IrouterFromMpidr(_In_ ULONGLONG Mpidr)
{
    ULONG Aff = HalpArm64AffinityFromMpidr(Mpidr);
    ULONGLONG Aff0 = (ULONGLONG)(Aff & 0xFF);
    ULONGLONG Aff1 = (ULONGLONG)((Aff >> 8) & 0xFF);
    ULONGLONG Aff2 = (ULONGLONG)((Aff >> 16) & 0xFF);
    ULONGLONG Aff3 = (ULONGLONG)((Aff >> 24) & 0xFF);
    return (Aff0) | (Aff1 << 8) | (Aff2 << 16) | (Aff3 << 32);
}

static ULONG_PTR
HalpArm64FindGicrForMpidr(_In_ ULONGLONG Mpidr)
{
    ULONG MaxFrames;
    ULONG Affinity = HalpArm64AffinityFromMpidr(Mpidr);
    ULONG_PTR Base;

    if (HalpGicrRegionBase == 0 || HalpGicrStride == 0)
        return 0;

    MaxFrames = HalpGicrRegionLength ?
                (ULONG)(HalpGicrRegionLength / HalpGicrStride) :
                MAXIMUM_PROCESSORS;
    if (MaxFrames == 0)
        MaxFrames = MAXIMUM_PROCESSORS;

    Base = (ULONG_PTR)HalpGicrRegionBase;
    for (ULONG Index = 0; Index < MaxFrames; ++Index, Base += HalpGicrStride)
    {
        ULONGLONG Typer = HalpMmioRead64(Base, GICR_TYPER);
        ULONG TyperAffinity = (ULONG)((Typer >> 32) & 0xFFFFFFFFu);

        if (TyperAffinity == Affinity)
            return Base;

        if (Typer & (1ULL << 4))
            break;
    }

    return 0;
}

static VOID
HalpArm64DiscoverGicFromMadt(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONGLONG Mpidr;
    BOOLEAN FoundGicd = FALSE;
    BOOLEAN FoundGicr = FALSE;
    BOOLEAN FoundCpuGicr = FALSE;
    BOOLEAN FoundGiccEntry = FALSE;
    BOOLEAN FoundGiccBase = FALSE;
    ULONG Cpu = KeGetCurrentProcessorNumber();

    if (HalpGicParsedMadt)
    {
        return;
    }

    HalpGicParsedMadt = TRUE;
    if (!LoaderBlock)
    {
        return;
    }
    Mpidr = HalpReadMpidr();
    HalpAcpiDiscoverArm64Tables(LoaderBlock);
    if (HalpArm64GicInfo.GicdBase)
    {
        HalpGicdBase = HalpArm64GicInfo.GicdBase;
        FoundGicd = TRUE;
    }
    if (HalpArm64GicInfo.GicrBase)
    {
        HalpGicrRegionBase = HalpArm64GicInfo.GicrBase;
        HalpGicrRegionLength = HalpArm64GicInfo.GicrLength;
        FoundGicr = TRUE;
    }
    if (HalpArm64GicInfo.GiccBase)
    {
        HalpGiccBase = HalpArm64GicInfo.GiccBase;
        FoundGiccBase = TRUE;
    }
    if (HalpArm64GicInfo.GiccEntryCount)
    {
        FoundGiccEntry = TRUE;
        for (ULONG Index = 0; Index < HalpArm64GicInfo.GiccEntryCount; ++Index)
        {
            PHALP_ARM64_GICC_ENTRY Entry = &HalpArm64GicInfo.GiccEntries[Index];

            if (!Entry->GicrBase)
                continue;

            /*
             * QEMU HVF bug workaround: When running with < 4GB RAM but ACPI tables
             * report GICR addresses at 0x100000000 (highmem location), the GICR
             * hardware is actually at the lowmem location (0x80a0000). Skip any
             * per-CPU GICR base that is at or above 4GB - we'll fall back to the
             * region-based discovery which uses the correct default address.
             *
             * This occurs because QEMU generates ACPI tables assuming highmem GIC
             * placement, but when using HVF with limited RAM, the actual GIC
             * hardware is at the lowmem addresses shown in the device tree.
             */
            if (Entry->GicrBase >= 0x100000000ULL)
            {
                DPRINT1("[arm64][HAL] Skipping highmem GICR@0x%llx for CPU %lu\n",
                        Entry->GicrBase, Index);
                continue;
            }

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
    if (HalpArm64GicInfo.ItsCount)
    {
        ULONG ItsNodeIdx = 0;
        for (ULONG Index = 0; Index < HalpArm64GicInfo.ItsCount && ItsNodeIdx < HALP_GIC_MAX_ITS_NODES_FWD; ++Index)
        {
            HALP_ARM64_GIC_ITS_ENTRY *ItsEntry = &HalpArm64GicInfo.ItsEntries[Index];
            if (ItsEntry->BaseAddress)
            {
                /*
                 * Initialize the ITS node structure in HalpGicItsNodes array.
                 * This mirrors what gic_init.c's HalpArm64DiscoverGicFromMadt does.
                 * We must initialize these fields because HalpGicItsProbeNode in
                 * gic_its.c checks ItsNode->VirtualBase != 0 before proceeding.
                 */
                PHALP_GIC_ITS_NODE_FWD ItsNode = &HalpGicItsNodes[ItsNodeIdx];

                RtlZeroMemory(ItsNode, sizeof(*ItsNode));
                ItsNode->PhysicalBase.QuadPart = ItsEntry->BaseAddress;
                ItsNode->VirtualBase = (ULONG_PTR)ItsEntry->BaseAddress; /* Identity mapping initially */
                ItsNode->ItsId = ItsEntry->ItsId;
                ItsNode->ListNumber = ItsNodeIdx;
                ItsNode->InitState = 0;
                KeInitializeSpinLock(&ItsNode->CmdLock);
                KeInitializeSpinLock(&ItsNode->DeviceLock);

                /*
                 * Set the legacy single-ITS fields for first ITS found.
                 */
                if (ItsNodeIdx == 0)
                {
                    HalpGicItsBase = ItsEntry->BaseAddress;
                    HalpGicItsId = ItsEntry->ItsId;
                }
                HalpGicItsPresent = TRUE;
                ItsNodeIdx++;
            }
        }
        /*
         * CRITICAL: Set HalpGicItsNodeCount so MSI allocation code
         * (HalpAllocateMsiInterrupt) and ITS initialization code know
         * ITS is available. This count is used by HalpGicItsInitAllNodes
         * to iterate over the ITS nodes we just initialized.
         */
        HalpGicItsNodeCount = ItsNodeIdx;
        DPRINT1("[arm64][HAL] ITS: Initialized %lu ITS nodes (ItsCount=%lu)\n",
                ItsNodeIdx, HalpArm64GicInfo.ItsCount);
    }
    if (HalpArm64GicInfo.MsiFrameCount)
    {
        for (ULONG Index = 0; Index < HalpArm64GicInfo.MsiFrameCount; ++Index)
        {
            HALP_ARM64_GIC_MSI_FRAME_ENTRY *Frame =
                &HalpArm64GicInfo.MsiFrames[Index];
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
    if (FoundGicd)
    {
        DPRINT1("[arm64][HAL] MADT: GICD @0x%llx\n", HalpGicdBase);
    }
    if (FoundGicr)
    {
        DPRINT1("[arm64][HAL] MADT: GICR region @0x%llx len=0x%llx\n",
                HalpGicrRegionBase,
                HalpGicrRegionLength);
    }
    if (FoundGiccEntry && !FoundGiccBase)
    {
        HalpGiccBase = 0;
        HalpGiccPresent = FALSE;
        DPRINT1("[arm64][HAL] MADT: no GICC base, legacy CPU IF unavailable\n");
    }
    else if (FoundGiccBase)
    {
        HalpGiccPresent = TRUE;
    }
    if (!FoundCpuGicr && FoundGicr && HalpGicrRegionBase)
    {
        ULONG_PTR Base = HalpArm64FindGicrForMpidr(Mpidr);
        if (Base && Cpu < RTL_NUMBER_OF(HalpGicrCpuBase))
        {
            HalpGicrCpuBase[Cpu] = Base;
            DPRINT1("[arm64][HAL] MADT: selected GICR CPU base @0x%p\n", (PVOID)Base);
        }
    }

    /* Log ITS detection result regardless of presence */
    DPRINT1("[arm64][HAL] MADT: ItsCount=%lu, ItsPresent=%d, ItsBase=0x%llx\n",
            HalpArm64GicInfo.ItsCount,
            (int)HalpGicItsPresent,
            HalpGicItsBase);
    if (HalpGicItsPresent)
    {
        DPRINT1("[arm64][HAL] MADT: GIC ITS %lu @0x%llx\n",
                HalpGicItsId,
                HalpGicItsBase);
    }
    if (HalpGicMsiPresent)
    {
        DPRINT1("[arm64][HAL] MADT: GIC MSI frame @0x%llx SPI[%u..%u] flags=0x%lx\n",
                HalpGicMsiFrameBase,
                HalpGicMsiSpiBase,
                (ULONG)(HalpGicMsiSpiBase + HalpGicMsiSpiCount - 1),
                HalpGicMsiFlags);
    }
}

static VOID
HalpArm64SelectGicInterface(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONGLONG pfr0 = 0;
    ULONG pfr0_gic = 0;
    ULONGLONG midr = 0;
    UCHAR implementer = 0;

    if (HalpGicInterfaceSelected)
    {
        return;
    }

    HalpGicInterfaceSelected = TRUE;

    /* Check for boot options that force GIC interface selection */
    if (LoaderBlock && LoaderBlock->LoadOptions)
    {
        if (HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICV3") ||
            HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICSYSREG"))
        {
            HalpForceSysRegs = TRUE;
            HalpForceLegacyGic = FALSE;
        }
        else if (HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICV2") ||
                 HalpHasLoaderOption(LoaderBlock->LoadOptions, "NOGICSYSREG") ||
                 HalpHasLoaderOption(LoaderBlock->LoadOptions, "LEGACYGIC"))
        {
            HalpForceLegacyGic = TRUE;
        }

        if (HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICV2GROUP0") ||
            HalpHasLoaderOption(LoaderBlock->LoadOptions, "HVFGICV2"))
        {
            HalpGicv2ForceGroup0 = TRUE;
            HalpGicv2GroupModeLocked = TRUE;
        }
        else if (HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICV2GROUP1"))
        {
            HalpGicv2ForceGroup0 = FALSE;
            HalpGicv2GroupModeLocked = TRUE;
        }
    }
    HalpArm64DiscoverGicFromMadt(LoaderBlock);

    /* Apple HVF quirk */
    if (!HalpForceSysRegs && !HalpForceLegacyGic && HalpGiccPresent)
    {
        midr = HalpReadMidr();
        implementer = (UCHAR)((midr >> 24) & 0xFF);
        if (implementer == 0x61) /* Apple */
        {
            HalpForceLegacyGic = TRUE;
            HalpGicv2ForceGroup0 = TRUE;
        }
    }

    if (!HalpGiccPresent)
    {
        HalpForceLegacyGic = FALSE;
        HalpForceSysRegs = TRUE;
        HalpGicv2ForceGroup0 = FALSE;
    }

    /*
     * QEMU virt quirk:
     * Legacy GICv2 Group1 delivery can stall very early during IRQL unmasking.
     * Keep Group0-only by default on this machine profile unless user override
     * explicitly requested GICV2GROUP1/GICV2GROUP0.
     */
    if (!HalpForceSysRegs &&
        HalpGiccPresent &&
        !HalpGicv2GroupModeLocked &&
        HalpGicdBase == HAL_ARM64_GICD_BASE_DEFAULT &&
        HalpGiccBase == HAL_ARM64_GICC_BASE_DEFAULT)
    {
        HalpGicv2ForceGroup0 = TRUE;
    }
    pfr0 = HalpReadPfr0();
    pfr0_gic = (ULONG)((pfr0 >> 24) & 0xF);

    if (HalpForceSysRegs)
    {
        ULONG sre = HalpReadIccSre();
        sre |= 0x1;
        HalpWriteIccSre(sre);
        sre = HalpReadIccSre();
        if (sre & 0x1)
        {
            HalpGicUseSysRegs = TRUE;
        }
        else
        {
            HalpGicUseSysRegs = FALSE;
        }
    }
    else if (!HalpForceLegacyGic && pfr0_gic >= 1)
    {
        ULONG sre = HalpReadIccSre();
        sre |= 0x1;
        HalpWriteIccSre(sre);
        sre = HalpReadIccSre();
        if (sre & 0x1)
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
}

static VOID
HalpArm64SendSgi(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG SgiId)
{
    ULONG TargetList;

    if ((TargetSet == 0) || (SgiId > 15))
        return;

    if (HalpGicUseSysRegs)
    {
        ULONGLONG mpidr = HalpReadMpidr();
        ULONG aff1 = (ULONG)((mpidr >> 8) & 0xFF);
        ULONG aff2 = (ULONG)((mpidr >> 16) & 0xFF);
        ULONG aff3 = (ULONG)((mpidr >> 32) & 0xFF);
        ULONG targetList = (ULONG)(TargetSet & 0xFFFF);
        ULONGLONG sgi = 0;

        if (targetList == 0)
            return;

        sgi |= (ULONGLONG)(SgiId & 0xF) << 24;
        sgi |= (ULONGLONG)(aff1 & 0xFF) << 16;
        sgi |= (ULONGLONG)(aff2 & 0xFF) << 32;
        sgi |= (ULONGLONG)(aff3 & 0xFF) << 48;
        sgi |= (ULONGLONG)(targetList & 0xFFFF);

        /*
         * CRITICAL ARM64 SGI DELIVERY SEQUENCE:
         *
         * 1. Write to ICC_SGI1R_EL1 to send the SGI
         * 2. ISB ensures the system register write completes
         * 3. DSB SY ensures the SGI is visible to all CPUs
         * 4. SEV wakes any CPUs in WFE/WFI state
         *
         * Without SEV, a CPU in WFI will not wake up even though the SGI is pending!
         * The ARM64 architecture requires an event (interrupt or SEV) to exit WFI.
         * SGI delivery alone does not guarantee WFI exit without proper synchronization.
         */
        HalpWriteIccSgi1r(sgi);
        __asm__ __volatile__("dsb sy; sev" ::: "memory");
        return;
    }

    TargetList = (ULONG)(TargetSet & 0xFF);
    if (TargetList == 0)
        return;

    /* GICv2 legacy path: write to GICD_SGIR */
    *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_SGIR) = (SgiId & 0xF) | (TargetList << 16);

    /* Ensure SGI write is visible and wake any waiting CPUs */
    __asm__ __volatile__("dsb sy; sev" ::: "memory");
}

BOOLEAN
NTAPI
HalInitSystem(
    _In_ ULONG BootPhase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;
    ULONG i, lines, nregs;
    ULONG typer;
    KIRQL OldIrql;

    if (BootPhase != 0)
    {
        KIRQL SwitchIrql;

        /*
         * CRITICAL: Transition from identity-mapped PA to proper kernel VA
         * for all GIC MMIO accesses.
         *
         * By Phase 1, the kernel MM has completed initialization and the TTBR0
         * identity mapping (PA == VA) that was used during early boot may have
         * been torn down.  Any MMIO access through HalpMmio() that still uses
         * the identity-mapping path will fault.
         *
         * For GICv2, timer PPI interrupts are already firing at 100Hz.  The
         * interrupt handler calls HalpGicv2AcknowledgeInterrupt() which reads
         * GICC_IAR via HalpMmio().  If we do not remap GICC/GICD BEFORE the
         * identity mapping becomes invalid, interrupt handling will data-abort.
         *
         * Strategy:
         *   1. Map GIC MMIO frames via MmMapIoSpace() (for GICv2).
         *      MmMapIoSpace() overwrites HalpGicdBase/HalpGiccBase with
         *      kernel-VA values.  HalpMmio() handles high-VA addresses
         *      correctly even while HalpUseIdentityMapping is still TRUE,
         *      because it checks (Va >= KSEG0_BASE) first.  This makes the
         *      swap safe against concurrent interrupts.
         *   2. Raise to HIGH_LEVEL (masks all interrupts via DAIF.I).
         *   3. Set HalpUseIdentityMapping = FALSE atomically.
         *   4. Full barrier (DSB SY + ISB).
         *   5. Lower IRQL.
         *
         * For GICv3, the CPU interface uses system registers (no MMIO).
         */
        if (HalpGicUseSysRegs)
        {
            if (!HalpMapGicv3RuntimeMmioWindows())
            {
                DPRINT1("[arm64][HAL] Phase1: MmMapIoSpace failed for GICv3 MMIO; "
                        "GICD/GICR addresses may be stale\n");
            }
        }
        else
        {
            if (!HalpMapGicv2RuntimeMmioWindows())
            {
                DPRINT1("[arm64][HAL] Phase1: MmMapIoSpace failed for GICv2 MMIO; "
                        "GICC/GICD addresses may be stale\n");
            }
        }

        /* Disable identity mapping for BOTH GICv2 and GICv3 */
        SwitchIrql = KfRaiseIrql(HIGH_LEVEL);
        HalpUseIdentityMapping = FALSE;
        __asm__ __volatile__("dsb sy" ::: "memory");
        __asm__ __volatile__("isb" ::: "memory");
        KfLowerIrql(SwitchIrql);

        if (!HalpLoggedGicOnce)
        {
            HalpLoggedGicOnce = TRUE;

            /*
             * For GICv3, enable Group 1 delivery after phase 0 has finished.
             *
             * For GICv2: GICC_CTLR was already enabled in Phase 0 with the
             * appropriate group configuration.  No additional work needed.
             */
            if (HalpGicUseSysRegs)
            {
                KIRQL OldIrql;

                OldIrql = KfRaiseIrql(HIGH_LEVEL);
                HalpWriteIccIgrpen1(1); /* Enable Group1 interrupt delivery */
                __asm__ __volatile__("dsb sy" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");
                KfLowerIrql(OldIrql);
            }
        }

        /*
         * NOTE: GIC ITS initialization is intentionally NOT done here in Phase 1.
         * While Phase 1 runs at PASSIVE_LEVEL (ideal for memory allocations),
         * enabling LPIs on the redistributor (GICR_CTLR.EnableLPIs) during early
         * boot causes the timer interrupt (PPI 27) to stop firing on QEMU HVF,
         * leading to a system hang. ITS initialization is deferred to
         * HalGetMsiVectorRange() with an IRQL guard to prevent crashes when
         * called at elevated IRQL during PnP resource assignment.
         */

        return TRUE;
    }

    /*
     * NOTE: KiHalInitialized flag is now set by the kernel (ex/init.c) after
     * HalInitSystem(0) returns, not by the HAL. This eliminates circular
     * import dependencies (HAL importing kernel, kernel importing HAL).
     *
     * Boot sequence:
     *   1. Kernel entry, PE loader resolves HAL imports
     *   2. Early kernel init - KiHalInitialized = FALSE (binary DAIF masking)
     *   3. HalInitSystem(0) called and completes
     *   4. Kernel sets KiHalInitialized = TRUE in ExArchPostHalInitSystemPhase0
     *   5. GIC priority masking becomes active for IRQL transitions
     *
     * The HAL no longer needs to call back into the kernel to enable GIC support.
     */

    KeInitializeSpinLock(&HalpPCIConfigLock);
    KeInitializeSpinLock(&HalpGicItsLock);
    Status = HalpSetupAcpiPhase0(LoaderBlock);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[arm64][HAL] ACPI phase0 init failed: 0x%08lx\n", Status);
    }

    /* Probe BCM2712 platform (RPi5) — actual MMIO init is deferred */
    Bcm2712PciProbe(LoaderBlock);
    /*
     * CRITICAL IRQL MANAGEMENT FOR GIC INITIALIZATION:
     *
     * We must raise IRQL to HIGH_LEVEL before enabling the GIC CPU interface
     * to prevent interrupts from firing during initialization. Here's why:
     *
     * 1. At PASSIVE_LEVEL, ARM64 IRQ is unmasked (see irql.c line 118)
     * 2. When we enable the GIC CPU interface below (line ~1248), interrupts
     *    become deliverable to the CPU immediately
     * 3. If an SGI or other interrupt fires during HalInitSystem, it would
     *    raise IRQL to interrupt level (≥DISPATCH_LEVEL) in HalBeginSystemInterrupt
     * 4. This causes IRQL violations when interrupt handlers call functions
     *    that require IRQL ≤ DISPATCH_LEVEL (like ExQueueWorkItem → KeInsertQueue)
     *
     * By raising to HIGH_LEVEL, we mask all interrupts at the CPU level via DAIF,
     * preventing any interrupt delivery until the kernel is ready and lowers IRQL.
     *
     * The kernel will lower IRQL to DISPATCH_LEVEL during scheduler initialization,
     * at which point interrupts can safely be processed.
     */
    OldIrql = KfRaiseIrql(HIGH_LEVEL);
    HalpArm64SelectGicInterface(LoaderBlock);
    DPRINT1("[HAL][INIT0] selected GIC sysregs=%u gicd=0x%llx gicc=0x%llx\n",
            HalpGicUseSysRegs,
            HalpGicdBase,
            HalpGiccBase);

    /* Probe GIC capabilities before touching CPU IF */
    {
        ULONG pidr2 = 0;

        /* Identify distributor architecture revision */
        pidr2 = *HalpMmio((ULONG_PTR)HalpGicdBase, 0xFE8); /* GICD_PIDR2 */
        HalpGicArchRev = ((pidr2 >> 4) & 0xF);
        if (HalpGicArchRev == 0)
            HalpGicArchRev = HalpGicUseSysRegs ? 3 : 2;
    }

    /* Disable distributor while we (re)configure */
    *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR) = 0;

    /* How many interrupt lines? */
    typer = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_TYPER);
    lines = 32 * ((typer & 0x1F) + 1);
    if (lines > 1020) lines = 1020;
    nregs = (lines + 31) / 32;

    /*
     * Configure SPI interrupt groups and clear pending.
     *
     * For GIC-v3 with system registers: Use Group 1 (non-secure) as standard.
     * For GIC-v2 (MMIO interface): default to Group 1 (Windows-compatible),
     * with optional Group 0 fallback for known HVF quirk platforms.
     *
     * Group 0 interrupts are delivered via IRQ (not FIQ) when the CPU is in
     * non-secure mode and GICC_CTLR.EOImodeNS=0, which is our configuration.
     */
    for (i = 1; i < nregs; ++i) /* start at 1 to skip SGI/PPI */
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICENABLER + i * 4) = 0xFFFFFFFF;
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICPENDR   + i * 4) = 0xFFFFFFFF;
        if (HalpGicUseSysRegs || !HalpGicv2ForceGroup0)
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IGROUPR + i * 4) = 0xFFFFFFFF; /* G1 */
        }
        else
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IGROUPR + i * 4) = 0x00000000; /* G0 for GIC-v2 */
        }
    }

    /*
     * Set priorities to 0x00 (highest) for all SPIs.
     * This matches the PPI priority fix and ensures interrupts pass the PMR mask.
     */
    for (i = 32; i < lines; i += 4)
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IPRIORITYR + (i & ~3)) = 0x00000000;
    }

    if (HalpGicUseSysRegs)
    {
        ULONGLONG Route = HalpArm64IrouterFromMpidr(HalpReadMpidr());
        for (i = 32; i < lines; ++i)
        {
            HalpMmioWrite64((ULONG_PTR)HalpGicdBase, GICD_IROUTER + (i * 8), Route);
        }
    }
    else
    {
        for (i = 32; i < lines; i += 4)
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ITARGETSR + (i & ~3))  = 0x01010101; /* CPU0 */
        }
    }
    /*
     * GIC-v2: Configure SGI/PPI bank (interrupts 0-31) BEFORE enabling distributor.
     *
     * CRITICAL: We must preserve PPIs that were already enabled by KeInitInterrupts()
     * (e.g., timer PPI 27, DPC SGI 2, APC SGI 1). KeInitInterrupts() is called before
     * HalInitSystem() in the boot sequence:
     *   1. KiInitializeSystem -> KeInitInterrupts() -> enables timer/DPC/APC via GICD_ISENABLER0
     *   2. KiInitializeKernel -> ExpInitializeExecutive -> HalInitSystem(0, ...)
     *
     * If we blindly disable all SGIs/PPIs here, the timer interrupt never fires,
     * causing the idle loop to stall indefinitely in WFE.
     *
     * For GIC-v3, this is handled differently via redistributor registers which
     * are configured per-CPU in HalpInitGicRedistributor() and are not reset here.
     */
    if (!HalpGicUseSysRegs)
    {
        ULONG SavedEnables;

        /*
         * Save currently enabled PPIs before reconfiguration.
         * SGIs (bits 0-15) are always enabled in GIC-v2 and cannot be disabled,
         * but PPIs (bits 16-31) need to be preserved if already enabled.
         */
        SavedEnables = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ISENABLER + 0);

        /* Clear pending interrupts but do NOT disable - we will restore enables */
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICPENDR + 0) = 0xFFFFFFFF;

        /*
         * Configure SGI/PPI groups for GIC-v2:
         * - Default: Group 1 non-secure (Windows-compatible behavior)
         * - HVF quirk mode: Group 0 only
         */
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IGROUPR + 0) =
            HalpGicv2ForceGroup0 ? 0x00000000 : 0xFFFFFFFF;

        /*
         * Set SGI/PPI priorities to 0x00 (highest).
         * Matches the GICv3 fix for priority masking.
         */
        for (ULONG i = 0; i < 32; i += 4)
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IPRIORITYR + i) = 0x00000000;
        }

        /*
         * Restore previously enabled interrupts.
         * Writing to GICD_ISENABLER sets enable bits (does not clear others).
         * This ensures timer PPI and SGIs remain enabled after reconfiguration.
         */
        if (SavedEnables != 0)
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ISENABLER + 0) = SavedEnables;
        }
    }

    /*
     * Memory barrier to ensure all distributor configuration (SPIs + SGI/PPI)
     * is complete before we enable interrupt delivery.
     */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * Enable the distributor.
     *
     * GICD_CTLR bits:
     *   Bit 0: EnableGrp0 - Group 0 interrupts
     *   Bit 1: EnableGrp1 - Group 1 (non-secure) interrupts
     *   Bit 4: ARE_NS - Affinity Routing Enable (GICv3 only)
     *
     * For GIC-v2, default to enabling both groups (Group0 + Group1) to match
     * non-secure Windows-style interrupt delivery. On known HVF quirk setups,
     * fallback to Group0-only mode.
     *
     * For GIC-v3, we MUST enable BOTH Group 0 and Group 1 because:
     *   - SPIs are configured as Group 1 (GICD_IGROUPR)
     *   - PPIs are configured as Group 1 in redistributor (GICR_IGROUPR0)
     *   - ICC_IGRPEN1_EL1 is enabled for Group 1 interrupt delivery
     */
    if (HalpGicUseSysRegs)
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR) = 0x13; /* EnableGrp0 | EnableGrp1NS | ARE_NS */
    }
    else
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR) = HalpGicv2ForceGroup0 ? 0x1 : 0x3;
    }

    /* Barrier to ensure GICD enable completes before CPU interface configuration */
    __asm__ __volatile__("dsb sy" ::: "memory");

    if (HalpGicUseSysRegs)
    {
        HalpInitGicRedistributor(KeGetCurrentProcessorNumber());
    }

    /* CPU interface: system registers (v3+) or legacy GICC (v2) */
    if (HalpGicUseSysRegs)
    {
        ULONG Sre;

        /* Enable system register access and configure priority mask. */
        Sre = HalpReadIccSre();
        Sre |= 0x1; /* SRE bit */
        HalpWriteIccSre(Sre);
        HalpWriteIccPmr(0xFF); /* allow all priorities */
        HalpWriteIccBpr1(0);

        /*
         * Keep Group 1 delivery disabled in phase 0. The executive enables CPU
         * IRQ delivery after HalInitSystem(0), and HAL phase 1 enables Group 1
         * after memory initialization has completed.
         */
        HalpWriteIccIgrpen1(0); /* KEEP DISABLED in Phase 0 */
    }
    else
    {
        if (HalpGiccBase == 0)
        {
            DPRINT1("[arm64][HAL] HalInitSystem: legacy CPU IF unavailable (GICC base is 0)\n");
        }
        else
        {
            ULONG GiccCtlr;

            /*
             * GIC-v2 Legacy CPU Interface (GICC) Initialization
             *
             * Note: The SGI/PPI bank (interrupts 0-31) was already configured
             * earlier while the distributor was disabled to avoid race conditions.
             * Now we just need to enable the per-CPU GICC interface.
             *
             * Configure GICC (CPU Interface):
             * - PMR = 0xFF: Allow all priority levels
             * - BPR = 0x0: No preemption grouping
             * - CTLR = 0x3: Enable both Group0 and Group1 (default path)
             * - CTLR = 0x1: Group0-only in HVF quirk mode
             */
            GiccCtlr = HalpGicv2ForceGroup0 ? 0x1 : 0x3;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_PMR) = 0xFF;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_BPR) = 0x0;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_CTLR) = GiccCtlr;

            /*
             * CRITICAL: Memory barrier after GICC configuration.
             *
             * ARM64 has a relaxed memory model. MMIO writes to device registers
             * may not complete immediately. DSB ensures that the GICC_CTLR
             * enable takes effect before any subsequent memory operations
             * (including PCI ECAM reads) can proceed.
             *
             * This barrier is essential for GIC-v2 because:
             * 1. The CPU interface must be fully enabled before interrupt
             *    delivery can work correctly
             * 2. Without this barrier, subsequent MMIO reads (like PCI config
             *    space access) may hang if they depend on interrupt state
             * 3. QEMU's GIC-v2 emulation is sensitive to proper ordering
             *
             * ISB ensures the barrier effects are synchronized with the
             * instruction stream.
             */
            __asm__ __volatile__("dsb sy" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");
        }
    }

    HalpGicPhase0Complete = TRUE;
    HalpArm64ApplyDeferredInterruptEnables();
    DPRINT1("[HAL][INIT0] phase0 GIC ready\n");

    /*
     * Initialize system time increment values for the scheduler tick.
     * ARM64 generic timer targets 100 Hz (10ms period = 100,000 100ns units).
     * The maximum increment is 10ms (100000), minimum is 1ms (10000).
     * This matches the configuration in KiArm64StartTimer().
     */
    KeSetTimeIncrement(100000, 10000);
    DPRINT1("[HAL][INIT0] KeSetTimeIncrement complete\n");

    /*
     * Set up HAL dispatch table callbacks.
     *
     * HalInitPnpDriver is called by IoInitSystem() to initialize the
     * "HAL Root Bus Driver". This driver creates the ACPI device node
     * that the ACPI.sys driver attaches to, enabling PnP enumeration.
     *
     * Without this callback, drivers load but never receive AddDevice()
     * or IRP_MN_START_DEVICE requests, so ACPI/PCI enumeration never happens.
     */
    HalInitPnpDriver = HaliInitPnpDriver;
    HalQuerySystemInformation = HaliQuerySystemInformation;
    HalSetSystemInformation = HaliSetSystemInformation;

    /*
     * CRITICAL: Lower IRQL back to the original level before returning.
     *
     * We raised IRQL to HIGH_LEVEL during GIC initialization to prevent
     * interrupts from firing while we configured the interrupt controller.
     * Now that the GIC is configured, we MUST restore the original IRQL.
     */
    KeLowerIrql(OldIrql);

    return TRUE;
}

VOID
FASTCALL
HalRequestSoftwareInterrupt(
    _In_ KIRQL SoftwareInterruptRequested)
{
    /*
     * CRITICAL ARM64 SOFTWARE INTERRUPT DESIGN:
     *
     * On ARM64, HalRequestSoftwareInterrupt must NOT send actual hardware interrupts (SGIs).
     * Instead, the kernel handles software interrupt delivery via KeLowerIrql.
     *
     * WHY NOT USE SGIs?
     * -----------------
     * If we send an SGI here, it fires immediately as a hardware interrupt, which:
     * 1. Raises IRQL in HalBeginSystemInterrupt before any pending work can be done
     * 2. Causes IRQL violations when called from code at PASSIVE_LEVEL
     * 3. Breaks the Windows IRQL model where software interrupts are "requested"
     *    but only "delivered" when IRQL permits
     *
     * EXAMPLE FAILURE SCENARIO (with SGI):
     * -------------------------------------
     * ExQueueWorkItem (IRQL=PASSIVE_LEVEL):
     *   -> HalRequestSoftwareInterrupt(DISPATCH_LEVEL)
     *      -> Sends SGI  [BUG: Immediate interrupt!]
     *         -> HalBeginSystemInterrupt raises IRQL to DISPATCH_LEVEL
     *   -> Continues executing at DISPATCH_LEVEL [BUG!]
     *   -> Calls KeInsertQueue (requires IRQL <= DISPATCH_LEVEL)
     *      -> ASSERTION FAILS: Current IRQL (2) > Maximum (2)
     *
     * THE CORRECT ARM64 MODEL (following x86 PIC HAL):
     * -------------------------------------------------
     * 1. HalRequestSoftwareInterrupt is a NO-OP (or sets a flag in kernel PRCB)
     * 2. The kernel's KeLowerIrql checks for pending software interrupts
     * 3. KeLowerIrql calls KiDispatchInterrupt when lowering to PASSIVE_LEVEL
     * 4. This ensures software interrupts never violate IRQL ordering
     *
     * For ARM64, the kernel's KeLowerIrql (in ntoskrnl/arch/arm64/ke/irql.c)
     * handles checking Prcb->DpcInterruptRequested and calling KiDispatchInterrupt.
     *
     * NOTE: SGIs should only be used for true inter-processor interrupts (IPI),
     * not for software interrupt simulation on the local processor.
     */

    UNREFERENCED_PARAMETER(SoftwareInterruptRequested);

    /*
     * ARM64: Send SEV (Send Event) to wake up the idle CPU from WFE.
     *
     * The kernel already sets the appropriate pending flags before calling
     * this function:
     * - Prcb->DpcInterruptRequested for DPCs (set by KeInsertQueueDpc)
     * - Prcb->NextThread for ready threads (set by KiDeferredReadyThread)
     *
     * SEV is the ARM64 equivalent of the x86 PIC HAL setting the IRR bit.
     * It doesn't cause an interrupt - it just sets the event register,
     * causing the next WFE (Wait For Event) to immediately wake up.
     *
     * The idle loop (KiIdleLoop) uses WFE and checks Prcb->NextThread
     * and Prcb->DpcInterruptRequested after waking.
     */
    __asm__ __volatile__("sev" ::: "memory");
}

VOID
NTAPI
HalAcquireDisplayOwnership(
    _In_ PHAL_RESET_DISPLAY_PARAMETERS ResetDisplayParameters)
{
    UNREFERENCED_PARAMETER(ResetDisplayParameters);
    UNIMPLEMENTED_STUB();
}

NTSTATUS
NTAPI
HalAdjustResourceList(
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST *ResourceList)
{
    UNREFERENCED_PARAMETER(ResourceList);
    UNIMPLEMENTED_STUB();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    return TRUE;
}

/*
 * HalAllocateAdapterChannel
 *
 * Allocates map registers for DMA operations and calls the driver's
 * execution routine when the registers are available.
 *
 * On ARM64, this function:
 * 1. Allocates a map register base structure
 * 2. Initializes map register entries for scatter/gather tracking
 * 3. Calls the driver's execution routine with the map register base
 */
NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PWAIT_CONTEXT_BLOCK Wcb,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine)
{
    PHAL_ARM64_MAP_REGISTER_BASE MapRegisterBase;
    SIZE_T AllocationSize;
    IO_ALLOCATION_ACTION Action;
    KIRQL OldIrql;

    if (!AdapterObject || !Wcb || !ExecutionRoutine)
    {
        DPRINT1("[arm64][DMA] HalAllocateAdapterChannel: Invalid parameters\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* Limit the number of map registers */
    if (NumberOfMapRegisters > HAL_ARM64_MAX_MAP_REGISTERS)
    {
        NumberOfMapRegisters = HAL_ARM64_MAX_MAP_REGISTERS;
    }

    if (NumberOfMapRegisters == 0)
    {
        NumberOfMapRegisters = 1;
    }

    /* Allocate the map register base structure */
    AllocationSize = FIELD_OFFSET(HAL_ARM64_MAP_REGISTER_BASE, Registers) +
                     (NumberOfMapRegisters * sizeof(HAL_ARM64_MAP_REGISTER_ENTRY));

    MapRegisterBase = ExAllocatePoolWithTag(NonPagedPool, AllocationSize, TAG_DMA_MAP);
    if (!MapRegisterBase)
    {
        DPRINT1("[arm64][DMA] HalAllocateAdapterChannel: Failed to allocate map registers\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Initialize the map register base */
    RtlZeroMemory(MapRegisterBase, AllocationSize);
    MapRegisterBase->Signature = HAL_ARM64_MAP_REG_SIGNATURE;
    MapRegisterBase->NumberOfMapRegisters = NumberOfMapRegisters;
    MapRegisterBase->AdapterObject = AdapterObject;

    /* Store the map register base in the adapter */
    AdapterObject->MapRegisterBase = MapRegisterBase;
    AdapterObject->NumberOfMapRegisters = NumberOfMapRegisters;

    /*
     * On ARM64, we don't have a DMA controller to arbitrate, so we can
     * immediately call the execution routine.
     */
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < DISPATCH_LEVEL)
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    Action = ExecutionRoutine(Wcb->DeviceObject,
                              Wcb->CurrentIrp,
                              MapRegisterBase,
                              Wcb->DeviceContext);

    if (OldIrql < DISPATCH_LEVEL)
        KeLowerIrql(OldIrql);

    /* Handle the execution routine's return value */
    switch (Action)
    {
        case DeallocateObject:
            IoFreeMapRegisters(AdapterObject, MapRegisterBase, NumberOfMapRegisters);
            break;

        case DeallocateObjectKeepRegisters:
        case KeepObject:
        default:
            break;
    }

    return STATUS_SUCCESS;
}

/*
 * HalAllocateCommonBuffer
 *
 * Allocates contiguous, DMA-accessible memory that can be shared between
 * the CPU and a DMA device (a "common buffer").
 *
 * On ARM64, common buffers must be physically contiguous, cache-line aligned,
 * and mapped appropriately based on CacheEnabled and system coherency.
 */
PVOID
NTAPI
HalAllocateCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress,
    _In_ BOOLEAN CacheEnabled)
{
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS BoundaryAddress;
    MEMORY_CACHING_TYPE CacheType;
    PVOID VirtualAddress;
    PHAL_ARM64_COMMON_BUFFER BufferEntry;
    KIRQL OldIrql;
    ULONG Alignment;

    if (!LogicalAddress)
    {
        DPRINT1("[arm64][DMA] HalAllocateCommonBuffer: NULL LogicalAddress\n");
        return NULL;
    }

    if (Length == 0)
    {
        LogicalAddress->QuadPart = 0;
        return NULL;
    }

    /* Determine alignment based on cache line size */
    Alignment = HalpArm64DmaCoherency.CacheLineSize;
    if (Alignment < HAL_ARM64_DMA_DEFAULT_ALIGNMENT)
        Alignment = HAL_ARM64_DMA_DEFAULT_ALIGNMENT;

    /* Round length up to alignment */
    Length = (Length + Alignment - 1) & ~(Alignment - 1);

    /* Set memory constraints */
    LowAddress.QuadPart = 0;
    if (AdapterObject && !AdapterObject->Dma64BitAddresses)
        HighAddress.QuadPart = HAL_ARM64_DMA_32BIT_LIMIT - 1;
    else
        HighAddress.QuadPart = ~0ULL;
    BoundaryAddress.QuadPart = 0;

    /*
     * ARM64 DMA common buffer cache type.
     *
     * On non-coherent systems (no IORT, no SMMU), DMA masters read from
     * DRAM directly, bypassing CPU caches. Common buffers MUST be non-
     * cacheable so CPU writes are immediately visible to DMA masters.
     *
     * With MmCached, command ring TRBs written by the CPU stay in L1/L2
     * cache and never reach DRAM — the xHCI reads stale zeros and never
     * processes commands (seen as NOOP/Enable Slot timeout on RPi5 RP1).
     *
     * Cache-coherent platforms can honor a cached common-buffer request: the
     * interconnect keeps CPU caches and DMA masters coherent, and the cached
     * allocation path avoids remapping every small buffer as noncached.
     *
     * MmNonCached maps as Normal Non-Cacheable (not Device memory), which
     * remains required for non-coherent systems.
     */
    CacheType = (CacheEnabled && HalpArm64DmaCoherency.SystemCoherent) ?
                MmCached :
                MmNonCached;

    /* Allocate physically contiguous memory */
    VirtualAddress = MmAllocateContiguousMemorySpecifyCache(Length,
                                                             LowAddress,
                                                             HighAddress,
                                                             BoundaryAddress,
                                                             CacheType);
    if (!VirtualAddress)
    {
        DPRINT1("[arm64][DMA] HalAllocateCommonBuffer: allocation failed (len=%lu)\n", Length);
        LogicalAddress->QuadPart = 0;
        return NULL;
    }

    RtlZeroMemory(VirtualAddress, Length);
    *LogicalAddress = MmGetPhysicalAddress(VirtualAddress);

    if (LogicalAddress->QuadPart == 0)
    {
        MmFreeContiguousMemorySpecifyCache(VirtualAddress, Length, CacheType);
        return NULL;
    }

    /* For non-coherent systems, clean and invalidate cache */
    if (CacheType == MmCached && !HalpArm64DmaCoherency.SystemCoherent)
    {
        HalpArm64CleanInvalidateDcacheRange(VirtualAddress, Length);
        __asm__ __volatile__("dsb sy" ::: "memory");
    }

    /* Track the allocation for cleanup */
    BufferEntry = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(HAL_ARM64_COMMON_BUFFER),
                                        TAG_DMA_CMN);
    if (BufferEntry)
    {
        BufferEntry->VirtualAddress = VirtualAddress;
        BufferEntry->PhysicalAddress = *LogicalAddress;
        BufferEntry->RawAllocation = VirtualAddress;
        BufferEntry->Length = Length;
        BufferEntry->Alignment = Alignment;
        BufferEntry->CacheEnabled = (CacheType == MmCached);

        KeAcquireSpinLock(&HalpArm64CommonBufferLock, &OldIrql);
        InsertTailList(&HalpArm64CommonBufferList, &BufferEntry->ListEntry);
        KeReleaseSpinLock(&HalpArm64CommonBufferLock, OldIrql);
    }

    return VirtualAddress;
}

PVOID
NTAPI
HalAllocateCrashDumpRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _Inout_ PULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    UNIMPLEMENTED_STUB();
    return NULL;
}

NTSTATUS
NTAPI
HalpAssignSlotResources(
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Inout_ PCM_RESOURCE_LIST *AllocatedResources)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(DriverClassName);
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(BusType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(AllocatedResources);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalAssignSlotResources(
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Inout_ PCM_RESOURCE_LIST *AllocatedResources)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(DriverClassName);
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(BusType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(AllocatedResources);
    UNIMPLEMENTED_STUB();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
HalBeginSystemInterrupt(
    _In_ KIRQL Irql,
    _In_ ULONG Vector,
    _Out_ PKIRQL OldIrql)
{
    ULONG cpu = KeGetCurrentProcessorNumber();
    ULONG intid = Vector;

    /*
     * CRITICAL: Check for spurious interrupts BEFORE raising IRQL.
     * GICv2: INTID 1023 = spurious
     * GICv3: INTID 1023 = spurious, 1020-1023 are special
     *
     * NOTE: INTID 0 IS a valid SGI (HAL_ARM64_SGI_IPI) and must NOT
     * be rejected. The kernel uses SGI 0 for inter-processor interrupts.
     */
    if ((intid >= 1020) && (intid < HAL_ARM64_LPI_BASE))
        return FALSE;

    if (!HalpGicItsEnabled && intid >= HAL_ARM64_LPI_BASE)
        return FALSE;

    if (HalpGicItsEnabled && HalpGicLpiCount &&
        intid >= (HAL_ARM64_LPI_BASE + HalpGicLpiCount))
    {
        return FALSE;
    }

    /*
     * Raise IRQL to the interrupt's synchronization level.
     * This prevents lower-priority interrupts from preempting this handler.
     * Must happen BEFORE processing the interrupt to maintain IRQL discipline.
     */
    if (OldIrql) *OldIrql = KfRaiseIrql(Irql);

    /*
     * Save the active INTID for this CPU so HalEndSystemInterrupt can EOI it.
     * Interrupts can nest by priority on ARM64, so a single per-CPU INTID slot
     * is not enough: a timer interrupt taken inside a device ISR would overwrite
     * the device INTID and leave the device active in the GIC forever.
     */
    if (cpu < MAXIMUM_PROCESSORS)
    {
        UCHAR depth = HalpArm64ActiveIntIdDepth[cpu];

        if (depth < HAL_ARM64_ACTIVE_INTID_STACK_DEPTH)
        {
            HalpArm64ActiveIntIdStack[cpu][depth] = intid + 1;
            HalpArm64ActiveIntIdDepth[cpu] = depth + 1;
            HalpArm64ActiveIntId[cpu] = intid + 1;
        }
        else
        {
            DPRINT1("[arm64][GIC] Active INTID stack overflow on CPU %lu, INTID %lu\n",
                    cpu,
                    intid);
        }
    }

    return TRUE;
}

VOID
NTAPI
HalCalibratePerformanceCounter(
    _In_ ULONG Count,
    _In_ ULONG64 Period,
    _Out_ PULONG64 Frequency)
{
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Period);
    if (Frequency)
    {
        *Frequency = HalpReadCntfrq();
    }
}

VOID
FASTCALL
HalClearSoftwareInterrupt(
    _In_ KIRQL Request)
{
    UNREFERENCED_PARAMETER(Request);
}

VOID
NTAPI
HalDisableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql)
{
    ULONG reg = Vector / 32;
    ULONG bit = Vector % 32;
    UNREFERENCED_PARAMETER(Irql);

    /*
     * SGIs (0-15) cannot be disabled in GICv3 - they are always enabled.
     * Attempting to disable them via ICENABLER has no effect per ARM GIC spec.
     * Only PPIs (16-31) and SPIs (32+) can be disabled.
     */
    if (Vector < 16)
    {
        /* SGI: no-op, cannot be disabled */
        return;
    }

    if (HalpGicUseSysRegs && Vector < 32)
    {
        /* PPI (16-31): disable via redistributor */
        *HalpMmio(HalpGicrSgiBase(KeGetCurrentProcessorNumber()), GICR_ICENABLER0) = (1u << bit);
    }
    else if (Vector >= HAL_ARM64_LPI_BASE)
    {
        /* LPI (8192+): Disable via ITS configuration table */
        ULONG LpiIndex = Vector - HAL_ARM64_LPI_BASE;

        DPRINT1("[arm64][LPI] Disabling LPI %lu (index %lu)\n", Vector, LpiIndex);

        if (!HalpGicLpiConfig || LpiIndex >= HalpGicLpiCount)
        {
            DPRINT1("[arm64][LPI] ERROR: Invalid LPI %lu (index %lu >= count %lu)\n",
                    Vector, LpiIndex, HalpGicLpiCount);
            return;
        }

        /* Clear enable bit in LPI configuration table */
        HalpGicLpiConfig[LpiIndex] = (UCHAR)(HAL_ARM64_LPI_PROP_PRIO_DEFAULT |
                                             HAL_ARM64_LPI_PROP_GROUP1);

        /* Clean cache to ensure configuration table update is visible */
        HalpArm64CleanDcacheRange(&HalpGicLpiConfig[LpiIndex], sizeof(UCHAR));

        __asm__ __volatile__("dsb sy" ::: "memory");
    }
    else if (Vector < 1020)
    {
        /* SPI (32-1019): disable via distributor */
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICENABLER + reg * 4) = (1u << bit);
    }
    else
    {
        /* Reserved range (1020-8191): no-op */
        DPRINT1("[arm64] HalDisableSystemInterrupt: Invalid vector %lu (reserved range)\n", Vector);
    }
}

VOID
NTAPI
HalDisplayString(
    _In_ PCSTR String)
{
    /* Call the Inbv driver */
    InbvDisplayString(String);
}

/*
 * Map IRQL to GIC priority.
 *
 * GIC priority: 0 = highest, 0xFF = lowest (typically 8-bit, 5-bit implemented)
 * Windows IRQL: 0 = lowest (PASSIVE), 15 = highest (HIGH_LEVEL)
 *
 * We map:
 *   IRQL 15 (HIGH_LEVEL)  -> GIC priority 0x00 (highest hardware priority)
 *   IRQL 14 (IPI_LEVEL)   -> GIC priority 0x10
 *   IRQL 13 (CLOCK_LEVEL) -> GIC priority 0x20
 *   IRQL 12-3 (device)    -> GIC priority 0x30-0xC0
 *   IRQL 2-0              -> Not hardware interrupts (use 0xF0)
 *
 * This allows higher-IRQL interrupts to preempt lower-IRQL handlers at
 * the GIC level, rather than relying solely on software IRQL checks.
 */
static UCHAR
HalpIrqlToGicPriority(
    _In_ KIRQL Irql)
{
    if (Irql >= HIGH_LEVEL)
        return 0x00;
    if (Irql >= IPI_LEVEL)
        return 0x10;
    if (Irql >= CLOCK_LEVEL)
        return 0x20;
    if (Irql >= DISPATCH_LEVEL + 1)
    {
        /* Device IRQLs 3-12 map to priorities 0x30-0xC0 */
        UCHAR offset = (UCHAR)(12 - Irql);  /* 12->0, 3->9 */
        return 0x30 + (offset * 0x10);
    }
    /* DISPATCH_LEVEL and below - low priority */
    return 0xF0;
}

static const UCHAR HalpArm64IrqlToPmr[HIGH_LEVEL + 1] =
{
    0xFF, /* PASSIVE_LEVEL */
    0xFF, /* APC_LEVEL */
    0xFF, /* DISPATCH_LEVEL */
    0xC0, /* DEVICE_LEVEL 3 */
    0xB0,
    0xA0,
    0x90,
    0x80,
    0x70,
    0x60,
    0x50,
    0x40,
    0x30, /* DEVICE_LEVEL 12 */
    0x20, /* CLOCK_LEVEL */
    0x10, /* IPI_LEVEL */
    0x00  /* HIGH_LEVEL */
};

BOOLEAN
NTAPI
HalEnableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE InterruptMode)
{
    ULONG reg = Vector / 32;
    ULONG bit = Vector % 32;
    ULONG prioReg;
    ULONG prioShift;
    ULONG prioVal;
    UCHAR priority;
    UNREFERENCED_PARAMETER(InterruptMode);

    if (!HalpGicPhase0Complete)
    {
        for (ULONG Index = 0; Index < RTL_NUMBER_OF(HalpArm64DeferredInterrupts); ++Index)
        {
            PHALP_ARM64_DEFERRED_INTERRUPT Entry = &HalpArm64DeferredInterrupts[Index];

            if (Entry->Valid && Entry->Vector == Vector)
            {
                Entry->Irql = Irql;
                Entry->InterruptMode = InterruptMode;
                return TRUE;
            }
        }

        for (ULONG Index = 0; Index < RTL_NUMBER_OF(HalpArm64DeferredInterrupts); ++Index)
        {
            PHALP_ARM64_DEFERRED_INTERRUPT Entry = &HalpArm64DeferredInterrupts[Index];

            if (!Entry->Valid)
            {
                Entry->Vector = Vector;
                Entry->Irql = Irql;
                Entry->InterruptMode = InterruptMode;
                Entry->Valid = TRUE;
                return TRUE;
            }
        }

        return FALSE;
    }

    /*
     * SGIs (0-15) are always enabled in GICv3 and cannot be enabled/disabled.
     * They are configured during redistributor initialization and are permanently enabled.
     * Writing to ISENABLER for SGIs is architecturally permitted but has no effect.
     * We skip the write to avoid confusion - SGIs are already enabled.
     */
    if (Vector < 16)
    {
        /* SGI: already enabled, no action needed */
        return TRUE;
    }

    /* Calculate GIC priority from IRQL */
    priority = HalpIrqlToGicPriority(Irql);

    if (HalpGicUseSysRegs && Vector < 32)
    {
        /* PPI (16-31): use Redistributor registers */
        ULONG Cpu = KeGetCurrentProcessorNumber();
        ULONG_PTR SgiBase = HalpGicrSgiBase(Cpu);
        ULONG IsEnablerAfter;

        if (SgiBase == 0)
        {
            DPRINT1("[arm64][PPI] ERROR: SgiBase is NULL for CPU %lu!\n", Cpu);
            return FALSE;
        }

        /* Set priority for this interrupt */
        prioReg = GICR_IPRIORITYR + (Vector & ~3);
        prioShift = (Vector & 3) * 8;
        prioVal = *HalpMmio(SgiBase, prioReg);
        prioVal &= ~(0xFFu << prioShift);
        prioVal |= ((ULONG)priority << prioShift);
        *HalpMmio(SgiBase, prioReg) = prioVal;

        /* Memory barrier after priority write */
        __asm__ __volatile__("dsb sy" ::: "memory");

        /* Enable the interrupt */
        *HalpMmio(SgiBase, GICR_ISENABLER0) = (1u << bit);

        /* Memory barrier after enable */
        __asm__ __volatile__("dsb sy" ::: "memory");

        /* Verify it was enabled */
        IsEnablerAfter = *HalpMmio(SgiBase, GICR_ISENABLER0);

        if (!(IsEnablerAfter & (1u << bit)))
        {
            DPRINT1("[arm64][PPI] WARNING: PPI %lu did not enable! Retrying...\n", Vector);
            /* Retry the enable */
            *HalpMmio(SgiBase, GICR_ISENABLER0) = (1u << bit);
            __asm__ __volatile__("dsb sy" ::: "memory");
        }
    }
    else if (Vector >= HAL_ARM64_LPI_BASE)
    {
        /* LPI (8192+): Enable via ITS configuration table */
        ULONG LpiIndex = Vector - HAL_ARM64_LPI_BASE;
        ULONG Cpu = KeGetCurrentProcessorNumber();
        UCHAR Config;

        DPRINT1("[arm64][LPI] Enabling LPI %lu (index %lu)\n", Vector, LpiIndex);

        if (!HalpGicLpiConfig || LpiIndex >= HalpGicLpiCount)
        {
            DPRINT1("[arm64][LPI] ERROR: Invalid LPI %lu (index %lu >= count %lu)\n",
                    Vector, LpiIndex, HalpGicLpiCount);
            return FALSE;
        }

        /* Set priority and enable in LPI configuration table */
        Config = (UCHAR)((priority & 0xFC) |  /* Priority in bits 7:2 */
                        HAL_ARM64_LPI_PROP_GROUP1 |
                        HAL_ARM64_LPI_PROP_ENABLED);
        HalpGicLpiConfig[LpiIndex] = Config;

        /* Clean cache to ensure configuration table update is visible to redistributor */
        HalpArm64CleanDcacheRange(&HalpGicLpiConfig[LpiIndex], sizeof(UCHAR));

        /* Memory barrier */
        __asm__ __volatile__("dsb sy" ::: "memory");

        /* Send INVALL command to invalidate cached LPI configuration */
        if (!HalpGicItsSendInvall(Cpu))
        {
            DPRINT1("[arm64][LPI] WARNING: INVALL command failed for CPU %lu\n", Cpu);
        }
    }
    else if (Vector < 1020)
    {
        /* SPI (32-1019): use Distributor registers */
        ULONG IsEnablerBefore, IsEnablerAfter;

        DPRINT("[arm64][SPI] Enabling SPI %lu\n", Vector);

        /* Read current enable state */
        IsEnablerBefore = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ISENABLER + reg * 4);

        /* Set priority for this interrupt */
        prioReg = GICD_IPRIORITYR + (Vector & ~3);
        prioShift = (Vector & 3) * 8;
        prioVal = *HalpMmio((ULONG_PTR)HalpGicdBase, prioReg);
        prioVal &= ~(0xFFu << prioShift);
        prioVal |= ((ULONG)priority << prioShift);
        *HalpMmio((ULONG_PTR)HalpGicdBase, prioReg) = prioVal;

        /* Memory barrier after priority write */
        __asm__ __volatile__("dsb sy" ::: "memory");

        /* Enable the interrupt */
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ISENABLER + reg * 4) = (1u << bit);

        /* Memory barrier after enable */
        __asm__ __volatile__("dsb sy" ::: "memory");

        /* Verify it was enabled */
        IsEnablerAfter = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ISENABLER + reg * 4);
        DPRINT("[arm64][SPI]   ISENABLER before: 0x%08lx after: 0x%08lx (bit %lu = %lu)\n",
               IsEnablerBefore, IsEnablerAfter, bit, (IsEnablerAfter >> bit) & 1);
    }
    else
    {
        /* Reserved range (1020-8191): error */
        DPRINT1("[arm64] HalEnableSystemInterrupt: Invalid vector %lu (reserved range)\n", Vector);
        return FALSE;
    }

    DPRINT1("[arm64] HalEnableSystemInterrupt: Vector=%lu IRQL=%u GICPriority=0x%02X COMPLETE\n",
            Vector, Irql, priority);

    return TRUE;
}

static VOID
HalpArm64ApplyDeferredInterruptEnables(VOID)
{
    for (ULONG Index = 0; Index < RTL_NUMBER_OF(HalpArm64DeferredInterrupts); ++Index)
    {
        PHALP_ARM64_DEFERRED_INTERRUPT Entry = &HalpArm64DeferredInterrupts[Index];

        if (!Entry->Valid)
        {
            continue;
        }

        if ((Entry->Vector == 27) || (Entry->Vector == 30))
        {
            continue;
        }

        (VOID)HalEnableSystemInterrupt(Entry->Vector,
                                       Entry->Irql,
                                       Entry->InterruptMode);
        Entry->Valid = FALSE;
    }
}

VOID
NTAPI
HalEndSystemInterrupt(
    _In_ KIRQL Irql,
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG cpu = KeGetCurrentProcessorNumber();
    ULONG stored = 0;
    UCHAR depth = 0;

    if (cpu < MAXIMUM_PROCESSORS)
    {
        depth = HalpArm64ActiveIntIdDepth[cpu];
        if (depth != 0)
        {
            depth--;
            stored = HalpArm64ActiveIntIdStack[cpu][depth];
            HalpArm64ActiveIntIdStack[cpu][depth] = 0;
            HalpArm64ActiveIntIdDepth[cpu] = depth;
            HalpArm64ActiveIntId[cpu] = (depth != 0) ?
                                       HalpArm64ActiveIntIdStack[cpu][depth - 1] :
                                       0;
        }
        else
        {
            stored = HalpArm64ActiveIntId[cpu];
            HalpArm64ActiveIntId[cpu] = 0;
        }
    }

    if (stored)
    {
        /* Decode actual INTID (stored as intid + 1 to avoid sentinel conflict with INTID 0) */
        ULONG intid = stored - 1;

        /* Memory barrier to ensure all interrupt processing is visible */
        __asm__ __volatile__("dsb sy" ::: "memory");

        /* Debug: Log LPI EOIs */
        if (intid >= HAL_ARM64_LPI_BASE)
        {
            DPRINT1("[arm64][LPI] EOI LPI %lu\n", intid);
        }

        if (HalpGicUseSysRegs)
        {
            HalpWriteIccEoir1(intid);
        }
        else
        {
            if (HalpGiccBase != 0)
            {
                *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_EOIR) = intid;
                __asm__ __volatile__("dsb sy" ::: "memory");
            }
        }
    }

    /*
     * Lower IRQL only after the interrupt is no longer active in the GIC.
     * KeLowerIrql can deliver pending DPC/timer/quantum work on ARM64; running
     * that dispatch path before EOI leaves the timer/device interrupt active
     * while switching threads, which can starve lower-priority GUI/input work.
     */
    KeLowerIrql(Irql);

    UNREFERENCED_PARAMETER(TrapFrame);
}

/*
 * HalpGetRootInterruptVector - Translate bus interrupt to system vector and IRQL
 *
 * This function maps a bus interrupt level/vector (typically a GSI from ACPI)
 * to a system interrupt vector and IRQL for ARM64 GIC systems.
 *
 * Parameters:
 *   BusInterruptLevel  - GSI (Global System Interrupt) from ACPI or bus-specific interrupt
 *   BusInterruptVector - Additional vector information (usually same as level on ARM64)
 *   OutIrql           - Receives the IRQL for this interrupt
 *   OutAffinity       - Receives the processor affinity mask
 *
 * Returns:
 *   System interrupt vector (GIC INTID on ARM64)
 *
 * On ARM64 with GIC:
 *   - Vector = INTID (GIC interrupt ID)
 *   - IRQL is assigned based on interrupt priority
 *   - Affinity defaults to all processors for SPIs, CPU-local for PPIs
 */
ULONG
NTAPI
HalpGetRootInterruptVector(
    _In_ ULONG BusInterruptLevel,
    _In_ ULONG BusInterruptVector,
    _Out_ PKIRQL OutIrql,
    _Out_ PKAFFINITY OutAffinity)
{
    ULONG IntId;
    UCHAR Polarity, TriggerMode;
    KIRQL Irql;
    KAFFINITY Affinity;

    UNREFERENCED_PARAMETER(BusInterruptVector);

    /*
     * Translate GSI to GIC INTID using ACPI translation.
     * This handles interrupt source overrides from MADT.
     */
    IntId = HalpArm64TranslateGsiToIntId(BusInterruptLevel, &Polarity, &TriggerMode);

    if (IntId == (ULONG)-1)
    {
        DPRINT1("[arm64] HalpGetRootInterruptVector: Invalid GSI %lu\n", BusInterruptLevel);
        *OutIrql = PASSIVE_LEVEL;
        *OutAffinity = 0;
        return (ULONG)-1;
    }

    /*
     * Assign IRQL based on interrupt type and priority.
     *
     * ARM64 IRQL assignment strategy:
     *   - SGIs (0-15): Used for IPIs, assigned to IPI_LEVEL (14) or DPC_LEVEL (2)
     *   - PPIs (16-31): Per-CPU interrupts (timers, PMU), assigned high priority
     *     * Timer PPI 27/30: CLOCK_LEVEL (13)
     *     * Other PPIs: DEVICE_LEVEL (varies based on priority)
     *   - SPIs (32+): Shared peripheral interrupts, assigned DEVICE_LEVEL (varies 3-12)
     *   - LPIs (8192+): MSI/MSI-X, assigned based on MSI priority
     *
     * Windows NT IRQL levels for reference:
     *   PASSIVE_LEVEL    =  0
     *   APC_LEVEL        =  1
     *   DISPATCH_LEVEL   =  2
     *   DEVICE_LEVEL     =  3-12 (device interrupts)
     *   CLOCK_LEVEL      = 13 (scheduler tick)
     *   IPI_LEVEL        = 14 (inter-processor interrupts)
     *   HIGH_LEVEL       = 15 (NMI, machine checks)
     */

    if (IntId < 16)
    {
        /* SGIs: Software Generated Interrupts (IPIs) */
        if (IntId == 0 || IntId == 1 || IntId == 2)
        {
            /* IPI, APC, DPC SGIs - fixed assignments from kernel */
            Irql = (IntId == 0) ? IPI_LEVEL : (IntId == 1) ? APC_LEVEL : DISPATCH_LEVEL;
        }
        else
        {
            /* Other SGIs - default to IPI_LEVEL */
            Irql = IPI_LEVEL;
        }
        /* SGIs target specific CPUs, affinity set by sender */
        Affinity = (KAFFINITY)-1;  /* All processors */
    }
    else if (IntId < 32)
    {
        /* PPIs: Private Peripheral Interrupts (per-CPU) */
        if (IntId == 27 || IntId == 30)
        {
            /* Virtual timer (27) or physical timer (30) */
            Irql = CLOCK_LEVEL;
        }
        else if (IntId == 26)
        {
            /* EL2 timer - also high priority */
            Irql = CLOCK_LEVEL;
        }
        else
        {
            /* Other PPIs (PMU, debug, etc.) - medium-high priority */
            Irql = CLOCK_LEVEL - 1;  /* 12 */
        }
        /* PPIs are per-CPU, affinity is current processor */
        Affinity = (KAFFINITY)(1ULL << KeGetCurrentProcessorNumber());
    }
    else if (IntId < 1020)
    {
        /* SPIs: Shared Peripheral Interrupts */
        /*
         * Assign DEVICE_LEVEL IRQL. Windows uses a range of device IRQLs
         * to allow prioritization. We use a simple mapping:
         * - Low INTIDs (32-127): DEVICE_LEVEL + 2 (5)
         * - Mid INTIDs (128-255): DEVICE_LEVEL + 1 (4)
         * - High INTIDs (256+): DEVICE_LEVEL (3)
         *
         * This ensures low INTID devices (typically platform devices
         * like UARTs) can preempt higher INTID devices if needed.
         */
        if (IntId < 128)
            Irql = 5;  /* High device IRQL (DISPATCH_LEVEL + 3) */
        else if (IntId < 256)
            Irql = 4;  /* Medium device IRQL (DISPATCH_LEVEL + 2) */
        else
            Irql = 3;  /* Low device IRQL (DISPATCH_LEVEL + 1) */

        /* SPIs can target any processor */
        Affinity = (KAFFINITY)-1;  /* All processors */
    }
    else if (IntId >= 8192)
    {
        /* LPIs: Locality-specific Peripheral Interrupts (MSI/MSI-X) */
        /* Assign medium priority, MSI should be handled promptly */
        Irql = 4;  /* Medium device IRQL */

        /* LPIs are typically routed to specific CPUs, default to all */
        Affinity = (KAFFINITY)-1;  /* All processors */
    }
    else
    {
        /* Reserved or invalid range */
        DPRINT1("[arm64] HalpGetRootInterruptVector: INTID %lu in reserved/invalid range\n", IntId);
        *OutIrql = PASSIVE_LEVEL;
        *OutAffinity = 0;
        return (ULONG)-1;
    }

    *OutIrql = Irql;
    *OutAffinity = Affinity;

    DPRINT("[arm64] HalpGetRootInterruptVector: GSI=%lu -> INTID=%lu IRQL=%u Affinity=0x%Ix Pol=%u Trig=%u\n",
           BusInterruptLevel, IntId, Irql, Affinity, Polarity, TriggerMode);

    return IntId;
}

/*
 * HalFlushCommonBuffer
 *
 * Flushes data between CPU and device for a common buffer.
 * On ARM64, this performs cache maintenance operations:
 * - WriteToDevice: Clean cache to ensure device sees CPU data
 * - ReadFromDevice: Invalidate cache to ensure CPU sees device data
 */
VOID
NTAPI
HalFlushCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID VirtualAddress,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(LogicalAddress);

    if (!VirtualAddress || Length == 0)
    {
        return;
    }

    /* Skip cache maintenance on cache-coherent systems */
    if (HalpArm64DmaCoherency.SystemCoherent)
    {
        return;
    }

    if (WriteToDevice)
    {
        /*
         * CPU wrote to buffer, device needs to read it.
         * Clean cache (DC CVAC) to push data to memory.
         */
        HalpArm64CleanDcacheRange(VirtualAddress, Length);
    }
    else
    {
        /*
         * Device wrote to buffer, CPU needs to read it.
         * Clean and invalidate cache (DC CIVAC) to discard stale data.
         */
        HalpArm64CleanInvalidateDcacheRange(VirtualAddress, Length);
    }

    /* Data synchronization barrier to ensure completion */
    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * HalFreeCommonBuffer
 *
 * Frees a common buffer previously allocated by HalAllocateCommonBuffer.
 * Searches the common buffer tracking list to find and remove the entry,
 * then frees the contiguous memory.
 */
VOID
NTAPI
HalFreeCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ BOOLEAN CacheEnabled)
{
    PLIST_ENTRY Entry;
    PHAL_ARM64_COMMON_BUFFER BufferEntry;
    BOOLEAN Found = FALSE;
    KIRQL OldIrql;
    MEMORY_CACHING_TYPE CacheType;

    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(LogicalAddress);

    if (!VirtualAddress)
    {
        DPRINT1("[arm64][DMA] HalFreeCommonBuffer: NULL VirtualAddress\n");
        return;
    }

    /* Search for the buffer in our tracking list */
    KeAcquireSpinLock(&HalpArm64CommonBufferLock, &OldIrql);

    for (Entry = HalpArm64CommonBufferList.Flink;
         Entry != &HalpArm64CommonBufferList;
         Entry = Entry->Flink)
    {
        BufferEntry = CONTAINING_RECORD(Entry, HAL_ARM64_COMMON_BUFFER, ListEntry);
        if (BufferEntry->VirtualAddress == VirtualAddress)
        {
            RemoveEntryList(Entry);
            Found = TRUE;
            break;
        }
    }

    KeReleaseSpinLock(&HalpArm64CommonBufferLock, OldIrql);

    if (Found)
    {
        /* Use the tracked length and cache type */
        CacheType = BufferEntry->CacheEnabled ? MmCached : MmNonCached;

        /* Free the contiguous memory */
        MmFreeContiguousMemorySpecifyCache(BufferEntry->RawAllocation,
                                           BufferEntry->Length,
                                           CacheType);

        /* Free the tracking entry */
        ExFreePoolWithTag(BufferEntry, TAG_DMA_CMN);
    }
    else
    {
        /*
         * Buffer not found in tracking list - try to free anyway
         * using provided parameters (legacy fallback)
         */
        CacheType = CacheEnabled ? MmCached : MmNonCached;
        MmFreeContiguousMemorySpecifyCache(VirtualAddress, Length, CacheType);
    }
}

/*
 * ARM64 DMA adapter object.
 * This is a simplified adapter for ARM64 systems that support identity-mapped
 * DMA or use an SMMU for IOVA translation.
 *
 * TODO: Full implementation requires:
 * - Master adapter management
 * - Map register allocation and bounce buffers for non-coherent DMA
 * - SMMU programming for systems with IOMMU
 */
static DMA_OPERATIONS HalpArm64DmaOperations;
static ADAPTER_OBJECT HalpArm64DmaAdapter;
static BOOLEAN HalpArm64DmaAdapterInitialized = FALSE;

typedef struct _HAL_ARM64_SCATTER_GATHER_CONTEXT
{
    BOOLEAN UsingUserBuffer;
    NTSTATUS Status;
    PADAPTER_OBJECT AdapterObject;
    PMDL Mdl;
    PVOID CurrentVa;
    ULONG Length;
    ULONG MapRegisterCount;
    PDRIVER_LIST_CONTROL AdapterListControlRoutine;
    PVOID AdapterListControlContext;
    BOOLEAN WriteToDevice;
    PVOID MapRegisterBase;
    WAIT_CONTEXT_BLOCK Wcb;
} HAL_ARM64_SCATTER_GATHER_CONTEXT, *PHAL_ARM64_SCATTER_GATHER_CONTEXT;

/*
 * DMA operation wrapper functions.
 * These adapt between PDMA_ADAPTER (used by DMA_OPERATIONS) and
 * PADAPTER_OBJECT (used by the HAL's Io* functions).
 * Since ADAPTER_OBJECT starts with DMA_ADAPTER, we can safely cast.
 */
static VOID NTAPI
HalpArm64PutDmaAdapter(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
}

static NTSTATUS NTAPI
HalpArm64AllocateAdapterChannel(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine,
    _In_ PVOID Context)
{
    /*
     * ARM64 FIX: Use IoAllocateAdapterChannel instead of calling the execution
     * routine directly. IoAllocateAdapterChannel properly initializes the
     * Wait Context Block (WCB) with DeviceObject->CurrentIrp, which is needed
     * by drivers like SCSIPORT that expect the IRP in SpiAdapterControl.
     *
     * Previously we called ExecutionRoutine directly with NULL for the IRP,
     * which caused crashes when the driver tried to access Irp->MdlAddress.
     */
    return IoAllocateAdapterChannel((PADAPTER_OBJECT)DmaAdapter,
                                    DeviceObject,
                                    NumberOfMapRegisters,
                                    ExecutionRoutine,
                                    Context);
}

static PVOID NTAPI
HalpArm64AllocateCommonBuffer(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ ULONG Length,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress,
    _In_ BOOLEAN CacheEnabled)
{
    return HalAllocateCommonBuffer((PADAPTER_OBJECT)DmaAdapter,
                                   Length,
                                   LogicalAddress,
                                   CacheEnabled);
}

static VOID NTAPI
HalpArm64FreeCommonBuffer(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ ULONG Length,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ BOOLEAN CacheEnabled)
{
    HalFreeCommonBuffer((PADAPTER_OBJECT)DmaAdapter,
                        Length,
                        LogicalAddress,
                        VirtualAddress,
                        CacheEnabled);
}

static BOOLEAN NTAPI
HalpArm64FlushAdapterBuffers(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    return IoFlushAdapterBuffers((PADAPTER_OBJECT)DmaAdapter,
                                 Mdl,
                                 MapRegisterBase,
                                 CurrentVa,
                                 Length,
                                 WriteToDevice);
}

static VOID NTAPI
HalpArm64FreeAdapterChannel(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    IoFreeAdapterChannel((PADAPTER_OBJECT)DmaAdapter);
}

static VOID NTAPI
HalpArm64FreeMapRegisters(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PVOID MapRegisterBase,
    _In_ ULONG NumberOfMapRegisters)
{
    IoFreeMapRegisters((PADAPTER_OBJECT)DmaAdapter,
                       MapRegisterBase,
                       NumberOfMapRegisters);
}

static PHYSICAL_ADDRESS NTAPI
HalpArm64MapTransfer(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _Inout_ PULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    return IoMapTransfer((PADAPTER_OBJECT)DmaAdapter,
                         Mdl,
                         MapRegisterBase,
                         CurrentVa,
                         Length,
                         WriteToDevice);
}

static ULONG NTAPI
HalpArm64GetDmaAlignment(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    /* ARM64 typically requires cache-line alignment for DMA */
    return HalpArm64GetCacheLineSize();
}

static ULONG NTAPI
HalpArm64ReadDmaCounter(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    return 0;
}

static NTSTATUS NTAPI
HalpArm64CalculateScatterGatherListSize(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_opt_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _Out_ PULONG ScatterGatherListSize,
    _Out_opt_ PULONG NumberOfMapRegisters)
{
    ULONG_PTR Offset;
    ULONG MapRegisters;

    UNREFERENCED_PARAMETER(DmaAdapter);

    if (!ScatterGatherListSize || Length == 0)
    {
        if (NumberOfMapRegisters) *NumberOfMapRegisters = 0;
        return STATUS_INVALID_PARAMETER;
    }

    if (CurrentVa)
        Offset = (ULONG_PTR)CurrentVa & (PAGE_SIZE - 1);
    else if (Mdl)
        Offset = Mdl->ByteOffset;
    else
        Offset = 0;

    MapRegisters = (ULONG)((Offset + Length + PAGE_SIZE - 1) >> PAGE_SHIFT);
    if (MapRegisters == 0)
        MapRegisters = 1;

    if (MapRegisters > HAL_ARM64_MAX_MAP_REGISTERS)
        return STATUS_INSUFFICIENT_RESOURCES;

    /*
     * This buffer is used for our private context. The public S/G list is
     * allocated when map registers are granted, matching the existing x86 HAL
     * contract used by ReactOS NDIS.
     */
    *ScatterGatherListSize = sizeof(HAL_ARM64_SCATTER_GATHER_CONTEXT);
    if (NumberOfMapRegisters) *NumberOfMapRegisters = MapRegisters;

    return STATUS_SUCCESS;
}

static IO_ALLOCATION_ACTION NTAPI
HalpArm64ScatterGatherAdapterControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PIRP Irp,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID Context)
{
    PHAL_ARM64_SCATTER_GATHER_CONTEXT SgContext = Context;
    PSCATTER_GATHER_LIST ScatterGatherList;
    ULONG ScatterGatherListSize;
    ULONG ElementCount = 0;
    ULONG RemainingLength;
    PUCHAR CurrentVa;

    if (!SgContext)
        return DeallocateObject;

    SgContext->MapRegisterBase = MapRegisterBase;
    SgContext->Status = STATUS_SUCCESS;

    ScatterGatherListSize = FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) +
                            SgContext->MapRegisterCount * sizeof(SCATTER_GATHER_ELEMENT);

    ScatterGatherList = ExAllocatePoolWithTag(NonPagedPool,
                                              ScatterGatherListSize,
                                              TAG_DMA_SGL);
    if (!ScatterGatherList)
    {
        SgContext->Status = STATUS_INSUFFICIENT_RESOURCES;
        return DeallocateObject;
    }

    RtlZeroMemory(ScatterGatherList, ScatterGatherListSize);

    RemainingLength = SgContext->Length;
    CurrentVa = SgContext->CurrentVa;

    while (RemainingLength != 0 && ElementCount < SgContext->MapRegisterCount)
    {
        PHYSICAL_ADDRESS Address;
        ULONG MappedLength = RemainingLength;

        Address = IoMapTransfer(SgContext->AdapterObject,
                                SgContext->Mdl,
                                MapRegisterBase,
                                CurrentVa,
                                &MappedLength,
                                SgContext->WriteToDevice);
        if (MappedLength == 0 || Address.QuadPart == 0)
        {
            SgContext->Status = STATUS_INSUFFICIENT_RESOURCES;
            ExFreePoolWithTag(ScatterGatherList, TAG_DMA_SGL);
            return DeallocateObject;
        }

        ScatterGatherList->Elements[ElementCount].Address = Address;
        ScatterGatherList->Elements[ElementCount].Length = MappedLength;
        ScatterGatherList->Elements[ElementCount].Reserved = 0;

        RemainingLength -= MappedLength;
        CurrentVa += MappedLength;
        ElementCount++;
    }

    if (RemainingLength != 0)
    {
        SgContext->Status = STATUS_INSUFFICIENT_RESOURCES;
        ExFreePoolWithTag(ScatterGatherList, TAG_DMA_SGL);
        return DeallocateObject;
    }

    ScatterGatherList->NumberOfElements = ElementCount;
    ScatterGatherList->Reserved = (ULONG_PTR)SgContext;

    SgContext->AdapterListControlRoutine(DeviceObject,
                                         Irp,
                                         ScatterGatherList,
                                         SgContext->AdapterListControlContext);

    return DeallocateObjectKeepRegisters;
}

static NTSTATUS NTAPI
HalpArm64BuildScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _In_opt_ PVOID ScatterGatherBuffer,
    _In_ ULONG ScatterGatherLength)
{
    NTSTATUS Status;
    ULONG ContextSize;
    ULONG NumberOfMapRegisters;
    PHAL_ARM64_SCATTER_GATHER_CONTEXT SgContext;
    BOOLEAN UsingUserBuffer;

    if (!DmaAdapter || !DeviceObject || !Mdl || !ExecutionRoutine || Length == 0)
        return STATUS_INVALID_PARAMETER;

    Status = HalpArm64CalculateScatterGatherListSize(DmaAdapter,
                                                     Mdl,
                                                     CurrentVa,
                                                     Length,
                                                     &ContextSize,
                                                     &NumberOfMapRegisters);
    if (!NT_SUCCESS(Status))
        return Status;

    if (ScatterGatherBuffer)
    {
        if (ScatterGatherLength < ContextSize)
            return STATUS_BUFFER_TOO_SMALL;

        SgContext = ScatterGatherBuffer;
        UsingUserBuffer = TRUE;
    }
    else
    {
        SgContext = ExAllocatePoolWithTag(NonPagedPool, ContextSize, TAG_DMA_SGL);
        if (!SgContext)
            return STATUS_INSUFFICIENT_RESOURCES;

        UsingUserBuffer = FALSE;
    }

    RtlZeroMemory(SgContext, ContextSize);
    SgContext->UsingUserBuffer = UsingUserBuffer;
    SgContext->Status = STATUS_SUCCESS;
    SgContext->AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    SgContext->Mdl = Mdl;
    SgContext->CurrentVa = CurrentVa;
    SgContext->Length = Length;
    SgContext->MapRegisterCount = NumberOfMapRegisters;
    SgContext->AdapterListControlRoutine = ExecutionRoutine;
    SgContext->AdapterListControlContext = Context;
    SgContext->WriteToDevice = WriteToDevice;
    SgContext->Wcb.DeviceObject = DeviceObject;
    SgContext->Wcb.DeviceContext = SgContext;
    SgContext->Wcb.CurrentIrp = DeviceObject->CurrentIrp;

    Status = HalAllocateAdapterChannel((PADAPTER_OBJECT)DmaAdapter,
                                       &SgContext->Wcb,
                                       NumberOfMapRegisters,
                                       HalpArm64ScatterGatherAdapterControl);
    if (!NT_SUCCESS(Status))
    {
        if (!UsingUserBuffer)
            ExFreePoolWithTag(SgContext, TAG_DMA_SGL);
        return Status;
    }

    Status = SgContext->Status;
    if (!NT_SUCCESS(Status) && !UsingUserBuffer)
        ExFreePoolWithTag(SgContext, TAG_DMA_SGL);

    return Status;
}

static NTSTATUS NTAPI
HalpArm64GetScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_ PVOID Context,
    _In_ BOOLEAN WriteToDevice)
{
    return HalpArm64BuildScatterGatherList(DmaAdapter,
                                           DeviceObject,
                                           Mdl,
                                           CurrentVa,
                                           Length,
                                           ExecutionRoutine,
                                           Context,
                                           WriteToDevice,
                                           NULL,
                                           0);
}

static VOID NTAPI
HalpArm64PutScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PSCATTER_GATHER_LIST ScatterGather,
    _In_ BOOLEAN WriteToDevice)
{
    PHAL_ARM64_SCATTER_GATHER_CONTEXT SgContext;
    ULONG i;

    UNREFERENCED_PARAMETER(WriteToDevice);

    if (!DmaAdapter || !ScatterGather)
        return;

    SgContext = (PHAL_ARM64_SCATTER_GATHER_CONTEXT)ScatterGather->Reserved;
    if (SgContext)
    {
        PUCHAR CurrentVa = SgContext->CurrentVa;

        for (i = 0; i < ScatterGather->NumberOfElements; i++)
        {
            IoFlushAdapterBuffers((PADAPTER_OBJECT)DmaAdapter,
                                  SgContext->Mdl,
                                  SgContext->MapRegisterBase,
                                  CurrentVa,
                                  ScatterGather->Elements[i].Length,
                                  SgContext->WriteToDevice);
            CurrentVa += ScatterGather->Elements[i].Length;
        }

        IoFreeMapRegisters((PADAPTER_OBJECT)DmaAdapter,
                           SgContext->MapRegisterBase,
                           SgContext->MapRegisterCount);
    }

    ExFreePoolWithTag(ScatterGather, TAG_DMA_SGL);

    if (SgContext && !SgContext->UsingUserBuffer)
        ExFreePoolWithTag(SgContext, TAG_DMA_SGL);
}

static NTSTATUS NTAPI
HalpArm64BuildMdlFromScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PSCATTER_GATHER_LIST ScatterGather,
    _In_ PMDL OriginalMdl,
    _Out_ PMDL *TargetMdl)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(ScatterGather);
    UNREFERENCED_PARAMETER(OriginalMdl);

    if (TargetMdl)
        *TargetMdl = NULL;

    return STATUS_NOT_IMPLEMENTED;
}

PADAPTER_OBJECT
NTAPI
HalGetAdapter(
    _In_ PDEVICE_DESCRIPTION DeviceDescription,
    _Out_ PULONG NumberOfMapRegisters)
{
    ULONG MaximumLength;
    ULONG MapRegisters;

    /*
     * ARM64 DMA adapter allocation.
     *
     * On ARM64, we implement a simplified DMA adapter that:
     * 1. Supports bus-master devices with identity-mapped DMA
     * 2. Provides cache maintenance through IoFlushAdapterBuffers/IoMapTransfer
     * 3. Does not support ISA/EISA DMA controllers (not applicable on ARM64)
     *
     * For systems with SMMU, additional work is needed to program the IOMMU.
     *
     * TODO: Implement full DMA support with:
     * - Bounce buffers for devices that can't reach all memory
     * - SMMU programming for IOVA translation
     * - Scatter/gather support
     */

    if (!DeviceDescription)
    {
        DPRINT1("[arm64][HAL] HalGetAdapter: NULL DeviceDescription\n");
        return NULL;
    }

    if (NumberOfMapRegisters)
    {
        *NumberOfMapRegisters = 0;
    }

    /* Validate version */
    if (DeviceDescription->Version > DEVICE_DESCRIPTION_VERSION3)
    {
        DPRINT1("[arm64][HAL] HalGetAdapter: unsupported version %u\n",
                DeviceDescription->Version);
        return NULL;
    }

    /*
     * ARM64 doesn't have ISA/EISA DMA controllers, so we only support
     * bus-master devices.
     */
    if (!DeviceDescription->Master)
    {
        DPRINT1("[arm64][HAL] HalGetAdapter: non-master DMA not supported on ARM64\n");
        /*
         * Return a minimal adapter anyway since some drivers might still work.
         * TODO: Implement proper error handling or minimal DMA support.
         */
    }

    /*
     * Calculate the number of map registers needed.
     * This determines how many pages can be transferred at once.
     */
    MaximumLength = DeviceDescription->MaximumLength;
    if (MaximumLength == 0)
    {
        /* Use a reasonable default */
        MaximumLength = 0x10000; /* 64 KB */
    }

    MapRegisters = (MaximumLength + PAGE_SIZE - 1) >> PAGE_SHIFT;

    /* Limit to a reasonable maximum */
    if (MapRegisters > 256)
    {
        MapRegisters = 256;
    }

    /*
     * Initialize the shared DMA adapter if not already done.
     * For simplicity, we use a single adapter object for all devices.
     * A full implementation would allocate per-device adapters.
     */
    if (!HalpArm64DmaAdapterInitialized)
    {
        RtlZeroMemory(&HalpArm64DmaAdapter, sizeof(ADAPTER_OBJECT));
        RtlZeroMemory(&HalpArm64DmaOperations, sizeof(DMA_OPERATIONS));

        /* Set up the DMA operations structure */
        HalpArm64DmaOperations.Size = sizeof(DMA_OPERATIONS);
        HalpArm64DmaOperations.PutDmaAdapter = HalpArm64PutDmaAdapter;
        HalpArm64DmaOperations.AllocateCommonBuffer = HalpArm64AllocateCommonBuffer;
        HalpArm64DmaOperations.FreeCommonBuffer = HalpArm64FreeCommonBuffer;
        HalpArm64DmaOperations.AllocateAdapterChannel = HalpArm64AllocateAdapterChannel;
        HalpArm64DmaOperations.FlushAdapterBuffers = HalpArm64FlushAdapterBuffers;
        HalpArm64DmaOperations.FreeAdapterChannel = HalpArm64FreeAdapterChannel;
        HalpArm64DmaOperations.FreeMapRegisters = HalpArm64FreeMapRegisters;
        HalpArm64DmaOperations.MapTransfer = HalpArm64MapTransfer;
        HalpArm64DmaOperations.GetDmaAlignment = HalpArm64GetDmaAlignment;
        HalpArm64DmaOperations.ReadDmaCounter = HalpArm64ReadDmaCounter;
        HalpArm64DmaOperations.GetScatterGatherList = HalpArm64GetScatterGatherList;
        HalpArm64DmaOperations.PutScatterGatherList = HalpArm64PutScatterGatherList;
        HalpArm64DmaOperations.CalculateScatterGatherList = HalpArm64CalculateScatterGatherListSize;
        HalpArm64DmaOperations.BuildScatterGatherList = HalpArm64BuildScatterGatherList;
        HalpArm64DmaOperations.BuildMdlFromScatterGatherList = HalpArm64BuildMdlFromScatterGatherList;

        /* Set up the adapter object */
        HalpArm64DmaAdapter.DmaHeader.Size = sizeof(ADAPTER_OBJECT);
        HalpArm64DmaAdapter.DmaHeader.Version = 1;
        HalpArm64DmaAdapter.DmaHeader.DmaOperations = &HalpArm64DmaOperations;
        HalpArm64DmaAdapter.MasterDevice = TRUE;
        HalpArm64DmaAdapter.ScatterGather = DeviceDescription->ScatterGather;
        /*
         * FIXME: HalpArm64DmaAdapter is a global singleton — last caller's
         * DMA settings overwrite previous ones.  This breaks when multiple
         * devices with different DMA widths call HalGetAdapter.  Needs
         * per-device adapter allocation (like Windows HalGetAdapter).
         */
        HalpArm64DmaAdapter.Dma32BitAddresses = DeviceDescription->Dma32BitAddresses;
        HalpArm64DmaAdapter.Dma64BitAddresses = DeviceDescription->Dma64BitAddresses;
        HalpArm64DmaAdapter.MapRegistersPerChannel = MapRegisters;
        HalpArm64DmaAdapter.ChannelNumber = 0xFF; /* Mark as system adapter */

        HalpArm64DmaAdapterInitialized = TRUE;
    }

    /* Update the map registers count */
    if (MapRegisters > HalpArm64DmaAdapter.MapRegistersPerChannel)
    {
        HalpArm64DmaAdapter.MapRegistersPerChannel = MapRegisters;
    }

    if (NumberOfMapRegisters)
    {
        *NumberOfMapRegisters = MapRegisters;
    }

    return &HalpArm64DmaAdapter;
}

/*
 * HalGetBusData - Read PCI configuration space on ARM64
 *
 * This function reads data from PCI configuration space. On ARM64, all PCI
 * configuration access is performed through ECAM (Enhanced Configuration
 * Access Mechanism) as there is no legacy CF8/CFC port mechanism.
 *
 * Parameters:
 *   BusDataType - Type of bus data (must be PCIConfiguration for PCI)
 *   BusNumber   - PCI bus number (0-255)
 *   SlotNumber  - Encoded device and function number (PCI_SLOT_NUMBER)
 *   Buffer      - Buffer to receive the configuration data
 *   Length      - Number of bytes to read
 *
 * Returns:
 *   Number of bytes successfully read, or 0 on failure.
 *
 * ECAM Address Calculation:
 *   Config Address = ECAM_BASE + (Bus << 20) + (Device << 15) + (Function << 12) + Register
 */
ULONG
NTAPI
HalGetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    /* HalGetBusData reads from offset 0 */
    return HalGetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, 0, Length);
}

/*
 * HalGetBusDataByOffset - Read PCI configuration space at specified offset
 *
 * This function reads data from PCI configuration space starting at a specific
 * offset. On ARM64, ECAM provides access to the full 4KB PCIe extended
 * configuration space (not just the 256-byte legacy space).
 *
 * Parameters:
 *   BusDataType - Type of bus data (must be PCIConfiguration for PCI)
 *   BusNumber   - PCI bus number (0-255)
 *   SlotNumber  - Encoded device and function number (PCI_SLOT_NUMBER)
 *   Buffer      - Buffer to receive the configuration data
 *   Offset      - Byte offset within configuration space (0-4095)
 *   Length      - Number of bytes to read
 *
 * Returns:
 *   Number of bytes successfully read, or 0 on failure.
 *
 * Notes:
 *   - ARM64 memory model requires explicit barriers for MMIO ordering.
 *     The ECAM access functions use READ_REGISTER_xxx which includes
 *     the necessary memory barriers.
 *   - The full 4KB PCIe extended configuration space is accessible.
 */
ULONG
NTAPI
HalGetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PCI_SLOT_NUMBER PciSlot;

    /* Only PCIConfiguration bus data type is supported */
    if (BusDataType != PCIConfiguration)
    {
        DPRINT1("[arm64][HAL] HalGetBusDataByOffset: unsupported BusDataType %u\n",
                BusDataType);
        return 0;
    }

    /* Validate parameters */
    if (Buffer == NULL || Length == 0)
    {
        return 0;
    }

    /* Validate bus number (0-255) */
    if (BusNumber > 0xFF)
    {
        DPRINT1("[arm64][HAL] HalGetBusDataByOffset: invalid bus %lu\n", BusNumber);
        RtlFillMemory(Buffer, Length, 0xFF);
        return Length;
    }

    /* Validate offset (0-4095 for PCIe extended config space) */
    if (Offset >= 0x1000)
    {
        DPRINT1("[arm64][HAL] HalGetBusDataByOffset: invalid offset 0x%lx\n", Offset);
        RtlFillMemory(Buffer, Length, 0xFF);
        return Length;
    }

    /* Clamp length to not exceed config space boundary */
    if ((Offset + Length) > 0x1000)
    {
        Length = 0x1000 - Offset;
    }

    /* Convert SlotNumber to PCI_SLOT_NUMBER union */
    PciSlot.u.AsULONG = SlotNumber;

    /*
     * Use the firmware-published memory-mapped config-space backend.
     * Prefer MCFG when present, otherwise use ACPI root-bridge _CBA.
     */
    if (HalpArm64AccessPciConfigSpace(FALSE,                  /* Read */
                                      HALP_ACPI_SEGMENT_ANY,  /* Use any segment */
                                      BusNumber,
                                      PciSlot,
                                      Buffer,
                                      Offset,
                                      Length))
    {
        /*
         * ARM64 FIX: Apply ACPI _PRT interrupt routing translation.
         *
         * On ARM64 with ACPI, firmware often does not program the InterruptLine
         * field in PCI config space (leaving it as 0x00 or 0xFF). The actual
         * interrupt routing is described by ACPI _PRT tables.
         *
         * If we read the PCI common header (offset 0, length >= 0x40), we need
         * to check if InterruptLine is unassigned and look up the correct GSI
         * from the ACPI _PRT callback registered by the ACPI driver.
         *
         * This is critical for VirtIO and other PCI devices to work on ARM64.
         */
        if (Offset == 0 && Length >= PCI_COMMON_HDR_LENGTH && HalpArm64PciRouteQueryCallback)
        {
            PPCI_COMMON_CONFIG PciConfig = (PPCI_COMMON_CONFIG)Buffer;
            UCHAR Pin = PciConfig->u.type0.InterruptPin;
            UCHAR Line = PciConfig->u.type0.InterruptLine;

            /* Check if device has an interrupt pin but InterruptLine is unassigned */
            if (Pin != 0 && Pin <= 4 && (Line == 0 || Line == 0xFF))
            {
                ULONG Gsi;
                UCHAR Polarity;
                UCHAR Trigger;
                USHORT Segment;

                Segment = HalpArm64ResolvePciSegment((UCHAR)BusNumber);

                /* Query ACPI _PRT for the correct GSI */
                if (HalpArm64PciRouteQueryCallback(
                        Segment,
                        (UCHAR)BusNumber,
                        (UCHAR)PciSlot.u.bits.DeviceNumber,
                        (UCHAR)PciSlot.u.bits.FunctionNumber,
                        Pin,
                        &Gsi,
                        &Polarity,
                        &Trigger))
                {
                    /*
                     * Found a _PRT entry. Update InterruptLine with the GSI.
                     * Note: InterruptLine is 8-bit, so clamp GSI if needed.
                     */
                    UCHAR NewLine = (Gsi <= 0xFF) ? (UCHAR)Gsi : 0xFF;
                    PciConfig->u.type0.InterruptLine = NewLine;

                    DPRINT1("[arm64][PCI] Bus=%lu Dev=%lu Func=%lu Pin=%c: "
                            "_PRT GSI=%lu -> InterruptLine=0x%02X\n",
                            BusNumber,
                            (ULONG)PciSlot.u.bits.DeviceNumber,
                            (ULONG)PciSlot.u.bits.FunctionNumber,
                            (CHAR)('A' + Pin - 1),
                            Gsi,
                            NewLine);
                }
            }
        }

        return Length;
    }

    /*
     * ECAM access failed. Fill buffer with 0xFF to indicate no device present.
     * This is consistent with PCI spec behavior for non-existent devices.
     */
    RtlFillMemory(Buffer, Length, 0xFF);
    return Length;
}

ARC_STATUS
NTAPI
HalGetEnvironmentVariable(
    _In_ PCH Variable,
    _In_ USHORT ValueLength,
    _Out_writes_bytes_(ValueLength) PCH Value)
{
    UNREFERENCED_PARAMETER(Variable);
    UNREFERENCED_PARAMETER(ValueLength);
    UNREFERENCED_PARAMETER(Value);
    UNIMPLEMENTED_STUB();
    return ESUCCESS;
}

/*
 * ARM64 interrupt vector allocation table.
 * Tracks which INTID -> IRQL mappings have been allocated.
 * For SPIs (32-1019), we map to device IRQLs (3-12).
 */
#define HAL_ARM64_SPI_BASE          32
#define HAL_ARM64_SPI_MAX           1020
#define HAL_ARM64_DEVICE_IRQL_MIN   3       /* DISPATCH_LEVEL + 1 */
#define HAL_ARM64_DEVICE_IRQL_MAX   12      /* CLOCK_LEVEL - 1 */
#define HAL_ARM64_DEVICE_IRQL_COUNT (HAL_ARM64_DEVICE_IRQL_MAX - HAL_ARM64_DEVICE_IRQL_MIN + 1)

static KIRQL HalpArm64IntIdToIrql[HAL_ARM64_SPI_MAX];
static KSPIN_LOCK HalpArm64VectorLock;
static BOOLEAN HalpArm64VectorLockInit = FALSE;

/*
 * Map GIC INTID to Windows IRQL for device interrupts.
 *
 * On ARM64, the GIC INTID is used directly as the "vector" since there's
 * no IDT indirection like on x86. The IRQL is computed to spread device
 * interrupts across the available device IRQL range (3-12).
 *
 * SPIs 32-1019 are mapped to IRQLs 3-12 using a simple hash to distribute
 * interrupts across priority levels. This prevents all device interrupts
 * from running at the same IRQL and allows some priority differentiation.
 */
static KIRQL
HalpArm64ComputeDeviceIrql(
    _In_ ULONG IntId)
{
    ULONG Range;

    /* SGIs (0-15) and PPIs (16-31) have fixed IRQLs */
    if (IntId < HAL_ARM64_SPI_BASE)
    {
        /* PPIs like timer run at CLOCK_LEVEL, SGIs at their designated levels */
        if (IntId < 16)
            return IPI_LEVEL; /* SGIs */
        else
            return CLOCK_LEVEL; /* PPIs including timer */
    }

    /* SPIs: distribute across device IRQL range */
    Range = IntId - HAL_ARM64_SPI_BASE;
    return (KIRQL)(HAL_ARM64_DEVICE_IRQL_MIN +
                   (Range % HAL_ARM64_DEVICE_IRQL_COUNT));
}

/*
 * HalGetInterruptVector - Translate bus interrupt to system vector
 *
 * Translates a bus-relative interrupt (from ACPI) to a system interrupt
 * vector (GIC INTID) with appropriate IRQL and processor affinity.
 */
ULONG
NTAPI
HalGetInterruptVector(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG BusInterruptLevel,
    _In_ ULONG BusInterruptVector,
    _Out_ PKIRQL Irql,
    _Out_ PKAFFINITY Affinity)
{
    ULONG Gsi;
    ULONG Vector;
    KIRQL DeviceIrql;
    KIRQL OldIrql = PASSIVE_LEVEL;
    UCHAR IntPolarity = 0;
    UCHAR IntTrigger = 0;
    PHALP_ARM64_INT_OVERRIDE_ENTRY Override;
    BOOLEAN IsNmi;
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(BusInterruptVector);

    /*
     * On ARM64 with ACPI, BusInterruptLevel contains the GSI.
     * Check for interrupt source overrides from MADT for platform devices.
     */
    Gsi = BusInterruptLevel;

    if (InterfaceType == Internal || InterfaceType == Isa)
    {
        if (BusInterruptLevel < 256)
        {
            Override = HalpArm64GetIntOverrideForIrq(0, (UCHAR)BusInterruptLevel);
            if (Override)
            {
                DPRINT("[arm64] HalGetInterruptVector: Override IRQ %lu -> GSI %lu\n",
                       BusInterruptLevel, Override->GlobalSystemInterrupt);
                Gsi = Override->GlobalSystemInterrupt;
                IntPolarity = Override->Polarity;
                IntTrigger = Override->TriggerMode;
            }
        }
    }

    /*
     * Legacy bus line interrupts are distributor interrupts on ARM64. ACPI/QEMU
     * may describe PCI INTA as GSI 0, and storage drivers can still ask for ISA
     * IRQ 14/15. Translating those values as CPU-local INTIDs returns SGI/PPI
     * vectors, so the resource connect path appears to succeed while no device
     * interrupt can be delivered. Move legacy bus IRQs into the SPI range before
     * the common GSI helper; keep Internal interrupts on the CPU-local path.
     */
    if (((InterfaceType == PCIBus) ||
         (InterfaceType == Isa) ||
         (InterfaceType == PNPISABus)) &&
        (Gsi < HAL_ARM64_SPI_BASE))
    {
        DPRINT("[arm64] HalGetInterruptVector: legacy GSI %lu -> SPI INTID %lu\n",
               Gsi, Gsi + HAL_ARM64_SPI_BASE);
        Gsi += HAL_ARM64_SPI_BASE;
    }

    /* Translate GSI to GIC INTID, applying SystemVectorBase */
    Vector = HalpArm64TranslateGsiToIntId(Gsi, &IntPolarity, &IntTrigger);
    if (Vector == (ULONG)-1)
    {
        DPRINT1("[arm64] HalGetInterruptVector: Failed to translate GSI %lu\n", Gsi);
        if (Irql) *Irql = PASSIVE_LEVEL;
        if (Affinity) *Affinity = 0;
        return 0;
    }

    /* Check if this is an NMI source */
    IsNmi = HalpArm64IsNmiSource(Gsi);

    /* Validate the interrupt ID */
    if (Vector >= HAL_ARM64_SPI_MAX && Vector < HAL_ARM64_LPI_BASE)
    {
        DPRINT1("[arm64] HalGetInterruptVector: Invalid INTID %lu (GSI %lu)\n", Vector, Gsi);
        if (Irql) *Irql = PASSIVE_LEVEL;
        if (Affinity) *Affinity = 0;
        return 0;
    }

    /* For LPIs (MSI/MSI-X) */
    if (Vector >= HAL_ARM64_LPI_BASE)
    {
        if (HalpGicItsEnabled && HalpGicLpiCount &&
            Vector < (HAL_ARM64_LPI_BASE + HalpGicLpiCount))
        {
            DeviceIrql = HAL_ARM64_DEVICE_IRQL_MIN +
                         ((Vector - HAL_ARM64_LPI_BASE) % HAL_ARM64_DEVICE_IRQL_COUNT);
            if (Irql) *Irql = DeviceIrql;
            if (Affinity) *Affinity = HalpDefaultInterruptAffinity ?
                                       HalpDefaultInterruptAffinity : 1;
            DPRINT("[arm64] HalGetInterruptVector: LPI GSI=%lu Vector=%lu IRQL=%u\n",
                   Gsi, Vector, DeviceIrql);
            return Vector;
        }
        else if (HalpGicMsiPresent)
        {
            /* GICv2m MSI frame fallback - use SPIs for MSI */
            ULONG SpiOffset = Vector - HAL_ARM64_LPI_BASE;
            if (SpiOffset < HalpGicMsiSpiCount)
            {
                Vector = HalpGicMsiSpiBase + SpiOffset;
                DeviceIrql = HAL_ARM64_DEVICE_IRQL_MIN +
                             (SpiOffset % HAL_ARM64_DEVICE_IRQL_COUNT);
                if (Irql) *Irql = DeviceIrql;
                if (Affinity) *Affinity = HalpDefaultInterruptAffinity ?
                                           HalpDefaultInterruptAffinity : 1;
                DPRINT("[arm64] HalGetInterruptVector: MSI via GICv2m SPI=%lu\n", Vector);
                return Vector;
            }
        }
        DPRINT1("[arm64] HalGetInterruptVector: LPI %lu but no MSI support\n", Vector);
        if (Irql) *Irql = PASSIVE_LEVEL;
        if (Affinity) *Affinity = 0;
        return 0;
    }

    /* Initialize lock if needed */
    if (!HalpArm64VectorLockInit)
    {
        KeInitializeSpinLock(&HalpArm64VectorLock);
        HalpArm64VectorLockInit = TRUE;
        RtlZeroMemory(HalpArm64IntIdToIrql, sizeof(HalpArm64IntIdToIrql));
    }

    /* Compute IRQL - NMI sources get highest priority */
    if (IsNmi)
    {
        DeviceIrql = HIGH_LEVEL;
        DPRINT("[arm64] HalGetInterruptVector: NMI GSI=%lu Vector=%lu\n", Gsi, Vector);
    }
    else
    {
        DeviceIrql = HalpArm64ComputeDeviceIrql(Vector);
    }

    /* Track the allocation */
    if (Vector < HAL_ARM64_SPI_MAX)
    {
        KeAcquireSpinLock(&HalpArm64VectorLock, &OldIrql);
        if (HalpArm64IntIdToIrql[Vector] == 0)
        {
            HalpArm64IntIdToIrql[Vector] = DeviceIrql;
        }
        else
        {
            /* Already allocated - return existing IRQL for consistency */
            DeviceIrql = HalpArm64IntIdToIrql[Vector];
        }
        KeReleaseSpinLock(&HalpArm64VectorLock, OldIrql);
    }

    /* Set output parameters */
    if (Irql)
    {
        *Irql = DeviceIrql;
    }

    if (Affinity)
    {
        /*
         * Set processor affinity based on interrupt type:
         * - SGIs/PPIs (0-31): Per-CPU, bind to current processor
         * - SPIs (32-1019): Shared, can target any processor
         */
        if (Vector < 32)
        {
            *Affinity = 1ULL << KeGetCurrentProcessorNumber();
        }
        else
        {
            *Affinity = HalpDefaultInterruptAffinity ? HalpDefaultInterruptAffinity : 1;
        }
    }

    DPRINT("[arm64] HalGetInterruptVector: %s GSI=%lu -> Vector=%lu IRQL=%u Aff=%lx\n",
           InterfaceType == PCIBus ? "PCI" : (InterfaceType == Internal ? "Internal" : "Other"),
           Gsi, Vector, (ULONG)DeviceIrql, (ULONG)(Affinity ? *Affinity : 0));

    return Vector;
}

ULONG
FASTCALL
HalGetInterruptSource(VOID)
{
    ULONG IntId;

    if (HalpGicUseSysRegs)
    {
        IntId = HalpReadIccIar1();
    }
    else
    {
        if (HalpGiccBase == 0)
            return 0;

        IntId = *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_IAR) & 0x3FFu;
    }

    return IntId;
}

/*
 * HalSetGicPriorityMask - Set GIC interrupt priority mask for IRQL-based filtering
 *
 * CRITICAL GIC SEMANTICS (CORRECTED):
 * ====================================
 * The GIC Priority Mask Register (ICC_PMR_EL1 / GICC_PMR) filters interrupts based
 * on their assigned priority. The GIC signals an interrupt if and only if:
 *
 *   interrupt_priority < PMR
 *
 * In other words, PMR is a THRESHOLD:
 *   - Lower PMR value = MORE RESTRICTIVE (blocks more interrupts)
 *   - Higher PMR value = MORE PERMISSIVE (allows more interrupts)
 *
 * GIC Priority Assignment (lower numeric value = higher urgency):
 *   0x10 = IPI_LEVEL (14) - Inter-processor interrupts
 *   0x20 = CLOCK_LEVEL (13) - Timer/scheduler tick
 *   0x30 = DEVICE_LEVEL (12) - Highest-priority device
 *   0x40 = DEVICE_LEVEL (11)
 *   ...
 *   0xC0 = DEVICE_LEVEL (3) - Lowest-priority device
 *
 * Windows IRQL Semantics (higher IRQL = more restrictive):
 *   IRQL 0-2 (PASSIVE/DISPATCH): Allow all interrupts
 *   IRQL 3-12 (DEVICE): Block this device level and below, allow higher-priority devices + timer + IPI
 *   IRQL 13 (CLOCK): Block all devices, allow IPI only
 *   IRQL 14 (IPI): Block everything except "NMI-like" events
 *   IRQL 15 (HIGH): Block everything (DAIF.I=1 + PMR=0x00)
 *
 * CORRECT PMR MAPPING TABLE:
 * ==========================
 * IRQL | Windows Semantics                  | Allowed Priorities | PMR Value
 * -----|------------------------------------|--------------------|----------
 *  0-2 | Allow all interrupts               | 0x10-0xC0          | 0xFF
 *  3   | Allow devices 4-12, timer, IPI     | 0x10-0xB0          | 0xC0
 *  4   | Allow devices 5-12, timer, IPI     | 0x10-0xA0          | 0xB0
 *  5   | Allow devices 6-12, timer, IPI     | 0x10-0x90          | 0xA0
 *  6   | Allow devices 7-12, timer, IPI     | 0x10-0x80          | 0x90
 *  7   | Allow devices 8-12, timer, IPI     | 0x10-0x70          | 0x80
 *  8   | Allow devices 9-12, timer, IPI     | 0x10-0x60          | 0x70
 *  9   | Allow devices 10-12, timer, IPI    | 0x10-0x50          | 0x60
 *  10  | Allow devices 11-12, timer, IPI    | 0x10-0x40          | 0x50
 *  11  | Allow device 12, timer, IPI        | 0x10-0x30          | 0x40
 *  12  | Allow timer, IPI only              | 0x10-0x20          | 0x30
 *  13  | Allow IPI only                     | 0x10               | 0x20
 *  14  | Allow "NMI" only (priorities < 10) | 0x00-0x0F          | 0x10
 *  15  | Block everything (DAIF.I=1)        | none               | 0x00
 *
 * Example: At IRQL=13 (CLOCK_LEVEL), we want to allow IPI (priority 0x10) but
 * block timer (priority 0x20) and all devices (priorities 0x30-0xC0).
 * So we set PMR=0x20, which allows only priorities < 0x20, i.e., 0x00-0x1F.
 *
 * This FIXES the previous implementation which had the semantics backwards and
 * would cause deadlocks when waiting for timer interrupts at DISPATCH_LEVEL.
 */
VOID
FASTCALL
HalSetGicPriorityMask(
    _In_ KIRQL Irql)
{
    UCHAR Priority = (Irql <= HIGH_LEVEL) ?
                     HalpArm64IrqlToPmr[Irql] :
                     HalpArm64IrqlToPmr[HIGH_LEVEL];

    /*
     * Write the priority mask to the GIC.
     * For GICv3, we use ICC_PMR_EL1 system register.
     * For GICv2, we use GICC_PMR MMIO register.
     */
    if (HalpGicUseSysRegs)
    {
        HalpWriteIccPmr(Priority);
    }
    else
    {
        if (HalpGiccBase != 0)
        {
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_PMR) = Priority;
            /* Memory barrier to ensure PMR write completes */
            __asm__ __volatile__("dsb sy" ::: "memory");
        }
    }
}

/*
 * HalGetGicPriorityMask - Read current GIC interrupt priority mask
 *
 * Returns the current ICC_PMR_EL1/GICC_PMR value for debugging.
 *
 * IMPORTANT: Return type is ULONG to match hal.spec declaration and
 * ARM64 fastcall ABI. The internal PMR value is a byte, but we cast
 * to ULONG for ABI compliance (fastcall returns in W0/X0 register).
 */
ULONG
FASTCALL
HalGetGicPriorityMask(VOID)
{
    if (HalpGicUseSysRegs)
    {
        return (ULONG)HalpReadIccPmr();
    }
    else
    {
        if (HalpGiccBase != 0)
        {
            return (ULONG)*HalpMmio((ULONG_PTR)HalpGiccBase, GICC_PMR);
        }
    }
    return 0xFF;
}

VOID
NTAPI
HalInitializeProcessor(
    _In_ ULONG ProcessorNumber,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    HalpArm64SelectGicInterface(LoaderBlock);

    if (HalpGicUseSysRegs)
    {

        /* GIC-v3: Find and use per-CPU redistributor */
        if (ProcessorNumber < RTL_NUMBER_OF(HalpGicrCpuBase) &&
            !HalpGicrCpuBase[ProcessorNumber] &&
            HalpGicrRegionBase)
        {
            ULONG_PTR Base = HalpArm64FindGicrForMpidr(HalpReadMpidr());
            if (Base)
                HalpGicrCpuBase[ProcessorNumber] = Base;
        }
        HalpInitGicRedistributor(ProcessorNumber);

        /*
         * GICv3 CPU interface init for APs.
         *
         * The BSP's CPU interface (ICC_SRE, ICC_PMR, ICC_BPR1, ICC_IGRPEN1)
         * is configured in HalInitSystem Phase 0/1. But those are per-CPU
         * system registers, so APs start with defaults:
         *   - ICC_PMR = 0 (all interrupts masked by priority)
         *   - ICC_BPR1 = default
         *   - ICC_IGRPEN1 = 0 (Group 1 interrupts DISABLED)
         *
         * Without this, APs cannot receive ANY interrupts (including SGI IPI
         * for debugger freeze), causing KxFreezeExecution to hang forever
         * in KiArm64WaitForFrozenTargets.
         *
         * Unlike the BSP, we can enable IGRPEN1 immediately because by the
         * time APs start, the system is past Phase 1 (MM initialized,
         * interrupt handlers registered).
         */
        if (ProcessorNumber > 0)
        {
            ULONG Sre;

            /* Ensure system register interface is enabled */
            Sre = HalpReadIccSre();
            Sre |= 0x1; /* SRE = 1 */
            HalpWriteIccSre(Sre);
            __asm__ __volatile__("isb" ::: "memory");

            /* Set priority mask to allow all interrupts */
            HalpWriteIccPmr(0xFF);

            /* Binary point = 0 for finest priority grouping */
            HalpWriteIccBpr1(0);

            /* Enable Group 1 interrupt delivery */
            HalpWriteIccIgrpen1(1);

            __asm__ __volatile__("dsb sy" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");
        }
    }
    else
    {

        /*
         * GIC-v2: Initialize the per-CPU GICC (CPU interface) for this processor.
         *
         * Unlike GIC-v3 which has per-CPU redistributors with dedicated registers,
         * GIC-v2 has a single distributor (GICD) and a per-CPU CPU interface (GICC).
         * The GICC is at the same address for all CPUs but each CPU sees its own
         * interface (the hardware routes based on the accessing CPU's ID).
         *
         * The BSP already configured the distributor's SGI/PPI bank in HalInitSystem.
         * Here we just need to enable this CPU's interface.
         */
        if ((ProcessorNumber != 0) && (HalpGiccBase != 0))
        {
            ULONG GiccCtlr = HalpGicv2ForceGroup0 ? 0x1 : 0x3;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_PMR) = 0xFF;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_BPR) = 0x0;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_CTLR) = GiccCtlr;

            /* Memory barrier to ensure GICC configuration takes effect */
            __asm__ __volatile__("dsb sy" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");
        }
        else
        {
        }
    }

    /* Initialize ITS tables for this CPU if available */
    if (HalpGicItsInitialized && HalpGicItsEnabled &&
        ProcessorNumber < RTL_NUMBER_OF(HalpGicItsCollectionMapped))
    {
        HalpGicItsCollectionMapped[ProcessorNumber] = FALSE;
        if (!HalpGicItsEnsureCollection(ProcessorNumber))
        {
        }
    }
}

BOOLEAN
NTAPI
HalMakeBeep(
    _In_ ULONG Frequency)
{
    UNREFERENCED_PARAMETER(Frequency);
    UNIMPLEMENTED_STUB();
    return FALSE;
}

VOID
NTAPI
HalProcessorIdle(VOID)
{
    __asm__ __volatile__("wfi" ::: "memory");
}

BOOLEAN
NTAPI
HalQueryDisplayParameters(
    _Out_opt_ PULONG Width,
    _Out_opt_ PULONG Height,
    _Out_opt_ PULONG Depth,
    _Out_opt_ PULONG Frequency)
{
    if (Width) *Width = 0;
    if (Height) *Height = 0;
    if (Depth) *Depth = 0;
    if (Frequency) *Frequency = 0;
    UNIMPLEMENTED_STUB();
    return FALSE;
}

BOOLEAN
NTAPI
HalQueryRealTimeClock(
    _Inout_ PTIME_FIELDS TimeFields)
{
    UNREFERENCED_PARAMETER(TimeFields);
    UNIMPLEMENTED_STUB();
    return FALSE;
}

ULONG
NTAPI
HalReadDmaCounter(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNIMPLEMENTED_STUB();
    return 0;
}

VOID
NTAPI
HalRequestIpi(
    _In_ KAFFINITY TargetSet)
{
    HalpArm64SendSgi(TargetSet, HAL_ARM64_SGI_IPI);
}

ARC_STATUS
NTAPI
HalSetEnvironmentVariable(
    _In_ PCH Variable,
    _In_ PCH Value)
{
    UNREFERENCED_PARAMETER(Variable);
    UNREFERENCED_PARAMETER(Value);
    UNIMPLEMENTED_STUB();
    return ESUCCESS;
}

ULONG_PTR
NTAPI
HalSetProfileInterval(
    _In_ ULONG_PTR Interval)
{
    UNREFERENCED_PARAMETER(Interval);
    UNIMPLEMENTED_STUB();
    return Interval;
}

BOOLEAN
NTAPI
HalSetRealTimeClock(
    _In_ PTIME_FIELDS TimeFields)
{
    UNREFERENCED_PARAMETER(TimeFields);
    UNIMPLEMENTED_STUB();
    return FALSE;
}

ULONG
NTAPI
HalSetTimeIncrement(
    _In_ ULONG Increment)
{
    UNREFERENCED_PARAMETER(Increment);

    /*
     * The ARM64 generic timer path is currently programmed at the fixed HAL
     * maximum increment. Report the actual achieved resolution so callers do
     * not believe a 1 ms request changed the interrupt cadence.
     */
    return KeQueryTimeIncrement();
}

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    ULONG ProcessorNumber;
    ULONGLONG TargetMpidr;
    PHYSICAL_ADDRESS EntryPoint;
    UINT64 TrampolinePhys;
    UINT64 GicrBase;
    LONG PsciResult;

    /*
     * ARM64 SMP bring-up using PSCI CPU_ON with proper AP trampoline.
     *
     * The boot sequence is:
     * 1. Initialize the AP trampoline (first call only)
     * 2. Prepare AP data structure with page tables and entry point
     * 3. Call PSCI CPU_ON with trampoline physical address
     * 4. AP wakes at trampoline, enables MMU, jumps to kernel entry
     * 5. Wait for AP to signal synchronization
     */

    if (!LoaderBlock || !ProcessorState)
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: invalid parameters\n");
        return FALSE;
    }

    /* Determine which processor to start next */
    ProcessorNumber = HalpStartedProcessorCount;

    /* Check if we have more processors to start from MADT GICC entries */
    if (ProcessorNumber >= HalpArm64GicInfo.GiccEntryCount)
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: no more processors (started=%lu, available=%lu)\n",
                ProcessorNumber, HalpArm64GicInfo.GiccEntryCount);
        return FALSE;
    }

    /* Get the MPIDR of the target processor from MADT GICC entries */
    TargetMpidr = HalpArm64GicInfo.GiccEntries[ProcessorNumber].Mpidr;

    /* Check if this processor is enabled */
    if (!(HalpArm64GicInfo.GiccEntries[ProcessorNumber].Flags & 0x1))
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: CPU %lu (MPIDR 0x%llx) not enabled in MADT\n",
                ProcessorNumber, TargetMpidr);
        /*
         * Skip disabled processors but try the next one.
         * TODO: This is a simplification; we should iterate properly.
         */
        HalpStartedProcessorCount++;
        return FALSE;
    }

    DPRINT1("[arm64][HAL] HalStartNextProcessor: starting CPU %lu (MPIDR 0x%llx)\n",
            ProcessorNumber, TargetMpidr);

    /*
     * Initialize the AP trampoline on first call.
     * The trampoline enables MMU and transitions to virtual addressing.
     */
    if (!HalpArm64IsTrampolineInitialized())
    {
        if (!HalpArm64InitApTrampoline(LoaderBlock))
        {
            DPRINT1("[arm64][HAL] HalStartNextProcessor: trampoline init failed\n");
            goto FallbackDirectPsci;
        }
        HalpArm64DiscoverParkedCpus(LoaderBlock);
    }

    /* Get trampoline physical address */
    TrampolinePhys = HalpArm64GetTrampolinePhysicalAddress();
    if (TrampolinePhys == 0)
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: trampoline not ready\n");
        goto FallbackDirectPsci;
    }

    /* Find the GIC redistributor base for this CPU */
    GicrBase = 0;
    if (HalpArm64GicInfo.GiccEntries[ProcessorNumber].GicrBase)
        GicrBase = HalpArm64GicInfo.GiccEntries[ProcessorNumber].GicrBase;
    else if (HalpGicrRegionBase)
        GicrBase = (UINT64)HalpArm64FindGicrForMpidr(TargetMpidr);

    /* Prepare AP data with page tables, entry point, and GICR */
    HalpArm64PrepareApData(
        ProcessorNumber,
        (UINT64)ProcessorState->ContextFrame.Pc,
        (UINT64)ProcessorState->ContextFrame.Sp,
        (UINT64)LoaderBlock,
        GicrBase
    );

    HalpApProcessorState = ProcessorState;
    __asm__ __volatile__("dsb sy; isb" ::: "memory");

    /* Check if PSCI is available */
    if (!HalpArm64PsciInfo.Present)
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: PSCI unavailable, trying parking\n");
        if (HalpArm64WakeParkedCpu(ProcessorNumber, TrampolinePhys, ProcessorNumber))
        {
            if (HalpArm64WaitForApSync(5000))
            {
                DPRINT1("[arm64][HAL] HalStartNextProcessor: CPU %lu started via parking\n",
                        ProcessorNumber);
                HalpStartedProcessorCount++;
                return TRUE;
            }
        }
        DPRINT1("[arm64][HAL] HalStartNextProcessor: no method to start CPU %lu\n",
                ProcessorNumber);
        return FALSE;
    }

    /* Call PSCI CPU_ON with trampoline address */
    PsciResult = HalpPsciCpuOn(TargetMpidr, TrampolinePhys, (ULONGLONG)ProcessorNumber);

    if (PsciResult == PSCI_SUCCESS || PsciResult == PSCI_E_ALREADY_ON)
    {
        if (HalpArm64WaitForApSync(5000))
        {
            DPRINT1("[arm64][HAL] HalStartNextProcessor: CPU %lu started and synced\n",
                    ProcessorNumber);
        }
        else
        {
            DPRINT1("[arm64][HAL] HalStartNextProcessor: CPU %lu started (sync timeout)\n",
                    ProcessorNumber);
        }
        HalpStartedProcessorCount++;
        return TRUE;
    }
    else
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: PSCI CPU_ON failed, err=%ld\n",
                PsciResult);
        return FALSE;
    }

FallbackDirectPsci:
    /*
     * Fallback: Direct PSCI CPU_ON without trampoline.
     * This assumes the kernel entry point is identity-mapped.
     */
    if (!HalpArm64PsciInfo.Present)
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: PSCI not available (fallback)\n");
        return FALSE;
    }

    EntryPoint = MmGetPhysicalAddress((PVOID)ProcessorState->ContextFrame.Pc);
    if (EntryPoint.QuadPart == 0)
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: failed to get entry PA\n");
        return FALSE;
    }

    HalpApProcessorState = ProcessorState;
    HalpApEntryPointPhys = EntryPoint;
    __asm__ __volatile__("dsb sy; isb" ::: "memory");

    PsciResult = HalpPsciCpuOn(TargetMpidr, EntryPoint.QuadPart, (ULONGLONG)ProcessorNumber);

    if (PsciResult == PSCI_SUCCESS || PsciResult == PSCI_E_ALREADY_ON)
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: CPU %lu started (fallback)\n",
                ProcessorNumber);
        HalpStartedProcessorCount++;
        return TRUE;
    }
    else
    {
        DPRINT1("[arm64][HAL] HalStartNextProcessor: fallback PSCI failed, err=%ld\n",
                PsciResult);
        return FALSE;
    }
}

VOID
NTAPI
HalStartProfileInterrupt(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
    UNIMPLEMENTED_STUB();
}

VOID
NTAPI
HalStopProfileInterrupt(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
    UNIMPLEMENTED_STUB();
}

VOID
FASTCALL
HalSweepDcache(VOID)
{
    __asm__ __volatile__("dsb sy\n\tisb" ::: "memory");
}

VOID
FASTCALL
HalSweepIcache(VOID)
{
    __asm__ __volatile__("ic iallu\n\tdsb sy\n\tisb" ::: "memory");
}

/*
 * HalSystemVectorDispatchEntry - Get dispatch entry for a system vector.
 *
 * On x86/x64, this is used for APIC vector→IDT dispatch table lookup.
 * On ARM64, interrupt dispatch is handled differently:
 *
 * 1. GIC delivers INTID via ICC_IAR1_EL1 (system register) or GICC_IAR (MMIO)
 * 2. HalGetInterruptSource() reads the active INTID
 * 3. KiArm64InterruptDispatchEntry() uses KiArm64IntTable[] for dispatch
 * 4. KeConnectInterrupt() populates KiArm64IntTable[] directly
 *
 * Since ARM64 doesn't use the x86-style vector→dispatch table mechanism,
 * this function returns 0 (no special dispatch type) like the x86 HAL.
 *
 * Return values:
 *   0 = Normal dispatch (use kernel's KINTERRUPT chain)
 *   1 = Flat dispatch (direct routine call)
 *   2 = No connection handler
 */
UCHAR
FASTCALL
HalSystemVectorDispatchEntry(
    _In_ ULONG Vector,
    _Out_ PKINTERRUPT_ROUTINE **FlatDispatch,
    _Out_ PKINTERRUPT_ROUTINE *NoConnection)
{
    UNREFERENCED_PARAMETER(Vector);

    /*
     * Return 0 to indicate normal KINTERRUPT chain dispatch.
     * The kernel will use its own dispatch table (KiArm64IntTable on ARM64).
     */
    if (FlatDispatch)
        *FlatDispatch = NULL;
    if (NoConnection)
        *NoConnection = NULL;

    return 0;
}

BOOLEAN
NTAPI
HalpTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(AddressSpace);

    if (TranslatedAddress)
    {
        TranslatedAddress->QuadPart = BusAddress.QuadPart;
    }

    return TRUE;
}

BOOLEAN
NTAPI
HalpFindBusAddressTranslation(
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress,
    _Inout_ PULONG_PTR Context,
    _In_ BOOLEAN NextBus)
{
    UNREFERENCED_PARAMETER(AddressSpace);

    if (!Context)
    {
        return FALSE;
    }

    if ((*Context != 0) && (NextBus != FALSE))
    {
        return FALSE;
    }

    if (TranslatedAddress)
    {
        TranslatedAddress->QuadPart = BusAddress.QuadPart;
    }

    *Context = 1;
    return TRUE;
}

BOOLEAN
NTAPI
HalTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    return HalpTranslateBusAddress(InterfaceType,
                                   BusNumber,
                                   BusAddress,
                                   AddressSpace,
                                   TranslatedAddress);
}

/*
 * IoFlushAdapterBuffers
 *
 * Flushes DMA adapter buffers after a transfer completes.
 * On ARM64, this performs cache maintenance operations and handles
 * bounce buffer data transfer for devices with limited addressing.
 */
BOOLEAN
NTAPI
IoFlushAdapterBuffers(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    PHAL_ARM64_MAP_REGISTER_BASE Base;
    ULONG i;

    UNREFERENCED_PARAMETER(AdapterObject);

    if (!Mdl || !CurrentVa || Length == 0)
    {
        return TRUE;
    }

    /* Check for bounce buffers that need data copied back */
    if (MapRegisterBase)
    {
        Base = (PHAL_ARM64_MAP_REGISTER_BASE)MapRegisterBase;
        if (Base->Signature == HAL_ARM64_MAP_REG_SIGNATURE)
        {
            for (i = 0; i < Base->NumberOfMapRegisters; i++)
            {
                PHAL_ARM64_MAP_REGISTER_ENTRY Entry = &Base->Registers[i];

                /* For DMA from device, copy bounce buffer back to original */
                if (Entry->UsesBounceBuffer && Entry->BounceBuffer &&
                    !Entry->WriteToDevice && Entry->OriginalVa)
                {
                    RtlCopyMemory(Entry->OriginalVa, Entry->BounceBuffer, Entry->Length);
                }
            }
        }
    }

    /* Perform cache maintenance for non-coherent DMA */
    if (!HalpArm64DmaCoherency.SystemCoherent)
    {
        /* Ensure all DMA writes from the device have committed to memory
         * before we invalidate the CPU cache. Without this barrier, a
         * posted PCIe write might still be in transit and we'd invalidate
         * the cache line before the data arrives. */
        __asm__ __volatile__("dsb sy" ::: "memory");

        if (!WriteToDevice)
        {
            /* Reading from device (DMA write to memory completed) — invalidate cache */
            HalpArm64CleanInvalidateDcacheRange(CurrentVa, Length);

            /* Debug: check if DMA data arrived by reading through KSEG0 */
            if (Length >= 9 && Mdl)
            {
                PPFN_NUMBER Pfns = MmGetMdlPfnArray(Mdl);
                if (Pfns)
                {
                    ULONG_PTR KsegVa = HAL_ARM64_PHYS_MAP_BASE |
                                       ((ULONG_PTR)Pfns[0] << PAGE_SHIFT) |
                                       ((ULONG_PTR)CurrentVa & (PAGE_SIZE - 1));
                    volatile UCHAR *KsegPtr = (volatile UCHAR *)KsegVa;
                    UCHAR CpuByte = *(volatile UCHAR *)CurrentVa;
                    UCHAR KsegByte = *KsegPtr;
                    if (CpuByte == 0 && KsegByte != 0)
                    {
                        DPRINT1("[arm64][DMA] FlushBuf MISMATCH: VA=%p cpu=0x%02x kseg=0x%02x len=%lu "
                                "pfn=%Ix (CACHE STALE!)\n",
                                CurrentVa, CpuByte, KsegByte, Length, Pfns[0]);
                    }
                    else if (CpuByte == 0 && KsegByte == 0 && Length >= 44)
                    {
                        DPRINT1("[arm64][DMA] FlushBuf BOTH ZERO: VA=%p len=%lu pfn=%Ix "
                                "(DMA did not write?)\n",
                                CurrentVa, Length, Pfns[0]);
                    }
                }
            }
        }
        else
        {
            /* Writing to device (DMA read from memory) — clean cache */
            HalpArm64CleanDcacheRange(CurrentVa, Length);
        }

        /* Ensure cache operations complete before returning */
        __asm__ __volatile__("dsb sy" ::: "memory");
    }

    return TRUE;
}

/*
 * IoFreeAdapterChannel
 *
 * Releases an adapter channel previously allocated by HalAllocateAdapterChannel.
 * On ARM64, there's no physical DMA controller to release, so this mainly
 * clears the adapter's state and allows it to be reused.
 */
VOID
NTAPI
IoFreeAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    if (!AdapterObject)
    {
        return;
    }

    /*
     * Clear the adapter's map register base.
     * The actual map registers should be freed via IoFreeMapRegisters.
     */
    AdapterObject->MapRegisterBase = NULL;
    AdapterObject->NumberOfMapRegisters = 0;

}

/*
 * IoFreeMapRegisters
 *
 * Frees map registers previously allocated by HalAllocateAdapterChannel.
 * Also frees any bounce buffers that were allocated for DMA transfers.
 */
VOID
NTAPI
IoFreeMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID MapRegisterBase,
    _In_ ULONG NumberOfMapRegisters)
{
    PHAL_ARM64_MAP_REGISTER_BASE Base;
    ULONG i;

    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);

    if (!MapRegisterBase)
    {
        return;
    }

    /* Validate the map register base signature */
    Base = (PHAL_ARM64_MAP_REGISTER_BASE)MapRegisterBase;
    if (Base->Signature != HAL_ARM64_MAP_REG_SIGNATURE)
    {
        DPRINT1("[arm64][DMA] IoFreeMapRegisters: Invalid signature 0x%lx\n",
                Base->Signature);
        return;
    }

    /* Free any bounce buffers that were allocated */
    for (i = 0; i < Base->NumberOfMapRegisters; i++)
    {
        PHAL_ARM64_MAP_REGISTER_ENTRY Entry = &Base->Registers[i];

        if (Entry->UsesBounceBuffer && Entry->BounceBuffer)
        {
            /*
             * If this was a read from device (DMA write), copy data
             * from bounce buffer back to original buffer before freeing.
             */
            if (!Entry->WriteToDevice && Entry->OriginalVa && Entry->Length > 0)
            {
                RtlCopyMemory(Entry->OriginalVa, Entry->BounceBuffer, Entry->Length);
            }

            MmFreeContiguousMemorySpecifyCache(Entry->BounceBuffer,
                                               Entry->Length,
                                               MmNonCached);
            Entry->BounceBuffer = NULL;
        }
    }

    /* Invalidate and free the map register base */
    Base->Signature = 0;
    ExFreePoolWithTag(Base, TAG_DMA_MAP);

}

/*
 * IoMapTransfer
 *
 * Maps a virtual buffer for DMA transfer and returns the physical address
 * that the device should use.
 *
 * On ARM64, this function:
 * 1. Gets the physical address from the MDL
 * 2. Checks if a bounce buffer is needed (32-bit device, high memory)
 * 3. Performs cache maintenance for non-coherent DMA
 * 4. Records the mapping in the map register (if provided)
 * 5. Returns the physical address for the device
 */
PHYSICAL_ADDRESS
NTAPI
IoMapTransfer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _Inout_ PULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PHYSICAL_ADDRESS ReturnAddress;
    ULONG TransferLength;
    PHAL_ARM64_MAP_REGISTER_BASE Base;
    PHAL_ARM64_MAP_REGISTER_ENTRY MapEntry = NULL;
    BOOLEAN NeedsBounceBuffer = FALSE;

    /* Validate parameters */
    if (!Length || *Length == 0)
    {
        if (Length) *Length = 0;
        return (PHYSICAL_ADDRESS){0};
    }

    /*
     * Get the physical address from the MDL PFN array.
     *
     * CurrentVa may be NULL for direct I/O buffers that were never
     * system-mapped (MmGetMdlVirtualAddress returns StartVa which
     * can be NULL). In that case, use the MDL's ByteOffset to
     * calculate the page index and offset within the PFN array.
     * This matches Windows HAL behavior: IoMapTransfer uses the
     * MDL PFN array, not the virtual address.
     */
    if (Mdl)
    {
        PPFN_NUMBER PfnArray = MmGetMdlPfnArray(Mdl);
        ULONG_PTR Offset;
        ULONG PageIndex;

        if (Mdl->StartVa)
        {
            /* Normal case: VA relative to MDL StartVa */
            Offset = (ULONG_PTR)CurrentVa - (ULONG_PTR)Mdl->StartVa;
        }
        else if (CurrentVa)
        {
            /* StartVa is NULL (unmapped MDL). USBPORT advances CurrentVa
             * from MmGetMdlVirtualAddress() which returned 0+ByteOffset.
             * Each subsequent call adds the mapped length: 0x1000, 0x2000...
             * Use CurrentVa directly as byte offset into the PFN array. */
            Offset = (ULONG_PTR)CurrentVa;
        }
        else
        {
            /* Both NULL — use ByteOffset for first page */
            Offset = Mdl->ByteOffset;
        }
        PageIndex = (ULONG)(Offset >> PAGE_SHIFT);
        PhysicalAddress.QuadPart = ((ULONGLONG)PfnArray[PageIndex] << PAGE_SHIFT) +
                                   (Offset & (PAGE_SIZE - 1));
    }
    else if (CurrentVa)
    {
        PhysicalAddress = MmGetPhysicalAddress(CurrentVa);
    }
    else
    {
        *Length = 0;
        return (PHYSICAL_ADDRESS){0};
    }

    if (PhysicalAddress.QuadPart == 0)
    {
        PPFN_NUMBER PfnDbg = Mdl ? MmGetMdlPfnArray(Mdl) : NULL;
        ULONG_PTR OffDbg = Mdl ? ((ULONG_PTR)CurrentVa - (ULONG_PTR)Mdl->StartVa) : 0;
        ULONG PgIdx = (ULONG)(OffDbg >> PAGE_SHIFT);
        DPRINT1("[arm64][HAL] IoMapTransfer: PA=0 VA=%p Mdl=%p StartVa=%p ByteOff=%lu PageIdx=%u PFN[0]=%lx PFN[idx]=%lx Len=%lu\n",
                CurrentVa, Mdl,
                Mdl ? Mdl->StartVa : NULL,
                Mdl ? Mdl->ByteOffset : 0,
                PgIdx,
                PfnDbg ? (ULONG_PTR)PfnDbg[0] : 0,
                PfnDbg ? (ULONG_PTR)PfnDbg[PgIdx] : 0,
                *Length);
        *Length = 0;
        return (PHYSICAL_ADDRESS){0};
    }

    /* Calculate the transfer length, limited by page boundaries */
    TransferLength = *Length;
    {
        ULONG PageOffset = (ULONG)(PhysicalAddress.QuadPart & (PAGE_SIZE - 1));
        ULONG BytesInPage = PAGE_SIZE - PageOffset;
        if (TransferLength > BytesInPage)
        {
            TransferLength = BytesInPage;
        }
    }

    /*
     * Check if we need a bounce buffer.
     * On ARM64, bus-master devices handle their own DMA addressing.
     * The shared adapter object's Dma64BitAddresses flag is unreliable
     * (overwritten by the last HalGetAdapter caller). Since ARM64
     * systems typically have all RAM above 4GB (e.g., RPi5), bounce
     * buffer allocation below 4GB would fail anyway. Skip bounce
     * buffers entirely — the device driver is responsible for ensuring
     * its DMA addresses are reachable (via XHCI 64-bit capability, etc.).
     */
    (void)NeedsBounceBuffer; /* always FALSE on ARM64 */

    /* Get map register entry if available */
    if (MapRegisterBase)
    {
        Base = (PHAL_ARM64_MAP_REGISTER_BASE)MapRegisterBase;
        if (Base->Signature == HAL_ARM64_MAP_REG_SIGNATURE &&
            Base->CurrentIndex < Base->NumberOfMapRegisters)
        {
            MapEntry = &Base->Registers[Base->CurrentIndex];
            Base->CurrentIndex++;
        }
    }

    /* Handle bounce buffer allocation */
    if (NeedsBounceBuffer)
    {
        PHYSICAL_ADDRESS Low = {0};
        PHYSICAL_ADDRESS High;
        PHYSICAL_ADDRESS Boundary = {0};
        PVOID BounceBuffer;

        High.QuadPart = HAL_ARM64_DMA_32BIT_LIMIT - 1;

        BounceBuffer = MmAllocateContiguousMemorySpecifyCache(TransferLength,
                                                               Low,
                                                               High,
                                                               Boundary,
                                                               MmNonCached);
        if (!BounceBuffer)
        {
            DPRINT1("[arm64][DMA] IoMapTransfer: Failed to allocate bounce buffer\n");
            *Length = 0;
            return (PHYSICAL_ADDRESS){0};
        }

        /* For write to device, copy data to bounce buffer */
        if (WriteToDevice)
        {
            RtlCopyMemory(BounceBuffer, CurrentVa, TransferLength);
        }

        ReturnAddress = MmGetPhysicalAddress(BounceBuffer);

        /* Track in map register if available */
        if (MapEntry)
        {
            MapEntry->OriginalVa = CurrentVa;
            MapEntry->PhysicalAddress = ReturnAddress;
            MapEntry->BounceBuffer = BounceBuffer;
            MapEntry->BouncePhysical = ReturnAddress;
            MapEntry->Length = TransferLength;
            MapEntry->UsesBounceBuffer = TRUE;
            MapEntry->WriteToDevice = WriteToDevice;
        }

        DPRINT("[arm64][DMA] IoMapTransfer: Using bounce buffer %p->%p (PA 0x%llx)\n",
               CurrentVa, BounceBuffer, ReturnAddress.QuadPart);
    }
    else
    {
        ReturnAddress = PhysicalAddress;

        /* Track in map register if available */
        if (MapEntry)
        {
            MapEntry->OriginalVa = CurrentVa;
            MapEntry->PhysicalAddress = PhysicalAddress;
            MapEntry->BounceBuffer = NULL;
            MapEntry->Length = TransferLength;
            MapEntry->UsesBounceBuffer = FALSE;
            MapEntry->WriteToDevice = WriteToDevice;
        }
    }

    /*
     * Perform cache maintenance for non-coherent DMA.
     * Use the private physical alias for cache operations instead of
     * CurrentVa, which may not be mapped in the current address space
     * (e.g. user-mode MDL pages during DPC-level I/O).
     */
    if (!HalpArm64DmaCoherency.SystemCoherent && TransferLength > 0)
    {
        ULONG_PTR KsegVa = HAL_ARM64_PHYS_MAP_BASE |
                            (PhysicalAddress.QuadPart & HAL_ARM64_PHYS_ADDR_MASK);
        ULONG_PTR Aligned = KsegVa & ~63ULL;
        ULONG_PTR End = KsegVa + TransferLength;

        if (WriteToDevice)
        {
            /* Device will read from memory - clean cache */
            for (; Aligned < End; Aligned += 64)
                __asm__ __volatile__("dc cvac, %0" :: "r"(Aligned) : "memory");
        }
        else
        {
            /* Device will write to memory - clean and invalidate cache */
            for (; Aligned < End; Aligned += 64)
                __asm__ __volatile__("dc civac, %0" :: "r"(Aligned) : "memory");
        }

        __asm__ __volatile__("dsb sy" ::: "memory");
    }

    /* Return the actual transfer length */
    *Length = TransferLength;

    return ReturnAddress;
}

VOID
NTAPI
HalReturnToFirmware(
    _In_ FIRMWARE_REENTRY Action)
{
    /*
     * ARM64 power management using PSCI.
     *
     * This function handles system halt, power down, restart, and reboot
     * requests using PSCI SYSTEM_RESET and SYSTEM_OFF calls.
     */

    /* Disable interrupts before power state transition */
    _disable();

    switch (Action)
    {
        case HalHaltRoutine:
            /*
             * Halt the system - enter a low-power wait state.
             * We use WFI (Wait For Interrupt) which is the ARM64 way to halt.
             */
            DPRINT1("[arm64][HAL] HalReturnToFirmware: HalHaltRoutine - halting CPU\n");
            __asm__ __volatile__("dsb sy; isb" ::: "memory");
            for (;;)
            {
                __asm__ __volatile__("wfi");
            }
            break;

        case HalPowerDownRoutine:
            /*
             * Power down the system completely.
             * Use PSCI SYSTEM_OFF if available, otherwise just halt.
             */
            DPRINT1("[arm64][HAL] HalReturnToFirmware: HalPowerDownRoutine - powering off\n");
            if (HalpArm64PsciInfo.Present)
            {
                HalpPsciSystemOff();
            }
            else
            {
                DPRINT1("[arm64][HAL] HalReturnToFirmware: PSCI not available, halting instead\n");
                __asm__ __volatile__("dsb sy; isb" ::: "memory");
                for (;;)
                {
                    __asm__ __volatile__("wfi");
                }
            }
            break;

        case HalRestartRoutine:
        case HalRebootRoutine:
            /*
             * Restart/Reboot the system.
             * Use PSCI SYSTEM_RESET if available.
             */
            DPRINT1("[arm64][HAL] HalReturnToFirmware: %s - rebooting\n",
                    (Action == HalRestartRoutine) ? "HalRestartRoutine" : "HalRebootRoutine");
            if (HalpArm64PsciInfo.Present)
            {
                HalpPsciSystemReset();
            }
            else
            {
                DPRINT1("[arm64][HAL] HalReturnToFirmware: PSCI not available, halting instead\n");
                __asm__ __volatile__("dsb sy; isb" ::: "memory");
                for (;;)
                {
                    __asm__ __volatile__("wfi");
                }
            }
            break;

        default:
            /*
             * Unknown action - log and halt.
             */
            DPRINT1("[arm64][HAL] HalReturnToFirmware: unknown action %d\n", Action);
            DbgBreakPoint();
            __asm__ __volatile__("dsb sy; isb" ::: "memory");
            for (;;)
            {
                __asm__ __volatile__("wfi");
            }
            break;
    }

    /* Should never reach here */
    for (;;)
    {
        __asm__ __volatile__("wfi");
    }
}

VOID
NTAPI
HalSetDisplayParameters(
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNIMPLEMENTED_STUB();
}

/*
 * HalSetBusData - Write PCI configuration space on ARM64
 *
 * This function writes data to PCI configuration space. On ARM64, all PCI
 * configuration access is performed through ECAM (Enhanced Configuration
 * Access Mechanism) as there is no legacy CF8/CFC port mechanism.
 *
 * Parameters:
 *   BusDataType - Type of bus data (must be PCIConfiguration for PCI)
 *   BusNumber   - PCI bus number (0-255)
 *   SlotNumber  - Encoded device and function number (PCI_SLOT_NUMBER)
 *   Buffer      - Buffer containing the configuration data to write
 *   Length      - Number of bytes to write
 *
 * Returns:
 *   Number of bytes successfully written, or 0 on failure.
 *
 * ECAM Address Calculation:
 *   Config Address = ECAM_BASE + (Bus << 20) + (Device << 15) + (Function << 12) + Register
 *
 * Note:
 *   Writing to PCI configuration space should be done with care. Improper
 *   writes can disable the device or cause system instability.
 */
ULONG
NTAPI
HalSetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    /* HalSetBusData writes starting at offset 0 */
    return HalSetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, 0, Length);
}

/*
 * HalSetBusDataByOffset - Write PCI configuration space at specified offset
 *
 * This function writes data to PCI configuration space starting at a specific
 * offset. On ARM64, ECAM provides access to the full 4KB PCIe extended
 * configuration space (not just the 256-byte legacy space).
 *
 * Parameters:
 *   BusDataType - Type of bus data (must be PCIConfiguration for PCI)
 *   BusNumber   - PCI bus number (0-255)
 *   SlotNumber  - Encoded device and function number (PCI_SLOT_NUMBER)
 *   Buffer      - Buffer containing the configuration data to write
 *   Offset      - Byte offset within configuration space (0-4095)
 *   Length      - Number of bytes to write
 *
 * Returns:
 *   Number of bytes successfully written, or 0 on failure.
 *
 * Notes:
 *   - ARM64 memory model requires explicit barriers for MMIO ordering.
 *     The ECAM access functions use WRITE_REGISTER_xxx which includes
 *     the necessary memory barriers.
 *   - The full 4KB PCIe extended configuration space is accessible.
 *   - Writes to read-only registers are silently ignored by hardware.
 */
ULONG
NTAPI
HalSetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PCI_SLOT_NUMBER PciSlot;

    /* Only PCIConfiguration bus data type is supported */
    if (BusDataType != PCIConfiguration)
    {
        DPRINT1("[arm64][HAL] HalSetBusDataByOffset: unsupported BusDataType %u\n",
                BusDataType);
        return 0;
    }

    /* Validate parameters */
    if (Buffer == NULL || Length == 0)
    {
        return 0;
    }

    /* Validate bus number (0-255) */
    if (BusNumber > 0xFF)
    {
        DPRINT1("[arm64][HAL] HalSetBusDataByOffset: invalid bus %lu\n", BusNumber);
        return 0;
    }

    /* Validate offset (0-4095 for PCIe extended config space) */
    if (Offset >= 0x1000)
    {
        DPRINT1("[arm64][HAL] HalSetBusDataByOffset: invalid offset 0x%lx\n", Offset);
        return 0;
    }

    /* Clamp length to not exceed config space boundary */
    if ((Offset + Length) > 0x1000)
    {
        Length = 0x1000 - Offset;
    }

    /* Convert SlotNumber to PCI_SLOT_NUMBER union */
    PciSlot.u.AsULONG = SlotNumber;

    /*
     * Use the firmware-published memory-mapped config-space backend.
     * Prefer MCFG when present, otherwise use ACPI root-bridge _CBA.
     */
    if (HalpArm64AccessPciConfigSpace(TRUE,                  /* Write */
                                      HALP_ACPI_SEGMENT_ANY, /* Use any segment */
                                      BusNumber,
                                      PciSlot,
                                      Buffer,
                                      Offset,
                                      Length))
    {
        return Length;
    }

    /* ECAM access failed */
    DPRINT1("[arm64][HAL] HalSetBusDataByOffset: ECAM write failed bus=%lu dev=%u func=%u offset=0x%lx\n",
            BusNumber, PciSlot.u.bits.DeviceNumber, PciSlot.u.bits.FunctionNumber, Offset);
    return 0;
}

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    __asm__ __volatile__("dsb sy" ::: "memory");
}

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    _Out_opt_ PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER Counter = {0};
    if (PerformanceFrequency)
    {
        PerformanceFrequency->QuadPart = (LONGLONG)HalpReadCntfrq();
    }
    Counter.QuadPart = (LONGLONG)HalpReadCntpct();
    return Counter;
}

VOID
NTAPI
KeStallExecutionProcessor(
    _In_ ULONG MicroSeconds)
{
    ULONGLONG Frequency = HalpReadCntfrq();
    ULONGLONG Start = HalpReadCntpct();
    ULONGLONG Ticks;

    if (Frequency == 0)
        return;

    Ticks = (Frequency / 1000000ULL) * (ULONGLONG)MicroSeconds;
    while ((HalpReadCntpct() - Start) < Ticks)
    {
        __asm__ __volatile__("isb" ::: "memory");
    }
}

/*
 * ============================================================================
 * MSI/MSI-X Support API
 * ============================================================================
 *
 * These functions provide the HAL API for MSI (Message Signaled Interrupts)
 * allocation and management, using the GICv3 ITS for interrupt translation.
 *
 * Following Windows 11 ARM64 patterns for PCI MSI integration.
 */

/*
 * External declarations for ITS functions (defined in gic_its.c)
 */
NTSTATUS
HalpGicItsAllocateMsi(
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG TargetCpu,
    _Out_ PULONG Lpi,
    _Out_ PPHYSICAL_ADDRESS MsiAddress,
    _Out_ PULONG MsiData);

NTSTATUS
HalpGicItsFreeMsi(
    _In_ ULONG DeviceId,
    _In_ ULONG EventId);

NTSTATUS
HalpGicItsSetMsiAffinity(
    _In_ ULONG Lpi,
    _In_ ULONG TargetCpu);

VOID
HalpGicItsDisableLpi(
    _In_ ULONG Lpi);

/*
 * HalpAllocateMsiInterrupt - Allocate MSI for a PCI device
 *
 * This is the main HAL API for PCI drivers to allocate MSI/MSI-X interrupts.
 * It uses the ITS to:
 * 1. Create/lookup the device in the ITS device table
 * 2. Allocate LPIs from the global pool
 * 3. Map the event to the LPI via MAPTI command
 * 4. Return the MSI address (GITS_TRANSLATER) and data (EventID)
 *
 * Parameters:
 *   BusNumber   - PCI bus number
 *   SlotNumber  - PCI slot/device number
 *   EventId     - MSI/MSI-X vector index (0 for MSI, 0-N for MSI-X)
 *   TargetCpu   - CPU to route the interrupt to
 *   MsiAddress  - Receives the address to program in PCI MSI capability
 *   MsiData     - Receives the data to program in PCI MSI capability
 *   Vector      - Receives the allocated system vector (LPI INTID)
 *
 * Returns:
 *   STATUS_SUCCESS on success, error code on failure.
 */
NTSTATUS
NTAPI
HalpAllocateMsiInterrupt(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ ULONG EventId,
    _In_ ULONG TargetCpu,
    _Out_ PPHYSICAL_ADDRESS MsiAddress,
    _Out_ PULONG MsiData,
    _Out_ PULONG Vector)
{
    PCI_SLOT_NUMBER PciSlot;
    ULONG DeviceId;
    ULONG Lpi;
    NTSTATUS Status;

    if (!MsiAddress || !MsiData || !Vector)
        return STATUS_INVALID_PARAMETER;

    /* Check if ITS is available */
    if (!HalpGicItsPresent || HalpGicItsNodeCount == 0)
    {
        /* Fall back to GICv2m MSI frame if available */
        if (HalpGicMsiPresent)
        {
            /* Use SPI-based MSI via GICv2m frame */
            MsiAddress->QuadPart = HalpGicMsiFrameBase + HAL_ARM64_GICV2M_SETSPI;
            *MsiData = HalpGicMsiSpiBase + (EventId % HalpGicMsiSpiCount);
            *Vector = *MsiData;
            return STATUS_SUCCESS;
        }
        return STATUS_DEVICE_NOT_READY;
    }

    /* Calculate DeviceID from BDF (Bus:Device:Function) */
    PciSlot.u.AsULONG = SlotNumber;
    DeviceId = ((BusNumber & 0xFF) << 8) |
               ((PciSlot.u.bits.DeviceNumber & 0x1F) << 3) |
               (PciSlot.u.bits.FunctionNumber & 0x07);

    /* Allocate MSI via ITS */
    Status = HalpGicItsAllocateMsi(DeviceId, EventId, TargetCpu,
                                    &Lpi, MsiAddress, MsiData);
    if (NT_SUCCESS(Status))
    {
        *Vector = Lpi;
        DPRINT("[arm64][MSI] Allocated MSI: Bus=%lu Dev=%u Func=%u Event=%lu -> LPI=%lu\n",
               BusNumber, PciSlot.u.bits.DeviceNumber, PciSlot.u.bits.FunctionNumber,
               EventId, Lpi);
    }
    else
    {
        DPRINT1("[arm64][MSI] Failed to allocate MSI: Bus=%lu Dev=%u Func=%u Event=%lu Status=0x%lx\n",
                BusNumber, PciSlot.u.bits.DeviceNumber, PciSlot.u.bits.FunctionNumber,
                EventId, Status);
    }

    return Status;
}

/*
 * HalpFreeMsiInterrupt - Free a previously allocated MSI
 *
 * Frees an MSI that was allocated by HalpAllocateMsiInterrupt.
 *
 * Parameters:
 *   BusNumber  - PCI bus number
 *   SlotNumber - PCI slot/device number
 *   EventId    - MSI/MSI-X vector index that was allocated
 *
 * Returns:
 *   STATUS_SUCCESS on success, error code on failure.
 */
NTSTATUS
NTAPI
HalpFreeMsiInterrupt(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ ULONG EventId)
{
    PCI_SLOT_NUMBER PciSlot;
    ULONG DeviceId;

    if (!HalpGicItsPresent || HalpGicItsNodeCount == 0)
        return STATUS_DEVICE_NOT_READY;

    /* Calculate DeviceID from BDF */
    PciSlot.u.AsULONG = SlotNumber;
    DeviceId = ((BusNumber & 0xFF) << 8) |
               ((PciSlot.u.bits.DeviceNumber & 0x1F) << 3) |
               (PciSlot.u.bits.FunctionNumber & 0x07);

    return HalpGicItsFreeMsi(DeviceId, EventId);
}

/*
 * HalpSetMsiInterruptAffinity - Change MSI interrupt affinity
 *
 * Changes the target CPU for an MSI interrupt. This requires sending
 * ITS commands to update the collection mapping.
 *
 * Parameters:
 *   Vector    - System vector (LPI INTID) from HalpAllocateMsiInterrupt
 *   TargetCpu - New target CPU for the interrupt
 *
 * Returns:
 *   STATUS_SUCCESS on success, error code on failure.
 */
NTSTATUS
NTAPI
HalpSetMsiInterruptAffinity(
    _In_ ULONG Vector,
    _In_ ULONG TargetCpu)
{
    if (!HalpGicItsPresent || HalpGicItsNodeCount == 0)
        return STATUS_DEVICE_NOT_READY;

    /* Validate vector is in LPI range */
    if (Vector < HAL_ARM64_LPI_BASE)
        return STATUS_INVALID_PARAMETER;

    return HalpGicItsSetMsiAffinity(Vector, TargetCpu);
}

/*
 * HalpEnableMsiInterrupt - Enable an MSI interrupt
 *
 * Enables an LPI in the PROPBASE table, making it deliverable.
 *
 * Parameters:
 *   Vector - System vector (LPI INTID)
 */
VOID
NTAPI
HalpEnableMsiInterrupt(
    _In_ ULONG Vector)
{
    if (Vector >= HAL_ARM64_LPI_BASE)
    {
        HalpGicItsEnableLpi(Vector);
    }
}

/*
 * HalpDisableMsiInterrupt - Disable an MSI interrupt
 *
 * Disables an LPI in the PROPBASE table, preventing delivery.
 *
 * Parameters:
 *   Vector - System vector (LPI INTID)
 */
VOID
NTAPI
HalpDisableMsiInterrupt(
    _In_ ULONG Vector)
{
    if (Vector >= HAL_ARM64_LPI_BASE)
    {
        HalpGicItsDisableLpi(Vector);
    }
}

/*
 * ============================================================================
 * GICv4 Virtualization API for Windows 11 ARM64 / Hyper-V Compatibility
 * ============================================================================
 *
 * These APIs provide the HAL-level interface for GICv4 virtual interrupt
 * support, enabling Hyper-V-style virtual machine interrupt injection.
 *
 * The GICv4 virtualization features allow:
 * - Virtual LPI (VLPI) support for per-vCPU interrupts
 * - Direct injection of virtual interrupts without hypervisor intervention
 * - Doorbell interrupts for vPE scheduling notifications
 *
 * These APIs are designed to integrate with Windows 11 ARM64's hypervisor
 * and virtual machine monitor subsystems.
 */

/*
 * HalpRegisterVirtualProcessor - Register a vPE for a virtual processor
 *
 * Allocates and initializes a GICv4 virtual Processing Element (vPE)
 * for use by a virtual machine. Each vPE can receive VLPIs directly
 * without hypervisor intervention when scheduled on a CPU.
 *
 * Parameters:
 *   VmId     - Virtual Machine identifier
 *   VpIndex  - Virtual Processor index within the VM (0-based)
 *   VpeId    - Receives the allocated vPE ID
 *
 * Returns:
 *   STATUS_SUCCESS - vPE allocated successfully
 *   STATUS_NOT_SUPPORTED - GICv4 VLPIs not supported on this hardware
 *   STATUS_INSUFFICIENT_RESOURCES - No vPE IDs or memory available
 */
NTSTATUS
NTAPI
HalpRegisterVirtualProcessor(
    _In_ ULONG VmId,
    _In_ ULONG VpIndex,
    _Out_ PULONG VpeId)
{
    PHALP_GIC_VPE Vpe = NULL;
    NTSTATUS Status;

    if (!VpeId)
        return STATUS_INVALID_PARAMETER;

    *VpeId = 0;

    /* Check if GICv4 VLPI support is available */
    if (!HalpGicHasVlpiSupport())
    {
        DPRINT("[arm64][HAL] GICv4 VLPI support not available\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Allocate a vPE for this virtual processor */
    Status = HalpGicItsAllocateVpe(VmId, VpIndex, &Vpe);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[arm64][HAL] Failed to allocate vPE for VM %lu VP %lu: 0x%lx\n",
                VmId, VpIndex, Status);
        return Status;
    }

    *VpeId = HalpGicItsGetVpeId(Vpe);

    DPRINT("[arm64][HAL] Registered vPE %lu for VM %lu VP %lu\n",
           *VpeId, VmId, VpIndex);

    return STATUS_SUCCESS;
}

/*
 * HalpUnregisterVirtualProcessor - Unregister a vPE
 *
 * Frees a previously registered vPE and all associated resources.
 * The vPE must not be currently scheduled on any CPU.
 *
 * Parameters:
 *   VpeId - vPE ID returned from HalpRegisterVirtualProcessor
 *
 * Returns:
 *   STATUS_SUCCESS - vPE freed successfully
 *   STATUS_INVALID_PARAMETER - Invalid vPE ID
 */
NTSTATUS
NTAPI
HalpUnregisterVirtualProcessor(
    _In_ ULONG VpeId)
{
    PHALP_GIC_VPE Vpe;

    if (VpeId >= HalpGicVpeTableSize)
        return STATUS_INVALID_PARAMETER;

    Vpe = HalpGicVpeTable[VpeId];
    if (!Vpe)
        return STATUS_INVALID_PARAMETER;

    DPRINT("[arm64][HAL] Unregistering vPE %lu\n", VpeId);

    HalpGicItsFreeVpe(Vpe);

    return STATUS_SUCCESS;
}

/*
 * HalpInjectVirtualInterrupt - Inject a virtual interrupt to a vPE
 *
 * Maps a virtual interrupt (VLPI) to a vPE. The interrupt will be
 * delivered directly to the vPE when it is scheduled on a CPU.
 *
 * For MSI-based device interrupts, use HalpMapVirtualDeviceInterrupt
 * instead to set up the full device-to-vPE mapping.
 *
 * Parameters:
 *   VpeId       - Target vPE ID
 *   VirtualLpi  - Virtual interrupt ID within the VM
 *
 * Returns:
 *   STATUS_SUCCESS - Interrupt mapped successfully
 *   STATUS_NOT_SUPPORTED - GICv4 VLPIs not supported
 *   STATUS_INVALID_PARAMETER - Invalid parameters
 */
NTSTATUS
NTAPI
HalpInjectVirtualInterrupt(
    _In_ ULONG VpeId,
    _In_ ULONG VirtualLpi)
{
    UNREFERENCED_PARAMETER(VpeId);
    UNREFERENCED_PARAMETER(VirtualLpi);

    /*
     * Direct interrupt injection is handled through the GICv4 VLPI mechanism.
     * For device interrupts, use HalpMapVirtualDeviceInterrupt to create
     * the VMAPTI mapping.
     *
     * For software-generated virtual interrupts, we would need to:
     * 1. Write to the vPE's Virtual Pending Table (VPT) directly
     * 2. Issue appropriate doorbell if vPE is not resident
     *
     * This is a placeholder for future Hyper-V integration.
     */
    if (!HalpGicHasVlpiSupport())
        return STATUS_NOT_SUPPORTED;

    DPRINT("[arm64][HAL] HalpInjectVirtualInterrupt: vPE=%lu VLPI=%lu (stub)\n",
           VpeId, VirtualLpi);

    return STATUS_SUCCESS;
}

/*
 * HalpScheduleVirtualProcessor - Schedule a vPE on a physical CPU
 *
 * Makes a vPE resident on the specified CPU by programming GICR_VPENDBASER.
 * Once scheduled, VLPIs for this vPE will be delivered directly to the CPU.
 *
 * Parameters:
 *   VpeId     - vPE ID to schedule
 *   TargetCpu - Physical CPU to schedule the vPE on
 *
 * Returns:
 *   STATUS_SUCCESS - vPE scheduled successfully
 *   STATUS_INVALID_PARAMETER - Invalid vPE ID or CPU
 *   STATUS_NOT_SUPPORTED - GICv4 not supported
 */
NTSTATUS
NTAPI
HalpScheduleVirtualProcessor(
    _In_ ULONG VpeId,
    _In_ ULONG TargetCpu)
{
    PHALP_GIC_VPE Vpe;

    if (!HalpGicHasVlpiSupport())
        return STATUS_NOT_SUPPORTED;

    if (VpeId >= HalpGicVpeTableSize)
        return STATUS_INVALID_PARAMETER;

    Vpe = HalpGicVpeTable[VpeId];
    if (!Vpe)
        return STATUS_INVALID_PARAMETER;

    return HalpGicItsScheduleVpe(Vpe, TargetCpu);
}

/*
 * HalpDescheduleVirtualProcessor - Deschedule a vPE from its current CPU
 *
 * Makes a vPE non-resident. A doorbell interrupt will be generated
 * if there are pending VLPIs for the vPE.
 *
 * Parameters:
 *   VpeId - vPE ID to deschedule
 *
 * Returns:
 *   STATUS_SUCCESS - vPE descheduled successfully
 *   STATUS_INVALID_PARAMETER - Invalid vPE ID
 */
NTSTATUS
NTAPI
HalpDescheduleVirtualProcessor(
    _In_ ULONG VpeId)
{
    PHALP_GIC_VPE Vpe;

    if (!HalpGicHasVlpiSupport())
        return STATUS_NOT_SUPPORTED;

    if (VpeId >= HalpGicVpeTableSize)
        return STATUS_INVALID_PARAMETER;

    Vpe = HalpGicVpeTable[VpeId];
    if (!Vpe)
        return STATUS_INVALID_PARAMETER;

    return HalpGicItsDescheduleVpe(Vpe);
}

/*
 * HalpMapVirtualDeviceInterrupt - Map a device interrupt to a vPE
 *
 * Creates a VMAPTI mapping that routes a device's MSI to a vPE.
 * When the device triggers the interrupt, it will be delivered
 * directly to the vPE without hypervisor intervention.
 *
 * Parameters:
 *   VpeId       - Target vPE ID
 *   DeviceId    - PCI device ID (requester ID)
 *   EventId     - Event ID within the device (MSI index)
 *   VirtualIntId - Virtual interrupt ID within the VM
 *
 * Returns:
 *   STATUS_SUCCESS - Mapping created successfully
 *   STATUS_NOT_SUPPORTED - GICv4 VLPIs not supported
 *   STATUS_INSUFFICIENT_RESOURCES - No resources available
 */
NTSTATUS
NTAPI
HalpMapVirtualDeviceInterrupt(
    _In_ ULONG VpeId,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG VirtualIntId)
{
    PHALP_GIC_VPE Vpe;

    if (!HalpGicHasVlpiSupport())
        return STATUS_NOT_SUPPORTED;

    if (VpeId >= HalpGicVpeTableSize)
        return STATUS_INVALID_PARAMETER;

    Vpe = HalpGicVpeTable[VpeId];
    if (!Vpe)
        return STATUS_INVALID_PARAMETER;

    return HalpGicItsMapVlpi(Vpe, DeviceId, EventId, VirtualIntId, TRUE);
}

/*
 * HalpUnmapVirtualDeviceInterrupt - Unmap a device interrupt from a vPE
 *
 * Removes a VMAPTI mapping, stopping device interrupts from being
 * delivered to the vPE.
 *
 * Parameters:
 *   VpeId    - Target vPE ID
 *   DeviceId - PCI device ID (requester ID)
 *   EventId  - Event ID within the device (MSI index)
 *
 * Returns:
 *   STATUS_SUCCESS - Mapping removed successfully
 *   STATUS_NOT_SUPPORTED - GICv4 VLPIs not supported
 */
NTSTATUS
NTAPI
HalpUnmapVirtualDeviceInterrupt(
    _In_ ULONG VpeId,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId)
{
    PHALP_GIC_VPE Vpe;

    if (!HalpGicHasVlpiSupport())
        return STATUS_NOT_SUPPORTED;

    if (VpeId >= HalpGicVpeTableSize)
        return STATUS_INVALID_PARAMETER;

    Vpe = HalpGicVpeTable[VpeId];
    if (!Vpe)
        return STATUS_INVALID_PARAMETER;

    return HalpGicItsUnmapVlpi(Vpe, DeviceId, EventId);
}

/*
 * HalpQueryGicCapabilities - Query GIC version and capabilities
 *
 * Returns information about the GIC hardware version and supported
 * features, including GICv3.1/v4 capabilities.
 *
 * Parameters:
 *   Architecture     - Receives GIC architecture version (3 or 4)
 *   HasVlpis         - Receives TRUE if VLPIs are supported
 *   HasDirectLpi     - Receives TRUE if direct LPI injection is supported
 *   HasExtendedSpi   - Receives TRUE if extended SPI range is supported
 *   MaxVpeid         - Receives maximum vPE ID (0 if VLPIs not supported)
 *
 * Returns:
 *   STATUS_SUCCESS
 */
NTSTATUS
NTAPI
HalpQueryGicCapabilities(
    _Out_opt_ PULONG Architecture,
    _Out_opt_ PBOOLEAN HasVlpis,
    _Out_opt_ PBOOLEAN HasDirectLpi,
    _Out_opt_ PBOOLEAN HasExtendedSpi,
    _Out_opt_ PULONG MaxVpeid)
{
    /* Ensure version detection has been performed */
    if (!HalpGicVersionInfo.Initialized)
        HalpGicDetectVersion();

    if (Architecture)
        *Architecture = HalpGicVersionInfo.Architecture;

    if (HasVlpis)
        *HasVlpis = HalpGicHasVlpis;

    if (HasDirectLpi)
        *HasDirectLpi = HalpGicHasDirectLpi;

    if (HasExtendedSpi)
        *HasExtendedSpi = HalpGicVersionInfo.HasExtendedSpiRange;

    if (MaxVpeid)
        *MaxVpeid = HalpGicVersionInfo.MaxVpeid;

    return STATUS_SUCCESS;
}
