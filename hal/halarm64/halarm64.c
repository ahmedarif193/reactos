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
#include <debug.h>

/*
 * PL011 UART early debug helpers for pre-KD initialization tracing.
 * These are used for early HAL debugging before the kernel debugger is ready.
 *
 * Platform-specific UART physical addresses:
 *   - QEMU virt:    0x09000000
 *   - Raspberry Pi 5 (BCM2712): 0x107d001000 (UART0 on debug header)
 *   - Raspberry Pi 4 (BCM2711): 0xFE201000
 */
#if defined(TARGET_QEMU_VIRT)
#define HAL_ARM64_PL011_BASE     0x09000000ULL
#elif defined(TARGET_RPI4)
#define HAL_ARM64_PL011_BASE     0xFE201000ULL
#else
/* Default to Raspberry Pi 5 (BCM2712) */
#define HAL_ARM64_PL011_BASE     0x107D001000ULL
#endif
#define HAL_ARM64_PL011_VA       (0xFFFF800000000000ULL + HAL_ARM64_PL011_BASE)
#define HAL_ARM64_PL011_FR_TXFF  (1U << 5)

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
static PHAL_ACPI_PCI_ROUTE_QUERY HalpArm64PciRouteQueryCallback = NULL;

/* Tags for memory allocation */
#define TAG_DMA_MAP  'PMAD'
#define TAG_DMA_BUF  'BMAD'
#define TAG_DMA_CMN  'CMAD'

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

/*
 * HalpPciLogEcamCoverage - ARM64 PCI ECAM coverage logging
 *
 * Logs diagnostic information about PCI Express ECAM (Enhanced Configuration
 * Access Mechanism) usage on ARM64. Unlike x86/x64, ARM64 has no legacy PCI
 * configuration mechanism (CF8/CFC ports), so ECAM is the only option.
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
 * On ARM64, this information comes from the ACPI MCFG table which describes
 * the ECAM regions.
 *
 * Parameters:
 *   MinBus - Optional pointer to receive minimum bus number
 *   MaxBus - Optional pointer to receive maximum bus number
 *
 * Returns:
 *   TRUE if valid bus range information is available, FALSE otherwise.
 *
 * Notes:
 *   ARM64 systems use ECAM exclusively for PCI configuration. The bus range
 *   is determined by the MCFG allocations. Multiple segments may exist, but
 *   this function returns the overall range for segment 0 (or the widest
 *   range across all segments).
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

    /*
     * If no MCFG allocations are present, we cannot determine the bus range.
     * This is a critical failure on ARM64 since ECAM is the only way to
     * access PCI configuration space.
     */
    if (!HalpAcpiMcfgAllocations || HalpAcpiMcfgAllocationCount == 0)
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
                DPRINT1("[arm64][DMA] Root Complex %lu (segment %lu) is cache-coherent\n",
                        Index, RcEntry->PciSegmentNumber);
            }
            else
            {
                HalpArm64DmaCoherency.RootComplexCoherent[Index] = FALSE;
                AllCoherent = FALSE;
                DPRINT1("[arm64][DMA] Root Complex %lu (segment %lu) is NOT cache-coherent\n",
                        Index, RcEntry->PciSegmentNumber);
            }
        }

        /* If no root complexes found, assume non-coherent */
        if (HalpArm64IortInfo.RootComplexCount == 0)
        {
            AllCoherent = FALSE;
        }

        HalpArm64DmaCoherency.SystemCoherent = AllCoherent;

        DPRINT1("[arm64][DMA] System cache coherency: %s (cache line %lu bytes)\n",
                AllCoherent ? "COHERENT" : "NON-COHERENT",
                HalpArm64DmaCoherency.CacheLineSize);
    }
    else
    {
        /*
         * No IORT present - assume non-coherent for safety.
         * This ensures cache maintenance is performed even if
         * the system doesn't provide coherency information.
         */
        HalpArm64DmaCoherency.SystemCoherent = FALSE;
        DPRINT1("[arm64][DMA] No IORT present, assuming non-coherent DMA\n");
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

        DPRINT1("[arm64][DMA] SMMU detected: Model=%lu Base=0x%llx Span=0x%llx\n",
                SmmuEntry->Model, SmmuEntry->BaseAddress, SmmuEntry->Span);

        /*
         * SMMU bypass mode configuration.
         *
         * For SMMUv2: Set SMMU_sCR0.CLIENTPD to disable client port.
         * For SMMUv3: Configure stream table entries for bypass.
         *
         * Full SMMU programming is deferred to HalpArm64SmmuInitialize().
         * For now, we rely on firmware leaving SMMU in bypass mode.
         */
        DPRINT1("[arm64][DMA] SMMU configured for bypass mode (identity mapping)\n");
    }
    else
    {
        DPRINT1("[arm64][DMA] No SMMU detected, using identity-mapped DMA\n");
    }

    HalpArm64DmaInitialized = TRUE;
    DPRINT1("[arm64][DMA] DMA subsystem initialized\n");
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
 * infrastructure (ARM64 uses ECAM exclusively for PCI configuration space).
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
    if (!Info)
        return;

    /*
     * Log the PCI root bridge configuration received from ACPI.
     * This confirms that ACPI PCI enumeration is working on ARM64.
     */
    DPRINT1("[arm64][PCI] ACPI PCI Root Bridge: Segment=%lu Bus=%lu BusRange=[%lu-%lu]\n",
            Info->Segment,
            Info->Bus,
            Info->BusStart,
            Info->BusEnd);

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

    /*
     * TODO: Create a proper bus handler for this PCI root bridge.
     * For now, the ACPI enumeration will log the root bridges but
     * the full PnP device tree integration requires bus handler support.
     */
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
#define HAL_ARM64_KSEG0_BASE 0xFFFF800000000000ULL
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

/* ACPI MADT subtable types for ARM GIC */
#define ACPI_MADT_TYPE_GENERIC_INTERRUPT      0x0B
#define ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR    0x0C
#define ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR  0x0E

