/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     I/O operation processing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

static NTSTATUS FltpSendDown(_Inout_ PFLTP_IRP_CTRL IrpCtrl, _In_ BOOLEAN Resumed);
static NTSTATUS FltpContinueIrp(_Inout_ PFLTP_IRP_CTRL IrpCtrl, _In_ BOOLEAN Resumed);

VOID
FltpInitializeRelatedObjects(
    _Out_ PFLTP_RELATED_OBJECTS FltObjects,
    _In_ PFLT_INSTANCE Instance,
    _In_opt_ PFILE_OBJECT FileObject)
{
    RtlZeroMemory(FltObjects, sizeof(*FltObjects));
    FltObjects->Size = sizeof(*FltObjects);
    FltObjects->Filter = Instance->Filter;
    FltObjects->Volume = Instance->Volume;
    FltObjects->Instance = Instance;
    FltObjects->FileObject = FileObject;
}

PFLTP_IRP_CTRL
FltpAllocateIrpCtrl(
    _In_ PFLT_VOLUME Volume,
    _In_ ULONG NodeCount)
{
    PFLTP_IRP_CTRL IrpCtrl;
    SIZE_T Size = sizeof(FLTP_IRP_CTRL) + NodeCount * sizeof(FLTP_COMPLETION_NODE);

    IrpCtrl = ExAllocatePoolWithTag(NonPagedPoolNx, Size, FLT_TAG_IRP_CTRL);
    if (IrpCtrl == NULL)
    {
        return NULL;
    }

    RtlZeroMemory(IrpCtrl, Size);
    IrpCtrl->Signature = FLTP_IRP_CTRL_SIGNATURE;
    IrpCtrl->ReferenceCount = 1;
    *(PFLT_IO_PARAMETER_BLOCK *)&IrpCtrl->Data.Iopb = &IrpCtrl->Iopb;
    IrpCtrl->Volume = Volume;
    IrpCtrl->Nodes = (PFLTP_COMPLETION_NODE)(IrpCtrl + 1);
    IrpCtrl->NodeCount = NodeCount;
    IrpCtrl->PostNode = -1;
    InitializeListHead(&IrpCtrl->ActiveLink);
    InitializeListHead(&IrpCtrl->Names);
    KeInitializeEvent(&IrpCtrl->SyncEvent, NotificationEvent, FALSE);
    FltpReferencePointer(&Volume->Base);
    return IrpCtrl;
}

VOID
FltpReferenceIrpCtrl(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    InterlockedIncrement(&IrpCtrl->ReferenceCount);
}

VOID
FltpReleaseNodes(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    ULONG Index;

    for (Index = 0; Index < IrpCtrl->NodeCount; Index++)
    {
        if (InterlockedExchange(&IrpCtrl->Nodes[Index].State, FLTP_NODE_DONE) != FLTP_NODE_DONE)
        {
            ExReleaseRundownProtection(&IrpCtrl->Nodes[Index].Instance->Base.RundownRef);
        }
    }
}

VOID
FltpDereferenceIrpCtrl(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    PFLTP_STATUS_CALLBACK Callback;

    if (InterlockedDecrement(&IrpCtrl->ReferenceCount) != 0)
    {
        return;
    }

    FltpReleaseNodes(IrpCtrl);

    while ((Callback = IrpCtrl->StatusCallbacks) != NULL)
    {
        IrpCtrl->StatusCallbacks = Callback->Next;
        ExReleaseRundownProtection(&Callback->Instance->Base.RundownRef);
        ExFreePoolWithTag(Callback, FLT_TAG_IRP_CTRL);
    }

    if (IrpCtrl->Nodes != NULL && IrpCtrl->Nodes != (PFLTP_COMPLETION_NODE)(IrpCtrl + 1))
    {
        ExFreePoolWithTag(IrpCtrl->Nodes, FLT_TAG_IRP_CTRL);
    }

    FltpReleaseNameList(&IrpCtrl->Names);
    FltpDereferencePointer(&IrpCtrl->Volume->Base);
    IrpCtrl->Signature = 0;
    ExFreePoolWithTag(IrpCtrl, FLT_TAG_IRP_CTRL);
}

static
BOOLEAN
FltpIsInterested(
    _In_ PFLT_INSTANCE Instance,
    _In_opt_ PFLT_INSTANCE BelowInstance,
    _In_ ULONG OperationIndex,
    _In_ UCHAR MajorFunction,
    _In_ ULONG IrpFlags,
    _In_ BOOLEAN FastIo,
    _In_opt_ PFILE_OBJECT FileObject)
{
    PFLT_OPERATION_REGISTRATION Operation = &Instance->Filter->Operations[OperationIndex];
    BOOLEAN Paging = (IrpFlags & (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO)) != 0;
    BOOLEAN ReadWrite = (MajorFunction == IRP_MJ_READ || MajorFunction == IRP_MJ_WRITE);

    if (Operation->PreOperation == NULL && Operation->PostOperation == NULL)
    {
        return FALSE;
    }
    if (BelowInstance != NULL &&
        (Instance == BelowInstance || FltpCompareAltitude(&Instance->Altitude, &BelowInstance->Altitude) >= 0))
    {
        return FALSE;
    }
    if (Instance->Flags & (FLTP_INSTANCE_DETACHING | FLTP_INSTANCE_INITIALIZING))
    {
        return FALSE;
    }

    if (ReadWrite)
    {
        if ((Operation->Flags & FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO) && Paging)
        {
            return FALSE;
        }
        if ((Operation->Flags & FLTFL_OPERATION_REGISTRATION_SKIP_CACHED_IO) &&
            (FastIo || !(Paging || (IrpFlags & IRP_NOCACHE))))
        {
            return FALSE;
        }
        if ((Operation->Flags & FLTFL_OPERATION_REGISTRATION_SKIP_NON_CACHED_NON_PAGING_IO) &&
            !FastIo && (IrpFlags & IRP_NOCACHE) && !Paging)
        {
            return FALSE;
        }
        if ((Operation->Flags & FLTFL_OPERATION_REGISTRATION_SKIP_NON_DASD_IO) &&
            (FileObject == NULL || !(FileObject->Flags & FO_VOLUME_OPEN)))
        {
            return FALSE;
        }
    }

    return TRUE;
}

