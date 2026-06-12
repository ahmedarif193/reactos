/*
* PROJECT:         ReactOS Kernel
* LICENSE:         GPL - See COPYING in the top level directory
* FILE:            ntoskrnl/ex/lookas.c
* PURPOSE:         Lookaside Lists
* PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
*/

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

LIST_ENTRY ExpNonPagedLookasideListHead;
KSPIN_LOCK ExpNonPagedLookasideListLock;
LIST_ENTRY ExpPagedLookasideListHead;
KSPIN_LOCK ExpPagedLookasideListLock;
LIST_ENTRY ExSystemLookasideListHead;
LIST_ENTRY ExPoolLookasideListHead;
GENERAL_LOOKASIDE ExpSmallNPagedPoolLookasideLists[NUMBER_POOL_LOOKASIDE_LISTS];
GENERAL_LOOKASIDE ExpSmallPagedPoolLookasideLists[NUMBER_POOL_LOOKASIDE_LISTS];

/* PRIVATE FUNCTIONS *********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
ExInitializeSystemLookasideList(IN PGENERAL_LOOKASIDE List,
                                IN POOL_TYPE Type,
                                IN ULONG Size,
                                IN ULONG Tag,
                                IN USHORT MaximumDepth,
                                IN PLIST_ENTRY ListHead)
{
    /* Initialize the list */
    List->Tag = Tag;
    List->Type = Type;
    List->Size = Size;
    InsertHeadList(ListHead, &List->ListEntry);
    List->MaximumDepth = MaximumDepth;
    List->Depth = 2;
    List->Allocate = ExAllocatePoolWithTag;
    List->Free = ExFreePool;
    InitializeSListHead(&List->ListHead);
    List->TotalAllocates = 0;
    List->AllocateHits = 0;
    List->TotalFrees = 0;
    List->FreeHits = 0;
    List->LastTotalAllocates = 0;
    List->LastAllocateHits = 0;
}

CODE_SEG("INIT")
VOID
NTAPI
ExInitPoolLookasidePointers(VOID)
{
    ULONG i;
    PKPRCB Prcb = KeGetCurrentPrcb();
    PGENERAL_LOOKASIDE Entry;

    /* Loop for all pool lists */
    for (i = 0; i < NUMBER_POOL_LOOKASIDE_LISTS; i++)
    {
        /* Initialize the non-paged list */
        Entry = &ExpSmallNPagedPoolLookasideLists[i];
        InitializeSListHead(&Entry->ListHead);

#if (NTDDI_VERSION >= NTDDI_LONGHORN)
        /* At Vista+, GENERAL_LOOKASIDE_POOL is inline, not a P/L pointer pair.
         * TODO: Initialize the inline lookaside for Win7 boot. */
        UNREFERENCED_PARAMETER(Entry);
        RtlZeroMemory(&Prcb->PPNPagedLookasideList[i], sizeof(Prcb->PPNPagedLookasideList[i]));
#else
        /* Bind to PRCB */
        Prcb->PPNPagedLookasideList[i].P = Entry;
        Prcb->PPNPagedLookasideList[i].L = Entry;
#endif

        /* Initialize the paged list */
        Entry = &ExpSmallPagedPoolLookasideLists[i];
        InitializeSListHead(&Entry->ListHead);

#if (NTDDI_VERSION >= NTDDI_LONGHORN)
        RtlZeroMemory(&Prcb->PPPagedLookasideList[i], sizeof(Prcb->PPPagedLookasideList[i]));
#else
        /* Bind to PRCB */
        Prcb->PPPagedLookasideList[i].P = Entry;
        Prcb->PPPagedLookasideList[i].L = Entry;
#endif
    }
}

