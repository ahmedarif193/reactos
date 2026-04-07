/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x Compatibility Layer
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This header provides NDIS 6.x API declarations and compatibility
 * definitions that may be missing from ReactOS headers.
 *
 * When ReactOS fully supports NDIS 6.x, this file can be removed.
 */

#pragma once

#include <ntddk.h>
#include <ndis.h>

/* ============================================================================
 * NDIS 6.x Version Definitions
 * ============================================================================ */

#ifndef NDIS_MINIPORT_MAJOR_VERSION
#define NDIS_MINIPORT_MAJOR_VERSION 6
#endif

#ifndef NDIS_MINIPORT_MINOR_VERSION
#define NDIS_MINIPORT_MINOR_VERSION 30
#endif

/* NDIS runtime version */
#ifndef NDIS630
#define NDIS630 1
#endif

/* ============================================================================
 * NDIS Object Header Definitions
 * ============================================================================ */

#ifndef NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS
#define NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS    0x8A
#endif

#ifndef NDIS_OBJECT_TYPE_MINIPORT_INIT_PARAMETERS
#define NDIS_OBJECT_TYPE_MINIPORT_INIT_PARAMETERS           0x8B
#endif

#ifndef NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT
#define NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT                 0x8C
#endif

#ifndef NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES
#define NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES 0x8D
#endif

#ifndef NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES
#define NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES 0x8E
#endif

#ifndef NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES
#define NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES 0x8F
#endif

#ifndef NDIS_OBJECT_TYPE_DEFAULT
#define NDIS_OBJECT_TYPE_DEFAULT                            0x80
#endif

#ifndef NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION
#define NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION                 0x83
#endif

#ifndef NDIS_OBJECT_TYPE_RSS_CAPABILITIES
#define NDIS_OBJECT_TYPE_RSS_CAPABILITIES                   0xB8
#endif

/* ============================================================================
 * NDIS 6.x Revision Numbers
 * ============================================================================ */

#ifndef NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1
#define NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1     1
#endif

#ifndef NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2
#define NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2     2
#endif

#ifndef NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1
#define NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1 1
#endif

#ifndef NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1
#define NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1 1
#endif

#ifndef NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2
#define NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2 2
#endif

/* ============================================================================
 * NET_BUFFER_LIST Flags and Macros
 * ============================================================================ */

#ifndef NET_BUFFER_LIST_FIRST_NB
#define NET_BUFFER_LIST_FIRST_NB(_NBL) ((_NBL)->FirstNetBuffer)
#endif

#ifndef NET_BUFFER_FIRST_MDL
#define NET_BUFFER_FIRST_MDL(_NB) ((_NB)->CurrentMdl)
#endif

#ifndef NET_BUFFER_DATA_LENGTH
#define NET_BUFFER_DATA_LENGTH(_NB) ((_NB)->DataLength)
#endif

#ifndef NET_BUFFER_DATA_OFFSET
#define NET_BUFFER_DATA_OFFSET(_NB) ((_NB)->DataOffset)
#endif

#ifndef NET_BUFFER_CURRENT_MDL
#define NET_BUFFER_CURRENT_MDL(_NB) ((_NB)->CurrentMdl)
#endif

#ifndef NET_BUFFER_CURRENT_MDL_OFFSET
#define NET_BUFFER_CURRENT_MDL_OFFSET(_NB) ((_NB)->CurrentMdlOffset)
#endif

#ifndef NET_BUFFER_LIST_NEXT_NBL
#define NET_BUFFER_LIST_NEXT_NBL(_NBL) ((_NBL)->Next)
#endif

#ifndef NET_BUFFER_NEXT_NB
#define NET_BUFFER_NEXT_NB(_NB) ((_NB)->Next)
#endif

#ifndef NET_BUFFER_LIST_STATUS
#define NET_BUFFER_LIST_STATUS(_NBL) ((_NBL)->Status)
#endif

#ifndef NET_BUFFER_LIST_INFO
#define NET_BUFFER_LIST_INFO(_NBL, _Id) ((_NBL)->NetBufferListInfo[(_Id)])
#endif

/* ============================================================================
 * NBL Context Manipulation
 * ============================================================================ */

#ifndef NET_BUFFER_LIST_CONTEXT_DATA_START
#define NET_BUFFER_LIST_CONTEXT_DATA_START(_NBL) \
    ((PUCHAR)(_NBL)->Context + ALIGN_UP(sizeof(NET_BUFFER_LIST_CONTEXT), PVOID))
#endif

#ifndef NET_BUFFER_LIST_CONTEXT_DATA_SIZE
#define NET_BUFFER_LIST_CONTEXT_DATA_SIZE(_NBL) \
    (((_NBL)->Context) ? ((_NBL)->Context->Size - sizeof(NET_BUFFER_LIST_CONTEXT)) : 0)
#endif

/* ============================================================================
 * Checksum Information Macros
 * ============================================================================ */

#ifndef NDIS_TCP_LARGE_SEND_OFFLOAD_V1_TYPE
#define NDIS_TCP_LARGE_SEND_OFFLOAD_V1_TYPE 0
#endif

#ifndef NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE
#define NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE 1
#endif

/* TX checksum info structure */
#ifndef _NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO
typedef union _NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO
{
    struct
    {
        ULONG IsIPv4:1;
        ULONG IsIPv6:1;
        ULONG TcpChecksum:1;
        ULONG UdpChecksum:1;
        ULONG IpHeaderChecksum:1;
        ULONG Reserved:11;
        ULONG TcpHeaderOffset:10;
    } Transmit;

    struct
    {
        ULONG TcpChecksumFailed:1;
        ULONG UdpChecksumFailed:1;
        ULONG IpChecksumFailed:1;
        ULONG TcpChecksumSucceeded:1;
        ULONG UdpChecksumSucceeded:1;
        ULONG IpChecksumSucceeded:1;
        ULONG Loopback:1;
        ULONG TcpChecksumValueInvalid:1;
        ULONG IpChecksumValueInvalid:1;
    } Receive;

    PVOID Value;

} NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO, *PNDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO;
#endif

