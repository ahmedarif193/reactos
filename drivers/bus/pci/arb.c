/*
 * PROJECT:         ReactOS PCI Bus Driver
 * FILE:            drivers/bus/pci/arb.c
 * PURPOSE:         PCI Resource Arbiter Infrastructure
 */

#include "pci.h"

#define NDEBUG
#include <debug.h>

#define TAG_PCI_ARB 'brAP'
#define PCI_ARBITER_SIGNATURE 'AbrP'

/*
 * PCI_ARBITER - Manages resource ranges for a specific resource type
 * (I/O ports, memory, or bus numbers) on a PCI root bus.
 *
 * Uses a vtable-based arbiter with dual range lists:
 * - Allocated: committed resource assignments
 * - Possible: working copy during rebalance operations
 */
typedef struct _PCI_ARBITER
{
    /* Signature for validation ('AbrP') */
    ULONG Signature;

    /* Link in the FDO's arbiter list */
    LIST_ENTRY ListEntry;

    /* Resource type this arbiter manages */
    CM_RESOURCE_TYPE ResourceType;

    /* Owning FDO extension */
    PFDO_DEVICE_EXTENSION FdoExtension;

    /* Committed resource assignments */
    RTL_RANGE_LIST AllocatedRanges;

    /* Working copy used during rebalance */
    RTL_RANGE_LIST PossibleRanges;

    /* Synchronization event (mutex-like, auto-reset) */
    KEVENT MutexEvent;

    /* Whether this arbiter has been initialized */
    BOOLEAN Initialized;

    /* Whether a transaction is in progress */
    BOOLEAN TransactionInProgress;
} PCI_ARBITER, *PPCI_ARBITER;

PPCI_ARBITER
PciFindArbiter(_In_ PFDO_DEVICE_EXTENSION FdoExtension, _In_ CM_RESOURCE_TYPE ResourceType);

/* ---- Internal helpers ---- */

/**
 * @brief Acquire the arbiter mutex for exclusive access.
 */