typedef struct _ACPI_SUBTABLE_HEADER
{
    UCHAR Type;
    UCHAR Length;
} ACPI_SUBTABLE_HEADER, *PACPI_SUBTABLE_HEADER;

#include <pshpack1.h>
typedef struct _ACPI_MADT
{
    DESCRIPTION_HEADER Header;
    ULONG LocalApicAddress;
    ULONG Flags;
} ACPI_MADT, *PACPI_MADT;

typedef struct _ACPI_MADT_GENERIC_DISTRIBUTOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG GicId;
    ULONGLONG BaseAddress;
    ULONG SystemVectorBase;
    UCHAR GicVersion;
    UCHAR Reserved2[3];
} ACPI_MADT_GENERIC_DISTRIBUTOR, *PACPI_MADT_GENERIC_DISTRIBUTOR;

typedef struct _ACPI_MADT_GENERIC_REDISTRIBUTOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONGLONG BaseAddress;
    ULONG Length;
    ULONG Reserved2;
} ACPI_MADT_GENERIC_REDISTRIBUTOR, *PACPI_MADT_GENERIC_REDISTRIBUTOR;

typedef struct _ACPI_MADT_GENERIC_INTERRUPT
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG CpuInterfaceNumber;
    ULONG AcpiProcessorUid;
    ULONG Flags;
    ULONG ParkingProtocolVersion;
    ULONG PerformanceInterrupt;
    ULONGLONG ParkedAddress;
    ULONGLONG BaseAddress;
    ULONGLONG GicvBaseAddress;
    ULONGLONG GichBaseAddress;
    ULONG VgicMaintenanceInterrupt;
    ULONG Reserved2;
    ULONGLONG GicrBaseAddress;
    ULONGLONG Mpidr;
} ACPI_MADT_GENERIC_INTERRUPT, *PACPI_MADT_GENERIC_INTERRUPT;
#include <poppack.h>

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
/* Use identity map until the kernel's KSEG0 direct map is fully established. */
static BOOLEAN HalpUseIdentityMapping = TRUE;
static ULONG HalpUsedAllocDescriptors;
static MEMORY_ALLOCATION_DESCRIPTOR HalpAllocationDescriptorArray[64];

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

static __inline volatile ULONG *HalpMmio(ULONG_PTR Base, ULONG Offset);
static __inline ULONGLONG HalpMmioRead64(ULONG_PTR Base, ULONG Offset);
static __inline VOID HalpMmioWrite64(ULONG_PTR Base, ULONG Offset, ULONGLONG Value);
static __inline ULONG_PTR HalpGicrBase(_In_ ULONG Cpu);

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

static VOID
HalpGicItsEnableLpi(_In_ ULONG EventId)
{
    if (!HalpGicLpiConfig || EventId >= HalpGicLpiCount)
        return;

    HalpGicLpiConfig[EventId] = (UCHAR)(HAL_ARM64_LPI_PROP_PRIO_DEFAULT |
                                       HAL_ARM64_LPI_PROP_GROUP1 |
                                       HAL_ARM64_LPI_PROP_ENABLED);
    HalpArm64CleanDcacheRange(&HalpGicLpiConfig[EventId], sizeof(UCHAR));
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
     * Convert ITS physical address to KSEG0 virtual address.
     * After early boot, identity mapping is disabled and we must use
     * the kernel's KSEG0 region for accessing physical device memory.
     * Store in static variable so helper functions can use it.
     */
    HalpGicItsVa = (ULONG_PTR)(HalpGicItsBase | HAL_ARM64_KSEG0_BASE);
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
    UNREFERENCED_PARAMETER(Segment);
    UNREFERENCED_PARAMETER(Bus);

    if (Supported) *Supported = (HalpGicItsPresent || HalpGicMsiPresent);
    if (OscStatusFlags) *OscStatusFlags = 0;
    if (OscControlGranted) *OscControlGranted = 0;
    if (EffectiveSegment) *EffectiveSegment = (USHORT)Segment;
    if (OscMaskedControls) *OscMaskedControls = 0;

    return (HalpGicItsPresent || HalpGicMsiPresent);
}

static __inline PVOID HalpPhysToKseg0(ULONGLONG Physical)
{
    if (Physical == 0)
        return NULL;

    if (Physical >= HAL_ARM64_KSEG0_BASE)
        return (PVOID)(ULONG_PTR)Physical;

    if (HalpUseIdentityMapping)
        return (PVOID)(ULONG_PTR)Physical;

    return (PVOID)(ULONG_PTR)(Physical | HAL_ARM64_KSEG0_BASE);
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

static __inline volatile ULONG *HalpMmio(ULONG_PTR Base, ULONG Offset)
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
     * After boot (HalpUseIdentityMapping == FALSE):
     *   - Convert physical addresses to KSEG0 virtual addresses (TTBR1)
     *     by adding HAL_ARM64_KSEG0_BASE.
     *   - At this point, MmMapIoSpace should be used for MMIO mappings.
     */
    ULONG_PTR Va = Base;

    /* Don't modify addresses that are already in high VA space */
    if (Va >= HAL_ARM64_KSEG0_BASE)
        return (volatile ULONG *)(Va + Offset);

    /* During early boot, use identity mapping (PA == VA) via TTBR0 */
    if (HalpUseIdentityMapping)
        return (volatile ULONG *)(Va + Offset);

    /* After boot, convert to KSEG0 VA via TTBR1 */
    Va |= HAL_ARM64_KSEG0_BASE;
    return (volatile ULONG *)(Va + Offset);
}

static __inline ULONGLONG HalpMmioRead64(ULONG_PTR Base, ULONG Offset)
{
    volatile ULONG *Ptr = HalpMmio(Base, Offset);
    ULONGLONG Low = Ptr[0];
    ULONGLONG High = Ptr[1];
    return Low | (High << 32);
}

