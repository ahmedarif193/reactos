/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PnP dispatch for the dxgkrnl control device object and
 *              child PDO enumeration (monitor/output device objects)
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * This file contains:
 *   1. DxgkDispatchPnp / DxgkDispatchPower — handlers for dxgkrnl's own
 *      control device (\Device\DxgKrnl).
 *   2. DxgkpQueryBusRelations — builds DEVICE_RELATIONS for the GPU FDO
 *      by calling DxgkDdiQueryChildRelations on the miniport.
 *   3. DxgkpCreateChildPdo / DxgkpDeleteChildPdo — child PDO lifecycle.
 *   4. DxgkpChildPdoPnpDispatch — PnP IRP handler for child PDOs
 *      (IRP_MN_QUERY_ID, IRP_MN_QUERY_DEVICE_RELATIONS, etc.).
 *
 * Miniport FDO PnP/Power is handled by DxgkpMiniportPnpDispatch /
 * DxgkpMiniportPowerDispatch in adapter.c.
 */

#include "dxgkrnl_private.h"
#include "pnp.h"
#include "vidmm.h"
#include "vidsch.h"
#include "hotplug_work_core.h"
#include <ntstrsafe.h>

typedef struct _DXGKP_POLL_CHILD_SNAPSHOT
{
    ULONG ChildUid;
    DXGK_CHILD_DEVICE_HPD_AWARENESS HpdAwareness;
} DXGKP_POLL_CHILD_SNAPSHOT, *PDXGKP_POLL_CHILD_SNAPSHOT;

typedef struct _DXGKP_POLL_CHILDREN_WORK
{
    PIO_WORKITEM WorkItem;
    PDXGKRNL_ADAPTER Adapter;
    BOOLEAN NonDestructiveOnly;
    BOOLEAN PollInterruptible;
    BOOLEAN DisableModeReset;
} DXGKP_POLL_CHILDREN_WORK, *PDXGKP_POLL_CHILDREN_WORK;

static BOOLEAN
DxgkpChildEligibleForPoll(
    _In_ DXGK_CHILD_DEVICE_HPD_AWARENESS HpdAwareness,
    _In_ BOOLEAN PollInterruptible)
{
    return HpdAwareness == HpdAwarenessPolled || (PollInterruptible && HpdAwareness == HpdAwarenessInterruptible);
}

static NTSTATUS
DxgkpSnapshotChildrenForPoll(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN PollInterruptible,
    _Outptr_result_buffer_maybenull_(*ChildCount) PDXGKP_POLL_CHILD_SNAPSHOT *Children,
    _Out_ PULONG ChildCount)
{
    PDXGKP_POLL_CHILD_SNAPSHOT Snapshot = NULL;
    PLIST_ENTRY Entry;
    LONG64 ExpectedEpoch;
    KIRQL OldIrql;
    NTSTATUS Status;
    ULONG Capacity = 0;
    ULONG Count = 0;

    *Children = NULL;
    *ChildCount = 0;
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    ExpectedEpoch = Adapter->ChildEnumerationEpoch;
    Status = DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, ExpectedEpoch);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        return Status;
    }
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (Child->Present && Child->EnumerationEpoch == ExpectedEpoch && DxgkpChildEligibleForPoll(Child->Descriptor.ChildCapabilities.HpdAwareness, PollInterruptible))
            ++Capacity;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    if (Capacity == 0)
        return STATUS_SUCCESS;
    if (Capacity > MAXULONG / sizeof(*Snapshot))
        return STATUS_INTEGER_OVERFLOW;
    Snapshot = ExAllocatePoolWithTag(PagedPool, Capacity * sizeof(*Snapshot), TAG_DXGK_RESOURCES);
    if (Snapshot == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    Status = DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, ExpectedEpoch);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        ExFreePoolWithTag(Snapshot, TAG_DXGK_RESOURCES);
        return Status;
    }
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead && Count < Capacity; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (!Child->Present || Child->EnumerationEpoch != ExpectedEpoch || !DxgkpChildEligibleForPoll(Child->Descriptor.ChildCapabilities.HpdAwareness, PollInterruptible))
            continue;
        Snapshot[Count].ChildUid = Child->Descriptor.ChildUid;
        Snapshot[Count].HpdAwareness = Child->Descriptor.ChildCapabilities.HpdAwareness;
        ++Count;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    *Children = Snapshot;
    *ChildCount = Count;
    return STATUS_SUCCESS;
}

static BOOLEAN
DxgkpPublishChildConnection(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG ChildUid,
    _In_ DXGK_CHILD_DEVICE_HPD_AWARENESS ExpectedHpdAwareness,
    _In_ BOOLEAN ValidateHpdAwareness,
    _In_ BOOLEAN Connected)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Changed = FALSE;
    BOOLEAN NewConnected = Connected ? TRUE : FALSE;

    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (Child->Descriptor.ChildUid != ChildUid || Child->Descriptor.ChildDeviceType != TypeVideoOutput || (ValidateHpdAwareness && Child->Descriptor.ChildCapabilities.HpdAwareness != ExpectedHpdAwareness))
            continue;
        if (Child->Connected != NewConnected)
        {
            Child->Connected = NewConnected;
            Child->EdidValid = FALSE;
            Child->StateGeneration++;
            (VOID)DxgkHotPlugWorkCorePublishLocked(&Adapter->HotPlugGeneration);
            Changed = TRUE;
        }
        break;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    return Changed;
}

BOOLEAN
DxgkPnpPublishChildConnection(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG ChildUid,
    _In_ BOOLEAN Connected)
{
    if (Adapter == NULL)
        return FALSE;
    return DxgkpPublishChildConnection(Adapter, ChildUid, HpdAwarenessUninitialized, FALSE, Connected);
}

VOID
DxgkPnpBeginChildEnumerationEpoch(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGK_CHILD_DESCRIPTOR OldDescriptors;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();
    if (Adapter == NULL)
        return;
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    OldDescriptors = Adapter->ChildDescriptors;
    Adapter->ChildDescriptors = NULL;
    (VOID)DxgkHotPlugWorkCoreBeginEnumerationEpochLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated);
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        Child->Present = FALSE;
        Child->Connected = FALSE;
        Child->EdidValid = FALSE;
        Child->EnumerationEpoch = 0;
        Child->StateGeneration++;
    }
    (VOID)DxgkHotPlugWorkCorePublishLocked(&Adapter->HotPlugGeneration);
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    if (OldDescriptors != NULL)
        ExFreePoolWithTag(OldDescriptors, TAG_DXGK_RESOURCES);
}

