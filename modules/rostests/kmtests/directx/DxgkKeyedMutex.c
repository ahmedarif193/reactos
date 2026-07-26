/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Keyed mutex state machine
 *
 * A keyed mutex decides which process may write a shared surface next.  Both
 * failure modes are silent: an acquire that succeeds on the wrong key hands the
 * surface to the wrong partner, and a release accepted on an unowned mutex
 * hands it out while its writer is still drawing.  Neither shows up as an
 * error, only as corruption, so the transitions are pinned here.
 */

#include <kmt_test.h>
#include "keyedmutex_core.h"

static VOID TestInitialState(VOID)
{
    DXGK_KEYED_MUTEX_STATE State;

    DxgkKeyedMutexCoreInitialize(&State, 7);
    ok_eq_ulong((ULONG)State.CurrentKey, 7UL);
    ok_bool_false(State.Owned, "freshly created mutex is owned");
    ok_eq_ulong(State.WaiterCount, 0UL);
    ok_eq_ulong((ULONG)State.FenceValue, 0UL);

    /* The creator's key is the one the first acquirer must ask for. */
    ok_bool_true(DxgkKeyedMutexCoreCanAcquire(&State, 7), "initial key not acquirable");
    ok_bool_false(DxgkKeyedMutexCoreCanAcquire(&State, 0), "wrong key acquirable");
}

static VOID TestAcquireRequiresMatchingKey(VOID)
{
    DXGK_KEYED_MUTEX_STATE State;

    DxgkKeyedMutexCoreInitialize(&State, 3);

    /* The whole point of the key: a waiter for a different hand-off must not
     * take the surface just because it happens to be free. */
    ok_bool_false(DxgkKeyedMutexCoreAcquire(&State, 2), "acquired on the wrong key");
    ok_bool_false(State.Owned, "a refused acquire took ownership");
    ok_bool_false(DxgkKeyedMutexCoreAcquire(&State, 4), "acquired on the wrong key");
    ok_bool_false(State.Owned, "a refused acquire took ownership");

    ok_bool_true(DxgkKeyedMutexCoreAcquire(&State, 3), "matching key refused");
    ok_bool_true(State.Owned, "a successful acquire did not take ownership");
    /* The key is unchanged by an acquire; only a release republishes it. */
    ok_eq_ulong((ULONG)State.CurrentKey, 3UL);
}

static VOID TestAcquireIsExclusive(VOID)
{
    DXGK_KEYED_MUTEX_STATE State;

    DxgkKeyedMutexCoreInitialize(&State, 0);
    ok_bool_true(DxgkKeyedMutexCoreAcquire(&State, 0), "first acquire refused");

    /* Holding the right key is not enough while somebody else holds the mutex;
     * two acquirers admitted at once is exactly the corruption case. */
    ok_bool_false(DxgkKeyedMutexCoreCanAcquire(&State, 0), "owned mutex reported acquirable");
    ok_bool_false(DxgkKeyedMutexCoreAcquire(&State, 0), "second acquire admitted");
    ok_bool_true(State.Owned, "ownership lost on a refused second acquire");
}

static VOID TestReleaseRequiresOwnership(VOID)
{
    DXGK_KEYED_MUTEX_STATE State;

    DxgkKeyedMutexCoreInitialize(&State, 5);

    /* Releasing one nobody holds would publish a key -- and wake a waiter --
     * while the real writer is still drawing into the surface. */
    ok_bool_false(DxgkKeyedMutexCoreRelease(&State, 9, 1), "release accepted on an unowned mutex");
    ok_eq_ulong((ULONG)State.CurrentKey, 5UL);
    ok_eq_ulong((ULONG)State.FenceValue, 0UL);

    ok_bool_true(DxgkKeyedMutexCoreAcquire(&State, 5), "acquire refused");
    ok_bool_true(DxgkKeyedMutexCoreRelease(&State, 9, 42), "release refused by its owner");
    ok_bool_false(State.Owned, "release did not drop ownership");
    ok_eq_ulong((ULONG)State.CurrentKey, 9UL);
    ok_eq_ulong((ULONG)State.FenceValue, 42UL);

    /* A second release is no longer the owner's to make. */
    ok_bool_false(DxgkKeyedMutexCoreRelease(&State, 11, 43), "double release accepted");
    ok_eq_ulong((ULONG)State.CurrentKey, 9UL);
}

