/*
 *
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            hal/arch/common/generic/dma.c
 * PURPOSE:         DMA functions
 * PROGRAMMERS:     David Welch (welch@mcmail.com)
 *                  Filip Navara (navaraf@reactos.com)
 * UPDATE HISTORY:
 *                  Created 22/05/98
 */

/**
 * @page DMA Implementation Notes
 *
 * Concepts:
 *
 * - Map register
 *
 *   Abstract encapsulation of physically contiguous buffer that resides
 *   in memory accessible by both the DMA device / controller and the system.
 *   The map registers are allocated and distributed on demand and are
 *   scarce resource.
 *
 *   The actual use of map registers is to allow transfers from/to buffer
 *   located in physical memory at address inaccessible by the DMA device /
 *   controller directly. For such transfers the map register buffers
 *   are used as intermediate data storage.
 *
 * - Master adapter
 *
 *   A container for map registers (typically corresponding to one physical
 *   bus connection type). There can be master adapters for 24-bit address
 *   ranges, 32-bit address ranges, etc. Every time a new DMA adapter is
 *   created it's associated with a corresponding master adapter that
 *   is used for any map register allocation requests.
 *
 * - Bus-master / Slave DMA
 *
 *   Slave DMA is term used for DMA transfers done by the system (E)ISA
 *   controller as opposed to transfers mastered by the device itself
 *   (hence the name).
 *
 *   For slave DMA special care is taken to actually access the system
 *   controller and handle the transfers. The relevant code is in
 *   HalpDmaInitializeEisaAdapter, HalReadDmaCounter, IoFlushAdapterBuffers
 *   and IoMapTransfer.
 *
 * Implementation:
 *
 * - Allocation of map registers
 *
 *   Initial set of map registers is allocated on the system start to
 *   ensure that low memory won't get filled up later. Additional map
 *   registers are allocated as needed by HalpGrowMapBuffers. This
 *   routine is called on two places:
 *
 *   - HalGetAdapter, since we're at PASSIVE_LEVEL and it's known that
 *     more map registers will probably be needed.
 *   - IoAllocateAdapterChannel (indirectly using HalpGrowMapBufferWorker
 *     since we're at DISPATCH_LEVEL and call HalpGrowMapBuffers directly)
 *     when no more map registers are free.
 *
 *   Note that even if no more map registers can be allocated it's not
 *   the end of the world. The adapters waiting for free map registers
 *   are queued in the master adapter's queue and once one driver hands
 *   back it's map registers (using IoFreeMapRegisters or indirectly using
 *   the execution routine callback in IoAllocateAdapterChannel) the
 *   queue gets processed and the map registers are reassigned.
 */

/* INCLUDES *****************************************************************/

#include <hal.h>
#include <suppress.h>

#define NDEBUG
#include <debug.h>

#define HALP_SG_STACK_ELEMENTS 16

#define HALP_DMA_MIN_REGISTERS              256
#define HALP_DMA_MAX_REGISTERS_PER_NODE     (128 * 1024)
#define HALP_DMA_DEFAULT_REGISTERS          1024
#define HALP_DMA_INITIAL_REGISTER_STEP      64
#define HALP_DMA_INITIAL_REGISTER_CAP       (HALP_DMA_INITIAL_REGISTER_STEP * 32)
/* Legacy ISA bounce pool is capped at 512 KiB (128 map registers). */
#define HALP_DMA_LEGACY_REGISTER_CAP        128
#define HALP_DMA_LEGACY_MAX_BYTES           (HALP_DMA_LEGACY_REGISTER_CAP << PAGE_SHIFT)
/* Never pre-allocate more than 2k map registers (~8 MiB) per adapter at boot. */
#define HALP_DMA_BOOTSTRAP_REGISTER_CAP     (HALP_DMA_INITIAL_REGISTER_STEP * 8)
#define HALP_DMA_BOOTSTRAP_BYTES            (HALP_DMA_BOOTSTRAP_REGISTER_CAP << PAGE_SHIFT)
#define HALP_DMA_LEGACY_SPLIT_MIN_BYTES     (16 << PAGE_SHIFT)

#ifndef _MINIHAL_
static KEVENT HalpDmaLock;
static KSPIN_LOCK HalpDmaAdapterListLock;
static LIST_ENTRY HalpDmaAdapterList;
static PADAPTER_OBJECT HalpEisaAdapter[8];
#endif
static BOOLEAN HalpEisaDma;
#ifndef _MINIHAL_
static BOOLEAN HalpDmaInitialized;
static ULONG HalpMaxMapRegisters;
static ULONG HalpInitialMapBufferBytes;
static BOOLEAN HalpDmaBootstrapInProgress;
typedef struct _HALP_DMA_NODE_ENTRY
{
    PADAPTER_OBJECT Adapter32;
    PADAPTER_OBJECT Adapter64;
} HALP_DMA_NODE_ENTRY, *PHALP_DMA_NODE_ENTRY;
static PHALP_DMA_NODE_ENTRY HalpDmaNodeTable;
static ULONG HalpDmaNodeCount;
static PADAPTER_OBJECT HalpLegacyMasterAdapter;
typedef PVOID
(NTAPI *PMM_ALLOCATE_CONTIGUOUS_MEMORY_SPECIFY_CACHE_NODE)(
    SIZE_T NumberOfBytes,
    PHYSICAL_ADDRESS LowestAcceptableAddress,
    PHYSICAL_ADDRESS HighestAcceptableAddress,
    PHYSICAL_ADDRESS BoundaryAddressMultiple,
    MEMORY_CACHING_TYPE CacheType,
    NODE_REQUIREMENT PreferredNode);
static PMM_ALLOCATE_CONTIGUOUS_MEMORY_SPECIFY_CACHE_NODE HalpMmAllocateContiguousMemorySpecifyCacheNode;
#endif

static const ULONG_PTR HalpEisaPortPage[8] = {
   FIELD_OFFSET(DMA_PAGE, Channel0),
   FIELD_OFFSET(DMA_PAGE, Channel1),
   FIELD_OFFSET(DMA_PAGE, Channel2),
   FIELD_OFFSET(DMA_PAGE, Channel3),
   0,
   FIELD_OFFSET(DMA_PAGE, Channel5),
   FIELD_OFFSET(DMA_PAGE, Channel6),
   FIELD_OFFSET(DMA_PAGE, Channel7)
};

static VOID HalpDmaEnsureInitialGoal(PADAPTER_OBJECT AdapterObject);
static VOID HalpDmaSatisfyInitialGoals(VOID);
static BOOLEAN NTAPI HalpGrowMapBuffers(PADAPTER_OBJECT AdapterObject,
                                       ULONG SizeOfMapBuffers);

#ifndef _MINIHAL_
NTSTATUS
NTAPI
HalCalculateScatterGatherListSize(
    IN PADAPTER_OBJECT AdapterObject,
    IN PMDL Mdl OPTIONAL,
    IN PVOID CurrentVa,
    IN ULONG Length,
    OUT PULONG ScatterGatherListSize,
    OUT PULONG pNumberOfMapRegisters);

NTSTATUS
NTAPI
HalBuildScatterGatherList(
    IN PADAPTER_OBJECT AdapterObject,
    IN PDEVICE_OBJECT DeviceObject,
    IN PMDL Mdl,
    IN PVOID CurrentVa,
    IN ULONG Length,
    IN PDRIVER_LIST_CONTROL ExecutionRoutine,
    IN PVOID Context,
    IN BOOLEAN WriteToDevice,
    IN PVOID ScatterGatherBuffer,
    IN ULONG ScatterGatherLength);

NTSTATUS
NTAPI
HalBuildMdlFromScatterGatherList(
    IN PDMA_ADAPTER DmaAdapter,
    IN PSCATTER_GATHER_LIST ScatterGather,
    IN PMDL OriginalMdl,
    OUT PMDL *TargetMdl);

static NTSTATUS
NTAPI
HalpDmaAllocateAdapterChannelEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID DmaTransferContext,
    _In_ ULONG NumberOfMapRegisters,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_CONTROL ExecutionRoutine,
    _In_opt_ PVOID ExecutionContext,
    _Out_opt_ PVOID *MapRegisterBase);

static NTSTATUS
NTAPI
HalpDmaMapTransferEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ ULONGLONG Offset,
    _In_ ULONG DeviceOffset,
    _Inout_ PULONG Length,
    _In_ BOOLEAN WriteToDevice,
    _Out_writes_bytes_(ScatterGatherBufferLength) PSCATTER_GATHER_LIST ScatterGatherBuffer,
    _In_ ULONG ScatterGatherBufferLength,
    _In_opt_ PDMA_COMPLETION_ROUTINE DmaCompletionRoutine,
    _In_opt_ PVOID CompletionContext);

static NTSTATUS
NTAPI
HalpDmaGetScatterGatherListEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID DmaTransferContext,
    _In_ PMDL Mdl,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_opt_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _In_opt_ PDMA_COMPLETION_ROUTINE DmaCompletionRoutine,
    _In_opt_ PVOID CompletionContext,
    _Out_opt_ PSCATTER_GATHER_LIST *ScatterGatherList);

static NTSTATUS
NTAPI
HalpDmaBuildScatterGatherListEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID DmaTransferContext,
    _In_ PMDL Mdl,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_opt_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _Inout_updates_bytes_(ScatterGatherLength) PVOID ScatterGatherBuffer,
    _In_ ULONG ScatterGatherLength,
    _In_opt_ PDMA_COMPLETION_ROUTINE DmaCompletionRoutine,
    _In_opt_ PVOID CompletionContext,
    _Out_opt_ PVOID ScatterGatherList);

static NTSTATUS
NTAPI
HalpDmaFlushAdapterBuffersEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice);


static DMA_OPERATIONS HalpDmaOperations = {
   sizeof(DMA_OPERATIONS),
   (PPUT_DMA_ADAPTER)HalPutDmaAdapter,
   (PALLOCATE_COMMON_BUFFER)HalAllocateCommonBuffer,
   (PFREE_COMMON_BUFFER)HalFreeCommonBuffer,
   NULL, /* Initialized in HalpInitDma() */
   NULL, /* Initialized in HalpInitDma() */
   NULL, /* Initialized in HalpInitDma() */
   NULL, /* Initialized in HalpInitDma() */
   NULL, /* Initialized in HalpInitDma() */
   (PGET_DMA_ALIGNMENT)HalpDmaGetDmaAlignment,
   (PREAD_DMA_COUNTER)HalReadDmaCounter,
   (PGET_SCATTER_GATHER_LIST)HalGetScatterGatherList,
   (PPUT_SCATTER_GATHER_LIST)HalPutScatterGatherList,
   (PCALCULATE_SCATTER_GATHER_LIST_SIZE)HalCalculateScatterGatherListSize,
   (PBUILD_SCATTER_GATHER_LIST)HalBuildScatterGatherList,
   (PBUILD_MDL_FROM_SCATTER_GATHER_LIST)HalBuildMdlFromScatterGatherList
};
#endif

#define TAG_DMA ' AMD'

#ifndef _MINIHAL_
static ULONG
HalpQueryNumaNodeCount(VOID)
{
    return 1;
}

static ULONGLONG
HalpQueryTotalPhysicalPages(VOID)
{
    PPHYSICAL_MEMORY_RANGE MemoryRanges;
    PPHYSICAL_MEMORY_RANGE Range;
    ULONGLONG TotalPages = 0;

    MemoryRanges = MmGetPhysicalMemoryRanges();
    if (MemoryRanges == NULL)
    {
        return 0;
    }

    for (Range = MemoryRanges;
         Range->BaseAddress.QuadPart || Range->NumberOfBytes.QuadPart;
         Range++)
    {
        TotalPages += (Range->NumberOfBytes.QuadPart >> PAGE_SHIFT);
    }

    ExFreePool(MemoryRanges);
    return TotalPages;
}

static ULONG
HalpSelectMaxMapRegisters(
    _In_ ULONGLONG TotalPages,
    _In_ ULONG NodeCount)
{
    ULONGLONG PagesPerNode;
    ULONGLONG RegistersPerNode;

    if (NodeCount == 0)
    {
        NodeCount = 1;
    }

    if (TotalPages == 0)
    {
        return HALP_DMA_DEFAULT_REGISTERS;
    }

    PagesPerNode = TotalPages / NodeCount;
    if (PagesPerNode == 0)
    {
        PagesPerNode = TotalPages;
    }

    RegistersPerNode = PagesPerNode / 32ULL;
    if (RegistersPerNode < HALP_DMA_MIN_REGISTERS)
    {
        RegistersPerNode = HALP_DMA_MIN_REGISTERS;
    }
    if (RegistersPerNode > HALP_DMA_MAX_REGISTERS_PER_NODE)
    {
        RegistersPerNode = HALP_DMA_MAX_REGISTERS_PER_NODE;
    }

    return (ULONG)(RegistersPerNode * NodeCount);
}

static ULONG
HalpSelectInitialMapBufferBytes(
    _In_ ULONG MaxRegisters)
{
    ULONG InitialRegisters;

    if (MaxRegisters == 0)
    {
        return HALP_DMA_INITIAL_REGISTER_STEP << PAGE_SHIFT;
    }

    InitialRegisters = MaxRegisters / 8;
    if (InitialRegisters < HALP_DMA_INITIAL_REGISTER_STEP)
    {
        InitialRegisters = HALP_DMA_INITIAL_REGISTER_STEP;
    }
    if (InitialRegisters > HALP_DMA_INITIAL_REGISTER_CAP)
    {
        InitialRegisters = HALP_DMA_INITIAL_REGISTER_CAP;
    }
    if (InitialRegisters > MaxRegisters)
    {
        InitialRegisters = MaxRegisters;
    }

    return InitialRegisters << PAGE_SHIFT;
}

static ULONG
HalpDmaDeriveMapRegisterCount(
    _In_ PDEVICE_DESCRIPTION DeviceDescription,
    _In_ ULONG MaximumLengthPages)
{
    UNREFERENCED_PARAMETER(DeviceDescription);
    return MaximumLengthPages;
}

static ULONG
HalpDmaQueryCurrentNode(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    if ((Prcb != NULL) && (Prcb->ParentNode != NULL))
    {
        return Prcb->ParentNode->NodeNumber;
    }

    return 0;
}

