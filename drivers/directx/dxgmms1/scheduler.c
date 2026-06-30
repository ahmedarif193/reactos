/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU command scheduler for dxgmms1.sys
 * COPYRIGHT:   Copyright 2024-2026 ReactOS Team
 *
 * This file implements the GPU command scheduling callbacks:
 *   DxgkMmsScheduleCommand()   — enqueue a DMA command buffer
 *   DxgkMmsScheduleNext()      — dispatch the next ready command on a node
 *   DxgkMmsWaitForIdleGpu()    — block until all work is drained
 *   DxgkMmsCommandCompleted()  — notification of GPU buffer completion
 *
 * Phase-2 implementation: real ready-queue scheduling with priority bands.
 */

#include "dxgmms1_private.h"
#include <d3dkmddi.h>

/* DxgkDdiSubmitCommand function pointer type */
typedef NTSTATUS (NTAPI *PFN_DXGMMS_SUBMITCOMMAND)(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand);

/* -------------------------------------------------------------------------
 * DxgkpSubmitToHardware
 *
 * Internal: submit a command buffer to the GPU via the miniport DDI.
 * Called without Node->Lock held.  The caller must publish CmdBuf in
 * Node->RunningBuffer before calling this helper.
 * ------------------------------------------------------------------------- */
static NTSTATUS
DxgkpSubmitToHardware(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ PDXGMMS_NODE Node,
    _In_ PDXGMMS_COMMAND_BUFFER CmdBuf)
{
    PFN_DXGMMS_SUBMITCOMMAND PfnSubmit;
    DXGKARG_SUBMITCOMMAND SubmitArg;
    NTSTATUS Status;

    PfnSubmit = (PFN_DXGMMS_SUBMITCOMMAND)Adapter->DxgkDdiSubmitCommand;
    if (PfnSubmit == NULL)
    {
        DXGMMS_WARN("DxgkpSubmitToHardware: no miniport submit callback\n");
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&SubmitArg, sizeof(SubmitArg));
    SubmitArg.pDmaBuffer = CmdBuf->DmaBuffer;
    SubmitArg.DmaBufferSize = CmdBuf->DmaBufferSize;
    SubmitArg.pDmaBufferPrivateData = CmdBuf->DmaBufferPrivateData;
    SubmitArg.DmaBufferPrivateDataSize = CmdBuf->DmaBufferPrivateDataSize;
    SubmitArg.DmaBufferSubmissionStartOffset = 0;
    SubmitArg.DmaBufferSubmissionEndOffset = CmdBuf->DmaBufferSize;
    SubmitArg.SubmissionFenceId = (UINT)CmdBuf->SubmitFenceId;
    SubmitArg.NodeOrdinal = CmdBuf->NodeOrdinal;
    SubmitArg.EngineOrdinal = 0;
    SubmitArg.hContext = CmdBuf->ContextHandle;
    SubmitArg.Flags = CmdBuf->Flags;

    DxgkMmsTdrStart(Adapter, Node->NodeOrdinal);

    Status = PfnSubmit(Adapter->MiniportContext, &SubmitArg);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkpSubmitToHardware: DxgkDdiSubmitCommand failed 0x%08lX\n",
                    Status);
        return Status;
    }

    return Status;
}

static VOID
DxgkpFailRunningCommand(
    _In_ PDXGMMS_NODE Node,
    _In_ PDXGMMS_COMMAND_BUFFER CmdBuf)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Node->Lock, &OldIrql);

    if (Node->RunningBuffer == CmdBuf)
        Node->RunningBuffer = NULL;

    CmdBuf->IsCompleted = TRUE;

    KeReleaseSpinLock(&Node->Lock, OldIrql);

    ExFreePoolWithTag(CmdBuf, TAG_DXGMMS_CMD_BUF);

    DxgkMmsScheduleNext(Node);
}