ULONG
FltpCollectNodes(
    _In_ PFLT_VOLUME Volume,
    _In_opt_ PFLT_INSTANCE BelowInstance,
    _In_ UCHAR MajorFunction,
    _In_ ULONG IrpFlags,
    _In_opt_ PFILE_OBJECT FileObject,
    _Out_ PFLTP_IRP_CTRL *RetIrpCtrl)
{
    ULONG OperationIndex = FltpOperationIndex(MajorFunction);
    BOOLEAN FastIo = (IrpFlags == MAXULONG);
    PFLTP_IRP_CTRL IrpCtrl = NULL;
    PFLT_INSTANCE Instance;
    PLIST_ENTRY Link;
    ULONG Count = 0, Filled = 0;

    *RetIrpCtrl = NULL;

    if (FastIo)
    {
        IrpFlags = 0;
    }

    if (OperationIndex == MAXULONG || Volume->InstanceCount == 0)
    {
        return 0;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Volume->InstanceLock);

    for (Link = Volume->InstanceList.Flink; Link != &Volume->InstanceList; Link = Link->Flink)
    {
        Instance = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
        if (FltpIsInterested(Instance, BelowInstance, OperationIndex, MajorFunction, IrpFlags, FastIo, FileObject))
        {
            Count++;
        }
    }

    if (Count != 0)
    {
        IrpCtrl = FltpAllocateIrpCtrl(Volume, Count);
    }

    if (IrpCtrl != NULL)
    {
        for (Link = Volume->InstanceList.Flink; Link != &Volume->InstanceList && Filled < Count; Link = Link->Flink)
        {
            Instance = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
            if (FltpIsInterested(Instance, BelowInstance, OperationIndex, MajorFunction, IrpFlags, FastIo, FileObject) &&
                ExAcquireRundownProtection(&Instance->Base.RundownRef))
            {
                IrpCtrl->Nodes[Filled].Instance = Instance;
                IrpCtrl->Nodes[Filled].Operation = &Instance->Filter->Operations[OperationIndex];
                IrpCtrl->Nodes[Filled].State = FLTP_NODE_IDLE;
                Filled++;
            }
        }
        IrpCtrl->NodeCount = Filled;
    }

    ExReleasePushLockShared(&Volume->InstanceLock);
    KeLeaveCriticalRegion();

    if (IrpCtrl != NULL && Filled == 0)
    {
        FltpDereferenceIrpCtrl(IrpCtrl);
        IrpCtrl = NULL;
    }

    *RetIrpCtrl = IrpCtrl;
    return Filled;
}

