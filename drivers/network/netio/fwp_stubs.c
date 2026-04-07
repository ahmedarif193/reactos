/*
 * PROJECT:     NetIO driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * FILE:        drivers/network/netio/fwp_stubs.c
 * PURPOSE:     Windows Filtering Platform kernel stub entry points.
 *              On real Windows these live in fwpkclnt.sys; ReactOS colocates
 *              them with netio.sys (and the BFE user-mode service provides
 *              the management side).
 *
 *              All functions are functional stubs: FwpsCalloutRegister0
 *              records the callout in a table and returns success, while
 *              classify path APIs return STATUS_NOT_SUPPORTED. The callouts
 *              never actually fire, so any driver that tries to inspect a
 *              packet simply never sees one.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors.
 */

#include <ntifs.h>
#include <ndis.h>
#include <fwpsk.h>
#include <fwpmk.h>

#define FWP_MAX_CALLOUTS  64
#define FWP_TAG           'pwFR'

typedef struct _FWP_CALLOUT_ENTRY {
    BOOLEAN     InUse;
    UINT32      CalloutId;
    FWPS_CALLOUT0 Callout;
} FWP_CALLOUT_ENTRY, *PFWP_CALLOUT_ENTRY;

static FWP_CALLOUT_ENTRY g_FwpCallouts[FWP_MAX_CALLOUTS];
static KSPIN_LOCK        g_FwpLock;
static UINT32            g_FwpNextCalloutId = 1;
static BOOLEAN           g_FwpInitialized = FALSE;

static VOID
FwpEnsureInitialized(VOID)
{
    if (!g_FwpInitialized)
    {
        KeInitializeSpinLock(&g_FwpLock);
        RtlZeroMemory(g_FwpCallouts, sizeof(g_FwpCallouts));
        g_FwpInitialized = TRUE;
    }
}

/* ------------------------------------------------------------------ */
/*  Callout registration (FWPS_* kernel API)                          */
/* ------------------------------------------------------------------ */

