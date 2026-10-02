/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform datagram callout test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndis.h>
#include <initguid.h>
#include <fwpsk.h>
#include <fwpmk.h>

#define NDEBUG
#include <debug.h>

#include "wfp_udp.h"

#define TEST_TAG 'uPfW'
#define MAX_EVENTS 48
#define MAX_NOTIFY 8

DEFINE_GUID(TEST_SUBLAYER, 0x7c5a6f31, 0x2b4d, 0x4e8a, 0x91, 0x33, 0x5d, 0x0e, 0x61, 0xa2, 0xc4, 0x01);
DEFINE_GUID(TEST_FLOW_CALLOUT, 0x7c5a6f31, 0x2b4d, 0x4e8a, 0x91, 0x33, 0x5d, 0x0e, 0x61, 0xa2, 0xc4, 0x02);
DEFINE_GUID(TEST_DATAGRAM_CALLOUT, 0x7c5a6f31, 0x2b4d, 0x4e8a, 0x91, 0x33, 0x5d, 0x0e, 0x61, 0xa2, 0xc4, 0x03);
DEFINE_GUID(TEST_UNKNOWN, 0x7c5a6f31, 0x2b4d, 0x4e8a, 0x91, 0x33, 0x5d, 0x0e, 0x61, 0xa2, 0xc4, 0x04);

typedef struct _UDP_HEADER
{
    UINT16 SourcePort;
    UINT16 DestinationPort;
    UINT16 Length;
    UINT16 Checksum;
} UDP_HEADER, *PUDP_HEADER;

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
    UINT32 IpHeaderSize;
    UINT32 TransportHeaderSize;
    UINT64 EndpointHandle;
    ULONG Compartment;
    ULONG DataLength;
    UCHAR Data[8];
    KIRQL Irql;
    UINT64 FlowContext;
    UINT64 FilterContext;
    UINT32 FilterAction;
    UINT32 FilterCallout;
    UINT16 SubLayerWeight;
    UINT16 FilterFlags;
    UINT32 WeightType;
    UINT32 Conditions;
    UINT32 Rights;
    UINT32 Action;
    UINT32 OutFlags;
    NTSTATUS AssociateStatus;
    NTSTATUS AssociateAgainStatus;
    NTSTATUS AssociateZeroStatus;
    NTSTATUS AssociateNoDeleteStatus;
    ULONG InjectionState;
    UINT32 InterfaceIndex;
    UINT32 SubInterfaceIndex;
    BOOLEAN ValuesValid;
    BOOLEAN PathValid;
} WFP_EVENT, *PWFP_EVENT;

typedef struct _NOTIFY_RECORD
{
    FWPS_CALLOUT_NOTIFY_TYPE Type;
    BOOLEAN HasKey;
    UINT64 Context;
    UINT32 CalloutId;
    UINT32 Action;
} NOTIFY_RECORD;

typedef struct _PENDED_PACKET
{
    WORK_QUEUE_ITEM WorkItem;
    PNET_BUFFER_LIST NetBufferList;
    UINT32 Direction;
    UINT64 EndpointHandle;
    ULONG CompartmentId;
    SCOPE_ID RemoteScopeId;
    UINT32 RemoteAddress;
    UINT32 LocalAddress;
    UINT32 IpHeaderSize;
    UINT32 TransportHeaderSize;
    ULONG DataOffset;
    ULONG InterfaceIndex;
    ULONG SubInterfaceIndex;
} PENDED_PACKET, *PPENDED_PACKET;

static KMT_MESSAGE_HANDLER TestMessageHandler;

static HANDLE EngineHandle;
static HANDLE InjectionHandle;
static UINT32 FlowCalloutId, DatagramCalloutId;
static WFP_EVENT Events[MAX_EVENTS];
static volatile LONG EventCount;
static volatile LONG FlowDeleteCount;
static volatile LONG NotifyCount;
static NOTIFY_RECORD Notifications[MAX_NOTIFY];
static UINT16 FlowDeleteLayer;
static UINT32 FlowDeleteCallout;
static UINT64 FlowDeleteContext;
static volatile LONG InjectCompleteCount;
static volatile LONG PendingPackets;
static NTSTATUS InjectStatus[2];
static NTSTATUS CompleteStatus[4];
static NTSTATUS CloneStatus[2];
static NTSTATUS ConstructStatus;
static USHORT ClonedPort[2];
static UINT64 FlowHandles[8];
static volatile LONG FlowHandleCount;
static BOOLEAN Redirect;
static ULONG LocalAddress;

static
VOID
NTAPI
InjectComplete(
    _Inout_ PVOID Context,
    _Inout_ PNET_BUFFER_LIST NetBufferList,
    _In_ BOOLEAN DispatchLevel)
{
    PPENDED_PACKET Packet = Context;
    LONG Index = InterlockedIncrement(&InjectCompleteCount) - 1;

    if (Index < (LONG)RTL_NUMBER_OF(CompleteStatus))
    {
        CompleteStatus[Index] = NetBufferList->Status;
    }

    FwpsFreeCloneNetBufferList0(NetBufferList, 0);
    FwpsDereferenceNetBufferList0(Packet->NetBufferList, DispatchLevel);
    ExFreePoolWithTag(Packet, TEST_TAG);
    InterlockedDecrement(&PendingPackets);
}

