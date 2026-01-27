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

#include <ntddstor.h>
#include <ntddscsi.h>
#include <limits.h>

/* MACROS *********************************************************************/

#ifndef ALIGN_UP_BY
#define ALIGN_UP_BY(length, alignment) (((length) + ((alignment) - 1)) & ~((alignment) - 1))
#endif


/* GLOBALS ********************************************************************/

ULONG PortNumber = 0;


/* FUNCTIONS ******************************************************************/

static
NTSTATUS
PortAddDriverInitData(
    PDRIVER_OBJECT_EXTENSION DriverExtension,
    PHW_INITIALIZATION_DATA HwInitializationData)
{
    PDRIVER_INIT_DATA InitData;

    InitData = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(DRIVER_INIT_DATA),
                                     TAG_INIT_DATA);
    if (InitData == NULL)
        return STATUS_NO_MEMORY;

    RtlCopyMemory(&InitData->HwInitData,
                  HwInitializationData,
                  sizeof(HW_INITIALIZATION_DATA));

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

    DPRINT("PortDeleteDriverInitData()\n");

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

    DPRINT("PortGetDriverInitData()\n");

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
    LockHandle->Lock = SpinLock;

    switch (SpinLock)
    {
        case DpcLock: /* 1 */
            break;

        case StartIoLock: /* 2 */
            break;

        case InterruptLock: /* 3 */
            if (DeviceExtension->Interrupt == NULL)
            {
                LockHandle->Context.OldIrql = 0;
            }
            else
            {
                LockHandle->Context.OldIrql = KeAcquireInterruptSpinLock(DeviceExtension->Interrupt);
            }
            break;
    }
}


static
VOID
PortReleaseSpinLock(
    PFDO_DEVICE_EXTENSION DeviceExtension,
    PSTOR_LOCK_HANDLE LockHandle)
{
    switch (LockHandle->Lock)
    {
        case DpcLock: /* 1 */
            break;

        case StartIoLock: /* 2 */
            break;

        case InterruptLock: /* 3 */
            if (DeviceExtension->Interrupt != NULL)
            {
                KeReleaseInterruptSpinLock(DeviceExtension->Interrupt,
                                           LockHandle->Context.OldIrql);
            }
            break;
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

    DPRINT("PortAddDevice(%p %p)\n",
            DriverObject, PhysicalDeviceObject);

    ASSERT(DriverObject);
    ASSERT(PhysicalDeviceObject);

    _snwprintf(NameBuffer, RTL_NUMBER_OF(NameBuffer), L"\\Device\\RaidPort%lu", PortNumber);
    NameBuffer[RTL_NUMBER_OF(NameBuffer) - 1] = UNICODE_NULL;
    RtlInitUnicodeString(&DeviceName, NameBuffer);
    PortNumber++;

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

    Fdo->Flags |= DO_DIRECT_IO;
    Fdo->Flags |= DO_POWER_PAGABLE;

    DeviceExtension = (PFDO_DEVICE_EXTENSION)Fdo->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(FDO_DEVICE_EXTENSION));

    DeviceExtension->ExtensionType = FdoExtension;

    DeviceExtension->Device = Fdo;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;

    DeviceExtension->PnpState = dsStopped;

    KeInitializeSpinLock(&DeviceExtension->PdoListLock);
    InitializeListHead(&DeviceExtension->PdoListHead);

    /* Initialize SRB extension pool spinlock early */
    KeInitializeSpinLock(&DeviceExtension->SrbExtensionPool.Lock);

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
                                                       DriverObject);
    if (DriverObjectExtension == NULL)
    {
        DPRINT1("Failed to get driver object extension in PortAddDevice!\n");
        IoDeleteDevice(Fdo);
        return STATUS_UNSUCCESSFUL;
    }
    ASSERT(DriverObjectExtension->ExtensionType == DriverExtension);

    DeviceExtension->DriverExtension = DriverObjectExtension;

    KeAcquireInStackQueuedSpinLock(&DriverObjectExtension->AdapterListLock,
                                   &LockHandle);

    InsertHeadList(&DriverObjectExtension->AdapterListHead,
                   &DeviceExtension->AdapterListEntry);
    DriverObjectExtension->AdapterCount++;

    KeReleaseInStackQueuedSpinLock(&LockHandle);

    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    return Status;
}


static
VOID
NTAPI
PortUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDRIVER_OBJECT_EXTENSION DriverExtension;

    DPRINT("PortUnload(%p)\n",
            DriverObject);

    DriverExtension = IoGetDriverObjectExtension(DriverObject,
                                                 DriverObject);
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
    DPRINT("PortDispatchCreate(%p %p)\n",
            DeviceObject, Irp);

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
    DPRINT("PortDispatchClose(%p %p)\n",
            DeviceObject, Irp);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


