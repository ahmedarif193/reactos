/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU submission-pipeline liveness: drives the dxgkrnl
 *              render-command escape (Render -> vidsch queue -> fence-at-
 *              kick -> SubmitCommand -> per-node completion -> retire)
 *              against the rpi5vc4 miniport's driver-private DMA stream.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Win11-green rule: success is asserted ONLY when the adapter positively
 * identifies as rpi5vc4 (the RPI5VC4 info escape succeeds).  On any other
 * driver (softgpu, viogpudo, real Win11 stacks) every submission outcome
 * is tolerated and the test skips.
 *
 * The pipeline payload is universally safe: a NOP packet plus a V3D job
 * whose bin and render lists are empty.  Without V3D hardware (QEMU) the
 * miniport CPU-completes the fence; with V3D the empty render list is
 * refused at the kick and the fence also completes immediately — no GPU
 * work is ever issued.  Submitting more packets than the miniport's
 * 64-entry pending ring proves fences actually retire (a stalled
 * pipeline would return STATUS_DEVICE_BUSY once the ring fills).
 */

#include "precomp.h"
#include <reactos/rpi5vc4_umd.h>

/* Driver-private contracts, mirrored from drivers/directx/rpi5vc4 and
 * drivers/directx/dxgkrnl (the test IS the second implementer). */

#define RPI5VC4_DMA_PACKET_MAGIC     0x52355644u
#define RPI5VC4_DMA_OP_NOP           0
#define RPI5VC4_DMA_OP_V3D_JOB       2

typedef struct _TEST_RPI5VC4_DMA_PACKET
{
    ULONG Magic;
    ULONG Op;
    ULONG Length;
    ULONG Reserved;
    union
    {
        struct
        {
            ULONG BclStart;
            ULONG BclEnd;
            ULONG RclStart;
            ULONG RclEnd;
        } V3dJob;
        struct
        {
            RECT DstRect;
            ULONG Color;
            ULONG Flags;
        } Present;
        struct
        {
            ULONG Regs[12];
        } TfuJob;
        struct
        {
            ULONG Cfg[8];
        } CsdJob;
    };
} TEST_RPI5VC4_DMA_PACKET;

/* dxgkrnl's command-escape transport framing (d3dkmt.c). */
typedef struct _TEST_ESCAPE_PACKET_HEADER
{
    USHORT PacketType;
    USHORT PayloadBytes;
} TEST_ESCAPE_PACKET_HEADER;

typedef struct _TEST_ESCAPE_COMMAND_HEADER
{
    UINT CommandType;
    UINT PayloadBytes;
} TEST_ESCAPE_COMMAND_HEADER;

typedef struct _TEST_PIPELINE_ESCAPE
{
    TEST_ESCAPE_PACKET_HEADER  Packet;
    TEST_ESCAPE_COMMAND_HEADER Command;
    TEST_RPI5VC4_DMA_PACKET    Stream[2];
} TEST_PIPELINE_ESCAPE;

/* Exceeds the miniport's 64-entry pending ring: proves retirement. */
#define PIPELINE_SUBMissions 80

static BOOLEAN
QueryRpi5vc4Info(
    PFN_D3DKMTEscape pfnEscape,
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice,
    PRPI5VC4_ESCAPE_INFO Info)
{
    D3DKMT_ESCAPE Escape;

    memset(Info, 0, sizeof(*Info));
    Info->Magic = RPI5VC4_ESCAPE_MAGIC;
    Info->Op = RPI5VC4_ESCAPE_OP_QUERY_INFO;

    memset(&Escape, 0, sizeof(Escape));
    Escape.hAdapter = hAdapter;
    Escape.hDevice = hDevice;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = Info;
    Escape.PrivateDriverDataSize = sizeof(*Info);

    return NT_SUCCESS(pfnEscape(&Escape));
}

static NTSTATUS
SubmitPipelinePacket(
    PFN_D3DKMTEscape pfnEscape,
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    TEST_PIPELINE_ESCAPE Payload;
    D3DKMT_ESCAPE Escape;

    memset(&Payload, 0, sizeof(Payload));
    Payload.Packet.PacketType = 1;
    Payload.Packet.PayloadBytes =
        (USHORT)(sizeof(Payload) - sizeof(Payload.Packet));
    Payload.Command.CommandType = 1;
    Payload.Command.PayloadBytes = sizeof(Payload.Stream);

    Payload.Stream[0].Magic = RPI5VC4_DMA_PACKET_MAGIC;
    Payload.Stream[0].Op = RPI5VC4_DMA_OP_NOP;
    Payload.Stream[0].Length = sizeof(Payload.Stream[0]);

    /* Empty bin + empty render list: completes without touching V3D. */
    Payload.Stream[1].Magic = RPI5VC4_DMA_PACKET_MAGIC;
    Payload.Stream[1].Op = RPI5VC4_DMA_OP_V3D_JOB;
    Payload.Stream[1].Length = sizeof(Payload.Stream[1]);

    memset(&Escape, 0, sizeof(Escape));
    Escape.hAdapter = hAdapter;
    Escape.hDevice = hDevice;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = &Payload;
    Escape.PrivateDriverDataSize = sizeof(Payload);

    return pfnEscape(&Escape);
}

