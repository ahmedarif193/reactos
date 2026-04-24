/*
 * PROJECT:     ReactOS SD Function Protocol Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SD memory card function protocol driver (sffp_sd.sys)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * DESCRIPTION:
 *     sffp_sd.sys is the function protocol driver for SD memory cards.
 *     It sits between the SD storage class driver (sffdisk.sys) above
 *     and the SD bus driver (sdbus.sys) below. Its primary role is to
 *     translate block I/O requests into the correct SD card commands
 *     (CMD17/CMD18 for reads, CMD24/CMD25 for writes) with proper
 *     address translation (block vs byte addressing for SDHC vs SDSC).
 */

/* INCLUDES *******************************************************************/

#include "sffp_sd.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, SffpSdAddDevice)
#pragma alloc_text(PAGE, SffpSdPnp)
#endif

/* PRIVATE FUNCTIONS **********************************************************/

/**
 * @brief Completion routine for synchronously forwarded IRPs.
 *
 * Signals the event so the waiting thread can proceed.
 *
 * @param[in] DeviceObject  Pointer to the device object (unused).
 * @param[in] Irp           Pointer to the completed IRP (unused).
 * @param[in] Context       Pointer to a KEVENT to signal.
 *
 * @return STATUS_MORE_PROCESSING_REQUIRED to prevent further completion.
 */
static
NTSTATUS
NTAPI
SffpSdForwardIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/**
 * @brief Send an IRP down the stack and wait synchronously for completion.
 *
 * @param[in]     DeviceObject  Pointer to the device object.
 * @param[in,out] Irp           Pointer to the IRP to forward.
 *
 * @return NTSTATUS from the lower driver.
 */
static
NTSTATUS
SffpSdForwardIrpSynchronous(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PSFFP_SD_EXTENSION DeviceExtension;
    KEVENT Event;
    NTSTATUS Status;

    DeviceExtension = (PSFFP_SD_EXTENSION)DeviceObject->DeviceExtension;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SffpSdForwardIrpCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = Irp->IoStatus.Status;
    }

    return Status;
}

/**
 * @brief Query a property from the SD bus driver.
 *
 * @param[in]  DeviceExtension  Pointer to the sffp_sd device extension.
 * @param[in]  Property         The SD bus property identifier to query.
 * @param[out] Buffer           Receives the queried property value.
 * @param[in]  Length           Size of the output buffer in bytes.
 *
 * @return NTSTATUS from the bus driver.
 */
static
NTSTATUS
SffpSdGetProperty(
    _In_ PSFFP_SD_EXTENSION DeviceExtension,
    _In_ SDBUS_PROPERTY Property,
    _Out_ PVOID Buffer,
    _In_ ULONG Length)
{
    SDBUS_REQUEST_PACKET Srb;

    SD_INIT_GET_PROPERTY(&Srb, Property, Buffer, Length);

    return SdBusSubmitRequest(DeviceExtension->BusInterface.Context, &Srb);
}

