/*
 * PROJECT:     ReactOS Universal Serial Bus Bulk Storage Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     USB block storage device driver.
 * COPYRIGHT:   2005-2006 James Tabor
 *              2011-2012 Michael Martin (michael.martin@reactos.org)
 *              2011-2013 Johannes Anderwald (johannes.anderwald@reactos.org)
 *              2017 Vadim Galyant
 */

#include "usbstor.h"
#include <sptilib.h>

#define NDEBUG
#include <debug.h>

// See CORE-10515 and CORE-10755
#define USBSTOR_DEFAULT_MAX_PHYS_PAGES    (USBSTOR_DEFAULT_MAX_TRANSFER_LENGTH / PAGE_SIZE + 1)

static NTSTATUS USBSTOR_DumpCommand(_In_ PUSBSTOR_DUMP_CONTEXT DumpContext, _In_reads_bytes_(CdbLength) PUCHAR Cdb, _In_ UCHAR CdbLength, _Inout_updates_bytes_opt_(DataLength) PVOID DataBuffer, _In_ PHYSICAL_ADDRESS DataPhysicalAddress, _In_ BOOLEAN DataIsPhysical, _In_ ULONG DataLength, _In_ BOOLEAN DataIn)
{
    NTSTATUS Status;

    RtlZeroMemory(&DumpContext->Cbw, sizeof(DumpContext->Cbw));
    RtlZeroMemory(&DumpContext->Csw, sizeof(DumpContext->Csw));
    DumpContext->Cbw.Signature = CBW_SIGNATURE;
    DumpContext->Cbw.Tag = ++DumpContext->Tag;
    DumpContext->Cbw.DataTransferLength = DataLength;
    DumpContext->Cbw.Flags = DataIn ? 0x80 : 0;
    DumpContext->Cbw.LUN = DumpContext->PdoExtension->LUN & MAX_LUN;
    DumpContext->Cbw.CommandBlockLength = CdbLength;
    RtlCopyMemory(DumpContext->Cbw.CommandBlock, Cdb, CdbLength);

    Status = DumpContext->UsbInterface.Transfer(DumpContext->UsbInterface.Context, FALSE, &DumpContext->Cbw, sizeof(DumpContext->Cbw));
    if (!NT_SUCCESS(Status))
        return Status;

    if (DataLength != 0)
    {
        if (DataIsPhysical)
        {
            if (DataIn)
                return STATUS_INVALID_PARAMETER;
            Status = DumpContext->UsbInterface.TransferPhysical(DumpContext->UsbInterface.Context, DataPhysicalAddress, DataLength);
        }
        else
        {
            Status = DumpContext->UsbInterface.Transfer(DumpContext->UsbInterface.Context, DataIn, DataBuffer, DataLength);
        }
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = DumpContext->UsbInterface.Transfer(DumpContext->UsbInterface.Context, TRUE, &DumpContext->Csw, sizeof(DumpContext->Csw));
    if (!NT_SUCCESS(Status))
        return Status;

    if ((DumpContext->Csw.Signature != CSW_SIGNATURE) || (DumpContext->Csw.Tag != DumpContext->Cbw.Tag) || (DumpContext->Csw.DataResidue != 0) || (DumpContext->Csw.Status != CSW_STATUS_COMMAND_PASSED))
    {
        return STATUS_IO_DEVICE_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI USBSTOR_DumpPrepare(_In_ PVOID Context)
{
    PUSBSTOR_DUMP_CONTEXT DumpContext = Context;

    DumpContext->Tag = 0;
    return DumpContext->UsbInterface.Prepare(DumpContext->UsbInterface.Context);
}

static NTSTATUS NTAPI USBSTOR_DumpWrite(_In_ PVOID Context, _In_ ULONG64 DiskByteOffset, _In_ PHYSICAL_ADDRESS Buffer, _In_ ULONG Length)
{
    PUSBSTOR_DUMP_CONTEXT DumpContext = Context;
    ULONG64 BlockAddress;
    ULONG BlockCount;
    CDB Cdb;
    UCHAR CdbLength;

    if ((((DiskByteOffset | Length) % DumpContext->BytesPerSector) != 0) || (Length == 0) || (Length > PAGE_SIZE))
    {
        return STATUS_INVALID_PARAMETER;
    }

    BlockAddress = DiskByteOffset / DumpContext->BytesPerSector;
    BlockCount = Length / DumpContext->BytesPerSector;
    RtlZeroMemory(&Cdb, sizeof(Cdb));
    CdbLength = RosDumpBuildWriteCdb(&Cdb, BlockAddress, BlockCount);

    return USBSTOR_DumpCommand(DumpContext, (PUCHAR)&Cdb, CdbLength, NULL, Buffer, TRUE, Length, FALSE);
}

static NTSTATUS NTAPI USBSTOR_DumpFlush(_In_ PVOID Context)
{
    PUSBSTOR_DUMP_CONTEXT DumpContext = Context;
    CDB Cdb;
    PHYSICAL_ADDRESS PhysicalAddress;

    RtlZeroMemory(&Cdb, sizeof(Cdb));
    PhysicalAddress.QuadPart = 0;
    Cdb.CDB10.OperationCode = SCSIOP_SYNCHRONIZE_CACHE;
    return USBSTOR_DumpCommand(DumpContext, (PUCHAR)&Cdb, 10, NULL, PhysicalAddress, FALSE, 0, FALSE);
}

static NTSTATUS USBSTOR_GetDumpInterface(_In_ PPDO_DEVICE_EXTENSION PdoExtension, _Inout_ PROS_STORAGE_DUMP_INTERFACE Interface)
{
    PFDO_DEVICE_EXTENSION FdoExtension;
    PUSBSTOR_DUMP_CONTEXT DumpContext;
    ROS_USB_DUMP_INTERFACE UsbInterface;
    PUSBD_PIPE_INFORMATION BulkInPipe;
    PUSBD_PIPE_INFORMATION BulkOutPipe;
    ULONG BytesPerSector;
    NTSTATUS Status;

    BytesPerSector = Interface->BytesPerSector;
    if (!RosDumpIsValidSectorSize(BytesPerSector))
    {
        DPRINT1("USBSTOR: dump interface rejected sector size %lu\n", BytesPerSector);
        return STATUS_INVALID_PARAMETER;
    }

    DumpContext = PdoExtension->DumpContext;
    if (DumpContext == NULL)
    {
        FdoExtension = PdoExtension->LowerDeviceObject->DeviceExtension;
        if ((FdoExtension->InterfaceInformation == NULL) || (FdoExtension->LowerDeviceObject == NULL))
        {
            return STATUS_DEVICE_NOT_READY;
        }

        BulkInPipe = &FdoExtension->InterfaceInformation->Pipes[FdoExtension->BulkInPipeIndex];
        BulkOutPipe = &FdoExtension->InterfaceInformation->Pipes[FdoExtension->BulkOutPipeIndex];
        RtlZeroMemory(&UsbInterface, sizeof(UsbInterface));
        UsbInterface.Version = ROS_USB_DUMP_INTERFACE_VERSION;
        UsbInterface.Size = sizeof(UsbInterface);
        UsbInterface.BulkInPipe = BulkInPipe->PipeHandle;
        UsbInterface.BulkOutPipe = BulkOutPipe->PipeHandle;
        UsbInterface.InterfaceNumber = FdoExtension->InterfaceInformation->InterfaceNumber;
        UsbInterface.BulkInEndpointAddress = BulkInPipe->EndpointAddress;
        UsbInterface.BulkOutEndpointAddress = BulkOutPipe->EndpointAddress;
        Status = USBSTOR_SyncInternalRequest(FdoExtension->LowerDeviceObject, IOCTL_INTERNAL_REACTOS_USB_GET_DUMP_INTERFACE, &UsbInterface);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("USBSTOR: USB dump interface request failed (0x%08lx)\n", Status);
            return Status;
        }
        if ((UsbInterface.Version != ROS_USB_DUMP_INTERFACE_VERSION) || (UsbInterface.Size < sizeof(UsbInterface)) || (UsbInterface.Context == NULL) || (UsbInterface.Prepare == NULL) || (UsbInterface.Transfer == NULL) || (UsbInterface.TransferPhysical == NULL))
        {
            return STATUS_REVISION_MISMATCH;
        }

        DumpContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DumpContext), USBSTOR_DUMP_CONTEXT_TAG);
        if (DumpContext == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(DumpContext, sizeof(*DumpContext));
        DumpContext->FdoExtension = FdoExtension;
        DumpContext->PdoExtension = PdoExtension;
        DumpContext->UsbInterface = UsbInterface;
        DumpContext->BytesPerSector = BytesPerSector;
        PdoExtension->DumpContext = DumpContext;
    }
    else if (DumpContext->BytesPerSector != BytesPerSector)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    RtlZeroMemory(Interface, sizeof(*Interface));
    Interface->Version = ROS_STORAGE_DUMP_INTERFACE_VERSION;
    Interface->Size = sizeof(*Interface);
    Interface->Context = DumpContext;
    Interface->BytesPerSector = DumpContext->BytesPerSector;
    Interface->MaximumTransferLength = PAGE_SIZE;
    Interface->Prepare = USBSTOR_DumpPrepare;
    Interface->Write = USBSTOR_DumpWrite;
    Interface->Flush = USBSTOR_DumpFlush;
    return STATUS_SUCCESS;
}

static
BOOLEAN
IsRequestValid(PIRP Irp)
{
    ULONG TransferLength;
    PIO_STACK_LOCATION IoStack;
    PSCSI_REQUEST_BLOCK Srb;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Srb = IoStack->Parameters.Scsi.Srb;

    if (Srb->SrbFlags & (SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT))
    {
        if ((Srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) == SRB_FLAGS_UNSPECIFIED_DIRECTION)
        {
            DPRINT1("IsRequestValid: Invalid Srb. Srb->SrbFlags - %X\n", Srb->SrbFlags);
            return FALSE;
        }

        TransferLength = Srb->DataTransferLength;

        if (Irp->MdlAddress == NULL)
        {
            DPRINT1("IsRequestValid: Invalid Srb. Irp->MdlAddress == NULL\n");
            return FALSE;
        }

        if (TransferLength == 0)
        {
            DPRINT1("IsRequestValid: Invalid Srb. TransferLength == 0\n");
            return FALSE;
        }

        if (TransferLength > USBSTOR_DEFAULT_MAX_TRANSFER_LENGTH)
        {
            DPRINT1("IsRequestValid: Invalid Srb. TransferLength > %lu\n",
                    (ULONG)USBSTOR_DEFAULT_MAX_TRANSFER_LENGTH);
            return FALSE;
        }
    }
    else
    {
        if (Srb->DataTransferLength)
        {
            DPRINT1("IsRequestValid: Invalid Srb. Srb->DataTransferLength != 0\n");
            return FALSE;
        }

        if (Srb->DataBuffer)
        {
            DPRINT1("IsRequestValid: Invalid Srb. Srb->DataBuffer != NULL\n");
            return FALSE;
        }

        if (Irp->MdlAddress)
        {
            DPRINT1("IsRequestValid: Invalid Srb. Irp->MdlAddress != NULL\n");
            return FALSE;
        }
    }

    return TRUE;
}

NTSTATUS
USBSTOR_HandleInternalDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PSCSI_REQUEST_BLOCK Request;
    PPDO_DEVICE_EXTENSION PDODeviceExtension;
    NTSTATUS Status;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Request = (PSCSI_REQUEST_BLOCK)IoStack->Parameters.Others.Argument1;
    ASSERT(Request);
    PDODeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);

    switch(Request->Function)
    {
        case SRB_FUNCTION_EXECUTE_SCSI:
        {
            DPRINT("SRB_FUNCTION_EXECUTE_SCSI\n");

            if (!IsRequestValid(Irp))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (Request->Cdb[0] == SCSIOP_MODE_SENSE)
            {
                DPRINT("USBSTOR_Scsi: SRB_FUNCTION_EXECUTE_SCSI - FIXME SCSIOP_MODE_SENSE\n");
                // FIXME Get from registry WriteProtect for StorageDevicePolicies;
                // L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\StorageDevicePolicies"
                // QueryTable[0].Name = L"WriteProtect"
            }

            IoMarkIrpPending(Irp);
            Request->SrbStatus = SRB_STATUS_PENDING;

            // add the request
            if (!USBSTOR_QueueAddIrp(PDODeviceExtension->LowerDeviceObject, Irp))
            {
                IoStartPacket(PDODeviceExtension->LowerDeviceObject, Irp, &Request->QueueSortKey, USBSTOR_CancelIo);
            }

            return STATUS_PENDING;
        }
        case SRB_FUNCTION_RELEASE_DEVICE:
        {
            DPRINT1("SRB_FUNCTION_RELEASE_DEVICE\n");
            ASSERT(PDODeviceExtension->Claimed == TRUE);

            // release claim
            PDODeviceExtension->Claimed = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }
        case SRB_FUNCTION_CLAIM_DEVICE:
        {
            DPRINT1("SRB_FUNCTION_CLAIM_DEVICE\n");

            // check if the device has been claimed
            if (PDODeviceExtension->Claimed)
            {
                // device has already been claimed
                Status = STATUS_DEVICE_BUSY;
                Request->SrbStatus = SRB_STATUS_BUSY;
                break;
            }

            // claim device
            PDODeviceExtension->Claimed = TRUE;

            // output device object
            Request->DataBuffer = DeviceObject;

            Status = STATUS_SUCCESS;
            break;
        }
        case SRB_FUNCTION_RELEASE_QUEUE:
        {
            DPRINT1("SRB_FUNCTION_RELEASE_QUEUE\n");

            USBSTOR_QueueRelease(PDODeviceExtension->LowerDeviceObject);

            Request->SrbStatus = SRB_STATUS_SUCCESS;
            Status = STATUS_SUCCESS;
            break;
        }

        case SRB_FUNCTION_SHUTDOWN:
        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_FLUSH_QUEUE:
        {
            // wait for pending requests to finish
            USBSTOR_QueueWaitForPendingRequests(PDODeviceExtension->LowerDeviceObject);

            Request->SrbStatus = SRB_STATUS_SUCCESS;
            Status = STATUS_SUCCESS;
            break;
        }
        default:
        {
            //
            // not supported
            //
            Status = STATUS_NOT_SUPPORTED;
            Request->SrbStatus = SRB_STATUS_ERROR;
        }
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

ULONG
USBSTOR_GetFieldLength(
    IN PUCHAR Name,
    IN ULONG MaxLength)
{
    ULONG Index;
    ULONG LastCharacterPosition = 0;

    // scan the field and return last position which contains a valid character
    for(Index = 0; Index < MaxLength; Index++)
    {
        if (Name[Index] != ' ')
        {
            // trim white spaces from field
            LastCharacterPosition = Index;
        }
    }

    // convert from zero based index to length
    return LastCharacterPosition + 1;
}

NTSTATUS
USBSTOR_HandleQueryProperty(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PSTORAGE_PROPERTY_QUERY PropertyQuery;
    PSTORAGE_DESCRIPTOR_HEADER DescriptorHeader;
    PSTORAGE_ADAPTER_DESCRIPTOR_WIN8 AdapterDescriptor;
    ULONG FieldLengthVendor, FieldLengthProduct, FieldLengthRevision, TotalLength, FieldLengthSerialNumber;
    PPDO_DEVICE_EXTENSION PDODeviceExtension;
    PINQUIRYDATA InquiryData;
    PSTORAGE_DEVICE_DESCRIPTOR DeviceDescriptor;
    PUCHAR Buffer;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    UNICODE_STRING SerialNumber;
    ANSI_STRING AnsiString;
    NTSTATUS Status;

    DPRINT("USBSTOR_HandleQueryProperty\n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    ASSERT(IoStack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(STORAGE_PROPERTY_QUERY));
    ASSERT(Irp->AssociatedIrp.SystemBuffer);

    PropertyQuery = (PSTORAGE_PROPERTY_QUERY)Irp->AssociatedIrp.SystemBuffer;

    // check property type
    if (PropertyQuery->PropertyId != StorageDeviceProperty &&
        PropertyQuery->PropertyId != StorageAdapterProperty)
    {
        // only device property / adapter property are supported
        return STATUS_INVALID_PARAMETER_1;
    }

    // check query type
    if (PropertyQuery->QueryType == PropertyExistsQuery)
    {
        // device property / adapter property is supported
        return STATUS_SUCCESS;
    }

    if (PropertyQuery->QueryType != PropertyStandardQuery)
    {
        // only standard query and exists query are supported
        return STATUS_INVALID_PARAMETER_2;
    }

    // check if it is a device property
    if (PropertyQuery->PropertyId == StorageDeviceProperty)
    {
        DPRINT("USBSTOR_HandleQueryProperty StorageDeviceProperty OutputBufferLength %lu\n", IoStack->Parameters.DeviceIoControl.OutputBufferLength);

        PDODeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
        ASSERT(PDODeviceExtension);
        ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);

        FDODeviceExtension = (PFDO_DEVICE_EXTENSION)PDODeviceExtension->LowerDeviceObject->DeviceExtension;
        ASSERT(FDODeviceExtension);
        ASSERT(FDODeviceExtension->Common.IsFDO);

        InquiryData = (PINQUIRYDATA)&PDODeviceExtension->InquiryData;

        // compute extra parameters length
        FieldLengthVendor = USBSTOR_GetFieldLength(InquiryData->VendorId, 8);
        FieldLengthProduct = USBSTOR_GetFieldLength(InquiryData->ProductId, 16);
        FieldLengthRevision = USBSTOR_GetFieldLength(InquiryData->ProductRevisionLevel, 4);

        if (FDODeviceExtension->SerialNumber)
        {
            FieldLengthSerialNumber = wcslen(FDODeviceExtension->SerialNumber->bString);
        }
        else
        {
            FieldLengthSerialNumber = 0;
        }

        // total length required is sizeof(STORAGE_DEVICE_DESCRIPTOR) + FieldLength + 4 extra null bytes - 1
        // -1 due STORAGE_DEVICE_DESCRIPTOR contains one byte length of parameter data
        TotalLength = sizeof(STORAGE_DEVICE_DESCRIPTOR) + FieldLengthVendor + FieldLengthProduct + FieldLengthRevision + FieldLengthSerialNumber + 3;

        // check if output buffer is long enough
        if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < TotalLength)
        {
            // buffer too small
            DescriptorHeader = (PSTORAGE_DESCRIPTOR_HEADER)Irp->AssociatedIrp.SystemBuffer;
            ASSERT(IoStack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(STORAGE_DESCRIPTOR_HEADER));

            // return required size
            DescriptorHeader->Version = TotalLength;
            DescriptorHeader->Size = TotalLength;

            Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
            return STATUS_SUCCESS;
        }

        // initialize the device descriptor
        DeviceDescriptor = (PSTORAGE_DEVICE_DESCRIPTOR)Irp->AssociatedIrp.SystemBuffer;

        DeviceDescriptor->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
        DeviceDescriptor->Size = TotalLength;
        DeviceDescriptor->DeviceType = InquiryData->DeviceType;
        DeviceDescriptor->DeviceTypeModifier = InquiryData->DeviceTypeModifier;
        DeviceDescriptor->RemovableMedia = InquiryData->RemovableMedia;
        DeviceDescriptor->CommandQueueing = FALSE;
        DeviceDescriptor->BusType = BusTypeUsb;
        DeviceDescriptor->VendorIdOffset = sizeof(STORAGE_DEVICE_DESCRIPTOR) - sizeof(UCHAR);
        DeviceDescriptor->ProductIdOffset = DeviceDescriptor->VendorIdOffset + FieldLengthVendor + 1;
        DeviceDescriptor->ProductRevisionOffset = DeviceDescriptor->ProductIdOffset + FieldLengthProduct + 1;
        DeviceDescriptor->SerialNumberOffset = (FieldLengthSerialNumber > 0 ? DeviceDescriptor->ProductRevisionOffset + FieldLengthRevision + 1 : 0);
        DeviceDescriptor->RawPropertiesLength = FieldLengthVendor + FieldLengthProduct + FieldLengthRevision + FieldLengthSerialNumber + 3 + (FieldLengthSerialNumber > 0 ? + 1 : 0);

        // copy descriptors
        Buffer = (PUCHAR)((ULONG_PTR)DeviceDescriptor + sizeof(STORAGE_DEVICE_DESCRIPTOR) - sizeof(UCHAR));

        RtlCopyMemory(Buffer, InquiryData->VendorId, FieldLengthVendor);
        Buffer[FieldLengthVendor] = '\0';
        Buffer += FieldLengthVendor + 1;

        RtlCopyMemory(Buffer, InquiryData->ProductId, FieldLengthProduct);
        Buffer[FieldLengthProduct] = '\0';
        Buffer += FieldLengthProduct + 1;

        RtlCopyMemory(Buffer, InquiryData->ProductRevisionLevel, FieldLengthRevision);
        Buffer[FieldLengthRevision] = '\0';
        Buffer += FieldLengthRevision + 1;

        if (FieldLengthSerialNumber)
        {
            RtlInitUnicodeString(&SerialNumber, FDODeviceExtension->SerialNumber->bString);

            AnsiString.Buffer = (PCHAR)Buffer;
            AnsiString.Length = 0;
            AnsiString.MaximumLength = FieldLengthSerialNumber * sizeof(WCHAR);

            Status = RtlUnicodeStringToAnsiString(&AnsiString, &SerialNumber, FALSE);
            ASSERT(Status == STATUS_SUCCESS);
        }

        DPRINT("Vendor %s\n", (LPCSTR)((ULONG_PTR)DeviceDescriptor + DeviceDescriptor->VendorIdOffset));
        DPRINT("Product %s\n", (LPCSTR)((ULONG_PTR)DeviceDescriptor + DeviceDescriptor->ProductIdOffset));
        DPRINT("Revision %s\n", (LPCSTR)((ULONG_PTR)DeviceDescriptor + DeviceDescriptor->ProductRevisionOffset));
        DPRINT("Serial %s\n", (LPCSTR)((ULONG_PTR)DeviceDescriptor + DeviceDescriptor->SerialNumberOffset));

        Irp->IoStatus.Information = TotalLength;
        return STATUS_SUCCESS;
    }
    else
    {
        // adapter property query request

        DPRINT("USBSTOR_HandleQueryProperty StorageAdapterProperty OutputBufferLength %lu\n", IoStack->Parameters.DeviceIoControl.OutputBufferLength);

        if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(STORAGE_ADAPTER_DESCRIPTOR_WIN8))
        {
            // buffer too small
            DescriptorHeader = (PSTORAGE_DESCRIPTOR_HEADER)Irp->AssociatedIrp.SystemBuffer;
            ASSERT(IoStack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(STORAGE_DESCRIPTOR_HEADER));

            // return required size
            DescriptorHeader->Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR_WIN8);
            DescriptorHeader->Size = sizeof(STORAGE_ADAPTER_DESCRIPTOR_WIN8);

            Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER);
            return STATUS_SUCCESS;
        }

        // get adapter descriptor, information is returned in the same buffer
        AdapterDescriptor = Irp->AssociatedIrp.SystemBuffer;

        // fill out descriptor
        // NOTE: STORAGE_ADAPTER_DESCRIPTOR_WIN8 may vary in size, so it's important to zero out
        // all unused fields
        *AdapterDescriptor = (STORAGE_ADAPTER_DESCRIPTOR_WIN8) {
            .Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR_WIN8),
            .Size = sizeof(STORAGE_ADAPTER_DESCRIPTOR_WIN8),
            .MaximumTransferLength = USBSTOR_DEFAULT_MAX_TRANSFER_LENGTH,
            .MaximumPhysicalPages = USBSTOR_DEFAULT_MAX_PHYS_PAGES,
            .BusType = BusTypeUsb,
            .BusMajorVersion = 2, //FIXME verify
            .BusMinorVersion = 0 //FIXME
        };

        // __debugbreak();

        // store returned length
        Irp->IoStatus.Information = sizeof(STORAGE_ADAPTER_DESCRIPTOR_WIN8);

        return STATUS_SUCCESS;
    }
}

