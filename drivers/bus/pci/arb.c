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
 * @brief Test whether a proposed allocation can be satisfied.
 *
 * Copies the committed AllocatedRanges into PossibleRanges, then removes
 * the requesting device's existing ranges so they can be reassigned.
 * For each requirement, calls RtlFindRange to check if a suitable range
 * exists in the available space.
 *
 * @param[in] Arbiter         The resource arbiter.
 * @param[in] ArbitrationList List of ARBITER_LIST_ENTRY describing requests.
 *
 * @return STATUS_SUCCESS if all requests can be satisfied,
 *         STATUS_CONFLICTING_ADDRESSES if not.
 */
NTSTATUS
PciArbiterTestAllocation(
    _In_ PPCI_ARBITER Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    NTSTATUS Status;
    PLIST_ENTRY Entry;
    PARBITER_LIST_ENTRY ArbEntry;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG i;
    ULONGLONG Start;

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

    /* For each entry, remove the device's existing ranges so they can be reallocated */
    for (Entry = ArbitrationList->Flink;
         Entry != ArbitrationList;
         Entry = Entry->Flink)
    {
        ArbEntry = CONTAINING_RECORD(Entry, ARBITER_LIST_ENTRY, ListEntry);

        Status = RtlDeleteOwnersRanges(&Arbiter->PossibleRanges,
                                       (PVOID)ArbEntry->PhysicalDeviceObject);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("PCI: RtlDeleteOwnersRanges failed: 0x%lx\n", Status);
            goto Cleanup;
        }
    }

    /* Try to satisfy each request */
    for (Entry = ArbitrationList->Flink;
         Entry != ArbitrationList;
         Entry = Entry->Flink)
    {
        ArbEntry = CONTAINING_RECORD(Entry, ARBITER_LIST_ENTRY, ListEntry);

        for (i = 0; i < ArbEntry->AlternativeCount; i++)
        {
            Descriptor = &ArbEntry->Alternatives[i];

            /* Skip descriptors that don't match our resource type */
            if (Descriptor->Type != Arbiter->ResourceType)
                continue;

            if (Descriptor->Type == CmResourceTypePort ||
                Descriptor->Type == CmResourceTypeMemory)
            {
                Status = RtlFindRange(&Arbiter->PossibleRanges,
                                      Descriptor->u.Generic.MinimumAddress.QuadPart,
                                      Descriptor->u.Generic.MaximumAddress.QuadPart,
                                      Descriptor->u.Generic.Length,
                                      Descriptor->u.Generic.Alignment,
                                      0,  /* Flags */
                                      0,  /* AttributeAvailableMask */
                                      NULL,  /* Context */
                                      NULL,  /* Callback */
                                      &Start);
            }
            else if (Descriptor->Type == CmResourceTypeBusNumber)
            {
                Status = RtlFindRange(&Arbiter->PossibleRanges,
                                      (ULONGLONG)Descriptor->u.BusNumber.MinBusNumber,
                                      (ULONGLONG)Descriptor->u.BusNumber.MaxBusNumber,
                                      Descriptor->u.BusNumber.Length,
                                      1,  /* Alignment */
                                      0,
                                      0,
                                      NULL,
                                      NULL,
                                      &Start);
            }
            else
            {
                continue;
            }

            if (!NT_SUCCESS(Status))
            {
                DPRINT("PCI: TestAllocation - no range found for device %p, type %u\n",
                       ArbEntry->PhysicalDeviceObject, Descriptor->Type);
                Status = STATUS_CONFLICTING_ADDRESSES;
                goto Cleanup;
            }

            /* Reserve the found range in the working set */
            Status = RtlAddRange(&Arbiter->PossibleRanges,
                                 Start,
                                 Start + Descriptor->u.Generic.Length - 1,
                                 0,  /* Attributes */
                                 RTL_RANGE_LIST_ADD_IF_CONFLICT,
                                 NULL,  /* UserData */
                                 (PVOID)ArbEntry->PhysicalDeviceObject);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("PCI: TestAllocation - RtlAddRange failed: 0x%lx\n", Status);
                goto Cleanup;
            }

            /* Found a valid alternative, move to next entry */
            break;
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
 * @brief Add a range to the arbiter's working (possible) range list.
 *
 * Records a device's allocation in the PossibleRanges list with
 * RTL_RANGE_LIST_ADD_IF_CONFLICT to handle overlapping ranges.
 *
 * @param[in] Arbiter The resource arbiter.
 * @param[in] Start   Start of the range.
 * @param[in] End     End of the range (inclusive).
 * @param[in] Owner   Device object that owns this range.
 *
 * @return NTSTATUS from RtlAddRange.
 */
NTSTATUS
PciArbiterAddAllocation(
    _In_ PPCI_ARBITER Arbiter,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End,
    _In_ PVOID Owner)
{
    NTSTATUS Status;

    DPRINT("PCI: AddAllocation %s [%I64x-%I64x] owner %p\n",
           Arbiter->ResourceType == CmResourceTypePort ? "I/O" :
           Arbiter->ResourceType == CmResourceTypeMemory ? "Memory" : "BusNumber",
           Start, End, Owner);

    Status = RtlAddRange(&Arbiter->PossibleRanges,
                         Start,
                         End,
                         0,  /* Attributes */
                         RTL_RANGE_LIST_ADD_IF_CONFLICT,
                         NULL,  /* UserData */
                         Owner);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PCI: AddAllocation failed: 0x%lx\n", Status);
    }

    return Status;
}

