/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform data flows and transport layer classification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wfp.h"

#define WFP_COMPARTMENT_ID 1
#define WFP_ADDRESS_TYPE_UNICAST 1

typedef struct _WFP_DATAGRAM_DATA
{
    const WFP_SHIM_DATAGRAM *Datagram;
    PWFP_PACKET Packet;
} WFP_DATAGRAM_DATA, *PWFP_DATAGRAM_DATA;

typedef struct _WFP_STREAM_DATA
{
    FWPS_STREAM_CALLOUT_IO_PACKET0 IoPacket;
    FWPS_STREAM_DATA0 Data;
    const VOID *Bytes;
    ULONG Length;
    PWFP_PACKET Packet;
} WFP_STREAM_DATA, *PWFP_STREAM_DATA;

static LIST_ENTRY WfpFlows = { &WfpFlows, &WfpFlows };
static UINT64 WfpNextFlowId;

static
PWFP_FLOW
WfpFindFlowByIdLocked(
    _In_ UINT64 FlowId)
{
    PWFP_FLOW Flow;
    PLIST_ENTRY Entry;

    for (Entry = WfpFlows.Flink; Entry != &WfpFlows; Entry = Entry->Flink)
    {
        Flow = CONTAINING_RECORD(Entry, WFP_FLOW, Link);
        if (Flow->Id == FlowId)
        {
            return Flow;
        }
    }
    return NULL;
}

static
PWFP_FLOW_CONTEXT
WfpFindContextLocked(
    _In_ PWFP_FLOW Flow,
    _In_ UINT16 LayerId,
    _In_ PWFP_CALLOUT Callout)
{
    PWFP_FLOW_CONTEXT FlowContext;
    PLIST_ENTRY Entry;

    for (Entry = Flow->Contexts.Flink; Entry != &Flow->Contexts; Entry = Entry->Flink)
    {
        FlowContext = CONTAINING_RECORD(Entry, WFP_FLOW_CONTEXT, Link);
        if (FlowContext->LayerId == LayerId && FlowContext->Callout == Callout)
        {
            return FlowContext;
        }
    }
    return NULL;
}

PWFP_FLOW_CONTEXT
WfpFindFlowContextLocked(
    _In_ UINT64 FlowId,
    _In_ UINT16 LayerId,
    _In_ PWFP_CALLOUT Callout)
{
    PWFP_FLOW Flow;

    if (FlowId == 0)
    {
        return NULL;
    }

    Flow = WfpFindFlowByIdLocked(FlowId);
    return Flow != NULL ? WfpFindContextLocked(Flow, LayerId, Callout) : NULL;
}

VOID
WfpDeleteFlowContext(
    _In_ PWFP_FLOW_CONTEXT FlowContext)
{
    PWFP_CALLOUT Callout = FlowContext->Callout;

    if (Callout->FlowDeleteFn != NULL)
    {
        Callout->FlowDeleteFn(FlowContext->LayerId, Callout->Id, FlowContext->Context);
    }
    InterlockedDecrement(&Callout->FlowContexts);
    WfpDereferenceCallout(Callout);
    ExFreePoolWithTag(FlowContext, WFP_TAG);
}

