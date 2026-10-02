/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx 2.3 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NETRECEIVESCALING_2_3_H_
#define _NETRECEIVESCALING_2_3_H_

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

typedef enum _NET_ADAPTER_RECEIVE_SCALING_UNHASHED_TARGET_TYPE {

    NetAdapterReceiveScalingUnhashedTargetTypeUnspecified = 0,

    NetAdapterReceiveScalingUnhashedTargetTypeHashIndex,
} NET_ADAPTER_RECEIVE_SCALING_UNHASHED_TARGET_TYPE;

typedef enum _NET_ADAPTER_RECEIVE_SCALING_HASH_TYPE {
    NetAdapterReceiveScalingHashTypeNone = 0x00000000,

    NetAdapterReceiveScalingHashTypeToeplitz = 0x00000001,
} NET_ADAPTER_RECEIVE_SCALING_HASH_TYPE;

typedef enum _NET_ADAPTER_RECEIVE_SCALING_PROTOCOL_TYPE {
    NetAdapterReceiveScalingProtocolTypeNone = 0x00000000,

    NetAdapterReceiveScalingProtocolTypeIPv4 = 0x00000001,

    NetAdapterReceiveScalingProtocolTypeIPv4Options = 0x00000002,

    NetAdapterReceiveScalingProtocolTypeIPv6 = 0x00000004,

    NetAdapterReceiveScalingProtocolTypeIPv6Extensions = 0x00000008,

    NetAdapterReceiveScalingProtocolTypeTcp = 0x00000010,

    NetAdapterReceiveScalingProtocolTypeUdp = 0x00000020,
} NET_ADAPTER_RECEIVE_SCALING_PROTOCOL_TYPE;

typedef enum _NET_ADAPTER_RECEIVE_SCALING_ENCAPSULATION_TYPE {
    NetAdapterReceiveScalingEncapsulationTypeNone = 0x00000000,

    NetAdapterReceiveScalingEncapsulationTypeNVGre = 0x00000001,

    NetAdapterReceiveScalingEncapsulationTypeVXLan = 0x00000002,

    NetAdapterReceiveScalingEncapsulationTypeVLan = 0x00000004,
} NET_ADAPTER_RECEIVE_SCALING_ENCAPSULATION_TYPE;

DEFINE_ENUM_FLAG_OPERATORS(NET_ADAPTER_RECEIVE_SCALING_HASH_TYPE);
DEFINE_ENUM_FLAG_OPERATORS(NET_ADAPTER_RECEIVE_SCALING_PROTOCOL_TYPE);
DEFINE_ENUM_FLAG_OPERATORS(NET_ADAPTER_RECEIVE_SCALING_ENCAPSULATION_TYPE);

typedef struct _NET_ADAPTER_RECEIVE_SCALING_HASH_SECRET_KEY
{
    _Field_size_full_(Length) UINT8 * Key;
    SIZE_T Length;
} NET_ADAPTER_RECEIVE_SCALING_HASH_SECRET_KEY;

typedef struct _NET_ADAPTER_RECEIVE_SCALING_INDIRECTION_ENTRY
{
    NETPACKETQUEUE PacketQueue;
    NTSTATUS Status;
    UINT32 Index;
} NET_ADAPTER_RECEIVE_SCALING_INDIRECTION_ENTRY;

typedef struct _NET_ADAPTER_RECEIVE_SCALING_INDIRECTION_ENTRIES
{
    _Field_size_full_(Length) NET_ADAPTER_RECEIVE_SCALING_INDIRECTION_ENTRY * Entries;
    SIZE_T Length;
} NET_ADAPTER_RECEIVE_SCALING_INDIRECTION_ENTRIES;

typedef
_Function_class_(EVT_NET_ADAPTER_RECEIVE_SCALING_ENABLE)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
EVT_NET_ADAPTER_RECEIVE_SCALING_ENABLE(
    _In_
    NETADAPTER Adapter,
    _In_
    NET_ADAPTER_RECEIVE_SCALING_HASH_TYPE HashType,
    _In_
    NET_ADAPTER_RECEIVE_SCALING_PROTOCOL_TYPE ProtocolType
);

typedef EVT_NET_ADAPTER_RECEIVE_SCALING_ENABLE *PFN_NET_ADAPTER_RECEIVE_SCALING_ENABLE;

typedef
_Function_class_(EVT_NET_ADAPTER_RECEIVE_SCALING_DISABLE)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
void
NTAPI
EVT_NET_ADAPTER_RECEIVE_SCALING_DISABLE(
    _In_
    NETADAPTER Adapter
);

typedef EVT_NET_ADAPTER_RECEIVE_SCALING_DISABLE *PFN_NET_ADAPTER_RECEIVE_SCALING_DISABLE;

typedef
_Function_class_(EVT_NET_ADAPTER_RECEIVE_SCALING_SET_HASH_SECRET_KEY)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
EVT_NET_ADAPTER_RECEIVE_SCALING_SET_HASH_SECRET_KEY(
    _In_
    NETADAPTER Adapter,
    _In_
    const NET_ADAPTER_RECEIVE_SCALING_HASH_SECRET_KEY * HashSecretKey
);

typedef EVT_NET_ADAPTER_RECEIVE_SCALING_SET_HASH_SECRET_KEY *PFN_NET_ADAPTER_RECEIVE_SCALING_SET_HASH_SECRET_KEY;

