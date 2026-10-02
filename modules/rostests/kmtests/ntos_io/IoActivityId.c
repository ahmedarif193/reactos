/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite IRP and thread activity identifiers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static const GUID First = { 0x11111111, 0x2222, 0x3333, { 0x44, 0x44, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 } };
static const GUID Second = { 0xAAAAAAAA, 0xBBBB, 0xCCCC, { 0xDD, 0xDD, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE } };

static const GUID Third = { 0x12345678, 0x9ABC, 0xDEF0, { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 } };

static
VOID
CheckExtension(
    _In_ PIRP Irp,
    _In_ USHORT ExpectedFlags,
    _In_ USHORT ExpectedTypes,
    _In_opt_ const GUID *Expected,
    _In_ PCSTR Label)
{
    PUSHORT Extension = Irp->Tail.Overlay.IrpExtension;
    const GUID *Guid;

    ok(Extension != NULL, "%s: no extension\n", Label);
    if (Extension == NULL)
        return;

    Guid = (const GUID *)((PUCHAR)Extension + 24);
    ok(Extension[0] == ExpectedFlags, "%s: flags %x, expected %x\n", Label, Extension[0], ExpectedFlags);
    ok(Extension[1] == ExpectedTypes, "%s: types %x, expected %x\n", Label, Extension[1], ExpectedTypes);
    if (Expected != NULL)
        ok(RtlEqualMemory(Guid, Expected, sizeof(GUID)), "%s: stored %08lx-%04x-%04x\n", Label, Guid->Data1,
           Guid->Data2, Guid->Data3);
}

static
VOID
CheckIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS ExpectedStatus,
    _In_opt_ const GUID *Expected,
    _In_ PCSTR Label)
{
    GUID Guid;
    GUID Untouched;
    NTSTATUS Status;

    RtlFillMemory(&Guid, sizeof(Guid), 0x55);
    RtlFillMemory(&Untouched, sizeof(Untouched), 0x55);
    Status = IoGetActivityIdIrp(Irp, &Guid);
    ok(Status == ExpectedStatus, "%s: status %lx, expected %lx\n", Label, Status, ExpectedStatus);
    if (Expected == NULL)
        Expected = &Untouched;
    ok(RtlEqualMemory(&Guid, Expected, sizeof(Guid)), "%s: guid %08lx-%04x-%04x, expected %08lx-%04x-%04x\n",
       Label, Guid.Data1, Guid.Data2, Guid.Data3, Expected->Data1, Expected->Data2, Expected->Data3);
}

START_TEST(IoActivityId)
{
    GUID Propagated, Saved;
    LPCGUID Original, Previous;
    PTEB Teb;
    NTSTATUS Status;
    PIRP Irp;
    USHORT Size;
    Irp = IoAllocateIrp(2, FALSE);
    ok(Irp != NULL, "No IRP\n");
    if (Irp == NULL)
        return;
    ok_eq_pointer(Irp->Tail.Overlay.IrpExtension, NULL);
    CheckIrp(Irp, STATUS_NOT_FOUND, NULL, "fresh");
    Status = IoSetActivityIdIrp(Irp, &First);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckExtension(Irp, 1, 1, &First, "first");
    CheckIrp(Irp, STATUS_SUCCESS, &First, "first");
    Status = IoSetActivityIdIrp(Irp, &Second);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckIrp(Irp, STATUS_SUCCESS, &Second, "second");
    Previous = IoGetActivityIdThread();
    ok_eq_pointer(Previous, NULL);
    Original = IoSetActivityIdThread(&First);
    ok_eq_pointer(Original, Previous);
    ok_eq_pointer(IoGetActivityIdThread(), &First);
    Status = IoSetActivityIdIrp(Irp, NULL);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_pointer(Irp->Tail.Overlay.IrpExtension, NULL);
    CheckIrp(Irp, STATUS_NOT_FOUND, NULL, "cleared");
    ok_eq_pointer(IoSetActivityIdThread(&Second), &First);
    ok_eq_pointer(IoGetActivityIdThread(), &Second);
    IoClearActivityIdThread(&First);
    ok_eq_pointer(IoGetActivityIdThread(), &First);
    IoClearActivityIdThread(Original);
    ok_eq_pointer(IoGetActivityIdThread(), Previous);
    Teb = PsGetCurrentThreadTeb();
    ok(Teb != NULL, "No TEB\n");
    if (Teb != NULL)
    {
        _SEH2_TRY
        {
            Saved = Teb->ActivityId;
            Teb->ActivityId = Third;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Teb = NULL;
        }
        _SEH2_END;
        ok(Teb != NULL, "TEB not writable\n");
    }

    Status = IoSetActivityIdIrp(Irp, NULL);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    CheckIrp(Irp, STATUS_NOT_FOUND, NULL, "user activity");

    if (Teb != NULL)
    {
        _SEH2_TRY
        {
            Teb->ActivityId = Saved;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }
    Status = IoSetActivityIdIrp(Irp, &Second);
    ok_eq_hex(Status, STATUS_SUCCESS);
    RtlFillMemory(&Propagated, sizeof(Propagated), 0x77);
    Original = (LPCGUID)(ULONG_PTR)1;
    Status = IoPropagateActivityIdToThread(Irp, &Propagated, &Original);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(RtlEqualMemory(&Propagated, &Second, sizeof(GUID)), "Propagated %08lx\n", Propagated.Data1);
    ok_eq_pointer(Original, Previous);
    ok_eq_pointer(IoGetActivityIdThread(), &Propagated);
    IoClearActivityIdThread(Original);
    ok_eq_pointer(IoGetActivityIdThread(), Previous);
    IoReuseIrp(Irp, STATUS_SUCCESS);
    ok(Irp->Tail.Overlay.IrpExtension != NULL, "Extension released by reuse\n");
    CheckIrp(Irp, STATUS_SUCCESS, &Second, "reused");
    IoFreeIrp(Irp);
    Irp = IoAllocateIrp(1, FALSE);
    ok(Irp != NULL, "No IRP\n");
    if (Irp != NULL)
    {
        RtlFillMemory(&Propagated, sizeof(Propagated), 0x77);
        Original = (LPCGUID)(ULONG_PTR)1;
        Status = IoPropagateActivityIdToThread(Irp, &Propagated, &Original);
        ok_eq_hex(Status, STATUS_NOT_FOUND);
        ok_eq_hex(Propagated.Data1, 0x77777777UL);
        ok_eq_pointer(Original, (LPCGUID)(ULONG_PTR)1);
        ok_eq_pointer(IoGetActivityIdThread(), Previous);
        IoFreeIrp(Irp);
    }
    Size = IoSizeOfIrp(1);
    Irp = ExAllocatePoolWithTag(NonPagedPool, Size, 'dIoI');
    ok(Irp != NULL, "No pool\n");
    if (Irp != NULL)
    {
        IoInitializeIrp(Irp, Size, 1);
        ok_eq_pointer(Irp->Tail.Overlay.IrpExtension, NULL);
        CheckIrp(Irp, STATUS_NOT_FOUND, NULL, "initialized");
        Status = IoSetActivityIdIrp(Irp, &First);
        ok_eq_hex(Status, STATUS_SUCCESS);
        CheckExtension(Irp, 1, 1, &First, "initialized first");
        CheckIrp(Irp, STATUS_SUCCESS, &First, "initialized first");
        IoCleanupIrp(Irp);
        ok_eq_pointer(Irp->Tail.Overlay.IrpExtension, NULL);
        CheckIrp(Irp, STATUS_NOT_FOUND, NULL, "cleaned");
        ExFreePoolWithTag(Irp, 'dIoI');
    }
}