NTSTATUS
NTAPI
FwpsFlowAssociateContext0(
    _In_ UINT64 flowId,
    _In_ UINT16 layerId,
    _In_ UINT32 calloutId,
    _In_ UINT64 flowContext)
{
    PWFP_FLOW_CONTEXT FlowContext;
    PWFP_CALLOUT Callout;
    PWFP_FLOW Flow;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    if (flowContext == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FlowContext = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*FlowContext), WFP_TAG);
    if (FlowContext == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(FlowContext, sizeof(*FlowContext));
    FlowContext->LayerId = layerId;
    FlowContext->Context = flowContext;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Callout = WfpFindCalloutById(calloutId);
    if (Callout == NULL || !Callout->Registered || Callout->FlowDeleteFn == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if ((Flow = WfpFindFlowByIdLocked(flowId)) == NULL)
    {
        Status = STATUS_NOT_FOUND;
    }
    else if (WfpFindContextLocked(Flow, layerId, Callout) != NULL)
    {
        Status = STATUS_OBJECT_NAME_EXISTS;
    }
    else
    {
        FlowContext->Callout = Callout;
        Callout->References++;
        InterlockedIncrement(&Callout->FlowContexts);
        InsertTailList(&Flow->Contexts, &FlowContext->Link);
        FlowContext = NULL;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (FlowContext != NULL)
    {
        ExFreePoolWithTag(FlowContext, WFP_TAG);
    }
    return Status;
}

NTSTATUS
NTAPI
FwpsFlowRemoveContext0(
    _In_ UINT64 flowId,
    _In_ UINT16 layerId,
    _In_ UINT32 calloutId)
{
    PWFP_FLOW_CONTEXT FlowContext = NULL;
    PWFP_CALLOUT Callout;
    PWFP_FLOW Flow;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Flow = WfpFindFlowByIdLocked(flowId);
    Callout = WfpFindCalloutById(calloutId);
    if (Flow == NULL)
    {
        Status = STATUS_UNSUCCESSFUL;
    }
    else if (Callout != NULL && (FlowContext = WfpFindContextLocked(Flow, layerId, Callout)) != NULL)
    {
        RemoveEntryList(&FlowContext->Link);
        if (FlowContext->Active != 0)
        {
            FlowContext->Removed = TRUE;
            FlowContext = NULL;
            Status = STATUS_PENDING;
        }
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (FlowContext != NULL)
    {
        WfpDeleteFlowContext(FlowContext);
    }
    return Status;
}

static
VOID
WfpDeleteFlows(
    _In_ ULONG64 EndpointId)
{
    PWFP_FLOW_CONTEXT FlowContext, Removed = NULL;
    PLIST_ENTRY Entry, NextEntry, ContextEntry;
    LIST_ENTRY FreeList;
    PWFP_FLOW Flow;
    KIRQL OldIrql;

    InitializeListHead(&FreeList);

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    for (Entry = WfpFlows.Flink; Entry != &WfpFlows; Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        Flow = CONTAINING_RECORD(Entry, WFP_FLOW, Link);
        if (Flow->EndpointId != EndpointId)
        {
            continue;
        }

        while (!IsListEmpty(&Flow->Contexts))
        {
            ContextEntry = RemoveHeadList(&Flow->Contexts);
            FlowContext = CONTAINING_RECORD(ContextEntry, WFP_FLOW_CONTEXT, Link);
            if (FlowContext->Active != 0)
            {
                FlowContext->Removed = TRUE;
            }
            else
            {
                FlowContext->Next = Removed;
                Removed = FlowContext;
            }
        }
        RemoveEntryList(&Flow->Link);
        InsertTailList(&FreeList, &Flow->Link);
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    while (Removed != NULL)
    {
        FlowContext = Removed;
        Removed = FlowContext->Next;
        WfpDeleteFlowContext(FlowContext);
    }

    while (!IsListEmpty(&FreeList))
    {
        ExFreePoolWithTag(RemoveHeadList(&FreeList), WFP_TAG);
    }
}

VOID
NTAPI
WfpShimEndpointClosed(
    _In_ ULONG64 EndpointId)
{
    if (!IsListEmpty(&WfpFlows))
    {
        WfpDeleteFlows(EndpointId);
    }
}

BOOLEAN
NTAPI
WfpShimDatagramActive(VOID)
{
    return WfpLayerFilters[FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4] != 0 ||
           WfpLayerFilters[FWPS_LAYER_DATAGRAM_DATA_V4] != 0;
}

static
UINT64
WfpGetFlow(
    _In_ const WFP_SHIM_FLOW *Datagram,
    _Out_ PBOOLEAN Created)
{
    PWFP_FLOW Flow, New;
    PLIST_ENTRY Entry;
    UINT64 FlowId = 0;
    KIRQL OldIrql;

    *Created = FALSE;
    New = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*New), WFP_TAG);

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    for (Entry = WfpFlows.Flink; Entry != &WfpFlows; Entry = Entry->Flink)
    {
        Flow = CONTAINING_RECORD(Entry, WFP_FLOW, Link);
        if (Flow->EndpointId == Datagram->EndpointId &&
            Flow->RemoteAddress == Datagram->RemoteAddress &&
            Flow->RemotePort == Datagram->RemotePort &&
            Flow->LocalAddress == Datagram->LocalAddress &&
            Flow->LocalPort == Datagram->LocalPort &&
            Flow->Protocol == Datagram->Protocol)
        {
            FlowId = Flow->Id;
            break;
        }
    }

    if (FlowId == 0 && New != NULL)
    {
        RtlZeroMemory(New, sizeof(*New));
        New->Id = FlowId = ++WfpNextFlowId;
        New->EndpointId = Datagram->EndpointId;
        New->LocalAddress = Datagram->LocalAddress;
        New->RemoteAddress = Datagram->RemoteAddress;
        New->LocalPort = Datagram->LocalPort;
        New->RemotePort = Datagram->RemotePort;
        New->Protocol = Datagram->Protocol;
        New->Outbound = Datagram->Outbound;
        New->Loopback = Datagram->Loopback;
        InitializeListHead(&New->Contexts);
        InsertTailList(&WfpFlows, &New->Link);
        New = NULL;
        *Created = TRUE;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (New != NULL)
    {
        ExFreePoolWithTag(New, WFP_TAG);
    }
    return FlowId;
}

static
VOID
WfpSetValue32(
    _Out_ FWPS_INCOMING_VALUE0 *Value,
    _In_ UINT32 Number)
{
    Value->value.type = FWP_UINT32;
    Value->value.uint32 = Number;
}

static
VOID
WfpSetValue16(
    _Out_ FWPS_INCOMING_VALUE0 *Value,
    _In_ UINT16 Number)
{
    Value->value.type = FWP_UINT16;
    Value->value.uint16 = Number;
}

static
VOID
WfpSetValue8(
    _Out_ FWPS_INCOMING_VALUE0 *Value,
    _In_ UINT8 Number)
{
    Value->value.type = FWP_UINT8;
    Value->value.uint8 = Number;
}

static
PVOID
WfpDatagramLayerData(
    _In_opt_ PVOID Context)
{
    PWFP_DATAGRAM_DATA Data = Context;
    const WFP_SHIM_DATAGRAM *Datagram = Data->Datagram;
    UCHAR Header[80];
    ULONG HeaderLength;

    if (Datagram->Outbound)
    {
        Data->Packet = WfpCreatePacket(Datagram->TransportHeader,
                                       Datagram->TransportHeaderSize,
                                       Datagram->Data,
                                       Datagram->DataLength,
                                       0,
                                       &Datagram->Tag);
    }
    else
    {
        HeaderLength = Datagram->IpHeaderSize + Datagram->TransportHeaderSize;
        if (HeaderLength > sizeof(Header))
        {
            return NULL;
        }
        RtlCopyMemory(Header, Datagram->IpHeader, Datagram->IpHeaderSize);
        RtlCopyMemory(Header + Datagram->IpHeaderSize, Datagram->TransportHeader, Datagram->TransportHeaderSize);
        Data->Packet = WfpCreatePacket(Header,
                                       HeaderLength,
                                       Datagram->Data,
                                       Datagram->DataLength,
                                       HeaderLength,
                                       &Datagram->Tag);
    }
    return Data->Packet != NULL ? Data->Packet->NetBufferList : NULL;
}

static
BOOLEAN
WfpGetProcessPath(
    _In_opt_ HANDLE ProcessId,
    _Out_ FWP_BYTE_BLOB *Path)
{
    PUNICODE_STRING ImageName = NULL;
    PEPROCESS Process;
    PWCHAR Buffer;
    ULONG Index;

    Path->size = 0;
    Path->data = NULL;
    if (ProcessId == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !NT_SUCCESS(PsLookupProcessByProcessId(ProcessId, &Process)))
    {
        return FALSE;
    }

    if (!NT_SUCCESS(SeLocateProcessImageName(Process, &ImageName)))
    {
        ImageName = NULL;
    }
    ObDereferenceObject(Process);
    if (ImageName == NULL)
    {
        return FALSE;
    }

    if (ImageName->Length != 0)
    {
        Buffer = ExAllocatePoolWithTag(NonPagedPoolNx, ImageName->Length + sizeof(WCHAR), WFP_TAG);
        if (Buffer != NULL)
        {
            for (Index = 0; Index < ImageName->Length / sizeof(WCHAR); Index++)
            {
                Buffer[Index] = RtlDowncaseUnicodeChar(ImageName->Buffer[Index]);
            }
            Buffer[Index] = UNICODE_NULL;
            Path->size = ImageName->Length + sizeof(WCHAR);
            Path->data = (UINT8 *)Buffer;
        }
    }
    ExFreePool(ImageName);
    return Path->data != NULL;
}

static
BOOLEAN
WfpClassifyFlowEstablished(
    _In_ const WFP_SHIM_FLOW *Flow,
    _In_ UINT64 FlowId)
{
    FWPS_INCOMING_VALUE0 Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_MAX];
    FWPS_INCOMING_METADATA_VALUES0 Metadata;
    FWPS_CLASSIFY_OUT0 ClassifyOut;
    FWP_BYTE_BLOB ProcessPath;
    UINT64 Interface = (UINT64)Flow->InterfaceType << 48;

    if (WfpLayerFilters[FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4] == 0)
    {
        return TRUE;
    }

    RtlZeroMemory(Values, sizeof(Values));
    WfpSetValue32(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_ADDRESS], Flow->LocalAddress);
    WfpSetValue8(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_ADDRESS_TYPE], WFP_ADDRESS_TYPE_UNICAST);
    WfpSetValue16(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_PORT], Flow->LocalPort);
    WfpSetValue8(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_PROTOCOL], Flow->Protocol);
    WfpSetValue32(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_REMOTE_ADDRESS], Flow->RemoteAddress);
    WfpSetValue16(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_REMOTE_PORT], Flow->RemotePort);
    WfpSetValue8(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_DESTINATION_ADDRESS_TYPE], WFP_ADDRESS_TYPE_UNICAST);
    Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_INTERFACE].value.type = FWP_UINT64;
    Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_INTERFACE].value.uint64 = &Interface;
    WfpSetValue32(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_DIRECTION],
                  Flow->Outbound ? FWP_DIRECTION_OUTBOUND : FWP_DIRECTION_INBOUND);
    WfpSetValue32(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_INTERFACE_TYPE], Flow->InterfaceType);
    WfpSetValue32(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_TUNNEL_TYPE], 0);
    WfpSetValue32(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_FLAGS], Flow->Loopback ? FWP_CONDITION_FLAG_IS_LOOPBACK : 0);
    WfpSetValue32(&Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_COMPARTMENT_ID], WFP_COMPARTMENT_ID);

    RtlZeroMemory(&Metadata, sizeof(Metadata));
    Metadata.currentMetadataValues = FWPS_METADATA_FIELD_FLOW_HANDLE |
                                     FWPS_METADATA_FIELD_COMPARTMENT_ID |
                                     FWPS_METADATA_FIELD_TRANSPORT_ENDPOINT_HANDLE;
    Metadata.flowHandle = FlowId;
    Metadata.compartmentId = WFP_COMPARTMENT_ID;
    Metadata.transportEndpointHandle = Flow->EndpointId;
    if (Flow->ProcessId != NULL)
    {
        Metadata.currentMetadataValues |= FWPS_METADATA_FIELD_PROCESS_ID;
        Metadata.processId = (UINT64)(ULONG_PTR)Flow->ProcessId;
    }
    if (WfpGetProcessPath(Flow->ProcessId, &ProcessPath))
    {
        Metadata.currentMetadataValues |= FWPS_METADATA_FIELD_PROCESS_PATH;
        Metadata.processPath = &ProcessPath;
        Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_ALE_APP_ID].value.type = FWP_BYTE_BLOB_TYPE;
        Values[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_ALE_APP_ID].value.byteBlob = &ProcessPath;
    }

    WfpClassify(FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4,
                Values,
                FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_MAX,
                &Metadata,
                FlowId,
                NULL,
                NULL,
                &ClassifyOut);
    if (ProcessPath.data != NULL)
    {
        ExFreePoolWithTag(ProcessPath.data, WFP_TAG);
    }
    return ClassifyOut.actionType != FWP_ACTION_BLOCK;
}