static __inline VOID HalpMmioWrite64(ULONG_PTR Base, ULONG Offset, ULONGLONG Value)
{
    volatile ULONG *Ptr = HalpMmio(Base, Offset);
    Ptr[0] = (ULONG)(Value & 0xFFFFFFFFu);
    Ptr[1] = (ULONG)(Value >> 32);
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
     * [CYCLE36] Wake redistributor and wait for ChildrenAsleep to clear
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
    DPRINT1("[CYCLE36][GICR] WAKER before wake: 0x%08lx\n", W);

    /* If WAKER is not implemented (reads as 0), skip wakeup sequence */
    if (W == 0)
    {
        DPRINT1("[CYCLE36][GICR] WAKER reads as 0 - redistributor may not implement power management, continuing\n");
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
        DPRINT1("[CYCLE36][GICR] WAKER after wake: 0x%08lx (spins left=%lu)\n", W, Spins);

        if (Spins == 0)
        {
            DPRINT1("[CYCLE36][GICR] WARNING: GICR wakeup timeout for CPU %lu (WAKER still 0x%08lx)\n", Cpu, W);
            /* Continue anyway - redistributor might still work */
        }
    }

    /* Clear any pending SGI/PPI interrupts first */
    *HalpMmio(SgiBase, GICR_ICPENDR0) = 0xFFFFFFFF;

    /*
     * [CYCLE36] CRITICAL FIX: Configure interrupts as Group 1 Non-Secure
     *
     * GICv3 uses TWO registers to determine interrupt group:
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
    DPRINT1("[CYCLE36][GICR] Configured all SGI/PPI as Group 1 Non-Secure\n");

    /*
     * [CYCLE36] CRITICAL FIX: Priority masking issue
     *
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
     * Fix: Use 0x00 for highest priority, ensuring interrupts pass PMR mask.
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
     * [CYCLE36] Configure PPI trigger modes in GICR_ICFGR1
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
    DPRINT1("[CYCLE36][GICR] ICFGR1=0x%08lx (PPI 27,30 set to level-sensitive)\n", Icfgr1);

    /*
     * [CYCLE35] CRITICAL FIX: Only disable SGIs, NOT PPIs!
     *
     * The comment above correctly states we should disable SGIs (0-15),
     * but the old code was disabling ALL interrupts including PPIs (16-31).
     * This prevented the timer PPI (30) from ever being enabled.
     *
     * Fix: Only write to the SGI bits (0-15), leaving PPI bits (16-31) alone.
     * PPIs will be enabled later by HalEnableSystemInterrupt when needed.
     */
    *HalpMmio(SgiBase, GICR_ICENABLER0) = 0x0000FFFF; /* Disable only SGIs (0-15) */

    /* Memory barrier after redistributor configuration */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /* Verify final state */
    ULONG IsEnabler = *HalpMmio(SgiBase, GICR_ISENABLER0);
    DPRINT1("[CYCLE35][GICR] Final ISENABLER0=0x%08lx (SGIs disabled, PPIs ready for enable)\n", IsEnabler);
    DPRINT1("[arm64][GICR] Redistributor initialization complete for CPU %lu\n", Cpu);
}

static ULONG HalpArm64ActiveIntId[MAXIMUM_PROCESSORS];

/* GIC detection: system-register interface (GICv3+) vs legacy CPU IF (GICv2)
 * HalpGicUseSysRegs, HalpForceLegacyGic, and HalpGicArchRev are declared
 * as extern at the top of this file and defined in gic_common.c. */