static PADAPTER_OBJECT
HalpDmaSelectMasterAdapter(
    _In_ PDEVICE_DESCRIPTION DeviceDescription)
{
    PADAPTER_OBJECT Adapter;
    ULONG Node;
    BOOLEAN Use64Bit;

    if (!DeviceDescription->Master || (HalpDmaNodeTable == NULL))
    {
        return HalpLegacyMasterAdapter;
    }

    if ((DeviceDescription->InterfaceType == Isa) ||
        (DeviceDescription->InterfaceType == Eisa))
    {
        return HalpLegacyMasterAdapter;
    }

    Use64Bit = DeviceDescription->Dma64BitAddresses ? TRUE : FALSE;
    Node = HalpDmaQueryCurrentNode();

    if (Node >= HalpDmaNodeCount)
    {
        Node = 0;
    }

    Adapter = Use64Bit ? HalpDmaNodeTable[Node].Adapter64 : HalpDmaNodeTable[Node].Adapter32;
    if (!Adapter)
    {
        Adapter = Use64Bit ? HalpDmaNodeTable[0].Adapter64 : HalpDmaNodeTable[0].Adapter32;
    }

    if (!Adapter)
    {
        Adapter = HalpLegacyMasterAdapter;
    }

    return Adapter;
}
#endif

static VOID
HalpDmaEnsureInitialGoal(
    _In_opt_ PADAPTER_OBJECT AdapterObject)
{
    ULONG CurrentBytes;
    ULONG NeededBytes;

    if ((AdapterObject == NULL) || (AdapterObject->InitialMapBufferBytesGoal == 0))
    {
        return;
    }

    CurrentBytes = AdapterObject->NumberOfMapRegisters << PAGE_SHIFT;
    if (CurrentBytes >= AdapterObject->InitialMapBufferBytesGoal)
    {
        AdapterObject->InitialMapBufferBytesGoal = 0;
        return;
    }

    NeededBytes = AdapterObject->InitialMapBufferBytesGoal - CurrentBytes;
    if (!HalpGrowMapBuffers(AdapterObject, NeededBytes))
    {
        DPRINT1("HAL DMA: deferred bootstrap grow failed (adapter=%p, bytes=%lu)\n",
                AdapterObject,
                NeededBytes);
        AdapterObject->InitialMapBufferBytesGoal = 0;
    }
}

static VOID
HalpDmaSatisfyInitialGoals(VOID)
{
    if (HalpLegacyMasterAdapter)
    {
        HalpDmaEnsureInitialGoal(HalpLegacyMasterAdapter);
    }

    if (!HalpDmaNodeTable)
    {
        return;
    }

    for (ULONG Index = 0; Index < HalpDmaNodeCount; Index++)
    {
        HalpDmaEnsureInitialGoal(HalpDmaNodeTable[Index].Adapter32);
        HalpDmaEnsureInitialGoal(HalpDmaNodeTable[Index].Adapter64);
    }
}

/* FUNCTIONS *****************************************************************/

#if defined(SARCH_PC98)
/*
 * Disable I/O for safety.
 * FIXME: Add support for PC-98 DMA controllers.
 */
#undef WRITE_PORT_UCHAR
#undef READ_PORT_UCHAR

#define WRITE_PORT_UCHAR(Port, Data) \
    do { \
        UNIMPLEMENTED; \
        (Port); \
        (Data); \
    } while (0)

#define READ_PORT_UCHAR(Port) 0x00
#endif

#ifndef _MINIHAL_
CODE_SEG("INIT")
VOID
HalpInitDma(VOID)
{
    ULONG NodeCount;
    ULONGLONG TotalPages;
    ULONG BootstrapMapBufferBytes;

    if (HalpDmaInitialized)
    {
        return;
    }

    HalpDmaInitialized = TRUE;
    HalpDmaBootstrapInProgress = TRUE;

    TotalPages = HalpQueryTotalPhysicalPages();
    NodeCount = HalpQueryNumaNodeCount();
    HalpMaxMapRegisters = HalpSelectMaxMapRegisters(TotalPages, NodeCount);
    HalpInitialMapBufferBytes = HalpSelectInitialMapBufferBytes(HalpMaxMapRegisters);
    BootstrapMapBufferBytes = 0;

    /*
     * Initialize the DMA Operation table
     */
    HalpDmaOperations.AllocateAdapterChannel = (PALLOCATE_ADAPTER_CHANNEL)IoAllocateAdapterChannel;
    HalpDmaOperations.FlushAdapterBuffers = (PFLUSH_ADAPTER_BUFFERS)IoFlushAdapterBuffers;
    HalpDmaOperations.FreeAdapterChannel = (PFREE_ADAPTER_CHANNEL)IoFreeAdapterChannel;
    HalpDmaOperations.FreeMapRegisters = (PFREE_MAP_REGISTERS)IoFreeMapRegisters;
    HalpDmaOperations.MapTransfer = (PMAP_TRANSFER)IoMapTransfer;
    HalpDmaOperations.AllocateAdapterChannelEx = HalpDmaAllocateAdapterChannelEx;
    HalpDmaOperations.MapTransferEx = HalpDmaMapTransferEx;
    HalpDmaOperations.GetScatterGatherListEx = HalpDmaGetScatterGatherListEx;
    HalpDmaOperations.BuildScatterGatherListEx = HalpDmaBuildScatterGatherListEx;
    HalpDmaOperations.FlushAdapterBuffersEx = HalpDmaFlushAdapterBuffersEx;

    if (HalpMmAllocateContiguousMemorySpecifyCacheNode == NULL)
    {
        UNICODE_STRING RoutineName;
        RtlInitUnicodeString(&RoutineName, L"MmAllocateContiguousMemorySpecifyCacheNode");
        HalpMmAllocateContiguousMemorySpecifyCacheNode =
            (PMM_ALLOCATE_CONTIGUOUS_MEMORY_SPECIFY_CACHE_NODE)MmGetSystemRoutineAddress(&RoutineName);
    }

    if (HalpBusType == MACHINE_TYPE_EISA)
    {
        /*
        * Check if Extended DMA is available. We're just going to do a random
        * read and write.
        */
        WRITE_PORT_UCHAR(UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaController2Pages.Channel2)), 0x2A);
        if (READ_PORT_UCHAR(UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaController2Pages.Channel2))) == 0x2A)
        {
            DPRINT1("Machine supports EISA DMA. Bus type: %lu\n", HalpBusType);
            HalpEisaDma = TRUE;
        }
    }

    /*
     * Intialize all the global variables and allocate master adapter with
     * first map buffers.
     */
    InitializeListHead(&HalpDmaAdapterList);
    KeInitializeSpinLock(&HalpDmaAdapterListLock);
    KeInitializeEvent(&HalpDmaLock, NotificationEvent, TRUE);

    if (NodeCount == 0)
    {
        NodeCount = 1;
    }

    HalpDmaNodeCount = NodeCount;
    HalpDmaNodeTable = ExAllocatePoolWithTag(NonPagedPool,
                                             sizeof(HALP_DMA_NODE_ENTRY) * HalpDmaNodeCount,
                                             TAG_DMA);
    if (!HalpDmaNodeTable)
    {
        KeBugCheckEx(HAL_INITIALIZATION_FAILED,
                     'AMDN',
                     HalpDmaNodeCount,
                     HalpMaxMapRegisters,
                     0);
    }
    RtlZeroMemory(HalpDmaNodeTable, sizeof(HALP_DMA_NODE_ENTRY) * HalpDmaNodeCount);

    HalpLegacyMasterAdapter = HalpDmaAllocateMasterAdapter(HalpMaxMapRegisters,
                                                          BootstrapMapBufferBytes,
                                                          0,
                                                          FALSE,
                                                          TRUE);
    if (!HalpLegacyMasterAdapter)
    {
        KeBugCheckEx(HAL_INITIALIZATION_FAILED,
                     'AMDL',
                     HalpMaxMapRegisters,
                     HalpInitialMapBufferBytes,
                     0);
    }
    {
        ULONG LegacyGoal = HalpInitialMapBufferBytes;
        if (LegacyGoal > HALP_DMA_LEGACY_MAX_BYTES)
        {
            LegacyGoal = HALP_DMA_LEGACY_MAX_BYTES;
        }
        HalpLegacyMasterAdapter->InitialMapBufferBytesGoal = LegacyGoal;
    }

    for (ULONG Index = 0; Index < HalpDmaNodeCount; Index++)
    {
        HalpDmaNodeTable[Index].Adapter32 =
            HalpDmaAllocateMasterAdapter(HalpMaxMapRegisters,
                                         0,
                                         (UCHAR)Index,
                                         FALSE,
                                         FALSE);
        HalpDmaNodeTable[Index].Adapter64 =
            HalpDmaAllocateMasterAdapter(HalpMaxMapRegisters,
                                         0,
                                         (UCHAR)Index,
                                         TRUE,
                                         FALSE);

        if (!HalpDmaNodeTable[Index].Adapter32 || !HalpDmaNodeTable[Index].Adapter64)
        {
            KeBugCheckEx(HAL_INITIALIZATION_FAILED,
                         'AMDX',
                         Index,
                         (ULONG_PTR)HalpDmaNodeTable[Index].Adapter32,
                         (ULONG_PTR)HalpDmaNodeTable[Index].Adapter64);
        }

        HalpDmaNodeTable[Index].Adapter32->InitialMapBufferBytesGoal = HalpInitialMapBufferBytes;
        HalpDmaNodeTable[Index].Adapter64->InitialMapBufferBytesGoal = HalpInitialMapBufferBytes;
    }

    /*
     * Setup the HalDispatchTable callback for creating PnP DMA adapters. It's
     * used by IoGetDmaAdapter in the kernel.
     */
    HalGetDmaAdapter = HalpGetDmaAdapter;

    HalpDmaBootstrapInProgress = FALSE;
    HalpDmaSatisfyInitialGoals();
}
#endif