/* ============================================================================
 * RSS Types
 * ============================================================================ */

#ifndef NDIS_HASH_TYPE_TCP_IPV4
#define NDIS_HASH_TYPE_TCP_IPV4     0x00000100
#endif

#ifndef NDIS_HASH_TYPE_TCP_IPV6
#define NDIS_HASH_TYPE_TCP_IPV6     0x00000200
#endif

#ifndef NDIS_HASH_TYPE_TCP_IPV6_EX
#define NDIS_HASH_TYPE_TCP_IPV6_EX  0x00000400
#endif

#ifndef NDIS_HASH_FUNCTION_TOEPLITZ
#define NDIS_HASH_FUNCTION_TOEPLITZ 0x00000001
#endif

/* ============================================================================
 * NDIS 6.x Status Codes
 * ============================================================================ */

#ifndef NDIS_STATUS_PAUSED
#define NDIS_STATUS_PAUSED                      ((NDIS_STATUS)0x40010002L)
#endif

#ifndef NDIS_STATUS_BUSY
#define NDIS_STATUS_BUSY                        ((NDIS_STATUS)0xC0010010L)
#endif

#ifndef NDIS_STATUS_LOW_POWER_STATE
#define NDIS_STATUS_LOW_POWER_STATE             ((NDIS_STATUS)0xC0010016L)
#endif

#ifndef NDIS_STATUS_INDICATION_REQUIRED
#define NDIS_STATUS_INDICATION_REQUIRED         ((NDIS_STATUS)0x40010011L)
#endif

/* ============================================================================
 * NDIS 6.x Attribute Flags
 * ============================================================================ */

#ifndef NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE
#define NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE            0x00000001
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM
#define NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM                   0x00000002
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK
#define NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK         0x00000004
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_NOT_CO_NDIS
#define NDIS_MINIPORT_ATTRIBUTES_NOT_CO_NDIS                0x00000008
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_DO_NOT_BIND_TO_ALL_CO
#define NDIS_MINIPORT_ATTRIBUTES_DO_NOT_BIND_TO_ALL_CO      0x00000010
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND
#define NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND         0x00000020
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER
#define NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER                 0x00000040
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_CONTROLS_DEFAULT_PORT
#define NDIS_MINIPORT_ATTRIBUTES_CONTROLS_DEFAULT_PORT      0x00000080
#endif

/* ============================================================================
 * NDIS 6.x Structures (if not defined)
 * ============================================================================ */

/* These structures should be defined in ndis.h, but we provide
 * compatibility definitions just in case */

#ifndef NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_DEFINED
typedef struct _NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES
{
    NDIS_OBJECT_HEADER                  Header;
    NDIS_HANDLE                         MiniportAdapterContext;
    ULONG                               AttributeFlags;
    UINT                                CheckForHangTimeInSeconds;
    NDIS_INTERFACE_TYPE                 InterfaceType;
} NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES, *PNDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
#define NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_DEFINED
#endif

/* ============================================================================
 * Scatter-Gather DMA Structures
 * ============================================================================ */

#ifndef NDIS_SG_DMA_DESCRIPTION_DEFINED
typedef struct _NDIS_SG_DMA_DESCRIPTION
{
    NDIS_OBJECT_HEADER  Header;
    ULONG               Flags;
    ULONG               MaximumPhysicalMapping;
    MINIPORT_PROCESS_SG_LIST_HANDLER ProcessSGListHandler;
    MINIPORT_ALLOCATE_SHARED_MEM_COMPLETE_HANDLER SharedMemAllocateCompleteHandler;
    ULONG               ScatterGatherListSize;
} NDIS_SG_DMA_DESCRIPTION, *PNDIS_SG_DMA_DESCRIPTION;
#define NDIS_SG_DMA_DESCRIPTION_DEFINED
#endif

/* ============================================================================
 * Interrupt Configuration Structures
 * ============================================================================ */

#ifndef NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS_DEFINED
typedef struct _NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS
{
    NDIS_OBJECT_HEADER                  Header;
    MINIPORT_ISR                        InterruptHandler;
    MINIPORT_INTERRUPT_DPC              InterruptDpcHandler;
    MINIPORT_DISABLE_INTERRUPT          DisableInterruptHandler;
    MINIPORT_ENABLE_INTERRUPT           EnableInterruptHandler;
    BOOLEAN                             MsiSupported;
    BOOLEAN                             MsiSyncWithAllMessages;
    PIO_INTERRUPT_MESSAGE_INFO          MessageInfoTable;
} NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS, *PNDIS_MINIPORT_INTERRUPT_CHARACTERISTICS;
#define NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS_DEFINED
#endif

/* ============================================================================
 * Status Indication
 * ============================================================================ */

#ifndef NDIS_OBJECT_TYPE_STATUS_INDICATION
#define NDIS_OBJECT_TYPE_STATUS_INDICATION      0x80
#endif

#ifndef NDIS_STATUS_INDICATION_REVISION_1
#define NDIS_STATUS_INDICATION_REVISION_1       1
#endif

