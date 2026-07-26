/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Offer/reclaim and discarded content
 *
 * Offered content is memory the app says it can regenerate.  Reclaim must tell
 * the truth about whether it survived: a caller told its content is intact
 * when it was discarded renders from uninitialised memory.
 */

#include <kmt_test.h>
#include "residency_core.h"

static VOID TestOfferAndReclaim(VOID)
{
    DXGK_OFFER_STATE State;
    BOOLEAN Discarded = TRUE;

    DxgkOfferCoreInitialize(&State);
    ok_bool_false(State.Offered, "not offered");

    { NTSTATUS Observed = DxgkOfferCoreOffer(&State, DxgkOfferPriorityNormal); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(State.Offered, "offered");

    /* Offering twice loses track of which offer the content belongs to. */
    { NTSTATUS Observed = DxgkOfferCoreOffer(&State, DxgkOfferPriorityLow); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }

    { NTSTATUS Observed = DxgkOfferCoreReclaim(&State, &Discarded); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(Discarded, "content survived");
    ok_bool_false(State.Offered, "no longer offered");

    /* Reclaiming what was never offered is a caller error. */
    { NTSTATUS Observed = DxgkOfferCoreReclaim(&State, &Discarded); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }

    { NTSTATUS Observed = DxgkOfferCoreOffer(&State, DxgkOfferPriorityNone); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkOfferCoreOffer(&State, (DXGK_OFFER_PRIORITY)99); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestDiscard(VOID)
{
    DXGK_OFFER_STATE State;
    BOOLEAN Discarded = FALSE;

    DxgkOfferCoreInitialize(&State);

    /* Only offered content may be discarded; anything else is still in use. */
    ok_bool_false(DxgkOfferCoreDiscard(&State), "cannot discard unoffered content");

    { NTSTATUS Observed = DxgkOfferCoreOffer(&State, DxgkOfferPriorityLow); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkOfferCoreDiscard(&State), "offered content may be discarded");

    /*
     * The reclaim must report the discard.  A caller told its content is
     * intact when it is not will render from uninitialised memory.
     */
    { NTSTATUS Observed = DxgkOfferCoreReclaim(&State, &Discarded); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(Discarded, "reclaim reports the discard");

    /* The flag is consumed, so a later reclaim of new content is honest. */
    { NTSTATUS Observed = DxgkOfferCoreOffer(&State, DxgkOfferPriorityLow); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Discarded = TRUE;
    { NTSTATUS Observed = DxgkOfferCoreReclaim(&State, &Discarded); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(Discarded, "fresh content is not reported as discarded");
}

static VOID TestVictimRanking(VOID)
{
    DXGK_OFFER_STATE InUse;
    DXGK_OFFER_STATE OfferedLow;
    DXGK_OFFER_STATE OfferedHigh;

    DxgkOfferCoreInitialize(&InUse);
    DxgkOfferCoreInitialize(&OfferedLow);
    DxgkOfferCoreInitialize(&OfferedHigh);
    { NTSTATUS Observed = DxgkOfferCoreOffer(&OfferedLow, DxgkOfferPriorityLow); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkOfferCoreOffer(&OfferedHigh, DxgkOfferPriorityHigh); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* Offered content is evicted before anything still in use, whatever the
     * priorities say. */
    ok_bool_true(DxgkOfferCoreOutranksAsVictim(&OfferedHigh, &InUse), "offered beats in-use");
    ok_bool_false(DxgkOfferCoreOutranksAsVictim(&InUse, &OfferedLow), "in-use never beats offered");

    /* Among offered content, the lowest offer priority goes first. */
    ok_bool_true(DxgkOfferCoreOutranksAsVictim(&OfferedLow, &OfferedHigh), "low beats high");
    ok_bool_false(DxgkOfferCoreOutranksAsVictim(&OfferedHigh, &OfferedLow), "high does not beat low");
    ok_bool_false(DxgkOfferCoreOutranksAsVictim(&OfferedLow, &OfferedLow), "equal does not outrank");
    ok_bool_false(DxgkOfferCoreOutranksAsVictim(&InUse, &InUse), "two in-use, neither outranks");
}

START_TEST(DxgkOfferReclaim)
{
    TestOfferAndReclaim();
    TestDiscard();
    TestVictimRanking();
}

/* EOF */
