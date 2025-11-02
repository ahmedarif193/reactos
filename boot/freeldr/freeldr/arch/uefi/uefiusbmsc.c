/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Prototype scaffolding for native USB mass-storage reads
 * Copyright : Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include <uefildr.h>
#include <UsbIo.h>
#include <uefiusbmsc.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(DISK);

#define EFI_USB2_HC_PROTOCOL_GUID   { 0x3e745226, 0x9818, 0x45b6, { 0xa2, 0xac, 0xd7, 0xcd, 0x0e, 0x8b, 0xa2, 0xbc } }
#define EFI_USB3_HC_PROTOCOL_GUID   { 0xf5089269, 0xaae5, 0x487d, { 0xa0, 0x7b, 0xd7, 0x1d, 0xfd, 0xb4, 0x54, 0x37 } }

EFI_GUID gEfiUsbIoProtocolGuid = EFI_USB_IO_PROTOCOL_GUID;
static EFI_GUID gEfiUsb2HcProtocolGuid = EFI_USB2_HC_PROTOCOL_GUID;
static EFI_GUID gEfiUsb3HcProtocolGuid = EFI_USB3_HC_PROTOCOL_GUID;
static EFI_GUID DevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static BOOLEAN gUsbControllersEnumerated = FALSE;
static BOOLEAN gUsbConnectAllFallback = FALSE; /* Gate aggressive ConnectController(AllHandles) */

extern EFI_SYSTEM_TABLE *GlobalSystemTable;

static
BOOLEAN
UefiDevicePathHasPrefix(
    IN EFI_DEVICE_PATH_PROTOCOL *ChildPath,
    IN EFI_DEVICE_PATH_PROTOCOL *PrefixPath)
{
    EFI_DEVICE_PATH_PROTOCOL *ChildNode;
    EFI_DEVICE_PATH_PROTOCOL *PrefixNode;

    if (!ChildPath || !PrefixPath)
        return FALSE;

    ChildNode = ChildPath;
    PrefixNode = PrefixPath;

    while (!IsDevicePathEnd(PrefixNode))
    {
        if (IsDevicePathEnd(ChildNode))
            return FALSE;

        if (DevicePathNodeLength(PrefixNode) != DevicePathNodeLength(ChildNode))
            return FALSE;

        if (!RtlEqualMemory(PrefixNode, ChildNode, DevicePathNodeLength(PrefixNode)))
            return FALSE;

        PrefixNode = NextDevicePathNode(PrefixNode);
        ChildNode = NextDevicePathNode(ChildNode);
    }

    return TRUE;
}

static
VOID
UefiUsbEnsureHostControllersConnected(VOID)
{
    EFI_STATUS Status;
    EFI_HANDLE *Handles = NULL;
    UINTN Count = 0;
    BOOLEAN Connected = FALSE;

    if (gUsbControllersEnumerated)
        return;

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol,
        &gEfiUsb2HcProtocolGuid,
        NULL,
        &Count,
        &Handles);
    if (!EFI_ERROR(Status) && Handles && Count != 0)
    {
        TRACE("UefiUsbEnsureHostControllersConnected: connecting %lu USB2 HC handles\n", Count);
        for (UINTN i = 0; i < Count; ++i)
            GlobalSystemTable->BootServices->ConnectController(Handles[i], NULL, NULL, TRUE);
        GlobalSystemTable->BootServices->FreePool(Handles);
        Handles = NULL;
        Connected = TRUE;
    }
    else if (Handles)
    {
        GlobalSystemTable->BootServices->FreePool(Handles);
        Handles = NULL;
    }

    Count = 0;
    Handles = NULL;
    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol,
        &gEfiUsb3HcProtocolGuid,
        NULL,
        &Count,
        &Handles);
    if (!EFI_ERROR(Status) && Handles && Count != 0)
    {
        TRACE("UefiUsbEnsureHostControllersConnected: connecting %lu USB3 HC handles\n", Count);
        for (UINTN i = 0; i < Count; ++i)
            GlobalSystemTable->BootServices->ConnectController(Handles[i], NULL, NULL, TRUE);
        GlobalSystemTable->BootServices->FreePool(Handles);
        Handles = NULL;
        Connected = TRUE;
    }
    else if (Handles)
    {
        GlobalSystemTable->BootServices->FreePool(Handles);
    }

    if (!Connected && gUsbConnectAllFallback)
    {
        Count = 0;
        Handles = NULL;
        Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
            AllHandles,
            NULL,
            NULL,
            &Count,
            &Handles);
        if (!EFI_ERROR(Status) && Handles && Count != 0)
        {
            TRACE("UefiUsbEnsureHostControllersConnected: connecting %lu generic handles\n", Count);
            for (UINTN i = 0; i < Count; ++i)
                GlobalSystemTable->BootServices->ConnectController(Handles[i], NULL, NULL, TRUE);
            GlobalSystemTable->BootServices->FreePool(Handles);
            Handles = NULL;
            Connected = TRUE;
        }
        else if (Handles)
        {
            GlobalSystemTable->BootServices->FreePool(Handles);
        }
    }

    if (Connected)
        gUsbControllersEnumerated = TRUE;
}



