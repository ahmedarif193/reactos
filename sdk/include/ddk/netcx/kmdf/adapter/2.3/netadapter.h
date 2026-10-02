/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx 2.3 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NETADAPTER_2_3_H_
#define _NETADAPTER_2_3_H_

#ifndef WDF_EXTERN_C
  #ifdef __cplusplus
    #define WDF_EXTERN_C       extern "C"
    #define WDF_EXTERN_C_START extern "C" {
    #define WDF_EXTERN_C_END   }
  #else
    #define WDF_EXTERN_C
    #define WDF_EXTERN_C_START
    #define WDF_EXTERN_C_END
  #endif
#endif

WDF_EXTERN_C_START

typedef enum _NET_ADAPTER_PAUSE_FUNCTION_TYPE {
    NetAdapterPauseFunctionTypeUnsupported = 0,
    NetAdapterPauseFunctionTypeSendOnly,
    NetAdapterPauseFunctionTypeReceiveOnly,
    NetAdapterPauseFunctionTypeSendAndReceive,
    NetAdapterPauseFunctionTypeUnknown,
} NET_ADAPTER_PAUSE_FUNCTION_TYPE;

typedef enum _NET_ADAPTER_AUTO_NEGOTIATION_FLAGS {
    NetAdapterAutoNegotiationFlagNone = 0x00000000,
    NetAdapterAutoNegotiationFlagXmitLinkSpeedAutoNegotiated = 0x00000001,
    NetAdapterAutoNegotiationFlagRcvLinkSpeedautoNegotiated = 0x00000002,
    NetAdapterAutoNegotiationFlagDuplexAutoNegotiated = 0x00000004,
    NetAdapterAutoNegotiationFlagPauseFunctionsAutoNegotiated = 0x00000008,
} NET_ADAPTER_AUTO_NEGOTIATION_FLAGS;

typedef enum _NET_MEMORY_MAPPING_REQUIREMENT {
    NetMemoryMappingRequirementNone = 0,
    NetMemoryMappingRequirementDmaMapped,
} NET_MEMORY_MAPPING_REQUIREMENT;

typedef enum _NET_ADAPTER_WAKE_PATTERN_ID {
    NetAdapterWakeMagicPatternId = 0x0000fffe,
    NetAdapterWakeEapolPatternId = 0x0000fffd,
    NetAdapterWakeFilterPatternId = 0x0000fffc,
} NET_ADAPTER_WAKE_PATTERN_ID;

typedef enum _NET_PACKET_FILTER_FLAGS {
    NetPacketFilterFlagDirected = 0x00000001,
    NetPacketFilterFlagMulticast = 0x00000002,
    NetPacketFilterFlagAllMulticast = 0x00000004,
    NetPacketFilterFlagBroadcast = 0x00000008,
    NetPacketFilterFlagPromiscuous = 0x00000020,
} NET_PACKET_FILTER_FLAGS;

#include <netreceivescaling.h>

#include <net/returncontexttypes.h>

DEFINE_ENUM_FLAG_OPERATORS(NET_ADAPTER_PAUSE_FUNCTION_TYPE);
DEFINE_ENUM_FLAG_OPERATORS(NET_ADAPTER_AUTO_NEGOTIATION_FLAGS);
DEFINE_ENUM_FLAG_OPERATORS(NET_PACKET_FILTER_FLAGS);

DECLARE_HANDLE(NETRECEIVEFILTER);

typedef
_Function_class_(EVT_NET_ADAPTER_CREATE_TXQUEUE)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
EVT_NET_ADAPTER_CREATE_TXQUEUE(
    _In_
    NETADAPTER Adapter,
    _Inout_
    NETTXQUEUE_INIT * TxQueueInit
);

typedef EVT_NET_ADAPTER_CREATE_TXQUEUE *PFN_NET_ADAPTER_CREATE_TXQUEUE;

typedef
_Function_class_(EVT_NET_ADAPTER_CREATE_RXQUEUE)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
EVT_NET_ADAPTER_CREATE_RXQUEUE(
    _In_
    NETADAPTER Adapter,
    _Inout_
    NETRXQUEUE_INIT * RxQueueInit
);

typedef EVT_NET_ADAPTER_CREATE_RXQUEUE *PFN_NET_ADAPTER_CREATE_RXQUEUE;

typedef struct _NET_ADAPTER_LINK_LAYER_ADDRESS
{
    _Field_range_(0, 32) USHORT Length;
    UCHAR Address[32];
} NET_ADAPTER_LINK_LAYER_ADDRESS;

inline
void
NET_ADAPTER_LINK_LAYER_ADDRESS_INIT(
    _Out_ NET_ADAPTER_LINK_LAYER_ADDRESS * LinkLayerAddress,
    _In_range_(1,32) USHORT Length,
    _In_reads_bytes_(Length) UCHAR const * AddressBuffer
)
{
    RtlZeroMemory(LinkLayerAddress, sizeof(NET_ADAPTER_LINK_LAYER_ADDRESS));
    LinkLayerAddress->Length = Length;

    RtlCopyMemory(
        LinkLayerAddress->Address,
        AddressBuffer,
        Length < sizeof(LinkLayerAddress->Address) ? Length : sizeof(LinkLayerAddress->Address));
}

typedef struct _NET_ADAPTER_LINK_LAYER_CAPABILITIES {

    ULONG                           Size;
    ULONG64                         MaxTxLinkSpeed;
    ULONG64                         MaxRxLinkSpeed;
} NET_ADAPTER_LINK_LAYER_CAPABILITIES;

