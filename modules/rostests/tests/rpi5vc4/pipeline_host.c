/*
 * PROJECT:     ReactOS host-side miniport tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Exercise the production queue handoff with simulated hardware.
 *
 * run-pipeline-host.py extracts the production function and queue helpers. Hardware kicks,
 * completion latches and MMU ownership are controlled here; the queue walk and
 * fence retirement logic are the actual miniport code, not a duplicated model.
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define _In_
#define _Inout_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define TRUE 1
#define FALSE 0
#define VOID void
#define RPI5VC4_GPU_NODE_COUNT 3
#define RPI5VC4_NODE_3D 0
#define RPI5VC4_V3D_SLAB_GPUVA 0x1000u
#define RPI5VC4_DMA_V3D_FLUSH_CACHE 1
#define RPI5VC4_DMA_V3D_BCL_INDEPENDENT 2
#define RPI5VC4_V3D_JOB_TIMEOUT_100NS 5000000
#define V3D_CLE_CT1CA 0
#define V3D_CLE_CT01RA0 0
#define V3D_CLE_CT0CA 0
#define V3D_CLE_CT00RA0 0
#define DPRINT1(...) ((void)0)
typedef int BOOLEAN, *PBOOLEAN;
typedef uint32_t ULONG, *PULONG;
typedef int32_t LONG;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef uint8_t UCHAR, *PUCHAR;
typedef void *PVOID;
typedef struct _RPI5VC4_PROCESS
{
    int id;
} PROCESS, *PRPI5VC4_PROCESS;
#include "pending.h"
typedef struct
{
    RPI5VC4_PENDING_SUBMIT *Head;
    ULONG Count;
} QUEUE;
typedef struct
{
    QUEUE NodeQueue[3];
    BOOLEAN V3dReady, StopAccepting, V3dExecGateActive;
    PROCESS *V3dActiveProcess;
    PVOID V3dCoreBase;
    ULONG LastCompletedFence, LastCompletedFencePerNode[3];
} DEVICE, *PRPI5VC4_DEVICE_EXTENSION;
static ULONGLONG KeQueryInterruptTime(void)
{
    return 100;
}
static ULONG READ_REGISTER_ULONG(PULONG p)
{
    assert(0);
    return 0;
}
static int tfu_done, csd_done, render_latch, render_done, bin_latch, kicks, consumes;
#include "helpers.h"
static BOOLEAN Rpi5Vc4SelectAddressSpaceLocked(DEVICE *d, PROCESS *p)
{
    if (d->V3dActiveProcess == p)
        return TRUE;
    assert(!Rpi5Vc4GpuJobActiveLocked(d));
    d->V3dActiveProcess = p;
    return TRUE;
}
static void Rpi5V3dConsumeCompletions(DEVICE *d, PBOOLEAN b, PBOOLEAN r, PBOOLEAN c, PBOOLEAN o)
{
    consumes++;
    *b = bin_latch;
    *r = render_latch;
    *c = csd_done;
    *o = FALSE;
    bin_latch = render_latch = 0;
}
static BOOLEAN Rpi5Vc4ServiceBinOomLocked(DEVICE *d, ULONGLONG n, PRPI5VC4_PENDING_SUBMIT *p)
{
    assert(0);
    return FALSE;
}
static BOOLEAN Rpi5V3dSubmitTfu(DEVICE *d, ULONG *p, UCHAR *q)
{
    kicks++;
    return TRUE;
}
static BOOLEAN Rpi5V3dSubmitCsd(DEVICE *d, ULONG *p)
{
    kicks++;
    return TRUE;
}
static BOOLEAN Rpi5V3dTfuDone(DEVICE *d, UCHAR v)
{
    return tfu_done;
}
static BOOLEAN Rpi5V3dCsdDone(DEVICE *d)
{
    return csd_done;
}
static BOOLEAN Rpi5Vc4KickBinLocked(DEVICE *d, PRPI5VC4_PENDING_SUBMIT e, ULONGLONG n)
{
    e->BinSubmitted = TRUE;
    kicks++;
    return TRUE;
}
static void Rpi5Vc4UpdateBinCompletionLocked(DEVICE *d, PRPI5VC4_PENDING_SUBMIT e, PBOOLEAN b)
{
    if (*b)
        e->BinDone = TRUE;
    *b = FALSE;
}
static BOOLEAN Rpi5V3dSubmitRender(DEVICE *d, ULONG a, ULONG b, BOOLEAN c, UCHAR *q)
{
    kicks++;
    return TRUE;
}
static BOOLEAN Rpi5V3dRenderDone(DEVICE *d, UCHAR c)
{
    return render_done;
}
static BOOLEAN Rpi5V3dCleanCaches(DEVICE *d)
{
    return TRUE;
}
static void Rpi5Vc4RemoveSubmitLocked(DEVICE *d, ULONG n)
{
    assert(d->NodeQueue[n].Count > 0);
    d->NodeQueue[n].Head = d->NodeQueue[n].Head->Next;
    d->NodeQueue[n].Count--;
}
static void Rpi5Vc4ClearPendingLocked(DEVICE *d)
{
    for (int i = 0; i < 3; i++)
    {
        d->NodeQueue[i].Head = NULL;
        d->NodeQueue[i].Count = 0;
    }
}
#include "pipeline.h"
static PROCESS pa = {1}, pb = {2};
static void RunHandoffCase(int node, int in_flight, int gate, int invalid_node, int not_done)
{
    DEVICE d = {0};
    RPI5VC4_PENDING_SUBMIT first = {0}, second = {0}, other = {0};
    BOOLEAN poll, abort;
    tfu_done = csd_done = render_done = render_latch = bin_latch = kicks = consumes = 0;
    d.V3dReady = TRUE;
    d.V3dActiveProcess = &pb;
    d.V3dExecGateActive = gate;
    first.IsV3dJob = TRUE;
    first.Process = &pa;
    first.Fence = 2;
    first.SubmissionSequence = 2;
    first.Next = &second;
    second.IsV3dJob = TRUE;
    second.Process = &pa;
    second.Fence = 3;
    second.SubmissionSequence = 3;
    other.Process = &pb;
    other.Fence = 1;
    other.SubmissionSequence = 1;
    other.ReportNode = invalid_node ? 3 : node;
    other.RenderSubmitted = TRUE;
    other.QueuedTime100ns = 50;
    if (node == 1)
    {
        other.IsTfuJob = TRUE;
        other.TfuRegs[1] = other.TfuRegs[6] = 0x2000;
        tfu_done = TRUE;
    }
    else
    {
        other.IsCsdJob = TRUE;
        csd_done = TRUE;
    }
    if (not_done)
        tfu_done = csd_done = FALSE;
    if (in_flight)
    {
        first.RenderSubmitted = TRUE;
        first.Process = &pb;
    }
    d.NodeQueue[0] = (QUEUE){&first, 2};
    d.NodeQueue[node] = (QUEUE){&other, 1};
    BOOLEAN completed = Rpi5Vc4ProcessPendingLocked(&d, &poll, &abort);
    if (gate)
    {
        assert(!completed && poll && !abort && !first.RenderSubmitted &&
               d.NodeQueue[node].Count == 1 && consumes == 0);
        return;
    }
    if (not_done)
    {
        assert(!completed && poll && !abort && !first.RenderSubmitted &&
               d.NodeQueue[node].Count == 1 && consumes == 1);
        return;
    }
    if (invalid_node)
    {
        assert(abort && d.StopAccepting && d.NodeQueue[0].Count == 0 && consumes == 1);
        return;
    }
    assert(completed && poll && !abort && d.NodeQueue[node].Count == 0);
    assert(d.LastCompletedFencePerNode[node] == 1 && d.LastCompletedFencePerNode[0] == 0);
    assert(first.RenderSubmitted && !second.RenderSubmitted && d.NodeQueue[0].Head == &first);
    if (in_flight)
    {
        assert(kicks == 0 && consumes == 1 && d.V3dActiveProcess == &pb);
    }
    else
    {
        assert(kicks == 1 && consumes == 2 && d.V3dActiveProcess == &pa);
    }
}
int main(void)
{
    for (int node = 1; node < 3; node++)
    {
        RunHandoffCase(node, 0, 0, 0, 0);
        RunHandoffCase(node, 1, 0, 0, 0);
        RunHandoffCase(node, 0, 1, 0, 0);
        RunHandoffCase(node, 0, 0, 1, 0);
        RunHandoffCase(node, 0, 0, 0, 1);
    }
    puts("10 production-pipeline scenarios passed: later-engine completion immediately starts "
         "earlier ready work; active jobs, FIFO fences, exec gate and abort remain protected");
}
