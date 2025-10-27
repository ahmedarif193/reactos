#include "precomp.h"

#define NDEBUG
#include <debug.h>

typedef struct _ACPI_NOTIFICATION_TARGET
{
    PDEVICE_NOTIFY_CALLBACK Callback;
    PVOID Context;
} ACPI_NOTIFICATION_TARGET, *PACPI_NOTIFICATION_TARGET;

#define ACPI_NOTIFY_STACK_TARGETS 4

static
NTSTATUS
AcpiStatusToNtStatus(ACPI_STATUS Status)
{
    if (ACPI_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
    }

    switch (Status)
    {
        case AE_ALREADY_EXISTS:
            return STATUS_SUCCESS;

        case AE_NO_MEMORY:
            return STATUS_INSUFFICIENT_RESOURCES;

        case AE_NOT_FOUND:
        case AE_NOT_EXIST:
            return STATUS_OBJECT_NAME_NOT_FOUND;

        case AE_BAD_PARAMETER:
            return STATUS_INVALID_PARAMETER;

        default:
            return STATUS_UNSUCCESSFUL;
    }
}

static
NTSTATUS
AcpiInterfaceInstallNotifyHandlers(PPDO_DEVICE_DATA DeviceData);

static
VOID
AcpiInterfaceRemoveNotifyHandlers(PPDO_DEVICE_DATA DeviceData);

static
VOID
ACPI_SYSTEM_XFACE
AcpiInterfaceNotifyThunk(ACPI_HANDLE Handle,
                         UINT32 Value,
                         PVOID Context);

static
PPDO_DEVICE_DATA
AcpiPdoFromContext(PVOID Context)
{
    PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)Context;

    if (!DeviceObject || !DeviceObject->DeviceExtension)
    {
        return NULL;
    }

    return (PPDO_DEVICE_DATA)DeviceObject->DeviceExtension;
}

VOID
NTAPI
AcpiInterfaceReference(PVOID Context)
{
    PPDO_DEVICE_DATA DeviceData;

    DeviceData = AcpiPdoFromContext(Context);
    if (!DeviceData)
    {
        return;
    }

    InterlockedIncrement((PLONG)&DeviceData->InterfaceRefCount);
}

VOID
NTAPI
AcpiInterfaceDereference(PVOID Context)
{
    PPDO_DEVICE_DATA DeviceData;

    DeviceData = AcpiPdoFromContext(Context);
    if (!DeviceData)
    {
        return;
    }

    InterlockedDecrement((PLONG)&DeviceData->InterfaceRefCount);
}

static
NTSTATUS
AcpiInterfaceInstallNotifyHandlers(PPDO_DEVICE_DATA DeviceData)
{
    ACPI_STATUS Status;

    if (!DeviceData || !DeviceData->AcpiHandle)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DeviceData->NotificationHandlersInstalled)
    {
        return STATUS_SUCCESS;
    }

    Status = AcpiInstallNotifyHandler(DeviceData->AcpiHandle,
                                      ACPI_SYSTEM_NOTIFY,
                                      AcpiInterfaceNotifyThunk,
                                      DeviceData);
    if (ACPI_FAILURE(Status) && Status != AE_ALREADY_EXISTS)
    {
        return AcpiStatusToNtStatus(Status);
    }

    Status = AcpiInstallNotifyHandler(DeviceData->AcpiHandle,
                                      ACPI_DEVICE_NOTIFY,
                                      AcpiInterfaceNotifyThunk,
                                      DeviceData);
    if (ACPI_FAILURE(Status) && Status != AE_ALREADY_EXISTS)
    {
        AcpiRemoveNotifyHandler(DeviceData->AcpiHandle,
                                ACPI_SYSTEM_NOTIFY,
                                AcpiInterfaceNotifyThunk);
        return AcpiStatusToNtStatus(Status);
    }

    DeviceData->NotificationHandlersInstalled = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
AcpiInterfaceRemoveNotifyHandlers(PPDO_DEVICE_DATA DeviceData)
{
    ACPI_STATUS Status;

    if (!DeviceData || !DeviceData->AcpiHandle)
    {
        return;
    }

    if (!DeviceData->NotificationHandlersInstalled)
    {
        return;
    }

    Status = AcpiRemoveNotifyHandler(DeviceData->AcpiHandle,
                                     ACPI_SYSTEM_NOTIFY,
                                     AcpiInterfaceNotifyThunk);
    if (ACPI_FAILURE(Status) && Status != AE_NOT_EXIST)
    {
        DPRINT1("AcpiRemoveNotifyHandler (system) failed: %s\n",
                AcpiFormatException(Status));
    }

    Status = AcpiRemoveNotifyHandler(DeviceData->AcpiHandle,
                                     ACPI_DEVICE_NOTIFY,
                                     AcpiInterfaceNotifyThunk);
    if (ACPI_FAILURE(Status) && Status != AE_NOT_EXIST)
    {
        DPRINT1("AcpiRemoveNotifyHandler (device) failed: %s\n",
                AcpiFormatException(Status));
    }

    DeviceData->NotificationHandlersInstalled = FALSE;
}