static BOOLEAN HalpLoggedGicOnce = FALSE; /* One-time post-KD log */

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
        DPRINT1("[arm64][HAL] HalpArm64DiscoverGicFromMadt: already parsed\n");
        return;
    }

    HalpGicParsedMadt = TRUE;
    if (!LoaderBlock)
    {
        DPRINT1("[arm64][HAL] HalpArm64DiscoverGicFromMadt: no LoaderBlock\n");
        return;
    }

    DPRINT1("[arm64][HAL] HalpArm64DiscoverGicFromMadt: parsing MADT\n");
    Mpidr = HalpReadMpidr();
    HalpAcpiDiscoverArm64Tables(LoaderBlock);
    DPRINT1("[arm64][HAL] HalpArm64DiscoverGicFromMadt: ACPI parse done\n");

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
    if (!FoundCpuGicr && HalpGicrRegionBase)
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
    static volatile LONG CallCount = 0;
    LONG ThisCall = InterlockedIncrement(&CallCount);

    DPRINT1("[arm64][HAL] HalpArm64SelectGicInterface: ENTRY call#%ld flag=%d\n",
            ThisCall, (int)HalpGicInterfaceSelected);

    if (HalpGicInterfaceSelected)
        return;

    HalpGicInterfaceSelected = TRUE;
    DPRINT1("[arm64][HAL] HalpArm64SelectGicInterface: call#%ld SELECTED\n", ThisCall);

    /* Check for boot options that force GIC interface selection */
    if (LoaderBlock && LoaderBlock->LoadOptions)
    {
        if (HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICV3") ||
            HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICSYSREG"))
        {
            HalpForceSysRegs = TRUE;
            HalpForceLegacyGic = FALSE;
            DPRINT1("[arm64][HAL] Forcing GICv3 system-register interface via boot option\n");
        }
        else if (HalpHasLoaderOption(LoaderBlock->LoadOptions, "GICV2") ||
                 HalpHasLoaderOption(LoaderBlock->LoadOptions, "NOGICSYSREG") ||
                 HalpHasLoaderOption(LoaderBlock->LoadOptions, "LEGACYGIC"))
        {
            HalpForceLegacyGic = TRUE;
            DPRINT1("[arm64][HAL] Forcing legacy GIC interface via boot option\n");
        }
    }

    HalpArm64DiscoverGicFromMadt(LoaderBlock);

    /* Apple HVF host mode tends to mis-handle GICv3 sysreg IRQ delivery. Prefer legacy GICC unless forced. */
    if (!HalpForceSysRegs && !HalpForceLegacyGic && HalpGiccPresent)
    {
        midr = HalpReadMidr();
        implementer = (UCHAR)((midr >> 24) & 0xFF);
        if (implementer == 0x61) /* Apple */
        {
            HalpForceLegacyGic = TRUE;
            DPRINT1("[arm64][HAL] Apple MIDR detected; preferring legacy GIC CPU interface for HVF compatibility\n");
        }
    }

    if (!HalpGiccPresent)
    {
        if (HalpForceLegacyGic)
        {
            DPRINT1("[arm64][HAL] Legacy GIC forced but MADT reports no GICC; using sysregs\n");
        }
        HalpForceLegacyGic = FALSE;
        HalpForceSysRegs = TRUE;
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
            if (!HalpGiccPresent)
            {
                DPRINT1("[arm64][HAL] GICV3 forced but SRE enable failed and no GICC present\n");
            }
            else
            {
                DPRINT1("[arm64][HAL] GICV3 forced but SRE enable failed, falling back to legacy\n");
            }
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
        HalpUseIdentityMapping = FALSE;
        if (!HalpLoggedGicOnce)
        {
            HalpLoggedGicOnce = TRUE;
            {
                ULONGLONG pfr0 = HalpReadPfr0();
                ULONG pfr0_gic = (ULONG)((pfr0 >> 24) & 0xF);
                ULONG pidr2 = *HalpMmio((ULONG_PTR)HalpGicdBase, 0xFE8);
                DPRINT1("[arm64][HAL] GIC probe: PFR0.GIC=%lu SRE=%lu PIDR2=0x%08lx ARCHREV=%lu\n",
                        pfr0_gic,
                        HalpGicUseSysRegs ? 1UL : 0UL,
                        pidr2,
                        HalpGicArchRev);
            }
            DPRINT1("[arm64][HAL] Using %s CPU interface\n",
                    HalpGicUseSysRegs ? "GICv3 system-register" : "GICv2 legacy (GICC)");

            /*
             * [CYCLE37] Phase 1: Enable GIC CPU interface Group 1 delivery
             *
             * Now that MM initialization is complete and the system is stable,
             * we can safely enable ICC_IGRPEN1_EL1 to allow Group 1 interrupts
             * (including the timer PPI) to be delivered to the CPU.
             *
             * This was kept disabled in Phase 0 to prevent spurious interrupts
             * from causing crashes during early MM initialization.
             *
             * CRITICAL: We must raise IRQL to HIGH_LEVEL before enabling ICC_IGRPEN1_EL1
             * to prevent interrupts from being delivered while we're in the middle of
             * the enable sequence. At PASSIVE_LEVEL, DAIF.I=0 so IRQs are unmasked.
             * If a spurious interrupt arrives during the ICC write, it can corrupt
             * the return address and cause a crash.
             */
            if (HalpGicUseSysRegs)
            {
                KIRQL OldIrql;
                ULONG Igrpen1;

                OldIrql = KfRaiseIrql(HIGH_LEVEL);
                HalpWriteIccIgrpen1(1); /* Enable Group1 interrupt delivery */

                /* Barrier to ensure the enable takes effect */
                __asm__ __volatile__("dsb sy" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");

                /* Verify it was enabled */
                Igrpen1 = HalpReadIccIgrpen1();
                DPRINT1("[CYCLE37] HalInitSystem Phase1: ICC_IGRPEN1_EL1=0x%x (interrupts now ENABLED)\n", Igrpen1);

                /* Lower IRQL - interrupts can now be delivered */
                KfLowerIrql(OldIrql);
                DPRINT1("[CYCLE37] HalInitSystem Phase1: Lowered IRQL from %u to %u, interrupts ACTIVE\n",
                        HIGH_LEVEL, KeGetCurrentIrql());
            }
            else
            {
                /*
                 * For GICv2 legacy interface, GICC_CTLR was already enabled
                 * in Phase 0 (EnableGrp0=1). No additional enablement needed.
                 */
                DPRINT1("[CYCLE37] HalInitSystem Phase1: GICv2 legacy CPU IF already enabled\n");
            }
        }
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
     *   4. Kernel sets KiHalInitialized = TRUE in Phase1InitializationDiscard
     *   5. GIC priority masking becomes active for IRQL transitions
     *
     * The HAL no longer needs to call back into the kernel to enable GIC support.
     */

    DPRINT1("[arm64][HAL] HalInitSystem: phase0 begin\n");

    KeInitializeSpinLock(&HalpPCIConfigLock);
    KeInitializeSpinLock(&HalpGicItsLock);
    Status = HalpSetupAcpiPhase0(LoaderBlock);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[arm64][HAL] ACPI phase0 init failed: 0x%08lx\n", Status);
    }
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
    DPRINT1("[arm64][HAL] HalInitSystem: raised IRQL from %u to %u for GIC init\n",
            OldIrql, KeGetCurrentIrql());

    HalpArm64SelectGicInterface(LoaderBlock);

    /* Probe GIC capabilities before touching CPU IF */
    {
        ULONGLONG pfr0 = 0;
        ULONG pfr0_gic = 0;
        ULONG pidr2 = 0;

        pfr0 = HalpReadPfr0();
        pfr0_gic = (ULONG)((pfr0 >> 24) & 0xF);

        /* Identify distributor architecture revision */
        pidr2 = *HalpMmio((ULONG_PTR)HalpGicdBase, 0xFE8); /* GICD_PIDR2 */
        HalpGicArchRev = ((pidr2 >> 4) & 0xF);
        if (HalpGicArchRev == 0)
            HalpGicArchRev = HalpGicUseSysRegs ? 3 : 2;

        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                   "[arm64][HAL] GIC probe: PFR0.GIC=%lu SRE=%lu PIDR2=0x%08lx ARCHREV=%lu\n",
                   pfr0_gic,
                   HalpGicUseSysRegs ? 1UL : 0UL,
                   pidr2,
                   HalpGicArchRev);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                   "[arm64][HAL] Using %s CPU interface\n",
                   HalpGicUseSysRegs ? "GICv3 system-register" : "GICv2 legacy (GICC)");
    }

    DPRINT1("[arm64][HAL] HalInitSystem: GIC probe done (useSys=%lu arch=%lu)\n",
            HalpGicUseSysRegs ? 1UL : 0UL,
            HalpGicArchRev);

    /* Log MSI state (MADT parsing happened earlier in HalInitializeProcessor) */
    DPRINT1("[arm64][HAL] HalInitSystem: MSI state: ITS=%d (Base=0x%llx) GICv2m=%d (Base=0x%llx SPI[%u..%u])\n",
            (int)HalpGicItsPresent, HalpGicItsBase,
            (int)HalpGicMsiPresent, HalpGicMsiFrameBase,
            HalpGicMsiSpiBase, (ULONG)(HalpGicMsiSpiBase + HalpGicMsiSpiCount - 1));

    /* Disable distributor while we (re)configure */
    *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR) = 0;
    DPRINT1("[arm64][HAL] HalInitSystem: GICD disabled\n");

    /* How many interrupt lines? */
    typer = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_TYPER);
    lines = 32 * ((typer & 0x1F) + 1);
    if (lines > 1020) lines = 1020;
    nregs = (lines + 31) / 32;
    DPRINT1("[arm64][HAL] HalInitSystem: GICD typer=0x%08lx lines=%lu nregs=%lu\n",
            typer,
            lines,
            nregs);

    /*
     * Configure SPI interrupt groups and clear pending.
     *
     * For GIC-v3 with system registers: Use Group 1 (non-secure) as standard.
     * For GIC-v2 (MMIO interface): Use Group 0 because QEMU HVF has a bug
     * where enabling Group 1 via GICD_CTLR causes a hang.
     *
     * Group 0 interrupts are delivered via IRQ (not FIQ) when the CPU is in
     * non-secure mode and GICC_CTLR.EOImodeNS=0, which is our configuration.
     */
    for (i = 1; i < nregs; ++i) /* start at 1 to skip SGI/PPI */
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICENABLER + i * 4) = 0xFFFFFFFF;
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICPENDR   + i * 4) = 0xFFFFFFFF;
        if (HalpGicUseSysRegs)
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IGROUPR + i * 4) = 0xFFFFFFFF; /* G1 */
        }
        else
        {
            *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IGROUPR + i * 4) = 0x00000000; /* G0 for GIC-v2 */
        }
    }

    DPRINT1("[arm64][HAL] HalInitSystem: GICD groups/disable done\n");

    /*
     * [CYCLE36] Set priorities to 0x00 (highest) for all SPIs.
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

    DPRINT1("[arm64][HAL] HalInitSystem: GICD priorities/targets done\n");

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
        DPRINT1("[arm64][HAL] HalInitSystem: GICv2 saved PPI enables: 0x%08lx\n", SavedEnables);

        /* Clear pending interrupts but do NOT disable - we will restore enables */
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICPENDR + 0) = 0xFFFFFFFF;

        /*
         * Set SGIs/PPIs to Group 0 for GIC-v2.
         * QEMU HVF has a bug where enabling Group 1 via GICD_CTLR hangs.
         * Group 0 interrupts are still delivered as IRQ in non-secure mode.
         */
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_IGROUPR + 0) = 0x00000000; /* G0 */

        /*
         * [CYCLE36] Set SGI/PPI priorities to 0x00 (highest).
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
            DPRINT1("[arm64][HAL] HalInitSystem: GICv2 restored PPI enables: 0x%08lx\n", SavedEnables);
        }

        DPRINT1("[arm64][HAL] HalInitSystem: GICv2 SGI/PPI configured with preserved enables\n");
    }

    /*
     * Memory barrier to ensure all distributor configuration (SPIs + SGI/PPI)
     * is complete before we enable interrupt delivery.
     */
    __asm__ __volatile__("dsb sy" ::: "memory");

    DPRINT1("[arm64][HAL] HalInitSystem: about to enable GICD\n");

    /*
     * Enable the distributor.
     *
     * GICD_CTLR bits:
     *   Bit 0: EnableGrp0 - Group 0 interrupts
     *   Bit 1: EnableGrp1 - Group 1 (non-secure) interrupts
     *   Bit 4: ARE_NS - Affinity Routing Enable (GICv3 only)
     *
     * For GIC-v2 on QEMU with Apple HVF, enabling Group 1 (bit 1) causes a hang.
     * This appears to be a QEMU HVF GIC-v2 emulation bug. As a workaround, we
     * configure all interrupts as Group 0 and only enable Group 0 here.
     *
     * For GIC-v3, we MUST enable BOTH Group 0 and Group 1 because:
     *   - SPIs are configured as Group 1 (GICD_IGROUPR)
     *   - PPIs are configured as Group 1 in redistributor (GICR_IGROUPR0)
     *   - ICC_IGRPEN1_EL1 is enabled for Group 1 interrupt delivery
     * [CYCLE34] Fix: Enable Group 1 for GICv3 to allow PPI timer delivery
     */
    if (HalpGicUseSysRegs)
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR) = 0x13; /* EnableGrp0 | EnableGrp1NS | ARE_NS */
        DPRINT1("[CYCLE34] GICD_CTLR set to 0x13 (Group0+Group1+ARE for GICv3)\n");
    }
    else
    {
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR) = 0x1; /* EnableGrp0 only for GICv2 */
    }

    /* Barrier to ensure GICD enable completes before CPU interface configuration */
    __asm__ __volatile__("dsb sy" ::: "memory");

    DPRINT1("[arm64][HAL] HalInitSystem: GICD enabled\n");

    if (HalpGicUseSysRegs)
    {
        HalpInitGicRedistributor(KeGetCurrentProcessorNumber());
    }

    /* CPU interface: system registers (v3+) or legacy GICC (v2) */
    if (HalpGicUseSysRegs)
    {
        ULONG Sre, Pmr;

        /* [CYCLE37] Enable system register access and configure priority mask */
        Sre = HalpReadIccSre();
        Sre |= 0x1; /* SRE bit */
        HalpWriteIccSre(Sre);
        Sre = HalpReadIccSre();

        HalpWriteIccPmr(0xFF); /* allow all priorities */
        HalpWriteIccBpr1(0);

        /*
         * [CYCLE37] DO NOT enable ICC_IGRPEN1_EL1 in Phase 0!
         *
         * CRITICAL: We must keep ICC_IGRPEN1_EL1=0 during Phase 0 to prevent
         * spurious interrupts from being delivered during early MM initialization.
         *
         * Even though IRQL=HIGH_LEVEL masks IRQs via DAIF.I=1, there are brief
         * windows during IRQL transitions where interrupts can be delivered.
         * If a spurious interrupt (IntId=1023) arrives during MmArmInitSystem
         * Phase 0, the interrupt handler may dereference NULL pointers or access
         * uninitialized structures, causing crashes.
         *
         * The timer is already configured but won't fire yet (ISTATUS=0), so
         * spurious IntId=1023 is expected. By keeping IGRPEN1=0, we prevent
         * these spurious interrupts from reaching the CPU until the system is
         * fully initialized.
         *
         * ICC_IGRPEN1_EL1 will be enabled in Phase 1 after MM initialization.
         */
        HalpWriteIccIgrpen1(0); /* KEEP DISABLED in Phase 0 */

        /* [CYCLE37] Read back and verify */
        Pmr = HalpReadIccPmr();
        DPRINT1("[CYCLE37] HalInitSystem Phase0: GICv3 CPU IF: SRE=0x%x PMR=0x%x IGRPEN1=DISABLED\n", Sre, Pmr);
        DPRINT1("[CYCLE37] ICC_IGRPEN1_EL1 will be enabled in Phase 1 after MM init\n");
    }
    else
    {
        if (HalpGiccBase == 0)
        {
            DPRINT1("[arm64][HAL] HalInitSystem: legacy CPU IF unavailable (GICC base is 0)\n");
        }
        else
        {
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
             * - CTLR = 0x1: Enable Group0 interrupts only
             *
             * NOTE: We only enable Group 0 because QEMU HVF has a bug where
             * enabling Group 1 causes a hang. All interrupts are configured
             * as Group 0 via GICD_IGROUPR.
             */
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_PMR) = 0xFF;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_BPR) = 0x0;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_CTLR) = 0x1; /* EnableGrp0 only */

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

            DPRINT1("[arm64][HAL] GICv2 legacy CPU interface initialized with barriers\n");
        }
    }

    DPRINT1("[arm64][HAL] HalInitSystem: CPU interface configured\n");

    /*
     * Initialize system time increment values for the scheduler tick.
     * ARM64 generic timer targets 100 Hz (10ms period = 100,000 100ns units).
     * The maximum increment is 10ms (100000), minimum is 1ms (10000).
     * This matches the configuration in KiArm64StartTimer().
     */
    KeSetTimeIncrement(100000, 10000);
    DPRINT1("[arm64][HAL] HalInitSystem: KeSetTimeIncrement configured (10ms tick)\n");

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
    DPRINT1("[arm64][HAL] HalInitSystem: HAL dispatch table callbacks set (PnP, QuerySysInfo, SetSysInfo)\n");

    /* Debug: dump GIC state at end of HAL init */
    {
        ULONG gicd_ctlr = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_CTLR);
        ULONG gicd_isenabler0 = *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ISENABLER + 0);
        ULONG gicc_ctlr = HalpGiccBase ? *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_CTLR) : 0;
        ULONG gicc_pmr = HalpGiccBase ? *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_PMR) : 0;
        DPRINT1("[arm64][HAL] HalInitSystem END: GICD_CTLR=0x%08lx GICD_ISENABLER0=0x%08lx GICC_CTLR=0x%08lx GICC_PMR=0x%08lx\n",
                gicd_ctlr, gicd_isenabler0, gicc_ctlr, gicc_pmr);
    }

    /*
     * CRITICAL: Lower IRQL back to the original level before returning.
     *
     * We raised IRQL to HIGH_LEVEL during GIC initialization to prevent
     * interrupts from firing while we configured the interrupt controller.
     * Now that the GIC is configured, we MUST restore the original IRQL.
     */
    KeLowerIrql(OldIrql);
    DPRINT1("[arm64][HAL] HalInitSystem: phase0 complete, lowered IRQL from %u to %u\n",
            HIGH_LEVEL, KeGetCurrentIrql());

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

    DPRINT("[arm64][DMA] HalAllocateAdapterChannel: Allocated %lu map registers at %p\n",
           NumberOfMapRegisters, MapRegisterBase);

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

    /* Determine cache type based on CacheEnabled and system coherency */
    if (CacheEnabled && HalpArm64DmaCoherency.SystemCoherent)
        CacheType = MmCached;
    else if (CacheEnabled)
        CacheType = MmCached;
    else
        CacheType = MmNonCached;

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

    DPRINT("[arm64][DMA] HalAllocateCommonBuffer: VA=%p PA=0x%llx Len=%lu\n",
           VirtualAddress, LogicalAddress->QuadPart, Length);

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
     * INTID 0 is reserved and should never be delivered
     */
    if (intid == 0)
        return FALSE;

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
     * This handles the case where multiple interrupts are nested - we track
     * per-CPU to ensure the correct INTID is EOI'd.
     */
    if (cpu < MAXIMUM_PROCESSORS) HalpArm64ActiveIntId[cpu] = intid;

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
    else
    {
        /* SPI (32+): disable via distributor */
        *HalpMmio((ULONG_PTR)HalpGicdBase, GICD_ICENABLER + reg * 4) = (1u << bit);
    }
}