NTSTATUS
FltpAttachNodes(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_opt_ PFLT_INSTANCE BelowInstance)
{
    PFLT_VOLUME Volume = IrpCtrl->Volume;
    ULONG OperationIndex = FltpOperationIndex(IrpCtrl->Iopb.MajorFunction);
    PFLTP_COMPLETION_NODE Nodes = NULL;
    PFLT_INSTANCE Instance;
    PLIST_ENTRY Link;
    ULONG Count = 0, Filled = 0;

    if (IrpCtrl->Nodes != NULL && IrpCtrl->Nodes != (PFLTP_COMPLETION_NODE)(IrpCtrl + 1))
    {
        ExFreePoolWithTag(IrpCtrl->Nodes, FLT_TAG_IRP_CTRL);
    }
    IrpCtrl->Nodes = NULL;
    IrpCtrl->NodeCount = 0;

    if (OperationIndex == MAXULONG)
    {
        return STATUS_SUCCESS;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Volume->InstanceLock);

    for (Link = Volume->InstanceList.Flink; Link != &Volume->InstanceList; Link = Link->Flink)
    {
        Instance = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
        if (FltpIsInterested(Instance,
                             BelowInstance,
                             OperationIndex,
                             IrpCtrl->Iopb.MajorFunction,
                             IrpCtrl->Iopb.IrpFlags,
                             FALSE,
                             IrpCtrl->Iopb.TargetFileObject))
        {
            Count++;
        }
    }

    if (Count != 0)
    {
        Nodes = ExAllocatePoolWithTag(NonPagedPoolNx, Count * sizeof(FLTP_COMPLETION_NODE), FLT_TAG_IRP_CTRL);
    }

    if (Nodes != NULL)
    {
        RtlZeroMemory(Nodes, Count * sizeof(FLTP_COMPLETION_NODE));
        for (Link = Volume->InstanceList.Flink; Link != &Volume->InstanceList && Filled < Count; Link = Link->Flink)
        {
            Instance = CONTAINING_RECORD(Link, FLT_INSTANCE, VolumeLink);
            if (FltpIsInterested(Instance,
                                 BelowInstance,
                                 OperationIndex,
                                 IrpCtrl->Iopb.MajorFunction,
                                 IrpCtrl->Iopb.IrpFlags,
                                 FALSE,
                                 IrpCtrl->Iopb.TargetFileObject) &&
                ExAcquireRundownProtection(&Instance->Base.RundownRef))
            {
                Nodes[Filled].Instance = Instance;
                Nodes[Filled].Operation = &Instance->Filter->Operations[OperationIndex];
                Nodes[Filled].State = FLTP_NODE_IDLE;
                Filled++;
            }
        }
    }

    ExReleasePushLockShared(&Volume->InstanceLock);
    KeLeaveCriticalRegion();

    if (Count != 0 && Nodes == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IrpCtrl->Nodes = Nodes;
    IrpCtrl->NodeCount = Filled;
    return STATUS_SUCCESS;
}

static
BOOLEAN
FltpIsNameChange(
    _In_ UCHAR MajorFunction,
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    if (MajorFunction != IRP_MJ_SET_INFORMATION)
    {
        return FALSE;
    }

    switch (FileInformationClass)
    {
        case FileRenameInformation:
        case FileRenameInformationBypassAccessCheck:
        case FileRenameInformationEx:
        case FileRenameInformationExBypassAccessCheck:
        case FileShortNameInformation:
            return TRUE;

        default:
            return FALSE;
    }
}

static
VOID
FltpPrepareNameChange(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    if (FltpIsNameChange(IrpCtrl->Iopb.MajorFunction,
                         IrpCtrl->Iopb.Parameters.SetFileInformation.FileInformationClass))
    {
        IrpCtrl->Flags |= FLTP_IRPCTRL_NAME_CHANGE;
        InterlockedIncrement(&FltGlobals.NameGeneration);
    }
}

NTSTATUS
FltpStartIrp(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    FltpPrepareNameChange(IrpCtrl);
    FltpSynchronizeSections(&IrpCtrl->Data);
    return FltpContinueIrp(IrpCtrl, FALSE);
}

PFLT_INSTANCE
FltpCreateTargetInstance(
    _In_ PIRP Irp)
{
    PECP_LIST EcpList = NULL;
    PVOID Context;

    if (!NT_SUCCESS(FsRtlGetEcpListFromIrp(Irp, &EcpList)) || EcpList == NULL)
    {
        return NULL;
    }
    if (!NT_SUCCESS(FsRtlFindExtraCreateParameter(EcpList, &FltpTargetInstanceEcp, &Context, NULL)))
    {
        return NULL;
    }

    return *(PFLT_INSTANCE *)Context;
}

static
VOID
FltpApplyPreStatus(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _Inout_ PFLTP_COMPLETION_NODE Node,
    _In_ FLT_PREOP_CALLBACK_STATUS Status,
    _In_opt_ PVOID CompletionContext)
{
    if ((Status == FLT_PREOP_SUCCESS_WITH_CALLBACK || Status == FLT_PREOP_SYNCHRONIZE) &&
        Node->Operation->PostOperation != NULL)
    {
        Node->CompletionContext = CompletionContext;
        if (Status == FLT_PREOP_SYNCHRONIZE)
        {
            Node->Synchronize = TRUE;
            IrpCtrl->Flags |= FLTP_IRPCTRL_SYNCHRONIZE;
        }
        InterlockedExchange(&Node->State, FLTP_NODE_POST);
    }
    else
    {
        if (InterlockedExchange(&Node->State, FLTP_NODE_DONE) != FLTP_NODE_DONE)
        {
            ExReleaseRundownProtection(&Node->Instance->Base.RundownRef);
        }
    }
}

static
VOID
FltpDetectSwap(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _Inout_ PFLTP_COMPLETION_NODE Node)
{
    PMDL *OldMdl, *NewMdl;
    PVOID *OldBuffer, *NewBuffer;
    PULONG Length;

    if (!NT_SUCCESS(FltpDecodeParameters(&IrpCtrl->Iopb, &Node->SavedParameters, &OldMdl, &OldBuffer, &Length, NULL)) ||
        !NT_SUCCESS(FltpDecodeParameters(&IrpCtrl->Iopb, &IrpCtrl->Iopb.Parameters, &NewMdl, &NewBuffer, &Length, NULL)))
    {
        return;
    }

    if ((OldMdl != NULL && *OldMdl != *NewMdl) || *OldBuffer != *NewBuffer)
    {
        Node->Swapped = TRUE;
    }
}

FLT_PREOP_CALLBACK_STATUS
FltpRunPreCallbacks(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    PFLT_CALLBACK_DATA Data = &IrpCtrl->Data;
    FLTP_RELATED_OBJECTS FltObjects;
    PFLTP_COMPLETION_NODE Node;
    FLT_PREOP_CALLBACK_STATUS Status;
    PVOID CompletionContext;

    while (IrpCtrl->NextNode < IrpCtrl->NodeCount)
    {
        Node = &IrpCtrl->Nodes[IrpCtrl->NextNode];

        Node->SavedParameters = IrpCtrl->Iopb.Parameters;
        Node->SavedTargetFileObject = IrpCtrl->Iopb.TargetFileObject;
        Node->ParametersSaved = TRUE;
        IrpCtrl->Iopb.TargetInstance = Node->Instance;

        if (Node->Operation->PreOperation != NULL)
        {
            FltpInitializeRelatedObjects(&FltObjects, Node->Instance, IrpCtrl->Iopb.TargetFileObject);
            CompletionContext = NULL;
            Status = Node->Operation->PreOperation(Data, (PCFLT_RELATED_OBJECTS)&FltObjects, &CompletionContext);
        }
        else
        {
            CompletionContext = NULL;
            Status = FLT_PREOP_SUCCESS_WITH_CALLBACK;
        }

        switch (Status)
        {
            case FLT_PREOP_SUCCESS_WITH_CALLBACK:
            case FLT_PREOP_SUCCESS_NO_CALLBACK:
            case FLT_PREOP_SYNCHRONIZE:
                FltpDetectSwap(IrpCtrl, Node);
                FltpApplyPreStatus(IrpCtrl, Node, Status, CompletionContext);
                IrpCtrl->NextNode++;
                break;

            case FLT_PREOP_PENDING:
            case FLT_PREOP_COMPLETE:
            case FLT_PREOP_DISALLOW_FASTIO:
            case FLT_PREOP_DISALLOW_FSFILTER_IO:
                return Status;

            default:
                FltpApplyPreStatus(IrpCtrl, Node, FLT_PREOP_SUCCESS_NO_CALLBACK, NULL);
                IrpCtrl->NextNode++;
                break;
        }
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

VOID
FltpBeginPostPhase(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    IrpCtrl->PostNode = (LONG)min(IrpCtrl->NextNode, IrpCtrl->NodeCount) - 1;
    IrpCtrl->Data.Flags |= FLTFL_CALLBACK_DATA_POST_OPERATION;
}

VOID
FltpFreeMdl(
    _In_ PMDL Mdl)
{
    if (Mdl->MdlFlags & MDL_PAGES_LOCKED)
    {
        MmUnlockPages(Mdl);
    }
    IoFreeMdl(Mdl);
}

static
PMDL
FltpParameterMdl(
    _In_ PFLTP_IRP_CTRL IrpCtrl,
    _In_ FLT_PARAMETERS *Parameters)
{
    PMDL *Mdl;
    PVOID *Buffer;
    PULONG Length;

    if (!NT_SUCCESS(FltpDecodeParameters(&IrpCtrl->Iopb, Parameters, &Mdl, &Buffer, &Length, NULL)) || Mdl == NULL)
    {
        return NULL;
    }
    return *Mdl;
}

VOID
FltpFinishNode(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _Inout_ PFLTP_COMPLETION_NODE Node)
{
    PMDL After = FltpParameterMdl(IrpCtrl, &IrpCtrl->Iopb.Parameters);
    PMDL Saved = Node->ParametersSaved ? FltpParameterMdl(IrpCtrl, &Node->SavedParameters) : NULL;

    if (After != NULL && After != Node->PostMdl && After != Saved)
    {
        After->Next = IrpCtrl->PostMdls;
        IrpCtrl->PostMdls = After;
    }

    if (Node->Swapped)
    {
        if (IrpCtrl->LowerMdl != NULL && IrpCtrl->LowerMdl != Saved && !IrpCtrl->RetainMdl)
        {
            FltpFreeMdl(IrpCtrl->LowerMdl);
        }
        IrpCtrl->LowerMdl = Saved;
    }

    IrpCtrl->CurrentNode = NULL;
    InterlockedExchange(&Node->State, FLTP_NODE_DONE);
    ExReleaseRundownProtection(&Node->Instance->Base.RundownRef);
}

BOOLEAN
FltpRunPostCallbacks(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    PFLT_CALLBACK_DATA Data = &IrpCtrl->Data;
    FLTP_RELATED_OBJECTS FltObjects;
    PFLTP_COMPLETION_NODE Node;
    FLT_POSTOP_CALLBACK_STATUS Status;

    while (IrpCtrl->PostNode >= 0)
    {
        Node = &IrpCtrl->Nodes[IrpCtrl->PostNode];
        IrpCtrl->PostNode--;

        if (InterlockedCompareExchange(&Node->State, FLTP_NODE_PENDED, FLTP_NODE_POST) != FLTP_NODE_POST)
        {
            continue;
        }

        if (Node->ParametersSaved)
        {
            IrpCtrl->Iopb.Parameters = Node->SavedParameters;
            IrpCtrl->Iopb.TargetFileObject = Node->SavedTargetFileObject;
        }
        IrpCtrl->Iopb.TargetInstance = Node->Instance;
        IrpCtrl->CurrentNode = Node;
        IrpCtrl->RetainMdl = FALSE;
        Node->PostMdl = FltpParameterMdl(IrpCtrl, &IrpCtrl->Iopb.Parameters);
        FltpInitializeRelatedObjects(&FltObjects, Node->Instance, IrpCtrl->Iopb.TargetFileObject);

        InterlockedExchange(&IrpCtrl->PostPendState, FLTP_PEND_CALLING);
        Status = Node->Operation->PostOperation(Data, (PCFLT_RELATED_OBJECTS)&FltObjects, Node->CompletionContext, Flags);
        if (Status == FLT_POSTOP_MORE_PROCESSING_REQUIRED && IrpCtrl->Irp != NULL)
        {
            IoMarkIrpPending(IrpCtrl->Irp);
            IrpCtrl->Flags |= FLTP_IRPCTRL_POST_PENDED | FLTP_IRPCTRL_IRP_PENDING;
            if (InterlockedCompareExchange(&IrpCtrl->PostPendState,
                                           FLTP_PEND_WAITING,
                                           FLTP_PEND_CALLING) == FLTP_PEND_CALLING)
            {
                return FALSE;
            }
        }

        InterlockedExchange(&IrpCtrl->PostPendState, FLTP_PEND_NONE);
        FltpFinishNode(IrpCtrl, Node);
    }

    return TRUE;
}

VOID
FltpRunStatusCallbacks(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_ NTSTATUS Status)
{
    PFLTP_STATUS_CALLBACK Callback;
    FLTP_RELATED_OBJECTS FltObjects;

    while ((Callback = InterlockedExchangePointer((PVOID *)&IrpCtrl->StatusCallbacks, NULL)) != NULL)
    {
        while (Callback != NULL)
        {
            PFLTP_STATUS_CALLBACK Next = Callback->Next;

            FltpInitializeRelatedObjects(&FltObjects, Callback->Instance, Callback->Iopb.TargetFileObject);
            Callback->Callback((PCFLT_RELATED_OBJECTS)&FltObjects, &Callback->Iopb, Status, Callback->RequesterContext);
            ExReleaseRundownProtection(&Callback->Instance->Base.RundownRef);
            ExFreePoolWithTag(Callback, FLT_TAG_IRP_CTRL);
            Callback = Next;
        }
    }
}

static
VOID
FltpActivate(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&IrpCtrl->Volume->ActiveLock, &OldIrql);
    InsertTailList(&IrpCtrl->Volume->ActiveList, &IrpCtrl->ActiveLink);
    IrpCtrl->Flags |= FLTP_IRPCTRL_ACTIVE;
    KeReleaseSpinLock(&IrpCtrl->Volume->ActiveLock, OldIrql);
}

static
VOID
FltpDeactivate(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&IrpCtrl->Volume->ActiveLock, &OldIrql);
    if (IrpCtrl->Flags & FLTP_IRPCTRL_ACTIVE)
    {
        RemoveEntryList(&IrpCtrl->ActiveLink);
        InitializeListHead(&IrpCtrl->ActiveLink);
        IrpCtrl->Flags &= ~FLTP_IRPCTRL_ACTIVE;
    }
    KeReleaseSpinLock(&IrpCtrl->Volume->ActiveLock, OldIrql);
}

VOID
FltpDrainInstance(
    _In_ PFLT_INSTANCE Instance)
{
    PFLT_VOLUME Volume = Instance->Volume;
    FLTP_RELATED_OBJECTS FltObjects;
    PFLTP_IRP_CTRL IrpCtrl, Found;
    PFLTP_COMPLETION_NODE Node;
    PLIST_ENTRY Link;
    ULONG Index;
    KIRQL OldIrql;

    for (;;)
    {
        Found = NULL;
        Node = NULL;

        KeAcquireSpinLock(&Volume->ActiveLock, &OldIrql);
        for (Link = Volume->ActiveList.Flink; Link != &Volume->ActiveList && Found == NULL; Link = Link->Flink)
        {
            IrpCtrl = CONTAINING_RECORD(Link, FLTP_IRP_CTRL, ActiveLink);
            for (Index = 0; Index < IrpCtrl->NodeCount; Index++)
            {
                if (IrpCtrl->Nodes[Index].Instance == Instance &&
                    InterlockedCompareExchange(&IrpCtrl->Nodes[Index].State,
                                               FLTP_NODE_DONE,
                                               FLTP_NODE_POST) == FLTP_NODE_POST)
                {
                    Found = IrpCtrl;
                    Node = &IrpCtrl->Nodes[Index];
                    FltpReferenceIrpCtrl(IrpCtrl);
                    break;
                }
            }
        }
        KeReleaseSpinLock(&Volume->ActiveLock, OldIrql);

        if (Found == NULL)
        {
            break;
        }

        FltpInitializeRelatedObjects(&FltObjects, Instance, Found->Iopb.TargetFileObject);
        Node->Operation->PostOperation(&Found->Data,
                                       (PCFLT_RELATED_OBJECTS)&FltObjects,
                                       Node->CompletionContext,
                                       FLTFL_POST_OPERATION_DRAINING);
        ExReleaseRundownProtection(&Instance->Base.RundownRef);
        FltpDereferenceIrpCtrl(Found);
    }
}

VOID
FltpCaptureCompletion(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    PIRP Irp = IrpCtrl->Irp;
    PMDL *Mdl;
    PVOID *Buffer;
    PULONG Length;
    ULONG Index;

    IrpCtrl->Data.IoStatus = Irp->IoStatus;

    if (IrpCtrl->Flags & FLTP_IRPCTRL_NAME_CHANGE)
    {
        InterlockedIncrement(&FltGlobals.NameGeneration);
    }

    if (Irp->AssociatedIrp.SystemBuffer != IrpCtrl->SentSystemBuffer)
    {
        IrpCtrl->NewSystemBuffer = Irp->AssociatedIrp.SystemBuffer;
        IrpCtrl->Data.Flags |= FLTFL_CALLBACK_DATA_NEW_SYSTEM_BUFFER;
    }

    if (IrpCtrl->Flags & FLTP_IRPCTRL_SWAPPED)
    {
        IrpCtrl->LowerMdl = Irp->MdlAddress;
        Irp->MdlAddress = IrpCtrl->OriginalMdl;
        Irp->UserBuffer = IrpCtrl->OriginalUserBuffer;
        if (!(IrpCtrl->Data.Flags & FLTFL_CALLBACK_DATA_NEW_SYSTEM_BUFFER))
        {
            Irp->AssociatedIrp.SystemBuffer = IrpCtrl->OriginalSystemBuffer;
        }
        return;
    }

    if (Irp->MdlAddress == IrpCtrl->OriginalMdl)
    {
        return;
    }

    if (NT_SUCCESS(FltpDecodeParameters(&IrpCtrl->Iopb, &IrpCtrl->Iopb.Parameters, &Mdl, &Buffer, &Length, NULL)) &&
        Mdl != NULL)
    {
        *Mdl = Irp->MdlAddress;
    }
    for (Index = 0; Index < IrpCtrl->NodeCount; Index++)
    {
        if (IrpCtrl->Nodes[Index].ParametersSaved &&
            NT_SUCCESS(FltpDecodeParameters(&IrpCtrl->Iopb,
                                            &IrpCtrl->Nodes[Index].SavedParameters,
                                            &Mdl,
                                            &Buffer,
                                            &Length,
                                            NULL)) &&
            Mdl != NULL)
        {
            *Mdl = Irp->MdlAddress;
        }
    }
}

static
VOID
FltpFreePostMdls(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    PMDL Mdl, Next;

    for (Mdl = IrpCtrl->PostMdls; Mdl != NULL; Mdl = Next)
    {
        Next = Mdl->Next;
        Mdl->Next = NULL;
        if (IrpCtrl->Irp != NULL && IrpCtrl->Irp->MdlAddress == Mdl)
        {
            continue;
        }
        FltpFreeMdl(Mdl);
    }
    IrpCtrl->PostMdls = NULL;
}

static
NTSTATUS
FltpFinishIrp(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl)
{
    PIRP Irp = IrpCtrl->Irp;
    NTSTATUS Status;

    if (!FltpRunPostCallbacks(IrpCtrl, 0))
    {
        return STATUS_PENDING;
    }

    Irp->IoStatus = IrpCtrl->Data.IoStatus;
    Status = (IrpCtrl->Flags & FLTP_IRPCTRL_IRP_PENDING) ? STATUS_PENDING : Irp->IoStatus.Status;
    FltpFreePostMdls(IrpCtrl);
    FltpReleaseNodes(IrpCtrl);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    FltpDereferenceIrpCtrl(IrpCtrl);
    return Status;
}

static
NTSTATUS
NTAPI
FltpCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PFLTP_IRP_CTRL IrpCtrl = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (IrpCtrl->Flags & FLTP_IRPCTRL_SYNCHRONIZE)
    {
        KeSetEvent(&IrpCtrl->SyncEvent, IO_NO_INCREMENT, FALSE);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    FltpDeactivate(IrpCtrl);
    FltpCaptureCompletion(IrpCtrl);
    FltpBeginPostPhase(IrpCtrl);

    if (!FltpRunPostCallbacks(IrpCtrl, 0))
    {
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    Irp->IoStatus = IrpCtrl->Data.IoStatus;
    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }

    FltpFreePostMdls(IrpCtrl);
    FltpReleaseNodes(IrpCtrl);
    FltpDereferenceIrpCtrl(IrpCtrl);
    return STATUS_SUCCESS;
}

static
NTSTATUS
FltpCompleteByFilter(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_ BOOLEAN Resumed)
{
    PFLTP_COMPLETION_NODE Node = &IrpCtrl->Nodes[IrpCtrl->NextNode];
    NTSTATUS Status;

    if (InterlockedExchange(&Node->State, FLTP_NODE_DONE) != FLTP_NODE_DONE)
    {
        ExReleaseRundownProtection(&Node->Instance->Base.RundownRef);
    }

    FltpRunStatusCallbacks(IrpCtrl, IrpCtrl->Data.IoStatus.Status);
    FltpBeginPostPhase(IrpCtrl);
    FltpReferenceIrpCtrl(IrpCtrl);
    Status = FltpFinishIrp(IrpCtrl);
    FltpDereferenceIrpCtrl(IrpCtrl);
    return Resumed ? STATUS_PENDING : Status;
}

static
NTSTATUS
FltpSendDown(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_ BOOLEAN Resumed)
{
    PFLTP_DEVICE_EXTENSION Extension = IrpCtrl->DeviceObject->DeviceExtension;
    PIRP Irp = IrpCtrl->Irp;
    BOOLEAN HasPost = FALSE;
    NTSTATUS Status;
    ULONG Index;

    if (IrpCtrl->Iopb.MajorFunction == IRP_MJ_CLOSE)
    {
        FltpCleanupFileObjectContexts(IrpCtrl->Iopb.TargetFileObject);
    }

    for (Index = 0; Index < IrpCtrl->NodeCount; Index++)
    {
        if (IrpCtrl->Nodes[Index].State == FLTP_NODE_POST)
        {
            HasPost = TRUE;
            break;
        }
    }

    if (!HasPost && IrpCtrl->StatusCallbacks == NULL && !Resumed && !(IrpCtrl->Flags & FLTP_IRPCTRL_NAME_CHANGE))
    {
        if (IrpCtrl->Data.Flags & FLTFL_CALLBACK_DATA_DIRTY)
        {
            FltpParametersToIrp(IrpCtrl, Irp, IoGetCurrentIrpStackLocation(Irp));
        }
        IoSkipCurrentIrpStackLocation(Irp);
        FltpDereferenceIrpCtrl(IrpCtrl);
        return IoCallDriver(Extension->AttachedToDeviceObject, Irp);
    }

    if (Resumed || IrpCtrl->Iopb.MajorFunction == IRP_MJ_CREATE)
    {
        IrpCtrl->Flags |= FLTP_IRPCTRL_SYNCHRONIZE;
    }

    IoCopyCurrentIrpStackLocationToNext(Irp);
    if (IrpCtrl->Data.Flags & FLTFL_CALLBACK_DATA_DIRTY)
    {
        FltpParametersToIrp(IrpCtrl, Irp, IoGetNextIrpStackLocation(Irp));
    }
    IrpCtrl->SentSystemBuffer = Irp->AssociatedIrp.SystemBuffer;
    IoSetCompletionRoutine(Irp, FltpCompletionRoutine, IrpCtrl, TRUE, TRUE, TRUE);

    FltpActivate(IrpCtrl);
    FltpReferenceIrpCtrl(IrpCtrl);

    Status = IoCallDriver(Extension->AttachedToDeviceObject, Irp);
    FltpRunStatusCallbacks(IrpCtrl, Status);

    if (IrpCtrl->Flags & FLTP_IRPCTRL_SYNCHRONIZE)
    {
        KeWaitForSingleObject(&IrpCtrl->SyncEvent, Executive, KernelMode, FALSE, NULL);
        FltpDeactivate(IrpCtrl);
        FltpCaptureCompletion(IrpCtrl);
        FltpBeginPostPhase(IrpCtrl);
        Status = FltpFinishIrp(IrpCtrl);
        if (Resumed)
        {
            Status = STATUS_PENDING;
        }
    }
    else if (IrpCtrl->Flags & FLTP_IRPCTRL_IRP_PENDING)
    {
        Status = STATUS_PENDING;
    }

    FltpDereferenceIrpCtrl(IrpCtrl);
    return Status;
}

static
NTSTATUS
FltpContinueIrp(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_ BOOLEAN Resumed)
{
    FLT_PREOP_CALLBACK_STATUS PreStatus;
    PFLTP_COMPLETION_NODE Node;

    for (;;)
    {
        PreStatus = FltpRunPreCallbacks(IrpCtrl);

        if (PreStatus != FLT_PREOP_PENDING)
        {
            break;
        }

        if (!Resumed)
        {
            IoMarkIrpPending(IrpCtrl->Irp);
            Resumed = TRUE;
        }
        IrpCtrl->Flags |= FLTP_IRPCTRL_PRE_PENDED;

        if (InterlockedCompareExchange(&IrpCtrl->PendState, FLTP_PEND_WAITING, FLTP_PEND_NONE) == FLTP_PEND_NONE)
        {
            return STATUS_PENDING;
        }

        InterlockedExchange(&IrpCtrl->PendState, FLTP_PEND_NONE);
        Node = &IrpCtrl->Nodes[IrpCtrl->NextNode];
        if (IrpCtrl->ResumeStatus == FLT_PREOP_COMPLETE)
        {
            PreStatus = FLT_PREOP_COMPLETE;
            break;
        }
        FltpApplyPreStatus(IrpCtrl, Node, IrpCtrl->ResumeStatus, IrpCtrl->ResumeContext);
        IrpCtrl->NextNode++;
    }

    if (PreStatus == FLT_PREOP_COMPLETE)
    {
        return FltpCompleteByFilter(IrpCtrl, Resumed);
    }

    return FltpSendDown(IrpCtrl, Resumed);
}

NTSTATUS
FltpPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFLTP_DEVICE_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->AttachedToDeviceObject, Irp);
}