START_TEST(pipeline)
{
    PFN_D3DKMTEscape pfnEscape;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice = 0;
    RPI5VC4_ESCAPE_INFO Info;
    BOOLEAN IsRpi5vc4;
    NTSTATUS Status;
    ULONG Iteration;
    ULONG Succeeded = 0;

    pfnEscape = (PFN_D3DKMTEscape)LoadD3DKMTProc("D3DKMTEscape");
    if (!pfnEscape)
    {
        skip("D3DKMTEscape not exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("cannot open adapter\n");
        return;
    }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("cannot create device\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* Positive identification gates every success assertion below. */
    IsRpi5vc4 = QueryRpi5vc4Info(pfnEscape, hAdapter, hDevice, &Info);

    if (IsRpi5vc4)
    {
        trace("rpi5vc4: V3dReady=%lu ver=%lu slab=%luMB %lux%lu caps=0x%08lx\n",
              Info.V3dReady, Info.V3dVersion,
              Info.SlabSize / (1024 * 1024),
              Info.ScreenWidth, Info.ScreenHeight, Info.Caps);

        ok(Info.AbiVersion >= RPI5VC4_ESCAPE_INFO_ABI_VERSION,
           "expected rpi5vc4 escape ABI >= %u, got %lu\n",
           RPI5VC4_ESCAPE_INFO_ABI_VERSION, Info.AbiVersion);
        ok((Info.Caps & RPI5VC4_CAP_GPUVA_MAP) != 0, "rpi5vc4 escape caps missing GPUVA_MAP\n");
        ok((Info.Caps & RPI5VC4_CAP_ALLOCATION_RELOCATION) != 0,
           "rpi5vc4 escape caps missing ALLOCATION_RELOCATION\n");
        ok((Info.Caps & RPI5VC4_CAP_MONITORED_FENCE) != 0, "rpi5vc4 escape caps missing MONITORED_FENCE\n");
        ok((Info.Caps & RPI5VC4_CAP_SUBMIT_SIGNAL) != 0,
           "rpi5vc4 escape caps missing SUBMIT_SIGNAL\n");
        ok((Info.Caps & RPI5VC4_CAP_CPU_WAIT_SIGNAL) != 0, "rpi5vc4 escape caps missing CPU_WAIT_SIGNAL\n");
        ok(Info.NodeCount >= 3,
           "rpi5vc4 should expose at least 3 nodes, got %lu\n",
           Info.NodeCount);
        ok(Info.MaxPendingSubmits >= PIPELINE_SUBMissions - 16,
           "rpi5vc4 pending depth unexpectedly small: %lu\n",
           Info.MaxPendingSubmits);

        if (Info.V3dReady)
        {
            ok((Info.Caps & RPI5VC4_CAP_CL_SUBMIT) != 0,
               "V3dReady without CL_SUBMIT cap\n");
            ok((Info.Caps & RPI5VC4_CAP_TFU_SUBMIT) != 0,
               "V3dReady without TFU_SUBMIT cap\n");
            ok((Info.Caps & RPI5VC4_CAP_CSD_SUBMIT) != 0,
               "V3dReady without CSD_SUBMIT cap\n");
            ok((Info.Caps & RPI5VC4_CAP_CACHE_FLUSH) != 0,
               "V3dReady without CACHE_FLUSH cap\n");
        }
    }

    for (Iteration = 0; Iteration < PIPELINE_SUBMissions; Iteration++)
    {
        Status = SubmitPipelinePacket(pfnEscape, hAdapter, hDevice);
        if (NT_SUCCESS(Status))
        {
            Succeeded++;
        }
        else if (IsRpi5vc4)
        {
            /* Ring is 64 deep: a failure before draining it means fences
             * stopped retiring (queue->kick->complete->retire stalled). */
            ok(FALSE, "submission %lu failed 0x%08lX — pipeline stalled?\n",
               Iteration, Status);
            break;
        }
        else
        {
            /* Foreign driver: private stream refused is a valid outcome. */
            break;
        }
    }

    if (IsRpi5vc4)
    {
        ok(Succeeded == PIPELINE_SUBMissions,
           "expected %u pipeline submissions to retire, got %lu\n",
           PIPELINE_SUBMissions, Succeeded);
    }
    else
    {
        skip("not rpi5vc4 (%lu/%u accepted) — outcomes tolerated\n",
             Succeeded, PIPELINE_SUBMissions);
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}
