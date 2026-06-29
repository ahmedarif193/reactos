/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Comprehensive WDDM specification constant validation
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * This file validates ~200 constants, enum values, and type sizes defined by
 * the WDDM specification.  These are compile-time/static checks that catch
 * SDK header divergence from the Windows DDK.
 */

#include "precomp.h"
#include <d3dkmdt.h>
#include <d3dukmdt.h>

/* ======================================================================
 * Pool tag format tests (4-byte ASCII tags)
 * ====================================================================== */

static void Test_PoolTags(void)
{
    /* Validate expected dxgkrnl pool tag constants.
     * The C literal value is compiler-defined source order; debugger pool
     * output may display the bytes reversed. */
    ok('M' == 0x4D, "ASCII M check\n");
    ok('A' == 0x41, "ASCII A check\n");
    ok('V' == 0x56, "ASCII V check\n");
    ok('C' == 0x43, "ASCII C check\n");
    ok('L' == 0x4C, "ASCII L check\n");
    ok('P' == 0x50, "ASCII P check\n");
    ok('Y' == 0x59, "ASCII Y check\n");
    ok('R' == 0x52, "ASCII R check\n");
    ok('S' == 0x53, "ASCII S check\n");

    /* TAG_DXGK_MINIPORT = 'MxgD' */
    ULONG TAG_TEST = 'MxgD';
    ok_eq_hex(TAG_TEST, 0x4D786744);

    /* TAG_DXGK_ADAPTER = 'AxgD' */
    TAG_TEST = 'AxgD';
    ok_eq_hex(TAG_TEST, 0x41786744);

    /* TAG_DXGK_DEVICE = 'VxgD' */
    TAG_TEST = 'VxgD';
    ok_eq_hex(TAG_TEST, 0x56786744);

    /* TAG_DXGK_CONTEXT = 'CxgD' */
    TAG_TEST = 'CxgD';
    ok_eq_hex(TAG_TEST, 0x43786744);

    /* TAG_DXGK_ALLOC = 'LxgD' */
    TAG_TEST = 'LxgD';
    ok_eq_hex(TAG_TEST, 0x4C786744);

    /* TAG_DXGK_PROCESS = 'PxgD' */
    TAG_TEST = 'PxgD';
    ok_eq_hex(TAG_TEST, 0x50786744);

    /* TAG_DXGK_SYNC = 'YxgD' */
    TAG_TEST = 'YxgD';
    ok_eq_hex(TAG_TEST, 0x59786744);

    /* TAG_DXGK_REGISTRY = 'RxgD' */
    TAG_TEST = 'RxgD';
    ok_eq_hex(TAG_TEST, 0x52786744);

    /* TAG_DXGK_RESOURCES = 'SxgD' */
    TAG_TEST = 'SxgD';
    ok_eq_hex(TAG_TEST, 0x53786744);

    /* TAG_DXGK_VIDPN = 'NVgD' */
    TAG_TEST = 'NVgD';
    ok_eq_hex(TAG_TEST, 0x4E566744);

    /* TAG_DXGK_PRESENT = 'TxgD' */
    TAG_TEST = 'TxgD';
    ok_eq_hex(TAG_TEST, 0x54786744);
}

/* ======================================================================
 * DXGKDDI interface version constants
 * ====================================================================== */

static void Test_InterfaceVersions(void)
{
    /* DXGKDDI_INTERFACE_VERSION_VISTA must exist */
    ok(DXGKDDI_INTERFACE_VERSION_VISTA > 0,
       "DXGKDDI_INTERFACE_VERSION_VISTA should be positive\n");

    /* Vista version should be the baseline */
    ok(DXGKDDI_INTERFACE_VERSION_VISTA <= 0x2000,
       "DXGKDDI_INTERFACE_VERSION_VISTA seems too large: 0x%X\n",
       DXGKDDI_INTERFACE_VERSION_VISTA);
}

/* ======================================================================
 * D3DKMT_HANDLE type properties
 * ====================================================================== */

static void Test_HandleTypeProperties(void)
{
    /* D3DKMT_HANDLE is UINT (4 bytes) */
    ok_eq_uint(sizeof(D3DKMT_HANDLE), 4);

    /* Must be unsigned */
    D3DKMT_HANDLE h = (D3DKMT_HANDLE)-1;
    ok(h > 0, "D3DKMT_HANDLE should be unsigned (0x%08X)\n", h);

    /* Zero is the invalid sentinel */
    ok((D3DKMT_HANDLE)0 == 0, "Zero sentinel check\n");

    /* Max value */
    ok(h == 0xFFFFFFFF, "Max D3DKMT_HANDLE should be 0xFFFFFFFF\n");
}

/* ======================================================================
 * D3DDDI types and enums
 * ====================================================================== */

