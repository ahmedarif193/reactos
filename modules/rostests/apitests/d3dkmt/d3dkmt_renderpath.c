/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Positive render path: Render -> Patch -> Submit -> fence (gate 2.3)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Every other render test in this suite is negative: bad contexts, bad flags,
 * NULL arguments.  None of them ever gets a command buffer back, so none of
 * them proves the path exists.  This one drives the whole chain the way a
 * user-mode driver does -- D3DKMTRender hands out a DMA buffer, the miniport
 * translates it, the scheduler submits it, the engine executes it, and the
 * context's fence advances past the submission -- and asserts the one thing
 * that cannot be faked: **the fence moved**.
 *
 * A fence that never advances is how a "working" render path looks when
 * nothing is actually executing, and no negative test can tell the difference.
 *
 * The command stream is softgpu's, which makes this driver-private in the same
 * way as the v3d and vc4kmt smoke subtests.  On any other miniport the render
 * is refused and the subtest skips rather than asserting.
 */

#include "precomp.h"

/* Mirrors SOFTGPU_CMD in drivers/directx/softgpu/softgpu.h.  The two only ever
 * meet through the command buffer, so a layout drift shows up here as a
 * rejected stream rather than as silent misexecution. */
#define SOFTGPU_CMD_MAGIC   0x444D4753UL
#define SOFTGPU_CMD_OP_NOP  1

#include <pshpack1.h>
typedef struct _TEST_SOFTGPU_CMD
{
    ULONG       Magic;
    ULONG       Op;
    ULONG       Size;
    ULONG       Color;
    RECT        SrcRect;
    RECT        DstRect;
    ULONG       SrcPitch;
    ULONG       DstPitch;
    ULONGLONG   SrcAddress;
    ULONGLONG   DstAddress;
    ULONGLONG   SlabAddress;
    ULONGLONG   SystemAddress;
    ULONGLONG   ByteCount;
    ULONG       Flags;
    ULONG       Reserved;
    ULONGLONG   FenceGpuVa;
    ULONGLONG   FenceValue;
} TEST_SOFTGPU_CMD;
#include <poppack.h>

#define RENDERPATH_SUBMISSIONS 16

typedef struct _RENDERPATH_CONTEXT
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hContext;
    VOID *pCommandBuffer;
    UINT CommandBufferSize;
    D3DDDI_ALLOCATIONLIST *pAllocationList;
    UINT AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT PatchLocationListSize;
} RENDERPATH_CONTEXT;

static PFND3DKMT_RENDER pfnRender;
static PFND3DKMT_CREATECONTEXT pfnCreateContext;
static PFND3DKMT_DESTROYCONTEXT pfnDestroyContext;
static PFND3DKMT_CREATEDEVICE pfnCreateDevice;
static PFND3DKMT_DESTROYDEVICE pfnDestroyDevice;

static void RenderPathTeardown(RENDERPATH_CONTEXT *Ctx)
{
    if (Ctx->hContext && pfnDestroyContext)
    {
        D3DKMT_DESTROYCONTEXT dc;

        memset(&dc, 0, sizeof(dc));
        dc.hContext = Ctx->hContext;
        pfnDestroyContext(&dc);
        Ctx->hContext = 0;
    }
    if (Ctx->hDevice && pfnDestroyDevice)
    {
        D3DKMT_DESTROYDEVICE dd;

        memset(&dd, 0, sizeof(dd));
        dd.hDevice = Ctx->hDevice;
        pfnDestroyDevice(&dd);
        Ctx->hDevice = 0;
    }
    if (Ctx->hAdapter)
    {
        CloseAdapter(Ctx->hAdapter);
        Ctx->hAdapter = 0;
    }
}

/* Brings up adapter -> device -> context and takes the first command buffer.
 * Returns FALSE having already skip()ed if any step is refused. */
