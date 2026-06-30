/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM 2.0 hardware scheduling / submission tests
 *              (CreateHwQueue / DestroyHwQueue / SubmitCommand[ToHwQueue])
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * WDDM 2.0 added GPU-side scheduling: with hardware scheduling enabled, work is
 * submitted to a per-engine hardware queue created on a hardware context, each
 * queue backed by a monitored progress fence. D3DKMTSubmitCommand submits to a
 * context's ring; D3DKMTSubmitCommandToHwQueue submits to a hardware queue.
 *
 * Reference: Microsoft "D3DKMTCreateHwQueue", "D3DKMTSubmitCommandToHwQueue",
 *            "GPU hardware scheduling".
 *
 * Building a real hardware queue needs a render-capable device, a virtual
 * context and driver-private data, so we validate the portable contract here:
 * NULL refused, bogus context fails.
 */

#include "precomp.h"

/* ---- NULL-argument contract ---- */
static void Test_CreateHwQueue_NullArg(void)
{
    LOADFN(PFND3DKMT_CREATEHWQUEUE, p, "D3DKMTCreateHwQueue");
    EXPECT_NULL_REJECTED(p, "D3DKMTCreateHwQueue");
}

static void Test_DestroyHwQueue_NullArg(void)
{
    LOADFN(PFND3DKMT_DESTROYHWQUEUE, p, "D3DKMTDestroyHwQueue");
    EXPECT_NULL_REJECTED(p, "D3DKMTDestroyHwQueue");
}

static void Test_SubmitCommand_NullArg(void)
{
    LOADFN(PFND3DKMT_SUBMITCOMMAND, p, "D3DKMTSubmitCommand");
    EXPECT_NULL_REJECTED(p, "D3DKMTSubmitCommand");
}

static void Test_SubmitCommandToHwQueue_NullArg(void)
{
    LOADFN(PFND3DKMT_SUBMITCOMMANDTOHWQUEUE, p, "D3DKMTSubmitCommandToHwQueue");
    EXPECT_NULL_REJECTED(p, "D3DKMTSubmitCommandToHwQueue");
}

/* ---- CreateHwQueue on a bogus hardware context must fail ---- */
static void Test_CreateHwQueue_BadHandle(void)
{
    D3DKMT_CREATEHWQUEUE chq;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_CREATEHWQUEUE, p, "D3DKMTCreateHwQueue");

    memset(&chq, 0, sizeof(chq));
    chq.hHwContext = (D3DKMT_HANDLE)0xDEAD4001;   /* never a valid context */

    Status = p(&chq);
    ok(!NT_SUCCESS(Status),
       "CreateHwQueue with a bogus context should fail, got 0x%08lX\n",
       (long)Status);
}

START_TEST(hwqueue)
{
    Test_CreateHwQueue_NullArg();
    Test_DestroyHwQueue_NullArg();
    Test_SubmitCommand_NullArg();
    Test_SubmitCommandToHwQueue_NullArg();
    Test_CreateHwQueue_BadHandle();
}
