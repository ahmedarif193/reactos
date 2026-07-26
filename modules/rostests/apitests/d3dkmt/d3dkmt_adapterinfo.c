/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Exhaustive D3DKMTQueryAdapterInfo selector coverage + adapter
 *              classification.  Every test is written to PASS on a real
 *              Windows 11 ARM64 machine, including a QEMU guest that exposes
 *              only a display-only (MSBDA) and/or WARP software adapter:
 *                - parameter/NULL/too-small cases assert REFUSAL only
 *                  (faulted or !NT_SUCCESS), never a specific NTSTATUS;
 *                - per-selector positive queries trace the key fields and
 *                  assert only invariants that always hold; a selector that is
 *                  unsupported on a display-only/software adapter simply returns
 *                  failure, which is traced (NOT a test failure);
 *                - everything that might AV is wrapped in SEH.
 *              The suite compiles at DXGKDDI_INTERFACE_VERSION=0x5023 (WDDM2_0),
 *              so only selectors/structs visible at that tier are referenced;
 *              higher-tier selectors are wrapped in version #if guards.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests: D3DKMTQueryAdapterInfo (all KMTQAITYPE_* selectors), adapter class.
 */

#include "precomp.h"

#ifndef STATUS_ACCESS_VIOLATION
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xC0000005L)
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)
#endif

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/*
 * Issue a single D3DKMTQueryAdapterInfo, SEH-guarded so a faulting thunk (some
 * Win11 thunks dereference before validating) becomes STATUS_ACCESS_VIOLATION
 * rather than aborting the subtest process.  Returns the resulting NTSTATUS.
 */
static NTSTATUS
QueryAI(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE hAdapter,
        KMTQUERYADAPTERINFOTYPE Type, void *pData, UINT Size)
{
    D3DKMT_QUERYADAPTERINFO q;
    NTSTATUS st = STATUS_INVALID_PARAMETER;

    memset(&q, 0, sizeof(q));
    q.hAdapter = hAdapter;
    q.Type = Type;
    q.pPrivateDriverData = pData;
    q.PrivateDriverDataSize = Size;

    _SEH2_TRY { st = pfn(&q); }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { st = STATUS_ACCESS_VIOLATION; }
    _SEH2_END;

    return st;
}

/*
 * For a fixed-size selector: both a NULL data buffer (with the correct size)
 * and a 1-byte (too-small) buffer must be refused.  "Refused" means any
 * non-success NTSTATUS or a fault; never a specific code.
 */
static void
ExpectRefuseBadBuffers(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE hAdapter,
                       KMTQUERYADAPTERINFOTYPE Type, const char *Name, UINT Size)
{
    NTSTATUS st;
    UCHAR tiny = 0;

    st = QueryAI(pfn, hAdapter, Type, NULL, Size);
    ok_failed(st,
       "%s: NULL data buffer (size=%u) must be refused, got 0x%08lX\n",
       Name, Size, (long)st);

    st = QueryAI(pfn, hAdapter, Type, &tiny, 1);
    ok_failed(st,
       "%s: 1-byte (too-small) buffer must be refused, got 0x%08lX\n",
       Name, (long)st);
}

/*
 * For a variable-size selector (UMD private payload, mode list) the too-small
 * case may legitimately succeed or report a required size, so only the NULL
 * data buffer contract is asserted here.
 */
static void
ExpectRefuseNullData(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE hAdapter,
                     KMTQUERYADAPTERINFOTYPE Type, const char *Name, UINT Size)
{
    NTSTATUS st = QueryAI(pfn, hAdapter, Type, NULL, Size);
    ok_failed(st,
       "%s: NULL data buffer must be refused, got 0x%08lX\n", Name, (long)st);
}

/* Trace a WCHAR field without relying on a wide-string printf conversion. */
static void
TraceWide(const char *Label, const WCHAR *w)
{
    char buf[256];
    int i;
    for (i = 0; i < (int)sizeof(buf) - 1 && w[i]; i++)
        buf[i] = (w[i] >= 0x20 && w[i] < 0x7f) ? (char)w[i] : '?';
    buf[i] = '\0';
    trace("    %s = \"%s\"\n", Label, buf);
}

/*
 * Many WDDM cap selectors return a single 4-byte BOOL.  Their structs are
 * layout-identical to a bare BOOL, so a BOOL stand-in is safe.
 */
static void
TestBoolCap(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h,
            KMTQUERYADAPTERINFOTYPE Type, const char *Name)
{
    BOOL val = FALSE;
    NTSTATUS st = QueryAI(pfn, h, Type, &val, sizeof(val));

    if (NT_SUCCESS(st))
        trace("%s: value=%d\n", Name, val);
    else
        trace("%s: not available (0x%08lX)\n", Name, (long)st);

    ExpectRefuseBadBuffers(pfn, h, Type, Name, sizeof(BOOL));
}

/* --------------------------------------------------------------------------
 * Generic parameter-validation tests (whole struct / handle / type)
 * -------------------------------------------------------------------------- */

static void Test_NullStruct(void)
{
    /* Whole-struct NULL must be refused (STATUS_INVALID_PARAMETER or fault). */
    CHECK_NULL_REJECTED(PFND3DKMT_QUERYADAPTERINFO, "D3DKMTQueryAdapterInfo");
}

static void Test_BadHandleAndType(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE hAdapter)
{
    D3DKMT_ADAPTERTYPE type;
    NTSTATUS st;

    memset(&type, 0, sizeof(type));

    /* Bogus adapter handle */
    st = QueryAI(pfn, (D3DKMT_HANDLE)0xBAD0CAFE, KMTQAITYPE_ADAPTERTYPE,
                 &type, sizeof(type));
    ok_failed(st,
       "QueryAdapterInfo with bogus adapter handle must fail, got 0x%08lX\n",
       (long)st);

    /* Zero adapter handle */
    st = QueryAI(pfn, (D3DKMT_HANDLE)0, KMTQAITYPE_ADAPTERTYPE,
                 &type, sizeof(type));
    ok_failed(st,
       "QueryAdapterInfo with zero adapter handle must fail, got 0x%08lX\n",
       (long)st);

    /* Unsupported selector value on a valid adapter */
    st = QueryAI(pfn, hAdapter, (KMTQUERYADAPTERINFOTYPE)0x7FFFFFFF,
                 &type, sizeof(type));
    ok_failed(st,
       "QueryAdapterInfo with unsupported Type must fail, got 0x%08lX\n",
       (long)st);
}

/* --------------------------------------------------------------------------
 * WDDM 1.0 (Vista) selectors
 * -------------------------------------------------------------------------- */