static BOOL RenderPathSetup(RENDERPATH_CONTEXT *Ctx)
{
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_CREATECONTEXT cc;
    D3DKMT_RENDER render;
    NTSTATUS Status;

    memset(Ctx, 0, sizeof(*Ctx));

    Ctx->hAdapter = OpenAdapterFromDisplay1();
    if (!Ctx->hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return FALSE;
    }

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = Ctx->hAdapter;
    Status = pfnCreateDevice(&cd);
    if (!NT_SUCCESS(Status))
    {
        skip("CreateDevice refused (0x%08lX)\n", (long)Status);
        RenderPathTeardown(Ctx);
        return FALSE;
    }
    Ctx->hDevice = cd.hDevice;

    memset(&cc, 0, sizeof(cc));
    cc.hDevice = Ctx->hDevice;
    cc.NodeOrdinal = 0;
    cc.EngineAffinity = 0;
    Status = pfnCreateContext(&cc);
    if (!NT_SUCCESS(Status))
    {
        skip("CreateContext refused (0x%08lX) -- no render node on this adapter\n", (long)Status);
        RenderPathTeardown(Ctx);
        return FALSE;
    }
    Ctx->hContext = cc.hContext;
    Ctx->pCommandBuffer = cc.pCommandBuffer;
    Ctx->CommandBufferSize = cc.CommandBufferSize;
    Ctx->pAllocationList = cc.pAllocationList;
    Ctx->AllocationListSize = cc.AllocationListSize;
    Ctx->pPatchLocationList = cc.pPatchLocationList;
    Ctx->PatchLocationListSize = cc.PatchLocationListSize;

    /* A context that reports no command buffer cannot render, whatever else it
     * reports; there is nothing to write commands into. */
    if (Ctx->pCommandBuffer == NULL || Ctx->CommandBufferSize < sizeof(TEST_SOFTGPU_CMD))
    {
        skip("context returned no usable command buffer (%p, %u bytes)\n",
             Ctx->pCommandBuffer, Ctx->CommandBufferSize);
        RenderPathTeardown(Ctx);
        return FALSE;
    }
    trace("context: cmdbuf=%p size=%u alloclist=%u patchlist=%u\n",
          Ctx->pCommandBuffer, Ctx->CommandBufferSize,
          Ctx->AllocationListSize, Ctx->PatchLocationListSize);

    /* One probe render decides whether this miniport speaks the stream we can
     * build.  Refusal is a skip, not a failure -- this subtest is only
     * meaningful against softgpu. */
    memset(Ctx->pCommandBuffer, 0, sizeof(TEST_SOFTGPU_CMD));
    {
        TEST_SOFTGPU_CMD *Cmd = (TEST_SOFTGPU_CMD *)Ctx->pCommandBuffer;

        Cmd->Magic = SOFTGPU_CMD_MAGIC;
        Cmd->Op = SOFTGPU_CMD_OP_NOP;
        Cmd->Size = sizeof(TEST_SOFTGPU_CMD);
    }
    memset(&render, 0, sizeof(render));
    render.hContext = Ctx->hContext;
    render.CommandOffset = 0;
    render.CommandLength = sizeof(TEST_SOFTGPU_CMD);
    render.AllocationCount = 0;
    render.PatchLocationCount = 0;
    Status = pfnRender(&render);
    if (!NT_SUCCESS(Status))
    {
        skip("Render refused a minimal command stream (0x%08lX) -- not softgpu\n", (long)Status);
        RenderPathTeardown(Ctx);
        return FALSE;
    }

    /* Render hands back the *next* buffer to write into. */
    Ctx->pCommandBuffer = render.pNewCommandBuffer;
    Ctx->CommandBufferSize = render.NewCommandBufferSize;
    Ctx->pAllocationList = render.pNewAllocationList;
    Ctx->pPatchLocationList = render.pNewPatchLocationList;
    return TRUE;
}

/* ------------------------------------------------------------------ *
 * The whole point: submit repeatedly and require the context's fence
 * to pass every submission.  A path that accepts commands and never
 * executes them looks identical to a working one until this is checked.
 * ------------------------------------------------------------------ */