static
VOID
ACPI_SYSTEM_XFACE
AcpiInterfaceNotifyThunk(ACPI_HANDLE Handle,
                         UINT32 Value,
                         PVOID Context)
{
    PPDO_DEVICE_DATA DeviceData;
    ACPI_NOTIFICATION_TARGET StackTargets[ACPI_NOTIFY_STACK_TARGETS];
    PACPI_NOTIFICATION_TARGET Targets;
    ULONG TargetCount;
    ULONG Index;
    KIRQL OldIrql;
    PLIST_ENTRY Link;

    UNREFERENCED_PARAMETER(Handle);

    DeviceData = (PPDO_DEVICE_DATA)Context;
    if (!DeviceData)
    {
        return;
    }

    Targets = StackTargets;
    TargetCount = 0;

    KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);

    for (Link = DeviceData->NotificationList.Flink;
         Link != &DeviceData->NotificationList;
         Link = Link->Flink)
    {
        TargetCount++;
    }

    if (TargetCount == 0)
    {
        KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
        return;
    }

    if (TargetCount > RTL_NUMBER_OF(StackTargets))
    {
        Targets = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(ACPI_NOTIFICATION_TARGET) * TargetCount,
                                        ACPI_NOTIFY_TAG);
        if (!Targets)
        {
            KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
            return;
        }
    }

    Index = 0;
    for (Link = DeviceData->NotificationList.Flink;
         Link != &DeviceData->NotificationList;
         Link = Link->Flink)
    {
        PACPI_NOTIFICATION_ENTRY Entry;

        Entry = CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry);
        Targets[Index].Callback = Entry->Callback;
        Targets[Index].Context = Entry->Context;
        Index++;
    }

    KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

    for (Index = 0; Index < TargetCount; Index++)
    {
        if (Targets[Index].Callback)
        {
            Targets[Index].Callback(Targets[Index].Context, Value);
        }
    }

    if (Targets != StackTargets)
    {
        ExFreePoolWithTag(Targets, ACPI_NOTIFY_TAG);
    }
}

NTSTATUS
NTAPI
AcpiInterfaceConnectVector(PDEVICE_OBJECT Context,
                           ULONG GpeNumber,
                           KINTERRUPT_MODE Mode,
                           BOOLEAN Shareable,
                           PGPE_SERVICE_ROUTINE ServiceRoutine,
                           PVOID ServiceContext,
                           PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceDisconnectVector(PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceEnableEvent(PDEVICE_OBJECT Context,
                         PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceDisableEvent(PDEVICE_OBJECT Context,
                          PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceClearStatus(PDEVICE_OBJECT Context,
                         PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceNotificationsRegister(PDEVICE_OBJECT Context,
                                   PDEVICE_NOTIFY_CALLBACK NotificationHandler,
                                   PVOID NotificationContext)
{
  PPDO_DEVICE_DATA DeviceData;
  PACPI_NOTIFICATION_ENTRY Entry;
  KIRQL OldIrql;
  PLIST_ENTRY Link;
  BOOLEAN NeedInstall;
  NTSTATUS Status;

  if (!NotificationHandler)
  {
    return STATUS_INVALID_PARAMETER;
  }

  DeviceData = AcpiPdoFromContext(Context);
  if (!DeviceData)
  {
    return STATUS_INVALID_PARAMETER;
  }

  Entry = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(ACPI_NOTIFICATION_ENTRY),
                                ACPI_NOTIFY_TAG);
  if (!Entry)
  {
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  Entry->Callback = NotificationHandler;
  Entry->Context = NotificationContext;

  KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);

  NeedInstall = (DeviceData->NotificationRegistrationCount == 0);
  if (NeedInstall)
  {
    KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

    Status = AcpiInterfaceInstallNotifyHandlers(DeviceData);
    if (!NT_SUCCESS(Status))
    {
      ExFreePoolWithTag(Entry, ACPI_NOTIFY_TAG);
      return Status;
    }

    KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);
  }

  for (Link = DeviceData->NotificationList.Flink;
       Link != &DeviceData->NotificationList;
       Link = Link->Flink)
  {
    PACPI_NOTIFICATION_ENTRY Existing;

    Existing = CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry);
    if (Existing->Callback == NotificationHandler &&
        Existing->Context == NotificationContext)
    {
      KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
      ExFreePoolWithTag(Entry, ACPI_NOTIFY_TAG);
      return STATUS_SUCCESS;
    }
  }

  InsertTailList(&DeviceData->NotificationList, &Entry->ListEntry);
  DeviceData->NotificationRegistrationCount++;
  KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

  AcpiInterfaceReference(DeviceData->Common.Self);

  return STATUS_SUCCESS;
}

VOID
NTAPI
AcpiInterfaceNotificationsUnregister(PDEVICE_OBJECT Context,
                                     PDEVICE_NOTIFY_CALLBACK NotificationHandler)
{
  PPDO_DEVICE_DATA DeviceData;
  KIRQL OldIrql;
  PLIST_ENTRY Link, Next;
  BOOLEAN RemoveHandlers = FALSE;

  if (!NotificationHandler)
  {
    return;
  }

  DeviceData = AcpiPdoFromContext(Context);
  if (!DeviceData)
  {
    return;
  }

  KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);

  Link = DeviceData->NotificationList.Flink;
  while (Link != &DeviceData->NotificationList)
  {
    PACPI_NOTIFICATION_ENTRY Entry;

    Entry = CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry);
    Next = Link->Flink;

    if (Entry->Callback == NotificationHandler)
    {
      RemoveEntryList(Link);
      if (DeviceData->NotificationRegistrationCount > 0)
      {
        DeviceData->NotificationRegistrationCount--;
      }
      RemoveHandlers = RemoveHandlers ||
                       (DeviceData->NotificationRegistrationCount == 0);
      ExFreePoolWithTag(Entry, ACPI_NOTIFY_TAG);

      AcpiInterfaceDereference(DeviceData->Common.Self);
    }

    Link = Next;
  }

  KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

  if (RemoveHandlers)
  {
    AcpiInterfaceRemoveNotifyHandlers(DeviceData);
  }
}

