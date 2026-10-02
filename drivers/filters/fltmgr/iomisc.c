/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Callback data parameters and helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

#define FLTP_METHOD(Code) ((Code) & 3)

NTSTATUS
FltpDecodeParameters(
    _In_ PFLT_IO_PARAMETER_BLOCK Iopb,
    _In_ FLT_PARAMETERS *Parameters,
    _Out_ PMDL **MdlAddressPointer,
    _Out_ PVOID **Buffer,
    _Out_ PULONG *Length,
    _Out_opt_ LOCK_OPERATION *DesiredAccess)
{
    LOCK_OPERATION Access = IoReadAccess;
    PMDL *Mdl = NULL;
    PVOID *Buf = NULL;
    PULONG Len = NULL;

    switch (Iopb->MajorFunction)
    {
        case IRP_MJ_CREATE:
            Buf = &Parameters->Create.EaBuffer;
            Len = &Parameters->Create.EaLength;
            break;

        case IRP_MJ_READ:
            Mdl = &Parameters->Read.MdlAddress;
            Buf = &Parameters->Read.ReadBuffer;
            Len = &Parameters->Read.Length;
            Access = IoWriteAccess;
            break;

        case IRP_MJ_WRITE:
            Mdl = &Parameters->Write.MdlAddress;
            Buf = &Parameters->Write.WriteBuffer;
            Len = &Parameters->Write.Length;
            break;

        case IRP_MJ_QUERY_INFORMATION:
            Buf = &Parameters->QueryFileInformation.InfoBuffer;
            Len = &Parameters->QueryFileInformation.Length;
            Access = IoWriteAccess;
            break;

        case IRP_MJ_SET_INFORMATION:
            Buf = &Parameters->SetFileInformation.InfoBuffer;
            Len = &Parameters->SetFileInformation.Length;
            break;

        case IRP_MJ_QUERY_EA:
            Mdl = &Parameters->QueryEa.MdlAddress;
            Buf = &Parameters->QueryEa.EaBuffer;
            Len = &Parameters->QueryEa.Length;
            Access = IoWriteAccess;
            break;

        case IRP_MJ_SET_EA:
            Mdl = &Parameters->SetEa.MdlAddress;
            Buf = &Parameters->SetEa.EaBuffer;
            Len = &Parameters->SetEa.Length;
            break;

        case IRP_MJ_QUERY_VOLUME_INFORMATION:
            Buf = &Parameters->QueryVolumeInformation.VolumeBuffer;
            Len = &Parameters->QueryVolumeInformation.Length;
            Access = IoWriteAccess;
            break;

        case IRP_MJ_SET_VOLUME_INFORMATION:
            Buf = &Parameters->SetVolumeInformation.VolumeBuffer;
            Len = &Parameters->SetVolumeInformation.Length;
            break;

        case IRP_MJ_DIRECTORY_CONTROL:
            if (Iopb->MinorFunction != IRP_MN_QUERY_DIRECTORY &&
                Iopb->MinorFunction != IRP_MN_NOTIFY_CHANGE_DIRECTORY &&
                Iopb->MinorFunction != IRP_MN_NOTIFY_CHANGE_DIRECTORY_EX)
            {
                break;
            }
            Mdl = &Parameters->DirectoryControl.QueryDirectory.MdlAddress;
            Buf = &Parameters->DirectoryControl.QueryDirectory.DirectoryBuffer;
            Len = &Parameters->DirectoryControl.QueryDirectory.Length;
            Access = IoWriteAccess;
            break;

        case IRP_MJ_FILE_SYSTEM_CONTROL:
            if (Iopb->MinorFunction != IRP_MN_USER_FS_REQUEST && Iopb->MinorFunction != IRP_MN_KERNEL_CALL)
            {
                break;
            }
        case IRP_MJ_DEVICE_CONTROL:
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
            Len = &Parameters->DeviceIoControl.Common.OutputBufferLength;
            switch (FLTP_METHOD(Parameters->DeviceIoControl.Common.IoControlCode))
            {
                case METHOD_BUFFERED:
                    Buf = &Parameters->DeviceIoControl.Buffered.SystemBuffer;
                    Access = IoWriteAccess;
                    break;

                case METHOD_IN_DIRECT:
                    Mdl = &Parameters->DeviceIoControl.Direct.OutputMdlAddress;
                    Buf = &Parameters->DeviceIoControl.Direct.OutputBuffer;
                    break;

                case METHOD_OUT_DIRECT:
                    Mdl = &Parameters->DeviceIoControl.Direct.OutputMdlAddress;
                    Buf = &Parameters->DeviceIoControl.Direct.OutputBuffer;
                    Access = IoWriteAccess;
                    break;

                default:
                    Mdl = &Parameters->DeviceIoControl.Neither.OutputMdlAddress;
                    Buf = &Parameters->DeviceIoControl.Neither.OutputBuffer;
                    Access = IoWriteAccess;
                    break;
            }
            break;

        case IRP_MJ_QUERY_SECURITY:
            Mdl = &Parameters->QuerySecurity.MdlAddress;
            Buf = &Parameters->QuerySecurity.SecurityBuffer;
            Len = &Parameters->QuerySecurity.Length;
            Access = IoWriteAccess;
            break;

        case IRP_MJ_SET_SECURITY:
            Buf = (PVOID *)&Parameters->SetSecurity.SecurityDescriptor;
            break;

        case IRP_MJ_QUERY_QUOTA:
            Mdl = &Parameters->QueryQuota.MdlAddress;
            Buf = &Parameters->QueryQuota.QuotaBuffer;
            Len = &Parameters->QueryQuota.Length;
            Access = IoWriteAccess;
            break;

        case IRP_MJ_SET_QUOTA:
            Mdl = &Parameters->SetQuota.MdlAddress;
            Buf = &Parameters->SetQuota.QuotaBuffer;
            Len = &Parameters->SetQuota.Length;
            break;

        default:
            break;
    }

    *MdlAddressPointer = Mdl;
    *Buffer = Buf;
    *Length = Len;
    if (DesiredAccess != NULL)
    {
        *DesiredAccess = Access;
    }

    return (Buf != NULL) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
FLTAPI
FltDecodeParameters(
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _Outptr_opt_ PMDL **MdlAddressPointer,
    _Outptr_opt_result_bytebuffer_(**Length) PVOID **Buffer,
    _Outptr_opt_ PULONG *Length,
    _Out_opt_ LOCK_OPERATION *DesiredAccess)
{
    PMDL *Mdl;
    PVOID *Buf;
    PULONG Len;
    NTSTATUS Status;

    Status = FltpDecodeParameters(CallbackData->Iopb,
                                  &CallbackData->Iopb->Parameters,
                                  &Mdl,
                                  &Buf,
                                  &Len,
                                  DesiredAccess);
    if (MdlAddressPointer != NULL)
    {
        *MdlAddressPointer = Mdl;
    }
    if (Buffer != NULL)
    {
        *Buffer = Buf;
    }
    if (Length != NULL)
    {
        *Length = Len;
    }
    return Status;
}

static
BOOLEAN
FltpUsesSystemBuffer(
    _In_ PIRP Irp,
    _In_ UCHAR MajorFunction)
{
    switch (MajorFunction)
    {
        case IRP_MJ_CREATE:
        case IRP_MJ_QUERY_INFORMATION:
        case IRP_MJ_SET_INFORMATION:
        case IRP_MJ_QUERY_VOLUME_INFORMATION:
        case IRP_MJ_SET_VOLUME_INFORMATION:
        case IRP_MJ_SET_EA:
        case IRP_MJ_SET_QUOTA:
            return TRUE;

        case IRP_MJ_READ:
        case IRP_MJ_WRITE:
        case IRP_MJ_DIRECTORY_CONTROL:
        case IRP_MJ_QUERY_EA:
        case IRP_MJ_QUERY_QUOTA:
            return (Irp->Flags & IRP_BUFFERED_IO) != 0;

        default:
            return FALSE;
    }
}

VOID
FltpIrpToParameters(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _Inout_ PFLT_CALLBACK_DATA Data)
{
    PFLT_IO_PARAMETER_BLOCK Iopb = Data->Iopb;
    FLT_PARAMETERS *Parameters = &Iopb->Parameters;
    PMDL *Mdl;
    PVOID *Buffer;
    PULONG Length;

    RtlZeroMemory(Parameters, sizeof(*Parameters));
    Parameters->Others.Argument1 = Stack->Parameters.Others.Argument1;
    Parameters->Others.Argument2 = Stack->Parameters.Others.Argument2;
    Parameters->Others.Argument3 = Stack->Parameters.Others.Argument3;
    Parameters->Others.Argument4 = Stack->Parameters.Others.Argument4;

    switch (Iopb->MajorFunction)
    {
        case IRP_MJ_CREATE:
            Parameters->Create.EaBuffer = Irp->AssociatedIrp.SystemBuffer;
            Parameters->Create.AllocationSize = Irp->Overlay.AllocationSize;
            Data->Flags |= FLTFL_CALLBACK_DATA_SYSTEM_BUFFER;
            return;

        case IRP_MJ_FILE_SYSTEM_CONTROL:
            if (Iopb->MinorFunction != IRP_MN_USER_FS_REQUEST && Iopb->MinorFunction != IRP_MN_KERNEL_CALL)
            {
                return;
            }
        case IRP_MJ_DEVICE_CONTROL:
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
            switch (FLTP_METHOD(Stack->Parameters.DeviceIoControl.IoControlCode))
            {
                case METHOD_BUFFERED:
                    Parameters->DeviceIoControl.Buffered.SystemBuffer = Irp->AssociatedIrp.SystemBuffer;
                    Data->Flags |= FLTFL_CALLBACK_DATA_SYSTEM_BUFFER;
                    break;

                case METHOD_IN_DIRECT:
                case METHOD_OUT_DIRECT:
                    Parameters->DeviceIoControl.Direct.InputSystemBuffer = Irp->AssociatedIrp.SystemBuffer;
                    Parameters->DeviceIoControl.Direct.OutputBuffer =
                        Irp->MdlAddress ? MmGetMdlVirtualAddress(Irp->MdlAddress) : NULL;
                    Parameters->DeviceIoControl.Direct.OutputMdlAddress = Irp->MdlAddress;
                    break;

                default:
                    Parameters->DeviceIoControl.Neither.InputBuffer =
                        Stack->Parameters.DeviceIoControl.Type3InputBuffer;
                    Parameters->DeviceIoControl.Neither.OutputBuffer = Irp->UserBuffer;
                    Parameters->DeviceIoControl.Neither.OutputMdlAddress = Irp->MdlAddress;
                    break;
            }
            return;

        default:
            break;
    }

    if (!NT_SUCCESS(FltpDecodeParameters(Iopb, Parameters, &Mdl, &Buffer, &Length, NULL)))
    {
        return;
    }

    if (Iopb->MajorFunction == IRP_MJ_SET_SECURITY)
    {
        return;
    }

    if (FltpUsesSystemBuffer(Irp, Iopb->MajorFunction))
    {
        *Buffer = Irp->AssociatedIrp.SystemBuffer;
        Data->Flags |= FLTFL_CALLBACK_DATA_SYSTEM_BUFFER;
    }
    else
    {
        *Buffer = Irp->UserBuffer;
    }

    if (Mdl != NULL)
    {
        *Mdl = Irp->MdlAddress;
    }
}

VOID
FltpParametersToIrp(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _Inout_ PIRP Irp,
    _Inout_ PIO_STACK_LOCATION Stack)
{
    PFLT_IO_PARAMETER_BLOCK Iopb = &IrpCtrl->Iopb;
    FLT_PARAMETERS *Parameters = &Iopb->Parameters;
    PMDL NewMdl = Irp->MdlAddress;
    PVOID NewUser = Irp->UserBuffer;
    PVOID NewSystem = Irp->AssociatedIrp.SystemBuffer;
    PMDL *Mdl;
    PVOID *Buffer;
    PULONG Length;

    Stack->Parameters.Others.Argument1 = Parameters->Others.Argument1;
    Stack->Parameters.Others.Argument2 = Parameters->Others.Argument2;
    Stack->Parameters.Others.Argument3 = Parameters->Others.Argument3;
    Stack->Parameters.Others.Argument4 = Parameters->Others.Argument4;
    Stack->FileObject = Iopb->TargetFileObject;
    Stack->Flags = Iopb->OperationFlags;

    switch (Iopb->MajorFunction)
    {
        case IRP_MJ_CREATE:
            NewSystem = Parameters->Create.EaBuffer;
            Irp->Overlay.AllocationSize = Parameters->Create.AllocationSize;
            break;

        case IRP_MJ_FILE_SYSTEM_CONTROL:
            if (Iopb->MinorFunction != IRP_MN_USER_FS_REQUEST && Iopb->MinorFunction != IRP_MN_KERNEL_CALL)
            {
                break;
            }
        case IRP_MJ_DEVICE_CONTROL:
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
            switch (FLTP_METHOD(Parameters->DeviceIoControl.Common.IoControlCode))
            {
                case METHOD_BUFFERED:
                    NewSystem = Parameters->DeviceIoControl.Buffered.SystemBuffer;
                    break;

                case METHOD_IN_DIRECT:
                case METHOD_OUT_DIRECT:
                    NewSystem = Parameters->DeviceIoControl.Direct.InputSystemBuffer;
                    NewMdl = Parameters->DeviceIoControl.Direct.OutputMdlAddress;
                    break;

                default:
                    Stack->Parameters.DeviceIoControl.Type3InputBuffer =
                        Parameters->DeviceIoControl.Neither.InputBuffer;
                    NewUser = Parameters->DeviceIoControl.Neither.OutputBuffer;
                    NewMdl = Parameters->DeviceIoControl.Neither.OutputMdlAddress;
                    break;
            }
            break;

        case IRP_MJ_SET_SECURITY:
            break;

        default:
            if (!NT_SUCCESS(FltpDecodeParameters(Iopb, Parameters, &Mdl, &Buffer, &Length, NULL)))
            {
                break;
            }
            if (FltpUsesSystemBuffer(Irp, Iopb->MajorFunction))
            {
                NewSystem = *Buffer;
            }
            else
            {
                NewUser = *Buffer;
            }
            if (Mdl != NULL)
            {
                NewMdl = *Mdl;
            }
            break;
    }

    if (NewMdl != Irp->MdlAddress || NewUser != Irp->UserBuffer || NewSystem != Irp->AssociatedIrp.SystemBuffer)
    {
        IrpCtrl->Flags |= FLTP_IRPCTRL_SWAPPED;
        Irp->MdlAddress = NewMdl;
        Irp->UserBuffer = NewUser;
        Irp->AssociatedIrp.SystemBuffer = NewSystem;
    }
}

VOID
FLTAPI
FltSetCallbackDataDirty(
    _Inout_ PFLT_CALLBACK_DATA Data)
{
    Data->Flags |= FLTFL_CALLBACK_DATA_DIRTY;
}

VOID
FLTAPI
FltClearCallbackDataDirty(
    _Inout_ PFLT_CALLBACK_DATA Data)
{
    Data->Flags &= ~FLTFL_CALLBACK_DATA_DIRTY;
}

BOOLEAN
FLTAPI
FltIsCallbackDataDirty(
    _In_ PFLT_CALLBACK_DATA Data)
{
    return (Data->Flags & FLTFL_CALLBACK_DATA_DIRTY) != 0;
}

BOOLEAN
FLTAPI
FltIsOperationSynchronous(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLT_IO_PARAMETER_BLOCK Iopb = CallbackData->Iopb;

    if (!(CallbackData->Flags & FLTFL_CALLBACK_DATA_IRP_OPERATION))
    {
        return TRUE;
    }

    if (Iopb->IrpFlags & IRP_PAGING_IO)
    {
        return (Iopb->IrpFlags & IRP_SYNCHRONOUS_PAGING_IO) != 0;
    }

    if (Iopb->IrpFlags & (IRP_SYNCHRONOUS_API | IRP_SYNCHRONOUS_PAGING_IO))
    {
        return TRUE;
    }

    if (Iopb->TargetFileObject != NULL && (Iopb->TargetFileObject->Flags & FO_SYNCHRONOUS_IO))
    {
        return TRUE;
    }

    if ((Iopb->MajorFunction == IRP_MJ_DEVICE_CONTROL ||
         Iopb->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL ||
         (Iopb->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL &&
          (Iopb->MinorFunction == IRP_MN_USER_FS_REQUEST || Iopb->MinorFunction == IRP_MN_KERNEL_CALL))) &&
        FLTP_METHOD(Iopb->Parameters.DeviceIoControl.Common.IoControlCode) == METHOD_BUFFERED)
    {
        return TRUE;
    }

    return FALSE;
}

PEPROCESS
FLTAPI
FltGetRequestorProcess(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);

    if ((CallbackData->Flags & FLTFL_CALLBACK_DATA_IRP_OPERATION) && IrpCtrl->Irp != NULL)
    {
        return IoGetRequestorProcess(IrpCtrl->Irp);
    }

    if (CallbackData->Thread != NULL)
    {
        return IoThreadToProcess(CallbackData->Thread);
    }

    return PsGetCurrentProcess();
}