inline
void
NET_ADAPTER_LINK_LAYER_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_LINK_LAYER_CAPABILITIES * LinkLayerCapabilities,
    _In_  ULONG64                        MaxTxLinkSpeed,
    _In_  ULONG64                        MaxRxLinkSpeed
)
{
    RtlZeroMemory(LinkLayerCapabilities, sizeof(NET_ADAPTER_LINK_LAYER_CAPABILITIES));
    LinkLayerCapabilities->Size = sizeof(NET_ADAPTER_LINK_LAYER_CAPABILITIES);
    LinkLayerCapabilities->MaxTxLinkSpeed = MaxTxLinkSpeed;
    LinkLayerCapabilities->MaxRxLinkSpeed = MaxRxLinkSpeed;
}

typedef
_Function_class_(EVT_NET_ADAPTER_RETURN_RX_BUFFER)
_IRQL_requires_same_
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
void
NTAPI
EVT_NET_ADAPTER_RETURN_RX_BUFFER(
    _In_
    NETADAPTER Adapter,
    _In_
    NET_FRAGMENT_RETURN_CONTEXT_HANDLE RxReturnContext
);

typedef EVT_NET_ADAPTER_RETURN_RX_BUFFER *PFN_NET_ADAPTER_RETURN_RX_BUFFER;

typedef struct _NET_ADAPTER_POWER_OFFLOAD_ARP_CAPABILITIES
{
    ULONG Size;
    BOOLEAN ArpOffload;
    SIZE_T MaximumOffloadCount;
} NET_ADAPTER_POWER_OFFLOAD_ARP_CAPABILITIES;

inline
void
NET_ADAPTER_POWER_OFFLOAD_ARP_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_POWER_OFFLOAD_ARP_CAPABILITIES * Capabilities,
    _In_ SIZE_T MaximumOffloadCount
)
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Size = sizeof(*Capabilities);
    Capabilities->ArpOffload = TRUE;
    Capabilities->MaximumOffloadCount = MaximumOffloadCount;
}

typedef struct _NET_ADAPTER_POWER_OFFLOAD_NS_CAPABILITIES
{
    ULONG Size;
    BOOLEAN NSOffload;
    SIZE_T MaximumOffloadCount;
} NET_ADAPTER_POWER_OFFLOAD_NS_CAPABILITIES;

inline
void
NET_ADAPTER_POWER_OFFLOAD_NS_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_POWER_OFFLOAD_NS_CAPABILITIES * Capabilities,
    _In_ SIZE_T MaximumOffloadCount
)
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Size = sizeof(*Capabilities);
    Capabilities->NSOffload = TRUE;
    Capabilities->MaximumOffloadCount = MaximumOffloadCount;
}

typedef struct _NET_ADAPTER_WAKE_BITMAP_CAPABILITIES
{
    ULONG Size;
    BOOLEAN BitmapPattern;
    SIZE_T MaximumPatternCount;
    SIZE_T MaximumPatternSize;
} NET_ADAPTER_WAKE_BITMAP_CAPABILITIES;

inline
void
NET_ADAPTER_WAKE_BITMAP_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_WAKE_BITMAP_CAPABILITIES * Capabilities
)
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Size = sizeof(*Capabilities);
}

typedef struct _NET_ADAPTER_WAKE_MEDIA_CHANGE_CAPABILITIES
{
    ULONG Size;
    BOOLEAN MediaConnect;
    BOOLEAN MediaDisconnect;
} NET_ADAPTER_WAKE_MEDIA_CHANGE_CAPABILITIES;

inline
void
NET_ADAPTER_WAKE_MEDIA_CHANGE_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_WAKE_MEDIA_CHANGE_CAPABILITIES * Capabilities
)
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Size = sizeof(*Capabilities);
}

typedef struct _NET_ADAPTER_WAKE_MAGIC_PACKET_CAPABILITIES
{
    ULONG Size;
    BOOLEAN MagicPacket;
} NET_ADAPTER_WAKE_MAGIC_PACKET_CAPABILITIES;

inline
void
NET_ADAPTER_WAKE_MAGIC_PACKET_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_WAKE_MAGIC_PACKET_CAPABILITIES * Capabilities
)
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Size = sizeof(*Capabilities);
}

typedef struct _NET_ADAPTER_WAKE_EAPOL_PACKET_CAPABILITIES
{
    ULONG Size;
    BOOLEAN EapolPacket;
} NET_ADAPTER_WAKE_EAPOL_PACKET_CAPABILITIES;

inline
void
NET_ADAPTER_WAKE_EAPOL_PACKET_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_WAKE_EAPOL_PACKET_CAPABILITIES * Capabilities
)
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Size = sizeof(*Capabilities);
}

typedef struct _NET_ADAPTER_WAKE_PACKET_FILTER_CAPABILITIES
{
    ULONG Size;
    BOOLEAN PacketFilterMatch;
} NET_ADAPTER_WAKE_PACKET_FILTER_CAPABILITIES;

inline
void
NET_ADAPTER_WAKE_PACKET_FILTER_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_WAKE_PACKET_FILTER_CAPABILITIES * Capabilities
)
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Size = sizeof(*Capabilities);
}

#define NET_ADAPTER_FRAGMENT_DEFAULT_ALIGNMENT 1

