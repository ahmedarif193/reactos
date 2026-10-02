/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform stream callout test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndis.h>
#include <initguid.h>
#include <fwpsk.h>
#include <fwpmk.h>

#define NDEBUG
#include <debug.h>

#include "wfp_tcp.h"

#define MAX_EVENTS 48

DEFINE_GUID(TEST_SUBLAYER, 0x7c5a6f31, 0x2b4d, 0x4e8a, 0x91, 0x33, 0x5d, 0x0e, 0x61, 0xa2, 0xc4, 0x11);
DEFINE_GUID(TEST_FLOW_CALLOUT, 0x7c5a6f31, 0x2b4d, 0x4e8a, 0x91, 0x33, 0x5d, 0x0e, 0x61, 0xa2, 0xc4, 0x12);
DEFINE_GUID(TEST_STREAM_CALLOUT, 0x7c5a6f31, 0x2b4d, 0x4e8a, 0x91, 0x33, 0x5d, 0x0e, 0x61, 0xa2, 0xc4, 0x13);

typedef struct _WFP_EVENT
{
    UINT16 LayerId;
    UINT32 Direction;
    UINT32 LocalAddress;
    UINT32 RemoteAddress;
    UINT16 LocalPort;
    UINT16 RemotePort;
    UINT8 Protocol;
    UINT32 Flags;
    UINT32 Metadata;
    UINT32 MetadataFlags;
    UINT64 FlowHandle;
    UINT64 EndpointHandle;
    ULONG Compartment;
    KIRQL Irql;
    UINT64 FlowContext;
    UINT64 FilterContext;
    UINT32 FilterAction;
    UINT32 Rights;
    UINT32 Action;
    NTSTATUS AssociateStatus;
    BOOLEAN HasLayerData;
    BOOLEAN HasPath;
    UINT32 StreamFlags;
    SIZE_T DataLength;
    SIZE_T Copied;
    UCHAR Data[8];
    BOOLEAN HasChain;
    BOOLEAN HasOffset;
    SIZE_T StreamOffset;
    SIZE_T MissedBytes;
    UINT32 BytesRequired;
    SIZE_T BytesEnforced;
    UINT32 StreamAction;
    ULONG Values;
    BOOLEAN ValuesValid;
} WFP_EVENT, *PWFP_EVENT;

static KMT_MESSAGE_HANDLER TestMessageHandler;

static HANDLE EngineHandle;
static UINT32 FlowCalloutId, StreamCalloutId;
static WFP_EVENT Events[MAX_EVENTS];
static volatile LONG EventCount;
static volatile LONG FlowDeleteCount;
static volatile LONG FlowCount;
static UINT16 ClientPort;
static UINT64 FlowHandles[2];

static
BOOLEAN
IsNumber(
    _In_ const FWPS_INCOMING_VALUES0 *Values,
    _In_ ULONG Field,
    _In_ FWP_DATA_TYPE Type,
    _In_ UINT64 Expected)
{
    const FWP_VALUE0 *Value = &Values->incomingValue[Field].value;

    if (Field >= Values->valueCount || Value->type != Type)
    {
        return FALSE;
    }
    return Type == FWP_UINT8 ? Value->uint8 == Expected : Value->uint32 == Expected;
}

static
BOOLEAN
IsType(
    _In_ const FWPS_INCOMING_VALUES0 *Values,
    _In_ ULONG Field,
    _In_ FWP_DATA_TYPE Type)
{
    return Field < Values->valueCount && Values->incomingValue[Field].value.type == Type;
}

