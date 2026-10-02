/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Work items, cancel-safe queues and cancellation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

#define FLTP_CBDQ_DISABLED 0x00000001
#define FLTP_IRP_TO_IRP_CTRL(Irp) ((PFLTP_IRP_CTRL)(Irp)->Tail.Overlay.DriverContext[2])

typedef struct _FLTP_WORK_ITEM
{
    WORK_QUEUE_ITEM Item;
    PVOID Routine;
    PVOID Object;
    PVOID Context;
    BOOLEAN Deferred;
} FLTP_WORK_ITEM, *PFLTP_WORK_ITEM;

static
VOID
NTAPI
FltpWorkItemRoutine(
    _In_ PVOID Parameter)
{
    PFLTP_WORK_ITEM WorkItem = Parameter;
    PFLT_INSTANCE Instance;
    PVOID Object = WorkItem->Object;

    if (WorkItem->Deferred)
    {
        Instance = ((PFLT_CALLBACK_DATA)Object)->Iopb->TargetInstance;
        ((PFLT_DEFERRED_IO_WORKITEM_ROUTINE)WorkItem->Routine)((PFLT_DEFERRED_IO_WORKITEM)WorkItem,
                                                                         Object,
                                                                         WorkItem->Context);
        FltObjectDereference(Instance);
    }
    else
    {
        ((PFLT_GENERIC_WORKITEM_ROUTINE)WorkItem->Routine)((PFLT_GENERIC_WORKITEM)WorkItem,
                                                                     Object,
                                                                     WorkItem->Context);
        FltObjectDereference(Object);
    }
}

static
PFLTP_WORK_ITEM
FltpAllocateWorkItem(VOID)
{
    PFLTP_WORK_ITEM WorkItem;

    WorkItem = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*WorkItem), FLT_TAG_WORK);
    if (WorkItem != NULL)
    {
        RtlZeroMemory(WorkItem, sizeof(*WorkItem));
    }
    return WorkItem;
}

PFLT_GENERIC_WORKITEM
FLTAPI
FltAllocateGenericWorkItem(VOID)
{
    return (PFLT_GENERIC_WORKITEM)FltpAllocateWorkItem();
}

VOID
FLTAPI
FltFreeGenericWorkItem(
    _In_ PFLT_GENERIC_WORKITEM FltWorkItem)
{
    ExFreePoolWithTag(FltWorkItem, FLT_TAG_WORK);
}

NTSTATUS
FLTAPI
FltQueueGenericWorkItem(
    _In_ PFLT_GENERIC_WORKITEM FltWorkItem,
    _In_ PVOID FltObject,
    _In_ PFLT_GENERIC_WORKITEM_ROUTINE WorkerRoutine,
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_opt_ PVOID Context)
{
    PFLTP_WORK_ITEM WorkItem = (PFLTP_WORK_ITEM)FltWorkItem;
    NTSTATUS Status;

    Status = FltObjectReference(FltObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    WorkItem->Routine = WorkerRoutine;
    WorkItem->Object = FltObject;
    WorkItem->Context = Context;
    WorkItem->Deferred = FALSE;
    ExInitializeWorkItem(&WorkItem->Item, FltpWorkItemRoutine, WorkItem);
    ExQueueWorkItem(&WorkItem->Item, QueueType);
    return STATUS_SUCCESS;
}

PFLT_DEFERRED_IO_WORKITEM
FLTAPI
FltAllocateDeferredIoWorkItem(VOID)
{
    return (PFLT_DEFERRED_IO_WORKITEM)FltpAllocateWorkItem();
}

VOID
FLTAPI
FltFreeDeferredIoWorkItem(
    _In_ PFLT_DEFERRED_IO_WORKITEM FltWorkItem)
{
    ExFreePoolWithTag(FltWorkItem, FLT_TAG_WORK);
}

NTSTATUS
FLTAPI
FltQueueDeferredIoWorkItem(
    _In_ PFLT_DEFERRED_IO_WORKITEM FltWorkItem,
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ PFLT_DEFERRED_IO_WORKITEM_ROUTINE WorkerRoutine,
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_ PVOID Context)
{
    PFLTP_WORK_ITEM WorkItem = (PFLTP_WORK_ITEM)FltWorkItem;
    NTSTATUS Status;

    if (!FLT_IS_IRP_OPERATION(Data) ||
        (Data->Iopb->IrpFlags & IRP_PAGING_IO) ||
        IoGetTopLevelIrp() != NULL)
    {
        return STATUS_FLT_NOT_SAFE_TO_POST_OPERATION;
    }

    Status = FltObjectReference(Data->Iopb->TargetInstance);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    WorkItem->Routine = WorkerRoutine;
    WorkItem->Object = Data;
    WorkItem->Context = Context;
    WorkItem->Deferred = TRUE;
    ExInitializeWorkItem(&WorkItem->Item, FltpWorkItemRoutine, WorkItem);
    ExQueueWorkItem(&WorkItem->Item, QueueType);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
FltpCbdqInsertIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp,
    _In_ PVOID InsertContext)
{
    PFLT_CALLBACK_DATA_QUEUE Cbdq = CONTAINING_RECORD(Csq, FLT_CALLBACK_DATA_QUEUE, Csq);

    if (Cbdq->Flags & FLTP_CBDQ_DISABLED)
    {
        return STATUS_FLT_CBDQ_DISABLED;
    }

    return Cbdq->InsertIo(Cbdq, &FLTP_IRP_TO_IRP_CTRL(Irp)->Data, InsertContext);
}

static
VOID
NTAPI
FltpCbdqRemoveIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    PFLT_CALLBACK_DATA_QUEUE Cbdq = CONTAINING_RECORD(Csq, FLT_CALLBACK_DATA_QUEUE, Csq);

    Cbdq->RemoveIo(Cbdq, &FLTP_IRP_TO_IRP_CTRL(Irp)->Data);
}

