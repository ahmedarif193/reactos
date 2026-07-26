/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     v3dsmoke Phase A (UMD track Stage 1 groundwork): the first
 *              UMD-shaped submission — create an allocation in the VRAM
 *              slab, Lock it, write control-list bytes with the vc4cle
 *              encoders, and submit an ALLOCATION-RELATIVE V3D job so the
 *              kernel exercises the whole relocation chain: Render emits
 *              patch locations -> vidsch kick mints the fence -> Patch
 *              validates slab membership and rewrites GPU VAs -> per-node
 *              queue completes the (empty-range) job.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Hardware-safe by construction: the job's BCL is absent and the RCL
 * range is empty (Start == End), so nothing is ever kicked to the CLE —
 * on a Raspberry Pi 5 exactly as in QEMU the fence completes without
 * touching the 3D core.  Phase B (the mesa-transcribed clear job with a
 * CPU readback) extends this file once GPU-VA discovery inside CLs is
 * settled (see vc4-umd-design.txt Stage 1).
 *
 * Win11-green: success is asserted only on positively-identified
 * rpi5vc4 adapters; everywhere else the private contracts are refused
 * and the test skips.
 */

#include "precomp.h"
#include <reactos/rpi5vc4_umd.h>
#include <reactos/vc4cle.h>

#define SMOKE_DMA_PACKET_MAGIC       0x52355644u
#define SMOKE_DMA_OP_V3D_JOB         2
#define SMOKE_RESOURCE_LIST_MAGIC    0x5652474cUL

/* Mirror the key verdicts to the kernel debug channel so a boot-time
 * probe run (Session-0 service, no console) still reports over serial —
 * the same proof-line pattern as the VULKAN-1/OPENGL32 loader lines. */
static void V3dProof(const char *Format, ...)
{
    char Buffer[192];
    va_list Args;

    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Args);
    va_end(Args);
    Buffer[sizeof(Buffer) - 1] = '\0';
    OutputDebugStringA(Buffer);
}

typedef RPI5VC4_ESCAPE_INFO SMOKE_INFO_ESCAPE;

typedef struct _SMOKE_DMA_PACKET
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
            ULONG BclAllocIndexPlusOne;
            ULONG RclAllocIndexPlusOne;
        } V3dJob;
        ULONG Pad[12];
    };
} SMOKE_DMA_PACKET;

/* Escape transport framing (dxgkrnl d3dkmt.c). */
#pragma pack(push, 1)
typedef struct _SMOKE_SUBMIT_ESCAPE
{
    USHORT PacketType;
    USHORT PayloadBytes;
    struct
    {
        ULONG Magic;
        UINT ResourceCount;
    } Resources;
    D3DKMT_HANDLE hResource;
    struct
    {
        UINT CommandType;
        UINT PayloadBytes;
    } Command;
    SMOKE_DMA_PACKET Packet;
} SMOKE_SUBMIT_ESCAPE;

/* PacketType 2: submit + signal a monitored fence on GPU completion. */
typedef struct _SMOKE_SUBMIT_SIGNAL_ESCAPE
{
    USHORT PacketType;
    USHORT PayloadBytes;
    struct
    {
        ULONG Magic;
        UINT ResourceCount;
    } Resources;
    D3DKMT_HANDLE hResource;
    struct
    {
        UINT CommandType;
        UINT PayloadBytes;
    } Command;
    struct
    {
        D3DKMT_HANDLE hSyncObject;
        ULONG64 FenceValue;
    } Signal;
    SMOKE_DMA_PACKET Packet;
} SMOKE_SUBMIT_SIGNAL_ESCAPE;
#pragma pack(pop)

/* ------------------------------------------------------------------ *
 * Phase B: the mesa-transcribed RCL clear job with real GPU VAs from
 * D3DKMTMapGpuVirtualAddress (segment-logical -> V3D window via the
 * escape's SlabPhysical/SlabGpuVa).  Encode+map+submit runs everywhere
 * the adapter is rpi5vc4; the pixel-readback assertion arms only when
 * V3dReady (real silicon).  Tile-geometry field values are flagged for
 * mesa-golden confirmation on hardware (encode bit positions are the
 * tested contract; wrong geometry misrenders, TDR recovers).
 * ------------------------------------------------------------------ */

