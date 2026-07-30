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

/*
 * WDDM 2.2 regular monitored-fence interrupt handoff.  The KMD reports an
 * engine identity, and the ISR coalesces repeated reports into one node bit
 * for the DPC to consume.  This runtime implements one engine per node.
 */
BOOLEAN
DxgkMonitoredInterruptCoreSupported(
    _In_ ULONG ConfiguredWddmLevel,
    _In_ ULONG RuntimeWddmLevel,
    _In_ ULONG NodeCount);

NTSTATUS
DxgkMonitoredInterruptCoreEnqueue(
    _Inout_ volatile LONG *PendingNodes,
    _In_ ULONG NodeCount,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG EngineOrdinal);

ULONG
DxgkMonitoredInterruptCoreDrain(
    _Inout_ volatile LONG *PendingNodes);

BOOLEAN
DxgkMonitoredInterruptCoreAffinityMatches(
    _In_ ULONG EngineAffinity,
    _In_ ULONG PhysicalAdapterIndex);

/* --- periodic monitored-fence notification lifetime ----------------- */

typedef enum _DXGK_PERIODIC_NOTIFICATION_STATE
{
    DxgkPeriodicNotificationNone = 0,
    DxgkPeriodicNotificationCreating,
    DxgkPeriodicNotificationActive,
    DxgkPeriodicNotificationDestroyed
} DXGK_PERIODIC_NOTIFICATION_STATE;

/*
 * The WDDM 2.2 periodic-fence DDI is a two-phase publication:
 *
 *   1. dxgkrnl reserves a stable, nonzero NotificationID and calls
 *      DxgkDdiCreatePeriodicFrameNotification.
 *   2. Only a successful callback that returns a non-NULL hNotification may
 *      become visible to DXGK_INTERRUPT_PERIODIC_MONITORED_FENCE_SIGNALED.
 *
 * The owner serializes these helpers with its notification-registry lock.
 */
typedef struct _DXGK_PERIODIC_NOTIFICATION_CORE
{
    volatile LONG State;
    ULONG VidPnTargetId;
    ULONG NotificationId;
    PVOID NotificationHandle;
} DXGK_PERIODIC_NOTIFICATION_CORE, *PDXGK_PERIODIC_NOTIFICATION_CORE;

VOID
DxgkPeriodicNotificationCoreInitialize(
    _Out_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification);

NTSTATUS
DxgkPeriodicNotificationCoreBeginCreate(
    _Inout_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG NotificationId);

NTSTATUS
DxgkPeriodicNotificationCoreCompleteCreate(
    _Inout_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification,
    _In_ PVOID NotificationHandle);

BOOLEAN
DxgkPeriodicNotificationCoreCancelCreate(
    _Inout_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification);

BOOLEAN
DxgkPeriodicNotificationCoreMatches(
    _In_ const DXGK_PERIODIC_NOTIFICATION_CORE *Notification,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG NotificationId);

BOOLEAN
DxgkPeriodicNotificationCoreClaimDestroy(
    _Inout_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification,
    _Out_ PVOID *NotificationHandle);

/* --- periodic interrupt ISR-to-DPC handoff ---------------------------- */

/*
 * The sync registry enforces the same per-adapter live-notification bound.
 * Consequently a pending-count slot exists for every valid distinct
 * NotificationID, while repeated pulses consume only the slot's 64-bit count.
 */
#define DXGK_PERIODIC_INTERRUPT_CORE_CAPACITY 64

typedef enum _DXGK_PERIODIC_INTERRUPT_CORE_STATE
{
    DxgkPeriodicInterruptDisabled = 0,
    DxgkPeriodicInterruptAccepting,
    DxgkPeriodicInterruptOverflowed
} DXGK_PERIODIC_INTERRUPT_CORE_STATE;

typedef struct _DXGK_PERIODIC_INTERRUPT_CORE_ENTRY
{
    ULONG VidPnTargetId;
    ULONG NotificationId;
    ULONGLONG PendingCount;
    BOOLEAN InUse;
} DXGK_PERIODIC_INTERRUPT_CORE_ENTRY,
 *PDXGK_PERIODIC_INTERRUPT_CORE_ENTRY;

typedef struct _DXGK_PERIODIC_INTERRUPT_CORE
{
    volatile LONG State;
    BOOLEAN DpcActive;
    ULONGLONG OverflowCount;
    DXGK_PERIODIC_INTERRUPT_CORE_ENTRY
        Entries[DXGK_PERIODIC_INTERRUPT_CORE_CAPACITY];
} DXGK_PERIODIC_INTERRUPT_CORE, *PDXGK_PERIODIC_INTERRUPT_CORE;

/*
 * Callers serialize every helper below with the adapter interrupt lock.  The
 * ISR holds it at DIRQL; the DPC must raise to the interrupt synchronize IRQL
 * before taking the same lock.
 */
VOID
DxgkPeriodicInterruptCoreInitialize(
    _Out_ PDXGK_PERIODIC_INTERRUPT_CORE Core);

VOID
DxgkPeriodicInterruptCoreEnableLocked(
    _Inout_ PDXGK_PERIODIC_INTERRUPT_CORE Core);

VOID
DxgkPeriodicInterruptCoreDisableLocked(
    _Inout_ PDXGK_PERIODIC_INTERRUPT_CORE Core);

NTSTATUS
DxgkPeriodicInterruptCoreEnqueueLocked(
    _Inout_ PDXGK_PERIODIC_INTERRUPT_CORE Core,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG NotificationId,
    _In_ ULONGLONG NotificationCount,
    _Out_ PBOOLEAN QueueDpc);

BOOLEAN
DxgkPeriodicInterruptCoreDequeueLocked(
    _Inout_ PDXGK_PERIODIC_INTERRUPT_CORE Core,
    _Out_ PDXGK_PERIODIC_INTERRUPT_CORE_ENTRY Entry);

#endif /* _DXGK_FENCE_CORE_H_ */
