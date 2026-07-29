/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     WDDM 2.2 periodic monitored-fence notification lifetime
 *
 * A periodic notification is not interrupt-visible until the KMD create DDI
 * succeeds and returns a handle.  Its target and stable NotificationID must
 * both match interrupt 14, and its KMD handle must be destroyed exactly once.
 */

#include <kmt_test.h>
#include "fence_core.h"

static VOID
TestCreatePublication(VOID)
{
    DXGK_PERIODIC_NOTIFICATION_CORE Notification;
    PVOID KmdHandle = (PVOID)(ULONG_PTR)0x1234;

    DxgkPeriodicNotificationCoreInitialize(&Notification);
    ok_eq_long(Notification.State, DxgkPeriodicNotificationNone);
    ok_eq_hex(DxgkPeriodicNotificationCoreBeginCreate(
                  &Notification, 2, 0),
              STATUS_INVALID_PARAMETER);
    ok_eq_hex(DxgkPeriodicNotificationCoreBeginCreate(
                  &Notification, 2, 41),
              STATUS_SUCCESS);
    ok_eq_long(Notification.State, DxgkPeriodicNotificationCreating);

    /* A reserved ID is not yet visible to interrupt 14. */
    ok_bool_false(DxgkPeriodicNotificationCoreMatches(
                      &Notification, 2, 41),
                  "creating notification was published");
    ok_eq_hex(DxgkPeriodicNotificationCoreBeginCreate(
                  &Notification, 2, 42),
              STATUS_INVALID_DEVICE_STATE);
    ok_eq_hex(DxgkPeriodicNotificationCoreCompleteCreate(
                  &Notification, NULL),
              STATUS_INVALID_PARAMETER);
    ok_bool_false(DxgkPeriodicNotificationCoreMatches(
                      &Notification, 2, 41),
                  "NULL KMD handle was published");

    ok_eq_hex(DxgkPeriodicNotificationCoreCompleteCreate(
                  &Notification, KmdHandle),
              STATUS_SUCCESS);
    ok_eq_long(Notification.State, DxgkPeriodicNotificationActive);
    ok_bool_true(DxgkPeriodicNotificationCoreMatches(
                     &Notification, 2, 41),
                 "active notification did not match");
    ok_bool_false(DxgkPeriodicNotificationCoreMatches(
                      &Notification, 3, 41),
                  "foreign target matched");
    ok_bool_false(DxgkPeriodicNotificationCoreMatches(
                      &Notification, 2, 42),
                  "foreign notification ID matched");
}

static VOID
TestRollbackAndExactDestroy(VOID)
{
    DXGK_PERIODIC_NOTIFICATION_CORE Notification;
    PVOID DestroyHandle;
    PVOID KmdHandle = (PVOID)(ULONG_PTR)0x5678;

    DxgkPeriodicNotificationCoreInitialize(&Notification);
    ok_eq_hex(DxgkPeriodicNotificationCoreBeginCreate(
                  &Notification, 7, 99),
              STATUS_SUCCESS);
    ok_bool_true(DxgkPeriodicNotificationCoreCancelCreate(
                     &Notification),
                 "create rollback lost");
    ok_eq_long(Notification.State, DxgkPeriodicNotificationNone);
    ok_bool_false(DxgkPeriodicNotificationCoreCancelCreate(
                      &Notification),
                  "create rolled back twice");
    ok_eq_hex(DxgkPeriodicNotificationCoreCompleteCreate(
                  &Notification, KmdHandle),
              STATUS_INVALID_DEVICE_STATE);

    ok_eq_hex(DxgkPeriodicNotificationCoreBeginCreate(
                  &Notification, 7, 100),
              STATUS_SUCCESS);
    ok_eq_hex(DxgkPeriodicNotificationCoreCompleteCreate(
                  &Notification, KmdHandle),
              STATUS_SUCCESS);

    DestroyHandle = NULL;
    ok_bool_true(DxgkPeriodicNotificationCoreClaimDestroy(
                     &Notification, &DestroyHandle),
                 "first destroy did not claim the KMD handle");
    ok(DestroyHandle == KmdHandle,
       "destroy returned %p, expected %p\n", DestroyHandle, KmdHandle);
    ok_eq_long(Notification.State, DxgkPeriodicNotificationDestroyed);
    ok_bool_false(DxgkPeriodicNotificationCoreMatches(
                      &Notification, 7, 100),
                  "destroyed notification remained published");

    DestroyHandle = (PVOID)(ULONG_PTR)0xDEAD;
    ok_bool_false(DxgkPeriodicNotificationCoreClaimDestroy(
                      &Notification, &DestroyHandle),
                  "second destroy claimed the handle again");
    ok(DestroyHandle == NULL,
       "losing destroy returned stale handle %p\n", DestroyHandle);
}