static
NTSTATUS
NTAPI
PortDispatchDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PVOID deviceExtension;
    EXTENSION_TYPE type;

    deviceExtension = DeviceObject->DeviceExtension;
    if (deviceExtension == NULL)
    {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    type = *((EXTENSION_TYPE *)deviceExtension);

    switch (type)
    {
        case FdoExtension:
            return PortFdoDeviceControl((PFDO_DEVICE_EXTENSION)deviceExtension, Irp);

        case PdoExtension:
            return PortPdoDeviceControl((PPDO_DEVICE_EXTENSION)deviceExtension, Irp);

        default:
            Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}


static
NTSTATUS
NTAPI
PortDispatchScsi(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;

    //DPRINT("PortDispatchScsi(%p %p)\n", DeviceObject, Irp);

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    //DPRINT("ExtensionType: %u\n", DeviceExtension->ExtensionType);

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
    DPRINT("PortDispatchSystemControl(%p %p)\n",
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

    DPRINT("PortDispatchPnp(%p %p)\n",
            DeviceObject, Irp);

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
    DPRINT("PortDispatchPower(%p %p)\n",
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
    DPRINT1("STORPORT DriverEntry: DriverObject=%p RegistryPath=%p\n",
            DriverObject,
            RegistryPath);
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
    DPRINT("StorPortAllocateRegistryBuffer()\n");
    UNIMPLEMENTED;
    return NULL;
}


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortBusy(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG RequestsToComplete)
{
    DPRINT("StorPortBuzy()\n");
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
    DPRINT("StorPortCompleteRequest()\n");
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
    DPRINT("StorPortConvertPhysicalAddressToUlong()\n");

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

    DPRINT("StorPortConvertUlongToPhysicalAddress()\n");

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
    DPRINT("StorPortDeviceBusy()\n");
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
    DPRINT("StorPortDeviceReady()\n");
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
STORPORT_API
ULONG
StorPortExtendedFunction(
    _In_ STORPORT_FUNCTION_CODE FunctionCode,
    _In_ PVOID HwDeviceExtension,
    ...)
{
    DPRINT("StorPortExtendedFunction(%d %p ...)\n",
            FunctionCode, HwDeviceExtension);
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
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
    DPRINT("StorPortFreeDeviceBase(%p %p)\n",
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
    DPRINT("StorPortFreeRegistryBuffer()\n");
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

    DPRINT("StorPortGetBusData(%p %lu %lu %lu %p %lu)\n",
            DeviceExtension, BusDataType, SystemIoBusNumber, SlotNumber, Buffer, Length);

    /* Get the miniport extension */
    MiniportExtension = CONTAINING_RECORD(DeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    DPRINT("DeviceExtension %p  MiniportExtension %p\n",
            DeviceExtension, MiniportExtension);

    Interface = &MiniportExtension->Miniport->DeviceExtension->BusInterface;

    if (BusDataType == 4)
        BusDataType = 0;

    ReturnLength = Interface->GetBusData(Interface->Context,
                                         BusDataType,
                                         Buffer,
                                         0,
                                         Length);
    DPRINT("ReturnLength: %lu\n", ReturnLength);

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

    DPRINT("StorPortGetDeviceBase(%p %lu %lu 0x%I64x %lu %u)\n",
            HwDeviceExtension, BusType, SystemIoBusNumber, IoAddress.QuadPart, NumberOfBytes, InIoSpace);

    /* Get the miniport extension */
    MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    DPRINT("HwDeviceExtension %p  MiniportExtension %p\n",
            HwDeviceExtension, MiniportExtension);

    if (!TranslateResourceListAddress(MiniportExtension->Miniport->DeviceExtension,
                                      BusType,
                                      SystemIoBusNumber,
                                      IoAddress,
                                      NumberOfBytes,
                                      InIoSpace,
                                      &TranslatedAddress))
    {
        DPRINT("Checkpoint!\n");
        return NULL;
    }

    DPRINT("Translated Address: 0x%I64x\n", TranslatedAddress.QuadPart);

    /* In I/O space */
    if (InIoSpace)
    {
        DPRINT("Translated Address: %p\n", (PVOID)(ULONG_PTR)TranslatedAddress.QuadPart);
        return (PVOID)(ULONG_PTR)TranslatedAddress.QuadPart;
    }

    /* In memory space */
    MappedAddress = MmMapIoSpace(TranslatedAddress,
                                 NumberOfBytes,
                                 FALSE);
    DPRINT("Mapped Address: %p\n", MappedAddress);

    Status = AllocateAddressMapping(&MiniportExtension->Miniport->DeviceExtension->MappedAddressList,
                                    IoAddress,
                                    MappedAddress,
                                    NumberOfBytes,
                                    SystemIoBusNumber);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("Checkpoint!\n");
        MappedAddress = NULL;
    }

    DPRINT("Mapped Address: %p\n", MappedAddress);
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
    DPRINT("StorPortGetLogicalUnit()\n");
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

    MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);

    DeviceExtension = MiniportExtension->Miniport->DeviceExtension;

    /* Inside of the uncached extension (includes SRB extension pool)? */
    if (((ULONG_PTR)VirtualAddress >= (ULONG_PTR)DeviceExtension->UncachedExtensionVirtualBase) &&
        ((ULONG_PTR)VirtualAddress < (ULONG_PTR)DeviceExtension->UncachedExtensionVirtualBase + DeviceExtension->UncachedExtensionSize))
    {
        Offset = (ULONG_PTR)VirtualAddress - (ULONG_PTR)DeviceExtension->UncachedExtensionVirtualBase;

        PhysicalAddress.QuadPart = DeviceExtension->UncachedExtensionPhysicalBase.QuadPart + Offset;
        *Length = DeviceExtension->UncachedExtensionSize - Offset;

        DPRINT("StorPortGetPhysicalAddress: VA=%p -> PA=0x%I64x (uncached extension, IRQL=%u)\n",
               VirtualAddress, PhysicalAddress.QuadPart, KeGetCurrentIrql());

        return PhysicalAddress;
    }

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        PPORT_SRB_EXTENSION portExt;
        PMDL Mdl;

        portExt = PortGetSrbExtensionContext(Srb);
        Mdl = (portExt != NULL) ? portExt->Mdl : NULL;
        if (Mdl != NULL)
        {
            PPFN_NUMBER pfnArray = MmGetMdlPfnArray(Mdl);
            PVOID baseVa;
            ULONG baseOffset = MmGetMdlByteOffset(Mdl);
            ULONG totalLength = MmGetMdlByteCount(Mdl);
            ULONG_PTR vaOffset;
            ULONG totalOffset;
            ULONG pageIndex;
            ULONG pageOffset;
            ULONG remaining;

        baseVa = (portExt->MappedSystemAddress != NULL)
                         ? portExt->MappedSystemAddress
                         : MmGetMdlVirtualAddress(Mdl);

        vaOffset = (ULONG_PTR)VirtualAddress - (ULONG_PTR)baseVa;
        if ((ULONG_PTR)VirtualAddress < (ULONG_PTR)baseVa)
        {
            *Length = 0;
            PhysicalAddress.QuadPart = 0;
            DPRINT1("StorPortGetPhysicalAddress: VA %p precedes base %p (MDL=%p MSA=%p Total=%lu)\n",
                    VirtualAddress,
                    baseVa,
                    Mdl,
                    portExt->MappedSystemAddress,
                    MmGetMdlByteCount(Mdl));
            return PhysicalAddress;
        }

        if (vaOffset >= totalLength)
        {
            *Length = 0;
            PhysicalAddress.QuadPart = 0;
            DPRINT1("StorPortGetPhysicalAddress: VA offset %lu beyond length %lu (Base=%p MSA=%p MDL=%p ByteOffset=%lu)\n",
                    (ULONG)vaOffset,
                    totalLength,
                    baseVa,
                    portExt->MappedSystemAddress,
                    Mdl,
                    baseOffset);
            return PhysicalAddress;
        }

            totalOffset = baseOffset + (ULONG)vaOffset;
            pageIndex = totalOffset >> PAGE_SHIFT;
            pageOffset = totalOffset & (PAGE_SIZE - 1);

            PhysicalAddress.QuadPart = ((ULONGLONG)pfnArray[pageIndex] << PAGE_SHIFT) + pageOffset;

            remaining = totalLength - (ULONG)vaOffset;
            if (totalLength <= 1024)
            {
                DPRINT("StorPortGetPhysicalAddress: MDL bytes=%lu BaseOffset=%lu VA=%p BaseVA=%p Offset=%lu Remaining=%lu PageIndex=%lu PageOffset=%lu\n",
                       totalLength,
                       baseOffset,
                       VirtualAddress,
                       baseVa,
                       (ULONG)vaOffset,
                       remaining,
                       pageIndex,
                       pageOffset);
            }
            *Length = min(PAGE_SIZE - pageOffset, remaining);

            DPRINT("StorPortGetPhysicalAddress: VA=%p -> PA=0x%I64x via MDL (IRQL=%u)\n",
                   VirtualAddress,
                   PhysicalAddress.QuadPart,
                   KeGetCurrentIrql());

            return PhysicalAddress;
        }

        DPRINT1("StorPortGetPhysicalAddress: Cannot translate address at elevated IRQL=%u (no MDL)\n",
                KeGetCurrentIrql());
        *Length = 0;
        PhysicalAddress.QuadPart = 0;
        return PhysicalAddress;
    }

    PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);
    {
        ULONG_PTR byteOffset = (ULONG_PTR)VirtualAddress & (PAGE_SIZE - 1);
        *Length = (ULONG)(PAGE_SIZE - byteOffset);
        if (*Length == 0)
            *Length = PAGE_SIZE;
    }

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
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;

    DPRINT("StorPortGetScatterGatherList()\n");

    if ((DeviceExtension == NULL) || (Srb == NULL))
        return NULL;

    MiniportExtension = CONTAINING_RECORD(DeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);

    return PortpBuildScatterGatherList(MiniportExtension, Srb);
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
    ULONG TotalAllocationSize;
    ULONG SrbExtensionSize;
    ULONG MaxConcurrentRequests;
    ULONG SlotSize;
    ULONG PoolSize;
    NTSTATUS Status;

    /* Get the miniport extension */
    MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);

    DeviceExtension = MiniportExtension->Miniport->DeviceExtension;

    /* Return the uncached extension base address if we already have one */
    if (DeviceExtension->UncachedExtensionVirtualBase != NULL)
        return DeviceExtension->UncachedExtensionVirtualBase;

    // FIXME: Set DMA stuff here?

    /*
     * Calculate total allocation size:
     * - Miniport's requested size (NumberOfBytes)
     * - SRB extension pool size (if miniport needs SRB extensions)
     */
    TotalAllocationSize = NumberOfBytes;
    SrbExtensionSize = ConfigInfo->SrbExtensionSize;

    if (SrbExtensionSize > 0)
    {
        /*
         * Allocate space for SRB extension pool.
         * Use a reasonable default for concurrent requests if not specified.
         * Typical values: 32-128 slots depending on workload.
         */
        MaxConcurrentRequests = 128;

        /* Account for SRB extension pool in total allocation */
        /* This will be initialized later by PortInitializeSrbExtensionPool */
        SlotSize = ALIGN_UP_BY(SrbExtensionSize, 128);
        PoolSize = SlotSize * MaxConcurrentRequests;

        TotalAllocationSize += PoolSize;
    }

    /* Allocate the uncached extension */
    Alignment.QuadPart = 0;
    LowestAddress.QuadPart = 0;
    HighestAddress.QuadPart = 0x00000000FFFFFFFF;
    DeviceExtension->UncachedExtensionVirtualBase = MmAllocateContiguousMemorySpecifyCache(TotalAllocationSize,
                                                                                           LowestAddress,
                                                                                           HighestAddress,
                                                                                           Alignment,
                                                                                           MmCached);
    if (DeviceExtension->UncachedExtensionVirtualBase == NULL)
        return NULL;

    DeviceExtension->UncachedExtensionPhysicalBase = MmGetPhysicalAddress(DeviceExtension->UncachedExtensionVirtualBase);
    DeviceExtension->UncachedExtensionSize = TotalAllocationSize;

    /* Initialize SRB extension pool if needed */
    if (SrbExtensionSize > 0)
    {
        Status = PortInitializeSrbExtensionPool(DeviceExtension,
                                               SrbExtensionSize,
                                               MaxConcurrentRequests);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("StorPortGetUncachedExtension: Failed to initialize SRB extension pool (0x%08lx)\n", Status);
            MmFreeContiguousMemory(DeviceExtension->UncachedExtensionVirtualBase);
            DeviceExtension->UncachedExtensionVirtualBase = NULL;
            DeviceExtension->UncachedExtensionSize = 0;
            return NULL;
        }

        DPRINT("StorPortGetUncachedExtension: SRB extension pool initialized at VA=%p PA=0x%I64x\n",
                DeviceExtension->SrbExtensionPool.BaseAddress,
                DeviceExtension->SrbExtensionPool.PhysicalBase.QuadPart);
    }

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
    DPRINT("StorPortGetVirtualAddress(%p %I64x)\n",
            HwDeviceExtension, PhysicalAddress.QuadPart);
    UNIMPLEMENTED;
    return NULL;
}


