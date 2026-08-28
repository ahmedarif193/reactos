/*
 * PROJECT:         ReactOS Raspberry Pi 5 HDMI Audio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Driver entry and PortCls adapter registration
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "private.h"

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag)
{
    PVOID Allocation = ExAllocatePoolWithTag(PoolType, Size, Tag);
    if (Allocation)
        RtlZeroMemory(Allocation, Size);
    return Allocation;
}

void
__cdecl
operator delete(PVOID Allocation)
{
    if (Allocation)
        ExFreePool(Allocation);
}

void
__cdecl
operator delete(PVOID Allocation, UINT_PTR)
{
    if (Allocation)
        ExFreePool(Allocation);
}

static NTSTATUS
InstallSubdevice(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PCWSTR Name,
    REFGUID PortClassId,
    PUNKNOWN Miniport,
    CRpi5HdmiAdapter *Adapter,
    PRESOURCELIST ResourceList,
    PUNKNOWN *PortUnknown)
{
    PPORT Port;
    NTSTATUS Status;

    Status = PcNewPort(&Port, PortClassId);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Port->Init(DeviceObject, Irp, Miniport, static_cast<PUNKNOWN>(Adapter), ResourceList);
    if (NT_SUCCESS(Status))
        Status = PcRegisterSubdevice(DeviceObject, const_cast<PWSTR>(Name), Port);
    if (NT_SUCCESS(Status) && PortUnknown)
        Status = Port->QueryInterface(IID_IUnknown, reinterpret_cast<PVOID *>(PortUnknown));

    Port->Release();
    return Status;
}

static NTSTATUS
NTAPI
Rpi5HdmiStartDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRESOURCELIST ResourceList)
{
    PRPI5HDMI_DEVICE_EXTENSION DeviceExtension;
    CRpi5HdmiAdapter *Adapter;
    PUNKNOWN TopologyMiniport = NULL;
    PUNKNOWN WaveMiniport = NULL;
    PUNKNOWN TopologyPort = NULL;
    PUNKNOWN WavePort = NULL;
    NTSTATUS Status;

    DeviceExtension = static_cast<PRPI5HDMI_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
    Adapter = new (NonPagedPool, TAG_RPI5HDMI) CRpi5HdmiAdapter();
    if (!Adapter)
        return STATUS_INSUFFICIENT_RESOURCES;
    Adapter->AddRef();

    Status = Adapter->Initialize(DeviceObject, ResourceList);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = Rpi5HdmiCreateTopology(&TopologyMiniport, Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = InstallSubdevice(
        DeviceObject,
        Irp,
        L"Topology",
        CLSID_PortTopology,
        TopologyMiniport,
        Adapter,
        ResourceList,
        &TopologyPort);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = Rpi5HdmiCreateWave(&WaveMiniport, Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = InstallSubdevice(
        DeviceObject,
        Irp,
        L"Wave",
        CLSID_PortWaveRT,
        WaveMiniport,
        Adapter,
        ResourceList,
        &WavePort);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = PcRegisterPhysicalConnection(DeviceObject, WavePort, 1, TopologyPort, 0);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    DeviceExtension->Adapter = Adapter;

Cleanup:
    if (WavePort)
        WavePort->Release();
    if (TopologyPort)
        TopologyPort->Release();
    if (WaveMiniport)
        WaveMiniport->Release();
    if (TopologyMiniport)
        TopologyMiniport->Release();
    if (!NT_SUCCESS(Status))
        Adapter->Release();
    return Status;
}

static NTSTATUS
NTAPI
Rpi5HdmiAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    if (!DriverObject || !PhysicalDeviceObject)
        return STATUS_INVALID_PARAMETER;

    return PcAddAdapterDevice(
        DriverObject,
        PhysicalDeviceObject,
        Rpi5HdmiStartDevice,
        2,
        sizeof(RPI5HDMI_DEVICE_EXTENSION));
}

static NTSTATUS
NTAPI
Rpi5HdmiPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MinorFunction == IRP_MN_STOP_DEVICE ||
        Stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL ||
        Stack->MinorFunction == IRP_MN_REMOVE_DEVICE)
    {
        PRPI5HDMI_DEVICE_EXTENSION DeviceExtension =
            static_cast<PRPI5HDMI_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
        CRpi5HdmiAdapter *Adapter = DeviceExtension->Adapter;
        if (Adapter)
        {
            DeviceExtension->Adapter = NULL;
            Adapter->Stop();
            Adapter->Release();
        }
    }

    return PcDispatchIrp(DeviceObject, Irp);
}

static VOID
NTAPI
Rpi5HdmiUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C"
NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status = PcInitializeAdapterDriver(DriverObject, RegistryPath, Rpi5HdmiAddDevice);
    if (NT_SUCCESS(Status))
    {
        DriverObject->DriverUnload = Rpi5HdmiUnload;
        DriverObject->MajorFunction[IRP_MJ_PNP] = Rpi5HdmiPnp;
    }
    return Status;
}