CODE_SEG("INIT")
VOID
NTAPI
ExpInitLookasideLists(VOID)
{
    ULONG i;

    /* Initialize locks and lists */
    InitializeListHead(&ExpNonPagedLookasideListHead);
    InitializeListHead(&ExpPagedLookasideListHead);
    InitializeListHead(&ExSystemLookasideListHead);
    InitializeListHead(&ExPoolLookasideListHead);
    KeInitializeSpinLock(&ExpNonPagedLookasideListLock);
    KeInitializeSpinLock(&ExpPagedLookasideListLock);

    /* Initialize the system lookaside lists */
    for (i = 0; i < NUMBER_POOL_LOOKASIDE_LISTS; i++)
    {
        /* Initialize the non-paged list */
        ExInitializeSystemLookasideList(&ExpSmallNPagedPoolLookasideLists[i],
                                        NonPagedPool,
                                        (i + 1) * 8,
                                        'looP',
                                        256,
                                        &ExPoolLookasideListHead);

        /* Initialize the paged list */
        ExInitializeSystemLookasideList(&ExpSmallPagedPoolLookasideLists[i],
                                        PagedPool,
                                        (i + 1) * 8,
                                        'looP',
                                        256,
                                        &ExPoolLookasideListHead);
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
PVOID
NTAPI
ExiAllocateFromPagedLookasideList(IN PPAGED_LOOKASIDE_LIST Lookaside)
{
    PVOID Entry;

    Lookaside->L.TotalAllocates++;
    Entry = InterlockedPopEntrySList(&Lookaside->L.ListHead);
    if (!Entry)
    {
        Lookaside->L.AllocateMisses++;
        Entry = (Lookaside->L.Allocate)(Lookaside->L.Type,
                                        Lookaside->L.Size,
                                        Lookaside->L.Tag);
    }
    return Entry;
}

/*
 * @implemented
 */
VOID
NTAPI
ExiFreeToPagedLookasideList(IN PPAGED_LOOKASIDE_LIST  Lookaside,
                            IN PVOID  Entry)
{
    Lookaside->L.TotalFrees++;
    if (ExQueryDepthSList(&Lookaside->L.ListHead) >= Lookaside->L.Depth)
    {
        Lookaside->L.FreeMisses++;
        (Lookaside->L.Free)(Entry);
    }
    else
    {
        InterlockedPushEntrySList(&Lookaside->L.ListHead, (PSLIST_ENTRY)Entry);
    }
}

/*
 * @implemented
 */
VOID
NTAPI
ExDeleteNPagedLookasideList(IN PNPAGED_LOOKASIDE_LIST Lookaside)
{
    KIRQL OldIrql;
    PVOID Entry;

    /* Pop all entries off the stack and release their resources */
    for (;;)
    {
        Entry = InterlockedPopEntrySList(&Lookaside->L.ListHead);
        if (!Entry) break;
        (*Lookaside->L.Free)(Entry);
    }

    /* Remove from list */
    KeAcquireSpinLock(&ExpNonPagedLookasideListLock, &OldIrql);
    RemoveEntryList(&Lookaside->L.ListEntry);
    KeReleaseSpinLock(&ExpNonPagedLookasideListLock, OldIrql);
}

/*
 * @implemented
 */
VOID
NTAPI
ExDeletePagedLookasideList(IN PPAGED_LOOKASIDE_LIST Lookaside)
{
    KIRQL OldIrql;
    PVOID Entry;

    /* Pop all entries off the stack and release their resources */
    for (;;)
    {
        Entry = InterlockedPopEntrySList(&Lookaside->L.ListHead);
        if (!Entry) break;
        (*Lookaside->L.Free)(Entry);
    }

    /* Remove from list */
    KeAcquireSpinLock(&ExpPagedLookasideListLock, &OldIrql);
    RemoveEntryList(&Lookaside->L.ListEntry);
    KeReleaseSpinLock(&ExpPagedLookasideListLock, OldIrql);
}

/*
 * @implemented
 */
VOID
NTAPI
ExInitializeNPagedLookasideList(IN PNPAGED_LOOKASIDE_LIST Lookaside,
                                IN PALLOCATE_FUNCTION Allocate OPTIONAL,
                                IN PFREE_FUNCTION Free OPTIONAL,
                                IN ULONG Flags,
                                IN SIZE_T Size,
                                IN ULONG Tag,
                                IN USHORT Depth)
{
    /* Initialize the Header */
    ExInitializeSListHead(&Lookaside->L.ListHead);
    Lookaside->L.TotalAllocates = 0;
    Lookaside->L.AllocateMisses = 0;
    Lookaside->L.TotalFrees = 0;
    Lookaside->L.FreeMisses = 0;
    Lookaside->L.Type = NonPagedPool | Flags;
    Lookaside->L.Tag = Tag;
    Lookaside->L.Size = (ULONG)Size;
    Lookaside->L.Depth = 4;
    Lookaside->L.MaximumDepth = 256;
    Lookaside->L.LastTotalAllocates = 0;
    Lookaside->L.LastAllocateMisses = 0;

    /* Set the Allocate/Free Routines */
    if (Allocate)
    {
        Lookaside->L.Allocate = Allocate;
    }
    else
    {
        Lookaside->L.Allocate = ExAllocatePoolWithTag;
    }

    if (Free)
    {
        Lookaside->L.Free = Free;
    }
    else
    {
        Lookaside->L.Free = ExFreePool;
    }

    /* Insert it into the list */
    ExInterlockedInsertTailList(&ExpNonPagedLookasideListHead,
                                &Lookaside->L.ListEntry,
                                &ExpNonPagedLookasideListLock);
}

/*
 * @implemented
 */
VOID
NTAPI
ExInitializePagedLookasideList(IN PPAGED_LOOKASIDE_LIST Lookaside,
                               IN PALLOCATE_FUNCTION Allocate OPTIONAL,
                               IN PFREE_FUNCTION Free OPTIONAL,
                               IN ULONG Flags,
                               IN SIZE_T Size,
                               IN ULONG Tag,
                               IN USHORT Depth)
{
    /* Initialize the Header */
    ExInitializeSListHead(&Lookaside->L.ListHead);
    Lookaside->L.TotalAllocates = 0;
    Lookaside->L.AllocateMisses = 0;
    Lookaside->L.TotalFrees = 0;
    Lookaside->L.FreeMisses = 0;
    Lookaside->L.Type = PagedPool | Flags;
    Lookaside->L.Tag = Tag;
    Lookaside->L.Size = (ULONG)Size;
    Lookaside->L.Depth = 4;
    Lookaside->L.MaximumDepth = 256;
    Lookaside->L.LastTotalAllocates = 0;
    Lookaside->L.LastAllocateMisses = 0;

    /* Set the Allocate/Free Routines */
    if (Allocate)
    {
        Lookaside->L.Allocate = Allocate;
    }
    else
    {
        Lookaside->L.Allocate = ExAllocatePoolWithTag;
    }

    if (Free)
    {
        Lookaside->L.Free = Free;
    }
    else
    {
        Lookaside->L.Free = ExFreePool;
    }

    /* Insert it into the list */
    ExInterlockedInsertTailList(&ExpPagedLookasideListHead,
                                &Lookaside->L.ListEntry,
                                &ExpPagedLookasideListLock);
}

static
PVOID
NTAPI
ExpLookasideListExAllocate(IN POOL_TYPE PoolType,
                           IN SIZE_T NumberOfBytes,
                           IN ULONG Tag,
                           IN OUT PLOOKASIDE_LIST_EX Lookaside)
{
    UNREFERENCED_PARAMETER(Lookaside);
    return ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);
}

static
VOID
NTAPI
ExpLookasideListExFree(IN PVOID Buffer,
                       IN OUT PLOOKASIDE_LIST_EX Lookaside)
{
    ExFreePoolWithTag(Buffer, Lookaside->L.Tag);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
ExInitializeLookasideListEx(OUT PLOOKASIDE_LIST_EX Lookaside,
                            IN PALLOCATE_FUNCTION_EX Allocate OPTIONAL,
                            IN PFREE_FUNCTION_EX Free OPTIONAL,
                            IN POOL_TYPE PoolType,
                            IN ULONG Flags,
                            IN SIZE_T Size,
                            IN ULONG Tag,
                            IN USHORT Depth)
{
    UNREFERENCED_PARAMETER(Depth);

    if (Size < LOOKASIDE_MINIMUM_BLOCK_SIZE)
        return STATUS_INVALID_PARAMETER_6;
    if ((Flags & ~(EX_LOOKASIDE_LIST_EX_FLAGS_RAISE_ON_FAIL | EX_LOOKASIDE_LIST_EX_FLAGS_FAIL_NO_RAISE)) != 0)
        return STATUS_INVALID_PARAMETER_5;

    if (Flags & EX_LOOKASIDE_LIST_EX_FLAGS_RAISE_ON_FAIL)
        PoolType |= POOL_RAISE_IF_ALLOCATION_FAILURE;

    ExInitializeSListHead(&Lookaside->L.ListHead);
    Lookaside->L.TotalAllocates = 0;
    Lookaside->L.AllocateMisses = 0;
    Lookaside->L.TotalFrees = 0;
    Lookaside->L.FreeMisses = 0;
    Lookaside->L.Type = PoolType;
    Lookaside->L.Tag = Tag;
    Lookaside->L.Size = (ULONG)Size;
    Lookaside->L.Depth = 4;
    Lookaside->L.MaximumDepth = EX_MAXIMUM_LOOKASIDE_DEPTH_BASE;
    Lookaside->L.LastTotalAllocates = 0;
    Lookaside->L.LastAllocateMisses = 0;
    Lookaside->L.AllocateEx = Allocate ? Allocate : ExpLookasideListExAllocate;
    Lookaside->L.FreeEx = Free ? Free : ExpLookasideListExFree;

    if ((PoolType & 1) == PagedPool)
    {
        ExInterlockedInsertTailList(&ExpPagedLookasideListHead,
                                    &Lookaside->L.ListEntry,
                                    &ExpPagedLookasideListLock);
    }
    else
    {
        ExInterlockedInsertTailList(&ExpNonPagedLookasideListHead,
                                    &Lookaside->L.ListEntry,
                                    &ExpNonPagedLookasideListLock);
    }

    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
NTAPI
ExFlushLookasideListEx(IN OUT PLOOKASIDE_LIST_EX Lookaside)
{
    PVOID Entry;

    while ((Entry = InterlockedPopEntrySList(&Lookaside->L.ListHead)) != NULL)
    {
        Lookaside->L.FreeEx(Entry, Lookaside);
    }
}

/*
 * @implemented
 */
VOID
NTAPI
ExDeleteLookasideListEx(IN OUT PLOOKASIDE_LIST_EX Lookaside)
{
    KIRQL OldIrql;
    PKSPIN_LOCK Lock;

    ExFlushLookasideListEx(Lookaside);

    if ((Lookaside->L.Type & 1) == PagedPool)
        Lock = &ExpPagedLookasideListLock;
    else
        Lock = &ExpNonPagedLookasideListLock;

    KeAcquireSpinLock(Lock, &OldIrql);
    RemoveEntryList(&Lookaside->L.ListEntry);
    KeReleaseSpinLock(Lock, OldIrql);
}

/* EOF */