BOOLEAN
NTAPI
WfpShimStreamActive(VOID)
{
    return WfpLayerFilters[FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4] != 0 ||
           WfpLayerFilters[FWPS_LAYER_STREAM_V4] != 0;
}

BOOLEAN
NTAPI
WfpShimEstablishFlow(
    _In_ const WFP_SHIM_FLOW *Flow)
{
    BOOLEAN Created;
    UINT64 FlowId;

    if (!WfpShimStreamActive())
    {
        return TRUE;
    }

    FlowId = WfpGetFlow(Flow, &Created);
    return !Created || WfpClassifyFlowEstablished(Flow, FlowId);
}

static
PVOID
WfpStreamLayerData(
    _In_opt_ PVOID Context)
{
    PWFP_STREAM_DATA Stream = Context;
    static const WFP_SHIM_TAG NoTag;

    if (Stream->Length != 0)
    {
        Stream->Packet = WfpCreatePacket(NULL, 0, Stream->Bytes, Stream->Length, 0, &NoTag);
        if (Stream->Packet != NULL)
        {
            Stream->Data.netBufferListChain = Stream->Packet->NetBufferList;
            Stream->Data.dataOffset.netBufferList = Stream->Packet->NetBufferList;
            Stream->Data.dataOffset.netBuffer = NET_BUFFER_LIST_FIRST_NB(Stream->Packet->NetBufferList);
            Stream->Data.dataOffset.mdl = NET_BUFFER_CURRENT_MDL(Stream->Data.dataOffset.netBuffer);
            Stream->Data.dataOffset.mdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(Stream->Data.dataOffset.netBuffer);
        }
    }
    return &Stream->IoPacket;
}

