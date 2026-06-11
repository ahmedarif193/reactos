/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        workitem.c
 * PURPOSE:     Implements the NDIS 6.0 work item interface
 * PROGRAMMERS: Cameron Gutman (aicommander@gmail.com)
 */

#include "ndissys.h"

#define NDIS_IO_WORKITEM_TAG 'wINn'
#define NDIS_IO_WORKITEM_MAGIC 0xB16C1001

typedef struct _NDIS_IO_WORKITEM_WRAPPER
{
   ULONG Magic;
   PIO_WORKITEM IoWorkItem;
   NDIS_IO_WORKITEM_ROUTINE Routine;
   PVOID WorkItemContext;
} NDIS_IO_WORKITEM_WRAPPER, *PNDIS_IO_WORKITEM_WRAPPER;

static VOID NTAPI
NdisIoWorkItemThunk(
   IN PDEVICE_OBJECT DeviceObject,
   IN PVOID Context)
{
   PNDIS_IO_WORKITEM_WRAPPER Wrapper = Context;

   UNREFERENCED_PARAMETER(DeviceObject);

   if (Wrapper == NULL || Wrapper->Magic != NDIS_IO_WORKITEM_MAGIC ||
       Wrapper->Routine == NULL)
   {
      return;
   }

   Wrapper->Routine(Wrapper->WorkItemContext, Wrapper);
}

NDIS_HANDLE
EXPORT
NdisAllocateIoWorkItem(
    IN NDIS_HANDLE NdisObjectHandle)
{
   PLOGICAL_ADAPTER Adapter = NdisObjectHandle;
   PNDIS_IO_WORKITEM_WRAPPER Wrapper;
   PIO_WORKITEM IoWorkItem;

   if (Adapter == NULL || Adapter->NdisMiniportBlock.PhysicalDeviceObject == NULL)
      return NULL;

   IoWorkItem = IoAllocateWorkItem(Adapter->NdisMiniportBlock.PhysicalDeviceObject);
   if (IoWorkItem == NULL)
      return NULL;

   Wrapper = ExAllocatePoolWithTag(NonPagedPool,
                                   sizeof(*Wrapper),
                                   NDIS_IO_WORKITEM_TAG);
   if (Wrapper == NULL)
   {
      IoFreeWorkItem(IoWorkItem);
      return NULL;
   }

   RtlZeroMemory(Wrapper, sizeof(*Wrapper));
   Wrapper->Magic = NDIS_IO_WORKITEM_MAGIC;
   Wrapper->IoWorkItem = IoWorkItem;
   return Wrapper;
}

VOID
EXPORT
NdisQueueIoWorkItem(
    IN NDIS_HANDLE NdisIoWorkItemHandle,
    IN NDIS_IO_WORKITEM_ROUTINE Routine,
    IN PVOID WorkItemContext)
{
   PNDIS_IO_WORKITEM_WRAPPER Wrapper = NdisIoWorkItemHandle;

   if (Wrapper == NULL || Wrapper->Magic != NDIS_IO_WORKITEM_MAGIC ||
       Wrapper->IoWorkItem == NULL || Routine == NULL)
   {
      return;
   }

   Wrapper->Routine = Routine;
   Wrapper->WorkItemContext = WorkItemContext;

   IoQueueWorkItem(Wrapper->IoWorkItem,
                   NdisIoWorkItemThunk,
                   DelayedWorkQueue,
                   Wrapper);
}

VOID
EXPORT
NdisFreeIoWorkItem(
    IN NDIS_HANDLE NdisIoWorkItemHandle)
{
   PNDIS_IO_WORKITEM_WRAPPER Wrapper = NdisIoWorkItemHandle;

   if (Wrapper == NULL || Wrapper->Magic != NDIS_IO_WORKITEM_MAGIC)
      return;

   Wrapper->Magic = 0;
   if (Wrapper->IoWorkItem != NULL)
      IoFreeWorkItem(Wrapper->IoWorkItem);

   ExFreePoolWithTag(Wrapper, NDIS_IO_WORKITEM_TAG);
}
