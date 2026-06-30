/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Extended present / overlay / multi-plane-overlay (MPO) coverage
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Goes deeper/wider than the existing present (present_test.c, d3dkmt_present.c)
 * and overlay NULL-contract (d3dkmt_surface.c) suites without duplicating them:
 *
 *   - D3DKMTPresent       : bad-handle x flag matrix (negative) + skip-guarded
 *                           Flip / ColorFill / dirty-BLT flag variants.
 *   - D3DKMTRender        : bad-context flag variants (deeper than the single
 *                           NULL/bad-context already covered).
 *   - MPO present         : PresentMultiPlaneOverlay bad-handle,
 *                           PresentMultiPlaneOverlay2 NULL + bad-handle,
 *                           PresentMultiPlaneOverlay3 NULL (header-gated struct).
 *   - MPO support query   : CheckMultiPlaneOverlaySupport bad-handle + query,
 *                           CheckMultiPlaneOverlaySupport2 NULL + bad-handle +
 *                           query, CheckMultiPlaneOverlaySupport3 NULL,
 *                           GetMultiPlaneOverlayCaps NULL (header-gated struct).
 *   - Overlay lifecycle   : skip-guarded Create/Get/Update/Flip/Destroy plus
 *                           bad-handle contract for the whole family and the
 *                           GetOverlayState NULL contract (not previously tested).
 *   - Present queue / sync: GetPresentQueueEvent NULL + skip-guarded,
 *                           CancelPresents NULL, SetSyncRefreshCountWaitTarget
 *                           NULL + bad-handle, GetDWMVerticalBlankEvent NULL +
 *                           bad-handle.
 *
 * WIN11-GREEN rules: NULL / bad-handle / param tests assert refusal only (a
 * non-success NTSTATUS, or a fault in the user-mode thunk -- both correct).
 * Positive present/overlay/MPO paths need a real render device + allocation +
 * hardware overlay support that a display-only / QEMU adapter lacks, so they are
 * attempted and skip()'d on failure, never asserted as success. AV-prone calls
 * are SEH-guarded. Values are trace()'d; ok() is reserved for invariants.
 *
 * Compiled at DXGKDDI_INTERFACE_VERSION 0x5023 (WDDM 2.0): the v3 MPO structs,
 * GetMultiPlaneOverlayCaps and CancelPresents structs are header-gated above
 * 2.0, so those functions are exercised through a generic single-pointer thunk
 * signature for their NULL contract only (the export still exists on Win11).
 *
 * Reference: Microsoft Direct3D DDI thunk reference (d3dkmthk).
 */

#include "precomp.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Generic single-pointer D3DKMT thunk signature. Used to drive the NULL
 * contract for entry points whose request struct is header-gated above the
 * 0x5023 (WDDM 2.0) compile version, so they cannot be typed precisely here but
 * still exist as gdi32 exports on Windows 11. Passing NULL is ABI-identical
 * regardless of the real pointee type.
 */
typedef NTSTATUS (APIENTRY *PFN_D3DKMT_GEN)(const void *);

/* D3DKMTGetPresentQueueEvent is declared as a 2-arg EXTERN_C (no PFND3DKMT_*). */
typedef NTSTATUS (APIENTRY *PFN_GETPRESENTQUEUEEVENT)(D3DKMT_HANDLE, HANDLE *);

/* Run CallExpr under SEH; record the NTSTATUS into St and a fault into Flt. */
#define SEH_CALL(St, Flt, CallExpr)                                  \
do {                                                                 \
    (St) = STATUS_SUCCESS;                                           \
    (Flt) = FALSE;                                                   \
    _SEH2_TRY { (St) = (CallExpr); }                                 \
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { (Flt) = TRUE; }        \
    _SEH2_END;                                                       \
} while (0)

/*
 * Portable refusal contract for a fully-formed but invalid request (e.g. a bad
 * handle): Windows either returns a non-success NTSTATUS or faults in the thunk.
 * Only a success status is the bug.
 */