STORPORT_API
BOOLEAN
NTAPI
StorPortGetInterruptInfo(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG InterruptIndex,
    _Out_opt_ PULONG FirmwareInterruptLine,
    _Out_opt_ PULONG SystemVector,
    _Out_opt_ PKIRQL SystemIrql,
    _Out_opt_ PKAFFINITY SystemAffinity)
{
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;
    PMINIPORT Miniport;

    MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    Miniport = MiniportExtension->Miniport;

    if (InterruptIndex >= RTL_NUMBER_OF(Miniport->SystemInterruptValid))
    {
        return FALSE;
    }

    if (!Miniport->SystemInterruptValid[InterruptIndex])
    {
        return FALSE;
    }

    if (FirmwareInterruptLine)
    {
        *FirmwareInterruptLine = Miniport->FirmwareInterruptLine[InterruptIndex];
    }

    if (SystemVector)
    {
        *SystemVector = Miniport->SystemInterruptVector[InterruptIndex];
    }

    if (SystemIrql)
    {
        *SystemIrql = Miniport->SystemInterruptIrql[InterruptIndex];
    }

    if (SystemAffinity)
    {
        *SystemAffinity = Miniport->SystemInterruptAffinity[InterruptIndex];
    }

    return TRUE;
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
                                                       DriverObject);
    if (DriverObjectExtension == NULL)
    {
        DPRINT1("No driver object extension! Allocating new one.\n");

        Status = IoAllocateDriverObjectExtension(DriverObject,
                                                 DriverObject,
                                                 sizeof(DRIVER_OBJECT_EXTENSION),
                                                 (PVOID *)&DriverObjectExtension);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IoAllocateDriverObjectExtension() failed (Status 0x%08lx)\n", Status);
            return Status;
        }

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

    Status = PortAddDriverInitData(DriverObjectExtension,
                                   HwInitializationData);

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
    PSTORPORT_EXTENDED_FUNCTIONS *ppExtendedFunctions;
    PSTOR_DPC Dpc;
    PHW_DPC_ROUTINE HwDpcRoutine;
    va_list ap;

    STOR_SPINLOCK SpinLock;
    PVOID LockContext;
    PSTOR_LOCK_HANDLE LockHandle;
    PSCSI_REQUEST_BLOCK Srb;

    /* Get the miniport extension */
    if (HwDeviceExtension != NULL)
    {
        MiniportExtension = CONTAINING_RECORD(HwDeviceExtension,
                                              MINIPORT_DEVICE_EXTENSION,
                                              HwDeviceExtension);

        DeviceExtension = MiniportExtension->Miniport->DeviceExtension;
    }

    va_start(ap, HwDeviceExtension);

    switch (NotificationType)
    {
        case RequestComplete:
            Srb = (PSCSI_REQUEST_BLOCK)va_arg(ap, PSCSI_REQUEST_BLOCK);
            if ((Srb != NULL) && (Srb->OriginalRequest != NULL))
            {
                PIRP Irp = (PIRP)Srb->OriginalRequest;
                PPORT_SRB_EXTENSION PortExtension = PortGetSrbExtensionContext(Srb);

                if (SRB_STATUS(Srb->SrbStatus) == SRB_STATUS_SUCCESS)
                {
                    PIO_STACK_LOCATION stackLocation = IoGetCurrentIrpStackLocation(Irp);
                    if (stackLocation != NULL && stackLocation->DeviceObject != NULL)
                    {
                        PDEVICE_OBJECT deviceObject = stackLocation->DeviceObject;
                        PPDO_DEVICE_EXTENSION pdoExtension = (PPDO_DEVICE_EXTENSION)deviceObject->DeviceExtension;
                        if (pdoExtension != NULL && pdoExtension->ExtensionType == PdoExtension)
                        {
                            PortpUpdateCapacityFromSrb(pdoExtension, Srb);
                        }
                    }
                }

                Irp->IoStatus.Information = Srb->DataTransferLength;
                Irp->IoStatus.Status = StorPortSrbStatusToNtStatus(Srb->SrbStatus);
                if (PortExtension != NULL)
                {
                    Srb->DataBuffer = PortExtension->OriginalDataBuffer;
                }
                PortpCleanupSrbExtension(Irp);
                Srb->OriginalRequest = NULL;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
            }
            break;

        case GetExtendedFunctionTable:
            ppExtendedFunctions = (PSTORPORT_EXTENDED_FUNCTIONS*)va_arg(ap, PSTORPORT_EXTENDED_FUNCTIONS*);
            if (ppExtendedFunctions != NULL)
                *ppExtendedFunctions = NULL; /* FIXME */
            break;

        case EnablePassiveInitialization:
        {
            PLONG ResultLong;

            HwPassiveInitRoutine = (PHW_PASSIVE_INITIALIZE_ROUTINE)va_arg(ap, PHW_PASSIVE_INITIALIZE_ROUTINE);
            ResultLong = (PLONG)va_arg(ap, PLONG);

            if (ResultLong != NULL)
            {
                *ResultLong = FALSE;
            }

            if ((DeviceExtension != NULL) &&
                (DeviceExtension->HwPassiveInitRoutine == NULL))
            {
                DeviceExtension->HwPassiveInitRoutine = HwPassiveInitRoutine;
                if (ResultLong != NULL)
                {
                    *ResultLong = TRUE;
                }
            }
            break;
        }

        case InitializeDpc:
            Dpc = (PSTOR_DPC)va_arg(ap, PSTOR_DPC);
            HwDpcRoutine = (PHW_DPC_ROUTINE)va_arg(ap, PHW_DPC_ROUTINE);

            KeInitializeDpc((PRKDPC)&Dpc->Dpc,
                            (PKDEFERRED_ROUTINE)HwDpcRoutine,
                            (PVOID)DeviceExtension);
            KeInitializeSpinLock(&Dpc->Lock);
            break;

        case IssueDpc:
        {
            PVOID SystemArgument1;
            PVOID SystemArgument2;
            PLONG SuccessPointer;
            BOOLEAN Queued;
            KIRQL OldIrql;

            Dpc = (PSTOR_DPC)va_arg(ap, PSTOR_DPC);
            SystemArgument1 = (PVOID)va_arg(ap, PVOID);
            SystemArgument2 = (PVOID)va_arg(ap, PVOID);
            SuccessPointer = (PLONG)va_arg(ap, PLONG);

            Queued = FALSE;

            if (Dpc != NULL)
            {
                KeAcquireSpinLock((PKSPIN_LOCK)&Dpc->Lock, &OldIrql);
                Queued = KeInsertQueueDpc((PRKDPC)&Dpc->Dpc,
                                          SystemArgument1,
                                          SystemArgument2);
                KeReleaseSpinLock((PKSPIN_LOCK)&Dpc->Lock, OldIrql);
            }

            if (SuccessPointer != NULL)
            {
                *SuccessPointer = Queued ? TRUE : FALSE;
            }
            break;
        }

        case BusChangeDetected:
        {
            ULONG PathId;
            ULONG TargetId;
            ULONG Lun;

            PathId = va_arg(ap, ULONG);
            TargetId = va_arg(ap, ULONG);
            Lun = va_arg(ap, ULONG);

            UNREFERENCED_PARAMETER(PathId);
            UNREFERENCED_PARAMETER(TargetId);
            UNREFERENCED_PARAMETER(Lun);

            DPRINT1("StorPortNotification: BusChangeDetected (Path %lu Target %lu Lun %lu) DevExt=%p PDO=%p\n",
                    PathId, TargetId, Lun,
                    DeviceExtension,
                    DeviceExtension ? DeviceExtension->PhysicalDevice : NULL);

            if ((DeviceExtension != NULL) &&
                (DeviceExtension->PhysicalDevice != NULL))
            {
                IoInvalidateDeviceRelations(DeviceExtension->PhysicalDevice,
                                            BusRelations);
            }
            break;
        }

        case AcquireSpinLock:
            SpinLock = (STOR_SPINLOCK)va_arg(ap, STOR_SPINLOCK);
            LockContext = (PVOID)va_arg(ap, PVOID);
            LockHandle = (PSTOR_LOCK_HANDLE)va_arg(ap, PSTOR_LOCK_HANDLE);
            PortAcquireSpinLock(DeviceExtension,
                                SpinLock,
                                LockContext,
                                LockHandle);
            break;

        case ReleaseSpinLock:
            LockHandle = (PSTOR_LOCK_HANDLE)va_arg(ap, PSTOR_LOCK_HANDLE);
            PortReleaseSpinLock(DeviceExtension,
                                LockHandle);
            break;

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
    DPRINT("StorPortPause()\n");
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
    DPRINT("StorPortPauseDevice()\n");
    UNIMPLEMENTED;
    return FALSE;
}


