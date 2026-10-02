/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter-initiated I/O
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

CONST GUID FltpTargetInstanceEcp =
    {0x7c5f6a1e, 0x2b63, 0x4a8e, {0x9d, 0x27, 0x5b, 0x1f, 0x0e, 0x6a, 0x3c, 0x41}};

typedef struct _FLTP_ASYNC_READ_WRITE
{
    PFLT_COMPLETED_ASYNC_IO_CALLBACK Callback;
    PVOID Context;
} FLTP_ASYNC_READ_WRITE, *PFLTP_ASYNC_READ_WRITE;

static
NTSTATUS
NTAPI
FltpGeneratedCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PFLTP_IRP_CTRL IrpCtrl = Context;
    PFLT_COMPLETED_ASYNC_IO_CALLBACK Callback;
    PVOID CallbackContext;
    BOOLEAN FreeIrp = TRUE;
    PMDL Mdl, Next;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IrpCtrl->Data.IoStatus = Irp->IoStatus;

    for (Mdl = Irp->MdlAddress; Mdl != NULL; Mdl = Next)
    {
        Next = Mdl->Next;
        if (Mdl != IrpCtrl->CallerMdl)
        {
            Mdl->Next = NULL;
            FltpFreeMdl(Mdl);
        }
    }
    Irp->MdlAddress = NULL;

    KeAcquireSpinLock(&FltGlobals.GeneratedLock, &OldIrql);
    if (IrpCtrl->Flags & FLTP_IRPCTRL_CANCELLING)
    {
        IrpCtrl->Flags |= FLTP_IRPCTRL_FREE_IRP;
        FreeIrp = FALSE;
    }
    else
    {
        IrpCtrl->Irp = NULL;
    }
    IrpCtrl->Flags &= ~FLTP_IRPCTRL_BUSY;
    KeReleaseSpinLock(&FltGlobals.GeneratedLock, OldIrql);

    if (FreeIrp)
    {
        IoFreeIrp(Irp);
    }

    Callback = IrpCtrl->AsyncCallback;
    CallbackContext = IrpCtrl->AsyncContext;
    if (Callback != NULL)
    {
        IrpCtrl->AsyncCallback = NULL;
        Callback(&IrpCtrl->Data, CallbackContext);
    }
    else
    {
        KeSetEvent(&IrpCtrl->GeneratedEvent, IO_NO_INCREMENT, FALSE);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
FltpPerformIo(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_opt_ PFLT_COMPLETED_ASYNC_IO_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext,
    _In_ BOOLEAN OwnMdl)
{
    PFLT_IO_PARAMETER_BLOCK Iopb = &IrpCtrl->Iopb;
    PDEVICE_OBJECT DeviceObject = IrpCtrl->Volume->DeviceObject;
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;
    PIRP Irp;

    if (Iopb->TargetFileObject == NULL)
    {
        Iopb->TargetFileObject = IrpCtrl->GeneratedFileObject;
    }

    IrpCtrl->AsyncCallback = Callback;
    IrpCtrl->AsyncContext = CallbackContext;
    KeClearEvent(&IrpCtrl->GeneratedEvent);
    KeClearEvent(&IrpCtrl->SyncEvent);

    Status = FltpAttachNodes(IrpCtrl, IrpCtrl->Initiator);
    Irp = NT_SUCCESS(Status) ? IoAllocateIrp(DeviceObject->StackSize + 1, FALSE) : NULL;
    if (Irp == NULL)
    {
        IrpCtrl->Data.IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IrpCtrl->Data.IoStatus.Information = 0;
        if (Callback != NULL)
        {
            IrpCtrl->AsyncCallback = NULL;
            Callback(&IrpCtrl->Data, CallbackContext);
        }
        else
        {
            KeSetEvent(&IrpCtrl->GeneratedEvent, IO_NO_INCREMENT, FALSE);
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = Iopb->TargetFileObject;
    Irp->RequestorMode = IrpCtrl->Data.RequestorMode;
    Irp->Flags = Iopb->IrpFlags;
    Irp->UserIosb = &IrpCtrl->Data.IoStatus;

    IoSetNextIrpStackLocation(Irp);
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = Iopb->MajorFunction;
    Stack->MinorFunction = Iopb->MinorFunction;
    Stack->DeviceObject = DeviceObject;
    FltpParametersToIrp(IrpCtrl, Irp, Stack);
    IoSetCompletionRoutine(Irp, FltpGeneratedCompletion, IrpCtrl, TRUE, TRUE, TRUE);
    IoSetNextIrpStackLocation(Irp);

    IrpCtrl->Flags = (IrpCtrl->Flags & FLTP_IRPCTRL_GENERATED) | FLTP_IRPCTRL_BUSY;
    IrpCtrl->Irp = Irp;
    IrpCtrl->DeviceObject = DeviceObject;
    IrpCtrl->OriginalMdl = Irp->MdlAddress;
    IrpCtrl->OriginalUserBuffer = Irp->UserBuffer;
    IrpCtrl->OriginalSystemBuffer = Irp->AssociatedIrp.SystemBuffer;
    IrpCtrl->CallerMdl = OwnMdl ? NULL : Irp->MdlAddress;
    IrpCtrl->NextNode = 0;
    IrpCtrl->PostNode = -1;
    IrpCtrl->PendState = FLTP_PEND_NONE;
    IrpCtrl->PostPendState = FLTP_PEND_NONE;
    IrpCtrl->LowerMdl = NULL;
    IrpCtrl->PostMdls = NULL;
    IrpCtrl->CurrentNode = NULL;
    IrpCtrl->NewSystemBuffer = NULL;
    IrpCtrl->Data.Flags = FLTFL_CALLBACK_DATA_IRP_OPERATION | FLTFL_CALLBACK_DATA_GENERATED_IO;
    *(PETHREAD *)&IrpCtrl->Data.Thread = PsGetCurrentThread();
    IrpCtrl->Data.IoStatus.Status = STATUS_SUCCESS;
    IrpCtrl->Data.IoStatus.Information = 0;
    Iopb->TargetInstance = IrpCtrl->Initiator;

    FltpReferenceIrpCtrl(IrpCtrl);
    return FltpStartIrp(IrpCtrl);
}

NTSTATUS
FLTAPI
FltAllocateCallbackDataEx(
    _In_ PFLT_INSTANCE Instance,
    _In_opt_ PFILE_OBJECT FileObject,
    _In_ FLT_ALLOCATE_CALLBACK_DATA_FLAGS Flags,
    _Outptr_ PFLT_CALLBACK_DATA *RetNewCallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl;

    UNREFERENCED_PARAMETER(Flags);

    *RetNewCallbackData = NULL;

    if (!ExAcquireRundownProtection(&Instance->Base.RundownRef))
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    IrpCtrl = FltpAllocateIrpCtrl(Instance->Volume, 0);
    if (IrpCtrl == NULL)
    {
        ExReleaseRundownProtection(&Instance->Base.RundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IrpCtrl->Nodes = NULL;
    IrpCtrl->Flags = FLTP_IRPCTRL_GENERATED;
    IrpCtrl->Initiator = Instance;
    IrpCtrl->Data.Flags = FLTFL_CALLBACK_DATA_IRP_OPERATION | FLTFL_CALLBACK_DATA_GENERATED_IO;
    *(PETHREAD *)&IrpCtrl->Data.Thread = PsGetCurrentThread();
    IrpCtrl->Data.RequestorMode = KernelMode;
    IrpCtrl->GeneratedFileObject = FileObject;
    KeInitializeEvent(&IrpCtrl->GeneratedEvent, NotificationEvent, FALSE);

    *RetNewCallbackData = &IrpCtrl->Data;
    return STATUS_SUCCESS;
}

NTSTATUS
FLTAPI
FltAllocateCallbackData(
    _In_ PFLT_INSTANCE Instance,
    _In_opt_ PFILE_OBJECT FileObject,
    _Outptr_ PFLT_CALLBACK_DATA *RetNewCallbackData)
{
    return FltAllocateCallbackDataEx(Instance, FileObject, 0, RetNewCallbackData);
}

VOID
FLTAPI
FltFreeCallbackData(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);
    PFLT_INSTANCE Instance = IrpCtrl->Initiator;

    FltpDereferenceIrpCtrl(IrpCtrl);
    ExReleaseRundownProtection(&Instance->Base.RundownRef);
}

VOID
FLTAPI
FltReuseCallbackData(
    _Inout_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);
    PFLT_IO_PARAMETER_BLOCK Iopb = &IrpCtrl->Iopb;

    Iopb->IrpFlags = 0;
    Iopb->MajorFunction = 0;
    Iopb->MinorFunction = 0;
    Iopb->OperationFlags = 0;
    Iopb->TargetFileObject = NULL;
    Iopb->TargetInstance = NULL;
    RtlZeroMemory(&Iopb->Parameters, sizeof(Iopb->Parameters));
    CallbackData->Flags = FLTFL_CALLBACK_DATA_IRP_OPERATION | FLTFL_CALLBACK_DATA_GENERATED_IO;
    CallbackData->IoStatus.Status = STATUS_SUCCESS;
    CallbackData->IoStatus.Information = 0;
    CallbackData->TagData = NULL;
    CallbackData->RequestorMode = KernelMode;
    IrpCtrl->CallerMdl = NULL;
}

VOID
FLTAPI
FltPerformSynchronousIo(
    _Inout_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);

    (VOID)FltpPerformIo(IrpCtrl, NULL, NULL, FALSE);
    KeWaitForSingleObject(&IrpCtrl->GeneratedEvent, Executive, KernelMode, FALSE, NULL);
}

NTSTATUS
FLTAPI
FltPerformAsynchronousIo(
    _Inout_ PFLT_CALLBACK_DATA CallbackData,
    _In_ PFLT_COMPLETED_ASYNC_IO_CALLBACK CallbackRoutine,
    _In_ PVOID CallbackContext)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);

    if (IrpCtrl->Iopb.MajorFunction == IRP_MJ_CREATE)
    {
        CallbackData->IoStatus.Status = STATUS_FLT_INVALID_ASYNCHRONOUS_REQUEST;
        CallbackData->IoStatus.Information = 0;
        CallbackRoutine(CallbackData, CallbackContext);
        return STATUS_FLT_INVALID_ASYNCHRONOUS_REQUEST;
    }

    return FltpPerformIo(IrpCtrl, CallbackRoutine, CallbackContext, FALSE);
}

