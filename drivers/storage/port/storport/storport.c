/*
 * PROJECT:     ReactOS Storport Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Storport driver main file
 * COPYRIGHT:   Copyright 2017 Eric Kohl (eric.kohl@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include "precomp.h"

#define NDEBUG
#include <debug.h>


/* GLOBALS ********************************************************************/

ULONG PortNumber = 0;

#ifdef _WIN64
C_ASSERT(sizeof(HW_INITIALIZATION_DATA) == 208);
#endif


/* FUNCTIONS ******************************************************************/

static
NTSTATUS
PortAddDriverInitData(
    PDRIVER_OBJECT_EXTENSION DriverExtension,
    PHW_INITIALIZATION_DATA HwInitializationData)
{
    PDRIVER_INIT_DATA InitData;
    ULONG CopyLength;

    DPRINT1("PortAddDriverInitData()\n");

    InitData = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(DRIVER_INIT_DATA),
                                     TAG_INIT_DATA);
    if (InitData == NULL)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(&InitData->HwInitData, sizeof(InitData->HwInitData));
    CopyLength = min(HwInitializationData->HwInitializationDataSize, sizeof(InitData->HwInitData));
    RtlCopyMemory(&InitData->HwInitData, HwInitializationData, CopyLength);

    InsertHeadList(&DriverExtension->InitDataListHead,
                   &InitData->Entry);

    return STATUS_SUCCESS;
}


static
VOID
PortDeleteDriverInitData(
    PDRIVER_OBJECT_EXTENSION DriverExtension)
{
    PDRIVER_INIT_DATA InitData;
    PLIST_ENTRY ListEntry;

    DPRINT1("PortDeleteDriverInitData()\n");

    ListEntry = DriverExtension->InitDataListHead.Flink;
    while (ListEntry != &DriverExtension->InitDataListHead)
    {
        InitData = CONTAINING_RECORD(ListEntry,
                                     DRIVER_INIT_DATA,
                                     Entry);

        RemoveEntryList(&InitData->Entry);

        ExFreePoolWithTag(InitData,
                          TAG_INIT_DATA);

        ListEntry = DriverExtension->InitDataListHead.Flink;
    }
}


PHW_INITIALIZATION_DATA
PortGetDriverInitData(
    PDRIVER_OBJECT_EXTENSION DriverExtension,
    INTERFACE_TYPE InterfaceType)
{
    PDRIVER_INIT_DATA InitData;
    PLIST_ENTRY ListEntry;

    DPRINT1("PortGetDriverInitData()\n");

    ListEntry = DriverExtension->InitDataListHead.Flink;
    while (ListEntry != &DriverExtension->InitDataListHead)
    {
        InitData = CONTAINING_RECORD(ListEntry,
                                     DRIVER_INIT_DATA,
                                     Entry);
        if (InitData->HwInitData.AdapterInterfaceType == InterfaceType)
            return &InitData->HwInitData;

        ListEntry = ListEntry->Flink;
    }

    return NULL;
}


static
VOID
PortAcquireSpinLock(
    PFDO_DEVICE_EXTENSION DeviceExtension,
    STOR_SPINLOCK SpinLock,
    PVOID LockContext,
    PSTOR_LOCK_HANDLE LockHandle)
{
    DPRINT("PortAcquireSpinLock(%p %lu %p %p)\n",
           DeviceExtension, SpinLock, LockContext, LockHandle);

    LockHandle->Lock = SpinLock;
    if (DeviceExtension->DumpMode)
    {
        LockHandle->Context.OldIrql = KeGetCurrentIrql();
        return;
    }

    switch (SpinLock)
    {
        case DpcLock: /* 1, */
        case StartIoLock: /* 2 */
            KeAcquireSpinLock(&DeviceExtension->MiniportExLock, &LockHandle->Context.OldIrql);
            break;

        case InterruptLock: /* 3 */
            DPRINT("InterruptLock\n");
            if (DeviceExtension->Interrupt == NULL)
                KeAcquireSpinLock(&DeviceExtension->MiniportExLock, &LockHandle->Context.OldIrql);
            else
                LockHandle->Context.OldIrql = KeAcquireInterruptSpinLock(DeviceExtension->Interrupt);
            break;
    }
}


static
VOID
PortReleaseSpinLock(
    PFDO_DEVICE_EXTENSION DeviceExtension,
    PSTOR_LOCK_HANDLE LockHandle)
{
    DPRINT("PortReleaseSpinLock(%p %p)\n",
           DeviceExtension, LockHandle);

    if (DeviceExtension->DumpMode)
        return;

    switch (LockHandle->Lock)
    {
        case DpcLock: /* 1, */
        case StartIoLock: /* 2 */
            KeReleaseSpinLock(&DeviceExtension->MiniportExLock, LockHandle->Context.OldIrql);
            break;

        case InterruptLock: /* 3 */
            DPRINT("InterruptLock\n");
            if (DeviceExtension->Interrupt == NULL)
                KeReleaseSpinLock(&DeviceExtension->MiniportExLock, LockHandle->Context.OldIrql);
            else
                KeReleaseInterruptSpinLock(DeviceExtension->Interrupt,
                                           LockHandle->Context.OldIrql);
            break;
    }
}

static
KIRQL
PortAcquireTimerLock(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL CurrentIrql, OldIrql, TargetIrql;

    CurrentIrql = KeGetCurrentIrql();
    TargetIrql = DISPATCH_LEVEL;

    /*
     * RequestTimerCall is valid from a miniport interrupt callback. Raise the
     * timer DPC to the adapter interrupt IRQL before taking this lock so an
     * interrupt on the same CPU cannot preempt the lock owner and deadlock.
     */
    if ((DeviceExtension->Interrupt != NULL) &&
        (DeviceExtension->InterruptIrql > TargetIrql))
    {
        TargetIrql = (KIRQL)DeviceExtension->InterruptIrql;
    }

    OldIrql = CurrentIrql;
    if (CurrentIrql < TargetIrql)
    {
        KeRaiseIrql(TargetIrql, &OldIrql);
    }

    KeAcquireSpinLockAtDpcLevel(&DeviceExtension->MiniportTimerLock);
    return OldIrql;
}

static
VOID
PortReleaseTimerLock(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ KIRQL OldIrql)
{
    KIRQL CurrentIrql;

    KeReleaseSpinLockFromDpcLevel(&DeviceExtension->MiniportTimerLock);

    CurrentIrql = KeGetCurrentIrql();
    if (CurrentIrql != OldIrql)
    {
        KeLowerIrql(OldIrql);
    }
}

static
VOID
NTAPI
PortMiniportTimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PHW_TIMER TimerRoutine;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeferredContext;
    TimerRoutine = NULL;

    OldIrql = PortAcquireTimerLock(DeviceExtension);
    if (DeviceExtension->MiniportTimerArmed)
    {
        DeviceExtension->MiniportTimerArmed = FALSE;
        TimerRoutine = DeviceExtension->MiniportTimerRoutine;
    }
    PortReleaseTimerLock(DeviceExtension, OldIrql);

    if ((TimerRoutine != NULL) &&
        (DeviceExtension->Miniport.MiniportExtension != NULL))
    {
        TimerRoutine(&DeviceExtension->Miniport.MiniportExtension->HwDeviceExtension);
    }
}

static
VOID
NTAPI
PortMiniportTimerRequestDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    LARGE_INTEGER DueTime;
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PHW_TIMER TimerRoutine;
    ULONG TimerValue;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeferredContext;

    /*
     * KeSetTimer is only valid through DISPATCH_LEVEL, whereas a miniport may
     * request its timer while running at the adapter interrupt IRQL. Keep the
     * request fields synchronized at that IRQL, but apply each request from
     * this DPC after dropping the lock.
     */
    for (;;)
    {
        OldIrql = PortAcquireTimerLock(DeviceExtension);
        if (!DeviceExtension->MiniportTimerRequestPending)
        {
            DeviceExtension->MiniportTimerRequestDpcActive = FALSE;
            PortReleaseTimerLock(DeviceExtension, OldIrql);
            break;
        }

        TimerRoutine = DeviceExtension->MiniportTimerRequestedRoutine;
        TimerValue = DeviceExtension->MiniportTimerRequestedValue;
        DeviceExtension->MiniportTimerRequestPending = FALSE;

        if ((TimerValue == 0) || (TimerRoutine == NULL))
        {
            DeviceExtension->MiniportTimerArmed = FALSE;
            DeviceExtension->MiniportTimerRoutine = NULL;
        }
        else
        {
            DeviceExtension->MiniportTimerRoutine = TimerRoutine;
            DeviceExtension->MiniportTimerArmed = TRUE;
        }
        PortReleaseTimerLock(DeviceExtension, OldIrql);

        if ((TimerValue == 0) || (TimerRoutine == NULL))
        {
            KeCancelTimer(&DeviceExtension->MiniportTimer);
        }
        else
        {
            DueTime.QuadPart = -((LONGLONG)TimerValue * 10);
            KeSetTimer(&DeviceExtension->MiniportTimer,
                       DueTime,
                       &DeviceExtension->MiniportTimerDpc);
        }
    }
}


static
NTSTATUS
NTAPI
PortAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDRIVER_OBJECT_EXTENSION DriverObjectExtension;
    PFDO_DEVICE_EXTENSION DeviceExtension = NULL;
    WCHAR NameBuffer[80];
    UNICODE_STRING DeviceName;
    PDEVICE_OBJECT Fdo = NULL;
    KLOCK_QUEUE_HANDLE LockHandle;
    NTSTATUS Status;

    DPRINT1("PortAddDevice(%p %p)\n",
            DriverObject, PhysicalDeviceObject);

    ASSERT(DriverObject);
    ASSERT(PhysicalDeviceObject);

    _swprintf(NameBuffer,
              L"\\Device\\RaidPort%lu",
              PortNumber);
    RtlInitUnicodeString(&DeviceName, NameBuffer);
    PortNumber++;

    DPRINT1("Creating device: %wZ\n", &DeviceName);

    /* Create the port device */
    Status = IoCreateDevice(DriverObject,
                            sizeof(FDO_DEVICE_EXTENSION),
                            &DeviceName,
                            FILE_DEVICE_CONTROLLER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoCreateDevice() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    DPRINT1("Created device: %wZ (%p)\n", &DeviceName, Fdo);

    /* Initialize the device */
    Fdo->Flags |= DO_DIRECT_IO;
    Fdo->Flags |= DO_POWER_PAGABLE;

    /* Initialize the device extension */
    DeviceExtension = (PFDO_DEVICE_EXTENSION)Fdo->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(FDO_DEVICE_EXTENSION));

    DeviceExtension->ExtensionType = FdoExtension;

    DeviceExtension->Device = Fdo;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    DeviceExtension->PortNumber = PortNumber - 1;

    DeviceExtension->PnpState = dsStopped;

    KeInitializeTimer(&DeviceExtension->MiniportTimer);
    KeInitializeDpc(&DeviceExtension->MiniportTimerDpc,
                    PortMiniportTimerDpc,
                    DeviceExtension);
    KeInitializeDpc(&DeviceExtension->MiniportTimerRequestDpc,
                    PortMiniportTimerRequestDpc,
                    DeviceExtension);
    KeInitializeSpinLock(&DeviceExtension->MiniportTimerLock);
    KeInitializeSpinLock(&DeviceExtension->MiniportExLock);

    KeInitializeSpinLock(&DeviceExtension->PdoListLock);
    InitializeListHead(&DeviceExtension->PdoListHead);

    /* Attach the FDO to the device stack */
    Status = IoAttachDeviceToDeviceStackSafe(Fdo,
                                             PhysicalDeviceObject,
                                             &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoAttachDeviceToDeviceStackSafe() failed (Status 0x%08lx)\n", Status);
        IoDeleteDevice(Fdo);
        return Status;
    }

    /* Insert the FDO to the drivers FDO list */
    DriverObjectExtension = IoGetDriverObjectExtension(DriverObject,
                                                       (PVOID)DriverEntry);
    ASSERT(DriverObjectExtension->ExtensionType == DriverExtension);

    DeviceExtension->DriverExtension = DriverObjectExtension;

    KeAcquireInStackQueuedSpinLock(&DriverObjectExtension->AdapterListLock,
                                   &LockHandle);

    InsertHeadList(&DriverObjectExtension->AdapterListHead,
                   &DeviceExtension->AdapterListEntry);
    DriverObjectExtension->AdapterCount++;

    KeReleaseInStackQueuedSpinLock(&LockHandle);

    /* The device has been initialized */
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    DPRINT1("PortAddDevice() done (Status 0x%08lx)\n", Status);

    return Status;
}


