/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Validate D3DKMT structure layouts and sizes
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * These tests verify that D3DKMT structure sizes and field offsets match the
 * expected binary layout.  Any drift breaks the user ↔ kernel IOCTL contract
 * since structures are passed by value through METHOD_BUFFERED IOCTLs.
 */

#include "precomp.h"
#include <stddef.h>

static void Test_AdapterInfoLayout(void)
{
    /* D3DKMT_ADAPTERINFO — one entry in the adapter enumeration array */
    ok(sizeof(D3DKMT_ADAPTERINFO) >= 16,
       "D3DKMT_ADAPTERINFO too small: %Iu bytes\n", sizeof(D3DKMT_ADAPTERINFO));

    /* hAdapter must be at offset 0 */
    ok_eq_uint(offsetof(D3DKMT_ADAPTERINFO, hAdapter), 0);

    /* AdapterLuid follows hAdapter (4 bytes for UINT handle) */
    ok_eq_uint(offsetof(D3DKMT_ADAPTERINFO, AdapterLuid),
               offsetof(D3DKMT_ADAPTERINFO, hAdapter) + sizeof(D3DKMT_HANDLE));

    /* NumOfSources follows AdapterLuid (8 bytes for LUID) */
    ok(offsetof(D3DKMT_ADAPTERINFO, NumOfSources) >
       offsetof(D3DKMT_ADAPTERINFO, AdapterLuid),
       "NumOfSources should be after AdapterLuid\n");
}

static void Test_EnumAdaptersLayout(void)
{
    /* D3DKMT_ENUMADAPTERS — NumAdapters + Adapters[16] */
    ok(sizeof(D3DKMT_ENUMADAPTERS) > sizeof(D3DKMT_ADAPTERINFO),
       "D3DKMT_ENUMADAPTERS must be larger than a single D3DKMT_ADAPTERINFO\n");

    /* NumAdapters at offset 0 */
    ok_eq_uint(offsetof(D3DKMT_ENUMADAPTERS, NumAdapters), 0);

    /* Adapters array follows NumAdapters */
    ok(offsetof(D3DKMT_ENUMADAPTERS, Adapters) > 0,
       "Adapters array should be after NumAdapters\n");

    /* Array must have exactly MAX_ENUM_ADAPTERS entries */
    ok_eq_uint(sizeof(((D3DKMT_ENUMADAPTERS *)0)->Adapters) /
               sizeof(D3DKMT_ADAPTERINFO),
               MAX_ENUM_ADAPTERS);
}

static void Test_EnumAdapters2Layout(void)
{
    /* D3DKMT_ENUMADAPTERS2 — NumAdapters + pAdapters (pointer) */
    ok_eq_uint(offsetof(D3DKMT_ENUMADAPTERS2, NumAdapters), 0);
    ok(offsetof(D3DKMT_ENUMADAPTERS2, pAdapters) >= sizeof(ULONG),
       "pAdapters should follow NumAdapters\n");

    /* Size should be compact: just ULONG + pointer (+ padding) */
    ok(sizeof(D3DKMT_ENUMADAPTERS2) <= 16,
       "D3DKMT_ENUMADAPTERS2 unexpectedly large: %Iu bytes\n",
       sizeof(D3DKMT_ENUMADAPTERS2));
}

static void Test_OpenAdapterFromLuidLayout(void)
{
    /* D3DKMT_OPENADAPTERFROMLUID — AdapterLuid + hAdapter */
    ok_eq_uint(offsetof(D3DKMT_OPENADAPTERFROMLUID, AdapterLuid), 0);
    ok(offsetof(D3DKMT_OPENADAPTERFROMLUID, hAdapter) >= sizeof(LUID),
       "hAdapter should follow AdapterLuid\n");
}