static
VOID
NTAPI
RedirectWorker(
    _In_ PVOID Parameter)
{
    PPENDED_PACKET Packet = Parameter;
    FWPS_TRANSPORT_SEND_PARAMS1 SendArgs;
    PNET_BUFFER_LIST Clone = NULL;
    PNET_BUFFER NetBuffer;
    PUDP_HEADER Udp;
    UINT32 Remote, Local;
    NTSTATUS Status;
    ULONG Slot = Packet->Direction == FWP_DIRECTION_OUTBOUND ? 0 : 1;

    if (Packet->Direction == FWP_DIRECTION_OUTBOUND)
    {
        Status = FwpsAllocateCloneNetBufferList0(Packet->NetBufferList, NULL, NULL, 0, &Clone);
        CloneStatus[Slot] = Status;
        if (NT_SUCCESS(Status))
        {
            Udp = NdisGetDataBuffer(NET_BUFFER_LIST_FIRST_NB(Clone), sizeof(UDP_HEADER), NULL, sizeof(UINT16), 0);
            if (Udp != NULL)
            {
                ClonedPort[Slot] = RtlUshortByteSwap(Udp->DestinationPort);
                Udp->DestinationPort = RtlUshortByteSwap(WFPUDP_SERVER_PORT);
                Udp->Checksum = 0;
            }

            Remote = RtlUlongByteSwap(Packet->RemoteAddress);
            RtlZeroMemory(&SendArgs, sizeof(SendArgs));
            SendArgs.remoteAddress = (UCHAR *)&Remote;
            SendArgs.remoteScopeId = Packet->RemoteScopeId;
            Status = FwpsInjectTransportSendAsync1(InjectionHandle,
                                                   NULL,
                                                   Packet->EndpointHandle,
                                                   0,
                                                   &SendArgs,
                                                   AF_INET,
                                                   Packet->CompartmentId,
                                                   Clone,
                                                   InjectComplete,
                                                   Packet);
            InjectStatus[Slot] = Status;
        }
    }
    else
    {
        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Packet->NetBufferList);
        if (NET_BUFFER_DATA_OFFSET(NetBuffer) != Packet->DataOffset)
        {
            Packet->TransportHeaderSize = 0;
        }

        Status = NdisRetreatNetBufferDataStart(NetBuffer,
                                               Packet->IpHeaderSize + Packet->TransportHeaderSize,
                                               0,
                                               NULL);
        if (NT_SUCCESS(Status))
        {
            Status = FwpsAllocateCloneNetBufferList0(Packet->NetBufferList, NULL, NULL, 0, &Clone);
            NdisAdvanceNetBufferDataStart(NetBuffer,
                                          Packet->IpHeaderSize + Packet->TransportHeaderSize,
                                          FALSE,
                                          NULL);
        }
        CloneStatus[Slot] = Status;
        if (NT_SUCCESS(Status))
        {
            NetBuffer = NET_BUFFER_LIST_FIRST_NB(Clone);
            NdisAdvanceNetBufferDataStart(NetBuffer, Packet->IpHeaderSize, FALSE, NULL);
            Udp = NdisGetDataBuffer(NetBuffer, sizeof(UDP_HEADER), NULL, sizeof(UINT16), 0);
            if (Udp != NULL)
            {
                ClonedPort[Slot] = RtlUshortByteSwap(Udp->SourcePort);
                Udp->SourcePort = RtlUshortByteSwap(WFPUDP_PROXY_PORT);
                Udp->Checksum = 0;
            }
            (VOID)NdisRetreatNetBufferDataStart(NetBuffer, Packet->IpHeaderSize, 0, NULL);

            Remote = RtlUlongByteSwap(Packet->RemoteAddress);
            Local = RtlUlongByteSwap(Packet->LocalAddress);
            ConstructStatus = FwpsConstructIpHeaderForTransportPacket0(Clone,
                                                                        Packet->IpHeaderSize,
                                                                        AF_INET,
                                                                        (UCHAR *)&Remote,
                                                                        (UCHAR *)&Local,
                                                                        IPPROTO_UDP,
                                                                        0,
                                                                        NULL,
                                                                        0,
                                                                        0,
                                                                        NULL,
                                                                        0,
                                                                        0);

            Status = FwpsInjectTransportReceiveAsync0(InjectionHandle,
                                                      NULL,
                                                      NULL,
                                                      0,
                                                      AF_INET,
                                                      Packet->CompartmentId,
                                                      Packet->InterfaceIndex,
                                                      Packet->SubInterfaceIndex,
                                                      Clone,
                                                      InjectComplete,
                                                      Packet);
            InjectStatus[Slot] = Status;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        if (Clone != NULL)
        {
            FwpsFreeCloneNetBufferList0(Clone, 0);
        }
        FwpsDereferenceNetBufferList0(Packet->NetBufferList, FALSE);
        ExFreePoolWithTag(Packet, TEST_TAG);
        InterlockedDecrement(&PendingPackets);
    }
}

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

    switch (Type)
    {
        case FWP_UINT8: return Value->uint8 == Expected;
        case FWP_UINT16: return Value->uint16 == Expected;
        case FWP_UINT32: return Value->uint32 == Expected;
        case FWP_UINT64: return Value->uint64 != NULL && *Value->uint64 == Expected;
        default: return FALSE;
    }
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
BOOLEAN
IsProcessPath(
    _In_ const FWPS_INCOMING_VALUES0 *Values,
    _In_ const FWPS_INCOMING_METADATA_VALUES0 *Metadata)
{
    const FWP_VALUE0 *AppId = &Values->incomingValue[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_ALE_APP_ID].value;
    const FWP_BYTE_BLOB *Path = Metadata->processPath;
    PCWSTR Text;
    ULONG Length, Index;

    if (!FWPS_IS_METADATA_FIELD_PRESENT(Metadata, FWPS_METADATA_FIELD_PROCESS_PATH) || Path == NULL ||
        Path->size < 2 * sizeof(WCHAR) || (Path->size % sizeof(WCHAR)) != 0)
    {
        return FALSE;
    }

    Text = (PCWSTR)Path->data;
    Length = Path->size / sizeof(WCHAR) - 1;
    if (Text[Length] != UNICODE_NULL || Text[0] != L'\\')
    {
        return FALSE;
    }
    for (Index = 0; Index < Length; Index++)
    {
        if (Text[Index] == UNICODE_NULL || Text[Index] != RtlDowncaseUnicodeChar(Text[Index]))
        {
            return FALSE;
        }
    }

    return AppId->type == FWP_BYTE_BLOB_TYPE && AppId->byteBlob != NULL &&
           AppId->byteBlob->size == Path->size &&
           RtlEqualMemory(AppId->byteBlob->data, Path->data, Path->size);
}

