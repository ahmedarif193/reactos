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

static
NTSTATUS
SffpSdAdjustDeviceUsage(
    _Inout_ PSFFP_SD_EXTENSION DeviceExtension,
    _In_ DEVICE_USAGE_NOTIFICATION_TYPE Type,
    _In_ BOOLEAN InPath)
{
    PULONG Counter;
    BOOLEAN AffectsPowerPageability;
    KIRQL OldIrql;
    NTSTATUS Status;

    switch (Type)
    {
        case DeviceUsageTypePaging:
            Counter = &DeviceExtension->PagingPathCount;
            AffectsPowerPageability = TRUE;
            break;

        case DeviceUsageTypeHibernation:
            Counter = &DeviceExtension->HibernationPathCount;
            AffectsPowerPageability = TRUE;
            break;

        case DeviceUsageTypeDumpFile:
            Counter = &DeviceExtension->DumpPathCount;
            AffectsPowerPageability = TRUE;
            break;

        case DeviceUsageTypeBoot:
            Counter = &DeviceExtension->BootPathCount;
            AffectsPowerPageability = FALSE;
            break;

        case DeviceUsageTypePostDisplay:
            Counter = &DeviceExtension->PostDisplayPathCount;
            AffectsPowerPageability = FALSE;
            break;

        case DeviceUsageTypeGuestAssigned:
            Counter = &DeviceExtension->GuestAssignedPathCount;
            AffectsPowerPageability = FALSE;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    Status = STATUS_SUCCESS;
    KeAcquireSpinLock(&DeviceExtension->UsageLock, &OldIrql);
    if (InPath)
    {
        if (*Counter == MAXULONG)
        {
            Status = STATUS_INTEGER_OVERFLOW;
        }
        else
        {
            (*Counter)++;
        }
    }
    else if (*Counter == 0)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    else
    {
        (*Counter)--;
    }

    if (NT_SUCCESS(Status) && AffectsPowerPageability)
    {
        if (DeviceExtension->PagingPathCount != 0 ||
            DeviceExtension->HibernationPathCount != 0 ||
            DeviceExtension->DumpPathCount != 0)
        {
            DeviceExtension->Self->Flags &= ~DO_POWER_PAGABLE;
        }
        else if (DeviceExtension->PowerPagable &&
                 !(DeviceExtension->Self->Flags & DO_POWER_INRUSH))
        {
            DeviceExtension->Self->Flags |= DO_POWER_PAGABLE;
        }
    }
    KeReleaseSpinLock(&DeviceExtension->UsageLock, OldIrql);

    return Status;
}

static
BOOLEAN
SffpSdHasActiveDeviceUsage(
    _Inout_ PSFFP_SD_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;
    BOOLEAN Active;

    KeAcquireSpinLock(&DeviceExtension->UsageLock, &OldIrql);
    Active = DeviceExtension->PagingPathCount != 0 ||
             DeviceExtension->HibernationPathCount != 0 ||
             DeviceExtension->DumpPathCount != 0 ||
             DeviceExtension->BootPathCount != 0 ||
             DeviceExtension->PostDisplayPathCount != 0 ||
             DeviceExtension->GuestAssignedPathCount != 0;
    KeReleaseSpinLock(&DeviceExtension->UsageLock, OldIrql);

    return Active;
}

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

static
NTSTATUS
NTAPI
SffpSdReleaseRemoveLockCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PSFFP_SD_EXTENSION DeviceExtension;

    UNREFERENCED_PARAMETER(DeviceObject);

    DeviceExtension = (PSFFP_SD_EXTENSION)Context;
    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

    return STATUS_CONTINUE_COMPLETION;
}

static NTSTATUS
SffpSdForwardIrpWithRemoveLock(
    _In_ PSFFP_SD_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ BOOLEAN PowerIrp)
{
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SffpSdReleaseRemoveLockCompletion,
                           DeviceExtension,
                           TRUE,
                           TRUE,
                           TRUE);

    if (PowerIrp)
    {
        return PoCallDriver(DeviceExtension->LowerDevice, Irp);
    }
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
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
    }

    Status = Irp->IoStatus.Status;

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

