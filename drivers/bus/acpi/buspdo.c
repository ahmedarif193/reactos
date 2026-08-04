#ifndef UNIT_TEST
#include "precomp.h"

#include <initguid.h>
#include <devpkey.h>
#include <poclass.h>
#include <reactos/drivers/reshubio.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

ACPI_STATUS AcpiRsCreateAmlResources(ACPI_BUFFER *ResourceList, ACPI_BUFFER *OutputBuffer);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, Bus_PDO_PnP)
#pragma alloc_text (PAGE, Bus_PDO_QueryDeviceCaps)
#pragma alloc_text (PAGE, Bus_PDO_QueryDeviceId)
#pragma alloc_text (PAGE, Bus_PDO_QueryDeviceText)
#pragma alloc_text (PAGE, Bus_PDO_QueryResources)
#pragma alloc_text (PAGE, Bus_PDO_QueryResourceRequirements)
#pragma alloc_text (PAGE, Bus_PDO_QueryDeviceRelations)
#pragma alloc_text (PAGE, Bus_PDO_QueryBusInformation)
#pragma alloc_text (PAGE, Bus_GetDeviceCapabilities)
#endif
#endif /* !UNIT_TEST */

#ifdef UNIT_TEST
#include <wchar.h>
ACPI_TABLE_FADT AcpiGbl_FADT;
#endif

/* =============================== Helpers =============================== */

static
BOOLEAN
BuspIsPciRootDevice(
    _In_ PPDO_DEVICE_DATA DeviceData)
{
    if (!DeviceData->HardwareIDs)
        return FALSE;

    if (wcsstr(DeviceData->HardwareIDs, L"PNP0A03") != NULL ||
        wcsstr(DeviceData->HardwareIDs, L"PNP0A08") != NULL)
    {
        if (!DeviceData->PciRootLogged)
        {
            DPRINT("ACPI: PDO %p (%S) recognised as PCI root candidate\n",
                   DeviceData,
                   DeviceData->HardwareIDs);
            DeviceData->PciRootLogged = TRUE;
        }
        return TRUE;
    }

    return FALSE;
}

#ifndef UNIT_TEST
static
ULONG
BuspEnsurePciRootBusNumber(
    _Inout_ PPDO_DEVICE_DATA DeviceData);

static
VOID
BuspSetUint32DeviceProperty(
    _In_ PPDO_DEVICE_DATA DeviceData,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ ULONG Value)
{
    NTSTATUS Status;

    Status = IoSetDevicePropertyData(DeviceData->Common.Self,
                                     PropertyKey,
                                     0,
                                     0,
                                     DEVPROP_TYPE_UINT32,
                                     sizeof(Value),
                                     &Value);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ACPI: IoSetDevicePropertyData(%lu) failed for pid %lu: 0x%08lx\n",
               Value,
               PropertyKey->pid,
               Status);
    }
}

static
VOID
BuspPublishDeviceProperties(
    _Inout_ PPDO_DEVICE_DATA DeviceData,
    _In_opt_ struct acpi_device *Device)
{
    if (BuspIsPciRootDevice(DeviceData))
    {
        ULONG BusNumber = BuspEnsurePciRootBusNumber(DeviceData);
        BuspSetUint32DeviceProperty(DeviceData, &DEVPKEY_Device_BusNumber, BusNumber);
    }

    if (Device != NULL)
    {
        ULONG Address = (ULONG)Device->pnp.bus_address;
        BuspSetUint32DeviceProperty(DeviceData, &DEVPKEY_Device_Address, Address);
    }
}

static
VOID
BuspApplyTrackedPciRootInfo(
    _Inout_ PPDO_DEVICE_DATA DeviceData)
{
    ULONG Segment, MinBus, MaxBus;

    if (!DeviceData || !DeviceData->AcpiHandle)
        return;

    if (!BuspIsPciRootDevice(DeviceData))
        return;

    if (!AcpiPciRootQueryInfo(DeviceData->AcpiHandle,
                              &Segment,
                              &MinBus,
                              &MaxBus))
    {
        return;
    }

    DeviceData->HasPciRootSegment = TRUE;
    DeviceData->PciRootSegment = Segment;

    if (!DeviceData->HasPciRootBusRange)
    {
        DeviceData->PciRootMinBus = MinBus;
        DeviceData->PciRootMaxBus = MaxBus;
        DeviceData->HasPciRootBusRange = TRUE;
    }
}
#else
static
VOID
BuspApplyTrackedPciRootInfo(
    _Inout_ PPDO_DEVICE_DATA DeviceData)
{
    UNREFERENCED_PARAMETER(DeviceData);
}
#endif

static __inline VOID
BuspCachePciRootIoWindow(
    _Inout_ PPDO_DEVICE_DATA DeviceData,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End)
{
    if (!DeviceData) return;
    if (!BuspIsPciRootDevice(DeviceData)) return;
    if (DeviceData->PciRootIoWindowCount < ACPI_PCI_MAX_WINDOWS)
    {
        ULONG i = DeviceData->PciRootIoWindowCount++;
        DeviceData->PciRootIoWindows[i].Start = Start;
        DeviceData->PciRootIoWindows[i].End = End;
    }
    else
    {
        DPRINT1("ACPI: PCI root %S exceeded IO window cache capacity (%u); ignoring window [0x%I64x,0x%I64x]\n",
                DeviceData->HardwareIDs ? DeviceData->HardwareIDs : L"<unknown>",
                ACPI_PCI_MAX_WINDOWS,
                Start,
                End);
    }
}

static __inline VOID
BuspCachePciRootMemWindow(
    _Inout_ PPDO_DEVICE_DATA DeviceData,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End,
    _In_ BOOLEAN Prefetchable)
{
    if (!DeviceData) return;
    if (!BuspIsPciRootDevice(DeviceData)) return;
    if (DeviceData->PciRootMemWindowCount < ACPI_PCI_MAX_WINDOWS)
    {
        ULONG i = DeviceData->PciRootMemWindowCount++;
        DeviceData->PciRootMemWindows[i].Start = Start;
        DeviceData->PciRootMemWindows[i].End = End;
        DeviceData->PciRootMemWindows[i].Prefetchable = Prefetchable ? TRUE : FALSE;
    }
    else
    {
        DPRINT1("ACPI: PCI root %S exceeded memory window cache capacity (%u); ignoring window [0x%I64x,0x%I64x] prefetch=%d\n",
                DeviceData->HardwareIDs ? DeviceData->HardwareIDs : L"<unknown>",
                ACPI_PCI_MAX_WINDOWS,
                Start,
                End,
                Prefetchable ? 1 : 0);
    }
}

static
BOOLEAN
BuspShouldForceSharedInterrupt(
    _In_ PPDO_DEVICE_DATA DeviceData,
    _In_ ULONG Interrupt)
{
    if (!AcpiGbl_FADT.SciInterrupt)
        return FALSE;

    if (!BuspIsPciRootDevice(DeviceData))
        return FALSE;

    return (Interrupt == AcpiGbl_FADT.SciInterrupt);
}

static
ULONG
BuspEnsurePciRootBusNumber(
    _Inout_ PPDO_DEVICE_DATA DeviceData)
{
#ifdef UNIT_TEST
    UNREFERENCED_PARAMETER(DeviceData);
    return 0;
#else
    struct acpi_device *device = NULL;
    ULONGLONG BusNumber = 0;
    ACPI_STATUS AcpiStatus;

    if (DeviceData->HasCachedBusNumber)
        return DeviceData->CachedBusNumber;

    if (DeviceData->AcpiHandle)
        acpi_bus_get_device(DeviceData->AcpiHandle, &device);

    AcpiStatus = acpi_evaluate_integer(DeviceData->AcpiHandle, "_BBN", NULL, &BusNumber);
    if (ACPI_SUCCESS(AcpiStatus))
    {
        DPRINT1("ACPI: Using _BBN for PCI root %S bus=%I64u\n",
                DeviceData->HardwareIDs ? DeviceData->HardwareIDs : L"<unknown>",
                BusNumber);
    }
    else if (device && device->flags.bus_address)
    {
        BusNumber = (ULONGLONG)device->pnp.bus_address;
        DPRINT1("ACPI: Using ACPI bus_address for PCI root %S bus=%I64u\n",
                DeviceData->HardwareIDs ? DeviceData->HardwareIDs : L"<unknown>",
                BusNumber);
    }
    else if (device && device->flags.unique_id && device->pnp.unique_id[0])
    {
        ULONG ParsedUid;
        if (NT_SUCCESS(RtlCharToInteger(device->pnp.unique_id, 10, &ParsedUid)))
        {
            BusNumber = ParsedUid;
            DPRINT1("ACPI: Using decimal _UID for PCI root %S bus=%I64u\n",
                    DeviceData->HardwareIDs ? DeviceData->HardwareIDs : L"<unknown>",
                    BusNumber);
        }
        else if (NT_SUCCESS(RtlCharToInteger(device->pnp.unique_id, 16, &ParsedUid)))
        {
            BusNumber = ParsedUid;
            DPRINT1("ACPI: Using hex _UID for PCI root %S bus=%I64u\n",
                    DeviceData->HardwareIDs ? DeviceData->HardwareIDs : L"<unknown>",
                    BusNumber);
        }
        else
        {
            DPRINT1("ACPI: Failed to parse _UID '%s' for PCI root %S\n",
                    device->pnp.unique_id,
                    DeviceData->HardwareIDs ? DeviceData->HardwareIDs : L"<unknown>");
            BusNumber = 0;
        }
    }
    else
    {
        BusNumber = 0;
        DPRINT1("ACPI: No bus number source for PCI root %S, defaulting to 0\n",
                DeviceData->HardwareIDs ? DeviceData->HardwareIDs : L"<unknown>");
    }

    DeviceData->CachedBusNumber = (ULONG)BusNumber;
    DeviceData->HasCachedBusNumber = TRUE;

    DPRINT1("ACPI: Cached PCI root %S bus number %lu\n",
            DeviceData->HardwareIDs ? DeviceData->HardwareIDs : L"<unknown>",
            DeviceData->CachedBusNumber);

    return DeviceData->CachedBusNumber;
#endif
}

static
VOID
BuspRecordPciRootBusRange(
    _Inout_ PPDO_DEVICE_DATA DeviceData,
    _In_ ULONG MinBus,
    _In_ ULONG MaxBus)
{
    if (MinBus > 0xFF)
        MinBus = 0xFF;
    if (MaxBus > 0xFF)
        MaxBus = 0xFF;

    if (!DeviceData->HasPciRootBusRange)
    {
        DeviceData->PciRootMinBus = MinBus;
        DeviceData->PciRootMaxBus = MaxBus;
        DeviceData->HasPciRootBusRange = TRUE;
        return;
    }

    if (MinBus < DeviceData->PciRootMinBus)
        DeviceData->PciRootMinBus = MinBus;
    if (MaxBus > DeviceData->PciRootMaxBus)
        DeviceData->PciRootMaxBus = MaxBus;
}

static
BOOLEAN
BuspUseAddressResource(
    _In_ BOOLEAN IsPciRoot,
    _In_ UCHAR ProducerConsumer,
    _In_ ULONGLONG AddressLength)
{
    /* PCI root _CRS address descriptors describe windows produced for
     * children.  Ordinary ACPI PDOs consume their address descriptors. */
    if (ProducerConsumer == ACPI_PRODUCER && !IsPciRoot)
        return FALSE;

    /* IO_RESOURCE_DESCRIPTOR and the ordinary CM memory descriptor carry a
     * 32-bit length.  Keep oversized root windows out until MemoryLarge is
     * available end-to-end; other representable windows remain usable. */
    if (AddressLength == 0 || AddressLength > MAXULONG)
        return FALSE;

    return TRUE;
}