static
PWFP_EVENT
NewEvent(
    _In_ const FWPS_INCOMING_VALUES0 *Values,
    _In_ const FWPS_INCOMING_METADATA_VALUES0 *Metadata,
    _In_ const FWPS_FILTER3 *Filter,
    _In_ UINT64 FlowContext,
    _In_ const FWPS_CLASSIFY_OUT0 *ClassifyOut)
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
    Event->Metadata = Metadata->currentMetadataValues;
    Event->MetadataFlags = Metadata->flags;
    Event->FlowHandle = Metadata->flowHandle;
    Event->IpHeaderSize = Metadata->ipHeaderSize;
    Event->TransportHeaderSize = Metadata->transportHeaderSize;
    Event->EndpointHandle = Metadata->transportEndpointHandle;
    Event->Compartment = Metadata->compartmentId;
    Event->Irql = KeGetCurrentIrql();
    Event->FlowContext = FlowContext;
    Event->FilterContext = Filter->context;
    Event->FilterAction = Filter->action.type;
    Event->FilterCallout = Filter->action.calloutId;
    Event->SubLayerWeight = Filter->subLayerWeight;
    Event->FilterFlags = Filter->flags;
    Event->WeightType = Filter->weight.type;
    Event->Conditions = Filter->numFilterConditions;
    Event->Rights = ClassifyOut->rights;
    Event->Action = ClassifyOut->actionType;
    Event->OutFlags = ClassifyOut->flags;
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
    PWFP_EVENT Event = NewEvent(Values, Metadata, Filter, FlowContext, ClassifyOut);
    LONG Slot;

    UNREFERENCED_PARAMETER(LayerData);
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
        Event->ValuesValid = IsType(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_ADDRESS, FWP_UINT32) &&
                             IsNumber(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_ADDRESS_TYPE, FWP_UINT8, 1) &&
                             IsType(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_PORT, FWP_UINT16) &&
                             IsType(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_PROTOCOL, FWP_UINT8) &&
                             IsType(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_REMOTE_ADDRESS, FWP_UINT32) &&
                             IsType(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_REMOTE_PORT, FWP_UINT16) &&
                             IsNumber(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_DESTINATION_ADDRESS_TYPE, FWP_UINT8, 1) &&
                             IsNumber(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_INTERFACE, FWP_UINT64, (UINT64)IF_TYPE_SOFTWARE_LOOPBACK << 48) &&
                             IsType(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_DIRECTION, FWP_UINT32) &&
                             IsNumber(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_INTERFACE_TYPE, FWP_UINT32, IF_TYPE_SOFTWARE_LOOPBACK) &&
                             IsNumber(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_TUNNEL_TYPE, FWP_UINT32, 0) &&
                             IsType(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_FLAGS, FWP_UINT32) &&
                             IsNumber(Values, FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_COMPARTMENT_ID, FWP_UINT32, 1);
        Event->PathValid = IsProcessPath(Values, Metadata);

        if (FWPS_IS_METADATA_FIELD_PRESENT(Metadata, FWPS_METADATA_FIELD_FLOW_HANDLE))
        {
            Event->AssociateZeroStatus = FwpsFlowAssociateContext0(Metadata->flowHandle,
                                                                   FWPS_LAYER_DATAGRAM_DATA_V4,
                                                                   DatagramCalloutId,
                                                                   0);
            Event->AssociateNoDeleteStatus = FwpsFlowAssociateContext0(Metadata->flowHandle,
                                                                       FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4,
                                                                       FlowCalloutId,
                                                                       1);
            Event->AssociateStatus = FwpsFlowAssociateContext0(Metadata->flowHandle,
                                                               FWPS_LAYER_DATAGRAM_DATA_V4,
                                                               DatagramCalloutId,
                                                               0x1000 + Event->RemotePort);
            Event->AssociateAgainStatus = FwpsFlowAssociateContext0(Metadata->flowHandle,
                                                                    FWPS_LAYER_DATAGRAM_DATA_V4,
                                                                    DatagramCalloutId,
                                                                    0x2000);
            if (NT_SUCCESS(Event->AssociateStatus))
            {
                Slot = InterlockedIncrement(&FlowHandleCount) - 1;
                if (Slot < (LONG)RTL_NUMBER_OF(FlowHandles))
                {
                    FlowHandles[Slot] = Metadata->flowHandle;
                }
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
DatagramClassify(
    _In_ const FWPS_INCOMING_VALUES0 *Values,
    _In_ const FWPS_INCOMING_METADATA_VALUES0 *Metadata,
    _Inout_opt_ PVOID LayerData,
    _In_opt_ const void *ClassifyContext,
    _In_ const FWPS_FILTER3 *Filter,
    _In_ UINT64 FlowContext,
    _Inout_ FWPS_CLASSIFY_OUT0 *ClassifyOut)
{
    PWFP_EVENT Event = NewEvent(Values, Metadata, Filter, FlowContext, ClassifyOut);
    PNET_BUFFER_LIST NetBufferList = LayerData;
    FWPS_PACKET_INJECTION_STATE State = FWPS_PACKET_NOT_INJECTED;
    PPENDED_PACKET Packet;
    PNET_BUFFER NetBuffer;
    UCHAR Storage[16];
    PUCHAR Data = NULL;
    UINT32 Direction, Local, Remote;
    UINT16 RemotePort;
    ULONG Length = 0, Skip;

    UNREFERENCED_PARAMETER(ClassifyContext);

    Direction = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_DIRECTION].value.uint32;
    Local = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_ADDRESS].value.uint32;
    Remote = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_IP_REMOTE_ADDRESS].value.uint32;
    RemotePort = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_IP_REMOTE_PORT].value.uint16;
    Skip = Direction == FWP_DIRECTION_OUTBOUND ? sizeof(UDP_HEADER) : 0;

    if (NetBufferList != NULL)
    {
        State = FwpsQueryPacketInjectionState0(InjectionHandle, NetBufferList, NULL);
        NetBuffer = NET_BUFFER_LIST_FIRST_NB(NetBufferList);
        Length = min(NET_BUFFER_DATA_LENGTH(NetBuffer), sizeof(Storage));
        Data = Length != 0 ? NdisGetDataBuffer(NetBuffer, Length, Storage, 1, 0) : NULL;
    }

    if (Event != NULL)
    {
        Event->Direction = Direction;
        Event->LocalAddress = Local;
        Event->RemoteAddress = Remote;
        Event->LocalPort = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_PORT].value.uint16;
        Event->RemotePort = RemotePort;
        Event->Protocol = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_IP_PROTOCOL].value.uint8;
        Event->Flags = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_FLAGS].value.uint32;
        Event->InjectionState = State;
        Event->InterfaceIndex = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_INTERFACE_INDEX].value.uint32;
        Event->SubInterfaceIndex = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_SUB_INTERFACE_INDEX].value.uint32;
        Event->ValuesValid = IsType(Values, FWPS_FIELD_DATAGRAM_DATA_V4_IP_PROTOCOL, FWP_UINT8) &&
                             IsType(Values, FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_ADDRESS, FWP_UINT32) &&
                             IsType(Values, FWPS_FIELD_DATAGRAM_DATA_V4_IP_REMOTE_ADDRESS, FWP_UINT32) &&
                             IsNumber(Values, FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_ADDRESS_TYPE, FWP_UINT8, 1) &&
                             IsType(Values, FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_PORT, FWP_UINT16) &&
                             IsType(Values, FWPS_FIELD_DATAGRAM_DATA_V4_IP_REMOTE_PORT, FWP_UINT16) &&
                             IsNumber(Values, FWPS_FIELD_DATAGRAM_DATA_V4_IP_LOCAL_INTERFACE, FWP_UINT64, (UINT64)IF_TYPE_SOFTWARE_LOOPBACK << 48) &&
                             IsType(Values, FWPS_FIELD_DATAGRAM_DATA_V4_INTERFACE_INDEX, FWP_UINT32) &&
                             IsNumber(Values,
                                      FWPS_FIELD_DATAGRAM_DATA_V4_SUB_INTERFACE_INDEX,
                                      FWP_UINT32,
                                      (Direction == FWP_DIRECTION_OUTBOUND || State != FWPS_PACKET_NOT_INJECTED) ? 0 : 1) &&
                             IsType(Values, FWPS_FIELD_DATAGRAM_DATA_V4_DIRECTION, FWP_UINT32) &&
                             IsType(Values, FWPS_FIELD_DATAGRAM_DATA_V4_FLAGS, FWP_UINT32) &&
                             IsNumber(Values, FWPS_FIELD_DATAGRAM_DATA_V4_INTERFACE_TYPE, FWP_UINT32, IF_TYPE_SOFTWARE_LOOPBACK) &&
                             IsNumber(Values, FWPS_FIELD_DATAGRAM_DATA_V4_TUNNEL_TYPE, FWP_UINT32, 0) &&
                             IsNumber(Values, FWPS_FIELD_DATAGRAM_DATA_V4_COMPARTMENT_ID, FWP_UINT32, 1);
        if (NetBufferList != NULL)
        {
            Event->DataLength = NET_BUFFER_DATA_LENGTH(NET_BUFFER_LIST_FIRST_NB(NetBufferList));
            if (Data != NULL)
            {
                RtlCopyMemory(Event->Data, Data, min(Length, sizeof(Event->Data)));
            }
        }
    }

    if (Data != NULL && Length >= Skip + 4 &&
        ((Direction == FWP_DIRECTION_OUTBOUND && RtlEqualMemory(Data + Skip, "DROP", 4)) ||
         (Direction == FWP_DIRECTION_INBOUND && RtlEqualMemory(Data + Skip, "DRPI", 4))))
    {
        ClassifyOut->actionType = FWP_ACTION_BLOCK;
        ClassifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
        return;
    }

    if (!Redirect ||
        NetBufferList == NULL ||
        !(ClassifyOut->rights & FWPS_RIGHT_ACTION_WRITE) ||
        State == FWPS_PACKET_INJECTED_BY_SELF ||
        State == FWPS_PACKET_PREVIOUSLY_INJECTED_BY_SELF ||
        (Direction == FWP_DIRECTION_OUTBOUND && RemotePort != WFPUDP_PROXY_PORT) ||
        (Direction == FWP_DIRECTION_INBOUND && (RemotePort != WFPUDP_SERVER_PORT || FlowContext != 0x1000 + WFPUDP_SERVER_PORT)))
    {
        ClassifyOut->actionType = FWP_ACTION_PERMIT;
        if (Filter->flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT)
        {
            ClassifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
        }
        return;
    }

    Packet = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Packet), TEST_TAG);
    if (Packet == NULL)
    {
        ClassifyOut->actionType = FWP_ACTION_BLOCK;
        ClassifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
        return;
    }

    RtlZeroMemory(Packet, sizeof(*Packet));
    Packet->NetBufferList = NetBufferList;
    Packet->Direction = Direction;
    Packet->CompartmentId = Metadata->compartmentId;
    Packet->RemoteAddress = Remote;
    Packet->LocalAddress = Local;
    if (Direction == FWP_DIRECTION_OUTBOUND)
    {
        Packet->EndpointHandle = Metadata->transportEndpointHandle;
        Packet->RemoteScopeId = Metadata->remoteScopeId;
    }
    else
    {
        Packet->IpHeaderSize = Metadata->ipHeaderSize;
        Packet->TransportHeaderSize = Metadata->transportHeaderSize;
        Packet->DataOffset = NET_BUFFER_DATA_OFFSET(NET_BUFFER_LIST_FIRST_NB(NetBufferList));
        Packet->InterfaceIndex = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_INTERFACE_INDEX].value.uint32;
        Packet->SubInterfaceIndex = Values->incomingValue[FWPS_FIELD_DATAGRAM_DATA_V4_SUB_INTERFACE_INDEX].value.uint32;
    }

    FwpsReferenceNetBufferList0(NetBufferList, TRUE);
    InterlockedIncrement(&PendingPackets);
    ExInitializeWorkItem(&Packet->WorkItem, RedirectWorker, Packet);
    ExQueueWorkItem(&Packet->WorkItem, DelayedWorkQueue);

    ClassifyOut->actionType = FWP_ACTION_BLOCK;
    ClassifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
    ClassifyOut->flags |= FWPS_CLASSIFY_OUT_FLAG_ABSORB;
}

