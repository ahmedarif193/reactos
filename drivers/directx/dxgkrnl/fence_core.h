/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU fence ordering, monitored fences and periodic fences
 *
 * Fence identity is how the whole stack decides what has executed.  A fence
 * comparison that gets wraparound wrong retires work the GPU never ran, so
 * every comparison here is signed-distance based and every publish is
 * monotonic.  No dxgkrnl or miniport types, so it can be exercised alone.
 */

#ifndef _DXGK_FENCE_CORE_H_
#define _DXGK_FENCE_CORE_H_

#include <ntddk.h>

/* --- 32-bit submission fence identity -------------------------------- */

/* TRUE if Completed has reached Target, wraparound-safe.  Valid while live
 * fences stay within 2^31 of each other, which the submission fence allocator
 * guarantees by refusing to lap an outstanding packet. */
BOOLEAN DxgkFenceCoreReached(_In_ ULONG CompletedFence, _In_ ULONG TargetFence);
/* Signed distance Later - Earlier; negative means Later precedes Earlier. */
LONG DxgkFenceCoreDistance(_In_ ULONG Earlier, _In_ ULONG Later);
/* Monotonic publish: never moves a watermark backwards.  Returns TRUE if the
 * watermark advanced. */
BOOLEAN DxgkFenceCoreAdvance(_Inout_ volatile LONG *Watermark, _In_ ULONG Reported);
/* Allocates the next nonzero fence identity; zero is reserved for "none". */
ULONG DxgkFenceCoreAllocate(_Inout_ volatile LONG *NextFenceId);

/* --- monitored fences (64-bit, GPU-visible) --------------------------- */

#define DXGK_MONITORED_FENCE_FLAG_TOP_OF_PIPELINE  0x00000001UL
#define DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL        0x00000002UL
#define DXGK_MONITORED_FENCE_FLAG_NO_WAIT          0x00000004UL
#define DXGK_MONITORED_FENCE_FLAG_SHARED           0x00000008UL
#define DXGK_MONITORED_FENCE_FLAG_VALID_MASK       0x0000000FUL

typedef struct _DXGK_MONITORED_FENCE
{
    ULONGLONG CurrentValue;
    ULONGLONG LastSignalledValue;
    ULONG     Flags;
    BOOLEAN   Initialized;
} DXGK_MONITORED_FENCE, *PDXGK_MONITORED_FENCE;

NTSTATUS DxgkMonitoredFenceCoreInitialize(_Out_ PDXGK_MONITORED_FENCE Fence, _In_ ULONGLONG InitialValue, _In_ ULONG Flags);
/* A monitored fence value only ever moves forward; a regressing signal is a
 * driver bug and is refused rather than applied. */
NTSTATUS DxgkMonitoredFenceCoreSignal(_Inout_ PDXGK_MONITORED_FENCE Fence, _In_ ULONGLONG Value);
BOOLEAN DxgkMonitoredFenceCoreIsSatisfied(_In_ const DXGK_MONITORED_FENCE *Fence, _In_ ULONGLONG WaitValue);
NTSTATUS DxgkMonitoredFenceCoreCanWait(_In_ const DXGK_MONITORED_FENCE *Fence);
NTSTATUS DxgkMonitoredFenceCoreCanSignal(_In_ const DXGK_MONITORED_FENCE *Fence);

/* --- periodic monitored fences --------------------------------------- */

typedef struct _DXGK_PERIODIC_FENCE
{
    ULONGLONG CurrentValue;
    ULONG     VidPnTargetId;
    ULONG     PeriodInVSyncs;
    ULONG     VSyncsSinceAdvance;
    BOOLEAN   Bound;
} DXGK_PERIODIC_FENCE, *PDXGK_PERIODIC_FENCE;

NTSTATUS DxgkPeriodicFenceCoreBind(_Out_ PDXGK_PERIODIC_FENCE Fence, _In_ ULONG VidPnTargetId, _In_ ULONG PeriodInVSyncs, _In_ ULONGLONG InitialValue);
/* Reports one vsync on a target; returns TRUE if this vsync advanced the
 * fence.  A vsync for another target must not advance it. */
BOOLEAN DxgkPeriodicFenceCoreNotifyVSync(_Inout_ PDXGK_PERIODIC_FENCE Fence, _In_ ULONG VidPnTargetId);

#endif /* _DXGK_FENCE_CORE_H_ */