NTSTATUS
FltpGeneratedIo(
    _In_ PFLT_INSTANCE Instance,
    _In_opt_ PFILE_OBJECT FileObject,
    _In_ UCHAR MajorFunction,
    _In_ UCHAR MinorFunction,
    _In_ ULONG IrpFlags,
    _In_ UCHAR OperationFlags,
    _In_ CONST FLT_PARAMETERS *Parameters,
    _Out_opt_ PULONG_PTR Information)
{
    PFLT_CALLBACK_DATA Data;
    NTSTATUS Status;

    if (Information != NULL)
    {
        *Information = 0;
    }

    Status = FltAllocateCallbackData(Instance, FileObject, &Data);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Data->Iopb->MajorFunction = MajorFunction;
    Data->Iopb->MinorFunction = MinorFunction;
    Data->Iopb->IrpFlags = IrpFlags | IRP_SYNCHRONOUS_API;
    Data->Iopb->OperationFlags = OperationFlags;
    Data->Iopb->Parameters = *Parameters;

    FltPerformSynchronousIo(Data);

    Status = Data->IoStatus.Status;
    if (Information != NULL)
    {
        *Information = Data->IoStatus.Information;
    }

    FltFreeCallbackData(Data);
    return Status;
}

static
NTSTATUS
FltpInsertTargetEcp(
    _In_ PFLT_INSTANCE Instance,
    _Inout_ PIO_DRIVER_CREATE_CONTEXT DriverContext,
    _Out_ PBOOLEAN OwnList)
{
    PFLT_INSTANCE *Context;
    NTSTATUS Status;

    *OwnList = FALSE;

    if (DriverContext->ExtraCreateParameter == NULL)
    {
        Status = FsRtlAllocateExtraCreateParameterList(0, &DriverContext->ExtraCreateParameter);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        *OwnList = TRUE;
    }

    Status = FsRtlAllocateExtraCreateParameter(&FltpTargetInstanceEcp,
                                               sizeof(PFLT_INSTANCE),
                                               0,
                                               NULL,
                                               FLT_TAG_GENERAL,
                                               (PVOID *)&Context);
    if (NT_SUCCESS(Status))
    {
        *Context = Instance;
        Status = FsRtlInsertExtraCreateParameter(DriverContext->ExtraCreateParameter, Context);
        if (!NT_SUCCESS(Status))
        {
            FsRtlFreeExtraCreateParameter(Context);
        }
    }

    if (!NT_SUCCESS(Status) && *OwnList)
    {
        FsRtlFreeExtraCreateParameterList(DriverContext->ExtraCreateParameter);
        DriverContext->ExtraCreateParameter = NULL;
        *OwnList = FALSE;
    }
    return Status;
}

