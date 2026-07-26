/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Graphics handle table: recycling, generations and type safety
 *
 * A handle table that hands a recycled slot back without a generation bump
 * lets a stale handle address whichever object now occupies the slot.  That
 * is the classic graphics-driver escape from a sandboxed process.
 */

#include <kmt_test.h>
#include "object_core.h"

static VOID TestAllocateAndResolve(VOID)
{
    DXGK_HANDLE_TABLE Table;
    ULONG DeviceObject = 0xD0;
    ULONG ContextObject = 0xC0;
    ULONG DeviceHandle = 0;
    ULONG ContextHandle = 0;
    PVOID Resolved = NULL;

    DxgkHandleCoreInitialize(&Table);
    { ULONG Observed = DxgkHandleCoreLiveCount(&Table); ok_eq_ulong(Observed, 0UL); }

    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, &DeviceObject, DxgkHandleTypeDevice, &DeviceHandle); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok(DeviceHandle != 0, "a handle is never zero\n");
    { ULONG Observed = DxgkHandleCoreLiveCount(&Table); ok_eq_ulong(Observed, 1UL); }

    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, &ContextObject, DxgkHandleTypeContext, &ContextHandle); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok(ContextHandle != DeviceHandle, "handles are distinct\n");

    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, DeviceHandle, DxgkHandleTypeDevice, &Resolved); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_pointer(Resolved, &DeviceObject);
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, ContextHandle, DxgkHandleTypeContext, &Resolved); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_pointer(Resolved, &ContextObject);

    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, NULL, DxgkHandleTypeDevice, &DeviceHandle); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, &DeviceObject, DxgkHandleTypeNone, &DeviceHandle); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, &DeviceObject, DxgkHandleTypeMax, &DeviceHandle); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestTypeSafety(VOID)
{
    DXGK_HANDLE_TABLE Table;
    ULONG Object = 0xAB;
    ULONG Handle = 0;
    PVOID Resolved = NULL;

    DxgkHandleCoreInitialize(&Table);
    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, &Object, DxgkHandleTypeAllocation, &Handle); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /*
     * Type confusion is as dangerous as a stale slot: resolving an allocation
     * handle as a context would reinterpret the object's memory.
     */
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, Handle, DxgkHandleTypeContext, &Resolved); ok_eq_hex(Observed, STATUS_OBJECT_TYPE_MISMATCH); }
    ok_eq_pointer(Resolved, NULL);
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, Handle, DxgkHandleTypeDevice, &Resolved); ok_eq_hex(Observed, STATUS_OBJECT_TYPE_MISMATCH); }
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, Handle, DxgkHandleTypeAllocation, &Resolved); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* Freeing under the wrong type must not destroy the object either. */
    { NTSTATUS Observed = DxgkHandleCoreFree(&Table, Handle, DxgkHandleTypeContext); ok_eq_hex(Observed, STATUS_OBJECT_TYPE_MISMATCH); }
    { ULONG Observed = DxgkHandleCoreLiveCount(&Table); ok_eq_ulong(Observed, 1UL); }
    { NTSTATUS Observed = DxgkHandleCoreFree(&Table, Handle, DxgkHandleTypeAllocation); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { ULONG Observed = DxgkHandleCoreLiveCount(&Table); ok_eq_ulong(Observed, 0UL); }
}

static VOID TestStaleHandles(VOID)
{
    DXGK_HANDLE_TABLE Table;
    ULONG First = 0x11;
    ULONG Second = 0x22;
    ULONG FirstHandle = 0;
    ULONG SecondHandle = 0;
    PVOID Resolved = NULL;

    DxgkHandleCoreInitialize(&Table);
    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, &First, DxgkHandleTypeAllocation, &FirstHandle); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkHandleCoreFree(&Table, FirstHandle, DxgkHandleTypeAllocation); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* The freed handle stops resolving immediately. */
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, FirstHandle, DxgkHandleTypeAllocation, &Resolved); ok_eq_hex(Observed, STATUS_INVALID_HANDLE); }
    { NTSTATUS Observed = DxgkHandleCoreFree(&Table, FirstHandle, DxgkHandleTypeAllocation); ok_eq_hex(Observed, STATUS_INVALID_HANDLE); }

    /*
     * The slot is reused, but the generation moved on.  The old handle must
     * not address the new object, or a process that leaked a handle reaches
     * whatever the driver put there next.
     */
    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, &Second, DxgkHandleTypeAllocation, &SecondHandle); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok(SecondHandle != FirstHandle, "the recycled slot yields a different handle\n");
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, FirstHandle, DxgkHandleTypeAllocation, &Resolved); ok_eq_hex(Observed, STATUS_INVALID_HANDLE); }
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, SecondHandle, DxgkHandleTypeAllocation, &Resolved); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_pointer(Resolved, &Second);
}

static VOID TestMalformedHandles(VOID)
{
    DXGK_HANDLE_TABLE Table;
    ULONG Object = 0x33;
    ULONG Handle = 0;
    PVOID Resolved = NULL;

    DxgkHandleCoreInitialize(&Table);
    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, &Object, DxgkHandleTypeSync, &Handle); ok_eq_hex(Observed, STATUS_SUCCESS); }

    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, 0, DxgkHandleTypeSync, &Resolved); ok_eq_hex(Observed, STATUS_INVALID_HANDLE); }
    /* An index past the table must be rejected before it is used to index. */
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, 0xFFFF, DxgkHandleTypeSync, &Resolved); ok_eq_hex(Observed, STATUS_INVALID_HANDLE); }
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, 0xFFFFFFFFUL, DxgkHandleTypeSync, &Resolved); ok_eq_hex(Observed, STATUS_INVALID_HANDLE); }
    /* A forged generation on a live slot must not resolve. */
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, Handle + 0x10000, DxgkHandleTypeSync, &Resolved); ok_eq_hex(Observed, STATUS_INVALID_HANDLE); }
    { NTSTATUS Observed = DxgkHandleCoreResolve(&Table, Handle, DxgkHandleTypeSync, &Resolved); ok_eq_hex(Observed, STATUS_SUCCESS); }
}

static VOID TestExhaustion(VOID)
{
    DXGK_HANDLE_TABLE Table;
    ULONG Object = 0x44;
    ULONG Handle;
    ULONG Allocated;

    DxgkHandleCoreInitialize(&Table);
    for (Allocated = 0; Allocated < DXGK_HANDLE_CORE_MAX_ENTRIES; ++Allocated)
    {
        if (!NT_SUCCESS(DxgkHandleCoreAllocate(&Table, &Object, DxgkHandleTypeResource, &Handle)))
            break;
    }
    ok_eq_ulong(Allocated, (ULONG)DXGK_HANDLE_CORE_MAX_ENTRIES);
    { ULONG Observed = DxgkHandleCoreLiveCount(&Table); ok_eq_ulong(Observed, (ULONG)DXGK_HANDLE_CORE_MAX_ENTRIES); }
    { NTSTATUS Observed = DxgkHandleCoreAllocate(&Table, &Object, DxgkHandleTypeResource, &Handle); ok_eq_hex(Observed, STATUS_INSUFFICIENT_RESOURCES); }
}

START_TEST(DxgkHandleTable)
{
    TestAllocateAndResolve();
    TestTypeSafety();
    TestStaleHandles();
    TestMalformedHandles();
    TestExhaustion();
}

/* EOF */