#if defined(_M_AMD64)
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
    DPRINT("StorPortQuerySystemTime(%p)\n", CurrentTime);

    KeQuerySystemTime(CurrentTime);
}
#endif /* defined(_M_AMD64) */


/*
 * @unimplemented
 */
STORPORT_API
BOOLEAN
NTAPI
StorPortReady(
    _In_ PVOID HwDeviceExtension)
{
    DPRINT("StorPortReady()\n");
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
    DPRINT("StorPortRegistryRead()\n");
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
    DPRINT("StorPortRegistryWrite()\n");
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
    DPRINT("StorPortResume()\n");
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
    DPRINT("StorPortResumeDevice()\n");
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

    DPRINT("StorPortSetBusData(%p %lu %lu %lu %p %lu %lu)\n",
            DeviceExtension, BusDataType, SystemIoBusNumber, SlotNumber, Buffer, Offset, Length);

    MiniportExtension = CONTAINING_RECORD(DeviceExtension,
                                          MINIPORT_DEVICE_EXTENSION,
                                          HwDeviceExtension);
    DPRINT("DeviceExtension %p  MiniportExtension %p\n",
            DeviceExtension, MiniportExtension);

    Interface = &MiniportExtension->Miniport->DeviceExtension->BusInterface;

    ReturnLength = Interface->SetBusData(Interface->Context,
                                         BusDataType,
                                         Buffer,
                                         Offset,
                                         Length);
    DPRINT("ReturnLength: %lu\n", ReturnLength);

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
    DPRINT("StorPortSetDeviceQueueDepth()\n");
    UNIMPLEMENTED;
    return FALSE;
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
    DPRINT("StorPortSynchronizeAccess()\n");
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
    DPRINT("StorPortValidateRange()\n");
    return TRUE;
}

/* EOF */
#include <ntddscsi.h>

/*
 * SRB Extension Pool Management
 *
 * These functions implement a bitmap-based allocator for SRB extensions
 * within the uncached extension memory. This ensures that all SRB extensions
 * can be translated to physical addresses at any IRQL without calling
 * MmGetPhysicalAddress() which is restricted to APC_LEVEL.
 */

/**
 * @brief Initialize the SRB extension pool within the uncached extension
 *
 * @param DeviceExtension - FDO device extension
 * @param SrbExtensionSize - Size requested by miniport for each SRB extension
 * @param MaxConcurrentRequests - Maximum number of concurrent SRB extensions needed
 * @return STATUS_SUCCESS or error code
 *
 * @remarks This function must be called at PASSIVE_LEVEL after the uncached
 *          extension has been allocated. The pool is placed after the miniport's
 *          uncached extension area.
 */
NTSTATUS
PortInitializeSrbExtensionPool(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG SrbExtensionSize,
    _In_ ULONG MaxConcurrentRequests)
{
    PSRB_EXTENSION_POOL Pool;
    ULONG SlotSize;
    ULONG PoolSize;
    ULONG BitmapSize;
    ULONG_PTR PoolBaseVa;
    PHYSICAL_ADDRESS PoolBasePa;
    PUCHAR MiniportEnd;

    DPRINT("PortInitializeSrbExtensionPool: SrbExtensionSize=%lu, MaxConcurrentRequests=%lu\n",
            SrbExtensionSize, MaxConcurrentRequests);

    if (SrbExtensionSize == 0 || MaxConcurrentRequests == 0)
        return STATUS_INVALID_PARAMETER;

    if (DeviceExtension->UncachedExtensionVirtualBase == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    Pool = &DeviceExtension->SrbExtensionPool;

    /*
     * Align slot size to 128 bytes as required by AHCI for command tables.
     * This also ensures good cache line alignment.
     */
    SlotSize = ALIGN_UP_BY(SrbExtensionSize, 128);
    PoolSize = SlotSize * MaxConcurrentRequests;

    /*
     * Pool is placed immediately after the miniport's uncached extension.
     * The miniport's extension was the original NumberOfBytes passed to
     * StorPortGetUncachedExtension().
     */
    MiniportEnd = (PUCHAR)DeviceExtension->UncachedExtensionVirtualBase +
                  (DeviceExtension->UncachedExtensionSize - PoolSize);

    PoolBaseVa = (ULONG_PTR)MiniportEnd;
    PoolBasePa.QuadPart = DeviceExtension->UncachedExtensionPhysicalBase.QuadPart +
                          (DeviceExtension->UncachedExtensionSize - PoolSize);

    /* Allocate bitmap buffer */
    BitmapSize = (MaxConcurrentRequests + 31) / 32; /* Number of ULONGs needed */
    Pool->BitmapBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                              BitmapSize * sizeof(ULONG),
                                              TAG_SRB_POOL_BITMAP);
    if (Pool->BitmapBuffer == NULL)
    {
        DPRINT1("PortInitializeSrbExtensionPool: Failed to allocate bitmap buffer\n");
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Pool->BitmapBuffer, BitmapSize * sizeof(ULONG));

    /* Initialize the bitmap */
    RtlInitializeBitMap(&Pool->Bitmap,
                        Pool->BitmapBuffer,
                        MaxConcurrentRequests);

    /* All slots start as free (0 = free, 1 = allocated) */
    RtlClearAllBits(&Pool->Bitmap);

    /* Initialize pool structure */
    Pool->BaseAddress = (PVOID)PoolBaseVa;
    Pool->PhysicalBase = PoolBasePa;
    Pool->SlotSize = SlotSize;
    Pool->SlotCount = MaxConcurrentRequests;
    /* Note: Pool->Lock is already initialized during FDO creation */

    DPRINT("PortInitializeSrbExtensionPool: Pool initialized at VA=%p PA=0x%I64x, SlotSize=%lu, SlotCount=%lu\n",
            Pool->BaseAddress, Pool->PhysicalBase.QuadPart, Pool->SlotSize, Pool->SlotCount);

    return STATUS_SUCCESS;
}

/**
 * @brief Allocate an SRB extension from the pool
 *
 * @param DeviceExtension - FDO device extension
 * @return Pointer to allocated SRB extension, or NULL if pool is exhausted
 *
 * @remarks This function can be called at any IRQL up to DISPATCH_LEVEL.
 *          It uses a spinlock to protect the bitmap.
 */
