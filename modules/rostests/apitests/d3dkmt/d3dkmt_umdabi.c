/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UMD DDI table ABI (roadmap gate 1.3)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * The two tables in d3dumddi.h are dispatch tables: the runtime indexes them by
 * slot, so a member in the wrong position sends a call to the wrong driver
 * function with the wrong argument type. That does not fail at build time and
 * it does not fail at load time -- it fails as a wild call the first time the
 * runtime reaches that entry.
 *
 * So what is pinned here is position and count, not behaviour. The counts *are*
 * the ABI at D3D_UMD_INTERFACE_VERSION_WDDM2_0; anything that shifts a slot
 * changes one of these numbers.
 */

#include "precomp.h"
#include <d3dumddi.h>

/* Every slot in both tables is one pointer, so size / pointer size is the
 * entry count. */
#define UMD_DEVICEFUNC_SLOTS     (sizeof(D3DDDI_DEVICEFUNCS) / sizeof(void *))
#define UMD_DEVICECALLBACK_SLOTS (sizeof(D3DDDI_DEVICECALLBACKS) / sizeof(void *))

C_ASSERT(sizeof(D3DDDI_ADAPTERFUNCS) == 3 * sizeof(void *));
C_ASSERT(sizeof(D3DDDI_ADAPTERCALLBACKS) == 2 * sizeof(void *));

/* The first slot of each table is load-bearing: it is the one a driver that has
 * the layout wrong still appears to fill correctly. */
C_ASSERT(FIELD_OFFSET(D3DDDI_DEVICEFUNCS, pfnSetRenderState) == 0);
C_ASSERT(FIELD_OFFSET(D3DDDI_DEVICECALLBACKS, pfnAllocateCb) == 0);
C_ASSERT(FIELD_OFFSET(D3DDDIARG_OPENADAPTER, hAdapter) == 0);

static void Test_TableSlotCounts(void)
{
    /*
     * This ABI-freeze target is deliberately compiled at
     * D3D_UMD_INTERFACE_VERSION_WDDM2_0, the current in-tree implementation
     * ceiling.  Lower configured images compile their runtime and UMD at their
     * exact shorter contract; higher images continue to advertise this 2.0
     * table until their newer callback tails are implemented.
     */
    ok_eq_ulong((ULONG)UMD_DEVICEFUNC_SLOTS, 140UL);
    ok_eq_ulong((ULONG)UMD_DEVICECALLBACK_SLOTS, 50UL);
    /* These are the slice the *effective* version selects, traced below. */
    ok_eq_ulong((ULONG)(sizeof(D3DDDI_ADAPTERFUNCS) / sizeof(void *)), 3UL);
    ok_eq_ulong((ULONG)(sizeof(D3DDDI_ADAPTERCALLBACKS) / sizeof(void *)), 2UL);
    trace("UMD tables: %lu driver entries, %lu runtime callbacks\n",
          (unsigned long)UMD_DEVICEFUNC_SLOTS, (unsigned long)UMD_DEVICECALLBACK_SLOTS);
}

static void Test_TablesAreAllPointers(void)
{
    /* A non-pointer member would break the size/pointer identity the slot count
     * rests on, and misalign everything after it. */
    ok((sizeof(D3DDDI_DEVICEFUNCS) % sizeof(void *)) == 0,
       "D3DDDI_DEVICEFUNCS is not pointer-sized\n");
    ok((sizeof(D3DDDI_DEVICECALLBACKS) % sizeof(void *)) == 0,
       "D3DDDI_DEVICECALLBACKS is not pointer-sized\n");
}

static void Test_EntrySurfaceShape(void)
{
    D3DDDIARG_OPENADAPTER Open;
    D3DDDIARG_CREATEDEVICE Create;

    memset(&Open, 0, sizeof(Open));
    memset(&Create, 0, sizeof(Create));

    /* The driver reports the version it was compiled against; the runtime uses
     * it to decide how much of each table to read.  Reporting nothing there is
     * how a driver gets its tail treated as garbage. */
    Open.DriverVersion = D3D_UMD_INTERFACE_VERSION;
    ok_eq_ulong((ULONG)D3D_UMD_INTERFACE_VERSION_WDDM2_0, 0x5002UL);

    /*
     * Report the compile-time contract as well as pinning its table shape.
     * CMake selects 0x5002 specifically for this ABI-freeze test; runtime-load
     * coverage separately receives the configured image's expected version.
     */
    trace("effective D3D_UMD_INTERFACE_VERSION = 0x%04X (WDDM2_0 = 0x%04X, WDDM3_2 = 0x%04X)\n",
          (unsigned)D3D_UMD_INTERFACE_VERSION,
          (unsigned)D3D_UMD_INTERFACE_VERSION_WDDM2_0,
          (unsigned)D3D_UMD_INTERFACE_VERSION_WDDM3_2);
    ok(Open.DriverVersion == D3D_UMD_INTERFACE_VERSION,
       "OpenAdapter cannot carry the compiled version\n");

    /* CreateDevice is where the runtime hands over the first command buffer and
     * both lists and takes the device table back.  All four must exist or a
     * driver cannot be written against the struct at all. */
    ok(FIELD_OFFSET(D3DDDIARG_CREATEDEVICE, pCommandBuffer) != 0, "no command buffer field\n");
    ok(FIELD_OFFSET(D3DDDIARG_CREATEDEVICE, pAllocationList) != 0, "no allocation list field\n");
    ok(FIELD_OFFSET(D3DDDIARG_CREATEDEVICE, pPatchLocationList) != 0, "no patch list field\n");
    ok(FIELD_OFFSET(D3DDDIARG_CREATEDEVICE, pDeviceFuncs) != 0, "no device function table field\n");

    /* Callbacks travel in, the function table travels out. */
    ok(FIELD_OFFSET(D3DDDIARG_CREATEDEVICE, pCallbacks) <
       FIELD_OFFSET(D3DDDIARG_CREATEDEVICE, pDeviceFuncs),
       "CreateDevice exchanges its tables in the wrong order\n");
    ok(FIELD_OFFSET(D3DDDIARG_OPENADAPTER, pAdapterCallbacks) <
       FIELD_OFFSET(D3DDDIARG_OPENADAPTER, pAdapterFuncs),
       "OpenAdapter exchanges its tables in the wrong order\n");
}

static void Test_ExportNameIsTheOneTheRuntimeResolves(void)
{
    /* The runtime resolves this by name; a driver exporting anything else is
     * simply not loaded, with no diagnostic. */
    ok(strcmp(D3DDDI_OPENADAPTER_PROCNAME, "OpenAdapter10_2") == 0,
       "UMD entry point name is \"%s\", not OpenAdapter10_2\n", D3DDDI_OPENADAPTER_PROCNAME);
}

START_TEST(umdabi)
{
    Test_TableSlotCounts();
    Test_TablesAreAllPointers();
    Test_EntrySurfaceShape();
    Test_ExportNameIsTheOneTheRuntimeResolves();
}

/* EOF */