#ifndef _MINIHAL_
static NTSTATUS
NTAPI
HalpDmaAllocateAdapterChannelEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID DmaTransferContext,
    _In_ ULONG NumberOfMapRegisters,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_CONTROL ExecutionRoutine,
    _In_opt_ PVOID ExecutionContext,
    _Out_opt_ PVOID *MapRegisterBase)
{
    PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    PVOID Context = ExecutionContext ? ExecutionContext : DmaTransferContext;

    if (MapRegisterBase)
    {
        *MapRegisterBase = NULL;
    }

    if (Flags != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (ExecutionRoutine == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return IoAllocateAdapterChannel(AdapterObject,
                                    DeviceObject,
                                    NumberOfMapRegisters,
                                    ExecutionRoutine,
                                    Context);
}

static ULONG
HalpDmaComputeScatterGatherCapacity(
    _In_ ULONG BufferLength)
{
    ULONG Capacity;

    if (BufferLength < sizeof(SCATTER_GATHER_LIST))
    {
        return 0;
    }

    Capacity = 1;
    Capacity += (BufferLength - sizeof(SCATTER_GATHER_LIST)) / sizeof(SCATTER_GATHER_ELEMENT);
    return Capacity;
}

static NTSTATUS
NTAPI
HalpDmaMapTransferEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ ULONGLONG Offset,
    _In_ ULONG DeviceOffset,
    _Inout_ PULONG Length,
    _In_ BOOLEAN WriteToDevice,
    _Out_writes_bytes_(ScatterGatherBufferLength) PSCATTER_GATHER_LIST ScatterGatherBuffer,
    _In_ ULONG ScatterGatherBufferLength,
    _In_opt_ PDMA_COMPLETION_ROUTINE DmaCompletionRoutine,
    _In_opt_ PVOID CompletionContext)
{
    PADAPTER_OBJECT AdapterObject = (PADAPTER_OBJECT)DmaAdapter;
    ULONG Capacity;
    ULONG Remaining;
    ULONG ElementCount = 0;
    PVOID CurrentVa;
    PHYSICAL_ADDRESS Address;

    UNREFERENCED_PARAMETER(DeviceOffset);

    if (DmaCompletionRoutine || CompletionContext)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (!ScatterGatherBuffer || !Length)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Capacity = HalpDmaComputeScatterGatherCapacity(ScatterGatherBufferLength);
    if (Capacity == 0)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Remaining = *Length;
    CurrentVa = (PVOID)((PUCHAR)MmGetMdlVirtualAddress(Mdl) + Offset);

    while ((Remaining > 0) && (ElementCount < Capacity))
    {
        ULONG Chunk = Remaining;

        Address = IoMapTransfer(AdapterObject,
                                Mdl,
                                MapRegisterBase,
                                CurrentVa,
                                &Chunk,
                                WriteToDevice);

        if (Chunk == 0)
        {
            break;
        }

        ScatterGatherBuffer->Elements[ElementCount].Address = Address;
        ScatterGatherBuffer->Elements[ElementCount].Length = Chunk;
        ScatterGatherBuffer->Elements[ElementCount].Reserved = 0;

        Remaining -= Chunk;
        CurrentVa = (PVOID)((PUCHAR)CurrentVa + Chunk);
        ElementCount++;
    }

    if (Remaining > 0)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ScatterGatherBuffer->NumberOfElements = ElementCount;
    ScatterGatherBuffer->Reserved = 0;
    *Length -= Remaining;

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
HalpDmaGetScatterGatherListEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID DmaTransferContext,
    _In_ PMDL Mdl,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_opt_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _In_opt_ PDMA_COMPLETION_ROUTINE DmaCompletionRoutine,
    _In_opt_ PVOID CompletionContext,
    _Out_opt_ PSCATTER_GATHER_LIST *ScatterGatherList)
{
    PVOID CurrentVa;

    UNREFERENCED_PARAMETER(DmaTransferContext);
    UNREFERENCED_PARAMETER(CompletionContext);

    if ((Flags != 0) || ScatterGatherList || DmaCompletionRoutine)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (ExecutionRoutine == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    CurrentVa = (PVOID)((PUCHAR)MmGetMdlVirtualAddress(Mdl) + Offset);

    return HalBuildScatterGatherList((PADAPTER_OBJECT)DmaAdapter,
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

static NTSTATUS
NTAPI
HalpDmaBuildScatterGatherListEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID DmaTransferContext,
    _In_ PMDL Mdl,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_opt_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _Inout_updates_bytes_(ScatterGatherLength) PVOID ScatterGatherBuffer,
    _In_ ULONG ScatterGatherLength,
    _In_opt_ PDMA_COMPLETION_ROUTINE DmaCompletionRoutine,
    _In_opt_ PVOID CompletionContext,
    _Out_opt_ PVOID ScatterGatherList)
{
    PVOID CurrentVa;

    UNREFERENCED_PARAMETER(DmaTransferContext);
    UNREFERENCED_PARAMETER(ScatterGatherList);

    if ((Flags != 0) || DmaCompletionRoutine || CompletionContext)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (ExecutionRoutine == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    CurrentVa = (PVOID)((PUCHAR)MmGetMdlVirtualAddress(Mdl) + Offset);

    return HalBuildScatterGatherList((PADAPTER_OBJECT)DmaAdapter,
                                     DeviceObject,
                                     Mdl,
                                     CurrentVa,
                                     Length,
                                     ExecutionRoutine,
                                     Context,
                                     WriteToDevice,
                                     ScatterGatherBuffer,
                                     ScatterGatherLength);
}

static NTSTATUS
NTAPI
HalpDmaFlushAdapterBuffersEx(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    PVOID CurrentVa;

    CurrentVa = (PVOID)((PUCHAR)MmGetMdlVirtualAddress(Mdl) + Offset);

    if (IoFlushAdapterBuffers((PADAPTER_OBJECT)DmaAdapter,
                              Mdl,
                              MapRegisterBase,
                              CurrentVa,
                              Length,
                              WriteToDevice))
    {
        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}
#endif

/**
 * @name HalpGetAdapterMaximumPhysicalAddress
 *
 * Get the maximum physical address acceptable by the device represented
 * by the passed DMA adapter.
 */
PHYSICAL_ADDRESS
NTAPI
HalpGetAdapterMaximumPhysicalAddress(IN PADAPTER_OBJECT AdapterObject)
{
    PHYSICAL_ADDRESS HighestAddress;

    if (AdapterObject->MasterDevice)
    {
        if (AdapterObject->Dma64BitAddresses)
        {
            HighestAddress.QuadPart = 0xFFFFFFFFFFFFFFFFULL;
            return HighestAddress;
        }
        else if (AdapterObject->Dma32BitAddresses)
        {
            HighestAddress.QuadPart = 0xFFFFFFFF;
            return HighestAddress;
        }
    }

    HighestAddress.QuadPart = 0xFFFFFF;
    return HighestAddress;
}

#ifndef _MINIHAL_
/**
 * @name HalpGrowMapBuffers
 *
 * Allocate initial, or additional, map buffers for DMA master adapter.
 *
 * @param MasterAdapter
 *        DMA master adapter to allocate buffers for.
 * @param SizeOfMapBuffers
 *        Size of the map buffers to allocate (not including the size
 *        already allocated).
 */
BOOLEAN
NTAPI
HalpGrowMapBuffers(IN PADAPTER_OBJECT AdapterObject,
                  IN ULONG SizeOfMapBuffers)
{
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    PHYSICAL_ADDRESS HighestAcceptableAddress;
    PHYSICAL_ADDRESS LowestAcceptableAddress;
    PHYSICAL_ADDRESS BoundryAddressMultiple;
    KIRQL OldIrql;
    ULONG MapRegisterCount;
    ULONG Capacity;
    ULONG AllocationBytes = SizeOfMapBuffers;
    ULONG AttemptBytes;
    BOOLEAN PartialAllowed;

    if ((AdapterObject->InitialMapBufferBytesGoal != 0) && !HalpDmaBootstrapInProgress)
    {
        ULONG CurrentBytes = AdapterObject->NumberOfMapRegisters << PAGE_SHIFT;
        if (CurrentBytes >= AdapterObject->InitialMapBufferBytesGoal)
        {
            AdapterObject->InitialMapBufferBytesGoal = 0;
        }
        else
        {
            ULONG Remaining = AdapterObject->InitialMapBufferBytesGoal - CurrentBytes;
            if (AllocationBytes < Remaining)
            {
                AllocationBytes = Remaining;
            }
        }
    }

    Capacity = AdapterObject->MapRegisters->SizeOfBitMap;
    AttemptBytes = AllocationBytes;
    PartialAllowed = (AdapterObject->MasterAdapter == HalpLegacyMasterAdapter) &&
                     (AdapterObject->InitialMapBufferBytesGoal != 0);

    for (;;)
    {
        MapRegisterCount = BYTES_TO_PAGES(AttemptBytes);
        if (MapRegisterCount == 0)
        {
            return FALSE;
        }

        if (MapRegisterCount + AdapterObject->NumberOfMapRegisters > Capacity)
        {
            if (!PartialAllowed)
            {
                DPRINT("No more map register slots available! (Current: %d | Requested: %d | Limit: %d)\n",
                       AdapterObject->NumberOfMapRegisters,
                       MapRegisterCount,
                       Capacity);
                return FALSE;
            }

            AttemptBytes >>= 1;
            if (AttemptBytes < HALP_DMA_LEGACY_SPLIT_MIN_BYTES)
            {
                DPRINT1("HAL DMA: unable to reserve legacy map registers (min split reached)\n");
                return FALSE;
            }
            continue;
        }

        HighestAcceptableAddress = HalpGetAdapterMaximumPhysicalAddress(AdapterObject);
        LowestAcceptableAddress.HighPart = 0;
        LowestAcceptableAddress.LowPart = HighestAcceptableAddress.LowPart == 0xFFFFFFFF ? 0x1000000 : 0;
        BoundryAddressMultiple.QuadPart = 0;

        VirtualAddress = NULL;

        if (AdapterObject->MasterDevice &&
            (HalpMmAllocateContiguousMemorySpecifyCacheNode != NULL))
        {
            NODE_REQUIREMENT PreferredNode = AdapterObject->NumaNode;
            if (PreferredNode == 0 && !AdapterObject->Dma32BitAddresses && !AdapterObject->Dma64BitAddresses)
            {
                PreferredNode = MM_ANY_NODE_OK;
            }

            VirtualAddress = HalpMmAllocateContiguousMemorySpecifyCacheNode(AttemptBytes,
                                                                            LowestAcceptableAddress,
                                                                            HighestAcceptableAddress,
                                                                            BoundryAddressMultiple,
                                                                            MmNonCached,
                                                                            PreferredNode);

            if ((!VirtualAddress) && (PreferredNode != MM_ANY_NODE_OK))
            {
                VirtualAddress = HalpMmAllocateContiguousMemorySpecifyCacheNode(AttemptBytes,
                                                                                LowestAcceptableAddress,
                                                                                HighestAcceptableAddress,
                                                                                BoundryAddressMultiple,
                                                                                MmNonCached,
                                                                                MM_ANY_NODE_OK);
            }
        }

        if (!VirtualAddress)
        {
            VirtualAddress = MmAllocateContiguousMemorySpecifyCache(AttemptBytes,
                                                                    LowestAcceptableAddress,
                                                                    HighestAcceptableAddress,
                                                                    BoundryAddressMultiple,
                                                                    MmNonCached);
            if ((!VirtualAddress) && (LowestAcceptableAddress.LowPart))
            {
                LowestAcceptableAddress.LowPart = 0;
                VirtualAddress = MmAllocateContiguousMemorySpecifyCache(AttemptBytes,
                                                                        LowestAcceptableAddress,
                                                                        HighestAcceptableAddress,
                                                                        BoundryAddressMultiple,
                                                                        MmNonCached);
            }
        }

        if (VirtualAddress)
        {
            AllocationBytes = AttemptBytes;
            break;
        }

        if (!PartialAllowed || AttemptBytes <= HALP_DMA_LEGACY_SPLIT_MIN_BYTES)
        {
            DPRINT1("HAL DMA: MmAllocateContiguousMemorySpecifyCache[Node] failed (Node=%u, Size=%lu)\n",
                    AdapterObject->NumaNode,
                    AttemptBytes);
            return FALSE;
        }

        AttemptBytes >>= 1;
    }

    PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);

    /*
     * All the following must be done with the master adapter lock held
     * to prevent corruption.
     */
    KeAcquireSpinLock(&AdapterObject->SpinLock, &OldIrql);

    /*
     * Setup map register entries for the buffer allocated. Each entry has
     * a virtual and physical address and corresponds to PAGE_SIZE large
     * buffer.
     */
    if (MapRegisterCount > 0)
    {
        PROS_MAP_REGISTER_ENTRY CurrentEntry, PreviousEntry;

        CurrentEntry = AdapterObject->MapRegisterBase + AdapterObject->NumberOfMapRegisters;
        do
        {
            /*
             * Leave one entry free for every non-contiguous memory region
             * in the map register bitmap. This ensures that we can search
             * using RtlFindClearBits for contiguous map register regions.
             *
             * Also for non-EISA DMA leave one free entry for every 64Kb
             * break, because the DMA controller can handle only coniguous
             * 64Kb regions.
             */
            if (CurrentEntry != AdapterObject->MapRegisterBase)
            {
                PreviousEntry = CurrentEntry - 1;
                if ((PreviousEntry->PhysicalAddress.LowPart + PAGE_SIZE) == PhysicalAddress.LowPart)
                {
                    if (!HalpEisaDma)
                    {
                        if ((PreviousEntry->PhysicalAddress.LowPart ^ PhysicalAddress.LowPart) & 0xFFFF0000)
                        {
                            CurrentEntry++;
                            AdapterObject->NumberOfMapRegisters++;
                        }
                    }
                }
                else
                {
                    CurrentEntry++;
                    AdapterObject->NumberOfMapRegisters++;
                }
            }

            RtlClearBit(AdapterObject->MapRegisters,
                        (ULONG)(CurrentEntry - AdapterObject->MapRegisterBase));
            CurrentEntry->VirtualAddress = VirtualAddress;
            CurrentEntry->PhysicalAddress = PhysicalAddress;

            PhysicalAddress.LowPart += PAGE_SIZE;
            VirtualAddress = (PVOID)((ULONG_PTR)VirtualAddress + PAGE_SIZE);

            CurrentEntry++;
            AdapterObject->NumberOfMapRegisters++;
            MapRegisterCount--;
        } while (MapRegisterCount);
    }

    KeReleaseSpinLock(&AdapterObject->SpinLock, OldIrql);

    if ((AdapterObject->InitialMapBufferBytesGoal != 0) &&
        !HalpDmaBootstrapInProgress &&
        ((AdapterObject->NumberOfMapRegisters << PAGE_SHIFT) >= AdapterObject->InitialMapBufferBytesGoal))
    {
        AdapterObject->InitialMapBufferBytesGoal = 0;
    }

    return TRUE;
}

/**
 * @name HalpDmaAllocateMasterAdapter
 *
 * Helper routine to allocate and initialize master adapter object and it's
 * associated map register buffers.
 *
 * @see HalpInitDma
 */
PADAPTER_OBJECT
NTAPI
HalpDmaAllocateMasterAdapter(IN ULONG RegisterCapacity,
                             IN ULONG InitialMapBufferBytes,
                             IN UCHAR NumaNode,
                             IN BOOLEAN Supports64Bit,
                             IN BOOLEAN LegacyAdapter)
{
    PADAPTER_OBJECT MasterAdapter;
    ULONG Size, SizeOfBitmap;

    if (RegisterCapacity == 0)
    {
        RegisterCapacity = HALP_DMA_DEFAULT_REGISTERS;
    }

    SizeOfBitmap = RegisterCapacity;
    Size = sizeof(ADAPTER_OBJECT);
    Size += sizeof(RTL_BITMAP);
    Size += (SizeOfBitmap + 7) >> 3;

    MasterAdapter = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_DMA);
    if (!MasterAdapter) return NULL;

    RtlZeroMemory(MasterAdapter, Size);

    KeInitializeSpinLock(&MasterAdapter->SpinLock);
    InitializeListHead(&MasterAdapter->AdapterQueue);

    MasterAdapter->MapRegisters = (PVOID)(MasterAdapter + 1);
    RtlInitializeBitMap(MasterAdapter->MapRegisters,
                        (PULONG)(MasterAdapter->MapRegisters + 1),
                        SizeOfBitmap);
    RtlSetAllBits(MasterAdapter->MapRegisters);
    MasterAdapter->NumberOfMapRegisters = 0;
    MasterAdapter->CommittedMapRegisters = 0;
    MasterAdapter->InitialMapBufferBytesGoal = InitialMapBufferBytes;
    MasterAdapter->MasterDevice = LegacyAdapter ? FALSE : TRUE;
    MasterAdapter->ScatterGather = TRUE;
    MasterAdapter->Dma32BitAddresses = (!LegacyAdapter && !Supports64Bit);
    MasterAdapter->Dma64BitAddresses = Supports64Bit;
    MasterAdapter->NumaNode = NumaNode;

    MasterAdapter->MapRegisterBase = ExAllocatePoolWithTag(NonPagedPool,
                                                           SizeOfBitmap *
                                                           sizeof(ROS_MAP_REGISTER_ENTRY),
                                                           TAG_DMA);
    if (!MasterAdapter->MapRegisterBase)
    {
        ExFreePool(MasterAdapter);
        return NULL;
    }

    RtlZeroMemory(MasterAdapter->MapRegisterBase,
                  SizeOfBitmap * sizeof(ROS_MAP_REGISTER_ENTRY));
    if ((InitialMapBufferBytes != 0) &&
        (!HalpGrowMapBuffers(MasterAdapter, InitialMapBufferBytes)))
    {
        ExFreePool(MasterAdapter->MapRegisterBase);
        ExFreePool(MasterAdapter);
        return NULL;
    }

    return MasterAdapter;
}

/**
 * @name HalpDmaAllocateChildAdapter
 *
 * Helper routine of HalGetAdapter. Allocate child adapter object and
 * fill out some basic fields.
 *
 * @see HalGetAdapter
 */
PADAPTER_OBJECT
NTAPI
HalpDmaAllocateChildAdapter(IN ULONG NumberOfMapRegisters,
                            IN PDEVICE_DESCRIPTION DeviceDescription,
                            IN PADAPTER_OBJECT MasterAdapter)
{
    PADAPTER_OBJECT AdapterObject;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    HANDLE Handle;

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_KERNEL_HANDLE | OBJ_PERMANENT,
                               NULL,
                               NULL);

    Status = ObCreateObject(KernelMode,
                            IoAdapterObjectType,
                            &ObjectAttributes,
                            KernelMode,
                            NULL,
                            sizeof(ADAPTER_OBJECT),
                            0,
                            0,
                            (PVOID)&AdapterObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HAL DMA: ObCreateObject failed %lx for %lu map registers\n",
                Status,
                NumberOfMapRegisters);
        return NULL;
    }

    RtlZeroMemory(AdapterObject, sizeof(ADAPTER_OBJECT));

    Status = ObInsertObject(AdapterObject,
                            NULL,
                            FILE_READ_DATA | FILE_WRITE_DATA,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HAL DMA: ObInsertObject failed %lx for %lu map registers\n",
                Status,
                NumberOfMapRegisters);
        return NULL;
    }

    ObReferenceObject(AdapterObject);

    ZwClose(Handle);

    AdapterObject->DmaHeader.Version = (USHORT)DeviceDescription->Version;
    AdapterObject->DmaHeader.Size = sizeof(ADAPTER_OBJECT);
    AdapterObject->DmaHeader.DmaOperations = &HalpDmaOperations;
    AdapterObject->MapRegistersPerChannel = 1;
    AdapterObject->Dma32BitAddresses = DeviceDescription->Dma32BitAddresses;
    AdapterObject->ChannelNumber = 0xFF;
    AdapterObject->MasterAdapter = MasterAdapter ? MasterAdapter : HalpLegacyMasterAdapter;
    AdapterObject->NumaNode = AdapterObject->MasterAdapter ? AdapterObject->MasterAdapter->NumaNode : 0;
    KeInitializeDeviceQueue(&AdapterObject->ChannelWaitQueue);

    return AdapterObject;
}
#endif