/**
 * @brief Open the SD bus interface and query card capabilities.
 *
 * Called during IRP_MN_START_DEVICE processing. Opens the SDBUS interface,
 * initializes block size parameters, and queries the function type and
 * high-capacity addressing mode.
 *
 * @param[in,out] DeviceExtension  Pointer to the sffp_sd device extension.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
static
NTSTATUS
SffpSdStartDevice(
    _In_ PSFFP_SD_EXTENSION DeviceExtension)
{
    NTSTATUS Status;
    SDBUS_INTERFACE_PARAMETERS InterfaceParams;
    BOOLEAN HighCapacity;
    ULONG BusCardType;

    /*
     * Open the SD bus interface on the PDO.
     */
    Status = SdBusOpenInterface(DeviceExtension->PhysicalDevice,
                                &DeviceExtension->BusInterface,
                                sizeof(SDBUS_INTERFACE_STANDARD),
                                SDBUS_INTERFACE_VERSION);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffpSdStartDevice: SdBusOpenInterface failed (0x%08lx)\n", Status);
        return Status;
    }

    DeviceExtension->InterfaceOpen = TRUE;

    /*
     * Initialize the interface parameters.
     * The function protocol driver sets the target block size to 512 bytes
     * and does not process card interrupts.
     */
    RtlZeroMemory(&InterfaceParams, sizeof(InterfaceParams));
    InterfaceParams.Size = sizeof(SDBUS_INTERFACE_PARAMETERS);
    InterfaceParams.SdioFlags = 0;
    InterfaceParams.TargetObject = DeviceExtension->LowerDevice;
    InterfaceParams.DeviceGeneratesInterrupts = FALSE;
    InterfaceParams.CallbackAtDpcLevel = FALSE;
    InterfaceParams.CallbackRoutine = NULL;
    InterfaceParams.CallbackRoutineContext = NULL;

    Status = DeviceExtension->BusInterface.InitializeInterface(
                 DeviceExtension->BusInterface.Context,
                 &InterfaceParams);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffpSdStartDevice: InitializeInterface failed (0x%08lx)\n", Status);
        goto CleanupInterface;
    }

    /*
     * Query the function type so we know what kind of card we are driving.
     */
    DeviceExtension->FunctionType = SDBUS_FUNCTION_TYPE_UNKNOWN;
    Status = SffpSdGetProperty(DeviceExtension,
                               SDP_FUNCTION_TYPE,
                               &DeviceExtension->FunctionType,
                               sizeof(DeviceExtension->FunctionType));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffpSdStartDevice: Failed to get function type (0x%08lx)\n", Status);
        goto CleanupInterface;
    }

    DeviceExtension->CardType = SdCardTypeUnknown;
    BusCardType = SdCardTypeUnknown;
    Status = SffpSdGetProperty(DeviceExtension,
                               SDP_ROS_CARD_TYPE,
                               &BusCardType,
                               sizeof(BusCardType));
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->CardType = (SD_CARD_TYPE)BusCardType;
    }

    /*
     * Query high-capacity support to determine addressing mode.
     * SDHC/SDXC and eMMC use block addressing; SDSC uses byte addressing.
     */
    HighCapacity = FALSE;
    Status = SffpSdGetProperty(DeviceExtension,
                               SDP_HIGH_CAPACITY_SUPPORTED,
                               &HighCapacity,
                               sizeof(HighCapacity));
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->HighCapacity = HighCapacity;
    }
    else
    {
        /* Default to byte addressing if the query fails */
        DeviceExtension->HighCapacity = FALSE;
    }

    /*
     * eMMC always uses block addressing regardless of what the bus reports.
     */
    if (DeviceExtension->CardType == SdCardTypeEmmc)
    {
        DeviceExtension->HighCapacity = TRUE;
    }

    DeviceExtension->DevicePowerState = PowerDeviceD0;

    DPRINT("SffpSdStartDevice: FunctionType %d, HighCapacity %s\n",
           DeviceExtension->FunctionType,
           DeviceExtension->HighCapacity ? "Yes" : "No");

    return STATUS_SUCCESS;

CleanupInterface:
    DeviceExtension->BusInterface.InterfaceDereference(
        DeviceExtension->BusInterface.Context);
    DeviceExtension->InterfaceOpen = FALSE;
    return Status;
}

/**
 * @brief Release the SD bus interface and clean up device resources.
 *
 * @param[in,out] DeviceExtension  Pointer to the sffp_sd device extension.
 */
static
VOID
SffpSdCleanupDevice(
    _In_ PSFFP_SD_EXTENSION DeviceExtension)
{
    if (DeviceExtension->InterfaceOpen)
    {
        DeviceExtension->BusInterface.InterfaceDereference(
            DeviceExtension->BusInterface.Context);
        DeviceExtension->InterfaceOpen = FALSE;
    }
}

/* COMMAND BUILDER FUNCTIONS **************************************************/