static
VOID
NTAPI
PortUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDRIVER_OBJECT_EXTENSION DriverExtension;

    DPRINT1("PortUnload(%p)\n",
            DriverObject);

    DriverExtension = IoGetDriverObjectExtension(DriverObject,
                                                 (PVOID)DriverEntry);
    if (DriverExtension != NULL)
    {
        PortDeleteDriverInitData(DriverExtension);
    }
}


static
NTSTATUS
NTAPI
PortDispatchCreate(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    DPRINT("PortDispatchCreate(%p %p)\n", DeviceObject, Irp);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = FILE_OPENED;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


static
NTSTATUS
NTAPI
PortDispatchClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    DPRINT("PortDispatchClose(%p %p)\n", DeviceObject, Irp);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


static
NTSTATUS
PortQueryDeviceProperty(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension,
    _Inout_ PIRP Irp,
    _In_ PSTORAGE_PROPERTY_QUERY Query,
    _In_ ULONG OutputLength)
{
    PINQUIRYDATA InquiryData;
    PSTORAGE_DESCRIPTOR_HEADER Header;
    PSTORAGE_DEVICE_DESCRIPTOR Descriptor;
    PUCHAR Buffer;
    ULONG DescriptorLength;

    if ((Query->PropertyId != StorageDeviceProperty) ||
        (PdoExtension->InquiryBuffer == NULL))
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Query->QueryType == PropertyExistsQuery)
    {
        return STATUS_SUCCESS;
    }

    if (Query->QueryType != PropertyStandardQuery)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DescriptorLength = FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) +
                       8 + 1 + 16 + 1 + 4 + 1;

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Header = Irp->AssociatedIrp.SystemBuffer;
    Header->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
    Header->Size = DescriptorLength;
    Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);

    if (OutputLength < DescriptorLength)
    {
        return STATUS_SUCCESS;
    }

    InquiryData = PdoExtension->InquiryBuffer;
    Descriptor = Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Descriptor, DescriptorLength);

    Descriptor->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
    Descriptor->Size = DescriptorLength;
    Descriptor->DeviceType = InquiryData->DeviceType;
    Descriptor->DeviceTypeModifier = InquiryData->DeviceTypeModifier;
    Descriptor->RemovableMedia = InquiryData->RemovableMedia;
    Descriptor->CommandQueueing = InquiryData->CommandQueue;
    Descriptor->BusType = BusTypeSata;

    Buffer = Descriptor->RawDeviceProperties;
    Descriptor->VendorIdOffset = (ULONG)(Buffer - (PUCHAR)Descriptor);
    RtlCopyMemory(Buffer, InquiryData->VendorId, 8);
    Buffer += 8 + 1;

    Descriptor->ProductIdOffset = (ULONG)(Buffer - (PUCHAR)Descriptor);
    RtlCopyMemory(Buffer, InquiryData->ProductId, 16);
    Buffer += 16 + 1;

    Descriptor->ProductRevisionOffset = (ULONG)(Buffer - (PUCHAR)Descriptor);
    RtlCopyMemory(Buffer, InquiryData->ProductRevisionLevel, 4);
    Descriptor->SerialNumberOffset = 0;
    Descriptor->RawPropertiesLength = DescriptorLength -
                                      FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties);

    Irp->IoStatus.Information = DescriptorLength;
    return STATUS_SUCCESS;
}


static
NTSTATUS
PortQueryAdapterProperty(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _Inout_ PIRP Irp,
    _In_ PSTORAGE_PROPERTY_QUERY Query,
    _In_ ULONG OutputLength)
{
    PSTORAGE_ADAPTER_DESCRIPTOR Descriptor;
    PSTORAGE_DESCRIPTOR_HEADER Header;

    if (Query->PropertyId != StorageAdapterProperty)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Query->QueryType == PropertyExistsQuery)
    {
        return STATUS_SUCCESS;
    }

    if (Query->QueryType != PropertyStandardQuery)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Header = Irp->AssociatedIrp.SystemBuffer;
    Header->Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    Header->Size = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);

    if (OutputLength < sizeof(STORAGE_ADAPTER_DESCRIPTOR))
    {
        return STATUS_SUCCESS;
    }

    Descriptor = Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->Version = sizeof(*Descriptor);
    Descriptor->Size = sizeof(*Descriptor);
    Descriptor->MaximumTransferLength = FdoExtension->Miniport.PortConfig.MaximumTransferLength;
    Descriptor->MaximumPhysicalPages = FdoExtension->Miniport.PortConfig.NumberOfPhysicalBreaks + 1;
    Descriptor->AlignmentMask = 0;
    Descriptor->AdapterUsesPio = FALSE;
    Descriptor->AdapterScansDown = FALSE;
    Descriptor->CommandQueueing = FdoExtension->Miniport.PortConfig.TaggedQueuing;
    Descriptor->AcceleratedTransfer = TRUE;
    Descriptor->BusType = BusTypeSata;
    Descriptor->BusMajorVersion = 1;
    Descriptor->BusMinorVersion = 0;

    Irp->IoStatus.Information = sizeof(*Descriptor);
    return STATUS_SUCCESS;
}


static
NTSTATUS
NTAPI
PortDispatchDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PFDO_DEVICE_EXTENSION FdoDeviceExtension;
    PPDO_DEVICE_EXTENSION PdoDeviceExtension;
    PIO_STACK_LOCATION Stack;
    PSTORAGE_PROPERTY_QUERY Query;
    PROS_STORAGE_DUMP_INTERFACE DumpInterface;
    PSCSI_ADDRESS Address;
    EXTENSION_TYPE ExtensionType;
    ULONG IoControlCode;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    IoControlCode = Stack->Parameters.DeviceIoControl.IoControlCode;
    ExtensionType = *(EXTENSION_TYPE *)DeviceObject->DeviceExtension;

    DPRINT("PortDispatchDeviceControl(%p %p) ioctl 0x%08lx extension %u\n",
           DeviceObject, Irp, IoControlCode, ExtensionType);

    Irp->IoStatus.Information = 0;

    switch (IoControlCode)
    {
        case IOCTL_STORAGE_QUERY_PROPERTY:
            if ((Irp->AssociatedIrp.SystemBuffer == NULL) ||
                (Stack->Parameters.DeviceIoControl.InputBufferLength <
                 sizeof(STORAGE_PROPERTY_QUERY)))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Query = Irp->AssociatedIrp.SystemBuffer;
            if (ExtensionType == PdoExtension)
            {
                PdoDeviceExtension = DeviceObject->DeviceExtension;
                if (Query->PropertyId == StorageAdapterProperty)
                {
                    Status = PortQueryAdapterProperty(PdoDeviceExtension->FdoExtension,
                                                      Irp,
                                                      Query,
                                                      Stack->Parameters.DeviceIoControl.OutputBufferLength);
                }
                else
                {
                    Status = PortQueryDeviceProperty(PdoDeviceExtension,
                                                     Irp,
                                                     Query,
                                                     Stack->Parameters.DeviceIoControl.OutputBufferLength);
                }
            }
            else if (ExtensionType == FdoExtension)
            {
                FdoDeviceExtension = DeviceObject->DeviceExtension;
                Status = PortQueryAdapterProperty(FdoDeviceExtension,
                                                  Irp,
                                                  Query,
                                                  Stack->Parameters.DeviceIoControl.OutputBufferLength);
            }
            else
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
            }
            break;

        case IOCTL_SCSI_GET_ADDRESS:
            if (ExtensionType != PdoExtension)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            if ((Irp->AssociatedIrp.SystemBuffer == NULL) ||
                (Stack->Parameters.DeviceIoControl.OutputBufferLength <
                 sizeof(SCSI_ADDRESS)))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PdoDeviceExtension = DeviceObject->DeviceExtension;
            Address = Irp->AssociatedIrp.SystemBuffer;
            Address->Length = sizeof(*Address);
            Address->PortNumber = PdoDeviceExtension->FdoExtension->PortNumber;
            Address->PathId = (UCHAR)PdoDeviceExtension->Bus;
            Address->TargetId = (UCHAR)PdoDeviceExtension->Target;
            Address->Lun = (UCHAR)PdoDeviceExtension->Lun;
            Irp->IoStatus.Information = sizeof(*Address);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_REACTOS_STORAGE_GET_DUMP_INTERFACE:
            if (ExtensionType != PdoExtension)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            if ((Irp->AssociatedIrp.SystemBuffer == NULL) || (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ROS_STORAGE_DUMP_INTERFACE)))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PdoDeviceExtension = DeviceObject->DeviceExtension;
            DumpInterface = Irp->AssociatedIrp.SystemBuffer;
            Status = PortGetDumpInterface(PdoDeviceExtension, DumpInterface);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(*DumpInterface);
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}


