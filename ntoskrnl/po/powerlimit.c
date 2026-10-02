/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power limit requests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <initguid.h>
#include <wdmguid.h>
#define NDEBUG
#include <debug.h>

#define TAG_PO_POWER_LIMIT 'LPoP'
#define POP_POWER_LIMIT_SIGNATURE 'qRLP'

typedef struct _POP_POWER_LIMIT_REQUEST
{
    ULONG Signature;
    PDEVICE_OBJECT TargetDeviceObject;
    PDEVICE_OBJECT PolicyDeviceObject;
    POWER_LIMIT_INTERFACE Interface;
} POP_POWER_LIMIT_REQUEST, *PPOP_POWER_LIMIT_REQUEST;

NTSTATUS
NTAPI
PoCreatePowerLimitRequest(
    _Outptr_ PVOID *PowerLimitRequest,
    _In_ PDEVICE_OBJECT TargetDeviceObject,
    _In_ PDEVICE_OBJECT PolicyDeviceObject,
    _In_ PCOUNTED_REASON_CONTEXT Context)
{
    PPOP_POWER_LIMIT_REQUEST Request;
    IO_STATUS_BLOCK IoStatusBlock;
    IO_STACK_LOCATION Stack;
    NTSTATUS Status;
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Context);

    if (PowerLimitRequest == NULL || TargetDeviceObject == NULL || PolicyDeviceObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *PowerLimitRequest = NULL;

    Request = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Request), TAG_PO_POWER_LIMIT);
    if (Request == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Request, sizeof(*Request));
    RtlZeroMemory(&Stack, sizeof(Stack));
    Stack.Parameters.QueryInterface.InterfaceType = &GUID_POWER_LIMIT_INTERFACE;
    Stack.Parameters.QueryInterface.Size = sizeof(POWER_LIMIT_INTERFACE);
    Stack.Parameters.QueryInterface.Version = 1;
    Stack.Parameters.QueryInterface.Interface = (PINTERFACE)&Request->Interface;

    Status = IopInitiatePnpIrp(TargetDeviceObject, &IoStatusBlock, IRP_MN_QUERY_INTERFACE, &Stack);
    if (NT_SUCCESS(Status) &&
        (Request->Interface.QueryAttributes == NULL ||
         Request->Interface.SetPowerLimit == NULL ||
         Request->Interface.QueryPowerLimit == NULL))
    {
        if (Request->Interface.InterfaceDereference != NULL)
        {
            Request->Interface.InterfaceDereference(Request->Interface.Context);
        }
        Status = STATUS_NOT_SUPPORTED;
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Request, TAG_PO_POWER_LIMIT);
        return Status;
    }

    ObReferenceObject(TargetDeviceObject);
    ObReferenceObject(PolicyDeviceObject);
    Request->Signature = POP_POWER_LIMIT_SIGNATURE;
    Request->TargetDeviceObject = TargetDeviceObject;
    Request->PolicyDeviceObject = PolicyDeviceObject;
    *PowerLimitRequest = Request;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PoQueryPowerLimitAttributes(
    _Inout_ PVOID PowerLimitRequest,
    _In_ ULONG BufferCount,
    _Inout_updates_opt_(BufferCount) PPOWER_LIMIT_ATTRIBUTES Buffer,
    _Out_ PULONG AttributeCount)
{
    PPOP_POWER_LIMIT_REQUEST Request = PowerLimitRequest;
    PAGED_CODE();

    if (Request == NULL || Request->Signature != POP_POWER_LIMIT_SIGNATURE || AttributeCount == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return Request->Interface.QueryAttributes(Request->Interface.Context, BufferCount, Buffer, AttributeCount);
}

NTSTATUS
NTAPI
PoSetPowerLimitValue(
    _Inout_ PVOID PowerLimitRequest,
    _In_opt_ PCOUNTED_REASON_CONTEXT Reason,
    _In_ ULONG ValueCount,
    _In_reads_(ValueCount) PPOWER_LIMIT_VALUE Values)
{
    PPOP_POWER_LIMIT_REQUEST Request = PowerLimitRequest;
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Reason);

    if (Request == NULL || Request->Signature != POP_POWER_LIMIT_SIGNATURE || (ValueCount != 0 && Values == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return Request->Interface.SetPowerLimit(Request->Interface.Context, ValueCount, Values);
}

NTSTATUS
NTAPI
PoQueryPowerLimitValue(
    _Inout_ PVOID PowerLimitRequest,
    _In_ ULONG ValueCount,
    _Inout_updates_(ValueCount) PPOWER_LIMIT_VALUE Values)
{
    PPOP_POWER_LIMIT_REQUEST Request = PowerLimitRequest;
    PAGED_CODE();

    if (Request == NULL || Request->Signature != POP_POWER_LIMIT_SIGNATURE || (ValueCount != 0 && Values == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return Request->Interface.QueryPowerLimit(Request->Interface.Context, ValueCount, Values);
}

NTSTATUS
NTAPI
PoDeletePowerLimitRequest(
    _Inout_ PVOID PowerLimitRequest)
{
    PPOP_POWER_LIMIT_REQUEST Request = PowerLimitRequest;
    PAGED_CODE();

    if (Request == NULL || Request->Signature != POP_POWER_LIMIT_SIGNATURE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Request->Signature = 0;
    if (Request->Interface.InterfaceDereference != NULL)
    {
        Request->Interface.InterfaceDereference(Request->Interface.Context);
    }
    ObDereferenceObject(Request->PolicyDeviceObject);
    ObDereferenceObject(Request->TargetDeviceObject);
    ExFreePoolWithTag(Request, TAG_PO_POWER_LIMIT);
    return STATUS_SUCCESS;
}