/**
 * @name HalpDmaInitializeEisaAdapter
 *
 * Setup DMA modes and extended modes for (E)ISA DMA adapter object.
 */
BOOLEAN
NTAPI
HalpDmaInitializeEisaAdapter(IN PADAPTER_OBJECT AdapterObject,
                             IN PDEVICE_DESCRIPTION DeviceDescription)
{
    UCHAR Controller;
    DMA_MODE DmaMode = {{0 }};
    DMA_EXTENDED_MODE ExtendedMode = {{ 0 }};
    PVOID AdapterBaseVa;

    Controller = (DeviceDescription->DmaChannel & 4) ? 2 : 1;

    if (Controller == 1)
    {
        AdapterBaseVa = UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaController1));
    }
    else
    {
        AdapterBaseVa = UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaController2));
    }

    AdapterObject->AdapterNumber = Controller;
    AdapterObject->ChannelNumber = (UCHAR)(DeviceDescription->DmaChannel & 3);
    AdapterObject->PagePort = (PUCHAR)HalpEisaPortPage[DeviceDescription->DmaChannel];
    AdapterObject->Width16Bits = FALSE;
    AdapterObject->AdapterBaseVa = AdapterBaseVa;

    if (HalpEisaDma)
    {
        ExtendedMode.ChannelNumber = AdapterObject->ChannelNumber;

        switch (DeviceDescription->DmaSpeed)
        {
            case Compatible: ExtendedMode.TimingMode = COMPATIBLE_TIMING; break;
            case TypeA: ExtendedMode.TimingMode = TYPE_A_TIMING; break;
            case TypeB: ExtendedMode.TimingMode = TYPE_B_TIMING; break;
            case TypeC: ExtendedMode.TimingMode = BURST_TIMING; break;
            default:
                return FALSE;
        }

        switch (DeviceDescription->DmaWidth)
        {
            case Width8Bits: ExtendedMode.TransferSize = B_8BITS; break;
            case Width16Bits: ExtendedMode.TransferSize = B_16BITS; break;
            case Width32Bits: ExtendedMode.TransferSize = B_32BITS; break;
            default:
                return FALSE;
        }

        if (Controller == 1)
        {
            WRITE_PORT_UCHAR(UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaExtendedMode1)),
                            ExtendedMode.Byte);
        }
        else
        {
            WRITE_PORT_UCHAR(UlongToPtr(FIELD_OFFSET(EISA_CONTROL, DmaExtendedMode2)),
                            ExtendedMode.Byte);
        }
    }
    else
    {
        /*
         * Validate setup for non-busmaster DMA adapter. Secondary controller
         * supports only 16-bit transfers and main controller supports only
         * 8-bit transfers. Anything else is invalid.
         */
        if (!DeviceDescription->Master)
        {
            if ((Controller == 2) && (DeviceDescription->DmaWidth == Width16Bits))
            {
                AdapterObject->Width16Bits = TRUE;
            }
            else if ((Controller != 1) || (DeviceDescription->DmaWidth != Width8Bits))
            {
                return FALSE;
            }
        }
    }

    DmaMode.Channel = AdapterObject->ChannelNumber;
    DmaMode.AutoInitialize = DeviceDescription->AutoInitialize;

    /*
     * Set the DMA request mode.
     *
     * For (E)ISA bus master devices just unmask (enable) the DMA channel
     * and set it to cascade mode. Otherwise just select the right one
     * bases on the passed device description.
     */
    if (DeviceDescription->Master)
    {
        DmaMode.RequestMode = CASCADE_REQUEST_MODE;
        if (Controller == 1)
        {
            /* Set the Request Data */
            _PRAGMA_WARNING_SUPPRESS(__WARNING_DEREF_NULL_PTR)
            WRITE_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)AdapterBaseVa + FIELD_OFFSET(DMA1_CONTROL, Mode)),
                DmaMode.Byte);

            /* Unmask DMA Channel */
            WRITE_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)AdapterBaseVa + FIELD_OFFSET(DMA1_CONTROL, SingleMask)),
                AdapterObject->ChannelNumber | DMA_CLEARMASK);
        }
        else
        {
            /* Set the Request Data */
            WRITE_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)AdapterBaseVa + FIELD_OFFSET(DMA2_CONTROL, Mode)),
                DmaMode.Byte);

            /* Unmask DMA Channel */
            WRITE_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)AdapterBaseVa + FIELD_OFFSET(DMA2_CONTROL, SingleMask)),
                AdapterObject->ChannelNumber | DMA_CLEARMASK);
        }
    }
    else
    {
        if (DeviceDescription->DemandMode)
        {
            DmaMode.RequestMode = DEMAND_REQUEST_MODE;
        }
        else
        {
            DmaMode.RequestMode = SINGLE_REQUEST_MODE;
        }
    }

    AdapterObject->AdapterMode = DmaMode;

    return TRUE;
}

#ifndef _MINIHAL_
/**
 * @name HalGetAdapter
 *
 * Allocate an adapter object for DMA device.
 *
 * @param DeviceDescription
 *        Structure describing the attributes of the device.
 * @param NumberOfMapRegisters
 *        On return filled with the maximum number of map registers the
 *        device driver can allocate for DMA transfer operations.
 *
 * @return The DMA adapter on success, NULL otherwise.
 *
 * @implemented
 */
PADAPTER_OBJECT
NTAPI
HalGetAdapter(IN PDEVICE_DESCRIPTION DeviceDescription,
              OUT PULONG NumberOfMapRegisters)
{
    PADAPTER_OBJECT AdapterObject = NULL;
    PADAPTER_OBJECT MasterAdapter;
    BOOLEAN EisaAdapter;
    ULONG MapRegisters;
    ULONG MaximumLength;
    ULONG MaximumLengthPages;
    KIRQL OldIrql;

    /* Validate parameters in device description */
    if (DeviceDescription->Version > DEVICE_DESCRIPTION_VERSION3) return NULL;

    /*
     * See if we're going to use ISA/EISA DMA adapter. These adapters are
     * special since they're reused.
     *
     * Also note that we check for channel number since there are only 8 DMA
     * channels on ISA, so any request above this requires new adapter.
     */
    if (((DeviceDescription->InterfaceType == Eisa) ||
         (DeviceDescription->InterfaceType == Isa)) || !(DeviceDescription->Master))
    {
        if (((DeviceDescription->InterfaceType == Isa) ||
             (DeviceDescription->InterfaceType == Eisa)) &&
            (DeviceDescription->DmaChannel >= 8))
        {
            EisaAdapter = FALSE;
        }
        else
        {
            EisaAdapter = TRUE;
        }
    }
    else
    {
        EisaAdapter = FALSE;
    }

    /*
     * Disallow creating adapter for ISA/EISA DMA channel 4 since it's used
     * for cascading the controllers and it's not available for software use.
     */
    if ((EisaAdapter) && (DeviceDescription->DmaChannel == 4)) return NULL;

    /*
     * Calculate the number of map registers.
     *
     * - For EISA and PCI scatter/gather no map registers are needed.
     * - For ISA slave scatter/gather one map register is needed.
     * - For all other cases the number of map registers depends on
     *   DeviceDescription->MaximumLength.
     */
    MaximumLength = DeviceDescription->MaximumLength & MAXLONG;
    MaximumLengthPages = BYTES_TO_PAGES(MaximumLength) + 1;
    if ((DeviceDescription->ScatterGather) &&
        ((DeviceDescription->InterfaceType == Eisa) ||
         (DeviceDescription->InterfaceType == PCIBus)))
    {
        /*
         * Historically we returned MapRegisters=0 for PCI/EISA scatter/gather
         * devices under the assumption that map registers are never needed.
         * That is not true for bus-master devices that cannot address the
         * full physical range (e.g. 32-bit DMA limits on x64).
         *
         * If the device is a PCI bus master with a 32-bit DMA ceiling, provide
         * map registers so IoMapTransfer can bounce buffers above the limit.
         */
        if ((DeviceDescription->InterfaceType == PCIBus) &&
            (DeviceDescription->Master) &&
            (DeviceDescription->Dma32BitAddresses) &&
            !(DeviceDescription->Dma64BitAddresses))
        {
            MapRegisters = HalpDmaDeriveMapRegisterCount(DeviceDescription,
                                                         MaximumLengthPages);
            if ((HalpMaxMapRegisters != 0) && (MapRegisters > HalpMaxMapRegisters))
            {
                MapRegisters = HalpMaxMapRegisters;
            }
            if (MapRegisters == 0)
            {
                MapRegisters = 1;
            }
        }
        else
        {
            MapRegisters = 0;
        }
    }
    else if ((DeviceDescription->ScatterGather) && !(DeviceDescription->Master))
    {
        MapRegisters = 1;
    }
    else
    {
        MapRegisters = HalpDmaDeriveMapRegisterCount(DeviceDescription,
                                                     MaximumLengthPages);
    }

    MasterAdapter = EisaAdapter ? HalpLegacyMasterAdapter
                                : HalpDmaSelectMasterAdapter(DeviceDescription);
    if (!MasterAdapter)
    {
        return NULL;
    }

    /*
     * Acquire the DMA lock that is used to protect the EISA adapter array.
     */
    KeWaitForSingleObject(&HalpDmaLock, Executive, KernelMode, FALSE, NULL);

    /*
     * Now we must get ahold of the adapter object. For first eight ISA/EISA
     * channels there are static adapter objects that are reused and updated
     * on succesive HalGetAdapter calls. In other cases a new adapter object
     * is always created and it's to the DMA adapter list (HalpDmaAdapterList).
     */
    if (EisaAdapter)
    {
        AdapterObject = HalpEisaAdapter[DeviceDescription->DmaChannel];
        if (AdapterObject)
        {
            AdapterObject->MasterAdapter = MasterAdapter;
            AdapterObject->NumaNode = MasterAdapter->NumaNode;
            if ((AdapterObject->NeedsMapRegisters) &&
                (MapRegisters > AdapterObject->MapRegistersPerChannel))
            {
                AdapterObject->MapRegistersPerChannel = MapRegisters;
            }
        }
    }

    if (AdapterObject == NULL)
    {
        AdapterObject = HalpDmaAllocateChildAdapter(MapRegisters,
                                                    DeviceDescription,
                                                    MasterAdapter);
        if (AdapterObject == NULL)
        {
            KeSetEvent(&HalpDmaLock, 0, 0);
            return NULL;
        }

        if (EisaAdapter)
        {
            HalpEisaAdapter[DeviceDescription->DmaChannel] = AdapterObject;
        }

        if (MapRegisters > 0)
        {
            AdapterObject->NeedsMapRegisters = TRUE;
            AdapterObject->MapRegistersPerChannel = MapRegisters;
        }
        else
        {
            AdapterObject->NeedsMapRegisters = FALSE;
            AdapterObject->MapRegistersPerChannel = DeviceDescription->Master ?
                MaximumLengthPages : 1;
        }
    }

    /*
     * Release the DMA lock. HalpEisaAdapter will no longer be touched,
     * so we don't need it.
     */
    KeSetEvent(&HalpDmaLock, 0, 0);

    if (!EisaAdapter)
    {
        /* If it's not one of the static adapters, add it to the list */
        KeAcquireSpinLock(&HalpDmaAdapterListLock, &OldIrql);
        InsertTailList(&HalpDmaAdapterList, &AdapterObject->AdapterList);
        KeReleaseSpinLock(&HalpDmaAdapterListLock, OldIrql);
    }

    /*
     * Setup the values in the adapter object that are common for all
     * types of buses.
     */
    if (DeviceDescription->Version >= DEVICE_DESCRIPTION_VERSION1)
    {
        AdapterObject->IgnoreCount = DeviceDescription->IgnoreCount;
    }
    else
    {
        AdapterObject->IgnoreCount = 0;
    }

    AdapterObject->Dma32BitAddresses = DeviceDescription->Dma32BitAddresses;
    AdapterObject->Dma64BitAddresses = DeviceDescription->Dma64BitAddresses;
    AdapterObject->ScatterGather = DeviceDescription->ScatterGather;
    AdapterObject->MasterDevice = DeviceDescription->Master;
    *NumberOfMapRegisters = AdapterObject->MapRegistersPerChannel;

    /*
     * For non-(E)ISA adapters we have already done all the work. On the
     * other hand for (E)ISA adapters we must still setup the DMA modes
     * and prepare the controller.
     */
    if (EisaAdapter)
    {
        if (!HalpDmaInitializeEisaAdapter(AdapterObject, DeviceDescription))
        {
            ObDereferenceObject(AdapterObject);
            return NULL;
        }
    }

    return AdapterObject;
}

/**
 * @name HalpGetDmaAdapter
 *
 * Internal routine to allocate PnP DMA adapter object. It's exported through
 * HalDispatchTable and used by IoGetDmaAdapter.
 *
 * @see HalGetAdapter
 */
PDMA_ADAPTER
NTAPI
HalpGetDmaAdapter(IN PVOID Context,
                  IN PDEVICE_DESCRIPTION DeviceDescription,
                  OUT PULONG NumberOfMapRegisters)
{
    return &HalGetAdapter(DeviceDescription, NumberOfMapRegisters)->DmaHeader;
}

/**
 * @name HalPutDmaAdapter
 *
 * Internal routine to free DMA adapter and resources for reuse. It's exported
 * using the DMA_OPERATIONS interface by HalGetAdapter.
 *
 * @see HalGetAdapter
 */
VOID
NTAPI
HalPutDmaAdapter(IN PADAPTER_OBJECT AdapterObject)
{
    KIRQL OldIrql;
    if (AdapterObject->ChannelNumber == 0xFF)
    {
        KeAcquireSpinLock(&HalpDmaAdapterListLock, &OldIrql);
        RemoveEntryList(&AdapterObject->AdapterList);
        KeReleaseSpinLock(&HalpDmaAdapterListLock, OldIrql);
    }

    ObDereferenceObject(AdapterObject);
}

/**
 * @name HalAllocateCommonBuffer
 *
 * Allocates memory that is visible to both the processor(s) and the DMA
 * device.
 *
 * @param AdapterObject
 *        Adapter object representing the bus master or system dma controller.
 * @param Length
 *        Number of bytes to allocate.
 * @param LogicalAddress
 *        Logical address the driver can use to access the buffer.
 * @param CacheEnabled
 *        Specifies if the memory can be cached.
 *
 * @return The base virtual address of the memory allocated or NULL on failure.
 *
 * @remarks
 *    On real NT x86 systems the CacheEnabled parameter is ignored, we honour
 *    it. If it proves to cause problems change it.
 *
 * @see HalFreeCommonBuffer
 *
 * @implemented
 */