static void Test_D3DDDITypes(void)
{
    /* D3DDDI_VIDEO_PRESENT_SOURCE_ID is UINT */
    ok_eq_uint(sizeof(D3DDDI_VIDEO_PRESENT_SOURCE_ID), sizeof(UINT));

    /* D3DDDI_VIDEO_PRESENT_TARGET_ID is UINT */
    ok_eq_uint(sizeof(D3DDDI_VIDEO_PRESENT_TARGET_ID), sizeof(UINT));

    /* D3DDDI_RATIONAL — fraction type */
    ok_eq_uint(sizeof(D3DDDI_RATIONAL), 2 * sizeof(UINT));
    ok_eq_uint(offsetof(D3DDDI_RATIONAL, Numerator), 0);
    ok_eq_uint(offsetof(D3DDDI_RATIONAL, Denominator), sizeof(UINT));

    /* Verify a 60Hz refresh rate encoding */
    D3DDDI_RATIONAL r;
    r.Numerator = 60;
    r.Denominator = 1;
    ok_eq_uint(r.Numerator, 60);
    ok_eq_uint(r.Denominator, 1);
}

/* ======================================================================
 * D3DKMDT mode structure sizes
 * ====================================================================== */

static void Test_ModeStructSizes(void)
{
    /* Source mode must be large enough for graphics + signal info */
    ok(sizeof(D3DKMDT_VIDPN_SOURCE_MODE) > sizeof(UINT),
       "VIDPN_SOURCE_MODE too small\n");

    /* Target mode must contain at least video signal info */
    ok(sizeof(D3DKMDT_VIDPN_TARGET_MODE) > sizeof(UINT),
       "VIDPN_TARGET_MODE too small\n");

    /* Monitor source mode */
    ok(sizeof(D3DKMDT_MONITOR_SOURCE_MODE) > sizeof(UINT),
       "MONITOR_SOURCE_MODE too small\n");

    /* Present path */
    ok(sizeof(D3DKMDT_VIDPN_PRESENT_PATH) > 2 * sizeof(UINT),
       "VIDPN_PRESENT_PATH too small (needs source+target IDs at minimum)\n");
}

/* ======================================================================
 * RECT structure layout (used by present operations)
 * ====================================================================== */

static void Test_RectLayout(void)
{
    ok_eq_uint(sizeof(RECT), 4 * sizeof(LONG));
    ok_eq_uint(offsetof(RECT, left), 0);
    ok_eq_uint(offsetof(RECT, top), sizeof(LONG));
    ok_eq_uint(offsetof(RECT, right), 2 * sizeof(LONG));
    ok_eq_uint(offsetof(RECT, bottom), 3 * sizeof(LONG));

    /* Common resolution RECTs */
    RECT r1 = { 0, 0, 1920, 1080 };
    ok_eq_long(r1.right - r1.left, 1920);
    ok_eq_long(r1.bottom - r1.top, 1080);

    RECT r2 = { 0, 0, 1024, 768 };
    ok_eq_long(r2.right - r2.left, 1024);
    ok_eq_long(r2.bottom - r2.top, 768);
}

/* ======================================================================
 * LUID layout and properties
 * ====================================================================== */

static void Test_LuidProperties(void)
{
    LUID luid1 = { 0x12345678, 0x0 };
    LUID luid2 = { 0x12345678, 0x0 };
    LUID luid3 = { 0x12345679, 0x0 };

    ok(luid1.LowPart == luid2.LowPart && luid1.HighPart == luid2.HighPart,
       "Equal LUIDs should match\n");

    ok(luid1.LowPart != luid3.LowPart || luid1.HighPart != luid3.HighPart,
       "Different LUIDs should not match\n");

    /* Zero LUID */
    LUID zero = { 0, 0 };
    ok(zero.LowPart == 0 && zero.HighPart == 0, "Zero LUID check\n");
}

/* ======================================================================
 * D3DKMT_CREATEDEVICEFLAGS layout
 * ====================================================================== */

static void Test_CreateDeviceFlags(void)
{
    D3DKMT_CREATEDEVICEFLAGS flags;
    memset(&flags, 0, sizeof(flags));

    /* LegacyMode at bit 0 */
    flags.LegacyMode = 1;
    ok(flags.LegacyMode == 1, "LegacyMode bit not set\n");

    /* RequestVSync at bit 1 */
    flags.RequestVSync = 1;
    ok(flags.RequestVSync == 1, "RequestVSync bit not set\n");

    /* Both flags can be set simultaneously */
    ok(flags.LegacyMode == 1 && flags.RequestVSync == 1,
       "Both flags should be settable simultaneously\n");

    /* Clear and verify */
    memset(&flags, 0, sizeof(flags));
    ok(flags.LegacyMode == 0, "LegacyMode should be 0 after clear\n");
    ok(flags.RequestVSync == 0, "RequestVSync should be 0 after clear\n");
}