ULONG
FLTAPI
FltGetRequestorProcessId(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    return HandleToUlong(PsGetProcessId(FltGetRequestorProcess(CallbackData)));
}

HANDLE
FLTAPI
FltGetRequestorProcessIdEx(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    return PsGetProcessId(FltGetRequestorProcess(CallbackData));
}

NTSTATUS
FLTAPI
FltGetRequestorSessionId(
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _Out_ PULONG SessionId)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);

    if ((CallbackData->Flags & FLTFL_CALLBACK_DATA_IRP_OPERATION) && IrpCtrl->Irp != NULL)
    {
        return IoGetRequestorSessionId(IrpCtrl->Irp, SessionId);
    }

    *SessionId = PsGetProcessSessionId(FltGetRequestorProcess(CallbackData));
    return STATUS_SUCCESS;
}

BOOLEAN
FLTAPI
FltIs32bitProcess(
    _In_opt_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl;

    if (FltGlobals.Is32bitProcess == NULL)
    {
        return TRUE;
    }

    if (CallbackData != NULL)
    {
        IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);
        if ((CallbackData->Flags & FLTFL_CALLBACK_DATA_IRP_OPERATION) && IrpCtrl->Irp != NULL)
        {
            return FltGlobals.Is32bitProcess(IrpCtrl->Irp);
        }
    }

    return FltGlobals.Is32bitProcess(NULL);
}