PVOID
PortAllocateSrbExtension(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PSRB_EXTENSION_POOL Pool;
    KIRQL OldIrql;
    ULONG SlotIndex;
    PVOID SrbExtension;

    Pool = &DeviceExtension->SrbExtensionPool;

    /* Check if pool is initialized */
    if (Pool->BaseAddress == NULL || Pool->SlotCount == 0)
    {
        DPRINT1("PortAllocateSrbExtension: Pool not initialized\n");
        return NULL;
    }

    /* Acquire spinlock to protect bitmap */
    KeAcquireSpinLock(&Pool->Lock, &OldIrql);

    /* Find a free slot */
    SlotIndex = RtlFindClearBits(&Pool->Bitmap, 1, 0);

    if (SlotIndex == 0xFFFFFFFF || SlotIndex >= Pool->SlotCount)
    {
        /* Pool exhausted */
        KeReleaseSpinLock(&Pool->Lock, OldIrql);
        DPRINT1("PortAllocateSrbExtension: Pool exhausted (SlotCount=%lu)\n", Pool->SlotCount);
        return NULL;
    }

    /* Mark slot as allocated */
    RtlSetBits(&Pool->Bitmap, SlotIndex, 1);

    KeReleaseSpinLock(&Pool->Lock, OldIrql);

    /* Calculate address of slot */
    SrbExtension = (PUCHAR)Pool->BaseAddress + (SlotIndex * Pool->SlotSize);

    /* Zero the extension for the miniport */
    RtlZeroMemory(SrbExtension, Pool->SlotSize);

    DPRINT("PortAllocateSrbExtension: Allocated slot %lu at VA=%p\n", SlotIndex, SrbExtension);

    return SrbExtension;
}

/**
 * @brief Free an SRB extension back to the pool
 *
 * @param DeviceExtension - FDO device extension
 * @param SrbExtension - Pointer to SRB extension to free
 *
 * @remarks This function can be called at any IRQL up to DISPATCH_LEVEL.
 *          It validates that the pointer is within the pool before freeing.
 */
VOID
PortFreeSrbExtension(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PVOID SrbExtension)
{
    PSRB_EXTENSION_POOL Pool;
    KIRQL OldIrql;
    ULONG_PTR Offset;
    ULONG SlotIndex;

    if (SrbExtension == NULL)
        return;

    Pool = &DeviceExtension->SrbExtensionPool;

    /* Check if pool is initialized */
    if (Pool->BaseAddress == NULL || Pool->SlotCount == 0)
    {
        DPRINT1("PortFreeSrbExtension: Pool not initialized, cannot free %p\n", SrbExtension);
        return;
    }

    /* Validate that the pointer is within the pool */
    if ((ULONG_PTR)SrbExtension < (ULONG_PTR)Pool->BaseAddress ||
        (ULONG_PTR)SrbExtension >= (ULONG_PTR)Pool->BaseAddress + (Pool->SlotSize * Pool->SlotCount))
    {
        DPRINT1("PortFreeSrbExtension: Invalid pointer %p (not in pool range [%p-%p])\n",
                SrbExtension, Pool->BaseAddress,
                (PUCHAR)Pool->BaseAddress + (Pool->SlotSize * Pool->SlotCount));
        return;
    }

    /* Calculate slot index */
    Offset = (ULONG_PTR)SrbExtension - (ULONG_PTR)Pool->BaseAddress;
    SlotIndex = (ULONG)(Offset / Pool->SlotSize);

    if (SlotIndex >= Pool->SlotCount)
    {
        DPRINT1("PortFreeSrbExtension: Invalid slot index %lu (max %lu)\n",
                SlotIndex, Pool->SlotCount);
        return;
    }

    /* Acquire spinlock to protect bitmap */
    KeAcquireSpinLock(&Pool->Lock, &OldIrql);

    /* Check if slot is actually allocated */
    if (!RtlAreBitsSet(&Pool->Bitmap, SlotIndex, 1))
    {
        DPRINT1("PortFreeSrbExtension: Double free detected for slot %lu\n", SlotIndex);
        KeReleaseSpinLock(&Pool->Lock, OldIrql);
        return;
    }

    /* Mark slot as free */
    RtlClearBits(&Pool->Bitmap, SlotIndex, 1);

    KeReleaseSpinLock(&Pool->Lock, OldIrql);

    DPRINT("PortFreeSrbExtension: Freed slot %lu at VA=%p\n", SlotIndex, SrbExtension);
}

/**
 * @brief Cleanup the SRB extension pool
 *
 * @param DeviceExtension - FDO device extension
 *
 * @remarks This function should be called during device removal at PASSIVE_LEVEL.
 *          It frees the bitmap buffer. The pool memory itself is part of the
 *          uncached extension and will be freed separately.
 */
VOID
PortCleanupSrbExtensionPool(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension)
{
    PSRB_EXTENSION_POOL Pool;

    Pool = &DeviceExtension->SrbExtensionPool;

    if (Pool->BitmapBuffer != NULL)
    {
        ExFreePoolWithTag(Pool->BitmapBuffer, TAG_SRB_POOL_BITMAP);
        Pool->BitmapBuffer = NULL;
    }

    RtlZeroMemory(Pool, sizeof(SRB_EXTENSION_POOL));
}

NTSTATUS
StorPortSrbStatusToNtStatus(_In_ UCHAR SrbStatus)
{
    SrbStatus = SRB_STATUS(SrbStatus);

    switch (SrbStatus)
    {
        case SRB_STATUS_SUCCESS:
            return STATUS_SUCCESS;

        case SRB_STATUS_PENDING:
            return STATUS_PENDING;

        case SRB_STATUS_TIMEOUT:
            return STATUS_IO_TIMEOUT;

        case SRB_STATUS_SELECTION_TIMEOUT:
        case SRB_STATUS_NO_DEVICE:
            return STATUS_NO_SUCH_DEVICE;

        case SRB_STATUS_INVALID_REQUEST:
        case SRB_STATUS_BAD_SRB_BLOCK_LENGTH:
            return STATUS_INVALID_DEVICE_REQUEST;

        default:
            return STATUS_IO_DEVICE_ERROR;
    }
}
#ifndef ALIGN_UP_BY
#define ALIGN_UP_BY(length, alignment) (((length) + ((alignment) - 1)) & ~((alignment) - 1))
#endif

static
VOID
PortpFreeScatterGatherList(_Inout_ PPORT_SRB_EXTENSION PortExtension)
{
    if ((PortExtension != NULL) && (PortExtension->ScatterList != NULL))
    {
        ExFreePoolWithTag(PortExtension->ScatterList, TAG_SG_LIST);
        PortExtension->ScatterList = NULL;
        PortExtension->ScatterListSize = 0;
    }
}

VOID
PortpCleanupSrbExtension(_Inout_opt_ PIRP Irp)
{
    PPORT_SRB_EXTENSION PortExtension;
    PFDO_DEVICE_EXTENSION FdoExtension;
    PIO_STACK_LOCATION IrpStack;
    PDEVICE_OBJECT DeviceObject;

    if (Irp == NULL)
        return;

    PortExtension = (PPORT_SRB_EXTENSION)Irp->Tail.Overlay.DriverContext[0];
    if (PortExtension == NULL)
        return;

    PortpFreeScatterGatherList(PortExtension);

    /*
     * Free the miniport extension back to the SRB extension pool if it was
     * allocated from the pool. The pool validates the pointer, so it's safe
     * to call even if the extension wasn't from the pool.
     */
    if (PortExtension->MiniportExtension != NULL)
    {
        /* Get FDO extension from IRP's device object */
        IrpStack = IoGetCurrentIrpStackLocation(Irp);
        DeviceObject = IrpStack->DeviceObject;

        if (DeviceObject != NULL)
        {
            PPDO_DEVICE_EXTENSION PdoExt = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

            /* Check if this is a PDO by examining the extension type */
            if (PdoExt != NULL && PdoExt->ExtensionType == PdoExtension)
            {
                FdoExtension = PdoExt->FdoExtension;

                if (FdoExtension != NULL &&
                    FdoExtension->SrbExtensionPool.BaseAddress != NULL)
                {
                    /* Free back to pool - function validates pointer is in pool */
                    PortFreeSrbExtension(FdoExtension, PortExtension->MiniportExtension);
                }
            }
        }

        /* Clear the extension memory for security */
        if (PortExtension->MiniportExtensionLength != 0)
        {
            RtlZeroMemory(PortExtension->MiniportExtension,
                          PortExtension->MiniportExtensionLength);
        }
    }

    Irp->Tail.Overlay.DriverContext[0] = NULL;
    ExFreePoolWithTag(PortExtension, TAG_SRB_EXT);
}

