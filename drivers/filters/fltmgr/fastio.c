/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Fast I/O and file system filter callbacks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

#define FLTP_FAST_IO_ROUTINE(Dispatch, Field) \
    ((Dispatch) != NULL && \
     (Dispatch)->SizeOfFastIoDispatch >= RTL_SIZEOF_THROUGH_FIELD(FAST_IO_DISPATCH, Field) && \
     (Dispatch)->Field != NULL)

static
PFLTP_IRP_CTRL
FltpBeginOperation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PFILE_OBJECT FileObject,
    _In_ UCHAR MajorFunction,
    _In_ UCHAR MinorFunction,
    _In_ ULONG DataFlags)
{
    PFLTP_DEVICE_EXTENSION Extension = DeviceObject->DeviceExtension;
    PFLTP_IRP_CTRL IrpCtrl;

    if (!FltpIsVolumeDevice(DeviceObject) || Extension->Volume == NULL)
    {
        return NULL;
    }

    if (FltpCollectNodes(Extension->Volume, NULL, MajorFunction, MAXULONG, FileObject, &IrpCtrl) == 0)
    {
        return NULL;
    }

    IrpCtrl->DeviceObject = DeviceObject;
    IrpCtrl->Flags |= (DataFlags == FLTFL_CALLBACK_DATA_FAST_IO_OPERATION) ? FLTP_IRPCTRL_FAST_IO : FLTP_IRPCTRL_FS_FILTER;
    IrpCtrl->Data.Flags = DataFlags;
    *(PETHREAD *)&IrpCtrl->Data.Thread = PsGetCurrentThread();
    IrpCtrl->Data.RequestorMode = ExGetPreviousMode();
    IrpCtrl->Iopb.MajorFunction = MajorFunction;
    IrpCtrl->Iopb.MinorFunction = MinorFunction;
    IrpCtrl->Iopb.TargetFileObject = FileObject;
    return IrpCtrl;
}

static
VOID
FltpEndOperation(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    FltpBeginPostPhase(IrpCtrl);
    (VOID)FltpRunPostCallbacks(IrpCtrl, 0);
    FltpReleaseNodes(IrpCtrl);
    FltpDereferenceIrpCtrl(IrpCtrl);
}

static
BOOLEAN
FltpFastIoPre(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _Out_opt_ PIO_STATUS_BLOCK IoStatus,
    _Out_ PBOOLEAN Result)
{
    FLT_PREOP_CALLBACK_STATUS Status = FltpRunPreCallbacks(IrpCtrl);
    PFLTP_COMPLETION_NODE Node;

    if (Status == FLT_PREOP_SUCCESS_NO_CALLBACK)
    {
        return TRUE;
    }

    Node = &IrpCtrl->Nodes[IrpCtrl->NextNode];
    if (InterlockedExchange(&Node->State, FLTP_NODE_DONE) != FLTP_NODE_DONE)
    {
        ExReleaseRundownProtection(&Node->Instance->Base.RundownRef);
    }

    if (Status == FLT_PREOP_COMPLETE)
    {
        *Result = TRUE;
        if (IoStatus != NULL)
        {
            *IoStatus = IrpCtrl->Data.IoStatus;
        }
    }
    else
    {
        *Result = FALSE;
        IrpCtrl->Data.IoStatus.Status = STATUS_FLT_DISALLOW_FAST_IO;
        IrpCtrl->Data.IoStatus.Information = 0;
    }

    FltpEndOperation(IrpCtrl);
    return FALSE;
}

static
VOID
FltpFastIoPost(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _Inout_opt_ PIO_STATUS_BLOCK IoStatus,
    _In_ BOOLEAN Result)
{
    if (Result && IoStatus != NULL)
    {
        IrpCtrl->Data.IoStatus = *IoStatus;
    }
    else if (!Result)
    {
        IrpCtrl->Data.IoStatus.Status = STATUS_FLT_DISALLOW_FAST_IO;
        IrpCtrl->Data.IoStatus.Information = 0;
    }
    else
    {
        IrpCtrl->Data.IoStatus.Status = STATUS_SUCCESS;
        IrpCtrl->Data.IoStatus.Information = 0;
    }

    FltpBeginPostPhase(IrpCtrl);
    (VOID)FltpRunPostCallbacks(IrpCtrl, 0);

    if (Result && IoStatus != NULL)
    {
        *IoStatus = IrpCtrl->Data.IoStatus;
    }

    FltpReleaseNodes(IrpCtrl);
    FltpDereferenceIrpCtrl(IrpCtrl);
}

