/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hot-plug rebuild generation and worker-state transitions
 */

#ifndef _DXGK_HOTPLUG_WORK_CORE_H_
#define _DXGK_HOTPLUG_WORK_CORE_H_

#include <ntddk.h>

#define DXGK_HOTPLUG_MAX_TRANSIENT_RETRIES 6
#define DXGK_HOTPLUG_RETRY_BASE_MS 10
#define DXGK_HOTPLUG_RETRY_MAX_MS 100

/* The caller serializes generation, activation, and enumeration helpers with
 * one lock.  A successful activation owns rundown before the embedded work
 * item is queued; the retry-policy helpers are side-effect free. */
FORCEINLINE LONG64 DxgkHotPlugWorkCorePublishLocked(_Inout_ volatile LONG64 *Generation)
{
    return InterlockedIncrement64(Generation);
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreTryActivateLocked(_Inout_ volatile LONG *Active)
{
    if (*Active != 0)
        return FALSE;
    *Active = 1;
    return TRUE;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreCompleteLocked(_In_ volatile const LONG64 *Generation, _Inout_ volatile LONG *Active, _In_ LONG64 ObservedGeneration, _In_ BOOLEAN LifecycleOpen)
{
    ASSERT(*Active != 0);
    if (LifecycleOpen && *Generation != ObservedGeneration)
        return FALSE;
    *Active = 0;
    return TRUE;
}

FORCEINLINE LONG64 DxgkHotPlugWorkCoreBeginEnumerationEpochLocked(_Inout_ volatile LONG64 *Epoch, _Inout_ volatile LONG *Enumerated)
{
    LONG64 NextEpoch = InterlockedIncrement64(Epoch);

    if (NextEpoch == 0)
        NextEpoch = InterlockedIncrement64(Epoch);
    InterlockedExchange(Enumerated, 0);
    return NextEpoch;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCorePublishEnumerationLocked(_In_ volatile const LONG64 *Epoch, _Inout_ volatile LONG *Enumerated, _In_ LONG64 ExpectedEpoch)
{
    if (*Epoch != ExpectedEpoch || *Enumerated != 0)
        return FALSE;
    *Enumerated = 1;
    return TRUE;
}

FORCEINLINE NTSTATUS DxgkHotPlugWorkCoreValidateEnumerationLocked(_In_ volatile const LONG64 *Epoch, _In_ volatile const LONG *Enumerated, _In_ LONG64 ExpectedEpoch)
{
    if (*Epoch != ExpectedEpoch)
        return STATUS_RETRY;
    if (*Enumerated == 0)
        return STATUS_DEVICE_NOT_READY;
    return STATUS_SUCCESS;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreCanAcquireLevel3AfterRundown(_In_ volatile const LONG *Active)
{
    return *Active == 0;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreIsTransientStatus(_In_ NTSTATUS Status)
{
    return Status == STATUS_RETRY || Status == STATUS_INSUFFICIENT_RESOURCES || Status == STATUS_NO_MEMORY || Status == STATUS_DEVICE_NOT_READY || Status == STATUS_DEVICE_BUSY || Status == STATUS_IO_TIMEOUT;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreShouldRetry(_In_ NTSTATUS Status, _In_ ULONG RetryCount)
{
    return RetryCount < DXGK_HOTPLUG_MAX_TRANSIENT_RETRIES && DxgkHotPlugWorkCoreIsTransientStatus(Status);
}

FORCEINLINE ULONG DxgkHotPlugWorkCoreRetryDelayMs(_In_ ULONG RetryCount)
{
    ULONG DelayMs = DXGK_HOTPLUG_RETRY_BASE_MS;

    while (RetryCount-- != 0 && DelayMs < DXGK_HOTPLUG_RETRY_MAX_MS)
        DelayMs = min(DelayMs * 2, DXGK_HOTPLUG_RETRY_MAX_MS);
    return DelayMs;
}

#endif /* _DXGK_HOTPLUG_WORK_CORE_H_ */