static
VOID
PciArbiterAcquireMutex(
    _In_ PPCI_ARBITER Arbiter)
{
    KeWaitForSingleObject(&Arbiter->MutexEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
}

/**
 * @brief Release the arbiter mutex.
 */
static
VOID
PciArbiterReleaseMutex(
    _In_ PPCI_ARBITER Arbiter)
{
    KeSetEvent(&Arbiter->MutexEvent, IO_NO_INCREMENT, FALSE);
}

/* ---- Arbiter Operations ---- */

/**
 * @brief Report a chosen range back through the NT arbiter contract.
 */
static
VOID
PciArbiterWriteAssignment(_Inout_ PARBITER_LIST_ENTRY ArbEntry, _In_ PIO_RESOURCE_DESCRIPTOR Descriptor, _In_ ULONGLONG Start, _In_ ULONGLONG Length)
{
    if (ArbEntry->Assignment)
    {
        ArbEntry->Assignment->Type = Descriptor->Type;
        ArbEntry->Assignment->ShareDisposition = Descriptor->ShareDisposition;
        ArbEntry->Assignment->Flags = Descriptor->Flags;
        if (Descriptor->Type == CmResourceTypeBusNumber)
        {
            ArbEntry->Assignment->u.BusNumber.Start = (ULONG)Start;
            ArbEntry->Assignment->u.BusNumber.Length = (ULONG)Length;
            ArbEntry->Assignment->u.BusNumber.Reserved = 0;
        }
        else
        {
            ArbEntry->Assignment->u.Generic.Start.QuadPart = (LONGLONG)Start;
            ArbEntry->Assignment->u.Generic.Length = (ULONG)Length;
        }
    }
    ArbEntry->SelectedAlternative = Descriptor;
    ArbEntry->Result = ArbiterResultSuccess;
}

/**
 * @brief Test whether a proposed allocation can be satisfied.
 *
 * Copies the committed AllocatedRanges into PossibleRanges, then removes
 * the requesting device's existing ranges so they can be reassigned.
 * For each entry, tries the alternatives in order until one fits; the
 * chosen range is written back through the NT arbiter contract fields
 * (Assignment, SelectedAlternative, Result).
 *
 * An entry with AlternativeCount == 0 acts as a pure release: the owner's
 * ranges are removed and nothing is reallocated.
 *
 * DeleteOwners distinguishes the two callers: a boot/record operation
 * (TRUE) replaces everything the owner holds, while an incremental pick
 * (FALSE) must leave the owner's earlier committed ranges in place so a
 * device's second BAR cannot land on top of its first.
 *
 * @param[in] Arbiter         The resource arbiter.
 * @param[in] ArbitrationList List of ARBITER_LIST_ENTRY describing requests.
 * @param[in] DeleteOwners    Remove each owner's existing ranges first.
 *
 * @return STATUS_SUCCESS if all requests can be satisfied,
 *         STATUS_CONFLICTING_ADDRESSES if not.
 */
NTSTATUS
PciArbiterTestAllocation(_In_ PPCI_ARBITER Arbiter, _In_ PLIST_ENTRY ArbitrationList, _In_ BOOLEAN DeleteOwners)
{
    NTSTATUS Status;
    PLIST_ENTRY Entry;
    PARBITER_LIST_ENTRY ArbEntry;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG i;
    ULONGLONG Start;
    ULONGLONG Length;
    ULONG Alignment;
    BOOLEAN Satisfied;
    BOOLEAN HadCandidate;

    DPRINT("PCI: TestAllocation for %s arbiter on bus %lu\n",
           Arbiter->ResourceType == CmResourceTypePort ? "I/O" :
           Arbiter->ResourceType == CmResourceTypeMemory ? "Memory" : "BusNumber",
           Arbiter->FdoExtension->BusNumber);

    /* Copy committed allocations to working set */
    RtlFreeRangeList(&Arbiter->PossibleRanges);
    RtlInitializeRangeList(&Arbiter->PossibleRanges);

    Status = RtlCopyRangeList(&Arbiter->PossibleRanges,
                              &Arbiter->AllocatedRanges);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PCI: RtlCopyRangeList failed: 0x%lx\n", Status);
        return Status;
    }

    /* For a record/release operation, drop the devices' existing ranges first */
    if (DeleteOwners)
    {
        for (Entry = ArbitrationList->Flink; Entry != ArbitrationList; Entry = Entry->Flink)
        {
            ArbEntry = CONTAINING_RECORD(Entry, ARBITER_LIST_ENTRY, ListEntry);

            Status = RtlDeleteOwnersRanges(&Arbiter->PossibleRanges, (PVOID)ArbEntry->PhysicalDeviceObject);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("PCI: RtlDeleteOwnersRanges failed: 0x%lx\n", Status);
                goto Cleanup;
            }
        }
    }

    /* Try to satisfy each request */
    for (Entry = ArbitrationList->Flink;
         Entry != ArbitrationList;
         Entry = Entry->Flink)
    {
        ArbEntry = CONTAINING_RECORD(Entry, ARBITER_LIST_ENTRY, ListEntry);

        Satisfied = FALSE;
        HadCandidate = FALSE;

        for (i = 0; i < ArbEntry->AlternativeCount; i++)
        {
            Descriptor = &ArbEntry->Alternatives[i];

            /* Skip descriptors that don't match our resource type */
            if (Descriptor->Type != Arbiter->ResourceType)
                continue;

            HadCandidate = TRUE;

            /*
             * A boot configuration is a statement of fact, not a request:
             * it is recorded even when it overlaps (IDE compatibility-mode
             * channels legitimately alias their legacy ranges).  Skip the
             * availability check and add with conflicts allowed.
             */
            if (ArbEntry->Flags & ARBITER_FLAG_BOOT_CONFIG)
            {
                if (Descriptor->Type == CmResourceTypeBusNumber)
                {
                    Start = Descriptor->u.BusNumber.MinBusNumber;
                    Length = Descriptor->u.BusNumber.Length;
                }
                else
                {
                    Start = Descriptor->u.Generic.MinimumAddress.QuadPart;
                    Length = Descriptor->u.Generic.Length;
                }

                if (Length == 0)
                    continue;

                Status = RtlAddRange(&Arbiter->PossibleRanges, Start, Start + Length - 1, 0, RTL_RANGE_LIST_ADD_IF_CONFLICT | RTL_RANGE_LIST_ADD_SHARED, NULL, (PVOID)ArbEntry->PhysicalDeviceObject);
                if (!NT_SUCCESS(Status))
                    continue;

                PciArbiterWriteAssignment(ArbEntry, Descriptor, Start, Length);
                Satisfied = TRUE;
                break;
            }

            if (Descriptor->Type == CmResourceTypePort ||
                Descriptor->Type == CmResourceTypeMemory)
            {
                Length = Descriptor->u.Generic.Length;
                Alignment = Descriptor->u.Generic.Alignment ? Descriptor->u.Generic.Alignment : 1;
                Status = RtlFindRange(&Arbiter->PossibleRanges, Descriptor->u.Generic.MinimumAddress.QuadPart, Descriptor->u.Generic.MaximumAddress.QuadPart, (ULONG)Length, Alignment, 0, 0, NULL, NULL, &Start);
            }
            else if (Descriptor->Type == CmResourceTypeBusNumber)
            {
                Length = Descriptor->u.BusNumber.Length;
                Status = RtlFindRange(&Arbiter->PossibleRanges, (ULONGLONG)Descriptor->u.BusNumber.MinBusNumber, (ULONGLONG)Descriptor->u.BusNumber.MaxBusNumber, Descriptor->u.BusNumber.Length, 1, 0, 0, NULL, NULL, &Start);
            }
            else
            {
                continue;
            }

            if (!NT_SUCCESS(Status) || Length == 0)
            {
                /* This alternative cannot be satisfied, try the next one */
                continue;
            }

            /* Reserve the found range in the working set */
            Status = RtlAddRange(&Arbiter->PossibleRanges, Start, Start + Length - 1, 0, RTL_RANGE_LIST_ADD_IF_CONFLICT, NULL, (PVOID)ArbEntry->PhysicalDeviceObject);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("PCI: TestAllocation - RtlAddRange failed: 0x%lx\n", Status);
                continue;
            }

            PciArbiterWriteAssignment(ArbEntry, Descriptor, Start, Length);
            Satisfied = TRUE;
            break;
        }

        if (!Satisfied && HadCandidate)
        {
            DPRINT("PCI: TestAllocation - no range found for device %p\n", ArbEntry->PhysicalDeviceObject);
            ArbEntry->Result = ArbiterResultExternalConflict;
            Status = STATUS_CONFLICTING_ADDRESSES;
            goto Cleanup;
        }

        if (!HadCandidate && ArbEntry->AlternativeCount == 0)
        {
            /* Pure release request: owner ranges were already deleted above */
            ArbEntry->Result = ArbiterResultSuccess;
        }
    }

    Arbiter->TransactionInProgress = TRUE;
    return STATUS_SUCCESS;

