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
#define DXGKDDI_INTERFACE_VERSION 0x11008
#include <dispmprt.h>
#include "adapter_map_core.h"
#include "adapter_start_core.h"

typedef struct _DXGK_TEST_RESOURCE_LIST
{
    CM_RESOURCE_LIST Resources;
    CM_PARTIAL_RESOURCE_DESCRIPTOR ExtraDescriptors[2];
} DXGK_TEST_RESOURCE_LIST;

static VOID
TestMapMemoryContractCore(VOID)
{
    DXGK_TEST_RESOURCE_LIST TestList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptors;
    PHYSICAL_ADDRESS Address;
    PVOID AdapterA = (PVOID)(ULONG_PTR)0x1000;
    PVOID AdapterB = (PVOID)(ULONG_PTR)0x2000;
    PVOID ProcessA = (PVOID)(ULONG_PTR)0x3000;
    PVOID ProcessB = (PVOID)(ULONG_PTR)0x4000;

    RtlZeroMemory(&TestList, sizeof(TestList));
    TestList.Resources.Count = 1;
    TestList.Resources.List[0].PartialResourceList.Count = 3;
    Descriptors = &TestList.Resources.List[0].PartialResourceList.PartialDescriptors[0];

    Descriptors[0].Type = CmResourceTypeMemory;
    Descriptors[0].u.Memory.Start.QuadPart = 0x100000;
    Descriptors[0].u.Memory.Length = 0x2000;

    Descriptors[1].Type = CmResourceTypePort;
    Descriptors[1].Flags = CM_RESOURCE_PORT_IO;
    Descriptors[1].u.Port.Start.QuadPart = 0x3C0;
    Descriptors[1].u.Port.Length = 0x20;

    Descriptors[2].Type = CmResourceTypeMemoryLarge;
    Descriptors[2].Flags = CM_RESOURCE_MEMORY_LARGE_40;
    Descriptors[2].u.Memory40.Start.QuadPart = 0x100000000LL;
    Descriptors[2].u.Memory40.Length40 = 0x20;

    Address.QuadPart = 0x100000;
    ok(DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 0x2000, FALSE), "Exact memory resource was rejected\n");
    Address.QuadPart = 0x100800;
    ok(DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 0x800, FALSE), "Contained memory range was rejected\n");
    Address.QuadPart = 0x0FFFFF;
    ok(!DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 1, FALSE), "Range before resource was accepted\n");
    Address.QuadPart = 0x101800;
    ok(!DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 0x1000, FALSE), "Range escaping resource was accepted\n");
    Address.QuadPart = 0x100000;
    ok(!DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 0x100, TRUE), "Memory resource was accepted as I/O space\n");

    Address.QuadPart = 0x3C0;
    ok(DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 0x20, TRUE), "Exact I/O-port resource was rejected\n");
    ok(!DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 0x21, TRUE), "I/O-port range escape was accepted\n");

    Address.QuadPart = 0x100000000LL;
    ok(DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 0x2000, FALSE), "Large-memory resource was decoded incorrectly\n");
    Address.QuadPart = -1;
    ok(!DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 1, FALSE), "Negative physical address was accepted\n");
    Address.QuadPart = 0x100000;
    ok(!DxgkAdapterMapRangeAssigned(NULL, Address, 1, FALSE), "NULL resource list was accepted\n");
    ok(!DxgkAdapterMapRangeAssigned(&TestList.Resources, Address, 0, FALSE), "Zero-length range was accepted\n");

    ok(DxgkAdapterMapOwnerMatches(AdapterA, NULL, AdapterA, ProcessA), "Kernel mapping rejected its adapter owner\n");
    ok(!DxgkAdapterMapOwnerMatches(AdapterA, NULL, AdapterB, ProcessA), "Kernel mapping accepted a foreign adapter\n");
    ok(DxgkAdapterMapOwnerMatches(AdapterA, ProcessA, AdapterA, ProcessA), "User mapping rejected its adapter/process owner\n");
    ok(!DxgkAdapterMapOwnerMatches(AdapterA, ProcessA, AdapterA, ProcessB), "User mapping accepted a foreign process\n");
    ok(!DxgkAdapterMapOwnerMatches(AdapterA, ProcessA, AdapterB, ProcessA), "User mapping accepted a foreign adapter\n");
}

