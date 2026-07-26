/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     VidPN topology validation
 *
 * A topology says which framebuffer feeds which monitor.  Two sources on one
 * target is not clone mode, it is two writers to one scanout.
 */

#include <kmt_test.h>
#include "display_core.h"

static VOID TestAddAndFind(VOID)
{
    DXGK_VIDPN_TOPOLOGY Topology;
    ULONG SourceId = 0xFFFFFFFF;

    DxgkVidPnCoreTopologyInitialize(&Topology);
    ok_eq_ulong(Topology.PathCount, 0UL);

    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 0, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(Topology.PathCount, 1UL);
    ok_bool_true(DxgkVidPnCoreFindPath(&Topology, 0, 0), "path present");
    ok_bool_false(DxgkVidPnCoreFindPath(&Topology, 0, 1), "other target absent");

    ok_bool_true(DxgkVidPnCoreTargetIsDriven(&Topology, 0, &SourceId), "target driven");
    ok_eq_ulong(SourceId, 0UL);
    ok_bool_false(DxgkVidPnCoreTargetIsDriven(&Topology, 1, &SourceId), "target not driven");

    /* The same path twice is a caller bug, not a second display. */
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 0, 0); ok_eq_hex(Observed, STATUS_OBJECT_NAME_COLLISION); }
    ok_eq_ulong(Topology.PathCount, 1UL);
}

static VOID TestCloneAndConflict(VOID)
{
    DXGK_VIDPN_TOPOLOGY Topology;

    DxgkVidPnCoreTopologyInitialize(&Topology);

    /* One source driving several targets is clone mode and is allowed. */
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 0, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 0, 1); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 0, 2); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { ULONG Observed = DxgkVidPnCoreCountTargetsForSource(&Topology, 0); ok_eq_ulong(Observed, 3UL); }

    /*
     * A monitor takes its picture from exactly one source.  Two sources on one
     * target would have two framebuffers scanning out to the same display.
     */
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 1, 0); ok_eq_hex(Observed, STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY); }
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 1, 3); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { ULONG Observed = DxgkVidPnCoreCountTargetsForSource(&Topology, 1); ok_eq_ulong(Observed, 1UL); }
    { ULONG Observed = DxgkVidPnCoreCountTargetsForSource(&Topology, 9); ok_eq_ulong(Observed, 0UL); }
}

static VOID TestRemoval(VOID)
{
    DXGK_VIDPN_TOPOLOGY Topology;
    ULONG SourceId;

    DxgkVidPnCoreTopologyInitialize(&Topology);
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 0, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 0, 1); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 1, 2); ok_eq_hex(Observed, STATUS_SUCCESS); }

    { NTSTATUS Observed = DxgkVidPnCoreRemovePath(&Topology, 0, 1); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(Topology.PathCount, 2UL);
    ok_bool_false(DxgkVidPnCoreFindPath(&Topology, 0, 1), "removed");
    /* Removing the middle entry must not disturb the others. */
    ok_bool_true(DxgkVidPnCoreFindPath(&Topology, 0, 0), "first survives");
    ok_bool_true(DxgkVidPnCoreFindPath(&Topology, 1, 2), "last survives");

    { NTSTATUS Observed = DxgkVidPnCoreRemovePath(&Topology, 0, 1); ok_eq_hex(Observed, STATUS_NOT_FOUND); }

    /* A freed target may be driven by a different source afterwards. */
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 2, 1); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkVidPnCoreTargetIsDriven(&Topology, 1, &SourceId), "redriven");
    ok_eq_ulong(SourceId, 2UL);
}

static VOID TestValidation(VOID)
{
    DXGK_VIDPN_TOPOLOGY Topology;

    DxgkVidPnCoreTopologyInitialize(&Topology);
    { NTSTATUS Observed = DxgkVidPnCoreValidateTopology(&Topology, 2, 2); ok_eq_hex(Observed, STATUS_SUCCESS); }

    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 0, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 1, 1); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkVidPnCoreValidateTopology(&Topology, 2, 2); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* A path naming hardware the adapter does not have would index past the
     * source or target array the first time it is walked. */
    { NTSTATUS Observed = DxgkVidPnCoreValidateTopology(&Topology, 1, 2); ok_eq_hex(Observed, STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE); }
    { NTSTATUS Observed = DxgkVidPnCoreValidateTopology(&Topology, 2, 1); ok_eq_hex(Observed, STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET); }
}

static VOID TestCapacity(VOID)
{
    DXGK_VIDPN_TOPOLOGY Topology;
    ULONG Added;

    DxgkVidPnCoreTopologyInitialize(&Topology);
    for (Added = 0; Added < DXGK_VIDPN_CORE_MAX_PATHS; ++Added)
    {
        if (!NT_SUCCESS(DxgkVidPnCoreAddPath(&Topology, 0, Added)))
            break;
    }
    ok_eq_ulong(Added, (ULONG)DXGK_VIDPN_CORE_MAX_PATHS);
    { NTSTATUS Observed = DxgkVidPnCoreAddPath(&Topology, 0, 999); ok_eq_hex(Observed, STATUS_INSUFFICIENT_RESOURCES); }
}

START_TEST(DxgkVidPnTopology)
{
    TestAddAndFind();
    TestCloneAndConflict();
    TestRemoval();
    TestValidation();
    TestCapacity();
}

/* EOF */
