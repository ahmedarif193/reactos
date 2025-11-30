/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     PnP manager Root IRQ Arbiter
 * COPYRIGHT:   Copyright 2025 Justin Miller <justin.miller@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#include <arbiter.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern ARBITER_INSTANCE IopRootIrqArbiter;
static RTL_BITMAP IopArbIrqBitmap;
static ULONG IopArbIrqBitmapBuffer[8]; /* 8 * 32 = 256 vectors */
static KSPIN_LOCK IopArbIrqLock;
static VOID IopArbIrqRebuildBitmap(_In_ PRTL_RANGE_LIST Allocation);
static VOID IopArbIrqMarkReservedLocked(VOID);
static volatile LONG IopArbIrqInitState;

#define IOP_ARB_IRQ_INIT_NOT_STARTED   0
#define IOP_ARB_IRQ_INIT_IN_PROGRESS   1
#define IOP_ARB_IRQ_INIT_DONE          2
#define IOP_ARB_IRQ_BITMAP_BITS        (RTL_NUMBER_OF(IopArbIrqBitmapBuffer) * sizeof(ULONG) * 8)

static
VOID
IopArbIrqEnsureInitialized(VOID)
{
    KIRQL OldIrql;

    while (TRUE)
    {
        LONG State = IopArbIrqInitState;
        if (State == IOP_ARB_IRQ_INIT_DONE)
            return;

        if (State == IOP_ARB_IRQ_INIT_NOT_STARTED)
        {
            if (InterlockedCompareExchange(&IopArbIrqInitState,
                                           IOP_ARB_IRQ_INIT_IN_PROGRESS,
                                           IOP_ARB_IRQ_INIT_NOT_STARTED) ==
                IOP_ARB_IRQ_INIT_NOT_STARTED)
            {
                KeInitializeSpinLock(&IopArbIrqLock);
                RtlInitializeBitMap(&IopArbIrqBitmap,
                                    IopArbIrqBitmapBuffer,
                                    IOP_ARB_IRQ_BITMAP_BITS);
                RtlClearAllBits(&IopArbIrqBitmap);

                KeAcquireSpinLock(&IopArbIrqLock, &OldIrql);
                IopArbIrqMarkReservedLocked();
                DPRINT1("IRQ Arbiter init: bitmap bits=%lu\n",
                        IopArbIrqBitmap.SizeOfBitMap);
                KeReleaseSpinLock(&IopArbIrqLock, OldIrql);

                InterlockedExchange(&IopArbIrqInitState, IOP_ARB_IRQ_INIT_DONE);
                return;
            }
        }

        KeStallExecutionProcessor(1);
    }
}

FORCEINLINE
ULONG
IopArbIrqGetSciVector(VOID)
{
    ULONG SciVector = HalGetAcpiSciVector();
    return (SciVector != 0xFFFFFFFFu) ? SciVector : 0xFFFFFFFFu;
}

static
VOID
IopArbIrqMarkReservedLocked(VOID)
{
    /* Reserve CPU-reserved vectors 0x00-0x1F */
    RtlSetBits(&IopArbIrqBitmap, 0, 0x20);

    /* Reserve spurious/APIC vectors we should not hand out */
    RtlSetBits(&IopArbIrqBitmap, 0xFF, 1);
}

static
VOID
IopArbIrqMarkSciReserved(VOID)
{
    IopArbIrqEnsureInitialized();

    ULONG SciVector = IopArbIrqGetSciVector();

    if (SciVector != 0xFFFFFFFFu && SciVector < IopArbIrqBitmap.SizeOfBitMap)
    {
        KIRQL OldIrql;
        KeAcquireSpinLock(&IopArbIrqLock, &OldIrql);
        RtlSetBits(&IopArbIrqBitmap, SciVector, 1);
        KeReleaseSpinLock(&IopArbIrqLock, OldIrql);
    }
}

static
NTSTATUS
NTAPI
IopArbIrqCommitAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    NTSTATUS Status = ArbCommitAllocation(Arbiter);
    if (NT_SUCCESS(Status))
        IopArbIrqRebuildBitmap(Arbiter->Allocation);
    return Status;
}

static
NTSTATUS
NTAPI
IopArbIrqRollbackAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    NTSTATUS Status = ArbRollbackAllocation(Arbiter);
    /* Rebuild from committed state to drop any staged allocations */
    IopArbIrqRebuildBitmap(Arbiter->Allocation);
    return Status;
}

