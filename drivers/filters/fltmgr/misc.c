/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pool, export lookup, extra create parameters and waits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

PVOID
FLTAPI
FltAllocatePoolAlignedWithTag(
    _In_ PFLT_INSTANCE Instance,
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    UNREFERENCED_PARAMETER(Instance);

    return ExAllocatePoolWithTag(PoolType, max(NumberOfBytes, (SIZE_T)PAGE_SIZE), Tag);
}

VOID
FLTAPI
FltFreePoolAlignedWithTag(
    _In_ PFLT_INSTANCE Instance,
    _In_ PVOID Buffer,
    _In_ ULONG Tag)
{
    UNREFERENCED_PARAMETER(Instance);

    ExFreePoolWithTag(Buffer, Tag);
}

PVOID
FLTAPI
FltGetRoutineAddress(
    _In_ PCSTR FltMgrRoutineName)
{
    PVOID Base = FltGlobals.DriverObject->DriverStart;
    PIMAGE_EXPORT_DIRECTORY Exports;
    PULONG Names, Functions;
    PUSHORT Ordinals;
    ULONG Size, Index;

    Exports = RtlImageDirectoryEntryToData(Base, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &Size);
    if (Exports == NULL)
    {
        return NULL;
    }

    Names = (PULONG)((PUCHAR)Base + Exports->AddressOfNames);
    Ordinals = (PUSHORT)((PUCHAR)Base + Exports->AddressOfNameOrdinals);
    Functions = (PULONG)((PUCHAR)Base + Exports->AddressOfFunctions);

    for (Index = 0; Index < Exports->NumberOfNames; Index++)
    {
        if (strcmp((PCSTR)Base + Names[Index], FltMgrRoutineName) == 0)
        {
            if (Ordinals[Index] >= Exports->NumberOfFunctions)
            {
                return NULL;
            }
            return (PUCHAR)Base + Functions[Ordinals[Index]];
        }
    }

    return NULL;
}

NTSTATUS
FLTAPI
FltAllocateExtraCreateParameterList(
    _In_ PFLT_FILTER Filter,
    _In_ FSRTL_ALLOCATE_ECPLIST_FLAGS Flags,
    _Outptr_ PECP_LIST *EcpList)
{
    UNREFERENCED_PARAMETER(Filter);

    return FsRtlAllocateExtraCreateParameterList(Flags, EcpList);
}

NTSTATUS
FLTAPI
FltAllocateExtraCreateParameter(
    _In_ PFLT_FILTER Filter,
    _In_ LPCGUID EcpType,
    _In_ ULONG SizeOfContext,
    _In_ FSRTL_ALLOCATE_ECP_FLAGS Flags,
    _In_opt_ PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback,
    _In_ ULONG PoolTag,
    _Outptr_ PVOID *EcpContext)
{
    UNREFERENCED_PARAMETER(Filter);

    return FsRtlAllocateExtraCreateParameter(EcpType, SizeOfContext, Flags, CleanupCallback, PoolTag, EcpContext);
}

VOID
FLTAPI
FltInitExtraCreateParameterLookasideList(
    _In_ PFLT_FILTER Filter,
    _Inout_ PVOID Lookaside,
    _In_ FSRTL_ECP_LOOKASIDE_FLAGS Flags,
    _In_ SIZE_T Size,
    _In_ ULONG Tag)
{
    UNREFERENCED_PARAMETER(Filter);

    FsRtlInitExtraCreateParameterLookasideList(Lookaside, Flags, Size, Tag);
}

VOID
FLTAPI
FltDeleteExtraCreateParameterLookasideList(
    _In_ PFLT_FILTER Filter,
    _Inout_ PVOID Lookaside,
    _In_ FSRTL_ECP_LOOKASIDE_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Filter);

    FsRtlDeleteExtraCreateParameterLookasideList(Lookaside, Flags);
}

NTSTATUS
FLTAPI
FltAllocateExtraCreateParameterFromLookasideList(
    _In_ PFLT_FILTER Filter,
    _In_ LPCGUID EcpType,
    _In_ ULONG SizeOfContext,
    _In_ FSRTL_ALLOCATE_ECP_FLAGS Flags,
    _In_opt_ PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback,
    _Inout_ PVOID LookasideList,
    _Outptr_ PVOID *EcpContext)
{
    UNREFERENCED_PARAMETER(Filter);

    return FsRtlAllocateExtraCreateParameterFromLookasideList(EcpType,
                                                              SizeOfContext,
                                                              Flags,
                                                              CleanupCallback,
                                                              LookasideList,
                                                              EcpContext);
}

NTSTATUS
FLTAPI
FltInsertExtraCreateParameter(
    _In_ PFLT_FILTER Filter,
    _Inout_ PECP_LIST EcpList,
    _Inout_ PVOID EcpContext)
{
    UNREFERENCED_PARAMETER(Filter);

    return FsRtlInsertExtraCreateParameter(EcpList, EcpContext);
}

NTSTATUS
FLTAPI
FltFindExtraCreateParameter(
    _In_ PFLT_FILTER Filter,
    _In_ PECP_LIST EcpList,
    _In_ LPCGUID EcpType,
    _Outptr_opt_ PVOID *EcpContext,
    _Out_opt_ ULONG *EcpContextSize)
{
    UNREFERENCED_PARAMETER(Filter);

    return FsRtlFindExtraCreateParameter(EcpList, EcpType, EcpContext, EcpContextSize);
}

