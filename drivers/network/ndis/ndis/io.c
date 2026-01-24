/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/io.c
 * PURPOSE:     I/O related routines
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Vizzini (vizzini@plasmic.com)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 *   20 Aug 2003 Vizzini - DMA support
 *   3  Oct 2003 Vizzini - Formatting and minor bugfixes
 */

#include "ndissys.h"

VOID NTAPI HandleDeferredProcessing(
    IN  PKDPC   Dpc,
    IN  PVOID   DeferredContext,
    IN  PVOID   SystemArgument1,
    IN  PVOID   SystemArgument2)
/*
 * FUNCTION: Deferred interrupt processing routine
 * ARGUMENTS:
 *     Dpc             = Pointer to DPC object
 *     DeferredContext = Pointer to context information (LOGICAL_ADAPTER)
 *     SystemArgument1 = Unused
 *     SystemArgument2 = Unused
 */
{
  PLOGICAL_ADAPTER Adapter = GET_LOGICAL_ADAPTER(DeferredContext);

  DPRINT("Called.\n");

  ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

  /* Call the deferred interrupt service handler for this adapter */
  (*Adapter->NdisMiniportBlock.DriverHandle->MiniportCharacteristics.HandleInterruptHandler)(
      Adapter->NdisMiniportBlock.MiniportAdapterContext);

  /* re-enable the interrupt */
  DPRINT("re-enabling the interrupt\n");
  if(Adapter->NdisMiniportBlock.DriverHandle->MiniportCharacteristics.EnableInterruptHandler)
    (*Adapter->NdisMiniportBlock.DriverHandle->MiniportCharacteristics.EnableInterruptHandler)(
        Adapter->NdisMiniportBlock.MiniportAdapterContext);

  DPRINT("Leaving.\n");
}

BOOLEAN NTAPI ServiceRoutine(
    IN  PKINTERRUPT Interrupt,
    IN  PVOID       ServiceContext)