BOOLEAN
NTAPI
WfpShimClassifyStream(
    _In_ ULONG64 EndpointId,
    _In_ ULONG StreamFlags,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    FWPS_INCOMING_VALUE0 Values[FWPS_FIELD_STREAM_V4_MAX];
    FWPS_INCOMING_METADATA_VALUES0 Metadata;
    FWPS_CLASSIFY_OUT0 ClassifyOut;
    WFP_STREAM_DATA Stream;
    PLIST_ENTRY Entry;
    WFP_FLOW Flow;
    BOOLEAN Found = FALSE;
    KIRQL OldIrql;

    if (WfpLayerFilters[FWPS_LAYER_STREAM_V4] == 0)
    {
        return TRUE;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    for (Entry = WfpFlows.Flink; Entry != &WfpFlows; Entry = Entry->Flink)
    {
        Flow = *CONTAINING_RECORD(Entry, WFP_FLOW, Link);
        if (Flow.EndpointId == EndpointId && Flow.Protocol == IPPROTO_TCP)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);
    if (!Found)
    {
        return TRUE;
    }

    RtlZeroMemory(Values, sizeof(Values));
    WfpSetValue32(&Values[FWPS_FIELD_STREAM_V4_IP_LOCAL_ADDRESS], Flow.LocalAddress);
    WfpSetValue8(&Values[FWPS_FIELD_STREAM_V4_IP_LOCAL_ADDRESS_TYPE], WFP_ADDRESS_TYPE_UNICAST);
    WfpSetValue32(&Values[FWPS_FIELD_STREAM_V4_IP_REMOTE_ADDRESS], Flow.RemoteAddress);
    WfpSetValue16(&Values[FWPS_FIELD_STREAM_V4_IP_LOCAL_PORT], Flow.LocalPort);
    WfpSetValue16(&Values[FWPS_FIELD_STREAM_V4_IP_REMOTE_PORT], Flow.RemotePort);
    WfpSetValue32(&Values[FWPS_FIELD_STREAM_V4_DIRECTION],
                  Flow.Outbound ? FWP_DIRECTION_OUTBOUND : FWP_DIRECTION_INBOUND);
    WfpSetValue32(&Values[FWPS_FIELD_STREAM_V4_FLAGS], Flow.Loopback ? FWP_CONDITION_FLAG_IS_LOOPBACK : 0);
    WfpSetValue32(&Values[FWPS_FIELD_STREAM_V4_COMPARTMENT_ID], WFP_COMPARTMENT_ID);

    RtlZeroMemory(&Metadata, sizeof(Metadata));
    Metadata.currentMetadataValues = FWPS_METADATA_FIELD_FLOW_HANDLE | FWPS_METADATA_FIELD_COMPARTMENT_ID;
    Metadata.flowHandle = Flow.Id;
    Metadata.compartmentId = WFP_COMPARTMENT_ID;

    RtlZeroMemory(&Stream, sizeof(Stream));
    Stream.Bytes = Data;
    Stream.Length = Length;
    if (StreamFlags & WFP_SHIM_STREAM_SEND)
    {
        Stream.Data.flags = FWPS_STREAM_FLAG_SEND |
                            ((StreamFlags & WFP_SHIM_STREAM_DISCONNECT) ? FWPS_STREAM_FLAG_SEND_DISCONNECT : 0);
    }
    else
    {
        Stream.Data.flags = FWPS_STREAM_FLAG_RECEIVE |
                            ((StreamFlags & WFP_SHIM_STREAM_PUSH) ? FWPS_STREAM_FLAG_RECEIVE_PUSH : 0) |
                            ((StreamFlags & WFP_SHIM_STREAM_DISCONNECT) ? FWPS_STREAM_FLAG_RECEIVE_DISCONNECT : 0);
    }
    Stream.Data.dataLength = Length;
    Stream.IoPacket.streamData = &Stream.Data;
    Stream.IoPacket.streamAction = FWPS_STREAM_ACTION_NONE;

    WfpClassify(FWPS_LAYER_STREAM_V4,
                Values,
                FWPS_FIELD_STREAM_V4_MAX,
                &Metadata,
                Flow.Id,
                WfpStreamLayerData,
                &Stream,
                &ClassifyOut);
    if (Stream.Packet != NULL)
    {
        WfpDereferencePacket(Stream.Packet);
    }
    return ClassifyOut.actionType != FWP_ACTION_BLOCK &&
           Stream.IoPacket.streamAction != FWPS_STREAM_ACTION_DROP_CONNECTION;
}

BOOLEAN
NTAPI
WfpShimClassifyDatagram(
    _In_ const WFP_SHIM_DATAGRAM *Datagram)
{
    FWPS_INCOMING_VALUE0 Values[FWPS_FIELD_DATAGRAM_DATA_V4_MAX];
    FWPS_INCOMING_METADATA_VALUES0 Metadata;
    FWPS_CLASSIFY_OUT0 ClassifyOut;
    WFP_DATAGRAM_DATA Data;
    WFP_SHIM_FLOW Flow;
    UINT64 Interface, FlowId;
    UINT32 Direction, Flags;
    BOOLEAN Created;

    if (!WfpShimDatagramActive())
    {
        return TRUE;
    }

    RtlZeroMemory(&Flow, sizeof(Flow));
    Flow.EndpointId = Datagram->EndpointId;
    Flow.Outbound = Datagram->Outbound;
    Flow.Loopback = Datagram->Loopback;
    Flow.Protocol = Datagram->Protocol;
    Flow.LocalAddress = Datagram->LocalAddress;
    Flow.RemoteAddress = Datagram->RemoteAddress;
    Flow.LocalPort = Datagram->LocalPort;
    Flow.RemotePort = Datagram->RemotePort;
    Flow.InterfaceType = Datagram->InterfaceType;
    Flow.ProcessId = Datagram->ProcessId;

    FlowId = WfpGetFlow(&Flow, &Created);
    Direction = Datagram->Outbound ? FWP_DIRECTION_OUTBOUND : FWP_DIRECTION_INBOUND;
    Flags = Datagram->Loopback ? FWP_CONDITION_FLAG_IS_LOOPBACK : 0;
    Interface = (UINT64)Datagram->InterfaceType << 48;

    if (Created && !WfpClassifyFlowEstablished(&Flow, FlowId))
    {
        return FALSE;
    }

    if (WfpLayerFilters[FWPS_LAYER_DATAGRAM_DATA_V4] == 0)
    {
        return TRUE;
    }

    RtlZeroMemory(Values, sizeof(Values));
    WfpSetValue8(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_IP_PROTOCOL], Datagram->Protocol);
    WfpSetValue32(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_ADDRESS], Datagram->LocalAddress);
    WfpSetValue32(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_IP_REMOTE_ADDRESS], Datagram->RemoteAddress);
    WfpSetValue8(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_ADDRESS_TYPE], WFP_ADDRESS_TYPE_UNICAST);
    WfpSetValue16(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_PORT], Datagram->LocalPort);
    WfpSetValue16(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_IP_REMOTE_PORT], Datagram->RemotePort);
    Values[FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_INTERFACE].value.type = FWP_UINT64;
    Values[FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_INTERFACE].value.uint64 = &Interface;
    WfpSetValue32(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_INTERFACE_INDEX], Datagram->InterfaceIndex);
    WfpSetValue32(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_SUB_INTERFACE_INDEX],
                  (Datagram->Outbound || Datagram->Tag.Handle != NULL) ? 0 : 1);
    WfpSetValue32(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_DIRECTION], Direction);
    WfpSetValue32(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_FLAGS], Flags);
    WfpSetValue32(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_INTERFACE_TYPE], Datagram->InterfaceType);
    WfpSetValue32(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_TUNNEL_TYPE], 0);
    WfpSetValue32(&Values[FWPS_FIELD_DATAGRAM_DATA_V4_COMPARTMENT_ID], WFP_COMPARTMENT_ID);

    RtlZeroMemory(&Metadata, sizeof(Metadata));
    Metadata.currentMetadataValues = FWPS_METADATA_FIELD_FLOW_HANDLE |
                                     FWPS_METADATA_FIELD_TRANSPORT_HEADER_SIZE |
                                     FWPS_METADATA_FIELD_COMPARTMENT_ID |
                                     FWPS_METADATA_FIELD_TRANSPORT_ENDPOINT_HANDLE;
    Metadata.flowHandle = FlowId;
    Metadata.transportHeaderSize = Datagram->TransportHeaderSize;
    Metadata.compartmentId = WFP_COMPARTMENT_ID;
    Metadata.transportEndpointHandle = Datagram->EndpointId;
    if (!Datagram->Outbound)
    {
        Metadata.currentMetadataValues |= FWPS_METADATA_FIELD_IP_HEADER_SIZE;
        Metadata.ipHeaderSize = Datagram->IpHeaderSize;
    }

    Data.Datagram = Datagram;
    Data.Packet = NULL;
    WfpClassify(FWPS_LAYER_DATAGRAM_DATA_V4,
                Values,
                FWPS_FIELD_DATAGRAM_DATA_V4_MAX,
                &Metadata,
                FlowId,
                WfpDatagramLayerData,
                &Data,
                &ClassifyOut);
    if (Data.Packet != NULL)
    {
        WfpDereferencePacket(Data.Packet);
    }
    return ClassifyOut.actionType != FWP_ACTION_BLOCK;
}