static
NTSTATUS
NTAPI
PortDispatchScsi(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("PortDispatchScsi(%p %p)\n", DeviceObject, Irp);

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    DPRINT("ExtensionType: %u\n", DeviceExtension->ExtensionType);

    switch (DeviceExtension->ExtensionType)
    {
        case FdoExtension:
            return PortFdoScsi(DeviceObject,
                               Irp);

        case PdoExtension:
            return PortPdoScsi(DeviceObject,
                               Irp);

        default:
            Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}


static
NTSTATUS
NTAPI
PortDispatchSystemControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    DPRINT1("PortDispatchSystemControl(%p %p)\n",
            DeviceObject, Irp);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


static
NTSTATUS
NTAPI
PortDispatchPnp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("PortDispatchPnp(%p %p)\n", DeviceObject, Irp);

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    DPRINT("ExtensionType: %u\n", DeviceExtension->ExtensionType);

    switch (DeviceExtension->ExtensionType)
    {
        case FdoExtension:
            return PortFdoPnp(DeviceObject,
                              Irp);

        case PdoExtension:
            return PortPdoPnp(DeviceObject,
                              Irp);

        default:
            Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_UNSUCCESSFUL;
    }
}


static
NTSTATUS
NTAPI
PortDispatchPower(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    DPRINT1("PortDispatchPower(%p %p)\n",
            DeviceObject, Irp);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    DPRINT1("DriverEntry(%p %p)\n", DriverObject, RegistryPath);
    return STATUS_SUCCESS;
}


/*
 * @unimplemented
 */
STORPORT_API
PUCHAR
NTAPI
StorPortAllocateRegistryBuffer(
    _In_ PVOID HwDeviceExtension,
    _In_ PULONG Length)
{
    DPRINT1("StorPortAllocateRegistryBuffer()\n");
    UNIMPLEMENTED;
    return NULL;
}


/*
 * @implemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortBusy(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG RequestsToComplete)
{
    DPRINT1("StorPortBuzy()\n");
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
STORPORT_API
VOID
NTAPI
StorPortCompleteRequest(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ UCHAR SrbStatus)
{
    DPRINT1("StorPortCompleteRequest()\n");
    UNIMPLEMENTED;
}


/*
 * @implemented
 */
STORPORT_API
ULONG
NTAPI
StorPortConvertPhysicalAddressToUlong(
    _In_ STOR_PHYSICAL_ADDRESS Address)
{
    DPRINT1("StorPortConvertPhysicalAddressToUlong()\n");

    return Address.u.LowPart;
}


/*
 * @implemented
 */
STORPORT_API
STOR_PHYSICAL_ADDRESS
NTAPI
StorPortConvertUlongToPhysicalAddress(
    _In_ ULONG_PTR UlongAddress)
{
    STOR_PHYSICAL_ADDRESS Address;

    DPRINT1("StorPortConvertUlongToPhysicalAddress()\n");

    Address.QuadPart = UlongAddress;
    return Address;
}


/*
 * @implemented
 */
STORPORT_API
VOID
StorPortDebugPrint(
    _In_ ULONG DebugPrintLevel,
    _In_ PCHAR DebugMessage,
    ...)
{
    va_list ap;

    va_start(ap, DebugMessage);
    vDbgPrintExWithPrefix("STORMINI: ", 0x58, DebugPrintLevel, DebugMessage, ap);
    va_end(ap);
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortDeviceBusy(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG RequestsToComplete)
{
    DPRINT1("StorPortDeviceBusy()\n");
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortDeviceReady(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun)
{
    DPRINT1("StorPortDeviceReady()\n");
    UNIMPLEMENTED;
    return FALSE;
}


typedef struct _STORPORT_MINIPORT_TIMER
{
    KTIMER Timer;
    KDPC Dpc;
    PHW_TIMER_EX Callback;
    PVOID HwDeviceExtension;
    PVOID CallbackContext;
} STORPORT_MINIPORT_TIMER, *PSTORPORT_MINIPORT_TIMER;

typedef struct _STORPORT_STARTIO_PERFORMANCE_PARAMETERS_V2
{
    STARTIO_PERFORMANCE_PARAMETERS Parameters;
    PROCESSOR_NUMBER ProcessorNumber;
} STORPORT_STARTIO_PERFORMANCE_PARAMETERS_V2, *PSTORPORT_STARTIO_PERFORMANCE_PARAMETERS_V2;

#define STORPORT_PERF_SUPPORTED_FLAGS (STOR_PERF_DPC_REDIRECTION | \
                                       STOR_PERF_CONCURRENT_CHANNELS | \
                                       STOR_PERF_INTERRUPT_MESSAGE_RANGES | \
                                       STOR_PERF_ADV_CONFIG_LOCALITY | \
                                       STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO | \
                                       STOR_PERF_DPC_REDIRECTION_CURRENT_CPU | \
                                       STOR_PERF_NO_SGL | \
                                       STOR_PERF_SOFT_NUMA | \
                                       STOR_PERF_HETEROGENEOUS_CPU)

C_ASSERT(sizeof(STORPORT_STARTIO_PERFORMANCE_PARAMETERS_V2) == 20);
C_ASSERT(sizeof(STOR_EVENT) == sizeof(KEVENT));
C_ASSERT(FIELD_OFFSET(STOR_EVENT, Header.SignalState) == FIELD_OFFSET(KEVENT, Header.SignalState));
C_ASSERT(FIELD_OFFSET(STOR_EVENT, Header.WaitListHead) == FIELD_OFFSET(KEVENT, Header.WaitListHead));

static
VOID
NTAPI
PortMiniportTimerExDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PSTORPORT_MINIPORT_TIMER MiniportTimer = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (MiniportTimer && MiniportTimer->Callback)
        MiniportTimer->Callback(MiniportTimer->HwDeviceExtension, MiniportTimer->CallbackContext);
}

/*
 * @implemented
 */