static void Test_UmDriverPrivate(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    /* Opaque UMD-private payload; generically querying it normally fails on a
     * display-only / WARP adapter.  Verify graceful behaviour + NULL refusal. */
    BYTE buf[256];
    NTSTATUS st;

    memset(buf, 0, sizeof(buf));
    st = QueryAI(pfn, h, KMTQAITYPE_UMDRIVERPRIVATE, buf, sizeof(buf));
    trace("UMDRIVERPRIVATE: 0x%08lX%s\n", (long)st, NT_SUCCESS(st) ? " (ok)" : "");
    ExpectRefuseNullData(pfn, h, KMTQAITYPE_UMDRIVERPRIVATE,
                         "UMDRIVERPRIVATE", sizeof(buf));
}

static void Test_UmDriverName(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_UMDFILENAMEINFO info;
    NTSTATUS st;

    memset(&info, 0, sizeof(info));
    info.Version = KMTUMDVERSION_DX9;
    st = QueryAI(pfn, h, KMTQAITYPE_UMDRIVERNAME, &info, sizeof(info));
    if (NT_SUCCESS(st))
    {
        trace("UMDRIVERNAME (DX9):\n");
        TraceWide("UmdFileName", info.UmdFileName);
    }
    else
    {
        trace("UMDRIVERNAME not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_UMDRIVERNAME,
                           "UMDRIVERNAME", sizeof(info));
}

static void Test_UmOpenGlInfo(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_OPENGLINFO gl;
    NTSTATUS st;

    memset(&gl, 0, sizeof(gl));
    st = QueryAI(pfn, h, KMTQAITYPE_UMOPENGLINFO, &gl, sizeof(gl));
    if (NT_SUCCESS(st))
    {
        trace("UMOPENGLINFO: Version=%lu Flags=0x%lX\n",
              (unsigned long)gl.Version, (unsigned long)gl.Flags);
        TraceWide("UmdOpenGlIcdFileName", gl.UmdOpenGlIcdFileName);
    }
    else
    {
        trace("UMOPENGLINFO not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_UMOPENGLINFO,
                           "UMOPENGLINFO", sizeof(gl));
}

static void Test_GetSegmentSize(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_SEGMENTSIZEINFO seg;
    NTSTATUS st;

    memset(&seg, 0, sizeof(seg));
    st = QueryAI(pfn, h, KMTQAITYPE_GETSEGMENTSIZE, &seg, sizeof(seg));
    if (NT_SUCCESS(st))
    {
        /* Sizes may legitimately be zero on a display-only/software adapter. */
        trace("GETSEGMENTSIZE: DedicatedVideo=%llu DedicatedSystem=%llu Shared=%llu\n",
              (unsigned long long)seg.DedicatedVideoMemorySize,
              (unsigned long long)seg.DedicatedSystemMemorySize,
              (unsigned long long)seg.SharedSystemMemorySize);
    }
    else
    {
        trace("GETSEGMENTSIZE not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_GETSEGMENTSIZE,
                           "GETSEGMENTSIZE", sizeof(seg));
}

static void Test_AdapterGuid(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    GUID g;
    NTSTATUS st;

    memset(&g, 0, sizeof(g));
    st = QueryAI(pfn, h, KMTQAITYPE_ADAPTERGUID, &g, sizeof(g));
    if (NT_SUCCESS(st))
    {
        trace("ADAPTERGUID: {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n",
              (unsigned long)g.Data1, (unsigned)g.Data2, (unsigned)g.Data3,
              (unsigned)g.Data4[0], (unsigned)g.Data4[1], (unsigned)g.Data4[2],
              (unsigned)g.Data4[3], (unsigned)g.Data4[4], (unsigned)g.Data4[5],
              (unsigned)g.Data4[6], (unsigned)g.Data4[7]);
    }
    else
    {
        trace("ADAPTERGUID not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_ADAPTERGUID,
                           "ADAPTERGUID", sizeof(g));
}

static void Test_FlipQueueInfo(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_FLIPQUEUEINFO fq;
    NTSTATUS st;

    memset(&fq, 0, sizeof(fq));
    st = QueryAI(pfn, h, KMTQAITYPE_FLIPQUEUEINFO, &fq, sizeof(fq));
    if (NT_SUCCESS(st))
        trace("FLIPQUEUEINFO: MaxHW=%u MaxSW=%u FlipInterval=%u\n",
              (unsigned)fq.MaxHardwareFlipQueueLength,
              (unsigned)fq.MaxSoftwareFlipQueueLength,
              (unsigned)fq.FlipFlags.FlipInterval);
    else
        trace("FLIPQUEUEINFO not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_FLIPQUEUEINFO,
                           "FLIPQUEUEINFO", sizeof(fq));
}

static void Test_AdapterAddress(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_ADAPTERADDRESS addr;
    NTSTATUS st;

    memset(&addr, 0, sizeof(addr));
    st = QueryAI(pfn, h, KMTQAITYPE_ADAPTERADDRESS, &addr, sizeof(addr));
    if (NT_SUCCESS(st))
        trace("ADAPTERADDRESS: Bus=%u Device=%u Function=%u\n",
              (unsigned)addr.BusNumber, (unsigned)addr.DeviceNumber,
              (unsigned)addr.FunctionNumber);
    else
        trace("ADAPTERADDRESS not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_ADAPTERADDRESS,
                           "ADAPTERADDRESS", sizeof(addr));
}

static void Test_SetWorkingSetInfo(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    /* This selector SETS working-set info; skip the positive call to avoid
     * perturbing the live adapter and only verify the bad-buffer contracts. */
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_SETWORKINGSETINFO,
                           "SETWORKINGSETINFO", sizeof(D3DKMT_WORKINGSETINFO));
}

static void Test_AdapterRegistryInfo(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_ADAPTERREGISTRYINFO reg;
    NTSTATUS st;

    memset(&reg, 0, sizeof(reg));
    st = QueryAI(pfn, h, KMTQAITYPE_ADAPTERREGISTRYINFO, &reg, sizeof(reg));
    if (NT_SUCCESS(st))
    {
        trace("ADAPTERREGISTRYINFO:\n");
        TraceWide("AdapterString", reg.AdapterString);
        TraceWide("BiosString", reg.BiosString);
        TraceWide("DacType", reg.DacType);
        TraceWide("ChipType", reg.ChipType);
    }
    else
    {
        trace("ADAPTERREGISTRYINFO not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_ADAPTERREGISTRYINFO,
                           "ADAPTERREGISTRYINFO", sizeof(reg));
}

static void Test_CurrentDisplayMode(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_CURRENTDISPLAYMODE mode;
    NTSTATUS st;

    memset(&mode, 0, sizeof(mode));
    mode.VidPnSourceId = 0; /* primary source */
    st = QueryAI(pfn, h, KMTQAITYPE_CURRENTDISPLAYMODE, &mode, sizeof(mode));
    if (NT_SUCCESS(st))
    {
        trace("CURRENTDISPLAYMODE: %ux%u Format=%d\n",
              (unsigned)mode.DisplayMode.Width,
              (unsigned)mode.DisplayMode.Height,
              (int)mode.DisplayMode.Format);
        /* An active display mode always has positive dimensions. */
        ok(mode.DisplayMode.Width > 0 && mode.DisplayMode.Height > 0,
           "current display mode should be non-empty, got %ux%u\n",
           (unsigned)mode.DisplayMode.Width, (unsigned)mode.DisplayMode.Height);
    }
    else
    {
        trace("CURRENTDISPLAYMODE not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_CURRENTDISPLAYMODE,
                           "CURRENTDISPLAYMODE", sizeof(mode));
}

static void Test_ModeList(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    /* Variable-size: an array of D3DKMT_DISPLAYMODE.  Provide a generous
     * buffer; treat any outcome as informational. */
    D3DKMT_DISPLAYMODE modes[64];
    NTSTATUS st;

    memset(modes, 0, sizeof(modes));
    st = QueryAI(pfn, h, KMTQAITYPE_MODELIST, modes, sizeof(modes));
    trace("MODELIST: 0x%08lX%s\n", (long)st, NT_SUCCESS(st) ? " (ok)" : "");
    if (NT_SUCCESS(st))
        trace("    first mode: %ux%u\n",
              (unsigned)modes[0].Width, (unsigned)modes[0].Height);
    ExpectRefuseNullData(pfn, h, KMTQAITYPE_MODELIST, "MODELIST", sizeof(modes));
}

static void Test_CheckDriverUpdateStatus(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    BOOL updated = FALSE;
    NTSTATUS st;

    st = QueryAI(pfn, h, KMTQAITYPE_CHECKDRIVERUPDATESTATUS,
                 &updated, sizeof(updated));
    if (NT_SUCCESS(st))
        trace("CHECKDRIVERUPDATESTATUS: updated=%d\n", updated);
    else
        trace("CHECKDRIVERUPDATESTATUS not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_CHECKDRIVERUPDATESTATUS,
                           "CHECKDRIVERUPDATESTATUS", sizeof(BOOL));
}

static void Test_VirtualAddressInfo(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_VIRTUALADDRESSINFO va;
    NTSTATUS st;

    memset(&va, 0, sizeof(va));
    st = QueryAI(pfn, h, KMTQAITYPE_VIRTUALADDRESSINFO, &va, sizeof(va));
    if (NT_SUCCESS(st))
        trace("VIRTUALADDRESSINFO: VirtualAddressSupported=%u\n",
              (unsigned)va.VirtualAddressFlags.VirtualAddressSupported);
    else
        trace("VIRTUALADDRESSINFO not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_VIRTUALADDRESSINFO,
                           "VIRTUALADDRESSINFO", sizeof(va));
}

/* --------------------------------------------------------------------------
 * WDDM 1.1 (Win7) selectors
 * -------------------------------------------------------------------------- */

static void Test_DriverVersion(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_DRIVERVERSION ver = (D3DKMT_DRIVERVERSION)0;
    NTSTATUS st;

    st = QueryAI(pfn, h, KMTQAITYPE_DRIVERVERSION, &ver, sizeof(ver));
    if (NT_SUCCESS(st))
    {
        trace("DRIVERVERSION: %d\n", (int)ver);
        ok(ver >= KMT_DRIVERVERSION_WDDM_1_0,
           "driver version %d should be >= WDDM 1.0 (%d)\n",
           (int)ver, (int)KMT_DRIVERVERSION_WDDM_1_0);
    }
    else
    {
        trace("DRIVERVERSION not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_DRIVERVERSION,
                           "DRIVERVERSION", sizeof(ver));
}

/* --------------------------------------------------------------------------
 * WDDM 1.2 (Win8) selectors
 * -------------------------------------------------------------------------- */

static void Test_AdapterType(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_ADAPTERTYPE type;
    NTSTATUS st;

    memset(&type, 0, sizeof(type));
    st = QueryAI(pfn, h, KMTQAITYPE_ADAPTERTYPE, &type, sizeof(type));
    if (NT_SUCCESS(st))
    {
        trace("ADAPTERTYPE: Value=0x%08X Render=%u Display=%u Software=%u Post=%u "
              "HybridDiscrete=%u HybridIntegrated=%u IndirectDisplay=%u\n",
              (unsigned)type.Value,
              (unsigned)type.RenderSupported, (unsigned)type.DisplaySupported,
              (unsigned)type.SoftwareDevice, (unsigned)type.PostDevice,
              (unsigned)type.HybridDiscrete, (unsigned)type.HybridIntegrated,
              (unsigned)type.IndirectDisplayDevice);
        /* Every adapter supports at least one of render or display. */
        ok(type.RenderSupported || type.DisplaySupported,
           "adapter must support render or display (Value=0x%08X)\n",
           (unsigned)type.Value);
    }
    else
    {
        trace("ADAPTERTYPE not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_ADAPTERTYPE,
                           "ADAPTERTYPE", sizeof(type));
}

static void Test_OutputDuplContextsCount(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_OUTPUTDUPLCONTEXTSCOUNT odc;
    NTSTATUS st;

    memset(&odc, 0, sizeof(odc));
    odc.VidPnSourceId = 0;
    st = QueryAI(pfn, h, KMTQAITYPE_OUTPUTDUPLCONTEXTSCOUNT, &odc, sizeof(odc));
    if (NT_SUCCESS(st))
        trace("OUTPUTDUPLCONTEXTSCOUNT: %u\n", (unsigned)odc.OutputDuplicationCount);
    else
        trace("OUTPUTDUPLCONTEXTSCOUNT not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_OUTPUTDUPLCONTEXTSCOUNT,
                           "OUTPUTDUPLCONTEXTSCOUNT", sizeof(odc));
}

static void Test_Wddm12Caps(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_WDDM_1_2_CAPS caps;
    NTSTATUS st;

    memset(&caps, 0, sizeof(caps));
    st = QueryAI(pfn, h, KMTQAITYPE_WDDM_1_2_CAPS, &caps, sizeof(caps));
    if (NT_SUCCESS(st))
        trace("WDDM_1_2_CAPS: Value=0x%08X SupportNonVGA=%u SupportGammaRamp=%u "
              "SupportHWCursor=%u\n",
              (unsigned)caps.Value, (unsigned)caps.SupportNonVGA,
              (unsigned)caps.SupportGammaRamp, (unsigned)caps.SupportHWCursor);
    else
        trace("WDDM_1_2_CAPS not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_WDDM_1_2_CAPS,
                           "WDDM_1_2_CAPS", sizeof(caps));
}

static void Test_UmdDriverVersion(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_UMD_DRIVER_VERSION uv;
    NTSTATUS st;

    memset(&uv, 0, sizeof(uv));
    st = QueryAI(pfn, h, KMTQAITYPE_UMD_DRIVER_VERSION, &uv, sizeof(uv));
    if (NT_SUCCESS(st))
        trace("UMD_DRIVER_VERSION: 0x%llX\n",
              (unsigned long long)uv.DriverVersion.QuadPart);
    else
        trace("UMD_DRIVER_VERSION not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_UMD_DRIVER_VERSION,
                           "UMD_DRIVER_VERSION", sizeof(uv));
}

/* --------------------------------------------------------------------------
 * WDDM 1.3 (Win8.1) selectors
 * -------------------------------------------------------------------------- */

static void Test_DListDriverName(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_DLIST_DRIVER_NAME dl;
    NTSTATUS st;

    memset(&dl, 0, sizeof(dl));
    st = QueryAI(pfn, h, KMTQAITYPE_DLIST_DRIVER_NAME, &dl, sizeof(dl));
    if (NT_SUCCESS(st))
    {
        trace("DLIST_DRIVER_NAME:\n");
        TraceWide("DListFileName", dl.DListFileName);
    }
    else
    {
        trace("DLIST_DRIVER_NAME not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_DLIST_DRIVER_NAME,
                           "DLIST_DRIVER_NAME", sizeof(dl));
}

static void Test_Wddm13Caps(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_WDDM_1_3_CAPS caps;
    NTSTATUS st;

    memset(&caps, 0, sizeof(caps));
    st = QueryAI(pfn, h, KMTQAITYPE_WDDM_1_3_CAPS, &caps, sizeof(caps));
    if (NT_SUCCESS(st))
        trace("WDDM_1_3_CAPS: Value=0x%08X SupportMiracast=%u "
              "IsHybridIntegratedGPU=%u IsHybridDiscreteGPU=%u\n",
              (unsigned)caps.Value, (unsigned)caps.SupportMiracast,
              (unsigned)caps.IsHybridIntegratedGPU,
              (unsigned)caps.IsHybridDiscreteGPU);
    else
        trace("WDDM_1_3_CAPS not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_WDDM_1_3_CAPS,
                           "WDDM_1_3_CAPS", sizeof(caps));
}

static void Test_MpoHudSupport(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_MULTIPLANEOVERLAY_HUD_SUPPORT hud;
    NTSTATUS st;

    memset(&hud, 0, sizeof(hud));
    st = QueryAI(pfn, h, KMTQAITYPE_MULTIPLANEOVERLAY_HUD_SUPPORT,
                 &hud, sizeof(hud));
    if (NT_SUCCESS(st))
        trace("MPO_HUD_SUPPORT: KernelSupported=%d HudSupported=%d\n",
              hud.KernelSupported, hud.HudSupported);
    else
        trace("MPO_HUD_SUPPORT not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_MULTIPLANEOVERLAY_HUD_SUPPORT,
                           "MPO_HUD_SUPPORT", sizeof(hud));
}

/* --------------------------------------------------------------------------
 * WDDM 2.0 (Win10 1507) selectors
 * -------------------------------------------------------------------------- */

static void Test_Wddm20Caps(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_WDDM_2_0_CAPS caps;
    NTSTATUS st;

    memset(&caps, 0, sizeof(caps));
    st = QueryAI(pfn, h, KMTQAITYPE_WDDM_2_0_CAPS, &caps, sizeof(caps));
    if (NT_SUCCESS(st))
        trace("WDDM_2_0_CAPS: Value=0x%08X Support64BitAtomics=%u "
              "GpuMmuSupported=%u IoMmuSupported=%u\n",
              (unsigned)caps.Value, (unsigned)caps.Support64BitAtomics,
              (unsigned)caps.GpuMmuSupported, (unsigned)caps.IoMmuSupported);
    else
        trace("WDDM_2_0_CAPS not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_WDDM_2_0_CAPS,
                           "WDDM_2_0_CAPS", sizeof(caps));
}

static void Test_NodeMetadata(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_NODEMETADATA nm;
    NTSTATUS st;

    memset(&nm, 0, sizeof(nm));
    nm.NodeOrdinalAndAdapterIndex = 0; /* node 0, physical adapter 0 */
    st = QueryAI(pfn, h, KMTQAITYPE_NODEMETADATA, &nm, sizeof(nm));
    if (NT_SUCCESS(st))
    {
        trace("NODEMETADATA[0]: EngineType=%d GpuMmuSupported=%d IoMmuSupported=%d\n",
              (int)nm.NodeData.EngineType,
              nm.NodeData.GpuMmuSupported, nm.NodeData.IoMmuSupported);
        TraceWide("FriendlyName", nm.NodeData.FriendlyName);
    }
    else
    {
        /* Display-only adapters have no render nodes -> expected failure. */
        trace("NODEMETADATA not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_NODEMETADATA,
                           "NODEMETADATA", sizeof(nm));
}

static void Test_CpDriverName(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_CPDRIVERNAME cp;
    NTSTATUS st;

    memset(&cp, 0, sizeof(cp));
    st = QueryAI(pfn, h, KMTQAITYPE_CPDRIVERNAME, &cp, sizeof(cp));
    if (NT_SUCCESS(st))
    {
        trace("CPDRIVERNAME:\n");
        TraceWide("ContentProtectionFileName", cp.ContentProtectionFileName);
    }
    else
    {
        trace("CPDRIVERNAME not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_CPDRIVERNAME,
                           "CPDRIVERNAME", sizeof(cp));
}

static void Test_MiracastCompanionDriverName(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_MIRACASTCOMPANIONDRIVERNAME mc;
    NTSTATUS st;

    memset(&mc, 0, sizeof(mc));
    st = QueryAI(pfn, h, KMTQAITYPE_MIRACASTCOMPANIONDRIVERNAME, &mc, sizeof(mc));
    if (NT_SUCCESS(st))
    {
        trace("MIRACASTCOMPANIONDRIVERNAME:\n");
        TraceWide("MiracastCompanionDriverName", mc.MiracastCompanionDriverName);
    }
    else
    {
        trace("MIRACASTCOMPANIONDRIVERNAME not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_MIRACASTCOMPANIONDRIVERNAME,
                           "MIRACASTCOMPANIONDRIVERNAME", sizeof(mc));
}

static void Test_PhysicalAdapterCount(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_PHYSICAL_ADAPTER_COUNT pc;
    NTSTATUS st;

    memset(&pc, 0, sizeof(pc));
    st = QueryAI(pfn, h, KMTQAITYPE_PHYSICALADAPTERCOUNT, &pc, sizeof(pc));
    if (NT_SUCCESS(st))
    {
        trace("PHYSICALADAPTERCOUNT: %u\n", (unsigned)pc.Count);
        ok(pc.Count >= 1,
           "physical adapter count should be >= 1, got %u\n", (unsigned)pc.Count);
    }
    else
    {
        trace("PHYSICALADAPTERCOUNT not available (0x%08lX)\n", (long)st);
    }
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_PHYSICALADAPTERCOUNT,
                           "PHYSICALADAPTERCOUNT", sizeof(pc));
}

static void Test_PhysicalAdapterDeviceIds(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_QUERY_DEVICE_IDS ids;
    NTSTATUS st;

    memset(&ids, 0, sizeof(ids));
    ids.PhysicalAdapterIndex = 0;
    st = QueryAI(pfn, h, KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &ids, sizeof(ids));
    if (NT_SUCCESS(st))
        trace("PHYSICALADAPTERDEVICEIDS[0]: Vendor=0x%04X Device=0x%04X "
              "SubVendor=0x%04X SubSystem=0x%04X Rev=0x%02X BusType=%u\n",
              (unsigned)ids.DeviceIds.VendorID, (unsigned)ids.DeviceIds.DeviceID,
              (unsigned)ids.DeviceIds.SubVendorID, (unsigned)ids.DeviceIds.SubSystemID,
              (unsigned)ids.DeviceIds.RevisionID, (unsigned)ids.DeviceIds.BusType);
    else
        trace("PHYSICALADAPTERDEVICEIDS not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_PHYSICALADAPTERDEVICEIDS,
                           "PHYSICALADAPTERDEVICEIDS", sizeof(ids));
}

static void Test_DriverCapsExt(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_DRIVERCAPS_EXT ext;
    NTSTATUS st;

    memset(&ext, 0, sizeof(ext));
    st = QueryAI(pfn, h, KMTQAITYPE_DRIVERCAPS_EXT, &ext, sizeof(ext));
    if (NT_SUCCESS(st))
        trace("DRIVERCAPS_EXT: Value=0x%08X VirtualModeSupport=%u Usb4MonitorSupport=%u\n",
              (unsigned)ext.Value, (unsigned)ext.VirtualModeSupport,
              (unsigned)ext.Usb4MonitorSupport);
    else
        trace("DRIVERCAPS_EXT not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_DRIVERCAPS_EXT,
                           "DRIVERCAPS_EXT", sizeof(ext));
}

static void Test_QueryMiracastDriverType(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_QUERY_MIRACAST_DRIVER_TYPE mt;
    NTSTATUS st;

    memset(&mt, 0, sizeof(mt));
    st = QueryAI(pfn, h, KMTQAITYPE_QUERY_MIRACAST_DRIVER_TYPE, &mt, sizeof(mt));
    if (NT_SUCCESS(st))
        trace("QUERY_MIRACAST_DRIVER_TYPE: %d\n", (int)mt.MiracastDriverType);
    else
        trace("QUERY_MIRACAST_DRIVER_TYPE not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_QUERY_MIRACAST_DRIVER_TYPE,
                           "QUERY_MIRACAST_DRIVER_TYPE", sizeof(mt));
}

static void Test_QueryGpuMmuCaps(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_QUERY_GPUMMU_CAPS mmu;
    NTSTATUS st;

    memset(&mmu, 0, sizeof(mmu));
    mmu.PhysicalAdapterIndex = 0;
    st = QueryAI(pfn, h, KMTQAITYPE_QUERY_GPUMMU_CAPS, &mmu, sizeof(mmu));
    if (NT_SUCCESS(st))
        trace("QUERY_GPUMMU_CAPS[0]: VABitCount=%u ReadOnly=%u NoExecute=%u "
              "CacheCoherent=%u\n",
              (unsigned)mmu.Caps.VirtualAddressBitCount,
              (unsigned)mmu.Caps.Flags.ReadOnlyMemorySupported,
              (unsigned)mmu.Caps.Flags.NoExecuteMemorySupported,
              (unsigned)mmu.Caps.Flags.CacheCoherentMemorySupported);
    else
        trace("QUERY_GPUMMU_CAPS not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_QUERY_GPUMMU_CAPS,
                           "QUERY_GPUMMU_CAPS", sizeof(mmu));
}

static void Test_QueryHwProtectionTeardownCount(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    UINT count = 0;
    NTSTATUS st;

    st = QueryAI(pfn, h, KMTQAITYPE_QUERY_HW_PROTECTION_TEARDOWN_COUNT,
                 &count, sizeof(count));
    if (NT_SUCCESS(st))
        trace("QUERY_HW_PROTECTION_TEARDOWN_COUNT: %u\n", (unsigned)count);
    else
        trace("QUERY_HW_PROTECTION_TEARDOWN_COUNT not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_QUERY_HW_PROTECTION_TEARDOWN_COUNT,
                           "QUERY_HW_PROTECTION_TEARDOWN_COUNT", sizeof(UINT));
}

/* --------------------------------------------------------------------------
 * Higher-tier selectors (compiled in only when the header exposes them).
 * -------------------------------------------------------------------------- */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
static void Test_GetSegmentGroupSize(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_SEGMENTGROUPSIZEINFO sg;
    NTSTATUS st;

    memset(&sg, 0, sizeof(sg));
    sg.PhysicalAdapterIndex = 0;
    st = QueryAI(pfn, h, KMTQAITYPE_GETSEGMENTGROUPSIZE, &sg, sizeof(sg));
    if (NT_SUCCESS(st))
        trace("GETSEGMENTGROUPSIZE[0]: Local=%llu NonLocal=%llu NonBudget=%llu\n",
              (unsigned long long)sg.LocalMemory,
              (unsigned long long)sg.NonLocalMemory,
              (unsigned long long)sg.NonBudgetMemory);
    else
        trace("GETSEGMENTGROUPSIZE not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_GETSEGMENTGROUPSIZE,
                           "GETSEGMENTGROUPSIZE", sizeof(sg));
}
#endif /* WDDM2_2 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
static void Test_KmdDriverVersion(PFND3DKMT_QUERYADAPTERINFO pfn, D3DKMT_HANDLE h)
{
    D3DKMT_KMD_DRIVER_VERSION kv;
    NTSTATUS st;

    memset(&kv, 0, sizeof(kv));
    st = QueryAI(pfn, h, KMTQAITYPE_KMD_DRIVER_VERSION, &kv, sizeof(kv));
    if (NT_SUCCESS(st))
        trace("KMD_DRIVER_VERSION: 0x%llX\n",
              (unsigned long long)kv.DriverVersion.QuadPart);
    else
        trace("KMD_DRIVER_VERSION not available (0x%08lX)\n", (long)st);
    ExpectRefuseBadBuffers(pfn, h, KMTQAITYPE_KMD_DRIVER_VERSION,
                           "KMD_DRIVER_VERSION", sizeof(kv));
}
#endif /* WDDM2_4 */

/* --------------------------------------------------------------------------
 * START_TEST: exhaustive selector sweep
 * -------------------------------------------------------------------------- */

START_TEST(D3dkmtAdapterInfo)
{
    PFND3DKMT_QUERYADAPTERINFO pfn;
    D3DKMT_HANDLE hAdapter;

    /* Whole-struct NULL contract (loads its own pointer, skips if missing). */
    Test_NullStruct();

    pfn = (PFND3DKMT_QUERYADAPTERINFO)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    if (!pfn)
    {
        skip("D3DKMTQueryAdapterInfo not exported by gdi32.dll\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open \\\\.\\DISPLAY1 adapter\n");
        return;
    }

    Test_BadHandleAndType(pfn, hAdapter);

    /* WDDM 1.0 */
    Test_UmDriverPrivate(pfn, hAdapter);
    Test_UmDriverName(pfn, hAdapter);
    Test_UmOpenGlInfo(pfn, hAdapter);
    Test_GetSegmentSize(pfn, hAdapter);
    Test_AdapterGuid(pfn, hAdapter);
    Test_FlipQueueInfo(pfn, hAdapter);
    Test_AdapterAddress(pfn, hAdapter);
    Test_SetWorkingSetInfo(pfn, hAdapter);
    Test_AdapterRegistryInfo(pfn, hAdapter);
    Test_CurrentDisplayMode(pfn, hAdapter);
    Test_ModeList(pfn, hAdapter);
    Test_CheckDriverUpdateStatus(pfn, hAdapter);
    Test_VirtualAddressInfo(pfn, hAdapter);

    /* WDDM 1.1 */
    Test_DriverVersion(pfn, hAdapter);

    /* WDDM 1.2 */
    Test_AdapterType(pfn, hAdapter);
    Test_OutputDuplContextsCount(pfn, hAdapter);
    Test_Wddm12Caps(pfn, hAdapter);
    Test_UmdDriverVersion(pfn, hAdapter);
    TestBoolCap(pfn, hAdapter, KMTQAITYPE_DIRECTFLIP_SUPPORT, "DIRECTFLIP_SUPPORT");

    /* WDDM 1.3 */
    TestBoolCap(pfn, hAdapter, KMTQAITYPE_MULTIPLANEOVERLAY_SUPPORT,
                "MULTIPLANEOVERLAY_SUPPORT");
    Test_DListDriverName(pfn, hAdapter);
    Test_Wddm13Caps(pfn, hAdapter);
    Test_MpoHudSupport(pfn, hAdapter);

    /* WDDM 2.0 */
    Test_Wddm20Caps(pfn, hAdapter);
    Test_NodeMetadata(pfn, hAdapter);
    Test_CpDriverName(pfn, hAdapter);
    TestBoolCap(pfn, hAdapter, KMTQAITYPE_XBOX, "XBOX");
    TestBoolCap(pfn, hAdapter, KMTQAITYPE_INDEPENDENTFLIP_SUPPORT,
                "INDEPENDENTFLIP_SUPPORT");
    Test_MiracastCompanionDriverName(pfn, hAdapter);
    Test_PhysicalAdapterCount(pfn, hAdapter);
    Test_PhysicalAdapterDeviceIds(pfn, hAdapter);
    Test_DriverCapsExt(pfn, hAdapter);
    Test_QueryMiracastDriverType(pfn, hAdapter);
    Test_QueryGpuMmuCaps(pfn, hAdapter);
    TestBoolCap(pfn, hAdapter, KMTQAITYPE_QUERY_MULTIPLANEOVERLAY_DECODE_SUPPORT,
                "QUERY_MULTIPLANEOVERLAY_DECODE_SUPPORT");
    Test_QueryHwProtectionTeardownCount(pfn, hAdapter);
    TestBoolCap(pfn, hAdapter, KMTQAITYPE_QUERY_ISBADDRIVERFORHWPROTECTIONDISABLED,
                "QUERY_ISBADDRIVERFORHWPROTECTIONDISABLED");
    TestBoolCap(pfn, hAdapter, KMTQAITYPE_MULTIPLANEOVERLAY_SECONDARY_SUPPORT,
                "MULTIPLANEOVERLAY_SECONDARY_SUPPORT");
    TestBoolCap(pfn, hAdapter, KMTQAITYPE_INDEPENDENTFLIP_SECONDARY_SUPPORT,
                "INDEPENDENTFLIP_SECONDARY_SUPPORT");

    /* Higher-tier selectors (only present at the corresponding compile level) */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    Test_GetSegmentGroupSize(pfn, hAdapter);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    Test_KmdDriverVersion(pfn, hAdapter);
#endif

    CloseAdapter(hAdapter);
}

/* --------------------------------------------------------------------------
 * START_TEST: adapter classification gate
 *
 * Queries ADAPTERTYPE for the render/display/software flags, then WDDM_2_0_CAPS
 * and QUERY_GPUMMU_CAPS for GpuMmu support.  This documents at runtime whether
 * render-path / GPU-VA tests can run on the target adapter (real GPU vs WARP
 * software vs display-only MSBDA).
 * -------------------------------------------------------------------------- */

START_TEST(adapterclass)
{
    PFND3DKMT_QUERYADAPTERINFO pfn;
    D3DKMT_HANDLE h;
    D3DKMT_ADAPTERTYPE type;
    D3DKMT_WDDM_2_0_CAPS caps20;
    D3DKMT_QUERY_GPUMMU_CAPS mmu;
    NTSTATUS st;
    BOOL render = FALSE, display = FALSE, software = FALSE, gpummu = FALSE;

    pfn = (PFND3DKMT_QUERYADAPTERINFO)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    if (!pfn)
    {
        skip("D3DKMTQueryAdapterInfo not exported by gdi32.dll\n");
        return;
    }

    h = OpenAdapterFromDisplay1();
    if (!h)
    {
        skip("Cannot open \\\\.\\DISPLAY1 adapter\n");
        return;
    }

    /* Primary capability gate: ADAPTERTYPE */
    memset(&type, 0, sizeof(type));
    st = QueryAI(pfn, h, KMTQAITYPE_ADAPTERTYPE, &type, sizeof(type));
    if (NT_SUCCESS(st))
    {
        render   = type.RenderSupported;
        display  = type.DisplaySupported;
        software = type.SoftwareDevice;
        trace("ADAPTERTYPE: Value=0x%08X Render=%u Display=%u Software=%u Post=%u "
              "HybridDiscrete=%u HybridIntegrated=%u IndirectDisplay=%u\n",
              (unsigned)type.Value,
              (unsigned)type.RenderSupported, (unsigned)type.DisplaySupported,
              (unsigned)type.SoftwareDevice, (unsigned)type.PostDevice,
              (unsigned)type.HybridDiscrete, (unsigned)type.HybridIntegrated,
              (unsigned)type.IndirectDisplayDevice);
        ok(render || display,
           "adapter must support render or display (Value=0x%08X)\n",
           (unsigned)type.Value);
    }
    else
    {
        trace("ADAPTERTYPE not available (0x%08lX)\n", (long)st);
    }

    /* GpuMmu / IoMmu support: WDDM_2_0_CAPS */
    memset(&caps20, 0, sizeof(caps20));
    st = QueryAI(pfn, h, KMTQAITYPE_WDDM_2_0_CAPS, &caps20, sizeof(caps20));
    if (NT_SUCCESS(st))
    {
        gpummu = caps20.GpuMmuSupported;
        trace("WDDM_2_0_CAPS: GpuMmuSupported=%u IoMmuSupported=%u "
              "Support64BitAtomics=%u\n",
              (unsigned)caps20.GpuMmuSupported, (unsigned)caps20.IoMmuSupported,
              (unsigned)caps20.Support64BitAtomics);
    }
    else
    {
        trace("WDDM_2_0_CAPS not available (0x%08lX)\n", (long)st);
    }

    /* GPU virtual-address geometry: QUERY_GPUMMU_CAPS */
    memset(&mmu, 0, sizeof(mmu));
    mmu.PhysicalAdapterIndex = 0;
    st = QueryAI(pfn, h, KMTQAITYPE_QUERY_GPUMMU_CAPS, &mmu, sizeof(mmu));
    if (NT_SUCCESS(st))
        trace("QUERY_GPUMMU_CAPS: VirtualAddressBitCount=%u ReadOnly=%u "
              "NoExecute=%u CacheCoherent=%u\n",
              (unsigned)mmu.Caps.VirtualAddressBitCount,
              (unsigned)mmu.Caps.Flags.ReadOnlyMemorySupported,
              (unsigned)mmu.Caps.Flags.NoExecuteMemorySupported,
              (unsigned)mmu.Caps.Flags.CacheCoherentMemorySupported);
    else
        trace("QUERY_GPUMMU_CAPS not available (0x%08lX)\n", (long)st);

    trace("ADAPTER CLASS SUMMARY: render=%d display=%d software=%d gpummu=%d "
          "=> render-path tests %s\n",
          render, display, software, gpummu,
          (render && gpummu) ? "MAY run" : "are EXPECTED to skip");

    CloseAdapter(h);
}

/* --------------------------------------------------------------------------
 * START_TEST: WDDM 2.x/3.0 capability words
 *
 * Queries every per-level capability selector and asserts only invariants
 * that hold on native Windows as well: a successful query never sets a
 * dependent bit without its prerequisite, the segment views agree with each
 * other, and the physical adapter count is at least one.  A selector the
 * target OS/adapter does not support is traced, never a failure.
 * -------------------------------------------------------------------------- */

START_TEST(wddmcaps)
{
    PFND3DKMT_QUERYADAPTERINFO pfn;
    D3DKMT_HANDLE h;
    NTSTATUS st;

    pfn = (PFND3DKMT_QUERYADAPTERINFO)LoadD3DKMTProc("D3DKMTQueryAdapterInfo");
    if (!pfn)
    {
        skip("D3DKMTQueryAdapterInfo not exported by gdi32.dll\n");
        return;
    }
    h = OpenAdapterFromDisplay1();
    if (!h)
    {
        skip("Cannot open \\\\.\\DISPLAY1 adapter\n");
        return;
    }

    {
        D3DKMT_WDDM_2_0_CAPS caps;

        memset(&caps, 0xCC, sizeof(caps));
        st = QueryAI(pfn, h, KMTQAITYPE_WDDM_2_0_CAPS, &caps, sizeof(caps));
        if (NT_SUCCESS(st))
            trace("WDDM_2_0_CAPS: 0x%08X GpuMmu=%u IoMmu=%u\n",
                  (unsigned)caps.Value, (unsigned)caps.GpuMmuSupported,
                  (unsigned)caps.IoMmuSupported);
        else
            trace("WDDM_2_0_CAPS not available (0x%08lX)\n", (long)st);
    }

    {
        D3DKMT_WDDM_2_7_CAPS caps;

        memset(&caps, 0xCC, sizeof(caps));
        st = QueryAI(pfn, h, KMTQAITYPE_WDDM_2_7_CAPS, &caps, sizeof(caps));
        if (NT_SUCCESS(st))
        {
            ok(!caps.HwSchEnabled || caps.HwSchSupported,
               "WDDM_2_7_CAPS: HwSchEnabled without HwSchSupported (0x%08X)\n",
               (unsigned)caps.Value);
            trace("WDDM_2_7_CAPS: 0x%08X HwSchSupported=%u HwSchEnabled=%u "
                  "IndependentVidPnVSyncControl=%u\n",
                  (unsigned)caps.Value, (unsigned)caps.HwSchSupported,
                  (unsigned)caps.HwSchEnabled,
                  (unsigned)caps.IndependentVidPnVSyncControl);
        }
        else
            trace("WDDM_2_7_CAPS not available (0x%08lX)\n", (long)st);
    }

    {
        D3DKMT_WDDM_2_9_CAPS caps;

        memset(&caps, 0xCC, sizeof(caps));
        st = QueryAI(pfn, h, KMTQAITYPE_WDDM_2_9_CAPS, &caps, sizeof(caps));
        if (NT_SUCCESS(st))
        {
            ok(!caps.HwSchEnabled ||
               caps.HwSchSupportState != DXGK_FEATURE_SUPPORT_ALWAYS_OFF,
               "WDDM_2_9_CAPS: HwSchEnabled while support state is ALWAYS_OFF\n");
            trace("WDDM_2_9_CAPS: 0x%08X HwSchSupportState=%u HwSchEnabled=%u "
                  "SelfRefreshMemorySupported=%u\n",
                  (unsigned)caps.Value, (unsigned)caps.HwSchSupportState,
                  (unsigned)caps.HwSchEnabled,
                  (unsigned)caps.SelfRefreshMemorySupported);
        }
        else
            trace("WDDM_2_9_CAPS not available (0x%08lX)\n", (long)st);
    }

    {
        D3DKMT_WDDM_3_0_CAPS caps;

        memset(&caps, 0xCC, sizeof(caps));
        st = QueryAI(pfn, h, KMTQAITYPE_WDDM_3_0_CAPS, &caps, sizeof(caps));
        if (NT_SUCCESS(st))
        {
            ok(!caps.HwFlipQueueEnabled ||
               caps.HwFlipQueueSupportState != DXGK_FEATURE_SUPPORT_ALWAYS_OFF,
               "WDDM_3_0_CAPS: HwFlipQueueEnabled while support state is ALWAYS_OFF\n");
            trace("WDDM_3_0_CAPS: 0x%08X HwFlipQueueSupportState=%u "
                  "HwFlipQueueEnabled=%u DisplayableSupported=%u\n",
                  (unsigned)caps.Value, (unsigned)caps.HwFlipQueueSupportState,
                  (unsigned)caps.HwFlipQueueEnabled,
                  (unsigned)caps.DisplayableSupported);
        }
        else
            trace("WDDM_3_0_CAPS not available (0x%08lX)\n", (long)st);
    }

    {
        D3DKMT_WDDM_3_1_CAPS caps;

        memset(&caps, 0xCC, sizeof(caps));
        st = QueryAI(pfn, h, KMTQAITYPE_WDDM_3_1_CAPS, &caps, sizeof(caps));
        if (NT_SUCCESS(st))
        {
            ok(caps.Reserved == 0,
               "WDDM_3_1_CAPS: reserved bits set (0x%08X)\n", (unsigned)caps.Value);
            trace("WDDM_3_1_CAPS: 0x%08X NativeGpuFenceSupported=%u\n",
                  (unsigned)caps.Value, (unsigned)caps.NativeGpuFenceSupported);
        }
        else
            trace("WDDM_3_1_CAPS not available (0x%08lX)\n", (long)st);
    }

    {
        D3DKMT_CROSSADAPTERRESOURCE_SUPPORT support;

        memset(&support, 0xCC, sizeof(support));
        st = QueryAI(pfn, h, KMTQAITYPE_CROSSADAPTERRESOURCE_SUPPORT,
                     &support, sizeof(support));
        if (NT_SUCCESS(st))
        {
            ok(support.SupportTier <= D3DKMT_CROSSADAPTERRESOURCE_SUPPORT_TIER_SCANOUT,
               "CROSSADAPTERRESOURCE_SUPPORT: bogus tier %u\n",
               (unsigned)support.SupportTier);
            trace("CROSSADAPTERRESOURCE_SUPPORT: tier=%u\n",
                  (unsigned)support.SupportTier);
        }
        else
            trace("CROSSADAPTERRESOURCE_SUPPORT not available (0x%08lX)\n", (long)st);
    }

    {
        D3DKMT_PHYSICAL_ADAPTER_COUNT count;

        memset(&count, 0, sizeof(count));
        st = QueryAI(pfn, h, KMTQAITYPE_PHYSICALADAPTERCOUNT, &count, sizeof(count));
        if (NT_SUCCESS(st))
        {
            ok(count.Count >= 1, "PHYSICALADAPTERCOUNT: zero adapters\n");
            trace("PHYSICALADAPTERCOUNT: %u\n", (unsigned)count.Count);
        }
        else
            trace("PHYSICALADAPTERCOUNT not available (0x%08lX)\n", (long)st);
    }

    {
        D3DKMT_SEGMENTSIZEINFO seg;
        D3DKMT_SEGMENTGROUPSIZEINFO group;
        NTSTATUS st2;

        memset(&seg, 0, sizeof(seg));
        memset(&group, 0, sizeof(group));
        st = QueryAI(pfn, h, KMTQAITYPE_GETSEGMENTSIZE, &seg, sizeof(seg));
        st2 = QueryAI(pfn, h, KMTQAITYPE_GETSEGMENTGROUPSIZE, &group, sizeof(group));
        if (NT_SUCCESS(st) && NT_SUCCESS(st2))
        {
            ok(seg.DedicatedVideoMemorySize == group.LegacyInfo.DedicatedVideoMemorySize &&
               seg.SharedSystemMemorySize == group.LegacyInfo.SharedSystemMemorySize,
               "segment views disagree: legacy %I64u/%I64u vs group legacy %I64u/%I64u\n",
               seg.DedicatedVideoMemorySize, seg.SharedSystemMemorySize,
               group.LegacyInfo.DedicatedVideoMemorySize,
               group.LegacyInfo.SharedSystemMemorySize);
            trace("SEGMENTGROUPSIZE: local=%I64u nonlocal=%I64u\n",
                  group.LocalMemory, group.NonLocalMemory);
        }
        else
            trace("segment size queries: 0x%08lX / 0x%08lX\n", (long)st, (long)st2);
    }

    {
        D3DKMT_NODEMETADATA meta;

        memset(&meta, 0, sizeof(meta));
        meta.NodeOrdinalAndAdapterIndex = 0;
        st = QueryAI(pfn, h, KMTQAITYPE_NODEMETADATA, &meta, sizeof(meta));
        if (NT_SUCCESS(st))
            trace("NODEMETADATA[0]: EngineType=%d name=%.32ws\n",
                  (int)meta.NodeData.EngineType, meta.NodeData.FriendlyName);
        else
            trace("NODEMETADATA not available (0x%08lX)\n", (long)st);

        memset(&meta, 0, sizeof(meta));
        meta.NodeOrdinalAndAdapterIndex = 0xFFFF;
        st = QueryAI(pfn, h, KMTQAITYPE_NODEMETADATA, &meta, sizeof(meta));
        ok_failed(st, "NODEMETADATA accepted bogus node ordinal 0xFFFF\n");
    }

    CloseAdapter(h);
}