PVOID
NTAPI
HalAllocateCommonBuffer(IN PADAPTER_OBJECT AdapterObject,
                        IN ULONG Length,
                        IN PPHYSICAL_ADDRESS LogicalAddress,
                        IN BOOLEAN CacheEnabled)
{
    PHYSICAL_ADDRESS LowestAcceptableAddress;
    PHYSICAL_ADDRESS HighestAcceptableAddress;
    PHYSICAL_ADDRESS BoundryAddressMultiple;
    PVOID VirtualAddress;

    LowestAcceptableAddress.QuadPart = 0;
    HighestAcceptableAddress = HalpGetAdapterMaximumPhysicalAddress(AdapterObject);
    BoundryAddressMultiple.QuadPart = 0;

    /*
     * For bus-master DMA devices the buffer mustn't cross 4Gb boundary. For
     * slave DMA devices the 64Kb boundary mustn't be crossed since the
     * controller wouldn't be able to handle it.
     */
    if (AdapterObject->MasterDevice)
    {
        if (AdapterObject->Dma64BitAddresses)
        {
            BoundryAddressMultiple.QuadPart = 0;
        }
        else
        {
            BoundryAddressMultiple.QuadPart = 0x100000000ULL;
        }
    }
    else
    {
        BoundryAddressMultiple.QuadPart = 0x10000;
    }

    VirtualAddress = MmAllocateContiguousMemorySpecifyCache(Length,
                                                            LowestAcceptableAddress,
                                                            HighestAcceptableAddress,
                                                            BoundryAddressMultiple,
                                                            CacheEnabled ? MmCached :
                                                            MmNonCached);
    if (VirtualAddress == NULL) return NULL;

    *LogicalAddress = MmGetPhysicalAddress(VirtualAddress);

    return VirtualAddress;
}

/**
 * @name HalFreeCommonBuffer
 *
 * Free common buffer allocated with HalAllocateCommonBuffer.
 *
 * @see HalAllocateCommonBuffer
 *
 * @implemented
 */
VOID
NTAPI
HalFreeCommonBuffer(IN PADAPTER_OBJECT AdapterObject,
                    IN ULONG Length,
                    IN PHYSICAL_ADDRESS LogicalAddress,
                    IN PVOID VirtualAddress,
                    IN BOOLEAN CacheEnabled)
{
    MmFreeContiguousMemorySpecifyCache(VirtualAddress,
                                       Length,
                                       CacheEnabled ? MmCached : MmNonCached);
}

typedef struct _SCATTER_GATHER_CONTEXT {
    BOOLEAN CallerSuppliedList;
    PADAPTER_OBJECT AdapterObject;
    PMDL Mdl;
    PUCHAR CurrentVa;
    ULONG Length;
    PDRIVER_LIST_CONTROL AdapterListControlRoutine;
    PVOID AdapterListControlContext;
    PVOID MapRegisterBase;
    ULONG MapRegisterCount;
    BOOLEAN WriteToDevice;
    PSCATTER_GATHER_LIST ScatterGatherList;
    WAIT_CONTEXT_BLOCK Wcb;
} SCATTER_GATHER_CONTEXT, *PSCATTER_GATHER_CONTEXT;


IO_ALLOCATION_ACTION
NTAPI
HalpScatterGatherAdapterControl(IN PDEVICE_OBJECT DeviceObject,
                                IN PIRP Irp,
								IN PVOID MapRegisterBase,
								IN PVOID Context)
{
	PSCATTER_GATHER_CONTEXT AdapterControlContext = Context;
	PADAPTER_OBJECT AdapterObject = AdapterControlContext->AdapterObject;
	PSCATTER_GATHER_LIST ScatterGatherList;
	SCATTER_GATHER_ELEMENT StackElements[HALP_SG_STACK_ELEMENTS];
	PSCATTER_GATHER_ELEMENT TempElements = StackElements;
	ULONG ElementCapacity = AdapterControlContext->MapRegisterCount;
	ULONG ElementCount = 0, RemainingLength = AdapterControlContext->Length;
	PUCHAR CurrentVa = AdapterControlContext->CurrentVa;
	BOOLEAN AllocatedTemp = FALSE;

	/* Store the map register base for later in HalPutScatterGatherList */
	AdapterControlContext->MapRegisterBase = MapRegisterBase;

    if (ElementCapacity == 0)
    {
        ExFreePoolWithTag(AdapterControlContext, TAG_DMA);
        return DeallocateObject;
    }

    if (ElementCapacity > HALP_SG_STACK_ELEMENTS)
    {
        SIZE_T TempBytes = ElementCapacity * sizeof(*TempElements);
        TempElements = ExAllocatePoolWithTag(NonPagedPool, TempBytes, TAG_DMA);
        if (!TempElements)
        {
            ExFreePoolWithTag(AdapterControlContext, TAG_DMA);
            return DeallocateObject;
        }
        AllocatedTemp = TRUE;
    }

	while (RemainingLength > 0 && ElementCount < ElementCapacity)
	{
	    TempElements[ElementCount].Length = RemainingLength;
		TempElements[ElementCount].Reserved = 0;
	    TempElements[ElementCount].Address = IoMapTransfer(AdapterObject,
		                                                   AdapterControlContext->Mdl,
														   MapRegisterBase,
														   CurrentVa + (AdapterControlContext->Length - RemainingLength),
														   &TempElements[ElementCount].Length,
														   AdapterControlContext->WriteToDevice);
		if (TempElements[ElementCount].Length == 0)
			break;

		DPRINT("Allocated one S/G element: 0x%I64u with length: 0x%x\n",
		        TempElements[ElementCount].Address.QuadPart,
				TempElements[ElementCount].Length);

		ASSERT(TempElements[ElementCount].Length <= RemainingLength);
		RemainingLength -= TempElements[ElementCount].Length;
		ElementCount++;
	}

	if (RemainingLength > 0)
	{
		DPRINT1("Scatter/gather list construction failed!\n");
        if (AllocatedTemp)
        {
            ExFreePoolWithTag(TempElements, TAG_DMA);
        }
        ExFreePoolWithTag(AdapterControlContext, TAG_DMA);
		return DeallocateObject;
	}

    ScatterGatherList = AdapterControlContext->ScatterGatherList;
    if (!ScatterGatherList)
    {
        DPRINT1("Scatter/gather list buffer missing\n");
        if (AllocatedTemp)
        {
            ExFreePoolWithTag(TempElements, TAG_DMA);
        }
        ExFreePoolWithTag(AdapterControlContext, TAG_DMA);
        return DeallocateObject;
    }

	ScatterGatherList->NumberOfElements = ElementCount;
	ScatterGatherList->Reserved = (ULONG_PTR)AdapterControlContext;
	RtlCopyMemory(ScatterGatherList->Elements,
	              TempElements,
				  sizeof(SCATTER_GATHER_ELEMENT) * ElementCount);

    if (AllocatedTemp)
    {
        ExFreePoolWithTag(TempElements, TAG_DMA);
    }

	DPRINT("Initiating S/G DMA with %d element(s)\n", ElementCount);

	AdapterControlContext->AdapterListControlRoutine(DeviceObject,
	                                                 Irp,
													 ScatterGatherList,
													 AdapterControlContext->AdapterListControlContext);

	return DeallocateObjectKeepRegisters;
}