static
PWFP_EVENT
NewEvent(
    _In_ const FWPS_INCOMING_VALUES0 *Values,
    _In_ const FWPS_INCOMING_METADATA_VALUES0 *Metadata,
    _In_ const FWPS_FILTER3 *Filter,
    _In_ UINT64 FlowContext,
    _In_ const FWPS_CLASSIFY_OUT0 *ClassifyOut,
    _In_opt_ PVOID LayerData)
{
    LONG Index = InterlockedIncrement(&EventCount) - 1;
    PWFP_EVENT Event;

    if (Index >= MAX_EVENTS)
    {
        return NULL;
    }

    Event = &Events[Index];
    RtlZeroMemory(Event, sizeof(*Event));
    Event->LayerId = Values->layerId;
    Event->Values = Values->valueCount;
    Event->Metadata = Metadata->currentMetadataValues;
    Event->MetadataFlags = Metadata->flags;
    Event->FlowHandle = Metadata->flowHandle;
    Event->EndpointHandle = Metadata->transportEndpointHandle;
    Event->Compartment = Metadata->compartmentId;
    Event->Irql = KeGetCurrentIrql();
    Event->FlowContext = FlowContext;
    Event->FilterContext = Filter->context;
    Event->FilterAction = Filter->action.type;
    Event->Rights = ClassifyOut->rights;
    Event->Action = ClassifyOut->actionType;
    Event->HasLayerData = (LayerData != NULL);
    Event->HasPath = FWPS_IS_METADATA_FIELD_PRESENT(Metadata, FWPS_METADATA_FIELD_PROCESS_PATH) &&
                     Metadata->processPath != NULL && Metadata->processPath->size != 0;
    return Event;
}

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
    PWFP_EVENT Event = NewEvent(Values, Metadata, Filter, FlowContext, ClassifyOut, LayerData);

    UNREFERENCED_PARAMETER(ClassifyContext);

    if (Event != NULL)
    {
        Event->Direction = Values->incomingValue[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_DIRECTION].value.uint32;
        Event->LocalAddress = Values->incomingValue[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_ADDRESS].value.uint32;
        Event->RemoteAddress = Values->incomingValue[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_REMOTE_ADDRESS].value.uint32;
        Event->LocalPort = Values->incomingValue[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_PORT].value.uint16;
        Event->RemotePort = Values->incomingValue[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_REMOTE_PORT].value.uint16;
        Event->Protocol = Values->incomingValue[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_PROTOCOL].value.uint8;
        Event->Flags = Values->incomingValue[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_FLAGS].value.uint32;
        if (FWPS_IS_METADATA_FIELD_PRESENT(Metadata, FWPS_METADATA_FIELD_FLOW_HANDLE))
        {
            Event->AssociateStatus = FwpsFlowAssociateContext0(Metadata->flowHandle,
                                                               FWPS_LAYER_STREAM_V4,
                                                               StreamCalloutId,
                                                               0x2000 + Event->RemotePort);
            if (NT_SUCCESS(Event->AssociateStatus))
            {
                InterlockedIncrement(&FlowCount);
            }
        }
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
    PWFP_EVENT Event = NewEvent(Values, Metadata, Filter, FlowContext, ClassifyOut, LayerData);
    FWPS_STREAM_CALLOUT_IO_PACKET0 *Packet = LayerData;
    FWPS_STREAM_DATA0 *Data;

    UNREFERENCED_PARAMETER(ClassifyContext);

    if (Event != NULL)
    {
        Event->Direction = Values->incomingValue[FWPS_FIELD_STREAM_V4_DIRECTION].value.uint32;
        Event->LocalAddress = Values->incomingValue[FWPS_FIELD_STREAM_V4_IP_LOCAL_ADDRESS].value.uint32;
        Event->RemoteAddress = Values->incomingValue[FWPS_FIELD_STREAM_V4_IP_REMOTE_ADDRESS].value.uint32;
        Event->LocalPort = Values->incomingValue[FWPS_FIELD_STREAM_V4_IP_LOCAL_PORT].value.uint16;
        Event->RemotePort = Values->incomingValue[FWPS_FIELD_STREAM_V4_IP_REMOTE_PORT].value.uint16;
        Event->Flags = Values->incomingValue[FWPS_FIELD_STREAM_V4_FLAGS].value.uint32;
        Event->ValuesValid = IsType(Values, FWPS_FIELD_STREAM_V4_IP_LOCAL_ADDRESS, FWP_UINT32) &&
                             IsNumber(Values, FWPS_FIELD_STREAM_V4_IP_LOCAL_ADDRESS_TYPE, FWP_UINT8, 1) &&
                             IsType(Values, FWPS_FIELD_STREAM_V4_IP_REMOTE_ADDRESS, FWP_UINT32) &&
                             IsType(Values, FWPS_FIELD_STREAM_V4_IP_LOCAL_PORT, FWP_UINT16) &&
                             IsType(Values, FWPS_FIELD_STREAM_V4_IP_REMOTE_PORT, FWP_UINT16) &&
                             IsType(Values, FWPS_FIELD_STREAM_V4_DIRECTION, FWP_UINT32) &&
                             IsNumber(Values, FWPS_FIELD_STREAM_V4_FLAGS, FWP_UINT32, FWP_CONDITION_FLAG_IS_LOOPBACK) &&
                             IsNumber(Values, FWPS_FIELD_STREAM_V4_COMPARTMENT_ID, FWP_UINT32, 1);
        if (Packet != NULL)
        {
            Event->MissedBytes = Packet->missedBytes;
            Event->BytesRequired = Packet->countBytesRequired;
            Event->BytesEnforced = Packet->countBytesEnforced;
            Event->StreamAction = Packet->streamAction;
            Data = Packet->streamData;
            if (Data != NULL)
            {
                Event->StreamFlags = Data->flags;
                Event->DataLength = Data->dataLength;
                Event->HasChain = (Data->netBufferListChain != NULL);
                Event->HasOffset = (Data->dataOffset.netBufferList != NULL);
                Event->StreamOffset = Data->dataOffset.streamDataOffset;
                if (Data->dataLength != 0)
                {
                    FwpsCopyStreamDataToBuffer0(Data, Event->Data, min(Data->dataLength, sizeof(Event->Data)), &Event->Copied);
                }
            }
        }
    }

    ClassifyOut->actionType = FWP_ACTION_CONTINUE;
}