static
PIRP
NTAPI
FltpCbdqPeekNextIrp(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PFLT_CALLBACK_DATA_QUEUE Cbdq = CONTAINING_RECORD(Csq, FLT_CALLBACK_DATA_QUEUE, Csq);
    PFLT_CALLBACK_DATA Data;

    Data = Cbdq->PeekNextIo(Cbdq, Irp ? &FLTP_IRP_TO_IRP_CTRL(Irp)->Data : NULL, PeekContext);
    return Data ? FLTP_DATA_TO_IRP_CTRL(Data)->Irp : NULL;
}

static
VOID
NTAPI
FltpCbdqAcquire(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    PFLT_CALLBACK_DATA_QUEUE Cbdq = CONTAINING_RECORD(Csq, FLT_CALLBACK_DATA_QUEUE, Csq);

    Cbdq->Acquire(Cbdq, Irql);
}

static
VOID
NTAPI
FltpCbdqRelease(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    PFLT_CALLBACK_DATA_QUEUE Cbdq = CONTAINING_RECORD(Csq, FLT_CALLBACK_DATA_QUEUE, Csq);

    Cbdq->Release(Cbdq, Irql);
}

static
VOID
NTAPI
FltpCbdqCompleteCanceledIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    PFLT_CALLBACK_DATA_QUEUE Cbdq = CONTAINING_RECORD(Csq, FLT_CALLBACK_DATA_QUEUE, Csq);

    Cbdq->CompleteCanceledIo(Cbdq, &FLTP_IRP_TO_IRP_CTRL(Irp)->Data);
}

NTSTATUS
FLTAPI
FltCbdqInitialize(
    _In_ PFLT_INSTANCE Instance,
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _In_ PFLT_CALLBACK_DATA_QUEUE_INSERT_IO CbdqInsertIo,
    _In_ PFLT_CALLBACK_DATA_QUEUE_REMOVE_IO CbdqRemoveIo,
    _In_ PFLT_CALLBACK_DATA_QUEUE_PEEK_NEXT_IO CbdqPeekNextIo,
    _In_ PFLT_CALLBACK_DATA_QUEUE_ACQUIRE CbdqAcquire,
    _In_ PFLT_CALLBACK_DATA_QUEUE_RELEASE CbdqRelease,
    _In_ PFLT_CALLBACK_DATA_QUEUE_COMPLETE_CANCELED_IO CbdqCompleteCanceledIo)
{
    Cbdq->Flags = 0;
    Cbdq->Instance = Instance;
    Cbdq->InsertIo = CbdqInsertIo;
    Cbdq->RemoveIo = CbdqRemoveIo;
    Cbdq->PeekNextIo = CbdqPeekNextIo;
    Cbdq->Acquire = CbdqAcquire;
    Cbdq->Release = CbdqRelease;
    Cbdq->CompleteCanceledIo = CbdqCompleteCanceledIo;

    return IoCsqInitializeEx(&Cbdq->Csq,
                             FltpCbdqInsertIrp,
                             FltpCbdqRemoveIrp,
                             FltpCbdqPeekNextIrp,
                             FltpCbdqAcquire,
                             FltpCbdqRelease,
                             FltpCbdqCompleteCanceledIrp);
}

VOID
FLTAPI
FltCbdqEnable(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq)
{
    KIRQL Irql = PASSIVE_LEVEL;

    Cbdq->Acquire(Cbdq, &Irql);
    Cbdq->Flags &= ~FLTP_CBDQ_DISABLED;
    Cbdq->Release(Cbdq, Irql);
}