static
PFAST_IO_DISPATCH
FltpLowerFastIo(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDEVICE_OBJECT *Lower)
{
    PFLTP_DEVICE_EXTENSION Extension = DeviceObject->DeviceExtension;

    *Lower = NULL;
    if (Extension == NULL || Extension->Signature != FLTP_DEVICE_EXTENSION_SIGNATURE)
    {
        return NULL;
    }

    *Lower = Extension->AttachedToDeviceObject;
    return (*Lower)->DriverObject->FastIoDispatch;
}

static
BOOLEAN
NTAPI
FltpFastIoCheckIfPossible(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _In_ BOOLEAN CheckForReadOperation,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.FastIoCheckIfPossible.FileOffset = *FileOffset;
        IrpCtrl->Iopb.Parameters.FastIoCheckIfPossible.Length = Length;
        IrpCtrl->Iopb.Parameters.FastIoCheckIfPossible.LockKey = LockKey;
        IrpCtrl->Iopb.Parameters.FastIoCheckIfPossible.CheckForReadOperation = CheckForReadOperation;
        if (!FltpFastIoPre(IrpCtrl, IoStatus, &Result))
        {
            return Result;
        }
    }

    if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoCheckIfPossible))
    {
        Result = Dispatch->FastIoCheckIfPossible(FileObject, FileOffset, Length, Wait, LockKey, CheckForReadOperation, IoStatus, Lower);
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, IoStatus, Result);
    }
    return Result;
}

static
BOOLEAN
NTAPI
FltpFastIoRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    LARGE_INTEGER Offset = *FileOffset;
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_READ, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.Read.Length = Length;
        IrpCtrl->Iopb.Parameters.Read.Key = LockKey;
        IrpCtrl->Iopb.Parameters.Read.ByteOffset = Offset;
        IrpCtrl->Iopb.Parameters.Read.ReadBuffer = Buffer;
        if (!FltpFastIoPre(IrpCtrl, IoStatus, &Result))
        {
            return Result;
        }
        Length = IrpCtrl->Iopb.Parameters.Read.Length;
        LockKey = IrpCtrl->Iopb.Parameters.Read.Key;
        Offset = IrpCtrl->Iopb.Parameters.Read.ByteOffset;
        Buffer = IrpCtrl->Iopb.Parameters.Read.ReadBuffer;
        FileObject = IrpCtrl->Iopb.TargetFileObject;
    }

    if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoRead))
    {
        Result = Dispatch->FastIoRead(FileObject, &Offset, Length, Wait, LockKey, Buffer, IoStatus, Lower);
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, IoStatus, Result);
    }
    return Result;
}

static
BOOLEAN
NTAPI
FltpFastIoWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _In_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    LARGE_INTEGER Offset = *FileOffset;
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_WRITE, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.Write.Length = Length;
        IrpCtrl->Iopb.Parameters.Write.Key = LockKey;
        IrpCtrl->Iopb.Parameters.Write.ByteOffset = Offset;
        IrpCtrl->Iopb.Parameters.Write.WriteBuffer = Buffer;
        if (!FltpFastIoPre(IrpCtrl, IoStatus, &Result))
        {
            return Result;
        }
        Length = IrpCtrl->Iopb.Parameters.Write.Length;
        LockKey = IrpCtrl->Iopb.Parameters.Write.Key;
        Offset = IrpCtrl->Iopb.Parameters.Write.ByteOffset;
        Buffer = IrpCtrl->Iopb.Parameters.Write.WriteBuffer;
        FileObject = IrpCtrl->Iopb.TargetFileObject;
    }

    if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoWrite))
    {
        Result = Dispatch->FastIoWrite(FileObject, &Offset, Length, Wait, LockKey, Buffer, IoStatus, Lower);
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, IoStatus, Result);
    }
    return Result;
}

