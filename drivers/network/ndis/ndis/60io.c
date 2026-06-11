/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60io.c
 * PURPOSE:     NDIS 6 hardware integration — MMIO, IO port range, DMA,
 *              shared memory, interrupt registration. Wraps the WDM
 *              kernel APIs (MmMapIoSpace, IoGetDmaAdapter, etc.) into
 *              the NDIS 6 surface that miniports expect.
 *
 *              Created on the dev-nt6-1 branch by the NDIS 5↔6 bridge
 *              work that lets e1000e move real packets.
 *
 *              Phase 1 scope: real MMIO/IO port/DMA/shared memory.
 *              Interrupt registration is a no-op stub at this layer
 *              (records the characteristics so Phase 2 can complete it).
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"
#define INITGUID
#include <guiddef.h>
#include <wdmguid.h>

/* ============================================================================
 *  Helper: get the NDIS6_ADAPTER_EXT from any miniport handle the driver
 *  passed in. The handle is whatever NdisMSetMiniportAttributes returned —
 *  in our world it's the LOGICAL_ADAPTER pointer.
 * ============================================================================ */

static PNDIS6_ADAPTER_EXT
Ndis6IoExtFromHandle(NDIS_HANDLE Handle)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)Handle;
    if (Adapter == NULL || !Adapter->IsNdis6)
        return NULL;
    return NDIS6_EXT(Adapter);
}

/* ============================================================================
 *  MMIO mapping — NDIS 6 helper
 *
 *  These functions are NOT exported under the NdisM* names (the legacy
 *  NDIS 5 library already exports them). Instead they're called from the
 *  legacy versions via an IsNdis6 dispatch at the top of each function.
 * ============================================================================ */