static
NTSTATUS
SffpSdOpenBusInterface(
    _Inout_ PSFFP_SD_EXTENSION DeviceExtension)
{
    SDBUS_INTERFACE_PARAMETERS InterfaceParams;
    KIRQL OldIrql;
    BOOLEAN InterfaceOpen;
    NTSTATUS Status;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    InterfaceOpen = DeviceExtension->InterfaceOpen;
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

    if (InterfaceOpen)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&DeviceExtension->BusInterface,
                  sizeof(DeviceExtension->BusInterface));

    Status = SdBusOpenInterface(DeviceExtension->PhysicalDevice,
                                &DeviceExtension->BusInterface,
                                sizeof(SDBUS_INTERFACE_STANDARD),
                                SDBUS_INTERFACE_VERSION);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffpSdOpenBusInterface: SdBusOpenInterface failed (0x%08lx)\n",
                Status);
        return Status;
    }

    RtlZeroMemory(&InterfaceParams, sizeof(InterfaceParams));
    InterfaceParams.Size = sizeof(SDBUS_INTERFACE_PARAMETERS);
    InterfaceParams.TargetObject = DeviceExtension->LowerDevice;

    Status = DeviceExtension->BusInterface.InitializeInterface(
                 DeviceExtension->BusInterface.Context,
                 &InterfaceParams);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffpSdOpenBusInterface: InitializeInterface failed (0x%08lx)\n",
                Status);
        DeviceExtension->BusInterface.InterfaceDereference(
            DeviceExtension->BusInterface.Context);
        RtlZeroMemory(&DeviceExtension->BusInterface,
                      sizeof(DeviceExtension->BusInterface));
        return Status;
    }

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    DeviceExtension->InterfaceOpen = TRUE;
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

    return STATUS_SUCCESS;
}

static
VOID
SffpSdCloseBusInterface(
    _Inout_ PSFFP_SD_EXTENSION DeviceExtension)
{
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PVOID InterfaceContext;
    KIRQL OldIrql;

    InterfaceDereference = NULL;
    InterfaceContext = NULL;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    if (DeviceExtension->InterfaceOpen)
    {
        InterfaceDereference = DeviceExtension->BusInterface.InterfaceDereference;
        InterfaceContext = DeviceExtension->BusInterface.Context;
        DeviceExtension->InterfaceOpen = FALSE;
    }
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

    if (InterfaceDereference != NULL)
    {
        InterfaceDereference(InterfaceContext);
        RtlZeroMemory(&DeviceExtension->BusInterface,
                      sizeof(DeviceExtension->BusInterface));
    }
}

static
BOOLEAN
SffpSdAcquireBusRequest(
    _In_ PSFFP_SD_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;
    BOOLEAN Acquired;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    Acquired = !DeviceExtension->BusRequestsBlocked &&
               DeviceExtension->InterfaceOpen &&
               DeviceExtension->OutstandingBusRequests != MAXULONG;
    if (Acquired)
    {
        if (DeviceExtension->OutstandingBusRequests++ == 0)
        {
            KeClearEvent(&DeviceExtension->BusRequestsDrained);
        }
    }
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

    return Acquired;
}

static
VOID
SffpSdReleaseBusRequest(
    _In_ PSFFP_SD_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    ASSERT(DeviceExtension->OutstandingBusRequests != 0);
    if (--DeviceExtension->OutstandingBusRequests == 0)
    {
        KeSetEvent(&DeviceExtension->BusRequestsDrained,
                   IO_NO_INCREMENT,
                   FALSE);
    }
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
}