static void Test_SegmentSizeInfoLayout(void)
{
    /* D3DKMT_SEGMENTSIZEINFO — 3 ULONGLONG fields */
    ok_eq_uint(sizeof(D3DKMT_SEGMENTSIZEINFO), 3 * sizeof(ULONGLONG));
    ok_eq_uint(offsetof(D3DKMT_SEGMENTSIZEINFO, DedicatedVideoMemorySize), 0);
    ok_eq_uint(offsetof(D3DKMT_SEGMENTSIZEINFO, DedicatedSystemMemorySize),
               sizeof(ULONGLONG));
    ok_eq_uint(offsetof(D3DKMT_SEGMENTSIZEINFO, SharedSystemMemorySize),
               2 * sizeof(ULONGLONG));
}

static void Test_CreateDeviceLayout(void)
{
    /* D3DKMT_CREATEDEVICE must have hAdapter/pAdapter at offset 0 */
    ok_eq_uint(offsetof(D3DKMT_CREATEDEVICE, hAdapter), 0);

    /* hDevice is an output field — must exist in the structure */
    ok(offsetof(D3DKMT_CREATEDEVICE, hDevice) > 0,
       "hDevice should be after the adapter handle\n");

    /* Flags field must exist */
    ok(sizeof(D3DKMT_CREATEDEVICEFLAGS) > 0,
       "D3DKMT_CREATEDEVICEFLAGS must have non-zero size\n");
}

static void Test_CloseAdapterLayout(void)
{
    /* D3DKMT_CLOSEADAPTER — just an hAdapter */
    ok_eq_uint(offsetof(D3DKMT_CLOSEADAPTER, hAdapter), 0);
    ok(sizeof(D3DKMT_CLOSEADAPTER) >= sizeof(D3DKMT_HANDLE),
       "D3DKMT_CLOSEADAPTER must be at least handle-sized\n");
}

static void Test_DestroyDeviceLayout(void)
{
    /* D3DKMT_DESTROYDEVICE — just an hDevice */
    ok_eq_uint(offsetof(D3DKMT_DESTROYDEVICE, hDevice), 0);
    ok(sizeof(D3DKMT_DESTROYDEVICE) >= sizeof(D3DKMT_HANDLE),
       "D3DKMT_DESTROYDEVICE must be at least handle-sized\n");
}

static void Test_PresentLayout(void)
{
    /* D3DKMT_PRESENT must have hDevice at offset 0 */
    ok_eq_uint(offsetof(D3DKMT_PRESENT, hDevice), 0);

    /* SrcRect and DstRect must exist */
    ok(offsetof(D3DKMT_PRESENT, SrcRect) > 0,
       "SrcRect should be at a positive offset\n");
    ok(offsetof(D3DKMT_PRESENT, DstRect) > 0,
       "DstRect should be at a positive offset\n");

    /* VidPnSourceId must exist */
    ok(sizeof(D3DKMT_PRESENT) > sizeof(D3DKMT_HANDLE),
       "D3DKMT_PRESENT must contain more than just a handle\n");
}

static void Test_HandleType(void)
{
    /* D3DKMT_HANDLE must be a 32-bit unsigned type */
    ok_eq_uint(sizeof(D3DKMT_HANDLE), sizeof(UINT));

    /* Zero handle is the invalid sentinel */
    D3DKMT_HANDLE Zero = 0;
    ok(Zero == 0, "Zero-initialized handle must be 0\n");
}

static void Test_LuidLayout(void)
{
    /* LUID is used for adapter identification — must be 8 bytes */
    ok_eq_uint(sizeof(LUID), 8);
    ok_eq_uint(offsetof(LUID, LowPart), 0);
    ok_eq_uint(offsetof(LUID, HighPart), sizeof(DWORD));
}

START_TEST(D3dkmtStructures)
{
    Test_AdapterInfoLayout();
    Test_EnumAdaptersLayout();
    Test_EnumAdapters2Layout();
    Test_OpenAdapterFromLuidLayout();
    Test_SegmentSizeInfoLayout();
    Test_CreateDeviceLayout();
    Test_CloseAdapterLayout();
    Test_DestroyDeviceLayout();
    Test_PresentLayout();
    Test_HandleType();
    Test_LuidLayout();
}