#define EXPECT_REFUSED(Name, CallExpr)                                          \
do {                                                                            \
    NTSTATUS _st;                                                               \
    BOOL _f;                                                                    \
    SEH_CALL(_st, _f, CallExpr);                                               \
    ok(_f || !NT_SUCCESS(_st),                                                  \
       Name " must be refused (error NTSTATUS or fault), got 0x%08lX%s\n",     \
       (long)_st, _f ? " (faulted)" : "");                                      \
    if (!_f && NT_SUCCESS(_st))                                                 \
        trace(Name " UNEXPECTEDLY succeeded (0x%08lX)\n", (long)_st);          \
} while (0)

/* ---- Local allocation helpers (single surface, miniport private data) ---- */
static D3DKMT_HANDLE
MakeSurface(PFN_D3DKMTCreateAllocation pfnCreate, D3DKMT_HANDLE hDevice,
            UINT Width, UINT Height)
{
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    struct { UINT Width; UINT Height; UINT Bpp; } priv;

    priv.Width = Width;
    priv.Height = Height;
    priv.Bpp = 32;

    memset(&ai, 0, sizeof(ai));
    ai.pPrivateDriverData = &priv;
    ai.PrivateDriverDataSize = sizeof(priv);

    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;

    if (!NT_SUCCESS(pfnCreate(&ca)))
        return 0;
    return ai.hAllocation;
}

static void
FreeSurface(PFN_D3DKMTDestroyAllocation pfnDestroy, D3DKMT_HANDLE hDevice,
            D3DKMT_HANDLE hAlloc)
{
    D3DKMT_DESTROYALLOCATION da;

    if (!hAlloc)
        return;
    memset(&da, 0, sizeof(da));
    da.hDevice = hDevice;
    da.phAllocationList = &hAlloc;
    da.AllocationCount = 1;
    pfnDestroy(&da);
}

/* =====================================================================
 *  D3DKMTPresent -- bad handle across a flag matrix (negative)
 * ===================================================================== */
static void Test_Present_BadDevice_FlagMatrix(void)
{
    static const struct { UINT Flags; const char *Name; } variants[] =
    {
        { 0x00000001, "Blt" },
        { 0x00000002, "ColorFill" },
        { 0x00000004, "Flip" },
        { 0x00000014, "Flip|FlipDoNotWait" },
        { 0x00000100, "RestrictVidPnSource" },
        { 0x000000C0, "DstRectValid|SrcRectValid" },
        { 0x00001000, "PresentCountValid" },
    };
    D3DKMT_PRESENT pres;
    ULONG i;

    LOADFN(PFND3DKMT_PRESENT, pfn, "D3DKMTPresent");

    for (i = 0; i < ARRAY_LEN(variants); ++i)
    {
        NTSTATUS st;
        BOOL f;
        memset(&pres, 0, sizeof(pres));
        pres.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
        pres.Flags.Value = variants[i].Flags;
        pres.SrcRect.right = 640;
        pres.SrcRect.bottom = 480;
        pres.DstRect = pres.SrcRect;
        SEH_CALL(st, f, pfn(&pres));
        ok(f || !NT_SUCCESS(st),
           "Present(bad device, flags=%s) must be refused, got 0x%08lX%s\n",
           variants[i].Name, (long)st, f ? " (faulted)" : "");
    }
}

/* =====================================================================
 *  D3DKMTPresent -- skip-guarded positive flag variants on a real device
 * ===================================================================== */