Cleanup:
    RtlFreeRangeList(&Arbiter->PossibleRanges);
    RtlInitializeRangeList(&Arbiter->PossibleRanges);
    return Status;
}

/**
 * @brief Commit a successful allocation.
 *
 * The PossibleRanges list (which now contains the new allocation state)
 * is copied into AllocatedRanges.  An RTL_RANGE_LIST embeds a LIST_ENTRY
 * head, so the lists must NEVER be swapped by struct copy: the first and
 * last entries would keep pointing at the old head's address and every
 * subsequent walk of the list would run off into freed memory.
 *
 * @param[in] Arbiter The resource arbiter.
 *
 * @return NTSTATUS.
 */
NTSTATUS
PciArbiterCommitAllocation(
    _In_ PPCI_ARBITER Arbiter)
{
    NTSTATUS Status;

    DPRINT("PCI: CommitAllocation for %s arbiter on bus %lu\n",
           Arbiter->ResourceType == CmResourceTypePort ? "I/O" :
           Arbiter->ResourceType == CmResourceTypeMemory ? "Memory" : "BusNumber",
           Arbiter->FdoExtension->BusNumber);

    /* Replace the committed state with the working state */
    RtlFreeRangeList(&Arbiter->AllocatedRanges);
    Status = RtlCopyRangeList(&Arbiter->AllocatedRanges, &Arbiter->PossibleRanges);
    if (!NT_SUCCESS(Status))
    {
        /* AllocatedRanges is empty but consistent; report the failure */
        DPRINT1("PCI: CommitAllocation - RtlCopyRangeList failed: 0x%lx\n", Status);
    }

    RtlFreeRangeList(&Arbiter->PossibleRanges);
    RtlInitializeRangeList(&Arbiter->PossibleRanges);

    Arbiter->TransactionInProgress = FALSE;

    return Status;
}

