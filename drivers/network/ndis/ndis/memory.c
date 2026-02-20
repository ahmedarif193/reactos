/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/memory.c
 * PURPOSE:     Memory management routines
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Vizzini (vizzini@plasmic.com)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 *   15 Aug 2003 Vizzini - DMA support
 *   3  Oct 2003 Vizzini - formatting and minor bugfixing
 */

#include "ndissys.h"

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisAllocateMemoryWithTag(
    OUT PVOID   *VirtualAddress,
    IN  UINT    Length,
    IN  ULONG   Tag)
/*
 * FUNCTION:  Allocates a block of memory, with a 32-bit tag
 * ARGUMENTS:
 *   VirtualAddress = a pointer to the returned memory block
 *   Length         = the number of requested bytes
 *   Tag            = 32-bit pool tag
 * RETURNS:
 *   NDIS_STATUS_SUCCESS on success
 *   NDIS_STATUS_FAILURE on failure
 */
{
  PVOID Block;

  DPRINT("Called.\n");

  Block = ExAllocatePoolWithTag(NonPagedPool, Length, Tag);
  *VirtualAddress = Block;

  if (!Block) {
    DPRINT1("Failed to allocate memory (%lx)\n", Length);
    return NDIS_STATUS_FAILURE;
  }

  return NDIS_STATUS_SUCCESS;
}


/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisAllocateMemory(
    OUT PVOID                   *VirtualAddress,
    IN  UINT                    Length,
    IN  UINT                    MemoryFlags,
    IN  NDIS_PHYSICAL_ADDRESS   HighestAcceptableAddress)