static
NTSTATUS
NTAPI
CalloutNotify(
    _In_ FWPS_CALLOUT_NOTIFY_TYPE NotifyType,
    _In_ const GUID *FilterKey,
    _Inout_ FWPS_FILTER3 *Filter)
{
    LONG Index = InterlockedIncrement(&NotifyCount) - 1;

    if (Index < MAX_NOTIFY)
    {
        Notifications[Index].Type = NotifyType;
        Notifications[Index].HasKey = (FilterKey != NULL);
        Notifications[Index].Context = Filter->context;
        Notifications[Index].CalloutId = Filter->action.calloutId;
        Notifications[Index].Action = Filter->action.type;
    }
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
    FlowDeleteLayer = LayerId;
    FlowDeleteCallout = CalloutId;
    FlowDeleteContext = FlowContext;
    InterlockedIncrement(&FlowDeleteCount);
}

static
NTSTATUS
AddFilter(
    _In_ const GUID *LayerKey,
    _In_ const GUID *SubLayerKey,
    _In_ const GUID *CalloutKey,
    _In_opt_ const GUID *PortField,
    _In_ UINT16 Port,
    _In_ UINT64 Context,
    _Out_opt_ PUINT64 Id)
{
    FWPM_FILTER_CONDITION0 Conditions[2];
    FWPM_FILTER0 Filter;
    ULONG Count = 0;

    RtlZeroMemory(&Filter, sizeof(Filter));
    RtlZeroMemory(Conditions, sizeof(Conditions));
    Filter.layerKey = *LayerKey;
    Filter.displayData.name = L"LiberNT WFP test filter";
    Filter.displayData.description = L"LiberNT WFP test filter";
    Filter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
    Filter.action.calloutKey = *CalloutKey;
    Filter.subLayerKey = *SubLayerKey;
    Filter.weight.type = FWP_EMPTY;
    Filter.rawContext = Context;

    if (PortField != NULL)
    {
        Conditions[Count].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
        Conditions[Count].matchType = FWP_MATCH_EQUAL;
        Conditions[Count].conditionValue.type = FWP_UINT8;
        Conditions[Count].conditionValue.uint8 = IPPROTO_UDP;
        Count++;
        Conditions[Count].fieldKey = *PortField;
        Conditions[Count].matchType = FWP_MATCH_EQUAL;
        Conditions[Count].conditionValue.type = FWP_UINT16;
        Conditions[Count].conditionValue.uint16 = Port;
        Count++;
    }
    Filter.numFilterConditions = Count;
    Filter.filterCondition = Conditions;

    return FwpmFilterAdd0(EngineHandle, &Filter, NULL, Id);
}

static
NTSTATUS
AddManagementCallout(
    _In_ const GUID *CalloutKey,
    _In_ const GUID *LayerKey,
    _Out_opt_ PUINT32 Id)
{
    FWPM_CALLOUT0 Management;

    RtlZeroMemory(&Management, sizeof(Management));
    Management.calloutKey = *CalloutKey;
    Management.displayData.name = L"LiberNT WFP test callout";
    Management.displayData.description = L"LiberNT WFP test callout";
    Management.applicableLayer = *LayerKey;
    return FwpmCalloutAdd0(EngineHandle, &Management, NULL, Id);
}

static
NTSTATUS
AddSubLayer(VOID)
{
    FWPM_SUBLAYER0 SubLayer;

    RtlZeroMemory(&SubLayer, sizeof(SubLayer));
    SubLayer.subLayerKey = TEST_SUBLAYER;
    SubLayer.displayData.name = L"LiberNT WFP test sublayer";
    SubLayer.displayData.description = L"LiberNT WFP test sublayer";
    SubLayer.weight = 0x100;
    return FwpmSubLayerAdd0(EngineHandle, &SubLayer, NULL);
}

static
NTSTATUS
RegisterCallout(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ const GUID *CalloutKey,
    _In_ FWPS_CALLOUT_CLASSIFY_FN3 Classify,
    _In_ BOOLEAN FlowCallout,
    _Out_ PUINT32 CalloutId)
{
    FWPS_CALLOUT3 Callout;

    RtlZeroMemory(&Callout, sizeof(Callout));
    Callout.calloutKey = *CalloutKey;
    Callout.classifyFn = Classify;
    Callout.notifyFn = CalloutNotify;
    if (!FlowCallout)
    {
        Callout.flowDeleteFn = FlowDelete;
        Callout.flags = FWP_CALLOUT_FLAG_CONDITIONAL_ON_FLOW;
    }

    return FwpsCalloutRegister3(DeviceObject, &Callout, CalloutId);
}