#ifndef NDIS_STATUS_INDICATION_DEFINED
typedef struct _NDIS_STATUS_INDICATION
{
    NDIS_OBJECT_HEADER      Header;
    NDIS_HANDLE             SourceHandle;
    NDIS_PORT_NUMBER        PortNumber;
    NDIS_STATUS             StatusCode;
    ULONG                   Flags;
    NDIS_HANDLE             DestinationHandle;
    PVOID                   RequestId;
    PVOID                   StatusBuffer;
    ULONG                   StatusBufferSize;
    GUID                    Guid;
    PVOID                   NdisReserved[4];
} NDIS_STATUS_INDICATION, *PNDIS_STATUS_INDICATION;
#define NDIS_STATUS_INDICATION_DEFINED
#endif

/* ============================================================================
 * Link State Indication
 * ============================================================================ */

#ifndef NDIS_LINK_STATE_REVISION_1
#define NDIS_LINK_STATE_REVISION_1              1
#endif

#ifndef NDIS_LINK_STATE_DEFINED
typedef struct _NDIS_LINK_STATE
{
    NDIS_OBJECT_HEADER          Header;
    NDIS_MEDIA_CONNECT_STATE    MediaConnectState;
    NDIS_MEDIA_DUPLEX_STATE     MediaDuplexState;
    ULONG64                     XmitLinkSpeed;
    ULONG64                     RcvLinkSpeed;
    NDIS_SUPPORTED_PAUSE_FUNCTIONS PauseFunctions;
    ULONG                       AutoNegotiationFlags;
} NDIS_LINK_STATE, *PNDIS_LINK_STATE;
#define NDIS_LINK_STATE_DEFINED
#endif

/* ============================================================================
 * OID Request Structure
 * ============================================================================ */

#ifndef NDIS_OID_REQUEST_DEFINED
/* The structure should be in ndis.h; this is for reference */
#endif

/* ============================================================================
 * NBL Pool Configuration
 * ============================================================================ */

#ifndef NET_BUFFER_LIST_POOL_PARAMETERS_DEFINED
typedef struct _NET_BUFFER_LIST_POOL_PARAMETERS
{
    NDIS_OBJECT_HEADER      Header;
    UCHAR                   ProtocolId;
    BOOLEAN                 fAllocateNetBuffer;
    USHORT                  ContextSize;
    ULONG                   PoolTag;
    ULONG                   DataSize;
} NET_BUFFER_LIST_POOL_PARAMETERS, *PNET_BUFFER_LIST_POOL_PARAMETERS;
#define NET_BUFFER_LIST_POOL_PARAMETERS_DEFINED
#endif

/* ============================================================================
 * Miniport PnP Event
 * ============================================================================ */

#ifndef NET_DEVICE_PNP_EVENT_DEFINED
typedef struct _NET_DEVICE_PNP_EVENT
{
    NDIS_OBJECT_HEADER                  Header;
    NDIS_PORT_NUMBER                    PortNumber;
    NDIS_DEVICE_PNP_EVENT               DevicePnPEvent;
    PVOID                               InformationBuffer;
    ULONG                               InformationBufferLength;
    UCHAR                               NdisReserved[2 * sizeof(PVOID)];
} NET_DEVICE_PNP_EVENT, *PNET_DEVICE_PNP_EVENT;
#define NET_DEVICE_PNP_EVENT_DEFINED
#endif

/* ============================================================================
 * Offload Capabilities
 * ============================================================================ */

#ifndef NDIS_OFFLOAD_DEFINED
/* NDIS_OFFLOAD is complex; rely on system headers */
#endif

/* ============================================================================
 * Function Pointer Types for NDIS 6.x Miniport
 *
 * These are now defined in ndis.h when NDIS_SUPPORT_NDIS6 is set.
 * We only define them here as a fallback for older headers.
 * ============================================================================ */

#ifndef NDIS_MINIPORT_HANDLERS_DEFINED

typedef NDIS_STATUS
(MINIPORT_INITIALIZE)(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters
    );
typedef MINIPORT_INITIALIZE *MINIPORT_INITIALIZE_HANDLER;

typedef VOID
(MINIPORT_HALT)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction
    );
typedef MINIPORT_HALT *MINIPORT_HALT_HANDLER;

typedef NDIS_STATUS
(MINIPORT_PAUSE)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters
    );
typedef MINIPORT_PAUSE *MINIPORT_PAUSE_HANDLER;

typedef NDIS_STATUS
(MINIPORT_RESTART)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters
    );
typedef MINIPORT_RESTART *MINIPORT_RESTART_HANDLER;

typedef VOID
(MINIPORT_SHUTDOWN)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction
    );
typedef MINIPORT_SHUTDOWN *MINIPORT_SHUTDOWN_HANDLER;

typedef VOID
(MINIPORT_DEVICE_PNP_EVENT_NOTIFY)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent
    );
typedef MINIPORT_DEVICE_PNP_EVENT_NOTIFY *MINIPORT_DEVICE_PNP_EVENT_NOTIFY_HANDLER;

typedef VOID
(MINIPORT_SEND_NET_BUFFER_LISTS)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags
    );
typedef MINIPORT_SEND_NET_BUFFER_LISTS *MINIPORT_SEND_NET_BUFFER_LISTS_HANDLER;

typedef VOID
(MINIPORT_RETURN_NET_BUFFER_LISTS)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags
    );
typedef MINIPORT_RETURN_NET_BUFFER_LISTS *MINIPORT_RETURN_NET_BUFFER_LISTS_HANDLER;

typedef VOID
(MINIPORT_CANCEL_SEND)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId
    );
typedef MINIPORT_CANCEL_SEND *MINIPORT_CANCEL_SEND_HANDLER;

typedef NDIS_STATUS
(MINIPORT_OID_REQUEST)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest
    );
typedef MINIPORT_OID_REQUEST *MINIPORT_OID_REQUEST_HANDLER;

typedef VOID
(MINIPORT_CANCEL_OID_REQUEST)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId
    );
typedef MINIPORT_CANCEL_OID_REQUEST *MINIPORT_CANCEL_OID_REQUEST_HANDLER;