static VOID TestHandOffSequence(VOID)
{
    DXGK_KEYED_MUTEX_STATE State;
    ULONG Round;

    DxgkKeyedMutexCoreInitialize(&State, 0);

    /* The producer/consumer ping-pong the API exists for: each side releases to
     * the key the other is waiting on, and neither can cut in. */
    for (Round = 0; Round < 64; ++Round)
    {
        ok_bool_true(DxgkKeyedMutexCoreAcquire(&State, 0), "producer acquire refused");
        ok_bool_false(DxgkKeyedMutexCoreCanAcquire(&State, 1), "consumer admitted mid-write");
        ok_bool_true(DxgkKeyedMutexCoreRelease(&State, 1, Round + 1), "producer release refused");

        ok_bool_false(DxgkKeyedMutexCoreCanAcquire(&State, 0), "producer re-admitted out of turn");
        ok_bool_true(DxgkKeyedMutexCoreAcquire(&State, 1), "consumer acquire refused");
        ok_bool_true(DxgkKeyedMutexCoreRelease(&State, 0, Round + 1), "consumer release refused");
    }
    ok_eq_ulong((ULONG)State.CurrentKey, 0UL);
    ok_eq_ulong((ULONG)State.FenceValue, 64UL);
}

static VOID TestKeysAreFull64Bit(VOID)
{
    DXGK_KEYED_MUTEX_STATE State;
    CONST UINT64 High = 0x123456789ABCDEF0ULL;
    CONST UINT64 Near = 0x123456799ABCDEF0ULL;   /* differs only above bit 32 */

    DxgkKeyedMutexCoreInitialize(&State, High);

    /* A comparison truncated to 32 bits would admit Near here, handing the
     * surface to a partner that asked for a different key entirely. */
    ok_bool_false(DxgkKeyedMutexCoreCanAcquire(&State, Near), "key compared as 32-bit");
    ok_bool_true(DxgkKeyedMutexCoreCanAcquire(&State, High), "64-bit key not matched");

    ok_bool_true(DxgkKeyedMutexCoreAcquire(&State, High), "acquire refused");
    ok_bool_true(DxgkKeyedMutexCoreRelease(&State, MAXULONGLONG, MAXULONGLONG), "release refused");
    ok_bool_true(State.CurrentKey == MAXULONGLONG, "key truncated on release\n");
    ok_bool_true(State.FenceValue == MAXULONGLONG, "fence truncated on release\n");
    ok_bool_true(DxgkKeyedMutexCoreCanAcquire(&State, MAXULONGLONG), "max key not matched");
}

static VOID TestWaiterAccounting(VOID)
{
    DXGK_KEYED_MUTEX_STATE State;
    ULONG Index;

    DxgkKeyedMutexCoreInitialize(&State, 0);

    for (Index = 0; Index < 8; ++Index)
        DxgkKeyedMutexCoreAddWaiter(&State);
    ok_eq_ulong(State.WaiterCount, 8UL);
    for (Index = 0; Index < 8; ++Index)
        DxgkKeyedMutexCoreRemoveWaiter(&State);
    ok_eq_ulong(State.WaiterCount, 0UL);

    /* A stray removal must not wrap the count: a huge waiter count would make
     * teardown believe threads are parked that are not. */
    DxgkKeyedMutexCoreRemoveWaiter(&State);
    ok_eq_ulong(State.WaiterCount, 0UL);
}

static VOID TestPrivateDataSizeContract(VOID)
{
    /* Zero is always fine and needs no buffer. */
    ok_bool_true(DxgkKeyedMutexCorePrivateDataSizeValid(0, FALSE), "zero size refused");
    ok_bool_true(DxgkKeyedMutexCorePrivateDataSizeValid(0, TRUE), "zero size refused");

    /* A nonzero size with no buffer is the shape that would read from NULL. */
    ok_bool_false(DxgkKeyedMutexCorePrivateDataSizeValid(1, FALSE), "size without a buffer accepted");

    ok_bool_true(DxgkKeyedMutexCorePrivateDataSizeValid(1, TRUE), "minimal size refused");
    ok_bool_true(DxgkKeyedMutexCorePrivateDataSizeValid(DXGK_KEYED_MUTEX_MAX_PRIVATE_DATA, TRUE),
                 "maximum size refused");
    ok_bool_false(DxgkKeyedMutexCorePrivateDataSizeValid(DXGK_KEYED_MUTEX_MAX_PRIVATE_DATA + 1, TRUE),
                  "oversized private data accepted");
    ok_bool_false(DxgkKeyedMutexCorePrivateDataSizeValid(MAXULONG, TRUE),
                  "oversized private data accepted");
}

START_TEST(DxgkKeyedMutex)
{
    TestInitialState();
    TestAcquireRequiresMatchingKey();
    TestAcquireIsExclusive();
    TestReleaseRequiresOwnership();
    TestHandOffSequence();
    TestKeysAreFull64Bit();
    TestWaiterAccounting();
    TestPrivateDataSizeContract();
}

/* EOF */