PCHAR
FLTAPI
FltGetIrpName(
    _In_ UCHAR IrpMajorCode)
{
    static CONST PCHAR Names[] =
    {
        "IRP_MJ_CREATE", "IRP_MJ_CREATE_NAMED_PIPE", "IRP_MJ_CLOSE", "IRP_MJ_READ", "IRP_MJ_WRITE",
        "IRP_MJ_QUERY_INFORMATION", "IRP_MJ_SET_INFORMATION", "IRP_MJ_QUERY_EA", "IRP_MJ_SET_EA",
        "IRP_MJ_FLUSH_BUFFERS", "IRP_MJ_QUERY_VOLUME_INFORMATION", "IRP_MJ_SET_VOLUME_INFORMATION",
        "IRP_MJ_DIRECTORY_CONTROL", "IRP_MJ_FILE_SYSTEM_CONTROL", "IRP_MJ_DEVICE_CONTROL",
        "IRP_MJ_INTERNAL_DEVICE_CONTROL", "IRP_MJ_SHUTDOWN", "IRP_MJ_LOCK_CONTROL", "IRP_MJ_CLEANUP",
        "IRP_MJ_CREATE_MAILSLOT", "IRP_MJ_QUERY_SECURITY", "IRP_MJ_SET_SECURITY", "IRP_MJ_POWER",
        "IRP_MJ_SYSTEM_CONTROL", "IRP_MJ_DEVICE_CHANGE", "IRP_MJ_QUERY_QUOTA", "IRP_MJ_SET_QUOTA",
        "IRP_MJ_PNP"
    };

    if (IrpMajorCode < RTL_NUMBER_OF(Names))
    {
        return Names[IrpMajorCode];
    }

    switch (IrpMajorCode)
    {
        case IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION: return "IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION";
        case IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION: return "IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION";
        case IRP_MJ_ACQUIRE_FOR_MOD_WRITE: return "IRP_MJ_ACQUIRE_FOR_MOD_WRITE";
        case IRP_MJ_RELEASE_FOR_MOD_WRITE: return "IRP_MJ_RELEASE_FOR_MOD_WRITE";
        case IRP_MJ_ACQUIRE_FOR_CC_FLUSH: return "IRP_MJ_ACQUIRE_FOR_CC_FLUSH";
        case IRP_MJ_RELEASE_FOR_CC_FLUSH: return "IRP_MJ_RELEASE_FOR_CC_FLUSH";
        case IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE: return "IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE";
        case IRP_MJ_NETWORK_QUERY_OPEN: return "IRP_MJ_NETWORK_QUERY_OPEN";
        case IRP_MJ_MDL_READ: return "IRP_MJ_MDL_READ";
        case IRP_MJ_MDL_READ_COMPLETE: return "IRP_MJ_MDL_READ_COMPLETE";
        case IRP_MJ_PREPARE_MDL_WRITE: return "IRP_MJ_PREPARE_MDL_WRITE";
        case IRP_MJ_MDL_WRITE_COMPLETE: return "IRP_MJ_MDL_WRITE_COMPLETE";
        case IRP_MJ_VOLUME_MOUNT: return "IRP_MJ_VOLUME_MOUNT";
        case IRP_MJ_VOLUME_DISMOUNT: return "IRP_MJ_VOLUME_DISMOUNT";
        default: return "*** Unknown Operation ***";
    }
}

