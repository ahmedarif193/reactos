/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Engine node affinity masks
 *
 * A submission is steered to a node by an affinity mask.  A mask naming a node
 * the adapter does not have indexes past the engine array.
 */

#include <kmt_test.h>
#include "object_core.h"

static VOID TestValidation(VOID)
{
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0x1, 1); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0x1, 4); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0xF, 4); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0x8, 4); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* An empty affinity steers the submission nowhere at all. */
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0, 4); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Naming a node the adapter does not have would index past the engines. */
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0x10, 4); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0xFF, 4); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0x2, 1); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0x80000000UL, 8); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0x1, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkNodeCoreValidateAffinity(0x1, DXGK_NODE_CORE_MAX_NODES + 1); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestSelection(VOID)
{
    ULONG Node = 0xFFFFFFFF;

    /* Steering picks the lowest set node deterministically. */
    ok_bool_true(DxgkNodeCoreFirstNode(0x1, 4, &Node), "node 0");
    ok_eq_ulong(Node, 0UL);
    ok_bool_true(DxgkNodeCoreFirstNode(0x2, 4, &Node), "node 1");
    ok_eq_ulong(Node, 1UL);
    ok_bool_true(DxgkNodeCoreFirstNode(0x8, 4, &Node), "node 3");
    ok_eq_ulong(Node, 3UL);
    ok_bool_true(DxgkNodeCoreFirstNode(0xC, 4, &Node), "lowest of several");
    ok_eq_ulong(Node, 2UL);

    Node = 0xFFFFFFFF;
    ok_bool_false(DxgkNodeCoreFirstNode(0, 4, &Node), "empty mask selects nothing");
    ok_eq_ulong(Node, 0UL);
    ok_bool_false(DxgkNodeCoreFirstNode(0x10, 4, &Node), "out-of-range mask selects nothing");
}

static VOID TestCounting(VOID)
{
    { ULONG Observed = DxgkNodeCoreCountNodes(0x1, 4); ok_eq_ulong(Observed, 1UL); }
    { ULONG Observed = DxgkNodeCoreCountNodes(0x5, 4); ok_eq_ulong(Observed, 2UL); }
    { ULONG Observed = DxgkNodeCoreCountNodes(0xF, 4); ok_eq_ulong(Observed, 4UL); }
    { ULONG Observed = DxgkNodeCoreCountNodes(0xFF, 8); ok_eq_ulong(Observed, 8UL); }
    /* An invalid mask counts as nothing rather than reporting a bogus total. */
    { ULONG Observed = DxgkNodeCoreCountNodes(0, 4); ok_eq_ulong(Observed, 0UL); }
    { ULONG Observed = DxgkNodeCoreCountNodes(0x10, 4); ok_eq_ulong(Observed, 0UL); }
}

START_TEST(DxgkNodeAffinity)
{
    TestValidation();
    TestSelection();
    TestCounting();
}

/* EOF */
