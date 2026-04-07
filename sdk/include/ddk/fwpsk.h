/*
 * fwpsk.h
 *
 * Windows Filtering Platform — kernel-mode callout driver API.
 * Header skeleton sufficient to let Win7-level callout drivers compile and
 * link against the stub BFE service in ReactOS.
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Created for the dev-nt6-1 branch as part of the NT 5.2 -> NT 6.1 upgrade.
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#pragma once
#define _FWPSK_

#include <fwpstypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FWPSAPI NTAPI

/* ------------------------------------------------------------------ */
/* Classify callback signature                                         */
/* ------------------------------------------------------------------ */

typedef struct FWPS_FILTER0_ FWPS_FILTER0;

typedef void (NTAPI *FWPS_CALLOUT_CLASSIFY_FN0)(
  _In_     const FWPS_INCOMING_VALUES0*           inFixedValues,
  _In_     const FWPS_INCOMING_METADATA_VALUES0*  inMetaValues,
  _Inout_opt_ void*                               layerData,
  _In_opt_ const void*                            classifyContext,
  _In_     const FWPS_FILTER0*                    filter,
  _In_     UINT64                                 flowContext,
  _Inout_  FWPS_CLASSIFY_OUT0*                    classifyOut);

typedef NTSTATUS (NTAPI *FWPS_CALLOUT_NOTIFY_FN0)(
  _In_ UINT32        notifyType,
  _In_ const GUID*   filterKey,
  _Inout_ const FWPS_FILTER0* filter);

typedef void (NTAPI *FWPS_CALLOUT_FLOW_DELETE_NOTIFY_FN0)(
  _In_ UINT16 layerId,
  _In_ UINT32 calloutId,
  _In_ UINT64 flowContext);

/* ------------------------------------------------------------------ */
/* Callout registration                                                */
/* ------------------------------------------------------------------ */

typedef struct FWPS_CALLOUT0_ {
  GUID                                calloutKey;
  UINT32                              flags;
  FWPS_CALLOUT_CLASSIFY_FN0           classifyFn;
  FWPS_CALLOUT_NOTIFY_FN0             notifyFn;
  FWPS_CALLOUT_FLOW_DELETE_NOTIFY_FN0 flowDeleteFn;
} FWPS_CALLOUT0;

/* Callout registration flags */
#define FWP_CALLOUT_FLAG_PERSISTENT                     0x00010000
#define FWP_CALLOUT_FLAG_USES_PROVIDER_CONTEXT          0x00040000
#define FWP_CALLOUT_FLAG_REGISTERED_AT_WFP_STARTUP      0x00080000
#define FWP_CALLOUT_FLAG_CONDITIONAL_ON_FLOW            0x00100000

NTSTATUS FWPSAPI
FwpsCalloutRegister0(
  _Inout_ void*                deviceObject,
  _In_    const FWPS_CALLOUT0* callout,
  _Out_opt_ UINT32*             calloutId);

NTSTATUS FWPSAPI
FwpsCalloutUnregisterById0(
  _In_ const UINT32 calloutId);

NTSTATUS FWPSAPI
FwpsCalloutUnregisterByKey0(
  _In_ const GUID* calloutKey);

/* ------------------------------------------------------------------ */
/* Packet allocation + injection helpers                               */
/* ------------------------------------------------------------------ */

struct _NET_BUFFER_LIST;
struct _NET_BUFFER;

NTSTATUS FWPSAPI
FwpsAllocateCloneNetBufferList0(
  _In_ struct _NET_BUFFER_LIST* originalNetBufferList,
  _In_opt_ NDIS_HANDLE          nblPoolHandle,
  _In_opt_ NDIS_HANDLE          nbPoolHandle,
  _In_ UINT32                   cloneFlags,
  _Out_ struct _NET_BUFFER_LIST** netBufferList);

VOID FWPSAPI
FwpsFreeCloneNetBufferList0(
  _Inout_ struct _NET_BUFFER_LIST* cloneNetBufferList,
  _In_    UINT32                   freeCloneFlags);

NTSTATUS FWPSAPI
FwpsAllocateNetBufferAndNetBufferList0(
  _In_ NDIS_HANDLE  poolHandle,
  _In_ USHORT       contextSize,
  _In_ USHORT       contextBackFill,
  _In_opt_ PMDL     mdlChain,
  _In_ ULONG        dataOffset,
  _In_ SIZE_T       dataLength,
  _Out_ struct _NET_BUFFER_LIST** netBufferList);

NTSTATUS FWPSAPI
FwpsInjectNetworkSendAsync0(
  _In_ HANDLE       injectionHandle,
  _In_opt_ HANDLE   injectionContext,
  _In_ UINT32       flags,
  _In_ UINT16       compartmentId,
  _In_ struct _NET_BUFFER_LIST* netBufferList,
  _In_ PVOID        completionFn,
  _In_opt_ HANDLE   completionContext);

NTSTATUS FWPSAPI
FwpsInjectNetworkReceiveAsync0(
  _In_ HANDLE       injectionHandle,
  _In_opt_ HANDLE   injectionContext,
  _In_ UINT32       flags,
  _In_ UINT16       compartmentId,
  _In_ UINT16       interfaceIndex,
  _In_ UINT16       subInterfaceIndex,
  _In_ struct _NET_BUFFER_LIST* netBufferList,
  _In_ PVOID        completionFn,
  _In_opt_ HANDLE   completionContext);

NTSTATUS FWPSAPI
FwpsInjectionHandleCreate0(
  _In_opt_ UINT16 addressFamily,
  _In_     UINT32 flags,
  _Out_    HANDLE* injectionHandle);

NTSTATUS FWPSAPI
FwpsInjectionHandleDestroy0(
  _In_ HANDLE injectionHandle);

VOID FWPSAPI
FwpsNetBufferListRelease0(
  _Inout_ struct _NET_BUFFER_LIST* netBufferList);

#ifdef __cplusplus
}
#endif

/* EOF */