NTSTATUS
FLTAPI
FltLockUserBuffer(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLT_IO_PARAMETER_BLOCK Iopb = CallbackData->Iopb;
    PEPROCESS Process;
    KAPC_STATE ApcState;
    LOCK_OPERATION Access;
    PMDL *MdlPointer, Mdl;
    PVOID *Buffer;
    PULONG Length;
    NTSTATUS Status;
    BOOLEAN Attached = FALSE;

    Status = FltpDecodeParameters(Iopb, &Iopb->Parameters, &MdlPointer, &Buffer, &Length, &Access);
    if (!NT_SUCCESS(Status) || MdlPointer == NULL || Length == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((Iopb->MajorFunction == IRP_MJ_READ || Iopb->MajorFunction == IRP_MJ_WRITE) &&
        (Iopb->MinorFunction & IRP_MN_MDL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (*MdlPointer != NULL)
    {
        return STATUS_SUCCESS;
    }
    if (*Buffer == NULL || *Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mdl = IoAllocateMdl(*Buffer, *Length, FALSE, FALSE, NULL);
    if (Mdl == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if ((CallbackData->Flags & FLTFL_CALLBACK_DATA_SYSTEM_BUFFER) || (ULONG_PTR)*Buffer > (ULONG_PTR)MM_HIGHEST_USER_ADDRESS)
    {
        MmBuildMdlForNonPagedPool(Mdl);
    }
    else
    {
        Process = FltGetRequestorProcess(CallbackData);
        if (Process != PsGetCurrentProcess())
        {
            KeStackAttachProcess((PKPROCESS)Process, &ApcState);
            Attached = TRUE;
        }

        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, CallbackData->RequestorMode, Access);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (Attached)
        {
            KeUnstackDetachProcess(&ApcState);
        }

        if (!NT_SUCCESS(Status))
        {
            IoFreeMdl(Mdl);
            return Status;
        }
    }

    *MdlPointer = Mdl;
    if (!(CallbackData->Flags & FLTFL_CALLBACK_DATA_POST_OPERATION))
    {
        CallbackData->Flags |= FLTFL_CALLBACK_DATA_DIRTY;
    }
    return STATUS_SUCCESS;
}

PMDL
FASTCALL
FltGetSwappedBufferMdlAddress(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);

    if (!(CallbackData->Flags & FLTFL_CALLBACK_DATA_IRP_OPERATION) ||
        IrpCtrl->CurrentNode == NULL ||
        !IrpCtrl->CurrentNode->Swapped)
    {
        return NULL;
    }

    return IrpCtrl->LowerMdl;
}

VOID
FASTCALL
FltRetainSwappedBufferMdlAddress(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    FLTP_DATA_TO_IRP_CTRL(CallbackData)->RetainMdl = TRUE;
}

PVOID
FLTAPI
FltGetNewSystemBufferAddress(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    if (!(CallbackData->Flags & FLTFL_CALLBACK_DATA_NEW_SYSTEM_BUFFER))
    {
        return NULL;
    }

    return FLTP_DATA_TO_IRP_CTRL(CallbackData)->NewSystemBuffer;
}

NTSTATUS
FLTAPI
FltRequestOperationStatusCallback(
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ PFLT_GET_OPERATION_STATUS_CALLBACK CallbackRoutine,
    _In_opt_ PVOID RequesterContext)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(Data);
    PFLTP_STATUS_CALLBACK Callback;
    PFLT_INSTANCE Instance = Data->Iopb->TargetInstance;

    if (!(Data->Flags & FLTFL_CALLBACK_DATA_IRP_OPERATION) ||
        (Data->Flags & FLTFL_CALLBACK_DATA_POST_OPERATION) ||
        Data->Iopb->MajorFunction == IRP_MJ_CLOSE ||
        Instance == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Callback = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Callback), FLT_TAG_IRP_CTRL);
    if (Callback == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!ExAcquireRundownProtection(&Instance->Base.RundownRef))
    {
        ExFreePoolWithTag(Callback, FLT_TAG_IRP_CTRL);
        return STATUS_FLT_DELETING_OBJECT;
    }

    Callback->Instance = Instance;
    Callback->Callback = CallbackRoutine;
    Callback->RequesterContext = RequesterContext;
    Callback->Iopb = *Data->Iopb;
    Callback->Next = IrpCtrl->StatusCallbacks;
    IrpCtrl->StatusCallbacks = Callback;
    return STATUS_SUCCESS;
}

typedef struct _FLTP_SAFE_POST_CONTEXT
{
    WORK_QUEUE_ITEM WorkItem;
    PFLT_CALLBACK_DATA Data;
    FLTP_RELATED_OBJECTS FltObjects;
    PVOID CompletionContext;
    FLT_POST_OPERATION_FLAGS Flags;
    PFLT_POST_OPERATION_CALLBACK SafePostCallback;
} FLTP_SAFE_POST_CONTEXT, *PFLTP_SAFE_POST_CONTEXT;

static
VOID
NTAPI
FltpSafePostWorker(
    _In_ PVOID Parameter)
{
    PFLTP_SAFE_POST_CONTEXT Context = Parameter;
    FLT_POSTOP_CALLBACK_STATUS Status;

    Status = Context->SafePostCallback(Context->Data,
                                       (PCFLT_RELATED_OBJECTS)&Context->FltObjects,
                                       Context->CompletionContext,
                                       Context->Flags);
    if (Status != FLT_POSTOP_MORE_PROCESSING_REQUIRED)
    {
        FltCompletePendedPostOperation(Context->Data);
    }

    ExFreePoolWithTag(Context, FLT_TAG_WORK);
}

BOOLEAN
FLTAPI
FltDoCompletionProcessingWhenSafe(
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags,
    _In_ PFLT_POST_OPERATION_CALLBACK SafePostCallback,
    _Out_ PFLT_POSTOP_CALLBACK_STATUS RetPostOperationStatus)
{
    PFLTP_SAFE_POST_CONTEXT Context;

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        *RetPostOperationStatus = SafePostCallback(Data, FltObjects, CompletionContext, Flags);
        return TRUE;
    }

    *RetPostOperationStatus = FLT_POSTOP_FINISHED_PROCESSING;

    if (!(Data->Flags & FLTFL_CALLBACK_DATA_IRP_OPERATION) ||
        (Data->Iopb->IrpFlags & (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO)) ||
        (Flags & FLTFL_POST_OPERATION_DRAINING))
    {
        return FALSE;
    }

    Context = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Context), FLT_TAG_WORK);
    if (Context == NULL)
    {
        return FALSE;
    }

    Context->Data = Data;
    RtlCopyMemory(&Context->FltObjects, FltObjects, sizeof(Context->FltObjects));
    Context->CompletionContext = CompletionContext;
    Context->Flags = Flags;
    Context->SafePostCallback = SafePostCallback;
    ExInitializeWorkItem(&Context->WorkItem, FltpSafePostWorker, Context);
    ExQueueWorkItem(&Context->WorkItem, DelayedWorkQueue);

    *RetPostOperationStatus = FLT_POSTOP_MORE_PROCESSING_REQUIRED;
    return TRUE;
}

BOOLEAN
FLTAPI
FltIsIoCanceled(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);

    return (CallbackData->Flags & FLTFL_CALLBACK_DATA_IRP_OPERATION) &&
           IrpCtrl->Irp != NULL &&
           IrpCtrl->Irp->Cancel;
}
