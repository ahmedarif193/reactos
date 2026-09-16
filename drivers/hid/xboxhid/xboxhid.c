/*
 * PROJECT:     ReactOS HID Stack
 * SPDX-License-Identifier: MIT
 * PURPOSE:     Xbox 360 (XUSB) and Xbox One / Series (GIP) USB gamepad HID minidriver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "xboxhid.h"

static const UCHAR XboxHid_ReportDescriptor[] =
{
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x85, XBOXHID_IG_REPORT_ID,
        0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34, 0x09, 0x32,
        0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x10, 0x95, 0x05, 0x81, 0x02,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x0A, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0A, 0x81, 0x02,
        0x05, 0x01, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
        0x75, 0x01, 0x95, 0x12, 0x81, 0x01,
    0xC0,

    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x85, XBOXHID_XI_REPORT_ID,
        0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34,
        0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x04, 0x81, 0x02,
        0x09, 0x32, 0x09, 0x35, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
        0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
        0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x0A, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0A, 0x81, 0x02,
        0x75, 0x06, 0x95, 0x01, 0x81, 0x01,
        0x06, 0x00, 0xFF, 0x19, 0x01, 0x29, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
        0x75, 0x07, 0x95, 0x01, 0x81, 0x01,

        0x06, 0x0E, 0x00, 0x09, 0x01, 0xA1, 0x02,
            0x85, XBOXHID_XI_REPORT_ID,
            0x15, 0x00, 0x26, 0xFF, 0x7F,
            0x09, 0x10, 0xA1, 0x04, 0x0B, 0x03, 0x00, 0x0A, 0x00, 0x75, 0x10, 0x95, 0x01, 0xB1, 0x42, 0xC0,
            0x09, 0x11, 0xA1, 0x04, 0x0B, 0x03, 0x00, 0x0A, 0x00, 0x75, 0x10, 0x95, 0x01, 0xB1, 0x42, 0xC0,
            0x09, 0x28, 0x66, 0x01, 0x10, 0x55, 0x0D, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0xFF, 0x7F,
            0x75, 0x20, 0x95, 0x01, 0xB1, 0x02, 0x65, 0x00, 0x55, 0x00,
            0x09, 0x23, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x91, 0x02,
        0xC0,

        0x06, 0x0E, 0x00, 0x09, 0x01, 0xA1, 0x02,
            0x85, XBOXHID_XI_REPORT_ID,
            0x15, 0x00, 0x26, 0xFF, 0x7F,
            0x09, 0x10, 0xA1, 0x04, 0x0B, 0x03, 0x00, 0x0A, 0x00, 0x75, 0x10, 0x95, 0x01, 0xB1, 0x42, 0xC0,
            0x09, 0x11, 0xA1, 0x04, 0x0B, 0x03, 0x00, 0x0A, 0x00, 0x75, 0x10, 0x95, 0x01, 0xB1, 0x42, 0xC0,
            0x09, 0x28, 0x66, 0x01, 0x10, 0x55, 0x0D, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0xFF, 0x7F,
            0x75, 0x20, 0x95, 0x01, 0xB1, 0x02, 0x65, 0x00, 0x55, 0x00,
            0x09, 0x23, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x91, 0x02,
        0xC0,
    0xC0,
};

typedef struct _XBOXHID_PRODUCT_NAME
{
    USHORT VendorId;
    USHORT ProductId;
    PCWSTR Name;
} XBOXHID_PRODUCT_NAME, *PXBOXHID_PRODUCT_NAME;

static const XBOXHID_PRODUCT_NAME XboxHid_ProductNames[] =
{
    { 0x045E, 0x028E, L"Xbox 360 Controller" },
    { 0x045E, 0x028F, L"Xbox 360 Controller" },
    { 0x045E, 0x02D1, L"Xbox One Controller" },
    { 0x045E, 0x02DD, L"Xbox One Controller" },
    { 0x045E, 0x02E3, L"Xbox Elite Wireless Controller" },
    { 0x045E, 0x02EA, L"Xbox Wireless Controller" },
    { 0x045E, 0x0719, L"Xbox 360 Wireless Controller" },
    { 0x045E, 0x0B00, L"Xbox Elite Wireless Controller Series 2" },
    { 0x045E, 0x0B0A, L"Xbox Adaptive Controller" },
    { 0x045E, 0x0B12, L"Xbox Wireless Controller" },
};

static
PCWSTR
XboxHid_GetProductName(
    _In_ PXBOXHID_EXTENSION Ext)
{
    ULONG Index;

    if (!Ext->DeviceDescriptor)
        return NULL;

    for (Index = 0; Index < RTL_NUMBER_OF(XboxHid_ProductNames); Index++)
    {
        if (XboxHid_ProductNames[Index].VendorId == Ext->DeviceDescriptor->idVendor &&
            XboxHid_ProductNames[Index].ProductId == Ext->DeviceDescriptor->idProduct)
        {
            return XboxHid_ProductNames[Index].Name;
        }
    }

    return NULL;
}

static
PXBOXHID_EXTENSION
XboxHid_GetExtension(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return ((PHID_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->MiniDeviceExtension;
}

static
NTSTATUS
XboxHid_SubmitUrbSync(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_ PURB Urb)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
                                        Ext->NextDeviceObject,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->Parameters.Others.Argument1 = Urb;

    Status = IoCallDriver(Ext->NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (NT_SUCCESS(Status) && !USBD_SUCCESS(Urb->UrbHeader.Status))
        Status = STATUS_UNSUCCESSFUL;

    return Status;
}

static
NTSTATUS
XboxHid_GetDescriptor(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_ UCHAR Type,
    _In_ UCHAR Index,
    _In_ USHORT LanguageId,
    _In_ ULONG Length,
    _Out_ PVOID *Buffer,
    _Out_ PULONG Transferred)
{
    NTSTATUS Status;
    PURB Urb;

    *Buffer = NULL;
    *Transferred = 0;

    Urb = ExAllocatePoolWithTag(NonPagedPool, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST), XBOXHID_TAG);
    if (!Urb)
        return STATUS_INSUFFICIENT_RESOURCES;

    *Buffer = ExAllocatePoolZero(NonPagedPool, Length, XBOXHID_TAG);
    if (!*Buffer)
    {
        ExFreePoolWithTag(Urb, XBOXHID_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST));
    UsbBuildGetDescriptorRequest(Urb,
                                 sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                 Type,
                                 Index,
                                 LanguageId,
                                 *Buffer,
                                 NULL,
                                 Length,
                                 NULL);

    Status = XboxHid_SubmitUrbSync(Ext, Urb);
    if (NT_SUCCESS(Status))
    {
        *Transferred = Urb->UrbControlDescriptorRequest.TransferBufferLength;
    }
    else
    {
        ExFreePoolWithTag(*Buffer, XBOXHID_TAG);
        *Buffer = NULL;
    }

    ExFreePoolWithTag(Urb, XBOXHID_TAG);
    return Status;
}

static
USHORT
XboxHid_Hat(
    _In_ USHORT Buttons)
{
    BOOLEAN Up = !!(Buttons & XBOXHID_BTN_DPAD_UP);
    BOOLEAN Down = !!(Buttons & XBOXHID_BTN_DPAD_DOWN);
    BOOLEAN Left = !!(Buttons & XBOXHID_BTN_DPAD_LEFT);
    BOOLEAN Right = !!(Buttons & XBOXHID_BTN_DPAD_RIGHT);

    if (Up && Down)
        Up = Down = FALSE;
    if (Left && Right)
        Left = Right = FALSE;

    if (Up)
        return Right ? 2 : (Left ? 8 : 1);
    if (Down)
        return Right ? 4 : (Left ? 6 : 5);
    if (Right)
        return 3;
    if (Left)
        return 7;
    return 0;
}

static
USHORT
XboxHid_ButtonBits(
    _In_ USHORT Buttons)
{
    USHORT Bits = 0;

    if (Buttons & XBOXHID_BTN_A)         Bits |= 0x0001;
    if (Buttons & XBOXHID_BTN_B)         Bits |= 0x0002;
    if (Buttons & XBOXHID_BTN_X)         Bits |= 0x0004;
    if (Buttons & XBOXHID_BTN_Y)         Bits |= 0x0008;
    if (Buttons & XBOXHID_BTN_LSHOULDER) Bits |= 0x0010;
    if (Buttons & XBOXHID_BTN_RSHOULDER) Bits |= 0x0020;
    if (Buttons & XBOXHID_BTN_BACK)      Bits |= 0x0040;
    if (Buttons & XBOXHID_BTN_START)     Bits |= 0x0080;
    if (Buttons & XBOXHID_BTN_LTHUMB)    Bits |= 0x0100;
    if (Buttons & XBOXHID_BTN_RTHUMB)    Bits |= 0x0200;

    return Bits;
}

static
VOID
XboxHid_PutU16(
    _Out_ PUCHAR Buffer,
    _In_ USHORT Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
}

static
VOID
XboxHid_FillIgReport(
    _In_ PXBOXHID_STATE State,
    _Out_ PUCHAR Report)
{
    ULONG Bits;
    LONG Z;

    RtlZeroMemory(Report, XBOXHID_IG_REPORT_LEN);
    Report[0] = XBOXHID_IG_REPORT_ID;
    XboxHid_PutU16(&Report[1], (USHORT)((LONG)State->ThumbLX + 32768));
    XboxHid_PutU16(&Report[3], (USHORT)(32767 - (LONG)State->ThumbLY));
    XboxHid_PutU16(&Report[5], (USHORT)((LONG)State->ThumbRX + 32768));
    XboxHid_PutU16(&Report[7], (USHORT)(32767 - (LONG)State->ThumbRY));
    Z = 0x8000 + ((LONG)(State->LeftTrigger >> 2) - (LONG)(State->RightTrigger >> 2)) * 128;
    XboxHid_PutU16(&Report[9], (USHORT)Z);
    Bits = XboxHid_ButtonBits(State->Buttons) | ((ULONG)XboxHid_Hat(State->Buttons) << 10);
    Report[11] = (UCHAR)Bits;
    Report[12] = (UCHAR)(Bits >> 8);
}

static
VOID
XboxHid_FillXiReport(
    _In_ PXBOXHID_STATE State,
    _Out_ PUCHAR Report)
{
    USHORT Bits;

    RtlZeroMemory(Report, XBOXHID_XI_REPORT_LEN);
    Report[0] = XBOXHID_XI_REPORT_ID;
    XboxHid_PutU16(&Report[1], (USHORT)State->ThumbLX);
    XboxHid_PutU16(&Report[3], (USHORT)State->ThumbLY);
    XboxHid_PutU16(&Report[5], (USHORT)State->ThumbRX);
    XboxHid_PutU16(&Report[7], (USHORT)State->ThumbRY);
    XboxHid_PutU16(&Report[9], State->LeftTrigger);
    XboxHid_PutU16(&Report[11], State->RightTrigger);
    Report[13] = (UCHAR)XboxHid_Hat(State->Buttons);
    Bits = XboxHid_ButtonBits(State->Buttons);
    Report[14] = (UCHAR)Bits;
    Report[15] = (UCHAR)(Bits >> 8);
    Report[16] = (State->Buttons & XBOXHID_BTN_GUIDE) ? 1 : 0;
}

static
BOOLEAN
XboxHid_FillReadIrp(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_ PIRP Irp)
{
    ULONG Length = IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.OutputBufferLength;

    if (Length >= XBOXHID_XI_REPORT_LEN)
    {
        XboxHid_FillXiReport(&Ext->State, Irp->UserBuffer);
        Ext->XiSequence = Ext->StateSequence;
        Irp->IoStatus.Information = XBOXHID_XI_REPORT_LEN;
        return TRUE;
    }

    if (Length >= XBOXHID_IG_REPORT_LEN)
    {
        XboxHid_FillIgReport(&Ext->State, Irp->UserBuffer);
        Ext->IgSequence = Ext->StateSequence;
        Irp->IoStatus.Information = XBOXHID_IG_REPORT_LEN;
        return TRUE;
    }

    return FALSE;
}

static
VOID
XboxHid_FlushReads(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_ NTSTATUS FailStatus)
{
    LIST_ENTRY Done;
    PLIST_ENTRY Entry, Next;
    KIRQL OldIrql;
    PIRP Irp;

    InitializeListHead(&Done);

    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    for (Entry = Ext->PendingReads.Flink; Entry != &Ext->PendingReads; Entry = Next)
    {
        Next = Entry->Flink;
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        if (!IoSetCancelRoutine(Irp, NULL))
            continue;

        RemoveEntryList(Entry);
        if (NT_SUCCESS(FailStatus) && XboxHid_FillReadIrp(Ext, Irp))
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
        }
        else
        {
            Irp->IoStatus.Status = NT_SUCCESS(FailStatus) ? STATUS_BUFFER_TOO_SMALL : FailStatus;
            Irp->IoStatus.Information = 0;
        }
        InsertTailList(&Done, Entry);
    }
    KeReleaseSpinLock(&Ext->Lock, OldIrql);

    while (!IsListEmpty(&Done))
    {
        Entry = RemoveHeadList(&Done);
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
VOID
NTAPI
XboxHid_CancelRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PXBOXHID_EXTENSION Ext = Irp->Tail.Overlay.DriverContext[0];
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Ext->Lock, OldIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
XboxHid_ReadReport(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_ PIRP Irp)
{
    ULONG Length = IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status;
    KIRQL OldIrql;
    ULONG Delivered;

    if (!Irp->UserBuffer || Length < XBOXHID_IG_REPORT_LEN)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Complete;
    }

    KeAcquireSpinLock(&Ext->Lock, &OldIrql);

    if (Ext->Removed || !Ext->Running)
    {
        KeReleaseSpinLock(&Ext->Lock, OldIrql);
        Status = STATUS_DEVICE_NOT_CONNECTED;
        goto Complete;
    }

    Delivered = (Length >= XBOXHID_XI_REPORT_LEN) ? Ext->XiSequence : Ext->IgSequence;
    if (Delivered != Ext->StateSequence)
    {
        XboxHid_FillReadIrp(Ext, Irp);
        KeReleaseSpinLock(&Ext->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    Irp->Tail.Overlay.DriverContext[0] = Ext;
    IoMarkIrpPending(Irp);
    InsertTailList(&Ext->PendingReads, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, XboxHid_CancelRead);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&Ext->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }
    KeReleaseSpinLock(&Ext->Lock, OldIrql);
    return STATUS_PENDING;

Complete:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
VOID
XboxHid_FinishWrite(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_ BOOLEAN Submission,
    _In_ BOOLEAN Completion)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    if (Submission && --Ext->WritesSubmitting == 0)
        KeSetEvent(&Ext->WriteSubmitIdle, IO_NO_INCREMENT, FALSE);
    if (Completion && --Ext->PendingWrites == 0)
        KeSetEvent(&Ext->WriteIdle, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Ext->Lock, OldIrql);
}

static
NTSTATUS
NTAPI
XboxHid_WriteComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PXBOXHID_EXTENSION Ext = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    ExFreePoolWithTag(Irp->Tail.Overlay.DriverContext[0], XBOXHID_TAG);
    IoFreeIrp(Irp);
    XboxHid_FinishWrite(Ext, FALSE, TRUE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
XboxHid_Write(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PIO_STACK_LOCATION Stack;
    KIRQL OldIrql;
    PUCHAR Buffer;
    PURB Urb;
    PIRP Irp;

    /* Reserve the submission before touching resources owned by Stop. */
    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    if (!Ext->OutPipe || !Ext->Running || Ext->Removed)
    {
        KeReleaseSpinLock(&Ext->Lock, OldIrql);
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    if (Ext->WritesSubmitting++ == 0)
        KeClearEvent(&Ext->WriteSubmitIdle);
    if (Ext->PendingWrites++ == 0)
        KeClearEvent(&Ext->WriteIdle);
    KeReleaseSpinLock(&Ext->Lock, OldIrql);

    Urb = ExAllocatePoolZero(NonPagedPool, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER) + Length, XBOXHID_TAG);
    if (!Urb)
    {
        XboxHid_FinishWrite(Ext, TRUE, TRUE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp = IoAllocateIrp(Ext->NextDeviceObject->StackSize, FALSE);
    if (!Irp)
    {
        ExFreePoolWithTag(Urb, XBOXHID_TAG);
        XboxHid_FinishWrite(Ext, TRUE, TRUE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Buffer = (PUCHAR)Urb + sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER);
    RtlCopyMemory(Buffer, Data, Length);
    UsbBuildInterruptOrBulkTransferRequest(Urb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Ext->OutPipe,
                                           Buffer,
                                           NULL,
                                           Length,
                                           USBD_TRANSFER_DIRECTION_OUT,
                                           NULL);

    Irp->Tail.Overlay.DriverContext[0] = Urb;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    Stack->Parameters.Others.Argument1 = Urb;
    IoSetCompletionRoutine(Irp, XboxHid_WriteComplete, Ext, TRUE, TRUE, TRUE);

    IoCallDriver(Ext->NextDeviceObject, Irp);
    /* Completion may have run inline; do not access Irp or Urb here. */
    XboxHid_FinishWrite(Ext, TRUE, FALSE);
    return STATUS_SUCCESS;
}

static
GIP_RESULT
XboxHid_GipSend(void *Context, const uint8_t *Data, size_t Length)
{
    return NT_SUCCESS(XboxHid_Write(Context, Data, (ULONG)Length)) ? GipSuccess : GipTransportError;
}

/* Called with GipLock held. Never complete HID reads from this callback:
 * completion can reenter the output path, which also acquires GipLock. */
static
GIP_RESULT
XboxHid_GipMessage(void *Context, const GIP_PACKET *Message)
{
    PXBOXHID_EXTENSION Ext = Context;
    GIP_GAMEPAD Gamepad;
    XBOXHID_STATE State;
    GIP_RESULT Result;
    KIRQL OldIrql;
    uint8_t Pressed;

    /* Audio and expansion-device messages belong to separate consumers. */
    if (Message->Flags & GIP_FLAG_DEVICE)
        return GipSuccess;

    if ((Message->Flags & GIP_FLAG_SYSTEM) && Message->Type == GIP_MESSAGE_HELLO)
    {
        static const uint8_t SecurityComplete[] = { 0x01, 0x00 };

        Ext->GipReady = FALSE;
        Result = GipSetDeviceState(&Ext->Gip, 0, GIP_STATE_START);
        if (Result != GipSuccess)
            return Result;

        Ext->GipReady = TRUE;
        if (Ext->DeviceDescriptor->idVendor == 0x045E &&
            (Ext->DeviceDescriptor->idProduct == 0x02EA || Ext->DeviceDescriptor->idProduct == 0x0B00))
        {
            /* Preserve the existing One S / Elite firmware workaround. Its
             * declared length differs from its USB length, so keep this
             * device-specific exception outside the generic GIP encoder. */
            uint8_t Quirk[] = { GIP_MESSAGE_STATE, GIP_FLAG_SYSTEM, 0, 0x0F, 0x06 };
            if (GipAllocateSequence(&Ext->Gip, 0, GIP_MESSAGE_STATE,
                                    GIP_FLAG_SYSTEM, &Quirk[2]) == GipSuccess)
                XboxHid_Write(Ext, Quirk, sizeof(Quirk));
        }
        GipSetLed(&Ext->Gip, 0, 1, 20);
        /* Retain the PC initialization completion used by this driver.
         * This is not an implementation of console authentication. */
        GipSendMessage(&Ext->Gip, 0, GIP_MESSAGE_SECURITY, GIP_FLAG_SYSTEM,
                       SecurityComplete, sizeof(SecurityComplete));
        return GipSuccess;
    }

    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    State = Ext->State;
    KeReleaseSpinLock(&Ext->Lock, OldIrql);

    if (Message->Type == GIP_MESSAGE_INPUT && !(Message->Flags & GIP_FLAG_SYSTEM))
    {
        Result = GipDecodeGamepad(Message, &Gamepad);
        if (Result != GipSuccess)
            return Result;
        State.Buttons = Gamepad.Buttons | (State.Buttons & XBOXHID_BTN_GUIDE);
        State.LeftTrigger = Gamepad.LeftTrigger;
        State.RightTrigger = Gamepad.RightTrigger;
        State.ThumbLX = Gamepad.LeftX;
        State.ThumbLY = Gamepad.LeftY;
        State.ThumbRX = Gamepad.RightX;
        State.ThumbRY = Gamepad.RightY;
    }
    else if (Message->Type == GIP_MESSAGE_GUIDE && (Message->Flags & GIP_FLAG_SYSTEM))
    {
        Result = GipDecodeGuide(Message, &Pressed);
        if (Result != GipSuccess)
            return Result;
        if (Pressed)
            State.Buttons |= XBOXHID_BTN_GUIDE;
        else
            State.Buttons &= ~XBOXHID_BTN_GUIDE;
    }
    else
    {
        return GipSuccess;
    }

    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    if (!RtlEqualMemory(&Ext->State, &State, sizeof(State)))
    {
        Ext->State = State;
        Ext->StateSequence++;
    }
    KeReleaseSpinLock(&Ext->Lock, OldIrql);
    return GipSuccess;
}

static
NTSTATUS
XboxHid_SendRumble(
    _In_ PXBOXHID_EXTENSION Ext)
{
    if (Ext->Type == XBOXHID_TYPE_XUSB)
    {
        UCHAR Packet[8] = { 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

        Packet[3] = (UCHAR)(Ext->RumbleIntensity >> 8);
        Packet[4] = (UCHAR)(Ext->BuzzIntensity >> 8);
        return XboxHid_Write(Ext, Packet, sizeof(Packet));
    }
    else
    {
        GIP_RESULT Result;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Ext->GipLock, &OldIrql);
        Result = Ext->GipReady ?
            GipSetMotors(&Ext->Gip, 0, 0x03, 0, 0, Ext->RumbleIntensity,
                         Ext->BuzzIntensity, 0xFF, 0, 0xFF) : GipNotReady;
        KeReleaseSpinLock(&Ext->GipLock, OldIrql);
        if (Result == GipNotReady)
            return STATUS_DEVICE_NOT_READY;
        return Result == GipSuccess ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }
}

static
VOID
XboxHid_ProcessPacket(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length)
{
    XBOXHID_STATE State;
    KIRQL OldIrql;
    ULONG Sequence;

    if (Ext->Type == XBOXHID_TYPE_GIP)
    {
        GIP_RESULT Result;

        KeAcquireSpinLock(&Ext->GipLock, &OldIrql);
        Sequence = Ext->StateSequence;
        Result = GipReceive(&Ext->Gip, Data, Length, (uint32_t)(KeQueryInterruptTime() / 10000));
        KeReleaseSpinLock(&Ext->GipLock, OldIrql);
        if (Result != GipSuccess)
            DPRINT1("[XBOXHID] GipReceive failed %u\n", Result);
        if (Sequence != Ext->StateSequence)
            XboxHid_FlushReads(Ext, STATUS_SUCCESS);
        return;
    }

    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    State = Ext->State;
    KeReleaseSpinLock(&Ext->Lock, OldIrql);

    if (Ext->Type == XBOXHID_TYPE_XUSB)
    {
        if (Length < 14 || Data[0] != 0x00 || Data[1] < 0x14)
            return;

        State.Buttons = (USHORT)(Data[2] | (Data[3] << 8)) & ~0x0800;
        State.LeftTrigger = (USHORT)((Data[4] << 2) | (Data[4] >> 6));
        State.RightTrigger = (USHORT)((Data[5] << 2) | (Data[5] >> 6));
        State.ThumbLX = (SHORT)(Data[6] | (Data[7] << 8));
        State.ThumbLY = (SHORT)(Data[8] | (Data[9] << 8));
        State.ThumbRX = (SHORT)(Data[10] | (Data[11] << 8));
        State.ThumbRY = (SHORT)(Data[12] | (Data[13] << 8));
    }
    else
    {
        return;
    }

    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    if (RtlEqualMemory(&Ext->State, &State, sizeof(State)))
    {
        KeReleaseSpinLock(&Ext->Lock, OldIrql);
        return;
    }
    Ext->State = State;
    Ext->StateSequence++;
    KeReleaseSpinLock(&Ext->Lock, OldIrql);

    XboxHid_FlushReads(Ext, STATUS_SUCCESS);
}

static
VOID
XboxHid_SubmitRead(
    _In_ PXBOXHID_EXTENSION Ext);

static
VOID
NTAPI
XboxHid_ResetWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PXBOXHID_EXTENSION Ext = Context;
    LARGE_INTEGER Delay;
    PURB Urb;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Ext->Running && !Ext->Removed)
    {
        Urb = ExAllocatePoolZero(NonPagedPool, sizeof(struct _URB_PIPE_REQUEST), XBOXHID_TAG);
        if (Urb)
        {
            Urb->UrbHeader.Function = URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
            Urb->UrbHeader.Length = sizeof(struct _URB_PIPE_REQUEST);
            Urb->UrbPipeRequest.PipeHandle = Ext->InPipe;
            XboxHid_SubmitUrbSync(Ext, Urb);
            ExFreePoolWithTag(Urb, XBOXHID_TAG);
        }

        Delay.QuadPart = -100 * 10000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    if (Ext->Running && !Ext->Removed)
        XboxHid_SubmitRead(Ext);
    else
        KeSetEvent(&Ext->ReadIdle, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
NTAPI
XboxHid_ReadComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PXBOXHID_EXTENSION Ext = Context;
    USBD_STATUS UrbStatus = Ext->ReadUrb->UrbHeader.Status;
    NTSTATUS Status = Irp->IoStatus.Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (NT_SUCCESS(Status) && USBD_SUCCESS(UrbStatus))
    {
        XboxHid_ProcessPacket(Ext,
                              Ext->ReadBuffer,
                              Ext->ReadUrb->UrbBulkOrInterruptTransfer.TransferBufferLength);
        if (Ext->Running && !Ext->Removed)
        {
            XboxHid_SubmitRead(Ext);
            return STATUS_MORE_PROCESSING_REQUIRED;
        }
    }
    else if (Status == STATUS_DEVICE_NOT_CONNECTED || UrbStatus == USBD_STATUS_DEVICE_GONE)
    {
        InterlockedExchange(&Ext->Removed, 1);
    }
    else if (Status != STATUS_CANCELLED && Ext->Running && !Ext->Removed)
    {
        IoQueueWorkItem(Ext->ResetWorkItem, XboxHid_ResetWorker, DelayedWorkQueue, Ext);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (Ext->Removed)
        XboxHid_FlushReads(Ext, STATUS_DEVICE_NOT_CONNECTED);
    KeSetEvent(&Ext->ReadIdle, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
XboxHid_SubmitRead(
    _In_ PXBOXHID_EXTENSION Ext)
{
    PIO_STACK_LOCATION Stack;

    IoReuseIrp(Ext->ReadIrp, STATUS_SUCCESS);
    RtlZeroMemory(Ext->ReadUrb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER));
    UsbBuildInterruptOrBulkTransferRequest(Ext->ReadUrb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Ext->InPipe,
                                           Ext->ReadBuffer,
                                           NULL,
                                           sizeof(Ext->ReadBuffer),
                                           USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                           NULL);

    Stack = IoGetNextIrpStackLocation(Ext->ReadIrp);
    Stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    Stack->Parameters.Others.Argument1 = Ext->ReadUrb;
    IoSetCompletionRoutine(Ext->ReadIrp, XboxHid_ReadComplete, Ext, TRUE, TRUE, TRUE);

    IoCallDriver(Ext->NextDeviceObject, Ext->ReadIrp);
}

static
VOID
XboxHid_Stop(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_ BOOLEAN Unconfigure)
{
    LARGE_INTEGER Timeout;
    struct _URB_PIPE_REQUEST AbortUrb;
    BOOLEAN WritesPending;
    KIRQL OldIrql;
    PURB Urb;

    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    InterlockedExchange(&Ext->Running, 0);
    KeReleaseSpinLock(&Ext->Lock, OldIrql);

    /* No new writes can enter. Let every accepted submission reach USB first. */
    KeWaitForSingleObject(&Ext->WriteSubmitIdle, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    WritesPending = Ext->PendingWrites != 0;
    KeReleaseSpinLock(&Ext->Lock, OldIrql);

    if (WritesPending)
    {
        RtlZeroMemory(&AbortUrb, sizeof(AbortUrb));
        AbortUrb.Hdr.Function = URB_FUNCTION_ABORT_PIPE;
        AbortUrb.Hdr.Length = sizeof(AbortUrb);
        AbortUrb.PipeHandle = Ext->OutPipe;
        XboxHid_SubmitUrbSync(Ext, (PURB)&AbortUrb);
    }

    /* Even a failed abort must not allow teardown with live completions. */
    KeWaitForSingleObject(&Ext->WriteIdle, Executive, KernelMode, FALSE, NULL);
    /* Pair with FinishWrite's lock release after it signals the idle events. */
    KeAcquireSpinLock(&Ext->Lock, &OldIrql);
    ASSERT(Ext->WritesSubmitting == 0 && Ext->PendingWrites == 0);
    KeReleaseSpinLock(&Ext->Lock, OldIrql);

    if (Ext->ReadIrp)
    {
        Timeout.QuadPart = -50 * 10000LL;
        while (KeWaitForSingleObject(&Ext->ReadIdle, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
            IoCancelIrp(Ext->ReadIrp);
    }

    XboxHid_FlushReads(Ext, STATUS_DEVICE_NOT_CONNECTED);

    KeAcquireSpinLock(&Ext->GipLock, &OldIrql);
    GipReset(&Ext->Gip);
    Ext->GipReady = FALSE;
    KeReleaseSpinLock(&Ext->GipLock, OldIrql);

    if (Unconfigure && !Ext->Removed && Ext->ConfigurationHandle)
    {
        Urb = ExAllocatePoolZero(NonPagedPool, sizeof(struct _URB_SELECT_CONFIGURATION), XBOXHID_TAG);
        if (Urb)
        {
            UsbBuildSelectConfigurationRequest(Urb, sizeof(struct _URB_SELECT_CONFIGURATION), NULL);
            XboxHid_SubmitUrbSync(Ext, Urb);
            ExFreePoolWithTag(Urb, XBOXHID_TAG);
        }
    }

    Ext->ConfigurationHandle = NULL;
    Ext->InPipe = NULL;
    Ext->OutPipe = NULL;

    if (Ext->ReadIrp)
    {
        IoFreeIrp(Ext->ReadIrp);
        Ext->ReadIrp = NULL;
    }
    if (Ext->ReadUrb)
    {
        ExFreePoolWithTag(Ext->ReadUrb, XBOXHID_TAG);
        Ext->ReadUrb = NULL;
    }
    if (Ext->ResetWorkItem)
    {
        IoFreeWorkItem(Ext->ResetWorkItem);
        Ext->ResetWorkItem = NULL;
    }
    if (Ext->InterfaceInfo)
    {
        ExFreePoolWithTag(Ext->InterfaceInfo, XBOXHID_TAG);
        Ext->InterfaceInfo = NULL;
    }
    if (Ext->ConfigurationDescriptor)
    {
        ExFreePoolWithTag(Ext->ConfigurationDescriptor, XBOXHID_TAG);
        Ext->ConfigurationDescriptor = NULL;
    }
    if (Ext->DeviceDescriptor)
    {
        ExFreePoolWithTag(Ext->DeviceDescriptor, XBOXHID_TAG);
        Ext->DeviceDescriptor = NULL;
    }
}

static
NTSTATUS
XboxHid_Start(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PXBOXHID_EXTENSION Ext = XboxHid_GetExtension(DeviceObject);
    PHID_DEVICE_EXTENSION HidExt = DeviceObject->DeviceExtension;
    PUSBD_INTERFACE_LIST_ENTRY InterfaceList = NULL;
    PUSB_INTERFACE_DESCRIPTOR Interface;
    PUSB_CONFIGURATION_DESCRIPTOR Config;
    PUSBD_PIPE_INFORMATION Pipe;
    ULONG Transferred, Index;
    NTSTATUS Status;
    PURB Urb;

    Ext->NextDeviceObject = HidExt->NextDeviceObject;
    Ext->Removed = 0;

    Status = XboxHid_GetDescriptor(Ext, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, sizeof(USB_DEVICE_DESCRIPTOR),
                                   (PVOID *)&Ext->DeviceDescriptor, &Transferred);
    if (!NT_SUCCESS(Status))
        goto Fail;
    if (Transferred < sizeof(USB_DEVICE_DESCRIPTOR))
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Fail;
    }

    Status = XboxHid_GetDescriptor(Ext, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, sizeof(USB_CONFIGURATION_DESCRIPTOR),
                                   (PVOID *)&Config, &Transferred);
    if (!NT_SUCCESS(Status))
        goto Fail;
    if (Transferred < sizeof(USB_CONFIGURATION_DESCRIPTOR))
    {
        ExFreePoolWithTag(Config, XBOXHID_TAG);
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Fail;
    }
    Index = Config->wTotalLength;
    ExFreePoolWithTag(Config, XBOXHID_TAG);
    if (Index < sizeof(USB_CONFIGURATION_DESCRIPTOR))
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Fail;
    }

    Status = XboxHid_GetDescriptor(Ext, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, Index,
                                   (PVOID *)&Ext->ConfigurationDescriptor, &Transferred);
    if (!NT_SUCCESS(Status))
        goto Fail;
    if (Transferred < Index)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Fail;
    }
    Config = Ext->ConfigurationDescriptor;
    if (Config->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE ||
        Config->bLength < sizeof(*Config) || Config->bLength > Index ||
        Config->wTotalLength != Index || !Config->bNumInterfaces)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Fail;
    }

    Interface = USBD_ParseConfigurationDescriptorEx(Ext->ConfigurationDescriptor, Ext->ConfigurationDescriptor,
                                                   -1, 0, USB_DEVICE_CLASS_VENDOR_SPECIFIC, 0x5D, 0x01);
    if (Interface)
    {
        Ext->Type = XBOXHID_TYPE_XUSB;
    }
    else
    {
        Interface = USBD_ParseConfigurationDescriptorEx(Ext->ConfigurationDescriptor, Ext->ConfigurationDescriptor,
                                                       -1, 0, USB_DEVICE_CLASS_VENDOR_SPECIFIC, 0x47, 0xD0);
        Ext->Type = XBOXHID_TYPE_GIP;
    }
    if (!Interface)
    {
        DPRINT1("[XBOXHID] no XUSB/GIP interface on %04x:%04x\n",
                Ext->DeviceDescriptor->idVendor, Ext->DeviceDescriptor->idProduct);
        Status = STATUS_NOT_SUPPORTED;
        goto Fail;
    }

    /*
     * We own the whole USB device, not a USBCCGP interface PDO. USBPORT
     * requires one entry for every interface in the configuration, including
     * unused audio/accessory interfaces. Select alternate setting zero for
     * each, but retain only the gamepad interface's pipes below.
     */
    if (Interface->bInterfaceNumber >= Config->bNumInterfaces)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Fail;
    }
    InterfaceList = ExAllocatePoolZero(NonPagedPool,
                                       (Config->bNumInterfaces + 1) * sizeof(*InterfaceList),
                                       XBOXHID_TAG);
    if (!InterfaceList)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }
    for (Index = 0; Index < Config->bNumInterfaces; Index++)
    {
        InterfaceList[Index].InterfaceDescriptor =
            USBD_ParseConfigurationDescriptorEx(Config, Config, Index, 0, -1, -1, -1);
        if (!InterfaceList[Index].InterfaceDescriptor)
        {
            DPRINT1("[XBOXHID] missing default setting for interface %lu\n", Index);
            Status = STATUS_DEVICE_DATA_ERROR;
            goto Fail;
        }
    }
    Urb = USBD_CreateConfigurationRequestEx(Ext->ConfigurationDescriptor, InterfaceList);
    if (!Urb)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }

    Status = XboxHid_SubmitUrbSync(Ext, Urb);
    if (NT_SUCCESS(Status))
    {
        PUSBD_INTERFACE_INFORMATION GamepadInfo = InterfaceList[Interface->bInterfaceNumber].Interface;

        Ext->ConfigurationHandle = Urb->UrbSelectConfiguration.ConfigurationHandle;
        Ext->InterfaceInfo = ExAllocatePoolWithTag(NonPagedPool, GamepadInfo->Length, XBOXHID_TAG);
        if (Ext->InterfaceInfo)
            RtlCopyMemory(Ext->InterfaceInfo, GamepadInfo, GamepadInfo->Length);
        else
            Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    ExFreePool(Urb);
    ExFreePoolWithTag(InterfaceList, XBOXHID_TAG);
    InterfaceList = NULL;
    if (!NT_SUCCESS(Status))
        goto Fail;

    for (Index = 0; Index < Ext->InterfaceInfo->NumberOfPipes; Index++)
    {
        Pipe = &Ext->InterfaceInfo->Pipes[Index];
        if (Pipe->PipeType != UsbdPipeTypeInterrupt)
            continue;
        if ((Pipe->EndpointAddress & USB_ENDPOINT_DIRECTION_MASK) && !Ext->InPipe)
        {
            Ext->InPipe = Pipe->PipeHandle;
            Ext->InMaxPacket = Pipe->MaximumPacketSize;
        }
        else if (!(Pipe->EndpointAddress & USB_ENDPOINT_DIRECTION_MASK) && !Ext->OutPipe)
        {
            Ext->OutPipe = Pipe->PipeHandle;
        }
    }
    if (!Ext->InPipe || (Ext->Type == XBOXHID_TYPE_GIP && !Ext->OutPipe))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Fail;
    }

    Ext->ReadIrp = IoAllocateIrp(Ext->NextDeviceObject->StackSize, FALSE);
    Ext->ReadUrb = ExAllocatePoolZero(NonPagedPool, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER), XBOXHID_TAG);
    Ext->ResetWorkItem = IoAllocateWorkItem(DeviceObject);
    if (!Ext->ReadIrp || !Ext->ReadUrb || !Ext->ResetWorkItem)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }

    InterlockedExchange(&Ext->Running, 1);
    KeClearEvent(&Ext->ReadIdle);
    XboxHid_SubmitRead(Ext);

    if (Ext->Type == XBOXHID_TYPE_XUSB)
    {
        static const UCHAR Led[] = { 0x01, 0x03, 0x02 };
        UCHAR Magic[20];

        Urb = ExAllocatePoolZero(NonPagedPool, sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST), XBOXHID_TAG);
        if (Urb)
        {
            UsbBuildVendorRequest(Urb,
                                  URB_FUNCTION_VENDOR_INTERFACE,
                                  sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                                  USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                  0,
                                  0x01,
                                  0x0100,
                                  0x0000,
                                  Magic,
                                  NULL,
                                  sizeof(Magic),
                                  NULL);
            XboxHid_SubmitUrbSync(Ext, Urb);
            ExFreePoolWithTag(Urb, XBOXHID_TAG);
        }

        XboxHid_Write(Ext, Led, sizeof(Led));
    }
    /* GIP initialization is driven by Hello in XboxHid_GipMessage. */

    return STATUS_SUCCESS;

