/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     vc4kmt UMD winsys smoke: route v3dsmoke Phase A through the
 *              reusable VC4 D3DKMT wrapper.
 */

#include "precomp.h"
#include <reactos/vc4kmt.h>
#include <reactos/vc4cle.h>

START_TEST(vc4kmt_smoke)
{
    VC4KMT_DEVICE *Device = NULL;
    VC4KMT_BO ClBo;
    VC4KMT_CL_SUBMIT Submit;
    VC4KMT_FENCE Fence;
    const RPI5VC4_ESCAPE_INFO *Info;
    PVOID CpuVa;
    NTSTATUS Status;

    RtlZeroMemory(&ClBo, sizeof(ClBo));
    RtlZeroMemory(&Submit, sizeof(Submit));
    RtlZeroMemory(&Fence, sizeof(Fence));

    Status = vc4kmt_open(&Device);
    if (!NT_SUCCESS(Status))
    {
        skip("vc4kmt_open failed 0x%08lX (not rpi5vc4 or ABI/caps mismatch)\n",
             Status);
        return;
    }

    Info = vc4kmt_info(Device);
    ok(Info != NULL, "vc4kmt_info returned NULL\n");
    if (Info != NULL)
    {
        trace("vc4kmt: V3dReady=%lu Abi=%lu Caps=0x%08lx slab=%luMB\n",
              Info->V3dReady, Info->AbiVersion, Info->Caps,
              Info->SlabSize / (1024 * 1024));
    }

    Status = vc4kmt_bo_create(Device, 4096, &ClBo);
    ok(NT_SUCCESS(Status), "vc4kmt_bo_create failed 0x%08lX\n", Status);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    ok(ClBo.hAllocation != 0, "CL allocation handle is zero\n");
    ok(vc4kmt_bo_gpuva(&ClBo) != 0, "CL GPU VA is zero\n");

    Status = vc4kmt_bo_map(Device, &ClBo, &CpuVa);
    ok(NT_SUCCESS(Status), "vc4kmt_bo_map failed 0x%08lX\n", Status);
    if (!NT_SUCCESS(Status) || CpuVa == NULL)
        goto cleanup;

    {
        unsigned char *Base = (unsigned char *)CpuVa;
        unsigned char *Cl = Base;

        Cl = Vc4CleTrmCfgCommonV71(Cl, 1, 64, 64, V3D71_DEPTH_TYPE_32F, 3, 3);
        Cl = Vc4CleTrmCfgZsClearV71(Cl, 0x3F800000, 0);
        Cl = Vc4CleClearRenderTargetsV71(Cl);
        Cl = Vc4CleEndOfRendering(Cl);
        ok((SIZE_T)(Cl - Base) <= ClBo.Size, "CL overflowed the allocation\n");
    }

    memset(&Submit, 0, sizeof(Submit));
    /* Empty RCL (Start==End) — exercises the transport without kicking the
     * CLE; addresses are now absolute GPU VAs. */
    Submit.RclStart = vc4kmt_bo_gpuva(&ClBo);
    Submit.RclEnd = vc4kmt_bo_gpuva(&ClBo);

    Status = vc4kmt_submit_cl(Device, &Submit, &Fence);
    ok(NT_SUCCESS(Status),
       "vc4kmt_submit_cl failed 0x%08lX -- allocation-relative transport broken?\n",
       Status);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    Status = vc4kmt_wait(Device, &Fence, INFINITE);
    ok(NT_SUCCESS(Status), "vc4kmt_wait failed 0x%08lX\n", Status);
    if (Fence.CpuValue != NULL)
    {
        ok(*Fence.CpuValue >= Fence.Value,
           "monitored fence value %I64u below target %I64u\n",
           *Fence.CpuValue, Fence.Value);
    }

cleanup:
    vc4kmt_fence_destroy(Device, &Fence);
    if (ClBo.hAllocation != 0)
        (void)vc4kmt_bo_destroy(Device, &ClBo);
    vc4kmt_close(Device);
}