static VOID
TestInterruptHandoff(VOID)
{
    DXGK_PERIODIC_INTERRUPT_CORE Core;
    DXGK_PERIODIC_INTERRUPT_CORE_ENTRY Entry;
    BOOLEAN QueueDpc;

    DxgkPeriodicInterruptCoreInitialize(&Core);
    QueueDpc = TRUE;
    ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                  &Core, 2, 41, 1, &QueueDpc),
              STATUS_DEVICE_NOT_READY);
    ok_bool_false(QueueDpc, "disabled handoff queued a DPC");

    DxgkPeriodicInterruptCoreEnableLocked(&Core);
    QueueDpc = FALSE;
    ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                  &Core, 2, 41, 1, &QueueDpc),
              STATUS_SUCCESS);
    ok_bool_true(QueueDpc, "first pulse did not queue the drain");

    QueueDpc = TRUE;
    ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                  &Core, 2, 41, 2, &QueueDpc),
              STATUS_SUCCESS);
    ok_bool_false(QueueDpc, "coalesced pulse queued a duplicate drain");

    QueueDpc = TRUE;
    ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                  &Core, 7, 99, 4, &QueueDpc),
              STATUS_SUCCESS);
    ok_bool_false(QueueDpc, "second ID queued a duplicate drain");

    ok_bool_true(DxgkPeriodicInterruptCoreDequeueLocked(
                     &Core, &Entry),
                 "first pending ID was lost");
    ok_eq_ulong(Entry.VidPnTargetId, 2);
    ok_eq_ulong(Entry.NotificationId, 41);
    ok_eq_ulonglong(Entry.PendingCount, 3);
    ok_bool_true(DxgkPeriodicInterruptCoreDequeueLocked(
                     &Core, &Entry),
                 "second pending ID was lost");
    ok_eq_ulong(Entry.VidPnTargetId, 7);
    ok_eq_ulong(Entry.NotificationId, 99);
    ok_eq_ulonglong(Entry.PendingCount, 4);
    ok_bool_false(DxgkPeriodicInterruptCoreDequeueLocked(
                      &Core, &Entry),
                  "empty table produced an entry");
    ok_bool_false(Core.DpcActive, "empty drain stayed active");

    /*
     * The empty transition is the ISR/DPC exit handshake.  A later pulse must
     * observe the inactive drain and request a fresh DPC.
     */
    QueueDpc = FALSE;
    ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                  &Core, 2, 41, 1, &QueueDpc),
              STATUS_SUCCESS);
    ok_bool_true(QueueDpc, "post-empty pulse could be stranded");
}

static VOID
TestInterruptOverflowIsSticky(VOID)
{
    DXGK_PERIODIC_INTERRUPT_CORE Core;
    DXGK_PERIODIC_INTERRUPT_CORE_ENTRY Entry;
    BOOLEAN QueueDpc;
    ULONG Index;

    DxgkPeriodicInterruptCoreInitialize(&Core);
    DxgkPeriodicInterruptCoreEnableLocked(&Core);
    for (Index = 0;
         Index < DXGK_PERIODIC_INTERRUPT_CORE_CAPACITY;
         ++Index)
    {
        QueueDpc = FALSE;
        ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                      &Core, Index, Index + 1, 1, &QueueDpc),
                  STATUS_SUCCESS);
    }
    QueueDpc = FALSE;
    ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                  &Core, 100, 1000, 1, &QueueDpc),
              STATUS_BUFFER_OVERFLOW);
    ok_eq_long(Core.State, DxgkPeriodicInterruptOverflowed);
    ok_eq_ulonglong(Core.OverflowCount, 1);
    ok_bool_false(DxgkPeriodicInterruptCoreDequeueLocked(
                      &Core, &Entry),
                  "overflow published an incomplete subset");

    QueueDpc = FALSE;
    ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                  &Core, 2, 41, 1, &QueueDpc),
              STATUS_BUFFER_OVERFLOW);
    DxgkPeriodicInterruptCoreDisableLocked(&Core);
    ok_eq_long(Core.State, DxgkPeriodicInterruptDisabled);

    DxgkPeriodicInterruptCoreEnableLocked(&Core);
    QueueDpc = FALSE;
    ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                  &Core, 2, 41, (ULONGLONG)-1, &QueueDpc),
              STATUS_SUCCESS);
    QueueDpc = FALSE;
    ok_eq_hex(DxgkPeriodicInterruptCoreEnqueueLocked(
                  &Core, 2, 41, 1, &QueueDpc),
              STATUS_INTEGER_OVERFLOW);
    ok_eq_long(Core.State, DxgkPeriodicInterruptOverflowed);
    ok_eq_ulonglong(Core.OverflowCount, 1);
}

START_TEST(DxgkPeriodicFence)
{
    TestCreatePublication();
    TestRollbackAndExactDestroy();
    TestInterruptHandoff();
    TestInterruptOverflowIsSticky();
}

/* EOF */