/*
 * FUNCTION: Interrupt service routine
 * ARGUMENTS:
 *     Interrupt      = Pointer to interrupt object
 *     ServiceContext = Pointer to context information (PNDIS_MINIPORT_INTERRUPT)
 * RETURNS
 *     TRUE if a miniport controlled device generated the interrupt
 */
{
  BOOLEAN InterruptRecognized = FALSE;
  BOOLEAN QueueMiniportHandleInterrupt = FALSE;
  PNDIS_MINIPORT_INTERRUPT NdisInterrupt = ServiceContext;
  PNDIS_MINIPORT_BLOCK NdisMiniportBlock = NdisInterrupt->Miniport;
  BOOLEAN Initializing;

  DPRINT("Called. Interrupt (0x%X)\n", NdisInterrupt);

  /* Certain behavior differs if MiniportInitialize is executing when the interrupt is generated */
  Initializing = (NdisMiniportBlock->PnPDeviceState != NdisPnPDeviceStarted);
  DPRINT1("MiniportInitialize executing: %s\n", (Initializing ? "yes" : "no"));

  /* MiniportISR is always called for interrupts during MiniportInitialize */
  if ((Initializing) || (NdisInterrupt->IsrRequested) || (NdisInterrupt->SharedInterrupt)) {
      DPRINT1("Calling MiniportISR\n");
      (*NdisMiniportBlock->DriverHandle->MiniportCharacteristics.ISRHandler)(
          &InterruptRecognized,
          &QueueMiniportHandleInterrupt,
          NdisMiniportBlock->MiniportAdapterContext);

  } else if (NdisMiniportBlock->DriverHandle->MiniportCharacteristics.DisableInterruptHandler) {
      DPRINT("Calling MiniportDisableInterrupt\n");
      (*NdisMiniportBlock->DriverHandle->MiniportCharacteristics.DisableInterruptHandler)(
          NdisMiniportBlock->MiniportAdapterContext);
       QueueMiniportHandleInterrupt = TRUE;
       InterruptRecognized = TRUE;
  }

  /* TODO: Figure out if we should call this or not if Initializing is true. It appears
   * that calling it fixes some NICs, but documentation is contradictory on it.  */
  if (QueueMiniportHandleInterrupt)
  {
      DPRINT1("Queuing DPC.\n");
      KeInsertQueueDpc(&NdisInterrupt->InterruptDpc, NULL, NULL);
  }

  DPRINT("Leaving.\n");

  return InterruptRecognized;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisImmediateReadPortUchar(
    IN  NDIS_HANDLE WrapperConfigurationContext,
    IN  ULONG       Port,
    OUT PUCHAR      Data)
{
  DPRINT("Called.\n");
  *Data = READ_PORT_UCHAR(UlongToPtr(Port)); // FIXME: What to do with WrapperConfigurationContext?
}

/*
 * @implemented
 */
VOID
EXPORT
NdisImmediateReadPortUlong(
    IN  NDIS_HANDLE WrapperConfigurationContext,
    IN  ULONG       Port,
    OUT PULONG      Data)
{
  DPRINT("Called.\n");
  *Data = READ_PORT_ULONG(UlongToPtr(Port)); // FIXME: What to do with WrapperConfigurationContext?
}

/*
 * @implemented
 */
VOID
EXPORT
NdisImmediateReadPortUshort(
    IN  NDIS_HANDLE WrapperConfigurationContext,
    IN  ULONG       Port,
    OUT PUSHORT     Data)
{
  DPRINT("Called.\n");
  *Data = READ_PORT_USHORT(UlongToPtr(Port)); // FIXME: What to do with WrapperConfigurationContext?
}

/*
 * @implemented
 */
VOID
EXPORT
NdisImmediateWritePortUchar(
    IN  NDIS_HANDLE WrapperConfigurationContext,
    IN  ULONG       Port,
    IN  UCHAR       Data)
{
  DPRINT("Called.\n");
  WRITE_PORT_UCHAR(UlongToPtr(Port), Data); // FIXME: What to do with WrapperConfigurationContext?
}

/*
 * @implemented
 */
VOID
EXPORT
NdisImmediateWritePortUlong(
    IN  NDIS_HANDLE WrapperConfigurationContext,
    IN  ULONG       Port,
    IN  ULONG       Data)
{
  DPRINT("Called.\n");
  WRITE_PORT_ULONG(UlongToPtr(Port), Data); // FIXME: What to do with WrapperConfigurationContext?
}

/*
 * @implemented
 */
VOID
EXPORT
NdisImmediateWritePortUshort(
    IN  NDIS_HANDLE WrapperConfigurationContext,
    IN  ULONG       Port,
    IN  USHORT      Data)
{
  DPRINT("Called.\n");
  WRITE_PORT_USHORT(UlongToPtr(Port), Data); // FIXME: What to do with WrapperConfigurationContext?
}

IO_ALLOCATION_ACTION NTAPI NdisSubordinateMapRegisterCallback (
    IN PDEVICE_OBJECT  DeviceObject,
    IN PIRP            Irp,
    IN PVOID           MapRegisterBase,
    IN PVOID           Context)
/*
 * FUNCTION: Called back during reservation of map registers
 * ARGUMENTS:
 *     DeviceObject: Device object of the device setting up DMA
 *     Irp: Reserved; must be ignored
 *     MapRegisterBase: Map registers assigned for transfer
 *     Context: LOGICAL_ADAPTER object of the requesting miniport
 * NOTES:
 *     - Called at IRQL = DISPATCH_LEVEL
 */
{
    PNDIS_DMA_BLOCK DmaBlock = Context;

    DPRINT("Called.\n");

    DmaBlock->MapRegisterBase = MapRegisterBase;

    DPRINT("setting event and leaving.\n");

    KeSetEvent(&DmaBlock->AllocationEvent, 0, FALSE);

    /* We have to hold the object open to keep our lock on the system DMA controller */
    return KeepObject;
}

IO_ALLOCATION_ACTION NTAPI NdisBusMasterMapRegisterCallback (
    IN PDEVICE_OBJECT  DeviceObject,
    IN PIRP            Irp,
    IN PVOID           MapRegisterBase,
    IN PVOID           Context)
/*
 * FUNCTION: Called back during reservation of map registers
 * ARGUMENTS:
 *     DeviceObject: Device object of the device setting up DMA
 *     Irp: Reserved; must be ignored
 *     MapRegisterBase: Map registers assigned for transfer
 *     Context: LOGICAL_ADAPTER object of the requesting miniport
 * NOTES:
 *     - Called once per BaseMapRegister (see NdisMAllocateMapRegisters)
 *     - Called at IRQL = DISPATCH_LEVEL
 */
{
  PLOGICAL_ADAPTER Adapter = Context;

  DPRINT("Called.\n");

  Adapter->NdisMiniportBlock.MapRegisters[Adapter->NdisMiniportBlock.CurrentMapRegister].MapRegister = MapRegisterBase;

  DPRINT("setting event and leaving.\n");

  KeSetEvent(Adapter->NdisMiniportBlock.AllocationEvent, 0, FALSE);

  /* We're a bus master so we can go ahead and deallocate the object now */
  return DeallocateObjectKeepRegisters;
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMAllocateMapRegisters(
    IN  NDIS_HANDLE   MiniportAdapterHandle,
    IN  UINT          DmaChannel,
    IN  NDIS_DMA_SIZE DmaSize,
    IN  ULONG         BaseMapRegistersNeeded,
    IN  ULONG         MaximumBufferSize)
/*
 * FUNCTION: Allocate map registers for use in DMA transfers
 * ARGUMENTS:
 *     MiniportAdapterHandle: Passed in to MiniportInitialize
 *     DmaChannel: DMA channel to use
 *     DmaSize: bit width of DMA transfers
 *     BaseMapRegistersNeeded: number of base map registers requested
 *     MaximumBufferSize: largest single buffer transferred
 * RETURNS:
 *     NDIS_STATUS_SUCCESS on success
 *     NDIS_STATUS_RESOURCES on failure
 * NOTES:
 *     - the win2k ddk and the nt4 ddk have conflicting prototypes for this.
 *       I'm implementing the 2k one.
 *     - do not confuse a "base map register" with a "map register" - they
 *       are different.  Only NDIS seems to use the base concept.  The idea
 *       is that a miniport supplies the number of base map registers it will
 *       need, which is equal to the number of DMA send buffers it manages.
 *       NDIS then allocates a number of map registers to go with each base
 *       map register, so that a driver just has to send the base map register
 *       number during dma operations and NDIS can find the group of real
 *       map registers that represent the transfer.
 *     - Because of the above sillyness, you can only specify a few base map
 *       registers at most.  a 1514-byte packet is two map registers at 4k
 *       page size.
 *     - NDIS limits the total number of allocated map registers to 64,
 *       which (in the case of the above example) limits the number of base
 *       map registers to 32.
 */
{
  DEVICE_DESCRIPTION   Description;
  PDMA_ADAPTER         AdapterObject = 0;
  UINT                 MapRegistersPerBaseRegister = 0;
  ULONG                AvailableMapRegisters;
  NTSTATUS             NtStatus;
  PLOGICAL_ADAPTER     Adapter;
  PDEVICE_OBJECT       DeviceObject = 0;
  KEVENT               AllocationEvent;
  KIRQL                OldIrql;

  DPRINT("called: Handle 0x%x, DmaChannel 0x%x, DmaSize 0x%x, BaseMapRegsNeeded: 0x%x, MaxBuffer: 0x%x.\n",
                            MiniportAdapterHandle, DmaChannel, DmaSize, BaseMapRegistersNeeded, MaximumBufferSize);

  memset(&Description,0,sizeof(Description));

  Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;

  ASSERT(Adapter);

  /* only bus masters may call this routine */
  if(!(Adapter->NdisMiniportBlock.Flags & NDIS_ATTRIBUTE_BUS_MASTER)) {
    DPRINT1("Not a bus master\n");
    return NDIS_STATUS_NOT_SUPPORTED;
  }

  DeviceObject = Adapter->NdisMiniportBlock.DeviceObject;

  KeInitializeEvent(&AllocationEvent, NotificationEvent, FALSE);
  Adapter->NdisMiniportBlock.AllocationEvent = &AllocationEvent;

  /*
  * map registers correlate to physical pages.  ndis documents a
  * maximum of 64 map registers that it will return.
  * at 4k pages, a 1514-byte buffer can span not more than 2 pages.
  *
  * the number of registers required for a given physical mapping
  * is (first register + last register + one per page size),
  * given that physical mapping is > 2.
  */

  /* unhandled corner case: {1,2}-byte max buffer size */
  ASSERT(MaximumBufferSize > 2);
  MapRegistersPerBaseRegister = ((MaximumBufferSize-2) / (2*PAGE_SIZE)) + 2;

  Description.Version = DEVICE_DESCRIPTION_VERSION;
  Description.Master = TRUE;                         /* implied by calling this function */
  Description.ScatterGather = TRUE;                  /* XXX UNTRUE: All BM DMA are S/G (ms seems to do this) */
  Description.BusNumber = Adapter->NdisMiniportBlock.BusNumber;
  Description.InterfaceType = (INTERFACE_TYPE)Adapter->NdisMiniportBlock.BusType;
  Description.DmaChannel = DmaChannel;
  Description.MaximumLength = MaximumBufferSize;

  if(DmaSize == NDIS_DMA_64BITS)
    Description.Dma64BitAddresses = TRUE;
  else if(DmaSize == NDIS_DMA_32BITS)
    Description.Dma32BitAddresses = TRUE;

  AdapterObject = IoGetDmaAdapter(
    Adapter->NdisMiniportBlock.PhysicalDeviceObject, &Description, &AvailableMapRegisters);

  if(!AdapterObject)
    {
      DPRINT1("Unable to allocate an adapter object; bailing out\n");
      return NDIS_STATUS_RESOURCES;
    }

  Adapter->NdisMiniportBlock.SystemAdapterObject = AdapterObject;

  if(AvailableMapRegisters < MapRegistersPerBaseRegister)
    {
      DPRINT1("Didn't get enough map registers from hal - requested 0x%x, got 0x%x\n",
          MapRegistersPerBaseRegister, AvailableMapRegisters);

      AdapterObject->DmaOperations->PutDmaAdapter(AdapterObject);
      Adapter->NdisMiniportBlock.SystemAdapterObject = NULL;
      return NDIS_STATUS_RESOURCES;
    }

  /* allocate & zero space in the miniport block for the registers */
  Adapter->NdisMiniportBlock.MapRegisters = ExAllocatePool(NonPagedPool, BaseMapRegistersNeeded * sizeof(MAP_REGISTER_ENTRY));
  if(!Adapter->NdisMiniportBlock.MapRegisters)
    {
      DPRINT1("insufficient resources.\n");
      AdapterObject->DmaOperations->PutDmaAdapter(AdapterObject);
      Adapter->NdisMiniportBlock.SystemAdapterObject = NULL;
      return NDIS_STATUS_RESOURCES;
    }

  memset(Adapter->NdisMiniportBlock.MapRegisters, 0, BaseMapRegistersNeeded * sizeof(MAP_REGISTER_ENTRY));
  Adapter->NdisMiniportBlock.BaseMapRegistersNeeded = (USHORT)BaseMapRegistersNeeded;

  while(BaseMapRegistersNeeded)
    {
      DPRINT1("iterating, basemapregistersneeded = %d\n", BaseMapRegistersNeeded);

      BaseMapRegistersNeeded--;
      Adapter->NdisMiniportBlock.CurrentMapRegister = (USHORT)BaseMapRegistersNeeded;
      KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        {
          NtStatus = AdapterObject->DmaOperations->AllocateAdapterChannel(
              AdapterObject, DeviceObject, MapRegistersPerBaseRegister,
              NdisBusMasterMapRegisterCallback, Adapter);
        }
      KeLowerIrql(OldIrql);

      if(!NT_SUCCESS(NtStatus))
        {
          DPRINT1("IoAllocateAdapterChannel failed: 0x%x\n", NtStatus);
          ExFreePool(Adapter->NdisMiniportBlock.MapRegisters);
          AdapterObject->DmaOperations->PutDmaAdapter(AdapterObject);
          Adapter->NdisMiniportBlock.CurrentMapRegister = Adapter->NdisMiniportBlock.BaseMapRegistersNeeded = 0;
          Adapter->NdisMiniportBlock.SystemAdapterObject = NULL;
          return NDIS_STATUS_RESOURCES;
        }

      DPRINT1("waiting on event\n");

      NtStatus = KeWaitForSingleObject(&AllocationEvent, Executive, KernelMode, FALSE, 0);

      if(!NT_SUCCESS(NtStatus))
        {
          DPRINT1("KeWaitForSingleObject failed: 0x%x\n", NtStatus);
          ExFreePool(Adapter->NdisMiniportBlock.MapRegisters);
          AdapterObject->DmaOperations->PutDmaAdapter(AdapterObject);
          Adapter->NdisMiniportBlock.CurrentMapRegister = Adapter->NdisMiniportBlock.BaseMapRegistersNeeded = 0;
          Adapter->NdisMiniportBlock.SystemAdapterObject = NULL;
          return NDIS_STATUS_RESOURCES;
        }

      DPRINT1("resetting event\n");

      KeClearEvent(&AllocationEvent);
    }

  DPRINT("returning success\n");
  return NDIS_STATUS_SUCCESS;
}


/*
 * @implemented
 */
VOID
EXPORT
NdisMSetupDmaTransfer(OUT PNDIS_STATUS Status,
                      IN NDIS_HANDLE MiniportDmaHandle,
                      IN PNDIS_BUFFER Buffer,
                      IN ULONG Offset,
                      IN ULONG Length,
                      IN BOOLEAN WriteToDevice)
{
    PNDIS_DMA_BLOCK DmaBlock = MiniportDmaHandle;
    NTSTATUS NtStatus;
    PLOGICAL_ADAPTER Adapter;
    KIRQL OldIrql;
    PDMA_ADAPTER AdapterObject;
    ULONG MapRegistersNeeded;

    DPRINT("called: Handle 0x%x, Buffer 0x%x, Offset 0x%x, Length 0x%x, WriteToDevice 0x%x\n",
                              MiniportDmaHandle, Buffer, Offset, Length, WriteToDevice);

    Adapter = (PLOGICAL_ADAPTER)DmaBlock->Miniport;
    AdapterObject = (PDMA_ADAPTER)DmaBlock->SystemAdapterObject;

    MapRegistersNeeded = (Length + (PAGE_SIZE - 1)) / PAGE_SIZE;

    KeFlushIoBuffers(Buffer, !WriteToDevice, TRUE);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    {
        NtStatus = AdapterObject->DmaOperations->AllocateAdapterChannel(AdapterObject,
                                                                        Adapter->NdisMiniportBlock.PhysicalDeviceObject,
                                                                        MapRegistersNeeded,
                                                                        NdisSubordinateMapRegisterCallback, Adapter);
    }
    KeLowerIrql(OldIrql);

    if(!NT_SUCCESS(NtStatus))
    {
        DPRINT1("AllocateAdapterChannel failed: 0x%x\n", NtStatus);
        AdapterObject->DmaOperations->FreeAdapterChannel(AdapterObject);
        *Status = NDIS_STATUS_RESOURCES;
        return;
    }

    NtStatus = KeWaitForSingleObject(&DmaBlock->AllocationEvent, Executive, KernelMode, FALSE, 0);

    if(!NT_SUCCESS(NtStatus))
    {
        DPRINT1("KeWaitForSingleObject failed: 0x%x\n", NtStatus);
        AdapterObject->DmaOperations->FreeAdapterChannel(AdapterObject);
        *Status = NDIS_STATUS_RESOURCES;
        return;
    }

    /* We must throw away the return value of MapTransfer for a system DMA device */
    AdapterObject->DmaOperations->MapTransfer(AdapterObject, Buffer,
                                              DmaBlock->MapRegisterBase,
                                              (PUCHAR)MmGetMdlVirtualAddress(Buffer) + Offset, &Length, WriteToDevice);

    DPRINT("returning success\n");
    *Status = NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisSetupDmaTransfer(OUT PNDIS_STATUS    Status,
                     IN  PNDIS_HANDLE    NdisDmaHandle,
                     IN  PNDIS_BUFFER    Buffer,
                     IN  ULONG           Offset,
                     IN  ULONG           Length,
                     IN  BOOLEAN         WriteToDevice)
/*
 * FUNCTION:
 * ARGUMENTS:
 * NOTES:
 *    NDIS 4.0
 */
{
    NdisMSetupDmaTransfer(Status,
                          NdisDmaHandle,
                          Buffer,
                          Offset,
                          Length,
                          WriteToDevice);
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMCompleteDmaTransfer(OUT PNDIS_STATUS Status,
                         IN NDIS_HANDLE MiniportDmaHandle,
                         IN PNDIS_BUFFER Buffer,
                         IN ULONG Offset,
                         IN ULONG Length,
                         IN BOOLEAN WriteToDevice)
{
    PNDIS_DMA_BLOCK DmaBlock = MiniportDmaHandle;
    PDMA_ADAPTER AdapterObject = (PDMA_ADAPTER)DmaBlock->SystemAdapterObject;

    DPRINT("called: Handle 0x%x, Buffer 0x%x, Offset 0x%x, Length 0x%x, WriteToDevice 0x%x\n",
                              MiniportDmaHandle, Buffer, Offset, Length, WriteToDevice);

    if (!AdapterObject->DmaOperations->FlushAdapterBuffers(AdapterObject,
                                                           Buffer,
                                                           DmaBlock->MapRegisterBase,
                                                           (PUCHAR)MmGetMdlVirtualAddress(Buffer) + Offset,
                                                           Length,
                                                           WriteToDevice))
    {
        DPRINT1("FlushAdapterBuffers failed\n");
        *Status = NDIS_STATUS_FAILURE;
        return;
    }

    AdapterObject->DmaOperations->FreeAdapterChannel(AdapterObject);

    DPRINT("returning success\n");
    *Status = NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisCompleteDmaTransfer(OUT PNDIS_STATUS    Status,
                        IN  PNDIS_HANDLE    NdisDmaHandle,
                        IN  PNDIS_BUFFER    Buffer,
                        IN  ULONG           Offset,
                        IN  ULONG           Length,
                        IN  BOOLEAN         WriteToDevice)
{
    NdisMCompleteDmaTransfer(Status,
                             NdisDmaHandle,
                             Buffer,
                             Offset,
                             Length,
                             WriteToDevice);
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMStartBufferPhysicalMapping(
    IN  NDIS_HANDLE                 MiniportAdapterHandle,
    IN  PNDIS_BUFFER                Buffer,
    IN  ULONG                       PhysicalMapRegister,
    IN  BOOLEAN                     WriteToDevice,
    OUT PNDIS_PHYSICAL_ADDRESS_UNIT	PhysicalAddressArray,
    OUT PUINT                       ArraySize)
/*
 * FUNCTION: Sets up map registers for a bus-master DMA transfer
 * ARGUMENTS:
 *     MiniportAdapterHandle: handle originally input to MiniportInitialize
 *     Buffer: data to be transferred
 *     PhysicalMapRegister: specifies the map register to set up
 *     WriteToDevice: if true, data is being written to the device; else it is being read
 *     PhysicalAddressArray: list of mapped ranges suitable for DMA with the device
 *     ArraySize: number of elements in PhysicalAddressArray
 * NOTES:
 *     - Must be called at IRQL <= DISPATCH_LEVEL
 *     - The basic idea:  call IoMapTransfer() in a loop as many times as it takes
 *       in order to map all of the virtual memory to physical memory readable
 *       by the device
 *     - The caller supplies storage for the physical address array.
 */
{
  PLOGICAL_ADAPTER Adapter;
  PVOID CurrentVa;
  ULONG TotalLength;
  PHYSICAL_ADDRESS ReturnedAddress;
  UINT LoopCount =  0;

  ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
  ASSERT(MiniportAdapterHandle && Buffer && PhysicalAddressArray && ArraySize);

  Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;
  CurrentVa = MmGetMdlVirtualAddress(Buffer);
  TotalLength = MmGetMdlByteCount(Buffer);

  while(TotalLength)
    {
      ULONG Length = TotalLength;

      ReturnedAddress = Adapter->NdisMiniportBlock.SystemAdapterObject->DmaOperations->MapTransfer(
          Adapter->NdisMiniportBlock.SystemAdapterObject, Buffer,
          Adapter->NdisMiniportBlock.MapRegisters[PhysicalMapRegister].MapRegister,
          CurrentVa, &Length, WriteToDevice);

      Adapter->NdisMiniportBlock.MapRegisters[PhysicalMapRegister].WriteToDevice = WriteToDevice;

      PhysicalAddressArray[LoopCount].PhysicalAddress = ReturnedAddress;
      PhysicalAddressArray[LoopCount].Length = Length;

      TotalLength -= Length;
      CurrentVa = (PVOID)((ULONG_PTR)CurrentVa + Length);

      LoopCount++;
    }

  *ArraySize = LoopCount;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMCompleteBufferPhysicalMapping(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  PNDIS_BUFFER    Buffer,
    IN  ULONG           PhysicalMapRegister)
/*
 * FUNCTION: Complete dma action started by NdisMStartBufferPhysicalMapping
 * ARGUMENTS:
 *     - MiniportAdapterHandle: handle originally input to MiniportInitialize
 *     - Buffer: NDIS_BUFFER to complete the mapping on
 *     - PhysicalMapRegister: the chosen map register
 * NOTES:
 *     - May be called at IRQL <= DISPATCH_LEVEL
 */
{
  PLOGICAL_ADAPTER Adapter;
  VOID *CurrentVa;
  ULONG Length;

  ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
  ASSERT(MiniportAdapterHandle && Buffer);

  Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;
  CurrentVa = MmGetMdlVirtualAddress(Buffer);
  Length = MmGetMdlByteCount(Buffer);

  Adapter->NdisMiniportBlock.SystemAdapterObject->DmaOperations->FlushAdapterBuffers(
      Adapter->NdisMiniportBlock.SystemAdapterObject, Buffer,
      Adapter->NdisMiniportBlock.MapRegisters[PhysicalMapRegister].MapRegister,
      CurrentVa, Length,
      Adapter->NdisMiniportBlock.MapRegisters[PhysicalMapRegister].WriteToDevice);
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMDeregisterDmaChannel(
    IN  NDIS_HANDLE    MiniportDmaHandle)
{
    PNDIS_DMA_BLOCK DmaBlock = MiniportDmaHandle;
    PDMA_ADAPTER AdapterObject = (PDMA_ADAPTER)DmaBlock->SystemAdapterObject;

    if (AdapterObject == ((PLOGICAL_ADAPTER)DmaBlock->Miniport)->NdisMiniportBlock.SystemAdapterObject)
        ((PLOGICAL_ADAPTER)DmaBlock->Miniport)->NdisMiniportBlock.SystemAdapterObject = NULL;

    AdapterObject->DmaOperations->PutDmaAdapter(AdapterObject);

    ExFreePool(DmaBlock);
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMDeregisterInterrupt(
    IN  PNDIS_MINIPORT_INTERRUPT    Interrupt)
/*
 * FUNCTION: Releases an interrupt vector
 * ARGUMENTS:
 *     Interrupt = Pointer to interrupt object
 */
{
    DPRINT("Called.\n");
    IoDisconnectInterrupt(Interrupt->InterruptObject);
    Interrupt->Miniport->RegisteredInterrupts--;

    if (Interrupt->Miniport->Interrupt == Interrupt)
        Interrupt->Miniport->Interrupt = NULL;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMFreeMapRegisters(
    IN  NDIS_HANDLE MiniportAdapterHandle)
/*
 * FUNCTION: Free previously allocated map registers
 * ARGUMENTS:
 *     MiniportAdapterHandle:  Handle originally passed in to MiniportInitialize
 * NOTES:
 */
{
  KIRQL                OldIrql;
  PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;
  PDMA_ADAPTER         AdapterObject;
  UINT                 MapRegistersPerBaseRegister;
  UINT                 i;

  DPRINT("Called.\n");

  ASSERT(Adapter);

  /* only bus masters may call this routine */
  if(!(Adapter->NdisMiniportBlock.Flags & NDIS_ATTRIBUTE_BUS_MASTER) ||
     Adapter->NdisMiniportBlock.SystemAdapterObject == NULL) {
     DPRINT1("Not bus master or bad adapter object\n");
    return;
  }

  MapRegistersPerBaseRegister = ((Adapter->NdisMiniportBlock.MaximumPhysicalMapping - 2) / PAGE_SIZE) + 2;

  AdapterObject = Adapter->NdisMiniportBlock.SystemAdapterObject;

  KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    {
      for(i = 0; i < Adapter->NdisMiniportBlock.BaseMapRegistersNeeded; i++)
        {
          AdapterObject->DmaOperations->FreeMapRegisters(
              Adapter->NdisMiniportBlock.SystemAdapterObject,
              Adapter->NdisMiniportBlock.MapRegisters[i].MapRegister,
              MapRegistersPerBaseRegister);
        }
    }
 KeLowerIrql(OldIrql);

 AdapterObject->DmaOperations->PutDmaAdapter(AdapterObject);
 Adapter->NdisMiniportBlock.SystemAdapterObject = NULL;

 ExFreePool(Adapter->NdisMiniportBlock.MapRegisters);
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMMapIoSpace(
    OUT PVOID                   *VirtualAddress,
    IN  NDIS_HANDLE             MiniportAdapterHandle,
    IN  NDIS_PHYSICAL_ADDRESS   PhysicalAddress,
    IN  UINT                    Length)
/*
 * FUNCTION: Maps a bus-relative address to a system-wide virtual address
 * ARGUMENTS:
 *     VirtualAddress: receives virtual address of mapping
 *     MiniportAdapterHandle: Handle originally input to MiniportInitialize
 *     PhysicalAddress: bus-relative address to map
 *     Length: Number of bytes to map
 * RETURNS:
 *     NDIS_STATUS_SUCCESS: the operation completed successfully
 *     NDIS_STATUS_RESOURCE_CONFLICT: the physical address range is already claimed
 *     NDIS_STATUS_RESOURCES: insufficient resources to complete the mapping
 *     NDIS_STATUS_FAILURE: a general failure has occured
 * NOTES:
 *     - Must be called at IRQL = PASSIVE_LEVEL
 */
{
  PLOGICAL_ADAPTER Adapter = MiniportAdapterHandle;
  ULONG AddressSpace = 0; /* Memory Space */
  NDIS_PHYSICAL_ADDRESS TranslatedAddress;

  PAGED_CODE();
  ASSERT(VirtualAddress && MiniportAdapterHandle);

  DPRINT("Called\n");

  if(!HalTranslateBusAddress((INTERFACE_TYPE)Adapter->NdisMiniportBlock.BusType, Adapter->NdisMiniportBlock.BusNumber,
                             PhysicalAddress, &AddressSpace, &TranslatedAddress))
  {
      DPRINT1("Unable to translate address\n");
      return NDIS_STATUS_RESOURCES;
  }

  *VirtualAddress = MmMapIoSpace(TranslatedAddress, Length, MmNonCached);

  if(!*VirtualAddress) {
    DPRINT1("MmMapIoSpace failed\n");
    return NDIS_STATUS_RESOURCES;
  }

  return NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
ULONG
EXPORT
NdisMReadDmaCounter(
    IN  NDIS_HANDLE MiniportDmaHandle)
{
  /* NOTE: Unlike NdisMGetDmaAlignment() below, this is a handle to the DMA block */
  PNDIS_DMA_BLOCK DmaBlock = MiniportDmaHandle;
  PDMA_ADAPTER AdapterObject = (PDMA_ADAPTER)DmaBlock->SystemAdapterObject;

  DPRINT("Called.\n");

  return AdapterObject->DmaOperations->ReadDmaCounter(AdapterObject);
}

/*
 * @implemented
 */
ULONG
EXPORT
NdisMGetDmaAlignment(
    IN  NDIS_HANDLE MiniportAdapterHandle)
{
  /* NOTE: Unlike NdisMReadDmaCounter() above, this is a handle to the NDIS miniport block */
  PLOGICAL_ADAPTER Adapter = MiniportAdapterHandle;
  PDMA_ADAPTER AdapterObject = (PDMA_ADAPTER)Adapter->NdisMiniportBlock.SystemAdapterObject;

  DPRINT("Called.\n");

  return AdapterObject->DmaOperations->GetDmaAlignment(AdapterObject);
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMRegisterDmaChannel(
    OUT PNDIS_HANDLE            MiniportDmaHandle,
    IN  NDIS_HANDLE             MiniportAdapterHandle,
    IN  UINT                    DmaChannel,
    IN  BOOLEAN                 Dma32BitAddresses,
    IN  PNDIS_DMA_DESCRIPTION   DmaDescription,
    IN  ULONG                   MaximumLength)
{
  PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;
  DEVICE_DESCRIPTION DeviceDesc;
  ULONG MapRegisters;
  PNDIS_DMA_BLOCK DmaBlock;

  DPRINT("Called.\n");

  RtlZeroMemory(&DeviceDesc, sizeof(DEVICE_DESCRIPTION));

  DeviceDesc.Version = DEVICE_DESCRIPTION_VERSION;
  DeviceDesc.Master = (Adapter->NdisMiniportBlock.Flags & NDIS_ATTRIBUTE_BUS_MASTER);
  DeviceDesc.ScatterGather = FALSE;
  DeviceDesc.DemandMode = DmaDescription->DemandMode;
  DeviceDesc.AutoInitialize = DmaDescription->AutoInitialize;
  DeviceDesc.Dma32BitAddresses = Dma32BitAddresses;
  DeviceDesc.Dma64BitAddresses = FALSE;
  DeviceDesc.BusNumber = Adapter->NdisMiniportBlock.BusNumber;
  DeviceDesc.DmaChannel = DmaDescription->DmaChannel;
  DeviceDesc.InterfaceType = (INTERFACE_TYPE)Adapter->NdisMiniportBlock.BusType;
  DeviceDesc.DmaWidth = DmaDescription->DmaWidth;
  DeviceDesc.DmaSpeed = DmaDescription->DmaSpeed;
  DeviceDesc.MaximumLength = MaximumLength;


  DmaBlock = ExAllocatePool(NonPagedPool, sizeof(NDIS_DMA_BLOCK));
  if (!DmaBlock) {
      DPRINT1("Insufficient resources\n");
      return NDIS_STATUS_RESOURCES;
  }

  DmaBlock->SystemAdapterObject = (PVOID)IoGetDmaAdapter(Adapter->NdisMiniportBlock.PhysicalDeviceObject, &DeviceDesc, &MapRegisters);

  if (!DmaBlock->SystemAdapterObject) {
      DPRINT1("Insufficient resources\n");
      ExFreePool(DmaBlock);
      return NDIS_STATUS_RESOURCES;
  }

  Adapter->NdisMiniportBlock.SystemAdapterObject = (PDMA_ADAPTER)DmaBlock->SystemAdapterObject;

  KeInitializeEvent(&DmaBlock->AllocationEvent, NotificationEvent, FALSE);

  DmaBlock->Miniport = Adapter;

  *MiniportDmaHandle = DmaBlock;

  return NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisAllocateDmaChannel(OUT PNDIS_STATUS            Status,
                       OUT PNDIS_HANDLE            NdisDmaHandle,
                       IN  NDIS_HANDLE             NdisAdapterHandle,
                       IN  PNDIS_DMA_DESCRIPTION   DmaDescription,
                       IN  ULONG                   MaximumLength)
{
    *Status = NdisMRegisterDmaChannel(NdisDmaHandle,
                                      NdisAdapterHandle,
                                      0,
                                      FALSE,
                                      DmaDescription,
                                      MaximumLength);
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMRegisterInterrupt(
    OUT PNDIS_MINIPORT_INTERRUPT    Interrupt,
    IN  NDIS_HANDLE                 MiniportAdapterHandle,
    IN  UINT                        InterruptVector,
    IN  UINT                        InterruptLevel,
    IN  BOOLEAN	                    RequestIsr,
    IN  BOOLEAN                     SharedInterrupt,
    IN  NDIS_INTERRUPT_MODE         InterruptMode)
/*
 * FUNCTION: Claims access to an interrupt vector
 * ARGUMENTS:
 *     Interrupt             = Address of interrupt object to initialize
 *     MiniportAdapterHandle = Specifies handle input to MiniportInitialize
 *     InterruptVector       = Specifies bus-relative vector to register
 *     InterruptLevel        = Specifies bus-relative DIRQL vector for interrupt
 *     RequestIsr            = TRUE if MiniportISR should always be called
 *     SharedInterrupt       = TRUE if other devices may use the same interrupt
 *     InterruptMode         = Specifies type of interrupt
 * RETURNS:
 *     Status of operation
 */
{
  NTSTATUS Status;
  ULONG MappedIRQ;
  KIRQL DIrql;
  KAFFINITY Affinity;
  PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;

  DPRINT("Called. InterruptVector (0x%X)  InterruptLevel (0x%X)  "
      "SharedInterrupt (%d)  InterruptMode (0x%X)\n",
      InterruptVector, InterruptLevel, SharedInterrupt, InterruptMode);

  /*
   * Debug logging for MSI investigation.
   * The Vector and Level values from the driver come from the CM_PARTIAL_RESOURCE_DESCRIPTOR.
   * For legacy interrupts, these are bus-relative values that need translation.
   * For MSI/MSI-X, these are already system vectors allocated by the IRQ arbiter.
   */
  DPRINT("NDIS: NdisMRegisterInterrupt entry\n");
  DPRINT("NDIS:   InterruptVector=%u (0x%x)\n", InterruptVector, InterruptVector);
  DPRINT("NDIS:   InterruptLevel=%u (0x%x)\n", InterruptLevel, InterruptLevel);
  DPRINT("NDIS:   SharedInterrupt=%d\n", SharedInterrupt);
  DPRINT("NDIS:   InterruptMode=%d (%s)\n", InterruptMode,
           (InterruptMode == NdisInterruptLatched) ? "Latched" : "Level");
  DPRINT1("NDIS:   BusType=%d BusNumber=%u\n",
           Adapter->NdisMiniportBlock.BusType, Adapter->NdisMiniportBlock.BusNumber);

  RtlZeroMemory(Interrupt, sizeof(NDIS_MINIPORT_INTERRUPT));

  KeInitializeSpinLock(&Interrupt->DpcCountLock);

  KeInitializeDpc(&Interrupt->InterruptDpc, HandleDeferredProcessing, Adapter);

  KeInitializeEvent(&Interrupt->DpcsCompletedEvent, NotificationEvent, FALSE);

  Interrupt->SharedInterrupt = SharedInterrupt;
  Interrupt->IsrRequested = RequestIsr;
  Interrupt->Miniport = &Adapter->NdisMiniportBlock;

  /*
   * For MSI/MSI-X interrupts, the InterruptVector already contains the
   * system interrupt vector allocated by the IRQ arbiter. We should NOT
   * call HalGetInterruptVector because that expects bus-relative values.
   *
   * Detect MSI by checking if the vector is in the MSI range (typically > 31
   * on x86/x64 since legacy IRQs are 0-23, and MSI vectors are allocated
   * from higher ranges).
   *
   * On x86/x64, interrupt vectors 0x30 and above are typically used for
   * device interrupts including MSI. If Level == Vector and both are >= 32,
   * this is likely an MSI vector that's already translated.
   *
   * Note: This is a heuristic. A proper solution would be to add a new
   * parameter to NdisMRegisterInterrupt to indicate MSI mode, but that
   * would break ABI compatibility.
   */
  if (InterruptLevel == InterruptVector && InterruptVector >= 32)
  {
      /*
       * This appears to be an MSI/MSI-X vector.
       * The vector is already the system interrupt vector.
       * Compute IRQL based on the vector number.
       *
       * On x86/x64, IRQL is derived from the vector:
       * - Vectors 0x30-0x3F -> IRQL 3
       * - Vectors 0x40-0x4F -> IRQL 4
       * ... and so on up to IRQL 26 (CLOCK_LEVEL-1)
       *
       * Formula: IRQL = (Vector >> 4)
       *
       * But we also need to ensure we stay within device IRQL range.
       * For MSI, use a fixed device IRQL in the safe range.
       */
      MappedIRQ = InterruptVector;
      DIrql = (KIRQL)((InterruptVector >> 4) & 0xF);

      /* Clamp IRQL to valid device range (DISPATCH_LEVEL+1 to CLOCK_LEVEL-1) */
      if (DIrql < DISPATCH_LEVEL + 1)
          DIrql = DISPATCH_LEVEL + 1;
      if (DIrql > CLOCK_LEVEL - 1)
          DIrql = CLOCK_LEVEL - 1;

      /* MSI targets the first processor by default */
      Affinity = 1;

      DPRINT("NDIS: MSI/MSI-X detected - using vector directly: MappedIRQ=%u DIrql=%u\n",
               MappedIRQ, (ULONG)DIrql);
  }
  else
  {
      /*
       * Legacy interrupt mode - call HalGetInterruptVector to translate
       * the bus-relative interrupt to a system interrupt vector.
       */
      DPRINT("NDIS: Calling HalGetInterruptVector(BusType=%d, BusNum=%u, Level=%u, Vector=%u)\n",
               (INTERFACE_TYPE)Adapter->NdisMiniportBlock.BusType,
               Adapter->NdisMiniportBlock.BusNumber,
               InterruptLevel, InterruptVector);

      MappedIRQ = HalGetInterruptVector((INTERFACE_TYPE)Adapter->NdisMiniportBlock.BusType, Adapter->NdisMiniportBlock.BusNumber,
                                        InterruptLevel, InterruptVector, &DIrql,
                                        &Affinity);

      DPRINT("NDIS: HalGetInterruptVector returned: MappedIRQ=%u (0x%x) DIrql=%u Affinity=0x%Ix\n",
               MappedIRQ, MappedIRQ, (ULONG)DIrql, (ULONG_PTR)Affinity);
  }

  DPRINT("Connecting to interrupt vector (0x%X)  Affinity (0x%X).\n", MappedIRQ, Affinity);

  DPRINT("NDIS: Calling IoConnectInterrupt(Vector=%u, DIrql=%u, Mode=%d, Shared=%d, Affinity=0x%Ix)\n",
           MappedIRQ, (ULONG)DIrql, InterruptMode, SharedInterrupt, (ULONG_PTR)Affinity);

  Status = IoConnectInterrupt(&Interrupt->InterruptObject, ServiceRoutine, Interrupt, &Interrupt->DpcCountLock, MappedIRQ,
      DIrql, DIrql, InterruptMode, SharedInterrupt, Affinity, FALSE);

  DPRINT("NDIS: IoConnectInterrupt returned Status=0x%08x\n", Status);

  DPRINT("Leaving. Status (0x%X).\n", Status);

  if (NT_SUCCESS(Status)) {
      Adapter->NdisMiniportBlock.Interrupt = Interrupt;
      Adapter->NdisMiniportBlock.RegisteredInterrupts++;
      return NDIS_STATUS_SUCCESS;
  }

  if (Status == STATUS_INSUFFICIENT_RESOURCES)
    {
        /* FIXME: Log error */
      DPRINT1("Resource conflict!\n");
      return NDIS_STATUS_RESOURCE_CONFLICT;
    }

  DPRINT1("Function failed. Status (0x%X).\n", Status);
  return NDIS_STATUS_FAILURE;
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMRegisterIoPortRange(
    OUT PVOID       *PortOffset,
    IN  NDIS_HANDLE MiniportAdapterHandle,
    IN  UINT        InitialPort,
    IN  UINT        NumberOfPorts)
/*
 * FUNCTION: Sets up driver access to device I/O ports
 * ARGUMENTS:
 *     PortOffset            = Address of buffer to place mapped base port address
 *     MiniportAdapterHandle = Specifies handle input to MiniportInitialize
 *     InitialPort           = Bus-relative base port address of a range to be mapped
 *     NumberOfPorts         = Specifies number of ports to be mapped
 * RETURNS:
 *     Status of operation
 */
{
  PHYSICAL_ADDRESS     PortAddress, TranslatedAddress;
  PLOGICAL_ADAPTER Adapter  = (PLOGICAL_ADAPTER)MiniportAdapterHandle;
  ULONG                AddressSpace = 1;    /* FIXME The HAL handles this wrong atm */

  *PortOffset = 0;

  DPRINT("Called - InitialPort 0x%x, NumberOfPorts 0x%x\n", InitialPort, NumberOfPorts);

  memset(&PortAddress, 0, sizeof(PortAddress));

  /*
   * FIXME: NDIS 5+ completely ignores the InitialPort parameter, but
   * we don't have a way to get the I/O base address yet (see
   * NDIS_MINIPORT_BLOCK->AllocatedResources and
   * NDIS_MINIPORT_BLOCK->AllocatedResourcesTranslated).
   */
  if(InitialPort)
      PortAddress = RtlConvertUlongToLargeInteger(InitialPort);
  else
      ASSERT(FALSE);

  DPRINT1("Translating address 0x%x 0x%x\n", PortAddress.u.HighPart, PortAddress.u.LowPart);

  if(!HalTranslateBusAddress((INTERFACE_TYPE)Adapter->NdisMiniportBlock.BusType, Adapter->NdisMiniportBlock.BusNumber,
                             PortAddress, &AddressSpace, &TranslatedAddress))
    {
      DPRINT1("Unable to translate address\n");
      return NDIS_STATUS_RESOURCES;
    }

  DPRINT1("Hal returned AddressSpace=0x%x TranslatedAddress=0x%x 0x%x\n",
                            AddressSpace, TranslatedAddress.u.HighPart, TranslatedAddress.u.LowPart);

  if(AddressSpace)
    {
      ASSERT(TranslatedAddress.u.HighPart == 0);
      *PortOffset = (PVOID)(ULONG_PTR)TranslatedAddress.QuadPart;
      DPRINT("Returning 0x%x\n", *PortOffset);
      return NDIS_STATUS_SUCCESS;
    }

  DPRINT1("calling MmMapIoSpace\n");

  *PortOffset = MmMapIoSpace(TranslatedAddress, NumberOfPorts, MmNonCached);
  DPRINT("Returning 0x%x for port range\n", *PortOffset);

  if(!*PortOffset) {
    DPRINT1("MmMapIoSpace failed\n");
    return NDIS_STATUS_RESOURCES;
  }

  return NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMDeregisterIoPortRange(IN  NDIS_HANDLE MiniportAdapterHandle,
                           IN  UINT        InitialPort,
                           IN  UINT        NumberOfPorts,
                           IN  PVOID       PortOffset)
/*
 * FUNCTION: Releases a register mapping to I/O ports
 * ARGUMENTS:
 *     MiniportAdapterHandle = Specifies handle input to MiniportInitialize
 *     InitialPort           = Bus-relative base port address of a range to be mapped
 *     NumberOfPorts         = Specifies number of ports to be mapped
 *     PortOffset            = Pointer to mapped base port address
 */
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;
    PHYSICAL_ADDRESS PortAddress = RtlConvertUlongToLargeInteger(InitialPort);
    PHYSICAL_ADDRESS TranslatedAddress;
    ULONG AddressSpace = 1;

    DPRINT("Called - InitialPort 0x%x, NumberOfPorts 0x%x, Port Offset 0x%x\n", InitialPort, NumberOfPorts, PortOffset);

    /* Translate the initial port again to find the address space of the translated address */
    if(!HalTranslateBusAddress((INTERFACE_TYPE)Adapter->NdisMiniportBlock.BusType, Adapter->NdisMiniportBlock.BusNumber,
                               PortAddress, &AddressSpace, &TranslatedAddress))
    {
        DPRINT1("Unable to translate address\n");
        return;
    }

    /* Make sure we got the same translation as last time */
    ASSERT(TranslatedAddress.QuadPart == (ULONG_PTR)PortOffset);

    /* Check if we're in memory space */
    if (!AddressSpace)
    {
        DPRINT1("Calling MmUnmapIoSpace\n");

        /* Unmap the memory */
        MmUnmapIoSpace(PortOffset, NumberOfPorts);
    }
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMUnmapIoSpace(
    IN  NDIS_HANDLE MiniportAdapterHandle,
    IN  PVOID       VirtualAddress,
    IN  UINT        Length)
/*
 * FUNCTION: Un-maps space previously mapped with NdisMMapIoSpace
 * ARGUMENTS:
 *     MiniportAdapterHandle: handle originally passed into MiniportInitialize
 *     VirtualAddress: Address to un-map
 *     Length: length of the mapped memory space
 * NOTES:
 *     - Must be called at IRQL = PASSIVE_LEVEL
 *     - Must only be called from MiniportInitialize and MiniportHalt
 *     - See also: NdisMMapIoSpace
 * BUGS:
 *     - Depends on MmUnmapIoSpace to Do The Right Thing in all cases
 */
{
  PAGED_CODE();

  ASSERT(MiniportAdapterHandle);

  MmUnmapIoSpace(VirtualAddress, Length);
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMInitializeScatterGatherDma(
    IN  NDIS_HANDLE MiniportAdapterHandle,
    IN  BOOLEAN     Dma64BitAddresses,
    IN  ULONG       MaximumPhysicalMapping)
/*
 * FUNCTION:
 * ARGUMENTS:
 * NOTES:
 *    NDIS 5.0
 */
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;
    ULONG MapRegisters;
    DEVICE_DESCRIPTION DeviceDesc;

    DPRINT("Called.\n");

    if (!(Adapter->NdisMiniportBlock.Flags & NDIS_ATTRIBUTE_BUS_MASTER)) {
        DPRINT1("Not a bus master\n");
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&DeviceDesc, sizeof(DEVICE_DESCRIPTION));

    DeviceDesc.Version = DEVICE_DESCRIPTION_VERSION;
    DeviceDesc.Master = TRUE;
    DeviceDesc.ScatterGather = TRUE;
    DeviceDesc.Dma32BitAddresses = TRUE; // All callers support 32-bit addresses
    DeviceDesc.Dma64BitAddresses = Dma64BitAddresses;
    DeviceDesc.BusNumber = Adapter->NdisMiniportBlock.BusNumber;
    DeviceDesc.InterfaceType = (INTERFACE_TYPE)Adapter->NdisMiniportBlock.BusType;
    DeviceDesc.MaximumLength = MaximumPhysicalMapping;

    Adapter->NdisMiniportBlock.SystemAdapterObject =
         IoGetDmaAdapter(Adapter->NdisMiniportBlock.PhysicalDeviceObject, &DeviceDesc, &MapRegisters);

    if (!Adapter->NdisMiniportBlock.SystemAdapterObject)
        return NDIS_STATUS_RESOURCES;

    /* FIXME: Right now we just use this as a place holder */
    Adapter->NdisMiniportBlock.ScatterGatherListSize = 1;

    return NDIS_STATUS_SUCCESS;
}


/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMRegisterScatterGatherDma(
    IN  NDIS_HANDLE MiniportAdapterHandle,
    IN  PNDIS_SG_DMA_DESCRIPTION DmaDescription,
    OUT PNDIS_HANDLE NdisMiniportDmaHandle)
/*
 * FUNCTION:
 *    Registers a miniport for scatter-gather DMA operations (NDIS 6.x)
 * ARGUMENTS:
 *    MiniportAdapterHandle - Handle returned by NdisMRegisterMiniport or
 *                           DeviceObject for NDIS 6.x miniports
 *    DmaDescription - Scatter-gather DMA description
 *    NdisMiniportDmaHandle - Receives the DMA adapter handle
 * NOTES:
 *    NDIS 6.0
 *    For NDIS 6.x miniports, MiniportAdapterHandle is actually a DeviceObject.
 *    We need to get the DMA adapter directly from the PDO stored in the
 *    device extension.
 */
{
    BOOLEAN Dma64BitAddresses;
    DEVICE_DESCRIPTION DeviceDesc;
    ULONG MapRegisters;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDMA_ADAPTER DmaAdapter;

    DPRINT("NdisMRegisterScatterGatherDma called.\n");

    if (!DmaDescription || !NdisMiniportDmaHandle) {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    *NdisMiniportDmaHandle = NULL;

    /* Determine 64-bit DMA support from flags */
    Dma64BitAddresses = (DmaDescription->Flags & 0x00000001) ? TRUE : FALSE;

    /*
     * In NDIS 6.x, MiniportAdapterHandle is actually a DeviceObject, not a
     * LOGICAL_ADAPTER. We need to extract the PDO from the device extension
     * to get the DMA adapter.
     *
     * The device extension layout (from Ndis6AddDevice):
     *   [0] = DriverBlock pointer
     *   [1] = PhysicalDeviceObject (PDO)
     *   [2] = Adapter context (set during init)
     *   [3] = NextDeviceObject (attached device)
     */
    DeviceObject = (PDEVICE_OBJECT)MiniportAdapterHandle;
    if (DeviceObject == NULL || DeviceObject->DeviceExtension == NULL) {
        DPRINT1("Invalid device object\n");
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    PhysicalDeviceObject = ((PVOID*)DeviceObject->DeviceExtension)[1];
    if (PhysicalDeviceObject == NULL) {
        DPRINT1("No PDO in device extension\n");
        return NDIS_STATUS_RESOURCES;
    }

    /* Setup device description for DMA adapter */
    RtlZeroMemory(&DeviceDesc, sizeof(DEVICE_DESCRIPTION));
    DeviceDesc.Version = DEVICE_DESCRIPTION_VERSION;
    DeviceDesc.Master = TRUE;
    DeviceDesc.ScatterGather = TRUE;
    DeviceDesc.Dma32BitAddresses = TRUE;
    DeviceDesc.Dma64BitAddresses = Dma64BitAddresses;
    DeviceDesc.InterfaceType = PCIBus;
    DeviceDesc.MaximumLength = DmaDescription->MaximumPhysicalMapping;

    /* Get the DMA adapter from the PDO */
    DmaAdapter = IoGetDmaAdapter(PhysicalDeviceObject, &DeviceDesc, &MapRegisters);
    if (DmaAdapter == NULL) {
        DPRINT1("IoGetDmaAdapter failed\n");
        return NDIS_STATUS_RESOURCES;
    }

    DPRINT1("DMA adapter obtained: %p, MapRegisters=%lu\n",
                              DmaAdapter, MapRegisters);

    /* Return the DMA adapter as the handle */
    *NdisMiniportDmaHandle = (NDIS_HANDLE)DmaAdapter;

    /*
     * Store the DMA adapter in the device extension so that NdisMAllocateSharedMemory
     * can find it. For NDIS 6.x, the device extension layout is:
     *   [0] = DriverBlock pointer
     *   [1] = PhysicalDeviceObject (PDO)
     *   [2] = Adapter context
     *   [3] = NextDeviceObject
     *   [4] = DMA adapter (stored here for shared memory allocation)
     */
    ((PVOID*)DeviceObject->DeviceExtension)[4] = DmaAdapter;

    DPRINT1("DMA adapter stored in DeviceExtension[4]: %p\n", DmaAdapter);

    return NDIS_STATUS_SUCCESS;
}


/*
 * @implemented
 */
VOID
EXPORT
NdisMDeregisterScatterGatherDma(
    IN  NDIS_HANDLE NdisMiniportDmaHandle)
/*
 * FUNCTION:
 *    Deregisters scatter-gather DMA (NDIS 6.x)
 * ARGUMENTS:
 *    NdisMiniportDmaHandle - DMA handle returned by NdisMRegisterScatterGatherDma
 *                           (this is actually a PDMA_ADAPTER)
 * NOTES:
 *    NDIS 6.0
 *    For NDIS 6.x, NdisMRegisterScatterGatherDma returns the DMA adapter directly,
 *    so we need to release it here using the adapter's PutDmaAdapter method.
 */
{
    PDMA_ADAPTER DmaAdapter = (PDMA_ADAPTER)NdisMiniportDmaHandle;

    DPRINT("NdisMDeregisterScatterGatherDma called.\n");

    if (DmaAdapter != NULL && DmaAdapter->DmaOperations != NULL &&
        DmaAdapter->DmaOperations->PutDmaAdapter != NULL)
    {
        DmaAdapter->DmaOperations->PutDmaAdapter(DmaAdapter);
        DPRINT1("DMA adapter released.\n");
    }
}


/*
 * @implemented
 */
VOID
EXPORT
NdisMapIoSpace(
    OUT PNDIS_STATUS            Status,
    OUT PVOID                   *VirtualAddress,
    IN  NDIS_HANDLE             NdisAdapterHandle,
    IN  NDIS_PHYSICAL_ADDRESS   PhysicalAddress,
    IN  UINT                    Length)
/*
 * FUNCTION:
 * ARGUMENTS:
 * NOTES:
 *    NDIS 4.0
 */
{
    *Status = NdisMMapIoSpace(VirtualAddress,
                              NdisAdapterHandle,
                              PhysicalAddress,
                              Length);
}


/*
 * @implemented
 */
VOID
EXPORT
NdisFreeDmaChannel(
    IN  PNDIS_HANDLE    NdisDmaHandle)
/*
 * FUNCTION:
 * ARGUMENTS:
 * NOTES:
 *    NDIS 4.0
 */
{
    NdisMDeregisterDmaChannel(NdisDmaHandle);
}



/* EOF */