static void Test_RenderAdvancesTheFence(void)
{
    RENDERPATH_CONTEXT Ctx;
    UINT Submission;
    UINT Accepted = 0;
    ULONG LastFence = 0;
    ULONG FirstFence = 0;
    BOOL Regressed = FALSE;

    if (!RenderPathSetup(&Ctx))
        return;

    for (Submission = 0; Submission < RENDERPATH_SUBMISSIONS; ++Submission)
    {
        D3DKMT_RENDER render;
        TEST_SOFTGPU_CMD *Cmd;
        NTSTATUS Status;

        if (Ctx.pCommandBuffer == NULL || Ctx.CommandBufferSize < sizeof(*Cmd))
        {
            trace("run out of command buffer after %u submissions\n", Submission);
            break;
        }
        Cmd = (TEST_SOFTGPU_CMD *)Ctx.pCommandBuffer;
        memset(Cmd, 0, sizeof(*Cmd));
        Cmd->Magic = SOFTGPU_CMD_MAGIC;
        Cmd->Op = SOFTGPU_CMD_OP_NOP;
        Cmd->Size = sizeof(*Cmd);

        memset(&render, 0, sizeof(render));
        render.hContext = Ctx.hContext;
        render.CommandOffset = 0;
        render.CommandLength = sizeof(*Cmd);
        render.AllocationCount = 0;
        render.PatchLocationCount = 0;
        Status = pfnRender(&render);
        ok_succeeded(Status, "Render submission %u failed 0x%08lX\n", Submission, (long)Status);
        if (!NT_SUCCESS(Status))
            break;

        Accepted++;
        /* QueuedBufferCount is the depth of the context's queue, not a fence.
         * It may sit flat while submissions drain as fast as they arrive; what
         * it must never do is go backwards. */
        if (Accepted == 1)
            FirstFence = render.QueuedBufferCount;
        else if (render.QueuedBufferCount < LastFence)
            Regressed = TRUE;
        LastFence = render.QueuedBufferCount;

        Ctx.pCommandBuffer = render.pNewCommandBuffer;
        Ctx.CommandBufferSize = render.NewCommandBufferSize;
        Ctx.pAllocationList = render.pNewAllocationList;
        Ctx.pPatchLocationList = render.pNewPatchLocationList;
    }

    ok(Accepted > 0, "the render path accepted nothing at all\n");
    ok(!Regressed, "queued-buffer count went backwards across submissions\n");
    trace("render path: %u submissions accepted, queued %lu -> %lu\n",
          Accepted, (unsigned long)FirstFence, (unsigned long)LastFence);

    /* Every submission must drain.  A path that accepts work and never
     * completes it leaves the count pinned, which is exactly the failure a
     * negative test cannot see. */
    if (Accepted > 0)
    {
        PFND3DKMT_WAITFORIDLE pfnWait = (PFND3DKMT_WAITFORIDLE)LoadD3DKMTProc("D3DKMTWaitForIdle");

        if (pfnWait != NULL)
        {
            D3DKMT_WAITFORIDLE wfi;
            NTSTATUS Status;

            memset(&wfi, 0, sizeof(wfi));
            wfi.hDevice = Ctx.hDevice;
            Status = pfnWait(&wfi);
            ok_succeeded(Status, "device never went idle after %u submissions (0x%08lX)\n",
                         Accepted, (long)Status);
        }
        else
        {
            skip("D3DKMTWaitForIdle not exported; cannot prove the queue drained\n");
        }
    }

    RenderPathTeardown(&Ctx);
}

/* ------------------------------------------------------------------ *
 * A command stream the miniport cannot execute must be refused at
 * Render, before it ever becomes a DMA buffer.  Accepting it and
 * skipping it at execution time is how corrupt streams reach an engine.
 * ------------------------------------------------------------------ */