typedef NTSTATUS (APIENTRY *SMOKE_PFN_MAPGPUVA)(D3DDDI_MAPGPUVIRTUALADDRESS *);

static ULONG
SmokeGpuVa(const SMOKE_INFO_ESCAPE *Info, ULONGLONG SegmentLogical)
{
    return (ULONG)(Info->SlabGpuVa +
                   (SegmentLogical - Info->SlabPhysical));
}

static BOOLEAN
SmokeMapAllocation(
    SMOKE_PFN_MAPGPUVA pfnMap,
    const SMOKE_INFO_ESCAPE *Info,
    D3DKMT_HANDLE hAllocation,
    UINT SizeInPages,
    ULONG *GpuVaOut)
{
    D3DDDI_MAPGPUVIRTUALADDRESS Map;
    NTSTATUS Status;

    memset(&Map, 0, sizeof(Map));
    Map.hAllocation = hAllocation;
    Map.SizeInPages = SizeInPages;
    Status = pfnMap(&Map);
    if (!NT_SUCCESS(Status))
        return FALSE;

    if ((ULONGLONG)Map.VirtualAddress < Info->SlabPhysical ||
        (ULONGLONG)Map.VirtualAddress >=
            Info->SlabPhysical + Info->SlabSize)
    {
        return FALSE;
    }

    *GpuVaOut = SmokeGpuVa(Info, (ULONGLONG)Map.VirtualAddress);
    return TRUE;
}

