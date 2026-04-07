/*
 * PROJECT:     NetIO driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * FILE:        drivers/network/netio/nmr_stubs.c
 * PURPOSE:     Network Module Registrar (NMR) stub entry points.
 *              On real Windows these live in netio.sys, so this file
 *              hosts the ReactOS equivalents at the same layer.
 *
 *              All functions are functional stubs — they record the
 *              client/provider characteristics in a static table but do
 *              not actually dispatch attach callbacks. A follow-up phase
 *              (G/I in the dev-nt6-1 upgrade plan) will make them real.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors.
 */

#include <ntifs.h>
#include <ndis.h>
#include <netioddk.h>

#define NMR_MAX_CLIENTS    32
#define NMR_MAX_PROVIDERS  32
#define NMR_TAG            'rmNR'

typedef struct _NMR_CLIENT_ENTRY {
    BOOLEAN InUse;
    PNPI_CLIENT_CHARACTERISTICS Characteristics;
    PVOID ClientContext;
} NMR_CLIENT_ENTRY, *PNMR_CLIENT_ENTRY;

typedef struct _NMR_PROVIDER_ENTRY {
    BOOLEAN InUse;
    PNPI_PROVIDER_CHARACTERISTICS Characteristics;
    PVOID ProviderContext;
} NMR_PROVIDER_ENTRY, *PNMR_PROVIDER_ENTRY;

static NMR_CLIENT_ENTRY   g_NmrClients[NMR_MAX_CLIENTS];
static NMR_PROVIDER_ENTRY g_NmrProviders[NMR_MAX_PROVIDERS];
static KSPIN_LOCK         g_NmrLock;
static BOOLEAN            g_NmrInitialized = FALSE;

static VOID
NmrEnsureInitialized(VOID)
{
    if (!g_NmrInitialized)
    {
        KeInitializeSpinLock(&g_NmrLock);
        RtlZeroMemory(g_NmrClients, sizeof(g_NmrClients));
        RtlZeroMemory(g_NmrProviders, sizeof(g_NmrProviders));
        g_NmrInitialized = TRUE;
    }
}

NTSTATUS
NTAPI
NmrRegisterClient(
    _In_ PNPI_CLIENT_CHARACTERISTICS ClientCharacteristics,
    _In_opt_ PVOID ClientContext,
    _Out_ PHANDLE NmrClientHandle)
{
    KIRQL oldIrql;
    ULONG i;

    NmrEnsureInitialized();
    if (NmrClientHandle == NULL) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_NmrLock, &oldIrql);
    for (i = 0; i < NMR_MAX_CLIENTS; i++)
    {
        if (!g_NmrClients[i].InUse)
        {
            g_NmrClients[i].InUse = TRUE;
            g_NmrClients[i].Characteristics = ClientCharacteristics;
            g_NmrClients[i].ClientContext = ClientContext;
            KeReleaseSpinLock(&g_NmrLock, oldIrql);
            *NmrClientHandle = (HANDLE)(ULONG_PTR)(0x01000000u | i);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_NmrLock, oldIrql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
NTAPI
NmrDeregisterClient(
    _In_ HANDLE NmrClientHandle)
{
    KIRQL oldIrql;
    ULONG i = (ULONG)((ULONG_PTR)NmrClientHandle & 0x00FFFFFFu);

    if (i >= NMR_MAX_CLIENTS) return STATUS_INVALID_HANDLE;

    KeAcquireSpinLock(&g_NmrLock, &oldIrql);
    if (!g_NmrClients[i].InUse)
    {
        KeReleaseSpinLock(&g_NmrLock, oldIrql);
        return STATUS_INVALID_HANDLE;
    }
    g_NmrClients[i].InUse = FALSE;
    g_NmrClients[i].Characteristics = NULL;
    g_NmrClients[i].ClientContext = NULL;
    KeReleaseSpinLock(&g_NmrLock, oldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NmrWaitForClientDeregisterComplete(
    _In_ HANDLE NmrClientHandle)
{
    UNREFERENCED_PARAMETER(NmrClientHandle);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NmrRegisterProvider(
    _In_ PNPI_PROVIDER_CHARACTERISTICS ProviderCharacteristics,
    _In_opt_ PVOID ProviderContext,
    _Out_ PHANDLE NmrProviderHandle)
{
    KIRQL oldIrql;
    ULONG i;

    NmrEnsureInitialized();
    if (NmrProviderHandle == NULL) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_NmrLock, &oldIrql);
    for (i = 0; i < NMR_MAX_PROVIDERS; i++)
    {
        if (!g_NmrProviders[i].InUse)
        {
            g_NmrProviders[i].InUse = TRUE;
            g_NmrProviders[i].Characteristics = ProviderCharacteristics;
            g_NmrProviders[i].ProviderContext = ProviderContext;
            KeReleaseSpinLock(&g_NmrLock, oldIrql);
            *NmrProviderHandle = (HANDLE)(ULONG_PTR)(0x02000000u | i);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_NmrLock, oldIrql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
NTAPI
NmrDeregisterProvider(
    _In_ HANDLE NmrProviderHandle)
{
    KIRQL oldIrql;
    ULONG i = (ULONG)((ULONG_PTR)NmrProviderHandle & 0x00FFFFFFu);

    if (i >= NMR_MAX_PROVIDERS) return STATUS_INVALID_HANDLE;

    KeAcquireSpinLock(&g_NmrLock, &oldIrql);
    if (!g_NmrProviders[i].InUse)
    {
        KeReleaseSpinLock(&g_NmrLock, oldIrql);
        return STATUS_INVALID_HANDLE;
    }
    g_NmrProviders[i].InUse = FALSE;
    g_NmrProviders[i].Characteristics = NULL;
    g_NmrProviders[i].ProviderContext = NULL;
    KeReleaseSpinLock(&g_NmrLock, oldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NmrWaitForProviderDeregisterComplete(
    _In_ HANDLE NmrProviderHandle)
{
    UNREFERENCED_PARAMETER(NmrProviderHandle);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NmrClientAttachProvider(
    _In_ HANDLE NmrBindingHandle,
    _In_ PVOID ClientBindingContext,
    _In_ CONST VOID *ClientDispatch,
    _Out_ PVOID *ProviderBindingContext,
    _Out_ CONST VOID* *ProviderDispatch)
{
    /* Stub: no provider is actually attached, so return STATUS_NOINTERFACE.
     * Real implementation would look up the binding handle, find a matching
     * provider, and invoke its ProviderAttachClient callback. */
    UNREFERENCED_PARAMETER(NmrBindingHandle);
    UNREFERENCED_PARAMETER(ClientBindingContext);
    UNREFERENCED_PARAMETER(ClientDispatch);
    if (ProviderBindingContext) *ProviderBindingContext = NULL;
    if (ProviderDispatch) *ProviderDispatch = NULL;
    return STATUS_NOINTERFACE;
}

VOID
NTAPI
NmrClientDetachProviderComplete(
    _In_ HANDLE NmrBindingHandle)
{
    UNREFERENCED_PARAMETER(NmrBindingHandle);
}

VOID
NTAPI
NmrProviderDetachClientComplete(
    _In_ HANDLE NmrBindingHandle)
{
    UNREFERENCED_PARAMETER(NmrBindingHandle);
}

/* EOF */