typedef struct _NET_ADAPTER_DMA_CAPABILITIES
{
    ULONG Size;

    WDFDMAENABLER DmaEnabler;

    PHYSICAL_ADDRESS MaximumPhysicalAddress;

    WDF_TRI_STATE CacheEnabled;

    NODE_REQUIREMENT PreferredNode;

} NET_ADAPTER_DMA_CAPABILITIES;

inline
void
NET_ADAPTER_DMA_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_DMA_CAPABILITIES * DmaCapabilities,
    _In_ WDFDMAENABLER DmaEnabler
)
{
    RtlZeroMemory(DmaCapabilities, sizeof(NET_ADAPTER_DMA_CAPABILITIES));
    DmaCapabilities->Size = sizeof(NET_ADAPTER_DMA_CAPABILITIES);
    DmaCapabilities->DmaEnabler = DmaEnabler;
    DmaCapabilities->CacheEnabled = WdfUseDefault;
    DmaCapabilities->PreferredNode = MM_ANY_NODE_OK;
}

typedef enum _NET_RX_FRAGMENT_BUFFER_ALLOCATION_MODE
{

    NetRxFragmentBufferAllocationModeSystem = 0,

    NetRxFragmentBufferAllocationModeDriver,
} NET_RX_FRAGMENT_BUFFER_ALLOCATION_MODE;

typedef enum _NET_RX_FRAGMENT_BUFFER_ATTACHMENT_MODE
{

    NetRxFragmentBufferAttachmentModeSystem = 0,

    NetRxFragmentBufferAttachmentModeDriver,
} NET_RX_FRAGMENT_BUFFER_ATTACHMENT_MODE;

#pragma warning(push)
#pragma warning(disable:4201)

typedef struct _NET_ADAPTER_RX_CAPABILITIES
{
    ULONG Size;

    NET_RX_FRAGMENT_BUFFER_ALLOCATION_MODE AllocationMode;

    NET_RX_FRAGMENT_BUFFER_ATTACHMENT_MODE AttachmentMode;

    UINT32 FragmentRingNumberOfElementsHint;

    SIZE_T MaximumFrameSize;

    SIZE_T MaximumNumberOfQueues;

    union
    {

        struct
        {
            PFN_NET_ADAPTER_RETURN_RX_BUFFER EvtAdapterReturnRxBuffer;
        }
        DUMMYSTRUCTNAME;

        struct
        {

            NET_MEMORY_MAPPING_REQUIREMENT MappingRequirement;

            SIZE_T FragmentBufferAlignment;

            NET_ADAPTER_DMA_CAPABILITIES * DmaCapabilities;
        }
        DUMMYSTRUCTNAME2;
    }
    DUMMYUNIONNAME;

} NET_ADAPTER_RX_CAPABILITIES;

#pragma warning(pop)

inline
void
NET_ADAPTER_RX_CAPABILITIES_INIT_DRIVER_MANAGED(
    _Out_ NET_ADAPTER_RX_CAPABILITIES * RxCapabilities,
    _In_ PFN_NET_ADAPTER_RETURN_RX_BUFFER EvtAdapterReturnRxBuffer,
    _In_ SIZE_T MaximumFrameSize,
    _In_ SIZE_T MaximumNumberOfQueues
)
{
    RtlZeroMemory(RxCapabilities, sizeof(NET_ADAPTER_RX_CAPABILITIES));
    RxCapabilities->Size = sizeof(NET_ADAPTER_RX_CAPABILITIES);
    RxCapabilities->FragmentBufferAlignment = NET_ADAPTER_FRAGMENT_DEFAULT_ALIGNMENT;
    RxCapabilities->MaximumFrameSize = MaximumFrameSize;
    RxCapabilities->MaximumNumberOfQueues = MaximumNumberOfQueues;

    RxCapabilities->AllocationMode = NetRxFragmentBufferAllocationModeDriver;
    RxCapabilities->AttachmentMode = NetRxFragmentBufferAttachmentModeDriver;
    RxCapabilities->EvtAdapterReturnRxBuffer = EvtAdapterReturnRxBuffer;
}

inline
void
NET_ADAPTER_RX_CAPABILITIES_INIT_SYSTEM_MANAGED(
    _Out_ NET_ADAPTER_RX_CAPABILITIES * RxCapabilities,
    _In_ SIZE_T MaximumFrameSize,
    _In_ SIZE_T MaximumNumberOfQueues
)
{
    RtlZeroMemory(RxCapabilities, sizeof(NET_ADAPTER_RX_CAPABILITIES));
    RxCapabilities->Size = sizeof(NET_ADAPTER_RX_CAPABILITIES);
    RxCapabilities->FragmentBufferAlignment = NET_ADAPTER_FRAGMENT_DEFAULT_ALIGNMENT;
    RxCapabilities->MaximumFrameSize = MaximumFrameSize;
    RxCapabilities->MaximumNumberOfQueues = MaximumNumberOfQueues;

    RxCapabilities->AllocationMode = NetRxFragmentBufferAllocationModeSystem;
    RxCapabilities->AttachmentMode = NetRxFragmentBufferAttachmentModeSystem;
    RxCapabilities->MappingRequirement = NetMemoryMappingRequirementNone;
}

