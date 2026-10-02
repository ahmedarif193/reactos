/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private interface between the WFP filter engine and the TCP/IP stack
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WFPSHIM_H_
#define _WFPSHIM_H_

typedef struct _WFP_SHIM_TAG
{
    PVOID Handle;
    PVOID Context;
    PVOID Previous;
} WFP_SHIM_TAG, *PWFP_SHIM_TAG;

typedef struct _WFP_SHIM_DATAGRAM
{
    ULONG64 EndpointId;
    BOOLEAN Outbound;
    BOOLEAN Loopback;
    UCHAR Protocol;
    ULONG LocalAddress;
    ULONG RemoteAddress;
    USHORT LocalPort;
    USHORT RemotePort;
    ULONG InterfaceIndex;
    ULONG InterfaceType;
    HANDLE ProcessId;
    const VOID *IpHeader;
    ULONG IpHeaderSize;
    const VOID *TransportHeader;
    ULONG TransportHeaderSize;
    const VOID *Data;
    ULONG DataLength;
    WFP_SHIM_TAG Tag;
} WFP_SHIM_DATAGRAM, *PWFP_SHIM_DATAGRAM;

#define WFP_SHIM_STREAM_SEND       0x1
#define WFP_SHIM_STREAM_PUSH       0x2
#define WFP_SHIM_STREAM_DISCONNECT 0x4

typedef struct _WFP_SHIM_FLOW
{
    ULONG64 EndpointId;
    BOOLEAN Outbound;
    BOOLEAN Loopback;
    UCHAR Protocol;
    ULONG LocalAddress;
    ULONG RemoteAddress;
    USHORT LocalPort;
    USHORT RemotePort;
    ULONG InterfaceType;
    HANDLE ProcessId;
} WFP_SHIM_FLOW, *PWFP_SHIM_FLOW;

typedef
VOID
(NTAPI WFP_SHIM_COMPLETE)(
    _In_ PVOID Context,
    _In_ NTSTATUS Status);
typedef WFP_SHIM_COMPLETE *PWFP_SHIM_COMPLETE;

typedef
NTSTATUS
(NTAPI WFP_SHIM_INJECT_SEND)(
    _In_ ULONG64 EndpointId,
    _In_ ULONG RemoteAddress,
    _In_reads_bytes_(Length) const VOID *Datagram,
    _In_ ULONG Length,
    _In_ const WFP_SHIM_TAG *Tag,
    _In_ PWFP_SHIM_COMPLETE Complete,
    _In_ PVOID Context);
typedef WFP_SHIM_INJECT_SEND *PWFP_SHIM_INJECT_SEND;

typedef
NTSTATUS
(NTAPI WFP_SHIM_INJECT_RECEIVE)(
    _In_ ULONG InterfaceIndex,
    _In_reads_bytes_(Length) const VOID *Packet,
    _In_ ULONG Length,
    _In_ const WFP_SHIM_TAG *Tag,
    _In_ PWFP_SHIM_COMPLETE Complete,
    _In_ PVOID Context);
typedef WFP_SHIM_INJECT_RECEIVE *PWFP_SHIM_INJECT_RECEIVE;

typedef struct _WFP_SHIM_DISPATCH
{
    PWFP_SHIM_INJECT_SEND InjectSend;
    PWFP_SHIM_INJECT_RECEIVE InjectReceive;
} WFP_SHIM_DISPATCH, *PWFP_SHIM_DISPATCH;

VOID
NTAPI
WfpShimRegister(
    _In_opt_ const WFP_SHIM_DISPATCH *Dispatch);

BOOLEAN
NTAPI
WfpShimDatagramActive(VOID);

BOOLEAN
NTAPI
WfpShimClassifyDatagram(
    _In_ const WFP_SHIM_DATAGRAM *Datagram);

BOOLEAN
NTAPI
WfpShimStreamActive(VOID);

BOOLEAN
NTAPI
WfpShimEstablishFlow(
    _In_ const WFP_SHIM_FLOW *Flow);

BOOLEAN
NTAPI
WfpShimClassifyStream(
    _In_ ULONG64 EndpointId,
    _In_ ULONG StreamFlags,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length);

VOID
NTAPI
WfpShimEndpointClosed(
    _In_ ULONG64 EndpointId);

#endif