static NTSTATUS
DxgkpPollDisplayChildrenAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN NonDestructiveOnly,
    _In_ BOOLEAN PollInterruptible,
    _In_ BOOLEAN DisableModeReset)
{
    PDXGKP_POLL_CHILD_SNAPSHOT Children = NULL;
    PDXGKDDI_QUERY_CHILD_STATUS QueryChildStatus;
    NTSTATUS RebuildStatus;
    NTSTATUS Status;
    ULONG ChildCount = 0;
    ULONG Index;
    BOOLEAN ConnectionChanged = FALSE;

    PAGED_CODE();
    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DEVICE_REMOVED;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    if (Adapter->DevicePowerState != PowerDeviceD0 || Adapter->MiniportDeviceStopped)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    Status = DxgkpSnapshotChildrenForPoll(Adapter, PollInterruptible, &Children, &ChildCount);
    if (!NT_SUCCESS(Status) || ChildCount == 0)
        goto Cleanup;
    QueryChildStatus = DXGK_CB(Adapter, DxgkDdiQueryChildStatus);
    if (QueryChildStatus == NULL)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }
    Status = STATUS_SUCCESS;
    for (Index = 0; Index < ChildCount; ++Index)
    {
        DXGK_CHILD_STATUS ChildStatus;

        RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));
        ChildStatus.Type = StatusConnection;
        ChildStatus.ChildUid = Children[Index].ChildUid;
        _SEH2_TRY
        {
            Status = QueryChildStatus(Adapter->MiniportDeviceContext, &ChildStatus, NonDestructiveOnly);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status))
            break;
        if (DxgkpPublishChildConnection(Adapter, Children[Index].ChildUid, Children[Index].HpdAwareness, TRUE, ChildStatus.HotPlug.Connected))
            ConnectionChanged = TRUE;
    }

Cleanup:
    DxgkEndKmdTransaction(Adapter);
    if (Children != NULL)
        ExFreePoolWithTag(Children, TAG_DXGK_RESOURCES);
    if (ConnectionChanged)
    {
        RebuildStatus = STATUS_SUCCESS;
        if (!DisableModeReset)
            RebuildStatus = DxgkVidPnRebuildForHotPlug(Adapter);
        IoInvalidateDeviceRelations(Adapter->PhysicalDeviceObject, BusRelations);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(RebuildStatus))
            Status = RebuildStatus;
    }
    return Status;
}

static VOID
NTAPI
DxgkpPollDisplayChildrenWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PDXGKP_POLL_CHILDREN_WORK Work = Context;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(DeviceObject);
    Status = DxgkpPollDisplayChildrenAdapter(Work->Adapter, Work->NonDestructiveOnly, Work->PollInterruptible, Work->DisableModeReset);
    if (!NT_SUCCESS(Status))
        DXGKRNL_WARN("DxgkpPollDisplayChildrenWorker: adapter %p poll failed 0x%08lX\n", Work->Adapter, Status);
    IoFreeWorkItem(Work->WorkItem);
    DxgkDereferenceAdapter(Work->Adapter);
    ExFreePoolWithTag(Work, TAG_DXGK_RESOURCES);
}