/**
 * @brief Rollback a failed allocation attempt.
 *
 * Discards the working PossibleRanges list, reverting to the previous
 * committed state in AllocatedRanges.
 *
 * @param[in] Arbiter The resource arbiter.
 *
 * @return STATUS_SUCCESS.
 */
NTSTATUS
PciArbiterRollbackAllocation(
    _In_ PPCI_ARBITER Arbiter)
{
    DPRINT("PCI: RollbackAllocation for %s arbiter on bus %lu\n",
           Arbiter->ResourceType == CmResourceTypePort ? "I/O" :
           Arbiter->ResourceType == CmResourceTypeMemory ? "Memory" : "BusNumber",
           Arbiter->FdoExtension->BusNumber);

    /* Discard the working copy */
    RtlFreeRangeList(&Arbiter->PossibleRanges);
    RtlInitializeRangeList(&Arbiter->PossibleRanges);

    Arbiter->TransactionInProgress = FALSE;

    return STATUS_SUCCESS;
}

/**
 * @brief Mark a range as permanently unavailable in an arbiter.
 *
 * Blocker ranges are owned by the FDO extension itself so that
 * RtlDeleteOwnersRanges for a device PDO can never remove them.
 */
static
NTSTATUS
PciArbiterBlockRange(_In_ PPCI_ARBITER Arbiter, _In_ ULONGLONG Start, _In_ ULONGLONG End)
{
    NTSTATUS Status;

    if (Start > End)
        return STATUS_SUCCESS;

    Status = RtlAddRange(&Arbiter->AllocatedRanges, Start, End, 0,      /* Attributes */ RTL_RANGE_LIST_ADD_IF_CONFLICT, NULL,   /* UserData */ (PVOID)Arbiter->FdoExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PCI: BlockRange [%I64x-%I64x] failed: 0x%lx\n", Start, End, Status);
    }

    return Status;
}

/**
 * @brief Seed an FDO's arbiters with the windows the bus decodes.
 *
 * The range lists hold OCCUPIED space, so constraining allocation to the
 * bridge windows means blocking the complement of the windows.  A root
 * FDO usually receives no memory/port descriptors in its start resources;
 * in that case those arbiters stay unconstrained and the global resource
 * map remains the only guard.  A bridge FDO that decodes no window of a
 * given type gets that entire space blocked, matching the hardware.
 *
 * The bus-number arbiter is always constrained to the FDO's bus range,
 * and the FDO's own bus number is blocked so a child bridge can never
 * alias its parent.
 */
VOID
PciArbiterSeedWindows(_In_ PFDO_DEVICE_EXTENSION FdoExtension, _In_ PCM_RESOURCE_LIST AllocatedResources, _In_ BOOLEAN IsBridgeFdo)
{
    PPCI_ARBITER Arbiter;
    RTL_RANGE_LIST WindowList;
    RTL_RANGE_LIST BlockedList;
    RTL_RANGE_LIST_ITERATOR Iterator;
    PRTL_RANGE Range;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc;
    CM_RESOURCE_TYPE Type;
    ULONG WindowCount;
    ULONG i;
    ULONG ii;
    static const CM_RESOURCE_TYPE SeedTypes[] = { CmResourceTypePort, CmResourceTypeMemory };

    /* Constrain the I/O and memory arbiters to the decoded windows */
    for (i = 0; i < RTL_NUMBER_OF(SeedTypes); i++)
    {
        Type = SeedTypes[i];

        Arbiter = PciFindArbiter(FdoExtension, Type);
        if (!Arbiter)
            continue;

        RtlInitializeRangeList(&WindowList);
        WindowCount = 0;

        if (AllocatedResources && AllocatedResources->Count >= 1)
        {
            PCM_PARTIAL_RESOURCE_LIST PartialList = &AllocatedResources->List[0].PartialResourceList;

            for (ii = 0; ii < PartialList->Count; ii++)
            {
                ULONGLONG Start, Length;

                Desc = &PartialList->PartialDescriptors[ii];
                if (Desc->Type != Type)
                    continue;

                if (Type == CmResourceTypePort)
                {
                    Start = (ULONGLONG)Desc->u.Port.Start.QuadPart;
                    Length = Desc->u.Port.Length;
                }
                else
                {
                    Start = (ULONGLONG)Desc->u.Memory.Start.QuadPart;
                    Length = Desc->u.Memory.Length;
                }

                if (Length == 0)
                    continue;

                if (NT_SUCCESS(RtlAddRange(&WindowList, Start, Start + Length - 1, 0, RTL_RANGE_LIST_ADD_IF_CONFLICT, NULL, NULL)))
                {
                    WindowCount++;
                }
            }
        }

        if (WindowCount != 0)
        {
            /* Block everything outside the windows */
            RtlInitializeRangeList(&BlockedList);
            if (NT_SUCCESS(RtlInvertRangeList(&BlockedList, &WindowList)))
            {
                if (!NT_SUCCESS(RtlGetFirstRange(&BlockedList, &Iterator, &Range)))
                    Range = NULL;
                while (Range)
                {
                    PciArbiterBlockRange(Arbiter, Range->Start, Range->End);
                    if (!NT_SUCCESS(RtlGetNextRange(&Iterator, &Range, TRUE)))
                        Range = NULL;
                }
            }
            RtlFreeRangeList(&BlockedList);
        }
        else if (IsBridgeFdo)
        {
            /* Bridge with this window disabled: it forwards nothing */
            PciArbiterBlockRange(Arbiter, 0, MAXULONGLONG);
        }

        RtlFreeRangeList(&WindowList);
    }

    /* Constrain the bus-number arbiter to the FDO's bus range */
    Arbiter = PciFindArbiter(FdoExtension, CmResourceTypeBusNumber);
    if (Arbiter)
    {
        if (FdoExtension->BusRangeStart > 0)
            PciArbiterBlockRange(Arbiter, 0, FdoExtension->BusRangeStart - 1);
        PciArbiterBlockRange(Arbiter, FdoExtension->BusRangeEnd + 1, MAXULONGLONG);

        /* The FDO's own bus number is taken */
        PciArbiterBlockRange(Arbiter, FdoExtension->BusNumber, FdoExtension->BusNumber);
    }

    DPRINT1("PCI: Seeded arbiter windows for bus %lu (bridge=%d)\n", FdoExtension->BusNumber, IsBridgeFdo);
}