#define USBMSC_TAG 'cmsu'

#define USB_MSC_CLASS             0x08
#define USB_MSC_SUBCLASS_SCSI     0x06
#define USB_MSC_PROTOCOL_BULK_ONLY 0x50

#define USB_MSC_CBW_SIGNATURE     0x43425355UL /* 'USBC' */
#define USB_MSC_CSW_SIGNATURE     0x53425355UL /* 'USBS' */

#define USB_MSC_CSW_STATUS_PASS        0x00
#define USB_MSC_CSW_STATUS_FAIL        0x01
#define USB_MSC_CSW_STATUS_PHASE_ERROR 0x02

#define USB_BOT_FLAG_IN                0x80
#define USB_BOT_TIMEOUT                5000

#define USB_MSC_BOT_RESET              0xFF
#define SCSI_CMD_REQUEST_SENSE         0x03
#define SCSI_CMD_WRITE10               0x2A
#define SCSI_CMD_READ10                0x28

#define USB_MSC_SENSE_DATA_LENGTH      18

/* Sense key values */
#define USB_MSC_SENSE_NO_SENSE         0x00
#define USB_MSC_SENSE_RECOVERED_ERROR  0x01
#define USB_MSC_SENSE_NOT_READY        0x02
#define USB_MSC_SENSE_MEDIUM_ERROR     0x03
#define USB_MSC_SENSE_HARDWARE_ERROR   0x04
#define USB_MSC_SENSE_ILLEGAL_REQUEST  0x05
#define USB_MSC_SENSE_UNIT_ATTENTION   0x06
#define USB_MSC_SENSE_DATA_PROTECT     0x07
#define USB_MSC_SENSE_BLANK_CHECK      0x08
#define USB_MSC_SENSE_VENDOR_SPECIFIC  0x09
#define USB_MSC_SENSE_COPY_ABORTED     0x0A
#define USB_MSC_SENSE_ABORTED_COMMAND  0x0B
#define USB_MSC_SENSE_VOLUME_OVERFLOW  0x0D
#define USB_MSC_SENSE_MISCOMPARE       0x0E

typedef struct _USB_MSC_CBW
{
    UINT32 Signature;
    UINT32 Tag;
    UINT32 DataTransferLength;
    UINT8  Flags;
    UINT8  Lun;
    UINT8  CdbLength;
    UINT8  Cdb[16];
} USB_MSC_CBW;

typedef struct _USB_MSC_CSW
{
    UINT32 Signature;
    UINT32 Tag;
    UINT32 DataResidue;
    UINT8  Status;
} USB_MSC_CSW;

typedef struct _UEFI_USB_MSC_CONTEXT
{
    EFI_HANDLE Handle;
    EFI_USB_IO_PROTOCOL *UsbIo;
    ULONG SectorSize;
    UINT8 InterfaceNumber;
    BOOLEAN PrototypeActive;
    UINT8 BulkInEndpoint;
    UINT8 BulkOutEndpoint;
} UEFI_USB_MSC_CONTEXT, *PUEFI_USB_MSC_CONTEXT;

extern EFI_SYSTEM_TABLE *GlobalSystemTable;

static UINT32 gUsbMscNextTag = 1;