static VOID
TestAdapterStartRolePolicy(VOID)
{
    ok_eq_int(DxgkAdapterStartClassifyRole(TRUE, 1), DxgkAdapterStartDisplayOnly);
    ok_eq_int(DxgkAdapterStartClassifyRole(FALSE, 1), DxgkAdapterStartFullDisplay);
    ok_eq_int(DxgkAdapterStartClassifyRole(FALSE, 0), DxgkAdapterStartRenderOnly);
    ok(!DxgkAdapterStartRoleRequiresScheduler(DxgkAdapterStartDisplayOnly), "DOD unexpectedly requires VidSch\n");
    ok(DxgkAdapterStartRoleRequiresScheduler(DxgkAdapterStartFullDisplay), "full display adapter does not require VidSch\n");
    ok(DxgkAdapterStartRoleRequiresScheduler(DxgkAdapterStartRenderOnly), "render-only adapter does not require VidSch\n");
    ok(DxgkAdapterStartRoleRequiresDisplayPipeline(DxgkAdapterStartDisplayOnly), "DOD lacks a display pipeline\n");
    ok(DxgkAdapterStartRoleRequiresDisplayPipeline(DxgkAdapterStartFullDisplay), "full display adapter lacks a display pipeline\n");
    ok(!DxgkAdapterStartRoleRequiresDisplayPipeline(DxgkAdapterStartRenderOnly), "render-only adapter unexpectedly requires a display pipeline\n");
    ok(DxgkAdapterStartRoleHasValidCounts(DxgkAdapterStartDisplayOnly, 1, 0, 64), "valid DOD counts rejected\n");
    ok(!DxgkAdapterStartRoleHasValidCounts(DxgkAdapterStartDisplayOnly, 0, 0, 64), "source-less DOD accepted\n");
    ok(DxgkAdapterStartRoleHasValidCounts(DxgkAdapterStartFullDisplay, 1, 1, 64), "valid full-display counts rejected\n");
    ok(!DxgkAdapterStartRoleHasValidCounts(DxgkAdapterStartFullDisplay, 1, 0, 64), "scheduler-less full-display adapter accepted\n");
    ok(DxgkAdapterStartRoleHasValidCounts(DxgkAdapterStartRenderOnly, 0, 1, 64), "valid render-only counts rejected\n");
    ok(!DxgkAdapterStartRoleHasValidCounts(DxgkAdapterStartRenderOnly, 1, 1, 64), "render-only adapter with a display source accepted\n");
    ok(!DxgkAdapterStartRoleHasValidCounts(DxgkAdapterStartRenderOnly, 0, 65, 64), "out-of-range GPU node count accepted\n");
}

START_TEST(DxgkWddmAbi)
{
    TestMapMemoryContractCore();
    TestAdapterStartRolePolicy();

    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_4, 0x9006);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_5, 0xA00B);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_6, 0xB004);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_7, 0xC004);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_8, 0xD001);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM2_9, 0xE003);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM3_0, 0xF003);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM3_1, 0x10004);
    ok_eq_ulong(DXGKDDI_INTERFACE_VERSION_WDDM3_2, 0x11008);

    ok_eq_ulong(DXGKDDI_WDDMv2_4_ENUM, 0x2400);
    ok_eq_ulong(DXGKDDI_WDDMv2_5_ENUM, 0x2500);
    ok_eq_ulong(DXGKDDI_WDDMv2_6_ENUM, 0x2600);
    ok_eq_ulong(DXGKDDI_WDDMv2_7_ENUM, 0x2700);
    ok_eq_ulong(DXGKDDI_WDDMv2_8_ENUM, 0x2800);
    ok_eq_ulong(DXGKDDI_WDDMv2_9_ENUM, 0x2900);
    ok_eq_ulong(DXGKDDI_WDDMv3_0_ENUM, 0x3000);
    ok_eq_ulong(DXGKDDI_WDDMv3_1_ENUM, 0x3100);
    ok_eq_ulong(DXGKDDI_WDDMv3_2_ENUM, 0x3200);

    ok_eq_ulong(DxgkServicesAgp, 0);
    ok_eq_ulong(DxgkServicesDebugReport, 1);
    ok_eq_ulong(DXGK_DEBUG_REPORT_INTERFACE_VERSION_1, 1);
    ok_eq_ulong(DXGK_DEBUG_REPORT_MAX_SIZE, 0xF800);
#ifdef _WIN64
    ok_eq_ulong(sizeof(DXGK_DEBUG_REPORT_INTERFACE), 0x38);
    ok_eq_ulong(FIELD_OFFSET(DXGK_DEBUG_REPORT_INTERFACE,
                             DbgReportCreate),
                0x20);
    ok_eq_ulong(FIELD_OFFSET(DXGK_DEBUG_REPORT_INTERFACE,
                             DbgReportSecondaryData),
                0x28);
    ok_eq_ulong(FIELD_OFFSET(DXGK_DEBUG_REPORT_INTERFACE,
                             DbgReportComplete),
                0x30);
#else
    ok_eq_ulong(sizeof(DXGK_DEBUG_REPORT_INTERFACE), 0x1C);
    ok_eq_ulong(FIELD_OFFSET(DXGK_DEBUG_REPORT_INTERFACE,
                             DbgReportCreate),
                0x10);
    ok_eq_ulong(FIELD_OFFSET(DXGK_DEBUG_REPORT_INTERFACE,
                             DbgReportSecondaryData),
                0x14);
    ok_eq_ulong(FIELD_OFFSET(DXGK_DEBUG_REPORT_INTERFACE,
                             DbgReportComplete),
                0x18);
#endif

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
    /* WDK 28000 changed the official selector from 0x11007 to 0x11008 but
     * retained the WDDM 3.2 append-only initialization-table boundary. */
#ifdef _WIN64
    ok_eq_ulong(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA,
                             DxgkDdiResetDisplayEngine),
                0x600);
    ok_eq_ulong(sizeof(DRIVER_INITIALIZATION_DATA), 0x608);
#else
    ok_eq_ulong(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA,
                             DxgkDdiResetDisplayEngine),
                0x300);
    ok_eq_ulong(sizeof(DRIVER_INITIALIZATION_DATA), 0x304);
#endif

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