static
NTSTATUS
NTAPI
IopArbIrqBootAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    NTSTATUS Status = ArbBootAllocation(Arbiter, ArbitrationList);
    if (NT_SUCCESS(Status))
        IopArbIrqRebuildBitmap(Arbiter->Allocation);
    return Status;
}

static
VOID
IopArbIrqRebuildBitmap(
    _In_ PRTL_RANGE_LIST Allocation)
{
    RTL_RANGE_LIST_ITERATOR Iterator;
    PRTL_RANGE Range;
    KIRQL OldIrql;
    NTSTATUS Status;

    IopArbIrqEnsureInitialized();

    KeAcquireSpinLock(&IopArbIrqLock, &OldIrql);
    RtlClearAllBits(&IopArbIrqBitmap);

    if (Allocation)
    {
        Status = RtlGetFirstRange(Allocation, &Iterator, &Range);
        while (NT_SUCCESS(Status) && Range != NULL)
        {
            ULONG Start = (ULONG)Range->Start;
            ULONG Length = (ULONG)(Range->End - Range->Start + 1);

            if (Length == 0)
            {
                Status = RtlGetNextRange(&Iterator, &Range, TRUE);
                continue;
            }

            if (Start >= IopArbIrqBitmap.SizeOfBitMap)
            {
                Status = RtlGetNextRange(&Iterator, &Range, TRUE);
                continue;
            }

            if ((Start + Length) > IopArbIrqBitmap.SizeOfBitMap)
                Length = IopArbIrqBitmap.SizeOfBitMap - Start;

            RtlSetBits(&IopArbIrqBitmap, Start, Length);

            Status = RtlGetNextRange(&Iterator, &Range, TRUE);
        }
    }

    IopArbIrqMarkReservedLocked();
    {
        ULONG SciVector = IopArbIrqGetSciVector();
        if (SciVector != 0xFFFFFFFFu && SciVector < IopArbIrqBitmap.SizeOfBitMap)
            RtlSetBits(&IopArbIrqBitmap, SciVector, 1);
    }

    KeReleaseSpinLock(&IopArbIrqLock, OldIrql);
}

static
BOOLEAN
IopArbIrqAllocateRange(
    _In_ ULONG Minimum,
    _In_ ULONG Maximum,
    _In_ ULONG Length,
    _Out_ PULONG Start)
{
    KIRQL OldIrql;
    ULONG Index;

    if (Length == 0 || Minimum > Maximum)
        return FALSE;

    IopArbIrqEnsureInitialized();

    if (Maximum >= IopArbIrqBitmap.SizeOfBitMap)
        Maximum = IopArbIrqBitmap.SizeOfBitMap - 1;

    KeAcquireSpinLock(&IopArbIrqLock, &OldIrql);
    Index = RtlFindClearBitsAndSet(&IopArbIrqBitmap,
                                   Length,
                                   Minimum);

    if (Index == 0xFFFFFFFF || (Index + Length - 1) > Maximum)
    {
        if (Index != 0xFFFFFFFF)
        {
            RtlClearBits(&IopArbIrqBitmap, Index, Length);
        }
        DPRINT1("IRQ Arbiter: unable to allocate range [%lu-%lu] length %lu\n",
                Minimum,
                Maximum,
                Length);
        KeReleaseSpinLock(&IopArbIrqLock, OldIrql);
        return FALSE;
    }

    DPRINT1("IRQ Arbiter: allocated [%lu-%lu] (len %lu)\n",
            Index,
            Index + Length - 1,
            Length);
    KeReleaseSpinLock(&IopArbIrqLock, OldIrql);

    *Start = Index;
    return TRUE;
}

NTSTATUS
NTAPI
IopAllocateIrqVectors(
    _In_ ULONG MinimumVector,
    _In_ ULONG MaximumVector,
    _In_ ULONG Count,
    _Out_ PULONG StartVector)
{
    ULONG Start;

    if (!StartVector || Count == 0)
        return STATUS_INVALID_PARAMETER;

    if (!IopArbIrqAllocateRange(MinimumVector,
                                MaximumVector,
                                Count,
                                &Start))
    {
        return STATUS_CONFLICTING_ADDRESSES;
    }

    *StartVector = Start;
    return STATUS_SUCCESS;
}