static NTSTATUS
DxgkpAllocatePollDisplayChildrenWork(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ CONST D3DKMT_POLLDISPLAYCHILDREN *PollRequest,
    _Outptr_ PDXGKP_POLL_CHILDREN_WORK *OutWork)
{
    PDXGKP_POLL_CHILDREN_WORK Work;

    *OutWork = NULL;
    Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), TAG_DXGK_RESOURCES);
    if (Work == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Work, sizeof(*Work));
    Work->WorkItem = IoAllocateWorkItem(Adapter->FunctionalDeviceObject);
    if (Work->WorkItem == NULL)
    {
        ExFreePoolWithTag(Work, TAG_DXGK_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Work->Adapter = Adapter;
    Work->NonDestructiveOnly = PollRequest->NonDestructiveOnly;
    Work->PollInterruptible = PollRequest->PollInterruptible;
    Work->DisableModeReset = PollRequest->DisableModeReset;
    *OutWork = Work;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkpPollDisplayChildrenRequest(
    _In_ CONST D3DKMT_POLLDISPLAYCHILDREN *PollRequest)
{
    PDXGKRNL_ADAPTER Adapters[MAX_ENUM_ADAPTERS] = {0};
    PDXGKRNL_ADAPTER RequestedAdapter = NULL;
    PDXGKP_POLL_CHILDREN_WORK WorkItems[MAX_ENUM_ADAPTERS] = {0};
    NTSTATUS FirstFailure = STATUS_SUCCESS;
    NTSTATUS Status;
    ULONG AdapterCount;
    ULONG Index;

    PAGED_CODE();
    /* Windows 11 polices neither the reserved flag bits nor a DisableModeReset
     * without SynchronousPolling; both are accepted. */
    if (PollRequest == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkReferenceAdapterByHandle(PollRequest->hAdapter, PsGetCurrentProcess(), &RequestedAdapter);
    if (!NT_SUCCESS(Status))
    {
        /* The handle lookup already reports an unusable D3DKMT handle as a
         * bad parameter, which is what Windows 11 returns here. */
        return Status == STATUS_DELETE_PENDING ? STATUS_DEVICE_REMOVED : Status;
    }
    if (PollRequest->PollAllAdapters)
    {
        AdapterCount = DxgkReferenceStartedAdapters(Adapters, RTL_NUMBER_OF(Adapters));
        if (RequestedAdapter->State != DxgkAdapterStateStarted)
        {
            for (Index = 0; Index < AdapterCount; ++Index)
                DxgkDereferenceAdapter(Adapters[Index]);
            DxgkDereferenceAdapter(RequestedAdapter);
            return STATUS_DEVICE_REMOVED;
        }
        DxgkDereferenceAdapter(RequestedAdapter);
        if (AdapterCount == 0)
            return STATUS_DEVICE_REMOVED;
    }
    else
    {
        Adapters[0] = RequestedAdapter;
        AdapterCount = 1;
    }
    if (PollRequest->SynchronousPolling)
    {
        for (Index = 0; Index < AdapterCount; ++Index)
        {
            Status = DxgkpPollDisplayChildrenAdapter(Adapters[Index], PollRequest->NonDestructiveOnly, PollRequest->PollInterruptible, PollRequest->DisableModeReset);
            if (NT_SUCCESS(FirstFailure) && !NT_SUCCESS(Status))
                FirstFailure = Status;
            DxgkDereferenceAdapter(Adapters[Index]);
        }
        return FirstFailure;
    }
    for (Index = 0; Index < AdapterCount; ++Index)
    {
        Status = DxgkpAllocatePollDisplayChildrenWork(Adapters[Index], PollRequest, &WorkItems[Index]);
        if (!NT_SUCCESS(Status))
        {
            FirstFailure = Status;
            break;
        }
    }
    if (!NT_SUCCESS(FirstFailure))
    {
        for (Index = 0; Index < AdapterCount; ++Index)
        {
            if (WorkItems[Index] != NULL)
            {
                IoFreeWorkItem(WorkItems[Index]->WorkItem);
                ExFreePoolWithTag(WorkItems[Index], TAG_DXGK_RESOURCES);
            }
            DxgkDereferenceAdapter(Adapters[Index]);
        }
        return FirstFailure;
    }
    for (Index = 0; Index < AdapterCount; ++Index)
        IoQueueWorkItem(WorkItems[Index]->WorkItem, DxgkpPollDisplayChildrenWorker, DelayedWorkQueue, WorkItems[Index]);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * Control device dispatchers (unchanged)
 * ====================================================================== */

/*
 * DxgkDispatchPnp
 *
 * PnP IRP handler for the dxgkrnl control device.
 * The control device is not a real PnP device; complete all PnP IRPs with
 * STATUS_SUCCESS without forwarding (there is no lower device object).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    /* Route \Device\Video0 PnP IRPs to the display handler */
    if (DxgkDisplayDispatchPnp(DeviceObject, Irp))
        return STATUS_SUCCESS;

    DXGKRNL_TRACE("DxgkDispatchPnp: MinorFunction=%u\n",
                  IoGetCurrentIrpStackLocation(Irp)->MinorFunction);

    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/*
 * DxgkDispatchPower
 *
 * Power IRP handler for the dxgkrnl control device.
 * Complete power IRPs with STATUS_SUCCESS; start the next power IRP.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
NTSTATUS
NTAPI
DxgkDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * Child PDO creation and deletion
 * ====================================================================== */

/*
 * DxgkpCreateChildPdo
 *
 * Allocate and initialise a PDO for one child device reported by the
 * miniport's DxgkDdiQueryChildRelations callback.  The PDO is created
 * under the miniport's DriverObject (same driver that owns the FDO)
 * so that IRP_MJ_PNP dispatches to DxgkpMiniportPnpDispatch.
 *
 * The caller must insert the returned extension into the adapter's
 * ChildListHead under ChildListLock.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkpCreateChildPdo(
    _In_  PDXGKRNL_ADAPTER          Adapter,
    _In_  PDXGK_CHILD_DESCRIPTOR    Descriptor,
    _In_  BOOLEAN                   ConnectionKnown,
    _In_  BOOLEAN                   Connected,
    _In_  ULONG64                   EnumerationEpoch,
    _Out_ PDXGK_CHILD_PDO_EXTENSION *ChildExtension)
{
    PDEVICE_OBJECT              Pdo = NULL;
    PDXGK_CHILD_PDO_EXTENSION  ChildExt;
    NTSTATUS                    Status;

    PAGED_CODE();

    *ChildExtension = NULL;

    /*
     * Create the PDO using the FDO's DriverObject.  The PnP manager will
     * route IRPs to the same dispatch table, and DxgkpMiniportPnpDispatch
     * will distinguish child PDOs from the FDO via the Signature field.
     */
    Status = IoCreateDevice(
                 Adapter->FunctionalDeviceObject->DriverObject,
                 sizeof(DXGK_CHILD_PDO_EXTENSION),
                 NULL,                      /* no device name for PDOs */
                 FILE_DEVICE_UNKNOWN,
                 FILE_DEVICE_SECURE_OPEN,
                 FALSE,
                 &Pdo);

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpCreateChildPdo: IoCreateDevice failed 0x%08lX\n",
                     Status);
        return Status;
    }

    ChildExt = (PDXGK_CHILD_PDO_EXTENSION)Pdo->DeviceExtension;
    RtlZeroMemory(ChildExt, sizeof(*ChildExt));

    ChildExt->Signature      = DXGK_CHILD_PDO_SIGNATURE;
    ChildExt->ParentAdapter  = Adapter;
    ChildExt->DeviceObject   = Pdo;
    ChildExt->Descriptor     = *Descriptor;
    ChildExt->Present        = TRUE;
    ChildExt->Connected      = ConnectionKnown && Connected;
    ChildExt->StateGeneration = 1;
    ChildExt->EnumerationEpoch = EnumerationEpoch;
    ChildExt->EdidValid      = FALSE;
    InitializeListHead(&ChildExt->ListEntry);

    /*
     * Query the monitor descriptor (EDID base block) for connected video
     * outputs through the documented DxgkDdiQueryDeviceDescriptor DDI.
     * STATUS_MONITOR_NO_DESCRIPTOR is a normal answer (fixed panels,
     * firmware-negotiated links); anything else non-successful is logged.
     */
    if (ChildExt->Connected &&
        Descriptor->ChildDeviceType == TypeVideoOutput &&
        Adapter->MiniportContext->InitData.s.DxgkDdiQueryDeviceDescriptor != NULL &&
        DxgkAcquireKmdCall(Adapter))
    {
        DXGK_DEVICE_DESCRIPTOR DeviceDescriptor;

        RtlZeroMemory(&DeviceDescriptor, sizeof(DeviceDescriptor));
        DeviceDescriptor.DescriptorOffset = 0;
        DeviceDescriptor.DescriptorLength = sizeof(ChildExt->Edid);
        DeviceDescriptor.DescriptorBuffer = ChildExt->Edid;

        Status = Adapter->MiniportContext->InitData.s.DxgkDdiQueryDeviceDescriptor(
                     Adapter->MiniportDeviceContext,
                     Descriptor->ChildUid,
                     &DeviceDescriptor);
        DxgkReleaseKmdCall(Adapter);

        if (NT_SUCCESS(Status))
        {
            ChildExt->EdidValid = TRUE;
            DXGKRNL_TRACE("DxgkpCreateChildPdo: EDID captured for "
                          "ChildUid %lu\n", Descriptor->ChildUid);
        }
        else if (Status != STATUS_MONITOR_NO_DESCRIPTOR &&
                 Status != STATUS_NOT_SUPPORTED)
        {
            DXGKRNL_WARN("DxgkpCreateChildPdo: QueryDeviceDescriptor "
                         "failed 0x%08lX for ChildUid %lu\n",
                         Status, Descriptor->ChildUid);
        }
    }

    /*
     * Clear the DO_DEVICE_INITIALIZING bit so the PnP manager can
     * send IRPs to this PDO.
     */
    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    DXGKRNL_TRACE("DxgkpCreateChildPdo: PDO %p ChildUid=%lu Type=%d "
                  "Hpd=%d Connected=%d\n",
                  Pdo, Descriptor->ChildUid,
                  Descriptor->ChildDeviceType,
                  Descriptor->ChildCapabilities.HpdAwareness,
                  ChildExt->Connected);

    *ChildExtension = ChildExt;
    return STATUS_SUCCESS;
}

/*
 * DxgkpDeleteChildPdo
 *
 * Tear down and free a child PDO previously created by DxgkpCreateChildPdo.
 * The caller must have already removed the extension from the adapter's
 * ChildListHead.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkpDeleteChildPdo(
    _In_ PDXGK_CHILD_PDO_EXTENSION ChildExtension)
{
    PAGED_CODE();

    if (ChildExtension == NULL)
        return;

    DXGKRNL_TRACE("DxgkpDeleteChildPdo: PDO %p ChildUid=%lu\n",
                  ChildExtension->DeviceObject,
                  ChildExtension->Descriptor.ChildUid);

    if (ChildExtension->DeviceObject != NULL)
    {
        IoDeleteDevice(ChildExtension->DeviceObject);
        /* DeviceExtension (ChildExtension) is freed by IoDeleteDevice. */
    }
}