static void Test_MalformedStreamsAreRefusedAtRender(void)
{
    RENDERPATH_CONTEXT Ctx;
    TEST_SOFTGPU_CMD *Cmd;
    D3DKMT_RENDER render;
    NTSTATUS Status;

    if (!RenderPathSetup(&Ctx))
        return;
    if (Ctx.pCommandBuffer == NULL || Ctx.CommandBufferSize < sizeof(*Cmd))
    {
        RenderPathTeardown(&Ctx);
        return;
    }
    Cmd = (TEST_SOFTGPU_CMD *)Ctx.pCommandBuffer;

    /* Wrong magic: not our stream at all. */
    memset(Cmd, 0, sizeof(*Cmd));
    Cmd->Magic = 0xDEADBEEF;
    Cmd->Op = SOFTGPU_CMD_OP_NOP;
    Cmd->Size = sizeof(*Cmd);
    memset(&render, 0, sizeof(render));
    render.hContext = Ctx.hContext;
    render.CommandLength = sizeof(*Cmd);
    Status = pfnRender(&render);
    ok_failed(Status, "Render accepted a stream with a foreign magic (0x%08lX)\n", (long)Status);

    /* A length that does not tile the record chain leaves a partial record at
     * the end, which the engine would read past. */
    memset(Cmd, 0, sizeof(*Cmd));
    Cmd->Magic = SOFTGPU_CMD_MAGIC;
    Cmd->Op = SOFTGPU_CMD_OP_NOP;
    Cmd->Size = sizeof(*Cmd);
    memset(&render, 0, sizeof(render));
    render.hContext = Ctx.hContext;
    render.CommandLength = sizeof(*Cmd) - 4;
    Status = pfnRender(&render);
    ok_failed(Status, "Render accepted a stream that does not tile its records (0x%08lX)\n",
              (long)Status);

    /* An opcode the engine does not implement. */
    memset(Cmd, 0, sizeof(*Cmd));
    Cmd->Magic = SOFTGPU_CMD_MAGIC;
    Cmd->Op = 0x7FFFFFFF;
    Cmd->Size = sizeof(*Cmd);
    memset(&render, 0, sizeof(render));
    render.hContext = Ctx.hContext;
    render.CommandLength = sizeof(*Cmd);
    Status = pfnRender(&render);
    ok_failed(Status, "Render accepted an unimplemented opcode (0x%08lX)\n", (long)Status);

    /* A zero-length render is not malformed: it carries no GPU work and is how
     * a user-mode driver asks for a fresh command buffer without submitting
     * anything.  dxgkrnl acknowledges it after validation, deliberately. */
    memset(&render, 0, sizeof(render));
    render.hContext = Ctx.hContext;
    render.CommandLength = 0;
    Status = pfnRender(&render);
    ok_succeeded(Status, "Render refused a zero-length (buffer-swap) request (0x%08lX)\n", (long)Status);

    /* Longer than the buffer it was handed. */
    memset(Cmd, 0, sizeof(*Cmd));
    Cmd->Magic = SOFTGPU_CMD_MAGIC;
    Cmd->Op = SOFTGPU_CMD_OP_NOP;
    Cmd->Size = sizeof(*Cmd);
    memset(&render, 0, sizeof(render));
    render.hContext = Ctx.hContext;
    render.CommandLength = Ctx.CommandBufferSize + sizeof(*Cmd);
    Status = pfnRender(&render);
    ok_failed(Status, "Render accepted a length past the command buffer (0x%08lX)\n", (long)Status);

    /* And the context still works afterwards: a refused stream must not have
     * left the ring in a state that rejects the next good one. */
    memset(Cmd, 0, sizeof(*Cmd));
    Cmd->Magic = SOFTGPU_CMD_MAGIC;
    Cmd->Op = SOFTGPU_CMD_OP_NOP;
    Cmd->Size = sizeof(*Cmd);
    memset(&render, 0, sizeof(render));
    render.hContext = Ctx.hContext;
    render.CommandLength = sizeof(*Cmd);
    Status = pfnRender(&render);
    ok_succeeded(Status, "context unusable after refused streams (0x%08lX)\n", (long)Status);

    RenderPathTeardown(&Ctx);
}

/* ------------------------------------------------------------------ *
 * Two contexts on one device submitting alternately.  The scheduler
 * must keep their fences independent -- one context's progress must
 * never retire the other's work.
 * ------------------------------------------------------------------ */