typedef BOOLEAN
(MINIPORT_CHECK_FOR_HANG)(
    _In_ NDIS_HANDLE MiniportAdapterContext
    );
typedef MINIPORT_CHECK_FOR_HANG *MINIPORT_CHECK_FOR_HANG_HANDLER;

typedef NDIS_STATUS
(MINIPORT_RESET)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset
    );
typedef MINIPORT_RESET *MINIPORT_RESET_HANDLER;

typedef VOID
(MINIPORT_UNLOAD)(
    _In_ PDRIVER_OBJECT DriverObject
    );
typedef MINIPORT_UNLOAD *MINIPORT_UNLOAD_HANDLER;

/* ============================================================================
 * Interrupt Handler Types
 * ============================================================================ */

typedef BOOLEAN
(MINIPORT_ISR)(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors
    );
typedef MINIPORT_ISR *MINIPORT_ISR_HANDLER;

typedef VOID
(MINIPORT_INTERRUPT_DPC)(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2
    );
typedef MINIPORT_INTERRUPT_DPC *MINIPORT_INTERRUPT_DPC_HANDLER;

typedef VOID
(MINIPORT_DISABLE_INTERRUPT)(
    _In_ NDIS_HANDLE MiniportInterruptContext
    );
typedef MINIPORT_DISABLE_INTERRUPT *MINIPORT_DISABLE_INTERRUPT_HANDLER;

typedef VOID
(MINIPORT_ENABLE_INTERRUPT)(
    _In_ NDIS_HANDLE MiniportInterruptContext
    );
typedef MINIPORT_ENABLE_INTERRUPT *MINIPORT_ENABLE_INTERRUPT_HANDLER;

/* ============================================================================
 * Scatter-Gather Handler Types
 * ============================================================================ */

typedef VOID
(MINIPORT_PROCESS_SG_LIST)(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PVOID Reserved,
    _In_ PSCATTER_GATHER_LIST ScatterGatherListBuffer,
    _In_ PVOID Context
    );
typedef MINIPORT_PROCESS_SG_LIST *MINIPORT_PROCESS_SG_LIST_HANDLER;

typedef VOID
(MINIPORT_ALLOCATE_SHARED_MEM_COMPLETE)(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID VirtualAddress,
    _In_ PNDIS_PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Length,
    _In_ PVOID Context
    );
typedef MINIPORT_ALLOCATE_SHARED_MEM_COMPLETE *MINIPORT_ALLOCATE_SHARED_MEM_COMPLETE_HANDLER;

#endif /* NDIS_MINIPORT_HANDLERS_DEFINED */

/* ============================================================================
 * Helper Macros
 * ============================================================================ */

/* Initialize NDIS_OBJECT_HEADER */
#define NDIS_INIT_OBJECT_HEADER(_Header, _Type, _Revision, _Size) \
    do { \
        (_Header)->Type = (_Type); \
        (_Header)->Revision = (_Revision); \
        (_Header)->Size = (_Size); \
    } while (0)

/* ETH_LENGTH_OF_ADDRESS if not defined */
#ifndef ETH_LENGTH_OF_ADDRESS
#define ETH_LENGTH_OF_ADDRESS 6
#endif

/* ============================================================================
 * Additional Status Codes
 * ============================================================================ */

#ifndef STATUS_NDIS_ADAPTER_NOT_FOUND
#define STATUS_NDIS_ADAPTER_NOT_FOUND ((NDIS_STATUS)0xC0230006L)
#endif

/* ============================================================================
 * NDIS 6.x Memory Allocation APIs
 * ============================================================================ */

#ifndef NdisAllocateMemoryWithTagPriority
#define NdisAllocateMemoryWithTagPriority(Handle, Size, Tag, Priority) \
    ExAllocatePoolWithTag(NonPagedPool, (Size), (Tag))
#endif

/* ============================================================================
 * NBL Cancel ID Macros
 * ============================================================================ */

#ifndef NDIS_GET_NET_BUFFER_LIST_CANCEL_ID
#define NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(_NBL) \
    ((_NBL)->NetBufferListInfo[NetBufferListCancelId])
#endif

#ifndef NDIS_SET_NET_BUFFER_LIST_CANCEL_ID
#define NDIS_SET_NET_BUFFER_LIST_CANCEL_ID(_NBL, _CancelId) \
    ((_NBL)->NetBufferListInfo[NetBufferListCancelId] = (_CancelId))
#endif

/* ============================================================================
 * NetBufferListInfo IDs
 * ============================================================================ */

#ifndef TcpIpChecksumNetBufferListInfo
#define TcpIpChecksumNetBufferListInfo      0
#endif

#ifndef TcpLargeSendNetBufferListInfo
#define TcpLargeSendNetBufferListInfo       1
#endif

#ifndef Ieee8021QNetBufferListInfo
#define Ieee8021QNetBufferListInfo          2
#endif

#ifndef NetBufferListCancelId
#define NetBufferListCancelId               3
#endif

#ifndef MediaSpecificInformation
#define MediaSpecificInformation            4
#endif

/* ============================================================================
 * RSS Structure Definitions
 * ============================================================================ */

#ifndef NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_1
#define NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_1 1
#endif

#ifndef NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED
#define NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED      0x0001
#endif

#ifndef NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED
#define NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED     0x0002
#endif

#ifndef NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED
#define NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED        0x0004
#endif

#ifndef NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED
#define NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED      0x0008
#endif

#ifndef NDIS_RSS_PARAM_FLAG_DISABLE_RSS
#define NDIS_RSS_PARAM_FLAG_DISABLE_RSS             0x0010
#endif

#ifndef NDIS_HASH_TYPE_MASK
#define NDIS_HASH_TYPE_MASK                         0x00FFFF00
#endif