/* ========================================================================
 * Bus relations query
 * ====================================================================== */

#define DXGK_QUERY_CHILD_DMA_DRAIN_TIMEOUT_MS 1000

static NTSTATUS DxgkpWaitForQueryChildDmaIdle(_In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Delay;
    ULONG ElapsedMs = 0;

    Delay.QuadPart = -(LONGLONG)(10 * 10 * 1000);
    for (;;)
    {
        BOOLEAN Outstanding;
        KIRQL OldIrql;

        DxgkRetireCompletedDmaBuffers(Adapter);
        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        Outstanding = !IsListEmpty(&Adapter->SubmitDmaListHead) || !IsListEmpty(&Adapter->SubmitDmaRetireListHead) || InterlockedCompareExchange(&Adapter->SubmitDmaRetireActiveWorkers, 0, 0) != 0;
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        if (!Outstanding)
            return STATUS_SUCCESS;
        if (ElapsedMs >= DXGK_QUERY_CHILD_DMA_DRAIN_TIMEOUT_MS)
            return STATUS_TIMEOUT;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        ElapsedMs += 10;
    }
}

static BOOLEAN DxgkpCanRunQueryChildRelations(_In_ PDXGKRNL_ADAPTER Adapter)
{
    return Adapter->State == DxgkAdapterStateStarted && Adapter->DevicePowerState == PowerDeviceD0 && !Adapter->MiniportDeviceStopped && InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) != 0 && InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) == 0 && InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) == 0 && Adapter->MiniportContext != NULL && Adapter->MiniportDeviceContext != NULL && Adapter->MiniportContext->InitData.s.DxgkDdiQueryChildRelations != NULL;
}