static
BOOLEAN
FltpFastIoQueryInformation(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _In_ FILE_INFORMATION_CLASS InformationClass,
    _In_ ULONG Length,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_QUERY_INFORMATION, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.QueryFileInformation.Length = Length;
        IrpCtrl->Iopb.Parameters.QueryFileInformation.FileInformationClass = InformationClass;
        IrpCtrl->Iopb.Parameters.QueryFileInformation.InfoBuffer = Buffer;
        if (!FltpFastIoPre(IrpCtrl, IoStatus, &Result))
        {
            return Result;
        }
        Buffer = IrpCtrl->Iopb.Parameters.QueryFileInformation.InfoBuffer;
        FileObject = IrpCtrl->Iopb.TargetFileObject;
    }

    switch (InformationClass)
    {
        case FileBasicInformation:
            if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoQueryBasicInfo))
            {
                Result = Dispatch->FastIoQueryBasicInfo(FileObject, Wait, Buffer, IoStatus, Lower);
            }
            break;

        case FileStandardInformation:
            if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoQueryStandardInfo))
            {
                Result = Dispatch->FastIoQueryStandardInfo(FileObject, Wait, Buffer, IoStatus, Lower);
            }
            break;

        case FileNetworkOpenInformation:
            if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoQueryNetworkOpenInfo))
            {
                Result = Dispatch->FastIoQueryNetworkOpenInfo(FileObject, Wait, Buffer, IoStatus, Lower);
            }
            break;

        default:
            break;
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, IoStatus, Result);
    }
    return Result;
}

static
BOOLEAN
NTAPI
FltpFastIoQueryBasicInfo(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _Out_ PFILE_BASIC_INFORMATION Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return FltpFastIoQueryInformation(FileObject, Wait, FileBasicInformation, sizeof(*Buffer), Buffer, IoStatus, DeviceObject);
}

static
BOOLEAN
NTAPI
FltpFastIoQueryStandardInfo(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _Out_ PFILE_STANDARD_INFORMATION Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return FltpFastIoQueryInformation(FileObject, Wait, FileStandardInformation, sizeof(*Buffer), Buffer, IoStatus, DeviceObject);
}

static
BOOLEAN
NTAPI
FltpFastIoQueryNetworkOpenInfo(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _Out_ PFILE_NETWORK_OPEN_INFORMATION Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return FltpFastIoQueryInformation(FileObject, Wait, FileNetworkOpenInformation, sizeof(*Buffer), Buffer, IoStatus, DeviceObject);
}

static
BOOLEAN
FltpFastIoLockControl(
    _In_ UCHAR MinorFunction,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER FileOffset,
    _In_opt_ PLARGE_INTEGER Length,
    _In_ PEPROCESS ProcessId,
    _In_ ULONG Key,
    _In_ BOOLEAN FailImmediately,
    _In_ BOOLEAN ExclusiveLock,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_LOCK_CONTROL, MinorFunction, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.LockControl.Length = Length;
        IrpCtrl->Iopb.Parameters.LockControl.Key = Key;
        if (FileOffset != NULL)
        {
            IrpCtrl->Iopb.Parameters.LockControl.ByteOffset = *FileOffset;
        }
        IrpCtrl->Iopb.Parameters.LockControl.ProcessId = ProcessId;
        IrpCtrl->Iopb.Parameters.LockControl.FailImmediately = FailImmediately;
        IrpCtrl->Iopb.Parameters.LockControl.ExclusiveLock = ExclusiveLock;
        if (!FltpFastIoPre(IrpCtrl, IoStatus, &Result))
        {
            return Result;
        }
    }

    switch (MinorFunction)
    {
        case IRP_MN_LOCK:
            if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoLock))
            {
                Result = Dispatch->FastIoLock(FileObject, FileOffset, Length, ProcessId, Key, FailImmediately, ExclusiveLock, IoStatus, Lower);
            }
            break;

        case IRP_MN_UNLOCK_SINGLE:
            if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoUnlockSingle))
            {
                Result = Dispatch->FastIoUnlockSingle(FileObject, FileOffset, Length, ProcessId, Key, IoStatus, Lower);
            }
            break;

        case IRP_MN_UNLOCK_ALL:
            if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoUnlockAll))
            {
                Result = Dispatch->FastIoUnlockAll(FileObject, ProcessId, IoStatus, Lower);
            }
            break;

        case IRP_MN_UNLOCK_ALL_BY_KEY:
            if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoUnlockAllByKey))
            {
                Result = Dispatch->FastIoUnlockAllByKey(FileObject, ProcessId, Key, IoStatus, Lower);
            }
            break;

        default:
            break;
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, IoStatus, Result);
    }
    return Result;
}