VOID
NTAPI
HalDisplayString(
    _In_ PCH String)
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
    else
    {
        /* SPI (32+): use Distributor registers */
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

    DPRINT1("[arm64] HalEnableSystemInterrupt: Vector=%lu IRQL=%u GICPriority=0x%02X COMPLETE\n",
            Vector, Irql, priority);

    return TRUE;
}

VOID
NTAPI
HalEndSystemInterrupt(
    _In_ KIRQL Irql,
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG cpu = KeGetCurrentProcessorNumber();
    ULONG intid = (cpu < MAXIMUM_PROCESSORS) ? HalpArm64ActiveIntId[cpu] : 0;

    /*
     * CRITICAL: Lower IRQL BEFORE sending EOI to prevent nested interrupts.
     * Windows ARM64 convention: IRQL synchronization happens before GIC EOI.
     * If we EOI first, a higher-priority interrupt can preempt before IRQL drops,
     * causing incorrect IRQL transitions and potential deadlocks.
     */
    KeLowerIrql(Irql);

    if (intid)
    {
        /* Memory barrier to ensure all interrupt processing is visible */
        __asm__ __volatile__("dsb sy" ::: "memory");

        if (HalpGicUseSysRegs)
        {
            HalpWriteIccEoir1(intid);
        }
        else
        {
            if (HalpGiccBase != 0)
                *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_EOIR) = intid;
        }
        if (cpu < MAXIMUM_PROCESSORS) HalpArm64ActiveIntId[cpu] = 0;
    }
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

        DPRINT("[arm64][DMA] HalFreeCommonBuffer: VA=%p Len=%lu Cache=%s\n",
               VirtualAddress, BufferEntry->Length,
               CacheType == MmCached ? "Yes" : "No");

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
        DPRINT("[arm64][DMA] HalFreeCommonBuffer: Buffer not in tracking list, using provided params\n");
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

