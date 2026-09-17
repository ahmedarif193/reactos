/*
 * PROJECT:     ReactOS kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V firmware PCI root bus PnP bridge
 *
 * The HAL has already validated the QEMU virt ECAM and bus range in its FDT.
 * Report the host as a PnP child so the ordinary pci.sys driver can enumerate
 * devices. This is a platform bus, not a replacement PCI enumerator.
 */

#include <ntoskrnl.h>

#define RISCV_PLATFORM_FDO 0x52504644UL
#define RISCV_PLATFORM_PDO 0x52505044UL
#define RISCV_PLATFORM_TAG 'bPvR'

typedef struct _RISCV_PLATFORM_EXTENSION
{
    ULONG Type;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT ChildDevice;
    ULONG FirstBus;
    ULONG LastBus;
} RISCV_PLATFORM_EXTENSION;

NTHALAPI BOOLEAN NTAPI HalpRiscvGetPciBusRange(PULONG FirstBus, PULONG LastBus);

static NTSTATUS
IopRiscvCopyId(_In_reads_bytes_(Size) const WCHAR *Source,
               _In_ SIZE_T Size,
               _Out_ PULONG_PTR Information)
{
    PWCHAR Copy = ExAllocatePoolWithTag(PagedPool, Size, RISCV_PLATFORM_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Source, Size);
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static NTSTATUS
IopRiscvPciResources(_In_ RISCV_PLATFORM_EXTENSION *Extension,
                     _Out_ PULONG_PTR Information)
{
    PCM_RESOURCE_LIST List = ExAllocatePoolWithTag(PagedPool, sizeof(*List), RISCV_PLATFORM_TAG);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, sizeof(*List));
    List->Count = 1;
    List->List[0].InterfaceType = PCIBus;
    List->List[0].BusNumber = Extension->FirstBus;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = 1;
    Resource = &List->List[0].PartialResourceList.PartialDescriptors[0];
    Resource->Type = CmResourceTypeBusNumber;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->u.BusNumber.Start = Extension->FirstBus;
    Resource->u.BusNumber.Length = Extension->LastBus - Extension->FirstBus + 1;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static NTSTATUS
IopRiscvPciRequirements(_In_ RISCV_PLATFORM_EXTENSION *Extension,
                        _Out_ PULONG_PTR Information)
{
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Resource;

    List = ExAllocatePoolWithTag(PagedPool, sizeof(*List), RISCV_PLATFORM_TAG);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, sizeof(*List));
    List->ListSize = sizeof(*List);
    List->InterfaceType = PCIBus;
    List->BusNumber = Extension->FirstBus;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = 1;
    Resource = &List->List[0].Descriptors[0];
    Resource->Type = CmResourceTypeBusNumber;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->u.BusNumber.Length = Extension->LastBus - Extension->FirstBus + 1;
    Resource->u.BusNumber.MinBusNumber = Extension->FirstBus;
    Resource->u.BusNumber.MaxBusNumber = Extension->LastBus;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
IopRiscvPlatformPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    RISCV_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    if (Extension->Type == RISCV_PLATFORM_FDO)
    {
        if (Stack->MinorFunction == IRP_MN_QUERY_DEVICE_RELATIONS &&
            Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
        {
            PDEVICE_RELATIONS Relations;

            Relations = ExAllocatePoolWithTag(PagedPool, sizeof(*Relations), RISCV_PLATFORM_TAG);
            if (!Relations)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Complete;
            }
            Relations->Count = 1;
            Relations->Objects[0] = Extension->ChildDevice;
            ObReferenceObject(Extension->ChildDevice);
            Irp->IoStatus.Information = (ULONG_PTR)Relations;
            Status = STATUS_SUCCESS;
            goto Complete;
        }
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->LowerDevice, Irp);
    }

    if (Extension->Type != RISCV_PLATFORM_PDO)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Complete;
    }

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                {
                    static const WCHAR DeviceId[] = L"ACPI\\PNP0A03";
                    Status = IopRiscvCopyId(DeviceId, sizeof(DeviceId), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryHardwareIDs:
                case BusQueryCompatibleIDs:
                {
                    static const WCHAR HardwareIds[] = L"*PNP0A03\0";
                    Status = IopRiscvCopyId(HardwareIds, sizeof(HardwareIds), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryInstanceID:
                {
                    static const WCHAR InstanceId[] = L"0";
                    Status = IopRiscvCopyId(InstanceId, sizeof(InstanceId), &Irp->IoStatus.Information);
                    break;
                }
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = IopRiscvPciResources(Extension, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = IopRiscvPciRequirements(Extension, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 ||
                Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->UniqueID = TRUE;
            Capabilities->Address = 0;
            Capabilities->UINumber = 0;
            Capabilities->SilentInstall = TRUE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_START_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }

Complete:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS NTAPI
IopRiscvPlatformPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    RISCV_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    if (Extension->Type == RISCV_PLATFORM_FDO)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(Extension->LowerDevice, Irp);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
IopRiscvPlatformAddDevice(_In_ PDRIVER_OBJECT DriverObject,
                          _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    RISCV_PLATFORM_EXTENSION *Extension;
    PDEVICE_OBJECT Fdo, Child;
    NTSTATUS Status;
    ULONG FirstBus, LastBus;

    if (!HalpRiscvGetPciBusRange(&FirstBus, &LastBus))
        return STATUS_NO_SUCH_DEVICE;

    Status = IoCreateDevice(DriverObject, sizeof(*Extension), NULL,
                            FILE_DEVICE_BUS_EXTENDER, 0, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    Extension->Type = RISCV_PLATFORM_FDO;
    Extension->FirstBus = FirstBus;
    Extension->LastBus = LastBus;
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    Status = IoCreateDevice(DriverObject, sizeof(*Extension), NULL,
                            FILE_DEVICE_BUS_EXTENDER, FILE_AUTOGENERATED_DEVICE_NAME,
                            FALSE, &Child);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(Extension->LowerDevice);
        IoDeleteDevice(Fdo);
        return Status;
    }
    Extension->ChildDevice = Child;
    Extension = Child->DeviceExtension;
    Extension->Type = RISCV_PLATFORM_PDO;
    Extension->FirstBus = FirstBus;
    Extension->LastBus = LastBus;
    Child->Flags &= ~DO_DEVICE_INITIALIZING;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
IopRiscvPlatformDriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                            _In_ PUNICODE_STRING RegistryPath)
{
    PDEVICE_OBJECT RootDevice = NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverExtension->AddDevice = IopRiscvPlatformAddDevice;
    DriverObject->MajorFunction[IRP_MJ_PNP] = IopRiscvPlatformPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = IopRiscvPlatformPower;

    Status = IoReportDetectedDevice(DriverObject, InterfaceTypeUndefined,
                                    MAXULONG, MAXULONG, NULL, NULL,
                                    FALSE, &RootDevice);
    if (!NT_SUCCESS(Status))
        return Status;
    RootDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return IopRiscvPlatformAddDevice(DriverObject, RootDevice);
}

NTSTATUS NTAPI
IopRiscvInitializePlatformBus(VOID)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\RISCV_PLATFORM");
    ULONG FirstBus, LastBus;

    if (!HalpRiscvGetPciBusRange(&FirstBus, &LastBus))
        return STATUS_SUCCESS;
    return IoCreateDriver(&DriverName, IopRiscvPlatformDriverEntry);
}