static NTSTATUS DxgkpCallQueryChildRelationsLevel3(_In_ PDXGKRNL_ADAPTER Adapter, _Out_writes_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR ChildRelations, _In_ ULONG ChildRelationsSize)
{
    NTSTATUS ResumeStatus = STATUS_SUCCESS;
    NTSTATUS SchedulerStatus;
    NTSTATUS Status;
    BOOLEAN InterruptAdmissionClosed = FALSE;
    BOOLEAN KeepAdmissionBlocked = FALSE;
    BOOLEAN KmdExclusiveHeld = FALSE;
    BOOLEAN SchedulerSuspended = FALSE;
    BOOLEAN SubmitAdmissionClosed = FALSE;
    BOOLEAN VidMmQuiesced = FALSE;

    DxgkAcquireLevel3Transition(Adapter);
    if (InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 1, 0) != 0)
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    SubmitAdmissionClosed = TRUE;
    DxgkWaitForSubmitDmaReservations(Adapter);

    SchedulerStatus = VidSchSuspendScheduler(Adapter);
    if (NT_SUCCESS(SchedulerStatus))
        SchedulerSuspended = Adapter->VidSchContext != NULL;
    else if (SchedulerStatus != STATUS_NOT_SUPPORTED)
    {
        Status = SchedulerStatus;
        goto Cleanup;
    }

    Status = DxgkpWaitForQueryChildDmaIdle(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    DxgkBeginKmdExclusive(Adapter);
    KmdExclusiveHeld = TRUE;
    DxgkVidMmQuiesceAdapter(Adapter);
    VidMmQuiesced = TRUE;
    if (!DxgkpCanRunQueryChildRelations(Adapter))
    {
        KeepAdmissionBlocked = TRUE;
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DxgkVidMmPrepareForIdle(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    InterruptAdmissionClosed = InterlockedCompareExchange(&Adapter->InterruptCallbacksBlocked, 1, 0) == 0;
    DxgkBlockInterruptCallbacks(Adapter);
    if (!DxgkpCanRunQueryChildRelations(Adapter))
    {
        KeepAdmissionBlocked = TRUE;
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        KeepAdmissionBlocked = TRUE;
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }

    Status = Adapter->MiniportContext->InitData.s.DxgkDdiQueryChildRelations(Adapter->MiniportDeviceContext, ChildRelations, ChildRelationsSize);
    DxgkReleaseMiniportCallback(Adapter);

Cleanup:
    if (!KeepAdmissionBlocked && !DxgkpCanRunQueryChildRelations(Adapter))
        KeepAdmissionBlocked = TRUE;
    if (!KeepAdmissionBlocked && VidMmQuiesced)
        DxgkVidMmResumeAdapter(Adapter);
    if (!KeepAdmissionBlocked && SchedulerSuspended)
    {
        ResumeStatus = VidSchResumeScheduler(Adapter);
        if (!NT_SUCCESS(ResumeStatus) && ResumeStatus != STATUS_NOT_SUPPORTED)
        {
            DXGKRNL_ERR("DxgkpQueryBusRelations: scheduler resume failed 0x%08lX; retaining blocked admission\n", ResumeStatus);
            DxgkVidMmQuiesceAdapter(Adapter);
            KeepAdmissionBlocked = TRUE;
            if (NT_SUCCESS(Status))
                Status = ResumeStatus;
        }
    }
    if (InterruptAdmissionClosed && !KeepAdmissionBlocked && !DxgkpCanRunQueryChildRelations(Adapter))
        KeepAdmissionBlocked = TRUE;
    if (InterruptAdmissionClosed && !KeepAdmissionBlocked)
        DxgkUnblockInterruptCallbacks(Adapter);
    if (KmdExclusiveHeld)
        DxgkEndKmdExclusive(Adapter, !KeepAdmissionBlocked);
    else if (KeepAdmissionBlocked)
    {
        DxgkBeginKmdExclusive(Adapter);
        DxgkVidMmQuiesceAdapter(Adapter);
        DxgkEndKmdExclusive(Adapter, FALSE);
    }
    if (KeepAdmissionBlocked)
        DxgkPresentBeginStop(Adapter);
    if (!KeepAdmissionBlocked && SubmitAdmissionClosed)
        InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
    DxgkReleaseLevel3Transition(Adapter);
    return Status;
}

static NTSTATUS
DxgkpQueryChildConnectionForEnumeration(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGK_CHILD_DESCRIPTOR Descriptor,
    _Out_ PBOOLEAN ConnectionKnown,
    _Out_ PBOOLEAN Connected)
{
    PDXGKDDI_QUERY_CHILD_STATUS QueryChildStatus;
    DXGK_CHILD_STATUS ChildStatus;
    NTSTATUS Status;

    *ConnectionKnown = FALSE;
    *Connected = FALSE;
    if (Descriptor->ChildDeviceType != TypeVideoOutput)
        return STATUS_SUCCESS;
    if (Descriptor->ChildCapabilities.HpdAwareness == HpdAwarenessAlwaysConnected)
    {
        *ConnectionKnown = TRUE;
        *Connected = TRUE;
        return STATUS_SUCCESS;
    }
    if (Descriptor->ChildCapabilities.HpdAwareness != HpdAwarenessPolled && Descriptor->ChildCapabilities.HpdAwareness != HpdAwarenessInterruptible)
        return STATUS_SUCCESS;
    QueryChildStatus = DXGK_CB(Adapter, DxgkDdiQueryChildStatus);
    if (QueryChildStatus == NULL)
        return STATUS_SUCCESS;
    RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));
    ChildStatus.Type = StatusConnection;
    ChildStatus.ChildUid = Descriptor->ChildUid;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    _SEH2_TRY
    {
        Status = QueryChildStatus(Adapter->MiniportDeviceContext, &ChildStatus, TRUE);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    if (NT_SUCCESS(Status))
    {
        *ConnectionKnown = TRUE;
        *Connected = ChildStatus.HotPlug.Connected ? TRUE : FALSE;
        return STATUS_SUCCESS;
    }
    DXGKRNL_WARN("DxgkpQueryBusRelations: QueryChildStatus failed 0x%08lX for ChildUid %lu; retaining the last interrupt observation\n", Status, Descriptor->ChildUid);
    return STATUS_SUCCESS;
}

/*
 * DxgkpQueryBusRelations
 *
 * Called from DxgkpMiniportPnpDispatch when the PnP manager sends
 * IRP_MN_QUERY_DEVICE_RELATIONS(BusRelations) to the GPU FDO.
 *
 * This function:
 *   1. Calls DxgkDdiQueryChildRelations on the miniport to get the
 *      child device descriptor array.
 *   2. For each child device reported (TypeVideoOutput or TypeOther),
 *      creates a PDO if one does not already exist for that ChildUid.
 *   3. Builds a DEVICE_RELATIONS struct referencing all current child
 *      PDOs.
 *
 * The DEVICE_RELATIONS buffer is allocated from PagedPool and must be
 * freed by the I/O manager (as per the IRP contract).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkpQueryBusRelations(
    _In_  PDXGKRNL_ADAPTER  Adapter,
    _Out_ PDEVICE_RELATIONS *Relations)
{
    PDXGK_CHILD_DESCRIPTOR     ChildRelations = NULL;
    PDXGK_CHILD_DESCRIPTOR     OldDescriptors = NULL;
    ULONG                      ChildRelationsSize;
    ULONG                      i;
    LONG64                     ExpectedEpoch;
    KIRQL                      OldIrql;
    NTSTATUS                   Status;
    BOOLEAN                    TopologyChanged = FALSE;

    PAGED_CODE();

    *Relations = NULL;

    DXGKRNL_TRACE("DxgkpQueryBusRelations: Adapter %p NumberOfChildren=%lu\n",
                  Adapter, Adapter->NumberOfChildren);

    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    ExpectedEpoch = Adapter->ChildEnumerationEpoch;
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    if (ExpectedEpoch == 0)
        return STATUS_DEVICE_NOT_READY;

    if (Adapter->NumberOfChildren == 0)
    {
        PDEVICE_RELATIONS Rel;
        PLIST_ENTRY Entry;

        Rel = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), TAG_DXGK_RESOURCES);
        if (Rel == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        if (Adapter->ChildEnumerationEpoch != ExpectedEpoch)
        {
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            ExFreePoolWithTag(Rel, TAG_DXGK_RESOURCES);
            return STATUS_RETRY;
        }
        OldDescriptors = Adapter->ChildDescriptors;
        Adapter->ChildDescriptors = NULL;
        for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
        {
            PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

            if (Child->Present || Child->Connected || Child->EdidValid || Child->EnumerationEpoch != ExpectedEpoch)
            {
                Child->Present = FALSE;
                Child->Connected = FALSE;
                Child->EdidValid = FALSE;
                Child->EnumerationEpoch = ExpectedEpoch;
                Child->StateGeneration++;
                TopologyChanged = TRUE;
            }
        }
        if (DxgkHotPlugWorkCorePublishEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, ExpectedEpoch))
        {
            (VOID)DxgkHotPlugWorkCorePublishLocked(&Adapter->HotPlugGeneration);
            TopologyChanged = TRUE;
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        if (OldDescriptors != NULL)
            ExFreePoolWithTag(OldDescriptors, TAG_DXGK_RESOURCES);
        Rel->Count = 0;
        *Relations = Rel;
        if (TopologyChanged)
            (VOID)DxgkVidPnQueueHotPlugRebuild(Adapter);
        return STATUS_SUCCESS;
    }

    /*
     * Allocate the child descriptor array.  The WDDM contract requires
     * (NumberOfChildren + 1) entries: the miniport fills entries [0..N-1]
     * and leaves entry [N] zeroed as a sentinel.
     */
    ChildRelationsSize = (Adapter->NumberOfChildren + 1) *
                         sizeof(DXGK_CHILD_DESCRIPTOR);

    ChildRelations = (PDXGK_CHILD_DESCRIPTOR)ExAllocatePoolWithTag(
                         PagedPool,
                         ChildRelationsSize,
                         TAG_DXGK_RESOURCES);
    if (ChildRelations == NULL)
    {
        DXGKRNL_ERR("DxgkpQueryBusRelations: failed to allocate "
                     "child descriptor array (%lu bytes)\n",
                     ChildRelationsSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(ChildRelations, ChildRelationsSize);

    /*
     * Call DxgkDdiQueryChildRelations.  The miniport fills in each
     * DXGK_CHILD_DESCRIPTOR element with the child type, capabilities,
     * and ChildUid.
     */
    Status = DxgkpCallQueryChildRelationsLevel3(Adapter, ChildRelations, ChildRelationsSize);

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpQueryBusRelations: DxgkDdiQueryChildRelations "
                     "failed 0x%08lX\n", Status);
        ExFreePoolWithTag(ChildRelations, TAG_DXGK_RESOURCES);
        return Status;
    }

    /*
     * Walk the descriptor array and create PDOs for each valid child.
     * If a PDO already exists for a given ChildUid (from a previous
     * enumeration), reuse it.
     */
    for (i = 0; i < Adapter->NumberOfChildren; i++)
    {
        PDXGK_CHILD_DESCRIPTOR Desc = &ChildRelations[i];
        PDXGK_CHILD_PDO_EXTENSION ExistingChild = NULL;
        PLIST_ENTRY Entry;
        ULONG64 BaselineGeneration = 0;
        BOOLEAN ConnectionKnown = FALSE;
        BOOLEAN Connected = FALSE;
        BOOLEAN Found = FALSE;

        /* Skip uninitialised entries (sentinel). */
        if (Desc->ChildDeviceType == TypeUninitialized)
            continue;

        DXGKRNL_TRACE("DxgkpQueryBusRelations: child[%lu] Type=%d "
                      "ChildUid=%lu Hpd=%d\n",
                      i, Desc->ChildDeviceType, Desc->ChildUid,
                      Desc->ChildCapabilities.HpdAwareness);

        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        if (Adapter->ChildEnumerationEpoch != ExpectedEpoch)
        {
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            ExFreePoolWithTag(ChildRelations, TAG_DXGK_RESOURCES);
            return STATUS_RETRY;
        }
        for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
        {
            ExistingChild = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);
            if (ExistingChild->Descriptor.ChildUid == Desc->ChildUid)
            {
                Found = TRUE;
                BaselineGeneration = ExistingChild->StateGeneration;
                break;
            }
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

        Status = DxgkpQueryChildConnectionForEnumeration(Adapter, Desc, &ConnectionKnown, &Connected);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(ChildRelations, TAG_DXGK_RESOURCES);
            return Status;
        }

        if (Found)
        {
            BOOLEAN ChildChanged = FALSE;

            KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
            if (Adapter->ChildEnumerationEpoch != ExpectedEpoch)
            {
                KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
                ExFreePoolWithTag(ChildRelations, TAG_DXGK_RESOURCES);
                return STATUS_RETRY;
            }
            ExistingChild = NULL;
            for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
            {
                PDXGK_CHILD_PDO_EXTENSION Candidate = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

                if (Candidate->Descriptor.ChildUid == Desc->ChildUid)
                {
                    ExistingChild = Candidate;
                    break;
                }
            }
            if (ExistingChild != NULL)
            {
                BOOLEAN NewConnected = ExistingChild->Connected;

                if (Desc->ChildCapabilities.HpdAwareness == HpdAwarenessAlwaysConnected)
                    NewConnected = TRUE;
                else if (ConnectionKnown && ExistingChild->StateGeneration == BaselineGeneration)
                    NewConnected = Connected;
                ChildChanged = RtlCompareMemory(&ExistingChild->Descriptor, Desc, sizeof(*Desc)) != sizeof(*Desc) || !ExistingChild->Present || ExistingChild->Connected != NewConnected || ExistingChild->EnumerationEpoch != ExpectedEpoch;
                ExistingChild->Descriptor = *Desc;
                ExistingChild->Present = TRUE;
                ExistingChild->Connected = NewConnected;
                ExistingChild->EnumerationEpoch = ExpectedEpoch;
                if (ChildChanged)
                {
                    ExistingChild->EdidValid = FALSE;
                    ExistingChild->StateGeneration++;
                    (VOID)DxgkHotPlugWorkCorePublishLocked(&Adapter->HotPlugGeneration);
                    TopologyChanged = TRUE;
                }
            }
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            if (ExistingChild != NULL)
                continue;
        }

        {
            PDXGK_CHILD_PDO_EXTENSION NewChild = NULL;
            BOOLEAN Duplicate = FALSE;
            BOOLEAN EpochCurrent;

            Status = DxgkpCreateChildPdo(Adapter, Desc, ConnectionKnown, Connected, ExpectedEpoch, &NewChild);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_ERR("DxgkpQueryBusRelations: DxgkpCreateChildPdo "
                             "failed 0x%08lX for ChildUid %lu\n",
                             Status, Desc->ChildUid);
                ExFreePoolWithTag(ChildRelations, TAG_DXGK_RESOURCES);
                return Status;
            }

            KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
            EpochCurrent = Adapter->ChildEnumerationEpoch == ExpectedEpoch;
            if (EpochCurrent)
            {
                for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
                {
                    PDXGK_CHILD_PDO_EXTENSION Candidate = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

                    if (Candidate->Descriptor.ChildUid == Desc->ChildUid)
                    {
                        Duplicate = TRUE;
                        break;
                    }
                }
                if (!Duplicate)
                {
                    InsertTailList(&Adapter->ChildListHead, &NewChild->ListEntry);
                    Adapter->ChildPdoCount++;
                    (VOID)DxgkHotPlugWorkCorePublishLocked(&Adapter->HotPlugGeneration);
                    TopologyChanged = TRUE;
                    NewChild = NULL;
                }
            }
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            if (NewChild != NULL)
                DxgkpDeleteChildPdo(NewChild);
            if (!EpochCurrent)
            {
                ExFreePoolWithTag(ChildRelations, TAG_DXGK_RESOURCES);
                return STATUS_RETRY;
            }
        }
    }

    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    {
        PLIST_ENTRY Entry;

        if (Adapter->ChildEnumerationEpoch != ExpectedEpoch)
        {
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            ExFreePoolWithTag(ChildRelations, TAG_DXGK_RESOURCES);
            return STATUS_RETRY;
        }
        OldDescriptors = Adapter->ChildDescriptors;
        Adapter->ChildDescriptors = ChildRelations;
        for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
        {
            PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);
            BOOLEAN Reported = FALSE;

            for (i = 0; i < Adapter->NumberOfChildren; ++i)
            {
                if (ChildRelations[i].ChildDeviceType != TypeUninitialized && ChildRelations[i].ChildUid == Child->Descriptor.ChildUid)
                {
                    Reported = TRUE;
                    break;
                }
            }
            if (!Reported && (Child->Present || Child->Connected || Child->EdidValid || Child->EnumerationEpoch != ExpectedEpoch))
            {
                Child->Present = FALSE;
                Child->Connected = FALSE;
                Child->EdidValid = FALSE;
                Child->EnumerationEpoch = ExpectedEpoch;
                Child->StateGeneration++;
                (VOID)DxgkHotPlugWorkCorePublishLocked(&Adapter->HotPlugGeneration);
                TopologyChanged = TRUE;
            }
        }
        if (DxgkHotPlugWorkCorePublishEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, ExpectedEpoch))
        {
            (VOID)DxgkHotPlugWorkCorePublishLocked(&Adapter->HotPlugGeneration);
            TopologyChanged = TRUE;
        }
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    ChildRelations = NULL;
    if (OldDescriptors != NULL)
        ExFreePoolWithTag(OldDescriptors, TAG_DXGK_RESOURCES);
    if (TopologyChanged)
        (VOID)DxgkVidPnQueueHotPlugRebuild(Adapter);

    /*
     * Build the DEVICE_RELATIONS structure.  Include all child PDOs
     * whose Present flag is TRUE.  Each PDO gets a reference (ObRef)
     * as required by the DEVICE_RELATIONS contract.
     */
    {
        ULONG Count = 0;
        ULONG Idx = 0;
        PDEVICE_RELATIONS Rel;
        PLIST_ENTRY Entry;

        /* Count present children. */
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        Status = DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, ExpectedEpoch);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            return Status;
        }
        for (Entry  = Adapter->ChildListHead.Flink;
             Entry != &Adapter->ChildListHead;
             Entry  = Entry->Flink)
        {
            PDXGK_CHILD_PDO_EXTENSION Child =
                CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);
            if (Child->Present && Child->EnumerationEpoch == ExpectedEpoch)
                Count++;
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

        DXGKRNL_TRACE("DxgkpQueryBusRelations: %lu present children\n",
                      Count);

        Rel = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
                  PagedPool,
                  FIELD_OFFSET(DEVICE_RELATIONS, Objects) +
                      Count * sizeof(PDEVICE_OBJECT),
                  TAG_DXGK_RESOURCES);

        if (Rel == NULL)
        {
            DXGKRNL_ERR("DxgkpQueryBusRelations: failed to allocate "
                         "DEVICE_RELATIONS for %lu children\n", Count);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Rel->Count = Count;

        /* Fill in the PDO array and take references. */
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        Status = DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, ExpectedEpoch);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            ExFreePoolWithTag(Rel, TAG_DXGK_RESOURCES);
            return Status;
        }
        for (Entry  = Adapter->ChildListHead.Flink;
             Entry != &Adapter->ChildListHead && Idx < Count;
             Entry  = Entry->Flink)
        {
            PDXGK_CHILD_PDO_EXTENSION Child =
                CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);
            if (Child->Present && Child->EnumerationEpoch == ExpectedEpoch)
            {
                Rel->Objects[Idx] = Child->DeviceObject;
                ObReferenceObject(Child->DeviceObject);
                Idx++;
            }
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

        *Relations = Rel;
    }

    return STATUS_SUCCESS;
}