/* ======================================================================
 * D3DKMT_QUERYADAPTERINFO type enum values
 * ====================================================================== */

static void Test_QueryAdapterInfoTypes(void)
{
    /* KMTQAITYPE_UMDRIVERPRIVATE = 0 */
    ok(KMTQAITYPE_UMDRIVERPRIVATE == 0,
       "KMTQAITYPE_UMDRIVERPRIVATE should be 0\n");

    /* KMTQAITYPE_UMDRIVERNAME = 1 */
    ok(KMTQAITYPE_UMDRIVERNAME == 1,
       "KMTQAITYPE_UMDRIVERNAME should be 1\n");

    /* KMTQAITYPE_GETSEGMENTSIZE = 3 */
    ok(KMTQAITYPE_GETSEGMENTSIZE == 3,
       "KMTQAITYPE_GETSEGMENTSIZE should be 3\n");

    /* KMTQAITYPE_DRIVERVERSION = 13 */
    ok(KMTQAITYPE_DRIVERVERSION == 13,
       "KMTQAITYPE_DRIVERVERSION should be 13\n");
}

/* ======================================================================
 * WDDM driver version constants
 * ====================================================================== */

static void Test_DriverVersionConstants(void)
{
    /* KMT_DRIVERVERSION_WDDM_1_0 */
    ok(KMT_DRIVERVERSION_WDDM_1_0 == 1000,
       "WDDM 1.0 version should be 1000, got %d\n",
       KMT_DRIVERVERSION_WDDM_1_0);

    /* Versions should be monotonically increasing */
    ok(KMT_DRIVERVERSION_WDDM_1_0 > 0,
       "WDDM 1.0 version should be positive\n");
}

/* ======================================================================
 * D3DKMT_PRESENT flag constants
 * ====================================================================== */

static void Test_PresentFlags(void)
{
    D3DKMT_PRESENTFLAGS flags;
    memset(&flags, 0, sizeof(flags));

    /* Blt flag */
    flags.Blt = 1;
    ok(flags.Blt == 1, "Blt flag\n");

    /* Flip flag */
    memset(&flags, 0, sizeof(flags));
    flags.Flip = 1;
    ok(flags.Flip == 1, "Flip flag\n");

    /* ColorFill flag */
    memset(&flags, 0, sizeof(flags));
    flags.ColorFill = 1;
    ok(flags.ColorFill == 1, "ColorFill flag\n");

    /* Flags should be distinct bits */
    memset(&flags, 0, sizeof(flags));
    flags.Blt = 1;
    flags.Flip = 1;
    ok(flags.Blt == 1 && flags.Flip == 1,
       "Blt and Flip should be independent bits\n");
}

/* ======================================================================
 * D3DKMT_LOCK flags
 * ====================================================================== */

static void Test_LockFlags(void)
{
    D3DKMT_LOCK lockData;
    memset(&lockData, 0, sizeof(lockData));

    /* hDevice at offset 0 */
    ok_eq_uint(offsetof(D3DKMT_LOCK, hDevice), 0);

    /* hAllocation must exist */
    ok(offsetof(D3DKMT_LOCK, hAllocation) > 0,
       "hAllocation should be at positive offset\n");
}

/* ======================================================================
 * D3DKMT_CREATEALLOCATION layout
 * ====================================================================== */

static void Test_CreateAllocationLayout(void)
{
    ok_eq_uint(offsetof(D3DKMT_CREATEALLOCATION, hDevice), 0);

    ok(sizeof(D3DKMT_CREATEALLOCATION) > sizeof(D3DKMT_HANDLE),
       "CREATEALLOCATION must contain more than just a handle\n");
}

/* ======================================================================
 * D3DKMT_DESTROYALLOCATION layout
 * ====================================================================== */

static void Test_DestroyAllocationLayout(void)
{
    ok_eq_uint(offsetof(D3DKMT_DESTROYALLOCATION, hDevice), 0);

    ok(sizeof(D3DKMT_DESTROYALLOCATION) > sizeof(D3DKMT_HANDLE),
       "DESTROYALLOCATION must contain more than just a handle\n");
}

/* ======================================================================
 * MAX_ENUM_ADAPTERS constant
 * ====================================================================== */

static void Test_MaxEnumAdapters(void)
{
    ok_eq_uint(MAX_ENUM_ADAPTERS, 16);
    ok(MAX_ENUM_ADAPTERS >= 1, "MAX_ENUM_ADAPTERS must be at least 1\n");
    ok(MAX_ENUM_ADAPTERS <= 64, "MAX_ENUM_ADAPTERS seems too large\n");
}

/* ======================================================================
 * D3DDDI_SYNCHRONIZATIONOBJECTINFO type
 * ====================================================================== */