static
VOID
SffpSdBlockBusRequests(
    _Inout_ PSFFP_SD_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;
    BOOLEAN WaitForDrain;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    DeviceExtension->BusRequestsBlocked = TRUE;
    WaitForDrain = (DeviceExtension->OutstandingBusRequests != 0);
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

    if (WaitForDrain)
    {
        KeWaitForSingleObject(&DeviceExtension->BusRequestsDrained,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }
}

static
VOID
SffpSdUnblockBusRequests(
    _Inout_ PSFFP_SD_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    ASSERT(DeviceExtension->InterfaceOpen);
    DeviceExtension->BusRequestsBlocked = FALSE;
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
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
    BOOLEAN HighCapacity;
    ULONG BusCardType;

    SffpSdBlockBusRequests(DeviceExtension);
    DeviceExtension->FunctionType = SDBUS_FUNCTION_TYPE_UNKNOWN;
    DeviceExtension->CardType = SdCardTypeUnknown;

    Status = SffpSdOpenBusInterface(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        return Status;
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
    if (!IsTypeMemory(DeviceExtension->FunctionType))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto CleanupInterface;
    }

    DeviceExtension->CardType = SdCardTypeUnknown;
    BusCardType = SdCardTypeUnknown;
    Status = SffpSdGetProperty(DeviceExtension,
                               SDP_ROS_CARD_TYPE,
                               &BusCardType,
                               sizeof(BusCardType));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffpSdStartDevice: Failed to get card type (0x%08lx)\n",
                Status);
        goto CleanupInterface;
    }
    switch ((SD_CARD_TYPE)BusCardType)
    {
        case SdCardTypeSdV1:
        case SdCardTypeSdV2:
        case SdCardTypeSdhc:
        case SdCardTypeSdxc:
        case SdCardTypeCombo:
        case SdCardTypeMmc:
        case SdCardTypeEmmc:
            DeviceExtension->CardType = (SD_CARD_TYPE)BusCardType;
            break;

        default:
            Status = STATUS_DEVICE_DATA_ERROR;
            goto CleanupInterface;
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
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffpSdStartDevice: Failed to get high-capacity state (0x%08lx)\n",
                Status);
        goto CleanupInterface;
    }
    DeviceExtension->HighCapacity = HighCapacity;

    /*
     * eMMC always uses block addressing regardless of what the bus reports.
     */
    if ((DeviceExtension->CardType == SdCardTypeSdhc ||
         DeviceExtension->CardType == SdCardTypeSdxc ||
         DeviceExtension->CardType == SdCardTypeEmmc) &&
        !DeviceExtension->HighCapacity)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto CleanupInterface;
    }
    if ((DeviceExtension->CardType == SdCardTypeSdV1 ||
         DeviceExtension->CardType == SdCardTypeSdV2) &&
        DeviceExtension->HighCapacity)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto CleanupInterface;
    }

    DeviceExtension->DevicePowerState = PowerDeviceD0;
    SffpSdUnblockBusRequests(DeviceExtension);

    DPRINT("SffpSdStartDevice: FunctionType %d, HighCapacity %s\n",
           DeviceExtension->FunctionType,
           DeviceExtension->HighCapacity ? "Yes" : "No");

    return STATUS_SUCCESS;