static
PVOID
UefiUsbMscBindHandle(
    EFI_HANDLE Handle,
    ULONG SectorSize,
    BOOLEAN Verbose)
{
    EFI_STATUS Status;
    EFI_USB_IO_PROTOCOL *UsbIo = NULL;
    EFI_USB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    EFI_USB_ENDPOINT_DESCRIPTOR EndpointDescriptor;
    UINT8 BulkIn = 0;
    UINT8 BulkOut = 0;
    BOOLEAN FoundBulkIn = FALSE;
    BOOLEAN FoundBulkOut = FALSE;
    UEFI_USB_MSC_CONTEXT *Context;

    if (!Handle || SectorSize == 0)
        return NULL;

    if (Verbose)
    {
        TRACE("UefiUsbMscBindHandle: handle=%p sectorSize=%lu\n",
              Handle,
              SectorSize);
    }

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        Handle,
        &gEfiUsbIoProtocolGuid,
        (VOID **)&UsbIo);
    if ((EFI_ERROR(Status) || !UsbIo))
    {
        if (Verbose)
        {
            TRACE("UefiUsbMscBindHandle: HandleProtocol failed status=%lx usbIo=%p\n",
                  (ULONG_PTR)Status,
                  UsbIo);
        }
        if (!EFI_ERROR(GlobalSystemTable->BootServices->ConnectController(Handle, NULL, NULL, TRUE)))
        {
            Status = GlobalSystemTable->BootServices->HandleProtocol(
                Handle,
                &gEfiUsbIoProtocolGuid,
                (VOID **)&UsbIo);
            if (Verbose)
            {
                TRACE("UefiUsbMscBindHandle: retry HandleProtocol after ConnectController status=%lx usbIo=%p\n",
                      (ULONG_PTR)Status,
                      UsbIo);
            }
        }
    }

    if (EFI_ERROR(Status) || !UsbIo)
        return NULL;

    Status = UsbIo->UsbGetInterfaceDescriptor(UsbIo, &InterfaceDescriptor);
    if (EFI_ERROR(Status))
        return NULL;

    if (Verbose)
    {
        TRACE("UefiUsbMscBindHandle: iface class=%u subclass=%u proto=%u endpoints=%u\n",
              InterfaceDescriptor.InterfaceClass,
              InterfaceDescriptor.InterfaceSubClass,
              InterfaceDescriptor.InterfaceProtocol,
              InterfaceDescriptor.NumEndpoints);
    }

    if (InterfaceDescriptor.InterfaceClass != USB_MSC_CLASS ||
        InterfaceDescriptor.InterfaceSubClass != USB_MSC_SUBCLASS_SCSI ||
        InterfaceDescriptor.InterfaceProtocol != USB_MSC_PROTOCOL_BULK_ONLY)
    {
        if (Verbose)
        {
            TRACE("UefiUsbMscBindHandle: rejecting interface (class=%u subclass=%u proto=%u)\n",
                  InterfaceDescriptor.InterfaceClass,
                  InterfaceDescriptor.InterfaceSubClass,
                  InterfaceDescriptor.InterfaceProtocol);
        }
        return NULL;
    }

    for (UINT8 Index = 0; Index < InterfaceDescriptor.NumEndpoints; ++Index)
    {
        Status = UsbIo->UsbGetEndpointDescriptor(UsbIo, Index, &EndpointDescriptor);
        if (EFI_ERROR(Status))
            continue;

        if ((EndpointDescriptor.Attributes & EFI_USB_ENDPOINT_TYPE_MASK) != EFI_USB_ENDPOINT_BULK)
            continue;

        if (EndpointDescriptor.EndpointAddress & EFI_USB_ENDPOINT_DIR_IN)
        {
            BulkIn = EndpointDescriptor.EndpointAddress;
            FoundBulkIn = TRUE;
        }
        else
        {
            BulkOut = EndpointDescriptor.EndpointAddress;
            FoundBulkOut = TRUE;
        }
    }

    if (!FoundBulkIn || !FoundBulkOut)
        return NULL;

    Context = FrLdrTempAlloc(sizeof(*Context), USBMSC_TAG);
    if (!Context)
        return NULL;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Handle = Handle;
    Context->UsbIo = UsbIo;
    Context->SectorSize = SectorSize;
    Context->InterfaceNumber = InterfaceDescriptor.InterfaceNumber;
    Context->PrototypeActive = TRUE;
    Context->BulkInEndpoint = BulkIn;
    Context->BulkOutEndpoint = BulkOut;

    TRACE("UefiUsbMscBindHandle: bound handle=%p interface=%u bulkIn=0x%02x bulkOut=0x%02x\n",
          Handle,
          InterfaceDescriptor.InterfaceNumber,
          BulkIn,
          BulkOut);

    return Context;
}