static
VOID
FltpRemoveTargetEcp(
    _Inout_ PIO_DRIVER_CREATE_CONTEXT DriverContext,
    _In_ BOOLEAN OwnList)
{
    PVOID Context;

    if (OwnList)
    {
        FsRtlFreeExtraCreateParameterList(DriverContext->ExtraCreateParameter);
        DriverContext->ExtraCreateParameter = NULL;
        return;
    }

    if (NT_SUCCESS(FsRtlRemoveExtraCreateParameter(DriverContext->ExtraCreateParameter,
                                                   &FltpTargetInstanceEcp,
                                                   &Context,
                                                   NULL)))
    {
        FsRtlFreeExtraCreateParameter(Context);
    }
}

NTSTATUS
FLTAPI
FltCreateFileEx2(
    _In_ PFLT_FILTER Filter,
    _In_opt_ PFLT_INSTANCE Instance,
    _Out_ PHANDLE FileHandle,
    _Outptr_opt_ PFILE_OBJECT *FileObject,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_reads_bytes_opt_(EaLength) PVOID EaBuffer,
    _In_ ULONG EaLength,
    _In_ ULONG Flags,
    _In_opt_ PIO_DRIVER_CREATE_CONTEXT DriverContext)
{
    IO_DRIVER_CREATE_CONTEXT LocalContext;
    BOOLEAN OwnList = FALSE;
    NTSTATUS Status;
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Filter);

    *FileHandle = NULL;
    if (FileObject != NULL)
    {
        *FileObject = NULL;
    }

    IoInitializeDriverCreateContext(&LocalContext);
    if (DriverContext != NULL)
    {
        if (!IO_DRIVER_CREATE_CONTEXT_IS_MIN_SIZE(DriverContext))
        {
            return STATUS_INVALID_PARAMETER;
        }
        LocalContext.ExtraCreateParameter = DriverContext->ExtraCreateParameter;
        LocalContext.TxnParameters = DriverContext->TxnParameters;
    }

    if (Instance != NULL)
    {
        if (!ExAcquireRundownProtection(&Instance->Base.RundownRef))
        {
            return STATUS_FLT_DELETING_OBJECT;
        }

        Status = FltpInsertTargetEcp(Instance, &LocalContext, &OwnList);
        if (!NT_SUCCESS(Status))
        {
            ExReleaseRundownProtection(&Instance->Base.RundownRef);
            return Status;
        }
        LocalContext.DeviceObjectHint = Instance->Volume->DeviceObject;
    }

    Status = IoCreateFileEx(FileHandle,
                            DesiredAccess,
                            ObjectAttributes,
                            IoStatusBlock,
                            AllocationSize,
                            FileAttributes,
                            ShareAccess,
                            CreateDisposition,
                            CreateOptions,
                            EaBuffer,
                            EaLength,
                            CreateFileTypeNone,
                            NULL,
                            Flags | IO_NO_PARAMETER_CHECKING,
                            &LocalContext);

    if (Instance != NULL)
    {
        FltpRemoveTargetEcp(&LocalContext, OwnList);
        ExReleaseRundownProtection(&Instance->Base.RundownRef);
    }

    if (NT_SUCCESS(Status) && Status != STATUS_REPARSE && FileObject != NULL)
    {
        Status = ObReferenceObjectByHandle(*FileHandle, 0, *IoFileObjectType, KernelMode, (PVOID *)FileObject, NULL);
        if (!NT_SUCCESS(Status))
        {
            ZwClose(*FileHandle);
            *FileHandle = NULL;
        }
    }

    return Status;
}

