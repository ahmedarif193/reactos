/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     dxgkrnl registration interface and adapter lifecycle
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * This file implements:
 *   DxgkMmsRegister()             — exported; called by dxgkrnl at init
 *   DxgkMmsUnregister()           — exported; called by dxgkrnl at teardown
 *   DxgkMmsAddAdapter()           — allocate per-adapter MMS context
 *   DxgkMmsRemoveAdapter()        — free per-adapter MMS context
 *   DxgkMmsQueryLastCompletedFence() — query per-node completion fence
 */

#include "dxgmms1_private.h"
#include "paging.h"

/* -------------------------------------------------------------------------
 * DxgkMmsAddAdapter
 *
 * Allocate and initialise a DXGMMS_ADAPTER_CONTEXT for a newly started
 * GPU adapter.  Also allocates the per-node and per-segment arrays.
 *
 * Called by dxgkrnl at adapter start time (PASSIVE_LEVEL).
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DxgkMmsAddAdapter(
    _In_  PVOID  AdapterContext,
    _In_  ULONG  NumNodes,
    _In_  ULONG  NumSegments,
    _Out_ PVOID *OutMmsContext)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter;
    PDXGMMS_NODE            NodeArray;
    PDXGMMS_SEGMENT         SegArray;
    ULONG                   i;
    KIRQL                   OldIrql;
    NTSTATUS                Status;

    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsAddAdapter: AdapterContext=%p NumNodes=%u NumSegments=%u\n",
                 AdapterContext, NumNodes, NumSegments);

    if (AdapterContext == NULL || OutMmsContext == NULL)
    {
        DXGMMS_ERR("DxgkMmsAddAdapter: invalid parameters\n");
        return STATUS_INVALID_PARAMETER;
    }

    *OutMmsContext = NULL;

    if (NumNodes > DXGMMS_MAX_NODES)
    {
        DXGMMS_ERR("DxgkMmsAddAdapter: NumNodes=%u exceeds limit %u\n",
                   NumNodes, DXGMMS_MAX_NODES);
        return STATUS_INVALID_PARAMETER;
    }

    if ((NumNodes != 0 &&
         (SIZE_T)NumNodes > ((SIZE_T)-1 / sizeof(DXGMMS_NODE))) ||
        (NumSegments != 0 &&
         (SIZE_T)NumSegments > ((SIZE_T)-1 / sizeof(DXGMMS_SEGMENT))))
    {
        DXGMMS_ERR("DxgkMmsAddAdapter: node/segment array size overflow\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (NumNodes == 0)
    {
        DXGMMS_WARN("DxgkMmsAddAdapter: adapter has zero GPU nodes\n");
    }

    /* Allocate the adapter context from non-paged pool. */
    Adapter = ExAllocatePoolWithTag(NonPagedPool,
                                   sizeof(DXGMMS_ADAPTER_CONTEXT),
                                   DXGMMS1_POOL_TAG);
    if (Adapter == NULL)
    {
        DXGMMS_ERR("DxgkMmsAddAdapter: failed to allocate adapter context\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Adapter, sizeof(DXGMMS_ADAPTER_CONTEXT));

    /* Allocate the node array (may be zero-length). */
    if (NumNodes > 0)
    {
        NodeArray = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(DXGMMS_NODE) * NumNodes,
                                         DXGMMS1_POOL_TAG);
        if (NodeArray == NULL)
        {
            DXGMMS_ERR("DxgkMmsAddAdapter: failed to allocate node array\n");
            ExFreePoolWithTag(Adapter, DXGMMS1_POOL_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(NodeArray, sizeof(DXGMMS_NODE) * NumNodes);
    }
    else
    {
        NodeArray = NULL;
    }

    /* Allocate the segment array (may be zero-length). */
    if (NumSegments > 0)
    {
        SegArray = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(DXGMMS_SEGMENT) * NumSegments,
                                        DXGMMS1_POOL_TAG);
        if (SegArray == NULL)
        {
            DXGMMS_ERR("DxgkMmsAddAdapter: failed to allocate segment array\n");
            if (NodeArray != NULL)
                ExFreePoolWithTag(NodeArray, DXGMMS1_POOL_TAG);
            ExFreePoolWithTag(Adapter, DXGMMS1_POOL_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(SegArray, sizeof(DXGMMS_SEGMENT) * NumSegments);
    }
    else
    {
        SegArray = NULL;
    }

    /* Populate the adapter context header. */
    Adapter->Signature            = DXGMMS_ADAPTER_SIGNATURE;
    Adapter->DxgkrnlContext       = AdapterContext;
    Adapter->NumNodes             = NumNodes;
    Adapter->Nodes                = NodeArray;
    Adapter->NumSegments          = NumSegments;
    Adapter->Segments             = SegArray;
    Adapter->Removed              = FALSE;
    Adapter->EvictCallback        = NULL;
    InitializeListHead(&Adapter->GlobalAllocationList);

    KeInitializeSpinLock(&Adapter->AdapterLock);

    /* Initialise each node. */
    for (i = 0; i < NumNodes; i++)
    {
        PDXGMMS_NODE Node = &NodeArray[i];
        ULONG        p;

        Node->NodeOrdinal             = i;
        Node->AdapterContext          = Adapter;
        Node->RunningBuffer           = NULL;
        Node->CurrentFenceId          = 0;
        Node->LastPreemptedFence      = 0;
        Node->PreemptionFenceId       = 0;
        Node->LastSubmittedFenceId    = 0;
        Node->LastCompletedFenceId    = 0;
        Node->PreemptionPending       = FALSE;

        KeInitializeSpinLock(&Node->Lock);

        /* Initialise one ready queue per priority band. */
        for (p = 0; p < DXGMMS_PRIORITY_LEVELS; p++)
            InitializeListHead(&Node->ReadyQueue[p]);

        KeInitializeDpc(&Node->QuantumDpc, NULL, Node);
        KeInitializeTimerEx(&Node->QuantumTimer, SynchronizationTimer);

        /*
         * Initialise the idle event as a notification event (manual reset)
         * in the signalled state: the engine starts idle.
         */
        KeInitializeEvent(&Node->IdleEvent, NotificationEvent, TRUE);

        Status = DxgkMmsInitPreemption(Node);
        if (!NT_SUCCESS(Status))
        {
            DXGMMS_ERR("DxgkMmsAddAdapter: preemption init failed for node "
                       "%u: 0x%08lx\n", i, Status);
            goto Failure;
        }

        DXGMMS_TRACE("DxgkMmsAddAdapter: initialised node %u\n", i);
    }

    /* Initialise each segment's spinlock, free list, and allocation list. */
    for (i = 0; i < NumSegments; i++)
    {
        PDXGMMS_SEGMENT Seg = &SegArray[i];

        Seg->SegmentId        = i;
        Seg->AperturePages    = NULL;
        Seg->NumAperturePages = 0;
        KeInitializeSpinLock(&Seg->Lock);
        InitializeListHead(&Seg->FreeListHead);
        InitializeListHead(&Seg->AllocationList);

        DXGMMS_TRACE("DxgkMmsAddAdapter: initialised segment %u\n", i);
    }

    Status = DxgkMmsTdrInit(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsAddAdapter: TDR init failed 0x%08lx\n", Status);
        goto Failure;
    }

    Status = DxgkMmsInitializePagingBuffer(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsAddAdapter: paging buffer init failed 0x%08lx\n",
                   Status);
        goto Failure;
    }

    /*
     * Link the adapter into the global list under the list spinlock so
     * DxgkMmsUnload can verify all adapters have been removed before the
     * driver image is unloaded.
     */
    KeAcquireSpinLock(&DxgMmsAdapterListLock, &OldIrql);
    InsertTailList(&DxgMmsAdapterListHead, &Adapter->GlobalListEntry);
    KeReleaseSpinLock(&DxgMmsAdapterListLock, OldIrql);

    *OutMmsContext = Adapter;

    DXGMMS_TRACE("DxgkMmsAddAdapter: adapter context %p registered\n", Adapter);
    return STATUS_SUCCESS;

Failure:
    DxgkMmsFreePagingBuffer(Adapter);

    if (SegArray != NULL)
        ExFreePoolWithTag(SegArray, DXGMMS1_POOL_TAG);

    if (NodeArray != NULL)
        ExFreePoolWithTag(NodeArray, DXGMMS1_POOL_TAG);

    ExFreePoolWithTag(Adapter, DXGMMS1_POOL_TAG);
    return Status;
}

/* -------------------------------------------------------------------------
 * DxgkMmsRemoveAdapter
 *
 * Tear down the per-adapter context created by DxgkMmsAddAdapter.
 * Called by dxgkrnl at adapter stop/remove time (PASSIVE_LEVEL).
 * ------------------------------------------------------------------------- */
VOID
NTAPI
DxgkMmsRemoveAdapter(
    _In_ PVOID MmsContext)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter = (PDXGMMS_ADAPTER_CONTEXT)MmsContext;
    KIRQL                   OldIrql;
    ULONG                   i;
    ULONG                   p;

    PAGED_CODE();

    if (Adapter == NULL)
    {
        DXGMMS_WARN("DxgkMmsRemoveAdapter: called with NULL context\n");
        return;
    }

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    DXGMMS_TRACE("DxgkMmsRemoveAdapter: removing adapter %p\n", Adapter);

    /* Prevent any further work submission. */
    KeAcquireSpinLock(&Adapter->AdapterLock, &OldIrql);
    if (Adapter->Removed)
    {
        KeReleaseSpinLock(&Adapter->AdapterLock, OldIrql);
        DXGMMS_WARN("DxgkMmsRemoveAdapter: adapter already removed\n");
        DxgMmsAssert(FALSE, DXGMMS1_BUGCHECK_DOUBLE_REMOVE);
        return;
    }
    Adapter->Removed = TRUE;
    KeReleaseSpinLock(&Adapter->AdapterLock, OldIrql);

    KeCancelTimer(&Adapter->TdrState.TdrTimer);
    KeRemoveQueueDpc(&Adapter->TdrState.TdrDpc);

    for (i = 0; i < Adapter->NumNodes; i++)
    {
        PDXGMMS_NODE Node = &Adapter->Nodes[i];
        PDXGMMS_COMMAND_BUFFER CmdBuf;

        KeCancelTimer(&Node->QuantumTimer);
        KeRemoveQueueDpc(&Node->QuantumDpc);
        KeCancelTimer(&Node->PreemptState.PreemptionTimer);
        KeRemoveQueueDpc(&Node->PreemptState.PreemptionTimeoutDpc);

        KeAcquireSpinLock(&Node->Lock, &OldIrql);

        CmdBuf = Node->RunningBuffer;
        Node->RunningBuffer = NULL;
        if (CmdBuf != NULL)
            ExFreePoolWithTag(CmdBuf, TAG_DXGMMS_CMD_BUF);

        for (p = 0; p < DXGMMS_PRIORITY_LEVELS; p++)
        {
            while (!IsListEmpty(&Node->ReadyQueue[p]))
            {
                PLIST_ENTRY Entry = RemoveHeadList(&Node->ReadyQueue[p]);
                CmdBuf = CONTAINING_RECORD(Entry,
                                           DXGMMS_COMMAND_BUFFER,
                                           QueueEntry);
                ExFreePoolWithTag(CmdBuf, TAG_DXGMMS_CMD_BUF);
            }
        }

        KeSetEvent(&Node->IdleEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&Node->Lock, OldIrql);
    }

    DxgkMmsFreePagingBuffer(Adapter);

    /*
     * Free segment free-list entries and aperture page arrays.
     * At this point dxgkrnl must have evicted all allocations.
     */
    for (i = 0; i < Adapter->NumSegments; i++)
    {
        PDXGMMS_SEGMENT Seg = &Adapter->Segments[i];

        /* Free the free-list blocks. */
        while (!IsListEmpty(&Seg->FreeListHead))
        {
            PLIST_ENTRY Entry = RemoveHeadList(&Seg->FreeListHead);
            PDXGMMS_FREE_BLOCK Block = CONTAINING_RECORD(Entry,
                                                          DXGMMS_FREE_BLOCK,
                                                          ListEntry);
            ExFreePoolWithTag(Block, DXGMMS1_POOL_TAG);
        }

        /* Free the aperture PFN array if present. */
        if (Seg->AperturePages != NULL)
        {
            ExFreePoolWithTag(Seg->AperturePages, DXGMMS1_POOL_TAG);
            Seg->AperturePages    = NULL;
            Seg->NumAperturePages = 0;
        }

        if (!IsListEmpty(&Seg->AllocationList))
        {
            DXGMMS_WARN("DxgkMmsRemoveAdapter: segment %u still has resident "
                        "allocations at teardown\n", i);
        }
    }

    /* Remove from the global list. */
    KeAcquireSpinLock(&DxgMmsAdapterListLock, &OldIrql);
    RemoveEntryList(&Adapter->GlobalListEntry);
    KeReleaseSpinLock(&DxgMmsAdapterListLock, OldIrql);

    /* Free sub-allocations. */
    if (Adapter->Segments != NULL)
        ExFreePoolWithTag(Adapter->Segments, DXGMMS1_POOL_TAG);

    if (Adapter->Nodes != NULL)
        ExFreePoolWithTag(Adapter->Nodes, DXGMMS1_POOL_TAG);

    /* Poison the signature to catch use-after-free in debug builds. */
#if DBG
    Adapter->Signature = 0xDEADBEEF;
#endif

    ExFreePoolWithTag(Adapter, DXGMMS1_POOL_TAG);

    DXGMMS_TRACE("DxgkMmsRemoveAdapter: adapter freed\n");
}

/* -------------------------------------------------------------------------
 * DxgkMmsQueryLastCompletedFence
 *
 * Return the monotonically increasing fence value of the last completed
 * DMA buffer on the given engine node.
 *
 * This can be called at DISPATCH_LEVEL (e.g. from a DPC), so the read
 * must be atomic.  On x86 and amd64, 64-bit reads are not guaranteed to
 * be atomic unless the operand is naturally aligned and the read is
 * performed through an interlocked primitive.  We use
 * InterlockedCompareExchange64 to guarantee atomicity.
 * ------------------------------------------------------------------------- */
ULONG64
NTAPI
DxgkMmsQueryLastCompletedFence(
    _In_ PVOID MmsContext,
    _In_ ULONG NodeOrdinal)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter = (PDXGMMS_ADAPTER_CONTEXT)MmsContext;
    PDXGMMS_NODE            Node;
    LONG64                  FenceId;

    if (Adapter == NULL)
    {
        DXGMMS_WARN("DxgkMmsQueryLastCompletedFence: NULL context\n");
        return 0;
    }

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (NodeOrdinal >= Adapter->NumNodes)
    {
        DXGMMS_WARN("DxgkMmsQueryLastCompletedFence: NodeOrdinal %u out of range "
                    "(NumNodes=%u)\n", NodeOrdinal, Adapter->NumNodes);
        return 0;
    }

    Node = &Adapter->Nodes[NodeOrdinal];

    /*
     * Atomically read LastCompletedFenceId using the compare-exchange
     * read trick: compare with current value, exchange writes back the
     * same value (no-op), and returns the current value atomically.
     */
    FenceId = InterlockedCompareExchange64(
                  (volatile LONG64 *)&Node->LastCompletedFenceId,
                  0,
                  0);

    return (ULONG64)FenceId;
}