inline
void
NET_ADAPTER_RX_CAPABILITIES_INIT_SYSTEM_MANAGED_DMA(
    _Out_ NET_ADAPTER_RX_CAPABILITIES * RxCapabilities,
    _In_ NET_ADAPTER_DMA_CAPABILITIES * DmaCapabilities,
    _In_ SIZE_T MaximumFrameSize,
    _In_ SIZE_T MaximumNumberOfQueues
)
{
    RtlZeroMemory(RxCapabilities, sizeof(NET_ADAPTER_RX_CAPABILITIES));
    RxCapabilities->Size = sizeof(NET_ADAPTER_RX_CAPABILITIES);
    RxCapabilities->FragmentBufferAlignment = NET_ADAPTER_FRAGMENT_DEFAULT_ALIGNMENT;
    RxCapabilities->MaximumFrameSize = MaximumFrameSize;
    RxCapabilities->MaximumNumberOfQueues = MaximumNumberOfQueues;

    RxCapabilities->AllocationMode = NetRxFragmentBufferAllocationModeSystem;
    RxCapabilities->AttachmentMode = NetRxFragmentBufferAttachmentModeSystem;
    RxCapabilities->MappingRequirement = NetMemoryMappingRequirementDmaMapped;
    RxCapabilities->DmaCapabilities = DmaCapabilities;
}

typedef struct _NET_ADAPTER_TX_CAPABILITIES
{
    ULONG Size;

    NET_MEMORY_MAPPING_REQUIREMENT MappingRequirement;

    SIZE_T PayloadBackfill;

    SIZE_T MaximumNumberOfFragments;

    SIZE_T FragmentBufferAlignment;

    UINT32 FragmentRingNumberOfElementsHint;

    SIZE_T MaximumNumberOfQueues;

    NET_ADAPTER_DMA_CAPABILITIES * DmaCapabilities;

} NET_ADAPTER_TX_CAPABILITIES;

inline
void
NET_ADAPTER_TX_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_TX_CAPABILITIES * TxCapabilities,
    _In_ SIZE_T MaximumNumberOfQueues
)
{
    RtlZeroMemory(TxCapabilities, sizeof(NET_ADAPTER_TX_CAPABILITIES));
    TxCapabilities->Size = sizeof(NET_ADAPTER_TX_CAPABILITIES);
    TxCapabilities->FragmentBufferAlignment = NET_ADAPTER_FRAGMENT_DEFAULT_ALIGNMENT;
    TxCapabilities->MaximumNumberOfQueues = MaximumNumberOfQueues;
    TxCapabilities->MaximumNumberOfFragments = (SIZE_T)-1;
}

inline
void
NET_ADAPTER_TX_CAPABILITIES_INIT_FOR_DMA(
    _Out_ NET_ADAPTER_TX_CAPABILITIES * TxCapabilities,
    _In_ NET_ADAPTER_DMA_CAPABILITIES * DmaCapabilities,
    _In_ SIZE_T MaximumNumberOfQueues
)
{
    NET_ADAPTER_TX_CAPABILITIES_INIT(
        TxCapabilities,
        MaximumNumberOfQueues);

    TxCapabilities->DmaCapabilities = DmaCapabilities;
    TxCapabilities->MappingRequirement = NetMemoryMappingRequirementDmaMapped;
}

typedef struct _NET_ADAPTER_LINK_STATE {

    ULONG                                Size;

    ULONG64                              TxLinkSpeed;
    ULONG64                              RxLinkSpeed;

    NET_IF_MEDIA_CONNECT_STATE           MediaConnectState;

    NET_IF_MEDIA_DUPLEX_STATE            MediaDuplexState;

    NET_ADAPTER_PAUSE_FUNCTION_TYPE          SupportedPauseFunctions;

    NET_ADAPTER_AUTO_NEGOTIATION_FLAGS   AutoNegotiationFlags;

} NET_ADAPTER_LINK_STATE;

inline
void
NET_ADAPTER_LINK_STATE_INIT(
    _Out_ NET_ADAPTER_LINK_STATE *             LinkState,
    _In_  ULONG64                              LinkSpeed,
    _In_  NET_IF_MEDIA_CONNECT_STATE           MediaConnectState,
    _In_  NET_IF_MEDIA_DUPLEX_STATE            MediaDuplexState,
    _In_  NET_ADAPTER_PAUSE_FUNCTION_TYPE          SupportedPauseFunctions,
    _In_  NET_ADAPTER_AUTO_NEGOTIATION_FLAGS   AutoNegotiationFlags
)
{
    RtlZeroMemory(LinkState, sizeof(NET_ADAPTER_LINK_STATE));
    LinkState->Size = sizeof(NET_ADAPTER_LINK_STATE);
    LinkState->TxLinkSpeed = LinkSpeed;
    LinkState->RxLinkSpeed = LinkSpeed;
    LinkState->MediaConnectState = MediaConnectState;
    LinkState->MediaDuplexState = MediaDuplexState;
    LinkState->SupportedPauseFunctions = SupportedPauseFunctions;
    LinkState->AutoNegotiationFlags = AutoNegotiationFlags;
}

#define NET_ADAPTER_LINK_SPEED_UNKNOWN ((ULONG64)(-1))

inline
void
NET_ADAPTER_LINK_STATE_INIT_DISCONNECTED(
    _Out_ NET_ADAPTER_LINK_STATE *             LinkState
)
{
    RtlZeroMemory(LinkState, sizeof(NET_ADAPTER_LINK_STATE));
    LinkState->Size = sizeof(NET_ADAPTER_LINK_STATE);

    LinkState->MediaConnectState = MediaConnectStateDisconnected;

    LinkState->TxLinkSpeed = NET_ADAPTER_LINK_SPEED_UNKNOWN;
    LinkState->RxLinkSpeed = NET_ADAPTER_LINK_SPEED_UNKNOWN;
    LinkState->MediaDuplexState = MediaDuplexStateUnknown;
    LinkState->SupportedPauseFunctions = NetAdapterPauseFunctionTypeUnsupported;
}

