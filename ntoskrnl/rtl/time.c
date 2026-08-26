/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NT 10+ RTL multi-clock query services
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef struct _RTL_MULTI_TIME_PRECISE
{
    ULONGLONG PerformanceCounter;
    ULONGLONG HostPerformanceCounter;
    ULONGLONG SystemTime;
} RTL_MULTI_TIME_PRECISE, *PRTL_MULTI_TIME_PRECISE;

NTSTATUS
NTAPI
RtlGetMultiTimePrecise(
    _Out_ PRTL_MULTI_TIME_PRECISE TimeValues,
    _In_ ULONG RequestedValues,
    _Out_ PULONG ReturnedValues)
{
    LARGE_INTEGER Counter;
    LARGE_INTEGER SystemTime;
    ULONG Returned = 0;

    if (ReturnedValues == NULL)
        return STATUS_INVALID_PARAMETER;
    *ReturnedValues = 0;
    if (RequestedValues == 0)
        return STATUS_SUCCESS;
    if (TimeValues == NULL)
        return STATUS_INVALID_PARAMETER;

    if (RequestedValues & 1)
    {
        Counter = KeQueryPerformanceCounter(NULL);
        TimeValues->PerformanceCounter = Counter.QuadPart;
        Returned |= 1;
    }

    /* ReactOS has no hypervisor host-performance-counter provider yet. */

    if (RequestedValues & 4)
    {
        KeQuerySystemTimePrecise(&SystemTime);
        TimeValues->SystemTime = SystemTime.QuadPart;
        Returned |= 4;
    }

    *ReturnedValues = Returned;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlConvertHostPerfCounterToPerfCounter(
    _In_ ULONGLONG HostPerformanceCounter,
    _In_ ULONGLONG MaximumError,
    _Out_ PULONGLONG PerformanceCounter)
{
    UNREFERENCED_PARAMETER(HostPerformanceCounter);
    UNREFERENCED_PARAMETER(MaximumError);

    if (PerformanceCounter == NULL)
        return STATUS_INVALID_PARAMETER;

    /* A conversion is impossible until the host-counter provider exists. */
    return STATUS_UNSUCCESSFUL;
}
