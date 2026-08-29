/*
 * PROJECT:     ReactOS Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Simple-peripheral-bus KMDF class extension
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "classlibrary.h"
#include <spbcx.h>

typedef struct _SPBCX_TRANSFER
{
    SPB_TRANSFER_DIRECTION Direction;
    ULONG DelayInUs;
    size_t Length;
    PMDL Mdl;
    BOOLEAN OwnsMdl;
    BOOLEAN Locked;
} SPBCX_TRANSFER, *PSPBCX_TRANSFER;

typedef struct _SPBCX_REQUEST_CONTEXT
{
    SPB_REQUEST_TYPE Type;
    SPB_REQUEST_SEQUENCE_POSITION Position;
    SPB_TRANSFER_DIRECTION PreviousTransferDirection;
    size_t Length;
    ULONG TransferCount;
    PSPBCX_TRANSFER Transfers;
    SPBCX_TRANSFER Inline;
    BOOLEAN Captured;
    BOOLEAN Prepared;
} SPBCX_REQUEST_CONTEXT, *PSPBCX_REQUEST_CONTEXT;

typedef struct _SPBCX_TARGET_CONTEXT
{
    LARGE_INTEGER ConnectionId;
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Properties;
    BOOLEAN Connected;
    BOOLEAN HoldsLock;
    BOOLEAN InSequence;
    SPB_TRANSFER_DIRECTION LastDirection;
} SPBCX_TARGET_CONTEXT, *PSPBCX_TARGET_CONTEXT;

typedef struct _SPBCX_DEVICE_CONTEXT
{
    PWDF_DRIVER_GLOBALS ClientGlobals;
    SPB_CONTROLLER_CONFIG Config;
    PFN_SPB_CONTROLLER_OTHER EvtIoOther;
    PFN_WDF_IO_IN_CALLER_CONTEXT EvtIoOtherInCallerContext;
    WDF_OBJECT_ATTRIBUTES RequestAttributes;
    WDF_OBJECT_ATTRIBUTES TargetAttributes;
    BOOLEAN HasRequestAttributes;
    BOOLEAN HasTargetAttributes;
    BOOLEAN Initialized;
    WDFQUEUE Queue;
    WDFQUEUE WaitQueue;
    KSPIN_LOCK Lock;
    WDFFILEOBJECT LockOwner;
} SPBCX_DEVICE_CONTEXT, *PSPBCX_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SPBCX_DEVICE_CONTEXT, SpbCxGetDeviceContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SPBCX_TARGET_CONTEXT, SpbCxGetTargetContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SPBCX_REQUEST_CONTEXT, SpbCxGetRequestContext)

static
VOID
SpbCxRequestComplete(
    _In_ PSPBCX_DEVICE_CONTEXT Device,
    _In_ WDFREQUEST Request,
    _In_ NTSTATUS Status);

static
VOID
SpbCxReleaseTransfers(
    _Inout_ PSPBCX_REQUEST_CONTEXT Context)
{
    ULONG Index;

    if (Context->Transfers == NULL)
        return;

    for (Index = 0; Index < Context->TransferCount; Index++)
    {
        PSPBCX_TRANSFER Transfer = &Context->Transfers[Index];
        PMDL Mdl = Transfer->Mdl;

        if (!Transfer->OwnsMdl)
            continue;

        while (Mdl != NULL)
        {
            PMDL Next = Mdl->Next;

            if (Transfer->Locked)
                MmUnlockPages(Mdl);
            IoFreeMdl(Mdl);
            Mdl = Next;
        }

        Transfer->Mdl = NULL;
    }

    if (Context->Transfers != &Context->Inline)
        ExFreePoolWithTag(Context->Transfers, WDFCX_TAG);

    Context->Transfers = NULL;
    Context->TransferCount = 0;
    Context->Captured = FALSE;
}

static
VOID
NTAPI
SpbCxEvtRequestContextCleanup(
    _In_ WDFOBJECT Object)
{
    SpbCxReleaseTransfers(SpbCxGetRequestContext(Object));
}

static
NTSTATUS
SpbCxPrepareRequest(
    _In_ PSPBCX_DEVICE_CONTEXT Device,
    _In_ WDFREQUEST Request,
    _Outptr_ PSPBCX_REQUEST_CONTEXT *Context)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    PSPBCX_REQUEST_CONTEXT RequestContext;
    NTSTATUS Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SPBCX_REQUEST_CONTEXT);
    Attributes.EvtCleanupCallback = SpbCxEvtRequestContextCleanup;

    Status = WdfObjectAllocateContext(Request, &Attributes, (PVOID *)&RequestContext);
    if (Status == STATUS_OBJECT_NAME_EXISTS)
    {
        RequestContext = SpbCxGetRequestContext(Request);
        Status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
        return Status;

    if (!RequestContext->Prepared)
    {
        RequestContext->Prepared = TRUE;
        RequestContext->Type = SpbRequestTypeUndefined;
        RequestContext->Position = SpbRequestSequencePositionSingle;
        RequestContext->PreviousTransferDirection = SpbTransferDirectionNone;

        if (Device->HasRequestAttributes)
        {
            Attributes = Device->RequestAttributes;
            Attributes.ParentObject = NULL;
            Status = WdfObjectAllocateContext(Request, &Attributes, NULL);
            if (Status == STATUS_OBJECT_NAME_EXISTS)
                Status = STATUS_SUCCESS;
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }

    *Context = RequestContext;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SpbCxLockBuffer(
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ SPB_TRANSFER_DIRECTION Direction,
    _In_ BOOLEAN NonPaged,
    _Outptr_ PMDL *Mdl,
    _Out_ PBOOLEAN Locked)
{
    PMDL NewMdl;

    *Mdl = NULL;
    *Locked = FALSE;

    NewMdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, NULL);
    if (NewMdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (NonPaged)
    {
        MmBuildMdlForNonPagedPool(NewMdl);
    }
    else
    {
        _SEH2_TRY
        {
            MmProbeAndLockPages(NewMdl,
                                RequestorMode,
                                Direction == SpbTransferDirectionFromDevice ? IoWriteAccess : IoReadAccess);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            IoFreeMdl(NewMdl);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;

        *Locked = TRUE;
    }

    *Mdl = NewMdl;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SpbCxCaptureBufferList(
    _In_ PSPB_TRANSFER_BUFFER_LIST_ENTRY UserList,
    _In_ ULONG ListCount,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Inout_ PSPBCX_TRANSFER Transfer)
{
    PSPB_TRANSFER_BUFFER_LIST_ENTRY List;
    PMDL *Tail;
    SIZE_T ListBytes;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    if (ListCount == 0 || ListCount > MAXULONG / sizeof(SPB_TRANSFER_BUFFER_LIST_ENTRY))
        return STATUS_INVALID_PARAMETER;

    ListBytes = ListCount * sizeof(SPB_TRANSFER_BUFFER_LIST_ENTRY);
    List = ExAllocatePoolWithTag(NonPagedPool, ListBytes, WDFCX_TAG);
    if (List == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        if (RequestorMode != KernelMode)
            ProbeForRead(UserList, ListBytes, sizeof(PVOID));
        RtlCopyMemory(List, UserList, ListBytes);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(List, WDFCX_TAG);
        return Status;
    }

    Transfer->OwnsMdl = TRUE;
    Tail = &Transfer->Mdl;
    for (Index = 0; Index < ListCount; Index++)
    {
        PMDL Mdl;
        BOOLEAN Locked;

        if (List[Index].BufferCb == 0)
            continue;

        Status = SpbCxLockBuffer(List[Index].Buffer,
                                 List[Index].BufferCb,
                                 RequestorMode,
                                 Transfer->Direction,
                                 FALSE,
                                 &Mdl,
                                 &Locked);
        if (!NT_SUCCESS(Status))
            break;

        Transfer->Locked = TRUE;
        Transfer->Length += List[Index].BufferCb;
        *Tail = Mdl;
        Tail = &Mdl->Next;
    }

    ExFreePoolWithTag(List, WDFCX_TAG);
    return Status;
}

static
NTSTATUS
SpbCxCaptureTransferList(
    _In_ WDFREQUEST Request,
    _Inout_ PSPBCX_REQUEST_CONTEXT Context)
{
    PSPB_TRANSFER_LIST List;
    size_t ListLength;
    KPROCESSOR_MODE RequestorMode;
    ULONG Index;
    NTSTATUS Status;

    if (Context->Captured)
        return STATUS_SUCCESS;

    Status = WdfRequestRetrieveInputBuffer(Request,
                                           sizeof(SPB_TRANSFER_LIST),
                                           (PVOID *)&List,
                                           &ListLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (List->Size != sizeof(SPB_TRANSFER_LIST) ||
        List->TransferCount == 0 ||
        List->TransferCount > (ListLength - FIELD_OFFSET(SPB_TRANSFER_LIST, Transfers)) / sizeof(SPB_TRANSFER_LIST_ENTRY))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (List->TransferCount == 1)
    {
        Context->Transfers = &Context->Inline;
    }
    else
    {
        Context->Transfers = ExAllocatePoolWithTag(NonPagedPool,
                                                   List->TransferCount * sizeof(SPBCX_TRANSFER),
                                                   WDFCX_TAG);
        if (Context->Transfers == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context->Transfers, List->TransferCount * sizeof(SPBCX_TRANSFER));
    Context->TransferCount = List->TransferCount;
    Context->Length = 0;
    RequestorMode = WdfRequestGetRequestorMode(Request);

    for (Index = 0; Index < List->TransferCount; Index++)
    {
        PSPB_TRANSFER_LIST_ENTRY Entry = &List->Transfers[Index];
        PSPBCX_TRANSFER Transfer = &Context->Transfers[Index];

        if (Entry->Direction != SpbTransferDirectionFromDevice &&
            Entry->Direction != SpbTransferDirectionToDevice)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Transfer->Direction = Entry->Direction;
        Transfer->DelayInUs = Entry->DelayInUs;

        switch (Entry->Buffer.Format)
        {
            case SpbTransferBufferFormatSimple:
            case SpbTransferBufferFormatSimpleNonPaged:
                Transfer->Length = Entry->Buffer.Simple.BufferCb;
                if (Transfer->Length == 0)
                    break;
                Transfer->OwnsMdl = TRUE;
                Status = SpbCxLockBuffer(Entry->Buffer.Simple.Buffer,
                                         Entry->Buffer.Simple.BufferCb,
                                         RequestorMode,
                                         Entry->Direction,
                                         Entry->Buffer.Format == SpbTransferBufferFormatSimpleNonPaged,
                                         &Transfer->Mdl,
                                         &Transfer->Locked);
                break;

            case SpbTransferBufferFormatList:
                Status = SpbCxCaptureBufferList(Entry->Buffer.BufferList.List,
                                                Entry->Buffer.BufferList.ListCe,
                                                RequestorMode,
                                                Transfer);
                break;

            case SpbTransferBufferFormatMdl:
                if (RequestorMode != KernelMode)
                {
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
                Transfer->Mdl = Entry->Buffer.Mdl;
                for (PMDL Mdl = Transfer->Mdl; Mdl != NULL; Mdl = Mdl->Next)
                    Transfer->Length += MmGetMdlByteCount(Mdl);
                break;

            default:
                Status = STATUS_INVALID_PARAMETER;
                break;
        }

        if (!NT_SUCCESS(Status))
            break;

        Context->Length += Transfer->Length;
    }

    if (!NT_SUCCESS(Status))
    {
        SpbCxReleaseTransfers(Context);
        return Status;
    }

    Context->Captured = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SpbCxCaptureReadWrite(
    _In_ WDFREQUEST Request,
    _Inout_ PSPBCX_REQUEST_CONTEXT Context,
    _In_ SPB_TRANSFER_DIRECTION Direction,
    _In_ size_t Length)
{
    PMDL Mdl = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Length != 0)
    {
        if (Direction == SpbTransferDirectionFromDevice)
            Status = WdfRequestRetrieveOutputWdmMdl(Request, &Mdl);
        else
            Status = WdfRequestRetrieveInputWdmMdl(Request, &Mdl);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    RtlZeroMemory(&Context->Inline, sizeof(Context->Inline));
    Context->Inline.Direction = Direction;
    Context->Inline.Length = Length;
    Context->Inline.Mdl = Mdl;
    Context->Transfers = &Context->Inline;
    Context->TransferCount = 1;
    Context->Length = Length;
    Context->Captured = TRUE;
    return STATUS_SUCCESS;
}

static
BOOLEAN
SpbCxIsUnlockRequest(
    _In_ ULONG IoControlCode)
{
    return IoControlCode == IOCTL_SPB_UNLOCK_CONTROLLER ||
           IoControlCode == IOCTL_SPB_UNLOCK_CONNECTION;
}

static
BOOLEAN
SpbCxNextRequestIsUnlock(
    _In_ PSPBCX_DEVICE_CONTEXT Device,
    _In_ WDFFILEOBJECT FileObject)
{
    WDF_REQUEST_PARAMETERS Parameters;
    WDFREQUEST Found;
    BOOLEAN Result = FALSE;
    NTSTATUS Status;

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    Status = WdfIoQueueFindRequest(Device->Queue, NULL, FileObject, &Parameters, &Found);
    if (!NT_SUCCESS(Status))
        return FALSE;

    if (Parameters.Type == WdfRequestTypeDeviceControl &&
        SpbCxIsUnlockRequest(Parameters.Parameters.DeviceIoControl.IoControlCode))
    {
        Result = TRUE;
    }

    WdfObjectDereference(Found);
    return Result;
}

static
VOID
SpbCxDrainWaiters(
    _In_ PSPBCX_DEVICE_CONTEXT Device)
{
    WDFREQUEST Request;
    NTSTATUS Status;

    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Device->WaitQueue, &Request)))
    {
        Status = WdfRequestForwardToIoQueue(Request, Device->Queue);
        if (!NT_SUCCESS(Status))
            WdfRequestComplete(Request, Status);
    }
}

static
VOID
SpbCxDispatch(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ SPB_REQUEST_TYPE Type,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE DeviceHandle = WdfIoQueueGetDevice(Queue);
    PSPBCX_DEVICE_CONTEXT Device = SpbCxGetDeviceContext(DeviceHandle);
    PSPBCX_REQUEST_CONTEXT Context;
    PSPBCX_TARGET_CONTEXT Target;
    WDFFILEOBJECT FileObject;
    KIRQL OldIrql;
    BOOLEAN Wait = FALSE;
    BOOLEAN Locked;
    NTSTATUS Status;

    FileObject = WdfRequestGetFileObject(Request);
    if (FileObject == NULL)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    Target = SpbCxGetTargetContext(FileObject);
    if (!Target->Connected)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
        return;
    }

    Status = SpbCxPrepareRequest(Device, Request, &Context);
    if (!NT_SUCCESS(Status))
    {
        WdfRequestComplete(Request, Status);
        return;
    }

    Context->Type = Type;
    Context->Position = SpbRequestSequencePositionSingle;
    Context->PreviousTransferDirection = SpbTransferDirectionNone;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Device->LockOwner != NULL && Device->LockOwner != FileObject)
    {
        Wait = TRUE;
    }
    else if (Type == SpbRequestTypeLockController || Type == SpbRequestTypeLockConnection)
    {
        if (Device->LockOwner == FileObject)
            Status = STATUS_INVALID_DEVICE_STATE;
        else
            Device->LockOwner = FileObject;
    }
    else if (Type == SpbRequestTypeUnlockController || Type == SpbRequestTypeUnlockConnection)
    {
        if (Device->LockOwner != FileObject)
            Status = STATUS_INVALID_DEVICE_STATE;
    }
    Locked = Device->LockOwner == FileObject && Target->HoldsLock;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    if (Wait)
    {
        Status = WdfRequestForwardToIoQueue(Request, Device->WaitQueue);
        if (!NT_SUCCESS(Status))
            WdfRequestComplete(Request, Status);
        return;
    }

    if (!NT_SUCCESS(Status))
    {
        WdfRequestComplete(Request, Status);
        return;
    }

    switch (Type)
    {
        case SpbRequestTypeRead:
        case SpbRequestTypeWrite:
        case SpbRequestTypeSequence:
        case SpbRequestTypeOther:
            if (Locked)
            {
                BOOLEAN Last = SpbCxNextRequestIsUnlock(Device, FileObject);

                if (!Target->InSequence)
                    Context->Position = Last ? SpbRequestSequencePositionSingle : SpbRequestSequencePositionFirst;
                else
                    Context->Position = Last ? SpbRequestSequencePositionLast : SpbRequestSequencePositionContinue;
                if (Target->InSequence)
                    Context->PreviousTransferDirection = Target->LastDirection;
            }
            break;

        default:
            break;
    }

    switch (Type)
    {
        case SpbRequestTypeRead:
            Status = SpbCxCaptureReadWrite(Request, Context, SpbTransferDirectionFromDevice, OutputBufferLength);
            if (!NT_SUCCESS(Status))
                break;
            if (Device->Config.EvtSpbIoRead == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }
            Device->Config.EvtSpbIoRead(DeviceHandle, (SPBTARGET)FileObject, (SPBREQUEST)Request, OutputBufferLength);
            return;

        case SpbRequestTypeWrite:
            Status = SpbCxCaptureReadWrite(Request, Context, SpbTransferDirectionToDevice, InputBufferLength);
            if (!NT_SUCCESS(Status))
                break;
            if (Device->Config.EvtSpbIoWrite == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }
            Device->Config.EvtSpbIoWrite(DeviceHandle, (SPBTARGET)FileObject, (SPBREQUEST)Request, InputBufferLength);
            return;

        case SpbRequestTypeSequence:
            Status = SpbCxCaptureTransferList(Request, Context);
            if (!NT_SUCCESS(Status))
                break;
            if (Device->Config.EvtSpbIoSequence == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }
            Device->Config.EvtSpbIoSequence(DeviceHandle, (SPBTARGET)FileObject, (SPBREQUEST)Request, Context->TransferCount);
            return;

        case SpbRequestTypeLockController:
        case SpbRequestTypeLockConnection:
            if (Device->Config.EvtSpbControllerLock != NULL)
            {
                Device->Config.EvtSpbControllerLock(DeviceHandle, (SPBTARGET)FileObject, (SPBREQUEST)Request);
                return;
            }
            Status = STATUS_SUCCESS;
            break;

        case SpbRequestTypeUnlockController:
        case SpbRequestTypeUnlockConnection:
            if (Device->Config.EvtSpbControllerUnlock != NULL)
            {
                Device->Config.EvtSpbControllerUnlock(DeviceHandle, (SPBTARGET)FileObject, (SPBREQUEST)Request);
                return;
            }
            Status = STATUS_SUCCESS;
            break;

        case SpbRequestTypeOther:
            if (Device->EvtIoOther == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }
            Device->EvtIoOther(DeviceHandle, (SPBTARGET)FileObject, (SPBREQUEST)Request, OutputBufferLength, InputBufferLength, IoControlCode);
            return;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    SpbCxRequestComplete(Device, Request, Status);
}

static
VOID
SpbCxRequestComplete(
    _In_ PSPBCX_DEVICE_CONTEXT Device,
    _In_ WDFREQUEST Request,
    _In_ NTSTATUS Status)
{
    PSPBCX_REQUEST_CONTEXT Context = SpbCxGetRequestContext(Request);
    WDFFILEOBJECT FileObject = WdfRequestGetFileObject(Request);
    PSPBCX_TARGET_CONTEXT Target = FileObject != NULL ? SpbCxGetTargetContext(FileObject) : NULL;
    KIRQL OldIrql;
    BOOLEAN Drain = FALSE;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    switch (Context->Type)
    {
        case SpbRequestTypeLockController:
        case SpbRequestTypeLockConnection:
            if (Device->LockOwner == FileObject)
            {
                if (NT_SUCCESS(Status))
                {
                    Target->HoldsLock = TRUE;
                    Target->InSequence = FALSE;
                    Target->LastDirection = SpbTransferDirectionNone;
                }
                else
                {
                    Device->LockOwner = NULL;
                    Drain = TRUE;
                }
            }
            break;

        case SpbRequestTypeUnlockController:
        case SpbRequestTypeUnlockConnection:
            if (NT_SUCCESS(Status) && Device->LockOwner == FileObject)
            {
                Device->LockOwner = NULL;
                Target->HoldsLock = FALSE;
                Target->InSequence = FALSE;
                Drain = TRUE;
            }
            break;

        case SpbRequestTypeRead:
        case SpbRequestTypeWrite:
        case SpbRequestTypeSequence:
        case SpbRequestTypeOther:
            if (Target != NULL && Target->HoldsLock && Context->TransferCount != 0)
            {
                Target->InSequence = TRUE;
                Target->LastDirection = Context->Transfers[Context->TransferCount - 1].Direction;
            }
            break;

        default:
            break;
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    SpbCxReleaseTransfers(Context);
    WdfRequestComplete(Request, Status);

    if (Drain)
        SpbCxDrainWaiters(Device);
}

static
VOID
NTAPI
SpbCxEvtIoRead(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length)
{
    SpbCxDispatch(Queue, Request, SpbRequestTypeRead, Length, 0, 0);
}

static
VOID
NTAPI
SpbCxEvtIoWrite(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length)
{
    SpbCxDispatch(Queue, Request, SpbRequestTypeWrite, 0, Length, 0);
}

static
VOID
NTAPI
SpbCxEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    SPB_REQUEST_TYPE Type;

    switch (IoControlCode)
    {
        case IOCTL_SPB_LOCK_CONTROLLER:
            Type = SpbRequestTypeLockController;
            break;
        case IOCTL_SPB_UNLOCK_CONTROLLER:
            Type = SpbRequestTypeUnlockController;
            break;
        case IOCTL_SPB_LOCK_CONNECTION:
            Type = SpbRequestTypeLockConnection;
            break;
        case IOCTL_SPB_UNLOCK_CONNECTION:
            Type = SpbRequestTypeUnlockConnection;
            break;
        case IOCTL_SPB_EXECUTE_SEQUENCE:
            Type = SpbRequestTypeSequence;
            break;
        default:
            Type = SpbRequestTypeOther;
            break;
    }

    SpbCxDispatch(Queue, Request, Type, OutputBufferLength, InputBufferLength, IoControlCode);
}

static
VOID
NTAPI
SpbCxEvtIoDefault(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request)
{
    UNREFERENCED_PARAMETER(Queue);
    WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
}

static
VOID
NTAPI
SpbCxEvtIoStop(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG ActionFlags)
{
    UNREFERENCED_PARAMETER(Queue);

    if (ActionFlags & WdfRequestStopRequestCancelable)
        return;

    WdfRequestStopAcknowledge(Request, FALSE);
}

static
VOID
NTAPI
SpbCxEvtIoInCallerContext(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFREQUEST Request)
{
    PSPBCX_DEVICE_CONTEXT Device = SpbCxGetDeviceContext(DeviceHandle);
    WDF_REQUEST_PARAMETERS Parameters;
    PSPBCX_REQUEST_CONTEXT Context;
    NTSTATUS Status;

    Status = SpbCxPrepareRequest(Device, Request, &Context);
    if (!NT_SUCCESS(Status))
    {
        WdfRequestComplete(Request, Status);
        return;
    }

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);

    if (Parameters.Type == WdfRequestTypeDeviceControl ||
        Parameters.Type == WdfRequestTypeDeviceControlInternal)
    {
        switch (Parameters.Parameters.DeviceIoControl.IoControlCode)
        {
            case IOCTL_SPB_EXECUTE_SEQUENCE:
                Status = SpbCxCaptureTransferList(Request, Context);
                if (!NT_SUCCESS(Status))
                {
                    WdfRequestComplete(Request, Status);
                    return;
                }
                break;

            case IOCTL_SPB_LOCK_CONTROLLER:
            case IOCTL_SPB_UNLOCK_CONTROLLER:
            case IOCTL_SPB_LOCK_CONNECTION:
            case IOCTL_SPB_UNLOCK_CONNECTION:
                break;

            default:
                if (Device->EvtIoOtherInCallerContext != NULL)
                {
                    Device->EvtIoOtherInCallerContext(DeviceHandle, Request);
                    return;
                }
                break;
        }
    }

    Status = WdfDeviceEnqueueRequest(DeviceHandle, Request);
    if (!NT_SUCCESS(Status))
        WdfRequestComplete(Request, Status);
}

static
BOOLEAN
NTAPI
SpbCxEvtCxDeviceFileCreate(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFREQUEST Request,
    _In_opt_ WDFFILEOBJECT FileObject)
{
    PSPBCX_DEVICE_CONTEXT Device = SpbCxGetDeviceContext(DeviceHandle);
    PSPBCX_TARGET_CONTEXT Target;
    PUNICODE_STRING FileName;
    NTSTATUS Status;

    if (FileObject == NULL || !Device->Initialized)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return TRUE;
    }

    Target = SpbCxGetTargetContext(FileObject);
    FileName = WdfFileObjectGetFileName(FileObject);
    if (!WdfCxParseConnectionId(FileName, &Target->ConnectionId))
    {
        WdfRequestComplete(Request, STATUS_OBJECT_NAME_INVALID);
        return TRUE;
    }

    Status = WdfCxQueryConnectionProperties(Target->ConnectionId, &Target->Properties);
    if (!NT_SUCCESS(Status))
    {
        WdfRequestComplete(Request, Status);
        return TRUE;
    }

    if (Device->HasTargetAttributes)
    {
        WDF_OBJECT_ATTRIBUTES Attributes = Device->TargetAttributes;

        Attributes.ParentObject = NULL;
        Status = WdfObjectAllocateContext(FileObject, &Attributes, NULL);
        if (Status == STATUS_OBJECT_NAME_EXISTS)
            Status = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(Status) && Device->Config.EvtSpbTargetConnect != NULL)
        Status = Device->Config.EvtSpbTargetConnect(DeviceHandle, (SPBTARGET)FileObject);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Target->Properties, WDFCX_TAG);
        Target->Properties = NULL;
        WdfRequestComplete(Request, Status);
        return TRUE;
    }

    Target->Connected = TRUE;
    WdfRequestComplete(Request, STATUS_SUCCESS);
    return TRUE;
}

static
VOID
NTAPI
SpbCxEvtFileClose(
    _In_ WDFFILEOBJECT FileObject)
{
    WDFDEVICE DeviceHandle = WdfFileObjectGetDevice(FileObject);
    PSPBCX_DEVICE_CONTEXT Device = SpbCxGetDeviceContext(DeviceHandle);
    PSPBCX_TARGET_CONTEXT Target = SpbCxGetTargetContext(FileObject);
    KIRQL OldIrql;
    BOOLEAN Drain = FALSE;

    if (!Target->Connected)
        return;

    Target->Connected = FALSE;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Device->LockOwner == FileObject)
    {
        Device->LockOwner = NULL;
        Target->HoldsLock = FALSE;
        Drain = TRUE;
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    if (Drain)
        SpbCxDrainWaiters(Device);

    if (Device->Config.EvtSpbTargetDisconnect != NULL)
        Device->Config.EvtSpbTargetDisconnect(DeviceHandle, (SPBTARGET)FileObject);

    if (Target->Properties != NULL)
    {
        ExFreePoolWithTag(Target->Properties, WDFCX_TAG);
        Target->Properties = NULL;
    }
}

static
NTSTATUS
NTAPI
SpbCxDdiDeviceInitConfig(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    PWDFCXDEVICE_INIT CxInit;
    WDFCX_FILEOBJECT_CONFIG FileConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;

    if (ClientGlobals == NULL || DeviceInit == NULL)
        return STATUS_INVALID_PARAMETER;

    CxInit = WdfCxDeviceInitAllocate(WdfDriverGlobals, DeviceInit);
    if (CxInit == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    WdfCxClientDeviceInitSetIoType(ClientGlobals, DeviceInit, WdfDeviceIoDirect);
    WdfCxClientDeviceInitSetDeviceType(ClientGlobals, DeviceInit, FILE_DEVICE_CONTROLLER);

    RtlZeroMemory(&FileConfig, sizeof(FileConfig));
    FileConfig.Size = sizeof(FileConfig);
    FileConfig.EvtCxDeviceFileCreate = SpbCxEvtCxDeviceFileCreate;
    FileConfig.EvtFileClose = SpbCxEvtFileClose;
    FileConfig.AutoForwardCleanupClose = WdfFalse;
    FileConfig.FileObjectClass = WdfFileObjectWdfCannotUseFsContexts;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SPBCX_TARGET_CONTEXT);
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    WdfCxDeviceInitSetFileObjectConfig(WdfDriverGlobals, CxInit, &FileConfig, &Attributes);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SPBCX_REQUEST_CONTEXT);
    Attributes.EvtCleanupCallback = SpbCxEvtRequestContextCleanup;
    WdfCxDeviceInitSetRequestAttributes(WdfDriverGlobals, CxInit, &Attributes);

    WdfCxDeviceInitSetIoInCallerContextCallback(WdfDriverGlobals, CxInit, SpbCxEvtIoInCallerContext);

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SpbCxDdiDeviceInitialize(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSPB_CONTROLLER_CONFIG Config)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    PSPBCX_DEVICE_CONTEXT Device;
    NTSTATUS Status;

    if (ClientGlobals == NULL || DeviceHandle == NULL || Config == NULL ||
        Config->Size != sizeof(SPB_CONTROLLER_CONFIG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Config->ControllerDispatchType != WdfIoQueueDispatchSequential &&
        Config->ControllerDispatchType != WdfIoQueueDispatchParallel)
    {
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SPBCX_DEVICE_CONTEXT);
    Status = WdfObjectAllocateContext(DeviceHandle, &Attributes, (PVOID *)&Device);
    if (Status == STATUS_OBJECT_NAME_EXISTS)
        return STATUS_INVALID_DEVICE_STATE;
    if (!NT_SUCCESS(Status))
        return Status;

    Device->ClientGlobals = ClientGlobals;
    Device->Config = *Config;
    KeInitializeSpinLock(&Device->Lock);

    WDF_IO_QUEUE_CONFIG_INIT(&QueueConfig, WdfIoQueueDispatchManual);
    QueueConfig.PowerManaged = WdfFalse;
    Status = WdfIoQueueCreate(DeviceHandle, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Device->WaitQueue);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, Config->ControllerDispatchType);
    QueueConfig.PowerManaged = Config->PowerManaged;
    QueueConfig.EvtIoRead = SpbCxEvtIoRead;
    QueueConfig.EvtIoWrite = SpbCxEvtIoWrite;
    QueueConfig.EvtIoDeviceControl = SpbCxEvtIoDeviceControl;
    QueueConfig.EvtIoInternalDeviceControl = SpbCxEvtIoDeviceControl;
    QueueConfig.EvtIoDefault = SpbCxEvtIoDefault;
    QueueConfig.EvtIoStop = SpbCxEvtIoStop;

    Status = WdfIoQueueCreate(DeviceHandle, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Device->Queue);
    if (!NT_SUCCESS(Status))
        return Status;

    Device->Initialized = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
SpbCxDdiControllerSetIoOtherCallback(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PFN_SPB_CONTROLLER_OTHER EvtSpbIoOther,
    _In_opt_ PFN_WDF_IO_IN_CALLER_CONTEXT EvtIoInCallerContext)
{
    PSPBCX_DEVICE_CONTEXT Device = SpbCxGetDeviceContext(DeviceHandle);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Device == NULL)
        return;

    Device->EvtIoOther = EvtSpbIoOther;
    Device->EvtIoOtherInCallerContext = EvtIoInCallerContext;
}

static
VOID
NTAPI
SpbCxDdiControllerSetRequestAttributes(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PWDF_OBJECT_ATTRIBUTES Attributes)
{
    PSPBCX_DEVICE_CONTEXT Device = SpbCxGetDeviceContext(DeviceHandle);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Device == NULL || Attributes == NULL || Attributes->Size != sizeof(WDF_OBJECT_ATTRIBUTES))
        return;

    Device->RequestAttributes = *Attributes;
    Device->HasRequestAttributes = Attributes->ContextTypeInfo != NULL;
}

static
VOID
NTAPI
SpbCxDdiControllerSetTargetAttributes(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PWDF_OBJECT_ATTRIBUTES Attributes)
{
    PSPBCX_DEVICE_CONTEXT Device = SpbCxGetDeviceContext(DeviceHandle);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Device == NULL || Attributes == NULL || Attributes->Size != sizeof(WDF_OBJECT_ATTRIBUTES))
        return;

    Device->TargetAttributes = *Attributes;
    Device->HasTargetAttributes = Attributes->ContextTypeInfo != NULL;
}

static
VOID
NTAPI
SpbCxDdiTargetGetConnectionParameters(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ SPBTARGET SpbTarget,
    _Out_ PSPB_CONNECTION_PARAMETERS Parameters)
{
    PSPBCX_TARGET_CONTEXT Target = SpbCxGetTargetContext((WDFFILEOBJECT)SpbTarget);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Parameters == NULL || Parameters->Size != sizeof(SPB_CONNECTION_PARAMETERS))
        return;

    Parameters->ConnectionTag = NULL;
    Parameters->ConnectionParameters = Target != NULL ? Target->Properties : NULL;
}

static
WDFFILEOBJECT
NTAPI
SpbCxDdiTargetGetFileObject(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ SPBTARGET SpbTarget)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    return (WDFFILEOBJECT)SpbTarget;
}

static
SPBTARGET
NTAPI
SpbCxDdiRequestGetTarget(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ SPBREQUEST SpbRequest)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    return (SPBTARGET)WdfRequestGetFileObject((WDFREQUEST)SpbRequest);
}

static
WDFDEVICE
NTAPI
SpbCxDdiRequestGetController(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ SPBREQUEST SpbRequest)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    return WdfIoQueueGetDevice(WdfRequestGetIoQueue((WDFREQUEST)SpbRequest));
}

static
VOID
NTAPI
SpbCxDdiRequestGetParameters(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ SPBREQUEST SpbRequest,
    _Out_ PSPB_REQUEST_PARAMETERS Parameters)
{
    PSPBCX_REQUEST_CONTEXT Context = SpbCxGetRequestContext((WDFREQUEST)SpbRequest);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Parameters == NULL || Parameters->Size != sizeof(SPB_REQUEST_PARAMETERS) || Context == NULL)
        return;

    Parameters->Type = Context->Type;
    Parameters->Position = Context->Position;
    Parameters->PreviousTransferDirection = Context->PreviousTransferDirection;
    Parameters->Length = Context->Length;
    Parameters->SequenceTransferCount = Context->TransferCount;
}

static
VOID
NTAPI
SpbCxDdiRequestGetTransferParameters(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ SPBREQUEST SpbRequest,
    _In_ ULONG Index,
    _Out_opt_ PSPB_TRANSFER_DESCRIPTOR Descriptor,
    _Out_opt_ PMDL *Mdl)
{
    PSPBCX_REQUEST_CONTEXT Context = SpbCxGetRequestContext((WDFREQUEST)SpbRequest);
    PSPBCX_TRANSFER Transfer = NULL;

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Context != NULL && Context->Captured && Index < Context->TransferCount)
        Transfer = &Context->Transfers[Index];

    if (Descriptor != NULL && Descriptor->Size == sizeof(SPB_TRANSFER_DESCRIPTOR))
    {
        Descriptor->Direction = Transfer != NULL ? Transfer->Direction : SpbTransferDirectionNone;
        Descriptor->TransferLength = Transfer != NULL ? Transfer->Length : 0;
        Descriptor->DelayInUs = Transfer != NULL ? Transfer->DelayInUs : 0;
    }

    if (Mdl != NULL)
        *Mdl = Transfer != NULL ? Transfer->Mdl : NULL;
}

static
VOID
NTAPI
SpbCxDdiRequestComplete(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ SPBREQUEST SpbRequest,
    _In_ NTSTATUS Status)
{
    WDFREQUEST Request = (WDFREQUEST)SpbRequest;
    WDFDEVICE DeviceHandle = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));

    UNREFERENCED_PARAMETER(ClientGlobals);
    SpbCxRequestComplete(SpbCxGetDeviceContext(DeviceHandle), Request, Status);
}

static
NTSTATUS
NTAPI
SpbCxDdiRequestCaptureIoOtherTransferList(
    _In_ PSPB_DRIVER_GLOBALS ClientGlobals,
    _In_ SPBREQUEST SpbRequest)
{
    WDFREQUEST Request = (WDFREQUEST)SpbRequest;
    PSPBCX_REQUEST_CONTEXT Context = SpbCxGetRequestContext(Request);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Context == NULL)
        return STATUS_INVALID_PARAMETER;

    return SpbCxCaptureTransferList(Request, Context);
}

static PVOID SpbCxFunctions[SpbFunctionTableNumEntries] =
{
    SpbCxDdiDeviceInitConfig,
    SpbCxDdiDeviceInitialize,
    SpbCxDdiControllerSetIoOtherCallback,
    SpbCxDdiControllerSetRequestAttributes,
    SpbCxDdiControllerSetTargetAttributes,
    SpbCxDdiTargetGetConnectionParameters,
    SpbCxDdiTargetGetFileObject,
    SpbCxDdiRequestGetTarget,
    SpbCxDdiRequestGetController,
    SpbCxDdiRequestGetParameters,
    SpbCxDdiRequestGetTransferParameters,
    SpbCxDdiRequestComplete,
    SpbCxDdiRequestCaptureIoOtherTransferList
};

static
NTSTATUS
NTAPI
SpbCxLibraryBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    return WdfCxBindClient(ClassBindInfo,
                           ClientGlobals,
                           SpbCxFunctions,
                           RTL_NUMBER_OF(SpbCxFunctions),
                           1);
}

static
VOID
NTAPI
SpbCxLibraryUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    WdfCxUnbindClient(ClassBindInfo);
}

static WDF_CLASS_LIBRARY_INFO SpbCxLibraryInfo =
{
    sizeof(WDF_CLASS_LIBRARY_INFO),
    {1, 0, 0},
    NULL,
    NULL,
    SpbCxLibraryBindClient,
    SpbCxLibraryUnbindClient
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    return WdfCxRegisterLibrary(DriverObject,
                                RegistryPath,
                                L"\\Device\\SPBCx",
                                &SpbCxLibraryInfo);
}
