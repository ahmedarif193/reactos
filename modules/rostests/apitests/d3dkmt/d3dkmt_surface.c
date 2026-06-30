/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Breadth coverage of the remaining D3DKMT surface (contract checks)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Each group validates the portable contract (NULL is refused) for a family of
 * D3DKMT entry points, so the suite exercises the full documented surface and
 * passes on Windows 11 regardless of whether the host adapter is render-capable.
 * Positive paths for these (overlays, keyed mutexes, GPU-VA contexts, MPO) need
 * real render resources and are covered/skipped in their dedicated subsystems.
 *
 * Reference: Microsoft Direct3D DDI thunk reference (d3dkmthk).
 */

#include "precomp.h"

/* ---- Virtual context + context scheduling (WDDM 2.0 / 1.x) ---- */
START_TEST(context2)
{
    CHECK_NULL_REJECTED(PFND3DKMT_CREATECONTEXTVIRTUAL, "D3DKMTCreateContextVirtual");
    CHECK_NULL_REJECTED(PFND3DKMT_SETCONTEXTSCHEDULINGPRIORITY, "D3DKMTSetContextSchedulingPriority");
    CHECK_NULL_REJECTED(PFND3DKMT_GETCONTEXTSCHEDULINGPRIORITY, "D3DKMTGetContextSchedulingPriority");
    CHECK_NULL_REJECTED(PFND3DKMT_SETCONTEXTINPROCESSSCHEDULINGPRIORITY, "D3DKMTSetContextInProcessSchedulingPriority");
    CHECK_NULL_REJECTED(PFND3DKMT_GETCONTEXTINPROCESSSCHEDULINGPRIORITY, "D3DKMTGetContextInProcessSchedulingPriority");
}

/* ---- Allocation v2 / Lock2 (WDDM 2.0) ---- */
START_TEST(alloc2)
{
    CHECK_NULL_REJECTED(PFND3DKMT_CREATEALLOCATION2, "D3DKMTCreateAllocation2");
    CHECK_NULL_REJECTED(PFND3DKMT_DESTROYALLOCATION2, "D3DKMTDestroyAllocation2");
    CHECK_NULL_REJECTED(PFND3DKMT_LOCK2, "D3DKMTLock2");
    CHECK_NULL_REJECTED(PFND3DKMT_UNLOCK2, "D3DKMTUnlock2");
}

/* ---- Keyed mutexes (WDDM 1.1 / 1.2) ---- */
START_TEST(keyedmutex)
{
    CHECK_NULL_REJECTED(PFND3DKMT_CREATEKEYEDMUTEX, "D3DKMTCreateKeyedMutex");
    CHECK_NULL_REJECTED(PFND3DKMT_CREATEKEYEDMUTEX2, "D3DKMTCreateKeyedMutex2");
    CHECK_NULL_REJECTED(PFND3DKMT_OPENKEYEDMUTEX, "D3DKMTOpenKeyedMutex");
    CHECK_NULL_REJECTED(PFND3DKMT_OPENKEYEDMUTEX2, "D3DKMTOpenKeyedMutex2");
}

/* ---- Overlays and multi-plane overlay (WDDM 1.0 / 1.2+) ---- */
START_TEST(overlay)
{
    CHECK_NULL_REJECTED(PFND3DKMT_CREATEOVERLAY, "D3DKMTCreateOverlay");
    CHECK_NULL_REJECTED(PFND3DKMT_UPDATEOVERLAY, "D3DKMTUpdateOverlay");
    CHECK_NULL_REJECTED(PFND3DKMT_FLIPOVERLAY, "D3DKMTFlipOverlay");
    CHECK_NULL_REJECTED(PFND3DKMT_DESTROYOVERLAY, "D3DKMTDestroyOverlay");
    CHECK_NULL_REJECTED(PFND3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT, "D3DKMTCheckMultiPlaneOverlaySupport");
    CHECK_NULL_REJECTED(PFND3DKMT_PRESENTMULTIPLANEOVERLAY, "D3DKMTPresentMultiPlaneOverlay");
}

/* ---- Power / occlusion / stable power state ---- */
START_TEST(power)
{
    CHECK_NULL_REJECTED(PFND3DKMT_CHECKMONITORPOWERSTATE, "D3DKMTCheckMonitorPowerState");
    CHECK_NULL_REJECTED(PFND3DKMT_CHECKOCCLUSION, "D3DKMTCheckOcclusion");
    CHECK_NULL_REJECTED(PFND3DKMT_SETSTABLEPOWERSTATE, "D3DKMTSetStablePowerState");
}