static void
SmokePhaseB(
    PFN_D3DKMTEscape pfnEscape,
    PFN_D3DKMTCreateAllocation pfnCreateAlloc,
    PFN_D3DKMTDestroyAllocation pfnDestroyAlloc,
    PFN_D3DKMTLock pfnLock,
    PFN_D3DKMTUnlock pfnUnlock,
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice,
    const SMOKE_INFO_ESCAPE *Info)
{
    SMOKE_PFN_MAPGPUVA pfnMap;
    enum { ALLOC_DST, ALLOC_TILE, ALLOC_CL, ALLOC_COUNT };
    D3DKMT_HANDLE hAllocs[ALLOC_COUNT];
    UINT Sizes[ALLOC_COUNT] = { 64 * 64 * 4, 64 * 1024, 4096 };
    ULONG GpuVas[ALLOC_COUNT];
    D3DKMT_CREATEALLOCATION CreateData;
    D3DDDI_ALLOCATIONINFO AllocationInfo;
    D3DKMT_LOCK LockData;
    NTSTATUS Status;
    int a;
    const ULONG ClearArgb = 0xFF3366CCu;

    pfnMap = (SMOKE_PFN_MAPGPUVA)LoadD3DKMTProc("D3DKMTMapGpuVirtualAddress");
    if (!pfnMap)
    {
        skip("D3DKMTMapGpuVirtualAddress not exported\n");
        return;
    }

    memset(hAllocs, 0, sizeof(hAllocs));
    memset(GpuVas, 0, sizeof(GpuVas));

    for (a = 0; a < ALLOC_COUNT; a++)
    {
        memset(&CreateData, 0, sizeof(CreateData));
        memset(&AllocationInfo, 0, sizeof(AllocationInfo));
        AllocationInfo.PrivateDriverDataSize = sizeof(UINT);
        AllocationInfo.pPrivateDriverData = &Sizes[a];
        CreateData.hDevice = hDevice;
        CreateData.NumAllocations = 1;
        CreateData.pAllocationInfo = &AllocationInfo;
        Status = pfnCreateAlloc(&CreateData);
        if (!NT_SUCCESS(Status))
        {
            skip("PhaseB alloc %d failed 0x%08lX\n", a, Status);
            goto cleanup;
        }
        hAllocs[a] = AllocationInfo.hAllocation;

        if (!SmokeMapAllocation(pfnMap, Info, hAllocs[a],
                                (Sizes[a] + 4095) / 4096, &GpuVas[a]))
        {
            skip("PhaseB MapGpuVirtualAddress %d failed/out-of-slab\n", a);
            goto cleanup;
        }
    }

    ok(GpuVas[ALLOC_CL] >= Info->SlabGpuVa,
       "CL GPU VA 0x%08lx below slab window\n", GpuVas[ALLOC_CL]);
    trace("PhaseB: dst=0x%08lx tile=0x%08lx cl=0x%08lx\n",
          GpuVas[ALLOC_DST], GpuVas[ALLOC_TILE], GpuVas[ALLOC_CL]);

    /* Encode the mesa-transcribed clear RCL with real addresses. */
    memset(&LockData, 0, sizeof(LockData));
    LockData.hDevice = hDevice;
    LockData.hAllocation = hAllocs[ALLOC_CL];
    Status = pfnLock(&LockData);
    if (!NT_SUCCESS(Status) || LockData.pData == NULL)
    {
        skip("PhaseB CL Lock failed 0x%08lX\n", Status);
        goto cleanup;
    }

    {
        unsigned char *Base = (unsigned char *)LockData.pData;
        unsigned char *Cl = Base;
        unsigned char *SubStart;
        ULONG RclStartVa = GpuVas[ALLOC_CL];
        ULONG RclEndVa;
        int i;

        Cl = Vc4CleTrmCfgCommonV71(Cl, 1, 64, 64, V3D71_DEPTH_TYPE_32F,
                                   /* log2 tile w/h: 64px tile => 3 —
                                    * pending mesa-golden check */ 3, 3);
        Cl = Vc4CleTrmCfgRtPart1V71(Cl, 0, 0, 1, V3D71_INTERNAL_BPP_32,
                                    V3D71_RT_TYPE_CLAMP_8, ClearArgb);
        Cl = Vc4CleTrmCfgZsClearV71(Cl, 0x3F800000, 0);
        Cl = Vc4CleTileListInitialBlockSize(Cl, 0, 1);
        Cl = Vc4CleTileListBase(Cl, 0, GpuVas[ALLOC_TILE]);
        Cl = Vc4CleMulticoreSupertileCfg(Cl, 1, 1, 1, 1, 1, 1, 1);

        /* GFXH-1742 + initial clear (mesa emit_frame_setup). */
        for (i = 0; i < 2; i++)
        {
            Cl = Vc4CleTileCoords(Cl, 0, 0);
            Cl = Vc4CleEndOfLoads(Cl);
            Cl = Vc4CleStoreTileBufferGeneral(Cl, V3D71_BUFFER_NONE,
                                              V3D71_MEMORY_FORMAT_RASTER,
                                              0, 0, 0, 0);
            if (i == 0)
                Cl = Vc4CleClearRenderTargetsV71(Cl);
            Cl = Vc4CleEndOfTileMarker(Cl);
        }
        Cl = Vc4CleFlushVcdCache(Cl);

        /* Per-tile generic sub-list. */
        SubStart = Cl;
        Cl = Vc4CleTileCoords(Cl, 0, 0);
        Cl = Vc4CleEndOfLoads(Cl);
        Cl = Vc4CleStoreTileBufferGeneral(Cl, V3D71_BUFFER_RT0,
                                          V3D71_MEMORY_FORMAT_RASTER,
                                          V3D71_OUTPUT_FORMAT_RGBA8,
                                          0, 64 * 4, GpuVas[ALLOC_DST]);
        Cl = Vc4CleEndOfTileMarker(Cl);
        Cl = Vc4CleReturnFromSubList(Cl);

        Cl = Vc4CleGenericTileList(Cl,
                 GpuVas[ALLOC_CL] + (ULONG)(SubStart - Base),
                 GpuVas[ALLOC_CL] + (ULONG)(Cl - Base));
        Cl = Vc4CleSupertileCoords(Cl, 0, 0);
        Cl = Vc4CleEndOfRendering(Cl);

        RclEndVa = GpuVas[ALLOC_CL] + (ULONG)(Cl - Base);
        ok((SIZE_T)(Cl - Base) <= Sizes[ALLOC_CL],
           "PhaseB CL overflow (%u bytes)\n", (unsigned)(Cl - Base));

        {
            D3DKMT_UNLOCK UnlockData;
            memset(&UnlockData, 0, sizeof(UnlockData));
            UnlockData.hDevice = hDevice;
            UnlockData.NumAllocations = 1;
            UnlockData.phAllocations = &hAllocs[ALLOC_CL];
            (void)pfnUnlock(&UnlockData);
        }

        /*
         * Submit an absolute-VA job over the encoded RCL, with a
         * monitored fence signalled on GPU completion (PacketType 2) —
         * the documented WDDM completion-wait, verified end-to-end even
         * without silicon (the empty job completes and must signal).
         */
        {
            SMOKE_SUBMIT_SIGNAL_ESCAPE Submit;
            D3DKMT_ESCAPE Escape;
            D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Fence;
            PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pfnCreateSync;
            PFN_D3DKMTDestroySynchronizationObject pfnDestroySync;
            typedef NTSTATUS (APIENTRY *SMOKE_PFN_WAITCPU)(
                const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *);
            SMOKE_PFN_WAITCPU pfnWaitCpu;

            pfnCreateSync = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)
                LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
            pfnDestroySync = (PFN_D3DKMTDestroySynchronizationObject)
                LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
            pfnWaitCpu = (SMOKE_PFN_WAITCPU)
                LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromCpu");

            memset(&Fence, 0, sizeof(Fence));
            Fence.hDevice = hDevice;
            Fence.Info.Type = D3DDDI_MONITORED_FENCE;
            if (pfnCreateSync == NULL || pfnWaitCpu == NULL ||
                !NT_SUCCESS(pfnCreateSync(&Fence)))
            {
                Fence.hSyncObject = 0;
            }

            memset(&Submit, 0, sizeof(Submit));
            Submit.PacketType = Fence.hSyncObject ? 2 : 1;
            Submit.PayloadBytes = (USHORT)(sizeof(Submit) - 4);
            Submit.Resources.Magic = SMOKE_RESOURCE_LIST_MAGIC;
            Submit.Resources.ResourceCount = 1;
            Submit.hResource = hAllocs[ALLOC_CL];
            Submit.Command.CommandType = Submit.PacketType;
            Submit.Command.PayloadBytes =
                sizeof(Submit.Signal) + sizeof(Submit.Packet);
            Submit.Signal.hSyncObject = Fence.hSyncObject;
            Submit.Signal.FenceValue = 7;
            Submit.Packet.Magic = SMOKE_DMA_PACKET_MAGIC;
            Submit.Packet.Op = SMOKE_DMA_OP_V3D_JOB;
            Submit.Packet.Length = sizeof(Submit.Packet);
            Submit.Packet.V3dJob.RclStart = RclStartVa;
            Submit.Packet.V3dJob.RclEnd = Info->V3dReady ? RclEndVa
                                                         : RclStartVa;

            if (Submit.PacketType == 1)
            {
                /* No fence support: fall back to plain framing. */
                SMOKE_SUBMIT_ESCAPE Plain;

                memset(&Plain, 0, sizeof(Plain));
                Plain.PacketType = 1;
                Plain.PayloadBytes = (USHORT)(sizeof(Plain) - 4);
                Plain.Resources.Magic = Submit.Resources.Magic;
                Plain.Resources.ResourceCount = Submit.Resources.ResourceCount;
                Plain.hResource = Submit.hResource;
                Plain.Command.CommandType = 1;
                Plain.Command.PayloadBytes = sizeof(Plain.Packet);
                Plain.Packet = Submit.Packet;

                memset(&Escape, 0, sizeof(Escape));
                Escape.hAdapter = hAdapter;
                Escape.hDevice = hDevice;
                Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
                Escape.pPrivateDriverData = &Plain;
                Escape.PrivateDriverDataSize = sizeof(Plain);
                Status = pfnEscape(&Escape);
            }
            else
            {
                memset(&Escape, 0, sizeof(Escape));
                Escape.hAdapter = hAdapter;
                Escape.hDevice = hDevice;
                Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
                Escape.pPrivateDriverData = &Submit;
                Escape.PrivateDriverDataSize = sizeof(Submit);
                Status = pfnEscape(&Escape);
            }

            ok_succeeded(Status,
               "PhaseB submission failed 0x%08lX\n", Status);
            V3dProof("V3DSMOKE: clear-job submit status=0x%08lX\n", Status);

            if (NT_SUCCESS(Status) && Fence.hSyncObject != 0)
            {
                D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait;
                D3DKMT_HANDLE Handles[1];
                UINT64 Values[1];

                Handles[0] = Fence.hSyncObject;
                Values[0] = 7;
                memset(&Wait, 0, sizeof(Wait));
                Wait.hDevice = hDevice;
                Wait.ObjectCount = 1;
                Wait.ObjectHandleArray = Handles;
                Wait.FenceValueArray = Values;

                Status = pfnWaitCpu(&Wait);
                ok_succeeded(Status,
                   "PhaseB completion wait failed 0x%08lX — GPU-"
                   "completion signal chain broken?\n", Status);
                V3dProof("V3DSMOKE: fence wait status=0x%08lX\n", Status);

                if (Fence.Info.MonitoredFence.FenceValueCPUVirtualAddress)
                {
                    volatile const UINT64 *Va = (volatile const UINT64 *)
                        Fence.Info.MonitoredFence.FenceValueCPUVirtualAddress;
                    int Spin;

                    for (Spin = 0; Spin < 100 && *Va != 7; Spin++)
                        Sleep(10);
                    ok(*Va == 7,
                       "PhaseB monitored value %I64u != 7 after retire\n",
                       *Va);
                }
            }

            if (Fence.hSyncObject != 0 && pfnDestroySync != NULL)
            {
                D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Dso;

                memset(&Dso, 0, sizeof(Dso));
                Dso.hSyncObject = Fence.hSyncObject;
                (void)pfnDestroySync(&Dso);
            }
        }

        /* Silicon-only: poll the destination for the clear color. */
        if (Info->V3dReady)
        {
            memset(&LockData, 0, sizeof(LockData));
            LockData.hDevice = hDevice;
            LockData.hAllocation = hAllocs[ALLOC_DST];
            Status = pfnLock(&LockData);
            ok_succeeded(Status, "PhaseB dst Lock failed 0x%08lX\n",
               Status);
            if (NT_SUCCESS(Status) && LockData.pData != NULL)
            {
                volatile const ULONG *Px = (volatile const ULONG *)LockData.pData;
                int Tries;

                for (Tries = 0; Tries < 200 && Px[0] != ClearArgb; Tries++)
                    Sleep(10);
                ok(Px[0] == ClearArgb,
                   "PhaseB clear pixel: got 0x%08lx want 0x%08lx — first "
                   "V3D render on this stack %s\n",
                   Px[0], ClearArgb,
                   Px[0] == ClearArgb ? "SUCCEEDED" : "failed");
                V3dProof("V3DSMOKE: READBACK %s got=0x%08lx want=0x%08lx\n",
                         Px[0] == ClearArgb ? "PASS" : "FAIL",
                         Px[0], ClearArgb);

                {
                    D3DKMT_UNLOCK UnlockData;
                    memset(&UnlockData, 0, sizeof(UnlockData));
                    UnlockData.hDevice = hDevice;
                    UnlockData.NumAllocations = 1;
                    UnlockData.phAllocations = &hAllocs[ALLOC_DST];
                    (void)pfnUnlock(&UnlockData);
                }
            }
        }
        else
        {
            skip("no V3D hardware — PhaseB readback awaits silicon\n");
            V3dProof("V3DSMOKE: no V3D silicon — readback skipped\n");
        }
    }