NTSTATUS
FLTAPI
FltCreateFileEx(
    _In_ PFLT_FILTER Filter,
    _In_opt_ PFLT_INSTANCE Instance,
    _Out_ PHANDLE FileHandle,
    _Outptr_opt_ PFILE_OBJECT *FileObject,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_reads_bytes_opt_(EaLength) PVOID EaBuffer,
    _In_ ULONG EaLength,
    _In_ ULONG Flags)
{
    return FltCreateFileEx2(Filter,
                            Instance,
                            FileHandle,
                            FileObject,
                            DesiredAccess,
                            ObjectAttributes,
                            IoStatusBlock,
                            AllocationSize,
                            FileAttributes,
                            ShareAccess,
                            CreateDisposition,
                            CreateOptions,
                            EaBuffer,
                            EaLength,
                            Flags,
                            NULL);
}

NTSTATUS
FLTAPI
FltCreateFile(
    _In_ PFLT_FILTER Filter,
    _In_opt_ PFLT_INSTANCE Instance,
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_reads_bytes_opt_(EaLength) PVOID EaBuffer,
    _In_ ULONG EaLength,
    _In_ ULONG Flags)
{
    return FltCreateFileEx2(Filter,
                            Instance,
                            FileHandle,
                            NULL,
                            DesiredAccess,
                            ObjectAttributes,
                            IoStatusBlock,
                            AllocationSize,
                            FileAttributes,
                            ShareAccess,
                            CreateDisposition,
                            CreateOptions,
                            EaBuffer,
                            EaLength,
                            Flags,
                            NULL);
}

NTSTATUS
FLTAPI
FltClose(
    _In_ HANDLE FileHandle)
{
    PAGED_CODE();

    return ZwClose(FileHandle);
}

static
VOID
FLTAPI
FltpAsyncReadWriteComplete(
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _In_ PFLT_CONTEXT Context)
{
    PFLTP_ASYNC_READ_WRITE Async = Context;

    Async->Callback(CallbackData, Async->Context);
    ExFreePoolWithTag(Async, FLT_TAG_GENERAL);
    FltFreeCallbackData(CallbackData);
}

static
NTSTATUS
FltpReadWrite(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _In_opt_ PVOID Buffer,
    _In_ FLT_IO_OPERATION_FLAGS Flags,
    _Out_opt_ PULONG BytesTransferred,
    _In_opt_ PFLT_COMPLETED_ASYNC_IO_CALLBACK CallbackRoutine,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PULONG Key,
    _In_opt_ PMDL Mdl,
    _In_ BOOLEAN Write)
{
    PFLTP_ASYNC_READ_WRITE Async = NULL;
    LARGE_INTEGER Offset, SavedOffset;
    PFLT_CALLBACK_DATA Data;
    PFLT_IO_PARAMETER_BLOCK Iopb;
    BOOLEAN OwnMdl = FALSE;
    NTSTATUS Status;

    if (BytesTransferred != NULL && CallbackRoutine == NULL)
    {
        *BytesTransferred = 0;
    }

    if (ByteOffset == NULL ||
        (ByteOffset->LowPart == FILE_USE_FILE_POINTER_POSITION && ByteOffset->HighPart == -1))
    {
        if (!(FileObject->Flags & FO_SYNCHRONOUS_IO))
        {
            return STATUS_INVALID_PARAMETER;
        }
        Offset = FileObject->CurrentByteOffset;
    }
    else
    {
        Offset = *ByteOffset;
    }

    Status = FltAllocateCallbackData(Instance, FileObject, &Data);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Iopb = Data->Iopb;
    Iopb->MajorFunction = Write ? IRP_MJ_WRITE : IRP_MJ_READ;
    Iopb->IrpFlags = Write ? IRP_WRITE_OPERATION : IRP_READ_OPERATION;
    if (CallbackRoutine == NULL)
    {
        Iopb->IrpFlags |= IRP_SYNCHRONOUS_API;
    }
    if (Flags & FLTFL_IO_OPERATION_NON_CACHED)
    {
        Iopb->IrpFlags |= IRP_NOCACHE;
    }
    if (Flags & FLTFL_IO_OPERATION_PAGING)
    {
        Iopb->IrpFlags |= IRP_PAGING_IO | IRP_NOCACHE;
    }
    if (Flags & FLTFL_IO_OPERATION_SYNCHRONOUS_PAGING)
    {
        Iopb->IrpFlags |= IRP_SYNCHRONOUS_PAGING_IO;
    }

    if (Mdl == NULL && Buffer != NULL && Length != 0 && (Flags & FLTFL_IO_OPERATION_PAGING))
    {
        Mdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, NULL);
        if (Mdl == NULL)
        {
            FltFreeCallbackData(Data);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        MmBuildMdlForNonPagedPool(Mdl);
        OwnMdl = TRUE;
    }

    if (Write)
    {
        Iopb->Parameters.Write.Length = Length;
        Iopb->Parameters.Write.Key = Key ? *Key : 0;
        Iopb->Parameters.Write.ByteOffset = Offset;
        Iopb->Parameters.Write.WriteBuffer = Buffer;
        Iopb->Parameters.Write.MdlAddress = Mdl;
    }
    else
    {
        Iopb->Parameters.Read.Length = Length;
        Iopb->Parameters.Read.Key = Key ? *Key : 0;
        Iopb->Parameters.Read.ByteOffset = Offset;
        Iopb->Parameters.Read.ReadBuffer = Buffer;
        Iopb->Parameters.Read.MdlAddress = Mdl;
    }

    if (CallbackRoutine != NULL)
    {
        Async = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Async), FLT_TAG_GENERAL);
        if (Async == NULL)
        {
            if (OwnMdl)
            {
                IoFreeMdl(Mdl);
            }
            FltFreeCallbackData(Data);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Async->Callback = CallbackRoutine;
        Async->Context = CallbackContext;
        Status = FltpPerformIo(FLTP_DATA_TO_IRP_CTRL(Data), FltpAsyncReadWriteComplete, Async, OwnMdl);
        return (Status == STATUS_INSUFFICIENT_RESOURCES) ? Status : STATUS_PENDING;
    }

    SavedOffset = FileObject->CurrentByteOffset;
    (VOID)FltpPerformIo(FLTP_DATA_TO_IRP_CTRL(Data), NULL, NULL, OwnMdl);
    KeWaitForSingleObject(&FLTP_DATA_TO_IRP_CTRL(Data)->GeneratedEvent, Executive, KernelMode, FALSE, NULL);

    if (Flags & FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET)
    {
        FileObject->CurrentByteOffset = SavedOffset;
    }

    Status = Data->IoStatus.Status;
    if (BytesTransferred != NULL)
    {
        *BytesTransferred = (ULONG)Data->IoStatus.Information;
    }

    FltFreeCallbackData(Data);
    return Status;
}