/**
 * @brief Handle an arbiter action request.
 *
 * This is the main dispatch function registered as the ARBITER_INTERFACE
 * handler. It dispatches ArbiterActionTestAllocation, CommitAllocation,
 * RollbackAllocation, and BootAllocation requests.
 *
 * @param[in]    Context    Pointer to PCI_ARBITER.
 * @param[in]    Action     The arbiter action to perform.
 * @param[inout] Parameters Action-specific parameters.
 *
 * @return NTSTATUS.
 */
NTSTATUS
NTAPI
PciArbiterHandler(
    _Inout_opt_ PVOID Context,
    _In_ ARBITER_ACTION Action,
    _Inout_ PARBITER_PARAMETERS Parameters)
{
    PPCI_ARBITER Arbiter = (PPCI_ARBITER)Context;
    NTSTATUS Status;

    if (!Arbiter || Arbiter->Signature != PCI_ARBITER_SIGNATURE)
    {
        DPRINT1("PCI: ArbiterHandler called with invalid context\n");
        return STATUS_INVALID_PARAMETER;
    }

    PciArbiterAcquireMutex(Arbiter);

    switch (Action)
    {
        case ArbiterActionTestAllocation:
            Status = PciArbiterTestAllocation(Arbiter, Parameters->Parameters.TestAllocation.ArbitrationList, FALSE);
            break;

        case ArbiterActionCommitAllocation:
            Status = PciArbiterCommitAllocation(Arbiter);
            break;

        case ArbiterActionRollbackAllocation:
            Status = PciArbiterRollbackAllocation(Arbiter);
            break;

        case ArbiterActionRetestAllocation:
            /* Retest uses same logic as test */
            Status = PciArbiterTestAllocation(Arbiter, Parameters->Parameters.RetestAllocation.ArbitrationList, FALSE);
            break;

        case ArbiterActionBootAllocation:
            /* Record a final assignment: replace whatever the owner held */
            Status = PciArbiterTestAllocation(Arbiter, Parameters->Parameters.BootAllocation.ArbitrationList, TRUE);
            if (NT_SUCCESS(Status))
            {
                Status = PciArbiterCommitAllocation(Arbiter);
            }
            break;

        default:
            DPRINT1("PCI: ArbiterHandler unsupported action %d\n", Action);
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    PciArbiterReleaseMutex(Arbiter);
    return Status;
}

/* ---- Arbiter Interface Reference Counting ---- */

static
VOID
NTAPI
PciArbiterInterfaceReference(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    /* Arbiter lifetime is tied to the FDO, no separate refcounting needed */
}

static
VOID
NTAPI
PciArbiterInterfaceDereference(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

/* ---- Arbiter Lifecycle ---- */

/**
 * @brief Initialize a single resource arbiter.
 */
static
NTSTATUS
PciInitializeArbiter(
    _Out_ PPCI_ARBITER Arbiter,
    _In_ CM_RESOURCE_TYPE ResourceType,
    _In_ PFDO_DEVICE_EXTENSION FdoExtension)
{
    RtlZeroMemory(Arbiter, sizeof(PCI_ARBITER));

    Arbiter->Signature = PCI_ARBITER_SIGNATURE;
    Arbiter->ResourceType = ResourceType;
    Arbiter->FdoExtension = FdoExtension;

    RtlInitializeRangeList(&Arbiter->AllocatedRanges);
    RtlInitializeRangeList(&Arbiter->PossibleRanges);

    /* Initialize mutex event as SynchronizationEvent, initially signaled */
    KeInitializeEvent(&Arbiter->MutexEvent, SynchronizationEvent, TRUE);

    Arbiter->TransactionInProgress = FALSE;
    Arbiter->Initialized = TRUE;

    DPRINT("PCI: Initialized %s arbiter for bus %lu\n",
           ResourceType == CmResourceTypePort ? "I/O" :
           ResourceType == CmResourceTypeMemory ? "Memory" :
           ResourceType == CmResourceTypeBusNumber ? "BusNumber" : "Unknown",
           FdoExtension->BusNumber);

    return STATUS_SUCCESS;
}

/**
 * @brief Destroy a single resource arbiter and free its range lists.
 */
static
VOID
PciDestroyArbiter(
    _Inout_ PPCI_ARBITER Arbiter)
{
    if (!Arbiter->Initialized)
        return;

    DPRINT("PCI: Destroying %s arbiter for bus %lu\n",
           Arbiter->ResourceType == CmResourceTypePort ? "I/O" :
           Arbiter->ResourceType == CmResourceTypeMemory ? "Memory" :
           Arbiter->ResourceType == CmResourceTypeBusNumber ? "BusNumber" : "Unknown",
           Arbiter->FdoExtension->BusNumber);

    RtlFreeRangeList(&Arbiter->AllocatedRanges);
    RtlFreeRangeList(&Arbiter->PossibleRanges);

    Arbiter->Initialized = FALSE;
    Arbiter->Signature = 0;
}

/**
 * @brief Create I/O, Memory, and Bus Number arbiters for an FDO.
 *
 * Called during IRP_MN_START_DEVICE for each PCI root bus.
 * Allocates and initializes three arbiters per root.
 */
NTSTATUS
PciCreateFdoArbiters(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension)
{
    PPCI_ARBITER IoArbiter = NULL;
    PPCI_ARBITER MemArbiter = NULL;
    PPCI_ARBITER BusArbiter = NULL;
    NTSTATUS Status;

    DPRINT1("PCI: Creating arbiters for bus %lu\n", FdoExtension->BusNumber);

    /* Allocate I/O port arbiter */
    IoArbiter = ExAllocatePoolWithTag(NonPagedPool, sizeof(PCI_ARBITER), TAG_PCI_ARB);
    if (!IoArbiter)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = PciInitializeArbiter(IoArbiter, CmResourceTypePort, FdoExtension);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Allocate Memory arbiter */
    MemArbiter = ExAllocatePoolWithTag(NonPagedPool, sizeof(PCI_ARBITER), TAG_PCI_ARB);
    if (!MemArbiter)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = PciInitializeArbiter(MemArbiter, CmResourceTypeMemory, FdoExtension);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Allocate Bus Number arbiter */
    BusArbiter = ExAllocatePoolWithTag(NonPagedPool, sizeof(PCI_ARBITER), TAG_PCI_ARB);
    if (!BusArbiter)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = PciInitializeArbiter(BusArbiter, CmResourceTypeBusNumber, FdoExtension);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Add all arbiters to the FDO's list */
    InsertTailList(&FdoExtension->ArbiterListHead, &IoArbiter->ListEntry);
    InsertTailList(&FdoExtension->ArbiterListHead, &MemArbiter->ListEntry);
    InsertTailList(&FdoExtension->ArbiterListHead, &BusArbiter->ListEntry);

    DPRINT1("PCI: Created 3 arbiters for bus %lu\n", FdoExtension->BusNumber);
    return STATUS_SUCCESS;

Cleanup:
    if (IoArbiter)
    {
        PciDestroyArbiter(IoArbiter);
        ExFreePoolWithTag(IoArbiter, TAG_PCI_ARB);
    }
    if (MemArbiter)
    {
        PciDestroyArbiter(MemArbiter);
        ExFreePoolWithTag(MemArbiter, TAG_PCI_ARB);
    }
    if (BusArbiter)
    {
        PciDestroyArbiter(BusArbiter);
        ExFreePoolWithTag(BusArbiter, TAG_PCI_ARB);
    }

    return Status;
}

/**
 * @brief Destroy all arbiters for an FDO.
 *
 * Called during IRP_MN_REMOVE_DEVICE.
 */
VOID
PciDestroyFdoArbiters(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension)
{
    PLIST_ENTRY Entry;
    PPCI_ARBITER Arbiter;

    DPRINT1("PCI: Destroying arbiters for bus %lu\n", FdoExtension->BusNumber);

    while (!IsListEmpty(&FdoExtension->ArbiterListHead))
    {
        Entry = RemoveHeadList(&FdoExtension->ArbiterListHead);
        Arbiter = CONTAINING_RECORD(Entry, PCI_ARBITER, ListEntry);

        PciDestroyArbiter(Arbiter);
        ExFreePoolWithTag(Arbiter, TAG_PCI_ARB);
    }
}

/**
 * @brief Find the arbiter for a given resource type on an FDO.
 *
 * @param[in] FdoExtension The FDO extension to search.
 * @param[in] ResourceType The resource type to find.
 *
 * @return Pointer to the arbiter, or NULL if not found.
 */
PPCI_ARBITER
PciFindArbiter(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _In_ CM_RESOURCE_TYPE ResourceType)
{
    PLIST_ENTRY Entry;
    PPCI_ARBITER Arbiter;

    for (Entry = FdoExtension->ArbiterListHead.Flink;
         Entry != &FdoExtension->ArbiterListHead;
         Entry = Entry->Flink)
    {
        Arbiter = CONTAINING_RECORD(Entry, PCI_ARBITER, ListEntry);

        if (Arbiter->Initialized &&
            Arbiter->Signature == PCI_ARBITER_SIGNATURE &&
            Arbiter->ResourceType == ResourceType)
        {
            return Arbiter;
        }
    }

    return NULL;
}

/**
 * @brief Build an ARBITER_INTERFACE for a given resource type.
 *
 * Called when the PnP manager queries for IRP_MN_QUERY_INTERFACE with
 * GUID_ARBITER_INTERFACE_STANDARD. Fills in the interface structure so
 * the PnP manager can call our arbiter handler.
 *
 * @param[in]  FdoExtension The FDO extension.
 * @param[in]  ResourceType The resource type requested.
 * @param[out] Interface    The arbiter interface to fill.
 *
 * @return STATUS_SUCCESS or STATUS_NOT_FOUND.
 */
NTSTATUS
PciQueryArbiterInterface(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _In_ CM_RESOURCE_TYPE ResourceType,
    _Out_ PARBITER_INTERFACE Interface)
{
    PPCI_ARBITER Arbiter;

    Arbiter = PciFindArbiter(FdoExtension, ResourceType);
    if (!Arbiter)
    {
        DPRINT("PCI: No arbiter found for resource type %u on bus %lu\n",
               ResourceType, FdoExtension->BusNumber);
        return STATUS_NOT_FOUND;
    }

    Interface->Size = sizeof(ARBITER_INTERFACE);
    Interface->Version = 1;
    Interface->Context = Arbiter;
    Interface->InterfaceReference = PciArbiterInterfaceReference;
    Interface->InterfaceDereference = PciArbiterInterfaceDereference;
    Interface->ArbiterHandler = PciArbiterHandler;
    Interface->Flags = 0;

    DPRINT("PCI: Returning %s arbiter interface for bus %lu\n",
           ResourceType == CmResourceTypePort ? "I/O" :
           ResourceType == CmResourceTypeMemory ? "Memory" : "BusNumber",
           FdoExtension->BusNumber);

    return STATUS_SUCCESS;
}