/* ========================================================================
 * Child PDO PnP dispatch helpers
 * ====================================================================== */

/*
 * DxgkpChildCopyIdString
 *
 * Allocate a PagedPool WCHAR buffer and copy the given NUL-terminated
 * wide string into it.  Used to build IRP_MN_QUERY_ID responses.
 * Returns NULL on allocation failure.
 */
static PWCHAR
DxgkpChildCopyIdString(
    _In_ PCWSTR Source)
{
    SIZE_T Bytes;
    PWCHAR Buffer;

    Bytes = (wcslen(Source) + 1) * sizeof(WCHAR);
    Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool, Bytes, TAG_DXGK_RESOURCES);
    if (Buffer != NULL)
        RtlCopyMemory(Buffer, Source, Bytes);
    return Buffer;
}

/*
 * DxgkpChildCopyMultiSzIdString
 *
 * Allocate a PagedPool WCHAR buffer for a REG_MULTI_SZ style ID list
 * containing one entry followed by a double-NUL terminator.
 */
static PWCHAR
DxgkpChildCopyMultiSzIdString(
    _In_ PCWSTR Source)
{
    SIZE_T Len;
    SIZE_T Bytes;
    PWCHAR Buffer;

    Len   = wcslen(Source);
    Bytes = (Len + 2) * sizeof(WCHAR);   /* string + NUL + extra NUL */
    Buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool, Bytes, TAG_DXGK_RESOURCES);
    if (Buffer != NULL)
    {
        RtlCopyMemory(Buffer, Source, Len * sizeof(WCHAR));
        Buffer[Len]     = L'\0';
        Buffer[Len + 1] = L'\0';
    }
    return Buffer;
}