static
VOID
TestManagementErrors(VOID)
{
    NTSTATUS Status;

    Status = FwpmTransactionCommit0(EngineHandle);
    ok_eq_hex(Status, STATUS_FWP_NO_TXN_IN_PROGRESS);
    Status = FwpmTransactionAbort0(EngineHandle);
    ok_eq_hex(Status, STATUS_FWP_NO_TXN_IN_PROGRESS);

    Status = FwpmTransactionBegin0(EngineHandle, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = FwpmTransactionBegin0(EngineHandle, 0);
    ok_eq_hex(Status, STATUS_FWP_TXN_IN_PROGRESS);
    Status = AddSubLayer();
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddSubLayer();
    ok_eq_hex(Status, STATUS_FWP_ALREADY_EXISTS);
    Status = AddManagementCallout(&TEST_UNKNOWN, &TEST_UNKNOWN, NULL);
    ok_eq_hex(Status, STATUS_FWP_LAYER_NOT_FOUND);
    Status = AddFilter(&TEST_UNKNOWN, &TEST_SUBLAYER, &TEST_FLOW_CALLOUT, NULL, 0, 0, NULL);
    ok_eq_hex(Status, STATUS_FWP_LAYER_NOT_FOUND);
    Status = AddFilter(&FWPM_LAYER_DATAGRAM_DATA_V4, &TEST_UNKNOWN, &TEST_FLOW_CALLOUT, NULL, 0, 0, NULL);
    ok_eq_hex(Status, STATUS_FWP_SUBLAYER_NOT_FOUND);
    Status = AddFilter(&FWPM_LAYER_DATAGRAM_DATA_V4, &TEST_SUBLAYER, &TEST_UNKNOWN, NULL, 0, 0, NULL);
    ok_eq_hex(Status, STATUS_FWP_CALLOUT_NOT_FOUND);
    Status = FwpmTransactionAbort0(EngineHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestSetup(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    FWPM_SESSION0 Session;
    UINT32 FlowManagementId = 0, DatagramManagementId = 0, Extra = 0;
    UINT64 FilterId[4] = { 0 };
    NTSTATUS Status;
    LONG Index;

    RtlZeroMemory(&Session, sizeof(Session));
    Session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    Status = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &Session, &EngineHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        EngineHandle = NULL;
        return;
    }

    Status = FwpsInjectionHandleCreate0(AF_INET, FWPS_INJECTION_TYPE_TRANSPORT, &InjectionHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);

    TestManagementErrors();

    Status = FwpmTransactionBegin0(EngineHandle, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = AddSubLayer();
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = RegisterCallout(DeviceObject, &TEST_FLOW_CALLOUT, FlowClassify, TRUE, &FlowCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddManagementCallout(&TEST_FLOW_CALLOUT, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, &FlowManagementId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddManagementCallout(&TEST_DATAGRAM_CALLOUT, &FWPM_LAYER_DATAGRAM_DATA_V4, &DatagramManagementId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RegisterCallout(DeviceObject, &TEST_DATAGRAM_CALLOUT, DatagramClassify, FALSE, &DatagramCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(FlowCalloutId != 0 && DatagramCalloutId != 0 && FlowCalloutId != DatagramCalloutId,
       "Callout ids %u %u\n", FlowCalloutId, DatagramCalloutId);
    ok_eq_uint(FlowManagementId, FlowCalloutId);
    ok_eq_uint(DatagramManagementId, DatagramCalloutId);

    Status = RegisterCallout(DeviceObject, &TEST_FLOW_CALLOUT, FlowClassify, TRUE, &Extra);
    ok_eq_hex(Status, STATUS_FWP_ALREADY_EXISTS);
    Status = AddManagementCallout(&TEST_FLOW_CALLOUT, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, NULL);
    ok_eq_hex(Status, STATUS_FWP_ALREADY_EXISTS);

    Status = AddFilter(&FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4,
                       &TEST_SUBLAYER,
                       &TEST_FLOW_CALLOUT,
                       &FWPM_CONDITION_IP_REMOTE_PORT,
                       WFPUDP_SERVER_PORT,
                       1,
                       &FilterId[0]);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddFilter(&FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4,
                       &TEST_SUBLAYER,
                       &TEST_FLOW_CALLOUT,
                       &FWPM_CONDITION_IP_REMOTE_PORT,
                       WFPUDP_PROXY_PORT,
                       2,
                       &FilterId[1]);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddFilter(&FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4,
                       &TEST_SUBLAYER,
                       &TEST_FLOW_CALLOUT,
                       &FWPM_CONDITION_IP_LOCAL_PORT,
                       WFPUDP_SERVER_PORT,
                       3,
                       &FilterId[2]);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = AddFilter(&FWPM_LAYER_DATAGRAM_DATA_V4, &TEST_SUBLAYER, &TEST_DATAGRAM_CALLOUT, NULL, 0, 4, &FilterId[3]);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(FilterId[0] != 0 && FilterId[1] != FilterId[0] && FilterId[2] != FilterId[1] && FilterId[3] != FilterId[2],
       "Filter ids %I64u %I64u %I64u %I64u\n", FilterId[0], FilterId[1], FilterId[2], FilterId[3]);
    ok_eq_long(NotifyCount, 0L);

    Status = FwpmTransactionCommit0(EngineHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ok_eq_long(NotifyCount, 4L);
    for (Index = 0; Index < min(NotifyCount, 4); Index++)
    {
        ok_eq_int(Notifications[Index].Type, FWPS_CALLOUT_NOTIFY_ADD_FILTER);
        ok_eq_bool(Notifications[Index].HasKey, TRUE);
        ok_eq_ulonglong(Notifications[Index].Context, (ULONGLONG)Index + 1);
        ok_eq_uint(Notifications[Index].CalloutId, Index == 3 ? DatagramCalloutId : FlowCalloutId);
        ok_eq_hex(Notifications[Index].Action, (UINT32)FWP_ACTION_CALLOUT_TERMINATING);
    }

    Status = FwpsFlowAssociateContext0(0x123456789ULL, FWPS_LAYER_DATAGRAM_DATA_V4, DatagramCalloutId, 1);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    Status = FwpsFlowRemoveContext0(0x123456789ULL, FWPS_LAYER_DATAGRAM_DATA_V4, DatagramCalloutId);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
}

static
VOID
WaitForPackets(
    _In_ LONG Completions)
{
    LARGE_INTEGER Delay;
    ULONG Tries;

    Delay.QuadPart = -10 * 1000 * 10;
    for (Tries = 0; Tries < 300 && (InjectCompleteCount < Completions || PendingPackets != 0); Tries++)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}

static
VOID
CheckEvent(
    _In_ PCSTR Phase,
    _In_ LONG Index,
    _In_ UINT16 LayerId,
    _In_ UINT32 Direction,
    _In_ UINT16 LocalPort,
    _In_ UINT16 RemotePort,
    _In_ UINT64 FilterContext)
{
    PWFP_EVENT Event = &Events[Index];
    BOOLEAN Datagram = (LayerId == FWPS_LAYER_DATAGRAM_DATA_V4);
    UINT32 Required = FWPS_METADATA_FIELD_FLOW_HANDLE | FWPS_METADATA_FIELD_COMPARTMENT_ID |
                      FWPS_METADATA_FIELD_TRANSPORT_ENDPOINT_HANDLE;

    if (Index >= min(EventCount, MAX_EVENTS))
    {
        ok(FALSE, "%s[%ld]: missing event\n", Phase, Index);
        return;
    }

    if (Datagram)
    {
        Required |= FWPS_METADATA_FIELD_TRANSPORT_HEADER_SIZE;
        if (Direction == FWP_DIRECTION_INBOUND)
        {
            Required |= FWPS_METADATA_FIELD_IP_HEADER_SIZE;
        }
    }
    else
    {
        Required |= FWPS_METADATA_FIELD_PROCESS_ID | FWPS_METADATA_FIELD_PROCESS_PATH;
    }

    ok(Event->LayerId == LayerId && Event->Direction == Direction,
       "%s[%ld]: layer %u direction %lu\n", Phase, Index, Event->LayerId, Event->Direction);
    ok(Event->LocalAddress == INADDR_LOOPBACK && Event->RemoteAddress == INADDR_LOOPBACK,
       "%s[%ld]: addresses %08lx %08lx\n", Phase, Index, Event->LocalAddress, Event->RemoteAddress);
    ok(Event->LocalPort == LocalPort && Event->RemotePort == RemotePort,
       "%s[%ld]: ports %u %u, expected %u %u\n", Phase, Index, Event->LocalPort, Event->RemotePort, LocalPort, RemotePort);
    ok(Event->Protocol == IPPROTO_UDP && Event->Flags == FWP_CONDITION_FLAG_IS_LOOPBACK,
       "%s[%ld]: protocol %u flags %08lx\n", Phase, Index, Event->Protocol, Event->Flags);
    ok((Event->Metadata & Required) == Required, "%s[%ld]: metadata %08lx\n", Phase, Index, Event->Metadata);
    ok(Event->FlowHandle != 0 && Event->EndpointHandle != 0 && Event->Compartment == 1,
       "%s[%ld]: flow %I64x endpoint %I64x compartment %lu\n",
       Phase, Index, Event->FlowHandle, Event->EndpointHandle, Event->Compartment);
    ok(Event->IpHeaderSize == (Datagram && Direction == FWP_DIRECTION_INBOUND ? 20UL : 0UL) &&
       Event->TransportHeaderSize == (Datagram ? 8UL : 0UL),
       "%s[%ld]: header sizes %lu %lu\n", Phase, Index, Event->IpHeaderSize, Event->TransportHeaderSize);
    ok(Event->Irql <= DISPATCH_LEVEL, "%s[%ld]: IRQL %u\n", Phase, Index, Event->Irql);
    ok(Event->FilterContext == FilterContext && Event->FilterAction == FWP_ACTION_CALLOUT_TERMINATING &&
       Event->FilterCallout == (Datagram ? DatagramCalloutId : FlowCalloutId),
       "%s[%ld]: filter %I64u action %lx callout %u\n",
       Phase, Index, Event->FilterContext, Event->FilterAction, Event->FilterCallout);
    ok(Event->SubLayerWeight == 0x100 && Event->FilterFlags == 0 && Event->WeightType == FWP_UINT64 &&
       Event->Conditions == (Datagram ? 0UL : 2UL),
       "%s[%ld]: sublayer weight %u flags %x weight type %lu conditions %lu\n",
       Phase, Index, Event->SubLayerWeight, Event->FilterFlags, Event->WeightType, Event->Conditions);
    ok(Event->Rights == FWPS_RIGHT_ACTION_WRITE && Event->Action == 0 && Event->OutFlags == 0,
       "%s[%ld]: rights %lx action %lx flags %lx\n", Phase, Index, Event->Rights, Event->Action, Event->OutFlags);
    ok(Event->ValuesValid, "%s[%ld]: unexpected incoming values, interface %lu/%lu\n",
       Phase, Index, Event->InterfaceIndex, Event->SubInterfaceIndex);
    if (!Datagram)
    {
        ok(Event->PathValid, "%s[%ld]: process path does not match the application id\n", Phase, Index);
        ok(Event->FlowContext == 0, "%s[%ld]: flow context %I64x\n", Phase, Index, Event->FlowContext);
        ok_eq_hex(Event->AssociateZeroStatus, STATUS_INVALID_PARAMETER);
        ok_eq_hex(Event->AssociateNoDeleteStatus, STATUS_INVALID_PARAMETER);
        ok_eq_hex(Event->AssociateStatus, STATUS_SUCCESS);
        ok_eq_hex(Event->AssociateAgainStatus, STATUS_OBJECT_NAME_EXISTS);
    }
}

static
VOID
CheckDatagram(
    _In_ PCSTR Phase,
    _In_ LONG Index,
    _In_ UINT32 Direction,
    _In_ UINT16 LocalPort,
    _In_ UINT16 RemotePort,
    _In_ PCSTR Payload,
    _In_ LONG FlowEvent,
    _In_ UINT16 ContextPort,
    _In_ ULONG InjectionState)
{
    PWFP_EVENT Event = &Events[Index];
    UDP_HEADER Header;

    CheckEvent(Phase, Index, FWPS_LAYER_DATAGRAM_DATA_V4, Direction, LocalPort, RemotePort, 4);
    if (Index >= min(EventCount, MAX_EVENTS))
    {
        return;
    }

    if (Direction == FWP_DIRECTION_OUTBOUND)
    {
        Header.SourcePort = RtlUshortByteSwap(LocalPort);
        Header.DestinationPort = RtlUshortByteSwap(RemotePort);
        Header.Length = RtlUshortByteSwap(sizeof(Header) + 8);
        Header.Checksum = 0;
        ok(Event->DataLength == sizeof(Header) + 8 && RtlEqualMemory(Event->Data, &Header, sizeof(Header)),
           "%s[%ld]: length %lu header %02x%02x%02x%02x%02x%02x%02x%02x\n", Phase, Index, Event->DataLength,
           Event->Data[0], Event->Data[1], Event->Data[2], Event->Data[3],
           Event->Data[4], Event->Data[5], Event->Data[6], Event->Data[7]);
    }
    else
    {
        ok(Event->DataLength == 8 && RtlEqualMemory(Event->Data, Payload, 8),
           "%s[%ld]: length %lu data %.8s, expected %s\n", Phase, Index, Event->DataLength, (PCSTR)Event->Data, Payload);
    }

    ok(Event->FlowContext == 0x1000ULL + ContextPort, "%s[%ld]: flow context %I64x\n", Phase, Index, Event->FlowContext);
    ok(Event->InjectionState == InjectionState, "%s[%ld]: injection state %lu\n", Phase, Index, Event->InjectionState);
    if (FlowEvent >= 0)
    {
        ok(Event->FlowHandle == Events[FlowEvent].FlowHandle && Event->EndpointHandle == Events[FlowEvent].EndpointHandle,
           "%s[%ld]: flow %I64x endpoint %I64x differ from event %ld\n",
           Phase, Index, Event->FlowHandle, Event->EndpointHandle, FlowEvent);
    }
}

static UINT16 ClientPort;
static UINT64 ClientFlow, ClientEndpoint, ServerFlow, ServerEndpoint;

static
VOID
TestObserved(VOID)
{
    ok_eq_long(EventCount, 8L);
    ClientPort = Events[0].LocalPort;
    ClientFlow = Events[0].FlowHandle;
    ClientEndpoint = Events[0].EndpointHandle;
    ServerFlow = Events[2].FlowHandle;
    ServerEndpoint = Events[2].EndpointHandle;
    ok(ClientFlow != ServerFlow && ClientEndpoint != ServerEndpoint,
       "Flows %I64x %I64x endpoints %I64x %I64x\n", ClientFlow, ServerFlow, ClientEndpoint, ServerEndpoint);

    CheckEvent("observe", 0, FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4, FWP_DIRECTION_OUTBOUND, ClientPort, WFPUDP_SERVER_PORT, 1);
    CheckDatagram("observe", 1, FWP_DIRECTION_OUTBOUND, ClientPort, WFPUDP_SERVER_PORT, NULL, 0, WFPUDP_SERVER_PORT, FWPS_PACKET_NOT_INJECTED);
    CheckEvent("observe", 2, FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4, FWP_DIRECTION_INBOUND, WFPUDP_SERVER_PORT, ClientPort, 3);
    CheckDatagram("observe", 3, FWP_DIRECTION_INBOUND, WFPUDP_SERVER_PORT, ClientPort, "PING0001", 2, ClientPort, FWPS_PACKET_NOT_INJECTED);
    CheckDatagram("observe", 4, FWP_DIRECTION_OUTBOUND, WFPUDP_SERVER_PORT, ClientPort, NULL, 2, ClientPort, FWPS_PACKET_NOT_INJECTED);
    CheckDatagram("observe", 5, FWP_DIRECTION_INBOUND, ClientPort, WFPUDP_SERVER_PORT, "PONG0001", 0, WFPUDP_SERVER_PORT, FWPS_PACKET_NOT_INJECTED);
    CheckDatagram("observe", 6, FWP_DIRECTION_OUTBOUND, ClientPort, WFPUDP_SERVER_PORT, NULL, 0, WFPUDP_SERVER_PORT, FWPS_PACKET_NOT_INJECTED);
    CheckDatagram("observe", 7, FWP_DIRECTION_INBOUND, WFPUDP_SERVER_PORT, ClientPort, "PING0002", 2, ClientPort, FWPS_PACKET_NOT_INJECTED);
    ok_eq_long(FlowDeleteCount, 0L);
    ok_eq_long(FlowHandleCount, 2L);
    trace("metadata %08lx/%08lx %08lx/%08lx, IRQL %u\n",
          Events[0].Metadata, Events[0].MetadataFlags, Events[1].Metadata, Events[1].MetadataFlags, Events[1].Irql);
    EventCount = 0;
}

static
VOID
TestBlocked(VOID)
{
    ok_eq_long(EventCount, 3L);
    CheckDatagram("block", 0, FWP_DIRECTION_OUTBOUND, ClientPort, WFPUDP_SERVER_PORT, NULL, -1, WFPUDP_SERVER_PORT, FWPS_PACKET_NOT_INJECTED);
    CheckDatagram("block", 1, FWP_DIRECTION_OUTBOUND, ClientPort, WFPUDP_SERVER_PORT, NULL, -1, WFPUDP_SERVER_PORT, FWPS_PACKET_NOT_INJECTED);
    CheckDatagram("block", 2, FWP_DIRECTION_INBOUND, WFPUDP_SERVER_PORT, ClientPort, "DRPI0005", -1, ClientPort, FWPS_PACKET_NOT_INJECTED);
    ok(Events[0].FlowHandle == ClientFlow && Events[2].FlowHandle == ServerFlow,
       "Flows %I64x %I64x\n", Events[0].FlowHandle, Events[2].FlowHandle);
    EventCount = 0;
}

static
VOID
TestRemove(VOID)
{
    LONG Deleted = FlowDeleteCount;
    NTSTATUS Status;

    if (FlowHandleCount == 0)
    {
        return;
    }

    FlowDeleteContext = 0;
    Status = FwpsFlowRemoveContext0(FlowHandles[0], FWPS_LAYER_DATAGRAM_DATA_V4, DatagramCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(FlowDeleteCount, Deleted + 1);
    ok_eq_uint(FlowDeleteLayer, FWPS_LAYER_DATAGRAM_DATA_V4);
    ok_eq_uint(FlowDeleteCallout, DatagramCalloutId);
    ok_eq_hex64(FlowDeleteContext, 0x1000ULL + WFPUDP_SERVER_PORT);
    Status = FwpsFlowRemoveContext0(FlowHandles[0], FWPS_LAYER_DATAGRAM_DATA_V4, DatagramCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(FlowDeleteCount, Deleted + 1);
}

static
VOID
TestRemoved(VOID)
{
    ok_eq_long(EventCount, 1L);
    CheckDatagram("removed", 0, FWP_DIRECTION_INBOUND, WFPUDP_SERVER_PORT, ClientPort, "PING0006", -1, ClientPort, FWPS_PACKET_NOT_INJECTED);
    EventCount = 0;
}

static
VOID
TestRedirected(VOID)
{
    UINT16 ProxiedPort;

    WaitForPackets(2);
    ok_eq_long(PendingPackets, 0L);
    ok_eq_long(EventCount, 8L);
    ProxiedPort = Events[0].LocalPort;

    CheckEvent("redirect", 0, FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4, FWP_DIRECTION_OUTBOUND, ProxiedPort, WFPUDP_PROXY_PORT, 2);
    CheckDatagram("redirect", 1, FWP_DIRECTION_OUTBOUND, ProxiedPort, WFPUDP_PROXY_PORT, NULL, 0, WFPUDP_PROXY_PORT, FWPS_PACKET_NOT_INJECTED);
    CheckEvent("redirect", 2, FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4, FWP_DIRECTION_OUTBOUND, ProxiedPort, WFPUDP_SERVER_PORT, 1);
    CheckDatagram("redirect", 3, FWP_DIRECTION_OUTBOUND, ProxiedPort, WFPUDP_SERVER_PORT, NULL, 2, WFPUDP_SERVER_PORT, FWPS_PACKET_INJECTED_BY_SELF);
    CheckEvent("redirect", 4, FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4, FWP_DIRECTION_INBOUND, WFPUDP_SERVER_PORT, ProxiedPort, 3);
    CheckDatagram("redirect", 5, FWP_DIRECTION_INBOUND, WFPUDP_SERVER_PORT, ProxiedPort, "PING0003", 4, ProxiedPort, FWPS_PACKET_INJECTED_BY_SELF);
    CheckDatagram("redirect", 6, FWP_DIRECTION_OUTBOUND, WFPUDP_SERVER_PORT, ProxiedPort, NULL, 4, ProxiedPort, FWPS_PACKET_NOT_INJECTED);
    CheckDatagram("redirect", 7, FWP_DIRECTION_INBOUND, ProxiedPort, WFPUDP_SERVER_PORT, "PONG0003", 2, WFPUDP_SERVER_PORT, FWPS_PACKET_NOT_INJECTED);
    ok(Events[0].EndpointHandle == Events[2].EndpointHandle && Events[0].FlowHandle != Events[2].FlowHandle,
       "Endpoints %I64x %I64x flows %I64x %I64x\n",
       Events[0].EndpointHandle, Events[2].EndpointHandle, Events[0].FlowHandle, Events[2].FlowHandle);

    ok_eq_hex(CloneStatus[0], STATUS_SUCCESS);
    ok_eq_hex(CloneStatus[1], STATUS_SUCCESS);
    ok_eq_uint(ClonedPort[0], WFPUDP_PROXY_PORT);
    ok_eq_uint(ClonedPort[1], WFPUDP_SERVER_PORT);
    ok_eq_hex(InjectStatus[0], STATUS_SUCCESS);
    ok_eq_hex(InjectStatus[1], STATUS_SUCCESS);
    ok_eq_hex(ConstructStatus, STATUS_SUCCESS);
    ok_eq_long(InjectCompleteCount, 2L);
    ok_eq_hex(CompleteStatus[0], STATUS_SUCCESS);
    ok_eq_hex(CompleteStatus[1], STATUS_DATA_NOT_ACCEPTED);
    ok_eq_long(FlowHandleCount, 5L);
    EventCount = 0;
}

static
VOID
TestRedirectLocal(
    _In_ ULONG Address)
{
    LocalAddress = Address;
    InjectCompleteCount = 0;
    RtlZeroMemory(CompleteStatus, sizeof(CompleteStatus));
    RtlZeroMemory(InjectStatus, sizeof(InjectStatus));
    ConstructStatus = STATUS_UNSUCCESSFUL;
    EventCount = 0;
    Redirect = TRUE;
}

static
VOID
TestRedirectedLocal(VOID)
{
    static const ULONG States[] =
    {
        FWPS_PACKET_NOT_INJECTED, FWPS_PACKET_NOT_INJECTED, FWPS_PACKET_NOT_INJECTED, FWPS_PACKET_INJECTED_BY_SELF,
        FWPS_PACKET_NOT_INJECTED, FWPS_PACKET_INJECTED_BY_SELF, FWPS_PACKET_NOT_INJECTED, FWPS_PACKET_NOT_INJECTED,
        FWPS_PACKET_INJECTED_BY_SELF
    };
    PWFP_EVENT Event;
    LONG Index;

    WaitForPackets(2);
    Redirect = FALSE;
    ok_eq_long(PendingPackets, 0L);
    ok_eq_long(EventCount, 9L);
    for (Index = 0; Index < min(EventCount, (LONG)RTL_NUMBER_OF(States)); Index++)
    {
        Event = &Events[Index];
        ok(Event->LocalAddress == LocalAddress && Event->RemoteAddress == LocalAddress,
           "local[%ld]: addresses %08lx %08lx\n", Index, Event->LocalAddress, Event->RemoteAddress);
        ok(Index == 5 || Event->InjectionState == States[Index],
           "local[%ld]: injection state %lu\n", Index, Event->InjectionState);
    }

    if (EventCount >= 9)
    {
        Event = &Events[8];
        ok(Event->LayerId == FWPS_LAYER_DATAGRAM_DATA_V4 && Event->Direction == FWP_DIRECTION_INBOUND,
           "local[8]: layer %u direction %lu\n", Event->LayerId, Event->Direction);
        ok(Event->LocalPort == Events[0].LocalPort && Event->RemotePort == WFPUDP_PROXY_PORT,
           "local[8]: ports %u %u\n", Event->LocalPort, Event->RemotePort);
        ok(Event->FlowHandle == Events[0].FlowHandle && Event->FlowContext == 0x1000ULL + WFPUDP_PROXY_PORT,
           "local[8]: flow %I64x context %I64x\n", Event->FlowHandle, Event->FlowContext);
        ok(Event->DataLength == 8 && RtlEqualMemory(Event->Data, "PONG0007", 8),
           "local[8]: length %lu data %.8s\n", Event->DataLength, (PCSTR)Event->Data);
    }

    ok_eq_hex(InjectStatus[0], STATUS_SUCCESS);
    ok_eq_hex(InjectStatus[1], STATUS_SUCCESS);
    ok_eq_hex(ConstructStatus, STATUS_SUCCESS);
    ok_eq_long(InjectCompleteCount, 2L);
    ok_eq_hex(CompleteStatus[0], STATUS_SUCCESS);
    ok_eq_hex(CompleteStatus[1], STATUS_SUCCESS);
    EventCount = 0;
}

static
VOID
TestClosed(VOID)
{
    LARGE_INTEGER Delay;
    ULONG Tries;

    Delay.QuadPart = -10 * 1000 * 10;
    for (Tries = 0; Tries < 300 && FlowDeleteCount != FlowHandleCount; Tries++)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    ok_eq_long(EventCount, 0L);
    ok_eq_long(FlowDeleteCount, FlowHandleCount);
}

static
VOID
TestTeardown(VOID)
{
    NTSTATUS Status;
    LONG Index;

    if (EngineHandle == NULL)
    {
        return;
    }

    WaitForPackets(0);
    Redirect = FALSE;

    Status = FwpmEngineClose0(EngineHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    EngineHandle = NULL;
    ok_eq_long(NotifyCount, 8L);
    for (Index = 4; Index < min(NotifyCount, MAX_NOTIFY); Index++)
    {
        ok_eq_int(Notifications[Index].Type, FWPS_CALLOUT_NOTIFY_DELETE_FILTER);
    }

    Status = FwpsCalloutUnregisterById0(DatagramCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = FwpsCalloutUnregisterById0(FlowCalloutId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = FwpsCalloutUnregisterById0(FlowCalloutId);
    ok_eq_hex(Status, STATUS_FWP_CALLOUT_NOT_FOUND);

    Status = FwpsInjectionHandleDestroy0(InjectionHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InjectionHandle = NULL;
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

    *DeviceName = L"WfpUdp";
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
    UNREFERENCED_PARAMETER(OutLength);

    switch (ControlCode)
    {
        case IOCTL_WFPUDP_REDIRECT_LOCAL:
            if (Buffer == NULL || InLength != sizeof(ULONG))
            {
                return STATUS_INVALID_PARAMETER;
            }
            TestRedirectLocal(*(PULONG)Buffer);
            break;

        case IOCTL_WFPUDP_REDIRECTED_LOCAL:
            TestRedirectedLocal();
            break;

        case IOCTL_WFPUDP_SETUP:
            TestSetup(DeviceObject);
            break;

        case IOCTL_WFPUDP_OBSERVED:
            TestObserved();
            break;

        case IOCTL_WFPUDP_BLOCKED:
            TestBlocked();
            break;

        case IOCTL_WFPUDP_REMOVE:
            TestRemove();
            break;

        case IOCTL_WFPUDP_REMOVED:
            TestRemoved();
            break;

        case IOCTL_WFPUDP_REDIRECT:
            Redirect = TRUE;
            break;

        case IOCTL_WFPUDP_REDIRECTED:
            TestRedirected();
            break;

        case IOCTL_WFPUDP_CLOSED:
            TestClosed();
            break;

        case IOCTL_WFPUDP_TEARDOWN:
            TestTeardown();
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}