NTSTATUS
USBSTOR_HandleDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PPDO_DEVICE_EXTENSION PDODeviceExtension;
    PSCSI_ADAPTER_BUS_INFO BusInfo;
    PSCSI_INQUIRY_DATA ScsiInquiryData;
    PINQUIRYDATA InquiryData;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_REACTOS_STORAGE_GET_DUMP_INTERFACE:
            PDODeviceExtension = DeviceObject->DeviceExtension;
            if (PDODeviceExtension->Common.IsFDO)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }
            if ((Irp->AssociatedIrp.SystemBuffer == NULL) || (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ROS_STORAGE_DUMP_INTERFACE)))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Status = USBSTOR_GetDumpInterface(PDODeviceExtension, Irp->AssociatedIrp.SystemBuffer);
            if (NT_SUCCESS(Status))
            {
                Irp->IoStatus.Information = sizeof(ROS_STORAGE_DUMP_INTERFACE);
            }
            break;

        case IOCTL_STORAGE_QUERY_PROPERTY:
            Status = USBSTOR_HandleQueryProperty(DeviceObject, Irp);
            break;
        case IOCTL_SCSI_PASS_THROUGH:
        case IOCTL_SCSI_PASS_THROUGH_DIRECT:
        {
            PDODeviceExtension = DeviceObject->DeviceExtension;

            ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
            ASSERT(PDODeviceExtension);
            ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);

            /* Skip requests that bypassed the class driver. See also cdrom!RequestHandleScsiPassThrough */
            if ((IoGetCurrentIrpStackLocation(Irp)->MinorFunction == 0) && PDODeviceExtension->Claimed)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Status = SptiHandleScsiPassthru(DeviceObject,
                                            Irp,
                                            USBSTOR_DEFAULT_MAX_TRANSFER_LENGTH,
                                            USBSTOR_DEFAULT_MAX_PHYS_PAGES);
            break;
        }
        case IOCTL_STORAGE_GET_MEDIA_SERIAL_NUMBER:
            DPRINT1("USBSTOR_HandleDeviceControl IOCTL_STORAGE_GET_MEDIA_SERIAL_NUMBER NOT implemented\n");
            Status = STATUS_NOT_SUPPORTED;
            break;
        case IOCTL_SCSI_GET_CAPABILITIES:
        {
            PIO_SCSI_CAPABILITIES Capabilities;

            // Legacy port capability query
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength == sizeof(PVOID))
            {
                Capabilities = *((PVOID *)Irp->AssociatedIrp.SystemBuffer) = ExAllocatePoolWithTag(NonPagedPool,
                                                                                                   sizeof(IO_SCSI_CAPABILITIES),
                                                                                                   USB_STOR_TAG);
                Irp->IoStatus.Information = sizeof(PVOID);
            }
            else
            {
                Capabilities = Irp->AssociatedIrp.SystemBuffer;
                Irp->IoStatus.Information = sizeof(IO_SCSI_CAPABILITIES);
            }

            if (Capabilities)
            {
                Capabilities->MaximumTransferLength = USBSTOR_DEFAULT_MAX_TRANSFER_LENGTH;
                Capabilities->MaximumPhysicalPages = USBSTOR_DEFAULT_MAX_PHYS_PAGES;
                Capabilities->SupportedAsynchronousEvents = 0;
                Capabilities->AlignmentMask = 0;
                Capabilities->TaggedQueuing = FALSE;
                Capabilities->AdapterScansDown = FALSE;
                Capabilities->AdapterUsesPio = FALSE;
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }

            break;
        }
        case IOCTL_SCSI_GET_INQUIRY_DATA:
        {
            PDODeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
            ASSERT(PDODeviceExtension);
            ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);

            // get parameters
            BusInfo = Irp->AssociatedIrp.SystemBuffer;
            ScsiInquiryData = (PSCSI_INQUIRY_DATA)(BusInfo + 1);
            InquiryData = (PINQUIRYDATA)ScsiInquiryData->InquiryData;


            BusInfo->NumberOfBuses = 1;
            BusInfo->BusData[0].NumberOfLogicalUnits = 1; //FIXME
            BusInfo->BusData[0].InitiatorBusId = 0;
            BusInfo->BusData[0].InquiryDataOffset = sizeof(SCSI_ADAPTER_BUS_INFO);

            ScsiInquiryData->PathId = 0;
            ScsiInquiryData->TargetId = 0;
            ScsiInquiryData->Lun = PDODeviceExtension->LUN & MAX_LUN;
            ScsiInquiryData->DeviceClaimed = PDODeviceExtension->Claimed;
            ScsiInquiryData->InquiryDataLength = sizeof(INQUIRYDATA);
            ScsiInquiryData->NextInquiryDataOffset = 0;

            // Note: INQUIRYDATA structure is larger than INQUIRYDATABUFFERSIZE
            RtlZeroMemory(InquiryData, sizeof(INQUIRYDATA));
            RtlCopyMemory(InquiryData, &PDODeviceExtension->InquiryData, sizeof(PDODeviceExtension->InquiryData));

            InquiryData->Versions = 0x04;
            InquiryData->ResponseDataFormat = 0x02; // some devices set this to 1

            Irp->IoStatus.Information = sizeof(SCSI_ADAPTER_BUS_INFO) + sizeof(SCSI_INQUIRY_DATA) + sizeof(INQUIRYDATA) - 1;
            Status = STATUS_SUCCESS;

            break;
        }
        case IOCTL_SCSI_GET_ADDRESS:
        {
            PSCSI_ADDRESS Address = Irp->AssociatedIrp.SystemBuffer;

            Address->Length = sizeof(SCSI_ADDRESS);
            Address->PortNumber = 0;
            Address->PathId = 0;
            Address->TargetId = 0;
            Address->Lun = (((PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->LUN & MAX_LUN);
            Irp->IoStatus.Information = sizeof(SCSI_ADDRESS);

            Status = STATUS_SUCCESS;

            break;
        }
        default:
            DPRINT("USBSTOR_HandleDeviceControl IoControl %x not supported\n", IoStack->Parameters.DeviceIoControl.IoControlCode);
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    return Status;
}