/**
 * @name HalGetScatterGatherList
 *
 * Creates a scatter-gather list to be using in scatter/gather DMA
 *
 * @param AdapterObject
 *        Adapter object representing the bus master or system dma controller.
 * @param DeviceObject
 *        The device target for DMA.
 * @param Mdl
 *        The MDL that describes the buffer to be mapped.
 * @param CurrentVa
 *        The current VA in the buffer to be mapped for transfer.
 * @param Length
 *        Specifies the length of data in bytes to be mapped.
 * @param ExecutionRoutine
 *        A caller supplied AdapterListControl routine to be called when DMA is available.
 * @param Context
 *        Context passed to the AdapterListControl routine.
 * @param WriteToDevice
 *        Indicates direction of DMA operation.
 *
 * @return The status of the operation.
 *
 * @see HalBuildScatterGatherList
 *
 * @implemented
 */
 NTSTATUS
 NTAPI
 HalGetScatterGatherList(IN PADAPTER_OBJECT AdapterObject,
                         IN PDEVICE_OBJECT DeviceObject,
                         IN PMDL Mdl,
                         IN PVOID CurrentVa,
                         IN ULONG Length,
                         IN PDRIVER_LIST_CONTROL ExecutionRoutine,
                         IN PVOID Context,
                         IN BOOLEAN WriteToDevice)
{
    return HalBuildScatterGatherList(AdapterObject,
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

/**
 * @name HalPutScatterGatherList
 *
 * Frees a scatter-gather list allocated from HalBuildScatterGatherList
 *
 * @param AdapterObject
 *        Adapter object representing the bus master or system dma controller.
 * @param ScatterGather
 *        The scatter/gather list to be freed.
 * @param WriteToDevice
 *        Indicates direction of DMA operation.
 *
 * @return None
 *
 * @see HalBuildScatterGatherList
 *
 * @implemented
 */
 VOID
 NTAPI
 HalPutScatterGatherList(IN PADAPTER_OBJECT AdapterObject,
                         IN PSCATTER_GATHER_LIST ScatterGather,
						 IN BOOLEAN WriteToDevice)
{
    PSCATTER_GATHER_CONTEXT AdapterControlContext = (PSCATTER_GATHER_CONTEXT)ScatterGather->Reserved;
	ULONG i;

	for (i = 0; i < ScatterGather->NumberOfElements; i++)
	{
	     IoFlushAdapterBuffers(AdapterObject,
		                       AdapterControlContext->Mdl,
							   AdapterControlContext->MapRegisterBase,
							   AdapterControlContext->CurrentVa,
							   ScatterGather->Elements[i].Length,
							   AdapterControlContext->WriteToDevice);
		 AdapterControlContext->CurrentVa += ScatterGather->Elements[i].Length;
	}

	IoFreeMapRegisters(AdapterObject,
	                   AdapterControlContext->MapRegisterBase,
					   AdapterControlContext->MapRegisterCount);


    ExFreePoolWithTag(AdapterControlContext, TAG_DMA);

    DPRINT("S/G DMA has finished!\n");
}

NTSTATUS
NTAPI
HalCalculateScatterGatherListSize(
    IN PADAPTER_OBJECT AdapterObject,
    IN PMDL Mdl OPTIONAL,
    IN PVOID CurrentVa,
    IN ULONG Length,
    OUT PULONG ScatterGatherListSize,
    OUT PULONG pNumberOfMapRegisters)
{
    ULONG NumberOfMapRegisters;
    ULONG SgSize;
    PUCHAR BaseVa;

    UNREFERENCED_PARAMETER(AdapterObject);

    if (!Length)
    {
        if (!Mdl)
        {
            return STATUS_INVALID_PARAMETER;
        }

        Length = MmGetMdlByteCount(Mdl);
    }

    if (Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (CurrentVa)
    {
        BaseVa = CurrentVa;
    }
    else if (Mdl)
    {
        BaseVa = MmGetMdlVirtualAddress(Mdl);
    }
    else
    {
        return STATUS_INVALID_PARAMETER;
    }

    NumberOfMapRegisters = ADDRESS_AND_SIZE_TO_SPAN_PAGES(BaseVa, Length);
    if (NumberOfMapRegisters == 0)
    {
        NumberOfMapRegisters = 1;
    }

    SgSize = FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) +
             (NumberOfMapRegisters * sizeof(SCATTER_GATHER_ELEMENT));

    *ScatterGatherListSize = SgSize;
    if (pNumberOfMapRegisters)
    {
        *pNumberOfMapRegisters = NumberOfMapRegisters;
    }

    return STATUS_SUCCESS;
}

/**
 * @name HalBuildScatterGatherList
 *
 * Creates a scatter-gather list to be using in scatter/gather DMA
 *
 * @param AdapterObject
 *        Adapter object representing the bus master or system dma controller.
 * @param DeviceObject
 *        The device target for DMA.
 * @param Mdl
 *        The MDL that describes the buffer to be mapped.
 * @param CurrentVa
 *        The current VA in the buffer to be mapped for transfer.
 * @param Length
 *        Specifies the length of data in bytes to be mapped.
 * @param ExecutionRoutine
 *        A caller supplied AdapterListControl routine to be called when DMA is available.
 * @param Context
 *        Context passed to the AdapterListControl routine.
 * @param WriteToDevice
 *        Indicates direction of DMA operation.
 *
 * @param ScatterGatherBuffer
 *        User buffer for the scatter-gather list
 *
 * @param ScatterGatherBufferLength
 *        Buffer length
 *
 * @return The status of the operation.
 *
 * @see HalPutScatterGatherList
 *
 * @implemented
 */
NTSTATUS
NTAPI
HalBuildScatterGatherList(
    IN PADAPTER_OBJECT AdapterObject,
    IN PDEVICE_OBJECT DeviceObject,
    IN PMDL Mdl,
    IN PVOID CurrentVa,
    IN ULONG Length,
    IN PDRIVER_LIST_CONTROL ExecutionRoutine,
    IN PVOID Context,
    IN BOOLEAN WriteToDevice,
    IN PVOID ScatterGatherBuffer,
    IN ULONG ScatterGatherBufferLength)
{
    NTSTATUS Status;
    ULONG SgSize, NumberOfMapRegisters;
    PSCATTER_GATHER_CONTEXT ScatterGatherContext;
    PSCATTER_GATHER_LIST ScatterGatherList;
    PVOID AllocationBlock = NULL;
    BOOLEAN CallerBufferProvided;
    PUCHAR BaseVa;

    Status = HalCalculateScatterGatherListSize(AdapterObject,
                                               Mdl,
                                               CurrentVa,
                                               Length,
                                               &SgSize,
                                               &NumberOfMapRegisters);
    if (!NT_SUCCESS(Status)) return Status;

    if (ScatterGatherBuffer)
    {
        /* Checking if user buffer is enough */
        if (ScatterGatherBufferLength < SgSize)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        ScatterGatherList = (PSCATTER_GATHER_LIST)ScatterGatherBuffer;
        CallerBufferProvided = TRUE;
        ScatterGatherContext = ExAllocatePoolWithTag(NonPagedPool,
                                                     sizeof(*ScatterGatherContext),
                                                     TAG_DMA);
        if (!ScatterGatherContext)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        AllocationBlock = ExAllocatePoolWithTag(NonPagedPool,
                                                sizeof(*ScatterGatherContext) + SgSize,
                                                TAG_DMA);
        if (!AllocationBlock)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        ScatterGatherContext = (PSCATTER_GATHER_CONTEXT)AllocationBlock;
        ScatterGatherList = (PSCATTER_GATHER_LIST)((PUCHAR)AllocationBlock + sizeof(*ScatterGatherContext));
        CallerBufferProvided = FALSE;
    }

    if (CurrentVa)
    {
        BaseVa = CurrentVa;
    }
    else
    {
        BaseVa = (PUCHAR)MmGetMdlVirtualAddress(Mdl);
    }

    /* Fill the scatter-gather context */
    ScatterGatherContext->CallerSuppliedList = CallerBufferProvided;
    ScatterGatherContext->AdapterObject = AdapterObject;
    ScatterGatherContext->Mdl = Mdl;
    ScatterGatherContext->CurrentVa = BaseVa;
    ScatterGatherContext->Length = Length;
    ScatterGatherContext->MapRegisterCount = NumberOfMapRegisters;
    ScatterGatherContext->AdapterListControlRoutine = ExecutionRoutine;
    ScatterGatherContext->AdapterListControlContext = Context;
    ScatterGatherContext->WriteToDevice = WriteToDevice;
    ScatterGatherContext->ScatterGatherList = ScatterGatherList;

    ScatterGatherContext->Wcb.DeviceObject = DeviceObject;
    ScatterGatherContext->Wcb.DeviceContext = (PVOID)ScatterGatherContext;
    ScatterGatherContext->Wcb.CurrentIrp = DeviceObject->CurrentIrp;

    Status = HalAllocateAdapterChannel(AdapterObject,
                                       &ScatterGatherContext->Wcb,
                                       NumberOfMapRegisters,
                                       HalpScatterGatherAdapterControl);

    if (!NT_SUCCESS(Status))
    {
        if (!CallerBufferProvided)
        {
            ExFreePoolWithTag(AllocationBlock, TAG_DMA);
        }
        else
        {
            ExFreePoolWithTag(ScatterGatherContext, TAG_DMA);
        }
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalBuildMdlFromScatterGatherList(
    IN PDMA_ADAPTER DmaAdapter,
    IN PSCATTER_GATHER_LIST ScatterGather,
    IN PMDL OriginalMdl,
    OUT PMDL *TargetMdl)
{
    PFN_COUNT TotalPages = 0;
    ULONG mdlSize;
    PMDL NewMdl;
    PFN_NUMBER *PfnArray;
    ULONGLONG TotalLength = 0;
    ULONGLONG FirstAddress;
    ULONG Index, PfnIndex = 0;

    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(OriginalMdl);

    if (!ScatterGather || !TargetMdl)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ScatterGather->NumberOfElements == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (Index = 0; Index < ScatterGather->NumberOfElements; ++Index)
    {
        PHYSICAL_ADDRESS SegmentAddress = ScatterGather->Elements[Index].Address;
        ULONG SegmentLength = ScatterGather->Elements[Index].Length;
        ULONGLONG CurrentAddress = SegmentAddress.QuadPart;

        if (SegmentLength == 0)
        {
            continue;
        }

        TotalLength += SegmentLength;
        TotalPages += ADDRESS_AND_SIZE_TO_SPAN_PAGES((PVOID)(ULONG_PTR)CurrentAddress,
                                                    SegmentLength);
    }

    if (TotalPages == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (TotalLength > MAXULONG)
    {
        return STATUS_INVALID_PARAMETER;
    }

    mdlSize = sizeof(MDL) + (TotalPages * sizeof(PFN_NUMBER));
    NewMdl = ExAllocatePoolWithTag(NonPagedPool, mdlSize, TAG_DMA);
    if (!NewMdl)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NewMdl, mdlSize);
    NewMdl->Size = (USHORT)mdlSize;
    NewMdl->MdlFlags = MDL_PAGES_LOCKED | MDL_SOURCE_IS_NONPAGED_POOL;
    NewMdl->ByteCount = (ULONG)TotalLength;

    FirstAddress = ScatterGather->Elements[0].Address.QuadPart;
    NewMdl->ByteOffset = (CSHORT)(FirstAddress & (PAGE_SIZE - 1));
    NewMdl->StartVa = (PVOID)(ULONG_PTR)(FirstAddress & ~(ULONGLONG)(PAGE_SIZE - 1));

    PfnArray = MmGetMdlPfnArray(NewMdl);

    for (Index = 0; Index < ScatterGather->NumberOfElements; ++Index)
    {
        PHYSICAL_ADDRESS SegmentAddress = ScatterGather->Elements[Index].Address;
        ULONG SegmentLength = ScatterGather->Elements[Index].Length;
        ULONGLONG CurrentAddress = SegmentAddress.QuadPart;

        while (SegmentLength)
        {
            ULONGLONG PageBase = CurrentAddress & ~(ULONGLONG)(PAGE_SIZE - 1);
            ULONG BytesInPage = (ULONG)(PAGE_SIZE - (CurrentAddress - PageBase));

            if (BytesInPage > SegmentLength)
            {
                BytesInPage = SegmentLength;
            }

            PfnArray[PfnIndex++] = (PFN_NUMBER)(PageBase >> PAGE_SHIFT);

            CurrentAddress += BytesInPage;
            SegmentLength -= BytesInPage;
        }
    }

    ASSERT(PfnIndex == TotalPages);

    *TargetMdl = NewMdl;
    return STATUS_SUCCESS;
}
#endif

/**
 * @name HalpDmaGetDmaAlignment
 *
 * Internal routine to return the DMA alignment requirement. It's exported
 * using the DMA_OPERATIONS interface by HalGetAdapter.
 *
 * @see HalGetAdapter
 */
ULONG
NTAPI
HalpDmaGetDmaAlignment(IN PADAPTER_OBJECT AdapterObject)
{
    return 1;
}

/*
 * @name HalReadDmaCounter
 *
 * Read DMA operation progress counter.
 *
 * @implemented
 */
ULONG
NTAPI
HalReadDmaCounter(IN PADAPTER_OBJECT AdapterObject)
{
    KIRQL OldIrql;
    ULONG Count, OldCount;

    ASSERT(!AdapterObject->MasterDevice);

    /*
     * Acquire the master adapter lock since we're going to mess with the
     * system DMA controller registers and we really don't want anyone
     * to do the same at the same time.
     */
    KeAcquireSpinLock(&AdapterObject->MasterAdapter->SpinLock, &OldIrql);

    /* Send the request to the specific controller. */
    if (AdapterObject->AdapterNumber == 1)
    {
        PVOID Base = AdapterObject->AdapterBaseVa;

        Count = 0xffff00;
        do
        {
            OldCount = Count;

            /* Send Reset */
            WRITE_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, ClearBytePointer)), 0);

            /* Read Count */
            Count = READ_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, DmaAddressCount) +
                         sizeof(DMA1_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                         FIELD_OFFSET(DMA1_ADDRESS_COUNT, DmaBaseCount)));
            Count |= READ_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, DmaAddressCount) +
                         sizeof(DMA1_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                         FIELD_OFFSET(DMA1_ADDRESS_COUNT, DmaBaseCount))) << 8;
        } while (0xffff00 & (OldCount ^ Count));
    }
    else
    {
        PVOID Base = AdapterObject->AdapterBaseVa;

        Count = 0xffff00;
        do
        {
            OldCount = Count;

            /* Send Reset */
            WRITE_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, ClearBytePointer)), 0);

            /* Read Count */
            Count = READ_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, DmaAddressCount) +
                         sizeof(DMA2_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                         FIELD_OFFSET(DMA2_ADDRESS_COUNT, DmaBaseCount)));
            Count |= READ_PORT_UCHAR(
                (PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, DmaAddressCount) +
                         sizeof(DMA2_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                         FIELD_OFFSET(DMA2_ADDRESS_COUNT, DmaBaseCount))) << 8;
        } while (0xffff00 & (OldCount ^ Count));
    }

    KeReleaseSpinLock(&AdapterObject->MasterAdapter->SpinLock, OldIrql);

    Count++;
    Count &= 0xffff;
    if (AdapterObject->Width16Bits) Count *= 2;

    return Count;
}

#ifndef _MINIHAL_
/**
 * @name HalpGrowMapBufferWorker
 *
 * Helper routine of HalAllocateAdapterChannel for allocating map registers
 * at PASSIVE_LEVEL in work item.
 */
VOID
NTAPI
HalpGrowMapBufferWorker(IN PVOID DeferredContext)
{
    PGROW_WORK_ITEM WorkItem = (PGROW_WORK_ITEM)DeferredContext;
    KIRQL OldIrql;
    BOOLEAN Succeeded;

    /*
     * Try to allocate new map registers for the adapter.
     *
     * NOTE: The NT implementation actually tries to allocate more map
     * registers than needed as an optimization.
     */
    KeWaitForSingleObject(&HalpDmaLock, Executive, KernelMode, FALSE, NULL);
    Succeeded = HalpGrowMapBuffers(WorkItem->AdapterObject->MasterAdapter,
                                   WorkItem->NumberOfMapRegisters << PAGE_SHIFT);
    KeSetEvent(&HalpDmaLock, 0, 0);

    if (Succeeded)
    {
        /*
         * Flush the adapter queue now that new map registers are ready. The
         * easiest way to do that is to call IoFreeMapRegisters to not free
         * any registers. Note that we use the magic (PVOID)2 map register
         * base to bypass the parameter checking.
         */
        OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
        IoFreeMapRegisters(WorkItem->AdapterObject, (PVOID)2, 0);
        KfLowerIrql(OldIrql);
    }

    ExFreePool(WorkItem);
}

/**
 * @name HalAllocateAdapterChannel
 *
 * Setup map registers for an adapter object.
 *
 * @param AdapterObject
 *        Pointer to an ADAPTER_OBJECT to set up.
 * @param WaitContextBlock
 *        Context block to be used with ExecutionRoutine.
 * @param NumberOfMapRegisters
 *        Number of map registers requested.
 * @param ExecutionRoutine
 *        Callback to call when map registers are allocated.
 *
 * @return
 *    If not enough map registers can be allocated then
 *    STATUS_INSUFFICIENT_RESOURCES is returned. If the function
 *    succeeds or the callback is queued for later delivering then
 *    STATUS_SUCCESS is returned.
 *
 * @see IoFreeAdapterChannel
 *
 * @implemented
 */
