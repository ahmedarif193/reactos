/*
 * COPYRIGHT:  See COPYING in the top level directory
 * PROJECT:    ReactOS kernel
 * FILE:       drivers/filesystems/msfs/rw.c
 * PURPOSE:    Mailslot filesystem
 * PROGRAMMER: Eric Kohl
 *             Nikita Pechenkin (n.pechenkin@mail.ru)
 */

/* INCLUDES ******************************************************************/

#include "msfs.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

static
NTSTATUS
MsfsCompleteRead(PMSFS_FCB Fcb,
                 PIRP Irp,
                 PVOID Data,
                 ULONG Size)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PVOID Buffer;

    UNREFERENCED_PARAMETER(Fcb);

    if (IoStack->Parameters.Read.Length < Size)
        return STATUS_BUFFER_TOO_SMALL;

    if (Size != 0)
    {
        Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
        if (Buffer == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(Buffer, Data, Size);
    }

    Irp->IoStatus.Information = Size;
    return STATUS_SUCCESS;
}

static
NTSTATUS
MsfsReadMessage(PMSFS_FCB Fcb,
                PIRP Irp)
{
    PMSFS_MESSAGE Message;
    KIRQL oldIrql;
    NTSTATUS Status;

    KeAcquireSpinLock(&Fcb->MessageListLock, &oldIrql);
    if (Fcb->MessageCount == 0)
    {
        KeReleaseSpinLock(&Fcb->MessageListLock, oldIrql);
        return STATUS_PENDING;
    }

    Message = CONTAINING_RECORD(Fcb->MessageListHead.Flink, MSFS_MESSAGE, MessageListEntry);
    Status = MsfsCompleteRead(Fcb, Irp, Message->Buffer, Message->Size);
    if (NT_SUCCESS(Status))
    {
        RemoveEntryList(&Message->MessageListEntry);
        Fcb->MessageCount--;
    }
    else
    {
        Message = NULL;
    }
    KeReleaseSpinLock(&Fcb->MessageListLock, oldIrql);

    if (Message != NULL)
        ExFreePoolWithTag(Message, 'rFsM');

    return Status;
}

NTSTATUS DEFAULTAPI
MsfsRead(PDEVICE_OBJECT DeviceObject,
         PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PFILE_OBJECT FileObject;
    PMSFS_FCB Fcb;
    PMSFS_CCB Ccb;
    PMSFS_DPC_CTX Context;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    DPRINT("MsfsRead(DeviceObject %p Irp %p)\n", DeviceObject, Irp);

    IoStack = IoGetCurrentIrpStackLocation (Irp);
    FileObject = IoStack->FileObject;
    Fcb = (PMSFS_FCB)FileObject->FsContext;
    Ccb = (PMSFS_CCB)FileObject->FsContext2;

    if (!Fcb)
    {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    DPRINT("MailslotName: %wZ\n", &Fcb->Name);

    /* reading is not permitted on client side */
    if (Fcb->ServerCcb != Ccb)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return STATUS_INVALID_PARAMETER;
    }

    Irp->IoStatus.Information = 0;

    ExAcquireFastMutex(&Fcb->IoLock);
    Timeout = Fcb->TimeOut;
    Status = MsfsReadMessage(Fcb, Irp);
    if (Status == STATUS_PENDING)
    {
        if (Timeout.QuadPart == 0)
        {
            Status = STATUS_IO_TIMEOUT;
        }
        else if ((Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(MSFS_DPC_CTX), 'NFsM')) == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            Context->Csq = &Fcb->CancelSafeQueue;
            Context->Fcb = Fcb;
            Context->ReferenceCount = 1;
            Context->Timeout = Timeout;
            Context->UseTimer = (Timeout.QuadPart != -1);
            if (Context->UseTimer)
            {
                KeInitializeTimer(&Context->Timer);
                KeInitializeDpc(&Context->Dpc, MsfsTimeout, Context);
            }
            InterlockedIncrement(&Fcb->MemoryReferences);
            Irp->Tail.Overlay.DriverContext[0] = Context;

            IoCsqInsertIrpEx(&Fcb->CancelSafeQueue, Irp, &Context->CsqContext, Context);
            ExReleaseFastMutex(&Fcb->IoLock);
            return STATUS_PENDING;
        }
    }
    ExReleaseFastMutex(&Fcb->IoLock);

    Irp->IoStatus.Status = Status;
    if (!NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}


NTSTATUS DEFAULTAPI
MsfsWrite(PDEVICE_OBJECT DeviceObject,
          PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PFILE_OBJECT FileObject;
    PMSFS_FCB Fcb;
    PMSFS_CCB Ccb;
    PMSFS_MESSAGE Message;
    KIRQL oldIrql;
    ULONG Length;
    PVOID Buffer = NULL;
    PIRP ReadIrp;
    NTSTATUS Status;

    DPRINT("MsfsWrite(DeviceObject %p Irp %p)\n", DeviceObject, Irp);

    IoStack = IoGetCurrentIrpStackLocation (Irp);
    FileObject = IoStack->FileObject;
    Fcb = (PMSFS_FCB)FileObject->FsContext;
    Ccb = (PMSFS_CCB)FileObject->FsContext2;

    if (!Fcb)
    {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    DPRINT("MailslotName: %wZ\n", &Fcb->Name);

    /* writing is not permitted on server side */
    if (Fcb->ServerCcb == Ccb)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return STATUS_INVALID_PARAMETER;
    }

    Length = IoStack->Parameters.Write.Length;
    if (Length != 0)
    {
        Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
        if (Buffer == NULL)
        {
            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            Irp->IoStatus.Information = 0;

            IoCompleteRequest(Irp, IO_NO_INCREMENT);

            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    ExAcquireFastMutex(&Fcb->IoLock);
    while ((ReadIrp = IoCsqRemoveNextIrp(&Fcb->CancelSafeQueue, NULL)) != NULL)
    {
        MsfsReleaseIrpContext(ReadIrp);
        ReadIrp->IoStatus.Information = 0;
        Status = MsfsCompleteRead(Fcb, ReadIrp, Buffer, Length);
        ReadIrp->IoStatus.Status = Status;
        IoCompleteRequest(ReadIrp, IO_NO_INCREMENT);
        if (NT_SUCCESS(Status))
        {
            ExReleaseFastMutex(&Fcb->IoLock);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = Length;

            IoCompleteRequest(Irp, IO_NO_INCREMENT);

            return STATUS_SUCCESS;
        }
    }

    /* Allocate new message */
    Message = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(MSFS_MESSAGE) + Length,
                                    'rFsM');
    if (Message == NULL)
    {
        ExReleaseFastMutex(&Fcb->IoLock);

        Irp->IoStatus.Status = STATUS_NO_MEMORY;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return STATUS_NO_MEMORY;
    }

    Message->Size = Length;
    if (Length != 0)
        memcpy(&Message->Buffer, Buffer, Length);

    KeAcquireSpinLock(&Fcb->MessageListLock, &oldIrql);
    InsertTailList(&Fcb->MessageListHead, &Message->MessageListEntry);
    Fcb->MessageCount++;
    KeReleaseSpinLock(&Fcb->MessageListLock, oldIrql);
    ExReleaseFastMutex(&Fcb->IoLock);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = Length;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

/* EOF */