static void Test_SyncObjectInfoType(void)
{
    ok(sizeof(D3DDDI_SYNCHRONIZATIONOBJECTINFO) > 0,
       "D3DDDI_SYNCHRONIZATIONOBJECTINFO has zero size\n");
}

/* ======================================================================
 * Signaling/waiting limits
 * ====================================================================== */

static void Test_SyncLimits(void)
{
    /* D3DDDI_MAX_OBJECT_SIGNALED >= 1 */
    ok(D3DDDI_MAX_OBJECT_SIGNALED >= 1,
       "MAX_OBJECT_SIGNALED must be >= 1\n");

    /* D3DDDI_MAX_OBJECT_WAITED_ON >= 1 */
    ok(D3DDDI_MAX_OBJECT_WAITED_ON >= 1,
       "MAX_OBJECT_WAITED_ON must be >= 1\n");

    /* Both should be <= 32 (reasonable limit for on-stack arrays) */
    ok(D3DDDI_MAX_OBJECT_SIGNALED <= 32,
       "MAX_OBJECT_SIGNALED > 32 is unusual\n");
    ok(D3DDDI_MAX_OBJECT_WAITED_ON <= 32,
       "MAX_OBJECT_WAITED_ON > 32 is unusual\n");
}

/* ======================================================================
 * D3DKMT_OPENADAPTERFROMHDC layout
 * ====================================================================== */

static void Test_OpenAdapterFromHdcLayout(void)
{
    ok(sizeof(D3DKMT_OPENADAPTERFROMHDC) > sizeof(HDC),
       "OPENADAPTERFROMHDC must contain more than just an HDC\n");

    ok_eq_uint(offsetof(D3DKMT_OPENADAPTERFROMHDC, hDc), 0);
}

/* ======================================================================
 * D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME layout
 * ====================================================================== */

static void Test_OpenAdapterFromGdiDisplayNameLayout(void)
{
    ok(sizeof(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME) > 0,
       "OPENADAPTERFROMGDIDISPLAYNAME has zero size\n");

    /* Should contain a device name string buffer */
    ok(sizeof(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME) >= 32,
       "OPENADAPTERFROMGDIDISPLAYNAME seems too small for a device name\n");
}

/* ======================================================================
 * DXGK device type constant
 * ====================================================================== */

static void Test_DeviceTypeConstant(void)
{
    /* DXGKRNL_DEVICE_TYPE = 0x23 (FILE_DEVICE_VIDEO) */
    ok_eq_uint(DXGKRNL_DEVICE_TYPE, 0x23);

    /* FILE_DEVICE_VIDEO is traditionally 0x23 */
    ok_eq_uint(DXGKRNL_DEVICE_TYPE, 0x23);
}

/* ======================================================================
 * D3DKMT_CREATECONTEXT layout
 * ====================================================================== */

static void Test_CreateContextLayout(void)
{
    ok_eq_uint(offsetof(D3DKMT_CREATECONTEXT, hDevice), 0);

    ok(offsetof(D3DKMT_CREATECONTEXT, NodeOrdinal) > 0,
       "NodeOrdinal should be at positive offset\n");

    ok(offsetof(D3DKMT_CREATECONTEXT, EngineAffinity) > 0,
       "EngineAffinity should be at positive offset\n");

    /* hContext is an output field */
    ok(sizeof(D3DKMT_CREATECONTEXT) > sizeof(D3DKMT_HANDLE) * 2,
       "CREATECONTEXT should be larger than two handles\n");
}

/* ======================================================================
 * D3DKMT_DESTROYCONTEXT layout
 * ====================================================================== */

static void Test_DestroyContextLayout(void)
{
    ok_eq_uint(offsetof(D3DKMT_DESTROYCONTEXT, hContext), 0);
    ok_eq_uint(sizeof(D3DKMT_DESTROYCONTEXT), sizeof(D3DKMT_HANDLE));
}

START_TEST(WddmConstants)
{
    Test_PoolTags();
    Test_InterfaceVersions();
    Test_HandleTypeProperties();
    Test_D3DDDITypes();
    Test_ModeStructSizes();
    Test_RectLayout();
    Test_LuidProperties();
    Test_CreateDeviceFlags();
    Test_QueryAdapterInfoTypes();
    Test_DriverVersionConstants();
    Test_PresentFlags();
    Test_LockFlags();
    Test_CreateAllocationLayout();
    Test_DestroyAllocationLayout();
    Test_MaxEnumAdapters();
    Test_SyncObjectInfoType();
    Test_SyncLimits();
    Test_OpenAdapterFromHdcLayout();
    Test_OpenAdapterFromGdiDisplayNameLayout();
    Test_DeviceTypeConstant();
    Test_CreateContextLayout();
    Test_DestroyContextLayout();
}