/**
 * @brief Commit a successful allocation.
 *
 * The PossibleRanges list (which now contains the new allocation state)
 * becomes the new AllocatedRanges list. The old AllocatedRanges is freed
 * and repurposed as the new (empty) PossibleRanges container.
 *
 * @param[in] Arbiter The resource arbiter.
 *
 * @return STATUS_SUCCESS.
 */
NTSTATUS
PciArbiterCommitAllocation(
    _In_ PPCI_ARBITER Arbiter)
{
    RTL_RANGE_LIST TempRanges;

    DPRINT("PCI: CommitAllocation for %s arbiter on bus %lu\n",
           Arbiter->ResourceType == CmResourceTypePort ? "I/O" :
           Arbiter->ResourceType == CmResourceTypeMemory ? "Memory" : "BusNumber",
           Arbiter->FdoExtension->BusNumber);

    /* Free the old committed allocations */
    RtlFreeRangeList(&Arbiter->AllocatedRanges);

    /* Swap: possible becomes allocated, old allocated container becomes possible */
    TempRanges = Arbiter->AllocatedRanges;
    Arbiter->AllocatedRanges = Arbiter->PossibleRanges;
    Arbiter->PossibleRanges = TempRanges;

    /* Re-initialize the now-empty PossibleRanges */
    RtlInitializeRangeList(&Arbiter->PossibleRanges);

    Arbiter->TransactionInProgress = FALSE;

    return STATUS_SUCCESS;
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
 * @brief Find a suitable range in the arbiter's available space.
 *
 * Uses RtlFindRange on PossibleRanges to locate a free range that
 * satisfies the given constraints (length, alignment, address bounds).
 *
 * @param[in]  Arbiter     The resource arbiter.
 * @param[in]  MinAddr     Minimum acceptable address.
 * @param[in]  MaxAddr     Maximum acceptable address.
 * @param[in]  Length      Required length of the range.
 * @param[in]  Alignment   Required alignment of the start address.
 * @param[out] ResultStart Receives the start of the found range.
 *
 * @return NTSTATUS from RtlFindRange.
 */
NTSTATUS
PciArbiterFindSuitableRange(
    _In_ PPCI_ARBITER Arbiter,
    _In_ ULONGLONG MinAddr,
    _In_ ULONGLONG MaxAddr,
    _In_ ULONG Length,
    _In_ ULONG Alignment,
    _Out_ PULONGLONG ResultStart)
{
    NTSTATUS Status;

    DPRINT("PCI: FindSuitableRange %s [%I64x-%I64x] len=%lx align=%lx\n",
           Arbiter->ResourceType == CmResourceTypePort ? "I/O" :
           Arbiter->ResourceType == CmResourceTypeMemory ? "Memory" : "BusNumber",
           MinAddr, MaxAddr, Length, Alignment);

    Status = RtlFindRange(&Arbiter->PossibleRanges,
                          MinAddr,
                          MaxAddr,
                          Length,
                          Alignment,
                          0,      /* Flags */
                          0,      /* AttributeAvailableMask */
                          NULL,   /* Context */
                          NULL,   /* Callback */
                          ResultStart);

    if (NT_SUCCESS(Status))
    {
        DPRINT("PCI: FindSuitableRange found start=%I64x\n", *ResultStart);
    }
    else
    {
        DPRINT("PCI: FindSuitableRange failed: 0x%lx\n", Status);
    }

    return Status;
}

/**
 * @brief Initialize the arbiter's PossibleRanges from boot-allocated resources.
 *
 * Given an array of CM_PARTIAL_RESOURCE_DESCRIPTORs (from firmware/BIOS),
 * builds a temporary range list of all allocated ranges for the matching
 * resource type, then inverts it (via RtlInvertRangeList) to produce
 * the list of AVAILABLE ranges.
 *
 * @param[in] Arbiter        The resource arbiter.
 * @param[in] BootResources  Array of resource descriptors from firmware.
 * @param[in] ResourceCount  Number of descriptors in the array.
 *
 * @return NTSTATUS.
 */
NTSTATUS
PciArbiterInitializeFromBootConfig(
    _In_ PPCI_ARBITER Arbiter,
    _In_reads_(ResourceCount) PCM_PARTIAL_RESOURCE_DESCRIPTOR BootResources,
    _In_ ULONG ResourceCount)
{
    NTSTATUS Status;
    RTL_RANGE_LIST TempRangeList;
    ULONG i;
    ULONGLONG Start;
    ULONGLONG Length;
    ULONGLONG End;

    DPRINT("PCI: InitializeFromBootConfig %s arbiter, %lu resources\n",
           Arbiter->ResourceType == CmResourceTypePort ? "I/O" :
           Arbiter->ResourceType == CmResourceTypeMemory ? "Memory" : "BusNumber",
           ResourceCount);

    /* Initialize a temporary range list for boot-allocated ranges */
    RtlInitializeRangeList(&TempRangeList);

    for (i = 0; i < ResourceCount; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &BootResources[i];

        /* Skip descriptors that don't match our resource type.
         * Type 7 (CmResourceTypeMemoryLarge) aliases to Memory. */
        if (Desc->Type != Arbiter->ResourceType &&
            !(Desc->Type == 7 && Arbiter->ResourceType == CmResourceTypeMemory))
        {
            continue;
        }

        /* Extract start and length based on resource type */
        if (Desc->Type == CmResourceTypePort)
        {
            Start = Desc->u.Port.Start.QuadPart;
            Length = (ULONGLONG)Desc->u.Port.Length;
        }
        else if (Desc->Type == CmResourceTypeMemory || Desc->Type == 7)
        {
            Start = Desc->u.Memory.Start.QuadPart;
            Length = (ULONGLONG)Desc->u.Memory.Length;
        }
        else if (Desc->Type == CmResourceTypeBusNumber)
        {
            Start = (ULONGLONG)Desc->u.BusNumber.Start;
            Length = (ULONGLONG)Desc->u.BusNumber.Length;
        }
        else
        {
            continue;
        }

        if (Length == 0)
            continue;

        End = Start + Length - 1;

        DPRINT("PCI: Boot resource %s [%I64x-%I64x]\n",
               Arbiter->ResourceType == CmResourceTypePort ? "I/O" :
               Arbiter->ResourceType == CmResourceTypeMemory ? "Memory" : "BusNumber",
               Start, End);

        /* Add to temp list with ADD_IF_CONFLICT to handle overlaps */
        Status = RtlAddRange(&TempRangeList,
                             Start,
                             End,
                             0,      /* Attributes */
                             RTL_RANGE_LIST_ADD_IF_CONFLICT,
                             NULL,   /* UserData */
                             NULL);  /* Owner */
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("PCI: RtlAddRange for boot resource failed: 0x%lx\n", Status);
            goto Cleanup;
        }
    }

    /* Invert the occupied ranges to get the available ranges */
    RtlFreeRangeList(&Arbiter->PossibleRanges);
    RtlInitializeRangeList(&Arbiter->PossibleRanges);

    Status = RtlInvertRangeList(&Arbiter->PossibleRanges, &TempRangeList);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PCI: RtlInvertRangeList failed: 0x%lx\n", Status);
    }

Cleanup:
    RtlFreeRangeList(&TempRangeList);
    return Status;
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
            Status = PciArbiterTestAllocation(
                         Arbiter,
                         Parameters->Parameters.TestAllocation.ArbitrationList);
            break;

        case ArbiterActionCommitAllocation:
            Status = PciArbiterCommitAllocation(Arbiter);
            break;

        case ArbiterActionRollbackAllocation:
            Status = PciArbiterRollbackAllocation(Arbiter);
            break;

        case ArbiterActionRetestAllocation:
            /* Retest uses same logic as test */
            Status = PciArbiterTestAllocation(
                         Arbiter,
                         Parameters->Parameters.RetestAllocation.ArbitrationList);
            break;

        case ArbiterActionBootAllocation:
            /* Boot allocation: record existing firmware assignments */
            Status = PciArbiterTestAllocation(
                         Arbiter,
                         Parameters->Parameters.BootAllocation.ArbitrationList);
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