NTSTATUS
FLTAPI
FltRemoveExtraCreateParameter(
    _In_ PFLT_FILTER Filter,
    _Inout_ PECP_LIST EcpList,
    _In_ LPCGUID EcpType,
    _Outptr_ PVOID *EcpContext,
    _Out_opt_ ULONG *EcpContextSize)
{
    UNREFERENCED_PARAMETER(Filter);

    return FsRtlRemoveExtraCreateParameter(EcpList, EcpType, EcpContext, EcpContextSize);
}

VOID
FLTAPI
FltFreeExtraCreateParameterList(
    _In_ PFLT_FILTER Filter,
    _In_ PECP_LIST EcpList)
{
    UNREFERENCED_PARAMETER(Filter);

    FsRtlFreeExtraCreateParameterList(EcpList);
}

VOID
FLTAPI
FltFreeExtraCreateParameter(
    _In_ PFLT_FILTER Filter,
    _In_ PVOID EcpContext)
{
    UNREFERENCED_PARAMETER(Filter);

    FsRtlFreeExtraCreateParameter(EcpContext);
}

NTSTATUS
FLTAPI
FltGetNextExtraCreateParameter(
    _In_ PFLT_FILTER Filter,
    _In_ PECP_LIST EcpList,
    _In_opt_ PVOID CurrentEcpContext,
    _Out_opt_ LPGUID NextEcpType,
    _Outptr_opt_ PVOID *NextEcpContext,
    _Out_opt_ ULONG *NextEcpContextSize)
{
    UNREFERENCED_PARAMETER(Filter);

    return FsRtlGetNextExtraCreateParameter(EcpList,
                                            CurrentEcpContext,
                                            NextEcpType,
                                            NextEcpContext,
                                            NextEcpContextSize);
}

VOID
FLTAPI
FltAcknowledgeEcp(
    _In_ PFLT_FILTER Filter,
    _In_ PVOID EcpContext)
{
    UNREFERENCED_PARAMETER(Filter);

    FsRtlAcknowledgeEcp(EcpContext);
}

BOOLEAN
FLTAPI
FltIsEcpAcknowledged(
    _In_ PFLT_FILTER Filter,
    _In_ PVOID EcpContext)
{
    UNREFERENCED_PARAMETER(Filter);

    return FsRtlIsEcpAcknowledged(EcpContext);
}

BOOLEAN
FLTAPI
FltIsEcpFromUserMode(
    _In_ PFLT_FILTER Filter,
    _In_ PVOID EcpContext)
{
    UNREFERENCED_PARAMETER(Filter);

    return FsRtlIsEcpFromUserMode(EcpContext);
}

VOID
FLTAPI
FltPrepareToReuseEcp(
    _In_ PFLT_FILTER Filter,
    _In_ PVOID EcpContext)
{
    UNREFERENCED_PARAMETER(Filter);

    FsRtlPrepareToReuseEcp(EcpContext);
}

static
PIRP
FltpCreateIrp(
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    if (!FLT_IS_IRP_OPERATION(CallbackData) || CallbackData->Iopb->MajorFunction != IRP_MJ_CREATE)
    {
        return NULL;
    }

    return FLTP_DATA_TO_IRP_CTRL(CallbackData)->Irp;
}

NTSTATUS
FLTAPI
FltGetEcpListFromCallbackData(
    _In_ PFLT_FILTER Filter,
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _Outptr_result_maybenull_ PECP_LIST *EcpList)
{
    PIRP Irp = FltpCreateIrp(CallbackData);

    UNREFERENCED_PARAMETER(Filter);

    *EcpList = NULL;
    if (Irp == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return FsRtlGetEcpListFromIrp(Irp, EcpList);
}

NTSTATUS
FLTAPI
FltSetEcpListIntoCallbackData(
    _In_ PFLT_FILTER Filter,
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _In_ PECP_LIST EcpList)
{
    PIRP Irp = FltpCreateIrp(CallbackData);

    UNREFERENCED_PARAMETER(Filter);

    if (Irp == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return FsRtlSetEcpListIntoIrp(Irp, EcpList);
}

NTSTATUS
FLTAPI
FltCancellableWaitForMultipleObjects(
    _In_ ULONG Count,
    _In_reads_(Count) PVOID ObjectArray[],
    _In_ WAIT_TYPE WaitType,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_opt_ PKWAIT_BLOCK WaitBlockArray,
    _In_ PFLT_CALLBACK_DATA CallbackData)
{
    PIRP Irp = NULL;

    if (CallbackData != NULL && FLT_IS_IRP_OPERATION(CallbackData))
    {
        Irp = FLTP_DATA_TO_IRP_CTRL(CallbackData)->Irp;
    }

    return FsRtlCancellableWaitForMultipleObjects(Count, ObjectArray, WaitType, Timeout, WaitBlockArray, Irp);
}

NTSTATUS
FLTAPI
FltCancellableWaitForSingleObject(
    _In_ PVOID Object,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_opt_ PFLT_CALLBACK_DATA CallbackData)
{
    return FltCancellableWaitForMultipleObjects(1, &Object, WaitAny, Timeout, NULL, CallbackData);
}