PVOID
UefiUsbMscTryBind(
    EFI_HANDLE Handle,
    ULONG SectorSize)
{
    UefiUsbEnsureHostControllersConnected();

    EFI_STATUS Status;
    EFI_DEVICE_PATH_PROTOCOL *BlockDevicePath = NULL;
    EFI_HANDLE *UsbHandles = NULL;
    UINTN UsbHandleCount = 0;
    PVOID Context = NULL;

    Context = UefiUsbMscBindHandle(Handle, SectorSize, TRUE);
    if (Context)
        return Context;

    if (!Handle)
        return NULL;

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        Handle,
        &DevicePathProtocolGuid,
        (VOID**)&BlockDevicePath);
    if (EFI_ERROR(Status) || !BlockDevicePath)
    {
        TRACE("UefiUsbMscTryBind: BlockIo handle %p has no device path (status=%lx)\n",
              Handle,
              (ULONG_PTR)Status);
        return NULL;
    }

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol,
        &gEfiUsbIoProtocolGuid,
        NULL,
        &UsbHandleCount,
        &UsbHandles);
    if (!EFI_ERROR(Status) && UsbHandles && UsbHandleCount == 0)
    {
        GlobalSystemTable->BootServices->FreePool(UsbHandles);
        UsbHandles = NULL;
        Status = EFI_NOT_FOUND;
    }

    if (EFI_ERROR(Status) || UsbHandles == NULL)
    {
        TRACE("UefiUsbMscTryBind: LocateHandleBuffer failed status=%lx count=%lu\n",
              (ULONG_PTR)Status,
              UsbHandleCount);

        GlobalSystemTable->BootServices->ConnectController(Handle, NULL, NULL, TRUE);
        UefiUsbEnsureHostControllersConnected();

        UsbHandleCount = 0;
        UsbHandles = NULL;
        Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
            ByProtocol,
            &gEfiUsbIoProtocolGuid,
            NULL,
            &UsbHandleCount,
            &UsbHandles);
        if (!EFI_ERROR(Status) && UsbHandles && UsbHandleCount == 0)
        {
            GlobalSystemTable->BootServices->FreePool(UsbHandles);
            UsbHandles = NULL;
            Status = EFI_NOT_FOUND;
        }
        if (EFI_ERROR(Status) || UsbHandles == NULL)
            goto Cleanup;
    }

    TRACE("UefiUsbMscTryBind: scanning %lu USB handles for BlockIo handle %p\n",
          UsbHandleCount,
          Handle);

    for (UINTN Index = 0; Index < UsbHandleCount; ++Index)
    {
        EFI_HANDLE Candidate = UsbHandles[Index];
        EFI_DEVICE_PATH_PROTOCOL *UsbPath = NULL;

        if (Candidate == Handle)
            continue;

        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
                Candidate,
                &DevicePathProtocolGuid,
                (VOID**)&UsbPath)) ||
            !UsbPath)
        {
            continue;
        }

        if (!UefiDevicePathHasPrefix(BlockDevicePath, UsbPath))
            continue;

        TRACE("UefiUsbMscTryBind: attempting parent USB handle %p for BlockIo handle %p\n",
              Candidate,
              Handle);

        Context = UefiUsbMscBindHandle(Candidate, SectorSize, FALSE);
        if (Context)
            break;
    }

Cleanup:
    if (UsbHandles)
        GlobalSystemTable->BootServices->FreePool(UsbHandles);

    return Context;
}