NTSTATUS
FLTAPI
FltReadFileEx(
    _In_ PFLT_INSTANCE InitiatingInstance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _Out_writes_bytes_to_opt_(Length, *BytesRead) PVOID Buffer,
    _In_ FLT_IO_OPERATION_FLAGS Flags,
    _Out_opt_ PULONG BytesRead,
    _In_opt_ PFLT_COMPLETED_ASYNC_IO_CALLBACK CallbackRoutine,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PULONG Key,
    _In_opt_ PMDL Mdl)
{
    return FltpReadWrite(InitiatingInstance,
                         FileObject,
                         ByteOffset,
                         Length,
                         Buffer,
                         Flags,
                         BytesRead,
                         CallbackRoutine,
                         CallbackContext,
                         Key,
                         Mdl,
                         FALSE);
}

NTSTATUS
FLTAPI
FltReadFile(
    _In_ PFLT_INSTANCE InitiatingInstance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _Out_writes_bytes_to_(Length, *BytesRead) PVOID Buffer,
    _In_ FLT_IO_OPERATION_FLAGS Flags,
    _Out_opt_ PULONG BytesRead,
    _In_opt_ PFLT_COMPLETED_ASYNC_IO_CALLBACK CallbackRoutine,
    _In_opt_ PVOID CallbackContext)
{
    return FltpReadWrite(InitiatingInstance,
                         FileObject,
                         ByteOffset,
                         Length,
                         Buffer,
                         Flags,
                         BytesRead,
                         CallbackRoutine,
                         CallbackContext,
                         NULL,
                         NULL,
                         FALSE);
}

NTSTATUS
FLTAPI
FltWriteFileEx(
    _In_ PFLT_INSTANCE InitiatingInstance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_opt_(Length) PVOID Buffer,
    _In_ FLT_IO_OPERATION_FLAGS Flags,
    _Out_opt_ PULONG BytesWritten,
    _In_opt_ PFLT_COMPLETED_ASYNC_IO_CALLBACK CallbackRoutine,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PULONG Key,
    _In_opt_ PMDL Mdl)
{
    return FltpReadWrite(InitiatingInstance,
                         FileObject,
                         ByteOffset,
                         Length,
                         Buffer,
                         Flags,
                         BytesWritten,
                         CallbackRoutine,
                         CallbackContext,
                         Key,
                         Mdl,
                         TRUE);
}

NTSTATUS
FLTAPI
FltWriteFile(
    _In_ PFLT_INSTANCE InitiatingInstance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ FLT_IO_OPERATION_FLAGS Flags,
    _Out_opt_ PULONG BytesWritten,
    _In_opt_ PFLT_COMPLETED_ASYNC_IO_CALLBACK CallbackRoutine,
    _In_opt_ PVOID CallbackContext)
{
    return FltpReadWrite(InitiatingInstance,
                         FileObject,
                         ByteOffset,
                         Length,
                         Buffer,
                         Flags,
                         BytesWritten,
                         CallbackRoutine,
                         CallbackContext,
                         NULL,
                         NULL,
                         TRUE);
}