static
BOOLEAN
NTAPI
FltpFastIoLock(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ PEPROCESS ProcessId,
    _In_ ULONG Key,
    _In_ BOOLEAN FailImmediately,
    _In_ BOOLEAN ExclusiveLock,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return FltpFastIoLockControl(IRP_MN_LOCK, FileObject, FileOffset, Length, ProcessId, Key, FailImmediately, ExclusiveLock, IoStatus, DeviceObject);
}

static
BOOLEAN
NTAPI
FltpFastIoUnlockSingle(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ PEPROCESS ProcessId,
    _In_ ULONG Key,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return FltpFastIoLockControl(IRP_MN_UNLOCK_SINGLE, FileObject, FileOffset, Length, ProcessId, Key, FALSE, FALSE, IoStatus, DeviceObject);
}

static
BOOLEAN
NTAPI
FltpFastIoUnlockAll(
    _In_ PFILE_OBJECT FileObject,
    _In_ PEPROCESS ProcessId,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return FltpFastIoLockControl(IRP_MN_UNLOCK_ALL, FileObject, NULL, NULL, ProcessId, 0, FALSE, FALSE, IoStatus, DeviceObject);
}

static
BOOLEAN
NTAPI
FltpFastIoUnlockAllByKey(
    _In_ PFILE_OBJECT FileObject,
    _In_ PVOID ProcessId,
    _In_ ULONG Key,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return FltpFastIoLockControl(IRP_MN_UNLOCK_ALL_BY_KEY, FileObject, NULL, NULL, ProcessId, Key, FALSE, FALSE, IoStatus, DeviceObject);
}

static
BOOLEAN
NTAPI
FltpFastIoDeviceControl(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Wait,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ ULONG IoControlCode,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    if (DeviceObject == FltGlobals.ControlDevice || DeviceObject == FltGlobals.MessageDevice)
    {
        return FALSE;
    }

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_DEVICE_CONTROL, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.DeviceIoControl.FastIo.OutputBufferLength = OutputBufferLength;
        IrpCtrl->Iopb.Parameters.DeviceIoControl.FastIo.InputBufferLength = InputBufferLength;
        IrpCtrl->Iopb.Parameters.DeviceIoControl.FastIo.IoControlCode = IoControlCode;
        IrpCtrl->Iopb.Parameters.DeviceIoControl.FastIo.InputBuffer = InputBuffer;
        IrpCtrl->Iopb.Parameters.DeviceIoControl.FastIo.OutputBuffer = OutputBuffer;
        if (!FltpFastIoPre(IrpCtrl, IoStatus, &Result))
        {
            return Result;
        }
    }

    if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoDeviceControl))
    {
        Result = Dispatch->FastIoDeviceControl(FileObject, Wait, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength, IoControlCode, IoStatus, Lower);
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, IoStatus, Result);
    }
    return Result;
}

static
VOID
NTAPI
FltpFastIoDetachDevice(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    FltpDetachDevice(SourceDevice, TargetDevice);
}

static
BOOLEAN
NTAPI
FltpFastIoMdlRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_MDL_READ, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.MdlRead.FileOffset = *FileOffset;
        IrpCtrl->Iopb.Parameters.MdlRead.Length = Length;
        IrpCtrl->Iopb.Parameters.MdlRead.Key = LockKey;
        IrpCtrl->Iopb.Parameters.MdlRead.MdlChain = MdlChain;
        if (!FltpFastIoPre(IrpCtrl, IoStatus, &Result))
        {
            return Result;
        }
    }

    if (FLTP_FAST_IO_ROUTINE(Dispatch, MdlRead))
    {
        Result = Dispatch->MdlRead(FileObject, FileOffset, Length, LockKey, MdlChain, IoStatus, Lower);
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, IoStatus, Result);
    }
    return Result;
}

static
BOOLEAN
NTAPI
FltpFastIoMdlReadComplete(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL MdlChain,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_MDL_READ_COMPLETE, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.MdlReadComplete.MdlChain = MdlChain;
        if (!FltpFastIoPre(IrpCtrl, NULL, &Result))
        {
            return Result;
        }
    }

    if (FLTP_FAST_IO_ROUTINE(Dispatch, MdlReadComplete))
    {
        Result = Dispatch->MdlReadComplete(FileObject, MdlChain, Lower);
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, NULL, Result);
    }
    return Result;
}