CleanupInterface:
    SffpSdCloseBusInterface(DeviceExtension);
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
    SffpSdBlockBusRequests(DeviceExtension);
    SffpSdCloseBusInterface(DeviceExtension);
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
    BOOLEAN CommandOnly;
    ULONG InLength, OutLength, HeaderSize, BufferOffset, BufferEnd;
    ULONG BytesReturned;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    InLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    OutLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

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
            return STATUS_NOT_SUPPORTED;

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
            CommandOnly = (CmdDesc->TransferType == SDTT_CMD_ONLY);

            if (OutLength < HeaderSize)
            {
                Irp->IoStatus.Information = 0;
                return STATUS_BUFFER_TOO_SMALL;
            }

            if (Data->DeviceDataBufferSize > MAXULONG - BufferOffset)
            {
                Irp->IoStatus.Information = 0;
                return STATUS_INVALID_PARAMETER;
            }

            BufferEnd = BufferOffset + Data->DeviceDataBufferSize;
            if (CommandOnly)
            {
                if (BufferEnd > OutLength)
                {
                    Irp->IoStatus.Information = 0;
                    return STATUS_BUFFER_TOO_SMALL;
                }
            }
            else if (CmdDesc->TransferDirection == SDTD_READ)
            {
                if (BufferEnd > OutLength)
                {
                    Irp->IoStatus.Information = 0;
                    return STATUS_BUFFER_TOO_SMALL;
                }
            }
            else if (CmdDesc->TransferDirection == SDTD_WRITE)
            {
                if (BufferEnd > InLength)
                {
                    Irp->IoStatus.Information = 0;
                    return STATUS_INVALID_PARAMETER;
                }
            }
            else if (CmdDesc->TransferDirection != SDTD_UNSPECIFIED ||
                     Data->DeviceDataBufferSize != 0)
            {
                Irp->IoStatus.Information = 0;
                return STATUS_INVALID_PARAMETER;
            }

            Mdl = NULL;
            DataBuf = NULL;
            if (Data->DeviceDataBufferSize != 0 && !CommandOnly)
            {
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
            Packet.Parameters.DeviceCommand.Length = CommandOnly ? 0 : Data->DeviceDataBufferSize;

            Status = SdBusSubmitRequest(DeviceExtension->BusInterface.Context, &Packet);

            Data->Information = Packet.Information;

            BytesReturned = HeaderSize;
            if (NT_SUCCESS(Status) && CommandOnly)
            {
                if (Packet.ResponseLength > sizeof(Packet.ResponseData.AsUCHAR) ||
                    Packet.ResponseLength > Data->DeviceDataBufferSize)
                {
                    Status = STATUS_BUFFER_TOO_SMALL;
                }
                else
                {
                    DataBuf = ((PUCHAR)Data) + BufferOffset;
                    if (Packet.ResponseLength != 0)
                    {
                        RtlCopyMemory(DataBuf,
                                      Packet.ResponseData.AsUCHAR,
                                      Packet.ResponseLength);
                    }
                    BytesReturned = BufferOffset + Packet.ResponseLength;
                }
            }
            else if (NT_SUCCESS(Status) &&
                     CmdDesc->TransferDirection == SDTD_READ)
            {
                if (Packet.Information > Data->DeviceDataBufferSize)
                {
                    Status = STATUS_DEVICE_DATA_ERROR;
                }
                else
                {
                    BytesReturned = BufferOffset + (ULONG)Packet.Information;
                }
            }

            if (Mdl != NULL)
            {
                IoFreeMdl(Mdl);
            }

            Irp->IoStatus.Information = NT_SUCCESS(Status) ? BytesReturned : 0;
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
    KeInitializeSpinLock(&DeviceExtension->UsageLock);
    KeInitializeSpinLock(&DeviceExtension->BusRequestLock);
    KeInitializeEvent(&DeviceExtension->BusRequestsDrained,
                      NotificationEvent,
                      TRUE);
    DeviceExtension->BusRequestsBlocked = TRUE;

    DeviceExtension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject,
                                                               PhysicalDeviceObject);
    if (DeviceExtension->LowerDevice == NULL)
    {
        DPRINT1("SffpSdAddDevice: IoAttachDeviceToDeviceStack failed\n");
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= DO_DIRECT_IO;
    if (DeviceExtension->LowerDevice->Flags & DO_POWER_INRUSH)
    {
        DeviceObject->Flags |= DO_POWER_INRUSH;
    }
    else if (DeviceExtension->LowerDevice->Flags & DO_POWER_PAGABLE)
    {
        DeviceObject->Flags |= DO_POWER_PAGABLE;
    }
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    DeviceExtension->PowerPagable =
        (DeviceObject->Flags & DO_POWER_PAGABLE) != 0;

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
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return SffpSdForwardIrpWithRemoveLock(DeviceExtension, Irp, FALSE);
        }

        case IRP_MN_QUERY_REMOVE_DEVICE:
        {
            NTSTATUS RestoreStatus;

            SffpSdCleanupDevice(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            Status = SffpSdForwardIrpSynchronous(DeviceObject, Irp);
            if (!NT_SUCCESS(Status))
            {
                RestoreStatus = SffpSdOpenBusInterface(DeviceExtension);
                if (NT_SUCCESS(RestoreStatus))
                {
                    SffpSdUnblockBusRequests(DeviceExtension);
                }
                else
                {
                    DPRINT1("SffpSdPnp: failed to reopen interface after rejected query-remove (0x%08lx)\n",
                            RestoreStatus);
                }
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_CANCEL_REMOVE_DEVICE:
        {
            NTSTATUS RestoreStatus;

            Irp->IoStatus.Status = STATUS_SUCCESS;
            Status = SffpSdForwardIrpSynchronous(DeviceObject, Irp);
            if (NT_SUCCESS(Status))
            {
                RestoreStatus = SffpSdOpenBusInterface(DeviceExtension);
                if (NT_SUCCESS(RestoreStatus))
                {
                    SffpSdUnblockBusRequests(DeviceExtension);
                }
                else
                {
                    Status = RestoreStatus;
                }
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_STOP_DEVICE:
        {
            SffpSdCleanupDevice(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            return SffpSdForwardIrpWithRemoveLock(DeviceExtension, Irp, FALSE);
        }

        case IRP_MN_SURPRISE_REMOVAL:
        {
            SffpSdCleanupDevice(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            return SffpSdForwardIrpWithRemoveLock(DeviceExtension, Irp, FALSE);
        }

        case IRP_MN_REMOVE_DEVICE:
        {
            PDEVICE_OBJECT LowerDevice = DeviceExtension->LowerDevice;

            SffpSdCleanupDevice(DeviceExtension);

            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(LowerDevice, Irp);

            IoDetachDevice(LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;
        }

        case IRP_MN_QUERY_CAPABILITIES:
        {
            /* The bus PDO owns slot wiring and stable-ID policy. */
            Status = SffpSdForwardIrpSynchronous(DeviceObject, Irp);

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
        {
            DEVICE_USAGE_NOTIFICATION_TYPE Type;
            BOOLEAN InPath;

            Type = IrpSp->Parameters.UsageNotification.Type;
            InPath = IrpSp->Parameters.UsageNotification.InPath;
            Status = SffpSdAdjustDeviceUsage(DeviceExtension, Type, InPath);
            if (NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = STATUS_SUCCESS;
                Status = SffpSdForwardIrpSynchronous(DeviceObject, Irp);
                if (!NT_SUCCESS(Status))
                {
                    (VOID)SffpSdAdjustDeviceUsage(DeviceExtension,
                                                  Type,
                                                  !InPath);
                }
                else
                {
                    IoInvalidateDeviceState(DeviceExtension->PhysicalDevice);
                }
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
        {
            Status = SffpSdForwardIrpSynchronous(DeviceObject, Irp);
            if (NT_SUCCESS(Status) &&
                SffpSdHasActiveDeviceUsage(DeviceExtension))
            {
                Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        default:
        {
            return SffpSdForwardIrpWithRemoveLock(DeviceExtension, Irp, FALSE);
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
    return SffpSdForwardIrpWithRemoveLock(DeviceExtension, Irp, TRUE);
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
    PSFFP_SD_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    DeviceExtension = (PSFFP_SD_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Status = STATUS_SUCCESS;

    if (IrpSp->MajorFunction == IRP_MJ_CREATE)
    {
        Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
        if (NT_SUCCESS(Status))
        {
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
        }
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
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

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_SFFDISK_QUERY_DEVICE_PROTOCOL:
            Status = SffpSdHandleQueryProtocol(DeviceExtension, Irp);
            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IOCTL_SFFDISK_DEVICE_COMMAND:
            if (!SffpSdAcquireBusRequest(DeviceExtension))
            {
                Status = STATUS_DEVICE_NOT_READY;
                Irp->IoStatus.Information = 0;
            }
            else
            {
                Status = SffpSdHandleDeviceCommand(DeviceExtension, Irp);
                SffpSdReleaseBusRequest(DeviceExtension);
            }
            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        default:
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp,
                                   SffpSdReleaseRemoveLockCompletion,
                                   DeviceExtension,
                                   TRUE,
                                   TRUE,
                                   TRUE);
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
    NTSTATUS Status;

    DeviceExtension = (PSFFP_SD_EXTENSION)DeviceObject->DeviceExtension;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SffpSdReleaseRemoveLockCompletion,
                           DeviceExtension,
                           TRUE,
                           TRUE,
                           TRUE);
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