static
NTSTATUS
NTAPI
CalloutNotify(
    _In_ FWPS_CALLOUT_NOTIFY_TYPE NotifyType,
    _In_ const GUID *FilterKey,
    _Inout_ FWPS_FILTER3 *Filter)
{
    UNREFERENCED_PARAMETER(NotifyType);
    UNREFERENCED_PARAMETER(FilterKey);
    UNREFERENCED_PARAMETER(Filter);

    return STATUS_SUCCESS;
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

    InterlockedIncrement(&FlowDeleteCount);
}

static
NTSTATUS
AddFilter(
    _In_ const GUID *LayerKey,
    _In_ const GUID *CalloutKey,
    _In_opt_ const GUID *PortField,
    _In_ UINT64 Context)
{
    FWPM_FILTER_CONDITION0 Conditions[2];
    FWPM_FILTER0 Filter;
    ULONG Count = 0;

    RtlZeroMemory(&Filter, sizeof(Filter));
    RtlZeroMemory(Conditions, sizeof(Conditions));
    Filter.layerKey = *LayerKey;
    Filter.displayData.name = L"LiberNT WFP stream test filter";
    Filter.displayData.description = L"LiberNT WFP stream test filter";
    Filter.action.type = FWP_ACTION_CALLOUT_INSPECTION;
    Filter.action.calloutKey = *CalloutKey;
    Filter.subLayerKey = TEST_SUBLAYER;
    Filter.weight.type = FWP_EMPTY;
    Filter.rawContext = Context;

    if (PortField != NULL)
    {
        Conditions[Count].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
        Conditions[Count].matchType = FWP_MATCH_EQUAL;
        Conditions[Count].conditionValue.type = FWP_UINT8;
        Conditions[Count].conditionValue.uint8 = IPPROTO_TCP;
        Count++;
        Conditions[Count].fieldKey = *PortField;
        Conditions[Count].matchType = FWP_MATCH_EQUAL;
        Conditions[Count].conditionValue.type = FWP_UINT16;
        Conditions[Count].conditionValue.uint16 = WFPTCP_SERVER_PORT;
        Count++;
    }
    Filter.numFilterConditions = Count;
    Filter.filterCondition = Conditions;

    return FwpmFilterAdd0(EngineHandle, &Filter, NULL, NULL);
}

static
NTSTATUS
AddCallout(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ const GUID *CalloutKey,
    _In_ const GUID *LayerKey,
    _In_ FWPS_CALLOUT_CLASSIFY_FN3 Classify,
    _In_ BOOLEAN Stream,
    _Out_ PUINT32 CalloutId)
{
    FWPM_CALLOUT0 Management;
    FWPS_CALLOUT3 Callout;
    NTSTATUS Status;

    RtlZeroMemory(&Callout, sizeof(Callout));
    Callout.calloutKey = *CalloutKey;
    Callout.classifyFn = Classify;
    Callout.notifyFn = CalloutNotify;
    if (Stream)
    {
        Callout.flowDeleteFn = FlowDelete;
        Callout.flags = FWP_CALLOUT_FLAG_CONDITIONAL_ON_FLOW;
    }

    Status = FwpsCalloutRegister3(DeviceObject, &Callout, CalloutId);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(&Management, sizeof(Management));
    Management.calloutKey = *CalloutKey;
    Management.displayData.name = L"LiberNT WFP stream test callout";
    Management.displayData.description = L"LiberNT WFP stream test callout";
    Management.applicableLayer = *LayerKey;
    return FwpmCalloutAdd0(EngineHandle, &Management, NULL, NULL);
}