static
BOOLEAN
NTAPI
FltpFastIoPrepareMdlWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_PREPARE_MDL_WRITE, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.PrepareMdlWrite.FileOffset = *FileOffset;
        IrpCtrl->Iopb.Parameters.PrepareMdlWrite.Length = Length;
        IrpCtrl->Iopb.Parameters.PrepareMdlWrite.Key = LockKey;
        IrpCtrl->Iopb.Parameters.PrepareMdlWrite.MdlChain = MdlChain;
        if (!FltpFastIoPre(IrpCtrl, IoStatus, &Result))
        {
            return Result;
        }
    }

    if (FLTP_FAST_IO_ROUTINE(Dispatch, PrepareMdlWrite))
    {
        Result = Dispatch->PrepareMdlWrite(FileObject, FileOffset, Length, LockKey, MdlChain, IoStatus, Lower);
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, IoStatus, Result);
    }
    return Result;
}

static
BOOLEAN
NTAPI
FltpFastIoMdlWriteComplete(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PMDL MdlChain,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, FileObject, IRP_MJ_MDL_WRITE_COMPLETE, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.MdlWriteComplete.FileOffset = *FileOffset;
        IrpCtrl->Iopb.Parameters.MdlWriteComplete.MdlChain = MdlChain;
        if (!FltpFastIoPre(IrpCtrl, NULL, &Result))
        {
            return Result;
        }
    }

    if (FLTP_FAST_IO_ROUTINE(Dispatch, MdlWriteComplete))
    {
        Result = Dispatch->MdlWriteComplete(FileObject, FileOffset, MdlChain, Lower);
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, NULL, Result);
    }
    return Result;
}

static
BOOLEAN
NTAPI
FltpFastIoReadCompressed(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _Out_ PVOID Buffer,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _Out_ PCOMPRESSED_DATA_INFO CompressedDataInfo,
    _In_ ULONG CompressedDataInfoLength,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);

    if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoReadCompressed))
    {
        return Dispatch->FastIoReadCompressed(FileObject, FileOffset, Length, LockKey, Buffer, MdlChain, IoStatus, CompressedDataInfo, CompressedDataInfoLength, Lower);
    }
    return FALSE;
}

static
BOOLEAN
NTAPI
FltpFastIoWriteCompressed(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG LockKey,
    _In_ PVOID Buffer,
    _Out_ PMDL *MdlChain,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PCOMPRESSED_DATA_INFO CompressedDataInfo,
    _In_ ULONG CompressedDataInfoLength,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);

    if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoWriteCompressed))
    {
        return Dispatch->FastIoWriteCompressed(FileObject, FileOffset, Length, LockKey, Buffer, MdlChain, IoStatus, CompressedDataInfo, CompressedDataInfoLength, Lower);
    }
    return FALSE;
}

static
BOOLEAN
NTAPI
FltpFastIoMdlReadCompleteCompressed(
    _In_ PFILE_OBJECT FileObject,
    _In_ PMDL MdlChain,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);

    if (FLTP_FAST_IO_ROUTINE(Dispatch, MdlReadCompleteCompressed))
    {
        return Dispatch->MdlReadCompleteCompressed(FileObject, MdlChain, Lower);
    }
    return FALSE;
}

static
BOOLEAN
NTAPI
FltpFastIoMdlWriteCompleteCompressed(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ PMDL MdlChain,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);

    if (FLTP_FAST_IO_ROUTINE(Dispatch, MdlWriteCompleteCompressed))
    {
        return Dispatch->MdlWriteCompleteCompressed(FileObject, FileOffset, MdlChain, Lower);
    }
    return FALSE;
}