static
VOID
PortpAppendScatterGatherElement(
    _Inout_ PSTOR_SCATTER_GATHER_LIST ScatterList,
    _Inout_ ULONG *ElementCount,
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG Length)
{
    PSTOR_SCATTER_GATHER_ELEMENT Element;

    if (*ElementCount > 0)
    {
        PSTOR_SCATTER_GATHER_ELEMENT PreviousElement;

        PreviousElement = &ScatterList->List[*ElementCount - 1];
        if (PreviousElement->PhysicalAddress.QuadPart + PreviousElement->Length == PhysicalAddress)
        {
            PreviousElement->Length += Length;
            return;
        }
    }

    Element = &ScatterList->List[*ElementCount];
    Element->PhysicalAddress.QuadPart = PhysicalAddress;
    Element->Length = Length;
    Element->Reserved = 0;
    (*ElementCount)++;
}

static
PSTOR_SCATTER_GATHER_LIST
PortpBuildScatterGatherFromMdl(
    _In_ PPORT_SRB_EXTENSION PortExtension,
    _In_ PMDL Mdl,
    _In_ ULONG DataTransferLength)
{
    PPFN_NUMBER PfnArray;
    ULONG ByteOffset;
    ULONG PageSpan;
    SIZE_T AllocationLength;
    PSTOR_SCATTER_GATHER_LIST ScatterList;
    ULONG RemainingLength;
    ULONG PageIndex;
    ULONG ElementCount;

    ByteOffset = MmGetMdlByteOffset(Mdl);

    if (MmGetMdlByteCount(Mdl) < ByteOffset + DataTransferLength)
    {
        DPRINT("PortpBuildScatterGatherFromMdl: MDL smaller than transfer length (ByteCount=%lu, Needed=%lu)\n",
                MmGetMdlByteCount(Mdl),
                ByteOffset + DataTransferLength);
        return NULL;
    }

    PageSpan = ADDRESS_AND_SIZE_TO_SPAN_PAGES((PUCHAR)MmGetMdlVirtualAddress(Mdl) + ByteOffset,
                                              DataTransferLength);
    if (PageSpan == 0)
        PageSpan = 1;

    AllocationLength = sizeof(STOR_SCATTER_GATHER_LIST) +
                       (SIZE_T)PageSpan * sizeof(STOR_SCATTER_GATHER_ELEMENT);

    ScatterList = ExAllocatePoolWithTag(NonPagedPool,
                                        AllocationLength,
                                        TAG_SG_LIST);
    if (ScatterList == NULL)
        return NULL;

    RtlZeroMemory(ScatterList, AllocationLength);

    PfnArray = MmGetMdlPfnArray(Mdl);
    RemainingLength = DataTransferLength;
    PageIndex = 0;
    ElementCount = 0;

    while ((RemainingLength > 0) && (PageIndex < PageSpan))
    {
        ULONGLONG PhysicalBase;
        ULONG ChunkLength;

        PhysicalBase = ((ULONGLONG)PfnArray[PageIndex] << PAGE_SHIFT) + ByteOffset;
        ChunkLength = min(PAGE_SIZE - ByteOffset, RemainingLength);

        PortpAppendScatterGatherElement(ScatterList,
                                        &ElementCount,
                                        PhysicalBase,
                                        ChunkLength);

        RemainingLength -= ChunkLength;
        PageIndex++;
        ByteOffset = 0;
    }

    if (RemainingLength != 0)
    {
        ExFreePoolWithTag(ScatterList, TAG_SG_LIST);
        return NULL;
    }

    ScatterList->NumberOfElements = ElementCount;
    ScatterList->Reserved = 0;

    PortExtension->ScatterList = ScatterList;
    PortExtension->ScatterListSize = (ULONG)AllocationLength;

    return ScatterList;
}

static
PSTOR_SCATTER_GATHER_LIST
PortpBuildScatterGatherFromBuffer(
    _In_ PPORT_SRB_EXTENSION PortExtension,
    _In_reads_bytes_(DataTransferLength) PVOID DataBuffer,
    _In_ ULONG DataTransferLength)
{
    SIZE_T AllocationLength;
    ULONG PageSpan;
    PSTOR_SCATTER_GATHER_LIST ScatterList;
    ULONG RemainingLength;
    ULONG ElementCount;
    PUCHAR VirtualAddress;

    PageSpan = ADDRESS_AND_SIZE_TO_SPAN_PAGES(DataBuffer, DataTransferLength);
    if (PageSpan == 0)
        PageSpan = 1;

    AllocationLength = sizeof(STOR_SCATTER_GATHER_LIST) +
                       (SIZE_T)PageSpan * sizeof(STOR_SCATTER_GATHER_ELEMENT);

    ScatterList = ExAllocatePoolWithTag(NonPagedPool,
                                        AllocationLength,
                                        TAG_SG_LIST);
    if (ScatterList == NULL)
        return NULL;

    RtlZeroMemory(ScatterList, AllocationLength);

    RemainingLength = DataTransferLength;
    ElementCount = 0;
    VirtualAddress = (PUCHAR)DataBuffer;

    while (RemainingLength > 0)
    {
        STOR_PHYSICAL_ADDRESS PhysicalAddress;
        ULONG ByteOffset;
        ULONG ChunkLength;

        if (!MmIsAddressValid(VirtualAddress))
        {
            DPRINT1("PortpBuildScatterGatherFromBuffer: address %p is invalid\n",
                    VirtualAddress);
            ExFreePoolWithTag(ScatterList, TAG_SG_LIST);
            return NULL;
        }

        PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);
        ByteOffset = (ULONG)((ULONG_PTR)VirtualAddress & (PAGE_SIZE - 1));
        ChunkLength = min(PAGE_SIZE - ByteOffset, RemainingLength);

        PortpAppendScatterGatherElement(ScatterList,
                                        &ElementCount,
                                        PhysicalAddress.QuadPart,
                                        ChunkLength);

        VirtualAddress += ChunkLength;
        RemainingLength -= ChunkLength;
    }

    ScatterList->NumberOfElements = ElementCount;
    ScatterList->Reserved = 0;

    PortExtension->ScatterList = ScatterList;
    PortExtension->ScatterListSize = (ULONG)AllocationLength;

    return ScatterList;
}

