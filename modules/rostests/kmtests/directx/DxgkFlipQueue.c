/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     The flip queue and vsync pacing
 *
 * A flip changes what the display scans out.  Flipping without owning the
 * VidPN source lets one client display over another's output.
 */

#include <kmt_test.h>
#include "display_core.h"

static VOID TestSourceOwnership(VOID)
{
    DXGK_FLIP_QUEUE Queue;

    DxgkFlipCoreInitialize(&Queue, 0);
    ok_bool_false(Queue.OwnsSource, "no ownership by default");

    /* Scanning out requires owning the source. */
    { NTSTATUS Observed = DxgkFlipCoreQueue(&Queue, 0x100, DxgkFlipIntervalImmediate); ok_eq_hex(Observed, STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE); }
    { ULONG Observed = DxgkFlipCoreQueuedCount(&Queue); ok_eq_ulong(Observed, 0UL); }

    DxgkFlipCoreSetSourceOwnership(&Queue, TRUE);
    { NTSTATUS Observed = DxgkFlipCoreQueue(&Queue, 0x100, DxgkFlipIntervalImmediate); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { ULONG Observed = DxgkFlipCoreQueuedCount(&Queue); ok_eq_ulong(Observed, 1UL); }

    /* Losing ownership stops further flips but does not discard queued ones. */
    DxgkFlipCoreSetSourceOwnership(&Queue, FALSE);
    { NTSTATUS Observed = DxgkFlipCoreQueue(&Queue, 0x200, DxgkFlipIntervalImmediate); ok_eq_hex(Observed, STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE); }
    { ULONG Observed = DxgkFlipCoreQueuedCount(&Queue); ok_eq_ulong(Observed, 1UL); }
}

static VOID TestImmediateFlip(VOID)
{
    DXGK_FLIP_QUEUE Queue;
    ULONGLONG Presented = 0;

    DxgkFlipCoreInitialize(&Queue, 0);
    DxgkFlipCoreSetSourceOwnership(&Queue, TRUE);

    ok_bool_false(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "an empty queue presents nothing");

    { NTSTATUS Observed = DxgkFlipCoreQueue(&Queue, 0x100, DxgkFlipIntervalImmediate); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "immediate flip presents");
    ok_eq_ulonglong(Presented, 0x100ULL);
    ok_eq_ulonglong(Queue.ScannedOutCookie, 0x100ULL);
    { ULONG Observed = DxgkFlipCoreQueuedCount(&Queue); ok_eq_ulong(Observed, 0UL); }
}

static VOID TestIntervalPacing(VOID)
{
    DXGK_FLIP_QUEUE Queue;
    ULONGLONG Presented = 0;

    DxgkFlipCoreInitialize(&Queue, 0);
    DxgkFlipCoreSetSourceOwnership(&Queue, TRUE);

    /* An interval of two must wait two vsyncs; presenting early is a tear. */
    { NTSTATUS Observed = DxgkFlipCoreQueue(&Queue, 0x300, DxgkFlipIntervalTwo); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "first vsync waits");
    ok_bool_false(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "second vsync waits");
    ok_bool_true(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "third vsync presents");
    ok_eq_ulonglong(Presented, 0x300ULL);

    { NTSTATUS Observed = DxgkFlipCoreQueue(&Queue, 0x400, (DXGK_FLIP_INTERVAL)99); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkFlipCoreQueue(&Queue, 0, DxgkFlipIntervalOne); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestQueueOrderAndDepth(VOID)
{
    DXGK_FLIP_QUEUE Queue;
    ULONGLONG Presented = 0;
    ULONG Queued;

    DxgkFlipCoreInitialize(&Queue, 0);
    DxgkFlipCoreSetSourceOwnership(&Queue, TRUE);

    for (Queued = 0; Queued < DXGK_FLIP_CORE_MAX_QUEUED; ++Queued)
    {
        if (!NT_SUCCESS(DxgkFlipCoreQueue(&Queue, 0x1000 + Queued, DxgkFlipIntervalImmediate)))
            break;
    }
    ok_eq_ulong(Queued, (ULONG)DXGK_FLIP_CORE_MAX_QUEUED);

    /* A full queue must refuse rather than overwrite a pending flip. */
    { NTSTATUS Observed = DxgkFlipCoreQueue(&Queue, 0x9999, DxgkFlipIntervalImmediate); ok_eq_hex(Observed, STATUS_GRAPHICS_TOO_MANY_REFERENCES); }

    /* Flips present in the order they were queued, or frames arrive out of
     * order on screen. */
    ok_bool_true(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "first");
    ok_eq_ulonglong(Presented, 0x1000ULL);
    ok_bool_true(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "second");
    ok_eq_ulonglong(Presented, 0x1001ULL);

    /* Space frees as flips retire, and the ring wraps rather than filling up. */
    { NTSTATUS Observed = DxgkFlipCoreQueue(&Queue, 0x2000, DxgkFlipIntervalImmediate); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "third");
    ok_eq_ulonglong(Presented, 0x1002ULL);
    ok_bool_true(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "fourth");
    ok_eq_ulonglong(Presented, 0x1003ULL);
    ok_bool_true(DxgkFlipCoreNotifyVSync(&Queue, &Presented), "wrapped entry");
    ok_eq_ulonglong(Presented, 0x2000ULL);
    { ULONG Observed = DxgkFlipCoreQueuedCount(&Queue); ok_eq_ulong(Observed, 0UL); }
}

START_TEST(DxgkFlipQueue)
{
    TestSourceOwnership();
    TestImmediateFlip();
    TestIntervalPacing();
    TestQueueOrderAndDepth();
}

/* EOF */