static
BOOLEAN
BuspGetConnectionResourceInfo(
    _In_ ACPI_RESOURCE *Resource,
    _Out_ PUCHAR ConnectionClass,
    _Out_ PUCHAR ConnectionType,
    _Out_ PUCHAR ShareDisposition)
{
    ACPI_RESOURCE_COMMON_SERIALBUS *SerialBus;

    if (Resource->Type == ACPI_RESOURCE_TYPE_GPIO)
    {
        ACPI_RESOURCE_GPIO *Gpio = &Resource->Data.Gpio;

        if (Gpio->ProducerConsumer == ACPI_PRODUCER || Gpio->ConnectionType != ACPI_RESOURCE_GPIO_TYPE_IO)
            return FALSE;

        *ConnectionClass = CM_RESOURCE_CONNECTION_CLASS_GPIO;
        *ConnectionType = CM_RESOURCE_CONNECTION_TYPE_GPIO_IO;
        *ShareDisposition = Gpio->Shareable == ACPI_SHARED ? CmResourceShareShared : CmResourceShareDeviceExclusive;
        return TRUE;
    }

    if (Resource->Type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
        return FALSE;

    SerialBus = &Resource->Data.CommonSerialBus;
    if (SerialBus->ProducerConsumer == ACPI_PRODUCER)
        return FALSE;

    *ConnectionClass = CM_RESOURCE_CONNECTION_CLASS_SERIAL;
    *ShareDisposition = SerialBus->ConnectionSharing ? CmResourceShareShared : CmResourceShareDeviceExclusive;
    switch (SerialBus->Type)
    {
        case ACPI_RESOURCE_SERIAL_TYPE_I2C:
            *ConnectionType = CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C;
            return TRUE;

        case ACPI_RESOURCE_SERIAL_TYPE_SPI:
            *ConnectionType = CM_RESOURCE_CONNECTION_TYPE_SERIAL_SPI;
            return TRUE;

        case ACPI_RESOURCE_SERIAL_TYPE_UART:
            *ConnectionType = CM_RESOURCE_CONNECTION_TYPE_SERIAL_UART;
            return TRUE;

        default:
            return FALSE;
    }
}

static
ULONGLONG
BuspGetConnectionId(
    _In_ PPDO_DEVICE_DATA DeviceData,
    _In_ ULONG ResourceIndex,
    _In_ UCHAR ConnectionClass,
    _In_ UCHAR ConnectionType)
{
    ACPI_BUFFER Path = {ACPI_ALLOCATE_BUFFER, NULL};
    ULONGLONG Hash = 1469598103934665603ULL;
    PUCHAR Character;

    if (ACPI_SUCCESS(AcpiGetName(DeviceData->AcpiHandle, ACPI_FULL_PATHNAME, &Path)) && Path.Pointer)
    {
        for (Character = Path.Pointer; *Character; Character++)
        {
            Hash ^= *Character;
            Hash *= 1099511628211ULL;
        }
        ACPI_FREE(Path.Pointer);
    }
    else
    {
        Hash ^= (ULONG_PTR)DeviceData->AcpiHandle;
        Hash *= 1099511628211ULL;
    }

    Hash ^= ResourceIndex;
    Hash *= 1099511628211ULL;
    Hash ^= ((ULONG)ConnectionClass << 8) | ConnectionType;
    Hash *= 1099511628211ULL;
    return Hash ? Hash : 1;
}

#ifndef UNIT_TEST
static
VOID
BuspRegisterConnectionResource(
    _In_ ACPI_RESOURCE *Resource,
    _In_ ULONGLONG ConnectionId,
    _In_ UCHAR ConnectionClass,
    _In_ UCHAR ConnectionType)
{
    static const UNICODE_STRING ResourceHubName = RTL_CONSTANT_STRING(RESOURCE_HUB_DEVICE_NAME);
    ACPI_BUFFER ResourceBuffer;
    ACPI_BUFFER AmlBuffer = {ACPI_ALLOCATE_BUFFER, NULL};
    ACPI_RESOURCE *ResourceCopy;
    ACPI_RESOURCE *EndTag;
    PRH_REGISTER_CONNECTION_INPUT Input;
    PFILE_OBJECT FileObject = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    ACPI_STATUS AcpiStatus;
    NTSTATUS Status;
    ULONG EndTagLength = ACPI_RS_SIZE(ACPI_RESOURCE_END_TAG);
    ULONG ResourceBufferLength;
    ULONG InputLength;
    USHORT AmlPayloadLength;
    ULONG PropertiesLength;

    if (Resource->Length > MAXULONG - EndTagLength)
        return;
    ResourceBufferLength = Resource->Length + EndTagLength;
    ResourceCopy = ExAllocatePoolWithTag(PagedPool, ResourceBufferLength, 'cRhA');
    if (!ResourceCopy)
        return;
    RtlCopyMemory(ResourceCopy, Resource, Resource->Length);
    EndTag = (ACPI_RESOURCE *)((PUCHAR)ResourceCopy + Resource->Length);
    RtlZeroMemory(EndTag, EndTagLength);
    EndTag->Type = ACPI_RESOURCE_TYPE_END_TAG;
    EndTag->Length = EndTagLength;
    ResourceBuffer.Length = ResourceBufferLength;
    ResourceBuffer.Pointer = ResourceCopy;
    AcpiStatus = AcpiRsCreateAmlResources(&ResourceBuffer, &AmlBuffer);
    ExFreePoolWithTag(ResourceCopy, 'cRhA');
    if (ACPI_FAILURE(AcpiStatus) || !AmlBuffer.Pointer || AmlBuffer.Length < 3)
    {
        if (AmlBuffer.Pointer)
            ACPI_FREE(AmlBuffer.Pointer);
        return;
    }
    RtlCopyMemory(&AmlPayloadLength, (PUCHAR)AmlBuffer.Pointer + 1, sizeof(AmlPayloadLength));
    PropertiesLength = 3 + AmlPayloadLength;
    if (!(*(PUCHAR)AmlBuffer.Pointer & 0x80) || PropertiesLength > AmlBuffer.Length || PropertiesLength > MAXULONG - FIELD_OFFSET(RH_REGISTER_CONNECTION_INPUT, Properties))
    {
        ACPI_FREE(AmlBuffer.Pointer);
        return;
    }

    InputLength = FIELD_OFFSET(RH_REGISTER_CONNECTION_INPUT, Properties) + PropertiesLength;
    Input = ExAllocatePoolWithTag(PagedPool, InputLength, 'iRhA');
    if (!Input)
    {
        ACPI_FREE(AmlBuffer.Pointer);
        return;
    }
    RtlZeroMemory(Input, FIELD_OFFSET(RH_REGISTER_CONNECTION_INPUT, Properties));
    Input->Version = RH_REGISTER_CONNECTION_VERSION;
    Input->ConnectionId.QuadPart = ConnectionId;
    Input->Class = ConnectionClass;
    Input->Type = ConnectionType;
    Input->PropertiesLength = PropertiesLength;
    RtlCopyMemory(Input->Properties, AmlBuffer.Pointer, PropertiesLength);
    ACPI_FREE(AmlBuffer.Pointer);

    Status = IoGetDeviceObjectPointer((PUNICODE_STRING)&ResourceHubName, FILE_READ_DATA | FILE_WRITE_DATA, &FileObject, &DeviceObject);
    if (NT_SUCCESS(Status))
    {
        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Irp = IoBuildDeviceIoControlRequest(IOCTL_RH_REGISTER_CONNECTION, DeviceObject, Input, InputLength, NULL, 0, FALSE, &Event, &IoStatus);
        if (Irp)
        {
            Status = IoCallDriver(DeviceObject, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = IoStatus.Status;
            }
        }
        else
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        ObDereferenceObject(FileObject);
    }
    if (!NT_SUCCESS(Status))
        DPRINT1("ACPI: Resource Hub registration failed for %08lx%08lx (0x%08lx)\n", (ULONG)(ConnectionId >> 32), (ULONG)ConnectionId, Status);
    ExFreePoolWithTag(Input, 'iRhA');
}
#else
#define BuspRegisterConnectionResource(Resource, ConnectionId, ConnectionClass, ConnectionType) ((void)0)
#endif

static
NTSTATUS
BuspCountRequirementsFromAcpiResources(
    _In_ PPDO_DEVICE_DATA DeviceData,
    _In_ ACPI_RESOURCE *FirstResource,
    _In_ BOOLEAN CurrentRes,
    _In_ BOOLEAN IsPciRoot,
    _In_ ULONG RootBusNumber,
    _Out_ PULONG NumberOfResourcesOut,
    _Out_ PBOOLEAN AppendSyntheticBusOut)
{
    ACPI_RESOURCE *resource;
    BOOLEAN SeenStartDependent = FALSE;
    ULONG NumberOfResources = 0;
    BOOLEAN AppendSyntheticBus = FALSE;
    BOOLEAN FoundBusRange = FALSE;

    PAGED_CODE();

    if (!NumberOfResourcesOut || !AppendSyntheticBusOut || !FirstResource)
        return STATUS_INVALID_PARAMETER;

    resource = FirstResource;
    while (resource->Type != ACPI_RESOURCE_TYPE_END_TAG &&
           resource->Type != ACPI_RESOURCE_TYPE_END_DEPENDENT)
    {
        if (resource->Type == ACPI_RESOURCE_TYPE_START_DEPENDENT)
        {
            if (SeenStartDependent)
                break;
            SeenStartDependent = TRUE;
            resource = ACPI_NEXT_RESOURCE(resource);
            continue;
        }

        switch (resource->Type)
        {
            case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
            {
                ACPI_RESOURCE_EXTENDED_IRQ *irq = &resource->Data.ExtendedIrq;
                if (irq->ProducerConsumer != ACPI_PRODUCER)
                    NumberOfResources += irq->InterruptCount;
                break;
            }

            case ACPI_RESOURCE_TYPE_IRQ:
            {
                ACPI_RESOURCE_IRQ *irq = &resource->Data.Irq;
                NumberOfResources += irq->InterruptCount;
                break;
            }

            case ACPI_RESOURCE_TYPE_DMA:
            {
                ACPI_RESOURCE_DMA *dma = &resource->Data.Dma;
                NumberOfResources += dma->ChannelCount;
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS16:
            {
                ACPI_RESOURCE_ADDRESS16 *addr16 = &resource->Data.Address16;
                if (BuspUseAddressResource(IsPciRoot, addr16->ProducerConsumer, addr16->Address.AddressLength))
                {
                    NumberOfResources++;
                    if (addr16->ResourceType == ACPI_BUS_NUMBER_RANGE)
                    {
                        FoundBusRange = TRUE;
                        /* Correct bus range: end is Maximum (or Minimum + Length - 1), not both */
                        BuspRecordPciRootBusRange(DeviceData,
                                                  addr16->Address.Minimum,
                                                  addr16->Address.Maximum);
                    }
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS32:
            {
                ACPI_RESOURCE_ADDRESS32 *addr32 = &resource->Data.Address32;
                if (BuspUseAddressResource(IsPciRoot, addr32->ProducerConsumer, addr32->Address.AddressLength))
                {
                    NumberOfResources++;
                    if (addr32->ResourceType == ACPI_BUS_NUMBER_RANGE)
                    {
                        FoundBusRange = TRUE;
                        BuspRecordPciRootBusRange(DeviceData,
                                                  addr32->Address.Minimum,
                                                  addr32->Address.Maximum);
                    }
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS64:
            {
                ACPI_RESOURCE_ADDRESS64 *addr64 = &resource->Data.Address64;
                if (BuspUseAddressResource(IsPciRoot, addr64->ProducerConsumer, addr64->Address.AddressLength))
                {
                    NumberOfResources++;
                    if (addr64->ResourceType == ACPI_BUS_NUMBER_RANGE)
                    {
                        FoundBusRange = TRUE;
                        BuspRecordPciRootBusRange(DeviceData,
                                                  (ULONG)addr64->Address.Minimum,
                                                  (ULONG)addr64->Address.Maximum);
                    }
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
            {
                ACPI_RESOURCE_EXTENDED_ADDRESS64 *addrx = &resource->Data.ExtAddress64;
                if (BuspUseAddressResource(IsPciRoot, addrx->ProducerConsumer, addrx->Address.AddressLength))
                {
                    NumberOfResources++;
                    if (addrx->ResourceType == ACPI_BUS_NUMBER_RANGE)
                    {
                        FoundBusRange = TRUE;
                        BuspRecordPciRootBusRange(DeviceData,
                                                  (ULONG)addrx->Address.Minimum,
                                                  (ULONG)addrx->Address.Maximum);
                    }
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_MEMORY24:
            case ACPI_RESOURCE_TYPE_MEMORY32:
            case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
            case ACPI_RESOURCE_TYPE_FIXED_IO:
            case ACPI_RESOURCE_TYPE_IO:
            case ACPI_RESOURCE_TYPE_GENERIC_REGISTER:
                NumberOfResources++;
                break;

            case ACPI_RESOURCE_TYPE_GPIO:
            case ACPI_RESOURCE_TYPE_SERIAL_BUS:
            {
                UCHAR ConnectionClass;
                UCHAR ConnectionType;
                UCHAR ShareDisposition;

                if (BuspGetConnectionResourceInfo(resource, &ConnectionClass, &ConnectionType, &ShareDisposition))
                    NumberOfResources++;
                break;
            }

            default:
                break;
        }

        resource = ACPI_NEXT_RESOURCE(resource);
    }

    if (IsPciRoot && !FoundBusRange)
    {
        AppendSyntheticBus = TRUE;
        NumberOfResources++;
        BuspRecordPciRootBusRange(DeviceData, RootBusNumber, RootBusNumber);
    }

    *NumberOfResourcesOut = NumberOfResources;
    *AppendSyntheticBusOut = AppendSyntheticBus;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BuspCreateRequirementsListFromAcpiResources(
    _In_ PPDO_DEVICE_DATA DeviceData,
    _In_ ACPI_RESOURCE *FirstResource,
    _In_ BOOLEAN CurrentRes,
    _In_ BOOLEAN IsPciRoot,
    _In_ ULONG RootBusNumber,
    _Outptr_result_maybenull_ PIO_RESOURCE_REQUIREMENTS_LIST *RequirementsListOut)
{
    ACPI_RESOURCE *resource;
    BOOLEAN SeenStartDependent = FALSE;
    BOOLEAN AppendSyntheticBus = FALSE;
    ULONG NumberOfResources = 0;
    ULONG RequirementsListSize;
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;
    PIO_RESOURCE_DESCRIPTOR RequirementDescriptor;
    ULONG i;
    ULONG ResourceIndex = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (!RequirementsListOut || !FirstResource)
        return STATUS_INVALID_PARAMETER;

    *RequirementsListOut = NULL;

    Status = BuspCountRequirementsFromAcpiResources(DeviceData,
                                                    FirstResource,
                                                    CurrentRes,
                                                    IsPciRoot,
                                                    RootBusNumber,
                                                    &NumberOfResources,
                                                    &AppendSyntheticBus);
    if (!NT_SUCCESS(Status))
        return Status;

    if (NumberOfResources == 0)
        return STATUS_SUCCESS;

    RequirementsListSize = sizeof(IO_RESOURCE_REQUIREMENTS_LIST) +
                           sizeof(IO_RESOURCE_DESCRIPTOR) * (NumberOfResources - 1);

    RequirementsList = ExAllocatePoolWithTag(PagedPool, RequirementsListSize, 'RpcA');
    if (!RequirementsList)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(RequirementsList, RequirementsListSize);
    RequirementsList->ListSize = RequirementsListSize;
    if (IsPciRoot && DeviceData->HasPciRootBusRange)
    {
        RequirementsList->InterfaceType = PCIBus;
        RequirementsList->BusNumber = DeviceData->PciRootMinBus;
    }
    else
    {
        RequirementsList->InterfaceType = IsPciRoot ? PCIBus : Internal;
        RequirementsList->BusNumber = IsPciRoot ?
                                      RootBusNumber :
                                      (DeviceData->HasCachedBusNumber ?
                                       DeviceData->CachedBusNumber : 0);
    }
    RequirementsList->SlotNumber = 0;
    RequirementsList->AlternativeLists = 1;
    RequirementsList->List[0].Version = 1;
    RequirementsList->List[0].Revision = 1;
    RequirementsList->List[0].Count = NumberOfResources;
    RequirementDescriptor = RequirementsList->List[0].Descriptors;

    SeenStartDependent = FALSE;
    resource = FirstResource;
    while (resource->Type != ACPI_RESOURCE_TYPE_END_TAG &&
           resource->Type != ACPI_RESOURCE_TYPE_END_DEPENDENT)
    {
        ULONG CurrentResourceIndex = ResourceIndex++;

        if (resource->Type == ACPI_RESOURCE_TYPE_START_DEPENDENT)
        {
            if (SeenStartDependent)
                break;
            SeenStartDependent = TRUE;
            resource = ACPI_NEXT_RESOURCE(resource);
            continue;
        }

        switch (resource->Type)
        {
            case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
            {
                ACPI_RESOURCE_EXTENDED_IRQ *irq = &resource->Data.ExtendedIrq;
                if (irq->ProducerConsumer == ACPI_PRODUCER)
                    break;

                for (i = 0; i < irq->InterruptCount; i++)
                {
                    ULONG Interrupt = irq->Interrupts[i];

                    RequirementDescriptor->Option = (i == 0) ? IO_RESOURCE_PREFERRED : IO_RESOURCE_ALTERNATIVE;
                    RequirementDescriptor->Type = CmResourceTypeInterrupt;
                    RequirementDescriptor->ShareDisposition =
                        (BuspShouldForceSharedInterrupt(DeviceData, Interrupt) ||
                         irq->Shareable == ACPI_SHARED) ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags =
                        (irq->Triggering == ACPI_LEVEL_SENSITIVE ?
                         CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE : CM_RESOURCE_INTERRUPT_LATCHED);
                    RequirementDescriptor->u.Interrupt.MinimumVector =
                    RequirementDescriptor->u.Interrupt.MaximumVector = Interrupt;
                    RequirementDescriptor++;
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_IRQ:
            {
                ACPI_RESOURCE_IRQ *irq = &resource->Data.Irq;

                for (i = 0; i < irq->InterruptCount; i++)
                {
                    ULONG Interrupt = irq->Interrupts[i];

                    RequirementDescriptor->Option = (i == 0) ? IO_RESOURCE_PREFERRED : IO_RESOURCE_ALTERNATIVE;
                    RequirementDescriptor->Type = CmResourceTypeInterrupt;
                    RequirementDescriptor->ShareDisposition =
                        (BuspShouldForceSharedInterrupt(DeviceData, Interrupt) ||
                         irq->Shareable == ACPI_SHARED) ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags =
                        (irq->Triggering == ACPI_LEVEL_SENSITIVE ?
                         CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE : CM_RESOURCE_INTERRUPT_LATCHED);
                    RequirementDescriptor->u.Interrupt.MinimumVector =
                    RequirementDescriptor->u.Interrupt.MaximumVector = Interrupt;
                    RequirementDescriptor++;
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_DMA:
            {
                ACPI_RESOURCE_DMA *dma = &resource->Data.Dma;

                for (i = 0; i < dma->ChannelCount; i++)
                {
                    RequirementDescriptor->Type = CmResourceTypeDma;
                    RequirementDescriptor->Flags = 0;

                    switch (dma->Type)
                    {
                        case ACPI_TYPE_A: RequirementDescriptor->Flags |= CM_RESOURCE_DMA_TYPE_A; break;
                        case ACPI_TYPE_B: RequirementDescriptor->Flags |= CM_RESOURCE_DMA_TYPE_B; break;
                        case ACPI_TYPE_F: RequirementDescriptor->Flags |= CM_RESOURCE_DMA_TYPE_F; break;
                    }

                    if (dma->BusMaster == ACPI_BUS_MASTER)
                        RequirementDescriptor->Flags |= CM_RESOURCE_DMA_BUS_MASTER;

                    switch (dma->Transfer)
                    {
                        case ACPI_TRANSFER_8: RequirementDescriptor->Flags |= CM_RESOURCE_DMA_8; break;
                        case ACPI_TRANSFER_16: RequirementDescriptor->Flags |= CM_RESOURCE_DMA_16; break;
                        case ACPI_TRANSFER_8_16: RequirementDescriptor->Flags |= CM_RESOURCE_DMA_8_AND_16; break;
                    }

                    RequirementDescriptor->Option = (i == 0) ? IO_RESOURCE_PREFERRED : IO_RESOURCE_ALTERNATIVE;
                    RequirementDescriptor->ShareDisposition = CmResourceShareDriverExclusive;
                    RequirementDescriptor->u.Dma.MinimumChannel =
                    RequirementDescriptor->u.Dma.MaximumChannel = dma->Channels[i];
                    RequirementDescriptor++;
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_IO:
            {
                ACPI_RESOURCE_IO *io = &resource->Data.Io;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;
                RequirementDescriptor->Type = CmResourceTypePort;
                RequirementDescriptor->ShareDisposition = CmResourceShareDriverExclusive;
                RequirementDescriptor->Flags = CM_RESOURCE_PORT_IO |
                    (io->IoDecode == ACPI_DECODE_16 ? CM_RESOURCE_PORT_16_BIT_DECODE : CM_RESOURCE_PORT_10_BIT_DECODE);
                RequirementDescriptor->u.Port.Alignment = io->Alignment ? io->Alignment : 1;
                RequirementDescriptor->u.Port.Length = io->AddressLength;
                RequirementDescriptor->u.Port.MinimumAddress.QuadPart = io->Minimum;
                RequirementDescriptor->u.Port.MaximumAddress.QuadPart = io->Maximum + io->AddressLength - 1;
                RequirementDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_FIXED_IO:
            {
                ACPI_RESOURCE_FIXED_IO *io = &resource->Data.FixedIo;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;
                RequirementDescriptor->Type = CmResourceTypePort;
                RequirementDescriptor->ShareDisposition = CmResourceShareDriverExclusive;
                RequirementDescriptor->Flags = CM_RESOURCE_PORT_IO;
                RequirementDescriptor->u.Port.Alignment = 1;
                RequirementDescriptor->u.Port.Length = io->AddressLength;
                RequirementDescriptor->u.Port.MinimumAddress.QuadPart = io->Address;
                RequirementDescriptor->u.Port.MaximumAddress.QuadPart = io->Address + io->AddressLength - 1;
                RequirementDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS16:
            {
                ACPI_RESOURCE_ADDRESS16 *addr16 = &resource->Data.Address16;

                if (!BuspUseAddressResource(IsPciRoot, addr16->ProducerConsumer, addr16->Address.AddressLength))
                    break;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;

                if (addr16->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    RequirementDescriptor->Type = CmResourceTypeBusNumber;
                    RequirementDescriptor->ShareDisposition = CmResourceShareShared;
                    RequirementDescriptor->Flags = 0;
                    RequirementDescriptor->u.BusNumber.MinBusNumber = addr16->Address.Minimum;
                    /* Correct bus range upper bound: use Maximum */
                    RequirementDescriptor->u.BusNumber.MaxBusNumber = addr16->Address.Maximum;
                    RequirementDescriptor->u.BusNumber.Length = addr16->Address.AddressLength;
                }
                else if (addr16->ResourceType == ACPI_IO_RANGE)
                {
                    ULONGLONG Minimum = addr16->Address.Minimum + addr16->Address.TranslationOffset;
                    ULONGLONG Maximum = addr16->Address.Maximum + addr16->Address.TranslationOffset;
                    ULONG Alignment = (ULONG)addr16->Address.Granularity + 1;

                    if (Alignment == 0)
                        Alignment = 1;

                    RequirementDescriptor->Type = CmResourceTypePort;
                    RequirementDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags = CM_RESOURCE_PORT_IO |
                        (addr16->Decode == ACPI_POS_DECODE ? CM_RESOURCE_PORT_POSITIVE_DECODE : 0);
                    if (IsPciRoot)
                        RequirementDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                    RequirementDescriptor->u.Port.Alignment = Alignment;
                    RequirementDescriptor->u.Port.Length = addr16->Address.AddressLength;
                    RequirementDescriptor->u.Port.MinimumAddress.QuadPart = Minimum;
                    RequirementDescriptor->u.Port.MaximumAddress.QuadPart = Maximum + addr16->Address.AddressLength - 1;
                }
                else
                {
                    ULONGLONG Minimum = addr16->Address.Minimum + addr16->Address.TranslationOffset;
                    ULONGLONG Maximum = addr16->Address.Maximum + addr16->Address.TranslationOffset;
                    ULONG Alignment = (ULONG)addr16->Address.Granularity + 1;

                    if (Alignment == 0)
                        Alignment = 1;

                    RequirementDescriptor->Type = CmResourceTypeMemory;
                    RequirementDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags = (addr16->Info.Mem.WriteProtect == ACPI_READ_ONLY_MEMORY)
                        ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;

                    switch (addr16->Info.Mem.Caching)
                    {
                        case ACPI_CACHABLE_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_CACHEABLE; break;
                        case ACPI_WRITE_COMBINING_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_COMBINEDWRITE; break;
                        case ACPI_PREFETCHABLE_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_PREFETCHABLE; break;
                    }

                    if (IsPciRoot)
                        RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;

                    RequirementDescriptor->u.Memory.Alignment = Alignment;
                    RequirementDescriptor->u.Memory.Length = addr16->Address.AddressLength;
                    RequirementDescriptor->u.Memory.MinimumAddress.QuadPart = Minimum;
                    RequirementDescriptor->u.Memory.MaximumAddress.QuadPart = Maximum + addr16->Address.AddressLength - 1;
                }

                RequirementDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS32:
            {
                ACPI_RESOURCE_ADDRESS32 *addr32 = &resource->Data.Address32;

                if (!BuspUseAddressResource(IsPciRoot, addr32->ProducerConsumer, addr32->Address.AddressLength))
                    break;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;

                if (addr32->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    RequirementDescriptor->Type = CmResourceTypeBusNumber;
                    RequirementDescriptor->ShareDisposition = CmResourceShareShared;
                    RequirementDescriptor->Flags = 0;
                    RequirementDescriptor->u.BusNumber.MinBusNumber = addr32->Address.Minimum;
                    RequirementDescriptor->u.BusNumber.MaxBusNumber = addr32->Address.Maximum;
                    RequirementDescriptor->u.BusNumber.Length = addr32->Address.AddressLength;
                }
                else if (addr32->ResourceType == ACPI_IO_RANGE)
                {
                    ULONGLONG Minimum = addr32->Address.Minimum + addr32->Address.TranslationOffset;
                    ULONGLONG Maximum = addr32->Address.Maximum + addr32->Address.TranslationOffset;
                    ULONG Alignment = (ULONG)addr32->Address.Granularity + 1;

                    if (Alignment == 0)
                        Alignment = 1;

                    RequirementDescriptor->Type = CmResourceTypePort;
                    RequirementDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags = CM_RESOURCE_PORT_IO |
                        (addr32->Decode == ACPI_POS_DECODE ? CM_RESOURCE_PORT_POSITIVE_DECODE : 0);
                    if (IsPciRoot)
                        RequirementDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                    RequirementDescriptor->u.Port.Alignment = Alignment;
                    RequirementDescriptor->u.Port.Length = addr32->Address.AddressLength;
                    RequirementDescriptor->u.Port.MinimumAddress.QuadPart = Minimum;
                    RequirementDescriptor->u.Port.MaximumAddress.QuadPart = Maximum + addr32->Address.AddressLength - 1;
                }
                else
                {
                    ULONGLONG Minimum = addr32->Address.Minimum + addr32->Address.TranslationOffset;
                    ULONGLONG Maximum = addr32->Address.Maximum + addr32->Address.TranslationOffset;
                    ULONG Alignment = (ULONG)addr32->Address.Granularity + 1;

                    if (Alignment == 0)
                        Alignment = 1;

                    RequirementDescriptor->Type = CmResourceTypeMemory;
                    RequirementDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags = (addr32->Info.Mem.WriteProtect == ACPI_READ_ONLY_MEMORY)
                        ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;

                    switch (addr32->Info.Mem.Caching)
                    {
                        case ACPI_CACHABLE_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_CACHEABLE; break;
                        case ACPI_WRITE_COMBINING_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_COMBINEDWRITE; break;
                        case ACPI_PREFETCHABLE_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_PREFETCHABLE; break;
                    }

                    if (IsPciRoot)
                        RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;

                    RequirementDescriptor->u.Memory.Alignment = Alignment;
                    RequirementDescriptor->u.Memory.Length = addr32->Address.AddressLength;
                    RequirementDescriptor->u.Memory.MinimumAddress.QuadPart = Minimum;
                    RequirementDescriptor->u.Memory.MaximumAddress.QuadPart = Maximum + addr32->Address.AddressLength - 1;
                }

                RequirementDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS64:
            {
                ACPI_RESOURCE_ADDRESS64 *addr64 = &resource->Data.Address64;

                if (!BuspUseAddressResource(IsPciRoot, addr64->ProducerConsumer, addr64->Address.AddressLength))
                    break;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;

                if (addr64->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    RequirementDescriptor->Type = CmResourceTypeBusNumber;
                    RequirementDescriptor->ShareDisposition = CmResourceShareShared;
                    RequirementDescriptor->Flags = 0;
                    RequirementDescriptor->u.BusNumber.MinBusNumber = (ULONG)addr64->Address.Minimum;
                    RequirementDescriptor->u.BusNumber.MaxBusNumber = (ULONG)addr64->Address.Maximum;
                    RequirementDescriptor->u.BusNumber.Length = addr64->Address.AddressLength;
                }
                else if (addr64->ResourceType == ACPI_IO_RANGE)
                {
                    ULONGLONG Minimum = addr64->Address.Minimum + addr64->Address.TranslationOffset;
                    ULONGLONG Maximum = addr64->Address.Maximum + addr64->Address.TranslationOffset;
                    ULONG Alignment = (ULONG)addr64->Address.Granularity + 1;

                    if (Alignment == 0)
                        Alignment = 1;

                    RequirementDescriptor->Type = CmResourceTypePort;
                    RequirementDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags = CM_RESOURCE_PORT_IO |
                        (addr64->Decode == ACPI_POS_DECODE ? CM_RESOURCE_PORT_POSITIVE_DECODE : 0);
                    if (IsPciRoot)
                        RequirementDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                    RequirementDescriptor->u.Port.Alignment = Alignment;
                    RequirementDescriptor->u.Port.Length = addr64->Address.AddressLength;
                    RequirementDescriptor->u.Port.MinimumAddress.QuadPart = Minimum;
                    RequirementDescriptor->u.Port.MaximumAddress.QuadPart = Maximum + addr64->Address.AddressLength - 1;
                }
                else
                {
                    ULONGLONG Minimum = addr64->Address.Minimum + addr64->Address.TranslationOffset;
                    ULONGLONG Maximum = addr64->Address.Maximum + addr64->Address.TranslationOffset;
                    ULONG Alignment = (ULONG)addr64->Address.Granularity + 1;

                    if (Alignment == 0)
                        Alignment = 1;

                    RequirementDescriptor->Type = CmResourceTypeMemory;
                    RequirementDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags = (addr64->Info.Mem.WriteProtect == ACPI_READ_ONLY_MEMORY)
                        ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;

                    switch (addr64->Info.Mem.Caching)
                    {
                        case ACPI_CACHABLE_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_CACHEABLE; break;
                        case ACPI_WRITE_COMBINING_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_COMBINEDWRITE; break;
                        case ACPI_PREFETCHABLE_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_PREFETCHABLE; break;
                    }

                    if (IsPciRoot)
                        RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;

                    RequirementDescriptor->u.Memory.Alignment = Alignment;
                    RequirementDescriptor->u.Memory.Length = addr64->Address.AddressLength;
                    RequirementDescriptor->u.Memory.MinimumAddress.QuadPart = Minimum;
                    RequirementDescriptor->u.Memory.MaximumAddress.QuadPart = Maximum + addr64->Address.AddressLength - 1;
                }

                RequirementDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
            {
                ACPI_RESOURCE_EXTENDED_ADDRESS64 *addrx = &resource->Data.ExtAddress64;

                if (!BuspUseAddressResource(IsPciRoot, addrx->ProducerConsumer, addrx->Address.AddressLength))
                    break;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;

                if (addrx->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    RequirementDescriptor->Type = CmResourceTypeBusNumber;
                    RequirementDescriptor->ShareDisposition = CmResourceShareShared;
                    RequirementDescriptor->Flags = 0;
                    RequirementDescriptor->u.BusNumber.MinBusNumber = (ULONG)addrx->Address.Minimum;
                    RequirementDescriptor->u.BusNumber.MaxBusNumber = (ULONG)addrx->Address.Maximum;
                    RequirementDescriptor->u.BusNumber.Length = addrx->Address.AddressLength;
                }
                else if (addrx->ResourceType == ACPI_IO_RANGE)
                {
                    ULONGLONG Minimum = addrx->Address.Minimum + addrx->Address.TranslationOffset;
                    ULONGLONG Maximum = addrx->Address.Maximum + addrx->Address.TranslationOffset;
                    ULONG Alignment = (ULONG)addrx->Address.Granularity + 1;

                    if (Alignment == 0)
                        Alignment = 1;

                    RequirementDescriptor->Type = CmResourceTypePort;
                    RequirementDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags = CM_RESOURCE_PORT_IO |
                        (addrx->Decode == ACPI_POS_DECODE ? CM_RESOURCE_PORT_POSITIVE_DECODE : 0);
                    if (IsPciRoot)
                        RequirementDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                    RequirementDescriptor->u.Port.Alignment = Alignment;
                    RequirementDescriptor->u.Port.Length = addrx->Address.AddressLength;
                    RequirementDescriptor->u.Port.MinimumAddress.QuadPart = Minimum;
                    RequirementDescriptor->u.Port.MaximumAddress.QuadPart = Maximum + addrx->Address.AddressLength - 1;
                }
                else
                {
                    ULONGLONG Minimum = addrx->Address.Minimum + addrx->Address.TranslationOffset;
                    ULONGLONG Maximum = addrx->Address.Maximum + addrx->Address.TranslationOffset;
                    ULONG Alignment = (ULONG)addrx->Address.Granularity + 1;

                    if (Alignment == 0)
                        Alignment = 1;

                    RequirementDescriptor->Type = CmResourceTypeMemory;
                    RequirementDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    RequirementDescriptor->Flags = (addrx->Info.Mem.WriteProtect == ACPI_READ_ONLY_MEMORY)
                        ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;

                    switch (addrx->Info.Mem.Caching)
                    {
                        case ACPI_CACHABLE_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_CACHEABLE; break;
                        case ACPI_WRITE_COMBINING_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_COMBINEDWRITE; break;
                        case ACPI_PREFETCHABLE_MEMORY: RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_PREFETCHABLE; break;
                    }

                    if (IsPciRoot)
                        RequirementDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;

                    RequirementDescriptor->u.Memory.Alignment = Alignment;
                    RequirementDescriptor->u.Memory.Length = addrx->Address.AddressLength;
                    RequirementDescriptor->u.Memory.MinimumAddress.QuadPart = Minimum;
                    RequirementDescriptor->u.Memory.MaximumAddress.QuadPart = Maximum + addrx->Address.AddressLength - 1;
                }

                RequirementDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_MEMORY24:
            {
                ACPI_RESOURCE_MEMORY24 *mem24 = &resource->Data.Memory24;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;
                RequirementDescriptor->Type = CmResourceTypeMemory;
                RequirementDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                RequirementDescriptor->Flags = CM_RESOURCE_MEMORY_24 |
                    ((mem24->WriteProtect == ACPI_READ_ONLY_MEMORY) ?
                     CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE);
                RequirementDescriptor->u.Memory.Alignment = 1;
                RequirementDescriptor->u.Memory.Length = mem24->AddressLength;
                RequirementDescriptor->u.Memory.MinimumAddress.QuadPart = mem24->Minimum;
                RequirementDescriptor->u.Memory.MaximumAddress.QuadPart = mem24->Minimum + mem24->AddressLength - 1;
                RequirementDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_MEMORY32:
            {
                ACPI_RESOURCE_MEMORY32 *mem32 = &resource->Data.Memory32;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;
                RequirementDescriptor->Type = CmResourceTypeMemory;
                RequirementDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                RequirementDescriptor->Flags = (mem32->WriteProtect == ACPI_READ_ONLY_MEMORY)
                    ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;
                RequirementDescriptor->u.Memory.Alignment = 1;
                RequirementDescriptor->u.Memory.Length = mem32->AddressLength;
                RequirementDescriptor->u.Memory.MinimumAddress.QuadPart = mem32->Minimum;
                RequirementDescriptor->u.Memory.MaximumAddress.QuadPart = mem32->Minimum + mem32->AddressLength - 1;
                RequirementDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
            {
                ACPI_RESOURCE_FIXED_MEMORY32 *mfix = &resource->Data.FixedMemory32;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;
                RequirementDescriptor->Type = CmResourceTypeMemory;
                RequirementDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                RequirementDescriptor->Flags = (mfix->WriteProtect == ACPI_READ_ONLY_MEMORY)
                    ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;
                RequirementDescriptor->u.Memory.Alignment = 1;
                RequirementDescriptor->u.Memory.Length = mfix->AddressLength;
                RequirementDescriptor->u.Memory.MinimumAddress.QuadPart = mfix->Address;
                RequirementDescriptor->u.Memory.MaximumAddress.QuadPart = mfix->Address + mfix->AddressLength - 1;
                RequirementDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_GENERIC_REGISTER:
            {
                ACPI_RESOURCE_GENERIC_REGISTER *gen = &resource->Data.GenericReg;
                ULONGLONG Start = gen->Address;
                ULONGLONG End;
                ULONG Length;

                Length = (gen->BitWidth + 7) / 8;
                if (Length == 0)
                    Length = 1;
                End = Start + Length - 1;

                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;

                switch (gen->SpaceId)
                {
                    case ACPI_ADR_SPACE_SYSTEM_MEMORY:
                        RequirementDescriptor->Type = CmResourceTypeMemory;
                        RequirementDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                        RequirementDescriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
                        RequirementDescriptor->u.Memory.Alignment = Length;
                        RequirementDescriptor->u.Memory.Length = Length;
                        RequirementDescriptor->u.Memory.MinimumAddress.QuadPart = Start;
                        RequirementDescriptor->u.Memory.MaximumAddress.QuadPart = End;
                        RequirementDescriptor++;
                        break;

                    case ACPI_ADR_SPACE_SYSTEM_IO:
                        RequirementDescriptor->Type = CmResourceTypePort;
                        RequirementDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                        RequirementDescriptor->Flags = CM_RESOURCE_PORT_IO;
                        RequirementDescriptor->u.Port.Alignment = Length;
                        RequirementDescriptor->u.Port.Length = Length;
                        RequirementDescriptor->u.Port.MinimumAddress.QuadPart = Start;
                        RequirementDescriptor->u.Port.MaximumAddress.QuadPart = End;
                        RequirementDescriptor++;
                        break;

                    default:
                        DPRINT1("ACPI: Unsupported generic register space 0x%x\n", gen->SpaceId);
                        break;
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_GPIO:
            case ACPI_RESOURCE_TYPE_SERIAL_BUS:
            {
                UCHAR ConnectionClass;
                UCHAR ConnectionType;
                UCHAR ShareDisposition;
                ULONGLONG ConnectionId;

                if (!BuspGetConnectionResourceInfo(resource, &ConnectionClass, &ConnectionType, &ShareDisposition))
                    break;

                ConnectionId = BuspGetConnectionId(DeviceData, CurrentResourceIndex, ConnectionClass, ConnectionType);
                RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;
                RequirementDescriptor->Type = CmResourceTypeConnection;
                RequirementDescriptor->ShareDisposition = ShareDisposition;
                RequirementDescriptor->Flags = 0;
                RequirementDescriptor->u.Connection.Class = ConnectionClass;
                RequirementDescriptor->u.Connection.Type = ConnectionType;
                RequirementDescriptor->u.Connection.IdLowPart = (ULONG)ConnectionId;
                RequirementDescriptor->u.Connection.IdHighPart = (ULONG)(ConnectionId >> 32);
                BuspRegisterConnectionResource(resource, ConnectionId, ConnectionClass, ConnectionType);
                RequirementDescriptor++;
                break;
            }

            default:
                break;
        }

        resource = ACPI_NEXT_RESOURCE(resource);
    }

    if (AppendSyntheticBus)
    {
        RequirementDescriptor->Option = CurrentRes ? 0 : IO_RESOURCE_PREFERRED;
        RequirementDescriptor->Type = CmResourceTypeBusNumber;
        RequirementDescriptor->ShareDisposition = CmResourceShareShared;
        RequirementDescriptor->Flags = 0;
        RequirementDescriptor->u.BusNumber.MinBusNumber = DeviceData->HasPciRootBusRange ?
                                                         DeviceData->PciRootMinBus :
                                                         RootBusNumber;
        RequirementDescriptor->u.BusNumber.MaxBusNumber = DeviceData->HasPciRootBusRange ?
                                                         DeviceData->PciRootMaxBus :
                                                         RootBusNumber;
        RequirementDescriptor->u.BusNumber.Length = DeviceData->HasPciRootBusRange ?
                                                  (DeviceData->PciRootMaxBus - DeviceData->PciRootMinBus + 1) :
                                                  1;
        RequirementDescriptor++;
        if (!DeviceData->HasPciRootBusRange)
            BuspRecordPciRootBusRange(DeviceData, RootBusNumber, RootBusNumber);
    }

    *RequirementsListOut = RequirementsList;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BuspCreateResourceListFromAcpiResources(
    _In_ PPDO_DEVICE_DATA DeviceData,
    _In_ ACPI_RESOURCE *FirstResource,
    _In_ BOOLEAN IsPciRoot,
    _In_ ULONG RootBusNumber,
    _Outptr_result_maybenull_ PCM_RESOURCE_LIST *ResourceListOut)
{
    ACPI_RESOURCE *resource;
    ULONG NumberOfResources = 0;
    BOOLEAN AppendSyntheticBus = FALSE;
    ULONG ResourceListSize;
    PCM_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR ResourceDescriptor;
    ULONG i;
    ULONG ResourceIndex = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (!ResourceListOut || !FirstResource)
        return STATUS_INVALID_PARAMETER;

    *ResourceListOut = NULL;

    Status = BuspCountRequirementsFromAcpiResources(DeviceData,
                                                    FirstResource,
                                                    TRUE,
                                                    IsPciRoot,
                                                    RootBusNumber,
                                                    &NumberOfResources,
                                                    &AppendSyntheticBus);
    if (!NT_SUCCESS(Status))
        return Status;

    if (NumberOfResources == 0)
        return STATUS_SUCCESS;

    ResourceListSize = sizeof(CM_RESOURCE_LIST) +
                       sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR) * (NumberOfResources - 1);

    ResourceList = ExAllocatePoolWithTag(PagedPool, ResourceListSize, 'RpcA');
    if (!ResourceList)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ResourceList, ResourceListSize);
    ResourceList->Count = 1;
    if (IsPciRoot && DeviceData->HasPciRootBusRange)
    {
        ResourceList->List[0].InterfaceType = PCIBus;
        ResourceList->List[0].BusNumber = DeviceData->PciRootMinBus;
    }
    else
    {
        ResourceList->List[0].InterfaceType = IsPciRoot ? PCIBus : Internal;
        if (IsPciRoot)
            ResourceList->List[0].BusNumber = RootBusNumber;
        else
            ResourceList->List[0].BusNumber = DeviceData->HasCachedBusNumber ?
                                              DeviceData->CachedBusNumber : 0;
    }
    ResourceList->List[0].PartialResourceList.Version = 1;
    ResourceList->List[0].PartialResourceList.Revision = 1;
    ResourceList->List[0].PartialResourceList.Count = NumberOfResources;
    ResourceDescriptor = ResourceList->List[0].PartialResourceList.PartialDescriptors;

    resource = FirstResource;
    while (resource && resource->Type != ACPI_RESOURCE_TYPE_END_TAG)
    {
        ULONG CurrentResourceIndex = ResourceIndex++;

        switch (resource->Type)
        {
            case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
            {
                ACPI_RESOURCE_EXTENDED_IRQ *irq = &resource->Data.ExtendedIrq;
                if (irq->ProducerConsumer == ACPI_PRODUCER)
                    break;

                for (i = 0; i < irq->InterruptCount; i++)
                {
                    ULONG Interrupt = irq->Interrupts[i];

                    ResourceDescriptor->Type = CmResourceTypeInterrupt;
                    ResourceDescriptor->ShareDisposition =
                        (BuspShouldForceSharedInterrupt(DeviceData, Interrupt) ||
                         irq->Shareable == ACPI_SHARED) ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags =
                        (irq->Triggering == ACPI_LEVEL_SENSITIVE ?
                         CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE : CM_RESOURCE_INTERRUPT_LATCHED);
                    ResourceDescriptor->u.Interrupt.Level =
                    ResourceDescriptor->u.Interrupt.Vector = Interrupt;
                    ResourceDescriptor->u.Interrupt.Affinity = (KAFFINITY)(-1);
                    ResourceDescriptor++;
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_IRQ:
            {
                ACPI_RESOURCE_IRQ *irq = &resource->Data.Irq;

                for (i = 0; i < irq->InterruptCount; i++)
                {
                    ULONG Interrupt = irq->Interrupts[i];

                    ResourceDescriptor->Type = CmResourceTypeInterrupt;
                    ResourceDescriptor->ShareDisposition =
                        (BuspShouldForceSharedInterrupt(DeviceData, Interrupt) ||
                         irq->Shareable == ACPI_SHARED) ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags =
                        (irq->Triggering == ACPI_LEVEL_SENSITIVE ?
                         CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE : CM_RESOURCE_INTERRUPT_LATCHED);
                    ResourceDescriptor->u.Interrupt.Level =
                    ResourceDescriptor->u.Interrupt.Vector = Interrupt;
                    ResourceDescriptor->u.Interrupt.Affinity = (KAFFINITY)(-1);
                    ResourceDescriptor++;
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_DMA:
            {
                ACPI_RESOURCE_DMA *dma = &resource->Data.Dma;

                for (i = 0; i < dma->ChannelCount; i++)
                {
                    ResourceDescriptor->Type = CmResourceTypeDma;
                    ResourceDescriptor->Flags = 0;

                    switch (dma->Type)
                    {
                        case ACPI_TYPE_A: ResourceDescriptor->Flags |= CM_RESOURCE_DMA_TYPE_A; break;
                        case ACPI_TYPE_B: ResourceDescriptor->Flags |= CM_RESOURCE_DMA_TYPE_B; break;
                        case ACPI_TYPE_F: ResourceDescriptor->Flags |= CM_RESOURCE_DMA_TYPE_F; break;
                    }

                    if (dma->BusMaster == ACPI_BUS_MASTER)
                        ResourceDescriptor->Flags |= CM_RESOURCE_DMA_BUS_MASTER;

                    switch (dma->Transfer)
                    {
                        case ACPI_TRANSFER_8: ResourceDescriptor->Flags |= CM_RESOURCE_DMA_8; break;
                        case ACPI_TRANSFER_16: ResourceDescriptor->Flags |= CM_RESOURCE_DMA_16; break;
                        case ACPI_TRANSFER_8_16: ResourceDescriptor->Flags |= CM_RESOURCE_DMA_8_AND_16; break;
                    }

                    ResourceDescriptor->u.Dma.Channel = dma->Channels[i];
                    ResourceDescriptor++;
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_IO:
            {
                ACPI_RESOURCE_IO *io = &resource->Data.Io;

                ResourceDescriptor->Type = CmResourceTypePort;
                ResourceDescriptor->ShareDisposition = CmResourceShareDriverExclusive;
                ResourceDescriptor->Flags = CM_RESOURCE_PORT_IO |
                    (io->IoDecode == ACPI_DECODE_16 ?
                     CM_RESOURCE_PORT_16_BIT_DECODE : CM_RESOURCE_PORT_10_BIT_DECODE);
                ResourceDescriptor->u.Port.Start.QuadPart = io->Minimum;
                ResourceDescriptor->u.Port.Length = io->AddressLength;
                if (IsPciRoot)
                {
                    ResourceDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                    ULONGLONG s = io->Minimum;
                    ULONGLONG e = (io->AddressLength) ? (s + io->AddressLength - 1) : s;
                    BuspCachePciRootIoWindow(DeviceData, s, e);
                }
                ResourceDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_FIXED_IO:
            {
                ACPI_RESOURCE_FIXED_IO *io = &resource->Data.FixedIo;

                ResourceDescriptor->Type = CmResourceTypePort;
                ResourceDescriptor->ShareDisposition = CmResourceShareDriverExclusive;
                ResourceDescriptor->Flags = CM_RESOURCE_PORT_IO;
                ResourceDescriptor->u.Port.Start.QuadPart = io->Address;
                ResourceDescriptor->u.Port.Length = io->AddressLength;
                if (IsPciRoot)
                {
                    ResourceDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                    ULONGLONG s = io->Address;
                    ULONGLONG e = (io->AddressLength) ? (s + io->AddressLength - 1) : s;
                    BuspCachePciRootIoWindow(DeviceData, s, e);
                }
                ResourceDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS16:
            {
                ACPI_RESOURCE_ADDRESS16 *addr16 = &resource->Data.Address16;
                if (!BuspUseAddressResource(IsPciRoot, addr16->ProducerConsumer, addr16->Address.AddressLength))
                    break;

                if (addr16->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    ResourceDescriptor->Type = CmResourceTypeBusNumber;
                    ResourceDescriptor->ShareDisposition = CmResourceShareShared;
                    ResourceDescriptor->Flags = 0;
                    ResourceDescriptor->u.BusNumber.Start = addr16->Address.Minimum;
                    ResourceDescriptor->u.BusNumber.Length = addr16->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ULONG MinBus = (ULONG)addr16->Address.Minimum;
                        ULONGLONG MaxBus = addr16->Address.Maximum;
                        if (addr16->Address.AddressLength > 0)
                            MaxBus = addr16->Address.Minimum + addr16->Address.AddressLength - 1;
                        BuspRecordPciRootBusRange(DeviceData,
                                                  MinBus,
                                                  (ULONG)MaxBus);
                    }
                }
                else if (addr16->ResourceType == ACPI_IO_RANGE)
                {
                    ResourceDescriptor->Type = CmResourceTypePort;
                    ResourceDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags = CM_RESOURCE_PORT_IO |
                        (addr16->Decode == ACPI_POS_DECODE ? CM_RESOURCE_PORT_POSITIVE_DECODE : 0);
                    ResourceDescriptor->u.Port.Start.QuadPart =
                        addr16->Address.Minimum + addr16->Address.TranslationOffset;
                    ResourceDescriptor->u.Port.Length = addr16->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ResourceDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                        ULONGLONG s = addr16->Address.Minimum + addr16->Address.TranslationOffset;
                        ULONGLONG e = (addr16->Address.AddressLength) ? (s + addr16->Address.AddressLength - 1) : s;
                        BuspCachePciRootIoWindow(DeviceData, s, e);
                    }
                }
                else
                {
                    ResourceDescriptor->Type = CmResourceTypeMemory;
                    ResourceDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags = (addr16->Info.Mem.WriteProtect == ACPI_READ_ONLY_MEMORY)
                        ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;
                    switch (addr16->Info.Mem.Caching)
                    {
                        case ACPI_CACHABLE_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_CACHEABLE; break;
                        case ACPI_WRITE_COMBINING_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_COMBINEDWRITE; break;
                        case ACPI_PREFETCHABLE_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_PREFETCHABLE; break;
                    }
                    ResourceDescriptor->u.Memory.Start.QuadPart =
                        addr16->Address.Minimum + addr16->Address.TranslationOffset;
                    ResourceDescriptor->u.Memory.Length = addr16->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;
                        ULONGLONG s = addr16->Address.Minimum + addr16->Address.TranslationOffset;
                        ULONGLONG e = (addr16->Address.AddressLength) ? (s + addr16->Address.AddressLength - 1) : s;
                        BOOLEAN prefetch = (ResourceDescriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) ? TRUE : FALSE;
                        BuspCachePciRootMemWindow(DeviceData, s, e, prefetch);
                    }
                }
                ResourceDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS32:
            {
                ACPI_RESOURCE_ADDRESS32 *addr32 = &resource->Data.Address32;
                if (!BuspUseAddressResource(IsPciRoot, addr32->ProducerConsumer, addr32->Address.AddressLength))
                    break;

                if (addr32->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    ResourceDescriptor->Type = CmResourceTypeBusNumber;
                    ResourceDescriptor->ShareDisposition = CmResourceShareShared;
                    ResourceDescriptor->Flags = 0;
                    ResourceDescriptor->u.BusNumber.Start = addr32->Address.Minimum;
                    ResourceDescriptor->u.BusNumber.Length = addr32->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ULONG MinBus = (ULONG)addr32->Address.Minimum;
                        ULONGLONG MaxBus = addr32->Address.Maximum;
                        if (addr32->Address.AddressLength > 0)
                            MaxBus = addr32->Address.Minimum + addr32->Address.AddressLength - 1;
                        BuspRecordPciRootBusRange(DeviceData,
                                                  MinBus,
                                                  (ULONG)MaxBus);
                    }
                }
                else if (addr32->ResourceType == ACPI_IO_RANGE)
                {
                    ResourceDescriptor->Type = CmResourceTypePort;
                    ResourceDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags = CM_RESOURCE_PORT_IO |
                        (addr32->Decode == ACPI_POS_DECODE ? CM_RESOURCE_PORT_POSITIVE_DECODE : 0);
                    ResourceDescriptor->u.Port.Start.QuadPart =
                        addr32->Address.Minimum + addr32->Address.TranslationOffset;
                    ResourceDescriptor->u.Port.Length = addr32->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ResourceDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                        ULONGLONG s = addr32->Address.Minimum + addr32->Address.TranslationOffset;
                        ULONGLONG e = (addr32->Address.AddressLength) ? (s + addr32->Address.AddressLength - 1) : s;
                        BuspCachePciRootIoWindow(DeviceData, s, e);
                    }
                }
                else
                {
                    ResourceDescriptor->Type = CmResourceTypeMemory;
                    ResourceDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags = (addr32->Info.Mem.WriteProtect == ACPI_READ_ONLY_MEMORY)
                        ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;
                    switch (addr32->Info.Mem.Caching)
                    {
                        case ACPI_CACHABLE_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_CACHEABLE; break;
                        case ACPI_WRITE_COMBINING_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_COMBINEDWRITE; break;
                        case ACPI_PREFETCHABLE_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_PREFETCHABLE; break;
                    }
                    ResourceDescriptor->u.Memory.Start.QuadPart =
                        addr32->Address.Minimum + addr32->Address.TranslationOffset;
                    ResourceDescriptor->u.Memory.Length = addr32->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;
                        ULONGLONG s = addr32->Address.Minimum + addr32->Address.TranslationOffset;
                        ULONGLONG e = (addr32->Address.AddressLength) ? (s + addr32->Address.AddressLength - 1) : s;
                        BOOLEAN prefetch = (ResourceDescriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) ? TRUE : FALSE;
                        BuspCachePciRootMemWindow(DeviceData, s, e, prefetch);
                    }
                }
                ResourceDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS64:
            {
                ACPI_RESOURCE_ADDRESS64 *addr64 = &resource->Data.Address64;
                if (!BuspUseAddressResource(IsPciRoot, addr64->ProducerConsumer, addr64->Address.AddressLength))
                    break;

                if (addr64->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    ResourceDescriptor->Type = CmResourceTypeBusNumber;
                    ResourceDescriptor->ShareDisposition = CmResourceShareShared;
                    ResourceDescriptor->Flags = 0;
                    ResourceDescriptor->u.BusNumber.Start = (ULONG)addr64->Address.Minimum;
                    ResourceDescriptor->u.BusNumber.Length = addr64->Address.AddressLength;
                }
                else if (addr64->ResourceType == ACPI_IO_RANGE)
                {
                    ResourceDescriptor->Type = CmResourceTypePort;
                    ResourceDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags = CM_RESOURCE_PORT_IO |
                        (addr64->Decode == ACPI_POS_DECODE ? CM_RESOURCE_PORT_POSITIVE_DECODE : 0);
                    ResourceDescriptor->u.Port.Start.QuadPart =
                        addr64->Address.Minimum + addr64->Address.TranslationOffset;
                    ResourceDescriptor->u.Port.Length = addr64->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ResourceDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                        ULONGLONG s = addr64->Address.Minimum + addr64->Address.TranslationOffset;
                        ULONGLONG e = (addr64->Address.AddressLength) ? (s + addr64->Address.AddressLength - 1) : s;
                        BuspCachePciRootIoWindow(DeviceData, s, e);
                    }
                }
                else
                {
                    ResourceDescriptor->Type = CmResourceTypeMemory;
                    ResourceDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags = (addr64->Info.Mem.WriteProtect == ACPI_READ_ONLY_MEMORY)
                        ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;
                    switch (addr64->Info.Mem.Caching)
                    {
                        case ACPI_CACHABLE_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_CACHEABLE; break;
                        case ACPI_WRITE_COMBINING_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_COMBINEDWRITE; break;
                        case ACPI_PREFETCHABLE_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_PREFETCHABLE; break;
                    }
                    ResourceDescriptor->u.Memory.Start.QuadPart =
                        addr64->Address.Minimum + addr64->Address.TranslationOffset;
                    ResourceDescriptor->u.Memory.Length = addr64->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;
                        ULONGLONG s = addr64->Address.Minimum + addr64->Address.TranslationOffset;
                        ULONGLONG e = (addr64->Address.AddressLength) ? (s + addr64->Address.AddressLength - 1) : s;
                        BOOLEAN prefetch = (ResourceDescriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) ? TRUE : FALSE;
                        BuspCachePciRootMemWindow(DeviceData, s, e, prefetch);
                    }
                }
                ResourceDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
            {
                ACPI_RESOURCE_EXTENDED_ADDRESS64 *addrx = &resource->Data.ExtAddress64;
                if (!BuspUseAddressResource(IsPciRoot, addrx->ProducerConsumer, addrx->Address.AddressLength))
                    break;

                if (addrx->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    ResourceDescriptor->Type = CmResourceTypeBusNumber;
                    ResourceDescriptor->ShareDisposition = CmResourceShareShared;
                    ResourceDescriptor->Flags = 0;
                    ResourceDescriptor->u.BusNumber.Start = (ULONG)addrx->Address.Minimum;
                    ResourceDescriptor->u.BusNumber.Length = addrx->Address.AddressLength;
                }
                else if (addrx->ResourceType == ACPI_IO_RANGE)
                {
                    ResourceDescriptor->Type = CmResourceTypePort;
                    ResourceDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags = CM_RESOURCE_PORT_IO |
                        (addrx->Decode == ACPI_POS_DECODE ? CM_RESOURCE_PORT_POSITIVE_DECODE : 0);
                    ResourceDescriptor->u.Port.Start.QuadPart =
                        addrx->Address.Minimum + addrx->Address.TranslationOffset;
                    ResourceDescriptor->u.Port.Length = addrx->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ResourceDescriptor->Flags |= CM_RESOURCE_PORT_WINDOW_DECODE;
                        ULONGLONG s = addrx->Address.Minimum + addrx->Address.TranslationOffset;
                        ULONGLONG e = (addrx->Address.AddressLength) ? (s + addrx->Address.AddressLength - 1) : s;
                        BuspCachePciRootIoWindow(DeviceData, s, e);
                    }
                }
                else
                {
                    ResourceDescriptor->Type = CmResourceTypeMemory;
                    ResourceDescriptor->ShareDisposition = IsPciRoot ?
                        CmResourceShareShared : CmResourceShareDeviceExclusive;
                    ResourceDescriptor->Flags = (addrx->Info.Mem.WriteProtect == ACPI_READ_ONLY_MEMORY)
                        ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;
                    switch (addrx->Info.Mem.Caching)
                    {
                        case ACPI_CACHABLE_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_CACHEABLE; break;
                        case ACPI_WRITE_COMBINING_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_COMBINEDWRITE; break;
                        case ACPI_PREFETCHABLE_MEMORY: ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_PREFETCHABLE; break;
                    }
                    ResourceDescriptor->u.Memory.Start.QuadPart =
                        addrx->Address.Minimum + addrx->Address.TranslationOffset;
                    ResourceDescriptor->u.Memory.Length = addrx->Address.AddressLength;
                    if (IsPciRoot)
                    {
                        ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;
                        ULONGLONG s = addrx->Address.Minimum + addrx->Address.TranslationOffset;
                        ULONGLONG e = (addrx->Address.AddressLength) ? (s + addrx->Address.AddressLength - 1) : s;
                        BOOLEAN prefetch = (ResourceDescriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) ? TRUE : FALSE;
                        BuspCachePciRootMemWindow(DeviceData, s, e, prefetch);
                    }
                }
                ResourceDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_MEMORY24:
            {
                ACPI_RESOURCE_MEMORY24 *mem24 = &resource->Data.Memory24;
                ResourceDescriptor->Type = CmResourceTypeMemory;
                ResourceDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                ResourceDescriptor->Flags = CM_RESOURCE_MEMORY_24 |
                    ((mem24->WriteProtect == ACPI_READ_ONLY_MEMORY) ?
                     CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE);
                ResourceDescriptor->u.Memory.Start.QuadPart = mem24->Minimum;
                ResourceDescriptor->u.Memory.Length = mem24->AddressLength;
                if (IsPciRoot)
                {
                    ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;
                    ULONGLONG s = mem24->Minimum;
                    ULONGLONG e = (mem24->AddressLength) ? (s + mem24->AddressLength - 1) : s;
                    BOOLEAN prefetch = (ResourceDescriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) ? TRUE : FALSE;
                    BuspCachePciRootMemWindow(DeviceData, s, e, prefetch);
                }
                ResourceDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_MEMORY32:
            {
                ACPI_RESOURCE_MEMORY32 *mem32 = &resource->Data.Memory32;
                ResourceDescriptor->Type = CmResourceTypeMemory;
                ResourceDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                ResourceDescriptor->Flags = (mem32->WriteProtect == ACPI_READ_ONLY_MEMORY)
                    ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;
                ResourceDescriptor->u.Memory.Start.QuadPart = mem32->Minimum;
                ResourceDescriptor->u.Memory.Length = mem32->AddressLength;
                if (IsPciRoot)
                {
                    ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;
                    ULONGLONG s = mem32->Minimum;
                    ULONGLONG e = (mem32->AddressLength) ? (s + mem32->AddressLength - 1) : s;
                    BOOLEAN prefetch = (ResourceDescriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) ? TRUE : FALSE;
                    BuspCachePciRootMemWindow(DeviceData, s, e, prefetch);
                }
                ResourceDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
            {
                ACPI_RESOURCE_FIXED_MEMORY32 *mfix = &resource->Data.FixedMemory32;
                ResourceDescriptor->Type = CmResourceTypeMemory;
                ResourceDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                ResourceDescriptor->Flags = (mfix->WriteProtect == ACPI_READ_ONLY_MEMORY)
                    ? CM_RESOURCE_MEMORY_READ_ONLY : CM_RESOURCE_MEMORY_READ_WRITE;
                ResourceDescriptor->u.Memory.Start.QuadPart = mfix->Address;
                ResourceDescriptor->u.Memory.Length = mfix->AddressLength;
                if (IsPciRoot)
                {
                    ResourceDescriptor->Flags |= CM_RESOURCE_MEMORY_WINDOW_DECODE;
                    ULONGLONG s = mfix->Address;
                    ULONGLONG e = (mfix->AddressLength) ? (s + mfix->AddressLength - 1) : s;
                    BOOLEAN prefetch = (ResourceDescriptor->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) ? TRUE : FALSE;
                    BuspCachePciRootMemWindow(DeviceData, s, e, prefetch);
                }
                ResourceDescriptor++;
                break;
            }

            case ACPI_RESOURCE_TYPE_GENERIC_REGISTER:
            {
                ACPI_RESOURCE_GENERIC_REGISTER *gen = &resource->Data.GenericReg;
                ULONGLONG Start = gen->Address;
                ULONG Length = (gen->BitWidth + 7) / 8;

                if (Length == 0)
                    Length = 1;

                switch (gen->SpaceId)
                {
                    case ACPI_ADR_SPACE_SYSTEM_MEMORY:
                        ResourceDescriptor->Type = CmResourceTypeMemory;
                        ResourceDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                        ResourceDescriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
                        ResourceDescriptor->u.Memory.Start.QuadPart = Start;
                        ResourceDescriptor->u.Memory.Length = Length;
                        ResourceDescriptor++;
                        break;

                    case ACPI_ADR_SPACE_SYSTEM_IO:
                        ResourceDescriptor->Type = CmResourceTypePort;
                        ResourceDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                        ResourceDescriptor->Flags = CM_RESOURCE_PORT_IO;
                        ResourceDescriptor->u.Port.Start.QuadPart = Start;
                        ResourceDescriptor->u.Port.Length = Length;
                        ResourceDescriptor++;
                        break;

                    default:
                        DPRINT1("ACPI: Unsupported generic register space 0x%x\n", gen->SpaceId);
                        break;
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_GPIO:
            case ACPI_RESOURCE_TYPE_SERIAL_BUS:
            {
                UCHAR ConnectionClass;
                UCHAR ConnectionType;
                UCHAR ShareDisposition;
                ULONGLONG ConnectionId;

                if (!BuspGetConnectionResourceInfo(resource, &ConnectionClass, &ConnectionType, &ShareDisposition))
                    break;

                ConnectionId = BuspGetConnectionId(DeviceData, CurrentResourceIndex, ConnectionClass, ConnectionType);
                ResourceDescriptor->Type = CmResourceTypeConnection;
                ResourceDescriptor->ShareDisposition = ShareDisposition;
                ResourceDescriptor->Flags = 0;
                ResourceDescriptor->u.Connection.Class = ConnectionClass;
                ResourceDescriptor->u.Connection.Type = ConnectionType;
                ResourceDescriptor->u.Connection.IdLowPart = (ULONG)ConnectionId;
                ResourceDescriptor->u.Connection.IdHighPart = (ULONG)(ConnectionId >> 32);
                BuspRegisterConnectionResource(resource, ConnectionId, ConnectionClass, ConnectionType);
                ResourceDescriptor++;
                break;
            }

            default:
                break;
        }

        resource = ACPI_NEXT_RESOURCE(resource);
    }

    if (AppendSyntheticBus)
    {
        ResourceDescriptor->Type = CmResourceTypeBusNumber;
        ResourceDescriptor->ShareDisposition = CmResourceShareShared;
        ResourceDescriptor->Flags = 0;
        ResourceDescriptor->u.BusNumber.Start = DeviceData->HasPciRootBusRange ?
                                               DeviceData->PciRootMinBus :
                                               RootBusNumber;
        ResourceDescriptor->u.BusNumber.Length = DeviceData->HasPciRootBusRange ?
                                                (DeviceData->PciRootMaxBus - DeviceData->PciRootMinBus + 1) :
                                                1;
        ResourceDescriptor++;
        if (!DeviceData->HasPciRootBusRange)
            BuspRecordPciRootBusRange(DeviceData, RootBusNumber, RootBusNumber);
    }

    *ResourceListOut = ResourceList;
    return STATUS_SUCCESS;
}

#ifndef UNIT_TEST
static
VOID
BuspPublishLegacyScsiportConfig(
    _In_ PPDO_DEVICE_DATA DeviceData,
    _In_ PCM_RESOURCE_LIST ResourceListTranslated)
{
    const WCHAR ServicePrefix[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
    UNICODE_STRING driverKeyName, serviceValueName, keyName;
    UNICODE_STRING basePathUs, parametersSuffix;
    OBJECT_ATTRIBUTES objectAttributes;
    HANDLE driverKey = NULL, parametersKey = NULL, deviceKey = NULL;
    PWCHAR serviceName = NULL, basePath = NULL, instanceName = NULL;
    KEY_VALUE_PARTIAL_INFORMATION *valueInfo = NULL;
    CM_FULL_RESOURCE_DESCRIPTOR *descriptorCopy = NULL;
    ULONG neededLength = 0, descriptorSize;
    ULONG deviceIndex;
    PCM_FULL_RESOURCE_DESCRIPTOR fullDescriptor;
    DEVPROPTYPE propType = DEVPROP_TYPE_EMPTY;
    NTSTATUS Status;

    driverKeyName.Buffer = NULL;
    driverKeyName.Length = driverKeyName.MaximumLength = 0;
    basePathUs.Buffer = NULL;
    basePathUs.Length = basePathUs.MaximumLength = 0;
    basePath = NULL;
    instanceName = NULL;

    if (!ResourceListTranslated || ResourceListTranslated->Count == 0)
        return;

    /* Prefer the Win7-style property-data path and fall back to the legacy API. */
    Status = IoGetDevicePropertyData(DeviceData->Common.Self,
                                     &DEVPKEY_Device_Driver,
                                     0,
                                     0,
                                     0,
                                     NULL,
                                     &neededLength,
                                     &propType);
    if (Status == STATUS_BUFFER_TOO_SMALL &&
        neededLength >= sizeof(WCHAR) &&
        propType == DEVPROP_TYPE_STRING)
    {
        driverKeyName.Buffer = ExAllocatePoolWithTag(PagedPool, neededLength, 'prCA');
        if (!driverKeyName.Buffer)
            return;

        Status = IoGetDevicePropertyData(DeviceData->Common.Self,
                                         &DEVPKEY_Device_Driver,
                                         0,
                                         0,
                                         neededLength,
                                         driverKeyName.Buffer,
                                         &neededLength,
                                         &propType);
    }
    else
    {
        Status = IoGetDeviceProperty(DeviceData->Common.Self,
                                     DevicePropertyDriverKeyName,
                                     0,
                                     NULL,
                                     &neededLength);
        if (Status != STATUS_BUFFER_TOO_SMALL || neededLength < sizeof(WCHAR))
            return;

        driverKeyName.Buffer = ExAllocatePoolWithTag(PagedPool, neededLength, 'prCA');
        if (!driverKeyName.Buffer)
            return;

        Status = IoGetDeviceProperty(DeviceData->Common.Self,
                                     DevicePropertyDriverKeyName,
                                     neededLength,
                                     driverKeyName.Buffer,
                                     &neededLength);
    }
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    driverKeyName.Length = (USHORT)(neededLength - sizeof(WCHAR));
    driverKeyName.MaximumLength = (USHORT)neededLength;

    InitializeObjectAttributes(&objectAttributes,
                               &driverKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenKey(&driverKey, KEY_READ, &objectAttributes);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    RtlInitUnicodeString(&serviceValueName, L"Service");
    Status = ZwQueryValueKey(driverKey,
                             &serviceValueName,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &neededLength);
    if (Status != STATUS_BUFFER_TOO_SMALL || neededLength < sizeof(KEY_VALUE_PARTIAL_INFORMATION))
        goto Cleanup;

    valueInfo = ExAllocatePoolWithTag(PagedPool, neededLength, 'prCA');
    if (!valueInfo)
        goto Cleanup;

    Status = ZwQueryValueKey(driverKey,
                             &serviceValueName,
                             KeyValuePartialInformation,
                             valueInfo,
                             neededLength,
                             &neededLength);
    if (!NT_SUCCESS(Status) || valueInfo->Type != REG_SZ || valueInfo->DataLength < sizeof(WCHAR))
        goto Cleanup;

    serviceName = ExAllocatePoolWithTag(PagedPool, valueInfo->DataLength, 'prCA');
    if (!serviceName)
        goto Cleanup;
    RtlCopyMemory(serviceName, valueInfo->Data, valueInfo->DataLength);

    /* Build the Parameters path */
    UNICODE_STRING prefixUs;
    prefixUs.Buffer = (PWSTR)ServicePrefix;
    prefixUs.Length = prefixUs.MaximumLength = (USHORT)((RTL_NUMBER_OF(ServicePrefix) - 1) * sizeof(WCHAR));

    UNICODE_STRING serviceUs;
    RtlInitUnicodeString(&serviceUs, serviceName);

    RtlInitUnicodeString(&parametersSuffix, L"\\Parameters");

    basePathUs.MaximumLength = prefixUs.Length + serviceUs.Length + parametersSuffix.Length + sizeof(WCHAR);
    basePathUs.Buffer = ExAllocatePoolWithTag(PagedPool, basePathUs.MaximumLength, 'prCA');
    if (!basePathUs.Buffer)
        goto Cleanup;
    basePathUs.Length = 0;
    basePath = basePathUs.Buffer;

    RtlCopyUnicodeString(&basePathUs, &prefixUs);
    RtlAppendUnicodeStringToString(&basePathUs, &serviceUs);
    RtlAppendUnicodeStringToString(&basePathUs, &parametersSuffix);

    RtlInitUnicodeString(&keyName, basePathUs.Buffer);
    InitializeObjectAttributes(&objectAttributes,
                               &keyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateKey(&parametersKey,
                         KEY_ALL_ACCESS,
                         &objectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    deviceIndex = ResourceListTranslated->List[0].BusNumber;
    if (deviceIndex == (ULONG)-1)
        deviceIndex = 0;

    instanceName = ExAllocatePoolWithTag(PagedPool, 32 * sizeof(WCHAR), 'prCA');
    if (!instanceName)
        goto Cleanup;
    _snwprintf(instanceName, 32, L"Device%lu", deviceIndex);
    instanceName[31] = UNICODE_NULL;
    RtlInitUnicodeString(&keyName, instanceName);

    InitializeObjectAttributes(&objectAttributes,
                               &keyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               parametersKey,
                               NULL);
    Status = ZwCreateKey(&deviceKey,
                         KEY_ALL_ACCESS,
                         &objectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    fullDescriptor = &ResourceListTranslated->List[0];
    descriptorSize = (ULONG)(FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList.PartialDescriptors) +
                             fullDescriptor->PartialResourceList.Count * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));
    descriptorCopy = ExAllocatePoolWithTag(PagedPool, descriptorSize, 'prCA');
    if (!descriptorCopy)
        goto Cleanup;
    RtlCopyMemory(descriptorCopy, fullDescriptor, descriptorSize);

    RtlInitUnicodeString(&keyName, L"ResourceList");
    Status = ZwSetValueKey(deviceKey,
                           &keyName,
                           0,
                           REG_FULL_RESOURCE_DESCRIPTOR,
                           descriptorCopy,
                           descriptorSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    RtlInitUnicodeString(&keyName, L"Configuration Data");
    ZwSetValueKey(deviceKey,
                  &keyName,
                  0,
                  REG_FULL_RESOURCE_DESCRIPTOR,
                  descriptorCopy,
                  descriptorSize);

    RtlInitUnicodeString(&keyName, L"BusNumber");
    ZwSetValueKey(deviceKey,
                  &keyName,
                  0,
                  REG_DWORD,
                  &fullDescriptor->BusNumber,
                  sizeof(ULONG));

Cleanup:
    if (descriptorCopy)
        ExFreePool(descriptorCopy);
    if (deviceKey)
        ZwClose(deviceKey);
    if (parametersKey)
        ZwClose(parametersKey);
    if (instanceName)
        ExFreePool(instanceName);
    if (basePath)
        ExFreePool(basePath);
    if (serviceName)
        ExFreePool(serviceName);
    if (valueInfo)
        ExFreePool(valueInfo);
    if (driverKey)
        ZwClose(driverKey);
    if (driverKeyName.Buffer)
        ExFreePool(driverKeyName.Buffer);
}
#endif /* !UNIT_TEST */

/* Build a stable instance ID string for devices that lack _UID.
 * Strategy: prefer _UID; then PCI root segment (if tracked); then bus_address (_ADR);
 * otherwise device-type fallback.
 * Returns the number of WCHARs written (without terminator). */
static
ULONG
BuspBuildStableInstanceId(
    _In_opt_ struct acpi_device* Device,
    _In_opt_ PPDO_DEVICE_DATA DeviceData,
    _In_ BOOLEAN IsFixedFeatureButton,
    _Out_writes_(MaxChars) PWCHAR Out,
    _In_ ULONG MaxChars)
{
    if (!Out || MaxChars == 0)
        return 0;

    if (Device)
    {
        if (Device->flags.unique_id && Device->pnp.unique_id[0])
        {
            /* Unique ID is ASCII; copy into unicode */
            size_t len = strlen(Device->pnp.unique_id);
            if (len >= MaxChars) len = MaxChars - 1;
            for (size_t i = 0; i < len; ++i)
                Out[i] = (WCHAR)(unsigned char)Device->pnp.unique_id[i];
            Out[len] = UNICODE_NULL;
            return (ULONG)len;
        }
    }

    if (DeviceData &&
        BuspIsPciRootDevice(DeviceData) &&
        DeviceData->HasPciRootSegment)
    {
        int n = _snwprintf(Out, MaxChars, L"%lu", DeviceData->PciRootSegment);
        if (n < 0) n = 0;
        Out[(n < (int)MaxChars) ? n : (int)MaxChars - 1] = UNICODE_NULL;
        return (ULONG)((n > 0) ? n : 0);
    }

    if (Device && Device->flags.bus_address)
    {
        /* Use bus address (commonly _ADR) as decimal instance id */
        int n = _snwprintf(Out, MaxChars, L"%u", (UINT)Device->pnp.bus_address);
        if (n < 0) n = 0;
        Out[(n < (int)MaxChars) ? n : (int)MaxChars - 1] = UNICODE_NULL;
        return (ULONG)((n > 0) ? n : 0);
    }

    if (IsFixedFeatureButton)
    {
        /* There is only one FFB. Use a stable literal. */
        static const WCHAR kFFBId[] = L"FFB0";
        ULONG n = (ULONG)wcslen(kFFBId);
        if (n >= MaxChars) n = MaxChars - 1;
        for (ULONG i = 0; i < n; ++i)
            Out[i] = kFFBId[i];
        Out[n] = UNICODE_NULL;
        return n;
    }

    /* Last resort: "0" */
    if (MaxChars >= 2) { Out[0] = L'0'; Out[1] = UNICODE_NULL; return 1; }
    return 0;
}

/* ============================== PDO PnP ================================ */

#ifndef UNIT_TEST

NTSTATUS
Bus_PDO_PnP (
     PDEVICE_OBJECT       DeviceObject,
     PIRP                 Irp,
     PIO_STACK_LOCATION   IrpStack,
     PPDO_DEVICE_DATA     DeviceData
    )
{
    NTSTATUS                status;
    POWER_STATE             state;
    struct acpi_device      *device = NULL;

    PAGED_CODE();

    if (DeviceData->AcpiHandle)
        acpi_bus_get_device(DeviceData->AcpiHandle, &device);

    switch (IrpStack->MinorFunction) {

    case IRP_MN_START_DEVICE:
        DeviceData->HasCachedBusNumber = FALSE;
        DeviceData->CachedBusNumber = 0;
        DeviceData->HasPciRootBusRange = FALSE;
        DeviceData->PciRootMinBus = 0;
        DeviceData->PciRootMaxBus = 0;
        DeviceData->HasPciRootSegment = FALSE;
        DeviceData->PciRootSegment = 0;

        BuspApplyTrackedPciRootInfo(DeviceData);

        if (BuspIsPciRootDevice(DeviceData))
        {
            (void)BuspEnsurePciRootBusNumber(DeviceData);
        }

        BuspPublishDeviceProperties(DeviceData, device);

        if (BuspIsPciRootDevice(DeviceData))
        {
            BuspPublishLegacyScsiportConfig(DeviceData,
                IrpStack->Parameters.StartDevice.AllocatedResourcesTranslated);
        }

        if (DeviceData->AcpiHandle &&
            acpi_bus_power_manageable(DeviceData->AcpiHandle) &&
            !ACPI_SUCCESS(acpi_bus_set_power(DeviceData->AcpiHandle, ACPI_STATE_D0)))
        {
            DPRINT1("Device %x failed to start!\n", DeviceData->AcpiHandle);
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        DeviceData->InterfaceName.Length = 0;
        RtlInitUnicodeString(&DeviceData->InterfaceName, NULL);
        status = STATUS_SUCCESS;

        if (!device)
        {
            /* Fixed feature button == SYS_BUTTON */
            status = IoRegisterDeviceInterface(DeviceData->Common.Self,
                                               &GUID_DEVICE_SYS_BUTTON,
                                               NULL,
                                               &DeviceData->InterfaceName);
        }
        else if (device->flags.hardware_id &&
                 strstr(device->pnp.hardware_id, ACPI_THERMAL_HID))
        {
            status = IoRegisterDeviceInterface(DeviceData->Common.Self,
                                               &GUID_DEVICE_THERMAL_ZONE,
                                               NULL,
                                               &DeviceData->InterfaceName);
        }
        else if (device->flags.hardware_id &&
                 strstr(device->pnp.hardware_id, ACPI_FAN_HID))
        {
            status = IoRegisterDeviceInterface(DeviceData->Common.Self,
                                               &GUID_DEVICE_FAN,
                                               NULL,
                                               &DeviceData->InterfaceName);
        }
        else if (device->flags.hardware_id &&
                 strstr(device->pnp.hardware_id, ACPI_BUTTON_HID_LID))
        {
            status = IoRegisterDeviceInterface(DeviceData->Common.Self,
                                               &GUID_DEVICE_LID,
                                               NULL,
                                               &DeviceData->InterfaceName);
        }
        else if (device->flags.hardware_id &&
                 strstr(device->pnp.hardware_id, ACPI_PROCESSOR_HID))
        {
            status = IoRegisterDeviceInterface(DeviceData->Common.Self,
                                               &GUID_DEVICE_PROCESSOR,
                                               NULL,
                                               &DeviceData->InterfaceName);
        }

        /* Enabling the interface is best-effort; don't fail start if that fails */
        if (NT_SUCCESS(status) && DeviceData->InterfaceName.Length != 0)
        {
            NTSTATUS InterfaceStatus = IoSetDeviceInterfaceState(&DeviceData->InterfaceName, TRUE);
            if (!NT_SUCCESS(InterfaceStatus))
            {
                DPRINT1("Failed to enable device interface: 0x%lx\n", InterfaceStatus);
            }
        }

        state.DeviceState = PowerDeviceD0;
        PoSetPowerState(DeviceData->Common.Self, DevicePowerState, state);
        DeviceData->Common.DevicePowerState = PowerDeviceD0;
        SET_NEW_PNP_STATE(DeviceData->Common, Started);
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_STOP_DEVICE:

        if (DeviceData->InterfaceName.Length != 0)
        {
            NTSTATUS InterfaceStatus = IoSetDeviceInterfaceState(&DeviceData->InterfaceName, FALSE);
            if (!NT_SUCCESS(InterfaceStatus))
            {
                DPRINT1("Failed to disable device interface: 0x%lx\n", InterfaceStatus);
            }
        }

        if (DeviceData->AcpiHandle &&
            acpi_bus_power_manageable(DeviceData->AcpiHandle) &&
            !ACPI_SUCCESS(acpi_bus_set_power(DeviceData->AcpiHandle, ACPI_STATE_D3)))
        {
            DPRINT1("Device %x failed to stop!\n", DeviceData->AcpiHandle);
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        state.DeviceState = PowerDeviceD3;
        PoSetPowerState(DeviceData->Common.Self, DevicePowerState, state);
        DeviceData->Common.DevicePowerState = PowerDeviceD3;
        SET_NEW_PNP_STATE(DeviceData->Common, Stopped);
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_STOP_DEVICE:
        SET_NEW_PNP_STATE(DeviceData->Common, StopPending);
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_CANCEL_STOP_DEVICE:
        if (StopPending == DeviceData->Common.DevicePnPState)
        {
            RESTORE_PREVIOUS_PNP_STATE(DeviceData->Common);
        }
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_REMOVE_DEVICE:
        if (DeviceData->InterfaceName.Length != 0)
        {
            NTSTATUS InterfaceStatus = IoSetDeviceInterfaceState(&DeviceData->InterfaceName, FALSE);
            if (!NT_SUCCESS(InterfaceStatus))
            {
                DPRINT1("Failed to disable device interface: 0x%lx\n", InterfaceStatus);
            }
            /* NOTE: Free the interface string on remove to avoid leaks. */
            RtlFreeUnicodeString(&DeviceData->InterfaceName);
            DeviceData->InterfaceName.Length = 0;
            DeviceData->InterfaceName.Buffer = NULL;
        }

        if (DeviceData->AcpiHandle &&
            acpi_bus_power_manageable(DeviceData->AcpiHandle) &&
            !ACPI_SUCCESS(acpi_bus_set_power(DeviceData->AcpiHandle, ACPI_STATE_D3)))
        {
            DPRINT1("Device %x failed to enter D3!\n", DeviceData->AcpiHandle);
            state.DeviceState = PowerDeviceD3;
            PoSetPowerState(DeviceData->Common.Self, DevicePowerState, state);
            DeviceData->Common.DevicePowerState = PowerDeviceD3;
        }

        SET_NEW_PNP_STATE(DeviceData->Common, Stopped);
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_REMOVE_DEVICE:
        SET_NEW_PNP_STATE(DeviceData->Common, RemovalPending);
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_CANCEL_REMOVE_DEVICE:
        if (RemovalPending == DeviceData->Common.DevicePnPState)
        {
            RESTORE_PREVIOUS_PNP_STATE(DeviceData->Common);
        }
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_CAPABILITIES:
        status = Bus_PDO_QueryDeviceCaps(DeviceData, Irp);
        break;

    case IRP_MN_QUERY_ID:
        status = Bus_PDO_QueryDeviceId(DeviceData, Irp);
        break;

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        DPRINT("\tQueryDeviceRelation Type: %s\n",
               DbgDeviceRelationString(IrpStack->Parameters.QueryDeviceRelations.Type));
        status = Bus_PDO_QueryDeviceRelations(DeviceData, Irp);
        break;

    case IRP_MN_QUERY_DEVICE_TEXT:
        status = Bus_PDO_QueryDeviceText(DeviceData, Irp);
        break;

    case IRP_MN_QUERY_RESOURCES:
        status = Bus_PDO_QueryResources(DeviceData, Irp);
        break;

    case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        status = Bus_PDO_QueryResourceRequirements(DeviceData, Irp);
        break;

    case IRP_MN_QUERY_BUS_INFORMATION:
        status = Bus_PDO_QueryBusInformation(DeviceData, Irp);
        break;

    case IRP_MN_QUERY_INTERFACE:
        status = Bus_PDO_QueryInterface(DeviceData, Irp);
        break;

    default:
        status = Irp->IoStatus.Status; /* Preserve prior status for unknowns */
        break;
    }

    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS
Bus_PDO_QueryDeviceCaps(
     PPDO_DEVICE_DATA     DeviceData,
      PIRP   Irp )
{
    PIO_STACK_LOCATION      stack;
    PDEVICE_CAPABILITIES    deviceCapabilities;
    struct acpi_device *device = NULL;
    ULONG i;

    PAGED_CODE();

    if (DeviceData->AcpiHandle)
        acpi_bus_get_device(DeviceData->AcpiHandle, &device);

    stack = IoGetCurrentIrpStackLocation(Irp);
    deviceCapabilities = stack->Parameters.DeviceCapabilities.Capabilities;

    if (deviceCapabilities->Version != 1 ||
        deviceCapabilities->Size < sizeof(DEVICE_CAPABILITIES))
    {
       return STATUS_UNSUCCESSFUL;
    }

    deviceCapabilities->D1Latency = 0;
    deviceCapabilities->D2Latency = 0;
    deviceCapabilities->D3Latency = 0;

    deviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    deviceCapabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
    deviceCapabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    deviceCapabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;

    for (i = 0; i < ACPI_D_STATE_COUNT && device; i++)
    {
        if (!device->power.states[i].flags.valid)
            continue;

        switch (i)
        {
           case ACPI_STATE_D0:
              deviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
              break;

           case ACPI_STATE_D1:
              deviceCapabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD1;
              deviceCapabilities->D1Latency = device->power.states[i].latency;
              break;

           case ACPI_STATE_D2:
              deviceCapabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD2;
              deviceCapabilities->D2Latency = device->power.states[i].latency;
              break;

           case ACPI_STATE_D3:
              deviceCapabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
              deviceCapabilities->D3Latency = device->power.states[i].latency;
              break;
        }
    }

    deviceCapabilities->DeviceWake = PowerDeviceD1;

    deviceCapabilities->DeviceD1 =
        (deviceCapabilities->DeviceState[PowerSystemSleeping1] == PowerDeviceD1);
    deviceCapabilities->DeviceD2 =
        (deviceCapabilities->DeviceState[PowerSystemSleeping2] == PowerDeviceD2);

    deviceCapabilities->WakeFromD0 = FALSE;
    deviceCapabilities->WakeFromD1 = TRUE;
    deviceCapabilities->WakeFromD2 = FALSE;
    deviceCapabilities->WakeFromD3 = FALSE;

    if (device)
    {
       deviceCapabilities->LockSupported = device->flags.lockable;
       deviceCapabilities->EjectSupported = device->flags.ejectable;
       deviceCapabilities->HardwareDisabled = !device->status.enabled && !device->status.functional;
       deviceCapabilities->Removable = device->flags.removable;
       deviceCapabilities->SurpriseRemovalOK = device->flags.surprise_removal_ok;
       deviceCapabilities->UniqueID = device->flags.unique_id;
       deviceCapabilities->NoDisplayInUI = !device->status.show_in_ui;
       deviceCapabilities->Address = device->pnp.bus_address;
    }

    if (DeviceData->DockDevice)
        deviceCapabilities->DockDevice = TRUE;
    DPRINT("DockDevice: %u\n", deviceCapabilities->DockDevice);

    if (!device ||
        (device->flags.hardware_id &&
         (strstr(device->pnp.hardware_id, ACPI_BUTTON_HID_LID) ||
          strstr(device->pnp.hardware_id, ACPI_THERMAL_HID) ||
          strstr(device->pnp.hardware_id, ACPI_PROCESSOR_HID))))
    {
        /* Allow ACPI to control lid, thermal zone, processor, or fixed feature button */
        deviceCapabilities->RawDeviceOK = TRUE;
    }

    deviceCapabilities->SilentInstall = FALSE;
    deviceCapabilities->UINumber = (ULONG)-1;

    return STATUS_SUCCESS;
}

NTSTATUS
Bus_PDO_QueryDeviceId(
     PPDO_DEVICE_DATA     DeviceData,
      PIRP   Irp )
{
    PIO_STACK_LOCATION      stack;
    PWCHAR                  buffer, src;
    WCHAR                   temp[256];
    ULONG                   length, i;
    NTSTATUS                status = STATUS_SUCCESS;
    struct acpi_device     *Device = NULL;

    PAGED_CODE();

    stack = IoGetCurrentIrpStackLocation(Irp);

    switch (stack->Parameters.QueryId.IdType) {

    case BusQueryDeviceID:
    {
        /* REG_SZ */
        if (DeviceData->AcpiHandle)
            acpi_bus_get_device(DeviceData->AcpiHandle, &Device);

        if (Device && Device->pnp.hardware_id && strcmp(Device->pnp.hardware_id, "Processor") == 0)
        {
            length = (ULONG)wcslen(ProcessorIdString);
            wcscpy(temp, ProcessorIdString);
        }
        else if (Device && Device->pnp.hardware_id)
        {
            length = (ULONG)swprintf(temp,
                                     RTL_NUMBER_OF(temp),
                                     L"ACPI\\%hs",
                                     Device->pnp.hardware_id);
        }
        else
        {
            /* Fixed feature button => use generic ACPI FixedButton ID */
            length = (ULONG)swprintf(temp,
                                     RTL_NUMBER_OF(temp),
                                     L"ACPI\\FixedButton");
        }

        temp[length++] = UNICODE_NULL;
        NT_ASSERT(length * sizeof(WCHAR) <= sizeof(temp));

        buffer = ExAllocatePoolWithTag(PagedPool, length * sizeof(WCHAR), 'IpcA');
        if (!buffer) { status = STATUS_INSUFFICIENT_RESOURCES; break; }

        RtlCopyMemory(buffer, temp, length * sizeof(WCHAR));
        Irp->IoStatus.Information = (ULONG_PTR)buffer;
        DPRINT("BusQueryDeviceID: %ls\n", buffer);
        break;
    }

    case BusQueryInstanceID:
    {
        /* REG_SZ */
        if (DeviceData->AcpiHandle)
            acpi_bus_get_device(DeviceData->AcpiHandle, &Device);

        BOOLEAN isFFB = (DeviceData->AcpiHandle == NULL);
        length = BuspBuildStableInstanceId(Device, DeviceData, isFFB, temp, RTL_NUMBER_OF(temp));
        if (length == 0)
        {
            /* Fallback */
            length = (ULONG)swprintf(temp,
                                     RTL_NUMBER_OF(temp),
                                     L"%ls",
                                     L"0");
        }
        temp[length++] = UNICODE_NULL;

        NT_ASSERT(length * sizeof(WCHAR) <= sizeof(temp));

        buffer = ExAllocatePoolWithTag(PagedPool, length * sizeof(WCHAR), 'IpcA');
        if (!buffer) { status = STATUS_INSUFFICIENT_RESOURCES; break; }

        RtlCopyMemory(buffer, temp, length * sizeof(WCHAR));
        DPRINT("BusQueryInstanceID: %ls\n", buffer);
        Irp->IoStatus.Information = (ULONG_PTR)buffer;
        break;
    }

    case BusQueryHardwareIDs:
    {
        /* REG_MULTI_SZ */
        length = 0;
        status = STATUS_NOT_SUPPORTED;

        if (DeviceData->AcpiHandle)
            acpi_bus_get_device(DeviceData->AcpiHandle, &Device);

        if (Device && Device->flags.hardware_id)
        {
            DPRINT("Device name: %s\n", Device->pnp.device_name);
            DPRINT("Hardware ID: %s\n", Device->pnp.hardware_id);

            if (strcmp(Device->pnp.hardware_id, "Processor") == 0)
            {
                length = ProcessorHardwareIds.Length / sizeof(WCHAR);
                src = ProcessorHardwareIds.Buffer;
            }
            else
            {
                length += (ULONG)swprintf(&temp[length],
                                          RTL_NUMBER_OF(temp) - length,
                                          L"ACPI\\%hs",
                                          Device->pnp.hardware_id);
                temp[length++] = UNICODE_NULL;

                length += (ULONG)swprintf(&temp[length],
                                          RTL_NUMBER_OF(temp) - length,
                                          L"*%hs",
                                          Device->pnp.hardware_id);
                temp[length++] = UNICODE_NULL;
                temp[length++] = UNICODE_NULL;
                src = temp;
            }

            NT_ASSERT(length * sizeof(WCHAR) <= sizeof(temp));
            buffer = ExAllocatePoolWithTag(PagedPool, length * sizeof(WCHAR), 'IpcA');
            if (!buffer) { status = STATUS_INSUFFICIENT_RESOURCES; break; }

            RtlCopyMemory(buffer, src, length * sizeof(WCHAR));
            Irp->IoStatus.Information = (ULONG_PTR)buffer;
            DPRINT("BusQueryHardwareIDs: %ls\n", buffer);
            status = STATUS_SUCCESS;
        }
        else if (!DeviceData->AcpiHandle)
        {
            /* FixedFeatureButton */
            length += (ULONG)swprintf(&temp[length],
                                      RTL_NUMBER_OF(temp) - length,
                                      L"ACPI\\FixedButton");
            temp[length++] = UNICODE_NULL;

            length += (ULONG)swprintf(&temp[length],
                                      RTL_NUMBER_OF(temp) - length,
                                      L"*FixedButton");
            temp[length++] = UNICODE_NULL;
            temp[length++] = UNICODE_NULL;
            src = temp;

            NT_ASSERT(length * sizeof(WCHAR) <= sizeof(temp));
            buffer = ExAllocatePoolWithTag(PagedPool, length * sizeof(WCHAR), 'IpcA');
            if (!buffer) { status = STATUS_INSUFFICIENT_RESOURCES; break; }

            RtlCopyMemory(buffer, src, length * sizeof(WCHAR));
            Irp->IoStatus.Information = (ULONG_PTR)buffer;
            DPRINT("BusQueryHardwareIDs: %ls\n", buffer);
            status = STATUS_SUCCESS;
        }
        break;
    }

    case BusQueryCompatibleIDs:
    {
        /* REG_MULTI_SZ */
        length = 0;
        status = STATUS_NOT_SUPPORTED;

        if (DeviceData->AcpiHandle)
            acpi_bus_get_device(DeviceData->AcpiHandle, &Device);

        if (Device && Device->flags.hardware_id)
        {
            DPRINT("Device name: %s\n", Device->pnp.device_name);
            DPRINT("Hardware ID: %s\n", Device->pnp.hardware_id);

            if (strcmp(Device->pnp.hardware_id, "Processor") == 0)
            {
                length += (ULONG)swprintf(&temp[length],
                                          RTL_NUMBER_OF(temp) - length,
                                          L"ACPI\\%hs",
                                          Device->pnp.hardware_id);
                temp[length++] = UNICODE_NULL;

                length += (ULONG)swprintf(&temp[length],
                                          RTL_NUMBER_OF(temp) - length,
                                          L"*%hs",
                                          Device->pnp.hardware_id);
                temp[length++] = UNICODE_NULL;
                temp[length++] = UNICODE_NULL;
            }
            else if (Device->flags.compatible_ids && Device->pnp.cid_list)
            {
                for (i = 0; i < Device->pnp.cid_list->Count; i++)
                {
                    length += (ULONG)swprintf(&temp[length],
                                              RTL_NUMBER_OF(temp) - length,
                                              L"ACPI\\%hs",
                                              Device->pnp.cid_list->Ids[i].String);
                    temp[length++] = UNICODE_NULL;

                    length += (ULONG)swprintf(&temp[length],
                                              RTL_NUMBER_OF(temp) - length,
                                              L"*%hs",
                                              Device->pnp.cid_list->Ids[i].String);
                    temp[length++] = UNICODE_NULL;
                }
                temp[length++] = UNICODE_NULL;
            }
            else
            {
                break; /* No compatible IDs */
            }

            NT_ASSERT(length * sizeof(WCHAR) <= sizeof(temp));
            buffer = ExAllocatePoolWithTag(PagedPool, length * sizeof(WCHAR), 'IpcA');
            if (!buffer) { status = STATUS_INSUFFICIENT_RESOURCES; break; }

            RtlCopyMemory(buffer, temp, length * sizeof(WCHAR));
            Irp->IoStatus.Information = (ULONG_PTR)buffer;
            DPRINT("BusQueryCompatibleIDs: %ls\n", buffer);
            status = STATUS_SUCCESS;
        }
        break;
    }

    default:
        status = Irp->IoStatus.Status;
    }
    return status;
}

NTSTATUS
Bus_PDO_QueryDeviceText(
     PPDO_DEVICE_DATA     DeviceData,
      PIRP   Irp )
{
    PWCHAR  Buffer, Temp;
    PIO_STACK_LOCATION   stack;
    NTSTATUS    status = Irp->IoStatus.Status;

    PAGED_CODE();

    stack = IoGetCurrentIrpStackLocation(Irp);

    switch (stack->Parameters.QueryDeviceText.DeviceTextType) {

    case DeviceTextDescription:

        if (!Irp->IoStatus.Information) {
            if (wcsstr(DeviceData->HardwareIDs, L"PNP000") != 0)
                Temp = L"Programmable interrupt controller";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP010") != 0)
                Temp = L"System timer";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP020") != 0)
                Temp = L"DMA controller";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP03") != 0)
                Temp = L"Keyboard";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP040") != 0)
                Temp = L"Parallel port";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP05") != 0)
                Temp = L"Serial port";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP06") != 0)
                Temp = L"Disk controller";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP07") != 0)
                Temp = L"Disk controller";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP09") != 0)
                Temp = L"Display adapter";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0A0") != 0)
                Temp = L"Bus controller";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0E0") != 0)
                Temp = L"PCMCIA controller";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0F") != 0)
                Temp = L"Mouse device";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP8") != 0)
                Temp = L"Network adapter";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNPA0") != 0)
                Temp = L"SCSI controller";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNPB0") != 0)
                Temp = L"Multimedia device";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNPC00") != 0)
                Temp = L"Modem";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0C0C") != 0)
                Temp = L"Power Button";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0C0E") != 0)
                Temp = L"Sleep Button";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0C0D") != 0)
                Temp = L"Lid Switch";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0C09") != 0)
                Temp = L"ACPI Embedded Controller";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0C0B") != 0)
                Temp = L"ACPI Fan";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0A03") != 0 ||
                     wcsstr(DeviceData->HardwareIDs, L"PNP0A08") != 0)
                Temp = L"PCI Root Bridge";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0C0A") != 0)
                Temp = L"ACPI Battery";
            else if (wcsstr(DeviceData->HardwareIDs, L"PNP0C0F") != 0)
                Temp = L"PCI Interrupt Link";
            else if (wcsstr(DeviceData->HardwareIDs, L"ACPI_PWR") != 0)
                Temp = L"ACPI Power Resource";
            else if (wcsstr(DeviceData->HardwareIDs, L"Processor") != 0)
            {
                if (ProcessorNameString != NULL)
                    Temp = ProcessorNameString;
                else
                    Temp = L"Processor";
            }
            else if (wcsstr(DeviceData->HardwareIDs, L"ThermalZone") != 0)
                Temp = L"ACPI Thermal Zone";
            else if (wcsstr(DeviceData->HardwareIDs, L"ACPI0002") != 0)
                Temp = L"Smart Battery";
            else if (wcsstr(DeviceData->HardwareIDs, L"ACPI0003") != 0)
                Temp = L"AC Adapter";
            else if (!DeviceData->AcpiHandle)
                Temp = L"ACPI Fixed Feature Button";
            else
                Temp = L"Other ACPI device";

            Buffer = ExAllocatePoolWithTag(PagedPool, (wcslen(Temp) + 1) * sizeof(WCHAR), 'IpcA');
            if (!Buffer) { status = STATUS_INSUFFICIENT_RESOURCES; break; }

            RtlCopyMemory(Buffer, Temp, (wcslen(Temp) + 1) * sizeof(WCHAR));
            DPRINT("\tDeviceTextDescription :%ws\n", Buffer);

            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            status = STATUS_SUCCESS;
        }
        break;

    default:
        break;
    }

    return status;
}

NTSTATUS
Bus_PDO_QueryResources(
     PPDO_DEVICE_DATA     DeviceData,
      PIRP   Irp )
{
    ACPI_STATUS AcpiStatus;
    ACPI_BUFFER Buffer;
    PCM_RESOURCE_LIST ResourceList = NULL;
    BOOLEAN IsPciRoot;
    ULONG RootBusNumber = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (!DeviceData->AcpiHandle)
        return Irp->IoStatus.Status;

    IsPciRoot = BuspIsPciRootDevice(DeviceData);
    if (IsPciRoot)
    {
        RootBusNumber = BuspEnsurePciRootBusNumber(DeviceData);
    }

    Buffer.Length = 0;
    Buffer.Pointer = NULL;
    AcpiStatus = AcpiGetCurrentResources(DeviceData->AcpiHandle, &Buffer);
    if ((!ACPI_SUCCESS(AcpiStatus) && AcpiStatus != AE_BUFFER_OVERFLOW) || Buffer.Length == 0)
        return Irp->IoStatus.Status;

    Buffer.Pointer = ExAllocatePoolWithTag(PagedPool, Buffer.Length, 'BpcA');
    if (!Buffer.Pointer)
        return STATUS_INSUFFICIENT_RESOURCES;

    AcpiStatus = AcpiGetCurrentResources(DeviceData->AcpiHandle, &Buffer);
    if (!ACPI_SUCCESS(AcpiStatus))
    {
        DPRINT1("AcpiGetCurrentResources #2 failed (0x%x)\n", AcpiStatus);
        ExFreePoolWithTag(Buffer.Pointer, 'BpcA');
        return STATUS_UNSUCCESSFUL;
    }

    Status = BuspCreateResourceListFromAcpiResources(DeviceData,
                                                     Buffer.Pointer,
                                                     IsPciRoot,
                                                     RootBusNumber,
                                                     &ResourceList);

    ExFreePoolWithTag(Buffer.Pointer, 'BpcA');

    if (!NT_SUCCESS(Status))
        return Status;

    if (!ResourceList)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    Irp->IoStatus.Information = (ULONG_PTR)ResourceList;
    return STATUS_SUCCESS;
}


#endif /* !UNIT_TEST */

NTSTATUS
Bus_PDO_QueryResourceRequirements(
     PPDO_DEVICE_DATA     DeviceData,
      PIRP   Irp )
{
    ACPI_STATUS AcpiStatus;
    ACPI_BUFFER Buffer;
    BOOLEAN CurrentRes = FALSE;
    BOOLEAN IsPciRoot;
    ULONG RootBusNumber = 0;
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (!DeviceData->AcpiHandle)
        return Irp->IoStatus.Status;

    IsPciRoot = BuspIsPciRootDevice(DeviceData);
    if (IsPciRoot)
    {
        RootBusNumber = BuspEnsurePciRootBusNumber(DeviceData);
        CurrentRes = TRUE;

        Buffer.Length = 0;
        Buffer.Pointer = NULL;
        AcpiStatus = AcpiGetCurrentResources(DeviceData->AcpiHandle, &Buffer);
        if ((!ACPI_SUCCESS(AcpiStatus) && AcpiStatus != AE_BUFFER_OVERFLOW) ||
            Buffer.Length == 0)
        {
            return Irp->IoStatus.Status;
        }
    }
    else
    {
        while (TRUE)
        {
            Buffer.Length = 0;
            Buffer.Pointer = NULL;

            if (CurrentRes)
                AcpiStatus = AcpiGetCurrentResources(DeviceData->AcpiHandle, &Buffer);
            else
                AcpiStatus = AcpiGetPossibleResources(DeviceData->AcpiHandle, &Buffer);

            if ((!ACPI_SUCCESS(AcpiStatus) && AcpiStatus != AE_BUFFER_OVERFLOW) ||
                Buffer.Length == 0)
            {
                if (!CurrentRes)
                {
                    CurrentRes = TRUE;
                    continue;
                }

                return Irp->IoStatus.Status;
            }

            break;
        }
    }

    Buffer.Pointer = ExAllocatePoolWithTag(PagedPool, Buffer.Length, 'BpcA');
    if (!Buffer.Pointer)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (CurrentRes)
        AcpiStatus = AcpiGetCurrentResources(DeviceData->AcpiHandle, &Buffer);
    else
        AcpiStatus = AcpiGetPossibleResources(DeviceData->AcpiHandle, &Buffer);

    if (!ACPI_SUCCESS(AcpiStatus))
    {
        DPRINT1("AcpiGetResources failed (0x%x)\n", AcpiStatus);
        ExFreePoolWithTag(Buffer.Pointer, 'BpcA');
        return STATUS_UNSUCCESSFUL;
    }

    Status = BuspCreateRequirementsListFromAcpiResources(DeviceData,
                                                         Buffer.Pointer,
                                                         CurrentRes,
                                                         IsPciRoot,
                                                         RootBusNumber,
                                                         &RequirementsList);

    ExFreePoolWithTag(Buffer.Pointer, 'BpcA');

    if (!NT_SUCCESS(Status))
        return Status;

    if (!RequirementsList)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    Irp->IoStatus.Information = (ULONG_PTR)RequirementsList;
    return STATUS_SUCCESS;
}


#ifndef UNIT_TEST
NTSTATUS
Bus_PDO_QueryDeviceRelations(
     PPDO_DEVICE_DATA     DeviceData,
      PIRP   Irp )
{
    PIO_STACK_LOCATION   stack;
    PDEVICE_RELATIONS deviceRelations;
    NTSTATUS status;

    PAGED_CODE();

    stack = IoGetCurrentIrpStackLocation(Irp);

    switch (stack->Parameters.QueryDeviceRelations.Type) {

    case TargetDeviceRelation:

        deviceRelations = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
        if (deviceRelations) {
            ASSERTMSG("Someone above is handling TargetDeviceRelation\n", !deviceRelations);
        }

        deviceRelations = ExAllocatePoolWithTag(PagedPool,
                                                sizeof(DEVICE_RELATIONS),
                                                'IpcA');
        if (!deviceRelations) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        deviceRelations->Count = 1;
        deviceRelations->Objects[0] = DeviceData->Common.Self;
        ObReferenceObject(DeviceData->Common.Self);

        status = STATUS_SUCCESS;
        Irp->IoStatus.Information = (ULONG_PTR)deviceRelations;
        break;

    default:
        status = Irp->IoStatus.Status;
    }

    return status;
}

NTSTATUS
Bus_PDO_QueryBusInformation(
     PPDO_DEVICE_DATA     DeviceData,
      PIRP   Irp )
{
    PPNP_BUS_INFORMATION busInfo;

    PAGED_CODE();

    busInfo = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), 'IpcA');
    if (busInfo == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (BuspIsPciRootDevice(DeviceData))
    {
        busInfo->BusTypeGuid = GUID_BUS_TYPE_PCI;
        busInfo->LegacyBusType = PCIBus;
        if (DeviceData->HasPciRootBusRange)
            busInfo->BusNumber = DeviceData->PciRootMinBus;
        else
            busInfo->BusNumber = BuspEnsurePciRootBusNumber(DeviceData);
    }
    else
    {
        /* NOTE: Use the bus *type* GUID, not the ACPI interface GUID. */
        busInfo->BusTypeGuid = GUID_BUS_TYPE_ACPI;
        busInfo->LegacyBusType = Internal;
        busInfo->BusNumber = DeviceData->HasCachedBusNumber ? DeviceData->CachedBusNumber : 0;
    }

    Irp->IoStatus.Information = (ULONG_PTR)busInfo;
    return STATUS_SUCCESS;
}

NTSTATUS
Bus_GetDeviceCapabilities(
      PDEVICE_OBJECT          DeviceObject,
      PDEVICE_CAPABILITIES    DeviceCapabilities
    )
{
    IO_STATUS_BLOCK     ioStatus;
    KEVENT              pnpEvent;
    NTSTATUS            status;
    PDEVICE_OBJECT      targetObject;
    PIO_STACK_LOCATION  irpStack;
    PIRP                pnpIrp;

    PAGED_CODE();

    RtlZeroMemory(DeviceCapabilities, sizeof(DEVICE_CAPABILITIES));
    DeviceCapabilities->Size = sizeof(DEVICE_CAPABILITIES);
    DeviceCapabilities->Version = 1;
    DeviceCapabilities->Address = (ULONG)-1;
    DeviceCapabilities->UINumber = (ULONG)-1;

    KeInitializeEvent(&pnpEvent, NotificationEvent, FALSE);
    targetObject = IoGetAttachedDeviceReference(DeviceObject);

    pnpIrp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                          targetObject,
                                          NULL,
                                          0,
                                          NULL,
                                          &pnpEvent,
                                          &ioStatus);
    if (pnpIrp == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto GetDeviceCapabilitiesExit;
    }

    pnpIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irpStack = IoGetNextIrpStackLocation(pnpIrp);
    RtlZeroMemory(irpStack, sizeof(IO_STACK_LOCATION));
    irpStack->MajorFunction = IRP_MJ_PNP;
    irpStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
    irpStack->Parameters.DeviceCapabilities.Capabilities = DeviceCapabilities;

    status = IoCallDriver(targetObject, pnpIrp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&pnpEvent, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }

GetDeviceCapabilitiesExit:
    ObDereferenceObject(targetObject);
    return status;
}
#endif /* !UNIT_TEST */