STORPORT_API
ULONG
StorPortExtendedFunction(
    _In_ STORPORT_FUNCTION_CODE FunctionCode,
    _In_ PVOID HwDeviceExtension,
    ...)
{
    ULONG Status;
    va_list Args;

    DPRINT("StorPortExtendedFunction(%d)\n", FunctionCode);
    va_start(Args, HwDeviceExtension);
    switch (FunctionCode)
    {
        case ExtFunctionAllocatePool:
        {
            ULONG NumberOfBytes = va_arg(Args, ULONG);
            ULONG Tag = va_arg(Args, ULONG);
            PVOID *BufferPointer = va_arg(Args, PVOID *);

            if (!BufferPointer || !NumberOfBytes)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *BufferPointer = ExAllocatePoolWithTag(NonPagedPool, NumberOfBytes, Tag);
            Status = *BufferPointer ? STOR_STATUS_SUCCESS
                                    : STOR_STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        case ExtFunctionFreePool:
        {
            PVOID Buffer = va_arg(Args, PVOID);

            if (!Buffer)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            ExFreePool(Buffer);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetSystemAddress:
        {
            PVOID Srb = va_arg(Args, PVOID);
            PVOID *SystemAddress = va_arg(Args, PVOID *);
            PIRP Irp;

            if (!Srb || !SystemAddress)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            Irp = PortGetOriginalRequestFromSrb(Srb);
            if (Irp && Irp->MdlAddress)
                *SystemAddress = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
            else if (PortIsExtendedSrb(Srb))
                *SystemAddress = ((PSTORAGE_REQUEST_BLOCK)Srb)->DataBuffer;
            else
                *SystemAddress = ((PSCSI_REQUEST_BLOCK)Srb)->DataBuffer;
            Status = *SystemAddress ? STOR_STATUS_SUCCESS : STOR_STATUS_INVALID_PARAMETER;
            break;
        }

        case ExtFunctionGetOriginalMdl:
        {
            PVOID Srb = va_arg(Args, PVOID);
            PMDL *Mdl = va_arg(Args, PMDL *);
            PIRP Irp;

            if (!Srb || !Mdl)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            Irp = PortGetOriginalRequestFromSrb(Srb);
            *Mdl = Irp ? Irp->MdlAddress : NULL;
            Status = *Mdl ? STOR_STATUS_SUCCESS : STOR_STATUS_INVALID_PARAMETER;
            break;
        }

        case ExtFunctionQueryPerformanceCounter:
        {
            PLARGE_INTEGER PerformanceFrequency = va_arg(Args, PLARGE_INTEGER);
            PLARGE_INTEGER PerformanceCounter = va_arg(Args, PLARGE_INTEGER);

            if (!PerformanceCounter)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *PerformanceCounter = KeQueryPerformanceCounter(PerformanceFrequency);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionLogSystemEvent:
        {
            PSTOR_LOG_EVENT_DETAILS LogDetails = va_arg(Args, PSTOR_LOG_EVENT_DETAILS);

            /* No storage event-log channel yet, so report on the debugger */
            if (LogDetails)
            {
                DPRINT1("StorPort: miniport event 0x%08lx (unique 0x%08lx, dump %lu bytes)\n",
                        LogDetails->ErrorCode, LogDetails->UniqueId, LogDetails->DumpDataSize);
                if (LogDetails->DumpData && LogDetails->DumpDataSize >= sizeof(ULONG))
                {
                    PULONG Dump = LogDetails->DumpData;
                    DPRINT1("StorPort: event data[0] %08lx\n", Dump[0]);
                }
            }
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionInitializePerformanceOptimizations:
        {
            BOOLEAN Query = (BOOLEAN)va_arg(Args, ULONG);
            PPERF_CONFIGURATION_DATA PerfConfig = va_arg(Args, PPERF_CONFIGURATION_DATA);
            PMINIPORT_DEVICE_EXTENSION MiniportExtension;
            PFDO_DEVICE_EXTENSION DeviceExtension;

            if (!HwDeviceExtension || !PerfConfig)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            if (PerfConfig->Version < STOR_PERF_VERSION_2 || PerfConfig->Version > STOR_PERF_VERSION || PerfConfig->Size < sizeof(*PerfConfig))
            {
                Status = STOR_STATUS_UNSUPPORTED_VERSION;
                break;
            }

            MiniportExtension = CONTAINING_RECORD(HwDeviceExtension, MINIPORT_DEVICE_EXTENSION, HwDeviceExtension);
            DeviceExtension = MiniportExtension->Miniport->DeviceExtension;
            if (Query)
            {
                PerfConfig->Flags = STORPORT_PERF_SUPPORTED_FLAGS;
                Status = STOR_STATUS_SUCCESS;
                break;
            }
            if ((PerfConfig->Flags & ~STORPORT_PERF_SUPPORTED_FLAGS) != 0 ||
                ((PerfConfig->Flags & STOR_PERF_INTERRUPT_MESSAGE_RANGES) && !(PerfConfig->Flags & STOR_PERF_DPC_REDIRECTION)) ||
                ((PerfConfig->Flags & STOR_PERF_ADV_CONFIG_LOCALITY) && ((PerfConfig->Flags & (STOR_PERF_DPC_REDIRECTION | STOR_PERF_INTERRUPT_MESSAGE_RANGES)) != (STOR_PERF_DPC_REDIRECTION | STOR_PERF_INTERRUPT_MESSAGE_RANGES))) ||
                ((PerfConfig->Flags & (STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO | STOR_PERF_DPC_REDIRECTION_CURRENT_CPU)) && !(PerfConfig->Flags & STOR_PERF_DPC_REDIRECTION)))
            {
                Status = STOR_STATUS_UNSUCCESSFUL;
                break;
            }

            if (PerfConfig->Flags & STOR_PERF_ADV_CONFIG_LOCALITY)
            {
                PerfConfig->DeviceNode = 0;
                if (PerfConfig->MessageTargets && PerfConfig->LastRedirectionMessageNumber >= PerfConfig->FirstRedirectionMessageNumber)
                {
                    ULONG MessageCount = PerfConfig->LastRedirectionMessageNumber - PerfConfig->FirstRedirectionMessageNumber + 1;
                    ULONG MessageIndex;
                    KAFFINITY ActiveAffinity = KeQueryGroupAffinity(0);

                    for (MessageIndex = 0; MessageIndex < MessageCount; MessageIndex++)
                    {
                        PerfConfig->MessageTargets[MessageIndex].Mask = ActiveAffinity;
                        PerfConfig->MessageTargets[MessageIndex].Group = 0;
                        RtlZeroMemory(PerfConfig->MessageTargets[MessageIndex].Reserved, sizeof(PerfConfig->MessageTargets[MessageIndex].Reserved));
                    }
                }
            }

            DeviceExtension->PerfFlags = PerfConfig->Flags;
            DeviceExtension->PerfConcurrentChannels = (PerfConfig->Flags & STOR_PERF_CONCURRENT_CHANNELS) ? PerfConfig->ConcurrentChannels : 0;
            DeviceExtension->PerfConfigured = TRUE;
            DPRINT1("StorPort: performance options 0x%08lx, %lu channels\n", DeviceExtension->PerfFlags, DeviceExtension->PerfConcurrentChannels);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetStartIoPerformanceParameters:
        {
            PVOID Srb = va_arg(Args, PVOID);
            PSTARTIO_PERFORMANCE_PARAMETERS Parameters = va_arg(Args, PSTARTIO_PERFORMANCE_PARAMETERS);
            PSTORPORT_STARTIO_PERFORMANCE_PARAMETERS_V2 ParametersV2;
            PMINIPORT_DEVICE_EXTENSION MiniportExtension;
            PFDO_DEVICE_EXTENSION DeviceExtension;
            PROCESSOR_NUMBER ProcessorNumber;
            ULONG ProcessorIndex;

            UNREFERENCED_PARAMETER(Srb);
            if (!HwDeviceExtension || !Parameters || Parameters->Size < sizeof(*Parameters))
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }

            MiniportExtension = CONTAINING_RECORD(HwDeviceExtension, MINIPORT_DEVICE_EXTENSION, HwDeviceExtension);
            DeviceExtension = MiniportExtension->Miniport->DeviceExtension;
            ProcessorIndex = KeGetCurrentProcessorNumberEx(&ProcessorNumber);
            Parameters->MessageNumber = 0;
            Parameters->ChannelNumber = (DeviceExtension->PerfConfigured && DeviceExtension->PerfConcurrentChannels) ? ProcessorIndex % DeviceExtension->PerfConcurrentChannels : 0;
            if (Parameters->Version >= 2 && Parameters->Size >= sizeof(*ParametersV2))
            {
                ParametersV2 = (PSTORPORT_STARTIO_PERFORMANCE_PARAMETERS_V2)Parameters;
                ParametersV2->ProcessorNumber = ProcessorNumber;
            }
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionInitializeEvent:
        {
            PSTOR_EVENT Event = va_arg(Args, PSTOR_EVENT);
            STOR_EVENT_TYPE Type = va_arg(Args, STOR_EVENT_TYPE);
            BOOLEAN State = (BOOLEAN)va_arg(Args, ULONG);

            if (!HwDeviceExtension || !Event || (Type != StorNotificationEvent && Type != StorSynchronizationEvent))
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            KeInitializeEvent((PKEVENT)Event, (EVENT_TYPE)Type, State);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionWaitForEvent:
        {
            PVOID Object = va_arg(Args, PVOID);
            BOOLEAN Alertable = (BOOLEAN)va_arg(Args, ULONG);
            PLARGE_INTEGER Timeout = va_arg(Args, PLARGE_INTEGER);
            NTSTATUS WaitStatus;

            if (!HwDeviceExtension || !Object)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            if (KeGetCurrentIrql() > DISPATCH_LEVEL || ((Timeout == NULL || Timeout->QuadPart != 0) && KeGetCurrentIrql() > APC_LEVEL))
            {
                Status = STOR_STATUS_INVALID_IRQL;
                break;
            }
            WaitStatus = KeWaitForSingleObject(Object, Executive, KernelMode, Alertable, Timeout);
            Status = NT_SUCCESS(WaitStatus) ? STOR_STATUS_SUCCESS : STOR_STATUS_UNSUCCESSFUL;
            break;
        }

        case ExtFunctionSetEvent:
        {
            PSTOR_EVENT Event = va_arg(Args, PSTOR_EVENT);

            if (!HwDeviceExtension || !Event)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            KeSetEvent((PKEVENT)Event, IO_NO_INCREMENT, FALSE);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetCurrentProcessorNumber:
        {
            PPROCESSOR_NUMBER ProcessorNumber = va_arg(Args, PPROCESSOR_NUMBER);

            if (!ProcessorNumber)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            KeGetCurrentProcessorNumberEx(ProcessorNumber);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetActiveGroupCount:
        {
            PUSHORT GroupCount = va_arg(Args, PUSHORT);

            if (!GroupCount)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *GroupCount = KeQueryActiveGroupCount();
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetGroupAffinity:
        {
            ULONG GroupNumber = va_arg(Args, ULONG);
            PKAFFINITY GroupAffinity = va_arg(Args, PKAFFINITY);

            if (!GroupAffinity || GroupNumber >= KeQueryActiveGroupCount())
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *GroupAffinity = KeQueryGroupAffinity((USHORT)GroupNumber);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetActiveNodeCount:
        {
            PULONG NodeCount = va_arg(Args, PULONG);

            if (!NodeCount)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *NodeCount = 1;
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetNodeAffinity:
        {
            ULONG NodeNumber = va_arg(Args, ULONG);
            PGROUP_AFFINITY NodeAffinity = va_arg(Args, PGROUP_AFFINITY);

            if (!NodeAffinity || NodeNumber != 0)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            KeQueryNodeActiveAffinity((USHORT)NodeNumber, NodeAffinity, NULL);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetHighestNodeNumber:
        {
            PULONG HighestNode = va_arg(Args, PULONG);

            if (!HighestNode)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *HighestNode = 0;
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetLogicalProcessorRelationship:
        {
            PPROCESSOR_NUMBER ProcessorNumber = va_arg(Args, PPROCESSOR_NUMBER);
            LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType = va_arg(Args, LOGICAL_PROCESSOR_RELATIONSHIP);
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Information = va_arg(Args, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
            PULONG Length = va_arg(Args, PULONG);
            NTSTATUS QueryStatus;

            if (!Length)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            QueryStatus = KeQueryLogicalProcessorRelationship(ProcessorNumber, RelationshipType, Information, Length);
            if (NT_SUCCESS(QueryStatus))
                Status = STOR_STATUS_SUCCESS;
            else if (QueryStatus == STATUS_INFO_LENGTH_MISMATCH || QueryStatus == STATUS_BUFFER_TOO_SMALL)
                Status = STOR_STATUS_BUFFER_TOO_SMALL;
            else if (QueryStatus == STATUS_INVALID_PARAMETER)
                Status = STOR_STATUS_INVALID_PARAMETER;
            else
                Status = STOR_STATUS_UNSUCCESSFUL;
            break;
        }

        case ExtFunctionInitializeTimer:
        {
            PVOID *TimerHandle = va_arg(Args, PVOID *);
            PSTORPORT_MINIPORT_TIMER MiniportTimer;

            if (!TimerHandle)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            MiniportTimer = ExAllocatePoolWithTag(NonPagedPool, sizeof(*MiniportTimer), TAG_MINIPORT_DATA);
            if (!MiniportTimer)
            {
                Status = STOR_STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            RtlZeroMemory(MiniportTimer, sizeof(*MiniportTimer));
            KeInitializeTimerEx(&MiniportTimer->Timer, NotificationTimer);
            KeInitializeDpc(&MiniportTimer->Dpc, PortMiniportTimerExDpc, MiniportTimer);
            *TimerHandle = MiniportTimer;
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionRequestTimer:
        {
            PSTORPORT_MINIPORT_TIMER MiniportTimer = va_arg(Args, PVOID);
            PHW_TIMER_EX TimerCallback = va_arg(Args, PHW_TIMER_EX);
            PVOID CallbackContext = va_arg(Args, PVOID);
            ULONGLONG TimerValue = va_arg(Args, ULONGLONG);
            ULONGLONG TolerableDelay = va_arg(Args, ULONGLONG);
            LARGE_INTEGER DueTime;

            UNREFERENCED_PARAMETER(TolerableDelay);
            if (!MiniportTimer)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            if (TimerValue == 0)
            {
                KeCancelTimer(&MiniportTimer->Timer);
                Status = STOR_STATUS_SUCCESS;
                break;
            }
            MiniportTimer->Callback = TimerCallback;
            MiniportTimer->HwDeviceExtension = HwDeviceExtension;
            MiniportTimer->CallbackContext = CallbackContext;
            /* TimerValue is in microseconds */
            DueTime.QuadPart = -(LONGLONG)TimerValue * 10;
            KeSetTimer(&MiniportTimer->Timer, DueTime, &MiniportTimer->Dpc);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionFreeTimer:
        {
            PSTORPORT_MINIPORT_TIMER MiniportTimer = va_arg(Args, PVOID);

            if (!MiniportTimer)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            KeCancelTimer(&MiniportTimer->Timer);
            KeRemoveQueueDpc(&MiniportTimer->Dpc);
            ExFreePoolWithTag(MiniportTimer, TAG_MINIPORT_DATA);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionAllocateDmaMemory:
        {
            SIZE_T NumberOfBytes = va_arg(Args, SIZE_T);
            PHYSICAL_ADDRESS LowestAcceptableAddress = va_arg(Args, PHYSICAL_ADDRESS);
            PHYSICAL_ADDRESS HighestAcceptableAddress = va_arg(Args, PHYSICAL_ADDRESS);
            PHYSICAL_ADDRESS BoundaryAddressMultiple = va_arg(Args, PHYSICAL_ADDRESS);
            MEMORY_CACHING_TYPE CacheType = va_arg(Args, MEMORY_CACHING_TYPE);
            ULONG PreferredNode = va_arg(Args, ULONG);
            PVOID *BufferPointer = va_arg(Args, PVOID *);
            PPHYSICAL_ADDRESS PhysicalAddress = va_arg(Args, PPHYSICAL_ADDRESS);

            UNREFERENCED_PARAMETER(PreferredNode);
            if (!BufferPointer || !PhysicalAddress || !NumberOfBytes)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *BufferPointer = MmAllocateContiguousMemorySpecifyCache(NumberOfBytes, LowestAcceptableAddress, HighestAcceptableAddress, BoundaryAddressMultiple, CacheType);
            if (*BufferPointer == NULL)
            {
                PhysicalAddress->QuadPart = 0;
                Status = STOR_STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            RtlZeroMemory(*BufferPointer, NumberOfBytes);
            *PhysicalAddress = MmGetPhysicalAddress(*BufferPointer);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionFreeDmaMemory:
        {
            PVOID BaseAddress = va_arg(Args, PVOID);
            SIZE_T NumberOfBytes = va_arg(Args, SIZE_T);
            MEMORY_CACHING_TYPE CacheType = va_arg(Args, MEMORY_CACHING_TYPE);

            UNREFERENCED_PARAMETER(NumberOfBytes);
            UNREFERENCED_PARAMETER(CacheType);
            if (!BaseAddress)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            MmFreeContiguousMemory(BaseAddress);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionDelayExecution:
        {
            ULONG DelayInMicroseconds = va_arg(Args, ULONG);

            if (KeGetCurrentIrql() < DISPATCH_LEVEL)
            {
                LARGE_INTEGER Interval;
                Interval.QuadPart = -(LONGLONG)DelayInMicroseconds * 10;
                KeDelayExecutionThread(KernelMode, FALSE, &Interval);
            }
            else
            {
                KeStallExecutionProcessor(DelayInMicroseconds);
            }
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetProcessorIndexFromNumber:
        {
            PPROCESSOR_NUMBER ProcessorNumber = va_arg(Args, PPROCESSOR_NUMBER);
            PULONG ProcessorIndex = va_arg(Args, PULONG);
            KAFFINITY GroupAffinity;

            if (!ProcessorNumber || !ProcessorIndex || ProcessorNumber->Group != 0)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            GroupAffinity = KeQueryGroupAffinity(0);
            if (ProcessorNumber->Number >= sizeof(KAFFINITY) * 8 || !(GroupAffinity & ((KAFFINITY)1 << ProcessorNumber->Number)))
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *ProcessorIndex = ProcessorNumber->Number;
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        /*
         * A DRAM-less controller keeps its mapping tables in host memory, so
         * it asks the port for a physically contiguous buffer to hand back as
         * its Host Memory Buffer. The preferred node is a hint and is ignored:
         * these are single-node systems.
         */
        case ExtFunctionAllocateContiguousMemorySpecifyCacheNode:
        {
            SIZE_T NumberOfBytes = va_arg(Args, SIZE_T);
            PHYSICAL_ADDRESS LowestAcceptableAddress = va_arg(Args, PHYSICAL_ADDRESS);
            PHYSICAL_ADDRESS HighestAcceptableAddress = va_arg(Args, PHYSICAL_ADDRESS);
            PHYSICAL_ADDRESS BoundaryAddressMultiple = va_arg(Args, PHYSICAL_ADDRESS);
            MEMORY_CACHING_TYPE CacheType = va_arg(Args, MEMORY_CACHING_TYPE);
            NODE_REQUIREMENT PreferredNode = va_arg(Args, NODE_REQUIREMENT);
            PVOID *BufferPointer = va_arg(Args, PVOID *);

            UNREFERENCED_PARAMETER(PreferredNode);
            if (!BufferPointer || NumberOfBytes == 0)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }

            *BufferPointer = MmAllocateContiguousMemorySpecifyCache(NumberOfBytes, LowestAcceptableAddress, HighestAcceptableAddress, BoundaryAddressMultiple, CacheType);
            Status = *BufferPointer ? STOR_STATUS_SUCCESS
                                    : STOR_STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        case ExtFunctionFreeContiguousMemorySpecifyCache:
        {
            PVOID BaseAddress = va_arg(Args, PVOID);
            SIZE_T NumberOfBytes = va_arg(Args, SIZE_T);
            MEMORY_CACHING_TYPE CacheType = va_arg(Args, MEMORY_CACHING_TYPE);

            if (!BaseAddress || NumberOfBytes == 0)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }

            MmFreeContiguousMemorySpecifyCache(BaseAddress, NumberOfBytes, CacheType);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionBuildMdlForNonPagedPool:
        {
            PMDL Mdl = va_arg(Args, PMDL);

            if (!Mdl)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }

            MmBuildMdlForNonPagedPool(Mdl);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetMessageInterruptInformation:
        {
            ULONG MessageId = va_arg(Args, ULONG);
            PMESSAGE_INTERRUPT_INFORMATION InterruptInfo = va_arg(Args, PMESSAGE_INTERRUPT_INFORMATION);
            PIO_INTERRUPT_MESSAGE_INFO_ENTRY Entry;
            PMINIPORT_DEVICE_EXTENSION MiniportExtension;
            PFDO_DEVICE_EXTENSION DeviceExtension;

            MiniportExtension = CONTAINING_RECORD(HwDeviceExtension, MINIPORT_DEVICE_EXTENSION, HwDeviceExtension);
            DeviceExtension = MiniportExtension->Miniport->DeviceExtension;
            if (!InterruptInfo)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            if (!DeviceExtension->MessageInfo || MessageId >= DeviceExtension->MessageInfo->MessageCount)
            {
                Status = STOR_STATUS_UNSUCCESSFUL;
                break;
            }

            Entry = &DeviceExtension->MessageInfo->MessageInfo[MessageId];
            InterruptInfo->MessageId = MessageId;
            InterruptInfo->MessageData = Entry->MessageData;
            InterruptInfo->MessageAddress = Entry->MessageAddress;
            InterruptInfo->InterruptVector = Entry->Vector;
            InterruptInfo->InterruptLevel = Entry->Irql;
            InterruptInfo->InterruptMode = Entry->Mode;
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        /*
         * The lock guarding a message is its interrupt object's own spin
         * lock: acquiring it holds off that message's service routine, which
         * is the entire point, and works at DIRQL where a plain spin lock
         * cannot.
         */
        case ExtFunctionAcquireMSISpinLock:
        {
            ULONG MessageId = va_arg(Args, ULONG);
            PULONG OldIrql = va_arg(Args, PULONG);
            PMINIPORT_DEVICE_EXTENSION MiniportExtension;
            PFDO_DEVICE_EXTENSION DeviceExtension;

            MiniportExtension = CONTAINING_RECORD(HwDeviceExtension, MINIPORT_DEVICE_EXTENSION, HwDeviceExtension);
            DeviceExtension = MiniportExtension->Miniport->DeviceExtension;
            if (!OldIrql)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            if (!DeviceExtension->MessageInfo || MessageId >= DeviceExtension->MessageInfo->MessageCount)
            {
                Status = STOR_STATUS_UNSUCCESSFUL;
                break;
            }

            *OldIrql = KeAcquireInterruptSpinLock(DeviceExtension->MessageInfo->MessageInfo[MessageId].InterruptObject);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionReleaseMSISpinLock:
        {
            ULONG MessageId = va_arg(Args, ULONG);
            ULONG OldIrql = va_arg(Args, ULONG);
            PMINIPORT_DEVICE_EXTENSION MiniportExtension;
            PFDO_DEVICE_EXTENSION DeviceExtension;

            MiniportExtension = CONTAINING_RECORD(HwDeviceExtension, MINIPORT_DEVICE_EXTENSION, HwDeviceExtension);
            DeviceExtension = MiniportExtension->Miniport->DeviceExtension;
            if (!DeviceExtension->MessageInfo || MessageId >= DeviceExtension->MessageInfo->MessageCount)
            {
                Status = STOR_STATUS_UNSUCCESSFUL;
                break;
            }

            KeReleaseInterruptSpinLock(DeviceExtension->MessageInfo->MessageInfo[MessageId].InterruptObject, (KIRQL)OldIrql);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetPfns:
        {
            PVOID Srb = va_arg(Args, PVOID);
            PMDL Mdl = va_arg(Args, PMDL);
            PVOID *Pfns = va_arg(Args, PVOID *);
            PULONG PfnCount = va_arg(Args, PULONG);
            PULONG StartingOffset = va_arg(Args, PULONG);
            PIRP Irp;

            if (!Srb || !Mdl || !Pfns || !PfnCount || !StartingOffset)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            Irp = PortGetOriginalRequestFromSrb(Srb);
            if (!Irp || Irp->MdlAddress != Mdl)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *Pfns = MmGetMdlPfnArray(Mdl);
            *PfnCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), MmGetMdlByteCount(Mdl));
            *StartingOffset = MmGetMdlByteOffset(Mdl);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetCurrentProcessorIndex:
        {
            PULONG ProcessorIndex = va_arg(Args, PULONG);

            if (!ProcessorIndex)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *ProcessorIndex = KeGetCurrentProcessorNumberEx(NULL);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionAcquireSpinLock:
        {
            STOR_SPINLOCK SpinLock = va_arg(Args, STOR_SPINLOCK);
            PVOID LockContext = va_arg(Args, PVOID);
            PSTOR_LOCK_HANDLE LockHandle = va_arg(Args, PSTOR_LOCK_HANDLE);
            PMINIPORT_DEVICE_EXTENSION MiniportExtension;

            if (!HwDeviceExtension || !LockHandle)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            MiniportExtension = CONTAINING_RECORD(HwDeviceExtension, MINIPORT_DEVICE_EXTENSION, HwDeviceExtension);
            /* Paired with the ReleaseSpinLock notification */
            PortAcquireSpinLock(MiniportExtension->Miniport->DeviceExtension, SpinLock, LockContext, LockHandle);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetProcessorCount:
        {
            PULONG ProcessorCount = va_arg(Args, PULONG);

            if (!ProcessorCount)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *ProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionQueryConfiguration:
        {
            ULONG Type = va_arg(Args, ULONG);
            PBOOLEAN Enabled = va_arg(Args, PBOOLEAN);

            UNREFERENCED_PARAMETER(Type);
            if (!Enabled)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            /* No optional platform configuration is enabled */
            *Enabled = FALSE;
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionSetFeatureList:
        {
            ULONG FeatureCount = va_arg(Args, ULONG);
            PBOOLEAN FeatureList = va_arg(Args, PBOOLEAN);

            if (!FeatureList || FeatureCount == 0)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            /* The miniport's feature choices are accepted as-is */
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        case ExtFunctionGetCurrentIrql:
        {
            PKIRQL Irql = va_arg(Args, PKIRQL);

            if (!Irql)
            {
                Status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }
            *Irql = KeGetCurrentIrql();
            Status = STOR_STATUS_SUCCESS;
            break;
        }

        default:
            DPRINT1("StorPortExtendedFunction(%d %p ...) not supported\n",
                    FunctionCode, HwDeviceExtension);
            Status = STOR_STATUS_NOT_IMPLEMENTED;
            break;
    }
    va_end(Args);

    return Status;
}


/*
 * @implemented
 */
STORPORT_API
VOID
NTAPI
StorPortFreeDeviceBase(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID MappedAddress)
{
    DPRINT1("StorPortFreeDeviceBase(%p %p)\n",
            HwDeviceExtension, MappedAddress);
}


/*
 * @unimplemented
 */
STORPORT_API
VOID
NTAPI
StorPortFreeRegistryBuffer(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR Buffer)
{
    DPRINT1("StorPortFreeRegistryBuffer()\n");
    UNIMPLEMENTED;
}


/*
 * @implemented
 */
STORPORT_API
ULONG
NTAPI
StorPortGetBusData(
    _In_ PVOID DeviceExtension,
    _In_ ULONG BusDataType,
    _In_ ULONG SystemIoBusNumber,
    _In_ ULONG SlotNumber,
    _Out_ _When_(Length != 0, _Out_writes_bytes_(Length)) PVOID Buffer,
    _In_ ULONG Length)
{
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;
    PBUS_INTERFACE_STANDARD Interface;
    ULONG ReturnLength;

    DPRINT1("StorPortGetBusData(%p %lu %lu %lu %p %lu)\n",
            DeviceExtension, BusDataType, SystemIoBusNumber, SlotNumber, Buffer, Length);

    /* Get the miniport extension */
    MiniportExtension = CONTAINING_RECORD(DeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    DPRINT1("DeviceExtension %p  MiniportExtension %p\n",
            DeviceExtension, MiniportExtension);

    Interface = &MiniportExtension->Miniport->DeviceExtension->BusInterface;

    if (BusDataType == 4)
        BusDataType = 0;

    ReturnLength = Interface->GetBusData(Interface->Context,
                                         BusDataType,
                                         Buffer,
                                         0,
                                         Length);
    DPRINT1("ReturnLength: %lu\n", ReturnLength);

    return ReturnLength;
}


/*
 * @implemented
 */
STORPORT_API
PVOID
NTAPI
StorPortGetDeviceBase(
    _In_ PVOID HwDeviceExtension,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG SystemIoBusNumber,
    _In_ STOR_PHYSICAL_ADDRESS IoAddress,
    _In_ ULONG NumberOfBytes,
    _In_ BOOLEAN InIoSpace)
{
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;
    PHYSICAL_ADDRESS TranslatedAddress;
    PVOID MappedAddress;
    NTSTATUS Status;

    DPRINT1("StorPortGetDeviceBase(%p %lu %lu 0x%I64x %lu %u)\n",
            HwDeviceExtension, BusType, SystemIoBusNumber, IoAddress.QuadPart, NumberOfBytes, InIoSpace);

    /* Get the miniport extension */
    MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    DPRINT1("HwDeviceExtension %p  MiniportExtension %p\n",
            HwDeviceExtension, MiniportExtension);

    if (!TranslateResourceListAddress(MiniportExtension->Miniport->DeviceExtension,
                                      BusType,
                                      SystemIoBusNumber,
                                      IoAddress,
                                      NumberOfBytes,
                                      InIoSpace,
                                      &TranslatedAddress))
    {
        DPRINT1("Checkpoint!\n");
        return NULL;
    }

    DPRINT1("Translated Address: 0x%I64x\n", TranslatedAddress.QuadPart);

    /* In I/O space */
    if (InIoSpace)
    {
        DPRINT1("Translated Address: %p\n", (PVOID)(ULONG_PTR)TranslatedAddress.QuadPart);
        return (PVOID)(ULONG_PTR)TranslatedAddress.QuadPart;
    }

    /* In memory space */
    MappedAddress = MmMapIoSpace(TranslatedAddress,
                                 NumberOfBytes,
                                 FALSE);
    DPRINT1("Mapped Address: %p\n", MappedAddress);

    Status = AllocateAddressMapping(&MiniportExtension->Miniport->DeviceExtension->MappedAddressList,
                                    IoAddress,
                                    MappedAddress,
                                    NumberOfBytes,
                                    SystemIoBusNumber);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Checkpoint!\n");
        MappedAddress = NULL;
    }

    DPRINT1("Mapped Address: %p\n", MappedAddress);
    return MappedAddress;
}


/*
 * @unimplemented
 */
STORPORT_API
PVOID
NTAPI
StorPortGetLogicalUnit(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun)
{
    DPRINT1("StorPortGetLogicalUnit()\n");
    UNIMPLEMENTED;
    return NULL;
}


/*
 * @implemented
 */
STORPORT_API
STOR_PHYSICAL_ADDRESS
NTAPI
StorPortGetPhysicalAddress(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_ PVOID VirtualAddress,
    _Out_ ULONG *Length)
{
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;
    PFDO_DEVICE_EXTENSION DeviceExtension;
    STOR_PHYSICAL_ADDRESS PhysicalAddress;
    ULONG_PTR Offset;

    DPRINT("StorPortGetPhysicalAddress(%p %p %p %p)\n",
           HwDeviceExtension, Srb, VirtualAddress, Length);

    /* Get the miniport extension */
    MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    DPRINT("HwDeviceExtension %p  MiniportExtension %p\n",
           HwDeviceExtension, MiniportExtension);

    DeviceExtension = MiniportExtension->Miniport->DeviceExtension;

    /* Inside of the uncached extension? */
    if (((ULONG_PTR)VirtualAddress >= (ULONG_PTR)DeviceExtension->UncachedExtensionVirtualBase) &&
        ((ULONG_PTR)VirtualAddress < (ULONG_PTR)DeviceExtension->UncachedExtensionVirtualBase + DeviceExtension->UncachedExtensionSize))
    {
        Offset = (ULONG_PTR)VirtualAddress - (ULONG_PTR)DeviceExtension->UncachedExtensionVirtualBase;

        PhysicalAddress.QuadPart = DeviceExtension->UncachedExtensionPhysicalBase.QuadPart + Offset;
        *Length = DeviceExtension->UncachedExtensionSize - Offset;

        return PhysicalAddress;
    }

    /*
     * Driver-owned nonpaged buffers are valid DMA sources as well.  Report
     * the physically contiguous remainder of the current page so a miniport
     * can construct an SG list without assuming that virtual contiguity also
     * means physical contiguity.
     */
    PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);
    *Length = PAGE_SIZE - BYTE_OFFSET(VirtualAddress);

    return PhysicalAddress;
}


/*
 * @unimplemented
 */
STORPORT_API
PSTOR_SCATTER_GATHER_LIST
NTAPI
StorPortGetScatterGatherList(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PSTOR_SRB_CONTEXT SrbContext;
    PIRP Irp;

    UNREFERENCED_PARAMETER(DeviceExtension);

    /* The list was built for this request in PortPdoScsi and lives in the
     * per-request context anchored on the IRP. */
    if (Srb == NULL)
        return NULL;

    Irp = PortGetOriginalRequestFromSrb(Srb);
    if (Irp == NULL)
        return NULL;

    SrbContext = PortGetSrbContext(Irp);
    if (SrbContext == NULL)
        return NULL;

    return SrbContext->Sgl;
}


/*
 * @implemented
 */
STORPORT_API
PSCSI_REQUEST_BLOCK
NTAPI
StorPortGetSrb(
    _In_ PVOID DeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ LONG QueueTag)
{
    DPRINT("StorPortGetSrb()\n");
    return NULL;
}


/*
 * @implemented
 */
STORPORT_API
PVOID
NTAPI
StorPortGetUncachedExtension(
    _In_ PVOID HwDeviceExtension,
    _In_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _In_ ULONG NumberOfBytes)
{
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PHYSICAL_ADDRESS LowestAddress, HighestAddress, Alignment;

    DPRINT1("StorPortGetUncachedExtension(%p %p %lu)\n",
            HwDeviceExtension, ConfigInfo, NumberOfBytes);

    /* Get the miniport extension */
    MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    DPRINT1("HwDeviceExtension %p  MiniportExtension %p\n",
            HwDeviceExtension, MiniportExtension);

    DeviceExtension = MiniportExtension->Miniport->DeviceExtension;

    /* Return the uncached extension base address if we already have one */
    if (DeviceExtension->UncachedExtensionVirtualBase != NULL)
        return DeviceExtension->UncachedExtensionVirtualBase;

    // FIXME: Set DMA stuff here?

    /* Allocate the uncached extension */
    Alignment.QuadPart = 0;
    LowestAddress.QuadPart = 0;
    HighestAddress.QuadPart = 0x00000000FFFFFFFF;
    /* Cached: the miniport treats this common buffer as normal memory, which
     * a Device-memory mapping would forbid on ARM64 */
    DeviceExtension->UncachedExtensionVirtualBase = MmAllocateContiguousMemorySpecifyCache(NumberOfBytes,
                                                                                           LowestAddress,
                                                                                           HighestAddress,
                                                                                           Alignment,
                                                                                           MmCached);
    if (DeviceExtension->UncachedExtensionVirtualBase == NULL)
        return NULL;

    DeviceExtension->UncachedExtensionPhysicalBase = MmGetPhysicalAddress(DeviceExtension->UncachedExtensionVirtualBase);
    DeviceExtension->UncachedExtensionSize = NumberOfBytes;

    return DeviceExtension->UncachedExtensionVirtualBase;
}


/*
 * @unimplemented
 */
STORPORT_API
PVOID
NTAPI
StorPortGetVirtualAddress(
    _In_ PVOID HwDeviceExtension,
    _In_ STOR_PHYSICAL_ADDRESS PhysicalAddress)
{
    DPRINT1("StorPortGetVirtualAddress(%p %I64x)\n",
            HwDeviceExtension, PhysicalAddress.QuadPart);
    UNIMPLEMENTED;
    return NULL;
}


/*
 * @implemented
 */
STORPORT_API
ULONG
NTAPI
StorPortInitialize(
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ struct _HW_INITIALIZATION_DATA *HwInitializationData,
    _In_opt_ PVOID HwContext)
{
    PDRIVER_OBJECT DriverObject = (PDRIVER_OBJECT)Argument1;
    PUNICODE_STRING RegistryPath = (PUNICODE_STRING)Argument2;
    PDRIVER_OBJECT_EXTENSION DriverObjectExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT1("StorPortInitialize(%p %p %p %p)\n",
            Argument1, Argument2, HwInitializationData, HwContext);

    DPRINT1("HwInitializationDataSize: %lu\n", HwInitializationData->HwInitializationDataSize);
    DPRINT1("AdapterInterfaceType: %u\n", HwInitializationData->AdapterInterfaceType);
    DPRINT1("HwInitialize: %p\n", HwInitializationData->HwInitialize);
    DPRINT1("HwStartIo: %p\n", HwInitializationData->HwStartIo);
    DPRINT1("HwInterrupt: %p\n", HwInitializationData->HwInterrupt);
    DPRINT1("HwFindAdapter: %p\n", HwInitializationData->HwFindAdapter);
    DPRINT1("HwResetBus: %p\n", HwInitializationData->HwResetBus);
    DPRINT1("HwDmaStarted: %p\n", HwInitializationData->HwDmaStarted);
    DPRINT1("HwAdapterState: %p\n", HwInitializationData->HwAdapterState);
    DPRINT1("DeviceExtensionSize: %lu\n", HwInitializationData->DeviceExtensionSize);
    DPRINT1("SpecificLuExtensionSize: %lu\n", HwInitializationData->SpecificLuExtensionSize);
    DPRINT1("SrbExtensionSize: %lu\n", HwInitializationData->SrbExtensionSize);
    DPRINT1("NumberOfAccessRanges: %lu\n", HwInitializationData->NumberOfAccessRanges);
    if (HwInitializationData->HwInitializationDataSize >= FIELD_OFFSET(HW_INITIALIZATION_DATA, Reserved1) + sizeof(ULONG))
    {
        DPRINT1("FeatureSupport: 0x%08lx SrbTypeFlags: 0x%08lx AddressTypeFlags: 0x%08lx\n",
                HwInitializationData->FeatureSupport,
                HwInitializationData->SrbTypeFlags,
                HwInitializationData->AddressTypeFlags);
    }

    /* Check parameters */
    if ((DriverObject == NULL) ||
        (RegistryPath == NULL) ||
        (HwInitializationData == NULL))
    {
        DPRINT1("Invalid parameter!\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* Check initialization data */
    if ((HwInitializationData->HwInitializationDataSize < sizeof(HW_INITIALIZATION_DATA)) ||
        (HwInitializationData->HwInitialize == NULL) ||
        (HwInitializationData->HwStartIo == NULL) ||
        (HwInitializationData->HwFindAdapter == NULL) ||
        (HwInitializationData->HwResetBus == NULL))
    {
        DPRINT1("Revision mismatch!\n");
        return STATUS_REVISION_MISMATCH;
    }

    DriverObjectExtension = IoGetDriverObjectExtension(DriverObject,
                                                       (PVOID)DriverEntry);
    if (DriverObjectExtension == NULL)
    {
        DPRINT1("No driver object extension!\n");

        Status = IoAllocateDriverObjectExtension(DriverObject,
                                                 (PVOID)DriverEntry,
                                                 sizeof(DRIVER_OBJECT_EXTENSION),
                                                 (PVOID *)&DriverObjectExtension);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IoAllocateDriverObjectExtension() failed (Status 0x%08lx)\n", Status);
            return Status;
        }

        DPRINT1("Driver object extension created!\n");

        /* Initialize the driver object extension */
        RtlZeroMemory(DriverObjectExtension,
                      sizeof(DRIVER_OBJECT_EXTENSION));

        DriverObjectExtension->ExtensionType = DriverExtension;
        DriverObjectExtension->DriverObject = DriverObject;

        InitializeListHead(&DriverObjectExtension->AdapterListHead);
        KeInitializeSpinLock(&DriverObjectExtension->AdapterListLock);

        InitializeListHead(&DriverObjectExtension->InitDataListHead);

        /* Set handlers */
        DriverObject->DriverExtension->AddDevice = PortAddDevice;
//        DriverObject->DriverStartIo = PortStartIo;
        DriverObject->DriverUnload = PortUnload;
        DriverObject->MajorFunction[IRP_MJ_CREATE] = PortDispatchCreate;
        DriverObject->MajorFunction[IRP_MJ_CLOSE] = PortDispatchClose;
        DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PortDispatchDeviceControl;
        DriverObject->MajorFunction[IRP_MJ_SCSI] = PortDispatchScsi;
        DriverObject->MajorFunction[IRP_MJ_POWER] = PortDispatchPower;
        DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = PortDispatchSystemControl;
        DriverObject->MajorFunction[IRP_MJ_PNP] = PortDispatchPnp;
    }

    /* Add the initialzation data to the driver extension */
    Status = PortAddDriverInitData(DriverObjectExtension,
                                   HwInitializationData);

    DPRINT1("StorPortInitialize() done (Status 0x%08lx)\n", Status);

    return Status;
}


/*
 * @unimplemented
 */
STORPORT_API
VOID
NTAPI
StorPortLogError(
    _In_ PVOID HwDeviceExtension,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG ErrorCode,
    _In_ ULONG UniqueId)
{
    DPRINT1("ScsiPortLogError() called\n");
    DPRINT1("PathId: 0x%02x  TargetId: 0x%02x  Lun: 0x%02x  ErrorCode: 0x%08lx  UniqueId: 0x%08lx\n",
            PathId, TargetId, Lun, ErrorCode, UniqueId);

    DPRINT1("ScsiPortLogError() done\n");
}


/*
 * @implemented
 */
STORPORT_API
VOID
NTAPI
StorPortMoveMemory(
    _Out_writes_bytes_(Length) PVOID Destination,
    _In_reads_bytes_(Length) PVOID Source,
    _In_ ULONG Length)
{
    RtlMoveMemory(Destination, Source, Length);
}


/*
 * @unimplemented
 */
STORPORT_API
VOID
StorPortNotification(
    _In_ SCSI_NOTIFICATION_TYPE NotificationType,
    _In_ PVOID HwDeviceExtension,
    ...)
{
    PMINIPORT_DEVICE_EXTENSION MiniportExtension = NULL;
    PFDO_DEVICE_EXTENSION DeviceExtension = NULL;
    PHW_PASSIVE_INITIALIZE_ROUTINE HwPassiveInitRoutine;
    PHW_TIMER HwTimerRoutine;
    PSTORPORT_EXTENDED_FUNCTIONS *ppExtendedFunctions;
    PBOOLEAN Result;
    PSTOR_DPC Dpc;
    PHW_DPC_ROUTINE HwDpcRoutine;
    va_list ap;

    STOR_SPINLOCK SpinLock;
    PVOID LockContext;
    PSTOR_LOCK_HANDLE LockHandle;
    PSCSI_REQUEST_BLOCK Srb;
    PVOID SystemArgument1;
    PVOID SystemArgument2;
    PLONG DpcResult;
    ULONG TimerValue;
    KIRQL OldIrql;

    DPRINT("StorPortNotification(%x %p)\n",
           NotificationType, HwDeviceExtension);

    /* Get the miniport extension */
    if (HwDeviceExtension != NULL)
    {
        MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                              MINIPORT_DEVICE_EXTENSION,
                                              HwDeviceExtension);
        DPRINT("HwDeviceExtension %p  MiniportExtension %p\n",
               HwDeviceExtension, MiniportExtension);

        DeviceExtension = MiniportExtension->Miniport->DeviceExtension;
    }

    va_start(ap, HwDeviceExtension);

    switch (NotificationType)
    {
        case RequestComplete:
        {
            PSTORAGE_REQUEST_BLOCK ExtendedSrb;
            PSRBEX_DATA_SCSI_CDB16 ScsiData;
            PSTOR_SRB_CONTEXT SrbContext;
            PIRP Irp;

            DPRINT("RequestComplete\n");
            Srb = (PSCSI_REQUEST_BLOCK)va_arg(ap, PSCSI_REQUEST_BLOCK);
            if (Srb != NULL)
                DPRINT("StorPort: RequestComplete %p function 0x%02x status 0x%02x\n", Srb, Srb->Function, Srb->SrbStatus);
            Irp = Srb != NULL ? PortGetOriginalRequestFromSrb(Srb) : NULL;
            if (Irp != NULL)
            {
                if (PortIsExtendedSrb(Srb))
                {
                    ExtendedSrb = (PSTORAGE_REQUEST_BLOCK)Srb;
                    SrbContext = PortGetSrbContext(Irp);
                    if (SrbContext == NULL || SrbContext->LegacySrb == NULL)
                        break;

                    Srb = SrbContext->LegacySrb;
                    Srb->SrbStatus = ExtendedSrb->SrbStatus;
                    Srb->DataTransferLength = ExtendedSrb->DataTransferLength;
                    if (ExtendedSrb->SrbFunction == SRB_FUNCTION_EXECUTE_SCSI && ExtendedSrb->NumSrbExData != 0 && ExtendedSrb->SrbExDataOffset[0] >= sizeof(*ExtendedSrb) && ExtendedSrb->SrbExDataOffset[0] + sizeof(*ScsiData) <= ExtendedSrb->SrbLength)
                    {
                        ScsiData = (PSRBEX_DATA_SCSI_CDB16)((PUCHAR)ExtendedSrb + ExtendedSrb->SrbExDataOffset[0]);
                        if (ScsiData->Type == SrbExDataTypeScsiCdb16)
                        {
                            Srb->ScsiStatus = ScsiData->ScsiStatus;
                            Srb->SenseInfoBufferLength = ScsiData->SenseInfoBufferLength;
                        }
                    }
                }

                if (Irp->Tail.Overlay.DriverContext[3] == PORT_DUMP_IRP_MARKER)
                {
                    PPORT_DUMP_CONTEXT DumpContext = Irp->Tail.Overlay.DriverContext[2];

                    DumpContext->Status = PortSrbStatusToNtStatus(Srb->SrbStatus);
                    KeMemoryBarrier();
                    InterlockedExchange(&DumpContext->Completed, 1);
                    break;
                }

                if ((DeviceExtension != NULL) && DeviceExtension->DumpMode)
                {
                    break;
                }

                /*
                 * The miniport is done with this request. Translate the SRB
                 * status, release the per-request state the port driver
                 * allocated in PortPdoScsi and complete the IRP.
                 */
                Irp->IoStatus.Status = PortSrbStatusToNtStatus(Srb->SrbStatus);
                Irp->IoStatus.Information = NT_SUCCESS(Irp->IoStatus.Status) ? Srb->DataTransferLength : 0;

                Srb->SrbExtension = NULL;

                PortFreeSrbContext(Irp);

                IoCompleteRequest(Irp, IO_NO_INCREMENT);
            }
            break;
        }

        case IssueDpc:
            DPRINT("IssueDpc\n");
            Dpc = (PSTOR_DPC)va_arg(ap, PSTOR_DPC);
            SystemArgument1 = (PVOID)va_arg(ap, PVOID);
            SystemArgument2 = (PVOID)va_arg(ap, PVOID);
            DpcResult = (PLONG)va_arg(ap, PLONG);

            /* Without this the miniport's completion DPC never runs. */
            if (DpcResult != NULL)
                *DpcResult = FALSE;

            if (Dpc != NULL)
            {
                if ((DeviceExtension != NULL) && DeviceExtension->DumpMode)
                {
                    HwDpcRoutine = (PHW_DPC_ROUTINE)Dpc->Dpc.DeferredRoutine;
                    HwDpcRoutine(Dpc, HwDeviceExtension, SystemArgument1, SystemArgument2);
                    if (DpcResult != NULL)
                        *DpcResult = TRUE;
                }
                else
                {
                    BOOLEAN Queued = KeInsertQueueDpc((PRKDPC)&Dpc->Dpc, SystemArgument1, SystemArgument2);
                    if (DpcResult != NULL)
                        *DpcResult = (LONG)Queued;
                }
            }
            break;

        case RequestTimerCall:
        {
            BOOLEAN QueueRequestDpc;

            HwTimerRoutine = (PHW_TIMER)va_arg(ap, PHW_TIMER);
            TimerValue = (ULONG)va_arg(ap, ULONG);

            if (DeviceExtension == NULL)
                break;

            QueueRequestDpc = FALSE;
            OldIrql = PortAcquireTimerLock(DeviceExtension);
            DeviceExtension->MiniportTimerArmed = FALSE;
            DeviceExtension->MiniportTimerRequestedRoutine = HwTimerRoutine;
            DeviceExtension->MiniportTimerRequestedValue = TimerValue;
            DeviceExtension->MiniportTimerRequestPending = TRUE;
            if (!DeviceExtension->MiniportTimerRequestDpcActive)
            {
                DeviceExtension->MiniportTimerRequestDpcActive = TRUE;
                QueueRequestDpc = TRUE;
            }
            PortReleaseTimerLock(DeviceExtension, OldIrql);

            if (QueueRequestDpc)
            {
                KeInsertQueueDpc(&DeviceExtension->MiniportTimerRequestDpc,
                                 NULL,
                                 NULL);
            }
            break;
        }

        case GetExtendedFunctionTable:
            DPRINT1("GetExtendedFunctionTable\n");
            ppExtendedFunctions = (PSTORPORT_EXTENDED_FUNCTIONS*)va_arg(ap, PSTORPORT_EXTENDED_FUNCTIONS*);
            if (ppExtendedFunctions != NULL)
                *ppExtendedFunctions = NULL; /* FIXME */
            break;

        case EnablePassiveInitialization:
            DPRINT1("EnablePassiveInitialization\n");
            HwPassiveInitRoutine = (PHW_PASSIVE_INITIALIZE_ROUTINE)va_arg(ap, PHW_PASSIVE_INITIALIZE_ROUTINE);
            DPRINT1("HwPassiveInitRoutine %p\n", HwPassiveInitRoutine);
            Result = (PBOOLEAN)va_arg(ap, PBOOLEAN);

            *Result = FALSE;

            if ((DeviceExtension != NULL) &&
                (DeviceExtension->HwPassiveInitRoutine == NULL))
            {
                DeviceExtension->HwPassiveInitRoutine = HwPassiveInitRoutine;
                *Result = TRUE;
            }
            break;

        case InitializeDpc:
            DPRINT1("InitializeDpc\n");
            Dpc = (PSTOR_DPC)va_arg(ap, PSTOR_DPC);
            DPRINT1("Dpc %p\n", Dpc);
            HwDpcRoutine = (PHW_DPC_ROUTINE)va_arg(ap, PHW_DPC_ROUTINE);
            DPRINT1("HwDpcRoutine %p\n", HwDpcRoutine);

            /* The miniport's DPC routine receives its own HwDeviceExtension as
             * the deferred context, not the port driver's FDO extension. */
            KeInitializeDpc((PRKDPC)&Dpc->Dpc,
                            (PKDEFERRED_ROUTINE)HwDpcRoutine,
                            HwDeviceExtension);
            KeInitializeSpinLock(&Dpc->Lock);
            break;

        case AcquireSpinLock:
            DPRINT("AcquireSpinLock\n");
            SpinLock = (STOR_SPINLOCK)va_arg(ap, STOR_SPINLOCK);
            DPRINT("SpinLock %lu\n", SpinLock);
            LockContext = (PVOID)va_arg(ap, PVOID);
            DPRINT("LockContext %p\n", LockContext);
            LockHandle = (PSTOR_LOCK_HANDLE)va_arg(ap, PSTOR_LOCK_HANDLE);
            DPRINT("LockHandle %p\n", LockHandle);
            PortAcquireSpinLock(DeviceExtension,
                                SpinLock,
                                LockContext,
                                LockHandle);
            break;

        case ReleaseSpinLock:
            DPRINT("ReleaseSpinLock\n");
            LockHandle = (PSTOR_LOCK_HANDLE)va_arg(ap, PSTOR_LOCK_HANDLE);
            DPRINT("LockHandle %p\n", LockHandle);
            PortReleaseSpinLock(DeviceExtension,
                                LockHandle);
            break;

        case MarkDeviceFailedEx:
        {
            PSTOR_ADDRESS StorAddress = (PSTOR_ADDRESS)va_arg(ap, PSTOR_ADDRESS);
            ULONG Flags = va_arg(ap, ULONG);
            ULONG FaultCode = va_arg(ap, ULONG);
            PWSTR FaultDescription = (PWSTR)va_arg(ap, PWSTR);
            ULONG AdditionalDataSize = va_arg(ap, ULONG);
            PUCHAR AdditionalData = (PUCHAR)va_arg(ap, PUCHAR);
            ULONG CriticalDataSize = va_arg(ap, ULONG);
            PUCHAR CriticalData = (PUCHAR)va_arg(ap, PUCHAR);
            PULONG Status = (PULONG)va_arg(ap, PULONG);

            UNREFERENCED_PARAMETER(StorAddress);
            UNREFERENCED_PARAMETER(AdditionalData);
            UNREFERENCED_PARAMETER(CriticalData);
            DPRINT1("StorPort: MarkDeviceFailedEx flags 0x%08lx fault 0x%04lx additional %lu critical %lu\n",
                    Flags, FaultCode, AdditionalDataSize, CriticalDataSize);
            if (FaultDescription)
                DPRINT1("StorPort: device failure: %S\n", FaultDescription);
            if (Status)
                *Status = STOR_STATUS_NOT_IMPLEMENTED;
            break;
        }

        default:
            DPRINT1("Unsupported Notification %lx\n", NotificationType);
            break;
    }

    va_end(ap);
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortPause(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG TimeOut)
{
    DPRINT1("StorPortPause()\n");
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortPauseDevice(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG TimeOut)
{
    DPRINT1("StorPortPauseDevice()\n");
    UNIMPLEMENTED;
    return FALSE;
}


#if defined(_M_AMD64) || defined(_M_ARM64)
/*
 * @implemented
 */
/* KeQuerySystemTime is an inline function,
   so we cannot forward the export to ntoskrnl */
STORPORT_API
VOID
NTAPI
StorPortQuerySystemTime(
    _Out_ PLARGE_INTEGER CurrentTime)
{
    DPRINT1("StorPortQuerySystemTime(%p)\n", CurrentTime);

    KeQuerySystemTime(CurrentTime);
}
#endif /* defined(_M_AMD64) || defined(_M_ARM64) */


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortReady(
    _In_ PVOID HwDeviceExtension)
{
    DPRINT1("StorPortReady()\n");
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortRegistryRead(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR ValueName,
    _In_ ULONG Global,
    _In_ ULONG Type,
    _In_ PUCHAR Buffer,
    _In_ PULONG BufferLength)
{
    DPRINT1("StorPortRegistryRead()\n");
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortRegistryWrite(
    _In_ PVOID HwDeviceExtension,
    _In_ PUCHAR ValueName,
    _In_ ULONG Global,
    _In_ ULONG Type,
    _In_ PUCHAR Buffer,
    _In_ ULONG BufferLength)
{
    DPRINT1("StorPortRegistryWrite()\n");
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortResume(
    _In_ PVOID HwDeviceExtension)
{
    DPRINT1("StorPortResume()\n");
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortResumeDevice(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun)
{
    DPRINT1("StorPortResumeDevice()\n");
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @implemented
 */
STORPORT_API
ULONG
NTAPI
StorPortSetBusDataByOffset(
    _In_ PVOID DeviceExtension,
    _In_ ULONG BusDataType,
    _In_ ULONG SystemIoBusNumber,
    _In_ ULONG SlotNumber,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;
    PBUS_INTERFACE_STANDARD Interface;
    ULONG ReturnLength;

    DPRINT1("StorPortSetBusData(%p %lu %lu %lu %p %lu %lu)\n",
            DeviceExtension, BusDataType, SystemIoBusNumber, SlotNumber, Buffer, Offset, Length);

    MiniportExtension = CONTAINING_RECORD(DeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    DPRINT1("DeviceExtension %p  MiniportExtension %p\n",
            DeviceExtension, MiniportExtension);

    Interface = &MiniportExtension->Miniport->DeviceExtension->BusInterface;

    ReturnLength = Interface->SetBusData(Interface->Context,
                                         BusDataType,
                                         Buffer,
                                         Offset,
                                         Length);
    DPRINT1("ReturnLength: %lu\n", ReturnLength);

    return ReturnLength;
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortSetDeviceQueueDepth(
    _In_ PVOID HwDeviceExtension,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG Depth)
{
    BOOLEAN Found;
    PLIST_ENTRY Entry;
    KLOCK_QUEUE_HANDLE LockHandle;
    PPDO_DEVICE_EXTENSION PdoExtension;
    PFDO_DEVICE_EXTENSION FdoExtension;
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;

    if ((HwDeviceExtension == NULL) || (Depth == 0))
    {
        return FALSE;
    }

    MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    FdoExtension = MiniportExtension->Miniport->DeviceExtension;
    Found = FALSE;

    KeAcquireInStackQueuedSpinLock(&FdoExtension->PdoListLock, &LockHandle);

    for (Entry = FdoExtension->PdoListHead.Flink;
         Entry != &FdoExtension->PdoListHead;
         Entry = Entry->Flink)
    {
        PdoExtension = CONTAINING_RECORD(Entry,
                                         PDO_DEVICE_EXTENSION,
                                         PdoListEntry);

        if ((PdoExtension->Bus == PathId) &&
            (PdoExtension->Target == TargetId) &&
            (PdoExtension->Lun == Lun))
        {
            PdoExtension->QueueDepth = Depth;
            Found = TRUE;
            break;
        }
    }

    KeReleaseInStackQueuedSpinLock(&LockHandle);

    DPRINT("StorPortSetDeviceQueueDepth(%u:%u:%u) depth %lu: %s\n",
           PathId,
           TargetId,
           Lun,
           Depth,
           Found ? "set" : "device not found");

    return Found;
}


/*
 * @implemented
 */
STORPORT_API
VOID
NTAPI
StorPortStallExecution(
    _In_ ULONG Delay)
{
    KeStallExecutionProcessor(Delay);
}


/*
 * @unimplemented
 */
STORPORT_API
VOID
NTAPI
StorPortSynchronizeAccess(
    _In_ PVOID HwDeviceExtension,
    _In_ PSTOR_SYNCHRONIZED_ACCESS SynchronizedAccessRoutine,
    _In_opt_ PVOID Context)
{
    DPRINT1("StorPortSynchronizeAccess()\n");
    UNIMPLEMENTED;
}


/*
 * @implemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortValidateRange(
    _In_ PVOID HwDeviceExtension,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG SystemIoBusNumber,
    _In_ STOR_PHYSICAL_ADDRESS IoAddress,
    _In_ ULONG NumberOfBytes,
    _In_ BOOLEAN InIoSpace)
{
    DPRINT1("StorPortValidateRange()\n");
    return TRUE;
}

/* EOF */
