/*
 * PROJECT:     LiberNT Network Power Dependency Broker
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-mode network interface activation references
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

#define NPD_BROKER_TAG 'BdpN'

#define NETPWRDEPBROKER_CLIENT_WIFICALLING    0
#define NETPWRDEPBROKER_TIMEOUT_MS_USEDEFAULT (-1)
#define NETPWRDEPBROKER_TIMEOUT_MS_USEMIN     (-2)
#define NETPWRDEPBROKER_TIMEOUT_MS_USEMAX     (-3)

#define NPD_TIMEOUT_MS_DEFAULT 3000
#define NPD_TIMEOUT_MS_MIN     500
#define NPD_TIMEOUT_MS_MAX     30000

typedef struct _NPD_BROKER
{
    ULONG Signature;
    ULONG ClientId;
    KSPIN_LOCK Lock;
    KTIMER Timer;
    KDPC Dpc;
    BOOLEAN Acquired;
} NPD_BROKER, *PNPD_BROKER;

static KDEFERRED_ROUTINE NpdBrokerTimerDpc;

static
VOID
NTAPI
NpdBrokerTimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PNPD_BROKER Broker = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLockAtDpcLevel(&Broker->Lock);
    Broker->Acquired = FALSE;
    KeReleaseSpinLockFromDpcLevel(&Broker->Lock);
}

NTSTATUS
NTAPI
NpdBrokerInitialize(
    _In_ ULONG ulClientID,
    _Out_ PHANDLE phBroker)
{
    PNPD_BROKER Broker;

    PAGED_CODE();

    if (phBroker == NULL)
        return STATUS_INVALID_PARAMETER;
    *phBroker = NULL;
    if (ulClientID != NETPWRDEPBROKER_CLIENT_WIFICALLING)
        return STATUS_INVALID_PARAMETER;

    Broker = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Broker), NPD_BROKER_TAG);
    if (Broker == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Broker, sizeof(*Broker));
    Broker->Signature = NPD_BROKER_TAG;
    Broker->ClientId = ulClientID;
    KeInitializeSpinLock(&Broker->Lock);
    KeInitializeTimer(&Broker->Timer);
    KeInitializeDpc(&Broker->Dpc, NpdBrokerTimerDpc, Broker);

    *phBroker = Broker;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NpdBrokerUninitialize(
    _In_ HANDLE hBroker)
{
    PNPD_BROKER Broker = hBroker;

    PAGED_CODE();

    if (Broker == NULL || Broker->Signature != NPD_BROKER_TAG)
        return STATUS_INVALID_HANDLE;

    KeCancelTimer(&Broker->Timer);
    KeFlushQueuedDpcs();
    Broker->Signature = 0;
    ExFreePoolWithTag(Broker, NPD_BROKER_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NpdBrokerAcquireWithTimeout(
    _In_ HANDLE hBroker,
    _In_ LONG lTimeoutMS)
{
    PNPD_BROKER Broker = hBroker;
    LARGE_INTEGER DueTime;
    BOOLEAN WasAcquired;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Broker == NULL || Broker->Signature != NPD_BROKER_TAG)
        return STATUS_INVALID_HANDLE;

    if (lTimeoutMS == NETPWRDEPBROKER_TIMEOUT_MS_USEDEFAULT)
        lTimeoutMS = NPD_TIMEOUT_MS_DEFAULT;
    else if (lTimeoutMS == NETPWRDEPBROKER_TIMEOUT_MS_USEMIN)
        lTimeoutMS = NPD_TIMEOUT_MS_MIN;
    else if (lTimeoutMS == NETPWRDEPBROKER_TIMEOUT_MS_USEMAX)
        lTimeoutMS = NPD_TIMEOUT_MS_MAX;
    else if (lTimeoutMS < NPD_TIMEOUT_MS_MIN || lTimeoutMS > NPD_TIMEOUT_MS_MAX)
        return STATUS_INVALID_PARAMETER;

    DueTime.QuadPart = -10000LL * lTimeoutMS;

    KeAcquireSpinLock(&Broker->Lock, &OldIrql);
    WasAcquired = Broker->Acquired;
    Broker->Acquired = TRUE;
    KeSetTimer(&Broker->Timer, DueTime, &Broker->Dpc);
    KeReleaseSpinLock(&Broker->Lock, OldIrql);

    return WasAcquired ? STATUS_ABANDONED : STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    return STATUS_SUCCESS;
}