static
BOOLEAN
NTAPI
FltpFastIoQueryOpen(
    _Inout_ PIRP Irp,
    _Out_ PFILE_NETWORK_OPEN_INFORMATION NetworkInformation,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT Lower;
    PFAST_IO_DISPATCH Dispatch = FltpLowerFastIo(DeviceObject, &Lower);
    PFLTP_IRP_CTRL IrpCtrl;
    BOOLEAN Result = FALSE;

    IrpCtrl = FltpBeginOperation(DeviceObject, Stack->FileObject, IRP_MJ_NETWORK_QUERY_OPEN, 0, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION);
    if (IrpCtrl != NULL)
    {
        IrpCtrl->Iopb.Parameters.NetworkQueryOpen.Irp = Irp;
        IrpCtrl->Iopb.Parameters.NetworkQueryOpen.NetworkInformation = NetworkInformation;
        if (!FltpFastIoPre(IrpCtrl, NULL, &Result))
        {
            return Result;
        }
    }

    if (FLTP_FAST_IO_ROUTINE(Dispatch, FastIoQueryOpen))
    {
        Stack->DeviceObject = Lower;
        Result = Dispatch->FastIoQueryOpen(Irp, NetworkInformation, Lower);
        Stack->DeviceObject = DeviceObject;
    }

    if (IrpCtrl != NULL)
    {
        FltpFastIoPost(IrpCtrl, NULL, Result);
    }
    return Result;
}

VOID
FltpInitializeFastIo(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PFAST_IO_DISPATCH Dispatch = &FltGlobals.FastIoDispatch;

    RtlZeroMemory(Dispatch, sizeof(*Dispatch));
    Dispatch->SizeOfFastIoDispatch = sizeof(FAST_IO_DISPATCH);
    Dispatch->FastIoCheckIfPossible = FltpFastIoCheckIfPossible;
    Dispatch->FastIoRead = FltpFastIoRead;
    Dispatch->FastIoWrite = FltpFastIoWrite;
    Dispatch->FastIoQueryBasicInfo = FltpFastIoQueryBasicInfo;
    Dispatch->FastIoQueryStandardInfo = FltpFastIoQueryStandardInfo;
    Dispatch->FastIoLock = FltpFastIoLock;
    Dispatch->FastIoUnlockSingle = FltpFastIoUnlockSingle;
    Dispatch->FastIoUnlockAll = FltpFastIoUnlockAll;
    Dispatch->FastIoUnlockAllByKey = FltpFastIoUnlockAllByKey;
    Dispatch->FastIoDeviceControl = FltpFastIoDeviceControl;
    Dispatch->FastIoDetachDevice = FltpFastIoDetachDevice;
    Dispatch->FastIoQueryNetworkOpenInfo = FltpFastIoQueryNetworkOpenInfo;
    Dispatch->MdlRead = FltpFastIoMdlRead;
    Dispatch->MdlReadComplete = FltpFastIoMdlReadComplete;
    Dispatch->PrepareMdlWrite = FltpFastIoPrepareMdlWrite;
    Dispatch->MdlWriteComplete = FltpFastIoMdlWriteComplete;
    Dispatch->FastIoReadCompressed = FltpFastIoReadCompressed;
    Dispatch->FastIoWriteCompressed = FltpFastIoWriteCompressed;
    Dispatch->MdlReadCompleteCompressed = FltpFastIoMdlReadCompleteCompressed;
    Dispatch->MdlWriteCompleteCompressed = FltpFastIoMdlWriteCompleteCompressed;
    Dispatch->FastIoQueryOpen = FltpFastIoQueryOpen;
    DriverObject->FastIoDispatch = Dispatch;
}