#ifndef NDIS_HASH_FUNCTION_MASK
#define NDIS_HASH_FUNCTION_MASK                     0x000000FF
#endif

#ifndef NDIS_RECEIVE_SCALE_PARAMETERS_DEFINED
typedef struct _NDIS_RECEIVE_SCALE_PARAMETERS {
    NDIS_OBJECT_HEADER      Header;
    USHORT                  Flags;
    USHORT                  BaseCpuNumber;
    ULONG                   HashInformation;
    USHORT                  IndirectionTableSize;
    ULONG                   IndirectionTableOffset;
    USHORT                  HashSecretKeySize;
    ULONG                   HashSecretKeyOffset;
    ULONG                   ProcessorMasksOffset;
    ULONG                   NumberOfProcessorMasks;
    ULONG                   ProcessorMasksEntrySize;
} NDIS_RECEIVE_SCALE_PARAMETERS, *PNDIS_RECEIVE_SCALE_PARAMETERS;
#define NDIS_RECEIVE_SCALE_PARAMETERS_DEFINED 1
#endif

/* ============================================================================
 * Receive Packet Steering Descriptor Type
 * ============================================================================ */

#ifndef E1000_RCTL_DTYP_PS
#define E1000_RCTL_DTYP_PS  0      /* Not supported, placeholder */
#endif

/* ============================================================================
 * NDIS 6.x MDL Functions Compatibility
 * ============================================================================ */

/*
 * NdisAllocateMdl - Allocate an MDL for NDIS 6.x
 *
 * For shared memory (DMA buffers allocated via NdisMAllocateSharedMemory),
 * we need to call MmBuildMdlForNonPagedPool to properly set up the MDL.
 * This ensures MmGetSystemAddressForMdlSafe will work correctly.
 */
#ifndef NdisAllocateMdl
static __inline PMDL
E1000_NdisAllocateMdl(
    _In_ NDIS_HANDLE Handle,
    _In_ PVOID VirtualAddress,
    _In_ ULONG Length)
{
    PMDL Mdl;
    UNREFERENCED_PARAMETER(Handle);

    Mdl = IoAllocateMdl(VirtualAddress, Length, FALSE, FALSE, NULL);
    if (Mdl != NULL)
    {
        /* Build the MDL for non-paged pool memory (DMA buffers) */
        MmBuildMdlForNonPagedPool(Mdl);
    }
    return Mdl;
}
#define NdisAllocateMdl(Handle, VirtualAddress, Length) \
    E1000_NdisAllocateMdl((Handle), (VirtualAddress), (Length))
#endif

#ifndef NdisFreeMdl
#define NdisFreeMdl(Mdl) IoFreeMdl(Mdl)
#endif

#ifndef NdisMFreeNetBufferSGList
/* Stub for scatter-gather list freeing - will need proper implementation */
#define NdisMFreeNetBufferSGList(MiniportDmaHandle, ScatterGatherListBuffer, NetBuffer) \
    do { UNREFERENCED_PARAMETER(MiniportDmaHandle); UNREFERENCED_PARAMETER(ScatterGatherListBuffer); UNREFERENCED_PARAMETER(NetBuffer); } while(0)
#endif

/* ============================================================================
 * NDIS_OFFLOAD Structure Definition
 * ============================================================================ */

#ifndef NDIS_OBJECT_TYPE_OFFLOAD
#define NDIS_OBJECT_TYPE_OFFLOAD    0x90
#endif

#ifndef NDIS_OFFLOAD_REVISION_1
#define NDIS_OFFLOAD_REVISION_1     1
#endif

#ifndef NDIS_OFFLOAD_REVISION_2
#define NDIS_OFFLOAD_REVISION_2     2
#endif

#ifndef NDIS_OFFLOAD_REVISION_3
#define NDIS_OFFLOAD_REVISION_3     3
#endif

#ifndef NDIS_ENCAPSULATION_IEEE_802_3
#define NDIS_ENCAPSULATION_IEEE_802_3       0x00000002
#endif

#ifndef NDIS_ENCAPSULATION_NOT_SUPPORTED
#define NDIS_ENCAPSULATION_NOT_SUPPORTED    0x00000000
#endif

#ifndef NDIS_OFFLOAD_DEFINED
/* TCP/IP Checksum Offload structure */
typedef struct _NDIS_TCP_IP_CHECKSUM_OFFLOAD {
    struct {
        ULONG Encapsulation;
        ULONG IpOptionsSupported:2;
        ULONG TcpOptionsSupported:2;
        ULONG TcpChecksum:2;
        ULONG UdpChecksum:2;
        ULONG IpChecksum:2;
    } IPv4Transmit;

    struct {
        ULONG Encapsulation;
        ULONG IpOptionsSupported:2;
        ULONG TcpOptionsSupported:2;
        ULONG TcpChecksum:2;
        ULONG UdpChecksum:2;
        ULONG IpChecksum:2;
    } IPv4Receive;

    struct {
        ULONG Encapsulation;
        ULONG IpExtensionHeadersSupported:2;
        ULONG TcpOptionsSupported:2;
        ULONG TcpChecksum:2;
        ULONG UdpChecksum:2;
    } IPv6Transmit;

    struct {
        ULONG Encapsulation;
        ULONG IpExtensionHeadersSupported:2;
        ULONG TcpOptionsSupported:2;
        ULONG TcpChecksum:2;
        ULONG UdpChecksum:2;
    } IPv6Receive;
} NDIS_TCP_IP_CHECKSUM_OFFLOAD, *PNDIS_TCP_IP_CHECKSUM_OFFLOAD;