NTSTATUS
FLTAPI
FltQueryInformationFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_to_(Length, *LengthReturned) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _Out_opt_ PULONG LengthReturned)
{
    FLT_PARAMETERS Parameters;
    ULONG_PTR Information;
    NTSTATUS Status;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.QueryFileInformation.Length = Length;
    Parameters.QueryFileInformation.FileInformationClass = FileInformationClass;
    Parameters.QueryFileInformation.InfoBuffer = FileInformation;

    Status = FltpGeneratedIo(Instance, FileObject, IRP_MJ_QUERY_INFORMATION, 0, 0, 0, &Parameters, &Information);
    if (LengthReturned != NULL)
    {
        *LengthReturned = (ULONG)Information;
    }
    return Status;
}

static
NTSTATUS
FltpOpenRenameTarget(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ PFILE_RENAME_INFORMATION Information,
    _Out_ PHANDLE TargetHandle,
    _Out_ PFILE_OBJECT *TargetFileObject)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Name;
    NTSTATUS Status;

    Name.Buffer = Information->FileName;
    Name.Length = Name.MaximumLength = (USHORT)Information->FileNameLength;
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_KERNEL_HANDLE |
                                   ((FileObject->Flags & FO_OPENED_CASE_SENSITIVE) ? 0 : OBJ_CASE_INSENSITIVE),
                               Information->RootDirectory,
                               NULL);

    Status = FltCreateFileEx2(Instance->Filter,
                              Instance,
                              TargetHandle,
                              TargetFileObject,
                              FILE_WRITE_DATA | SYNCHRONIZE,
                              &ObjectAttributes,
                              &IoStatusBlock,
                              NULL,
                              0,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              FILE_OPEN,
                              FILE_OPEN_FOR_BACKUP_INTENT,
                              NULL,
                              0,
                              IO_OPEN_TARGET_DIRECTORY | IO_IGNORE_SHARE_ACCESS_CHECK,
                              NULL);
    if (NT_SUCCESS(Status) && IoGetRelatedDeviceObject(*TargetFileObject) != IoGetRelatedDeviceObject(FileObject))
    {
        ObDereferenceObject(*TargetFileObject);
        ZwClose(*TargetHandle);
        Status = STATUS_NOT_SAME_DEVICE;
    }
    return Status;
}

NTSTATUS
FLTAPI
FltSetInformationFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    PFILE_RENAME_INFORMATION Rename = FileInformation;
    PFILE_OBJECT TargetFileObject = NULL;
    HANDLE TargetHandle = NULL;
    FLT_PARAMETERS Parameters;
    NTSTATUS Status;
    ULONG Index;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.SetFileInformation.Length = Length;
    Parameters.SetFileInformation.FileInformationClass = FileInformationClass;
    Parameters.SetFileInformation.InfoBuffer = FileInformation;

    if (FileInformationClass == FileRenameInformation ||
        FileInformationClass == FileLinkInformation ||
        FileInformationClass == FileRenameInformationEx ||
        FileInformationClass == FileLinkInformationEx)
    {
        BOOLEAN Qualified = (Rename->RootDirectory != NULL);

        if (Length < FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) ||
            Rename->FileNameLength > Length - FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName))
        {
            return STATUS_INVALID_PARAMETER;
        }

        for (Index = 0; !Qualified && Index < Rename->FileNameLength / sizeof(WCHAR); Index++)
        {
            if (Rename->FileName[Index] == L'\\')
            {
                Qualified = TRUE;
            }
        }

        if (Qualified)
        {
            Status = FltpOpenRenameTarget(Instance, FileObject, Rename, &TargetHandle, &TargetFileObject);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            Parameters.SetFileInformation.ParentOfTarget = TargetFileObject;
        }
        Parameters.SetFileInformation.ReplaceIfExists = Rename->ReplaceIfExists;
    }

    Status = FltpGeneratedIo(Instance, FileObject, IRP_MJ_SET_INFORMATION, 0, 0, 0, &Parameters, NULL);

    if (TargetFileObject != NULL)
    {
        ObDereferenceObject(TargetFileObject);
        ZwClose(TargetHandle);
    }
    return Status;
}

NTSTATUS
FLTAPI
FltQueryVolumeInformationFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_to_(Length, *LengthReturned) PVOID FsInformation,
    _In_ ULONG Length,
    _In_ FS_INFORMATION_CLASS FsInformationClass,
    _Out_opt_ PULONG LengthReturned)
{
    FLT_PARAMETERS Parameters;
    ULONG_PTR Information;
    NTSTATUS Status;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.QueryVolumeInformation.Length = Length;
    Parameters.QueryVolumeInformation.FsInformationClass = FsInformationClass;
    Parameters.QueryVolumeInformation.VolumeBuffer = FsInformation;

    Status = FltpGeneratedIo(Instance, FileObject, IRP_MJ_QUERY_VOLUME_INFORMATION, 0, 0, 0, &Parameters, &Information);
    if (LengthReturned != NULL)
    {
        *LengthReturned = (ULONG)Information;
    }
    return Status;
}

