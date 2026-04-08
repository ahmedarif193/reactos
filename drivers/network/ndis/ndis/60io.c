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

    UNREFERENCED_PARAMETER(DmaDescription);

    if (NdisMiniportDmaHandle == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = Ndis6IoExtFromHandle(NdisMiniportHandle);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Status = Ndis6IoInitDmaAdapter(Ext, Ext->PhysicalDeviceObject);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    /* The DMA handle the driver gets back is just the adapter extension —
     * we don't yet do per-call SG list management, so all we need is for
     * the handle to be unique per adapter and dereferenceable in the
     * Free path. */
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

/* NdisMFreeNetBufferSGList — Phase 5 stub. The Phase 3 TX thunk hands the
 * legacy NDIS_PACKET MDL chain straight to SendNetBufferListsHandler
 * without ever allocating a per-packet SG list, so e1000e's send-completion
 * path that calls this is operating on a synthetic NDIS 6 NBL whose
 * MiniportReserved we own. There's no SG list to actually free. We accept
 * the call and no-op so the driver doesn't see NDIS_STATUS_NOT_SUPPORTED. */
VOID
NTAPI
NdisMFreeNetBufferSGList(
    _In_ NDIS_HANDLE          NdisMiniportDmaHandle,
    _In_ PSCATTER_GATHER_LIST pSGL,
    _In_ PNET_BUFFER          NetBuffer)
{
    UNREFERENCED_PARAMETER(NdisMiniportDmaHandle);
    UNREFERENCED_PARAMETER(pSGL);
    UNREFERENCED_PARAMETER(NetBuffer);
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
    static volatile LONG IsrCount = 0;
    LONG MyCount;

    UNREFERENCED_PARAMETER(Interrupt);

    if (Ext == NULL || Ext->IntChars.InterruptHandler == NULL)
        return FALSE;

    Recognized = Ext->IntChars.InterruptHandler(
        Ext->MiniportInterruptContext,
        &QueueDpc,
        &TargetCpus);

    /* Log the first few ISRs (limit to avoid log flood). */
    MyCount = InterlockedIncrement(&IsrCount);
    if (MyCount <= 30)
        DbgPrint("NDIS6-IRQ: ISR fired #%ld Recognized=%d QueueDpc=%d\n",
                 MyCount, Recognized, QueueDpc);

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

NDIS_STATUS
NTAPI
NdisMRegisterInterruptEx(
    _In_  NDIS_HANDLE                                NdisMiniportHandle,
    _In_  NDIS_HANDLE                                MiniportInterruptContext,
    _In_  PNDIS_MINIPORT_INTERRUPT_CHARACTERISTICS   MiniportInterruptCharacteristics,
    _Out_ PNDIS_HANDLE                               NdisInterruptHandle)
{
    PNDIS6_ADAPTER_EXT Ext;
    NTSTATUS           Status;
    KINTERRUPT_MODE    InterruptMode;
    BOOLEAN            ShareVector;

    if (NdisInterruptHandle == NULL || MiniportInterruptCharacteristics == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = Ndis6IoExtFromHandle(NdisMiniportHandle);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* Save the driver's characteristics + per-interrupt context. */
    Ext->IntChars                 = *MiniportInterruptCharacteristics;
    Ext->MiniportInterruptContext = MiniportInterruptContext;

    /* Initialize the wrapper DPC bound to Ndis6DpcWrapper. The driver's DPC
     * handler runs from inside this wrapper at DISPATCH_LEVEL. */
    KeInitializeDpc(&Ext->InterruptDpc, Ndis6DpcWrapper, Ext);

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

    /* Share the vector unless the driver explicitly asked for MSI-X (which
     * we can't satisfy on this HAL — fall back to line-based shared). */
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
     * check the driver does picks its legacy interrupt code path. e1000e
     * inspects this in interrupt_ndis6.c around line 168. */
    Ext->IntChars.InterruptType = NDIS_CONNECT_LINE_BASED;

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
