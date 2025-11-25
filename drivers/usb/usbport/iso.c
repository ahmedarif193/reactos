/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBPort isochronous transfer functions
 * COPYRIGHT:   Copyright 2017 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbport.h"

#define NDEBUG
#include <debug.h>

static VOID
USBPORT_IsoRecalculateErrors(PUSBPORT_ISO_BLOCK Iso)
{
    ULONG ix;

    Iso->ErrorCount = 0;

    for (ix = 0; ix < Iso->NumberOfPackets; ix++)
    {
        if (USBD_ERROR(Iso->Packets[ix].Status))
            Iso->ErrorCount++;
    }
}

USBD_STATUS
NTAPI
USBPORT_InitializeIsoTransfer(PDEVICE_OBJECT FdoDevice,
                              struct _URB_ISOCH_TRANSFER * Urb,
                              PUSBPORT_TRANSFER Transfer)
{
    PUSBPORT_ISO_BLOCK IsoBlock;
    ULONG ix;
    ULONG EndOffset;

    UNREFERENCED_PARAMETER(FdoDevice);

    IsoBlock = Transfer->IsoBlockPtr;

    if (!IsoBlock)
    {
        DPRINT1("USBPORT_InitializeIsoTransfer: IsoBlock not allocated\n");
        return USBD_STATUS_INSUFFICIENT_RESOURCES;
    }

    IsoBlock->StartFrame = Urb->StartFrame;
    IsoBlock->NumberOfPackets = Urb->NumberOfPackets;
    IsoBlock->TransferFlags = Urb->TransferFlags;
    IsoBlock->ErrorCount = 0;
    IsoBlock->SgList = &Transfer->SgList;

    for (ix = 0; ix < IsoBlock->NumberOfPackets; ix++)
    {
        EndOffset = Urb->IsoPacket[ix].Offset + Urb->IsoPacket[ix].Length;

        if (EndOffset > Urb->TransferBufferLength)
        {
            DPRINT1("USBPORT_InitializeIsoTransfer: packet %lu exceeds buffer\n",
                    ix);
            return USBD_STATUS_INVALID_PARAMETER;
        }

        IsoBlock->Packets[ix].Offset = Urb->IsoPacket[ix].Offset;
        IsoBlock->Packets[ix].Length = Urb->IsoPacket[ix].Length;
        IsoBlock->Packets[ix].ActualLength = 0;
        IsoBlock->Packets[ix].Status = USBD_STATUS_ISO_NOT_ACCESSED_BY_HW;
    }

    return USBD_STATUS_SUCCESS;
}

VOID
NTAPI
USBPORT_SetIsoPacketsStatus(IN PUSBPORT_TRANSFER Transfer,
                            IN USBD_STATUS Status)
{
    PUSBPORT_ISO_BLOCK IsoBlock;
    ULONG ix;

    IsoBlock = Transfer->IsoBlockPtr;

    if (!IsoBlock)
        return;

    for (ix = 0; ix < IsoBlock->NumberOfPackets; ix++)
    {
        if (IsoBlock->Packets[ix].Status == USBD_STATUS_ISO_NOT_ACCESSED_BY_HW)
        {
            IsoBlock->Packets[ix].Status = Status;
            IsoBlock->Packets[ix].ActualLength = 0;
        }
    }

    USBPORT_IsoRecalculateErrors(IsoBlock);
}

VOID
NTAPI
USBPORT_FailIsoTransfer(IN PUSBPORT_TRANSFER Transfer,
                        IN USBD_STATUS Status)
{
    USBPORT_SetIsoPacketsStatus(Transfer, Status);
    Transfer->USBDStatus = Status;
    Transfer->CompletedTransferLen = 0;

    USBPORT_CompleteIsoTransfer(NULL,
                                NULL,
                                &Transfer->TransferParameters,
                                0);
}

VOID
NTAPI
USBPORT_FlushIsoTransfer(IN PUSBPORT_TRANSFER Transfer)
{
    if (!Transfer)
        return;

    /* Treat aborted/canceled ISO transfers as fully failed. */
    USBPORT_FailIsoTransfer(Transfer, USBD_STATUS_CANCELED);
}

VOID
NTAPI
USBPORT_ErrorCompleteIsoTransfer(IN PUSBPORT_TRANSFER Transfer)
{
    if (!Transfer)
        return;

    /* Map miniport submission errors to a generic request failure. */
    USBPORT_FailIsoTransfer(Transfer, USBD_STATUS_REQUEST_FAILED);
}

ULONG
NTAPI
USBPORT_CompleteIsoTransfer(IN PVOID MiniPortExtension,
                            IN PVOID MiniPortEndpoint,
                            IN PVOID TransferParameters,
                            IN ULONG TransferLength)
{
    PUSBPORT_TRANSFER Transfer;
    PUSBPORT_ISO_BLOCK IsoBlock;
    PURB Urb;
    ULONG ix;
    USBD_STATUS Status;

    UNREFERENCED_PARAMETER(MiniPortExtension);
    UNREFERENCED_PARAMETER(MiniPortEndpoint);

    Transfer = CONTAINING_RECORD(TransferParameters,
                                 USBPORT_TRANSFER,
                                 TransferParameters);

    IsoBlock = Transfer->IsoBlockPtr;
    Urb = Transfer->Urb;

    if (IsoBlock && Urb)
    {
        USBPORT_IsoRecalculateErrors(IsoBlock);

        Urb->UrbIsochronousTransfer.StartFrame = IsoBlock->StartFrame;
        Urb->UrbIsochronousTransfer.ErrorCount = IsoBlock->ErrorCount;

        for (ix = 0; ix < IsoBlock->NumberOfPackets; ix++)
        {
            Urb->UrbIsochronousTransfer.IsoPacket[ix].Length =
                IsoBlock->Packets[ix].ActualLength;
            Urb->UrbIsochronousTransfer.IsoPacket[ix].Status =
                IsoBlock->Packets[ix].Status;
        }
    }

    Transfer->CompletedTransferLen = TransferLength;
    Status = Transfer->USBDStatus;

    if (Status == 0)
        Status = USBD_STATUS_SUCCESS;

    USBPORT_CompleteTransfer(Urb, Status);

    return TransferLength;
}
