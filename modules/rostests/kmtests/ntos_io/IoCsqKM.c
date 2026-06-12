/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite cancel-safe queue API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static IO_CSQ Csq;
static LIST_ENTRY CsqQueue;
static KSPIN_LOCK CsqLock;
static volatile LONG CompleteCanceledCalls;

static
VOID
NTAPI
CsqInsertIrp(
    _In_ PIO_CSQ IoCsq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(IoCsq);
    InsertTailList(&CsqQueue, &Irp->Tail.Overlay.ListEntry);
}

static
VOID
NTAPI
CsqRemoveIrp(
    _In_ PIO_CSQ IoCsq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(IoCsq);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

static
PIRP
NTAPI
CsqPeekNextIrp(
    _In_ PIO_CSQ IoCsq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(IoCsq);
    UNREFERENCED_PARAMETER(PeekContext);

    Entry = Irp != NULL ? Irp->Tail.Overlay.ListEntry.Flink : CsqQueue.Flink;
    if (Entry == &CsqQueue)
        return NULL;
    return CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
}

_IRQL_raises_(DISPATCH_LEVEL)
static
VOID
NTAPI
CsqAcquireLock(
    _In_ PIO_CSQ IoCsq,
    _Out_ PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(IoCsq);
    KeAcquireSpinLock(&CsqLock, OldIrql);
}

static
VOID
NTAPI
CsqReleaseLock(
    _In_ PIO_CSQ IoCsq,
    _In_ KIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(IoCsq);
    KeReleaseSpinLock(&CsqLock, OldIrql);
}

static
VOID
NTAPI
CsqCompleteCanceledIrp(
    _In_ PIO_CSQ IoCsq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(IoCsq);
    UNREFERENCED_PARAMETER(Irp);
    InterlockedIncrement(&CompleteCanceledCalls);
}

START_TEST(IoCsqKM)
{
    PIRP Irps[3];
    PIRP Removed;
    NTSTATUS Status;
    ULONG i;

    InitializeListHead(&CsqQueue);
    KeInitializeSpinLock(&CsqLock);
    CompleteCanceledCalls = 0;

    Status = IoCsqInitialize(&Csq, CsqInsertIrp, CsqRemoveIrp, CsqPeekNextIrp, CsqAcquireLock, CsqReleaseLock, CsqCompleteCanceledIrp);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    for (i = 0; i < 3; i++)
    {
        Irps[i] = IoAllocateIrp(1, FALSE);
        ok(Irps[i] != NULL, "irp %lu alloc failed\n", i);
        if (Irps[i] == NULL) return;
        IoCsqInsertIrp(&Csq, Irps[i], NULL);
    }

    Removed = IoCsqRemoveNextIrp(&Csq, NULL);
    ok_eq_pointer(Removed, Irps[0]);

    Removed = IoCsqRemoveNextIrp(&Csq, NULL);
    ok_eq_pointer(Removed, Irps[1]);

    ok_bool_true(IoCancelIrp(Irps[2]), "cancel queued irp");
    ok_eq_long(CompleteCanceledCalls, 1L);

    Removed = IoCsqRemoveNextIrp(&Csq, NULL);
    ok_eq_pointer(Removed, NULL);

    for (i = 0; i < 3; i++)
        IoFreeIrp(Irps[i]);
}