/**
 * @brief Build an SD read command descriptor and argument.
 *
 * Uses CMD17 (READ_SINGLE_BLOCK) for single-sector reads and
 * CMD18 (READ_MULTIPLE_BLOCK) for multi-sector reads.
 *
 * @param[out] CmdDesc        Receives the filled-in command descriptor.
 * @param[out] Argument       Receives the command argument (address).
 * @param[in]  SectorAddress  Starting sector number.
 * @param[in]  SectorCount    Number of sectors to read.
 * @param[in]  HighCapacity   TRUE for block addressing, FALSE for byte addressing.
 */
VOID
SffpSdBuildReadCommand(
    _Out_ PSDCMD_DESCRIPTOR CmdDesc,
    _Out_ PULONG Argument,
    _In_ ULONGLONG SectorAddress,
    _In_ ULONG SectorCount,
    _In_ BOOLEAN HighCapacity)
{
    RtlZeroMemory(CmdDesc, sizeof(SDCMD_DESCRIPTOR));

    if (SectorCount == 1)
    {
        CmdDesc->Cmd = SDCMD_READ_SINGLE_BLOCK;
        CmdDesc->TransferType = SDTT_SINGLE_BLOCK;
    }
    else
    {
        CmdDesc->Cmd = SDCMD_READ_MULTIPLE_BLOCK;
        CmdDesc->TransferType = SDTT_MULTI_BLOCK;
    }

    CmdDesc->CmdClass = SDCC_STANDARD;
    CmdDesc->TransferDirection = SDTD_READ;
    CmdDesc->ResponseType = SDRT_1;

    /*
     * SDHC/SDXC/eMMC: argument is the sector number (block address).
     * SDSC: argument is the byte address (sector * 512).
     */
    if (HighCapacity)
    {
        *Argument = (ULONG)SectorAddress;
    }
    else
    {
        *Argument = (ULONG)(SectorAddress * SD_SECTOR_SIZE);
    }
}

/**
 * @brief Build an SD write command descriptor and argument.
 *
 * Uses CMD24 (WRITE_BLOCK) for single-sector writes and
 * CMD25 (WRITE_MULTIPLE_BLOCK) for multi-sector writes.
 *
 * @param[out] CmdDesc        Receives the filled-in command descriptor.
 * @param[out] Argument       Receives the command argument (address).
 * @param[in]  SectorAddress  Starting sector number.
 * @param[in]  SectorCount    Number of sectors to write.
 * @param[in]  HighCapacity   TRUE for block addressing, FALSE for byte addressing.
 */
VOID
SffpSdBuildWriteCommand(
    _Out_ PSDCMD_DESCRIPTOR CmdDesc,
    _Out_ PULONG Argument,
    _In_ ULONGLONG SectorAddress,
    _In_ ULONG SectorCount,
    _In_ BOOLEAN HighCapacity)
{
    RtlZeroMemory(CmdDesc, sizeof(SDCMD_DESCRIPTOR));

    if (SectorCount == 1)
    {
        CmdDesc->Cmd = SDCMD_WRITE_BLOCK;
        CmdDesc->TransferType = SDTT_SINGLE_BLOCK;
    }
    else
    {
        CmdDesc->Cmd = SDCMD_WRITE_MULTIPLE_BLOCK;
        CmdDesc->TransferType = SDTT_MULTI_BLOCK;
    }

    CmdDesc->CmdClass = SDCC_STANDARD;
    CmdDesc->TransferDirection = SDTD_WRITE;
    CmdDesc->ResponseType = SDRT_1;

    if (HighCapacity)
    {
        *Argument = (ULONG)SectorAddress;
    }
    else
    {
        *Argument = (ULONG)(SectorAddress * SD_SECTOR_SIZE);
    }
}

