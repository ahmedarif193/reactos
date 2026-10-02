/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform filter engine, internal declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NETIO_WFP_H_
#define _NETIO_WFP_H_

#include <ntifs.h>
#include <ndis.h>
#include <fwpsk.h>
#include <fwpmk.h>
#include <ntstrsafe.h>
#include <drivers/wfp/wfpshim.h>

#define WFP_TAG 'pfWN'

#define WFP_STATE_UNCOMMITTED    0x01
#define WFP_STATE_DELETE_PENDING 0x02

#define WFP_MAX_LAYERS 128

typedef struct _WFP_LAYER
{
    GUID Key;
    const GUID *Fields;
    ULONG FieldCount;
} WFP_LAYER;

typedef struct _WFP_SESSION
{
    LIST_ENTRY Link;
    UINT32 Flags;
    UINT32 TransactionTimeout;
    BOOLEAN InTransaction;
} WFP_SESSION, *PWFP_SESSION;

typedef struct _WFP_PROVIDER
{
    LIST_ENTRY Link;
    GUID Key;
    PWFP_SESSION Owner;
    UCHAR State;
} WFP_PROVIDER, *PWFP_PROVIDER;

typedef struct _WFP_SUBLAYER
{
    LIST_ENTRY Link;
    GUID Key;
    UINT16 Weight;
    PWFP_SESSION Owner;
    UCHAR State;
} WFP_SUBLAYER, *PWFP_SUBLAYER;

typedef struct _WFP_CALLOUT
{
    LIST_ENTRY Link;
    GUID Key;
    UINT32 Id;
    LONG References;
    BOOLEAN Added;
    BOOLEAN Registered;
    UCHAR State;
    PWFP_SESSION Owner;
    UINT16 LayerId;
    UINT32 Flags;
    UCHAR Version;
    PVOID ClassifyFn;
    FWPS_CALLOUT_NOTIFY_FN0 NotifyFn;
    FWPS_CALLOUT_FLOW_DELETE_NOTIFY_FN0 FlowDeleteFn;
    LONG FlowContexts;
    LONG Active;
} WFP_CALLOUT, *PWFP_CALLOUT;

typedef struct _WFP_FILTER
{
    LIST_ENTRY Link;
    LONG References;
    GUID Key;
    UINT16 LayerId;
    UCHAR State;
    BOOLEAN Notified;
    PWFP_SESSION Owner;
    PWFP_SUBLAYER SubLayer;
    PWFP_CALLOUT Callout;
    UINT64 Weight;
    UINT32 Flags;
    NTSTATUS NotifyStatus;
    struct _WFP_FILTER *Next;
    FWPS_FILTER0 Runtime;
} WFP_FILTER, *PWFP_FILTER;

typedef struct _WFP_FLOW_CONTEXT
{
    LIST_ENTRY Link;
    struct _WFP_FLOW_CONTEXT *Next;
    UINT16 LayerId;
    PWFP_CALLOUT Callout;
    UINT64 Context;
    LONG Active;
    BOOLEAN Removed;
} WFP_FLOW_CONTEXT, *PWFP_FLOW_CONTEXT;

typedef struct _WFP_FLOW
{
    LIST_ENTRY Link;
    UINT64 Id;
    ULONG64 EndpointId;
    ULONG LocalAddress;
    ULONG RemoteAddress;
    USHORT LocalPort;
    USHORT RemotePort;
    UCHAR Protocol;
    BOOLEAN Outbound;
    BOOLEAN Loopback;
    LIST_ENTRY Contexts;
} WFP_FLOW, *PWFP_FLOW;

typedef struct _WFP_INJECTION
{
    LIST_ENTRY Link;
    ADDRESS_FAMILY Family;
    UINT32 Flags;
    LONG Pending;
    BOOLEAN Closing;
} WFP_INJECTION, *PWFP_INJECTION;

typedef struct _WFP_PACKET
{
    LIST_ENTRY Link;
    PNET_BUFFER_LIST NetBufferList;
    LONG References;
    PMDL Mdl;
    PVOID Buffer;
    WFP_SHIM_TAG Tag;
    FWPS_INJECT_COMPLETE0 CompletionFn;
    HANDLE CompletionContext;
    PWFP_INJECTION Injection;
    LONG Pending;
    NTSTATUS Status;
    BOOLEAN Owned;
} WFP_PACKET, *PWFP_PACKET;

typedef
PVOID
(WFP_LAYER_DATA_ROUTINE)(
    _In_opt_ PVOID Context);
typedef WFP_LAYER_DATA_ROUTINE *PWFP_LAYER_DATA_ROUTINE;

extern const WFP_LAYER WfpLayers[];
extern const ULONG WfpLayerCount;

extern KSPIN_LOCK WfpLock;
extern LIST_ENTRY WfpCallouts;
extern LONG WfpLayerFilters[WFP_MAX_LAYERS];
extern NDIS_HANDLE WfpNblPool;

VOID
WfpInitialize(VOID);

PWFP_CALLOUT
WfpFindCalloutById(
    _In_ UINT32 Id);

VOID
WfpDereferenceCallout(
    _In_ PWFP_CALLOUT Callout);

PWFP_FLOW_CONTEXT
WfpFindFlowContextLocked(
    _In_ UINT64 FlowId,
    _In_ UINT16 LayerId,
    _In_ PWFP_CALLOUT Callout);

VOID
WfpDeleteFlowContext(
    _In_ PWFP_FLOW_CONTEXT FlowContext);

VOID
WfpClassify(
    _In_ UINT16 LayerId,
    _In_reads_(ValueCount) FWPS_INCOMING_VALUE0 *Values,
    _In_ ULONG ValueCount,
    _In_ const FWPS_INCOMING_METADATA_VALUES0 *Metadata,
    _In_ UINT64 FlowId,
    _In_opt_ PWFP_LAYER_DATA_ROUTINE LayerDataRoutine,
    _In_opt_ PVOID LayerDataContext,
    _Out_ FWPS_CLASSIFY_OUT0 *ClassifyOut);

PWFP_PACKET
WfpCreatePacket(
    _In_reads_bytes_opt_(HeaderLength) const VOID *Header,
    _In_ ULONG HeaderLength,
    _In_reads_bytes_opt_(DataLength) const VOID *Data,
    _In_ ULONG DataLength,
    _In_ ULONG DataOffset,
    _In_ const WFP_SHIM_TAG *Tag);

VOID
WfpDereferencePacket(
    _In_ PWFP_PACKET Packet);

NTSTATUS
NTAPI
FwpsCalloutRegister1(
    _Inout_ void *deviceObject,
    _In_ const FWPS_CALLOUT3 *callout,
    _Out_opt_ UINT32 *calloutId);

NTSTATUS
NTAPI
FwpsCalloutRegister2(
    _Inout_ void *deviceObject,
    _In_ const FWPS_CALLOUT3 *callout,
    _Out_opt_ UINT32 *calloutId);

NTSTATUS
WfpCreateDevice(
    _In_ PDRIVER_OBJECT DriverObject);

NTSTATUS
NTAPI
FwpmSubLayerDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key);

#endif
