/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WFP engine state, provider context and IPsec SA entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>

#define FWP_BFE_SUBSCRIPTION_TAG 'sBwF'

typedef enum _NETIO_FWPM_SERVICE_STATE
{
    NetioFwpmServiceStopped = 0,
    NetioFwpmServiceStartPending,
    NetioFwpmServiceStopPending,
    NetioFwpmServiceRunning
} NETIO_FWPM_SERVICE_STATE;

typedef VOID (NTAPI *NETIO_FWPM_SERVICE_STATE_CHANGE_CALLBACK)(
    _Inout_ PVOID Context,
    _In_ NETIO_FWPM_SERVICE_STATE NewState);

typedef struct _NETIO_BFE_SUBSCRIPTION
{
    ULONG Signature;
    NETIO_FWPM_SERVICE_STATE_CHANGE_CALLBACK Callback;
    PVOID Context;
} NETIO_BFE_SUBSCRIPTION, *PNETIO_BFE_SUBSCRIPTION;

NETIO_FWPM_SERVICE_STATE
NTAPI
FwpmBfeStateGet0(VOID)
{
    return NetioFwpmServiceRunning;
}

NTSTATUS
NTAPI
FwpmBfeStateSubscribeChanges0(
    _Inout_ PVOID DeviceObject,
    _In_ NETIO_FWPM_SERVICE_STATE_CHANGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ HANDLE *ChangeHandle)
{
    PNETIO_BFE_SUBSCRIPTION Subscription;

    UNREFERENCED_PARAMETER(DeviceObject);

    if ((Callback == NULL) || (ChangeHandle == NULL))
        return STATUS_INVALID_PARAMETER;
    *ChangeHandle = NULL;

    Subscription = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Subscription), FWP_BFE_SUBSCRIPTION_TAG);
    if (Subscription == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Subscription->Signature = FWP_BFE_SUBSCRIPTION_TAG;
    Subscription->Callback = Callback;
    Subscription->Context = Context;
    *ChangeHandle = Subscription;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpmBfeStateUnsubscribeChanges0(
    _Inout_ HANDLE ChangeHandle)
{
    PNETIO_BFE_SUBSCRIPTION Subscription = ChangeHandle;

    if ((Subscription == NULL) || (Subscription->Signature != FWP_BFE_SUBSCRIPTION_TAG))
        return STATUS_INVALID_HANDLE;

    Subscription->Signature = 0;
    ExFreePoolWithTag(Subscription, FWP_BFE_SUBSCRIPTION_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpmProviderContextAdd3(
    _In_ HANDLE EngineHandle,
    _In_ CONST VOID *ProviderContext,
    _In_opt_ PSECURITY_DESCRIPTOR Sd,
    _Out_opt_ UINT64 *Id)
{
    static LONG64 NextId;

    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(Sd);

    if (ProviderContext == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Id != NULL)
        *Id = (UINT64)InterlockedIncrement64(&NextId);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpmProviderContextDeleteById0(
    _In_ HANDLE EngineHandle,
    _In_ UINT64 Id)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(Id);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpsVirtualIfTunnelInfoSet0(
    _Inout_ PVOID NetBufferList,
    _In_ CONST VOID *VirtualIfTunnelInfo)
{
    UNREFERENCED_PARAMETER(NetBufferList);
    UNREFERENCED_PARAMETER(VirtualIfTunnelInfo);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecGetStatistics0(
    _In_ HANDLE EngineHandle,
    _Out_ PVOID IpsecStatistics)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(IpsecStatistics);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaContextCreate1(
    _In_ HANDLE EngineHandle,
    _In_ CONST VOID *OutboundTraffic,
    _In_opt_ CONST VOID *VirtualIfTunnelInfo,
    _Out_opt_ UINT64 *InboundFilterId,
    _Out_ UINT64 *Id)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(OutboundTraffic);
    UNREFERENCED_PARAMETER(VirtualIfTunnelInfo);

    if (InboundFilterId != NULL)
        *InboundFilterId = 0;
    if (Id != NULL)
        *Id = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaContextDeleteById0(
    _In_ HANDLE EngineHandle,
    _In_ UINT64 Id)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(Id);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaContextGetSpi1(
    _In_ HANDLE EngineHandle,
    _In_ UINT64 Id,
    _In_ CONST VOID *GetSpi,
    _Out_ UINT32 *InboundSpi)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(GetSpi);

    if (InboundSpi != NULL)
        *InboundSpi = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaContextSetSpi0(
    _In_ HANDLE EngineHandle,
    _In_ UINT64 Id,
    _In_ CONST VOID *GetSpi,
    _In_ UINT32 InboundSpi)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(GetSpi);
    UNREFERENCED_PARAMETER(InboundSpi);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaContextAddInbound1(
    _In_ HANDLE EngineHandle,
    _In_ UINT64 Id,
    _In_ CONST VOID *InboundBundle)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(InboundBundle);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaContextAddOutbound1(
    _In_ HANDLE EngineHandle,
    _In_ UINT64 Id,
    _In_ CONST VOID *OutboundBundle)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(OutboundBundle);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaContextCreateEnumHandle0(
    _In_ HANDLE EngineHandle,
    _In_opt_ CONST VOID *EnumTemplate,
    _Out_ HANDLE *EnumHandle)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(EnumTemplate);

    if (EnumHandle != NULL)
        *EnumHandle = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaContextEnum1(
    _In_ HANDLE EngineHandle,
    _In_ HANDLE EnumHandle,
    _In_ UINT32 NumEntriesRequested,
    _Out_ PVOID *Entries,
    _Out_ UINT32 *NumEntriesReturned)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(EnumHandle);
    UNREFERENCED_PARAMETER(NumEntriesRequested);

    if (Entries != NULL)
        *Entries = NULL;
    if (NumEntriesReturned != NULL)
        *NumEntriesReturned = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaCreateEnumHandle0(
    _In_ HANDLE EngineHandle,
    _In_opt_ CONST VOID *EnumTemplate,
    _Out_ HANDLE *EnumHandle)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(EnumTemplate);

    if (EnumHandle != NULL)
        *EnumHandle = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaEnum1(
    _In_ HANDLE EngineHandle,
    _In_ HANDLE EnumHandle,
    _In_ UINT32 NumEntriesRequested,
    _Out_ PVOID *Entries,
    _Out_ UINT32 *NumEntriesReturned)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(EnumHandle);
    UNREFERENCED_PARAMETER(NumEntriesRequested);

    if (Entries != NULL)
        *Entries = NULL;
    if (NumEntriesReturned != NULL)
        *NumEntriesReturned = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
IPsecSaDestroyEnumHandle0(
    _In_ HANDLE EngineHandle,
    _Inout_ HANDLE EnumHandle)
{
    UNREFERENCED_PARAMETER(EngineHandle);
    UNREFERENCED_PARAMETER(EnumHandle);
    return STATUS_INVALID_HANDLE;
}