static
NTSTATUS
SffpSdHandleQueryProtocol(
    _In_ PSFFP_SD_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PSFFDISK_QUERY_DEVICE_PROTOCOL_DATA Protocol;
    static const GUID SdProtocolGuid = GUID_SFF_PROTOCOL_SD;
    static const GUID MmcProtocolGuid = GUID_SFF_PROTOCOL_MMC;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Protocol))
    {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Protocol = (PSFFDISK_QUERY_DEVICE_PROTOCOL_DATA)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Protocol, sizeof(*Protocol));
    Protocol->Size = sizeof(*Protocol);

    switch (DeviceExtension->CardType)
    {
        case SdCardTypeSdV1:
        case SdCardTypeSdV2:
        case SdCardTypeSdhc:
        case SdCardTypeSdxc:
        case SdCardTypeCombo:
            Protocol->ProtocolGUID = SdProtocolGuid;
            break;
        case SdCardTypeMmc:
        case SdCardTypeEmmc:
            Protocol->ProtocolGUID = MmcProtocolGuid;
            break;
        default:
            Irp->IoStatus.Information = 0;
            return STATUS_INVALID_DEVICE_REQUEST;
    }

    Irp->IoStatus.Information = sizeof(*Protocol);
    return STATUS_SUCCESS;
}