NTSTATUS NTAPI
FwpsCalloutRegister0(
    _Inout_ void* deviceObject,
    _In_    const FWPS_CALLOUT0* callout,
    _Out_opt_ UINT32* calloutId)
{
    KIRQL oldIrql;
    ULONG i;

    UNREFERENCED_PARAMETER(deviceObject);
    if (callout == NULL) return STATUS_INVALID_PARAMETER;

    FwpEnsureInitialized();
    KeAcquireSpinLock(&g_FwpLock, &oldIrql);
    for (i = 0; i < FWP_MAX_CALLOUTS; i++)
    {
        if (!g_FwpCallouts[i].InUse)
        {
            g_FwpCallouts[i].InUse = TRUE;
            g_FwpCallouts[i].CalloutId = g_FwpNextCalloutId++;
            g_FwpCallouts[i].Callout = *callout;
            if (calloutId) *calloutId = g_FwpCallouts[i].CalloutId;
            KeReleaseSpinLock(&g_FwpLock, oldIrql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_FwpLock, oldIrql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI
FwpsCalloutUnregisterById0(
    _In_ const UINT32 calloutId)
{
    KIRQL oldIrql;
    ULONG i;

    KeAcquireSpinLock(&g_FwpLock, &oldIrql);
    for (i = 0; i < FWP_MAX_CALLOUTS; i++)
    {
        if (g_FwpCallouts[i].InUse && g_FwpCallouts[i].CalloutId == calloutId)
        {
            g_FwpCallouts[i].InUse = FALSE;
            KeReleaseSpinLock(&g_FwpLock, oldIrql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_FwpLock, oldIrql);
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS NTAPI
FwpsCalloutUnregisterByKey0(
    _In_ const GUID* calloutKey)
{
    KIRQL oldIrql;
    ULONG i;

    KeAcquireSpinLock(&g_FwpLock, &oldIrql);
    for (i = 0; i < FWP_MAX_CALLOUTS; i++)
    {
        if (g_FwpCallouts[i].InUse &&
            RtlCompareMemory(&g_FwpCallouts[i].Callout.calloutKey, calloutKey, sizeof(GUID)) == sizeof(GUID))
        {
            g_FwpCallouts[i].InUse = FALSE;
            KeReleaseSpinLock(&g_FwpLock, oldIrql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_FwpLock, oldIrql);
    return STATUS_INVALID_PARAMETER;
}

/* ------------------------------------------------------------------ */
/*  Network buffer list helpers — all stubs                           */
/* ------------------------------------------------------------------ */

NTSTATUS NTAPI
FwpsAllocateCloneNetBufferList0(
    _In_ struct _NET_BUFFER_LIST* originalNetBufferList,
    _In_opt_ NDIS_HANDLE nblPoolHandle,
    _In_opt_ NDIS_HANDLE nbPoolHandle,
    _In_ UINT32 cloneFlags,
    _Out_ struct _NET_BUFFER_LIST** netBufferList)
{
    UNREFERENCED_PARAMETER(originalNetBufferList);
    UNREFERENCED_PARAMETER(nblPoolHandle);
    UNREFERENCED_PARAMETER(nbPoolHandle);
    UNREFERENCED_PARAMETER(cloneFlags);
    if (netBufferList) *netBufferList = NULL;
    return STATUS_NOT_SUPPORTED;
}

VOID NTAPI
FwpsFreeCloneNetBufferList0(
    _Inout_ struct _NET_BUFFER_LIST* cloneNetBufferList,
    _In_ UINT32 freeCloneFlags)
{
    UNREFERENCED_PARAMETER(cloneNetBufferList);
    UNREFERENCED_PARAMETER(freeCloneFlags);
}

NTSTATUS NTAPI
FwpsAllocateNetBufferAndNetBufferList0(
    _In_ NDIS_HANDLE poolHandle,
    _In_ USHORT contextSize,
    _In_ USHORT contextBackFill,
    _In_opt_ PMDL mdlChain,
    _In_ ULONG dataOffset,
    _In_ SIZE_T dataLength,
    _Out_ struct _NET_BUFFER_LIST** netBufferList)
{
    UNREFERENCED_PARAMETER(poolHandle);
    UNREFERENCED_PARAMETER(contextSize);
    UNREFERENCED_PARAMETER(contextBackFill);
    UNREFERENCED_PARAMETER(mdlChain);
    UNREFERENCED_PARAMETER(dataOffset);
    UNREFERENCED_PARAMETER(dataLength);
    if (netBufferList) *netBufferList = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI
FwpsInjectNetworkSendAsync0(
    _In_ HANDLE injectionHandle,
    _In_opt_ HANDLE injectionContext,
    _In_ UINT32 flags,
    _In_ UINT16 compartmentId,
    _In_ struct _NET_BUFFER_LIST* netBufferList,
    _In_ PVOID completionFn,
    _In_opt_ HANDLE completionContext)
{
    UNREFERENCED_PARAMETER(injectionHandle);
    UNREFERENCED_PARAMETER(injectionContext);
    UNREFERENCED_PARAMETER(flags);
    UNREFERENCED_PARAMETER(compartmentId);
    UNREFERENCED_PARAMETER(netBufferList);
    UNREFERENCED_PARAMETER(completionFn);
    UNREFERENCED_PARAMETER(completionContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI
FwpsInjectNetworkReceiveAsync0(
    _In_ HANDLE injectionHandle,
    _In_opt_ HANDLE injectionContext,
    _In_ UINT32 flags,
    _In_ UINT16 compartmentId,
    _In_ UINT16 interfaceIndex,
    _In_ UINT16 subInterfaceIndex,
    _In_ struct _NET_BUFFER_LIST* netBufferList,
    _In_ PVOID completionFn,
    _In_opt_ HANDLE completionContext)
{
    UNREFERENCED_PARAMETER(injectionHandle);
    UNREFERENCED_PARAMETER(injectionContext);
    UNREFERENCED_PARAMETER(flags);
    UNREFERENCED_PARAMETER(compartmentId);
    UNREFERENCED_PARAMETER(interfaceIndex);
    UNREFERENCED_PARAMETER(subInterfaceIndex);
    UNREFERENCED_PARAMETER(netBufferList);
    UNREFERENCED_PARAMETER(completionFn);
    UNREFERENCED_PARAMETER(completionContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI
FwpsInjectionHandleCreate0(
    _In_opt_ UINT16 addressFamily,
    _In_ UINT32 flags,
    _Out_ HANDLE* injectionHandle)
{
    UNREFERENCED_PARAMETER(addressFamily);
    UNREFERENCED_PARAMETER(flags);
    if (injectionHandle == NULL) return STATUS_INVALID_PARAMETER;
    *injectionHandle = (HANDLE)(ULONG_PTR)0xFF0A0000u;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpsInjectionHandleDestroy0(
    _In_ HANDLE injectionHandle)
{
    UNREFERENCED_PARAMETER(injectionHandle);
    return STATUS_SUCCESS;
}

VOID NTAPI
FwpsNetBufferListRelease0(
    _Inout_ struct _NET_BUFFER_LIST* netBufferList)
{
    UNREFERENCED_PARAMETER(netBufferList);
}

/* ------------------------------------------------------------------ */
/*  Management API (FWPM_* kernel side) — all stubs                   */
/* ------------------------------------------------------------------ */

NTSTATUS NTAPI
FwpmEngineOpen0(
    _In_opt_ const wchar_t* serverName,
    _In_ UINT32 authnService,
    _In_opt_ PVOID authIdentity,
    _In_opt_ PVOID session,
    _Out_ HANDLE* engineHandle)
{
    UNREFERENCED_PARAMETER(serverName);
    UNREFERENCED_PARAMETER(authnService);
    UNREFERENCED_PARAMETER(authIdentity);
    UNREFERENCED_PARAMETER(session);
    if (engineHandle == NULL) return STATUS_INVALID_PARAMETER;
    *engineHandle = (HANDLE)(ULONG_PTR)0xFF0B0000u;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmEngineClose0(_In_ HANDLE engineHandle)
{
    UNREFERENCED_PARAMETER(engineHandle);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmTransactionBegin0(_In_ HANDLE engineHandle, _In_ UINT32 flags)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(flags);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmTransactionCommit0(_In_ HANDLE engineHandle)
{
    UNREFERENCED_PARAMETER(engineHandle);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmTransactionAbort0(_In_ HANDLE engineHandle)
{
    UNREFERENCED_PARAMETER(engineHandle);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmFilterAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_FILTER0* filter,
    _In_opt_ PVOID sd,
    _Out_opt_ UINT64* id)
{
    static UINT64 s_next = 1;
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(filter);
    UNREFERENCED_PARAMETER(sd);
    if (id) *id = s_next++;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmFilterDeleteByKey0(_In_ HANDLE engineHandle, _In_ const GUID* key)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(key);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmFilterDeleteById0(_In_ HANDLE engineHandle, _In_ UINT64 id)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(id);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmFilterGetByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID* key,
    _Out_ FWPM_FILTER0** filter)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(key);
    if (filter) *filter = NULL;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI
FwpmFilterGetById0(
    _In_ HANDLE engineHandle,
    _In_ UINT64 id,
    _Out_ FWPM_FILTER0** filter)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(id);
    if (filter) *filter = NULL;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI
FwpmCalloutAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_CALLOUT0* callout,
    _In_opt_ PVOID sd,
    _Out_opt_ UINT32* id)
{
    static UINT32 s_next = 1;
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(callout);
    UNREFERENCED_PARAMETER(sd);
    if (id) *id = s_next++;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmCalloutDeleteByKey0(_In_ HANDLE engineHandle, _In_ const GUID* key)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(key);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmCalloutDeleteById0(_In_ HANDLE engineHandle, _In_ UINT32 id)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(id);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmProviderAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_PROVIDER0* provider,
    _In_opt_ PVOID sd)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(provider);
    UNREFERENCED_PARAMETER(sd);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
FwpmProviderDeleteByKey0(_In_ HANDLE engineHandle, _In_ const GUID* key)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(key);
    return STATUS_SUCCESS;
}

VOID NTAPI
FwpmFreeMemory0(_Inout_ PVOID* p)
{
    if (p) *p = NULL;
}

/* Well-known GUID storage for fwpvi.h externs */
const GUID FWPM_PROVIDER_MICROSOFT                    = {0};
const GUID FWPM_PROVIDER_IKEEXT                       = {0};
const GUID FWPM_PROVIDER_IPSEC_DOSP_CONFIG            = {0};
const GUID FWPM_SUBLAYER_UNIVERSAL                    = {0};
const GUID FWPM_SUBLAYER_IPSEC_TUNNEL                 = {0};
const GUID FWPM_SUBLAYER_IPSEC_FORWARD_OUTBOUND_TUNNEL = {0};

/* EOF */
