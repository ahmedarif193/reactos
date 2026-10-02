/*
 * COPYRIGHT:  See COPYING in the top level directory
 * PROJECT:    ReactOS kernel
 * FILE:       drivers/filesystems/msfs/msfssup.c
 * PURPOSE:    Mailslot filesystem
 * PROGRAMMER: Nikita Pechenkin (n.pechenkin@mail.ru)
 */

/* INCLUDES ******************************************************************/
#include "msfs.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

VOID
MsfsDereferenceFcb(PMSFS_FCB Fcb)
{
    if (InterlockedDecrement(&Fcb->MemoryReferences) == 0)
        ExFreePoolWithTag(Fcb, 'fFsM');
}

static
VOID
MsfsDereferenceContext(PMSFS_DPC_CTX Context)
{
    PMSFS_FCB Fcb;

    if (InterlockedDecrement(&Context->ReferenceCount) != 0)
        return;

    Fcb = Context->Fcb;
    ExFreePoolWithTag(Context, 'NFsM');
    MsfsDereferenceFcb(Fcb);
}

VOID
MsfsReleaseIrpContext(PIRP Irp)
{
    PMSFS_DPC_CTX Context = Irp->Tail.Overlay.DriverContext[0];

    if (Context->UseTimer && KeCancelTimer(&Context->Timer))
        MsfsDereferenceContext(Context);

    MsfsDereferenceContext(Context);
}

NTSTATUS NTAPI
MsfsInsertIrpEx(PIO_CSQ Csq, PIRP Irp, PVOID InsertContext)
{
    PMSFS_FCB Fcb;
    PMSFS_DPC_CTX Context = InsertContext;

    Fcb = CONTAINING_RECORD(Csq, MSFS_FCB, CancelSafeQueue);
    InsertTailList(&Fcb->PendingIrpQueue, &Irp->Tail.Overlay.ListEntry);
    if (Context->UseTimer)
    {
        InterlockedIncrement(&Context->ReferenceCount);
        KeSetTimer(&Context->Timer, Context->Timeout, &Context->Dpc);
    }

    return STATUS_SUCCESS;
}

VOID NTAPI
MsfsRemoveIrp(PIO_CSQ Csq, PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);

    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

PIRP NTAPI
MsfsPeekNextIrp(PIO_CSQ Csq, PIRP Irp, PVOID PeekContext)
{
    PMSFS_FCB Fcb;
    PIRP NextIrp = NULL;
    PLIST_ENTRY NextEntry, ListHead;
    PIO_STACK_LOCATION Stack;

    Fcb = CONTAINING_RECORD(Csq, MSFS_FCB, CancelSafeQueue);

    ListHead = &Fcb->PendingIrpQueue;

    if (Irp == NULL)
    {
        NextEntry = ListHead->Flink;
    }
    else
    {
        NextEntry = Irp->Tail.Overlay.ListEntry.Flink;
    }

    for (; NextEntry != ListHead; NextEntry = NextEntry->Flink)
    {
        NextIrp = CONTAINING_RECORD(NextEntry, IRP, Tail.Overlay.ListEntry);

        Stack = IoGetCurrentIrpStackLocation(NextIrp);

        if (PeekContext)
        {
            if (Stack->FileObject == (PFILE_OBJECT)PeekContext)
            {
                break;
            }
        }
        else
        {
            break;
        }

        NextIrp = NULL;
    }

    return NextIrp;
}

VOID NTAPI
MsfsAcquireLock(PIO_CSQ Csq, PKIRQL Irql)
{
    PMSFS_FCB Fcb;

    Fcb = CONTAINING_RECORD(Csq, MSFS_FCB, CancelSafeQueue);
    KeAcquireSpinLock(&Fcb->MessageListLock, Irql);
}


VOID NTAPI
MsfsReleaseLock(PIO_CSQ Csq, KIRQL Irql)
{
    PMSFS_FCB Fcb;

    Fcb = CONTAINING_RECORD(Csq, MSFS_FCB, CancelSafeQueue);
    KeReleaseSpinLock(&Fcb->MessageListLock, Irql);
}

VOID NTAPI
MsfsCompleteCanceledIrp(PIO_CSQ Csq, PIRP Irp)
{

    UNREFERENCED_PARAMETER(Csq);

    MsfsReleaseIrpContext(Irp);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

VOID NTAPI
MsfsTimeout(PKDPC Dpc,
            PVOID DeferredContext,
            PVOID SystemArgument1,
            PVOID SystemArgument2)
{
    PMSFS_DPC_CTX Context;
    PIRP Irp;

    Context = (PMSFS_DPC_CTX)DeferredContext;

    /* Try to get the IRP */
    Irp = IoCsqRemoveIrp(Context->Csq, &Context->CsqContext);
    if (Irp != NULL)
    {
        Irp->IoStatus.Status = STATUS_IO_TIMEOUT;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        MsfsDereferenceContext(Context);
    }

    MsfsDereferenceContext(Context);
}

/* EOF */