typedef struct _NET_ADAPTER_DATAPATH_CALLBACKS {

    ULONG                                     Size;

    PFN_NET_ADAPTER_CREATE_TXQUEUE            EvtAdapterCreateTxQueue;
    PFN_NET_ADAPTER_CREATE_RXQUEUE            EvtAdapterCreateRxQueue;

} NET_ADAPTER_DATAPATH_CALLBACKS;

inline
void
NET_ADAPTER_DATAPATH_CALLBACKS_INIT(
    _Out_ NET_ADAPTER_DATAPATH_CALLBACKS *        DatapathCallbacks,
    _In_  PFN_NET_ADAPTER_CREATE_TXQUEUE          EvtAdapterCreateTxQueue,
    _In_  PFN_NET_ADAPTER_CREATE_RXQUEUE          EvtAdapterCreateRxQueue
)
{
    RtlZeroMemory(DatapathCallbacks,
        sizeof(NET_ADAPTER_DATAPATH_CALLBACKS));
    DatapathCallbacks->Size = sizeof(NET_ADAPTER_DATAPATH_CALLBACKS);
    DatapathCallbacks->EvtAdapterCreateTxQueue = EvtAdapterCreateTxQueue;
    DatapathCallbacks->EvtAdapterCreateRxQueue = EvtAdapterCreateRxQueue;
}

typedef
_Function_class_(EVT_NET_ADAPTER_OFFLOAD_SET_CHECKSUM)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
void
NTAPI
EVT_NET_ADAPTER_OFFLOAD_SET_CHECKSUM(
    _In_
    NETADAPTER Adapter,
    _In_
    NETOFFLOAD Offload
);

typedef EVT_NET_ADAPTER_OFFLOAD_SET_CHECKSUM *PFN_NET_ADAPTER_OFFLOAD_SET_CHECKSUM;

typedef struct _NET_ADAPTER_OFFLOAD_CHECKSUM_CAPABILITIES
{

    ULONG Size;

    BOOLEAN IPv4;

    BOOLEAN Tcp;

    BOOLEAN Udp;

    PFN_NET_ADAPTER_OFFLOAD_SET_CHECKSUM
        EvtAdapterOffloadSetChecksum;

} NET_ADAPTER_OFFLOAD_CHECKSUM_CAPABILITIES;

inline
void
NET_ADAPTER_OFFLOAD_CHECKSUM_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_OFFLOAD_CHECKSUM_CAPABILITIES * ChecksumCapabilities,
    _In_  BOOLEAN IPv4,
    _In_  BOOLEAN Tcp,
    _In_  BOOLEAN Udp,
    _In_ PFN_NET_ADAPTER_OFFLOAD_SET_CHECKSUM EvtAdapterOffloadSetChecksum
)
{
    RtlZeroMemory(ChecksumCapabilities, sizeof(NET_ADAPTER_OFFLOAD_CHECKSUM_CAPABILITIES));
    ChecksumCapabilities->Size = sizeof(NET_ADAPTER_OFFLOAD_CHECKSUM_CAPABILITIES);

    ChecksumCapabilities->IPv4 = IPv4;
    ChecksumCapabilities->Tcp = Tcp;
    ChecksumCapabilities->Udp = Udp;
    ChecksumCapabilities->EvtAdapterOffloadSetChecksum = EvtAdapterOffloadSetChecksum;
}

typedef struct _NET_ADAPTER_WAKE_REASON_PACKET
{
    ULONG Size;
    ULONG PatternId;
    ULONG OriginalPacketSize;
    WDFMEMORY WakePacket;
} NET_ADAPTER_WAKE_REASON_PACKET;

inline
void
NET_ADAPTER_WAKE_REASON_PACKET_INIT(
    _Out_ NET_ADAPTER_WAKE_REASON_PACKET * Reason
)
{
    RtlZeroMemory(Reason, sizeof(*Reason));
    Reason->Size = sizeof(*Reason);
}

inline
void
NET_ADAPTER_WAKE_REASON_MAGIC_PACKET_INIT(
    _Out_ NET_ADAPTER_WAKE_REASON_PACKET * Reason
)
{
    RtlZeroMemory(Reason, sizeof(*Reason));
    Reason->Size = sizeof(*Reason);
    Reason->PatternId = NetAdapterWakeMagicPatternId;
}

inline
void
NET_ADAPTER_WAKE_REASON_EAPOL_PACKET_INIT(
    _Out_ NET_ADAPTER_WAKE_REASON_PACKET * Reason
)
{
    RtlZeroMemory(Reason, sizeof(*Reason));
    Reason->Size = sizeof(*Reason);
    Reason->PatternId = NetAdapterWakeEapolPatternId;
}

inline
void
NET_ADAPTER_WAKE_REASON_FILTER_PACKET_INIT(
    _Out_ NET_ADAPTER_WAKE_REASON_PACKET * Reason
)
{
    RtlZeroMemory(Reason, sizeof(*Reason));
    Reason->Size = sizeof(*Reason);
    Reason->PatternId = NetAdapterWakeFilterPatternId;
}

typedef
_Function_class_(EVT_NET_ADAPTER_SET_RECEIVE_FILTER)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
void
NTAPI
EVT_NET_ADAPTER_SET_RECEIVE_FILTER(
    _In_
    NETADAPTER Adapter,
    _In_
    NETRECEIVEFILTER
);