/* Large Send Offload version 1 structure */
typedef struct _NDIS_TCP_LARGE_SEND_OFFLOAD_V1 {
    struct {
        ULONG Encapsulation;
        ULONG MaxOffLoadSize;
        ULONG MinSegmentCount;
        ULONG TcpOptions:2;
        ULONG IpOptions:2;
    } IPv4;
} NDIS_TCP_LARGE_SEND_OFFLOAD_V1, *PNDIS_TCP_LARGE_SEND_OFFLOAD_V1;

/* Large Send Offload version 2 structure */
typedef struct _NDIS_TCP_LARGE_SEND_OFFLOAD_V2 {
    struct {
        ULONG Encapsulation;
        ULONG MaxOffLoadSize;
        ULONG MinSegmentCount;
    } IPv4;
    struct {
        ULONG Encapsulation;
        ULONG MaxOffLoadSize;
        ULONG MinSegmentCount;
        ULONG IpExtensionHeadersSupported:2;
        ULONG TcpOptionsSupported:2;
    } IPv6;
} NDIS_TCP_LARGE_SEND_OFFLOAD_V2, *PNDIS_TCP_LARGE_SEND_OFFLOAD_V2;

/* IPSEC Offload v1 - minimal placeholder */
typedef struct _NDIS_IPSEC_OFFLOAD_V1 {
    struct {
        ULONG Encapsulation;
    } Supported;
    struct {
        ULONG Encapsulation;
    } IPv4AH;
    struct {
        ULONG Encapsulation;
    } IPv4ESP;
} NDIS_IPSEC_OFFLOAD_V1, *PNDIS_IPSEC_OFFLOAD_V1;

/* Main NDIS_OFFLOAD structure */
typedef struct _NDIS_OFFLOAD {
    NDIS_OBJECT_HEADER              Header;
    NDIS_TCP_IP_CHECKSUM_OFFLOAD    Checksum;
    NDIS_TCP_LARGE_SEND_OFFLOAD_V1  LsoV1;
    NDIS_IPSEC_OFFLOAD_V1           IPsecV1;
    NDIS_TCP_LARGE_SEND_OFFLOAD_V2  LsoV2;
    ULONG                           Flags;
} NDIS_OFFLOAD, *PNDIS_OFFLOAD;
#define NDIS_OFFLOAD_DEFINED 1
#endif

/* ============================================================================
 * NDIS_OFFLOAD_PARAMETERS Structure Definition
 * ============================================================================ */

#ifndef NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED
#define NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED          0
#endif

#ifndef NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED
#define NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED  1
#endif

#ifndef NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED
#define NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED  2
#endif

#ifndef NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED
#define NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED           3
#endif

#ifndef NDIS_OFFLOAD_PARAMETERS_NO_CHANGE
#define NDIS_OFFLOAD_PARAMETERS_NO_CHANGE               0xFF
#endif

#ifndef NDIS_OFFLOAD_PARAMETERS_REVISION_1
#define NDIS_OFFLOAD_PARAMETERS_REVISION_1              1
#endif

#ifndef NDIS_OBJECT_TYPE_DEFAULT_OFFLOAD_PARAMETERS
#define NDIS_OBJECT_TYPE_DEFAULT_OFFLOAD_PARAMETERS     0x91
#endif

#ifndef NDIS_OFFLOAD_PARAMETERS_DEFINED
typedef struct _NDIS_OFFLOAD_PARAMETERS {
    NDIS_OBJECT_HEADER      Header;
    UCHAR                   IPv4Checksum;
    UCHAR                   TCPIPv4Checksum;
    UCHAR                   UDPIPv4Checksum;
    UCHAR                   TCPIPv6Checksum;
    UCHAR                   UDPIPv6Checksum;
    UCHAR                   LsoV1;
    UCHAR                   IPsecV1;
    UCHAR                   LsoV2IPv4;
    UCHAR                   LsoV2IPv6;
    UCHAR                   TcpConnectionIPv4;
    UCHAR                   TcpConnectionIPv6;
    ULONG                   Flags;
} NDIS_OFFLOAD_PARAMETERS, *PNDIS_OFFLOAD_PARAMETERS;
#define NDIS_OFFLOAD_PARAMETERS_DEFINED 1
#endif

/* ============================================================================
 * NDIS 6.x OID Values
 * ============================================================================ */

/* General OIDs for NDIS 6.x */
#ifndef OID_GEN_LINK_SPEED_EX
#define OID_GEN_LINK_SPEED_EX               0x00010206
#endif

#ifndef OID_GEN_MAX_LINK_SPEED
#define OID_GEN_MAX_LINK_SPEED              0x00010207
#endif

#ifndef OID_GEN_MEDIA_CONNECT_STATUS_EX
#define OID_GEN_MEDIA_CONNECT_STATUS_EX     0x00010208
#endif

#ifndef OID_GEN_MEDIA_DUPLEX_STATE
#define OID_GEN_MEDIA_DUPLEX_STATE          0x00010209
#endif

#ifndef OID_GEN_LINK_STATE
#define OID_GEN_LINK_STATE                  0x0001020A
#endif

#ifndef OID_GEN_INTERRUPT_MODERATION
#define OID_GEN_INTERRUPT_MODERATION        0x0001020C
#endif

#ifndef OID_GEN_STATISTICS
#define OID_GEN_STATISTICS                  0x00020106
#endif

#ifndef OID_GEN_BYTES_XMIT
#define OID_GEN_BYTES_XMIT                  0x00020201
#endif

#ifndef OID_GEN_BYTES_RCV
#define OID_GEN_BYTES_RCV                   0x00020202
#endif

#ifndef OID_GEN_RECEIVE_SCALE_CAPABILITIES
#define OID_GEN_RECEIVE_SCALE_CAPABILITIES  0x00010203
#endif