static
NTSTATUS
SffpSdHandleDeviceCommand(
    _In_ PSFFP_SD_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PSFFDISK_DEVICE_COMMAND_DATA Data;
    PSDCMD_DESCRIPTOR CmdDesc;
    SDBUS_REQUEST_PACKET Packet;
    PMDL Mdl;
    PUCHAR DataBuf;
    NTSTATUS Status;
    ULONG InLength, OutLength, TotalLength, HeaderSize, BufferOffset;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    InLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    OutLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    TotalLength = (InLength > OutLength) ? InLength : OutLength;

    if (!DeviceExtension->InterfaceOpen)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_DEVICE_NOT_READY;
    }

    if (InLength < FIELD_OFFSET(SFFDISK_DEVICE_COMMAND_DATA, Data))
    {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Data = (PSFFDISK_DEVICE_COMMAND_DATA)Irp->AssociatedIrp.SystemBuffer;
    HeaderSize = Data->HeaderSize;
    if (HeaderSize < FIELD_OFFSET(SFFDISK_DEVICE_COMMAND_DATA, Data) ||
        HeaderSize > InLength ||
        (Data->Flags & ~SFFDISK_DEVCMD_VALID_FLAGS) != 0)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    switch (Data->Command)
    {
        case SFFDISK_DC_GET_VERSION:
            if (OutLength < HeaderSize)
            {
                Irp->IoStatus.Information = 0;
                return STATUS_BUFFER_TOO_SMALL;
            }
            Data->Information = SDBUS_DRIVER_VERSION_4;
            Irp->IoStatus.Information = HeaderSize;
            return STATUS_SUCCESS;

        case SFFDISK_DC_LOCK_CHANNEL:
        case SFFDISK_DC_UNLOCK_CHANNEL:
            Irp->IoStatus.Information = 0;
            return STATUS_SUCCESS;

        case SFFDISK_DC_DEVICE_COMMAND:
            if (Data->ProtocolArgumentSize < sizeof(SDCMD_DESCRIPTOR) ||
                Data->ProtocolArgumentSize > InLength - HeaderSize)
            {
                Irp->IoStatus.Information = 0;
                return STATUS_INVALID_PARAMETER;
            }

            BufferOffset = HeaderSize + Data->ProtocolArgumentSize;
            if (BufferOffset < HeaderSize)
            {
                Irp->IoStatus.Information = 0;
                return STATUS_INVALID_PARAMETER;
            }

            CmdDesc = (PSDCMD_DESCRIPTOR)(((PUCHAR)Data) + HeaderSize);

            Mdl = NULL;
            DataBuf = NULL;
            if (Data->DeviceDataBufferSize != 0)
            {
                if (BufferOffset > TotalLength ||
                    Data->DeviceDataBufferSize > TotalLength - BufferOffset)
                {
                    Irp->IoStatus.Information = 0;
                    return STATUS_INVALID_PARAMETER;
                }
                DataBuf = ((PUCHAR)Data) + BufferOffset;
                Mdl = IoAllocateMdl(DataBuf, Data->DeviceDataBufferSize, FALSE, FALSE, NULL);
                if (Mdl == NULL)
                {
                    Irp->IoStatus.Information = 0;
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                MmBuildMdlForNonPagedPool(Mdl);
            }

            SD_INIT_REQUEST_PACKET(&Packet, SDRF_DEVICE_COMMAND);
            if (Data->Flags & SFFDISK_DEVCMD_FLAG_APPEND_CMD_SEQ)
            {
                Packet.Flags |= SDRP_FLAG_APPEND_CMD_SEQ;
            }
            if (Data->Flags & SFFDISK_DEVCMD_FLAG_LONG_OPERATION)
            {
                Packet.Flags |= SDRP_FLAG_WAIT_FOR_BUSY;
            }
            Packet.Parameters.DeviceCommand.CmdDesc = *CmdDesc;
            Packet.Parameters.DeviceCommand.Argument = (ULONG)Data->Information;
            Packet.Parameters.DeviceCommand.Mdl = Mdl;
            Packet.Parameters.DeviceCommand.Length = Data->DeviceDataBufferSize;

            Status = SdBusSubmitRequest(DeviceExtension->BusInterface.Context, &Packet);

            Data->Information = Packet.Information;

            if (Mdl != NULL)
            {
                IoFreeMdl(Mdl);
            }

            Irp->IoStatus.Information = BufferOffset + Data->DeviceDataBufferSize;
            return Status;

        default:
            Irp->IoStatus.Information = 0;
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

/* DISPATCH FUNCTIONS *********************************************************/

/**
 * @brief Create the FDO for the SD function protocol driver and attach it
 *        to the device stack.
 *
 * @param[in] DriverObject          Pointer to the driver object.
 * @param[in] PhysicalDeviceObject  Pointer to the PDO from the bus driver.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
NTAPI
SffpSdAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    PSFFP_SD_EXTENSION DeviceExtension;

    PAGED_CODE();

    Status = IoCreateDevice(DriverObject,
                            sizeof(SFFP_SD_EXTENSION),
                            NULL,
                            FILE_DEVICE_DISK,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffpSdAddDevice: IoCreateDevice failed (0x%08lx)\n", Status);
        return Status;
    }

    DeviceExtension = (PSFFP_SD_EXTENSION)DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(SFFP_SD_EXTENSION));

    DeviceExtension->Self = DeviceObject;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;

    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, TAG_SFFP_SD, 0, 0);

    DeviceExtension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject,
                                                               PhysicalDeviceObject);
    if (DeviceExtension->LowerDevice == NULL)
    {
        DPRINT1("SffpSdAddDevice: IoAttachDeviceToDeviceStack failed\n");
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= DO_DIRECT_IO;
    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    DeviceObject->AlignmentRequirement =
        DeviceExtension->LowerDevice->AlignmentRequirement;

    DeviceExtension->DevicePowerState = PowerDeviceD0;

    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MJ_PNP requests for the SD function protocol device.
 *
 * Processes start, stop, remove, surprise removal, query capabilities,
 * and other PnP minor functions.
 *
 * @param[in]     DeviceObject  Pointer to the device object.
 * @param[in,out] Irp           Pointer to the PnP I/O request packet.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
NTAPI
SffpSdPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFP_SD_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    PAGED_CODE();

    DeviceExtension = (PSFFP_SD_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            /* Forward the start IRP down first */
            Status = SffpSdForwardIrpSynchronous(DeviceObject, Irp);
            if (NT_SUCCESS(Status))
            {
                Status = SffpSdStartDevice(DeviceExtension);
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return Status;
        }

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return Status;
        }

        case IRP_MN_STOP_DEVICE:
        {
            SffpSdCleanupDevice(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return Status;
        }

        case IRP_MN_SURPRISE_REMOVAL:
        {
            SffpSdCleanupDevice(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return Status;
        }

        case IRP_MN_REMOVE_DEVICE:
        {
            SffpSdCleanupDevice(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);

            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);

            IoDetachDevice(DeviceExtension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;
        }

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities;

            Status = SffpSdForwardIrpSynchronous(DeviceObject, Irp);
            if (NT_SUCCESS(Status))
            {
                Capabilities = IrpSp->Parameters.DeviceCapabilities.Capabilities;

                /* SD cards are removable; eMMC is not */
                if (DeviceExtension->CardType == SdCardTypeEmmc)
                {
                    Capabilities->Removable = FALSE;
                    Capabilities->EjectSupported = FALSE;
                    Capabilities->SurpriseRemovalOK = FALSE;
                }
                else
                {
                    Capabilities->Removable = TRUE;
                    Capabilities->EjectSupported = TRUE;
                    Capabilities->SurpriseRemovalOK = TRUE;
                }
                Capabilities->UniqueID = FALSE;
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return Status;
        }

        default:
        {
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return Status;
        }
    }
}

/**
 * @brief Handle IRP_MJ_POWER by passing all power IRPs down the stack.
 *
 * @param[in]     DeviceObject  Pointer to the device object.
 * @param[in,out] Irp           Pointer to the power I/O request packet.
 *
 * @return NTSTATUS from the lower driver.
 */
NTSTATUS
NTAPI
SffpSdPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFP_SD_EXTENSION DeviceExtension;
    NTSTATUS Status;

    DeviceExtension = (PSFFP_SD_EXTENSION)DeviceObject->DeviceExtension;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(DeviceExtension->LowerDevice, Irp);

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return Status;
}

/**
 * @brief Handle IRP_MJ_CREATE and IRP_MJ_CLOSE.
 *
 * Always succeeds immediately.
 *
 * @param[in]     DeviceObject  Pointer to the device object (unused).
 * @param[in,out] Irp           Pointer to the I/O request packet.
 *
 * @return STATUS_SUCCESS.
 */
NTSTATUS
NTAPI
SffpSdCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MJ_DEVICE_CONTROL by passing all IOCTLs down to the
 *        next lower driver.
 *
 * The class driver above (sffdisk.sys) handles the actual IOCTL semantics.
 *
 * @param[in]     DeviceObject  Pointer to the device object.
 * @param[in,out] Irp           Pointer to the I/O request packet.
 *
 * @return NTSTATUS from the lower driver.
 */
NTSTATUS
NTAPI
SffpSdDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFP_SD_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    DeviceExtension = (PSFFP_SD_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_SFFDISK_QUERY_DEVICE_PROTOCOL:
            Status = SffpSdHandleQueryProtocol(DeviceExtension, Irp);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IOCTL_SFFDISK_DEVICE_COMMAND:
            Status = SffpSdHandleDeviceCommand(DeviceExtension, Irp);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->LowerDevice, Irp);
    }
}