NTSTATUS
FltpProcessIrp(
    _In_ PFLT_VOLUME Volume,
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFLT_INSTANCE BelowInstance = NULL;
    PFLTP_IRP_CTRL IrpCtrl;

    if (Stack->MajorFunction == IRP_MJ_CREATE ||
        Stack->MajorFunction == IRP_MJ_CREATE_NAMED_PIPE ||
        Stack->MajorFunction == IRP_MJ_CREATE_MAILSLOT)
    {
        BelowInstance = FltpCreateTargetInstance(Irp);
    }

    if (FltpCollectNodes(Volume, BelowInstance, Stack->MajorFunction, Irp->Flags, Stack->FileObject, &IrpCtrl) == 0)
    {
        if (FltpIsNameChange(Stack->MajorFunction, Stack->Parameters.SetFile.FileInformationClass) ||
            ((Stack->MajorFunction == IRP_MJ_WRITE || Stack->MajorFunction == IRP_MJ_SET_INFORMATION) &&
             FltpHasSections(Stack->FileObject)))
        {
            IrpCtrl = FltpAllocateIrpCtrl(Volume, 0);
        }
        if (IrpCtrl == NULL)
        {
            if (Stack->MajorFunction == IRP_MJ_CLOSE)
            {
                FltpCleanupFileObjectContexts(Stack->FileObject);
            }
            return FltpPassThrough(DeviceObject, Irp);
        }
    }

    IrpCtrl->Irp = Irp;
    IrpCtrl->DeviceObject = DeviceObject;
    IrpCtrl->OriginalMdl = Irp->MdlAddress;
    IrpCtrl->OriginalUserBuffer = Irp->UserBuffer;
    IrpCtrl->OriginalSystemBuffer = Irp->AssociatedIrp.SystemBuffer;

    IrpCtrl->Data.Flags = FLTFL_CALLBACK_DATA_IRP_OPERATION;
    *(PETHREAD *)&IrpCtrl->Data.Thread = Irp->Tail.Overlay.Thread;
    IrpCtrl->Data.RequestorMode = Irp->RequestorMode;
    IrpCtrl->Iopb.IrpFlags = Irp->Flags;
    IrpCtrl->Iopb.MajorFunction = Stack->MajorFunction;
    IrpCtrl->Iopb.MinorFunction = Stack->MinorFunction;
    IrpCtrl->Iopb.OperationFlags = Stack->Flags;
    IrpCtrl->Iopb.TargetFileObject = Stack->FileObject;
    FltpIrpToParameters(Irp, Stack, &IrpCtrl->Data);

    FltpPrepareNameChange(IrpCtrl);
    FltpSynchronizeSections(&IrpCtrl->Data);
    return FltpContinueIrp(IrpCtrl, FALSE);
}

