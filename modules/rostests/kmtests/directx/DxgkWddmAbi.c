/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     WDDM tier selector and interrupt-control ABI tests
 */

#include <kmt_test.h>
#include <windef.h>

/*
 * This test consumes the audited WDDM 3.2 declaration ceiling independently
 * of the OS-wide legacy default used by unrelated kmtests.
 */
#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
#include <dispmprt.h>

START_TEST(DxgkWddmAbi)
{
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_4, 0x9006);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_5, 0xA00B);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_6, 0xB004);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_7, 0xC004);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_8, 0xD001);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_9, 0xE003);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM3_0, 0xF003);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM3_1, 0x10004);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM3_2, 0x11007);

    ok_eq_ulong(DXGKDDI_WDDMv2_4_ENUM, 0x2400);
    ok_eq_ulong(DXGKDDI_WDDMv2_5_ENUM, 0x2500);
    ok_eq_ulong(DXGKDDI_WDDMv2_6_ENUM, 0x2600);
    ok_eq_ulong(DXGKDDI_WDDMv2_7_ENUM, 0x2700);
    ok_eq_ulong(DXGKDDI_WDDMv2_8_ENUM, 0x2800);
    ok_eq_ulong(DXGKDDI_WDDMv2_9_ENUM, 0x2900);
    ok_eq_ulong(DXGKDDI_WDDMv3_0_ENUM, 0x3000);
    ok_eq_ulong(DXGKDDI_WDDMv3_1_ENUM, 0x3100);
    ok_eq_ulong(DXGKDDI_WDDMv3_2_ENUM, 0x3200);

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
    {
        D3DDDI_SYNCHRONIZATIONOBJECT_FLAGS Flags;

        RtlZeroMemory(&Flags, sizeof(Flags));
        Flags.SignalByKmd = 1;
        ok_eq_ulong(Flags.Value, 0x00000100);
    }

    ok_eq_ulong(D3DDDI_DRIVERESCAPETYPE_CPUEVENTUSAGE, 2);
    ok_eq_ulong(DXGK_DRIVER_FEATURE_KMD_SIGNAL_CPU_EVENT, 3);
    ok_eq_ulong(DXGK_FEATURE_KMD_SIGNAL_CPU_EVENT, 3);

    ok_eq_ulong(sizeof(D3DDDI_DRIVERESCAPE_CPUEVENTUSAGE), 0x30);
    ok_eq_ulong(FIELD_OFFSET(D3DDDI_DRIVERESCAPE_CPUEVENTUSAGE,
                             hKmdCpuEvent),
                0x8);
    ok_eq_ulong(FIELD_OFFSET(D3DDDI_DRIVERESCAPE_CPUEVENTUSAGE,
                             Usage),
                0x10);

    ok_eq_ulong(sizeof(DXGK_CREATECPUEVENTFLAGS), 0x4);
    ok_eq_ulong(FIELD_OFFSET(DXGKARG_CREATECPUEVENT, Flags),
                2 * sizeof(HANDLE));
#ifdef _WIN64
    ok_eq_ulong(sizeof(DXGKARG_CREATECPUEVENT), 0x20);
    ok_eq_ulong(FIELD_OFFSET(DXGKARG_CREATECPUEVENT, hKmdCpuEvent),
                0x18);
    ok_eq_ulong(sizeof(DXGKARGCB_SIGNALEVENT), 0x18);
    ok_eq_ulong(FIELD_OFFSET(DXGKARGCB_SIGNALEVENT, Flags), 0x10);
#else
    ok_eq_ulong(sizeof(DXGKARG_CREATECPUEVENT), 0x10);
    ok_eq_ulong(FIELD_OFFSET(DXGKARG_CREATECPUEVENT, hKmdCpuEvent),
                0xC);
    ok_eq_ulong(sizeof(DXGKARGCB_SIGNALEVENT), 0xC);
    ok_eq_ulong(FIELD_OFFSET(DXGKARGCB_SIGNALEVENT, Flags), 0x8);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
    ok_eq_ulong(DXGK_FEATURE_INTERFACE_VERSION_1, 1);
    ok_eq_ulong(sizeof(DXGKARG_QUERYFEATURESUPPORT), 0xC);
    ok_eq_ulong(sizeof(DXGKARGCB_ISFEATUREENABLED2_FLAGS), 0x4);
    ok_eq_ulong(sizeof(DXGKARGCB_ISFEATUREENABLED2), 0xC);
    ok_eq_ulong(FIELD_OFFSET(DXGKARGCB_ISFEATUREENABLED2, Flags), 0x4);
    ok_eq_ulong(FIELD_OFFSET(DXGKARGCB_ISFEATUREENABLED2, Result), 0x8);

#ifdef _WIN64
    ok_eq_ulong(sizeof(DXGK_FEATURE_INTERFACE), 0x30);
    ok_eq_ulong(sizeof(DXGKDDI_FEATURE_INTERFACE), 0x30);
    ok_eq_ulong(FIELD_OFFSET(DXGK_FEATURE_INTERFACE,
                             IsFeatureEnabled),
                0x20);
    ok_eq_ulong(FIELD_OFFSET(DXGK_FEATURE_INTERFACE,
                             QueryFeatureInterface),
                0x28);
    ok_eq_ulong(sizeof(DXGKARGCB_QUERYFEATUREINTERFACE), 0x10);
    ok_eq_ulong(FIELD_OFFSET(DXGKARGCB_QUERYFEATUREINTERFACE,
                             Interface),
                0x8);
#else
    ok_eq_ulong(sizeof(DXGK_FEATURE_INTERFACE), 0x18);
    ok_eq_ulong(sizeof(DXGKDDI_FEATURE_INTERFACE), 0x18);
    ok_eq_ulong(FIELD_OFFSET(DXGK_FEATURE_INTERFACE,
                             IsFeatureEnabled),
                0x10);
    ok_eq_ulong(FIELD_OFFSET(DXGK_FEATURE_INTERFACE,
                             QueryFeatureInterface),
                0x14);
    ok_eq_ulong(sizeof(DXGKARGCB_QUERYFEATUREINTERFACE), 0xC);
    ok_eq_ulong(FIELD_OFFSET(DXGKARGCB_QUERYFEATUREINTERFACE,
                             Interface),
                0x8);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    ok_eq_ulong(sizeof(DXGKARG_CONTROLINTERRUPT2), 8);
    ok_eq_ulong(FIELD_OFFSET(DXGKARG_CONTROLINTERRUPT2, InterruptType), 0);
    ok_eq_ulong(FIELD_OFFSET(DXGKARG_CONTROLINTERRUPT2, InterruptState), 4);
    ok_eq_ulong(FIELD_OFFSET(DXGKARG_CONTROLINTERRUPT2, CrtcVsyncState), 4);
    ok_eq_ulong(DXGK_INTERRUPT_ENABLE, 0);
    ok_eq_ulong(DXGK_INTERRUPT_DISABLE, 1);
    ok_eq_ulong(DXGK_VSYNC_ENABLE, 0);
    ok_eq_ulong(DXGK_VSYNC_DISABLE_KEEP_PHASE, 1);
    ok_eq_ulong(DXGK_VSYNC_DISABLE_NO_PHASE, 2);
#endif
}

/* EOF */
