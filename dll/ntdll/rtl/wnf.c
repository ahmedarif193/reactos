/*
 * PROJECT:     ReactOS NT User-Mode DLL
 * PURPOSE:     Windows Notification Facility user-mode entry points
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
RtlQueryWnfStateData(
    _Out_ PULONG ChangeStamp,
    _In_ ULONGLONG StateName,
    _In_ PVOID Callback,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID TypeId,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(StateName);
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(CallbackContext);
    UNREFERENCED_PARAMETER(TypeId);
    UNREFERENCED_PARAMETER(Flags);

    DPRINT1("RtlQueryWnfStateData: no WNF provider\n");

    if (ChangeStamp)
        *ChangeStamp = 0;

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
RtlSubscribeWnfStateChangeNotification(
    _Outptr_ PVOID *Subscription,
    _In_ ULONGLONG StateName,
    _In_ ULONG ChangeStamp,
    _In_ PVOID Callback,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID TypeId,
    _In_ ULONG SerializationGroup,
    _In_ ULONG Unknown,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(StateName);
    UNREFERENCED_PARAMETER(ChangeStamp);
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(CallbackContext);
    UNREFERENCED_PARAMETER(TypeId);
    UNREFERENCED_PARAMETER(SerializationGroup);
    UNREFERENCED_PARAMETER(Unknown);
    UNREFERENCED_PARAMETER(Flags);

    DPRINT1("RtlSubscribeWnfStateChangeNotification: no WNF provider\n");

    if (Subscription)
        *Subscription = NULL;

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
RtlUnsubscribeWnfNotificationWaitForCompletion(
    _In_opt_ PVOID Subscription)
{
    UNREFERENCED_PARAMETER(Subscription);

    DPRINT1("RtlUnsubscribeWnfNotificationWaitForCompletion: no WNF provider\n");
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
RtlFlushHeaps(VOID)
{
    HANDLE Heaps[64];
    HANDLE *HeapArray = Heaps;
    ULONG Count, Index;
    SIZE_T Capacity;
    NTSTATUS Status = STATUS_SUCCESS;

    Count = RtlGetProcessHeaps(RTL_NUMBER_OF(Heaps), Heaps);
    Capacity = RTL_NUMBER_OF(Heaps);
    while (Count > Capacity)
    {
        if (HeapArray != Heaps)
            RtlFreeHeap(RtlGetProcessHeap(), 0, HeapArray);
        Capacity = Count;
        if (Capacity > MAXULONG_PTR / sizeof(HANDLE))
            return STATUS_NO_MEMORY;
        HeapArray = RtlAllocateHeap(RtlGetProcessHeap(), 0, (SIZE_T)Capacity * sizeof(HANDLE));
        if (!HeapArray)
            return STATUS_NO_MEMORY;

        Count = RtlGetProcessHeaps(Capacity, HeapArray);
    }

    for (Index = 0; Index < Count; Index++)
        RtlCompactHeap(HeapArray[Index], 0);

    if (HeapArray != Heaps)
        RtlFreeHeap(RtlGetProcessHeap(), 0, HeapArray);

    return Status;
}