NTSTATUS
NTAPI
HalAllocateAdapterChannel(IN PADAPTER_OBJECT AdapterObject,
                          IN PWAIT_CONTEXT_BLOCK WaitContextBlock,
                          IN ULONG NumberOfMapRegisters,
                          IN PDRIVER_CONTROL ExecutionRoutine)
{
    PADAPTER_OBJECT MasterAdapter;
    PGROW_WORK_ITEM WorkItem;
    ULONG Index = MAXULONG;
    ULONG Result;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    /* Set up the wait context block in case we can't run right away. */
    WaitContextBlock->DeviceRoutine = ExecutionRoutine;
    WaitContextBlock->NumberOfMapRegisters = NumberOfMapRegisters;

    /* Returns true if queued, else returns false and sets the queue to busy */
    if (KeInsertDeviceQueue(&AdapterObject->ChannelWaitQueue,
                            &WaitContextBlock->WaitQueueEntry))
    {
        return STATUS_SUCCESS;
    }

    MasterAdapter = AdapterObject->MasterAdapter;

    AdapterObject->NumberOfMapRegisters = NumberOfMapRegisters;
    AdapterObject->CurrentWcb = WaitContextBlock;

    if ((NumberOfMapRegisters) && (AdapterObject->NeedsMapRegisters))
    {
        if (NumberOfMapRegisters > AdapterObject->MapRegistersPerChannel)
        {
            AdapterObject->NumberOfMapRegisters = 0;
            IoFreeAdapterChannel(AdapterObject);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /*
         * Get the map registers. This is partly complicated by the fact
         * that new map registers can only be allocated at PASSIVE_LEVEL
         * and we're currently at DISPATCH_LEVEL. The following code has
         * two code paths:
         *
         * - If there is no adapter queued for map register allocation,
         *   try to see if enough contiguous map registers are present.
         *   In case they're we can just get them and proceed further.
         *
         * - If some adapter is already present in the queue we must
         *   respect the order of adapters asking for map registers and
         *   so the fast case described above can't take place.
         *   This case is also entered if not enough coniguous map
         *   registers are present.
         *
         *   A work queue item is allocated and queued, the adapter is
         *   also queued into the master adapter queue. The worker
         *   routine does the job of allocating the map registers at
         *   PASSIVE_LEVEL and calling the ExecutionRoutine.
         */

        KeAcquireSpinLock(&MasterAdapter->SpinLock, &OldIrql);

        if (IsListEmpty(&MasterAdapter->AdapterQueue))
        {
            Index = RtlFindClearBitsAndSet(MasterAdapter->MapRegisters, NumberOfMapRegisters, 0);
            if (Index != MAXULONG)
            {
                AdapterObject->MapRegisterBase = MasterAdapter->MapRegisterBase + Index;
                if (!AdapterObject->ScatterGather)
                {
                    AdapterObject->MapRegisterBase = (PROS_MAP_REGISTER_ENTRY)((ULONG_PTR)AdapterObject->MapRegisterBase | MAP_BASE_SW_SG);
                }
            }
        }

        if (Index == MAXULONG)
        {
            InsertTailList(&MasterAdapter->AdapterQueue, &AdapterObject->AdapterQueue);

            WorkItem = ExAllocatePoolWithTag(NonPagedPool,
                                             sizeof(GROW_WORK_ITEM),
                                             TAG_DMA);
            if (WorkItem)
            {
                ExInitializeWorkItem(&WorkItem->WorkQueueItem, HalpGrowMapBufferWorker, WorkItem);
                WorkItem->AdapterObject = AdapterObject;
                WorkItem->NumberOfMapRegisters = NumberOfMapRegisters;

                ExQueueWorkItem(&WorkItem->WorkQueueItem, DelayedWorkQueue);
            }

            KeReleaseSpinLock(&MasterAdapter->SpinLock, OldIrql);

            return STATUS_SUCCESS;
        }

        KeReleaseSpinLock(&MasterAdapter->SpinLock, OldIrql);
    }
    else
    {
        AdapterObject->MapRegisterBase = NULL;
        AdapterObject->NumberOfMapRegisters = 0;
    }

    AdapterObject->CurrentWcb = WaitContextBlock;

    Result = ExecutionRoutine(WaitContextBlock->DeviceObject,
                              WaitContextBlock->CurrentIrp,
                              AdapterObject->MapRegisterBase,
                              WaitContextBlock->DeviceContext);

    /*
     * Possible return values:
     *
     * - KeepObject
     *   Don't free any resources, the ADAPTER_OBJECT is still in use and
     *   the caller will call IoFreeAdapterChannel later.
     *
     * - DeallocateObject
     *   Deallocate the map registers and release the ADAPTER_OBJECT, so
     *   someone else can use it.
     *
     * - DeallocateObjectKeepRegisters
     *   Release the ADAPTER_OBJECT, but hang on to the map registers. The
     *   client will later call IoFreeMapRegisters.
     *
     * NOTE:
     * IoFreeAdapterChannel runs the queue, so it must be called unless
     * the adapter object is not to be freed.
     */
    if (Result == DeallocateObject)
    {
        IoFreeAdapterChannel(AdapterObject);
    }
    else if (Result == DeallocateObjectKeepRegisters)
    {
        AdapterObject->NumberOfMapRegisters = 0;
        IoFreeAdapterChannel(AdapterObject);
    }

    return STATUS_SUCCESS;
}

/**
 * @name IoFreeAdapterChannel
 *
 * Free DMA resources allocated by IoAllocateAdapterChannel.
 *
 * @param AdapterObject
 *        Adapter object with resources to free.
 *
 * @remarks
 *    This function releases map registers registers assigned to the DMA
 *    adapter. After releasing the adapter, it checks the adapter's queue
 *    and runs each queued device object in series until the queue is
 *    empty. This is the only way the device queue is emptied.
 *
 * @see IoAllocateAdapterChannel
 *
 * @implemented
 */
VOID
NTAPI
IoFreeAdapterChannel(IN PADAPTER_OBJECT AdapterObject)
{
    PADAPTER_OBJECT MasterAdapter;
    PKDEVICE_QUEUE_ENTRY DeviceQueueEntry;
    PWAIT_CONTEXT_BLOCK WaitContextBlock;
    ULONG Index = MAXULONG;
    ULONG Result;
    KIRQL OldIrql;

    MasterAdapter = AdapterObject->MasterAdapter;

    for (;;)
    {
        /*
         * To keep map registers, call here with AdapterObject->
         * NumberOfMapRegisters set to zero. This trick is used in
         * HalAllocateAdapterChannel for example.
         */
        if (AdapterObject->NumberOfMapRegisters)
        {
            IoFreeMapRegisters(AdapterObject,
                               AdapterObject->MapRegisterBase,
                               AdapterObject->NumberOfMapRegisters);
        }

        DeviceQueueEntry = KeRemoveDeviceQueue(&AdapterObject->ChannelWaitQueue);
        if (!DeviceQueueEntry) break;

        WaitContextBlock = CONTAINING_RECORD(DeviceQueueEntry,
                                             WAIT_CONTEXT_BLOCK,
                                             WaitQueueEntry);

        AdapterObject->CurrentWcb = WaitContextBlock;
        AdapterObject->NumberOfMapRegisters = WaitContextBlock->NumberOfMapRegisters;

        if ((WaitContextBlock->NumberOfMapRegisters) && (AdapterObject->MasterAdapter))
        {
            KeAcquireSpinLock(&MasterAdapter->SpinLock, &OldIrql);

            if (IsListEmpty(&MasterAdapter->AdapterQueue))
            {
                Index = RtlFindClearBitsAndSet(MasterAdapter->MapRegisters,
                                               WaitContextBlock->NumberOfMapRegisters,
                                               0);
                if (Index != MAXULONG)
                {
                    AdapterObject->MapRegisterBase = MasterAdapter->MapRegisterBase + Index;
                    if (!AdapterObject->ScatterGather)
                    {
                        AdapterObject->MapRegisterBase =(PROS_MAP_REGISTER_ENTRY)((ULONG_PTR)AdapterObject->MapRegisterBase | MAP_BASE_SW_SG);
                    }
                }
            }

            if (Index == MAXULONG)
            {
                InsertTailList(&MasterAdapter->AdapterQueue, &AdapterObject->AdapterQueue);
                KeReleaseSpinLock(&MasterAdapter->SpinLock, OldIrql);
                break;
            }

            KeReleaseSpinLock(&MasterAdapter->SpinLock, OldIrql);
        }
        else
        {
            AdapterObject->MapRegisterBase = NULL;
            AdapterObject->NumberOfMapRegisters = 0;
        }

        /* Call the adapter control routine. */
        Result = ((PDRIVER_CONTROL)WaitContextBlock->DeviceRoutine)(WaitContextBlock->DeviceObject,
                                                                    WaitContextBlock->CurrentIrp,
                                                                    AdapterObject->MapRegisterBase,
                                                                    WaitContextBlock->DeviceContext);
        switch (Result)
        {
            case KeepObject:
                /*
                 * We're done until the caller manually calls IoFreeAdapterChannel
                 * or IoFreeMapRegisters.
                 */
                return;

            case DeallocateObjectKeepRegisters:
                /*
                 * Hide the map registers so they aren't deallocated next time
                 * around.
                 */
                AdapterObject->NumberOfMapRegisters = 0;
                break;

            default:
                break;
        }
    }
}

/**
 * @name IoFreeMapRegisters
 *
 * Free map registers reserved by the system for a DMA.
 *
 * @param AdapterObject
 *        DMA adapter to free map registers on.
 * @param MapRegisterBase
 *        Handle to map registers to free.
 * @param NumberOfRegisters
 *        Number of map registers to be freed.
 *
 * @implemented
 */
VOID
NTAPI
IoFreeMapRegisters(IN PADAPTER_OBJECT AdapterObject,
                   IN PVOID MapRegisterBase,
                   IN ULONG NumberOfMapRegisters)
{
    PADAPTER_OBJECT MasterAdapter = AdapterObject->MasterAdapter;
    PLIST_ENTRY ListEntry;
    KIRQL OldIrql;
    ULONG Index;
    ULONG Result;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    if (!(MasterAdapter) || !(MapRegisterBase)) return;

    KeAcquireSpinLock(&MasterAdapter->SpinLock, &OldIrql);

    if (NumberOfMapRegisters != 0)
    {
        PROS_MAP_REGISTER_ENTRY RealMapRegisterBase;

        RealMapRegisterBase = (PROS_MAP_REGISTER_ENTRY)((ULONG_PTR)MapRegisterBase & ~MAP_BASE_SW_SG);
        RtlClearBits(MasterAdapter->MapRegisters,
                     (ULONG)(RealMapRegisterBase - MasterAdapter->MapRegisterBase),
                     NumberOfMapRegisters);
    }

    /*
     * Now that we freed few map registers it's time to look at the master
     * adapter queue and see if there is someone waiting for map registers.
     */
    while (!IsListEmpty(&MasterAdapter->AdapterQueue))
    {
        ListEntry = RemoveHeadList(&MasterAdapter->AdapterQueue);
        AdapterObject = CONTAINING_RECORD(ListEntry, struct _ADAPTER_OBJECT, AdapterQueue);

        Index = RtlFindClearBitsAndSet(MasterAdapter->MapRegisters,
                                       AdapterObject->NumberOfMapRegisters,
                                       0);
        if (Index == MAXULONG)
        {
            InsertHeadList(&MasterAdapter->AdapterQueue, ListEntry);
            break;
        }

        KeReleaseSpinLock(&MasterAdapter->SpinLock, OldIrql);

        AdapterObject->MapRegisterBase = MasterAdapter->MapRegisterBase + Index;
        if (!AdapterObject->ScatterGather)
        {
            AdapterObject->MapRegisterBase =
                (PROS_MAP_REGISTER_ENTRY)((ULONG_PTR)AdapterObject->MapRegisterBase | MAP_BASE_SW_SG);
        }

        Result = ((PDRIVER_CONTROL)AdapterObject->CurrentWcb->DeviceRoutine)(AdapterObject->CurrentWcb->DeviceObject,
                                                                             AdapterObject->CurrentWcb->CurrentIrp,
                                                                             AdapterObject->MapRegisterBase,
                                                                             AdapterObject->CurrentWcb->DeviceContext);
        switch (Result)
        {
            case DeallocateObjectKeepRegisters:
                AdapterObject->NumberOfMapRegisters = 0;
                /* fall through */

            case DeallocateObject:
                if (AdapterObject->NumberOfMapRegisters)
                {
                    KeAcquireSpinLock(&MasterAdapter->SpinLock, &OldIrql);
                    RtlClearBits(MasterAdapter->MapRegisters,
                                 (ULONG)(AdapterObject->MapRegisterBase -
                                         MasterAdapter->MapRegisterBase),
                                 AdapterObject->NumberOfMapRegisters);
                    KeReleaseSpinLock(&MasterAdapter->SpinLock, OldIrql);
                }

                IoFreeAdapterChannel(AdapterObject);
                break;

            default:
                break;
        }

        KeAcquireSpinLock(&MasterAdapter->SpinLock, &OldIrql);
    }

    KeReleaseSpinLock(&MasterAdapter->SpinLock, OldIrql);
}

/**
 * @name HalpCopyBufferMap
 *
 * Helper function for copying data from/to map register buffers.
 *
 * @see IoFlushAdapterBuffers, IoMapTransfer
 */
VOID
NTAPI
HalpCopyBufferMap(IN PMDL Mdl,
                  IN PROS_MAP_REGISTER_ENTRY MapRegisterBase,
                  IN PVOID CurrentVa,
                  IN ULONG Length,
                  IN BOOLEAN WriteToDevice)
{
    ULONG CurrentLength;
    ULONG_PTR CurrentAddress;
    ULONG ByteOffset;
    PVOID VirtualAddress;

    VirtualAddress = MmGetSystemAddressForMdlSafe(Mdl, HighPagePriority);
    if (!VirtualAddress)
    {
        /*
         * NOTE: On real NT a mechanism with reserved pages is implemented
         * to handle this case in a slow, but graceful non-fatal way.
         */
         KeBugCheckEx(HAL_MEMORY_ALLOCATION, PAGE_SIZE, 0, (ULONG_PTR)__FILE__, 0);
    }

    CurrentAddress = (ULONG_PTR)VirtualAddress +
                     (ULONG_PTR)CurrentVa -
                     (ULONG_PTR)MmGetMdlVirtualAddress(Mdl);

    while (Length > 0)
    {
        ByteOffset = BYTE_OFFSET(CurrentAddress);
        CurrentLength = PAGE_SIZE - ByteOffset;
        if (CurrentLength > Length) CurrentLength = Length;

        if (WriteToDevice)
        {
            RtlCopyMemory((PVOID)((ULONG_PTR)MapRegisterBase->VirtualAddress + ByteOffset),
                          (PVOID)CurrentAddress,
                          CurrentLength);
        }
        else
        {
            RtlCopyMemory((PVOID)CurrentAddress,
                          (PVOID)((ULONG_PTR)MapRegisterBase->VirtualAddress + ByteOffset),
                          CurrentLength);
        }

        Length -= CurrentLength;
        CurrentAddress += CurrentLength;
        MapRegisterBase++;
    }
}

/**
 * @name IoFlushAdapterBuffers
 *
 * Flush any data remaining in the DMA controller's memory into the host
 * memory.
 *
 * @param AdapterObject
 *        The adapter object to flush.
 * @param Mdl
 *        Original MDL to flush data into.
 * @param MapRegisterBase
 *        Map register base that was just used by IoMapTransfer, etc.
 * @param CurrentVa
 *        Offset into Mdl to be flushed into, same as was passed to
 *        IoMapTransfer.
 * @param Length
 *        Length of the buffer to be flushed into.
 * @param WriteToDevice
 *        TRUE if it's a write, FALSE if it's a read.
 *
 * @return TRUE in all cases.
 *
 * @remarks
 *    This copies data from the map register-backed buffer to the user's
 *    target buffer. Data are not in the user buffer until this function
 *    is called.
 *    For slave DMA transfers the controller channel is masked effectively
 *    stopping the current transfer.
 *
 * @unimplemented.
 */
BOOLEAN
NTAPI
IoFlushAdapterBuffers(IN PADAPTER_OBJECT AdapterObject,
                      IN PMDL Mdl,
                      IN PVOID MapRegisterBase,
                      IN PVOID CurrentVa,
                      IN ULONG Length,
                      IN BOOLEAN WriteToDevice)
{
    BOOLEAN SlaveDma = FALSE;
    PROS_MAP_REGISTER_ENTRY RealMapRegisterBase;
    PHYSICAL_ADDRESS HighestAcceptableAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    PPFN_NUMBER MdlPagesPtr;

    /* Sanity checks */
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);
    ASSERT(AdapterObject);

    if (!AdapterObject->MasterDevice)
    {
        /* Mask out (disable) the DMA channel. */
        if (AdapterObject->AdapterNumber == 1)
        {
            PDMA1_CONTROL DmaControl1 = AdapterObject->AdapterBaseVa;
            WRITE_PORT_UCHAR(&DmaControl1->SingleMask,
                             AdapterObject->ChannelNumber | DMA_SETMASK);
        }
        else
        {
            PDMA2_CONTROL DmaControl2 = AdapterObject->AdapterBaseVa;
            WRITE_PORT_UCHAR(&DmaControl2->SingleMask,
                             AdapterObject->ChannelNumber | DMA_SETMASK);
        }
        SlaveDma = TRUE;
    }

    /* This can happen if the device supports hardware scatter/gather. */
    if (MapRegisterBase == NULL) return TRUE;

    RealMapRegisterBase = (PROS_MAP_REGISTER_ENTRY)((ULONG_PTR)MapRegisterBase & ~MAP_BASE_SW_SG);

    if (!WriteToDevice)
    {
        if ((ULONG_PTR)MapRegisterBase & MAP_BASE_SW_SG)
        {
            if (RealMapRegisterBase->Counter != MAXULONG)
            {
                if ((SlaveDma) && !(AdapterObject->IgnoreCount))
                {
                    Length -= HalReadDmaCounter(AdapterObject);
                }
            }
            HalpCopyBufferMap(Mdl,
                              RealMapRegisterBase,
                              CurrentVa,
                              Length,
                              FALSE);
        }
        else
        {
            MdlPagesPtr = MmGetMdlPfnArray(Mdl);
            MdlPagesPtr += ((ULONG_PTR)CurrentVa - (ULONG_PTR)Mdl->StartVa) >> PAGE_SHIFT;

            PhysicalAddress.QuadPart = *MdlPagesPtr << PAGE_SHIFT;
            PhysicalAddress.QuadPart += BYTE_OFFSET(CurrentVa);

            HighestAcceptableAddress = HalpGetAdapterMaximumPhysicalAddress(AdapterObject);
            if ((PhysicalAddress.QuadPart + Length) > HighestAcceptableAddress.QuadPart)
            {
                HalpCopyBufferMap(Mdl,
                                  RealMapRegisterBase,
                                  CurrentVa,
                                  Length,
                                  FALSE);
            }
        }
    }

    RealMapRegisterBase->Counter = 0;

    return TRUE;
}