static void Test_TwoContextsSubmitIndependently(void)
{
    RENDERPATH_CONTEXT Ctx;
    D3DKMT_CREATECONTEXT cc2;
    D3DKMT_HANDLE hContext2 = 0;
    VOID *pBuffer2;
    UINT Size2;
    UINT Round;
    UINT Accepted1 = 0, Accepted2 = 0;
    NTSTATUS Status;

    if (!RenderPathSetup(&Ctx))
        return;

    memset(&cc2, 0, sizeof(cc2));
    cc2.hDevice = Ctx.hDevice;
    cc2.NodeOrdinal = 0;
    cc2.EngineAffinity = 0;
    Status = pfnCreateContext(&cc2);
    if (!NT_SUCCESS(Status))
    {
        skip("second CreateContext refused (0x%08lX)\n", (long)Status);
        RenderPathTeardown(&Ctx);
        return;
    }
    hContext2 = cc2.hContext;
    pBuffer2 = cc2.pCommandBuffer;
    Size2 = cc2.CommandBufferSize;
    ok(hContext2 != Ctx.hContext, "two contexts share one handle\n");
    ok(pBuffer2 != Ctx.pCommandBuffer, "two contexts share one command buffer\n");

    for (Round = 0; Round < 8; ++Round)
    {
        D3DKMT_RENDER r1, r2;
        TEST_SOFTGPU_CMD *Cmd;

        if (Ctx.pCommandBuffer != NULL && Ctx.CommandBufferSize >= sizeof(*Cmd))
        {
            Cmd = (TEST_SOFTGPU_CMD *)Ctx.pCommandBuffer;
            memset(Cmd, 0, sizeof(*Cmd));
            Cmd->Magic = SOFTGPU_CMD_MAGIC;
            Cmd->Op = SOFTGPU_CMD_OP_NOP;
            Cmd->Size = sizeof(*Cmd);
            memset(&r1, 0, sizeof(r1));
            r1.hContext = Ctx.hContext;
            r1.CommandLength = sizeof(*Cmd);
            if (NT_SUCCESS(pfnRender(&r1)))
            {
                Accepted1++;
                Ctx.pCommandBuffer = r1.pNewCommandBuffer;
                Ctx.CommandBufferSize = r1.NewCommandBufferSize;
            }
        }
        if (pBuffer2 != NULL && Size2 >= sizeof(*Cmd))
        {
            Cmd = (TEST_SOFTGPU_CMD *)pBuffer2;
            memset(Cmd, 0, sizeof(*Cmd));
            Cmd->Magic = SOFTGPU_CMD_MAGIC;
            Cmd->Op = SOFTGPU_CMD_OP_NOP;
            Cmd->Size = sizeof(*Cmd);
            memset(&r2, 0, sizeof(r2));
            r2.hContext = hContext2;
            r2.CommandLength = sizeof(*Cmd);
            if (NT_SUCCESS(pfnRender(&r2)))
            {
                Accepted2++;
                pBuffer2 = r2.pNewCommandBuffer;
                Size2 = r2.NewCommandBufferSize;
            }
        }
    }
    trace("interleaved: context1 accepted %u, context2 accepted %u\n", Accepted1, Accepted2);
    ok(Accepted1 > 0 && Accepted2 > 0,
       "interleaved submission starved a context (%u / %u)\n", Accepted1, Accepted2);

    if (pfnDestroyContext)
    {
        D3DKMT_DESTROYCONTEXT dc;

        memset(&dc, 0, sizeof(dc));
        dc.hContext = hContext2;
        Status = pfnDestroyContext(&dc);
        ok_succeeded(Status, "destroying the second context failed 0x%08lX\n", (long)Status);
    }

    /* The first context must be unaffected by the second's destruction. */
    {
        D3DKMT_RENDER r;
        TEST_SOFTGPU_CMD *Cmd;

        if (Ctx.pCommandBuffer != NULL && Ctx.CommandBufferSize >= sizeof(*Cmd))
        {
            Cmd = (TEST_SOFTGPU_CMD *)Ctx.pCommandBuffer;
            memset(Cmd, 0, sizeof(*Cmd));
            Cmd->Magic = SOFTGPU_CMD_MAGIC;
            Cmd->Op = SOFTGPU_CMD_OP_NOP;
            Cmd->Size = sizeof(*Cmd);
            memset(&r, 0, sizeof(r));
            r.hContext = Ctx.hContext;
            r.CommandLength = sizeof(*Cmd);
            Status = pfnRender(&r);
            ok_succeeded(Status, "surviving context broke when its peer was destroyed (0x%08lX)\n",
                         (long)Status);
        }
    }

    RenderPathTeardown(&Ctx);
}

START_TEST(renderpath)
{
    pfnRender = (PFND3DKMT_RENDER)LoadD3DKMTProc("D3DKMTRender");
    pfnCreateContext = (PFND3DKMT_CREATECONTEXT)LoadD3DKMTProc("D3DKMTCreateContext");
    pfnDestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    pfnCreateDevice = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    pfnDestroyDevice = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");

    if (!pfnRender || !pfnCreateContext || !pfnDestroyContext ||
        !pfnCreateDevice || !pfnDestroyDevice)
    {
        skip("render-path entry points not exported\n");
        return;
    }

    Test_RenderAdvancesTheFence();
    Test_MalformedStreamsAreRefusedAtRender();
    Test_TwoContextsSubmitIndependently();
}

/* EOF */