/*
 * DxgkpChildPdoQueryId
 *
 * Handle IRP_MN_QUERY_ID for a child PDO.  Returns the device ID,
 * instance ID, and hardware IDs expected by the PnP manager for
 * monitor child devices.
 *
 * Device ID format:  DISPLAY\Default_Monitor
 * Instance ID format: 4&<ChildUid_hex>&0&<ChildUid>
 * Hardware IDs:      MONITOR\Default_Monitor  (multi-sz)
 *
 * IRQL: PASSIVE_LEVEL
 */
static NTSTATUS
DxgkpChildPdoQueryId(
    _In_  PDXGK_CHILD_PDO_EXTENSION ChildExt,
    _In_  BUS_QUERY_ID_TYPE         IdType,
    _Out_ PVOID                     *Information)
{
    WCHAR Buffer[128];
    PWCHAR Result;

    *Information = NULL;

    switch (IdType)
    {
        case BusQueryDeviceID:
            /*
             * The Device ID identifies the type of device.  For WDDM
             * monitor children, this is "DISPLAY\Default_Monitor".
             */
            Result = DxgkpChildCopyIdString(L"DISPLAY\\Default_Monitor");
            if (Result == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            DXGKRNL_TRACE("DxgkpChildPdoQueryId: DeviceID = %ls\n", Result);
            *Information = Result;
            return STATUS_SUCCESS;

        case BusQueryInstanceID:
            /*
             * Sibling-unique only (ChildUid). Combined with UniqueID=FALSE
             * in the capabilities, PnP prepends a parent-unique prefix that
             * makes the full instance path globally unique — two adapters
             * both exposing monitor UID 1 must not collide.
             */
            RtlStringCchPrintfW(Buffer,
                                sizeof(Buffer) / sizeof(WCHAR),
                                L"%02lu",
                                ChildExt->Descriptor.ChildUid);

            Result = DxgkpChildCopyIdString(Buffer);
            if (Result == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            DXGKRNL_TRACE("DxgkpChildPdoQueryId: InstanceID = %ls\n", Result);
            *Information = Result;
            return STATUS_SUCCESS;

        case BusQueryHardwareIDs:
            /*
             * Hardware IDs are a REG_MULTI_SZ list of compatible IDs.
             * The PnP manager matches these against INF files.
             */
            Result = DxgkpChildCopyMultiSzIdString(L"MONITOR\\Default_Monitor");
            if (Result == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            DXGKRNL_TRACE("DxgkpChildPdoQueryId: HardwareID = %ls\n", Result);
            *Information = Result;
            return STATUS_SUCCESS;

        case BusQueryCompatibleIDs:
            /*
             * Return an empty multi-sz for compatible IDs.
             */
            Result = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                                   2 * sizeof(WCHAR),
                                                   TAG_DXGK_RESOURCES);
            if (Result == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            Result[0] = L'\0';
            Result[1] = L'\0';
            *Information = Result;
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

/*
 * DxgkpChildPdoPnpDispatch
 *
 * Handle IRP_MJ_PNP for a child PDO (monitor or other output device).
 * This function is called from DxgkpMiniportPnpDispatch (adapter.c)
 * after it detects that the target device is a child PDO (via the
 * Signature check).
 *
 * Handles the following PnP minor codes:
 *   IRP_MN_QUERY_ID               — device/instance/hardware IDs
 *   IRP_MN_QUERY_DEVICE_RELATIONS — TargetDeviceRelation
 *   IRP_MN_QUERY_CAPABILITIES     — device capabilities
 *   IRP_MN_START_DEVICE           — succeed (PDO is self-contained)
 *   IRP_MN_STOP_DEVICE            — succeed
 *   IRP_MN_REMOVE_DEVICE          — clean up
 *   IRP_MN_SURPRISE_REMOVAL       — mark not present
 *   IRP_MN_QUERY_BUS_INFORMATION  — bus type info
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkpChildPdoPnpDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PDXGK_CHILD_PDO_EXTENSION ChildExt;
    PIO_STACK_LOCATION        Stack;
    NTSTATUS                  Status;

    PAGED_CODE();

    ChildExt = (PDXGK_CHILD_PDO_EXTENSION)DeviceObject->DeviceExtension;
    Stack    = IoGetCurrentIrpStackLocation(Irp);

    DXGKRNL_TRACE("DxgkpChildPdoPnpDispatch: PDO %p Minor=%u ChildUid=%lu\n",
                  DeviceObject, Stack->MinorFunction,
                  ChildExt->Descriptor.ChildUid);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
        {
            PVOID IdBuffer = NULL;
            Status = DxgkpChildPdoQueryId(
                         ChildExt,
                         Stack->Parameters.QueryId.IdType,
                         &IdBuffer);

            if (NT_SUCCESS(Status))
            {
                Irp->IoStatus.Information = (ULONG_PTR)IdBuffer;
            }
            else if (Status == STATUS_NOT_SUPPORTED)
            {
                /* Leave Information as-is for unsupported ID types. */
                Status = Irp->IoStatus.Status;
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            if (Stack->Parameters.QueryDeviceRelations.Type ==
                TargetDeviceRelation)
            {
                /*
                 * TargetDeviceRelation: return a single-element
                 * DEVICE_RELATIONS pointing at this PDO itself.
                 */
                PDEVICE_RELATIONS Rel;
                Rel = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
                          PagedPool,
                          sizeof(DEVICE_RELATIONS),
                          TAG_DXGK_RESOURCES);
                if (Rel == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
                else
                {
                    Rel->Count     = 1;
                    Rel->Objects[0] = DeviceObject;
                    ObReferenceObject(DeviceObject);
                    Irp->IoStatus.Information = (ULONG_PTR)Rel;
                    Status = STATUS_SUCCESS;
                }
            }
            else
            {
                /* BusRelations, EjectionRelations, etc. not applicable. */
                Status = Irp->IoStatus.Status;
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_QUERY_CAPABILITIES:
        {
            /*
             * Fill in minimal device capabilities for the child PDO.
             * Mark the device as non-removable (integrated display).
             */
            PDEVICE_CAPABILITIES DevCaps =
                Stack->Parameters.DeviceCapabilities.Capabilities;

            if (DevCaps != NULL)
            {
                DevCaps->LockSupported   = FALSE;
                DevCaps->EjectSupported  = FALSE;
                DevCaps->Removable       = FALSE;
                DevCaps->DockDevice      = FALSE;
                /* Instance ID is only sibling-unique: PnP must prepend the
                 * parent prefix (two adapters both expose monitor UID 1). */
                DevCaps->UniqueID        = FALSE;
                DevCaps->SilentInstall   = TRUE;
                DevCaps->RawDeviceOK     = FALSE;
                DevCaps->SurpriseRemovalOK = FALSE;

                DevCaps->Address         = ChildExt->Descriptor.ChildUid;
                DevCaps->UINumber        = ChildExt->Descriptor.ChildUid;

                /* All device power states supported. */
                DevCaps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
                DevCaps->DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
                DevCaps->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
                DevCaps->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
                DevCaps->DeviceState[PowerSystemHibernate] = PowerDeviceD3;
                DevCaps->DeviceState[PowerSystemShutdown]  = PowerDeviceD3;
            }

            Status = STATUS_SUCCESS;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_START_DEVICE:
        {
            DXGKRNL_TRACE("DxgkpChildPdoPnpDispatch: START_DEVICE ChildUid=%lu\n",
                          ChildExt->Descriptor.ChildUid);
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_STOP_DEVICE:
        {
            DXGKRNL_TRACE("DxgkpChildPdoPnpDispatch: STOP_DEVICE ChildUid=%lu\n",
                          ChildExt->Descriptor.ChildUid);
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_SURPRISE_REMOVAL:
        {
            PDXGKRNL_ADAPTER ParentAdapter = ChildExt->ParentAdapter;

            if (ParentAdapter != NULL)
            {
                KIRQL OldIrql;

                KeAcquireSpinLock(&ParentAdapter->ChildListLock, &OldIrql);
                if (ChildExt->Present)
                {
                    ChildExt->Present = FALSE;
                    ChildExt->Connected = FALSE;
                    ChildExt->EdidValid = FALSE;
                    ChildExt->StateGeneration++;
                    (VOID)DxgkHotPlugWorkCorePublishLocked(&ParentAdapter->HotPlugGeneration);
                }
                KeReleaseSpinLock(&ParentAdapter->ChildListLock, OldIrql);
                (VOID)DxgkVidPnQueueHotPlugRebuild(ParentAdapter);
            }
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_REMOVE_DEVICE:
        {
            DXGKRNL_TRACE("DxgkpChildPdoPnpDispatch: REMOVE_DEVICE ChildUid=%lu\n",
                          ChildExt->Descriptor.ChildUid);

            /*
             * Remove the child from the adapter's list and delete the PDO.
             * The adapter may already be gone (surprise removal path), so
             * check ParentAdapter validity.
             */
            if (ChildExt->ParentAdapter != NULL)
            {
                KIRQL OldIrql;
                KeAcquireSpinLock(&ChildExt->ParentAdapter->ChildListLock,
                                  &OldIrql);
                RemoveEntryList(&ChildExt->ListEntry);
                InitializeListHead(&ChildExt->ListEntry);
                if (ChildExt->ParentAdapter->ChildPdoCount > 0)
                    ChildExt->ParentAdapter->ChildPdoCount--;
                KeReleaseSpinLock(&ChildExt->ParentAdapter->ChildListLock,
                                  OldIrql);
            }

            Status = STATUS_SUCCESS;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);

            /* Delete the device object after completing the IRP. */
            IoDeleteDevice(DeviceObject);
            return Status;
        }

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        {
            /* No resources required for monitor child PDOs. */
            Irp->IoStatus.Information = 0;
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_QUERY_DEVICE_TEXT:
        {
            /*
             * Return a friendly device description text.  The PnP
             * manager uses this for display in Device Manager.
             */
            if (Stack->Parameters.QueryDeviceText.DeviceTextType ==
                DeviceTextDescription)
            {
                PCWSTR Text = L"Generic Non-PnP Monitor";
                SIZE_T Bytes = (wcslen(Text) + 1) * sizeof(WCHAR);
                PWCHAR Buf = (PWCHAR)ExAllocatePoolWithTag(
                                 PagedPool, Bytes, TAG_DXGK_RESOURCES);
                if (Buf != NULL)
                {
                    RtlCopyMemory(Buf, Text, Bytes);
                    Irp->IoStatus.Information = (ULONG_PTR)Buf;
                    Status = STATUS_SUCCESS;
                }
                else
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }
            else
            {
                Status = Irp->IoStatus.Status;
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        default:
        {
            /*
             * For all other PnP minor codes, complete with whatever
             * status is already set.  PDOs do not forward IRPs.
             */
            Status = Irp->IoStatus.Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
    }
}