Fail:
    DPRINT1("[XBOXHID] start failed %lx\n", Status);
    if (InterfaceList)
        ExFreePoolWithTag(InterfaceList, XBOXHID_TAG);
    XboxHid_Stop(Ext, TRUE);
    return Status;
}

static
NTSTATUS
XboxHid_GetString(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Request = PtrToUlong(Stack->Parameters.DeviceIoControl.Type3InputBuffer);
    ULONG OutLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUSB_STRING_DESCRIPTOR Descriptor;
    UNICODE_STRING ProductName;
    USHORT LanguageId = (USHORT)(Request >> 16);
    ULONG Transferred, Length;
    PVOID Output;
    NTSTATUS Status;
    UCHAR Index;

    RtlInitUnicodeString(&ProductName, NULL);

    Irp->IoStatus.Information = 0;

    if (!Ext->DeviceDescriptor)
        return STATUS_DEVICE_NOT_READY;

    if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_HID_GET_STRING)
    {
        switch ((USHORT)Request)
        {
            case HID_STRING_ID_IMANUFACTURER: Index = Ext->DeviceDescriptor->iManufacturer; break;
            case HID_STRING_ID_IPRODUCT:
                RtlInitUnicodeString(&ProductName, XboxHid_GetProductName(Ext));
                Index = Ext->DeviceDescriptor->iProduct;
                break;
            case HID_STRING_ID_ISERIALNUMBER: Index = Ext->DeviceDescriptor->iSerialNumber; break;
            default:                          return STATUS_INVALID_PARAMETER;
        }
    }
    else
    {
        Index = (UCHAR)Request;
    }

    Output = Irp->UserBuffer;
    if (!Output && Irp->MdlAddress)
        Output = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (!Output || OutLength < sizeof(WCHAR))
        return STATUS_INVALID_BUFFER_SIZE;

    /* xboxhid exposes a synthetic HID device. Give known Microsoft pads a
     * useful HID product name instead of propagating the literal USB string
     * "Controller" used by their GIP firmware. All HID clients then observe
     * the same identity, including DirectInput and Windows.Gaming.Input. */
    if (ProductName.Buffer)
    {
        if (OutLength < ProductName.Length + sizeof(WCHAR))
            return STATUS_BUFFER_TOO_SMALL;

        RtlCopyMemory(Output, ProductName.Buffer, ProductName.Length);
        ((PWCHAR)Output)[ProductName.Length / sizeof(WCHAR)] = UNICODE_NULL;
        Irp->IoStatus.Information = ProductName.Length + sizeof(WCHAR);
        return STATUS_SUCCESS;
    }

    if (!Index)
        return STATUS_NOT_FOUND;

    if (!LanguageId)
        LanguageId = 0x0409;

    Status = XboxHid_GetDescriptor(Ext, USB_STRING_DESCRIPTOR_TYPE, Index, LanguageId, MAXIMUM_USB_STRING_LENGTH,
                                   (PVOID *)&Descriptor, &Transferred);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Transferred < FIELD_OFFSET(USB_STRING_DESCRIPTOR, bString) ||
        Descriptor->bLength < FIELD_OFFSET(USB_STRING_DESCRIPTOR, bString) ||
        Descriptor->bLength > Transferred ||
        Descriptor->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
    }
    else
    {
        Length = (Descriptor->bLength - FIELD_OFFSET(USB_STRING_DESCRIPTOR, bString)) & ~1UL;
        if (OutLength < Length + sizeof(WCHAR))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            RtlCopyMemory(Output, Descriptor->bString, Length);
            ((PWCHAR)Output)[Length / sizeof(WCHAR)] = UNICODE_NULL;
            Irp->IoStatus.Information = Length + sizeof(WCHAR);
        }
    }

    ExFreePoolWithTag(Descriptor, XBOXHID_TAG);
    return Status;
}