/**
 * @brief Handle IRP_MJ_INTERNAL_DEVICE_CONTROL.
 *
 * This is the entry point for block I/O requests from the class driver.
 * In the current architecture where the class driver calls SdBusSubmitRequest
 * directly, this function simply passes the IRP down the stack.
 *
 * @param[in]     DeviceObject  Pointer to the device object.
 * @param[in,out] Irp           Pointer to the I/O request packet.
 *
 * @return NTSTATUS from the lower driver.
 */
NTSTATUS
NTAPI
SffpSdInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFP_SD_EXTENSION DeviceExtension;

    DeviceExtension = (PSFFP_SD_EXTENSION)DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

/* DRIVER ENTRY ***************************************************************/

/**
 * @brief Entry point for sffp_sd.sys.
 *
 * Registers all IRP dispatch routines and the AddDevice callback.
 *
 * @param[in] DriverObject  Pointer to the driver object.
 * @param[in] RegistryPath  Path to the driver's service registry key.
 *
 * @return STATUS_SUCCESS.
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_PNP] = SffpSdPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = SffpSdPower;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = SffpSdCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SffpSdCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SffpSdDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = SffpSdInternalDeviceControl;
    DriverObject->DriverExtension->AddDevice = SffpSdAddDevice;

    return STATUS_SUCCESS;
}