NTSTATUS
NTAPI
FltpDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFLTP_DEVICE_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFLT_VOLUME Volume;

    if (DeviceObject == FltGlobals.ControlDevice)
    {
        return FltpControlDispatch(DeviceObject, Irp);
    }
    if (DeviceObject == FltGlobals.MessageDevice)
    {
        return FltpMessageDispatch(DeviceObject, Irp);
    }

    if (Extension->FileSystemControlDevice)
    {
        if (Stack->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL &&
            Stack->MinorFunction == IRP_MN_MOUNT_VOLUME)
        {
            return FltpMountVolume(DeviceObject, Irp);
        }
        return FltpPassThrough(DeviceObject, Irp);
    }

    Volume = Extension->Volume;
    if (Volume == NULL)
    {
        return FltpPassThrough(DeviceObject, Irp);
    }

    if ((Volume->Flags & FLTP_VOLUME_SETUP_PENDING) &&
        KeGetCurrentIrql() == PASSIVE_LEVEL &&
        !(Irp->Flags & (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO)) &&
        (InterlockedAnd((PLONG)&Volume->Flags, ~FLTP_VOLUME_SETUP_PENDING) & FLTP_VOLUME_SETUP_PENDING))
    {
        FltpAttachFiltersToVolume(Volume, TRUE);
    }

    return FltpProcessIrp(Volume, DeviceObject, Irp);
}