/* -------------------------------------------------------------------------
 * DxgkMmsScheduleCommand
 *
 * Enqueue a DMA command buffer.  If the target node is idle, dispatch
 * immediately.  Otherwise queue in the appropriate priority band.
 *
 * IRQL: <= DISPATCH_LEVEL
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DxgkMmsScheduleCommand(
    _In_ PVOID AdapterContext,
    _In_ PVOID CommandBuffer)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter = (PDXGMMS_ADAPTER_CONTEXT)AdapterContext;
    PDXGMMS_COMMAND_BUFFER SrcBuf = (PDXGMMS_COMMAND_BUFFER)CommandBuffer;
    PDXGMMS_COMMAND_BUFFER CmdBuf;
    PDXGMMS_NODE Node;
    KIRQL OldIrql;
    ULONG Priority;
    NTSTATUS Status;
    BOOLEAN DispatchNow;
    ULONG LogNodeOrdinal;
    ULONG LogPriority;
    ULONG64 LogFenceId;

    if (Adapter == NULL || CommandBuffer == NULL)
    {
        DXGMMS_ERR("DxgkMmsScheduleCommand: invalid parameters\n");
        return STATUS_INVALID_PARAMETER;
    }

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (Adapter->Removed)
    {
        DXGMMS_WARN("DxgkMmsScheduleCommand: adapter already removed\n");
        return STATUS_DEVICE_REMOVED;
    }

    if (SrcBuf->NodeOrdinal >= Adapter->NumNodes)
        return STATUS_INVALID_PARAMETER;

    Priority = SrcBuf->Priority;
    if (Priority >= DXGMMS_PRIORITY_LEVELS)
        Priority = DXGMMS_PRIORITY_NORMAL;

    /* Allocate our own command buffer copy */
    CmdBuf = (PDXGMMS_COMMAND_BUFFER)ExAllocatePoolWithTag(
                  NonPagedPool, sizeof(DXGMMS_COMMAND_BUFFER), TAG_DXGMMS_CMD_BUF);
    if (CmdBuf == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(CmdBuf, SrcBuf, sizeof(DXGMMS_COMMAND_BUFFER));
    CmdBuf->Priority = Priority;
    CmdBuf->IsPreempted = FALSE;
    CmdBuf->IsCompleted = FALSE;
    InitializeListHead(&CmdBuf->QueueEntry);

    Node = &Adapter->Nodes[CmdBuf->NodeOrdinal];
    DispatchNow = FALSE;
    LogNodeOrdinal = CmdBuf->NodeOrdinal;
    LogPriority = Priority;
    LogFenceId = CmdBuf->SubmitFenceId;

    KeAcquireSpinLock(&Node->Lock, &OldIrql);

    KeClearEvent(&Node->IdleEvent);

    if (Node->RunningBuffer == NULL)
    {
        Node->RunningBuffer = CmdBuf;
        InterlockedExchange64(&Node->LastSubmittedFenceId,
                              CmdBuf->SubmitFenceId);
        DispatchNow = TRUE;
        Status = STATUS_SUCCESS;
    }
    else
    {
        /* Node busy — enqueue at tail of priority band */
        InsertTailList(&Node->ReadyQueue[Priority], &CmdBuf->QueueEntry);
        Status = STATUS_SUCCESS;
    }

    KeReleaseSpinLock(&Node->Lock, OldIrql);

    if (DispatchNow)
    {
        Status = DxgkpSubmitToHardware(Adapter, Node, CmdBuf);
        if (!NT_SUCCESS(Status))
            DxgkpFailRunningCommand(Node, CmdBuf);
    }

    DXGMMS_TRACE("DxgkMmsScheduleCommand: node=%u pri=%u fence=%llu %s\n",
                 LogNodeOrdinal, LogPriority, LogFenceId,
                 DispatchNow ? "dispatched" : "queued");

    return Status;
}

/* -------------------------------------------------------------------------
 * DxgkMmsScheduleNext
 *
 * Select and dispatch the highest-priority ready command buffer on the
 * given GPU engine node.  Called at DISPATCH_LEVEL from preemption
 * completion, command completion, and TDR reset paths.
 *
 * IRQL: <= DISPATCH_LEVEL
 * ------------------------------------------------------------------------- */
