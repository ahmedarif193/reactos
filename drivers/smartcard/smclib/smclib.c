/*
 * PROJECT:     LiberNT Smart Card Driver Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Initialization, remove lock and device control dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "smcpriv.h"

#define NDEBUG
#include <debug.h>

static ULONG SmcDebugLevel;

ULONG
NTAPI
SmartcardGetDebugLevel(VOID)
{
    return SmcDebugLevel;
}

VOID
NTAPI
SmartcardSetDebugLevel(
    ULONG Level)
{
    SmcDebugLevel = Level;
}

NTSTATUS
NTAPI
SmartcardInitialize(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    POS_DEP_DATA OsData;

    if (SmartcardExtension == NULL)
        return STATUS_INVALID_PARAMETER;

    if (SmartcardExtension->Version > SMCLIB_VERSION ||
        SmartcardExtension->Version < SMCLIB_VERSION_REQUIRED)
    {
        return STATUS_UNSUCCESSFUL;
    }

    if (SmartcardExtension->SmartcardRequest.BufferSize < MIN_BUFFER_SIZE)
        SmartcardExtension->SmartcardRequest.BufferSize = MIN_BUFFER_SIZE;
    if (SmartcardExtension->SmartcardReply.BufferSize < MIN_BUFFER_SIZE)
        SmartcardExtension->SmartcardReply.BufferSize = MIN_BUFFER_SIZE;

    SmartcardExtension->SmartcardRequest.Buffer =
        ExAllocatePoolWithTag(NonPagedPool, SmartcardExtension->SmartcardRequest.BufferSize, SMCLIB_TAG);
    SmartcardExtension->SmartcardReply.Buffer =
        ExAllocatePoolWithTag(NonPagedPool, SmartcardExtension->SmartcardReply.BufferSize, SMCLIB_TAG);
    SmartcardExtension->T1.ReplyData =
        ExAllocatePoolWithTag(NonPagedPool, SmartcardExtension->SmartcardReply.BufferSize, SMCLIB_TAG);
    OsData = ExAllocatePoolZero(NonPagedPool, sizeof(*OsData), SMCLIB_TAG);
    SmartcardExtension->OsData = OsData;

    if (SmartcardExtension->SmartcardRequest.Buffer == NULL ||
        SmartcardExtension->SmartcardReply.Buffer == NULL ||
        SmartcardExtension->T1.ReplyData == NULL ||
        OsData == NULL)
    {
        SmartcardExit(SmartcardExtension);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeMutex(&OsData->Mutex, 0);
    KeInitializeSpinLock(&OsData->SpinLock);
    OsData->RemoveLock.Removed = FALSE;
    OsData->RemoveLock.RefCount = 1;
    KeInitializeEvent(&OsData->RemoveLock.RemoveEvent, SynchronizationEvent, FALSE);
    InitializeListHead(&OsData->RemoveLock.TagList);

    SmartcardExtension->CardCapabilities.ClockRateConversion = SmcClockRateConversion;
    SmartcardExtension->CardCapabilities.BitRateAdjustment = SmcBitRateAdjustment;
    return STATUS_SUCCESS;
}

VOID
NTAPI
SmartcardExit(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    if (SmartcardExtension->SmartcardRequest.Buffer != NULL)
    {
        ExFreePoolWithTag(SmartcardExtension->SmartcardRequest.Buffer, SMCLIB_TAG);
        SmartcardExtension->SmartcardRequest.Buffer = NULL;
    }

    if (SmartcardExtension->SmartcardReply.Buffer != NULL)
    {
        ExFreePoolWithTag(SmartcardExtension->SmartcardReply.Buffer, SMCLIB_TAG);
        SmartcardExtension->SmartcardReply.Buffer = NULL;
    }

    if (SmartcardExtension->T1.ReplyData != NULL)
    {
        ExFreePoolWithTag(SmartcardExtension->T1.ReplyData, SMCLIB_TAG);
        SmartcardExtension->T1.ReplyData = NULL;
    }

    if (SmartcardExtension->OsData != NULL)
    {
        ExFreePoolWithTag(SmartcardExtension->OsData, SMCLIB_TAG);
        SmartcardExtension->OsData = NULL;
    }
}

NTSTATUS
NTAPI
SmartcardAcquireRemoveLock(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    POS_DEP_DATA OsData = SmartcardExtension->OsData;

    InterlockedIncrement(&OsData->RemoveLock.RefCount);
    if (OsData->RemoveLock.Removed)
    {
        if (InterlockedDecrement(&OsData->RemoveLock.RefCount) == 0)
            KeSetEvent(&OsData->RemoveLock.RemoveEvent, IO_NO_INCREMENT, FALSE);
        return STATUS_DELETE_PENDING;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SmartcardAcquireRemoveLockWithTag(
    PSMARTCARD_EXTENSION SmartcardExtension,
    ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);

    return SmartcardAcquireRemoveLock(SmartcardExtension);
}

VOID
NTAPI
SmartcardReleaseRemoveLock(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    POS_DEP_DATA OsData = SmartcardExtension->OsData;

    if (InterlockedDecrement(&OsData->RemoveLock.RefCount) == 0)
        KeSetEvent(&OsData->RemoveLock.RemoveEvent, IO_NO_INCREMENT, FALSE);
}

VOID
NTAPI
SmartcardReleaseRemoveLockWithTag(
    PSMARTCARD_EXTENSION SmartcardExtension,
    ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);

    SmartcardReleaseRemoveLock(SmartcardExtension);
}

VOID
NTAPI
SmartcardReleaseRemoveLockAndWait(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    POS_DEP_DATA OsData = SmartcardExtension->OsData;

    PAGED_CODE();

    OsData->RemoveLock.Removed = TRUE;
    InterlockedDecrement(&OsData->RemoveLock.RefCount);
    if (InterlockedDecrement(&OsData->RemoveLock.RefCount) > 0)
    {
        KeWaitForSingleObject(&OsData->RemoveLock.RemoveEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }
}

NTSTATUS
NTAPI
SmartcardCreateLink(
    PUNICODE_STRING LinkName,
    PUNICODE_STRING DeviceName)
{
    WCHAR Buffer[32];
    UNICODE_STRING Name;
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();

    if (LinkName == NULL || DeviceName == NULL)
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < 100; Index++)
    {
        Status = RtlStringCbPrintfW(Buffer, sizeof(Buffer), L"\\DosDevices\\SCReader%lu", Index);
        if (!NT_SUCCESS(Status))
            return Status;

        RtlInitUnicodeString(&Name, Buffer);
        Status = IoCreateSymbolicLink(&Name, DeviceName);
        if (NT_SUCCESS(Status))
        {
            LinkName->MaximumLength = Name.Length + sizeof(WCHAR);
            LinkName->Buffer = ExAllocatePoolWithTag(PagedPool, LinkName->MaximumLength, SMCLIB_TAG);
            if (LinkName->Buffer == NULL)
            {
                IoDeleteSymbolicLink(&Name);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlCopyMemory(LinkName->Buffer, Name.Buffer, Name.Length);
            LinkName->Buffer[Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
            LinkName->Length = Name.Length;
            return STATUS_SUCCESS;
        }

        if (Status != STATUS_OBJECT_NAME_COLLISION)
            return Status;
    }

    return Status;
}

VOID
NTAPI
SmartcardLogError(
    PVOID Object,
    LONG ErrorCode,
    PUNICODE_STRING Insertion,
    ULONG DumpWord)
{
    PIO_ERROR_LOG_PACKET Packet;
    ULONG InsertionSize = 0;
    ULONG Size;

    if (Insertion != NULL && Insertion->Buffer != NULL)
        InsertionSize = Insertion->Length + sizeof(WCHAR);

    Size = sizeof(IO_ERROR_LOG_PACKET) + InsertionSize;
    if (Size > ERROR_LOG_MAXIMUM_SIZE)
    {
        InsertionSize = 0;
        Size = sizeof(IO_ERROR_LOG_PACKET);
    }

    Packet = IoAllocateErrorLogEntry(Object, (UCHAR)Size);
    if (Packet == NULL)
        return;

    Packet->ErrorCode = ErrorCode;
    Packet->SequenceNumber = 0;
    Packet->MajorFunctionCode = 0;
    Packet->RetryCount = 0;
    Packet->UniqueErrorValue = 0;
    Packet->FinalStatus = STATUS_SUCCESS;
    Packet->DumpDataSize = sizeof(ULONG);
    Packet->DumpData[0] = DumpWord;
    Packet->NumberOfStrings = 0;
    Packet->StringOffset = 0;

    if (InsertionSize != 0)
    {
        PWCHAR String = (PWCHAR)((PUCHAR)Packet + sizeof(IO_ERROR_LOG_PACKET));

        Packet->NumberOfStrings = 1;
        Packet->StringOffset = sizeof(IO_ERROR_LOG_PACKET);
        RtlCopyMemory(String, Insertion->Buffer, Insertion->Length);
        String[Insertion->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    IoWriteErrorLogEntry(Packet);
}

static
NTSTATUS
SmcCallReader(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_ ULONG Function)
{
    if (SmartcardExtension->ReaderFunction[Function] == NULL)
        return STATUS_NOT_SUPPORTED;

    return SmartcardExtension->ReaderFunction[Function](SmartcardExtension);
}

static
NTSTATUS
SmcReturn(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    if (SmartcardExtension->IoRequest.ReplyBufferLength < Length)
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(SmartcardExtension->IoRequest.ReplyBuffer, Data, Length);
    *SmartcardExtension->IoRequest.Information = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SmcReturnUlong(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_ ULONG Value)
{
    return SmcReturn(SmartcardExtension, &Value, sizeof(Value));
}

static
NTSTATUS
SmcReturnByte(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_ UCHAR Value)
{
    return SmcReturn(SmartcardExtension, &Value, sizeof(Value));
}

static
NTSTATUS
SmcGetAttribute(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSCARD_READER_CAPABILITIES Reader = &SmartcardExtension->ReaderCapabilities;
    PSCARD_CARD_CAPABILITIES Card = &SmartcardExtension->CardCapabilities;
    PVENDOR_ATTR Vendor = &SmartcardExtension->VendorAttr;
    ULONG State = Reader->CurrentState;

    switch (SmartcardExtension->MinorIoControlCode)
    {
        case SCARD_ATTR_VENDOR_NAME:
            return SmcReturn(SmartcardExtension, Vendor->VendorName.Buffer, Vendor->VendorName.Length);

        case SCARD_ATTR_VENDOR_IFD_TYPE:
            return SmcReturn(SmartcardExtension, Vendor->IfdType.Buffer, Vendor->IfdType.Length);

        case SCARD_ATTR_VENDOR_IFD_VERSION:
            return SmcReturn(SmartcardExtension, &Vendor->IfdVersion, sizeof(Vendor->IfdVersion));

        case SCARD_ATTR_VENDOR_IFD_SERIAL_NO:
            return SmcReturn(SmartcardExtension, Vendor->IfdSerialNo.Buffer, Vendor->IfdSerialNo.Length);

        case SCARD_ATTR_DEVICE_UNIT:
            return SmcReturnUlong(SmartcardExtension, Vendor->UnitNo);

        case SCARD_ATTR_CHANNEL_ID:
            return SmcReturnUlong(SmartcardExtension, (Reader->ReaderType << 16) | Reader->Channel);

        case SCARD_ATTR_CHARACTERISTICS:
            return SmcReturnUlong(SmartcardExtension, Reader->MechProperties);

        case SCARD_ATTR_PROTOCOL_TYPES:
            return SmcReturnUlong(SmartcardExtension, Reader->SupportedProtocols);

        case SCARD_ATTR_DEFAULT_CLK:
            return SmcReturnUlong(SmartcardExtension, Reader->CLKFrequency.Default);

        case SCARD_ATTR_MAX_CLK:
            return SmcReturnUlong(SmartcardExtension, Reader->CLKFrequency.Max);

        case SCARD_ATTR_DEFAULT_DATA_RATE:
            return SmcReturnUlong(SmartcardExtension, Reader->DataRate.Default);

        case SCARD_ATTR_MAX_DATA_RATE:
            return SmcReturnUlong(SmartcardExtension, Reader->DataRate.Max);

        case SCARD_ATTR_MAX_IFSD:
            return SmcReturnUlong(SmartcardExtension, Reader->MaxIFSD);

        case SCARD_ATTR_POWER_MGMT_SUPPORT:
            return SmcReturnUlong(SmartcardExtension, Reader->PowerMgmtSupport);

        case SCARD_ATTR_ICC_PRESENCE:
            if (State == SCARD_UNKNOWN)
                return STATUS_INVALID_DEVICE_STATE;
            return SmcReturnByte(SmartcardExtension,
                                 State == SCARD_ABSENT ? 0 : (State == SCARD_PRESENT ? 1 : 2));

        case SCARD_ATTR_ICC_INTERFACE_STATUS:
            return SmcReturnByte(SmartcardExtension, State >= SCARD_SWALLOWED ? 0xFF : 0);

        case SCARD_ATTR_ATR_STRING:
            if (State < SCARD_NEGOTIABLE)
                return STATUS_INVALID_DEVICE_STATE;
            return SmcReturn(SmartcardExtension, Card->ATR.Buffer, Card->ATR.Length);

        case SCARD_ATTR_ICC_TYPE_PER_ATR:
            if (State < SCARD_NEGOTIABLE)
                return STATUS_INVALID_DEVICE_STATE;
            return SmcReturnByte(SmartcardExtension, Card->Protocol.Selected != SCARD_PROTOCOL_UNDEFINED);

        case SCARD_ATTR_CURRENT_PROTOCOL_TYPE:
        case SCARD_ATTR_CURRENT_CLK:
        case SCARD_ATTR_CURRENT_F:
        case SCARD_ATTR_CURRENT_D:
        case SCARD_ATTR_CURRENT_N:
        case SCARD_ATTR_CURRENT_W:
        case SCARD_ATTR_CURRENT_IFSC:
        case SCARD_ATTR_CURRENT_IFSD:
        case SCARD_ATTR_CURRENT_BWT:
        case SCARD_ATTR_CURRENT_CWT:
        case SCARD_ATTR_CURRENT_EBC_ENCODING:
            if (State < SCARD_NEGOTIABLE)
                return STATUS_INVALID_DEVICE_STATE;
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    switch (SmartcardExtension->MinorIoControlCode)
    {
        case SCARD_ATTR_CURRENT_PROTOCOL_TYPE:
            return SmcReturnUlong(SmartcardExtension, Card->Protocol.Selected);

        case SCARD_ATTR_CURRENT_CLK:
            return SmcReturnUlong(SmartcardExtension, Reader->CLKFrequency.Default);

        case SCARD_ATTR_CURRENT_F:
            return SmcReturnUlong(SmartcardExtension, SmcClockRateConversion[Card->Fl & 15].F);

        case SCARD_ATTR_CURRENT_D:
            if (SmcBitRateAdjustment[Card->Dl & 15].DDivisor == 0)
                return STATUS_UNRECOGNIZED_MEDIA;
            return SmcReturnUlong(SmartcardExtension,
                                  SmcBitRateAdjustment[Card->Dl & 15].DNumerator /
                                  SmcBitRateAdjustment[Card->Dl & 15].DDivisor);

        case SCARD_ATTR_CURRENT_N:
            return SmcReturnUlong(SmartcardExtension, Card->N);

        case SCARD_ATTR_CURRENT_W:
            return SmcReturnUlong(SmartcardExtension, Card->T0.WI);

        case SCARD_ATTR_CURRENT_IFSC:
            return SmcReturnUlong(SmartcardExtension, Card->T1.IFSC);

        case SCARD_ATTR_CURRENT_IFSD:
            return SmcReturnUlong(SmartcardExtension, SmartcardExtension->T1.IFSD);

        case SCARD_ATTR_CURRENT_BWT:
            return SmcReturnUlong(SmartcardExtension, Card->T1.BWI);

        case SCARD_ATTR_CURRENT_CWT:
            return SmcReturnUlong(SmartcardExtension, Card->T1.CWI);

        default:
            return SmcReturnUlong(SmartcardExtension, Card->T1.EDC);
    }
}

static
NTSTATUS
SmcTrack(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_ PIRP Irp)
{
    POS_DEP_DATA OsData = SmartcardExtension->OsData;
    ULONG State;
    KIRQL Irql;

    if (SmartcardExtension->ReaderFunction[RDF_CARD_TRACKING] == NULL)
        return STATUS_NOT_SUPPORTED;

    KeAcquireSpinLock(&OsData->SpinLock, &Irql);
    if (OsData->NotificationIrp != NULL)
    {
        KeReleaseSpinLock(&OsData->SpinLock, Irql);
        return STATUS_DEVICE_BUSY;
    }

    State = SmartcardExtension->ReaderCapabilities.CurrentState;
    if (State == SCARD_UNKNOWN)
    {
        KeReleaseSpinLock(&OsData->SpinLock, Irql);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if ((SmartcardExtension->MajorIoControlCode == IOCTL_SMARTCARD_IS_PRESENT) == (State > SCARD_ABSENT))
    {
        KeReleaseSpinLock(&OsData->SpinLock, Irql);
        return STATUS_SUCCESS;
    }

    OsData->NotificationIrp = Irp;
    KeReleaseSpinLock(&OsData->SpinLock, Irql);

    return SmartcardExtension->ReaderFunction[RDF_CARD_TRACKING](SmartcardExtension);
}

static
NTSTATUS
SmcPower(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension)
{
    if (SmartcardExtension->ReaderFunction[RDF_CARD_POWER] == NULL)
        return STATUS_NOT_SUPPORTED;

    if (SmartcardExtension->ReaderCapabilities.CurrentState <= SCARD_ABSENT)
        return STATUS_INVALID_DEVICE_STATE;

    SmartcardInitializeCardCapabilities(SmartcardExtension);

    switch (SmartcardExtension->MinorIoControlCode)
    {
        case SCARD_COLD_RESET:
        case SCARD_WARM_RESET:
            if (SmartcardExtension->IoRequest.ReplyBufferLength < MAXIMUM_ATR_LENGTH)
                return STATUS_BUFFER_TOO_SMALL;
            break;

        case SCARD_POWER_DOWN:
            break;

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }

    return SmartcardExtension->ReaderFunction[RDF_CARD_POWER](SmartcardExtension);
}

static
NTSTATUS
SmcSetProtocol(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSCARD_READER_CAPABILITIES Reader = &SmartcardExtension->ReaderCapabilities;
    PSCARD_CARD_CAPABILITIES Card = &SmartcardExtension->CardCapabilities;
    ULONG Minor = SmartcardExtension->MinorIoControlCode;
    BOOLEAN Negotiable;
    NTSTATUS Status;
    ULONG Selected;

    if (SmartcardExtension->ReaderFunction[RDF_SET_PROTOCOL] == NULL)
        return STATUS_NOT_SUPPORTED;

    if (SmartcardExtension->IoRequest.ReplyBufferLength < sizeof(ULONG))
        return STATUS_BUFFER_TOO_SMALL;

    if (Reader->CurrentState <= SCARD_ABSENT)
        return STATUS_INVALID_DEVICE_STATE;

    if (Reader->CurrentState == SCARD_SPECIFIC && (Card->Protocol.Selected & Minor) != 0)
        return STATUS_SUCCESS;

    if (!(Minor & SCARD_PROTOCOL_Tx) && SmartcardExtension->ReaderFunction[RDF_ATR_PARSE] != NULL)
    {
        Status = SmartcardExtension->ReaderFunction[RDF_ATR_PARSE](SmartcardExtension);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (!(Minor & Card->Protocol.Supported))
    {
        Card->Protocol.Selected = SCARD_PROTOCOL_UNDEFINED;
        Reader->CurrentState = SCARD_NEGOTIABLE;
        return STATUS_NOT_SUPPORTED;
    }

    if (Minor & SCARD_PROTOCOL_Tx)
    {
        Negotiable = SmcIsNegotiable(SmartcardExtension, &Selected);
        SmcSelectTransmission(SmartcardExtension, !(Minor & SCARD_PROTOCOL_DEFAULT));

        if (Negotiable)
        {
            Reader->CurrentState = SCARD_NEGOTIABLE;
        }
        else
        {
            Card->Protocol.Selected = Selected;
            Reader->CurrentState = SCARD_SPECIFIC;
        }
    }

    return SmartcardExtension->ReaderFunction[RDF_SET_PROTOCOL](SmartcardExtension);
}

static
NTSTATUS
SmcTransmit(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSCARD_IO_REQUEST Header = (PSCARD_IO_REQUEST)SmartcardExtension->IoRequest.RequestBuffer;

    if (SmartcardExtension->ReaderFunction[RDF_TRANSMIT] == NULL)
        return STATUS_NOT_SUPPORTED;

    if (SmartcardExtension->ReaderCapabilities.CurrentState != SCARD_SPECIFIC)
        return STATUS_INVALID_DEVICE_STATE;

    if (SmartcardExtension->IoRequest.RequestBufferLength < sizeof(SCARD_IO_REQUEST))
        return STATUS_INVALID_PARAMETER;

    if (Header->dwProtocol != SmartcardExtension->CardCapabilities.Protocol.Selected)
        return STATUS_INVALID_DEVICE_STATE;

    return SmartcardExtension->ReaderFunction[RDF_TRANSMIT](SmartcardExtension);
}

static
NTSTATUS
SmcDispatch(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension)
{
    ULONG Code = SmartcardExtension->MajorIoControlCode;

    switch (Code)
    {
        case IOCTL_SMARTCARD_GET_STATE:
            return SmcReturnUlong(SmartcardExtension, SmartcardExtension->ReaderCapabilities.CurrentState);

        case IOCTL_SMARTCARD_POWER:
            return SmcPower(SmartcardExtension);

        case IOCTL_SMARTCARD_SET_PROTOCOL:
            return SmcSetProtocol(SmartcardExtension);

        case IOCTL_SMARTCARD_TRANSMIT:
            return SmcTransmit(SmartcardExtension);

        case IOCTL_SMARTCARD_EJECT:
            return SmcCallReader(SmartcardExtension, RDF_CARD_EJECT);

        case IOCTL_SMARTCARD_SWALLOW:
            return SmcCallReader(SmartcardExtension, RDF_READER_SWALLOW);

        case IOCTL_SMARTCARD_CONFISCATE:
            return SmcCallReader(SmartcardExtension, RDF_CARD_CONFISCATE);

        case IOCTL_SMARTCARD_GET_ATTRIBUTE:
            return SmcGetAttribute(SmartcardExtension);

        case IOCTL_SMARTCARD_SET_ATTRIBUTE:
            if (SmartcardExtension->MinorIoControlCode != SCARD_ATTR_SUPRESS_T1_IFS_REQUEST)
                return STATUS_NOT_SUPPORTED;
            SmartcardExtension->T1.State = T1_START;
            return STATUS_SUCCESS;

        default:
            if (((Code >> 2) & 0xFFF) < 2048 || SmartcardExtension->ReaderFunction[RDF_IOCTL_VENDOR] == NULL)
                return STATUS_INVALID_DEVICE_REQUEST;
            return SmartcardExtension->ReaderFunction[RDF_IOCTL_VENDOR](SmartcardExtension);
    }
}

NTSTATUS
NTAPI
SmartcardDeviceControl(
    PSMARTCARD_EXTENSION SmartcardExtension,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    POS_DEP_DATA OsData = SmartcardExtension->OsData;
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    NTSTATUS Status;
    KIRQL Irql;

    if ((Code & 3) != METHOD_BUFFERED)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    Status = KeWaitForSingleObject(&OsData->Mutex, UserRequest, KernelMode, FALSE, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    SmartcardExtension->MajorIoControlCode = Code;
    SmartcardExtension->IoRequest.Information = (PULONG)&Irp->IoStatus.Information;
    SmartcardExtension->IoRequest.RequestBuffer = Irp->AssociatedIrp.SystemBuffer;
    SmartcardExtension->IoRequest.RequestBufferLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    SmartcardExtension->IoRequest.ReplyBuffer = Irp->AssociatedIrp.SystemBuffer;
    SmartcardExtension->IoRequest.ReplyBufferLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (SmartcardExtension->IoRequest.RequestBufferLength >= sizeof(ULONG))
    {
        RtlCopyMemory(&SmartcardExtension->MinorIoControlCode,
                      SmartcardExtension->IoRequest.RequestBuffer,
                      sizeof(ULONG));
    }

    if (Code == IOCTL_SMARTCARD_IS_PRESENT || Code == IOCTL_SMARTCARD_IS_ABSENT)
    {
        Status = SmcTrack(SmartcardExtension, Irp);
    }
    else
    {
        KeAcquireSpinLock(&OsData->SpinLock, &Irql);
        OsData->CurrentIrp = Irp;
        KeReleaseSpinLock(&OsData->SpinLock, Irql);

        Irp->IoStatus.Information = 0;
        Status = SmcDispatch(SmartcardExtension);

        KeAcquireSpinLock(&OsData->SpinLock, &Irql);
        OsData->CurrentIrp = NULL;
        KeReleaseSpinLock(&OsData->SpinLock, Irql);
    }

    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    KeReleaseMutex(&OsData->Mutex, FALSE);
    return Status;
}