#ifndef OID_GEN_RECEIVE_SCALE_PARAMETERS
#define OID_GEN_RECEIVE_SCALE_PARAMETERS    0x00010204
#endif

/* ============================================================================
 * NDIS 6.x Statistics Structures
 * ============================================================================ */

#ifndef NDIS_LINK_SPEED_DEFINED
typedef struct _NDIS_LINK_SPEED {
    ULONG64 XmitLinkSpeed;
    ULONG64 RcvLinkSpeed;
} NDIS_LINK_SPEED, *PNDIS_LINK_SPEED;
#define NDIS_LINK_SPEED_DEFINED 1
#endif

#ifndef NDIS_STATISTICS_INFO_REVISION_1
#define NDIS_STATISTICS_INFO_REVISION_1     1
#endif

#ifndef NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV
#define NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV           0x00000001
#endif

#ifndef NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT
#define NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT          0x00000002
#endif

#ifndef NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS
#define NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS        0x00000004
#endif

#ifndef NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR
#define NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR           0x00000008
#endif

#ifndef NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR
#define NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR          0x00000010
#endif

#ifndef NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS
#define NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS       0x00000020
#endif

#ifndef NDIS_STATISTICS_INFO_DEFINED
typedef struct _NDIS_STATISTICS_INFO {
    NDIS_OBJECT_HEADER  Header;
    ULONG               SupportedStatistics;
    ULONG64             ifInDiscards;
    ULONG64             ifInErrors;
    ULONG64             ifHCInOctets;
    ULONG64             ifHCInUcastPkts;
    ULONG64             ifHCInMulticastPkts;
    ULONG64             ifHCInBroadcastPkts;
    ULONG64             ifHCOutOctets;
    ULONG64             ifHCOutUcastPkts;
    ULONG64             ifHCOutMulticastPkts;
    ULONG64             ifHCOutBroadcastPkts;
    ULONG64             ifOutErrors;
    ULONG64             ifOutDiscards;
    ULONG64             ifHCInUcastOctets;
    ULONG64             ifHCInMulticastOctets;
    ULONG64             ifHCInBroadcastOctets;
    ULONG64             ifHCOutUcastOctets;
    ULONG64             ifHCOutMulticastOctets;
    ULONG64             ifHCOutBroadcastOctets;
} NDIS_STATISTICS_INFO, *PNDIS_STATISTICS_INFO;
#define NDIS_STATISTICS_INFO_DEFINED 1
#endif

/* ============================================================================
 * NDIS 6.x Interrupt Moderation Structures
 * ============================================================================ */

#ifndef NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1
#define NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1 1
#endif

#ifndef NdisInterruptModerationNotSupported
#define NdisInterruptModerationNotSupported 0
#endif

#ifndef NdisInterruptModerationEnabled
#define NdisInterruptModerationEnabled      1
#endif

#ifndef NdisInterruptModerationDisabled
#define NdisInterruptModerationDisabled     2
#endif

#ifndef NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_RESET
#define NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_RESET    0x00000001
#endif

#ifndef NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_REINITIALIZE
#define NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_REINITIALIZE 0x00000002
#endif

#ifndef NDIS_INTERRUPT_MODERATION_PARAMETERS_DEFINED
typedef struct _NDIS_INTERRUPT_MODERATION_PARAMETERS {
    NDIS_OBJECT_HEADER  Header;
    ULONG               Flags;
    ULONG               InterruptModeration;
} NDIS_INTERRUPT_MODERATION_PARAMETERS, *PNDIS_INTERRUPT_MODERATION_PARAMETERS;
#define NDIS_INTERRUPT_MODERATION_PARAMETERS_DEFINED 1
#endif

/* ============================================================================
 * NDIS Link Speed Constants
 * ============================================================================ */

#ifndef NDIS_LINK_SPEED_UNKNOWN
#define NDIS_LINK_SPEED_UNKNOWN             0
#endif

/* ============================================================================
 * NDIS Statistics Flags
 * ============================================================================ */

#ifndef NDIS_STATISTICS_XMIT_OK_SUPPORTED
#define NDIS_STATISTICS_XMIT_OK_SUPPORTED           0x00000040
#endif

#ifndef NDIS_STATISTICS_RCV_OK_SUPPORTED
#define NDIS_STATISTICS_RCV_OK_SUPPORTED            0x00000080
#endif

#ifndef NDIS_STATISTICS_XMIT_ERROR_SUPPORTED
#define NDIS_STATISTICS_XMIT_ERROR_SUPPORTED        0x00000100
#endif

#ifndef NDIS_STATISTICS_RCV_ERROR_SUPPORTED
#define NDIS_STATISTICS_RCV_ERROR_SUPPORTED         0x00000200
#endif

#ifndef NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED
#define NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED     0x00000400
#endif

#ifndef NDIS_STATISTICS_DIRECTED_BYTES_XMIT_SUPPORTED
#define NDIS_STATISTICS_DIRECTED_BYTES_XMIT_SUPPORTED       0x00000800
#endif

#ifndef NDIS_STATISTICS_DIRECTED_FRAMES_XMIT_SUPPORTED
#define NDIS_STATISTICS_DIRECTED_FRAMES_XMIT_SUPPORTED      0x00001000
#endif

#ifndef NDIS_STATISTICS_MULTICAST_BYTES_XMIT_SUPPORTED
#define NDIS_STATISTICS_MULTICAST_BYTES_XMIT_SUPPORTED      0x00002000
#endif

#ifndef NDIS_STATISTICS_MULTICAST_FRAMES_XMIT_SUPPORTED
#define NDIS_STATISTICS_MULTICAST_FRAMES_XMIT_SUPPORTED     0x00004000
#endif

#ifndef NDIS_STATISTICS_BROADCAST_BYTES_XMIT_SUPPORTED
#define NDIS_STATISTICS_BROADCAST_BYTES_XMIT_SUPPORTED      0x00008000
#endif

