/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform user-mode management test, callout driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndis.h>
#include <initguid.h>
#include <fwpsk.h>
#include <fwpmk.h>

#define NDEBUG
#include <debug.h>

#include "wfp_user.h"

static KMT_MESSAGE_HANDLER TestMessageHandler;

static WFPUSER_STATE State;
static BOOLEAN Registered[2];

static
VOID
NTAPI
FlowClassify(
    _In_ const FWPS_INCOMING_VALUES0 *Values,
    _In_ const FWPS_INCOMING_METADATA_VALUES0 *Metadata,
    _Inout_opt_ PVOID LayerData,
    _In_opt_ const void *ClassifyContext,
    _In_ const FWPS_FILTER3 *Filter,
    _In_ UINT64 FlowContext,
    _Inout_ FWPS_CLASSIFY_OUT0 *ClassifyOut)
{
    UNREFERENCED_PARAMETER(Values);
    UNREFERENCED_PARAMETER(LayerData);
    UNREFERENCED_PARAMETER(ClassifyContext);
    UNREFERENCED_PARAMETER(FlowContext);

    if (Filter->context == WFPUSER_CONTEXT_OWN)
    {
        InterlockedIncrement(&State.FlowClassify[0]);
        if (FWPS_IS_METADATA_FIELD_PRESENT(Metadata, FWPS_METADATA_FIELD_PROCESS_PATH) &&
            Metadata->processPath != NULL)
        {
            State.PathSize = Metadata->processPath->size;
            RtlCopyMemory(State.Path, Metadata->processPath->data, min(State.PathSize, sizeof(State.Path)));
        }
        if (FWPS_IS_METADATA_FIELD_PRESENT(Metadata, FWPS_METADATA_FIELD_FLOW_HANDLE) &&
            NT_SUCCESS(FwpsFlowAssociateContext0(Metadata->flowHandle,
                                                 FWPS_LAYER_STREAM_V4,
                                                 State.CalloutId[WFPUSER_STREAM],
                                                 WFPUSER_CONTEXT_STREAM)))
        {
            InterlockedIncrement(&State.Associated);
        }
    }
    else if (Filter->context == WFPUSER_CONTEXT_OTHER)
    {
        InterlockedIncrement(&State.FlowClassify[1]);
    }
    else
    {
        InterlockedIncrement(&State.FlowClassify[2]);
    }

    ClassifyOut->actionType = FWP_ACTION_PERMIT;
    if (Filter->flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT)
    {
        ClassifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
    }
}

static
VOID
NTAPI
StreamClassify(
    _In_ const FWPS_INCOMING_VALUES0 *Values,
    _In_ const FWPS_INCOMING_METADATA_VALUES0 *Metadata,
    _Inout_opt_ PVOID LayerData,
    _In_opt_ const void *ClassifyContext,
    _In_ const FWPS_FILTER3 *Filter,
    _In_ UINT64 FlowContext,
    _Inout_ FWPS_CLASSIFY_OUT0 *ClassifyOut)
{
    FWPS_STREAM_CALLOUT_IO_PACKET0 *Packet = LayerData;

    UNREFERENCED_PARAMETER(Values);
    UNREFERENCED_PARAMETER(Metadata);
    UNREFERENCED_PARAMETER(ClassifyContext);

    if (Filter->context == WFPUSER_CONTEXT_STREAM && FlowContext == WFPUSER_CONTEXT_STREAM &&
        Packet != NULL && Packet->streamData != NULL && Packet->streamData->dataLength != 0)
    {
        InterlockedIncrement(&State.StreamClassify);
        InterlockedExchangeAdd(&State.StreamBytes[(Packet->streamData->flags & FWPS_STREAM_FLAG_RECEIVE) ? 1 : 0],
                               (LONG)Packet->streamData->dataLength);
    }

    ClassifyOut->actionType = FWP_ACTION_CONTINUE;
}