static
NTSTATUS
XboxHid_XferRequest(
    _In_ PXBOXHID_EXTENSION Ext,
    _In_ PIRP Irp,
    _In_ ULONG IoControlCode)
{
    PHID_XFER_PACKET Packet = Irp->UserBuffer;
    PUCHAR Buffer;
    KIRQL OldIrql;

    Irp->IoStatus.Information = 0;

    if (!Packet || !Packet->reportBuffer)
        return STATUS_INVALID_PARAMETER;
    Buffer = Packet->reportBuffer;

    switch (IoControlCode)
    {
        case IOCTL_HID_WRITE_REPORT:
        case IOCTL_HID_SET_OUTPUT_REPORT:
            if (Packet->reportId != XBOXHID_XI_REPORT_ID || Packet->reportBufferLen < XBOXHID_OUTPUT_LEN)
                return STATUS_INVALID_PARAMETER;
            Ext->RumbleIntensity = (USHORT)(Buffer[1] | (Buffer[2] << 8));
            Ext->BuzzIntensity = (USHORT)(Buffer[3] | (Buffer[4] << 8));
            Irp->IoStatus.Information = XBOXHID_OUTPUT_LEN;
            return XboxHid_SendRumble(Ext);

        case IOCTL_HID_GET_FEATURE:
            if (Packet->reportId != XBOXHID_XI_REPORT_ID || Packet->reportBufferLen < XBOXHID_FEATURE_LEN)
                return STATUS_INVALID_PARAMETER;
            RtlZeroMemory(Buffer, XBOXHID_FEATURE_LEN);
            Buffer[0] = XBOXHID_XI_REPORT_ID;
            XboxHid_PutU16(&Buffer[1], HID_USAGE_HAPTICS_WAVEFORM_RUMBLE);
            XboxHid_PutU16(&Buffer[5], (USHORT)Ext->RumbleCutoff);
            XboxHid_PutU16(&Buffer[7], (USHORT)(Ext->RumbleCutoff >> 16));
            XboxHid_PutU16(&Buffer[9], HID_USAGE_HAPTICS_WAVEFORM_BUZZ);
            XboxHid_PutU16(&Buffer[13], (USHORT)Ext->BuzzCutoff);
            XboxHid_PutU16(&Buffer[15], (USHORT)(Ext->BuzzCutoff >> 16));
            Packet->reportBufferLen = XBOXHID_FEATURE_LEN;
            Irp->IoStatus.Information = XBOXHID_FEATURE_LEN;
            return STATUS_SUCCESS;

        case IOCTL_HID_SET_FEATURE:
            if (Packet->reportId != XBOXHID_XI_REPORT_ID || Packet->reportBufferLen < XBOXHID_FEATURE_LEN)
                return STATUS_INVALID_PARAMETER;
            Ext->RumbleCutoff = Buffer[5] | (Buffer[6] << 8) | (Buffer[7] << 16) | ((ULONG)Buffer[8] << 24);
            Ext->BuzzCutoff = Buffer[13] | (Buffer[14] << 8) | (Buffer[15] << 16) | ((ULONG)Buffer[16] << 24);
            Irp->IoStatus.Information = XBOXHID_FEATURE_LEN;
            return STATUS_SUCCESS;

        case IOCTL_HID_GET_INPUT_REPORT:
            KeAcquireSpinLock(&Ext->Lock, &OldIrql);
            if (Packet->reportId == XBOXHID_IG_REPORT_ID && Packet->reportBufferLen >= XBOXHID_IG_REPORT_LEN)
            {
                XboxHid_FillIgReport(&Ext->State, Buffer);
                Packet->reportBufferLen = XBOXHID_IG_REPORT_LEN;
            }
            else if (Packet->reportId == XBOXHID_XI_REPORT_ID && Packet->reportBufferLen >= XBOXHID_XI_REPORT_LEN)
            {
                XboxHid_FillXiReport(&Ext->State, Buffer);
                Packet->reportBufferLen = XBOXHID_XI_REPORT_LEN;
            }
            else
            {
                KeReleaseSpinLock(&Ext->Lock, OldIrql);
                return STATUS_INVALID_PARAMETER;
            }
            KeReleaseSpinLock(&Ext->Lock, OldIrql);
            Irp->IoStatus.Information = Packet->reportBufferLen;
            return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
XboxHid_InternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PXBOXHID_EXTENSION Ext = XboxHid_GetExtension(DeviceObject);
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG OutLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    PHID_DEVICE_ATTRIBUTES Attributes;
    PHID_DESCRIPTOR HidDescriptor;
    NTSTATUS Status;

    switch (Code)
    {
        case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
            if (OutLength < sizeof(HID_DESCRIPTOR))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Irp->IoStatus.Information = 0;
                break;
            }
            HidDescriptor = Irp->UserBuffer;
            RtlZeroMemory(HidDescriptor, sizeof(HID_DESCRIPTOR));
            HidDescriptor->bLength = sizeof(HID_DESCRIPTOR);
            HidDescriptor->bDescriptorType = HID_HID_DESCRIPTOR_TYPE;
            HidDescriptor->bcdHID = 0x0111;
            HidDescriptor->bNumDescriptors = 1;
            HidDescriptor->DescriptorList[0].bReportType = HID_REPORT_DESCRIPTOR_TYPE;
            HidDescriptor->DescriptorList[0].wReportLength = sizeof(XboxHid_ReportDescriptor);
            Irp->IoStatus.Information = sizeof(HID_DESCRIPTOR);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_HID_GET_REPORT_DESCRIPTOR:
            if (OutLength < sizeof(XboxHid_ReportDescriptor))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Irp->IoStatus.Information = 0;
                break;
            }
            RtlCopyMemory(Irp->UserBuffer, XboxHid_ReportDescriptor, sizeof(XboxHid_ReportDescriptor));
            Irp->IoStatus.Information = sizeof(XboxHid_ReportDescriptor);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
            if (OutLength < sizeof(HID_DEVICE_ATTRIBUTES) || !Ext->DeviceDescriptor)
            {
                Status = STATUS_INVALID_BUFFER_SIZE;
                Irp->IoStatus.Information = 0;
                break;
            }
            Attributes = Irp->UserBuffer;
            RtlZeroMemory(Attributes, sizeof(HID_DEVICE_ATTRIBUTES));
            Attributes->Size = sizeof(HID_DEVICE_ATTRIBUTES);
            Attributes->VendorID = Ext->DeviceDescriptor->idVendor;
            Attributes->ProductID = Ext->DeviceDescriptor->idProduct;
            Attributes->VersionNumber = Ext->DeviceDescriptor->bcdDevice;
            Irp->IoStatus.Information = sizeof(HID_DEVICE_ATTRIBUTES);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_HID_READ_REPORT:
            return XboxHid_ReadReport(Ext, Irp);

        case IOCTL_HID_WRITE_REPORT:
        case IOCTL_HID_SET_OUTPUT_REPORT:
        case IOCTL_HID_GET_FEATURE:
        case IOCTL_HID_SET_FEATURE:
        case IOCTL_HID_GET_INPUT_REPORT:
            Status = XboxHid_XferRequest(Ext, Irp, Code);
            break;

        case IOCTL_HID_GET_STRING:
        case IOCTL_HID_GET_INDEXED_STRING:
            Status = XboxHid_GetString(Ext, Irp);
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            Irp->IoStatus.Information = 0;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
XboxHid_SyncCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
XboxHid_ForwardSync(
    _In_ PDEVICE_OBJECT NextDeviceObject,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, XboxHid_SyncCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
NTAPI
XboxHid_Pnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHID_DEVICE_EXTENSION HidExt = DeviceObject->DeviceExtension;
    PXBOXHID_EXTENSION Ext = HidExt->MiniDeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PWCHAR OldId, NewId;
    NTSTATUS Status;
    SIZE_T Length;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = XboxHid_ForwardSync(HidExt->NextDeviceObject, Irp);
            if (NT_SUCCESS(Status))
                Status = XboxHid_Start(DeviceObject);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
            XboxHid_Stop(Ext, TRUE);
            Status = XboxHid_ForwardSync(HidExt->NextDeviceObject, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
            InterlockedExchange(&Ext->Removed, 1);
            XboxHid_Stop(Ext, FALSE);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            XboxHid_Stop(Ext, TRUE);
            InterlockedExchange(&Ext->Removed, 1);
            break;

        case IRP_MN_QUERY_ID:
            if (Stack->Parameters.QueryId.IdType != BusQueryDeviceID ||
                !(DeviceObject->Flags & DO_BUS_ENUMERATED_DEVICE))
            {
                break;
            }

            Status = XboxHid_ForwardSync(HidExt->NextDeviceObject, Irp);
            OldId = (PWCHAR)Irp->IoStatus.Information;
            if (NT_SUCCESS(Status) && OldId && !wcsstr(OldId, L"&IG_"))
            {
                Length = wcslen(OldId);
                NewId = ExAllocatePoolWithTag(PagedPool, (Length + 7) * sizeof(WCHAR), XBOXHID_TAG);
                if (NewId)
                {
                    RtlCopyMemory(NewId, OldId, Length * sizeof(WCHAR));
                    RtlCopyMemory(NewId + Length, L"&IG_00", 7 * sizeof(WCHAR));
                    ExFreePool(OldId);
                    Irp->IoStatus.Information = (ULONG_PTR)NewId;
                }
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(HidExt->NextDeviceObject, Irp);
}

static
NTSTATUS
NTAPI
XboxHid_Power(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHID_DEVICE_EXTENSION HidExt = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(HidExt->NextDeviceObject, Irp);
}

static
NTSTATUS
NTAPI
XboxHid_SystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHID_DEVICE_EXTENSION HidExt = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(HidExt->NextDeviceObject, Irp);
}

static
NTSTATUS
NTAPI
XboxHid_CreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
XboxHid_AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PXBOXHID_EXTENSION Ext = XboxHid_GetExtension(DeviceObject);

    UNREFERENCED_PARAMETER(DriverObject);

    RtlZeroMemory(Ext, sizeof(XBOXHID_EXTENSION));
    KeInitializeSpinLock(&Ext->Lock);
    KeInitializeSpinLock(&Ext->GipLock);
    GipInitialize(&Ext->Gip, XboxHid_GipSend, XboxHid_GipMessage, Ext);
    InitializeListHead(&Ext->PendingReads);
    KeInitializeEvent(&Ext->ReadIdle, NotificationEvent, TRUE);
    KeInitializeEvent(&Ext->WriteSubmitIdle, NotificationEvent, TRUE);
    KeInitializeEvent(&Ext->WriteIdle, NotificationEvent, TRUE);
    Ext->StateSequence = 1;
    Ext->RumbleCutoff = 1000;
    Ext->BuzzCutoff = 1000;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
XboxHid_Unload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    HID_MINIDRIVER_REGISTRATION Registration;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = XboxHid_CreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = XboxHid_CreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = XboxHid_InternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_POWER] = XboxHid_Power;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = XboxHid_SystemControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = XboxHid_Pnp;
    DriverObject->DriverExtension->AddDevice = XboxHid_AddDevice;
    DriverObject->DriverUnload = XboxHid_Unload;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Revision = HID_REVISION;
    Registration.DriverObject = DriverObject;
    Registration.RegistryPath = RegistryPath;
    Registration.DeviceExtensionSize = sizeof(XBOXHID_EXTENSION);
    Registration.DevicesArePolled = FALSE;

    return HidRegisterMinidriver(&Registration);
}