static void Test_Present_SkipGuarded_Variants(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hSrc, hDst;
    D3DKMT_PRESENT pres;
    RECT sub;
    NTSTATUS st;
    BOOL f;

    LOADFN(PFND3DKMT_PRESENT, pfnPresent, "D3DKMTPresent");
    LOADFN(PFN_D3DKMTCreateAllocation, pfnCreate, "D3DKMTCreateAllocation");
    LOADFN(PFN_D3DKMTDestroyAllocation, pfnDestroy, "D3DKMTDestroyAllocation");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter for present variants\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed for present variants\n"); CloseAdapter(hAdapter); return; }

    hSrc = MakeSurface(pfnCreate, hDevice, 640, 480);
    hDst = MakeSurface(pfnCreate, hDevice, 640, 480);
    if (!hSrc)
    {
        skip("CreateAllocation failed for present variants\n");
        FreeSurface(pfnDestroy, hDevice, hDst);
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    /* Variant A: flip present (no real flip chain on a basic adapter). */
    memset(&pres, 0, sizeof(pres));
    pres.hDevice = hDevice;
    pres.hSource = hSrc;
    pres.Flags.Flip = 1;
    pres.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
    SEH_CALL(st, f, pfnPresent(&pres));
    if (f || !NT_SUCCESS(st))
        skip("Flip present unsupported on this adapter (0x%08lX%s)\n", (long)st, f ? " fault" : "");
    else
        trace("Flip present accepted (0x%08lX)\n", (long)st);

    /* Variant B: color-fill present. */
    memset(&pres, 0, sizeof(pres));
    pres.hDevice = hDevice;
    pres.Color = 0xFFFF0000; /* opaque red, ARGB */
    pres.Flags.ColorFill = 1;
    pres.Flags.DstRectValid = 1;
    pres.DstRect.right = 640;
    pres.DstRect.bottom = 480;
    SEH_CALL(st, f, pfnPresent(&pres));
    if (f || !NT_SUCCESS(st))
        skip("ColorFill present unsupported on this adapter (0x%08lX%s)\n", (long)st, f ? " fault" : "");
    else
        trace("ColorFill present accepted (0x%08lX)\n", (long)st);

    /* Variant C: BLT present with a dirty sub-rect. */
    if (hDst)
    {
        sub.left = 0; sub.top = 0; sub.right = 320; sub.bottom = 240;
        memset(&pres, 0, sizeof(pres));
        pres.hDevice = hDevice;
        pres.hSource = hSrc;
        pres.hDestination = hDst;
        pres.Flags.Blt = 1;
        pres.Flags.SrcRectValid = 1;
        pres.Flags.DstRectValid = 1;
        pres.SrcRect.right = 640; pres.SrcRect.bottom = 480;
        pres.DstRect = pres.SrcRect;
        pres.SubRectCnt = 1;
        pres.pSrcSubRects = &sub;
        SEH_CALL(st, f, pfnPresent(&pres));
        if (f || !NT_SUCCESS(st))
            skip("Dirty-BLT present unsupported on this adapter (0x%08lX%s)\n", (long)st, f ? " fault" : "");
        else
            trace("Dirty-BLT present accepted (0x%08lX)\n", (long)st);
    }

    FreeSurface(pfnDestroy, hDevice, hDst);
    FreeSurface(pfnDestroy, hDevice, hSrc);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* =====================================================================
 *  D3DKMTRender -- bad-context flag variants (deeper than NULL/bad-context)
 * ===================================================================== */
static void Test_Render_BadContext_Variants(void)
{
    D3DKMT_RENDER rnd;

    LOADFN(PFND3DKMT_RENDER, pfn, "D3DKMTRender");

    /* Plain bad context with a non-zero command length. */
    memset(&rnd, 0, sizeof(rnd));
    rnd.hContext = (D3DKMT_HANDLE)0xBAD0CAFE;
    rnd.CommandLength = 64;
    EXPECT_REFUSED("Render(bad context, CommandLength=64)", pfn(&rnd));

    /* Request a resized command buffer on a bad context. */
    memset(&rnd, 0, sizeof(rnd));
    rnd.hContext = (D3DKMT_HANDLE)0xBAD0CAFE;
    rnd.Flags.ResizeCommandBuffer = 1;
    rnd.NewCommandBufferSize = 4096;
    EXPECT_REFUSED("Render(bad context, ResizeCommandBuffer)", pfn(&rnd));

    /* Null-rendering flag on a bad context. */
    memset(&rnd, 0, sizeof(rnd));
    rnd.hContext = (D3DKMT_HANDLE)0xBAD0CAFE;
    rnd.Flags.NullRendering = 1;
    EXPECT_REFUSED("Render(bad context, NullRendering)", pfn(&rnd));
}

/* =====================================================================
 *  PresentMultiPlaneOverlay (v1) -- bad handle (NULL is in d3dkmt_surface.c)
 * ===================================================================== */
static void Test_PresentMPO1_BadHandle(void)
{
    D3DKMT_PRESENT_MULTIPLANE_OVERLAY mpo;

    LOADFN(PFND3DKMT_PRESENTMULTIPLANEOVERLAY, pfn, "D3DKMTPresentMultiPlaneOverlay");

    memset(&mpo, 0, sizeof(mpo));
    mpo.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
    mpo.VidPnSourceId = 0;
    mpo.PresentPlaneCount = 0;
    EXPECT_REFUSED("PresentMultiPlaneOverlay(bad handle)", pfn(&mpo));
}

/* =====================================================================
 *  PresentMultiPlaneOverlay2 -- NULL + bad handle
 * ===================================================================== */
static void Test_PresentMPO2_Null_BadHandle(void)
{
    D3DKMT_PRESENT_MULTIPLANE_OVERLAY2 mpo;

    LOADFN(PFND3DKMT_PRESENTMULTIPLANEOVERLAY2, pfn, "D3DKMTPresentMultiPlaneOverlay2");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTPresentMultiPlaneOverlay2");

    memset(&mpo, 0, sizeof(mpo));
    mpo.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    mpo.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
    mpo.PresentPlaneCount = 0;
    EXPECT_REFUSED("PresentMultiPlaneOverlay2(bad handle)", pfn(&mpo));
}

/* =====================================================================
 *  CheckMultiPlaneOverlaySupport (v1) -- bad handle + skip-guarded query
 *  (NULL contract lives in d3dkmt_surface.c)
 * ===================================================================== */
static void Test_CheckMPOSupport1_BadHandle_Query(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT chk;
    NTSTATUS st;
    BOOL f;

    LOADFN(PFND3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT, pfn, "D3DKMTCheckMultiPlaneOverlaySupport");

    /* Bad handle. */
    memset(&chk, 0, sizeof(chk));
    chk.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
    chk.PlaneCount = 0;
    EXPECT_REFUSED("CheckMultiPlaneOverlaySupport(bad handle)", pfn(&chk));

    /* Skip-guarded query against a real device. */
    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter for CheckMPOSupport query\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed for CheckMPOSupport query\n"); CloseAdapter(hAdapter); return; }

    memset(&chk, 0, sizeof(chk));
    chk.hDevice = hDevice;
    chk.PlaneCount = 0;
    SEH_CALL(st, f, pfn(&chk));
    if (f || !NT_SUCCESS(st))
        skip("CheckMultiPlaneOverlaySupport query unsupported (0x%08lX%s)\n", (long)st, f ? " fault" : "");
    else
        trace("CheckMultiPlaneOverlaySupport: Supported=%d\n", chk.Supported);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* =====================================================================
 *  CheckMultiPlaneOverlaySupport2 -- NULL + bad handle + skip-guarded query
 * ===================================================================== */
static void Test_CheckMPOSupport2_Null_BadHandle_Query(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT2 chk;
    NTSTATUS st;
    BOOL f;

    LOADFN(PFND3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT2, pfn, "D3DKMTCheckMultiPlaneOverlaySupport2");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTCheckMultiPlaneOverlaySupport2");

    memset(&chk, 0, sizeof(chk));
    chk.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    chk.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
    chk.PlaneCount = 0;
    EXPECT_REFUSED("CheckMultiPlaneOverlaySupport2(bad handle)", pfn(&chk));

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter for CheckMPOSupport2 query\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed for CheckMPOSupport2 query\n"); CloseAdapter(hAdapter); return; }

    memset(&chk, 0, sizeof(chk));
    chk.hAdapter = hAdapter;
    chk.hDevice = hDevice;
    chk.PlaneCount = 0;
    SEH_CALL(st, f, pfn(&chk));
    if (f || !NT_SUCCESS(st))
        skip("CheckMultiPlaneOverlaySupport2 query unsupported (0x%08lX%s)\n", (long)st, f ? " fault" : "");
    else
        trace("CheckMultiPlaneOverlaySupport2: Supported=%d\n", chk.Supported);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* =====================================================================
 *  Header-gated-at-0x5023 entry points -- NULL contract via generic thunk.
 *  Their request structs require WDDM >= 2.1/2.2/3.0 headers, so only the
 *  export's NULL refusal is portable here. The export still ships on Win11.
 * ===================================================================== */
static void Test_GatedFunctions_NullContract(void)
{
    CHECK_NULL_REJECTED(PFN_D3DKMT_GEN, "D3DKMTPresentMultiPlaneOverlay3");
    CHECK_NULL_REJECTED(PFN_D3DKMT_GEN, "D3DKMTCheckMultiPlaneOverlaySupport3");
    CHECK_NULL_REJECTED(PFN_D3DKMT_GEN, "D3DKMTGetMultiPlaneOverlayCaps");

    /* The positive caps query needs the gated request struct; recorded as
     * skipped at this compile version rather than asserted. */
    skip("GetMultiPlaneOverlayCaps positive query needs WDDM>=2.2 headers (compiled at 0x5023)\n");
}

/* =====================================================================
 *  Overlay lifecycle -- skip-guarded Create/Get/Update/Flip/Destroy
 * ===================================================================== */
static void Test_Overlay_Lifecycle_SkipGuarded(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc, hOverlay;
    D3DKMT_CREATEOVERLAY co;
    D3DKMT_GETOVERLAYSTATE gos;
    D3DKMT_UPDATEOVERLAY uo;
    D3DKMT_FLIPOVERLAY fo;
    D3DKMT_DESTROYOVERLAY do_;
    NTSTATUS st;
    BOOL f;

    LOADFN(PFND3DKMT_CREATEOVERLAY, pfnCreate, "D3DKMTCreateOverlay");
    LOADFN(PFN_D3DKMTCreateAllocation, pfnAlloc, "D3DKMTCreateAllocation");
    LOADFN(PFN_D3DKMTDestroyAllocation, pfnFree, "D3DKMTDestroyAllocation");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter for overlay lifecycle\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed for overlay lifecycle\n"); CloseAdapter(hAdapter); return; }

    hAlloc = MakeSurface(pfnAlloc, hDevice, 640, 480);
    if (!hAlloc)
    {
        skip("CreateAllocation failed for overlay lifecycle\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    memset(&co, 0, sizeof(co));
    co.VidPnSourceId = 0;
    co.hDevice = hDevice;
    co.OverlayInfo.hAllocation = hAlloc;
    SEH_CALL(st, f, pfnCreate(&co));
    if (f || !NT_SUCCESS(st))
    {
        /* Hardware overlays are not available on a basic / QEMU adapter. */
        skip("CreateOverlay unsupported on this adapter (0x%08lX%s)\n", (long)st, f ? " fault" : "");
        FreeSurface(pfnFree, hDevice, hAlloc);
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    hOverlay = co.hOverlay;
    trace("CreateOverlay succeeded, hOverlay=0x%08lX\n", (long)hOverlay);

    {
        PFND3DKMT_GETOVERLAYSTATE pfnGet =
            (PFND3DKMT_GETOVERLAYSTATE)LoadD3DKMTProc("D3DKMTGetOverlayState");
        if (pfnGet)
        {
            memset(&gos, 0, sizeof(gos));
            gos.hDevice = hDevice;
            gos.hOverlay = hOverlay;
            SEH_CALL(st, f, pfnGet(&gos));
            if (!f && NT_SUCCESS(st))
                trace("GetOverlayState: OverlayEnabled=%d\n", gos.OverlayEnabled);
            else
                trace("GetOverlayState returned 0x%08lX%s\n", (long)st, f ? " (fault)" : "");
        }
    }

    {
        PFND3DKMT_UPDATEOVERLAY pfnUpdate =
            (PFND3DKMT_UPDATEOVERLAY)LoadD3DKMTProc("D3DKMTUpdateOverlay");
        if (pfnUpdate)
        {
            memset(&uo, 0, sizeof(uo));
            uo.hDevice = hDevice;
            uo.hOverlay = hOverlay;
            uo.OverlayInfo.hAllocation = hAlloc;
            SEH_CALL(st, f, pfnUpdate(&uo));
            trace("UpdateOverlay returned 0x%08lX%s\n", (long)st, f ? " (fault)" : "");
        }
    }

    {
        PFND3DKMT_FLIPOVERLAY pfnFlip =
            (PFND3DKMT_FLIPOVERLAY)LoadD3DKMTProc("D3DKMTFlipOverlay");
        if (pfnFlip)
        {
            memset(&fo, 0, sizeof(fo));
            fo.hDevice = hDevice;
            fo.hOverlay = hOverlay;
            fo.hSource = hAlloc;
            SEH_CALL(st, f, pfnFlip(&fo));
            trace("FlipOverlay returned 0x%08lX%s\n", (long)st, f ? " (fault)" : "");
        }
    }

    {
        PFND3DKMT_DESTROYOVERLAY pfnDestroy =
            (PFND3DKMT_DESTROYOVERLAY)LoadD3DKMTProc("D3DKMTDestroyOverlay");
        if (pfnDestroy)
        {
            memset(&do_, 0, sizeof(do_));
            do_.hDevice = hDevice;
            do_.hOverlay = hOverlay;
            SEH_CALL(st, f, pfnDestroy(&do_));
            /* Destroying the overlay we just created should succeed. */
            ok(!f && NT_SUCCESS(st),
               "DestroyOverlay of a created overlay should succeed, got 0x%08lX%s\n",
               (long)st, f ? " (fault)" : "");
        }
    }

    FreeSurface(pfnFree, hDevice, hAlloc);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* =====================================================================
 *  Overlay family -- bad-handle contract for Update / Flip / Destroy
 *  (their NULL contract lives in d3dkmt_surface.c)
 * ===================================================================== */
static void Test_Overlay_BadHandle_Family(void)
{
    {
        D3DKMT_UPDATEOVERLAY uo;
        LOADFN(PFND3DKMT_UPDATEOVERLAY, pfn, "D3DKMTUpdateOverlay");
        memset(&uo, 0, sizeof(uo));
        uo.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
        uo.hOverlay = (D3DKMT_HANDLE)0xBAD0CAFE;
        EXPECT_REFUSED("UpdateOverlay(bad handle)", pfn(&uo));
    }
    {
        D3DKMT_FLIPOVERLAY fo;
        LOADFN(PFND3DKMT_FLIPOVERLAY, pfn, "D3DKMTFlipOverlay");
        memset(&fo, 0, sizeof(fo));
        fo.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
        fo.hOverlay = (D3DKMT_HANDLE)0xBAD0CAFE;
        fo.hSource = (D3DKMT_HANDLE)0xBAD0CAFE;
        EXPECT_REFUSED("FlipOverlay(bad handle)", pfn(&fo));
    }
    {
        D3DKMT_DESTROYOVERLAY dov;
        LOADFN(PFND3DKMT_DESTROYOVERLAY, pfn, "D3DKMTDestroyOverlay");
        memset(&dov, 0, sizeof(dov));
        dov.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
        dov.hOverlay = (D3DKMT_HANDLE)0xBAD0CAFE;
        EXPECT_REFUSED("DestroyOverlay(bad handle)", pfn(&dov));
    }
}

/* =====================================================================
 *  GetOverlayState -- NULL + bad handle (not previously covered)
 * ===================================================================== */
static void Test_GetOverlayState_Null_BadHandle(void)
{
    D3DKMT_GETOVERLAYSTATE gos;

    LOADFN(PFND3DKMT_GETOVERLAYSTATE, pfn, "D3DKMTGetOverlayState");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTGetOverlayState");

    memset(&gos, 0, sizeof(gos));
    gos.hDevice = (D3DKMT_HANDLE)0xBAD0CAFE;
    gos.hOverlay = (D3DKMT_HANDLE)0xBAD0CAFE;
    EXPECT_REFUSED("GetOverlayState(bad handle)", pfn(&gos));
}

/* =====================================================================
 *  GetPresentQueueEvent -- NULL + skip-guarded (2-arg EXTERN_C export)
 * ===================================================================== */
static void Test_GetPresentQueueEvent_Null_SkipGuarded(void)
{
    PFN_GETPRESENTQUEUEEVENT pfn;
    D3DKMT_HANDLE hAdapter;
    HANDLE hEvent;
    NTSTATUS st;
    BOOL f;

    pfn = (PFN_GETPRESENTQUEUEEVENT)LoadD3DKMTProc("D3DKMTGetPresentQueueEvent");
    if (!pfn) { skip("D3DKMTGetPresentQueueEvent not exported by gdi32.dll\n"); return; }

    /* NULL contract: bad adapter + NULL out pointer. */
    EXPECT_REFUSED("GetPresentQueueEvent(0, NULL)", pfn(0, NULL));

    /* Skip-guarded real query. */
    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter for GetPresentQueueEvent query\n"); return; }

    hEvent = NULL;
    SEH_CALL(st, f, pfn(hAdapter, &hEvent));
    if (f || !NT_SUCCESS(st))
        skip("GetPresentQueueEvent query unsupported (0x%08lX%s)\n", (long)st, f ? " fault" : "");
    else
        trace("GetPresentQueueEvent: hEvent=%p\n", hEvent);

    CloseAdapter(hAdapter);
}

/* =====================================================================
 *  CancelPresents -- NULL contract (struct header-gated to WDDM 3.0)
 * ===================================================================== */
static void Test_CancelPresents_Null(void)
{
    CHECK_NULL_REJECTED(PFN_D3DKMT_GEN, "D3DKMTCancelPresents");
}

/* =====================================================================
 *  SetSyncRefreshCountWaitTarget -- NULL + bad handle
 * ===================================================================== */
static void Test_SetSyncRefreshCountWaitTarget_Null_BadHandle(void)
{
    D3DKMT_SETSYNCREFRESHCOUNTWAITTARGET ssr;

    LOADFN(PFND3DKMT_SETSYNCREFRESHCOUNTWAITTARGET, pfn, "D3DKMTSetSyncRefreshCountWaitTarget");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTSetSyncRefreshCountWaitTarget");

    memset(&ssr, 0, sizeof(ssr));
    ssr.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    ssr.VidPnSourceId = 0;
    ssr.TargetSyncRefreshCount = 1;
    EXPECT_REFUSED("SetSyncRefreshCountWaitTarget(bad handle)", pfn(&ssr));
}

/* =====================================================================
 *  GetDWMVerticalBlankEvent -- NULL + bad handle
 * ===================================================================== */
static void Test_GetDWMVerticalBlankEvent_Null_BadHandle(void)
{
    D3DKMT_GETVERTICALBLANKEVENT gvb;
    D3DKMT_PTR_TYPE hEvent = NULL;

    LOADFN(PFND3DKMT_GETDWMVERTICALBLANKEVENT, pfn, "D3DKMTGetDWMVerticalBlankEvent");

    EXPECT_NULL_REJECTED(pfn, "D3DKMTGetDWMVerticalBlankEvent");

    memset(&gvb, 0, sizeof(gvb));
    gvb.hAdapter = (D3DKMT_HANDLE)0xBAD0CAFE;
    gvb.VidPnSourceId = 0;
    gvb.phEvent = &hEvent;
    EXPECT_REFUSED("GetDWMVerticalBlankEvent(bad handle)", pfn(&gvb));
}

START_TEST(presentext)
{
    /* Present / Render negative + skip-guarded positive */
    Test_Present_BadDevice_FlagMatrix();
    Test_Present_SkipGuarded_Variants();
    Test_Render_BadContext_Variants();

    /* Multi-plane overlay present */
    Test_PresentMPO1_BadHandle();
    Test_PresentMPO2_Null_BadHandle();

    /* Multi-plane overlay support / caps queries */
    Test_CheckMPOSupport1_BadHandle_Query();
    Test_CheckMPOSupport2_Null_BadHandle_Query();
    Test_GatedFunctions_NullContract();

    /* Overlay lifecycle + contracts */
    Test_Overlay_Lifecycle_SkipGuarded();
    Test_Overlay_BadHandle_Family();
    Test_GetOverlayState_Null_BadHandle();

    /* Present queue / vblank / sync targets */
    Test_GetPresentQueueEvent_Null_SkipGuarded();
    Test_CancelPresents_Null();
    Test_SetSyncRefreshCountWaitTarget_Null_BadHandle();
    Test_GetDWMVerticalBlankEvent_Null_BadHandle();
}