VOID
DxgkMmsScheduleNext(
    _In_ PDXGMMS_NODE Node)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter;
    PDXGMMS_COMMAND_BUFFER CmdBuf;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    LONG p;
    NTSTATUS Status;
    ULONG LogNodeOrdinal;
    ULONG LogPriority;
    ULONG64 LogFenceId;

    if (Node == NULL)
        return;

    Adapter = Node->AdapterContext;
    if (Adapter == NULL)
        return;

    KeAcquireSpinLock(&Node->Lock, &OldIrql);

    /* Don't dispatch if something is already running */
    if (Node->RunningBuffer != NULL)
    {
        KeReleaseSpinLock(&Node->Lock, OldIrql);
        return;
    }

    /* Find highest-priority non-empty queue */
    CmdBuf = NULL;
    for (p = DXGMMS_PRIORITY_LEVELS - 1; p >= 0; --p)
    {
        if (!IsListEmpty(&Node->ReadyQueue[p]))
        {
            Entry = RemoveHeadList(&Node->ReadyQueue[p]);
            CmdBuf = CONTAINING_RECORD(Entry, DXGMMS_COMMAND_BUFFER, QueueEntry);
            break;
        }
    }

    if (CmdBuf != NULL)
    {
        LogNodeOrdinal = Node->NodeOrdinal;
        LogPriority = CmdBuf->Priority;
        LogFenceId = CmdBuf->SubmitFenceId;

        Node->RunningBuffer = CmdBuf;
        KeClearEvent(&Node->IdleEvent);
        InterlockedExchange64(&Node->LastSubmittedFenceId,
                              CmdBuf->SubmitFenceId);
        KeReleaseSpinLock(&Node->Lock, OldIrql);

        Status = DxgkpSubmitToHardware(Adapter, Node, CmdBuf);
        if (!NT_SUCCESS(Status))
        {
            DxgkpFailRunningCommand(Node, CmdBuf);
            return;
        }

        DXGMMS_TRACE("DxgkMmsScheduleNext: node=%u dispatched fence=%llu pri=%u\n",
                     LogNodeOrdinal, LogFenceId, LogPriority);
        return;
    }
    else
    {
        /* All queues empty — node is idle */
        KeSetEvent(&Node->IdleEvent, IO_NO_INCREMENT, FALSE);
    }

    KeReleaseSpinLock(&Node->Lock, OldIrql);
    DxgkMmsTdrStop(Adapter, Node->NodeOrdinal);
}

/* -------------------------------------------------------------------------
 * DxgkMmsCommandCompleted
 *
 * Called from dxgkrnl's DPC handler when the GPU completes a DMA buffer.
 *
 * IRQL: DISPATCH_LEVEL
 * ------------------------------------------------------------------------- */
VOID
DxgkMmsCommandCompleted(
    _In_ PVOID  MmsContext,
    _In_ ULONG  NodeOrdinal,
    _In_ ULONG64 CompletedFenceId)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter = (PDXGMMS_ADAPTER_CONTEXT)MmsContext;
    PDXGMMS_NODE Node;
    PDXGMMS_COMMAND_BUFFER CmdBuf;
    KIRQL OldIrql;

    if (Adapter == NULL || NodeOrdinal >= Adapter->NumNodes)
        return;

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (Adapter->Removed)
        return;

    Node = &Adapter->Nodes[NodeOrdinal];

    KeAcquireSpinLock(&Node->Lock, &OldIrql);

    Node->CurrentFenceId = CompletedFenceId;
    InterlockedExchange64(&Node->LastCompletedFenceId, CompletedFenceId);

    CmdBuf = Node->RunningBuffer;
    if (CmdBuf != NULL && CmdBuf->SubmitFenceId <= CompletedFenceId)
    {
        CmdBuf->IsCompleted = TRUE;
        Node->RunningBuffer = NULL;
        ExFreePoolWithTag(CmdBuf, TAG_DXGMMS_CMD_BUF);
    }

    KeReleaseSpinLock(&Node->Lock, OldIrql);

    /* Dispatch next queued buffer */
    DxgkMmsScheduleNext(Node);
}

/* -------------------------------------------------------------------------
 * DxgkMmsWaitForIdleGpu
 *
 * Block until all queued and running work on the specified node completes.
 *
 * IRQL: PASSIVE_LEVEL
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DxgkMmsWaitForIdleGpu(
    _In_ PVOID AdapterContext,
    _In_ ULONG NodeOrdinal)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter = (PDXGMMS_ADAPTER_CONTEXT)AdapterContext;
    PDXGMMS_NODE Node;
    LARGE_INTEGER Timeout;
    BOOLEAN HasWork;
    KIRQL OldIrql;
    LONG p;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (NodeOrdinal >= Adapter->NumNodes)
        return STATUS_INVALID_PARAMETER;

    Node = &Adapter->Nodes[NodeOrdinal];
    Timeout.QuadPart = -50LL * 10000LL; /* 50ms poll interval */

    for (;;)
    {
        KeAcquireSpinLock(&Node->Lock, &OldIrql);

        HasWork = (Node->RunningBuffer != NULL);
        if (!HasWork)
        {
            for (p = 0; p < DXGMMS_PRIORITY_LEVELS; ++p)
            {
                if (!IsListEmpty(&Node->ReadyQueue[p]))
                {
                    HasWork = TRUE;
                    break;
                }
            }
        }

        KeReleaseSpinLock(&Node->Lock, OldIrql);

        if (!HasWork)
            return STATUS_SUCCESS;

        KeWaitForSingleObject(&Node->IdleEvent, Executive, KernelMode,
                              FALSE, &Timeout);
    }
}