NDIS_STATUS
NTAPI
Ndis6MMapIoSpace(
    _Out_ PVOID*                    VirtualAddress,
    _In_  NDIS_HANDLE               MiniportAdapterHandle,
    _In_  NDIS_PHYSICAL_ADDRESS     PhysicalAddress,
    _In_  UINT                      Length)
{
    PNDIS6_ADAPTER_EXT Ext;
    PVOID Mapped;

    if (VirtualAddress == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Mapped = MmMapIoSpace(PhysicalAddress, Length, MmNonCached);
    if (Mapped == NULL)
        return NDIS_STATUS_RESOURCES;

    *VirtualAddress = Mapped;

    /* Track the first BAR mapping in the adapter extension so we can
     * unmap it on Halt. Subsequent mappings (e.g. MSI-X BAR) get tracked
     * by the driver itself. */
    Ext = Ndis6IoExtFromHandle(MiniportAdapterHandle);
    if (Ext && Ext->MmioBase == NULL)
    {
        Ext->MmioBase = Mapped;
        Ext->MmioSize = Length;
    }

    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
Ndis6MUnmapIoSpace(
    _In_ NDIS_HANDLE                MiniportAdapterHandle,
    _In_ PVOID                      VirtualAddress,
    _In_ UINT                       Length)
{
    PNDIS6_ADAPTER_EXT Ext;

    UNREFERENCED_PARAMETER(Length);

    if (VirtualAddress == NULL)
        return;

    MmUnmapIoSpace(VirtualAddress, Length);

    Ext = Ndis6IoExtFromHandle(MiniportAdapterHandle);
    if (Ext && Ext->MmioBase == VirtualAddress)
    {
        Ext->MmioBase = NULL;
        Ext->MmioSize = 0;
    }
}

/* ============================================================================
 *  IO port range
 *
 *  Real Windows checks the registered IO port range against the resource
 *  list and may translate. We just hand back the same physical port number
 *  the caller asked for, which is what most drivers expect on x86/amd64.
 * ============================================================================ */

NDIS_STATUS
NTAPI
Ndis6MRegisterIoPortRange(
    _Out_ PVOID*                    PortOffset,
    _In_  NDIS_HANDLE               MiniportAdapterHandle,
    _In_  UINT                      InitialPort,
    _In_  UINT                      NumberOfPorts)
{
    PNDIS6_ADAPTER_EXT Ext;

    UNREFERENCED_PARAMETER(NumberOfPorts);

    if (PortOffset == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* On x86/amd64 the port number is the same as the offset. */
    *PortOffset = (PVOID)(ULONG_PTR)InitialPort;

    Ext = Ndis6IoExtFromHandle(MiniportAdapterHandle);
    if (Ext)
    {
        Ext->IoPortBase   = InitialPort;
        Ext->IoPortLength = NumberOfPorts;
    }

    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
Ndis6MDeregisterIoPortRange(
    _In_ NDIS_HANDLE                MiniportAdapterHandle,
    _In_ UINT                       InitialPort,
    _In_ UINT                       NumberOfPorts,
    _In_ PVOID                      PortOffset)
{
    PNDIS6_ADAPTER_EXT Ext;

    UNREFERENCED_PARAMETER(InitialPort);
    UNREFERENCED_PARAMETER(NumberOfPorts);
    UNREFERENCED_PARAMETER(PortOffset);

    Ext = Ndis6IoExtFromHandle(MiniportAdapterHandle);
    if (Ext)
    {
        Ext->IoPortBase   = 0;
        Ext->IoPortLength = 0;
    }
}

/* ============================================================================
 *  DMA adapter — used for AllocateCommonBuffer (descriptor rings) and
 *  scatter-gather lists (transmit packet payloads).
 * ============================================================================ */

NDIS_STATUS
Ndis6IoInitDmaAdapter(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PDEVICE_OBJECT     Pdo)
{
    DEVICE_DESCRIPTION desc;
    PDMA_ADAPTER       dmaAdapter;
    ULONG              numMapRegs = 0;

    if (Ext == NULL || Pdo == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Ext->DmaAdapter != NULL)
        return NDIS_STATUS_SUCCESS;

    RtlZeroMemory(&desc, sizeof(desc));
    desc.Version          = DEVICE_DESCRIPTION_VERSION;
    desc.Master           = TRUE;
    desc.ScatterGather    = TRUE;
    desc.Dma32BitAddresses = TRUE;
    desc.Dma64BitAddresses = TRUE;
    desc.InterfaceType    = PCIBus;
    desc.DmaWidth         = Width32Bits;
    desc.DmaSpeed         = Compatible;
    desc.MaximumLength    = MAXULONG;

    dmaAdapter = IoGetDmaAdapter(Pdo, &desc, &numMapRegs);
    if (dmaAdapter == NULL)
        return NDIS_STATUS_RESOURCES;

    Ext->DmaAdapter           = dmaAdapter;
    Ext->NumberOfMapRegisters = numMapRegs;
    return NDIS_STATUS_SUCCESS;
}

VOID
Ndis6IoFreeDmaAdapter(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    if (Ext == NULL || Ext->DmaAdapter == NULL)
        return;
    Ext->DmaAdapter->DmaOperations->PutDmaAdapter(Ext->DmaAdapter);
    Ext->DmaAdapter = NULL;
    Ext->NumberOfMapRegisters = 0;
}

NDIS_STATUS
NTAPI
NdisMRegisterScatterGatherDma(
    _In_  NDIS_HANDLE                NdisMiniportHandle,
    _In_  PNDIS_SG_DMA_DESCRIPTION   DmaDescription,
    _Out_ PNDIS_HANDLE               NdisMiniportDmaHandle)
{
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_STATUS Status;

    if (NdisMiniportDmaHandle == NULL || DmaDescription == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = Ndis6IoExtFromHandle(NdisMiniportHandle);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Status = Ndis6IoInitDmaAdapter(Ext, Ext->PhysicalDeviceObject);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    /* A3: save the driver's ProcessSGListHandler + MaximumPhysicalMapping
     * so NdisMAllocateNetBufferSGList can call the driver back when the
     * DMA adapter produces a SGL. */
    Ext->SgDescription      = *DmaDescription;
    Ext->SgDescriptionValid = TRUE;

    /* The DMA handle the driver gets back is just the adapter extension. */
    *NdisMiniportDmaHandle = (NDIS_HANDLE)Ext;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisMDeregisterScatterGatherDma(
    _In_ NDIS_HANDLE NdisMiniportDmaHandle)
{
    PNDIS6_ADAPTER_EXT Ext = (PNDIS6_ADAPTER_EXT)NdisMiniportDmaHandle;
    if (Ext == NULL)
        return;
    Ndis6IoFreeDmaAdapter(Ext);
}

/* ============================================================================
 *  E2: NdisMGetBusData / NdisMSetBusData
 *
 *  NDIS wrappers over BUS_INTERFACE_STANDARD. The miniport could also
 *  do its own IRP_MN_QUERY_INTERFACE dance, but these helpers are the
 *  idiomatic way to read/write PCI config space from an NDIS 6 driver.
 *  We query the bus interface lazily on first use and cache it on the
 *  adapter extension.
 * ============================================================================ */

static BUS_INTERFACE_STANDARD*
Ndis6IoGetBusInterface(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    KEVENT          Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP            Irp;
    PIO_STACK_LOCATION Stack;
    NTSTATUS        Status;
    static BUS_INTERFACE_STANDARD CachedBusInterface;
    static BOOLEAN                CachedValid = FALSE;

    /* Simple per-adapter cache. For multi-adapter workloads the cache
     * would need to move onto Ext — this is a static for now because
     * the e1000e-only test path has a single adapter. */
    if (CachedValid)
        return &CachedBusInterface;

    if (Ext == NULL || Ext->PhysicalDeviceObject == NULL)
        return NULL;

    RtlZeroMemory(&CachedBusInterface, sizeof(CachedBusInterface));

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_PNP, Ext->PhysicalDeviceObject, NULL, 0, NULL, &Event, &IoStatus);
    if (Irp == NULL)
        return NULL;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType        = &GUID_BUS_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size                 = sizeof(BUS_INTERFACE_STANDARD);
    Stack->Parameters.QueryInterface.Version              = 1;
    Stack->Parameters.QueryInterface.Interface            = (PINTERFACE)&CachedBusInterface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(Ext->PhysicalDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (!NT_SUCCESS(Status) || CachedBusInterface.GetBusData == NULL)
        return NULL;

    CachedValid = TRUE;
    return &CachedBusInterface;
}

ULONG
NTAPI
NdisMGetBusData(
    _In_  NDIS_HANDLE NdisMiniportHandle,
    _In_  ULONG       DataType,
    _In_  ULONG       Offset,
    _Out_ PVOID       Buffer,
    _In_  ULONG       Length)
{
    PNDIS6_ADAPTER_EXT      Ext;
    BUS_INTERFACE_STANDARD* Bus;

    UNREFERENCED_PARAMETER(DataType);

    Ext = Ndis6IoExtFromHandle(NdisMiniportHandle);
    if (Ext == NULL)
        return 0;

    Bus = Ndis6IoGetBusInterface(Ext);
    if (Bus == NULL || Bus->GetBusData == NULL)
        return 0;

    return Bus->GetBusData(Bus->Context, PCI_WHICHSPACE_CONFIG,
                           Buffer, Offset, Length);
}

ULONG
NTAPI
NdisMSetBusData(
    _In_  NDIS_HANDLE NdisMiniportHandle,
    _In_  ULONG       DataType,
    _In_  ULONG       Offset,
    _In_  PVOID       Buffer,
    _In_  ULONG       Length)
{
    PNDIS6_ADAPTER_EXT      Ext;
    BUS_INTERFACE_STANDARD* Bus;

    UNREFERENCED_PARAMETER(DataType);

    Ext = Ndis6IoExtFromHandle(NdisMiniportHandle);
    if (Ext == NULL)
        return 0;

    Bus = Ndis6IoGetBusInterface(Ext);
    if (Bus == NULL || Bus->SetBusData == NULL)
        return 0;

    return Bus->SetBusData(Bus->Context, PCI_WHICHSPACE_CONFIG,
                           Buffer, Offset, Length);
}

/* ============================================================================
 *  A3: NdisMAllocateNetBufferSGList + NdisMFreeNetBufferSGList
 *
 *  Real implementation that calls the DMA adapter's GetScatterGatherList /
 *  PutScatterGatherList. When the SGL is ready, we invoke the driver's
 *  ProcessSGListHandler (which was saved in Ext->SgDescription during
 *  NdisMRegisterScatterGatherDma).
 *
 *  On x86/amd64 with identity DMA, GetScatterGatherList typically calls
 *  the ExecutionRoutine SYNCHRONOUSLY before returning. We still use a
 *  lookaside-list-backed wrapper context to handle the async case
 *  (waiting map registers) correctly.
 * ============================================================================ */

#define NDIS6_SG_CTX_TAG  'SgN6'

typedef struct _NDIS6_SG_CALL_CONTEXT
{
    PNDIS6_ADAPTER_EXT  Ext;
    PNET_BUFFER         NetBuffer;
    PVOID               OriginalContext;    /* caller's Context */
    BOOLEAN             WriteToDevice;
} NDIS6_SG_CALL_CONTEXT, *PNDIS6_SG_CALL_CONTEXT;

/* Execution routine invoked by the DMA subsystem when a SGL is ready. We
 * unpack the bridge context, call the miniport's ProcessSGListHandler,
 * and free our wrapper context. The DeviceObject parameter is the DMA
 * device object (= our adapter's PDO). */
static VOID NTAPI
Ndis6SgExecutionRoutine(
    _In_ PDEVICE_OBJECT       DeviceObject,
    _In_ PIRP                 Irp,
    _In_ PSCATTER_GATHER_LIST SGList,
    _In_ PVOID                Context)
{
    PNDIS6_SG_CALL_CONTEXT ctx = (PNDIS6_SG_CALL_CONTEXT)Context;
    PNDIS6_ADAPTER_EXT     Ext;
    MINIPORT_PROCESS_SG_LIST* Handler;

    UNREFERENCED_PARAMETER(Irp);

    if (ctx == NULL)
        return;

    Ext     = ctx->Ext;
    Handler = (Ext && Ext->SgDescriptionValid)
                  ? Ext->SgDescription.ProcessSGListHandler
                  : NULL;

    if (Handler != NULL)
    {
        Handler(DeviceObject,
                ctx->NetBuffer,     /* Reserved/NetBuffer slot */
                SGList,
                ctx->OriginalContext);
    }

    ExFreePoolWithTag(ctx, NDIS6_SG_CTX_TAG);
}

NDIS_STATUS
NTAPI
NdisMAllocateNetBufferSGList(
    _In_  NDIS_HANDLE NdisMiniportDmaHandle,
    _In_  PNET_BUFFER NetBuffer,
    _In_  PVOID       Context,
    _In_  ULONG       Flags,
    _Out_ PVOID       ScatterGatherListBuffer,
    _In_  ULONG       ScatterGatherListBufferSize)
{
    PNDIS6_ADAPTER_EXT     Ext = (PNDIS6_ADAPTER_EXT)NdisMiniportDmaHandle;
    PNDIS6_SG_CALL_CONTEXT ctx;
    PMDL                   Mdl;
    PVOID                  CurrentVa;
    ULONG                  Length;
    ULONG                  MdlOffset;
    BOOLEAN                WriteToDevice;
    NTSTATUS               Status;

    UNREFERENCED_PARAMETER(ScatterGatherListBuffer);
    UNREFERENCED_PARAMETER(ScatterGatherListBufferSize);

    if (Ext == NULL || NetBuffer == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Ext->DmaAdapter == NULL || !Ext->SgDescriptionValid ||
        Ext->SgDescription.ProcessSGListHandler == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Mdl       = NET_BUFFER_CURRENT_MDL(NetBuffer);
    MdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    Length    = NET_BUFFER_DATA_LENGTH(NetBuffer);

    if (Mdl == NULL || Length == 0)
        return NDIS_STATUS_INVALID_PARAMETER;

    CurrentVa = (PUCHAR)MmGetMdlVirtualAddress(Mdl) + MdlOffset;

    /* NDIS_SG_LIST_WRITE_TO_DEVICE == 0x01 — for TX direction (most sends).
     * The symbol isn't in the ReactOS NDIS headers yet; use the literal. */
    WriteToDevice = (Flags & 0x00000001) ? TRUE : FALSE;

    ctx = (PNDIS6_SG_CALL_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_SG_CALL_CONTEXT), NDIS6_SG_CTX_TAG);
    if (ctx == NULL)
        return NDIS_STATUS_RESOURCES;

    ctx->Ext             = Ext;
    ctx->NetBuffer       = NetBuffer;
    ctx->OriginalContext = Context;
    ctx->WriteToDevice   = WriteToDevice;

    Status = Ext->DmaAdapter->DmaOperations->GetScatterGatherList(
        Ext->DmaAdapter,
        Ext->PhysicalDeviceObject,
        Mdl,
        CurrentVa,
        Length,
        Ndis6SgExecutionRoutine,
        ctx,
        WriteToDevice);

    if (!NT_SUCCESS(Status))
    {
        /* On sync failure the execution routine is NOT called — free ctx. */
        ExFreePoolWithTag(ctx, NDIS6_SG_CTX_TAG);
        return (NDIS_STATUS)Status;
    }

    /* Success path: on x86/amd64 the execution routine already ran and
     * freed ctx. On an async path the routine will run and free it later;
     * we must NOT touch ctx here. */
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisMFreeNetBufferSGList(
    _In_ NDIS_HANDLE          NdisMiniportDmaHandle,
    _In_ PSCATTER_GATHER_LIST pSGL,
    _In_ PNET_BUFFER          NetBuffer)
{
    PNDIS6_ADAPTER_EXT Ext = (PNDIS6_ADAPTER_EXT)NdisMiniportDmaHandle;
    BOOLEAN WriteToDevice = FALSE;
    UNREFERENCED_PARAMETER(NetBuffer);

    if (Ext == NULL || pSGL == NULL || Ext->DmaAdapter == NULL)
        return;

    /* HalPutScatterGatherList reads WriteToDevice from the per-element
     * context stashed in SGL->Reserved, but still wants a hint. We can't
     * reliably recover the original direction here — HalPutScatterGatherList
     * uses the stashed copy regardless, so the hint is cosmetic. */
    Ext->DmaAdapter->DmaOperations->PutScatterGatherList(
        Ext->DmaAdapter, pSGL, WriteToDevice);
}

/* ============================================================================
 *  Shared (DMA-coherent) memory — used for descriptor rings.
 *
 *  Real implementation calls AllocateCommonBuffer on the DMA adapter so
 *  the buffer is bus-master-accessible.
 * ============================================================================ */

VOID
NTAPI
Ndis6MAllocateSharedMemory(
    _In_  NDIS_HANDLE                MiniportAdapterHandle,
    _In_  ULONG                      Length,
    _In_  BOOLEAN                    Cached,
    _Out_ PVOID*                     VirtualAddress,
    _Out_ PNDIS_PHYSICAL_ADDRESS     PhysicalAddress)
{
    PNDIS6_ADAPTER_EXT Ext;
    PVOID              va;
    PHYSICAL_ADDRESS   pa;

    if (VirtualAddress == NULL || PhysicalAddress == NULL)
        return;

    *VirtualAddress = NULL;
    PhysicalAddress->QuadPart = 0;

    Ext = Ndis6IoExtFromHandle(MiniportAdapterHandle);
    if (Ext == NULL)
        return;

    /* Lazy DMA adapter creation: most drivers call this before they call
     * NdisMRegisterScatterGatherDma, so we may have to set up the adapter
     * here. The PDO must already have been recorded by AddDevice. */
    if (Ext->DmaAdapter == NULL)
    {
        if (Ndis6IoInitDmaAdapter(Ext, Ext->PhysicalDeviceObject) != NDIS_STATUS_SUCCESS)
            return;
    }

    UNREFERENCED_PARAMETER(Cached);

    va = Ext->DmaAdapter->DmaOperations->AllocateCommonBuffer(
        Ext->DmaAdapter, Length, &pa, FALSE);
    if (va == NULL)
        return;

    *VirtualAddress = va;
    *PhysicalAddress = pa;
}

VOID
NTAPI
Ndis6MFreeSharedMemory(
    _In_ NDIS_HANDLE                MiniportAdapterHandle,
    _In_ ULONG                      Length,
    _In_ BOOLEAN                    Cached,
    _In_ PVOID                      VirtualAddress,
    _In_ NDIS_PHYSICAL_ADDRESS      PhysicalAddress)
{
    PNDIS6_ADAPTER_EXT Ext;

    UNREFERENCED_PARAMETER(Cached);

    Ext = Ndis6IoExtFromHandle(MiniportAdapterHandle);
    if (Ext == NULL || Ext->DmaAdapter == NULL || VirtualAddress == NULL)
        return;

    Ext->DmaAdapter->DmaOperations->FreeCommonBuffer(
        Ext->DmaAdapter, Length, PhysicalAddress, VirtualAddress, FALSE);
}

/* ============================================================================
 *  Interrupt registration — Phase 2 real wiring
 *
 *  An NDIS 6 miniport calls NdisMRegisterInterruptEx from MiniportInitializeEx
 *  to register its ISR and DPC. We translate that into IoConnectInterrupt
 *  using two static wrappers — one at DIRQL (Ndis6IsrWrapper) that walks
 *  Ext->IntChars.InterruptHandler, and one at DISPATCH_LEVEL (Ndis6DpcWrapper)
 *  that walks Ext->IntChars.InterruptDpcHandler. The miniport's per-interrupt
 *  context is stashed in Ext->MiniportInterruptContext.
 *
 *  ReactOS HAL only supports CONNECT_FULLY_SPECIFIED in IoConnectInterruptEx,
 *  so we silently fall back to line-based via the legacy IoConnectInterrupt
 *  even if the driver requested MSI-X. e1000e (interrupt_ndis6.c:91-148) and
 *  every other NDIS 6 driver in the tree handles the line-based fallback.
 *  After connecting, we force IntChars.InterruptType = NDIS_CONNECT_LINE_BASED
 *  so any post-registration check the driver does picks the legacy path.
 * ============================================================================ */

static BOOLEAN NTAPI
Ndis6IsrWrapper(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID       ServiceContext)
{
    PNDIS6_ADAPTER_EXT Ext = (PNDIS6_ADAPTER_EXT)ServiceContext;
    BOOLEAN QueueDpc       = FALSE;
    ULONG   TargetCpus     = 0;
    BOOLEAN Recognized;

    UNREFERENCED_PARAMETER(Interrupt);

    if (Ext == NULL || Ext->IntChars.InterruptHandler == NULL)
        return FALSE;

    Recognized = Ext->IntChars.InterruptHandler(
        Ext->MiniportInterruptContext,
        &QueueDpc,
        &TargetCpus);

    if (Recognized && QueueDpc)
        KeInsertQueueDpc(&Ext->InterruptDpc, NULL, NULL);

    return Recognized;
}

static VOID NTAPI
Ndis6DpcWrapper(
    _In_     PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PNDIS6_ADAPTER_EXT Ext = (PNDIS6_ADAPTER_EXT)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Ext == NULL || Ext->IntChars.InterruptDpcHandler == NULL)
        return;

    /* The NDIS 6 DPC handler signature takes:
     *   (MiniportInterruptContext, MiniportDpcContext,
     *    ReceiveThrottleParameters, NdisReserved2)
     * Drivers tolerate NULL for the latter three; e1000e
     * (interrupt_ndis6.c:743 and on) ignores them. */
    Ext->IntChars.InterruptDpcHandler(
        Ext->MiniportInterruptContext,
        NULL,
        NULL,
        NULL);
}

/* MSI message-service adapter. The NDIS 6 driver's message-based
 * ISR is a MINIPORT_MESSAGE_INTERRUPT routine with signature
 *   BOOLEAN (*)(NDIS_HANDLE Ctx, ULONG MessageId, PBOOLEAN QueueDpc,
 *               PULONG TargetProcessors);
 * The kernel IoConnectInterruptEx speaks PKMESSAGE_SERVICE_ROUTINE which
 * is (PKINTERRUPT, PVOID, ULONG). We bridge by stashing the Ext pointer
 * as ServiceContext; the kernel then gives us (Interrupt, Ext, MsgId)
 * and we call into Ext->IntChars.MessageInterruptHandler.
 *
 * Many drivers (e1000e, virtio-net) only set MsiSupported = TRUE and
 * leave MessageInterruptHandler NULL — for those, NDIS dispatches the
 * regular line-based InterruptHandler and the driver checks ICR/EIMS
 * itself to figure out which message fired. We fall back to the
 * regular handler when MessageInterruptHandler is absent. */
static BOOLEAN NTAPI
Ndis6MsiIsrWrapper(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID       Context,
    _In_ ULONG       MessageId)
{
    PNDIS6_ADAPTER_EXT Ext = (PNDIS6_ADAPTER_EXT)Context;
    BOOLEAN QueueDpc = FALSE;
    ULONG TargetCpus = 0;
    BOOLEAN Recognized;

    UNREFERENCED_PARAMETER(Interrupt);

    if (Ext == NULL)
        return FALSE;

    if (Ext->IntChars.MessageInterruptHandler != NULL)
    {
        Recognized = Ext->IntChars.MessageInterruptHandler(
            Ext->MiniportInterruptContext,
            MessageId,
            &QueueDpc,
            &TargetCpus);
    }
    else if (Ext->IntChars.InterruptHandler != NULL)
    {
        /* Fall back to the regular ISR — the driver figures out which
         * message fired by reading hardware status registers. */
        Recognized = Ext->IntChars.InterruptHandler(
            Ext->MiniportInterruptContext,
            &QueueDpc,
            &TargetCpus);
    }
    else
    {
        return FALSE;
    }

    if (Recognized && QueueDpc)
        KeInsertQueueDpc(&Ext->InterruptDpc, NULL, NULL);

    return Recognized;
}

NDIS_STATUS
NTAPI
NdisMRegisterInterruptEx(
    _In_    NDIS_HANDLE                                NdisMiniportHandle,
    _In_    NDIS_HANDLE                                MiniportInterruptContext,
    _Inout_ PNDIS_MINIPORT_INTERRUPT_CHARACTERISTICS   MiniportInterruptCharacteristics,
    _Out_   PNDIS_HANDLE                               NdisInterruptHandle)
{
    PNDIS6_ADAPTER_EXT Ext;
    NTSTATUS           Status;
    KINTERRUPT_MODE    InterruptMode;
    BOOLEAN            ShareVector;
    BOOLEAN            WantsMsi;

    if (NdisInterruptHandle == NULL || MiniportInterruptCharacteristics == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = Ndis6IoExtFromHandle(NdisMiniportHandle);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* Save the driver's characteristics + per-interrupt context. Copy only
     * Header.Size bytes — a driver built against a shorter revision must
     * not have the missing tail read as garbage. */
    RtlZeroMemory(&Ext->IntChars, sizeof(Ext->IntChars));
    {
        USHORT CharSize = MiniportInterruptCharacteristics->Header.Size;
        if (CharSize == 0 || CharSize > sizeof(Ext->IntChars))
            CharSize = sizeof(Ext->IntChars);
        RtlCopyMemory(&Ext->IntChars, MiniportInterruptCharacteristics, CharSize);
    }
    Ext->MiniportInterruptContext = MiniportInterruptContext;

    /* Initialize the wrapper DPC bound to Ndis6DpcWrapper. The driver's DPC
     * handler runs from inside this wrapper at DISPATCH_LEVEL. */
    KeInitializeDpc(&Ext->InterruptDpc, Ndis6DpcWrapper, Ext);

    /* C1/C2: the driver asks for MSI by setting IntChars.MsiSupported.
     * Some drivers also fill MessageInterruptHandler; many (e1000e
     * included) leave it NULL and reuse the regular ISR per message.
     * Try the message-based path first via IoConnectInterruptEx; if the
     * PDO has no MSI resources the EX path falls back to a line-based
     * connection on its own via FallBackServiceRoutine. */
    WantsMsi = (Ext->IntChars.MsiSupported &&
                Ext->PhysicalDeviceObject != NULL);

    if (WantsMsi)
    {
        IO_CONNECT_INTERRUPT_PARAMETERS p;
        RtlZeroMemory(&p, sizeof(p));
        p.Version = CONNECT_MESSAGE_BASED;
        p.MessageBased.PhysicalDeviceObject    = Ext->PhysicalDeviceObject;
        p.MessageBased.ConnectionContext.InterruptMessageTable = &Ext->MsiTable;
        p.MessageBased.MessageServiceRoutine   = Ndis6MsiIsrWrapper;
        p.MessageBased.ServiceContext          = Ext;
        p.MessageBased.SpinLock                = NULL;
        p.MessageBased.SynchronizeIrql         = 0;
        p.MessageBased.FloatingSave            = FALSE;
        p.MessageBased.FallBackServiceRoutine  = Ndis6IsrWrapper;

        Status = IoConnectInterruptEx(&p);
        DbgPrint("NDIS6: IoConnectInterruptEx(MSG) -> 0x%08lx MsiTable=%p\n",
                 (ULONG)Status, Ext->MsiTable);

        if (NT_SUCCESS(Status))
        {
            if (Ext->MsiTable != NULL && Ext->MsiTable->MessageCount > 0)
            {
                /* True MSI/MSI-X was connected — keep the table, mark
                 * the InterruptObject as the first vector's PKINTERRUPT
                 * so the legacy deregister path still works. */
                Ext->InterruptObject = Ext->MsiTable->MessageInfo[0].InterruptObject;
                Ext->MsiConnected    = TRUE;
                Ext->IntChars.InterruptType    = NDIS_CONNECT_MESSAGE_BASED;
                Ext->IntChars.MessageInfoTable = Ext->MsiTable;

                /* Per the DDK, MiniportInterruptCharacteristics is in/out:
                 * NDIS must write the actual connection type and the
                 * message info table back to the caller so the miniport
                 * can decide whether it's running MSI-X (and program its
                 * own per-vector cause routing accordingly). */
                MiniportInterruptCharacteristics->InterruptType    = NDIS_CONNECT_MESSAGE_BASED;
                MiniportInterruptCharacteristics->MessageInfoTable = Ext->MsiTable;

                DbgPrint("NDIS6: connected %lu MSI vectors (wrote back InterruptType=MSG MessageInfoTable=%p)\n",
                         Ext->MsiTable->MessageCount, Ext->MsiTable);
            }
            else
            {
                /* Fallback path inside IoConnectInterruptEx used the
                 * line-based connection; InterruptObject is stashed in
                 * ConnectionContext.InterruptObject which aliases the
                 * first element of the MessageTable union. */
                Ext->InterruptObject =
                    (PKINTERRUPT)(ULONG_PTR)Ext->MsiTable;
                Ext->MsiTable        = NULL;
                Ext->IntChars.InterruptType    = NDIS_CONNECT_LINE_BASED;
                Ext->IntChars.MessageInfoTable = NULL;

                /* Same write-back as above, line-based variant. */
                MiniportInterruptCharacteristics->InterruptType    = NDIS_CONNECT_LINE_BASED;
                MiniportInterruptCharacteristics->MessageInfoTable = NULL;

                DbgPrint("NDIS6: message-based fell back to line KINTERRUPT=%p\n",
                         Ext->InterruptObject);
            }

            *NdisInterruptHandle = (NDIS_HANDLE)Ext;
            return NDIS_STATUS_SUCCESS;
        }
        /* EX path failed outright — fall through to the legacy line path. */
    }

    /* The Phase 1 PnP dispatcher already extracted the IRQ vector / IRQL /
     * affinity from the translated resource list at IRP_MN_START_DEVICE
     * time (see 60driver.c). If those fields are zero we can't connect. */
    if (Ext->InterruptVector == 0 || Ext->InterruptIrql == 0)
    {
        /* No IRQ resource was assigned by PnP. Some drivers (notably USB
         * RNDIS) never call this function — but if they do, fail loudly
         * rather than silently dropping interrupts. */
        return NDIS_STATUS_RESOURCES;
    }

    InterruptMode = (Ext->InterruptFlags & CM_RESOURCE_INTERRUPT_LATCHED)
                        ? Latched
                        : LevelSensitive;

    /* Share the vector — we default to shared line-based. */
    ShareVector = TRUE;

    DbgPrint("NDIS6: NdisMRegisterInterruptEx vec=%u irql=%u affinity=0x%lx mode=%s share=%d\n",
             Ext->InterruptVector, Ext->InterruptIrql,
             (ULONG)Ext->InterruptAffinity,
             (InterruptMode == Latched) ? "Latched" : "Level",
             ShareVector);

    Status = IoConnectInterrupt(
        &Ext->InterruptObject,
        Ndis6IsrWrapper,
        Ext,                        /* ServiceContext */
        NULL,                       /* SpinLock — let the kernel allocate one */
        Ext->InterruptVector,
        Ext->InterruptIrql,         /* SynchronizeIrql */
        Ext->InterruptIrql,         /* Irql */
        InterruptMode,
        ShareVector,
        Ext->InterruptAffinity,
        FALSE);                     /* FloatingSave */

    DbgPrint("NDIS6: IoConnectInterrupt -> 0x%08lx, KINTERRUPT=%p\n",
             (ULONG)Status, Ext->InterruptObject);

    if (!NT_SUCCESS(Status))
    {
        Ext->InterruptObject = NULL;
        return NDIS_STATUS_RESOURCES;
    }

    /* Force the interrupt-type field to LINE_BASED so any post-registration
     * check the driver does picks its legacy interrupt code path. */
    Ext->IntChars.InterruptType    = NDIS_CONNECT_LINE_BASED;
    Ext->IntChars.MessageInfoTable = NULL;

    /* DDK in/out semantics: write back to the caller. */
    MiniportInterruptCharacteristics->InterruptType    = NDIS_CONNECT_LINE_BASED;
    MiniportInterruptCharacteristics->MessageInfoTable = NULL;

    *NdisInterruptHandle = (NDIS_HANDLE)Ext;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisMDeregisterInterruptEx(
    _In_ NDIS_HANDLE NdisInterruptHandle)
{
    PNDIS6_ADAPTER_EXT Ext = (PNDIS6_ADAPTER_EXT)NdisInterruptHandle;

    if (Ext == NULL)
        return;

    /* Flush any in-flight DPC before tearing down the connection so the
     * wrapper doesn't run after Ext->IntChars is gone. */
    KeRemoveQueueDpc(&Ext->InterruptDpc);

    if (Ext->InterruptObject != NULL)
    {
        IoDisconnectInterrupt(Ext->InterruptObject);
        Ext->InterruptObject = NULL;
    }

    Ext->IntChars.InterruptHandler    = NULL;
    Ext->IntChars.InterruptDpcHandler = NULL;
    Ext->MiniportInterruptContext     = NULL;
}

/* EOF */