typedef
_Function_class_(EVT_NET_ADAPTER_RECEIVE_SCALING_SET_INDIRECTION_ENTRIES)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
EVT_NET_ADAPTER_RECEIVE_SCALING_SET_INDIRECTION_ENTRIES(
    _In_
    NETADAPTER Adapter,
    _Inout_
    NET_ADAPTER_RECEIVE_SCALING_INDIRECTION_ENTRIES * IndirectionEntries
);

typedef EVT_NET_ADAPTER_RECEIVE_SCALING_SET_INDIRECTION_ENTRIES *PFN_NET_ADAPTER_RECEIVE_SCALING_SET_INDIRECTION_ENTRIES;

typedef struct _NET_ADAPTER_RECEIVE_SCALING_CAPABILITIES {

    ULONG Size;

    SIZE_T NumberOfQueues;

    SIZE_T IndirectionTableSize;

    SIZE_T UnhashedTargetIndex;

    NET_ADAPTER_RECEIVE_SCALING_UNHASHED_TARGET_TYPE UnhashedTargetType;

    NET_ADAPTER_RECEIVE_SCALING_HASH_TYPE ReceiveScalingHashTypes;

    NET_ADAPTER_RECEIVE_SCALING_PROTOCOL_TYPE ReceiveScalingProtocolTypes;

    struct _RECEIEVE_SCALING_ENCAPSULATION {
        NET_ADAPTER_RECEIVE_SCALING_ENCAPSULATION_TYPE Outer;
        NET_ADAPTER_RECEIVE_SCALING_ENCAPSULATION_TYPE Inner;
    } ReceiveScalingEncapsulationTypes;

    BOOLEAN SynchronizeSetIndirectionEntries;

    PFN_NET_ADAPTER_RECEIVE_SCALING_ENABLE EvtAdapterReceiveScalingEnable;
    PFN_NET_ADAPTER_RECEIVE_SCALING_DISABLE EvtAdapterReceiveScalingDisable;
    PFN_NET_ADAPTER_RECEIVE_SCALING_SET_HASH_SECRET_KEY EvtAdapterReceiveScalingSetHashSecretKey;
    PFN_NET_ADAPTER_RECEIVE_SCALING_SET_INDIRECTION_ENTRIES EvtAdapterReceiveScalingSetIndirectionEntries;

} NET_ADAPTER_RECEIVE_SCALING_CAPABILITIES;

inline
void
NET_ADAPTER_RECEIVE_SCALING_CAPABILITIES_INIT(
    _Out_ NET_ADAPTER_RECEIVE_SCALING_CAPABILITIES * Capabilities,
    _In_ SIZE_T NumberOfQueues,
    _In_ NET_ADAPTER_RECEIVE_SCALING_UNHASHED_TARGET_TYPE UnhashedTargetType,
    _In_ NET_ADAPTER_RECEIVE_SCALING_HASH_TYPE HashTypes,
    _In_ NET_ADAPTER_RECEIVE_SCALING_PROTOCOL_TYPE ProtocolTypes,
    _In_ PFN_NET_ADAPTER_RECEIVE_SCALING_ENABLE Enable,
    _In_ PFN_NET_ADAPTER_RECEIVE_SCALING_DISABLE Disable,
    _In_ PFN_NET_ADAPTER_RECEIVE_SCALING_SET_HASH_SECRET_KEY SetHashSecretKey,
    _In_ PFN_NET_ADAPTER_RECEIVE_SCALING_SET_INDIRECTION_ENTRIES SetIndirectionEntries
)
{
    RtlZeroMemory(Capabilities, sizeof(NET_ADAPTER_RECEIVE_SCALING_CAPABILITIES));
    Capabilities->Size = sizeof(NET_ADAPTER_RECEIVE_SCALING_CAPABILITIES);
    Capabilities->NumberOfQueues = NumberOfQueues;
    Capabilities->UnhashedTargetType = UnhashedTargetType;
    Capabilities->IndirectionTableSize = NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_1;
    Capabilities->ReceiveScalingHashTypes = HashTypes;
    Capabilities->ReceiveScalingProtocolTypes = ProtocolTypes;
    Capabilities->ReceiveScalingEncapsulationTypes.Outer = NetAdapterReceiveScalingEncapsulationTypeNone;
    Capabilities->ReceiveScalingEncapsulationTypes.Inner = NetAdapterReceiveScalingEncapsulationTypeNone;
    Capabilities->SynchronizeSetIndirectionEntries = FALSE;
    Capabilities->EvtAdapterReceiveScalingEnable = Enable;
    Capabilities->EvtAdapterReceiveScalingDisable = Disable;
    Capabilities->EvtAdapterReceiveScalingSetHashSecretKey = SetHashSecretKey;
    Capabilities->EvtAdapterReceiveScalingSetIndirectionEntries = SetIndirectionEntries;
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETADAPTERSETRECEIVESCALINGCAPABILITIES)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_RECEIVE_SCALING_CAPABILITIES* Capabilities
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetAdapterSetReceiveScalingCapabilities(
    _In_
    NETADAPTER Adapter,
    _In_
    CONST NET_ADAPTER_RECEIVE_SCALING_CAPABILITIES* Capabilities
    )
{
    ((PFN_NETADAPTERSETRECEIVESCALINGCAPABILITIES) NetFunctions[NetAdapterSetReceiveScalingCapabilitiesTableIndex])(NetDriverGlobals, Adapter, Capabilities);
}

WDF_EXTERN_C_END

#endif