typedef EVT_NET_ADAPTER_SET_RECEIVE_FILTER *PFN_NET_ADAPTER_SET_RECEIVE_FILTER;

typedef struct _NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES
{
    ULONG Size;
    NET_PACKET_FILTER_FLAGS SupportedPacketFilters;
    SIZE_T MaximumMulticastAddresses;
    PFN_NET_ADAPTER_SET_RECEIVE_FILTER EvtSetReceiveFilter;
} NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES;

inline
void
NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES * Capabilities,
    _In_  PFN_NET_ADAPTER_SET_RECEIVE_FILTER EvtSetReceiveFilter
)
{
    RtlZeroMemory(Capabilities, sizeof(NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES));
    Capabilities->Size = sizeof(NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES);
    Capabilities->EvtSetReceiveFilter = EvtSetReceiveFilter;
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NETADAPTER_INIT *
(NTAPI *PFN_NETADAPTERINITALLOCATE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDEVICE Device
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NETADAPTER_INIT *
NetAdapterInitAllocate(
    _In_
    WDFDEVICE Device
    )
{
    return ((PFN_NETADAPTERINITALLOCATE) NetFunctions[NetAdapterInitAllocateTableIndex])(NetDriverGlobals, Device);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NETADAPTER_INIT *
(NTAPI *PFN_NETADAPTERLIGHTWEIGHTINITALLOCATE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    CONST GUID* NetworkInterfaceGuid
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NETADAPTER_INIT *
NetAdapterLightweightInitAllocate(
    _In_
    CONST GUID* NetworkInterfaceGuid
    )
{
    return ((PFN_NETADAPTERLIGHTWEIGHTINITALLOCATE) NetFunctions[NetAdapterLightweightInitAllocateTableIndex])(NetDriverGlobals, NetworkInterfaceGuid);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERINITFREE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER_INIT* AdapterInit
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterInitFree(
    _In_
    NETADAPTER_INIT* AdapterInit
    )
{
    ((PFN_NETADAPTERINITFREE) NetFunctions[NetAdapterInitFreeTableIndex])(NetDriverGlobals, AdapterInit);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERINITSETDATAPATHCALLBACKS)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _Inout_
    NETADAPTER_INIT* AdapterInit,
    _In_
    NET_ADAPTER_DATAPATH_CALLBACKS* DatapathCallbacks
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterInitSetDatapathCallbacks(
    _Inout_
    NETADAPTER_INIT* AdapterInit,
    _In_
    NET_ADAPTER_DATAPATH_CALLBACKS* DatapathCallbacks
    )
{
    ((PFN_NETADAPTERINITSETDATAPATHCALLBACKS) NetFunctions[NetAdapterInitSetDatapathCallbacksTableIndex])(NetDriverGlobals, AdapterInit, DatapathCallbacks);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_NETADAPTERCREATE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER_INIT* AdapterInit,
    _In_opt_
    WDF_OBJECT_ATTRIBUTES* AdapterAttributes,
    _Out_
    NETADAPTER* Adapter
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
NetAdapterCreate(
    _In_
    NETADAPTER_INIT* AdapterInit,
    _In_opt_
    WDF_OBJECT_ATTRIBUTES* AdapterAttributes,
    _Out_
    NETADAPTER* Adapter
    )
{
    return ((PFN_NETADAPTERCREATE) NetFunctions[NetAdapterCreateTableIndex])(NetDriverGlobals, AdapterInit, AdapterAttributes, Adapter);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_NETADAPTERSTART)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
NetAdapterStart(
    _In_
    NETADAPTER Adapter
    )
{
    return ((PFN_NETADAPTERSTART) NetFunctions[NetAdapterStartTableIndex])(NetDriverGlobals, Adapter);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERSTOP)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterStop(
    _In_
    NETADAPTER Adapter
    )
{
    ((PFN_NETADAPTERSTOP) NetFunctions[NetAdapterStopTableIndex])(NetDriverGlobals, Adapter);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERSETLINKLAYERCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_LINK_LAYER_CAPABILITIES* LinkLayerCapabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterSetLinkLayerCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_LINK_LAYER_CAPABILITIES* LinkLayerCapabilities
    )
{
    ((PFN_NETADAPTERSETLINKLAYERCAPABILITIES) NetFunctions[NetAdapterSetLinkLayerCapabilitiesTableIndex])(NetDriverGlobals, Adapter, LinkLayerCapabilities);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERSETLINKLAYERMTUSIZE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    ULONG MtuSize
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterSetLinkLayerMtuSize(
    _In_
    NETADAPTER Adapter,
    _In_
    ULONG MtuSize
    )
{
    ((PFN_NETADAPTERSETLINKLAYERMTUSIZE) NetFunctions[NetAdapterSetLinkLayerMtuSizeTableIndex])(NetDriverGlobals, Adapter, MtuSize);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERPOWEROFFLOADSETARPCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_POWER_OFFLOAD_ARP_CAPABILITIES* Capabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterPowerOffloadSetArpCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_POWER_OFFLOAD_ARP_CAPABILITIES* Capabilities
    )
{
    ((PFN_NETADAPTERPOWEROFFLOADSETARPCAPABILITIES) NetFunctions[NetAdapterPowerOffloadSetArpCapabilitiesTableIndex])(NetDriverGlobals, Adapter, Capabilities);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERPOWEROFFLOADSETNSCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_POWER_OFFLOAD_NS_CAPABILITIES* Capabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterPowerOffloadSetNSCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_POWER_OFFLOAD_NS_CAPABILITIES* Capabilities
    )
{
    ((PFN_NETADAPTERPOWEROFFLOADSETNSCAPABILITIES) NetFunctions[NetAdapterPowerOffloadSetNSCapabilitiesTableIndex])(NetDriverGlobals, Adapter, Capabilities);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERWAKESETBITMAPCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_BITMAP_CAPABILITIES* Capabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterWakeSetBitmapCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_BITMAP_CAPABILITIES* Capabilities
    )
{
    ((PFN_NETADAPTERWAKESETBITMAPCAPABILITIES) NetFunctions[NetAdapterWakeSetBitmapCapabilitiesTableIndex])(NetDriverGlobals, Adapter, Capabilities);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERWAKESETMEDIACHANGECAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_MEDIA_CHANGE_CAPABILITIES* Capabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterWakeSetMediaChangeCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_MEDIA_CHANGE_CAPABILITIES* Capabilities
    )
{
    ((PFN_NETADAPTERWAKESETMEDIACHANGECAPABILITIES) NetFunctions[NetAdapterWakeSetMediaChangeCapabilitiesTableIndex])(NetDriverGlobals, Adapter, Capabilities);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERWAKESETMAGICPACKETCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_MAGIC_PACKET_CAPABILITIES* Capabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterWakeSetMagicPacketCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_MAGIC_PACKET_CAPABILITIES* Capabilities
    )
{
    ((PFN_NETADAPTERWAKESETMAGICPACKETCAPABILITIES) NetFunctions[NetAdapterWakeSetMagicPacketCapabilitiesTableIndex])(NetDriverGlobals, Adapter, Capabilities);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERWAKESETEAPOLPACKETCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_EAPOL_PACKET_CAPABILITIES* Capabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterWakeSetEapolPacketCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_EAPOL_PACKET_CAPABILITIES* Capabilities
    )
{
    ((PFN_NETADAPTERWAKESETEAPOLPACKETCAPABILITIES) NetFunctions[NetAdapterWakeSetEapolPacketCapabilitiesTableIndex])(NetDriverGlobals, Adapter, Capabilities);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERWAKESETPACKETFILTERCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_PACKET_FILTER_CAPABILITIES* Capabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterWakeSetPacketFilterCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_PACKET_FILTER_CAPABILITIES* Capabilities
    )
{
    ((PFN_NETADAPTERWAKESETPACKETFILTERCAPABILITIES) NetFunctions[NetAdapterWakeSetPacketFilterCapabilitiesTableIndex])(NetDriverGlobals, Adapter, Capabilities);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERSETDATAPATHCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_TX_CAPABILITIES* TxCapabilities,
    _In_
    NET_ADAPTER_RX_CAPABILITIES* RxCapabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterSetDataPathCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_TX_CAPABILITIES* TxCapabilities,
    _In_
    NET_ADAPTER_RX_CAPABILITIES* RxCapabilities
    )
{
    ((PFN_NETADAPTERSETDATAPATHCAPABILITIES) NetFunctions[NetAdapterSetDataPathCapabilitiesTableIndex])(NetDriverGlobals, Adapter, TxCapabilities, RxCapabilities);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERSETLINKSTATE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_LINK_STATE* State
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
void
NetAdapterSetLinkState(
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_LINK_STATE* State
    )
{
    ((PFN_NETADAPTERSETLINKSTATE) NetFunctions[NetAdapterSetLinkStateTableIndex])(NetDriverGlobals, Adapter, State);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
NET_LUID
(NTAPI *PFN_NETADAPTERGETNETLUID)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
NET_LUID
NetAdapterGetNetLuid(
    _In_
    NETADAPTER Adapter
    )
{
    return ((PFN_NETADAPTERGETNETLUID) NetFunctions[NetAdapterGetNetLuidTableIndex])(NetDriverGlobals, Adapter);
}

typedef
_Must_inspect_result_
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_NETADAPTEROPENCONFIGURATION)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_opt_
    WDF_OBJECT_ATTRIBUTES* ConfigurationAttributes,
    _Out_
    NETCONFIGURATION* Configuration
    );

_Must_inspect_result_
_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
NetAdapterOpenConfiguration(
    _In_
    NETADAPTER Adapter,
    _In_opt_
    WDF_OBJECT_ATTRIBUTES* ConfigurationAttributes,
    _Out_
    NETCONFIGURATION* Configuration
    )
{
    return ((PFN_NETADAPTEROPENCONFIGURATION) NetFunctions[NetAdapterOpenConfigurationTableIndex])(NetDriverGlobals, Adapter, ConfigurationAttributes, Configuration);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERSETPERMANENTLINKLAYERADDRESS)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_LINK_LAYER_ADDRESS* LinkLayerAddress
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterSetPermanentLinkLayerAddress(
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_LINK_LAYER_ADDRESS* LinkLayerAddress
    )
{
    ((PFN_NETADAPTERSETPERMANENTLINKLAYERADDRESS) NetFunctions[NetAdapterSetPermanentLinkLayerAddressTableIndex])(NetDriverGlobals, Adapter, LinkLayerAddress);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERSETCURRENTLINKLAYERADDRESS)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_LINK_LAYER_ADDRESS* LinkLayerAddress
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterSetCurrentLinkLayerAddress(
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_LINK_LAYER_ADDRESS* LinkLayerAddress
    )
{
    ((PFN_NETADAPTERSETCURRENTLINKLAYERADDRESS) NetFunctions[NetAdapterSetCurrentLinkLayerAddressTableIndex])(NetDriverGlobals, Adapter, LinkLayerAddress);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTEROFFLOADSETCHECKSUMCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_OFFLOAD_CHECKSUM_CAPABILITIES* HardwareCapabilities
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterOffloadSetChecksumCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_OFFLOAD_CHECKSUM_CAPABILITIES* HardwareCapabilities
    )
{
    ((PFN_NETADAPTEROFFLOADSETCHECKSUMCAPABILITIES) NetFunctions[NetAdapterOffloadSetChecksumCapabilitiesTableIndex])(NetDriverGlobals, Adapter, HardwareCapabilities);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
BOOLEAN
(NTAPI *PFN_NETOFFLOADISCHECKSUMIPV4ENABLED)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETOFFLOAD Offload
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
BOOLEAN
NetOffloadIsChecksumIPv4Enabled(
    _In_
    NETOFFLOAD Offload
    )
{
    return ((PFN_NETOFFLOADISCHECKSUMIPV4ENABLED) NetFunctions[NetOffloadIsChecksumIPv4EnabledTableIndex])(NetDriverGlobals, Offload);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
BOOLEAN
(NTAPI *PFN_NETOFFLOADISCHECKSUMTCPENABLED)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETOFFLOAD Offload
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
BOOLEAN
NetOffloadIsChecksumTcpEnabled(
    _In_
    NETOFFLOAD Offload
    )
{
    return ((PFN_NETOFFLOADISCHECKSUMTCPENABLED) NetFunctions[NetOffloadIsChecksumTcpEnabledTableIndex])(NetDriverGlobals, Offload);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
BOOLEAN
(NTAPI *PFN_NETOFFLOADISCHECKSUMUDPENABLED)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETOFFLOAD Offload
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
BOOLEAN
NetOffloadIsChecksumUdpEnabled(
    _In_
    NETOFFLOAD Offload
    )
{
    return ((PFN_NETOFFLOADISCHECKSUMUDPENABLED) NetFunctions[NetOffloadIsChecksumUdpEnabledTableIndex])(NetDriverGlobals, Offload);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
VOID
(NTAPI *PFN_NETADAPTERREPORTWAKEREASONPACKET)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_REASON_PACKET* Reason
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
VOID
NetAdapterReportWakeReasonPacket(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_WAKE_REASON_PACKET* Reason
    )
{
    ((PFN_NETADAPTERREPORTWAKEREASONPACKET) NetFunctions[NetAdapterReportWakeReasonPacketTableIndex])(NetDriverGlobals, Adapter, Reason);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
VOID
(NTAPI *PFN_NETADAPTERREPORTWAKEREASONMEDIACHANGE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    NET_IF_MEDIA_CONNECT_STATE Reason
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
VOID
NetAdapterReportWakeReasonMediaChange(
    _In_
    NETADAPTER Adapter,
    _In_
    NET_IF_MEDIA_CONNECT_STATE Reason
    )
{
    ((PFN_NETADAPTERREPORTWAKEREASONMEDIACHANGE) NetFunctions[NetAdapterReportWakeReasonMediaChangeTableIndex])(NetDriverGlobals, Adapter, Reason);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERSETRECEIVEFILTERCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES* Capabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterSetReceiveFilterCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES* Capabilities
    )
{
    ((PFN_NETADAPTERSETRECEIVEFILTERCAPABILITIES) NetFunctions[NetAdapterSetReceiveFilterCapabilitiesTableIndex])(NetDriverGlobals, Adapter, Capabilities);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
NET_PACKET_FILTER_FLAGS
(NTAPI *PFN_NETRECEIVEFILTERGETPACKETFILTER)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETRECEIVEFILTER ReceiveFilter
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NET_PACKET_FILTER_FLAGS
NetReceiveFilterGetPacketFilter(
    _In_
    NETRECEIVEFILTER ReceiveFilter
    )
{
    return ((PFN_NETRECEIVEFILTERGETPACKETFILTER) NetFunctions[NetReceiveFilterGetPacketFilterTableIndex])(NetDriverGlobals, ReceiveFilter);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
SIZE_T
(NTAPI *PFN_NETRECEIVEFILTERGETMULTICASTADDRESSCOUNT)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETRECEIVEFILTER ReceiveFilter
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
SIZE_T
NetReceiveFilterGetMulticastAddressCount(
    _In_
    NETRECEIVEFILTER ReceiveFilter
    )
{
    return ((PFN_NETRECEIVEFILTERGETMULTICASTADDRESSCOUNT) NetFunctions[NetReceiveFilterGetMulticastAddressCountTableIndex])(NetDriverGlobals, ReceiveFilter);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
NET_ADAPTER_LINK_LAYER_ADDRESS const *
(NTAPI *PFN_NETRECEIVEFILTERGETMULTICASTADDRESSLIST)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETRECEIVEFILTER ReceiveFilter
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NET_ADAPTER_LINK_LAYER_ADDRESS const *
NetReceiveFilterGetMulticastAddressList(
    _In_
    NETRECEIVEFILTER ReceiveFilter
    )
{
    return ((PFN_NETRECEIVEFILTERGETMULTICASTADDRESSLIST) NetFunctions[NetReceiveFilterGetMulticastAddressListTableIndex])(NetDriverGlobals, ReceiveFilter);
}

WDF_EXTERN_C_END

#endif