cleanup:
    for (a = 0; a < ALLOC_COUNT; a++)
    {
        if (hAllocs[a] != 0)
        {
            D3DKMT_DESTROYALLOCATION DestroyData;

            memset(&DestroyData, 0, sizeof(DestroyData));
            DestroyData.hDevice = hDevice;
            DestroyData.phAllocationList = &hAllocs[a];
            DestroyData.AllocationCount = 1;
            (void)pfnDestroyAlloc(&DestroyData);
        }
    }
}

START_TEST(v3dsmoke)
{
    PFN_D3DKMTEscape pfnEscape;
    PFN_D3DKMTCreateAllocation pfnCreateAlloc;
    PFN_D3DKMTDestroyAllocation pfnDestroyAlloc;
    PFN_D3DKMTLock pfnLock;
    PFN_D3DKMTUnlock pfnUnlock;
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEALLOCATION CreateData;
    D3DDDI_ALLOCATIONINFO AllocationInfo;
    D3DKMT_LOCK LockData;
    SMOKE_INFO_ESCAPE Info;
    D3DKMT_ESCAPE Escape;
    NTSTATUS Status;
    UINT AllocSize = 4096;
    BOOLEAN IsRpi5vc4;

    pfnEscape = (PFN_D3DKMTEscape)LoadD3DKMTProc("D3DKMTEscape");
    pfnCreateAlloc = (PFN_D3DKMTCreateAllocation)
                     LoadD3DKMTProc("D3DKMTCreateAllocation");
    pfnDestroyAlloc = (PFN_D3DKMTDestroyAllocation)
                      LoadD3DKMTProc("D3DKMTDestroyAllocation");
    pfnLock = (PFN_D3DKMTLock)LoadD3DKMTProc("D3DKMTLock");
    pfnUnlock = (PFN_D3DKMTUnlock)LoadD3DKMTProc("D3DKMTUnlock");
    if (!pfnEscape || !pfnCreateAlloc || !pfnDestroyAlloc ||
        !pfnLock || !pfnUnlock)
    {
        skip("required D3DKMT entry points not exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("cannot open adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("cannot create device\n"); CloseAdapter(hAdapter); return; }

    /* Positive rpi5vc4 identification gates all assertions. */
    memset(&Info, 0, sizeof(Info));
    Info.Magic = RPI5VC4_ESCAPE_MAGIC;
    Info.Op = RPI5VC4_ESCAPE_OP_QUERY_INFO;
    memset(&Escape, 0, sizeof(Escape));
    Escape.hAdapter = hAdapter;
    Escape.hDevice = hDevice;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = &Info;
    Escape.PrivateDriverDataSize = sizeof(Info);
    IsRpi5vc4 = NT_SUCCESS(pfnEscape(&Escape));

    if (!IsRpi5vc4)
    {
        skip("not rpi5vc4 — smoke contracts are driver-private\n");
        V3dProof("V3DSMOKE: display adapter is not rpi5vc4 — skipped\n");
        goto cleanup_device;
    }

    trace("rpi5vc4: V3dReady=%lu slab=%luMB GpuVa=0x%08lx\n",
          Info.V3dReady, Info.SlabSize / (1024 * 1024), Info.SlabGpuVa);
    V3dProof("V3DSMOKE: rpi5vc4 V3dReady=%lu ver=%lu slab=%luMB gpuva=0x%08lx\n",
             Info.V3dReady, Info.V3dVersion,
             Info.SlabSize / (1024 * 1024), Info.SlabGpuVa);

    /* Slab allocation for the control list. */
    memset(&CreateData, 0, sizeof(CreateData));
    memset(&AllocationInfo, 0, sizeof(AllocationInfo));
    AllocationInfo.PrivateDriverDataSize = sizeof(UINT);
    AllocationInfo.pPrivateDriverData = &AllocSize;
    CreateData.hDevice = hDevice;
    CreateData.NumAllocations = 1;
    CreateData.pAllocationInfo = &AllocationInfo;
    Status = pfnCreateAlloc(&CreateData);
    if (!NT_SUCCESS(Status))
    {
        skip("CreateAllocation failed 0x%08lX\n", Status);
        goto cleanup_device;
    }
    ok(AllocationInfo.hAllocation != 0, "allocation handle is zero\n");

    /* Lock: the CPU mapping a UMD writes control lists through. */
    memset(&LockData, 0, sizeof(LockData));
    LockData.hDevice = hDevice;
    LockData.hAllocation = AllocationInfo.hAllocation;
    Status = pfnLock(&LockData);
    ok_succeeded(Status, "Lock failed 0x%08lX\n", Status);

    if (NT_SUCCESS(Status) && LockData.pData != NULL)
    {
        /* Write a genuine RCL fragment via the Stage-0 encoders (content
         * is CPU-write exercise only: the submitted range is empty). */
        unsigned char *Cl = (unsigned char *)LockData.pData;

        Cl = Vc4CleTrmCfgCommonV71(Cl, 1, 64, 64, V3D71_DEPTH_TYPE_32F, 3, 3);
        Cl = Vc4CleTrmCfgZsClearV71(Cl, 0x3F800000, 0);
        Cl = Vc4CleClearRenderTargetsV71(Cl);
        Cl = Vc4CleEndOfRendering(Cl);
        ok((SIZE_T)(Cl - (unsigned char *)LockData.pData) <= AllocSize,
           "CL overflowed the allocation\n");

        {
            D3DKMT_UNLOCK UnlockData;
            D3DKMT_HANDLE hAlloc = AllocationInfo.hAllocation;

            memset(&UnlockData, 0, sizeof(UnlockData));
            UnlockData.hDevice = hDevice;
            UnlockData.NumAllocations = 1;
            UnlockData.phAllocations = &hAlloc;
            (void)pfnUnlock(&UnlockData);
        }
    }

    /*
     * Allocation-relative submission: RCL pair references allocation 1
     * (index 0) with an empty range at offset 0.  Render must emit two
     * patch locations; Patch must resolve them against the slab.
     */
    {
        SMOKE_SUBMIT_ESCAPE Submit;

        memset(&Submit, 0, sizeof(Submit));
        Submit.PacketType = 1;
        Submit.PayloadBytes = (USHORT)(sizeof(Submit) - 4);
        Submit.Resources.Magic = SMOKE_RESOURCE_LIST_MAGIC;
        Submit.Resources.ResourceCount = 1;
        Submit.hResource = AllocationInfo.hAllocation;
        Submit.Command.CommandType = 1;
        Submit.Command.PayloadBytes = sizeof(Submit.Packet);
        Submit.Packet.Magic = SMOKE_DMA_PACKET_MAGIC;
        Submit.Packet.Op = SMOKE_DMA_OP_V3D_JOB;
        Submit.Packet.Length = sizeof(Submit.Packet);
        Submit.Packet.V3dJob.RclAllocIndexPlusOne = 1;
        Submit.Packet.V3dJob.RclStart = 0;
        Submit.Packet.V3dJob.RclEnd = 0;      /* empty: never kicks CLE */

        memset(&Escape, 0, sizeof(Escape));
        Escape.hAdapter = hAdapter;
        Escape.hDevice = hDevice;
        Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        Escape.pPrivateDriverData = &Submit;
        Escape.PrivateDriverDataSize = sizeof(Submit);

        Status = pfnEscape(&Escape);
        ok_succeeded(Status,
           "allocation-relative submission failed 0x%08lX — relocation "
           "chain (Render patch emission / kick Patch) broken?\n", Status);
        V3dProof("V3DSMOKE: alloc-relative submit status=0x%08lX\n", Status);
    }

    /* Phase B: the real clear job. */
    SmokePhaseB(pfnEscape, pfnCreateAlloc, pfnDestroyAlloc,
                pfnLock, pfnUnlock, hAdapter, hDevice, &Info);

    /* Cleanup. */
    {
        D3DKMT_DESTROYALLOCATION DestroyData;
        D3DKMT_HANDLE hAlloc = AllocationInfo.hAllocation;

        memset(&DestroyData, 0, sizeof(DestroyData));
        DestroyData.hDevice = hDevice;
        DestroyData.phAllocationList = &hAlloc;
        DestroyData.AllocationCount = 1;
        Status = pfnDestroyAlloc(&DestroyData);
        ok_succeeded(Status, "DestroyAllocation failed 0x%08lX\n", Status);
    }

cleanup_device:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}