static BOOLEAN
UefiUsbMscClearStall(
    PUEFI_USB_MSC_CONTEXT Context,
    UINT8 Endpoint)
{
    EFI_USB_DEVICE_REQUEST Request;
    UINT32 UsbStatus = 0;
    EFI_STATUS Status;

    if (!Context || !Context->UsbIo)
        return FALSE;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.RequestType = USB_BMRT_HOST_TO_DEVICE | USB_BMRT_TYPE_STANDARD | USB_BMRT_RECIP_ENDPOINT;
    Request.Request = USB_REQ_CLEAR_FEATURE;
    Request.Value = USB_FEATURE_ENDPOINT_HALT;
    Request.Index = Endpoint;
    Request.Length = 0;

    Status = Context->UsbIo->UsbControlTransfer(Context->UsbIo,
                                                &Request,
                                                EfiUsbNoData,
                                                USB_BOT_TIMEOUT,
                                                NULL,
                                                0,
                                                &UsbStatus);

    if (EFI_ERROR(Status) || UsbStatus != EFI_USB_NOERROR)
    {
        WARN("UefiUsbMscClearStall: failed for endpoint 0x%02x (Status=%lx UsbStatus=%lx)\n",
             Endpoint,
             (ULONG_PTR)Status,
             (ULONG_PTR)UsbStatus);
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
UefiUsbMscRecover(
    PUEFI_USB_MSC_CONTEXT Context)
{
    EFI_USB_DEVICE_REQUEST Request;
    UINT32 UsbStatus = 0;
    EFI_STATUS Status;
    BOOLEAN Result = TRUE;

    if (!Context || !Context->UsbIo)
        return FALSE;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.RequestType = USB_BMRT_HOST_TO_DEVICE | USB_BMRT_TYPE_CLASS | USB_BMRT_RECIP_INTERFACE;
    Request.Request = USB_MSC_BOT_RESET;
    Request.Value = 0;
    Request.Index = Context->InterfaceNumber;
    Request.Length = 0;

    Status = Context->UsbIo->UsbControlTransfer(Context->UsbIo,
                                                &Request,
                                                EfiUsbNoData,
                                                USB_BOT_TIMEOUT,
                                                NULL,
                                                0,
                                                &UsbStatus);

    if (EFI_ERROR(Status) || UsbStatus != EFI_USB_NOERROR)
    {
        WARN("UefiUsbMscRecover: BOT reset failed (Status=%lx UsbStatus=%lx)\n",
             (ULONG_PTR)Status,
             (ULONG_PTR)UsbStatus);
        Result = FALSE;
    }

    if (!UefiUsbMscClearStall(Context, Context->BulkInEndpoint))
        Result = FALSE;

    if (!UefiUsbMscClearStall(Context, Context->BulkOutEndpoint))
        Result = FALSE;

    return Result;
}

static BOOLEAN
UefiUsbMscSenseIndicatesRetry(
    UINT8 SenseKey,
    UINT8 Asc,
    UINT8 Ascq)
{
    switch (SenseKey & 0x0F)
    {
        case USB_MSC_SENSE_NO_SENSE:
        case USB_MSC_SENSE_RECOVERED_ERROR:
        case USB_MSC_SENSE_UNIT_ATTENTION:
        case USB_MSC_SENSE_ABORTED_COMMAND:
            return TRUE;

        case USB_MSC_SENSE_NOT_READY:
            if (Asc == 0x04 && (Ascq == 0x00 || Ascq == 0x01 || Ascq == 0x02))
                return TRUE;
            return FALSE;

        default:
            return FALSE;
    }
}

static BOOLEAN
UefiUsbMscRequestSense(
    PUEFI_USB_MSC_CONTEXT Context,
    UINT8 *SenseBuffer,
    UINTN SenseLength)
{
    USB_MSC_CBW Cbw;
    USB_MSC_CSW Csw;
    UINTN TransferLength;
    UINT32 UsbStatus = 0;
    EFI_STATUS Status;

    if (!Context || !Context->UsbIo || !SenseBuffer || SenseLength == 0)
        return FALSE;

    RtlZeroMemory(SenseBuffer, SenseLength);
    RtlZeroMemory(&Cbw, sizeof(Cbw));
    Cbw.Signature = USB_MSC_CBW_SIGNATURE;
    Cbw.Tag = gUsbMscNextTag++;
    Cbw.DataTransferLength = (UINT32)SenseLength;
    Cbw.Flags = USB_BOT_FLAG_IN;
    Cbw.Lun = 0;
    Cbw.CdbLength = 6;
    Cbw.Cdb[0] = SCSI_CMD_REQUEST_SENSE;
    Cbw.Cdb[4] = (UINT8)SenseLength;

    TransferLength = sizeof(Cbw);
    Status = Context->UsbIo->UsbBulkTransfer(Context->UsbIo,
                                             Context->BulkOutEndpoint,
                                             &Cbw,
                                             &TransferLength,
                                             USB_BOT_TIMEOUT,
                                             &UsbStatus);
    if (EFI_ERROR(Status) || UsbStatus != EFI_USB_NOERROR || TransferLength != sizeof(Cbw))
        return FALSE;

    TransferLength = SenseLength;
    UsbStatus = 0;
    Status = Context->UsbIo->UsbBulkTransfer(Context->UsbIo,
                                             Context->BulkInEndpoint,
                                             SenseBuffer,
                                             &TransferLength,
                                             USB_BOT_TIMEOUT,
                                             &UsbStatus);
    if (EFI_ERROR(Status) || UsbStatus != EFI_USB_NOERROR || TransferLength != SenseLength)
        return FALSE;

    TransferLength = sizeof(Csw);
    UsbStatus = 0;
    Status = Context->UsbIo->UsbBulkTransfer(Context->UsbIo,
                                             Context->BulkInEndpoint,
                                             &Csw,
                                             &TransferLength,
                                             USB_BOT_TIMEOUT,
                                             &UsbStatus);
    if (EFI_ERROR(Status) || UsbStatus != EFI_USB_NOERROR || TransferLength != sizeof(Csw))
        return FALSE;

    if (Csw.Signature != USB_MSC_CSW_SIGNATURE || Csw.Tag != Cbw.Tag)
        return FALSE;

    return (Csw.Status == USB_MSC_CSW_STATUS_PASS);
}

static BOOLEAN
UefiUsbMscLogSenseAndDecideRetry(
    PUEFI_USB_MSC_CONTEXT Context)
{
    UINT8 Sense[USB_MSC_SENSE_DATA_LENGTH];

    if (!UefiUsbMscRequestSense(Context, Sense, sizeof(Sense)))
    {
        WARN("UefiUsbMscLogSenseAndDecideRetry: REQUEST SENSE failed\n");
        return FALSE;
    }

    WARN("UefiUsbMscSense: key=0x%02x asc=0x%02x ascq=0x%02x\n",
         Sense[2] & 0x0F,
         Sense[12],
         Sense[13]);

    return UefiUsbMscSenseIndicatesRetry(Sense[2] & 0x0F, Sense[12], Sense[13]);
}


static
BOOLEAN
UefiUsbMscExecuteTransfer(
    PUEFI_USB_MSC_CONTEXT Context,
    ULONGLONG Lba,
    ULONG SectorCount,
    ULONG SectorSize,
    PVOID Buffer,
    BOOLEAN Write)
{
    EFI_USB_IO_PROTOCOL *UsbIo;
    UINT64 RemainingSectors = SectorCount;
    UINT8 *CurrentBuffer = (UINT8 *)Buffer;
    const CHAR *OpName = Write ? "WRITE" : "READ";

    if (!Context || !Context->UsbIo || !Buffer || SectorCount == 0)
        return FALSE;

    UsbIo = Context->UsbIo;

    while (RemainingSectors > 0)
    {
        ULONG ChunkSectors = (RemainingSectors > 0xFFFFULL) ? 0xFFFFUL : (ULONG)RemainingSectors;
        BOOLEAN RecoveryAttempted = FALSE;
        UINTN DataLength;
        USB_MSC_CBW Cbw;
        USB_MSC_CSW Csw;
        UINTN TransferLength;
        UINT32 UsbStatus;
        EFI_STATUS BulkStatus;

RetryChunk:
        DataLength = (UINTN)ChunkSectors * SectorSize;

        RtlZeroMemory(&Cbw, sizeof(Cbw));
        Cbw.Signature = USB_MSC_CBW_SIGNATURE;
        Cbw.Tag = gUsbMscNextTag++;
        Cbw.DataTransferLength = (UINT32)DataLength;
        Cbw.Flags = Write ? 0 : USB_BOT_FLAG_IN;
        Cbw.Lun = 0;
        Cbw.CdbLength = 10;
        Cbw.Cdb[0] = Write ? SCSI_CMD_WRITE10 : SCSI_CMD_READ10;
        Cbw.Cdb[2] = (UINT8)((Lba >> 24) & 0xFF);
        Cbw.Cdb[3] = (UINT8)((Lba >> 16) & 0xFF);
        Cbw.Cdb[4] = (UINT8)((Lba >> 8) & 0xFF);
        Cbw.Cdb[5] = (UINT8)(Lba & 0xFF);
        Cbw.Cdb[7] = (UINT8)((ChunkSectors >> 8) & 0xFF);
        Cbw.Cdb[8] = (UINT8)(ChunkSectors & 0xFF);

        TransferLength = sizeof(Cbw);
        UsbStatus = 0;
        BulkStatus = UsbIo->UsbBulkTransfer(UsbIo,
                                            Context->BulkOutEndpoint,
                                            &Cbw,
                                            &TransferLength,
                                            USB_BOT_TIMEOUT,
                                            &UsbStatus);
        if (EFI_ERROR(BulkStatus) || UsbStatus != EFI_USB_NOERROR || TransferLength != sizeof(Cbw))
        {
            WARN("UefiUsbMscExecuteTransfer(%s): CBW transfer failed (Status=%lx UsbStatus=%lx len=%lu)\n",
                 OpName,
                 (ULONG_PTR)BulkStatus,
                 (ULONG_PTR)UsbStatus,
                 (ULONG)TransferLength);

            BOOLEAN RetryAllowed = FALSE;
            if (!RecoveryAttempted)
                RetryAllowed = UefiUsbMscLogSenseAndDecideRetry(Context);

            if (!RecoveryAttempted && RetryAllowed && UefiUsbMscRecover(Context))
            {
                RecoveryAttempted = TRUE;
                goto RetryChunk;
            }

            return FALSE;
        }

        TransferLength = DataLength;
        UsbStatus = 0;
        BulkStatus = UsbIo->UsbBulkTransfer(UsbIo,
                                            Write ? Context->BulkOutEndpoint : Context->BulkInEndpoint,
                                            CurrentBuffer,
                                            &TransferLength,
                                            USB_BOT_TIMEOUT,
                                            &UsbStatus);
        if (EFI_ERROR(BulkStatus) || UsbStatus != EFI_USB_NOERROR || TransferLength != DataLength)
        {
            WARN("UefiUsbMscExecuteTransfer(%s): data transfer failed (Status=%lx UsbStatus=%lx len=%lu/%lu)\n",
                 OpName,
                 (ULONG_PTR)BulkStatus,
                 (ULONG_PTR)UsbStatus,
                 (ULONG)TransferLength,
                 (ULONG)DataLength);

            BOOLEAN RetryAllowed = FALSE;
            if (!RecoveryAttempted)
                RetryAllowed = UefiUsbMscLogSenseAndDecideRetry(Context);

            if (!RecoveryAttempted && RetryAllowed && UefiUsbMscRecover(Context))
            {
                RecoveryAttempted = TRUE;
                goto RetryChunk;
            }

            return FALSE;
        }

        TransferLength = sizeof(Csw);
        UsbStatus = 0;
        BulkStatus = UsbIo->UsbBulkTransfer(UsbIo,
                                            Context->BulkInEndpoint,
                                            &Csw,
                                            &TransferLength,
                                            USB_BOT_TIMEOUT,
                                            &UsbStatus);
        if (EFI_ERROR(BulkStatus) || UsbStatus != EFI_USB_NOERROR || TransferLength != sizeof(Csw))
        {
            WARN("UefiUsbMscExecuteTransfer(%s): CSW transfer failed (Status=%lx UsbStatus=%lx len=%lu)\n",
                 OpName,
                 (ULONG_PTR)BulkStatus,
                 (ULONG_PTR)UsbStatus,
                 (ULONG)TransferLength);

            BOOLEAN RetryAllowed = FALSE;
            if (!RecoveryAttempted)
                RetryAllowed = UefiUsbMscLogSenseAndDecideRetry(Context);

            if (!RecoveryAttempted && RetryAllowed && UefiUsbMscRecover(Context))
            {
                RecoveryAttempted = TRUE;
                goto RetryChunk;
            }

            return FALSE;
        }

        if (Csw.Signature != USB_MSC_CSW_SIGNATURE || Csw.Tag != Cbw.Tag)
        {
            WARN("UefiUsbMscExecuteTransfer(%s): CSW signature/tag mismatch (sig=%lx tag=%lx expected=%lx)\n",
                 OpName,
                 Csw.Signature,
                 Csw.Tag,
                 Cbw.Tag);

            BOOLEAN RetryAllowed = FALSE;
            if (!RecoveryAttempted)
                RetryAllowed = UefiUsbMscLogSenseAndDecideRetry(Context);

            if (!RecoveryAttempted && RetryAllowed && UefiUsbMscRecover(Context))
            {
                RecoveryAttempted = TRUE;
                goto RetryChunk;
            }

            return FALSE;
        }

        if (Csw.Status != USB_MSC_CSW_STATUS_PASS)
        {
            WARN("UefiUsbMscExecuteTransfer(%s): CSW reports failure (status=%u residue=%lu)\n",
                 OpName,
                 Csw.Status,
                 Csw.DataResidue);

            BOOLEAN RetryAllowed = FALSE;
            if (!RecoveryAttempted)
                RetryAllowed = UefiUsbMscLogSenseAndDecideRetry(Context);

            if (!RecoveryAttempted && RetryAllowed && UefiUsbMscRecover(Context))
            {
                RecoveryAttempted = TRUE;
                goto RetryChunk;
            }

            return FALSE;
        }

        CurrentBuffer += DataLength;
        Lba += ChunkSectors;
        RemainingSectors -= ChunkSectors;
    }

    return TRUE;
}


VOID
UefiUsbMscRelease(
    PVOID GenericContext)
{
    PUEFI_USB_MSC_CONTEXT Context = (PUEFI_USB_MSC_CONTEXT)GenericContext;

    if (!Context)
        return;

    TRACE("UefiUsbMscRelease: releasing context for handle %p\n", Context->Handle);
    Context->UsbIo = NULL;
    FrLdrTempFree(Context, USBMSC_TAG);
}

BOOLEAN
UefiUsbMscRead(
    PVOID GenericContext,
    ULONGLONG Lba,
    ULONG SectorCount,
    ULONG SectorSize,
    PVOID Buffer)
{
    PUEFI_USB_MSC_CONTEXT Context = (PUEFI_USB_MSC_CONTEXT)GenericContext;

    if (!Context || !Buffer || SectorCount == 0)
        return FALSE;

    if (!Context->PrototypeActive)
    {
        TRACE("UefiUsbMscRead: prototype inactive for handle %p, LBA %I64u count %lu\n",
              Context->Handle,
              Lba,
              SectorCount);
        return FALSE;
    }

    if (UefiUsbMscExecuteTransfer(Context, Lba, SectorCount, SectorSize, Buffer, FALSE))
        return TRUE;

    WARN("UefiUsbMscRead: USB MSC transfer failed; falling back to firmware path\n");
    Context->PrototypeActive = FALSE;
    return FALSE;
}

BOOLEAN
UefiUsbMscWrite(
    PVOID GenericContext,
    ULONGLONG Lba,
    ULONG SectorCount,
    ULONG SectorSize,
    PVOID Buffer)
{
    PUEFI_USB_MSC_CONTEXT Context = (PUEFI_USB_MSC_CONTEXT)GenericContext;

    if (!Context || !Buffer || SectorCount == 0)
        return FALSE;

    if (!Context->PrototypeActive)
    {
        TRACE("UefiUsbMscWrite: prototype inactive for handle %p, LBA %I64u count %lu\n",
              Context->Handle,
              Lba,
              SectorCount);
        return FALSE;
    }

    if (UefiUsbMscExecuteTransfer(Context, Lba, SectorCount, SectorSize, Buffer, TRUE))
        return TRUE;

    WARN("UefiUsbMscWrite: USB MSC transfer failed; falling back to firmware path\n");
    Context->PrototypeActive = FALSE;
    return FALSE;
}