/**
 * @name IoMapTransfer
 *
 * Map a DMA for transfer and do the DMA if it's a slave.
 *
 * @param AdapterObject
 *        Adapter object to do the DMA on. Bus-master may pass NULL.
 * @param Mdl
 *        Locked-down user buffer to DMA in to or out of.
 * @param MapRegisterBase
 *        Handle to map registers to use for this dma.
 * @param CurrentVa
 *        Index into Mdl to transfer into/out of.
 * @param Length
 *        Length of transfer. Number of bytes actually transferred on
 *        output.
 * @param WriteToDevice
 *        TRUE if it's an output DMA, FALSE otherwise.
 *
 * @return
 *    A logical address that can be used to program a DMA controller, it's
 *    not meaningful for slave DMA device.
 *
 * @remarks
 *    This function does a copyover to contiguous memory <16MB represented
 *    by the map registers if needed. If the buffer described by MDL can be
 *    used as is no copyover is done.
 *    If it's a slave transfer, this function actually performs it.
 *
 * @implemented
 */
PHYSICAL_ADDRESS
NTAPI
IoMapTransfer(IN PADAPTER_OBJECT AdapterObject,
              IN PMDL Mdl,
              IN PVOID MapRegisterBase,
              IN PVOID CurrentVa,
              IN OUT PULONG Length,
              IN BOOLEAN WriteToDevice)
{
    PPFN_NUMBER MdlPagesPtr;
    PFN_NUMBER MdlPage1, MdlPage2;
    ULONG ByteOffset;
    ULONG TransferOffset;
    ULONG TransferLength;
    BOOLEAN UseMapRegisters;
    PROS_MAP_REGISTER_ENTRY RealMapRegisterBase;
    PHYSICAL_ADDRESS PhysicalAddress;
    PHYSICAL_ADDRESS HighestAcceptableAddress;
    ULONG Counter;
    DMA_MODE AdapterMode;
    KIRQL OldIrql;

    /*
     * Precalculate some values that are used in all cases.
     *
     * ByteOffset is offset inside the page at which the transfer starts.
     * MdlPagesPtr is pointer inside the MDL page chain at the page where the
     *             transfer start.
     * PhysicalAddress is physical address corresponding to the transfer
     *                 start page and offset.
     * TransferLength is the initial length of the transfer, which is reminder
     *                of the first page. The actual value is calculated below.
     *
     * Note that all the variables can change during the processing which
     * takes place below. These are just initial values.
     */
    ByteOffset = BYTE_OFFSET(CurrentVa);

    MdlPagesPtr = MmGetMdlPfnArray(Mdl);
    MdlPagesPtr += ((ULONG_PTR)CurrentVa - (ULONG_PTR)Mdl->StartVa) >> PAGE_SHIFT;

    PhysicalAddress.QuadPart = *MdlPagesPtr << PAGE_SHIFT;
    PhysicalAddress.QuadPart += ByteOffset;

    TransferLength = PAGE_SIZE - ByteOffset;

    /*
     * Special case for bus master adapters with S/G support. We can directly
     * use the buffer specified by the MDL, so not much work has to be done.
     *
     * Just return the passed VA's corresponding physical address and update
     * length to the number of physically contiguous bytes found. Also
     * pages crossing the 4Gb boundary aren't considered physically contiguous.
     */
    if (MapRegisterBase == NULL)
    {
        BOOLEAN EnforceLimit = FALSE;
        ULONGLONG MaxAddress = (ULONGLONG)-1;

        if (AdapterObject)
        {
            HighestAcceptableAddress = HalpGetAdapterMaximumPhysicalAddress(AdapterObject);
            if (HighestAcceptableAddress.QuadPart != -1)
            {
                EnforceLimit = TRUE;
                MaxAddress = HighestAcceptableAddress.QuadPart;
            }
        }

        while (TransferLength < *Length)
        {
            MdlPage1 = *MdlPagesPtr;
            MdlPage2 = *(MdlPagesPtr + 1);
            if (MdlPage1 + 1 != MdlPage2) break;
            if (EnforceLimit)
            {
                ULONGLONG NextEnd = PhysicalAddress.QuadPart +
                                    TransferLength +
                                    PAGE_SIZE - 1;
                if (NextEnd > MaxAddress) break;
            }
            TransferLength += PAGE_SIZE;
            MdlPagesPtr++;
        }

        if (TransferLength < *Length) *Length = TransferLength;

        return PhysicalAddress;
    }

    /*
     * The code below applies to slave DMA adapters and bus master adapters
     * without hardward S/G support.
     */
    RealMapRegisterBase = (PROS_MAP_REGISTER_ENTRY)((ULONG_PTR)MapRegisterBase & ~MAP_BASE_SW_SG);

    /*
     * Try to calculate the size of the transfer. We can only transfer
     * pages that are physically contiguous and that don't cross the
     * 64Kb boundary (this limitation applies only for ISA controllers).
     */
    while (TransferLength < *Length)
    {
        MdlPage1 = *MdlPagesPtr;
        MdlPage2 = *(MdlPagesPtr + 1);
        if (MdlPage1 + 1 != MdlPage2) break;
        if ((!AdapterObject) ||
            ((!AdapterObject->MasterDevice) &&
             ((MdlPage1 ^ MdlPage2) & ~0xF))) break;
        TransferLength += PAGE_SIZE;
        MdlPagesPtr++;
    }

    if (TransferLength > *Length) TransferLength = *Length;

    /*
     * If we're about to simulate software S/G and not all the pages are
     * physically contiguous then we must use the map registers to store
     * the data and allow the whole transfer to proceed at once.
     */
    if (((ULONG_PTR)MapRegisterBase & MAP_BASE_SW_SG) && (TransferLength < *Length))
    {
        UseMapRegisters = TRUE;
        PhysicalAddress = RealMapRegisterBase->PhysicalAddress;
        PhysicalAddress.QuadPart += ByteOffset;
        TransferLength = *Length;
        RealMapRegisterBase->Counter = MAXULONG;
        Counter = 0;
    }
    else
    {
        /*
         * This is ordinary DMA transfer, so just update the progress
         * counters. These are used by IoFlushAdapterBuffers to track
         * the transfer progress.
         */
        UseMapRegisters = FALSE;
        Counter = RealMapRegisterBase->Counter;
        RealMapRegisterBase->Counter += BYTES_TO_PAGES(ByteOffset + TransferLength);

        /*
         * Check if the buffer doesn't exceed the highest physical address
         * limit of the device. In that case we must use the map registers to
         * store the data.
         */
        HighestAcceptableAddress = HalpGetAdapterMaximumPhysicalAddress(AdapterObject);
        if ((PhysicalAddress.QuadPart + TransferLength) > HighestAcceptableAddress.QuadPart)
        {
            UseMapRegisters = TRUE;
            PhysicalAddress = RealMapRegisterBase[Counter].PhysicalAddress;
            PhysicalAddress.QuadPart += ByteOffset;
            if ((ULONG_PTR)MapRegisterBase & MAP_BASE_SW_SG)
            {
                RealMapRegisterBase->Counter = MAXULONG;
                Counter = 0;
            }
        }
    }

    /*
     * If we decided to use the map registers (see above) and we're about
     * to transfer data to the device then copy the buffers into the map
     * register memory.
     */
    if ((UseMapRegisters) && (WriteToDevice))
    {
        HalpCopyBufferMap(Mdl,
                          RealMapRegisterBase + Counter,
                          CurrentVa,
                          TransferLength,
                          WriteToDevice);
    }

    /*
     * Return the length of transfer that actually takes place.
     */
    *Length = TransferLength;

    /*
     * If we're doing slave (system) DMA then program the (E)ISA controller
     * to actually start the transfer.
     */
    if ((AdapterObject) && !(AdapterObject->MasterDevice))
    {
        AdapterMode = AdapterObject->AdapterMode;

        if (WriteToDevice)
        {
            AdapterMode.TransferType = WRITE_TRANSFER;
        }
        else
        {
            AdapterMode.TransferType = READ_TRANSFER;
            if (AdapterObject->IgnoreCount)
            {
                RtlZeroMemory((PUCHAR)RealMapRegisterBase[Counter].VirtualAddress + ByteOffset,
                              TransferLength);
            }
        }

        TransferOffset = PhysicalAddress.LowPart & 0xFFFF;
        if (AdapterObject->Width16Bits)
        {
            TransferLength >>= 1;
            TransferOffset >>= 1;
        }

        KeAcquireSpinLock(&AdapterObject->MasterAdapter->SpinLock, &OldIrql);

        if (AdapterObject->AdapterNumber == 1)
        {
            PVOID Base = AdapterObject->AdapterBaseVa;

            /* Reset Register */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, ClearBytePointer)), 0);

            /* Set the Mode */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, Mode)), AdapterMode.Byte);

            /* Set the Offset Register */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, DmaAddressCount) +
                                      sizeof(DMA1_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                                      FIELD_OFFSET(DMA1_ADDRESS_COUNT, DmaBaseAddress)),
                             (UCHAR)(TransferOffset));
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, DmaAddressCount) +
                                      sizeof(DMA1_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                                      FIELD_OFFSET(DMA1_ADDRESS_COUNT, DmaBaseAddress)),
                             (UCHAR)(TransferOffset >> 8));

            /* Set the Page Register */
            WRITE_PORT_UCHAR(AdapterObject->PagePort + FIELD_OFFSET(EISA_CONTROL, DmaController1Pages),
                             (UCHAR)(PhysicalAddress.LowPart >> 16));
            if (HalpEisaDma)
            {
                WRITE_PORT_UCHAR(AdapterObject->PagePort + FIELD_OFFSET(EISA_CONTROL, DmaController2Pages),
                                 0);
            }

            /* Set the Length */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, DmaAddressCount) +
                                      sizeof(DMA1_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                                      FIELD_OFFSET(DMA1_ADDRESS_COUNT, DmaBaseCount)),
                             (UCHAR)(TransferLength - 1));
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, DmaAddressCount) +
                                      sizeof(DMA1_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                                      FIELD_OFFSET(DMA1_ADDRESS_COUNT, DmaBaseCount)),
                             (UCHAR)((TransferLength - 1) >> 8));

            /* Unmask the Channel */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA1_CONTROL, SingleMask)),
                             AdapterObject->ChannelNumber | DMA_CLEARMASK);
        }
        else
        {
            PVOID Base = AdapterObject->AdapterBaseVa;

            /* Reset Register */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, ClearBytePointer)), 0);

            /* Set the Mode */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, Mode)), AdapterMode.Byte);

            /* Set the Offset Register */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, DmaAddressCount) +
                                      sizeof(DMA2_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                                      FIELD_OFFSET(DMA2_ADDRESS_COUNT, DmaBaseAddress)),
                             (UCHAR)(TransferOffset));
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, DmaAddressCount) +
                                      sizeof(DMA2_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                                      FIELD_OFFSET(DMA2_ADDRESS_COUNT, DmaBaseAddress)),
                             (UCHAR)(TransferOffset >> 8));

            /* Set the Page Register */
            WRITE_PORT_UCHAR(AdapterObject->PagePort + FIELD_OFFSET(EISA_CONTROL, DmaController1Pages),
                             (UCHAR)(PhysicalAddress.u.LowPart >> 16));
            if (HalpEisaDma)
            {
                WRITE_PORT_UCHAR(AdapterObject->PagePort + FIELD_OFFSET(EISA_CONTROL, DmaController2Pages),
                                 0);
            }

            /* Set the Length */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, DmaAddressCount) +
                                      sizeof(DMA2_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                                      FIELD_OFFSET(DMA2_ADDRESS_COUNT, DmaBaseCount)),
                             (UCHAR)(TransferLength - 1));
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, DmaAddressCount) +
                                      sizeof(DMA2_ADDRESS_COUNT) * AdapterObject->ChannelNumber +
                                      FIELD_OFFSET(DMA2_ADDRESS_COUNT, DmaBaseCount)),
                             (UCHAR)((TransferLength - 1) >> 8));

            /* Unmask the Channel */
            WRITE_PORT_UCHAR((PUCHAR)((ULONG_PTR)Base + FIELD_OFFSET(DMA2_CONTROL, SingleMask)),
                             AdapterObject->ChannelNumber | DMA_CLEARMASK);
        }

        KeReleaseSpinLock(&AdapterObject->MasterAdapter->SpinLock, OldIrql);
    }

    /*
     * Return physical address of the buffer with data that is used for the
     * transfer. It can either point inside the Mdl that was passed by the
     * caller or into the map registers if the Mdl buffer can't be used
     * directly.
     */
     return PhysicalAddress;
}
#endif

/**
 * @name HalFlushCommonBuffer
 *
 * @implemented
 */
BOOLEAN
NTAPI
HalFlushCommonBuffer(IN PADAPTER_OBJECT AdapterObject,
                     IN ULONG Length,
                     IN PHYSICAL_ADDRESS LogicalAddress,
                     IN PVOID VirtualAddress)
{
    /* Function always returns true */
    return TRUE;
}

/*
 * @implemented
 */
PVOID
NTAPI
HalAllocateCrashDumpRegisters(IN PADAPTER_OBJECT AdapterObject,
                              IN OUT PULONG NumberOfMapRegisters)
{
    PADAPTER_OBJECT MasterAdapter = AdapterObject->MasterAdapter;
    ULONG MapRegisterNumber;

    /* Check if it needs map registers */
    if (AdapterObject->NeedsMapRegisters)
    {
        /* Check if we have enough */
        if (*NumberOfMapRegisters > AdapterObject->MapRegistersPerChannel)
        {
            /* We don't, fail */
            AdapterObject->NumberOfMapRegisters = 0;
            return NULL;
        }

        /* Try to find free map registers */
        MapRegisterNumber = RtlFindClearBitsAndSet(MasterAdapter->MapRegisters,
                                                   *NumberOfMapRegisters,
                                                   0);

        /* Check if nothing was found */
        if (MapRegisterNumber == MAXULONG)
        {
            /* No free registers found, so use the base registers */
            RtlSetBits(MasterAdapter->MapRegisters,
                       0,
                       *NumberOfMapRegisters);
            MapRegisterNumber = 0;
        }

        /* Calculate the new base */
        AdapterObject->MapRegisterBase =
            (PROS_MAP_REGISTER_ENTRY)(MasterAdapter->MapRegisterBase +
                                      MapRegisterNumber);

        /* Check if scatter gather isn't supported */
        if (!AdapterObject->ScatterGather)
        {
            /* Set the flag */
            AdapterObject->MapRegisterBase =
                (PROS_MAP_REGISTER_ENTRY)
                ((ULONG_PTR)AdapterObject->MapRegisterBase | MAP_BASE_SW_SG);
        }
    }
    else
    {
        AdapterObject->MapRegisterBase = NULL;
        AdapterObject->NumberOfMapRegisters = 0;
    }

    /* Return the base */
    return AdapterObject->MapRegisterBase;
}

/* EOF */
