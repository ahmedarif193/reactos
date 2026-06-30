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
#include <ntstrsafe.h>

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
    ChildExt->Connected      = FALSE;
    ChildExt->EdidValid      = FALSE;
    InitializeListHead(&ChildExt->ListEntry);

    /*
     * Determine initial connection state.  For HpdAwarenessAlwaysConnected
     * children (e.g. integrated LCD panels), mark connected immediately.
     * For polled/interruptible children, query the miniport.
     */
    if (Descriptor->ChildCapabilities.HpdAwareness == HpdAwarenessAlwaysConnected)
    {
        ChildExt->Connected = TRUE;
    }
    else if (Descriptor->ChildCapabilities.HpdAwareness == HpdAwarenessPolled ||
             Descriptor->ChildCapabilities.HpdAwareness == HpdAwarenessInterruptible)
    {
        /*
         * Call DxgkDdiQueryChildStatus to check if a display is connected.
         */
        if (Adapter->MiniportContext->InitData.s.DxgkDdiQueryChildStatus != NULL)
        {
            DXGK_CHILD_STATUS ChildStatus;
            RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));
            ChildStatus.Type     = StatusConnection;
            ChildStatus.ChildUid = Descriptor->ChildUid;

            Status = Adapter->MiniportContext->InitData.s.DxgkDdiQueryChildStatus(
                         Adapter->MiniportDeviceContext,
                         &ChildStatus,
                         TRUE /* NonDestructiveOnly */);

            if (NT_SUCCESS(Status))
            {
                ChildExt->Connected = ChildStatus.HotPlug.Connected;
            }
            else
            {
                DXGKRNL_WARN("DxgkpCreateChildPdo: DxgkDdiQueryChildStatus "
                             "failed 0x%08lX for ChildUid %lu\n",
                             Status, Descriptor->ChildUid);
                /* Assume disconnected on failure. */
                ChildExt->Connected = FALSE;
            }
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
    ULONG                      ChildRelationsSize;
    ULONG                      i;
    KIRQL                      OldIrql;
    NTSTATUS                   Status;

    PAGED_CODE();

    *Relations = NULL;

    DXGKRNL_TRACE("DxgkpQueryBusRelations: Adapter %p NumberOfChildren=%lu\n",
                  Adapter, Adapter->NumberOfChildren);

    if (Adapter->NumberOfChildren == 0)
    {
        /*
         * No children reported by the miniport.  Return an empty
         * DEVICE_RELATIONS (Count=0).
         */
        PDEVICE_RELATIONS Rel;
        Rel = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
                  PagedPool,
                  sizeof(DEVICE_RELATIONS),
                  TAG_DXGK_RESOURCES);
        if (Rel == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        Rel->Count = 0;
        *Relations = Rel;
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
    Status = Adapter->MiniportContext->InitData.s.DxgkDdiQueryChildRelations(
                 Adapter->MiniportDeviceContext,
                 ChildRelations,
                 ChildRelationsSize);

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpQueryBusRelations: DxgkDdiQueryChildRelations "
                     "failed 0x%08lX\n", Status);
        ExFreePoolWithTag(ChildRelations, TAG_DXGK_RESOURCES);
        return Status;
    }

    /*
     * Save the child descriptors in the adapter for later use by
     * DxgkDdiQueryChildStatus and EDID queries.
     */
    if (Adapter->ChildDescriptors != NULL)
    {
        ExFreePoolWithTag(Adapter->ChildDescriptors, TAG_DXGK_RESOURCES);
    }
    Adapter->ChildDescriptors = ChildRelations;

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
        BOOLEAN Found = FALSE;

        /* Skip uninitialised entries (sentinel). */
        if (Desc->ChildDeviceType == TypeUninitialized)
            continue;

        DXGKRNL_TRACE("DxgkpQueryBusRelations: child[%lu] Type=%d "
                      "ChildUid=%lu Hpd=%d\n",
                      i, Desc->ChildDeviceType, Desc->ChildUid,
                      Desc->ChildCapabilities.HpdAwareness);

        /*
         * Check if a PDO already exists for this ChildUid.
         */
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        for (Entry  = Adapter->ChildListHead.Flink;
             Entry != &Adapter->ChildListHead;
             Entry  = Entry->Flink)
        {
            ExistingChild = CONTAINING_RECORD(Entry,
                                              DXGK_CHILD_PDO_EXTENSION,
                                              ListEntry);
            if (ExistingChild->Descriptor.ChildUid == Desc->ChildUid)
            {
                Found = TRUE;
                /* Update the descriptor in case the miniport changed it. */
                ExistingChild->Descriptor = *Desc;
                ExistingChild->Present    = TRUE;
                break;
            }
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

        if (!Found)
        {
            PDXGK_CHILD_PDO_EXTENSION NewChild = NULL;

            Status = DxgkpCreateChildPdo(Adapter, Desc, &NewChild);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_ERR("DxgkpQueryBusRelations: DxgkpCreateChildPdo "
                             "failed 0x%08lX for ChildUid %lu\n",
                             Status, Desc->ChildUid);
                continue;
            }

            /* Insert into the adapter's child list. */
            KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
            InsertTailList(&Adapter->ChildListHead, &NewChild->ListEntry);
            Adapter->ChildPdoCount++;
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        }
    }

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
        for (Entry  = Adapter->ChildListHead.Flink;
             Entry != &Adapter->ChildListHead;
             Entry  = Entry->Flink)
        {
            PDXGK_CHILD_PDO_EXTENSION Child =
                CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);
            if (Child->Present)
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
        for (Entry  = Adapter->ChildListHead.Flink;
             Entry != &Adapter->ChildListHead && Idx < Count;
             Entry  = Entry->Flink)
        {
            PDXGK_CHILD_PDO_EXTENSION Child =
                CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);
            if (Child->Present)
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
             * Instance ID must be unique among siblings.  Use the
             * ChildUid from the descriptor to guarantee uniqueness.
             */
            RtlStringCchPrintfW(Buffer,
                                sizeof(Buffer) / sizeof(WCHAR),
                                L"4&%08lX&0&%02lu",
                                ChildExt->Descriptor.ChildUid,
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
                DevCaps->UniqueID        = TRUE;
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
            ChildExt->Present = FALSE;
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