VOID
AcpiInterfaceResetNotifications(PPDO_DEVICE_DATA DeviceData)
{
  LIST_ENTRY FreeList;
  PLIST_ENTRY Link;
  KIRQL OldIrql;

  if (!DeviceData)
  {
    return;
  }

  InitializeListHead(&FreeList);

  AcpiInterfaceRemoveNotifyHandlers(DeviceData);

  KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);
  DeviceData->NotificationRegistrationCount = 0;

  while (!IsListEmpty(&DeviceData->NotificationList))
  {
    Link = RemoveHeadList(&DeviceData->NotificationList);
    InsertTailList(&FreeList, Link);
  }

  KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

  while (!IsListEmpty(&FreeList))
  {
    Link = RemoveHeadList(&FreeList);
    AcpiInterfaceDereference(DeviceData->Common.Self);
    ExFreePoolWithTag(CONTAINING_RECORD(Link, ACPI_NOTIFICATION_ENTRY, ListEntry),
                      ACPI_NOTIFY_TAG);
  }
}

NTSTATUS
Bus_PDO_QueryInterface(PPDO_DEVICE_DATA DeviceData,
                       PIRP Irp)
{
  PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
  PACPI_INTERFACE_STANDARD AcpiInterface;

  if (IrpSp->Parameters.QueryInterface.Version != 1)
  {
      DPRINT1("Invalid version number: %d\n",
              IrpSp->Parameters.QueryInterface.Version);
      return STATUS_INVALID_PARAMETER;
  }

  if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                        &GUID_ACPI_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
  {
      DPRINT("GUID_ACPI_INTERFACE_STANDARD\n");

      if (IrpSp->Parameters.QueryInterface.Size < sizeof(ACPI_INTERFACE_STANDARD))
      {
          DPRINT1("Buffer too small! (%d)\n", IrpSp->Parameters.QueryInterface.Size);
          return STATUS_BUFFER_TOO_SMALL;
      }

     AcpiInterface = (PACPI_INTERFACE_STANDARD)IrpSp->Parameters.QueryInterface.Interface;

     AcpiInterface->Size = sizeof(ACPI_INTERFACE_STANDARD);
     AcpiInterface->Version = 1;
     AcpiInterface->Context = DeviceData->Common.Self;
     AcpiInterface->InterfaceReference = AcpiInterfaceReference;
     AcpiInterface->InterfaceDereference = AcpiInterfaceDereference;
     AcpiInterface->GpeConnectVector = AcpiInterfaceConnectVector;
     AcpiInterface->GpeDisconnectVector = AcpiInterfaceDisconnectVector;
     AcpiInterface->GpeEnableEvent = AcpiInterfaceEnableEvent;
     AcpiInterface->GpeDisableEvent = AcpiInterfaceDisableEvent;
     AcpiInterface->GpeClearStatus = AcpiInterfaceClearStatus;
     AcpiInterface->RegisterForDeviceNotifications = AcpiInterfaceNotificationsRegister;
     AcpiInterface->UnregisterForDeviceNotifications = AcpiInterfaceNotificationsUnregister;

     AcpiInterfaceReference(AcpiInterface->Context);

     return STATUS_SUCCESS;
  }
  else
  {
      DPRINT1("Invalid GUID\n");
      return STATUS_NOT_SUPPORTED;
  }
}