/* -------------------------------------------------------------------------
 * DxgkMmsRegister  (exported)
 *
 * Called by dxgkrnl to obtain the DXGMMS_INTERFACE dispatch table.
 *
 * IRQL: PASSIVE_LEVEL
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DxgkMmsRegister(
    _In_  PVOID            DxgkrnlContext,
    _Out_ PDXGMMS_INTERFACE Interface)
{
    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsRegister: DxgkrnlContext=%p\n", DxgkrnlContext);

    if (Interface == NULL)
    {
        DXGMMS_ERR("DxgkMmsRegister: NULL Interface pointer\n");
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Interface, sizeof(DXGMMS_INTERFACE));

    Interface->Size    = sizeof(DXGMMS_INTERFACE);
    Interface->Version = DXGMMS_INTERFACE_VERSION;

    Interface->ScheduleCommand          = DxgkMmsScheduleCommand;
    Interface->PreemptCommand           = DxgkMmsPreemptCommand;
    Interface->WaitForIdleGpu           = DxgkMmsWaitForIdleGpu;

    Interface->BuildPagingBuffer        = DxgkMmsBuildPagingBuffer;
    Interface->SubmitPagingBuffer       = DxgkMmsSubmitPagingBuffer;

    Interface->InitializeSegment        = DxgkMmsInitializeSegment;
    Interface->AllocateFromSegment      = DxgkMmsAllocateFromSegment;
    Interface->FreeFromSegment          = DxgkMmsFreeFromSegment;

    Interface->AddAdapter               = DxgkMmsAddAdapter;
    Interface->RemoveAdapter            = DxgkMmsRemoveAdapter;

    Interface->QueryLastCompletedFence  = DxgkMmsQueryLastCompletedFence;

    DXGMMS_TRACE("DxgkMmsRegister: interface v%u registered (size=%u)\n",
                 Interface->Version, Interface->Size);

    return STATUS_SUCCESS;
}

/* -------------------------------------------------------------------------
 * DxgkMmsUnregister  (exported)
 *
 * Called by dxgkrnl when it no longer needs the DXGMMS_INTERFACE.
 *
 * IRQL: PASSIVE_LEVEL
 * ------------------------------------------------------------------------- */
VOID
NTAPI
DxgkMmsUnregister(
    _In_ PVOID DxgkrnlContext)
{
    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsUnregister: DxgkrnlContext=%p\n", DxgkrnlContext);

    if (!IsListEmpty(&DxgMmsAdapterListHead))
    {
        DXGMMS_WARN("DxgkMmsUnregister: adapter list is not empty — "
                    "DxgkMmsRemoveAdapter was not called for all adapters\n");
    }

    DXGMMS_TRACE("DxgkMmsUnregister: complete\n");
}