/*
 * FUNCTION: Allocates a block of memory
 * ARGUMENTS:
 *     VirtualAddress           = Address of buffer to place virtual
 *                                address of the allocated memory
 *     Length                   = Size of the memory block to allocate
 *     MemoryFlags              = Flags to specify special restrictions
 *     HighestAcceptableAddress = Specifies -1
 * RETURNS:
 *     NDIS_STATUS_SUCCESS on success
 *     NDIS_STATUS_FAILURE on failure
 */
{
  DPRINT("Called.\n");

  if (MemoryFlags & NDIS_MEMORY_CONTIGUOUS)
  {
      /* Allocate contiguous memory (possibly noncached) */
      *VirtualAddress = MmAllocateContiguousMemorySpecifyCache(Length,
                                                               RtlConvertUlongToLargeInteger(0),
                                                               HighestAcceptableAddress,
                                                               RtlConvertUlongToLargeInteger(0),
                                                               (MemoryFlags & NDIS_MEMORY_NONCACHED) ? MmNonCached : MmCached);
  }
  else if (MemoryFlags & NDIS_MEMORY_NONCACHED)
  {
      /* Allocate noncached noncontiguous memory */
      *VirtualAddress = MmAllocateNonCachedMemory(Length);
  }
  else
  {
      /* Allocate plain nonpaged memory */
      *VirtualAddress = ExAllocatePool(NonPagedPool, Length);
  }

  if (!*VirtualAddress) {
    DPRINT1("Allocation failed (%lx, %lx)\n", MemoryFlags, Length);
    return NDIS_STATUS_FAILURE;
  }

  return NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisFreeMemory(
    IN  PVOID   VirtualAddress,
    IN  UINT    Length,
    IN  UINT    MemoryFlags)
/*
 * FUNCTION: Frees a memory block allocated with NdisAllocateMemory
 * ARGUMENTS:
 *     VirtualAddress = Pointer to the base virtual address of the allocated memory
 *     Length         = Size of the allocated memory block as passed to NdisAllocateMemory
 *     MemoryFlags    = Memory flags passed to NdisAllocateMemory
 */
{
  DPRINT("Called.\n");

  if (MemoryFlags & NDIS_MEMORY_CONTIGUOUS)
  {
      /* Free contiguous memory (possibly noncached) */
      MmFreeContiguousMemorySpecifyCache(VirtualAddress,
                                         Length,
                                         (MemoryFlags & NDIS_MEMORY_NONCACHED) ? MmNonCached : MmCached);
  }
  else if (MemoryFlags & NDIS_MEMORY_NONCACHED)
  {
      /* Free noncached noncontiguous memory */
      MmFreeNonCachedMemory(VirtualAddress, Length);
  }
  else
  {
      /* Free nonpaged pool */
      ExFreePool(VirtualAddress);
  }
}

/*
 * Helper function to check if a handle is an NDIS 6.x device object
 * Returns the DMA adapter if it's NDIS 6.x, NULL otherwise
 */
static PDMA_ADAPTER
NdispGetNdis6DmaAdapter(
    IN NDIS_HANDLE MiniportAdapterHandle)
{
    PDEVICE_OBJECT DeviceObject;

    /*
     * For NDIS 6.x, MiniportAdapterHandle is a PDEVICE_OBJECT.
     * For NDIS 5.x, it's a PLOGICAL_ADAPTER where the first member
     * is NDIS_MINIPORT_BLOCK.
     *
     * We can distinguish them by checking if the object looks like
     * a DEVICE_OBJECT (Type == IO_TYPE_DEVICE == 3).
     *
     * For NDIS 6.x device objects created by Ndis6AddDevice:
     *   DeviceExtension[4] contains the DMA adapter set by
     *   NdisMRegisterScatterGatherDma.
     */
    DeviceObject = (PDEVICE_OBJECT)MiniportAdapterHandle;

    /* Check if this looks like a device object */
    if (DeviceObject != NULL &&
        DeviceObject->Type == IO_TYPE_DEVICE &&
        DeviceObject->DeviceExtension != NULL)
    {
        PDMA_ADAPTER DmaAdapter = ((PVOID*)DeviceObject->DeviceExtension)[4];
        if (DmaAdapter != NULL)
        {
            return DmaAdapter;
        }
    }

    return NULL;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMAllocateSharedMemory(
    IN	NDIS_HANDLE             MiniportAdapterHandle,
    IN	ULONG                   Length,
    IN	BOOLEAN                 Cached,
    OUT	PVOID                   *VirtualAddress,
    OUT	PNDIS_PHYSICAL_ADDRESS  PhysicalAddress)
/*
 * FUNCTION: Allocate a common buffer for DMA
 * ARGUMENTS:
 *     MiniportAdapterHandle:  Handle passed into MiniportInitialize
 *     Length:  Number of bytes to allocate
 *     Cached:  Whether or not the memory can be cached
 *     VirtualAddress:  Pointer to memory is returned here
 *     PhysicalAddress:  Physical address corresponding to virtual address
 * NOTES:
 *     - Cached is ignored; we always allocate non-cached
 *     - For NDIS 6.x, MiniportAdapterHandle is a PDEVICE_OBJECT and the
 *       DMA adapter is stored in DeviceExtension[4] by NdisMRegisterScatterGatherDma
 */
{
  PDMA_ADAPTER DmaAdapter;

  DPRINT("Called.\n");

  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
  {
      KeBugCheckEx(BUGCODE_ID_DRIVER,
                   (ULONG_PTR)MiniportAdapterHandle,
                   Length,
                   0,
                   1);
  }

  /* Check if this is an NDIS 6.x handle */
  DmaAdapter = NdispGetNdis6DmaAdapter(MiniportAdapterHandle);
  if (DmaAdapter != NULL)
  {
      /* NDIS 6.x path - use DMA adapter from device extension */
      *VirtualAddress = DmaAdapter->DmaOperations->AllocateCommonBuffer(
          DmaAdapter, Length, PhysicalAddress, Cached);
  }
  else
  {
      /* NDIS 5.x path - use SystemAdapterObject from LOGICAL_ADAPTER */
      PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;

      *VirtualAddress = Adapter->NdisMiniportBlock.SystemAdapterObject->DmaOperations->AllocateCommonBuffer(
          Adapter->NdisMiniportBlock.SystemAdapterObject, Length, PhysicalAddress, Cached);
  }
}

VOID
NTAPI
NdisMFreeSharedMemoryPassive(
    PDEVICE_OBJECT DeviceObject,
    PVOID Context)
/*
 * FUNCTION:  Free a common buffer
 * ARGUMENTS:
 *     Context:  Pointer to a miniport shared memory context
 * NOTES:
 *     - Called by NdisMFreeSharedMemory to do the actual work
 */
{
  PMINIPORT_SHARED_MEMORY Memory = (PMINIPORT_SHARED_MEMORY)Context;

  DPRINT("Called.\n");

  ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

  Memory->AdapterObject->DmaOperations->FreeCommonBuffer(
      Memory->AdapterObject, Memory->Length, Memory->PhysicalAddress,
      Memory->VirtualAddress, Memory->Cached);

  IoFreeWorkItem(Memory->WorkItem);
  ExFreePool(Memory);
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMFreeSharedMemory(
    IN  NDIS_HANDLE             MiniportAdapterHandle,
    IN  ULONG                   Length,
    IN  BOOLEAN                 Cached,
    IN  PVOID                   VirtualAddress,
    IN  NDIS_PHYSICAL_ADDRESS   PhysicalAddress)
/*
 * FUNCTION:  Free a shared memory block
 * ARGUMENTS:
 *     MiniportAdapterHandle:  Handle passed into MiniportInitialize
 *     Length:  Number of bytes in the block to free
 *     Cached:  Whether or not the memory was cached
 *     VirtualAddress:  Address to free
 *     PhysicalAddress:  corresponding physical addres
 * NOTES:
 *     - This function can be called at dispatch_level or passive_level.
 *       Therefore we have to do this in a worker thread.
 *     - For NDIS 6.x, MiniportAdapterHandle is a PDEVICE_OBJECT and the
 *       DMA adapter is stored in DeviceExtension[4]
 */
{
  PMINIPORT_SHARED_MEMORY Memory;
  PDMA_ADAPTER DmaAdapter;
  PDEVICE_OBJECT DeviceObject;

  DPRINT("Called.\n");

  ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

  /* Check if this is an NDIS 6.x handle */
  DmaAdapter = NdispGetNdis6DmaAdapter(MiniportAdapterHandle);
  if (DmaAdapter != NULL)
  {
      /* NDIS 6.x path */
      DeviceObject = (PDEVICE_OBJECT)MiniportAdapterHandle;

      DPRINT1("NDIS 6.x FreeSharedMemory: VA=%p, DmaAdapter=%p\n",
                                VirtualAddress, DmaAdapter);

      /* Call FreeCommonBuffer synchronously if we are at PASSIVE_LEVEL */
      if (KeGetCurrentIrql() == PASSIVE_LEVEL)
      {
          DmaAdapter->DmaOperations->FreeCommonBuffer(DmaAdapter,
                                                      Length,
                                                      PhysicalAddress,
                                                      VirtualAddress,
                                                      Cached);
          return;
      }

      /* Queue work item for DISPATCH_LEVEL */
      Memory = ExAllocatePool(NonPagedPool, sizeof(MINIPORT_SHARED_MEMORY));
      if (!Memory)
      {
          DPRINT1("Insufficient resources\n");
          return;
      }

      Memory->AdapterObject = DmaAdapter;
      Memory->Length = Length;
      Memory->PhysicalAddress = PhysicalAddress;
      Memory->VirtualAddress = VirtualAddress;
      Memory->Cached = Cached;
      Memory->Adapter = NULL;  /* Not used for NDIS 6.x */

      Memory->WorkItem = IoAllocateWorkItem(DeviceObject);
      if (!Memory->WorkItem)
      {
          DPRINT1("Insufficient resources\n");
          ExFreePool(Memory);
          return;
      }

      IoQueueWorkItem(Memory->WorkItem,
                      NdisMFreeSharedMemoryPassive,
                      CriticalWorkQueue,
                      Memory);
  }
  else
  {
      /* NDIS 5.x path */
      PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;
      DmaAdapter = Adapter->NdisMiniportBlock.SystemAdapterObject;

      /* Call FreeCommonBuffer synchronously if we are at PASSIVE_LEVEL */
      if (KeGetCurrentIrql() == PASSIVE_LEVEL)
      {
          /* We need this case because we free shared memory asynchronously
           * and the miniport (and DMA adapter object) could be freed before
           * our work item executes. Lucky for us, the scenarios where the
           * freeing needs to be synchronous (failed init, MiniportHalt,
           * and driver unload) are all at PASSIVE_LEVEL so we can just
           * call FreeCommonBuffer synchronously and not have to worry
           * about the miniport falling out from under us */

          DPRINT1("Freeing shared memory synchronously\n");

          DmaAdapter->DmaOperations->FreeCommonBuffer(DmaAdapter,
                                                      Length,
                                                      PhysicalAddress,
                                                      VirtualAddress,
                                                      Cached);
          return;
      }

      /* Must be NonpagedPool because by definition we're at DISPATCH_LEVEL */
      Memory = ExAllocatePool(NonPagedPool, sizeof(MINIPORT_SHARED_MEMORY));

      if(!Memory)
        {
          DPRINT1("Insufficient resources\n");
          return;
        }

      Memory->AdapterObject = Adapter->NdisMiniportBlock.SystemAdapterObject;
      Memory->Length = Length;
      Memory->PhysicalAddress = PhysicalAddress;
      Memory->VirtualAddress = VirtualAddress;
      Memory->Cached = Cached;
      Memory->Adapter = &Adapter->NdisMiniportBlock;

      Memory->WorkItem = IoAllocateWorkItem(Adapter->NdisMiniportBlock.DeviceObject);
      if (!Memory->WorkItem)
      {
          DPRINT1("Insufficient resources\n");
          ExFreePool(Memory);
          return;
      }

      IoQueueWorkItem(Memory->WorkItem,
                      NdisMFreeSharedMemoryPassive,
                      CriticalWorkQueue,
                      Memory);
  }
}

VOID
NTAPI
NdisMAllocateSharedMemoryPassive(
    PDEVICE_OBJECT DeviceObject,
    PVOID Context)
/*
 * FUNCTION:  Allocate a common buffer
 * ARGUMENTS:
 *     Context:  Pointer to a miniport shared memory context
 * NOTES:
 *     - Called by NdisMAllocateSharedMemoryAsync to do the actual work
 */
{
  PMINIPORT_SHARED_MEMORY Memory = (PMINIPORT_SHARED_MEMORY)Context;

  DPRINT("Called.\n");

  ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

  Memory->VirtualAddress = Memory->AdapterObject->DmaOperations->AllocateCommonBuffer(
      Memory->AdapterObject, Memory->Length, &Memory->PhysicalAddress, Memory->Cached);

  if (Memory->Adapter->DriverHandle->MiniportCharacteristics.AllocateCompleteHandler)
      Memory->Adapter->DriverHandle->MiniportCharacteristics.AllocateCompleteHandler(
             Memory->Adapter->MiniportAdapterContext, Memory->VirtualAddress,
             &Memory->PhysicalAddress, Memory->Length, Memory->Context);

  IoFreeWorkItem(Memory->WorkItem);
  ExFreePool(Memory);
}


/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMAllocateSharedMemoryAsync(
    IN  NDIS_HANDLE MiniportAdapterHandle,
    IN  ULONG       Length,
    IN  BOOLEAN     Cached,
    IN  PVOID       Context)
{
  PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;
  PMINIPORT_SHARED_MEMORY Memory;

  DPRINT("Called.\n");

  ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

  /* Must be NonpagedPool because by definition we're at DISPATCH_LEVEL */
  Memory = ExAllocatePool(NonPagedPool, sizeof(MINIPORT_SHARED_MEMORY));

  if(!Memory)
    {
      DPRINT1("Insufficient resources\n");
      return NDIS_STATUS_FAILURE;
    }

  Memory->AdapterObject = Adapter->NdisMiniportBlock.SystemAdapterObject;
  Memory->Length = Length;
  Memory->Cached = Cached;
  Memory->Adapter = &Adapter->NdisMiniportBlock;
  Memory->Context = Context;

  Memory->WorkItem = IoAllocateWorkItem(Adapter->NdisMiniportBlock.DeviceObject);
  if (!Memory->WorkItem)
  {
      DPRINT1("Insufficient resources\n");
      ExFreePool(Memory);
      return NDIS_STATUS_FAILURE;
  }

  IoQueueWorkItem(Memory->WorkItem,
                  NdisMAllocateSharedMemoryPassive,
                  DelayedWorkQueue,
                  Memory);

  return NDIS_STATUS_PENDING;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisAllocateSharedMemory(
    IN  NDIS_HANDLE             NdisAdapterHandle,
    IN  ULONG                   Length,
    IN  BOOLEAN                 Cached,
    OUT PVOID                   *VirtualAddress,
    OUT PNDIS_PHYSICAL_ADDRESS  PhysicalAddress)
{
    NdisMAllocateSharedMemory(NdisAdapterHandle,
                              Length,
                              Cached,
                              VirtualAddress,
                              PhysicalAddress);
}


/*
 * @implemented
 */
VOID
EXPORT
NdisFreeSharedMemory(
    IN NDIS_HANDLE              NdisAdapterHandle,
    IN ULONG                    Length,
    IN BOOLEAN                  Cached,
    IN PVOID                    VirtualAddress,
    IN NDIS_PHYSICAL_ADDRESS    PhysicalAddress)
/*
 * FUNCTION:
 * ARGUMENTS:
 * NOTES:
 *    NDIS 4.0
 */
{
    NdisMFreeSharedMemory(NdisAdapterHandle,
                          Length,
                          Cached,
                          VirtualAddress,
                          PhysicalAddress);
}


/* EOF */