static
NTSTATUS
NTAPI
FltpPreFsFilterOperation(
    _In_ PFS_FILTER_CALLBACK_DATA Data,
    _Out_ PVOID *CompletionContext)
{
    PFLTP_IRP_CTRL IrpCtrl;
    FLT_PREOP_CALLBACK_STATUS Status;
    PFLTP_COMPLETION_NODE Node;
    NTSTATUS Result;

    *CompletionContext = NULL;

    IrpCtrl = FltpBeginOperation(Data->DeviceObject, Data->FileObject, Data->Operation, 0, FLTFL_CALLBACK_DATA_FS_FILTER_OPERATION);
    if (IrpCtrl == NULL)
    {
        return STATUS_SUCCESS;
    }

    switch (Data->Operation)
    {
        case FS_FILTER_ACQUIRE_FOR_SECTION_SYNCHRONIZATION:
            IrpCtrl->Iopb.Parameters.AcquireForSectionSynchronization.SyncType =
                Data->Parameters.AcquireForSectionSynchronization.SyncType;
            IrpCtrl->Iopb.Parameters.AcquireForSectionSynchronization.PageProtection =
                Data->Parameters.AcquireForSectionSynchronization.PageProtection;
            IrpCtrl->Iopb.Parameters.AcquireForSectionSynchronization.OutputInformation =
                Data->Parameters.AcquireForSectionSynchronization.OutputInformation;
            break;

        case FS_FILTER_ACQUIRE_FOR_MOD_WRITE:
            IrpCtrl->Iopb.Parameters.AcquireForModifiedPageWriter.EndingOffset =
                Data->Parameters.AcquireForModifiedPageWriter.EndingOffset;
            IrpCtrl->Iopb.Parameters.AcquireForModifiedPageWriter.ResourceToRelease =
                Data->Parameters.AcquireForModifiedPageWriter.ResourceToRelease;
            break;

        case FS_FILTER_RELEASE_FOR_MOD_WRITE:
            IrpCtrl->Iopb.Parameters.ReleaseForModifiedPageWriter.ResourceToRelease =
                Data->Parameters.ReleaseForModifiedPageWriter.ResourceToRelease;
            break;

        default:
            break;
    }

    Status = FltpRunPreCallbacks(IrpCtrl);
    if (Status == FLT_PREOP_SUCCESS_NO_CALLBACK)
    {
        *CompletionContext = IrpCtrl;
        return STATUS_SUCCESS;
    }

    Node = &IrpCtrl->Nodes[IrpCtrl->NextNode];
    if (InterlockedExchange(&Node->State, FLTP_NODE_DONE) != FLTP_NODE_DONE)
    {
        ExReleaseRundownProtection(&Node->Instance->Base.RundownRef);
    }

    if (Status == FLT_PREOP_COMPLETE)
    {
        Result = IrpCtrl->Data.IoStatus.Status;
        if (Result == STATUS_SUCCESS)
        {
            Result = STATUS_FSFILTER_OP_COMPLETED_SUCCESSFULLY;
        }
    }
    else
    {
        Result = STATUS_FLT_DISALLOW_FSFILTER_IO;
        IrpCtrl->Data.IoStatus.Status = Result;
    }

    FltpEndOperation(IrpCtrl);
    return Result;
}

static
VOID
NTAPI
FltpPostFsFilterOperation(
    _In_ PFS_FILTER_CALLBACK_DATA Data,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID CompletionContext)
{
    PFLTP_IRP_CTRL IrpCtrl = CompletionContext;

    UNREFERENCED_PARAMETER(Data);

    if (IrpCtrl == NULL)
    {
        return;
    }

    IrpCtrl->Data.IoStatus.Status = OperationStatus;
    IrpCtrl->Data.IoStatus.Information = 0;
    FltpEndOperation(IrpCtrl);
}

NTSTATUS
FltpRegisterFsFilterCallbacks(
    _In_ PDRIVER_OBJECT DriverObject)
{
    FS_FILTER_CALLBACKS Callbacks;

    RtlZeroMemory(&Callbacks, sizeof(Callbacks));
    Callbacks.SizeOfFsFilterCallbacks = sizeof(Callbacks);
    Callbacks.PreAcquireForSectionSynchronization = FltpPreFsFilterOperation;
    Callbacks.PostAcquireForSectionSynchronization = FltpPostFsFilterOperation;
    Callbacks.PreReleaseForSectionSynchronization = FltpPreFsFilterOperation;
    Callbacks.PostReleaseForSectionSynchronization = FltpPostFsFilterOperation;
    Callbacks.PreAcquireForCcFlush = FltpPreFsFilterOperation;
    Callbacks.PostAcquireForCcFlush = FltpPostFsFilterOperation;
    Callbacks.PreReleaseForCcFlush = FltpPreFsFilterOperation;
    Callbacks.PostReleaseForCcFlush = FltpPostFsFilterOperation;
    Callbacks.PreAcquireForModifiedPageWriter = FltpPreFsFilterOperation;
    Callbacks.PostAcquireForModifiedPageWriter = FltpPostFsFilterOperation;
    Callbacks.PreReleaseForModifiedPageWriter = FltpPreFsFilterOperation;
    Callbacks.PostReleaseForModifiedPageWriter = FltpPostFsFilterOperation;

    return FsRtlRegisterFileSystemFilterCallbacks(DriverObject, &Callbacks);
}