PSTOR_SCATTER_GATHER_LIST
PortpBuildScatterGatherList(
    _In_ PMINIPORT_DEVICE_EXTENSION MiniportExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PPORT_SRB_EXTENSION PortExtension;
    PSTOR_SCATTER_GATHER_LIST ScatterList;
    PIRP Irp;
    PMDL Mdl;

    UNREFERENCED_PARAMETER(MiniportExtension);

    PortExtension = PortGetSrbExtensionContext(Srb);
    if (PortExtension == NULL)
    {
        DPRINT("PortpBuildScatterGatherList: Missing SRB extension context\n");
        return NULL;
    }

    if (PortExtension->ScatterList != NULL)
        return PortExtension->ScatterList;

    if ((Srb->DataTransferLength == 0) || (Srb->DataBuffer == NULL))
    {
        SIZE_T AllocationLength = sizeof(STOR_SCATTER_GATHER_LIST);

        ScatterList = ExAllocatePoolWithTag(NonPagedPool,
                                            AllocationLength,
                                            TAG_SG_LIST);
        if (ScatterList == NULL)
            return NULL;

        ScatterList->NumberOfElements = 0;
        ScatterList->Reserved = 0;

        PortExtension->ScatterList = ScatterList;
        PortExtension->ScatterListSize = (ULONG)AllocationLength;
        return ScatterList;
    }

    Irp = (PIRP)Srb->OriginalRequest;
    Mdl = Irp ? Irp->MdlAddress : NULL;
    PortExtension->Mdl = Mdl;

    if (Mdl != NULL)
    {
        ScatterList = PortpBuildScatterGatherFromMdl(PortExtension,
                                                    Mdl,
                                                    Srb->DataTransferLength);
        if (ScatterList != NULL)
            return ScatterList;

        DPRINT1("PortpBuildScatterGatherList: Falling back to virtual address mapping\n");
    }

    return PortpBuildScatterGatherFromBuffer(PortExtension,
                                             Srb->DataBuffer,
                                             Srb->DataTransferLength);
}
static NTSTATUS
PortCompleteRequest(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static ULONG
PortTrimInquiryString(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ ULONG Length)
{
    while (Length > 0 && Buffer[Length - 1] == ' ')
        Length--;
    return Length;
}

static VOID
PortBuildAdapterDescriptor(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _Out_ PSTORAGE_ADAPTER_DESCRIPTOR Descriptor)
{
    ULONG maxTransfer = DeviceExtension->Miniport.PortConfig.MaximumTransferLength;

    RtlZeroMemory(Descriptor, sizeof(*Descriptor));

    Descriptor->Version = sizeof(*Descriptor);
    Descriptor->Size = sizeof(*Descriptor);

    if (maxTransfer == 0)
        maxTransfer = 512 * 1024; /* 512 KiB default */

    Descriptor->MaximumTransferLength = maxTransfer;
    Descriptor->MaximumPhysicalPages = (maxTransfer + PAGE_SIZE - 1) >> PAGE_SHIFT;
    if (Descriptor->MaximumPhysicalPages == 0)
        Descriptor->MaximumPhysicalPages = 1;

    Descriptor->AlignmentMask = DeviceExtension->Miniport.PortConfig.AlignmentMask;
    Descriptor->AdapterUsesPio = FALSE;
    Descriptor->AdapterScansDown = FALSE;
    Descriptor->CommandQueueing = TRUE;
    Descriptor->AcceleratedTransfer = FALSE;
    Descriptor->BusType = BusTypeSata;
    Descriptor->BusMajorVersion = 1;
    Descriptor->BusMinorVersion = 0;

#if defined(SRB_TYPE_SCSI_REQUEST_BLOCK)
    Descriptor->SrbType = SRB_TYPE_SCSI_REQUEST_BLOCK;
#endif
#if defined(STORAGE_ADDRESS_TYPE_BTL8)
    Descriptor->AddressType = STORAGE_ADDRESS_TYPE_BTL8;
#endif
}

static NTSTATUS
PortHandleAdapterPropertyQuery(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG OutputLength,
    _Inout_updates_bytes_(OutputLength) PVOID OutputBuffer,
    _Out_ ULONG_PTR *Information)
{
    STORAGE_ADAPTER_DESCRIPTOR descriptor;

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
        return STATUS_BUFFER_TOO_SMALL;

    PortBuildAdapterDescriptor(DeviceExtension, &descriptor);

    if (OutputLength < sizeof(STORAGE_ADAPTER_DESCRIPTOR))
    {
        PSTORAGE_DESCRIPTOR_HEADER header = (PSTORAGE_DESCRIPTOR_HEADER)OutputBuffer;
        header->Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
        header->Size = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
        *Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    RtlCopyMemory(OutputBuffer, &descriptor, sizeof(descriptor));
    *Information = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    return STATUS_SUCCESS;
}

static NTSTATUS
PortBuildDeviceDescriptor(
    _In_opt_ PINQUIRYDATA Inquiry,
    _In_ ULONG OutputLength,
    _Inout_updates_bytes_(OutputLength) PVOID OutputBuffer,
    _Out_ ULONG_PTR *Information)
{
    ULONG vendorLength = 0, productLength = 0, revisionLength = 0;
    ULONG requiredLength;
    ULONG offset;
    PSTORAGE_DEVICE_DESCRIPTOR descriptor;
    BOOLEAN headerOnly;

    if (OutputLength < sizeof(STORAGE_DESCRIPTOR_HEADER))
        return STATUS_BUFFER_TOO_SMALL;

    if (Inquiry != NULL)
    {
        vendorLength = PortTrimInquiryString((const CHAR *)Inquiry->VendorId, sizeof(Inquiry->VendorId));
        productLength = PortTrimInquiryString((const CHAR *)Inquiry->ProductId, sizeof(Inquiry->ProductId));
        revisionLength = PortTrimInquiryString((const CHAR *)Inquiry->ProductRevisionLevel, sizeof(Inquiry->ProductRevisionLevel));
    }

    requiredLength = sizeof(STORAGE_DEVICE_DESCRIPTOR);
    if (vendorLength > 0)
        requiredLength += vendorLength + 1;
    if (productLength > 0)
        requiredLength += productLength + 1;
    if (revisionLength > 0)
        requiredLength += revisionLength + 1;

    descriptor = (PSTORAGE_DEVICE_DESCRIPTOR)OutputBuffer;
    RtlZeroMemory(OutputBuffer, min(OutputLength, requiredLength));

    descriptor->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
    descriptor->Size = requiredLength;

    headerOnly = (OutputLength < sizeof(STORAGE_DEVICE_DESCRIPTOR));
    if (headerOnly)
    {
        *Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    descriptor->RawPropertiesLength = 0;
    descriptor->BusType = BusTypeSata;
    descriptor->SerialNumberOffset = ULONG_MAX;
    descriptor->VendorIdOffset = ULONG_MAX;
    descriptor->ProductIdOffset = ULONG_MAX;
    descriptor->ProductRevisionOffset = ULONG_MAX;

    if (Inquiry != NULL)
    {
        descriptor->DeviceType = Inquiry->DeviceType;
        descriptor->DeviceTypeModifier = Inquiry->DeviceTypeModifier;
        descriptor->RemovableMedia = (Inquiry->RemovableMedia) ? TRUE : FALSE;
    }
    else
    {
        descriptor->DeviceType = DIRECT_ACCESS_DEVICE;
        descriptor->DeviceTypeModifier = 0;
        descriptor->RemovableMedia = FALSE;
    }

    descriptor->CommandQueueing = TRUE;

    offset = sizeof(STORAGE_DEVICE_DESCRIPTOR);

    if (vendorLength > 0)
    {
        descriptor->VendorIdOffset = offset;
        if (offset < OutputLength)
        {
            ULONG bytes = min(vendorLength, OutputLength - offset - 1);
            RtlCopyMemory((PUCHAR)OutputBuffer + offset, Inquiry->VendorId, bytes);
            ((PUCHAR)OutputBuffer + offset)[bytes] = '\0';
        }
        offset += vendorLength + 1;
    }

    if (productLength > 0)
    {
        descriptor->ProductIdOffset = offset;
        if (offset < OutputLength)
        {
            ULONG bytes = min(productLength, OutputLength - offset - 1);
            RtlCopyMemory((PUCHAR)OutputBuffer + offset, Inquiry->ProductId, bytes);
            ((PUCHAR)OutputBuffer + offset)[bytes] = '\0';
        }
        offset += productLength + 1;
    }

    if (revisionLength > 0)
    {
        descriptor->ProductRevisionOffset = offset;
        if (offset < OutputLength)
        {
            ULONG bytes = min(revisionLength, OutputLength - offset - 1);
            RtlCopyMemory((PUCHAR)OutputBuffer + offset, Inquiry->ProductRevisionLevel, bytes);
            ((PUCHAR)OutputBuffer + offset)[bytes] = '\0';
        }
        offset += revisionLength + 1;
    }

    if (requiredLength > OutputLength)
    {
        *Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
        return STATUS_SUCCESS;
    }

    *Information = requiredLength;
    return STATUS_SUCCESS;
}

static
ULONG
PortpGetBytesPerBlock(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->CapacityValid && DeviceExtension->BytesPerBlock != 0)
        return DeviceExtension->BytesPerBlock;

    return 512;
}

static
MEDIA_TYPE
PortpGetMediaType(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->FdoExtension != NULL &&
        DeviceExtension->FdoExtension->Device != NULL &&
        (DeviceExtension->FdoExtension->Device->Characteristics & FILE_REMOVABLE_MEDIA))
    {
        return RemovableMedia;
    }

    return FixedMedia;
}

static
VOID
PortpBuildDiskGeometry(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _Out_ PDISK_GEOMETRY Geometry)
{
    ULONGLONG cylinders = 1;
    const ULONG sectorsPerTrack = 63;
    const ULONG tracksPerCylinder = 255;
    ULONG bytesPerSector;

    RtlZeroMemory(Geometry, sizeof(*Geometry));

    Geometry->MediaType = PortpGetMediaType(DeviceExtension);

    bytesPerSector = PortpGetBytesPerBlock(DeviceExtension);
    Geometry->BytesPerSector = bytesPerSector;
    Geometry->SectorsPerTrack = sectorsPerTrack;
    Geometry->TracksPerCylinder = tracksPerCylinder;

    if (DeviceExtension->CapacityValid && DeviceExtension->DiskSize != 0 && bytesPerSector != 0)
    {
        ULONGLONG totalSectors = DeviceExtension->DiskSize / bytesPerSector;
        ULONGLONG sectorsPerCylinder = (ULONGLONG)sectorsPerTrack * tracksPerCylinder;

        if (sectorsPerCylinder == 0)
            sectorsPerCylinder = 1;

        if (totalSectors == 0)
            totalSectors = 1;

        cylinders = totalSectors / sectorsPerCylinder;
        if (cylinders == 0)
            cylinders = 1;
    }

    Geometry->Cylinders.QuadPart = (LONGLONG)cylinders;
}

NTSTATUS
PortFdoDeviceControl(
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG_PTR information = 0;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    switch (code)
    {
        case IOCTL_SCSI_GET_CAPABILITIES:
        {
            PIO_SCSI_CAPABILITIES caps;

            if (buffer == NULL || outLen < sizeof(IO_SCSI_CAPABILITIES))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            caps = (PIO_SCSI_CAPABILITIES)buffer;
            RtlZeroMemory(caps, sizeof(*caps));
            caps->Length = sizeof(*caps);
            caps->MaximumTransferLength = DeviceExtension->Miniport.PortConfig.MaximumTransferLength;
            if (caps->MaximumTransferLength == 0)
                caps->MaximumTransferLength = 512 * 1024;
            caps->MaximumPhysicalPages = (caps->MaximumTransferLength + PAGE_SIZE - 1) >> PAGE_SHIFT;
            if (caps->MaximumPhysicalPages == 0)
                caps->MaximumPhysicalPages = 1;
            caps->AlignmentMask = DeviceExtension->Miniport.PortConfig.AlignmentMask;
            caps->TaggedQueuing = TRUE;
            caps->AdapterScansDown = FALSE;
            caps->AdapterUsesPio = FALSE;

            information = sizeof(IO_SCSI_CAPABILITIES);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_STORAGE_QUERY_PROPERTY:
        {
            PSTORAGE_PROPERTY_QUERY query;

            if (buffer == NULL || stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(STORAGE_PROPERTY_QUERY))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            query = (PSTORAGE_PROPERTY_QUERY)buffer;

            if (query->QueryType != PropertyStandardQuery &&
                query->QueryType != PropertyExistsQuery)
            {
                status = STATUS_NOT_SUPPORTED;
                break;
            }

            switch (query->PropertyId)
            {
                case StorageAdapterProperty:
                    status = PortHandleAdapterPropertyQuery(DeviceExtension, outLen, buffer, &information);
                    if (status == STATUS_BUFFER_TOO_SMALL && query->QueryType == PropertyExistsQuery)
                        status = STATUS_SUCCESS;
                    break;

                case StorageDeviceProperty:
                {
                    PPDO_DEVICE_EXTENSION pdo = NULL;
                    KLOCK_QUEUE_HANDLE lockHandle;

                    KeAcquireInStackQueuedSpinLock(&DeviceExtension->PdoListLock, &lockHandle);
                    if (!IsListEmpty(&DeviceExtension->PdoListHead))
                    {
                        PLIST_ENTRY entry = DeviceExtension->PdoListHead.Flink;
                        pdo = CONTAINING_RECORD(entry, PDO_DEVICE_EXTENSION, PdoListEntry);
                    }
                    KeReleaseInStackQueuedSpinLock(&lockHandle);

                    status = PortBuildDeviceDescriptor(pdo ? pdo->InquiryBuffer : NULL,
                                                       outLen,
                                                       buffer,
                                                       &information);
                    if (status == STATUS_BUFFER_TOO_SMALL && query->QueryType == PropertyExistsQuery)
                        status = STATUS_SUCCESS;
                    break;
                }

                default:
                    status = STATUS_NOT_SUPPORTED;
                    break;
            }

            break;
        }

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    return PortCompleteRequest(Irp, status, information);
}

NTSTATUS
PortPdoDeviceControl(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG_PTR information = 0;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    switch (code)
    {
        case IOCTL_SCSI_GET_ADDRESS:
        {
            PSCSI_ADDRESS address;

            if (buffer == NULL || outLen < sizeof(SCSI_ADDRESS))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            address = (PSCSI_ADDRESS)buffer;
            RtlZeroMemory(address, sizeof(*address));
            address->Length = sizeof(SCSI_ADDRESS);
            address->PortNumber = 0;
            address->PathId = (UCHAR)DeviceExtension->Bus;
            address->TargetId = (UCHAR)DeviceExtension->Target;
            address->Lun = (UCHAR)DeviceExtension->Lun;

            information = sizeof(SCSI_ADDRESS);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_STORAGE_GET_DEVICE_NUMBER:
        {
            PSTORAGE_DEVICE_NUMBER deviceNumber;

            if (buffer == NULL || outLen < sizeof(STORAGE_DEVICE_NUMBER))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            deviceNumber = (PSTORAGE_DEVICE_NUMBER)buffer;
            deviceNumber->DeviceType = DeviceExtension->Device->DeviceType;
            deviceNumber->DeviceNumber = DeviceExtension->Target;
            deviceNumber->PartitionNumber = (ULONG)DeviceExtension->Lun;

            information = sizeof(STORAGE_DEVICE_NUMBER);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_DISK_GET_DRIVE_GEOMETRY:
        {
            if (buffer == NULL || outLen < sizeof(DISK_GEOMETRY))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PortpBuildDiskGeometry(DeviceExtension, (PDISK_GEOMETRY)buffer);
            information = sizeof(DISK_GEOMETRY);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
        {
            PDISK_GEOMETRY_EX geometryEx;
            ULONGLONG computedSize;
            ULONG headerSize = FIELD_OFFSET(DISK_GEOMETRY_EX, Data);

            if (buffer == NULL || outLen < headerSize)
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            geometryEx = (PDISK_GEOMETRY_EX)buffer;
            RtlZeroMemory(geometryEx, headerSize);

            PortpBuildDiskGeometry(DeviceExtension, &geometryEx->Geometry);

            if (DeviceExtension->CapacityValid && DeviceExtension->DiskSize != 0)
            {
                geometryEx->DiskSize.QuadPart = DeviceExtension->DiskSize;
            }
            else
            {
                computedSize = (ULONGLONG)geometryEx->Geometry.Cylinders.QuadPart *
                               geometryEx->Geometry.TracksPerCylinder *
                               geometryEx->Geometry.SectorsPerTrack *
                               geometryEx->Geometry.BytesPerSector;
                geometryEx->DiskSize.QuadPart = (LONGLONG)computedSize;
            }

            if (outLen > headerSize)
            {
                RtlZeroMemory(geometryEx->Data, outLen - headerSize);
            }

            information = headerSize;
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_DISK_GET_LENGTH_INFO:
        {
            PGET_LENGTH_INFORMATION lengthInfo;

            if (buffer == NULL || outLen < sizeof(GET_LENGTH_INFORMATION))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            lengthInfo = (PGET_LENGTH_INFORMATION)buffer;
            if (DeviceExtension->CapacityValid)
                lengthInfo->Length.QuadPart = DeviceExtension->DiskSize;
            else
                lengthInfo->Length.QuadPart = 0;

            information = sizeof(GET_LENGTH_INFORMATION);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_STORAGE_QUERY_PROPERTY:
        {
            PSTORAGE_PROPERTY_QUERY query;

            if (buffer == NULL || stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(STORAGE_PROPERTY_QUERY))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            query = (PSTORAGE_PROPERTY_QUERY)buffer;

            if (query->QueryType != PropertyStandardQuery &&
                query->QueryType != PropertyExistsQuery)
            {
                status = STATUS_NOT_SUPPORTED;
                break;
            }

            if (query->PropertyId == StorageDeviceProperty)
            {
                status = PortBuildDeviceDescriptor(DeviceExtension->InquiryBuffer,
                                                   outLen,
                                                   buffer,
                                                   &information);
                if (status == STATUS_BUFFER_TOO_SMALL && query->QueryType == PropertyExistsQuery)
                    status = STATUS_SUCCESS;
            }
            else if (query->PropertyId == StorageAdapterProperty && DeviceExtension->FdoExtension != NULL)
            {
                status = PortHandleAdapterPropertyQuery(DeviceExtension->FdoExtension,
                                                         outLen,
                                                         buffer,
                                                         &information);
                if (status == STATUS_BUFFER_TOO_SMALL && query->QueryType == PropertyExistsQuery)
                    status = STATUS_SUCCESS;
            }
            else
            {
                status = STATUS_NOT_SUPPORTED;
            }

            break;
        }

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    return PortCompleteRequest(Irp, status, information);
}