#ifndef NDIS_STATISTICS_BROADCAST_FRAMES_XMIT_SUPPORTED
#define NDIS_STATISTICS_BROADCAST_FRAMES_XMIT_SUPPORTED     0x00010000
#endif

#ifndef NDIS_STATISTICS_DIRECTED_BYTES_RCV_SUPPORTED
#define NDIS_STATISTICS_DIRECTED_BYTES_RCV_SUPPORTED        0x00020000
#endif

#ifndef NDIS_STATISTICS_DIRECTED_FRAMES_RCV_SUPPORTED
#define NDIS_STATISTICS_DIRECTED_FRAMES_RCV_SUPPORTED       0x00040000
#endif

#ifndef NDIS_STATISTICS_MULTICAST_BYTES_RCV_SUPPORTED
#define NDIS_STATISTICS_MULTICAST_BYTES_RCV_SUPPORTED       0x00080000
#endif

#ifndef NDIS_STATISTICS_MULTICAST_FRAMES_RCV_SUPPORTED
#define NDIS_STATISTICS_MULTICAST_FRAMES_RCV_SUPPORTED      0x00100000
#endif

#ifndef NDIS_STATISTICS_BROADCAST_BYTES_RCV_SUPPORTED
#define NDIS_STATISTICS_BROADCAST_BYTES_RCV_SUPPORTED       0x00200000
#endif

#ifndef NDIS_STATISTICS_BROADCAST_FRAMES_RCV_SUPPORTED
#define NDIS_STATISTICS_BROADCAST_FRAMES_RCV_SUPPORTED      0x00400000
#endif

#ifndef NDIS_STATISTICS_BYTES_RCV_SUPPORTED
#define NDIS_STATISTICS_BYTES_RCV_SUPPORTED                 0x00800000
#endif

#ifndef NDIS_STATISTICS_BYTES_XMIT_SUPPORTED
#define NDIS_STATISTICS_BYTES_XMIT_SUPPORTED                0x01000000
#endif

#ifndef NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED
#define NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED            0x02000000
#endif

/* ============================================================================
 * NDIS Link State Flags
 * ============================================================================ */

#ifndef NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED
#define NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED     0x00000001
#endif

#ifndef NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED
#define NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED      0x00000002
#endif

#ifndef NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED
#define NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED              0x00000004
#endif

/* ============================================================================
 * NDIS Miniport Adapter Offload Attributes
 * ============================================================================ */

#ifndef NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1
#define NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1 1
#endif

#ifndef NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_DEFINED
typedef struct _NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES {
    NDIS_OBJECT_HEADER      Header;
    PNDIS_OFFLOAD           DefaultOffloadConfiguration;
    PNDIS_OFFLOAD           HardwareOffloadCapabilities;
    PVOID                   DefaultTcpConnectionOffloadConfiguration;
    PVOID                   TcpConnectionOffloadHardwareCapabilities;
} NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES, *PNDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES;
#define NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_DEFINED 1
#endif

/* ============================================================================
 * NdisMGetBusData Compatibility
 * ============================================================================ */

#ifndef NdisMGetBusData
#define NdisMGetBusData(Handle, WhichSpace, Offset, Buffer, Length) \
    HalGetBusDataByOffset(PCIConfiguration, 0, 0, Buffer, Offset, Length)
#endif

/* ============================================================================
 * NDIS 6.x Scatter-Gather DMA Constants
 * ============================================================================ */

#ifndef NDIS_SG_DMA_DESCRIPTION_REVISION_1
#define NDIS_SG_DMA_DESCRIPTION_REVISION_1  1
#endif

#ifndef NDIS_SG_DMA_64_BIT_ADDRESS
#define NDIS_SG_DMA_64_BIT_ADDRESS          0x00000001
#endif

/* ============================================================================
 * NDIS 6.20+ Receive Throttle Parameters
 *
 * Used for budget-based receive processing (NAPI-style).
 * NDIS passes this to MiniportInterruptDpc to limit how many
 * packets the driver should indicate per DPC.
 * ============================================================================ */

/* Indicate all available NBLs without limit */
#ifndef NDIS_INDICATE_ALL_NBLS
#define NDIS_INDICATE_ALL_NBLS              ((ULONG)-1)
#endif

/* Feature flag indicating NDIS 6.20+ support */
#ifndef NDIS_SUPPORT_NDIS620
#if (NDIS_MINIPORT_MAJOR_VERSION >= 6) && (NDIS_MINIPORT_MINOR_VERSION >= 20)
#define NDIS_SUPPORT_NDIS620                1
#endif
#endif

#ifndef NDIS_RECEIVE_THROTTLE_PARAMETERS_DEFINED
/*
 * NDIS_RECEIVE_THROTTLE_PARAMETERS structure
 *
 * Passed to MiniportInterruptDpc in ReceiveThrottleParameters parameter
 * for NDIS 6.20 and later.
 *
 * MaxNblsToIndicate: Maximum number of NBLs the driver should indicate.
 *                    Set to NDIS_INDICATE_ALL_NBLS if no limit.
 * MoreNblsPending:   Output - driver sets TRUE if more NBLs are pending
 *                    after reaching the budget limit. NDIS will reschedule.
 */
typedef struct _NDIS_RECEIVE_THROTTLE_PARAMETERS {
    ULONG   MaxNblsToIndicate;
    ULONG   MoreNblsPending;
} NDIS_RECEIVE_THROTTLE_PARAMETERS, *PNDIS_RECEIVE_THROTTLE_PARAMETERS;
#define NDIS_RECEIVE_THROTTLE_PARAMETERS_DEFINED 1
#endif

/* EOF */