static
NTSTATUS
Notify(
    _In_ ULONG Index,
    _In_ FWPS_CALLOUT_NOTIFY_TYPE NotifyType,
    _In_ FWPS_FILTER3 *Filter)
{
    if (NotifyType == FWPS_CALLOUT_NOTIFY_ADD_FILTER)
    {
        State.NotifyAction[Index] = Filter->action.type;
        InterlockedExchangeAdd(&State.NotifyContext[Index], (LONG)Filter->context);
        InterlockedIncrement(&State.NotifyAdd[Index]);
    }
    else if (NotifyType == FWPS_CALLOUT_NOTIFY_DELETE_FILTER)
    {
        InterlockedIncrement(&State.NotifyDelete[Index]);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
FlowNotify(
    _In_ FWPS_CALLOUT_NOTIFY_TYPE NotifyType,
    _In_ const GUID *FilterKey,
    _Inout_ FWPS_FILTER3 *Filter)
{
    UNREFERENCED_PARAMETER(FilterKey);

    return Notify(WFPUSER_FLOW, NotifyType, Filter);
}

static
NTSTATUS
NTAPI
StreamNotify(
    _In_ FWPS_CALLOUT_NOTIFY_TYPE NotifyType,
    _In_ const GUID *FilterKey,
    _Inout_ FWPS_FILTER3 *Filter)
{
    UNREFERENCED_PARAMETER(FilterKey);

    return Notify(WFPUSER_STREAM, NotifyType, Filter);
}

static
VOID
NTAPI
FlowDelete(
    _In_ UINT16 LayerId,
    _In_ UINT32 CalloutId,
    _In_ UINT64 FlowContext)
{
    UNREFERENCED_PARAMETER(LayerId);
    UNREFERENCED_PARAMETER(CalloutId);
    UNREFERENCED_PARAMETER(FlowContext);

    InterlockedIncrement(&State.FlowDelete);
}

static
VOID
Register(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    FWPS_CALLOUT3 Callout;

    RtlZeroMemory(&State, sizeof(State));

    RtlZeroMemory(&Callout, sizeof(Callout));
    Callout.calloutKey = WFPUSER_FLOW_CALLOUT;
    Callout.classifyFn = FlowClassify;
    Callout.notifyFn = FlowNotify;
    State.Status[WFPUSER_FLOW] = FwpsCalloutRegister3(DeviceObject, &Callout, &State.CalloutId[WFPUSER_FLOW]);
    Registered[WFPUSER_FLOW] = NT_SUCCESS(State.Status[WFPUSER_FLOW]);

    RtlZeroMemory(&Callout, sizeof(Callout));
    Callout.calloutKey = WFPUSER_STREAM_CALLOUT;
    Callout.classifyFn = StreamClassify;
    Callout.notifyFn = StreamNotify;
    Callout.flowDeleteFn = FlowDelete;
    Callout.flags = FWP_CALLOUT_FLAG_CONDITIONAL_ON_FLOW;
    State.Status[WFPUSER_STREAM] = FwpsCalloutRegister3(DeviceObject, &Callout, &State.CalloutId[WFPUSER_STREAM]);
    Registered[WFPUSER_STREAM] = NT_SUCCESS(State.Status[WFPUSER_STREAM]);
}

static
VOID
Unregister(VOID)
{
    LARGE_INTEGER Delay;
    ULONG Tries, Index;

    Delay.QuadPart = -10 * 1000 * 10;
    for (Tries = 0; Tries < 300 && State.FlowDelete != State.Associated; Tries++)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    for (Index = WFPUSER_STREAM + 1; Index-- > 0;)
    {
        if (Registered[Index])
        {
            State.Status[Index] = FwpsCalloutUnregisterById0(State.CalloutId[Index]);
            Registered[Index] = !NT_SUCCESS(State.Status[Index]);
        }
    }
}

NTSTATUS
TestEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PCUNICODE_STRING RegistryPath,
    OUT PCWSTR *DeviceName,
    IN OUT INT *Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(Flags);

    *DeviceName = L"WfpUser";
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);

    Unregister();
}

static
NTSTATUS
TestMessageHandler(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(InLength);

    if (Buffer == NULL || *OutLength < sizeof(State))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    switch (ControlCode)
    {
        case IOCTL_WFPUSER_REGISTER:
            Register(DeviceObject);
            break;

        case IOCTL_WFPUSER_QUERY:
            break;

        case IOCTL_WFPUSER_UNREGISTER:
            Unregister();
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    RtlCopyMemory(Buffer, &State, sizeof(State));
    *OutLength = sizeof(State);
    return STATUS_SUCCESS;
}