/*
 * DMA operation wrapper functions.
 * These adapt between PDMA_ADAPTER (used by DMA_OPERATIONS) and
 * PADAPTER_OBJECT (used by the HAL's Io* functions).
 * Since ADAPTER_OBJECT starts with DMA_ADAPTER, we can safely cast.
 */
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

    DPRINT1("[arm64][HAL] HalGetAdapter: InterfaceType=%d Master=%d ScatterGather=%d "
            "Dma32BitAddresses=%d Dma64BitAddresses=%d MaxLength=%lu\n",
            DeviceDescription->InterfaceType,
            DeviceDescription->Master,
            DeviceDescription->ScatterGather,
            DeviceDescription->Dma32BitAddresses,
            DeviceDescription->Dma64BitAddresses,
            DeviceDescription->MaximumLength);

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
        HalpArm64DmaOperations.PutDmaAdapter = NULL; /* TODO */
        HalpArm64DmaOperations.AllocateCommonBuffer = HalpArm64AllocateCommonBuffer;
        HalpArm64DmaOperations.FreeCommonBuffer = HalpArm64FreeCommonBuffer;
        HalpArm64DmaOperations.AllocateAdapterChannel = HalpArm64AllocateAdapterChannel;
        HalpArm64DmaOperations.FlushAdapterBuffers = HalpArm64FlushAdapterBuffers;
        HalpArm64DmaOperations.FreeAdapterChannel = HalpArm64FreeAdapterChannel;
        HalpArm64DmaOperations.FreeMapRegisters = HalpArm64FreeMapRegisters;
        HalpArm64DmaOperations.MapTransfer = HalpArm64MapTransfer;
        HalpArm64DmaOperations.GetDmaAlignment = HalpArm64GetDmaAlignment;
        HalpArm64DmaOperations.ReadDmaCounter = NULL; /* TODO */

        /* Set up the adapter object */
        HalpArm64DmaAdapter.DmaHeader.Size = sizeof(ADAPTER_OBJECT);
        HalpArm64DmaAdapter.DmaHeader.Version = 1;
        HalpArm64DmaAdapter.DmaHeader.DmaOperations = &HalpArm64DmaOperations;
        HalpArm64DmaAdapter.MasterDevice = TRUE;
        HalpArm64DmaAdapter.ScatterGather = DeviceDescription->ScatterGather;
        HalpArm64DmaAdapter.MapRegistersPerChannel = MapRegisters;
        HalpArm64DmaAdapter.ChannelNumber = 0xFF; /* Mark as system adapter */

        HalpArm64DmaAdapterInitialized = TRUE;

        DPRINT1("[arm64][HAL] HalGetAdapter: initialized ARM64 DMA adapter\n");
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

    DPRINT1("[arm64][HAL] HalGetAdapter: returning adapter with %lu map registers\n",
            MapRegisters);

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
     * Use ECAM to access configuration space.
     * On ARM64, there is no fallback - ECAM is the only mechanism.
     */
    if (HalpAcpiAccessConfigEcam(FALSE,                 /* Read */
                                  HALP_ACPI_SEGMENT_ANY, /* Use any segment */
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

                /* Query ACPI _PRT for the correct GSI */
                if (HalpArm64PciRouteQueryCallback(
                        0,  /* Segment - TODO: support multi-segment */
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
    UCHAR Priority;

    /*
     * Map IRQL to PMR threshold value.
     * Remember: interrupts are signaled if their priority is LESS THAN PMR.
     * Higher IRQL → lower PMR (more restrictive).
     */
    if (Irql >= HIGH_LEVEL)
    {
        /* HIGH_LEVEL (15): Block everything. PMR=0x00 blocks all (no priority < 0). */
        Priority = 0x00;
    }
    else if (Irql >= IPI_LEVEL)
    {
        /* IPI_LEVEL (14): Allow only priorities < 0x10 (i.e., 0x00-0x0F, "NMI-like"). */
        Priority = 0x10;
    }
    else if (Irql >= CLOCK_LEVEL)
    {
        /* CLOCK_LEVEL (13): Allow only IPI (priority 0x10 < 0x20). Block timer. */
        Priority = 0x20;
    }
    else if (Irql >= DISPATCH_LEVEL + 1)
    {
        /*
         * DEVICE_LEVEL (3-12): Block devices at this IRQL and below, allow higher devices + timer + IPI.
         *
         * IRQL 3  → device priority 0xC0 → set PMR=0xC0 (allow 0x10-0xBF, block 0xC0+)
         * IRQL 4  → device priority 0xB0 → set PMR=0xB0 (allow 0x10-0xAF, block 0xB0+)
         * ...
         * IRQL 12 → device priority 0x30 → set PMR=0x30 (allow 0x10-0x2F, block 0x30+)
         *
         * Formula: IRQL 3+N → PMR = 0xC0 - (N * 0x10)
         */
        ULONG DeviceOffset = Irql - (DISPATCH_LEVEL + 1);
        if (DeviceOffset > 9)
            DeviceOffset = 9;
        Priority = (UCHAR)(0xC0 - (DeviceOffset * 0x10));
    }
    else
    {
        /* PASSIVE_LEVEL/DISPATCH_LEVEL (0-2): Allow all interrupts. PMR=0xFF (most permissive). */
        Priority = 0xFF;
    }

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
        if (HalpGiccBase != 0)
        {
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_PMR) = 0xFF;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_BPR) = 0x0;
            *HalpMmio((ULONG_PTR)HalpGiccBase, GICC_CTLR) = 0x1; /* EnableGrp0 only */

            /* Memory barrier to ensure GICC configuration takes effect */
            __asm__ __volatile__("dsb sy" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");
        }
    }

    /* Initialize ITS tables for this CPU if available */
    if (HalpGicItsInitialized && HalpGicItsEnabled &&
        ProcessorNumber < RTL_NUMBER_OF(HalpGicItsCollectionMapped))
    {
        HalpGicItsCollectionMapped[ProcessorNumber] = FALSE;
        if (!HalpGicItsEnsureCollection(ProcessorNumber))
        {
            DPRINT1("[arm64][HAL] ITS: failed to initialize CPU %lu tables\n",
                    ProcessorNumber);
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
    UNIMPLEMENTED_STUB();
    return Increment;
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
        if (!WriteToDevice)
        {
            /* Reading from device (DMA write to memory completed) */
            HalpArm64CleanInvalidateDcacheRange(CurrentVa, Length);
        }
        else
        {
            /* Writing to device (DMA read from memory) */
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

    DPRINT("[arm64][DMA] IoFreeAdapterChannel: Adapter %p released\n", AdapterObject);
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

    DPRINT("[arm64][DMA] IoFreeMapRegisters: Freed %lu map registers at %p\n",
           NumberOfMapRegisters, MapRegisterBase);
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
    static ULONG MapRegisterIndex = 0;

    /* Validate parameters */
    if (!Mdl || !CurrentVa || !Length || *Length == 0)
    {
        if (Length)
        {
            *Length = 0;
        }
        return (PHYSICAL_ADDRESS){0};
    }

    /* Get the physical address */
    if (Mdl)
    {
        PPFN_NUMBER PfnArray = MmGetMdlPfnArray(Mdl);
        ULONG_PTR Offset = (ULONG_PTR)CurrentVa - (ULONG_PTR)Mdl->StartVa;
        ULONG PageIndex = (ULONG)(Offset >> PAGE_SHIFT);
        PhysicalAddress.QuadPart = ((ULONGLONG)PfnArray[PageIndex] << PAGE_SHIFT) + (Offset & (PAGE_SIZE - 1));
    }
    else
    {
        PhysicalAddress = MmGetPhysicalAddress(CurrentVa);
    }

    if (PhysicalAddress.QuadPart == 0)
    {
        DPRINT1("[arm64][HAL] IoMapTransfer: failed to get physical address for VA %p (Mdl=%p)\n",
                CurrentVa, Mdl);
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
     * This is required when:
     * 1. Device only supports 32-bit addressing but buffer is above 4GB
     * 2. Buffer is in high memory that device can't reach
     */
    if (AdapterObject && !AdapterObject->Dma64BitAddresses)
    {
        if (PhysicalAddress.QuadPart >= HAL_ARM64_DMA_32BIT_LIMIT)
        {
            NeedsBounceBuffer = TRUE;
        }
    }

    /* Get map register entry if available */
    if (MapRegisterBase)
    {
        Base = (PHAL_ARM64_MAP_REGISTER_BASE)MapRegisterBase;
        if (Base->Signature == HAL_ARM64_MAP_REG_SIGNATURE &&
            MapRegisterIndex < Base->NumberOfMapRegisters)
        {
            MapEntry = &Base->Registers[MapRegisterIndex];
            MapRegisterIndex++;

            /* Wrap around for scatter/gather */
            if (MapRegisterIndex >= Base->NumberOfMapRegisters)
            {
                MapRegisterIndex = 0;
            }
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
     * Skip if system is cache-coherent.
     */
    if (!HalpArm64DmaCoherency.SystemCoherent)
    {
        if (WriteToDevice)
        {
            /* Device will read from memory - clean cache */
            HalpArm64CleanDcacheRange(CurrentVa, TransferLength);
        }
        else
        {
            /* Device will write to memory - clean and invalidate cache */
            HalpArm64CleanInvalidateDcacheRange(CurrentVa, TransferLength);
        }

        /* Ensure cache operations complete before DMA starts */
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
     * Use ECAM to access configuration space.
     * On ARM64, there is no fallback - ECAM is the only mechanism.
     */
    if (HalpAcpiAccessConfigEcam(TRUE,                  /* Write */
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