NTSTATUS
FLTAPI
FltQueryDirectoryFileEx(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ ULONG QueryFlags,
    _In_opt_ PUNICODE_STRING FileName,
    _Out_opt_ PULONG LengthReturned)
{
    FLT_PARAMETERS Parameters;
    ULONG_PTR Information;
    NTSTATUS Status;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.DirectoryControl.QueryDirectory.Length = Length;
    Parameters.DirectoryControl.QueryDirectory.FileName = FileName;
    Parameters.DirectoryControl.QueryDirectory.FileInformationClass = FileInformationClass;
    Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer = FileInformation;

    Status = FltpGeneratedIo(Instance,
                             FileObject,
                             IRP_MJ_DIRECTORY_CONTROL,
                             IRP_MN_QUERY_DIRECTORY,
                             0,
                             (UCHAR)QueryFlags,
                             &Parameters,
                             &Information);
    if (LengthReturned != NULL)
    {
        *LengthReturned = (ULONG)Information;
    }
    return Status;
}

NTSTATUS
FLTAPI
FltQueryDirectoryFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_opt_ PUNICODE_STRING FileName,
    _In_ BOOLEAN RestartScan,
    _Out_opt_ PULONG LengthReturned)
{
    ULONG QueryFlags = 0;

    if (ReturnSingleEntry)
    {
        QueryFlags |= SL_RETURN_SINGLE_ENTRY;
    }
    if (RestartScan)
    {
        QueryFlags |= SL_RESTART_SCAN;
    }

    return FltQueryDirectoryFileEx(Instance,
                                   FileObject,
                                   FileInformation,
                                   Length,
                                   FileInformationClass,
                                   QueryFlags,
                                   FileName,
                                   LengthReturned);
}

static
NTSTATUS
FltpControlFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ UCHAR MajorFunction,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_opt_ PULONG LengthReturned)
{
    FLT_PARAMETERS Parameters;
    PVOID SystemBuffer = NULL;
    ULONG_PTR Information = 0;
    PMDL Mdl = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Size;

    if (LengthReturned != NULL)
    {
        *LengthReturned = 0;
    }

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.DeviceIoControl.Common.OutputBufferLength = OutputBufferLength;
    Parameters.DeviceIoControl.Common.InputBufferLength = InputBufferLength;
    Parameters.DeviceIoControl.Common.IoControlCode = ControlCode;

    switch (ControlCode & 3)
    {
        case METHOD_BUFFERED:
            Size = max(InputBufferLength, OutputBufferLength);
            if (Size != 0)
            {
                SystemBuffer = ExAllocatePoolWithTag(NonPagedPoolNx, Size, FLT_TAG_GENERAL);
                if (SystemBuffer == NULL)
                {
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                if (InputBufferLength != 0 && InputBuffer != NULL)
                {
                    RtlCopyMemory(SystemBuffer, InputBuffer, InputBufferLength);
                }
            }
            Parameters.DeviceIoControl.Buffered.SystemBuffer = SystemBuffer;
            break;

        case METHOD_IN_DIRECT:
        case METHOD_OUT_DIRECT:
            Parameters.DeviceIoControl.Direct.InputSystemBuffer = InputBuffer;
            Parameters.DeviceIoControl.Direct.OutputBuffer = OutputBuffer;
            if (OutputBuffer != NULL && OutputBufferLength != 0)
            {
                Mdl = IoAllocateMdl(OutputBuffer, OutputBufferLength, FALSE, FALSE, NULL);
                if (Mdl == NULL)
                {
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                _SEH2_TRY
                {
                    MmProbeAndLockPages(Mdl,
                                        KernelMode,
                                        ((ControlCode & 3) == METHOD_IN_DIRECT) ? IoReadAccess : IoWriteAccess);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;

                if (!NT_SUCCESS(Status))
                {
                    IoFreeMdl(Mdl);
                    return Status;
                }
            }
            Parameters.DeviceIoControl.Direct.OutputMdlAddress = Mdl;
            break;

        default:
            Parameters.DeviceIoControl.Neither.InputBuffer = InputBuffer;
            Parameters.DeviceIoControl.Neither.OutputBuffer = OutputBuffer;
            break;
    }

    Status = FltpGeneratedIo(Instance,
                             FileObject,
                             MajorFunction,
                             (MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL) ? IRP_MN_USER_FS_REQUEST : 0,
                             0,
                             0,
                             &Parameters,
                             &Information);

    if (SystemBuffer != NULL)
    {
        if (!NT_ERROR(Status) && OutputBuffer != NULL)
        {
            RtlCopyMemory(OutputBuffer, SystemBuffer, min(Information, (ULONG_PTR)OutputBufferLength));
        }
        ExFreePoolWithTag(SystemBuffer, FLT_TAG_GENERAL);
    }
    if (Mdl != NULL)
    {
        FltpFreeMdl(Mdl);
    }
    if (LengthReturned != NULL)
    {
        *LengthReturned = (ULONG)Information;
    }
    return Status;
}

NTSTATUS
FLTAPI
FltFsControlFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG FsControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *LengthReturned) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_opt_ PULONG LengthReturned)
{
    return FltpControlFile(Instance,
                           FileObject,
                           IRP_MJ_FILE_SYSTEM_CONTROL,
                           FsControlCode,
                           InputBuffer,
                           InputBufferLength,
                           OutputBuffer,
                           OutputBufferLength,
                           LengthReturned);
}

NTSTATUS
FLTAPI
FltDeviceIoControlFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *LengthReturned) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_opt_ PULONG LengthReturned)
{
    return FltpControlFile(Instance,
                           FileObject,
                           IRP_MJ_DEVICE_CONTROL,
                           IoControlCode,
                           InputBuffer,
                           InputBufferLength,
                           OutputBuffer,
                           OutputBufferLength,
                           LengthReturned);
}