VOID
FLTAPI
FltCompletePendedPreOperation(
    _Inout_ PFLT_CALLBACK_DATA CallbackData,
    _In_ FLT_PREOP_CALLBACK_STATUS CallbackStatus,
    _In_opt_ PVOID Context)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);
    PFLTP_COMPLETION_NODE Node;

    IrpCtrl->ResumeStatus = CallbackStatus;
    IrpCtrl->ResumeContext = Context;

    if (InterlockedCompareExchange(&IrpCtrl->PendState, FLTP_PEND_RESUME, FLTP_PEND_NONE) == FLTP_PEND_NONE)
    {
        return;
    }

    InterlockedExchange(&IrpCtrl->PendState, FLTP_PEND_NONE);

    if (CallbackStatus == FLT_PREOP_COMPLETE)
    {
        (VOID)FltpCompleteByFilter(IrpCtrl, TRUE);
        return;
    }

    Node = &IrpCtrl->Nodes[IrpCtrl->NextNode];
    FltpApplyPreStatus(IrpCtrl, Node, CallbackStatus, Context);
    IrpCtrl->NextNode++;
    (VOID)FltpContinueIrp(IrpCtrl, TRUE);
}

VOID
FLTAPI
FltCompletePendedPostOperation(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PFLTP_IRP_CTRL IrpCtrl = FLTP_DATA_TO_IRP_CTRL(CallbackData);
    PFLTP_COMPLETION_NODE Node = &IrpCtrl->Nodes[IrpCtrl->PostNode + 1];
    PIRP Irp = IrpCtrl->Irp;

    if (InterlockedCompareExchange(&IrpCtrl->PostPendState,
                                   FLTP_PEND_RESUME,
                                   FLTP_PEND_CALLING) == FLTP_PEND_CALLING)
    {
        return;
    }

    InterlockedExchange(&IrpCtrl->PostPendState, FLTP_PEND_NONE);
    FltpFinishNode(IrpCtrl, Node);

    if (!FltpRunPostCallbacks(IrpCtrl, 0))
    {
        return;
    }

    if (Irp == NULL)
    {
        return;
    }

    Irp->IoStatus = IrpCtrl->Data.IoStatus;
    FltpFreePostMdls(IrpCtrl);
    FltpReleaseNodes(IrpCtrl);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    FltpDereferenceIrpCtrl(IrpCtrl);
}