VOID
NTAPI
IopReserveIrqVectors(
    _In_reads_(Count) PULONG Vectors,
    _In_ ULONG Count)
{
    KIRQL OldIrql;
    ULONG i;

    if (!Vectors || Count == 0)
        return;

    IopArbIrqEnsureInitialized();

    KeAcquireSpinLock(&IopArbIrqLock, &OldIrql);
    for (i = 0; i < Count; i++)
    {
        ULONG vec = Vectors[i];
        if (vec >= IopArbIrqBitmap.SizeOfBitMap)
        {
            DPRINT1("IopReserveIrqVectors: vector %lu outside allocator range %lu\n",
                    vec, IopArbIrqBitmap.SizeOfBitMap);
            continue;
        }

        if (vec < 0x20 || vec == 0xFF)
            continue;

        RtlSetBits(&IopArbIrqBitmap, vec, 1);
        DPRINT1("IopReserveIrqVectors: reserved vector %lu\n", vec);
    }
    KeReleaseSpinLock(&IopArbIrqLock, OldIrql);
}

VOID
NTAPI
IopReleaseIrqVectors(
    _In_reads_(Count) PULONG Vectors,
    _In_ ULONG Count)
{
    KIRQL OldIrql;
    ULONG i;
    ULONG SciVector;

    if (!Vectors || Count == 0)
        return;

    IopArbIrqEnsureInitialized();

    SciVector = IopArbIrqGetSciVector();
    KeAcquireSpinLock(&IopArbIrqLock, &OldIrql);
    for (i = 0; i < Count; i++)
    {
        ULONG vec = Vectors[i];
        if (vec >= IopArbIrqBitmap.SizeOfBitMap)
        {
            DPRINT1("IopReleaseIrqVectors: vector %lu outside allocator range %lu\n",
                    vec, IopArbIrqBitmap.SizeOfBitMap);
            continue;
        }

        /* Never release reserved vectors (CPU-reserved, spurious, SCI) */
        if (vec < 0x20 || vec == 0xFF)
            continue;
        if (SciVector != 0xFFFFFFFFu && vec == SciVector)
            continue;

        RtlClearBits(&IopArbIrqBitmap, vec, 1);
        DPRINT1("IopReleaseIrqVectors: released vector %lu\n", vec);
    }
    KeReleaseSpinLock(&IopArbIrqLock, OldIrql);
}

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
IopArbIrqUnpackRequirements(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 OutMinimumAddress,
    _Out_ PUINT64 OutMaximumAddress,
    _Out_ PULONG OutLength,
    _Out_ PULONG OutAlignment)
{
    PAGED_CODE();
    IopArbIrqEnsureInitialized();
    DPRINT("IopArbIrqUnpackRequirements: IoDescriptor: %p, OutMinimumAddress: %p, OutMaximumAddress: %p, OutLength: %p, OutAlignment: %p\n",
           IoDescriptor,
           OutMinimumAddress,
           OutMaximumAddress,
           OutLength,
           OutAlignment);

    if (IoDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
    {
        ULONG MessageCount = (ULONG)(IoDescriptor->u.Interrupt.MaximumVector -
                                     IoDescriptor->u.Interrupt.MinimumVector + 1);
        if (MessageCount == 0)
            MessageCount = 1;

        *OutMinimumAddress = 0;
        *OutMaximumAddress = IopArbIrqBitmap.SizeOfBitMap ?
                             IopArbIrqBitmap.SizeOfBitMap - 1 : 0;
        *OutLength = MessageCount;
    }
    else
    {
        *OutMinimumAddress = IoDescriptor->u.Interrupt.MinimumVector;
        *OutMaximumAddress = IoDescriptor->u.Interrupt.MaximumVector;
        *OutLength = (ULONG)(IoDescriptor->u.Interrupt.MaximumVector -
                             IoDescriptor->u.Interrupt.MinimumVector + 1);
    }
    *OutAlignment = 1;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopArbIrqPackResource(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    PAGED_CODE();
    IopArbIrqEnsureInitialized();
    DPRINT("IopArbIrqPackResource: IoDescriptor: %p, Start: %p, CmDescriptor: %p\n",
           IoDescriptor,
           Start,
           CmDescriptor);

    CmDescriptor->Type = CmResourceTypeInterrupt;
    CmDescriptor->ShareDisposition = IoDescriptor->ShareDisposition;
    CmDescriptor->Flags = IoDescriptor->Flags;

    if (IoDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
    {
        USHORT MessageCount = (USHORT)(IoDescriptor->u.Interrupt.MaximumVector -
                                       IoDescriptor->u.Interrupt.MinimumVector + 1);
        if (MessageCount == 0)
            MessageCount = 1;

        ULONG AllocStart = (ULONG)Start;
        if (AllocStart == 0 &&
            !IopArbIrqAllocateRange(0,
                                    IopArbIrqBitmap.SizeOfBitMap ?
                                        IopArbIrqBitmap.SizeOfBitMap - 1 : 0,
                                    MessageCount,
                                    &AllocStart))
        {
            return STATUS_CONFLICTING_ADDRESSES;
        }

        KAFFINITY RequestedAffinity = IoDescriptor->u.Interrupt.TargetedProcessors ?
                                      IoDescriptor->u.Interrupt.TargetedProcessors :
                                      KeActiveProcessors;

        CmDescriptor->u.MessageInterrupt.Raw.Reserved = 0;
        CmDescriptor->u.MessageInterrupt.Raw.MessageCount = MessageCount;
        CmDescriptor->u.MessageInterrupt.Raw.Vector = AllocStart;
        CmDescriptor->u.MessageInterrupt.Raw.Affinity = RequestedAffinity;
    }
    else
    {
        ULONG AllocStart = (ULONG)Start;
        if (AllocStart == 0 &&
            !IopArbIrqAllocateRange(IoDescriptor->u.Interrupt.MinimumVector,
                                    IoDescriptor->u.Interrupt.MaximumVector,
                                    1,
                                    &AllocStart))
        {
            return STATUS_CONFLICTING_ADDRESSES;
        }

        KAFFINITY RequestedAffinity = IoDescriptor->u.Interrupt.TargetedProcessors ?
                                      IoDescriptor->u.Interrupt.TargetedProcessors :
                                      KeActiveProcessors;

        CmDescriptor->u.Interrupt.Level = AllocStart;
        CmDescriptor->u.Interrupt.Vector = AllocStart;
        CmDescriptor->u.Interrupt.Affinity = RequestedAffinity;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopArbIrqUnpackResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PULONG OutLength)
{
    PAGED_CODE();
    DPRINT("IopArbIrqUnpackResource: CmDescriptor: %p, Start: %p, OutLength: %p\n",
           CmDescriptor,
           Start,
           OutLength);

    if (CmDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
    {
        *Start = CmDescriptor->u.MessageInterrupt.Raw.Vector;
        *OutLength = (CmDescriptor->u.MessageInterrupt.Raw.MessageCount ?
                      CmDescriptor->u.MessageInterrupt.Raw.MessageCount : 1);
    }
    else
    {
        *Start = CmDescriptor->u.Interrupt.Vector;
        *OutLength = 1;
    }

    return STATUS_SUCCESS;
}

INT32
NTAPI
IopArbIrqScoreRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    PAGED_CODE();
    DPRINT("IopArbIrqScoreRequirement: IoDescriptor: %p\n",
           IoDescriptor);

    /* Prefer message interrupts over legacy line-based */
    if (IoDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
        return 20;

    return 0;
}

NTSTATUS
NTAPI
IopArbIrqInitialize(VOID)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    PAGED_CODE();
    IopArbIrqEnsureInitialized();
    IopArbIrqMarkSciReserved();

    IopRootIrqArbiter.Name = L"RootIRQ";
    IopRootIrqArbiter.UnpackRequirement = IopArbIrqUnpackRequirements;
    IopRootIrqArbiter.PackResource = IopArbIrqPackResource;
    IopRootIrqArbiter.UnpackResource = IopArbIrqUnpackResource;
    IopRootIrqArbiter.ScoreRequirement = IopArbIrqScoreRequirement;
    IopRootIrqArbiter.CommitAllocation = IopArbIrqCommitAllocation;
    IopRootIrqArbiter.RollbackAllocation = IopArbIrqRollbackAllocation;
    IopRootIrqArbiter.BootAllocation = IopArbIrqBootAllocation;

    Status = ArbInitializeArbiterInstance(&IopRootIrqArbiter,
                                          NULL,
                                          CmResourceTypeInterrupt,
                                          IopRootIrqArbiter.Name,
                                          L"Root",
                                          NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopArbDmaInitialize: Failed with %X", Status);
    }

    return Status;
}