VOID
FLTAPI
FltCbdqDisable(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq)
{
    KIRQL Irql = PASSIVE_LEVEL;

    Cbdq->Acquire(Cbdq, &Irql);
    Cbdq->Flags |= FLTP_CBDQ_DISABLED;
    Cbdq->Release(Cbdq, Irql);
}

NTSTATUS
FLTAPI
FltCbdqInsertIo(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _In_ PFLT_CALLBACK_DATA Cbd,
    _In_opt_ PFLT_CALLBACK_DATA_QUEUE_IO_CONTEXT Context,
    _In_opt_ PVOID InsertContext)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(Cbd);

    if (!FLT_IS_IRP_OPERATION(Cbd) || IrpCtrl->Irp == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    IrpCtrl->Irp->Tail.Overlay.DriverContext[2] = IrpCtrl;
    return IoCsqInsertIrpEx(&Cbdq->Csq, IrpCtrl->Irp, Context, InsertContext);
}

PFLT_CALLBACK_DATA
FLTAPI
FltCbdqRemoveIo(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _In_ PFLT_CALLBACK_DATA_QUEUE_IO_CONTEXT Context)
{
    PIRP Irp = IoCsqRemoveIrp(&Cbdq->Csq, Context);

    return Irp ? &FLTP_IRP_TO_IRP_CTRL(Irp)->Data : NULL;
}

PFLT_CALLBACK_DATA
FLTAPI
FltCbdqRemoveNextIo(
    _Inout_ PFLT_CALLBACK_DATA_QUEUE Cbdq,
    _In_opt_ PVOID PeekContext)
{
    PIRP Irp = IoCsqRemoveNextIrp(&Cbdq->Csq, PeekContext);

    return Irp ? &FLTP_IRP_TO_IRP_CTRL(Irp)->Data : NULL;
}

static
VOID
NTAPI
FltpCancelCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_IRP_TO_IRP_CTRL(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    IrpCtrl->CanceledCallback(&IrpCtrl->Data);
}

NTSTATUS
FLTAPI
FltSetCancelCompletion(
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _In_ PFLT_COMPLETE_CANCELED_CALLBACK CanceledCallback)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);
    PIRP Irp = IrpCtrl->Irp;
    KIRQL OldIrql;

    if (!FLT_IS_IRP_OPERATION(CallbackData) || Irp == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    IrpCtrl->CanceledCallback = CanceledCallback;
    Irp->Tail.Overlay.DriverContext[2] = IrpCtrl;

    IoAcquireCancelSpinLock(&OldIrql);
    if (Irp->Cancel)
    {
        IoReleaseCancelSpinLock(OldIrql);
        return STATUS_CANCELLED;
    }
    IoSetCancelRoutine(Irp, FltpCancelCompletion);
    IoReleaseCancelSpinLock(OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltClearCancelCompletion(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);

    if (!FLT_IS_IRP_OPERATION(CallbackData) || IrpCtrl->Irp == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (IoSetCancelRoutine(IrpCtrl->Irp, NULL) == NULL)
    {
        return STATUS_CANCELLED;
    }
    return STATUS_SUCCESS;
}

BOOLEAN
FLTAPI
FltCancelIo(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);
    BOOLEAN Result, FreeIrp;
    KIRQL OldIrql;
    PIRP Irp;

    if (!FLT_IS_IRP_OPERATION(CallbackData))
    {
        return FALSE;
    }

    if (!(IrpCtrl->Flags & FLTP_IRPCTRL_GENERATED))
    {
        return IrpCtrl->Irp ? IoCancelIrp(IrpCtrl->Irp) : FALSE;
    }

    KeAcquireSpinLock(&FltGlobals.GeneratedLock, &OldIrql);
    Irp = IrpCtrl->Irp;
    if (Irp == NULL || !(IrpCtrl->Flags & FLTP_IRPCTRL_BUSY))
    {
        KeReleaseSpinLock(&FltGlobals.GeneratedLock, OldIrql);
        return FALSE;
    }
    IrpCtrl->Flags |= FLTP_IRPCTRL_CANCELLING;
    KeReleaseSpinLock(&FltGlobals.GeneratedLock, OldIrql);

    Result = IoCancelIrp(Irp);

    KeAcquireSpinLock(&FltGlobals.GeneratedLock, &OldIrql);
    IrpCtrl->Flags &= ~FLTP_IRPCTRL_CANCELLING;
    FreeIrp = (IrpCtrl->Flags & FLTP_IRPCTRL_FREE_IRP) != 0;
    if (FreeIrp)
    {
        IrpCtrl->Flags &= ~FLTP_IRPCTRL_FREE_IRP;
        IrpCtrl->Irp = NULL;
    }
    KeReleaseSpinLock(&FltGlobals.GeneratedLock, OldIrql);

    if (FreeIrp)
    {
        IoFreeIrp(Irp);
    }
    return Result;
}