NTSTATUS
FLTAPI
FltFlushBuffers(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject)
{
    FLT_PARAMETERS Parameters;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    return FltpGeneratedIo(Instance, FileObject, IRP_MJ_FLUSH_BUFFERS, 0, 0, 0, &Parameters, NULL);
}

NTSTATUS
FLTAPI
FltQuerySecurityObject(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Inout_updates_bytes_opt_(Length) PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ULONG Length,
    _Out_opt_ PULONG LengthNeeded)
{
    FLT_PARAMETERS Parameters;
    ULONG_PTR Information;
    NTSTATUS Status;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.QuerySecurity.SecurityInformation = SecurityInformation;
    Parameters.QuerySecurity.Length = Length;
    Parameters.QuerySecurity.SecurityBuffer = SecurityDescriptor;

    Status = FltpGeneratedIo(Instance, FileObject, IRP_MJ_QUERY_SECURITY, 0, 0, 0, &Parameters, &Information);
    if (LengthNeeded != NULL)
    {
        *LengthNeeded = (ULONG)Information;
    }
    return Status;
}

NTSTATUS
FLTAPI
FltSetSecurityObject(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ SECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    FLT_PARAMETERS Parameters;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.SetSecurity.SecurityInformation = SecurityInformation;
    Parameters.SetSecurity.SecurityDescriptor = SecurityDescriptor;

    return FltpGeneratedIo(Instance, FileObject, IRP_MJ_SET_SECURITY, 0, 0, 0, &Parameters, NULL);
}

NTSTATUS
FLTAPI
FltQueryEaFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_to_(Length, *LengthReturned) PVOID ReturnedEaData,
    _In_ ULONG Length,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_reads_bytes_opt_(EaListLength) PVOID EaList,
    _In_ ULONG EaListLength,
    _In_opt_ PULONG EaIndex,
    _In_ BOOLEAN RestartScan,
    _Out_opt_ PULONG LengthReturned)
{
    FLT_PARAMETERS Parameters;
    ULONG_PTR Information;
    UCHAR OperationFlags = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.QueryEa.Length = Length;
    Parameters.QueryEa.EaList = EaList;
    Parameters.QueryEa.EaListLength = EaListLength;
    Parameters.QueryEa.EaBuffer = ReturnedEaData;
    if (EaIndex != NULL)
    {
        Parameters.QueryEa.EaIndex = *EaIndex;
        OperationFlags |= SL_INDEX_SPECIFIED;
    }
    if (ReturnSingleEntry)
    {
        OperationFlags |= SL_RETURN_SINGLE_ENTRY;
    }
    if (RestartScan)
    {
        OperationFlags |= SL_RESTART_SCAN;
    }

    Status = FltpGeneratedIo(Instance, FileObject, IRP_MJ_QUERY_EA, 0, 0, OperationFlags, &Parameters, &Information);
    if (LengthReturned != NULL)
    {
        *LengthReturned = (ULONG)Information;
    }
    return Status;
}

NTSTATUS
FLTAPI
FltSetEaFile(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) PVOID EaBuffer,
    _In_ ULONG Length)
{
    FLT_PARAMETERS Parameters;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.SetEa.Length = Length;
    Parameters.SetEa.EaBuffer = EaBuffer;

    return FltpGeneratedIo(Instance, FileObject, IRP_MJ_SET_EA, 0, 0, 0, &Parameters, NULL);
}

NTSTATUS
FLTAPI
FltIsDirectory(
    _In_ PFILE_OBJECT FileObject,
    _In_ PFLT_INSTANCE Instance,
    _Out_ PBOOLEAN IsDirectory)
{
    FILE_STANDARD_INFORMATION Standard;
    NTSTATUS Status;

    *IsDirectory = FALSE;

    Status = FltQueryInformationFile(Instance, FileObject, &Standard, sizeof(Standard), FileStandardInformation, NULL);
    if (NT_SUCCESS(Status))
    {
        *IsDirectory = Standard.Directory;
    }
    return Status;
}

VOID
FLTAPI
FltCancelFileOpen(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject)
{
    FLT_PARAMETERS Parameters;
    PAGED_CODE();

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    (VOID)FltpGeneratedIo(Instance, FileObject, IRP_MJ_CLEANUP, 0, IRP_CLOSE_OPERATION, 0, &Parameters, NULL);
    FileObject->Flags |= FO_FILE_OPEN_CANCELLED;
}

NTSTATUS
FLTAPI
FltCreateSystemVolumeInformationFolder(
    _In_ PFLT_INSTANCE Instance)
{
    PFLT_VOLUME Volume = Instance->Volume;
    UNICODE_STRING Root;
    NTSTATUS Status;
    PAGED_CODE();

    Root.Length = 0;
    Root.MaximumLength = Volume->DeviceName.Length + 2 * sizeof(WCHAR);
    Root.Buffer = ExAllocatePoolWithTag(PagedPool, Root.MaximumLength, FLT_TAG_NAME);
    if (Root.Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyUnicodeString(&Root, &Volume->DeviceName);
    RtlAppendUnicodeToString(&Root, L"\\");

    Status = RtlCreateSystemVolumeInformationFolder(&Root);
    ExFreePoolWithTag(Root.Buffer, FLT_TAG_NAME);
    return Status;
}
