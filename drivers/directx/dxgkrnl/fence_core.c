/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU fence ordering, monitored fences and periodic fences
 */

#include "fence_core.h"

/* --- 32-bit submission fence identity -------------------------------- */

BOOLEAN
DxgkFenceCoreReached(
    _In_ ULONG CompletedFence,
    _In_ ULONG TargetFence)
{
    return ((LONG)(CompletedFence - TargetFence) >= 0);
}

LONG
DxgkFenceCoreDistance(
    _In_ ULONG Earlier,
    _In_ ULONG Later)
{
    return (LONG)(Later - Earlier);
}

BOOLEAN
DxgkFenceCoreAdvance(
    _Inout_ volatile LONG *Watermark,
    _In_ ULONG Reported)
{
    LONG Current;

    for (;;)
    {
        Current = InterlockedCompareExchange(Watermark, 0, 0);
        if (DxgkFenceCoreReached((ULONG)Current, Reported))
            return FALSE;
        if (InterlockedCompareExchange(Watermark, (LONG)Reported, Current) == Current)
            return TRUE;
    }
}

ULONG
DxgkFenceCoreAllocate(
    _Inout_ volatile LONG *NextFenceId)
{
    ULONG Fence;

    /* Zero means "no fence", so the sequence skips it on wrap. */
    do
    {
        Fence = (ULONG)InterlockedIncrement(NextFenceId);
    } while (Fence == 0);
    return Fence;
}

/* --- monitored fences ------------------------------------------------- */

NTSTATUS
DxgkMonitoredFenceCoreInitialize(
    _Out_ PDXGK_MONITORED_FENCE Fence,
    _In_ ULONGLONG InitialValue,
    _In_ ULONG Flags)
{
    RtlZeroMemory(Fence, sizeof(*Fence));
    if ((Flags & ~DXGK_MONITORED_FENCE_FLAG_VALID_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    /* A fence that can neither be signalled nor waited on carries no
     * information and is a caller error, not a degenerate-but-legal object. */
    if ((Flags & DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL) != 0 &&
        (Flags & DXGK_MONITORED_FENCE_FLAG_NO_WAIT) != 0)
        return STATUS_INVALID_PARAMETER;
    Fence->CurrentValue = InitialValue;
    Fence->LastSignalledValue = InitialValue;
    Fence->Flags = Flags;
    Fence->Initialized = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkMonitoredFenceCoreSignal(
    _Inout_ PDXGK_MONITORED_FENCE Fence,
    _In_ ULONGLONG Value)
{
    if (!Fence->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Fence->Flags & DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL) != 0)
        return STATUS_NOT_SUPPORTED;
    /*
     * Monitored fence values are monotonic by contract.  Accepting a regress
     * would un-satisfy waits that already resolved, so it is refused and the
     * published value is left alone.
     */
    if (Value < Fence->CurrentValue)
        return STATUS_INVALID_PARAMETER;
    Fence->CurrentValue = Value;
    Fence->LastSignalledValue = Value;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkMonitoredFenceCoreIsSatisfied(
    _In_ const DXGK_MONITORED_FENCE *Fence,
    _In_ ULONGLONG WaitValue)
{
    if (!Fence->Initialized)
        return FALSE;
    return Fence->CurrentValue >= WaitValue;
}

NTSTATUS
DxgkMonitoredFenceCoreCanWait(
    _In_ const DXGK_MONITORED_FENCE *Fence)
{
    if (!Fence->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Fence->Flags & DXGK_MONITORED_FENCE_FLAG_NO_WAIT) != 0)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkMonitoredFenceCoreCanSignal(
    _In_ const DXGK_MONITORED_FENCE *Fence)
{
    if (!Fence->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Fence->Flags & DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL) != 0)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

/* --- periodic monitored fences --------------------------------------- */

NTSTATUS
DxgkPeriodicFenceCoreBind(
    _Out_ PDXGK_PERIODIC_FENCE Fence,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG PeriodInVSyncs,
    _In_ ULONGLONG InitialValue)
{
    RtlZeroMemory(Fence, sizeof(*Fence));
    /* A zero period would advance the fence on nothing at all. */
    if (PeriodInVSyncs == 0)
        return STATUS_INVALID_PARAMETER;
    Fence->VidPnTargetId = VidPnTargetId;
    Fence->PeriodInVSyncs = PeriodInVSyncs;
    Fence->CurrentValue = InitialValue;
    Fence->Bound = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkPeriodicFenceCoreNotifyVSync(
    _Inout_ PDXGK_PERIODIC_FENCE Fence,
    _In_ ULONG VidPnTargetId)
{
    if (!Fence->Bound)
        return FALSE;
    /* A periodic fence tracks one display target; another target's vsync is
     * not its clock. */
    if (Fence->VidPnTargetId != VidPnTargetId)
        return FALSE;
    Fence->VSyncsSinceAdvance++;
    if (Fence->VSyncsSinceAdvance < Fence->PeriodInVSyncs)
        return FALSE;
    Fence->VSyncsSinceAdvance = 0;
    Fence->CurrentValue++;
    return TRUE;
}

/* EOF */