static
VOID
TestSetup(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    FWPM_SUBLAYER0 SubLayer;
    FWPM_SESSION0 Session;
    NTSTATUS Status;

    RtlZeroMemory(&Session, sizeof(Session));
    Session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    Status = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &Session, &EngineHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        EngineHandle = NULL;
        return;
    }

    Status = FwpmTransactionBegin0(EngineHandle, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(&SubLayer, sizeof(SubLayer));
    SubLayer.subLayerKey = TEST_SUBLAYER;
    SubLayer.displayData.name = L"LiberNT WFP stream test sublayer";
    SubLayer.displayData.description = L"LiberNT WFP stream test sublayer";
    SubLayer.weight = 0x100;
    Status = FwpmSubLayerAdd0(EngineHandle, &SubLayer, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = AddCallout(DeviceObject, &TEST_FLOW_CALLOUT, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, FlowClassify, FALSE, &FlowCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddCallout(DeviceObject, &TEST_STREAM_CALLOUT, &FWPM_LAYER_STREAM_V4, StreamClassify, TRUE, &StreamCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = AddFilter(&FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, &TEST_FLOW_CALLOUT, &FWPM_CONDITION_IP_REMOTE_PORT, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddFilter(&FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, &TEST_FLOW_CALLOUT, &FWPM_CONDITION_IP_LOCAL_PORT, 2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddFilter(&FWPM_LAYER_STREAM_V4, &TEST_STREAM_CALLOUT, NULL, 3);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = FwpmTransactionCommit0(EngineHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
BOOLEAN
CheckCommon(
    _In_ PCSTR Phase,
    _In_ LONG Index,
    _In_ UINT16 LayerId,
    _In_ UINT32 Direction,
    _In_ UINT16 LocalPort,
    _In_ UINT16 RemotePort,
    _In_ UINT64 FilterContext,
    _In_ UINT32 Required)
{
    PWFP_EVENT Event = &Events[Index];

    if (Index >= min(EventCount, MAX_EVENTS))
    {
        ok(FALSE, "%s[%ld]: missing event\n", Phase, Index);
        return FALSE;
    }

    ok(Event->LayerId == LayerId && Event->Direction == Direction,
       "%s[%ld]: layer %u direction %lu\n", Phase, Index, Event->LayerId, Event->Direction);
    ok(Event->LocalAddress == INADDR_LOOPBACK && Event->RemoteAddress == INADDR_LOOPBACK,
       "%s[%ld]: addresses %08lx %08lx\n", Phase, Index, Event->LocalAddress, Event->RemoteAddress);
    ok(Event->LocalPort == LocalPort && Event->RemotePort == RemotePort,
       "%s[%ld]: ports %u %u, expected %u %u\n", Phase, Index, Event->LocalPort, Event->RemotePort, LocalPort, RemotePort);
    ok(Event->Flags == FWP_CONDITION_FLAG_IS_LOOPBACK, "%s[%ld]: flags %08lx\n", Phase, Index, Event->Flags);
    ok((Event->Metadata & Required) == Required, "%s[%ld]: metadata %08lx\n", Phase, Index, Event->Metadata);
    ok(Event->FlowHandle != 0 && Event->Compartment == 1,
       "%s[%ld]: flow %I64x compartment %lu\n", Phase, Index, Event->FlowHandle, Event->Compartment);
    ok(Event->Irql <= DISPATCH_LEVEL, "%s[%ld]: IRQL %u\n", Phase, Index, Event->Irql);
    ok(Event->FilterContext == FilterContext && Event->FilterAction == FWP_ACTION_CALLOUT_INSPECTION,
       "%s[%ld]: filter %I64u action %lx\n", Phase, Index, Event->FilterContext, Event->FilterAction);
    ok(Event->Rights == FWPS_RIGHT_ACTION_WRITE && Event->Action == 0,
       "%s[%ld]: rights %lx action %lx\n", Phase, Index, Event->Rights, Event->Action);
    return TRUE;
}

static
VOID
CheckFlow(
    _In_ LONG Index,
    _In_ UINT32 Direction,
    _In_ UINT16 LocalPort,
    _In_ UINT16 RemotePort,
    _In_ UINT64 FilterContext)
{
    PWFP_EVENT Event = &Events[Index];

    if (!CheckCommon("connect",
                     Index,
                     FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4,
                     Direction,
                     LocalPort,
                     RemotePort,
                     FilterContext,
                     FWPS_METADATA_FIELD_FLOW_HANDLE | FWPS_METADATA_FIELD_COMPARTMENT_ID |
                     FWPS_METADATA_FIELD_TRANSPORT_ENDPOINT_HANDLE | FWPS_METADATA_FIELD_PROCESS_ID |
                     FWPS_METADATA_FIELD_PROCESS_PATH))
    {
        return;
    }

    ok(Event->Values == FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_MAX && Event->Protocol == IPPROTO_TCP,
       "connect[%ld]: values %lu protocol %u\n", Index, Event->Values, Event->Protocol);
    ok(Event->EndpointHandle != 0 && Event->HasPath && Event->FlowContext == 0,
       "connect[%ld]: endpoint %I64x path %d context %I64x\n",
       Index, Event->EndpointHandle, Event->HasPath, Event->FlowContext);
    ok_eq_hex(Event->AssociateStatus, STATUS_SUCCESS);
}

static
VOID
CheckStream(
    _In_ PCSTR Phase,
    _In_ LONG Index,
    _In_ LONG FlowEvent,
    _In_ UINT32 StreamFlags,
    _In_ ULONG Length,
    _In_opt_ PCSTR Data)
{
    PWFP_EVENT Event = &Events[Index];
    UINT16 LocalPort = FlowEvent == 0 ? ClientPort : WFPTCP_SERVER_PORT;
    UINT16 RemotePort = FlowEvent == 0 ? WFPTCP_SERVER_PORT : ClientPort;

    if (!CheckCommon(Phase,
                     Index,
                     FWPS_LAYER_STREAM_V4,
                     FlowEvent == 0 ? FWP_DIRECTION_OUTBOUND : FWP_DIRECTION_INBOUND,
                     LocalPort,
                     RemotePort,
                     3,
                     FWPS_METADATA_FIELD_FLOW_HANDLE | FWPS_METADATA_FIELD_COMPARTMENT_ID))
    {
        return;
    }

    ok(Event->Values == FWPS_FIELD_STREAM_V4_MAX && Event->ValuesValid,
       "%s[%ld]: values %lu valid %d\n", Phase, Index, Event->Values, Event->ValuesValid);
    ok(Event->FlowHandle == FlowHandles[FlowEvent] && Event->FlowContext == 0x2000ULL + RemotePort,
       "%s[%ld]: flow %I64x context %I64x\n", Phase, Index, Event->FlowHandle, Event->FlowContext);
    ok(Event->HasLayerData && Event->StreamFlags == StreamFlags && Event->DataLength == Length,
       "%s[%ld]: packet %d stream flags %08lx length %lu\n",
       Phase, Index, Event->HasLayerData, Event->StreamFlags, (ULONG)Event->DataLength);
    ok(Event->MissedBytes == 0 && Event->BytesRequired == 0 && Event->BytesEnforced == 0 &&
       Event->StreamAction == FWPS_STREAM_ACTION_NONE && Event->StreamOffset == 0,
       "%s[%ld]: missed %lu required %lu enforced %lu action %lu offset %lu\n",
       Phase, Index, (ULONG)Event->MissedBytes, Event->BytesRequired, (ULONG)Event->BytesEnforced,
       Event->StreamAction, (ULONG)Event->StreamOffset);
    if (Data != NULL)
    {
        ok(Event->HasChain && Event->HasOffset && Event->Copied == 8 && RtlEqualMemory(Event->Data, Data, 8),
           "%s[%ld]: chain %d offset %d copied %lu data %.8s\n",
           Phase, Index, Event->HasChain, Event->HasOffset, (ULONG)Event->Copied, (PCSTR)Event->Data);
    }
}

static
VOID
TestConnected(VOID)
{
    ok_eq_long(EventCount, 2L);
    ClientPort = Events[0].LocalPort;
    FlowHandles[0] = Events[0].FlowHandle;
    FlowHandles[1] = Events[1].FlowHandle;
    CheckFlow(0, FWP_DIRECTION_OUTBOUND, ClientPort, WFPTCP_SERVER_PORT, 1);
    CheckFlow(1, FWP_DIRECTION_INBOUND, WFPTCP_SERVER_PORT, ClientPort, 2);
    ok(FlowHandles[0] != FlowHandles[1] && Events[0].EndpointHandle != Events[1].EndpointHandle,
       "Flows %I64x %I64x\n", FlowHandles[0], FlowHandles[1]);
    ok_eq_long(FlowCount, 2L);
    trace("metadata %08lx/%08lx, IRQL %u\n", Events[0].Metadata, Events[0].MetadataFlags, Events[0].Irql);
    EventCount = 0;
}

static
VOID
TestExchanged(VOID)
{
    ok_eq_long(EventCount, 6L);
    CheckStream("exchange", 0, 0, FWPS_STREAM_FLAG_SEND, 8, "HELLO001");
    CheckStream("exchange", 1, 1, FWPS_STREAM_FLAG_RECEIVE | FWPS_STREAM_FLAG_RECEIVE_PUSH, 8, "HELLO001");
    CheckStream("exchange", 2, 1, FWPS_STREAM_FLAG_SEND, 17, "REPLY001");
    CheckStream("exchange", 3, 0, FWPS_STREAM_FLAG_RECEIVE | FWPS_STREAM_FLAG_RECEIVE_PUSH, 17, "REPLY001");
    CheckStream("exchange", 4, 0, FWPS_STREAM_FLAG_SEND, 8, "HELLO002");
    CheckStream("exchange", 5, 1, FWPS_STREAM_FLAG_RECEIVE | FWPS_STREAM_FLAG_RECEIVE_PUSH, 8, "HELLO002");
    trace("metadata %08lx/%08lx, IRQL %u\n", Events[0].Metadata, Events[0].MetadataFlags, Events[0].Irql);
    EventCount = 0;
}

static
VOID
TestShutdown(VOID)
{
    ok_eq_long(EventCount, 4L);
    CheckStream("shutdown", 0, 0, FWPS_STREAM_FLAG_SEND | FWPS_STREAM_FLAG_SEND_DISCONNECT, 0, NULL);
    CheckStream("shutdown", 1, 1, FWPS_STREAM_FLAG_RECEIVE | FWPS_STREAM_FLAG_RECEIVE_DISCONNECT, 0, NULL);
    CheckStream("shutdown", 2, 1, FWPS_STREAM_FLAG_SEND | FWPS_STREAM_FLAG_SEND_DISCONNECT, 0, NULL);
    CheckStream("shutdown", 3, 0, FWPS_STREAM_FLAG_RECEIVE | FWPS_STREAM_FLAG_RECEIVE_DISCONNECT, 0, NULL);
    ok_eq_long(FlowDeleteCount, 0L);
    EventCount = 0;
}

static
VOID
TestClosed(VOID)
{
    LARGE_INTEGER Delay;
    ULONG Tries;

    Delay.QuadPart = -10 * 1000 * 10;
    for (Tries = 0; Tries < 300 && FlowDeleteCount != FlowCount; Tries++)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    ok_eq_long(EventCount, 0L);
    ok_eq_long(FlowDeleteCount, 2L);
}

static
VOID
TestTeardown(VOID)
{
    NTSTATUS Status;

    if (EngineHandle == NULL)
    {
        return;
    }

    Status = FwpmEngineClose0(EngineHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    EngineHandle = NULL;

    Status = FwpsCalloutUnregisterById0(StreamCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = FwpsCalloutUnregisterById0(FlowCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);
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

    *DeviceName = L"WfpTcp";
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);

    TestTeardown();
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
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    switch (ControlCode)
    {
        case IOCTL_WFPTCP_SETUP:
            TestSetup(DeviceObject);
            break;

        case IOCTL_WFPTCP_CONNECTED:
            TestConnected();
            break;

        case IOCTL_WFPTCP_EXCHANGED:
            TestExchanged();
            break;

        case IOCTL_WFPTCP_SHUTDOWN:
            TestShutdown();
            break;

        case IOCTL_WFPTCP_CLOSED:
            TestClosed();
            break;

        case IOCTL_WFPTCP_TEARDOWN:
            TestTeardown();
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}
